// ============================================================================
//  rin_expr_host.cpp — واجهة التقييم المعزول لمحرّك indsin (Indsin <-> Rin)
// ----------------------------------------------------------------------------
//  كان indsin يقيّم قيم خصائص @view بمُقيِّم مصغَّر (indsin::evalAttrExpr: أرقام/نصوص + - * /). هذه
//  الدوال تتيح له تفويض أي تعبير Rin آخر إلى المفسّر الحقيقي: عوامل المقارنة والمنطق، النفي، الفهرسة،
//  `.member`، استدعاء الدوال الأصلية و`fun` المعرَّفة، Set/Array/Map ... بدلالات اللغة نفسها.
//  يُضمَّن في نهاية rin_interpreter.cpp (مثل rin_set.cpp) فلا تغيير في ملفات البناء.
// ============================================================================
#include "rin_interpreter.h"

namespace rin {

bool Interpreter::lookupGlobal(const std::string& name, Value& out, const std::string& containerName) const {
    if (!containerName.empty()) {
        auto it = containers.find(containerName);
        if (it != containers.end() && it->second) {
            auto v = it->second->values.find(name);
            if (v != it->second->values.end()) { out = v->second; return true; }
        }
    }
    if (!globals) return false;
    auto v = globals->values.find(name);
    if (v == globals->values.end()) return false;
    out = v->second;
    return true;
}

bool Interpreter::isCallableName(const std::string& name) const {
    if (natives.find(name) != natives.end()) return true;
    if (globals) {
        auto v = globals->values.find(name);
        if (v != globals->values.end() && v->second.type == Value::Type::FUNCTION) return true;
    }
    return false;
}

bool Interpreter::evalExpression(const ExprPtr& e, const std::unordered_map<std::string, Value>& locals, Value& out,
                                 std::string& err, const std::string& containerName) {
    if (!e) { err = "empty expression"; return false; }
    EnvPtr parent = globals;
    if (!containerName.empty()) {
        auto it = containers.find(containerName);
        if (it != containers.end() && it->second) parent = it->second;
    }
    if (!parent) { err = "interpreter has no global environment"; return false; }
    auto env = std::make_shared<Environment>(parent);
    for (auto& kv : locals) env->define(kv.first, kv.second);
    try {
        out = evaluate(e, env);
        return true;
    } catch (const RinError& ex) {
        err = ex.message;
    } catch (const std::exception& ex) {
        err = ex.what();
    } catch (...) {
        err = "unknown error";
    }
    return false;
}

} // namespace rin

// ============================================================================
//  التقاط ناتج الطباعة — الأساس الذي يقوم عليه جسر <<...>> في indsin (docs/indsin_bridge.md)
// ----------------------------------------------------------------------------
//  captureOutput(fn, ...args)  → نص: كل ما طبعته fn (print/println...) أثناء استدعائها.
//  capture(fn, ...args)        → map {ok, output, result, error}: لا يرمي أبداً؛ الخطأ يظهر في الحقول.
//  indsin.run(name, ...args)   → نص جاهز للعرض: ناتج الطباعة (بلا سطر جديد أخير)، وإن لم تطبع الدالة شيئاً
//                                 فقيمتها المُعادة. مُخزَّنة بمفتاح (الاسم + الوسائط) فتُنفَّذ مرة واحدة لكل
//                                 وسائط مختلفة فقط — لذا هي الآمنة داخل قيم خصائص @view (نتيجة <<run f(x)>>)،
//                                 وتُعاد عند تغيّر وسيط warp. أخطاؤها تُعاد كنص "⚠ …" بدل إسقاط الواجهة.
//  fn: قيمة دالة، أو اسم (نص) لـ fun معرَّفة في المستوى الأعلى أو لدالة أصلية.
//  تُستعمل كل واحدة وحدها في أي برنامج Rin، لا في indsin فقط.
// ============================================================================
namespace rin {

Value Interpreter::captureCall(const Value& callee, std::vector<Value>& args, int line, std::string& captured) {
    const std::streamoff mark = output.tellp();
    auto restore = [&]() {
        std::string all = output.str();
        captured = all.size() > static_cast<size_t>(mark) ? all.substr(static_cast<size_t>(mark)) : std::string();
        all.resize(std::min(all.size(), static_cast<size_t>(mark)));
        output.str(all);
        output.clear();
        output.seekp(0, std::ios_base::end);
        if (streamMark_ > mark) streamMark_ = mark;
    };
    Value ret;
    try {
        if (callee.type == Value::Type::FUNCTION && callee.function) {
            ret = callFunction(callee.function, args, line);
        } else if (callee.type == Value::Type::STRING) {
            const Value* target = nullptr;
            if (globals) {
                auto it = globals->values.find(callee.str);
                if (it != globals->values.end() && it->second.type == Value::Type::FUNCTION && it->second.function) target = &it->second;
            }
            if (target) {
                ret = callFunction(target->function, args, line);
            } else {
                auto nit = natives.find(callee.str);
                if (nit == natives.end())
                    throw errWithReason(diag::Code::E0007_InvalidArguments, line,
                                        "no function named `" + callee.str + "` to call",
                                        "define it with `fun " + callee.str + "(...) { ... }` at the top level, or pass the function value itself");
                ret = nit->second(args, line);
            }
        } else {
            throw errWithReason(diag::Code::E0007_InvalidArguments, line,
                                "the first argument must be a function or a function name",
                                "got a " + callee.typeName() + " instead");
        }
    } catch (...) {
        restore();
        throw;
    }
    restore();
    return ret;
}

void Interpreter::registerNativesCapture() {
    natives["captureOutput"] = [this](std::vector<Value>& a, int line) -> Value {
        if (a.empty()) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "captureOutput() needs a function (or its name)", "usage: captureOutput(fn, arg1, arg2, ...)");
        std::vector<Value> rest(a.begin() + 1, a.end());
        std::string out;
        captureCall(a[0], rest, line, out);
        return Value::string(out);
    };
    natives["capture"] = [this](std::vector<Value>& a, int line) -> Value {
        if (a.empty()) throw errWithReason(diag::Code::E0007_InvalidArguments, line, "capture() needs a function (or its name)", "usage: capture(fn, arg1, arg2, ...)");
        std::vector<Value> rest(a.begin() + 1, a.end());
        std::string out, err;
        Value ret = Value::nil();
        bool ok = true;
        try { ret = captureCall(a[0], rest, line, out); }
        catch (const RinError& e) { ok = false; err = e.message; }
        auto m = std::make_shared<MapData>();
        m->push_back({Value::string("ok"), Value::boolean_(ok)});
        m->push_back({Value::string("output"), Value::string(out)});
        m->push_back({Value::string("result"), ret});
        m->push_back({Value::string("error"), Value::string(err)});
        return Value::makeMap(m);
    };
    natives["indsin.run"] = [this](std::vector<Value>& a, int line) -> Value {
        if (a.empty() || a[0].type != Value::Type::STRING)
            throw errWithReason(diag::Code::E0007_InvalidArguments, line, "indsin.run() needs a function name as a string", "usage: indsin.run(\"report\", arg1, ...)");
        std::string key = a[0].str;
        for (size_t i = 1; i < a.size(); ++i) key += std::string("\x1f") + a[i].typeName() + ":" + a[i].toDisplayString();
        auto hit = runCache_.find(key);
        if (hit != runCache_.end()) return Value::string(hit->second);
        std::vector<Value> rest(a.begin() + 1, a.end());
        std::string out, text;
        try {
            Value ret = captureCall(a[0], rest, line, out);
            while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
            text = out.empty() && ret.type != Value::Type::NIL ? ret.toDisplayString() : out;
        } catch (const RinError& e) {
            text = "\xE2\x9A\xA0 " + e.message;
        }
        runCache_[key] = text;
        return Value::string(text);
    };
}

} // namespace rin

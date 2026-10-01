// ============================================================================
//  rin_oop_bind.cpp — Rin 1.4: دوال الربط (binding)
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp بعد rin_oop_natives.cpp، ويُسجَّل من
//  Interpreter::registerNativesOopBind() (تُستدعى في نهاية registerNativesOop).
//
//  1) ربط الدوال بكائن (bound methods)
//       oop.bind(obj, "method")      -> دالة مربوطة بالكائن (self ثابتة) — تحترم private/protected
//       oop.bind(fn, obj)            -> دالة/lambda عادية تُربَط بكائن فتستطيع استخدام self
//       oop.bindAll(obj, [names]?)   -> قاموس {اسم: دالة مربوطة} (كل الدوال العامة إن لم تُحدَّد أسماء)
//       oop.unbind(fn) / oop.isBound(fn) / oop.boundTo(fn)
//       oop.callWith(fn, obj, [args]) / oop.apply(fn, [args])
//  2) التطبيق الجزئي
//       oop.partial(fn, a, b, ...)   -> دالة بوسائطها الأولى مثبَّتة
//       oop.curry(fn)                -> دالة تستقبل وسيطاً واحداً في كل نداء حتى يكتمل العدد
//  3) ربط الخصائص (data binding / observers)
//       oop.observe(obj, "field"|"*", fn(new, old, obj)) -> id
//       oop.bindProperty(src, "a", dst, "b", transform?) -> id    (اتجاه واحد + مزامنة فورية)
//       oop.bindTwoWay(a, "x", b, "y")                    -> [id, id]
//       oop.unobserve(id) / oop.unobserveAll(obj) / oop.observers(obj)
//  4) ربط الأحداث
//       oop.on(obj, "event", fn) -> id   |  oop.once(...)  |  oop.off(obj, "event"?)
//       oop.emit(obj, "event", args...) -> عدد المستمعين الذين استُدعوا   |  oop.listeners(obj, "event"?)
//
//  المراقبة تعمل على أي كتابة تمرّ عبر  obj.field = v  أو  oop.set/poke  أو setter (set name(v)).
//  تُطلَق فقط حين تتغيّر القيمة فعلاً (valuesEqual) — وهذا ما يمنع الحلقات اللانهائية في bindTwoWay.
//  الكائنات تُراقَب بـ weak_ptr: المراقب لا يُطيل عمر الكائن.
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <functional>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

// ---------------------------------------------------------------------------
// أدوات مشتركة
// ---------------------------------------------------------------------------
Value Interpreter::callFlexible(const Value& fn, std::vector<Value> args, int line) {
    if (fn.type == Value::Type::FUNCTION && fn.function) {
        size_t n = fn.function->declaration->params.size();
        args.resize(n); // يقصّ الزائد ويحشو الناقص بـ nil
        return callFunction(fn.function, args, line);
    }
    if (fn.type == Value::Type::INSTANCE && hasMagic(fn, "__call__")) return callValue(fn, args, line);
    throw diagErr(diag::Code::E0004_InvalidType, line, "expected a function but got a value of type `" + fn.typeName() + "`");
}

void Interpreter::notifyObservers(const Value& obj, const std::string& field, const Value& oldV, const Value& newV, int line) {
    if (obj.type != Value::Type::INSTANCE || !obj.instance) return;
    if (valuesEqual(oldV, newV) && oldV.type != Value::Type::INSTANCE) return; // لا تغيير فعلي
    // نسخة من المطابقين: callback قد يضيف/يزيل مراقبين أثناء التكرار.
    std::vector<OopObserver> hit;
    for (auto& o : observers_) {
        if (o.isEvent) continue;
        auto sp = o.obj.lock();
        if (!sp || sp != obj.instance) continue;
        if (o.field == "*" || o.field == field) hit.push_back(o);
    }
    for (auto& o : hit) {
        if (o.isBinding) {
            Value v = newV;
            if (o.transform.type != Value::Type::NIL) v = callFlexible(o.transform, {newV, oldV, obj}, line);
            writeMember(o.dst, o.dstField, v, nullptr, line);
        } else {
            callFlexible(o.fn, {newV, oldV, obj, Value::string(field)}, line);
        }
    }
}

// ---------------------------------------------------------------------------
// التسجيل
// ---------------------------------------------------------------------------
void Interpreter::registerNativesOopBind() {
    using namespace extra;
    using Args = std::vector<Value>;

    auto S = [](const std::string& s) { return Value::string(s); };
    auto N = [](double d) { return Value::num(d); };
    auto B = [](bool b) { return Value::boolean_(b); };
    auto needFn = [](const Value& v, const std::string& fn, int line) {
        if (v.type != Value::Type::FUNCTION || !v.function)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a function but got " + v.typeName());
    };
    auto needInst = [](const Value& v, const std::string& fn, int line) {
        if (v.type != Value::Type::INSTANCE || !v.instance)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an object but got " + v.typeName());
    };
    auto arrArgs = [](const Value& v, const std::string& fn, int line) -> std::vector<Value> {
        if (v.type == Value::Type::NIL) return {};
        if (v.type != Value::Type::ARRAY || !v.array)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an array of arguments but got " + v.typeName());
        return *v.array;
    };

    // دالة اصطناعية: params + جسم `return nativeName(args...)`، وقيم ملتقَطة في بيئتها.
    auto makeNativeBacked = [this](const std::vector<std::string>& params, const std::string& nativeName,
                                   const std::vector<std::string>& argVarNames,
                                   const std::vector<std::pair<std::string, Value>>& captured) -> Value {
        auto fn = std::make_shared<FunctionStmt>();
        fn->name = "<" + nativeName + ">";
        fn->params = params;
        fn->body = std::make_shared<BlockStmt>();
        auto call = std::make_shared<CallExpr>();
        call->callee = nativeName;
        for (auto& vn : argVarNames) {
            auto ve = std::make_shared<VariableExpr>();
            ve->name = vn;
            call->args.push_back(ve);
        }
        auto ret = std::make_shared<ReturnStmt>();
        ret->value = call;
        fn->body->statements.push_back(ret);
        auto callable = std::make_shared<Callable>();
        callable->declaration = fn;
        auto env = std::make_shared<Environment>(globals);
        for (auto& kv : captured) env->define(kv.first, kv.second);
        callable->closure = env;
        Value v;
        v.type = Value::Type::FUNCTION;
        v.function = callable;
        return v;
    };

    // ======================================================== 1) ربط الدوال بكائن
    // دالة (FUNCTION) + كائن -> نسخة بنفس الجسم، بيئتها فرع من بيئة الأصل (فتبقى لامبداتها ملتقِطة لمتغيّراتها)
    // مع self/__class__ معرَّفتين.
    auto bindFnToObj = [](const Value& fn, const Value& obj) -> Value {
        auto callable = std::make_shared<Callable>();
        callable->declaration = fn.function->declaration;
        auto env = std::make_shared<Environment>(fn.function->closure);
        env->define("self", obj);
        env->define("__class__", Value::string(obj.instance->className));
        callable->closure = env;
        Value v;
        v.type = Value::Type::FUNCTION;
        v.function = callable;
        return v;
    };

    natives["oop.bind"] = [this, needFn, needInst, bindFnToObj](Args& a, int line) -> Value {
        need("oop.bind", a, 2, 2, line);
        if (a[0].type == Value::Type::INSTANCE && a[1].type == Value::Type::STRING) {
            const Value& obj = a[0];
            const std::string& name = a[1].str;
            auto fIt = obj.instance->fields.find(name);
            if (fIt != obj.instance->fields.end() && fIt->second.type == Value::Type::FUNCTION) return fIt->second;
            std::string owner;
            auto m = findMethod(obj.instance->className, name, &owner);
            if (!m)
                throw errWithReason(diag::Code::E0006_UnknownFunction, line,
                                    "`" + obj.instance->className + "` has no method `" + name + "` to bind",
                                    "oop.bind(obj, \"name\") needs an existing method or a function-valued field");
            checkMemberAccess(owner, m->access, name, "method", nullptr, line); // من خارج أي صنف
            return bindMethod(obj, m, owner);
        }
        if (a[0].type == Value::Type::FUNCTION && a[1].type == Value::Type::INSTANCE) {
            needFn(a[0], "oop.bind", line); needInst(a[1], "oop.bind", line);
            return bindFnToObj(a[0], a[1]);
        }
        throw diagErr(diag::Code::E0004_InvalidType, line,
                      "'oop.bind' expects (object, \"method\") or (function, object) but got (" + a[0].typeName() + ", " + a[1].typeName() + ")");
    };

    natives["oop.bindAll"] = [this, needInst](Args& a, int line) -> Value {
        need("oop.bindAll", a, 1, 2, line);
        needInst(a[0], "oop.bindAll", line);
        std::vector<std::string> names;
        if (a.size() == 2) {
            for (auto& n : arr(a[1], "oop.bindAll", line)) names.push_back(n.toDisplayString());
        } else {
            Args margs{a[0], Value::boolean_(true)};
            Value all = natives["oop.methods"](margs, line);
            for (auto& n : *all.array) {
                const std::string& nm = n.str;
                if (nm == "init" || (nm.size() > 1 && nm[0] == '_')) continue; // لا مُنشئ ولا سحري/خاص بالاسم
                auto m = findMethod(a[0].instance->className, nm);
                if (m && !m->access.empty()) continue; // private/protected
                names.push_back(nm);
            }
        }
        MapData out;
        for (auto& nm : names) {
            Args one{a[0], Value::string(nm)};
            out.push_back({Value::string(nm), natives["oop.bind"](one, line)});
        }
        return newMap(std::move(out));
    };

    natives["oop.isBound"] = [needFn, B](Args& a, int line) -> Value {
        need("oop.isBound", a, 1, 1, line);
        needFn(a[0], "oop.isBound", line);
        return B(a[0].function->closure && a[0].function->closure->values.count("self") > 0);
    };
    natives["oop.boundTo"] = [needFn](Args& a, int line) -> Value {
        need("oop.boundTo", a, 1, 1, line);
        needFn(a[0], "oop.boundTo", line);
        if (a[0].function->closure) {
            auto it = a[0].function->closure->values.find("self");
            if (it != a[0].function->closure->values.end()) return it->second;
        }
        return Value::nil();
    };
    // unbind: دالة مربوطة بدالة الصنف -> نسخة بلا self (تُربَط لاحقاً بـ oop.bind(fn, otherObj)).
    natives["oop.unbind"] = [this, needFn](Args& a, int line) -> Value {
        need("oop.unbind", a, 1, 1, line);
        needFn(a[0], "oop.unbind", line);
        auto cl = a[0].function->closure;
        if (!cl || !cl->values.count("self")) return a[0];
        auto callable = std::make_shared<Callable>();
        callable->declaration = a[0].function->declaration;
        auto env = std::make_shared<Environment>(cl->parent ? cl->parent : globals);
        // نُبقي __class__/__method__ (لأجل super والصلاحيات) ونُسقط self فقط.
        for (const char* k : {"__class__", "__method__"}) {
            auto it = cl->values.find(k);
            if (it != cl->values.end()) env->define(k, it->second);
        }
        callable->closure = env;
        Value v;
        v.type = Value::Type::FUNCTION;
        v.function = callable;
        return v;
    };
    natives["oop.apply"] = [this, arrArgs](Args& a, int line) -> Value {
        need("oop.apply", a, 1, 2, line);
        std::vector<Value> args = a.size() > 1 ? arrArgs(a[1], "oop.apply", line) : std::vector<Value>{};
        return callValue(a[0], args, line);
    };
    natives["oop.callWith"] = [this, needFn, needInst, arrArgs, bindFnToObj](Args& a, int line) -> Value {
        need("oop.callWith", a, 2, 3, line);
        needFn(a[0], "oop.callWith", line); needInst(a[1], "oop.callWith", line);
        std::vector<Value> args = a.size() > 2 ? arrArgs(a[2], "oop.callWith", line) : std::vector<Value>{};
        Value bound = bindFnToObj(a[0], a[1]);
        return callFunction(bound.function, args, line);
    };

    // ======================================================== 2) partial / curry
    // نواتان داخليتان تُنادَيان من الدوال الاصطناعية (أسماؤها تبدأ بـ oop.__ ولا تُوثَّق للمستخدم).
    natives["oop.__partialCall"] = [this](Args& a, int line) -> Value {
        // a = [f, presetArray, rest...]
        std::vector<Value> args = *a[1].array;
        for (size_t i = 2; i < a.size(); ++i) args.push_back(a[i]);
        return callFunction(a[0].function, args, line);
    };

    natives["oop.partial"] = [this, needFn, makeNativeBacked](Args& a, int line) -> Value {
        if (a.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'oop.partial' expects a function followed by preset arguments");
        needFn(a[0], "oop.partial", line);
        size_t arity = a[0].function->declaration->params.size();
        size_t k = a.size() - 1;
        if (k > arity)
            throw errWithReason(diag::Code::E0007_InvalidArguments, line,
                                "oop.partial: " + std::to_string(k) + " preset argument(s) given but the function takes only " + std::to_string(arity),
                                "pass at most as many preset arguments as the function has parameters");
        auto preset = std::make_shared<ArrayData>(a.begin() + 1, a.end());
        std::vector<std::string> params, argVars{"__f", "__p"};
        for (size_t i = 0; i < arity - k; ++i) { params.push_back("r" + std::to_string(i)); argVars.push_back("r" + std::to_string(i)); }
        return makeNativeBacked(params, "oop.__partialCall", argVars, {{"__f", a[0]}, {"__p", Value::makeArray(preset)}});
    };

    // curry: كل نداء يستقبل وسيطاً واحداً ويُرجع دالة جديدة، حتى يكتمل عدد وسائط الدالة فتُنفَّذ.
    auto curryBuild = std::make_shared<std::function<Value(const Value&, const std::shared_ptr<ArrayData>&)>>();
    *curryBuild = [makeNativeBacked](const Value& f, const std::shared_ptr<ArrayData>& collected) -> Value {
        return makeNativeBacked({"a"}, "oop.__curryStep", {"__f", "__c", "a"},
                                {{"__f", f}, {"__c", Value::makeArray(collected)}});
    };
    natives["oop.__curryStep"] = [this, curryBuild](Args& a, int line) -> Value {
        auto collected = std::make_shared<ArrayData>(*a[1].array);
        collected->push_back(a[2]);
        size_t arity = a[0].function->declaration->params.size();
        if (collected->size() >= arity) {
            std::vector<Value> args = *collected;
            return callFunction(a[0].function, args, line);
        }
        return (*curryBuild)(a[0], collected);
    };
    natives["oop.curry"] = [needFn, curryBuild](Args& a, int line) -> Value {
        need("oop.curry", a, 1, 1, line);
        needFn(a[0], "oop.curry", line);
        if (a[0].function->declaration->params.empty()) return a[0]; // لا وسائط: لا شيء لتجميعه
        return (*curryBuild)(a[0], std::make_shared<ArrayData>());
    };

    // ======================================================== 3) ربط الخصائص (observers)
    auto addObserver = [this](OopObserver o) -> int {
        o.id = nextObserverId_++;
        observers_.push_back(std::move(o));
        observersExist_ = true;
        // تنظيف المراقبين الذين زال كائنهم
        observers_.erase(std::remove_if(observers_.begin(), observers_.end(), [](const OopObserver& x) { return x.obj.expired(); }),
                         observers_.end());
        return observers_.back().id;
    };

    natives["oop.observe"] = [needInst, addObserver, N](Args& a, int line) -> Value {
        need("oop.observe", a, 3, 3, line);
        needInst(a[0], "oop.observe", line);
        if (a[2].type != Value::Type::FUNCTION)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'oop.observe' expects a callback function as 3rd argument but got " + a[2].typeName());
        OopObserver o;
        o.obj = a[0].instance;
        o.field = str(a[1], "oop.observe", line);
        o.fn = a[2];
        return N(addObserver(std::move(o)));
    };

    natives["oop.bindProperty"] = [this, needInst, addObserver, N](Args& a, int line) -> Value {
        need("oop.bindProperty", a, 4, 5, line);
        needInst(a[0], "oop.bindProperty", line); needInst(a[2], "oop.bindProperty", line);
        std::string sf = str(a[1], "oop.bindProperty", line), df = str(a[3], "oop.bindProperty", line);
        Value transform = a.size() > 4 ? a[4] : Value::nil();
        if (transform.type != Value::Type::NIL && transform.type != Value::Type::FUNCTION)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'oop.bindProperty' transform must be a function or nil");
        OopObserver o;
        o.obj = a[0].instance;
        o.field = sf;
        o.isBinding = true;
        o.dst = a[2];
        o.dstField = df;
        o.transform = transform;
        int id = addObserver(std::move(o));
        // مزامنة أولية: dst.df = transform(src.sf)
        Value cur = readMember(a[0], sf, nullptr, line);
        if (transform.type == Value::Type::FUNCTION) cur = callFlexible(transform, {cur, Value::nil(), a[0]}, line);
        writeMember(a[2], df, cur, nullptr, line);
        return N(id);
    };

    natives["oop.bindTwoWay"] = [this, needInst, addObserver, N](Args& a, int line) -> Value {
        need("oop.bindTwoWay", a, 4, 4, line);
        needInst(a[0], "oop.bindTwoWay", line); needInst(a[2], "oop.bindTwoWay", line);
        std::string f1 = str(a[1], "oop.bindTwoWay", line), f2 = str(a[3], "oop.bindTwoWay", line);
        OopObserver fwd, back;
        fwd.obj = a[0].instance; fwd.field = f1; fwd.isBinding = true; fwd.dst = a[2]; fwd.dstField = f2;
        back.obj = a[2].instance; back.field = f2; back.isBinding = true; back.dst = a[0]; back.dstField = f1;
        int id1 = addObserver(std::move(fwd));
        int id2 = addObserver(std::move(back));
        writeMember(a[2], f2, readMember(a[0], f1, nullptr, line), nullptr, line); // مزامنة أولية a -> b
        return newArray({N(id1), N(id2)});
    };

    natives["oop.unobserve"] = [this, B](Args& a, int line) -> Value {
        need("oop.unobserve", a, 1, 1, line);
        int id = static_cast<int>(num(a[0], "oop.unobserve", line));
        size_t before = observers_.size();
        observers_.erase(std::remove_if(observers_.begin(), observers_.end(), [id](const OopObserver& o) { return o.id == id; }), observers_.end());
        return B(observers_.size() != before);
    };
    natives["oop.unobserveAll"] = [this, needInst, N](Args& a, int line) -> Value {
        need("oop.unobserveAll", a, 1, 1, line);
        needInst(a[0], "oop.unobserveAll", line);
        size_t before = observers_.size();
        auto target = a[0].instance;
        observers_.erase(std::remove_if(observers_.begin(), observers_.end(), [&](const OopObserver& o) {
            auto sp = o.obj.lock();
            return !sp || sp == target;
        }), observers_.end());
        return N(static_cast<double>(before - observers_.size()));
    };
    natives["oop.observers"] = [this, needInst, S, N](Args& a, int line) -> Value {
        need("oop.observers", a, 1, 1, line);
        needInst(a[0], "oop.observers", line);
        ArrayData out;
        for (auto& o : observers_) {
            auto sp = o.obj.lock();
            if (!sp || sp != a[0].instance) continue;
            MapData m;
            m.push_back({S("id"), N(o.id)});
            m.push_back({S("kind"), S(o.isEvent ? "event" : (o.isBinding ? "binding" : "observer"))});
            m.push_back({S("field"), S(o.field)});
            out.push_back(newMap(std::move(m)));
        }
        return newArray(std::move(out));
    };

    // ======================================================== 4) الأحداث
    auto addListener = [needInst, addObserver, N](Args& a, int line, const char* fname, bool once) -> Value {
        need(fname, a, 3, 3, line);
        needInst(a[0], fname, line);
        if (a[2].type != Value::Type::FUNCTION)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fname + "' expects a callback function as 3rd argument but got " + a[2].typeName());
        OopObserver o;
        o.obj = a[0].instance;
        o.field = str(a[1], fname, line);
        o.isEvent = true;
        o.once = once;
        o.fn = a[2];
        return N(addObserver(std::move(o)));
    };
    natives["oop.on"] = [addListener](Args& a, int line) -> Value { return addListener(a, line, "oop.on", false); };
    natives["oop.once"] = [addListener](Args& a, int line) -> Value { return addListener(a, line, "oop.once", true); };
    natives["oop.off"] = [this, needInst, N](Args& a, int line) -> Value {
        need("oop.off", a, 1, 2, line);
        needInst(a[0], "oop.off", line);
        std::string ev = a.size() > 1 ? str(a[1], "oop.off", line) : std::string();
        size_t before = observers_.size();
        auto target = a[0].instance;
        observers_.erase(std::remove_if(observers_.begin(), observers_.end(), [&](const OopObserver& o) {
            auto sp = o.obj.lock();
            return o.isEvent && sp == target && (ev.empty() || o.field == ev);
        }), observers_.end());
        return N(static_cast<double>(before - observers_.size()));
    };
    natives["oop.emit"] = [this, needInst, N](Args& a, int line) -> Value {
        if (a.size() < 2) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'oop.emit' expects (object, \"event\", args...)");
        needInst(a[0], "oop.emit", line);
        std::string ev = str(a[1], "oop.emit", line);
        std::vector<Value> args(a.begin() + 2, a.end());
        std::vector<OopObserver> hit;
        for (auto& o : observers_) {
            if (!o.isEvent || o.field != ev) continue;
            auto sp = o.obj.lock();
            if (sp && sp == a[0].instance) hit.push_back(o);
        }
        for (auto& o : hit) {
            if (o.once) { // يُزال قبل الاستدعاء حتى لا يُشغَّل مرتين لو أُطلق الحدث من داخل callback
                int id = o.id;
                observers_.erase(std::remove_if(observers_.begin(), observers_.end(), [id](const OopObserver& x) { return x.id == id; }), observers_.end());
            }
            callFlexible(o.fn, args, line);
        }
        return N(static_cast<double>(hit.size()));
    };
    natives["oop.listeners"] = [this, needInst, N](Args& a, int line) -> Value {
        need("oop.listeners", a, 1, 2, line);
        needInst(a[0], "oop.listeners", line);
        std::string ev = a.size() > 1 ? str(a[1], "oop.listeners", line) : std::string();
        double n = 0;
        for (auto& o : observers_) {
            auto sp = o.obj.lock();
            if (o.isEvent && sp && sp == a[0].instance && (ev.empty() || o.field == ev)) n += 1;
        }
        return N(n);
    };
}

} // namespace rin

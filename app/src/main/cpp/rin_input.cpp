// ============================================================================
//  rin_input.cpp — Rin 1.0: مفسِّر/مترجم/منفِّذ نماذج الإدخال (انظر rin_input.h للتصميم الكامل)
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp بعد rin_oop_link.cpp، ويُسجَّل من
//  Interpreter::registerNativesInput() (تُستدعى في نهاية registerNatives).
//
//  التوافق: كل نداء input() ليس نموذجاً (input(p) · input(p, validator) · input(p, target, key)) يذهب إلى
//  التنفيذ القديم حرفياً. ونداءات النماذج التي كانت تعمل تنتج الأسئلة نفسها بالترتيب نفسه والكتابة نفسها —
//  الفرق الوحيد أنها صارت معاملة: عند الإلغاء/الفشل يُعاد الهدف كما كان.
// ============================================================================
#include "rin_input.h"
#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace inputx {

// ---------------------------------------------------------------------------
// أدوات صغيرة
// ---------------------------------------------------------------------------
static bool isTypeWord(const std::string& s) { return s == "number" || s == "bool" || s == "string" || s == "text"; }

static const char* kindName(FieldKind k) {
    switch (k) {
        case FieldKind::Text: return "text";
        case FieldKind::Number: return "number";
        case FieldKind::Bool: return "bool";
        case FieldKind::Choice: return "choice";
        case FieldKind::NestedObject: return "object";
        case FieldKind::NestedContainer: return "container";
    }
    return "?";
}

static const char* targetName(TargetKind k) {
    switch (k) {
        case TargetKind::Container: return "container";
        case TargetKind::Object: return "object";
        case TargetKind::Map: return "map";
    }
    return "?";
}

// ===========================================================================
//  المرحلة 1 — المفسِّر: وسائط Rin → خطة
// ===========================================================================
bool FormInterpreter::isTarget(const Value& v) const {
    return (v.type == Value::Type::STRING && h_.hasContainer(v.str)) ||
           v.type == Value::Type::MAP || v.type == Value::Type::INSTANCE;
}

bool FormInterpreter::recognize(const std::vector<Value>& a) const {
    if (a.empty()) return false;
    size_t i = 1;
    if (i < a.size() && h_.isCallable(a[i])) ++i;                    // مُدقِّق النموذج
    const size_t rest = a.size() > i ? a.size() - i : 0;
    if (rest == 1) return isTarget(a[i]);                            // input(p[, validator], target)
    if (rest == 2) return a[i + 1].type == Value::Type::MAP && isTarget(a[i]);   // ... , target, schema)
    return false;                                                    // (target, key) وغيرها: الأصل القديم
}

Request FormInterpreter::analyze(const std::vector<Value>& a, int line) const {
    Request rq;
    rq.line = line;
    const std::string prompt = a[0].type == Value::Type::STRING ? a[0].str : a[0].toDisplayString();
    size_t i = 1;
    if (i < a.size() && h_.isCallable(a[i])) { rq.hasCheck = true; rq.check = a[i]; ++i; }
    const Value& target = a[i];
    const Value* schema = (a.size() > i + 1) ? &a[i + 1] : nullptr;
    Visited vis;
    rq.root = planScope(target, prompt, schema, "", 0, vis, line);
    return rq;
}

std::shared_ptr<FormPlan> FormInterpreter::planScope(const Value& target, const std::string& prompt, const Value* schema,
                                                     const std::string& path, int depth, Visited& vis, int line) const {
    const std::string where = path.empty() ? std::string() : " (الحقل '" + path + "')";
    if (depth > kMaxDepth)
        throw h_.fail(line, "input: النموذج متداخل أعمق من " + std::to_string(kMaxDepth) + " مستويات" + where,
                      "غالباً دورة في الكائنات/الحاويات المتداخلة");

    auto plan = std::make_shared<FormPlan>();
    plan->target = target;
    plan->prompt = prompt;

    // ---- فحص مسبق + جمع الحقول (الترتيب نفسه الذي كان يستعمله وضع النموذج القديم) ----
    std::vector<std::string> keys;
    std::unordered_map<std::string, std::string> types;      // اسم الحقل -> نوعه
    if (target.type == Value::Type::STRING) {
        plan->kind = TargetKind::Container;
        if (!vis.containers.insert(target.str).second)
            throw h_.fail(line, "input: دورة في النموذج: الحاوية '" + target.str + "' تتكرّر" + where, "كل حاوية تُملأ مرة واحدة في الشجرة");
        if (h_.call("container.isLocked", {target}, line).isTruthy())
            throw h_.fail(line, "input: الحاوية '" + target.str + "' مقفلة" + where, "افتحها بـ container.unlock(\"" + target.str + "\") قبل الإدخال");
        Value names = h_.call("container.fieldNames", {target}, line);
        if (names.type == Value::Type::ARRAY && names.array) for (auto& n : *names.array) keys.push_back(n.str);
        std::sort(keys.begin(), keys.end());                       // ترتيب حقول الحاوية غير مضمون => أبجدي
        for (const auto& k : keys) types[k] = h_.call("container.fieldType", {target, Value::string(k)}, line).str;
    } else if (target.type == Value::Type::INSTANCE) {
        plan->kind = TargetKind::Object;
        if (!target.instance) throw h_.fail(line, "input: كائن فارغ" + where, "");
        if (!vis.objects.insert(target.instance.get()).second)
            throw h_.fail(line, "input: دورة في النموذج: الكائن `" + target.instance->className + "` يتكرّر" + where, "كل كائن يُملأ مرة واحدة في الشجرة");
        if (target.instance->frozen)                              // افشل قبل السؤال لا بعده
            throw h_.fail(line, "cannot fill a frozen `" + target.instance->className + "` object" + where,
                          "this object was frozen with `oop.freeze(...)`");
        Value m = h_.call("oop.toMap", {target}, line);
        for (auto& kv : *m.map) { keys.push_back(kv.first.toDisplayString()); types[keys.back()] = kv.second.typeName(); }
    } else {
        plan->kind = TargetKind::Map;
        for (auto& kv : *target.map) { keys.push_back(kv.first.toDisplayString()); types[keys.back()] = kv.second.typeName(); }
    }

    // ---- مواصفات المخطّط: حقول المخطّط غير الموجودة في الهدف (مثل nil) تُضاف بعد حقوله ----
    std::unordered_map<std::string, Value> spec;
    if (schema) {
        for (auto& kv : *schema->map) {
            std::string k = kv.first.toDisplayString();
            spec[k] = kv.second;
            if (!types.count(k)) { keys.push_back(k); types[k] = "nil"; }
        }
    }

    for (const auto& k : keys) {
        FieldPlan f;
        f.name = k;
        std::string ty = types[k];
        auto sp = spec.find(k);
        const std::string fpath = path.empty() ? k : path + "." + k;

        if (sp == spec.end()) {
            // بلا مواصفة: النوع من القيمة الحالية (nil/مركّب => يُتخطّى؛ المتداخل صريح فقط عبر المخطّط).
            if (ty == "number") f.kind = FieldKind::Number;
            else if (ty == "bool") f.kind = FieldKind::Bool;
            else if (ty == "string") f.kind = FieldKind::Text;
            else continue;
        } else {
            const Value& v = sp->second;
            if (v.type == Value::Type::NIL) continue;
            if (v.type == Value::Type::STRING) {
                if (isTypeWord(v.str)) {
                    ty = v.str == "text" ? "string" : v.str;
                    f.kind = ty == "number" ? FieldKind::Number : ty == "bool" ? FieldKind::Bool : FieldKind::Text;
                } else if (h_.hasContainer(v.str)) {              // حاوية متداخلة: تُملأ ثم تُسلَّم لقطتها للحقل
                    f.kind = FieldKind::NestedContainer;
                    f.spec = v;
                    f.nested = planScope(v, plan->prompt + k + ".", nullptr, fpath, depth + 1, vis, line);
                    f.commitNested = true;
                } else {
                    throw diagErr(diag::Code::E0007_InvalidArguments, line,
                                  "input: نوع غير معروف '" + v.str + "' للحقل '" + k + "' (المتاح: number · bool · string)");
                }
            } else if (v.type == Value::Type::ARRAY || v.type == Value::Type::MAP) {
                f.kind = FieldKind::Choice;
                f.spec = v;
            } else if (h_.isCallable(v)) {
                f.hasValidator = true; f.validator = v;
                // نوع الحقل: من حقل `type` في صنف المُدقِّق (انظر lib/inputkit)، وإلا من القيمة الحالية، وإلا نص.
                Value t = v.type == Value::Type::INSTANCE ? h_.call("oop.get", {v, Value::string("type"), Value::nil()}, line) : Value::nil();
                if (t.type == Value::Type::STRING) ty = t.str;
                else if (ty == "nil") ty = "string";
                if (ty == "number") f.kind = FieldKind::Number;
                else if (ty == "bool") f.kind = FieldKind::Bool;
                else if (ty == "string") f.kind = FieldKind::Text;
                else continue;
            } else if (v.type == Value::Type::INSTANCE) {         // كائن متداخل (تركيب): يُملأ في مكانه
                f.kind = FieldKind::NestedObject;
                f.spec = v;
                f.nested = planScope(v, plan->prompt + k + ".", nullptr, fpath, depth + 1, vis, line);
                // الكائن نفسه موجود أصلاً في الحقل الأب؟ فلا حاجة لإعادة كتابته (يُعدَّل مكانه).
                f.commitNested = true;
                if (plan->kind == TargetKind::Object && plan->target.instance) {
                    auto cur = plan->target.instance->fields.find(k);
                    if (cur != plan->target.instance->fields.end() && cur->second.type == Value::Type::INSTANCE &&
                        cur->second.instance == v.instance) f.commitNested = false;
                } else if (plan->kind == TargetKind::Map && plan->target.map) {
                    for (auto& kv : *plan->target.map)
                        if (kv.first.toDisplayString() == k && kv.second.type == Value::Type::INSTANCE &&
                            kv.second.instance == v.instance) { f.commitNested = false; break; }
                }
            } else {
                throw diagErr(diag::Code::E0004_InvalidType, line,
                              "input: مواصفة الحقل '" + k + "' يجب أن تكون نوعاً (نص) أو خيارات أو مُدقِّقاً، لا " + v.typeName());
            }
        }
        plan->fields.push_back(std::move(f));
    }
    return plan;
}

// ===========================================================================
//  المرحلة 2 — المترجم: خطة → برنامج تعليمات مسطّح
// ===========================================================================
int FormCompiler::str(Program& p, const std::string& s) {
    for (size_t i = 0; i < p.strings.size(); ++i) if (p.strings[i] == s) return static_cast<int>(i);   // دمج المكرّر
    p.strings.push_back(s);
    return static_cast<int>(p.strings.size()) - 1;
}

int FormCompiler::cst(Program& p, const Value& v) {
    p.consts.push_back(v);
    return static_cast<int>(p.consts.size()) - 1;
}

int FormCompiler::emitScope(const FormPlan& plan, int parent, const std::string& path, Program& p) const {
    const int me = static_cast<int>(p.scopes.size());
    Scope sc; sc.kind = plan.kind; sc.target = plan.target; sc.path = path; sc.parent = parent;
    p.scopes.push_back(sc);
    Instr enter; enter.op = Op::Enter; enter.scope = me;
    p.code.push_back(enter);

    for (const auto& f : plan.fields) {
        Instr in; in.scope = me; in.name = str(p, f.name);
        in.label = str(p, plan.prompt + f.name + ": ");
        switch (f.kind) {
            case FieldKind::Text:   in.op = Op::AskText;   break;
            case FieldKind::Number: in.op = Op::AskNumber; break;
            case FieldKind::Bool:   in.op = Op::AskBool;   break;
            case FieldKind::Choice: in.op = Op::AskChoice; in.spec = cst(p, f.spec); break;
            case FieldKind::NestedObject:
            case FieldKind::NestedContainer: {
                const std::string sub = path.empty() ? f.name : path + "." + f.name;
                const int child = emitScope(*f.nested, me, sub, p);
                if (f.commitNested) {
                    Instr c; c.op = Op::Commit; c.scope = me; c.name = in.name; c.src = child;
                    p.code.push_back(c);
                }
                continue;
            }
        }
        // confirm لا تقبل مُدقِّقاً (كالتنفيذ القديم)؛ البقية تمرّره.
        if (f.hasValidator && f.kind != FieldKind::Bool) in.valid = cst(p, f.validator);
        p.code.push_back(in);
    }
    Instr leave; leave.op = Op::Leave; leave.scope = me;
    p.code.push_back(leave);
    return me;
}

Program FormCompiler::compile(const Request& rq) const {
    Program p;
    p.line = rq.line;
    emitScope(*rq.root, -1, "", p);
    if (rq.hasCheck) { p.hasCheck = true; p.checkConst = cst(p, rq.check); }
    return p;
}

size_t Program::askCount() const {
    size_t n = 0;
    for (const auto& i : code) if (i.op == Op::AskText || i.op == Op::AskNumber || i.op == Op::AskBool || i.op == Op::AskChoice) ++n;
    return n;
}

std::string Program::disassemble() const {
    static const char* names[] = {"ENTER", "LEAVE", "ASK.TEXT", "ASK.NUM", "ASK.BOOL", "ASK.CHOICE", "COMMIT"};
    std::ostringstream o;
    o << "; form  scopes=" << scopes.size() << " ops=" << code.size() << " asks=" << askCount()
      << " check=" << (hasCheck ? "yes" : "no") << "\n";
    for (size_t pc = 0; pc < code.size(); ++pc) {
        const Instr& in = code[pc];
        char head[32];
        std::snprintf(head, sizeof head, "%04zu  %-10s s%d", pc, names[static_cast<int>(in.op)], in.scope);
        o << head;
        if (in.op == Op::Enter) {
            const Scope& s = scopes[static_cast<size_t>(in.scope)];
            o << "  " << targetName(s.kind) << (s.target.type == Value::Type::STRING ? " " + s.target.str : std::string())
              << (s.path.empty() ? "" : "  path=" + s.path);
        } else if (in.op == Op::Commit) {
            o << "  " << strings[static_cast<size_t>(in.name)] << " <- s" << in.src;
        } else if (in.op != Op::Leave) {
            o << "  " << strings[static_cast<size_t>(in.name)] << "  \"" << strings[static_cast<size_t>(in.label)] << "\"";
            if (in.valid >= 0) o << "  +validator";
            if (in.spec >= 0) o << "  +options";
        }
        o << "\n";
    }
    if (hasCheck) o << "check  -> validator over the final " << targetName(scopes[0].kind) << "\n";
    return o.str();
}

// ===========================================================================
//  المرحلة 3 — المنفِّذ: تشغيل البرنامج + دفتر المعاملة + جولات مُدقِّق النموذج
// ===========================================================================
Value FormExecutor::read(const Scope& s, const std::string& key, bool& existed) {
    existed = true;
    try {
        if (s.kind == TargetKind::Container) {
            Value names = h_.call("container.fieldNames", {s.target}, line_);
            existed = false;
            if (names.type == Value::Type::ARRAY && names.array) for (auto& n : *names.array) if (n.str == key) { existed = true; break; }
            return h_.call("getField", {s.target, Value::string(key)}, line_);
        }
        if (s.kind == TargetKind::Object) {
            existed = h_.call("oop.hasField", {s.target, Value::string(key)}, line_).isTruthy();
            return h_.call("oop.get", {s.target, Value::string(key), Value::nil()}, line_);
        }
        existed = false;
        for (auto& kv : *s.target.map) if (kv.first.toDisplayString() == key) { existed = true; return kv.second; }
    } catch (const RinError&) {}
    return Value::nil();
}

void FormExecutor::write(const Scope& s, const std::string& key, const Value& v) {
    // setState = setField + on update(prev) للحقول state؛ oop.set تحترم private/final/set/freeze والمراقبين.
    if (s.kind == TargetKind::Container) h_.call("setState", {s.target, Value::string(key), v}, line_);
    else                                  h_.call("oop.set", {s.target, Value::string(key), v}, line_);
}

void FormExecutor::journal(const Program& p, int scope, const std::string& key) {
    if (!seen_.insert({scope, key}).second) return;                  // أول قيمة فقط تُسجَّل (قبل أي كتابة)
    bool existed = true;
    Value before = read(p.scopes[static_cast<size_t>(scope)], key, existed);
    journal_.push_back({scope, key, before, existed});
}

void FormExecutor::rollback(const Program& p) {
    // عكس ترتيب الكتابة: المتداخل يُعاد قبل أبيه. كل حقل يُعاد عبر مسار الكتابة نفسه (فيرى المراقبون التراجع).
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) {
        const Scope& s = p.scopes[static_cast<size_t>(it->scope)];
        try {
            // حقل سُجِّل قبل سؤاله لكنه لم يُكتب (فشل/إلغاء قبل الوصول إليه): لا نلمسه، فلا يُطلَق on update بلا داعٍ.
            bool nowExists = true;
            Value now = read(s, it->key, nowExists);
            if (it->existed == nowExists && valuesEqual(now, it->before)) continue;
            if (s.kind == TargetKind::Map && !it->existed) {
                auto& m = *s.target.map;
                m.erase(std::remove_if(m.begin(), m.end(), [&](const std::pair<Value, Value>& kv) { return kv.first.toDisplayString() == it->key; }), m.end());
            } else {
                write(s, it->key, it->before);
            }
        } catch (const RinError&) { /* حقل لا يقبل الكتابة (private/final): يبقى كما هو */ }
    }
    journal_.clear();
    seen_.clear();
}

Value FormExecutor::subject(const Program& p) {
    const Scope& root = p.scopes[0];
    return root.kind == TargetKind::Container ? h_.call("container.snapshot", {root.target}, line_) : root.target;
}

void FormExecutor::pass(const Program& p) {
    for (const Instr& in : p.code) {
        switch (in.op) {
            case Op::Enter: case Op::Leave: break;
            case Op::Commit: {
                const Scope& parent = p.scopes[static_cast<size_t>(in.scope)];
                const Scope& child = p.scopes[static_cast<size_t>(in.src)];
                const std::string& key = p.strings[static_cast<size_t>(in.name)];
                journal(p, in.scope, key);
                write(parent, key, child.kind == TargetKind::Container ? h_.call("container.snapshot", {child.target}, line_) : child.target);
                break;
            }
            default: {
                const Scope& sc = p.scopes[static_cast<size_t>(in.scope)];
                const std::string& key = p.strings[static_cast<size_t>(in.name)];
                std::string label = p.strings[static_cast<size_t>(in.label)];
                if (!note_.empty()) { label = note_ + label; note_.clear(); }   // سبب رفض الجولة السابقة يظهر أمام أول سؤال
                journal(p, in.scope, key);
                Value kk = Value::string(key);
                if (in.op == Op::AskChoice) { h_.call("choose", {Value::string(label), p.consts[static_cast<size_t>(in.spec)], sc.target, kk}, line_); break; }
                const char* fn = in.op == Op::AskNumber ? "inputNumber" : in.op == Op::AskBool ? "confirm" : "input";
                if (in.valid >= 0) h_.call(fn, {Value::string(label), p.consts[static_cast<size_t>(in.valid)], sc.target, kk}, line_);
                else               h_.call(fn, {Value::string(label), sc.target, kk}, line_);
            }
        }
    }
}

Value FormExecutor::run(const Program& p) {
    journal_.clear(); seen_.clear(); note_.clear();
    line_ = p.line;
    try {
        for (int round = 0; round < kMaxRounds; ++round) {
            if (round > 0 && h_.atEof())                              // لا جدوى من إعادة السؤال بلا مصدر إدخال
                throw diagErr(diag::Code::E0035_RuntimeError, line_, "تم إلغاء الإدخال (input)");
            pass(p);
            if (!p.hasCheck) break;
            std::vector<Value> args{subject(p)};
            Value verdict = h_.invoke(p.consts[static_cast<size_t>(p.checkConst)], args, line_);
            if (verdict.type != Value::Type::STRING && verdict.isTruthy()) break;     // قُبل النموذج
            std::string why = verdict.type == Value::Type::STRING ? verdict.str : std::string();
            if (why.empty()) why = "النموذج غير مقبول";
            if (round + 1 == kMaxRounds)
                throw diagErr(diag::Code::E0035_RuntimeError, line_, "'input': لم يُقبل النموذج بعد " + std::to_string(kMaxRounds) + " محاولات (" + why + ")");
            note_ = "[" + why + "] ";
        }
    } catch (...) {
        rollback(p);                                                  // معاملة: أي فشل/إلغاء يعيد الهدف كما كان
        throw;
    }
    journal_.clear(); seen_.clear();
    return subject(p);
}

Value FormEngine::run(const std::vector<Value>& a, int line) {
    Request rq = interp_.analyze(a, line);
    last_ = comp_.compile(rq);
    return exec_.run(last_);
}

} // namespace inputx

// ===========================================================================
//  التسجيل: input() الجديدة تلتقط النماذج وتمرّر كل ما عداها إلى التنفيذ القديم كما هو.
// ===========================================================================
void Interpreter::registerNativesInput() {
    inputx::Host host;
    host.call = [this](const char* fn, std::vector<Value> args, int line) -> Value {
        auto it = natives.find(fn);
        if (it == natives.end())
            throw diagErr(diag::Code::E0035_RuntimeError, line, std::string("input: الدالة المبنية '") + fn + "' غير مسجَّلة");
        return it->second(args, line);
    };
    host.hasContainer = [this](const std::string& n) { return containers.count(n) > 0; };
    host.isCallable = [this](const Value& v) {
        return v.type == Value::Type::FUNCTION || (v.type == Value::Type::INSTANCE && hasMagic(v, "__call__"));
    };
    host.invoke = [this](const Value& f, std::vector<Value> args, int line) -> Value { return callValue(f, args, line); };
    host.fail = [this](int line, const std::string& msg, const std::string& why) -> RinError {
        return why.empty() ? diagErr(diag::Code::E0035_RuntimeError, line, msg)
                           : errWithReason(diag::Code::E0035_RuntimeError, line, msg, why);
    };
    host.atEof = [this]() { return inputAtEof_; };

    auto engine = std::make_shared<inputx::FormEngine>(host);
    NativeFn legacy = natives["input"];
    natives["input"] = [engine, legacy](std::vector<Value>& a, int line) -> Value {
        if (!engine->recognize(a)) return legacy(a, line);
        return engine->run(a, line);
    };
}

} // namespace rin

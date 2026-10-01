// ============================================================================
//  rin_oop.cpp — Rin 1.4: نواة OOP الموسَّعة
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp بنفس أسلوب rin_extra_natives*.cpp، فيدخل كل أهداف
//  البناء (CLI/APK/WASM/CI) تلقائياً دون تعديل أي قائمة ملفات.
//
//  ما يضيفه هذا الملف فوق class/struct/extends/super الموجودة سابقاً:
//    1) interface + implements        (عقد: توقيعات فقط، يُتحقَّق منه عند الإنشاء)
//    2) trait + uses                  (كود قابل لإعادة الاستخدام: حقول + دوال، مع تركيب متداخل)
//    3) abstract class / abstract fun / final class / final fun / override fun / final let
//    4) static let / static fun       (ClassName.member — تهيئة كسولة)
//    5) public / private / protected  (حقول ودوال ومُنشئ)
//    6) get name() / set name(v)      (خصائص محسوبة)
//    7) عوامل سحرية جديدة: __eq__ __ne__ __lt__ __le__ __gt__ __ge__ __cmp__ __str__ toString
//                          __getitem__ __setitem__ __call__ __iter__ __len__
//    8) is-a موسَّعة: isInstanceOf تشمل الواجهات والسمات (وتُستخدم في `x: Type` و`instanceof`)
//
//  كل شيء هنا "إضافي بحت": أي برنامج Rin قديم لا يستخدم هذه الكلمات يتصرف حرفياً كما كان.
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <functional>
#include <map>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

// ---------------------------------------------------------------------------
// 1) استعلامات الأنواع والوراثة
// ---------------------------------------------------------------------------
std::vector<std::string> Interpreter::classChain(const std::string& cls) const {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    std::string cur = cls;
    while (!cur.empty()) {
        auto it = classes.find(cur);
        if (it == classes.end()) break;
        if (!seen.insert(cur).second) break; // وراثة دائرية: نتوقّف بلا خطأ (يُكتشف في instantiateClass)
        out.push_back(cur);
        cur = it->second.superclass;
    }
    return out;
}

bool Interpreter::classIsSubclassOf(const std::string& cls, const std::string& base) const {
    for (auto& c : classChain(cls)) if (c == base) return true;
    return false;
}

void Interpreter::collectTraits(const std::string& cls, std::vector<std::string>& out) const {
    std::function<void(const std::string&)> addTrait = [&](const std::string& t) {
        if (std::find(out.begin(), out.end(), t) != out.end()) return;
        auto it = classes.find(t);
        if (it == classes.end()) return;
        out.push_back(t);
        for (auto& u : it->second.traits) addTrait(u);
    };
    for (auto& c : classChain(cls)) {
        auto it = classes.find(c);
        if (it == classes.end()) continue;
        for (auto& t : it->second.traits) addTrait(t);
    }
}

void Interpreter::collectInterfaces(const std::string& cls, std::vector<std::string>& out) const {
    std::function<void(const std::string&)> addIface = [&](const std::string& i) {
        if (std::find(out.begin(), out.end(), i) != out.end()) return;
        out.push_back(i);
        auto it = classes.find(i);
        if (it != classes.end()) for (auto& j : it->second.interfaces) addIface(j);
    };
    for (auto& c : classChain(cls)) {
        auto it = classes.find(c);
        if (it == classes.end()) continue;
        for (auto& i : it->second.interfaces) addIface(i);
    }
}

bool Interpreter::typeNameIsAncestor(const std::string& cls, const std::string& typeName) const {
    if (classIsSubclassOf(cls, typeName)) return true;
    std::vector<std::string> v;
    collectTraits(cls, v);
    if (std::find(v.begin(), v.end(), typeName) != v.end()) return true;
    v.clear();
    collectInterfaces(cls, v);
    return std::find(v.begin(), v.end(), typeName) != v.end();
}

bool Interpreter::isInstanceOf(const Value& v, const std::string& typeName) const {
    if (v.type != Value::Type::INSTANCE || !v.instance) return false;
    return typeNameIsAncestor(v.instance->className, typeName);
}

std::shared_ptr<FunctionStmt> Interpreter::findAccessor(const std::string& cls, const std::string& name, bool setter,
                                                         std::string* ownerOut) const {
    int kind = setter ? 2 : 1;
    for (auto& cur : classChain(cls)) {
        auto it = classes.find(cur);
        if (it == classes.end()) continue;
        if (auto f = oopPickFn(it->second, name, kind)) { if (ownerOut) *ownerOut = cur; return f; }
        if (!it->second.traits.empty()) {
            std::unordered_set<std::string> seen;
            if (auto f = oopFindInTraits(classes, it->second, name, kind, seen)) { if (ownerOut) *ownerOut = cur; return f; }
        }
    }
    return nullptr;
}

const ClassDef::MemberMeta* Interpreter::findFieldMeta(const std::string& cls, const std::string& name,
                                                        std::string* declOut) const {
    for (auto& cur : classChain(cls)) {
        auto it = classes.find(cur);
        if (it == classes.end()) continue;
        auto f = it->second.fieldMeta.find(name);
        if (f != it->second.fieldMeta.end()) { if (declOut) *declOut = cur; return &f->second; }
        // حقول السمات المستخدَمة تُعدّ معرَّفة في الصنف المستخدِم نفسه.
        std::vector<std::string> ts;
        std::function<void(const ClassDef&)> walk = [&](const ClassDef& d) {
            for (auto& t : d.traits) {
                if (std::find(ts.begin(), ts.end(), t) != ts.end()) continue;
                auto tit = classes.find(t);
                if (tit == classes.end()) continue;
                ts.push_back(t);
                walk(tit->second);
            }
        };
        walk(it->second);
        for (auto& t : ts) {
            auto tf = classes.at(t).fieldMeta.find(name);
            if (tf != classes.at(t).fieldMeta.end()) { if (declOut) *declOut = cur; return &tf->second; }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 2) التحقق عند الإنشاء: abstract / interface / trait / final / override
// ---------------------------------------------------------------------------
void Interpreter::validateInstantiable(const std::string& className, int line) {
    if (validatedClasses_.count(className)) return;
    auto cit = classes.find(className);
    if (cit == classes.end()) return;
    const ClassDef& top = cit->second;

    auto fail = [&](const std::string& msg, const std::string& reason) {
        throw errWithReason(diag::Code::E0035_RuntimeError, line, msg, reason);
    };
    auto kindName = [](ClassKind k) { return k == ClassKind::Interface ? "interface" : k == ClassKind::Trait ? "trait" : "class"; };

    if (top.kind == ClassKind::Interface)
        fail("cannot instantiate interface `" + className + "`",
             "an interface only declares method signatures; write a class that `implements " + className + "` and instantiate that");
    if (top.kind == ClassKind::Trait)
        fail("cannot instantiate trait `" + className + "`",
             "a trait is mixed into classes with `uses " + className + "`; it cannot be created on its own");
    if (top.isAbstract)
        fail("cannot instantiate abstract class `" + className + "`",
             "`" + className + "` is declared `abstract`; create a concrete subclass (`class X extends " + className + "`) and instantiate that");

    std::vector<std::string> chain = classChain(className); // [self, parent, ..., root]

    // --- الأب: صنف عادي وغير final؛ السمات: سمات فعلاً ---
    for (auto& cn : chain) {
        const ClassDef& d = classes.at(cn);
        if (!d.superclass.empty()) {
            auto pit = classes.find(d.superclass);
            if (pit != classes.end()) {
                if (pit->second.kind != ClassKind::Class)
                    fail("class `" + cn + "` cannot extend " + kindName(pit->second.kind) + " `" + d.superclass + "`",
                         std::string("use `implements` for interfaces and `uses` for traits; `extends` is for classes only"));
                if (pit->second.isFinal)
                    fail("class `" + cn + "` cannot extend final class `" + d.superclass + "`",
                         "`" + d.superclass + "` is declared `final class`, so it can never be extended");
            }
        }
        for (auto& t : d.traits) {
            auto tit = classes.find(t);
            if (tit == classes.end()) fail("class `" + cn + "` uses unknown trait `" + t + "`", "no trait named `" + t + "` is defined");
            if (tit->second.kind != ClassKind::Trait)
                fail("`" + t + "` is a " + kindName(tit->second.kind) + ", not a trait",
                     "only a `trait` can appear after `uses`");
        }
    }

    // --- الواجهات موجودة وفعلاً interface ---
    std::vector<std::string> ifaces;
    collectInterfaces(className, ifaces);
    for (auto& i : ifaces) {
        auto iit = classes.find(i);
        if (iit == classes.end()) fail("class `" + className + "` implements unknown interface `" + i + "`", "no interface named `" + i + "` is defined");
        if (iit->second.kind != ClassKind::Interface)
            fail("`" + i + "` is a " + kindName(iit->second.kind) + ", not an interface",
                 "`implements` only accepts interfaces; use `extends` for a class or `uses` for a trait");
    }

    // --- override / final على كل دالة مُعرَّفة في السلسلة ---
    for (auto& cn : chain) {
        const ClassDef& d = classes.at(cn);
        // كل ما هو "فوق" d: أسلاف الصنف + كل السمات + كل الواجهات.
        std::vector<std::string> above;
        if (!d.superclass.empty()) for (auto& p : classChain(d.superclass)) above.push_back(p);
        std::vector<std::string> ts, is;
        collectTraits(cn, ts);
        collectInterfaces(cn, is);
        for (auto& t : ts) above.push_back(t);
        for (auto& i : is) above.push_back(i);

        auto checkFn = [&](const std::shared_ptr<FunctionStmt>& m) {
            bool declaredAbove = false, finalAbove = false;
            std::string finalIn;
            for (auto& an : above) {
                auto ait = classes.find(an);
                if (ait == classes.end()) continue;
                const auto& mm = m->accessorKind == 0 ? ait->second.methods : (m->accessorKind == 1 ? ait->second.getters : ait->second.setters);
                auto f = mm.find(m->name);
                if (f != mm.end()) {
                    declaredAbove = true;
                    if (f->second->isFinal) { finalAbove = true; finalIn = an; }
                }
            }
            if (m->isOverride && !declaredAbove)
                fail("`" + cn + "." + m->name + "` is marked `override` but no ancestor declares `" + m->name + "`",
                     "`override` asserts that the method replaces one from a parent class, trait or interface; check the name for a typo");
            if (finalAbove)
                fail("`" + cn + "." + m->name + "` cannot override final method `" + m->name + "` of `" + finalIn + "`",
                     "`" + finalIn + "." + m->name + "` is declared `final fun`, so subclasses may not redefine it");
        };
        for (auto& kv : d.methods) checkFn(kv.second);
        for (auto& kv : d.getters) checkFn(kv.second);
        for (auto& kv : d.setters) checkFn(kv.second);
    }

    // --- كل دالة abstract / توقيع واجهة لها تنفيذ فعلي بنفس عدد الوسائط ---
    struct Req { std::string from; size_t arity; };
    std::map<std::string, Req> required;
    auto addAbstract = [&](const ClassDef& d, const std::string& dname) {
        for (auto& kv : d.methods)
            if (kv.second->isAbstract && !required.count(kv.first)) required[kv.first] = {dname, kv.second->params.size()};
    };
    for (auto& cn : chain) addAbstract(classes.at(cn), cn);
    std::vector<std::string> traits;
    collectTraits(className, traits);
    for (auto& t : traits) addAbstract(classes.at(t), t);
    for (auto& i : ifaces) addAbstract(classes.at(i), i);
    for (auto& kv : required) {
        std::string owner;
        auto impl = findMethod(className, kv.first, &owner);
        if (!impl)
            fail("class `" + className + "` must implement method `" + kv.first + "` (required by `" + kv.second.from + "`)",
                 "declare `fun " + kv.first + "(...) { ... }` in `" + className + "` (or inherit a concrete one) to satisfy `" + kv.second.from + "`");
        if (impl->params.size() != kv.second.arity)
            fail("method `" + owner + "." + kv.first + "` takes " + std::to_string(impl->params.size()) + " parameter(s) but `" +
                     kv.second.from + "` declares " + std::to_string(kv.second.arity),
                 "an implementation must have the same number of parameters as the abstract signature it fulfils");
    }

    validatedClasses_.insert(className);
}

// ---------------------------------------------------------------------------
// 3) الأعضاء static
// ---------------------------------------------------------------------------
void Interpreter::ensureStatics(const std::string& cls) {
    auto it = classes.find(cls);
    if (it == classes.end()) return;
    ClassDef* d = &it->second;
    if (d->staticsInitialized) return;
    d->staticsInitialized = true; // قبل التقييم: يمنع التكرار اللانهائي لو أشار مُهيِّئ static لنفسه
    auto env = std::make_shared<Environment>(globals);
    env->define("__class__", Value::string(cls));
    auto defs = d->staticFieldDefs; // نسخة: قد يُعاد تسجيل أصناف أثناء التقييم
    for (auto& fd : defs) {
        Value v = fd.second ? copyForBinding(evaluate(fd.second, env)) : Value::nil();
        auto it2 = classes.find(cls);
        if (it2 == classes.end()) return;
        it2->second.staticValues[fd.first] = v;
    }
}

ClassDef* Interpreter::findStaticFieldOwner(const std::string& cls, const std::string& name) {
    for (auto& cur : classChain(cls)) {
        auto it = classes.find(cur);
        if (it != classes.end() && it->second.staticMeta.count(name)) return &it->second;
    }
    return nullptr;
}

std::shared_ptr<FunctionStmt> Interpreter::findStaticMethod(const std::string& cls, const std::string& name,
                                                             std::string* ownerOut) const {
    for (auto& cur : classChain(cls)) {
        auto it = classes.find(cur);
        if (it == classes.end()) continue;
        auto m = it->second.staticMethods.find(name);
        if (m != it->second.staticMethods.end()) { if (ownerOut) *ownerOut = cur; return m->second; }
    }
    return nullptr;
}

Value Interpreter::bindStaticFn(const std::shared_ptr<FunctionStmt>& m, const std::string& owner) {
    auto callable = std::make_shared<Callable>();
    callable->declaration = m;
    auto closureEnv = std::make_shared<Environment>(globals);
    closureEnv->define("__class__", Value::string(owner));
    closureEnv->define("__method__", Value::string(m->name));
    callable->closure = closureEnv;
    Value v;
    v.type = Value::Type::FUNCTION;
    v.function = callable;
    return v;
}

Value Interpreter::staticGet(const std::string& cls, const std::string& name, const EnvPtr& env, int line) {
    if (ClassDef* owner = findStaticFieldOwner(cls, name)) {
        std::string ownerName = owner->name;
        checkMemberAccess(ownerName, owner->staticMeta[name].access, name, "static field", env, line);
        ensureStatics(ownerName);
        return classes.at(ownerName).staticValues[name];
    }
    std::string mo;
    if (auto m = findStaticMethod(cls, name, &mo)) {
        checkMemberAccess(mo, m->access, name, "static method", env, line);
        return bindStaticFn(m, mo);
    }
    throw errWithReason(diag::Code::E0001_UndefinedVariable, line,
                        "class `" + cls + "` has no static member `" + name + "`",
                        "`" + cls + "` (and its ancestors) declare no `static let " + name + "` or `static fun " + name + "`");
}

Value Interpreter::staticSet(const std::string& cls, const std::string& name, const Value& v, const EnvPtr& env, int line) {
    ClassDef* owner = findStaticFieldOwner(cls, name);
    if (!owner) {
        throw errWithReason(diag::Code::E0001_UndefinedVariable, line,
                            "class `" + cls + "` has no static field `" + name + "`",
                            "declare it first inside the class with `static let " + name + " = ...;`");
    }
    std::string ownerName = owner->name;
    const auto& meta = owner->staticMeta[name];
    checkMemberAccess(ownerName, meta.access, name, "static field", env, line);
    if (meta.isFinal) {
        throw errWithReason(diag::Code::E0035_RuntimeError, line,
                            "cannot assign to final static field `" + ownerName + "." + name + "`",
                            "`" + name + "` is declared `final`, so its initial value can never change");
    }
    ensureStatics(ownerName);
    Value stored = copyForBinding(v);
    classes.at(ownerName).staticValues[name] = stored;
    return stored;
}

Value Interpreter::callStatic(const std::string& cls, const std::string& name, std::vector<Value>& args,
                              const EnvPtr& env, int line) {
    std::string mo;
    if (auto m = findStaticMethod(cls, name, &mo)) {
        checkMemberAccess(mo, m->access, name, "static method", env, line);
        Value bound = bindStaticFn(m, mo);
        return callFunction(bound.function, args, line);
    }
    // حقل static يحمل دالة (callback)
    if (ClassDef* owner = findStaticFieldOwner(cls, name)) {
        std::string ownerName = owner->name;
        ensureStatics(ownerName);
        Value v = classes.at(ownerName).staticValues[name];
        if (v.type == Value::Type::FUNCTION) return callFunction(v.function, args, line);
    }
    throw errWithReason(diag::Code::E0006_UnknownFunction, line,
                        "class `" + cls + "` has no static method `" + name + "`",
                        "`" + cls + "` (and its ancestors) declare no `static fun " + name + "(...)`");
}

// ---------------------------------------------------------------------------
// 4) الصلاحيات + قراءة/كتابة/استدعاء الأعضاء
// ---------------------------------------------------------------------------
std::string Interpreter::currentClassCtx(const EnvPtr& env) const {
    Value v;
    if (env && env->get("__class__", v) && v.type == Value::Type::STRING) return v.str;
    return std::string();
}

void Interpreter::checkMemberAccess(const std::string& declClass, const std::string& access, const std::string& member,
                                    const char* what, const EnvPtr& env, int line) const {
    if (access.empty()) return;
    std::string ctx = currentClassCtx(env);
    bool ok = false;
    if (!ctx.empty()) {
        if (ctx == declClass) ok = true;
        else if (access == "protected" && classIsSubclassOf(ctx, declClass)) ok = true;
    }
    if (ok) return;
    throw errWithReason(diag::Code::E0035_RuntimeError, line,
                        "cannot access " + access + " " + what + " `" + member + "` of class `" + declClass + "`",
                        access == "private"
                            ? "`" + member + "` is `private`: it can only be used from inside the methods of `" + declClass + "` itself"
                            : "`" + member + "` is `protected`: it can only be used from inside `" + declClass + "` and its subclasses");
}

Value Interpreter::readMember(const Value& obj, const std::string& name, const EnvPtr& env, int line) {
    if (obj.type == Value::Type::INSTANCE && obj.instance) {
        auto& inst = *obj.instance;
        // خاصية get name() — إلا حين نكون داخل getter نفسه (فيقرأ الحقل الخام بدل التكرار اللانهائي)
        if (accessorsExist_) {
            Value m;
            bool inside = env && env->get("__method__", m) && m.type == Value::Type::STRING && m.str == "get " + name;
            if (!inside) {
                std::string owner;
                auto g = findAccessor(inst.className, name, false, &owner);
                if (g) {
                    checkMemberAccess(owner, g->access, name, "property", env, line);
                    std::vector<Value> none;
                    Value bound = bindMethod(obj, g, owner);
                    return callFunction(bound.function, none, line);
                }
            }
        }
        auto fIt = inst.fields.find(name);
        if (fIt != inst.fields.end()) {
            if (restrictedMembers_) {
                std::string decl;
                if (auto* fm = findFieldMeta(inst.className, name, &decl))
                    checkMemberAccess(decl, fm->access, name, "field", env, line);
            }
            return fIt->second;
        }
        std::string owner;
        auto method = findMethod(inst.className, name, &owner);
        if (method) {
            if (restrictedMembers_) checkMemberAccess(owner, method->access, name, "method", env, line);
            return bindMethod(obj, method, owner);
        }
        throw errWithReason(diag::Code::E0001_UndefinedVariable, line,
                            "no field or method named `" + name + "` on `" + inst.className + "`",
                            "`" + inst.className + "` has neither a field nor a method called `" + name + "`");
    }
    if (obj.type == Value::Type::MAP) {
        for (auto& kv : *obj.map)
            if (kv.first.type == Value::Type::STRING && kv.first.str == name) return kv.second;
        return Value::nil();
    }
    throw diagErr(diag::Code::E0004_InvalidType, line,
                  "cannot access property `." + name + "` on a value of type `" + obj.typeName() + "`");
}

Value Interpreter::writeMember(const Value& obj, const std::string& name, const Value& val, const EnvPtr& env, int line) {
    auto& inst = *obj.instance;
    if (inst.frozen) {
        throw errWithReason(diag::Code::E0035_RuntimeError, line,
                            "cannot modify field `" + name + "` of a frozen `" + inst.className + "` object",
                            "this object was frozen with `oop.freeze(...)`; frozen objects are read-only");
    }
    if (accessorsExist_) {
        Value m;
        bool have = env && env->get("__method__", m) && m.type == Value::Type::STRING;
        bool insideSet = have && m.str == "set " + name;
        bool insideGet = have && m.str == "get " + name;
        std::string owner;
        auto setter = findAccessor(inst.className, name, true, &owner);
        if (setter && !insideSet) {
            checkMemberAccess(owner, setter->access, name, "property", env, line);
            Value oldProp = Value::nil();
            if (observersExist_) { // قيمة الخاصية قبل الكتابة (إن كان لها getter) لأجل المراقبين
                if (auto g = findAccessor(inst.className, name, false)) {
                    std::vector<Value> none; Value gb = bindMethod(obj, g, owner);
                    oldProp = callFunction(gb.function, none, line);
                }
            }
            std::vector<Value> a{val};
            Value bound = bindMethod(obj, setter, owner);
            callFunction(bound.function, a, line);
            if (observersExist_) {
                Value newProp = val;
                if (auto g = findAccessor(inst.className, name, false)) {
                    std::vector<Value> none; Value gb = bindMethod(obj, g, owner);
                    newProp = callFunction(gb.function, none, line);
                }
                notifyObservers(obj, name, oldProp, newProp, line);
            }
            return val;
        }
        if (!setter && !insideGet && findAccessor(inst.className, name, false, nullptr)) {
            throw errWithReason(diag::Code::E0035_RuntimeError, line,
                                "property `" + name + "` of `" + inst.className + "` is read-only",
                                "it has a `get " + name + "()` but no `set " + name + "(v)`; "
                                "store the value in a differently-named backing field (e.g. `_" + name + "`)");
        }
    }
    if (restrictedMembers_) {
        std::string decl;
        if (auto* fm = findFieldMeta(inst.className, name, &decl)) {
            checkMemberAccess(decl, fm->access, name, "field", env, line);
            if (fm->isFinal) {
                Value m;
                bool inInit = env && env->get("__method__", m) && m.type == Value::Type::STRING && m.str == "init";
                if (!inInit)
                    throw errWithReason(diag::Code::E0035_RuntimeError, line,
                                        "cannot assign to final field `" + name + "` of `" + inst.className + "`",
                                        "`" + name + "` is declared `final let`: it may only be assigned inside `init`");
            }
        }
    }
    Value stored = copyForBinding(val);
    Value oldVal = Value::nil();
    if (observersExist_) { auto oit = inst.fields.find(name); if (oit != inst.fields.end()) oldVal = oit->second; }
    if (!inst.fields.count(name)) inst.fieldOrder.push_back(name);
    inst.fields[name] = stored;
    if (observersExist_) notifyObservers(obj, name, oldVal, stored, line);
    return stored;
}

Value Interpreter::callMethodOn(const Value& obj, const std::string& method, std::vector<Value>& args,
                                const EnvPtr& env, int line) {
    if (obj.type == Value::Type::INSTANCE && obj.instance) {
        auto& inst = *obj.instance;
        auto fIt = inst.fields.find(method);
        if (fIt != inst.fields.end() && fIt->second.type == Value::Type::FUNCTION) {
            return callFunction(fIt->second.function, args, line);
        }
        std::string owner;
        auto m = findMethod(inst.className, method, &owner);
        if (!m) {
            throw errWithReason(diag::Code::E0006_UnknownFunction, line,
                                "`" + inst.className + "` has no method `" + method + "`",
                                "no method or callable field named `" + method + "` exists on `" + inst.className + "`");
        }
        if (restrictedMembers_) checkMemberAccess(owner, m->access, method, "method", env, line);
        Value bound = bindMethod(obj, m, owner);
        return callFunction(bound.function, args, line);
    }
    if (obj.type == Value::Type::MAP) {
        for (auto& kv : *obj.map)
            if (kv.first.type == Value::Type::STRING && kv.first.str == method && kv.second.type == Value::Type::FUNCTION)
                return callFunction(kv.second.function, args, line);
    }
    throw diagErr(diag::Code::E0004_InvalidType, line,
                  "cannot call method `." + method + "` on a value of type `" + obj.typeName() + "`");
}

Value Interpreter::callValue(const Value& callee, std::vector<Value>& args, int line) {
    if (callee.type == Value::Type::FUNCTION && callee.function) return callFunction(callee.function, args, line);
    if (callee.type == Value::Type::INSTANCE) {
        if (auto r = callMagic(callee, "__call__", args, line)) return *r;
    }
    throw diagErr(diag::Code::E0004_InvalidType, line, "cannot call a value of type `" + callee.typeName() + "`");
}

// ---------------------------------------------------------------------------
// 5) العوامل السحرية الإضافية
// ---------------------------------------------------------------------------
bool Interpreter::hasMagic(const Value& inst, const std::string& magic) const {
    return inst.type == Value::Type::INSTANCE && inst.instance && findMethod(inst.instance->className, magic) != nullptr;
}

std::optional<Value> Interpreter::callMagic(const Value& inst, const std::string& magic, std::vector<Value>& args, int line) {
    if (inst.type != Value::Type::INSTANCE || !inst.instance) return std::nullopt;
    std::string owner;
    auto m = findMethod(inst.instance->className, magic, &owner);
    if (!m) return std::nullopt;
    Value bound = bindMethod(inst, m, owner);
    return callFunction(bound.function, args, line);
}

std::optional<Value> Interpreter::tryCompareOverload(TokenType op, const Value& l, const Value& r, int line) {
    const char* direct;
    const char* swapped;
    switch (op) {
        case TokenType::LESS:          direct = "__lt__"; swapped = "__gt__"; break;
        case TokenType::LESS_EQUAL:    direct = "__le__"; swapped = "__ge__"; break;
        case TokenType::GREATER:       direct = "__gt__"; swapped = "__lt__"; break;
        default:                       direct = "__ge__"; swapped = "__le__"; break;
    }
    if (hasMagic(l, direct)) { std::vector<Value> a{r}; return callMagic(l, direct, a, line); }
    if (hasMagic(r, swapped)) { std::vector<Value> a{l}; return callMagic(r, swapped, a, line); }
    auto fromCmp = [&](const Value& inst, const Value& other, bool instIsLeft) -> std::optional<Value> {
        std::vector<Value> a{other};
        Value res = *callMagic(inst, "__cmp__", a, line);
        if (res.type != Value::Type::NUMBER)
            throw diagErr(diag::Code::E0004_InvalidType, line,
                          "`__cmp__` of `" + inst.typeName() + "` must return a number (negative, zero or positive)");
        double c = instIsLeft ? res.number : -res.number;
        switch (op) {
            case TokenType::LESS:       return Value::boolean_(c < 0);
            case TokenType::LESS_EQUAL: return Value::boolean_(c <= 0);
            case TokenType::GREATER:    return Value::boolean_(c > 0);
            default:                    return Value::boolean_(c >= 0);
        }
    };
    if (hasMagic(l, "__cmp__")) return fromCmp(l, r, true);
    if (hasMagic(r, "__cmp__")) return fromCmp(r, l, false);
    return std::nullopt;
}

std::optional<bool> Interpreter::tryEqOverload(const Value& l, const Value& r, bool negate, int line) {
    if (negate && hasMagic(l, "__ne__")) {
        std::vector<Value> a{r};
        return callMagic(l, "__ne__", a, line)->isTruthy();
    }
    if (hasMagic(l, "__eq__")) {
        std::vector<Value> a{r};
        bool b = callMagic(l, "__eq__", a, line)->isTruthy();
        return negate ? !b : b;
    }
    if (hasMagic(r, "__eq__")) {
        std::vector<Value> a{l};
        bool b = callMagic(r, "__eq__", a, line)->isTruthy();
        return negate ? !b : b;
    }
    return std::nullopt;
}

bool Interpreter::tryInstanceToString(const Value& v, std::string& out) {
    if (v.type != Value::Type::INSTANCE || !v.instance) return false;
    std::string owner;
    auto m = findMethod(v.instance->className, "__str__", &owner);
    if (!m) m = findMethod(v.instance->className, "toString", &owner);
    if (!m || !m->params.empty()) return false;
    std::vector<Value> none;
    Value bound = bindMethod(v, m, owner);
    Value res = callFunction(bound.function, none, m->line);
    out = res.toDisplayString();
    return true;
}

} // namespace rin

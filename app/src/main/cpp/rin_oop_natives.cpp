// ============================================================================
//  rin_oop_natives.cpp — Rin 1.4: دوال oop.* (استبطان الأصناف + أدوات الكائنات)
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp (بعد rin_oop.cpp) ويُسجَّل من
//  Interpreter::registerNativesOop().  كل الدوال داخل مساحة الأسماء  oop.*  فلا تتصادم مع أي
//  دالة/متغيّر موجود (مثل getField/hasField الخاصة بالحاويات).
//
//  المجموعات:
//    1) أنواع ووراثة   : className isInstance isClass isInterface isTrait isAbstract isFinal isStruct exists
//                         isSubclass parent ancestors children descendants classes interfaces traits implements
//    2) استبطان الأعضاء : methods staticMethods abstractMethods fields staticFields properties
//                         hasMethod hasField hasProperty hasStatic arity params signature access describe
//    3) عمليات ديناميكية: get set peek poke call callStatic new newWith staticGet staticSet extend delete
//    4) أدوات الكائنات  : toMap fromMap entries pick omit assign clone deepClone equals same hash
//                         toString inspect freeze isFrozen
//    5) أنماط تصميم     : singleton hasSingleton resetSingleton
//    6) مصفوفات كائنات  : compare sort sortBy min max pluck groupBy findBy filterBy
//    7) تغليف len/contains لتعمل مع __len__/__contains__
//
//  ملاحظة صلاحيات: get/set/call/new تحترم private/protected (تُعامَل كأنها نداء من خارج أي صنف)،
//  بينما peek/poke/toMap/inspect "مفاتيح استبطان" تتجاوزها عمداً (للتشخيص والتسلسل والاختبار).
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace oopn {
using Args = std::vector<Value>;
using namespace rin::extra;

static Value S(const std::string& s) { return Value::string(s); }
static Value N(double d) { return Value::num(d); }
static Value B(bool b) { return Value::boolean_(b); }

static Value strArray(const std::vector<std::string>& v) {
    ArrayData out;
    out.reserve(v.size());
    for (auto& s : v) out.push_back(Value::string(s));
    return newArray(std::move(out));
}
static std::vector<std::string> sortedUnique(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}
static Value M(std::vector<std::pair<std::string, Value>> kv) {
    MapData out;
    out.reserve(kv.size());
    for (auto& p : kv) out.push_back({Value::string(p.first), std::move(p.second)});
    return newMap(std::move(out));
}
} // namespace oopn

void Interpreter::registerNativesOop() {
    using namespace oopn;

    // ---- مساعدات مشتركة ----
    // اسم صنف من وسيط: كائن (className) أو نص (اسم صنف موجود).
    auto clsOf = [this](const Value& v, const std::string& fn, int line) -> std::string {
        if (v.type == Value::Type::INSTANCE && v.instance) return v.instance->className;
        if (v.type == Value::Type::STRING) {
            if (!classes.count(v.str))
                throw errWithReason(diag::Code::E0001_UndefinedVariable, line, fn + ": unknown class `" + v.str + "`",
                                    "no class, struct, interface or trait named `" + v.str + "` is defined");
            return v.str;
        }
        throw diagErr(diag::Code::E0004_InvalidType, line,
                      "'" + fn + "' expects an object or a class name but got " + v.typeName());
    };
    auto instOf = [](const Value& v, const std::string& fn, int line) -> InstanceData& {
        if (v.type != Value::Type::INSTANCE || !v.instance)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an object but got " + v.typeName());
        return *v.instance;
    };
    auto argsFromArray = [](const Value& v, const std::string& fn, int line) -> std::vector<Value> {
        if (v.type == Value::Type::NIL) return {};
        if (v.type != Value::Type::ARRAY || !v.array)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an array of arguments but got " + v.typeName());
        return *v.array;
    };

    // أسماء الدوال: inherited=true يشمل الأسلاف والسمات.
    auto methodNames = [this](const std::string& cls, bool inherited) {
        std::vector<std::string> out;
        auto addDef = [&](const ClassDef& d) { for (auto& kv : d.methods) if (!kv.second->isAbstract) out.push_back(kv.first); };
        std::vector<std::string> chain = classChain(cls);
        if (!inherited && chain.size() > 1) chain.resize(1);
        for (auto& c : chain) {
            addDef(classes.at(c));
            std::vector<std::string> ts;
            std::function<void(const ClassDef&)> walk = [&](const ClassDef& d) {
                for (auto& t : d.traits) {
                    if (std::find(ts.begin(), ts.end(), t) != ts.end()) continue;
                    auto it = classes.find(t);
                    if (it == classes.end()) continue;
                    ts.push_back(t);
                    walk(it->second);
                }
            };
            walk(classes.at(c));
            for (auto& t : ts) addDef(classes.at(t));
        }
        return sortedUnique(out);
    };
    // أسماء الحقول المُعرَّفة (بترتيب: الجذر -> الصنف، مع حقول السمات قبل حقول كل صنف).
    auto fieldNames = [this](const std::string& cls) {
        std::vector<std::string> out;
        auto add = [&](const std::string& n) { if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n); };
        std::vector<std::string> chain = classChain(cls);
        std::reverse(chain.begin(), chain.end());
        for (auto& c : chain) {
            const ClassDef& d = classes.at(c);
            std::vector<std::string> ts;
            std::function<void(const ClassDef&)> walk = [&](const ClassDef& x) {
                for (auto& t : x.traits) {
                    if (std::find(ts.begin(), ts.end(), t) != ts.end()) continue;
                    auto it = classes.find(t);
                    if (it == classes.end()) continue;
                    ts.push_back(t);
                    walk(it->second);
                    for (auto& f : it->second.fieldDefs) add(f.first);
                }
            };
            walk(d);
            for (auto& f : d.fieldDefs) add(f.first);
        }
        return out;
    };
    auto staticMethodNames = [this](const std::string& cls) {
        std::vector<std::string> out;
        for (auto& c : classChain(cls)) for (auto& kv : classes.at(c).staticMethods) out.push_back(kv.first);
        return sortedUnique(out);
    };
    auto staticFieldNames = [this](const std::string& cls) {
        std::vector<std::string> out;
        std::vector<std::string> chain = classChain(cls);
        std::reverse(chain.begin(), chain.end());
        for (auto& c : chain) for (auto& n : classes.at(c).staticOrder)
            if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
        return out;
    };
    auto propertyNames = [this](const std::string& cls) {
        std::vector<std::string> out;
        for (auto& c : classChain(cls)) {
            const ClassDef& d = classes.at(c);
            for (auto& kv : d.getters) out.push_back(kv.first);
            for (auto& kv : d.setters) out.push_back(kv.first);
        }
        return sortedUnique(out);
    };
    // أسماء الدوال المجرَّدة (abstract/توقيع واجهة) التي لم يُنفَّذ لها جسم بعد في سلسلة الصنف.
    auto abstractNames = [this](const std::string& cls) {
        std::vector<std::string> out;
        std::vector<std::string> sources = classChain(cls);
        std::vector<std::string> ts, is;
        collectTraits(cls, ts);
        collectInterfaces(cls, is);
        for (auto& t : ts) sources.push_back(t);
        for (auto& i : is) sources.push_back(i);
        for (auto& n : sources) {
            auto it = classes.find(n);
            if (it == classes.end()) continue;
            for (auto& kv : it->second.methods)
                if (kv.second->isAbstract && !findMethod(cls, kv.first)) out.push_back(kv.first); // غير المنفَّذ بعد فقط
        }
        return sortedUnique(out);
    };
    auto kindName = [this](const std::string& cls) -> std::string {
        const ClassDef& d = classes.at(cls);
        if (d.kind == ClassKind::Interface) return "interface";
        if (d.kind == ClassKind::Trait) return "trait";
        return d.isStruct ? "struct" : "class";
    };
    // هل للكائن عضو بهذا الاسم (حقل/دالة/خاصية)؟
    auto hasMember = [this](const Value& v, const std::string& name) -> bool {
        if (v.type == Value::Type::MAP) { for (auto& kv : *v.map) if (kv.first.type == Value::Type::STRING && kv.first.str == name) return true; return false; }
        if (v.type != Value::Type::INSTANCE || !v.instance) return false;
        return v.instance->fields.count(name) || findMethod(v.instance->className, name) || findAccessor(v.instance->className, name, false);
    };
    // قراءة عضو عام (احترام الصلاحيات) بقيمة افتراضية nil إن لم يوجد.
    auto getMember = [this, hasMember](const Value& v, const std::string& name, int line) -> Value {
        if (!hasMember(v, name)) return Value::nil();
        return readMember(v, name, nullptr, line);
    };

    // ======================================================== 1) أنواع ووراثة
    natives["oop.className"] = [this](Args& a, int line) -> Value {
        need("oop.className", a, 1, 1, line);
        if (a[0].type == Value::Type::INSTANCE) return S(a[0].instance->className);
        if (a[0].type == Value::Type::STRING && classes.count(a[0].str)) return a[0];
        return S(a[0].typeName());
    };
    natives["oop.typeOf"] = natives["oop.className"];
    natives["oop.isInstance"] = [this](Args& a, int line) -> Value {
        need("oop.isInstance", a, 2, 2, line);
        std::string t = str(a[1], "oop.isInstance", line);
        const Value& v = a[0];
        if (t == "Any") return B(true);
        if (t == "Number") return B(v.type == Value::Type::NUMBER);
        if (t == "Int") return B(v.type == Value::Type::NUMBER && std::isfinite(v.number) && v.number == std::floor(v.number));
        if (t == "String") return B(v.type == Value::Type::STRING);
        if (t == "Bool") return B(v.type == Value::Type::BOOL);
        if (t == "Array") return B(v.type == Value::Type::ARRAY);
        if (t == "Map") return B(v.type == Value::Type::MAP);
        if (t == "Function") return B(v.type == Value::Type::FUNCTION);
        return B(isInstanceOf(v, t));
    };
    natives["oop.is"] = natives["oop.isInstance"];
    natives["oop.exists"] = [this](Args& a, int line) -> Value { need("oop.exists", a, 1, 1, line); return B(classes.count(str(a[0], "oop.exists", line)) > 0); };
    natives["oop.isClass"] = [this](Args& a, int line) -> Value {
        need("oop.isClass", a, 1, 1, line);
        auto it = classes.find(str(a[0], "oop.isClass", line));
        return B(it != classes.end() && it->second.kind == ClassKind::Class);
    };
    natives["oop.isInterface"] = [this](Args& a, int line) -> Value {
        need("oop.isInterface", a, 1, 1, line);
        auto it = classes.find(str(a[0], "oop.isInterface", line));
        return B(it != classes.end() && it->second.kind == ClassKind::Interface);
    };
    natives["oop.isTrait"] = [this](Args& a, int line) -> Value {
        need("oop.isTrait", a, 1, 1, line);
        auto it = classes.find(str(a[0], "oop.isTrait", line));
        return B(it != classes.end() && it->second.kind == ClassKind::Trait);
    };
    natives["oop.isAbstract"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.isAbstract", a, 1, 1, line);
        const ClassDef& d = classes.at(clsOf(a[0], "oop.isAbstract", line));
        return B(d.isAbstract || d.kind != ClassKind::Class);
    };
    natives["oop.isFinal"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.isFinal", a, 1, 1, line);
        return B(classes.at(clsOf(a[0], "oop.isFinal", line)).isFinal);
    };
    natives["oop.isStruct"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.isStruct", a, 1, 1, line);
        return B(classes.at(clsOf(a[0], "oop.isStruct", line)).isStruct);
    };
    natives["oop.isSubclass"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.isSubclass", a, 2, 2, line);
        return B(typeNameIsAncestor(clsOf(a[0], "oop.isSubclass", line), clsOf(a[1], "oop.isSubclass", line)));
    };
    natives["oop.parent"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.parent", a, 1, 1, line);
        const std::string& p = classes.at(clsOf(a[0], "oop.parent", line)).superclass;
        return p.empty() ? Value::nil() : S(p);
    };
    natives["oop.ancestors"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.ancestors", a, 1, 1, line);
        auto chain = classChain(clsOf(a[0], "oop.ancestors", line));
        if (!chain.empty()) chain.erase(chain.begin());
        return strArray(chain);
    };
    natives["oop.children"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.children", a, 1, 1, line);
        std::string c = clsOf(a[0], "oop.children", line);
        std::vector<std::string> out;
        for (auto& kv : classes) if (kv.second.superclass == c && kv.second.kind == ClassKind::Class) out.push_back(kv.first);
        return strArray(sortedUnique(out));
    };
    natives["oop.descendants"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.descendants", a, 1, 1, line);
        std::string c = clsOf(a[0], "oop.descendants", line);
        std::vector<std::string> out;
        for (auto& kv : classes)
            if (kv.first != c && kv.second.kind == ClassKind::Class && classIsSubclassOf(kv.first, c)) out.push_back(kv.first);
        return strArray(sortedUnique(out));
    };
    // oop.classes(kind?) — kind: "class" (افتراضي) | "interface" | "trait" | "all"
    natives["oop.classes"] = [this](Args& a, int line) -> Value {
        need("oop.classes", a, 0, 1, line);
        std::string k = a.empty() ? "class" : str(a[0], "oop.classes", line);
        std::vector<std::string> out;
        for (auto& kv : classes) {
            bool take = k == "all" ||
                        (k == "class" && kv.second.kind == ClassKind::Class) ||
                        (k == "interface" && kv.second.kind == ClassKind::Interface) ||
                        (k == "trait" && kv.second.kind == ClassKind::Trait);
            if (take) out.push_back(kv.first);
        }
        return strArray(sortedUnique(out));
    };
    natives["oop.interfaces"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.interfaces", a, 1, 1, line);
        std::vector<std::string> v;
        collectInterfaces(clsOf(a[0], "oop.interfaces", line), v);
        return strArray(sortedUnique(v));
    };
    natives["oop.traits"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.traits", a, 1, 1, line);
        std::vector<std::string> v;
        collectTraits(clsOf(a[0], "oop.traits", line), v);
        return strArray(sortedUnique(v));
    };
    natives["oop.implements"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.implements", a, 2, 2, line);
        std::vector<std::string> v;
        collectInterfaces(clsOf(a[0], "oop.implements", line), v);
        return B(std::find(v.begin(), v.end(), str(a[1], "oop.implements", line)) != v.end());
    };

    // ======================================================== 2) استبطان الأعضاء
    natives["oop.methods"] = [clsOf, methodNames](Args& a, int line) -> Value {
        need("oop.methods", a, 1, 2, line);
        bool inherited = a.size() > 1 ? a[1].isTruthy() : true;
        return strArray(methodNames(clsOf(a[0], "oop.methods", line), inherited));
    };
    natives["oop.staticMethods"] = [clsOf, staticMethodNames](Args& a, int line) -> Value {
        need("oop.staticMethods", a, 1, 1, line);
        return strArray(staticMethodNames(clsOf(a[0], "oop.staticMethods", line)));
    };
    natives["oop.abstractMethods"] = [clsOf, abstractNames](Args& a, int line) -> Value {
        need("oop.abstractMethods", a, 1, 1, line);
        return strArray(abstractNames(clsOf(a[0], "oop.abstractMethods", line)));
    };
    natives["oop.fields"] = [clsOf, fieldNames](Args& a, int line) -> Value {
        need("oop.fields", a, 1, 1, line);
        if (a[0].type == Value::Type::INSTANCE && a[0].instance) {
            std::vector<std::string> out;
            for (auto& n : a[0].instance->fieldOrder) if (a[0].instance->fields.count(n)) out.push_back(n);
            return strArray(out);
        }
        return strArray(fieldNames(clsOf(a[0], "oop.fields", line)));
    };
    natives["oop.staticFields"] = [clsOf, staticFieldNames](Args& a, int line) -> Value {
        need("oop.staticFields", a, 1, 1, line);
        return strArray(staticFieldNames(clsOf(a[0], "oop.staticFields", line)));
    };
    natives["oop.properties"] = [clsOf, propertyNames](Args& a, int line) -> Value {
        need("oop.properties", a, 1, 1, line);
        return strArray(propertyNames(clsOf(a[0], "oop.properties", line)));
    };
    natives["oop.hasMethod"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.hasMethod", a, 2, 2, line);
        return B(findMethod(clsOf(a[0], "oop.hasMethod", line), str(a[1], "oop.hasMethod", line)) != nullptr);
    };
    natives["oop.hasField"] = [this, clsOf, fieldNames](Args& a, int line) -> Value {
        need("oop.hasField", a, 2, 2, line);
        std::string n = str(a[1], "oop.hasField", line);
        if (a[0].type == Value::Type::INSTANCE && a[0].instance) return B(a[0].instance->fields.count(n) > 0);
        auto f = fieldNames(clsOf(a[0], "oop.hasField", line));
        return B(std::find(f.begin(), f.end(), n) != f.end());
    };
    natives["oop.hasProperty"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.hasProperty", a, 2, 2, line);
        std::string c = clsOf(a[0], "oop.hasProperty", line), n = str(a[1], "oop.hasProperty", line);
        return B(findAccessor(c, n, false) != nullptr || findAccessor(c, n, true) != nullptr);
    };
    natives["oop.hasStatic"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.hasStatic", a, 2, 2, line);
        std::string c = clsOf(a[0], "oop.hasStatic", line), n = str(a[1], "oop.hasStatic", line);
        return B(findStaticFieldOwner(c, n) != nullptr || findStaticMethod(c, n) != nullptr);
    };
    // دالة instance أو static بهذا الاسم (أو nullptr)
    auto findAnyFn = [this](const std::string& cls, const std::string& name) -> std::shared_ptr<FunctionStmt> {
        if (auto m = findMethod(cls, name)) return m;
        if (auto m = findStaticMethod(cls, name)) return m;
        if (auto m = findAccessor(cls, name, false)) return m;
        return findAccessor(cls, name, true);
    };
    natives["oop.arity"] = [clsOf, findAnyFn](Args& a, int line) -> Value {
        need("oop.arity", a, 2, 2, line);
        auto m = findAnyFn(clsOf(a[0], "oop.arity", line), str(a[1], "oop.arity", line));
        return m ? N(static_cast<double>(m->params.size())) : Value::nil();
    };
    natives["oop.params"] = [clsOf, findAnyFn](Args& a, int line) -> Value {
        need("oop.params", a, 2, 2, line);
        auto m = findAnyFn(clsOf(a[0], "oop.params", line), str(a[1], "oop.params", line));
        return m ? strArray(m->params) : Value::nil();
    };
    natives["oop.signature"] = [clsOf, findAnyFn](Args& a, int line) -> Value {
        need("oop.signature", a, 2, 2, line);
        std::string n = str(a[1], "oop.signature", line);
        auto m = findAnyFn(clsOf(a[0], "oop.signature", line), n);
        if (!m) return Value::nil();
        std::string sig = n + "(";
        for (size_t i = 0; i < m->params.size(); ++i) {
            if (i) sig += ", ";
            sig += m->params[i];
            if (i < m->paramTypes.size() && !m->paramTypes[i].empty()) sig += ": " + m->paramTypes[i];
        }
        sig += ")";
        if (!m->returnType.empty()) sig += ": " + m->returnType;
        return S(sig);
    };
    natives["oop.access"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.access", a, 2, 2, line);
        std::string c = clsOf(a[0], "oop.access", line), n = str(a[1], "oop.access", line);
        auto fmt = [](const std::string& acc) { return S(acc.empty() ? "public" : acc); };
        if (auto* fm = findFieldMeta(c, n)) return fmt(fm->access);
        if (auto m = findMethod(c, n)) return fmt(m->access);
        if (auto m = findAccessor(c, n, false)) return fmt(m->access);
        if (auto m = findAccessor(c, n, true)) return fmt(m->access);
        if (auto m = findStaticMethod(c, n)) return fmt(m->access);
        if (ClassDef* o = findStaticFieldOwner(c, n)) return fmt(o->staticMeta[n].access);
        return Value::nil();
    };
    natives["oop.describe"] = [this, clsOf, methodNames, fieldNames, staticMethodNames, staticFieldNames, propertyNames, abstractNames, kindName](Args& a, int line) -> Value {
        need("oop.describe", a, 1, 1, line);
        std::string c = clsOf(a[0], "oop.describe", line);
        const ClassDef& d = classes.at(c);
        std::vector<std::string> ifs, ts;
        collectInterfaces(c, ifs);
        collectTraits(c, ts);
        return M({{"name", S(c)},
                  {"kind", S(kindName(c))},
                  {"parent", d.superclass.empty() ? Value::nil() : S(d.superclass)},
                  {"abstract", B(d.isAbstract)},
                  {"final", B(d.isFinal)},
                  {"interfaces", strArray(sortedUnique(ifs))},
                  {"traits", strArray(sortedUnique(ts))},
                  {"fields", strArray(fieldNames(c))},
                  {"methods", strArray(methodNames(c, true))},
                  {"abstractMethods", strArray(abstractNames(c))},
                  {"properties", strArray(propertyNames(c))},
                  {"staticFields", strArray(staticFieldNames(c))},
                  {"staticMethods", strArray(staticMethodNames(c))}});
    };

    // ======================================================== 3) عمليات ديناميكية
    // get/set/call/new تُنفَّذ كأنها من خارج أي صنف (تحترم private/protected).
    natives["oop.get"] = [this, hasMember](Args& a, int line) -> Value {
        need("oop.get", a, 2, 3, line);
        std::string n = str(a[1], "oop.get", line);
        if (!hasMember(a[0], n)) {
            if (a.size() == 3) return a[2];
            if (a[0].type == Value::Type::MAP) return Value::nil();
        }
        return readMember(a[0], n, nullptr, line);
    };
    natives["oop.set"] = [this, instOf](Args& a, int line) -> Value {
        need("oop.set", a, 3, 3, line);
        if (a[0].type == Value::Type::MAP) { mapSet(*a[0].map, a[1], a[2]); return a[2]; }
        instOf(a[0], "oop.set", line);
        return writeMember(a[0], str(a[1], "oop.set", line), a[2], nullptr, line);
    };
    // peek/poke: مفاتيح استبطان تتجاوز الصلاحيات و final (للتشخيص/التسلسل/الاختبار) — لا تتجاوز freeze.
    natives["oop.peek"] = [instOf](Args& a, int line) -> Value {
        need("oop.peek", a, 2, 3, line);
        InstanceData& inst = instOf(a[0], "oop.peek", line);
        auto it = inst.fields.find(str(a[1], "oop.peek", line));
        if (it != inst.fields.end()) return it->second;
        return a.size() == 3 ? a[2] : Value::nil();
    };
    natives["oop.poke"] = [this, instOf](Args& a, int line) -> Value {
        need("oop.poke", a, 3, 3, line);
        InstanceData& inst = instOf(a[0], "oop.poke", line);
        std::string n = str(a[1], "oop.poke", line);
        if (inst.frozen)
            throw errWithReason(diag::Code::E0035_RuntimeError, line, "cannot modify field `" + n + "` of a frozen `" + inst.className + "` object",
                                "this object was frozen with `oop.freeze(...)`");
        Value oldVal = Value::nil();
        { auto oit = inst.fields.find(n); if (oit != inst.fields.end()) oldVal = oit->second; }
        if (!inst.fields.count(n)) inst.fieldOrder.push_back(n);
        inst.fields[n] = copyForBinding(a[2]);
        if (observersExist_) notifyObservers(a[0], n, oldVal, inst.fields[n], line);
        return inst.fields[n];
    };
    natives["oop.call"] = [this, argsFromArray](Args& a, int line) -> Value {
        need("oop.call", a, 2, 3, line);
        std::vector<Value> args = a.size() > 2 ? argsFromArray(a[2], "oop.call", line) : std::vector<Value>{};
        return callMethodOn(a[0], str(a[1], "oop.call", line), args, nullptr, line);
    };
    natives["oop.callStatic"] = [this, clsOf, argsFromArray](Args& a, int line) -> Value {
        need("oop.callStatic", a, 2, 3, line);
        std::vector<Value> args = a.size() > 2 ? argsFromArray(a[2], "oop.callStatic", line) : std::vector<Value>{};
        return callStatic(clsOf(a[0], "oop.callStatic", line), str(a[1], "oop.callStatic", line), args, nullptr, line);
    };
    natives["oop.new"] = [this](Args& a, int line) -> Value {
        if (a.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'oop.new' expects a class name followed by constructor arguments");
        std::string c = str(a[0], "oop.new", line);
        if (!classes.count(c)) throw unknownFunctionErr(c, line);
        std::vector<Value> args(a.begin() + 1, a.end());
        return instantiateClass(c, args, line);
    };
    natives["oop.newWith"] = [this, argsFromArray](Args& a, int line) -> Value {
        need("oop.newWith", a, 1, 2, line);
        std::string c = str(a[0], "oop.newWith", line);
        if (!classes.count(c)) throw unknownFunctionErr(c, line);
        std::vector<Value> args = a.size() > 1 ? argsFromArray(a[1], "oop.newWith", line) : std::vector<Value>{};
        return instantiateClass(c, args, line);
    };
    natives["oop.staticGet"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.staticGet", a, 2, 2, line);
        return staticGet(clsOf(a[0], "oop.staticGet", line), str(a[1], "oop.staticGet", line), nullptr, line);
    };
    natives["oop.staticSet"] = [this, clsOf](Args& a, int line) -> Value {
        need("oop.staticSet", a, 3, 3, line);
        return staticSet(clsOf(a[0], "oop.staticSet", line), str(a[1], "oop.staticSet", line), a[2], nullptr, line);
    };
    // oop.extend(obj, {name: valueOrFunction, ...}) — يضيف حقولاً/دوالاً (callbacks) ديناميكياً لكائن بعينه.
    natives["oop.extend"] = [this, instOf](Args& a, int line) -> Value {
        need("oop.extend", a, 2, 2, line);
        InstanceData& inst = instOf(a[0], "oop.extend", line);
        if (inst.frozen) throw errWithReason(diag::Code::E0035_RuntimeError, line, "cannot extend a frozen `" + inst.className + "` object", "frozen objects are read-only");
        for (auto& kv : mp(a[1], "oop.extend", line)) {
            std::string n = kv.first.toDisplayString();
            if (!inst.fields.count(n)) inst.fieldOrder.push_back(n);
            inst.fields[n] = copyForBinding(kv.second);
        }
        return a[0];
    };
    natives["oop.delete"] = [instOf](Args& a, int line) -> Value {
        need("oop.delete", a, 2, 2, line);
        InstanceData& inst = instOf(a[0], "oop.delete", line);
        std::string n = str(a[1], "oop.delete", line);
        if (inst.frozen) throw diagErr(diag::Code::E0035_RuntimeError, line, "cannot delete field `" + n + "` of a frozen object");
        bool had = inst.fields.erase(n) > 0;
        inst.fieldOrder.erase(std::remove(inst.fieldOrder.begin(), inst.fieldOrder.end(), n), inst.fieldOrder.end());
        return B(had);
    };

    // ======================================================== 4) أدوات الكائنات
    natives["oop.toMap"] = [instOf](Args& a, int line) -> Value {
        need("oop.toMap", a, 1, 1, line);
        InstanceData& inst = instOf(a[0], "oop.toMap", line);
        MapData out;
        for (auto& n : inst.fieldOrder) {
            auto it = inst.fields.find(n);
            if (it != inst.fields.end()) out.push_back({Value::string(n), it->second});
        }
        return newMap(std::move(out));
    };
    natives["oop.entries"] = [instOf](Args& a, int line) -> Value {
        need("oop.entries", a, 1, 1, line);
        InstanceData& inst = instOf(a[0], "oop.entries", line);
        ArrayData out;
        for (auto& n : inst.fieldOrder) {
            auto it = inst.fields.find(n);
            if (it != inst.fields.end()) out.push_back(newArray({Value::string(n), it->second}));
        }
        return newArray(std::move(out));
    };
    // oop.fromMap(className, map) — ينشئ كائناً بحقوله الافتراضية (بلا استدعاء init) ثم يطغى عليها بقيم القاموس.
    natives["oop.fromMap"] = [this](Args& a, int line) -> Value {
        need("oop.fromMap", a, 2, 2, line);
        std::string c = str(a[0], "oop.fromMap", line);
        if (!classes.count(c)) throw unknownFunctionErr(c, line);
        const MapData& src = mp(a[1], "oop.fromMap", line);
        std::vector<Value> noArgs;
        skipInit_ = true;
        Value obj = instantiateClass(c, noArgs, line);
        InstanceData& inst = *obj.instance;
        for (auto& kv : src) {
            std::string n = kv.first.toDisplayString();
            if (!inst.fields.count(n)) inst.fieldOrder.push_back(n);
            inst.fields[n] = copyForBinding(kv.second);
        }
        return obj;
    };
    natives["oop.pick"] = [this, instOf, hasMember](Args& a, int line) -> Value {
        need("oop.pick", a, 2, 2, line);
        InstanceData& inst = instOf(a[0], "oop.pick", line);
        MapData out;
        for (auto& n : arr(a[1], "oop.pick", line)) {
            std::string k = n.toDisplayString();
            auto it = inst.fields.find(k);
            if (it != inst.fields.end()) out.push_back({Value::string(k), it->second});
            else if (hasMember(a[0], k)) out.push_back({Value::string(k), readMember(a[0], k, nullptr, line)});
        }
        return newMap(std::move(out));
    };
    natives["oop.omit"] = [instOf](Args& a, int line) -> Value {
        need("oop.omit", a, 2, 2, line);
        InstanceData& inst = instOf(a[0], "oop.omit", line);
        std::set<std::string> skip;
        for (auto& n : arr(a[1], "oop.omit", line)) skip.insert(n.toDisplayString());
        MapData out;
        for (auto& n : inst.fieldOrder) {
            auto it = inst.fields.find(n);
            if (it != inst.fields.end() && !skip.count(n)) out.push_back({Value::string(n), it->second});
        }
        return newMap(std::move(out));
    };
    // oop.assign(target, source) — ينسخ حقول source (كائن أو قاموس) إلى target (يتجاوز الصلاحيات، يحترم freeze).
    natives["oop.assign"] = [this, instOf](Args& a, int line) -> Value {
        need("oop.assign", a, 2, 2, line);
        InstanceData& dst = instOf(a[0], "oop.assign", line);
        if (dst.frozen) throw errWithReason(diag::Code::E0035_RuntimeError, line, "cannot assign into a frozen `" + dst.className + "` object", "frozen objects are read-only");
        auto put = [&](const std::string& n, const Value& v) {
            if (!dst.fields.count(n)) dst.fieldOrder.push_back(n);
            dst.fields[n] = copyForBinding(v);
        };
        if (a[1].type == Value::Type::INSTANCE && a[1].instance) {
            for (auto& n : a[1].instance->fieldOrder) {
                auto it = a[1].instance->fields.find(n);
                if (it != a[1].instance->fields.end()) put(n, it->second);
            }
        } else {
            for (auto& kv : mp(a[1], "oop.assign", line)) put(kv.first.toDisplayString(), kv.second);
        }
        return a[0];
    };
    natives["oop.clone"] = [](Args& a, int line) -> Value {
        need("oop.clone", a, 1, 1, line);
        const Value& v = a[0];
        if (v.type == Value::Type::INSTANCE && v.instance) {
            auto c = std::make_shared<InstanceData>();
            c->className = v.instance->className;
            c->isStruct = v.instance->isStruct;
            c->fields = v.instance->fields;
            c->fieldOrder = v.instance->fieldOrder;
            return Value::makeInstance(c);
        }
        if (v.type == Value::Type::ARRAY && v.array) return newArray(*v.array);
        if (v.type == Value::Type::MAP && v.map) return newMap(*v.map);
        return v;
    };
    // deepClone: الكائنات/المصفوفات/القواميس بشكل متكرّر؛ الدوال تبقى كما هي.
    static std::function<Value(const Value&, int)> deepCloneRec;
    deepCloneRec = [](const Value& v, int depth) -> Value {
        if (depth > 200) return v;
        if (v.type == Value::Type::INSTANCE && v.instance) {
            auto c = std::make_shared<InstanceData>();
            c->className = v.instance->className;
            c->isStruct = v.instance->isStruct;
            c->fieldOrder = v.instance->fieldOrder;
            for (auto& kv : v.instance->fields) c->fields[kv.first] = deepCloneRec(kv.second, depth + 1);
            return Value::makeInstance(c);
        }
        if (v.type == Value::Type::ARRAY && v.array) {
            ArrayData out;
            out.reserve(v.array->size());
            for (auto& x : *v.array) out.push_back(deepCloneRec(x, depth + 1));
            return newArray(std::move(out));
        }
        if (v.type == Value::Type::MAP && v.map) {
            MapData out;
            out.reserve(v.map->size());
            for (auto& kv : *v.map) out.push_back({kv.first, deepCloneRec(kv.second, depth + 1)});
            return newMap(std::move(out));
        }
        return v;
    };
    natives["oop.deepClone"] = [](Args& a, int line) -> Value { need("oop.deepClone", a, 1, 1, line); return deepCloneRec(a[0], 0); };

    // equals: مساواة تركيبية عميقة (حتى للكائنات من class) — بخلاف == التي تقارن هوية class.
    static std::function<bool(const Value&, const Value&, int)> deepEqRec;
    deepEqRec = [](const Value& x, const Value& y, int depth) -> bool {
        if (depth > 200) return false;
        if (x.type != y.type) return false;
        if (x.type == Value::Type::INSTANCE) {
            if (x.instance == y.instance) return true;
            if (!x.instance || !y.instance) return false;
            if (x.instance->className != y.instance->className || x.instance->fields.size() != y.instance->fields.size()) return false;
            for (auto& kv : x.instance->fields) {
                auto it = y.instance->fields.find(kv.first);
                if (it == y.instance->fields.end() || !deepEqRec(kv.second, it->second, depth + 1)) return false;
            }
            return true;
        }
        if (x.type == Value::Type::ARRAY) {
            if (x.array == y.array) return true;
            if (x.array->size() != y.array->size()) return false;
            for (size_t i = 0; i < x.array->size(); ++i) if (!deepEqRec((*x.array)[i], (*y.array)[i], depth + 1)) return false;
            return true;
        }
        if (x.type == Value::Type::MAP) {
            if (x.map == y.map) return true;
            if (x.map->size() != y.map->size()) return false;
            for (auto& kv : *x.map) {
                bool found = false;
                for (auto& kv2 : *y.map)
                    if (valuesEqual(kv.first, kv2.first)) { found = deepEqRec(kv.second, kv2.second, depth + 1); break; }
                if (!found) return false;
            }
            return true;
        }
        return valuesEqual(x, y);
    };
    natives["oop.equals"] = [](Args& a, int line) -> Value { need("oop.equals", a, 2, 2, line); return B(deepEqRec(a[0], a[1], 0)); };
    natives["oop.same"] = [](Args& a, int line) -> Value {
        need("oop.same", a, 2, 2, line);
        const Value &x = a[0], &y = a[1];
        if (x.type != y.type) return B(false);
        if (x.type == Value::Type::INSTANCE) return B(x.instance == y.instance);
        if (x.type == Value::Type::ARRAY) return B(x.array == y.array);
        if (x.type == Value::Type::MAP) return B(x.map == y.map);
        return B(valuesEqual(x, y));
    };
    // hash: FNV-1a (64-bit) على تمثيل قانوني للمحتوى (حقول الكائن مرتّبة أبجدياً) — ثابت بين التشغيلات.
    static std::function<void(const Value&, std::string&, int)> canonRec;
    canonRec = [](const Value& v, std::string& out, int depth) {
        if (depth > 100) { out += "~"; return; }
        switch (v.type) {
            case Value::Type::NIL: out += "n;"; break;
            case Value::Type::BOOL: out += v.boolean ? "t;" : "f;"; break;
            case Value::Type::NUMBER: out += "d" + v.toDisplayString() + ";"; break;
            case Value::Type::STRING: out += "s" + std::to_string(v.str.size()) + ":" + v.str + ";"; break;
            case Value::Type::FUNCTION: out += "F;"; break;
            case Value::Type::ARRAY:
                out += "[";
                for (auto& x : *v.array) canonRec(x, out, depth + 1);
                out += "]";
                break;
            case Value::Type::MAP: {
                out += "{";
                std::vector<std::pair<std::string, const Value*>> items;
                for (auto& kv : *v.map) items.push_back({kv.first.toDisplayString(), &kv.second});
                std::sort(items.begin(), items.end(), [](auto& p, auto& q) { return p.first < q.first; });
                for (auto& it : items) { out += it.first + "="; canonRec(*it.second, out, depth + 1); }
                out += "}";
                break;
            }
            case Value::Type::INSTANCE: {
                out += "<" + v.instance->className + ":";
                std::vector<std::string> keys;
                for (auto& kv : v.instance->fields) keys.push_back(kv.first);
                std::sort(keys.begin(), keys.end());
                for (auto& k : keys) { out += k + "="; canonRec(v.instance->fields.at(k), out, depth + 1); }
                out += ">";
                break;
            }
        }
    };
    natives["oop.hash"] = [](Args& a, int line) -> Value {
        need("oop.hash", a, 1, 1, line);
        std::string canon;
        canonRec(a[0], canon, 0);
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : canon) { h ^= c; h *= 1099511628211ULL; }
        return N(static_cast<double>(h & ((1ULL << 53) - 1)));
    };
    natives["oop.toString"] = [](Args& a, int line) -> Value { need("oop.toString", a, 1, 1, line); return S(a[0].toDisplayString()); };
    // inspect: عرض متعدد الأسطر مع إزاحة (يُظهر كل الحقول بما فيها private).
    static std::function<std::string(const Value&, int, int)> inspectRec;
    inspectRec = [](const Value& v, int indent, int depth) -> std::string {
        std::string pad(static_cast<size_t>(indent), ' '), in2(static_cast<size_t>(indent + 2), ' ');
        if (depth > 20) return "...";
        if (v.type == Value::Type::INSTANCE && v.instance) {
            if (v.instance->fieldOrder.empty()) return v.instance->className + " {}";
            std::string s = v.instance->className + " {\n";
            for (auto& n : v.instance->fieldOrder) {
                auto it = v.instance->fields.find(n);
                if (it == v.instance->fields.end()) continue;
                s += in2 + n + ": " + inspectRec(it->second, indent + 2, depth + 1) + ",\n";
            }
            return s + pad + "}";
        }
        if (v.type == Value::Type::ARRAY && v.array) {
            if (v.array->empty()) return "[]";
            std::string s = "[\n";
            for (auto& x : *v.array) s += in2 + inspectRec(x, indent + 2, depth + 1) + ",\n";
            return s + pad + "]";
        }
        if (v.type == Value::Type::MAP && v.map) {
            if (v.map->empty()) return "{}";
            std::string s = "{\n";
            for (auto& kv : *v.map) s += in2 + kv.first.toDisplayString() + ": " + inspectRec(kv.second, indent + 2, depth + 1) + ",\n";
            return s + pad + "}";
        }
        if (v.type == Value::Type::STRING) return "\"" + v.str + "\"";
        return v.toDisplayString();
    };
    natives["oop.inspect"] = [](Args& a, int line) -> Value { need("oop.inspect", a, 1, 1, line); return S(inspectRec(a[0], 0, 0)); };
    // freeze(obj, deep=false)
    static std::function<void(const Value&, int)> freezeRec;
    freezeRec = [](const Value& v, int depth) {
        if (depth > 100) return;
        if (v.type == Value::Type::INSTANCE && v.instance) {
            if (v.instance->frozen) return;
            v.instance->frozen = true;
            for (auto& kv : v.instance->fields) freezeRec(kv.second, depth + 1);
        }
    };
    natives["oop.freeze"] = [instOf](Args& a, int line) -> Value {
        need("oop.freeze", a, 1, 2, line);
        InstanceData& inst = instOf(a[0], "oop.freeze", line);
        if (a.size() > 1 && a[1].isTruthy()) freezeRec(a[0], 0);
        else inst.frozen = true;
        return a[0];
    };
    natives["oop.isFrozen"] = [instOf](Args& a, int line) -> Value { need("oop.isFrozen", a, 1, 1, line); return B(instOf(a[0], "oop.isFrozen", line).frozen); };

    // ======================================================== 5) أنماط تصميم
    // oop.singleton("Config", args...) — أول نداء ينشئ الكائن (ويمرّر args إلى init)، وما بعده يعيد النسخة نفسها.
    natives["oop.singleton"] = [this](Args& a, int line) -> Value {
        if (a.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'oop.singleton' expects a class name followed by constructor arguments");
        std::string c = str(a[0], "oop.singleton", line);
        auto it = singletons_.find(c);
        if (it != singletons_.end()) return it->second;
        if (!classes.count(c)) throw unknownFunctionErr(c, line);
        std::vector<Value> args(a.begin() + 1, a.end());
        Value v = instantiateClass(c, args, line);
        singletons_[c] = v;
        return v;
    };
    natives["oop.hasSingleton"] = [this](Args& a, int line) -> Value { need("oop.hasSingleton", a, 1, 1, line); return B(singletons_.count(str(a[0], "oop.hasSingleton", line)) > 0); };
    natives["oop.resetSingleton"] = [this](Args& a, int line) -> Value { need("oop.resetSingleton", a, 1, 1, line); return B(singletons_.erase(str(a[0], "oop.resetSingleton", line)) > 0); };

    // ======================================================== 6) مقارنة وفرز وأدوات مصفوفات كائنات
    // compare(a, b) -> -1/0/1 : أرقام/نصوص/منطقي، أو كائنات عبر __lt__/__gt__/__cmp__.
    auto cmpValues = [this](const Value& x, const Value& y, int line) -> int {
        if (x.type == Value::Type::NUMBER && y.type == Value::Type::NUMBER) return x.number < y.number ? -1 : (x.number > y.number ? 1 : 0);
        if (x.type == Value::Type::STRING && y.type == Value::Type::STRING) return x.str < y.str ? -1 : (x.str > y.str ? 1 : 0);
        if (x.type == Value::Type::BOOL && y.type == Value::Type::BOOL) return static_cast<int>(x.boolean) - static_cast<int>(y.boolean);
        if (x.type == Value::Type::NIL && y.type == Value::Type::NIL) return 0;
        if (x.type == Value::Type::INSTANCE || y.type == Value::Type::INSTANCE) {
            auto lt = tryCompareOverload(TokenType::LESS, x, y, line);
            auto gt = tryCompareOverload(TokenType::GREATER, x, y, line);
            if (lt || gt) return (lt && lt->isTruthy()) ? -1 : ((gt && gt->isTruthy()) ? 1 : 0);
        }
        throw diagErr(diag::Code::E0004_InvalidType, line,
                      "cannot compare `" + x.typeName() + "` with `" + y.typeName() + "` (define `__lt__` or `__cmp__` on the class)");
    };
    natives["oop.compare"] = [cmpValues](Args& a, int line) -> Value { need("oop.compare", a, 2, 2, line); return N(cmpValues(a[0], a[1], line)); };
    natives["oop.sort"] = [cmpValues](Args& a, int line) -> Value {
        need("oop.sort", a, 1, 2, line);
        ArrayData out = arr(a[0], "oop.sort", line);
        bool desc = a.size() > 1 && a[1].isTruthy();
        std::stable_sort(out.begin(), out.end(), [&](const Value& x, const Value& y) {
            int c = cmpValues(x, y, line);
            return desc ? c > 0 : c < 0;
        });
        return newArray(std::move(out));
    };
    natives["oop.sortBy"] = [cmpValues, getMember](Args& a, int line) -> Value {
        need("oop.sortBy", a, 2, 3, line);
        ArrayData out = arr(a[0], "oop.sortBy", line);
        std::string f = str(a[1], "oop.sortBy", line);
        bool desc = a.size() > 2 && a[2].isTruthy();
        std::stable_sort(out.begin(), out.end(), [&](const Value& x, const Value& y) {
            int c = cmpValues(getMember(x, f, line), getMember(y, f, line), line);
            return desc ? c > 0 : c < 0;
        });
        return newArray(std::move(out));
    };
    natives["oop.min"] = [cmpValues](Args& a, int line) -> Value {
        need("oop.min", a, 1, 1, line);
        const ArrayData& v = arr(a[0], "oop.min", line);
        if (v.empty()) return Value::nil();
        Value best = v[0];
        for (size_t i = 1; i < v.size(); ++i) if (cmpValues(v[i], best, line) < 0) best = v[i];
        return best;
    };
    natives["oop.max"] = [cmpValues](Args& a, int line) -> Value {
        need("oop.max", a, 1, 1, line);
        const ArrayData& v = arr(a[0], "oop.max", line);
        if (v.empty()) return Value::nil();
        Value best = v[0];
        for (size_t i = 1; i < v.size(); ++i) if (cmpValues(v[i], best, line) > 0) best = v[i];
        return best;
    };
    natives["oop.pluck"] = [getMember](Args& a, int line) -> Value {
        need("oop.pluck", a, 2, 2, line);
        std::string f = str(a[1], "oop.pluck", line);
        ArrayData out;
        for (auto& x : arr(a[0], "oop.pluck", line)) out.push_back(getMember(x, f, line));
        return newArray(std::move(out));
    };
    natives["oop.groupBy"] = [getMember](Args& a, int line) -> Value {
        need("oop.groupBy", a, 2, 2, line);
        std::string f = str(a[1], "oop.groupBy", line);
        MapData out;
        for (auto& x : arr(a[0], "oop.groupBy", line)) {
            Value key = getMember(x, f, line);
            Value* bucket = nullptr;
            for (auto& kv : out) if (valuesEqual(kv.first, key)) { bucket = &kv.second; break; }
            if (!bucket) { out.push_back({key, newArray()}); bucket = &out.back().second; }
            bucket->array->push_back(x);
        }
        return newMap(std::move(out));
    };
    natives["oop.findBy"] = [getMember](Args& a, int line) -> Value {
        need("oop.findBy", a, 3, 3, line);
        std::string f = str(a[1], "oop.findBy", line);
        for (auto& x : arr(a[0], "oop.findBy", line)) if (valuesEqual(getMember(x, f, line), a[2])) return x;
        return Value::nil();
    };
    natives["oop.filterBy"] = [getMember](Args& a, int line) -> Value {
        need("oop.filterBy", a, 3, 3, line);
        std::string f = str(a[1], "oop.filterBy", line);
        ArrayData out;
        for (auto& x : arr(a[0], "oop.filterBy", line)) if (valuesEqual(getMember(x, f, line), a[2])) out.push_back(x);
        return newArray(std::move(out));
    };

    // ======================================================== 7) تغليف len/contains للكائنات
    // len(obj) -> obj.__len__() ، contains(obj, x) -> obj.__contains__(x)؛ أي نوع آخر يمرّ للدالة الأصلية كما كانت.
    if (natives.count("len")) {
        auto oldLen = natives["len"];
        natives["len"] = [this, oldLen](Args& a, int line) -> Value {
            if (a.size() == 1 && a[0].type == Value::Type::INSTANCE && hasMagic(a[0], "__len__")) {
                std::vector<Value> none;
                return *callMagic(a[0], "__len__", none, line);
            }
            return oldLen(a, line);
        };
    }
    if (natives.count("contains")) {
        auto oldContains = natives["contains"];
        natives["contains"] = [this, oldContains](Args& a, int line) -> Value {
            if (a.size() == 2 && a[0].type == Value::Type::INSTANCE && hasMagic(a[0], "__contains__")) {
                std::vector<Value> one{a[1]};
                return Value::boolean_(callMagic(a[0], "__contains__", one, line)->isTruthy());
            }
            return oldContains(a, line);
        };
    }

    registerNativesOopBind(); // دوال الربط (rin_oop_bind.cpp)
}

} // namespace rin

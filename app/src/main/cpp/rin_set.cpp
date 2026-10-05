// ============================================================================
//  rin_set.cpp — Set: نوع المجموعة الأساسي في Rin (Value::Type::SET)
// ----------------------------------------------------------------------------
//  كان "set" موجوداً فقط كمكتبة اختيارية (lib/boat.og.rin: قاموس بحقل kind). الآن هو نوع أصلي في
//  المفسّر نفسه، بجانب array و map:
//
//    • عناصر فريدة بحسب valuesEqual (مقارنة تركيبية)، بترتيب الإدراج، ومرجعية (كالمصفوفة والقاموس).
//    • بحث/إضافة/إزالة سريعة (فهرس تجزئة للقيم البدائية) — انظر SetData في rin_interpreter.h.
//    • إنشاء:  Set()  Set([..])  Set(a, b, c)  Set.of(..)  Set.from(iterable|string)
//    • معاملات: a + b اتحاد · a * b تقاطع · a - b فرق · a <= b جزئية · a < b جزئية فعلية · == !=
//    • تكرار:  for (let x in s) { ... }       • أنواع:  let s: Set = Set([1, 2]);
//    • دوال عامة تفهمه: len contains has remove empty join sum mean median ... jsonEncode type
//    • طرق نقطية وصيغة ثابتة متطابقة:  s.add(x)  ==  Set.add(s, x)  (القائمة: kSetMethods أدناه)
//
//  لا يضيف أي اسم عام جديد سوى `Set` (حرف كبير، كاسم النوع) — بقية الدوال تحت `Set.` فلا تتعارض مع
//  أي دالة موجودة أو معرَّفة من المستخدم (مثل set()/setAdd() في lib/boat.og.rin، التي تبقى كما هي).
//
//  يُضمَّن (#include) في نهاية rin_interpreter.cpp مثل rin_table.cpp، فيدخل كل أهداف البناء
//  (APK / CLI / WASM / CI) تلقائياً بلا أي تعديل في ملفات البناء. الشرح الكامل: docs/set.md
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <cstring>
#include <functional>

namespace rin {

// ============================ SetData ============================

bool SetData::primKey(const Value& v, std::string& key) {
    switch (v.type) {
        case Value::Type::NIL:  key = "z"; return true;
        case Value::Type::BOOL: key = v.boolean ? "b1" : "b0"; return true;
        case Value::Type::NUMBER: {
            double d = v.number;
            if (d != d) return false;   // NaN لا يساوي نفسه في valuesEqual: يُعامَل كعنصر مركّب (لا يُدمج أبداً)
            if (d == 0.0) d = 0.0;      // -0 == +0
            char buf[sizeof(double)];
            std::memcpy(buf, &d, sizeof(d));
            key = "n";
            key.append(buf, sizeof(d));
            return true;
        }
        case Value::Type::STRING: key = "s" + v.str; return true;
        default: return false;
    }
}

void SetData::reindex() const {
    pidx.clear();
    comp.clear();
    pidx.reserve(items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        std::string k;
        if (primKey(items[i], k)) pidx.emplace(std::move(k), i);
        else comp.push_back(i);
    }
    indexed = true;
}

bool SetData::contains(const Value& x) const {
    if (!indexed) reindex();
    std::string k;
    if (primKey(x, k)) return pidx.find(k) != pidx.end();
    for (size_t i : comp) if (valuesEqual(items[i], x)) return true;
    return false;
}

bool SetData::add(const Value& x) {
    if (!indexed) reindex();
    std::string k;
    if (primKey(x, k)) {
        if (!pidx.emplace(std::move(k), items.size()).second) return false;
        items.push_back(x);
        return true;
    }
    for (size_t i : comp) if (valuesEqual(items[i], x)) return false;
    comp.push_back(items.size());
    items.push_back(x);
    return true;
}

bool SetData::remove(const Value& x) {
    if (!indexed) reindex();
    size_t pos = static_cast<size_t>(-1);
    std::string k;
    bool prim = primKey(x, k);
    if (prim) {
        auto it = pidx.find(k);
        if (it == pidx.end()) return false;
        pos = it->second;
    } else {
        for (size_t i : comp) if (valuesEqual(items[i], x)) { pos = i; break; }
        if (pos == static_cast<size_t>(-1)) return false;
    }
    if (pos + 1 == items.size()) { // آخر عنصر: المواضع الأخرى لا تتغيّر فيبقى الفهرس صالحاً
        if (prim) pidx.erase(k); else comp.pop_back();
        items.pop_back();
        return true;
    }
    items.erase(items.begin() + static_cast<std::ptrdiff_t>(pos));
    indexed = false; // الإزاحة غيّرت المواضع: يُعاد بناء الفهرس عند أول بحث لاحق
    return true;
}

// ============================ setops ============================
namespace setops {

bool subset(const SetData& a, const SetData& b) {
    if (a.size() > b.size()) return false;
    for (auto& e : a.items) if (!b.contains(e)) return false;
    return true;
}

bool equal(const SetData& a, const SetData& b) {
    return a.size() == b.size() && subset(a, b);
}

SetPtr unite(const SetData& a, const SetData& b) {
    auto r = std::make_shared<SetData>(a); // نسخ العناصر والفهرس معاً
    for (auto& e : b.items) r->add(e);
    return r;
}

SetPtr intersect(const SetData& a, const SetData& b) {
    auto r = std::make_shared<SetData>();
    for (auto& e : a.items) if (b.contains(e)) r->add(e);
    return r;
}

SetPtr subtract(const SetData& a, const SetData& b) {
    auto r = std::make_shared<SetData>();
    for (auto& e : a.items) if (!b.contains(e)) r->add(e);
    return r;
}

std::string display(const SetData& s) {
    std::string out = "Set{";
    for (size_t i = 0; i < s.items.size(); ++i) {
        if (i) out += ", ";
        out += reprValue(s.items[i]);
    }
    return out + "}";
}

Value binary(TokenType op, const Value& l, const Value& r, int line) {
    const char* sym = "?";
    const char* what = "";
    switch (op) {
        case TokenType::PLUS:          sym = "+";  what = "union"; break;
        case TokenType::STAR:          sym = "*";  what = "intersection"; break;
        case TokenType::MINUS:         sym = "-";  what = "difference"; break;
        case TokenType::LESS_EQUAL:    sym = "<="; what = "subset test"; break;
        case TokenType::LESS:          sym = "<";  what = "proper-subset test"; break;
        case TokenType::GREATER_EQUAL: sym = ">="; what = "superset test"; break;
        case TokenType::GREATER:       sym = ">";  what = "proper-superset test"; break;
        default: break;
    }
    if (l.type != Value::Type::SET || r.type != Value::Type::SET || !l.set || !r.set) {
        auto d = diagErr(diag::Code::E0004_InvalidType, line,
                         std::string("`") + sym + "` on a set (" + what + ") needs a set on both sides, found `" +
                             l.typeName() + "` and `" + r.typeName() + "`");
        d.diagnostic->withHint("wrap the other side with `Set(...)`, or use `s.add(x)` / `s.addAll(items)` / `s.has(x)`");
        throw d;
    }
    const SetData& a = *l.set;
    const SetData& b = *r.set;
    switch (op) {
        case TokenType::PLUS:          return Value::makeSet(unite(a, b));
        case TokenType::STAR:          return Value::makeSet(intersect(a, b));
        case TokenType::MINUS:         return Value::makeSet(subtract(a, b));
        case TokenType::LESS_EQUAL:    return Value::boolean_(subset(a, b));
        case TokenType::LESS:          return Value::boolean_(a.size() < b.size() && subset(a, b));
        case TokenType::GREATER_EQUAL: return Value::boolean_(subset(b, a));
        case TokenType::GREATER:       return Value::boolean_(b.size() < a.size() && subset(b, a));
        default: break;
    }
    throw diagErr(diag::Code::E0004_InvalidType, line, "unsupported operator on sets");
}

} // namespace setops

// ============================ helpers (داخلية) ============================
namespace setimpl {

using Args = std::vector<Value>;

// كل أسماء عمليات Set (طريقة s.name(...) وصيغة ثابتة Set.name(s, ...)). mut=true: تعدّل المجموعة نفسها
// فيجب أن يكون الوسيط الأول SET فعلياً في الصيغة الثابتة (وإلا ضاع التعديل على نسخة مؤقتة).
struct MethodInfo { const char* name; bool mut; };
static const MethodInfo kSetMethods[] = {
    {"size", false}, {"len", false}, {"length", false}, {"isEmpty", false}, {"empty", false},
    {"has", false}, {"contains", false}, {"hasAll", false}, {"hasAny", false},
    {"add", true}, {"addAll", true}, {"remove", true}, {"removeAll", true}, {"retain", true},
    {"clear", true}, {"pop", true},
    {"union", false}, {"intersect", false}, {"intersection", false}, {"diff", false}, {"difference", false},
    {"symDiff", false}, {"isSubset", false}, {"isSuperset", false}, {"isDisjoint", false}, {"equals", false},
    {"toArray", false}, {"sorted", false}, {"join", false}, {"copy", false}, {"clone", false},
    {"first", false}, {"last", false}, {"min", false}, {"max", false}, {"sum", false},
    {"map", false}, {"filter", false}, {"each", false}, {"forEach", false}, {"some", false}, {"any", false},
    {"every", false}, {"all", false}, {"reduce", false},
};

static RinError typeErr(int line, const std::string& msg) {
    return diagErr(diag::Code::E0004_InvalidType, line, msg);
}

static void argc(const std::string& fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi) {
        std::string want = lo == hi ? std::to_string(lo)
                         : (hi == static_cast<size_t>(-1) ? std::to_string(lo) + " or more"
                                                           : std::to_string(lo) + " to " + std::to_string(hi));
        throw diagErr(diag::Code::E0007_InvalidArguments, line,
                      "'" + fn + "' expects " + want + " argument(s) but got " + std::to_string(a.size()));
    }
}

// مصفوفة/مجموعة/قاموس (مفاتيحه)/نص (نقاط ترميز UTF-8) -> لقطة عناصر. false إن لم تكن قابلة للتكرار.
static bool iterItems(const Value& v, std::vector<Value>& out) {
    switch (v.type) {
        case Value::Type::ARRAY: if (v.array) out = *v.array; return true;
        case Value::Type::SET:   if (v.set) out = v.set->items; return true;
        case Value::Type::MAP:
            if (v.map) { out.reserve(v.map->size()); for (auto& kv : *v.map) out.push_back(kv.first); }
            return true;
        case Value::Type::STRING:
            for (auto& cp : utf8Codepoints(v.str)) out.push_back(Value::string(cp));
            return true;
        default: return false;
    }
}

static std::vector<Value> needItems(const Value& v, const std::string& fn, int line) {
    std::vector<Value> out;
    if (!iterItems(v, out))
        throw typeErr(line, "'" + fn + "' expects an array, set, map or string, found `" + v.typeName() + "`");
    return out;
}

static SetPtr buildSet(const std::vector<Value>& items) {
    auto r = std::make_shared<SetData>();
    for (auto& e : items) r->add(e);
    return r;
}

static Value arrayOf(const std::vector<Value>& items) {
    return Value::makeArray(std::make_shared<ArrayData>(items));
}

// ترتيب عناصر متجانسة (كلها أرقام أو كلها نصوص) — نفس شرط natives["sort"].
static bool lessVal(const Value& x, const Value& y, int line, const std::string& fn) {
    if (x.type == Value::Type::NUMBER && y.type == Value::Type::NUMBER) return x.number < y.number;
    if (x.type == Value::Type::STRING && y.type == Value::Type::STRING) return x.str < y.str;
    throw diagErr(diag::Code::E0035_RuntimeError, line,
                  "'" + fn + "' needs every element to be a number, or every element to be a string");
}

} // namespace setimpl

// ============================ الطرق (s.method / Set.method) ============================

bool Interpreter::tryCallSetMethod(const Value& obj, const std::string& m, std::vector<Value>& a, int line, Value& out) {
    using namespace setimpl;
    if (obj.type != Value::Type::SET || !obj.set) return false;
    SetData& S = *obj.set;
    const std::string fn = "Set." + m;

    auto one = [&]() { argc(fn, a, 1, 1, line); };
    auto callback = [&](const Value& f) {
        if (f.type != Value::Type::FUNCTION && !(f.type == Value::Type::INSTANCE))
            throw typeErr(line, "'" + fn + "' expects a function, found `" + f.typeName() + "`");
    };
    auto call1 = [&](const Value& f, const Value& x) {
        std::vector<Value> args{x};
        return callValue(f, args, line);
    };

    // ---- حجم/عضوية ----
    if (m == "size" || m == "len" || m == "length") { argc(fn, a, 0, 0, line); out = Value::num(static_cast<double>(S.size())); return true; }
    if (m == "isEmpty" || m == "empty") { argc(fn, a, 0, 0, line); out = Value::boolean_(S.empty()); return true; }
    if (m == "has" || m == "contains") { one(); out = Value::boolean_(S.contains(a[0])); return true; }
    if (m == "hasAll") {
        one();
        for (auto& e : needItems(a[0], fn, line)) if (!S.contains(e)) { out = Value::boolean_(false); return true; }
        out = Value::boolean_(true); return true;
    }
    if (m == "hasAny") {
        one();
        for (auto& e : needItems(a[0], fn, line)) if (S.contains(e)) { out = Value::boolean_(true); return true; }
        out = Value::boolean_(false); return true;
    }

    // ---- تعديل في المكان (مرجعي: يؤثر على كل من يشير لنفس المجموعة) ----
    if (m == "add") { one(); out = Value::boolean_(S.add(a[0])); return true; } // true إن كان جديداً
    if (m == "addAll") {
        one();
        size_t n = 0;
        for (auto& e : needItems(a[0], fn, line)) if (S.add(e)) ++n;
        out = Value::num(static_cast<double>(n)); return true; // عدد العناصر الجديدة فعلاً
    }
    if (m == "remove") { one(); out = Value::boolean_(S.remove(a[0])); return true; } // true إن كان موجوداً
    if (m == "removeAll") {
        one();
        auto other = buildSet(needItems(a[0], fn, line));
        std::vector<Value> keep;
        for (auto& e : S.items) if (!other->contains(e)) keep.push_back(e);
        size_t removed = S.items.size() - keep.size();
        if (removed) { S.items = std::move(keep); S.indexed = false; }
        out = Value::num(static_cast<double>(removed)); return true;
    }
    if (m == "retain") { // يُبقي فقط ما يوجد أيضاً في الوسيط (تقاطع في المكان)
        one();
        auto other = buildSet(needItems(a[0], fn, line));
        std::vector<Value> keep;
        for (auto& e : S.items) if (other->contains(e)) keep.push_back(e);
        size_t removed = S.items.size() - keep.size();
        if (removed) { S.items = std::move(keep); S.indexed = false; }
        out = Value::num(static_cast<double>(removed)); return true;
    }
    if (m == "clear") { argc(fn, a, 0, 0, line); size_t n = S.size(); S.clear(); out = Value::num(static_cast<double>(n)); return true; }
    if (m == "pop") { // يُزيل ويُرجع أقدم عنصر أُدرج
        argc(fn, a, 0, 0, line);
        if (S.empty()) throw typeErr(line, "'" + fn + "': المجموعة فارغة");
        Value v = S.items.front();
        S.remove(v);
        out = v; return true;
    }

    // ---- جبر المجموعات (تُرجع مجموعة جديدة ولا تمسّ الأصل) ----
    if (m == "union") {
        SetPtr r = std::make_shared<SetData>(S);
        for (auto& arg : a) for (auto& e : needItems(arg, fn, line)) r->add(e);
        out = Value::makeSet(r); return true;
    }
    if (m == "intersect" || m == "intersection") {
        SetPtr r = std::make_shared<SetData>(S);
        for (auto& arg : a) r = setops::intersect(*r, *buildSet(needItems(arg, fn, line)));
        out = Value::makeSet(r); return true;
    }
    if (m == "diff" || m == "difference") {
        SetPtr r = std::make_shared<SetData>(S);
        for (auto& arg : a) r = setops::subtract(*r, *buildSet(needItems(arg, fn, line)));
        out = Value::makeSet(r); return true;
    }
    if (m == "symDiff") {
        one();
        auto other = buildSet(needItems(a[0], fn, line));
        auto left = setops::subtract(S, *other);
        auto right = setops::subtract(*other, S);
        out = Value::makeSet(setops::unite(*left, *right)); return true;
    }

    // ---- علاقات ----
    if (m == "isSubset")   { one(); out = Value::boolean_(setops::subset(S, *buildSet(needItems(a[0], fn, line)))); return true; }
    if (m == "isSuperset") { one(); out = Value::boolean_(setops::subset(*buildSet(needItems(a[0], fn, line)), S)); return true; }
    if (m == "isDisjoint") {
        one();
        for (auto& e : needItems(a[0], fn, line)) if (S.contains(e)) { out = Value::boolean_(false); return true; }
        out = Value::boolean_(true); return true;
    }
    if (m == "equals") { one(); out = Value::boolean_(setops::equal(S, *buildSet(needItems(a[0], fn, line)))); return true; }

    // ---- تحويل/نسخ ----
    if (m == "toArray") { argc(fn, a, 0, 0, line); out = arrayOf(S.items); return true; }
    if (m == "copy" || m == "clone") { argc(fn, a, 0, 0, line); out = Value::makeSet(std::make_shared<SetData>(S)); return true; }
    if (m == "sorted") {
        argc(fn, a, 0, 0, line);
        std::vector<Value> v = S.items;
        std::sort(v.begin(), v.end(), [&](const Value& x, const Value& y) { return lessVal(x, y, line, fn); });
        out = arrayOf(v); return true;
    }
    if (m == "join") {
        argc(fn, a, 0, 1, line);
        std::string sep = ",";
        if (!a.empty()) {
            if (a[0].type != Value::Type::STRING) throw typeErr(line, "'" + fn + "' expects the separator as a string");
            sep = a[0].str;
        }
        std::string s;
        for (size_t i = 0; i < S.items.size(); ++i) { if (i) s += sep; s += S.items[i].toDisplayString(); }
        out = Value::string(s); return true;
    }
    if (m == "first") { argc(fn, a, 0, 0, line); out = S.empty() ? Value::nil() : S.items.front(); return true; }
    if (m == "last")  { argc(fn, a, 0, 0, line); out = S.empty() ? Value::nil() : S.items.back();  return true; }
    if (m == "min" || m == "max") {
        argc(fn, a, 0, 0, line);
        if (S.empty()) throw typeErr(line, "'" + fn + "': المجموعة فارغة");
        Value best = S.items[0];
        for (size_t i = 1; i < S.items.size(); ++i) {
            bool less = lessVal(S.items[i], best, line, fn);
            if (m == "min" ? less : lessVal(best, S.items[i], line, fn)) best = S.items[i];
        }
        out = best; return true;
    }
    if (m == "sum") {
        argc(fn, a, 0, 0, line);
        double t = 0;
        for (auto& e : S.items) {
            if (e.type != Value::Type::NUMBER) throw typeErr(line, "'" + fn + "' needs numbers only; found `" + e.typeName() + "`");
            t += e.number;
        }
        out = Value::num(t); return true;
    }

    // ---- دوال عليا (callback) — تعمل على لقطة فيُسمح للدالة بتعديل المجموعة بأمان ----
    if (m == "map") {
        one(); callback(a[0]);
        std::vector<Value> snap = S.items;
        auto r = std::make_shared<SetData>();
        for (auto& e : snap) r->add(call1(a[0], e));
        out = Value::makeSet(r); return true;
    }
    if (m == "filter") {
        one(); callback(a[0]);
        std::vector<Value> snap = S.items;
        auto r = std::make_shared<SetData>();
        for (auto& e : snap) if (call1(a[0], e).isTruthy()) r->add(e);
        out = Value::makeSet(r); return true;
    }
    if (m == "each" || m == "forEach") {
        one(); callback(a[0]);
        std::vector<Value> snap = S.items;
        for (auto& e : snap) call1(a[0], e);
        out = Value::nil(); return true;
    }
    if (m == "some" || m == "any") {
        one(); callback(a[0]);
        std::vector<Value> snap = S.items;
        for (auto& e : snap) if (call1(a[0], e).isTruthy()) { out = Value::boolean_(true); return true; }
        out = Value::boolean_(false); return true;
    }
    if (m == "every" || m == "all") {
        one(); callback(a[0]);
        std::vector<Value> snap = S.items;
        for (auto& e : snap) if (!call1(a[0], e).isTruthy()) { out = Value::boolean_(false); return true; }
        out = Value::boolean_(true); return true;
    }
    if (m == "reduce") {
        argc(fn, a, 1, 2, line); callback(a[0]);
        std::vector<Value> snap = S.items;
        size_t i = 0;
        Value acc;
        if (a.size() == 2) acc = a[1];
        else {
            if (snap.empty()) throw typeErr(line, "'" + fn + "': المجموعة فارغة ولا توجد قيمة ابتدائية");
            acc = snap[i++];
        }
        for (; i < snap.size(); ++i) { std::vector<Value> args{acc, snap[i]}; acc = callValue(a[0], args, line); }
        out = acc; return true;
    }

    auto d = diagErr(diag::Code::E0006_UnknownFunction, line, "`set` has no method `" + m + "`");
    d.diagnostic->withReason("no operation named `" + m + "` exists on a Set");
    d.diagnostic->withHint("common ones: add, remove, has, size, union, intersect, diff, toArray, sorted — see docs/set.md");
    throw d;
}

// ============================ تسجيل الدوال الأصلية ============================

void Interpreter::registerNativesSet() {
    using namespace setimpl;

    // ---- المُنشئ: Set() · Set(iterable) · Set(a, b, c) ----
    // وسيط واحد مصفوفة/مجموعة/قاموس => يُؤخذ عناصره (القاموس: مفاتيحه)؛ أي شيء آخر => كل وسيط عنصر.
    // النص وسيطاً مفرداً هو عنصر واحد (لتفكيكه إلى أحرف استعمل Set.from("...")).
    natives["Set"] = [](Args& a, int line) -> Value {
        (void)line;
        auto r = std::make_shared<SetData>();
        if (a.size() == 1 && (a[0].type == Value::Type::ARRAY || a[0].type == Value::Type::SET || a[0].type == Value::Type::MAP)) {
            std::vector<Value> items;
            iterItems(a[0], items);
            for (auto& e : items) r->add(e);
        } else {
            for (auto& v : a) r->add(v);
        }
        return Value::makeSet(r);
    };
    // Set.of(...): كل وسيط عنصر دائماً (حتى لو كان مصفوفة).
    natives["Set.of"] = [](Args& a, int line) -> Value {
        (void)line;
        auto r = std::make_shared<SetData>();
        for (auto& v : a) r->add(v);
        return Value::makeSet(r);
    };
    // Set.from(x): x مصفوفة/مجموعة/قاموس(مفاتيحه)/نص(أحرفه بنقاط ترميز UTF-8).
    natives["Set.from"] = [](Args& a, int line) -> Value {
        argc("Set.from", a, 1, 1, line);
        return Value::makeSet(buildSet(needItems(a[0], "Set.from", line)));
    };

    // ---- الصيغة الثابتة: Set.add(s, x) ... تساوي s.add(x). غير المُعدِّلة تقبل أي قابل للتكرار أولاً. ----
    for (const auto& mi : kSetMethods) {
        std::string name = mi.name;
        bool mut = mi.mut;
        natives["Set." + name] = [this, name, mut](Args& a, int line) -> Value {
            if (a.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line,
                                         "'Set." + name + "' expects the set as its first argument");
            Value target = a[0];
            if (target.type != Value::Type::SET) {
                if (mut) throw typeErr(line, "'Set." + name + "' changes a set in place, so its first argument must be a set, found `" + target.typeName() + "`");
                target = Value::makeSet(buildSet(needItems(target, "Set." + name, line)));
            }
            Args rest(a.begin() + 1, a.end());
            Value out;
            tryCallSetMethod(target, name, rest, line, out);
            return out;
        };
    }

    // ---- جعل الدوال العامة الموجودة تفهم Set (بلا أسماء جديدة) ----
    auto prevLen = natives["len"];
    natives["len"] = [prevLen](Args& a, int line) -> Value {
        if (a.size() == 1 && a[0].type == Value::Type::SET) return Value::num(static_cast<double>(a[0].set ? a[0].set->size() : 0));
        return prevLen(a, line);
    };
    auto prevEmpty = natives["empty"];
    natives["empty"] = [prevEmpty](Args& a, int line) -> Value {
        if (a.size() == 1 && a[0].type == Value::Type::SET) return Value::boolean_(!a[0].set || a[0].set->empty());
        return prevEmpty(a, line);
    };
    auto prevContains = natives["contains"];
    natives["contains"] = [prevContains](Args& a, int line) -> Value {
        if (a.size() == 2 && a[0].type == Value::Type::SET) return Value::boolean_(a[0].set && a[0].set->contains(a[1]));
        return prevContains(a, line);
    };
    auto prevHas = natives["has"];
    natives["has"] = [prevHas](Args& a, int line) -> Value {
        if (a.size() == 2 && a[0].type == Value::Type::SET) return Value::boolean_(a[0].set && a[0].set->contains(a[1]));
        return prevHas(a, line);
    };
    auto prevRemove = natives["remove"];
    natives["remove"] = [prevRemove](Args& a, int line) -> Value {
        if (a.size() == 2 && a[0].type == Value::Type::SET) return Value::boolean_(a[0].set && a[0].set->remove(a[1]));
        return prevRemove(a, line);
    };
    // دوال قراءة فقط تأخذ مصفوفة: نمرّر لها نسخة مصفوفة من عناصر المجموعة (بترتيب الإدراج).
    static const char* kReadOnlyArrayFns[] = {
        "join", "sum", "mean", "median", "variance", "stddev", "mode", "minOf", "maxOf", "product", "count",
        "geometricMean", "harmonicMean", "rms", "percentile", "iqr", "all", "any", "none", "jsonEncode",
    };
    for (const char* nm : kReadOnlyArrayFns) {
        auto it = natives.find(nm);
        if (it == natives.end()) continue;
        auto prev = it->second;
        natives[nm] = [prev](Args& a, int line) -> Value {
            if (!a.empty() && a[0].type == Value::Type::SET) {
                Args b = a;
                b[0] = arrayOf(a[0].set ? a[0].set->items : std::vector<Value>{});
                return prev(b, line);
            }
            return prev(a, line);
        };
    }
}

} // namespace rin

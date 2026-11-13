// ============================================================================
//  rin_container_blend.cpp — دمج الحاويات الموجودة لتكوين حاوية/ميزة جديدة (blend + وصفات recipe)
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) من rin_interpreter.cpp بعد rin_container_zip.cpp ويُسجَّل عبر
//  Interpreter::registerNativesContainerBlend(). التوثيق: docs/container_blend.md
//
//  لا مفهوم جديد في اللغة: هو الدمج الذي كانت مكتبة compose تؤدّيه للحقول فقط (mergeContainers)،
//  منقولاً إلى مستوى الحاوية كلها — حقول + مستندات (doc) + صفوف جدول + ستايل + النوع — مع سياسة تعارض
//  وتقرير كامل، ويقبل أي مفهوم حاويات موجود كمصدر (اسم حاوية، مصفوفة أسماء، Containers.Group، Volume).
//
//    container.blend(target, sources, opts?)   يدمج المصادر في target (تُنشأ إن غابت) ويعيد تقريراً
//    container.recipe(name, spec)              يحفظ وصفة دمج مسمّاة: {from, target?, include?, ...opts}
//    container.cook(name, target?)             ينفّذ الوصفة (يمكن إعادة تنفيذها كلما تغيّرت المصادر)
//    container.recipes()                       أسماء الوصفات
//    container.recipeInfo(name)                نص الوصفة (نسخة)
//    container.dropRecipe(name)                يحذف وصفة
//  opts: policy "deep"|"override"|"keep"|"error" · only/omit (حقول) · prefix أو prefixes{مصدر:بادئة}
//        kind (نوع target إن أُنشئ) · documents/rows/style (bool، الافتراضي true) · dry (تقرير بلا كتابة)
// ============================================================================
#include "rin_interpreter.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace rin {
namespace blend {
using namespace extra;
using czip::optv;
using czip::optBool;
using czip::optStr;
using czip::strList;
using czip::mkMap;
using czip::numV;
using czip::strV;
using czip::boolV;

// نسخة عميقة: لا يتشارك target مع المصادر أي map/array/set (دلالة المرجع في Rin كانت ستُفسد المصدر عند التعديل)
static Value cloneV(const Value& v, int d = 0) {
    if (d > 200) throw std::runtime_error("بنية متداخلة أعمق من 200 مستوى");
    switch (v.type) {
        case Value::Type::MAP: {
            if (!v.map) return v;
            MapData m;
            for (const auto& kv : *v.map) m.push_back({kv.first, cloneV(kv.second, d + 1)});
            return newMap(std::move(m));
        }
        case Value::Type::ARRAY: {
            if (!v.array) return v;
            ArrayData a;
            for (const auto& x : *v.array) a.push_back(cloneV(x, d + 1));
            return newArray(std::move(a));
        }
        case Value::Type::SET: {
            if (!v.set) return v;
            auto s = std::make_shared<SetData>();
            for (const auto& x : v.set->items) s->add(cloneV(x, d + 1));
            return Value::makeSet(s);
        }
        default: return v;
    }
}

// دمج عميق: map بعمق، array لصق، Set اتحاد، وغير ذلك الأحدث يفوز
static Value mergeV(const Value& o, const Value& n, int d = 0) {
    if (d > 200) throw std::runtime_error("بنية متداخلة أعمق من 200 مستوى");
    if (o.type == Value::Type::MAP && n.type == Value::Type::MAP && o.map && n.map) {
        Value r = cloneV(o);
        for (const auto& kv : *n.map) {
            if (Value* ex = r.map->findValue(kv.first)) *ex = mergeV(*ex, kv.second, d + 1);
            else r.map->append(kv.first, cloneV(kv.second));
        }
        return r;
    }
    if (o.type == Value::Type::ARRAY && n.type == Value::Type::ARRAY && o.array && n.array) {
        Value r = cloneV(o);
        for (const auto& x : *n.array) r.array->push_back(cloneV(x));
        return r;
    }
    if (o.type == Value::Type::SET && n.type == Value::Type::SET && o.set && n.set) {
        Value r = cloneV(o);
        for (const auto& x : n.set->items) r.set->add(cloneV(x));
        return r;
    }
    return cloneV(n);
}

struct COpt { bool caseless = false, natural = false, nilsFirst = true; };
static int cmpV(const Value& a, const Value& b, const COpt& o, int d = 0);
static Value instanceFields(const Value& v);
// ---- السياسة المتقدمة: سياسة أساسية + دالة قرار + قواعد لكل مسار
// rules: {"opts.theme": "keep", "roles": "union", "stats.*": "sum", "x": fn(old, new, path)}
//   الاستراتيجيات: deep · override|last · keep|first · error · concat · union · sum · max · min
struct Pol {
    std::string base = "deep", sep;
    Value fn;                                              // policy كدالة fn(old, new, path) تُستدعى عند كل تعارض حقيقي
    std::vector<std::pair<std::string, Value>> rules;      // نمط مسار (يقبل * و ?) -> استراتيجية (نص) أو دالة
    bool unique = false;
    std::function<Value(const Value&, std::vector<Value>&)> call;
};
static bool validStrategy(const std::string& s) {
    static const char* ok[] = {"deep", "override", "last", "keep", "first", "error", "concat", "union", "sum", "max", "min"};
    for (const char* k : ok) if (s == k) return true;
    return false;
}
static bool wild(const char* p, const char* s) {
    while (*p) {
        if (*p == '*') { ++p; if (!*p) return true; for (; *s; ++s) if (wild(p, s)) return true; return wild(p, s); }
        if (!*s || (*p != '?' && *p != *s)) return false;
        ++p; ++s;
    }
    return !*s;
}
static const Value* ruleFor(const Pol& P, const std::string& path) {
    for (const auto& r : P.rules) if (r.first == path || wild(r.first.c_str(), path.c_str())) return &r.second;
    return nullptr;
}
static Value mergeR(const Value& o, const Value& n, const std::string& path, const Pol& P, int d = 0);
static const char* kindName(const Value& v);
static Value strategy(const std::string& st, const Value& o, const Value& n, const std::string& path, const Pol& P, int d) {
    auto bad = [&](const char* what) { return std::runtime_error(std::string("القاعدة '") + st + "' على '" + (path.empty() ? "القيمة" : path) + "': " + what); };
    if (st == "override" || st == "last") return cloneV(n);
    if (st == "keep" || st == "first") return cloneV(o);
    if (st == "error") { if (valuesEqual(o, n)) return cloneV(o); throw std::runtime_error("تعارض على '" + (path.empty() ? std::string("القيمة") : path) + "' (error)"); }
    if ((st == "max" || st == "min") && !(o.type == Value::Type::NUMBER && n.type == Value::Type::NUMBER)) { // أي نوعين عبر الترتيب الشامل compare
        COpt co; int c = cmpV(o, n, co);
        return cloneV((st == "max") == (c >= 0) ? o : n);
    }
    if (st == "sum" || st == "max" || st == "min") {
        if (o.type != Value::Type::NUMBER || n.type != Value::Type::NUMBER) throw bad("تحتاج رقمين");
        return Value::num(st == "sum" ? o.number + n.number : st == "max" ? std::max(o.number, n.number) : std::min(o.number, n.number));
    }
    if (st == "concat" || st == "union") {
        if (o.type == Value::Type::ARRAY && n.type == Value::Type::ARRAY && o.array && n.array) {
            Value r = cloneV(o);
            for (const auto& x : *n.array) {
                bool dup = false;
                if (st == "union") for (const auto& y : *r.array) if (valuesEqual(x, y)) { dup = true; break; }
                if (!dup) r.array->push_back(cloneV(x));
            }
            return r;
        }
        if (o.type == Value::Type::SET && n.type == Value::Type::SET) return mergeV(o, n);
        if (st == "concat" && o.type == Value::Type::STRING && n.type == Value::Type::STRING) return Value::string(o.str + P.sep + n.str);
        if (o.type == Value::Type::MAP && n.type == Value::Type::MAP && o.map && n.map) {
            Value r = cloneV(o);
            for (const auto& kv : *n.map) r.map->put(kv.first, cloneV(kv.second));
            return r;
        }
        throw bad("لا تنطبق على هذين النوعين");
    }
    // deep
    if (o.type == Value::Type::MAP && n.type == Value::Type::MAP && o.map && n.map) {
        Value r = cloneV(o);
        for (const auto& kv : *n.map) {
            std::string sub = path.empty() ? kv.first.toDisplayString() : path + "." + kv.first.toDisplayString();
            if (Value* ex = r.map->findValue(kv.first)) *ex = mergeR(*ex, kv.second, sub, P, d + 1);
            else r.map->append(kv.first, cloneV(kv.second));
        }
        return r;
    }
    if (o.type == n.type && (o.type == Value::Type::ARRAY || o.type == Value::Type::SET)) return mergeV(o, n);
    return cloneV(n);
}
static Value mergeR(const Value& o, const Value& n, const std::string& path, const Pol& P, int d) {
    if (d > 200) throw std::runtime_error("بنية متداخلة أعمق من 200 مستوى");
    if (o.type == Value::Type::NIL) return cloneV(n);
    if (n.type == Value::Type::NIL) return cloneV(o);
    if (const Value* rl = ruleFor(P, path)) {
        if (rl->type == Value::Type::FUNCTION) { std::vector<Value> args{o, n, Value::string(path)}; return cloneV(P.call(*rl, args)); }
        return strategy(rl->str, o, n, path, P, d);
    }
    bool deepish = (P.base == "deep" || P.base == "error");
    bool bothMap = o.type == Value::Type::MAP && n.type == Value::Type::MAP && o.map && n.map;
    if ((deepish || P.fn.type == Value::Type::FUNCTION) && bothMap) return strategy("deep", o, n, path, P, d);
    if (!bothMap && P.fn.type != Value::Type::FUNCTION && deepish && o.type == n.type && (o.type == Value::Type::ARRAY || o.type == Value::Type::SET)) return mergeV(o, n);
    if (valuesEqual(o, n)) return cloneV(o);
    if (P.fn.type == Value::Type::FUNCTION) { std::vector<Value> args{o, n, Value::string(path)}; return cloneV(P.call(P.fn, args)); }
    if (bothMap && !deepish) { // override/keep على map: لكل مفتاح على حدة
        Value r = cloneV(o);
        for (const auto& kv : *n.map) {
            if (Value* ex = r.map->findValue(kv.first)) { if (P.base == "override") *ex = cloneV(kv.second); }
            else r.map->append(kv.first, cloneV(kv.second));
        }
        return r;
    }
    return strategy(P.base, o, n, path, P, d);
}
static const char* kindName(const Value& v) {
    switch (v.type) {
        case Value::Type::NIL: return "nil"; case Value::Type::NUMBER: return "number"; case Value::Type::STRING: return "string";
        case Value::Type::BOOL: return "bool"; case Value::Type::FUNCTION: return "function"; case Value::Type::ARRAY: return "array";
        case Value::Type::MAP: return "map"; case Value::Type::INSTANCE: return "instance"; case Value::Type::SET: return "set";
    }
    return "?";
}
// قيمة instance → map حقولها (بترتيب الإدخال) كي تدخل في الدمج مع أي map
static Value instanceFields(const Value& v) {
    MapData m;
    if (v.instance) {
        for (const auto& k : v.instance->fieldOrder) { auto it = v.instance->fields.find(k); if (it != v.instance->fields.end()) m.push_back({Value::string(k), cloneV(it->second)}); }
        for (const auto& kv : v.instance->fields) if (std::find(v.instance->fieldOrder.begin(), v.instance->fieldOrder.end(), kv.first) == v.instance->fieldOrder.end()) m.push_back({Value::string(kv.first), cloneV(kv.second)});
    }
    return newMap(std::move(m));
}
// الدمج العام لقائمة قيم من العائلة نفسها
static Value blendValues(const std::vector<Value>& in, const Pol& P, const std::string& who) {
    std::vector<Value> vs;
    for (const auto& v : in) if (v.type != Value::Type::NIL) vs.push_back(v.type == Value::Type::INSTANCE ? instanceFields(v) : v);
    if (vs.empty()) return Value();
    Value::Type t = vs[0].type;
    for (const auto& v : vs) if (v.type != t)
        throw std::runtime_error(who + ": لا يمكن دمج " + kindName(vs[0]) + " مع " + kindName(v) + " — الأنواع يجب أن تتوافق");
    if (t == Value::Type::FUNCTION)
        throw std::runtime_error(who + ": الدوال تُدمج عبر blendFn أو blend(f, g) مباشرة");
    if (t == Value::Type::NUMBER) { double s = 0; for (const auto& v : vs) s += v.number; return Value::num(s); }
    if (t == Value::Type::STRING) { std::string r; for (size_t i = 0; i < vs.size(); ++i) { if (i) r += P.sep; r += vs[i].str; } return Value::string(r); }
    if (t == Value::Type::BOOL) throw std::runtime_error(who + ": قيم bool لا تُدمج (استعمل and/or)");
    Value acc = cloneV(vs[0]);
    for (size_t i = 1; i < vs.size(); ++i) acc = mergeR(acc, vs[i], "", P);
    if (P.unique && acc.type == Value::Type::ARRAY && acc.array) {
        ArrayData u;
        for (const auto& x : *acc.array) { bool dup = false; for (const auto& y : u) if (valuesEqual(x, y)) { dup = true; break; } if (!dup) u.push_back(x); }
        acc = newArray(std::move(u));
    }
    return acc;
}

static void readPol(const Value* o, const std::string& who, int line, Pol& P, std::string& fnMode) {
    if (!o || o->type == Value::Type::NIL) return;
        if (o->type == Value::Type::STRING) P.base = o->str;
        else if (o->type == Value::Type::MAP && o->map) {
            if (const Value* pv = o->map->findValue(Value::string("policy")); pv && pv->type != Value::Type::NIL) {
                if (pv->type == Value::Type::STRING) P.base = pv->str;
                else if (pv->type == Value::Type::FUNCTION) P.fn = *pv;
                else throw diagErr(diag::Code::E0004_InvalidType, line, who + ": policy نص أو دالة fn(old, new, path)");
            }
            P.sep = czip::optStr(o, "sep"); P.unique = czip::optBool(o, "unique", false);
            fnMode = czip::optStr(o, "fn", "chain");
            if (const Value* rv = o->map->findValue(Value::string("rules")); rv && rv->type != Value::Type::NIL) {
                if (rv->type != Value::Type::MAP || !rv->map) throw diagErr(diag::Code::E0004_InvalidType, line, who + ": rules map {مسار: استراتيجية|دالة}");
                for (const auto& kv : *rv->map) {
                    if (kv.second.type == Value::Type::STRING && !validStrategy(kv.second.str))
                        throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": استراتيجية غير معروفة '" + kv.second.str + "' (deep override keep error concat union sum max min first last)");
                    if (kv.second.type != Value::Type::STRING && kv.second.type != Value::Type::FUNCTION)
                        throw diagErr(diag::Code::E0004_InvalidType, line, who + ": قاعدة '" + kv.first.toDisplayString() + "' نص أو دالة");
                    P.rules.push_back({kv.first.toDisplayString(), kv.second});
                }
            }
        } else throw diagErr(diag::Code::E0004_InvalidType, line, who + ": الخيارات نص سياسة أو map");
        if (P.base != "deep" && P.base != "override" && P.base != "keep" && P.base != "error")
            throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": policy يجب أن تكون deep أو override أو keep أو error (أو دالة)");
        if (fnMode != "chain" && fnMode != "all" && fnMode != "first" && fnMode != "each")
            throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": fn يجب أن تكون chain أو all أو first أو each");
}
// ======================================================================== الترتيب الشامل: compare / orderBy
// ترتيب كلي لأي قيمتين: nil < bool < number < string < array < Set < map < instance < function (بالرتبة)، ثم داخل النوع:
// أرقام بالقيمة (NaN أصغر) · نصوص بايتياً (caseless/natural اختياري) · مصفوفات معجمياً ثم بالطول · Set/map بعد الفرز ثم بالحجم.
static int rankOf(const Value& v) {
    switch (v.type) {
        case Value::Type::NIL: return 0; case Value::Type::BOOL: return 1; case Value::Type::NUMBER: return 2; case Value::Type::STRING: return 3;
        case Value::Type::ARRAY: return 4; case Value::Type::SET: return 5; case Value::Type::MAP: return 6; case Value::Type::INSTANCE: return 7;
        default: return 8;
    }
}
static int cmpStr(const std::string& a, const std::string& b, const COpt& o) {
    size_t i = 0, j = 0;
    auto low = [&](unsigned char c) { return o.caseless ? (unsigned char)std::tolower(c) : c; };
    while (i < a.size() && j < b.size()) {
        unsigned char x = a[i], y = b[j];
        if (o.natural && std::isdigit(x) && std::isdigit(y)) { // مقاطع رقمية بالقيمة
            size_t i2 = i, j2 = j;
            while (i2 < a.size() && std::isdigit((unsigned char)a[i2])) ++i2;
            while (j2 < b.size() && std::isdigit((unsigned char)b[j2])) ++j2;
            std::string da = a.substr(i, i2 - i), db = b.substr(j, j2 - j);
            da.erase(0, std::min(da.find_first_not_of('0'), da.size())); db.erase(0, std::min(db.find_first_not_of('0'), db.size()));
            if (da.size() != db.size()) return da.size() < db.size() ? -1 : 1;
            if (int c = da.compare(db)) return c < 0 ? -1 : 1;
            i = i2; j = j2; continue;
        }
        unsigned char lx = low(x), ly = low(y);
        if (lx != ly) return lx < ly ? -1 : 1;
        ++i; ++j;
    }
    if (a.size() - i != b.size() - j) return (a.size() - i) < (b.size() - j) ? -1 : 1;
    return 0;
}
static int cmpSeq(const std::vector<Value>& x, const std::vector<Value>& y, const COpt& o, int d) {
    size_t n = std::min(x.size(), y.size());
    for (size_t i = 0; i < n; ++i) if (int c = cmpV(x[i], y[i], o, d + 1)) return c;
    return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
}
static int cmpV(const Value& a, const Value& b, const COpt& o, int d) {
    if (d > 100) throw std::runtime_error("compare: بنية متداخلة أعمق من 100 مستوى");
    int ra = rankOf(a), rb = rankOf(b);
    if (ra != rb) {
        if (!o.nilsFirst && (ra == 0 || rb == 0)) return ra == 0 ? 1 : -1;
        return ra < rb ? -1 : 1;
    }
    switch (a.type) {
        case Value::Type::NIL: return 0;
        case Value::Type::BOOL: return a.boolean == b.boolean ? 0 : (!a.boolean ? -1 : 1);
        case Value::Type::NUMBER: {
            bool na = std::isnan(a.number), nb = std::isnan(b.number);
            if (na || nb) return na == nb ? 0 : (na ? -1 : 1);
            return a.number == b.number ? 0 : (a.number < b.number ? -1 : 1);
        }
        case Value::Type::STRING: return cmpStr(a.str, b.str, o);
        case Value::Type::ARRAY: return cmpSeq(a.array ? *a.array : ArrayData{}, b.array ? *b.array : ArrayData{}, o, d);
        case Value::Type::SET: {
            std::vector<Value> x = a.set ? a.set->items : std::vector<Value>{}, y = b.set ? b.set->items : std::vector<Value>{};
            if (x.size() != y.size()) return x.size() < y.size() ? -1 : 1;
            auto lt = [&](const Value& p, const Value& q) { return cmpV(p, q, o, d + 1) < 0; };
            std::stable_sort(x.begin(), x.end(), lt); std::stable_sort(y.begin(), y.end(), lt);
            return cmpSeq(x, y, o, d);
        }
        case Value::Type::MAP: {
            std::vector<std::pair<Value, Value>> x, y;
            if (a.map) for (const auto& kv : *a.map) x.push_back(kv);
            if (b.map) for (const auto& kv : *b.map) y.push_back(kv);
            auto lt = [&](const std::pair<Value, Value>& p, const std::pair<Value, Value>& q) { return cmpV(p.first, q.first, o, d + 1) < 0; };
            std::stable_sort(x.begin(), x.end(), lt); std::stable_sort(y.begin(), y.end(), lt);
            size_t n = std::min(x.size(), y.size());
            for (size_t i = 0; i < n; ++i) {
                if (int c = cmpV(x[i].first, y[i].first, o, d + 1)) return c;
                if (int c = cmpV(x[i].second, y[i].second, o, d + 1)) return c;
            }
            return x.size() == y.size() ? 0 : (x.size() < y.size() ? -1 : 1);
        }
        case Value::Type::INSTANCE: {
            if (!a.instance || !b.instance) return 0;
            if (int c = a.instance->className.compare(b.instance->className)) return c < 0 ? -1 : 1;
            return cmpV(instanceFields(a), instanceFields(b), o, d + 1);
        }
        default: return 0;
    }
}
// يقرأ مساراً منقّطاً (a.b.0) من map/كائن/مصفوفة؛ المفقود = nil
static Value pathGet(const Value& v, const std::string& path) {
    Value cur = v;
    size_t i = 0;
    while (i <= path.size()) {
        size_t j = path.find('.', i); if (j == std::string::npos) j = path.size();
        std::string seg = path.substr(i, j - i);
        Value next;
        if (cur.type == Value::Type::MAP && cur.map) { if (const Value* f = cur.map->findValue(Value::string(seg))) next = *f; }
        else if (cur.type == Value::Type::INSTANCE && cur.instance) { auto it = cur.instance->fields.find(seg); if (it != cur.instance->fields.end()) next = it->second; }
        else if (cur.type == Value::Type::ARRAY && cur.array && !seg.empty() && std::all_of(seg.begin(), seg.end(), ::isdigit)) { size_t k = std::stoul(seg); if (k < cur.array->size()) next = (*cur.array)[k]; }
        cur = next;
        i = j + 1;
    }
    return cur;
}

static bool isContainerLike(const Value& v) {
    return v.type == Value::Type::MAP || v.type == Value::Type::ARRAY || v.type == Value::Type::SET;
}

} // namespace blend

int compareValues(const Value& a, const Value& b) { blend::COpt c; return blend::cmpV(a, b, c); }

void Interpreter::registerNativesContainerBlend() {
    using namespace extra;
    using namespace blend;
    using Args = std::vector<Value>;

    // أسماء المصادر الفعلية: نص | مصفوفة (متداخلة) | Containers.Group | Volume
    auto expandSources = [this](const Value& v, const std::string& fn, int line) {
        std::vector<std::string> out;
        auto add = [&](const std::string& c) { if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c); };
        std::function<void(const Value&, int)> one = [&](const Value& x, int depth) {
            if (depth > 8) throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "': مصفوفة المصادر متداخلة أكثر من اللازم");
            if (x.type == Value::Type::ARRAY && x.array) { for (const auto& y : *x.array) one(y, depth + 1); return; }
            if (x.type != Value::Type::STRING)
                throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' يتوقع اسم حاوية أو مصفوفة أسماء أو اسم Group/Volume");
            std::string c = canonContainer(x.str);
            if (!containers.count(c)) {
                const char* which = groupMembers.count(x.str) ? "groupContainers" : (volumeMembers.count(x.str) ? "volumeContainers" : nullptr);
                if (which && natives.count(which)) {
                    Args ga{Value::string(x.str)};
                    Value members = natives[which](ga, line);
                    if (members.type == Value::Type::ARRAY && members.array)
                        for (const auto& m : *members.array) if (m.type == Value::Type::STRING) add(canonContainer(m.str));
                    return;
                }
            }
            add(c);
        };
        one(v, 0);
        return out;
    };

    struct Conflict { std::string key, from, action; };

    // ------------------------------------------------------------------ container.blend
    auto doBlend = [this, expandSources](const std::string& who, const std::string& targetIn, const std::vector<std::string>& srcIn, const Value* opts, int line) -> Value {
        std::string target = canonContainer(targetIn);
        std::string policy = czip::optStr(opts, "policy", "deep");
        if (policy != "deep" && policy != "override" && policy != "keep" && policy != "error")
            throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": policy يجب أن تكون deep أو override أو keep أو error");
        std::vector<std::string> only = czip::strList(czip::optv(opts, "only"), who, "only", line);
        std::vector<std::string> omit = czip::strList(czip::optv(opts, "omit"), who, "omit", line);
        Pol P;
        P.call = [this, line](const Value& f, std::vector<Value>& a) { return callValue(f, a, line); };
        { std::string ignoredMode; readPol(opts, who, line, P, ignoredMode); }
        bool wantDocs = czip::optBool(opts, "documents", true), wantRows = czip::optBool(opts, "rows", true), wantStyle = czip::optBool(opts, "style", true);
        bool dry = czip::optBool(opts, "dry", false);
        std::string prefix = czip::optStr(opts, "prefix");
        const Value* prefixes = czip::optv(opts, "prefixes");

        std::vector<std::string> sources;
        for (const auto& s : srcIn) {
            if (s == target) continue; // الهدف أساس الدمج أصلاً
            if (!containers.count(s)) throw diagErr(diag::Code::E0014_InvalidContainer, line, who + ": الحاوية المصدر '" + s + "' غير موجودة");
            sources.push_back(s);
        }
        if (sources.empty()) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": لا مصادر للدمج (أعطِ حاوية واحدة على الأقل غير الهدف)");

        auto kindOf = [&](const std::string& n) {
            Args ka{Value::string(n)};
            Value k = natives["container.kind"](ka, line);
            return k.type == Value::Type::STRING ? k.str : std::string("container");
        };

        bool exists = containers.count(target) > 0;
        std::string kind;
        if (exists) kind = kindOf(target);
        else {
            kind = czip::optStr(opts, "kind");
            if (kind.empty() || kind == "auto") {
                kind = "container";
                for (const auto& s : sources) { std::string k = kindOf(s); if (k != "container") { kind = k; break; } }
            }
        }
        bool targetIsTable = exists ? isTableContainer(target) : (kind == "table");

        // ---- الخطة: تبدأ من حالة الهدف الحالية ثم تُطبَّق المصادر بالترتيب (لا كتابة قبل نجاح كل شيء)
        std::map<std::string, Value> fields;
        std::set<std::string> touched;
        std::vector<std::pair<std::string, Value>> docs;
        ArrayData rows;
        std::string style;
        if (exists) {
            for (const auto& kv : containers[target]->values) fields[kv.first] = kv.second;
            auto ds = docStore.find(target);
            if (ds != docStore.end()) docs = ds->second;
            auto tr = tableRows.find(target);
            if (targetIsTable && tr != tableRows.end() && tr->second) rows = *tr->second;
            auto st = containerStyles.find(target);
            if (st != containerStyles.end()) style = st->second;
        }
        size_t nDocs = 0, nRows = 0, nMerged = 0;
        bool rowsTouched = false, styleTouched = false, docsTouched = false;
        std::vector<Conflict> conflicts;

        try {
            for (const auto& s : sources) {
                std::string pfx = prefix;
                if (prefixes && prefixes->type == Value::Type::MAP && prefixes->map)
                    if (const Value* pv = prefixes->map->findValue(Value::string(s)); pv && pv->type == Value::Type::STRING) pfx = pv->str;

                std::vector<std::string> keys;
                for (const auto& kv : containers[s]->values) {
                    if (!only.empty() && std::find(only.begin(), only.end(), kv.first) == only.end()) continue;
                    if (std::find(omit.begin(), omit.end(), kv.first) != omit.end()) continue;
                    keys.push_back(kv.first);
                }
                std::sort(keys.begin(), keys.end());
                for (const auto& k : keys) {
                    const Value& val = containers[s]->values[k];
                    std::string nk = pfx + k;
                    auto it = fields.find(nk);
                    if (it == fields.end()) { fields[nk] = cloneV(val); touched.insert(nk); continue; }
                    const Value* rl = ruleFor(P, nk);
                    if (!rl && valuesEqual(it->second, val)) continue;
                    if (rl || P.fn.type == Value::Type::FUNCTION) { // قاعدة لهذا الحقل أو سياسة بدالة
                        it->second = mergeR(it->second, val, nk, P); conflicts.push_back({nk, s, "rule"}); ++nMerged; touched.insert(nk); continue;
                    }
                    if (policy == "error") throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": تعارض على الحقل '" + nk + "' بين المصادر (policy=error)");
                    if (policy == "keep") { conflicts.push_back({nk, s, "keep"}); continue; }
                    if (policy == "deep" && isContainerLike(it->second) && it->second.type == val.type) {
                        it->second = mergeV(it->second, val); conflicts.push_back({nk, s, "merged"}); ++nMerged;
                    } else { it->second = cloneV(val); conflicts.push_back({nk, s, "override"}); }
                    touched.insert(nk);
                }

                if (wantDocs) {
                    auto ds = docStore.find(s);
                    if (ds != docStore.end()) for (const auto& d : ds->second) {
                        bool found = false;
                        for (auto& p : docs) if (p.first == d.first) {
                            found = true;
                            { const Value* dr = ruleFor(P, "doc:" + d.first);
                              if (!dr && valuesEqual(p.second, d.second)) break;
                              if (dr || P.fn.type == Value::Type::FUNCTION) { p.second = mergeR(p.second, d.second, "doc:" + d.first, P); conflicts.push_back({"doc:" + d.first, s, "rule"}); docsTouched = true; break; } }
                            if (policy == "error") throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": تعارض على المستند '" + d.first + "' (policy=error)");
                            if (policy == "keep") { conflicts.push_back({"doc:" + d.first, s, "keep"}); break; }
                            if (policy == "deep") { p.second = mergeV(p.second, d.second); conflicts.push_back({"doc:" + d.first, s, "merged"}); }
                            else { p.second = cloneV(d.second); conflicts.push_back({"doc:" + d.first, s, "override"}); }
                            docsTouched = true;
                            break;
                        }
                        if (!found) { docs.push_back({d.first, cloneV(d.second)}); ++nDocs; docsTouched = true; }
                    }
                }
                if (wantRows && targetIsTable && isTableContainer(s)) {
                    auto tr = tableRows.find(s);
                    if (tr != tableRows.end() && tr->second) { for (const auto& r : *tr->second) { rows.push_back(cloneV(r)); ++nRows; } rowsTouched = true; }
                }
                if (wantStyle) {
                    auto st = containerStyles.find(s);
                    if (st != containerStyles.end() && !st->second.empty() && (style.empty() || policy == "override" || policy == "deep")) {
                        if (st->second != style) { style = st->second; styleTouched = true; }
                    }
                }
            }
        } catch (const std::runtime_error& e) {
            throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": " + e.what());
        }

        // ---- الاعتماد
        if (!dry) {
            if (!exists) {
                Args sp{Value::string(kind), Value::string(target)};
                natives["spawn"](sp, line);
            }
            auto env = containers[target];
            for (const auto& k : touched) env->values[k] = fields[k];
            if (docsTouched) {
                docStore[target] = docs;
                if (isDatalistContainer(target)) datalistSync(target);
            }
            if (rowsTouched) tableRows[target] = std::make_shared<ArrayData>(rows);
            if (styleTouched) containerStyles[target] = style;
        }

        ArrayData cf;
        for (const auto& c : conflicts) cf.push_back(mkMap({{"key", strV(c.key)}, {"from", strV(c.from)}, {"action", strV(c.action)}}));
        ArrayData from;
        for (const auto& s : sources) from.push_back(strV(s));
        return mkMap({{"ok", boolV(true)}, {"target", strV(target)}, {"created", boolV(!exists && !dry)}, {"kind", strV(kind)}, {"policy", strV(policy)},
                      {"from", newArray(std::move(from))}, {"fields", numV(double(touched.size()))}, {"merged", numV(double(nMerged))},
                      {"documents", numV(double(nDocs))}, {"rows", numV(double(nRows))}, {"styled", boolV(styleTouched)},
                      {"conflicts", newArray(std::move(cf))}, {"dry", boolV(dry)}});
    };

    natives["container.blend"] = [this, expandSources, doBlend](Args& a, int line) -> Value {
        need("container.blend", a, 2, 3, line);
        std::string target = str(a[0], "container.blend", line);
        std::vector<std::string> src = expandSources(a[1], "container.blend", line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        if (opts && opts->type != Value::Type::MAP && opts->type != Value::Type::NIL) throw diagErr(diag::Code::E0004_InvalidType, line, "container.blend: الخيارات يجب أن تكون map");
        return doBlend("container.blend", target, src, opts, line);
    };

    // ------------------------------------------------------------------ الوصفات
    // include: وصفات أخرى تُضاف مصادرها أولاً (تركيب ميزة من ميزات)، مع كشف الدوران
    // gather يلتقط نفسه بالمرجع: نُبقيه حياً بنسخة مشتركة
    using GatherFn = std::function<void(const std::string&, std::vector<std::string>&, std::vector<std::string>&, int, int)>;
    auto gatherP = std::make_shared<GatherFn>();
    *gatherP = [this, expandSources, gatherP](const std::string& rname, std::vector<std::string>& out, std::vector<std::string>& stack, int depth, int line) {
        if (depth > 16 || std::find(stack.begin(), stack.end(), rname) != stack.end())
            throw diagErr(diag::Code::E0035_RuntimeError, line, "container.cook: دوران أو عمق زائد في include عند الوصفة '" + rname + "'");
        auto it = blendRecipes.find(rname);
        if (it == blendRecipes.end()) throw diagErr(diag::Code::E0035_RuntimeError, line, "container.cook: الوصفة '" + rname + "' غير موجودة");
        stack.push_back(rname);
        Value spec = it->second;
        if (const Value* inc = spec.map->findValue(Value::string("include")); inc && inc->type != Value::Type::NIL) {
            std::vector<std::string> names = czip::strList(inc, "container.cook", "include", line);
            for (const auto& n : names) (*gatherP)(n, out, stack, depth + 1, line);
        }
        if (const Value* fr = spec.map->findValue(Value::string("from")); fr && fr->type != Value::Type::NIL)
            for (const auto& s : expandSources(*fr, "container.cook", line)) if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        stack.pop_back();
    };

    natives["container.recipe"] = [this](Args& a, int line) -> Value {
        need("container.recipe", a, 2, 2, line);
        std::string name = str(a[0], "container.recipe", line);
        if (name.empty()) throw diagErr(diag::Code::E0004_InvalidType, line, "container.recipe: اسم الوصفة فارغ");
        if (a[1].type != Value::Type::MAP || !a[1].map) throw diagErr(diag::Code::E0004_InvalidType, line, "container.recipe: المواصفة يجب أن تكون map {from, target?, include?, ...}");
        const Value* fr = a[1].map->findValue(Value::string("from"));
        const Value* inc = a[1].map->findValue(Value::string("include"));
        bool hasFrom = fr && fr->type != Value::Type::NIL, hasInc = inc && inc->type != Value::Type::NIL;
        if (!hasFrom && !hasInc) throw diagErr(diag::Code::E0035_RuntimeError, line, "container.recipe: أعطِ from (مصادر) أو include (وصفات)");
        blendRecipes[name] = cloneV(a[1]);
        return blendRecipes[name];
    };

    natives["container.cook"] = [this, doBlend, gatherP](Args& a, int line) -> Value {
        need("container.cook", a, 1, 2, line);
        std::string name = str(a[0], "container.cook", line);
        auto it = blendRecipes.find(name);
        if (it == blendRecipes.end()) throw diagErr(diag::Code::E0035_RuntimeError, line, "container.cook: الوصفة '" + name + "' غير موجودة");
        Value spec = it->second;
        std::string target;
        if (a.size() > 1 && a[1].type == Value::Type::STRING) target = a[1].str;
        else if (const Value* t = spec.map->findValue(Value::string("target")); t && t->type == Value::Type::STRING) target = t->str;
        if (target.empty()) throw diagErr(diag::Code::E0035_RuntimeError, line, "container.cook: حدّد الهدف (وسيط ثانٍ أو target داخل الوصفة)");
        std::vector<std::string> out, stack;
        (*gatherP)(name, out, stack, 0, line);
        Value r = doBlend("container.cook", target, out, &spec, line);
        if (r.type == Value::Type::MAP && r.map) r.map->put(Value::string("recipe"), Value::string(name));
        return r;
    };

    natives["container.recipes"] = [this](Args& a, int line) -> Value {
        need("container.recipes", a, 0, 0, line);
        std::vector<std::string> names;
        for (const auto& kv : blendRecipes) if (kv.first.rfind("__fn:", 0) != 0) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        ArrayData out;
        for (const auto& n : names) out.push_back(Value::string(n));
        return newArray(std::move(out));
    };
    natives["container.recipeInfo"] = [this](Args& a, int line) -> Value {
        need("container.recipeInfo", a, 1, 1, line);
        auto it = blendRecipes.find(str(a[0], "container.recipeInfo", line));
        if (it == blendRecipes.end()) return Value();
        return cloneV(it->second);
    };
    natives["container.dropRecipe"] = [this](Args& a, int line) -> Value {
        need("container.dropRecipe", a, 1, 1, line);
        return Value::boolean_(blendRecipes.erase(str(a[0], "container.dropRecipe", line)) > 0);
    };

    // ======================================================================== الدمج العام لأي مفهوم
    // blend(a, b, ...)           يدمج أي قيم من النوع نفسه: map عميقاً · array لصقاً · Set اتحاداً · نص لصقاً · رقم جمعاً
    //                            · instance → map حقوله. قيم nil تُهمَل. الدوال: خطأ واضح يدلّ على chain/compose.
    // blendWith(opts, a, b, ...) نفسه بخيارات: نص سياسة ("deep"|"override"|"keep"|"error") أو map {policy, unique, sep}
    // blendInto(target, a, ...)  في المكان: map/array/Set/instance تُعدَّل وتُعاد، واسم حاوية يُمرَّر إلى container.blend
    // blendRecipe(name, {parts, include?, policy?, unique?, sep?})  وصفة قيم؛ blendCook(name) تدمج أجزاءها (include أولاً)
    struct GOpts { Pol pol; std::string fnMode = "chain"; };
    // يقرأ opts (نص سياسة | map {policy|دالة, rules, sep, unique, fn}) إلى Pol
    auto parsePol = [this](const Value* o, const std::string& who, int line, GOpts& g) {
        g.pol.call = [this, line](const Value& f, std::vector<Value>& a) { return callValue(f, a, line); };
        readPol(o, who, line, g.pol, g.fnMode);
    };
    auto parseGOpts = [parsePol](const Value& o, const std::string& who, int line) { GOpts g; parsePol(&o, who, line, g); return g; };

    // ---- دمج الدوال: دالة جديدة حقيقية تُعرَّف في النطاق العام بتحليل مصدر Rin مولَّد (الدوال مرتبطة بشجرة الكود،
    //      فلا يمكن تركيبها من الداخل مباشرة). النمط: chain (خط أنابيب يسار→يمين) · all (مصفوفة نتائج) · first (أول نتيجة غير nil) · each (آخر نتيجة)
    auto arityOf = [](const Value& f) -> size_t { return (f.type == Value::Type::FUNCTION && f.function && f.function->declaration) ? f.function->declaration->params.size() : 1; };
    auto makeFnBlend = [this, arityOf](const std::string& mode, const std::vector<Value>& fns, int line) -> Value {
        for (const auto& f : fns) if (f.type != Value::Type::FUNCTION) throw diagErr(diag::Code::E0004_InvalidType, line, "blendFn: كل الأجزاء يجب أن تكون دوالاً");
        if (fns.empty()) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendFn: لا دوال");
        int id = 1;
        if (auto it = blendRecipes.find("__fn:count"); it != blendRecipes.end() && it->second.type == Value::Type::NUMBER) id = int(it->second.number) + 1;
        blendRecipes["__fn:count"] = Value::num(id);
        ArrayData fa; size_t ar = 0;
        for (const auto& f : fns) { fa.push_back(f); ar = std::max(ar, arityOf(f)); }
        blendRecipes["__fn:" + std::to_string(id)] = mkMap({{"mode", strV(mode)}, {"fns", newArray(std::move(fa))}});
        std::string nm = "__blendfn_" + std::to_string(id), params, arr;
        for (size_t i = 0; i < ar; ++i) { if (i) { params += ", "; arr += ", "; } params += "a" + std::to_string(i); arr += "a" + std::to_string(i); }
        std::string src = "fun " + nm + "(" + params + ") { return __blendfn_call(" + std::to_string(id) + ", [" + arr + "]); }";
        try {
            Lexer lex(src); auto toks = lex.scanTokens(); Parser ps(toks); auto stmts = ps.parse();
            executeBlock(stmts, globals);
        } catch (RinError& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, "blendFn: تعذّر توليد الدالة: " + e.message); }
        Value out;
        if (!lookupGlobal(nm, out)) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendFn: تعذّر العثور على الدالة المولَّدة");
        return out;
    };
    natives["__blendfn_call"] = [this, arityOf](Args& a, int line) -> Value {
        if (a.size() != 2 || a[0].type != Value::Type::NUMBER || a[1].type != Value::Type::ARRAY || !a[1].array) return Value();
        auto it = blendRecipes.find("__fn:" + std::to_string(int(a[0].number)));
        if (it == blendRecipes.end()) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendFn: الدالة المدموجة لم تعد موجودة");
        std::string mode = czip::optStr(&it->second, "mode", "chain");
        const Value* fv = it->second.map->findValue(Value::string("fns"));
        auto callWith = [&](const Value& f, const std::vector<Value>& in) {
            std::vector<Value> args(in.begin(), in.end());
            size_t ar = arityOf(f);
            if (f.type == Value::Type::FUNCTION) args.resize(ar);   // يقصّ أو يملأ nil حسب وسائط الدالة نفسها
            return callValue(f, args, line);
        };
        std::vector<Value> in(a[1].array->begin(), a[1].array->end());
        Value last; ArrayData all;
        bool firstDone = false;
        for (size_t i = 0; i < fv->array->size(); ++i) {
            const Value& f = (*fv->array)[i];
            if (mode == "chain") { last = callWith(f, i == 0 ? in : std::vector<Value>{last}); }
            else {
                Value r = callWith(f, in);
                if (mode == "all") all.push_back(r);
                else if (mode == "first") { if (!firstDone && r.type != Value::Type::NIL) { last = r; firstDone = true; } }
                else last = r;
            }
        }
        return mode == "all" ? newArray(std::move(all)) : last;
    };
    natives["blendFn"] = [makeFnBlend](Args& a, int line) -> Value {
        if (a.size() < 2 || a[0].type != Value::Type::STRING) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendFn(mode, f, g, ...): mode = chain|all|first|each ثم دالتان على الأقل");
        const std::string& m = a[0].str;
        if (m != "chain" && m != "all" && m != "first" && m != "each") throw diagErr(diag::Code::E0035_RuntimeError, line, "blendFn: mode يجب أن يكون chain أو all أو first أو each");
        return makeFnBlend(m, std::vector<Value>(a.begin() + 1, a.end()), line);
    };

    auto runBlend = [this, makeFnBlend](const std::vector<Value>& vs, const GOpts& g, const std::string& who, int line) -> Value {
        bool anyFn = false, allFn = true; std::vector<Value> fns;
        for (const auto& v : vs) if (v.type != Value::Type::NIL) { if (v.type == Value::Type::FUNCTION) { anyFn = true; fns.push_back(v); } else allFn = false; }
        if (anyFn && allFn) return makeFnBlend(g.fnMode, fns, line);
        try { return blendValues(vs, g.pol, who); }
        catch (const std::runtime_error& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, e.what()); }
    };
    natives["blend"] = [runBlend](Args& a, int line) -> Value {
        if (a.empty()) throw diagErr(diag::Code::E0035_RuntimeError, line, "blend يحتاج قيمتين على الأقل");
        return runBlend(a, GOpts{}, "blend", line);
    };
    natives["blendWith"] = [runBlend, parseGOpts](Args& a, int line) -> Value {
        if (a.size() < 2) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendWith(opts, a, b, ...) يحتاج خيارات وقيمة على الأقل");
        GOpts g = parseGOpts(a[0], "blendWith", line);
        return runBlend(std::vector<Value>(a.begin() + 1, a.end()), g, "blendWith", line);
    };
    auto intoImpl = [this, runBlend](const GOpts& g, const Value* rawOpts, Args& a, int line) -> Value {
        if (a.size() < 2) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendInto(target, a, ...) يحتاج هدفاً ومصدراً");
        Value& t = a[0];
        std::vector<Value> srcs(a.begin() + 1, a.end());
        if (t.type == Value::Type::STRING) { // اسم حاوية → container.blend
            Args ca{t, srcs.size() == 1 ? srcs[0] : newArray(ArrayData(srcs.begin(), srcs.end()))};
            if (rawOpts) ca.push_back(*rawOpts);
            return natives["container.blend"](ca, line);
        }
        if (t.type == Value::Type::INSTANCE) {
            if (!t.instance) return t;
            if (t.instance->frozen) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendInto: الكائن مجمّد (oop.freeze)");
            std::vector<Value> all{t}; all.insert(all.end(), srcs.begin(), srcs.end());
            Value m = runBlend(all, g, "blendInto", line);
            for (const auto& kv : *m.map) {
                std::string k = kv.first.toDisplayString();
                if (!t.instance->fields.count(k)) t.instance->fieldOrder.push_back(k);
                t.instance->fields[k] = kv.second;
            }
            return t;
        }
        std::vector<Value> all{t}; all.insert(all.end(), srcs.begin(), srcs.end());
        Value r = runBlend(all, g, "blendInto", line);
        if (t.type == Value::Type::MAP && t.map) { *t.map = *r.map; return t; }
        if (t.type == Value::Type::ARRAY && t.array) { *t.array = *r.array; return t; }
        if (t.type == Value::Type::SET && t.set) { t.set->clear(); for (const auto& x : r.set->items) t.set->add(x); return t; }
        throw diagErr(diag::Code::E0004_InvalidType, line, std::string("blendInto: الهدف من نوع ") + kindName(t) + " لا يُعدَّل في المكان (استعمل blend)");
    };
    natives["blendInto"] = [intoImpl](Args& a, int line) -> Value { return intoImpl(GOpts{}, nullptr, a, line); };
    natives["blendIntoWith"] = [intoImpl, parseGOpts](Args& a, int line) -> Value {
        if (a.size() < 3) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendIntoWith(opts, target, a, ...) يحتاج خيارات وهدفاً ومصدراً");
        GOpts g = parseGOpts(a[0], "blendIntoWith", line);
        Value rawOpts = a[0];
        Args rest(a.begin() + 1, a.end());
        return intoImpl(g, rawOpts.type == Value::Type::MAP ? &rawOpts : nullptr, rest, line);
    };
    natives["blendRecipe"] = [this](Args& a, int line) -> Value {
        need("blendRecipe", a, 2, 2, line);
        std::string name = str(a[0], "blendRecipe", line);
        if (a[1].type != Value::Type::MAP || !a[1].map) throw diagErr(diag::Code::E0004_InvalidType, line, "blendRecipe: المواصفة map {parts, include?, policy?}");
        const Value* pr = a[1].map->findValue(Value::string("parts"));
        const Value* inc = a[1].map->findValue(Value::string("include"));
        if ((!pr || pr->type != Value::Type::ARRAY) && (!inc || inc->type == Value::Type::NIL))
            throw diagErr(diag::Code::E0035_RuntimeError, line, "blendRecipe: أعطِ parts (مصفوفة قيم) أو include (وصفات)");
        try { blendRecipes[name] = cloneV(a[1]); } catch (const std::runtime_error& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, e.what()); }
        return blendRecipes[name];
    };
    natives["blendCook"] = [this, runBlend, parseGOpts](Args& a, int line) -> Value {
        need("blendCook", a, 1, 1, line);
        std::string name = str(a[0], "blendCook", line);
        std::vector<Value> parts; std::vector<std::string> stack;
        std::function<void(const std::string&, int)> walk = [&](const std::string& rn, int depth) {
            if (depth > 16 || std::find(stack.begin(), stack.end(), rn) != stack.end())
                throw diagErr(diag::Code::E0035_RuntimeError, line, "blendCook: دوران أو عمق زائد في include عند '" + rn + "'");
            auto it = blendRecipes.find(rn);
            if (it == blendRecipes.end()) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendCook: الوصفة '" + rn + "' غير موجودة");
            stack.push_back(rn);
            Value spec = it->second;
            if (const Value* inc = spec.map->findValue(Value::string("include")); inc && inc->type != Value::Type::NIL)
                for (const auto& n : czip::strList(inc, "blendCook", "include", line)) walk(n, depth + 1);
            if (const Value* pr = spec.map->findValue(Value::string("parts")); pr && pr->type == Value::Type::ARRAY && pr->array)
                for (const auto& p : *pr->array) parts.push_back(cloneV(p));
            stack.pop_back();
        };
        walk(name, 0);
        GOpts g = parseGOpts(blendRecipes[name], "blendCook", line);
        return runBlend(parts, g, "blendCook", line);
    };

    // blendDiff(a, b) — ماذا سيتغيّر لو دُمجت b في a؟ مقارنة عميقة للـ map (والباقي بالقيمة):
    //   {equal, same, added:[{path,to}], removed:[{path,from}], changed:[{path,from,to}]}  — added = في b لا في a
    natives["blendDiff"] = [](Args& a, int line) -> Value {
        need("blendDiff", a, 2, 2, line);
        ArrayData added, removed, changed; size_t same = 0;
        std::function<void(const Value&, const Value&, const std::string&, int)> walk = [&](const Value& x, const Value& y, const std::string& path, int dep) {
            if (dep > 200) throw diagErr(diag::Code::E0035_RuntimeError, line, "blendDiff: بنية متداخلة أعمق من 200 مستوى");
            if (x.type == Value::Type::MAP && y.type == Value::Type::MAP && x.map && y.map) {
                for (const auto& kv : *x.map) {
                    std::string sub = path.empty() ? kv.first.toDisplayString() : path + "." + kv.first.toDisplayString();
                    if (const Value* o = y.map->findValue(kv.first)) walk(kv.second, *o, sub, dep + 1);
                    else removed.push_back(mkMap({{"path", strV(sub)}, {"from", kv.second}}));
                }
                for (const auto& kv : *y.map) if (!x.map->findValue(kv.first)) {
                    std::string sub = path.empty() ? kv.first.toDisplayString() : path + "." + kv.first.toDisplayString();
                    added.push_back(mkMap({{"path", strV(sub)}, {"to", kv.second}}));
                }
                return;
            }
            if (valuesEqual(x, y)) { ++same; return; }
            changed.push_back(mkMap({{"path", strV(path)}, {"from", x}, {"to", y}}));
        };
        walk(a[0], a[1], "", 0);
        bool eq = added.empty() && removed.empty() && changed.empty();
        return mkMap({{"equal", boolV(eq)}, {"same", numV(double(same))}, {"added", newArray(std::move(added))},
                      {"removed", newArray(std::move(removed))}, {"changed", newArray(std::move(changed))}});
    };

    // ======================================================================== الترتيب الشامل
    // compare(a, b, opts?) -> -1 | 0 | 1 : ترتيب كلي لأي قيمتين (أنواع مختلطة، مصفوفات، Set، map، كائنات)
    // orderBy(list, spec?, opts?) -> مصفوفة جديدة مرتّبة ثباتياً؛ spec: مسار "a.b" (بادئة "-" = تنازلي) | دالة مفتاح | مصفوفة مواصفات | {by, desc}
    //   opts: caseless · natural ("a2" < "a10") · nilsFirst (الافتراضي true) · desc · by (لـ compare)
    struct OSpec { std::string path; Value fn; bool desc = false; bool hasFn = false; };
    auto readCOpt = [](const Value* o, int line, const char* who, COpt& c, bool& desc) {
        if (!o || o->type == Value::Type::NIL) return;
        if (o->type != Value::Type::MAP || !o->map) throw diagErr(diag::Code::E0004_InvalidType, line, std::string(who) + ": الخيارات map");
        c.caseless = czip::optBool(o, "caseless", false); c.natural = czip::optBool(o, "natural", false);
        c.nilsFirst = czip::optBool(o, "nilsFirst", true); desc = czip::optBool(o, "desc", false);
    };
    auto readSpec = [](const Value& v, int line, const char* who, std::vector<OSpec>& out) {
        std::function<void(const Value&, int)> one = [&](const Value& x, int depth) {
            if (depth > 4) throw diagErr(diag::Code::E0004_InvalidType, line, std::string(who) + ": مواصفات الترتيب متداخلة أكثر من اللازم");
            OSpec sp;
            if (x.type == Value::Type::NIL) { out.push_back(sp); return; }          // العنصر نفسه
            if (x.type == Value::Type::STRING) { sp.path = x.str; if (!sp.path.empty() && sp.path[0] == '-') { sp.desc = true; sp.path.erase(0, 1); } out.push_back(sp); return; }
            if (x.type == Value::Type::FUNCTION) { sp.fn = x; sp.hasFn = true; out.push_back(sp); return; }
            if (x.type == Value::Type::MAP && x.map) {
                const Value* by = x.map->findValue(Value::string("by"));
                if (by && by->type == Value::Type::STRING) sp.path = by->str;
                else if (by && by->type == Value::Type::FUNCTION) { sp.fn = *by; sp.hasFn = true; }
                sp.desc = czip::optBool(&x, "desc", false); out.push_back(sp); return;
            }
            if (x.type == Value::Type::ARRAY && x.array) { for (const auto& y : *x.array) one(y, depth + 1); return; }
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string(who) + ": مواصفة ترتيب غير صالحة (مسار نصي أو دالة أو map أو مصفوفة)");
        };
        one(v, 0);
    };
    auto keyOf = [this](const OSpec& sp, const Value& el, int line) -> Value {
        if (sp.hasFn) { std::vector<Value> args{el}; return callValue(sp.fn, args, line); }
        return sp.path.empty() ? el : pathGet(el, sp.path);
    };
    natives["compare"] = [this, readCOpt, readSpec, keyOf](Args& a, int line) -> Value {
        need("compare", a, 2, 3, line);
        COpt c; bool desc = false;
        const Value* o = a.size() > 2 ? &a[2] : nullptr;
        readCOpt(o, line, "compare", c, desc);
        Value x = a[0], y = a[1];
        if (o && o->type == Value::Type::MAP && o->map) if (const Value* by = o->map->findValue(Value::string("by")); by && by->type != Value::Type::NIL) {
            std::vector<OSpec> sp; readSpec(*by, line, "compare", sp);
            if (!sp.empty()) { x = keyOf(sp[0], a[0], line); y = keyOf(sp[0], a[1], line); }
        }
        int r;
        if (x.type == Value::Type::INSTANCE || y.type == Value::Type::INSTANCE) { // نفس منطق المعامل <!=g=?>: __cmp__/__lt__/__gt__ أولاً
            if (auto lt = tryCompareOverload(TokenType::LESS, x, y, line)) {
                if (lt->isTruthy()) return Value::num(desc ? 1 : -1);
                auto gt = tryCompareOverload(TokenType::GREATER, x, y, line);
                int rr = (gt && gt->isTruthy()) ? 1 : 0;
                return Value::num(desc ? -rr : rr);
            }
        }
        try { r = cmpV(x, y, c); } catch (const std::runtime_error& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, e.what()); }
        return Value::num(desc ? -r : r);
    };
    natives["orderBy"] = [readCOpt, readSpec, keyOf](Args& a, int line) -> Value {
        need("orderBy", a, 1, 3, line);
        std::vector<Value> items;
        if (a[0].type == Value::Type::ARRAY && a[0].array) items.assign(a[0].array->begin(), a[0].array->end());
        else if (a[0].type == Value::Type::SET && a[0].set) items = a[0].set->items;
        else throw diagErr(diag::Code::E0004_InvalidType, line, "orderBy يتوقع مصفوفة أو Set");
        std::vector<OSpec> specs;
        if (a.size() > 1 && a[1].type != Value::Type::NIL) readSpec(a[1], line, "orderBy", specs);
        if (specs.empty()) specs.push_back(OSpec{});
        COpt c; bool desc = false;
        readCOpt(a.size() > 2 ? &a[2] : nullptr, line, "orderBy", c, desc);
        std::vector<std::vector<Value>> keys(items.size());
        for (size_t i = 0; i < items.size(); ++i) for (const auto& sp : specs) keys[i].push_back(keyOf(sp, items[i], line));
        std::vector<size_t> idx(items.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::string err;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t p, size_t q) {
            try {
                for (size_t k = 0; k < specs.size(); ++k) {
                    int r = cmpV(keys[p][k], keys[q][k], c);
                    if (specs[k].desc) r = -r;
                    if (desc) r = -r;
                    if (r) return r < 0;
                }
            } catch (const std::runtime_error& e) { err = e.what(); }
            return false;
        });
        if (!err.empty()) throw diagErr(diag::Code::E0035_RuntimeError, line, err);
        ArrayData out;
        for (size_t i : idx) out.push_back(items[i]);
        return newArray(std::move(out));
    };

    // ======================================================================== عائلة الترتيب الموسّعة
    // كلها مبنية على نفس المحرّك (cmpV) ونفس مواصفات orderBy: مسار "a.b" | دالة | مصفوفة | {by,desc}؛ opts: caseless/natural/nilsFirst/desc
    auto toItems = [](const Value& v, int line, const char* who) {
        std::vector<Value> items;
        if (v.type == Value::Type::ARRAY && v.array) items.assign(v.array->begin(), v.array->end());
        else if (v.type == Value::Type::SET && v.set) items = v.set->items;
        else throw diagErr(diag::Code::E0004_InvalidType, line, std::string(who) + " يتوقع مصفوفة أو Set");
        return items;
    };
    // مفاتيح عنصر واحد وفق المواصفات
    auto keysFor = [keyOf](const std::vector<OSpec>& specs, const Value& el, int line) {
        std::vector<Value> k;
        for (const auto& sp : specs) k.push_back(keyOf(sp, el, line));
        return k;
    };
    auto cmpKeys = [](const std::vector<OSpec>& specs, const std::vector<Value>& x, const std::vector<Value>& y, const COpt& c, bool desc, int line) {
        try {
            for (size_t k = 0; k < specs.size(); ++k) {
                int r = cmpV(x[k], y[k], c);
                if (specs[k].desc) r = -r;
                if (desc) r = -r;
                if (r) return r;
            }
        } catch (const std::runtime_error& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, e.what()); }
        return 0;
    };
    auto specsOf = [readSpec](const Value* v, int line, const char* who) {
        std::vector<OSpec> specs;
        if (v && v->type != Value::Type::NIL) readSpec(*v, line, who, specs);
        if (specs.empty()) specs.push_back(OSpec{});
        return specs;
    };

    // relate(a, b, opts?) -> {order, relation, less, equal, greater, identical, sameKind}
    //   equal = ترتيبياً (order==0)؛ identical = تساوي القيم الحرفي valuesEqual (1 و 1.0 متساويان، "a" و "A" لا — ما لم caseless)
    natives["relate"] = [this, readCOpt](Args& a, int line) -> Value {
        need("relate", a, 2, 3, line);
        COpt c; bool desc = false;
        readCOpt(a.size() > 2 ? &a[2] : nullptr, line, "relate", c, desc);
        int r;
        bool done = false; r = 0;
        if (a[0].type == Value::Type::INSTANCE || a[1].type == Value::Type::INSTANCE) {
            if (auto lt = tryCompareOverload(TokenType::LESS, a[0], a[1], line)) {
                if (lt->isTruthy()) r = -1;
                else { auto gt = tryCompareOverload(TokenType::GREATER, a[0], a[1], line); r = (gt && gt->isTruthy()) ? 1 : 0; }
                done = true;
            }
        }
        if (!done) { try { r = cmpV(a[0], a[1], c); } catch (const std::runtime_error& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, e.what()); } }
        if (desc) r = -r;
        const char* rel = r < 0 ? "less" : (r > 0 ? "greater" : "equal");
        return mkMap({{"order", numV(r)}, {"relation", strV(rel)}, {"less", boolV(r < 0)}, {"equal", boolV(r == 0)},
                      {"greater", boolV(r > 0)}, {"identical", boolV(valuesEqual(a[0], a[1]))},
                      {"sameKind", boolV(kindName(a[0]) == kindName(a[1]))}});
    };

    // inRange(x, lo, hi, opts?) -> bool  (مغلق على الطرفين؛ opts.exclusive لاستبعادهما؛ lo/hi يُعكسان تلقائياً إن كان lo>hi)
    natives["inRange"] = [this, readCOpt](Args& a, int line) -> Value {
        need("inRange", a, 3, 4, line);
        COpt c; bool desc = false;
        readCOpt(a.size() > 3 ? &a[3] : nullptr, line, "inRange", c, desc);
        bool excl = a.size() > 3 && a[3].type == Value::Type::MAP ? czip::optBool(&a[3], "exclusive", false) : false;
        try {
            Value lo = a[1], hi = a[2];
            if (cmpV(lo, hi, c) > 0) std::swap(lo, hi);
            int l = cmpV(a[0], lo, c), h = cmpV(a[0], hi, c);
            return Value::boolean_(excl ? (l > 0 && h < 0) : (l >= 0 && h <= 0));
        } catch (const std::runtime_error& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, e.what()); }
    };

    // isSorted(list, spec?, opts?) -> bool ؛ opts.strict: بلا تكرار
    natives["isSorted"] = [readCOpt, toItems, keysFor, cmpKeys, specsOf](Args& a, int line) -> Value {
        need("isSorted", a, 1, 3, line);
        auto items = toItems(a[0], line, "isSorted");
        auto specs = specsOf(a.size() > 1 ? &a[1] : nullptr, line, "isSorted");
        COpt c; bool desc = false;
        readCOpt(a.size() > 2 ? &a[2] : nullptr, line, "isSorted", c, desc);
        bool strict = a.size() > 2 && a[2].type == Value::Type::MAP ? czip::optBool(&a[2], "strict", false) : false;
        std::vector<Value> prev;
        for (size_t i = 0; i < items.size(); ++i) {
            auto k = keysFor(specs, items[i], line);
            if (i) { int r = cmpKeys(specs, prev, k, c, desc, line); if (r > 0 || (strict && r == 0)) return Value::boolean_(false); }
            prev = std::move(k);
        }
        return Value::boolean_(true);
    };

    // sortedIndex(list, value, spec?, opts?) -> موضع الإدراج (بحث ثنائي؛ القائمة يجب أن تكون مرتّبة). opts.right: بعد المتساويات
    natives["sortedIndex"] = [readCOpt, toItems, keysFor, cmpKeys, specsOf](Args& a, int line) -> Value {
        need("sortedIndex", a, 2, 4, line);
        auto items = toItems(a[0], line, "sortedIndex");
        auto specs = specsOf(a.size() > 2 ? &a[2] : nullptr, line, "sortedIndex");
        COpt c; bool desc = false;
        readCOpt(a.size() > 3 ? &a[3] : nullptr, line, "sortedIndex", c, desc);
        bool right = a.size() > 3 && a[3].type == Value::Type::MAP ? czip::optBool(&a[3], "right", false) : false;
        auto target = keysFor(specs, a[1], line);
        size_t lo = 0, hi = items.size();
        while (lo < hi) {
            size_t mid = (lo + hi) / 2;
            int r = cmpKeys(specs, keysFor(specs, items[mid], line), target, c, desc, line);
            if (right ? (r <= 0) : (r < 0)) lo = mid + 1; else hi = mid;
        }
        return Value::num(double(lo));
    };

    // mergeSorted(lists, spec?, opts?) -> دمج ثباتي لقوائم مرتّبة (التعادل: القائمة الأولى أولاً) — دمج مفاهيم الترتيب مع blend
    natives["mergeSorted"] = [readCOpt, toItems, keysFor, cmpKeys, specsOf](Args& a, int line) -> Value {
        need("mergeSorted", a, 1, 3, line);
        if (a[0].type != Value::Type::ARRAY || !a[0].array) throw diagErr(diag::Code::E0004_InvalidType, line, "mergeSorted يتوقع مصفوفة من القوائم");
        std::vector<std::vector<Value>> lists;
        for (const auto& l : *a[0].array) lists.push_back(toItems(l, line, "mergeSorted"));
        auto specs = specsOf(a.size() > 1 ? &a[1] : nullptr, line, "mergeSorted");
        COpt c; bool desc = false;
        readCOpt(a.size() > 2 ? &a[2] : nullptr, line, "mergeSorted", c, desc);
        std::vector<size_t> pos(lists.size(), 0);
        std::vector<std::vector<Value>> head(lists.size());
        auto load = [&](size_t i) { if (pos[i] < lists[i].size()) head[i] = keysFor(specs, lists[i][pos[i]], line); };
        for (size_t i = 0; i < lists.size(); ++i) load(i);
        ArrayData out;
        for (;;) {
            int best = -1;
            for (size_t i = 0; i < lists.size(); ++i) {
                if (pos[i] >= lists[i].size()) continue;
                if (best < 0 || cmpKeys(specs, head[i], head[size_t(best)], c, desc, line) < 0) best = int(i);
            }
            if (best < 0) break;
            out.push_back(lists[size_t(best)][pos[size_t(best)]]);
            ++pos[size_t(best)]; load(size_t(best));
        }
        return newArray(std::move(out));
    };

    // rank(list, spec?, opts?) -> رتبة كل عنصر بترتيب القائمة الأصلي (1 = الأصغر؛ المتعادلون يتشاركون أدنى رتبة 1,2,2,4). opts.desc: 1 = الأكبر
    natives["rank"] = [readCOpt, toItems, keysFor, cmpKeys, specsOf](Args& a, int line) -> Value {
        need("rank", a, 1, 3, line);
        auto items = toItems(a[0], line, "rank");
        auto specs = specsOf(a.size() > 1 ? &a[1] : nullptr, line, "rank");
        COpt c; bool desc = false;
        readCOpt(a.size() > 2 ? &a[2] : nullptr, line, "rank", c, desc);
        std::vector<std::vector<Value>> keys;
        for (const auto& it : items) keys.push_back(keysFor(specs, it, line));
        std::vector<size_t> idx(items.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t p, size_t q) { return cmpKeys(specs, keys[p], keys[q], c, desc, line) < 0; });
        std::vector<double> rk(items.size(), 0);
        for (size_t i = 0; i < idx.size(); ++i)
            rk[idx[i]] = (i > 0 && cmpKeys(specs, keys[idx[i - 1]], keys[idx[i]], c, desc, line) == 0) ? rk[idx[i - 1]] : double(i + 1);
        ArrayData out;
        for (double r : rk) out.push_back(Value::num(r));
        return newArray(std::move(out));
    };

    // topN(list, n, spec?, opts?) -> أكبر n عنصراً (مرتّبة تنازلياً)؛ opts.desc=true يعكس الاتجاه فتصير "أصغر n". n<=0 → [] ؛ n>الطول → الكل
    natives["topN"] = [readCOpt, toItems, keysFor, cmpKeys, specsOf](Args& a, int line) -> Value {
        need("topN", a, 2, 4, line);
        auto items = toItems(a[0], line, "topN");
        if (a[1].type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, "topN: n رقم");
        auto specs = specsOf(a.size() > 2 ? &a[2] : nullptr, line, "topN");
        COpt c; bool flip = false;
        readCOpt(a.size() > 3 ? &a[3] : nullptr, line, "topN", c, flip);
        size_t n = a[1].number <= 0 ? 0 : size_t(a[1].number);
        std::vector<std::vector<Value>> keys;
        for (const auto& it : items) keys.push_back(keysFor(specs, it, line));
        std::vector<size_t> idx(items.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        // الافتراضي: الأكبر أولاً ⇒ ترتيب تنازلي؛ flip يعكسه
        std::stable_sort(idx.begin(), idx.end(), [&](size_t p, size_t q) { return cmpKeys(specs, keys[p], keys[q], c, !flip, line) < 0; });
        ArrayData out;
        for (size_t i = 0; i < idx.size() && i < n; ++i) out.push_back(items[idx[i]]);
        return newArray(std::move(out));
    };

    // uniqueBy(list, spec?, opts?) -> أول عنصر من كل مجموعة متعادلة ترتيبياً، مع حفظ ترتيب الظهور الأصلي
    natives["uniqueBy"] = [readCOpt, toItems, keysFor, cmpKeys, specsOf](Args& a, int line) -> Value {
        need("uniqueBy", a, 1, 3, line);
        auto items = toItems(a[0], line, "uniqueBy");
        auto specs = specsOf(a.size() > 1 ? &a[1] : nullptr, line, "uniqueBy");
        COpt c; bool desc = false;
        readCOpt(a.size() > 2 ? &a[2] : nullptr, line, "uniqueBy", c, desc);
        std::vector<std::vector<Value>> keys;
        for (const auto& it : items) keys.push_back(keysFor(specs, it, line));
        std::vector<size_t> idx(items.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
        std::stable_sort(idx.begin(), idx.end(), [&](size_t p, size_t q) { return cmpKeys(specs, keys[p], keys[q], c, false, line) < 0; });
        std::vector<char> keep(items.size(), 0);
        for (size_t i = 0; i < idx.size(); ++i)
            if (i == 0 || cmpKeys(specs, keys[idx[i - 1]], keys[idx[i]], c, false, line) != 0) keep[idx[i]] = 1;
        ArrayData out;
        for (size_t i = 0; i < items.size(); ++i) if (keep[i]) out.push_back(items[i]);
        return newArray(std::move(out));
    };
}

} // namespace rin

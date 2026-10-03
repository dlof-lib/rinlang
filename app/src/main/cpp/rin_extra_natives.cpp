// ============================================================================
//  rin_extra_natives.cpp — Rin 1.0 native library additions
// ----------------------------------------------------------------------------
//  يضيف دوال أصلية جديدة دون لمس منطق المفسّر الأساسي. تُسجَّل من نهاية
//  Interpreter::registerNatives() عبر registerNativesExtra().
//
//  ملاحظة بناء: هذا الملف يُضمَّن (#include) في نهاية rin_interpreter.cpp، فيدخل تلقائياً
//  في كل أهداف البناء (APK/CLI/WASM/CI) دون تعديل قوائم الملفات. للتجميع المنفصل أثناء
//  التطوير عرّف RIN_EXTRA_NATIVES_SEPARATE عند ترجمة rin_interpreter.cpp.
//
//  المجموعات:
//    1) فحص الأنواع   : isNil isNumber isString isArray isMap isFunction
//    2) الاختبار      : assert assertEq fail
//    3) النصوص        : lastIndexOf padStart padEnd repeat trimStart trimEnd
//    4) المصفوفات     : slice reverse concat flatten
//    5) القواميس      : entries fromEntries mergeMaps deepMerge pickKeys omitKeys
//                       getPath setPath deepCopy
//    6) الحاويات      : container.clone/rename/ensure/each/update/incr/append/
//                       getOr/pick/omit/toJson/fromJson/diff/pluck/sum/groupBy/
//                       query/tree
// ============================================================================
#include "rin_interpreter.h"
#include "rin_json.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <sstream>

namespace rin {

RinError diagErr(diag::Code code, int line, std::string message);

namespace extra {

using Args = std::vector<Value>;

// ---------------------------------------------------------------- arg helpers
void need(const std::string& fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi) {
        std::string range = (lo == hi) ? std::to_string(lo) : (std::to_string(lo) + " to " + std::to_string(hi));
        throw diagErr(diag::Code::E0007_InvalidArguments, line,
                      "'" + fn + "' expects " + range + " argument(s) but got " + std::to_string(a.size()));
    }
}
std::string str(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::STRING)
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a string but got " + v.typeName());
    return v.str;
}
double num(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::NUMBER)
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a number but got " + v.typeName());
    return v.number;
}
const ArrayData& arr(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::ARRAY || !v.array)
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects an array but got " + v.typeName());
    return *v.array;
}
const MapData& mp(const Value& v, const std::string& fn, int line) {
    if (v.type != Value::Type::MAP || !v.map)
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a map but got " + v.typeName());
    return *v.map;
}
Value newArray(ArrayData items = {}) { return Value::makeArray(std::make_shared<ArrayData>(std::move(items))); }
Value newMap(MapData items = {}) { return Value::makeMap(std::make_shared<MapData>(std::move(items))); }

// ------------------------------------------------------------------ map utils
const Value* mapFind(const MapData& m, const Value& key) {
    for (const auto& kv : m) if (valuesEqual(kv.first, key)) return &kv.second;
    return nullptr;
}
void mapSet(MapData& m, const Value& key, const Value& value) {
    for (auto& kv : m) if (valuesEqual(kv.first, key)) { kv.second = value; return; }
    m.push_back({key, value});
}

Value deepCopyValue(const Value& v, int depth = 0) {
    if (depth > 200) return v;
    if (v.type == Value::Type::ARRAY && v.array) {
        auto out = std::make_shared<ArrayData>();
        out->reserve(v.array->size());
        for (const auto& x : *v.array) out->push_back(deepCopyValue(x, depth + 1));
        return Value::makeArray(out);
    }
    if (v.type == Value::Type::MAP && v.map) {
        auto out = std::make_shared<MapData>();
        out->reserve(v.map->size());
        for (const auto& kv : *v.map) out->push_back({kv.first, deepCopyValue(kv.second, depth + 1)});
        return Value::makeMap(out);
    }
    return v;
}

Value deepMergeValue(const Value& a, const Value& b, int depth = 0) {
    if (depth < 100 && a.type == Value::Type::MAP && b.type == Value::Type::MAP && a.map && b.map) {
        auto out = std::make_shared<MapData>();
        for (const auto& kv : *a.map) out->push_back({kv.first, deepCopyValue(kv.second)});
        for (const auto& kv : *b.map) {
            const Value* have = mapFind(*out, kv.first);
            if (have) mapSet(*out, kv.first, deepMergeValue(*have, kv.second, depth + 1));
            else out->push_back({kv.first, deepCopyValue(kv.second)});
        }
        return Value::makeMap(out);
    }
    return deepCopyValue(b);
}

// --------------------------------------------------------------- utf-8 utils
std::vector<std::string> codePoints(const std::string& s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t n = (c < 0x80) ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        if (i + n > s.size()) n = 1;
        out.push_back(s.substr(i, n));
        i += n;
    }
    return out;
}
std::string joinCodePoints(const std::vector<std::string>& cps, size_t from, size_t to) {
    std::string out;
    for (size_t i = from; i < to && i < cps.size(); ++i) out += cps[i];
    return out;
}
// يحوّل فهرساً (قد يكون سالباً) إلى موضع صالح في [0, n]
size_t clampIndex(double d, size_t n) {
    long long i = static_cast<long long>(std::floor(d));
    if (i < 0) i += static_cast<long long>(n);
    if (i < 0) i = 0;
    if (i > static_cast<long long>(n)) i = static_cast<long long>(n);
    return static_cast<size_t>(i);
}

// ------------------------------------------------------------------ path utils
std::vector<Value> parsePath(const Value& p, const std::string& fn, int line) {
    std::vector<Value> out;
    if (p.type == Value::Type::STRING) {
        std::string cur;
        for (char c : p.str) {
            if (c == '.') { if (!cur.empty()) out.push_back(Value::string(cur)); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) out.push_back(Value::string(cur));
    } else if (p.type == Value::Type::ARRAY && p.array) {
        out = *p.array;
    } else if (p.type == Value::Type::NUMBER) {
        out.push_back(p);
    } else {
        throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' expects a path string or array but got " + p.typeName());
    }
    return out;
}
bool asIndex(const Value& k, long long& out) {
    if (k.type == Value::Type::NUMBER) { out = static_cast<long long>(k.number); return true; }
    if (k.type == Value::Type::STRING && !k.str.empty()) {
        size_t i = (k.str[0] == '-') ? 1 : 0;
        if (i >= k.str.size()) return false;
        for (size_t j = i; j < k.str.size(); ++j) if (!std::isdigit(static_cast<unsigned char>(k.str[j]))) return false;
        out = std::stoll(k.str);
        return true;
    }
    return false;
}

// ------------------------------------------------------------ container utils
bool matchCondition(const Value* have, const Value& cond) {
    if (cond.type != Value::Type::MAP || !cond.map) {
        return have && valuesEqual(*have, cond);
    }
    for (const auto& op : *cond.map) {
        std::string name = op.first.toDisplayString();
        const Value& want = op.second;
        if (name == "exists") { if ((have != nullptr) != want.isTruthy()) return false; continue; }
        if (name == "ne")     { if (have && valuesEqual(*have, want)) return false; continue; }
        if (!have) return false;
        if (name == "eq")  { if (!valuesEqual(*have, want)) return false; }
        else if (name == "gt" || name == "gte" || name == "lt" || name == "lte") {
            int cmp;
            if (have->type == Value::Type::NUMBER && want.type == Value::Type::NUMBER)
                cmp = have->number < want.number ? -1 : have->number > want.number ? 1 : 0;
            else if (have->type == Value::Type::STRING && want.type == Value::Type::STRING)
                cmp = have->str < want.str ? -1 : have->str > want.str ? 1 : 0;
            else return false;
            if (name == "gt" && !(cmp > 0)) return false;
            if (name == "gte" && !(cmp >= 0)) return false;
            if (name == "lt" && !(cmp < 0)) return false;
            if (name == "lte" && !(cmp <= 0)) return false;
        }
        else if (name == "in") {
            if (want.type != Value::Type::ARRAY || !want.array) return false;
            bool found = false;
            for (const auto& x : *want.array) if (valuesEqual(x, *have)) { found = true; break; }
            if (!found) return false;
        }
        else if (name == "contains") {
            if (have->type == Value::Type::STRING && want.type == Value::Type::STRING) {
                if (have->str.find(want.str) == std::string::npos) return false;
            } else if (have->type == Value::Type::ARRAY && have->array) {
                bool found = false;
                for (const auto& x : *have->array) if (valuesEqual(x, want)) { found = true; break; }
                if (!found) return false;
            } else return false;
        }
        else if (name == "startsWith") {
            if (have->type != Value::Type::STRING || want.type != Value::Type::STRING) return false;
            if (have->str.rfind(want.str, 0) != 0) return false;
        }
        else if (name == "endsWith") {
            if (have->type != Value::Type::STRING || want.type != Value::Type::STRING) return false;
            if (have->str.size() < want.str.size() ||
                have->str.compare(have->str.size() - want.str.size(), want.str.size(), want.str) != 0) return false;
        }
        else {
            // عامل غير معروف: فشل صريح بدل تجاهل صامت
            return false;
        }
    }
    return true;
}

} // namespace extra

using namespace extra;

// ============================================================================
void Interpreter::registerNativesExtra() {

    // kind display name of a container (nil if missing) — reuses the existing native
    auto kindOf = [this](const std::string& name) -> std::string {
        Args args{Value::string(name)};
        Value k = natives["container.kind"](args, 0);
        return k.type == Value::Type::STRING ? k.str : std::string();
    };
    auto sortedNames = [this]() {
        std::vector<std::string> names;
        names.reserve(containers.size());
        for (const auto& kv : containers) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        return names;
    };
    auto kindMatches = [kindOf](const std::string& name, const std::string& kind) {
        return kind.empty() || kind == "*" || kindOf(name) == kind;
    };

    // ------------------------------------------------------------ 1) types
    natives["isNil"]      = [](Args& a, int line) -> Value { need("isNil", a, 1, 1, line);      return Value::boolean_(a[0].type == Value::Type::NIL); };
    natives["isNumber"]   = [](Args& a, int line) -> Value { need("isNumber", a, 1, 1, line);   return Value::boolean_(a[0].type == Value::Type::NUMBER); };
    natives["isString"]   = [](Args& a, int line) -> Value { need("isString", a, 1, 1, line);   return Value::boolean_(a[0].type == Value::Type::STRING); };
    natives["isArray"]    = [](Args& a, int line) -> Value { need("isArray", a, 1, 1, line);    return Value::boolean_(a[0].type == Value::Type::ARRAY); };
    natives["isMap"]      = [](Args& a, int line) -> Value { need("isMap", a, 1, 1, line);      return Value::boolean_(a[0].type == Value::Type::MAP); };
    natives["isFunction"] = [](Args& a, int line) -> Value { need("isFunction", a, 1, 1, line); return Value::boolean_(a[0].type == Value::Type::FUNCTION); };

    // ---------------------------------------------------------- 2) testing
    natives["fail"] = [](Args& a, int line) -> Value {
        need("fail", a, 0, 1, line);
        throw diagErr(diag::Code::E0035_RuntimeError, line, a.empty() ? std::string("fail() called") : a[0].toDisplayString());
    };
    natives["assert"] = [](Args& a, int line) -> Value {
        need("assert", a, 1, 2, line);
        if (!a[0].isTruthy())
            throw diagErr(diag::Code::E0035_RuntimeError, line,
                          "assertion failed" + (a.size() > 1 ? ": " + a[1].toDisplayString() : std::string()));
        return Value::boolean_(true);
    };
    natives["assertEq"] = [](Args& a, int line) -> Value {
        need("assertEq", a, 2, 3, line);
        if (!valuesEqual(a[0], a[1]))
            throw diagErr(diag::Code::E0035_RuntimeError, line,
                          "assertEq failed: expected " + a[1].toDisplayString() + " but got " + a[0].toDisplayString() +
                          (a.size() > 2 ? " (" + a[2].toDisplayString() + ")" : std::string()));
        return Value::boolean_(true);
    };

    // ----------------------------------------------------------- 3) strings
    natives["lastIndexOf"] = [](Args& a, int line) -> Value {
        need("lastIndexOf", a, 2, 2, line);
        std::string s = str(a[0], "lastIndexOf", line), sub = str(a[1], "lastIndexOf", line);
        size_t pos = s.rfind(sub);
        return Value::num(pos == std::string::npos ? -1.0 : static_cast<double>(pos));
    };
    auto padImpl = [](const std::string& fn, Args& a, int line, bool start) -> Value {
        need(fn, a, 2, 3, line);
        std::string s = str(a[0], fn, line);
        double target = num(a[1], fn, line);
        std::string pad = a.size() > 2 ? str(a[2], fn, line) : " ";
        auto have = codePoints(s).size();
        if (pad.empty() || target <= static_cast<double>(have)) return Value::string(s);
        if (target > 10000000.0) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "' target length is too large");
        size_t need_ = static_cast<size_t>(target) - have;
        auto padCps = codePoints(pad);
        std::string fill;
        for (size_t i = 0; i < need_; ++i) fill += padCps[i % padCps.size()];
        return Value::string(start ? fill + s : s + fill);
    };
    natives["padStart"] = [padImpl](Args& a, int line) -> Value { return padImpl("padStart", a, line, true); };
    natives["padEnd"]   = [padImpl](Args& a, int line) -> Value { return padImpl("padEnd", a, line, false); };
    natives["repeat"] = [](Args& a, int line) -> Value {
        need("repeat", a, 2, 2, line);
        std::string s = str(a[0], "repeat", line);
        double n = num(a[1], "repeat", line);
        if (n < 0) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'repeat' count must not be negative");
        if (static_cast<double>(s.size()) * n > 10000000.0)
            throw diagErr(diag::Code::E0007_InvalidArguments, line, "'repeat' result would be too large");
        std::string out;
        out.reserve(s.size() * static_cast<size_t>(n));
        for (long long i = 0; i < static_cast<long long>(n); ++i) out += s;
        return Value::string(out);
    };
    natives["trimStart"] = [](Args& a, int line) -> Value {
        need("trimStart", a, 1, 1, line);
        std::string s = str(a[0], "trimStart", line);
        size_t i = s.find_first_not_of(" \t\r\n");
        return Value::string(i == std::string::npos ? std::string() : s.substr(i));
    };
    natives["trimEnd"] = [](Args& a, int line) -> Value {
        need("trimEnd", a, 1, 1, line);
        std::string s = str(a[0], "trimEnd", line);
        size_t i = s.find_last_not_of(" \t\r\n");
        return Value::string(i == std::string::npos ? std::string() : s.substr(0, i + 1));
    };

    // ---------------------------------------------------------- 4) arrays
    // slice(arrayOrString, start, end?) — فهارس سالبة تُعدّ من النهاية؛ النص يُقطَّع حسب محارف UTF-8.
    natives["slice"] = [](Args& a, int line) -> Value {
        need("slice", a, 2, 3, line);
        double s = num(a[1], "slice", line);
        if (a[0].type == Value::Type::STRING) {
            auto cps = codePoints(a[0].str);
            size_t from = clampIndex(s, cps.size());
            size_t to = a.size() > 2 ? clampIndex(num(a[2], "slice", line), cps.size()) : cps.size();
            return Value::string(from < to ? joinCodePoints(cps, from, to) : std::string());
        }
        const ArrayData& src = arr(a[0], "slice", line);
        size_t from = clampIndex(s, src.size());
        size_t to = a.size() > 2 ? clampIndex(num(a[2], "slice", line), src.size()) : src.size();
        ArrayData out;
        for (size_t i = from; i < to; ++i) out.push_back(src[i]);
        return newArray(std::move(out));
    };
    natives["reverse"] = [](Args& a, int line) -> Value {
        need("reverse", a, 1, 1, line);
        if (a[0].type == Value::Type::STRING) {
            auto cps = codePoints(a[0].str);
            std::reverse(cps.begin(), cps.end());
            return Value::string(joinCodePoints(cps, 0, cps.size()));
        }
        ArrayData out = arr(a[0], "reverse", line);
        std::reverse(out.begin(), out.end());
        return newArray(std::move(out));
    };
    natives["concat"] = [](Args& a, int line) -> Value {
        ArrayData out;
        for (const auto& x : a) {
            if (x.type == Value::Type::ARRAY && x.array) for (const auto& e : *x.array) out.push_back(e);
            else out.push_back(x);
        }
        (void)line;
        return newArray(std::move(out));
    };
    natives["flatten"] = [](Args& a, int line) -> Value {
        need("flatten", a, 1, 2, line);
        arr(a[0], "flatten", line);
        double d = a.size() > 1 ? num(a[1], "flatten", line) : 1.0;
        int depth = d < 0 ? 64 : static_cast<int>(std::min(d, 64.0));
        std::function<void(const ArrayData&, int, ArrayData&)> walk =
            [&](const ArrayData& in, int left, ArrayData& out) {
                for (const auto& x : in) {
                    if (left > 0 && x.type == Value::Type::ARRAY && x.array) walk(*x.array, left - 1, out);
                    else out.push_back(x);
                }
            };
        ArrayData out;
        walk(*a[0].array, depth, out);
        return newArray(std::move(out));
    };

    // ---------------------------------------------------------- 5) maps
    natives["entries"] = [](Args& a, int line) -> Value {
        need("entries", a, 1, 1, line);
        ArrayData out;
        for (const auto& kv : mp(a[0], "entries", line)) out.push_back(newArray({kv.first, kv.second}));
        return newArray(std::move(out));
    };
    natives["fromEntries"] = [](Args& a, int line) -> Value {
        need("fromEntries", a, 1, 1, line);
        MapData out;
        for (const auto& e : arr(a[0], "fromEntries", line)) {
            if (e.type != Value::Type::ARRAY || !e.array || e.array->size() != 2)
                throw diagErr(diag::Code::E0004_InvalidType, line, "'fromEntries' expects an array of [key, value] pairs");
            mapSet(out, (*e.array)[0], (*e.array)[1]);
        }
        return newMap(std::move(out));
    };
    natives["mergeMaps"] = [](Args& a, int line) -> Value {
        MapData out;
        for (const auto& m : a)
            for (const auto& kv : mp(m, "mergeMaps", line)) mapSet(out, kv.first, kv.second);
        return newMap(std::move(out));
    };
    natives["deepMerge"] = [](Args& a, int line) -> Value {
        need("deepMerge", a, 2, 2, line);
        mp(a[0], "deepMerge", line); mp(a[1], "deepMerge", line);
        return deepMergeValue(a[0], a[1]);
    };
    natives["deepCopy"] = [](Args& a, int line) -> Value {
        need("deepCopy", a, 1, 1, line);
        return deepCopyValue(a[0]);
    };
    natives["pickKeys"] = [](Args& a, int line) -> Value {
        need("pickKeys", a, 2, 2, line);
        const MapData& m = mp(a[0], "pickKeys", line);
        MapData out;
        for (const auto& k : arr(a[1], "pickKeys", line)) {
            const Value* v = mapFind(m, k);
            if (v) mapSet(out, k, *v);
        }
        return newMap(std::move(out));
    };
    natives["omitKeys"] = [](Args& a, int line) -> Value {
        need("omitKeys", a, 2, 2, line);
        const MapData& m = mp(a[0], "omitKeys", line);
        const ArrayData& drop = arr(a[1], "omitKeys", line);
        MapData out;
        for (const auto& kv : m) {
            bool skip = false;
            for (const auto& k : drop) if (valuesEqual(k, kv.first)) { skip = true; break; }
            if (!skip) out.push_back(kv);
        }
        return newMap(std::move(out));
    };
    // getPath(value, "a.b.0", default?) — مسار نصي بنقاط أو مصفوفة مفاتيح؛ يعيد default (أو nil) عند أي غياب.
    natives["getPath"] = [](Args& a, int line) -> Value {
        need("getPath", a, 2, 3, line);
        Value fallback = a.size() > 2 ? a[2] : Value::nil();
        Value cur = a[0];
        for (const auto& step : parsePath(a[1], "getPath", line)) {
            if (cur.type == Value::Type::MAP && cur.map) {
                const Value* v = mapFind(*cur.map, step);
                if (!v && step.type != Value::Type::STRING) v = mapFind(*cur.map, Value::string(step.toDisplayString()));
                if (!v) return fallback;
                cur = *v;
            } else if (cur.type == Value::Type::ARRAY && cur.array) {
                long long i;
                if (!asIndex(step, i)) return fallback;
                if (i < 0) i += static_cast<long long>(cur.array->size());
                if (i < 0 || i >= static_cast<long long>(cur.array->size())) return fallback;
                cur = (*cur.array)[static_cast<size_t>(i)];
            } else {
                return fallback;
            }
        }
        return cur;
    };
    // setPath(value, path, newValue) — يعدّل في المكان وينشئ القواميس الوسيطة المفقودة؛ يعيد القيمة الجذرية.
    natives["setPath"] = [](Args& a, int line) -> Value {
        need("setPath", a, 3, 3, line);
        auto steps = parsePath(a[1], "setPath", line);
        if (steps.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'setPath' needs a non-empty path");
        if (a[0].type != Value::Type::MAP && a[0].type != Value::Type::ARRAY)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'setPath' expects a map or array but got " + a[0].typeName());
        Value cur = a[0];
        for (size_t i = 0; i < steps.size(); ++i) {
            bool last = (i + 1 == steps.size());
            const Value& step = steps[i];
            if (cur.type == Value::Type::MAP && cur.map) {
                if (last) { mapSet(*cur.map, step, a[2]); break; }
                const Value* nxt = mapFind(*cur.map, step);
                if (!nxt || (nxt->type != Value::Type::MAP && nxt->type != Value::Type::ARRAY)) {
                    mapSet(*cur.map, step, newMap());
                    nxt = mapFind(*cur.map, step);
                }
                cur = *nxt;
            } else if (cur.type == Value::Type::ARRAY && cur.array) {
                long long idx;
                if (!asIndex(step, idx))
                    throw diagErr(diag::Code::E0004_InvalidType, line, "'setPath' needs a numeric index to step into an array");
                if (idx < 0) idx += static_cast<long long>(cur.array->size());
                if (idx < 0 || idx >= static_cast<long long>(cur.array->size()))
                    throw diagErr(diag::Code::E0007_InvalidArguments, line, "'setPath' array index out of range");
                if (last) { (*cur.array)[static_cast<size_t>(idx)] = a[2]; break; }
                Value nxt = (*cur.array)[static_cast<size_t>(idx)];
                if (nxt.type != Value::Type::MAP && nxt.type != Value::Type::ARRAY)
                    throw diagErr(diag::Code::E0004_InvalidType, line, "'setPath' cannot step through a " + nxt.typeName());
                cur = nxt;
            } else {
                throw diagErr(diag::Code::E0004_InvalidType, line, "'setPath' cannot step through a " + cur.typeName());
            }
        }
        return a[0];
    };

    // --------------------------------------------------------- 6) containers
    natives["container.getOr"] = [this](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.getOr", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.getOr", a, 3, 3, line);
        auto it = containers.find(str(a[0], "container.getOr", line));
        if (it == containers.end()) return a[2];
        Value out;
        return it->second->get(str(a[1], "container.getOr", line), out) ? out : a[2];
    };

    // container.ensure(kind, name) — ينشئ الحاوية فقط إن لم تكن موجودة، ويعيد اسمها دائماً.
    natives["container.ensure"] = [this](Args& a, int line) -> Value {
        need("container.ensure", a, 2, 2, line);
        std::string name = str(a[1], "container.ensure", line);
        if (containers.count(name)) return Value::string(name);
        Args spawnArgs{a[0], a[1]};
        return natives["spawn"](spawnArgs, line);
    };

    // container.clone(src, dst) — نسخة عميقة من الحقول والنوع (بلا الأبناء ولا الـhooks).
    natives["container.clone"] = [this](Args& a, int line) -> Value {
        need("container.clone", a, 2, 2, line);
        std::string src = str(a[0], "container.clone", line), dst = str(a[1], "container.clone", line);
        auto it = containers.find(src);
        if (it == containers.end() || src == dst || containers.count(dst)) return Value::boolean_(false);
        auto env = std::make_shared<Environment>(globals);
        for (const auto& kv : it->second->values) env->values[kv.first] = deepCopyValue(kv.second);
        containers[dst] = env;
        tableCopy(src, dst); // جدول: صفوفه ونمطه (نسخة عميقة)
        auto kindIt = containerKinds.find(src);
        if (kindIt != containerKinds.end()) containerKinds[dst] = kindIt->second;
        auto customIt = containerCustomKind.find(src);
        if (customIt != containerCustomKind.end()) containerCustomKind[dst] = customIt->second;
        return Value::string(dst);
    };

    // container.rename(old, new) — يحدّث كل السجلات: النوع، الشجرة، المجموعات، الأحداث، دورة الحياة.
    natives["container.rename"] = [this](Args& a, int line) -> Value {
        need("container.rename", a, 2, 2, line);
        std::string from = str(a[0], "container.rename", line), to = str(a[1], "container.rename", line);
        if (to.empty() || from == to || !containers.count(from) || containers.count(to)) return Value::boolean_(false);
        auto moveKey = [&](auto& m) {
            auto it = m.find(from);
            if (it == m.end()) return;
            auto v = std::move(it->second);
            m.erase(it);
            m[to] = std::move(v);
        };
        moveKey(containers);
        moveKey(containerKinds);
        moveKey(containerCustomKind);
        moveKey(containerLifecycle);
        moveKey(containerStateNames);
        moveKey(containerSlots);
        moveKey(eventHandlers);
        moveKey(containerChildren);
        moveKey(containerParent);
        tableMove(from, to); // جدول: صفوفه ونمطه تتبع الاسم الجديد
        // الأب يشير إليها كابن، والأبناء يشيرون إليها كأب
        auto parentIt = containerParent.find(to);
        if (parentIt != containerParent.end()) {
            auto ch = containerChildren.find(parentIt->second);
            if (ch != containerChildren.end()) for (auto& n : ch->second) if (n == from) n = to;
        }
        auto kids = containerChildren.find(to);
        if (kids != containerChildren.end()) for (auto& c : kids->second) containerParent[c] = to;
        for (auto& g : groupMembers)  for (auto& n : g.second) if (n == from) n = to;
        for (auto& v : volumeMembers) for (auto& n : v.second) if (n == from) n = to;
        for (auto& n : containerStack) if (n == from) n = to;
        return Value::boolean_(true);
    };

    natives["container.each"] = [this, sortedNames, kindMatches](Args& a, int line) -> Value {
        need("container.each", a, 2, 2, line);
        std::string kind = str(a[0], "container.each", line);
        if (a[1].type != Value::Type::FUNCTION || !a[1].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'container.each' expects a function as its second argument");
        double count = 0;
        for (const auto& name : sortedNames()) {
            if (!containers.count(name) || !kindMatches(name, kind)) continue;
            Args callArgs{Value::string(name)};
            callFunction(a[1].function, callArgs, line);
            count += 1;
        }
        return Value::num(count);
    };

    // container.update(name, field, fn) — field = fn(field_or_nil)؛ يعيد القيمة الجديدة (nil إن لم توجد الحاوية).
    natives["container.update"] = [this](Args& a, int line) -> Value {
        need("container.update", a, 3, 3, line);
        auto it = containers.find(str(a[0], "container.update", line));
        if (it == containers.end()) return Value::nil();
        if (a[2].type != Value::Type::FUNCTION || !a[2].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'container.update' expects a function as its third argument");
        std::string field = str(a[1], "container.update", line);
        Value cur;
        it->second->get(field, cur);
        Args callArgs{cur};
        Value result = callFunction(a[2].function, callArgs, line);
        auto again = containers.find(a[0].str); // الدالة قد تكون حذفت الحاوية
        if (again == containers.end()) return Value::nil();
        again->second->values[field] = result;
        return result;
    };

    natives["container.incr"] = [this](Args& a, int line) -> Value {
        need("container.incr", a, 2, 3, line);
        auto it = containers.find(str(a[0], "container.incr", line));
        if (it == containers.end()) return Value::nil();
        std::string field = str(a[1], "container.incr", line);
        double by = a.size() > 2 ? num(a[2], "container.incr", line) : 1.0;
        double cur = 0;
        auto f = it->second->values.find(field);
        if (f != it->second->values.end()) {
            if (f->second.type != Value::Type::NUMBER)
                throw diagErr(diag::Code::E0004_InvalidType, line,
                              "'container.incr' field '" + field + "' is a " + f->second.typeName() + ", not a number");
            cur = f->second.number;
        }
        Value out = Value::num(cur + by);
        it->second->values[field] = out;
        return out;
    };

    // container.append(name, field, value) — يضيف لمصفوفة الحقل (ينشئها إن غابت) ويعيد الطول الجديد.
    natives["container.append"] = [this](Args& a, int line) -> Value {
        need("container.append", a, 3, 3, line);
        auto it = containers.find(str(a[0], "container.append", line));
        if (it == containers.end()) return Value::boolean_(false);
        std::string field = str(a[1], "container.append", line);
        auto f = it->second->values.find(field);
        if (f == it->second->values.end() || f->second.type == Value::Type::NIL) {
            it->second->values[field] = newArray();
            f = it->second->values.find(field);
        }
        if (f->second.type != Value::Type::ARRAY || !f->second.array)
            throw diagErr(diag::Code::E0004_InvalidType, line,
                          "'container.append' field '" + field + "' is a " + f->second.typeName() + ", not an array");
        f->second.array->push_back(a[2]);
        return Value::num(static_cast<double>(f->second.array->size()));
    };

    auto pickOmit = [this](const std::string& fn, bool keep) {
        return [this, fn, keep](Args& a, int line) -> Value {
            need(fn, a, 2, 2, line);
            auto it = containers.find(str(a[0], fn, line));
            if (it == containers.end()) return Value::nil();
            const ArrayData& names = arr(a[1], fn, line);
            std::vector<std::string> keys;
            for (const auto& kv : it->second->values) {
                bool listed = false;
                for (const auto& n : names) if (n.type == Value::Type::STRING && n.str == kv.first) { listed = true; break; }
                if (listed == keep) keys.push_back(kv.first);
            }
            std::sort(keys.begin(), keys.end());
            MapData out;
            for (const auto& k : keys) out.push_back({Value::string(k), it->second->values[k]});
            return newMap(std::move(out));
        };
    };
    natives["container.pick"] = pickOmit("container.pick", true);
    natives["container.omit"] = pickOmit("container.omit", false);

    natives["container.toJson"] = [this](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.toJson", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.toJson", a, 1, 1, line);
        auto it = containers.find(str(a[0], "container.toJson", line));
        if (it == containers.end()) return Value::nil();
        std::vector<std::string> keys;
        for (const auto& kv : it->second->values) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        MapData m;
        for (const auto& k : keys) {
            const Value& v = it->second->values[k];
            if (v.type == Value::Type::FUNCTION) continue; // الدوال لا تُحوَّل إلى JSON
            m.push_back({Value::string(k), v});
        }
        return Value::string(json::encode(newMap(std::move(m))));
    };

    // container.fromJson(name, json, overwrite=true) — يدمج حقول كائن JSON داخل حاوية موجودة.
    natives["container.fromJson"] = [this](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.fromJson", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.fromJson", a, 2, 3, line);
        auto it = containers.find(str(a[0], "container.fromJson", line));
        if (it == containers.end()) return Value::boolean_(false);
        Value decoded = json::decodeOrRaw(str(a[1], "container.fromJson", line));
        if (decoded.type != Value::Type::MAP || !decoded.map) return Value::boolean_(false);
        bool overwrite = a.size() < 3 || a[2].isTruthy();
        for (const auto& kv : *decoded.map) {
            std::string key = kv.first.toDisplayString();
            if (!overwrite && it->second->values.count(key)) continue;
            it->second->values[key] = kv.second;
        }
        return Value::boolean_(true);
    };

    // container.diff(a, b) -> {same, added, removed, changed:{field:{from,to}}} (added = موجود في b فقط)
    natives["container.diff"] = [this](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.diff", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.diff", a, 2, 2, line);
        auto ia = containers.find(str(a[0], "container.diff", line));
        auto ib = containers.find(str(a[1], "container.diff", line));
        if (ia == containers.end() || ib == containers.end()) return Value::nil();
        std::vector<std::string> keys;
        for (const auto& kv : ia->second->values) keys.push_back(kv.first);
        for (const auto& kv : ib->second->values) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
        MapData added, removed, changed;
        for (const auto& k : keys) {
            auto fa = ia->second->values.find(k);
            auto fb = ib->second->values.find(k);
            bool inA = fa != ia->second->values.end(), inB = fb != ib->second->values.end();
            if (inA && !inB) removed.push_back({Value::string(k), fa->second});
            else if (!inA && inB) added.push_back({Value::string(k), fb->second});
            else if (!valuesEqual(fa->second, fb->second))
                changed.push_back({Value::string(k), newMap({{Value::string("from"), fa->second}, {Value::string("to"), fb->second}})});
        }
        bool same = added.empty() && removed.empty() && changed.empty();
        return newMap({{Value::string("same"), Value::boolean_(same)},
                       {Value::string("added"), newMap(std::move(added))},
                       {Value::string("removed"), newMap(std::move(removed))},
                       {Value::string("changed"), newMap(std::move(changed))}});
    };

    natives["container.pluck"] = [this, sortedNames, kindMatches](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.pluck", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.pluck", a, 2, 2, line);
        std::string kind = str(a[0], "container.pluck", line), field = str(a[1], "container.pluck", line);
        MapData out;
        for (const auto& name : sortedNames()) {
            if (!kindMatches(name, kind)) continue;
            auto f = containers[name]->values.find(field);
            if (f != containers[name]->values.end()) out.push_back({Value::string(name), f->second});
        }
        return newMap(std::move(out));
    };

    // container.sum(kind, field) — مجموع الحقول الرقمية فقط (غير الرقمي يُتجاهل).
    natives["container.sum"] = [this, kindMatches](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.sum", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.sum", a, 2, 2, line);
        std::string kind = str(a[0], "container.sum", line), field = str(a[1], "container.sum", line);
        double total = 0;
        for (const auto& kv : containers) {
            if (!kindMatches(kv.first, kind)) continue;
            auto f = kv.second->values.find(field);
            if (f != kv.second->values.end() && f->second.type == Value::Type::NUMBER) total += f->second.number;
        }
        return Value::num(total);
    };

    // container.groupBy(kind, field) -> {قيمة الحقل: [أسماء الحاويات]}
    natives["container.groupBy"] = [this, sortedNames, kindMatches](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.groupBy", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.groupBy", a, 2, 2, line);
        std::string kind = str(a[0], "container.groupBy", line), field = str(a[1], "container.groupBy", line);
        MapData out;
        for (const auto& name : sortedNames()) {
            if (!kindMatches(name, kind)) continue;
            auto f = containers[name]->values.find(field);
            if (f == containers[name]->values.end()) continue;
            Value key = Value::string(f->second.toDisplayString());
            const Value* have = mapFind(out, key);
            if (have) have->array->push_back(Value::string(name));
            else out.push_back({key, newArray({Value::string(name)})});
        }
        return newMap(std::move(out));
    };

    // container.query(kind, conditions?, options?) -> أسماء الحاويات المطابقة
    //   conditions: {field: value} للمساواة، أو {field: {gt:5, contains:"x", in:[...], exists:true, ...}}
    //   options   : {sortBy:"field", desc:true, limit:n}
    natives["container.query"] = [this, sortedNames, kindMatches](Args& a, int line) -> Value {
        { Value tv_; if (tableNative("container.query", a, line, tv_)) return tv_; } // جدول (rin_table.cpp)
        need("container.query", a, 1, 3, line);
        std::string kind = str(a[0], "container.query", line);
        const MapData* conds = nullptr;
        if (a.size() > 1 && a[1].type != Value::Type::NIL) conds = &mp(a[1], "container.query", line);
        const MapData* opts = nullptr;
        if (a.size() > 2 && a[2].type != Value::Type::NIL) opts = &mp(a[2], "container.query", line);

        std::vector<std::string> hits;
        for (const auto& name : sortedNames()) {
            if (!kindMatches(name, kind)) continue;
            bool ok = true;
            if (conds) {
                for (const auto& c : *conds) {
                    auto f = containers[name]->values.find(c.first.toDisplayString());
                    const Value* have = (f == containers[name]->values.end()) ? nullptr : &f->second;
                    if (!matchCondition(have, c.second)) { ok = false; break; }
                }
            }
            if (ok) hits.push_back(name);
        }
        if (opts) {
            const Value* sortBy = mapFind(*opts, Value::string("sortBy"));
            const Value* desc = mapFind(*opts, Value::string("desc"));
            const Value* limit = mapFind(*opts, Value::string("limit"));
            if (sortBy && sortBy->type == Value::Type::STRING) {
                std::string field = sortBy->str;
                bool descending = desc && desc->isTruthy();
                auto less = [&](const std::string& x, const std::string& y) {
                    auto fx = containers[x]->values.find(field), fy = containers[y]->values.find(field);
                    bool hx = fx != containers[x]->values.end(), hy = fy != containers[y]->values.end();
                    if (hx != hy) return hy;                       // الحقول الغائبة تأتي أولاً
                    if (!hx) return false;
                    const Value &vx = fx->second, &vy = fy->second;
                    if (vx.type == Value::Type::NUMBER && vy.type == Value::Type::NUMBER) return vx.number < vy.number;
                    return vx.toDisplayString() < vy.toDisplayString();
                };
                std::stable_sort(hits.begin(), hits.end(), less);
                if (descending) std::reverse(hits.begin(), hits.end());
            }
            if (limit && limit->type == Value::Type::NUMBER && limit->number >= 0 &&
                hits.size() > static_cast<size_t>(limit->number))
                hits.resize(static_cast<size_t>(limit->number));
        }
        ArrayData out;
        for (const auto& h : hits) out.push_back(Value::string(h));
        return newArray(std::move(out));
    };

    // container.tree(name) -> {name, kind, fields, children:[...]} (عمق أقصى 64 + حماية من الدورات)
    natives["container.tree"] = [this, kindOf](Args& a, int line) -> Value {
        need("container.tree", a, 1, 1, line);
        std::string root = str(a[0], "container.tree", line);
        if (!containers.count(root)) return Value::nil();
        std::vector<std::string> seen;
        std::function<Value(const std::string&, int)> build = [&](const std::string& name, int depth) -> Value {
            seen.push_back(name);
            std::vector<std::string> keys;
            auto cit = containers.find(name);
            if (cit != containers.end()) for (const auto& kv : cit->second->values) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            MapData fields;
            for (const auto& k : keys) {
                const Value& v = cit->second->values[k];
                if (v.type != Value::Type::FUNCTION) fields.push_back({Value::string(k), v});
            }
            ArrayData kids;
            auto ch = containerChildren.find(name);
            if (ch != containerChildren.end() && depth < 64) {
                for (const auto& c : ch->second) {
                    if (std::find(seen.begin(), seen.end(), c) != seen.end()) continue;
                    kids.push_back(build(c, depth + 1));
                }
            }
            return newMap({{Value::string("name"), Value::string(name)},
                           {Value::string("kind"), Value::string(kindOf(name))},
                           {Value::string("fields"), newMap(std::move(fields))},
                           {Value::string("children"), newArray(std::move(kids))}});
        };
        (void)line;
        return build(root, 0);
    };
}

} // namespace rin

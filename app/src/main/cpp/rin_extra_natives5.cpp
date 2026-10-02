// ============================================================================
//  rin_extra_natives5.cpp — عائلة `#`: الدوال الأصلية #sed #sum #diff #add #to #swap
// ----------------------------------------------------------------------------
//  دفعة خامسة فوق rin_extra_natives.cpp / 2 / 3 / 4 دون تعديل أي منها. تُسجَّل من
//  Interpreter::registerNativesExtra5() وتُضمَّن (#include) في نهاية rin_interpreter.cpp،
//  فتدخل كل أهداف البناء (APK / CLI / WASM / CI) تلقائياً بلا أي تعديل في ملفات البناء.
//
//  أسماؤها تبدأ بـ `#` (يقرؤها Lexer كـ IDENT يحتفظ بالعلامة)، فلا تتعارض مع أي دالة/متغيّر
//  موجود مسبقاً (مثل sum الأصلية التي بقيت كما هي). الشرح الكامل: docs/hash-family.md
//
//    #sed(text, "s/pat/repl/flags")        |  #sed(text, pattern, repl[, flags])
//    #sum(a, b, [c, d], {x: 1} ...)        |  مجموع أرقام/مصفوفات/قيم قواميس (متداخلة)
//    #diff(a, b)                           |  فرق أرقام / فرق مصفوفات / فروق قواميس
//    #add(target, ...)                     |  جمع أرقام / دمج نصوص / إضافة لمصفوفة أو قاموس
//    #to(from, to[, step])                 |  مدى شامل للطرفين (يغذّي for (let i in A to B))
//    #swap(arr, i, j) | #swap(a, b)        |  تبديل عنصرين في مكانهما / إرجاع [b, a]
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <regex>

namespace rin {
namespace extra5 {

using Args = std::vector<Value>;

static Value A5(std::vector<Value> v) { return Value::makeArray(std::make_shared<ArrayData>(std::move(v))); }

static RinError err5(int line, const std::string& msg) {
    return diagErr(diag::Code::E0004_InvalidType, line, msg);
}
static void argc5(const char* fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi)
        throw diagErr(diag::Code::E0007_InvalidArguments, line,
                      std::string("'") + fn + "' expects " + (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + (hi == SIZE_MAX ? std::string("many") : std::to_string(hi))) +
                          " argument(s) but got " + std::to_string(a.size()));
}

// ---- #sed ------------------------------------------------------------------------------------
// استبدال على طريقة sed: الافتراضي أول تطابق فقط، والعلم g لكل التطابقات، والعلم i لتجاهل حالة الأحرف.
// في نص الاستبدال: & = التطابق كله، \1..\9 = مجموعات الالتقاط، \& = & حرفية، \n = سطر جديد، \\ = \ .
static std::string sedFormat(const std::string& repl) {
    std::string out;
    for (size_t i = 0; i < repl.size(); ++i) {
        char c = repl[i];
        if (c == '\\' && i + 1 < repl.size()) {
            char n = repl[++i];
            if (n >= '1' && n <= '9') { out += '$'; out += n; }
            else if (n == '&') out += '&';
            else if (n == 'n') out += '\n';
            else if (n == 't') out += '\t';
            else if (n == '\\') out += '\\';
            else out += n;
        } else if (c == '&') out += "$&";
        else if (c == '$') out += "$$";
        else out += c;
    }
    return out;
}

static std::string sedApply(const std::string& text, const std::string& pattern, const std::string& repl,
                            const std::string& flags, int line) {
    bool global = false, icase = false;
    for (char f : flags) {
        if (f == 'g' || f == 'G') global = true;
        else if (f == 'i' || f == 'I') icase = true;
        else throw diagErr(diag::Code::E0007_InvalidArguments, line,
                           std::string("'#sed': علم غير معروف '") + f + "' — المتاح: g (كل التطابقات) و i (تجاهل حالة الأحرف)");
    }
    std::string pat = icase && !(pattern.size() >= 2 && pattern[1] == ':') ? "i:" + pattern : pattern;
    std::regex re = compileRinRegex(pat, "#sed", line);
    auto fl = std::regex_constants::format_default;
    if (!global) fl |= std::regex_constants::format_first_only;
    return std::regex_replace(text, re, sedFormat(repl), fl);
}

// يفكّ سكربت sed مثل  s/a/b/g  أو  s|a|b|  أو  s#a#b#i  (الفاصل أي حرف بعد s؛ \فاصل = حرفي).
static bool sedParseScript(const std::string& script, std::string& pat, std::string& repl, std::string& flags) {
    if (script.size() < 4 || script[0] != 's') return false;
    char d = script[1];
    if (std::isalnum(static_cast<unsigned char>(d)) || d == '\\' || d == ' ') return false;
    std::vector<std::string> parts(1);
    for (size_t i = 2; i < script.size(); ++i) {
        char c = script[i];
        if (c == '\\' && i + 1 < script.size() && script[i + 1] == d) { parts.back() += d; ++i; continue; }
        if (c == '\\' && i + 1 < script.size()) { parts.back() += c; parts.back() += script[++i]; continue; }
        if (c == d && parts.size() < 3) { parts.emplace_back(); continue; }
        parts.back() += c;
    }
    if (parts.size() != 3) return false;
    pat = parts[0]; repl = parts[1]; flags = parts[2];
    return true;
}

static Value sedValue(const Value& v, const std::string& pat, const std::string& repl, const std::string& flags, int line) {
    if (v.type == Value::Type::STRING) return Value::string(sedApply(v.str, pat, repl, flags, line));
    if (v.type == Value::Type::ARRAY) {
        std::vector<Value> out;
        out.reserve(v.array->size());
        for (auto& e : *v.array) out.push_back(sedValue(e, pat, repl, flags, line));
        return A5(std::move(out));
    }
    throw err5(line, "'#sed' expects text (or an array of text), found `" + v.typeName() + "`");
}

// ---- #sum ------------------------------------------------------------------------------------
static void sumInto(const Value& v, double& total, int depth, int line) {
    if (depth > 64) throw err5(line, "'#sum': nesting is too deep");
    switch (v.type) {
        case Value::Type::NUMBER: total += v.number; return;
        case Value::Type::ARRAY: for (auto& e : *v.array) sumInto(e, total, depth + 1, line); return;
        case Value::Type::MAP: for (auto& kv : *v.map) sumInto(kv.second, total, depth + 1, line); return;
        default:
            throw err5(line, "'#sum' can only add numbers (also inside arrays/maps), found `" + v.typeName() + "`");
    }
}

// ---- #diff -----------------------------------------------------------------------------------
static Value diffValues(const Value& a, const Value& b, int line) {
    if (a.type == Value::Type::NUMBER && b.type == Value::Type::NUMBER) return Value::num(a.number - b.number);
    if (a.type == Value::Type::ARRAY && b.type == Value::Type::ARRAY) {
        std::vector<Value> out;
        for (auto& x : *a.array) {
            bool found = false;
            for (auto& y : *b.array) if (valuesEqual(x, y)) { found = true; break; }
            if (!found) out.push_back(x);
        }
        return A5(std::move(out));
    }
    if (a.type == Value::Type::MAP && b.type == Value::Type::MAP) {
        // كل مفتاح تختلف قيمته أو يغيب من أحد الطرفين -> مفتاح: [قيمة_a, قيمة_b] (nil للغائب)
        auto out = std::make_shared<MapData>();
        auto find = [](const MapData& m, const Value& k) -> const Value* {
            for (auto& kv : m) if (valuesEqual(kv.first, k)) return &kv.second;
            return nullptr;
        };
        for (auto& kv : *a.map) {
            const Value* other = find(*b.map, kv.first);
            if (!other || !valuesEqual(kv.second, *other))
                out->push_back({kv.first, A5({kv.second, other ? *other : Value::nil()})});
        }
        for (auto& kv : *b.map)
            if (!find(*a.map, kv.first)) out->push_back({kv.first, A5({Value::nil(), kv.second})});
        return Value::makeMap(out);
    }
    throw err5(line, "'#diff' compares two numbers, two arrays or two maps; found `" + a.typeName() + "` and `" + b.typeName() + "`");
}

// ---- #swap -----------------------------------------------------------------------------------
static size_t swapIndex(const Value& idx, size_t n, int line) {
    if (idx.type != Value::Type::NUMBER || idx.number != std::floor(idx.number))
        throw err5(line, "'#swap': an index must be a whole number");
    long i = static_cast<long>(idx.number);
    if (i < 0) i += static_cast<long>(n);
    if (i < 0 || i >= static_cast<long>(n))
        throw diagErr(diag::Code::E0035_RuntimeError, line, "'#swap': index " + std::to_string(static_cast<long>(idx.number)) +
                                                             " is out of range for an array of " + std::to_string(n));
    return static_cast<size_t>(i);
}

} // namespace extra5

void Interpreter::registerNativesExtra5() {
    using namespace extra5;

    natives["#sed"] = [](Args& a, int line) -> Value {
        argc5("#sed", a, 2, 4, line);
        std::string pat, repl, flags;
        if (a.size() == 2) {
            if (a[1].type != Value::Type::STRING || !sedParseScript(a[1].str, pat, repl, flags))
                throw diagErr(diag::Code::E0007_InvalidArguments, line,
                              "'#sed' with two arguments needs a sed script such as \"s/old/new/g\"; "
                              "or call it as #sed(text, pattern, replacement[, flags])");
        } else {
            if (a[1].type != Value::Type::STRING || a[2].type != Value::Type::STRING)
                throw err5(line, "'#sed': the pattern and the replacement must be text");
            pat = a[1].str; repl = a[2].str;
            if (a.size() == 4) {
                if (a[3].type != Value::Type::STRING) throw err5(line, "'#sed': flags must be text, e.g. \"g\" or \"gi\"");
                flags = a[3].str;
            }
        }
        return sedValue(a[0], pat, repl, flags, line);
    };

    natives["#sum"] = [](Args& a, int line) -> Value {
        double total = 0.0;
        for (auto& v : a) sumInto(v, total, 0, line);
        return Value::num(total);
    };

    natives["#diff"] = [](Args& a, int line) -> Value {
        argc5("#diff", a, 2, 2, line);
        return diffValues(a[0], a[1], line);
    };

    natives["#add"] = [](Args& a, int line) -> Value {
        argc5("#add", a, 2, SIZE_MAX, line);
        const Value& t = a[0];
        if (t.type == Value::Type::NUMBER) {
            double total = t.number;
            for (size_t i = 1; i < a.size(); ++i) {
                if (a[i].type != Value::Type::NUMBER)
                    throw err5(line, "'#add' on a number needs numbers; found `" + a[i].typeName() + "`");
                total += a[i].number;
            }
            return Value::num(total);
        }
        if (t.type == Value::Type::STRING) {
            std::string out = t.str;
            for (size_t i = 1; i < a.size(); ++i) out += a[i].toDisplayString();
            return Value::string(out);
        }
        if (t.type == Value::Type::ARRAY) { // يُعدِّل المصفوفة في مكانها (مثل push) ويُرجعها للتسلسل
            for (size_t i = 1; i < a.size(); ++i) t.array->push_back(a[i]);
            return t;
        }
        if (t.type == Value::Type::MAP) {
            auto setKey = [&](const Value& k, const Value& v) {
                for (auto& kv : *t.map) if (valuesEqual(kv.first, k)) { kv.second = v; return; }
                t.map->push_back({k, v});
            };
            if (a.size() == 2 && a[1].type == Value::Type::MAP) { // #add(map, other) = دمج
                for (auto& kv : *a[1].map) setKey(kv.first, kv.second);
            } else if (a.size() == 3) {                           // #add(map, key, value)
                setKey(a[1], a[2]);
            } else {
                throw diagErr(diag::Code::E0007_InvalidArguments, line,
                              "'#add' on a map takes #add(map, key, value) or #add(map, otherMap)");
            }
            return t;
        }
        throw err5(line, "'#add' works on numbers, text, arrays and maps; found `" + t.typeName() + "`");
    };

    natives["#to"] = [](Args& a, int line) -> Value {
        argc5("#to", a, 2, 3, line);
        for (auto& v : a)
            if (v.type != Value::Type::NUMBER || std::isnan(v.number))
                throw err5(line, "'#to' (A to B [step C]) needs numbers; found `" + v.typeName() + "`");
        double from = a[0].number, to = a[1].number;
        // الخطوة مقدار فقط: الاتجاه يُستنتج من الطرفين (10 #to 4 step 3 => 10, 7, 4)
        double mag = a.size() == 3 ? std::fabs(a[2].number) : 1.0;
        if (mag == 0.0) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'#to': the step cannot be 0");
        double step = from <= to ? mag : -mag;
        double count = std::floor((to - from) / step + 1e-9) + 1.0;
        if (count > 10000000.0)
            throw diagErr(diag::Code::E0035_RuntimeError, line, "'#to': the range would hold more than 10,000,000 items");
        std::vector<Value> out;
        out.reserve(static_cast<size_t>(count));
        for (long i = 0; i < static_cast<long>(count); ++i) out.push_back(Value::num(from + step * static_cast<double>(i)));
        return A5(std::move(out));
    };

    natives["#swap"] = [](Args& a, int line) -> Value {
        argc5("#swap", a, 2, 3, line);
        if (a.size() == 2) return A5({a[1], a[0]}); // #swap(a, b) -> [b, a]  (لتبديل متغيّرين فعلاً: `#swap a, b;`)
        if (a[0].type == Value::Type::ARRAY) {
            size_t i = swapIndex(a[1], a[0].array->size(), line), j = swapIndex(a[2], a[0].array->size(), line);
            std::swap((*a[0].array)[i], (*a[0].array)[j]);
            return a[0];
        }
        if (a[0].type == Value::Type::MAP) {
            Value *p = nullptr, *q = nullptr;
            for (auto& kv : *a[0].map) {
                if (!p && valuesEqual(kv.first, a[1])) p = &kv.second;
                if (!q && valuesEqual(kv.first, a[2])) q = &kv.second;
            }
            if (!p || !q) throw diagErr(diag::Code::E0035_RuntimeError, line, "'#swap': both keys must exist in the map");
            std::swap(*p, *q);
            return a[0];
        }
        throw err5(line, "'#swap' with 3 arguments works on an array or a map; found `" + a[0].typeName() + "`");
    };
}

} // namespace rin

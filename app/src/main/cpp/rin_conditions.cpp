// ============================================================================
//  rin_conditions.cpp — توسعة "متغيّرات الشروط" (condition helpers) في Rin
// ----------------------------------------------------------------------------
//  تُكمل الدوال الأصلية (is / isNot / empty / typeIs / has / between / all / any / none /
//  coalesce / toBool) بعائلة أوسع من الدوال المنطقية الصغيرة التي تُرجع bool دائماً وتتركّب
//  بحرّية مع if / when / =if= / while / الأنابيب. تُضمَّن (#include) في نهاية rin_interpreter.cpp
//  بنفس أسلوب rin_env.cpp و rin_set.cpp، فتشارك نفس المترجم والمساعدات الساكنة (asNumber ...).
//
//  الحضور والفراغ:   notNil · present · blank
//  المنطقية الصارمة: isTrue · isFalse · xor · implies
//  المجموعات:        oneOf · noneOf · exactly · atLeast · atMost · lengthIs
//  الأرقام:          outside · isInt · isZero · closeTo
//  النصوص:           matches
//  ملاحظة: isNumeric / isEven / isOdd / isPositive / isNegative / inRange / startsWith / endsWith
//  موجودة أصلاً في مكتبات lib/*.og.rin (تعريفها كدوال Rin)، فلا تُضاف هنا كي لا تتعارض مع أسمائها.
//  بالدالة:          every · some · countIf · findIf   (الشرط دالة تأخذ العنصر)
//  الوصف:            key_conditions()   -> قاموس صيغ =term= المدعومة ومعناها القانوني
//
//  الشرح الكامل: docs/key_terms_and_builtins.md
// ============================================================================
#include "rin_interpreter.h"
#include <regex>
#include <cmath>
#include <cctype>
#include <algorithm>

namespace rin {
namespace condimpl {

using Args = std::vector<Value>;

static bool isBlankStr(const std::string& s) {
    for (unsigned char c : s) if (!std::isspace(c)) return false;
    return true;
}

// "فارغ" بالمعنى الموسَّع: nil / نص فارغ / مصفوفة أو قاموس أو مجموعة بلا عناصر.
static bool isEmptyValue(const Value& v) {
    switch (v.type) {
        case Value::Type::NIL:    return true;
        case Value::Type::STRING: return v.str.empty();
        case Value::Type::ARRAY:  return !v.array || v.array->empty();
        case Value::Type::MAP:    return !v.map || v.map->empty();
        case Value::Type::SET:    return !v.set || v.set->empty();
        default:                  return false;
    }
}

// يجمع عناصر القائمة من: وسيط واحد مصفوفة/مجموعة، أو من الوسائط المتبقية مباشرةً بدءاً من start.
static std::vector<Value> collectCandidates(const Args& a, size_t start) {
    std::vector<Value> out;
    if (a.size() == start + 1 && a[start].type == Value::Type::ARRAY && a[start].array) {
        out = *a[start].array;
    } else if (a.size() == start + 1 && a[start].type == Value::Type::SET && a[start].set) {
        out = a[start].set->items;
    } else {
        for (size_t i = start; i < a.size(); ++i) out.push_back(a[i]);
    }
    return out;
}

static size_t countTruthy(const Value& arr, const char* fn, int line) {
    if (arr.type != Value::Type::ARRAY || !arr.array)
        throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "' يتطلب array كوسيط أول");
    size_t n = 0;
    for (const auto& v : *arr.array) if (v.isTruthy()) ++n;
    return n;
}

static bool isIntegral(double d) { return std::isfinite(d) && std::floor(d) == d; }

// يحوّل النص الرقمي إلى رقم؛ false إن لم يكن النص بأكمله رقماً صحيحاً صالحاً.
static bool parseNumeric(const std::string& s, double& out) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    if (b == e) return false;
    std::string t = s.substr(b, e - b);
    try {
        size_t used = 0;
        double d = std::stod(t, &used);
        if (used != t.size() || !std::isfinite(d)) return false;
        out = d;
        return true;
    } catch (...) { return false; }
}

// عناصر مصفوفة أو مجموعة (نسخة) للدوال التي تأخذ شرطاً على شكل دالة.
static std::vector<Value> itemsOf(const Value& v, const char* fn, int line) {
    if (v.type == Value::Type::ARRAY && v.array) return *v.array;
    if (v.type == Value::Type::SET && v.set) return v.set->items;
    throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "' يتطلب array أو set كوسيط أول");
}

} // namespace condimpl

void Interpreter::registerNativesConditions() {
    using namespace condimpl;
    using Args = std::vector<Value>;

    // ---------------------------------------------------------------- الحضور والفراغ
    natives["notNil"] = [](Args& a, int line) -> Value {
        expectArgs("notNil", a, 1, line);
        return Value::boolean_(a[0].type != Value::Type::NIL);
    };
    // present: له قيمة فعلية — ليس nil، وليس فارغاً، وليس نصاً من فراغات فقط.
    natives["present"] = [](Args& a, int line) -> Value {
        expectArgs("present", a, 1, line);
        if (a[0].type == Value::Type::STRING) return Value::boolean_(!isBlankStr(a[0].str));
        return Value::boolean_(!isEmptyValue(a[0]));
    };
    // blank: عكس present (nil / فارغ / نص فراغات فقط).
    natives["blank"] = [](Args& a, int line) -> Value {
        expectArgs("blank", a, 1, line);
        if (a[0].type == Value::Type::STRING) return Value::boolean_(isBlankStr(a[0].str));
        return Value::boolean_(isEmptyValue(a[0]));
    };

    // ---------------------------------------------------------------- المنطقية الصارمة
    // isTrue / isFalse لا تقبل truthiness: 1 ليست true، و nil ليست false.
    natives["isTrue"] = [](Args& a, int line) -> Value {
        expectArgs("isTrue", a, 1, line);
        return Value::boolean_(a[0].type == Value::Type::BOOL && a[0].boolean);
    };
    natives["isFalse"] = [](Args& a, int line) -> Value {
        expectArgs("isFalse", a, 1, line);
        return Value::boolean_(a[0].type == Value::Type::BOOL && !a[0].boolean);
    };
    natives["xor"] = [](Args& a, int line) -> Value {
        expectArgs("xor", a, 2, line);
        return Value::boolean_(a[0].isTruthy() != a[1].isTruthy());
    };
    // implies(p, q): إن صحّ p يجب أن يصحّ q (p => q).
    natives["implies"] = [](Args& a, int line) -> Value {
        expectArgs("implies", a, 2, line);
        return Value::boolean_(!a[0].isTruthy() || a[1].isTruthy());
    };

    // ---------------------------------------------------------------- المجموعات
    // oneOf(v, a, b, c) أو oneOf(v, [a, b, c]) — المقارنة تركيبية (valuesEqual).
    natives["oneOf"] = [](Args& a, int line) -> Value {
        if (a.size() < 2) throw diagErr(diag::Code::E0004_InvalidType, line, "'oneOf' يتطلب قيمة ثم خيارًا واحدًا على الأقل");
        for (const auto& c : collectCandidates(a, 1)) if (valuesEqual(a[0], c)) return Value::boolean_(true);
        return Value::boolean_(false);
    };
    natives["noneOf"] = [](Args& a, int line) -> Value {
        if (a.size() < 2) throw diagErr(diag::Code::E0004_InvalidType, line, "'noneOf' يتطلب قيمة ثم خيارًا واحدًا على الأقل");
        for (const auto& c : collectCandidates(a, 1)) if (valuesEqual(a[0], c)) return Value::boolean_(false);
        return Value::boolean_(true);
    };
    // exactly / atLeast / atMost (array, n): عدد العناصر الصحيحة (truthy) في المصفوفة.
    natives["exactly"] = [](Args& a, int line) -> Value {
        expectArgs("exactly", a, 2, line);
        return Value::boolean_(static_cast<double>(countTruthy(a[0], "exactly", line)) == asNumber(a[1], "exactly", line));
    };
    natives["atLeast"] = [](Args& a, int line) -> Value {
        expectArgs("atLeast", a, 2, line);
        return Value::boolean_(static_cast<double>(countTruthy(a[0], "atLeast", line)) >= asNumber(a[1], "atLeast", line));
    };
    natives["atMost"] = [](Args& a, int line) -> Value {
        expectArgs("atMost", a, 2, line);
        return Value::boolean_(static_cast<double>(countTruthy(a[0], "atMost", line)) <= asNumber(a[1], "atMost", line));
    };
    // lengthIs(v, n): طول نص (بالبايت كما في len) أو مصفوفة أو قاموس أو مجموعة يساوي n.
    natives["lengthIs"] = [](Args& a, int line) -> Value {
        expectArgs("lengthIs", a, 2, line);
        double want = asNumber(a[1], "lengthIs", line);
        double have;
        switch (a[0].type) {
            case Value::Type::STRING: have = static_cast<double>(a[0].str.size()); break;
            case Value::Type::ARRAY:  have = a[0].array ? static_cast<double>(a[0].array->size()) : 0; break;
            case Value::Type::MAP:    have = a[0].map ? static_cast<double>(a[0].map->size()) : 0; break;
            case Value::Type::SET:    have = a[0].set ? static_cast<double>(a[0].set->size()) : 0; break;
            default:
                throw diagErr(diag::Code::E0004_InvalidType, line, "'lengthIs' يتطلب نصًا أو array أو map أو set");
        }
        return Value::boolean_(have == want);
    };

    // outside(x, lo, hi): خارج النطاق المغلق [lo, hi] (نقيض between).
    natives["outside"] = [](Args& a, int line) -> Value {
        expectArgs("outside", a, 3, line);
        double x = asNumber(a[0], "outside", line);
        double lo = asNumber(a[1], "outside", line);
        double hi = asNumber(a[2], "outside", line);
        if (lo > hi) std::swap(lo, hi);
        return Value::boolean_(x < lo || x > hi);
    };
    natives["isInt"] = [](Args& a, int line) -> Value {
        expectArgs("isInt", a, 1, line);
        return Value::boolean_(a[0].type == Value::Type::NUMBER && isIntegral(a[0].number));
    };
    natives["isZero"] = [](Args& a, int line) -> Value {
        expectArgs("isZero", a, 1, line);
        return Value::boolean_(asNumber(a[0], "isZero", line) == 0.0);
    };
    // closeTo(a, b, eps): |a-b| <= eps (eps افتراضيها 1e-9) — مفيدة لمقارنة الأعداد العشرية.
    natives["closeTo"] = [](Args& a, int line) -> Value {
        expectArgsRange("closeTo", a, 2, 3, line);
        double x = asNumber(a[0], "closeTo", line);
        double y = asNumber(a[1], "closeTo", line);
        double eps = a.size() == 3 ? asNumber(a[2], "closeTo", line) : 1e-9;
        return Value::boolean_(std::fabs(x - y) <= eps);
    };

    // matches(text, pattern): النص كله يطابق التعبير النمطي (ECMAScript)؛ نمط غير صالح => خطأ واضح.
    natives["matches"] = [](Args& a, int line) -> Value {
        expectArgs("matches", a, 2, line);
        if (a[0].type != Value::Type::STRING || a[1].type != Value::Type::STRING)
            throw diagErr(diag::Code::E0004_InvalidType, line, "'matches' يتطلب نصًا ثم نمطًا نصيًا");
        try {
            std::regex re(a[1].str, std::regex::ECMAScript);
            return Value::boolean_(std::regex_match(a[0].str, re));
        } catch (const std::regex_error&) {
            throw diagErr(diag::Code::E0004_InvalidType, line, "'matches': تعبير نمطي غير صالح: " + a[1].str);
        }
    };


    // ---------------------------------------------------------------- شروط على شكل دالة (predicates)
    // every(items, fn) · some(items, fn) · countIf(items, fn) · findIf(items, fn)
    // fn تأخذ العنصر وترجع قيمة تُعامَل كشرط (truthy). every على مصفوفة فارغة => true، some => false.
    auto predicate = [this](Args& a, const char* fn, int line) -> std::shared_ptr<Callable> {
        if (a.size() != 2) throw diagErr(diag::Code::E0007_InvalidArguments, line, std::string("'") + fn + "' يتطلب (items, fn)");
        if (a[1].type != Value::Type::FUNCTION || !a[1].function)
            throw diagErr(diag::Code::E0004_InvalidType, line, std::string("'") + fn + "' يتطلب دالة كوسيط ثانٍ");
        return a[1].function;
    };
    auto test = [this](const std::shared_ptr<Callable>& f, const Value& item, int line) -> bool {
        std::vector<Value> args{item};
        return callFunction(f, args, line).isTruthy();
    };
    natives["every"] = [=](Args& a, int line) -> Value {
        auto f = predicate(a, "every", line);
        for (const auto& it : itemsOf(a[0], "every", line)) if (!test(f, it, line)) return Value::boolean_(false);
        return Value::boolean_(true);
    };
    natives["some"] = [=](Args& a, int line) -> Value {
        auto f = predicate(a, "some", line);
        for (const auto& it : itemsOf(a[0], "some", line)) if (test(f, it, line)) return Value::boolean_(true);
        return Value::boolean_(false);
    };
    natives["countIf"] = [=](Args& a, int line) -> Value {
        auto f = predicate(a, "countIf", line);
        double n = 0;
        for (const auto& it : itemsOf(a[0], "countIf", line)) if (test(f, it, line)) ++n;
        return Value::num(n);
    };
    natives["findIf"] = [=](Args& a, int line) -> Value {
        auto f = predicate(a, "findIf", line);
        for (const auto& it : itemsOf(a[0], "findIf", line)) if (test(f, it, line)) return it;
        return Value::nil();
    };

    // ---------------------------------------------------------------- الوصف
    // key_conditions(): كل صيغ `=term=(...)` المدعومة -> معناها القانوني (للمحرّرات والتوثيق).
    natives["key_conditions"] = [](Args& a, int line) -> Value {
        expectArgs("key_conditions", a, 0, line);
        static const char* kTable[][2] = {
            {"if", "if"}, {"when", "if"}, {"unless", "if-not"}, {"ifnot", "if-not"},
            {"elif", "else-if"}, {"elseif", "else-if"}, {"else", "else"},
            {"while", "while"}, {"until", "while-not"},
            {"all", "if-all"}, {"any", "if-any"}, {"none", "if-none"},
            {"nil", "if-nil"}, {"notnil", "if-not-nil"},
            {"empty", "if-empty"}, {"present", "if-present"},
        };
        auto m = std::make_shared<MapData>();
        for (const auto& row : kTable) m->push_back({Value::string(row[0]), Value::string(row[1])});
        return Value::makeMap(m);
    };
}

} // namespace rin

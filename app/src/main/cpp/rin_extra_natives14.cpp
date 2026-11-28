// ============================================================================
//  rin_extra_natives14.cpp — دعم صيغ اللغة الجديدة (يُستدعى من المحلّل)
// ----------------------------------------------------------------------------
//  __sliceRange(x, lo, hi)  ← a[i:j] / a[:j] / a[i:]  على المصفوفات والنصوص (UTF-8 بالأحرف)،
//  تقبل فهارس سالبة (من النهاية) وحدوداً nil (مفتوحة) وتقصّ خارج المدى بدل الخطأ، كبايثون.
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>

namespace rin {
void Interpreter::registerNativesExtra14() {
    auto clampIdx = [this](const Value& v, long long n, long long dflt, const char* what, int line) -> long long {
        if (v.type == Value::Type::NIL) return dflt;
        if (v.type != Value::Type::NUMBER) throw diagErr(diag::Code::E0004_InvalidType, line, std::string("slice ") + what + " must be a number but got " + v.typeName());
        long long i = static_cast<long long>(v.number);
        if (i < 0) i += n;
        return std::max<long long>(0, std::min<long long>(i, n));
    };
    natives["__sliceRange"] = [this, clampIdx](std::vector<Value>& a, int line) -> Value {
        expectArgs("slice", a, 3, line);
        const Value& x = a[0];
        if (x.type == Value::Type::ARRAY && x.array) {
            long long n = static_cast<long long>(x.array->size());
            long long lo = clampIdx(a[1], n, 0, "start", line), hi = clampIdx(a[2], n, n, "end", line);
            auto out = std::make_shared<ArrayData>();
            for (long long i = lo; i < hi; ++i) out->push_back((*x.array)[static_cast<size_t>(i)]);
            return Value::makeArray(out);
        }
        if (x.type == Value::Type::STRING) {
            std::vector<size_t> starts;                       // بداية كل حرف UTF-8
            for (size_t i = 0; i < x.str.size(); ++i) if ((static_cast<unsigned char>(x.str[i]) & 0xC0) != 0x80) starts.push_back(i);
            long long n = static_cast<long long>(starts.size());
            long long lo = clampIdx(a[1], n, 0, "start", line), hi = clampIdx(a[2], n, n, "end", line);
            if (hi <= lo) return Value::string("");
            size_t b = starts[static_cast<size_t>(lo)];
            size_t e = hi >= n ? x.str.size() : starts[static_cast<size_t>(hi)];
            return Value::string(x.str.substr(b, e - b));
        }
        throw diagErr(diag::Code::E0004_InvalidType, line, std::string("cannot slice a value of type ") + x.typeName() + " (arrays and strings only)");
    };
}
} // namespace rin

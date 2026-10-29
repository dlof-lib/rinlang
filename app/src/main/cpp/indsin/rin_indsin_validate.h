// indsin/rin_indsin_validate.h — التحقق من حقول الإدخال حسب النوع (type=text|password|number|email|tel|search|url)
//
// مشترك بين الرسم (حدّ أحمر تلقائي للقيمة غير الصالحة)، وواجهة C (تنقية النص القادم من المضيف قبل كتابته في الخلية)،
// ودوال Rin (indsin.isEmail/isPhone/isUrl/isNumber/fieldError). نقي بلا حالة.
#pragma once
#include <string>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace indsin {

inline bool looksEmail(const std::string& s) {
    size_t at = s.find('@');
    if (at == std::string::npos || at == 0 || s.find('@', at + 1) != std::string::npos) return false;
    for (unsigned char c : s) if (c <= ' ') return false;
    size_t dot = s.find('.', at + 1);
    return dot != std::string::npos && dot > at + 1 && dot + 1 < s.size() && s.back() != '.';
}
inline bool looksPhone(const std::string& s) {
    int digits = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') digits++;
        else if (!(c == ' ' || c == '-' || c == '(' || c == ')' || (c == '+' && i == 0))) return false;
    }
    return digits >= 7 && digits <= 15;
}
inline bool looksUrl(const std::string& s) {
    auto st = [&](const char* p) { return s.rfind(p, 0) == 0 && s.size() > std::strlen(p) + 2 && s.find(' ') == std::string::npos; };
    return st("http://") || st("https://");
}
// عدد عشري اختياري الإشارة (لا أُسّ علمي). يملأ out عند النجاح.
inline bool looksNumber(const std::string& s, double* out = nullptr) {
    if (s.empty()) return false;
    size_t i = 0; if (s[0] == '-' || s[0] == '+') i = 1;
    bool digits = false, dot = false;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') digits = true;
        else if (c == '.' && !dot) dot = true;
        else return false;
    }
    if (!digits) return false;
    if (out) *out = std::strtod(s.c_str(), nullptr);
    return true;
}

// عدد محارف UTF-8 (لا بايتات).
inline size_t utf8CharCount(const std::string& s) { size_t n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; }
inline std::string utf8Truncate(const std::string& s, size_t maxChars) {
    size_t n = 0, i = 0;
    for (; i < s.size(); i++) { if (((unsigned char)s[i] & 0xC0) != 0x80) { if (n == maxChars) break; n++; } }
    return s.substr(0, i);
}

// يُنقّي نصاً قادماً من المضيف حسب النوع: number → محارف عددية فقط، tel → أرقام و+()-مسافة، email/url → بلا مسافات،
// ثم الاقتطاع إلى maxlength (محارف). نوع غير معروف/text/password/search يبقى كما هو (عدا الاقتطاع).
inline std::string sanitizeFieldText(const std::string& type, const std::string& text, size_t maxLen /*0 = بلا حد*/) {
    std::string out;
    if (type == "number") {
        bool dot = false;
        for (size_t i = 0; i < text.size(); i++) {
            char c = text[i];
            if (c >= '0' && c <= '9') out += c;
            else if ((c == '-' || c == '+') && out.empty()) out += c;
            else if (c == '.' && !dot) { dot = true; out += c; }
        }
    } else if (type == "tel") {
        for (size_t i = 0; i < text.size(); i++) {
            char c = text[i];
            if ((c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '(' || c == ')' || (c == '+' && out.empty())) out += c;
        }
    } else if (type == "email" || type == "url") {
        for (unsigned char c : text) if (c > ' ') out += (char)c;
    } else out = text;
    if (maxLen > 0 && utf8CharCount(out) > maxLen) out = utf8Truncate(out, maxLen);
    return out;
}

// رسالة خطأ الصحة ("" = صالح). القيمة الفارغة صالحة دائماً (الإلزام يُفحص بـ required).
// minV/maxV: حدّا النوع number (NaN = بلا حد).
inline std::string fieldValidationMessage(const std::string& type, const std::string& v, double minV = NAN, double maxV = NAN) {
    if (v.empty()) return "";
    if (type == "email") return looksEmail(v) ? "" : "Enter a valid email";
    if (type == "tel" || type == "phone") return looksPhone(v) ? "" : "Enter a valid phone number";
    if (type == "url") return looksUrl(v) ? "" : "Enter a valid URL";
    if (type == "number") {
        double n;
        if (!looksNumber(v, &n)) return "Enter a valid number";
        if (!std::isnan(minV) && n < minV) return "Must be at least " + std::to_string((long long)minV);
        if (!std::isnan(maxV) && n > maxV) return "Must be at most " + std::to_string((long long)maxV);
    }
    return "";
}

} // namespace indsin

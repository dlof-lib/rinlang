// indsin/rin_indsin_validate.h — التحقق من حقول الإدخال حسب النوع (type=text|password|number|email|tel|search|url)
//
// مشترك بين الرسم (حدّ أحمر تلقائي للقيمة غير الصالحة)، وواجهة C (تنقية النص القادم من المضيف قبل كتابته في الخلية)،
// ودوال Rin (indsin.isEmail/isPhone/isUrl/isNumber/fieldError). نقي بلا حالة.
#pragma once
#include <string>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <vector>
#include <algorithm>

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

// تاريخ YYYY-MM-DD صحيح تقويمياً، ووقت HH:MM[:SS].
inline bool looksDate(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i : {0,1,2,3,5,6,8,9}) if (s[i] < '0' || s[i] > '9') return false;
    int y = std::atoi(s.substr(0,4).c_str()), m = std::atoi(s.substr(5,2).c_str()), d = std::atoi(s.substr(8,2).c_str());
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1 || m > 12 || d < 1) return false;
    int lim = dm[m-1] + ((m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 1 : 0);
    return d <= lim;
}
inline bool looksTime(const std::string& s) {
    if (s.size() != 5 && s.size() != 8) return false;
    for (size_t i = 0; i < s.size(); i++) { if (i % 3 == 2) { if (s[i] != ':') return false; } else if (s[i] < '0' || s[i] > '9') return false; }
    int h = std::atoi(s.substr(0,2).c_str()), m = std::atoi(s.substr(3,2).c_str()), sec = s.size() == 8 ? std::atoi(s.substr(6,2).c_str()) : 0;
    return h < 24 && m < 60 && sec < 60;
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
    } else if (type == "date") {
        for (char c : text) if ((c >= '0' && c <= '9') || c == '-') out += c;
    } else if (type == "time") {
        for (char c : text) if ((c >= '0' && c <= '9') || c == ':') out += c;
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
    if (type == "date") return looksDate(v) ? "" : "Enter a valid date (YYYY-MM-DD)";
    if (type == "time") return looksTime(v) ? "" : "Enter a valid time (HH:MM)";
    if (type == "number") {
        double n;
        if (!looksNumber(v, &n)) return "Enter a valid number";
        if (!std::isnan(minV) && n < minV) return "Must be at least " + std::to_string((long long)minV);
        if (!std::isnan(maxV) && n > maxV) return "Must be at most " + std::to_string((long long)maxV);
    }
    return "";
}

// قاعدة تحقق عامة لـ ValidatedField: required | email | url | phone | number | date | time | length | pattern
// (length: minV/maxV = حدّا عدد المحارف؛ pattern: تعبير نمطي ECMAScript مع رسالة اختيارية). "" = صالح.
inline std::string validateRule(const std::string& rule, const std::string& v, double minV, double maxV,
                                const std::string& pattern, const std::string& message) {
    auto msg = [&](const std::string& d) { return message.empty() ? d : message; };
    if (rule == "required") return v.empty() ? msg("This field is required") : "";
    if (v.empty()) return "";
    if (rule == "email" || rule == "url" || rule == "phone" || rule == "tel" || rule == "number" || rule == "date" || rule == "time") {
        std::string m = fieldValidationMessage(rule, v, minV, maxV);
        return m.empty() ? "" : (rule == "number" ? m : msg(m));
    }
    if (rule == "length") {
        double n = (double)utf8CharCount(v);
        if (!std::isnan(minV) && n < minV) return msg("At least " + std::to_string((long long)minV) + " characters");
        if (!std::isnan(maxV) && n > maxV) return msg("At most " + std::to_string((long long)maxV) + " characters");
        return "";
    }
    if (rule == "pattern") {
        try { return std::regex_match(v, std::regex(pattern)) ? "" : msg("Invalid format"); }
        catch (...) { return "Invalid pattern"; }
    }
    return "";
}

// اقتراحات الإكمال التلقائي: options مفصولة بفواصل؛ الأسبقية للبادئة ثم لما يحتويها (بلا حساس للحالة)، تُستثنى المطابقة التامة.
inline std::string suggestAt(const std::string& value, const std::string& options, int index) {
    if (value.empty() || index < 0) return "";
    auto low = [](std::string x) { for (auto& c : x) if (c >= 'A' && c <= 'Z') c = (char)(c + 32); return x; };
    std::string q = low(value);
    std::vector<std::string> pre, mid; size_t i = 0;
    while (i <= options.size()) {
        size_t j = options.find(',', i); if (j == std::string::npos) j = options.size();
        std::string o = options.substr(i, j - i);
        size_t a = o.find_first_not_of(' '), b = o.find_last_not_of(' ');
        o = a == std::string::npos ? "" : o.substr(a, b - a + 1);
        if (!o.empty() && o != value) {
            std::string lo = low(o);
            if (lo.rfind(q, 0) == 0) pre.push_back(o); else if (lo.find(q) != std::string::npos) mid.push_back(o);
        }
        i = j + 1;
    }
    pre.insert(pre.end(), mid.begin(), mid.end());
    return (size_t)index < pre.size() ? pre[(size_t)index] : "";
}

} // namespace indsin

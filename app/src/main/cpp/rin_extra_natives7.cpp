// ============================================================================
//  rin_extra_natives7.cpp — مفهوم `time`: الزمن كقيمة من الدرجة الأولى في Rin
// ----------------------------------------------------------------------------
//  دفعة سابعة فوق rin_extra_natives*.cpp دون تعديل أي منها. تُسجَّل من
//  Interpreter::registerNativesExtra7() وتُضمَّن (#include) في نهاية rin_interpreter.cpp
//  (كالدفعتين الخامسة والسادسة)، فتدخل كل أهداف البناء (APK / CLI / WASM / CI) بلا أي
//  تعديل في ملفات البناء.
//
//  الفكرة: «لحظة» (Time) هي خريطة عادية فيها epoch (ميلي ثانية) + offset (دقائق) + كل أجزائها
//  الجاهزة (year month day hour minute second ms weekday ...). فكل دالة تقبل أي شيء يشبه الزمن:
//  لحظة | رقم (ثوانٍ أو ميلي ثانية تلقائياً) | نص بأي صيغة معقولة (ISO، «غداً 14:30»، «بعد 3 أيام»،
//  «11 أكتوبر 2026»، «next friday») | خريطة أجزاء {year:2026, month:1} | nil = الآن.
//
//  الدوال (الشرح الكامل والأمثلة: docs/TIME.md):
//    time() · time(x) · time(y,m,d,...) · time(x, "صيغة")
//    time.now / utc / of / parse / valid / unix / ms
//    time.format(t[, pattern[, opts]])        |  YYYY MM DD HH mm ss SSS dddd MMMM A Z ... + presets
//    time.add / sub (t, amount, unit | {days:1,hours:2})   |  time.diff(a, b[, unit])
//    time.startOf / endOf (t, unit)           |  time.range(a, b[, step])
//    time.ago(t[, opts])  =  time.human       |  «منذ 3 ساعات» · «in 2 days»
//    time.span(ms | "1h30m" | {…})            |  مدة: أجزاء + مجاميع + نص
//    time.before / after / same / between / is
//    time.zone(t, "+03:00" | "Riyadh" | "Asia/Riyadh") · time.zones()
//    time.hijri(t) · time.fromHijri(y,m,d)    |  التقويم الهجري (حسابي — قد يختلف يوماً عن الرؤية)
//    time.isLeap(y) · time.daysIn(y,m)
//    time.sleep(ms) · time.stopwatch() · time.elapsed(sw[,unit]) · time.measure(fn)
//    time.config({lang:"ar", digits:"arabic", weekStart:"sat", weekend:["fri","sat"]})
//
//  القواعد المشتركة:
//    * كل الدوال لا تُعدِّل مدخلاتها: تُرجع لحظة/قيمة جديدة.
//    * سوء الاستخدام (نوع/عدد وسائط خاطئ، نص لا يُفهم) يرمي خطأً صريحاً فيه الصيغ المقبولة؛
//      أما time.parse فتُرجع nil، وtime.valid تُرجع true/false، لتفحص مدخل المستخدم بلا try.
//    * اللغة الافتراضية en حتى تُغيَّر بـ time.config (حتميّة للاختبارات).
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <thread>

namespace rin {
namespace extra7 {

using Args = std::vector<Value>;
using i64 = long long;

// ------------------------------------------------------------------ قيم صغيرة
static Value S7(const std::string& s) { return Value::string(s); }
static Value N7(double d) { return Value::num(d); }
static Value B7(bool b) { return Value::boolean_(b); }
static Value A7(std::vector<Value> v) { return Value::makeArray(std::make_shared<ArrayData>(std::move(v))); }
static Value M7(std::vector<std::pair<std::string, Value>> kv) {
    MapData out;
    out.reserve(kv.size());
    for (auto& p : kv) out.push_back({Value::string(p.first), std::move(p.second)});
    return Value::makeMap(std::make_shared<MapData>(std::move(out)));
}
static const Value* mget(const Value& m, const std::string& key) {
    if (m.type != Value::Type::MAP || !m.map) return nullptr;
    for (auto& kv : *m.map)
        if (kv.first.type == Value::Type::STRING && kv.first.str == key) return &kv.second;
    return nullptr;
}

static RinError badArgs(int line, const std::string& msg) { return diagErr(diag::Code::E0007_InvalidArguments, line, msg); }
static RinError badType(int line, const std::string& msg) { return diagErr(diag::Code::E0004_InvalidType, line, msg); }
static void argc7(const char* fn, const Args& a, size_t lo, size_t hi, int line) {
    if (a.size() < lo || a.size() > hi)
        throw badArgs(line, std::string("'") + fn + "' expects " +
                                (lo == hi ? std::to_string(lo) : std::to_string(lo) + " to " + std::to_string(hi)) +
                                " argument(s) but got " + std::to_string(a.size()));
}

// ------------------------------------------------------------------ إعدادات عامة (time.config)
struct Config {
    std::string lang = "en";   // en | ar
    bool arDigits = false;     // أرقام عربية-هندية ٠١٢٣ في مخرجات format/ago
    int weekStart = 1;         // 0=الأحد … 6=السبت (الافتراضي الاثنين كما في ISO)
    bool weekend[7] = {true, false, false, false, false, false, true}; // السبت والأحد افتراضياً
};
static Config g_cfg;

// ------------------------------------------------------------------ حساب التقويم (خوارزمية Hinnant)
static i64 floorDiv(i64 a, i64 b) { i64 q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
static i64 floorMod(i64 a, i64 b) { return a - floorDiv(a, b) * b; }

static i64 daysFromCivil(i64 y, int m, int d) {
    y -= m <= 2;
    const i64 era = floorDiv(y, 400);
    const i64 yoe = y - era * 400;
    const i64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const i64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static void civilFromDays(i64 z, i64& y, int& m, int& d) {
    z += 719468;
    const i64 era = floorDiv(z, 146097);
    const i64 doe = z - era * 146097;
    const i64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    const i64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const i64 mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y += (m <= 2);
}
static bool isLeapYear(i64 y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int daysInMonth(i64 y, int m) {
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeapYear(y)) ? 29 : dm[(m - 1) % 12];
}

struct Parts { i64 year; int month, day, hour, minute, second, ms, weekday, dayOfYear; };

static Parts breakdown(i64 epochMs, int offMin) {
    const i64 local = epochMs + static_cast<i64>(offMin) * 60000;
    const i64 days = floorDiv(local, 86400000);
    i64 rem = local - days * 86400000;
    Parts p{};
    civilFromDays(days, p.year, p.month, p.day);
    p.hour = static_cast<int>(rem / 3600000); rem %= 3600000;
    p.minute = static_cast<int>(rem / 60000); rem %= 60000;
    p.second = static_cast<int>(rem / 1000);
    p.ms = static_cast<int>(rem % 1000);
    p.weekday = static_cast<int>(floorMod(days + 4, 7)); // 1970-01-01 كان الخميس
    p.dayOfYear = static_cast<int>(days - daysFromCivil(p.year, 1, 1)) + 1;
    return p;
}
static i64 composeEpoch(i64 y, int mo, int d, int h, int mi, int s, int ms, int offMin) {
    return daysFromCivil(y, mo, d) * 86400000LL + h * 3600000LL + mi * 60000LL + s * 1000LL + ms -
           static_cast<i64>(offMin) * 60000LL;
}

static i64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// إزاحة توقيت الجهاز (بالدقائق) عند لحظة معيّنة — بلا tm_gmtoff ليعمل على ويندوز/أندرويد/WASM بالمثل.
static int localOffsetMin(i64 epochMs) {
    std::time_t t = static_cast<std::time_t>(floorDiv(epochMs, 1000));
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    const i64 asUtc = daysFromCivil(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday) * 86400LL +
                      tmv.tm_hour * 3600LL + tmv.tm_min * 60LL + tmv.tm_sec;
    return static_cast<int>((asUtc - static_cast<i64>(t)) / 60);
}

// ------------------------------------------------------------------ أسماء ثنائية اللغة
static const char* const kMonthEn[12] = {"January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December"};
static const char* const kMonthAr[12] = {"يناير", "فبراير", "مارس", "أبريل", "مايو", "يونيو", "يوليو",
                                         "أغسطس", "سبتمبر", "أكتوبر", "نوفمبر", "ديسمبر"};
static const char* const kDayEn[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* const kDayAr[7] = {"الأحد", "الاثنين", "الثلاثاء", "الأربعاء", "الخميس", "الجمعة", "السبت"};
static const char* const kHijriAr[12] = {"محرم", "صفر", "ربيع الأول", "ربيع الآخر", "جمادى الأولى", "جمادى الآخرة",
                                         "رجب", "شعبان", "رمضان", "شوال", "ذو القعدة", "ذو الحجة"};
static const char* const kHijriEn[12] = {"Muharram", "Safar", "Rabi' al-awwal", "Rabi' al-thani", "Jumada al-awwal",
                                         "Jumada al-thani", "Rajab", "Sha'ban", "Ramadan", "Shawwal",
                                         "Dhu al-Qi'dah", "Dhu al-Hijjah"};

static std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}
static std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}
// أرقام عربية-هندية (٠-٩) وفارسية (۰-۹) -> ASCII
static std::string asciiDigits(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xD9 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) >= 0xA0 && static_cast<unsigned char>(s[i + 1]) <= 0xA9) {
            out += static_cast<char>('0' + (static_cast<unsigned char>(s[i + 1]) - 0xA0)); i += 2;
        } else if (c == 0xDB && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) >= 0xB0 && static_cast<unsigned char>(s[i + 1]) <= 0xB9) {
            out += static_cast<char>('0' + (static_cast<unsigned char>(s[i + 1]) - 0xB0)); i += 2;
        } else if (c == 0xD9 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xAB) { // ٫ فاصلة عشرية
            out += '.'; i += 2;
        } else { out += s[i++]; }
    }
    return out;
}
static std::string arabicDigits(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c >= '0' && c <= '9') { out += "\xD9"; out += static_cast<char>(0xA0 + (c - '0')); }
        else out += c;
    }
    return out;
}
static std::string loc(const std::string& s, bool arDigits) { return arDigits ? arabicDigits(s) : s; }

static std::string pad(i64 v, int w) {
    std::string s = std::to_string(v < 0 ? -v : v);
    while (static_cast<int>(s.size()) < w) s = "0" + s;
    return (v < 0 ? "-" : "") + s;
}
static std::string offStr(int offMin, bool colon = true) {
    const int a = offMin < 0 ? -offMin : offMin;
    return std::string(offMin < 0 ? "-" : "+") + pad(a / 60, 2) + (colon ? ":" : "") + pad(a % 60, 2);
}

// ------------------------------------------------------------------ أسبوع ISO
static void isoWeek(const Parts& p, i64& wy, int& wn) {
    const int wd = p.weekday == 0 ? 7 : p.weekday;          // الاثنين=1 … الأحد=7
    int w = (p.dayOfYear - wd + 10) / 7;
    wy = p.year;
    auto weeksIn = [](i64 y) {
        auto p1 = [](i64 yy) { return (yy + yy / 4 - yy / 100 + yy / 400) % 7; };
        return (p1(y) == 4 || p1(y - 1) == 3) ? 53 : 52;
    };
    if (w < 1) { wy = p.year - 1; w = weeksIn(wy); }
    else if (w > weeksIn(p.year)) { w = 1; wy = p.year + 1; }
    wn = w;
}

// ------------------------------------------------------------------ التقويم الهجري الحسابي (جدولي)
static double islamicToJd(i64 y, int m, int d) {
    return d + std::ceil(29.5 * (m - 1)) + (y - 1) * 354 + std::floor((3 + 11.0 * y) / 30.0) + 1948439.5 - 1;
}
static void hijriFromDays(i64 daysSinceEpoch, i64& hy, int& hm, int& hd) {
    const double jd = std::floor(static_cast<double>(daysSinceEpoch) + 2440587.5) + 0.5;
    hy = static_cast<i64>(std::floor((30.0 * (jd - 1948439.5) + 10646.0) / 10631.0));
    hm = static_cast<int>(std::min(12.0, std::ceil((jd - (29 + islamicToJd(hy, 1, 1))) / 29.5) + 1));
    hd = static_cast<int>(jd - islamicToJd(hy, hm, 1) + 1);
}
static i64 daysFromHijri(i64 hy, int hm, int hd) {
    const double jd = islamicToJd(hy, hm, hd);
    return static_cast<i64>(std::llround(jd - 2440587.5)); // jd = أيام منذ epoch + 2440587.5
}

// ------------------------------------------------------------------ تمثيل «اللحظة» كخريطة
struct T { i64 ms = 0; int off = 0; };

static Value mkTime(const T& t) {
    const Parts p = breakdown(t.ms, t.off);
    i64 wy; int wn; isoWeek(p, wy, wn);
    const bool ar = g_cfg.lang == "ar";
    const std::string tz = (t.off == 0) ? "Z" : offStr(t.off);
    const std::string date = pad(p.year, 4) + "-" + pad(p.month, 2) + "-" + pad(p.day, 2);
    const std::string tim = pad(p.hour, 2) + ":" + pad(p.minute, 2) + ":" + pad(p.second, 2);
    return M7({{"type", S7("time")},
               {"epoch", N7(static_cast<double>(t.ms))},
               {"offset", N7(t.off)},
               {"year", N7(static_cast<double>(p.year))},
               {"month", N7(p.month)},
               {"day", N7(p.day)},
               {"hour", N7(p.hour)},
               {"minute", N7(p.minute)},
               {"second", N7(p.second)},
               {"ms", N7(p.ms)},
               {"weekday", N7(p.weekday)},
               {"weekdayName", S7(ar ? kDayAr[p.weekday] : kDayEn[p.weekday])},
               {"monthName", S7(ar ? kMonthAr[p.month - 1] : kMonthEn[p.month - 1])},
               {"dayOfYear", N7(p.dayOfYear)},
               {"week", N7(wn)},
               {"quarter", N7((p.month - 1) / 3 + 1)},
               {"date", S7(date)},
               {"time", S7(tim)},
               {"tz", S7(tz)},
               {"iso", S7(date + "T" + tim + "." + pad(p.ms, 3) + tz)}});
}

static bool isTimeMap(const Value& v) {
    const Value* e = mget(v, "epoch");
    return e && e->type == Value::Type::NUMBER;
}

// ------------------------------------------------------------------ المناطق الزمنية
struct Zone { const char* name; int off; };
// مناطق بلا توقيت صيفي (فتبقى الإزاحة صحيحة طوال السنة). المناطق ذات التوقيت الصيفي تُحَلّ عبر TZ (IANA) إن توفّرت.
static const Zone kZones[] = {
    {"utc", 0}, {"gmt", 0}, {"z", 0},
    {"riyadh", 180}, {"mecca", 180}, {"makkah", 180}, {"medina", 180}, {"jeddah", 180}, {"saudi", 180}, {"ksa", 180},
    {"baghdad", 180}, {"kuwait", 180}, {"doha", 180}, {"qatar", 180}, {"bahrain", 180}, {"aden", 180}, {"yemen", 180},
    {"amman", 180}, {"damascus", 180}, {"istanbul", 180}, {"minsk", 180}, {"nairobi", 180},
    {"dubai", 240}, {"abudhabi", 240}, {"abu_dhabi", 240}, {"muscat", 240}, {"oman", 240}, {"uae", 240},
    {"tehran", 210}, {"kabul", 270}, {"karachi", 300}, {"tashkent", 300}, {"kolkata", 330}, {"delhi", 330},
    {"dhaka", 360}, {"bangkok", 420}, {"jakarta", 420}, {"singapore", 480}, {"kuala_lumpur", 480}, {"shanghai", 480},
    {"tokyo", 540}, {"seoul", 540},
    {"khartoum", 120}, {"tripoli", 120}, {"johannesburg", 120}, {"kigali", 120},
    {"lagos", 60}, {"tunis", 60}, {"algiers", 60},
    {"casablanca", 60}, {"rabat", 60}, {"accra", 0}, {"dakar", 0},
    {"moscow", 180},
};

static bool ianaOffset(const std::string& name, i64 epochMs, int& off) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    (void)name; (void)epochMs; (void)off; return false;
#else
    static std::mutex mu;
    if (name.empty() || name.find("..") != std::string::npos) return false;
    for (char c : name) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '_' || c == '-' || c == '+')) return false;
    {   // نقبل الاسم فقط إن وُجد فعلاً في قاعدة tzdata (وإلا يعيد glibc UTC بصمت)
        std::ifstream f1("/usr/share/zoneinfo/" + name);
        std::ifstream f2("/usr/lib/zoneinfo/" + name);
        std::ifstream f3("/system/usr/share/zoneinfo/tzdata");
        if (!f1.good() && !f2.good() && !f3.good()) return false;
    }
    std::lock_guard<std::mutex> lk(mu);
    const char* old = std::getenv("TZ");
    std::string saved = old ? old : "";
    const bool had = old != nullptr;
    setenv("TZ", name.c_str(), 1); tzset();
    off = localOffsetMin(epochMs);
    if (had) setenv("TZ", saved.c_str(), 1); else unsetenv("TZ");
    tzset();
    return true;
#endif
}

// يفهم: Z UTC GMT | +03:00 | +0300 | +3 | UTC+3 | GMT-5:30 | اسم مدينة | منطقة IANA مثل Asia/Riyadh
static bool parseZone(const std::string& raw, i64 atMs, int& off) {
    std::string s = lower(trim(raw));
    if (s.empty()) return false;
    if (s.compare(0, 3, "utc") == 0 && s.size() > 3 && (s[3] == '+' || s[3] == '-')) s = s.substr(3);
    else if (s.compare(0, 3, "gmt") == 0 && s.size() > 3 && (s[3] == '+' || s[3] == '-')) s = s.substr(3);
    if (s[0] == '+' || s[0] == '-') {
        const int sign = s[0] == '-' ? -1 : 1;
        std::string body = s.substr(1), hh, mm;
        const size_t c = body.find(':');
        if (c != std::string::npos) { hh = body.substr(0, c); mm = body.substr(c + 1); }
        else if (body.size() == 4) { hh = body.substr(0, 2); mm = body.substr(2); }
        else { hh = body; mm = "0"; }
        if (hh.empty() || hh.size() > 2 || mm.empty() || mm.size() > 2) return false;
        for (char ch : hh + mm) if (ch < '0' || ch > '9') return false;
        const int h = std::atoi(hh.c_str()), m = std::atoi(mm.c_str());
        if (h > 14 || m > 59) return false;
        off = sign * (h * 60 + m);
        return true;
    }
    std::string tail = s;
    const size_t sl = s.rfind('/');
    if (sl != std::string::npos) tail = s.substr(sl + 1);
    for (auto& ch : tail) if (ch == ' ') ch = '_';
    for (const Zone& z : kZones) if (tail == z.name) { off = z.off; return true; }
    // المنطقة الأصلية بحالتها الأصلية (IANA حسّاسة للحروف): نرجع للنص الخام
    return ianaOffset(trim(raw), atMs, off);
}

// ------------------------------------------------------------------ الوحدات
enum class U { MS, SEC, MIN, HOUR, DAY, WEEK, MONTH, QUARTER, YEAR, NONE };

static U unitOf(const std::string& raw) {
    const std::string t = trim(raw);
    // حسّاسة للحالة فقط في m (دقيقة) مقابل M (شهر)
    if (t == "M") return U::MONTH;
    if (t == "m") return U::MIN;
    const std::string s = lower(t);
    static const struct { const char* n; U u; } tab[] = {
        {"ms", U::MS}, {"msec", U::MS}, {"millisecond", U::MS}, {"milliseconds", U::MS}, {"ميلي", U::MS}, {"ميلي ثانية", U::MS},
        {"s", U::SEC}, {"sec", U::SEC}, {"secs", U::SEC}, {"second", U::SEC}, {"seconds", U::SEC},
        {"ثانية", U::SEC}, {"ثواني", U::SEC}, {"ثانيتين", U::SEC}, {"ثوان", U::SEC}, {"ثوانٍ", U::SEC},
        {"min", U::MIN}, {"mins", U::MIN}, {"minute", U::MIN}, {"minutes", U::MIN},
        {"دقيقة", U::MIN}, {"دقائق", U::MIN}, {"دقيقتين", U::MIN}, {"دقيقه", U::MIN},
        {"h", U::HOUR}, {"hr", U::HOUR}, {"hrs", U::HOUR}, {"hour", U::HOUR}, {"hours", U::HOUR},
        {"ساعة", U::HOUR}, {"ساعات", U::HOUR}, {"ساعتين", U::HOUR}, {"ساعه", U::HOUR},
        {"d", U::DAY}, {"day", U::DAY}, {"days", U::DAY}, {"يوم", U::DAY}, {"أيام", U::DAY}, {"ايام", U::DAY}, {"يومين", U::DAY}, {"يومًا", U::DAY},
        {"w", U::WEEK}, {"wk", U::WEEK}, {"week", U::WEEK}, {"weeks", U::WEEK},
        {"أسبوع", U::WEEK}, {"اسبوع", U::WEEK}, {"أسابيع", U::WEEK}, {"اسابيع", U::WEEK}, {"أسبوعين", U::WEEK}, {"اسبوعين", U::WEEK},
        {"mo", U::MONTH}, {"mon", U::MONTH}, {"month", U::MONTH}, {"months", U::MONTH},
        {"شهر", U::MONTH}, {"أشهر", U::MONTH}, {"اشهر", U::MONTH}, {"شهور", U::MONTH}, {"شهرين", U::MONTH}, {"شهرًا", U::MONTH},
        {"q", U::QUARTER}, {"quarter", U::QUARTER}, {"quarters", U::QUARTER}, {"ربع", U::QUARTER},
        {"y", U::YEAR}, {"yr", U::YEAR}, {"yrs", U::YEAR}, {"year", U::YEAR}, {"years", U::YEAR},
        {"سنة", U::YEAR}, {"سنوات", U::YEAR}, {"سنتين", U::YEAR}, {"سنه", U::YEAR}, {"عام", U::YEAR}, {"أعوام", U::YEAR}, {"عامين", U::YEAR},
    };
    for (auto& e : tab) if (s == e.n) return e.u;
    return U::NONE;
}
static bool isDualWord(const std::string& s) {
    static const char* const d[] = {"ساعتين", "دقيقتين", "ثانيتين", "يومين", "أسبوعين", "اسبوعين", "شهرين", "سنتين", "عامين"};
    for (auto x : d) if (s == x) return true;
    return false;
}
static i64 fixedMs(U u) {
    switch (u) {
        case U::MS: return 1; case U::SEC: return 1000; case U::MIN: return 60000; case U::HOUR: return 3600000;
        case U::DAY: return 86400000; case U::WEEK: return 7 * 86400000LL; default: return 0;
    }
}
static const char* const kUnitList = "ms, s, m(inute), h, d, w, M(onth), q, y — أو بالعربية: ثانية دقيقة ساعة يوم أسبوع شهر سنة";

static U needUnit(const Value& v, const char* fn, int line) {
    if (v.type != Value::Type::STRING) throw badType(line, std::string("'") + fn + "': the unit must be text; found `" + v.typeName() + "`");
    U u = unitOf(v.str);
    if (u == U::NONE) throw badArgs(line, std::string("'") + fn + "': unknown unit `" + v.str + "` — المتاح: " + kUnitList);
    return u;
}

// ------------------------------------------------------------------ حساب الأشهر مع تثبيت اليوم
static T addMonths(const T& t, i64 months) {
    const Parts p = breakdown(t.ms, t.off);
    i64 total = p.year * 12 + (p.month - 1) + months;
    const i64 ny = floorDiv(total, 12);
    const int nm = static_cast<int>(floorMod(total, 12)) + 1;
    const int nd = std::min(p.day, daysInMonth(ny, nm));
    return {composeEpoch(ny, nm, nd, p.hour, p.minute, p.second, p.ms, t.off), t.off};
}
static T addUnit(const T& t, double amount, U u, const char* fn, int line) {
    if (std::isnan(amount) || std::isinf(amount)) throw badArgs(line, std::string("'") + fn + "': the amount must be a finite number");
    if (u == U::MONTH || u == U::QUARTER || u == U::YEAR) {
        if (amount != std::floor(amount))
            throw badArgs(line, std::string("'") + fn + "': months/quarters/years must be whole numbers (use days/hours for fractions)");
        const i64 n = static_cast<i64>(amount);
        return addMonths(t, u == U::MONTH ? n : u == U::QUARTER ? n * 3 : n * 12);
    }
    if (std::fabs(amount) > 1e12) throw badArgs(line, std::string("'") + fn + "': the amount is out of range");
    return {t.ms + static_cast<i64>(std::llround(amount * static_cast<double>(fixedMs(u)))), t.off};
}

static T startOfUnit(const T& t, U u) {
    const Parts p = breakdown(t.ms, t.off);
    switch (u) {
        case U::MS: return t;
        case U::SEC: return {composeEpoch(p.year, p.month, p.day, p.hour, p.minute, p.second, 0, t.off), t.off};
        case U::MIN: return {composeEpoch(p.year, p.month, p.day, p.hour, p.minute, 0, 0, t.off), t.off};
        case U::HOUR: return {composeEpoch(p.year, p.month, p.day, p.hour, 0, 0, 0, t.off), t.off};
        case U::DAY: return {composeEpoch(p.year, p.month, p.day, 0, 0, 0, 0, t.off), t.off};
        case U::WEEK: {
            const int back = static_cast<int>(floorMod(p.weekday - g_cfg.weekStart, 7));
            return {composeEpoch(p.year, p.month, p.day, 0, 0, 0, 0, t.off) - back * 86400000LL, t.off};
        }
        case U::MONTH: return {composeEpoch(p.year, p.month, 1, 0, 0, 0, 0, t.off), t.off};
        case U::QUARTER: return {composeEpoch(p.year, ((p.month - 1) / 3) * 3 + 1, 1, 0, 0, 0, 0, t.off), t.off};
        case U::YEAR: return {composeEpoch(p.year, 1, 1, 0, 0, 0, 0, t.off), t.off};
        default: return t;
    }
}
static T endOfUnit(const T& t, U u) {
    if (u == U::MS) return t;
    T s = startOfUnit(t, u);
    T n = (u == U::MONTH || u == U::QUARTER || u == U::YEAR) ? addMonths(s, u == U::MONTH ? 1 : u == U::QUARTER ? 3 : 12)
                                                              : T{s.ms + fixedMs(u), s.off};
    return {n.ms - 1, t.off};
}

// ------------------------------------------------------------------ format
static std::string nameMonth(int m, bool abbr, bool ar) {
    std::string s = ar ? kMonthAr[m - 1] : kMonthEn[m - 1];
    return (abbr && !ar) ? s.substr(0, 3) : s;
}
static std::string nameDay(int d, bool abbr, bool ar) {
    std::string s = ar ? kDayAr[d] : kDayEn[d];
    return (abbr && !ar) ? s.substr(0, 3) : s;
}

static std::string presetPattern(const std::string& p, bool ar) {
    if (p == "iso") return "YYYY-MM-DD[T]HH:mm:ss.SSSZ";
    if (p == "date") return "YYYY-MM-DD";
    if (p == "time") return "HH:mm:ss";
    if (p == "datetime") return "YYYY-MM-DD HH:mm:ss";
    if (p == "short") return "DD/MM/YYYY HH:mm";
    if (p == "long") return ar ? "dddd D MMMM YYYY - hh:mm A" : "dddd, D MMMM YYYY, HH:mm";
    if (p == "date.long") return ar ? "dddd D MMMM YYYY" : "dddd, D MMMM YYYY";
    if (p == "time12") return "hh:mm A";
    if (p == "rfc") return "ddd, DD MMM YYYY HH:mm:ss ZZ";
    if (p == "compact") return "YYYYMMDD_HHmmss";
    return p;
}

static std::string formatTime(const T& t, std::string pat, bool ar, bool arDigits) {
    const Parts p = breakdown(t.ms, t.off);
    i64 wy; int wn; isoWeek(p, wy, wn);
    const bool isRfc = pat == "rfc";
    pat = presetPattern(pat, ar);
    const bool en = isRfc ? true : !ar;
    auto num = [&](i64 v, int w) { return loc(pad(v, w), arDigits); };
    const int h12 = p.hour % 12 == 0 ? 12 : p.hour % 12;
    std::string out;
    for (size_t i = 0; i < pat.size();) {
        if (pat[i] == '[') {                       // نص حرفي بين أقواس: [at]
            size_t e = pat.find(']', i);
            if (e == std::string::npos) { out += pat.substr(i); break; }
            out += pat.substr(i + 1, e - i - 1); i = e + 1; continue;
        }
        auto starts = [&](const char* tk) { return pat.compare(i, std::strlen(tk), tk) == 0; };
        struct Tk { const char* k; };
        static const char* const toks[] = {"YYYY", "YY", "MMMM", "MMM", "MM", "M", "dddd", "ddd", "DD", "D", "HH", "H", "hh", "h",
                                           "mm", "m", "ss", "s", "SSS", "ZZ", "Z", "A", "a", "Q", "W", "X", "x", "d"};
        const char* hit = nullptr;
        for (const char* tk : toks) if (starts(tk)) { hit = tk; break; }
        if (!hit) { out += pat[i++]; continue; }
        const std::string k = hit;
        i += k.size();
        if (k == "YYYY") out += num(p.year, 4);
        else if (k == "YY") out += num(floorMod(p.year, 100), 2);
        else if (k == "MMMM") out += nameMonth(p.month, false, !en);
        else if (k == "MMM") out += nameMonth(p.month, true, !en);
        else if (k == "MM") out += num(p.month, 2);
        else if (k == "M") out += num(p.month, 1);
        else if (k == "dddd") out += nameDay(p.weekday, false, !en);
        else if (k == "ddd") out += nameDay(p.weekday, true, !en);
        else if (k == "DD") out += num(p.day, 2);
        else if (k == "D") out += num(p.day, 1);
        else if (k == "HH") out += num(p.hour, 2);
        else if (k == "H") out += num(p.hour, 1);
        else if (k == "hh") out += num(h12, 2);
        else if (k == "h") out += num(h12, 1);
        else if (k == "mm") out += num(p.minute, 2);
        else if (k == "m") out += num(p.minute, 1);
        else if (k == "ss") out += num(p.second, 2);
        else if (k == "s") out += num(p.second, 1);
        else if (k == "SSS") out += num(p.ms, 3);
        else if (k == "ZZ") out += offStr(t.off, false);
        else if (k == "Z") out += (t.off == 0 && pat.find("Z") != std::string::npos && presetPattern("iso", ar) == pat) ? "Z" : offStr(t.off);
        else if (k == "A") out += en ? (p.hour < 12 ? "AM" : "PM") : (p.hour < 12 ? "ص" : "م");
        else if (k == "a") out += en ? (p.hour < 12 ? "am" : "pm") : (p.hour < 12 ? "ص" : "م");
        else if (k == "Q") out += num((p.month - 1) / 3 + 1, 1);
        else if (k == "W") out += num(wn, 1);
        else if (k == "X") out += num(floorDiv(t.ms, 1000), 1);
        else if (k == "x") out += num(t.ms, 1);
        else if (k == "d") out += num(p.weekday, 1);
    }
    return out;
}

// ------------------------------------------------------------------ التحليل (parse)
static std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> w; std::string cur;
    for (char c : s) {
        if (c == ' ' || c == ',' || c == '\t' || c == '\n') { if (!cur.empty()) { w.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}
static bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}
static int monthFromName(const std::string& w0) {
    std::string w = lower(w0);
    if (w.size() >= 3 && w.back() == '.') w.pop_back();
    for (int i = 0; i < 12; ++i) {
        std::string full = lower(kMonthEn[i]);
        if (w == full || (w.size() >= 3 && w == full.substr(0, 3)) || (i == 8 && w == "sept")) return i + 1;
        if (w == kMonthAr[i]) return i + 1;
    }
    static const struct { const char* n; int m; } alt[] = {
        {"ابريل", 4}, {"اغسطس", 8}, {"اكتوبر", 10}, {"أبريل", 4}, {"كانون", 0},
        {"يناير", 1}, {"جانفي", 1}, {"فيفري", 2}, {"أفريل", 4}, {"ماي", 5}, {"جوان", 6}, {"جويلية", 7}, {"أوت", 8},
        {"شباط", 2}, {"آذار", 3}, {"اذار", 3}, {"نيسان", 4}, {"أيار", 5}, {"ايار", 5}, {"حزيران", 6}, {"تموز", 7}, {"آب", 8}, {"ايلول", 9}, {"أيلول", 9},
    };
    for (auto& e : alt) if (e.m && w == e.n) return e.m;
    return 0;
}
static int weekdayFromName(const std::string& w0) {
    std::string w = lower(w0);
    for (int i = 0; i < 7; ++i) {
        std::string full = lower(kDayEn[i]);
        if (w == full || (w.size() >= 3 && w == full.substr(0, 3)) || w == kDayAr[i]) return i;
    }
    static const struct { const char* n; int d; } alt[] = {
        {"الاحد", 0}, {"احد", 0}, {"الإثنين", 1}, {"الاثنين", 1}, {"الإثنين", 1}, {"الثلاثاء", 2}, {"الاربعاء", 3}, {"الأربعاء", 3},
        {"الخميس", 4}, {"الجمعة", 5}, {"الجمعه", 5}, {"السبت", 6}, {"tues", 2}, {"wednes", 3}, {"thur", 4}, {"thurs", 4},
    };
    for (auto& e : alt) if (w == e.n) return e.d;
    return -1;
}
static bool isFiller(const std::string& w) {
    static const char* const f[] = {"at", "on", "the", "of", "في", "الساعة", "الساعه", "يوم", "عند", "ب", "على", "and", "و", "،"};
    for (auto x : f) if (w == x) return true;
    return false;
}

struct ParseCtx { i64 nowMs; int nowOff; };

// يقرأ HH:MM[:SS[.fff]] [am|pm|ص|م] ابتداءً من s[i]
static bool readClock(const std::string& s, size_t& i, int& h, int& mi, int& sec, int& ms) {
    size_t j = i;
    auto num = [&](int maxDigits, int& out) {
        size_t st = j; int v = 0;
        while (j < s.size() && s[j] >= '0' && s[j] <= '9' && static_cast<int>(j - st) < maxDigits) { v = v * 10 + (s[j] - '0'); ++j; }
        out = v; return j > st;
    };
    if (!num(2, h)) return false;
    if (j >= s.size() || s[j] != ':') return false;
    ++j;
    if (!num(2, mi)) return false;
    sec = 0; ms = 0;
    if (j < s.size() && s[j] == ':') {
        ++j;
        if (!num(2, sec)) return false;
        if (j < s.size() && (s[j] == '.' || s[j] == ',')) {
            ++j; size_t st = j; int frac = 0, digits = 0;
            while (j < s.size() && s[j] >= '0' && s[j] <= '9') { if (digits < 3) { frac = frac * 10 + (s[j] - '0'); ++digits; } ++j; }
            if (j == st) return false;
            while (digits < 3) { frac *= 10; ++digits; }
            ms = frac;
        }
    }
    i = j;
    return true;
}
static bool applyMeridiem(const std::string& w, int& h) {
    if (w == "am" || w == "a.m." || w == "ص" || w == "صباحا" || w == "صباحًا") { if (h == 12) h = 0; return true; }
    if (w == "pm" || w == "p.m." || w == "م" || w == "مساء" || w == "مساءً" || w == "ظهرا" || w == "ظهرًا" || w == "عصرا") { if (h < 12) h += 12; return true; }
    return false;
}

// نص زمني -> لحظة. يُرجع false (والسبب في err) إن لم يُفهم.
static bool parseText(const std::string& input, const ParseCtx& cx, T& out, int forcedOff, bool hasForced) {
    std::string s = lower(trim(asciiDigits(input)));
    for (size_t k = 0; (k = s.find("\xD8\x8C", k)) != std::string::npos;) s.replace(k, 2, " "); // ،
    if (s.empty()) return false;
    const int baseOff = hasForced ? forcedOff : cx.nowOff;
    const T now{cx.nowMs, baseOff};

    // ---- 1) ISO / تاريخ رقمي: YYYY-MM-DD[T| ]HH:MM[:SS[.fff]][Z|±HH:MM]
    {
        size_t i = 0;
        auto num = [&](int maxD, i64& v) {
            size_t st = i; v = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9' && static_cast<int>(i - st) < maxD) { v = v * 10 + (s[i] - '0'); ++i; }
            return i > st ? static_cast<int>(i - st) : 0;
        };
        i64 a = 0, b = 0, c = 0;
        const int la = num(4, a);
        if (la > 0 && i < s.size() && (s[i] == '-' || s[i] == '/' || s[i] == '.')) {
            const char sep = s[i]; ++i;
            const int lb = num(4, b);
            if (lb > 0 && i < s.size() && s[i] == sep) {
                ++i;
                const int lc = num(4, c);
                if (lc > 0) {
                    i64 y, m, d;
                    bool ok = true;
                    if (la == 4) { y = a; m = b; d = c; }                       // YYYY-MM-DD
                    else if (lc == 4) { y = c; m = b; d = a; }                  // DD/MM/YYYY (اليوم أولاً)
                    else ok = false;
                    if (ok && m >= 1 && m <= 12 && d >= 1 && d <= daysInMonth(y, static_cast<int>(m))) {
                        int h = 0, mi = 0, sec = 0, ms = 0, off = baseOff;
                        bool zoneGiven = false;
                        size_t j = i;
                        if (j < s.size() && (s[j] == 't' || s[j] == ' ')) {
                            size_t k = j + 1;
                            while (k < s.size() && s[k] == ' ') ++k;
                            size_t k2 = k;
                            if (readClock(s, k2, h, mi, sec, ms)) {
                                j = k2;
                                size_t m2 = j; while (m2 < s.size() && s[m2] == ' ') ++m2;
                                size_t e = m2; while (e < s.size() && s[e] != ' ') ++e;
                                if (e > m2 && applyMeridiem(s.substr(m2, e - m2), h)) j = e;
                            } else if (s[j] == 't') j = s.size() + 1;           // T بلا ساعة = خطأ
                        }
                        if (j <= s.size()) {
                            std::string rest = trim(s.substr(j));
                            if (!rest.empty()) {
                                int zo = 0;
                                if (rest == "z") { zo = 0; zoneGiven = true; }
                                else if (parseZone(rest, composeEpoch(y, static_cast<int>(m), static_cast<int>(d), h, mi, sec, ms, 0), zo)) zoneGiven = true;
                                else ok = false;
                                if (zoneGiven) off = zo;
                            }
                        } else ok = false;
                        if (ok && h <= 23 && mi <= 59 && sec <= 59) {
                            out = {composeEpoch(y, static_cast<int>(m), static_cast<int>(d), h, mi, sec, ms, off), zoneGiven ? off : baseOff};
                            return true;
                        }
                    }
                }
            }
        }
    }

    // ---- 2) نص حر: ساعة + كلمات (تاريخ/نسبي/أيام)
    std::vector<std::string> w = splitWords(s);
    int H = -1, MI = 0, SEC = 0, MS = 0;
    // استخراج الساعة
    for (size_t k = 0; k < w.size(); ++k) {
        size_t pos = 0; int h, mi, sec, ms;
        std::string tok = w[k];
        if (readClock(tok, pos, h, mi, sec, ms)) {
            std::string tail = tok.substr(pos);
            bool consumedTail = tail.empty();
            if (!tail.empty() && applyMeridiem(tail, h)) consumedTail = true;
            if (!consumedTail) continue;
            size_t erase = 1;
            if (tail.empty() && k + 1 < w.size() && applyMeridiem(w[k + 1], h)) erase = 2;
            if (h > 23 || mi > 59 || sec > 59) return false;
            H = h; MI = mi; SEC = sec; MS = ms;
            w.erase(w.begin() + k, w.begin() + k + erase);
            break;
        }
        // "9pm" / "9 pm" / "٩ م"
        if (allDigits(tok) && k + 1 < w.size()) { int h2 = std::atoi(tok.c_str()); std::string nx = w[k + 1]; if (applyMeridiem(nx, h2) && h2 <= 23) { H = h2; MI = SEC = MS = 0; w.erase(w.begin() + k, w.begin() + k + 2); break; } }
        size_t dg = 0; while (dg < tok.size() && tok[dg] >= '0' && tok[dg] <= '9') ++dg;
        if (dg > 0 && dg < tok.size()) { int h2 = std::atoi(tok.substr(0, dg).c_str()); if (applyMeridiem(tok.substr(dg), h2) && h2 <= 23) { H = h2; MI = SEC = MS = 0; w.erase(w.begin() + k); break; } }
    }
    std::vector<std::string> ws;
    for (auto& x : w) {
        std::string y = x;
        // «و30» ملتصقة بالواو
        if (y.size() > 2 && y.compare(0, 2, "و") == 0 && (std::isdigit(static_cast<unsigned char>(y[2])))) y = y.substr(2);
        if (!isFiller(y)) ws.push_back(y);
    }
    // كلمات الوقت الخاصة
    for (size_t k = 0; k < ws.size(); ++k) {
        if (ws[k] == "noon" || ws[k] == "midday") { H = 12; MI = SEC = MS = 0; ws.erase(ws.begin() + k); break; }
        if (ws[k] == "midnight") { H = 0; MI = SEC = MS = 0; ws.erase(ws.begin() + k); break; }
    }

    const Parts np = breakdown(now.ms, baseOff);
    T base = now;
    bool dayLevel = false;      // الأساس «يوم» (يُصفَّر الوقت إلا إن ذُكرت ساعة)
    bool understood = false;
    i64 offsetMs = 0; i64 monthsDelta = 0; bool relative = false; int dir = 0;
    int month = 0, dayNum = 0; i64 yearNum = 0; bool haveYear = false; int wantWeekday = -1; int mod = 0; // mod: +1 next, -1 last, 0 this
    U modUnit = U::NONE;

    auto numWord = [](const std::string& x, double& v) {
        static const struct { const char* n; double v; } nw[] = {{"a", 1}, {"an", 1}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"six", 6},
                                                                 {"seven", 7}, {"eight", 8}, {"nine", 9}, {"ten", 10}, {"twelve", 12}, {"half", 0.5}, {"نصف", 0.5}};
        for (auto& e : nw) if (x == e.n) { v = e.v; return true; }
        return false;
    };

    for (size_t k = 0; k < ws.size(); ++k) {
        const std::string& x = ws[k];
        if (x == "now" || x == "الآن" || x == "الان" || x == "حالا" || x == "حاليا") { understood = true; continue; }
        if (x == "today" || x == "اليوم") { dayLevel = true; understood = true; continue; }
        if (x == "tomorrow" || x == "غدا" || x == "غداً" || x == "بكرة" || x == "بكره" || x == "غدًا") { dayLevel = true; offsetMs += 86400000; understood = true; continue; }
        if (x == "yesterday" || x == "أمس" || x == "امس" || x == "البارحة") { dayLevel = true; offsetMs -= 86400000; understood = true; continue; }
        if (x == "in" || x == "after" || x == "بعد" || x == "خلال" || x == "from") { if (dir == 0) dir = 1; relative = true; continue; }
        if (x == "ago" || x == "قبل" || x == "منذ" || x == "مضت" || x == "مضى" || x == "before") { dir = -1; relative = true; continue; }
        if (x == "next" || x == "القادم" || x == "القادمة" || x == "المقبل" || x == "المقبلة" || x == "التالي") { mod = 1; understood = true; continue; }
        if (x == "last" || x == "past" || x == "previous" || x == "الماضي" || x == "الماضية" || x == "السابق" || x == "السابقة") { mod = -1; understood = true; continue; }
        if (x == "this" || x == "الحالي" || x == "الحالية" || x == "هذا" || x == "هذه") { mod = 0; understood = true; continue; }
        const int mn = monthFromName(x);
        if (mn) { month = mn; understood = true; continue; }
        const int wd = weekdayFromName(x);
        if (wd >= 0) { wantWeekday = wd; understood = true; continue; }
        // رقم + وحدة | كلمة عدد + وحدة | وحدة مثناة
        double amount = 0; bool haveAmount = false; U u = U::NONE;
        std::string xn = x;
        // 3d / 5min / 2h30m متتابعة ملتصقة
        size_t dg = 0; while (dg < xn.size() && ((xn[dg] >= '0' && xn[dg] <= '9') || xn[dg] == '.')) ++dg;
        if (dg > 0) {
            amount = std::atof(xn.substr(0, dg).c_str()); haveAmount = true;
            std::string unitPart = xn.substr(dg);
            if (!unitPart.empty()) {
                // قد تكون عدة أزواج ملتصقة: 1h30m
                size_t pos = 0; bool any = false; double amt = amount;
                while (pos <= xn.size()) {
                    size_t d2 = pos; while (d2 < xn.size() && ((xn[d2] >= '0' && xn[d2] <= '9') || xn[d2] == '.')) ++d2;
                    if (d2 == pos) break;
                    amt = std::atof(xn.substr(pos, d2 - pos).c_str());
                    size_t u2 = d2; while (u2 < xn.size() && !((xn[u2] >= '0' && xn[u2] <= '9') || xn[u2] == '.')) ++u2;
                    U uu = unitOf(xn.substr(d2, u2 - d2));
                    if (uu == U::NONE) { any = false; break; }
                    if (uu == U::MONTH || uu == U::QUARTER || uu == U::YEAR) monthsDelta += static_cast<i64>(amt) * (uu == U::MONTH ? 1 : uu == U::QUARTER ? 3 : 12) * (dir == 0 ? 1 : 1);
                    else offsetMs += static_cast<i64>(std::llround(amt * static_cast<double>(fixedMs(uu))));
                    any = true; pos = u2;
                    if (pos >= xn.size()) break;
                }
                if (any) { relative = true; understood = true; haveAmount = false; continue; }
                return false;
            }
            // الرقم وحده: وحدة في الكلمة التالية، أو يوم من الشهر/سنة
            if (k + 1 < ws.size() && unitOf(ws[k + 1]) != U::NONE && monthFromName(ws[k + 1]) == 0) {
                u = unitOf(ws[k + 1]); ++k;
            } else if (xn.size() == 4 && !haveYear && amount >= 1000) { yearNum = static_cast<i64>(amount); haveYear = true; understood = true; continue; }
            else if (amount >= 1 && amount <= 31 && dayNum == 0) { dayNum = static_cast<int>(amount); understood = true; continue; }
            else return false;
        } else if (numWord(xn, amount) && k + 1 < ws.size() && unitOf(ws[k + 1]) != U::NONE) {
            haveAmount = true; u = unitOf(ws[k + 1]); ++k;
        } else {
            // اللواحق الإنجليزية للأيام: 1st 2nd 3rd 4th
            u = unitOf(xn);
            if (u == U::NONE) {
                // وحدة بعد next/last فقط (week/month/year/ثواني...)
                return false;
            }
            if (isDualWord(xn)) { amount = 2; haveAmount = true; }
            else if (mod != 0 || (k > 0 && (ws[k - 1] == "next" || ws[k - 1] == "last"))) { modUnit = u; understood = true; continue; }
            else if (k + 1 < ws.size() && (ws[k + 1] == "القادم" || ws[k + 1] == "القادمة" || ws[k + 1] == "الماضي" || ws[k + 1] == "الماضية" || ws[k + 1] == "المقبل" || ws[k + 1] == "المقبلة")) { modUnit = u; understood = true; continue; }
            else { amount = 1; haveAmount = true; }
        }
        if (haveAmount && u != U::NONE) {
            relative = true; understood = true;
            if (u == U::MONTH || u == U::QUARTER || u == U::YEAR) {
                if (amount != std::floor(amount)) return false;
                monthsDelta += static_cast<i64>(amount) * (u == U::MONTH ? 1 : u == U::QUARTER ? 3 : 12);
            } else offsetMs += static_cast<i64>(std::llround(amount * static_cast<double>(fixedMs(u))));
        }
    }
    if (!understood && !relative) {
        // «3rd october» وما شابه
        return false;
    }

    // ---- ركّب النتيجة
    if (month != 0 || dayNum != 0 || haveYear) {
        const i64 y = haveYear ? yearNum : np.year;
        const int m = month ? month : np.month;
        const int d = dayNum ? dayNum : 1;
        if (d > daysInMonth(y, m)) return false;
        base = {composeEpoch(y, m, d, 0, 0, 0, 0, baseOff), baseOff};
        dayLevel = true;
    }
    if (relative) {
        const i64 sign = (dir < 0) ? -1 : 1;
        base = {base.ms + sign * offsetMs, baseOff};
        if (monthsDelta) base = addMonths(base, sign * monthsDelta);
        if (offsetMs % 86400000 == 0 && monthsDelta == 0 && H < 0 && offsetMs != 0 && !dayLevel) { /* أيام كاملة: نُبقي الوقت الحالي */ }
    } else if (offsetMs != 0) {
        base = {base.ms + offsetMs, baseOff};
    }
    if (modUnit != U::NONE) {
        if (modUnit == U::MONTH || modUnit == U::QUARTER || modUnit == U::YEAR)
            base = addMonths(base, (mod == 0 ? 0 : mod) * (modUnit == U::MONTH ? 1 : modUnit == U::QUARTER ? 3 : 12));
        else base = {base.ms + mod * fixedMs(modUnit), baseOff};
    }
    if (wantWeekday >= 0) {
        const Parts bp = breakdown(base.ms, baseOff);
        int diff = wantWeekday - bp.weekday;
        if (mod > 0) { if (diff <= 0) diff += 7; }
        else if (mod < 0) { if (diff >= 0) diff -= 7; }
        else { if (diff < 0) diff += 7; }
        base = {base.ms + diff * 86400000LL, baseOff};
        dayLevel = true;
    }
    if (dayLevel) base = startOfUnit(base, U::DAY);
    if (H >= 0) {
        const Parts bp = breakdown(base.ms, baseOff);
        base = {composeEpoch(bp.year, bp.month, bp.day, H, MI, SEC, MS, baseOff), baseOff};
    }
    out = base;
    return true;
}

// ------------------------------------------------------------------ تحليل بصيغة صريحة (time.parse(text, "DD/MM/YYYY HH:mm"))
static bool parseWithFormat(const std::string& textIn, const std::string& fmtIn, const ParseCtx& cx, T& out) {
    const std::string text = asciiDigits(textIn);
    const std::string fmt = presetPattern(fmtIn, false);
    size_t i = 0;
    i64 Y = 1970; int Mo = 1, D = 1, H = 0, Mi = 0, S = 0, Ms = 0; int off = cx.nowOff; bool pm = false, hasMer = false, h12 = false, haveOff = false;
    bool haveY = false, haveMo = false, haveD = false; bool isUnix = false; i64 unixV = 0; bool unixMs = false;
    auto readNum = [&](int minD, int maxD, i64& v) {
        size_t st = i; v = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9' && static_cast<int>(i - st) < maxD) { v = v * 10 + (text[i] - '0'); ++i; }
        return static_cast<int>(i - st) >= minD;
    };
    for (size_t f = 0; f < fmt.size();) {
        if (fmt[f] == '[') {
            size_t e = fmt.find(']', f); if (e == std::string::npos) return false;
            const std::string lit = fmt.substr(f + 1, e - f - 1);
            if (text.compare(i, lit.size(), lit) != 0) return false;
            i += lit.size(); f = e + 1; continue;
        }
        static const char* const toks[] = {"YYYY", "YY", "MMMM", "MMM", "MM", "M", "DD", "D", "HH", "H", "hh", "h", "mm", "m", "ss", "s", "SSS", "ZZ", "Z", "A", "a", "X", "x"};
        const char* hit = nullptr;
        for (const char* tk : toks) if (fmt.compare(f, std::strlen(tk), tk) == 0) { hit = tk; break; }
        if (!hit) { if (i >= text.size() || (text[i] != fmt[f] && !(fmt[f] == ' ' && text[i] == ' '))) return false; ++i; ++f; continue; }
        const std::string k = hit; f += k.size();
        i64 v = 0;
        if (k == "YYYY") { if (!readNum(4, 4, v)) return false; Y = v; haveY = true; }
        else if (k == "YY") { if (!readNum(2, 2, v)) return false; Y = 2000 + v; haveY = true; }
        else if (k == "MM" || k == "M") { if (!readNum(k == "MM" ? 2 : 1, 2, v) || v < 1 || v > 12) return false; Mo = static_cast<int>(v); haveMo = true; }
        else if (k == "MMMM" || k == "MMM") {
            size_t e = i; while (e < text.size() && text[e] != ' ' && text[e] != ',' && text[e] != '-' && text[e] != '/' && text[e] != '.') ++e;
            const int m = monthFromName(text.substr(i, e - i)); if (!m) return false; Mo = m; haveMo = true; i = e;
        }
        else if (k == "DD" || k == "D") { if (!readNum(k == "DD" ? 2 : 1, 2, v) || v < 1 || v > 31) return false; D = static_cast<int>(v); haveD = true; }
        else if (k == "HH" || k == "H") { if (!readNum(k == "HH" ? 2 : 1, 2, v) || v > 23) return false; H = static_cast<int>(v); }
        else if (k == "hh" || k == "h") { if (!readNum(k == "hh" ? 2 : 1, 2, v) || v < 1 || v > 12) return false; H = static_cast<int>(v); h12 = true; }
        else if (k == "mm" || k == "m") { if (!readNum(k == "mm" ? 2 : 1, 2, v) || v > 59) return false; Mi = static_cast<int>(v); }
        else if (k == "ss" || k == "s") { if (!readNum(k == "ss" ? 2 : 1, 2, v) || v > 59) return false; S = static_cast<int>(v); }
        else if (k == "SSS") { if (!readNum(3, 3, v)) return false; Ms = static_cast<int>(v); }
        else if (k == "A" || k == "a") {
            const std::string rest = lower(text.substr(i, 2));
            if (rest == "am") { pm = false; i += 2; } else if (rest == "pm") { pm = true; i += 2; }
            else if (text.compare(i, 2, "ص") == 0) { pm = false; i += 2; } else if (text.compare(i, 2, "م") == 0) { pm = true; i += 2; }
            else return false;
            hasMer = true;
        }
        else if (k == "Z" || k == "ZZ") {
            size_t e = i; while (e < text.size() && text[e] != ' ') ++e;
            int zo; if (!parseZone(text.substr(i, e - i), cx.nowMs, zo)) return false; off = zo; haveOff = true; i = e;
        }
        else if (k == "X" || k == "x") { if (!readNum(1, 18, v)) return false; isUnix = true; unixV = v; unixMs = (k == "x"); }
    }
    if (i != text.size()) return false;
    if (isUnix) { out = {unixMs ? unixV : unixV * 1000, cx.nowOff}; return true; }
    if (h12 || hasMer) { if (hasMer && pm && H < 12) H += 12; if (hasMer && !pm && H == 12) H = 0; }
    const Parts np = breakdown(cx.nowMs, cx.nowOff);
    if (!haveY) Y = np.year;
    if (!haveMo && !haveD && !haveY) { Mo = np.month; D = np.day; }
    if (D > daysInMonth(Y, Mo)) return false;
    out = {composeEpoch(Y, Mo, D, H, Mi, S, Ms, off), haveOff ? off : cx.nowOff};
    return true;
}

// ------------------------------------------------------------------ تحويل أي قيمة إلى لحظة
static T nowT() { const i64 n = nowMs(); return {n, localOffsetMin(n)}; }

static const char* const kAccepted =
    "المقبول: ISO «2026-10-11T14:30» · «2026-10-11 14:30» · «11/10/2026» · «11 أكتوبر 2026» · «October 11, 2026» · "
    "«14:30» · «today/tomorrow/yesterday/اليوم/غداً/أمس» · «in 3 days / بعد 3 أيام» · «2 hours ago / قبل ساعتين» · «next friday / الجمعة القادمة»";

static T fromParts(const Value& m, int line, const char* fn) {
    const T now = nowT();
    const Parts np = breakdown(now.ms, now.off);
    auto num = [&](const char* k, double def) {
        const Value* v = mget(m, k);
        if (!v || v->type == Value::Type::NIL) return def;
        if (v->type != Value::Type::NUMBER) throw badType(line, std::string("'") + fn + "': `" + k + "` must be a number");
        return v->number;
    };
    int off = now.off;
    if (const Value* z = mget(m, "tz")) { if (z->type == Value::Type::STRING && !parseZone(z->str, now.ms, off)) throw badArgs(line, std::string("'") + fn + "': unknown zone `" + z->str + "`"); }
    const i64 y = static_cast<i64>(num("year", static_cast<double>(np.year)));
    const int mo = static_cast<int>(num("month", mget(m, "year") ? 1 : np.month));
    const int d = static_cast<int>(num("day", mget(m, "year") || mget(m, "month") ? 1 : np.day));
    const int h = static_cast<int>(num("hour", 0)), mi = static_cast<int>(num("minute", 0)), s = static_cast<int>(num("second", 0)), ms = static_cast<int>(num("ms", 0));
    if (mo < 1 || mo > 12) throw badArgs(line, std::string("'") + fn + "': month must be 1..12");
    if (d < 1 || d > daysInMonth(y, mo)) throw badArgs(line, std::string("'") + fn + "': day must be 1.." + std::to_string(daysInMonth(y, mo)) + " for that month");
    if (h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59 || ms < 0 || ms > 999) throw badArgs(line, std::string("'") + fn + "': hour/minute/second/ms out of range");
    return {composeEpoch(y, mo, d, h, mi, s, ms, off), off};
}

static T toT(const Value& v, int line, const char* fn) {
    switch (v.type) {
        case Value::Type::NIL: return nowT();
        case Value::Type::NUMBER: {
            if (std::isnan(v.number) || std::isinf(v.number)) throw badArgs(line, std::string("'") + fn + "': the number is not a valid time");
            const double ms = std::fabs(v.number) < 1e11 ? v.number * 1000.0 : v.number; // ثوانٍ أو ميلي ثانية تلقائياً
            const i64 e = static_cast<i64>(std::llround(ms));
            return {e, localOffsetMin(e)};
        }
        case Value::Type::STRING: {
            T out; const T now = nowT();
            if (parseText(v.str, {now.ms, now.off}, out, 0, false)) return out;
            throw badArgs(line, std::string("'") + fn + "': لم أفهم الزمن `" + v.str + "` — " + kAccepted);
        }
        case Value::Type::MAP: {
            if (isTimeMap(v)) {
                const i64 e = static_cast<i64>(std::llround(mget(v, "epoch")->number));
                const Value* o = mget(v, "offset");
                return {e, o && o->type == Value::Type::NUMBER ? static_cast<int>(o->number) : localOffsetMin(e)};
            }
            return fromParts(v, line, fn);
        }
        default: throw badType(line, std::string("'") + fn + "' expects a time, number, text or parts map; found `" + v.typeName() + "`");
    }
}

// ------------------------------------------------------------------ مدد زمنية
struct DurPart { double amount; U unit; };

static bool parseDurationText(const std::string& in, std::vector<DurPart>& out) {
    std::string s = lower(trim(asciiDigits(in)));
    if (s.empty()) return false;
    // H:MM[:SS]
    {
        size_t pos = 0; int h, mi, sec, ms;
        if (readClock(s, pos, h, mi, sec, ms) && pos == s.size()) {
            out = {{static_cast<double>(h), U::HOUR}, {static_cast<double>(mi), U::MIN}, {static_cast<double>(sec), U::SEC}, {static_cast<double>(ms), U::MS}};
            return true;
        }
    }
    for (size_t k = 0; (k = s.find("\xD8\x8C", k)) != std::string::npos;) s.replace(k, 2, " ");
    for (auto& c : s) if (c == ',') c = ' ';
    std::vector<std::string> w = splitWords(s);
    std::vector<std::string> ws;
    for (auto& x : w) {
        std::string y = x;
        if (y.size() > 2 && y.compare(0, 2, "و") == 0 && std::isdigit(static_cast<unsigned char>(y[2]))) y = y.substr(2);
        if (y == "and" || y == "و") continue;
        ws.push_back(y);
    }
    if (ws.empty()) return false;
    for (size_t k = 0; k < ws.size(); ++k) {
        const std::string& x = ws[k];
        size_t dg = 0; while (dg < x.size() && ((x[dg] >= '0' && x[dg] <= '9') || x[dg] == '.')) ++dg;
        if (dg > 0) {
            // 1h30m ملتصقة
            size_t pos = 0; bool ok = true; bool consumedAll = false;
            while (pos < x.size()) {
                size_t d2 = pos; while (d2 < x.size() && ((x[d2] >= '0' && x[d2] <= '9') || x[d2] == '.')) ++d2;
                if (d2 == pos) { ok = false; break; }
                const double amt = std::atof(x.substr(pos, d2 - pos).c_str());
                size_t u2 = d2; while (u2 < x.size() && !((x[u2] >= '0' && x[u2] <= '9') || x[u2] == '.')) ++u2;
                if (u2 == d2) {   // رقم وحده: الوحدة في الكلمة التالية
                    if (pos == 0 && u2 == x.size() && k + 1 < ws.size()) {
                        const U u = unitOf(ws[k + 1]);
                        if (u == U::NONE) return false;
                        out.push_back({amt, u}); ++k; consumedAll = true; break;
                    }
                    return false;
                }
                const U u = unitOf(x.substr(d2, u2 - d2));
                if (u == U::NONE) return false;
                out.push_back({amt, u}); pos = u2;
            }
            if (!ok) return false;
            (void)consumedAll;
            continue;
        }
        double nv = 0;
        static const struct { const char* n; double v; } nw[] = {{"a", 1}, {"an", 1}, {"one", 1}, {"two", 2}, {"three", 3}, {"four", 4}, {"five", 5}, {"half", 0.5}, {"نصف", 0.5}};
        bool isNW = false; for (auto& e : nw) if (x == e.n) { nv = e.v; isNW = true; break; }
        if (isNW && k + 1 < ws.size() && unitOf(ws[k + 1]) != U::NONE) { out.push_back({nv, unitOf(ws[k + 1])}); ++k; continue; }
        const U u = unitOf(x);
        if (u == U::NONE) return false;
        out.push_back({isDualWord(x) ? 2.0 : 1.0, u});
    }
    return !out.empty();
}

static double durationMs(const std::vector<DurPart>& p) {
    double ms = 0;
    for (auto& d : p) {
        if (d.unit == U::MONTH) ms += d.amount * 30.4375 * 86400000.0;
        else if (d.unit == U::QUARTER) ms += d.amount * 91.3125 * 86400000.0;
        else if (d.unit == U::YEAR) ms += d.amount * 365.25 * 86400000.0;
        else ms += d.amount * static_cast<double>(fixedMs(d.unit));
    }
    return ms;
}

static bool isSpanMap(const Value& v) {
    const Value* t = mget(v, "type");
    const Value* m = mget(v, "ms");
    return t && t->type == Value::Type::STRING && t->str == "span" && m && m->type == Value::Type::NUMBER;
}

// أي «مدة» -> قائمة أجزاء (تدعم أشهر/سنوات تقويمية عند الجمع إلى لحظة)
static std::vector<DurPart> durationParts(const Value& spec, const Value* unitArg, int line, const char* fn) {
    std::vector<DurPart> out;
    if (spec.type == Value::Type::NUMBER) {
        if (!unitArg) throw badArgs(line, std::string("'") + fn + "': أضف الوحدة، مثال: " + fn + "(t, 3, \"days\") — " + kUnitList);
        out.push_back({spec.number, needUnit(*unitArg, fn, line)});
        return out;
    }
    if (spec.type == Value::Type::STRING) {
        if (!parseDurationText(spec.str, out)) throw badArgs(line, std::string("'") + fn + "': لم أفهم المدة `" + spec.str + "` — أمثلة: \"1h30m\" · \"2 days\" · \"ساعة و30 دقيقة\" · \"1:30:00\"");
        return out;
    }
    if (spec.type == Value::Type::MAP) {
        if (isSpanMap(spec)) { out.push_back({mget(spec, "ms")->number, U::MS}); return out; }
        for (auto& kv : *spec.map) {
            if (kv.first.type != Value::Type::STRING) continue;
            if (kv.second.type != Value::Type::NUMBER) throw badType(line, std::string("'") + fn + "': `" + kv.first.str + "` must be a number");
            const U u = unitOf(kv.first.str);
            if (u == U::NONE) throw badArgs(line, std::string("'") + fn + "': unknown unit key `" + kv.first.str + "` — " + kUnitList);
            out.push_back({kv.second.number, u});
        }
        return out;
    }
    throw badType(line, std::string("'") + fn + "' expects a number+unit, duration text or a map; found `" + spec.typeName() + "`");
}

static T applyParts(T t, const std::vector<DurPart>& parts, double sign, const char* fn, int line) {
    // الأشهر والسنوات أولاً (تقويمية)، ثم الباقي كمدد ثابتة
    for (auto& p : parts) if (p.unit == U::MONTH || p.unit == U::QUARTER || p.unit == U::YEAR) t = addUnit(t, sign * p.amount, p.unit, fn, line);
    for (auto& p : parts) if (!(p.unit == U::MONTH || p.unit == U::QUARTER || p.unit == U::YEAR)) t = addUnit(t, sign * p.amount, p.unit, fn, line);
    return t;
}

static Value mkSpan(double msD) {
    const bool neg = msD < 0;
    const i64 ms = static_cast<i64>(std::llround(std::fabs(msD)));
    const i64 days = ms / 86400000, hours = (ms / 3600000) % 24, minutes = (ms / 60000) % 60, seconds = (ms / 1000) % 60, rest = ms % 1000;
    std::string text;
    auto add = [&](i64 v, const char* u) { if (v) { if (!text.empty()) text += " "; text += std::to_string(v) + u; } };
    add(days, "d"); add(hours, "h"); add(minutes, "m"); add(seconds, "s");
    if (rest || text.empty()) { if (!text.empty()) text += " "; text += std::to_string(rest) + "ms"; }
    if (neg) text = "-" + text;
    // نص طويل مترجم
    const bool ar = g_cfg.lang == "ar";
    std::vector<std::string> pieces;
    auto piece = [&](i64 v, const char* en1, const char* enN, const char* a1, const char* a2, const char* aP, const char* aS) {
        if (!v) return;
        if (!ar) { pieces.push_back(loc(std::to_string(v), g_cfg.arDigits) + " " + (v == 1 ? en1 : enN)); return; }
        if (v == 1) pieces.push_back(a1);
        else if (v == 2) pieces.push_back(a2);
        else if (v >= 3 && v <= 10) pieces.push_back(loc(std::to_string(v), g_cfg.arDigits) + " " + aP);
        else pieces.push_back(loc(std::to_string(v), g_cfg.arDigits) + " " + aS);
    };
    piece(days, "day", "days", "يوم", "يومان", "أيام", "يومًا");
    piece(hours, "hour", "hours", "ساعة", "ساعتان", "ساعات", "ساعة");
    piece(minutes, "minute", "minutes", "دقيقة", "دقيقتان", "دقائق", "دقيقة");
    piece(seconds, "second", "seconds", "ثانية", "ثانيتان", "ثوانٍ", "ثانية");
    if (pieces.empty()) piece(rest, "millisecond", "milliseconds", "ميلي ثانية", "ميلي ثانيتان", "ميلي ثوانٍ", "ميلي ثانية");
    std::string lng;
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (i) lng += (i + 1 == pieces.size()) ? (ar ? " و" : " and ") : (ar ? "، " : ", ");
        lng += pieces[i];
    }
    if (lng.empty()) lng = ar ? "لا شيء" : "0 seconds";
    const double sgn = neg ? -1.0 : 1.0;
    return M7({{"type", S7("span")},
               {"ms", N7(sgn * static_cast<double>(ms))},
               {"negative", B7(neg)},
               {"days", N7(static_cast<double>(days))},
               {"hours", N7(static_cast<double>(hours))},
               {"minutes", N7(static_cast<double>(minutes))},
               {"seconds", N7(static_cast<double>(seconds))},
               {"milliseconds", N7(static_cast<double>(rest))},
               {"totalSeconds", N7(sgn * static_cast<double>(ms) / 1000.0)},
               {"totalMinutes", N7(sgn * static_cast<double>(ms) / 60000.0)},
               {"totalHours", N7(sgn * static_cast<double>(ms) / 3600000.0)},
               {"totalDays", N7(sgn * static_cast<double>(ms) / 86400000.0)},
               {"text", S7(text)},
               {"long", S7(lng)}});
}

// «منذ 3 ساعات» / «in 2 days»
static std::string arCount(i64 n, const char* s1, const char* s2, const char* pl, const char* sg, bool d) {
    if (n == 1) return s1;
    if (n == 2) return s2;
    return loc(std::to_string(n), d) + " " + (n >= 3 && n <= 10 ? pl : sg);
}
static std::string humanDelta(i64 deltaMs, bool ar, bool arDigits) {
    const i64 a = deltaMs < 0 ? -deltaMs : deltaMs;
    if (a < 5000) return ar ? "الآن" : "just now";
    const bool past = deltaMs < 0;
    i64 n; int kind;
    if (a < 60000LL) { n = a / 1000; kind = 0; }
    else if (a < 3600000LL) { n = a / 60000; kind = 1; }
    else if (a < 86400000LL) { n = a / 3600000; kind = 2; }
    else if (a < 7 * 86400000LL) { n = a / 86400000; kind = 3; }
    else if (a < 30 * 86400000LL) { n = a / (7 * 86400000LL); kind = 4; }
    else if (a < 365 * 86400000LL) { n = static_cast<i64>(static_cast<double>(a) / (30.4375 * 86400000.0)); if (n < 1) n = 1; kind = 5; }
    else { n = static_cast<i64>(static_cast<double>(a) / (365.25 * 86400000.0)); if (n < 1) n = 1; kind = 6; }
    if (ar) {
        std::string body;
        switch (kind) {
            case 0: body = arCount(n, "ثانية", "ثانيتين", "ثوانٍ", "ثانية", arDigits); break;
            case 1: body = arCount(n, "دقيقة", "دقيقتين", "دقائق", "دقيقة", arDigits); break;
            case 2: body = arCount(n, "ساعة", "ساعتين", "ساعات", "ساعة", arDigits); break;
            case 3: body = arCount(n, "يوم", "يومين", "أيام", "يومًا", arDigits); break;
            case 4: body = arCount(n, "أسبوع", "أسبوعين", "أسابيع", "أسبوعًا", arDigits); break;
            case 5: body = arCount(n, "شهر", "شهرين", "أشهر", "شهرًا", arDigits); break;
            default: body = arCount(n, "سنة", "سنتين", "سنوات", "سنة", arDigits); break;
        }
        return std::string(past ? "منذ " : "بعد ") + body;
    }
    static const char* const un[] = {"second", "minute", "hour", "day", "week", "month", "year"};
    const std::string body = loc(std::to_string(n), arDigits) + " " + un[kind] + (n == 1 ? "" : "s");
    return past ? body + " ago" : "in " + body;
}

// ------------------------------------------------------------------ مساعدات التسجيل
static bool optBool(const Value* v, bool def) { return v && v->type == Value::Type::BOOL ? v->boolean : def; }
static std::string optStr(const Value& opts, const char* k, const std::string& def) {
    const Value* v = mget(opts, k);
    return v && v->type == Value::Type::STRING ? v->str : def;
}
static int weekdayArg(const Value& v, int line, const char* fn) {
    if (v.type == Value::Type::NUMBER && v.number >= 0 && v.number <= 6) return static_cast<int>(v.number);
    if (v.type == Value::Type::STRING) { const int d = weekdayFromName(trim(v.str)); if (d >= 0) return d; }
    throw badArgs(line, std::string("'") + fn + "': يوم الأسبوع غير مفهوم — استعمل 0..6 (0=الأحد) أو اسماً مثل sat / السبت");
}
static T withZone(const T& t, const Value& zone, int line, const char* fn) {
    int off;
    if (zone.type == Value::Type::NUMBER) { off = static_cast<int>(zone.number); if (off < -840 || off > 840) throw badArgs(line, std::string("'") + fn + "': the offset (minutes) must be within ±840"); }
    else if (zone.type == Value::Type::STRING) {
        if (!parseZone(zone.str, t.ms, off))
            throw badArgs(line, std::string("'") + fn + "': منطقة زمنية غير معروفة `" + zone.str + "` — جرّب \"+03:00\" · \"UTC\" · \"Riyadh\" · \"Asia/Riyadh\" (انظر time.zones())");
    } else throw badType(line, std::string("'") + fn + "': the zone must be text (e.g. \"+03:00\") or minutes");
    return {t.ms, off};
}

} // namespace extra7

void Interpreter::registerNativesExtra7() {
    using namespace extra7;

    // ---- الإنشاء
    natives["time.now"] = [](Args& a, int line) -> Value {
        argc7("time.now", a, 0, 1, line);
        T t = nowT();
        if (a.size() == 1) t = withZone(t, a[0], line, "time.now");
        return mkTime(t);
    };
    natives["time.utc"] = [](Args& a, int line) -> Value {
        argc7("time.utc", a, 0, 1, line);
        T t = a.empty() ? nowT() : toT(a[0], line, "time.utc");
        return mkTime({t.ms, 0});
    };
    natives["time.unix"] = [](Args& a, int line) -> Value {
        argc7("time.unix", a, 0, 1, line);
        const T t = a.empty() ? nowT() : toT(a[0], line, "time.unix");
        return N7(static_cast<double>(floorDiv(t.ms, 1000)));
    };
    natives["time.ms"] = [](Args& a, int line) -> Value {
        argc7("time.ms", a, 0, 1, line);
        const T t = a.empty() ? nowT() : toT(a[0], line, "time.ms");
        return N7(static_cast<double>(t.ms));
    };
    natives["time.of"] = [](Args& a, int line) -> Value {
        argc7("time.of", a, 1, 8, line);
        Args nums = a; std::string tz;
        if (nums.back().type == Value::Type::MAP) { tz = optStr(nums.back(), "tz", optStr(nums.back(), "zone", "")); nums.pop_back(); }
        if (nums.empty() || nums.size() > 7) throw badArgs(line, "'time.of' expects (year[, month, day, hour, minute, second, ms][, {tz:\"+03:00\"}])");
        for (auto& v : nums) if (v.type != Value::Type::NUMBER) throw badType(line, "'time.of' expects numbers; found `" + v.typeName() + "`");
        std::vector<std::pair<std::string, Value>> kv;
        static const char* const keys[] = {"year", "month", "day", "hour", "minute", "second", "ms"};
        for (size_t i = 0; i < nums.size(); ++i) kv.push_back({keys[i], nums[i]});
        if (!tz.empty()) kv.push_back({"tz", S7(tz)});
        return mkTime(fromParts(M7(std::move(kv)), line, "time.of"));
    };
    natives["time.parse"] = [](Args& a, int line) -> Value {
        argc7("time.parse", a, 1, 3, line);
        if (a[0].type != Value::Type::STRING) { if (a[0].type == Value::Type::NIL) return Value::nil(); return mkTime(toT(a[0], line, "time.parse")); }
        const T now = nowT(); ParseCtx cx{now.ms, now.off};
        int forced = 0; bool hasForced = false;
        std::string fmt;
        for (size_t i = 1; i < a.size(); ++i) {
            if (a[i].type == Value::Type::STRING) fmt = a[i].str;
            else if (a[i].type == Value::Type::MAP) {
                const std::string tz = optStr(a[i], "tz", optStr(a[i], "zone", ""));
                if (!tz.empty()) { if (!parseZone(tz, now.ms, forced)) throw badArgs(line, "'time.parse': unknown zone `" + tz + "`"); hasForced = true; cx.nowOff = forced; }
                fmt = optStr(a[i], "format", fmt);
            }
        }
        T out;
        const bool ok = fmt.empty() ? parseText(a[0].str, cx, out, forced, hasForced) : parseWithFormat(a[0].str, fmt, cx, out);
        return ok ? mkTime(out) : Value::nil();
    };
    natives["time.valid"] = [](Args& a, int line) -> Value {
        argc7("time.valid", a, 1, 2, line);
        if (a[0].type == Value::Type::NUMBER) return B7(!std::isnan(a[0].number) && !std::isinf(a[0].number));
        if (isTimeMap(a[0])) return B7(true);
        if (a[0].type != Value::Type::STRING) return B7(false);
        const T now = nowT(); T out;
        if (a.size() == 2 && a[1].type == Value::Type::STRING) return B7(parseWithFormat(a[0].str, a[1].str, {now.ms, now.off}, out));
        return B7(parseText(a[0].str, {now.ms, now.off}, out, 0, false));
    };

    // ---- time(...) نفسها: الباب السهل
    natives["time"] = [this](Args& a, int line) -> Value {
        if (a.empty()) return mkTime(nowT());
        if (a.size() == 1) return mkTime(toT(a[0], line, "time"));
        if (a.size() == 2 && a[1].type == Value::Type::STRING) {          // time(x, "YYYY-MM-DD")
            return S7(formatTime(toT(a[0], line, "time"), a[1].str, g_cfg.lang == "ar", g_cfg.arDigits));
        }
        if (a.size() == 2 && a[1].type == Value::Type::MAP && !isTimeMap(a[1])) { // time(x, {lang:"ar"}) -> نص long
            const std::string lang = optStr(a[1], "lang", g_cfg.lang);
            const std::string dg = optStr(a[1], "digits", g_cfg.arDigits ? "arabic" : "latin");
            return S7(formatTime(toT(a[0], line, "time"), "long", lang == "ar", dg == "arabic"));
        }
        for (auto& v : a) if (v.type != Value::Type::NUMBER && v.type != Value::Type::MAP) throw badType(line, "'time' expects (), (x), (x, \"pattern\") or (year, month, day, ...); found `" + v.typeName() + "`");
        Args b = a;
        return natives["time.of"](b, line);   // time(2026, 10, 11, 14, 30)
    };

    // ---- العرض
    natives["time.format"] = [](Args& a, int line) -> Value {
        argc7("time.format", a, 1, 3, line);
        const T t = toT(a[0], line, "time.format");
        std::string pat = "iso"; Value opts = Value::nil();
        if (a.size() >= 2) {
            if (a[1].type == Value::Type::STRING) pat = a[1].str;
            else if (a[1].type == Value::Type::MAP) opts = a[1];
            else if (a[1].type != Value::Type::NIL) throw badType(line, "'time.format': the pattern must be text; found `" + a[1].typeName() + "`");
        }
        if (a.size() == 3) { if (a[2].type != Value::Type::MAP) throw badType(line, "'time.format': options must be a map like {lang:\"ar\", digits:\"arabic\"}"); opts = a[2]; }
        std::string lang = g_cfg.lang; bool dg = g_cfg.arDigits; T tt = t;
        if (opts.type == Value::Type::MAP) {
            lang = optStr(opts, "lang", lang);
            const std::string d = optStr(opts, "digits", dg ? "arabic" : "latin");
            dg = (d == "arabic" || d == "ar" || d == "hindi");
            const std::string tz = optStr(opts, "tz", optStr(opts, "zone", ""));
            if (!tz.empty()) tt = withZone(t, S7(tz), line, "time.format");
        }
        return S7(formatTime(tt, pat, lang == "ar", dg));
    };
    natives["time.ago"] = [](Args& a, int line) -> Value {
        argc7("time.ago", a, 1, 2, line);
        const T t = toT(a[0], line, "time.ago");
        std::string lang = g_cfg.lang; bool dg = g_cfg.arDigits; i64 ref = nowMs();
        if (a.size() == 2) {
            if (a[1].type == Value::Type::STRING) lang = a[1].str;
            else if (a[1].type == Value::Type::MAP) {
                lang = optStr(a[1], "lang", lang);
                const std::string d = optStr(a[1], "digits", dg ? "arabic" : "latin"); dg = (d == "arabic" || d == "ar");
                if (const Value* n = mget(a[1], "now")) ref = toT(*n, line, "time.ago").ms;
            } else throw badType(line, "'time.ago': options must be a language (\"ar\"/\"en\") or a map");
        }
        return S7(humanDelta(t.ms - ref, lang == "ar", dg));
    };
    natives["time.human"] = natives["time.ago"];

    // ---- الحساب
    natives["time.add"] = [](Args& a, int line) -> Value {
        argc7("time.add", a, 2, 3, line);
        const T t = toT(a[0], line, "time.add");
        return mkTime(applyParts(t, durationParts(a[1], a.size() == 3 ? &a[2] : nullptr, line, "time.add"), 1.0, "time.add", line));
    };
    natives["time.sub"] = [](Args& a, int line) -> Value {
        argc7("time.sub", a, 2, 3, line);
        const T t = toT(a[0], line, "time.sub");
        return mkTime(applyParts(t, durationParts(a[1], a.size() == 3 ? &a[2] : nullptr, line, "time.sub"), -1.0, "time.sub", line));
    };
    natives["time.diff"] = [](Args& a, int line) -> Value {
        argc7("time.diff", a, 2, 3, line);
        const T x = toT(a[0], line, "time.diff"), y = toT(a[1], line, "time.diff");
        if (a.size() == 2) return mkSpan(static_cast<double>(x.ms - y.ms));
        const U u = needUnit(a[2], "time.diff", line);
        if (u == U::MONTH || u == U::QUARTER || u == U::YEAR) {
            const T xa{x.ms, y.off};
            const Parts pa = breakdown(xa.ms, y.off), pb = breakdown(y.ms, y.off);
            i64 months = (pa.year * 12 + pa.month) - (pb.year * 12 + pb.month);
            const T anchor = addMonths(y, months);
            if (months > 0 && anchor.ms > xa.ms) --months;
            else if (months < 0 && anchor.ms < xa.ms) ++months;
            const i64 div = u == U::MONTH ? 1 : u == U::QUARTER ? 3 : 12;
            return N7(static_cast<double>(months / div));
        }
        return N7(static_cast<double>(x.ms - y.ms) / static_cast<double>(fixedMs(u)));
    };
    natives["time.startOf"] = [](Args& a, int line) -> Value {
        argc7("time.startOf", a, 2, 2, line);
        return mkTime(startOfUnit(toT(a[0], line, "time.startOf"), needUnit(a[1], "time.startOf", line)));
    };
    natives["time.endOf"] = [](Args& a, int line) -> Value {
        argc7("time.endOf", a, 2, 2, line);
        return mkTime(endOfUnit(toT(a[0], line, "time.endOf"), needUnit(a[1], "time.endOf", line)));
    };
    natives["time.span"] = [](Args& a, int line) -> Value {
        argc7("time.span", a, 1, 2, line);
        if (a[0].type == Value::Type::NUMBER) {
            double ms = a[0].number;
            if (a.size() == 2) ms = a[0].number * static_cast<double>(fixedMs(needUnit(a[1], "time.span", line)) ? fixedMs(needUnit(a[1], "time.span", line)) : 1);
            return mkSpan(ms);
        }
        if (isTimeMap(a[0])) throw badType(line, "'time.span' takes a duration, not a time — use time.diff(a, b) to get the span between two times");
        return mkSpan(durationMs(durationParts(a[0], nullptr, line, "time.span")));
    };
    natives["time.range"] = [](Args& a, int line) -> Value {
        argc7("time.range", a, 2, 4, line);
        const T s = toT(a[0], line, "time.range"), e = toT(a[1], line, "time.range");
        std::vector<DurPart> step;
        if (a.size() == 2) step = {{1, U::DAY}};
        else step = durationParts(a[2], a.size() == 4 ? &a[3] : nullptr, line, "time.range");
        double stepMs = durationMs(step);
        if (stepMs <= 0) throw badArgs(line, "'time.range': the step must be positive");
        const double sign = e.ms >= s.ms ? 1.0 : -1.0;
        std::vector<Value> out;
        for (i64 k = 0; k <= 100000; ++k) {
            T cur = s;
            std::vector<DurPart> scaled = step;
            for (auto& p : scaled) p.amount *= static_cast<double>(k);
            cur = applyParts(s, scaled, sign, "time.range", line);
            if ((sign > 0 && cur.ms > e.ms) || (sign < 0 && cur.ms < e.ms)) break;
            if (k == 100000) throw diagErr(diag::Code::E0035_RuntimeError, line, "'time.range': the range would hold more than 100,000 items — use a larger step");
            out.push_back(mkTime(cur));
        }
        return A7(std::move(out));
    };

    // ---- المقارنة والفحص
    natives["time.before"] = [](Args& a, int line) -> Value { argc7("time.before", a, 2, 2, line); return B7(toT(a[0], line, "time.before").ms < toT(a[1], line, "time.before").ms); };
    natives["time.after"] = [](Args& a, int line) -> Value { argc7("time.after", a, 2, 2, line); return B7(toT(a[0], line, "time.after").ms > toT(a[1], line, "time.after").ms); };
    natives["time.same"] = [](Args& a, int line) -> Value {
        argc7("time.same", a, 2, 3, line);
        T x = toT(a[0], line, "time.same"), y = toT(a[1], line, "time.same");
        if (a.size() == 2) return B7(x.ms == y.ms);
        const U u = needUnit(a[2], "time.same", line);
        y = {y.ms, x.off};
        return B7(startOfUnit(x, u).ms == startOfUnit(y, u).ms);
    };
    natives["time.between"] = [](Args& a, int line) -> Value {
        argc7("time.between", a, 3, 4, line);
        const i64 t = toT(a[0], line, "time.between").ms;
        i64 lo = toT(a[1], line, "time.between").ms, hi = toT(a[2], line, "time.between").ms;
        if (lo > hi) std::swap(lo, hi);
        const bool inc = a.size() == 4 ? optBool(&a[3], true) : true;
        return B7(inc ? (t >= lo && t <= hi) : (t > lo && t < hi));
    };
    natives["time.min"] = [](Args& a, int line) -> Value {
        Args v = a.size() == 1 && a[0].type == Value::Type::ARRAY ? *a[0].array : a;
        if (v.empty()) throw badArgs(line, "'time.min' needs at least one time");
        T best = toT(v[0], line, "time.min");
        for (auto& x : v) { T c = toT(x, line, "time.min"); if (c.ms < best.ms) best = c; }
        return mkTime(best);
    };
    natives["time.max"] = [](Args& a, int line) -> Value {
        Args v = a.size() == 1 && a[0].type == Value::Type::ARRAY ? *a[0].array : a;
        if (v.empty()) throw badArgs(line, "'time.max' needs at least one time");
        T best = toT(v[0], line, "time.max");
        for (auto& x : v) { T c = toT(x, line, "time.max"); if (c.ms > best.ms) best = c; }
        return mkTime(best);
    };
    natives["time.is"] = [](Args& a, int line) -> Value {
        argc7("time.is", a, 2, 2, line);
        const T t = toT(a[0], line, "time.is");
        if (a[1].type != Value::Type::STRING) throw badType(line, "'time.is' expects a check name as text");
        const std::string w = lower(trim(a[1].str));
        const T now = nowT();
        const Parts p = breakdown(t.ms, t.off);
        const T tn{now.ms, t.off};
        if (w == "today") return B7(startOfUnit(t, U::DAY).ms == startOfUnit(tn, U::DAY).ms);
        if (w == "tomorrow") return B7(startOfUnit(t, U::DAY).ms == startOfUnit(tn, U::DAY).ms + 86400000);
        if (w == "yesterday") return B7(startOfUnit(t, U::DAY).ms == startOfUnit(tn, U::DAY).ms - 86400000);
        if (w == "past") return B7(t.ms < now.ms);
        if (w == "future") return B7(t.ms > now.ms);
        if (w == "weekend") return B7(g_cfg.weekend[p.weekday]);
        if (w == "weekday") return B7(!g_cfg.weekend[p.weekday]);
        if (w == "leap") return B7(isLeapYear(p.year));
        throw badArgs(line, "'time.is': unknown check `" + a[1].str + "` — المتاح: today tomorrow yesterday past future weekend weekday leap");
    };
    natives["time.isLeap"] = [](Args& a, int line) -> Value {
        argc7("time.isLeap", a, 1, 1, line);
        if (a[0].type == Value::Type::NUMBER && std::fabs(a[0].number) < 100000) return B7(isLeapYear(static_cast<i64>(a[0].number)));
        return B7(isLeapYear(breakdown(toT(a[0], line, "time.isLeap").ms, toT(a[0], line, "time.isLeap").off).year));
    };
    natives["time.daysIn"] = [](Args& a, int line) -> Value {
        argc7("time.daysIn", a, 1, 2, line);
        if (a.size() == 2) {
            if (a[0].type != Value::Type::NUMBER || a[1].type != Value::Type::NUMBER) throw badType(line, "'time.daysIn' expects (year, month) as numbers");
            const int m = static_cast<int>(a[1].number);
            if (m < 1 || m > 12) throw badArgs(line, "'time.daysIn': month must be 1..12");
            return N7(daysInMonth(static_cast<i64>(a[0].number), m));
        }
        const T t = toT(a[0], line, "time.daysIn"); const Parts p = breakdown(t.ms, t.off);
        return N7(daysInMonth(p.year, p.month));
    };

    // ---- المناطق الزمنية
    natives["time.zone"] = [](Args& a, int line) -> Value {
        argc7("time.zone", a, 1, 2, line);
        T t = toT(a[0], line, "time.zone");
        if (a.size() == 1) return mkTime({t.ms, localOffsetMin(t.ms)});
        return mkTime(withZone(t, a[1], line, "time.zone"));
    };
    natives["time.zones"] = [](Args& a, int line) -> Value {
        argc7("time.zones", a, 0, 0, line);
        std::vector<Value> out;
        for (const Zone& z : kZones) out.push_back(M7({{"name", S7(z.name)}, {"offset", N7(z.off)}, {"tz", S7(z.off == 0 ? "Z" : offStr(z.off))}}));
        return A7(std::move(out));
    };

    // ---- التقويم الهجري
    natives["time.hijri"] = [](Args& a, int line) -> Value {
        argc7("time.hijri", a, 0, 2, line);
        const T t = a.empty() ? nowT() : toT(a[0], line, "time.hijri");
        bool ar = g_cfg.lang == "ar", dg = g_cfg.arDigits;
        if (a.size() == 2 && a[1].type == Value::Type::MAP) { ar = optStr(a[1], "lang", ar ? "ar" : "en") == "ar"; const std::string d = optStr(a[1], "digits", dg ? "arabic" : "latin"); dg = (d == "arabic" || d == "ar"); }
        const i64 days = floorDiv(t.ms + static_cast<i64>(t.off) * 60000, 86400000);
        i64 hy; int hm, hd; hijriFromDays(days, hy, hm, hd);
        const std::string mname = ar ? kHijriAr[hm - 1] : kHijriEn[hm - 1];
        const std::string text = loc(std::to_string(hd), dg) + " " + mname + " " + loc(std::to_string(hy), dg) + (ar ? " هـ" : " AH");
        return M7({{"type", S7("hijri")}, {"year", N7(static_cast<double>(hy))}, {"month", N7(hm)}, {"day", N7(hd)},
                   {"monthName", S7(mname)}, {"text", S7(text)}, {"iso", S7(pad(hy, 4) + "-" + pad(hm, 2) + "-" + pad(hd, 2))},
                   {"approximate", B7(true)}});
    };
    natives["time.fromHijri"] = [](Args& a, int line) -> Value {
        argc7("time.fromHijri", a, 3, 3, line);
        for (auto& v : a) if (v.type != Value::Type::NUMBER) throw badType(line, "'time.fromHijri' expects (year, month, day) as numbers");
        const i64 y = static_cast<i64>(a[0].number); const int m = static_cast<int>(a[1].number), d = static_cast<int>(a[2].number);
        if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 30) throw badArgs(line, "'time.fromHijri': year 1..9999, month 1..12, day 1..30");
        const i64 days = daysFromHijri(y, m, d);
        const T now = nowT();
        return mkTime({days * 86400000LL - static_cast<i64>(now.off) * 60000LL, now.off});
    };

    // ---- التوقيت والانتظار
    natives["time.sleep"] = [](Args& a, int line) -> Value {
        argc7("time.sleep", a, 1, 2, line);
        double ms;
        if (a[0].type == Value::Type::NUMBER) ms = a.size() == 2 ? a[0].number * static_cast<double>(std::max<i64>(1, fixedMs(needUnit(a[1], "time.sleep", line)))) : a[0].number;
        else ms = durationMs(durationParts(a[0], nullptr, line, "time.sleep"));
        if (std::isnan(ms) || ms < 0) throw badArgs(line, "'time.sleep': the duration cannot be negative");
        if (ms > 600000.0) throw badArgs(line, "'time.sleep': the maximum is 10 minutes (600000 ms)");
        i64 left = static_cast<i64>(ms);
        while (left > 0) { const i64 c = std::min<i64>(left, 50); std::this_thread::sleep_for(std::chrono::milliseconds(c)); left -= c; }
        return B7(true);
    };
    natives["time.stopwatch"] = [](Args& a, int line) -> Value {
        argc7("time.stopwatch", a, 0, 0, line);
        const double mono = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        return M7({{"type", S7("stopwatch")}, {"startedAt", N7(mono)}, {"startedEpoch", N7(static_cast<double>(nowMs()))}});
    };
    natives["time.elapsed"] = [](Args& a, int line) -> Value {
        argc7("time.elapsed", a, 1, 2, line);
        const Value* st = mget(a[0], "startedAt");
        if (!st || st->type != Value::Type::NUMBER) throw badType(line, "'time.elapsed' expects a stopwatch from time.stopwatch()");
        const double mono = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        double ms = mono - st->number;
        if (a.size() == 2) { const U u = needUnit(a[1], "time.elapsed", line); if (fixedMs(u) == 0) throw badArgs(line, "'time.elapsed': use ms/s/m/h/d/w"); return N7(ms / static_cast<double>(fixedMs(u))); }
        return N7(ms);
    };
    natives["time.measure"] = [this](Args& a, int line) -> Value {
        if (a.empty() || a[0].type != Value::Type::FUNCTION || !a[0].function) throw badType(line, "'time.measure' expects a function: time.measure(fn[, args...])");
        const auto t0 = std::chrono::steady_clock::now();
        Args callArgs(a.begin() + 1, a.end());
        Value result = callFunction(a[0].function, callArgs, line);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const Value sp = mkSpan(ms);
        return M7({{"result", result}, {"ms", N7(ms)}, {"seconds", N7(ms / 1000.0)}, {"text", *mget(sp, "text")}});
    };

    // ---- الإعدادات
    natives["time.config"] = [](Args& a, int line) -> Value {
        argc7("time.config", a, 0, 1, line);
        if (a.size() == 1) {
            if (a[0].type != Value::Type::MAP) throw badType(line, "'time.config' expects a map: {lang:\"ar\", digits:\"arabic\", weekStart:\"sat\", weekend:[\"fri\",\"sat\"]}");
            for (auto& kv : *a[0].map) {
                if (kv.first.type != Value::Type::STRING) continue;
                const std::string& k = kv.first.str; const Value& v = kv.second;
                if (k == "lang") {
                    if (v.type != Value::Type::STRING || (lower(v.str) != "ar" && lower(v.str) != "en")) throw badArgs(line, "'time.config': lang must be \"ar\" or \"en\"");
                    g_cfg.lang = lower(v.str);
                } else if (k == "digits") {
                    if (v.type != Value::Type::STRING) throw badArgs(line, "'time.config': digits must be \"arabic\" or \"latin\"");
                    const std::string d = lower(v.str);
                    if (d != "arabic" && d != "latin" && d != "ar" && d != "en") throw badArgs(line, "'time.config': digits must be \"arabic\" or \"latin\"");
                    g_cfg.arDigits = (d == "arabic" || d == "ar");
                } else if (k == "weekStart") {
                    g_cfg.weekStart = weekdayArg(v, line, "time.config");
                } else if (k == "weekend") {
                    if (v.type != Value::Type::ARRAY) throw badArgs(line, "'time.config': weekend must be an array like [\"fri\",\"sat\"]");
                    bool w[7] = {false, false, false, false, false, false, false};
                    for (auto& d : *v.array) w[weekdayArg(d, line, "time.config")] = true;
                    for (int i = 0; i < 7; ++i) g_cfg.weekend[i] = w[i];
                } else throw badArgs(line, "'time.config': unknown option `" + k + "` — lang, digits, weekStart, weekend");
            }
        }
        std::vector<Value> we;
        for (int i = 0; i < 7; ++i) if (g_cfg.weekend[i]) we.push_back(S7(kDayEn[i]));
        return M7({{"lang", S7(g_cfg.lang)}, {"digits", S7(g_cfg.arDigits ? "arabic" : "latin")},
                   {"weekStart", S7(kDayEn[g_cfg.weekStart])}, {"weekend", A7(std::move(we))}});
    };
}

} // namespace rin

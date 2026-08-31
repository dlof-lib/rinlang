// cli/linux/src/pkg/toml_lite.h
// ============================================================================
// RinPM :: محلّل TOML مصغّر — يدعم فقط المجموعة الفرعية التي يحتاجها rin.toml
// و rin.lock فعلياً: عناوين أقسام [section] و[section.sub]، مصفوفات الجداول
// [[array_of_tables]]، أزواج key = value حيث القيمة نص "..." أو رقم أو bool
// أو مصفوفة نصوص ["a", "b"]. هذا ليس محلّل TOML عاماً (لا جداول مضمّنة inline
// {}؛ لا تواريخ)، لكنه كافٍ تماماً وواضح الأخطاء لصيغة الحزم في Rin.
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>

namespace rinpm::toml {

struct ParseError : std::runtime_error {
    int line;
    ParseError(const std::string& msg, int line_) : std::runtime_error(msg), line(line_) {}
};

// قيمة TOML مبسّطة: نص، أو مصفوفة نصوص. (الأرقام/bool تُخزَّن كنص وتُحوَّل عند الحاجة
// من قِبل الطبقة الأعلى -- يبقي هذا المحلل بسيطاً وقابلاً للتنبؤ به).
struct Value {
    enum class Kind { String, StringArray, Bool, Int } kind = Kind::String;
    std::string str;
    std::vector<std::string> arr;
    bool boolean = false;
    long long integer = 0;
};

using Table = std::map<std::string, Value>;

struct Document {
    // أقسام عادية: "package" -> جدول، "package.rin" -> جدول (المفتاح هو المسار الكامل بالنقاط)
    std::map<std::string, Table> sections;
    // ترتيب ظهور الأقسام العادية كما وردت في الملف (لأغراض عرض/تصحيح فقط)
    std::vector<std::string> sectionOrder;
    // مصفوفات الجداول [[name]] -> قائمة صفوف بالترتيب
    std::map<std::string, std::vector<Table>> arrayTables;
    std::vector<std::string> arrayTableOrder;

    bool hasSection(const std::string& name) const { return sections.count(name) != 0; }
    const Table* section(const std::string& name) const {
        auto it = sections.find(name);
        return it == sections.end() ? nullptr : &it->second;
    }
};

// يحلل نص TOML مصغّر. يرمي ParseError مع رقم السطر عند أي خطأ في الصيغة.
Document parse(const std::string& text);

// أدوات مساعدة لقراءة قيمة من جدول بأمان (تعيد قيمة افتراضية إن غابت).
std::string getStr(const Table& t, const std::string& key, const std::string& def = "");
bool hasKey(const Table& t, const std::string& key);
std::vector<std::string> getStrArray(const Table& t, const std::string& key);

// ---------------------------------------------------------------------------
// كتابة TOML: أدوات بناء تسلسلية بسيطة (تُستخدم من Manifest/Lockfile عند التوليد)
// ---------------------------------------------------------------------------
class Writer {
public:
    void section(const std::string& name);
    void beginArrayTable(const std::string& name); // يفتح صفاً جديداً [[name]]
    void kv(const std::string& key, const std::string& value);   // "value" مقتبسة
    void kvRaw(const std::string& key, const std::string& rawValue); // بلا اقتباس (أرقام/bool)
    void kvArray(const std::string& key, const std::vector<std::string>& values);
    void blank();
    std::string str() const { return out_; }
private:
    std::string out_;
};

std::string quote(const std::string& s);

} // namespace rinpm::toml

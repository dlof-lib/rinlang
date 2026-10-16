// ============================================================================
//  rin terminal — الطرفية التفاعلية الكاملة للغة Rin (Linux / POSIX)
// ============================================================================
//  بديل حقيقي لـ REPL السطر الواحد القديم. لا مكتبات خارجية (لا readline ولا
//  linenoise): محرّر سطر مكتوب بالكامل فوق termios، ويستخدم نفس محرّك
//  المفسّر (rin::Interpreter) المُستخدَم في `rin run` وتطبيق RinStudio، فتبقى
//  الحالة (let / fun / class ...) حيّة بين الأوامر داخل الجلسة.
//
//  تُستدعى من cli/linux/src/main.cpp:
//      rin                       (عند وجود tty)
//      rin terminal [خيارات] [ملفات.rin ...]      (أو rin repl / rin shell)
//
//  التفاصيل الكاملة: docs/terminal.md
// ============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rin {
namespace terminal {

struct Options {
    bool color      = true;   // يُلغى تلقائياً عند NO_COLOR / TERM=dumb / غير tty
    bool highlight  = true;   // تلوين الصيغة أثناء الكتابة
    bool hints      = true;   // اقتراح من التاريخ (نص باهت يُقبَل بـ → أو End)
    bool autoprint  = true;   // طباعة قيمة التعبير الحرّ (1 + 2  ←  => 3)
    bool useHistory = true;   // قراءة/كتابة ملف التاريخ
    bool banner     = true;   // ترويسة البدء
    bool forceBatch = false;  // وضع بلا tty حتى لو كان الطرف tty (للاختبار)
    std::string timing = "auto";     // auto | on | off
    std::string theme  = "dark";     // dark | light
    std::string historyFile;         // يتجاوز المسار الافتراضي
    std::vector<std::string> preload;  // ملفات تُحمَّل قبل أول مؤشّر (-i / ملفات موضعية)
    std::vector<std::string> evalFirst; // مقاطع -e تُنفَّذ قبل أول مؤشّر
    std::string version;
};

// يحلّل وسائط `rin terminal ...`. يعيد false مع err عند وسيط غير معروف.
// عند --help يضبط showHelp ويعيد true.
bool parseArgs(const std::vector<std::string>& args, Options& opt, std::string& err, bool& showHelp);

void printUsage();

// `rin indsin <ملف.rin> [--width N] [--dump|--plain]`: يشغّل واجهة indsin داخل الطرفية مباشرة.
int runIndsinCommand(const std::vector<std::string>& args);

// `rin img <صورة> [--width N] [--mode half|braille|shade|ascii] [--plain] [--invert]`: يعرض صورة بحروف Unicode
// (نصف-كتل ملوّنة أو نقاط براي أو ظلال) عبر ImageMagick. يعيد 0 نجاحاً؛ 1 فشل؛ 2 وسائط خاطئة.
int runImgCommand(const std::vector<std::string>& args);

// ---------------------------------------------------------------------------
// EmbeddedTerminal — نفس جلسة `rin terminal` (مفسّر دائم الحالة + أوامر `:`) لكن بلا tty:
// تُغذّى سطراً بسطر وتُسلِّم مخرجاتها وطلبات input() عبر دوال. تستعملها طرفية محرّر RinStudio
// على أندرويد عبر JNI (app/src/main/cpp/terminal/rin_terminal_jni.cpp). خيط واحد في كل مرة.
// لا تعمل فيها :sh و:edit و:pkg و:indsin (تحتاج شلاً/tty) — تُرجع رسالة خطأ واضحة.
// ---------------------------------------------------------------------------
struct EmbedConfig {
    std::string cwd;        // مجلد الجلسة الابتدائي (مجلد المشروع)؛ :cd يغيّره داخل الجلسة فقط
    bool english = false;   // لغة رسائل الجلسة
};

class EmbeddedTerminal {
public:
    enum class Result { Ok, Failed, NeedMore, Quit };
    using OutputFn = std::function<void(const std::string&)>;
    using InputFn = std::function<bool(const std::string& prompt, std::string& answer)>;   // false = أُلغي الإدخال

    explicit EmbeddedTerminal(const EmbedConfig& cfg);
    ~EmbeddedTerminal();
    EmbeddedTerminal(const EmbeddedTerminal&) = delete;
    EmbeddedTerminal& operator=(const EmbeddedTerminal&) = delete;

    // سطر واحد؛ NeedMore = المدخل ناقص (قوس/نص مفتوح) فأرسل السطر التالي.
    Result feed(const std::string& line, const OutputFn& out, const InputFn& in);
    void cancelPending();
    bool continuing() const;
    void reset();                                   // جلسة جديدة (يمسح كل المتغيرات)
    std::vector<std::string> variableNames();       // ما عرّفه المستخدم في الجلسة

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// الحلقة الرئيسية. تعيد رمز الخروج (0 نجاح؛ 1 إن فشل أمر في الوضع غير التفاعلي).
int run(const Options& opt);

} // namespace terminal
} // namespace rin

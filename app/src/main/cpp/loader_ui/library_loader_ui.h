// app/src/main/cpp/loader_ui/library_loader_ui.h
// ============================================================================
// Rin :: Library Loader UI — وحدة عامة مستقلة تماماً عن أي مُفسِّر.
//
// الهدف: أي مكان يحمّل مكتبة (سواء @import الحالي في rin_interpreter.cpp،
// أو مُفسِّر @import مستقبلي، أو حتى RinPM) يستطيع الإبلاغ عن مراحل تحميل
// *حقيقية* (وليست Animation وهمية) عبر واجهة صغيرة: LoadReporter.
//
// المراحل الثمانية (تطابق تماماً ما يحدث فعلياً عند @import في
// rin_interpreter.cpp، راجع التعليق أعلى كل enumerator):
//
//   Resolving      10%   تحويل اسم عارٍ إلى مسار (lib/<name>.og.rin)
//   Locating       25%   البحث: مكتبة مدمجة embedded؟ ثم قرص؟ ثم حزمة RinPM؟
//   Reading        40%   قراءة المصدر فعلياً (ifstream / embedded map / حزمة)
//   Parsing        55%   Lexer + Parser -> AST (importedStatements)
//   Dependencies   70%   تنفيذ أي @import متداخلة داخل المكتبة نفسها (تكراري)
//   Registering    85%   دمج مباشر في env، أو تسجيل alias كحاوية (containers[])
//   Initializing   95%   executeBlock الفعلي لتعريفات المكتبة أعلى المستوى
//   Completed      100%  importedPaths.insert + نجاح الاستيراد
//
// هذه الوحدة لا تعرف شيئاً عن Lexer/Parser/Environment — فقط تستقبل أحداثاً
// نصية بسيطة (اسم المكتبة + المرحلة + تفاصيل اختيارية) وتعرضها، أو تتجاهلها
// تماماً في وضع الصمت. الربط الفعلي بـ @import يكون لاحقاً بإضافة نداءات
// reporter.stage(...) بين الأسطر الموجودة فعلاً في rin_interpreter.cpp
// (أرقام الأسطر مذكورة أعلاه) دون تغيير أي منطق تنفيذي هناك.
// ============================================================================
#pragma once

#include <cstddef>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace rin::loaderui {

// ----------------------------------------------------------------------------
// المراحل الثمانية لتحميل مكتبة واحدة، بالترتيب الحقيقي الذي تحدث فيه.
// القيمة العددية = النسبة المئوية المعروضة (وليست ترتيباً تعسفياً)، بحيث لا
// حاجة لجدول تحويل منفصل: percentOf(stage) تعيد enum value مباشرة.
// ----------------------------------------------------------------------------
enum class LoadStage : int {
    Resolving    = 10,
    Locating     = 25,
    Reading      = 40,
    Parsing      = 55,
    Dependencies = 70,
    Registering  = 85,
    Initializing = 95,
    Completed    = 100,
};

// نص عرض عربي/إنجليزي مختصر لكل مرحلة (يُستخدم من الـ sinks الافتراضية،
// وأي sink مخصص حر في تجاهله واستخدام نصوصه الخاصة).
const char* stageLabel(LoadStage stage);

inline int percentOf(LoadStage stage) { return static_cast<int>(stage); }

// نتيجة استيراد مكتبة واحدة (تُمرَّر إلى onEnd/finish).
struct LoadOutcome {
    bool success = true;
    bool fromEmbedded = false;   // مكتبة مدمجة داخل المفسّر أم من القرص/حزمة
    bool fromCache = false;      // مستوردة مسبقاً في نفس التشغيل (تكرار مُتجاهَل)
    std::string errorMessage;    // فقط إن success == false
};

// ----------------------------------------------------------------------------
// ILoadSink — الواجهة التي يطبّقها أي "عارض" لأحداث التحميل.
//
// هذا هو نقطة التوسّع المطلوبة صراحةً في الطلب: تفعيل/تعطيل الإخراج حسب
// السياق (Quiet/Silent، IDE Background Loading، Live Preview) يتم ببساطة
// باستبدال الـ sink، دون أي تغيير في منطق الاستدعاء من جهة المُفسِّر.
// ----------------------------------------------------------------------------
class ILoadSink {
public:
    virtual ~ILoadSink() = default;

    // بداية استيراد مكتبة (قد تكون متداخلة: depth > 0 يعني أنها تبعية مكتبة أخرى قيد التحميل
    // حالياً — يُستخدَم لعرض شجرة كما في مثال rin.ui).
    virtual void onBegin(const std::string& libraryName, std::size_t depth) = 0;

    // تحديث المرحلة الحالية لمكتبة معيّنة (قد يُستدعى عدة مرات بترتيب تصاعدي).
    // detail: نص اختياري إضافي (مثال: "Resolving dependencies..."). depth: نفس عمق onBegin لهذه
    // الجلسة بعينها — يُمرَّر صراحة هنا بدل الاعتماد على تخمين داخلي (مطابقة اسم، ترتيب استدعاء...)
    // حتى يستطيع أي sink مركّب (كـ ConsoleBarSink) تمييز أحداث المكتبة الأم عن أحداث تبعياتها
    // بثقة، حتى مع تسمية متكررة أو تعشيش أعمق من مستوى واحد.
    virtual void onStage(const std::string& libraryName, LoadStage stage,
                          const std::string& detail, std::size_t depth) = 0;

    // انتهاء استيراد مكتبة (نجاحاً أو فشلاً). depth: كما في onStage أعلاه.
    virtual void onEnd(const std::string& libraryName, const LoadOutcome& outcome, std::size_t depth) = 0;
};

// ----------------------------------------------------------------------------
// SilentSink — تنفيذ لا-عملية (no-op) لكل الأحداث.
// يُستخدم في: Quiet/Silent Mode صراحةً، أو IDE Background Loading، أو أي
// سياق لا نريد فيه أي Output على الإطلاق. الأداء: كل نداء مضمون inline/فارغ،
// بلا أي تكلفة تُذكر حتى لو استُدعي آلاف المرات (تحميل شجرة تبعيات كبيرة).
// ----------------------------------------------------------------------------
class SilentSink final : public ILoadSink {
public:
    void onBegin(const std::string&, std::size_t) override {}
    void onStage(const std::string&, LoadStage, const std::string&, std::size_t) override {}
    void onEnd(const std::string&, const LoadOutcome&, std::size_t) override {}
};

// ----------------------------------------------------------------------------
// قدرات الطرفية (تُكتشف فعلياً، لا تُفترض): TTY؟ ألوان؟ Unicode؟ عرض النافذة؟
//
// قواعد الكشف (بالترتيب):
//   * الدفق ليس std::cout/std::cerr (مثلاً ostringstream) أو ليس طرفية حقيقية => plain (بلا أنيميشن).
//   * NO_COLOR (أي قيمة) أو TERM=dumb => بلا ألوان؛ CLICOLOR_FORCE=1 يفرضها.
//   * اللغة (LC_ALL/LC_CTYPE/LANG) صريحة وليست UTF-8 (أو TERM=linux) => رموز ASCII بديلة.
//   * RIN_PROGRESS=plain يعطّل الأنيميشن، RIN_PROGRESS=fancy يفرضه (للاختبار/CI الذي يدعم ANSI).
//   * العرض: ioctl(TIOCGWINSZ) ثم COLUMNS ثم 80.
// ----------------------------------------------------------------------------
struct TermCaps {
    bool tty = false;       // الدفق طرفية حقيقية
    bool animate = false;   // يُسمح بإعادة الرسم في نفس الموضع (ANSI cursor control)
    bool color = false;     // ألوان ANSI
    bool color256 = false;  // لوحة xterm-256 (وإلا 16 لوناً)
    bool unicode = true;    // رموز Unicode (✔ ━ ⠋ …) وإلا ASCII
    int  cols = 80;         // عرض الطرفية بالأعمدة
    int  accent256 = 141;   // لون التمييز (xterm-256)؛ يتغيّر حسب نوع الأمر (TaskProfile)
    int  accentBasic = 35;  // مقابله في 16 لوناً (ANSI SGR)
};

TermCaps detectTermCaps(const std::ostream& out);

// ----------------------------------------------------------------------------
// أنواع المؤشرات. تُختار برمجياً (Options/ctor) أو بيئياً:
//   RIN_PROGRESS_BAR=line|block|dots|segments|shade
//   RIN_PROGRESS_SPINNER=braille|dots|line|arc|bounce|pulse|arrow|grow|slide
// بلا Unicode تتراجع كلها تلقائياً إلى ASCII ([=====>---] و | / - \).
//
//   line      ━━━━╸────      خط رفيع بدقة نصف خانة (الافتراضي)
//   block     ████▌░░░░      كتل بدقة 1/8 خانة
//   dots      ●●●●○○○○       نقاط
//   segments  ▰▰▰▰▱▱▱▱       أقسام
//   shade     ▓▓▓▓░░░░       تظليل
// ----------------------------------------------------------------------------
enum class BarStyle { Line, Block, Dots, Segments, Shade };
enum class SpinnerStyle { Braille, Dots, Line, Arc, Bounce, Pulse, Arrow, Grow, Slide };

struct IndicatorStyle {
    BarStyle bar = BarStyle::Line;
    SpinnerStyle spinner = SpinnerStyle::Braille;
};

bool parseBarStyle(const std::string& name, BarStyle& out);
bool parseSpinnerStyle(const std::string& name, SpinnerStyle& out);
const std::vector<std::string>& barStyleNames();
const std::vector<std::string>& spinnerStyleNames();
IndicatorStyle styleFromEnv();   // يقرأ RIN_PROGRESS_BAR / RIN_PROGRESS_SPINNER (قيم غير معروفة تُتجاهل)

// ----------------------------------------------------------------------------
// نوع الأمر (Task) — لكل نوع أمر هويّة مؤشر خاصة: فعل، وحدة عدّ، لون، شكل شريط وspinner،
// وهل يُعرض الوقت المتبقي (ETA) والسرعة.
//
//   Install  "Installing"  pkgs    بنفسجي  braille + line      ETA
//   Test     "Testing"     tests   أخضر    arc     + shade     ETA + سرعة
//   Build    "Building"    steps   برتقالي  grow    + block     ETA
//   Check    "Checking"    files   سماوي   dots    + segments  ETA + سرعة
//   Format   "Formatting"  files   وردي    line    + dots      ETA
//   Clean    "Cleaning"    items   أزرق    bounce  + segments
//   Doctor   "Diagnosing"  checks  أخضر فاتح pulse + dots
//   Run      "Running"             بنفسجي  slide   + line
// ----------------------------------------------------------------------------
enum class Task { Generic, Install, Test, Build, Check, Format, Clean, Doctor, Run };

struct TaskProfile {
    std::string verb;          // "Installing"
    std::string unit;          // "pkgs" (يظهر بعد العدّاد: 3/12 pkgs)
    IndicatorStyle style;
    int accent256 = 141;
    int accentBasic = 35;
    bool eta = false;          // عرض الوقت المتبقي المقدَّر (من معدّل الإنجاز الفعلي)
    bool rate = false;         // عرض السرعة (وحدة/ثانية)
};
TaskProfile profileFor(Task task);   // RIN_PROGRESS_BAR / RIN_PROGRESS_SPINNER تتغلّب على النمط الافتراضي للنوع

// "412ms" / "1.4s" / "1m 05s" — مدة مقروءة موحّدة لكل العارضات.
std::string formatDuration(double seconds);

// ----------------------------------------------------------------------------
// ConsoleBarSink — العارض الافتراضي لـ @import. ثلاثة أوضاع تُختار تلقائياً:
//
//  * حي (طرفية حقيقية): سطر تقدّم متحرك مع spinner وشريط ناعم ونسبة ومرحلة ومدة، وتحته شجرة
//    التبعيات بأي عمق (├─ └─ │) تتحدّث في مكانها. عند الانتهاء يُستبدل الكل بملخص نظيف
//    (✔ name loaded · embedded · 0.21s) مع الشجرة النهائية.
//  * نظيف (pipe/ملف/ostringstream/CI): سطر لكل حدث، بلا ESC ولا \r — مناسب للسجلات.
//  * صامت: SilentSink (Mode::Quiet).
//
// كل ما يُعرض حقيقي: النسب والمراحل تأتي من LoadSession فعلياً، والمدد من steady_clock.
// ----------------------------------------------------------------------------
class ConsoleBarSink final : public ILoadSink {
public:
    enum class Look { Auto, Live, Plain };
    struct Options {
        int  barWidth = 24;        // عرض الشريط الأقصى (يتقلّص تلقائياً في الطرفيات الضيقة)
        Look look = Look::Auto;    // Auto: كشف تلقائي؛ Live/Plain: فرض
        bool showElapsed = true;   // عرض المدة بجانب كل خطوة
        IndicatorStyle style = styleFromEnv();   // شكل الشريط والـ spinner
    };

    explicit ConsoleBarSink(std::ostream& out, const Options& opts);
    // توافق مع الواجهة القديمة: useCarriageReturn=false => Look::Plain.
    explicit ConsoleBarSink(std::ostream& out, int barWidth = 24, bool useCarriageReturn = true);
    ~ConsoleBarSink() override;

    void onBegin(const std::string& libraryName, std::size_t depth) override;
    void onStage(const std::string& libraryName, LoadStage stage,
                 const std::string& detail, std::size_t depth) override;
    void onEnd(const std::string& libraryName, const LoadOutcome& outcome, std::size_t depth) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ----------------------------------------------------------------------------
// ProgressBar — شريط تقدّم حي عام لعمليات متعددة الخطوات (تثبيت حزم RinPM مثلاً).
//
//   ProgressBar bar(std::cout, "Installing", packages.size());
//   for (...) {
//       bar.setStatus("http 1.2.0 · downloading");   // ما يجري الآن فعلاً
//       ... العمل الحقيقي ...
//       bar.note(ProgressBar::Glyph::Ok, "http 1.2.0  downloaded");  // سطر دائم فوق الشريط
//       bar.advance();
//   }
//   bar.finish(true, "5 packages installed");
//
// في الطرفية الحقيقية: خيط خفيف يحرّك الـ spinner ويحدّث المدة كل ~80ms حتى أثناء
// عمليات الحجب الطويلة (تنزيل). خارجها: سطر نظيف لكل note/finish فقط، بلا أي ESC.
// آمن عبر الخيوط. الهادم ينهي الشريط ويُظهر المؤشر بأمان إن لم يُستدعَ finish().
// ----------------------------------------------------------------------------
class ProgressBar {
public:
    enum class Glyph { Ok, Fail, Warn, Info, Skip };

    ProgressBar(std::ostream& out, std::string title, std::size_t total,
                IndicatorStyle style = styleFromEnv());
    // حسب نوع الأمر: عنوان ووحدة ولون وشكل وETA/سرعة من الـprofile. fd: واصف الدفق الحقيقي
    // (1 = stdout، 2 = stderr) حين يُعاد توجيه std::cout مؤقتاً (rdbuf) فلا يمكن كشفه من العنوان.
    ProgressBar(std::ostream& out, const TaskProfile& profile, std::size_t total, int fd = -1);
    ~ProgressBar();
    ProgressBar(const ProgressBar&) = delete;
    ProgressBar& operator=(const ProgressBar&) = delete;

    void setStatus(const std::string& status);              // نص الخطوة الجارية (يُقتطع تلقائياً)
    void advance(std::size_t n = 1);                         // خطوات مكتملة
    void note(Glyph g, const std::string& text);             // سطر دائم فوق الشريط
    void finish(bool success, const std::string& summary);   // يُنهي ويطبع الملخص؛ استدعاء واحد فقط

    struct Impl;   // تفصيل داخلي (مكشوف للتصريح فقط)
private:
    std::unique_ptr<Impl> impl_;
};

// ----------------------------------------------------------------------------
// Spinner — مؤشر غير محدَّد المدة (لا نسبة): لعمل لا يُعرف طوله مسبقاً (اتصال، حل تبعيات،
// بناء...). يعرض: <spinner> الرسالة  المدة. خيط خفيف يحرّكه حتى أثناء الحجب.
//
//   Spinner sp(std::cout, "Resolving dependencies");
//   sp.setMessage("Fetching registry index");
//   sp.succeed("Resolved 12 packages");   // أو fail(...) / warn(...) / stop()
//
// خارج الطرفية الحقيقية: سطر واحد عند البدء وسطر نتيجة عند الانتهاء، بلا أنيميشن.
// ----------------------------------------------------------------------------
class Spinner {
public:
    Spinner(std::ostream& out, std::string message, IndicatorStyle style = styleFromEnv());
    Spinner(std::ostream& out, const TaskProfile& profile, std::string message, int fd = -1);
    ~Spinner();
    Spinner(const Spinner&) = delete;
    Spinner& operator=(const Spinner&) = delete;

    void setMessage(const std::string& message);
    void succeed(const std::string& text);
    void fail(const std::string& text);
    void warn(const std::string& text);
    void stop();   // يمسح السطر بلا نتيجة

    struct Impl;   // تفصيل داخلي (مكشوف للتصريح فقط)
private:
    std::unique_ptr<Impl> impl_;
};

// ----------------------------------------------------------------------------
// report — سطر ملخّص نهائي بهوية نوع الأمر (لأوامر لا يصلح فيها مؤشر حي لأن الأمر الفرعي
// يكتب في نفس الطرفية، مثل rinc): "✔ Building  app · 48.2 KB · 1.3s".
// ----------------------------------------------------------------------------
void report(std::ostream& out, const TaskProfile& profile, bool ok, const std::string& text, double seconds, int fd = -1);

// 48230 -> "47.1 KB" — أحجام مقروءة موحّدة.
std::string formatBytes(unsigned long long bytes);

// ----------------------------------------------------------------------------
// LoadSession — واجهة الاستخدام من جهة المُفسِّر/RinPM. RAII: تستدعي onBegin
// عند الإنشاء، ويجب استدعاء finish() صراحةً (أو تُستدعى تلقائياً بـ outcome
// فاشل من الهادم إن لم يُستدعَ finish — يحمي من "التحميل عالق عند 70%" لو
// حدث استثناء بين مرحلتين، بنفس الحال الذي تُرمى فيه RinError من @import).
//
// الاستخدام المتوقّع لاحقاً داخل rin_interpreter.cpp عند ImportStmt (بدون أي
// تعديل على المنطق الفعلي، فقط إضافة استدعاءات .stage(...) بين الأسطر
// القائمة فعلاً):
//
//   LoadSession session(sink, rawPath, currentDepth);
//   session.stage(LoadStage::Resolving);         // بعد بناء libPath
//   session.stage(LoadStage::Locating);          // بعد فحص embedded map
//   session.stage(LoadStage::Reading);           // بعد قراءة source بنجاح
//   session.stage(LoadStage::Parsing);           // بعد Parser::parse()
//   session.stage(LoadStage::Dependencies);      // قبل executeBlock (قد تحوي @import متداخلة)
//   session.stage(LoadStage::Registering);       // عند تسجيل alias/دمج مباشر
//   session.stage(LoadStage::Initializing);      // أثناء/بعد executeBlock
//   session.finish(LoadOutcome{true, fromEmbedded, false, ""});
// ----------------------------------------------------------------------------
class LoadSession {
public:
    LoadSession(std::shared_ptr<ILoadSink> sink, std::string libraryName, std::size_t depth = 0);
    ~LoadSession();

    LoadSession(const LoadSession&) = delete;
    LoadSession& operator=(const LoadSession&) = delete;

    // ينتقل إلى مرحلة جديدة. detail اختياري (مثلاً "Resolving dependencies...").
    void stage(LoadStage s, const std::string& detail = "");
    // ينهي الجلسة بنجاح أو فشل. آمن الاستدعاء مرة واحدة فقط؛ نداءات إضافية تُتجاهل.
    void finish(LoadOutcome outcome);

private:
    std::shared_ptr<ILoadSink> sink_;
    std::string name_;
    std::size_t depth_;
    bool finished_ = false;
};

// ----------------------------------------------------------------------------
// Mode — تبديل سريع بين وضعين شائعين بدون كتابة sink مخصص:
//   Verbose -> ConsoleBarSink على std::cout
//   Quiet   -> SilentSink
// لأي احتياج أدق (IDE Background Loading يبعث الأحداث كـ JSON عبر IPC مثلاً،
// أو Live Preview يرسل الأحداث إلى WebView)، يُطبَّق ILoadSink مخصص مباشرة
// بدل استخدام هذه الدالة المساعدة.
// ----------------------------------------------------------------------------
enum class Mode { Verbose, Quiet };

std::shared_ptr<ILoadSink> makeSink(Mode mode, std::ostream& out);

} // namespace rin::loaderui

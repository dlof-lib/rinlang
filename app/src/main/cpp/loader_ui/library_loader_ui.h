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
// ConsoleBarSink — العارض الافتراضي: شريط تقدّم حي بالأسطر، بالضبط بالشكل
// الموصوف في الطلب (Importing X / [████---] NN% / ✓ loaded)، بما فيه دعم
// عرض شجرة للمكتبات المركّبة (rin.ui: core/layout/paint/widgets).
//
// ملاحظة تنفيذية: يكتب مباشرة إلى std::ostream مُمرَّر (وليس std::cout دائماً)
// حتى يمكن استخدامه مع نفس نمط `output <<` المستخدم في rin_interpreter.cpp
// (حيث output هو std::ostringstream/std::ostream يُجمَّع منه ناتج التشغيل).
//
// عرض الشجرة (rin.ui): تُبنى فقط عند depth==0 (مكتبة أعلى مستوى) — تراكم أسماء تبعياتها
// المباشرة (depth==1) بينما تبدأ/تنتهي أثناء تنفيذ executeBlock الخاص بها، وتُعاد طباعتها في
// موضعين فقط: عند وصول المكتبة الأم لمرحلة Dependencies (أول ظهور)، وعند انتهاء كل تبعية مباشرة
// (لتحديث ✓/... تدريجياً). أحداث depth>=2 (أحفاد) لا تُطبع كصف شجرة مستقل حالياً (حد معروف:
// شجرة بعمق واحد فقط، تكفي لمثال rin.ui وأمثلة مشابهة دون تعقيد عرض متعدد المستويات).
// ----------------------------------------------------------------------------
class ConsoleBarSink final : public ILoadSink {
public:
    // out: أين يُكتب الشريط. barWidth: عدد خانات الشريط (افتراضي 20 كما بالمثال).
    // useCarriageReturn: true لسلوك طرفية حية (\r يعيد كتابة نفس السطر)، أو
    // false لبيئات لا تدعم \r بشكل جيد (بعض نوافذ الالتقاط/logs) فيطبع كل
    // تحديث كسطر جديد بدلاً من الكتابة فوق السابق.
    explicit ConsoleBarSink(std::ostream& out, int barWidth = 20,
                             bool useCarriageReturn = true);

    void onBegin(const std::string& libraryName, std::size_t depth) override;
    void onStage(const std::string& libraryName, LoadStage stage,
                 const std::string& detail, std::size_t depth) override;
    void onEnd(const std::string& libraryName, const LoadOutcome& outcome, std::size_t depth) override;

private:
    void renderBar(int percent) const;
    void renderChildTree() const;

    std::ostream& out_;
    int barWidth_;
    bool useCr_;
    // تبعيات المكتبة الأم الحالية (depth==0) المباشرة (depth==1)، بترتيب ظهورها، مع حالة كل واحدة.
    // تُصفَّر عند onBegin(depth==0) جديد (استيراد أعلى مستوى تالٍ لا يرث شجرة سابقه).
    std::vector<std::pair<std::string, bool>> currentTopLevelChildren_; // (name, done)
};

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

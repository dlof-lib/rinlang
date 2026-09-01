// app/src/main/cpp/loader_ui/library_loader_ui.cpp
#include "library_loader_ui.h"

#include <algorithm>
#include <ostream>

namespace rin::loaderui {

const char* stageLabel(LoadStage stage) {
    switch (stage) {
        case LoadStage::Resolving:    return "Resolving";
        case LoadStage::Locating:     return "Locating";
        case LoadStage::Reading:      return "Reading";
        case LoadStage::Parsing:      return "Parsing";
        case LoadStage::Dependencies: return "Resolving dependencies";
        case LoadStage::Registering:  return "Registering";
        case LoadStage::Initializing: return "Initializing";
        case LoadStage::Completed:    return "Completed";
    }
    return "";
}

// ============================================================================
// ConsoleBarSink
// ============================================================================
ConsoleBarSink::ConsoleBarSink(std::ostream& out, int barWidth, bool useCarriageReturn)
    : out_(out), barWidth_(barWidth), useCr_(useCarriageReturn) {}

void ConsoleBarSink::renderBar(int percent) const {
    percent = std::clamp(percent, 0, 100);
    int filled = (percent * barWidth_) / 100;
    out_ << "[";
    for (int i = 0; i < barWidth_; ++i) out_ << (i < filled ? "\u2588" : "-");
    out_ << "] " << percent << "%";
}

void ConsoleBarSink::onBegin(const std::string& libraryName, std::size_t depth) {
    if (depth == 0) {
        out_ << "Importing " << libraryName << "\n";
        // مكتبة أعلى مستوى جديدة: تبدأ بشجرة تبعيات فارغة خاصة بها، لا ترث أي شيء من استيراد
        // سابق (تفادي التسرّب بين استيرادات @import متتالية وغير مرتبطة على نفس المستوى الأعلى).
        currentTopLevelChildren_.clear();
    } else if (depth == 1) {
        // تبعية مباشرة لمكتبة أعلى مستوى قيد التحميل حالياً. ملاحظة ترتيب حقيقية: هذا يحدث
        // *بعد* أن تكون المكتبة الأم قد أبلغت بالفعل عن مرحلتها Dependencies (لأن onBegin هذا
        // يُستدعى من داخل executeBlock نفسه، وهو ما يُنفَّذ بعد تلك المرحلة) — لذا الشجرة تُبنى
        // هنا تدريجياً بمجرد ظهور كل تبعية فعلياً، لا دفعة واحدة عند مرحلة الأم (لم تكن التبعيات
        // معروفة بعد في تلك اللحظة).
        currentTopLevelChildren_.emplace_back(libraryName, false);
        renderChildTree();
    }
    // depth >= 2 (أحفاد): لا يُعرَض كصف شجرة منفصل حالياً — حد معروف في هذا الإصدار (شجرة بعمق
    // واحد فقط)؛ لا يزال شريط تلك المكتبة نفسها يعمل بشكل طبيعي عبر onStage/onEnd الخاصين بها.
}

void ConsoleBarSink::renderChildTree() const {
    out_ << "\n\nLoading:\n";
    for (std::size_t i = 0; i < currentTopLevelChildren_.size(); ++i) {
        bool last = (i + 1 == currentTopLevelChildren_.size());
        const auto& [childName, done] = currentTopLevelChildren_[i];
        std::string padded = childName.size() < 10 ? childName + std::string(10 - childName.size(), ' ')
                                                     : childName + " ";
        out_ << (last ? "\u2514\u2500\u2500 " : "\u251c\u2500\u2500 ") << padded << (done ? "\u2713" : "...") << "\n";
    }
    out_ << "\n";
}

void ConsoleBarSink::onStage(const std::string& /*libraryName*/, LoadStage stage,
                              const std::string& detail, std::size_t /*depth*/) {
    int percent = percentOf(stage);

    if (useCr_) out_ << "\r";
    renderBar(percent);
    if (!detail.empty()) out_ << "  " << detail;
    else if (stage != LoadStage::Completed) out_ << "  " << stageLabel(stage) << "...";
    if (!useCr_) out_ << "\n";
}

void ConsoleBarSink::onEnd(const std::string& libraryName, const LoadOutcome& outcome, std::size_t depth) {
    if (outcome.fromCache) {
        if (useCr_) out_ << "\r";
        out_ << "\u21ba " << libraryName << " already imported (skipped)\n";
        return;
    }

    if (outcome.success) {
        // onStage(Completed) رسم الشريط عند 100% مسبقاً في نفس السطر (\r)؛
        // هنا فقط ننهي ذلك السطر ونضيف رسالة النجاح، دون رسم الشريط مرتين.
        out_ << "\n\u2713 " << libraryName << " loaded";
        if (outcome.fromEmbedded) out_ << " (embedded)";
        out_ << "\n";
    } else {
        // فشل: لا نرسم شريطاً ممتلئاً وهمياً عند نسبة لم تُبلَغ فعلياً — ننهي سطر
        // الشريط الجزئي الحالي بسطر جديد ثم نطبع الخطأ، حتى لا يوحي العرض بأن
        // التحميل اكتمل ثم فشل لاحقاً.
        out_ << "\n\u2717 " << libraryName << " failed: " << outcome.errorMessage << "\n";
    }

    // انتهت تبعية مباشرة (depth==1) لمكتبة أم قيد التحميل: علّمها كمكتملة في شجرتها وأعد رسم
    // تلك الشجرة فوراً لتظهر ✓ محدَّثة (أول ظهور للاسم المطابق غير المكتمل بعد، احتياطاً من
    // تبعيات بنفس الاسم مستوردة أكثر من مرة تحت نفس الأم).
    if (depth == 1) {
        for (auto& [childName, done] : currentTopLevelChildren_) {
            if (childName == libraryName && !done) { done = true; break; }
        }
        renderChildTree();
    }
}

// ============================================================================
// LoadSession
// ============================================================================
LoadSession::LoadSession(std::shared_ptr<ILoadSink> sink, std::string libraryName, std::size_t depth)
    : sink_(std::move(sink)), name_(std::move(libraryName)), depth_(depth) {
    if (sink_) sink_->onBegin(name_, depth_);
}

LoadSession::~LoadSession() {
    if (!finished_ && sink_) {
        // جلسة انتهت دون finish() صريح (مثال: استثناء بين مرحلتين، تماماً كخطأ
        // Parser/RinError في @import الحالي) — تُبلَّغ كفشل بدل تجميد الشريط
        // عند آخر نسبة وصل إليها بصمت.
        LoadOutcome outcome;
        outcome.success = false;
        outcome.errorMessage = "import interrupted";
        sink_->onEnd(name_, outcome, depth_);
    }
}

void LoadSession::stage(LoadStage s, const std::string& detail) {
    if (finished_ || !sink_) return;
    sink_->onStage(name_, s, detail, depth_);
}

void LoadSession::finish(LoadOutcome outcome) {
    if (finished_) return;
    finished_ = true;
    if (sink_) sink_->onEnd(name_, outcome, depth_);
}

// ============================================================================
// makeSink
// ============================================================================
std::shared_ptr<ILoadSink> makeSink(Mode mode, std::ostream& out) {
    switch (mode) {
        case Mode::Verbose: return std::make_shared<ConsoleBarSink>(out);
        case Mode::Quiet:   return std::make_shared<SilentSink>();
    }
    return std::make_shared<SilentSink>();
}

} // namespace rin::loaderui

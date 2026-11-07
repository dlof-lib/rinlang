// ============================================================================
//  rin_clock.h — الساعة المشتركة لمفهومَي time و timer (docs/TIMER.md)
// ----------------------------------------------------------------------------
//  ساعة واحدة لكل شيء يقرأ الزمن في Rin: time.now() وعائلة time.* (rin_extra_natives7.cpp)
//  والمجدوِل timers.* و`timer` (rin_timer.cpp). لها وضعان:
//    * حقيقي (الافتراضي): system_clock للحائط (epoch) و steady_clock للمدد (mono).
//    * مُجمَّد/افتراضي (time.freeze): الساعة لا تتحرك إلا بـ time.advance / time.sleep / timers.run،
//      فتصير كل اختبارات الزمن والمؤقّتات حتمية (نفس المخرجات في كل تشغيل).
//  رأس فقط (inline) كي يراه أكثر من ملف .cpp مُضمَّن في نفس وحدة الترجمة دون تعريف مزدوج.
//  القيم ميلي ثانية (double) كي لا يضيع كسر الميلي ثانية في الوضع الافتراضي.
// ============================================================================
#pragma once
#include <chrono>

namespace rin {
namespace clk {

struct State {
    bool virt = false;   // true = ساعة مُجمَّدة تتحرك يدوياً
    double epoch = 0;    // وضع virt: لحظة الحائط الحالية (ms منذ 1970)
    double mono = 0;     // وضع virt: الساعة الرتيبة الحالية (ms)
};
inline State& state() { static State s; return s; }

inline double realEpochMs() {
    return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
inline double realMonoMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline bool isVirtual() { return state().virt; }
// لحظة الحائط الحالية (للتواريخ) — تتبع الساعة المُجمَّدة إن وُجدت.
inline double epochMs() { return state().virt ? state().epoch : realEpochMs(); }
// الساعة الرتيبة (للمدد والمؤقّتات) — لا تقفز مع تغيير ساعة النظام.
inline double monoMs() { return state().virt ? state().mono : realMonoMs(); }

// يُجمّد الساعة عند لحظة epoch (ms). يُستدعى أيضاً لإعادة التجميد عند لحظة أخرى.
inline void freeze(double atEpochMs) {
    State& s = state();
    s.mono = s.virt ? s.mono : realMonoMs();
    s.epoch = atEpochMs;
    s.virt = true;
}
inline void unfreeze() { state().virt = false; }
// يُقدِّم الساعة المُجمَّدة إلى الساعة الرتيبة target (لا يرجع للخلف). لا أثر في الوضع الحقيقي.
inline void advanceMonoTo(double target) {
    State& s = state();
    if (!s.virt || target <= s.mono) return;
    s.epoch += target - s.mono;
    s.mono = target;
}

} // namespace clk
} // namespace rin

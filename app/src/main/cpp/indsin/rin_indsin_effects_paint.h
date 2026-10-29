// indsin/rin_indsin_effects_paint.h — يطبّق إطارات EffectRuntime على DrawList (غير مدمِّر: لا يمسّ الـFabric).
//
// كل DrawCommand يحمل `owner` (Strand الذي أصدره). نبحث عن إطار المالك (مجمّعاً مع أسلافه) ونطبّق:
//   • التحويل  p' = (sx*p.x + tx, sy*p.y + ty): bounds، radius، strokeWidth، fontSize، الظلال
//   • الشفافية: تُضرب في ألفا اللون/التدرّج/الظل
//   • bg: يمزج لون تعبئة العنصر نفسه · color: يمزج ألوان نصوص العنصر وأبنائه
//   • glow: يحوّل ظل التعبئة الأولى للعنصر إلى توهّج بلا إزاحة (لون glowColor أو primary)
// SCRIM_RECT: شفافية فقط (يغطي الشاشة كلها فلا يُحوَّل).
#pragma once
#include "rin_indsin_effects.h"
#include "rin_indsin_paint.h"

namespace indsin {

inline void applyEffectsToDrawList(DrawList& list, const EffectRuntime& rt) {
    if (rt.empty()) return;
    std::unordered_set<StrandId> fillDone; // أول FILL_RECT لكل مالك = تعبئة الصندوق (هدف bg/glow)
    auto scaleA = [](Color& c, double k) { c.a = (unsigned char)std::lround(std::max(0.0, std::min(255.0, c.a * k))); };
    for (auto& cmd : list) {
        const fx::StrandFx* f = rt.frameOf(cmd.owner);
        if (!f) continue;
        if (cmd.op != DrawOp::SCRIM_RECT) {
            cmd.bounds.x = f->sx * cmd.bounds.x + f->tx; cmd.bounds.y = f->sy * cmd.bounds.y + f->ty;
            cmd.bounds.w *= f->sx; cmd.bounds.h *= f->sy;
            const double s = std::min(std::fabs(f->sx), std::fabs(f->sy));
            cmd.radius *= s; cmd.strokeWidth *= s; cmd.fontSize *= s;
            cmd.shadowBlur *= s; cmd.shadowDy *= f->sy; cmd.shadowSpread *= s;
        }
        if (cmd.op == DrawOp::FILL_RECT && f->hasBg && !fillDone.count(cmd.owner) && cmd.owner != 0) {
            cmd.color = fx::lerpColor(cmd.color, f->bg, f->bgAmt);
            for (auto& st : cmd.stops) st = fx::lerpColor(st, f->bg, f->bgAmt);
        }
        if (cmd.op == DrawOp::FILL_RECT && f->glow > 0 && !fillDone.count(cmd.owner) && cmd.owner != 0) {
            Color gc = f->hasGlowColor ? f->glowColor : themeRegistry().active().primary;
            double strength = std::min(1.0, f->glow / 16.0);
            gc.a = (unsigned char)std::lround(std::max(60.0, 200.0 * strength));
            cmd.shadowBlur = std::max(cmd.shadowBlur, f->glow * f->sx); cmd.shadowDy = 0; cmd.shadowSpread = f->glow * 0.15;
            cmd.shadowColor = gc;
        }
        if (cmd.op == DrawOp::FILL_RECT) fillDone.insert(cmd.owner);
        if (cmd.op == DrawOp::TEXT_RUN && f->hasFg) cmd.color = fx::lerpColor(cmd.color, f->fg, f->fgAmt);
        if (f->opacity < 0.9999) {
            scaleA(cmd.color, f->opacity); scaleA(cmd.shadowColor, f->opacity);
            for (auto& st : cmd.stops) scaleA(st, f->opacity);
        }
    }
}

} // namespace indsin

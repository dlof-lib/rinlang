// tests/tools/test_indsin_typo_motion_a11y.cpp — الخطوط · حركة النص · إمكانية الوصول (docs/indsin_typography_motion_a11y.md).
// Build (from app/src/main/cpp):
//   g++ -std=c++23 -I. -Iindsin ../../../../tests/tools/test_indsin_typo_motion_a11y.cpp <core objects> -lz -o test_indsin_typo_motion_a11y
#include "rin_indsin_pipeline.h"
#include "rin_indsin_responsive.h"
#include "rin_indsin_html.h"
#include "rin_indsin_textfx.h"
#include "rin_indsin_a11y.h"
#include "rin_indsin_audit.h"
#include <cmath>
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { if (cond) std::cout << "  [PASS] " << label << "\n"; else { std::cout << "  [FAIL] " << label << "\n"; failures++; } } while (0)
using namespace indsin;

struct Scene {
    PipelineResult r; Indsin eng; bool ok = false; double W;
    explicit Scene(const std::string& src, double w = 400) : W(w) {
        r = runColdPipeline(src); ok = r.ok;
        if (!ok) { std::cout << "  parse error: " << r.errorMessage << " line " << r.errorLine << "\n"; return; }
        viewportWidthPx() = w; viewportHeightPx() = 800;
        r.fabric->screenRoot = true;
        applyResponsiveAttrs(r.fabric, w);
        eng.layout(r.fabric, {w, w, 0, 1e9}, 0, 0);
    }
    StrandPtr n(const char* name) { return findAny(r.fabric, [&](const StrandPtr& s) { return s->name == name; }); }
    DrawList paint() { Dye d; return d.paint(r.fabric); }
};
static const DrawCommand* textOf(const DrawList& l, StrandId id) { for (auto& c : l) if (c.op == DrawOp::TEXT_RUN && c.owner == id) return &c; return nullptr; }

int main() {
    std::cout << std::unitbuf;
    double fake = 0; typo::fontRegistry().clock = [&] { return fake; };
    typo::fontRegistry().clear();

    std::cout << "-- 1. CustomFont / FontLoad / FontDisplay --\n";
    {
        Scene sc(R"RIN(
<Font#Brand src="fonts/brand.woff2, fonts/brand.ttf" weight="100 900" display=block axes="wght 100 900, wdth 75 125" />
<Font#Lazy src="fonts/lazy.woff2" load=lazy display=optional />
<Column#root lang="ar">
  <Text#t font="Brand, serif" weight=650>Hello</Text>
  <Text#u font="Lazy">Lazy</Text>
  <Text#plain>plain</Text>
</Column>
)RIN");
        CHECK(sc.ok, "program with <Font> parses");
        auto& reg = typo::fontRegistry();
        CHECK(reg.has("Brand") && reg.has("Lazy"), "@font registered through <Font>");
        auto* b = reg.best("Brand", 400, "normal");
        CHECK(b && b->variable && b->weightMin == 100 && b->weightMax == 900, "weight range => variable font");
        CHECK(b && b->src.size() == 2 && b->src[0].format == "woff2" && b->src[1].format == "truetype", "src list + format inferred from extension");
        CHECK(b && b->status == typo::FontStatus::LOADING, "FontLoad: eager font starts loading at declaration");
        auto* lz = reg.best("Lazy", 400, "normal");
        auto l = sc.paint();
        CHECK(lz && lz->status == typo::FontStatus::LOADING, "FontLoad: lazy font starts loading on first use");
        auto* t = textOf(l, sc.n("t")->id);
        CHECK(t && t->fontFamily.find("\"Brand\"") != std::string::npos && t->fontFamily.find("serif") != std::string::npos, "CustomFont stack: \"Brand\" + serif");
        CHECK(t && t->fontWeight == 650 && t->bold, "weight=650 => fontWeight + bold");
        CHECK(t && t->fontBlocked && t->fontStatus == "loading", "FontDisplay=block: text blocked while loading");
        auto* u = textOf(l, sc.n("u")->id);
        CHECK(u && u->fontBlocked, "FontDisplay=optional: blocked only in first ~100ms");
        fake += 200;
        l = sc.paint();
        CHECK(!textOf(l, sc.n("u")->id)->fontBlocked, "optional: after 100ms falls back, never blocks");
        CHECK(textOf(l, sc.n("t")->id)->fontBlocked, "block still blocked at 200ms");
        fake += 3000;
        l = sc.paint();
        CHECK(!textOf(l, sc.n("t")->id)->fontBlocked, "block period ends after 3s => fallback shown");
        reg.setStatus("Brand", "loaded");
        l = sc.paint();
        CHECK(textOf(l, sc.n("t")->id)->fontStatus == "loaded", "host reports loaded");
        auto* p = textOf(l, sc.n("plain")->id);
        CHECK(p && p->fontFamily.empty() && p->lang == "ar", "unstyled text: no font, inherits lang from root");
        std::string css = typo::fontFacesCss();
        CHECK(css.find("@font-face{font-family:\"Brand\"") != std::string::npos && css.find("font-weight:100 900") != std::string::npos && css.find("font-display:block") != std::string::npos, "@font-face css (weight range + display)");
    }

    std::cout << "-- 2. SystemFont --\n";
    {
        Scene sc(R"RIN(
<Column#root>
  <Text#a font="system">a</Text><Text#b font="mono">b</Text><Text#c font="Helvetica Neue, sans">c</Text><Text#d font="kufi">d</Text>
</Column>
)RIN");
        auto l = sc.paint();
        CHECK(textOf(l, sc.n("a")->id)->fontFamily.find("system-ui") != std::string::npos, "system => system-ui stack");
        CHECK(textOf(l, sc.n("b")->id)->fontFamily.find("monospace") != std::string::npos, "mono => monospace stack");
        auto* c = textOf(l, sc.n("c")->id);
        CHECK(c && c->fontFamily.rfind("\"Helvetica Neue\"", 0) == 0, "installed system font by name is quoted first");
        CHECK(textOf(l, sc.n("d")->id)->fontFamily.find("Noto Kufi Arabic") != std::string::npos, "Arabic generic (kufi)");
        CHECK(typo::genericCanonical("ui-monospace") == "ui-monospace" && typo::genericCanonical("handwriting") == "cursive", "generic aliases");
    }

    std::cout << "-- 3. FontVariant / FontFeature / VariableFont --\n";
    {
        Scene sc(R"RIN(
<Column#root>
  <Text#a font_variant="small-caps tabular-nums" font_feature="liga, kern=0, 'ss01', -dlig, smcp off">a</Text>
  <Text#b font_variant="bogus-caps" font_feature="toolong">b</Text>
  <Text#c font="Brand" font_variation="wght 5000, wdth 85" font_stretch="condensed" slant=-8>c</Text>
  <Text#d font_style="italic" weight="semibold">d</Text>
  <Text#e font_style="oblique 12deg">e</Text>
</Column>
)RIN");
        auto l = sc.paint();
        auto* a = textOf(l, sc.n("a")->id);
        CHECK(a && a->fontCss.find("font-variant:small-caps tabular-nums") != std::string::npos, "FontVariant => font-variant");
        CHECK(a && a->fontFeatures.find("\"liga\" 1") != std::string::npos && a->fontFeatures.find("\"kern\" 0") != std::string::npos &&
              a->fontFeatures.find("\"ss01\" 1") != std::string::npos && a->fontFeatures.find("\"dlig\" 0") != std::string::npos, "FontFeature parsing (tag, tag=n, quoted, -tag)");
        CHECK(a && a->fontFeatures.find("\"smcp\" 0") != std::string::npos, "explicit feature overrides variant feature (smcp off)");
        typo::AttrScope s0; s0.self = sc.n("b").get();
        auto tb = typo::resolveTypography(s0);
        CHECK(tb.warnings.size() >= 2, "invalid variant/feature produce warnings");
        typo::AttrScope s1; s1.self = sc.n("c").get();
        auto tc = typo::resolveTypography(s1);
        bool wghtClamped = false, wdth = false; for (auto& v : tc.variationList) { if (v.first == "wght" && v.second == 900) wghtClamped = true; if (v.first == "wdth" && v.second == 85) wdth = true; }
        CHECK(wghtClamped && wdth, "VariableFont: axis clamped to range (wght 5000 -> 900 (axes of Brand)), wdth kept");
        CHECK(tc.stretch == "75%", "font_stretch=condensed => 75%");
        bool slnt = false; for (auto& v : tc.variationList) if (v.first == "slnt" && v.second == -8) slnt = true;
        CHECK(slnt, "slant=-8 => slnt axis -8");
        auto* d = textOf(l, sc.n("d")->id);
        CHECK(d && d->italic && d->fontWeight == 600 && d->bold, "italic + weight=semibold => 600/bold");
        auto* e = textOf(l, sc.n("e")->id);
        CHECK(e && !e->italic && e->oblique == 12, "oblique 12deg");
        CHECK(typo::weightFromName("black") == 900 && typo::weightFromName("350") == 350, "weight names/numbers");
    }

    std::cout << "-- 4. TextTransform / TextShadow --\n";
    {
        CHECK(typo::applyTextTransform("hello world", "uppercase") == "HELLO WORLD", "uppercase");
        CHECK(typo::applyTextTransform("HeLLo", "lowercase") == "hello", "lowercase");
        CHECK(typo::applyTextTransform("hello big world", "capitalize") == "Hello Big World", "capitalize");
        CHECK(typo::applyTextTransform("straße", "uppercase") == "STRASSE", "ß -> SS");
        CHECK(typo::applyTextTransform("привет", "uppercase") == "ПРИВЕТ", "Cyrillic");
        CHECK(typo::applyTextTransform("istanbul", "uppercase", "tr") == "İSTANBUL", "Turkish dotted I (lang=tr)");
        CHECK(typo::applyTextTransform("مرحبا", "uppercase") == "مرحبا", "Arabic unaffected");
        CHECK(typo::applyTextTransform("Ab1", "full-width") == "Ａｂ１", "full-width");
        Scene sc(R"RIN(
<Column#root text_transform="uppercase" text_shadow="1 2 3 #ff0000, 0 0 8 primary">
  <Text#a>child</Text>
  <Text#b text_transform="none" text_shadow="none" text_rotate=30>keep</Text>
</Column>
)RIN");
        auto l = sc.paint();
        auto* a = textOf(l, sc.n("a")->id); auto* b = textOf(l, sc.n("b")->id);
        CHECK(a && a->text == "CHILD" && a->textTransform == "uppercase", "text_transform inherited from ancestor");
        CHECK(a && a->textShadows.size() == 2 && a->textShadows[0].dy == 2 && a->textShadows[0].blur == 3 && a->textShadows[0].color.r == 255, "text_shadow inherited + parsed (2 layers)");
        CHECK(b && b->text == "keep" && b->textShadows.empty() && b->rotateDeg == 30, "child overrides: none; text_rotate=30 static");
        std::string html = drawListToHtml(l, 400, 300, "t");
        CHECK(html.find("text-shadow:1px 2px 3px rgba(255,0,0,1)") != std::string::npos, "HTML text-shadow");
        CHECK(html.find("transform:rotate(30deg)") != std::string::npos, "HTML rotate");
    }

    std::cout << "-- 5. TextAnimation: TypeWriter --\n";
    {
        Scene sc(R"RIN(
<Column#root>
  <Text#t typewriter=true type_speed=10 type_cursor="|" type_cursor_blink=500>HelloWorld</Text>
  <Text#loop text_animation="typewriter" type_speed=10 type_loop=true type_hold=500>ab</Text>
</Column>
)RIN");
        TextFxRuntime rt;
        bool anim = rt.update(sc.r.fabric, 0);
        auto l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(anim, "typewriter animating");
        CHECK(textOf(l, sc.n("t")->id)->text == "|", "t=0: nothing typed, cursor shown");
        rt.update(sc.r.fabric, 300); l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(textOf(l, sc.n("t")->id)->text == "Hel|", "t=300ms @10cps: 3 chars + cursor");
        rt.update(sc.r.fabric, 1000); l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(textOf(l, sc.n("t")->id)->text == "HelloWorld|", "t=1000ms: complete, cursor solid (blink phase 0)");
        rt.update(sc.r.fabric, 1600); l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(textOf(l, sc.n("t")->id)->text == "HelloWorld ", "after done the cursor blinks (hidden phase)");
        bool still = rt.update(sc.r.fabric, 2000);
        CHECK(still, "blinking cursor keeps host ticking");
        // بلا runtime: النص النهائي كاملاً (CLI/PNG)
        auto plain = sc.paint();
        CHECK(textOf(plain, sc.n("t")->id)->text == "HelloWorld", "no runtime => final static text");
        rt.update(sc.r.fabric, 0); // loop: 'ab' typed in 200ms, hold 500, erase 100, restart
        rt.update(sc.r.fabric, 100); l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(textOf(l, sc.n("loop")->id)->text == "a", "loop: typing");
        rt.update(sc.r.fabric, 500); l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(textOf(l, sc.n("loop")->id)->text == "ab", "loop: holding full text");
        rt.update(sc.r.fabric, 790); l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(textOf(l, sc.n("loop")->id)->text.size() <= 1, "loop: erasing");
        rt.reduceMotion = true;
        CHECK(!rt.update(sc.r.fabric, 100), "ReduceMotion: no animation");
        l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(textOf(l, sc.n("t")->id)->text == "HelloWorld", "ReduceMotion: final text immediately");
    }

    std::cout << "-- 6. FadeText / TextReveal --\n";
    {
        Scene sc(R"RIN(
<Column#root>
  <Text#f text_animation="fade-in" text_duration=1000 text_easing=linear>fade</Text>
  <Text#fo fade_text="out" text_duration=1000 text_easing=linear>out</Text>
  <Text#fc fade_text="chars" text_duration=1000 text_easing=linear>abcd</Text>
  <Text#fa fade_text="chars" text_duration=1000 text_easing=linear>مرحبا بك</Text>
  <Text#rl text_reveal="left" text_duration=1000 text_easing=linear>reveal</Text>
  <Text#rc text_reveal="center" text_duration=1000 text_easing=linear>mid</Text>
  <Text#rw text_reveal="words" text_duration=1000 text_easing=linear>one two three</Text>
  <Text#d text_animation="slide-up" text_duration=1000 text_easing=linear text_distance=20>up</Text>
</Column>
)RIN");
        TextFxRuntime rt; rt.update(sc.r.fabric, 0); rt.update(sc.r.fabric, 500);
        auto base = sc.paint(); auto l = base; applyTextFxToDrawList(l, rt);
        auto alphaOf = [&](const char* n) { return textOf(l, sc.n(n)->id)->color.a / (double)textOf(base, sc.n(n)->id)->color.a; };
        CHECK(std::fabs(alphaOf("f") - 0.5) < 0.02, "fade-in @50% => alpha 0.5");
        CHECK(std::fabs(alphaOf("fo") - 0.5) < 0.02, "fade out @50% => alpha 0.5");
        auto* fc = textOf(l, sc.n("fc")->id);
        CHECK(fc && fc->glyphUnit == 1 && fc->glyphFx.size() == 4 && fc->glyphFx[0].alpha > fc->glyphFx[3].alpha, "fade chars: per-letter stagger (first more visible than last)");
        auto* fa = textOf(l, sc.n("fa")->id);
        CHECK(fa && fa->glyphUnit == 2 && fa->glyphFx.size() == 2, "Arabic falls back to per-WORD units (keeps letter joining)");
        auto* rl = textOf(l, sc.n("rl")->id);
        CHECK(rl && std::fabs(rl->clipR - 0.5) < 0.02 && rl->clipL == 0, "reveal-left @50%: right 50% clipped");
        auto* rc = textOf(l, sc.n("rc")->id);
        CHECK(rc && std::fabs(rc->clipL - 0.25) < 0.02 && std::fabs(rc->clipR - 0.25) < 0.02, "reveal-center @50%: 25% each side");
        auto* rw = textOf(l, sc.n("rw")->id);
        CHECK(rw && rw->glyphUnit == 2 && rw->glyphFx.size() == 3, "reveal words: 3 units");
        auto* d = textOf(l, sc.n("d")->id); auto* d0 = textOf(base, sc.n("d")->id);
        CHECK(d && std::fabs((d->bounds.y - d0->bounds.y) - 10.0) < 0.5, "slide-up @50% of 20px => 10px below final");
        // before delay w/ fill both => hidden
        Scene sc2(R"RIN(<Column#root><Text#x text_animation="fade-in" text_delay=500 text_duration=500>late</Text></Column>)RIN");
        TextFxRuntime r2; r2.update(sc2.r.fabric, 0); r2.update(sc2.r.fabric, 100);
        auto l2 = sc2.paint(); applyTextFxToDrawList(l2, r2);
        CHECK(textOf(l2, sc2.n("x")->id)->color.a == 0, "delay + fill=both: invisible before start");
        // native rasterizer honors clip + glyph alpha without crashing & draws something
        auto buf = rasterizeToBuffer(l, 400, (int)std::ceil(sc.r.fabric->geometry.h));
        CHECK(!buf.empty(), "native rasterizer handles clip/glyph fx");
        std::string html = drawListToHtml(l, 400, 400, "t");
        CHECK(html.find("clip-path:inset(0% 50% 0% 0%)") != std::string::npos, "HTML clip-path for reveal");
        CHECK(html.find("display:inline-block;opacity:") != std::string::npos, "HTML per-unit spans");
    }

    std::cout << "-- 7. TextRotate / TextTransition / Rotator --\n";
    {
        Scene sc(R"RIN(
<warp label="One" />
<Column#root>
  <Text#r text_rotate=10 text_rotate_to=110 text_rotate_duration=1000 text_easing=linear>spin</Text>
  <Text#s text_rotate="spin" text_rotate_duration=1000>spin</Text>
  <Text#w text_rotator="Fast|Simple|Fun" text_rotator_interval=1000 text_transition_duration=200 text_rotator_effect=slide-up></Text>
  <Text#tr text_transition="fade" text_transition_duration=400 text={label} />
</Column>
)RIN");
        CHECK(sc.ok, "program parses");
        TextFxRuntime rt; rt.update(sc.r.fabric, 0); rt.update(sc.r.fabric, 500);
        auto l = sc.paint(); applyTextFxToDrawList(l, rt);
        CHECK(std::fabs(textOf(l, sc.n("r")->id)->rotateDeg - 60) < 0.5, "text_rotate 10 -> 110: @50% = 60deg");
        CHECK(std::fabs(textOf(l, sc.n("s")->id)->rotateDeg - 180) < 0.5, "text_rotate=spin: 360deg/1s => 180 @500ms");
        CHECK(textOf(l, sc.n("w")->id)->text == "Fast", "rotator: first item at t<interval");
        rt.update(sc.r.fabric, 1300); l = sc.paint(); applyTextFxToDrawList(l, rt);
        int runsW = 0; std::string cur, prev; for (auto& c : l) if (c.op == DrawOp::TEXT_RUN && c.owner == sc.n("w")->id) { runsW++; (runsW == 1 ? prev : cur) = c.text; }
        CHECK(runsW == 2 && prev == "Fast" && cur == "Simple", "rotator transition: outgoing 'Fast' + incoming 'Simple'");
        rt.update(sc.r.fabric, 1500); l = sc.paint(); applyTextFxToDrawList(l, rt);
        runsW = 0; for (auto& c : l) if (c.op == DrawOp::TEXT_RUN && c.owner == sc.n("w")->id) { runsW++; cur = c.text; }
        CHECK(runsW == 1 && cur == "Simple", "rotator settled on 'Simple'");
        rt.update(sc.r.fabric, 2500); l = sc.paint(); applyTextFxToDrawList(l, rt);
        for (auto& c : l) if (c.op == DrawOp::TEXT_RUN && c.owner == sc.n("w")->id) cur = c.text;
        CHECK(cur == "Fun" || cur == "Simple", "rotator advances");
        // Transition on value change
        rt.update(sc.r.fabric, 3000);
        sc.r.warp.set("label", Value::txt("Two"));
        auto trs = sc.n("tr"); for (auto& a : trs->attrs) if (a.key == "text") a.value = Value::txt("Two");
        rt.update(sc.r.fabric, 3100); l = sc.paint(); applyTextFxToDrawList(l, rt);
        int runs = 0; for (auto& c : l) if (c.op == DrawOp::TEXT_RUN && c.owner == trs->id) runs++;
        CHECK(runs == 2, "TextTransition: old+new text drawn while text changes");
        bool still = rt.update(sc.r.fabric, 3600);
        l = sc.paint(); applyTextFxToDrawList(l, rt);
        runs = 0; for (auto& c : l) if (c.op == DrawOp::TEXT_RUN && c.owner == trs->id) runs++;
        CHECK(runs == 1, "TextTransition finished after duration");
        (void)still;
        // name of rotator for screen readers
        CHECK(a11y::nameOf(sc.r.fabric, *sc.n("w")) == "Fast, Simple, Fun", "rotator accessible name lists all items");
    }

    std::cout << "-- 8. TextShadowAnimation --\n";
    {
        Scene sc(R"RIN(
<Column#root>
  <Text#g text_shadow_animation="glow" text_shadow_color="#00ff88" text_shadow_duration=1000 text_easing=linear text_direction=normal>glow</Text>
  <Text#o text_animation="orbit" text_shadow_duration=1000 text_amplitude=5>orbit</Text>
  <Text#ft text_shadow="0 0 0 #ff0000" text_shadow_to="0 0 20 #0000ff" text_shadow_animation=custom text_shadow_duration=1000 text_easing=linear text_direction=normal>fromto</Text>
  <Text#n text_shadow_animation="neon">neon</Text>
</Column>
)RIN");
        TextFxRuntime rt; rt.update(sc.r.fabric, 0); rt.update(sc.r.fabric, 500);
        auto l = sc.paint(); applyTextFxToDrawList(l, rt);
        auto* g = textOf(l, sc.n("g")->id);
        CHECK(g && g->textShadows.size() == 1 && std::fabs(g->textShadows[0].blur - 9.0) < 0.6 && g->textShadows[0].color.g == 255, "glow: blur 2->16, color from text_shadow_color");
        auto* o = textOf(l, sc.n("o")->id);
        CHECK(o && o->textShadows.size() == 1 && std::fabs(o->textShadows[0].dx + 5.0) < 0.2, "orbit: shadow offset circles (x=-5 at half-turn)");
        auto* ft = textOf(l, sc.n("ft")->id);
        CHECK(ft && ft->textShadows.size() == 1 && std::fabs(ft->textShadows[0].blur - 10.0) < 0.5 && ft->textShadows[0].color.b > 100 && ft->textShadows[0].color.r > 100, "from->to shadow lerps blur and color");
        auto* n = textOf(l, sc.n("n")->id);
        CHECK(n && n->textShadows.size() == 3, "neon: 3 stacked layers");
        auto buf = rasterizeToBuffer(l, 400, 300);
        CHECK(!buf.empty(), "native rasterizer draws shadows");
    }

    std::cout << "-- 9. AriaLabel / AltText / AccessibleName --\n";
    {
        Scene sc(R"RIN(
<Column#root lang="en">
  <Text#title semantic=heading heading_level=1>Dashboard</Text>
  <Text#lbl>Email address</Text>
  <Input#email aria-labelledby="lbl" placeholder="you@x.com" />
  <Button#b1 ariaLabel="Close dialog">X</Button>
  <Button#b2 accessible_name="Save changes" label="S" />
  <Image#img alt_text="Company logo" src="logo.png" />
  <Image#deco decorative=true src="x.png" />
  <Text#hid aria_hidden=true>secret</Text>
  <Text#sr sr_only=true>Skip to content</Text>
  <Button#desc label="Pay" aria_describedby="lbl" />
</Column>
)RIN");
        CHECK(sc.ok, "parses with ariaLabel / aria-labelledby / alt_text aliases");
        CHECK(sc.n("b1")->attr("a11y_label") && !sc.n("b1")->attr("ariaLabel"), "ariaLabel -> a11y_label");
        CHECK(sc.n("img")->attr("alt") && !sc.n("img")->attr("alt_text"), "alt_text -> alt");
        CHECK(sc.n("email")->attr("aria_labelledby"), "aria-labelledby -> aria_labelledby");
        auto R = sc.r.fabric;
        CHECK(a11y::nameOf(R, *sc.n("email")) == "Email address", "aria_labelledby wins over placeholder");
        CHECK(a11y::nameOf(R, *sc.n("b1")) == "Close dialog", "aria-label overrides visible label");
        CHECK(a11y::nameOf(R, *sc.n("b2")) == "Save changes", "accessible_name");
        CHECK(a11y::nameOf(R, *sc.n("img")) == "Company logo", "AltText names the image");
        CHECK(a11y::nameOf(R, *sc.n("deco")).empty() && a11y::roleOf(*sc.n("deco")) == "presentation", "decorative image: no name, role=presentation");
        CHECK(a11y::descriptionOf(R, *sc.n("desc")) == "Email address", "aria_describedby => description");
        auto tree = a11y::buildTree(R);
        bool hasHidden = false, hasSr = false, hasHeading = false;
        for (auto& nd : tree) { if (nd.name == "secret") hasHidden = true; if (nd.name == "Skip to content") hasSr = true; if (nd.role == "heading" && nd.level == 1) hasHeading = true; }
        CHECK(!hasHidden, "aria_hidden excluded from the accessibility tree");
        CHECK(hasSr, "sr_only stays in the tree");
        CHECK(hasHeading, "SemanticText: heading level 1");
        std::string tr = a11y::screenReaderTranscript(R);
        CHECK(tr.find("heading level 1: Dashboard") != std::string::npos, "ScreenReader transcript: heading");
        CHECK(tr.find("button: Close dialog") != std::string::npos && tr.find("image: Company logo") != std::string::npos, "transcript: button + image");
        CHECK(tr.find("secret") == std::string::npos && tr.find("Skip to content") != std::string::npos, "transcript omits hidden, keeps sr_only");
        auto A = auditFabric(R);
        CHECK(A.count("A11Y-001") == 0 && A.count("A11Y-002") == 0, "audit: names/alt satisfied via aria-label/alt_text/labelledby");
        auto paint = sc.paint();
        CHECK(textOf(paint, sc.n("hid")->id) != nullptr, "aria_hidden still painted");
    }

    std::cout << "-- 10. LanguageTag --\n";
    {
        CHECK(typo::isValidLangTag("ar") && typo::isValidLangTag("en-US") && typo::isValidLangTag("zh-Hans-CN") && typo::isValidLangTag("sr-Latn-RS") && typo::isValidLangTag("de-DE-1996"), "valid tags");
        CHECK(!typo::isValidLangTag("toolonglang1") && !typo::isValidLangTag("e") && !typo::isValidLangTag("en--US") && !typo::isValidLangTag("") && !typo::isValidLangTag("a1"), "invalid tags");
        CHECK(typo::normalizeLangTag("ZH_hans_cn") == "zh-Hans-CN" && typo::normalizeLangTag("EN-us") == "en-US", "normalize case");
        CHECK(typo::isRtlLang("ar-EG") && typo::isRtlLang("he") && typo::isRtlLang("fa") && !typo::isRtlLang("en"), "RTL languages");
        Scene sc(R"RIN(
<Column#root lang="ar">
  <Text#a>مرحبا</Text>
  <Text#b lang="en-US">Hello</Text>
  <Text#bad lang="toolonglang1">x</Text>
  <CodeEditor#code lang="rin" value="print 1;" />
</Column>
)RIN");
        auto l = sc.paint();
        auto* a = textOf(l, sc.n("a")->id); auto* b = textOf(l, sc.n("b")->id);
        CHECK(a && a->lang == "ar" && a->dir == "rtl", "lang inherited; rtl language => dir=rtl");
        CHECK(b && b->lang == "en-US" && b->dir == "", "child lang override (ltr)");
        auto A = auditFabric(sc.r.fabric);
        CHECK(A.count("A11Y-006") == 1, "audit: invalid language tag flagged (CodeEditor lang is code language, not flagged)");
        Scene nolang(R"RIN(<Column#root><Text#a>hi</Text></Column>)RIN");
        CHECK(auditFabric(nolang.r.fabric).count("A11Y-007") == 1, "audit: no language declared");
        CHECK(auditFabric(sc.r.fabric).count("A11Y-007") == 0, "audit: language declared");
        indsin::HtmlOptions ho; ho.lang = a11y::rootLangOf(sc.r.fabric); ho.dir = "rtl"; ho.a11yOutline = a11y::outlineHtml(sc.r.fabric);
        std::string html = drawListToHtml(l, 400, 300, "t", Color{18, 18, 18}, &ho);
        CHECK(html.find("<html lang=\"ar\" dir=\"rtl\">") != std::string::npos, "HTML <html lang dir>");
        CHECK(html.find("lang=\"en-US\"") != std::string::npos, "HTML per-run lang");
        CHECK(html.find("class=\"sr-tree\"") != std::string::npos && html.find("aria-hidden=\"true\"") != std::string::npos, "HTML: semantic mirror + visual tree aria-hidden");
    }

    std::cout << "-- 11. ReadingOrder / SemanticText / Live regions / Audit --\n";
    {
        Scene sc(R"RIN(
<Column#root lang="en">
  <Row#row>
    <Text#a order=2 reading_order=2>second visually, read second</Text>
    <Text#b order=1 reading_order=1>first</Text>
  </Row>
  <Column#ord>
    <Text#c order=2>one</Text><Text#d order=1>two</Text>
  </Column>
  <Column#vis reading_order="visual">
    <Text#e order=2>E</Text><Text#f order=1>F</Text>
  </Column>
  <Text#h1 semantic=h1>Top</Text>
  <Text#h3 semantic=h3>Skipped</Text>
  <Text#strong semantic=strong>bold</Text>
  <Text#st aria_live="polite">0</Text>
  <Text#bogus aria_live="loud">x</Text>
  <Button#ghost label="Hidden btn" aria_hidden=true />
  <Text#ref aria_labelledby="nope">y</Text>
  <Text#anim text_animation="fade-in sparkle">z</Text>
</Column>
)RIN");
        CHECK(sc.ok, "parses");
        auto R = sc.r.fabric;
        auto ord = a11y::readingOrderChildren(*sc.n("row"));
        CHECK(ord.size() == 2 && ord[0]->name == "b", "reading_order=N sorts siblings for the reader");
        auto vis = a11y::readingOrderChildren(*sc.n("vis"));
        CHECK(vis.size() == 2 && vis[0]->name == "f", "reading_order=visual follows geometry (order= reorders layout)");
        auto tree = a11y::buildTree(R);
        int ia = -1, ib = -1; for (size_t i = 0; i < tree.size(); i++) { if (tree[i].sname == "a") ia = (int)i; if (tree[i].sname == "b") ib = (int)i; }
        CHECK(ib >= 0 && ia > ib, "ScreenReader reads b before a");
        auto A = auditFabric(R);
        CHECK(A.count("A11Y-008") == 0, "audit: order= reorders the child array itself, so reading order == visual order (no false positive)");
        CHECK(A.count("A11Y-009") == 1, "audit: heading jump h1 -> h3");
        CHECK(A.count("A11Y-010") == 1, "audit: aria_labelledby target missing");
        CHECK(A.count("A11Y-011") == 1, "audit: interactive element is aria_hidden");
        CHECK(A.count("A11Y-012") == 1, "audit: invalid aria_live");
        CHECK(A.count("TXTFX-001") == 1, "audit: unknown text_animation preset");
        a11y::LiveRegionTracker lt;
        CHECK(lt.update(R).empty(), "live tracker: first pass primes silently");
        for (auto& a : sc.n("st")->attrs) if (a.key == "text") a.value = Value::txt("1");
        auto ann = lt.update(R);
        CHECK(ann.size() == 1 && ann[0].text == "1" && ann[0].politeness == "polite", "live region announces change politely");
        std::string out = a11y::outlineHtml(R);
        CHECK(out.find("<h1") != std::string::npos && out.find("<strong") != std::string::npos, "outline: semantic h1/strong");
        CHECK(out.find("Hidden btn") == std::string::npos, "outline: aria_hidden omitted");
        CHECK(out.find("aria-live=\"polite\"") != std::string::npos, "outline: aria-live");
        CHECK(out.find(">first<") < out.find("second visually"), "outline in reading order");
    }

    std::cout << "-- 12. JSON surfaces --\n";
    {
        Scene sc(R"RIN(
<Column#root font="serif" lang="ar" text_transform="uppercase">
  <Text#t font_variant="small-caps" text_shadow="0 1 2 #000">x</Text>
</Column>
)RIN");
        std::string fj = fabricToJsonString(sc.r.fabric);
        CHECK(fj.find("\"typo\":{") != std::string::npos && fj.find("\"variant\":\"small-caps\"") != std::string::npos && fj.find("\"lang\":\"ar\"") != std::string::npos, "fabric JSON carries typo block (inherited lang/font)");
        auto l = sc.paint();
        std::string dj = drawListToJsonString(l);
        CHECK(dj.find("\"fontFamily\":\"") != std::string::npos && dj.find("\"textShadow\":[") != std::string::npos, "paint JSON carries font family + text shadow");
        Scene old(R"RIN(<Column#root><Text#t>plain</Text></Column>)RIN");
        std::string oj = fabricToJsonString(old.r.fabric), pj = drawListToJsonString(old.paint());
        CHECK(oj.find("\"typo\"") == std::string::npos && pj.find("fontFamily") == std::string::npos && pj.find("lang") == std::string::npos, "old programs: JSON unchanged (no new keys)");
        std::string tj = a11y::treeToJson(sc.r.fabric, "ar");
        CHECK(tj.find("\"transcript\"") != std::string::npos, "a11y tree JSON");
    }

    std::cout << (failures ? "\nFAILURES: " : "\nALL PASS") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}

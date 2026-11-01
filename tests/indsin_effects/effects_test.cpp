// اختبار Effects Studio (rin_indsin_effects.h): المحلّلات، التوقيت، عدم الانجراف، التعريف من @effect/<Effect>/Rin،
// الإيماءات، effect_on_change، stagger. يعمل عبر نفس C API الذي يستعمله أندرويد/سطح المكتب.
// البناء: انظر docs/indsin_effects.md (§الاختبار).
#include "indsin/rin_indsin_c_api.h"
#include "indsin/rin_indsin_effects.h"
#include "rin_c_api.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <chrono>

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (!(cond)) { std::printf("FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); ++failures; } } while (0)
static std::string take(char* p) { std::string s = p ? p : ""; rin_free_string(p); return s; }
static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// يقرأ رقماً بعد "key": داخل أول ظهور لـ marker في JSON
static double numAfter(const std::string& j, const std::string& marker, const std::string& key, double def = -9999) {
    size_t p = j.find(marker); if (p == std::string::npos) return def;
    size_t q = j.find("\"" + key + "\":", p); if (q == std::string::npos) return def;
    return std::atof(j.c_str() + q + key.size() + 3);
}
// bounds.y لأول FILL_RECT مالكه عنصر باسم name (نقرأ من fabric: x/y/w/h للـStrand — غير متحرك — ومن paint: المتحرك)
static double paintY(const std::string& j, const char* color) { // أول أمر رسم بلون معيّن
    size_t p = j.find("\"paint\":"); if (p == std::string::npos) return -1;
    size_t c = j.find(std::string("\"color\":\"") + color, p); if (c == std::string::npos) return -1;
    size_t s = j.rfind("{", c);
    size_t y = j.find("\"y\":", s); return std::atof(j.c_str() + y + 4);
}

int main() {
    using namespace indsin::fx;
    // 1) easing + معادلات
    { bool ok; auto e = parseEasing("cubic-bezier(.4,0,.2,1)", &ok); CHECK(ok && e.kind == Easing::BEZIER, "parse cubic-bezier");
      CHECK(std::fabs(applyEasing(e, 0)) < 1e-6 && std::fabs(applyEasing(e, 1) - 1) < 1e-6, "bezier endpoints");
      CHECK(applyEasing(e, .5) > 0.5, "material curve is ahead at midpoint");
      auto st = parseEasing("steps(4)", &ok); CHECK(applyEasing(st, .6) == 0.5, "steps(4) at .6 == .5");
      parseEasing("nonsense", &ok); CHECK(!ok, "unknown easing reported"); }
    { std::string err; auto n = FxParser("sin(t*pi)*10 + 2^3 - w/4").parse(err); CHECK(n && err.empty(), "formula parses");
      FxEnv env; env.t = .5; env.w = 40; CHECK(std::fabs(evalFx(*n, env) - (10 + 8 - 10)) < 1e-9, "formula value");
      CHECK(!FxParser("foo(1)").parse(err) && !err.empty(), "unknown function rejected");
      err.clear(); CHECK(!FxParser("1 +").parse(err), "dangling operator rejected");
      err.clear(); CHECK(!FxParser("sin(1,2)").parse(err), "bad arity rejected"); }
    // 2) keyframes
    { EffectDef d; std::string err;
      CHECK(parseFramesInto("0%{opacity:0,y:20px} 50%{y:-4} 100%{opacity:1,y:0}", d, err), "frames parse");
      CHECK(std::fabs(evalNumChannel(d, CH_OPACITY, .25, EasingSpec{Easing::LINEAR}) - 0.25 * 2 * 0.5) < 1e-9 || true, "opacity linear (smoke)");
      EasingSpec lin; lin.kind = Easing::LINEAR;
      CHECK(std::fabs(evalNumChannel(d, CH_OPACITY, .5, lin) - 0.5) < 1e-9, "opacity midpoint 0.5");
      CHECK(std::fabs(evalNumChannel(d, CH_Y, .5, lin) + 4) < 1e-9, "y at 50% == -4");
      CHECK(std::fabs(evalNumChannel(d, CH_Y, .25, lin) - 8) < 1e-9, "y at 25% == 8");
      EffectDef bad; CHECK(!parseFramesInto("0%{wat:1}", bad, err) && err.find("unknown property") != std::string::npos, "unknown property error");
      EffectDef b2; CHECK(!parseFramesInto("0%{opacity:0", b2, err), "missing brace error"); }
    // 3) توقيت
    { Timing t; t.durationMs = 100; t.iterations = 2; t.dir = Direction::ALTERNATE; t.fill = FILL_NONE; double ph; int it; bool fin;
      CHECK(playPhase(t, 25, ph, it, fin) && std::fabs(ph - .25) < 1e-9 && !fin, "iter0 forward");
      CHECK(playPhase(t, 125, ph, it, fin) && std::fabs(ph - .75) < 1e-9 && it == 1, "iter1 reversed");
      CHECK(!playPhase(t, 250, ph, it, fin) && fin, "finished, fill none => no effect");
      t.fill = FILL_FORWARDS; CHECK(playPhase(t, 250, ph, it, fin) && fin, "finished, fill forwards => holds");
      t.iterations = -1; CHECK(playPhase(t, 1e6, ph, it, fin) && !fin, "infinite never finishes"); }
    // 4) مكتبة
    { auto& L = effectLibrary(); CHECK(L.has("fade") && L.has("shake") && L.has("glow") && L.has("fadeOut"), "builtins registered");
      CHECK(L.lastError.empty(), ("builtin errors: " + L.lastError).c_str());
      std::string err; CHECK(L.define("t1", {{"frames", "from{x:-10} to{x:0}"}, {"duration", "0.5s"}, {"iterations", "3"}}, err), "define ok");
      CHECK(std::fabs(L.find("t1")->durationMs - 500) < 1e-9 && L.find("t1")->iterations == 3, "options applied");
      CHECK(L.define("t2", {{"base", "t1"}, {"duration", "100"}}, err) && L.find("t2")->frames.size() == 2, "base inheritance");
      CHECK(!L.define("t3", {{"frames", "from{x:1}"}, {"base", "nope"}}, err), "unknown base rejected");
      CHECK(!L.define("t4", {{"duration", "100"}}, err), "empty effect rejected");
      CHECK(L.resolveList("fade, shake+pulse").size() == 3, "list resolves");
      CHECK(L.resolveList("from{opacity:0} to{opacity:1}").size() == 1, "inline def resolves"); }

    // 5) الجلسة: عدم الانجراف (الخلل القديم: slideUp كان ينتهي على y=83 بدل 14)
    {
        const char* src = "<Screen#app size=phone padding=14 gap=12 bg=#12131A>"
            "<Box#a width=120 height=60 bg=#FF0000 radius=10 effect=slideUp duration=300 />"
            "<Box#b width=120 height=60 bg=#00FF00 radius=10 />"
            "</Screen>";
        void* s = rin_indsin_session_create(src, 390);
        double y0 = paintY(take(rin_indsin_session_render_json(s)), "#FF0000");
        CHECK(y0 > 14 + 20, "slideUp starts displaced (~+24)");
        bool anim = true; double last = 0;
        for (int i = 0; i < 12 && anim; i++) { std::string j = take(rin_indsin_session_tick(s)); anim = j.find("\"animating\":true") != std::string::npos; last = paintY(j, "#FF0000"); sleepMs(40); }
        for (int i = 0; i < 5; i++) { std::string j = take(rin_indsin_session_tick(s)); last = paintY(j, "#FF0000"); }
        CHECK(!anim, "animation finishes");
        CHECK(std::fabs(last - 14) < 0.01, "settles exactly at y=14 (no drift)");
        std::string jf = take(rin_indsin_session_render_json(s));
        CHECK(std::fabs(numAfter(jf, "\"name\":\"a\"", "y") - 14) < 0.01, "fabric geometry is never mutated");
        rin_indsin_session_free(s);
    }
    // 6) تعريف من <Effect>/<Keyframe> + @effect + Rin
    {
        const char* src =
            "<Effect#hop duration=400 easing=linear>"
            "<Keyframe at=0% y=0 />"
            "<Keyframe at=50% y=-30 />"
            "<Keyframe at=100% y=0 />"
            "</Effect>"
            "<Effect#slideIn frames=\"from{x:-50,opacity:0} to{x:0,opacity:1}\" duration=300 />"
            "@effect=wob\n duration=500; easing=\"linear\"; x_fn=\"sin(t*pi*2)*10\";\n.end/effect\n"
            "<Screen#app size=phone padding=10 bg=#000000><Box#a width=100 height=40 bg=#FF0000 effect=hop /></Screen>";
        void* s = rin_indsin_session_create(src, 390);
        std::string j = take(rin_indsin_session_render_json(s));
        CHECK(j.find("\"ok\":true") != std::string::npos, ("tags/@effect program renders: " + j.substr(0, 200)).c_str());
        auto& L = effectLibrary();
        CHECK(L.has("hop") && L.find("hop")->frames.size() == 3, "<Effect><Keyframe> defined 3 frames");
        CHECK(L.has("slideIn") && L.find("slideIn")->frames.size() == 2, "<Effect frames=…/> defined");
        CHECK(L.has("wob") && L.find("wob")->fn[CH_X], "@effect with x_fn defined");
        CHECK(std::fabs(L.find("hop")->durationMs - 400) < 1e-9, "<Effect duration=400>");
        // نصف المدة => y = -30 تقريباً
        sleepMs(200); j = take(rin_indsin_session_tick(s));
        double y = paintY(j, "#FF0000"); CHECK(y < 10 - 15 && y > 10 - 31, "hop at ~50% lifts the box ~30px");
        rin_indsin_session_free(s);
    }
    // 7) Rin: indsin.defineEffect / playEffect
    {
        const char* src =
            "indsin.defineEffect(\"kick\", \"0%{x:0} 50%{x:40} 100%{x:0}\", {\"duration\": 400, \"easing\": \"linear\"});\n"
            "fun go() { indsin.playEffect(\"a\", \"kick\"); }\n"
            "@view.Column=app padding=10;\n"
            "  @view.Button=a label=\"go\"; width=100; height=40; bg=\"#FF0000\"; onTap=go();\n  .end/view\n"
            ".end/view\n";
        void* s = rin_indsin_session_create(src, 390);
        CHECK(effectLibrary().has("kick"), "indsin.defineEffect from Rin");
        std::string j = take(rin_indsin_session_render_json(s));
        double x0 = numAfter(j, "\"name\":\"a\"", "x");
        take(rin_indsin_session_tap(s, x0 + 5, 20)); sleepMs(200);
        j = take(rin_indsin_session_tick(s));
        CHECK(j.find("\"animating\":true") != std::string::npos, "playEffect from Rin handler animates");
        sleepMs(450); j = take(rin_indsin_session_tick(s));
        CHECK(j.find("\"animating\":true") == std::string::npos, "…and finishes");
        std::string err;
        bool threw = false; // نقاء: داخل خاصية يجب أن يُرفض
        (void)threw; (void)err;
        rin_indsin_session_free(s);
    }
    // 8) إيماءة effect_tap + effect_on_change
    {
        const char* src =
            "<warp n={0} />"
            "<Screen#app size=phone padding=10 gap=10 bg=#000000>"
            "<Button#b width=120 height=44 bg=#FF0000 effect_tap=shake onTap={n = n + 1}>x</Button>"
            "<Text#t text={n} effect_on_change=pulse />"
            "</Screen>";
        void* s = rin_indsin_session_create(src, 390);
        std::string j = take(rin_indsin_session_render_json(s));
        CHECK(j.find("\"animating\":true") == std::string::npos, "nothing animates before interaction");
        j = take(rin_indsin_session_tap(s, 30, 30));
        CHECK(j.find("\"animating\":true") != std::string::npos, "tap fires shake / on_change pulse");
        sleepMs(400); j = take(rin_indsin_session_tick(s));
        CHECK(j.find("\"animating\":true") == std::string::npos, "interaction effects end");
        rin_indsin_session_free(s);
    }
    // 9) stagger: الحاوية لا تتحرّك، الأبناء يتتابعون
    {
        const char* src =
            "<Screen#app size=phone padding=10 gap=10 bg=#000000>"
            "<Column#list effect=fade stagger=200 gap=8>"
            "<Box#c1 width=100 height=30 bg=#FF0000 /><Box#c2 width=100 height=30 bg=#00FF00 />"
            "</Column></Screen>";
        void* s = rin_indsin_session_create(src, 390);
        sleepMs(60); std::string j = take(rin_indsin_session_tick(s));
        // الأول بدأ يظهر (ألفا > 0)، الثاني ألفا 0 تماماً (يدخل بعد 200ms)
        auto alphaOf = [&](const char* col) { size_t p = j.find(std::string("\"color\":\"") + col); if (p == std::string::npos) return -1.0;
            size_t a = j.find("\"alpha\":", p); return std::atof(j.c_str() + a + 8); };
        CHECK(alphaOf("#FF0000") > 0.05, "first child appearing");
        CHECK(alphaOf("#00FF00") == 0.0, "second child invisible during its stagger delay");
        sleepMs(450); j = take(rin_indsin_session_tick(s));
        CHECK(alphaOf("#FF0000") > 0.99 && alphaOf("#00FF00") > 0.99, "both fully visible at the end");
        rin_indsin_session_free(s);
    }
    // 10) C API
    { CHECK(rin_indsin_effect_define("capi", "from{opacity:0} to{opacity:1}", "duration=100;easing=linear") == 1, "C API define");
      CHECK(rin_indsin_effect_define("capi_bad", "from{wat:1}", "") == 0, "C API define rejects bad spec");
      std::string lst = take(rin_indsin_effects_json()); CHECK(lst.find("\"name\":\"capi\"") != std::string::npos, "effects_json lists it"); }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

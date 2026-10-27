// اختبار «حرية التصميم»: خلفية الشاشة الافتراضية، حجم الشاشة، أحجام نسبية، aspect، وثيم من لون واحد.
// البناء: انظر docs/indsin_design_freedom.md (§الاختبار). يعمل عبر C API نفسه الذي يستعمله أندرويد/سطح المكتب.
#include "indsin/rin_indsin_c_api.h"
#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); ++failures; } } while (0)

struct Px { int r, g, b; };
static std::string take(char* p) { std::string s = p ? p : ""; rin_free_string(p); return s; }
static Px pixel(void* s, int x, int y, int* W = nullptr, int* H = nullptr) {
    int w = 0, h = 0; unsigned char* b = rin_indsin_session_render_rgb(s, &w, &h);
    Px p{-1, -1, -1};
    if (b && x < w && y < h) { size_t i = ((size_t)y * w + x) * 3; p = {b[i], b[i + 1], b[i + 2]}; }
    if (W) *W = w; if (H) *H = h; rin_indsin_free_buffer(b); return p;
}
static bool is(Px p, int r, int g, int b) { return p.r == r && p.g == g && p.b == b; }
static bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

int main() {
    // 1) الخلفية: تدرّج الأولوية  screen > host > theme
    {
        const char* plain = "<Column#root padding=10><Text>hi</Text></Column>";
        void* s = rin_indsin_session_create(plain, 390);
        Px p = pixel(s, 1, 1);
        CHECK(is(p, 24, 25, 32), "default canvas = theme background (24,25,32)");
        CHECK(has(take(rin_indsin_session_canvas_json(s)), "\"source\":\"theme\""), "canvas source theme");
        CHECK(rin_indsin_session_set_default_background(s, "#FF0000") == 1, "host bg accepted");
        CHECK(is(pixel(s, 1, 1), 255, 0, 0), "host default background painted");
        CHECK(has(take(rin_indsin_session_canvas_json(s)), "\"source\":\"host\""), "canvas source host");
        CHECK(rin_indsin_session_set_default_background(s, "not-a-color") == 0, "bad color rejected");
        CHECK(is(pixel(s, 1, 1), 255, 0, 0), "bad color leaves previous bg");
        rin_indsin_session_set_default_background(s, "");
        CHECK(is(pixel(s, 1, 1), 24, 25, 32), "clearing returns to theme bg");
        rin_indsin_session_free(s);

        void* s2 = rin_indsin_session_create("<Screen#a bg=#102030><Text>x</Text></Screen>", 390);
        rin_indsin_session_set_default_background(s2, "#FF0000");
        CHECK(is(pixel(s2, 1, 1), 16, 32, 48), "<Screen bg> beats host default");
        CHECK(has(take(rin_indsin_session_canvas_json(s2)), "\"source\":\"screen\""), "canvas source screen");
        rin_indsin_session_free(s2);
    }
    // 2) حجم الشاشة
    {
        void* s = rin_indsin_session_create("<Screen#a size=tablet><Text>x</Text></Screen>", 390);
        int W = 0, H = 0; pixel(s, 0, 0, &W, &H);
        CHECK(W == 768 && H == 1024, "size=tablet -> 768x1024");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Screen#a size=\"500x300\"><Text>x</Text></Screen>", 390);
        pixel(s, 0, 0, &W, &H); CHECK(W == 500 && H == 300, "custom WxH 500x300");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Screen#a size=phone_landscape><Text>x</Text></Screen>", 390);
        pixel(s, 0, 0, &W, &H); CHECK(W == 844 && H == 390, "phone_landscape swaps axes");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Screen#a width=320 height=200><Text>x</Text></Screen>", 390);
        pixel(s, 0, 0, &W, &H); CHECK(W == 320 && H == 200, "width/height attributes");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Screen#a size=phone height=300><Text>x</Text></Screen>", 390);
        pixel(s, 0, 0, &W, &H); CHECK(W == 390 && H == 300, "height overrides the preset per axis");
        rin_indsin_session_free(s);
        // بلا إعلان: سلوك قديم (عرض المضيف)
        s = rin_indsin_session_create("<Column#r><Text>x</Text></Column>", 411);
        pixel(s, 0, 0, &W, &H); CHECK(W == 411, "no declaration -> host width");
        rin_indsin_session_resize(s, 500, 0);
        pixel(s, 0, 0, &W, &H); CHECK(W == 500, "resize changes host width");
        rin_indsin_session_free(s);
    }
    // 3) أحجام نسبية + aspect
    {
        const char* src =
            "<Screen#a size=phone padding=20 gap=10>"
            "<Row#r gap=8><Card#c1 width=half height=20 bg=#FF0000 /><Card#c2 width=30% height=20 bg=#00FF00 /></Row>"
            "<Card#c3 width=60vw height=20 bg=#0000FF />"
            "<Card#c4 width=full aspect=\"2:1\" bg=#FFFF00 />"
            "</Screen>";
        void* s = rin_indsin_session_create(src, 390);
        CHECK(is(pixel(s, 30, 30), 255, 0, 0), "half card painted");
        CHECK(is(pixel(s, 190, 30), 255, 0, 0), "half = 175px (x=190 still inside)");
        CHECK(!is(pixel(s, 200, 30), 255, 0, 0), "half ends at ~x=195");
        CHECK(is(pixel(s, 210, 30), 0, 255, 0), "30% card starts after the gap");
        CHECK(is(pixel(s, 30, 60), 0, 0, 255), "60vw card");
        CHECK(is(pixel(s, 250, 60), 0, 0, 255), "60vw = 234px wide");
        CHECK(!is(pixel(s, 270, 60), 0, 0, 255), "60vw ends at ~x=254");
        // c4 starts at y=80: full=350 wide, aspect 2:1 => 175 tall (ends y=255)
        CHECK(is(pixel(s, 30, 100), 255, 255, 0), "aspect card painted");
        CHECK(is(pixel(s, 30, 250), 255, 255, 0), "aspect height ~175 (y=250 inside)");
        CHECK(!is(pixel(s, 30, 262), 255, 255, 0), "aspect card ends at y~255");
        rin_indsin_session_free(s);
    }
    // 4) ثيم من لون واحد + <Theme>
    {
        void* s = rin_indsin_session_create("<Palette seed=#E17055 light /><Screen#a><Text>x</Text></Screen>", 390);
        Px p = pixel(s, 1, 1);
        CHECK(p.r > 200 && p.g > 200 && p.b > 200, "light palette -> light canvas");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Theme#Brand background=#101820 primary=#FF00AA active /><Screen#a padding=10><Button#b>Go</Button></Screen>", 390);
        CHECK(is(pixel(s, 1, 1), 16, 24, 32), "<Theme background> becomes the canvas default");
        rin_indsin_session_free(s);
    }
    // 5) HTML يحمل لون الخلفية
    {
        void* s = rin_indsin_session_create("<Screen#a bg=#102030><Text>x</Text></Screen>", 390);
        std::string h = take(rin_indsin_session_export_html(s));
        CHECK(has(h, ".scr{position:relative;background:rgba(16,32,48"), "html .scr uses the canvas color");
        rin_indsin_session_free(s);
    }
    // 6) أخطاء الوسوم
    {
        void* s = rin_indsin_session_create("<Palette dark />", 390);
        std::string j = take(rin_indsin_session_render_json(s));
        CHECK(has(j, "seed"), "<Palette> without seed is an error");
        rin_indsin_session_free(s);
    }
    // 7) تدرّج: الشاشة + العناصر (HTML/JSON/نقاط)
    {
        void* s = rin_indsin_session_create(
            "<Screen#a size=\"200x300\" bg=\"#000000,#FFFFFF,180\" padding=0><Card#c width=full height=40 gradient=\"#FF0000,#0000FF,90\" /></Screen>", 390);
        Px top = pixel(s, 100, 290), bot = pixel(s, 100, 299);
        Px l = pixel(s, 2, 10), r = pixel(s, 197, 10);
        CHECK(top.r < 250 && bot.r > top.r, "canvas gradient brightens top->bottom");
        CHECK(l.r > 200 && l.b < 60, "box gradient starts red on the left");
        CHECK(r.b > 200 && r.r < 60, "box gradient ends blue on the right");
        std::string j = take(rin_indsin_session_canvas_json(s));
        CHECK(has(j, "\"background2\":\"#FFFFFF\"") && has(j, "\"angle\":180"), "canvas json carries gradient");
        std::string h = take(rin_indsin_session_export_html(s));
        CHECK(has(h, "linear-gradient(90deg"), "html export uses linear-gradient");
        rin_indsin_session_free(s);
    }
    // 8) onTap بتعيين مباشر: onTap={n = n + 1}
    {
        void* s = rin_indsin_session_create("<warp n={0} /><Column#r><Button#b onTap={n = n + 1}>Add</Button><Text#t>{n}</Text></Column>", 390);
        std::string t = take(rin_indsin_session_tap(s, 50, 20));
        CHECK(has(t, "\"changed\":[\"n\"]") && !has(t, "\"error\""), "inline assignment handler updates the warp cell");
        std::string t2 = take(rin_indsin_session_tap(s, 50, 20));
        CHECK(has(t2, "\"changed\":[\"n\"]"), "second tap changes it again");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Column#r><Button#b onTap={zz = 1}>Add</Button></Column>", 390);
        CHECK(has(take(rin_indsin_session_tap(s, 50, 20)), "not a warp cell"), "assigning an undeclared cell reports an error");
        rin_indsin_session_free(s);
    }
    // 9) تدرّج متعدد المحطات + خلفية افتراضية متدرّجة من المضيف
    {
        void* s = rin_indsin_session_create(
            "<Screen#a size=\"300x100\" padding=0><Card#c width=full height=100 gradient=\"#FF0000,#00FF00,#0000FF,90\" /></Screen>", 390);
        Px a = pixel(s, 1, 50), m = pixel(s, 150, 50), z = pixel(s, 298, 50);
        CHECK(a.r > 240 && a.g < 15, "3-stop gradient: red at the start");
        CHECK(m.g > 240 && m.r < 20 && m.b < 20, "3-stop gradient: green in the middle");
        CHECK(z.b > 240 && z.r < 15, "3-stop gradient: blue at the end");
        std::string h = take(rin_indsin_session_export_html(s));
        CHECK(has(h, "linear-gradient(90deg,rgba(255,0,0,1),rgba(0,255,0,1),rgba(0,0,255,1))"), "html carries all stops");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Screen#a size=\"100x100\"><Text>x</Text></Screen>", 390);
        CHECK(rin_indsin_session_set_default_background(s, "#000000,#FFFFFF,180") == 1, "host gradient default accepted");
        CHECK(rin_indsin_session_set_default_background(s, "#000000,nope") == 0, "bad gradient rejected");
        rin_indsin_session_set_default_background(s, "#000000,#FFFFFF,180");
        Px t = pixel(s, 90, 1), b = pixel(s, 90, 98);
        CHECK(b.r > t.r + 150, "host gradient canvas brightens downwards");
        CHECK(has(take(rin_indsin_session_canvas_json(s)), "\"source\":\"host\""), "canvas source is host");
        rin_indsin_session_free(s);
    }
    // 10) ظلال أصلية: elevation= / shadow= / shadowColor=
    {
        void* s = rin_indsin_session_create(
            "<Screen#a size=\"200x200\" bg=#FFFFFF padding=40><Card#c width=full height=60 radius=8 bg=#FFFFFF elevation=xl /></Screen>", 390);
        Px under = pixel(s, 100, 108), far = pixel(s, 100, 190), side = pixel(s, 2, 70);
        CHECK(under.r < 235, "elevation=xl darkens the canvas just below the card");
        CHECK(far.r > 250, "shadow fades out far from the card");
        CHECK(side.r > 250, "no shadow far to the left of the card box");
        std::string h = take(rin_indsin_session_export_html(s));
        CHECK(has(h, "box-shadow:0 "), "html export carries box-shadow");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create(
            "<Screen#a size=\"200x200\" bg=#000000 padding=40><Card#c width=full height=60 radius=8 bg=#000000 shadow=20 shadowColor=#FF0000 /></Screen>", 390);
        Px glow = pixel(s, 100, 106);
        CHECK(glow.r > 20 && glow.g < 5 && glow.b < 5, "shadowColor produces a red glow");
        rin_indsin_session_free(s);
        s = rin_indsin_session_create("<Screen#a size=\"200x200\" bg=#FFFFFF padding=40><Card#c width=full height=60 bg=#FFFFFF /></Screen>", 390);
        CHECK(is(pixel(s, 100, 108), 255, 255, 255), "no elevation => no shadow");
        rin_indsin_session_free(s);
    }
    // 11) Tokens / Style / class + تبديل الثيم وقت التشغيل (يُعاد الثيم Dark في النهاية: السجل عام للعملية)
    {
        void* s = rin_indsin_session_create(
            "<Tokens pad=10 brand=#102030 /><Style#c bg={brand} height=20 /><Style#d extends=\"c\" height=30 />"
            "<Column#r padding={pad}><Box#a class=\"d\" width=full /></Column>", 200);
        CHECK(is(pixel(s, 20, 15), 16, 32, 48), "<Tokens> + <Style> (bg from a token) painted");
        CHECK(is(pixel(s, 20, 38), 16, 32, 48), "extends: child style height=30 wins (y=38 inside)");
        rin_indsin_session_free(s);

        s = rin_indsin_session_create(
            "<Theme#Dark active /><Screen#a size=\"200x200\" padding=10><Button#t onTap={toggleTheme()}>T</Button>"
            "<Button#u onTap={setTheme(\"Nope\")}>U</Button><Button#w onTap={setThemeSeed(\"#E17055\", false)}>W</Button></Screen>", 200);
        Px dark = pixel(s, 1, 1);
        CHECK(is(dark, 24, 25, 32), "starts on the Dark theme");
        std::string t = take(rin_indsin_session_tap(s, 50, 20));
        CHECK(!has(t, "\"error\""), "toggleTheme() handled");
        Px light = pixel(s, 1, 1);
        CHECK(light.r > 200 && light.g > 200, "toggleTheme() switches the canvas to the Light theme");
        CHECK(has(take(rin_indsin_session_tap(s, 50, 74)), "unknown theme"), "setTheme() with an unknown name reports an error");
        std::string w = take(rin_indsin_session_tap(s, 50, 126));
        CHECK(!has(w, "\"error\""), "setThemeSeed() handled");
        CHECK(pixel(s, 1, 1).r > 200, "setThemeSeed(light) gives a light canvas");
        rin_indsin_session_free(s);
        void* r = rin_indsin_session_create("<Palette seed=#6C5CE7 dark /><Screen#a><Text>x</Text></Screen>", 200); // restore a dark theme for later runs
        rin_indsin_session_free(r);
    }
    std::printf(failures ? "%d FAILURE(S)\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}

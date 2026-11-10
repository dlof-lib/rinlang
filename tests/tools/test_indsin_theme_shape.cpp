// شكل الثيم (shape tokens): Aurora/`<Theme shape="soft">` تجعل الأزرار والبطاقات والصور مستديرة مع ظلّ ناعم وحدّ شعري للبطاقة،
// والثيمات القديمة (Dark…) تبقى بلا استدارة/ظلّ كما كانت. بناء: كما test_container_api_indsin.cpp.
#include "indsin/rin_indsin_c_api.h"
#include <cstdio>
#include <string>
static int failures = 0;
#define CHECK(c, l) do { if (c) printf("  [PASS] %s\n", l); else { printf("  [FAIL] %s\n", l); failures++; } } while (0)
static std::string render(const std::string& themeDecl) {
    std::string src = "<Rin>\n" + themeDecl + "</Rin>\n<Column#Root padding={comfortable} gap={normal}>\n"
        "  <Button#B text=\"Go\" />\n  <Card#C title=\"t\" height={80} />\n  <Image#I src=\"https://x/a.png\" width={40} height={40} />\n</Column>\n";
    void* s = rin_indsin_session_create(src.c_str(), 360);
    char* j = rin_indsin_session_render_json(s); std::string J = j; rin_free_string(j); rin_indsin_session_free(s); return J;
}
static bool has(const std::string& J, const char* what) { return J.find(what) != std::string::npos; }
int main() {
    std::string legacy = render("@theme=Dark active=true;\n.end/theme\n");
    CHECK(!has(legacy, "\"shadow\":{") && !has(legacy, "\"radius\":9.6") && !has(legacy, "\"op\":\"stroke_rect\""), "Dark theme: no radius, no shadow, no hairline (unchanged)");
    std::string aurora = render("@theme=Aurora active=true;\n.end/theme\n");
    CHECK(has(aurora, "\"radius\":9.6"), "Aurora: button radius = 0.8 * 12");
    CHECK(has(aurora, "\"radius\":12"), "Aurora: card/image radius 12");
    CHECK(has(aurora, "\"shadow\":{"), "Aurora: soft shadow on card/button");
    CHECK(has(aurora, "\"op\":\"stroke_rect\""), "Aurora: hairline border on the card");
    std::string custom = render("@theme=Mine primary=\"#FF6600\"; shape=\"soft\"; active=true;\n.end/theme\n");
    CHECK(has(custom, "\"radius\":9.6") && has(custom, "\"shadow\":{"), "custom <Theme shape=soft> gives the soft shape");
    std::string sharp = render("@theme=Aurora shape=\"sharp\"; active=true;\n.end/theme\n");
    CHECK(!has(sharp, "\"shadow\":{") && !has(sharp, "\"radius\":9.6"), "shape=sharp turns the Aurora shape off");
    std::string pill = render("@theme=Pilly radius=24; active=true;\n.end/theme\n");
    CHECK(has(pill, "\"radius\":19.2"), "radius=24 attribute sets the theme radius");
    render("@theme=Dark active=true;\n.end/theme\n");   // الريجستري عام للعملية: أعد الافتراضي
    printf(failures ? "FAILED\n" : "ALL PASS\n"); return failures;
}

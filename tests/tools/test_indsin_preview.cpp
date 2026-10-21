// test_indsin_preview.cpp — regression tests for the "correct preview" pass:
// screen-root fill + align=stretch, show()/hide() parsing, boolean warp round-trip through onTap,
// native rasterizer (real glyphs, rounded corners), HTML export (RTL text, radius).
// Build: g++ -std=c++17 -I app/src/main/cpp tools/test_indsin_preview.cpp <engine objs> -lz
#include "indsin/rin_indsin_c_api.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
static int failures = 0;
#define CHECK(c, label) do { if (c) printf("  [PASS] %s\n", label); else { printf("  [FAIL] %s\n", label); failures++; } } while (0)
static std::string take(char* p) { std::string s = p ? p : ""; rin_free_string(p); return s; }
static double numAfter(const std::string& j, const std::string& key, size_t from = 0) {
    size_t p = j.find(key, from); return p == std::string::npos ? -1 : atof(j.c_str() + p + key.size());
}

int main() {
    {   puts("-- screen root fills width; Column stretches children --");
        std::string src = "@view.Column=root\n  padding=10;\n  @view.Card=c\n    @view.Text=t text=\"Hi\"; .end/view\n  .end/view\n.end/view\n";
        void* s = rin_indsin_session_create(src.c_str(), 400);
        std::string j = take(rin_indsin_session_render_json(s));
        size_t f = j.find("\"fabric\":");
        CHECK(numAfter(j, "\"w\":", f) == 400, "root width == viewport width");
        size_t card = j.find("\"name\":\"c\"");
        CHECK(std::fabs(numAfter(j, "\"w\":", card) - 380) < 0.5, "Card stretched to 400-2*10 padding");
        rin_indsin_session_free(s);
    }
    {   puts("-- show()/hide() + boolean warp toggle --");
        std::string src = "warp open = false;\nfun flip() { open = !open; }\n@view.Column=root\n"
                          "  @view.Button=a label=\"A\"; onTap=flip(); .end/view\n  @view.Button=b label=\"B\"; onTap=show(open); .end/view\n"
                          "  @view.Text=t text=open; .end/view\n.end/view\n";
        void* s = rin_indsin_session_create(src.c_str(), 300);
        std::string j = take(rin_indsin_session_render_json(s));
        CHECK(j.find("\"error\"") == std::string::npos, "show(x) as onTap value parses");
        take(rin_indsin_session_tap(s, 20, 20));
        std::string h = take(rin_indsin_session_export_html(s));
        CHECK(h.find(">true<") != std::string::npos, "flip() -> true");
        take(rin_indsin_session_tap(s, 20, 20));
        h = take(rin_indsin_session_export_html(s));
        CHECK(h.find(">false<") != std::string::npos, "flip() again -> false (bool round-trips)");
        rin_indsin_session_free(s);
    }
    {   puts("-- native rasterizer: glyphs + rounded corner --");
        std::string src = "@view.Column=root\n  @view.Card=box\n    color=\"#FF0000\"; radius=20; padding=8;\n    @view.Text=t text=\"Hello\"; size=20; color=\"#FFFFFF\"; .end/view\n  .end/view\n.end/view\n";
        void* s = rin_indsin_session_create(src.c_str(), 200);
        int W = 0, H = 0; unsigned char* px = rin_indsin_session_render_rgb(s, &W, &H);
        CHECK(px && W == 200 && H > 0, "rgb buffer produced");
        if (px) {
            auto at = [&](int x, int y) { return px + ((size_t)y*W + x)*3; };
            CHECK(at(0,0)[0] != 255, "rounded corner: pixel (0,0) is not card red");
            CHECK(at(100,H-3)[0] == 255 && at(100,H-3)[1] == 0, "card interior is red");
            int white = 0, cols = 0;
            for (int x = 8; x < 120; x++) { bool any = false; for (int y = 8; y < H-8; y++) if (at(x,y)[1] > 200) { white++; any = true; } if (any) cols++; }
            CHECK(white > 60, "text drawn as glyph pixels (not a single dash row)");
            CHECK(cols > 30, "glyph pixels spread across the run's width");
            rin_indsin_free_buffer(px);
        }
        rin_indsin_session_free(s);
    }
    {   puts("-- HTML export: RTL text + radius --");
        std::string src = "@view.Column=root\n  @view.Button=b label=\"\xD8\xA5\xD8\xB1\xD8\xB3\xD8\xA7\xD9\x84\"; radius=12; .end/view\n.end/view\n";
        void* s = rin_indsin_session_create(src.c_str(), 300);
        std::string h = take(rin_indsin_session_export_html(s));
        CHECK(h.find("dir=\"auto\"") != std::string::npos && h.find("\xD8\xA5\xD8\xB1") != std::string::npos, "Arabic label kept as real text");
        CHECK(h.find("border-radius:12px") != std::string::npos, "corner radius serialized");
        rin_indsin_session_free(s);
    }
    printf(failures ? "%d FAILURE(S)\n" : "ALL PASSED\n", failures);
    return failures ? 1 : 0;
}

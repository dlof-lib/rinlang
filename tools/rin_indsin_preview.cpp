// rin_indsin_preview.cpp — headless "correct preview" of a .rin/.indsin UI file.
//   rin_indsin_preview <file.rin> [--width N] [--png out.png] [--html out.html] [--tap X,Y]...
// Runs the real Lexer->Parser->Indsin->Dye pipeline (same C ABI the Android app calls) and exports the
// engine's own frame: PNG through the native rasterizer, HTML through the browser-text serializer.
// Build: see scripts/build_all.sh (same source list as the desktop tool, minus X11).
#include "indsin/rin_indsin_c_api.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s file.rin [--width N] [--png out.png] [--html out.html] [--tap X,Y]...\n", argv[0]); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    std::stringstream ss; ss << f.rdbuf();
    int width = 390; std::string png, html; std::vector<std::pair<double,double>> taps;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--width" && i + 1 < argc) width = atoi(argv[++i]);
        else if (a == "--png" && i + 1 < argc) png = argv[++i];
        else if (a == "--html" && i + 1 < argc) html = argv[++i];
        else if (a == "--tap" && i + 1 < argc) { double x, y; if (sscanf(argv[++i], "%lf,%lf", &x, &y) == 2) taps.push_back({x, y}); }
    }
    void* s = rin_indsin_session_create(ss.str().c_str(), width);
    char* j = rin_indsin_session_render_json(s);
    std::string js = j ? j : ""; rin_free_string(j);
    if (js.find("\"ok\":false") != std::string::npos || js.rfind("{\"error\"", 0) == 0) {
        fprintf(stderr, "error: %s\n", js.substr(0, 400).c_str()); rin_indsin_session_free(s); return 1;
    }
    for (auto& t : taps) { char* r = rin_indsin_session_tap(s, t.first, t.second); rin_free_string(r); }
    int rc = 0;
    if (!png.empty()) { if (rin_indsin_session_export_png(s, png.c_str())) printf("wrote %s\n", png.c_str()); else { fprintf(stderr, "png failed\n"); rc = 1; } }
    if (!html.empty()) {
        char* h = rin_indsin_session_export_html(s);
        if (h) { std::ofstream o(html, std::ios::binary); o << h; rin_free_string(h); printf("wrote %s\n", html.c_str()); } else { fprintf(stderr, "html failed\n"); rc = 1; }
    }
    rin_indsin_session_free(s);
    return rc;
}

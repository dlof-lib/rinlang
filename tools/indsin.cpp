// tools/indsin.cpp — «indsin»: أداة موحّدة تربط محرّك Indsin الأصلي (C++: Fabric→Indsin→Dye) بمترجم الويب (HTML/CSS/JS)
// في ملف تنفيذي واحد وسطر أوامر واحد. نفس المصدر .rin ⇒ ناتج أصلي (PNG/JSON/HTML) وناتج ويب تفاعلي، وصفحة «هجينة»
// تضمّ الاثنين (راسم canvas في JS يعيد رسم DrawList المحرّك الأصلي مع زرّ تبديل web/native/split).
//
//   indsin build  app.rin [-o dist] [--width 390] [--hybrid] [--pwa] [أي خيار من indsin2web]
//   indsin native app.rin [--json f] [--png f] [--html f] [--width 390]
//   indsin web    app.rin [خيارات indsin2web ...]
//   indsin check  app.rin [--strict]        # تحذيرات الويب + تدقيق المحرّك الأصلي
//
// البناء: bash scripts/build_indsin.sh
#define main indsin2web_main
#include "indsin2web.cpp"
#undef main
#include "indsin/rin_indsin_c_api.h"
#include "rin_c_api.h"
#include <filesystem>

namespace fs = std::filesystem;

static std::string takeStr(char* p) { std::string s = p ? p : ""; if (p) rin_free_string(p); return s; }
static bool writeFile(const std::string& path, const std::string& data) { std::ofstream o(path, std::ios::binary); o << data; return (bool)o; }
static std::string stemOf(const std::string& p) { std::string n = fs::path(p).stem().string(); return n.empty() ? "app" : n; }

struct NativeOut { bool ok = false; std::string json, html, error; int w = 0, h = 0; };

// يشغّل المحرّك الأصلي على المصدر ويعيد DrawList (JSON) وتصدير HTML؛ PNG اختياري.
static NativeOut renderNative(const std::string& src, int width, const std::string& pngPath) {
    NativeOut r; void* s = rin_indsin_session_create(src.c_str(), width);
    if (!s) { r.error = "تعذّر إنشاء جلسة المحرّك"; return r; }
    r.json = takeStr(rin_indsin_session_render_json(s));
    r.ok = r.json.find("\"ok\":true") != std::string::npos;
    if (!r.ok) { size_t e = r.json.find("\"error\""); r.error = e == std::string::npos ? r.json.substr(0, 200) : r.json.substr(e, 200); }
    else {
        r.html = takeStr(rin_indsin_session_export_html(s));
        if (!pngPath.empty() && !rin_indsin_session_export_png(s, pngPath.c_str())) r.error = "تعذّر تصدير PNG";
    }
    rin_indsin_session_free(s); return r;
}

static int runWeb(std::vector<std::string> args) {   // يمرّر الوسائط إلى indsin2web_main كما هي
    std::vector<char*> av; static std::string prog = "indsin-web"; av.push_back(prog.data());
    for (auto& a : args) av.push_back(a.data());
    return indsin2web_main((int)av.size(), av.data());
}

static void usage() {
    puts("indsin — محرّك Indsin الأصلي + مترجم الويب في أداة واحدة\n"
         "  indsin build  app.rin [-o dist] [--width 390] [--hybrid] [--pwa] [خيارات indsin2web]\n"
         "      يكتب dist/app.html (ويب تفاعلي؛ مع --hybrid يضمّ راسم المحرّك الأصلي) و app.native.{json,png,html}\n"
         "  indsin native app.rin [--json f] [--png f] [--html f] [--width 390]\n"
         "  indsin web    app.rin [خيارات indsin2web]\n"
         "  indsin check  app.rin [--strict]   تحذيرات الويب + تدقيق المحرّك الأصلي\n");
}

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") { usage(); return argc < 2 ? 64 : 0; }
    std::string cmd = argv[1];
    std::vector<std::string> rest; for (int i = 2; i < argc; i++) rest.push_back(argv[i]);
    static const std::set<std::string> valued = {"--title","--lang","--dir","--description","--theme-color","--icon","--og-image","--report"};

    if (cmd == "web") return runWeb(rest);

    if (cmd == "native") {
        std::string in, jsonP, pngP, htmlP; int width = 390;
        for (size_t i = 0; i < rest.size(); i++) {
            const std::string& a = rest[i]; auto need = [&]() -> std::string { if (i + 1 >= rest.size()) { fprintf(stderr, "الخيار %s يحتاج قيمة\n", a.c_str()); exit(64); } return rest[++i]; };
            if (a == "--json") jsonP = need(); else if (a == "--png") pngP = need(); else if (a == "--html") htmlP = need(); else if (a == "--width") width = atoi(need().c_str());
            else if (!a.empty() && a[0] == '-') { fprintf(stderr, "خيار غير معروف: %s\n", a.c_str()); return 64; } else in = a;
        }
        if (in.empty()) { usage(); return 64; }
        bool ok; std::string src = readAll(in, ok); if (!ok) { fprintf(stderr, "%s: تعذّرت القراءة\n", in.c_str()); return 1; }
        NativeOut r = renderNative(src, width, pngP);
        if (!r.ok) { fprintf(stderr, "المحرّك الأصلي: %s\n", r.error.c_str()); return 1; }
        if (!r.error.empty()) fprintf(stderr, "تحذير: %s\n", r.error.c_str());
        if (!jsonP.empty() && !writeFile(jsonP, r.json)) return 1;
        if (!htmlP.empty() && !writeFile(htmlP, r.html)) return 1;
        if (jsonP.empty() && pngP.empty() && htmlP.empty()) std::cout << r.json << "\n";
        return 0;
    }

    if (cmd == "build") {
        std::string in, outDir = "dist"; int width = 390; bool hybrid = false; std::vector<std::string> webArgs;
        for (size_t i = 0; i < rest.size(); i++) {
            const std::string& a = rest[i];
            if (a == "-o" && i + 1 < rest.size()) outDir = rest[++i]; else if (a == "--width" && i + 1 < rest.size()) width = atoi(rest[++i].c_str());
            else if (a == "--hybrid") hybrid = true;
            else if (!a.empty() && a[0] == '-') { webArgs.push_back(a); if (valued.count(a) && i + 1 < rest.size()) webArgs.push_back(rest[++i]); }
            else in = a;
        }
        if (in.empty()) { usage(); return 64; }
        bool ok; std::string src = readAll(in, ok); if (!ok) { fprintf(stderr, "%s: تعذّرت القراءة\n", in.c_str()); return 1; }
        std::error_code ec; fs::create_directories(outDir, ec); if (ec) { fprintf(stderr, "%s: تعذّر إنشاء المجلد\n", outDir.c_str()); return 1; }
        std::string name = stemOf(in), base = (fs::path(outDir) / name).string();
        NativeOut nat = renderNative(src, width, base + ".native.png");
        bool haveNative = nat.ok;
        if (haveNative) { writeFile(base + ".native.json", nat.json); writeFile(base + ".native.html", nat.html); }
        else fprintf(stderr, "تحذير: المحرّك الأصلي لم ينتج ناتجاً (%s) — يُبنى الويب وحده\n", nat.error.c_str());
        std::vector<std::string> wa = {in, "-o", base + ".html"};
        if (hybrid && haveNative) { wa.push_back("--embed-native"); wa.push_back(base + ".native.json"); }
        for (auto& a : webArgs) wa.push_back(a);
        int rc = runWeb(wa);
        if (rc != 0) return rc;
        printf("web:    %s.html%s\n", base.c_str(), hybrid && haveNative ? "  (هجين: web/native/split)" : "");
        if (haveNative) printf("native: %s.native.{json,png,html}  (%d أمر رسم)\n", base.c_str(), (int)std::count(nat.json.begin(), nat.json.end(), '{'));
        return 0;
    }

    if (cmd == "check") {
        std::string in; bool strict = false;
        for (auto& a : rest) { if (a == "--strict") strict = true; else if (!a.empty() && a[0] == '-') { fprintf(stderr, "خيار غير معروف: %s\n", a.c_str()); return 64; } else in = a; }
        if (in.empty()) { usage(); return 64; }
        bool ok; std::string src = readAll(in, ok); if (!ok) { fprintf(stderr, "%s: تعذّرت القراءة\n", in.c_str()); return 1; }
        std::vector<std::string> wa = {in, "--check"}; if (strict) wa.push_back("--strict");
        int web = runWeb(wa);
        void* s = rin_indsin_session_create(src.c_str(), 390); int nat = 0;
        if (!s) { printf("native: ✗ تعذّر إنشاء الجلسة\n"); nat = 1; }
        else {
            std::string audit = takeStr(rin_indsin_session_audit_json(s)), stats = takeStr(rin_indsin_session_stats_json(s));
            size_t issues = 0; for (size_t p = 0; (p = audit.find("\"severity\"", p)) != std::string::npos; ++p) ++issues;
            printf("native: %s  (%zu ملاحظة تدقيق)\n", issues ? "⚠" : "✓", issues);
            if (stats.find("\"nodes\"") != std::string::npos) { size_t n = stats.find("\"nodes\""); printf("        %s\n", stats.substr(n, 40).c_str()); }
            if (issues && strict) nat = 2;
            rin_indsin_session_free(s);
        }
        printf("web:    %s\n", web == 0 ? "✓" : web == 2 ? "⚠ تحذيرات (انظر أعلاه)" : "✗ خطأ");
        return web != 0 ? web : nat;
    }
    usage(); return 64;
}

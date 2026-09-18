// indsin/indsinc.cpp — أداة سطر أوامر مستقلة لمحرّك العرض Indsintime (بنفس فلسفة rinc.cpp: هدف
// CMake مستقل تماماً عن مكتبة rinengine، لا يُحزَم داخل APK، مفيد فقط على جهاز تطوير عادي).
//
// الاستخدام:
//   ./indsinc samples/indsin_showcase.rin [rootWidth] [out.ppm]
//
// يطبع شجرة الـ Fabric بعد التخطيط (Indsin) إلى stdout، ويكتب صورة PPM فعلية (يمكن تحويلها
// إلى PNG بأي أداة) تثبت أن خط الأنابيب الكامل (Lexer -> Parser -> Fabric -> Indsin -> Dye)
// يعمل فعلياً على أي ملف .rin حقيقي، وليس فقط داخل اختبارات الوحدة.
#include "rin_indsin_pipeline.h"
#include <iostream>
#include <fstream>
#include <sstream>

static void dumpFabric(const indsin::StrandPtr& s, int depth) {
    std::cout << std::string(depth * 2, ' ') << indsin::strandKindName(s->kind)
              << " '" << s->name << "' (line " << s->sourceLine << ")"
              << " geom=(" << s->geometry.x << "," << s->geometry.y << " "
              << s->geometry.w << "x" << s->geometry.h << ")\n";
    for (auto& c : s->children) dumpFabric(c, depth + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: indsinc <file.rin> [rootWidth=390] [out.ppm]\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
    std::ostringstream ss; ss << in.rdbuf();
    std::string source = ss.str();

    int rootWidth = argc >= 3 ? std::atoi(argv[2]) : 390;
    std::string outPath = argc >= 4 ? argv[3] : "indsin_out.ppm";

    auto result = indsin::runColdPipeline(source);
    if (!result.ok) {
        std::cerr << "[Snag] line " << result.errorLine << ": " << result.errorMessage << "\n";
        return 1;
    }

    indsin::Indsin indsinEngine;
    indsinEngine.layout(result.fabric, {0, (double)rootWidth, 0, 1e9}, 0, 0);
    std::cout << "Strands measured: " << indsinEngine.stats.strandsMeasured
              << "  Tension cache hits: " << indsinEngine.stats.cacheHits << "\n";
    std::cout << "Fabric:\n";
    dumpFabric(result.fabric, 0);

    indsin::Dye dye;
    auto drawList = dye.paint(result.fabric);
    indsin::rasterizeToPPM(drawList, rootWidth, (int)result.fabric->geometry.h, outPath);
    std::cout << "\nWrote " << drawList.size() << " draw command(s) to " << outPath << "\n";
    return 0;
}

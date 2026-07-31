// loom/loomc.cpp — أداة سطر أوامر مستقلة لمحرّك العرض Loomtime (بنفس فلسفة rinc.cpp: هدف
// CMake مستقل تماماً عن مكتبة rinengine، لا يُحزَم داخل APK، مفيد فقط على جهاز تطوير عادي).
//
// الاستخدام:
//   ./loomc samples/loom_showcase.rin [rootWidth] [out.ppm]
//
// يطبع شجرة الـ Fabric بعد التخطيط (Loom) إلى stdout، ويكتب صورة PPM فعلية (يمكن تحويلها
// إلى PNG بأي أداة) تثبت أن خط الأنابيب الكامل (Lexer -> Parser -> Fabric -> Loom -> Dye)
// يعمل فعلياً على أي ملف .rin حقيقي، وليس فقط داخل اختبارات الوحدة.
#include "rin_loom_pipeline.h"
#include <iostream>
#include <fstream>
#include <sstream>

static void dumpFabric(const loom::StrandPtr& s, int depth) {
    std::cout << std::string(depth * 2, ' ') << loom::strandKindName(s->kind)
              << " '" << s->name << "' (line " << s->sourceLine << ")"
              << " geom=(" << s->geometry.x << "," << s->geometry.y << " "
              << s->geometry.w << "x" << s->geometry.h << ")\n";
    for (auto& c : s->children) dumpFabric(c, depth + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: loomc <file.rin> [rootWidth=390] [out.ppm]\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
    std::ostringstream ss; ss << in.rdbuf();
    std::string source = ss.str();

    int rootWidth = argc >= 3 ? std::atoi(argv[2]) : 390;
    std::string outPath = argc >= 4 ? argv[3] : "loom_out.ppm";

    auto result = loom::runColdPipeline(source);
    if (!result.ok) {
        std::cerr << "[Snag] line " << result.errorLine << ": " << result.errorMessage << "\n";
        return 1;
    }

    loom::Loom loomEngine;
    loomEngine.layout(result.fabric, {0, (double)rootWidth, 0, 1e9}, 0, 0);
    std::cout << "Strands measured: " << loomEngine.stats.strandsMeasured
              << "  Tension cache hits: " << loomEngine.stats.cacheHits << "\n";
    std::cout << "Fabric:\n";
    dumpFabric(result.fabric, 0);

    loom::Dye dye;
    auto drawList = dye.paint(result.fabric);
    loom::rasterizeToPPM(drawList, rootWidth, (int)result.fabric->geometry.h, outPath);
    std::cout << "\nWrote " << drawList.size() << " draw command(s) to " << outPath << "\n";
    return 0;
}

// pitok/tools/pitokc.cpp — أداة سطر أوامر مستقلة لـ PITOK (بنفس فلسفة loomc.cpp تمامًا: هدف
// CMake مستقل تمامًا عن مكتبة rinengine، لا يُحزَم داخل APK، مفيد على جهاز تطوير عادي).
//
// الفرق الوحيد عن loomc: هذه الأداة لا تستدعي rin::Lexer/rin::Parser/rin::Interpreter إطلاقًا.
// المسار الكامل هنا هو pitok::Lexer -> pitok::Parser -> pitok_bridge -> Loom -> Dye، بحيث
// تُثبت هذه الأداة نفسها (لا فقط الاختبارات اليدوية) أن ملف واجهة مكتوب بلهجة @view/@loop أو
// @container/@element يمكن أن يُعرَض بالكامل دون المرور بمُحلِّل Rin العام على الإطلاق.
//
// الاستخدام (مطابق لـ loomc):
//   ./pitokc samples/loom_showcase.rin [rootWidth] [out.ppm]
//
// ملاحظة نطاق مهمة: على عكس loomc (الذي يقبل أي ملف .rin كامل، بما فيه fun/print/إلخ عبر
// rin::Interpreter)، هذه الأداة تقبل فقط الملفات التي محتواها بالكامل عبارات واجهة PITOK
// (warp/@view/@loop/@element/@container) -- ملف يحوي كودًا عامًا مثل `fun x() {...}` سيفشل هنا
// برسالة lex/parse واضحة، وهذا متوقع تمامًا (انظر pitok/README.md لقرار النطاق).
#include "pitok_lexer.h"
#include "pitok_parser.h"
#include "loom/pitok_loom_bridge.h"
#include "loom/rin_loom_layout.h"
#include "loom/rin_loom_paint.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>

static void dumpFabric(const loom::StrandPtr& s, int depth) {
    std::cout << std::string(depth * 2, ' ') << loom::strandKindName(s->kind)
              << " '" << s->name << "' (line " << s->sourceLine << ")"
              << " geom=(" << s->geometry.x << "," << s->geometry.y << " "
              << s->geometry.w << "x" << s->geometry.h << ")\n";
    for (auto& c : s->children) dumpFabric(c, depth + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: pitokc <file.rin> [rootWidth=390] [out.ppm]\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
    std::ostringstream ss; ss << in.rdbuf();
    std::string source = ss.str();

    int rootWidth = argc >= 3 ? std::atoi(argv[2]) : 390;
    std::string outPath = argc >= 4 ? argv[3] : "pitok_out.ppm";

    // ---- Stage 1+2: PITOK lex + parse (zero rin::Lexer/Parser involvement) ----
    pitok::Program prog;
    try {
        pitok::Lexer lexer(source, argv[1]);
        auto tokens = lexer.scanTokens();
        pitok::Parser parser(tokens, argv[1]);
        prog = parser.parseProgram();
    } catch (const pitok::LexError& e) {
        std::cerr << "[pitok] lex error at " << argv[1] << ":" << e.line << ":" << e.column
                  << " -- " << e.what() << "\n";
        return 1;
    } catch (const pitok::ParseError& e) {
        std::cerr << "[pitok] parse error at " << argv[1] << ":" << e.line << ":" << e.column
                  << " -- " << e.what() << "\n";
        return 1;
    }

    // ---- Stage 3: bridge to loom::Strand + real Loomtime layout ----
    auto result = pitok_bridge::runPitokColdPipeline(prog);
    if (!result.ok) {
        std::cerr << "[pitok] bridge error: " << result.errorMessage << "\n";
        return 1;
    }

    loom::Loom loomEngine;
    loomEngine.layout(result.fabric, {0, (double)rootWidth, 0, 1e9}, 0, 0);
    std::cout << "Strands measured: " << loomEngine.stats.strandsMeasured
              << "  Tension cache hits: " << loomEngine.stats.cacheHits << "\n";
    std::cout << "Fabric (via PITOK, no rin::Parser involved):\n";
    dumpFabric(result.fabric, 0);

    // ---- Stage 4: real paint pass, same as loomc -- proves the whole pipeline end to end ----
    loom::Dye dye;
    auto drawList = dye.paint(result.fabric);
    loom::rasterizeToPPM(drawList, rootWidth, (int)result.fabric->geometry.h, outPath);
    std::cout << "\nWrote " << drawList.size() << " draw command(s) to " << outPath << "\n";
    return 0;
}

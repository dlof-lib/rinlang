// pitok/tools/pitok_indsin_e2e.cpp — end-to-end proof: reads a .rin UI file, runs it through
// pitok::Lexer + pitok::Parser (zero Rin parsing), then pitok_bridge::runPitokColdPipeline,
// then the REAL indsin::Indsin::layout() from rin_indsin_layout.h (zero changes to that file, beyond
// the pre-existing missing-#include fix), and prints computed geometry for every Strand.
//
// Build (from repo root):
//   g++ -std=c++17 -Ipitok/include -Iapp/src/main/cpp \
//       pitok/src/pitok_lexer.cpp pitok/src/pitok_parser.cpp \
//       pitok/tools/pitok_indsin_e2e.cpp -o /tmp/pitok_indsin_e2e
//   /tmp/pitok_indsin_e2e samples/indsin_showcase.rin

#include "pitok_lexer.h"
#include "pitok_parser.h"
#include "indsin/pitok_indsin_bridge.h"
#include "indsin/rin_indsin_layout.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <functional>

static void printTree(const indsin::StrandPtr& s, int depth) {
    std::cout << std::string(depth * 2, ' ')
              << indsin::strandKindName(s->kind) << " \"" << s->name << "\""
              << "  geometry=(" << s->geometry.x << "," << s->geometry.y
              << " " << s->geometry.w << "x" << s->geometry.h << ")\n";
    for (auto& c : s->children) printTree(c, depth + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: pitok_indsin_e2e <file.rin>\n"; return 1; }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }
    std::stringstream buf; buf << in.rdbuf();

    // ---- Stage 1: PITOK lex + parse (no Rin parser involved at all) ----
    pitok::Lexer lexer(buf.str(), argv[1]);
    auto tokens = lexer.scanTokens();
    pitok::Parser parser(tokens, argv[1]);
    pitok::Program prog = parser.parseProgram();
    std::cerr << "[stage1: pitok parse] ok -- " << tokens.size() << " tokens, "
              << prog.roots.size() << " root(s), " << prog.warps.size() << " warp(s)\n";

    // ---- Stage 2: bridge to indsin::Strand (the real Fabric type) ----
    auto result = pitok_bridge::runPitokColdPipeline(prog);
    if (!result.ok) {
        std::cerr << "[stage2: bridge] FAILED -- " << result.errorMessage << "\n";
        return 2;
    }
    std::cerr << "[stage2: bridge] ok -- fabric root kind=" << indsin::strandKindName(result.fabric->kind)
              << " children=" << result.fabric->children.size() << "\n";

    // ---- Stage 3: real Indsintime layout pass (rin_indsin_layout.h, unmodified) ----
    indsin::Indsin engine;
    indsin::Constraints c; c.maxW = 390; c.maxH = 844; // typical phone canvas, same default Indsintime uses
    engine.layout(result.fabric, c, 0, 0);
    std::cerr << "[stage3: layout] ok -- strandsMeasured=" << engine.stats.strandsMeasured << "\n\n";

    printTree(result.fabric, 0);
    return 0;
}

// pitok/tools/pitok_loom_e2e.cpp — end-to-end proof: reads a .rin UI file, runs it through
// pitok::Lexer + pitok::Parser (zero Rin parsing), then pitok_bridge::runPitokColdPipeline,
// then the REAL loom::Loom::layout() from rin_loom_layout.h (zero changes to that file, beyond
// the pre-existing missing-#include fix), and prints computed geometry for every Strand.
//
// Build (from repo root):
//   g++ -std=c++17 -Ipitok/include -Iapp/src/main/cpp \
//       pitok/src/pitok_lexer.cpp pitok/src/pitok_parser.cpp \
//       pitok/tools/pitok_loom_e2e.cpp -o /tmp/pitok_loom_e2e
//   /tmp/pitok_loom_e2e samples/loom_showcase.rin

#include "pitok_lexer.h"
#include "pitok_parser.h"
#include "loom/pitok_loom_bridge.h"
#include "loom/rin_loom_layout.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <functional>

static void printTree(const loom::StrandPtr& s, int depth) {
    std::cout << std::string(depth * 2, ' ')
              << loom::strandKindName(s->kind) << " \"" << s->name << "\""
              << "  geometry=(" << s->geometry.x << "," << s->geometry.y
              << " " << s->geometry.w << "x" << s->geometry.h << ")\n";
    for (auto& c : s->children) printTree(c, depth + 1);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: pitok_loom_e2e <file.rin>\n"; return 1; }
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

    // ---- Stage 2: bridge to loom::Strand (the real Fabric type) ----
    auto result = pitok_bridge::runPitokColdPipeline(prog);
    if (!result.ok) {
        std::cerr << "[stage2: bridge] FAILED -- " << result.errorMessage << "\n";
        return 2;
    }
    std::cerr << "[stage2: bridge] ok -- fabric root kind=" << loom::strandKindName(result.fabric->kind)
              << " children=" << result.fabric->children.size() << "\n";

    // ---- Stage 3: real Loomtime layout pass (rin_loom_layout.h, unmodified) ----
    loom::Loom engine;
    loom::Constraints c; c.maxW = 390; c.maxH = 844; // typical phone canvas, same default Loomtime uses
    engine.layout(result.fabric, c, 0, 0);
    std::cerr << "[stage3: layout] ok -- strandsMeasured=" << engine.stats.strandsMeasured << "\n\n";

    printTree(result.fabric, 0);
    return 0;
}

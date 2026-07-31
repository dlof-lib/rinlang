// rin_loom_run.cpp — Real Rin Live Preview runner.
//
// This is NOT a mock/simulator. It links directly against RIN's actual
// Lexer -> Parser -> Interpreter -> Loomtime pipeline (the same C++ engine
// the Android app calls over JNI in RinEngine.kt#renderView()) via the flat
// C ABI in loom/rin_loom_c_api.h. Every JSON "Fabric" this prints is produced
// by genuinely lexing, parsing, and (for taps) interpreting the .rin source —
// nothing here hand-writes or fakes the layout output.
//
// Usage:
//   rin_loom_run render <file.rin> [rootWidth]
//       One-shot: parse + layout, print the Fabric JSON, exit.
//
//   rin_loom_run session <file.rin> [rootWidth]
//       Interactive: keeps a live Loomtime session open (same session API
//       the app's Live Preview uses) and reads commands from stdin:
//         tap <x> <y>      - dispatch a real tap; if the target Strand has
//                            onTap=..., the actual interpreter runs it
//                            (a matching top-level `fun`, or a built-in Warp
//                            op) and the Fabric is genuinely re-laid-out.
//         edit <file.rin>  - re-read the file from disk and hot-diff it into
//                            the running session (Shuttle), keeping Warp state.
//         print            - print the current Fabric JSON again.
//         quit
//
// Build (see README.md next to this file for the full command):
//   g++ -std=c++17 -O2 -o rin_loom_run rin_loom_run.cpp loom/rin_loom_c_api.cpp \
//       rin_lexer.cpp rin_parser.cpp rin_interpreter.cpp rin_c_api.cpp -I .

#include "loom/rin_loom_c_api.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void printJson(char* json, const char* label) {
    if (!json) { fprintf(stderr, "%s: (null result)\n", label); return; }
    printf("%s\n", json);
    fflush(stdout);
}

int cmdRender(const std::string& path, int width) {
    std::string src;
    if (!readFile(path, src)) {
        fprintf(stderr, "error: cannot read %s\n", path.c_str());
        return 1;
    }
    char* json = rin_loom_render_json(src.c_str(), width);
    printJson(json, "render");
    rin_free_string(json);
    return 0;
}

int cmdSession(const std::string& path, int width) {
    std::string src;
    if (!readFile(path, src)) {
        fprintf(stderr, "error: cannot read %s\n", path.c_str());
        return 1;
    }

    void* session = rin_loom_session_create(src.c_str(), width);
    if (!session) {
        fprintf(stderr, "error: could not create Loomtime session\n");
        return 1;
    }

    char* initial = rin_loom_session_render_json(session);
    printJson(initial, "session:init");
    rin_free_string(initial);

    fprintf(stderr,
        "# Live session running against the REAL Rin interpreter.\n"
        "# Commands: tap <x> <y> | edit <file.rin> | print | quit\n");

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") {
            break;
        } else if (cmd == "print") {
            char* j = rin_loom_session_render_json(session);
            printJson(j, "session:print");
            rin_free_string(j);
        } else if (cmd == "tap") {
            double x = 0, y = 0;
            iss >> x >> y;
            char* j = rin_loom_session_tap(session, x, y);
            printJson(j, "session:tap");
            rin_free_string(j);
        } else if (cmd == "edit") {
            std::string newPath;
            iss >> newPath;
            std::string newSrc;
            if (!readFile(newPath.empty() ? path : newPath, newSrc)) {
                fprintf(stderr, "error: cannot read %s\n", (newPath.empty() ? path : newPath).c_str());
                continue;
            }
            char* j = rin_loom_session_update_source(session, newSrc.c_str());
            printJson(j, "session:edit");
            rin_free_string(j);
        } else if (!cmd.empty()) {
            fprintf(stderr, "unknown command: %s\n", cmd.c_str());
        }
    }

    rin_loom_session_free(session);
    return 0;
}

void usage(const char* prog) {
    fprintf(stderr,
        "Rin Live Preview — real engine CLI\n"
        "usage:\n"
        "  %s render  <file.rin> [rootWidth=390]\n"
        "  %s session <file.rin> [rootWidth=390]\n",
        prog, prog);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { usage(argv[0]); return 1; }
    std::string mode = argv[1];
    std::string path = argv[2];
    int width = argc > 3 ? std::atoi(argv[3]) : 390;

    if (mode == "render") return cmdRender(path, width);
    if (mode == "session") return cmdSession(path, width);

    usage(argv[0]);
    return 1;
}

// ============================================================================
//  rinvm — آلة Rin الافتراضية الكاملة (ملف تنفيذي واحد)
// ============================================================================
//  المحرّك الكامل (rin_interpreter + toolchain/BytecodeVM) + مكتبة rin_vm في ملف واحد صغير بلا
//  طرفية ولا RinPM. يشغّل صور RVM1 ويحزمها:
//     rinvm prog.rvm [args…]            تنفيذ صورة
//     rinvm prog.rin [args…]            يحزم في الذاكرة (يضمّ @import) ثم ينفّذ فوراً
//     rinvm pack prog.rin [-o out.rvm]  حزم
//     rinvm info prog.rvm               محتوى الصورة
//     rinvm --version | --help
//  وهو أيضاً runtime لـ `rin_ava -fvm`: التنفيذي الناتج = rinvm + صورة مُلحَقة (حمولة kind='M').
// ============================================================================
#include "rin_vm.h"
#include "rin_ava_payload.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "rin_version.h"
#include "diagnostics/diagnostic_renderer.h"
#include "diagnostics/source_manager.h"
#include "toolchain/rin_toolchain.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

bool runSource(const std::string& source, const std::string& name, rin::Interpreter& interp) {
    try {
        rin::Lexer lexer(source, name);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, name);
        auto statements = parser.parse();
        interp.setSourceFile(name);
        std::cout << interp.run(statements);
        return !interp.hadError();
    } catch (rin::RinError& e) {
        if (e.diagnostic) std::cerr << "\n" << rin::diag::renderPlain(*e.diagnostic, rin::diag::globalSourceManager());
        else std::cerr << "\n[rinvm] error in " << name << " at line " << e.line << ": " << e.message << "\n";
        return false;
    } catch (std::exception& e) {
        std::cerr << "\n[rinvm] internal error: " << e.what() << "\n";
        return false;
    }
}

int runImage(const rin_vm::Image& im, const std::vector<std::string>& args) {
    if (!im.bytecode.empty()) {
        rin::toolchain::Chunk chunk;
        if (!rin::toolchain::readBytecodeFromMemory(chunk, im.bytecode)) { std::cerr << "[rinvm] corrupt bytecode\n"; return 1; }
        rin::toolchain::BytecodeVM vm;
        auto r = vm.run(chunk);
        std::cout << r.output;
        if (!r.ok) { std::cerr << r.diagnostics.renderText(); return 1; }
        return 0;
    }
    rin::Interpreter interp;
    interp.setProgramArgs(args);
    interp.setVirtualFiles(std::unordered_map<std::string, std::string>(im.files.begin(), im.files.end()));
    return runSource(im.main, im.name, interp) ? 0 : 1;
}

int usage() {
    std::cerr << "rinvm " << RIN_VERSION_STRING << " — Rin virtual machine\n"
                 "  rinvm prog.rvm [args…]            run an image\n"
                 "  rinvm prog.rin [args…]            pack in memory (bundles @import) and run\n"
                 "  rinvm pack prog.rin [-o out.rvm]  create an image\n"
                 "  rinvm info prog.rvm               show an image\n";
    return 2;
}

std::string leaf(const std::string& p) { auto s = p.find_last_of('/'); return s == std::string::npos ? p : p.substr(s + 1); }

}  // namespace

int main(int argc, char** argv) {
    {   // تنفيذي مبني بـ rin_ava: صورة (أو مصدر) مُلحَقة في نهايته؛ كل الوسائط للبرنامج
        rin_ava::Payload pl;
        if (rin_ava::readSelfPayloadEx(argc > 0 ? argv[0] : nullptr, pl)) {
            std::vector<std::string> a(argv + 1, argv + argc);
            if (pl.kind == rin_ava::kKindImage) {
                rin_vm::Image im; std::string err;
                if (!rin_vm::deserialize(pl.data, im, &err)) { std::cerr << "[rinvm] invalid image: " << err << "\n"; return 1; }
                return runImage(im, a);
            }
            if (pl.kind == rin_ava::kKindSource) {
                rin_vm::Image im = rin_vm::build(pl.name, pl.name, pl.data);
                return runImage(im, a);
            }
            std::cerr << "[rinvm] unsupported payload kind\n";
            return 1;
        }
    }
    if (argc < 2) return usage();
    std::string cmd = argv[1];
    if (cmd == "--version" || cmd == "-v") { std::cout << "rinvm " << RIN_VERSION_STRING << "\n"; return 0; }
    if (cmd == "--help" || cmd == "-h") { usage(); return 0; }

    if (cmd == "pack" || cmd == "info") {
        if (argc < 3) return usage();
        std::string in = argv[2], out, data;
        for (int i = 3; i + 1 < argc; ++i) if (std::string(argv[i]) == "-o") out = argv[++i];
        if (!rin_vm::readFile(in, data)) { std::cerr << "rinvm: cannot read " << in << "\n"; return 1; }
        if (cmd == "pack") {
            auto im = rin_vm::build(leaf(in), in, data);
            if (out.empty()) { out = leaf(in); auto d = out.find_last_of('.'); if (d != std::string::npos) out.resize(d); out += ".rvm"; }
            if (!rin_vm::writeFile(out, rin_vm::serialize(im))) { std::cerr << "rinvm: cannot write " << out << "\n"; return 1; }
            std::cout << rin_vm::describe(im) << "wrote " << out << "\n";
            return 0;
        }
        rin_vm::Image im; std::string err;
        if (!rin_vm::deserialize(data, im, &err)) { std::cerr << "rinvm: " << in << ": " << err << "\n"; return 1; }
        std::cout << rin_vm::describe(im);
        return 0;
    }

    std::string data;
    if (!rin_vm::readFile(cmd, data)) { std::cerr << "rinvm: cannot read " << cmd << "\n"; return 2; }
    std::vector<std::string> args(argv + 2, argv + argc);
    rin_vm::Image im; std::string err;
    if (data.compare(0, 4, rin_vm::kMagic) == 0) {
        if (!rin_vm::deserialize(data, im, &err)) { std::cerr << "rinvm: " << cmd << ": " << err << "\n"; return 1; }
    } else {
        im = rin_vm::build(leaf(cmd), cmd, data);   // مصدر Rin: حزم فوري ثم تنفيذ
    }
    return runImage(im, args);
}

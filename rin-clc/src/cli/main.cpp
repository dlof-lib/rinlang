// cli/main.cpp — أداة سطر الأوامر "clc" الخاصة بصيغة CLC 1.0.0 (Rin Compact Library Container).
#include "clc_container.h"
#include "clc_format.h"
#include "clc_zip_import.h"
#include "sha256.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>

namespace fs = std::filesystem;
using namespace clc;

namespace {

constexpr const char* CLC_TOOL_VERSION = "1.0.0";

std::string humanSize(uint64_t bytes) {
    const char* units[] = {"B","KB","MB","GB","TB"};
    double v = double(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(u == 0 ? 0 : 2) << v << " " << units[u];
    return ss.str();
}

Level parseLevel(const std::string& s) {
    if (s == "0") return Level::L0;
    if (s == "1") return Level::L1;
    if (s == "2") return Level::L2;
    if (s == "3") return Level::L3;
    if (s == "4") return Level::L4;
    if (s == "ultra") return Level::ULTRA;
    throw std::runtime_error("invalid --level value: " + s + " (use 0,1,2,3,4,ultra)");
}

std::string levelName(uint8_t lvl) {
    switch (Level(lvl)) {
        case Level::L0: return "0 (store)";
        case Level::L1: return "1 (fast)";
        case Level::L2: return "2 (balanced)";
        case Level::L3: return "3 (strong)";
        case Level::L4: return "4 (max)";
        case Level::ULTRA: return "ultra";
    }
    return "?";
}

std::string opt(const std::vector<std::string>& args, const std::string& flag, const std::string& def = "") {
    for (size_t i = 0; i < args.size(); ++i)
        if (args[i] == flag && i + 1 < args.size()) return args[i+1];
    return def;
}
bool hasFlag(const std::vector<std::string>& args, const std::string& flag) {
    for (auto& a : args) if (a == flag) return true;
    return false;
}
std::vector<std::string> positional(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (!args[i].empty() && args[i][0] == '-') {
            // بعض الأعلام تأخذ قيمة بعدها (تخطَّها)
            static const std::vector<std::string> withValue = {"-o","--level","--author","--license",
                "--name","--version","--desc","--description","--entry","--rin-version","--dep"};
            for (auto& wv : withValue) if (args[i] == wv) { ++i; break; }
            continue;
        }
        out.push_back(args[i]);
    }
    return out;
}

int cmdPack(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.empty()) { std::cerr << "clc pack: missing <project_dir>\n"; return 2; }
    std::string src = pos[0];
    std::string out = opt(args, "-o");
    if (out.empty()) out = fs::path(src).filename().string() + ".rcl";

    PackOptions po;
    po.level = parseLevel(opt(args, "--level", "2"));
    po.metadata.name = opt(args, "--name", fs::path(src).filename().string());
    po.metadata.version = opt(args, "--version", "0.1.0");
    po.metadata.author = opt(args, "--author");
    po.metadata.description = opt(args, "--description", opt(args, "--desc"));
    po.metadata.license = opt(args, "--license");
    po.metadata.rinVersion = opt(args, "--rin-version");
    po.metadata.entryPoint = opt(args, "--entry");
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--dep" && i + 1 < args.size()) {
            std::string spec = args[++i]; // name>=1.2.0 أو name^2.0.0 أو name:constraint
            size_t p = spec.find_first_of(":");
            DependencyEntry d;
            if (p != std::string::npos) { d.name = spec.substr(0,p); d.constraint = spec.substr(p+1); }
            else { d.name = spec; d.constraint = "*"; }
            po.dependencies.push_back(d);
        }
    }

    std::cout << "CLC " << CLC_TOOL_VERSION << "\n\n";
    std::cout << "Scanning project...\n";
    // إحصاء سريع قبل البدء (تجربة استخدام ودّية كما طُلب صراحة)
    size_t fcount = 0, rcount = 0;
    for (auto& e : fs::recursive_directory_iterator(src)) {
        if (e.is_regular_file()) { fcount++; if (e.path().extension() == ".rin") rcount++; }
    }
    std::cout << "Found " << fcount << " files\n";
    std::cout << "Found " << rcount << " Rin files\n";
    std::cout << "Building dictionary...\n";
    std::cout << "Deduplicating blocks...\n";
    std::cout << "Compressing (level " << levelName(uint8_t(po.level)) << ")...\n";
    std::cout << "Writing container...\n\n";

    try {
        PackStats st = packDirectory(src, out, po);
        std::cout << "Done.\n\n";
        std::cout << "Original:   " << humanSize(st.originalSize) << "\n";
        std::cout << "CLC:        " << humanSize(st.compressedSize) << "\n";
        std::cout << "Saved:      " << std::fixed << std::setprecision(1) << st.ratioPercent() << "%\n";
        std::cout << "Files:      " << st.fileCount << " (" << st.dedupedFiles << " deduplicated)\n";
        std::cout << "Blocks:     " << st.blockCount << "\n";
        std::cout << "Pack time:  " << std::fixed << std::setprecision(3) << st.packSeconds << " sec\n";
        std::cout << "Output:     " << out << "\n";
    } catch (const std::exception& e) {
        std::cerr << "clc pack: error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdUnpack(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.empty()) { std::cerr << "clc unpack: missing <file.rcl>\n"; return 2; }
    std::string in = pos[0];
    std::string out = opt(args, "-o", "./output");
    bool quiet = hasFlag(args, "-q") || hasFlag(args, "--quiet");
    try {
        auto st = unpackContainer(in, out, quiet);
        std::cout << "\nExtracted " << st.fileCount << " file(s), " << humanSize(st.totalBytes)
                  << " in " << std::fixed << std::setprecision(3) << st.unpackSeconds << " sec -> " << out << "\n";
    } catch (const std::exception& e) {
        std::cerr << "clc unpack: error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdExtract(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.size() < 2) { std::cerr << "clc extract: usage: clc extract <file.rcl> <entry_path> [-o dir]\n"; return 2; }
    std::string out = opt(args, "-o", "./output");
    try {
        extractOneFile(pos[0], pos[1], out);
        std::cout << "Extracted '" << pos[1] << "' -> " << out << "\n";
    } catch (const std::exception& e) {
        std::cerr << "clc extract: error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdList(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.empty()) { std::cerr << "clc list: missing <file.rcl>\n"; return 2; }
    try {
        auto info = readContainerInfo(pos[0]);
        std::cout << std::left << std::setw(48) << "Path" << std::right << std::setw(12) << "Size"
                  << std::setw(14) << "Compressed" << std::setw(10) << "Ratio" << "\n";
        std::cout << std::string(84, '-') << "\n";
        for (auto& f : info.files) {
            if (f.flags & FILE_FLAG_IS_DIR) { std::cout << std::left << std::setw(48) << (f.path + "/") << "  <dir>\n"; continue; }
            uint64_t comp = 0;
            for (uint32_t i = 0; i < f.blockCount; ++i) comp += info.blocks.at(f.firstBlock+i).compressedSize;
            double ratio = f.originalSize ? 100.0*(1.0 - double(comp)/double(f.originalSize)) : 0.0;
            std::cout << std::left << std::setw(48) << f.path << std::right << std::setw(12) << f.originalSize
                      << std::setw(14) << comp << std::setw(9) << std::fixed << std::setprecision(1) << ratio << "%\n";
        }
        std::cout << std::string(84, '-') << "\n";
        std::cout << info.files.size() << " entries, " << info.blocks.size() << " blocks\n";
    } catch (const std::exception& e) {
        std::cerr << "clc list: error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdInfo(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.empty()) { std::cerr << "clc info: missing <file.rcl>\n"; return 2; }
    try {
        auto info = readContainerInfo(pos[0]);
        uint64_t totalOrig = 0, totalComp = 0;
        for (auto& f : info.files) totalOrig += f.originalSize;
        for (auto& b : info.blocks) totalComp += b.compressedSize;
        std::cout << "CLC Container Info\n";
        std::cout << "===================\n";
        std::cout << "Format version:   " << int(info.header.versionMajor) << "." << int(info.header.versionMinor) << "\n";
        std::cout << "Compression:      " << levelName(info.header.level) << "\n";
        std::cout << "Name:             " << info.metadata.name << "\n";
        std::cout << "Version:          " << info.metadata.version << "\n";
        if (!info.metadata.author.empty())      std::cout << "Author:           " << info.metadata.author << "\n";
        if (!info.metadata.description.empty()) std::cout << "Description:      " << info.metadata.description << "\n";
        if (!info.metadata.license.empty())     std::cout << "License:          " << info.metadata.license << "\n";
        if (!info.metadata.rinVersion.empty())  std::cout << "Rin version:      " << info.metadata.rinVersion << "\n";
        if (!info.metadata.entryPoint.empty())  std::cout << "Entry point:      " << info.metadata.entryPoint << "\n";
        std::cout << "Created:          " << info.metadata.created << "\n";
        std::cout << "Modified:         " << info.metadata.modified << "\n";
        if (!info.dependencies.empty()) {
            std::cout << "Dependencies:\n";
            for (auto& d : info.dependencies) std::cout << "  " << d.name << " " << d.constraint << "\n";
        }
        std::cout << "Files:            " << info.header.fileCount << "\n";
        std::cout << "Blocks:           " << info.header.blockCount << "\n";
        std::cout << "Original size:    " << humanSize(totalOrig) << "\n";
        std::cout << "Container size:   " << humanSize(info.containerFileSize) << "\n";
        double ratio = totalOrig ? 100.0*(1.0 - double(info.containerFileSize)/double(totalOrig)) : 0.0;
        std::cout << "Overall ratio:    " << std::fixed << std::setprecision(1) << ratio << "%\n";
    } catch (const std::exception& e) {
        std::cerr << "clc info: error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int cmdCheck(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.empty()) { std::cerr << "clc check: missing <file.rcl>\n"; return 2; }
    auto report = checkContainer(pos[0]);
    std::cout << "CLC Structural Check\n\n";
    for (auto& s : report.sections)
        std::cout << std::left << std::setw(14) << s.name << (s.ok ? "OK" : ("FAIL: " + s.detail)) << "\n";
    for (auto& msg : report.corruptBlocks) std::cout << "\nERROR:\n" << msg << "\n";
    std::cout << "\nResult: " << (report.allOk() ? "VALID" : "CORRUPTED") << "\n";
    return report.allOk() ? 0 : 1;
}

int cmdVerify(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.empty()) { std::cerr << "clc verify: missing <file.rcl>\n"; return 2; }
    auto report = verifyContainer(pos[0]);
    std::cout << "CLC Integrity Check\n\n";
    for (auto& s : report.sections)
        std::cout << std::left << std::setw(24) << s.name << (s.ok ? "OK" : ("FAIL: " + s.detail)) << "\n";
    for (auto& msg : report.corruptBlocks) std::cout << "\nERROR:\n" << msg << "\n";
    std::cout << "\nIntegrity: " << (report.allOk() ? "VALID" : "INVALID") << "\n";
    return report.allOk() ? 0 : 1;
}

void printHelp() {
    std::cout <<
R"(clc — CLC 1.0.0 (Rin Compact Library Container) command-line tool

Usage:
  clc pack <dir> -o out.rcl [--level 0..4|ultra] [--name N] [--version V]
                            [--author A] [--description D] [--license L]
                            [--rin-version R] [--entry FILE] [--dep name:constraint]...
  clc unpack <file.rcl> -o <dir> [-q]
  clc extract <file.rcl> <entry_path> -o <dir>
  clc list <file.rcl>
  clc info <file.rcl>
  clc check <file.rcl>
  clc verify <file.rcl>
  clc convert <in.zip> <out.rcl>
  clc test [--tmp DIR]
  clc --version | --help
)";
}

int cmdConvertZip(const std::vector<std::string>& args) {
    auto pos = positional(args);
    if (pos.size() < 2) { std::cerr << "clc convert: usage: clc convert <in.zip> <out.rcl>\n"; return 2; }
    fs::path tmp = fs::temp_directory_path() / fs::path("clc_convert_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(tmp);
        extractZipToDirectory(pos[0], tmp.string());
        PackOptions po;
        po.metadata.name = fs::path(pos[0]).stem().string();
        auto st = packDirectory(tmp.string(), pos[1], po);
        std::cout << "Converted " << pos[0] << " -> " << pos[1]
                  << "  (" << humanSize(st.originalSize) << " -> " << humanSize(st.compressedSize) << ")\n";
        fs::remove_all(tmp);
    } catch (const std::exception& e) {
        fs::remove_all(tmp);
        std::cerr << "clc convert: error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

} // namespace

int cmdTest(const std::vector<std::string>& args); // مُعرَّفة في cli/self_test.cpp

int main(int argc, char** argv) {
    std::vector<std::string> all(argv + 1, argv + argc);
    if (all.empty() || all[0] == "--help" || all[0] == "-h") { printHelp(); return all.empty() ? 2 : 0; }
    if (all[0] == "--version" || all[0] == "-v") { std::cout << "clc " << CLC_TOOL_VERSION << "\n"; return 0; }

    std::string cmd = all[0];
    std::vector<std::string> rest(all.begin()+1, all.end());

    if (cmd == "pack") return cmdPack(rest);
    if (cmd == "unpack") return cmdUnpack(rest);
    if (cmd == "extract") return cmdExtract(rest);
    if (cmd == "list") return cmdList(rest);
    if (cmd == "info") return cmdInfo(rest);
    if (cmd == "check") return cmdCheck(rest);
    if (cmd == "verify") return cmdVerify(rest);
    if (cmd == "convert") return cmdConvertZip(rest);
    if (cmd == "test") return cmdTest(rest);

    std::cerr << "clc: unknown command '" << cmd << "'\n";
    printHelp();
    return 2;
}

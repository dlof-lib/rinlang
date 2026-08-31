// cli/self_test.cpp — اختبارات ذاتية حقيقية يشغّلها `clc test` (القسم 17 من المتطلبات).
// كل اختبار: pack -> unpack -> compare ORIGINAL == RESTORED byte-for-byte، بالإضافة
// لاختبارات الأمان (path traversal) واكتشاف التلف (corruption detection).
#include "clc_container.h"
#include "clc_security.h"
#include "clc_io.h"
#include "sha256.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;
using namespace clc;

namespace {

int passed = 0, failed = 0;

void check(const std::string& name, bool ok, const std::string& detail = "") {
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << name;
    if (!ok && !detail.empty()) std::cout << " — " << detail;
    std::cout << "\n";
    if (ok) ++passed; else ++failed;
}

void writeFile(const fs::path& p, const std::vector<uint8_t>& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!content.empty()) f.write(reinterpret_cast<const char*>(content.data()), std::streamsize(content.size()));
}
void writeText(const fs::path& p, const std::string& s) {
    std::vector<uint8_t> v(s.begin(), s.end());
    writeFile(p, v);
}

bool dirsByteIdentical(const fs::path& a, const fs::path& b, std::string& detail) {
    std::vector<fs::path> aFiles, bFiles;
    for (auto& e : fs::recursive_directory_iterator(a)) if (e.is_regular_file()) aFiles.push_back(fs::relative(e.path(), a));
    for (auto& e : fs::recursive_directory_iterator(b)) if (e.is_regular_file()) bFiles.push_back(fs::relative(e.path(), b));
    std::sort(aFiles.begin(), aFiles.end());
    std::sort(bFiles.begin(), bFiles.end());
    if (aFiles != bFiles) { detail = "file list differs (" + std::to_string(aFiles.size()) + " vs " + std::to_string(bFiles.size()) + ")"; return false; }
    for (auto& rel : aFiles) {
        std::ifstream fa(a/rel, std::ios::binary), fb(b/rel, std::ios::binary);
        std::vector<uint8_t> ca((std::istreambuf_iterator<char>(fa)), std::istreambuf_iterator<char>());
        std::vector<uint8_t> cb((std::istreambuf_iterator<char>(fb)), std::istreambuf_iterator<char>());
        if (ca != cb) { detail = "content differs: " + rel.generic_string(); return false; }
    }
    return true;
}

bool roundtrip(const fs::path& base, const std::string& caseName, std::string& detail, Level level = Level::L2) {
    fs::path srcDir = base / (caseName + "_src");
    fs::path rcl = base / (caseName + ".rcl");
    fs::path outDir = base / (caseName + "_out");
    fs::remove_all(outDir);
    PackOptions po; po.level = level; po.metadata.name = caseName;
    try {
        packDirectory(srcDir.string(), rcl.string(), po);
        unpackContainer(rcl.string(), outDir.string(), true);
    } catch (const std::exception& e) { detail = e.what(); return false; }
    return dirsByteIdentical(srcDir, outDir, detail);
}

} // namespace

int cmdTest(const std::vector<std::string>&) {
    fs::path base = fs::temp_directory_path() / fs::path("clc_selftest_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(base);
    std::cout << "CLC Self-Test Suite\n====================\n\n";

    // 1) Empty project
    fs::create_directories(base / "empty_src");
    { std::string d; check("Empty project (pack/unpack roundtrip)", roundtrip(base, "empty", d), d); }

    // 2) Single file
    writeText(base / "single_src" / "hello.rin", "print \"hello\";\n");
    { std::string d; check("Single file", roundtrip(base, "single", d), d); }

    // 3) Many small files
    for (int i = 0; i < 200; ++i) writeText(base / "many_src" / ("file" + std::to_string(i) + ".txt"), "line " + std::to_string(i) + "\n");
    { std::string d; check("Many files (200)", roundtrip(base, "many", d), d); }

    // 4) Large file (~6 MB pseudo-random, forces streaming + multi-block path)
    {
        std::vector<uint8_t> big(6 * 1024 * 1024);
        std::mt19937 rng(42);
        for (auto& b : big) b = uint8_t(rng() & 0xff);
        writeFile(base / "large_src" / "blob.bin", big);
        std::string d; check("Large binary file (6 MB, streamed)", roundtrip(base, "large", d));
    }

    // 5) Unicode / Arabic content
    writeText(base / "unicode_src" / "arabic.txt", "مرحباً بالعالم — هذا نص عربي لاختبار CLC، بما في ذلك رموز خاصة: ١٢٣ ✓ 🎉");
    writeText(base / "unicode_src" / "mixed.rin", "// تعليق عربي\nfun container_open() { return \"مكتبة\"; }\n");
    { std::string d; check("Unicode / Arabic content", roundtrip(base, "unicode", d), d); }

    // 6) Binary file (PNG-like header bytes, not actually valid PNG but binary-shaped)
    {
        std::vector<uint8_t> png = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A,0,0,0,0,1,2,3,4,5,0,0,0};
        writeFile(base / "binary_src" / "img.png", png);
        std::string d; check("Binary file (non-text)", roundtrip(base, "binary", d), d);
    }

    // 7) Duplicate files (deduplication correctness)
    {
        std::string content = "container.open\nobject\nreturn\ncontainer.open\nobject\nreturn\n";
        writeText(base / "dup_src" / "a.rin", content);
        writeText(base / "dup_src" / "backup" / "a.rin", content);
        writeText(base / "dup_src" / "copy" / "a.rin", content);
        std::string d;
        bool ok = roundtrip(base, "dup", d);
        if (ok) {
            auto info = readContainerInfo((base / "dup.rcl").string());
            // 3 ملفات متطابقة يجب أن تُشير لنفس نطاق الكتل (dedup فعلي، لا 3 نسخ).
            uint32_t fb = UINT32_MAX, bc = UINT32_MAX; bool sameBlocks = true;
            for (auto& f : info.files) {
                if (f.flags & FILE_FLAG_IS_DIR) continue;
                if (fb == UINT32_MAX) { fb = f.firstBlock; bc = f.blockCount; }
                else if (f.firstBlock != fb || f.blockCount != bc) sameBlocks = false;
            }
            check("Duplicate files (content dedup verified)", sameBlocks, sameBlocks ? "" : "duplicate files stored as separate blocks");
        } else check("Duplicate files (content dedup verified)", false, d);
    }

    // 8) Nested directories (incl. empty subdirectory)
    writeText(base / "nested_src" / "a" / "b" / "c" / "deep.rin", "let x = 1;\n");
    fs::create_directories(base / "nested_src" / "a" / "empty_dir");
    { std::string d; check("Nested directories + empty subdirectory", roundtrip(base, "nested", d), d); }

    // 9) A small realistic "Rin project" (mirrors dedup + rin optimization together)
    {
        fs::path p = base / "rinproj_src";
        writeText(p / "src" / "main.rin", "@import \"math\";\nfun main() { let r = container.open(\"lib\"); return object; }\n");
        writeText(p / "src" / "util.rin", "fun helper() { return object; }\nfun helper2() { return object; }\n");
        writeText(p / "README.md", "# Demo\nRin project.\n");
        std::string d; check("Realistic Rin project (mixed files)", roundtrip(base, "rinproj", d), d);
    }

    // 10) ULTRA level roundtrip
    writeText(base / "ultra_src" / "code.rin", "fun a(){return object;}\nfun b(){return object;}\nfun c(){return object;}\n");
    { std::string d; check("--ultra level roundtrip", roundtrip(base, "ultra", d, Level::ULTRA), d); }

    // 11) Invalid header rejected
    {
        fs::path bad = base / "bad_header.rcl";
        std::ofstream f(bad, std::ios::binary); f << "NOT_A_CLC_FILE_AT_ALL_1234567890"; f.close();
        bool threw = false;
        try { readContainerInfo(bad.string()); } catch (const ClcFormatError&) { threw = true; }
        check("Invalid header rejected cleanly", threw);
    }

    // 12) Path traversal rejected
    {
        bool threw1 = false, threw2 = false;
        try { sanitizeEntryPath("../../etc/passwd"); } catch (const ClcFormatError&) { threw1 = true; }
        try { sanitizeEntryPath("/etc/passwd"); } catch (const ClcFormatError&) { threw2 = true; }
        check("Path traversal ('..') rejected", threw1);
        check("Absolute path rejected", threw2);
    }

    // 13) Corruption detection (flip a byte inside a data block, expect `check` to catch it)
    {
        fs::path srcDir = base / "corrupt_src";
        writeText(srcDir / "data.rin", std::string(2000, 'x') + "container.open\nobject\nreturn\n");
        fs::path rcl = base / "corrupt.rcl";
        PackOptions po; packDirectory(srcDir.string(), rcl.string(), po);
        auto info = readContainerInfo(rcl.string());
        // اقلب بايتاً واحداً منتصف أول كتلة بيانات فعلية (بعد الرأس مباشرة).
        {
            std::fstream f(rcl, std::ios::binary | std::ios::in | std::ios::out);
            int64_t at = int64_t(clc::CLC_HEADER_SIZE) + 5;
            char b;
            f.seekg(at); f.read(&b, 1);
            b = char(b ^ 0xFF);
            f.seekp(at); f.write(&b, 1);
        }
        auto report = checkContainer(rcl.string());
        check("Corrupted block detected by `clc check`", !report.allOk() && !report.corruptBlocks.empty());
    }

    std::cout << "\n" << passed << " passed, " << failed << " failed.\n";
    fs::remove_all(base);
    return failed == 0 ? 0 : 1;
}

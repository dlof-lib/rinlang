#include "clc_zip_import.h"
#include "clc_io.h"
#include "clc_compress.h"
#include "clc_security.h"
#include <fstream>
#include <vector>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace clc {

namespace {
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); }
}

void extractZipToDirectory(const std::string& zipPath, const std::string& destDir) {
    std::ifstream in(zipPath, std::ios::binary);
    if (!in) throw ClcFormatError("cannot open zip file: " + zipPath);
    in.seekg(0, std::ios::end);
    uint64_t fileSize = uint64_t(in.tellg());
    if (fileSize < 22) throw ClcFormatError("not a valid zip file (too small): " + zipPath);

    // ابحث عن EOCD بدءاً من النهاية (قد يوجد تعليق حتى 65535 بايت بعد السجل).
    uint64_t searchLen = std::min<uint64_t>(fileSize, 65557);
    std::vector<uint8_t> tail(searchLen);
    in.seekg(int64_t(fileSize - searchLen), std::ios::beg);
    in.read(reinterpret_cast<char*>(tail.data()), std::streamsize(searchLen));

    int64_t eocdPos = -1;
    for (int64_t i = int64_t(searchLen) - 22; i >= 0; --i) {
        if (rd32(&tail[i]) == 0x06054b50u) { eocdPos = i; break; }
    }
    if (eocdPos < 0) throw ClcFormatError("EOCD record not found — not a standard zip file: " + zipPath);

    uint16_t numEntries = rd16(&tail[eocdPos + 10]);
    uint32_t cdSize     = rd32(&tail[eocdPos + 12]);
    uint32_t cdOffset   = rd32(&tail[eocdPos + 16]);
    if (cdOffset > fileSize || uint64_t(cdOffset) + cdSize > fileSize)
        throw ClcFormatError("zip central directory offsets inconsistent (corrupted/zip64 unsupported): " + zipPath);

    std::vector<uint8_t> cd(cdSize);
    in.seekg(cdOffset, std::ios::beg);
    in.read(reinterpret_cast<char*>(cd.data()), cdSize);

    fs::create_directories(destDir);

    size_t pos = 0;
    for (uint16_t entryIdx = 0; entryIdx < numEntries; ++entryIdx) {
        if (pos + 46 > cd.size()) throw ClcFormatError("truncated zip central directory: " + zipPath);
        if (rd32(&cd[pos]) != 0x02014b50u) throw ClcFormatError("bad central directory entry signature: " + zipPath);
        uint16_t method = rd16(&cd[pos+10]);
        uint32_t compSize = rd32(&cd[pos+20]);
        uint32_t uncompSize = rd32(&cd[pos+24]);
        uint16_t nameLen = rd16(&cd[pos+28]);
        uint16_t extraLen = rd16(&cd[pos+30]);
        uint16_t commentLen = rd16(&cd[pos+32]);
        uint32_t localOffset = rd32(&cd[pos+42]);
        if (pos + 46 + nameLen > cd.size()) throw ClcFormatError("truncated zip filename field: " + zipPath);
        std::string name(reinterpret_cast<char*>(&cd[pos+46]), nameLen);
        pos += 46 + nameLen + extraLen + commentLen;

        bool isDir = !name.empty() && name.back() == '/';
        std::string safe;
        try { safe = sanitizeEntryPath(name); } catch (...) { continue; } // تجاهل عناصر غير آمنة بصمت (best-effort import)
        std::string outPath = resolveUnderRoot(destDir, safe);
        if (isDir) { fs::create_directories(outPath); continue; }

        if (localOffset > fileSize) throw ClcFormatError("zip local header offset out of range: " + name);
        std::vector<uint8_t> lh(30);
        in.seekg(localOffset, std::ios::beg);
        in.read(reinterpret_cast<char*>(lh.data()), 30);
        if (rd32(&lh[0]) != 0x04034b50u) throw ClcFormatError("bad local file header signature for: " + name);
        uint16_t lNameLen = rd16(&lh[26]);
        uint16_t lExtraLen = rd16(&lh[28]);
        uint64_t dataStart = uint64_t(localOffset) + 30 + lNameLen + lExtraLen;
        if (dataStart + compSize > fileSize) throw ClcFormatError("zip entry data out of range: " + name);

        std::vector<uint8_t> compData(compSize);
        in.seekg(int64_t(dataStart), std::ios::beg);
        if (compSize > 0) in.read(reinterpret_cast<char*>(compData.data()), compSize);

        std::vector<uint8_t> plain;
        if (method == 0) plain = std::move(compData);
        else if (method == 8) plain = inflateRaw(compData.data(), compData.size(), uncompSize);
        else throw ClcFormatError("unsupported zip compression method (" + std::to_string(method) +
                                   ") for '" + name + "' — only Store(0)/Deflate(8) supported");

        fs::create_directories(fs::path(outPath).parent_path());
        std::ofstream fout(outPath, std::ios::binary | std::ios::trunc);
        if (!plain.empty()) fout.write(reinterpret_cast<char*>(plain.data()), std::streamsize(plain.size()));
    }
}

} // namespace clc

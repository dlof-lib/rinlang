#include "clc_container.h"
#include "clc_io.h"
#include "clc_compress.h"
#include "clc_rin_opt.h"
#include "clc_security.h"
#include "sha256.h"

#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <unordered_map>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <cstring>
#include <iostream>

namespace fs = std::filesystem;

namespace clc {

namespace {

constexpr uint64_t CHUNK_SIZE_NORMAL = 1ull * 1024 * 1024; // 1 MiB
constexpr uint64_t CHUNK_SIZE_ULTRA  = 8ull * 1024 * 1024; // 8 MiB

std::string nowIso8601() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

void writeAndHash(std::ofstream& out, Sha256Stream& hasher, const uint8_t* data, size_t n) {
    out.write(reinterpret_cast<const char*>(data), std::streamsize(n));
    if (!out) throw ClcFormatError("write error while packing container (disk full / permissions?)");
    hasher.update(data, n);
}
void writeAndHash(std::ofstream& out, Sha256Stream& hasher, const std::vector<uint8_t>& v) {
    if (!v.empty()) writeAndHash(out, hasher, v.data(), v.size());
}

// عنصر واحد يمثّل ملفاً أو مجلداً فارغاً مُكتشَفاً أثناء مسح المشروع.
struct ScanEntry {
    std::string relPath; // بفواصل '/'
    bool isDir;
    uint64_t size;
};

std::vector<ScanEntry> scanProject(const std::string& srcDir) {
    fs::path root(srcDir);
    if (!fs::exists(root) || !fs::is_directory(root))
        throw ClcFormatError("source path is not a directory: " + srcDir);
    std::vector<ScanEntry> entries;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied);
         it != fs::recursive_directory_iterator(); ++it) {
        fs::path rel = fs::relative(it->path(), root);
        std::string relStr = rel.generic_string();
        if (relStr.empty()) continue;
        if (it->is_directory()) {
            bool empty = fs::is_empty(it->path());
            if (empty) entries.push_back({relStr, true, 0});
        } else if (it->is_regular_file()) {
            entries.push_back({relStr, false, uint64_t(fs::file_size(it->path()))});
        }
        // روابط رمزية وأنواع خاصة أخرى: تُتجاهَل بأمان في هذا الإصدار (موثّق في FORMAT.md).
    }
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b){ return a.relPath < b.relPath; });
    return entries;
}

std::vector<uint8_t> readWholeFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw ClcFormatError("cannot open file for reading: " + path);
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    size_t sz = size_t(n);
    std::vector<uint8_t> buf(sz);
    if (n > 0) f.read(reinterpret_cast<char*>(buf.data()), n);
    return buf;
}

// يضغط كتلة واحدة ويختار STORE إن لم يفدها DEFLATE فعلياً (لا وعود ضغط كاذبة).
BlockEntry compressChunkAndWrite(std::ofstream& out, Sha256Stream& hasher, uint64_t& dataCursor,
                                  uint32_t& nextBlockId, const uint8_t* chunk, size_t chunkLen, int zlibLevel) {
    BlockEntry be;
    be.id = nextBlockId++;
    be.originalSize = chunkLen;
    std::vector<uint8_t> compressed;
    if (zlibLevel > 0 && chunkLen > 0) compressed = deflateRaw(chunk, chunkLen, zlibLevel);
    bool useStore = zlibLevel == 0 || compressed.size() >= chunkLen;
    const uint8_t* storedPtr = useStore ? chunk : compressed.data();
    size_t storedLen = useStore ? chunkLen : compressed.size();
    be.method = uint8_t(useStore ? CompressionMethod::STORE : CompressionMethod::DEFLATE);
    be.compressedSize = storedLen;
    be.offset = dataCursor;
    be.crc32OfStored = crc32Of(storedPtr, storedLen);
    writeAndHash(out, hasher, storedPtr, storedLen);
    dataCursor += storedLen;
    return be;
}

std::vector<BlockEntry> chunkCompressAndWrite(std::ofstream& out, Sha256Stream& hasher, uint64_t& dataCursor,
                                               uint32_t& nextBlockId, const std::vector<uint8_t>& data,
                                               uint64_t chunkSize, int zlibLevel) {
    std::vector<BlockEntry> blocks;
    if (data.empty()) return blocks;
    size_t off = 0;
    while (off < data.size()) {
        size_t len = std::min<size_t>(chunkSize, data.size() - off);
        blocks.push_back(compressChunkAndWrite(out, hasher, dataCursor, nextBlockId, data.data()+off, len, zlibLevel));
        off += len;
    }
    return blocks;
}

// -- تسلسل/فك تسلسل الأقسام الوصفية (كلها عبر ByteWriter/ByteReader، مستقلة عن الـ struct padding) --

std::vector<uint8_t> serializeMetadata(const Metadata& m) {
    ByteWriter w;
    w.str32(m.name); w.str32(m.version); w.str32(m.author); w.str32(m.description);
    w.str32(m.license); w.str32(m.rinVersion); w.str32(m.entryPoint);
    w.str32(m.created); w.str32(m.modified);
    w.u32(uint32_t(m.extra.size()));
    for (auto& kv : m.extra) { w.str32(kv.first); w.str32(kv.second); }
    return w.buf;
}
Metadata parseMetadata(ByteReader& r) {
    Metadata m;
    m.name = r.str32(); m.version = r.str32(); m.author = r.str32(); m.description = r.str32();
    m.license = r.str32(); m.rinVersion = r.str32(); m.entryPoint = r.str32();
    m.created = r.str32(); m.modified = r.str32();
    uint32_t n = r.u32();
    if (n > DEFAULT_MAX_ENTRIES) throw ClcFormatError("metadata extra-field count implausible (corrupted?)");
    for (uint32_t i = 0; i < n; ++i) { std::string k = r.str32(); std::string v = r.str32(); m.extra[k] = v; }
    return m;
}

std::vector<uint8_t> serializeDependencies(const std::vector<DependencyEntry>& deps) {
    ByteWriter w;
    w.u32(uint32_t(deps.size()));
    for (auto& d : deps) { w.str32(d.name); w.str32(d.constraint); }
    return w.buf;
}
std::vector<DependencyEntry> parseDependencies(ByteReader& r) {
    uint32_t n = r.u32();
    if (n > DEFAULT_MAX_ENTRIES) throw ClcFormatError("dependency count implausible (corrupted?)");
    std::vector<DependencyEntry> out; out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) { DependencyEntry d; d.name = r.str32(); d.constraint = r.str32(); out.push_back(d); }
    return out;
}

std::vector<uint8_t> serializeSymbolTable(const std::vector<std::string>& dict) {
    ByteWriter w;
    w.u32(uint32_t(dict.size()));
    for (auto& s : dict) w.str32(s);
    return w.buf;
}
std::vector<std::string> parseSymbolTable(ByteReader& r) {
    uint32_t n = r.u32();
    if (n > RIN_OPT_MAX_DICT_SIZE) throw ClcFormatError("rin symbol table size implausible (corrupted?)");
    std::vector<std::string> out; out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) out.push_back(r.str32());
    return out;
}

std::vector<uint8_t> serializeFileIndex(const std::vector<FileEntry>& files) {
    ByteWriter w;
    w.u32(uint32_t(files.size()));
    for (auto& f : files) {
        w.str32(f.path); w.u64(f.originalSize); w.u8(f.flags);
        w.u32(f.firstBlock); w.u32(f.blockCount);
        w.raw(f.contentHash.data(), f.contentHash.size());
    }
    return w.buf;
}
std::vector<FileEntry> parseFileIndex(ByteReader& r, uint32_t declaredCount) {
    // تحقّق: العدد المُعلَن في الرأس يجب ألا يتجاوز ما يسمح به الحد الأقصى المعقول،
    // وألا يتجاوز ما يمكن أن يحتويه الجزء المتبقي فعلياً من الملف (حد أدنى ~16 بايت/سجل).
    if (declaredCount > DEFAULT_MAX_ENTRIES) throw ClcFormatError("file_count exceeds sane maximum (rejected)");
    uint32_t n = r.u32();
    if (n != declaredCount) throw ClcFormatError("file index count mismatch with header (corrupted)");
    std::vector<FileEntry> out; out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        FileEntry f;
        f.path = r.str32(4096);
        f.originalSize = r.u64();
        f.flags = r.u8();
        f.firstBlock = r.u32();
        f.blockCount = r.u32();
        auto h = r.raw(32);
        std::copy(h.begin(), h.end(), f.contentHash.begin());
        out.push_back(std::move(f));
    }
    return out;
}

std::vector<uint8_t> serializeBlockTable(const std::vector<BlockEntry>& blocks) {
    ByteWriter w;
    w.u32(uint32_t(blocks.size()));
    for (auto& b : blocks) {
        w.u32(b.id); w.u64(b.originalSize); w.u64(b.compressedSize); w.u64(b.offset);
        w.u8(b.method); w.u32(b.crc32OfStored);
    }
    return w.buf;
}
std::vector<BlockEntry> parseBlockTable(ByteReader& r, uint32_t declaredCount) {
    if (declaredCount > DEFAULT_MAX_ENTRIES) throw ClcFormatError("block_count exceeds sane maximum (rejected)");
    uint32_t n = r.u32();
    if (n != declaredCount) throw ClcFormatError("block table count mismatch with header (corrupted)");
    std::vector<BlockEntry> out; out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        BlockEntry b;
        b.id = r.u32(); b.originalSize = r.u64(); b.compressedSize = r.u64(); b.offset = r.u64();
        b.method = r.u8(); b.crc32OfStored = r.u32();
        out.push_back(b);
    }
    return out;
}

std::vector<uint8_t> serializeHeader(const ClcHeader& h) {
    ByteWriter w;
    w.raw(h.magic, 4);
    w.u8(h.versionMajor); w.u8(h.versionMinor);
    w.u16(h.flags);
    w.u8(h.compressionMethod); w.u8(h.level);
    w.u16(h.reserved);
    w.u32(h.fileCount); w.u32(h.blockCount);
    w.u64(h.metadataOffset); w.u64(h.dependencyOffset); w.u64(h.symbolTableOffset);
    w.u64(h.indexOffset); w.u64(h.blockTableOffset); w.u64(h.dataOffset);
    w.u64(h.integrityOffset); w.u64(h.footerOffset);
    w.u32(h.headerCrc32);
    w.u64(h.reserved2);
    if (w.buf.size() != CLC_HEADER_SIZE) throw ClcFormatError("internal error: header serialization size mismatch");
    return w.buf;
}

ClcHeader parseHeaderRaw(const uint8_t* bytes /* CLC_HEADER_SIZE */) {
    ByteReader r(bytes, CLC_HEADER_SIZE);
    ClcHeader h;
    auto m = r.raw(4);
    std::copy(m.begin(), m.end(), h.magic);
    h.versionMajor = r.u8(); h.versionMinor = r.u8();
    h.flags = r.u16();
    h.compressionMethod = r.u8(); h.level = r.u8();
    h.reserved = r.u16();
    h.fileCount = r.u32(); h.blockCount = r.u32();
    h.metadataOffset = r.u64(); h.dependencyOffset = r.u64(); h.symbolTableOffset = r.u64();
    h.indexOffset = r.u64(); h.blockTableOffset = r.u64(); h.dataOffset = r.u64();
    h.integrityOffset = r.u64(); h.footerOffset = r.u64();
    h.headerCrc32 = r.u32();
    h.reserved2 = r.u64();
    return h;
}

uint32_t headerCrc(const std::vector<uint8_t>& headerBytes) {
    // يغطّي بايتات 0..83 فقط (كل شيء قبل حقل الـ crc32 نفسه في offset=84)، كما هو موثّق.
    return crc32Of(headerBytes.data(), 84);
}

// يقرأ ويتحقق من الرأس + الـ footer الأساسيين، ويعيد (header, fileSize).
std::pair<ClcHeader,uint64_t> readAndValidateHeader(std::ifstream& in, const std::string& path) {
    in.seekg(0, std::ios::end);
    uint64_t fileSize = uint64_t(in.tellg());
    if (fileSize < CLC_HEADER_SIZE + CLC_FOOTER_SIZE)
        throw ClcFormatError("file too small to be a valid .rcl container: " + path);
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> hbuf(CLC_HEADER_SIZE);
    in.read(reinterpret_cast<char*>(hbuf.data()), CLC_HEADER_SIZE);
    ClcHeader h = parseHeaderRaw(hbuf.data());
    if (std::memcmp(h.magic, CLC_MAGIC, 4) != 0)
        throw ClcFormatError("not a CLC container (bad magic number): " + path);
    if (h.versionMajor != CLC_VERSION_MAJOR)
        throw ClcFormatError("unsupported CLC major version " + std::to_string(h.versionMajor) +
                              " (this tool supports " + std::to_string(CLC_VERSION_MAJOR) + ".x)");
    uint32_t expectedCrc = headerCrc(hbuf);
    if (expectedCrc != h.headerCrc32)
        throw ClcFormatError("header checksum mismatch — container header is corrupted: " + path);
    // اتساق الإزاحات مع حجم الملف الفعلي (يمنع Malformed Header / قراءة خارج الحدود).
    for (uint64_t off : {h.metadataOffset, h.dependencyOffset, h.symbolTableOffset, h.indexOffset,
                          h.blockTableOffset, h.dataOffset, h.integrityOffset, h.footerOffset}) {
        if (off > fileSize) throw ClcFormatError("header references offset beyond end of file (corrupted): " + path);
    }
    // Footer
    in.seekg(int64_t(fileSize - CLC_FOOTER_SIZE), std::ios::beg);
    std::vector<uint8_t> fbuf(CLC_FOOTER_SIZE);
    in.read(reinterpret_cast<char*>(fbuf.data()), CLC_FOOTER_SIZE);
    if (std::memcmp(fbuf.data(), CLC_FOOTER_MAGIC, 4) != 0)
        throw ClcFormatError("missing/invalid footer — file truncated or not a CLC container: " + path);
    return {h, fileSize};
}

struct ParsedSections {
    Metadata metadata;
    std::vector<DependencyEntry> deps;
    std::vector<std::string> dict;
    std::vector<FileEntry> files;
    std::vector<BlockEntry> blocks;
};

ParsedSections readAllSections(std::ifstream& in, const ClcHeader& h, uint64_t fileSize) {
    auto readSection = [&](uint64_t offset, uint64_t maxLen)->std::vector<uint8_t> {
        if (offset > fileSize) throw ClcFormatError("section offset beyond file size (corrupted)");
        uint64_t avail = fileSize - offset;
        uint64_t want = std::min(avail, maxLen);
        std::vector<uint8_t> buf(want);
        in.seekg(int64_t(offset), std::ios::beg);
        if (want > 0) in.read(reinterpret_cast<char*>(buf.data()), std::streamsize(want));
        return buf;
    };
    ParsedSections ps;
    {
        auto buf = readSection(h.metadataOffset, h.dependencyOffset > h.metadataOffset ? h.dependencyOffset - h.metadataOffset : fileSize - h.metadataOffset);
        ByteReader r(buf.data(), buf.size());
        ps.metadata = parseMetadata(r);
    }
    {
        auto buf = readSection(h.dependencyOffset, h.symbolTableOffset > h.dependencyOffset ? h.symbolTableOffset - h.dependencyOffset : fileSize - h.dependencyOffset);
        ByteReader r(buf.data(), buf.size());
        ps.deps = parseDependencies(r);
    }
    {
        auto buf = readSection(h.symbolTableOffset, h.indexOffset > h.symbolTableOffset ? h.indexOffset - h.symbolTableOffset : fileSize - h.symbolTableOffset);
        ByteReader r(buf.data(), buf.size());
        ps.dict = parseSymbolTable(r);
    }
    {
        auto buf = readSection(h.indexOffset, h.blockTableOffset > h.indexOffset ? h.blockTableOffset - h.indexOffset : fileSize - h.indexOffset);
        ByteReader r(buf.data(), buf.size());
        ps.files = parseFileIndex(r, h.fileCount);
    }
    {
        auto buf = readSection(h.blockTableOffset, h.integrityOffset > h.blockTableOffset ? h.integrityOffset - h.blockTableOffset : fileSize - h.blockTableOffset);
        ByteReader r(buf.data(), buf.size());
        ps.blocks = parseBlockTable(r, h.blockCount);
    }
    return ps;
}

// يعيد بناء محتوى ملف واحد بالكامل (inflate كل كتله بالترتيب + فك قاموس Rin إن لزم).
std::vector<uint8_t> reconstructFile(std::ifstream& in, const ClcHeader& h, const FileEntry& fe,
                                      const std::vector<BlockEntry>& blocks, const std::vector<std::string>& dict) {
    std::vector<uint8_t> encoded;
    encoded.reserve(fe.originalSize);
    for (uint32_t i = 0; i < fe.blockCount; ++i) {
        const BlockEntry& b = blocks.at(fe.firstBlock + i);
        uint64_t absOffset = h.dataOffset + b.offset;
        in.seekg(int64_t(absOffset), std::ios::beg);
        std::vector<uint8_t> stored(size_t(b.compressedSize));
        if (b.compressedSize > 0) in.read(reinterpret_cast<char*>(stored.data()), std::streamsize(b.compressedSize));
        std::vector<uint8_t> plain;
        if (CompressionMethod(b.method) == CompressionMethod::STORE) plain = std::move(stored);
        else plain = inflateRaw(stored.data(), stored.size(), size_t(b.originalSize));
        encoded.insert(encoded.end(), plain.begin(), plain.end());
    }
    bool isTextRin = (fe.flags & (FILE_FLAG_IS_RIN_SOURCE | FILE_FLAG_IS_TEXT)) != 0;
    return isTextRin ? decodeWithDictionary(encoded, dict) : encoded;
}

} // namespace (details)

// =========================================================================
// PACK
// =========================================================================
PackStats packDirectory(const std::string& srcDir, const std::string& outPath, const PackOptions& opts) {
    auto t0 = std::chrono::steady_clock::now();
    PackStats stats;

    auto entries = scanProject(srcDir);
    fs::path root(srcDir);

    // --- تمريرة أولى: بناء قاموس Rin/النصوص المشترك (بلا حمل كل المحتوى في الذاكرة) ---
    std::map<std::string,uint64_t> freq;
    for (auto& e : entries) {
        if (e.isDir) continue;
        std::string full = (root / e.relPath).string();
        // فحص سريع بالامتداد أولاً لتفادي قراءة ملفات ثنائية كبيرة بلا داعٍ.
        if (!hasRinExtension(e.relPath) && e.size > 8*1024*1024) continue; // ملف ثنائي كبير غالباً
        auto content = readWholeFile(full);
        if (looksLikeTextOrRin(e.relPath, content)) accumulateTokenFrequency(content, freq);
    }
    std::vector<std::string> dict = finalizeDictionary(freq);

    // --- فتح ملف الإخراج وكتابة رأس مؤقت (سيُعاد كتابته لاحقاً بالقيم الحقيقية) ---
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) throw ClcFormatError("cannot create output file: " + outPath);
    std::vector<uint8_t> placeholder(CLC_HEADER_SIZE, 0);
    out.write(reinterpret_cast<char*>(placeholder.data()), CLC_HEADER_SIZE);

    Sha256Stream containerHasher; // يبدأ من أول بايت بيانات فعلي (لا يشمل الرأس نفسه)
    uint64_t dataCursor = 0;
    uint32_t nextBlockId = 0;
    int zlibLevel = levelToZlib(opts.level);
    uint64_t chunkSize = (opts.level == Level::ULTRA) ? CHUNK_SIZE_ULTRA : CHUNK_SIZE_NORMAL;

    std::vector<FileEntry> fileEntries;
    std::vector<BlockEntry> blockEntries;
    std::unordered_map<std::string, std::pair<uint32_t,uint32_t>> dedupMap; // sha256 hex -> (firstBlock,count)

    for (auto& e : entries) {
        if (e.isDir) {
            FileEntry fe;
            fe.path = e.relPath; fe.flags = FILE_FLAG_IS_DIR;
            fileEntries.push_back(std::move(fe));
            continue;
        }
        std::string full = (root / e.relPath).string();
        stats.originalSize += e.size;

        bool likelyTextByExt = hasRinExtension(e.relPath) || e.size <= 8*1024*1024;
        FileEntry fe;
        fe.path = e.relPath;

        if (likelyTextByExt) {
            auto raw = readWholeFile(full);
            Sha256Digest hash = sha256(raw);
            std::string hex = sha256_hex(hash);
            fe.originalSize = raw.size();
            fe.contentHash = hash;
            auto dedupIt = dedupMap.find(hex);
            if (dedupIt != dedupMap.end()) {
                fe.firstBlock = dedupIt->second.first;
                fe.blockCount = dedupIt->second.second;
                // نحتاج أيضاً نسخ نفس الـ flags (نص/rin) المستخدَمة عند أول تخزين لنفس المحتوى،
                // لضمان فك تشفير القاموس بنفس الطريقة عند unpack.
                for (auto& existing : fileEntries) {
                    if (existing.firstBlock == fe.firstBlock && existing.blockCount == fe.blockCount && !(existing.flags & FILE_FLAG_IS_DIR)) {
                        fe.flags = existing.flags; break;
                    }
                }
                stats.dedupedFiles++;
            } else {
                bool isText = looksLikeTextOrRin(e.relPath, raw);
                bool isRin = e.relPath.size() >= 4 && e.relPath.compare(e.relPath.size()-4, 4, ".rin") == 0;
                fe.flags = uint8_t((isRin ? FILE_FLAG_IS_RIN_SOURCE : 0) | (isText ? FILE_FLAG_IS_TEXT : 0));
                std::vector<uint8_t> toStore = isText ? encodeWithDictionary(raw, dict) : raw;
                fe.firstBlock = nextBlockId;
                auto blocks = chunkCompressAndWrite(out, containerHasher, dataCursor, nextBlockId, toStore, chunkSize, zlibLevel);
                fe.blockCount = uint32_t(blocks.size());
                for (auto& b : blocks) blockEntries.push_back(b);
                dedupMap[hex] = {fe.firstBlock, fe.blockCount};
            }
        } else {
            // ملف ثنائي كبير: نتدفّق (stream) قراءته على شكل chunks بلا تحميله كاملاً في
            // الذاكرة أبداً — هذا هو المسار الذي يهم فعلياً لأداء الذاكرة (صور/فيديو كبيرة).
            std::ifstream fin(full, std::ios::binary);
            if (!fin) throw ClcFormatError("cannot open file: " + full);
            Sha256Stream fileHasher;
            fe.firstBlock = nextBlockId;
            std::vector<uint8_t> chunkBuf(chunkSize);
            uint32_t blocksForFile = 0;
            uint64_t totalRead = 0;
            while (fin) {
                fin.read(reinterpret_cast<char*>(chunkBuf.data()), std::streamsize(chunkSize));
                std::streamsize got = fin.gcount();
                if (got <= 0) break;
                fileHasher.update(chunkBuf.data(), size_t(got));
                BlockEntry be = compressChunkAndWrite(out, containerHasher, dataCursor, nextBlockId, chunkBuf.data(), size_t(got), zlibLevel);
                blockEntries.push_back(be);
                blocksForFile++;
                totalRead += uint64_t(got);
                if (!fin.good() && !fin.eof()) throw ClcFormatError("read error: " + full);
            }
            fe.originalSize = totalRead;
            fe.blockCount = blocksForFile;
            fe.contentHash = fileHasher.finish();
            fe.flags = 0;
            // ملاحظة تصميم موثّقة (FORMAT.md): الملفات الثنائية الكبيرة (>8MB) تُبَثّ
            // مباشرة بلا تحميلها كاملة في الذاكرة، وبالتالي لا تخضع لـ deduplication
            // (يتطلب معرفة الـ hash *قبل* الكتابة). الملفات الأصغر (وكل نصوص/Rin مهما
            // كان حجمها) تُقرأ كاملة فتستفيد من dedup الكامل.
        }
        fileEntries.push_back(std::move(fe));
    }

    stats.fileCount = fileEntries.size();

    // Metadata
    Metadata md = opts.metadata;
    std::string now = nowIso8601();
    if (md.created.empty()) md.created = now;
    md.modified = now;
    uint64_t metadataOffset = CLC_HEADER_SIZE + dataCursor;
    writeAndHash(out, containerHasher, serializeMetadata(md));

    // Dependencies
    uint64_t dependencyOffset = uint64_t(out.tellp());
    writeAndHash(out, containerHasher, serializeDependencies(opts.dependencies));

    // Rin symbol table
    uint64_t symbolTableOffset = uint64_t(out.tellp());
    writeAndHash(out, containerHasher, serializeSymbolTable(dict));

    // File index
    uint64_t indexOffset = uint64_t(out.tellp());
    writeAndHash(out, containerHasher, serializeFileIndex(fileEntries));

    // Block table
    uint64_t blockTableOffset = uint64_t(out.tellp());
    writeAndHash(out, containerHasher, serializeBlockTable(blockEntries));

    // Integrity section: sha256 لكل شيء من data_offset حتى نهاية block table.
    uint64_t integrityOffset = uint64_t(out.tellp());
    Sha256Digest containerHash = containerHasher.finish();
    out.write(reinterpret_cast<char*>(containerHash.data()), std::streamsize(containerHash.size()));

    // Footer
    uint64_t footerOffset = uint64_t(out.tellp());

    ClcHeader h;
    h.flags = uint16_t((opts.dependencies.empty() ? 0 : FLAG_HAS_DEPENDENCIES) |
                        (dict.empty() ? 0 : FLAG_HAS_RIN_DICT) | FLAG_STREAMING_SAFE);
    h.compressionMethod = uint8_t(CompressionMethod::DEFLATE);
    h.level = uint8_t(opts.level);
    h.fileCount = uint32_t(fileEntries.size());
    h.blockCount = uint32_t(blockEntries.size());
    h.metadataOffset = metadataOffset;
    h.dependencyOffset = dependencyOffset;
    h.symbolTableOffset = symbolTableOffset;
    h.indexOffset = indexOffset;
    h.blockTableOffset = blockTableOffset;
    h.dataOffset = CLC_HEADER_SIZE;
    h.integrityOffset = integrityOffset;
    h.footerOffset = footerOffset;

    auto headerBytesForCrc = serializeHeader(h); // crc32 field still 0 هنا، محسوب على bytes[0..83] فقط
    h.headerCrc32 = headerCrc(headerBytesForCrc);
    auto finalHeaderBytes = serializeHeader(h);

    ByteWriter fw;
    fw.raw(CLC_FOOTER_MAGIC, 4);
    fw.u64(0); // header_offset (الرأس دائماً عند 0 في CLC 1.0)
    fw.raw(containerHash.data(), containerHash.size());
    fw.u32(h.headerCrc32);
    out.write(reinterpret_cast<char*>(fw.buf.data()), std::streamsize(fw.buf.size()));

    // إعادة كتابة الرأس الحقيقي في البداية
    out.seekp(0, std::ios::beg);
    out.write(reinterpret_cast<char*>(finalHeaderBytes.data()), std::streamsize(finalHeaderBytes.size()));
    out.close();

    stats.compressedSize = fs::file_size(outPath);
    stats.blockCount = blockEntries.size();
    auto t1 = std::chrono::steady_clock::now();
    stats.packSeconds = std::chrono::duration<double>(t1 - t0).count();
    return stats;
}

// =========================================================================
// INFO / LIST (بنية فقط، بلا فك بيانات)
// =========================================================================
ContainerInfo readContainerInfo(const std::string& rclPath) {
    std::ifstream in(rclPath, std::ios::binary);
    if (!in) throw ClcFormatError("cannot open container: " + rclPath);
    auto [h, fileSize] = readAndValidateHeader(in, rclPath);
    auto sections = readAllSections(in, h, fileSize);
    ContainerInfo info;
    info.header = h;
    info.metadata = sections.metadata;
    info.dependencies = sections.deps;
    info.files = sections.files;
    info.blocks = sections.blocks;
    info.containerFileSize = fileSize;
    return info;
}

// =========================================================================
// CHECK (سريع: crc32 لكل كتلة + اتساق بنيوي)
// =========================================================================
CheckReport checkContainer(const std::string& rclPath) {
    CheckReport report;
    std::ifstream in(rclPath, std::ios::binary);
    if (!in) { report.sections.push_back({"Open", false, "cannot open file"}); return report; }

    ClcHeader h; uint64_t fileSize;
    try {
        auto pr = readAndValidateHeader(in, rclPath);
        h = pr.first; fileSize = pr.second;
        report.sections.push_back({"Header", true, ""});
    } catch (const std::exception& e) {
        report.sections.push_back({"Header", false, e.what()});
        return report;
    }

    ParsedSections ps;
    try {
        ps = readAllSections(in, h, fileSize);
        report.sections.push_back({"Metadata", true, ""});
        report.sections.push_back({"Index", true, ""});
    } catch (const std::exception& e) {
        report.sections.push_back({"Metadata/Index", false, e.what()});
        return report;
    }

    bool blocksOk = true;
    for (auto& b : ps.blocks) {
        uint64_t absOffset = h.dataOffset + b.offset;
        if (absOffset + b.compressedSize > fileSize) {
            report.corruptBlocks.push_back("Block " + std::to_string(b.id) + " points beyond end of file");
            blocksOk = false;
            continue;
        }
        in.seekg(int64_t(absOffset), std::ios::beg);
        std::vector<uint8_t> stored(size_t(b.compressedSize));
        if (b.compressedSize > 0) in.read(reinterpret_cast<char*>(stored.data()), std::streamsize(b.compressedSize));
        uint32_t actualCrc = crc32Of(stored.data(), stored.size());
        if (actualCrc != b.crc32OfStored) {
            std::ostringstream msg;
            msg << "Block " << std::setfill('0') << std::setw(4) << b.id << " is corrupted.\n"
                << "  Expected CRC32: " << std::hex << std::setw(8) << b.crc32OfStored << "\n"
                << "  Found CRC32:    " << std::hex << std::setw(8) << actualCrc;
            report.corruptBlocks.push_back(msg.str());
            blocksOk = false;
        }
    }
    report.sections.push_back({"Blocks", blocksOk, blocksOk ? "" : (std::to_string(report.corruptBlocks.size()) + " corrupted block(s)")});

    // اتساق الملفات: كل blockCount/firstBlock يجب أن يقع ضمن جدول الكتل الفعلي.
    bool filesOk = true;
    for (auto& f : ps.files) {
        if (f.flags & FILE_FLAG_IS_DIR) continue;
        if (uint64_t(f.firstBlock) + f.blockCount > ps.blocks.size()) { filesOk = false; break; }
    }
    report.sections.push_back({"Files", filesOk, filesOk ? "" : "file index references out-of-range blocks"});

    return report;
}

// =========================================================================
// VERIFY (كامل: check() + sha256 لكل ملف + sha256 للحاوية)
// =========================================================================
CheckReport verifyContainer(const std::string& rclPath) {
    CheckReport report = checkContainer(rclPath);
    if (!report.allOk()) return report;

    std::ifstream in(rclPath, std::ios::binary);
    auto [h, fileSize] = readAndValidateHeader(in, rclPath);
    auto ps = readAllSections(in, h, fileSize);

    bool filesHashOk = true;
    for (auto& f : ps.files) {
        if (f.flags & FILE_FLAG_IS_DIR) continue;
        try {
            auto content = reconstructFile(in, h, f, ps.blocks, ps.dict);
            if (content.size() != f.originalSize) { filesHashOk = false; continue; }
            auto actual = sha256(content);
            if (actual != f.contentHash) filesHashOk = false;
        } catch (...) { filesHashOk = false; }
    }
    report.sections.push_back({"File Hashes (sha256)", filesHashOk, filesHashOk ? "" : "one or more files failed content verification"});

    // إعادة حساب sha256 الحاوية: من data_offset حتى نهاية block table (نفس نطاق الحزمة).
    Sha256Stream hasher;
    uint64_t rangeStart = h.dataOffset;
    uint64_t rangeEnd = h.integrityOffset;
    in.seekg(int64_t(rangeStart), std::ios::beg);
    std::vector<uint8_t> buf(1 << 20);
    uint64_t remaining = rangeEnd - rangeStart;
    while (remaining > 0) {
        uint64_t chunk = std::min<uint64_t>(remaining, buf.size());
        in.read(reinterpret_cast<char*>(buf.data()), std::streamsize(chunk));
        hasher.update(buf.data(), size_t(chunk));
        remaining -= chunk;
    }
    Sha256Digest recomputed = hasher.finish();
    in.seekg(int64_t(h.integrityOffset), std::ios::beg);
    Sha256Digest stored;
    in.read(reinterpret_cast<char*>(stored.data()), std::streamsize(stored.size()));
    bool containerOk = (recomputed == stored);
    report.sections.push_back({"Container Hash (sha256)", containerOk, containerOk ? "" : "container-level hash mismatch"});

    return report;
}

// =========================================================================
// UNPACK
// =========================================================================
UnpackStats unpackContainer(const std::string& rclPath, const std::string& outDir, bool quiet) {
    auto t0 = std::chrono::steady_clock::now();
    std::ifstream in(rclPath, std::ios::binary);
    if (!in) throw ClcFormatError("cannot open container: " + rclPath);
    auto [h, fileSize] = readAndValidateHeader(in, rclPath);
    auto ps = readAllSections(in, h, fileSize);

    fs::create_directories(outDir);
    UnpackStats stats;

    for (auto& f : ps.files) {
        std::string safe = sanitizeEntryPath(f.path);
        std::string fullPath = resolveUnderRoot(outDir, safe);
        if (f.flags & FILE_FLAG_IS_DIR) {
            fs::create_directories(fullPath);
            continue;
        }
        fs::create_directories(fs::path(fullPath).parent_path());
        auto content = reconstructFile(in, h, f, ps.blocks, ps.dict);
        if (content.size() != f.originalSize)
            throw ClcFormatError("size mismatch reconstructing '" + f.path + "' (corrupted container)");
        auto actualHash = sha256(content);
        if (actualHash != f.contentHash)
            throw ClcFormatError("content hash mismatch for '" + f.path + "' (corrupted container)");
        std::ofstream fout(fullPath, std::ios::binary | std::ios::trunc);
        if (!fout) throw ClcFormatError("cannot write output file: " + fullPath);
        if (!content.empty()) fout.write(reinterpret_cast<char*>(content.data()), std::streamsize(content.size()));
        stats.fileCount++;
        stats.totalBytes += content.size();
        if (!quiet) std::cout << "  extracted  " << f.path << " (" << content.size() << " bytes)\n";
    }
    auto t1 = std::chrono::steady_clock::now();
    stats.unpackSeconds = std::chrono::duration<double>(t1 - t0).count();
    return stats;
}

void extractOneFile(const std::string& rclPath, const std::string& entryPath, const std::string& outDir) {
    std::ifstream in(rclPath, std::ios::binary);
    if (!in) throw ClcFormatError("cannot open container: " + rclPath);
    auto [h, fileSize] = readAndValidateHeader(in, rclPath);
    auto ps = readAllSections(in, h, fileSize);

    for (auto& f : ps.files) {
        if (f.flags & FILE_FLAG_IS_DIR) continue;
        if (f.path == entryPath) {
            std::string safe = sanitizeEntryPath(f.path);
            std::string fullPath = resolveUnderRoot(outDir, safe);
            fs::create_directories(fs::path(fullPath).parent_path());
            auto content = reconstructFile(in, h, f, ps.blocks, ps.dict);
            auto actualHash = sha256(content);
            if (actualHash != f.contentHash) throw ClcFormatError("content hash mismatch for '" + f.path + "'");
            std::ofstream fout(fullPath, std::ios::binary | std::ios::trunc);
            if (!content.empty()) fout.write(reinterpret_cast<char*>(content.data()), std::streamsize(content.size()));
            return;
        }
    }
    throw ClcFormatError("entry not found in container: " + entryPath);
}

} // namespace clc

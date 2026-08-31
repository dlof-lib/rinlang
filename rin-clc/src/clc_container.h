// clc_container.h — الواجهة العليا لصيغة CLC: pack/unpack/list/info/check/verify/extract.
// هذا هو ما يستدعيه CLI (src/cli/main.cpp) ولاحقاً ما سيُربَط بـ API لغة Rin
// (انظر docs/RIN_INTEGRATION.md).
#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include "clc_format.h"

namespace clc {

struct Metadata {
    std::string name;
    std::string version = "0.1.0";
    std::string author;
    std::string description;
    std::string license;
    std::string rinVersion;
    std::string entryPoint;
    std::string created;  // ISO-8601، يُملأ تلقائياً عند pack إن تُرك فارغاً
    std::string modified; // كذلك
    std::map<std::string,std::string> extra; // حقول مخصّصة إضافية
};

struct PackOptions {
    Level level = Level::L2;
    Metadata metadata;
    std::vector<DependencyEntry> dependencies;
    bool quiet = false;
};

struct PackStats {
    uint64_t originalSize = 0;
    uint64_t compressedSize = 0;
    uint64_t fileCount = 0;
    uint64_t blockCount = 0;
    uint64_t dedupedFiles = 0; // ملفات أُعيد استخدام blocks لها بدل تخزينها من جديد
    double packSeconds = 0.0;
    double ratioPercent() const {
        if (originalSize == 0) return 0.0;
        return 100.0 * (1.0 - double(compressedSize) / double(originalSize));
    }
};

struct UnpackStats {
    uint64_t fileCount = 0;
    uint64_t totalBytes = 0;
    double unpackSeconds = 0.0;
};

// نتيجة فحص بنيوي لكل قسم (لأمر `clc check` / `clc verify`).
struct SectionCheck { std::string name; bool ok; std::string detail; };
struct CheckReport {
    std::vector<SectionCheck> sections;
    std::vector<std::string> corruptBlocks; // رسائل مفصّلة لكل كتلة تالفة
    bool allOk() const {
        for (auto& s : sections) if (!s.ok) return false;
        return corruptBlocks.empty();
    }
};

// معلومات مقروءة من رأس/فهرس حاوية (لأمر `clc info` / `clc list`).
struct ContainerInfo {
    ClcHeader header;
    Metadata metadata;
    std::vector<DependencyEntry> dependencies;
    std::vector<FileEntry> files;
    std::vector<BlockEntry> blocks;
    uint64_t containerFileSize = 0;
};

// pack: يقرأ srcDir (شجرة ملفات على القرص) ويكتب حاوية CLC إلى outPath.
PackStats packDirectory(const std::string& srcDir, const std::string& outPath, const PackOptions& opts);

// unpack: يستخرج كل محتوى الحاوية إلى outDir (بأمان — انظر clc_security.h).
UnpackStats unpackContainer(const std::string& rclPath, const std::string& outDir, bool quiet = false);

// استخراج ملف واحد فقط بالاسم/المسار النسبي بدون فك المشروع كاملاً.
void extractOneFile(const std::string& rclPath, const std::string& entryPath, const std::string& outDir);

// يقرأ فقط البنية (header/metadata/index/blocks) بلا فك أي بيانات — سريع.
ContainerInfo readContainerInfo(const std::string& rclPath);

// فحص سريع: crc32 لكل كتلة + اتساق الأقسام البنيوية (بلا sha256 كامل).
CheckReport checkContainer(const std::string& rclPath);

// فحص كامل: check() + إعادة بناء كل ملف والتحقق من sha256 + تجزئة الحاوية الكلية.
CheckReport verifyContainer(const std::string& rclPath);

} // namespace clc

// ============================================================================
//  rin_ava_payload.h — برنامج Rin مُضمَّن داخل الملف التنفيذي (RIN_AVA)
// ============================================================================
//  rin_ava (الأداة الشبيهة بـ g++) تبني تنفيذياً مستقلاً بنسخ ملف `rin` نفسه ثم إلحاق
//  برنامجك به. عند تشغيل هذا التنفيذي يكتشف `rin` الحمولة الملحقة ويشغّلها بنفس
//  المفسّر الحقيقي وبنفس صياغة Rin — لا لغة جديدة ولا محاكاة.
//
//  تخطيط الحمولة (في نهاية الملف):
//      [name \0 source] [u64 payloadLen (little-endian)] ["RINAVAP1"]
//  الملف التنفيذي العادي لا يحمل المقطع الختامي فلا يتغيّر سلوكه إطلاقاً.
// ============================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace rin_ava {

constexpr char kMagic[9] = "RINAVAP1";

inline std::string selfExePath(const char* argv0) {
#if defined(__APPLE__)
    char buf[4096]; uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) == 0) return buf;
#else
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; return buf; }
#endif
    return argv0 ? argv0 : "";
}

// يرجع true إن كان الملف التنفيذي الحالي يحمل برنامج Rin مُضمَّناً.
inline bool readSelfPayload(const char* argv0, std::string& source, std::string& name) {
    std::string path = selfExePath(argv0);
    if (path.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = false;
    do {
        if (std::fseek(f, -16, SEEK_END) != 0) break;
        unsigned char tail[16];
        if (std::fread(tail, 1, 16, f) != 16) break;
        if (std::memcmp(tail + 8, kMagic, 8) != 0) break;
        uint64_t len = 0;
        for (int i = 7; i >= 0; --i) len = (len << 8) | tail[i];
        long total = std::ftell(f);
        if (total < 16 || len == 0 || len > static_cast<uint64_t>(total) - 16) break;
        if (std::fseek(f, total - 16 - static_cast<long>(len), SEEK_SET) != 0) break;
        std::string buf(static_cast<size_t>(len), '\0');
        if (std::fread(&buf[0], 1, buf.size(), f) != buf.size()) break;
        size_t z = buf.find('\0');
        if (z == std::string::npos) break;
        name = buf.substr(0, z);
        source = buf.substr(z + 1);
        ok = true;
    } while (false);
    std::fclose(f);
    return ok;
}

} // namespace rin_ava

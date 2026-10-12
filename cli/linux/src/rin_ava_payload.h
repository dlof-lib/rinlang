// ============================================================================
//  rin_ava_payload.h — برنامج Rin مُضمَّن داخل الملف التنفيذي (RIN_AVA)
// ============================================================================
//  rin_ava (الأداة الشبيهة بـ g++) تبني تنفيذياً مستقلاً بنسخ ملف `rin` نفسه ثم إلحاق
//  برنامجك به. عند تشغيل هذا التنفيذي يكتشف `rin` الحمولة الملحقة ويشغّلها بنفس
//  المفسّر/الآلة الحقيقيين وبنفس صياغة Rin — لا لغة جديدة ولا محاكاة.
//
//  تخطيط الحمولة (في نهاية الملف):
//      v1:  [name \0 source]        [u64 payloadLen LE] ["RINAVAP1"]   ← مصدر يُشغَّل بالمفسّر
//      v2:  [kind name \0 data]     [u64 payloadLen LE] ["RINAVAP2"]
//             kind = 'S' : data = مصدر Rin  → المفسّر (rin_interpreter.cpp)
//             kind = 'B' : data = بايتكود RBC1 → آلة البايتكود (toolchain/BytecodeVM)
//             kind = 'M' : data = صورة RVM1 (rin_vm) → المحرّك الكامل مع الاستيرادات المُضمَّنة، أو بايتكود إن حملته
//  الملف التنفيذي العادي لا يحمل المقطع الختامي فلا يتغيّر سلوكه إطلاقاً.
//  نواة v1 تبقى مدعومة كاملةً (توافق مع تنفيذيات بُنيت بإصدار سابق).
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

constexpr char kMagic[9]   = "RINAVAP1";  // v1
constexpr char kMagicV2[9] = "RINAVAP2";  // v2

constexpr char kKindSource   = 'S';  // مصدر Rin → المفسّر
constexpr char kKindBytecode = 'B';  // بايتكود RBC1 → BytecodeVM
constexpr char kKindImage    = 'M';  // صورة RVM1 (cli/rin_vm/include/rin_vm.h): مصدر + استيرادات (+ بايتكود)

struct Payload {
    char kind = kKindSource;
    std::string name;
    std::string data;
};

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

// يقرأ الحمولة (v1 أو v2) من نهاية الملف التنفيذي الحالي. true إن وُجدت حمولة صالحة.
inline bool readSelfPayloadEx(const char* argv0, Payload& out) {
    std::string path = selfExePath(argv0);
    if (path.empty()) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    bool ok = false;
    do {
        if (std::fseek(f, -16, SEEK_END) != 0) break;
        unsigned char tail[16];
        if (std::fread(tail, 1, 16, f) != 16) break;
        bool v2 = std::memcmp(tail + 8, kMagicV2, 8) == 0;
        if (!v2 && std::memcmp(tail + 8, kMagic, 8) != 0) break;
        uint64_t len = 0;
        for (int i = 7; i >= 0; --i) len = (len << 8) | tail[i];
        long total = std::ftell(f);
        if (total < 16 || len == 0 || len > static_cast<uint64_t>(total) - 16) break;
        if (std::fseek(f, total - 16 - static_cast<long>(len), SEEK_SET) != 0) break;
        std::string buf(static_cast<size_t>(len), '\0');
        if (std::fread(&buf[0], 1, buf.size(), f) != buf.size()) break;
        size_t off = 0;
        out.kind = kKindSource;
        if (v2) {
            out.kind = buf[0];
            off = 1;
            if (out.kind != kKindSource && out.kind != kKindBytecode && out.kind != kKindImage) break;
        }
        size_t z = buf.find('\0', off);
        if (z == std::string::npos) break;
        out.name = buf.substr(off, z - off);
        out.data = buf.substr(z + 1);
        ok = true;
    } while (false);
    std::fclose(f);
    return ok;
}

// واجهة v1 القديمة (تُبقي التوافق): تُرجع فقط حمولات المصدر.
inline bool readSelfPayload(const char* argv0, std::string& source, std::string& name) {
    Payload p;
    if (!readSelfPayloadEx(argv0, p) || p.kind != kKindSource) return false;
    source = p.data; name = p.name;
    return true;
}

} // namespace rin_ava

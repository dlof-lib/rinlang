#include "clc_compress.h"
#include "clc_io.h"
#include <zlib.h>
#include <stdexcept>

namespace clc {

int levelToZlib(Level lvl) {
    switch (lvl) {
        case Level::L0: return 0;
        case Level::L1: return 1;
        case Level::L2: return 6;
        case Level::L3: return 8;
        case Level::L4: return 9;
        case Level::ULTRA: return 9;
    }
    return 6;
}

std::vector<uint8_t> deflateRaw(const uint8_t* data, size_t len, int zlibLevel) {
    if (len == 0) return {};
    z_stream zs{};
    // windowBits سالب = raw deflate بلا رأس zlib (نفس ما تستخدمه rinzip.og.rin
    // عبر zlibDeflateRaw في المفسّر، لكن هنا كخطوة entropy فقط داخل CLC block).
    if (deflateInit2(&zs, zlibLevel, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        throw ClcFormatError("zlib deflateInit2 failed");
    zs.next_in = const_cast<Bytef*>(data);
    zs.avail_in = uInt(len);
    std::vector<uint8_t> out;
    out.resize(len + (len / 2) + 64);
    zs.next_out = out.data();
    zs.avail_out = uInt(out.size());
    int ret;
    for (;;) {
        ret = deflate(&zs, Z_FINISH);
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK && ret != Z_BUF_ERROR) { deflateEnd(&zs); throw ClcFormatError("zlib deflate failed"); }
        if (zs.avail_out == 0) {
            size_t used = out.size();
            out.resize(out.size() * 2);
            zs.next_out = out.data() + used;
            zs.avail_out = uInt(out.size() - used);
        } else if (ret == Z_BUF_ERROR) {
            break;
        }
    }
    out.resize(out.size() - zs.avail_out);
    deflateEnd(&zs);
    return out;
}

std::vector<uint8_t> inflateRaw(const uint8_t* data, size_t len, size_t originalSize) {
    if (originalSize == 0) return {};
    // حماية ضد decompression bombs: نُخصّص originalSize بالضبط (القيمة موثوقة لأنها
    // أتت من Block Table الذي تحقّقنا من اتساقه مع حجم الملف قبل الوصول هنا)، ولا
    // نسمح لـ zlib بالتوسّع أكثر من ذلك مهما كانت البيانات المضغوطة.
    z_stream zs{};
    if (inflateInit2(&zs, -15) != Z_OK)
        throw ClcFormatError("zlib inflateInit2 failed");
    std::vector<uint8_t> out(originalSize);
    zs.next_in = const_cast<Bytef*>(data);
    zs.avail_in = uInt(len);
    zs.next_out = out.data();
    zs.avail_out = uInt(out.size());
    int ret = inflate(&zs, Z_FINISH);
    size_t produced = out.size() - zs.avail_out;
    inflateEnd(&zs);
    if (ret != Z_STREAM_END || produced != originalSize)
        throw ClcFormatError("zlib inflate failed or size mismatch (corrupted block)");
    return out;
}

uint32_t crc32Of(const uint8_t* data, size_t len) {
    uLong c = crc32(0L, Z_NULL, 0);
    c = crc32(c, data, uInt(len));
    return uint32_t(c);
}

} // namespace clc

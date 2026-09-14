// bytebuf.h — أداة بناء بايتات خام (Little-Endian) مشتركة بين كل مولّدات الصيغ
// (ELF/PE/Mach-O/ar). هذا الملف لا يعرف شيئاً عن أي صيغة؛ فقط عمليات كتابة ثنائية دقيقة
// بالحجم والترتيب الصحيحين، لأن كل خطأ بايت واحد هنا يعني ملفاً تنفيذياً غير صالح فعلياً.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

namespace binfmt {

class ByteBuf {
public:
    std::vector<uint8_t> data;

    size_t pos() const { return data.size(); }

    void u8(uint8_t v) { data.push_back(v); }

    void u16le(uint16_t v) { u8(uint8_t(v & 0xFF)); u8(uint8_t((v >> 8) & 0xFF)); }
    void u32le(uint32_t v) {
        u8(uint8_t(v & 0xFF)); u8(uint8_t((v >> 8) & 0xFF));
        u8(uint8_t((v >> 16) & 0xFF)); u8(uint8_t((v >> 24) & 0xFF));
    }
    void u64le(uint64_t v) { u32le(uint32_t(v & 0xFFFFFFFFull)); u32le(uint32_t(v >> 32)); }

    void i32le(int32_t v) { u32le(uint32_t(v)); }
    void i64le(int64_t v) { u64le(uint64_t(v)); }

    // Big-endian (Mach-O على PowerPoint القديم لا يهمنا، لكن بعض حقول ar/COFF تحتاج BE)
    void u16be(uint16_t v) { u8(uint8_t((v >> 8) & 0xFF)); u8(uint8_t(v & 0xFF)); }
    void u32be(uint32_t v) {
        u8(uint8_t((v >> 24) & 0xFF)); u8(uint8_t((v >> 16) & 0xFF));
        u8(uint8_t((v >> 8) & 0xFF)); u8(uint8_t(v & 0xFF));
    }

    void bytes(const void* p, size_t n) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
        data.insert(data.end(), b, b + n);
    }
    void bytes(const std::vector<uint8_t>& v) { if (!v.empty()) bytes(v.data(), v.size()); }
    void str(const std::string& s, bool nullTerminate = false) {
        bytes(s.data(), s.size());
        if (nullTerminate) u8(0);
    }
    // نص بعرض ثابت مملوء بحرف معيّن (يُستخدَم في رؤوس ar/COFF ذات الحقول الثابتة العرض)
    void fixedStr(const std::string& s, size_t width, char pad = ' ') {
        std::string t = s.substr(0, width);
        bytes(t.data(), t.size());
        for (size_t i = t.size(); i < width; ++i) u8(uint8_t(pad));
    }

    void zeros(size_t n) { data.insert(data.end(), n, 0); }

    void alignTo(size_t alignment, uint8_t fill = 0) {
        if (alignment == 0) return;
        size_t rem = data.size() % alignment;
        if (rem != 0) data.insert(data.end(), alignment - rem, fill);
    }

    // يكتب v في موضع (offset) سابق بعد أن كان محجوزاً بأصفار — لملء حقول مثل "حجم القسم"
    // التي لا تُعرَف قيمتها إلا بعد بناء بقية الملف.
    void patchU32le(size_t offset, uint32_t v) {
        if (offset + 4 > data.size()) throw std::runtime_error("patchU32le: out of range");
        data[offset+0] = uint8_t(v & 0xFF);
        data[offset+1] = uint8_t((v >> 8) & 0xFF);
        data[offset+2] = uint8_t((v >> 16) & 0xFF);
        data[offset+3] = uint8_t((v >> 24) & 0xFF);
    }
    void patchU64le(size_t offset, uint64_t v) {
        patchU32le(offset, uint32_t(v & 0xFFFFFFFFull));
        patchU32le(offset + 4, uint32_t(v >> 32));
    }
    void patchU16le(size_t offset, uint16_t v) {
        if (offset + 2 > data.size()) throw std::runtime_error("patchU16le: out of range");
        data[offset+0] = uint8_t(v & 0xFF);
        data[offset+1] = uint8_t((v >> 8) & 0xFF);
    }

    std::string toString() const {
        return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    }
};

} // namespace binfmt

// clc_io.h — أدوات قراءة/كتابة ثنائية منخفضة المستوى لصيغة CLC.
// كل الأعداد تُكتب Little-Endian بعرض ثابت (لا نعتمد على padding البنى/الـ struct
// كي يكون التخطيط الثنائي مطابقاً بالضبط لما هو موثّق في docs/FORMAT.md على أي منصة).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include <istream>

namespace clc {

// يُرمى عند أي تناقض بنيوي في حاوية .rcl (رأس تالف، عدد غير معقول، إزاحة خارج الملف...).
struct ClcFormatError : std::runtime_error {
    explicit ClcFormatError(const std::string& msg) : std::runtime_error(msg) {}
};

// ---- كاتب bytes متسلسل في الذاكرة ----
class ByteWriter {
public:
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { for (int i=0;i<2;++i) buf.push_back(uint8_t((v >> (8*i)) & 0xff)); }
    void u32(uint32_t v) { for (int i=0;i<4;++i) buf.push_back(uint8_t((v >> (8*i)) & 0xff)); }
    void u64(uint64_t v) { for (int i=0;i<8;++i) buf.push_back(uint8_t((v >> (8*i)) & 0xff)); }
    void raw(const uint8_t* p, size_t n) { buf.insert(buf.end(), p, p+n); }
    void raw(const std::vector<uint8_t>& v) { raw(v.data(), v.size()); }
    // نص مسبوق بطوله كـ u32 (UTF-8 خام كما هو، يدعم العربية/أي Unicode بلا أي تحويل).
    void str32(const std::string& s) {
        u32(uint32_t(s.size()));
        raw(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
};

// ---- قارئ bytes متسلسل مع تحقق حدود صارم (يمنع Integer Overflow / قراءة خارج الحدود) ----
class ByteReader {
public:
    const uint8_t* data;
    size_t size;
    size_t pos = 0;

    ByteReader(const uint8_t* d, size_t n) : data(d), size(n) {}

    void need(size_t n) const {
        if (pos + n < pos || pos + n > size) // pos+n < pos يكتشف overflow نظرياً
            throw ClcFormatError("unexpected end of container data (truncated or corrupted .rcl)");
    }
    uint8_t u8() { need(1); return data[pos++]; }
    uint16_t u16() {
        need(2);
        uint16_t v = uint16_t(data[pos]) | (uint16_t(data[pos+1]) << 8);
        pos += 2; return v;
    }
    uint32_t u32() {
        need(4);
        uint32_t v = 0;
        for (int i=0;i<4;++i) v |= uint32_t(data[pos+i]) << (8*i);
        pos += 4; return v;
    }
    uint64_t u64() {
        need(8);
        uint64_t v = 0;
        for (int i=0;i<8;++i) v |= uint64_t(data[pos+i]) << (8*i);
        pos += 8; return v;
    }
    std::vector<uint8_t> raw(size_t n) {
        need(n);
        std::vector<uint8_t> out(data+pos, data+pos+n);
        pos += n;
        return out;
    }
    std::string str32(size_t maxLen = (64u*1024*1024)) {
        uint32_t n = u32();
        if (n > maxLen) throw ClcFormatError("string field declares implausible length (possible corruption/attack)");
        need(n);
        std::string s(reinterpret_cast<const char*>(data+pos), n);
        pos += n;
        return s;
    }
    bool eof() const { return pos >= size; }
    size_t remaining() const { return size - pos; }
};

// ---- بث حقيقي بلا قرص: يعرض buffer بايتات في الذاكرة (مثلاً جسم رد HTTP بعد تنزيل حاوية
// .rcl) كـ std::istream عادي، كي تُعاد نفس دوال القراءة (readAndValidateHeader/readAllSections/
// reconstructFile في clc_container.cpp، المكتوبة أصلاً بلغة std::istream& عامة) دون أي تكرار
// كود ودون كتابة أي بايت واحد إلى القرص. seekg/tellg/read تعمل بمؤشرات صرفة داخل الذاكرة.
class MemoryStreamBuf : public std::streambuf {
public:
    MemoryStreamBuf(const uint8_t* data, size_t size) {
        char* p = const_cast<char*>(reinterpret_cast<const char*>(data));
        setg(p, p, p + size);
    }
protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override {
        if (dir == std::ios_base::cur) gbump(int(off));
        else if (dir == std::ios_base::end) setg(eback(), egptr() + off, egptr());
        else setg(eback(), eback() + off, egptr());
        return pos_type(gptr() - eback());
    }
    pos_type seekpos(pos_type pos, std::ios_base::openmode which) override {
        return seekoff(off_type(pos), std::ios_base::beg, which);
    }
};

class MemoryIStream : public std::istream {
public:
    MemoryIStream(const uint8_t* data, size_t size) : std::istream(&buf_), buf_(data, size) {
        rdbuf(&buf_);
    }
    explicit MemoryIStream(const std::vector<uint8_t>& bytes)
        : MemoryIStream(bytes.data(), bytes.size()) {}
private:
    MemoryStreamBuf buf_;
};

} // namespace clc

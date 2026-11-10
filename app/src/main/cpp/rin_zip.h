// ============================================================================
//  rin_zip.h — محرّك ZIP مستقل (قراءة + كتابة) فوق zlib. بلا أي اعتماد على المفسّر.
// ----------------------------------------------------------------------------
//  يُستعمل من rin_container_zip.cpp (natives: zip.* و container.zip/unzip ...)،
//  وهو header-only ويُختبَر منفرداً (tools/test_rin_zip.cpp).
//
//  ما يدعمه:
//    • قراءة: Store (0) و Deflate (8)، data-descriptor، أسماء UTF-8، تعليق الأرشيف،
//      ZipCrypto (كلمة مرور تقليدية)، تحقّق CRC-32 وحجم كل ملف.
//    • كتابة: Deflate بمستويات 0..9 (يختار Store تلقائياً إن لم يصغّر الضغط الملف)،
//      ZipCrypto اختيارياً، تعليق، وقت تعديل. أرشيف ناتج يُفتح بـ unzip/7-Zip/Explorer.
//    • إعادة كتابة أرشيف موجود (إضافة/حذف/إعادة تسمية/دمج) بنسخ البيانات المضغوطة
//      كما هي بلا فكّ ولا إعادة ضغط (addRaw).
//  حدود أمان صريحة (Limits): عدد الملفات، حجم الملف الواحد، المجموع بعد الفكّ — ضد
//  "قنابل الضغط". أسماء الملفات تُفحَص (safeName) ضد Zip-Slip والمسارات المطلقة.
//  غير مدعوم عمداً (يُرفض بخطأ واضح لا بصمت): ZIP64، تشفير AES، طرق ضغط غير 0/8.
//  ملاحظة: ZipCrypto حماية خفيفة متوافقة مع الأدوات القديمة وليست تشفيراً قوياً.
// ============================================================================
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include <zlib.h>

namespace rin {
namespace zipx {

struct ZipError : std::runtime_error {
    explicit ZipError(const std::string& m) : std::runtime_error(m) {}
};

struct Limits {
    size_t   maxEntries   = 50000;
    uint64_t maxEntrySize = 256ull << 20;   // 256 MiB لكل ملف بعد الفكّ
    uint64_t maxTotal     = 1024ull << 20;  // 1 GiB لكل الأرشيف بعد الفكّ
};

struct Entry {
    std::string name;
    uint16_t method = 0;
    uint16_t flags = 0;
    uint32_t crc = 0;
    uint64_t csize = 0;
    uint64_t usize = 0;
    uint64_t localOffset = 0;
    uint16_t dosTime = 0, dosDate = 0x21; // 1980-01-01
    uint32_t extAttr = 0;
    uint16_t madeBy = (3 << 8) | 20;       // Unix, v2.0
    std::string comment;
    bool isDir() const { return !name.empty() && name.back() == '/'; }
    bool encrypted() const { return (flags & 1) != 0; }
};

// ------------------------------------------------------------------ أدوات صغيرة
namespace detail {
inline uint16_t rd16(const std::string& d, size_t p) {
    return static_cast<uint16_t>(static_cast<uint8_t>(d[p]) | (static_cast<uint8_t>(d[p + 1]) << 8));
}
inline uint32_t rd32(const std::string& d, size_t p) {
    return static_cast<uint32_t>(static_cast<uint8_t>(d[p])) | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 2])) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d[p + 3])) << 24);
}
inline void wr16(std::string& o, uint32_t v) { o.push_back(char(v & 0xff)); o.push_back(char((v >> 8) & 0xff)); }
inline void wr32(std::string& o, uint32_t v) { wr16(o, v & 0xffff); wr16(o, (v >> 16) & 0xffff); }

inline uint32_t crcOf(const std::string& s) {
    uLong c = crc32(0L, Z_NULL, 0);
    size_t off = 0;
    while (off < s.size()) { // crc32 يأخذ uInt: نقطّع للحجوم الكبيرة
        uInt n = static_cast<uInt>(std::min<size_t>(s.size() - off, 1u << 30));
        c = crc32(c, reinterpret_cast<const Bytef*>(s.data()) + off, n);
        off += n;
    }
    return static_cast<uint32_t>(c);
}

// ---- ZipCrypto (PKWARE traditional) ----
struct Keys {
    uint32_t k0 = 0x12345678, k1 = 0x23456789, k2 = 0x34567890;
    static uint32_t crcUpd(uint32_t c, uint8_t b) { return get_crc_table()[(c ^ b) & 0xff] ^ (c >> 8); }
    void update(uint8_t c) {
        k0 = crcUpd(k0, c);
        k1 = (k1 + (k0 & 0xff)) * 134775813u + 1;
        k2 = crcUpd(k2, static_cast<uint8_t>(k1 >> 24));
    }
    uint8_t stream() const { uint32_t t = (k2 | 2) & 0xffff; return static_cast<uint8_t>(((t * (t ^ 1)) >> 8) & 0xff); }
    void init(const std::string& pw) { for (unsigned char c : pw) update(c); }
    uint8_t decrypt(uint8_t c) { uint8_t p = c ^ stream(); update(p); return p; }
    uint8_t encrypt(uint8_t p) { uint8_t c = p ^ stream(); update(p); return c; }
};

inline void toDos(time_t t, uint16_t& dt, uint16_t& dd) {
    std::tm tmv{};
#ifdef _WIN32
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    int y = tmv.tm_year + 1900;
    if (y < 1980) { dt = 0; dd = 0x21; return; }
    if (y > 2107) y = 2107;
    dt = static_cast<uint16_t>((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
    dd = static_cast<uint16_t>(((y - 1980) << 9) | ((tmv.tm_mon + 1) << 5) | tmv.tm_mday);
}
inline time_t fromDos(uint16_t dt, uint16_t dd) {
    std::tm tmv{};
    tmv.tm_year = ((dd >> 9) & 0x7f) + 80;
    tmv.tm_mon = ((dd >> 5) & 0x0f) - 1;
    tmv.tm_mday = dd & 0x1f;
    tmv.tm_hour = (dt >> 11) & 0x1f;
    tmv.tm_min = (dt >> 5) & 0x3f;
    tmv.tm_sec = (dt & 0x1f) * 2;
    if (tmv.tm_mon < 0 || tmv.tm_mon > 11 || tmv.tm_mday < 1) return 0;
#ifdef _WIN32
    return _mkgmtime(&tmv);
#else
    return timegm(&tmv);
#endif
}
} // namespace detail

// يطبّع اسم entry ويرفض الخطير: مطلق / ".." / NUL / حرف قرص / طويل جداً. يرمي ZipError.
// يعيد الاسم بفواصل '/' ودون بادئة "./" (يحافظ على '/' الختامية للمجلدات).
inline std::string safeName(std::string name) {
    for (auto& c : name) if (c == '\\') c = '/';
    if (name.find('\0') != std::string::npos) throw ZipError("اسم ملف يحتوي NUL");
    bool dir = !name.empty() && name.back() == '/';
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= name.size()) {
        size_t s = name.find('/', i);
        std::string seg = (s == std::string::npos) ? name.substr(i) : name.substr(i, s - i);
        if (seg == "..") throw ZipError("اسم ملف غير آمن (يحتوي ..): '" + name + "'");
        if (!seg.empty() && seg != ".") parts.push_back(seg);
        if (s == std::string::npos) break;
        i = s + 1;
    }
    if (!name.empty() && name[0] == '/') throw ZipError("اسم ملف مطلق غير مسموح: '" + name + "'");
    if (!parts.empty() && parts[0].size() >= 2 && parts[0][1] == ':' && std::isalpha(static_cast<unsigned char>(parts[0][0])))
        throw ZipError("اسم ملف بحرف قرص غير مسموح: '" + name + "'");
    if (parts.empty()) throw ZipError("اسم ملف فارغ");
    std::string out;
    for (size_t k = 0; k < parts.size(); ++k) { if (k) out += '/'; out += parts[k]; }
    if (dir) out += '/';
    if (out.size() > 65000) throw ZipError("اسم ملف طويل جداً");
    return out;
}

// تطابق glob بسيط: * (أي شيء بما فيه '/') ، ? (حرف واحد). بلا حساسية لحالة الأحرف.
inline bool globMatch(const std::string& pat, const std::string& s) {
    size_t p = 0, i = 0, star = std::string::npos, mark = 0;
    auto lc = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
    while (i < s.size()) {
        if (p < pat.size() && (pat[p] == '?' || lc(pat[p]) == lc(s[i]))) { ++p; ++i; }
        else if (p < pat.size() && pat[p] == '*') { star = p++; mark = i; }
        else if (star != std::string::npos) { p = star + 1; i = ++mark; }
        else return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

// ------------------------------------------------------------------ القارئ
class Reader {
public:
    explicit Reader(std::string data, Limits lim = Limits()) : d_(std::move(data)), lim_(lim) { parse(); }

    const std::vector<Entry>& entries() const { return entries_; }
    const std::string& comment() const { return comment_; }
    size_t byteSize() const { return d_.size(); }
    const Limits& limits() const { return lim_; }

    const Entry* find(const std::string& name) const {
        auto it = index_.find(name);
        return it == index_.end() ? nullptr : &entries_[it->second];
    }

    // البيانات المضغوطة كما هي (لإعادة الكتابة بلا فكّ). تتحقق من الحدود.
    std::string raw(const Entry& e) const {
        size_t start = dataStart(e);
        return d_.substr(start, static_cast<size_t>(e.csize));
    }

    // يفكّ ملفاً كاملاً مع التحقق من الحجم وCRC. password لـ ZipCrypto.
    std::string read(const Entry& e, const std::string& password = std::string()) const {
        if (e.isDir()) return std::string();
        if (e.usize > lim_.maxEntrySize) throw ZipError("الملف '" + e.name + "' أكبر من الحد المسموح (" + std::to_string(lim_.maxEntrySize) + " بايت)");
        if (e.flags & 0x40) throw ZipError("تشفير قوي (strong encryption) غير مدعوم: '" + e.name + "'");
        if (e.method == 99) throw ZipError("تشفير AES غير مدعوم: '" + e.name + "' (المدعوم: ZipCrypto)");
        size_t start = dataStart(e);
        std::string body = d_.substr(start, static_cast<size_t>(e.csize));
        if (e.encrypted()) {
            if (password.empty()) throw ZipError("الملف '" + e.name + "' مشفّر — كلمة المرور مطلوبة (opts.password)");
            if (body.size() < 12) throw ZipError("بيانات مشفّرة تالفة: '" + e.name + "'");
            detail::Keys k; k.init(password);
            std::string plain; plain.reserve(body.size() - 12);
            uint8_t last = 0;
            for (size_t i = 0; i < body.size(); ++i) {
                uint8_t p = k.decrypt(static_cast<uint8_t>(body[i]));
                if (i < 12) last = p; else plain.push_back(static_cast<char>(p));
            }
            uint8_t want = (e.flags & 8) ? static_cast<uint8_t>(e.dosTime >> 8) : static_cast<uint8_t>(e.crc >> 24);
            if (last != want) throw ZipError("كلمة المرور خاطئة للملف '" + e.name + "'");
            body.swap(plain);
        }
        std::string out;
        if (e.method == 0) {
            if (body.size() != e.usize) throw ZipError("حجم غير متطابق (Store): '" + e.name + "'");
            out.swap(body);
        } else if (e.method == 8) {
            out = inflateRaw(body, static_cast<size_t>(e.usize), e.name);
        } else {
            throw ZipError("طريقة ضغط غير مدعومة (" + std::to_string(e.method) + ") للملف '" + e.name + "'");
        }
        if (detail::crcOf(out) != e.crc) throw ZipError("فشل CRC-32 للملف '" + e.name + "' (بيانات تالفة" + std::string(e.encrypted() ? " أو كلمة مرور خاطئة" : "") + ")");
        return out;
    }

    static std::string inflateRaw(const std::string& in, size_t expected, const std::string& who) {
        if (expected == 0) return std::string();
        z_stream s{};
        if (inflateInit2(&s, -15) != Z_OK) throw ZipError("تعذّر تهيئة فكّ الضغط");
        std::string out; out.resize(expected);
        s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
        s.avail_in = static_cast<uInt>(in.size());
        s.next_out = reinterpret_cast<Bytef*>(&out[0]);
        s.avail_out = static_cast<uInt>(out.size());
        int r = inflate(&s, Z_FINISH);
        size_t got = s.total_out;
        inflateEnd(&s);
        if (r != Z_STREAM_END || got != expected) throw ZipError("فشل فكّ ضغط '" + who + "' (بيانات تالفة أو الحجم المعلن خاطئ)");
        return out;
    }

private:
    std::string d_;
    Limits lim_;
    std::vector<Entry> entries_;
    std::map<std::string, size_t> index_;
    std::string comment_;
    uint64_t cdOffset_ = 0;

    size_t dataStart(const Entry& e) const {
        uint64_t o = e.localOffset;
        if (o + 30 > d_.size() || detail::rd32(d_, static_cast<size_t>(o)) != 0x04034b50u)
            throw ZipError("ترويسة محلية تالفة للملف '" + e.name + "'");
        uint64_t n = detail::rd16(d_, static_cast<size_t>(o) + 26), x = detail::rd16(d_, static_cast<size_t>(o) + 28);
        uint64_t start = o + 30 + n + x;
        if (start + e.csize > d_.size()) throw ZipError("بيانات الملف '" + e.name + "' تتجاوز حجم الأرشيف (الأرشيف مبتور؟)");
        return static_cast<size_t>(start);
    }

    void parse() {
        if (d_.size() < 22) throw ZipError("ليس أرشيف ZIP (صغير جداً)");
        size_t minPos = d_.size() > 22 + 65535 ? d_.size() - 22 - 65535 : 0;
        size_t eocd = std::string::npos;
        for (size_t p = d_.size() - 22 + 1; p-- > minPos;) {
            if (detail::rd32(d_, p) == 0x06054b50u) {
                uint16_t cl = detail::rd16(d_, p + 20);
                if (p + 22 + cl <= d_.size()) { eocd = p; break; }
            }
        }
        if (eocd == std::string::npos) throw ZipError("ليس أرشيف ZIP صالحاً (لا توجد نهاية دليل مركزي)");
        if (eocd >= 20 && detail::rd32(d_, eocd - 20) == 0x07064b50u) throw ZipError("ZIP64 غير مدعوم");
        uint16_t total = detail::rd16(d_, eocd + 10);
        uint32_t cdSize = detail::rd32(d_, eocd + 12);
        uint32_t cdOff = detail::rd32(d_, eocd + 16);
        uint16_t cl = detail::rd16(d_, eocd + 20);
        if (total == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) throw ZipError("ZIP64 غير مدعوم");
        comment_ = d_.substr(eocd + 22, cl);
        if (static_cast<uint64_t>(cdOff) + cdSize > eocd) throw ZipError("دليل مركزي تالف (إزاحة خارج الأرشيف)");
        if (total > lim_.maxEntries) throw ZipError("عدد ملفات الأرشيف (" + std::to_string(total) + ") يتجاوز الحد (" + std::to_string(lim_.maxEntries) + ")");
        cdOffset_ = cdOff;
        size_t p = cdOff;
        uint64_t sumU = 0;
        for (uint16_t i = 0; i < total; ++i) {
            if (p + 46 > eocd || detail::rd32(d_, p) != 0x02014b50u) throw ZipError("إدخال دليل مركزي تالف (#" + std::to_string(i) + ")");
            Entry e;
            e.madeBy = detail::rd16(d_, p + 4);
            e.flags = detail::rd16(d_, p + 8);
            e.method = detail::rd16(d_, p + 10);
            e.dosTime = detail::rd16(d_, p + 12);
            e.dosDate = detail::rd16(d_, p + 14);
            e.crc = detail::rd32(d_, p + 16);
            e.csize = detail::rd32(d_, p + 20);
            e.usize = detail::rd32(d_, p + 24);
            uint16_t nl = detail::rd16(d_, p + 28), xl = detail::rd16(d_, p + 30), cm = detail::rd16(d_, p + 32);
            e.extAttr = detail::rd32(d_, p + 38);
            e.localOffset = detail::rd32(d_, p + 42);
            if (p + 46 + nl + xl + cm > eocd) throw ZipError("إدخال دليل مركزي مبتور");
            e.name = d_.substr(p + 46, nl);
            e.comment = d_.substr(p + 46 + nl + xl, cm);
            if (e.csize == 0xFFFFFFFFu || e.usize == 0xFFFFFFFFu || e.localOffset == 0xFFFFFFFFu) throw ZipError("ZIP64 غير مدعوم");
            for (auto& c : e.name) if (c == '\\') c = '/';
            sumU += e.usize;
            if (sumU > lim_.maxTotal) throw ZipError("الحجم الكلي بعد الفكّ يتجاوز الحد المسموح (حماية من قنبلة ضغط)");
            if (!index_.count(e.name)) index_[e.name] = entries_.size();
            entries_.push_back(std::move(e));
            p += 46 + nl + xl + cm;
        }
    }
};

// ------------------------------------------------------------------ الكاتب
struct AddOptions {
    int level = 6;               // 0 = Store، 1..9 = Deflate
    std::string password;        // ZipCrypto إن لم تكن فارغة
    time_t mtime = 0;            // 0 = الآن
    uint32_t mode = 0644;        // صلاحيات Unix
    std::string comment;
};

class Writer {
public:
    using Add = AddOptions;

    void setComment(std::string c) { comment_ = std::move(c); }
    size_t count() const { return items_.size(); }
    bool has(const std::string& name) const { return names_.count(name) > 0; }

    void addDir(const std::string& rawName, time_t mtime = 0) {
        std::string name = safeName(rawName);
        if (name.back() != '/') name += '/';
        if (names_.count(name)) return;
        Item it;
        it.e.name = name; it.e.method = 0; it.e.flags = 1u << 11; it.e.crc = 0;
        it.e.extAttr = ((040755u) << 16) | 0x10;
        detail::toDos(mtime ? mtime : std::time(nullptr), it.e.dosTime, it.e.dosDate);
        push(std::move(it));
    }

    // يضيف ملفاً (يستبدل أي ملف بنفس الاسم). يعيد Entry المكتوب (الأحجام بعد الضغط).
    Entry addFile(const std::string& rawName, const std::string& content, const Add& opt = Add()) {
        std::string name = safeName(rawName);
        if (name.back() == '/') throw ZipError("اسم ملف ينتهي بـ '/' : '" + name + "'");
        Item it;
        it.e.name = name;
        it.e.usize = content.size();
        it.e.crc = detail::crcOf(content);
        it.e.flags = 1u << 11; // UTF-8
        it.e.extAttr = (static_cast<uint32_t>(0100000 | (opt.mode & 07777)) << 16);
        it.e.comment = opt.comment;
        detail::toDos(opt.mtime ? opt.mtime : std::time(nullptr), it.e.dosTime, it.e.dosDate);

        std::string body;
        int level = opt.level < 0 ? 6 : (opt.level > 9 ? 9 : opt.level);
        if (level > 0 && !content.empty()) {
            std::string z = deflateRaw(content, level);
            if (z.size() < content.size()) { body = std::move(z); it.e.method = 8; }
        }
        if (it.e.method == 0) body = content;
        if (!opt.password.empty()) {
            it.e.flags |= 1;
            static thread_local std::mt19937_64 rng{std::random_device{}()};
            std::string hdr(12, '\0');
            for (int i = 0; i < 11; ++i) hdr[i] = static_cast<char>(rng() & 0xff);
            hdr[11] = static_cast<char>(it.e.crc >> 24);
            detail::Keys k; k.init(opt.password);
            std::string enc; enc.reserve(12 + body.size());
            for (char c : hdr) enc.push_back(static_cast<char>(k.encrypt(static_cast<uint8_t>(c))));
            for (char c : body) enc.push_back(static_cast<char>(k.encrypt(static_cast<uint8_t>(c))));
            body.swap(enc);
        }
        it.e.csize = body.size();
        it.data = std::move(body);
        Entry out = it.e;
        push(std::move(it));
        return out;
    }

    // ينسخ ملفاً من قارئ آخر بلا فكّ ولا إعادة ضغط (يحفظ التشفير والطريقة). newName اختياري (إعادة تسمية).
    void addRaw(const Reader& r, const Entry& src, const std::string& newName = std::string()) {
        Item it;
        it.e = src;
        if (!newName.empty()) it.e.name = safeName(newName);
        if (!newName.empty()) { // الاسم الجديد نص UTF-8 → علّم الراية (bit 11)
            for (unsigned char c : it.e.name) if (c >= 0x80) { it.e.flags |= (1u << 11); break; }
        }
        it.data = r.raw(src);
        push(std::move(it));
    }

    bool remove(const std::string& name) {
        auto m = names_.find(name);
        if (m == names_.end()) return false;
        items_[m->second].dead = true;
        names_.erase(m);
        return true;
    }

    std::string finish() const {
        std::string out;
        std::vector<Entry> cd;
        for (const Item& it : items_) {
            if (it.dead) continue;
            Entry e = it.e;
            e.localOffset = out.size();
            bool desc = (e.flags & 8) != 0;
            detail::wr32(out, 0x04034b50u);
            detail::wr16(out, e.method == 0 && !e.isDir() ? 10 : 20);
            detail::wr16(out, e.flags);
            detail::wr16(out, e.method);
            detail::wr16(out, e.dosTime);
            detail::wr16(out, e.dosDate);
            detail::wr32(out, desc ? 0 : e.crc);
            detail::wr32(out, desc ? 0 : checked32(e.csize));
            detail::wr32(out, desc ? 0 : checked32(e.usize));
            detail::wr16(out, static_cast<uint32_t>(e.name.size()));
            detail::wr16(out, 0);
            out += e.name;
            out += it.data;
            if (desc) {
                detail::wr32(out, 0x08074b50u);
                detail::wr32(out, e.crc);
                detail::wr32(out, checked32(e.csize));
                detail::wr32(out, checked32(e.usize));
            }
            if (out.size() > 0xFFFFFFF0ull) throw ZipError("حجم الأرشيف يتجاوز حدّ ZIP العادي (4GB) — ZIP64 غير مدعوم");
            cd.push_back(std::move(e));
        }
        if (cd.size() >= 0xFFFF) throw ZipError("عدد الملفات يتجاوز حدّ ZIP العادي (65534)");
        size_t cdStart = out.size();
        for (const Entry& e : cd) {
            detail::wr32(out, 0x02014b50u);
            detail::wr16(out, e.madeBy);
            detail::wr16(out, e.method == 0 && !e.isDir() ? 10 : 20);
            detail::wr16(out, e.flags);
            detail::wr16(out, e.method);
            detail::wr16(out, e.dosTime);
            detail::wr16(out, e.dosDate);
            detail::wr32(out, e.crc);
            detail::wr32(out, checked32(e.csize));
            detail::wr32(out, checked32(e.usize));
            detail::wr16(out, static_cast<uint32_t>(e.name.size()));
            detail::wr16(out, 0);
            detail::wr16(out, static_cast<uint32_t>(e.comment.size()));
            detail::wr16(out, 0); detail::wr16(out, 0);
            detail::wr32(out, e.extAttr);
            detail::wr32(out, static_cast<uint32_t>(e.localOffset));
            out += e.name;
            out += e.comment;
        }
        size_t cdSize = out.size() - cdStart;
        detail::wr32(out, 0x06054b50u);
        detail::wr16(out, 0); detail::wr16(out, 0);
        detail::wr16(out, static_cast<uint32_t>(cd.size()));
        detail::wr16(out, static_cast<uint32_t>(cd.size()));
        detail::wr32(out, static_cast<uint32_t>(cdSize));
        detail::wr32(out, static_cast<uint32_t>(cdStart));
        std::string cm = comment_.substr(0, 65535);
        detail::wr16(out, static_cast<uint32_t>(cm.size()));
        out += cm;
        return out;
    }

    static std::string deflateRaw(const std::string& in, int level) {
        z_stream s{};
        if (deflateInit2(&s, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) throw ZipError("تعذّر تهيئة الضاغط");
        std::string out; out.resize(deflateBound(&s, static_cast<uLong>(in.size())) + 64);
        s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
        s.avail_in = static_cast<uInt>(in.size());
        s.next_out = reinterpret_cast<Bytef*>(&out[0]);
        s.avail_out = static_cast<uInt>(out.size());
        int r = deflate(&s, Z_FINISH);
        size_t n = s.total_out;
        deflateEnd(&s);
        if (r != Z_STREAM_END) throw ZipError("فشل الضغط");
        out.resize(n);
        return out;
    }

private:
    struct Item { Entry e; std::string data; bool dead = false; };
    std::vector<Item> items_;
    std::map<std::string, size_t> names_;
    std::string comment_;

    static uint32_t checked32(uint64_t v) {
        if (v > 0xFFFFFFFEull) throw ZipError("ملف أكبر من حدّ ZIP العادي (4GB) — ZIP64 غير مدعوم");
        return static_cast<uint32_t>(v);
    }
    void push(Item&& it) {
        auto m = names_.find(it.e.name);
        if (m != names_.end()) items_[m->second].dead = true; // الأحدث يستبدل
        names_[it.e.name] = items_.size();
        items_.push_back(std::move(it));
    }
};

// ------------------------------------------------------------------ gzip (RFC 1952)
inline std::string gzipCompress(const std::string& in, int level = 6) {
    z_stream s{};
    if (deflateInit2(&s, level < 0 ? 6 : (level > 9 ? 9 : level), Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) throw ZipError("تعذّر تهيئة gzip");
    std::string out; out.resize(deflateBound(&s, static_cast<uLong>(in.size())) + 64);
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    s.avail_in = static_cast<uInt>(in.size());
    s.next_out = reinterpret_cast<Bytef*>(&out[0]);
    s.avail_out = static_cast<uInt>(out.size());
    int r = deflate(&s, Z_FINISH);
    size_t n = s.total_out;
    deflateEnd(&s);
    if (r != Z_STREAM_END) throw ZipError("فشل ضغط gzip");
    out.resize(n);
    return out;
}
// يفكّ gzip (أو تدفّق zlib) مع سقف للحجم الناتج (حماية من القنابل).
inline std::string gzipDecompress(const std::string& in, uint64_t maxOut = 256ull << 20) {
    z_stream s{};
    if (inflateInit2(&s, 15 + 32) != Z_OK) throw ZipError("تعذّر تهيئة فكّ gzip");
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    s.avail_in = static_cast<uInt>(in.size());
    std::string out;
    char buf[1 << 16];
    int r = Z_OK;
    while (r != Z_STREAM_END) {
        s.next_out = reinterpret_cast<Bytef*>(buf);
        s.avail_out = sizeof(buf);
        r = inflate(&s, Z_NO_FLUSH);
        if (r != Z_OK && r != Z_STREAM_END) { inflateEnd(&s); throw ZipError("بيانات gzip تالفة"); }
        out.append(buf, sizeof(buf) - s.avail_out);
        if (out.size() > maxOut) { inflateEnd(&s); throw ZipError("الناتج بعد فكّ gzip يتجاوز الحد المسموح"); }
        if (r == Z_OK && s.avail_in == 0 && s.avail_out != 0) { inflateEnd(&s); throw ZipError("بيانات gzip مبتورة"); }
    }
    inflateEnd(&s);
    return out;
}

} // namespace zipx
} // namespace rin

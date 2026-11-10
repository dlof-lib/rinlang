// ============================================================================
//  rin_container_zip.cpp — ضغط وفكّ ضغط الحاويات (ZIP حقيقي) + أدوات أرشيف كاملة
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) من rin_interpreter.cpp بعد rin_extra_natives12.cpp، ويُسجَّل عبر
//  Interpreter::registerNativesContainerZip(). المحرّك نفسه في rin_zip.h (مستقل ومُختبَر).
//  التوثيق الكامل: docs/container_zip.md
//
//  حاويات (container.*):
//    container.zip(name | [names], path, opts?)   يضغط حاوية/عدّة حاويات (حقول + مستندات + صفوف جدول) في ZIP
//    container.zipAll(path, opts?)                نسخة احتياطية لكل الحاويات (opts.kind للتصفية)
//    container.unzip(name, path, opts?)           يستعيد حاوية من ZIP (تُنشأ إن غابت)
//    container.unzipAll(path, opts?)              يستعيد كل حاويات الأرشيف
//    container.zipInfo(path)                      يقرأ فهرس الأرشيف بلا استعادة
//    container.pack(name | [names], opts?)        نفس zip لكن يعيد البايتات (نص) بدل ملف — للشبكة/التخزين
//    container.unpack(name, bytes, opts?)         نفس unzip من بايتات في الذاكرة
//  أرشيفات عامة (zip.*):
//    create add list find info read readJson exists extract remove rename combine comment test
//    bytes unbytes gzip gunzip gzipFile gunzipFile
// ============================================================================
#include "rin_interpreter.h"
#include "rin_json.h"
#include "rin_zip.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#ifdef _WIN32
#  define RIN_STAT _stat
#  define RIN_STAT_T struct _stat
#else
#  define RIN_STAT stat
#  define RIN_STAT_T struct stat
#endif

namespace rin {
namespace czip {
using namespace extra;
namespace fs = std::filesystem;
using zipx::Entry;
using zipx::Reader;
using zipx::Writer;
using zipx::ZipError;

// ---------------------------------------------------------------- خيارات
static const Value* optv(const Value* o, const char* k) {
    if (!o || o->type != Value::Type::MAP || !o->map) return nullptr;
    return o->map->findValue(Value::string(k));
}
static bool optBool(const Value* o, const char* k, bool def) { const Value* v = optv(o, k); return (v && v->type != Value::Type::NIL) ? v->isTruthy() : def; }
static std::string optStr(const Value* o, const char* k, const std::string& def = std::string()) {
    const Value* v = optv(o, k);
    return (v && v->type == Value::Type::STRING) ? v->str : def;
}
static double optNum(const Value* o, const char* k, double def) {
    const Value* v = optv(o, k);
    return (v && v->type == Value::Type::NUMBER) ? v->number : def;
}
static std::vector<std::string> strList(const Value* v, const std::string& fn, const char* what, int line) {
    std::vector<std::string> out;
    if (!v || v->type == Value::Type::NIL) return out;
    if (v->type == Value::Type::STRING) { out.push_back(v->str); return out; }
    if (v->type == Value::Type::ARRAY && v->array) {
        for (const auto& x : *v->array) {
            if (x.type != Value::Type::STRING) throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "': " + what + " يجب أن تكون نصاً أو مصفوفة نصوص");
            out.push_back(x.str);
        }
        return out;
    }
    throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "': " + what + " يجب أن تكون نصاً أو مصفوفة نصوص");
}
static Value mkMap(std::initializer_list<std::pair<const char*, Value>> kv) {
    MapData m;
    for (const auto& p : kv) m.push_back({Value::string(p.first), p.second});
    return newMap(std::move(m));
}
static Value numV(double d) { return Value::num(d); }
static Value strV(const std::string& s) { return Value::string(s); }
static Value boolV(bool b) { return Value::boolean_(b); }

// مطابقة نمط: اسم تام، glob، أو بادئة مجلد ("docs" أو "docs/")
static bool matchPat(const std::string& pat, const std::string& name) {
    if (pat == name || zipx::globMatch(pat, name)) return true;
    std::string dir = pat;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    if (name.size() >= dir.size() && name.compare(0, dir.size(), dir) == 0) return true;
    return false;
}
static bool matchAny(const std::vector<std::string>& pats, const std::string& name) {
    for (const auto& p : pats) if (matchPat(p, name)) return true;
    return false;
}

// ---------------------------------------------------------------- JSON مُنمَّط (يحفظ Set والبايتات الثنائية والدقة الكاملة)
static bool validUtf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!len || i + len > n) return false;
        for (size_t k = 1; k < len; ++k) if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        if (len == 2 && c < 0xC2) return false;
        i += len;
    }
    return true;
}
static void encStr(const std::string& s, std::string& out) {
    if (!validUtf8(s)) { // بايتات ثنائية → {"$bytes":"hex"} بلا فقد
        static const char* hx = "0123456789abcdef";
        out += "{\"$bytes\":\"";
        for (unsigned char c : s) { out += hx[c >> 4]; out += hx[c & 15]; }
        out += "\"}";
        return;
    }
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += static_cast<char>(c);
        }
    }
    out += '"';
}
static void encKey(const std::string& s, std::string& out) { // مفاتيح JSON نصية دائماً
    std::string tmp; encStr(s, tmp);
    if (!tmp.empty() && tmp[0] == '"') out += tmp;
    else { out += '"'; for (unsigned char c : s) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; } out += '"'; }
}
static void encodeTyped(const Value& v, std::string& out, int depth = 0) {
    if (depth > 200) throw ZipError("بنية متداخلة أعمق من 200 مستوى");
    switch (v.type) {
        case Value::Type::NIL: case Value::Type::FUNCTION: out += "null"; break;
        case Value::Type::BOOL: out += v.boolean ? "true" : "false"; break;
        case Value::Type::NUMBER: {
            double n = v.number;
            if (std::isnan(n) || std::isinf(n)) { out += "null"; break; }
            if (n == std::floor(n) && std::fabs(n) < 9.0e15) { out += std::to_string(static_cast<long long>(n)); break; }
            char b[40]; snprintf(b, sizeof b, "%.17g", n); out += b; break;
        }
        case Value::Type::STRING: encStr(v.str, out); break;
        case Value::Type::ARRAY:
            out += '[';
            if (v.array) for (size_t i = 0; i < v.array->size(); ++i) { if (i) out += ','; encodeTyped((*v.array)[i], out, depth + 1); }
            out += ']'; break;
        case Value::Type::SET:
            out += "{\"$set\":[";
            if (v.set) for (size_t i = 0; i < v.set->items.size(); ++i) { if (i) out += ','; encodeTyped(v.set->items[i], out, depth + 1); }
            out += "]}"; break;
        case Value::Type::MAP:
            out += '{';
            if (v.map) for (size_t i = 0; i < v.map->size(); ++i) {
                if (i) out += ',';
                encKey((*v.map)[i].first.toDisplayString(), out);
                out += ':';
                encodeTyped((*v.map)[i].second, out, depth + 1);
            }
            out += '}'; break;
        case Value::Type::INSTANCE:
            out += '{';
            if (v.instance) {
                bool first = true;
                for (const auto& nm : v.instance->fieldOrder) {
                    auto it = v.instance->fields.find(nm);
                    if (it == v.instance->fields.end()) continue;
                    if (!first) out += ',';
                    first = false;
                    encKey(nm, out); out += ':'; encodeTyped(it->second, out, depth + 1);
                }
            }
            out += '}'; break;
    }
}
static std::string unhex(const std::string& h) {
    auto hv = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); };
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) { int a = hv(h[i]), b = hv(h[i + 1]); if (a < 0 || b < 0) break; out.push_back(static_cast<char>(a * 16 + b)); }
    return out;
}
static Value revive(const Value& v, int depth = 0) {
    if (depth > 200) return v;
    if (v.type == Value::Type::ARRAY && v.array) {
        ArrayData items; items.reserve(v.array->size());
        for (const auto& x : *v.array) items.push_back(revive(x, depth + 1));
        return newArray(std::move(items));
    }
    if (v.type == Value::Type::MAP && v.map) {
        if (v.map->size() == 1) {
            const auto& kv = (*v.map)[0];
            std::string k = kv.first.toDisplayString();
            if (k == "$set" && kv.second.type == Value::Type::ARRAY && kv.second.array) {
                auto sd = std::make_shared<SetData>();
                for (const auto& x : *kv.second.array) sd->add(revive(x, depth + 1));
                return Value::makeSet(sd);
            }
            if (k == "$bytes" && kv.second.type == Value::Type::STRING) return Value::string(unhex(kv.second.str));
        }
        MapData m;
        for (const auto& kv : *v.map) m.push_back({kv.first, revive(kv.second, depth + 1)});
        return newMap(std::move(m));
    }
    return v;
}
static std::string sanitizeFile(std::string s) {
    for (auto& c : s) { unsigned char u = static_cast<unsigned char>(c); if (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_'; }
    if (s.empty() || s[0] == '.') s.insert(s.begin(), '_');
    if (s.size() > 120) s.resize(120);
    return s;
}
static std::string hex8(uint32_t v) { char b[16]; snprintf(b, sizeof b, "%08x", v); return b; }

struct Counts { size_t files = 0, dirs = 0; uint64_t original = 0; };

} // namespace czip

void Interpreter::registerNativesContainerZip() {
    using namespace czip;
    using Args = std::vector<Value>;

    // ------------------------------------------------------------ ملفات القرص
    auto readDisk = [this](const std::string& path, int line, const std::string& who, uint64_t maxSize) -> std::string {
        std::string full = resolvePath(path, line);
        std::error_code ec;
        if (!fs::is_regular_file(full, ec)) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": الملف غير موجود '" + path + "'");
        uint64_t sz = fs::file_size(full, ec);
        if (!ec && sz > maxSize) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": الملف '" + path + "' أكبر من الحد المسموح (" + std::to_string(maxSize) + " بايت)");
        std::ifstream in(full, std::ios::binary);
        if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر فتح '" + path + "'");
        std::ostringstream buf; buf << in.rdbuf();
        return buf.str();
    };
    // كتابة ذرّية: ملف مؤقت ثم rename — لا يبقى أرشيف نصف مكتوب عند الفشل
    auto writeDisk = [this](const std::string& path, const std::string& bytes, int line, const std::string& who) {
        std::string full = resolvePath(path, line);
        ensureParentDir(full);
        std::string tmp = full + ".rin-tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر إنشاء '" + path + "' للكتابة");
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.flush();
            if (!out.good()) { std::error_code ec; fs::remove(tmp, ec); throw diagErr(diag::Code::E0036_IOFailure, line, who + ": فشلت الكتابة إلى '" + path + "' (المساحة؟)"); }
        }
        std::error_code ec;
        fs::rename(tmp, full, ec);
        if (ec) { fs::remove(full, ec); std::error_code ec2; fs::rename(tmp, full, ec2); if (ec2) { fs::remove(tmp, ec2); throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر استبدال '" + path + "'"); } }
    };
    auto limitsOf = [](const Value* opts) {
        zipx::Limits l;
        double v;
        if ((v = optNum(opts, "maxFiles", 0)) > 0) l.maxEntries = static_cast<size_t>(v);
        if ((v = optNum(opts, "maxFileSize", 0)) > 0) l.maxEntrySize = static_cast<uint64_t>(v);
        if ((v = optNum(opts, "maxSize", 0)) > 0) l.maxTotal = static_cast<uint64_t>(v);
        return l;
    };
    auto parseBytes = [limitsOf](const std::string& bytes, const Value* opts, int line, const std::string& who, const std::string& label) -> std::shared_ptr<Reader> {
        try { return std::make_shared<Reader>(bytes, limitsOf(opts)); }
        catch (const ZipError& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": " + e.what() + (label.empty() ? "" : " ('" + label + "')")); }
    };
    auto openZip = [this, readDisk, parseBytes](const std::string& path, const Value* opts, int line, const std::string& who) -> std::shared_ptr<Reader> {
        return parseBytes(readDisk(path, line, who, 2000ull << 20), opts, line, who, path);
    };
    // ينفّذ f ويحوّل ZipError إلى خطأ Rin واضح
    auto guard = [](int line, const std::string& who, auto f) -> decltype(f()) {
        try { return f(); }
        catch (const ZipError& e) { throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": " + e.what()); }
    };

    // ------------------------------------------------------------ جمع المصادر وإضافتها لكاتب
    auto addTree = [this](Writer& w, const std::string& rawPath, const std::string& prefix, const std::vector<std::string>& excludes,
                          const Writer::Add& base, bool useDiskTime, uint64_t maxFile, Counts& cnt, int line, const std::string& who) {
        std::string full = resolvePath(rawPath, line);
        std::error_code ec;
        if (!fs::exists(full, ec)) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": المسار غير موجود '" + rawPath + "'");
        auto statOf = [&](const std::string& p, time_t& mt, uint32_t& mode) {
            RIN_STAT_T st{};
            if (RIN_STAT(p.c_str(), &st) == 0) { if (useDiskTime) mt = static_cast<time_t>(st.st_mtime); mode = static_cast<uint32_t>(st.st_mode & 0777); }
        };
        auto addOne = [&](const std::string& diskFull, const std::string& entryName, const std::string& displayPath) {
            std::error_code e2;
            uint64_t sz = fs::file_size(diskFull, e2);
            if (!e2 && sz > maxFile) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": الملف '" + displayPath + "' أكبر من الحد المسموح (" + std::to_string(maxFile) + " بايت)");
            std::ifstream in(diskFull, std::ios::binary);
            if (!in) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر قراءة '" + displayPath + "'");
            std::ostringstream buf; buf << in.rdbuf();
            Writer::Add o = base;
            statOf(diskFull, o.mtime, o.mode);
            if (o.mode == 0) o.mode = 0644;
            std::string content = buf.str();
            w.addFile(entryName, content, o);
            cnt.files++; cnt.original += content.size();
        };
        if (fs::is_symlink(fs::symlink_status(full, ec))) return; // لا نتبع الروابط الرمزية أبداً
        if (fs::is_regular_file(full, ec)) {
            std::string nm = fs::path(rawPath).filename().generic_string();
            if (nm.empty()) nm = "file";
            std::string entry = prefix + nm;
            if (!matchAny(excludes, nm) && !matchAny(excludes, entry)) addOne(full, entry, rawPath);
            return;
        }
        if (!fs::is_directory(full, ec)) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": '" + rawPath + "' ليس ملفاً ولا مجلداً");
        fs::path lex = fs::path(rawPath).lexically_normal();
        std::string top = lex.filename().generic_string();
        if (top == "." || top == ".." || top.empty()) top = ""; // "." = محتوى المجلد عند جذر الأرشيف
        std::string base2 = prefix + (top.empty() ? "" : top + "/");
        std::vector<std::pair<std::string, bool>> items; // (rel, isDir)
        for (auto it = fs::recursive_directory_iterator(full, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code e3;
            if (fs::is_symlink(it->symlink_status(e3))) { it.disable_recursion_pending(); continue; }
            std::string rel = fs::relative(it->path(), full, e3).generic_string();
            if (e3 || rel.empty()) continue;
            if (it->is_directory(e3)) items.push_back({rel, true});
            else if (it->is_regular_file(e3)) items.push_back({rel, false});
        }
        std::sort(items.begin(), items.end());
        if (!top.empty() && !matchAny(excludes, top)) { w.addDir(prefix + top, base.mtime); cnt.dirs++; }
        for (const auto& [rel, isDir] : items) {
            std::string entry = base2 + rel;
            if (matchAny(excludes, rel) || matchAny(excludes, entry)) continue;
            // استبعاد ما تحت مجلد مستبعد
            bool under = false;
            for (const auto& ex : excludes) { std::string d = ex; if (!d.empty() && d.back() != '/') d += '/'; if (rel.compare(0, d.size(), d) == 0) { under = true; break; } }
            if (under) continue;
            if (isDir) { w.addDir(entry, base.mtime); cnt.dirs++; }
            else addOne((fs::path(full) / rel).string(), entry, rawPath + "/" + rel);
        }
    };

    // src: نص (مسار) | مصفوفة مسارات | map اسم_داخل_الأرشيف → محتوى
    auto addSources = [this, addTree](Writer& w, const Value& src, const Value* opts, const std::string& prefix, Counts& cnt, int line, const std::string& who) {
        Writer::Add base;
        double lv = optNum(opts, "level", 6);
        if (lv < 0 || lv > 9) throw diagErr(diag::Code::E0007_InvalidArguments, line, who + ": level يجب أن يكون بين 0 و9");
        base.level = static_cast<int>(lv);
        base.password = optStr(opts, "password");
        bool fixedTime = optv(opts, "time") != nullptr;
        base.mtime = fixedTime ? static_cast<time_t>(optNum(opts, "time", 0)) : 0;
        std::vector<std::string> excludes = strList(optv(opts, "exclude"), who, "exclude", line);
        uint64_t maxFile = static_cast<uint64_t>(optNum(opts, "maxFileSize", 256.0 * 1024 * 1024));
        auto putContent = [&](const std::string& name, const Value& content) {
            std::string data;
            if (content.type == Value::Type::STRING) data = content.str;
            else if (content.type == Value::Type::NIL) data = "";
            else if (content.type == Value::Type::NUMBER || content.type == Value::Type::BOOL) data = content.toDisplayString();
            else { encodeTyped(content, data); }
            w.addFile(prefix + name, data, base);
            cnt.files++; cnt.original += data.size();
        };
        if (src.type == Value::Type::MAP && src.map) {
            for (const auto& kv : *src.map) {
                std::string nm = kv.first.toDisplayString();
                if (!nm.empty() && nm.back() == '/') { w.addDir(prefix + nm, base.mtime); cnt.dirs++; }
                else putContent(nm, kv.second);
            }
            return;
        }
        std::vector<std::string> paths = strList(&src, who, "المصدر", line);
        for (const auto& p : paths) addTree(w, p, prefix, excludes, base, !fixedTime, maxFile, cnt, line, who);
    };

    auto entryMap = [](const Entry& e) {
        return mkMap({{"name", strV(e.name)}, {"size", numV(static_cast<double>(e.usize))}, {"packed", numV(static_cast<double>(e.csize))},
                      {"method", strV(e.method == 0 ? "store" : e.method == 8 ? "deflate" : "method" + std::to_string(e.method))},
                      {"ratio", numV(e.usize ? std::round(1000.0 * double(e.csize) / double(e.usize)) / 1000.0 : 0.0)},
                      {"crc", strV(hex8(e.crc))}, {"dir", boolV(e.isDir())}, {"encrypted", boolV(e.encrypted())},
                      {"time", numV(static_cast<double>(zipx::detail::fromDos(e.dosTime, e.dosDate)))}, {"comment", strV(e.comment)}});
    };
    auto statsMap = [](const std::string& path, uint64_t archiveBytes, const Counts& c, double extra = -1) {
        double ratio = c.original ? std::round(1000.0 * double(archiveBytes) / double(c.original)) / 1000.0 : 0.0;
        double saved = c.original ? std::round(1000.0 * (1.0 - double(archiveBytes) / double(c.original))) / 10.0 : 0.0;
        (void)extra;
        return mkMap({{"ok", boolV(true)}, {"path", strV(path)}, {"files", numV(double(c.files))}, {"dirs", numV(double(c.dirs))},
                      {"bytes", numV(double(archiveBytes))}, {"originalBytes", numV(double(c.original))}, {"ratio", numV(ratio)}, {"saved", numV(saved)}});
    };
    // يفكّ ملفاً واحداً (مع كلمة المرور من opts)
    auto readEntry = [guard](const Reader& r, const std::string& name, const Value* opts, int line, const std::string& who) -> std::string {
        std::string nm;
        try { nm = zipx::safeName(name); } catch (const ZipError&) { nm = name; }
        const Entry* e = r.find(nm);
        if (!e) {
            std::string avail; size_t k = 0;
            for (const auto& x : r.entries()) { if (k++ >= 6) { avail += "، ..."; break; } avail += (avail.empty() ? "" : "، ") + x.name; }
            throw diagErr(diag::Code::E0036_IOFailure, line, who + ": '" + name + "' غير موجود في الأرشيف (المتاح: " + avail + ")");
        }
        return guard(line, who, [&] { return r.read(*e, optStr(opts, "password")); });
    };

    // ------------------------------------------------------------ الاستخراج إلى مجلد
    auto extractAll = [this, guard](const Reader& r, const std::string& destRaw, const std::vector<std::string>& only, const std::string& stripPrefix,
                                    bool overwrite, int strip, const std::string& password, int line, const std::string& who) -> Value {
        std::string destFull = resolvePath(destRaw, line);
        std::error_code ec;
        fs::create_directories(destFull, ec);
        if (ec) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر إنشاء المجلد '" + destRaw + "'");
        fs::path root = fs::weakly_canonical(destFull, ec);
        ArrayData written, skipped;
        uint64_t bytes = 0; size_t files = 0, dirs = 0;
        // فحص مسبق: أي اسم غير آمن يُلغي الاستخراج كاملاً قبل كتابة أي شيء
        std::vector<std::pair<const Entry*, std::string>> plan;
        for (const Entry& e : r.entries()) {
            std::string nm;
            try { nm = zipx::safeName(e.name); }
            catch (const ZipError& ex) { throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": الأرشيف يحوي مساراً غير آمن — تم رفض الاستخراج كاملاً (" + ex.what() + ")"); }
            if (!stripPrefix.empty()) { if (nm.compare(0, stripPrefix.size(), stripPrefix) != 0) continue; nm = nm.substr(stripPrefix.size()); if (nm.empty()) continue; }
            if (!only.empty() && !matchAny(only, nm)) continue;
            for (int s = 0; s < strip && !nm.empty(); ++s) { size_t p = nm.find('/'); if (p == std::string::npos) { nm.clear(); break; } nm = nm.substr(p + 1); }
            if (nm.empty()) continue;
            plan.push_back({&e, nm});
        }
        for (const auto& [ep, nm] : plan) {
            const Entry& e = *ep;
            std::string target = destRaw;
            if (!target.empty() && target.back() != '/') target += '/';
            target += nm;
            std::string full = resolvePath(target, line);
            fs::path parent = fs::path(full).parent_path();
            fs::path canonParent = fs::weakly_canonical(parent, ec);
            auto inside = [&](const fs::path& p) { auto a = root.generic_string(), b = p.generic_string(); return b.size() >= a.size() && b.compare(0, a.size(), a) == 0 && (b.size() == a.size() || b[a.size()] == '/' || (!a.empty() && a.back() == '/')); };
            if (!inside(canonParent)) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": رفض الكتابة خارج المجلد الهدف (رابط رمزي؟): '" + nm + "'");
            if (e.isDir()) { fs::create_directories(full, ec); dirs++; continue; }
            if (!overwrite && fs::exists(full, ec)) { skipped.push_back(mkMap({{"name", strV(nm)}, {"reason", strV("exists")}})); continue; }
            std::string data = guard(line, who, [&] { return r.read(e, password); });
            fs::create_directories(parent, ec);
            std::ofstream out(full, std::ios::binary | std::ios::trunc);
            if (!out) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": تعذّر إنشاء '" + nm + "'");
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!out.good()) throw diagErr(diag::Code::E0036_IOFailure, line, who + ": فشلت كتابة '" + nm + "'");
            bytes += data.size(); files++;
            written.push_back(strV(nm));
        }
        return mkMap({{"ok", boolV(true)}, {"dest", strV(destRaw)}, {"files", numV(double(files))}, {"dirs", numV(double(dirs))},
                      {"bytes", numV(double(bytes))}, {"names", newArray(std::move(written))}, {"skipped", newArray(std::move(skipped))}});
    };

    // ============================================================ zip.* — أرشيفات عامة
    // zip.create(path, source, opts?) — ينشئ أرشيفاً جديداً (يستبدل الموجود).
    //   source: مسار | [مسارات ملفات/مجلدات] | {اسم_في_الأرشيف: محتوى}
    //   opts: level(0-9) password comment time exclude:[glob] maxFileSize
    natives["zip.create"] = [this, addSources, writeDisk, statsMap, guard](Args& a, int line) -> Value {
        need("zip.create", a, 2, 3, line);
        std::string path = str(a[0], "zip.create", line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        Writer w; Counts cnt;
        return guard(line, "zip.create", [&] {
            addSources(w, a[1], opts, "", cnt, line, "zip.create");
            std::string cm = optStr(opts, "comment");
            if (!cm.empty()) w.setComment(cm);
            std::string bytes = w.finish();
            writeDisk(path, bytes, line, "zip.create");
            return statsMap(path, bytes.size(), cnt);
        });
    };
    // zip.add(path, source, opts?) — يضيف/يستبدل ملفات داخل أرشيف موجود (يُنشأ إن غاب). الملفات الأخرى تُنسخ بلا إعادة ضغط.
    natives["zip.add"] = [this, addSources, writeDisk, readDisk, parseBytes, statsMap, guard](Args& a, int line) -> Value {
        need("zip.add", a, 2, 3, line);
        std::string path = str(a[0], "zip.add", line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        std::error_code ec;
        Writer w; Counts cnt;
        return guard(line, "zip.add", [&] {
            std::shared_ptr<Reader> old;
            if (fs::is_regular_file(resolvePath(path, line), ec)) old = parseBytes(readDisk(path, line, "zip.add", 2000ull << 20), opts, line, "zip.add", path);
            Writer fresh; Counts c2;
            addSources(fresh, a[1], opts, optStr(opts, "prefix"), c2, line, "zip.add");
            // الجديد يستبدل القديم بالاسم
            std::vector<std::string> newNames;
            Writer out;
            if (old) {
                // نبني قائمة بأسماء الجديد عبر قارئ مؤقت
                Reader tmp(fresh.finish());
                for (const Entry& e : tmp.entries()) newNames.push_back(e.name);
                for (const Entry& e : old->entries()) if (std::find(newNames.begin(), newNames.end(), e.name) == newNames.end()) out.addRaw(*old, e);
                for (const Entry& e : tmp.entries()) out.addRaw(tmp, e);
                out.setComment(old->comment());
            } else {
                Reader tmp(fresh.finish());
                for (const Entry& e : tmp.entries()) out.addRaw(tmp, e);
            }
            std::string cm = optStr(opts, "comment");
            if (!cm.empty()) out.setComment(cm);
            std::string bytes = out.finish();
            writeDisk(path, bytes, line, "zip.add");
            Value m = statsMap(path, bytes.size(), c2);
            m.map->put(strV("entries"), numV(double(out.count())));
            return m;
        });
    };
    // zip.list(path, opts?) — مصفوفة {name,size,packed,method,ratio,crc,dir,encrypted,time,comment}
    natives["zip.list"] = [openZip, entryMap](Args& a, int line) -> Value {
        need("zip.list", a, 1, 2, line);
        auto r = openZip(str(a[0], "zip.list", line), a.size() > 1 ? &a[1] : nullptr, line, "zip.list");
        ArrayData out;
        for (const Entry& e : r->entries()) out.push_back(entryMap(e));
        return newArray(std::move(out));
    };
    // zip.find(path, pattern | [patterns]) — أسماء مطابقة (glob: * ?  أو بادئة مجلد)
    natives["zip.find"] = [openZip](Args& a, int line) -> Value {
        need("zip.find", a, 2, 2, line);
        auto r = openZip(str(a[0], "zip.find", line), nullptr, line, "zip.find");
        auto pats = strList(&a[1], "zip.find", "النمط", line);
        ArrayData out;
        for (const Entry& e : r->entries()) if (matchAny(pats, e.name)) out.push_back(strV(e.name));
        return newArray(std::move(out));
    };
    // zip.exists(path, entry)
    natives["zip.exists"] = [openZip](Args& a, int line) -> Value {
        need("zip.exists", a, 2, 2, line);
        auto r = openZip(str(a[0], "zip.exists", line), nullptr, line, "zip.exists");
        std::string nm = str(a[1], "zip.exists", line);
        try { nm = zipx::safeName(nm); } catch (const ZipError&) {}
        return boolV(r->find(nm) != nullptr);
    };
    // zip.info(path) — ملخص الأرشيف
    natives["zip.info"] = [openZip](Args& a, int line) -> Value {
        need("zip.info", a, 1, 2, line);
        std::string path = str(a[0], "zip.info", line);
        auto r = openZip(path, a.size() > 1 ? &a[1] : nullptr, line, "zip.info");
        size_t files = 0, dirs = 0, enc = 0, nStore = 0, nDef = 0; uint64_t u = 0, c = 0;
        for (const Entry& e : r->entries()) {
            if (e.isDir()) { dirs++; continue; }
            files++; u += e.usize; c += e.csize; if (e.encrypted()) enc++;
            (e.method == 0 ? nStore : nDef)++;
        }
        return mkMap({{"path", strV(path)}, {"entries", numV(double(r->entries().size()))}, {"files", numV(double(files))}, {"dirs", numV(double(dirs))},
                      {"size", numV(double(r->byteSize()))}, {"uncompressed", numV(double(u))}, {"compressed", numV(double(c))},
                      {"ratio", numV(u ? std::round(1000.0 * double(r->byteSize()) / double(u)) / 1000.0 : 0.0)},
                      {"saved", numV(u ? std::round(1000.0 * (1.0 - double(r->byteSize()) / double(u))) / 10.0 : 0.0)},
                      {"encrypted", numV(double(enc))}, {"store", numV(double(nStore))}, {"deflate", numV(double(nDef))}, {"comment", strV(r->comment())}});
    };
    // zip.read(path, entry, opts?) — محتوى ملف كنص. opts.password
    natives["zip.read"] = [openZip, readEntry](Args& a, int line) -> Value {
        need("zip.read", a, 2, 3, line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        auto r = openZip(str(a[0], "zip.read", line), opts, line, "zip.read");
        return strV(readEntry(*r, str(a[1], "zip.read", line), opts, line, "zip.read"));
    };
    // zip.readJson(path, entry, opts?) — يقرأ ملف JSON ويحوّله إلى قيمة Rin
    natives["zip.readJson"] = [openZip, readEntry](Args& a, int line) -> Value {
        need("zip.readJson", a, 2, 3, line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        auto r = openZip(str(a[0], "zip.readJson", line), opts, line, "zip.readJson");
        return revive(json::decodeOrRaw(readEntry(*r, str(a[1], "zip.readJson", line), opts, line, "zip.readJson")));
    };
    // zip.extract(path, dest, opts?) — يستخرج إلى مجلد. opts: only:[glob] overwrite(true) strip(N) password
    natives["zip.extract"] = [openZip, extractAll](Args& a, int line) -> Value {
        need("zip.extract", a, 2, 3, line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        auto r = openZip(str(a[0], "zip.extract", line), opts, line, "zip.extract");
        auto only = strList(optv(opts, "only"), "zip.extract", "only", line);
        return extractAll(*r, str(a[1], "zip.extract", line), only, "", optBool(opts, "overwrite", true),
                          static_cast<int>(optNum(opts, "strip", 0)), optStr(opts, "password"), line, "zip.extract");
    };
    // zip.remove(path, pattern | [patterns]) → {ok, removed, remaining}
    natives["zip.remove"] = [openZip, writeDisk, guard](Args& a, int line) -> Value {
        need("zip.remove", a, 2, 2, line);
        std::string path = str(a[0], "zip.remove", line);
        auto pats = strList(&a[1], "zip.remove", "النمط", line);
        auto r = openZip(path, nullptr, line, "zip.remove");
        return guard(line, "zip.remove", [&] {
            Writer w; size_t removed = 0;
            for (const Entry& e : r->entries()) { if (matchAny(pats, e.name)) { removed++; continue; } w.addRaw(*r, e); }
            w.setComment(r->comment());
            if (removed) writeDisk(path, w.finish(), line, "zip.remove");
            return mkMap({{"ok", boolV(true)}, {"removed", numV(double(removed))}, {"remaining", numV(double(w.count()))}});
        });
    };
    // zip.rename(path, from, to) — إعادة تسمية ملف (أو مجلد بكل محتواه) بلا إعادة ضغط
    natives["zip.rename"] = [openZip, writeDisk, guard](Args& a, int line) -> Value {
        need("zip.rename", a, 3, 3, line);
        std::string path = str(a[0], "zip.rename", line), from = str(a[1], "zip.rename", line), to = str(a[2], "zip.rename", line);
        auto r = openZip(path, nullptr, line, "zip.rename");
        return guard(line, "zip.rename", [&] {
            std::string f = zipx::safeName(from), t = zipx::safeName(to);
            bool isDirRename = !f.empty() && f.back() == '/';
            if (isDirRename && t.back() != '/') t += '/';
            Writer w; size_t n = 0;
            for (const Entry& e : r->entries()) {
                if (!isDirRename && e.name == f) { w.addRaw(*r, e, t); n++; }
                else if (isDirRename && e.name.compare(0, f.size(), f) == 0) { w.addRaw(*r, e, t + e.name.substr(f.size())); n++; }
                else w.addRaw(*r, e);
            }
            if (!n) throw ZipError("'" + from + "' غير موجود في الأرشيف");
            w.setComment(r->comment());
            writeDisk(path, w.finish(), line, "zip.rename");
            return mkMap({{"ok", boolV(true)}, {"renamed", numV(double(n))}});
        });
    };
    // zip.combine(dest, [paths]) — يدمج أرشيفات في واحد (اللاحق يستبدل السابق) بلا إعادة ضغط
    natives["zip.combine"] = [openZip, writeDisk, guard](Args& a, int line) -> Value {
        need("zip.combine", a, 2, 2, line);
        std::string dest = str(a[0], "zip.combine", line);
        auto srcs = strList(&a[1], "zip.combine", "الأرشيفات", line);
        if (srcs.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "zip.combine: لا أرشيفات للدمج");
        std::vector<std::shared_ptr<Reader>> readers;
        for (const auto& s : srcs) readers.push_back(openZip(s, nullptr, line, "zip.combine"));
        return guard(line, "zip.combine", [&] {
            Writer w;
            for (auto& r : readers) for (const Entry& e : r->entries()) w.addRaw(*r, e);
            std::string bytes = w.finish();
            writeDisk(dest, bytes, line, "zip.combine");
            return mkMap({{"ok", boolV(true)}, {"path", strV(dest)}, {"entries", numV(double(w.count()))}, {"bytes", numV(double(bytes.size()))}, {"sources", numV(double(srcs.size()))}});
        });
    };
    // zip.comment(path, text?) — يقرأ/يضبط تعليق الأرشيف
    natives["zip.comment"] = [openZip, writeDisk, guard](Args& a, int line) -> Value {
        need("zip.comment", a, 1, 2, line);
        std::string path = str(a[0], "zip.comment", line);
        auto r = openZip(path, nullptr, line, "zip.comment");
        if (a.size() == 1) return strV(r->comment());
        std::string text = str(a[1], "zip.comment", line);
        return guard(line, "zip.comment", [&] {
            Writer w;
            for (const Entry& e : r->entries()) w.addRaw(*r, e);
            w.setComment(text);
            writeDisk(path, w.finish(), line, "zip.comment");
            return boolV(true);
        });
    };
    // zip.test(path, opts?) → {ok, entries, checked, skipped, errors:[{name,error}]} — لا يرمي عند تلف ملف بل يبلّغ
    natives["zip.test"] = [openZip](Args& a, int line) -> Value {
        need("zip.test", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        std::string path = str(a[0], "zip.test", line);
        std::string pw = optStr(opts, "password");
        std::shared_ptr<Reader> r;
        try { r = openZip(path, opts, line, "zip.test"); }
        catch (const RinError& e) { // أرشيف تالف بنيوياً: نبلّغ لا نرمي
            return mkMap({{"ok", boolV(false)}, {"entries", numV(0)}, {"checked", numV(0)}, {"skipped", numV(0)},
                          {"errors", newArray({mkMap({{"name", strV("(archive)")}, {"error", strV(e.message)}})})}});
        }
        ArrayData errs; size_t checked = 0, skipped = 0;
        for (const Entry& e : r->entries()) {
            if (e.isDir()) continue;
            if (e.encrypted() && pw.empty()) { skipped++; continue; }
            try { r->read(e, pw); checked++; }
            catch (const ZipError& ex) { errs.push_back(mkMap({{"name", strV(e.name)}, {"error", strV(ex.what())}})); }
        }
        // أسماء مكرّرة / خطرة
        std::map<std::string, int> seen;
        for (const Entry& e : r->entries()) {
            if (++seen[e.name] == 2) errs.push_back(mkMap({{"name", strV(e.name)}, {"error", strV("اسم مكرّر داخل الأرشيف")}}));
            try { zipx::safeName(e.name); } catch (const ZipError& ex) { errs.push_back(mkMap({{"name", strV(e.name)}, {"error", strV(ex.what())}})); }
        }
        bool ok = errs.empty();
        return mkMap({{"ok", boolV(ok)}, {"entries", numV(double(r->entries().size()))}, {"checked", numV(double(checked))},
                      {"skipped", numV(double(skipped))}, {"errors", newArray(std::move(errs))}});
    };
    // zip.bytes(source, opts?) — أرشيف في الذاكرة (نص بايتات) بدل ملف
    natives["zip.bytes"] = [addSources, guard](Args& a, int line) -> Value {
        need("zip.bytes", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        return guard(line, "zip.bytes", [&] {
            Writer w; Counts cnt;
            addSources(w, a[0], opts, "", cnt, line, "zip.bytes");
            std::string cm = optStr(opts, "comment");
            if (!cm.empty()) w.setComment(cm);
            return strV(w.finish());
        });
    };
    // zip.unbytes(bytes, opts?) → map اسم → محتوى (الملفات فقط). opts.password / only
    natives["zip.unbytes"] = [parseBytes, guard](Args& a, int line) -> Value {
        need("zip.unbytes", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        auto r = parseBytes(str(a[0], "zip.unbytes", line), opts, line, "zip.unbytes", "");
        auto only = strList(optv(opts, "only"), "zip.unbytes", "only", line);
        MapData m;
        for (const Entry& e : r->entries()) {
            if (e.isDir() || (!only.empty() && !matchAny(only, e.name))) continue;
            std::string d = guard(line, "zip.unbytes", [&] { return r->read(e, optStr(opts, "password")); });
            m.push_back({strV(e.name), strV(d)});
        }
        return newMap(std::move(m));
    };
    // ---- gzip ----
    natives["zip.gzip"] = [guard](Args& a, int line) -> Value {
        need("zip.gzip", a, 1, 2, line);
        return guard(line, "zip.gzip", [&] { return strV(zipx::gzipCompress(str(a[0], "zip.gzip", line), a.size() > 1 ? static_cast<int>(num(a[1], "zip.gzip", line)) : 6)); });
    };
    natives["zip.gunzip"] = [guard](Args& a, int line) -> Value {
        need("zip.gunzip", a, 1, 1, line);
        return guard(line, "zip.gunzip", [&] { return strV(zipx::gzipDecompress(str(a[0], "zip.gunzip", line))); });
    };
    natives["zip.gzipFile"] = [readDisk, writeDisk, guard](Args& a, int line) -> Value {
        need("zip.gzipFile", a, 1, 3, line);
        std::string src = str(a[0], "zip.gzipFile", line);
        std::string dst = a.size() > 1 && a[1].type == Value::Type::STRING ? a[1].str : src + ".gz";
        int lvl = a.size() > 2 ? static_cast<int>(num(a[2], "zip.gzipFile", line)) : 6;
        std::string data = readDisk(src, line, "zip.gzipFile", 1024ull << 20);
        return guard(line, "zip.gzipFile", [&] {
            std::string z = zipx::gzipCompress(data, lvl);
            writeDisk(dst, z, line, "zip.gzipFile");
            return mkMap({{"ok", boolV(true)}, {"path", strV(dst)}, {"bytes", numV(double(z.size()))}, {"originalBytes", numV(double(data.size()))}});
        });
    };
    natives["zip.gunzipFile"] = [readDisk, writeDisk, guard](Args& a, int line) -> Value {
        need("zip.gunzipFile", a, 1, 2, line);
        std::string src = str(a[0], "zip.gunzipFile", line);
        std::string dst;
        if (a.size() > 1 && a[1].type == Value::Type::STRING) dst = a[1].str;
        else if (src.size() > 3 && src.compare(src.size() - 3, 3, ".gz") == 0) dst = src.substr(0, src.size() - 3);
        else dst = src + ".out";
        std::string data = readDisk(src, line, "zip.gunzipFile", 1024ull << 20);
        return guard(line, "zip.gunzipFile", [&] {
            std::string d = zipx::gzipDecompress(data);
            writeDisk(dst, d, line, "zip.gunzipFile");
            return mkMap({{"ok", boolV(true)}, {"path", strV(dst)}, {"bytes", numV(double(d.size()))}});
        });
    };

    // ============================================================ container.zip — الحاويات
    // لقطة حاوية واحدة كنص JSON مُنمَّط + عدّادات
    struct SnapInfo { std::string json, kind; size_t fields = 0, docs = 0, rows = 0; };
    auto snapshotOf = [this](const std::string& name, const Value* opts, int line) -> SnapInfo {
        auto it = containers.find(name);
        if (it == containers.end()) throw diagErr(diag::Code::E0014_InvalidContainer, line, "container.zip: الحاوية '" + name + "' غير موجودة");
        std::vector<std::string> only = strList(optv(opts, "only"), "container.zip", "only", line);
        std::vector<std::string> omit = strList(optv(opts, "omit"), "container.zip", "omit", line);
        SnapInfo si;
        { Args ka{Value::string(name)}; Value k = natives["container.kind"](ka, line); si.kind = k.type == Value::Type::STRING ? k.str : "container"; }
        std::vector<std::string> keys;
        for (const auto& kv : it->second->values) {
            if (kv.second.type == Value::Type::FUNCTION) continue;
            if (!only.empty() && std::find(only.begin(), only.end(), kv.first) == only.end()) continue;
            if (std::find(omit.begin(), omit.end(), kv.first) != omit.end()) continue;
            keys.push_back(kv.first);
        }
        std::sort(keys.begin(), keys.end());
        std::string j = "{\"name\":"; encStr(name, j); j += ",\"kind\":"; encStr(si.kind, j); j += ",\"fields\":{";
        for (size_t i = 0; i < keys.size(); ++i) { if (i) j += ','; encKey(keys[i], j); j += ':'; encodeTyped(it->second->values[keys[i]], j); }
        j += '}';
        si.fields = keys.size();
        auto ds = docStore.find(name);
        if (ds != docStore.end() && !ds->second.empty() && !optBool(opts, "noDocuments", false)) {
            j += ",\"documents\":[";
            for (size_t i = 0; i < ds->second.size(); ++i) { if (i) j += ','; j += "{\"id\":"; encStr(ds->second[i].first, j); j += ",\"doc\":"; encodeTyped(ds->second[i].second, j); j += '}'; }
            j += ']';
            si.docs = ds->second.size();
        }
        if (isTableContainer(name)) {
            auto tr = tableRows.find(name);
            if (tr != tableRows.end() && tr->second && !tr->second->empty()) {
                j += ",\"table\":"; encodeTyped(Value::makeArray(tr->second), j);
                si.rows = tr->second->size();
            }
        }
        auto st = containerStyles.find(name);
        if (st != containerStyles.end() && !st->second.empty()) { j += ",\"style\":"; encStr(st->second, j); }
        j += '}';
        si.json = std::move(j);
        return si;
    };

    // يبني أرشيف حاويات كاملاً (manifest + containers/*.json + files/*)
    auto buildArchive = [this, snapshotOf, addSources](const std::vector<std::string>& names, const Value* opts, int line, const std::string& who,
                                                       Counts& cnt, ArrayData& infos) -> std::string {
        Writer w;
        Writer::Add base;
        double lv = optNum(opts, "level", 6);
        if (lv < 0 || lv > 9) throw diagErr(diag::Code::E0007_InvalidArguments, line, who + ": level يجب أن يكون بين 0 و9");
        base.level = static_cast<int>(lv);
        base.password = optStr(opts, "password");
        time_t now = optv(opts, "time") ? static_cast<time_t>(optNum(opts, "time", 0)) : std::time(nullptr);
        base.mtime = now;
        std::string manifest = "{\"format\":\"rin-container-zip\",\"version\":1,\"rin\":";
#ifdef RIN_VERSION_STRING
        encStr(RIN_VERSION_STRING, manifest);
#else
        encStr("", manifest);
#endif
        manifest += ",\"created\":" + std::to_string(static_cast<long long>(now)) + ",\"containers\":[";
        std::map<std::string, int> used;
        size_t idx = 0;
        for (const auto& nm : names) {
            SnapInfo si = snapshotOf(nm, opts, line);
            std::string fn = sanitizeFile(nm);
            int n = ++used[fn];
            if (n > 1) fn += "~" + std::to_string(n);
            std::string file = "containers/" + fn + ".json";
            w.addFile(file, si.json, base);
            cnt.files++; cnt.original += si.json.size();
            if (idx++) manifest += ',';
            manifest += "{\"name\":"; encStr(nm, manifest); manifest += ",\"kind\":"; encStr(si.kind, manifest);
            manifest += ",\"file\":"; encStr(file, manifest);
            manifest += ",\"fields\":" + std::to_string(si.fields) + ",\"documents\":" + std::to_string(si.docs) + ",\"rows\":" + std::to_string(si.rows) + "}";
            infos.push_back(mkMap({{"name", strV(nm)}, {"kind", strV(si.kind)}, {"fields", numV(double(si.fields))}, {"documents", numV(double(si.docs))}, {"rows", numV(double(si.rows))}}));
        }
        manifest += "]";
        // ملفات مرفقة: opts.include (مسارات) و opts.dir (مجلد)
        Counts extra;
        Value inc = optv(opts, "include") ? *optv(opts, "include") : Value::nil();
        Value dirv = optv(opts, "dir") ? *optv(opts, "dir") : Value::nil();
        if (inc.type != Value::Type::NIL) addSources(w, inc, opts, "files/", extra, line, who);
        if (dirv.type != Value::Type::NIL) addSources(w, dirv, opts, "files/", extra, line, who);
        cnt.files += extra.files; cnt.dirs += extra.dirs; cnt.original += extra.original;
        manifest += ",\"files\":" + std::to_string(extra.files) + "}";
        w.addFile("rin-container.json", manifest, base); // آخر ملف؛ القراءة تعتمد الفهرس المركزي لا الترتيب
        std::string cm = optStr(opts, "comment");
        w.setComment(cm.empty() ? "Rin container archive" : cm);
        cnt.files++; cnt.original += manifest.size();
        return w.finish();
    };

    struct ManifestItem { std::string name, kind, file; };
    // يقرأ manifest الأرشيف
    auto readManifest = [guard](const Reader& r, const Value* opts, int line, const std::string& who, std::vector<ManifestItem>& items, Value& whole) {
        const Entry* me = r.find("rin-container.json");
        if (!me) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": ليس أرشيف حاويات Rin (rin-container.json مفقود) — استعمل zip.* للأرشيفات العامة");
        std::string txt = guard(line, who, [&] { return r.read(*me, optStr(opts, "password")); });
        whole = json::decodeOrRaw(txt);
        if (whole.type != Value::Type::MAP || !whole.map) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": rin-container.json تالف");
        const Value* fmt = whole.map->findValue(Value::string("format"));
        if (!fmt || fmt->type != Value::Type::STRING || fmt->str != "rin-container-zip") throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": صيغة manifest غير معروفة");
        const Value* ver = whole.map->findValue(Value::string("version"));
        if (ver && ver->type == Value::Type::NUMBER && ver->number > 1) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": الأرشيف بإصدار أحدث (" + std::to_string(static_cast<int>(ver->number)) + ") من المدعوم — حدّث Rin");
        const Value* cs = whole.map->findValue(Value::string("containers"));
        if (cs && cs->type == Value::Type::ARRAY && cs->array) for (const auto& c : *cs->array) {
            if (c.type != Value::Type::MAP || !c.map) continue;
            ManifestItem mi;
            if (auto* v = c.map->findValue(Value::string("name")); v && v->type == Value::Type::STRING) mi.name = v->str;
            if (auto* v = c.map->findValue(Value::string("kind")); v && v->type == Value::Type::STRING) mi.kind = v->str;
            if (auto* v = c.map->findValue(Value::string("file")); v && v->type == Value::Type::STRING) mi.file = v->str;
            if (!mi.name.empty() && !mi.file.empty()) items.push_back(mi);
        }
    };

    // يستعيد حاوية من لقطتها
    auto restoreContainer = [this, guard](const std::string& target, const ManifestItem& mi, const Reader& r, const Value* opts, int line, const std::string& who) -> Value {
        bool overwrite = optBool(opts, "overwrite", true);
        std::string txt = guard(line, who, [&] { return r.read(*r.find(mi.file), optStr(opts, "password")); });
        Value snap = json::decodeOrRaw(txt);
        if (snap.type != Value::Type::MAP || !snap.map) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": ملف الحاوية '" + mi.file + "' تالف");
        std::string kind = mi.kind;
        if (auto* v = snap.map->findValue(Value::string("kind")); v && v->type == Value::Type::STRING && !v->str.empty()) kind = v->str;
        bool created = false;
        if (!containers.count(target)) {
            Args sp{Value::string(kind.empty() ? "container" : kind), Value::string(target)};
            natives["spawn"](sp, line);
            created = true;
        }
        auto env = containers[target];
        size_t nf = 0, nd = 0, nr = 0;
        if (auto* fv = snap.map->findValue(Value::string("fields")); fv && fv->type == Value::Type::MAP && fv->map) {
            Value rv = revive(*fv);
            for (const auto& kv : *rv.map) {
                std::string key = kv.first.toDisplayString();
                if (!overwrite && env->values.count(key)) continue;
                env->values[key] = kv.second; nf++;
            }
        }
        if (auto* dv = snap.map->findValue(Value::string("documents")); dv && dv->type == Value::Type::ARRAY && dv->array) {
            Value rv = revive(*dv);
            auto& store = docStore[target];
            if (overwrite) store.clear();
            for (const auto& d : *rv.array) {
                if (d.type != Value::Type::MAP || !d.map) continue;
                const Value* id = d.map->findValue(Value::string("id"));
                const Value* doc = d.map->findValue(Value::string("doc"));
                if (!id || !doc) continue;
                std::string sid = id->toDisplayString();
                bool exists = false;
                for (auto& p : store) if (p.first == sid) { exists = true; if (overwrite) p.second = *doc; break; }
                if (!exists) store.push_back({sid, *doc});
                nd++;
            }
            if (isDatalistContainer(target)) datalistSync(target);
        }
        if (auto* tv = snap.map->findValue(Value::string("table")); tv && tv->type == Value::Type::ARRAY && tv->array) {
            bool has = tableRows.count(target) && tableRows[target] && !tableRows[target]->empty();
            if (overwrite || !has) {
                Value rv = revive(*tv);
                tableRows[target] = std::make_shared<ArrayData>(*rv.array);
                nr = rv.array->size();
            }
        }
        if (auto* sv = snap.map->findValue(Value::string("style")); sv && sv->type == Value::Type::STRING && (overwrite || !containerStyles.count(target))) containerStyles[target] = sv->str;
        ArrayData issues;
        if (natives.count("validateContainer")) {
            try { Args va{Value::string(target)}; Value iv = natives["validateContainer"](va, line); if (iv.type == Value::Type::ARRAY && iv.array) issues = *iv.array; } catch (...) {}
        }
        return mkMap({{"ok", boolV(true)}, {"name", strV(target)}, {"from", strV(mi.name)}, {"kind", strV(kind)}, {"created", boolV(created)},
                      {"fields", numV(double(nf))}, {"documents", numV(double(nd))}, {"rows", numV(double(nr))}, {"issues", newArray(std::move(issues))}});
    };

    // الأسماء الفعلية للحاويات (تحويل easy_key/مسار إلى المفتاح الداخلي)
    auto namesArg = [this](const Value& v, const std::string& fn, int line) {
        std::vector<std::string> out;
        auto one = [&](const Value& x) {
            if (x.type != Value::Type::STRING) throw diagErr(diag::Code::E0004_InvalidType, line, "'" + fn + "' يتوقع اسم حاوية (نص) أو مصفوفة أسماء");
            std::string c = canonContainer(x.str);
            if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
        };
        if (v.type == Value::Type::ARRAY && v.array) for (const auto& x : *v.array) one(x); else one(v);
        if (out.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "'" + fn + "': لا حاويات للضغط");
        return out;
    };
    auto allNames = [this](const Value* opts) {
        std::string kind = optStr(opts, "kind");
        bool anon = optBool(opts, "anonymous", false);
        std::vector<std::string> out;
        for (const auto& kv : containers) {
            if (!anon && !kv.first.empty() && kv.first[0] == '#') continue;
            if (!kind.empty() && kind != "*") { Args ka{Value::string(kv.first)}; Value k = natives["container.kind"](ka, 0); if (!(k.type == Value::Type::STRING && k.str == kind)) continue; }
            out.push_back(kv.first);
        }
        std::sort(out.begin(), out.end());
        return out;
    };

    // container.zip(name | [names], path, opts?) — يضغط حاوية/حاويات في ملف ZIP حقيقي
    //   opts: level(0-9) password comment time only:[حقول] omit:[حقول] include:[ملفات] dir:"مجلد" exclude:[glob]
    //   يعيد {ok, path, containers:[{name,kind,fields,documents,rows}], files, bytes, originalBytes, ratio, saved}
    natives["container.zip"] = [this, namesArg, buildArchive, writeDisk, statsMap, guard](Args& a, int line) -> Value {
        need("container.zip", a, 2, 3, line);
        std::string path = str(a[1], "container.zip", line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        auto names = namesArg(a[0], "container.zip", line);
        return guard(line, "container.zip", [&] {
            Counts cnt; ArrayData infos;
            std::string bytes = buildArchive(names, opts, line, "container.zip", cnt, infos);
            writeDisk(path, bytes, line, "container.zip");
            Value m = statsMap(path, bytes.size(), cnt);
            m.map->put(strV("containers"), newArray(std::move(infos)));
            return m;
        });
    };
    natives["container.zipAll"] = [this, allNames, buildArchive, writeDisk, statsMap, guard](Args& a, int line) -> Value {
        need("container.zipAll", a, 1, 2, line);
        std::string path = str(a[0], "container.zipAll", line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        auto names = allNames(opts);
        if (names.empty()) throw diagErr(diag::Code::E0007_InvalidArguments, line, "container.zipAll: لا توجد حاويات مطابقة");
        return guard(line, "container.zipAll", [&] {
            Counts cnt; ArrayData infos;
            std::string bytes = buildArchive(names, opts, line, "container.zipAll", cnt, infos);
            writeDisk(path, bytes, line, "container.zipAll");
            Value m = statsMap(path, bytes.size(), cnt);
            m.map->put(strV("containers"), newArray(std::move(infos)));
            return m;
        });
    };
    // container.pack(name | [names], opts?) → بايتات الأرشيف (نص) بلا ملف
    natives["container.pack"] = [this, namesArg, buildArchive, guard](Args& a, int line) -> Value {
        need("container.pack", a, 1, 2, line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        auto names = namesArg(a[0], "container.pack", line);
        return guard(line, "container.pack", [&] {
            Counts cnt; ArrayData infos;
            return strV(buildArchive(names, opts, line, "container.pack", cnt, infos));
        });
    };

    // اختيار حاوية من الأرشيف
    auto pickItem = [](const std::vector<ManifestItem>& items, const std::string& want, const std::string& from, int line, const std::string& who) -> const ManifestItem& {
        if (items.empty()) throw diagErr(diag::Code::E0035_RuntimeError, line, who + ": الأرشيف لا يحوي أي حاوية");
        std::string key = from.empty() ? want : from;
        for (const auto& it : items) if (it.name == key) return it;
        if (from.empty() && items.size() == 1) return items[0]; // حاوية وحيدة → تُحمَّل في الاسم المطلوب
        std::string avail;
        for (const auto& it : items) avail += (avail.empty() ? "" : "، ") + it.name;
        throw diagErr(diag::Code::E0014_InvalidContainer, line, who + ": الحاوية '" + key + "' غير موجودة في الأرشيف (المتاح: " + avail + ") — استعمل opts.from");
    };
    // يستخرج الملفات المرفقة (files/*) إلى opts.extract
    auto extractAttached = [extractAll](const Reader& r, const Value* opts, int line, const std::string& who) -> Value {
        std::string dest = optStr(opts, "extract");
        if (dest.empty()) return Value::nil();
        return extractAll(r, dest, {}, "files/", optBool(opts, "overwrite", true), 0, optStr(opts, "password"), line, who);
    };

    // container.unzip(name, path, opts?) — يستعيد حاوية. opts: from:"اسم داخل الأرشيف" overwrite(true) password extract:"مجلد للملفات المرفقة"
    natives["container.unzip"] = [this, openZip, readManifest, pickItem, restoreContainer, extractAttached](Args& a, int line) -> Value {
        need("container.unzip", a, 2, 3, line);
        std::string name = canonContainer(str(a[0], "container.unzip", line));
        std::string path = str(a[1], "container.unzip", line);
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        auto r = openZip(path, opts, line, "container.unzip");
        std::vector<ManifestItem> items; Value whole;
        readManifest(*r, opts, line, "container.unzip", items, whole);
        const ManifestItem& mi = pickItem(items, name, optStr(opts, "from"), line, "container.unzip");
        if (!r->find(mi.file)) throw diagErr(diag::Code::E0035_RuntimeError, line, "container.unzip: ملف الحاوية '" + mi.file + "' مفقود من الأرشيف");
        Value rep = restoreContainer(name, mi, *r, opts, line, "container.unzip");
        rep.map->put(strV("path"), strV(path));
        Value ex = extractAttached(*r, opts, line, "container.unzip");
        if (ex.type == Value::Type::MAP && ex.map) rep.map->put(strV("extracted"), ex);
        return rep;
    };
    natives["container.unpack"] = [this, parseBytes, readManifest, pickItem, restoreContainer, extractAttached](Args& a, int line) -> Value {
        need("container.unpack", a, 2, 3, line);
        std::string name = canonContainer(str(a[0], "container.unpack", line));
        const Value* opts = a.size() > 2 ? &a[2] : nullptr;
        auto r = parseBytes(str(a[1], "container.unpack", line), opts, line, "container.unpack", "");
        std::vector<ManifestItem> items; Value whole;
        readManifest(*r, opts, line, "container.unpack", items, whole);
        const ManifestItem& mi = pickItem(items, name, optStr(opts, "from"), line, "container.unpack");
        if (!r->find(mi.file)) throw diagErr(diag::Code::E0035_RuntimeError, line, "container.unpack: ملف الحاوية '" + mi.file + "' مفقود من الأرشيف");
        Value rep = restoreContainer(name, mi, *r, opts, line, "container.unpack");
        Value ex = extractAttached(*r, opts, line, "container.unpack");
        if (ex.type == Value::Type::MAP && ex.map) rep.map->put(strV("extracted"), ex);
        return rep;
    };
    // container.unzipAll(path, opts?) — يستعيد كل الحاويات. opts: only:[أسماء] prefix:"p_" overwrite password extract
    natives["container.unzipAll"] = [this, openZip, readManifest, restoreContainer, extractAttached](Args& a, int line) -> Value {
        need("container.unzipAll", a, 1, 2, line);
        std::string path = str(a[0], "container.unzipAll", line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        auto r = openZip(path, opts, line, "container.unzipAll");
        std::vector<ManifestItem> items; Value whole;
        readManifest(*r, opts, line, "container.unzipAll", items, whole);
        auto only = strList(optv(opts, "only"), "container.unzipAll", "only", line);
        std::string prefix = optStr(opts, "prefix");
        ArrayData reps;
        for (const auto& mi : items) {
            if (!only.empty() && std::find(only.begin(), only.end(), mi.name) == only.end()) continue;
            if (!r->find(mi.file)) throw diagErr(diag::Code::E0035_RuntimeError, line, "container.unzipAll: ملف الحاوية '" + mi.file + "' مفقود من الأرشيف");
            reps.push_back(restoreContainer(prefix + mi.name, mi, *r, opts, line, "container.unzipAll"));
        }
        size_t n = reps.size();
        Value out = mkMap({{"ok", boolV(true)}, {"path", strV(path)}, {"count", numV(double(n))}, {"restored", newArray(std::move(reps))}});
        Value ex = extractAttached(*r, opts, line, "container.unzipAll");
        if (ex.type == Value::Type::MAP && ex.map) out.map->put(strV("extracted"), ex);
        return out;
    };
    // container.zipInfo(path | bytes-opts) — فهرس الأرشيف بلا استعادة
    natives["container.zipInfo"] = [openZip, readManifest](Args& a, int line) -> Value {
        need("container.zipInfo", a, 1, 2, line);
        std::string path = str(a[0], "container.zipInfo", line);
        const Value* opts = a.size() > 1 ? &a[1] : nullptr;
        auto r = openZip(path, opts, line, "container.zipInfo");
        std::vector<ManifestItem> items; Value whole;
        readManifest(*r, opts, line, "container.zipInfo", items, whole);
        ArrayData cs;
        const Value* mc = whole.map->findValue(Value::string("containers"));
        if (mc && mc->type == Value::Type::ARRAY && mc->array) for (const auto& c : *mc->array) cs.push_back(c);
        size_t attached = 0; uint64_t u = 0;
        for (const Entry& e : r->entries()) { if (!e.isDir()) u += e.usize; if (!e.isDir() && e.name.compare(0, 6, "files/") == 0) attached++; }
        auto g = [&](const char* k) { const Value* v = whole.map->findValue(Value::string(k)); return v ? *v : Value::nil(); };
        return mkMap({{"path", strV(path)}, {"format", g("format")}, {"version", g("version")}, {"rin", g("rin")}, {"created", g("created")},
                      {"containers", newArray(std::move(cs))}, {"attached", numV(double(attached))}, {"size", numV(double(r->byteSize()))},
                      {"uncompressed", numV(double(u))}, {"comment", strV(r->comment())}});
    };
}

} // namespace rin

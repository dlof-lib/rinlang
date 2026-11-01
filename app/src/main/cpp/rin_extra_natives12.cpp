// ============================================================================
//  rin_extra_natives12.cpp — دوال ملفات وقواعد بيانات: متانة وقفل وتسمية ذرّية ونطاق
// ----------------------------------------------------------------------------
//  يُضمَّن (#include) من rin_interpreter.cpp بعد rin_extra_natives11.cpp، ويُسجَّل عبر
//  Interpreter::registerNativesExtra12(). التوثيق: docs/FILEDB.md
//
//    fileSync(path)                   → true    fsync: يُلزم نظام التشغيل بكتابة الملف (أو المجلد) فعلياً على القرص
//    fileRename(from, to)             → true    إعادة تسمية ذرّية (تستبدل الوجهة إن وُجدت) — لنقطة تفتيش آمنة
//    fileSize(path)                   → number  حجم الملف بالبايت
//    fileLock(path)                   → bool    قفل حصري استشاري (flock/_locking) غير حاجب: true=أخذناه، false=عند غيرنا
//    fileUnlock(path)                 → bool    يحرر قفل هذه العملية (false إن لم تكن تحمله)
//    fileIsLocked(path)               → bool    هل الملف مقفول (بهذه العملية أو بعملية أخرى)؟ لا ينشئ الملف
//    findByRange(coll, field, lo, hi) → array   مستندات field فيها ضمن [lo, hi] مرتّبة تصاعدياً (nil = حدّ مفتوح)
//
//  ملاحظة: القفل استشاري (advisory): يمنع من يتعاون معه (مثل مكتبة rindb) لا أي برنامج. يحرره النظام تلقائياً عند
//  انتهاء العملية. على Windows يُستعمل _locking (لم يُختبر هناك).
// ============================================================================
#include "rin_interpreter.h"
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#ifdef _WIN32
#  include <io.h>
#  include <fcntl.h>
#  include <sys/locking.h>
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <sys/file.h>
#  include <unistd.h>
#endif

namespace rin {
namespace extra12 {
// أقفال هذه العملية: المسار الكامل → واصف الملف (يبقى مفتوحاً ما دام القفل مأخوذاً)
static std::unordered_map<std::string, int>& heldLocks() { static std::unordered_map<std::string, int> m; return m; }

static bool tryLockFd(int fd) {
#ifdef _WIN32
    _lseek(fd, 0, SEEK_SET);
    return _locking(fd, _LK_NBLCK, 1) == 0;
#else
    return ::flock(fd, LOCK_EX | LOCK_NB) == 0;
#endif
}
static void unlockAndClose(int fd) {
#ifdef _WIN32
    _lseek(fd, 0, SEEK_SET); _locking(fd, _LK_UNLCK, 1); _close(fd);
#else
    ::flock(fd, LOCK_UN); ::close(fd);
#endif
}
} // namespace extra12

void Interpreter::registerNativesExtra12() {
    using namespace extra12;

    natives["fileSync"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileSync", a, 1, line);
        std::string path = asString(a[0], "fileSync", line);
        std::string full = resolvePath(path, line);
#ifdef _WIN32
        int fd = _open(full.c_str(), _O_RDWR | _O_BINARY);
        if (fd < 0) throw diagErr(diag::Code::E0036_IOFailure, line, "fileSync: cannot open '" + path + "'");
        int rc = _commit(fd); _close(fd);
        if (rc != 0) throw diagErr(diag::Code::E0036_IOFailure, line, "fileSync: commit failed for '" + path + "'");
#else
        int fd = ::open(full.c_str(), O_RDONLY);
        if (fd < 0) throw diagErr(diag::Code::E0036_IOFailure, line, "fileSync: cannot open '" + path + "' (missing or not accessible)");
        int rc = ::fsync(fd); ::close(fd);
        if (rc != 0) throw diagErr(diag::Code::E0036_IOFailure, line, "fileSync: fsync failed for '" + path + "'");
#endif
        return Value::boolean_(true);
    };

    natives["fileRename"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileRename", a, 2, line);
        std::string from = asString(a[0], "fileRename", line), to = asString(a[1], "fileRename", line);
        std::string ff = resolvePath(from, line), tf = resolvePath(to, line);
        ensureParentDir(tf);
        std::error_code ec;
        std::filesystem::rename(ff, tf, ec); // POSIX: rename(2) ذرّية وتستبدل الوجهة
        if (ec) throw diagErr(diag::Code::E0036_IOFailure, line, "fileRename: cannot rename '" + from + "' to '" + to + "': " + ec.message());
        return Value::boolean_(true);
    };

    natives["fileSize"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileSize", a, 1, line);
        std::string path = asString(a[0], "fileSize", line);
        std::error_code ec;
        auto sz = std::filesystem::file_size(resolvePath(path, line), ec);
        if (ec) throw diagErr(diag::Code::E0036_IOFailure, line, "fileSize: cannot read the size of '" + path + "' (missing?)");
        return Value::num(static_cast<double>(sz));
    };

    natives["fileLock"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileLock", a, 1, line);
        std::string path = asString(a[0], "fileLock", line);
        std::string full = resolvePath(path, line);
        if (heldLocks().count(full)) return Value::boolean_(true); // نحمله أصلاً
        ensureParentDir(full);
#ifdef _WIN32
        int fd = _open(full.c_str(), _O_RDWR | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        int fd = ::open(full.c_str(), O_RDWR | O_CREAT, 0644);
#endif
        if (fd < 0) throw diagErr(diag::Code::E0036_IOFailure, line, "fileLock: cannot open '" + path + "'");
        if (!tryLockFd(fd)) {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
            return Value::boolean_(false); // عند عملية أخرى
        }
        heldLocks()[full] = fd;
        return Value::boolean_(true);
    };

    natives["fileUnlock"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileUnlock", a, 1, line);
        std::string full = resolvePath(asString(a[0], "fileUnlock", line), line);
        auto it = heldLocks().find(full);
        if (it == heldLocks().end()) return Value::boolean_(false);
        unlockAndClose(it->second);
        heldLocks().erase(it);
        return Value::boolean_(true);
    };

    natives["fileIsLocked"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("fileIsLocked", a, 1, line);
        std::string full = resolvePath(asString(a[0], "fileIsLocked", line), line);
        if (heldLocks().count(full)) return Value::boolean_(true);
#ifdef _WIN32
        int fd = _open(full.c_str(), _O_RDWR | _O_BINARY);
#else
        int fd = ::open(full.c_str(), O_RDWR);
#endif
        if (fd < 0) return Value::boolean_(false); // لا ملف = لا قفل (ولا نُنشئه)
        bool got = tryLockFd(fd);
        if (got) unlockAndClose(fd);
        else {
#ifdef _WIN32
            _close(fd);
#else
            ::close(fd);
#endif
        }
        return Value::boolean_(!got);
    };

    // findByRange(collection, field, lo, hi): مسح مرتّب (ليس فهرساً): أرقام أو نصوص (تواريخ ISO) من نفس نوع الحدّ
    natives["findByRange"] = [this](std::vector<Value>& a, int line) -> Value {
        expectArgs("findByRange", a, 4, line);
        std::string coll = canonContainer(asString(a[0], "findByRange", line));
        std::string field = asString(a[1], "findByRange", line);
        const Value& lo = a[2]; const Value& hi = a[3];
        auto okBound = [](const Value& v) { return v.type == Value::Type::NIL || v.type == Value::Type::NUMBER || v.type == Value::Type::STRING; };
        if (!okBound(lo) || !okBound(hi))
            throw diagErr(diag::Code::E0004_InvalidType, line, "findByRange: الحدّان يجب أن يكونا رقماً أو نصاً أو nil (حدّ مفتوح)");
        if (lo.type != Value::Type::NIL && hi.type != Value::Type::NIL && lo.type != hi.type)
            throw diagErr(diag::Code::E0004_InvalidType, line, "findByRange: الحدّان يجب أن يكونا من نوع واحد");
        std::vector<std::pair<std::string, Value>> docs;
        auto it = docStore.find(coll);
        if (it != docStore.end()) docs = it->second;
        else if (isTableContainer(coll)) docs = tableAsDocs(coll);
        else throw diagErr(diag::Code::E0014_InvalidContainer, line, "findByRange: `" + coll + "` ليست حاوية مستندات/قائمة/جدول");
        auto path = sqlSplitSlash(field);
        std::vector<std::pair<Value, std::pair<std::string, Value>>> hit; // (قيمة الحقل, (id, doc))
        for (auto& d : docs) {
            Value fv;
            if (!sqlGetNestedField(d.second, path, fv)) continue;
            if (fv.type != Value::Type::NUMBER && fv.type != Value::Type::STRING) continue;
            if (lo.type != Value::Type::NIL && (fv.type != lo.type || sqlCompareValues(fv, lo) < 0)) continue;
            if (hi.type != Value::Type::NIL && (fv.type != hi.type || sqlCompareValues(fv, hi) > 0)) continue;
            hit.push_back({fv, d});
        }
        std::stable_sort(hit.begin(), hit.end(), [](auto& x, auto& y) { return x.first.type == y.first.type && sqlCompareValues(x.first, y.first) < 0; });
        auto out = std::make_shared<ArrayData>();
        for (auto& h : hit) {
            auto m = std::make_shared<MapData>();
            m->push_back({Value::string("_id"), Value::string(h.second.first)});
            if (h.second.second.type == Value::Type::MAP && h.second.second.map)
                for (auto& kv : *h.second.second.map) if (!(kv.first.type == Value::Type::STRING && kv.first.str == "_id")) m->push_back(kv);
            out->push_back(Value::makeMap(m));
        }
        return Value::makeArray(out);
    };
}

} // namespace rin

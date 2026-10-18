// ============================================================================
//  rin terminal — batch 3: 15 advanced commands. Header-only, POSIX + std only.
//  Same style as rin_terminal_xcmds.h:  $ cmd args …   * globs   ~/   A:B ranges
//  No process is spawned, so everything also works in the embedded (Android)
//  terminal. All text printed by these commands is English only.
//
//    files    bigfiles recent dupes empty loc todo
//    safety   trash backup sync rename
//    data     json csv stats context verify
// ============================================================================
#pragma once

#include "rin_terminal_xcmds.h"

#include <sys/time.h>

namespace rin {
namespace terminal {
namespace xcmds {

// ----------------------------------------------------------------------------
// helpers
// ----------------------------------------------------------------------------
struct WalkEnt {
    std::string path;
    struct stat st;
};

// Recursive, symlink-safe walk. Hidden entries are skipped unless `hidden`; `.git` is always skipped.
// Bounded (depth 64, `cap` entries) so a huge tree can never hang or exhaust memory.
inline void walkTree(const std::string& root, bool hidden, bool wantDirs, size_t cap, std::vector<WalkEnt>& out, bool& truncated, int depth = 0) {
    if (depth > 64 || out.size() >= cap) { truncated = truncated || out.size() >= cap; return; }
    DIR* d = ::opendir(root.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (dirent* e = ::readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n == ".git") continue;
        if (!hidden && n[0] == '.') continue;
        names.push_back(n);
    }
    ::closedir(d);
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
        if (out.size() >= cap) { truncated = true; return; }
        std::string p = joinPath(root, n);
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (wantDirs) out.push_back({p, st});
            walkTree(p, hidden, wantDirs, cap, out, truncated, depth + 1);
        } else if (S_ISREG(st.st_mode)) {
            out.push_back({p, st});
        }
    }
}
// Collect the files under every operand (a plain file operand is taken as is). Default operand: ".".
inline std::vector<WalkEnt> collectFiles(const std::vector<std::string>& roots, bool hidden, bool wantDirs, bool& truncated, size_t cap = 200000) {
    std::vector<WalkEnt> out;
    std::vector<std::string> r = roots.empty() ? std::vector<std::string>{"."} : roots;
    for (const auto& root : r) {
        struct stat st;
        if (::lstat(root.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walkTree(root, hidden, wantDirs, cap, out, truncated);
        else if (S_ISREG(st.st_mode)) out.push_back({root, st});
    }
    return out;
}
inline bool looksBinary(const std::string& s) {
    size_t n = std::min<size_t>(s.size(), 4096);
    for (size_t i = 0; i < n; ++i) if (s[i] == '\0') return true;
    return false;
}
inline std::string agoStr(time_t t) {
    long d = static_cast<long>(std::time(nullptr) - t);
    if (d < 0) d = 0;
    char b[32];
    if (d < 60) std::snprintf(b, sizeof(b), "%lds", d);
    else if (d < 3600) std::snprintf(b, sizeof(b), "%ldm", d / 60);
    else if (d < 86400) std::snprintf(b, sizeof(b), "%ldh", d / 3600);
    else if (d < 86400L * 60) std::snprintf(b, sizeof(b), "%ldd", d / 86400);
    else if (d < 86400L * 730) std::snprintf(b, sizeof(b), "%ldmo", d / (86400L * 30));
    else std::snprintf(b, sizeof(b), "%ldy", d / (86400L * 365));
    return std::string(b) + " ago";
}
inline std::string stampNow(const char* fmt = "%Y%m%d-%H%M%S") {
    char b[64];
    std::time_t t = std::time(nullptr);
    std::tm tmv;
    ::localtime_r(&t, &tmv);
    std::strftime(b, sizeof(b), fmt, &tmv);
    return b;
}
inline size_t u8len(const std::string& s) {
    size_t n = 0;
    for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n;
    return n;
}
inline std::string u8trunc(const std::string& s, size_t maxCp) {
    if (u8len(s) <= maxCp) return s;
    size_t cp = 0, i = 0;
    while (i < s.size() && cp < maxCp - 1) { ++i; while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i; ++cp; }
    return s.substr(0, i) + "\xE2\x80\xA6";
}
inline std::string padU8(const std::string& s, size_t w, bool right = false) {
    size_t l = u8len(s);
    if (l >= w) return s;
    return right ? std::string(w - l, ' ') + s : s + std::string(w - l, ' ');
}
// Copy one regular file, keeping mode and modification time (so a later `$ sync` sees it as up to date).
inline bool copyFileKeep(const std::string& a, const std::string& b, std::string& why) {
    int in = ::open(a.c_str(), O_RDONLY);
    if (in < 0) { why = std::strerror(errno); return false; }
    struct stat st;
    ::fstat(in, &st);
    int out = ::open(b.c_str(), O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (out < 0) { why = std::strerror(errno); ::close(in); return false; }
    char buf[65536];
    ssize_t n;
    bool ok = true;
    while ((n = ::read(in, buf, sizeof(buf))) > 0) {
        char* p = buf;
        while (n > 0) {
            ssize_t w = ::write(out, p, static_cast<size_t>(n));
            if (w < 0) { why = std::strerror(errno); ok = false; break; }
            p += w; n -= w;
        }
        if (!ok) break;
    }
    if (n < 0 && ok) { why = std::strerror(errno); ok = false; }
    ::close(in);
    ::close(out);
    if (ok) { struct utimbuf tb; tb.actime = st.st_atime; tb.modtime = st.st_mtime; ::utime(b.c_str(), &tb); }
    return ok;
}
inline bool mkdirsFor(const std::string& dir) {
    if (dir.empty() || isDir(dir)) return true;
    std::string cur;
    size_t i = 0;
    if (dir[0] == '/') { cur = "/"; i = 1; }
    while (i <= dir.size()) {
        size_t j = dir.find('/', i);
        std::string part = dir.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (!part.empty()) {
            cur = joinPath(cur, part);
            if (!isDir(cur) && ::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return isDir(dir);
}

// ----------------------------------------------------------------------------
// files
// ----------------------------------------------------------------------------
inline int cmdBigfiles(Ctx& c) {
    if (!c.parse("", "n")) return 2;
    long n = c.hasv('n') ? std::strtol(c.v('n').c_str(), nullptr, 10) : 10;
    if (n < 1) n = 10;
    bool trunc = false;
    auto files = collectFiles(c.pos, false, false, trunc);
    std::sort(files.begin(), files.end(), [](const WalkEnt& a, const WalkEnt& b) { return a.st.st_size > b.st.st_size; });
    unsigned long long total = 0;
    for (const auto& f : files) total += static_cast<unsigned long long>(f.st.st_size);
    for (long i = 0; i < n && i < static_cast<long>(files.size()); ++i) {
        char b[48];
        std::snprintf(b, sizeof(b), "%8s  ", human(static_cast<unsigned long long>(files[static_cast<size_t>(i)].st.st_size)).c_str());
        c.say(b + files[static_cast<size_t>(i)].path);
    }
    c.say(std::to_string(files.size()) + " files, " + human(total) + " total" + (trunc ? " (scan truncated)" : ""));
    return 0;
}
inline int cmdRecent(Ctx& c) {
    if (!c.parse("", "nd")) return 2;
    long n = c.hasv('n') ? std::strtol(c.v('n').c_str(), nullptr, 10) : 15;
    if (n < 1) n = 15;
    long days = c.hasv('d') ? std::strtol(c.v('d').c_str(), nullptr, 10) : 0;
    bool trunc = false;
    auto files = collectFiles(c.pos, false, false, trunc);
    std::sort(files.begin(), files.end(), [](const WalkEnt& a, const WalkEnt& b) { return a.st.st_mtime > b.st.st_mtime; });
    time_t now = std::time(nullptr);
    long shown = 0;
    for (const auto& f : files) {
        if (shown >= n) break;
        if (days > 0 && now - f.st.st_mtime > days * 86400L) break;
        char b[64];
        std::snprintf(b, sizeof(b), "%-9s %8s  ", agoStr(f.st.st_mtime).c_str(), human(static_cast<unsigned long long>(f.st.st_size)).c_str());
        c.say(b + f.path);
        ++shown;
    }
    if (shown == 0) c.say("(no matching files)");
    return 0;
}
inline int cmdDupes(Ctx& c) {
    if (!c.parse("", "m")) return 2;
    long long minSize = c.hasv('m') ? std::strtoll(c.v('m').c_str(), nullptr, 10) : 1;
    if (minSize < 1) minSize = 1;
    bool trunc = false;
    auto files = collectFiles(c.pos, false, false, trunc, 50000);
    std::map<long long, std::vector<const WalkEnt*>> bySize;
    for (const auto& f : files) if (f.st.st_size >= minSize && f.st.st_size <= (64LL << 20)) bySize[f.st.st_size].push_back(&f);
    unsigned long long wasted = 0;
    size_t groups = 0, dupFiles = 0;
    for (const auto& kv : bySize) {
        if (kv.second.size() < 2) continue;
        std::map<std::string, std::vector<const WalkEnt*>> byHash;
        for (const auto* f : kv.second) {
            std::string data, why;
            if (!readFileCapped(f->path, data, why)) continue;
            byHash[sha256hex(data)].push_back(f);
        }
        for (const auto& h : byHash) {
            if (h.second.size() < 2) continue;
            ++groups;
            c.say(human(static_cast<unsigned long long>(kv.first)) + " x " + std::to_string(h.second.size()) + "  " + h.first.substr(0, 12));
            for (const auto* f : h.second) { c.say("  " + f->path); ++dupFiles; }
            wasted += static_cast<unsigned long long>(kv.first) * (h.second.size() - 1);
        }
    }
    if (groups == 0) c.say("no duplicate files");
    else c.say(std::to_string(groups) + " groups, " + std::to_string(dupFiles) + " files, " + human(wasted) + " reclaimable");
    if (trunc) c.say("(scan truncated at 50000 files)");
    return 0;
}
inline int cmdEmpty(Ctx& c) {
    if (!c.parse("a", "")) return 2;
    bool trunc = false;
    auto ents = collectFiles(c.pos, c.has('a'), true, trunc);
    size_t n = 0;
    for (const auto& e : ents) {
        bool empty = false;
        if (S_ISREG(e.st.st_mode)) empty = e.st.st_size == 0;
        else if (S_ISDIR(e.st.st_mode)) {
            DIR* d = ::opendir(e.path.c_str());
            if (d) { empty = true; while (dirent* x = ::readdir(d)) { std::string nm = x->d_name; if (nm != "." && nm != "..") { empty = false; break; } } ::closedir(d); }
        }
        if (empty) { c.say(std::string(S_ISDIR(e.st.st_mode) ? "dir   " : "file  ") + e.path); ++n; }
    }
    c.say(std::to_string(n) + " empty entries");
    return 0;
}
inline int cmdLoc(Ctx& c) {
    if (!c.parse("a", "")) return 2;
    bool trunc = false;
    auto files = collectFiles(c.pos, c.has('a'), false, trunc);
    struct Row { size_t files = 0, lines = 0, blank = 0; unsigned long long bytes = 0; };
    std::map<std::string, Row> rows;
    for (const auto& f : files) {
        std::string data, why;
        if (f.st.st_size > (8LL << 20) || !readFileCapped(f.path, data, why) || looksBinary(data)) continue;
        std::string base = baseName(f.path);
        size_t dot = base.find_last_of('.');
        std::string ext = dot == std::string::npos || dot == 0 ? "(none)" : base.substr(dot);
        Row& r = rows[ext];
        r.files++;
        r.bytes += data.size();
        for (const auto& l : textLines(data)) { r.lines++; if (l.find_first_not_of(" \t") == std::string::npos) r.blank++; }
    }
    std::vector<std::pair<std::string, Row>> v(rows.begin(), rows.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.lines > b.second.lines; });
    Row tot;
    char b[160];
    std::snprintf(b, sizeof(b), "%-10s %7s %9s %9s %9s %9s", "ext", "files", "lines", "code", "blank", "size");
    c.say(b);
    for (const auto& kv : v) {
        const Row& r = kv.second;
        std::snprintf(b, sizeof(b), "%-10s %7zu %9zu %9zu %9zu %9s", kv.first.c_str(), r.files, r.lines, r.lines - r.blank, r.blank, human(r.bytes).c_str());
        c.say(b);
        tot.files += r.files; tot.lines += r.lines; tot.blank += r.blank; tot.bytes += r.bytes;
    }
    std::snprintf(b, sizeof(b), "%-10s %7zu %9zu %9zu %9zu %9s", "TOTAL", tot.files, tot.lines, tot.lines - tot.blank, tot.blank, human(tot.bytes).c_str());
    c.say(b);
    if (trunc) c.say("(scan truncated)");
    return 0;
}
inline int cmdTodo(Ctx& c) {
    if (!c.parse("i", "t")) return 2;
    std::vector<std::string> tags;
    {
        std::string t = c.hasv('t') ? c.v('t') : "TODO,FIXME,XXX,HACK,BUG";
        std::stringstream ss(t);
        std::string x;
        while (std::getline(ss, x, ',')) if (!x.empty()) tags.push_back(x);
    }
    bool trunc = false;
    auto files = collectFiles(c.pos, false, false, trunc);
    size_t hits = 0;
    for (const auto& f : files) {
        std::string data, why;
        if (f.st.st_size > (4LL << 20) || !readFileCapped(f.path, data, why) || looksBinary(data)) continue;
        size_t ln = 0;
        for (const auto& l : textLines(data)) {
            ++ln;
            for (const auto& t : tags) {
                size_t p = c.has('i') ? std::string::npos : l.find(t);
                if (c.has('i')) {
                    std::string a = l, bt = t;
                    std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                    std::transform(bt.begin(), bt.end(), bt.begin(), ::tolower);
                    p = a.find(bt);
                }
                if (p == std::string::npos) continue;
                bool wordL = p == 0 || !std::isalnum(static_cast<unsigned char>(l[p - 1]));
                bool wordR = p + t.size() >= l.size() || !std::isalnum(static_cast<unsigned char>(l[p + t.size()]));
                if (!wordL || !wordR) continue;
                size_t a = l.find_first_not_of(" \t");
                c.say(f.path + ":" + std::to_string(ln) + ": " + u8trunc(l.substr(a == std::string::npos ? 0 : a), 140));
                ++hits;
                break;
            }
        }
    }
    c.say(std::to_string(hits) + " marker(s)");
    return 0;
}

// ----------------------------------------------------------------------------
// safety: trash / backup / sync / rename
// ----------------------------------------------------------------------------
inline std::string trashRoot() {
    if (const char* x = std::getenv("XDG_DATA_HOME")) if (*x) return std::string(x) + "/rin/trash";
    if (const char* h = std::getenv("HOME")) return std::string(h) + "/.local/share/rin/trash";
    return "";
}
inline int cmdTrash(Ctx& c) {
    if (!c.parse("leuy", "")) return 2;
    std::string root = trashRoot();
    if (root.empty()) return c.fail("HOME is not set");
    std::string files = root + "/files", info = root + "/info";
    if (c.has('l') || (c.pos.empty() && !c.has('e'))) {
        DIR* d = ::opendir(info.c_str());
        std::vector<std::string> ids;
        if (d) { while (dirent* e = ::readdir(d)) { std::string n = e->d_name; if (n != "." && n != "..") ids.push_back(n); } ::closedir(d); }
        std::sort(ids.begin(), ids.end());
        for (const auto& id : ids) {
            std::string t, why;
            readFileCapped(info + "/" + id, t, why);
            auto ls = textLines(t);
            c.say(id + "  <-  " + (ls.size() > 1 ? ls[0] : t));
        }
        c.say(std::to_string(ids.size()) + " item(s) in trash   (restore: $ trash -u <id>, empty: $ trash -e -y)");
        return 0;
    }
    if (c.has('e')) {
        if (!c.has('y')) return c.fail("this permanently deletes everything in the trash; run $ trash -e -y");
        std::string why;
        removeTree(files, why);
        removeTree(info, why);
        c.say("trash emptied");
        return 0;
    }
    if (c.has('u')) {
        for (const auto& id : c.pos) {
            std::string t, why;
            if (id.find('/') != std::string::npos || !readFileCapped(info + "/" + id, t, why)) { c.warn("no such trash item: " + id); continue; }
            auto ls = textLines(t);
            if (ls.empty() || ls[0].empty()) { c.warn("corrupt trash record: " + id); continue; }
            if (exists(ls[0])) { c.warn("cannot restore, destination exists: " + ls[0]); continue; }
            mkdirsFor(dirName(ls[0]));
            if (::rename((files + "/" + id).c_str(), ls[0].c_str()) != 0) {
                std::string w2;
                if (!copyTree(files + "/" + id, ls[0], w2) || !removeTree(files + "/" + id, w2)) { c.warn("cannot restore '" + id + "': " + w2); continue; }
            }
            ::unlink((info + "/" + id).c_str());
            c.say("restored " + ls[0]);
        }
        return c.rc;
    }
    if (!mkdirsFor(files) || !mkdirsFor(info)) return c.fail("cannot create the trash directory");
    const char* home = std::getenv("HOME");
    static unsigned seq = 0;
    for (const auto& p : c.pos) {
        std::string real = realPathOf(p);
        if (real.empty()) { c.warn("cannot trash '" + p + "': " + std::strerror(errno)); continue; }
        if (real == "/" || (home && real == realPathOf(home)) || real == realPathOf(root) || startsWith(real + "/", realPathOf(root) + "/")) { c.warn("refusing to trash '" + p + "'"); continue; }
        std::string id = stampNow() + "-" + std::to_string(++seq) + "-" + baseName(real);
        std::string dst = files + "/" + id;
        if (::rename(p.c_str(), dst.c_str()) != 0) {
            std::string why;
            if (errno != EXDEV || !copyTree(p, dst, why) || !removeTree(p, why)) { c.warn("cannot trash '" + p + "': " + (why.empty() ? std::strerror(errno) : why)); continue; }
        }
        std::FILE* f = std::fopen((info + "/" + id).c_str(), "wb");
        if (f) { std::string rec = real + "\n" + stampNow("%Y-%m-%d %H:%M:%S") + "\n"; std::fwrite(rec.data(), 1, rec.size(), f); std::fclose(f); }
        c.say("trashed " + p + "  ->  " + id);
    }
    return c.rc;
}
inline int cmdBackup(Ctx& c) {
    if (!c.parse("", "d")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ backup [-d DIR] <file|dir…>   e.g. $ backup main.rin -d ~/backups");
    std::string destDir = c.v('d');
    if (!destDir.empty() && !mkdirsFor(destDir)) return c.fail("cannot create '" + destDir + "'");
    for (const auto& p : c.pos) {
        if (!exists(p)) { c.warn("cannot back up '" + p + "': No such file or directory"); continue; }
        std::string clean = p;
        while (clean.size() > 1 && clean.back() == '/') clean.pop_back();
        std::string target = (destDir.empty() ? clean : joinPath(destDir, baseName(clean))) + ".bak-" + stampNow();
        std::string why;
        bool ok = isDir(p) ? copyTree(p, target, why) : copyFileKeep(p, target, why);
        if (!ok) c.warn("cannot back up '" + p + "': " + why);
        else c.say(p + "  ->  " + target);
    }
    return c.rc;
}
inline int cmdSync(Ctx& c) {
    if (!c.parse("nv", "")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ sync [-n dry-run] [-v] <src-dir> <dest-dir>   e.g. $ sync . ~/backup");
    std::string src = c.pos[0], dst = c.pos[1];
    if (!isDir(src)) return c.fail("'" + src + "' is not a directory");
    std::string rs = realPathOf(src), rd = realPathOf(dst);
    if (!rd.empty() && (rd == rs || startsWith(rd, rs + "/") || startsWith(rs, rd + "/"))) return c.fail("source and destination must not contain each other");
    bool trunc = false;
    std::vector<WalkEnt> ents;
    walkTree(src, true, true, 200000, ents, trunc);
    size_t copied = 0, same = 0, failed = 0;
    unsigned long long bytes = 0;
    std::string prefix = src;
    while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
    for (const auto& e : ents) {
        std::string rel = e.path.substr(prefix.size());
        if (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
        std::string out = joinPath(dst, rel);
        if (S_ISDIR(e.st.st_mode)) { if (!c.has('n') && !mkdirsFor(out)) { c.warn("cannot create '" + out + "'"); ++failed; } continue; }
        struct stat ds;
        bool need = ::stat(out.c_str(), &ds) != 0 || ds.st_size != e.st.st_size || ds.st_mtime < e.st.st_mtime;
        if (!need) { ++same; continue; }
        if (c.has('n') || c.has('v')) c.say(std::string(c.has('n') ? "would copy " : "copy ") + rel);
        if (c.has('n')) { ++copied; bytes += static_cast<unsigned long long>(e.st.st_size); continue; }
        std::string why;
        if (!mkdirsFor(dirName(out)) || !copyFileKeep(e.path, out, why)) { c.warn("cannot copy '" + rel + "': " + (why.empty() ? "cannot create directory" : why)); ++failed; continue; }
        ++copied; bytes += static_cast<unsigned long long>(e.st.st_size);
    }
    c.say(std::string(c.has('n') ? "dry run: " : "") + std::to_string(copied) + " to copy (" + human(bytes) + "), " + std::to_string(same) + " up to date" +
          (failed ? ", " + std::to_string(failed) + " failed" : "") + (trunc ? " (scan truncated)" : ""));
    return c.rc;
}
inline int cmdRename(Ctx& c) {
    if (!c.parse("nfi", "")) return 2;
    if (c.pos.size() < 3) return c.fail("usage: $ rename [-n dry-run] [-f overwrite] [-i ignore case] <regex> <replacement> <file…>   e.g. $ rename '(.*)\\.txt' '$1.rin' *.txt");
    std::regex re;
    try {
        re = std::regex(c.pos[0], c.has('i') ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript);
    } catch (const std::regex_error& e) { return c.fail(std::string("bad regex: ") + e.what()); }
    const std::string& rep = c.pos[1];
    size_t changed = 0;
    std::set<std::string> targets;
    for (size_t i = 2; i < c.pos.size(); ++i) {
        const std::string& p = c.pos[i];
        std::string dir = dirName(p), base = baseName(p);
        std::string nb = std::regex_replace(base, re, rep);
        if (nb == base) continue;
        if (nb.empty() || nb.find('/') != std::string::npos || nb == "." || nb == "..") { c.warn("invalid new name for '" + p + "'"); continue; }
        std::string target = dir == "." && p.find('/') == std::string::npos ? nb : joinPath(dir, nb);
        if ((exists(target) || targets.count(target)) && !c.has('f')) { c.warn("'" + target + "' already exists (use -f to overwrite)"); continue; }
        targets.insert(target);
        if (c.has('n')) { c.say(p + "  ->  " + target); ++changed; continue; }
        if (::rename(p.c_str(), target.c_str()) != 0) { c.warn("cannot rename '" + p + "': " + std::strerror(errno)); continue; }
        c.say(p + "  ->  " + target);
        ++changed;
    }
    c.say(std::to_string(changed) + (c.has('n') ? " would be renamed (dry run)" : " renamed"));
    return c.rc;
}

// ----------------------------------------------------------------------------
// data: json / csv / stats / context / verify
// ----------------------------------------------------------------------------
struct JV {
    char t = 'z';                      // o object, a array, s string, n number, b bool, z null
    std::string raw;                   // s/n/b/z: exact source text
    std::vector<std::string> keys;     // object keys (raw, quoted)
    std::vector<JV> items;             // object values / array elements
};
struct JsonParser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    explicit JsonParser(const std::string& in) : s(in) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool fail(const std::string& m) {
        if (!err.empty()) return false;
        size_t line = 1, col = 1;
        for (size_t k = 0; k < i && k < s.size(); ++k) { if (s[k] == '\n') { ++line; col = 1; } else ++col; }
        err = m + " at line " + std::to_string(line) + ", column " + std::to_string(col);
        return false;
    }
    bool str(std::string& out) {
        size_t st = i;
        ++i;
        while (i < s.size() && s[i] != '"') {
            unsigned char ch = static_cast<unsigned char>(s[i]);
            if (ch < 0x20) return fail("control character in string");
            if (s[i] == '\\') {
                ++i;
                if (i >= s.size()) return fail("unterminated string");
                char e = s[i];
                if (e == 'u') { for (int k = 1; k <= 4; ++k) if (i + k >= s.size() || !std::isxdigit(static_cast<unsigned char>(s[i + k]))) { i += static_cast<size_t>(k); return fail("bad \\u escape"); } i += 4; }
                else if (!std::strchr("\"\\/bfnrt", e)) return fail("bad escape");
            }
            ++i;
        }
        if (i >= s.size()) return fail("unterminated string");
        ++i;
        out = s.substr(st, i - st);
        return true;
    }
    bool value(JV& v, int depth) {
        if (depth > 200) return fail("nesting too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end of input");
        char ch = s[i];
        if (ch == '{') {
            v.t = 'o'; ++i; ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') return fail("expected a string key");
                std::string k;
                if (!str(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                JV child;
                if (!value(child, depth + 1)) return false;
                v.keys.push_back(k);
                v.items.push_back(std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (ch == '[') {
            v.t = 'a'; ++i; ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                JV child;
                if (!value(child, depth + 1)) return false;
                v.items.push_back(std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (ch == '"') { v.t = 's'; return str(v.raw); }
        if (s.compare(i, 4, "true") == 0) { v.t = 'b'; v.raw = "true"; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { v.t = 'b'; v.raw = "false"; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { v.t = 'z'; v.raw = "null"; i += 4; return true; }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            size_t st = i;
            if (s[i] == '-') ++i;
            if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return fail("bad number");
            if (s[i] == '0') ++i; else while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            if (i < s.size() && s[i] == '.') { ++i; if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return fail("bad number"); while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i; }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) { ++i; if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i; if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return fail("bad number"); while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i; }
            v.t = 'n'; v.raw = s.substr(st, i - st);
            return true;
        }
        return fail(std::string("unexpected character '") + ch + "'");
    }
};
inline void jsonOut(const JV& v, bool pretty, int ind, std::string& o) {
    auto nl = [&](int n) { if (pretty) { o += '\n'; o.append(static_cast<size_t>(n) * 2, ' '); } };
    if (v.t == 'o' || v.t == 'a') {
        char open = v.t == 'o' ? '{' : '[', close = v.t == 'o' ? '}' : ']';
        if (v.items.empty()) { o += open; o += close; return; }
        o += open;
        for (size_t k = 0; k < v.items.size(); ++k) {
            if (k) o += ',';
            nl(ind + 1);
            if (v.t == 'o') { o += v.keys[k]; o += pretty ? ": " : ":"; }
            jsonOut(v.items[k], pretty, ind + 1, o);
        }
        nl(ind);
        o += close;
    } else o += v.raw;
}
inline std::string jsonUnquote(const std::string& raw) { return raw.size() >= 2 ? raw.substr(1, raw.size() - 2) : raw; }
inline int cmdJson(Ctx& c) {
    if (!c.parse("mc", "")) return 2;
    std::string data, why;
    if (c.pos.empty()) return c.fail("usage: $ json [-m minify] [-c check only] <file> [path]   e.g. $ json ~/data.json users[0].name");
    if (!readFileCapped(c.pos[0], data, why)) return c.fail("cannot read '" + c.pos[0] + "': " + why);
    JsonParser p(data);
    JV root;
    if (p.value(root, 0)) { p.ws(); if (p.i < data.size()) p.fail("trailing characters"); }
    if (!p.err.empty()) return c.fail("invalid JSON: " + p.err);
    if (c.has('c')) { c.say("valid JSON"); return 0; }
    const JV* cur = &root;
    if (c.pos.size() > 1) {   // path: a.b[0].c   or   ["key"]
        const std::string& path = c.pos[1];
        size_t k = 0;
        while (k < path.size() && cur) {
            if (path[k] == '.') { ++k; continue; }
            if (path[k] == '[') {
                size_t e = path.find(']', k);
                if (e == std::string::npos) return c.fail("bad path: missing ']'");
                std::string idx = path.substr(k + 1, e - k - 1);
                if (cur->t != 'a') return c.fail("path: '" + path.substr(0, e + 1) + "' is not an array");
                char* endp = nullptr;
                long n = std::strtol(idx.c_str(), &endp, 10);
                if (idx.empty() || *endp || n < 0 || static_cast<size_t>(n) >= cur->items.size()) return c.fail("path: index " + idx + " out of range");
                cur = &cur->items[static_cast<size_t>(n)];
                k = e + 1;
            } else {
                size_t e = path.find_first_of(".[", k);
                std::string key = path.substr(k, e == std::string::npos ? std::string::npos : e - k);
                if (cur->t != 'o') return c.fail("path: '" + key + "' — parent is not an object");
                const JV* nx = nullptr;
                for (size_t q = 0; q < cur->keys.size(); ++q) if (jsonUnquote(cur->keys[q]) == key) { nx = &cur->items[q]; break; }
                if (!nx) return c.fail("path: key '" + key + "' not found");
                cur = nx;
                k = e == std::string::npos ? path.size() : e;
            }
        }
    }
    std::string out;
    jsonOut(*cur, !c.has('m'), 0, out);
    c.say(out);
    return 0;
}
inline std::vector<std::vector<std::string>> parseCsv(const std::string& t, char d) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string f;
    bool q = false, any = false;
    for (size_t i = 0; i < t.size(); ++i) {
        char ch = t[i];
        if (q) {
            if (ch == '"') { if (i + 1 < t.size() && t[i + 1] == '"') { f += '"'; ++i; } else q = false; }
            else f += ch;
        } else if (ch == '"' && f.empty()) { q = true; any = true; }
        else if (ch == d) { row.push_back(f); f.clear(); any = true; }
        else if (ch == '\n' || ch == '\r') {
            if (ch == '\r' && i + 1 < t.size() && t[i + 1] == '\n') ++i;
            if (any || !f.empty() || !row.empty()) { row.push_back(f); rows.push_back(row); }
            row.clear(); f.clear(); any = false;
        } else { f += ch; any = true; }
    }
    if (any || !f.empty() || !row.empty()) { row.push_back(f); rows.push_back(row); }
    return rows;
}
inline int cmdCsv(Ctx& c) {
    if (!c.parse("H", "dnw")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ csv [-d DELIM] [-n ROWS] [-w MAXWIDTH] [-H no header] <file>   e.g. $ csv ~/sales.csv -n 20");
    std::string data, why;
    if (!readFileCapped(c.pos[0], data, why)) return c.fail("cannot read '" + c.pos[0] + "': " + why);
    char d = ',';
    if (c.hasv('d')) { std::string x = c.v('d'); d = (x == "tab" || x == "\\t") ? '\t' : (x.empty() ? ',' : x[0]); }
    else {
        size_t nl = data.find('\n');
        std::string first = data.substr(0, nl);
        size_t best = 0;
        for (char cand : {',', ';', '\t', '|'}) { size_t n = static_cast<size_t>(std::count(first.begin(), first.end(), cand)); if (n > best) { best = n; d = cand; } }
    }
    auto rows = parseCsv(data, d);
    if (rows.empty()) { c.say("(empty)"); return 0; }
    size_t maxRows = c.hasv('n') ? static_cast<size_t>(std::max(1L, std::strtol(c.v('n').c_str(), nullptr, 10))) : 50;
    size_t maxW = c.hasv('w') ? static_cast<size_t>(std::max(4L, std::strtol(c.v('w').c_str(), nullptr, 10))) : 40;
    size_t total = rows.size();
    size_t limit = std::min(total, maxRows + (c.has('H') ? 0 : 1));
    size_t cols = 0;
    for (size_t r = 0; r < limit; ++r) cols = std::max(cols, rows[r].size());
    std::vector<size_t> w(cols, 1);
    for (size_t r = 0; r < limit; ++r)
        for (size_t k = 0; k < rows[r].size(); ++k) w[k] = std::max(w[k], std::min(maxW, u8len(rows[r][k])));
    auto line = [&](size_t r) {
        std::string o = "\xE2\x94\x82";
        for (size_t k = 0; k < cols; ++k) {
            std::string cell = k < rows[r].size() ? u8trunc(rows[r][k], maxW) : "";
            for (auto& ch : cell) if (ch == '\t') ch = ' ';
            bool num = !cell.empty() && cell.find_first_not_of("0123456789.-+eE,") == std::string::npos;
            o += " " + padU8(cell, w[k], num) + " \xE2\x94\x82";
        }
        return o;
    };
    auto rule = [&](const char* l, const char* m, const char* r) {
        std::string o = l;
        for (size_t k = 0; k < cols; ++k) { for (size_t q = 0; q < w[k] + 2; ++q) o += "\xE2\x94\x80"; o += (k + 1 == cols ? r : m); }
        return o;
    };
    c.say(rule("\xE2\x94\x8C", "\xE2\x94\xAC", "\xE2\x94\x90"));
    for (size_t r = 0; r < limit; ++r) {
        c.say(line(r));
        if (r == 0 && !c.has('H') && limit > 1) c.say(rule("\xE2\x94\x9C", "\xE2\x94\xBC", "\xE2\x94\xA4"));
    }
    c.say(rule("\xE2\x94\x94", "\xE2\x94\xB4", "\xE2\x94\x98"));
    size_t dataRows = c.has('H') ? total : total - 1;
    c.say(std::to_string(dataRows) + " rows, " + std::to_string(cols) + " columns" + (limit < total ? "  (showing " + std::to_string(c.has('H') ? limit : limit - 1) + ")" : ""));
    return 0;
}
inline int cmdStats(Ctx& c) {
    if (!c.parse("h", "fde")) return 2;
    std::string text;
    if (c.hasv('e')) text = c.v('e');
    else if (c.pos.empty()) return c.fail("usage: $ stats [-f COLUMN] [-d DELIM] [-h histogram] <file…>   |   $ stats -e \"3 1 4 1 5 9\"");
    for (const auto& p : c.pos) {
        std::string data, why;
        if (!readFileCapped(p, data, why)) { c.warn("cannot read '" + p + "': " + why); continue; }
        text += data + "\n";
    }
    long col = c.hasv('f') ? std::strtol(c.v('f').c_str(), nullptr, 10) : 0;
    std::string delim = c.hasv('d') ? c.v('d') : "";
    std::vector<double> v;
    size_t skipped = 0;
    for (const auto& l : textLines(text)) {
        std::vector<std::string> cells;
        std::string cur;
        for (char ch : l) {
            bool sep = delim.empty() ? (ch == ' ' || ch == '\t' || ch == ',' || ch == ';') : (delim == "tab" ? ch == '\t' : ch == delim[0]);
            if (sep) { if (!cur.empty() || !delim.empty()) cells.push_back(cur); cur.clear(); } else cur += ch;
        }
        if (!cur.empty() || !delim.empty()) cells.push_back(cur);
        if (col > 0) { if (static_cast<size_t>(col) > cells.size()) { if (!l.empty()) ++skipped; continue; } cells = {cells[static_cast<size_t>(col) - 1]}; }
        for (const auto& s : cells) {
            if (s.empty()) continue;
            char* e = nullptr;
            double x = std::strtod(s.c_str(), &e);
            if (e && *e == 0 && std::isfinite(x)) v.push_back(x); else ++skipped;
        }
    }
    if (v.empty()) return c.fail("no numbers found");
    std::sort(v.begin(), v.end());
    double sum = 0;
    for (double x : v) sum += x;
    double mean = sum / static_cast<double>(v.size());
    double var = 0;
    for (double x : v) var += (x - mean) * (x - mean);
    auto pct = [&](double p) {
        double pos = p * static_cast<double>(v.size() - 1);
        size_t lo = static_cast<size_t>(pos);
        size_t hi = std::min(lo + 1, v.size() - 1);
        return v[lo] + (v[hi] - v[lo]) * (pos - static_cast<double>(lo));
    };
    auto row = [&](const char* k, double x) { char b[96]; std::snprintf(b, sizeof(b), "%-8s %.10g", k, x); c.say(b); };
    row("count", static_cast<double>(v.size()));
    row("sum", sum);
    row("min", v.front());
    row("max", v.back());
    row("mean", mean);
    row("median", pct(0.5));
    row("p25", pct(0.25));
    row("p75", pct(0.75));
    row("p95", pct(0.95));
    row("stddev", v.size() > 1 ? std::sqrt(var / static_cast<double>(v.size() - 1)) : 0.0);
    if (skipped) c.say("(" + std::to_string(skipped) + " non-numeric value(s) skipped)");
    if (c.has('h') && v.front() < v.back()) {
        const int B = 10;
        std::vector<size_t> bins(B, 0);
        double lo = v.front(), span = v.back() - v.front();
        for (double x : v) { int bi = static_cast<int>((x - lo) / span * B); if (bi >= B) bi = B - 1; bins[static_cast<size_t>(bi)]++; }
        size_t mx = *std::max_element(bins.begin(), bins.end());
        for (int i = 0; i < B; ++i) {
            char b[96];
            std::snprintf(b, sizeof(b), "%12.5g .. %-12.5g ", lo + span * i / B, lo + span * (i + 1) / B);
            std::string bar;
            size_t len = mx ? bins[static_cast<size_t>(i)] * 30 / mx : 0;
            if (bins[static_cast<size_t>(i)] && len == 0) len = 1;
            for (size_t q = 0; q < len; ++q) bar += "\xE2\x96\x88";
            c.say(std::string(b) + bar + " " + std::to_string(bins[static_cast<size_t>(i)]));
        }
    }
    return 0;
}
inline int cmdContext(Ctx& c) {
    if (!c.parse("", "C")) return 2;
    if (c.pos.size() != 2) return c.fail("usage: $ context [-C N] <file> <line>   e.g. $ context main.rin 42 -C 5");
    char* e = nullptr;
    long line = std::strtol(c.pos[1].c_str(), &e, 10);
    if (!e || *e || line < 1) return c.fail("invalid line number '" + c.pos[1] + "'");
    long around = c.hasv('C') ? std::strtol(c.v('C').c_str(), nullptr, 10) : 3;
    if (around < 0) around = 3;
    std::string data, why;
    if (!readFileCapped(c.pos[0], data, why)) return c.fail("cannot read '" + c.pos[0] + "': " + why);
    auto ls = textLines(data);
    if (static_cast<size_t>(line) > ls.size()) return c.fail("file has only " + std::to_string(ls.size()) + " lines");
    long a = std::max(1L, line - around), b = std::min(static_cast<long>(ls.size()), line + around);
    size_t wd = std::to_string(b).size();
    for (long i = a; i <= b; ++i) {
        std::string num = std::to_string(i);
        c.say(std::string(i == line ? "> " : "  ") + std::string(wd - num.size(), ' ') + num + " | " + ls[static_cast<size_t>(i) - 1]);
    }
    return 0;
}
inline int cmdVerify(Ctx& c) {
    if (!c.parse("q", "")) return 2;
    if (c.pos.empty()) return c.fail("usage: $ verify [-q] <sums-file>   (lines: <sha256>  <path>, like sha256sum -c)");
    std::string sums, why;
    if (!readFileCapped(c.pos[0], sums, why)) return c.fail("cannot read '" + c.pos[0] + "': " + why);
    std::string base = dirName(c.pos[0]);
    size_t ok = 0, bad = 0, missing = 0;
    for (const auto& l : textLines(sums)) {
        if (l.empty() || l[0] == '#') continue;
        size_t sp = l.find_first_of(" \t");
        if (sp != 64) { c.warn("improperly formatted line: " + u8trunc(l, 60)); continue; }
        std::string hash = l.substr(0, 64);
        size_t k = l.find_first_not_of(" \t*", 64);
        if (k == std::string::npos) continue;
        std::string path = l.substr(k);
        std::string full = (!path.empty() && path[0] == '/') ? path : (base == "." ? path : joinPath(base, path));
        std::string data, w2;
        if (!readFileCapped(full, data, w2)) { ++missing; c.say(path + ": MISSING (" + w2 + ")"); continue; }
        std::transform(hash.begin(), hash.end(), hash.begin(), ::tolower);
        if (sha256hex(data) == hash) { ++ok; if (!c.has('q')) c.say(path + ": OK"); }
        else { ++bad; c.say(path + ": FAILED"); }
    }
    c.say(std::to_string(ok) + " OK, " + std::to_string(bad) + " FAILED, " + std::to_string(missing) + " missing");
    return (bad || missing) ? 1 : c.rc;
}

// ----------------------------------------------------------------------------
// registry (batch 3) — merged into registry() in rin_terminal_xcmds.h
// ----------------------------------------------------------------------------
inline const std::vector<Entry>& registry3() {
    static const std::vector<Entry> r = {
        {{"bigfiles", "$ bigfiles [-n N] [dir…]",                      "largest files under a directory (default 10, current dir)", true}, cmdBigfiles},
        {{"recent",   "$ recent [-n N] [-d DAYS] [dir…]",              "most recently modified files, with age", true}, cmdRecent},
        {{"dupes",    "$ dupes [-m MINBYTES] [dir…]",                  "find duplicate files by content (SHA-256)", true}, cmdDupes},
        {{"empty",    "$ empty [-a] [dir…]",                           "list empty files and empty directories (-a: hidden too)", true}, cmdEmpty},
        {{"loc",      "$ loc [-a] [dir…]",                             "lines of code per file extension (text files only)", true}, cmdLoc},
        {{"todo",     "$ todo [-i] [-t TAG,TAG] [dir…]",               "find TODO/FIXME/XXX/HACK/BUG markers (-t custom tags)", true}, cmdTodo},
        {{"trash",    "$ trash <path…> | -l | -u <id> | -e -y",        "safe delete to ~/.local/share/rin/trash (list, restore, empty)", true}, cmdTrash},
        {{"backup",   "$ backup [-d DIR] <file|dir…>",                 "copy to <name>.bak-<timestamp> (e.g. $ backup main.rin -d ~/)", true}, cmdBackup},
        {{"sync",     "$ sync [-n] [-v] <src-dir> <dest-dir>",         "copy new/changed files only (-n dry run); e.g. $ sync . ~/backup", true}, cmdSync},
        {{"rename",   "$ rename [-n -f -i] <regex> <replacement> <file…>", "bulk rename by regex ($1 groups); -n previews", true}, cmdRename},
        {{"json",     "$ json [-m] [-c] <file> [path]",                "validate, pretty-print, minify or query JSON (a.b[0].c)", true}, cmdJson},
        {{"csv",      "$ csv [-d DELIM] [-n ROWS] [-w W] [-H] <file>", "show a CSV file as an aligned table", true}, cmdCsv},
        {{"stats",    "$ stats [-f COL] [-d DELIM] [-h] <file…|-e TEXT>", "count/sum/min/max/mean/median/percentiles/stddev (-h histogram)", true}, cmdStats},
        {{"context",  "$ context [-C N] <file> <line>",                "show a line with N lines around it (default 3)", true}, cmdContext},
        {{"verify",   "$ verify [-q] <sums-file>",                     "check SHA-256 sums (like sha256sum -c)", true}, cmdVerify},
    };
    return r;
}

}  // namespace xcmds
}  // namespace terminal
}  // namespace rin

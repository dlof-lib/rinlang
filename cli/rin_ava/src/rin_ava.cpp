// ============================================================================
//  rin_ava — أداة بناء Rin بواجهة g++  (RIN_AVA)
// ============================================================================
//  rin_ava prog.rin -o prog      ← يحوّل برنامج Rin إلى ملف تنفيذي مستقل
//
//  لا لغة جديدة هنا: التحقق يتم بمحلّل Rin الأصلي (`rin check`) والتنفيذ بمحرّكات Rin
//  الأصلية نفسها. rin_ava مجرد "سائق" (driver) كما في g++: يحلّل الخيارات، يشغّل المراحل
//  بالترتيب، ويعرض الأخطاء بصيغة  file:line: error: message.
//
//  المراحل:
//    1) فحص  : `rin check`  (نفس lexer/parser الحقيقيين)            [-fsyntax-only يتوقف هنا]
//       تحليل: `rin analyze` (SemanticAnalyzer + TypeChecker)        [-fanalyze]
//    2) بناء : حسب الخلفية (backend) — كلها تشترك في نفس الواجهة:
//         interp (الافتراضية) : نسخة من runtime `rin` + البرنامج مُضمَّناً → المفسّر
//                               rin_interpreter.cpp (دلالات مطابقة 100%، بلا مترجم C)
//         vm                  : `rin bytecode` ← بايتكود RBC1 مُضمَّن → toolchain/BytecodeVM
//         native              : rinc → C → cc (المجموعة المدعومة من rinc فقط)
//         engine              : يبني المفسّر نفسه من مصدره (rin_interpreter.cpp وأخواته)
//                               مع البرنامج مُضمَّناً — تنفيذي مستقل لا يحتاج `rin` أصلاً
//         auto                : native إن أمكن وإلا interp
//
//  المتطلبات: C++17 + POSIX (Linux / macOS / WSL / MSYS2). بلا أي مكتبة خارجية.
// ============================================================================
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "rin_vm.h"
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef RIN_AVA_VERSION
#define RIN_AVA_VERSION "1.0.0"
#endif

namespace {

// يجب أن تطابق cli/linux/src/rin_ava_payload.h
const char kMagic[9]   = "RINAVAP1";  // v1: [name \0 source]            ← المفسّر
const char kMagicV2[9] = "RINAVAP2";  // v2: [kind name \0 data]  kind: S مصدر / B بايتكود

// ---------------------------------------------------------------- لغة الرسائل
bool g_ar = false;
const char* T(const char* en, const char* ar) { return g_ar ? ar : en; }

// ---------------------------------------------------------------- ألوان
bool g_color = false;
std::string paint(const std::string& s, const char* code) {
    return g_color ? std::string("\033[") + code + "m" + s + "\033[0m" : s;
}
std::string tag() { return paint("rin_ava:", "1"); }
std::string errWord() { return paint("error:", "1;31"); }
std::string warnWord() { return paint("warning:", "1;35"); }
std::string fatalWord() { return paint("fatal error:", "1;31"); }

// ---------------------------------------------------------------- أدوات نظام
bool fileExists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }
bool isExec(const std::string& p) { return fileExists(p) && ::access(p.c_str(), X_OK) == 0; }

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

std::string baseName(const std::string& p) {
    auto s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
std::string stem(const std::string& p) {
    std::string b = baseName(p);
    auto d = b.find_last_of('.');
    return d == std::string::npos || d == 0 ? b : b.substr(0, d);
}
std::string extOf(const std::string& p) {
    std::string b = baseName(p);
    auto d = b.find_last_of('.');
    return d == std::string::npos || d == 0 ? "" : b.substr(d);
}

std::string exeDir(const char* argv0) {
    std::string path;
#if defined(__APPLE__)
    char buf[4096]; uint32_t n = sizeof(buf);
    if (_NSGetExecutablePath(buf, &n) == 0) path = buf;
#else
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; path = buf; }
#endif
    if (path.empty() && argv0) {
        path = argv0;
        if (path.find('/') == std::string::npos) {
            const char* P = std::getenv("PATH");
            std::stringstream ss(P ? P : "");
            std::string d;
            while (std::getline(ss, d, ':')) if (isExec(d + "/" + path)) { path = d + "/" + path; break; }
        }
    }
    char rp[4096];
    if (!path.empty() && ::realpath(path.c_str(), rp)) path = rp;
    auto s = path.find_last_of('/');
    return s == std::string::npos ? "." : path.substr(0, s);
}

std::string findInPath(const std::string& name) {
    const char* P = std::getenv("PATH");
    std::stringstream ss(P ? P : "");
    std::string d;
    while (std::getline(ss, d, ':')) {
        if (d.empty()) continue;
        if (isExec(d + "/" + name)) return d + "/" + name;
    }
    return "";
}

// يبحث عن أداة مرافقة: خيار صريح، متغير بيئة، بجوار rin_ava، libexec، RIN_HOME، ثم PATH.
std::string findTool(const std::string& name, const std::string& explicitPath, const char* envVar,
                     const std::string& selfDir) {
    if (!explicitPath.empty()) return explicitPath;
    if (envVar) if (const char* e = std::getenv(envVar)) if (*e) return e;
    std::vector<std::string> c = {selfDir + "/" + name, selfDir + "/../libexec/rin_ava/" + name,
                                  selfDir + "/../lib/rin_ava/" + name};
    if (const char* h = std::getenv("RIN_HOME")) c.push_back(std::string(h) + "/bin/" + name);
    for (auto& p : c) if (isExec(p)) return p;
    return findInPath(name);
}

std::string quote(const std::string& s) {
    if (!s.empty() && s.find_first_of(" \t\"'\\$`!*?<>|&;()[]{}~#") == std::string::npos) return s;
    std::string r = "'";
    for (char c : s) { if (c == '\'') r += "'\\''"; else r += c; }
    return r + "'";
}
std::string joinCmd(const std::vector<std::string>& a) {
    std::string r;
    for (size_t i = 0; i < a.size(); ++i) { if (i) r += ' '; r += quote(a[i]); }
    return r;
}

// يشغّل أمراً. capture=true: يجمع stdout+stderr في out. وإلا يرث الطرفية.
int runCmd(const std::vector<std::string>& argv, const std::string& cwd, bool capture, std::string* out) {
    int pfd[2] = {-1, -1};
    if (capture && ::pipe(pfd) != 0) return -1;
    pid_t pid = ::fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        if (capture) {
            ::dup2(pfd[1], 1); ::dup2(pfd[1], 2);
            ::close(pfd[0]); ::close(pfd[1]);
        }
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) ::_exit(126);
        std::vector<char*> av;
        for (auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
        av.push_back(nullptr);
        ::execvp(av[0], av.data());
        ::_exit(127);
    }
    if (capture) {
        ::close(pfd[1]);
        char buf[4096]; ssize_t n;
        std::string data;
        while ((n = ::read(pfd[0], buf, sizeof(buf))) > 0) data.append(buf, static_cast<size_t>(n));
        ::close(pfd[0]);
        if (out) *out = data;
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}

std::string makeTempDir() {
    const char* t = std::getenv("TMPDIR");
    std::string base = (t && *t) ? t : "/tmp";
    std::string tpl = base + "/rin_ava.XXXXXX";
    std::vector<char> b(tpl.begin(), tpl.end()); b.push_back(0);
    return ::mkdtemp(b.data()) ? std::string(b.data()) : "";
}
void removeTree(const std::string& d) {
    if (d.empty() || d.find("rin_ava.") == std::string::npos) return;  // حماية من حذف خاطئ
    runCmd({"rm", "-rf", d}, "", true, nullptr);
}

// ---------------------------------------------------------------- الخيارات
struct Opts {
    std::string input, output, forcedLang;
    std::string vmMode = "image";  // خلفية vm: image (كاملة، افتراضية) | bytecode (نواة سريعة) | auto
    std::string backend;           // "" = غير محدد؛ interp | vm | native | engine | go | auto
    bool compileOnly = false;      // -c
    bool emitC = false;            // -S
    bool syntaxOnly = false;       // -fsyntax-only
    bool analyze = false;          // -fanalyze  (rin analyze: SemanticAnalyzer + TypeChecker)
    bool listBackends = false;     // --list-backends
    bool verbose = false;          // -v
    bool dryRun = false;           // -###
    bool saveTemps = false;        // -save-temps
    bool noWarn = false;           // -w
    bool werror = false;           // -Werror
    int colorMode = -1;            // -1 auto, 0 never, 1 always
    int jobs = 0;                  // -jN (0 = عدد أنوية المعالج)
    std::string cc, rinc, runtime, engine, cacheDir, goRuntime, goSrc;
    std::vector<std::string> ccFlags;   // تُمرَّر لمترجم C/C++ في الخلفيتين native وengine
    std::vector<std::string> libs;      // -lNAME / -LDIR (بالترتيب)
    std::vector<std::string> inputs;
};

void printVersion() {
    std::cout << "rin_ava (RinLang) " << RIN_AVA_VERSION << "\n"
              << T("Build driver for the Rin language with a g++-style command line.\n",
                   "أداة بناء للغة Rin بواجهة أوامر شبيهة بـ g++.\n");
}

void printHelp() {
    std::cout <<
"Usage: rin_ava [options] file.rin\n"
"\n"
"Options:\n"
"  -o <file>          Place the output into <file> (default: a.out)\n"
"  -fsyntax-only      Check the program with Rin's real parser and stop\n"
"  -fanalyze          Also run Rin's semantic analyzer + type checker (rin analyze)\n"
"  -S                 Emit assembly-like output: C (native), bytecode listing (vm), wrapper C++ (engine)\n"
"  -c                 Compile only: object file (native) or .rbc bytecode file (vm)\n"
"  -O0 -O1 -O2 -O3 -Os -Ofast -g   C/C++ compiler flags (native/engine); -O1+ also optimizes vm bytecode\n"
"  -I<dir> -D<m> -U<m> -L<dir> -l<lib> -static -pthread -m<opt> -f<opt>\n"
"                     Passed to the C/C++ compiler (native and engine backends only)\n"
"  -x rin             Treat the input as Rin source whatever its extension\n"
"  -save-temps        Keep intermediate files (generated C / C++ / bytecode) in the current directory\n"
"  -v                 Show the commands being run\n"
"  -###               Show the commands without running them\n"
"  -w                 Suppress warnings        -Werror   Treat warnings as errors\n"
"  -Wall -Wextra -pedantic -std=... -pipe   Accepted and ignored (g++ compatibility)\n"
"  -fdiagnostics-color=[auto|always|never]\n"
"\n"
"Backends (-fbackend=NAME or --backend=NAME):\n"
"  interp   (default) copy of the `rin` runtime + your program -> the real interpreter\n"
"                     (rin_interpreter.cpp). Accepts every valid Rin program; no C compiler needed.\n"
"  vm                 VM image (rin_vm library): your program + every @import resolved at build time\n"
"                     in ONE self-contained image, run by the full engine -> complete language, instant build.\n"
"                     -fvm-bytecode: strict fast path (`rin bytecode` -> BytecodeVM, core subset only)\n"
"                     -fvm-auto:     bytecode when the program compiles to it (no imports), else the image\n"
"  native             rinc -> C -> cc (the subset rinc can translate; fastest output)\n"
"  engine             build the interpreter engine itself from source (needs a C++ compiler + zlib)\n"
"                     with the program embedded: a standalone binary that does not need `rin`\n"
"  go                 gorin's embedded Go interpreter (gorin/cmd/rin) + your program: a standalone binary\n"
"                     that needs neither `rin` nor a C compiler (core language subset; checked with --check)\n"
"  auto               native when rinc accepts the program, otherwise interp (or go if only gorin exists)\n"
"  Shortcuts: -fnative -finterp (-fbundle) -fvm -fvm-bytecode -fvm-auto -fengine -fgo -fauto\n"
"\n"
"Tools (also via environment):\n"
"  --runtime=<rin>   RIN_AVA_RUNTIME     --rinc=<path>  RIN_AVA_RINC     --cc=<cc>  RIN_AVA_CC\n"
"  --engine-src=<dir> RIN_AVA_ENGINE_SRC (app/src/main/cpp)   --engine-cache=<dir> RIN_AVA_CACHE   -jN\n"
"  RIN_AVA_RINVM=<rinvm>  (VM runtime preferred by -fvm when found next to rin_ava)\n"
"  --go-runtime=<gorin-embedded> RIN_AVA_GORIN   --gorin-src=<dir> RIN_AVA_GORIN_SRC (built with `go build` if no binary)\n"
"  --list-backends    Show which backends are usable on this machine\n"
"  --version  -dumpversion  -dumpmachine  --help\n"
"\n"
"Environment: RIN_AVA_LANG=ar for Arabic messages, NO_COLOR to disable colors.\n"
"Exit status: 0 on success, 1 on any error (like g++).\n";
}

// ---------------------------------------------------------------- تشخيصات
std::string sourceLine(const std::string& src, int line) {
    std::istringstream ss(src);
    std::string l; int n = 0;
    while (std::getline(ss, l)) if (++n == line) return l;
    return "";
}

// يحوّل مخرجات `rin check --format=short` أو رسائل rinc العربية إلى file:line: error: msg
void printDiagnostics(const std::string& raw, const std::string& file, const std::string& src) {
    std::istringstream ss(raw);
    std::string line;
    bool any = false;
    std::regex shortRe(R"(^(.*?):(\d+):(?:(\d+):)?\s*(error|warning|note)?:?\s*(.*)$)");
    std::regex rincRe(R"(سطر\s*(\d+)\)?:\s*(.*)$)");
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        std::smatch m;
        int ln = 0; std::string msg, sev = "error", col;
        if (std::regex_search(line, m, rincRe)) { ln = std::atoi(m[1].str().c_str()); msg = m[2]; }
        else if (std::regex_match(line, m, shortRe)) {
            ln = std::atoi(m[2].str().c_str()); col = m[3]; sev = m[4].matched && !m[4].str().empty() ? m[4].str() : "error"; msg = m[5];
        } else {
            if (line.find("✓") != std::string::npos || line.find("ℹ") != std::string::npos) continue;
            std::cerr << line << "\n"; any = true; continue;
        }
        std::string where = file + ":" + std::to_string(ln) + (col.empty() ? "" : ":" + col) + ":";
        std::string word = sev == "warning" ? warnWord() : sev == "note" ? paint("note:", "1;36") : errWord();
        std::cerr << paint(where, "1") << " " << word << " " << msg << "\n";
        std::string sl = sourceLine(src, ln);
        if (!sl.empty()) {
            char gut[32]; std::snprintf(gut, sizeof(gut), "%5d | ", ln);
            std::cerr << gut << sl << "\n";
        }
        any = true;
    }
    if (!any) std::cerr << file << ": " << errWord() << " " << T("syntax check failed", "فشل فحص الصياغة") << "\n";
}

void announce(const Opts& o, const std::vector<std::string>& cmd) {
    if (o.verbose || o.dryRun) std::cerr << tag() << " " << joinCmd(cmd) << "\n";
}

[[noreturn]] void fatalNoInput() {
    std::cerr << tag() << " " << fatalWord() << " " << T("no input files", "لا توجد ملفات إدخال") << "\n"
              << T("compilation terminated.", "تم إنهاء البناء.") << "\n";
    std::exit(1);
}

// ---------------------------------------------------------------- الحمولة (bundle)
// kind: 'S' مصدر (يُكتب بتخطيط v1 المتوافق مع كل runtime سابق) أو 'B' بايتكود (v2).
bool bundle(const std::string& runtimePath, char kind, const std::string& displayName, const std::string& data,
            const std::string& outPath, std::string& err) {
    std::string rt;
    if (!readFile(runtimePath, rt)) { err = std::string(T("cannot read runtime: ", "تعذّر قراءة runtime: ")) + runtimePath; return false; }
    // لا تُلحِق مرتين إن كان runtime نفسه ناتج rin_ava
    if (rt.size() >= 16 && (std::memcmp(rt.data() + rt.size() - 8, kMagic, 8) == 0 ||
                            std::memcmp(rt.data() + rt.size() - 8, kMagicV2, 8) == 0)) {
        err = T("runtime already contains a program", "الـ runtime يحوي برنامجاً مُضمَّناً بالفعل");
        return false;
    }
    const bool v2 = kind != 'S';
    std::string payload;
    if (v2) payload.push_back(kind);
    payload += displayName; payload.push_back('\0'); payload += data;
    std::string trailer(16, '\0');
    uint64_t len = payload.size();
    for (int i = 0; i < 8; ++i) trailer[i] = static_cast<char>((len >> (8 * i)) & 0xff);
    std::memcpy(&trailer[8], v2 ? kMagicV2 : kMagic, 8);

    std::string tmp = outPath + ".rin_ava_tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { err = std::string(T("cannot write: ", "تعذّرت الكتابة: ")) + outPath; return false; }
        f.write(rt.data(), static_cast<std::streamsize>(rt.size()));
        f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        f.write(trailer.data(), static_cast<std::streamsize>(trailer.size()));
        if (!f) { ::remove(tmp.c_str()); err = T("write failed", "فشلت الكتابة"); return false; }
    }
    if (::chmod(tmp.c_str(), 0755) != 0 || ::rename(tmp.c_str(), outPath.c_str()) != 0) {
        ::remove(tmp.c_str());
        err = std::string(T("cannot create output: ", "تعذّر إنشاء الناتج: ")) + outPath;
        return false;
    }
    return true;
}

// تحذير: الملفات المستورَدة لا تُضمَّن (تُحلّ وقت التشغيل)
void warnImports(const Opts& o, const std::string& file, const std::string& src) {
    if (o.noWarn) return;
    std::regex imp(R"(^\s*(@import\b|use\s+\S+\s+from\s+["']))");
    std::istringstream ss(src); std::string l; int n = 0;
    while (std::getline(ss, l)) {
        ++n;
        if (std::regex_search(l, imp)) {
            std::cerr << paint(file + ":" + std::to_string(n) + ":", "1") << " " << warnWord() << " "
                      << T("imported files are not embedded; they are resolved at run time",
                           "الملفات المستورَدة لا تُضمَّن؛ تُحلّ وقت التشغيل")
                      << "\n";
            if (o.werror) std::exit(1);
            return;  // تحذير واحد يكفي
        }
    }
}

std::string defaultMachine() {
#if defined(__x86_64__)
    std::string a = "x86_64";
#elif defined(__aarch64__)
    std::string a = "aarch64";
#else
    std::string a = "unknown";
#endif
#if defined(__APPLE__)
    return a + "-apple-darwin";
#else
    return a + "-linux-gnu";
#endif
}

// ---------------------------------------------------------------- خلفية engine: مصدر المحرّك
// نفس ملفات cli/linux/CMakeLists.txt التي يُبنى منها `rin` (بلا الطرفية وRinPM)، لذا أي تعديل
// على المفسّر في app/src/main/cpp ينعكس هنا تلقائياً دون نسخ أي كود.
const char* const kEngineSources[] = {
    "rin_lexer.cpp", "rin_parser.cpp", "rin_make.cpp", "rin_container_sql.cpp", "rin_artifact.cpp",
    "rin_interpreter.cpp", "rin_http.cpp", "rin_c_api.cpp", "indsin/rin_indsin_c_api.cpp",
    "toolchain/rin_toolchain.cpp",
    "binfmt/elf_format.cpp", "binfmt/ar_format.cpp", "binfmt/pe_format.cpp", "binfmt/macho_format.cpp",
    "binfmt/coff_obj.cpp", "loader_ui/library_loader_ui.cpp",
    "diagnostics/diagnostic.cpp", "diagnostics/source_manager.cpp", "diagnostics/diagnostic_engine.cpp",
    "diagnostics/diagnostic_renderer.cpp",
    "clc/clc_container.cpp", "clc/clc_compress.cpp", "clc/clc_security.cpp", "clc/clc_rin_opt.cpp",
    "clc/clc_zip_import.cpp", "clc/sha256.cpp",
};

bool isEngineDir(const std::string& d) {
    return !d.empty() && fileExists(d + "/rin_interpreter.cpp") && fileExists(d + "/rin_lexer.cpp") &&
           fileExists(d + "/rin_parser.cpp");
}

std::string findEngineSrc(const std::string& explicitDir, const std::string& selfDir) {
    if (!explicitDir.empty()) return explicitDir;
    if (const char* e = std::getenv("RIN_AVA_ENGINE_SRC")) if (*e) return e;
    std::vector<std::string> c = {
        selfDir + "/../../../app/src/main/cpp",   // cli/linux/build → جذر المستودع
        selfDir + "/../../app/src/main/cpp",
        selfDir + "/../app/src/main/cpp",
        selfDir + "/../share/rin/engine", selfDir + "/../lib/rin/engine",
        "app/src/main/cpp", "../app/src/main/cpp", "../../app/src/main/cpp"};
    if (const char* h = std::getenv("RIN_HOME")) { c.push_back(std::string(h) + "/engine"); c.push_back(std::string(h) + "/app/src/main/cpp"); }
    for (auto& d : c) if (isEngineDir(d)) { char rp[4096]; return ::realpath(d.c_str(), rp) ? std::string(rp) : d; }
    return "";
}

// بصمة شجرة المصدر (أحدث mtime + العدد): أي تعديل على المحرّك يُبطل ذاكرة الكائنات تلقائياً.
std::string treeStamp(const std::string& dir) {
    namespace fs = std::filesystem;
    long long newest = 0; size_t n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        auto ext = it->path().extension().string();
        if (ext != ".cpp" && ext != ".h" && ext != ".hpp") continue;
        auto t = fs::last_write_time(it->path(), ec);
        long long v = static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count());
        if (v > newest) newest = v;
        ++n;
    }
    return std::to_string(newest) + "-" + std::to_string(n);
}

std::string hashHex(const std::string& s) {  // FNV-1a 64 — يكفي لتسمية مجلد ذاكرة مؤقتة
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char b[24]; std::snprintf(b, sizeof(b), "%016llx", static_cast<unsigned long long>(h));
    return b;
}

bool mkdirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec || std::filesystem::is_directory(path);
}

std::string cacheRoot(const Opts& o) {
    if (!o.cacheDir.empty()) return o.cacheDir;
    if (const char* e = std::getenv("RIN_AVA_CACHE")) if (*e) return e;
    if (const char* x = std::getenv("XDG_CACHE_HOME")) if (*x) return std::string(x) + "/rin_ava";
    if (const char* h = std::getenv("HOME")) if (*h) return std::string(h) + "/.cache/rin_ava";
    return "/tmp/rin_ava_cache";
}

// يشغّل عدة أوامر بالتوازي (حتى jobs في آنٍ واحد). كل أمر يكتب خرجه في ملف سجل.
// يرجع -1 عند النجاح الكامل، أو فهرس أول أمر فشل.
struct Job { std::vector<std::string> argv; std::string log; };
int runParallel(const std::vector<Job>& jobs, int maxJobs, bool verbose) {
    std::map<pid_t, size_t> running;
    size_t next = 0; int failed = -1;
    auto launch = [&](size_t i) {
        if (verbose) std::cerr << tag() << " " << joinCmd(jobs[i].argv) << "\n";
        pid_t pid = ::fork();
        if (pid == 0) {
            int fd = ::open(jobs[i].log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd >= 0) { ::dup2(fd, 1); ::dup2(fd, 2); ::close(fd); }
            std::vector<char*> av;
            for (auto& a : jobs[i].argv) av.push_back(const_cast<char*>(a.c_str()));
            av.push_back(nullptr);
            ::execvp(av[0], av.data());
            ::_exit(127);
        }
        if (pid > 0) running[pid] = i; else if (failed < 0) failed = static_cast<int>(i);
    };
    while ((next < jobs.size() && failed < 0) || !running.empty()) {
        while (failed < 0 && next < jobs.size() && static_cast<int>(running.size()) < maxJobs) launch(next++);
        if (running.empty()) break;
        int st = 0; pid_t done = ::wait(&st);
        if (done < 0) break;
        auto it = running.find(done);
        if (it == running.end()) continue;
        bool ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
        if (!ok && failed < 0) failed = static_cast<int>(it->second);
        running.erase(it);
    }
    return failed;
}

std::string cxxLiteralBytes(const std::string& data) {
    std::string o = "{";
    char b[8]; int col = 0;
    for (unsigned char c : data) {
        int n = std::snprintf(b, sizeof(b), "%u,", c);
        o.append(b, static_cast<size_t>(n));
        if ((col += n) > 100) { o += '\n'; col = 0; }
    }
    return o + "0}";  // بايت صفري ختامي حتى لا تكون المصفوفة فارغة أبداً
}

// main() للتنفيذي المستقل: نفس سلوك `rin file.rin args…` (runSource في cli/linux/src/main.cpp).
std::string engineWrapper(const std::string& name, const std::string& source) {
    std::string w =
"// Auto-generated by rin_ava (engine backend) — do not edit\n"
"#include \"rin_lexer.h\"\n#include \"rin_parser.h\"\n#include \"rin_interpreter.h\"\n"
"#include \"diagnostics/diagnostic_renderer.h\"\n#include \"diagnostics/source_manager.h\"\n"
"#include <iostream>\n#include <string>\n#include <vector>\n\n";
    w += "static const unsigned char kRinSrc[] = " + cxxLiteralBytes(source) + ";\n";
    w += "static const char* kRinName = ";
    std::string q = "\"";
    for (char c : name) { if (c == '"' || c == '\\') q += '\\'; if (c == '\n') continue; q += c; }
    w += q + "\";\n";
    w +=
"\nint main(int argc, char** argv) {\n"
"    std::string source(reinterpret_cast<const char*>(kRinSrc), sizeof(kRinSrc) - 1);\n"
"    std::string sourceName = kRinName;\n"
"    std::vector<std::string> args;\n"
"    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);\n"
"    rin::Interpreter interp;\n"
"    interp.setProgramArgs(args);\n"
"    try {\n"
"        rin::Lexer lexer(source, sourceName);\n"
"        auto tokens = lexer.scanTokens();\n"
"        rin::Parser parser(tokens, sourceName);\n"
"        auto statements = parser.parse();\n"
"        interp.setSourceFile(sourceName);\n"
"        std::string out = interp.run(statements);\n"
"        std::cout << out;\n"
"        return interp.hadError() ? 1 : 0;\n"
"    } catch (rin::RinError& e) {\n"
"        if (e.diagnostic) std::cerr << \"\\n\" << rin::diag::renderPlain(*e.diagnostic, rin::diag::globalSourceManager());\n"
"        else std::cerr << \"\\n[rin] error in \" << sourceName << \" at line \" << e.line << \": \" << e.message << \"\\n\";\n"
"        return 1;\n"
"    } catch (std::exception& e) {\n"
"        std::cerr << \"\\n[rin] internal error: \" << e.what() << \"\\n\";\n"
"        return 1;\n"
"    }\n"
"}\n";
    return w;
}

}  // namespace

// ============================================================================
//  الخلفيات (backends)
// ============================================================================
namespace {

struct Ctx {
    Opts o;
    std::string source, runtime, rincPath, selfDir, base;
};

std::string rtOrDefault(const Ctx& c) { return c.runtime.empty() ? "rin" : c.runtime; }

int fail(const std::string& msg) {
    std::cerr << tag() << " " << errWord() << " " << msg << "\n";
    return 1;
}

std::string firstLines(const std::string& text, size_t n) {
    std::istringstream ss(text); std::string l, r; size_t i = 0;
    std::vector<std::string> all;
    while (std::getline(ss, l)) all.push_back(l);
    for (i = all.size() > n ? all.size() - n : 0; i < all.size(); ++i) r += all[i] + "\n";
    return r;
}

std::string pickCxx(const Opts& o) {
    std::string cxx;
    if (const char* e = std::getenv("RIN_AVA_CXX")) cxx = e;
    if (cxx.empty() && !o.cc.empty()) {  // استنتاج مرافق مترجم C المُختار
        cxx = o.cc;
        auto rep = [&](const char* a, const char* b) {
            size_t p = cxx.rfind(a);
            if (p != std::string::npos && p + std::strlen(a) == cxx.size()) cxx.replace(p, std::strlen(a), b);
        };
        if (cxx == o.cc) { rep("gcc", "g++"); }
        if (cxx == o.cc) { rep("clang", "clang++"); }
        if (cxx == o.cc) { rep("cc", "c++"); }
    }
    if (cxx.empty()) for (const char* n : {"c++", "g++", "clang++"}) if (!findInPath(n).empty()) { cxx = n; break; }
    return cxx.empty() ? "c++" : cxx;
}

bool hasOptFlag(const std::vector<std::string>& f) {
    for (auto& x : f) if (x.rfind("-O", 0) == 0) return true;
    return false;
}
// آخر -O<n> مُعطى: يحدّد هل نُحسّن البايتكود (O0 أو لا شيء = لا).
bool wantsOptimize(const std::vector<std::string>& f) {
    std::string last;
    for (auto& x : f) if (x.rfind("-O", 0) == 0) last = x;
    return !last.empty() && last != "-O0" && last != "-O";
}

// ---------------------------------------------------------------- interp
int buildInterp(Ctx& c) {
    Opts& o = c.o;
    std::string out = o.output.empty() ? "a.out" : o.output;
    if (!o.dryRun) warnImports(o, o.input, c.source);
    announce(o, {"<bundle:interp>", rtOrDefault(c), "+", o.input, "->", out});
    if (o.dryRun) return 0;
    std::string err;
    if (!bundle(c.runtime, 'S', baseName(o.input), c.source, out, err)) return fail(err);
    return 0;
}

// ---------------------------------------------------------------- vm (بايتكود)
int buildVmBytecode(Ctx& c) {
    Opts& o = c.o;
    std::string tmp = o.dryRun ? "/tmp/rin_ava.DRYRUN" : makeTempDir();
    if (tmp.empty()) return fail(T("cannot create a temporary directory", "تعذّر إنشاء مجلد مؤقت"));
    auto cleanup = [&]() { if (!o.dryRun) removeTree(tmp); };

    char rp[4096];
    std::string absInput = ::realpath(o.input.c_str(), rp) ? std::string(rp) : o.input;
    std::string rbc = tmp + "/" + c.base + ".rbc";
    std::vector<std::string> cmd = {rtOrDefault(c), "bytecode", absInput};
    bool listing = o.emitC;   // -S: قائمة التعليمات (disassembly)
    if (!listing) { cmd.push_back("--out"); cmd.push_back(rbc); }
    if (wantsOptimize(o.ccFlags)) cmd.push_back("--optimize");
    announce(o, cmd);
    std::string out;
    if (!o.dryRun) {
        int rc = runCmd(cmd, "", true, &out);
        if (rc != 0) {
            printDiagnostics(out, o.input, c.source);
            std::cerr << tag() << " " << T("hint: the vm backend supports only Rin's core subset; try the default backend (interp) or -fauto",
                                            "تلميح: خلفية vm تدعم نواة اللغة فقط؛ جرّب الخلفية الافتراضية (interp) أو -fauto") << "\n";
            cleanup();
            return 1;
        }
    }
    if (listing) {
        std::string dest = o.output.empty() ? c.base + ".rbc.txt" : o.output;
        if (!o.dryRun) {
            if (dest == "-") std::cout << out;
            else { std::ofstream f(dest, std::ios::binary); f << out; if (!f) { cleanup(); return fail(dest + ": " + std::strerror(errno)); } }
        }
        cleanup();
        return 0;
    }
    std::string bytes;
    if (!o.dryRun && !readFile(rbc, bytes)) { cleanup(); return fail(T("rin did not produce a bytecode file", "لم ينتج rin ملف البايتكود")); }
    if (o.saveTemps && !o.dryRun) { std::ofstream f(c.base + ".rbc", std::ios::binary); f << bytes; }
    int rc = 0;
    if (o.compileOnly) {   // -c: ملف .rbc فقط (يُنفَّذ لاحقاً بـ `rin vm` أو يُضمَّن)
        std::string dest = o.output.empty() ? c.base + ".rbc" : o.output;
        if (!o.dryRun) { std::ofstream f(dest, std::ios::binary); f << bytes; if (!f) rc = fail(dest + ": " + std::strerror(errno)); }
    } else {
        std::string dest = o.output.empty() ? "a.out" : o.output;
        announce(o, {"<bundle:vm>", rtOrDefault(c), "+", c.base + ".rbc", "->", dest});
        if (!o.dryRun) {
            std::string err;
            if (!bundle(c.runtime, 'B', baseName(o.input), bytes, dest, err)) rc = fail(err);
        }
    }
    cleanup();
    return rc;
}

// ---------------------------------------------------------------- vm: صورة rin_vm (كاملة)
// tryBytecode: يحاول `rin bytecode` أولاً (إن نجح وبلا استيرادات يُضمَّن البايتكود السريع)، وإلا المحرّك الكامل.
int buildVmImage(Ctx& c, bool tryBytecode) {
    Opts& o = c.o;
    std::string bytes;
    bool useBytecode = false;
    if (tryBytecode && rin_vm::scanImports(c.source).empty() && !c.runtime.empty() && !o.dryRun) {
        std::string tmp = makeTempDir();
        if (!tmp.empty()) {
            char rp[4096];
            std::string absInput = ::realpath(o.input.c_str(), rp) ? std::string(rp) : o.input;
            std::string rbc = tmp + "/" + c.base + ".rbc", out;
            std::vector<std::string> cmd = {rtOrDefault(c), "bytecode", absInput, "--out", rbc};
            if (wantsOptimize(o.ccFlags)) cmd.push_back("--optimize");
            announce(o, cmd);
            if (runCmd(cmd, "", true, &out) == 0 && readFile(rbc, bytes) && !bytes.empty()) useBytecode = true;
            removeTree(tmp);
        }
        if (!useBytecode && !o.noWarn)
            std::cerr << tag() << " " << T("note: program is outside the bytecode subset; using the full-engine VM image",
                                            "ملاحظة: البرنامج خارج نواة البايتكود؛ سيُستخدم VM الكامل (المحرّك)") << "\n";
    }
    if (o.dryRun) { announce(o, {"<vm-image>", o.input, "->", o.output.empty() ? "a.out" : o.output}); return 0; }
    rin_vm::Image im = rin_vm::build(baseName(o.input), o.input, c.source, useBytecode ? bytes : std::string());
    if (!im.unresolved.empty() && !o.noWarn) {
        std::string l; for (auto& u : im.unresolved) l += (l.empty() ? "" : ", ") + u;
        std::cerr << paint(o.input + ":", "1") << " " << warnWord() << " "
                  << T("not found at build time (embedded library or package, resolved at run time): ",
                       "لم يُعثَر عليها وقت البناء (مكتبة مدمجة أو حزمة؛ تُحلّ وقت التشغيل): ") << l << "\n";
        if (o.werror) return 1;
    }
    std::string image = rin_vm::serialize(im);
    if (o.emitC) {   // -S: وصف الصورة
        std::string dest = o.output.empty() ? c.base + ".rvm.txt" : o.output;
        std::string d = rin_vm::describe(im);
        if (dest == "-") { std::cout << d; return 0; }
        return rin_vm::writeFile(dest, d) ? 0 : fail(dest + ": " + std::strerror(errno));
    }
    if (o.compileOnly) {   // -c: ملف .rvm يُنفَّذ بـ `rin image`
        std::string dest = o.output.empty() ? c.base + ".rvm" : o.output;
        return rin_vm::writeFile(dest, image) ? 0 : fail(dest + ": " + std::strerror(errno));
    }
    std::string dest = o.output.empty() ? "a.out" : o.output;
    // runtime الصورة: rinvm (الآلة الافتراضية الكاملة، أصغر) إن وُجدت بجوار rin_ava/RIN_AVA_RINVM، وإلا rin
    std::string vmrt = findTool("rinvm", "", "RIN_AVA_RINVM", c.selfDir);
    if (vmrt.empty()) vmrt = c.runtime;
    announce(o, {"<bundle:vm-image>", vmrt, "+", c.base + ".rvm", "->", dest});
    std::string err;
    if (!bundle(vmrt, 'M', baseName(o.input), image, dest, err)) return fail(err);
    return 0;
}

int buildVm(Ctx& c) {
    const std::string& m = c.o.vmMode;
    if (m == "bytecode") return buildVmBytecode(c);
    return buildVmImage(c, m == "auto");
}

// ---------------------------------------------------------------- native (rinc → C → cc)
// soft=true: لا تطبع أخطاء؛ تُرجع السبب في why (للاختيار التلقائي auto).
int buildNative(Ctx& c, bool soft, std::string* why) {
    Opts& o = c.o;
    auto bail = [&](const std::string& reason, const std::string& raw = "") {
        if (soft) { if (why) *why = reason; return 1; }
        if (!raw.empty()) printDiagnostics(raw, o.input, c.source);
        else std::cerr << tag() << " " << errWord() << " " << reason << "\n";
        return 1;
    };
    if (c.rincPath.empty() && !o.dryRun) {
        if (soft) { if (why) *why = T("rinc not found", "rinc غير موجود"); return 1; }
        std::cerr << tag() << " " << fatalWord() << " "
                  << T("cannot find 'rinc' (needed by the native backend). Set RIN_AVA_RINC or use --rinc=<path>",
                       "تعذّر إيجاد 'rinc' (مطلوب للخلفية native). اضبط RIN_AVA_RINC أو استخدم --rinc=<path>")
                  << "\n";
        return 1;
    }
    std::string tmp = o.dryRun ? "/tmp/rin_ava.DRYRUN" : makeTempDir();
    if (tmp.empty()) return bail(T("cannot create a temporary directory", "تعذّر إنشاء مجلد مؤقت"));
    auto cleanup = [&]() { if (!o.dryRun) removeTree(tmp); };

    char rp[4096];
    std::string absInput = ::realpath(o.input.c_str(), rp) ? std::string(rp) : o.input;
    std::vector<std::string> rcmd = {c.rincPath.empty() ? "rinc" : c.rincPath, absInput, "--emit-c-only", "--no-embed-fallback"};
    announce(o, rcmd);
    std::string cfile = tmp + "/" + c.base + ".c";
    if (!o.dryRun) {
        std::string out;
        int rc = runCmd(rcmd, tmp, true, &out);
        if (rc != 0) {
            std::string r = firstLines(out, 1);
            cleanup();
            if (!soft) {
                printDiagnostics(out, o.input, c.source);
                std::cerr << tag() << " " << T("hint: the default backend accepts every valid Rin program; use -finterp (or -fauto)",
                                                "تلميح: الخلفية الافتراضية تقبل كل برنامج Rin صالح؛ استخدم -finterp (أو -fauto)") << "\n";
                return 1;
            }
            if (why) *why = r;
            return 1;
        }
        if (!fileExists(cfile)) { cleanup(); return bail(T("rinc did not produce a C file", "لم ينتج rinc ملف C")); }
    }

    if (o.emitC) {
        std::string dest = o.output.empty() ? c.base + ".c" : o.output;
        if (!o.dryRun) {
            std::string cs; readFile(cfile, cs);
            if (dest == "-") std::cout << cs;
            else { std::ofstream f(dest, std::ios::binary); f << cs; if (!f) { cleanup(); return bail(dest + ": " + std::strerror(errno)); } }
        }
        cleanup();
        return 0;
    }

    std::string cc = o.cc;
    if (cc.empty()) {
        for (const char* n : {"cc", "gcc", "clang"}) if (!findInPath(n).empty()) { cc = n; break; }
        if (cc.empty()) cc = "cc";
    }
    std::vector<std::string> ccmd = {cc};
    if (!hasOptFlag(o.ccFlags)) ccmd.push_back("-O0");
    if (o.compileOnly) ccmd.push_back("-c");
    for (auto& f : o.ccFlags) ccmd.push_back(f);
    std::string out = o.output.empty() ? (o.compileOnly ? c.base + ".o" : "a.out") : o.output;
    ccmd.push_back("-o"); ccmd.push_back(out);
    ccmd.push_back(cfile);
    if (!o.compileOnly) { for (auto& l : o.libs) ccmd.push_back(l); ccmd.push_back("-lm"); }
    announce(o, ccmd);
    int rc = 0;
    if (!o.dryRun) {
        std::string log;
        rc = runCmd(ccmd, "", soft, soft ? &log : nullptr);
        if (o.saveTemps) { std::string cs; readFile(cfile, cs); std::ofstream f(c.base + ".c", std::ios::binary); f << cs; }
        if (rc != 0 && soft && why) *why = T("the C compiler failed", "فشل مترجم C");
    }
    cleanup();
    if (rc != 0) {
        if (!soft) std::cerr << tag() << " " << errWord() << " " << T("the C backend failed (use -save-temps to inspect the generated C)",
                                                                  "فشلت خلفية C (استخدم -save-temps لفحص كود C المولَّد)") << "\n";
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------- engine (المحرّك من المصدر)
int buildEngine(Ctx& c) {
    Opts& o = c.o;
    if (o.compileOnly) return fail(T("-c is not supported by the engine backend (use -S to get the wrapper C++, or omit -c)",
                                     "-c غير مدعوم في خلفية engine (استخدم -S للحصول على ملف C++ المُغلِّف أو احذف -c)"));
    std::string eng = findEngineSrc(o.engine, c.selfDir);
    if (eng.empty() && !o.dryRun) {
        std::cerr << tag() << " " << fatalWord() << " "
                  << T("cannot find the Rin engine sources (rin_interpreter.cpp, rin_lexer.cpp, rin_parser.cpp). "
                       "Pass --engine-src=<repo>/app/src/main/cpp or set RIN_AVA_ENGINE_SRC",
                       "تعذّر إيجاد مصدر محرّك Rin (rin_interpreter.cpp وrin_lexer.cpp وrin_parser.cpp). "
                       "مرّر --engine-src=<repo>/app/src/main/cpp أو اضبط RIN_AVA_ENGINE_SRC")
                  << "\n";
        return 1;
    }
    if (eng.empty()) eng = "<engine-src>";
    std::string cxx = pickCxx(o);
    if (!o.dryRun && findInPath(cxx).empty() && !isExec(cxx)) return fail(std::string(T("no C++ compiler found: ", "لا يوجد مترجم C++: ")) + cxx);

    std::string wrapper = engineWrapper(baseName(o.input), c.source);
    if (o.emitC) {   // -S: ملف C++ المُغلِّف فقط
        std::string dest = o.output.empty() ? c.base + ".engine.cpp" : o.output;
        if (o.dryRun) return 0;
        if (dest == "-") { std::cout << wrapper; return 0; }
        std::ofstream f(dest, std::ios::binary); f << wrapper;
        return f ? 0 : fail(dest + ": " + std::strerror(errno));
    }

    std::string tmp = o.dryRun ? "/tmp/rin_ava.DRYRUN" : makeTempDir();
    if (tmp.empty()) return fail(T("cannot create a temporary directory", "تعذّر إنشاء مجلد مؤقت"));
    auto cleanup = [&]() { if (!o.dryRun) removeTree(tmp); };

    std::vector<std::string> flags = {"-std=c++17"};
    if (!hasOptFlag(o.ccFlags)) flags.push_back("-O2");
    for (auto& f : o.ccFlags) flags.push_back(f);
    flags.push_back("-w");           // تحذيرات المحرّك ليست من شأن مستخدم rin_ava
    flags.push_back("-I" + eng);

    std::string flagStr; for (auto& f : flags) flagStr += f + " ";
    std::string cacheSub = cacheRoot(o) + "/engine-" + hashHex(eng + "|" + cxx + "|" + flagStr + "|" + (o.dryRun ? "" : treeStamp(eng)));
    if (!o.dryRun && !mkdirs(cacheSub)) { cleanup(); return fail(std::string(T("cannot create cache directory: ", "تعذّر إنشاء مجلد الذاكرة المؤقتة: ")) + cacheSub); }

    std::string wsrc = tmp + "/" + c.base + ".engine.cpp", wobj = tmp + "/" + c.base + ".engine.o";
    if (!o.dryRun) { std::ofstream f(wsrc, std::ios::binary); f << wrapper; }

    std::vector<Job> jobs; std::vector<std::pair<std::string, std::string>> renames;  // part → final
    std::vector<std::string> objs;
    size_t cached = 0;
    for (const char* rel : kEngineSources) {
        std::string src = eng + "/" + rel, safe = rel;
        std::replace(safe.begin(), safe.end(), '/', '_');
        std::string obj = cacheSub + "/" + safe + ".o";
        objs.push_back(obj);
        if (!o.dryRun && fileExists(obj)) { ++cached; continue; }
        if (!o.dryRun && !fileExists(src)) { cleanup(); return fail(std::string(T("missing engine source: ", "ملف مصدر المحرّك مفقود: ")) + src); }
        std::vector<std::string> cmd = {cxx}; for (auto& f : flags) cmd.push_back(f);
        cmd.push_back("-c"); cmd.push_back(src); cmd.push_back("-o"); cmd.push_back(obj + ".part");
        jobs.push_back({cmd, tmp + "/" + safe + ".log"});
        renames.push_back({obj + ".part", obj});
    }
    {
        std::vector<std::string> cmd = {cxx}; for (auto& f : flags) cmd.push_back(f);
        cmd.push_back("-c"); cmd.push_back(wsrc); cmd.push_back("-o"); cmd.push_back(wobj);
        jobs.push_back({cmd, tmp + "/wrapper.log"});
    }

    int nj = o.jobs > 0 ? o.jobs : static_cast<int>(::sysconf(_SC_NPROCESSORS_ONLN));
    if (nj < 1) nj = 1;
    if (o.dryRun) {
        for (auto& j : jobs) announce(o, j.argv);
    } else {
        if (jobs.size() > 1 && !o.noWarn)
            std::cerr << tag() << " " << T("building the engine: ", "بناء المحرّك: ") << (jobs.size() - 1) << " "
                      << T("source file(s) to compile, ", "ملف مصدر للترجمة، ") << cached << " "
                      << T("cached (first build is slow; later builds reuse the cache)", "من الذاكرة (أول بناء بطيء؛ التالية تعيد استخدامها)") << "\n";
        int bad = runParallel(jobs, nj, o.verbose);
        if (bad >= 0) {
            std::string log; readFile(jobs[bad].log, log);
            std::cerr << tag() << " " << errWord() << " " << T("C++ compilation failed: ", "فشلت ترجمة C++: ") << jobs[bad].argv[jobs[bad].argv.size() - 1] << "\n"
                      << firstLines(log, 30);
            for (auto& r : renames) ::remove(r.first.c_str());
            cleanup();
            return 1;
        }
        for (auto& r : renames) if (::rename(r.first.c_str(), r.second.c_str()) != 0) { cleanup(); return fail(std::string(T("cannot store object: ", "تعذّر حفظ الكائن: ")) + r.second); }
    }

    std::string out = o.output.empty() ? "a.out" : o.output;
    std::vector<std::string> lcmd = {cxx};
    for (auto& f : o.ccFlags) if (f == "-static" || f == "-pthread" || f == "-s" || f == "-pie" || f == "-no-pie" || f == "-rdynamic" || f.rfind("-m", 0) == 0) lcmd.push_back(f);
    lcmd.push_back("-o"); lcmd.push_back(out);
    lcmd.push_back(wobj);
    for (auto& ob : objs) lcmd.push_back(ob);
    for (auto& l : o.libs) lcmd.push_back(l);
    lcmd.push_back("-lz"); lcmd.push_back("-ldl"); lcmd.push_back("-lpthread"); lcmd.push_back("-lm");
    announce(o, lcmd);
    int rc = 0;
    if (!o.dryRun) {
        std::string log;
        rc = runCmd(lcmd, "", true, &log);
        if (rc != 0) std::cerr << firstLines(log, 30);
        if (o.saveTemps) { std::ofstream f(c.base + ".engine.cpp", std::ios::binary); f << wrapper; }
    }
    cleanup();
    if (rc != 0) return fail(T("linking the engine failed (is zlib's development package installed?)", "فشل ربط المحرّك (هل حزمة تطوير zlib مثبّتة؟)"));
    return 0;
}

// ---------------------------------------------------------------- go (مفسّر Go المدمج في gorin)
bool isGorinDir(const std::string& d) { return !d.empty() && fileExists(d + "/go.mod") && fileExists(d + "/cmd/rin/main.go"); }

std::string findGorinSrc(const Opts& o, const std::string& selfDir) {
    if (!o.goSrc.empty()) return o.goSrc;
    if (const char* e = std::getenv("RIN_AVA_GORIN_SRC")) if (*e) return e;
    std::vector<std::string> c = {selfDir + "/../../../gorin", selfDir + "/../../gorin", selfDir + "/../gorin",
                                  selfDir + "/../share/rin/gorin", "gorin", "../gorin", "../../gorin"};
    if (const char* h = std::getenv("RIN_HOME")) c.push_back(std::string(h) + "/gorin");
    for (auto& d : c) if (isGorinDir(d)) { char rp[4096]; return ::realpath(d.c_str(), rp) ? std::string(rp) : d; }
    return "";
}

// يحدّد ملف مفسّر Go التنفيذي: صريح ← بيئة ← بجوار rin_ava ← RIN_HOME ← PATH ← بناؤه من gorin/ بـ `go build` (مع ذاكرة).
// build=false: لا يبني (للقوائم/الاستعلام فقط). يرجع "" إن تعذّر.
std::string resolveGoRuntime(const Opts& o, const std::string& selfDir, bool build, std::string* why) {
    std::string r = findTool("gorin-embedded", o.goRuntime, "RIN_AVA_GORIN", selfDir);
    if (r.empty()) r = findTool("rin-go", "", nullptr, selfDir);
    if (!r.empty()) return r;
    std::string src = findGorinSrc(o, selfDir);
    if (src.empty()) { if (why) *why = T("gorin-embedded binary not found and no gorin/ sources (use --go-runtime= or --gorin-src=)", "لا يوجد gorin-embedded ولا مصدر gorin/ (استخدم --go-runtime= أو --gorin-src=)"); return ""; }
    if (findInPath("go").empty()) { if (why) *why = T("the Go toolchain ('go') is not installed", "أدوات Go ('go') غير مثبّتة"); return ""; }
    std::string dir = cacheRoot(o) + "/gorin-" + hashHex(src + "|" + treeStamp(src));
    std::string bin = dir + "/gorin-embedded";
    if (isExec(bin)) return bin;
    if (!build) { if (why) *why = T("not built yet (will be built with `go build` on first use)", "لم يُبنَ بعد (سيُبنى بـ `go build` عند أول استخدام)"); return ""; }
    if (!mkdirs(dir)) { if (why) *why = T("cannot create cache directory", "تعذّر إنشاء مجلد الذاكرة المؤقتة"); return ""; }
    if (!o.noWarn) std::cerr << tag() << " " << T("building the Go interpreter (first use only): ", "بناء مفسّر Go (أول استخدام فقط): ") << src << "\n";
    std::vector<std::string> cmd = {"go", "build", "-o", bin + ".part", "./cmd/rin"};
    if (o.verbose) std::cerr << tag() << " " << joinCmd(cmd) << "\n";
    std::string out;
    int rc = runCmd(cmd, src, true, &out);
    if (rc != 0 || !fileExists(bin + ".part")) { if (why) *why = std::string("go build failed:\n") + firstLines(out, 20); ::remove((bin + ".part").c_str()); return ""; }
    ::chmod((bin + ".part").c_str(), 0755);
    if (::rename((bin + ".part").c_str(), bin.c_str()) != 0) { if (why) *why = T("cannot store the built binary", "تعذّر حفظ الملف المبني"); return ""; }
    return bin;
}

int buildGo(Ctx& c) {
    Opts& o = c.o;
    std::string why;
    std::string gort = o.dryRun ? std::string("gorin-embedded") : resolveGoRuntime(o, c.selfDir, true, &why);
    if (gort.empty()) {
        std::cerr << tag() << " " << fatalWord() << " " << T("cannot use the go backend: ", "تعذّر استخدام خلفية go: ") << why << "\n";
        return 1;
    }
    // فحص توافق البرنامج مع محرك Go نفسه (نواة اللغة فقط): يفشل مبكراً بدل الفشل وقت التشغيل
    {
        std::vector<std::string> cmd = {gort, "--check", o.input};
        announce(o, cmd);
        if (!o.dryRun) {
            std::string out;
            int rc = runCmd(cmd, "", true, &out);
            if (rc != 0) {
                printDiagnostics(out, o.input, c.source);
                std::cerr << tag() << " " << T("hint: the Go engine supports only Rin's core subset; use the default backend (interp)",
                                                "تلميح: محرك Go يدعم نواة اللغة فقط؛ استخدم الخلفية الافتراضية (interp)") << "\n";
                return 1;
            }
        }
    }
    std::string out = o.output.empty() ? "a.out" : o.output;
    if (!o.dryRun) warnImports(o, o.input, c.source);
    announce(o, {"<bundle:go>", gort, "+", o.input, "->", out});
    if (o.dryRun) return 0;
    std::string err;
    if (!bundle(gort, 'S', baseName(o.input), c.source, out, err)) return fail(err);
    return 0;
}

// ---------------------------------------------------------------- تحليل دلالي/أنواع (rin analyze)
int runAnalyze(Ctx& c) {
    Opts& o = c.o;
    std::vector<std::string> cmd = {rtOrDefault(c), "analyze", o.input};
    announce(o, cmd);
    if (o.dryRun) return 0;
    std::string out;
    int rc = runCmd(cmd, "", true, &out);
    if (rc == 127 || rc == 126 || rc == -1) return fail(std::string(T("failed to run: ", "تعذّر التشغيل: ")) + joinCmd(cmd));
    bool failed = rc != 0 || out.find("FAILED") != std::string::npos;
    // الأسطر التشخيصية فقط (بلا عناوين الملخّص)
    std::istringstream ss(out); std::string l; bool any = false;
    while (std::getline(ss, l)) {
        if (l.empty() || l.rfind("Rin Toolchain Analysis", 0) == 0 || l.rfind("semantic:", 0) == 0 || l.rfind("types:", 0) == 0) continue;
        std::cerr << o.input << ": " << (failed ? errWord() : warnWord()) << " " << l << "\n";
        any = true;
    }
    if (failed) { if (!any) std::cerr << o.input << ": " << errWord() << " " << T("analysis failed", "فشل التحليل") << "\n"; return 1; }
    if (any && o.werror) return 1;
    return 0;
}

// ---------------------------------------------------------------- --list-backends
int listBackends(Ctx& c) {
    Opts& o = c.o;
    auto row = [&](const char* name, bool ok, const std::string& what) {
        std::cout << "  " << (ok ? paint("[ok]", "1;32") : paint("[--]", "1;31")) << " " << name;
        for (size_t i = std::strlen(name); i < 8; ++i) std::cout << ' ';
        std::cout << what << "\n";
    };
    std::string cc = o.cc;
    if (cc.empty()) for (const char* n : {"cc", "gcc", "clang"}) if (!findInPath(n).empty()) { cc = n; break; }
    std::string cxx = pickCxx(o);
    std::string eng = findEngineSrc(o.engine, c.selfDir);
    bool cxxOk = !findInPath(cxx).empty() || isExec(cxx);
    std::cout << "rin_ava " << RIN_AVA_VERSION << "\n";
    row("interp", !c.runtime.empty(), "rin_interpreter.cpp via runtime: " + (c.runtime.empty() ? std::string("(rin not found)") : c.runtime) + "  [default]");
    row("vm", !c.runtime.empty(), "rin_vm image (full engine + bundled imports); -fvm-bytecode for the BytecodeVM fast path");
    row("native", !c.rincPath.empty() && !cc.empty(), "rinc: " + (c.rincPath.empty() ? std::string("(not found)") : c.rincPath) + "  cc: " + (cc.empty() ? std::string("(not found)") : cc));
    row("engine", !eng.empty() && cxxOk, "sources: " + (eng.empty() ? std::string("(not found; use --engine-src)") : eng) + "  c++: " + (cxxOk ? cxx : std::string("(not found)")));
    {
        std::string why, gr = resolveGoRuntime(o, c.selfDir, false, &why);
        std::string gs = findGorinSrc(o, c.selfDir);
        bool ok = !gr.empty() || (!gs.empty() && !findInPath("go").empty());
        row("go", ok, gr.empty() ? (ok ? "gorin/ sources at " + gs + " (built on first use)" : why) : "gorin-embedded: " + gr);
    }
    row("auto", !c.runtime.empty() || !c.rincPath.empty(), "native when rinc accepts the program, otherwise interp");
    return 0;
}

}  // namespace

// ============================================================================
int main(int argc, char** argv) {
    {
        const char* l = std::getenv("RIN_AVA_LANG");
        const char* lg = std::getenv("LANG");
        g_ar = (l && std::strncmp(l, "ar", 2) == 0) || (!l && lg && std::strncmp(lg, "ar", 2) == 0);
    }
    Ctx ctx;
    Opts& o = ctx.o;
    std::vector<std::string> args(argv + 1, argv + argc);
    bool endOpts = false;

    auto needArg = [&](size_t& i, const std::string& flag, std::string& out) {
        if (i + 1 >= args.size()) {
            std::cerr << tag() << " " << errWord() << " " << T("missing filename after '", "اسم الملف مفقود بعد '") << flag << "'\n";
            std::exit(1);
        }
        out = args[++i];
    };
    auto setBackend = [&](const std::string& b) -> bool {
        static const std::map<std::string, std::string> alias = {
            {"interp", "interp"}, {"interpreter", "interp"}, {"bundle", "interp"}, {"rin", "interp"},
            {"vm", "vm"}, {"bytecode", "vm"}, {"native", "native"}, {"rinc", "native"}, {"c", "native"},
            {"engine", "engine"}, {"embed", "engine"}, {"go", "go"}, {"gorin", "go"}, {"auto", "auto"}};
        auto it = alias.find(b);
        if (it == alias.end()) {
            std::cerr << tag() << " " << errWord() << " " << T("unknown backend '", "خلفية غير معروفة '") << b
                      << T("' (expected interp, vm, native, engine, go or auto)", "' (المتوقع interp أو vm أو native أو engine أو go أو auto)") << "\n";
            return false;
        }
        o.backend = it->second;
        return true;
    };

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string a = args[i];
        if (endOpts || a.empty() || a[0] != '-' || a == "-") { o.inputs.push_back(a); continue; }
        if (a == "--") { endOpts = true; continue; }
        std::string v;
        if (a == "-o") { needArg(i, a, o.output); }
        else if (a.rfind("-o", 0) == 0 && a.size() > 2 && a[2] != 'f') { o.output = a.substr(2); }
        else if (a == "-x") { needArg(i, a, o.forcedLang); }
        else if (a.rfind("-x", 0) == 0 && a.size() > 2) { o.forcedLang = a.substr(2); }
        else if (a == "-c") o.compileOnly = true;
        else if (a == "-S") o.emitC = true;
        else if (a == "-E") {
            std::cerr << tag() << " " << errWord() << " "
                      << T("-E is not applicable: Rin has no separate preprocessing stage",
                           "-E غير مطبّق: Rin لا تملك مرحلة معالجة مسبقة منفصلة") << "\n";
            return 1;
        }
        else if (a == "-v") o.verbose = true;
        else if (a == "-###") o.dryRun = true;
        else if (a == "-save-temps") o.saveTemps = true;
        else if (a == "-fsyntax-only") o.syntaxOnly = true;
        else if (a == "-fanalyze" || a == "-Wanalyze") o.analyze = true;
        else if (a == "-fno-analyze") o.analyze = false;
        else if (a == "-fnative") { if (!setBackend("native")) return 1; }
        else if (a == "-finterp" || a == "-fbundle" || a == "-fno-native") { if (!setBackend("interp")) return 1; }
        else if (a == "-fvm") { if (!setBackend("vm")) return 1; o.vmMode = "image"; }
        else if (a == "-fvm-bytecode") { if (!setBackend("vm")) return 1; o.vmMode = "bytecode"; }
        else if (a == "-fvm-auto") { if (!setBackend("vm")) return 1; o.vmMode = "auto"; }
        else if (a.rfind("--vm-mode=", 0) == 0) { o.vmMode = a.substr(10); if (o.vmMode != "image" && o.vmMode != "bytecode" && o.vmMode != "auto") { std::cerr << tag() << " " << errWord() << " " << T("--vm-mode expects image, bytecode or auto", "--vm-mode يقبل image أو bytecode أو auto") << "\n"; return 1; } }
        else if (a == "-fengine") { if (!setBackend("engine")) return 1; }
        else if (a == "-fgo") { if (!setBackend("go")) return 1; }
        else if (a == "-fauto") { if (!setBackend("auto")) return 1; }
        else if (a.rfind("-fbackend=", 0) == 0) { if (!setBackend(a.substr(10))) return 1; }
        else if (a.rfind("--backend=", 0) == 0) { if (!setBackend(a.substr(10))) return 1; }
        else if (a == "--backend") { needArg(i, a, v); if (!setBackend(v)) return 1; }
        else if (a == "--list-backends") o.listBackends = true;
        else if (a == "-w") o.noWarn = true;
        else if (a == "-Werror") o.werror = true;
        else if (a == "-pipe" || a == "-pedantic" || a == "-ansi" || a.rfind("-W", 0) == 0 || a.rfind("-std=", 0) == 0) { /* مقبول ومتجاهَل */ }
        else if (a == "-fdiagnostics-color=always" || a == "-fcolor-diagnostics") o.colorMode = 1;
        else if (a == "-fdiagnostics-color=never" || a == "-fno-color-diagnostics") o.colorMode = 0;
        else if (a == "-fdiagnostics-color=auto") o.colorMode = -1;
        else if (a == "--version" || a == "-version") { printVersion(); return 0; }
        else if (a == "--help" || a == "-h" || a == "-help") { printHelp(); return 0; }
        else if (a == "-dumpversion") { std::cout << RIN_AVA_VERSION << "\n"; return 0; }
        else if (a == "-dumpmachine") { std::cout << defaultMachine() << "\n"; return 0; }
        else if (a.rfind("--cc=", 0) == 0) o.cc = a.substr(5);
        else if (a.rfind("--rinc=", 0) == 0) o.rinc = a.substr(7);
        else if (a.rfind("--runtime=", 0) == 0) o.runtime = a.substr(10);
        else if (a.rfind("--engine-src=", 0) == 0) o.engine = a.substr(13);
        else if (a.rfind("--go-runtime=", 0) == 0) o.goRuntime = a.substr(13);
        else if (a.rfind("--gorin-src=", 0) == 0) o.goSrc = a.substr(12);
        else if (a.rfind("--engine-cache=", 0) == 0) o.cacheDir = a.substr(15);
        else if (a.rfind("--jobs=", 0) == 0) o.jobs = std::atoi(a.c_str() + 7);
        else if (a.rfind("-j", 0) == 0 && a.size() > 2 && std::isdigit(static_cast<unsigned char>(a[2]))) o.jobs = std::atoi(a.c_str() + 2);
        else if (a == "-j") { needArg(i, a, v); o.jobs = std::atoi(v.c_str()); }
        else if (a == "-I" || a == "-D" || a == "-U" || a == "-L" || a == "-l") {
            needArg(i, a, v);
            if (a == "-L" || a == "-l") o.libs.push_back(a + v); else o.ccFlags.push_back(a + v);
        }
        else if (a.rfind("-l", 0) == 0 || a.rfind("-L", 0) == 0) o.libs.push_back(a);
        else if (a.rfind("-I", 0) == 0 || a.rfind("-D", 0) == 0 || a.rfind("-U", 0) == 0 ||
                 a.rfind("-O", 0) == 0 || a.rfind("-g", 0) == 0 || a.rfind("-m", 0) == 0 ||
                 a.rfind("-f", 0) == 0 || a == "-static" || a == "-pthread" || a == "-s" ||
                 a == "-shared" || a == "-pie" || a == "-no-pie" || a == "-rdynamic")
            o.ccFlags.push_back(a);
        else {
            std::cerr << tag() << " " << errWord() << " "
                      << T("unrecognized command-line option '", "خيار غير معروف '") << a << "'\n";
            return 1;
        }
    }

    // الألوان
    {
        bool tty = ::isatty(2);
        bool noColor = std::getenv("NO_COLOR") != nullptr;
        g_color = o.colorMode == 1 || (o.colorMode == -1 && tty && !noColor);
    }
    if (o.verbose) { std::cerr << tag() << " "; printVersion(); }
    if (const char* e = std::getenv("RIN_AVA_CC")) if (o.cc.empty()) o.cc = e;

    ctx.selfDir = exeDir(argv[0]);
    ctx.runtime = findTool("rin", o.runtime, "RIN_AVA_RUNTIME", ctx.selfDir);
    ctx.rincPath = findTool("rinc", o.rinc, "RIN_AVA_RINC", ctx.selfDir);
    if (o.listBackends) return listBackends(ctx);

    // الخلفية الفعلية: -S/-c بلا خلفية صريحة = native (السلوك الأصلي)
    if (o.backend.empty()) o.backend = (o.emitC || o.compileOnly) ? "native" : "interp";
    if (o.emitC && o.compileOnly) {
        std::cerr << tag() << " " << errWord() << " " << T("-S and -c cannot be combined", "لا يمكن الجمع بين -S و -c") << "\n";
        return 1;
    }
    if (o.backend == "interp" && (o.emitC || o.compileOnly)) {
        std::cerr << tag() << " " << errWord() << " "
                  << T("-S/-c do not apply to the interp backend (use -fnative, -fvm or -fengine)",
                       "-S/-c لا تنطبق على خلفية interp (استخدم -fnative أو -fvm أو -fengine)") << "\n";
        return 1;
    }

    // المدخلات
    if (o.inputs.empty()) fatalNoInput();
    if (o.inputs.size() > 1) {
        std::cerr << tag() << " " << errWord() << " "
                  << T("exactly one Rin source file is supported per build (got ",
                       "يُدعَم ملف Rin واحد فقط لكل عملية بناء (المُعطى ")
                  << o.inputs.size() << ")\n";
        return 1;
    }
    o.input = o.inputs[0];
    bool forcedRin = o.forcedLang == "rin";
    if (!o.forcedLang.empty() && o.forcedLang != "rin" && o.forcedLang != "none") {
        std::cerr << tag() << " " << errWord() << " " << T("unknown language '", "لغة غير معروفة '") << o.forcedLang << "' ("
                  << T("only 'rin' is supported", "المدعوم 'rin' فقط") << ")\n";
        return 1;
    }
    if (!forcedRin && extOf(o.input) != ".rin") {
        std::cerr << tag() << " " << errWord() << " " << o.input << ": "
                  << T("unrecognized file type (expected .rin; use -x rin to force)",
                       "نوع ملف غير معروف (المتوقع .rin؛ استخدم -x rin للإجبار)") << "\n";
        return 1;
    }
    if (!readFile(o.input, ctx.source)) {
        std::cerr << tag() << " " << errWord() << " " << o.input << ": " << std::strerror(errno) << "\n"
                  << tag() << " " << fatalWord() << " " << T("no input files", "لا توجد ملفات إدخال") << "\n"
                  << T("compilation terminated.", "تم إنهاء البناء.") << "\n";
        return 1;
    }
    ctx.base = stem(o.input);

    // ---------------------------------------------------------- المرحلة 1: الفحص (+ التحليل)
    // native/engine لا يحتاجان `rin` للبناء نفسه؛ فإن غاب نتخطى الفحص بتحذير بدل الفشل.
    const bool runtimeOptional = o.backend == "native" || o.backend == "engine" || o.backend == "go" || o.backend == "auto";
    if (ctx.runtime.empty() && !o.dryRun) {
        if (!runtimeOptional || o.analyze || o.syntaxOnly) {
            std::cerr << tag() << " " << fatalWord() << " "
                      << T("cannot find the Rin runtime ('rin'). Install it next to rin_ava, set RIN_AVA_RUNTIME, or use --runtime=<path>",
                           "تعذّر إيجاد runtime Rin ('rin'). ثبّته بجانب rin_ava أو اضبط RIN_AVA_RUNTIME أو استخدم --runtime=<path>")
                      << "\n";
            return 1;
        }
        if (!o.noWarn)
            std::cerr << tag() << " " << warnWord() << " "
                      << T("rin runtime not found: skipping the syntax check", "runtime ‏rin غير موجود: تخطّي فحص الصياغة") << "\n";
    } else {
        std::vector<std::string> cmd = {rtOrDefault(ctx), "check", o.input, "--format=short"};
        announce(o, cmd);
        if (!o.dryRun) {
            std::string out;
            int rc = runCmd(cmd, "", true, &out);
            if (rc == 127 || rc == 126 || rc == -1) {
                std::cerr << tag() << " " << errWord() << " " << T("failed to run: ", "تعذّر التشغيل: ") << joinCmd(cmd) << "\n";
                return 1;
            }
            if (rc != 0) {
                printDiagnostics(out, o.input, ctx.source);
                std::cerr << T("compilation terminated.", "تم إنهاء البناء.") << "\n";
                return 1;
            }
        }
        if (o.analyze) {
            if (runAnalyze(ctx) != 0) { std::cerr << T("compilation terminated.", "تم إنهاء البناء.") << "\n"; return 1; }
        }
    }
    if (o.syntaxOnly) return 0;

    // ---------------------------------------------------------- المرحلة 2: البناء
    if (o.backend == "interp") return buildInterp(ctx);
    if (o.backend == "vm") return buildVm(ctx);
    if (o.backend == "engine") return buildEngine(ctx);
    if (o.backend == "go") return buildGo(ctx);
    if (o.backend == "native") return buildNative(ctx, false, nullptr);

    // auto: native إن قبله rinc وبُني، وإلا المفسّر (دلالات كاملة)
    {
        std::string why;
        if (buildNative(ctx, true, &why) == 0) return 0;
        if (!o.noWarn)
            std::cerr << tag() << " " << T("note: native backend not used (", "ملاحظة: لم تُستخدم الخلفية native (")
                      << (why.empty() ? std::string("unsupported") : why) << T("); using the interpreter", ")؛ سيُستخدم المفسّر") << "\n";
        if (ctx.runtime.empty() && !o.dryRun) {
            // لا runtime C++: جرّب مفسّر Go إن وُجد (تنفيذي مستقل بنواة اللغة)
            std::string gw; if (!resolveGoRuntime(o, ctx.selfDir, false, &gw).empty() || !findGorinSrc(o, ctx.selfDir).empty()) {
                if (!o.noWarn) std::cerr << tag() << " " << T("note: rin runtime not found; trying the Go engine", "ملاحظة: runtime ‏rin غير موجود؛ تجربة محرك Go") << "\n";
                return buildGo(ctx);
            }
            return fail(T("cannot find the Rin runtime ('rin') for the interpreter fallback", "تعذّر إيجاد runtime ‏rin للرجوع إلى المفسّر"));
        }
        return buildInterp(ctx);
    }
}

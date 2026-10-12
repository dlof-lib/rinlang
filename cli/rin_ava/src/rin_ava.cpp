// ============================================================================
//  rin_ava — أداة بناء Rin بواجهة g++  (RIN_AVA)
// ============================================================================
//  rin_ava prog.rin -o prog      ← يحوّل برنامج Rin إلى ملف تنفيذي مستقل
//
//  لا لغة جديدة هنا: التحقق يتم بمحلّل Rin الأصلي (`rin check`) والتنفيذ بمفسّر
//  Rin الأصلي نفسه. rin_ava مجرد "سائق" (driver) كما في g++: يحلّل الخيارات،
//  يشغّل المراحل بالترتيب، ويعرض الأخطاء بصيغة  file:line: error: message.
//
//  المراحل:
//    1) فحص  : `rin check`  (نفس lexer/parser الحقيقيين)            [-fsyntax-only يتوقف هنا]
//    2) بناء : الخلفية الافتراضية "bundle": نسخة من runtime `rin` + البرنامج مُضمَّناً
//              (دلالات مطابقة 100% للمفسّر، لا تحتاج مترجم C على جهاز البناء)
//              الخلفية "native" (-fnative / -S / -c): rinc → C → cc (للمجموعة المدعومة فقط)
//
//  المتطلبات: C++17 + POSIX (Linux / macOS / WSL / MSYS2). بلا أي مكتبة خارجية.
// ============================================================================
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#ifndef RIN_AVA_VERSION
#define RIN_AVA_VERSION "1.0.0"
#endif

namespace {

const char kMagic[9] = "RINAVAP1";  // يجب أن يطابق cli/linux/src/rin_ava_payload.h

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
    bool compileOnly = false;      // -c
    bool emitC = false;            // -S
    bool syntaxOnly = false;       // -fsyntax-only
    bool native = false;           // -fnative
    bool verbose = false;          // -v
    bool dryRun = false;           // -###
    bool saveTemps = false;        // -save-temps
    bool noWarn = false;           // -w
    bool werror = false;           // -Werror
    int colorMode = -1;            // -1 auto, 0 never, 1 always
    std::string cc, rinc, runtime, engine;
    std::vector<std::string> ccFlags;   // تُمرَّر لمترجم C في الخلفية native فقط
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
"  -fnative           Use the native backend (rinc -> C -> cc) instead of bundling\n"
"  -S                 Emit the generated C source (implies -fnative)\n"
"  -c                 Compile to an object file (implies -fnative)\n"
"  -O0 -O1 -O2 -O3 -Os -Ofast -g   Passed to the C compiler (native backend only)\n"
"  -I<dir> -D<m> -U<m> -L<dir> -l<lib> -static -pthread -m<opt> -f<opt>\n"
"                     Passed to the C compiler (native backend only)\n"
"  -x rin             Treat the input as Rin source whatever its extension\n"
"  -save-temps        Keep intermediate files (generated C) in the current directory\n"
"  -v                 Show the commands being run\n"
"  -###               Show the commands without running them\n"
"  -w                 Suppress warnings        -Werror   Treat warnings as errors\n"
"  -Wall -Wextra -pedantic -std=... -pipe   Accepted and ignored (g++ compatibility)\n"
"  -fdiagnostics-color=[auto|always|never]\n"
"  --cc=<cc>  --rinc=<path>  --runtime=<rin>  --engine-src=<dir>\n"
"                     Choose tools explicitly (also RIN_AVA_CC / RIN_AVA_RINC / RIN_AVA_RUNTIME)\n"
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
bool bundle(const std::string& runtimePath, const std::string& displayName, const std::string& source,
            const std::string& outPath, std::string& err) {
    std::string rt;
    if (!readFile(runtimePath, rt)) { err = std::string(T("cannot read runtime: ", "تعذّر قراءة runtime: ")) + runtimePath; return false; }
    // لا تُلحِق مرتين إن كان runtime نفسه ناتج rin_ava
    if (rt.size() >= 16 && std::memcmp(rt.data() + rt.size() - 8, kMagic, 8) == 0) {
        err = T("runtime already contains a program", "الـ runtime يحوي برنامجاً مُضمَّناً بالفعل");
        return false;
    }
    std::string payload = displayName; payload.push_back('\0'); payload += source;
    std::string trailer(16, '\0');
    uint64_t len = payload.size();
    for (int i = 0; i < 8; ++i) trailer[i] = static_cast<char>((len >> (8 * i)) & 0xff);
    std::memcpy(&trailer[8], kMagic, 8);

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

}  // namespace

int main(int argc, char** argv) {
    {
        const char* l = std::getenv("RIN_AVA_LANG");
        const char* lg = std::getenv("LANG");
        g_ar = (l && std::strncmp(l, "ar", 2) == 0) || (!l && lg && std::strncmp(lg, "ar", 2) == 0);
    }
    Opts o;
    std::vector<std::string> args(argv + 1, argv + argc);
    bool endOpts = false;

    auto needArg = [&](size_t& i, const std::string& flag, std::string& out) {
        if (i + 1 >= args.size()) {
            std::cerr << tag() << " " << errWord() << " " << T("missing filename after '", "اسم الملف مفقود بعد '") << flag << "'\n";
            std::exit(1);
        }
        out = args[++i];
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
        else if (a == "-fnative") o.native = true;
        else if (a == "-fno-native" || a == "-fbundle") o.native = false;
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

    if (o.emitC || o.compileOnly) o.native = true;
    if (o.emitC && o.compileOnly) {
        std::cerr << tag() << " " << errWord() << " " << T("-S and -c cannot be combined", "لا يمكن الجمع بين -S و -c") << "\n";
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
    std::string source;
    if (!readFile(o.input, source)) {
        std::cerr << tag() << " " << errWord() << " " << o.input << ": " << std::strerror(errno) << "\n"
                  << tag() << " " << fatalWord() << " " << T("no input files", "لا توجد ملفات إدخال") << "\n"
                  << T("compilation terminated.", "تم إنهاء البناء.") << "\n";
        return 1;
    }

    const std::string selfDir = exeDir(argv[0]);
    const std::string runtime = findTool("rin", o.runtime, "RIN_AVA_RUNTIME", selfDir);
    const std::string rincPath = findTool("rinc", o.rinc, "RIN_AVA_RINC", selfDir);

    // ---------------------------------------------------------- المرحلة 1: الفحص
    if (runtime.empty() && !o.dryRun) {
        std::cerr << tag() << " " << fatalWord() << " "
                  << T("cannot find the Rin runtime ('rin'). Install it next to rin_ava, set RIN_AVA_RUNTIME, or use --runtime=<path>",
                       "تعذّر إيجاد runtime Rin ('rin'). ثبّته بجانب rin_ava أو اضبط RIN_AVA_RUNTIME أو استخدم --runtime=<path>")
                  << "\n";
        return 1;
    }
    {
        std::vector<std::string> cmd = {runtime.empty() ? "rin" : runtime, "check", o.input, "--format=short"};
        announce(o, cmd);
        if (!o.dryRun) {
            std::string out;
            int rc = runCmd(cmd, "", true, &out);
            if (rc == 127 || rc == 126 || rc == -1) {
                std::cerr << tag() << " " << errWord() << " " << T("failed to run: ", "تعذّر التشغيل: ") << joinCmd(cmd) << "\n";
                return 1;
            }
            if (rc != 0) {
                printDiagnostics(out, o.input, source);
                std::cerr << T("compilation terminated.", "تم إنهاء البناء.") << "\n";
                return 1;
            }
        }
    }
    if (o.syntaxOnly) return 0;

    // ---------------------------------------------------------- المرحلة 2: البناء
    const std::string base = stem(o.input);

    if (!o.native) {
        std::string out = o.output.empty() ? "a.out" : o.output;
        if (!o.dryRun) warnImports(o, o.input, source);
        std::vector<std::string> plan = {"<bundle>", runtime.empty() ? "rin" : runtime, "+", o.input, "->", out};
        announce(o, plan);
        if (o.dryRun) return 0;
        std::string err;
        if (!bundle(runtime, baseName(o.input), source, out, err)) {
            std::cerr << tag() << " " << errWord() << " " << err << "\n";
            return 1;
        }
        return 0;
    }

    // الخلفية native: rinc → C → cc
    if (rincPath.empty() && !o.dryRun) {
        std::cerr << tag() << " " << fatalWord() << " "
                  << T("cannot find 'rinc' (needed by -fnative/-S/-c). Set RIN_AVA_RINC or use --rinc=<path>",
                       "تعذّر إيجاد 'rinc' (مطلوب لـ -fnative/-S/-c). اضبط RIN_AVA_RINC أو استخدم --rinc=<path>")
                  << "\n";
        return 1;
    }
    std::string tmp = o.dryRun ? "/tmp/rin_ava.DRYRUN" : makeTempDir();
    if (tmp.empty()) { std::cerr << tag() << " " << errWord() << " " << T("cannot create a temporary directory", "تعذّر إنشاء مجلد مؤقت") << "\n"; return 1; }
    auto cleanup = [&]() { if (!o.dryRun) removeTree(tmp); };

    char rp[4096];
    std::string absInput = ::realpath(o.input.c_str(), rp) ? std::string(rp) : o.input;
    std::vector<std::string> rcmd = {rincPath.empty() ? "rinc" : rincPath, absInput, "--emit-c-only", "--no-embed-fallback"};
    announce(o, rcmd);
    std::string cfile = tmp + "/" + base + ".c";
    if (!o.dryRun) {
        std::string out;
        int rc = runCmd(rcmd, tmp, true, &out);
        if (rc != 0) {
            printDiagnostics(out, o.input, source);
            std::cerr << tag() << " " << T("hint: the default backend accepts every valid Rin program; drop -fnative/-S/-c",
                                            "تلميح: الخلفية الافتراضية تقبل كل برنامج Rin صالح؛ احذف -fnative/-S/-c") << "\n";
            cleanup();
            return 1;
        }
        if (!fileExists(cfile)) {
            std::cerr << tag() << " " << errWord() << " " << T("rinc did not produce a C file", "لم ينتج rinc ملف C") << "\n";
            cleanup();
            return 1;
        }
    }

    if (o.emitC) {
        std::string dest = o.output.empty() ? base + ".c" : o.output;
        if (!o.dryRun) {
            std::string c; readFile(cfile, c);
            if (dest == "-") std::cout << c;
            else { std::ofstream f(dest, std::ios::binary); f << c; if (!f) { std::cerr << tag() << " " << errWord() << " " << dest << ": " << std::strerror(errno) << "\n"; cleanup(); return 1; } }
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
    bool hasOpt = false;
    for (auto& f : o.ccFlags) { if (f.rfind("-O", 0) == 0) hasOpt = true; }
    if (!hasOpt) ccmd.push_back("-O0");
    if (o.compileOnly) ccmd.push_back("-c");
    for (auto& f : o.ccFlags) ccmd.push_back(f);
    std::string out = o.output.empty() ? (o.compileOnly ? base + ".o" : "a.out") : o.output;
    ccmd.push_back("-o"); ccmd.push_back(out);
    ccmd.push_back(cfile);
    if (!o.compileOnly) { for (auto& l : o.libs) ccmd.push_back(l); ccmd.push_back("-lm"); }
    announce(o, ccmd);
    int rc = 0;
    if (!o.dryRun) {
        rc = runCmd(ccmd, "", false, nullptr);
        if (o.saveTemps) {
            std::string c; readFile(cfile, c);
            std::ofstream f(base + ".c", std::ios::binary); f << c;
        }
    }
    cleanup();
    if (rc != 0) {
        std::cerr << tag() << " " << errWord() << " " << T("the C backend failed (use -save-temps to inspect the generated C)",
                                                         "فشلت خلفية C (استخدم -save-temps لفحص كود C المولَّد)") << "\n";
        return 1;
    }
    return 0;
}

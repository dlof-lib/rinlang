// ============================================================================
//  rin — Rin Language Unified CLI (Linux)
// ============================================================================
//  نقطة الدخول الموحّدة لأدوات Rin على لينكس. لا يعيد هذا الملف تنفيذ أي محرك:
//  التشغيل/REPL/الفحص تستخدم نفس محرك المفسّر الحقيقي (rin_lexer/rin_parser/
//  rin_interpreter) المُستخدَم في تطبيق أندرويد، والبناء التنفيذي (`rin build`)
//  يستدعي المترجم الحقيقي rinc (compiler/rinc.cpp) كعملية فرعية — لا يوجد أي
//  محاكاة أو أوامر وهمية هنا: كل أمر إما يعمل فعلياً أو يطبع بوضوح أنه غير مدعوم.
//
//  الأوامر:
//    rin new <name> [--template console]   إنشاء مشروع Rin جديد
//    rin build [file] [-o out] [--release]  بناء تنفيذي أصلي (عبر rinc)
//    rin run [file]                         تشغيل برنامج Rin
//    rin check <file> [--format=plain|short|json|lsp]
//    rin test [dir]                         تشغيل اختبارات .rin
//    rin fmt <file> [--write]               إعادة محاذاة المسافات البادئة
//    rin clean                              حذف مخرجات ./build
//    rin doctor                             فحص بيئة التطوير الفعلية
//    rin -c "print 1+1;"                    تشغيل كود مباشر
//    rin < file.rin                          قراءة الكود من stdin
//    rin                                     الطرفية التفاعلية
//    rin terminal|repl|shell [خيارات] [ملفات]  الطرفية التفاعلية الكاملة
//    rin --version | -v ، --help | -h
// ============================================================================
#include "rin_version.h"
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "rin_bcvm.h"
#include "diagnostics/diagnostic_renderer.h"
#include "diagnostics/source_manager.h"
#include "pkg/cli_pkg.h"
#include "toolchain/rin_toolchain.h"
#include "terminal/rin_terminal.h"
#include "rin_ava_payload.h"
#include "rin_vm.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <climits>

namespace {

// رقم إصدار موحَّد يأتي من rin_version.h (المصدر الرسمي الوحيد) — كان قبل
// هذا الملف رقماً منفصلاً هنا (0.2.0) اختلف بالصدفة عن macOS/Windows
// (0.1.0/0.1.0) رغم أنها نفس أداة rin CLI. انظر docs/VERSIONING.md.
constexpr const char* kVersion = RIN_VERSION_STRING;

// ---------------------------------------------------------------------------
// أدوات عامة
// ---------------------------------------------------------------------------
std::string readAll(std::istream& in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out = readAll(f);
    return true;
}

bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << content;
    return true;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

bool isDir(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool makeDir(const std::string& path) {
    return ::mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

// مسار مجلّد الملف التنفيذي الحالي (rin نفسه) — يُستخدم لإيجاد rinc بجانبه
// عند تثبيت أدوات Rin معاً في نفس مجلد bin/.
std::string exeDir() {
    char buf[PATH_MAX];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string full(buf);
    auto pos = full.find_last_of('/');
    return pos == std::string::npos ? "" : full.substr(0, pos);
}

// ينفّذ عملية فرعية وينتظر خروجها، يعيد رمز الخروج (أو -1 إن تعذّر التشغيل).
int runSubprocess(const std::vector<std::string>& args) {
    if (args.empty()) return -1;
    std::vector<char*> cargs;
    cargs.reserve(args.size() + 1);
    for (auto& a : args) cargs.push_back(const_cast<char*>(a.c_str()));
    cargs.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(cargs[0], cargs.data());
        std::cerr << "rin: تعذّر تشغيل '" << args[0] << "': " << std::strerror(errno) << "\n";
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// يبحث عن rinc: بجانب rin نفسه أولاً (توزيع مُثبّت)، ثم RIN_HOME/bin، ثم PATH.
std::string findRinc() {
    std::string dir = exeDir();
    if (!dir.empty() && fileExists(dir + "/rinc")) return dir + "/rinc";
    const char* home = std::getenv("RIN_HOME");
    if (home && fileExists(std::string(home) + "/bin/rinc")) return std::string(home) + "/bin/rinc";
    return "rinc"; // يعتمد على execvp للبحث في PATH؛ يفشل بوضوح إن لم يوجد
}

// ---------------------------------------------------------------------------
// rin.toml — محلّل بسيط وحقيقي (key = "value" داخل أقسام [section])
// لا يدّعي دعم TOML كاملاً؛ يكفي لما يحتاجه rin فعلياً حالياً.
// ---------------------------------------------------------------------------
struct RinToml {
    std::map<std::string, std::map<std::string, std::string>> sections;

    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const {
        auto s = sections.find(section);
        if (s == sections.end()) return def;
        auto k = s->second.find(key);
        return k == s->second.end() ? def : k->second;
    }
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

RinToml parseToml(const std::string& content) {
    RinToml toml;
    std::string section = "";
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        toml.sections[section][key] = val;
    }
    return toml;
}

// يصعد من المجلد الحالي بحثاً عن rin.toml (جذر مشروع Rin). فارغ إن لم يوجد.
std::string findProjectRoot() {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return "";
    std::string dir(cwd);
    for (int i = 0; i < 64 && !dir.empty(); ++i) {
        if (fileExists(dir + "/rin.toml")) return dir;
        auto pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0) break;
        dir = dir.substr(0, pos);
    }
    return "";
}

void printUsage() {
    std::cout <<
        "Rin Language CLI - v" << kVersion << "\n\n"
        "الاستخدام:\n"
        "  rin new <name> [--template console]   إنشاء مشروع Rin جديد\n"
        "  rin pkg <command> [...]                RinPM: مدير حزم Rin (rin pkg --help)\n"
        "  rin --allow-native file.rin            تفعيل جسر C++ (cpp.*) لهذا التشغيل\n"
        "  rin build [file] [-o out] [--release]  بناء تنفيذي أصلي (عبر rinc)\n"
        "  rin run [file] [--import-progress] [--time]  تشغيل برنامج Rin (شريط تحميل لـ @import، --time يعرض زمن التشغيل)\n"
        "  rin check <file> [--format=plain|short|json|lsp]\n"
        "  rin analyze <file>                      semantic analyzer + type checker\n"
        "  rin bytecode <file> [--optimize] [--out file.rbc]  compile/disassemble bytecode\n"
        "  rin vm <file> [--disasm] [--stats]      run on RinVM (real bytecode VM)\n"
        "  rin --vm <file>                         run any program on RinVM\n"
        "  rin toolchain-new <name>                generate a complete toolchain project skeleton\n"
        "  rin lib <new|check|info|list|run>      مكتبات Rin المقسَّمة (.og.rin: Disconom + .zpp + .rinz) — rin lib --help\n"
        "  rin test [dir]                         تشغيل اختبارات .rin\n"
        "  rin fmt <file> [--write]               إعادة محاذاة المسافات البادئة\n"
        "  rin clean                              حذف مخرجات ./build\n"
        "  rin doctor                             فحص بيئة التطوير\n"
        "  rin -c \"print 1+1;\"                    تشغيل كود مباشر\n"
        "  rin < file.rin                          قراءة الكود من stdin\n"
        "  rin                                     الطرفية التفاعلية (rin terminal --help)\n"
        "  rin terminal [-i file] [-e code] [...]  الطرفية التفاعلية الكاملة (أو rin repl / rin shell)\n"
        "  rin indsin <file.rin> [--width N] [--dump]  تشغيل واجهة indsin داخل الطرفية\n"
        "  rin img <image> [--mode half|braille|shade|ascii] [--width N]  عرض صورة بحروف Unicode في الطرفية\n"
        "  rin --version | -v ، --help | -h\n"
        "  rin [--max-depth=N] [--budget=N] file.rin   حدود وقت التشغيل (docs/runtime.md)\n";
}

// ---------------------------------------------------------------------------
// تشغيل عبر المفسّر (نفس محرك rin_interpreter الحقيقي)
// ---------------------------------------------------------------------------
bool runSource(const std::string& source, const std::string& sourceName, rin::Interpreter& interp,
               bool printOutput = true) {
    try {
        rin::Lexer lexer(source, sourceName);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, sourceName);
        auto statements = parser.parse();
        interp.setSourceFile(sourceName);
        std::string out = interp.run(statements);
        if (printOutput) std::cout << out;
        // interp.run() catches RinError/ThrowSignal/ReturnSignal internally and appends the
        // rendered diagnostic to `out` instead of rethrowing -- so a runtime failure never
        // reaches the catch clauses below. hadError() is the interpreter's own accessor for
        // exactly this case (see rin_interpreter.h): without checking it, this function
        // reported success for any script that merely parsed, even if it failed at runtime.
        return !interp.hadError();
    } catch (rin::RinError& e) {
        if (e.diagnostic) {
            std::cerr << "\n" << rin::diag::renderPlain(*e.diagnostic, rin::diag::globalSourceManager());
        } else {
            std::cerr << "\n[rin] خطأ في " << sourceName << " عند السطر " << e.line << ": " << e.message << "\n";
        }
        return false;
    } catch (std::exception& e) {
        std::cerr << "\n[rin] خطأ داخلي: " << e.what() << "\n";
        return false;
    }
}

int runCheck(const std::string& path, rin::diag::OutputFormat fmt) {
    std::string source;
    if (!readFile(path, source)) {
        std::cerr << "rin check: تعذّر فتح الملف '" << path << "'\n";
        return 2;
    }
    rin::diag::DiagnosticEngine engine;
    try {
        rin::Lexer lexer(source, path);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, path);
        parser.parseCollectingDiagnostics(engine);
    } catch (rin::RinError& e) {
        if (e.diagnostic) engine.emit(*e.diagnostic);
    }
    std::cout << rin::diag::renderAll(engine, rin::diag::globalSourceManager(), fmt);
    return engine.hasErrors() ? 1 : 0;
}

// REPL التفاعلي = الطرفية الكاملة (terminal/rin_terminal.cpp): تحرير سطر حقيقي، تاريخ دائم، إكمال Tab،
// تلوين صيغة، أسطر متتابعة، وحالة جلسة تبقى بين الأوامر. (كان هنا سابقاً حلقة getline من سطر واحد.)
int runRepl() {
    rin::terminal::Options o;
    o.version = kVersion;
    return rin::terminal::run(o);
}

// ---------------------------------------------------------------------------
// rin new <name>
// ---------------------------------------------------------------------------
int cmdNew(const std::string& name) {
    if (name.empty()) {
        std::cerr << "rin new: يلزم اسم المشروع. مثال: rin new hello\n";
        return 2;
    }
    if (fileExists(name)) {
        std::cerr << "rin new: المسار '" << name << "' موجود مسبقاً\n";
        return 1;
    }
    if (!makeDir(name) || !makeDir(name + "/src") || !makeDir(name + "/tests") || !makeDir(name + "/assets")) {
        std::cerr << "rin new: تعذّر إنشاء بنية المشروع\n";
        return 1;
    }
    std::ostringstream toml;
    toml << "[package]\n"
         << "name = \"" << name << "\"\n"
         << "version = \"0.1.0\"\n"
         << "edition = \"2026\"\n\n"
         << "[dependencies]\n";
    writeFile(name + "/rin.toml", toml.str());

    writeFile(name + "/src/main.rin",
        "// نقطة الدخول - " + name + "\n"
        "print \"Hello from Rin!\";\n");

    writeFile(name + "/tests/basic.rin",
        "// اختبار أساسي: ينجح إن لم يُطلق أي خطأ أثناء التنفيذ.\n"
        "let x = 2 + 2;\n"
        "if (x != 4) { print \"FAIL: 2+2 != 4\"; } else { print \"OK\"; }\n");

    writeFile(name + "/README.md",
        "# " + name + "\n\nمشروع Rin.\n\n```\nrin build\n./" + name + "\nrin run\nrin test\n```\n");

    std::cout << "تم إنشاء مشروع Rin: " << name << "/\n"
              << "  " << name << "/rin.toml\n"
              << "  " << name << "/src/main.rin\n"
              << "  " << name << "/tests/basic.rin\n"
              << "  " << name << "/README.md\n\n"
              << "التالي:\n  cd " << name << " && rin build && ./" << name << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// rin build [file] [-o out] [--release]
// ---------------------------------------------------------------------------
int cmdBuild(const std::vector<std::string>& args) {
    std::string file, out;
    bool release = false;
    std::string root = findProjectRoot();
    std::string projName;
    if (!root.empty()) {
        std::string tomlSrc;
        readFile(root + "/rin.toml", tomlSrc);
        RinToml toml = parseToml(tomlSrc);
        projName = toml.get("package", "name", "app");
        file = root + "/src/main.rin";
    }

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-o" && i + 1 < args.size()) { out = args[++i]; }
        else if (args[i] == "--release") { release = true; }
        else if (args[i] == "--debug") { release = false; }
        else if (!args[i].empty() && args[i][0] != '-') { file = args[i]; }
    }

    if (file.empty()) {
        std::cerr << "rin build: لا يوجد ملف مصدر. حدّده أو شغّل داخل مشروع (يحتوي rin.toml)\n";
        return 2;
    }
    if (!fileExists(file)) {
        std::cerr << "rin build: الملف غير موجود: " << file << "\n";
        return 2;
    }

    std::string buildDir = root.empty() ? "build" : root + "/build";
    makeDir(buildDir);
    if (out.empty()) {
        out = buildDir + "/" + (!projName.empty() ? projName : "a.out");
    }

    std::string rinc = findRinc();
    std::vector<std::string> cmd = {rinc, file, "-o", out};
    (void)release; // rinc الحالي لا يميّز release/debug بعد؛ محجوز للتوسعة القادمة دون كسر الواجهة

    std::cout << "[rin build] " << rinc << " " << file << " -o " << out << "\n";
    auto buildT0 = std::chrono::steady_clock::now();
    int rc = runSubprocess(cmd);
    double buildSecs = std::chrono::duration<double>(std::chrono::steady_clock::now() - buildT0).count();
    if (rc == 127 || rc == -1) {
        std::cerr << "rin build: تعذّر إيجاد/تشغيل rinc. تأكد من تثبيته بجانب rin أو في PATH\n";
        return 1;
    }
    {
        // ملخّص Task::Build: الأمر الفرعي (rinc) يكتب في نفس الطرفية فلا يصلح مؤشر حي هنا؛
        // نعرض النتيجة الحقيقية: الزمن المقاس وحجم الناتج الفعلي.
        std::string text = out;
        struct stat st{};
        if (rc == 0 && ::stat(out.c_str(), &st) == 0) text += "  \xC2\xB7  " + rin::loaderui::formatBytes(static_cast<unsigned long long>(st.st_size));
        if (rc != 0) text = "rinc exit " + std::to_string(rc);
        rin::loaderui::report(std::cout, rin::loaderui::profileFor(rin::loaderui::Task::Build), rc == 0, text, buildSecs);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// مكتبات Rin المقسَّمة (<name>.og.rin/ : Disconom + .zpp + .rinz + native/) — docs/split-libraries.md
//   rin lib new <name> [--dir D] [--xml] [--native] [--version V] [--author A] [--desc T]
//   rin lib check <name|dir>     rin lib info <name|dir>     rin lib list [dir]     rin lib run <name|dir>
// ---------------------------------------------------------------------------
// ملف .rinz داخل مكتبة: يُشغَّل وحده بعد ربط أجزاء مكتبته (لا يُنفَّذ مرتين). يُعيد بادئة @import على نفس السطر الأول.
static std::string rinzPrelude(const std::string& file, rin::Interpreter& interp) {
    if (file.size() < 5 || file.compare(file.size() - 5, 5, ".rinz") != 0) return "";
    char buf[PATH_MAX];
    if (!realpath(file.c_str(), buf)) return "";
    std::string dir = buf;
    for (int i = 0; i < 16; ++i) {
        size_t k = dir.find_last_of('/');
        if (k == std::string::npos || k == 0) break;
        dir = dir.substr(0, k);
        struct stat st;
        if (stat((dir + "/disconom.json").c_str(), &st) == 0 || stat((dir + "/disconom.xml").c_str(), &st) == 0) {
            interp.setSkipLibraryRun(true);
            return "@import \"" + dir + "\"; ";
        }
    }
    return "";
}

static bool resolveLibArg(const std::string& arg, rin::libdir::PackageRef& ref) {
    rin::Interpreter probe;
    if (probe.resolvePackage(arg, ref)) return true;
    std::string root;   // مجلد صريح بلا لاحقة .og.rin وبلا Disconom (يُقبل لـ check/info)
    std::error_code ec;
    if (std::filesystem::is_directory(arg, ec)) { ref.kind = rin::libdir::PackageKind::Library; ref.root = arg; return true; }
    return false;
}

int cmdLib(const std::vector<std::string>& args) {
    namespace ld = rin::libdir;
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        std::cout <<
            "rin lib — الحزم: «مكتبة» (مجلد <name>.og.rin: Disconom + .zpp + .rinz + native/) و«توكن أداة OG» (ملف واحد <rinname>.og.rin)\n"
            "  rin lib new <name> [--dir D] [--xml] [--native] [--version V] [--author A] [--desc T]   إنشاء مكتبة جديدة\n"
            "  rin lib check <name|dir>   فحص (مكتبة: Disconom + تحليل كل .zpp/.rinz + التبعيات المتعدّية؛ توكن: صياغة) — رمز 1 عند الأخطاء\n"
            "  rin lib info  <name|dir>   وصف الحزمة وترتيب تحميل أجزائها\n"
            "  rin lib kind  <name>       نوع الحزمة: library | tool-token\n"
            "  rin lib list  [dir]        سرد المكتبات (المجلدات) في مجلد (الافتراضي lib)\n"
            "  rin lib tokens [dir]       سرد توكنات الأداة OG (الملفات المفردة + المدمجة في المفسّر)\n"
            "  rin lib packages [dir]     كل الحزم (مكتبات + توكنات)\n"
            "  rin lib graph <name|dir>   رسم التبعيات المتعدّية بترتيب التحميل (يكشف الدورات وعدم توافق الإصدارات)\n"
            "  rin lib fingerprint <name|dir>   بصمة SHA-256 لمحتوى الحزمة\n"
            "  rin lib run   <name|dir>   استيراد المكتبة وتنفيذ ملفات .rinz (أضف --allow-native للكود C++)\n";
        return 0;
    }
    const std::string sub = args[0];
    if (sub == "new") {
        ld::ScaffoldOpts o; std::string dir = "lib";
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            auto val = [&](std::string& dst) { if (i + 1 < args.size()) dst = args[++i]; };
            if (a == "--dir") val(dir); else if (a == "--xml") o.xml = true; else if (a == "--native") o.native = true;
            else if (a == "--version") val(o.version); else if (a == "--author") val(o.author); else if (a == "--desc") val(o.description);
            else if (a == "--license") val(o.license);
            else if (!a.empty() && a[0] != '-') o.name = a;
        }
        if (o.name.empty()) { std::cerr << "rin lib new: يلزم اسم المكتبة\n"; return 2; }
        std::string root, err; std::vector<std::string> created;
        if (!ld::scaffoldLibrary(dir, o, kVersion, root, created, err)) { std::cerr << "rin lib new: " << err << "\n"; return 1; }
        std::cout << "تم إنشاء مكتبة " << root << "\n";
        for (auto& f : created) std::cout << "  + " << f << "\n";
        return 0;
    }
    if (sub == "list" || sub == "tokens" || sub == "packages") {
        std::string dir = args.size() > 1 ? args[1] : "lib";
        const char* fn = sub == "list" ? "discover" : sub == "tokens" ? "tokens" : "packages";
        rin::Interpreter in;
        std::string src = std::string("for (let l in rinlib.") + fn + "(\"" + dir + "\") { let v = l.version; if (v == nil) { v = \"\"; } " +
            "let d = l.description; if (d == nil) { d = \"\"; } print l.name + \" [\" + l.kind + \"] \" + v + \"  \" + l.root + (d == \"\" ? \"\" : \"  — \" + d); }";
        return runSource(src, "<rin lib " + sub + ">", in) ? 0 : 1;
    }
    if (args.size() < 2) { std::cerr << "rin lib " << sub << ": يلزم اسم الحزمة أو مسارها\n"; return 2; }
    ld::PackageRef ref;
    if (!resolveLibArg(args[1], ref)) { std::cerr << "rin lib " << sub << ": لم أجد مكتبة ولا توكن أداة OG باسم '" << args[1] << "'\n"; return 1; }
    const std::string root = ref.root;
    if (sub == "kind") { std::cout << ld::kindName(ref.kind) << "  (" << ld::kindLabelAr(ref.kind) << ", " << (ref.source.empty() ? "disk" : ref.source) << ")  " << root << "\n"; return 0; }
    if (sub == "fingerprint") {
        rin::Interpreter in; std::string fp = in.libraryFingerprint(root);
        if (fp.empty()) { std::cerr << "rin lib fingerprint: تعذّر حساب البصمة\n"; return 1; }
        std::cout << fp << "  " << root << "\n"; return 0;
    }
    if (sub == "graph") {
        rin::Interpreter in; auto g = in.dependencyGraph(root);
        int i = 1;
        for (auto& n : g.nodes) {
            std::cout << "  " << i++ << ". " << n.name << (n.version.empty() ? "" : " " + n.version) << "  [" << ld::kindName(n.kind) << "]"
                      << (n.range.empty() ? "" : "  يطلب " + n.range) << (n.found ? "" : "  (غير موجودة)") << (n.satisfied ? "" : "  (إصدار غير مطابق)") << "\n";
            for (auto& d : n.deps) std::cout << "       └─ " << d << "\n";
        }
        for (auto& c : g.cycles) std::cout << "  دورة: " << c << "\n";
        for (auto& p : g.problems) std::cout << "  خطأ: " << p << "\n";
        for (auto& m : g.missing) std::cout << "  تحذير: " << m << "\n";
        std::cout << (g.ok() ? "OK" : "FAILED") << "\n";
        return g.ok() ? 0 : 1;
    }
    if (sub == "check") {
        rin::Interpreter in; auto rep = in.checkLibraryDir(root);
        std::cout << root << "  [" << ld::kindName(ref.kind) << "]  " << rep.name << (rep.version.empty() ? "" : " " + rep.version) << "\n";
        for (auto& p : rep.parts) std::cout << "  part  " << p << "\n";
        for (auto& p : rep.runFiles) std::cout << "  run   " << p << "\n";
        if (rep.loadOrder.size() > 1) { std::cout << "  load  "; for (size_t i = 0; i < rep.loadOrder.size(); ++i) std::cout << (i ? " → " : "") << rep.loadOrder[i]; std::cout << "\n"; }
        if (!rep.fingerprint.empty()) std::cout << "  " << rep.fingerprint << "\n";
        for (auto& w : rep.warnings) std::cout << "  تحذير: " << w << "\n";
        for (auto& e : rep.errors) std::cout << "  خطأ: " << e << "\n";
        std::cout << (rep.ok ? "OK" : "FAILED") << "\n";
        return rep.ok ? 0 : 1;
    }
    if (sub == "info") {
        if (ref.kind == ld::PackageKind::ToolToken) {
            std::cout << ref.name << "  [توكن أداة OG]  " << root << "  (" << ref.source << ", " << ref.bytes << " bytes)\n"
                      << "  ملف واحد بلا Disconom — لتحويله إلى مكتبة بإصدار وتبعيات: rin lib new " << ref.name << "\n";
            return 0;
        }
        ld::Plan plan; std::vector<std::string> errs;
        ld::planLibrary(ld::diskFS(), root, "", plan, errs);
        const auto& m = plan.manifest;
        std::cout << m.name << " " << m.version << "  [مكتبة]  (" << plan.root << ")\n";
        if (!m.description.empty()) std::cout << "  " << m.description << "\n";
        std::cout << "  Disconom: " << (plan.hasManifest ? plan.manifestFile : "—") << "   Rin: " << (m.rin.empty() ? "—" : m.rin) << "\n";
        for (auto& d : m.dependencies) std::cout << "  dep   " << d.name << " " << d.range << "\n";
        int i = 1; for (auto& p : plan.parts) std::cout << "  " << i++ << ". part " << p << "\n";
        for (auto& p : plan.runFiles) std::cout << "  " << i++ << ". run  " << p << "\n";
        for (auto& s : m.native.sources) std::cout << "  native " << s << "\n";
        for (auto& e : errs) std::cout << "  خطأ: " << e << "\n";
        return errs.empty() ? 0 : 1;
    }
    if (sub == "run") {
        rin::Interpreter in;
        return runSource("@import \"" + root + "\";", "<rin lib run>", in) ? 0 : 1;
    }
    std::cerr << "rin lib: أمر غير معروف '" << sub << "' (rin lib --help)\n";
    return 2;
}

// ---------------------------------------------------------------------------
// rin run [file]
// ---------------------------------------------------------------------------
int cmdRun(const std::vector<std::string>& args) {
    std::string file;
    bool importProgress = false;
    bool showTime = false;
    for (auto& a : args) {
        if (a == "--import-progress") importProgress = true;
        else if (a == "--time") showTime = true;
        else if (!a.empty() && a[0] != '-') file = a;
    }
    if (file.empty()) {
        std::string root = findProjectRoot();
        if (!root.empty()) file = root + "/src/main.rin";
    }
    if (file.empty()) {
        std::cerr << "rin run: لا يوجد ملف. حدّده أو شغّل داخل مشروع Rin\n";
        return 2;
    }
    std::string source;
    if (!readFile(file, source)) {
        std::cerr << "rin run: تعذّر فتح الملف '" << file << "'\n";
        return 2;
    }
    rin::Interpreter interp;
    source = rinzPrelude(file, interp) + source;   // ملف .rinz داخل مكتبة: اربط أجزاءها أولاً
    // --import-progress اختياري تماماً: بدونه السلوك مطابق تماماً لما كان عليه قبل هذه الميزة
    // (لا شريط تحميل، لا أي فرق في الناتج). راجع rin::loaderui في app/src/main/cpp/loader_ui/.
    if (importProgress) interp.setImportUIMode(rin::loaderui::Mode::Verbose);
    auto runT0 = std::chrono::steady_clock::now();
    // Task::Run — مؤشر حيّ مرتبط بالتشغيل الفعلي. يظهر فقط للتشغيلات الأطول من ~0.35s وعلى طرفية حقيقية
    // (stderr tty) والبرنامج صامت، ويختفي عند أي مخرجات/input()، ويعرض الاستيراد الجاري. خارج الطرفية
    // (pipe/ملف/CI)، أو مع --import-progress (شريطه الخاص)، أو حين stdin ليس طرفية: لا يُفعَّل وتبقى
    // المسارات القديمة كما كانت حرفياً (بما فيها طباعة الناتج دفعة واحدة).
    rin::loaderui::RunIndicator::Options runOpts;
    runOpts.hint = "Ctrl-C to stop";
    runOpts.forceInactive = importProgress || !isatty(fileno(stdin));
    rin::loaderui::RunIndicator runInd(std::cerr, rin::loaderui::profileFor(rin::loaderui::Task::Run), file, /*fd=*/2, runOpts);
    const bool liveRun = runInd.active();
    if (liveRun) {
        interp.setStreamSink([&runInd](const std::string& chunk) {
            runInd.emit(chunk, [](const std::string& s) { std::cout << s << std::flush; });
        });
        interp.setImportUISink(runInd.importSink());
        interp.setInputProvider(runInd.inputProvider());
    }
    bool ok = runSource(source, file, interp, /*printOutput=*/!liveRun);
    runInd.stop();
    if (showTime) {
        // Task::Run: ملخّص الزمن الفعلي على stderr (لا يلوّث ناتج البرنامج على stdout).
        rin::loaderui::report(std::cerr, rin::loaderui::profileFor(rin::loaderui::Task::Run), ok,
            file + (ok ? "  finished" : "  failed"),
            std::chrono::duration<double>(std::chrono::steady_clock::now() - runT0).count());
    }
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------------------
// rin test [dir]  — يشغّل كل ملفات .rin تحت tests/ (أو المجلد المُعطى)
// النجاح = تنفيذ بلا استثناء (RinError). لا يوجد بعد إطار assert مخصص في اللغة؛
// هذا سلوك حقيقي (ليس مُصطنعاً) يطابق ما هو متاح فعلياً في المفسّر اليوم.
// ---------------------------------------------------------------------------
int cmdTest(const std::string& dirArg) {
    std::string dir = dirArg;
    if (dir.empty()) {
        std::string root = findProjectRoot();
        dir = root.empty() ? "tests" : root + "/tests";
    }
    if (!isDir(dir)) {
        std::cerr << "rin test: مجلد الاختبارات غير موجود: " << dir << "\n";
        return 2;
    }

    std::vector<std::string> files;
    DIR* d = opendir(dir.c_str());
    if (!d) { std::cerr << "rin test: تعذّر فتح " << dir << "\n"; return 2; }
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".rin") {
            files.push_back(dir + "/" + name);
        }
    }
    closedir(d);
    std::sort(files.begin(), files.end());

    if (files.empty()) {
        std::cout << "rin test: لا توجد ملفات .rin في " << dir << "\n";
        return 0;
    }

    int passed = 0, failed = 0;
    // مؤشر Task::Test: بما أن الاختبار يعيد توجيه std::cout مؤقتاً لالتقاط ناتجه، نكتب الشريط
    // على دفق مستقل مربوط بالمخرج الحقيقي (fd=1) كي لا يختلط به ولا بالناتج الملتقَط.
    std::ostream real(std::cout.rdbuf());
    rin::loaderui::ProgressBar bar(real, rin::loaderui::profileFor(rin::loaderui::Task::Test), files.size(), /*fd=*/1);
    using Glyph = rin::loaderui::ProgressBar::Glyph;
    std::string failureLogs;   // ناتج الاختبارات الفاشلة: يُطبع بعد إغلاق الشريط (سطر الشريط لا يتحمّل متعدد الأسطر)
    for (auto& f : files) {
        bar.setStatus(f);
        auto t0 = std::chrono::steady_clock::now();
        std::string source;
        if (!readFile(f, source)) {
            ++failed;
            bar.note(Glyph::Fail, "FAIL  " + f + " (تعذّر القراءة)");
            bar.advance();
            continue;
        }
        rin::Interpreter interp;
        std::ostringstream captured;
        auto* oldBuf = std::cout.rdbuf(captured.rdbuf());
        auto* oldErr = std::cerr.rdbuf(captured.rdbuf());   // أخطاء التحليل تُلتقط أيضاً فلا تشوّه الشريط
        bool ok = runSource(source, f, interp, /*printOutput=*/true);
        std::cerr.rdbuf(oldErr);
        std::cout.rdbuf(oldBuf);
        std::string took = rin::loaderui::formatDuration(
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        if (ok) { ++passed; bar.note(Glyph::Ok, "PASS  " + f + "  (" + took + ")"); }
        else {
            ++failed;
            bar.note(Glyph::Fail, "FAIL  " + f + "  (" + took + ")");
            if (!captured.str().empty()) failureLogs += "\n--- " + f + " ---\n" + captured.str();
        }
        bar.advance();
    }
    bar.finish(failed == 0, std::to_string(passed) + " ناجح، " + std::to_string(failed) + " فاشل، من " +
                            std::to_string(files.size()) + " ملف");
    if (!failureLogs.empty()) std::cout << failureLogs;
    return failed == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------
// rin fmt <file> [--write]
// إعادة محاذاة مسافات بداية الأسطر حسب عمق الأقواس {} فقط (4 مسافات لكل مستوى)
// + إزالة المسافات الزائدة نهاية السطر + دمج الأسطر الفارغة المتتالية إلى سطر واحد.
// يعمل على مستوى السطر (لا يعيد بناء AST) لذا يحافظ حرفياً على التعليقات
// والنصوص الحرفية دون أي فقدان — هذا نطاقه المعلن، وليس Formatter كامل بعد.
// ---------------------------------------------------------------------------
std::string reindent(const std::string& source) {
    std::istringstream ss(source);
    std::string line;
    std::vector<std::string> outLines;
    int depth = 0;
    int blankRun = 0;

    while (std::getline(ss, line)) {
        std::string content = trim(line);
        if (content.empty()) {
            if (blankRun == 0 && !outLines.empty()) outLines.push_back("");
            ++blankRun;
            continue;
        }
        blankRun = 0;

        // اخفض العمق أولاً إن بدأ السطر بـ } واحد أو أكثر
        int leadingClose = 0;
        {
            bool inStr = false;
            for (size_t i = 0; i < content.size(); ++i) {
                char c = content[i];
                if (c == '"' && (i == 0 || content[i - 1] != '\\')) inStr = !inStr;
                if (inStr) continue;
                if (c == '}') { ++leadingClose; }
                else if (!isspace((unsigned char)c)) break;
            }
        }
        int thisDepth = depth - leadingClose;
        if (thisDepth < 0) thisDepth = 0;

        outLines.push_back(std::string(thisDepth * 4, ' ') + content);

        // احسب صافي فتح/إغلاق الأقواس في السطر (متجاهلاً ما داخل النصوص) لتحديث العمق للسطر التالي
        int net = 0;
        bool inStr = false;
        for (size_t i = 0; i < content.size(); ++i) {
            char c = content[i];
            if (c == '"' && (i == 0 || content[i - 1] != '\\')) { inStr = !inStr; continue; }
            if (inStr) continue;
            if (c == '/' && i + 1 < content.size() && content[i + 1] == '/') break; // تعليق سطري
            if (c == '{') ++net;
            else if (c == '}') --net;
        }
        depth += net;
        if (depth < 0) depth = 0;
    }

    std::ostringstream out;
    for (auto& l : outLines) out << l << "\n";
    return out.str();
}

int cmdFmt(const std::vector<std::string>& args) {
    std::string file;
    bool write = false;
    for (auto& a : args) {
        if (a == "--write" || a == "-w") write = true;
        else if (!a.empty() && a[0] != '-') file = a;
    }
    if (file.empty()) {
        std::cerr << "rin fmt: يلزم مسار ملف .rin\n";
        return 2;
    }
    std::string source;
    if (!readFile(file, source)) {
        std::cerr << "rin fmt: تعذّر فتح الملف '" << file << "'\n";
        return 2;
    }
    // تحقّق أولاً أن الملف صالح نحوياً (fmt على ملف به أخطاء نحوية قد يشوّهه)
    rin::diag::DiagnosticEngine engine;
    try {
        rin::Lexer lexer(source, file);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, file);
        parser.parseCollectingDiagnostics(engine);
    } catch (rin::RinError&) { /* يُبلَّغ عبر engine أدناه إن أمكن */ }
    if (engine.hasErrors()) {
        std::cerr << "rin fmt: الملف يحتوي أخطاء نحوية؛ شغّل rin check أولاً\n";
        std::cerr << rin::diag::renderAll(engine, rin::diag::globalSourceManager(), rin::diag::OutputFormat::Short);
        return 1;
    }

    auto fmtT0 = std::chrono::steady_clock::now();
    std::string formatted = reindent(source);
    {
        // ملخّص Task::Format على stderr كي لا يلوّث الناتج المنسَّق على stdout (rin fmt file > out).
        std::istringstream a(source), b(formatted);
        std::string la, lb;
        unsigned long long total = 0, changed = 0;
        while (true) {
            bool ga = static_cast<bool>(std::getline(a, la)), gb = static_cast<bool>(std::getline(b, lb));
            if (!ga && !gb) break;
            ++total;
            if (!ga || !gb || la != lb) ++changed;
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - fmtT0).count();
        rin::loaderui::report(std::cerr, rin::loaderui::profileFor(rin::loaderui::Task::Format), true,
            file + "  \xC2\xB7  " + std::to_string(changed) + " of " + std::to_string(total) + " lines " + (write ? "reformatted" : "would change"), secs);
    }
    if (write) {
        if (!writeFile(file, formatted)) {
            std::cerr << "rin fmt: تعذّر الكتابة إلى '" << file << "'\n";
            return 1;
        }
        std::cout << "rin fmt: تمت إعادة تنسيق " << file << "\n";
    } else {
        std::cout << formatted;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Rin Toolchain commands: semantic analysis, static typing and bytecode VM.
// These commands share the canonical Lexer/Parser/AST used by the interpreter.
// ---------------------------------------------------------------------------
bool loadAstForToolchain(const std::string& path, std::vector<rin::StmtPtr>& out) {
    std::string source;
    if (!readFile(path, source)) {
        std::cerr << "rin: تعذّر فتح الملف '" << path << "'\n";
        return false;
    }
    try {
        rin::Lexer lexer(source, path);
        auto tokens = lexer.scanTokens();
        rin::Parser parser(tokens, path);
        out = parser.parse();
        return true;
    } catch (rin::RinError& e) {
        if (e.diagnostic) std::cerr << rin::diag::renderPlain(*e.diagnostic, rin::diag::globalSourceManager());
        else std::cerr << "[rin] خطأ عند السطر " << e.line << ": " << e.message << "\n";
        return false;
    }
}

int cmdAnalyze(const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "rin analyze: يلزم ملف .rin\n"; return 2; }
    std::vector<rin::StmtPtr> ast;
    if (!loadAstForToolchain(args[0], ast)) return 1;
    rin::toolchain::SemanticAnalyzer semantic;
    rin::toolchain::TypeChecker types;
    rin::loaderui::Spinner spin(std::cout, rin::loaderui::profileFor(rin::loaderui::Task::Check), "semantic + types  " + args[0]);
    auto sr=semantic.analyze(ast);
    auto tr=types.check(ast);
    if (sr.diagnostics.hasErrors() || tr.diagnostics.hasErrors()) spin.fail("issues found");
    else spin.succeed("no issues");
    std::cout << "Rin Toolchain Analysis\n";
    std::cout << "semantic: " << (sr.diagnostics.hasErrors() ? "FAILED" : "OK") << "\n";
    std::cout << sr.diagnostics.renderText();
    std::cout << "types:    " << (tr.diagnostics.hasErrors() ? "FAILED" : "OK") << "\n";
    std::cout << tr.diagnostics.renderText();
    return (sr.diagnostics.hasErrors() || tr.diagnostics.hasErrors()) ? 1 : 0;
}

int cmdBytecode(const std::vector<std::string>& args, bool execute) {
    if (args.empty()) { std::cerr << "rin " << (execute?"vm":"bytecode") << ": يلزم ملف .rin\n"; return 2; }
    std::vector<rin::StmtPtr> ast;
    if (!loadAstForToolchain(args[0], ast)) return 1;
    rin::toolchain::SemanticAnalyzer semantic;
    auto sr=semantic.analyze(ast);
    if (sr.diagnostics.hasErrors()) { std::cerr << sr.diagnostics.renderText(); return 1; }
    rin::toolchain::BytecodeCompiler compiler;
    auto cr=compiler.compile(ast);
    if (!cr.diagnostics.renderText().empty()) std::cerr << cr.diagnostics.renderText();
    if (cr.diagnostics.hasErrors()) return 1;
    bool optimize = std::find(args.begin(), args.end(), "--optimize") != args.end();
    auto outIt = std::find(args.begin(), args.end(), "--out");
    std::string outPath;
    if (outIt != args.end() && std::next(outIt) != args.end()) outPath = *std::next(outIt);
    if (optimize) {
        rin::toolchain::Optimizer opt;
        std::cout << "optimizer: " << opt.optimize(cr.chunk) << " optimization(s)\n";
    }
    if (!execute) {
        if (!outPath.empty()) {
            if (!rin::toolchain::writeBytecode(cr.chunk, outPath)) {
                std::cerr << "rin bytecode: تعذّر كتابة " << outPath << "\n"; return 1;
            }
            std::cout << "bytecode: " << outPath << "\n";
        }
        std::cout << rin::toolchain::BytecodeCompiler::disassemble(cr.chunk); return 0;
    }
    rin::toolchain::BytecodeVM vm;
    auto r=vm.run(cr.chunk);
    std::cout << r.output;
    if (!r.ok) { std::cerr << r.diagnostics.renderText(); return 1; }
    return 0;
}

int cmdToolchainNew(const std::vector<std::string>& args) {
    if (args.empty()) { std::cerr << "rin toolchain-new: يلزم اسم المشروع\n"; return 2; }
    rin::toolchain::DiagnosticBag d;
    rin::toolchain::ProjectOptions o; o.name=args[0];
    if (!rin::toolchain::generateProject(args[0],o,d)) { std::cerr << d.renderText(); return 1; }
    std::cout << "Rin Toolchain: تم إنشاء المشروع " << args[0] << "\n";
    return 0;
}

// ---------------------------------------------------------------------------
// rin clean
// ---------------------------------------------------------------------------
// يعدّ الملفات والبايتات تحت مجلد (بلا اتباع الروابط الرمزية).
void countTree(const std::string& dir, unsigned long long& files, unsigned long long& bytes) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string p = dir + "/" + n;
        struct stat st{};
        if (::lstat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) countTree(p, files, bytes);
        else { ++files; bytes += static_cast<unsigned long long>(st.st_size); }
    }
    closedir(d);
}

int cmdClean() {
    std::string root = findProjectRoot();
    std::string buildDir = root.empty() ? "build" : root + "/build";
    if (!isDir(buildDir)) {
        std::cout << "rin clean: لا يوجد شيء لحذفه (" << buildDir << " غير موجود)\n";
        return 0;
    }
    // الكمية الحقيقية المحذوفة (عدد الملفات والحجم) تُحسب قبل الحذف.
    unsigned long long cleanFiles = 0, cleanBytes = 0;
    countTree(buildDir, cleanFiles, cleanBytes);
    rin::loaderui::Spinner spin(std::cout, rin::loaderui::profileFor(rin::loaderui::Task::Clean), buildDir);
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::string> cmd = {"rm", "-rf", buildDir};
    int rc = runSubprocess(cmd);
    (void)t0;
    if (rc == 0) spin.succeed("removed " + std::to_string(cleanFiles) + " files \xC2\xB7 " + rin::loaderui::formatBytes(cleanBytes));
    else spin.fail("rm failed (exit " + std::to_string(rc) + ")");
    return rc;
}

// ---------------------------------------------------------------------------
// rin doctor — فحص صادق: يُبلغ فقط عمّا تحقق فعلياً، ولا يدّعي جاهزية أي شيء
// لم يُختبر (Android/WASM/إلخ تُعرض بوضوح كـ"غير مُدمَج بعد" في هذا التوزيع).
// ---------------------------------------------------------------------------
bool commandExists(const std::string& name) {
    std::vector<std::string> cmd = {"/bin/sh", "-c", "command -v " + name + " >/dev/null 2>&1"};
    return runSubprocess(cmd) == 0;
}

int cmdDoctor() {
    std::cout << "Rin Doctor — فحص بيئة التطوير\n\n";
    using Glyph = rin::loaderui::ProgressBar::Glyph;
    rin::loaderui::ProgressBar bar(std::cout, rin::loaderui::profileFor(rin::loaderui::Task::Doctor), 5);
    bar.note(Glyph::Ok, std::string("rin CLI  (v") + kVersion + ")");
    bar.advance();

    bar.setStatus("rinc");
    std::string rinc = findRinc();
    bool rincOk = fileExists(rinc) || commandExists("rinc");
    bar.note(rincOk ? Glyph::Ok : Glyph::Fail, std::string("rinc (Native Compiler) — ") + (rincOk ? rinc : "غير موجود في PATH ولا بجانب rin"));
    bar.advance();

    bar.setStatus("cc / gcc / clang");
    bool ccOk = commandExists("cc") || commandExists("gcc") || commandExists("clang");
    bar.note(ccOk ? Glyph::Ok : Glyph::Fail, "مترجم C على النظام (مطلوب لـrinc: cc/gcc/clang)");
    bar.advance();

    const char* home = std::getenv("RIN_HOME");
    bar.note(home ? Glyph::Ok : Glyph::Fail, std::string("RIN_HOME — ") + (home ? home : "غير مضبوط"));
    bar.advance();

    std::string root = findProjectRoot();
    bar.note(!root.empty() ? Glyph::Ok : Glyph::Fail, std::string("مشروع Rin حالي — ") + (root.empty() ? "لا يوجد rin.toml في المسار الحالي" : root));
    bar.advance();
    bar.finish(rincOk && ccOk, rincOk && ccOk ? "environment ready" : "environment incomplete");

    std::cout << "\nما هو مدعوم فعلياً في هذا التوزيع:\n"
              << "  ✓ المفسّر (rin run / REPL)\n"
              << "  ✓ الفحص النحوي (rin check)\n"
              << "  ✓ البناء الأصلي عبر rinc (rin build) — اللغة الإجرائية الأساسية + @container/@container.data\n"
              << "  ✗ container.pipe/api/import/table/doc/object/portal/block/sticker/aukt، Containers.Group، Volume — غير مدعومة بعد في rinc\n"
              << "  ✗ WASM / Android backends من خلال هذا CLI — غير مُدمَجة بعد\n";

    return (rincOk && ccOk) ? 0 : 1;
}

} // namespace

static int realMain(int argc, char** argv);

namespace {
struct MainArgs { int argc; char** argv; int rc; };
void* mainThreadEntry(void* p) {
    auto* a = static_cast<MainArgs*>(p);
    a->rc = realMain(a->argc, a->argv);
    return nullptr;
}
} // namespace

// المفسِّر شجري (tree-walking) وكل استدعاء Rin يستهلك عدة إطارات C++؛ مكدّس العملية الافتراضي (8MB) لا
// يسمح إلا بمئات المستويات. لذا يعمل البرنامج كله على خيط بمكدّس افتراضي ضخم (1GB، ذاكرة افتراضية لا
// تُحجز فعلياً إلا عند الاستخدام) ويُرفع حدّ عمق الاستدعاء من 300 إلى 10000 — فتعمل الدوال التكرارية
// العميقة المشروعة، ويبقى تجاوز الحدّ خطأ Rin عادياً (E0035) لا انهياراً. فشل إنشاء الخيط => تشغيل مباشر.
static long long g_runtimeBudget = 0;
// rin vm file.rin [--disasm] [--stats] [args...] : ينفّذ البرنامج على RinVM الحقيقية (آلة بايتكود كاملة).
int cmdRealVM(const std::vector<std::string>& args) {
    std::string file; bool disasm = false, stats = false, noRun = false;
    std::vector<std::string> prog;
    for (auto& a : args) {
        if (a == "--disasm") disasm = true;
        else if (a == "--stats") stats = true;
        else if (a == "--no-run") noRun = true;
        else if (file.empty()) file = a;
        else prog.push_back(a);
    }
    if (file.empty()) { std::cerr << "rin vm: يلزم ملف .rin  [--disasm] [--stats] [--no-run]\n"; return 2; }
    std::string source;
    if (!readFile(file, source)) { std::cerr << "rin vm: تعذّر فتح الملف '" << file << "'\n"; return 2; }
    rin::Interpreter interp;
    interp.setEngineVM(true);
    if (g_runtimeBudget > 0) interp.setExecutionBudget(g_runtimeBudget);
    interp.setProgramArgs(prog);
    if (disasm) {
        try {
            rin::Lexer lexer(source, file);
            auto tokens = lexer.scanTokens();
            rin::Parser parser(tokens, file);
            auto ast = parser.parse();
            std::cout << interp.vmDisassembleProgram(ast);
        } catch (std::exception& e) { std::cerr << "rin vm: " << e.what() << "\n"; return 1; }
        if (noRun) return 0;
    }
    bool ok = noRun ? true : runSource(source, file, interp);
    if (stats) {
        if (auto* st = interp.vmStats())
            std::cerr << "[vm] instructions=" << st->instructions << " calls=" << st->calls
                      << " nestedRuns=" << st->nestedRuns << " compiled=" << st->compiled
                      << " delegated=" << st->delegated << "\n";
    }
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    MainArgs args{argc, argv, 0};
    pthread_attr_t attr;
    pthread_t th;
    rin::Interpreter::setDefaultMaxCallDepth(10000);
    if (pthread_attr_init(&attr) == 0 &&
        pthread_attr_setstacksize(&attr, static_cast<size_t>(1) << 30) == 0 &&
        pthread_create(&th, &attr, mainThreadEntry, &args) == 0) {
        pthread_join(th, nullptr);
        return args.rc;
    }
    rin::Interpreter::setDefaultMaxCallDepth(300); // لا خيط كبير => الحدّ الآمن للمكدّس العادي
    return realMain(argc, argv);
}

// حدود وقت التشغيل المرنة (docs/runtime.md §3): تُعطى قبل اسم الملف فقط، فلا تلتبس بوسائط البرنامج.
//   rin --max-depth=N file.rin   حد عمق الاستدعاء (الافتراضي 10000، الأقصى 50000)
//   rin --budget=N    file.rin   سقف عدد العبارات المنفَّذة (حماية من الحلقات اللانهائية؛ 0 = بلا حد)
static bool parseRuntimeLimit(const std::string& a, const char* flag, long long lo, long long hi, long long& out) {
    std::string pre = std::string(flag) + "=";
    if (a.rfind(pre, 0) != 0) return false;
    std::string v = a.substr(pre.size());
    char* end = nullptr;
    long long n = std::strtoll(v.c_str(), &end, 10);
    if (v.empty() || *end != '\0' || n < lo || n > hi) {
        std::cerr << "rin: قيمة غير صالحة للخيار " << flag << " (المسموح " << lo << ".." << hi << ")\n";
        std::exit(2);
    }
    out = n;
    return true;
}

// ---------------------------------------------------------------------------
// صورة RVM (rin_vm): تُنفَّذ إما ببايتكود (آلة toolchain) أو بالمحرّك الكامل مع ملفات الاستيراد الافتراضية
// ---------------------------------------------------------------------------
static int runImageBytes(const std::string& bytes, const std::vector<std::string>& progArgs) {
    rin_vm::Image im; std::string err;
    if (!rin_vm::deserialize(bytes, im, &err)) { std::cerr << "[rin] صورة VM غير صالحة: " << err << "\n"; return 1; }
    if (!im.bytecode.empty()) {
        rin::toolchain::Chunk chunk;
        if (!rin::toolchain::readBytecodeFromMemory(chunk, im.bytecode)) { std::cerr << "[rin] بايتكود الصورة تالف\n"; return 1; }
        rin::toolchain::BytecodeVM vm;
        auto r = vm.run(chunk);
        std::cout << r.output;
        if (!r.ok) { std::cerr << r.diagnostics.renderText(); return 1; }
        return 0;
    }
    rin::Interpreter interp;
    interp.setProgramArgs(progArgs);
    std::unordered_map<std::string, std::string> vf(im.files.begin(), im.files.end());
    interp.setVirtualFiles(std::move(vf));
    return runSource(im.main, im.name, interp) ? 0 : 1;
}

static int realMain(int argc, char** argv) {
    // RIN_AVA: إن كان هذا الملف التنفيذي ناتجاً عن rin_ava (برنامج Rin مُلحَق في نهايته) فشغّله
    // مباشرة، وكل وسائط سطر الأوامر تذهب للبرنامج كما هي (دون تفسير خيارات rin).
    // الحمولة نوعان: مصدر ← المفسّر (rin_interpreter.cpp)، أو بايتكود ← آلة toolchain/BytecodeVM.
    // الملف التنفيذي العادي لا يحمل المقطع الختامي فيتجاوز هذا الفرع دون أي تغيير في السلوك.
    {
        rin_ava::Payload pl;
        if (rin_ava::readSelfPayloadEx(argc > 0 ? argv[0] : nullptr, pl)) {
            if (pl.kind == rin_ava::kKindBytecode) {
                rin::toolchain::Chunk chunk;
                if (!rin::toolchain::readBytecodeFromMemory(chunk, pl.data)) {
                    std::cerr << "[rin_ava] بايتكود مُضمَّن تالف: " << pl.name << "\n";
                    return 1;
                }
                rin::toolchain::BytecodeVM vm;
                auto r = vm.run(chunk);
                std::cout << r.output;
                if (!r.ok) { std::cerr << r.diagnostics.renderText(); return 1; }
                return 0;
            }
            std::vector<std::string> avaArgs;
            for (int i = 1; i < argc; ++i) avaArgs.emplace_back(argv[i]);
            if (pl.kind == rin_ava::kKindImage) return runImageBytes(pl.data, avaArgs);
            rin::Interpreter interp;
            interp.setProgramArgs(avaArgs);
            return runSource(pl.data, pl.name, interp) ? 0 : 1;
        }
    }
    while (argc >= 2) {
        std::string a = argv[1];
        long long n = 0;
        if (parseRuntimeLimit(a, "--max-depth", 1, 50000, n)) rin::Interpreter::setDefaultMaxCallDepth(static_cast<int>(n));
        else if (parseRuntimeLimit(a, "--budget", 0, 1000000000000LL, n)) g_runtimeBudget = n;
        else if (a == "--vm" || a == "--engine=vm") ::setenv("RIN_ENGINE", "vm", 1);
        else if (a == "--interp" || a == "--engine=interp") ::unsetenv("RIN_ENGINE");
        else break;
        for (int i = 1; i + 1 < argc; ++i) argv[i] = argv[i + 1];
        --argc;
    }
    // --allow-native: يفعّل جسر C++ (cpp.*) لهذا التشغيل فقط (Rin 1.0). قرار المستخدم لا الكود.
    {
        int w = 1;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--allow-native") == 0) { ::setenv("RIN_ALLOW_NATIVE", "1", 1); continue; }
            argv[w++] = argv[i];
        }
        argc = w;
    }
    if (argc >= 2) {
        std::string cmd = argv[1];
        std::vector<std::string> rest(argv + 2, argv + argc);

        if (cmd == "--version" || cmd == "-v") {
            // تقرير إصدار مركّب حقيقي: يستدعي rinc فعلياً لمعرفة إصداره الفعلي بدل افتراضه.
            std::string rinc = findRinc();
            std::string rincVersion = "غير مثبَّت";
            {
                std::string tmpOut = "/tmp/.rin_version_probe";
                std::string cmdline = "\"" + rinc + "\" --version > " + tmpOut + " 2>/dev/null";
                if (std::system(cmdline.c_str()) == 0) {
                    std::string content;
                    if (readFile(tmpOut, content) && !content.empty()) {
                        rincVersion = trim(content);
                    }
                    ::remove(tmpOut.c_str());
                }
            }
#if defined(__x86_64__)
            std::string target = "x86_64-linux";
#elif defined(__aarch64__)
            std::string target = "aarch64-linux";
#else
            std::string target = "unknown-linux";
#endif
            std::cout << "Rin " << kVersion << "\n"
                      << rincVersion << "\n"
                      << "Runtime " << kVersion << "\n"
                      << "Edition 2026\n"
                      << "Target " << target << "\n";
            return 0;
        }
        if (cmd == "--help" || cmd == "-h") { printUsage(); return 0; }

        if (cmd == "pkg") return rinpm::cli::run(rest, kVersion);

        // rin indsin <file.rin> [--width N] [--dump|--plain]: واجهة indsin تُعرَض داخل الطرفية نفسها
        // (نصف-كتل ملوّنة + فأرة + تحميل حيّ) — لا نافذة ولا Android.
        if (cmd == "indsin") return rin::terminal::runIndsinCommand(rest);

        // rin img <image> [...]: يعرض صورة في الطرفية بحروف Unicode (نصف-كتل ▀ / براي ⣿ / ظلال ░▒▓█).
        if (cmd == "img" || cmd == "image") return rin::terminal::runImgCommand(rest);

        if (cmd == "terminal" || cmd == "repl" || cmd == "shell") {
            rin::terminal::Options topt;
            topt.version = kVersion;
            std::string terr;
            bool thelp = false;
            if (!rin::terminal::parseArgs(rest, topt, terr, thelp)) {
                std::cerr << "rin " << cmd << ": " << terr << "\n";
                return 2;
            }
            if (thelp) { rin::terminal::printUsage(); return 0; }
            return rin::terminal::run(topt);
        }

        if (cmd == "new") return cmdNew(rest.empty() ? "" : rest[0]);
        if (cmd == "build") return cmdBuild(rest);
        if (cmd == "run") return cmdRun(rest);
        if (cmd == "test") return cmdTest(rest.empty() ? "" : rest[0]);
        if (cmd == "fmt") return cmdFmt(rest);
        if (cmd == "clean") return cmdClean();
        if (cmd == "doctor") return cmdDoctor();
        if (cmd == "lib") return cmdLib(rest);
        if (cmd == "analyze") return cmdAnalyze(rest);
        if (cmd == "bytecode") return cmdBytecode(rest, false);
        if (cmd == "vm") return cmdRealVM(rest);
        if (cmd == "vm-legacy") return cmdBytecode(rest, true);
        if (cmd == "toolchain-new") return cmdToolchainNew(rest);
        if (cmd == "image") {   // rin image prog.rvm [args…]  — ينفّذ صورة VM
            if (rest.empty()) { std::cerr << "rin image: يلزم ملف .rvm (أنشئه بـ rin_vm pack أو rin_ava -fvm -c)\n"; return 2; }
            std::string bytes;
            if (!readFile(rest[0], bytes)) { std::cerr << "rin image: تعذّر فتح '" << rest[0] << "'\n"; return 2; }
            return runImageBytes(bytes, std::vector<std::string>(rest.begin() + 1, rest.end()));
        }

        if (cmd == "check" && !rest.empty()) {
            rin::diag::OutputFormat fmt = rin::diag::OutputFormat::Plain;
            std::string filePath = rest[0];
            for (size_t i = 1; i < rest.size(); ++i) {
                if (rest[i] == "--format=json") fmt = rin::diag::OutputFormat::Json;
                else if (rest[i] == "--format=short") fmt = rin::diag::OutputFormat::Short;
                else if (rest[i] == "--format=lsp") fmt = rin::diag::OutputFormat::Lsp;
                else if (rest[i] == "--format=plain") fmt = rin::diag::OutputFormat::Plain;
            }
            return runCheck(filePath, fmt);
        }
    }

    // توافقية خلفية كاملة: rin file.rin / rin -c "..." / rin < file / REPL
    std::string source;
    std::string sourceName = "<stdin>";
    // أي وسائط إضافية بعد اسم الملف (rin file.rin arg1 arg2 ...) تُمرَّر كما هي إلى الجسم عبر
    // setProgramArgs() -- انظر شرح "args" الكامل في rin_ast.h (ProgramStmt) وrin_interpreter.h.
    // لا تُستخدَم في وضع '-c'/REPL/stdin (rest تبقى فارغة هناك، فـ "args" تكون [] كما كانت دائماً).
    std::vector<std::string> rest;

    if (argc >= 3 && std::strcmp(argv[1], "-c") == 0) {
        source = argv[2];
        sourceName = "<inline>";
    } else if (argc >= 2) {
        if (!readFile(argv[1], source)) {
            std::cerr << "rin: تعذّر فتح الملف '" << argv[1] << "'\n";
            return 2;
        }
        sourceName = argv[1];
        for (int i = 2; i < argc; i++) rest.emplace_back(argv[i]);
    } else if (isatty(fileno(stdin))) {
        return runRepl();
    } else {
        source = readAll(std::cin);
    }

    rin::Interpreter interp;
    if (g_runtimeBudget > 0) interp.setExecutionBudget(g_runtimeBudget);
    if (sourceName != "<stdin>" && sourceName != "<inline>") source = rinzPrelude(sourceName, interp) + source;
    interp.setProgramArgs(rest);
    bool ok = runSource(source, sourceName, interp);
    return ok ? 0 : 1;
}

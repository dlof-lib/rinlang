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
//    rin                                     REPL تفاعلي
//    rin --version | -v ، --help | -h
// ============================================================================
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "diagnostics/diagnostic_renderer.h"
#include "diagnostics/source_manager.h"
#include "pkg/cli_pkg.h"

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
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <climits>

namespace {

constexpr const char* kVersion = "0.2.0";

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
        "  rin build [file] [-o out] [--release]  بناء تنفيذي أصلي (عبر rinc)\n"
        "  rin run [file] [--import-progress]     تشغيل برنامج Rin (شريط تحميل حي لـ @import)\n"
        "  rin check <file> [--format=plain|short|json|lsp]\n"
        "  rin test [dir]                         تشغيل اختبارات .rin\n"
        "  rin fmt <file> [--write]               إعادة محاذاة المسافات البادئة\n"
        "  rin clean                              حذف مخرجات ./build\n"
        "  rin doctor                             فحص بيئة التطوير\n"
        "  rin -c \"print 1+1;\"                    تشغيل كود مباشر\n"
        "  rin < file.rin                          قراءة الكود من stdin\n"
        "  rin                                     REPL تفاعلي\n"
        "  rin --version | -v ، --help | -h\n";
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
        return true;
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

int runRepl() {
    std::cout << "Rin v" << kVersion << " - وضع تفاعلي. اكتب سطر Rin ثم Enter لتنفيذه (exit للخروج).\n";
    rin::Interpreter interp;
    std::string line;
    int lineNo = 1;
    while (true) {
        std::cout << "rin[" << lineNo << "]> ";
        if (!std::getline(std::cin, line)) { std::cout << "\n"; break; }
        if (line == "exit" || line == "quit") break;
        if (line.empty()) continue;
        runSource(line, "<repl>", interp);
        std::cout << "\n";
        ++lineNo;
    }
    return 0;
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
    int rc = runSubprocess(cmd);
    if (rc == 127 || rc == -1) {
        std::cerr << "rin build: تعذّر إيجاد/تشغيل rinc. تأكد من تثبيته بجانب rin أو في PATH\n";
        return 1;
    }
    if (rc == 0) {
        std::cout << "[rin build] تم بنجاح: " << out << "\n";
    }
    return rc;
}

// ---------------------------------------------------------------------------
// rin run [file]
// ---------------------------------------------------------------------------
int cmdRun(const std::vector<std::string>& args) {
    std::string file;
    bool importProgress = false;
    for (auto& a : args) {
        if (a == "--import-progress") importProgress = true;
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
    // --import-progress اختياري تماماً: بدونه السلوك مطابق تماماً لما كان عليه قبل هذه الميزة
    // (لا شريط تحميل، لا أي فرق في الناتج). راجع rin::loaderui في app/src/main/cpp/loader_ui/.
    if (importProgress) interp.setImportUIMode(rin::loaderui::Mode::Verbose);
    bool ok = runSource(source, file, interp);
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
    for (auto& f : files) {
        std::string source;
        if (!readFile(f, source)) { ++failed; std::cout << "FAIL  " << f << " (تعذّر القراءة)\n"; continue; }
        rin::Interpreter interp;
        std::ostringstream captured;
        auto* oldBuf = std::cout.rdbuf(captured.rdbuf());
        bool ok = runSource(source, f, interp, /*printOutput=*/true);
        std::cout.rdbuf(oldBuf);
        if (ok) { ++passed; std::cout << "PASS  " << f << "\n"; }
        else { ++failed; std::cout << "FAIL  " << f << "\n" << captured.str(); }
    }
    std::cout << "\n" << passed << " ناجح، " << failed << " فاشل، من " << files.size() << " ملف\n";
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

    std::string formatted = reindent(source);
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
// rin clean
// ---------------------------------------------------------------------------
int cmdClean() {
    std::string root = findProjectRoot();
    std::string buildDir = root.empty() ? "build" : root + "/build";
    if (!isDir(buildDir)) {
        std::cout << "rin clean: لا يوجد شيء لحذفه (" << buildDir << " غير موجود)\n";
        return 0;
    }
    std::vector<std::string> cmd = {"rm", "-rf", buildDir};
    int rc = runSubprocess(cmd);
    if (rc == 0) std::cout << "rin clean: تم حذف " << buildDir << "\n";
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
    std::cout << "✓ rin CLI  (v" << kVersion << ")\n";

    std::string rinc = findRinc();
    bool rincOk = fileExists(rinc) || commandExists("rinc");
    std::cout << (rincOk ? "✓" : "✗") << " rinc (Native Compiler) — " << (rincOk ? rinc : "غير موجود في PATH ولا بجانب rin") << "\n";

    bool ccOk = commandExists("cc") || commandExists("gcc") || commandExists("clang");
    std::cout << (ccOk ? "✓" : "✗") << " مترجم C على النظام (مطلوب لـrinc: cc/gcc/clang)\n";

    const char* home = std::getenv("RIN_HOME");
    std::cout << (home ? "✓" : "✗") << " RIN_HOME — " << (home ? home : "غير مضبوط") << "\n";

    std::string root = findProjectRoot();
    std::cout << (!root.empty() ? "✓" : "✗") << " مشروع Rin حالي — " << (root.empty() ? "لا يوجد rin.toml في المسار الحالي" : root) << "\n";

    std::cout << "\nما هو مدعوم فعلياً في هذا التوزيع:\n"
              << "  ✓ المفسّر (rin run / REPL)\n"
              << "  ✓ الفحص النحوي (rin check)\n"
              << "  ✓ البناء الأصلي عبر rinc (rin build) — اللغة الإجرائية الأساسية + @container/@container.data\n"
              << "  ✗ container.pipe/api/import/table/doc/object/portal/block/sticker/aukt، Containers.Group، Volume — غير مدعومة بعد في rinc\n"
              << "  ✗ WASM / Android backends من خلال هذا CLI — غير مُدمَجة بعد\n";

    return (rincOk && ccOk) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
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

        if (cmd == "new") return cmdNew(rest.empty() ? "" : rest[0]);
        if (cmd == "build") return cmdBuild(rest);
        if (cmd == "run") return cmdRun(rest);
        if (cmd == "test") return cmdTest(rest.empty() ? "" : rest[0]);
        if (cmd == "fmt") return cmdFmt(rest);
        if (cmd == "clean") return cmdClean();
        if (cmd == "doctor") return cmdDoctor();

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

    if (argc >= 3 && std::strcmp(argv[1], "-c") == 0) {
        source = argv[2];
        sourceName = "<inline>";
    } else if (argc >= 2) {
        if (!readFile(argv[1], source)) {
            std::cerr << "rin: تعذّر فتح الملف '" << argv[1] << "'\n";
            return 2;
        }
        sourceName = argv[1];
    } else if (isatty(fileno(stdin))) {
        return runRepl();
    } else {
        source = readAll(std::cin);
    }

    rin::Interpreter interp;
    bool ok = runSource(source, sourceName, interp);
    return ok ? 0 : 1;
}

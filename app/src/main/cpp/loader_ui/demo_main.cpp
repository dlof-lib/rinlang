// app/src/main/cpp/loader_ui/demo_main.cpp
// ============================================================================
// عرض توضيحي مستقل: يحاكي ما سيحدث لاحقاً عند ربط هذه الوحدة بـ @import
// الفعلي في rin_interpreter.cpp، لكن بدون أي اعتماد عليه. يبني بأمر واحد:
//
//   g++ -std=c++17 -pthread -o loader_ui_demo library_loader_ui.cpp demo_main.cpp
//
// ويُشغَّل: ./loader_ui_demo
// ============================================================================
#include "library_loader_ui.h"

#include <chrono>
#include <iostream>
#include <thread>

using namespace rin::loaderui;

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// يحاكي استيراد مكتبة بسيطة (@import "math";) بالمراحل الثمانية الحقيقية.
static void simulateSimpleImport(std::shared_ptr<ILoadSink> sink, const std::string& name) {
    LoadSession session(sink, name);
    session.stage(LoadStage::Resolving);
    sleepMs(60);
    session.stage(LoadStage::Locating);
    sleepMs(60);
    session.stage(LoadStage::Reading);
    sleepMs(60);
    session.stage(LoadStage::Parsing);
    sleepMs(60);
    session.stage(LoadStage::Dependencies, "Resolving dependencies...");
    sleepMs(80);
    session.stage(LoadStage::Registering);
    sleepMs(60);
    session.stage(LoadStage::Initializing);
    sleepMs(60);
    session.stage(LoadStage::Completed);
    session.finish(LoadOutcome{true, /*fromEmbedded=*/true, /*fromCache=*/false, ""});
}

// يحاكي استيراد مكتبة كبيرة مركّبة من عدة وحدات فرعية (@import "rin.ui";)
// حيث كل وحدة فرعية هي بحد ذاتها استيراد كامل (لها أربع مراحل مبسّطة هنا
// للعرض)، تماماً كمثال core/layout/paint/widgets في الطلب.
static void simulateCompositeImport(std::shared_ptr<ILoadSink> sink, const std::string& name,
                                     const std::vector<std::string>& children) {
    LoadSession session(sink, name);
    session.stage(LoadStage::Resolving);
    sleepMs(50);
    session.stage(LoadStage::Locating);
    sleepMs(50);
    session.stage(LoadStage::Reading);
    sleepMs(50);
    session.stage(LoadStage::Parsing);
    sleepMs(50);

    for (std::size_t i = 0; i < children.size(); ++i) {
        LoadSession child(sink, children[i], /*depth=*/1);
        child.stage(LoadStage::Resolving);
        sleepMs(30);
        child.stage(LoadStage::Reading);
        sleepMs(30);
        child.stage(LoadStage::Parsing);
        sleepMs(30);
        child.stage(LoadStage::Completed);
        child.finish(LoadOutcome{true, false, false, ""});
    }

    session.stage(LoadStage::Dependencies, "Resolving dependencies...");
    sleepMs(60);
    session.stage(LoadStage::Registering);
    sleepMs(50);
    session.stage(LoadStage::Initializing);
    sleepMs(50);
    session.stage(LoadStage::Completed);
    session.finish(LoadOutcome{true, false, false, ""});
}

// يحاكي استيراد فاشل (module not found) لإثبات أن الشريط لا يتجمّد صامتاً.
static void simulateFailedImport(std::shared_ptr<ILoadSink> sink, const std::string& name) {
    LoadSession session(sink, name);
    session.stage(LoadStage::Resolving);
    sleepMs(40);
    session.stage(LoadStage::Locating);
    sleepMs(40);
    session.finish(LoadOutcome{false, false, false, "module not found: `" + name + "`"});
}

// معرض كل أنواع المؤشرات: ./loader_ui_demo --gallery
static int gallery() {
    std::cout << "=== Progress bars (BarStyle) ===\n\n";
    for (const auto& name : barStyleNames()) {
        BarStyle bs; parseBarStyle(name, bs);
        IndicatorStyle st; st.bar = bs;
        ProgressBar bar(std::cout, name, 4, st);
        for (int i = 0; i < 4; ++i) { bar.setStatus("step " + std::to_string(i + 1)); sleepMs(260); bar.advance(); }
        bar.finish(true, name + " done");
    }
    std::cout << "\n=== Spinners (SpinnerStyle, indeterminate) ===\n\n";
    for (const auto& name : spinnerStyleNames()) {
        SpinnerStyle ss; parseSpinnerStyle(name, ss);
        IndicatorStyle st; st.spinner = ss;
        Spinner sp(std::cout, "spinner: " + name, st);
        sleepMs(900);
        sp.succeed(name);
    }
    return 0;
}

// معرض أنواع الأوامر: كل نوع بهويّته (فعل/وحدة/لون/شكل/ETA/سرعة): ./loader_ui_demo --tasks
static int tasks() {
    struct T { Task t; const char* item; int total; int ms; };
    const T list[] = {
        {Task::Install, "http 1.2.0",  8,  330}, {Task::Test,   "math_test.rin", 12, 240},
        {Task::Build,   "linking",     6,  420}, {Task::Check,  "app/main.rin", 14, 200},
        {Task::Format,  "src/ui.rin",  5,  500}, {Task::Clean,  "build/obj",    9, 280},
        {Task::Doctor,  "rinc",        5,  520}, {Task::Load,   "lib/math.og.rin", 4, 260},
        {Task::Project, "src/main.rin", 5, 240}, {Task::Files,  "assets/",      8, 180},
        {Task::Hash,    "release.rcl",  4,  300}, {Task::Bench,  "run",         10, 140},
    };
    for (const auto& x : list) {
        ProgressBar bar(std::cout, profileFor(x.t), x.total);
        for (int i = 0; i < x.total; ++i) { bar.setStatus(std::string(x.item) + " " + std::to_string(i + 1)); sleepMs(x.ms); bar.advance(); }
        bar.finish(true, profileFor(x.t).verb + " complete");
    }
    Spinner sp(std::cout, profileFor(Task::Run), "main.rin");
    sleepMs(1200);
    sp.succeed("finished");
    return 0;
}

// مؤشر تشغيل كود Rin: ./loader_ui_demo --run
// يحاكي برنامجاً: عمل صامت (يظهر المؤشر) ← استيراد مكتبة (يظهر اسمها ومرحلتها) ← مخرجات (يختفي المؤشر ثم يعود).
static int runDemo() {
    RunIndicator ind(std::cout, profileFor(Task::Run), "main.rin", /*fd=*/1);
    auto out = [&](const std::string& s) { ind.emit(s, [](const std::string& t) { std::cout << t << std::flush; }); };
    auto imp = ind.importSink();
    sleepMs(700);
    imp->onBegin("rin.ui", 0);
    for (LoadStage st : {LoadStage::Reading, LoadStage::Parsing, LoadStage::Initializing}) { imp->onStage("rin.ui", st, "", 0); sleepMs(450); }
    imp->onEnd("rin.ui", LoadOutcome{}, 0);
    out("hello from rin\n");
    sleepMs(900);
    out("step 2 done\n");
    sleepMs(1500);
    out("finished\n");
    ind.stop();
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--run") return runDemo();
    if (argc > 1 && std::string(argv[1]) == "--tasks") return tasks();
    if (argc > 1 && std::string(argv[1]) == "--gallery") return gallery();
    std::cout << "=== Verbose mode (ConsoleBarSink) ===\n\n";
    auto verbose = makeSink(Mode::Verbose, std::cout);

    simulateSimpleImport(verbose, "math");
    std::cout << "\n";
    simulateCompositeImport(verbose, "rin.ui", {"core", "layout", "paint", "widgets"});
    std::cout << "\n";
    simulateFailedImport(verbose, "not_a_real_lib");

    std::cout << "\n=== ProgressBar (multi-step: package install) ===\n\n";
    {
        const char* pkgs[] = {"http 1.2.0", "json 0.9.4", "router 2.0.1"};
        const char* phases[] = {"downloading", "verifying", "unpacking"};
        ProgressBar bar(std::cout, "Installing", 3);
        for (const char* p : pkgs) {
            for (const char* ph : phases) {
                bar.setStatus(std::string(p) + " \xC2\xB7 " + ph);
                sleepMs(350);
            }
            bar.note(ProgressBar::Glyph::Ok, std::string(p) + "  downloaded");
            bar.advance();
        }
        bar.finish(true, "3 packages ready");
    }

    std::cout << "\n=== Quiet/Silent mode (SilentSink) ===\n";
    auto quiet = makeSink(Mode::Quiet, std::cout);
    simulateSimpleImport(quiet, "math");
    simulateCompositeImport(quiet, "rin.ui", {"core", "layout", "paint", "widgets"});
    std::cout << "(no output above between the two headers — as expected in Quiet mode)\n";

    return 0;
}

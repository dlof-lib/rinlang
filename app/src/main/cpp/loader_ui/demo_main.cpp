// app/src/main/cpp/loader_ui/demo_main.cpp
// ============================================================================
// عرض توضيحي مستقل: يحاكي ما سيحدث لاحقاً عند ربط هذه الوحدة بـ @import
// الفعلي في rin_interpreter.cpp، لكن بدون أي اعتماد عليه. يبني بأمر واحد:
//
//   g++ -std=c++17 -o loader_ui_demo library_loader_ui.cpp demo_main.cpp
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

int main() {
    std::cout << "=== Verbose mode (ConsoleBarSink) ===\n\n";
    auto verbose = makeSink(Mode::Verbose, std::cout);

    simulateSimpleImport(verbose, "math");
    std::cout << "\n";
    simulateCompositeImport(verbose, "rin.ui", {"core", "layout", "paint", "widgets"});
    std::cout << "\n";
    simulateFailedImport(verbose, "not_a_real_lib");

    std::cout << "\n=== Quiet/Silent mode (SilentSink) ===\n";
    auto quiet = makeSink(Mode::Quiet, std::cout);
    simulateSimpleImport(quiet, "math");
    simulateCompositeImport(quiet, "rin.ui", {"core", "layout", "paint", "widgets"});
    std::cout << "(no output above between the two headers — as expected in Quiet mode)\n";

    return 0;
}

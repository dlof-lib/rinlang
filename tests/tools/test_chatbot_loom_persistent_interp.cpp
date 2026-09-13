// tools/test_chatbot_loom_persistent_interp.cpp — verifies the fix for the gap documented in
// README_CHATBOT.md ("سيفشل اليوم... الحاوية غير مسجَّلة بتلك الـ Interpreter المؤقّتة"):
// an onTap handler that calls sendMessage()/botReply() on a real @container.chatbot must see its
// own previous messages on the *next* tap, when the caller passes the same rin::Interpreter
// instance (and seeded flag) into dispatchTap on every tap -- exactly how LoomSession in
// rin_loom_c_api.cpp now does it for every real Android/CLI session.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_chatbot_loom_persistent_interp.cpp \
//       rin_lexer.cpp rin_parser.cpp rin_interpreter.cpp rin_http.cpp \
//       diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz \
//       -o test_chatbot_loom_persistent_interp
#include "rin_loom_pipeline.h"
#include "rin_loom_needle.h"
#include <cassert>
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { \
    if (cond) { std::cout << "  [PASS] " << label << "\n"; } \
    else { std::cout << "  [FAIL] " << label << "\n"; failures++; } \
} while (0)

static loom::StrandPtr byName(const loom::StrandPtr& root, const std::string& n) {
    return loom::findAny(root, [&](const loom::StrandPtr& s){ return s->name == n; });
}

// A single @view.Button whose onTap runs a real `fun` that talks to a real container.chatbot
// ("Support"), plus a second helper `fun` that copies chatMessageCount("Support") into a plain
// `warp` cell so the test can read it back through the same public callTopLevelFunction API that
// Needle itself uses (chatHistoryStore is a private interpreter member -- this is the sanctioned
// way any real .rin script would inspect it too).
static const char* kSource = R"RIN(
warp msgCount = 0;

fun sendToSupport() {
    botReply("Support", "hi");
}

fun checkCount() {
    msgCount = chatMessageCount("Support");
}

@container.chatbot=Support
    text model = "rin-chat-1";
.end/container.chatbot

@view.Button=SendButton
    text = "Send";
    onTap = sendToSupport();
.end/view
)RIN";

// Reads chatMessageCount("Support") on `interp` via the public callTopLevelFunction API (mirrors
// what dispatchTap itself does for a zero-arg onTap handler).
static double readCount(rin::Interpreter& interp, const std::vector<rin::StmtPtr>& program) {
    std::unordered_map<std::string, rin::Value> globals;
    globals["msgCount"] = rin::Value::num(-1);
    std::vector<rin::Value> args;
    std::vector<std::string> aliases;
    std::string err;
    bool ok = interp.callTopLevelFunction(program, "checkCount", args, aliases, globals, err);
    if (!ok) { std::cout << "    (checkCount error: " << err << ")\n"; return -999; }
    return globals["msgCount"].number;
}

int main() {
    std::cout << "-- persistent-interpreter chatbot tap dispatch --\n";

    auto result = loom::runColdPipeline(kSource);
    CHECK(result.ok, "parses");
    if (!result.ok) { std::cout << result.errorMessage << "\n"; return 1; }

    loom::Loom engine;
    engine.layout(result.fabric, {0, 1200, 0, 1e9}, 0, 0);

    auto btn = byName(result.fabric, "SendButton");
    CHECK(btn != nullptr, "SendButton exists in the Fabric");

    rin::Interpreter persistentInterp;
    bool seeded = false;
    double x = btn->geometry.x + 1, y = btn->geometry.y + 1;

    // First tap: seeds the interpreter (runs @container.chatbot=Support once) AND calls
    // sendToSupport(), leaving exactly one message.
    auto tap1 = loom::dispatchTap(result.fabric, result.warp, result.program, x, y,
                                   nullptr, nullptr, &persistentInterp, &seeded);
    CHECK(tap1.handled, "first tap handled");
    CHECK(tap1.error.empty(), "no runtime error on first tap: " + tap1.error);
    CHECK(seeded, "persistent interpreter was seeded after first tap");
    CHECK(readCount(persistentInterp, result.program) == 1,
          "exactly 1 message in chatHistory(\"Support\") after 1 tap");

    // Second tap on the SAME persistent interpreter: must NOT re-run @container.chatbot= (the
    // seeded guard prevents that -- a second run() would also be harmless here, but a container
    // with e.g. `warp memory = {}` should only ever be initialized once per session), and must
    // still see the first message, ending at 2 total.
    auto tap2 = loom::dispatchTap(result.fabric, result.warp, result.program, x, y,
                                   nullptr, nullptr, &persistentInterp, &seeded);
    CHECK(tap2.handled, "second tap handled");
    CHECK(tap2.error.empty(), "no runtime error on second tap: " + tap2.error);
    CHECK(readCount(persistentInterp, result.program) == 2,
          "exactly 2 messages in chatHistory(\"Support\") after 2 taps (state persisted!)");

    // Control, and proof this is really the documented bug being fixed (not a no-op change): the
    // OLD default behavior (omit the two new params) creates a brand-new Interpreter every tap.
    // callTopLevelFunction only hoists top-level `fun`s and seeds Warp cells -- it never executes
    // a `@container.chatbot=` block -- so that fresh Interpreter's containerKinds map never learns
    // "Support" is a chatbot container, and botReply()/sendMessage() correctly refuse to write
    // into an unregistered container. This is exactly the failure README_CHATBOT.md predicted
    // ("سيفشل اليوم") for onTap handlers before this fix; dispatchTap remains fully backward
    // compatible (same failure as before) for any caller that still omits the new params.
    auto tapOld = loom::dispatchTap(result.fabric, result.warp, result.program, x, y);
    CHECK(tapOld.handled, "old-style (no persistent interp) tap still hit-tests/dispatches");
    CHECK(!tapOld.error.empty() &&
          tapOld.error.find("not a chatbot container") != std::string::npos,
          "old-style tap reproduces the documented pre-fix failure: " + tapOld.error);

    if (failures == 0) {
        std::cout << "\nAll checks passed.\n";
        return 0;
    }
    std::cout << "\n" << failures << " check(s) FAILED.\n";
    return 1;
}

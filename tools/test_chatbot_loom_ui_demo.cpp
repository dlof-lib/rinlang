// tools/test_chatbot_loom_ui_demo.cpp — verifies examples/chatbot_loom_ui_demo.rin's Loom UI
// half (warp bubbles + TextArea/placeholder + send Button + onTap) actually parses, lays out,
// and reacts to a real dispatchTap the same way tools/test_loom_button.cpp verifies plain
// counters -- no engine changes, just proof the manual pattern the user asked for really works.
//
// Build (from app/src/main/cpp):
//   g++ -std=c++17 -I. -Iloom ../../../../tools/test_chatbot_loom_ui_demo.cpp rin_lexer.cpp \
//       rin_parser.cpp rin_interpreter.cpp rin_http.cpp \
//       diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
//       diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz \
//       -o test_chatbot_loom_ui_demo
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
static void layoutAt(loom::PipelineResult& r, double width = 1200) {
    loom::Loom engine;
    engine.layout(r.fabric, {0, width, 0, 1e9}, 0, 0);
}

// Only the Loom (@view/warp/fun) half of the demo -- the trailing container.chatbot half is
// exercised separately by the plain interpreter (see chatbot_container_demo.rin's own test),
// and, as documented in the .rin file itself, is intentionally NOT reachable from onTap today.
static const char* kSource = R"RIN(
warp bubble1_role = ""; warp bubble1_text = "";
warp bubble2_role = ""; warp bubble2_text = "";
warp bubble3_role = ""; warp bubble3_text = "";
warp bubble4_role = ""; warp bubble4_text = "";
warp draftText = "";

fun sendChat() {
    bubble1_role = bubble2_role; bubble1_text = bubble2_text;
    bubble2_role = bubble3_role; bubble2_text = bubble3_text;
    bubble3_role = "user"; bubble3_text = draftText;
    bubble4_role = "bot";  bubble4_text = "تلقيت: " + draftText;
    draftText = "";
}

@view.Column=ChatScreen
    @view.Text=Title
        text="محادثة (مثال UI)";
    .end/view
    @view.Text=Bubble1
        text = bubble1_role + ": " + bubble1_text;
    .end/view
    @view.Text=Bubble2
        text = bubble2_role + ": " + bubble2_text;
    .end/view
    @view.Text=Bubble3
        text = bubble3_role + ": " + bubble3_text;
    .end/view
    @view.Text=Bubble4
        text = bubble4_role + ": " + bubble4_text;
    .end/view
    @view.TextArea=DraftInput
        value = draftText;
        placeholder = "اكتب رسالتك هنا...";
    .end/view
    @view.Button=SendButton
        label = "إرسال";
        onTap = sendChat();
    .end/view
.end/view
)RIN";

int main() {
    std::cout << "-- chatbot Loom UI demo: parses + lays out --\n";
    auto r = loom::runColdPipeline(kSource);
    CHECK(r.ok, "parses");
    if (!r.ok) { std::cout << r.errorMessage << " (line " << r.errorLine << ")\n"; return 1; }
    layoutAt(r);

    auto input = byName(r.fabric, "DraftInput");
    CHECK(input != nullptr, "TextArea(DraftInput) exists in the Fabric");
    CHECK(input->attrStr("placeholder", "") == "اكتب رسالتك هنا...", "placeholder= is set correctly");
    auto send = byName(r.fabric, "SendButton");
    CHECK(send != nullptr, "Button(SendButton) exists");
    CHECK(send->attrStr("label", "") == "إرسال", "send button label is correct");

    auto bubble3 = byName(r.fabric, "Bubble3");
    auto bubble4 = byName(r.fabric, "Bubble4");
    CHECK(bubble3->attrStr("text", "") == ": ", "Bubble3 starts empty");
    CHECK(bubble4->attrStr("text", "") == ": ", "Bubble4 starts empty");

    // Simulate the platform bridge that would normally capture real keyboard input into
    // draftText (out of scope for this headless engine -- see the .rin file's top comment).
    r.warp.set("draftText", loom::Value::txt("مرحباً يا بوت"));

    double cx = send->geometry.x + 1, cy = send->geometry.y + 1;
    auto tap = loom::dispatchTap(r.fabric, r.warp, r.program, cx, cy);
    CHECK(tap.handled, "tapping Send is handled by Needle");
    CHECK(tap.error.empty(), std::string("no runtime error: ") + tap.error);

    CHECK(r.warp.get("draftText").asString() == "", "draftText cleared after send");
    CHECK(r.warp.get("bubble3_role").asString() == "user", "bubble3 became the new user message");
    CHECK(r.warp.get("bubble3_text").asString() == "مرحباً يا بوت", "bubble3 text is the typed message");
    CHECK(r.warp.get("bubble4_role").asString() == "bot", "bubble4 became the bot's echo reply");
    CHECK(r.warp.get("bubble4_text").asString() == "تلقيت: مرحباً يا بوت", "bubble4 text is the echo");

    // A second send proves the shift-up logic (bubble1/2 receive the previous bubble2/3 pair)
    // works across repeated taps, not just once from a blank slate.
    r.warp.set("draftText", loom::Value::txt("سؤال ثاني"));
    auto tap2 = loom::dispatchTap(r.fabric, r.warp, r.program, cx, cy);
    CHECK(tap2.handled, "second tap handled");
    CHECK(r.warp.get("bubble2_role").asString() == "user", "the first user message shifted into slot 2");
    CHECK(r.warp.get("bubble2_text").asString() == "مرحباً يا بوت", "...with its original text");
    CHECK(r.warp.get("bubble3_role").asString() == "user" &&
          r.warp.get("bubble3_text").asString() == "سؤال ثاني", "newest user message shifted into slot 3");

    std::cout << (failures == 0 ? "\nAll checks passed.\n" : "\nSome checks FAILED.\n");
    return failures == 0 ? 0 : 1;
}

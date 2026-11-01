// tests/tools/test_indsin_time.cpp — time داخل indsin (docs/TIME.md): دوال time.* نقية في الخصائص،
// وhارس النقاء يرفض time.sleep/measure/config، والساعة الحيّة `warp clock` تتقدّم مع rin_indsin_session_tick.
//
// Build & run (من app/src/main/cpp؛ وصفة بقية اختبارات indsin + مصدر الواجهة C):
//   g++ -std=c++17 -I. -Iindsin ../../../../tests/tools/test_indsin_time.cpp rin_lexer.cpp rin_parser.cpp \
//       rin_make.cpp rin_container_sql.cpp rin_artifact.cpp rin_interpreter.cpp rin_c_api.cpp indsin/rin_indsin_c_api.cpp \
//       toolchain/rin_toolchain.cpp binfmt/*.cpp loader_ui/library_loader_ui.cpp rin_http.cpp diagnostics/*.cpp \
//       clc/*.cpp -lz -ldl -lpthread -o test_indsin_time && ./test_indsin_time
#include "rin_c_api.h"
#include "indsin/rin_indsin_c_api.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

static int checks = 0, failures = 0;
static void check(bool cond, const std::string& label) {
    checks++;
    if (!cond) { failures++; std::cerr << "FAIL: " << label << "\n"; } else std::cout << "ok:   " << label << "\n";
}
static std::string run(char* p) { std::string s = p ? p : ""; rin_free_string(p); return s; }
static bool has(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

int main() {
    // ---- 1) دوال time.* النقية داخل خصائص @view (قيم ثابتة فحتمية)
    const char* pure = R"RIN(
let t = time.utc("2026-10-11T14:30:45Z");
@view.Column=Root
  @view.Text=A text=time.format(t, "YYYY-MM-DD HH:mm"); .end/view
  @view.Text=B text=time.add(t, 3, "days").date; .end/view
  @view.Text=C text=time.diff(time.utc("2026-12-31T00:00:00Z"), t, "days"); .end/view
.end/view
)RIN";
    std::string out = run(rin_indsin_render_json(pure, 390));
    check(has(out, "\"ok\":true"), "render ok");
    check(has(out, "2026-10-11 14:30"), "time.format داخل خاصية");
    check(has(out, "2026-10-14"), "time.add(...).date داخل خاصية");
    check(has(out, "80.3"), "time.diff(..., \"days\") داخل خاصية");

    // ---- 2) حارس النقاء: time.sleep لا يُنفَّذ داخل خاصية (يعود السلوك القديم: النص الحرفي)
    const char* impure = R"RIN(
@view.Column=Root
  @view.Text=S text=time.sleep(2000); .end/view
.end/view
)RIN";
    auto t0 = std::chrono::steady_clock::now();
    out = run(rin_indsin_render_json(impure, 390));
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    check(ms < 500, "time.sleep لم يُنفَّذ داخل الخاصية");

    // ---- 3) الساعة الحيّة
    const char* live = R"RIN(
warp clock = time.ms();
@view.Column=Root
  @view.Text=Clock text=time(clock, "YYYY"); .end/view
  @view.Text=Sec text=time(clock, "ss"); .end/view
.end/view
)RIN";
    void* sess = rin_indsin_session_create(live, 390);
    check(sess != nullptr, "session_create");
    out = run(rin_indsin_session_render_json(sess));
    check(has(out, "\"ok\":true"), "session render ok");
    check(has(out, "2026") || has(out, "2027") || has(out, "2028"), "الساعة تعرض السنة الحالية");
    out = run(rin_indsin_session_tick(sess));
    check(has(out, "\"ticking\":true"), "tick يعلن ticking");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    out = run(rin_indsin_session_tick(sess));
    check(has(out, "\"changed\":[\"clock\"]"), "tick بعد ثانية يحدّث clock");

    // ---- 4) بلا warp clock لا ticking
    void* plain = rin_indsin_session_create(pure, 390);
    out = run(rin_indsin_session_tick(plain));
    check(!has(out, "ticking"), "بلا clock لا يوجد ticking");

    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}

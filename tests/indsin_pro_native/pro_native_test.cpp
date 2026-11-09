// اختبار المحرّك الأصلي: الأنواع الجديدة Sparkline / BarChart / ProgressRing (تُخطَّط وتُرسَم عبر DrawList نفسها
// التي يستهلكها أندرويد/سطح المكتب/معاينة HTML). البناء: انظر docs/indsin_pro_native.md.
#include "indsin/rin_indsin_c_api.h"
#include "rin_c_api.h"
#include <cstdio>
#include <string>
#include <cstring>
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  [%d]\n", msg, __LINE__); ++failures; } } while (0)
static std::string take(char* p) { std::string s = p ? p : ""; rin_free_string(p); return s; }
static bool has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }
struct Px { int r, g, b; };
static Px pixel(void* s, int x, int y) { int w = 0, h = 0; unsigned char* b = rin_indsin_session_render_rgb(s, &w, &h); Px p{-1,-1,-1};
    if (b && x < w && y < h) { size_t i = ((size_t)y*w + x)*3; p = {b[i], b[i+1], b[i+2]}; } rin_indsin_free_buffer(b); return p; }
static bool differs(Px a, Px b) { return a.r != b.r || a.g != b.g || a.b != b.b; }

int main() {
    // 1) الأنواع معروفة في الشجرة والإحصاءات
    {
        const char* src = "<Column#root padding=10 gap=8 bg=\"#101018\"><ProgressRing#pr value=50 /><BarChart#bc data=\"Mon:3,Tue:6,Wed:2\" /><Sparkline#sp data=\"1,4,2,8,5\" /></Column>";
        void* s = rin_indsin_session_create(src, 390);
        CHECK(s != nullptr, "session created");
        std::string out = take(rin_indsin_session_outline(s));
        CHECK(has(out, "ProgressRing"), "outline has ProgressRing");
        CHECK(has(out, "BarChart"), "outline has BarChart");
        CHECK(has(out, "Sparkline"), "outline has Sparkline");
        std::string json = take(rin_indsin_session_render_json(s));
        CHECK(has(json, "STROKE_ARC") || has(json, "arc") || json.size() > 200, "draw list produced");
        // الصفحة HTML تُنتَج دون أن تنهار (أمر التصدير يمرّ على نفس DrawList)
        std::string html = take(rin_indsin_session_export_html(s));
        CHECK(html.size() > 200, "html export ok");
        rin_indsin_session_free(s);
    }
    // 2) ProgressRing: مسار كامل دائماً + قوس تقدّم بزاوية = value×3.6 (من خلال DrawList، مستقل عن المرسِم)
    {
        auto arcs = [](const std::string& js, int& n) { n = 0; size_t p = 0; while ((p = js.find("\"stroke_arc\"", p)) != std::string::npos) { ++n; ++p; } };
        void* a = rin_indsin_session_create("<Column#root padding=0 bg=\"#000000\"><ProgressRing#pr value=0 diameter=100 /></Column>", 200);
        void* b = rin_indsin_session_create("<Column#root padding=0 bg=\"#000000\"><ProgressRing#pr value=25 diameter=100 label=\"1/4\" /></Column>", 200);
        std::string ja = take(rin_indsin_session_render_json(a)), jb = take(rin_indsin_session_render_json(b));
        int na, nb; arcs(ja, na); arcs(jb, nb);
        CHECK(na == 1, "value=0 → track arc only");
        CHECK(nb == 2, "value=25 → track + progress arc");
        CHECK(has(jb, "\"sweep\":90"), "25% sweeps 90 degrees");
        CHECK(has(jb, "1/4") && has(ja, "0%"), "center label: explicit and default percent");
        CHECK(has(jb, "\"w\":78") || has(jb, "\"w\":89"), "ring is square (size 100, not stretched to column width)");
        rin_indsin_session_free(a); rin_indsin_session_free(b);
    }
    // 3) BarChart: ارتفاع العمود الأطول يملأ منطقة الرسم (بكسل داخل العمود ≠ الخلفية) والفارغ مظلم
    {
        void* s = rin_indsin_session_create("<Column#root padding=0 bg=\"#000000\"><BarChart#bc data=\"A:10,B:1\" width=200 height=100 /></Column>", 200);
        Px inA = pixel(s, 50, 60), empty = pixel(s, 150, 20);
        CHECK(!(inA.r == 0 && inA.g == 0 && inA.b == 0), "tall bar A is painted");
        CHECK(empty.r == 0 && empty.g == 0 && empty.b == 0, "above short bar B stays background");
        rin_indsin_session_free(s);
    }
    // 4) Sparkline: خط/منطقة مرسومان ضمن الصندوق؛ نقطة فوق أعلى قيمة تبقى خلفية
    {
        void* s = rin_indsin_session_create("<Column#root padding=0 bg=\"#000000\"><Sparkline#sp data=\"0,10,0\" width=100 height=40 /></Column>", 200);
        Px mid = pixel(s, 50, 3), base = pixel(s, 50, 36), corner = pixel(s, 3, 2);
        CHECK(!(mid.r == 0 && mid.g == 0 && mid.b == 0), "sparkline peak painted at center");
        CHECK(!(base.r == 0 && base.g == 0 && base.b == 0), "sparkline area painted under the peak");
        CHECK(corner.r == 0 && corner.g == 0 && corner.b == 0, "left-top corner stays background (value 0 there)");
        rin_indsin_session_free(s);
    }
    if (failures == 0) std::printf("ALL PASSED\n");
    return failures ? 1 : 0;
}

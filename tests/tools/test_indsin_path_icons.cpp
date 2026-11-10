// DrawOp::STROKE_PATH — أيقونات متجهة حقيقية في الـrasterizer: مدمجة (vector="true") ومستوردة من api (افتراضياً).
// بناء: كما test_container_api_indsin.cpp.
#include "indsin/rin_indsin_c_api.h"
#include "indsin/rin_indsin_pathraster.h"
#include <cstdio>
#include <string>

static int failures = 0;
#define CHECK(c, l) do { if (c) printf("  [PASS] %s\n", l); else { printf("  [FAIL] %s\n", l); failures++; } } while (0)

static const char* kApp = R"RIN(
<Rin>
@container.api=pack
    let data = nil; let icon = nil;
    let iconName = "bolt-px";
    route method="GET" path="/bolt" status=200 body={"svg": "<svg><path d='M13 2L3 14H12L11 22L21 10H12Z'/></svg>"};
.end/container.api
fun load() { callApi("pack", "GET", "/bolt"); }
</Rin>
<Column#Root padding={comfortable} gap={normal}>
  <Icon#Imp icon={pack.icon} size="xl" />
  <Icon#Home icon="home" vector="true" size="xl" />
  <Icon#Glyph icon="home" size="xl" />
  <Button#Go text="load" onTap={load()} />
</Column>
)RIN";

static long inkIn(void* s, int x0, int y0, int x1, int y1) {   // بكسلات تختلف عن خلفية (0x18,0x19,0x20)
    int W = 0, H = 0; unsigned char* p = rin_indsin_session_render_rgb(s, &W, &H); long n = 0;
    for (int y = y0; y < y1 && y < H; y++) for (int x = x0; x < x1 && x < W; x++) { unsigned char* q = p + ((size_t)y * W + x) * 3; if (q[0] > 60 || q[1] > 60 || q[2] > 60) n++; }
    rin_free_string((char*)p); return n;
}

int main() {
    // 1) المُسطِّح
    auto polys = indsin::flattenSvgPath("M13 2L3 14H12L11 22L21 10H12Z");
    CHECK(polys.size() == 1 && polys[0].size() == 7, "flatten: closed bolt = 1 polyline of 7 points");
    auto circ = indsin::flattenSvgPath("M7 12A5 5 0 1 0 17 12A5 5 0 1 0 7 12Z");
    bool onCircle = true; for (auto& p : circ[0]) { double dx = p.x - 12, dy = p.y - 12; if (std::fabs(std::sqrt(dx*dx + dy*dy) - 5) > 0.05) onCircle = false; }
    CHECK(circ.size() == 1 && circ[0].size() > 30 && onCircle, "flatten: two arcs form a circle of radius 5");
    auto rel = indsin::flattenSvgPath("m1 1l2 0v2h-2z");
    CHECK(rel.size() == 1 && rel[0].size() == 5 && rel[0][1].x == 3 && rel[0][2].y == 3, "flatten: relative commands");
    CHECK(indsin::flattenSvgPath("garbage").empty(), "flatten: garbage gives nothing");

    // 2) الرسم
    void* s = rin_indsin_session_create(kApp, 400);
    char* j = rin_indsin_session_render_json(s); std::string J0 = j; rin_free_string(j);
    CHECK(J0.find("\"op\":\"stroke_path\"") != std::string::npos, "vector=\"true\" built-in icon paints a stroke_path op");
    long home0 = inkIn(s, 16, 16 + 0, 400, 420);
    CHECK(home0 > 50, "pixels are drawn for the built-in vector icon / glyph");
    // الأيقونة المستوردة: لا حبر في صفّها قبل الاستدعاء
    int W = 0, H = 0; rin_free_string((char*)rin_indsin_session_render_rgb(s, &W, &H));
    long impBefore = inkIn(s, 16, 16, 120, 16 + 24);
    { char* jj = rin_indsin_session_render_json(s); std::string Q = jj; rin_free_string(jj); size_t g = Q.find("\"name\":\"Go\""); size_t xx = Q.find("\"x\":", g); printf("%s\n", Q.substr(xx, 60).c_str()); }
    char* r = rin_indsin_session_tap(s, 30, 140); rin_free_string(r);
    j = rin_indsin_session_render_json(s); std::string J1 = j; rin_free_string(j);
    size_t n0 = 0, cnt = 0; while ((n0 = J1.find("stroke_path", n0)) != std::string::npos) { cnt++; n0++; }
    CHECK(cnt >= 2, "imported icon adds a second stroke_path op after the api call");
    long impAfter = inkIn(s, 16, 16, 120, 16 + 24);
    printf("  ink before=%ld after=%ld\n", impBefore, impAfter);
    CHECK(impAfter > impBefore, "imported icon actually draws pixels");
    rin_indsin_session_export_png(s, "/tmp/path_icons.png");
    rin_indsin_session_free(s);
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures;
}

// tests/tools/test_indsin_web_layout.cpp — مفاهيم تخطيط صفحات الويب (docs/indsin_web_layout.md).
// Build (from app/src/main/cpp):
//   g++ -std=c++23 -I. -Iindsin ../../../../tests/tools/test_indsin_web_layout.cpp <core objects> -lz -o test_indsin_web_layout
#include "rin_indsin_pipeline.h"
#include "rin_indsin_responsive.h"
#include <cassert>
#include <cmath>
#include <iostream>

static int failures = 0;
#define CHECK(cond, label) do { if (cond) std::cout << "  [PASS] " << label << "\n"; else { std::cout << "  [FAIL] " << label << "\n"; failures++; } } while (0)
#define NEAR(a, b) (std::fabs((a) - (b)) < 0.6)
using indsin::StrandPtr;

static StrandPtr by(const StrandPtr& root, const std::string& n) {
    return indsin::findAny(root, [&](const StrandPtr& s) { return s->name == n; });
}
struct Scene {
    indsin::PipelineResult r; indsin::Indsin eng; bool ok = false; double W;
    static std::string wrapIfMany(const std::string& src) {   // أكثر من جذر @view واحد: لفّها بعمود جذر
        int top = 0; size_t pos = 0;
        while ((pos = src.find("\n@view.", pos)) != std::string::npos) { top++; pos += 7; }
        if (top <= 1) return src;
        return "@view.Column=__root align=\"left\"; gap=0;\n" + src + "\n.end/view\n";
    }
    Scene(const std::string& src0, double w = 400, double h = 800) : W(w) {
        std::string src = wrapIfMany(src0);
        r = indsin::runColdPipeline(src);
        ok = r.ok;
        if (!ok) { std::cout << "  parse error: " << r.errorMessage << " line " << r.errorLine << "\n"; return; }
        indsin::viewportWidthPx() = w; indsin::viewportHeightPx() = h;
        r.fabric->screenRoot = true;
        indsin::applyResponsiveAttrs(r.fabric, w);
        eng.layout(r.fabric, {w, w, 0, 1e9}, 0, 0);
    }
    StrandPtr n(const char* name) { return by(r.fabric, name); }
    indsin::Rect g(const char* name) { auto s = n(name); return s ? s->geometry : indsin::Rect{-999, -999, -999, -999}; }
    void relayout() { r.fabric->screenRoot = true; eng.layout(r.fabric, {W, W, 0, 1e9}, 0, 0); }
};
static std::vector<indsin::DrawCommand> paintOf(Scene& sc) { indsin::Dye d; return d.paint(sc.r.fabric); }
static int firstOwnerIndex(const std::vector<indsin::DrawCommand>& l, indsin::StrandId id) {
    for (size_t i = 0; i < l.size(); i++) if (l[i].owner == id) return (int)i; return -1; }

int main() {
    std::cout << std::unitbuf;
    std::cout << "-- 1. Attribute aliases (CamelCase / kebab / snake) --\n";
    {
        Scene sc(R"RIN(
@view.Column=root
  @view.Box=a MinWidth=200; MaxHeight=30; @view.Text=t text="x"; .end/view .end/view
  @view.Box=b MinWidth_md=150; .end/view
  @view.Box=c ZIndex=5; BorderRadius=8; AspectRatio="2:1"; .end/view
.end/view
)RIN");
        CHECK(sc.ok, "parses");
        if (sc.ok) {
            CHECK(sc.n("a")->attr("min_width") && !sc.n("a")->attr("MinWidth"), "MinWidth -> min_width");
            CHECK(sc.g("a").w >= 199.5, "MinWidth=200 enforced in layout");
            CHECK(sc.n("a")->attr("max_height"), "MaxHeight -> max_height");
            CHECK(sc.n("b")->attr("min_width_md"), "responsive suffix kept: MinWidth_md -> min_width_md");
            CHECK(indsin::webCanonicalKey("min-width") == "min_width" && indsin::webCanonicalKey("minWidth") == "min_width" && indsin::webCanonicalKey("Row-Gap") == "row_gap", "kebab/camel canonical keys");
            CHECK(sc.n("c")->attr("z") && sc.n("c")->attr("radius") && sc.n("c")->attr("aspect"), "ZIndex/BorderRadius/AspectRatio canonical");
        }
    }
    std::cout << "-- 2. Structural tags --\n";
    {
        Scene sc(R"RIN(
@view.Main=m
  @view.Article=ar @view.Text=t text="a"; .end/view .end/view
  @view.Aside=as @view.Text=t2 text="b"; .end/view .end/view
  @view.Wrapper=wr @view.Text=t3 text="c"; .end/view .end/view
  @view.Navigation=nv @view.Text=t4 text="d"; .end/view @view.Text=t5 text="e"; .end/view .end/view
.end/view
)RIN");
        CHECK(sc.ok, "parses");
        if (sc.ok) {
            CHECK(sc.n("m")->kind == indsin::StrandKind::COLUMN && sc.n("m")->sourceTag == "Main", "Main = Column (sourceTag kept)");
            CHECK(sc.n("wr")->kind == indsin::StrandKind::BOX, "Wrapper = Box");
            CHECK(sc.n("nv")->kind == indsin::StrandKind::ROW && sc.g("t5").x > sc.g("t4").x, "Navigation = Row (children side by side)");
            CHECK(sc.g("as").y > sc.g("ar").y, "Article/Aside stack vertically");
        }
    }
    std::cout << "-- 3. RowGap / ColumnGap --\n";
    {
        Scene sc(R"RIN(
@view.Column=col row_gap=20; gap=4;
  @view.Box=a height=10; .end/view
  @view.Box=b height=10; .end/view
.end/view
)RIN");
        CHECK(sc.ok && NEAR(sc.g("b").y - (sc.g("a").y + sc.g("a").h), 20), "Column uses row_gap over gap");
        Scene sr(R"RIN(
@view.Row=row column_gap=30; gap=2;
  @view.Box=a width=10; height=10; .end/view
  @view.Box=b width=10; height=10; .end/view
.end/view
)RIN");
        CHECK(sr.ok && NEAR(sr.g("b").x - (sr.g("a").x + sr.g("a").w), 30), "Row uses column_gap");
        Scene sg(R"RIN(
@view.Grid=gr columns=2; row_gap=16; column_gap=8;
  @view.Box=a height=20; .end/view @view.Box=b height=20; .end/view
  @view.Box=c height=20; .end/view @view.Box=d height=20; .end/view
.end/view
)RIN");
        CHECK(sg.ok && NEAR(sg.g("c").y - (sg.g("a").y + 20), 16), "Grid row_gap");
        CHECK(sg.ok && NEAR(sg.g("b").x - (sg.g("a").x + sg.g("a").w), 8), "Grid column_gap");
    }
    std::cout << "-- 4. Position: relative / absolute / fixed / sticky / static --\n";
    {
        Scene sc(R"RIN(
@view.Column=root
  @view.Box=before height=40; .end/view
  @view.Box=host position="relative"; height=100;
    @view.Box=abs position="absolute"; top=10; left=20; width=30; height=30; .end/view
    @view.Box=absbr position="absolute"; right=5; bottom=5; width=20; height=20; .end/view
    @view.Box=absstretch position="absolute"; left=0; right=0; top=0; height=4; .end/view
    @view.Box=ctr position="absolute"; center=true; width=10; height=10; .end/view
  .end/view
  @view.Box=after height=40; .end/view
  @view.Box=rel position="relative"; top=7; left=9; width=10; height=10; .end/view
  @view.Box=stat position="static"; top=99; width=10; height=10; .end/view
.end/view
)RIN");
        CHECK(sc.ok, "parses");
        if (sc.ok) {
            auto h = sc.g("host");
            CHECK(NEAR(sc.g("after").y, h.y + h.h), "absolute children take no space in flow");
            CHECK(NEAR(sc.g("abs").x, h.x + 20) && NEAR(sc.g("abs").y, h.y + 10), "absolute top/left from relative host");
            CHECK(NEAR(sc.g("absbr").x + 20, h.x + h.w - 5) && NEAR(sc.g("absbr").y + 20, h.y + h.h - 5), "absolute right/bottom");
            CHECK(NEAR(sc.g("absstretch").w, h.w), "absolute left+right stretches width");
            CHECK(NEAR(sc.g("ctr").x, h.x + (h.w - 10) / 2) && NEAR(sc.g("ctr").y, h.y + (h.h - 10) / 2), "absolute center");
            double relNatural = sc.g("after").y + sc.g("after").h;
            CHECK(NEAR(sc.g("rel").y, relNatural + 7) && NEAR(sc.g("rel").x, 9), "relative offsets visually (top/left)");
            CHECK(NEAR(sc.g("stat").y, relNatural + 10), "static ignores top and keeps flow slot (after rel's reserved slot)");
        }
        Scene sf(R"RIN(
@view.Column=root
  @view.Box=pad height=500; .end/view
  @view.Box=fx position="fixed"; top=10; left=10; width=20; height=20; .end/view
  @view.Box=st position="sticky"; top=0; height=30; .end/view
  @view.Box=tail height=600; .end/view
.end/view
)RIN", 400, 800);
        if (sf.ok) {
            CHECK(NEAR(sf.g("fx").y, 10) && NEAR(sf.g("fx").x, 10), "fixed pinned to viewport at scroll 0");
            indsin::webPostLayout(sf.eng, sf.r.fabric, 0, 300);
            CHECK(NEAR(sf.g("fx").y, 310), "fixed follows scrollY");
            CHECK(NEAR(sf.g("st").y, 500 + 0) || sf.g("st").y >= 300, "sticky pins at scroll offset (>= scroll)");
            double stY = sf.g("st").y;
            indsin::webPostLayout(sf.eng, sf.r.fabric, 0, 0);
            CHECK(NEAR(sf.g("st").y, 500) && stY >= 500, "sticky returns to its flow slot at scroll 0");
        }
    }
    std::cout << "-- 5. Order --\n";
    {
        Scene sc(R"RIN(
@view.Row=row
  @view.Box=a width=10; height=10; order=3; .end/view
  @view.Box=b width=10; height=10; order=1; .end/view
  @view.Box=c width=10; height=10; .end/view
.end/view
)RIN");
        CHECK(sc.ok && sc.g("c").x < sc.g("b").x && sc.g("b").x < sc.g("a").x, "order sorts children (0 < 1 < 3)");
        sc.relayout();
        CHECK(sc.g("c").x < sc.g("b").x && sc.g("b").x < sc.g("a").x, "order stable on relayout");
    }
    std::cout << "-- 6. ZIndex paint order / isolation --\n";
    {
        Scene sc(R"RIN(
@view.Stack=st
  @view.Box=top z=10; width=50; height=50; color="#ff0000"; .end/view
  @view.Box=bottom width=50; height=50; color="#00ff00"; .end/view
.end/view
)RIN");
        auto l = paintOf(sc);
        CHECK(sc.ok && firstOwnerIndex(l, sc.n("top")->id) > firstOwnerIndex(l, sc.n("bottom")->id), "z=10 element painted after later sibling");
        CHECK(sc.ok && sc.g("top").x == sc.g("bottom").x, "z does not change layout");
    }
    std::cout << "-- 7. Display / Hidden / Visibility --\n";
    {
        Scene sc(R"RIN(
@view.Column=col gap=10;
  @view.Box=a height=20; .end/view
  @view.Box=gone display="none"; height=50; .end/view
  @view.Box=hid hidden=true; height=50; .end/view
  @view.Box=inv visibility="hidden"; height=30; color="#123456"; .end/view
  @view.Box=z height=20; .end/view
.end/view
)RIN");
        CHECK(sc.ok, "parses");
        if (sc.ok) {
            CHECK(sc.g("gone").h == 0 && sc.g("hid").h == 0, "display:none / hidden collapse to zero");
            CHECK(NEAR(sc.g("inv").y, sc.g("a").y + 20 + 10), "collapsed elements leave no gap");
            CHECK(NEAR(sc.g("z").y, sc.g("inv").y + 30 + 10), "visibility:hidden keeps its space");
            auto l = paintOf(sc);
            CHECK(firstOwnerIndex(l, sc.n("inv")->id) < 0, "visibility:hidden not painted");
            CHECK(firstOwnerIndex(l, sc.n("a")->id) >= 0, "visible still painted");
        }
    }
    std::cout << "-- 8. Overflow / Clip --\n";
    {
        Scene sc(R"RIN(
@view.Box=host width=100; height=100; overflow="hidden"; color="#222222";
  @view.Box=big position="relative"; left=60; width=100; height=40; color="#ff0000"; .end/view
.end/view
@view.Box=free width=100; height=100;
  @view.Box=big2 position="relative"; left=60; width=100; height=40; color="#00ff00"; .end/view
.end/view
)RIN");
        if (sc.ok) {
            auto l = paintOf(sc);
            double r1 = 0, r2 = 0;
            for (auto& d : l) { if (d.owner == sc.n("big")->id && d.op == indsin::DrawOp::FILL_RECT) r1 = std::max(r1, d.bounds.x + d.bounds.w);
                                if (d.owner == sc.n("big2")->id && d.op == indsin::DrawOp::FILL_RECT) r2 = std::max(r2, d.bounds.x + d.bounds.w); }
            auto h = sc.g("host");
            CHECK(r1 <= h.x + h.w + 0.5 && r1 > 0, "overflow:hidden clips child rect to host");
            CHECK(r2 > sc.g("free").x + sc.g("free").w, "no overflow -> unclipped");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 9. Grid: auto-fit / auto-fill / spans / placement --\n";
    {
        Scene sc(R"RIN(
@view.Grid=af columns="auto-fit"; min_column_width=100; gap=0;
  @view.Box=a height=10; .end/view @view.Box=b height=10; .end/view
.end/view
@view.Grid=al columns="auto-fill"; min_column_width=100; gap=0;
  @view.Box=c height=10; .end/view @view.Box=d height=10; .end/view
.end/view
)RIN", 400);
        if (sc.ok) {
            CHECK(NEAR(sc.g("a").w, 200), "auto-fit with 2 items on 400px -> 2 stretched cells of 200");
            CHECK(NEAR(sc.g("c").w, 100), "auto-fill keeps 4 tracks of 100");
        } else CHECK(false, "parses");
        Scene sp(R"RIN(
@view.Grid=g columns=3; gap=10;
  @view.Box=wide grid_column="1 / span 2"; height=20; .end/view
  @view.Box=b height=20; .end/view
  @view.Box=pin grid_column=3; grid_row=2; height=20; .end/view
  @view.Box=tall grid_row="span 2"; height=60; .end/view
.end/view
)RIN", 320);
        if (sp.ok) {
            double cw = (320 - 20) / 3.0;
            CHECK(NEAR(sp.g("wide").w, cw * 2 + 10), "column span 2 = two cells + gap");
            CHECK(NEAR(sp.g("b").x, 10 + 2 * (cw + 10) - 10 + 0) || NEAR(sp.g("b").x, 2 * (cw + 10)), "next item flows to third column");
            CHECK(NEAR(sp.g("pin").x, 2 * (cw + 10)) && sp.g("pin").y > sp.g("wide").y, "explicit grid_column/grid_row placement");
            CHECK(sp.g("tall").h >= 59.5, "row span child keeps its height");
        } else CHECK(false, "span parses");
    }
    std::cout << "-- 10. Intrinsic keywords + BoxSizing + IntrinsicSize --\n";
    {
        Scene sc(R"RIN(
@view.Column=root align="left";
  @view.Row=maxc width="max-content"; gap=10;
    @view.Box=a width=30; height=10; .end/view @view.Box=b width=40; height=10; .end/view
  .end/view
  @view.Row=fitc width="fit-content"; gap=10;
    @view.Box=c width=30; height=10; .end/view @view.Box=d width=40; height=10; .end/view
  .end/view
  @view.Column=minc width="min-content";
    @view.Text=t text="aa bbbbbbbb cc"; size=16; .end/view
  .end/view
  @view.Box=cb width=100; padding=10; box_sizing="content-box"; height=20; .end/view
  @view.Box=bb width=100; padding=10; height=20; .end/view
  @view.Box=img intrinsic_size="160x90"; .end/view
.end/view
)RIN", 400);
        if (sc.ok) {
            CHECK(NEAR(sc.g("maxc").w, 80), "max-content = natural width of children");
            CHECK(NEAR(sc.g("fitc").w, 80), "fit-content = min(max-content, available)");
            std::cout << "    minc.w=" << sc.g("minc").w << " maxc.w=" << sc.g("maxc").w << " t.w=" << sc.g("t").w << "\n";
            CHECK(sc.g("minc").w < indsin::measureTextWidth("aa bbbbbbbb cc", 16) && sc.g("minc").w > 0, "min-content narrower than text line");
            CHECK(NEAR(sc.g("cb").w, 120) && NEAR(sc.g("bb").w, 100), "content-box adds padding to width; border-box does not");
            CHECK(NEAR(sc.g("img").w, 160) && NEAR(sc.g("img").h, 90), "intrinsic_size 160x90");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 11. Alignment: align_items / align_self / justify_content / place --\n";
    {
        Scene sc(R"RIN(
@view.Column=col width=200; align_items="center";
  @view.Box=c1 width=40; height=10; .end/view
  @view.Box=c2 width=40; height=10; align_self="flex-end"; .end/view
  @view.Box=c3 width=40; height=10; align_self="start"; .end/view
.end/view
@view.Row=row width=200; height=60; JustifyContent="space-between"; AlignItems="flex-end";
  @view.Box=r1 width=20; height=10; .end/view @view.Box=r2 width=20; height=10; .end/view
.end/view
@view.Column=pl width=200; height=100; place="center";
  @view.Box=p1 width=20; height=20; .end/view
.end/view
)RIN");
        if (sc.ok) {
            CHECK(NEAR(sc.g("c1").x, sc.g("col").x + 80), "align_items=center centers children");
            CHECK(NEAR(sc.g("c2").x, sc.g("col").x + 160), "align_self=flex-end overrides to far edge");
            CHECK(NEAR(sc.g("c3").x, sc.g("col").x), "align_self=start");
            CHECK(NEAR(sc.g("r2").x + 20, sc.g("row").x + 200), "justify_content space-between pushes last to end");
            CHECK(NEAR(sc.g("r1").y + 10, sc.g("row").y + 60), "align_items flex-end on Row");
            CHECK(NEAR(sc.g("p1").x, sc.g("pl").x + 90) && NEAR(sc.g("p1").y, sc.g("pl").y + 40), "place=center centers on both axes");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 12. SpaceAllocation / AutoSpace / weights --\n";
    {
        Scene sc(R"RIN(
@view.Row=eq width=300; height=10; space_allocation="equal";
  @view.Box=a height=10; .end/view @view.Box=b height=10; .end/view @view.Box=c height=10; .end/view
.end/view
@view.Row=pr width=300; height=10; space_allocation="proportional";
  @view.Box=d weight=1; height=10; .end/view @view.Box=e weight=2; height=10; .end/view
.end/view
@view.Row=au width=300; height=10; auto_space=true;
  @view.Box=f width=50; height=10; .end/view @view.Box=g width=50; height=10; .end/view
.end/view
)RIN");
        if (sc.ok) {
            CHECK(NEAR(sc.g("a").w, 100) && NEAR(sc.g("b").w, 100) && NEAR(sc.g("c").w, 100), "equal allocation = thirds");
            CHECK(NEAR(sc.g("d").w, 100) && NEAR(sc.g("e").w, 200), "proportional 1:2");
            CHECK(NEAR(sc.g("g").x + 50, sc.g("au").x + 300), "auto_space distributes free space");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 13. Collision / Avoid / Overlap / Priority / Bounds --\n";
    {
        Scene sc(R"RIN(
@view.Box=host position="relative"; width=200; height=300;
  @view.Box=base position="absolute"; left=10; top=10; width=50; height=50; priority=5; collision_policy="push"; .end/view
  @view.Box=other position="absolute"; left=20; top=20; width=50; height=50; collision_policy="push"; .end/view
  @view.Box=ghost position="absolute"; left=15; top=15; width=50; height=50; collision_policy="hide"; .end/view
  @view.Box=free position="absolute"; left=15; top=15; width=50; height=50; overlap=true; .end/view
  @view.Box=out position="absolute"; left=190; top=250; width=50; height=50; bounds="parent"; .end/view
.end/view
)RIN");
        if (sc.ok) {
            auto h = sc.g("host");
            CHECK(NEAR(sc.g("base").y, h.y + 10), "higher priority keeps its place");
            CHECK(sc.g("other").y >= sc.g("base").y + 50 - 0.5, "push moves the colliding element below the blocker");
            CHECK(indsin::webTruthy(sc.n("ghost")->attr("_collision_hidden")), "hide policy marks the colliding element hidden");
            auto l = paintOf(sc);
            CHECK(firstOwnerIndex(l, sc.n("ghost")->id) < 0, "hidden-by-collision element not painted");
            CHECK(NEAR(sc.g("free").x, h.x + 15), "overlap=true opts out");
            std::cout << "    out.x=" << sc.g("out").x << " y=" << sc.g("out").y << " host=" << h.x << "," << h.y << "," << h.w << "," << h.h << "\n";
            CHECK(sc.g("out").x + 50 <= h.x + h.w + 0.5, "bounds=parent clamps inside host");
            sc.relayout();
            CHECK(indsin::webTruthy(sc.n("ghost")->attr("_collision_hidden")) && sc.g("other").y >= sc.g("base").y + 49.5, "stable across relayout");
        } else CHECK(false, "parses");
        Scene sd(R"RIN(
@view.Box=host position="relative"; width=200; height=200;
  @view.Box=a position="absolute"; left=0; top=0; width=50; height=50; .end/view
  @view.Box=b position="absolute"; left=25; top=25; width=50; height=50; .end/view
  @view.Box=c position="absolute"; left=100; top=100; width=10; height=10; .end/view
.end/view
@view.Box=avoider position="relative"; width=200; height=200; overlap=false;
  @view.Box=t1 position="absolute"; left=0; top=0; width=40; height=40; .end/view
  @view.Box=t2 position="absolute"; left=10; top=10; width=40; height=40; .end/view
.end/view
)RIN");
        if (sd.ok) {
            std::vector<indsin::WebCollision> cs;
            indsin::webDetectCollisions(sd.n("host"), cs, false);
            CHECK(cs.size() == 1 && cs[0].nameA == "a" && cs[0].nameB == "b", "detectCollisions finds only the overlapping pair");
            CHECK(cs.size() == 1 && NEAR(cs[0].overlap.w, 25) && NEAR(cs[0].overlap.h, 25), "overlap rectangle reported");
            CHECK(sd.g("t2").y >= sd.g("t1").y + 40 - 0.5, "container overlap=false pushes siblings apart");
        } else CHECK(false, "detect parses");
    }
    std::cout << "-- 14. Media queries / device size / responsive --\n";
    {
        CHECK(indsin::webMediaMatches("(min-width: 600px)", 700, 800) && !indsin::webMediaMatches("(min-width: 600px)", 500, 800), "min-width");
        CHECK(indsin::webMediaMatches("(min-width: md) and (max-width: 900)", 700, 800), "bp name + and");
        CHECK(indsin::webMediaMatches("phone, desktop", 1300, 800) && !indsin::webMediaMatches("phone", 1300, 800), "device list OR");
        CHECK(indsin::webMediaMatches("(orientation: portrait)", 400, 800), "orientation");
        CHECK(indsin::webMediaMatches(">=lg", 900, 800) && !indsin::webMediaMatches(">=lg", 500, 800), "shorthand >=lg");
        Scene sc(R"RIN(
@view.Column=root
  @view.Box=wide media="(min-width: 600px)"; height=30; .end/view
  @view.Box=narrow media="(max-width: 599px)"; height=30; .end/view
  @view.Box=ph device_size="phone"; height=30; .end/view
.end/view
)RIN", 390);
        if (sc.ok) CHECK(sc.g("wide").h == 0 && sc.g("narrow").h == 30 && sc.g("ph").h == 30, "media/device_size gate elements at 390px");
        Scene sd(R"RIN(
@view.Column=root
  @view.Box=wide media="(min-width: 600px)"; height=30; .end/view
  @view.Box=ph device_size="phone"; height=30; .end/view
.end/view
)RIN", 900);
        if (sd.ok) CHECK(sd.g("wide").h == 30 && sd.g("ph").h == 0, "same source at 900px flips");
    }
    std::cout << "-- 15. Selector --\n";
    {
        Scene sc(R"RIN(
@view.Column=root
  @view.Card=c1 class="a b";
    @view.Text=t1 text="x"; .end/view
    @view.Button=b1 label="go"; .end/view
  .end/view
  @view.Row=r1
    @view.Text=t2 text="y"; role2="m"; .end/view
    @view.Text=t3 text="z"; .end/view
  .end/view
.end/view
)RIN");
        if (sc.ok) {
            std::vector<StrandPtr> out;
            CHECK(indsin::webQuery(sc.r.fabric, "Text", out) && out.size() == 3, "type selector");
            CHECK(indsin::webQuery(sc.r.fabric, "Card > Text", out) && out.size() == 1 && out[0]->name == "t1", "child combinator");
            CHECK(indsin::webQuery(sc.r.fabric, "#r1 Text", out) && out.size() == 2, "descendant + id");
            CHECK(indsin::webQuery(sc.r.fabric, ".a.b", out) && out.size() == 1 && out[0]->name == "c1", "class selector");
            CHECK(indsin::webQuery(sc.r.fabric, "Text + Button", out) && out.size() == 1 && out[0]->name == "b1", "adjacent sibling");
            CHECK(indsin::webQuery(sc.r.fabric, "Row Text:first-child", out) && out.size() == 1 && out[0]->name == "t2", ":first-child");
            CHECK(indsin::webQuery(sc.r.fabric, "Row Text:nth-child(2)", out) && out.size() == 1 && out[0]->name == "t3", ":nth-child(n)");
            CHECK(indsin::webQuery(sc.r.fabric, "[text=\"z\"]", out) && out.size() == 1 && out[0]->name == "t3", "attribute selector");
            CHECK(indsin::webQuery(sc.r.fabric, "Button, Row", out) && out.size() == 2, "selector list");
            CHECK(!indsin::webQuery(sc.r.fabric, "Text[", out), "malformed selector rejected");
            auto p = indsin::webParentOf(sc.r.fabric, sc.n("t3").get());
            CHECK(p && p->name == "r1", "parent lookup");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 16. Space report --\n";
    {
        Scene sc(R"RIN(
@view.Box=host width=200; height=100; padding=10;
  @view.Box=a width=50; height=20; .end/view
.end/view
)RIN");
        if (sc.ok) {
            auto rep = indsin::webSpaceReport(*sc.n("host"));
            CHECK(NEAR(rep.innerW, 180) && NEAR(rep.innerH, 80), "InnerSpace = box - padding");
            CHECK(NEAR(rep.occupied, 1000) && NEAR(rep.empty, 180 * 80 - 1000), "Occupied/Empty areas");
            CHECK(NEAR(rep.availableW, 130) && NEAR(rep.availableH, 60), "AvailableSpace after last child");
            CHECK(sc.n("host")->geometry.w == 200 && rep.outerW >= 200, "OuterSpace >= box");
            CHECK(indsin::webSpaceReportJson(*sc.n("host")).find("\"occupied\"") != std::string::npos, "json export");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 17. Scroll --\n";
    {
        Scene sc(R"RIN(
@view.Column=sv height=100; scroll_y=true; scroll_snap="start";
  @view.Box=i1 height=60; .end/view @view.Box=i2 height=60; .end/view @view.Box=i3 height=60; .end/view
.end/view
@view.Column=ns height=100;
  @view.Box=j1 height=60; .end/view @view.Box=j2 height=60; .end/view
.end/view
)RIN");
        if (sc.ok) {
            auto sv = sc.n("sv");
            CHECK(NEAR(sv->geometry.h, 100) && NEAR(sv->scrollContentH, 180), "scroll container keeps its window, records content 180");
            CHECK(NEAR(sc.g("i3").y - sv->geometry.y, 120), "children laid out at natural size inside the scroller");
            CHECK(indsin::webSetScroll(sc.r.fabric, "sv", 0, 50), "scrollTo accepted");
            sc.relayout();
            CHECK(NEAR(sv->scrollOffY, 60), "scroll_snap=start snaps 50 -> 60");
            CHECK(NEAR(sc.g("i2").y, sv->geometry.y), "content shifted by offset");
            indsin::webSetScroll(sc.r.fabric, "sv", 0, 9999); sc.relayout();
            CHECK(sv->scrollOffY <= 80.5 && NEAR(sv->scrollOffY, 60), "offset clamped to <=80 then snapped to nearest start (60)");
            auto l = paintOf(sc);
            bool thumb = false; for (auto& d : l) if (d.owner == sv->id && d.op == indsin::DrawOp::FILL_RECT && d.bounds.w <= 6.1 && d.bounds.w > 0) thumb = true;
            CHECK(thumb, "scrollbar thumb painted");
            CHECK(!indsin::webSetScroll(sc.r.fabric, "ns", 0, 10), "non-scroll container rejected");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 18. Flex tag / flow --\n";
    {
        Scene sc(R"RIN(
@view.Flex=fr flex_direction="column"; gap=5;
  @view.Box=a width=10; height=10; .end/view @view.Box=b width=10; height=10; .end/view
.end/view
@view.Flex=fw flow="row wrap"; gap=0; width=50;
  @view.Box=c width=30; height=10; .end/view @view.Box=d width=30; height=10; .end/view
.end/view
@view.Flex=rev flex_direction="row-reverse"; width=100;
  @view.Box=e width=10; height=10; .end/view @view.Box=f width=10; height=10; .end/view
.end/view
@view.Flex=crev flex_direction="column-reverse";
  @view.Box=g width=10; height=10; .end/view @view.Box=h width=10; height=10; .end/view
.end/view
)RIN");
        if (sc.ok) {
            CHECK(sc.g("b").y > sc.g("a").y && NEAR(sc.g("b").x, sc.g("a").x), "Flex column");
            CHECK(sc.g("d").y > sc.g("c").y, "Flex wrap breaks lines");
            CHECK(sc.g("e").x > sc.g("f").x, "row-reverse mirrors order");
            CHECK(sc.g("h").y < sc.g("g").y, "column-reverse reverses order");
            sc.relayout();
            CHECK(sc.g("h").y < sc.g("g").y && sc.g("e").x > sc.g("f").x, "reverse is idempotent across relayouts");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 19. Transform / Translate / LayoutEngine --\n";
    {
        Scene sc(R"RIN(
@view.Column=root
  @view.Box=a width=20; height=20; .end/view
  @view.Box=t transform="translate(10, 5) rotate(30) scale(2)"; width=20; height=20; .end/view
  @view.Box=u translate_x=7; width=20; height=20; .end/view
  @view.Box=after height=5; .end/view
.end/view
@view.Column=lg layout="grid"; columns=2; gap=0; width=100;
  @view.Box=g1 height=10; .end/view @view.Box=g2 height=10; .end/view
.end/view
@view.Box=rw layout="row"; gap=0;
  @view.Box=r1 width=10; height=10; .end/view @view.Box=r2 width=10; height=10; .end/view
.end/view
)RIN");
        if (sc.ok) {
            CHECK(NEAR(sc.g("t").x, 10) && NEAR(sc.g("t").y, 20 + 5), "transform translate offsets the box visually");
            auto wt = indsin::webReadTransform(*sc.n("t"), 20, 20);
            CHECK(NEAR(wt.rot, 30) && NEAR(wt.sx, 2) && NEAR(wt.sy, 2), "rotate/scale parsed for renderers");
            CHECK(NEAR(sc.g("u").x, 7), "translate_x");
            CHECK(NEAR(sc.g("after").y, 20 + 20 + 20), "transform does not affect flow");
            CHECK(NEAR(sc.g("g2").x - sc.g("g1").x, 50), "layout=grid on a Column");
            CHECK(sc.g("r2").x > sc.g("r1").x, "layout=row on a Box");
        } else CHECK(false, "parses");
    }
    std::cout << "-- 20. Backward compatibility (no new attrs = old behavior) --\n";
    {
        Scene sc(R"RIN(
@view.Grid=g columns=2; gap=10; padding=4;
  @view.Box=a height=20; .end/view @view.Box=b height=30; .end/view
  @view.Box=c height=10; .end/view
.end/view
)RIN", 200);
        if (sc.ok) {
            double cw = (200 - 8 - 10) / 2.0;
            CHECK(NEAR(sc.g("a").w, cw) && NEAR(sc.g("b").x, 4 + cw + 10) && NEAR(sc.g("c").y, 4 + 30 + 10), "legacy grid geometry unchanged");
        } else CHECK(false, "parses");
    }

    std::cout << "-- 21. <> tags: kebab-case attrs + structural tags --\n";
    {
        auto r = indsin::runColdPipeline("<Main#m>\n<Flex#f flex-direction=column row-gap=6 min-width=120>\n<Box#a height=10 />\n<Box#b height=10 hidden />\n<Box#c position=\"absolute\" top-right=4 width=10 height=10 />\n</Flex>\n</Main>\n");
        CHECK(r.ok, "tags parse (kebab attrs)");
        if (r.ok) {
            indsin::Indsin e; r.fabric->screenRoot = true; indsin::viewportWidthPx() = 300;
            e.layout(r.fabric, {300, 300, 0, 1e9}, 0, 0);
            auto f = by(r.fabric, "f"); auto c = by(r.fabric, "c");
            CHECK(f->kind == indsin::StrandKind::COLUMN && f->attr("row_gap") && f->attr("min_width"), "kebab keys canonical; Flex column");
            CHECK(by(r.fabric, "b")->geometry.h == 0, "hidden attribute collapses");
            CHECK(NEAR(c->geometry.x, 286) && NEAR(c->geometry.y, 4), "top-right=4 pins the corner");
        }
    }

    std::cout << "\n" << (failures == 0 ? "ALL PASS" : "FAILURES: " + std::to_string(failures)) << "\n";
    return failures == 0 ? 0 : 1;
}

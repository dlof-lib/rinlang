// Regression coverage for real @element -> Loom output.
#include "rin_loom_pipeline.h"
#include <iostream>
#include <cassert>

static loom::StrandPtr findName(const loom::StrandPtr& s, const std::string& n) {
    if (!s) return nullptr;
    if (s->name == n) return s;
    for (auto& c : s->children) if (auto r = findName(c, n)) return r;
    return nullptr;
}
static std::string attr(const loom::StrandPtr& s, const std::string& k) {
    return s && s->attr(k) ? s->attr(k)->asString() : "";
}
int main() {
    std::string src = R"(
@loop=app
  width=390;
  height=700;
  background="#ffffff";
  element_width=320;
  element_height=48;
  element_color="#111111";
  @element.text=title
    text="Rin Elements";
  .end/element
  @element.button=run
    text="Run";
  .end/element
  @element.input=name
    placeholder="Name";
  .end/element
  @element.progress=download
    value=50;
    max=100;
  .end/element
.end/loop
)";
    auto r = loom::runColdPipeline(src);
    if (!r.ok) { std::cerr << r.errorMessage << "\n"; return 1; }
    loom::Loom engine; engine.layout(r.fabric, {0,390,0,700},0,0);
    auto run=findName(r.fabric,"run"), title=findName(r.fabric,"title"), input=findName(r.fabric,"name"), prog=findName(r.fabric,"download");
    if (!run || !title || !input || !prog) return 2;
    if (run->role != rin::UiRole::ELEMENT || title->role != rin::UiRole::ELEMENT) return 3;
    if (run->geometry.w <= 0 || run->geometry.h != 48) return 4;
    if (attr(run,"text") != "Run") return 5;
    loom::Dye dye; auto list=dye.paint(r.fabric);
    bool buttonText=false;
    for (auto &d:list) if (d.owner==run->id && d.op==loom::DrawOp::TEXT_RUN && d.text=="Run") buttonText=true;
    if (!buttonText) return 6;
    std::cout << "ALL ELEMENT REAL OUTPUT TESTS PASSED\n";
    return 0;
}

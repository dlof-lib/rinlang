// tests/indsin_tags/tags_test.cpp — اختبارات مترجم الوسوم <> (rin_indsin_tags.h). header-only، بلا اعتماديات:
//   g++ -std=c++17 -I app/src/main/cpp tests/indsin_tags/tags_test.cpp -o /tmp/tags_test && /tmp/tags_test [corpus files…]
// الملفات الممرَّرة كوسائط تُفحَص لضمان التوافق العكسي: مصدر بلا وسوم يجب أن يخرج مطابقاً حرفياً.
#include "indsin/rin_indsin_tags.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

static int fails = 0, runs = 0;
static int lines(const std::string& s) { return (int)std::count(s.begin(), s.end(), '\n'); }

static void eq(const char* name, const std::string& src, const std::string& want) {
    ++runs;
    try {
        std::string got = indsin::tags::desugar(src);
        if (got != want) { ++fails; std::cerr << "FAIL " << name << "\n  want: " << want << "\n  got:  " << got << "\n"; }
        else if (lines(got) != lines(src)) { ++fails; std::cerr << "FAIL " << name << " (line count changed)\n"; }
    } catch (indsin::tags::TagError& e) { ++fails; std::cerr << "FAIL " << name << " threw: " << e.message << "\n"; }
}
static void err(const char* name, const std::string& src, int line, const char* part) {
    ++runs;
    try { indsin::tags::desugar(src); ++fails; std::cerr << "FAIL " << name << " (no error)\n"; }
    catch (indsin::tags::TagError& e) {
        if (e.line != line || e.message.find(part) == std::string::npos) {
            ++fails; std::cerr << "FAIL " << name << " got line " << e.line << ": " << e.message << "\n";
        }
    }
}

int main(int argc, char** argv) {
    eq("passthrough", "warp a = 1 < 2;\n@view.Text=t text=\"<b>\"; .end/view\n", "warp a = 1 < 2;\n@view.Text=t text=\"<b>\"; .end/view\n");
    eq("selfclose+id", "<Divider#d />", "@view.Divider=d .end/view");
    eq("open/close", "<Column#r gap=16>\n</Column>", "@view.Column=r gap=16;\n.end/view");
    eq("value kinds", "<Box#b w=12 x=120px c=#1E1E2E s=spacious t=true q='it\"s' e={a < b} />",
       "@view.Box=b w=12; x=120px; c=\"#1E1E2E\"; s=\"spacious\"; t=true; q=\"it\\\"s\"; e=a < b; .end/view");
    eq("bool shorthand", "<Banner dismissible />", "@view.Banner=banner_1 dismissible=true; .end/view");
    eq("text child", "<Text#t>Hi {name}!</Text>", "@view.Text=t text=\"Hi \" + (name) + \"!\"; .end/view");
    eq("text lone expr", "<Text#t>{n}</Text>", "@view.Text=t text=\"\" + (n); .end/view");
    eq("text entity", "<Text#t>a &lt; b</Text>", "@view.Text=t text=\"a < b\"; .end/view");
    eq("button label", "<Button#b onTap={n = n + 1}>Add</Button>", "@view.Button=b onTap=n = n + 1; label=\"Add\"; .end/view");
    eq("warp", "<warp a={0} />", "warp a = 0; ");
    eq("import", "<Import src=\"x.rin\" />", "@import \"x.rin\";");
    eq("loop", "<Loop#c></Loop>", "@loop=c .end/loop");
    eq("container/element", "<Container#A><Element.Button#ok /></Container>", "@container=A @element.Button=ok .end/element .end/container");
    eq("fragment", "<><Text#a/></>", "@view.Text=a .end/view");
    eq("comment", "<!-- a\nb -->\n", "// a\n//b \n");
    eq("brace in string", "<Text#t x={\"}\"} />", "@view.Text=t x=\"}\"; .end/view");
    eq("mixed legacy inside", "@view.Column=r\n  <Text#a>x</Text>\n.end/view", "@view.Column=r\n  @view.Text=a text=\"x\"; .end/view\n.end/view");
    eq("multiline attrs keep lines", "<Box#b\n  w=1\n  h=2\n/>\nnext", "@view.Box=b w=1; h=2; .end/view\n\n\n\nnext");
    err("mismatch", "<Column#a>\n</Row>", 2, "does not match");
    err("unclosed", "\n<Column#a>\n", 2, "unclosed");
    err("stray close", "</Column>", 1, "unexpected closing");
    err("unterminated brace", "<Box e={1 + />", 1, "unterminated");
    err("text twice", "<Text text=\"a\">b</Text>", 1, "both");
    err("warp not self-closed", "<warp a={1}>", 1, "self-closing");

    // ── مفاهيم متقدمة ──
    eq("fun", "<Fun#add params=\"a,b\">\n  return a + b;\n</Fun>", "fun add(a, b) {\n  return a + b;\n}");
    eq("rin raw", "<Rin>\nlet x = 1 < 2;\n</Rin>", "\nlet x = 1 < 2;\n");
    eq("repeat", "<Repeat times=2 as=k><Text#r>n{k}</Text></Repeat>",
       "@view.Text=r_0 text=\"n\" + (0); .end/view @view.Text=r_1 text=\"n\" + (1); .end/view ");
    eq("define+use", "<Define#P label=\"d\"><Text#t>{label}</Text></Define><P#a label=\"x\" />",
       "@view.Text=t_a text=\"\" + (\"x\"); .end/view ");
    eq("define default+slot", "<Define#B t=\"d\"><Card#c><Slot /></Card></Define>\n<B><Text#k>z</Text></B>",
       "\n@view.Card=c_1 @view.Text=k_1 text=\"z\"; .end/view .end/view ");
    eq("if/else", "<If cond={a}><Text#x>1</Text></If><Else><Text#y>2</Text></Else>",
       "@view.Text=x visible=(a); text=\"1\"; .end/view @view.Text=y visible=(!(a)); text=\"2\"; .end/view");
    eq("else-if chain", "<If cond={a}><Text#x/></If><Else cond={b}><Text#y/></Else><Else><Text#z/></Else>",
       "@view.Text=x visible=(a); .end/view @view.Text=y visible=(!(a) and (b)); .end/view @view.Text=z visible=(!((a) or (b))); .end/view");
    eq("if keeps own visible", "<If cond={a}><Text#x visible={b} /></If>", "@view.Text=x visible=((a)) and (b); .end/view");
    err("else alone", "<Else><Text/></Else>", 1, "must directly follow");
    err("unknown prop", "<Define#P a=1><Text/></Define>\n<P zzz=1 />", 2, "no property");
    err("recursive", "<Define#R><R /></Define><R />", 1, "too deep");
    err("define unnamed", "<Define><Text/></Define>", 1, "needs a name");
    err("slot outside", "<Slot />", 1, "only valid inside");

    for (int k = 1; k < argc; ++k) {
        std::ifstream f(argv[k]); std::stringstream ss; ss << f.rdbuf(); std::string src = ss.str();
        ++runs;
        try { if (indsin::tags::desugar(src) != src) { ++fails; std::cerr << "FAIL corpus (changed legacy file): " << argv[k] << "\n"; } }
        catch (indsin::tags::TagError& e) { ++fails; std::cerr << "FAIL corpus " << argv[k] << ":" << e.line << " " << e.message << "\n"; }
    }
    std::cout << (runs - fails) << "/" << runs << " passed\n";
    return fails ? 1 : 0;
}

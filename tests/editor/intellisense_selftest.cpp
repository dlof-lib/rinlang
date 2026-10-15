// اختبار ذاتي لمحرك IntelliSense (g++): sh tests/editor/run.sh
// يغطي: البحث الضبابي، السياقات (أعضاء/@/#/import/نص/تعليق/تصريح)، النطاقات، الأصناف، Set، enum،
// signature help، describe، الاستخدام (MRU)، ومتانة أمام ملفات حقيقية (لا انهيار + زمن معقول).
#include "editor/rin_editor_engine.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
using namespace rinedit;
static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) passes++; else { fails++; printf("FAIL [%d]: %s\n", __LINE__, msg); } } while (0)

// نص فيه علامة | = موضع المؤشر
struct Doc {
    EditorEngine e;
    explicit Doc(std::string text) {
        size_t p = text.find('|');
        int line = 0, col = 0;
        if (p != std::string::npos) {
            for (size_t i = 0; i < p; ++i) { if (text[i] == '\n') { line++; col = 0; } else col++; }
            text.erase(p, 1);
        }
        e.setText(text);
        e.setCursor(line, col, false);
    }
    intel::CompletionResult comp(bool ex = false) { return e.complete(ex, 80); }
};
static const intel::CompletionItem* find(const intel::CompletionResult& r, const std::string& label) {
    for (auto& i : r.items) if (i.label == label) return &i;
    return nullptr;
}
static int indexOf(const intel::CompletionResult& r, const std::string& label) {
    for (size_t i = 0; i < r.items.size(); ++i) if (r.items[i].label == label) return (int)i;
    return -1;
}

int main(int argc, char** argv) {
    // ---- بحث ضبابي
    {
        auto f = intel::fuzzyMatch("gsl", "getSomeLong");
        CHECK(f.ok && f.pos.size() == 3 && f.pos[0] == 0 && f.pos[1] == 3 && f.pos[2] == 7, "camelCase humps gsl→getSomeLong");
        CHECK(!intel::fuzzyMatch("xyz", "print").ok, "no match");
        CHECK(intel::fuzzyMatch("pri", "print").score > intel::fuzzyMatch("pri", "sprint").score, "prefix beats infix");
        CHECK(intel::fuzzyMatch("pri", "print").score > intel::fuzzyMatch("pnt", "print").score, "prefix beats subsequence");
        auto g = intel::fuzzyMatch("iN", "inputNumber");
        CHECK(g.ok && g.pos.size() == 2 && g.pos[0] == 0 && g.pos[1] == 5, "iN→inputNumber humps");
        CHECK(intel::fuzzyMatch("PRINT", "print").ok, "case-insensitive");
        CHECK(intel::fuzzyMatch("", "x").ok, "empty ok");
    }
    // ---- الأولوية والأساسيات
    {
        Doc d("pri|");
        auto r = d.comp();
        CHECK(!r.items.empty() && r.items[0].label == "print", "pri → print first");
        CHECK(r.startCol == 0 && r.endCol == 3, "replace range = word");
    }
    {
        Doc d("let alpha = 5;\nlet beta = \"x\";\nlet gamma = [1];\nal|");
        auto r = d.comp();
        auto* a = find(r, "alpha");
        CHECK(a && a->kind == intel::Kind::Variable && a->detail == "number", "local var alpha: number");
        CHECK(r.items[0].label == "alpha", "alpha ranks first");
        Doc d2("let alpha = 5;\nlet beta = \"x\";\nbe|");
        auto res_b = d2.comp();
        auto* b = find(res_b, "beta");
        CHECK(b && b->detail == "string", "beta: string");
        Doc d3("let g = [1];\nlet o = {a: 1};\nlet t = true;\nlet n = nil;\nlet f = fun(x) { return x; };\nlet z = len(g);\n|");
        auto r3 = d3.comp(true);
        CHECK(find(r3, "g") && find(r3, "g")->detail == "array", "array type");
        CHECK(find(r3, "o") && find(r3, "o")->detail == "object", "object type");
        CHECK(find(r3, "t") && find(r3, "t")->detail == "boolean", "bool type");
        CHECK(find(r3, "z") && find(r3, "z")->detail == "number", "ret type of len");
    }
    // ---- نطاقات: معاملات الدوال
    {
        Doc d("fun area(width, height) {\n    return wid|\n}\n");
        auto r = d.comp();
        auto* w = find(r, "width");
        CHECK(w && w->kind == intel::Kind::Parameter, "param visible in body");
        CHECK(r.items[0].label == "width", "param first");
        Doc d2("fun area(width, height) {\n    return 1;\n}\nwid|");
        auto res_w2 = d2.comp();
        auto* w2 = find(res_w2, "width");
        CHECK(!w2 || w2->kind == intel::Kind::Word, "param NOT visible outside (only as plain word)");
        Doc d3("fun area(width, height) {\n    let inner = 1;\n}\nlet outer = 2;\ninn|");
        auto res_i3 = d3.comp();
        auto* i3 = find(res_i3, "inner");
        CHECK(!i3 || i3->kind == intel::Kind::Word, "inner local not visible outside fn");
    }
    // ---- دوال المستخدم + hoisting
    {
        Doc d("/// يحسب المجموع\n// بين رقمين\nfun addNums(a, b) { return a + b; }\nlet x = addN|");
        auto res_f = d.comp();
        auto* f = find(res_f, "addNums");
        CHECK(f && f->kind == intel::Kind::Function && f->detail == "(a, b)", "user fun detail");
        if (f && f->doc != "يحسب المجموع\nبين رقمين") printf("  got doc: [%s]\n", f->doc.c_str());
        CHECK(f && f->doc == "يحسب المجموع\nبين رقمين", "doc comment captured");
        CHECK(f && f->insertText == "addNums()" && f->cursorBack == 1, "fun insert with parens, cursor inside");
        Doc d2("let v = lat|;\nfun later() { return 1; }\n");
        CHECK(find(d2.comp(), "later") != nullptr, "hoisted fun defined after cursor");
    }
    // ---- حلقات
    {
        Doc d("each item, idx in xs {\n    pr = ite|\n}\n");
        auto res_it = d.comp();
        auto* it = find(res_it, "item");
        CHECK(it && it->kind == intel::Kind::Variable, "each var visible inside body");
        Doc d2("each item, idx in xs {\n    pr = 1;\n}\nite|");
        auto res_it2 = d2.comp();
        auto* it2 = find(res_it2, "item");
        CHECK(!it2 || it2->kind == intel::Kind::Word, "each var not visible after loop");
        Doc d3("repeat 3 as turn {\n    tu|\n}\n");
        CHECK(find(d3.comp(), "turn") != nullptr, "repeat as var");
        Doc d4("let c = [each val in xs yield va|];\n");
        auto res_v4 = d4.comp();
        auto* v4 = find(res_v4, "val");
        CHECK(v4 && v4->kind == intel::Kind::Variable, "comprehension var visible in yield");
        Doc d5("for (let idx = 0; idx < 3; idx = idx + 1) {\n    id|\n}\nid2");
        CHECK(find(d5.comp(), "idx") != nullptr, "for-let visible in body");
        Doc d6("try { x(); } catch (err) {\n    er|\n}");
        CHECK(find(d6.comp(), "err") != nullptr, "catch var");
    }
    // ---- الأعضاء: مساحات الأسماء
    {
        Doc d("Set.|");
        auto r = d.comp();
        CHECK(r.trigger == intel::Trigger::Member, "member trigger");
        CHECK(find(r, "of") && find(r, "union") && find(r, "from"), "Set.* members");
        CHECK(!find(r, "print"), "no keywords after dot");
        CHECK(find(r, "union") && find(r, "union")->kind == intel::Kind::Method, "method kind");
        Doc d2("Env.ge|");
        auto r2 = d2.comp();
        CHECK(!r2.items.empty() && r2.items[0].label == "get", "Env.get first");
        Doc d3("container.art|");
        auto res_a = d3.comp();
        auto* a = find(res_a, "artifact");
        CHECK(a && a->kind == intel::Kind::Namespace, "nested namespace container.artifact");
        Doc d4("container.artifact.|");
        CHECK(find(d4.comp(), "info") != nullptr, "container.artifact.info");
    }
    // ---- Set مُنشأ: s.add(x)
    {
        Doc d("let s = Set([1, 2]);\ns.|");
        auto r = d.comp();
        auto* add = find(r, "add");
        CHECK(add && add->detail.find("item") != std::string::npos, "s.add shows (…) without receiver");
        CHECK(find(r, "union") != nullptr, "set instance methods");
        auto sh = Doc("let s = Set([1]);\ns.add(|").e.signatureHelp();
        CHECK(sh.valid && sh.name == "Set.add", "sig help on s.add");
    }
    // ---- أصناف
    {
        std::string src = "class Base {\n    fun hello() { return 1; }\n}\n"
                          "class P extends Base {\n    let x = 1;\n    static let count = 0;\n    fun init(a, b) { self.x = a; }\n    fun go(n) { return n; }\n    fun me() { self.@ }\n}\n";
        std::string withSelf = src; withSelf.replace(withSelf.find("@"), 1, "|");
        Doc d(withSelf);
        auto r = d.comp();
        CHECK(find(r, "x") && find(r, "go") && find(r, "hello"), "self. members incl. inherited");
        CHECK(find(r, "init") == nullptr, "constructor init hidden from member lists");
        std::string srcNoSelf = src; srcNoSelf.replace(srcNoSelf.find("self.@"), 6, "self.x");
        Doc d2(srcNoSelf + "let p = P(1, 2);\np.|");
        auto r2 = d2.comp();
        CHECK(find(r2, "x") && find(r2, "go") && find(r2, "hello"), "instance members via variable");
        CHECK(!find(r2, "count"), "static not on instance");
        Doc d3(srcNoSelf + "P.|");
        auto r3 = d3.comp();
        CHECK(find(r3, "count") != nullptr && !find(r3, "go"), "static members via class name");
        Doc d4(srcNoSelf + "class Q extends |");
        auto r4 = d4.comp(true);
        CHECK(find(r4, "Base") && find(r4, "P") && !find(r4, "print"), "class names only after extends");
        auto sh = Doc(srcNoSelf + "let p = P(|").e.signatureHelp();
        CHECK(sh.valid && sh.kind == 2 && sh.params.size() == 2 && sh.params[0] == "a", "constructor sig uses init params");
        auto sh2 = Doc(srcNoSelf + "let p = P(1,2);\np.go(|").e.signatureHelp();
        CHECK(sh2.valid && sh2.name == "P.go" && sh2.params.size() == 1, "method sig");
    }
    // ---- enum و object literal
    {
        Doc d("enum Status { Active, Paused, Done = 99 }\nlet s = Status.|");
        auto r = d.comp();
        CHECK(find(r, "Active") && find(r, "Paused") && find(r, "Done"), "enum members");
        CHECK(find(r, "Active")->kind == intel::Kind::EnumMember, "enum member kind");
        Doc d2("let cfg = {name: 1, \"port\": 2, nested: {a: 1}};\ncfg.|");
        auto r2 = d2.comp();
        CHECK(find(r2, "name") && find(r2, "port") && find(r2, "nested"), "object literal keys");
        CHECK(!find(r2, "a"), "nested keys not at top level");
    }
    // ---- @ و # و import
    {
        Doc d("@con|");
        auto r = d.comp();
        CHECK(r.trigger == intel::Trigger::At && find(r, "@container") && find(r, "@container.pipe"), "@container tags");
        CHECK(r.startCol == 0, "@ replace starts at @");
        Doc d2("@container.pi|");
        auto r2 = d2.comp();
        CHECK(!r2.items.empty() && r2.items[0].label == "@container.pipe", "@container.pipe first");
        Doc d3("@tmpl twice(xs) each x in xs;\n@tw|");
        CHECK(find(d3.comp(), "@twice") != nullptr, "template usage @name");
        Doc d4("#se|");
        auto r4 = d4.comp();
        CHECK(r4.trigger == intel::Trigger::Hash && find(r4, "#sed"), "#sed");
        Doc d5("@import \"lib/str|\";");
        auto r5 = d5.comp();
        CHECK(r5.trigger == intel::Trigger::ImportPath && find(r5, "lib/strings.og.rin"), "import path");
        CHECK(r5.startCol == 9 && r5.endCol == 16, "import replace range inside quotes");
        Doc d6("@el|");
        CHECK(find(d6.comp(), "@element.button") != nullptr, "@element.* tags");
    }
    // ---- سياقات صامتة
    {
        CHECK(Doc("let s = \"pri|\";").comp().items.empty(), "silent inside string");
        CHECK(Doc("// pri|").comp().items.empty(), "silent inside comment");
        CHECK(Doc("let |").comp(true).items.empty(), "silent when naming a let");
        CHECK(Doc("fun f(a, b|").comp(true).items.empty(), "silent in param list");
        CHECK(Doc("fun add|").comp(true).items.empty(), "silent when naming a fun");
        CHECK(Doc("let x = 12|").comp().items.empty(), "silent after number");
        CHECK(!Doc("let name = 1;\nlet x = `hello ${na|}`;").comp().items.empty(), "completion works inside template ${ }");
        CHECK(Doc("|").comp().items.empty(), "empty prefix not auto");
        CHECK(!Doc("|").comp(true).items.empty(), "explicit invoke lists");
        CHECK(Doc("let t = `abc|").comp().items.empty(), "silent in template text");
    }
    // ---- حالات سياقية
    {
        Doc d("outer: each x in xs {\n    break ou|\n}\n");
        auto r = d.comp();
        CHECK(find(r, "outer") && find(r, "outer")->kind == intel::Kind::Label, "labels after break");
        Doc d2("let v = tr|");
        auto r2 = d2.comp();
        CHECK(find(r2, "true") && find(r2, "true")->kind == intel::Kind::Constant, "true const in expr");
        Doc d3("let v = whi|");
        CHECK(!find(d3.comp(), "while"), "statement keywords hidden in expression");
        Doc d4("whi|");
        CHECK(find(d4.comp(), "while") != nullptr, "while at statement start");
        Doc d5("each|");
        auto r5 = Doc("ea|").comp();
        auto* e5 = find(r5, "each");
        CHECK(e5 && e5->snippet && e5->kind == intel::Kind::Snippet, "each is snippet at statement start");
        (void)d5;
    }
    // ---- دوال مدمجة
    {
        Doc d("let n = ab|");
        auto r = d.comp();
        auto* a = find(r, "abs");
        CHECK(a && a->kind == intel::Kind::Function && a->detail == "(number) → number" && a->cursorBack == 1, "abs builtin detail");
        CHECK(a && !a->doc.empty(), "abs doc");
        Doc d2("let n = random|");
        auto res_rd = d2.comp();
        auto* rd = find(res_rd, "random");
        // random مطابقة تامة؟ لا (ما كُتب كاملاً يُستثنى)
        CHECK(rd == nullptr, "exact match hidden");
        Doc d3("let n = rando|");
        auto res_rd3 = d3.comp();
        auto* rd3 = find(res_rd3, "random");
        CHECK(rd3 && rd3->insertText == "random()" && rd3->cursorBack == 0, "zero-arg fn inserts () with cursor after");
        Doc d5("let n = ab|(x);");
        auto res_a5 = d5.comp();
        auto* a5 = find(res_a5, "abs");
        CHECK(a5 && a5->insertText == "abs" && a5->cursorBack == 0, "no () added when ( follows");
    }
    // ---- signature help
    {
        auto sh = Doc("push(arr, |").e.signatureHelp();
        CHECK(sh.valid && sh.name == "push" && sh.active == 1 && sh.params.size() == 2, "push active=1");
        auto sh0 = Doc("push(|").e.signatureHelp();
        CHECK(sh0.valid && sh0.active == 0, "push active=0");
        auto sh2 = Doc("replace(a, b, c, |").e.signatureHelp();
        CHECK(sh2.valid && sh2.active == 3, "extra args → past last param");
        auto sh3 = Doc("len(push(x, |)").e.signatureHelp();
        CHECK(sh3.valid && sh3.name == "push" && sh3.active == 1, "nested call → inner");
        auto sh4 = Doc("len(push(x, y), |").e.signatureHelp();
        CHECK(sh4.valid && sh4.name == "len", "after inner call closes → outer");
        auto sh5 = Doc("fun add(a, b) { return a + b; }\nadd(1, |").e.signatureHelp();
        CHECK(sh5.valid && sh5.kind == 1 && sh5.active == 1 && sh5.params[1] == "b", "user fun sig");
        auto sh6 = Doc("push(\"a,b,c\", |").e.signatureHelp();
        CHECK(sh6.valid && sh6.active == 1, "commas inside strings ignored");
        auto sh7 = Doc("if (|").e.signatureHelp();
        CHECK(!sh7.valid, "no sig for control keywords");
        auto sh8 = Doc("fun add(a, |").e.signatureHelp();
        CHECK(!sh8.valid, "no sig inside declaration");
        auto sh9 = Doc("Env.get(|").e.signatureHelp();
        CHECK(sh9.valid && sh9.name == "Env.get", "namespace fn sig");
        auto sh10 = Doc("@tmpl twice(xs, n) each x in xs;\n@twice(a, |").e.signatureHelp();
        CHECK(sh10.valid && sh10.kind == 3 && sh10.active == 1, "template call sig");
        auto sh11 = Doc("let x = [1, 2, |").e.signatureHelp();
        CHECK(!sh11.valid, "no sig inside array literal");
        auto sh12 = Doc("clamp(x, 0, 10) + max(|").e.signatureHelp();
        CHECK(sh12.valid && sh12.name == "max", "second call on same line");
    }
    // ---- describe
    {
        Doc d("// يضيف رقمين\nfun add(a, b) { return a + b; }\nlet total = add(1, 2);\nprint len(\"x\");\n");
        auto s1 = d.e.describeAt(2, 14);
        CHECK(s1.valid && s1.kindName == "function" && s1.signature == "fun add(a, b)" && s1.doc == "يضيف رقمين" && s1.defLine == 1, "describe user fun");
        auto s2 = d.e.describeAt(3, 7);
        CHECK(s2.valid && s2.kindName == "builtin" && s2.signature.find("len(value)") == 0, "describe builtin");
        auto s3 = d.e.describeAt(2, 1);
        CHECK(s3.valid && s3.kindName == "keyword", "describe keyword");
        auto s4 = d.e.describeAt(2, 5);
        CHECK(s4.valid && s4.kindName == "variable" && s4.signature == "let total", "describe var (hover at decl)");
        auto s5 = d.e.describeAt(0, 3);
        CHECK(!s5.valid, "no describe inside comment");
    }
    // ---- MRU
    {
        Doc a("ac|");
        auto r0 = a.comp();
        int before = indexOf(r0, "acos");
        for (int i = 0; i < 6; ++i) EditorEngine::recordCompletionUse("acos");
        auto r1 = Doc("ac|").comp();
        CHECK(indexOf(r1, "acos") <= before, "MRU never lowers");
        CHECK(indexOf(r1, "acos") == 0, "MRU boosts acos to first");
    }
    // ---- قوالب الطباعة وسمات print (التحديث الأخير)
    {
        Doc d("print \"Title\" bol|");
        auto r = d.comp();
        auto* b = find(r, "bold");
        CHECK(b && b->kind == intel::Kind::Property && b->insertText == "bold=true", "print attr bold=true");
        CHECK(r.items[0].label == "bold", "bold ranks first among print attrs");
        Doc d2("print \"x\" col|");
        auto r2 = d2.comp();
        auto* c = find(r2, "color");
        CHECK(c && c->insertText == "color=\"\"" && c->cursorBack == 1, "color=\"\" with cursor inside quotes");
        Doc d3("let bold = 1;\nlet y = bol|");
        auto r3 = d3.comp();
        auto* v = find(r3, "bold");
        CHECK(v && v->kind == intel::Kind::Variable, "outside print: variable not attr");
        Doc d4("@tmpl h1(w = 30) st|");
        auto r4 = d4.comp();
        CHECK(!r4.items.empty() && r4.items[0].label == "style", "tmpl kind: style");
        Doc d5("@tmpl warn(msg) pr|");
        CHECK(find(Doc("@tmpl warn(msg) pr|").comp(), "print") != nullptr, "tmpl kind: print");
        Doc d6("@tmpl warn(msg) print msg level=\"warn\" bo|");
        auto r6 = d6.comp();
        CHECK(find(r6, "bold") != nullptr, "attrs inside @tmpl print header");
        Doc d7("@tmpl title(w = 30) style width=w align=\"center\";\nprint \"report\" @tit|");
        CHECK(find(d7.comp(), "@title") != nullptr, "style template usable after print value");
        auto sh = Doc("@tmpl title(w = 30) style width=w;\nprint \"r\" @title(|").e.signatureHelp();
        CHECK(sh.valid && sh.kind == 3 && sh.params.size() == 1 && sh.params[0] == "w", "style template sig (default param)");
        (void)d5;
    }
    // ---- متانة
    {
        const char* bad[] = {"let s = \"unterminated\nfun f(", "((((((", "}}}}}", "@", "#", ".", "a.b.c.", "`${", "each x in", "class", "fun (", "\"", "let ", "@tmpl", "// c"};
        for (const char* b : bad) {
            EditorEngine e; e.setText(b);
            for (int l = 0; l < e.lineCount(); ++l)
                for (int c = 0; c <= (int)e.getLine(l).size(); ++c) {
                    e.setCursor(l, c, false);
                    e.complete(true, 20); e.signatureHelp(); e.describeAt(l, c);
                }
        }
        CHECK(true, "robust on malformed input (no crash)");
    }
    // ---- ملفات حقيقية: لا انهيار + أداء
    {
        int files = 0, positions = 0;
        double worst = 0;
        for (int i = 1; i < argc; ++i) {
            std::ifstream in(argv[i]); if (!in) continue;
            std::stringstream ss; ss << in.rdbuf();
            EditorEngine e; e.setText(ss.str());
            ++files;
            int step = std::max(1, e.lineCount() / 25);
            for (int l = 0; l < e.lineCount(); l += step) {
                int len = (int)e.getLine(l).size();
                for (int c : {0, len / 2, len}) {
                    e.setCursor(l, c, false);
                    auto t0 = std::chrono::steady_clock::now();
                    e.complete(true, 60); e.signatureHelp();
                    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                    worst = std::max(worst, ms); ++positions;
                }
            }
        }
        printf("real files: %d, positions: %d, worst call: %.2f ms\n", files, positions, worst);
        CHECK(worst < 120.0, "completion latency < 120ms on real files (-O0)");
    }
    // ---- مستند كبير مصنَّع
    {
        std::string big;
        for (int i = 0; i < 4000; ++i) big += "fun f" + std::to_string(i) + "(a, b) {\n    let v" + std::to_string(i) + " = a + b;\n    return v" + std::to_string(i) + ";\n}\n";
        big += "f39|";
        Doc d(big);
        auto t0 = std::chrono::steady_clock::now();
        auto r = d.comp();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("16k-line doc: %.2f ms, %zu items\n", ms, r.items.size());
        CHECK(!r.items.empty(), "large doc completes");
        CHECK(ms < 400.0, "16k-line latency sane at -O0");
    }
    printf("intellisense: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

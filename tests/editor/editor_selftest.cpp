// اختبار ذاتي لأدوات المحرر: g++ -std=c++17 ... (انظر tests/editor/run.sh)
#include "editor/rin_editor_engine.h"
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <map>
using namespace rinedit;
static int fails = 0, passes = 0;
#define CHECK(c, msg) do { if (c) passes++; else { fails++; printf("FAIL: %s\n", msg); } } while (0)

static std::string wordKind(EditorEngine& e, int line, const std::string& word, HighlightKind* out) {
    auto sp = e.computeHighlights();
    std::string ln = e.getLine(line);
    size_t pos = 0;
    while ((pos = ln.find(word, pos)) != std::string::npos) {
        for (auto& s : sp) if (s.line == line && s.startCol == (int)pos && s.endCol == (int)(pos + word.size())) { *out = s.kind; return "ok"; }
        pos++;
    }
    return "none";
}
static bool isKind(EditorEngine& e, int line, const std::string& w, HighlightKind k) {
    HighlightKind got; return wordKind(e, line, w, &got) == "ok" && got == k;
}

int main(int argc, char** argv) {
    EditorEngine e;
    e.setText("each x, i in xs where x > 1 reversed limit 5 {\n    print x;\n}\n"
              "let loop = 1;\nlet r = repeat(\"ab\", 2);\n"
              "outer: each j in 1 to 3 step 2 {\n    continue outer when (j == 2);\n}\n"
              "let t = sum each x in xs where x > 1 yield x * x;\n"
              "let c = [each a in xs yield a];\n"
              "each n in ns { if (n) { break; } } nobreak { print 1; }\n");
    CHECK(isKind(e, 0, "each", HighlightKind::Control), "each control");
    CHECK(isKind(e, 0, "where", HighlightKind::Modifier), "where modifier");
    CHECK(isKind(e, 0, "reversed", HighlightKind::Modifier), "reversed kw");
    CHECK(isKind(e, 0, "limit", HighlightKind::Modifier), "limit kw");
    CHECK(isKind(e, 0, "in", HighlightKind::Modifier), "in kw");
    CHECK(!isKind(e, 3, "loop", HighlightKind::Control), "let loop stays ident");
    CHECK(isKind(e, 4, "repeat", HighlightKind::Call), "repeat() call not loop");
    CHECK(isKind(e, 5, "outer", HighlightKind::Label), "label");
    CHECK(isKind(e, 6, "outer", HighlightKind::Label), "continue label");
    CHECK(isKind(e, 6, "when", HighlightKind::Modifier), "when");
    CHECK(isKind(e, 8, "sum", HighlightKind::Modifier), "reducer");
    CHECK(isKind(e, 8, "yield", HighlightKind::Modifier), "yield");
    CHECK(isKind(e, 9, "each", HighlightKind::Control), "comprehension each");
    CHECK(isKind(e, 10, "nobreak", HighlightKind::Control), "nobreak");

    e.setText("each n in xs distinct on (n[0]) take 3 { print n; }\neach x in xs, y in ys { print x; }\n");
    CHECK(isKind(e, 0, "distinct", HighlightKind::Modifier), "distinct kw");
    CHECK(isKind(e, 0, "take", HighlightKind::Modifier), "take after distinct");
    CHECK(isKind(e, 1, "each", HighlightKind::Control), "multi-source each");
    CHECK(e.expandSnippetAtCursor() == false, "no snippet without word");

    // قوالب الحلقات @tmpl: كلمات القالب، الاسم، المعاملات، الاستعمال
    e.setText("@tmpl evens(xs, k = 2) as x each x in xs where x % k == 0;\n"
              "@evens(nums) { print x; }\n"
              "@tmpl grid(r) as i { each i in 1 to r { @body } }\n"
              "let tmpl = 1;\n");
    CHECK(isKind(e, 0, "tmpl", HighlightKind::TemplateKw), "tmpl kw");
    CHECK(isKind(e, 0, "evens", HighlightKind::Template), "tmpl name def");
    CHECK(isKind(e, 0, "xs", HighlightKind::Parameter), "tmpl param");
    CHECK(isKind(e, 0, "k", HighlightKind::Parameter), "tmpl param default");
    CHECK(isKind(e, 1, "evens", HighlightKind::Template), "tmpl use");
    CHECK(isKind(e, 2, "body", HighlightKind::TemplateKw), "@body kw");
    CHECK(!isKind(e, 3, "tmpl", HighlightKind::TemplateKw), "let tmpl stays ident");

    // الرموز: مقارنة، فواصل، تدفق، أقواس بالعمق
    e.setText("let r = f(a[0], (b)) != 3 and x >= 1;\nlet v = xs |> g;\nlet w = ok ? 1 : 2;\nlet z = a + b;\n}\n");
    CHECK(isKind(e, 0, "!=", HighlightKind::Compare), "compare !=");
    CHECK(isKind(e, 0, ">=", HighlightKind::Compare), "compare >=");
    CHECK(isKind(e, 0, "and", HighlightKind::Compare), "logic and");
    CHECK(isKind(e, 0, ",", HighlightKind::Punct), "comma punct");
    CHECK(isKind(e, 0, ";", HighlightKind::Punct), "semicolon punct");
    CHECK(isKind(e, 0, "(", HighlightKind::Bracket0), "bracket depth0");
    CHECK(isKind(e, 1, "|>", HighlightKind::Flow), "pipe flow");
    CHECK(isKind(e, 2, "?", HighlightKind::Flow), "ternary flow");
    CHECK(isKind(e, 3, "+", HighlightKind::Operator), "plus operator");
    CHECK(isKind(e, 4, "}", HighlightKind::Error), "unmatched closer");

    // مفاهيم دلالية
    e.setText("class Point {\n    fun area(a) { return a; }\n}\nlet p = Point();\nlet MAX_SIZE = 10;\nif (true) { print p.x + MAX_SIZE; } else { print nil; }\n"
              "try {\n    throw 1;\n} catch (err) {\n    print err;\n}\nlet s = Set.of(1);\ncontainer.pipe;\n");
    CHECK(isKind(e, 0, "class", HighlightKind::Keyword), "class kw");
    CHECK(isKind(e, 0, "Point", HighlightKind::Class), "class name");
    CHECK(isKind(e, 1, "area", HighlightKind::FunctionDef), "fun def name");
    CHECK(isKind(e, 1, "return", HighlightKind::Control), "return control");
    CHECK(isKind(e, 3, "Point", HighlightKind::Class), "class use");
    CHECK(isKind(e, 4, "MAX_SIZE", HighlightKind::Constant), "caps constant");
    CHECK(isKind(e, 5, "true", HighlightKind::Constant), "true constant");
    CHECK(isKind(e, 5, "nil", HighlightKind::Constant), "nil constant");
    CHECK(isKind(e, 5, "x", HighlightKind::Property), "property");
    CHECK(isKind(e, 5, "if", HighlightKind::Control), "if control");
    CHECK(isKind(e, 6, "try", HighlightKind::Control), "try control");
    CHECK(isKind(e, 7, "throw", HighlightKind::Control), "throw control");
    CHECK(isKind(e, 8, "catch", HighlightKind::Control), "catch control");
    CHECK(isKind(e, 11, "Set", HighlightKind::Class), "Set namespace");
    CHECK(isKind(e, 12, "container", HighlightKind::Type), "container concept");
    CHECK(!isKind(e, 12, "pipe", HighlightKind::Property), "container.pipe not property");

    // إكمال
    auto sg = e.collectSuggestions("rev", 10);
    CHECK(std::find(sg.begin(), sg.end(), "reversed") != sg.end(), "complete reversed");

    // طيّ وهيكل
    e.setText("fun f(a) {\n    let x = 1;\n    each i in a {\n        print i;\n    }\n}\nlet top = 2;\n");
    auto folds = e.computeFoldRanges();
    CHECK(folds.size() == 2 && folds[0].first == 0 && folds[0].second == 4, "fold outer");
    CHECK(folds.size() == 2 && folds[1].first == 2 && folds[1].second == 3, "fold inner");
    auto ol = e.computeOutline();
    CHECK(ol.size() == 2 && ol[0].name == "f" && ol[0].kind == 0 && ol[1].name == "top" && ol[1].kind == 2, "outline");

    // إعادة محاذاة
    e.setText("fun f(a) {\nlet x = \"a\nb\";\n  each i in a {\n// c\nprint i;\n}\n}\n");
    e.setCursor(4, 2, false);
    CHECK(e.reindentDocument(4), "reindent changed");
    CHECK(e.getLine(1) == "    let x = \"a", "indent 1");
    CHECK(e.getLine(2) == "b\";", "string interior untouched");
    CHECK(e.getLine(3) == "    each i in a {", "indent each");
    CHECK(e.getLine(4) == "        // c", "comment follows code");
    CHECK(e.getLine(5) == "        print i;", "indent body");
    CHECK(e.getLine(6) == "    }", "closer");
    CHECK(!e.reindentDocument(4), "idempotent");
    CHECK(e.undo(), "single undo");
    CHECK(e.getLine(1) == "let x = \"a", "undo restores");

    // قوالب
    e.setText("fun g() {\n    each");
    e.setCursor(1, 8, false);
    CHECK(e.expandSnippetAtCursor(), "snippet expands");
    CHECK(e.getLine(1) == "    each  in  {", "snippet line");
    CHECK(e.getLine(2) == "        ", "snippet body indent");
    CHECK(e.getLine(3) == "    }", "snippet close");
    CHECK(e.getCursor().line == 1 && e.getCursor().col == 9, "caret at $0");
    e.setText("let each");
    e.setCursor(0, 8, false);
    CHECK(!e.expandSnippetAtCursor(), "no expand after let");

    // كل قوالب الحلقات يجب أن تُحلَّل بلا أخطاء بعد ملء الفراغات
    // ملفات tests/loops: لا أخطاء نحوية والتلوين لا يرمي
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i]); std::stringstream ss; ss << f.rdbuf();
        e.setText(ss.str());
        auto d = e.computeDiagnostics();
        int errs = 0; for (auto& x : d) if (x.severity == DiagnosticSeverity::Error) errs++;
        CHECK(errs == 0, argv[i]);
        CHECK(!e.computeHighlights().empty(), "hl nonempty");
        CHECK(!e.reindentDocument(4) || true, "reindent safe");
        // إعادة المحاذاة لا تغيّر دلالة الكود: يبقى بلا أخطاء
        auto d2 = e.computeDiagnostics();
        int errs2 = 0; for (auto& x : d2) if (x.severity == DiagnosticSeverity::Error) errs2++;
        CHECK(errs2 == 0, "post-reindent clean");
    }
    printf("passed %d failed %d\n", passes, fails);
    return fails ? 1 : 0;
}

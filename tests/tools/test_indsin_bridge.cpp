// tests/tools/test_indsin_bridge.cpp — جسر Rin ⇄ indsin بالأقواس المزدوجة << >> (docs/indsin_bridge.md):
//   <<expr>> ربط تفاعلي · <<print …>> · <<run f(x)>> (التقاط ناتج الطباعة) · <<let/warp/import>> ·
//   <<if/else>> · <<each list as it, i>> فوق بيانات Rin · دوال Rin captureOutput/capture/indsin.run.
//
// Build & run (من app/src/main/cpp؛ نفس وصفة بقية اختبارات indsin، والمعيار C++23):
//   g++ -std=c++23 -I. -Iindsin ../../../../tests/tools/test_indsin_bridge.cpp rin_lexer.cpp rin_parser.cpp rin_make.cpp \
//       rin_container_sql.cpp rin_artifact.cpp rin_interpreter.cpp toolchain/rin_toolchain.cpp binfmt/*.cpp \
//       loader_ui/library_loader_ui.cpp rin_http.cpp diagnostics/*.cpp clc/*.cpp indsin/rin_indsin_c_api.cpp rin_c_api.cpp -lz -lpthread -o test_indsin_bridge
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "indsin/rin_indsin_pipeline.h"
#include "indsin/rin_indsin_needle.h"
#include "indsin/rin_indsin_rinbridge.h"
#include "indsin/rin_indsin_rinlib.h"
#include "indsin/rin_indsin_c_api.h"
#include <functional>
#include <iostream>
#include <memory>
#include <string>

static int checks = 0, failures = 0;
static void check(bool cond, const std::string& label) {
    checks++;
    if (!cond) { failures++; std::cerr << "FAIL: " << label << "\n"; }
    else std::cout << "ok:   " << label << "\n";
}

static indsin::StrandPtr findByName(const indsin::StrandPtr& s, const std::string& name) {
    if (!s) return nullptr;
    if (s->name == name) return s;
    for (auto& c : s->children) if (auto f = findByName(c, name)) return f;
    return nullptr;
}
static std::string attr(const indsin::StrandPtr& s, const std::string& key) {
    if (!s) return "<null>";
    for (auto& a : s->attrs) {
        if (a.key != key) continue;
        if (a.value.kind == indsin::Value::Kind::STRING) return a.value.str;
        double n = a.value.number;
        if (n == (long long)n) return std::to_string((long long)n);
        return std::to_string(n);
    }
    return "<none>";
}
static std::string textOf(const indsin::PipelineResult& r, const std::string& name) { return attr(findByName(r.fabric, name), "text"); }
static int countByPrefix(const indsin::StrandPtr& s, const std::string& prefix) {
    if (!s) return 0;
    int n = s->name.rfind(prefix, 0) == 0 ? 1 : 0;
    for (auto& c : s->children) n += countByPrefix(c, prefix);
    return n;
}
static void setWarp(indsin::PipelineResult& r, rin::Interpreter& interp, const std::string& name, const indsin::Value& v) {
    indsin::InterpreterExprHost host(interp, "");
    indsin::ScopedRinHost scope(&host);
    r.warp.set(name, v);
    indsin::Shuttle sh;
    std::unordered_map<indsin::StrandId, indsin::StrandPtr> idx;
    std::function<void(const indsin::StrandPtr&)> walk = [&](const indsin::StrandPtr& st) { idx[st->id] = st; for (auto& c : st->children) walk(c); };
    walk(r.fabric);
    sh.applyWarpChange(name, r.warp, r.subs, idx);
}
static double gnum(rin::Interpreter& in, const char* n) { rin::Value v; in.lookupGlobal(n, v); return v.number; }

int main() {
    // ───────── 1) دوال Rin: captureOutput / capture / indsin.run ─────────
    {
        rin::Interpreter in;
        indsin::registerIndsinRinLib(in);
        const char* src = R"RIN(
fun hello(who) { print "hello " + who; print "second line"; return 42; }
fun boom() { print "before"; let z = nope + 1; }
fun silent(x) { return x * 2; }
let a = captureOutput(hello, "Rin");
let b = captureOutput("hello", "Ali");
let m = capture("hello", "Zed");
let e = capture("boom");
let s = indsin.run("silent", 21);
print "visible";
)RIN";
        rin::Lexer lx(src); rin::Parser ps(lx.scanTokens()); auto prog = ps.parse();
        std::string out = in.run(prog);
        check(!in.hadError(), "التقاط الناتج: البرنامج يعمل بلا خطأ");
        rin::Value v;
        in.lookupGlobal("a", v); check(v.str == "hello Rin\nsecond line\n", "captureOutput(fn, args) يعيد ما طُبع");
        in.lookupGlobal("b", v); check(v.str == "hello Ali\nsecond line\n", "captureOutput(\"اسم\", args) بالاسم");
        in.lookupGlobal("m", v);
        check(v.type == rin::Value::Type::MAP, "capture يعيد map");
        if (v.type == rin::Value::Type::MAP) {
            std::string ok, output, result;
            for (auto& kv : *v.map) {
                if (kv.first.str == "ok") ok = kv.second.toDisplayString();
                if (kv.first.str == "output") output = kv.second.str;
                if (kv.first.str == "result") result = kv.second.toDisplayString();
            }
            check(ok == "true" && output == "hello Zed\nsecond line\n" && result == "42", "capture: ok/output/result");
        }
        in.lookupGlobal("e", v);
        bool sawErr = false, okFalse = false;
        if (v.type == rin::Value::Type::MAP) for (auto& kv : *v.map) {
            if (kv.first.str == "ok") okFalse = (kv.second.type == rin::Value::Type::BOOL && !kv.second.boolean);
            if (kv.first.str == "error") sawErr = !kv.second.str.empty();
        }
        check(okFalse && sawErr, "capture لا يرمي: الخطأ يعود في الحقول");
        in.lookupGlobal("s", v); check(v.str == "42", "indsin.run بلا طباعة يعيد القيمة المُعادة");
        check(out.find("hello") == std::string::npos && out.find("before") == std::string::npos, "ما التُقط لا يتسرّب إلى ناتج البرنامج");
        check(out.find("visible") != std::string::npos, "ما طُبع خارج الالتقاط يبقى في الناتج");
    }

    // ───────── 2) ربط/طباعة/استدعاء + إعادة الحساب ─────────
    {
        const char* src = R"RIN(
<warp count={5} />
<Rin>
let user = {"name": "ليلى", "age": 30};
let calls = 0;
fun report(n) { calls = calls + 1; print "total=" + n; print "ok"; }
</Rin>
<Column#root>
  <Text#a>Hi <<user.name>>!</Text>
  <Text#b text="Age: <<user["age"]>> yrs" />
  <Text#c><<print "n", count + 1, true>></Text>
  <Text#d><<run report(count)>></Text>
  <Button#e label=<<"Count " + count>> />
  <<count * 2>>
  <<print "x", count>>
</Column>
)RIN";
        rin::Interpreter in;
        auto r = indsin::runColdPipelineWithRuntime(src, in);
        check(r.ok, std::string("جسر: الـ pipeline ينجح") + (r.ok ? "" : (": " + r.errorMessage + " @" + std::to_string(r.errorLine))));
        if (!r.ok) { std::cerr << failures << " failure(s)\n"; return 1; }
        check(textOf(r, "a") == "Hi ليلى!", "<<user.name>> داخل نص: " + textOf(r, "a"));
        check(textOf(r, "b") == "Age: 30 yrs", "استيفاء داخل \"…<<x>>…\" مع علامات اقتباس داخلية: " + textOf(r, "b"));
        check(textOf(r, "c") == "n 6 true", "<<print a, b, c>>: " + textOf(r, "c"));
        check(textOf(r, "d") == "total=5\nok", "<<run report(count)>> يعرض ناتج الطباعة: " + textOf(r, "d"));
        check(attr(findByName(r.fabric, "e"), "label") == "Count 5", "سمة=<<expr>>");
        check(textOf(r, "text_1") == "10", "<<expr>> وحدها = عنصر Text: " + textOf(r, "text_1"));
        check(textOf(r, "text_2") == "x 5", "<<print …>> وحدها = عنصر Text");
        check(gnum(in, "calls") <= 1.0, "indsin.run مُخزَّنة: الدالة لم تُنفَّذ أكثر من مرة");

        setWarp(r, in, "count", indsin::Value::num(9));
        check(textOf(r, "a") == "Hi ليلى!", "تغيّر warp: ما لا يعتمد عليه لا يتغيّر");
        check(textOf(r, "c") == "n 10 true", "تغيّر warp: <<print>> أُعيد حسابه: " + textOf(r, "c"));
        check(textOf(r, "d") == "total=9\nok", "تغيّر warp: <<run>> أُعيد تنفيذه بالوسيط الجديد: " + textOf(r, "d"));
        check(gnum(in, "calls") == 2.0, "التنفيذ الثاني تمّ مرة واحدة فقط (calls=2)");
        setWarp(r, in, "count", indsin::Value::num(5));
        check(textOf(r, "d") == "total=5\nok" && gnum(in, "calls") == 2.0, "عودة الوسيط القديم: من الذاكرة بلا إعادة تنفيذ");
        check(attr(findByName(r.fabric, "e"), "label") == "Count 5", "تغيّر warp: label أُعيد حسابه");
    }

    // ───────── 3) <<let>> / <<warp>> / <<if>> / <<else>> ─────────
    {
        const char* src = R"RIN(
<<let base = 10>>
<<warp n = 0>>
<Column#root>
  <<let doubled = base * 2>>
  <<if n > 1>>
    <Text#big>big</Text>
  <<else if n == 1>>
    <Text#one>one</Text>
  <<else>>
    <Text#none>none</Text>
  <</if>>
  <Text#dbl><<doubled>></Text>
</Column>
)RIN";
        rin::Interpreter in;
        auto r = indsin::runColdPipelineWithRuntime(src, in);
        check(r.ok, std::string("let/warp/if: ينجح") + (r.ok ? "" : (": " + r.errorMessage + " @" + std::to_string(r.errorLine))));
        if (r.ok) {
            auto vis = [&](const char* nm) { return attr(findByName(r.fabric, nm), "visible"); };
            check(textOf(r, "dbl") == "20", "<<let>> داخل عنصر (يُرفع للنهاية) يعمل: " + textOf(r, "dbl"));
            check(vis("none") != "false" && vis("big") == "false" && vis("one") == "false", "n=0: يظهر else فقط");
            setWarp(r, in, "n", indsin::Value::num(1));
            check(vis("one") != "false" && vis("none") == "false" && vis("big") == "false", "n=1: يظهر else-if فقط");
            setWarp(r, in, "n", indsin::Value::num(5));
            check(vis("big") != "false" && vis("one") == "false" && vis("none") == "false", "n=5: يظهر if فقط");
        }
    }

    // ───────── 4) <<each>> فوق بيانات Rin (تمريرتان) ─────────
    {
        const char* src = R"RIN(
<Rin>
let people = [{"name": "Ali", "age": 30}, {"name": "Sara", "age": 25}, {"name": "Omar", "age": 41}];
fun select(k) { return upper(people[k]["name"]); }
</Rin>
<Column#list>
  <<each people as p, i>>
    <Card#row padding=8>
      <Text#nm><<i + 1>>. <<p["name"]>> (<<p.age>>)</Text>
      <Button#pick onTap={select(i)}>Pick</Button>
    </Card>
  <</each>>
  <Text#total>Total: <<len(people)>></Text>
</Column>
)RIN";
        rin::Interpreter in;
        auto r = indsin::runColdPipelineWithRuntime(src, in);
        check(r.ok, std::string("each: ينجح") + (r.ok ? "" : (": " + r.errorMessage + " @" + std::to_string(r.errorLine))));
        if (r.ok) {
            check(countByPrefix(r.fabric, "row_") == 3, "3 بطاقات من 3 عناصر مصفوفة Rin");
            check(textOf(r, "nm_0") == "1. Ali (30)", "العنصر الأول: " + textOf(r, "nm_0"));
            check(textOf(r, "nm_1") == "2. Sara (25)", "العنصر الثاني: " + textOf(r, "nm_1"));
            check(textOf(r, "nm_2") == "3. Omar (41)", "العنصر الثالث: " + textOf(r, "nm_2"));
            check(textOf(r, "total") == "Total: 3", "ما بعد each يعمل");
            check(attr(findByName(r.fabric, "pick_1"), "onTap") == "select(1)", "فهرس العنصر يمرَّر لمعالج الحدث: " + attr(findByName(r.fabric, "pick_1"), "onTap"));
        }
    }
    {   // قائمة فارغة + رقم + قائمة حرفية + عنصر Text وحده + each داخل Define
        const char* src = R"RIN(
<Rin> let none = []; let n = 2; </Rin>
<Define#Tag t="">
  <Text#tt>#<<t>></Text>
</Define>
<Column#list>
  <<each none as z>>
    <Text#never>never</Text>
  <</each>>
  <<each n as k>>
    <Text#num>k=<<k>></Text>
  <</each>>
  <<each ["x", "y", "z"] as w, j>>
    <<w>>
  <</each>>
  <<each ["p", "q"] as w>>
    <Tag t=<<w>> />
  <</each>>
</Column>
)RIN";
        rin::Interpreter in;
        auto r = indsin::runColdPipelineWithRuntime(src, in);
        check(r.ok, std::string("each (فارغة/رقم/حرفية/Define): ينجح") + (r.ok ? "" : (": " + r.errorMessage + " @" + std::to_string(r.errorLine))));
        if (r.ok) {
            check(findByName(r.fabric, "never_0") == nullptr, "قائمة فارغة: لا عناصر");
            check(textOf(r, "num_0") == "k=0" && textOf(r, "num_1") == "k=1", "each على رقم: [" + textOf(r, "num_0") + "] [" + textOf(r, "num_1") + "]");
            check(countByPrefix(r.fabric, "text_") == 3, "each على قائمة حرفية مع <<w>> كعنصر Text");
            check(textOf(r, "tt_Tag_1") != "<none>" || countByPrefix(r.fabric, "tt") == 2, "each يستدعي قالب Define لكل عنصر");
        }
    }

    // ───────── 5) الأخطاء ─────────
    {
        auto lineOf = [](const std::string& src, std::string& msg) {
            rin::Interpreter in; auto r = indsin::runColdPipelineWithRuntime(src, in); msg = r.errorMessage; return r.ok ? -1 : r.errorLine; };
        std::string m;
        int L;
        auto chk = [&](const std::string& src, int wantLine, const char* part, const std::string& label) {
            L = lineOf(src, m);
            check(L == wantLine && m.find(part) != std::string::npos, label + " — [" + std::to_string(L) + "] " + m);
        };
        chk("<Column#c>\n<<if a>>\n<Text>x</Text>\n</Column>", 2, "<</if>>", "<<if>> بلا <</if>>: السطر الصحيح");
        chk("<Column#c>\n<Text>x</Text>\n<<else>>\n</Column>", 3, "without a matching <<if>>", "<<else>> يتيمة");
        chk("<Column#c>\n<Text>a <<x</Text>\n</Column>", 2, "unterminated", "<<x بلا >>");
        chk("<Rin> let xs = [1]; </Rin>\n<Column#c>\n<<each xs as a>>\n<<each xs as b>>\n<</each>>\n<</each>>\n</Column>", 4, "nested", "each متداخلة مرفوضة بوضوح");
        chk("<Column#c>\n<Text><<let q = 1>></Text>\n</Column>", 2, "statement", "<<let>> داخل نص مرفوضة");
        chk("<Rin> let t = \"abc\"; </Rin>\n<Column#c>\n<<each t as a>>\n<</each>>\n</Column>", 1, "array or a number", "each على نص: رسالة مفيدة");
        // desugar المجرّد (بلا مفسّر) يرفض each غير الحرفية برسالة واضحة
        bool threw = false;
        try { indsin::tags::desugar("<<each users as u>>\n<</each>>"); } catch (indsin::tags::TagError& e) { threw = e.message.find("Rin runtime") != std::string::npos; }
        check(threw, "tags::desugar بلا مفسّر: each على متغيّر يطلب مفسّراً برسالة واضحة");
    }

    // ───────── 6) الجلسة الحيّة (واجهة C: أندرويد/المعاينة): each + تحميل ساخن ─────────
    {
        auto mk = [](int n) {
            std::string xs = "[";
            for (int k = 0; k < n; ++k) xs += (k ? ", " : "") + std::string("\"user") + std::to_string(k) + "\"";
            xs += "]";
            return "<Rin> let users = " + xs + "; </Rin>\n<Column#c>\n<<each users as u, i>>\n<Text#row><<i>>:<<u>></Text>\n<</each>>\n</Column>\n";
        };
        void* sess = rin_indsin_session_create(mk(2).c_str(), 390);
        char* js = rin_indsin_session_render_json(sess);
        std::string j1 = js ? js : ""; if (js) rin_free_string(js);
        check(j1.find("0:user0") != std::string::npos && j1.find("1:user1") != std::string::npos && j1.find("2:user2") == std::string::npos, "الجلسة الحيّة: each يرسم عنصرين");
        char* up = rin_indsin_session_update_source(sess, mk(4).c_str());
        if (up) rin_free_string(up);
        js = rin_indsin_session_render_json(sess);
        std::string j2 = js ? js : ""; if (js) rin_free_string(js);
        check(j2.find("3:user3") != std::string::npos, "تحميل ساخن: القائمة صارت 4 عناصر");
        rin_indsin_session_free(sess);
    }

    // ───────── 7) بلا <<>> لا شيء يتغيّر ─────────
    {
        const char* src = "@view.Text=t text=\"a << b\"; .end/view\nlet x = 1 << 2;\n";
        check(indsin::tags::desugar(src) == src, "كود Rin فيه << (إزاحة/نص) يمرّ حرفياً");
    }

    std::cout << "\n" << (checks - failures) << "/" << checks << " passed\n";
    return failures ? 1 : 0;
}

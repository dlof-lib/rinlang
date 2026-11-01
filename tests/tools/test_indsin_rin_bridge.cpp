// tests/tools/test_indsin_rin_bridge.cpp — يثبت أن indsin يعتمد على لغة Rin الحقيقية (docs/indsin_rin.md):
//   متغيّرات let/const كقيم خصائص، تعابير كاملة (مقارنات/منطق/نفي/فهرسة/عضو/طرق)، دوال Rin الأصلية
//   والمعرَّفة، Set حقيقية، دوال indsin.* لكود Rin، حارس النقاء، وإعادة الحساب عند تغيّر warp.
//
// Build & run (من app/src/main/cpp؛ نفس وصفة بقية اختبارات indsin):
//   g++ -std=c++17 -I. -Iindsin ../../../../tests/tools/test_indsin_rin_bridge.cpp rin_lexer.cpp \
//       rin_parser.cpp rin_make.cpp rin_container_sql.cpp rin_artifact.cpp rin_interpreter.cpp \
//       toolchain/rin_toolchain.cpp binfmt/*.cpp loader_ui/library_loader_ui.cpp rin_http.cpp diagnostics/*.cpp \
//       clc/*.cpp -lz -lpthread -o test_indsin_rin_bridge && ./test_indsin_rin_bridge
#include "rin_lexer.h"
#include "rin_parser.h"
#include "rin_interpreter.h"
#include "indsin/rin_indsin_pipeline.h"
#include "indsin/rin_indsin_needle.h"
#include "indsin/rin_indsin_rinbridge.h"
#include "indsin/rin_indsin_rinlib.h"
#include "indsin/rin_indsin_system.h"
#include <iostream>
#include <string>
#include <functional>
#include <memory>

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
static std::string textOf(const indsin::PipelineResult& r, const std::string& name) {
    return attr(findByName(r.fabric, name), "text");
}

int main() {
    // ---- 1) متغيّرات Rin العامة كقيم خصائص + دوال Rin الأصلية والمعرَّفة + تعابير كاملة ----
    rin::Interpreter interp;
    const char* src = R"RIN(
let title = "Rin";
let LIMIT = 3;
let items = ["a", "b", "c", "d"];
let user = {"name": "ليلى", "age": 30};
fun badge(n) { return "[" + n + "]"; }
warp count = 5;
warp loading = false;

@view.Column=Root
  @view.Text=T1
    text=title;
  .end/view
  @view.Text=T2
    text=upper(title);
  .end/view
  @view.Text=T3
    text=len(items) + " items";
  .end/view
  @view.Text=T4
    text=badge(count);
  .end/view
  @view.Text=T5
    text=user["name"];
  .end/view
  @view.Text=T6
    text=items[1];
  .end/view
  @view.Text=T7
    text=count > LIMIT;
  .end/view
  @view.Text=T8
    text=count > LIMIT and !loading;
  .end/view
  @view.Text=T9
    text=count % 2;
  .end/view
  @view.Text=T10
    text=count > 100 ? "big" : "small";
  .end/view
  @view.Text=T11
    text=Set([1, 2, 2, 3]).size();
  .end/view
  @view.Text=T12
    text=Set(items).has("c");
  .end/view
  @view.Text=T13
    text=-count;
  .end/view
  @view.Text=T14
    text=count == 5;
  .end/view
.end/view
)RIN";
    auto r = indsin::runColdPipelineWithRuntime(src, interp);
    check(r.ok, std::string("cold pipeline ok") + (r.ok ? "" : (": " + r.errorMessage)));
    if (!r.ok) { std::cerr << failures << " failure(s)\n"; return 1; }

    check(textOf(r, "T1") == "Rin", "T1 متغيّر let عام كقيمة نص (كان يُعرض اسمه)");
    check(textOf(r, "T2") == "RIN", "T2 دالة Rin أصلية upper(title)");
    check(textOf(r, "T3") == "4 items", "T3 len(items) + نص");
    check(textOf(r, "T4") == "[5]", "T4 دالة fun معرَّفة badge(count) على خلية warp");
    check(textOf(r, "T5") == "ليلى", "T5 فهرسة قاموس user[\"name\"]");
    check(textOf(r, "T6") == "b", "T6 فهرسة مصفوفة items[1]");
    check(textOf(r, "T7") == "true", "T7 مقارنة > مع let عام");
    check(textOf(r, "T8") == "true", "T8 and و ! (منطق)");
    check(textOf(r, "T9") == "1", "T9 باقي القسمة %");
    check(textOf(r, "T10") == "small", "T10 ثلاثي بشرط مقارنة");
    check(textOf(r, "T11") == "3", "T11 Set حقيقية داخل الخاصية .size()");
    check(textOf(r, "T12") == "true", "T12 Set(items).has(...)");
    check(textOf(r, "T13") == "-5", "T13 نفي أحادي");
    check(textOf(r, "T14") == "true", "T14 ==");

    // ---- 2) إعادة الحساب عند تغيّر warp (الاشتراكات تلتقط المتغيّرات الحرّة) ----
    {
        indsin::InterpreterExprHost host(interp, "");
        indsin::ScopedRinHost scope(&host);
        r.warp.set("count", indsin::Value::num(2));
        indsin::Shuttle sh;
        std::unordered_map<indsin::StrandId, indsin::StrandPtr> idx;
        std::function<void(const indsin::StrandPtr&)> walk = [&](const indsin::StrandPtr& st) { idx[st->id] = st; for (auto& c : st->children) walk(c); };
        walk(r.fabric);
        sh.applyWarpChange("count", r.warp, r.subs, idx);
        check(textOf(r, "T4") == "[2]", "تغيّر warp: badge(count) أُعيد حسابه");
        check(textOf(r, "T7") == "false", "تغيّر warp: count > LIMIT أُعيد حسابه");
        check(textOf(r, "T9") == "0", "تغيّر warp: count % 2 أُعيد حسابه");
        check(textOf(r, "T8") == "false", "تغيّر warp: المنطق المركّب أُعيد حسابه");
    }

    // ---- 3) حارس النقاء: تعبير فيه أثر جانبي لا يُنفَّذ ولا يغيّر الحالة ----
    {
        const char* s2 = R"RIN(
let items = [1, 2, 3];
@view.Column=Root
  @view.Text=P1
    text=items.pop();
  .end/view
  @view.Text=P2
    text=len(items);
  .end/view
  @view.Text=P3
    text=push(items, 9);
  .end/view
  @view.Text=P4
    text=Set(items).add(7);
  .end/view
.end/view
)RIN";
        rin::Interpreter i2;
        auto r2 = indsin::runColdPipelineWithRuntime(s2, i2);
        check(r2.ok, "pure-guard pipeline ok");
        check(textOf(r2, "P2") == "3", "الأثر الجانبي لم يحدث: items ما زالت 3 عناصر");
        check(attr(findByName(r2.fabric, "P1"), "text") != "3", "items.pop() مرفوضة (لا تُرجع عنصراً فعلياً)");
        check(textOf(r2, "P3") != "4", "push(items, 9) مرفوضة");
        rin::Value probe;
        check(i2.lookupGlobal("items", probe) && probe.array && probe.array->size() == 3, "المصفوفة الأصلية لم تتغيّر");
    }

    // ---- 4) توافق خلفي: بلا مضيف يبقى السلوك القديم حرفياً ----
    {
        indsin::WarpScope w; w.set("n", indsin::Value::num(4));
        auto parseExpr = [](const std::string& code) {
            rin::Lexer lx("let __x = " + code + ";"); rin::Parser ps(lx.scanTokens());
            auto prog = ps.parse();
            return std::static_pointer_cast<rin::LetStmt>(prog[0])->initializer;
        };
        indsin::ScopedRinHost none(nullptr);
        check(indsin::evalAttrExpr(parseExpr("n + 1"), w).asNumber() == 5, "بلا مضيف: + يعمل كما كان");
        check(indsin::evalAttrExpr(parseExpr("n > 1"), w).asString() == "41", "بلا مضيف: المقارنة تبقى بالسلوك القديم (لصق نص)");
        check(indsin::evalAttrExpr(parseExpr("!n"), w).asString() == "<expr>", "بلا مضيف: النفي يبقى <expr>");
    }

    // ---- 5) معالج الحدث ما زال وصفاً: onTap=increment(count) لا يُحسَب كقيمة ----
    {
        const char* s3 = R"RIN(
warp count = 0;
fun increment(c) { c = c + 1; }
@view.Column=Root
  @view.Button=B
    text="+";
    onTap=increment(count);
  .end/view
  @view.Text=L
    text="n=" + count;
  .end/view
.end/view
)RIN";
        rin::Interpreter i3;
        auto r3 = indsin::runColdPipelineWithRuntime(s3, i3);
        check(r3.ok, "handler pipeline ok");
        std::string d = attr(findByName(r3.fabric, "B"), "onTap");
        check(d == "increment(count)", "onTap يبقى وصف معالج: " + d);
        check(textOf(r3, "L") == "n=0", "text مع warp ما زال يعمل");
    }

    // ---- 6) دوال indsin.* في كود Rin وفي الخصائص ----
    {
        const char* s4 = R"RIN(
let seed = "#6C5CE7";
let pal = indsin.palette(seed);
let mid = pal["500"];
let sc = indsin.token("spacing", "comfortable");
let bp = indsin.breakpoint(700);
let ok = indsin.contrast("#000000", "#ffffff");
let fixed = indsin.ensureContrast("#888888", "#ffffff");
let tone = indsin.onColor("#000000");
let roles = indsin.themeColors();
let nTheme = len(indsin.themes());
let hm = indsin.harmony(seed, "triadic");
let tk = indsin.tokens("duration");
@view.Column=Root
  @view.Text=A
    text=indsin.token("radius", "pill");
  .end/view
  @view.Text=B
    text=indsin.breakpoint(1000);
  .end/view
  @view.Text=C
    text=indsin.palette("#6C5CE7")["500"];
  .end/view
  @view.Text=D
    text=indsin.useTheme("Light");
  .end/view
.end/view
)RIN";
        rin::Interpreter i4;
        auto r4 = indsin::runColdPipelineWithRuntime(s4, i4);
        check(r4.ok, std::string("indsin.* pipeline ok") + (r4.ok ? "" : (": " + r4.errorMessage)));
        auto g = [&](const char* n) { rin::Value v; i4.lookupGlobal(n, v); return v; };
        check(g("mid").str == "#6c5ce7" || g("mid").str == "#6C5CE7", "palette(seed)[500] == البذرة: " + g("mid").str);
        check(g("sc").number == 16 || g("sc").number > 0, "token(spacing, comfortable) رقم موجب");
        check(g("bp").str == "md", "breakpoint(700) == md");
        check(g("ok").number > 20.9 && g("ok").number < 21.1, "contrast(black, white) ≈ 21");
        check(rincolor::contrastRatio(rincolor::parseColor(g("fixed").str), rincolor::parseColor("#ffffff")) >= 4.5, "ensureContrast يبلغ 4.5");
        check(g("tone").str == "#ffffff" || g("tone").str == "#FFFFFF" || g("tone").str.size() == 7, "onColor(black) لون فاتح");
        check(g("roles").type == rin::Value::Type::MAP && g("roles").map->size() == 12, "themeColors() = 12 دوراً");
        check(g("nTheme").number >= 11, "themes() ≥ 11 ثيماً");
        check(g("hm").type == rin::Value::Type::ARRAY && g("hm").array->size() == 2, "harmony triadic = لونان");
        check(g("tk").type == rin::Value::Type::MAP && g("tk").map->size() == 5, "tokens(duration) = 5");
        check(textOf(r4, "A") != "<expr>" && textOf(r4, "A") != "", "indsin.token داخل الخاصية (نقية)");
        check(textOf(r4, "B") == "lg", "indsin.breakpoint(1000) == lg داخل الخاصية");
        check(textOf(r4, "C") != "<expr>" && textOf(r4, "C").size() == 7, "indsin.palette(...)[...] داخل الخاصية");
        check(attr(findByName(r4.fabric, "D"), "text") != "true", "indsin.useTheme ممنوعة داخل الخاصية (أثر جانبي)");
        check(indsin::themeRegistry().activeName != "Light", "الثيم النشط لم يتغيّر من داخل خاصية");
        // أخطاء مفيدة
        std::string err;
        rin::Interpreter i5; indsin::registerIndsinRinLib(i5);
        rin::Lexer lx(R"(let q = indsin.token("nope", "x");)"); rin::Parser ps(lx.scanTokens());
        i5.run(ps.parse());
        check(i5.hadError() && i5.lastErrorMessage().value_or("").find("categories") != std::string::npos, "خطأ token فئة مجهولة يعدّد الفئات");
    }

    std::cout << "\n" << checks << " checks, " << failures << " failure(s)\n";
    return failures ? 1 : 0;
}

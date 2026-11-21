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

    // ── جسر << >> (docs/indsin_bridge.md) ──
    eq("bridge text", "<Text#t>Hi <<user.name>>!</Text>", "@view.Text=t text=\"Hi \" + (user.name) + \"!\"; .end/view");
    eq("bridge attr", "<Button#b label=<<title>> />", "@view.Button=b label=title; .end/view");
    eq("bridge quoted", "<Text#t text=\"a <<x>> b\" />", "@view.Text=t text=\"a \" + (x) + \" b\"; .end/view");
    eq("bridge print", "<Text#t><<print a, b + 1>></Text>", "@view.Text=t text=\"\" + (\"\" + (a) + \" \" + (b + 1)); .end/view");
    eq("bridge run", "<Text#t><<run report(n, 2)>></Text>", "@view.Text=t text=\"\" + (indsin.run(\"report\", n, 2)); .end/view");
    eq("bridge '>>' in parens/strings", "<Text#t><<(a >> 1) + \">>\">></Text>", "@view.Text=t text=\"\" + ((a >> 1) + \">>\"); .end/view");
    eq("bridge element", "<Column#c>\n<<n * 2>>\n</Column>", "@view.Column=c\n@view.Text=text_1 text=n * 2; .end/view\n.end/view");
    eq("bridge let/warp/import", "<<let a = 1>><<warp b = 2>><<import \"x\">>", "let a = 1; warp b = 2; @import \"x\";");
    { ++runs; // <<let>> داخل عنصر يُرفع إلى آخر الملف (سطر إضافي بعد كل الأسطر الأصلية)
      std::string got = indsin::tags::desugar("<Column#c>\n<<let q = 5>>\n</Column>");
      if (got != "@view.Column=c\n\n.end/view\nlet q = 5; ") { ++fails; std::cerr << "FAIL bridge let hoisted: " << got << "\n"; } }
    eq("bridge each literal", "<<each [\"a\", \"b\"] as w, i>>\n<Text#t><<i>>=<<w>></Text>\n<</each>>",
       " @view.Text=t_0 text=\"\" + (0) + \"=\" + (((\"a\"))); .end/view   @view.Text=t_1 text=\"\" + (1) + \"=\" + (((\"b\"))); .end/view  \n\n");
    err("bridge unterminated", "<Text>a <<x</Text>", 1, "unterminated");
    err("bridge empty", "<Text><<>></Text>", 1, "empty");
    err("bridge stray else", "<<else>>", 1, "without a matching");
    err("bridge if unclosed", "<Column#c>\n<<if a>>\n</Column>", 2, "<</if>>");
    err("bridge let in text", "<Text><<let a = 1>></Text>", 1, "statement");
    err("bridge each needs runtime", "<<each xs as x>>\n<</each>>", 1, "Rin runtime");
    err("bridge nested each", "<<each [1] as a>>\n<<each [2] as b>>\n<</each>>\n<</each>>", 2, "nested");
    eq("bridge shift untouched", "let x = 1 << 2;\n", "let x = 1 << 2;\n");

    // ── حرية التصميم: Screen / Theme / Palette + قيم نسبية ──
    eq("screen", "<Screen#app size=phone bg=#0B1020 width=300 height=500>\n</Screen>",
       "@view.Column=app screen=\"phone\"; canvas=\"#0B1020\"; canvas_width=300; canvas_height=500;\n.end/view");
    eq("screen background alias", "<Screen#a background=#fff />", "@view.Column=a canvas=\"#fff\"; .end/view");
    eq("relative values", "<Card#c width=50% w2=60vw h=10vh s=390x844 />",
       "@view.Card=c width=\"50%\"; w2=\"60vw\"; h=\"10vh\"; s=\"390x844\"; .end/view");
    eq("theme", "<Theme#Brand primary=#6C5CE7 active />", "@theme=Brand primary=\"#6C5CE7\"; active=true; .end/theme");
    eq("palette", "<Palette seed=#6C5CE7 dark />", "@theme=palette_1 seed=\"#6C5CE7\"; dark=true; active=true; .end/theme");
    eq("palette light", "<Palette#p seed=#6C5CE7 light />", "@theme=p seed=\"#6C5CE7\"; mode=\"light\"; active=true; .end/theme");
    err("palette needs seed", "<Palette dark />", 1, "seed");
    err("theme not self-closed", "<Theme#a primary=#fff>", 1, "self-closing");

    // ── Tokens / Style / class ──
    eq("tokens", "<Tokens brand=#6C5CE7 gap=16 />", "let brand = \"#6C5CE7\"; let gap = 16; ");
    eq("style+class", "<Style#card radius=16 bg=#fff />\n<Card#c class=\"card\" radius=4 />",
       "\n@view.Card=c radius=4; bg=\"#fff\"; .end/view");
    eq("style extends + last class wins", "<Style#a w=1 h=1 /><Style#b extends=\"a\" h=2 />\n<Box#x class=\"b\" />",
       "\n@view.Box=x w=1; h=2; .end/view");
    err("unknown style", "<Box#x class=\"nope\" />", 1, "unknown style");
    err("style extends unknown", "<Style#a extends=\"zz\" />", 1, "extends unknown");
    err("tokens not self-closed", "<Tokens a=1>", 1, "self-closing");

    // ── مكتبة المكوّنات الجاهزة ──
    {
        ++runs;
        std::string src = "\n\n<Chip label=\"a\" />\n<Stat label=\"b\" value=\"1\" />\n";
        try {
            std::string got = indsin::tags::desugar(src);
            if (lines(got) != lines(src)) { ++fails; std::cerr << "FAIL builtin prelude changed the line count\n"; }
            if (got.find("@view.Card") == std::string::npos || got.find("a") == std::string::npos) { ++fails; std::cerr << "FAIL builtin Chip/Stat not expanded\n"; }
        } catch (indsin::tags::TagError& e) { ++fails; std::cerr << "FAIL builtin expand threw: " << e.message << "\n"; }
        ++runs;
        std::string plain = "<Column#r>\n</Column>"; // بلا مكوّنات جاهزة: لا مقدّمة
        if (indsin::tags::desugar(plain).find("Define") != std::string::npos) { ++fails; std::cerr << "FAIL prelude injected without use\n"; }
        ++runs;
        std::string own = "<Define#Chip label=\"x\"><Box#mine /></Define><Chip />"; // تعريف المستخدم يحلّ محلّ الجاهز
        if (indsin::tags::desugar(own).find("@view.Box") == std::string::npos) { ++fails; std::cerr << "FAIL user <Define#Chip> must override the built-in\n"; }
    }
    err("builtin error line", "\n<Section>\n</Row>", 2, "unclosed");

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
    { // ContainerView: مكوّن جاهز + مكتبة Rin تُحقَن فقط عند الاستعمال، والأسطر محفوظة
        ++runs;
        std::string src = "<Column#r>\n<ContainerView name=\"inv\" tick={n} />\n</Column>\n";
        std::string got = indsin::tags::desugar(src);
        bool ok = got.find("fun containerBody(") != std::string::npos && got.find("indsin.run(\"containerBody\", \"inv\", n)") != std::string::npos &&
                  got.find("indsin.run(\"containerKind\"") != std::string::npos && lines(got) == lines(src);
        if (!ok) { ++fails; std::cerr << "FAIL ContainerView expansion\n" << got << "\n"; }
        ++runs;
        std::string plain = indsin::tags::desugar("<Column#r>\n</Column>\n");
        if (plain.find("containerBody") != std::string::npos) { ++fails; std::cerr << "FAIL ContainerView prelude leaked into unrelated source\n"; }
    }

    // ── field ==> body · head.X · rin--g==> (docs/field-head-ring.md) ──
    auto has = [&](const char* name, const std::string& src, std::initializer_list<const char*> parts, std::initializer_list<const char*> absent = {}) {
        ++runs;
        try {
            std::string got = indsin::tags::desugar(src);
            for (const char* p : parts) if (got.find(p) == std::string::npos) { ++fails; std::cerr << "FAIL " << name << " missing: " << p << "\n  got: " << got << "\n"; return; }
            for (const char* p : absent) if (got.find(p) != std::string::npos) { ++fails; std::cerr << "FAIL " << name << " unexpected: " << p << "\n  got: " << got << "\n"; return; }
            if (lines(got) < lines(src)) { ++fails; std::cerr << "FAIL " << name << " (line count shrank)\n"; }
        } catch (indsin::tags::TagError& e) { ++fails; std::cerr << "FAIL " << name << " threw line " << e.line << ": " << e.message << "\n"; }
    };
    const std::string INV = "@container.data=inv\n let apples = 5;\n let owner = \"shop\";\n let tags = [\"a\"];\n fun f() { let inner = 1; }\n.end/container.data\n";
    // field
    has("field default", INV + "<field.inv[];>", {"fld_inv_apples", "container.get(\"inv\", \"apples\")", "\"Owner\"", "jsonEncode(container.get(\"inv\", \"tags\"))"}, {"fld_inv_inner"});
    has("field no semicolon self-close", INV + "<field.inv[] />", {"fld_inv_apples"});
    has("field select order", INV + "<field.inv[owner, apples];>", {"fld_inv_owner"}, {"fld_inv_tags"});
    has("field exclude", INV + "<field.inv[!tags, !owner];>", {"fld_inv_apples"}, {"fld_inv_tags", "fld_inv_owner"});
    has("field body vars", INV + "<field.inv[apples]>\n<Text#t>{label}|{key}|{value}|{kind}|{index}</Text>\n</field>",
        {"\"Apples\"", "(\"apples\")", "container.get(\"inv\", \"apples\")", "(\"number\")", "(0)"});
    has("field as rename", INV + "<field.inv[apples] as=\"k, v\"><Text#t>{k}={v}</Text></field>", {"(\"apples\")", "container.get(\"inv\", \"apples\")"});
    has("field type filter", INV + "<field.inv[] type=\"number\" />", {"fld_inv_apples"}, {"fld_inv_owner", "fld_inv_tags"});
    has("field tick", INV + "<field.inv[apples] tick={n};>", {"fieldGet(\"inv\", \"apples\", n)", "fun fieldGet(name, key, tick)"});
    has("field tick prelude once", INV + "<field.inv[apples] tick={n};>\n<field.inv[owner] tick={n};>", {"fieldGet(\"inv\", \"owner\", n)"});
    has("field implicit tag container", "<Container#C>\n<Rin> let a = 1; let b = 2; </Rin>\n<field[];>\n</Container>", {"container.get(\"C\", \"a\")", "container.get(\"C\", \"b\")"});
    has("field implicit legacy container", "@container=C\n let a = 1;\n <field[];>\n.end/container\n", {"container.get(\"C\", \"a\")"});
    has("field nested container excluded", "@container=P\n let a = 1;\n @container=Q\n  let b = 2;\n .end/container\n let c = 3;\n.end/container\n<field.P[];>",
        {"fld_P_a", "fld_P_c"}, {"fld_P_b"});
    has("field unique ids on reuse", INV + "<field.inv[apples];>\n<field.inv[apples];>", {"fld_inv_apples", "fld_inv_2_apples"});
    has("field external container with names", "<field.ext[p, q];>", {"container.get(\"ext\", \"p\")"});
    has("field arabic container+field", "@container=متجر\n let تفاح = 5;\n.end/container\n<field.متجر[];>", {"container.get(\"متجر\", \"تفاح\")"});
    has("field state/const collected", "@container=C\n state n = 0;\n const k = 1;\n.end/container\n<field.C[];>", {"fld_C_n", "fld_C_k"});
    has("Field component untouched", "<Field label=\"x\" value={v} />", {"@view.Column=fld"});
    err("field unknown container", "<field.nope[];>", 1, "no container named 'nope'");
    err("field unknown name", INV + "<field.inv[zzz];>", 7, "has no field 'zzz'");
    err("field no container", "<field[];>", 1, "has no container");
    err("field unclosed body", INV + "<field.inv[apples]>\n<Text/>", 7, "unclosed tag <field>");
    err("field bad attr", INV + "<field.inv[] foo=1 />", 7, "no attribute 'foo'");
    err("field empty match", INV + "<field.inv[] type=\"bool\" />", 7, "matched no fields");
    // head
    const std::string APP = "@container.app=piano\n let name = \"P\";\n let version = \"1.0.0\";\n let icon = \"i.png\";\n let theme_color = \"#6C5CE7\";\n.end/container.app\n";
    has("head expr", APP + "<head.piano />\n<Text#t>{head.name} v{head.version}</Text>",
        {"container.appInfo(\"piano\")[\"name\"]", "container.appInfo(\"piano\")[\"version\"]"});
    has("head nested seo", APP + "<head.piano;>\n<Text#t>{head.seo.title}</Text>", {"container.appInfo(\"piano\")[\"seo\"][\"title\"]"});
    has("head bridge", APP + "<head.piano />\n<Text#t>x <<head.name>></Text>", {"container.appInfo(\"piano\")[\"name\"]"});
    has("head attr", APP + "<head.piano />\n<Box#b bg={head.theme_color} />", {"bg=container.appInfo(\"piano\")[\"theme_color\"]"});
    has("head string untouched", APP + "<head.piano />\n<Text#t>{\"head.name\"}</Text>", {"\"head.name\""}, {"appInfo(\"piano\")[\"name\"]\"})"});
    has("head show", APP + "<head.piano#bar show />", {"@view.Row=bar", "@view.Image=bar_icon", "bar_name", "bar_ver"});
    has("head show no icon", "@container.app=a\n let name = \"n\";\n.end/container.app\n<head.a show />", {"head_a_name"}, {"_icon", "_ver"});
    has("head apply theme", APP + "<head.piano apply=theme dark />", {"@theme=head_piano_theme", "seed=\"#6C5CE7\"", "dark=true"});
    has("head apply tokens", APP + "<head.piano apply=tokens />", {"let head_name = container.appInfo(\"piano\")[\"name\"];", "let head_version"}, {"head_background_color"});
    has("head block", APP + "<head.piano#top padding=4>\n<Text#t>{head.name}</Text>\n</head>", {"@view.Row=top padding=4;", ".end/view"});
    has("head shorthand app", "@piano.app\n let name = \"P\";\n.end/container.app\n<head.piano />", {});
    has("head extern", "<head.late extern />\n<Text#t>{head.name}</Text>", {"container.appInfo(\"late\")[\"name\"]"});
    has("head before use only", "<Text#t>{head.name}</Text>", {"(head.name)"}, {"appInfo"}); // بلا <head.X> لا يُمسّ شيء (قد يكون نصاً عادياً)
    err("head unknown field", APP + "<head.piano />\n<Text>{head.nme}</Text>", 8, "did you mean 'head.name'");
    err("head no container", "<head.zz />", 1, "no @container.app=zz");
    err("head wrong kind", "@container.data=d\n let a=1;\n.end/container.data\n<head.d />", 4, "needs an app/site container");
    err("head needs name", "<head />", 1, "needs the app container");
    err("head bad apply", APP + "<head.piano apply=\"zzz\" />", 7, "is unknown");
    err("head theme without color", "@container.app=a\n let name=\"n\";\n.end/container.app\n<head.a apply=theme />", 4, "needs a literal theme_color");
    err("head attrs without element", APP + "<head.piano padding=4 />", 7, "no element to apply");
    err("head show + body", APP + "<head.piano show>\n<Text/>\n</head>", 7, "both `show` and a body");
    // rin--g
    has("ring canonical in container", "<Container#C>\n<Rin> let a = 1; </Rin>\n<rin--g==>[]&{{}};>\n</Container>", {"fld_g1_a", "container.get(\"C\", \"a\")"});
    has("ring literal list", "<rin--g==>[\"a\", \"b\", 3] & {{ <Text#t>{{it}}-{{i}}</Text> }};>", {"(\"a\") + \"-\" + (0)", "(\"b\") + \"-\" + (1)", "(3) + \"-\" + (2)"});
    has("ring number", "<rin--g==>2 & {{ <Text#n>row {{i + 1}}</Text> }};>", {"\"row \" + (0 + 1)", "\"row \" + (1 + 1)"});
    has("ring default template", "<rin--g==>[\"x\", \"y\"];>", {"@view.Text=itm_g1_0", "(\"x\")"});
    has("ring container source", INV + "<rin--g==>inv[apples, owner] & {{ <Text#t>{{label}}:{{shown}}</Text> }};>", {"(\"Apples\")", "container.get(\"inv\", \"owner\")"});
    has("ring concat sources", INV + "<rin--g==>inv[apples] & [\"z\"] & {{ <Text#t>{{key}}</Text> }};>", {"(\"apples\")", "(\"1\")"});
    has("ring mustache keeps strings", "<rin--g==>[\"a\"] & {{ <Text#t label=\"{{it}}\">ok</Text> }};>", {"label=\"{{it}}\""});
    has("ring braced literal list", "<rin--g==>{[1, 2]} & {{ <Text#t>{{it}}</Text> }};>", {"(1)", "(2)"});
    has("ring multiline template keeps lines", "<rin--g==>[\"a\"] &\n{{\n<Text#t>{{it}}</Text>\n}};>\nafter", {"after"});
    err("ring needs arrow", "<rin--g [] />", 1, "needs '==>'");
    err("ring no context", "<rin--g==>[]&{{}};>", 1, "has no list");
    err("ring template not last", "<rin--g==>[\"a\"] & {{ <Text/> }} & [\"b\"];>", 1, "must be the last");
    err("ring empty list", "<rin--g==>{[]} & {{ <Text/> }};>", 1, "no items");
    err("ring unterminated template", "<rin--g==>[\"a\"] & {{ <Text/> ;>", 1, "unterminated '{{'");
    err("ring needs end", "<rin--g==>[\"a\"] & {{ <Text/> }}", 1, "must end with");
    { // rin--g {expr}: يحتاج حجم القائمة من التشغيل التجريبي (كـ <<each>>)
        ++runs;
        std::vector<std::string> exprs;
        std::string probe = indsin::tags::desugarProbe("<rin--g==>{users} & {{ <Text#t>{{it}}</Text> }};>\nz", exprs);
        std::map<std::string, indsin::tags::EachSize> counts; counts["users"] = {2, false};
        std::string fin = indsin::tags::desugarWithCounts("<rin--g==>{users} & {{ <Text#t>{{it}}</Text> }};>\nz", counts);
        if (exprs.size() != 1 || exprs[0] != "users" || probe.find("@view") != std::string::npos ||
            fin.find("(users)[0]") == std::string::npos || fin.find("(users)[1]") == std::string::npos || fin.find("(users)[2]") != std::string::npos) {
            ++fails; std::cerr << "FAIL ring {expr} probe/counts\n  probe: " << probe << "\n  final: " << fin << "\n";
        }
        err("ring {expr} without runtime", "<rin--g==>{users} & {{ <Text#t>{{it}}</Text> }};>", 1, "needs the Rin runtime");
    }
    { // توافق عكسي: مصدر بلا الوسوم الثلاثة لا يُمسح ولا يتغيّر
        ++runs;
        std::string plain = "@container=a\n let x = 1;\n.end/container\n<Column#r>\n<Field label=\"f\" />\n</Column>\n";
        std::string got = indsin::tags::desugar(plain);
        if (got.find("container.appInfo") != std::string::npos || got.find("fieldGet") != std::string::npos) { ++fails; std::cerr << "FAIL new concepts leaked into unrelated source\n"; }
    }
    std::cout << (runs - fails) << "/" << runs << " passed\n";
    return fails ? 1 : 0;
}

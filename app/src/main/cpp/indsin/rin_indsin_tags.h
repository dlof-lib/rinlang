// indsin/rin_indsin_tags.h — صيغة الوسوم بالأقواس <> لواجهات indsin ("Tag Syntax").
//
// هذا الملف header-only ومستقل تماماً (لا يعتمد على rin::Lexer/Parser ولا على PITOK)، ويحوّل
// صيغة الوسوم إلى لهجة @view/@loop/@container/@element الحالية *نصاً بنص* قبل التحليل، لذلك
// يعمل مع كل شيء موجود (المحرّك، المعاينة الحيّة، indsinc، PITOK) دون أي تعديل على المحلّلات.
//
// ضمانات:
//  * عدد الأسطر محفوظ حرفياً (كل '\n' في المصدر يقابله '\n' واحد في الناتج) فتبقى أرقام أخطاء
//    Rin/المعاينة صحيحة.
//  * المصدر الذي لا يحوي وسوماً يخرج مطابقاً حرفاً بحرف (توافق عكسي كامل مع الصيغة القديمة).
//  * الصيغتان يمكن خلطهما في الملف نفسه.
//
// ── القواعد ─────────────────────────────────────────────────────────────────────────────
//   <Column#root gap=16 padding="spacious">     →  @view.Column=root gap=16; padding="spacious";
//   </Column>                                    →  .end/view         (يُتحقَّق من تطابق الاسم)
//   <Divider />                                  →  عنصر بلا أبناء (self-closing)
//   <Text#t size=22>Count: {count}</Text>        →  text="Count: " + (count);   (نص كابن)
//   <Button#inc onTap={count = count + 1}>Add</Button>   (الزر: النص الابن يصبح label=)
//   <Banner dismissible />                       →  dismissible=true;   (سمة منطقية مختصرة)
//   <warp count={0} />                           →  warp count = 0;
//   <Import src="lib/ui.og.rin" />               →  @import "lib/ui.og.rin";
//   <Loop#canvas> … </Loop>                      →  @loop=canvas … .end/loop
//   <Container#App> … </Container>               →  @container=App … .end/container
//   <Element.Button#run> … </Element>            →  @element.Button=run … .end/element
//   <Screen#app size=phone bg=#0B1020> … </Screen>   →  جذر شاشة بحجم/لون خلفية حرّين (screen / canvas_width / canvas_height / canvas)
//   <Theme#Brand primary=#6C5CE7 active />        →  @theme=Brand primary="#6C5CE7"; active=true; .end/theme
//   <Palette seed=#6C5CE7 dark />                 →  ثيم كامل من لون واحد (يُفعَّل تلقائياً)
//   <Rin> كود Rin خام </Rin>                     →  يُنسَخ حرفياً (دوال/let/…)
//   <Fun#f params="a,b"> … </Fun>               →  fun f(a, b) { … }
//   <> … </>                                     →  تجميع شفاف (Fragment): لا يُنتج عنصراً
//   <!-- تعليق -->                               →  // تعليق
//
// جسر Rin ⇄ indsin بالأقواس المزدوجة << >> (docs/indsin_bridge.md): Rin للبيانات والمنطق، وindsin يرسم الواجهة.
//   <Text>Hi <<user.name>>!</Text>         ربط تفاعلي بأي تعبير Rin (≡ {expr})، في النص والسمات وداخل "…<<x>>…"
//   <<print a, b>>                         عرض قيمتين أو أكثر كما تفعل print (مفصولتين بمسافة)
//   <<run report(n)>>                      استدعاء دالة Rin وعرض ما تطبعه (indsin.run: مُخزَّنة، وتُعاد عند تغيّر n)
//   <<name>> (وحدها في موضع عنصر)          عنصر Text جاهز
//   <<let x = expr>> · <<warp n = 0>> · <<import "lib">>
//   <<if c>> … <<else if c2>> … <<else>> … <</if>>
//   <<each items as it, i>> … <</each>>    تكرار على مصفوفة/رقم من Rin (يُقاس حجمها بتشغيل تجريبي — انظر pipeline)
//
// قيم السمات:
//   "نص" أو 'نص'      سلسلة حرفية          |  {تعبير Rin}   تعبير يُقيَّم (متغيرات warp، شروط، دوال)
//   12  120px  250ms   رقم/طول/زمن كما هو    |  #1E1E2E       لون (يُحاط بعلامتي اقتباس تلقائياً)
//   true false null    ثوابت                 |  spacious      كلمة مجرّدة = توكن نصّي (مثل HTML)
//   (متغيّر Rin يُكتب دائماً بين أقواس معقوصة: visible={flag})
#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace indsin {
namespace tags {
// اسم عنصر يطابق كلمة محجوزة في Rin (save/text/file/link/…) لا يُقبل بعد '=' إلا مقتبساً: `@element.button="save"`.
inline std::string nameToken(const std::string& id) {
    static const char* kw[] = {"let","print","if","else","while","fun","return","true","false","nil","and","or","break","continue",
        "rinopen","for","text","container","containers","group","volume","section","translations","translation","link","tying",
        "merge","installation","simplified","save","file","end","pipe"};
    for (const char* k : kw) if (id == k) return "\"" + id + "\"";
    return id;
}


struct TagError {
    std::string message;
    int line;
};

// حجم قائمة <<each>> بعد التشغيل التجريبي: numeric=true إن كان التعبير رقماً (it ← k) لا مصفوفة (it ← (LIST)[k]).
struct EachSize {
    int count = 0;
    bool numeric = false;
};

namespace detail {

inline bool isIdStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
inline bool isIdChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

struct Attr {
    std::string key;
    std::string value; // نص Rin جاهز كقيمة
};

struct Frame {
    std::string tagName; // كما كُتب (للتحقق من الإغلاق)
    std::string closer;  // ".end/view" ... أو "" للـ fragment
    int line;
    std::string chain;   // ما يجب نفيه في <Else> التالي (If: الشرط؛ Else-if: تراكمي)
    std::string cond;    // شرط <If>/<Else> (تعبير Rin) — يُحقَن كـ visible= في العناصر الأبناء
    bool br = false;     // فُتح بصيغة <<if>> (يُغلَق بـ <</if>> ويقبل <<else>>)
    std::string cname;   // اسم الحاوية إن كان الإطار <Container#X> (لـ <field[]> بلا اسم)
};

struct Macro {
    std::vector<std::pair<std::string, std::string>> props; // الخصائص وقيمها الافتراضية (نص Rin)
    std::string body;                                       // مصدر الجسم الخام (بصيغة الوسوم)
};

// ── مسح ساكن للحاويات (field ==> body · head.X · rin--g): أسماء الحاويات وحقولها (let/state/const) بلا تشغيل ──
struct FieldInfo {
    std::string name, kind, raw; // kind: string|number|bool|list|map|nil|expr
};
struct ContainerInfo {
    std::string name, kind;      // kind: plain|app|site|data|… أو "tag" لوسم <Container#X> بلا نوع
    bool tagForm = false;
    size_t begin = 0, end = 0;   // مواضع في المصدر الممسوح (لمعرفة الحاوية المحيطة بوسم)
    std::vector<FieldInfo> fields;
    const FieldInfo* find(const std::string& nm) const {
        for (auto& f : fields) if (f.name == nm) return &f;
        return nullptr;
    }
};

// معرِّفات تقبل العربية (بايتات UTF-8 ≥ 0x80) كما تقبلها أسماء الحقول والحاويات.
inline bool isWordByte(char c) { return std::isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80; }

inline std::string inferKind(const std::string& raw) {
    size_t a = 0, b = raw.size();
    while (a < b && std::isspace((unsigned char)raw[a])) ++a;
    while (b > a && (std::isspace((unsigned char)raw[b - 1]) || raw[b - 1] == ';')) --b;
    if (a >= b) return "expr";
    const std::string t = raw.substr(a, b - a);
    char c = t[0];
    if (c == '"' || c == '\'') return "string";
    if (c == '[') return "list";
    if (c == '{') return "map";
    if (t == "true" || t == "false") return "bool";
    if (t == "nil" || t == "null") return "nil";
    size_t k = 0;
    if (t[k] == '-' || t[k] == '+') ++k;
    size_t digits = 0;
    while (k < t.size() && (std::isdigit((unsigned char)t[k]) || t[k] == '.' || t[k] == '_')) { if (std::isdigit((unsigned char)t[k])) ++digits; ++k; }
    if (digits > 0 && k == t.size()) return "number";
    return "expr";
}

inline std::vector<ContainerInfo> scanContainers(const std::string& s) {
    std::vector<ContainerInfo> res;
    struct Open { size_t idx; int depth; bool tagForm; };
    std::vector<Open> open;
    const size_t n = s.size();
    auto word = [&](size_t& p) { size_t b = p; while (p < n && isWordByte(s[p])) ++p; return s.substr(b, p - b); };
    auto skipSp = [&](size_t& p) { while (p < n && (s[p] == ' ' || s[p] == '\t')) ++p; };
    auto skipWsAll = [&](size_t& p) { while (p < n && std::isspace((unsigned char)s[p])) ++p; };
    auto push = [&](const std::string& nm, const std::string& kind, bool tagForm, size_t at) {
        ContainerInfo ci; ci.name = nm; ci.kind = kind; ci.tagForm = tagForm; ci.begin = at; ci.end = n;
        res.push_back(std::move(ci));
        open.push_back({res.size() - 1, 0, tagForm});
    };
    auto popKind = [&](bool tagForm, size_t at) {
        for (size_t q = open.size(); q-- > 0;) if (open[q].tagForm == tagForm) { res[open[q].idx].end = at; open.erase(open.begin() + (long)q); return; }
    };
    // يقرأ "name = value" عند p (بعد let/state/const) ويضيف الحقل إلى الحاوية المفتوحة.
    auto tryField = [&](size_t p, bool bridge) {
        skipWsAll(p);
        std::string nm = word(p);
        if (nm.empty() || std::isdigit((unsigned char)nm[0])) return;
        skipSp(p);
        if (p >= n || s[p] != '=' || (p + 1 < n && s[p + 1] == '=')) return;
        ++p;
        std::string raw; int d = 0;
        while (p < n && raw.size() < 300) {
            char ch = s[p];
            if (ch == '"' || ch == '\'') {
                char q = ch; raw += ch; ++p;
                while (p < n && s[p] != q && s[p] != '\n') { if (s[p] == '\\' && p + 1 < n) raw += s[p++]; raw += s[p++]; }
                if (p < n && s[p] == q) raw += s[p++];
                continue;
            }
            if (ch == '(' || ch == '[' || ch == '{') ++d;
            else if (ch == ')' || ch == ']' || ch == '}') { if (d > 0) --d; }
            else if (d == 0 && (ch == ';' || ch == '\n')) break;
            else if (d == 0 && bridge && ch == '>' && p + 1 < n && s[p + 1] == '>') break;
            else if (d == 0 && ch == '<' && p + 1 < n && s[p + 1] == '/') break;
            raw += ch; ++p;
        }
        ContainerInfo& ci = res[open.back().idx];
        if (!ci.find(nm)) ci.fields.push_back({nm, inferKind(raw), raw});
    };
    size_t k = 0;
    while (k < n) {
        char c = s[k];
        if (c == '/' && k + 1 < n && s[k + 1] == '/') { while (k < n && s[k] != '\n') ++k; continue; }
        if (c == '/' && k + 1 < n && s[k + 1] == '*') { size_t e = s.find("*/", k + 2); k = e == std::string::npos ? n : e + 2; continue; }
        if (c == '<' && s.compare(k, 4, "<!--") == 0) { size_t e = s.find("-->", k + 4); k = e == std::string::npos ? n : e + 3; continue; }
        if (c == '"') { ++k; while (k < n && s[k] != '"') { if (s[k] == '\\') ++k; ++k; } ++k; continue; }
        if (c == '\'') { // سلسلة فقط إن أُغلقت في السطر نفسه (الفاصلة العليا في نص عادي لا تكسر المسح)
            size_t e = k + 1;
            while (e < n && s[e] != '\'' && s[e] != '\n') { if (s[e] == '\\') ++e; ++e; }
            if (e < n && s[e] == '\'') k = e + 1; else ++k;
            continue;
        }
        if (c == '{') { if (!open.empty()) ++open.back().depth; ++k; continue; }
        if (c == '}') { if (!open.empty() && open.back().depth > 0) --open.back().depth; ++k; continue; }
        if (c == '@') {
            size_t p = k + 1;
            std::string w = word(p);
            if (w == "container") {
                std::string kind = "plain";
                if (p < n && s[p] == '.') { ++p; kind = word(p); }
                skipSp(p);
                if (p < n && s[p] == '=') {
                    ++p; skipSp(p);
                    std::string nm;
                    if (p < n && (s[p] == '"' || s[p] == '\'')) { char q = s[p++]; size_t b = p; while (p < n && s[p] != q) ++p; nm = s.substr(b, p - b); if (p < n) ++p; }
                    else nm = word(p);
                    if (!nm.empty()) push(nm, kind, false, k);
                }
                k = p; continue;
            }
            if (!w.empty() && p < n && s[p] == '.') { // اختصار @piano.app / @blog.site
                size_t q = p + 1;
                std::string sub = word(q);
                if (sub == "app" || sub == "site") { push(w, sub, false, k); k = q; continue; }
            }
            ++k; continue;
        }
        if (c == '.' && s.compare(k, 14, ".end/container") == 0) { popKind(false, k); k += 14; continue; }
        if (c == '<') {
            if (k + 10 < n && lower(s.substr(k + 1, 9)) == "container" &&
                (s[k + 10] == '#' || s[k + 10] == '.' || s[k + 10] == '>' || s[k + 10] == '/' || std::isspace((unsigned char)s[k + 10]))) {
                size_t p = k + 10;
                std::string kind = "tag";
                if (p < n && s[p] == '.') { ++p; kind = word(p); }
                std::string nm;
                if (p < n && s[p] == '#') { ++p; nm = word(p); }
                size_t e = p; int bd = 0; char q = 0;
                while (e < n) {
                    char ch = s[e];
                    if (q) { if (ch == '\\') ++e; else if (ch == q) q = 0; }
                    else if (ch == '"' || ch == '\'') q = ch;
                    else if (ch == '{') ++bd;
                    else if (ch == '}') --bd;
                    else if (ch == '>' && bd <= 0) break;
                    ++e;
                }
                bool selfc = e > 0 && e < n && s[e - 1] == '/';
                if (!nm.empty()) { push(nm, kind, true, k); if (selfc) { res[open.back().idx].end = e; open.pop_back(); } }
                k = e < n ? e + 1 : n; continue;
            }
            if (k + 11 <= n && s[k + 1] == '/' && lower(s.substr(k + 2, 9)) == "container") { popKind(true, k); k += 11; continue; }
            ++k; continue;
        }
        if (isWordByte(c) && (k == 0 || !isWordByte(s[k - 1]))) {
            size_t p = k;
            std::string w = word(p);
            if ((w == "let" || w == "state" || w == "const") && !open.empty() && open.back().depth == 0) {
                size_t b = k; bool ok = false;
                while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
                if (b == 0 || s[b - 1] == '\n') ok = true;
                else { char pc = s[b - 1]; ok = pc == ';' || pc == '}' || pc == '>' || pc == '{'; }
                if (ok) tryField(p, false);
            }
            k = p > k ? p : k + 1; continue;
        }
        ++k;
    }
    return res;
}

struct Ctx { // حالة مشتركة بين التشغيل الرئيسي وتشغيلات التوسعة (Define/Repeat)
    std::map<std::string, Macro> macros;
    std::map<std::string, std::vector<Attr>> styles; // <Style#name …> — أنماط قابلة لإعادة الاستعمال عبر class="…"
    int autoId = 0;
    int depth = 0;
    // ── جسر << >> ──
    std::string tail;                         // عبارات Rin مرفوعة (<<let>> داخل عنصر) تُلحَق بآخر الناتج
    bool probe = false;                       // تمريرة قياس: <<each>> غير الحرفية تُسجَّل ولا تُوسَّع
    std::vector<std::string> eachExprs;       // تعابير القوائم المسجَّلة في تمريرة القياس (بترتيب أول ظهور)
    std::map<std::string, EachSize> eachCounts; // تعبير القائمة → حجمها ونوعها (من التشغيل التجريبي)
    // ── field ==> body · head.X · rin--g ──
    std::vector<ContainerInfo> reg;           // مسح ساكن للحاويات وحقولها (يُملأ فقط إن استُعمل أحد الوسوم الثلاثة)
    std::string curContainer;                 // الحاوية التي يقع فيها الوسم الحالي (@container=X … .end/container)
    std::string headApp;                      // اسم @container.app المربوط بـ <head.X> (يفعّل head.* في التعابير)
    std::map<std::string, int> fieldUse;      // عدد استعمالات <field.X> (لأسماء عناصر فريدة)
    int ringUse = 0;
    bool fieldGetDeclared = false;
};

class Desugarer {
public:
    Desugarer(const std::string& src, Ctx& c, std::map<std::string, std::string> v = {}, std::string suffix = "", bool flatMode = false, std::string base = "")
        : ctx(c), vars(std::move(v)), idSuffix(std::move(suffix)), flat(flatMode), baseCond(std::move(base)), s(src), n(src.size()) {}

    std::string run() {
        if (!flat && s.find('<') == std::string::npos) return s;
        out.reserve(s.size() + s.size() / 4);
        while (i < n) {
            char c = s[i];
            if (c == '\n') { out += c; ++i; ++line; boundary = true; continue; }
            if (boundary && isTagStart(i)) { handleTag(); continue; }
            if (std::isspace((unsigned char)c)) { out += c; ++i; continue; }
            boundary = false;
            if (c == '/' && i + 1 < n && s[i + 1] == '/') { copyLineComment(); continue; }
            if (c == '/' && i + 1 < n && s[i + 1] == '*') { copyBlockComment(); continue; }
            if (c == '"' || c == '\'') { copyString(c); continue; }
            out += c; ++i;
        }
        if (!stack.empty()) {
            const Frame& f = stack.back();
            if (f.br) throw TagError{"unclosed <<if>> (missing <</if>>)", f.line};
            throw TagError{"unclosed tag <" + f.tagName + ">", f.line};
        }
        if (flat) for (auto& ch : out) if (ch == '\n' || ch == '\r') ch = ' ';
        return out;
    }

private:
    Ctx& ctx;
    std::map<std::string, std::string> vars; // استبدال المعاملات داخل { } (Define/Repeat)
    std::string idSuffix;                    // لاحقة تُضاف لأسماء #id لتفادي التكرار عند التوسعة
    bool flat;                               // وضع التوسعة: بلا تعليقات، والأسطر تُسطَّح
    std::string baseCond;                    // شرط موروث من <If> المحيط بالقالب/التكرار
    std::map<int, std::string> lastIf;       // آخر شرط <If> أُغلق عند كل عمق (لـ <Else>)
    const std::string& s;
    size_t n;
    size_t i = 0;
    int line = 1;
    bool boundary = true; // نحن في موضع بداية عبارة (أول السطر أو بعد وسم)
    int autoId = 0;
    std::string out;
    std::vector<Frame> stack;

    [[noreturn]] void fail(const std::string& m, int ln = -1) const { throw TagError{m, ln < 0 ? line : ln}; }

    bool isTagStart(size_t p) const {
        if (s[p] != '<' || p + 1 >= n) return false;
        char c = s[p + 1];
        if (c == '<') return p + 2 < n && s[p + 2] != '<' && s[p + 2] != '=';   // <<…>> جسر Rin (لا <<< ولا <<=)
        if (isIdStart(c) || c == '>') return true;
        if (c == '/') return p + 2 < n && (isIdStart(s[p + 2]) || s[p + 2] == '>');
        return s.compare(p, 4, "<!--") == 0;
    }

    // ── نسخ حرفي لما لا يخصّنا (تعليقات/سلاسل) مع عدّ الأسطر ──
    void copyLineComment() { while (i < n && s[i] != '\n') { if (!flat) out += s[i]; ++i; } }
    void copyBlockComment() {
        if (!flat) out += "/*";
        i += 2;
        while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/')) { if (s[i] == '\n') ++line; if (!flat) out += s[i]; ++i; }
        if (i < n) { if (!flat) out += "*/"; i += 2; }
    }
    void copyString(char q) {
        out += s[i++];
        while (i < n && s[i] != q) {
            if (s[i] == '\\' && i + 1 < n) { out += s[i++]; }
            if (s[i] == '\n') ++line;
            out += s[i++];
        }
        if (i < n) out += s[i++];
    }

    // ── أدوات مسح داخل الوسم ──
    int skipWs() { // يتخطى المسافات ويعيد عدد الأسطر المستهلكة
        int nl = 0;
        while (i < n) {
            if (s[i] == '\n') { ++nl; ++line; ++i; }
            else if (std::isspace((unsigned char)s[i])) ++i;
            else if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') { while (i < n && s[i] != '\n') ++i; }
            else break;
        }
        return nl;
    }

    std::string readQuoted() { // يقرأ "..." أو '...' ويعيد سلسلة بعلامتي " دائماً (ويدعم الاستيفاء "مرحباً <<name>>")
        char q = s[i++];
        std::string body;
        std::vector<std::string> parts; // غير فارغة فقط عند وجود <<…>> داخل السلسلة
        while (i < n && s[i] != q) {
            if (s[i] == '\\' && i + 1 < n) { body += s[i++]; body += s[i++]; continue; }
            if (s[i] == '<' && i + 1 < n && s[i + 1] == '<' && i + 2 < n && s[i + 2] != '<' && s[i + 2] != '=') {
                if (!body.empty()) { parts.push_back("\"" + body + "\""); body.clear(); }
                parts.push_back("(" + bridgeValue() + ")");
                continue;
            }
            if (s[i] == '\n') ++line;
            if (q == '\'' && s[i] == '"') { body += "\\\""; ++i; continue; }
            body += s[i++];
        }
        if (i >= n) fail("unterminated string in tag");
        ++i;
        if (parts.empty()) return "\"" + body + "\"";
        if (!body.empty()) parts.push_back("\"" + body + "\"");
        std::string r = parts[0][0] == '"' ? "" : "\"\" + ";
        for (size_t k = 0; k < parts.size(); ++k) { if (k) r += " + "; r += parts[k]; }
        return r;
    }

    std::string readBraced() { // يقرأ {…} متوازنة ويعيد المحتوى (بلا الأقواس)
        int startLine = line;
        ++i; // {
        int depth = 1;
        std::string body;
        while (i < n) {
            char c = s[i];
            if (c == '"' || c == '\'') {
                char q = c; body += s[i++];
                while (i < n && s[i] != q) {
                    if (s[i] == '\\' && i + 1 < n) body += s[i++];
                    if (s[i] == '\n') ++line;
                    body += s[i++];
                }
                if (i < n) body += s[i++];
                continue;
            }
            if (c == '{') ++depth;
            if (c == '}') { if (--depth == 0) { ++i; return applyVars(trim(body)); } }
            if (c == '\n') ++line;
            body += c; ++i;
        }
        fail("unterminated '{' expression", startLine);
    }

    static std::string trim(const std::string& x) {
        size_t a = 0, b = x.size();
        while (a < b && std::isspace((unsigned char)x[a])) ++a;
        while (b > a && std::isspace((unsigned char)x[b - 1])) --b;
        std::string r = x.substr(a, b - a);
        while (!r.empty() && r.back() == ';') { r.pop_back(); while (!r.empty() && std::isspace((unsigned char)r.back())) r.pop_back(); }
        return r;
    }

    std::string readValue() {
        if (i >= n) fail("unexpected end of file in tag");
        char c = s[i];
        if (c == '"' || c == '\'') return readQuoted();
        if (c == '{') {
            std::string e = readBraced();
            if (e.empty()) fail("empty {} expression in tag attribute");
            return e;
        }
        if (c == '<' && i + 2 < n && s[i + 1] == '<') return bridgeValue(); // سمة=<<expr>> ≡ سمة={expr}
        if (c == '#') { // لون
            size_t b = i++;
            while (i < n && std::isxdigit((unsigned char)s[i])) ++i;
            if (i - b < 2) fail("expected hex colour after '#'");
            return "\"" + s.substr(b, i - b) + "\"";
        }
        if (std::isdigit((unsigned char)c) || ((c == '-' || c == '+' || c == '.') && i + 1 < n &&
                                                (std::isdigit((unsigned char)s[i + 1]) || s[i + 1] == '.'))) {
            size_t b = i++;
            while (i < n && (std::isalnum((unsigned char)s[i]) || s[i] == '.' || s[i] == '%' || s[i] == '_')) ++i;
            std::string tok = s.substr(b, i - b);
            // النسب (50%) ووحدات الشاشة (60vw، 50vh، 100vmin) وأحجام "390x844" ليست أرقاماً في Rin → تُمرَّر نصاً.
            size_t t = tok.find_first_not_of("+-.0123456789");
            if (t != std::string::npos) {
                std::string tail = lower(tok.substr(t));
                size_t xp = tail.find('x');
                bool dims = xp == 0 && tail.size() > 1 && std::isdigit((unsigned char)tail[1]);
                if (tail == "%" || tail == "vw" || tail == "vh" || tail == "vmin" || tail == "vmax" || dims) return "\"" + tok + "\"";
            }
            return tok;
        }
        if (isIdStart(c)) {
            size_t b = i;
            while (i < n && (isIdChar(s[i]) || s[i] == '-')) ++i;
            std::string w = s.substr(b, i - b);
            if (w == "true" || w == "false" || w == "null") return w;
            return "\"" + w + "\"";
        }
        fail(std::string("unexpected '") + c + "' in attribute value");
    }

    // ── نص كابن: "Count: {n} items" → "Count: " + (n) + " items" ──
    static std::string entity(const std::string& e, bool& ok) {
        ok = true;
        if (e == "lt") return "<";
        if (e == "gt") return ">";
        if (e == "amp") return "&";
        if (e == "lbrace") return "{";
        if (e == "rbrace") return "}";
        if (e == "quot") return "\"";
        ok = false; return "";
    }
    static void flushLiteral(std::string& lit, std::vector<std::string>& parts) {
        if (lit.empty()) return;
        std::string q = "\"";
        for (char c : lit) { if (c == '"' || c == '\\') q += '\\'; q += c; }
        q += '"';
        parts.push_back(q);
        lit.clear();
    }

    // يحاول قراءة محتوى نصّي خالص بدءاً من الموضع الحالي حتى </name>. عند النجاح يعيد true ويترك i بعد
    // وسم الإغلاق؛ وإلا يترك الحالة كما هي ويعيد false (للمحتوى الذي فيه عناصر أبناء).
    bool tryTextContent(const std::string& tagName, std::string& exprOut) {
        size_t save = i; int saveLine = line;
        std::vector<std::string> parts;
        std::string lit;
        bool sawSpace = false, any = false;
        while (i < n) {
            char c = s[i];
            if (c == '<' && i + 2 < n && s[i + 1] == '<' && s[i + 2] != '<' && s[i + 2] != '=') { // <<expr>> داخل النص
                if (!lit.empty() || sawSpace) { if (sawSpace && any) lit += ' '; }
                sawSpace = false;
                flushLiteral(lit, parts);
                parts.push_back("(" + bridgeValue() + ")");
                any = true;
                continue;
            }
            if (c == '<') {
                if (i + 1 < n && s[i + 1] == '/') break;
                i = save; line = saveLine; return false; // عنصر ابن
            }
            if (c == '{') {
                if (!lit.empty() || sawSpace) { if (sawSpace && any) lit += ' '; }
                sawSpace = false;
                flushLiteral(lit, parts);
                parts.push_back("(" + readBraced() + ")");
                any = true;
                continue;
            }
            if (c == '&') {
                size_t semi = s.find(';', i);
                if (semi != std::string::npos && semi - i <= 8) {
                    bool ok; std::string r = entity(s.substr(i + 1, semi - i - 1), ok);
                    if (ok) {
                        if (sawSpace && any) lit += ' ';
                        sawSpace = false; lit += r; any = true; i = semi + 1; continue;
                    }
                }
            }
            if (std::isspace((unsigned char)c)) { if (c == '\n') ++line; sawSpace = true; ++i; continue; }
            if (sawSpace && any) lit += ' ';
            sawSpace = false; lit += c; any = true; ++i;
        }
        if (!any) { i = save; line = saveLine; return false; }
        // وسم الإغلاق
        if (!(i + 1 < n && s[i] == '<' && s[i + 1] == '/')) fail("unclosed tag <" + tagName + ">");
        i += 2;
        size_t b = i;
        while (i < n && (isIdChar(s[i]) || s[i] == '.')) ++i;
        std::string closeName = s.substr(b, i - b);
        if (i < n && s[i] == '>') ++i; else fail("malformed closing tag");
        if (!closeName.empty() && lower(closeName) != lower(tagName))
            fail("closing tag </" + closeName + "> does not match <" + tagName + ">");
        flushLiteral(lit, parts);
        if (parts.size() == 1 && parts[0][0] == '"') exprOut = parts[0];
        else {
            exprOut = parts.empty() ? "\"\"" : "";
            if (!parts.empty() && parts[0][0] != '"') exprOut = "\"\" + ";
            for (size_t k = 0; k < parts.size(); ++k) { if (k) exprOut += " + "; exprOut += parts[k]; }
        }
        return true;
    }

    static const char* textAttrFor(const std::string& kindLower) {
        if (kindLower == "text") return "text";
        if (kindLower == "button") return "label";
        if (kindLower == "banner") return "message";
        return nullptr;
    }

    // ── استبدال المعاملات: كل معرِّف في vars يُستبدل بقيمته (خارج السلاسل) ──
    static bool simpleValue(const std::string& v) {
        if (v.empty()) return false;
        if (v.front() == '"' && v.back() == '"' && v.size() >= 2 && v.find('"', 1) == v.size() - 1) return true;
        bool ok = true;
        for (char c : v) if (!(isIdChar(c) || c == '.' || c == '-' || c == '%')) { ok = false; break; }
        return ok;
    }
    std::string applyVars(const std::string& e0) const {
        std::string e = vars.empty() ? e0 : substituteVars(e0);
        if (!ctx.headApp.empty() && e.find("head") != std::string::npos) e = headRewrite(e); // head.name → container.appInfo("APP")["name"]
        return e;
    }
    std::string substituteVars(const std::string& e) const {
        if (vars.empty()) return e;
        std::string r;
        size_t k = 0;
        while (k < e.size()) {
            char c = e[k];
            if (c == '"' || c == '\'') {
                size_t b = k++;
                while (k < e.size() && e[k] != c) { if (e[k] == '\\' && k + 1 < e.size()) ++k; ++k; }
                if (k < e.size()) ++k;
                r += e.substr(b, k - b);
            } else if (isIdStart(c)) {
                size_t b = k;
                while (k < e.size() && isIdChar(e[k])) ++k;
                std::string w = e.substr(b, k - b);
                bool member = b > 0 && e[b - 1] == '.';
                auto it = vars.find(w);
                if (it != vars.end() && !member) r += simpleValue(it->second) ? it->second : "(" + it->second + ")";
                else r += w;
            } else { r += c; ++k; }
        }
        return r;
    }

    // يبحث عن </name> المطابق (مع احتساب التداخل إن nest) ابتداءً من from؛ يعيد موضعه أو npos.
    size_t findClose(const std::string& name, size_t from, bool nest) const {
        std::string ln = lower(name);
        int depth = 1;
        for (size_t k = from; k < n; ++k) {
            if (s[k] != '<') continue;
            if (k + 1 < n && s[k + 1] == '/') {
                size_t e = k + 2, b = e;
                while (e < n && (isIdChar(s[e]) || s[e] == '.')) ++e;
                if (lower(s.substr(b, e - b)) == ln && --depth == 0) return k;
            } else if (nest) {
                size_t e = k + 1, b = e;
                while (e < n && (isIdChar(s[e]) || s[e] == '.')) ++e;
                if (lower(s.substr(b, e - b)) == ln) {
                    // وسم ذاتي الإغلاق لا يزيد العمق
                    size_t g = e; char q = 0;
                    while (g < n && (q || s[g] != '>')) { if (q) { if (s[g] == q) q = 0; } else if (s[g] == '"' || s[g] == '\'') q = s[g]; ++g; }
                    if (!(g > 0 && s[g - 1] == '/')) ++depth;
                }
            }
        }
        return std::string::npos;
    }

    // يلتقط الجسم الخام حتى </name>، ويتخطى وسم الإغلاق. يعيد الجسم.
    std::string captureBody(const std::string& name, bool nest, int startLine) {
        size_t c = findClose(name, i, nest);
        if (c == std::string::npos) fail("unclosed tag <" + name + ">", startLine);
        std::string body = s.substr(i, c - i);
        size_t e = s.find('>', c);
        if (e == std::string::npos) fail("malformed closing tag </" + name + ">", startLine);
        for (size_t k = i; k <= e; ++k) if (s[k] == '\n') ++line;
        i = e + 1;
        return body;
    }

    static int intAttr(const std::string& v, const char* what, int line) {
        std::string x = v;
        if (!x.empty() && x[0] == '(') x = x.substr(1, x.size() - 2);
        try { size_t used = 0; int r = std::stoi(x, &used); if (used == x.size()) return r; } catch (...) {}
        throw TagError{std::string(what) + " must be an integer literal", line};
    }

    std::string expandBody(const std::string& body, std::map<std::string, std::string> v, const std::string& suffix,
                           const std::string& what, int startLine, const std::string& base = "") {
        if (ctx.depth >= 24) fail("template nesting too deep (recursive <" + what + ">?)", startLine);
        ++ctx.depth;
        std::string r;
        try {
            Desugarer sub(body, ctx, std::move(v), suffix, true, base);
            r = sub.run();
        } catch (TagError& e) {
            --ctx.depth;
            throw TagError{"in <" + what + ">: " + e.message, startLine};
        }
        --ctx.depth;
        return r;
    }

    std::string activeCond() const {
        std::string c = baseCond.empty() ? "" : baseCond;
        for (auto& f : stack) if (!f.cond.empty()) { if (!c.empty()) c += " and "; c += "(" + f.cond + ")"; }
        return c;
    }

    // يضيف النص مع فاصل إن لزم، حتى لا تلتصق العبارات المتجاورة (…=name@view.X / .end/view.end/view)
    void put(const std::string& text) {
        if (!out.empty() && !std::isspace((unsigned char)out.back()) && !text.empty()) out += ' ';
        out += text;
    }

    void emitNewlines(int nl) { for (int k = 0; k < nl; ++k) out += '\n'; }

    // ═════════════════════ جسر Rin ⇄ indsin: << … >> ═════════════════════
    static std::string trimWs(const std::string& x) {
        size_t a = 0, b = x.size();
        while (a < b && std::isspace((unsigned char)x[a])) ++a;
        while (b > a && std::isspace((unsigned char)x[b - 1])) --b;
        return x.substr(a, b - a);
    }
    static std::string firstWord(const std::string& raw) {
        size_t k = 0;
        while (k < raw.size() && isIdChar(raw[k])) ++k;
        return raw.substr(0, k);
    }
    // يقسم e على sep خارج السلاسل والأقواس.
    static std::vector<std::string> splitTop(const std::string& e, char sep = ',') {
        std::vector<std::string> r;
        std::string cur;
        int depth = 0;
        for (size_t k = 0; k < e.size(); ++k) {
            char c = e[k];
            if (c == '"' || c == '\'') {
                char q = c; cur += c; ++k;
                while (k < e.size() && e[k] != q) { if (e[k] == '\\' && k + 1 < e.size()) cur += e[k++]; cur += e[k++]; }
                if (k < e.size()) cur += e[k];
                continue;
            }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; }
            if (c == sep && depth == 0) { r.push_back(cur); cur.clear(); continue; }
            cur += c;
        }
        r.push_back(cur);
        return r;
    }

    // يقرأ <<…>> عند الموضع الحالي (على أول '<') ويعيد المحتوى الخام مشذَّباً. '>>' داخل سلسلة أو (…) […] {…} لا تُنهيه.
    std::string readBridge() {
        const int startLine = line;
        i += 2;
        int depth = 0;
        std::string body;
        while (i < n) {
            char c = s[i];
            if (c == '"' || c == '\'') {
                char q = c; body += s[i++];
                while (i < n && s[i] != q) {
                    if (s[i] == '\\' && i + 1 < n) body += s[i++];
                    if (s[i] == '\n') ++line;
                    body += s[i++];
                }
                if (i < n) body += s[i++];
                continue;
            }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; }
            else if (c == '>' && depth == 0 && i + 1 < n && s[i + 1] == '>') { i += 2; return trimWs(body); }
            if (c == '\n') ++line;
            body += c; ++i;
        }
        fail("unterminated '<<' (missing '>>')", startLine);
    }

    // محتوى << >> في موضع قيمة (نص ابن / سمة / داخل "…") → تعبير Rin.
    //   <<expr>>            تعبير نقي تفاعلي
    //   <<print a, b>>      "" + (a) + " " + (b)                  (كما تفعل print: مسافة بين القيم)
    //   <<run f(x, y)>>     indsin.run("f", x, y)                  (استدعاء دالة وعرض ما تطبعه؛ مُخزَّن بالوسائط)
    std::string bridgeExpr(const std::string& raw, int ln) {
        if (raw.empty()) fail("empty <<>> bridge: put a Rin expression inside, e.g. <<user.name>>", ln);
        if (raw[0] == '/') fail("closing '<<" + raw + ">>' without its opening block", ln);
        std::string w = firstWord(raw);
        if (w == "else" || w == "if" || w == "each" || w == "let" || w == "const" || w == "warp" || w == "import")
            fail("<<" + w + " …>> is a statement: write it on its own, not inside text or an attribute value", ln);
        const bool kw = !w.empty() && w.size() < raw.size() && std::isspace((unsigned char)raw[w.size()]);
        if (kw && w == "print") {
            std::string rest = trimWs(raw.substr(w.size()));
            std::string r = "\"\"";
            std::vector<std::string> ps = splitTop(rest);
            for (size_t k = 0; k < ps.size(); ++k) {
                std::string t = trim(ps[k]);
                if (t.empty()) fail("<<print …>> has an empty value (check the commas)", ln);
                if (k) r += " + \" \"";
                r += " + (" + applyVars(t) + ")";
            }
            return r;
        }
        if (kw && w == "run") {
            std::string rest = trimWs(raw.substr(w.size()));
            size_t lp = rest.find('(');
            std::string name = trimWs(lp == std::string::npos ? rest : rest.substr(0, lp));
            if (name.empty()) fail("<<run …>> needs a function: <<run report(n)>>", ln);
            for (char ch : name) if (!(isIdChar(ch) || ch == '.')) fail("<<run " + name + ">>: the function name must be a plain identifier", ln);
            std::string args;
            if (lp != std::string::npos) {
                size_t rp = rest.rfind(')');
                if (rp == std::string::npos || rp < lp || !trimWs(rest.substr(rp + 1)).empty())
                    fail("<<run " + name + "(…)>> is malformed: expected name(args)", ln);
                args = trim(rest.substr(lp + 1, rp - lp - 1));
            }
            return "indsin.run(\"" + name + "\"" + (args.empty() ? "" : ", " + applyVars(args)) + ")";
        }
        std::string e = applyVars(trim(raw));
        if (e.empty()) fail("empty <<>> bridge", ln);
        return e;
    }
    std::string bridgeValue() { int ln = line; std::string raw = readBridge(); return bridgeExpr(raw, ln); }

    // شرط <If>/<Else> (مشترك بين <If cond=…> و <<if …>>)
    void openCond(bool isElse, std::string c, int ln, const std::string& name, bool viaBridge) {
        std::string chain = c;
        if (isElse) {
            auto it = lastIf.find((int)stack.size());
            if (it == lastIf.end()) fail(viaBridge ? "<<else>> must directly follow an <<if>> branch" : "<Else> must directly follow an </If>", ln);
            std::string neg = "!(" + it->second + ")";
            chain = c.empty() ? it->second : "(" + it->second + ") or (" + c + ")";
            c = c.empty() ? neg : neg + " and (" + c + ")";
            lastIf.erase(it);
        }
        Frame fr{name, "", ln, chain, c};
        fr.br = viaBridge;
        stack.push_back(fr);
    }

    // آخر " as " على المستوى الأعلى في "items as it, i"
    static size_t findTopAs(const std::string& e) {
        size_t found = std::string::npos;
        int depth = 0;
        for (size_t k = 0; k < e.size(); ++k) {
            char c = e[k];
            if (c == '"' || c == '\'') { char q = c; ++k; while (k < e.size() && e[k] != q) { if (e[k] == '\\') ++k; ++k; } continue; }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; }
            else if (depth == 0 && std::isspace((unsigned char)c) && k + 3 < e.size() && e[k + 1] == 'a' && e[k + 2] == 's' && std::isspace((unsigned char)e[k + 3])) found = k;
        }
        return found;
    }

    // جسم <<each>> حتى <</each>> (يرفض التداخل). يترك i بعد <</each>>.
    std::string captureEachBody(int startLine) {
        for (size_t k = i; k + 1 < n; ++k) {
            if (!(s[k] == '<' && s[k + 1] == '<')) continue;
            size_t e = k + 2;
            while (e < n && std::isspace((unsigned char)s[e])) ++e;
            bool closing = e < n && s[e] == '/';
            if (closing) ++e;
            while (e < n && std::isspace((unsigned char)s[e])) ++e;
            size_t wb = e;
            while (e < n && isIdChar(s[e])) ++e;
            if (lower(s.substr(wb, e - wb)) != "each") continue;
            if (!closing) {
                int ln = line; for (size_t q = i; q < k; ++q) if (s[q] == '\n') ++ln;
                fail("nested <<each>> is not supported — move the inner loop into a <Define> component", ln);
            }
            size_t g = s.find(">>", e);
            if (g == std::string::npos) fail("malformed <</each>> (missing '>>')", startLine);
            std::string body = s.substr(i, k - i);
            for (size_t q = i; q < g + 2; ++q) if (s[q] == '\n') ++line;
            i = g + 2;
            return body;
        }
        fail("unclosed <<each>> (missing <</each>>)", startLine);
    }

    void handleEach(const std::string& rest, int startLine) {
        size_t asPos = findTopAs(rest);
        if (asPos == std::string::npos) fail("<<each LIST as item>> needs 'as': e.g. <<each users as u, i>>", startLine);
        std::string listText = trimWs(rest.substr(0, asPos));
        std::vector<std::string> names = splitTop(trimWs(rest.substr(asPos + 3)));
        if (listText.empty() || names.empty() || names.size() > 2) fail("<<each LIST as item[, index]>> is malformed", startLine);
        std::string itName = trimWs(names[0]), idxName = names.size() > 1 ? trimWs(names[1]) : "";
        auto okName = [](const std::string& nm) { if (nm.empty() || !isIdStart(nm[0])) return false; for (char ch : nm) if (!isIdChar(ch)) return false; return true; };
        if (!okName(itName) || (names.size() > 1 && !okName(idxName))) fail("<<each … as item, index>>: names must be plain identifiers", startLine);

        std::string body = captureEachBody(startLine);
        std::string le = applyVars(trim(listText));

        int count = -1;
        bool numeric = false, literal = false;
        std::vector<std::string> elems;
        if (le.size() >= 2 && le.front() == '[' && le.back() == ']') { // مصفوفة حرفية: الحجم معروف بلا تشغيل
            int d = 0; bool single = true;
            for (size_t k = 0; k < le.size(); ++k) {
                char c = le[k];
                if (c == '"' || c == '\'') { char q = c; ++k; while (k < le.size() && le[k] != q) { if (le[k] == '\\') ++k; ++k; } continue; }
                if (c == '[' || c == '(' || c == '{') ++d;
                else if (c == ']' || c == ')' || c == '}') { --d; if (d == 0 && k + 1 < le.size()) { single = false; break; } }
            }
            if (single) {
                literal = true;
                std::string inner = trimWs(le.substr(1, le.size() - 2));
                if (!inner.empty()) for (auto& e : splitTop(inner)) { std::string t = trimWs(e); if (!t.empty()) elems.push_back(t); }
                count = (int)elems.size();
            }
        }
        if (!literal && !le.empty() && le.find_first_not_of("0123456789") == std::string::npos && le.size() < 6) { numeric = true; count = std::stoi(le); }
        if (!literal && !numeric) {
            if (ctx.probe) {
                bool seen = false;
                for (auto& e : ctx.eachExprs) if (e == le) seen = true;
                if (!seen) ctx.eachExprs.push_back(le);
                emitNewlines(line - startLine);
                return;
            }
            auto f = ctx.eachCounts.find(le);
            if (f == ctx.eachCounts.end())
                fail("<<each " + le + " …>> needs the Rin runtime to know the list size (use it through the indsin pipeline/preview, "
                     "or give a literal list like [\"a\", \"b\"] or a number)", startLine);
            count = f->second.count;
            numeric = f->second.numeric;
        }
        if (count < 0) count = 0;
        if (count > 2000) fail("<<each>> list is too long (" + std::to_string(count) + " > 2000)", startLine);
        std::string text;
        for (int k = 0; k < count; ++k) {
            auto v = vars;
            v[itName] = literal ? "(" + elems[k] + ")" : numeric ? std::to_string(k) : "(" + le + ")[" + std::to_string(k) + "]";
            if (!idxName.empty()) v[idxName] = std::to_string(k);
            text += expandBody(body, v, idSuffix + "_" + std::to_string(k), "each", startLine, activeCond()) + " ";
        }
        put(text);
        emitNewlines(line - startLine);
    }

    // <<…>> في موضع عنصر/عبارة (بداية سطر أو بعد وسم)
    void handleBridge() {
        const int startLine = line;
        std::string raw = readBridge();
        const int consumed = line - startLine;
        std::string w = firstWord(raw);
        std::string rest = trimWs(raw.substr(w.size()));
        const bool kw = !w.empty() && (w.size() == raw.size() || std::isspace((unsigned char)raw[w.size()]));

        if (!raw.empty() && raw[0] == '/') { // إغلاق
            std::string nm = lower(trimWs(raw.substr(1)));
            if (nm == "each") fail("<</each>> without a matching <<each>>", startLine);
            if (nm != "if") fail("unknown closing <<" + raw + ">> (only <</if>> and <</each>> exist)", startLine);
            if (stack.empty()) fail("unexpected <</if>>", startLine);
            Frame f = stack.back();
            if (!f.br) fail("<</if>> does not match <" + f.tagName + "> opened at line " + std::to_string(f.line), startLine);
            stack.pop_back();
            lastIf[(int)stack.size()] = f.chain;
            emitNewlines(consumed);
            return;
        }
        if (kw && w == "if") {
            if (rest.empty()) fail("<<if>> needs a condition: <<if count > 3>>", startLine);
            openCond(false, applyVars(trim(rest)), startLine, "If", true);
            emitNewlines(consumed);
            return;
        }
        if (kw && w == "else") {
            std::string c;
            if (!rest.empty()) {
                if (firstWord(rest) != "if" || trimWs(rest.substr(2)).empty()) fail("<<else>> takes no condition — write <<else if cond>>", startLine);
                c = applyVars(trim(trimWs(rest.substr(2))));
            }
            if (stack.empty() || !stack.back().br) fail("<<else>> without a matching <<if>>", startLine);
            Frame f = stack.back();
            stack.pop_back();
            lastIf[(int)stack.size()] = f.chain;
            openCond(true, c, startLine, "Else", true);
            emitNewlines(consumed);
            return;
        }
        if (kw && w == "each") {
            if (rest.empty()) fail("<<each>> needs a list: <<each items as it>>", startLine);
            handleEach(rest, startLine);
            return;
        }
        if (kw && (w == "let" || w == "const" || w == "warp")) {
            std::string name = firstWord(rest);
            std::string after = trimWs(rest.substr(name.size()));
            if (name.empty() || !isIdStart(name[0]) || after.empty() || after[0] != '=' || (after.size() > 1 && after[1] == '='))
                fail("<<" + w + " name = value>> is malformed", startLine);
            std::string val = applyVars(trim(after.substr(1)));
            if (val.empty()) fail("<<" + w + " " + name + " = …>> needs a value", startLine);
            std::string stmt = w + " " + name + " = " + val + ";";
            if (w != "warp" && (!stack.empty() || flat)) ctx.tail += stmt + " "; // داخل عنصر: يُرفع إلى آخر الملف (الواجهة تُقيَّم بعد التشغيل)
            else put(stmt);
            emitNewlines(consumed);
            return;
        }
        if (kw && w == "import") {
            if (rest.empty()) fail("<<import \"file\">> needs a path", startLine);
            std::string q = (rest[0] == '"' || rest[0] == '\'') ? rest : "\"" + rest + "\"";
            put("@import " + q + ";");
            emitNewlines(consumed);
            return;
        }
        // قيمة وحدها: عنصر Text جاهز  (<<name>> / <<print …>> / <<run f()>>)
        std::string e = bridgeExpr(raw, startLine);
        std::string id = "text_" + std::to_string(++ctx.autoId) + idSuffix;
        std::string stmt = "@view.Text=" + id + " text=" + e + ";";
        std::string cnd = activeCond();
        if (!cnd.empty()) stmt += " visible=" + cnd + ";";
        put(stmt + " .end/view");
        emitNewlines(consumed);
    }

    // ═════════════ field ==> body · head.X · rin--g==> ═════════════
    // ثلاثة وسوم تُترجَم وقت الترجمة إلى وسوم/عناصر موجودة (لا مفاسّر جديدة): انظر docs/field-head-ring.md
    struct RItem {
        std::string it, value, shown, key, kind, label; // كلها نصوص Rin جاهزة (مقتبسة عند الحاجة)
        bool isField = false;
    };

    static std::string quoteRin(const std::string& v) {
        std::string q = "\"";
        for (char c : v) { if (c == '"' || c == '\\') q += '\\'; q += c; }
        return q + "\"";
    }
    static std::string unquoteRin(const std::string& v) {
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) return v.substr(1, v.size() - 2);
        return v;
    }
    // short_name / shortName → "Short name"
    static std::string humanize(const std::string& k) {
        std::string r;
        for (size_t p = 0; p < k.size(); ++p) {
            char c = k[p];
            if (c == '_' || c == '-') { if (!r.empty() && r.back() != ' ') r += ' '; continue; }
            if (p > 0 && std::isupper((unsigned char)c) && std::islower((unsigned char)k[p - 1])) { r += ' '; r += (char)std::tolower((unsigned char)c); continue; }
            r += c;
        }
        if (!r.empty()) r[0] = (char)std::toupper((unsigned char)r[0]);
        return r;
    }

    const ContainerInfo* findContainer(const std::string& name) const {
        for (auto& c : ctx.reg) if (c.name == name) return &c;
        return nullptr;
    }
    static std::string containerAtOffset(const std::vector<ContainerInfo>& reg, size_t off) {
        const ContainerInfo* best = nullptr;
        for (auto& c : reg) if (off >= c.begin && off < c.end && (!best || c.begin >= best->begin)) best = &c;
        return best ? best->name : "";
    }
    // الحاوية المحيطة بالموضع الحالي: وسم <Container#X> مفتوح، وإلا حاوية @container=X التي يقع الوسم داخلها.
    std::string enclosingContainer() const {
        for (size_t k = stack.size(); k-- > 0;) if (!stack[k].cname.empty()) return stack[k].cname;
        return ctx.curContainer;
    }

    // [ … ] متوازنة عند الموضع الحالي (على '[')؛ يعيد المحتوى مشذَّباً.
    std::string readBracket() {
        const int startLine = line;
        ++i;
        int depth = 1;
        std::string body;
        while (i < n) {
            char c = s[i];
            if (c == '"' || c == '\'') {
                char q = c; body += s[i++];
                while (i < n && s[i] != q) {
                    if (s[i] == '\\' && i + 1 < n) body += s[i++];
                    if (s[i] == '\n') ++line;
                    body += s[i++];
                }
                if (i < n) body += s[i++];
                continue;
            }
            if (c == '[') ++depth;
            if (c == ']' && --depth == 0) { ++i; return trimWs(body); }
            if (c == '\n') ++line;
            body += c; ++i;
        }
        fail("unterminated '[' (missing ']')", startLine);
    }

    // سمات وسم مخصّص حتى '>' أو '/>'؛ يقبل ';' قبلهما (<field.x[];>) ويسجّله في semi.
    void readCustomAttrs(const std::string& tagName, int startLine, std::vector<Attr>& attrs, bool& selfClose, bool& semi) {
        for (;;) {
            skipWs();
            if (i >= n) fail("unterminated tag <" + tagName + ">", startLine);
            if (s[i] == ';') { semi = true; ++i; continue; }
            if (s[i] == '>') { ++i; return; }
            if (s[i] == '/' && i + 1 < n && s[i + 1] == '>') { i += 2; selfClose = true; return; }
            if (!isIdStart(s[i])) fail(std::string("unexpected '") + s[i] + "' in tag <" + tagName + ">");
            size_t kb = i;
            while (i < n && (isIdChar(s[i]) || (s[i] == '-' && i + 1 < n && isIdChar(s[i + 1])))) ++i;
            Attr a; a.key = s.substr(kb, i - kb);
            for (auto& ch : a.key) if (ch == '-') ch = '_';
            size_t save = i; int saveLine = line;
            skipWs();
            if (i < n && s[i] == '=' && !(i + 1 < n && s[i + 1] == '>')) { ++i; skipWs(); a.value = readValue(); }
            else { i = save; line = saveLine; a.value = "true"; }
            attrs.push_back(std::move(a));
        }
    }
    static std::string attrOf(const std::vector<Attr>& attrs, const char* key) {
        for (auto& a : attrs) if (a.key == key) return a.value;
        return "";
    }
    static std::vector<std::string> csvOf(const std::string& v) {
        std::vector<std::string> r;
        std::string x = unquoteRin(v), cur;
        for (char c : x) { if (c == ',' || std::isspace((unsigned char)c)) { if (!cur.empty()) r.push_back(cur); cur.clear(); } else cur += c; }
        if (!cur.empty()) r.push_back(cur);
        return r;
    }

    // حقول حاوية → عناصر (key/value/shown/kind/label). sel = محتوى [ ] ('' = الكل؛ a,b = مختارة؛ !a = استثناء).
    std::vector<RItem> fieldItems(const std::string& cn, const std::string& sel, const std::string& tick,
                                  const std::vector<std::string>& kinds, int ln) {
        const ContainerInfo* ci = findContainer(cn);
        std::vector<std::string> pos, neg;
        for (auto& part : splitTop(sel)) {
            std::string t = trimWs(part);
            if (t.empty()) continue;
            if (t[0] == '!') neg.push_back(unquoteRin(trimWs(t.substr(1))));
            else pos.push_back(unquoteRin(t));
        }
        auto names = [&]() { std::string r; if (ci) for (auto& f : ci->fields) r += (r.empty() ? "" : ", ") + f.name; return r.empty() ? std::string("none") : r; };
        std::vector<FieldInfo> chosen;
        if (pos.empty()) {
            if (!ci) fail("no container named '" + cn + "' in this file — declare it (@container=" + cn + " / <Container#" + cn + ">) or list the fields: <field." + cn + "[a, b]>", ln);
            chosen = ci->fields;
        } else {
            for (auto& nm : pos) {
                const FieldInfo* f = ci ? ci->find(nm) : nullptr;
                if (ci && !f) fail("container '" + cn + "' has no field '" + nm + "' (fields: " + names() + ")", ln);
                chosen.push_back(f ? *f : FieldInfo{nm, "expr", ""});
            }
        }
        for (auto& nm : neg) {
            if (ci && !ci->find(nm)) fail("container '" + cn + "' has no field '" + nm + "' to exclude (fields: " + names() + ")", ln);
            for (size_t k = 0; k < chosen.size(); ++k) if (chosen[k].name == nm) { chosen.erase(chosen.begin() + (long)k); break; }
        }
        if (!kinds.empty()) {
            std::vector<FieldInfo> keep;
            for (auto& f : chosen) {
                bool ok = f.kind == "expr";
                for (auto& k : kinds) if (k == f.kind) ok = true;
                if (ok) keep.push_back(f);
            }
            chosen = keep;
        }
        if (chosen.empty()) fail("<field." + cn + "[…]> matched no fields" + (ci ? " (container fields: " + names() + ")" : ""), ln);
        if (!tick.empty() && !ctx.fieldGetDeclared) {
            ctx.fieldGetDeclared = true;
            ctx.tail += "fun fieldGet(name, key, tick) { return container.get(name, key); } ";
        }
        std::vector<RItem> r;
        for (auto& f : chosen) {
            RItem it;
            it.isField = true;
            it.key = quoteRin(f.name);
            it.value = tick.empty() ? "container.get(" + quoteRin(cn) + ", " + quoteRin(f.name) + ")"
                                    : "fieldGet(" + quoteRin(cn) + ", " + quoteRin(f.name) + ", " + tick + ")";
            it.shown = (f.kind == "list" || f.kind == "map") ? "jsonEncode(" + it.value + ")" : it.value;
            it.kind = quoteRin(f.kind);
            it.label = quoteRin(humanize(f.name));
            it.it = it.value;
            r.push_back(std::move(it));
        }
        return r;
    }

    // حجم قائمة Rin (حرفية/رقم/قياس تجريبي) لمصدر {expr}. يعيد false في تمريرة القياس (سُجِّل التعبير فقط).
    bool listInfo(const std::string& le, int ln, int& count, bool& numeric, bool& literal, std::vector<std::string>& elems) {
        count = -1; numeric = false; literal = false; elems.clear();
        if (le.size() >= 2 && le.front() == '[' && le.back() == ']') {
            int d = 0; bool single = true;
            for (size_t k = 0; k < le.size(); ++k) {
                char c = le[k];
                if (c == '"' || c == '\'') { char q = c; ++k; while (k < le.size() && le[k] != q) { if (le[k] == '\\') ++k; ++k; } continue; }
                if (c == '[' || c == '(' || c == '{') ++d;
                else if (c == ']' || c == ')' || c == '}') { --d; if (d == 0 && k + 1 < le.size()) { single = false; break; } }
            }
            if (single) {
                literal = true;
                std::string inner = trimWs(le.substr(1, le.size() - 2));
                if (!inner.empty()) for (auto& e : splitTop(inner)) { std::string t = trimWs(e); if (!t.empty()) elems.push_back(t); }
                count = (int)elems.size();
                return true;
            }
        }
        if (!le.empty() && le.find_first_not_of("0123456789") == std::string::npos && le.size() < 6) { numeric = true; count = std::stoi(le); return true; }
        if (ctx.probe) {
            bool seen = false;
            for (auto& e : ctx.eachExprs) if (e == le) seen = true;
            if (!seen) ctx.eachExprs.push_back(le);
            return false;
        }
        auto f = ctx.eachCounts.find(le);
        if (f == ctx.eachCounts.end())
            fail("<rin--g==>{" + le + "} needs the Rin runtime to know the list size (use it through the indsin pipeline/preview, "
                 "or give a literal list like [\"a\", \"b\"] or a number)", ln);
        count = f->second.count; numeric = f->second.numeric;
        return true;
    }

    // {{x}} → {x} (خارج علامات الاقتباس فقط): قالب rin--g بصيغة mustache.
    std::string normalizeMustache(const std::string& t) const {
        std::string r;
        size_t k = 0;
        while (k < t.size()) {
            char c = t[k];
            if (c == '"' || c == '\'') {
                char q = c; r += c; ++k;
                while (k < t.size() && t[k] != q) { if (t[k] == '\\' && k + 1 < t.size()) r += t[k++]; r += t[k++]; }
                if (k < t.size()) r += t[k++];
                continue;
            }
            if (c == '{' && k + 1 < t.size() && t[k + 1] == '{') {
                size_t b = k + 2; int d = 0; size_t e = b;
                while (e < t.size()) {
                    if (t[e] == '{') ++d;
                    else if (t[e] == '}') { if (d == 0 && e + 1 < t.size() && t[e + 1] == '}') break; if (d > 0) --d; }
                    ++e;
                }
                if (e >= t.size()) { r += c; ++k; continue; }
                r += "{" + t.substr(b, e - b) + "}";
                k = e + 2;
                continue;
            }
            r += c; ++k;
        }
        return r;
    }

    // يطبّق قالب الجسم على كل عنصر ويضيف النتيجة إلى المخرجات (الأسطر المستهلكة تُحفَظ).
    void renderItems(const std::vector<RItem>& items, const std::string& body, const std::string& kName, const std::string& vName,
                     bool ringVars, const std::string& what, const std::string& idTag, int startLine) {
        static const char* kFieldRow = "<Row#fld gap=8 padding=4><Text#fk size=small color=text_muted>{label}</Text><Spacer /><Text#fv>{shown}</Text></Row>";
        static const char* kPlainText = "<Text#itm>{shown}</Text>";
        std::string text;
        for (size_t k = 0; k < items.size(); ++k) {
            const RItem& it = items[k];
            auto v = vars;
            v[kName] = it.key; v[vName] = it.value;
            v["shown"] = it.shown; v["kind"] = it.kind; v["label"] = it.label; v["index"] = std::to_string(k);
            if (ringVars) { v["it"] = it.it; v["i"] = std::to_string(k); }
            std::string tag;
            if (it.isField) { tag = unquoteRin(it.key); for (auto& ch : tag) if (!isIdChar(ch)) ch = '_'; }
            else tag = std::to_string(k);
            const std::string& tpl = !body.empty() ? body : std::string(it.isField ? kFieldRow : kPlainText);
            text += expandBody(tpl, v, idSuffix + "_" + idTag + "_" + tag, what, startLine, activeCond()) + " ";
        }
        put(text);
        emitNewlines(line - startLine);
    }

    // ── <field.NAME[sel];> / <field[]> … </field> ─────────────────────────────────────────────
    //   حقول الحاوية NAME (أو المحيطة إن حُذف الاسم) تتدفّق إلى جسم: قالب افتراضي (سطر: اسم + قيمة) أو جسم مخصّص.
    void handleField() {
        const int startLine = line;
        i += 6; // "<field"
        std::string cname;
        if (i < n && s[i] == '.') {
            ++i; size_t b = i;
            while (i < n && isWordByte(s[i])) ++i;
            cname = s.substr(b, i - b);
            if (cname.empty()) fail("expected a container name after '<field.'", startLine);
        }
        std::string sel;
        if (i < n && s[i] == '[') sel = readBracket();
        std::vector<Attr> attrs; bool selfClose = false, semi = false;
        readCustomAttrs("field", startLine, attrs, selfClose, semi);
        for (auto& a : attrs) if (a.key != "tick" && a.key != "as" && a.key != "type" && a.key != "kind")
            fail("<field> has no attribute '" + a.key + "' (known: tick, as, type)", startLine);
        std::string cn = cname.empty() ? enclosingContainer() : cname;
        if (cn.empty()) fail("<field[]> has no container: write <field.NAME[]> or put it inside <Container#NAME> / @container=NAME", startLine);
        std::string kName = "key", vName = "value";
        std::vector<std::string> as = csvOf(attrOf(attrs, "as"));
        if (as.size() > 2) fail("<field as=\"k, v\"> takes at most two names", startLine);
        if (as.size() >= 1) kName = as[0];
        if (as.size() == 2) vName = as[1];
        std::vector<std::string> kinds = csvOf(!attrOf(attrs, "type").empty() ? attrOf(attrs, "type") : attrOf(attrs, "kind"));
        std::vector<RItem> items = fieldItems(cn, sel, attrOf(attrs, "tick"), kinds, startLine);
        std::string body;
        if (!selfClose && !semi) body = trimWs(captureBody("field", false, startLine));
        int use = ++ctx.fieldUse[cn];
        renderItems(items, body, kName, vName, false, "field", cn + (use > 1 ? "_" + std::to_string(use) : ""), startLine);
    }

    // ── <rin--g==>SRC & SRC & {{قالب}};> ─────────────────────────────────────────────────────
    //   SRC: [] سياق (حقول الحاوية المحيطة) · [a, "b", 3] قائمة حرفية · NAME[sel] حقول حاوية · {expr} قائمة/رقم من Rin
    //   & يضمّ المصادر بالترتيب، و{{ … }} (الأخير) قالب الجسم؛ فارغ = القالب الافتراضي.
    void handleRing() {
        const int startLine = line;
        i += 7; // "<rin--g"
        skipWs();
        if (s.compare(i, 3, "==>") != 0) fail("rin--g needs '==>' after its name: <rin--g==>[] & {{ … }};>", startLine);
        i += 3;
        std::vector<RItem> items;
        std::string tpl; bool haveTpl = false, probeSkip = false;
        for (;;) {
            skipWs();
            if (i >= n) fail("unterminated tag <rin--g>", startLine);
            if (haveTpl) fail("<rin--g>: the {{ template }} must be the last item (after '&')", startLine);
            if (s[i] == '{' && i + 1 < n && s[i + 1] == '{') {
                i += 2; int d = 0; std::string raw; bool closed = false;
                while (i < n) {
                    char c = s[i];
                    if (c == '"' || c == '\'') {
                        char q = c; raw += s[i++];
                        while (i < n && s[i] != q) { if (s[i] == '\\' && i + 1 < n) raw += s[i++]; if (s[i] == '\n') ++line; raw += s[i++]; }
                        if (i < n) raw += s[i++];
                        continue;
                    }
                    if (c == '{') ++d;
                    else if (c == '}') { if (d == 0 && i + 1 < n && s[i + 1] == '}') { i += 2; closed = true; break; } if (d > 0) --d; }
                    if (c == '\n') ++line;
                    raw += c; ++i;
                }
                if (!closed) fail("unterminated '{{' template (missing '}}')", startLine);
                tpl = trimWs(normalizeMustache(raw)); haveTpl = true;
            } else if (s[i] == '{' || std::isdigit((unsigned char)s[i])) { // {expr} أو رقم N: قائمة/عدد من Rin
                std::string e;
                if (s[i] == '{') e = readBraced();
                else { size_t b = i; while (i < n && std::isdigit((unsigned char)s[i])) ++i; e = s.substr(b, i - b); }
                if (e.empty()) fail("empty {} source in <rin--g>", startLine);
                int count; bool numeric, literal; std::vector<std::string> elems;
                if (!listInfo(e, startLine, count, numeric, literal, elems)) probeSkip = true;
                else {
                    if (count < 0) count = 0;
                    if (count > 2000) fail("<rin--g> list is too long (" + std::to_string(count) + " > 2000)", startLine);
                    for (int k = 0; k < count; ++k) {
                        RItem it;
                        it.it = literal ? "(" + elems[k] + ")" : numeric ? std::to_string(k) : "(" + e + ")[" + std::to_string(k) + "]";
                        it.value = it.shown = it.it;
                        it.key = quoteRin(std::to_string(k));
                        it.kind = quoteRin(literal ? inferKind(elems[k]) : numeric ? "number" : "expr");
                        it.label = literal ? quoteRin(humanize(unquoteRin(elems[k]))) : quoteRin("#" + std::to_string(k));
                        items.push_back(std::move(it));
                    }
                }
            } else if (s[i] == '[') { // [] سياق · [a, b, …] قائمة حرفية
                std::string inner = readBracket();
                if (inner.empty()) {
                    std::string cn = enclosingContainer();
                    if (cn.empty()) fail("<rin--g==>[]> has no list: put it inside <Container#NAME>, or write [a, b, c], NAME[] or {expr}", startLine);
                    for (auto& it : fieldItems(cn, "", "", {}, startLine)) items.push_back(std::move(it));
                } else {
                    for (auto& e : splitTop(inner)) {
                        std::string t = applyVars(trim(e));
                        if (t.empty()) continue;
                        RItem it;
                        it.it = it.value = it.shown = t;
                        it.key = quoteRin(std::to_string(items.size()));
                        it.kind = quoteRin(inferKind(t));
                        it.label = quoteRin(humanize(unquoteRin(t)));
                        items.push_back(std::move(it));
                    }
                }
            } else if (isWordByte(s[i])) { // NAME[sel] — حقول حاوية
                size_t b = i;
                while (i < n && isWordByte(s[i])) ++i;
                std::string cn = s.substr(b, i - b), sel;
                if (i < n && s[i] == '[') sel = readBracket();
                for (auto& it : fieldItems(cn, sel, "", {}, startLine)) items.push_back(std::move(it));
            } else fail(std::string("unexpected '") + s[i] + "' in <rin--g==>: expected [..] · NAME[..] · {expr} · {{template}}", startLine);
            skipWs();
            if (i < n && s[i] == '&') { ++i; continue; }
            break;
        }
        skipWs();
        if (i < n && s[i] == ';') { ++i; skipWs(); }
        if (i < n && s[i] == '>') ++i;
        else if (i + 1 < n && s[i] == '/' && s[i + 1] == '>') i += 2;
        else fail("<rin--g==> must end with '>' (e.g. …};>)", startLine);
        if (probeSkip) { emitNewlines(line - startLine); return; }
        if (items.empty()) fail("<rin--g==> has no items (empty list)", startLine);
        int use = ++ctx.ringUse;
        renderItems(items, tpl, "key", "value", true, "rin--g", "g" + std::to_string(use), startLine);
    }

    // ── <head.NAME …> : ربط هوية @container.app=NAME بعناصر التصميم ─────────────────────────
    static size_t editDistance(const std::string& a, const std::string& b) {
        std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
        for (size_t x = 1; x <= a.size(); ++x) {
            cur[0] = x;
            for (size_t j = 1; j <= b.size(); ++j)
                cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[x - 1] == b[j - 1] ? 0 : 1)});
            prev.swap(cur);
        }
        return prev[b.size()];
    }
    static const std::vector<std::string>& headFields() {
        static const std::vector<std::string> f = {"kind", "id", "name", "short_name", "shortName", "category", "version", "version_code", "versionCode",
            "channel", "author", "publisher", "license", "homepage", "lang", "dir", "description", "short_description", "shortDescription",
            "theme_color", "themeColor", "background_color", "backgroundColor", "display", "orientation", "start_url", "startUrl", "scope",
            "icon", "icons", "favicon", "seo", "locales", "releases", "version_parts", "container", "keywords", "canonical", "robots",
            "seo_title", "seo_description", "og_image"};
        return f;
    }
    // head.a.b → container.appInfo("APP")["a"]["b"] (خارج السلاسل). حقل غير معروف خطأ مع اقتراح.
    std::string headRewrite(const std::string& e) const {
        std::string r;
        size_t k = 0;
        const std::string base = "container.appInfo(" + quoteRin(ctx.headApp) + ")";
        while (k < e.size()) {
            char c = e[k];
            if (c == '"' || c == '\'') {
                size_t b = k++;
                while (k < e.size() && e[k] != c) { if (e[k] == '\\' && k + 1 < e.size()) ++k; ++k; }
                if (k < e.size()) ++k;
                r += e.substr(b, k - b);
            } else if (isIdStart(c)) {
                size_t b = k;
                while (k < e.size() && isIdChar(e[k])) ++k;
                std::string w = e.substr(b, k - b);
                bool member = b > 0 && e[b - 1] == '.';
                if (w != "head" || member) { r += w; continue; }
                std::vector<std::string> path;
                size_t q = k;
                while (q < e.size() && e[q] == '.') {
                    size_t p = q + 1, pb = p;
                    while (p < e.size() && isIdChar(e[p])) ++p;
                    if (p == pb) break;
                    path.push_back(e.substr(pb, p - pb));
                    q = p;
                }
                if (path.empty()) { r += base; continue; }
                bool known = false;
                for (auto& f : headFields()) if (f == path[0]) known = true;
                if (!known) {
                    std::string best; size_t bd = 3;
                    for (auto& f : headFields()) { size_t d = editDistance(path[0], f); if (d < bd) { bd = d; best = f; } }
                    fail("unknown head field 'head." + path[0] + "'" + (best.empty() ? "" : " — did you mean 'head." + best + "'?") +
                         " (fields of @container.app=" + ctx.headApp + ": name, version, description, icon, theme_color, lang, dir, seo.title …)");
                }
                std::string x = base;
                for (auto& p : path) x += "[" + quoteRin(p) + "]";
                r += x;
                k = q;
            } else { r += c; ++k; }
        }
        return r;
    }

    void handleHead() {
        const int startLine = line;
        i += 5; // "<head"
        if (!(i < n && s[i] == '.')) fail("<head> needs the app container: <head.name_app>", startLine);
        ++i;
        size_t b = i;
        while (i < n && isWordByte(s[i])) ++i;
        const std::string app = s.substr(b, i - b);
        if (app.empty()) fail("expected a container name after '<head.'", startLine);
        std::string id = "head_" + app;
        if (i < n && s[i] == '#') { ++i; size_t ib = i; while (i < n && isIdChar(s[i])) ++i; id = s.substr(ib, i - ib); if (id.empty()) fail("expected a name after '#'", startLine); }
        std::vector<Attr> attrs; bool selfClose = false, semi = false;
        readCustomAttrs("head." + app, startLine, attrs, selfClose, semi);
        const bool block = !selfClose && !semi;
        const bool show = !attrOf(attrs, "show").empty();
        const bool dark = !attrOf(attrs, "dark").empty(), light = !attrOf(attrs, "light").empty();
        const bool extern_ = !attrOf(attrs, "extern").empty();
        std::vector<std::string> apply = csvOf(attrOf(attrs, "apply"));
        std::vector<Attr> passthru;
        for (auto& a : attrs) if (a.key != "show" && a.key != "dark" && a.key != "light" && a.key != "extern" && a.key != "apply") passthru.push_back(a);
        if (!passthru.empty() && !show && !block)
            fail("<head." + app + "> has no element to apply '" + passthru[0].key + "' to — add `show` or give it a body (<head." + app + "> … </head>)", startLine);

        const ContainerInfo* ci = findContainer(app);
        if (!ci && !extern_) fail("no @container.app=" + app + " in this file for <head." + app + "> (add `extern` if it is declared in an imported file)", startLine);
        if (ci && !ci->tagForm && ci->kind != "app" && ci->kind != "site")
            fail("<head." + app + "> needs an app/site container, but '" + app + "' is container." + ci->kind + " (use @container.app=" + app + ")", startLine);
        ctx.headApp = app;

        std::string text;
        for (auto& a : apply) {
            if (a == "theme") {
                const FieldInfo* f = ci ? ci->find("theme_color") : nullptr;
                std::string hex = f ? unquoteRin(trimWs(f->raw)) : "";
                if (!f || f->kind != "string" || hex.size() < 4 || hex[0] != '#')
                    fail("<head." + app + " apply=theme> needs a literal theme_color=\"#RRGGBB\" inside @container.app=" + app, startLine);
                text += expandBody("<Palette#" + id + "_theme seed=" + hex + (dark ? " dark" : "") + (light ? " light" : "") + " />", {}, idSuffix, "head", startLine, activeCond()) + " ";
            } else if (a == "tokens") {
                static const char* fs[] = {"version", "description", "theme_color", "background_color", "lang", "dir", "icon"};
                // name وshort_name مشتقّان دائماً؛ الباقي فقط إن عُرِّف في الحاوية (لا مفاتيح ناقصة وقت التشغيل)
                std::vector<std::string> have = {"name", "short_name"};
                for (const char* f : fs) if (!ci || ci->find(f)) have.push_back(f);
                for (auto& f : have) ctx.tail += "let head_" + f + " = container.appInfo(" + quoteRin(app) + ")[\"" + f + "\"]; ";
            } else fail("<head." + app + " apply=\"" + a + "\"> is unknown (use theme, tokens)", startLine);
        }
        auto attrText = [&]() { std::string t; for (auto& a : passthru) t += " " + a.key + "=" + a.value; return t; };
        if (show) {
            if (block) fail("<head." + app + "> has both `show` and a body — pick one", startLine);
            std::string t = "<Row#" + id + attrText() + (attrOf(passthru, "gap").empty() ? " gap=12" : "") + (attrOf(passthru, "padding").empty() ? " padding=12" : "") + ">";
            if (ci && ci->find("icon")) t += "<Image#" + id + "_icon src={head.icon} width=40 height=40 />";
            t += "<Column#" + id + "_txt gap=2><Text#" + id + "_name size=title>{head.name}</Text>";
            if (ci && ci->find("version")) t += "<Text#" + id + "_ver size=small color=text_muted>v{head.version}</Text>";
            t += "</Column></Row>";
            text += expandBody(t, {}, idSuffix, "head", startLine, activeCond()) + " ";
        }
        if (!text.empty()) put(text);
        if (block) { // <head.X> … </head>: الأبناء داخل صف ترويسة (Row افتراضياً أو as=Column)
            std::string kind = attrOf(passthru, "as"); kind = kind.empty() ? "Row" : unquoteRin(kind);
            std::string st = "@view." + kind + "=" + nameToken(id + idSuffix);
            for (auto& a : passthru) if (a.key != "as") st += " " + a.key + "=" + a.value + ";";
            put(st);
            Frame fr{"head." + app, ".end/view", startLine, "", ""};
            stack.push_back(fr);
        }
        emitNewlines(line - startLine);
    }

    void handleTag() {
        if (!flat && !ctx.reg.empty()) ctx.curContainer = containerAtOffset(ctx.reg, i);
        if (i + 1 < n && s[i] == '<' && s[i + 1] == '<') { handleBridge(); return; } // << … >>
        const int startLine = line;
        const size_t startPos = i;

        // تعليق HTML
        if (s.compare(i, 4, "<!--") == 0) {
            size_t e = s.find("-->", i + 4);
            if (e == std::string::npos) fail("unterminated <!-- comment");
            std::string body = s.substr(i + 4, e - (i + 4));
            if (!flat) out += "//";
            for (char c : body) { if (c == '\n') { if (!flat) out += "\n//"; ++line; } else if (!flat) out += c; }
            i = e + 3;
            return;
        }

        // Fragment افتتاحي
        if (s.compare(i, 2, "<>") == 0) {
            i += 2;
            stack.push_back({"", "", startLine, "", ""});
            return;
        }

        // إغلاق
        if (s[i + 1] == '/') {
            i += 2;
            size_t b = i;
            while (i < n && (isIdChar(s[i]) || s[i] == '.')) ++i;
            std::string nm = s.substr(b, i - b);
            skipWs();
            if (i >= n || s[i] != '>') fail("malformed closing tag");
            ++i;
            if (stack.empty()) fail("unexpected closing tag </" + nm + ">", startLine);
            Frame f = stack.back();
            if (f.br) fail("<<" + lower(f.tagName) + ">> opened at line " + std::to_string(f.line) + " is not closed: it needs <</if>> before </" + nm + ">", f.line);
            const bool headClose = lower(nm) == "head" && lower(f.tagName).compare(0, 5, "head.") == 0; // </head> يغلق <head.X>
            if (!nm.empty() && lower(nm) != lower(f.tagName) && !headClose)
                fail("closing tag </" + nm + "> does not match <" + f.tagName + "> opened at line " + std::to_string(f.line), startLine);
            if (nm.empty() && !f.tagName.empty())
                fail("'</>' closes a fragment, but <" + f.tagName + "> is open (line " + std::to_string(f.line) + ")", startLine);
            stack.pop_back();
            if (lower(f.tagName) == "if" || lower(f.tagName) == "else") lastIf[(int)stack.size()] = f.chain;
            if (!f.closer.empty()) put(f.closer);
            return;
        }

        // field ==> body · head.X · rin--g==>  (حروف صغيرة فقط؛ <Field> الجاهز و<Head…> لا يتأثران)
        {
            auto delim = [&](size_t p) { return p >= n || s[p] == '.' || s[p] == '[' || s[p] == '>' || s[p] == '/' || s[p] == ';' || std::isspace((unsigned char)s[p]); };
            if (s.compare(i, 6, "<field") == 0 && delim(i + 6) && !ctx.macros.count("field")) { handleField(); return; }
            if (s.compare(i, 5, "<head") == 0 && delim(i + 5) && !ctx.macros.count("head")) { handleHead(); return; }
            if (s.compare(i, 7, "<rin--g") == 0 && (delim(i + 7) || s[i + 7] == '=') && !ctx.macros.count("rin--g")) { handleRing(); return; }
        }
        // افتتاح
        ++i; // <
        size_t b = i;
        while (i < n && (isIdChar(s[i]) || s[i] == '.')) ++i;
        std::string tagName = s.substr(b, i - b);
        std::string id;
        if (i < n && s[i] == '#') {
            ++i; size_t ib = i;
            while (i < n && isIdChar(s[i])) ++i;
            id = s.substr(ib, i - ib);
            if (id.empty()) fail("expected a name after '#'");
        }

        std::vector<Attr> attrs;
        bool selfClose = false;
        int nlInside = 0;
        for (;;) {
            nlInside += skipWs();
            if (i >= n) fail("unterminated tag <" + tagName + ">", startLine);
            if (s[i] == '>') { ++i; break; }
            if (s[i] == '/' && i + 1 < n && s[i + 1] == '>') { i += 2; selfClose = true; break; }
            if (!isIdStart(s[i])) fail(std::string("unexpected '") + s[i] + "' in tag <" + tagName + ">");
            size_t kb = i;
            // kebab-case مسموح في أسماء الخصائص (min-width / row-gap / flex-direction) ويُحوَّل إلى snake_case.
            while (i < n && (isIdChar(s[i]) || (s[i] == '-' && i + 1 < n && isIdChar(s[i + 1])))) ++i;
            Attr a; a.key = s.substr(kb, i - kb);
            for (auto& ch : a.key) if (ch == '-') ch = '_';
            size_t save = i; int saveLine = line;
            nlInside += skipWs();
            if (i < n && s[i] == '=') {
                ++i; nlInside += skipWs();
                int before = line;
                a.value = readValue();
                nlInside += (line - before);
            } else { i = save; line = saveLine; nlInside += 0; a.value = "true"; }
            attrs.push_back(std::move(a));
        }
        // الأسطر المستهلكة داخل الوسم (قبل أي محتوى نصي)
        int consumed = line - startLine;
        (void)nlInside;

        // ── مفاهيم متقدمة: Define / Repeat / If / Else / Fun / Rin / استعمال القوالب ──
        {
            const std::string tl = lower(tagName);
            auto attrVal = [&](std::initializer_list<const char*> keys) -> std::string {
                for (auto& a : attrs) for (auto k : keys) if (a.key == k) return a.value;
                return "";
            };
            auto finish = [&](const std::string& text) {
                int consumed2 = line - startLine;
                out += text;
                emitNewlines(consumed2);
            };
            // <Tokens brand=#6C5CE7 gap=16 />  →  let brand = "#6C5CE7"; let gap = 16;   (ثوابت تصميم تُستعمل كـ {brand})
            if (tl == "tokens") {
                if (!selfClose) fail("<Tokens> must be self-closing: <Tokens name=value … />", startLine);
                if (attrs.empty()) fail("<Tokens> needs at least one name=value", startLine);
                std::string lets;
                for (auto& a : attrs) lets += "let " + a.key + " = " + a.value + "; ";
                finish(lets);
                return;
            }
            // <Style#card radius=16 bg=#fff elevation=md [extends="base"] />  — نمط مسمّى؛ يُطبَّق بـ class="card pill"
            if (tl == "style") {
                if (!selfClose) fail("<Style#name …> must be self-closing", startLine);
                if (id.empty()) fail("<Style> needs a name: <Style#card radius=16 />", startLine);
                std::vector<Attr> merged;
                auto put1 = [&](const Attr& a) {
                    for (auto& m : merged) if (m.key == a.key) { m.value = a.value; return; }
                    merged.push_back(a);
                };
                std::string ext = attrVal({"extends"});
                if (ext.size() >= 2 && (ext.front() == '"' || ext.front() == '\'')) ext = ext.substr(1, ext.size() - 2);
                for (size_t p = 0; p < ext.size();) {
                    size_t e = ext.find_first_of(" ,", p); if (e == std::string::npos) e = ext.size();
                    std::string nm = ext.substr(p, e - p); p = e + 1;
                    if (nm.empty()) continue;
                    auto base = ctx.styles.find(nm);
                    if (base == ctx.styles.end()) fail("<Style#" + id + "> extends unknown style '" + nm + "' (declare it first)", startLine);
                    for (auto& a : base->second) put1(a);
                }
                for (auto& a : attrs) if (a.key != "extends") put1(a);
                ctx.styles[id] = std::move(merged);
                finish("");
                return;
            }
            // class="card pill": يدمج سمات الأنماط (اللاحق يغلب السابق) تحت سمات العنصر الصريحة، ثم تُحذف class.
            if (tl != "define" && tl != "repeat" && tl != "if" && tl != "else" && tl != "fun" && tl != "rin" && !ctx.macros.count(tagName)) {
                for (size_t ai = 0; ai < attrs.size(); ai++) {
                    if (attrs[ai].key != "class") continue;
                    std::string cv = attrs[ai].value;
                    if (cv.size() >= 2 && (cv.front() == '"' || cv.front() == '\'')) cv = cv.substr(1, cv.size() - 2);
                    attrs.erase(attrs.begin() + ai);
                    std::vector<Attr> fromStyles;
                    for (size_t p = 0; p < cv.size();) {
                        size_t e = cv.find_first_of(" ,", p); if (e == std::string::npos) e = cv.size();
                        std::string nm = cv.substr(p, e - p); p = e + 1;
                        if (nm.empty()) continue;
                        auto st = ctx.styles.find(nm);
                        if (st == ctx.styles.end()) fail("unknown style '" + nm + "' in class= (declare <Style#" + nm + " … /> first)", startLine);
                        for (auto& a : st->second) {
                            bool replaced = false;
                            for (auto& f : fromStyles) if (f.key == a.key) { f.value = a.value; replaced = true; }
                            if (!replaced) fromStyles.push_back(a);
                        }
                    }
                    for (auto& f : fromStyles) {
                        bool explicitSet = false;
                        for (auto& a : attrs) if (a.key == f.key) explicitSet = true;
                        if (!explicitSet) attrs.push_back(f);
                    }
                    break;
                }
            }
            if (tl == "define") {
                if (selfClose) fail("<Define#Name> needs a body and </Define>", startLine);
                if (id.empty()) fail("<Define> needs a name: <Define#Chip label=\"x\">", startLine);
                Macro m;
                for (auto& a : attrs) m.props.push_back({a.key, a.value});
                m.body = captureBody("Define", true, startLine);
                ctx.macros[id] = std::move(m);
                finish("");
                return;
            }
            if (tl == "repeat") {
                if (selfClose) fail("<Repeat times=N> needs a body and </Repeat>", startLine);
                std::string tv = attrVal({"times", "count"});
                if (tv.empty()) fail("<Repeat> needs times=N", startLine);
                int times = intAttr(tv, "<Repeat times>", startLine);
                if (times < 0 || times > 500) fail("<Repeat times> must be between 0 and 500", startLine);
                std::string as = attrVal({"as"});
                if (as.size() >= 2 && as.front() == '"') as = as.substr(1, as.size() - 2);
                if (as.empty()) as = "i";
                std::string sv = attrVal({"start"});
                int start = sv.empty() ? 0 : intAttr(sv, "<Repeat start>", startLine);
                std::string body = captureBody("Repeat", true, startLine);
                std::string text;
                for (int k = 0; k < times; ++k) {
                    auto v = vars;
                    v[as] = std::to_string(start + k);
                    text += expandBody(body, v, idSuffix + "_" + std::to_string(k), "Repeat", startLine, activeCond()) + " ";
                }
                finish(text);
                return;
            }
            if (tl == "if" || tl == "else") {
                if (selfClose) fail("<" + tagName + "> needs a body", startLine);
                std::string c = attrVal({"cond", "when", "test"});
                if (tl == "if" && c.empty()) fail("<If> needs cond={expression}", startLine);
                if (!c.empty() && c.front() == '"' && c.back() == '"') c = c.substr(1, c.size() - 2);
                openCond(tl == "else", c, startLine, tagName, false);
                finish("");
                return;
            }
            if (tl == "fun" || tl == "rin") {
                if (selfClose) fail("<" + tagName + "> needs a Rin body and </" + tagName + ">", startLine);
                int openLines = line - startLine;
                std::string text;
                if (tl == "fun") {
                    if (id.empty()) fail("<Fun#name params=\"a,b\"> needs a name", startLine);
                    std::string ps = attrVal({"params", "args"});
                    if (ps.size() >= 2 && ps.front() == '"') ps = ps.substr(1, ps.size() - 2);
                    std::string pl; // "a,b" → "a, b"
                    for (char ch : ps) { if (ch == ',') pl += ", "; else if (!std::isspace((unsigned char)ch)) pl += ch; }
                    text = "fun " + id + "(" + pl + ") {";
                }
                std::string body = captureBody(tagName, false, startLine);
                out += text;
                emitNewlines(openLines);
                out += body;
                if (tl == "fun") out += "}";
                return;
            }
            if (tl == "slot") fail("<Slot /> is only valid inside <Define>", startLine);
            auto mit = ctx.macros.find(tagName);
            if (mit != ctx.macros.end()) {
                std::string body = mit->second.body;
                std::string children;
                if (!selfClose) children = captureBody(tagName, true, startLine);
                // <Slot /> → أبناء الاستعمال
                for (size_t sp; (sp = body.find("<Slot")) != std::string::npos;) {
                    size_t e = body.find("/>", sp);
                    if (e == std::string::npos) fail("<Slot> must be self-closing: <Slot />", startLine);
                    body.replace(sp, e + 2 - sp, children);
                }
                std::map<std::string, std::string> v = vars;
                for (auto& pr : mit->second.props) v[pr.first] = pr.second;
                for (auto& a : attrs) {
                    bool known = false;
                    for (auto& pr : mit->second.props) if (pr.first == a.key) known = true;
                    if (!known) fail("<" + tagName + "> has no property '" + a.key + "' (declare it on <Define#" + tagName + ">)", startLine);
                    v[a.key] = a.value;
                }
                std::string suffix = idSuffix + "_" + (id.empty() ? std::to_string(++ctx.autoId) : id);
                std::string text = expandBody(body, v, suffix, tagName, startLine, activeCond());
                finish(text + " ");
                return;
            }
            // حقن شروط <If>/<Else> على العناصر المرسومة
            std::string cnd = activeCond();
            if (!cnd.empty() && tl != "warp" && tl != "import" && tl != "loop" && tl != "container") {
                bool had = false;
                for (auto& a : attrs) if (a.key == "visible") { a.value = "(" + cnd + ") and (" + a.value + ")"; had = true; }
                if (!had) attrs.push_back({"visible", cnd});
            }
        }

        const std::string rawId = id;
        if (!id.empty()) id += idSuffix; // عناصر القوالب/التكرار: أسماء فريدة لكل نسخة
        // تصنيف الوسم
        std::string head = tagName, sub;
        size_t dot = tagName.find('.');
        if (dot != std::string::npos) { head = tagName.substr(0, dot); sub = tagName.substr(dot + 1); }
        std::string hl = lower(head);

        std::string kw, closer, stmt;
        bool isStatement = false; // warp/import: عبارة قائمة بذاتها بلا إغلاق

        if (hl == "warp") {
            if (!selfClose) fail("<warp> must be self-closing: <warp name={value} />", startLine);
            for (auto& a : attrs) stmt += "warp " + a.key + " = " + a.value + "; ";
            if (attrs.empty()) fail("<warp> needs at least one name={value}", startLine);
            isStatement = true;
        } else if (hl == "import") {
            if (!selfClose) fail("<Import> must be self-closing: <Import src=\"file\" />", startLine);
            std::string src;
            for (auto& a : attrs) if (a.key == "src" || a.key == "path" || a.key == "from") src = a.value;
            if (src.empty() && !attrs.empty()) src = attrs[0].value;
            if (src.empty()) fail("<Import> needs src=\"…\"", startLine);
            stmt = "@import " + src + ";";
            isStatement = true;
        } else if (hl == "loop" || hl == "container") {
            kw = hl; closer = ".end/" + hl;
            if (id.empty()) id = hl + "_" + std::to_string(++ctx.autoId);
            stmt = "@" + hl + "=" + nameToken(id);
        } else if (hl == "theme" || hl == "palette") {
            // <Theme#Brand primary=#6C5CE7 background=#0B1020 active />  →  @theme=Brand …; .end/theme
            // <Palette seed=#6C5CE7 dark />  → theme مشتقّ من لون واحد (WCAG)، يُفعَّل تلقائياً.
            if (!selfClose) fail(std::string("<") + tagName + "> must be self-closing: <" + tagName + " … />", startLine);
            if (id.empty()) id = (hl == "palette" ? "palette_" : "theme_") + std::to_string(++ctx.autoId);
            bool hasActive = false;
            for (auto& a : attrs) {
                if (a.key == "active") hasActive = true;
                if (a.key == "light") { a.key = "mode"; a.value = "\"light\""; }
            }
            if (hl == "palette") {
                bool hasSeed = false;
                for (auto& a : attrs) if (a.key == "seed") hasSeed = true;
                if (!hasSeed) fail("<Palette> needs seed=#RRGGBB (one color → a full theme)", startLine);
                if (!hasActive) attrs.push_back({"active", "true"});
            }
            kw = "theme"; closer = ".end/theme";
            stmt = "@theme=" + id;
        } else if (hl == "effect") {
            // <Effect#wobble duration=900 iterations=infinite> <Keyframe at=50% x=6 /> … </Effect>  →  @effect=wobble …; .end/effect
            // أو مختصراً: <Effect#fadeIn frames="from{opacity:0} to{opacity:1}" duration=400 />  (Effects Studio)
            if (id.empty()) fail("<Effect#name …> needs a name, e.g. <Effect#wobble duration=900>", startLine);
            kw = "effect"; closer = ".end/effect";
            stmt = "@effect=" + id;
        } else if (hl == "font") {
            // <Font#Brand src="fonts/brand.woff2" weight="100 900" display=swap />  →  @font=Brand …; .end/font   (CustomFont/FontLoad)
            if (id.empty()) fail("<Font#Family …> needs a family name, e.g. <Font#Brand src=\"fonts/brand.woff2\" />", startLine);
            kw = "font"; closer = ".end/font";
            stmt = "@font=" + id;
        } else if (hl == "keyframe") {
            // <Keyframe at=50% x=6 opacity=.8 />  →  at50="x:6,opacity:.8";   (قيم {expr} تُقيَّم وقت التسجيل)
            if (!selfClose) fail("<Keyframe> must be self-closing: <Keyframe at=50% y=-8 />", startLine);
            std::string at, key;
            for (auto& a : attrs) if (a.key == "at" || a.key == "t") at = a.value;
            if (at.empty()) fail("<Keyframe> needs at=0%..100% (or at=from / at=to)", startLine);
            std::string av = at;
            if (av.size() >= 2 && av.front() == '"' && av.back() == '"') av = av.substr(1, av.size() - 2);
            if (!av.empty() && av.back() == '%') av.pop_back();
            for (auto& c : av) if (c == '.') c = '_';
            std::string lv = lower(av);
            key = (lv == "from" || lv == "to") ? lv : "at" + av;
            std::string expr; bool firstDecl = true;
            for (auto& a : attrs) {
                if (a.key == "at" || a.key == "t") continue;
                expr += (expr.empty() ? "" : " + ") + std::string("\"") + (firstDecl ? "" : ",") + a.key + ":\"";
                firstDecl = false;
                bool lit = a.value.size() >= 2 && a.value.front() == '"' && a.value.back() == '"';
                expr += " + " + (lit ? a.value : "(" + a.value + ")");
            }
            if (expr.empty()) expr = "\"\"";
            stmt = key + "=" + expr + ";";
            isStatement = true;
        } else if (hl == "element") {
            if (sub.empty()) fail("<Element.Kind#name> needs a kind, e.g. <Element.Button#ok>", startLine);
            kw = "element"; closer = ".end/element";
            if (id.empty()) id = lower(sub) + "_" + std::to_string(++ctx.autoId);
            stmt = "@element." + sub + "=" + nameToken(id);
        } else {
            std::string kind = hl == "view" ? sub : tagName;
            // <Screen size=phone|"390x844" width=… height=… bg=#fff>: جذر الشاشة = Column + لوحة (canvas) بحجم ولون حرّين.
            //   size→screen · width→canvas_width · height→canvas_height (حدّ أدنى) · bg/background→canvas (لون خلفية الشاشة)
            if (hl == "screen") {
                kind = "Column";
                for (auto& a : attrs) {
                    if (a.key == "size") a.key = "screen";
                    else if (a.key == "width") a.key = "canvas_width";
                    else if (a.key == "height") a.key = "canvas_height";
                    else if (a.key == "bg" || a.key == "background") a.key = "canvas";
                }
            }
            if (hl == "view" && sub.empty()) fail("<View.Kind#name> needs a kind", startLine);
            if (kind.find('.') != std::string::npos) fail("unexpected '.' in element name <" + tagName + ">", startLine);
            kw = "view"; closer = ".end/view";
            if (id.empty()) id = lower(kind) + "_" + std::to_string(++ctx.autoId);
            stmt = "@view." + kind + "=" + nameToken(id);
        }

        if (!isStatement) {
            for (auto& a : attrs) stmt += " " + a.key + "=" + a.value + ";";
        }

        // نص كابن
        if (!selfClose && !isStatement) {
            const char* tattr = textAttrFor(lower(hl == "view" ? sub : tagName));
            if (tattr) {
                std::string expr;
                int beforeLine = line;
                if (tryTextContent(tagName, expr)) {
                    for (auto& a : attrs) if (a.key == tattr)
                        fail(std::string("<") + tagName + "> has both a '" + tattr + "' attribute and text content", startLine);
                    stmt += std::string(" ") + tattr + "=" + expr + ";";
                    consumed += line - beforeLine;
                    put(stmt + " " + closer);
                    emitNewlines(consumed);
                    return;
                }
            }
        }

        if (selfClose || isStatement) {
            put(stmt);
            if (!isStatement) out += " " + closer;
        } else {
            put(stmt);
            Frame fr{tagName, closer, startLine, "", ""};
            if (hl == "container") fr.cname = rawId;
            stack.push_back(fr);
        }
        emitNewlines(consumed);
        (void)startPos;
    }
};

} // namespace detail

// يحوّل صيغة الوسوم إلى لهجة @view القديمة. يرمي TagError عند خطأ في الوسوم.
// ── مكتبة المكوّنات الجاهزة (built-in components) ─────────────────────────────────────────────
// مكوّنات مركّبة تُعرَّف بـ <Define> وتتوسّع وقت الترجمة إلى عناصر indsin الموجودة (تعمل في كل المضيفين بلا تغيير).
// تُحقَن في أول المصدر *فقط* إن استُعمل أحدها، وبلا أي سطر جديد (فتبقى أرقام الأسطر صحيحة، ويبقى المصدر
// بلا مكوّنات مطابقاً حرفياً). تعريف المستخدم لنفس الاسم (<Define#Chip …>) يحلّ محلّها.
inline const char* builtinPrelude() {
    return
        // Chip: حبّة صغيرة ملوّنة (داخل Row كي تلتفّ حول محتواها ولا تتمدّد)
        "<Define#Chip label=\"Chip\" tint=primary><Row#cw><Card#chip padding=8 radius=14 bg={tint}><Text#t size=small color=#ffffff>{label}</Text></Card></Row></Define>"
        // Stat: بطاقة رقم + عنوان (w=30% داخل Row)
        "<Define#Stat label=\"Stat\" value=\"0\" tint=primary w=full><Card#s width={w} padding=12 radius=16 bg={tint} elevation=md><Text#v size=heading color=#ffffff>{value}</Text><Text#l size=small color=#ffffffcc>{label}</Text></Card></Define>"
        // Alert: تنبيه بدور ثيم (info|success|warning|danger)
        "<Define#Alert tone=info title=\"Notice\" message=\"\"><Card#a padding=12 radius=12 gap=4 bg={tone}><Text#t size=title color=#ffffff>{title}</Text><Text#m size=small color=#ffffffdd visible={message != \"\"}>{message}</Text><Slot /></Card></Define>"
        // Section: عنوان + أبناء
        "<Define#Section title=\"Section\"><Column#sec gap=8><Text#t size=title>{title}</Text><Slot /></Column></Define>"
        // ListTile: عنوان + وصف + نص جانبي
        "<Define#ListTile title=\"Title\" subtitle=\"\" trailing=\"\"><Row#tile gap=12 padding=8><Column#c gap=2><Text#t>{title}</Text><Text#s size=small color=text_muted visible={subtitle != \"\"}>{subtitle}</Text></Column><Spacer /><Text#r color=text_muted>{trailing}</Text></Row></Define>"
        // Hero: بطاقة بتدرّج وتوهّج
        "<Define#Hero title=\"Title\" subtitle=\"\" grad=\"#6C5CE7,#A29BFE,#FD79A8,35\"><Card#h width=full padding=18 radius=22 gap=4 elevation=lg gradient={grad}><Text#t size=heading color=#ffffff>{title}</Text><Text#s color=#ffffffdd visible={subtitle != \"\"}>{subtitle}</Text><Slot /></Card></Define>"
        // EmptyState: حالة فارغة
        "<Define#EmptyState title=\"Nothing here\" message=\"\"><Column#e align=center gap=6 padding=24><Text#t size=title>{title}</Text><Text#m size=small color=text_muted>{message}</Text><Slot /></Column></Define>"
        // Toggle: سطر بمفتاح تشغيل مرتبط بخلية warp: <Toggle label="…" cell={flag} />
        "<Define#Toggle label=\"Option\" cell=false><Row#tg gap=12 padding=6><Text#t>{label}</Text><Spacer /><Switch#sw checked={cell} onTap={toggle(cell)} /></Row></Define>"
        // ---- التنقّل والهيكل: TopBar · Header · NavBar · Breadcrumb · SearchBar · Pagination · Footer · Fab ----
        // TopBar: شريط علوي: رجوع اختياري + عنوان + إجراء اختياري
        "<Define#TopBar title=\"Title\" back=false action=\"\" onBack=noop onAction=noop><Row#tb width=full padding=10 gap=8 bg=surface elevation=sm><Button#tbb variant=ghost visible={back} onTap={onBack}>Back</Button><Text#tbt size=title>{title}</Text><Spacer /><Button#tba variant=ghost visible={action != \"\"} onTap={onAction}>{action}</Button></Row></Define>"
        // Header: ترويسة صفحة: عنوان كبير + وصف + أبناء
        "<Define#Header title=\"Title\" subtitle=\"\"><Column#hd width=full padding=8 gap=2><Text#hdt size=heading>{title}</Text><Text#hds size=small color=text_muted visible={subtitle != \"\"}>{subtitle}</Text><Slot /></Column></Define>"
        // NavBar: شريط تنقّل سفلي حتى 4 عناصر مرتبط بخلية رقمية: <NavBar tab={t} a="Home" b="Search" c="Inbox" d="Me" />
        "<Define#NavBar tab=0 a=\"Home\" b=\"\" c=\"\" d=\"\"><Row#nv width=full padding=8 gap=4 bg=surface elevation=md><Button#n0 width=22% variant={tab == 0 ? \"fill\" : \"ghost\"} visible={a != \"\"} onTap={tab = 0}>{a}</Button><Button#n1 width=22% variant={tab == 1 ? \"fill\" : \"ghost\"} visible={b != \"\"} onTap={tab = 1}>{b}</Button><Button#n2 width=22% variant={tab == 2 ? \"fill\" : \"ghost\"} visible={c != \"\"} onTap={tab = 2}>{c}</Button><Button#n3 width=22% variant={tab == 3 ? \"fill\" : \"ghost\"} visible={d != \"\"} onTap={tab = 3}>{d}</Button></Row></Define>"
        // Breadcrumb: مسار: Home › Section › Page
        "<Define#Breadcrumb a=\"Home\" b=\"\" c=\"\"><Row#bc gap=6><Text#bca size=small color=primary>{a}</Text><Text#bcs1 size=small color=text_muted visible={b != \"\"}>/</Text><Text#bcb size=small color=primary visible={b != \"\"}>{b}</Text><Text#bcs2 size=small color=text_muted visible={c != \"\"}>/</Text><Text#bcc size=small visible={c != \"\"}>{c}</Text></Row></Define>"
        // SearchBar: حقل بحث مرتبط بخلية + زر مسح
        "<Define#SearchBar value=\"\" placeholder=\"Search\"><Row#sb2 width=full gap=8><Input#sbi width=75% value={value} placeholder={placeholder} /><Button#sbc variant=outline visible={value != \"\"} onTap={value = \"\"}>Clear</Button></Row></Define>"
        // Pagination: صفحة/صفحات مع سابق/تالي مرتبطة بخلية
        "<Define#Pagination page=1 pages=5><Row#pg gap=10><Button#pgp variant=outline visible={page > 1} onTap={page = page - 1}>Prev</Button><Text#pgt>{page} / {pages}</Text><Button#pgn variant=outline visible={page < pages} onTap={page = page + 1}>Next</Button></Row></Define>"
        // Footer: تذييل صغير
        "<Define#Footer text=\"\"><Column#ft width=full padding=12 align=center gap=2><Divider /><Text#ftt size=small color=text_muted>{text}</Text><Slot /></Column></Define>"
        // Fab: زر عائم دائري
        "<Define#Fab label=\"+\" tap=noop tint=primary><Row#fb><Button#fbb tone={tint} onTap={tap}>{label}</Button></Row></Define>"
        // ---- تسجيل الدخول ----
        // Field: تسمية + حقل + مساعدة/خطأ. الحقل مرتبط بخلية: value={cell} (يحرّره المضيف عبر set_input)
        "<Define#Field label=\"\" value=\"\" placeholder=\"\" helper=\"\" error=\"\" secure=false type=text min=-999999999 max=999999999 maxlength=0 prefix=\"\" suffix=\"\"><Column#fld width=full gap=4><Text#fl size=small color=text_muted visible={label != \"\"}>{label}</Text><Input#fi width=full value={value} placeholder={placeholder} secure={secure} error={error} type={type} min={min} max={max} maxlength={maxlength} prefix={prefix} suffix={suffix} /><Text#fe size=small color=danger visible={error != \"\"}>{error}</Text><Text#fh size=small color=text_muted visible={helper != \"\" and error == \"\"}>{helper}</Text></Column></Define>"
        // PasswordField: حقل سرّي + زر إظهار/إخفاء (show خلية منطقية) + مقياس قوة اختياري
        "<Define#PasswordField label=\"Password\" value=\"\" placeholder=\"\" error=\"\" show=false meter=false><Column#pw width=full gap=4><Text#pl size=small color=text_muted visible={label != \"\"}>{label}</Text><Row#pr gap=8><Input#pi width=68% value={value} placeholder={placeholder} secure=true reveal={show} error={error} /><Button#pb variant=outline onTap={toggle(show)}>Show</Button></Row><Progress#pm width=full value={indsin.passwordScore(value)} max=4 tone={indsin.passwordTone(value)} visible={meter and value != \"\"}/><Text#ps size=small color={indsin.passwordTone(value)} visible={meter and value != \"\"}>{indsin.passwordLabel(value)}</Text><Text#pe size=small color=danger visible={error != \"\"}>{error}</Text></Column></Define>"
        // OrDivider: خطّ مع كلمة في الوسط
        "<Define#OrDivider text=\"or\"><Row#od gap=10 padding=4><Divider width=25% /><Text#ot size=small color=text_muted>{text}</Text><Divider width=25% /></Row></Define>"
        // SocialButton: زر دخول عبر مزوّد خارجي: <SocialButton provider="Google" tap={loginGoogle()} />
        "<Define#SocialButton provider=\"Google\" tap=noop tint=neutral><Button#sb width=full variant=outline tone={tint} onTap={tap}>Continue with {provider}</Button></Define>"
        // OtpBoxes: ستّ خانات لرمز التحقق من نص code (يُملأ بحقل Input منفصل)
        "<Define#OtpBoxes code=\"\"><Row#otp gap=8><Card#o0 width=44 height=52 radius=10 padding=12><Text#d0 size=title>{substr(code,0,1)}</Text></Card><Card#o1 width=44 height=52 radius=10 padding=12><Text#d1 size=title>{substr(code,1,1)}</Text></Card><Card#o2 width=44 height=52 radius=10 padding=12><Text#d2 size=title>{substr(code,2,1)}</Text></Card><Card#o3 width=44 height=52 radius=10 padding=12><Text#d3 size=title>{substr(code,3,1)}</Text></Card><Card#o4 width=44 height=52 radius=10 padding=12><Text#d4 size=title>{substr(code,4,1)}</Text></Card><Card#o5 width=44 height=52 radius=10 padding=12><Text#d5 size=title>{substr(code,5,1)}</Text></Card></Row></Define>"
        // ---- مكوّنات عامة إضافية ----
        // Tabs: ثلاث تبويبات مرتبطة بخلية رقمية: <Tabs tab={t} a="Home" b="Search" c="Profile" />
        "<Define#Tabs tab=0 a=\"One\" b=\"Two\" c=\"Three\"><Row#tabs gap=6><Button#t0 width=30% variant={tab == 0 ? \"fill\" : \"outline\"} onTap={tab = 0}>{a}</Button><Button#t1 width=30% variant={tab == 1 ? \"fill\" : \"outline\"} onTap={tab = 1}>{b}</Button><Button#t2 width=30% variant={tab == 2 ? \"fill\" : \"outline\"} onTap={tab = 2}>{c}</Button></Row></Define>"
        // Stepper: أربع خطوات: <Stepper step={s} /> (step من 1 إلى 4)
        "<Define#Stepper step=1 tint=primary><Row#stp gap=6><Card#s1 width=22% height=8 radius=4 bg={step >= 1 ? tint : \"border\"} /><Card#s2 width=22% height=8 radius=4 bg={step >= 2 ? tint : \"border\"} /><Card#s3 width=22% height=8 radius=4 bg={step >= 3 ? tint : \"border\"} /><Card#s4 width=22% height=8 radius=4 bg={step >= 4 ? tint : \"border\"} /></Row></Define>"
        // Badge: شارة نصية صغيرة بدور ثيم
        "<Define#Badge text=\"New\" tint=danger><Row#bw><Card#bd padding=4 radius=10 bg={tint}><Text#bt size=small color=#ffffff>{text}</Text></Card></Row></Define>"
        // ProfileCard: صورة رمزية بالحروف الأولى + الاسم والبريد
        "<Define#ProfileCard name=\"Name\" email=\"\" tint=primary><Card#pc width=full padding=14 radius=18 elevation=sm><Row#pr2 gap=12><Avatar#av initials={indsin.initials(name)} width=48 height=48 tone={tint} /><Column#pi gap=2><Text#pn size=title>{name}</Text><Text#pe2 size=small color=text_muted>{email}</Text></Column></Row><Slot /></Card></Define>"
        // ConfirmCard: بطاقة تأكيد بزرّين: yes/no معالجات
        "<Define#ConfirmCard title=\"Are you sure?\" message=\"\" yes=\"Confirm\" no=\"Cancel\" onYes=noop onNo=noop><Card#cc width=full padding=16 radius=18 gap=10 elevation=md><Text#ct size=title>{title}</Text><Text#cm size=small color=text_muted visible={message != \"\"}>{message}</Text><Row#cb gap=8><Button#cn width=45% variant=outline onTap={onNo}>{no}</Button><Button#cy width=45% tone=danger onTap={onYes}>{yes}</Button></Row></Card></Define>"
        // LoginForm: بطاقة دخول كاملة: بريد + كلمة مرور + تذكّرني + خطأ عام + زر (loading) + «نسيت كلمة المرور»
        "<Define#LoginForm meter=false title=\"Sign in\" subtitle=\"\" email=\"\" password=\"\" show=false remember=false error=\"\" emailError=\"\" passError=\"\" loading=false action=\"Sign in\" submit=noop forgot=noop><Card#lf width=full padding=20 radius=22 gap=12 elevation=md><Text#lt size=heading>{title}</Text><Text#ls size=small color=text_muted visible={subtitle != \"\"}>{subtitle}</Text><Column#lw visible={error != \"\"}><Alert#le tone=danger title={error} /></Column><Field#lem label=\"Email\" value={email} placeholder=\"you@example.com\" error={emailError} /><PasswordField#lpw label=\"Password\" value={password} show={show} meter={meter} error={passError} /><Toggle#lrm label=\"Remember me\" cell={remember} /><Button#lgo width=full loading={loading} onTap={submit}>{action}</Button><Button#lfg variant=ghost onTap={forgot}>Forgot password?</Button></Card></Define>"
        // ---- أنواع حقول الإدخال: TextField · EmailField · NumberField · PhoneField · SearchField · TextAreaField (PasswordField أعلاه) ----
        // كل حقل يتحقق تلقائياً من النوع (حدّ أحمر + رسالة)، error= الصريح يغلب. القيمة خلية: value={cell}
        "<Define#TextField label=\"\" value=\"\" placeholder=\"\" helper=\"\" error=\"\" maxlength=0 prefix=\"\" suffix=\"\"><Field#tf label={label} value={value} placeholder={placeholder} helper={helper} error={error} maxlength={maxlength} prefix={prefix} suffix={suffix} /></Define>"
        "<Define#EmailField label=\"Email\" value=\"\" placeholder=\"you@example.com\" helper=\"\" error=\"\"><Field#ef label={label} value={value} placeholder={placeholder} helper={helper} type=email error={error != \"\" ? error : indsin.fieldError(\"email\", value)} /></Define>"
        "<Define#NumberField label=\"\" value=\"\" placeholder=\"0\" helper=\"\" error=\"\" min=-999999999 max=999999999 prefix=\"\" suffix=\"\"><Field#nf label={label} value={value} placeholder={placeholder} helper={helper} type=number min={min} max={max} prefix={prefix} suffix={suffix} error={error != \"\" ? error : indsin.fieldError(\"number\", value, min, max)} /></Define>"
        "<Define#PhoneField label=\"Phone\" value=\"\" placeholder=\"+961 3 123 456\" helper=\"\" error=\"\"><Field#phf label={label} value={value} placeholder={placeholder} helper={helper} type=tel error={error != \"\" ? error : indsin.fieldError(\"tel\", value)} /></Define>"
        "<Define#SearchField value=\"\" placeholder=\"Search\"><Row#sfr width=full gap=8><Input#sfi width=75% type=search value={value} placeholder={placeholder} /><Button#sfc variant=outline visible={value != \"\"} onTap={value = \"\"}>Clear</Button></Row></Define>"
        "<Define#TextAreaField label=\"\" value=\"\" placeholder=\"\" helper=\"\" error=\"\" height=120 maxlength=0><Column#taf width=full gap=4><Text#tal size=small color=text_muted visible={label != \"\"}>{label}</Text><TextArea#tai width=full height={height} value={value} placeholder={placeholder} maxlength={maxlength} error={error} /><Text#tae size=small color=danger visible={error != \"\"}>{error}</Text><Text#tah size=small color=text_muted visible={helper != \"\" and error == \"\"}>{helper}</Text></Column></Define>"
        // ContainerView: بطاقة تعرض بيانات container حيّة (حقول/جدول/مستندات) — name=اسم الحاوية، tick=خلية تُغيَّر لإعادة الرسم
        "<Define#ContainerView name=\"\" tick=0 tint=primary><Card#cv width=full padding=12 radius=14 gap=6 elevation=sm><Row#cvh gap=8><Text#cvt size=title>{name}</Text><Spacer /><Text#cvk size=small color={tint}>{indsin.run(\"containerKind\", name, tick)}</Text></Row><Divider /><Text#cvb>{indsin.run(\"containerBody\", name, tick)}</Text></Card></Define>";
}
// مكتبة الحفظ بحاويات Rin (@container.doc) مع ملف JSON اختياري: storeOpen/Save/Get/Has/Delete/All/Count/Find/Flush/Hash.
inline const char* storeLibPrelude() {
    return "<Rin>fun storeFlush(coll) { let p = cacheGet(\"store.filePath.\" + coll); if (p == nil or p == \"\") { return 0; } let rows = []; for (let id in docIds(coll)) { push(rows, findDoc(coll, id)); } writeFile(p, json.stringify(rows)); return len(rows); } fun storeOpen(coll, filePath) { container.ensure(\"doc\", coll); cacheSet(\"store.filePath.\" + coll, filePath); if (filePath != \"\" and fileExists(filePath)) { let txt = readFile(filePath); if (json.valid(txt)) { for (let r in json.parse(txt)) { insertDoc(coll, r[\"_id\"], r); } } } return len(docIds(coll)); } fun storeSave(coll, id, data) { container.ensure(\"doc\", coll); data[\"_id\"] = id; insertDoc(coll, id, data); storeFlush(coll); return id; } fun storeGet(coll, id) { container.ensure(\"doc\", coll); return findDoc(coll, id); } fun storeHas(coll, id) { return storeGet(coll, id) != nil; } fun storeDelete(coll, id) { container.ensure(\"doc\", coll); let r = deleteDoc(coll, id); storeFlush(coll); return r; } fun storeAll(coll) { container.ensure(\"doc\", coll); let rows = []; for (let id in docIds(coll)) { push(rows, findDoc(coll, id)); } return rows; } fun storeCount(coll) { container.ensure(\"doc\", coll); return len(docIds(coll)); } fun storeFind(q) { return sql(q); } fun storeHash(plain) { return hash(plain); }</Rin>";
}
inline bool usesStoreApi(const std::string& src) {
    static const char* names[] = {"storeOpen(", "storeSave(", "storeGet(", "storeHas(", "storeDelete(", "storeAll(", "storeCount(", "storeFind(", "storeFlush(", "storeHash("};
    for (const char* n : names) if (src.find(n) != std::string::npos) return true;
    return false;
}
// عرض بيانات container في indsin: دوال Rin تُحقَن فقط إن استُعمل <ContainerView> أو containerBody/containerKind (سطر واحد: الأسطر محفوظة).
// containerKind(name) -> "data" | "table" | ... · containerBody(name, tick) يطبع الحقول (أبجدياً) ثم صفوف الجدول/المستندات؛ tick لإعادة الحساب فقط.
inline const char* containerLibPrelude() {
    return "<Rin>fun containerKind(name, tick) { if (!hasContainer(name)) { return \"?\"; } let k = container.info(name)[\"kind\"]; if (k == \"container\") { return \"plain\"; } return replace(k, \"container.\", \"\"); } "
           "fun containerBody(name, tick) { if (!hasContainer(name)) { print \"no container: \" + name; return 0; } let info = container.info(name); let isTable = info[\"kind\"] == \"container.table\"; let f = container.fields(name); let ks = sort(keys(f)); let n = 0; "
           "for (let k in ks) { if (isTable and (k == \"rows\" or k == \"header\" or k == \"body\" or k == \"records\" or k == \"columns\" or k == \"style\")) { continue; } let v = f[k]; if (type(v) == \"function\") { continue; } n = n + 1; if (type(v) == \"string\") { print k + \" : \" + v; } else { print k + \" : \" + jsonEncode(v); } } "
           "if (isTable) { let rows = container.get(name, \"rows\"); if (rows != nil) { for (let r in rows) { let cells = []; for (let c in r) { push(cells, str(c)); } print join(cells, \" | \"); n = n + 1; } } } "
           "if (info[\"kind\"] == \"container.doc\" or info[\"kind\"] == \"container.datalist\") { for (let id in docIds(name)) { print \"#\" + id + \"  \" + jsonEncode(findDoc(name, id)); n = n + 1; } } "
           "if (n == 0) { print \"(no data)\"; } return n; }</Rin>";
}
inline bool usesContainerView(const std::string& src) {
    return src.find("<ContainerView") != std::string::npos || src.find("containerBody(") != std::string::npos ||
           src.find("containerKind(") != std::string::npos;
}
inline bool usesBuiltinComponent(const std::string& src) {
    static const char* names[] = {"ContainerView", "Chip", "Stat", "Alert", "Section", "ListTile", "Hero", "EmptyState", "Toggle", "Field", "PasswordField", "OrDivider", "SocialButton", "OtpBoxes", "LoginForm", "Tabs", "Stepper", "Badge", "ProfileCard", "ConfirmCard", "TopBar", "Header", "NavBar", "Breadcrumb", "SearchBar", "Pagination", "Footer", "Fab", "TextField", "EmailField", "NumberField", "PhoneField", "SearchField", "TextAreaField"};
    for (const char* n : names) {
        std::string needle = std::string("<") + n;
        for (size_t p = src.find(needle); p != std::string::npos; p = src.find(needle, p + 1)) {
            size_t e = p + needle.size();
            if (e >= src.size() || !(detail::isIdChar(src[e]) || src[e] == '.')) return true;
        }
    }
    return false;
}

// هل في المصدر <field …> أو <head.X> أو <rin--g==> ؟ (فحص سريع؛ يفعّل مسح الحاويات)
inline bool usesFieldHeadRing(const std::string& src) {
    return src.find("<field") != std::string::npos || src.find("<head") != std::string::npos || src.find("<rin--g") != std::string::npos;
}

namespace detail {
inline std::string desugarWith(const std::string& source, Ctx& ctx) {
    std::string pre;
    if (usesBuiltinComponent(source)) pre += builtinPrelude();
    if (usesStoreApi(source)) pre += storeLibPrelude();
    if (usesContainerView(source)) pre += containerLibPrelude();
    std::string r;
    std::string withPrelude = pre.empty() ? std::string() : pre + source; // بلا '\n' في المقدمة: الأسطر محفوظة
    const std::string& text = pre.empty() ? source : withPrelude;
    if (usesFieldHeadRing(source)) ctx.reg = scanContainers(text); // مسح ساكن للحاويات: فقط إن استُعمل field/head/rin--g
    Desugarer d(text, ctx);
    r = d.run();
    if (!ctx.tail.empty()) r += "\n" + ctx.tail; // عبارات <<let>> المرفوعة (بعد كل الأسطر الأصلية، فأرقام الأخطاء سليمة)
    return r;
}
} // namespace detail

inline std::string desugar(const std::string& source) {
    detail::Ctx ctx;
    return detail::desugarWith(source, ctx);
}

// ── <<each LIST as it>> على بيانات Rin: تمريرتان (انظر rin_indsin_pipeline.h → expandTagSyntax) ──
// هل في المصدر <<each …>>؟ (فحص سريع قبل أي عمل إضافي)
inline bool usesEach(const std::string& src) {
    for (size_t p = src.find("<<"); p != std::string::npos; p = src.find("<<", p + 2)) {
        size_t e = p + 2;
        while (e < src.size() && std::isspace((unsigned char)src[e])) ++e;
        if (src.compare(e, 4, "each") == 0 && e + 4 < src.size() && !detail::isIdChar(src[e + 4])) return true;
    }
    return false;
}
// تمريرة القياس: أجسام <<each>> غير الحرفية تُحذف (الأسطر محفوظة) وتعابير قوائمها تُعاد في exprs.
inline std::string desugarProbe(const std::string& source, std::vector<std::string>& exprs) {
    detail::Ctx ctx;
    ctx.probe = true;
    std::string r = detail::desugarWith(source, ctx);
    exprs = ctx.eachExprs;
    return r;
}
// التمريرة النهائية: counts = حجم كل قائمة (مفتاحها نص تعبير القائمة كما كُتب).
inline std::string desugarWithCounts(const std::string& source, const std::map<std::string, EachSize>& counts) {
    detail::Ctx ctx;
    ctx.eachCounts = counts;
    return detail::desugarWith(source, ctx);
}

} // namespace tags
} // namespace indsin

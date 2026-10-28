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
// قيم السمات:
//   "نص" أو 'نص'      سلسلة حرفية          |  {تعبير Rin}   تعبير يُقيَّم (متغيرات warp، شروط، دوال)
//   12  120px  250ms   رقم/طول/زمن كما هو    |  #1E1E2E       لون (يُحاط بعلامتي اقتباس تلقائياً)
//   true false null    ثوابت                 |  spacious      كلمة مجرّدة = توكن نصّي (مثل HTML)
//   (متغيّر Rin يُكتب دائماً بين أقواس معقوصة: visible={flag})
#pragma once

#include <cctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace indsin {
namespace tags {

struct TagError {
    std::string message;
    int line;
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
};

struct Macro {
    std::vector<std::pair<std::string, std::string>> props; // الخصائص وقيمها الافتراضية (نص Rin)
    std::string body;                                       // مصدر الجسم الخام (بصيغة الوسوم)
};

struct Ctx { // حالة مشتركة بين التشغيل الرئيسي وتشغيلات التوسعة (Define/Repeat)
    std::map<std::string, Macro> macros;
    std::map<std::string, std::vector<Attr>> styles; // <Style#name …> — أنماط قابلة لإعادة الاستعمال عبر class="…"
    int autoId = 0;
    int depth = 0;
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

    std::string readQuoted() { // يقرأ "..." أو '...' ويعيد سلسلة بعلامتي " دائماً
        char q = s[i++];
        std::string body;
        while (i < n && s[i] != q) {
            if (s[i] == '\\' && i + 1 < n) { body += s[i++]; body += s[i++]; continue; }
            if (s[i] == '\n') ++line;
            if (q == '\'' && s[i] == '"') { body += "\\\""; ++i; continue; }
            body += s[i++];
        }
        if (i >= n) fail("unterminated string in tag");
        ++i;
        return "\"" + body + "\"";
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
    std::string applyVars(const std::string& e) const {
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

    void handleTag() {
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
            if (!nm.empty() && lower(nm) != lower(f.tagName))
                fail("closing tag </" + nm + "> does not match <" + f.tagName + "> opened at line " + std::to_string(f.line), startLine);
            if (nm.empty() && !f.tagName.empty())
                fail("'</>' closes a fragment, but <" + f.tagName + "> is open (line " + std::to_string(f.line) + ")", startLine);
            stack.pop_back();
            if (lower(f.tagName) == "if" || lower(f.tagName) == "else") lastIf[(int)stack.size()] = f.chain;
            if (!f.closer.empty()) put(f.closer);
            return;
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
            while (i < n && isIdChar(s[i])) ++i;
            Attr a; a.key = s.substr(kb, i - kb);
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
                std::string chain = c;
                if (tl == "else") {
                    auto it = lastIf.find((int)stack.size());
                    if (it == lastIf.end()) fail("<Else> must directly follow an </If>", startLine);
                    std::string neg = "!(" + it->second + ")";
                    chain = c.empty() ? it->second : "(" + it->second + ") or (" + c + ")";
                    c = c.empty() ? neg : neg + " and (" + c + ")";
                    lastIf.erase(it);
                }
                stack.push_back({tagName, "", startLine, chain, c});
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
            stmt = "@" + hl + "=" + id;
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
        } else if (hl == "element") {
            if (sub.empty()) fail("<Element.Kind#name> needs a kind, e.g. <Element.Button#ok>", startLine);
            kw = "element"; closer = ".end/element";
            if (id.empty()) id = lower(sub) + "_" + std::to_string(++ctx.autoId);
            stmt = "@element." + sub + "=" + id;
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
            stmt = "@view." + kind + "=" + id;
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
            stack.push_back({tagName, closer, startLine, "", ""});
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
        // ---- تسجيل الدخول ----
        // Field: تسمية + حقل + مساعدة/خطأ. الحقل مرتبط بخلية: value={cell} (يحرّره المضيف عبر set_input)
        "<Define#Field label=\"\" value=\"\" placeholder=\"\" helper=\"\" error=\"\" secure=false><Column#fld width=full gap=4><Text#fl size=small color=text_muted visible={label != \"\"}>{label}</Text><Input#fi width=full value={value} placeholder={placeholder} secure={secure} error={error} /><Text#fe size=small color=danger visible={error != \"\"}>{error}</Text><Text#fh size=small color=text_muted visible={helper != \"\" and error == \"\"}>{helper}</Text></Column></Define>"
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
        "<Define#LoginForm meter=false title=\"Sign in\" subtitle=\"\" email=\"\" password=\"\" show=false remember=false error=\"\" emailError=\"\" passError=\"\" loading=false action=\"Sign in\" submit=noop forgot=noop><Card#lf width=full padding=20 radius=22 gap=12 elevation=md><Text#lt size=heading>{title}</Text><Text#ls size=small color=text_muted visible={subtitle != \"\"}>{subtitle}</Text><Column#lw visible={error != \"\"}><Alert#le tone=danger title={error} /></Column><Field#lem label=\"Email\" value={email} placeholder=\"you@example.com\" error={emailError} /><PasswordField#lpw label=\"Password\" value={password} show={show} meter={meter} error={passError} /><Toggle#lrm label=\"Remember me\" cell={remember} /><Button#lgo width=full loading={loading} onTap={submit}>{action}</Button><Button#lfg variant=ghost onTap={forgot}>Forgot password?</Button></Card></Define>";
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
inline bool usesBuiltinComponent(const std::string& src) {
    static const char* names[] = {"Chip", "Stat", "Alert", "Section", "ListTile", "Hero", "EmptyState", "Toggle", "Field", "PasswordField", "OrDivider", "SocialButton", "OtpBoxes", "LoginForm", "Tabs", "Stepper", "Badge", "ProfileCard", "ConfirmCard"};
    for (const char* n : names) {
        std::string needle = std::string("<") + n;
        for (size_t p = src.find(needle); p != std::string::npos; p = src.find(needle, p + 1)) {
            size_t e = p + needle.size();
            if (e >= src.size() || !(detail::isIdChar(src[e]) || src[e] == '.')) return true;
        }
    }
    return false;
}

inline std::string desugar(const std::string& source) {
    detail::Ctx ctx;
    std::string pre;
    if (usesBuiltinComponent(source)) pre += builtinPrelude();
    if (usesStoreApi(source)) pre += storeLibPrelude();
    if (!pre.empty()) {
        std::string withPrelude = pre + source; // بلا '\n' في المقدمة: الأسطر محفوظة
        detail::Desugarer d(withPrelude, ctx);
        return d.run();
    }
    detail::Desugarer d(source, ctx);
    return d.run();
}

} // namespace tags
} // namespace indsin

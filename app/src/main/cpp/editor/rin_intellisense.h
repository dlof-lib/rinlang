// rin_intellisense.h — محرك IntelliSense لمحرر Rin (header-only، بلا اعتماد على Android ولا على EditorEngine).
//
// يوفّر أربع قدرات، كلها دوال نقية فوق (أسطر المستند + موضع المؤشر) فتُختبر بـ g++ على سطح المكتب:
//   1) complete()        إكمال سياقي: أعضاء (obj. / Set. / container.) · @tags · #words · وحدات @import ·
//                        متغيرات ضمن النطاق (let / معاملات / متغيرات الحلقات / catch) · دوال وأصناف المستخدم ·
//                        الدوال المدمجة (892) · قوالب (snippets) · كلمات المستند — مع بحث ضبابي وترتيب ذكي.
//   2) signatureHelp()   توقيع الدالة التي يقف المؤشر داخل استدعائها مع المعامل النشط.
//   3) describe()        توثيق موضع (hover): نوع الرمز + توقيعه + تعليقات `//` التي تسبق تعريفه.
//   4) recordUse()       تعلّم المستخدم: ما يُقبل كثيراً يصعد في الترتيب.
//
// التحليل يعتمد ماسحاً متسامحاً خاصاً (لا rin::Lexer) لأن الكود أثناء الكتابة غالباً غير صالح نحوياً
// (نص غير مغلق، قوس ناقص) فيرمي rin::Lexer استثناءً ويُفقَد كل السياق. هذا الماسح لا يرمي أبداً.
//
// كل الأعمدة إزاحات بايت UTF-8 صفرية (نفس اصطلاح EditorEngine).
#pragma once

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "rin_intellisense_data.h"

namespace rinedit {
namespace intel {

// ---------------------------------------------------------------------------------------
// الأنواع العامة (تطابق CompletionKind في Kotlin بالترتيب)
// ---------------------------------------------------------------------------------------

enum class Kind : int {
    Keyword = 0, Function = 1, Method = 2, Variable = 3, Constant = 4, Class = 5, Snippet = 6,
    Namespace = 7, Property = 8, Template = 9, Label = 10, Parameter = 11, Tag = 12, Module = 13,
    Word = 14, Enum = 15, EnumMember = 16, Interface = 17,
};

enum class Trigger : int { None = 0, Member = 1, At = 2, Hash = 3, ImportPath = 4 };

struct CompletionItem {
    std::string label;       // ما يُعرض ويُطابَق
    std::string insertText;  // ما يُدرَج (قد يختلف: name())
    std::string detail;      // سطر قصير يمين الصف: (a, b) → number · let: number · class
    std::string doc;         // شرح (يظهر في لوحة التوثيق)
    Kind kind = Kind::Word;
    int cursorBack = 0;      // كم بايت يُرجَع المؤشر بعد الإدراج (للدخول داخل الأقواس)
    bool snippet = false;    // قالب: الواجهة تستدعي expandSnippetAtCursor بعد الإدراج
    int score = 0;
    std::vector<int> matchPos; // مواضع الحروف المطابقة داخل label (بايت)
};

struct CompletionResult {
    int startCol = 0;  // بداية النص المُستبدَل (بايت)
    int endCol = 0;    // نهايته (بايت)؛ = عمود المؤشر عادةً
    Trigger trigger = Trigger::None;
    std::vector<CompletionItem> items;
};

struct SignatureHelp {
    bool valid = false;
    std::string name;
    std::vector<std::string> params;
    int active = 0;
    std::string ret;
    std::string doc;
    int kind = 0; // 0 مدمجة، 1 دالة مستخدم، 2 صنف/منشئ، 3 قالب حلقة
};

struct SymbolInfo {
    bool valid = false;
    std::string name;
    std::string kindName;   // "function" "variable" "class" "keyword" "builtin" "namespace" ...
    std::string signature;  // سطر كود مختصر
    std::string doc;
    int defLine = -1;       // سطر التعريف (للمستخدم) أو -1
    int defCol = -1;
};

struct SnippetInfo { std::string trigger; std::string title; };

// ما يحتاجه المحرك من الخارج (قوائم تبنيها EditorEngine من rin::keywordList() و tools::snippets())
struct Env {
    std::vector<std::string> keywords;     // كلمات محجوزة
    std::vector<std::string> contextual;   // كلمات سياقية (each/where/...)
    std::vector<SnippetInfo> snippets;
};

// ---------------------------------------------------------------------------------------
// أدوات نصية
// ---------------------------------------------------------------------------------------

inline bool isIdStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
inline bool isIdChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

inline std::string lowerAscii(const std::string& s) {
    std::string r = s;
    for (auto& c : r) if ((unsigned char)c < 0x80) c = (char)std::tolower((unsigned char)c);
    return r;
}

// ---------------------------------------------------------------------------------------
// بحث ضبابي بترتيب احترافي (بادئة > حدود كلمات/camelCase > تسلسل جزئي)
// ---------------------------------------------------------------------------------------

struct FuzzyResult { bool ok = false; int score = 0; std::vector<int> pos; };

inline FuzzyResult fuzzyMatch(const std::string& query, const std::string& cand) {
    FuzzyResult r;
    if (query.empty()) { r.ok = true; return r; }
    const size_t qn = query.size(), cn = cand.size();
    if (qn > cn) return r;
    const std::string ql = lowerAscii(query), cl = lowerAscii(cand);
    { // فحص سريع: تسلسل جزئي؟
        size_t qi = 0;
        for (size_t j = 0; j < cn && qi < qn; ++j) if (cl[j] == ql[qi]) ++qi;
        if (qi < qn) return r;
    }
    auto boundary = [&](size_t j) -> int {
        if (j == 0) return 22;
        unsigned char p = (unsigned char)cand[j - 1], c = (unsigned char)cand[j];
        if (p == '_' || p == '.' || p == '#' || p == '@' || p == '-') return 18;
        if (std::islower(p) && std::isupper(c)) return 14;
        return 0;
    };
    const int NEG = -1000000;
    std::vector<int> dp(qn * cn, NEG), from(qn * cn, -1);
    for (size_t i = 0; i < qn; ++i) {
        for (size_t j = i; j < cn; ++j) {
            if (cl[j] != ql[i]) continue;
            int bonus = 10 + boundary(j) + (cand[j] == query[i] ? 2 : 0);
            // حرف كبير مكتوب = نيّة صريحة لبداية مقطع (hump): iN → inputNumber لا iN → input(n)
            if (std::isupper((unsigned char)query[i]) && cand[j] != query[i]) bonus -= 14;
            if (i == 0) {
                dp[i * cn + j] = bonus - (int)std::min<size_t>(j, 8);
            } else {
                int best = NEG, bk = -1;
                for (size_t k = i - 1; k < j; ++k) {
                    int prev = dp[(i - 1) * cn + k];
                    if (prev == NEG) continue;
                    int v = prev + bonus + (k + 1 == j ? 15 : -(int)std::min<size_t>(j - k - 1, 6) * 2);
                    if (v > best) { best = v; bk = (int)k; }
                }
                if (bk >= 0) { dp[i * cn + j] = best; from[i * cn + j] = bk; }
            }
        }
    }
    int best = NEG, bj = -1;
    for (size_t j = 0; j < cn; ++j) {
        int v = dp[(qn - 1) * cn + j];
        if (v > best) { best = v; bj = (int)j; }
    }
    if (bj < 0) return r;
    r.ok = true;
    r.pos.assign(qn, 0);
    int j = bj;
    for (int i = (int)qn - 1; i >= 0; --i) {
        r.pos[i] = j;
        j = from[(size_t)i * cn + j];
    }
    int score = best;
    if (cl.compare(0, qn, ql) == 0) { // بادئة حرفية (غير حسّاسة للحالة)
        score += 40;
        if (cand.compare(0, qn, query) == 0) score += 10;
        if (qn == cn) score += 20;
    }
    score -= (int)(cn - qn) / 2;
    r.score = score;
    return r;
}

// ---------------------------------------------------------------------------------------
// الماسح المتسامح
// ---------------------------------------------------------------------------------------

enum class TK : uint8_t { Ident, Number, String, Punct, Hash };

struct Tok {
    TK k;
    std::string s;
    int line, a, b;   // [a, b) بايت
};

struct CursorState {
    bool inString = false;
    bool inComment = false;
    int strLine = 0, strContentCol = 0; // بداية محتوى النص الحرفي
    int tokensBefore = 0;               // عدد الرموز المنتهية قبل المؤشر
    bool valid = false;
};

struct Scan {
    std::vector<Tok> toks;
    CursorState cur;
};

// يمسح كل المستند. يلتقط حالة المؤشر (داخل نص/تعليق) عند (curLine, curCol).
inline Scan scanDocument(const std::vector<std::string>& lines, int curLine, int curCol) {
    Scan sc;
    sc.toks.reserve(lines.size() * 6);
    struct Tpl { int depth; };
    std::vector<Tpl> tplStack;      // قوالب `...${ code }...` قيد المسح
    bool inTplText = false;         // داخل نص قالب (خارج ${})
    bool inStr = false;             // داخل "..."
    int strLine = 0, strStart = 0;  // بداية الرمز النصي (موضع علامة الاقتباس)
    std::string strBuf;

    auto snap = [&](int ln, int col, bool inComment) {
        sc.cur.valid = true;
        sc.cur.inString = inStr || inTplText;
        sc.cur.inComment = inComment;
        sc.cur.strLine = strLine;
        sc.cur.strContentCol = strStart + 1;
        sc.cur.tokensBefore = (int)sc.toks.size();
        (void)ln; (void)col;
    };

    for (int ln = 0; ln < (int)lines.size(); ++ln) {
        const std::string& L = lines[ln];
        const int n = (int)L.size();
        int i = 0;
        bool lineComment = false;
        while (true) {
            if (ln == curLine && i == curCol && !sc.cur.valid) snap(ln, i, lineComment);
            if (i >= n) break;
            unsigned char c = (unsigned char)L[i];
            if (inStr) {
                if (c == '\\' && i + 1 < n) { strBuf += L[i]; strBuf += L[i + 1]; i += 2; continue; }
                if (c == '"') {
                    inStr = false;
                    sc.toks.push_back({TK::String, strBuf, strLine, strStart, i + 1});
                    strBuf.clear(); ++i; continue;
                }
                strBuf += L[i]; ++i; continue;
            }
            if (inTplText) {
                if (c == '\\' && i + 1 < n) { i += 2; continue; }
                if (c == '`') { inTplText = false; ++i; continue; }
                if (c == '$' && i + 1 < n && L[i + 1] == '{') {
                    inTplText = false; tplStack.push_back({1}); i += 2; continue;
                }
                ++i; continue;
            }
            if (c == ' ' || c == '\t' || c == '\r') { ++i; continue; }
            if (c == '/' && i + 1 < n && L[i + 1] == '/') {
                // بقية السطر تعليق
                if (ln == curLine && curCol > i) { snap(ln, curCol, true); lineComment = true; }
                break;
            }
            if (c == '"') { inStr = true; strLine = ln; strStart = i; strBuf.clear(); ++i; continue; }
            if (c == '`') { inTplText = true; strLine = ln; strStart = i; ++i; continue; }
            if (isIdStart(c)) {
                int j = i + 1;
                while (j < n && isIdChar((unsigned char)L[j])) ++j;
                sc.toks.push_back({TK::Ident, L.substr(i, j - i), ln, i, j});
                i = j; continue;
            }
            if (std::isdigit(c)) {
                int j = i + 1;
                while (j < n) {
                    unsigned char d = (unsigned char)L[j];
                    if (std::isalnum(d) || d == '_') ++j;
                    else if (d == '.' && j + 1 < n && std::isdigit((unsigned char)L[j + 1])) ++j;
                    else break;
                }
                sc.toks.push_back({TK::Number, L.substr(i, j - i), ln, i, j});
                i = j; continue;
            }
            if (c == '#' && i + 1 < n && isIdStart((unsigned char)L[i + 1])) {
                int j = i + 1;
                while (j < n && isIdChar((unsigned char)L[j])) ++j;
                sc.toks.push_back({TK::Hash, L.substr(i, j - i), ln, i, j});
                i = j; continue;
            }
            if (!tplStack.empty()) {
                if (c == '{') ++tplStack.back().depth;
                else if (c == '}' && --tplStack.back().depth == 0) {
                    tplStack.pop_back(); inTplText = true; ++i; continue;
                }
            }
            // معاملات من محرفين
            if (i + 1 < n) {
                char d = L[i + 1];
                if ((c == '|' && d == '>') || (c == '=' && d == '=') || (c == '!' && d == '=') ||
                    (c == '<' && d == '=') || (c == '>' && d == '=')) {
                    sc.toks.push_back({TK::Punct, L.substr(i, 2), ln, i, i + 2});
                    i += 2; continue;
                }
            }
            sc.toks.push_back({TK::Punct, std::string(1, (char)c), ln, i, i + 1});
            ++i;
        }
        if (inStr) strBuf += '\n';
    }
    if (inStr) sc.toks.push_back({TK::String, strBuf, strLine, strStart, strStart + 1 + (int)strBuf.size()});
    if (!sc.cur.valid) snap(curLine, curCol, false);
    return sc;
}

// ---------------------------------------------------------------------------------------
// التحليل الدلالي الخفيف: نطاقات + رموز + أصناف
// ---------------------------------------------------------------------------------------

struct Sym {
    std::string name;
    Kind kind = Kind::Variable;
    std::string type;                    // number/string/array/object/boolean/nil/function/Set/اسم صنف
    std::string cls;                     // اسم الصنف إن عُرف (لإكمال الأعضاء)
    std::vector<std::string> params;
    bool hasParams = false;
    bool isStatic = false;
    int line = -1, col = -1;
    std::string doc;
    std::vector<std::string> members;    // مفاتيح كائن حرفي
};

struct ClassInfo {
    std::string name;
    std::string kw;                      // class/struct/interface/trait/enum
    std::vector<std::string> bases;      // extends / implements / uses
    std::vector<Sym> members;
    int line = -1, col = -1;
    std::string doc;
};

struct Analysis {
    std::vector<Sym> visible;            // الأقرب أولاً
    std::vector<Sym> globals;            // دوال/أصناف/قوالب علوية (مرفوعة)
    std::unordered_map<std::string, ClassInfo> classes;
    std::vector<Sym> labels;
    std::string enclosingClass;
    bool inFunction = false;
    std::unordered_set<std::string> memberWords;
    std::unordered_map<std::string, int> idents;
};

inline bool isBuiltinKw(const std::string& s);

inline std::string docAbove(const std::vector<std::string>& lines, int line) {
    std::vector<std::string> out;
    for (int l = line - 1; l >= 0 && (int)out.size() < 8; --l) {
        const std::string& t = lines[l];
        size_t p = t.find_first_not_of(" \t");
        if (p == std::string::npos || t.compare(p, 2, "//") != 0) break;
        std::string body = t.substr(p + 2);
        while (!body.empty() && body[0] == '/') body.erase(0, 1); // ///
        if (!body.empty() && body[0] == ' ') body.erase(0, 1);
        out.push_back(body);
    }
    std::string r;
    for (int i = (int)out.size() - 1; i >= 0; --i) { r += out[i]; if (i) r += '\n'; }
    return r;
}

inline const data::FuncSig* findFunc(const std::string& name) {
    static std::unordered_map<std::string, const data::FuncSig*> idx = [] {
        std::unordered_map<std::string, const data::FuncSig*> m;
        for (const auto& f : data::kFuncs) m[f.name] = &f;
        return m;
    }();
    auto it = idx.find(name);
    return it == idx.end() ? nullptr : it->second;
}

inline std::vector<std::string> splitCsv(const char* s) {
    std::vector<std::string> out;
    if (!s[0]) return out;
    std::string cur;
    for (const char* p = s; *p; ++p) {
        if (*p == ',') { out.push_back(cur); cur.clear(); } else cur += *p;
    }
    out.push_back(cur);
    return out;
}

inline bool isTok(const std::vector<Tok>& t, int i, const char* s) {
    return i >= 0 && i < (int)t.size() && t[i].s == s && t[i].k != TK::String;
}
inline bool isIdentTok(const std::vector<Tok>& t, int i) {
    return i >= 0 && i < (int)t.size() && t[i].k == TK::Ident;
}

// نوع تقريبي لتعبير يبدأ عند الرمز j
inline void inferType(const std::vector<Tok>& t, int j, const Analysis& an, Sym& out) {
    if (j >= (int)t.size()) return;
    const Tok& x = t[j];
    if (x.k == TK::Number) { out.type = "number"; return; }
    if (x.k == TK::String) { out.type = "string"; return; }
    if (x.k == TK::Punct && x.s == "[") { out.type = "array"; return; }
    if (x.k == TK::Punct && x.s == "{") { out.type = "object"; return; }
    if (x.k == TK::Punct && x.s == "-" && j + 1 < (int)t.size() && t[j + 1].k == TK::Number) { out.type = "number"; return; }
    if (x.k != TK::Ident) return;
    if (x.s == "true" || x.s == "false") { out.type = "boolean"; return; }
    if (x.s == "nil") { out.type = "nil"; return; }
    if (x.s == "fun") { out.type = "function"; return; }
    if (x.s == "new" && isIdentTok(t, j + 1)) { out.type = t[j + 1].s; out.cls = t[j + 1].s; return; }
    if (x.s == "Set") {
        bool call = isTok(t, j + 1, "(");
        bool ctor = isTok(t, j + 1, ".") && isIdentTok(t, j + 2) &&
                    (t[j + 2].s == "of" || t[j + 2].s == "from");
        if (call || ctor) { out.type = "Set"; out.cls = "Set"; return; }
    }
    if (isTok(t, j + 1, "(")) {
        if (an.classes.count(x.s)) { out.type = x.s; out.cls = x.s; return; }
        if (const auto* f = findFunc(x.s)) { if (f->ret[0] && std::string(f->ret).find(' ') == std::string::npos) out.type = f->ret; return; }
        if (!x.s.empty() && std::isupper((unsigned char)x.s[0])) { out.type = x.s; out.cls = x.s; }
        return;
    }
    if (!isTok(t, j + 1, ".")) { // نسخ نوع متغير آخر
        for (const Sym& s : an.visible) if (s.name == x.s) { out.type = s.type; out.cls = s.cls; return; }
    }
}

inline std::vector<std::string> objectKeys(const std::vector<Tok>& t, int open) {
    std::vector<std::string> keys;
    int depth = 0;
    for (int i = open; i < (int)t.size(); ++i) {
        const Tok& x = t[i];
        if (x.k == TK::Punct) {
            if (x.s == "{" || x.s == "[" || x.s == "(") ++depth;
            else if (x.s == "}" || x.s == "]" || x.s == ")") { if (--depth <= 0) break; }
        }
        if (depth == 1 && (x.k == TK::Ident || x.k == TK::String) && isTok(t, i + 1, ":") &&
            (isTok(t, i - 1, "{") || isTok(t, i - 1, ",")))
            keys.push_back(x.s);
    }
    return keys;
}

// يحلّل المستند كله. [ci] = عدد الرموز قبل المؤشر (يُلتقَط لقطة النطاق عنده).
inline Analysis analyze(const std::vector<std::string>& lines, const std::vector<Tok>& t, int ci) {
    Analysis an;
    struct Frame { std::vector<Sym> syms; std::string ownerClass; bool isFunc = false; bool isEnum = false; };
    std::vector<Frame> frames(1);
    std::vector<Sym> pending;
    std::string pendingClass; bool pendingEnum = false, pendingFunc = false;
    std::string headerKw;
    int depthParen = 0, pendingDepth = -1;
    bool snapped = false;
    const int N = (int)t.size();

    auto ensureClass = [&](const std::string& name) -> ClassInfo& {
        ClassInfo& c = an.classes[name];
        c.name = name; return c;
    };
    auto snapshot = [&]() {
        snapped = true;
        for (int f = (int)frames.size() - 1; f >= 0; --f) {
            for (int k = (int)frames[f].syms.size() - 1; k >= 0; --k) an.visible.push_back(frames[f].syms[k]);
            if (frames[f].isFunc) an.inFunction = true;
            if (an.enclosingClass.empty() && !frames[f].ownerClass.empty()) an.enclosingClass = frames[f].ownerClass;
        }
        for (int k = (int)pending.size() - 1; k >= 0; --k) an.visible.insert(an.visible.begin(), pending[k]);
        if (!pending.empty() && pendingFunc) an.inFunction = true;
    };
    auto atClassBody = [&]() -> const std::string& { return frames.back().ownerClass; };
    auto declRow = [&](Sym& s, int ti) { s.line = t[ti].line; s.col = t[ti].a; };
    auto parseParams = [&](int open, std::vector<Sym>& out, std::vector<std::string>& names) -> int {
        int depth = 0, i = open;
        bool expect = true;
        for (; i < N; ++i) {
            if (t[i].k == TK::Punct) {
                const std::string& p = t[i].s;
                if (p == "(" || p == "[" || p == "{") { ++depth; continue; }
                if (p == ")" || p == "]" || p == "}") { if (--depth == 0) break; continue; }
                if (p == "," && depth == 1) { expect = true; continue; }
            }
            if (depth == 1 && expect && t[i].k == TK::Ident) {
                if (t[i].s == "let" || t[i].s == "final") continue;
                Sym s; s.name = t[i].s; s.kind = Kind::Parameter; declRow(s, i);
                out.push_back(s); names.push_back(s.name); expect = false;
            }
        }
        return i;
    };

    for (int i = 0; i < N; ++i) {
        if (!snapped && i >= ci) snapshot();
        const Tok& x = t[i];
        const bool before = i < ci;
        if (x.k == TK::Punct) {
            const std::string& p = x.s;
            if (p == "(" || p == "[") ++depthParen;
            else if (p == ")" || p == "]") {
                --depthParen;
                if (pendingDepth >= 0 && depthParen < pendingDepth) { pending.clear(); pendingDepth = -1; pendingFunc = false; }
            } else if (p == "{") {
                Frame f;
                f.syms = pending; f.isFunc = pendingFunc; f.ownerClass = pendingClass; f.isEnum = pendingEnum;
                // كائن حرفي؟ (قيمة)
                bool valuePos = i > 0 && (t[i - 1].s == "=" || t[i - 1].s == "(" || t[i - 1].s == "," ||
                                          t[i - 1].s == ":" || t[i - 1].s == "[" || t[i - 1].s == "return" ||
                                          t[i - 1].s == "yield");
                if (valuePos && pendingClass.empty() && !pendingFunc) {
                    for (const auto& k : objectKeys(t, i)) an.memberWords.insert(k);
                }
                if (pendingEnum) {
                    // أعضاء enum: IDENT في بداية كل مدخل
                    int d = 0;
                    ClassInfo& ci2 = ensureClass(pendingClass);
                    for (int q = i; q < N; ++q) {
                        if (t[q].k == TK::Punct) {
                            if (t[q].s == "{") ++d; else if (t[q].s == "}") { if (--d == 0) break; }
                        }
                        if (d == 1 && t[q].k == TK::Ident && (isTok(t, q - 1, "{") || isTok(t, q - 1, ","))) {
                            Sym m; m.name = t[q].s; m.kind = Kind::EnumMember; m.type = pendingClass; declRow(m, q);
                            ci2.members.push_back(m);
                        }
                    }
                }
                frames.push_back(std::move(f));
                pending.clear(); pendingDepth = -1; pendingClass.clear(); pendingEnum = false; pendingFunc = false;
                headerKw.clear();
            } else if (p == "}") {
                if (frames.size() > 1) frames.pop_back();
                headerKw.clear();
            } else if (p == ";") {
                if (depthParen <= 0) { pending.clear(); pendingDepth = -1; pendingFunc = false; headerKw.clear(); }
            } else if (p == ":" && i > 0 && isIdentTok(t, i - 1) && i + 1 < N && isIdentTok(t, i + 1)) {
                static const std::unordered_set<std::string> loops = {"each", "repeat", "loop", "while", "for", "retry"};
                if (loops.count(t[i + 1].s) && !isTok(t, i - 2, ".")) {
                    Sym l; l.name = t[i - 1].s; l.kind = Kind::Label; declRow(l, i - 1);
                    if (before) an.labels.push_back(l);
                }
            } else if (p == "@" && isTok(t, i + 1, "tmpl") && isIdentTok(t, i + 2)) {
                Sym s; s.name = t[i + 2].s; s.kind = Kind::Template; s.hasParams = true; declRow(s, i + 2);
                s.doc = docAbove(lines, t[i].line);
                if (isTok(t, i + 3, "(")) {
                    std::vector<Sym> ps;
                    int end = parseParams(i + 3, ps, s.params);
                    for (auto& q : ps) pending.push_back(q);
                    pendingDepth = depthParen; pendingFunc = true;
                    i = std::min(end, N - 1);
                }
                an.globals.push_back(s);
                headerKw = "tmpl";
                continue;
            }
            continue;
        }
        if (x.k != TK::Ident) continue;
        const std::string& s = x.s;
        an.idents[s]++;
        if (isTok(t, i - 1, ".")) {          // اسم عضو: obj.name
            an.memberWords.insert(s);
            continue;
        }
        if (s == "let" && isIdentTok(t, i + 1) && !isTok(t, i + 1, "in")) {
            Sym v; v.name = t[i + 1].s; v.kind = Kind::Variable; declRow(v, i + 1);
            v.doc = docAbove(lines, t[i].line);
            if (isTok(t, i + 2, "=")) {
                inferType(t, i + 3, an, v);
                if (isTok(t, i + 3, "{")) v.members = objectKeys(t, i + 3);
            } else if (isTok(t, i + 2, ":") && isIdentTok(t, i + 3)) { v.type = t[i + 3].s; if (an.classes.count(v.type) || v.type == "Set") v.cls = v.type; }
            bool isStatic = isTok(t, i - 1, "static") || isTok(t, i - 2, "static");
            v.isStatic = isStatic;
            if (!atClassBody().empty() && frames.back().ownerClass == atClassBody()) {
                v.kind = Kind::Property;
                ClassInfo& c = ensureClass(atClassBody());
                c.members.push_back(v);
            } else if ((headerKw == "for" || headerKw == "#for") && depthParen > 0) {
                pending.push_back(v); if (pendingDepth < 0) pendingDepth = depthParen;
            } else if (before) {
                frames.back().syms.push_back(v);
            }
            ++i; continue;
        }
        if (s == "fun") {
            int j = i + 1;
            Sym f; f.kind = Kind::Function; f.hasParams = true; f.type = "function";
            if (isIdentTok(t, j)) { f.name = t[j].s; declRow(f, j); ++j; } else declRow(f, i);
            f.doc = docAbove(lines, t[i].line);
            std::vector<Sym> ps;
            int end = j;
            if (isTok(t, j, "(")) { end = parseParams(j, ps, f.params); }
            f.isStatic = isTok(t, i - 1, "static");
            bool inClass = !frames.back().ownerClass.empty();
            if (!f.name.empty()) {
                if (inClass) {
                    f.kind = Kind::Method;
                    ensureClass(frames.back().ownerClass).members.push_back(f);
                } else if (frames.size() == 1) {
                    an.globals.push_back(f);
                    if (before) frames.back().syms.push_back(f);
                } else if (before) {
                    frames.back().syms.push_back(f);
                }
            }
            for (auto& q : ps) pending.push_back(q);
            pendingFunc = true; pendingDepth = depthParen;
            i = std::min(end, N - 1);
            continue;
        }
        if ((s == "class" || s == "struct" || s == "interface" || s == "trait" || s == "enum") && isIdentTok(t, i + 1)) {
            const std::string name = t[i + 1].s;
            ClassInfo& c = ensureClass(name);
            c.kw = s; c.line = t[i + 1].line; c.col = t[i + 1].a;
            c.doc = docAbove(lines, t[i].line);
            for (int q = i + 2; q < N && q < i + 40; ++q) {
                if (t[q].k == TK::Punct && (t[q].s == "{" || t[q].s == ";")) break;
                if (t[q].k == TK::Ident && (t[q].s == "extends" || t[q].s == "implements" || t[q].s == "uses")) continue;
                if (t[q].k == TK::Ident && q > i + 2 && (isTok(t, q - 1, ",") || isTok(t, q - 1, "extends") ||
                    isTok(t, q - 1, "implements") || isTok(t, q - 1, "uses")))
                    c.bases.push_back(t[q].s);
            }
            Sym cs; cs.name = name; cs.line = c.line; cs.col = c.col; cs.doc = c.doc;
            cs.kind = s == "enum" ? Kind::Enum : (s == "interface" || s == "trait") ? Kind::Interface : Kind::Class;
            cs.type = s;
            an.globals.push_back(cs);
            if (before) frames.front().syms.push_back(cs);
            pendingClass = name; pendingEnum = (s == "enum");
            ++i; continue;
        }
        if (s == "each" && !isTok(t, i + 1, "(") && isIdentTok(t, i + 1)) {
            headerKw = "each";
            int rel = 0;
            for (int q = i + 1; q < N && q < i + 90; ++q) {
                if (t[q].k == TK::Punct) {
                    const std::string& p = t[q].s;
                    if (p == "(" || p == "[") ++rel;
                    else if (p == ")" || p == "]") { if (--rel < 0) break; }
                    else if (p == "{" || p == ";") { if (rel <= 0) break; }
                }
                if (rel == 0 && t[q].k == TK::Ident && isTok(t, q + 1, "in") ) {
                    // أسماء هذا البند: الاسم الحالي + (اسم قبله إن كان أول بند: each k, v in)
                    Sym a; a.name = t[q].s; a.kind = Kind::Variable; declRow(a, q);
                    if (isTok(t, q - 1, ",") && isIdentTok(t, q - 2) && q - 3 == i) {
                        Sym b; b.name = t[q - 2].s; b.kind = Kind::Variable; declRow(b, q - 2);
                        pending.push_back(b);
                    }
                    pending.push_back(a);
                }
            }
            if (pendingDepth < 0) pendingDepth = depthParen;
            continue;
        }
        if (s == "repeat" || s == "retry" || s == "loop" || s == "while" || s == "for" || s == "#for" || s == "#while") {
            headerKw = s;
            if (s == "for" || s == "#for") headerKw = "for";
            continue;
        }
        if (s == "as" && isIdentTok(t, i + 1)) {
            static const std::unordered_set<std::string> hk = {"each", "repeat", "retry", "loop", "while", "for", "tmpl"};
            Sym a; a.name = t[i + 1].s; a.kind = Kind::Variable; declRow(a, i + 1);
            if (hk.count(headerKw)) {
                pending.push_back(a); if (pendingDepth < 0) pendingDepth = depthParen;
            } else if (before && i >= 2 && t[i - 1].k == TK::String) {
                a.type = "module"; frames.back().syms.push_back(a);
            }
            ++i; continue;
        }
        if (s == "catch") {
            int j = i + 1; if (isTok(t, j, "(")) ++j;
            if (isIdentTok(t, j)) {
                Sym a; a.name = t[j].s; a.kind = Kind::Variable; a.type = "error"; declRow(a, j);
                pending.push_back(a); if (pendingDepth < 0) pendingDepth = depthParen;
            }
            continue;
        }
    }
    if (!snapped) snapshot();

    // إزالة التكرار (الأقرب يحجب الأبعد)
    std::unordered_set<std::string> seen;
    std::vector<Sym> uniq;
    for (auto& v : an.visible) if (seen.insert(v.name).second) uniq.push_back(v);
    an.visible.swap(uniq);
    return an;
}

// ---------------------------------------------------------------------------------------
// الاستخدام (MRU)
// ---------------------------------------------------------------------------------------

inline std::unordered_map<std::string, int>& usageMap() {
    static std::unordered_map<std::string, int> m;
    return m;
}
inline void recordUse(const std::string& label) {
    int& c = usageMap()[label];
    if (c < 30) ++c;
}

// ---------------------------------------------------------------------------------------
// مولّدات النصوص المساعدة
// ---------------------------------------------------------------------------------------

inline std::string joinParams(const std::vector<std::string>& ps) {
    std::string r;
    for (size_t i = 0; i < ps.size(); ++i) { if (i) r += ", "; r += ps[i]; }
    return r;
}

inline std::vector<std::string> funcParams(const data::FuncSig& f) {
    if (f.params[0]) return splitCsv(f.params);
    std::vector<std::string> v;
    int n = f.maxArgs >= 0 ? f.maxArgs : f.minArgs;
    for (int i = 0; i < n; ++i) {
        std::string p = "arg" + std::to_string(i + 1);
        if (i >= f.minArgs && f.minArgs >= 0) p = "[" + p + "]";
        v.push_back(p);
    }
    return v;
}

inline std::string funcDetail(const data::FuncSig& f) {
    std::string d = "(" + joinParams(funcParams(f)) + ")";
    if (f.minArgs < 0 && !f.params[0]) d = "(…)";
    if (f.ret[0]) d += std::string(" → ") + f.ret;
    return d;
}

inline const char* keywordDoc(const std::string& k) {
    static std::unordered_map<std::string, const char*> m = [] {
        std::unordered_map<std::string, const char*> r;
        for (const auto& d : data::kKeywordDocs) r[d.name] = d.doc;
        return r;
    }();
    auto it = m.find(k);
    return it == m.end() ? "" : it->second;
}

inline const char* namespaceDoc(const std::string& k) {
    for (const auto& d : data::kNamespaceDocs) if (k == d.name) return d.doc;
    return "";
}

inline bool isBuiltinKw(const std::string& s) { return keywordDoc(s)[0] != 0; }

// أعضاء صنف (مع الوراثة)، الأقرب أولاً
inline void collectClassMembers(const Analysis& an, const std::string& cls, std::vector<Sym>& out,
                                std::unordered_set<std::string>& seenClasses, std::unordered_set<std::string>& seenNames) {
    if (!seenClasses.insert(cls).second) return;
    auto it = an.classes.find(cls);
    if (it == an.classes.end()) return;
    for (const Sym& m : it->second.members)
        if (seenNames.insert(m.name).second) out.push_back(m);
    for (const std::string& b : it->second.bases) collectClassMembers(an, b, out, seenClasses, seenNames);
}

inline std::string symDetail(const Sym& s, bool withKw = false) {
    switch (s.kind) {
        case Kind::Function: case Kind::Method: case Kind::Template: {
            std::string d = "(" + joinParams(s.params) + ")";
            return d;
        }
        case Kind::Class: return s.type.empty() ? "class" : s.type;
        case Kind::Enum: return "enum";
        case Kind::Interface: return s.type.empty() ? "interface" : s.type;
        case Kind::EnumMember: return s.type;
        case Kind::Label: return "label";
        case Kind::Parameter: return "param";
        default: return s.type.empty() ? (withKw ? "variable" : "") : s.type;
    }
}

// ---------------------------------------------------------------------------------------
// الإكمال
// ---------------------------------------------------------------------------------------

namespace detail {

inline const std::unordered_set<std::string>& declKeywords() {
    static const std::unordered_set<std::string> s = {"let", "fun", "class", "struct", "interface", "trait", "enum", "as", "catch"};
    return s;
}
inline const std::unordered_set<std::string>& valueKeywords() {
    static const std::unordered_set<std::string> s = {"true", "false", "nil", "and", "or", "fun", "self", "super", "not"};
    return s;
}
inline const std::unordered_set<std::string>& operandWords() {
    static const std::unordered_set<std::string> s = {
        "and", "or", "in", "to", "step", "as", "by", "where", "reversed", "yield", "sorted", "unique", "distinct",
        "skip", "take", "limit", "until", "from", "window", "every", "zip", "desc", "downto", "cross", "pairs", "chain",
        "flat", "times", "on", "instanceof", "extends", "implements", "uses", "else", "when"};
    return s;
}
inline const std::unordered_set<std::string>& exprExpectingWords() {
    static const std::unordered_set<std::string> s = {
        "print", "show", "return", "yield", "in", "and", "or", "when", "until", "unless", "where", "limit", "skip", "take",
        "by", "to", "step", "from", "case", "throw", "not", "elif", "elseif", "ifnot", "match", "goal", "achieve",
        "sum", "product", "count", "avg", "min", "max", "any", "all", "first", "last", "if", "while", "downto", "window",
        "every", "zip", "on", "else", "text"};
    return s;
}
inline const std::unordered_set<std::string>& classOnlyWords() {
    static const std::unordered_set<std::string> s = {"extends", "implements", "uses", "instanceof"};
    return s;
}
inline const std::unordered_set<std::string>& modifierWords() {
    static const std::unordered_set<std::string> s = {"static", "final", "abstract", "override", "public", "private", "protected"};
    return s;
}
inline const std::unordered_set<std::string>& controlCallWords() {
    static const std::unordered_set<std::string> s = {"if", "while", "for", "elif", "elseif", "unless", "match", "catch",
                                                       "return", "print", "show", "switch", "each", "loop", "until", "when", "and", "or", "not", "in"};
    return s;
}

inline const std::vector<std::string>& atTags() {
    static const std::vector<std::string> v = {
        "container", "container.pipe", "container.data", "container.api", "container.import", "container.table",
        "container.doc", "container.object", "container.portal", "container.block", "container.sticker",
        "container.aukt", "container.chatbot", "container.make", "container.everything", "make", "Rin.make", "Object",
        "Containers.Group", "Volume", "view", "theme", "effect", "loop", "import", "tmpl", "body",
        "element.button", "element.input", "element.search", "element.image", "element.video", "element.audio",
        "element.progress", "element.checkbox", "element.radio", "element.switch", "element.slider",
        "element.select", "element.list", "element.column", "element.box", "element.card", "element.sidebar",
        "element.popup", "element.modal", "element.tabs", "element.code_editor", "element.calculator",
        "element.divider", "element.date", "element.time", "element.dropdown", "element.listitem"};
    return v;
}

inline const char* atTagDoc(const std::string& t) {
    if (t == "import") return "استيراد وحدة: @import \"lib/strings.og.rin\";";
    if (t == "tmpl") return "تعريف قالب حلقة قابل لإعادة الاستخدام: @tmpl name(args) ...";
    if (t == "body") return "موضع جسم الحلقة داخل قالب @tmpl.";
    if (t == "theme") return "كتلة ثيم: @theme=Name ... .end/theme";
    if (t == "effect") return "تأثير حركي مخصّص: @effect=Name duration=..; from=\"x:-6\"; at50=\"x:6\"; to=\"x:0\"; ... .end/effect";
    if (t == "loop") return "طبقة اللوحة (canvas): @loop(.Kind)=name ... .end/loop";
    if (t == "view") return "كتلة عرض: @view.<Kind>=name ... .end/view";
    if (t.rfind("element.", 0) == 0) return "عنصر واجهة: @element.<kind>=name ... .end/element";
    if (t.rfind("container", 0) == 0) return "كتلة حاوية: @container.<kind>=name ... .end/container";
    return "كتلة لغة الحاويات.";
}

inline std::string textBefore(const std::string& line, int col) { return line.substr(0, (size_t)std::max(0, std::min<int>(col, (int)line.size()))); }

} // namespace detail

struct Context {
    enum Pos { Stmt, Expr, Operand, DeclName, ClassOnly, Label, Member, At, Hash, ImportPath, TmplKind, None } pos = None;
    bool inPrintImage = false;          // داخل print.image(...): تُقترح mode/cols/invert/color/caption
    bool inPrint = false;               // داخل عبارة print/show أو ترويسة @tmpl print|style: تُقترح سماتها
    std::string prefix;
    int startCol = 0, endCol = 0;
    std::vector<std::string> chain;     // لـ Member
    bool memberOfCall = false;          // الاستقبال نتيجة استدعاء/فهرسة
    int ci = 0;
};

// أقرب استدعاء مفتوح قبل الرمز ci: يعيد فهرس '(' أو -1، ويحسب الفواصل
inline int enclosingCallOpen(const std::vector<Tok>& t, int ci, int& commas) {
    int depth = 0; commas = 0;
    for (int i = ci - 1; i >= 0; --i) {
        const Tok& x = t[i];
        if (x.k != TK::Punct) continue;
        const std::string& p = x.s;
        if (p == ")" || p == "]") ++depth;
        else if (p == "}") ++depth;
        else if (p == "(") { if (depth == 0) return i; --depth; }
        else if (p == "[" || p == "{") { if (depth == 0) return -1; --depth; }
        else if (p == "," && depth == 0) ++commas;
        else if (p == ";" && depth == 0) return -1;
    }
    return -1;
}


// هل المؤشر داخل عبارة print/show (أو ترويسة قالب print/style)؟ تُقترح عندئذ سمات الطباعة.
inline bool inPrintStatement(const std::vector<Tok>& t, int ci) {
    int depth = 0;
    for (int i = ci - 1; i >= 0 && i >= ci - 80; --i) {
        const Tok& x = t[i];
        if (x.k == TK::Punct) {
            const std::string& p = x.s;
            if (p == ")" || p == "]") ++depth;
            else if (p == "(" || p == "[") { if (depth == 0) return false; --depth; }
            else if ((p == ";" || p == "{" || p == "}") && depth == 0) return false;
        } else if (x.k == TK::Ident && depth == 0 && !(i > 0 && t[i - 1].s == ".")) {
            if (x.s == "print" || x.s == "show") return true;
            if (x.s == "style" && i > 0 && t[i - 1].s == ")") return true;
        }
    }
    return false;
}

struct PrintAttr { const char* name; const char* doc; const char* value; };

// داخل print.image( ... ) أو imageText( ... ): معاملات مسمّاة خاصة بالصور
inline bool inPrintImageCall(const std::vector<Tok>& t, int ci) {
    int commas = 0;
    int o = enclosingCallOpen(t, ci, commas);
    if (o < 1) return false;
    if (t[o - 1].s == "image" && o >= 3 && t[o - 2].s == "." && t[o - 3].s == "print") return true;
    return false;
}

inline const std::vector<PrintAttr>& printImageAttrs() {
    static const std::vector<PrintAttr> v = {
        {"mode", "نمط الرسم: blocks|half|ansi256|shade|ascii|braille. مثال: mode=\"braille\"", "\"blocks\""},
        {"cols", "عدد الأعمدة (افتراضي 60، حتى 400). مثال: cols=40", ""},
        {"invert", "قلب الفاتح/الغامق في shade/ascii/braille (invert=true)", "true"},
        {"color", "true يفرض الألوان · false يمنعها · الافتراضي تلقائي", "true"},
        {"caption", "سطر تحت الصورة. مثال: caption=\"Rin\"", "\"\""},
        {"if", "شرط: لا تُطبع الصورة إلا إن تحقق", ""},
    };
    return v;
}

// بعد ترويسة قالب: @tmpl name(params) |  → print / style / text
inline bool afterTmplHeader(const std::vector<Tok>& t, int ci) {
    if (ci < 1 || t[ci - 1].s != ")") return false;
    int depth = 0;
    for (int i = ci - 1; i >= 0; --i) {
        if (t[i].k != TK::Punct) continue;
        if (t[i].s == ")") ++depth;
        else if (t[i].s == "(" && --depth == 0)
            return i >= 3 && t[i - 1].k == TK::Ident && t[i - 2].s == "tmpl" && t[i - 3].s == "@";
    }
    return false;
}

inline const std::vector<PrintAttr>& printAttrs() {
    static const std::vector<PrintAttr> v = {
        {"bold", "غامق (bold=true)", "true"}, {"italic", "مائل (italic=true)", "true"},
        {"underline", "تحته خط (underline=true)", "true"}, {"strike", "يتوسطه خط (strike=true)", "true"},
        {"dim", "باهت (dim=true)", "true"}, {"reverse", "عكس الألوان (reverse=true)", "true"},
        {"blink", "وميض حسب الطرفية (blink=true)", "true"},
        {"color", "لون النص: اسم CSS أو #rrggbb أو rgb(). مثال: color=\"tomato\"", "\"\""},
        {"bg", "لون الخلفية. مثال: bg=\"#112233\"", "\"\""},
        {"width", "عرض الحقل. مثال: width=20", ""}, {"align", "محاذاة داخل width. مثال: align=\"center\"", "\"\""},
        {"upper", "أحرف كبيرة (upper=true)", "true"}, {"lower", "أحرف صغيرة (lower=true)", "true"},
        {"label", "وسم يُطبع مع السطر", "\"\""}, {"level", "مستوى الرسالة. مثال: level=\"warn\" أو \"success\"", "\"\""},
        {"sep", "فاصل بين القيم المتعددة. مثال: sep=\" : \"", "\"\""}, {"end", "ما يُطبع في نهاية السطر", "\"\""},
        {"pretty", "طباعة منسّقة للقيم المركّبة", "true"}, {"repeat", "تكرار الطباعة عدداً من المرات", ""},
        {"if", "شرط: لا يُطبع السطر إلا إن تحقق. مثال: if=total > 10", ""},
    };
    return v;
}

inline Context classifyContext(const std::vector<std::string>& lines, int curLine, int curCol, const Scan& sc, bool explicitInvoke) {
    Context cx;
    const std::string& L = curLine < (int)lines.size() ? lines[curLine] : std::string();
    int col = std::max(0, std::min<int>(curCol, (int)L.size()));
    cx.endCol = col;
    const auto& t = sc.toks;

    // داخل تعليق: لا شيء. داخل نص: فقط مسار @import.
    if (sc.cur.inComment) { cx.pos = Context::None; return cx; }
    if (sc.cur.inString) {
        // @import "|
        int k = sc.cur.tokensBefore;
        if (k >= 2 && isTok(t, k - 1, "import") && isTok(t, k - 2, "@") && sc.cur.strLine == curLine) {
            cx.pos = Context::ImportPath;
            cx.startCol = sc.cur.strContentCol;
            cx.prefix = L.substr((size_t)cx.startCol, (size_t)std::max(0, col - cx.startCol));
            size_t q = L.find('"', (size_t)col);
            cx.endCol = q == std::string::npos ? col : (int)q;
            return cx;
        }
        cx.pos = Context::None; return cx;
    }

    // الكلمة الجارية
    int ws = col;
    while (ws > 0 && isIdChar((unsigned char)L[ws - 1])) --ws;
    cx.prefix = L.substr((size_t)ws, (size_t)(col - ws));
    cx.startCol = ws;
    if (!cx.prefix.empty() && std::isdigit((unsigned char)cx.prefix[0])) { cx.pos = Context::None; return cx; }

    // عدد الرموز قبل بداية الكلمة
    int ci = 0;
    while (ci < (int)t.size() && (t[ci].line < curLine || (t[ci].line == curLine && t[ci].b <= ws))) ++ci;
    cx.ci = ci;

    // @tag
    {
        int s = col;
        while (s > 0 && (isIdChar((unsigned char)L[s - 1]) || L[s - 1] == '.')) --s;
        if (s > 0 && L[s - 1] == '@' && (s - 1 == 0 || !isIdChar((unsigned char)L[s - 2]))) {
            cx.pos = Context::At; cx.startCol = s - 1; cx.prefix = L.substr((size_t)s, (size_t)(col - s)); return cx;
        }
        if (s > 0 && L[s - 1] == '#' && (s - 1 == 0 || !isIdChar((unsigned char)L[s - 2]))) {
            cx.pos = Context::Hash; cx.startCol = s - 1; cx.prefix = L.substr((size_t)s, (size_t)(col - s)); return cx;
        }
    }

    // عضو: ... receiver . word
    if (ws > 0 && L[ws - 1] == '.' && !(ws >= 2 && (std::isdigit((unsigned char)L[ws - 2]) || L[ws - 2] == '.'))) {
        cx.pos = Context::Member;
        int j = ci - 1; // الرمز '.'
        if (j >= 0 && t[j].s == "." && t[j].line == curLine) {
            int r = j - 1;
            if (r >= 0 && t[r].k == TK::Ident) {
                std::vector<std::string> ch{t[r].s};
                while (r - 2 >= 0 && t[r - 1].s == "." && t[r - 2].k == TK::Ident) { ch.insert(ch.begin(), t[r - 2].s); r -= 2; }
                cx.chain = ch;
            } else cx.memberOfCall = true;
        }
        return cx;
    }

    const Tok* p1 = ci >= 1 ? &t[ci - 1] : nullptr;
    const Tok* p2 = ci >= 2 ? &t[ci - 2] : nullptr;
    (void)p2;

    // داخل ترويسة تعريف: fun name(a, | أو @tmpl name(a, |
    {
        int commas = 0;
        int open = enclosingCallOpen(t, ci, commas);
        if (open >= 1 && t[open - 1].k == TK::Ident) {
            int nm = open - 1;
            if ((nm >= 1 && t[nm - 1].s == "fun" && t[nm - 1].k == TK::Ident) ||
                (nm >= 2 && t[nm - 1].s == "tmpl" && t[nm - 2].s == "@")) { cx.pos = Context::DeclName; return cx; }
        } else if (open >= 1 && t[open - 1].s == "fun") { cx.pos = Context::DeclName; return cx; }
    }

    if (p1 && p1->k == TK::Ident) {
        const std::string& w = p1->s;
        if (detail::declKeywords().count(w) && p1->line == curLine) { cx.pos = Context::DeclName; return cx; }
        if (w == "each" && p1->line == curLine) { cx.pos = Context::DeclName; return cx; }
        if ((w == "break" || w == "continue") && p1->line == curLine) { cx.pos = Context::Label; return cx; }
        if (detail::classOnlyWords().count(w)) { cx.pos = Context::ClassOnly; return cx; }
        if (detail::modifierWords().count(w)) { cx.pos = Context::Stmt; return cx; }
    }
    // , بعد extends/implements/uses
    if (p1 && p1->s == ",") {
        for (int q = ci - 2; q >= 0 && q >= ci - 12; --q) {
            if (t[q].k == TK::Ident && detail::classOnlyWords().count(t[q].s) && t[q].s != "instanceof") { cx.pos = Context::ClassOnly; return cx; }
            if (t[q].s == ";" || t[q].s == "{" || t[q].s == "}") break;
        }
    }
    // , في تسمية each: each k, | v in
    if (p1 && p1->s == ",") {
        for (int q = ci - 2; q >= 0 && q >= ci - 3; --q)
            if (t[q].k == TK::Ident && t[q].s == "each") { cx.pos = Context::DeclName; return cx; }
    }

    if (afterTmplHeader(t, ci) && (!p1 || p1->line == curLine)) { cx.pos = Context::TmplKind; return cx; }
    cx.inPrint = inPrintStatement(t, ci);
    cx.inPrintImage = inPrintImageCall(t, ci);

    // الموضع: بداية عبارة؟ تعبير؟ بعد معامل؟
    auto firstOnLine = [&]() { return !p1 || p1->line < curLine; };
    if (!p1) { cx.pos = Context::Stmt; return cx; }
    if (p1->k == TK::Punct) {
        const std::string& p = p1->s;
        if (p == ";" || p == "{" || p == "}") cx.pos = Context::Stmt;
        else if (p == ")" || p == "]") cx.pos = firstOnLine() ? Context::Stmt : Context::Operand;
        else cx.pos = Context::Expr;
        return cx;
    }
    if (p1->k == TK::Number || p1->k == TK::String || p1->k == TK::Hash) {
        cx.pos = firstOnLine() ? Context::Stmt : Context::Operand; return cx;
    }
    // Ident
    if (p1->line < curLine) { cx.pos = Context::Stmt; return cx; }
    if (detail::exprExpectingWords().count(p1->s)) { cx.pos = Context::Expr; return cx; }
    cx.pos = Context::Operand;
    (void)explicitInvoke;
    return cx;
}

inline void pushItem(CompletionResult& res, CompletionItem&& it, const std::string& q, int bonus, std::unordered_set<std::string>& seen) {
    if (!seen.insert(it.label).second) return;
    FuzzyResult f = fuzzyMatch(q, it.label);
    if (!f.ok) return;
    if (!q.empty() && q.size() == it.label.size() && q == it.label) return; // ما كُتب بالكامل
    int mru = 0;
    auto& um = usageMap();
    auto u = um.find(it.label);
    if (u != um.end()) mru = std::min(u->second, 6) * 4;
    it.score = f.score + bonus + mru;
    it.matchPos = std::move(f.pos);
    res.items.push_back(std::move(it));
}

inline CompletionItem funcItem(const data::FuncSig& f, const std::string& label, bool nextIsParen, bool dropFirst = false) {
    CompletionItem it;
    it.label = label;
    it.kind = label.find('.') == std::string::npos && std::string(f.name).find('.') == std::string::npos ? Kind::Function : Kind::Method;
    std::vector<std::string> ps = funcParams(f);
    if (dropFirst && !ps.empty()) ps.erase(ps.begin());
    it.detail = "(" + joinParams(ps) + ")";
    if (f.minArgs < 0 && !f.params[0]) it.detail = "(…)";
    if (f.ret[0]) it.detail += std::string(" → ") + f.ret;
    it.doc = f.doc;
    bool noArgs = dropFirst ? (ps.empty()) : (f.maxArgs == 0);
    it.insertText = label + (nextIsParen ? "" : (noArgs ? "()" : "()"));
    it.cursorBack = nextIsParen ? 0 : (noArgs ? 0 : 1);
    return it;
}

inline CompletionItem symItem(const Sym& s, bool nextIsParen);
inline bool isCtor(const Sym& s) { return s.kind == Kind::Method && s.name == "init"; }

inline CompletionItem symItem(const Sym& s, bool nextIsParen) {
    CompletionItem it;
    it.label = s.name; it.kind = s.kind; it.detail = symDetail(s); it.doc = s.doc;
    it.insertText = s.name;
    if (s.kind == Kind::Function || s.kind == Kind::Method || s.kind == Kind::Template) {
        it.insertText = s.name + (nextIsParen ? "" : "()");
        it.cursorBack = (nextIsParen || s.params.empty()) ? 0 : 1;
        if (s.kind == Kind::Template) { it.label = s.name; }
    }
    return it;
}

inline CompletionResult complete(const std::vector<std::string>& lines, int curLine, int curCol, const Env& env,
                                 bool explicitInvoke, int maxResults) {
    CompletionResult res;
    if (curLine < 0 || curLine >= (int)lines.size()) return res;
    Scan sc = scanDocument(lines, curLine, curCol);
    Context cx = classifyContext(lines, curLine, curCol, sc, explicitInvoke);
    res.startCol = cx.startCol; res.endCol = cx.endCol;
    if (cx.pos == Context::None || cx.pos == Context::DeclName) return res;

    const std::string& L = lines[curLine];
    const bool nextIsParen = cx.endCol < (int)L.size() && L[(size_t)cx.endCol] == '(';
    const std::string& q = cx.prefix;
    // التشغيل التلقائي يتطلب حرفاً على الأقل ما لم يكن سياقاً يستدعيه المحرف الفاصل نفسه
    const bool triggerChar = cx.pos == Context::Member || cx.pos == Context::At || cx.pos == Context::Hash || cx.pos == Context::ImportPath;
    if (q.empty() && !explicitInvoke && !triggerChar) return res;

    Analysis an = analyze(lines, sc.toks, cx.ci);
    std::unordered_set<std::string> seen;
    auto& um = usageMap(); (void)um;

    // ---- @import "…|
    if (cx.pos == Context::ImportPath) {
        res.trigger = Trigger::ImportPath;
        for (const char* m : data::kLibModules) {
            CompletionItem it; it.label = std::string("lib/") + m; it.insertText = it.label; it.kind = Kind::Module;
            it.detail = "وحدة"; it.doc = std::string("وحدة مكتبة قياسية مضمّنة: ") + m;
            pushItem(res, std::move(it), q, 0, seen);
        }
    }
    // ---- @tags
    else if (cx.pos == Context::At) {
        res.trigger = Trigger::At;
        for (const auto& tag : detail::atTags()) {
            CompletionItem it; it.label = "@" + tag; it.insertText = it.label; it.kind = Kind::Tag; it.detail = "وسم";
            it.doc = detail::atTagDoc(tag);
            if (tag == "tmpl") { it.snippet = false; }
            FuzzyResult f = fuzzyMatch(q, tag);
            if (!f.ok) continue;
            if (!seen.insert(it.label).second) continue;
            it.score = f.score; for (auto& p : f.pos) p += 1; it.matchPos = f.pos;
            res.items.push_back(std::move(it));
        }
        for (const Sym& g : an.globals) {
            if (g.kind != Kind::Template) continue;
            CompletionItem it = symItem(g, nextIsParen);
            it.label = "@" + g.name; it.insertText = it.label + (nextIsParen ? "" : "()"); it.cursorBack = (nextIsParen || g.params.empty()) ? 0 : 1;
            it.detail = "(" + joinParams(g.params) + ")"; if (it.doc.empty()) it.doc = "قالب حلقة معرَّف في هذا الملف.";
            FuzzyResult f = fuzzyMatch(q, g.name);
            if (!f.ok || !seen.insert(it.label).second) continue;
            it.score = f.score + 30; for (auto& p : f.pos) p += 1; it.matchPos = f.pos;
            res.items.push_back(std::move(it));
        }
    }
    // ---- #words
    else if (cx.pos == Context::Hash) {
        res.trigger = Trigger::Hash;
        for (const char* w : data::kHashWords) {
            std::string label = w; // يبدأ بـ #
            FuzzyResult f = fuzzyMatch("#" + q, label);
            if (!f.ok) continue;
            CompletionItem it; it.label = label; it.insertText = label; it.kind = Kind::Keyword; it.detail = "#";
            it.score = f.score; it.matchPos = f.pos;
            if (seen.insert(label).second) res.items.push_back(std::move(it));
        }
    }
    // ---- أعضاء
    else if (cx.pos == Context::Member) {
        res.trigger = Trigger::Member;
        std::string ns;
        for (size_t i = 0; i < cx.chain.size(); ++i) { if (i) ns += "."; ns += cx.chain[i]; }
        auto nsMembers = [&](const std::string& prefixNs, bool dropFirst, int bonus) {
            std::string pre = prefixNs + ".";
            for (const auto& f : data::kFuncs) {
                std::string full = f.name;
                if (full.compare(0, pre.size(), pre) != 0) continue;
                std::string rest = full.substr(pre.size());
                size_t dot = rest.find('.');
                if (dot != std::string::npos) {
                    CompletionItem it; it.label = rest.substr(0, dot); it.insertText = it.label; it.kind = Kind::Namespace;
                    it.detail = "namespace";
                    pushItem(res, std::move(it), q, bonus, seen);
                    continue;
                }
                CompletionItem it = funcItem(f, rest, nextIsParen, dropFirst);
                pushItem(res, std::move(it), q, bonus, seen);
            }
        };
        bool handled = false;
        if (!cx.chain.empty()) {
            const std::string& head = cx.chain[0];
            const Sym* var = nullptr;
            for (const Sym& v : an.visible) if (v.name == head) { var = &v; break; }
            // self / super
            if ((head == "self" || head == "super") && cx.chain.size() == 1 && !an.enclosingClass.empty()) {
                std::vector<Sym> ms; std::unordered_set<std::string> sc2, sn;
                if (head == "self") collectClassMembers(an, an.enclosingClass, ms, sc2, sn);
                else {
                    auto it = an.classes.find(an.enclosingClass);
                    if (it != an.classes.end()) for (const auto& b : it->second.bases) collectClassMembers(an, b, ms, sc2, sn);
                }
                for (const Sym& m : ms) if (!isCtor(m)) pushItem(res, symItem(m, nextIsParen), q, 20, seen);
                handled = true;
            }
            // صنف/enum بالاسم: أعضاء ثابتة / حالات
            if (!handled && cx.chain.size() == 1) {
                auto ci = an.classes.find(head);
                if (ci != an.classes.end() && (!var || var->kind == Kind::Class || var->kind == Kind::Enum || var->kind == Kind::Interface)) {
                    std::vector<Sym> ms; std::unordered_set<std::string> sc2, sn;
                    collectClassMembers(an, head, ms, sc2, sn);
                    for (const Sym& m : ms) {
                        if (!isCtor(m) && (ci->second.kw == "enum" ? m.kind == Kind::EnumMember : (m.isStatic))) pushItem(res, symItem(m, nextIsParen), q, 20, seen);
                    }
                    handled = true;
                }
            }
            // متغير من صنف معروف / Set / كائن حرفي
            if (!handled && var && cx.chain.size() == 1) {
                if (var->cls == "Set") { nsMembers("Set", true, 15); handled = true; }
                else if (!var->cls.empty() && an.classes.count(var->cls)) {
                    std::vector<Sym> ms; std::unordered_set<std::string> sc2, sn;
                    collectClassMembers(an, var->cls, ms, sc2, sn);
                    for (const Sym& m : ms) if (!m.isStatic && m.kind != Kind::EnumMember && !isCtor(m)) pushItem(res, symItem(m, nextIsParen), q, 20, seen);
                    handled = true;
                } else if (!var->members.empty()) {
                    for (const std::string& k : var->members) {
                        CompletionItem it; it.label = k; it.insertText = k; it.kind = Kind::Property; it.detail = "حقل";
                        pushItem(res, std::move(it), q, 20, seen);
                    }
                    handled = true;
                }
            }
            // مساحة أسماء: Set. Env. container. container.artifact.
            if (!handled && !var) {
                std::string pre = ns + ".";
                bool any = false;
                for (const auto& f : data::kFuncs) if (std::string(f.name).compare(0, pre.size(), pre) == 0) { any = true; break; }
                if (any) { nsMembers(ns, false, 20); handled = true; }
            }
        }
        // احتياطي: كلمات أعضاء ظهرت في المستند
        if (!handled || res.items.empty()) {
            for (const std::string& w : an.memberWords) {
                CompletionItem it; it.label = w; it.insertText = w; it.kind = Kind::Property; it.detail = "عضو";
                pushItem(res, std::move(it), q, 0, seen);
            }
            if (an.classes.size()) for (const auto& kv : an.classes) for (const Sym& m : kv.second.members)
                if (!m.isStatic && !isCtor(m)) pushItem(res, symItem(m, nextIsParen), q, 2, seen);
        }
    }
    // ---- أسماء أصناف فقط
    else if (cx.pos == Context::ClassOnly) {
        for (const Sym& g : an.globals)
            if (g.kind == Kind::Class || g.kind == Kind::Interface || g.kind == Kind::Enum) pushItem(res, symItem(g, false), q, 30, seen);
    }
    // ---- نوع قالب بعد @tmpl name(...)
    else if (cx.pos == Context::TmplKind) {
        struct K { const char* n; const char* d; };
        for (const K& k : {K{"print", "قالب سطر/أسطر طباعة: @tmpl n(p) print ...;"}, K{"style", "قالب تنسيق سمات print: @tmpl n(p) style bold=true;"},
                           K{"text", "قالب نصي (تعبير): @tmpl n(p) text \"[\" + p + \"]\";"}, K{"each", "قالب حلقة: @tmpl n(xs) each x in xs;"}}) {
            CompletionItem it; it.label = k.n; it.insertText = k.n; it.kind = Kind::Keyword; it.detail = "نوع قالب"; it.doc = k.d;
            pushItem(res, std::move(it), q, 30, seen);
        }
    }
    // ---- تسميات
    else if (cx.pos == Context::Label) {
        for (const Sym& l : an.labels) pushItem(res, symItem(l, false), q, 40, seen);
        CompletionItem w; w.label = "when"; w.insertText = "when"; w.kind = Kind::Keyword; w.doc = keywordDoc("when");
        pushItem(res, std::move(w), q, 10, seen);
    }
    // ---- عام (بداية عبارة / تعبير / بعد معامل)
    else {
        const bool stmt = cx.pos == Context::Stmt, expr = cx.pos == Context::Expr, operand = cx.pos == Context::Operand;
        const bool inClassBody = !an.enclosingClass.empty() && !an.inFunction;

        if (cx.inPrintImage && (operand || expr)) {
            for (const PrintAttr& pa : printImageAttrs()) {
                CompletionItem it; it.label = pa.name; it.insertText = std::string(pa.name) + "=" + pa.value;
                it.kind = Kind::Property; it.detail = "سمة print.image"; it.doc = pa.doc;
                it.cursorBack = (pa.value[0] == '"') ? 1 : 0;
                pushItem(res, std::move(it), q, 44, seen);
            }
        }
        if (cx.inPrint && operand) {
            for (const PrintAttr& pa : printAttrs()) {
                CompletionItem it; it.label = pa.name; it.insertText = std::string(pa.name) + "=" + pa.value;
                it.kind = Kind::Property; it.detail = "سمة print"; it.doc = pa.doc;
                it.cursorBack = (pa.value[0] == '"') ? 1 : 0;
                pushItem(res, std::move(it), q, 44, seen);
            }
        }
        if (!operand || explicitInvoke) {
            // المتغيرات والمعاملات ضمن النطاق
            int rank = 0;
            for (const Sym& v : an.visible) {
                int prox = std::max(0, 8 - rank / 3); ++rank;
                int closer = v.line >= 0 ? std::max(0, 6 - (curLine - v.line) / 8) : 0;
                int base = (v.kind == Kind::Parameter ? 46 : v.kind == Kind::Label ? 10 : 40) + prox / 2 + closer;
                if (v.kind == Kind::Label) continue;
                if (v.kind == Kind::Function || v.kind == Kind::Method) base = 36;
                if (v.kind == Kind::Class || v.kind == Kind::Enum || v.kind == Kind::Interface) base = 34;
                pushItem(res, symItem(v, nextIsParen), q, base, seen);
            }
            // دوال/أصناف/قوالب علوية معرَّفة بعد المؤشر (مرفوعة)
            for (const Sym& g : an.globals) {
                if (g.kind == Kind::Template) continue;
                pushItem(res, symItem(g, nextIsParen), q, 30, seen);
            }
            // مساحات الأسماء الجذرية (Set, Env, container, ...)
            {
                std::unordered_set<std::string> roots;
                for (const auto& f : data::kFuncs) {
                    std::string n = f.name; size_t d = n.find('.');
                    if (d != std::string::npos) roots.insert(n.substr(0, d));
                }
                roots.insert("Set"); roots.insert("Env");
                std::vector<std::string> rs(roots.begin(), roots.end()); std::sort(rs.begin(), rs.end());
                for (const auto& r : rs) {
                    CompletionItem it; it.label = r; it.insertText = r; it.kind = (r == "Set") ? Kind::Class : Kind::Namespace;
                    it.detail = r == "Set" ? "type" : "namespace"; it.doc = namespaceDoc(r);
                    if (r == "Set") { it.insertText = "Set"; }
                    pushItem(res, std::move(it), q, 28, seen);
                }
            }
            // الدوال المدمجة
            for (const auto& f : data::kFuncs) {
                if (std::string(f.name).find('.') != std::string::npos) continue;
                pushItem(res, funcItem(f, f.name, nextIsParen), q, 22, seen);
            }
        }
        // كلمات محجوزة/سياقية/قوالب
        auto isKwAllowed = [&](const std::string& k) {
            if (operand) return detail::operandWords().count(k) > 0;
            if (expr) return detail::valueKeywords().count(k) > 0 || k == "sum" || k == "product" || k == "avg" || k == "first" || k == "last" || k == "each" || k == "match";
            return true;
        };
        auto addKw = [&](const std::string& k, int bonus) {
            if (!explicitInvoke && !isKwAllowed(k)) return;
            if (explicitInvoke && !isKwAllowed(k)) bonus -= 20;
            CompletionItem it; it.label = k; it.insertText = k; it.kind = Kind::Keyword; it.detail = "keyword";
            it.doc = keywordDoc(k);
            if (k == "true" || k == "false" || k == "nil") { it.kind = Kind::Constant; it.detail = "constant"; }
            bool hasSnip = false;
            for (const auto& sn : env.snippets) if (sn.trigger == k) { hasSnip = true; if (it.doc.empty()) it.doc = sn.title; }
            if (hasSnip && stmt) { it.snippet = true; it.kind = Kind::Snippet; it.detail = "snippet"; }
            if (inClassBody && (k == "let" || k == "fun" || k == "static" || k == "final" || k == "abstract" || k == "override" ||
                                k == "public" || k == "private" || k == "protected" || k == "get" || k == "set")) bonus += 12;
            pushItem(res, std::move(it), q, bonus, seen);
        };
        for (const std::string& k : env.keywords) addKw(k, 26);
        for (const std::string& k : env.contextual) addKw(k, 18);
        if (stmt) {
            for (const auto& sn : env.snippets) {
                CompletionItem it; it.label = sn.trigger; it.insertText = sn.trigger; it.kind = Kind::Snippet; it.snippet = true;
                it.detail = "snippet"; it.doc = sn.title;
                pushItem(res, std::move(it), q, 24, seen);
            }
        }
        // كلمات المستند (لا تُعرَّف إلا كمعرّفات)
        if (q.size() >= 2) {
            for (const auto& kv : an.idents) {
                if (kv.first == q) continue;
                CompletionItem it; it.label = kv.first; it.insertText = kv.first; it.kind = Kind::Word; it.detail = "";
                pushItem(res, std::move(it), q, 2 + std::min(kv.second, 4), seen);
            }
        }
    }

    std::stable_sort(res.items.begin(), res.items.end(), [](const CompletionItem& a, const CompletionItem& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.label.size() != b.label.size()) return a.label.size() < b.label.size();
        return a.label < b.label;
    });
    if (maxResults > 0 && (int)res.items.size() > maxResults) res.items.resize((size_t)maxResults);
    return res;
}

// ---------------------------------------------------------------------------------------
// Signature help
// ---------------------------------------------------------------------------------------

inline SignatureHelp signatureHelp(const std::vector<std::string>& lines, int curLine, int curCol) {
    SignatureHelp sh;
    if (curLine < 0 || curLine >= (int)lines.size()) return sh;
    Scan sc = scanDocument(lines, curLine, curCol);
    if (sc.cur.inString || sc.cur.inComment) return sh;
    const auto& t = sc.toks;
    int ci = sc.cur.tokensBefore;
    int commas = 0;
    int open = enclosingCallOpen(t, ci, commas);
    if (open < 1 || t[open - 1].k != TK::Ident) return sh;
    int nm = open - 1;
    const std::string callee = t[nm].s;
    if (detail::controlCallWords().count(callee)) return sh;
    if ((nm >= 1 && t[nm - 1].s == "fun" && t[nm - 1].k == TK::Ident) || (nm >= 2 && t[nm - 1].s == "tmpl" && t[nm - 2].s == "@")) return sh;

    std::vector<std::string> chain{callee};
    int r = nm;
    while (r - 2 >= 0 && t[r - 1].s == "." && t[r - 2].k == TK::Ident) { chain.insert(chain.begin(), t[r - 2].s); r -= 2; }
    bool afterAt = r >= 1 && t[r - 1].s == "@";
    std::string full;
    for (size_t i = 0; i < chain.size(); ++i) { if (i) full += "."; full += chain[i]; }

    Analysis an = analyze(lines, t, nm);
    auto fill = [&](const std::string& name, const std::vector<std::string>& ps, const std::string& ret, const std::string& doc, int kind, bool dropFirst) {
        sh.valid = true; sh.name = name; sh.params = ps; sh.ret = ret; sh.doc = doc; sh.kind = kind;
        if (dropFirst && !sh.params.empty()) sh.params.erase(sh.params.begin());
        sh.active = commas;
    };
    auto fromSym = [&](const Sym& s, int kind) { fill(s.name, s.params, "", s.doc, kind, false); };
    auto fromClassInit = [&](const std::string& cls) -> bool {
        std::vector<Sym> ms; std::unordered_set<std::string> a, b;
        collectClassMembers(an, cls, ms, a, b);
        for (const Sym& m : ms) if (m.name == "init" && m.kind == Kind::Method) {
            fill(cls, m.params, "", an.classes[cls].doc, 2, false); return true; }
        fill(cls, {}, "", an.classes[cls].doc, 2, false); return true;
    };

    if (afterAt && chain.size() == 1) {
        for (const Sym& g : an.globals) if (g.kind == Kind::Template && g.name == callee) { fromSym(g, 3); break; }
    } else if (chain.size() == 1) {
        const Sym* found = nullptr;
        for (const Sym& v : an.visible) if (v.name == callee) { found = &v; break; }
        if (!found) for (const Sym& g : an.globals) if (g.name == callee) { found = &g; break; }
        if (found && (found->kind == Kind::Function || found->kind == Kind::Template)) fromSym(*found, found->kind == Kind::Template ? 3 : 1);
        else if (an.classes.count(callee)) fromClassInit(callee);
        else if (const auto* f = findFunc(callee)) fill(callee, funcParams(*f), f->ret, f->doc, 0, false);
        else if (callee == "Set") fill("Set", {"...items"}, "Set", "ينشئ مجموعة: Set([1,2,2]) أو Set(1,2,3) أو Set().", 0, false);
    } else {
        const std::string& head = chain[0];
        const Sym* var = nullptr;
        for (const Sym& v : an.visible) if (v.name == head) { var = &v; break; }
        bool done = false;
        if (chain.size() == 2 && var && var->cls == "Set") {
            if (const auto* f = findFunc("Set." + callee)) { fill("Set." + callee, funcParams(*f), f->ret, f->doc, 0, true); done = true; }
        }
        if (!done && chain.size() == 2) {
            std::string cls;
            if (head == "self" || head == "super") cls = an.enclosingClass;
            else if (var && !var->cls.empty()) cls = var->cls;
            else if (!var && an.classes.count(head)) cls = head;
            if (!cls.empty() && an.classes.count(cls)) {
                std::vector<Sym> ms; std::unordered_set<std::string> a, b;
                collectClassMembers(an, cls, ms, a, b);
                for (const Sym& m : ms) if (m.name == callee && m.kind == Kind::Method) { fill(cls + "." + callee, m.params, "", m.doc, 1, false); done = true; break; }
            }
        }
        if (!done) if (const auto* f = findFunc(full)) fill(full, funcParams(*f), f->ret, f->doc, 0, false);
    }
    if (sh.valid && !sh.params.empty()) {
        bool variadic = sh.params.back().rfind("...", 0) == 0;
        if (sh.active >= (int)sh.params.size()) sh.active = variadic ? (int)sh.params.size() - 1 : (int)sh.params.size();
    }
    return sh;
}

// ---------------------------------------------------------------------------------------
// Describe (hover)
// ---------------------------------------------------------------------------------------

inline SymbolInfo describe(const std::vector<std::string>& lines, int line, int col) {
    SymbolInfo si;
    if (line < 0 || line >= (int)lines.size()) return si;
    const std::string& L = lines[line];
    int c = std::max(0, std::min<int>(col, (int)L.size()));
    int s = c, e = c;
    while (s > 0 && isIdChar((unsigned char)L[s - 1])) --s;
    while (e < (int)L.size() && isIdChar((unsigned char)L[e])) ++e;
    if (s == e) return si;
    std::string word = L.substr((size_t)s, (size_t)(e - s));
    if (std::isdigit((unsigned char)word[0])) return si;
    // سلسلة نقطية قبل الكلمة: ns.sub.word
    std::vector<std::string> chain{word};
    int p = s;
    while (p >= 2 && L[p - 1] == '.' && isIdChar((unsigned char)L[p - 2])) {
        int q = p - 1; int q0 = q;
        while (q0 > 0 && isIdChar((unsigned char)L[q0 - 1])) --q0;
        chain.insert(chain.begin(), L.substr((size_t)q0, (size_t)(q - q0)));
        p = q0;
    }
    Scan sc = scanDocument(lines, line, s);
    if (sc.cur.inString || sc.cur.inComment) return si;
    Analysis an = analyze(lines, sc.toks, sc.cur.tokensBefore);
    std::string full;
    for (size_t i = 0; i < chain.size(); ++i) { if (i) full += "."; full += chain[i]; }
    const bool atBefore = p >= 1 && L[p - 1] == '@';

    auto fromSym = [&](const Sym& v) {
        si.valid = true; si.name = v.name; si.doc = v.doc; si.defLine = v.line; si.defCol = v.col;
        switch (v.kind) {
            case Kind::Function: case Kind::Method:
                si.kindName = v.kind == Kind::Method ? "method" : "function";
                si.signature = "fun " + v.name + "(" + joinParams(v.params) + ")"; break;
            case Kind::Template: si.kindName = "template"; si.signature = "@tmpl " + v.name + "(" + joinParams(v.params) + ")"; break;
            case Kind::Class: case Kind::Enum: case Kind::Interface: {
                auto it = an.classes.find(v.name);
                std::string kw = it != an.classes.end() && !it->second.kw.empty() ? it->second.kw : "class";
                si.kindName = kw; si.signature = kw + " " + v.name;
                if (it != an.classes.end() && !it->second.bases.empty()) si.signature += " : " + joinParams(it->second.bases);
                break; }
            case Kind::Parameter: si.kindName = "parameter"; si.signature = v.name + (v.type.empty() ? "" : ": " + v.type); break;
            case Kind::Label: si.kindName = "label"; si.signature = v.name + ":"; break;
            case Kind::Property: si.kindName = "field"; si.signature = v.name + (v.type.empty() ? "" : ": " + v.type); break;
            default: si.kindName = "variable"; si.signature = "let " + v.name + (v.type.empty() ? "" : ": " + v.type); break;
        }
    };

    if (chain.size() == 1 && !atBefore) {
        for (const Sym& v : an.visible) if (v.name == word) { fromSym(v); return si; }
        for (const Sym& g : an.globals) if (g.name == word && g.kind != Kind::Template) { fromSym(g); return si; }
    }
    if (atBefore) {
        for (const Sym& g : an.globals) if (g.kind == Kind::Template && g.name == word) { fromSym(g); return si; }
        si.valid = true; si.name = "@" + word; si.kindName = "tag"; si.doc = detail::atTagDoc(word); si.signature = "@" + word; return si;
    }
    if (chain.size() == 1) {
        const char* kd = keywordDoc(word);
        if (kd[0]) { si.valid = true; si.name = word; si.kindName = "keyword"; si.signature = word; si.doc = kd; return si; }
    }
    if (const auto* f = findFunc(full)) {
        si.valid = true; si.name = full; si.kindName = "builtin";
        si.signature = full + "(" + joinParams(funcParams(*f)) + ")" + (f->ret[0] ? std::string(" → ") + f->ret : "");
        si.doc = f->doc; return si;
    }
    if (chain.size() >= 2 && chain[0] != "self") { // طريقة صنف أو Set عبر متغير
        const Sym* var = nullptr;
        for (const Sym& v : an.visible) if (v.name == chain[0]) { var = &v; break; }
        if (var && var->cls == "Set") if (const auto* f = findFunc("Set." + word)) {
            std::vector<std::string> ps = funcParams(*f); if (!ps.empty()) ps.erase(ps.begin());
            si.valid = true; si.name = "Set." + word; si.kindName = "builtin";
            si.signature = "Set." + word + "(" + joinParams(ps) + ")" + (f->ret[0] ? std::string(" → ") + f->ret : "");
            si.doc = f->doc; return si;
        }
    }
    {
        const char* nd = namespaceDoc(full);
        if (nd[0]) { si.valid = true; si.name = full; si.kindName = "namespace"; si.signature = full; si.doc = nd; return si; }
    }
    return si;
}

} // namespace intel
} // namespace rinedit

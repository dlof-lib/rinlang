// rin_editor_tools.h
//
// أدوات المحرر الاحترافية (header-only، بلا أي اعتماد على JNI) فوق rin::Token:
//   1) تلوين سياقي لكلمات الحلقات الجديدة (Loops v2/v3). كلها كلمات "سياقية" في المحلل
//      (each/repeat/loop/retry/where/yield/...) أي أن rin::Lexer يُصدرها IDENT عادياً، فلا
//      يمكن تلوينها من نوع التوكن وحده — هنا نستنتج السياق بنفس منطق rin_parser.cpp.
//   2) نطاقات الطيّ (folding) من الأقواس الفعلية (لا تتأثر بالنصوص/التعليقات).
//   3) هيكل الملف (outline): دوال، أصناف، متغيرات المستوى الأعلى، تسميات الحلقات.
//   4) إعادة محاذاة المستند كاملاً حسب عمق الأقواس (تتجاوز النصوص متعددة الأسطر).
//   5) قوالب (snippets) الحلقات والبنى الشائعة مع علامة المؤشر $0.
//
// الأعمدة هنا بايتات UTF-8 صفرية القاعدة (نفس اصطلاح EditorEngine).
#pragma once

#include "rin_editor_engine.h"
#include "../rin_common.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace rinedit {
namespace tools {

// ---------------------------------------------------------------------------------------
// قوائم الكلمات السياقية
// ---------------------------------------------------------------------------------------

// بادئات حلقات سياقية (تبدأ عبارة/تعبير).
inline const std::vector<std::string>& loopStarters() {
    static const std::vector<std::string> v = {"each", "repeat", "loop", "retry"};
    return v;
}

// معدِّلات داخل ترويسة الحلقة (بين الكلمة الأولى والقوس `{`).
inline const std::unordered_set<std::string>& loopModifiers() {
    static const std::unordered_set<std::string> s = {
        "in", "to", "step", "by", "window", "every", "zip", "skip", "take", "until",
        "where", "reversed", "yield", "sorted", "desc", "unique", "flat", "chain",
        "downto", "cross", "pairs", "from", "limit", "as", "times", "distinct", "on",
    };
    return s;
}

// عوامل الاختزال: sum each x in xs ...
inline const std::unordered_set<std::string>& reducers() {
    static const std::unordered_set<std::string> s = {
        "sum", "product", "count", "avg", "min", "max", "any", "all", "first", "last",
    };
    return s;
}

// كل الكلمات السياقية التي تُقترَح في الإكمال التلقائي (بعد الكلمات المحجوزة).
inline std::vector<std::string> contextualCompletionWords() {
    std::vector<std::string> out = {"each", "repeat", "loop", "retry", "nobreak"};
    for (const char* w : {"where", "reversed", "yield", "limit", "skip", "take", "until",
                          "sorted", "unique", "distinct", "flat", "chain", "downto", "cross", "pairs",
                          "window", "every", "zip", "times", "step", "from", "desc"})
        out.push_back(w);
    for (const char* w : {"sum", "product", "count", "avg", "min", "max", "any", "all", "first", "last"})
        out.push_back(w);
    return out;
}

// ---------------------------------------------------------------------------------------
// 1) التلوين السياقي للحلقات
// ---------------------------------------------------------------------------------------

namespace detail {

using TT = rin::TokenType;

inline bool isIdentTok(const std::vector<rin::Token>& t, size_t i, const char* w = nullptr) {
    if (i >= t.size() || t[i].type != TT::IDENT) return false;
    return w == nullptr || t[i].lexeme == w;
}

inline bool isType(const std::vector<rin::Token>& t, size_t i, TT ty) {
    return i < t.size() && t[i].type == ty;
}

// بداية عبارة: أول توكن، أو بعد `;` `{` `}`، أو بعد `else`، أو بعد تسمية `name:`.
inline bool atStatementStart(const std::vector<rin::Token>& t, size_t i) {
    if (i == 0) return true;
    TT p = t[i - 1].type;
    if (p == TT::SEMICOLON || p == TT::LBRACE || p == TT::RBRACE || p == TT::ELSE) return true;
    if (p == TT::COLON && i >= 2 && t[i - 2].type == TT::IDENT) {
        return i < 3 || atStatementStart(t, i - 2);
    }
    return false;
}

// يبحث عن `{` الجسم في ترويسة عبارة تبدأ عند i (على عمق أقواس صفر) قبل `;` أو نهاية الملف.
// يُرجع فهرس `{` أو -1.
inline long findBodyBrace(const std::vector<rin::Token>& t, size_t from) {
    int depth = 0;
    for (size_t j = from; j < t.size() && j < from + 96; ++j) {
        TT ty = t[j].type;
        if (ty == TT::LPAREN || ty == TT::LBRACKET) depth++;
        else if (ty == TT::RPAREN || ty == TT::RBRACKET) { if (--depth < 0) return -1; }
        else if (depth == 0) {
            if (ty == TT::LBRACE) return (long)j;
            if (ty == TT::SEMICOLON || ty == TT::END_OF_FILE) return -1;
        }
    }
    return -1;
}

// هل في الترويسة [from, to) كلمة `in` على عمق صفر؟
inline bool headerHasIn(const std::vector<rin::Token>& t, size_t from, size_t to) {
    int depth = 0;
    for (size_t j = from; j < to && j < t.size(); ++j) {
        TT ty = t[j].type;
        if (ty == TT::LPAREN || ty == TT::LBRACKET) depth++;
        else if (ty == TT::RPAREN || ty == TT::RBRACKET) depth--;
        else if (depth == 0 && ty == TT::IDENT && t[j].lexeme == "in") return true;
    }
    return false;
}

inline void markKeyword(std::vector<HighlightKind>& kinds, const std::vector<rin::Token>& t, size_t i,
                        HighlightKind k = HighlightKind::Modifier) {
    if (i >= kinds.size()) return;
    if (t[i].type == TT::IDENT) kinds[i] = k;
}

// يلوّن معدِّلات الترويسة من fromTok حتى نهاية النطاق. exprForm: ترويسة تعبير (مصفوفة/قاموس/اختزال)
// فتنتهي عند الإغلاق المطابق أو `;` بدل `{`.
inline void markHeader(const std::vector<rin::Token>& t, std::vector<HighlightKind>& kinds,
                       size_t from, bool exprForm) {
    int depth = 0;
    for (size_t j = from; j < t.size() && j < from + 160; ++j) {
        TT ty = t[j].type;
        if (ty == TT::LPAREN || ty == TT::LBRACKET) { depth++; continue; }
        if (ty == TT::RPAREN || ty == TT::RBRACKET) { if (--depth < 0) return; continue; }
        if (ty == TT::SEMICOLON || ty == TT::END_OF_FILE) return;
        if (ty == TT::LBRACE) {
            if (exprForm) { depth++; continue; }
            if (depth == 0) return; // بداية الجسم
            depth++;
            continue;
        }
        if (ty == TT::RBRACE) { if (--depth < 0) return; continue; }
        if (ty != TT::IDENT) continue;
        // اسم دالة/عضو لا معدِّل: foo.where أو where(...)
        if (j > 0 && t[j - 1].type == TT::DOT) continue;
        if (isType(t, j + 1, TT::LPAREN) || isType(t, j + 1, TT::DOT)) continue;
        if (depth == 0 && loopModifiers().count(t[j].lexeme)) markKeyword(kinds, t, j, HighlightKind::Modifier);
    }
}

} // namespace detail

// يعدّل kinds (بنفس ترتيب tokens) بتلوين كلمات الحلقات السياقية والتسميات.
inline void classifyLoopContext(const std::vector<rin::Token>& t, std::vector<HighlightKind>& kinds) {
    using namespace detail;
    const size_t n = t.size();
    if (kinds.size() != n) return;

    for (size_t i = 0; i < n; ++i) {
        const rin::Token& tok = t[i];

        // --- while/for مع limit، و for x in بلا أقواس ------------------------------------
        if (tok.type == TT::WHILE || tok.type == TT::FOR) {
            long body = findBodyBrace(t, i + 1);
            if (body >= 0) {
                bool paren = isType(t, i + 1, TT::LPAREN);
                if (!paren || tok.type == TT::WHILE) {
                    if (!paren && !headerHasIn(t, i + 1, (size_t)body)) continue;
                    markHeader(t, kinds, i + 1, false);
                } else {
                    // for ( ... ) limit N { — قد تكون الصيغة القديمة؛ نلوّن limit فقط بعد القوس المغلق
                    int depth = 0; size_t j = i + 1;
                    for (; j < (size_t)body; ++j) {
                        if (t[j].type == TT::LPAREN) depth++;
                        else if (t[j].type == TT::RPAREN && --depth == 0) { ++j; break; }
                    }
                    if (j < (size_t)body) markHeader(t, kinds, j, false);
                }
            }
            continue;
        }

        if (tok.type == TT::BREAK || tok.type == TT::CONTINUE) {
            // break name / continue name  (التسمية على نفس السطر وليست when/unless)
            size_t j = i + 1;
            if (isIdentTok(t, j) && t[j].line == tok.line &&
                t[j].lexeme != "when" && t[j].lexeme != "unless") {
                kinds[j] = HighlightKind::Label;
                ++j;
            }
            if (isIdentTok(t, j, "when") || isIdentTok(t, j, "unless")) markKeyword(kinds, t, j, HighlightKind::Modifier);
            continue;
        }

        if (tok.type != TT::IDENT) continue;
        const std::string& w = tok.lexeme;

        // --- تسمية حلقة  name: <loop> ------------------------------------------------------
        if (isType(t, i + 1, TT::COLON) && atStatementStart(t, i)) {
            size_t j = i + 2;
            bool loopNext = isType(t, j, TT::FOR) || isType(t, j, TT::WHILE) ||
                            isIdentTok(t, j, "each") || isIdentTok(t, j, "repeat") ||
                            isIdentTok(t, j, "loop") || isIdentTok(t, j, "retry") ||
                            isIdentTok(t, j, "#do");
            if (loopNext) kinds[i] = HighlightKind::Label;
            continue;
        }

        // --- nobreak { } ------------------------------------------------------------------
        if (w == "nobreak" && isType(t, i + 1, TT::LBRACE) && i > 0 && t[i - 1].type == TT::RBRACE) {
            kinds[i] = HighlightKind::Control;
            continue;
        }

        // --- عوامل الاختزال: sum each x in xs ... ----------------------------------------
        if (reducers().count(w) && isIdentTok(t, i + 1, "each")) {
            kinds[i] = HighlightKind::Modifier;
            kinds[i + 1] = HighlightKind::Control;
            markHeader(t, kinds, i + 2, true);
            ++i;
            continue;
        }

        if (w != "each" && w != "repeat" && w != "loop" && w != "retry") continue;
        if (isType(t, i + 1, TT::EQUAL) || isType(t, i + 1, TT::DOT) || isType(t, i + 1, TT::COMMA) ||
            isType(t, i + 1, TT::RPAREN) || isType(t, i + 1, TT::SEMICOLON) ||
            isType(t, i + 1, TT::COLON)) continue;
        if (i > 0 && t[i - 1].type == TT::DOT) continue;
        if (i > 0 && (t[i - 1].type == TT::LET || t[i - 1].type == TT::FUN)) continue;

        bool stmt = atStatementStart(t, i);
        bool expr = i > 0 && (t[i - 1].type == TT::LBRACKET || t[i - 1].type == TT::LBRACE ||
                              t[i - 1].type == TT::COMMA || t[i - 1].type == TT::LPAREN) && w == "each";

        if (w == "each") {
            if (stmt) {
                long body = findBodyBrace(t, i + 1);
                if (body < 0 || !headerHasIn(t, i + 1, (size_t)body)) continue;
                kinds[i] = HighlightKind::Control;
                markHeader(t, kinds, i + 1, false);
            } else if (expr) {
                // [each x in xs where c yield e]  أو  {each x in xs yield k : v}
                size_t scanEnd = std::min(n, i + 96);
                bool hasIn = false;
                for (size_t j = i + 1; j < scanEnd; ++j)
                    if (isIdentTok(t, j, "in")) { hasIn = true; break; }
                if (!hasIn) continue;
                kinds[i] = HighlightKind::Control;
                markHeader(t, kinds, i + 1, true);
            }
            continue;
        }

        if (!stmt) continue;
        long body = findBodyBrace(t, i + 1);
        if (body < 0) continue;
        if (w == "repeat" || w == "retry") {
            // repeat 3 {  /  repeat n as i {  /  repeat (a + b) {   — وليس نداءً مثل repeat("ab", 2);
            kinds[i] = HighlightKind::Control;
            markHeader(t, kinds, i + 1, false);
        } else { // loop
            bool ok = isType(t, i + 1, TT::LBRACE) || isType(t, i + 1, TT::LPAREN) ||
                      isIdentTok(t, i + 1, "until") || isIdentTok(t, i + 1, "limit");
            if (!ok) continue;
            kinds[i] = HighlightKind::Control;
            markHeader(t, kinds, i + 1, false);
            if (isIdentTok(t, i + 1, "until")) markKeyword(kinds, t, i + 1, HighlightKind::Modifier);
        }
    }
}


// ---------------------------------------------------------------------------------------
// 1b) مفاهيم دلالية: تعريف دالة، أصناف، ثوابت، خصائص، try/catch/throw، كلمات # الخاصة
// ---------------------------------------------------------------------------------------

inline bool isAllCapsConstant(const std::string& s) {
    if (s.size() < 2) return false;
    bool letter = false;
    for (char ch : s) {
        if (ch >= 'A' && ch <= 'Z') letter = true;
        else if (!((ch >= '0' && ch <= '9') || ch == '_')) return false;
    }
    return letter;
}

// يُشغَّل بعد classifyLoopContext (ولا يمسّ إلا ما صُنِّف Ident/Call).
inline void classifyConcepts(const std::vector<rin::Token>& t, std::vector<HighlightKind>& kinds) {
    using namespace detail;
    const size_t n = t.size();
    if (kinds.size() != n) return;
    auto plain = [&](size_t i) { return kinds[i] == HighlightKind::Ident || kinds[i] == HighlightKind::Call; };

    // أسماء الأصناف المعرَّفة في المستند (class/struct/enum X) + مساحات الأسماء القياسية.
    std::unordered_set<std::string> classNames = {"Set", "Env"};
    for (size_t i = 0; i + 1 < n; ++i) {
        if (t[i].type == TT::IDENT && (t[i].lexeme == "class" || t[i].lexeme == "struct" || t[i].lexeme == "enum") &&
            isIdentTok(t, i + 1) && (atStatementStart(t, i) || isIdentTok(t, i - 1 < n ? i - 1 : 0, "abstract") ||
                                      isIdentTok(t, i - 1 < n ? i - 1 : 0, "final"))) {
            classNames.insert(t[i + 1].lexeme);
            kinds[i] = HighlightKind::Keyword;
        }
    }

    for (size_t i = 0; i < n; ++i) {
        const rin::Token& tok = t[i];
        if (tok.type == TT::FUN && isIdentTok(t, i + 1) && isType(t, i + 2, TT::LPAREN)) {
            kinds[i + 1] = HighlightKind::FunctionDef;
            continue;
        }
        if (tok.type != TT::IDENT || !plain(i)) continue;
        const std::string& w = tok.lexeme;
        const bool afterDot = i > 0 && t[i - 1].type == TT::DOT;

        if (!afterDot && classNames.count(w)) { kinds[i] = HighlightKind::Class; continue; }

        // try { } catch (e) { }   /   throw expr;
        if (!afterDot && w == "try" && isType(t, i + 1, TT::LBRACE) && atStatementStart(t, i)) { kinds[i] = HighlightKind::Control; continue; }
        if (!afterDot && w == "catch" && i > 0 && t[i - 1].type == TT::RBRACE &&
            (isType(t, i + 1, TT::LPAREN) || isType(t, i + 1, TT::LBRACE) || isIdentTok(t, i + 1))) { kinds[i] = HighlightKind::Control; continue; }
        if (!afterDot && w == "throw" && atStatementStart(t, i) && !isType(t, i + 1, TT::EQUAL) &&
            !isType(t, i + 1, TT::DOT) && !isType(t, i + 1, TT::SEMICOLON)) { kinds[i] = HighlightKind::Control; continue; }

        // كلمات الهاش: #do/#done حلقات، #to نطاق، والباقي دوال أصلية (#sorted(...)) تبقى Call/Ident.
        if (!w.empty() && w[0] == '#') {
            if (w == "#do" || w == "#done") kinds[i] = HighlightKind::Control;
            else if (w == "#to" || w == "#downto") kinds[i] = HighlightKind::Modifier;
            continue;
        }

        // عضو بعد النقطة بلا نداء: obj.name  (يشترط أن يسبق النقطةَ قيمة، لا وسم container.pipe)
        if (afterDot && kinds[i] == HighlightKind::Ident && i >= 2 &&
            (t[i - 2].type == TT::IDENT || t[i - 2].type == TT::RPAREN || t[i - 2].type == TT::RBRACKET)) {
            kinds[i] = HighlightKind::Property;
            continue;
        }

        // ثابت بأحرف كبيرة MAX_SIZE (وليس نداءً ولا عضواً)
        if (!afterDot && kinds[i] == HighlightKind::Ident && isAllCapsConstant(w)) kinds[i] = HighlightKind::Constant;
    }
}


// ---------------------------------------------------------------------------------------
// قوالب الحلقات: @tmpl name(params) ... ثم @name(args) — يُشغَّل بعد classifyConcepts ويتجاوز تصنيفها
//   @tmpl / @body  -> TemplateKw      اسم القالب (تعريفاً واستعمالاً مع @) -> Template
//   معاملات القالب وأسماء `as` وظهورها داخل الجسم -> Parameter
// ---------------------------------------------------------------------------------------
inline void classifyTemplates(const std::vector<rin::Token>& t, std::vector<HighlightKind>& kinds) {
    using namespace detail;
    const size_t n = t.size();
    if (kinds.size() != n) return;
    auto nameLike = [&](size_t i) {
        if (i >= n || t[i].type == TT::STRING || t[i].type == TT::NUMBER || t[i].lexeme.empty()) return false;
        unsigned char ch = (unsigned char)t[i].lexeme[0];
        return std::isalpha(ch) || ch == '_' || ch >= 0x80;
    };
    auto match = [&](size_t open) -> size_t {
        int d = 0;
        for (size_t i = open; i < n; ++i) {
            TT y = t[i].type;
            if (y == TT::LPAREN || y == TT::LBRACKET || y == TT::LBRACE) d++;
            else if (y == TT::RPAREN || y == TT::RBRACKET || y == TT::RBRACE) { if (--d == 0) return i; }
        }
        return n;
    };
    std::unordered_set<std::string> names;
    for (size_t i = 0; i + 3 < n; ++i) {
        if (t[i].type != TT::AT || !isIdentTok(t, i + 1, "tmpl") || !nameLike(i + 2) || t[i + 3].type != TT::LPAREN) continue;
        kinds[i] = kinds[i + 1] = HighlightKind::TemplateKw;
        kinds[i + 2] = HighlightKind::Template;
        names.insert(t[i + 2].lexeme);
        size_t close = match(i + 3);
        if (close >= n) continue;
        std::unordered_set<std::string> params;
        int depth = 0; bool expectName = true;
        for (size_t j = i + 4; j < close; ++j) {
            TT y = t[j].type;
            if (y == TT::LPAREN || y == TT::LBRACKET || y == TT::LBRACE) depth++;
            else if (y == TT::RPAREN || y == TT::RBRACKET || y == TT::RBRACE) depth--;
            else if (depth == 0 && y == TT::COMMA) { expectName = true; continue; }
            if (depth == 0 && expectName && nameLike(j)) { kinds[j] = HighlightKind::Parameter; params.insert(t[j].lexeme); }
            expectName = false;
        }
        size_t k = close + 1;
        if (isIdentTok(t, k, "as")) {                       // as x, i
            kinds[k] = HighlightKind::Modifier;
            for (++k; k < n && (nameLike(k) || t[k].type == TT::COMMA); ++k)
                if (t[k].type != TT::COMMA && t[k].type != TT::LBRACE) { kinds[k] = HighlightKind::Parameter; params.insert(t[k].lexeme); }
        }
        size_t end = k;                                     // نهاية التعريف: } للكتلة، أو ; / } الجسم الافتراضي
        if (k < n && t[k].type == TT::LBRACE) end = match(k);
        else {
            size_t j = k;
            while (j < n) {
                if (t[j].type == TT::LPAREN || t[j].type == TT::LBRACKET) { j = match(j); if (j >= n) break; ++j; continue; }
                if (t[j].type == TT::SEMICOLON) { end = j; break; }
                if (t[j].type == TT::LBRACE) { end = match(j); break; }
                ++j;
            }
            if (j >= n) end = n - 1;
        }
        for (size_t j = close + 1; j <= end && j < n; ++j) {
            if (t[j].type == TT::AT && isIdentTok(t, j + 1, "body")) { kinds[j] = kinds[j + 1] = HighlightKind::TemplateKw; ++j; continue; }
            if (t[j].type == TT::IDENT && params.count(t[j].lexeme) && !(j > 0 && t[j - 1].type == TT::DOT) &&
                (kinds[j] == HighlightKind::Ident || kinds[j] == HighlightKind::Call || kinds[j] == HighlightKind::Modifier))
                kinds[j] = HighlightKind::Parameter;
        }
    }
    for (size_t i = 0; i + 2 < n; ++i)                      // الاستعمالات @name(
        if (t[i].type == TT::AT && nameLike(i + 1) && t[i + 2].type == TT::LPAREN && names.count(t[i + 1].lexeme) &&
            kinds[i] != HighlightKind::TemplateKw)
            kinds[i] = kinds[i + 1] = HighlightKind::Template;
}


// ---------------------------------------------------------------------------------------
// الرموز: مقارنة/منطق · فواصل خافتة · تدفّق (|> ?) · أقواس ملوّنة بالعمق (3 ألوان بالتناوب)
// قوس إغلاق بلا فتح (أو غير مطابق) يُصنَّف Error. يُشغَّل آخراً ولا يمسّ إلا توكنات الرموز.
// ---------------------------------------------------------------------------------------
inline void classifyPunctuation(const std::vector<rin::Token>& t, std::vector<HighlightKind>& kinds) {
    using namespace detail;
    const size_t n = t.size();
    if (kinds.size() != n) return;
    std::vector<size_t> stack;
    for (size_t i = 0; i < n; ++i) {
        switch (t[i].type) {
            case TT::EQUAL_EQUAL: case TT::BANG_EQUAL: case TT::LESS: case TT::LESS_EQUAL:
            case TT::GREATER: case TT::GREATER_EQUAL: case TT::NOT_SAME: case TT::COPY_OF: case TT::BANG:
                kinds[i] = HighlightKind::Compare; break;
            case TT::AND: case TT::OR:
                kinds[i] = HighlightKind::Compare; break;
            case TT::COMMA: case TT::SEMICOLON: case TT::COLON: case TT::DOT:
                if (kinds[i] == HighlightKind::Operator) kinds[i] = HighlightKind::Punct;
                break;
            case TT::PIPE: case TT::QUESTION:
                kinds[i] = HighlightKind::Flow; break;
            case TT::LPAREN: case TT::LBRACKET: case TT::LBRACE: {
                static const HighlightKind lv[3] = {HighlightKind::Bracket0, HighlightKind::Bracket1, HighlightKind::Bracket2};
                kinds[i] = lv[stack.size() % 3];
                stack.push_back(i);
                break;
            }
            case TT::RPAREN: case TT::RBRACKET: case TT::RBRACE: {
                TT want = t[i].type == TT::RPAREN ? TT::LPAREN : t[i].type == TT::RBRACKET ? TT::LBRACKET : TT::LBRACE;
                if (!stack.empty() && t[stack.back()].type == want) { kinds[i] = kinds[stack.back()]; stack.pop_back(); }
                else kinds[i] = HighlightKind::Error;
                break;
            }
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------------------
// 2) نطاقات الطيّ
// ---------------------------------------------------------------------------------------

struct FoldRange { int startLine; int endLine; }; // صفرية، endLine > startLine، شاملة

inline std::vector<FoldRange> computeFoldRanges(const std::vector<rin::Token>& t) {
    using namespace detail;
    std::vector<FoldRange> out;
    struct Open { int line; TT type; };
    std::vector<Open> stack;
    for (const rin::Token& tok : t) {
        if (tok.type == TT::LBRACE || tok.type == TT::LBRACKET || tok.type == TT::LPAREN) {
            stack.push_back({tok.line - 1, tok.type});
        } else if (tok.type == TT::RBRACE || tok.type == TT::RBRACKET || tok.type == TT::RPAREN) {
            TT want = tok.type == TT::RBRACE ? TT::LBRACE : tok.type == TT::RBRACKET ? TT::LBRACKET : TT::LPAREN;
            for (int k = (int)stack.size() - 1; k >= 0; --k) {
                if (stack[k].type == want) {
                    int sl = stack[k].line, el = tok.line - 1;
                    // يطوي من سطر الفتح حتى السطر *قبل* الإغلاق، فيبقى `}` ظاهراً.
                    if (el - 1 > sl) out.push_back({sl, el - 1});
                    stack.resize(k);
                    break;
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const FoldRange& a, const FoldRange& b) {
        return a.startLine != b.startLine ? a.startLine < b.startLine : a.endLine > b.endLine;
    });
    // سطر واحد = نطاق واحد (الأوسع) حتى لا يتكرر زر الطيّ.
    std::vector<FoldRange> uniq;
    for (const FoldRange& r : out)
        if (uniq.empty() || uniq.back().startLine != r.startLine) uniq.push_back(r);
    return uniq;
}

// ---------------------------------------------------------------------------------------
// 3) هيكل الملف
// ---------------------------------------------------------------------------------------

enum class SymbolKind { Function = 0, Class = 1, Variable = 2, Label = 3 };

struct Symbol {
    int line;       // صفرية
    int col;        // بايت صفري
    SymbolKind kind;
    std::string name;
};

inline std::vector<Symbol> computeOutline(const std::vector<rin::Token>& t) {
    using namespace detail;
    std::vector<Symbol> out;
    int depth = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        TT ty = t[i].type;
        if (ty == TT::LBRACE || ty == TT::LPAREN || ty == TT::LBRACKET) { depth++; continue; }
        if (ty == TT::RBRACE || ty == TT::RPAREN || ty == TT::RBRACKET) { depth = std::max(0, depth - 1); continue; }
        auto push = [&](size_t at, SymbolKind k) {
            out.push_back({t[at].line - 1, std::max(0, t[at].col - 1), k, t[at].lexeme});
        };
        if (ty == TT::FUN && isIdentTok(t, i + 1) && isType(t, i + 2, TT::LPAREN)) {
            push(i + 1, SymbolKind::Function);
        } else if (ty == TT::IDENT && t[i].lexeme == "class" && isIdentTok(t, i + 1)) {
            push(i + 1, SymbolKind::Class);
        } else if (ty == TT::LET && depth == 0 && isIdentTok(t, i + 1)) {
            push(i + 1, SymbolKind::Variable);
        } else if (ty == TT::IDENT && isType(t, i + 1, TT::COLON) && atStatementStart(t, i)) {
            size_t j = i + 2;
            if (isType(t, j, TT::FOR) || isType(t, j, TT::WHILE) || isIdentTok(t, j, "each") ||
                isIdentTok(t, j, "repeat") || isIdentTok(t, j, "loop") || isIdentTok(t, j, "retry"))
                push(i, SymbolKind::Label);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// 4) إعادة المحاذاة
// ---------------------------------------------------------------------------------------

// أي الأسطر يبدأ داخل نص ممتد على عدة أسطر؟ مسح خام للمصدر (سطر/عمود توكن STRING في
// rin::Lexer يشيران إلى نهاية النص لا بدايته، فلا يصلحان لهذا الغرض).
inline std::vector<char> linesStartingInString(const std::vector<std::string>& lines) {
    std::vector<char> res(lines.size(), 0);
    char quote = 0;
    for (size_t l = 0; l < lines.size(); ++l) {
        res[l] = quote ? 1 : 0;
        const std::string& s = lines[l];
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (quote) {
                if (c == '\\') ++i;
                else if (c == quote) quote = 0;
            } else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
                break;
            } else if (c == '"' || c == '\'' || c == '`') {
                quote = c;
            }
        }
    }
    return res;
}

// يُرجع الأسطر بعد إعادة ضبط المسافة البادئة حسب عمق الأقواس. indentUnit مثل "    ".
// الأسطر داخل نص متعدد الأسطر لا تُمَسّ. الأسطر الفارغة تبقى فارغة (بلا مسافات زائدة).
inline std::vector<std::string> reindentLines(const std::vector<std::string>& lines,
                                              const std::vector<rin::Token>& t,
                                              const std::string& indentUnit) {
    using namespace detail;
    const int N = (int)lines.size();
    std::vector<int> firstTok(N, -1);          // فهرس أول توكن يبدأ على السطر
    std::vector<char> insideString = linesStartingInString(lines);
    for (size_t k = 0; k < t.size(); ++k) {
        if (t[k].type == TT::END_OF_FILE || t[k].type == TT::STRING) continue; // سطر/عمود STRING لا يُعتمد (يشير لنهايته)
        int l = t[k].line - 1;
        if (l < 0 || l >= N) continue;
        if (firstTok[l] < 0 || t[firstTok[l]].col > t[k].col) firstTok[l] = (int)k;
    }
    // العمق عند بداية كل سطر = صافي الأقواس في كل التوكنز السابقة.
    std::vector<int> depthAtLine(N, 0);
    {
        int depth = 0;
        size_t k = 0;
        for (int l = 0; l < N; ++l) {
            while (k < t.size() && t[k].line - 1 < l) {
                TT ty = t[k].type;
                if (ty == TT::LBRACE || ty == TT::LPAREN || ty == TT::LBRACKET) depth++;
                else if (ty == TT::RBRACE || ty == TT::RPAREN || ty == TT::RBRACKET) depth = std::max(0, depth - 1);
                ++k;
            }
            depthAtLine[l] = depth;
        }
    }
    auto isCloser = [&](int tokIdx) {
        if (tokIdx < 0) return false;
        TT ty = t[tokIdx].type;
        return ty == TT::RBRACE || ty == TT::RPAREN || ty == TT::RBRACKET;
    };
    std::vector<std::string> out(lines);
    for (int l = 0; l < N; ++l) {
        if (insideString[l]) continue;
        const std::string& s = lines[l];
        size_t ws = 0;
        while (ws < s.size() && (s[ws] == ' ' || s[ws] == '\t')) ++ws;
        if (ws >= s.size() || s[ws] == '\r') { out[l] = ""; continue; }
        int depth = depthAtLine[l];
        if (firstTok[l] >= 0) {
            if (isCloser(firstTok[l])) depth = std::max(0, depth - 1);
        } else {
            // سطر تعليق فقط: يتبع السطر التالي الذي فيه كود إن وُجد.
            int nxt = l + 1;
            while (nxt < N && firstTok[nxt] < 0 && !insideString[nxt]) ++nxt;
            if (nxt < N && firstTok[nxt] >= 0) {
                depth = depthAtLine[nxt];
                if (isCloser(firstTok[nxt])) depth = std::max(0, depth - 1);
            }
        }
        std::string ind;
        for (int d = 0; d < depth; ++d) ind += indentUnit;
        out[l] = ind + s.substr(ws);
    }
    return out;
}

// ---------------------------------------------------------------------------------------
// 5) القوالب (Snippets)
// ---------------------------------------------------------------------------------------

struct Snippet {
    const char* trigger;
    const char* body;   // \n للأسطر، $0 موضع المؤشر النهائي. الإزاحة النسبية بـ4 مسافات لكل مستوى.
    const char* title;  // وصف قصير بالعربية
};

inline const std::vector<Snippet>& snippets() {
    static const std::vector<Snippet> v = {
        {"each",     "each $0 in  {\n    \n}",                          "تكرار على عناصر"},
        {"eachi",    "each x, i in $0 {\n    \n}",                      "تكرار مع الفهرس"},
        {"eachk",    "each k, v in $0 {\n    \n}",                      "تكرار على قاموس (مفتاح، قيمة)"},
        {"eachw",    "each x in $0 where  {\n    \n}",                  "تكرار مع مرشّح where"},
        {"eachd",    "each x in $0 distinct {\n    \n}",                  "تكرار بلا تكرار (أول ظهور لكل قيمة)"},
        {"eachp",    "each x in $0, y in  {\n    \n}",                  "ضرب ديكارتي بحلقة واحدة"},
        {"eachr",    "each x in $0 reversed {\n    \n}",                "تكرار عكسي"},
        {"eachn",    "each i in 1 to $0 {\n    \n}",                    "تكرار على مدى رقمي"},
        {"repeat",   "repeat $0 {\n    \n}",                            "تكرار عدد ثابت"},
        {"repeati",  "repeat $0 as i {\n    \n}",                       "تكرار عدد ثابت مع فهرس"},
        {"loop",     "loop {\n    $0\n    break when ();\n}",           "حلقة لا نهائية حتى break"},
        {"loopu",    "loop until ($0) {\n    \n}",                      "حلقة حتى يتحقق الشرط"},
        {"loopl",    "loop limit $0 {\n    \n}",                        "حلقة بسقف أمان"},
        {"retry",    "retry $0 as attempt {\n    \n}",                  "إعادة المحاولة عند الفشل"},
        {"tmpl",     "@tmpl $0(xs) each x in xs;",                      "قالب حلقة قابل لإعادة الاستخدام (@name(args) { ... })"},
        {"tmplb",    "@tmpl $0(xs) as x {\n    each x in xs {\n        @body\n    }\n}", "قالب حلقة كتلة مع @body"},
        {"nobreak",  "nobreak {\n    $0\n}",                           "يُنفَّذ إن انتهت الحلقة دون break"},
        {"label",    "outer: each $0 in  {\n    \n}",                  "حلقة مسمّاة (break outer / continue outer)"},
        {"comp",     "[each x in $0 where  yield x]",                   "تجميع نتائج في مصفوفة"},
        {"sum",      "sum each x in $0 yield x",                        "مجموع بتعبير اختزال"},
        {"fun",      "fun $0() {\n    \n}",                            "تعريف دالة"},
        {"if",       "if ($0) {\n    \n}",                             "شرط"},
        {"ife",      "if ($0) {\n    \n} else {\n    \n}",             "شرط مع else"},
        {"while",    "while ($0) limit 100000 {\n    \n}",             "while بسقف أمان"},
        {"for",      "for (let i = 0; i < $0; i = i + 1) {\n    \n}",   "for على طراز C"},
    };
    return v;
}

inline const Snippet* findSnippet(const std::string& trigger) {
    for (const Snippet& s : snippets())
        if (trigger == s.trigger) return &s;
    return nullptr;
}

// يوسّع قالباً: baseIndent تُضاف لكل سطر بعد الأول. يُرجع النص ويكتب موضع المؤشر (سطر نسبي، بايت).
inline std::string expandSnippetText(const Snippet& sn, const std::string& baseIndent,
                                     int* caretLineRel, int* caretCol) {
    std::string raw = sn.body, out;
    int line = 0, col = 0;
    int cl = -1, cc = -1;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '$' && i + 1 < raw.size() && raw[i + 1] == '0') {
            cl = line; cc = col; ++i; continue;
        }
        if (raw[i] == '\n') {
            out += '\n'; out += baseIndent;
            line++; col = (int)baseIndent.size();
        } else {
            out += raw[i]; col++;
        }
    }
    if (cl < 0) { cl = line; cc = col; }
    *caretLineRel = cl; *caretCol = cc;
    return out;
}

} // namespace tools
} // namespace rinedit

// rin_passkit_highlight.h
//
// مُلوِّن ملفات `.passkit` (لغة الوسوم <tag attr="..."/>) للمحرر الأصلي. header-only وبلا أي
// اعتماد على rin::Lexer، فلا يتأثر بأخطاء Lexer عند كتابة XML-like (كان كل ملف .passkit يُلوَّن
// سابقاً كأنه Rin فيخرج تلويناً خاطئاً وأخطاء حمراء كاذبة).
//
// الألوان الرسمية معرَّفة في examples/customlang/passkit/syntax.rinsyntax.json وتُترجَم في
// Kotlin (RinCodeEditorView.colorForKind) عبر أنواع التلوين الجديدة 13..19 أدناه.
//
// ضمانات الأمان:
//  - لا استثناءات: كل الفهارس مفحوصة، ولا تعتمد الحلقات على تقدّم "متوقَّع" بل تُجبَر على التقدّم.
//  - كل الأعمدة إزاحات بايت UTF-8 (نفس اصطلاح المحرك)؛ الرموز الهيكلية كلها ASCII فلا تُقطَّع
//    الحروف متعددة البايتات (البايتات >= 0x80 تُعامَل كأحرف معرِّف).
//  - الامتدادات المُنتَجة غير متداخلة ومرتّبة بحسب (سطر، عمود).
#pragma once

#include "rin_editor_engine.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rinedit {
namespace passkit {

// أنواع التلوين الخاصة بـ passkit — تطابق HighlightKind في Kotlin حرفياً.
constexpr int kBracket   = 13; // < </ > />  (كهرماني، لون أقواس أيقونة passkit)
constexpr int kTagName   = 14; // وسم معروف: email password apikey ...
constexpr int kAttr      = 15; // اسم سمة
constexpr int kVariable  = 16; // $var و {var}
constexpr int kCustomTag = 17; // وسم غير معروف (قد يكون مُسجَّلاً عبر passkitRegister)
constexpr int kString    = 18; // قيمة سمة "..."
constexpr int kComment   = 19; // <!-- ... -->
constexpr int kDirective = 12; // <?...?> و <!DOCTYPE ...>  (يطابق PREPROCESSOR)

inline const std::vector<std::string>& tagNames() {
    static const std::vector<std::string> names = {
        "passkit", "set", "print", "if", "else", "for", "assert", "email", "password", "apikey",
        "link", "api", "sql", "input", "return", "import", "run", "call", "crypt", "token",
        "otp", "db", "container"};
    return names;
}

inline bool isKnownTag(const std::string& name) {
    for (const std::string& n : tagNames()) {
        if (n == name) return true;
    }
    return false;
}

inline bool isIdentStart(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}
inline bool isIdentChar(unsigned char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}
inline bool isTagNameChar(unsigned char c) {
    return isIdentChar(c) || c == '-' || c == ':' || c == '.';
}
inline bool isSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

class Highlighter {
public:
    explicit Highlighter(const std::vector<std::string>& lines) : lines_(lines) {}

    std::vector<HighlightSpan> run() {
        out_.clear();
        state_ = State::Text;
        quote_ = 0;
        for (size_t li = 0; li < lines_.size(); ++li) {
            scanLine(static_cast<int>(li), lines_[li]);
        }
        return mergeAdjacent(std::move(out_));
    }

    // يدمج الامتدادات المتلاصقة من نفس النوع (مثل علامتي التنصيص مع نص السمة) لتقليل عدد
    // عمليات الرسم في المحرر.
    static std::vector<HighlightSpan> mergeAdjacent(std::vector<HighlightSpan> in) {
        std::vector<HighlightSpan> res;
        res.reserve(in.size());
        for (const HighlightSpan& sp : in) {
            if (!res.empty() && res.back().line == sp.line && res.back().kind == sp.kind &&
                res.back().endCol == sp.startCol) {
                res.back().endCol = sp.endCol;
            } else {
                res.push_back(sp);
            }
        }
        return res;
    }

private:
    enum class State { Text, Comment, Directive, TagName, InTag, Quoted };

    const std::vector<std::string>& lines_;
    std::vector<HighlightSpan> out_;
    State state_ = State::Text;
    char quote_ = 0;

    void emit(int line, size_t s, size_t e, int kind) {
        if (e <= s) return;
        out_.push_back({line, static_cast<int>(s), static_cast<int>(e), static_cast<HighlightKind>(kind)});
    }

    // يطابق $name(.name)* ؛ يُرجع نهاية المطابقة أو start إن لم يوجد.
    static size_t matchDollarVar(const std::string& t, size_t i) {
        if (i + 1 >= t.size() || t[i] != '$' || !isIdentStart(static_cast<unsigned char>(t[i + 1]))) return i;
        size_t j = i + 1;
        while (j < t.size()) {
            unsigned char c = static_cast<unsigned char>(t[j]);
            if (isIdentChar(c)) { ++j; continue; }
            // نقطة تتبعها بداية معرِّف فقط (حتى لا تُبتلَع نقطة نهاية الجملة)
            if (c == '.' && j + 1 < t.size() && isIdentStart(static_cast<unsigned char>(t[j + 1]))) { ++j; continue; }
            break;
        }
        return j;
    }

    // يطابق {name.path[0]} داخل السطر؛ يُرجع نهاية المطابقة (بعد '}') أو start إن لم يوجد.
    static size_t matchBraceVar(const std::string& t, size_t i) {
        if (i + 1 >= t.size() || t[i] != '{' || !isIdentStart(static_cast<unsigned char>(t[i + 1]))) return i;
        size_t j = i + 1;
        while (j < t.size()) {
            unsigned char c = static_cast<unsigned char>(t[j]);
            if (isIdentChar(c) || c == '.' || c == '[' || c == ']' || c == '-') { ++j; continue; }
            break;
        }
        if (j < t.size() && t[j] == '}') return j + 1;
        return i;
    }

    // يُرسل [s,e) كنص عادي مع استخراج المتغيّرات؛ textKind = النوع الأساسي للأجزاء غير المتغيّرة
    // (kString داخل السمات، أو -1 لعدم الإرسال في النص الخارجي).
    void emitWithVars(int line, const std::string& t, size_t s, size_t e, int baseKind) {
        size_t segStart = s;
        size_t i = s;
        while (i < e) {
            size_t before = i;
            size_t end = i;
            if (t[i] == '$') end = matchDollarVar(t, i);
            else if (t[i] == '{') end = matchBraceVar(t, i);
            if (end > i && end <= e) {
                if (baseKind >= 0) emit(line, segStart, i, baseKind);
                emit(line, i, end, kVariable);
                i = end;
                segStart = i;
            } else {
                ++i;
            }
            if (i <= before) i = before + 1; // حاجز تقدّم قاطع
        }
        if (baseKind >= 0 && segStart < e) emit(line, segStart, e, baseKind);
    }

    void scanLine(int li, const std::string& t) {
        const size_t n = t.size();
        size_t i = 0;
        size_t textStart = 0; // بداية مقطع نص خارجي لم يُرسَل بعد (للمتغيّرات فقط)

        auto flushText = [&](size_t upto) {
            if (upto > textStart) emitWithVars(li, t, textStart, upto, -1);
            textStart = upto;
        };

        while (i < n) {
            const size_t loopStart = i;
            switch (state_) {
            case State::Comment: {
                size_t end = t.find("-->", i);
                if (end == std::string::npos) { emit(li, i, n, kComment); i = n; }
                else { emit(li, i, end + 3, kComment); i = end + 3; state_ = State::Text; textStart = i; }
                break;
            }
            case State::Directive: {
                size_t end = t.find('>', i);
                if (end == std::string::npos) { emit(li, i, n, kDirective); i = n; }
                else { emit(li, i, end + 1, kDirective); i = end + 1; state_ = State::Text; textStart = i; }
                break;
            }
            case State::Quoted: {
                // قيمة سمة مفتوحة (قد تمتد لعدة أسطر): نبحث عن علامة الإغلاق
                size_t j = i;
                while (j < n && t[j] != quote_) ++j;
                if (j < n) {
                    emitWithVars(li, t, i, j, kString);
                    emit(li, j, j + 1, kString);
                    i = j + 1;
                    state_ = State::InTag;
                } else {
                    emitWithVars(li, t, i, n, kString);
                    i = n;
                }
                break;
            }
            case State::TagName: {
                // بعد '<' أو '</': اسم الوسم
                size_t j = i;
                while (j < n && isTagNameChar(static_cast<unsigned char>(t[j]))) ++j;
                if (j > i) {
                    std::string name = t.substr(i, j - i);
                    emit(li, i, j, isKnownTag(name) ? kTagName : kCustomTag);
                    i = j;
                }
                state_ = State::InTag;
                break;
            }
            case State::InTag: {
                unsigned char c = static_cast<unsigned char>(t[i]);
                if (isSpace(c)) { ++i; break; }
                if (c == '>') { emit(li, i, i + 1, kBracket); ++i; state_ = State::Text; textStart = i; break; }
                if (c == '/' && i + 1 < n && t[i + 1] == '>') {
                    emit(li, i, i + 2, kBracket); i += 2; state_ = State::Text; textStart = i; break;
                }
                if (c == '"' || c == '\'') {
                    quote_ = static_cast<char>(c);
                    emit(li, i, i + 1, kString);
                    ++i;
                    state_ = State::Quoted;
                    break;
                }
                if (c == '=') { ++i; break; }
                if (c == '$') {
                    size_t end = matchDollarVar(t, i);
                    if (end > i) { emit(li, i, end, kVariable); i = end; break; }
                }
                if (isIdentStart(c)) {
                    size_t j = i;
                    while (j < n) {
                        unsigned char d = static_cast<unsigned char>(t[j]);
                        if (isIdentChar(d) || d == '-' || d == ':' || d == '.') ++j; else break;
                    }
                    // مسافات ثم '=' => اسم سمة؛ وإلا قيمة بلا تنصيص (إن سبقتها '=') أو سمة منطقية
                    emit(li, i, j, kAttr);
                    i = j;
                    break;
                }
                // أي رمز آخر داخل الوسم (نادر): تخطَّه بلا تلوين حتى لا نعلق
                ++i;
                break;
            }
            case State::Text: {
                unsigned char c = static_cast<unsigned char>(t[i]);
                if (c == '<') {
                    if (t.compare(i, 4, "<!--") == 0) {
                        flushText(i);
                        size_t end = t.find("-->", i + 4);
                        if (end == std::string::npos) { emit(li, i, n, kComment); i = n; state_ = State::Comment; }
                        else { emit(li, i, end + 3, kComment); i = end + 3; textStart = i; }
                        break;
                    }
                    if (i + 1 < n && (t[i + 1] == '?' || t[i + 1] == '!')) {
                        flushText(i);
                        size_t end = t.find('>', i + 2);
                        if (end == std::string::npos) { emit(li, i, n, kDirective); i = n; state_ = State::Directive; }
                        else { emit(li, i, end + 1, kDirective); i = end + 1; textStart = i; }
                        break;
                    }
                    if (i + 1 < n && t[i + 1] == '/' && i + 2 < n && isIdentStart(static_cast<unsigned char>(t[i + 2]))) {
                        flushText(i);
                        emit(li, i, i + 2, kBracket);
                        i += 2;
                        state_ = State::TagName;
                        break;
                    }
                    if (i + 1 < n && isIdentStart(static_cast<unsigned char>(t[i + 1]))) {
                        flushText(i);
                        emit(li, i, i + 1, kBracket);
                        i += 1;
                        state_ = State::TagName;
                        break;
                    }
                    // '<' مجرد (مثل a < b) نص عادي
                    ++i;
                    break;
                }
                ++i;
                break;
            }
            }
            if (i <= loopStart && state_ == State::Text) i = loopStart + 1; // حاجز تقدّم قاطع
            else if (i < loopStart) i = loopStart + 1;
            // الحالات التي تغيّر state_ دون تقدّم (TagName→InTag) مسموحة لمرة واحدة فقط لكل انتقال
            // لأن الحالة التالية تتقدّم دائماً؛ لذا لا حلقة لا نهائية ممكنة.
        }

        // نهاية السطر: أرسل ما تبقّى من النص الخارجي (متغيّرات)، ثم أعد الحالات أحادية السطر إلى Text.
        if (state_ == State::Text) flushText(n);
        else if (state_ == State::TagName) state_ = State::InTag; // السطر انتهى بعد '<' مباشرة
        // Comment / Directive / InTag / Quoted تستمر عبر الأسطر عمداً.
    }
};

} // namespace passkit

// نقطة الدخول المستخدمة من EditorEngine::computeHighlights().
inline std::vector<HighlightSpan> computePasskitHighlights(const std::vector<std::string>& lines) {
    try {
        return passkit::Highlighter(lines).run();
    } catch (...) {
        return {}; // لا تُعطِّل الواجهة أبداً بسبب التلوين
    }
}

} // namespace rinedit

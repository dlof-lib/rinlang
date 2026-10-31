// rin_indsin_highlight.h
//
// مُلوِّن مستندات indsin للمحرر الأصلي (header-only، بلا اعتماد على rin::Lexer).
//
// لماذا مُلوِّن مستقل: كان .indsin يمرّ على Lexer لغة Rin، فتظهر الوسوم <Column#root gap=16> بلا
// أي تمييز (أو بأخطاء كاذبة)، ولا يُفرَّق بين اسم المكوّن والسمة وقيمة التصميم واللون وخلية warp.
// هنا يُلوَّن المستند بأدوار دلالية واضحة (36..47) تُترجَم لألوان في RinCodeEditorView.colorForKind:
//
//   <  </  >  />        IxBracket   أقواس الوسم (كهرماني)
//   Column Button …     IxTag       مكوّن معروف (تيل)
//   MyWidget            IxTagCustom مكوّن مخصّص/غير معروف
//   #root               IxId        معرّف العنصر
//   gap padding …       IxAttr      اسم سمة
//   onTap onLongPress   IxEvent     معالج حدث
//   "primary" "pill"    IxToken     قيمة من قيم نظام التصميم
//   #3498db             IxColor     لون
//   16 12px 300ms 50%   IxLength    رقم/طول بوحدة
//   count (خلية warp)   IxState     خلية حالة حيّة — في أي مكان تُقرأ فيه
//   {  }                IxBrace     حدّا التعبير المضمَّن
//   @view .end/view     IxStruct    بنية الصياغة القديمة + وسوم التوجيه (Define Slot Repeat If Else Fun Rin …)
//   <<expr>> <<each …>> IxBrace     جسر Rin: الحدّان << >> وما بينهما تعبير Rin
//
// وداخل التعبيرات { … } تُستعمل أنواع Rin الموجودة (نص/رقم/نداء/ثابت/تحكّم/مقارنة/أقواس ملوّنة بالعمق).
//
// ضمانات الأمان (نفس مبادئ rin_passkit_highlight.h):
//  - لا استثناءات؛ كل الفهارس مفحوصة، وكل حلقة مجبرة على التقدّم.
//  - الأعمدة إزاحات بايت UTF-8؛ كل الرموز الهيكلية ASCII، والبايتات >= 0x80 تُعامَل كأحرف معرِّف.
//  - المقاطع المُنتَجة مرتّبة بحسب (سطر، عمود) وغير متداخلة.
#pragma once

#include "rin_editor_engine.h"

#include <cstddef>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace rinedit {
namespace indsinhl {

// أنواع التلوين الخاصة بـ indsin — تطابق HighlightKind في Kotlin حرفياً.
constexpr int kBracket   = 36;
constexpr int kTag       = 37;
constexpr int kTagCustom = 38;
constexpr int kId        = 39;
constexpr int kAttr      = 40;
constexpr int kEvent     = 41;
constexpr int kToken     = 42;
constexpr int kColor     = 43;
constexpr int kLength    = 44;
constexpr int kState     = 45;
constexpr int kBrace     = 46;
constexpr int kStruct    = 47;

// أنواع Rin الموجودة التي نعيد استعمالها داخل التعبيرات.
constexpr int kKeyword  = 1;
constexpr int kString   = 2;
constexpr int kNumber   = 3;
constexpr int kOperator = 5;
constexpr int kAt       = 7;
constexpr int kCall     = 9;
constexpr int kComment  = 10;
constexpr int kConstant = 20;
constexpr int kFuncDef  = 21;
constexpr int kControl  = 22;
constexpr int kProperty = 26;
constexpr int kCompare  = 30;
constexpr int kPunct    = 31;
constexpr int kFlow     = 32;
constexpr int kBr0      = 33; // 33,34,35 بالتناوب حسب العمق

inline bool isIdentStart(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80;
}
inline bool isIdentChar(unsigned char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }
inline bool isTagNameChar(unsigned char c) { return isIdentChar(c) || c == '-' || c == ':' || c == '.'; }
inline bool isSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
inline bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
inline bool isHex(unsigned char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline const std::unordered_set<std::string>& knownTags() {
    static const std::unordered_set<std::string> s = {
        "Text", "Image", "Button", "Card", "Column", "Row", "Stack", "Divider", "Header", "TopBar",
        "BottomBar", "Drawer", "Menu", "MenuItem", "Table", "TableRow", "Video", "Audio", "WebView",
        "Scaffold", "Splash", "Banner", "Box", "Grid", "Wrap", "Spacer", "Badge", "Progress",
        "Checkbox", "Switch", "Avatar", "Input", "TextArea", "Dialog", "Tabs", "TabItem", "Tooltip",
        "Icon", "IconButton", "Object", "Link", "Radio", "Slider", "Select", "List", "ListItem",
        "File", "Date", "Time", "CodeEditor", "Calculator", "Search", "Tag", "Kbd", "Rating",
        "Skeleton", "Spinner", "Steps", "StepItem", "Timeline", "TimelineItem", "Breadcrumb",
        "Pagination", "DonutChart", "Custom",
        "Screen", "Scroll", "Hero", "Section", "LoginForm", "Field", "PasswordField", "Stat",
        "Alert", "ListTile", "EmptyState", "ProfileCard", "ConfirmCard", "Toggle", "Stepper",
        "breadcrumbs", "chip", "container", "datepicker", "donut", "dropdown", "element",
        "filepicker", "frame", "key", "loader", "modal", "pager", "panel", "piechart", "popup",
        "range", "searchbar", "sidebar", "step", "stepper", "textfield", "timelineevent",
        "timepicker"};
    return s;
}

// وسوم توجيه المترجم (لا تُنتج عناصر بذاتها): قوالب/تكرار/شروط/كود Rin/ثيم/استيراد.
inline bool isDirectiveTag(const std::string& name) {
    static const std::unordered_set<std::string> s = {
        "Define", "Slot", "Repeat", "If", "Else", "Fun", "Rin", "Import", "Loop", "Container",
        "Element", "Palette", "Theme", "Tokens", "Style"};
    size_t dot = name.find('.');
    return s.count(dot == std::string::npos ? name : name.substr(0, dot)) != 0;
}

inline const std::unordered_set<std::string>& tokenValues() {
    static const std::unordered_set<std::string> s = {
        "action", "auto", "back", "background", "body", "bold", "bottom", "bounce", "bounceout",
        "caption", "center", "code", "comfortable", "compact", "contain", "cover", "cubicin",
        "cubicinout", "cubicout", "custom", "danger", "dark", "display", "ease", "easein",
        "easeinout", "easeout", "elastic", "elevated", "end", "error", "fade", "fast", "fill",
        "filled", "forest", "ghost", "glass", "gradient", "grow", "heading", "headline",
        "highcontrastdark", "highcontrastlight", "info", "instant", "italic", "label", "large",
        "left", "lg", "light", "linear", "link", "ltr", "md", "medium", "midnight", "neutral",
        "none", "normal", "ocean", "outline", "outlined", "phone", "tablet", "desktop", "pill",
        "primary", "progress", "pulse", "right", "rose", "round", "rtl", "secondary", "sepia",
        "shake", "sharp", "sine", "slate", "slideDown", "slideLeft", "slideRight", "slideUp",
        "slow", "slower", "sm", "small", "soft", "spacious", "spring", "start", "stretch",
        "subtitle", "success", "sunset", "surface", "title", "tonal", "top", "underline",
        "warning", "xl", "xs"};
    return s;
}

inline bool isControlWord(const std::string& w) {
    static const std::unordered_set<std::string> s = {
        "if", "else", "while", "for", "return", "break", "continue", "each", "repeat", "loop",
        "retry", "try", "catch", "throw", "when", "unless"};
    return s.count(w) != 0;
}
// كلمات تصريح لا تُقرأ كاسم سمة حتى لو تلتها = (text/set/print سمات شائعة في indsin: text="..." ).
inline bool isDeclKeyword(const std::string& w) {
    return w == "let" || w == "fun" || w == "warp" || w == "import" || w == "reckon" || w == "where" || w == "as";
}
inline bool isKeywordWord(const std::string& w) {
    static const std::unordered_set<std::string> s = {
        "let", "fun", "warp", "text", "print", "show", "reckon", "where", "import", "set", "as"};
    return s.count(w) != 0;
}

// #RGB / #RGBA / #RRGGBB / #RRGGBBAA كاملة المطابقة.
inline bool isHexColor(const std::string& s) {
    if (s.size() < 4 || s[0] != '#') return false;
    size_t n = s.size() - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    for (size_t i = 1; i < s.size(); ++i) if (!isHex(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

// رقم اختياري الإشارة والكسر، متبوعاً بوحدة اختيارية.
inline bool isLength(const std::string& s) {
    size_t i = 0, n = s.size();
    if (i < n && (s[i] == '-' || s[i] == '+')) ++i;
    size_t digits = 0;
    while (i < n && isDigit(static_cast<unsigned char>(s[i]))) { ++i; ++digits; }
    if (i < n && s[i] == '.') {
        ++i;
        while (i < n && isDigit(static_cast<unsigned char>(s[i]))) { ++i; ++digits; }
    }
    if (digits == 0) return false;
    if (i == n) return true;
    static const char* units[] = {"px", "dp", "sp", "pt", "ms", "s", "%", "vw", "vh", "em", "rem", "deg"};
    std::string u = s.substr(i);
    for (const char* x : units) if (u == x) return true;
    return false;
}

// تصنيف قيمة سمة (نص داخل تنصيص أو قيمة عارية).
inline int classifyValue(const std::string& v) {
    if (isHexColor(v)) return kColor;
    if (isLength(v)) return kLength;
    if (v == "true" || v == "false" || v == "nil") return kConstant;
    if (tokenValues().count(v)) return kToken;
    return kString;
}

class Highlighter {
public:
    explicit Highlighter(const std::vector<std::string>& lines) : lines_(lines) {}

    // هل المستند بصياغة الوسوم (<Screen> / <Column#id> / <warp …>)؟ — فحص رخيص سطراً سطراً.
    static bool looksLikeTags(const std::vector<std::string>& lines) {
        for (const std::string& t : lines) {
            size_t i = 0;
            while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
            if (i + 1 >= t.size() || t[i] != '<') continue;
            unsigned char c = static_cast<unsigned char>(t[i + 1]);
            if (c >= 'A' && c <= 'Z') return true;
            if (t.compare(i, 5, "<warp") == 0) return true;
        }
        return false;
    }

    std::vector<HighlightSpan> run() {
        out_.clear();
        tagMode_ = looksLikeTags(lines_);
        collectWarps();
        state_ = State::Text;
        quote_ = 0;
        nest_ = 0;
        cmtRet_ = State::Text;
        for (size_t li = 0; li < lines_.size(); ++li) scanLine(static_cast<int>(li), lines_[li]);
        return mergeAdjacent(std::move(out_));
    }

    static std::vector<HighlightSpan> mergeAdjacent(std::vector<HighlightSpan> in) {
        std::vector<HighlightSpan> res;
        res.reserve(in.size());
        for (const HighlightSpan& sp : in) {
            if (!res.empty() && res.back().line == sp.line && res.back().kind == sp.kind &&
                res.back().endCol == sp.startCol && sp.kind != static_cast<HighlightKind>(kBr0) &&
                sp.kind != static_cast<HighlightKind>(kBr0 + 1) &&
                sp.kind != static_cast<HighlightKind>(kBr0 + 2) &&
                sp.kind != static_cast<HighlightKind>(kBrace)) {
                res.back().endCol = sp.endCol;
            } else {
                res.push_back(sp);
            }
        }
        return res;
    }

private:
    enum class State { Text, BlockComment, TagName, InTag, Quoted, Expr, RawRin };

    const std::vector<std::string>& lines_;
    std::vector<HighlightSpan> out_;
    std::set<std::string> warps_;
    bool tagMode_ = false;
    State state_ = State::Text;
    State ret_ = State::Text;       // حالة العودة بعد إغلاق تعبير { }
    State cmtRet_ = State::Text;    // حالة العودة بعد تعليق كتلي
    std::string cmtEnd_;
    char quote_ = 0;
    bool attrOk_ = true;            // false داخل كود Rin الخام/التعابير: ident = قيمة ليست سمة
    int exprMode_ = 1;              // 1 = { }  ·  2 = << >>
    std::string rawEnd_;            // وسم إغلاق كتلة Rin الخام (</Fun> أو </Rin>)
    std::string curTag_;            // اسم الوسم المفتوح حالياً
    int nest_ = 0;                  // عمق الأقواس داخل التعبير الحالي (للتلوين بالتناوب)

    void emit(int line, size_t s, size_t e, int kind) {
        if (e <= s) return;
        out_.push_back({line, static_cast<int>(s), static_cast<int>(e), static_cast<HighlightKind>(kind)});
    }

    static int rainbow(int depth) { return kBr0 + (depth < 0 ? 0 : depth % 3); }

    // ---- مسح أولي: أسماء خلايا warp (<warp a={0} b={1} /> و warp name = …;) ----
    void collectWarps() {
        warps_.clear();
        for (const std::string& t : lines_) {
            size_t i = 0;
            const size_t n = t.size();
            while (i < n && (t[i] == ' ' || t[i] == '\t')) ++i;
            if (t.compare(i, 5, "<warp") == 0 && (i + 5 >= n || !isIdentChar(static_cast<unsigned char>(t[i + 5])))) {
                size_t j = i + 5;
                int brace = 0;
                char q = 0;
                while (j < n) {
                    unsigned char c = static_cast<unsigned char>(t[j]);
                    if (q) { if (c == static_cast<unsigned char>(q)) q = 0; ++j; continue; }
                    if (c == '"' || c == '\'') { q = static_cast<char>(c); ++j; continue; }
                    if (c == '{') { ++brace; ++j; continue; }
                    if (c == '}') { if (brace > 0) --brace; ++j; continue; }
                    if (brace == 0 && c == '>') break;
                    if (brace == 0 && isIdentStart(c)) {
                        size_t k = j;
                        while (k < n && isIdentChar(static_cast<unsigned char>(t[k]))) ++k;
                        size_t m = k;
                        while (m < n && (t[m] == ' ' || t[m] == '\t')) ++m;
                        if (m < n && t[m] == '=') warps_.insert(t.substr(j, k - j));
                        j = k;
                        continue;
                    }
                    ++j;
                }
            } else if (t.compare(i, 5, "warp ") == 0 || t.compare(i, 5, "warp\t") == 0) {
                size_t j = i + 5;
                while (j < n && (t[j] == ' ' || t[j] == '\t')) ++j;
                size_t k = j;
                while (k < n && isIdentChar(static_cast<unsigned char>(t[k]))) ++k;
                if (k > j && isIdentStart(static_cast<unsigned char>(t[j]))) warps_.insert(t.substr(j, k - j));
            }
        }
    }

    // ---- مسح شيفرة (تعبير/عبارة) على النطاق [i, limit) ----
    // braceMode: يُغلَق التعبير عند } التي توازن { الفاتحة (state_ يعود إلى ret_). وإلا وضع عبارة (سطر Rin).
    // يُرجع الموضع الذي توقّف عنده.
    size_t scanCode(int li, const std::string& t, size_t i, size_t limit, int mode) {
        // mode: 0 = عبارة Rin (سطر كامل) · 1 = تعبير { } · 2 = جسر << >>
        std::string prevWord;
        bool firstToken = true;
        bool stmtStart = true; // بداية عبارة (أول السطر أو بعد ; { }) — هنا تُقرأ ident = قيمة كسمة (الصياغة القديمة)
        while (i < limit) {
            const size_t startI = i;
            unsigned char c = static_cast<unsigned char>(t[i]);
            if (isSpace(c)) { ++i; continue; }
            const bool atStmtStart = stmtStart;
            stmtStart = false;

            // تعليقات
            if (c == '/' && i + 1 < limit && t[i + 1] == '/' && (i == 0 || t[i - 1] != ':')) {
                emit(li, i, limit, kComment);
                return limit;
            }
            if (c == '/' && i + 1 < limit && t[i + 1] == '*') {
                size_t end = t.find("*/", i + 2);
                if (end == std::string::npos || end + 2 > limit) {
                    emit(li, i, limit, kComment);
                    if (mode != 0 || limit == t.size()) {
                        if (mode != 0) exprMode_ = mode;
                        cmtRet_ = mode != 0 ? State::Expr : state_;
                        cmtEnd_ = "*/";
                        state_ = State::BlockComment;
                    }
                    return limit;
                }
                emit(li, i, end + 2, kComment);
                i = end + 2;
                continue;
            }

            // نصوص
            if (c == '"' || c == '\'' || c == '`') {
                size_t j = i + 1;
                while (j < limit && static_cast<unsigned char>(t[j]) != c) {
                    if (t[j] == '\\' && j + 1 < limit) ++j;
                    ++j;
                }
                size_t end = (j < limit) ? j + 1 : limit;
                if (j < limit && end - i >= 3 && c != '`') {
                    // نص كامل يحمل قيمة تصميم/لون/طول ("heading" "#3498db" "12px") يُلوَّن بدوره
                    int vk = classifyValue(t.substr(i + 1, end - i - 2));
                    if (vk != kString && vk != kConstant) {
                        emit(li, i, i + 1, kString);
                        emit(li, i + 1, end - 1, vk);
                        emit(li, end - 1, end, kString);
                        i = end;
                        prevWord.clear();
                        continue;
                    }
                }
                emit(li, i, end, kString);
                i = end;
                prevWord.clear();
                continue;
            }

            // أرقام
            if (isDigit(c)) {
                size_t j = i;
                if (c == '0' && j + 1 < limit && (t[j + 1] == 'x' || t[j + 1] == 'X')) {
                    j += 2;
                    while (j < limit && isHex(static_cast<unsigned char>(t[j]))) ++j;
                } else {
                    while (j < limit && isDigit(static_cast<unsigned char>(t[j]))) ++j;
                    if (j + 1 < limit && t[j] == '.' && isDigit(static_cast<unsigned char>(t[j + 1]))) {
                        ++j;
                        while (j < limit && isDigit(static_cast<unsigned char>(t[j]))) ++j;
                    }
                    // وحدة لاصقة (px, dp, ms, %…)
                    size_t u = j;
                    while (u < limit && ((t[u] >= 'a' && t[u] <= 'z') || t[u] == '%')) ++u;
                    if (u > j && isLength(t.substr(i, u - i))) { emit(li, i, u, kLength); i = u; prevWord.clear(); continue; }
                }
                emit(li, i, j, kNumber);
                i = j;
                prevWord.clear();
                continue;
            }

            // # : لون hex أو كلمة عائلة # في Rin
            if (c == '#') {
                size_t j = i + 1;
                while (j < limit && isHex(static_cast<unsigned char>(t[j]))) ++j;
                if ((j < limit ? !isIdentChar(static_cast<unsigned char>(t[j])) : true) && isHexColor(t.substr(i, j - i))) {
                    emit(li, i, j, kColor);
                    i = j;
                    continue;
                }
                size_t k = i + 1;
                while (k < limit && isIdentChar(static_cast<unsigned char>(t[k]))) ++k;
                if (k > i + 1) { emit(li, i, k, kKeyword); i = k; continue; }
                ++i;
                continue;
            }

            // @name
            if (c == '@' && i + 1 < limit && isIdentStart(static_cast<unsigned char>(t[i + 1]))) {
                size_t k = i + 1;
                while (k < limit && (isIdentChar(static_cast<unsigned char>(t[k])) || t[k] == '.')) ++k;
                emit(li, i, k, kAt);
                i = k;
                continue;
            }

            // معرِّفات وكلمات
            if (isIdentStart(c)) {
                size_t k = i;
                while (k < limit && isIdentChar(static_cast<unsigned char>(t[k]))) ++k;
                std::string w = t.substr(i, k - i);
                size_t m = k;
                while (m < limit && (t[m] == ' ' || t[m] == '\t')) ++m;
                const bool call = (m < limit && t[m] == '(');
                const bool afterDot = (i > 0 && t[i - 1] == '.' && (i < 2 || t[i - 2] != '.'));
                int kind = -1;
                bool singleEq = false;
                if (m < limit && t[m] == '=') {
                    unsigned char nx = (m + 1 < limit) ? static_cast<unsigned char>(t[m + 1]) : 0;
                    singleEq = (nx != '=' && nx != '>');
                }
                if (mode == 0 && attrOk_ && atStmtStart && singleEq && !afterDot && !isDeclKeyword(w) &&
                    !isControlWord(w) && !warps_.count(w) && w != "true" && w != "false" && w != "nil") {
                    kind = attrKind(w);
                } else
                if (mode == 2 && firstToken && (w == "run" || w == "each")) kind = kKeyword;
                else if (prevWord == "fun" || prevWord == "fn") kind = kFuncDef;
                else if (w == "true" || w == "false" || w == "nil") kind = kConstant;
                else if (w == "and" || w == "or" || w == "not") kind = kCompare;
                else if (isControlWord(w)) kind = kControl;
                else if (isKeywordWord(w)) kind = kKeyword;
                else if (warps_.count(w) && !afterDot) kind = kState;
                else if (call) kind = kCall;
                else if (afterDot) kind = kProperty;
                if (kind >= 0) emit(li, i, k, kind);
                prevWord = w;
                firstToken = false;
                i = k;
                continue;
            }

            prevWord.clear();

            // أقواس
            if (c == '(' || c == '[' || c == '{') {
                emit(li, i, i + 1, rainbow(nest_));
                ++nest_;
                ++i;
                if (c == '{') stmtStart = true;
                continue;
            }
            if (c == ')' || c == ']' || c == '}') {
                if (c == '}' && mode == 1 && nest_ == 0) {
                    emit(li, i, i + 1, kBrace);
                    state_ = ret_;
                    return i + 1;
                }
                if (nest_ > 0) --nest_;
                emit(li, i, i + 1, rainbow(nest_));
                ++i;
                if (c == '}') stmtStart = true;
                continue;
            }

            // عوامل
            const unsigned char d = (i + 1 < limit) ? static_cast<unsigned char>(t[i + 1]) : 0;
            if (c == '=') {
                if (d == '=') { emit(li, i, i + 2, kCompare); i += 2; }
                else if (d == '>') { emit(li, i, i + 2, kFlow); i += 2; }
                else { emit(li, i, i + 1, kOperator); ++i; }
                continue;
            }
            if (c == '!' ) {
                if (d == '=') { emit(li, i, i + 2, kCompare); i += 2; }
                else { emit(li, i, i + 1, kCompare); ++i; }
                continue;
            }
            if (c == '>' && d == '>' && mode == 2 && nest_ == 0) {
                emit(li, i, i + 2, kBrace);
                state_ = ret_;
                return i + 2;
            }
            if (c == '<' || c == '>') {
                if (d == '=') { emit(li, i, i + 2, kCompare); i += 2; }
                else { emit(li, i, i + 1, kCompare); ++i; }
                continue;
            }
            if ((c == '&' && d == '&') || (c == '|' && d == '|')) { emit(li, i, i + 2, kCompare); i += 2; continue; }
            if (c == '|' && d == '>') { emit(li, i, i + 2, kFlow); i += 2; continue; }
            if (c == '?') {
                size_t w = (d == '?' || d == '.') ? 2 : 1;
                emit(li, i, i + w, kFlow);
                i += w;
                continue;
            }
            if (c == ';') { emit(li, i, i + 1, kPunct); ++i; stmtStart = true; continue; }
            if (c == ',' || c == ':' || c == '.') { emit(li, i, i + 1, kPunct); ++i; continue; }
            if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^' || c == '~' || c == '&' || c == '|') {
                emit(li, i, i + 1, kOperator);
                ++i;
                continue;
            }
            ++i;
            if (i <= startI) i = startI + 1; // حاجز تقدّم
        }
        return i;
    }

    // يصنّف قيمة داخل تنصيص مع استخراج { تعبيرات } في نفس السطر.
    void emitQuotedInner(int li, const std::string& t, size_t s, size_t e) {
        size_t segStart = s;
        size_t i = s;
        while (i < e) {
            if (t[i] == '<' && i + 1 < e && t[i + 1] == '<') {
                size_t close = t.find(">>", i + 2);
                if (close != std::string::npos && close + 2 <= e) {
                    flushValue(li, t, segStart, i);
                    emit(li, i, i + 2, kBrace);
                    int savedNest = nest_;
                    nest_ = 0;
                    attrOk_ = false;
                    scanCode(li, t, i + 2, close, 0);
                    attrOk_ = true;
                    nest_ = savedNest;
                    emit(li, close, close + 2, kBrace);
                    i = close + 2;
                    segStart = i;
                    continue;
                }
            }
            if (t[i] == '{') {
                size_t close = std::string::npos;
                int depth = 0;
                for (size_t j = i; j < e; ++j) {
                    if (t[j] == '{') ++depth;
                    else if (t[j] == '}') { if (--depth == 0) { close = j; break; } }
                }
                if (close != std::string::npos) {
                    flushValue(li, t, segStart, i);
                    emit(li, i, i + 1, kBrace);
                    int savedNest = nest_;
                    nest_ = 0;
                    attrOk_ = false;
                    scanCode(li, t, i + 1, close, 0);
                    attrOk_ = true;
                    nest_ = savedNest;
                    emit(li, close, close + 1, kBrace);
                    i = close + 1;
                    segStart = i;
                    continue;
                }
            }
            ++i;
        }
        flushValue(li, t, segStart, e);
    }

    void flushValue(int li, const std::string& t, size_t s, size_t e) {
        if (e <= s) return;
        emit(li, s, e, classifyValue(t.substr(s, e - s)));
    }

    // اسم سمة: حدث (onXxx) أو سمة عادية.
    static int attrKind(const std::string& name) {
        if (name.size() > 2 && name[0] == 'o' && name[1] == 'n' && name[2] >= 'A' && name[2] <= 'Z') return kEvent;
        return kAttr;
    }

    // ---- أسطر الصياغة القديمة (@view… / .end/… / سمة = قيمة;) ----
    // يُرجع الموضع بعد الجزء البنيوي (أو 0 إن لم يكن السطر بنيوياً).
    size_t scanStructuralPrefix(int li, const std::string& t) {
        const size_t n = t.size();
        size_t i = 0;
        while (i < n && (t[i] == ' ' || t[i] == '\t')) ++i;
        if (i >= n) return n;

        if (t.compare(i, 5, ".end/") == 0) {
            size_t k = i + 5;
            while (k < n && (isIdentChar(static_cast<unsigned char>(t[k])) || t[k] == '.')) ++k;
            emit(li, i, k, kStruct);
            return k;
        }
        if (t[i] == '@') {
            size_t k = i + 1;
            while (k < n && isIdentChar(static_cast<unsigned char>(t[k]))) ++k;
            std::string w = t.substr(i + 1, k - i - 1);
            if (w == "view" || w == "loop" || w == "container" || w == "import" || w == "theme" || w == "screen") {
                emit(li, i, k, kStruct);
                size_t j = k;
                if (j < n && t[j] == '.') {
                    size_t e = j + 1;
                    while (e < n && isIdentChar(static_cast<unsigned char>(t[e]))) ++e;
                    emit(li, j, j + 1, kPunct);
                    std::string kind = t.substr(j + 1, e - j - 1);
                    emit(li, j + 1, e, knownTags().count(kind) ? kTag : kTagCustom);
                    j = e;
                }
                size_t m = j;
                while (m < n && (t[m] == ' ' || t[m] == '\t')) ++m;
                if (m < n && t[m] == '=') {
                    emit(li, m, m + 1, kOperator);
                    size_t s = m + 1;
                    while (s < n && (t[s] == ' ' || t[s] == '\t')) ++s;
                    size_t e = s;
                    while (e < n && (isIdentChar(static_cast<unsigned char>(t[e])) || t[e] == '.' || t[e] == '-')) ++e;
                    emit(li, s, e, kId);
                    return e;
                }
                return j;
            }
            return 0;
        }
        // سمة = قيمة (صياغة قديمة): ident متبوعة بـ = مفردة
        if (isIdentStart(static_cast<unsigned char>(t[i]))) {
            size_t k = i;
            while (k < n && isIdentChar(static_cast<unsigned char>(t[k]))) ++k;
            std::string w = t.substr(i, k - i);
            size_t m = k;
            while (m < n && (t[m] == ' ' || t[m] == '\t')) ++m;
            if (m < n && t[m] == '=' && (m + 1 >= n || (t[m + 1] != '=' && t[m + 1] != '>')) &&
                !isDeclKeyword(w) && !isControlWord(w) && !warps_.count(w)) {
                emit(li, i, k, attrKind(w));
                emit(li, m, m + 1, kOperator);
                // قيمة عارية/منصَّصة على نفس السطر
                size_t s = m + 1;
                while (s < n && (t[s] == ' ' || t[s] == '\t')) ++s;
                if (s < n && (t[s] == '"' || t[s] == '\'')) {
                    size_t e = s + 1;
                    while (e < n && t[e] != t[s]) { if (t[e] == '\\' && e + 1 < n) ++e; ++e; }
                    size_t close = (e < n) ? e : n;
                    emit(li, s, s + 1, kString);
                    emitQuotedInner(li, t, s + 1, close);
                    if (close < n) emit(li, close, close + 1, kString);
                    return close < n ? close + 1 : n;
                }
                return s;
            }
        }
        return 0;
    }

    void scanLine(int li, const std::string& t) {
        const size_t n = t.size();
        size_t i = 0;

        // أسطر بنيوية/سمات (صياغة قديمة) لا تكون إلا في حالة Text خارج أي وسم.
        if (state_ == State::Text) {
            // تعليق سطر كامل بدءاً من أول حرف غير فراغ
            size_t f = 0;
            while (f < n && (t[f] == ' ' || t[f] == '\t')) ++f;
            if (f + 1 < n && t[f] == '/' && t[f + 1] == '/') { emit(li, f, n, kComment); return; }
            size_t consumed = scanStructuralPrefix(li, t);
            if (consumed > 0) {
                i = consumed;
                if (!tagMode_) { scanCode(li, t, i, n, 0); return; }
            } else if (!tagMode_) {
                scanCode(li, t, 0, n, 0);
                return;
            }
        }

        while (i < n) {
            const size_t loopStart = i;
            const State prev = state_;
            switch (state_) {
            case State::BlockComment: {
                size_t end = t.find(cmtEnd_, i);
                if (end == std::string::npos) { emit(li, i, n, kComment); i = n; }
                else { emit(li, i, end + cmtEnd_.size(), kComment); i = end + cmtEnd_.size(); state_ = cmtRet_; }
                break;
            }
            case State::Expr: {
                i = scanCode(li, t, i, n, exprMode_);
                break;
            }
            case State::Quoted: {
                size_t j = i;
                while (j < n && t[j] != quote_) ++j;
                if (j < n) {
                    emitQuotedInner(li, t, i, j);
                    emit(li, j, j + 1, kString);
                    i = j + 1;
                    state_ = State::InTag;
                } else {
                    emitQuotedInner(li, t, i, n);
                    i = n;
                }
                break;
            }
            case State::TagName: {
                size_t j = i;
                while (j < n && isTagNameChar(static_cast<unsigned char>(t[j]))) ++j;
                if (j > i) {
                    std::string name = t.substr(i, j - i);
                    int kind;
                    curTag_ = name;
                    if (name == "warp") kind = kState;
                    else if (isDirectiveTag(name)) kind = kStruct;
                    else if (knownTags().count(name)) kind = kTag;
                    else kind = kTagCustom;
                    emit(li, i, j, kind);
                    i = j;
                    if (i < n && t[i] == '#') { // #id
                        size_t k = i + 1;
                        while (k < n && (isIdentChar(static_cast<unsigned char>(t[k])) || t[k] == '-')) ++k;
                        emit(li, i, k, kId);
                        i = k;
                    }
                }
                state_ = State::InTag;
                break;
            }
            case State::InTag: {
                unsigned char c = static_cast<unsigned char>(t[i]);
                if (isSpace(c)) { ++i; break; }
                if (c == '>') {
                    emit(li, i, i + 1, kBracket); ++i;
                    if (curTag_ == "Fun" || curTag_ == "Rin") { rawEnd_ = "</" + curTag_ + ">"; state_ = State::RawRin; nest_ = 0; }
                    else state_ = State::Text;
                    break;
                }
                if (c == '/' && i + 1 < n && t[i + 1] == '>') {
                    emit(li, i, i + 2, kBracket); i += 2; state_ = State::Text; break;
                }
                if (c == '"' || c == '\'') {
                    quote_ = static_cast<char>(c);
                    emit(li, i, i + 1, kString);
                    ++i;
                    state_ = State::Quoted;
                    break;
                }
                if (c == '{') { // قيمة تعبير بلا اسم سمة (spread) — عامِلها كتعبير
                    emit(li, i, i + 1, kBrace);
                    ++i; nest_ = 0; ret_ = State::InTag; exprMode_ = 1; state_ = State::Expr;
                    break;
                }
                if (isIdentStart(c)) {
                    size_t j = i;
                    while (j < n) {
                        unsigned char d = static_cast<unsigned char>(t[j]);
                        if (isIdentChar(d) || d == '-' || d == ':' || d == '.') ++j; else break;
                    }
                    std::string name = t.substr(i, j - i);
                    emit(li, i, j, attrKind(name));
                    i = j;
                    size_t m = i;
                    while (m < n && (t[m] == ' ' || t[m] == '\t')) ++m;
                    if (m < n && t[m] == '=') {
                        emit(li, m, m + 1, kOperator);
                        size_t s = m + 1;
                        while (s < n && (t[s] == ' ' || t[s] == '\t')) ++s;
                        i = s;
                        if (i < n && t[i] == '{') {
                            emit(li, i, i + 1, kBrace);
                            ++i; nest_ = 0; ret_ = State::InTag; exprMode_ = 1; state_ = State::Expr;
                        } else if (i + 1 < n && t[i] == '<' && t[i + 1] == '<') {
                            emit(li, i, i + 2, kBrace);
                            i += 2; nest_ = 0; ret_ = State::InTag; exprMode_ = 2; state_ = State::Expr;
                        } else if (i < n && (t[i] == '"' || t[i] == '\'')) {
                            // يُعالَج في دورة InTag التالية
                        } else if (i < n && t[i] != '>' && !(t[i] == '/' && i + 1 < n && t[i + 1] == '>')) {
                            size_t e = i;
                            while (e < n && !isSpace(static_cast<unsigned char>(t[e])) && t[e] != '>' &&
                                   !(t[e] == '/' && e + 1 < n && t[e + 1] == '>')) ++e;
                            if (e > i) emit(li, i, e, classifyValue(t.substr(i, e - i)));
                            i = e;
                        }
                    }
                    break;
                }
                ++i; // رمز غريب داخل الوسم: تخطَّه
                break;
            }
            case State::RawRin: {
                size_t end = t.find(rawEnd_, i);
                attrOk_ = false;
                if (end == std::string::npos) {
                    scanCode(li, t, i, n, 0);
                    i = n;
                } else {
                    scanCode(li, t, i, end, 0);
                    emit(li, end, end + 2, kBracket);
                    emit(li, end + 2, end + rawEnd_.size() - 1, kStruct);
                    emit(li, end + rawEnd_.size() - 1, end + rawEnd_.size(), kBracket);
                    i = end + rawEnd_.size();
                    state_ = State::Text;
                }
                attrOk_ = true;
                break;
            }
            case State::Text: {
                unsigned char c = static_cast<unsigned char>(t[i]);
                if (c == '{') {
                    emit(li, i, i + 1, kBrace);
                    ++i; nest_ = 0; ret_ = State::Text; exprMode_ = 1; state_ = State::Expr;
                    break;
                }
                if (c == '&') { // كيانات: &lt; &gt; &amp; &lbrace; &rbrace; &quot;
                    size_t k = i + 1;
                    while (k < n && k - i <= 8 && ((t[k] >= 'a' && t[k] <= 'z') || (t[k] >= 'A' && t[k] <= 'Z') || isDigit(static_cast<unsigned char>(t[k])))) ++k;
                    if (k < n && t[k] == ';' && k > i + 1) { emit(li, i, k + 1, kToken); i = k + 1; break; }
                    ++i;
                    break;
                }
                if (c == '<') {
                    if (t.compare(i, 4, "<!--") == 0) {
                        size_t end = t.find("-->", i + 4);
                        if (end == std::string::npos) {
                            emit(li, i, n, kComment); i = n;
                            cmtRet_ = State::Text; cmtEnd_ = "-->"; state_ = State::BlockComment;
                        } else { emit(li, i, end + 3, kComment); i = end + 3; }
                        break;
                    }
                    if (t.compare(i, 3, "<</") == 0) { // <</if>> <</each>>
                        size_t end = t.find(">>", i + 3);
                        if (end != std::string::npos) { emit(li, i, end + 2, kStruct); i = end + 2; break; }
                    }
                    if (i + 1 < n && t[i + 1] == '<') { // جسر Rin <<expr>>
                        emit(li, i, i + 2, kBrace);
                        i += 2; nest_ = 0; ret_ = State::Text; exprMode_ = 2; state_ = State::Expr;
                        break;
                    }
                    if (i + 1 < n && t[i + 1] == '>') { emit(li, i, i + 2, kBracket); i += 2; break; } // <> Fragment
                    if (i + 2 < n && t[i + 1] == '/' && t[i + 2] == '>') { emit(li, i, i + 3, kBracket); i += 3; break; } // </>
                    if (i + 2 < n && t[i + 1] == '/' && isIdentStart(static_cast<unsigned char>(t[i + 2]))) {
                        emit(li, i, i + 2, kBracket); i += 2; state_ = State::TagName; break;
                    }
                    if (i + 1 < n && isIdentStart(static_cast<unsigned char>(t[i + 1]))) {
                        emit(li, i, i + 1, kBracket); i += 1; state_ = State::TagName; break;
                    }
                    ++i; // '<' مجردة في نص
                    break;
                }
                if (c == '/' && i + 1 < n && t[i + 1] == '*') {
                    size_t end = t.find("*/", i + 2);
                    if (end == std::string::npos) {
                        emit(li, i, n, kComment); i = n;
                        cmtRet_ = State::Text; cmtEnd_ = "*/"; state_ = State::BlockComment;
                    } else { emit(li, i, end + 2, kComment); i = end + 2; }
                    break;
                }
                ++i;
                break;
            }
            }
            if (i <= loopStart && state_ == prev) i = loopStart + 1; // حاجز تقدّم قاطع
        }
        // نهاية السطر: الحالات أحادية السطر تعود إلى أصلها.
        if (state_ == State::TagName) state_ = State::InTag;
    }
};

} // namespace indsinhl

// نقطة الدخول المستخدمة من EditorEngine::computeHighlights().
inline bool looksLikeIndsinTags(const std::vector<std::string>& lines) {
    return indsinhl::Highlighter::looksLikeTags(lines);
}

inline std::vector<HighlightSpan> computeIndsinHighlights(const std::vector<std::string>& lines) {
    try {
        return indsinhl::Highlighter(lines).run();
    } catch (...) {
        return {}; // لا تُعطِّل الواجهة أبداً بسبب التلوين
    }
}

} // namespace rinedit

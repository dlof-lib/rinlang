// rin_editor_engine.cpp
#include "rin_editor_engine.h"
#include "../rin_lexer.h"
#include "../rin_common.h"
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <cctype>

namespace rinedit {

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            out.push_back(text.substr(pos));
            break;
        }
        out.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    if (out.empty()) out.push_back("");
    return out;
}

EditorEngine::EditorEngine() { lines_.push_back(""); }

void EditorEngine::setText(const std::string& text) {
    lines_ = splitLines(text);
    if (lines_.empty()) lines_.push_back("");
    cursor_ = {0, 0};
    selAnchor_ = {0, 0};
    hasSel_ = false;
    undoStack_.clear();
    redoStack_.clear();
}

std::string EditorEngine::getText() const {
    std::string out;
    for (size_t i = 0; i < lines_.size(); ++i) {
        out += lines_[i];
        if (i + 1 < lines_.size()) out += '\n';
    }
    return out;
}

std::string EditorEngine::getLine(int line) const {
    if (line < 0 || line >= (int)lines_.size()) return "";
    return lines_[line];
}

int EditorEngine::clampLine(int line) const {
    return std::max(0, std::min(line, (int)lines_.size() - 1));
}

int EditorEngine::clampCol(int line, int col) const {
    int len = (int)lines_[clampLine(line)].size();
    return std::max(0, std::min(col, len));
}

void EditorEngine::clampCursor() {
    cursor_.line = clampLine(cursor_.line);
    cursor_.col = clampCol(cursor_.line, cursor_.col);
}

void EditorEngine::getSelection(int* sl, int* sc, int* el, int* ec) const {
    Position a = selAnchor_, b = cursor_;
    if (a.line > b.line || (a.line == b.line && a.col > b.col)) std::swap(a, b);
    *sl = a.line; *sc = a.col; *el = b.line; *ec = b.col;
}

void EditorEngine::setCursor(int line, int col, bool extendSelection) {
    line = clampLine(line);
    col = clampCol(line, col);
    if (!extendSelection) {
        selAnchor_ = {line, col};
        hasSel_ = false;
    } else {
        if (!hasSel_) { selAnchor_ = cursor_; }
        hasSel_ = true;
    }
    cursor_ = {line, col};
    if (hasSel_ && selAnchor_.line == cursor_.line && selAnchor_.col == cursor_.col) hasSel_ = false;
}

void EditorEngine::setSelection(int aLine, int aCol, int bLine, int bCol) {
    aLine = clampLine(aLine); aCol = clampCol(aLine, aCol);
    bLine = clampLine(bLine); bCol = clampCol(bLine, bCol);
    selAnchor_ = {aLine, aCol};
    cursor_ = {bLine, bCol};
    hasSel_ = !(aLine == bLine && aCol == bCol);
}

void EditorEngine::collapseSelectionToCursor() {
    selAnchor_ = cursor_;
    hasSel_ = false;
}

std::string EditorEngine::textOfRange(int sl, int sc, int el, int ec) const {
    if (sl == el) return lines_[sl].substr(sc, ec - sc);
    std::string out = lines_[sl].substr(sc);
    for (int i = sl + 1; i < el; ++i) { out += '\n'; out += lines_[i]; }
    out += '\n';
    out += lines_[el].substr(0, ec);
    return out;
}

void EditorEngine::applyRawReplace(int sl, int sc, int el, int ec, const std::string& text, Position* outEnd) {
    std::string prefix = lines_[sl].substr(0, sc);
    std::string suffix = lines_[el].substr(ec);
    std::vector<std::string> parts = splitLines(text);
    std::vector<std::string> newLines;
    if (parts.size() == 1) {
        newLines.push_back(prefix + parts[0] + suffix);
    } else {
        newLines.push_back(prefix + parts.front());
        for (size_t i = 1; i + 1 < parts.size(); ++i) newLines.push_back(parts[i]);
        newLines.push_back(parts.back() + suffix);
    }
    lines_.erase(lines_.begin() + sl, lines_.begin() + el + 1);
    lines_.insert(lines_.begin() + sl, newLines.begin(), newLines.end());
    if (lines_.empty()) lines_.push_back("");
    if (outEnd) {
        if (parts.size() == 1) {
            outEnd->line = sl;
            outEnd->col = sc + (int)parts[0].size();
        } else {
            outEnd->line = sl + (int)parts.size() - 1;
            outEnd->col = (int)parts.back().size();
        }
    }
}

void EditorEngine::pushUndo(EditRecord rec) {
    redoStack_.clear();
    if (rec.coalescable && !undoStack_.empty()) {
        EditRecord& top = undoStack_.back();
        bool contiguous = top.coalescable &&
            top.newEndLine == rec.startLine && top.newEndCol == rec.startCol &&
            top.removedText.empty() && rec.removedText.empty() && // فقط إدراج متتابع خالص
            (rec.timestampMs - top.timestampMs) <= kCoalesceWindowMs;
        // كذلك دعم تجميع حذف Backspace متتابع (removedText يكبر للخلف)
        bool contiguousBackspace = top.coalescable &&
            top.insertedText.empty() && rec.insertedText.empty() &&
            rec.origEndLine == top.startLine && rec.origEndCol == top.startCol &&
            (rec.timestampMs - top.timestampMs) <= kCoalesceWindowMs;
        if (contiguous) {
            top.insertedText += rec.insertedText;
            top.newEndLine = rec.newEndLine;
            top.newEndCol = rec.newEndCol;
            top.cursorAfter = rec.cursorAfter;
            top.timestampMs = rec.timestampMs;
            return;
        }
        if (contiguousBackspace) {
            // rec يحذف حرفًا يقع مباشرة قبل بداية top الحالية: وسّع الحذف يسارًا.
            // origEnd (الحافة اليمنى الأصلية قبل أي حذف من هذه السلسلة) تبقى كما هي في top.
            top.removedText = rec.removedText + top.removedText;
            top.startLine = rec.startLine;
            top.startCol = rec.startCol;
            top.newEndLine = top.startLine;
            top.newEndCol = top.startCol;
            top.cursorAfter = rec.cursorAfter;
            top.timestampMs = rec.timestampMs;
            return;
        }
    }
    undoStack_.push_back(std::move(rec));
    if (undoStack_.size() > kMaxHistory) undoStack_.pop_front();
}

void EditorEngine::applyReplaceTracked(int sl, int sc, int el, int ec, const std::string& text, bool coalescable) {
    sl = clampLine(sl); el = clampLine(el);
    sc = clampCol(sl, sc); ec = clampCol(el, ec);
    if (sl > el || (sl == el && sc > ec)) { std::swap(sl, el); std::swap(sc, ec); }
    std::string removed = textOfRange(sl, sc, el, ec);
    Position before = cursor_;
    Position after;
    applyRawReplace(sl, sc, el, ec, text, &after);
    EditRecord rec;
    rec.startLine = sl; rec.startCol = sc;
    rec.origEndLine = el; rec.origEndCol = ec;
    rec.newEndLine = after.line; rec.newEndCol = after.col;
    rec.removedText = removed;
    rec.insertedText = text;
    rec.cursorBefore = before;
    rec.cursorAfter = after;
    rec.timestampMs = nowMs();
    rec.coalescable = coalescable;
    pushUndo(rec);
    cursor_ = after;
    selAnchor_ = after;
    hasSel_ = false;
}

void EditorEngine::replaceRange(int sl, int sc, int el, int ec, const std::string& text) {
    applyReplaceTracked(sl, sc, el, ec, text, false);
}

void EditorEngine::deleteSelectionIfAny() {
    if (!hasSel_) return;
    int sl, sc, el, ec;
    getSelection(&sl, &sc, &el, &ec);
    applyReplaceTracked(sl, sc, el, ec, "", false);
}

std::string EditorEngine::leadingWhitespace(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(0, i);
}

std::string EditorEngine::rtrim(const std::string& s) {
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
    return s.substr(0, end);
}

static const std::unordered_map<char, char> kCloserOf = {{'(', ')'}, {'[', ']'}, {'{', '}'}};
static const std::unordered_map<char, char> kOpenerOf = {{')', '('}, {']', '['}, {'}', '{'}};

void EditorEngine::insertText(const std::string& text, bool smart) {
    if (hasSel_) {
        int sl, sc, el, ec;
        getSelection(&sl, &sc, &el, &ec);
        applyReplaceTracked(sl, sc, el, ec, text, false);
        return;
    }

    if (smart && text == "\n") {
        const std::string& curLine = lines_[cursor_.line];
        std::string before = curLine.substr(0, cursor_.col);
        std::string indent = leadingWhitespace(before);
        std::string trimmedBefore = rtrim(before);
        std::string toInsert = "\n" + indent;
        if (!trimmedBefore.empty() && trimmedBefore.back() == '{') toInsert += "    ";
        applyReplaceTracked(cursor_.line, cursor_.col, cursor_.line, cursor_.col, toInsert, false);
        return;
    }

    if (smart && text.size() == 1) {
        char c = text[0];
        const std::string& curLine = lines_[cursor_.line];
        char nextChar = (cursor_.col < (int)curLine.size()) ? curLine[cursor_.col] : '\0';
        bool isQuote = (c == '"' || c == '\'');

        // تخطٍّ فوق القوس/التنصيص المغلق الذي أُدرج تلقائيًا بالفعل
        bool typedIsCloser = kOpenerOf.count(c) > 0; // c هو قوس إغلاق مثل ) ] }
        if ((typedIsCloser || isQuote) && nextChar == c) {
            cursor_.col += 1;
            selAnchor_ = cursor_;
            hasSel_ = false;
            return;
        }

        if (kCloserOf.count(c) > 0) { // c هو قوس فتح مثل ( [ {
            std::string ins(1, c);
            ins += kCloserOf.at(c);
            applyReplaceTracked(cursor_.line, cursor_.col, cursor_.line, cursor_.col, ins, false);
            cursor_.col -= 1;
            selAnchor_ = cursor_;
            return;
        }

        if (isQuote) {
            bool nextIsWordChar = nextChar != '\0' &&
                (std::isalnum((unsigned char)nextChar) || nextChar == '_');
            if (!nextIsWordChar) {
                std::string ins(1, c);
                ins += c;
                applyReplaceTracked(cursor_.line, cursor_.col, cursor_.line, cursor_.col, ins, false);
                cursor_.col -= 1;
                selAnchor_ = cursor_;
                return;
            }
        }

        applyReplaceTracked(cursor_.line, cursor_.col, cursor_.line, cursor_.col, text, true);
        if (c == '}') applySmartDedentForClosingBrace();
        return;
    }

    applyReplaceTracked(cursor_.line, cursor_.col, cursor_.line, cursor_.col, text, false);
}

void EditorEngine::applySmartDedentForClosingBrace() {
    // المؤشر الآن مباشرة بعد '}' الذي أُدرج للتو.
    int line = cursor_.line;
    int closeCol = cursor_.col - 1;
    if (closeCol < 0) return;
    std::string beforeBrace = lines_[line].substr(0, closeCol);
    if (!rtrim(beforeBrace).empty()) return; // ليس القوس الوحيد على السطر

    // ابحث عن القوس '{' المطابق بالمسح للخلف عبر كامل النص
    int depth = 0;
    int ml = line, mc = closeCol;
    bool found = false;
    while (true) {
        if (mc == 0) {
            if (ml == 0) break;
            --ml;
            mc = (int)lines_[ml].size();
            continue;
        }
        --mc;
        char ch = lines_[ml][mc];
        if (ch == '}') ++depth;
        else if (ch == '{') {
            if (depth == 0) { found = true; break; }
            --depth;
        }
    }
    if (!found) return;
    std::string targetIndent = leadingWhitespace(lines_[ml]);
    std::string currentIndent = beforeBrace; // كله فراغات كما تحقّقنا أعلاه
    if (currentIndent == targetIndent) return;
    applyReplaceTracked(line, 0, line, closeCol, targetIndent, false);
}

void EditorEngine::deleteBackward() {
    if (hasSel_) { deleteSelectionIfAny(); return; }
    int line = cursor_.line, col = cursor_.col;
    if (col == 0 && line == 0) return;

    if (col > 0) {
        char deleted = lines_[line][col - 1];
        char next = (col < (int)lines_[line].size()) ? lines_[line][col] : '\0';
        bool isBracketPair = kCloserOf.count(deleted) && kCloserOf.at(deleted) == next;
        bool isQuotePair = (deleted == '"' || deleted == '\'') && next == deleted;
        if (isBracketPair || isQuotePair) {
            applyReplaceTracked(line, col - 1, line, col + 1, "", false);
            return;
        }
        applyReplaceTracked(line, col - 1, line, col, "", true);
        return;
    }
    // بداية سطر: دمج مع السطر السابق
    int prevLen = (int)lines_[line - 1].size();
    applyReplaceTracked(line - 1, prevLen, line, 0, "", false);
}

void EditorEngine::deleteForward() {
    if (hasSel_) { deleteSelectionIfAny(); return; }
    int line = cursor_.line, col = cursor_.col;
    int lineLen = (int)lines_[line].size();
    if (col >= lineLen) {
        if (line + 1 >= (int)lines_.size()) return;
        applyReplaceTracked(line, col, line + 1, 0, "", false);
        return;
    }
    applyReplaceTracked(line, col, line, col + 1, "", true);
}

bool EditorEngine::undo() {
    if (undoStack_.empty()) return false;
    EditRecord rec = undoStack_.back();
    undoStack_.pop_back();
    Position after;
    applyRawReplace(rec.startLine, rec.startCol, rec.newEndLine, rec.newEndCol, rec.removedText, &after);
    cursor_ = rec.cursorBefore;
    selAnchor_ = cursor_;
    hasSel_ = false;
    clampCursor();
    redoStack_.push_back(rec);
    return true;
}

bool EditorEngine::redo() {
    if (redoStack_.empty()) return false;
    EditRecord rec = redoStack_.back();
    redoStack_.pop_back();
    Position after;
    applyRawReplace(rec.startLine, rec.startCol, rec.origEndLine, rec.origEndCol, rec.insertedText, &after);
    cursor_ = rec.cursorAfter;
    selAnchor_ = cursor_;
    hasSel_ = false;
    clampCursor();
    undoStack_.push_back(rec);
    return true;
}

// --- أوامر مستوى-السطر --------------------------------------------------

void EditorEngine::duplicateCurrentLine() {
    int line = cursor_.line;
    int col = cursor_.col;
    const std::string& content = lines_[line];
    applyReplaceTracked(line, (int)content.size(), line, (int)content.size(), "\n" + content, false);
    cursor_ = {line + 1, col};
    selAnchor_ = cursor_;
}

void EditorEngine::deleteCurrentLine() {
    int line = cursor_.line;
    int col = cursor_.col;
    if (lines_.size() == 1) {
        applyReplaceTracked(0, 0, 0, (int)lines_[0].size(), "", false);
        cursor_ = {0, 0};
        selAnchor_ = cursor_;
        return;
    }
    if (line + 1 < (int)lines_.size()) {
        applyReplaceTracked(line, 0, line + 1, 0, "", false);
        cursor_ = {line, std::min(col, (int)lines_[std::min(line, (int)lines_.size() - 1)].size())};
    } else {
        int prevEnd = (int)lines_[line - 1].size();
        applyReplaceTracked(line - 1, prevEnd, line, (int)lines_[line].size(), "", false);
        cursor_ = {line - 1, prevEnd};
    }
    selAnchor_ = cursor_;
}

void EditorEngine::moveLineUp() {
    int line = cursor_.line;
    if (line == 0) return;
    int col = cursor_.col;
    std::string cur = lines_[line];
    std::string prev = lines_[line - 1];
    applyReplaceTracked(line - 1, 0, line, (int)cur.size(), cur + "\n" + prev, false);
    cursor_ = {line - 1, col};
    selAnchor_ = cursor_;
}

void EditorEngine::moveLineDown() {
    int line = cursor_.line;
    if (line + 1 >= (int)lines_.size()) return;
    int col = cursor_.col;
    std::string cur = lines_[line];
    std::string next = lines_[line + 1];
    applyReplaceTracked(line, 0, line + 1, (int)next.size(), next + "\n" + cur, false);
    cursor_ = {line + 1, col};
    selAnchor_ = cursor_;
}

void EditorEngine::toggleLineComment() {
    int sl, sc, el, ec;
    if (hasSel_) getSelection(&sl, &sc, &el, &ec);
    else { sl = el = cursor_.line; sc = cursor_.col; ec = cursor_.col; }

    bool allCommented = true;
    for (int i = sl; i <= el; ++i) {
        std::string trimmed = lines_[i];
        size_t firstNonWs = trimmed.find_first_not_of(" \t");
        if (firstNonWs == std::string::npos) continue; // سطر فارغ لا يُحتسَب
        if (trimmed.compare(firstNonWs, 2, "//") != 0) { allCommented = false; break; }
    }

    std::vector<std::string> newLines;
    for (int i = sl; i <= el; ++i) {
        const std::string& l = lines_[i];
        size_t firstNonWs = l.find_first_not_of(" \t");
        if (firstNonWs == std::string::npos) { newLines.push_back(l); continue; }
        if (allCommented) {
            size_t idx = l.find("//");
            if (idx == std::string::npos) { newLines.push_back(l); continue; }
            std::string after = l.substr(idx + 2);
            if (!after.empty() && after[0] == ' ') after = after.substr(1);
            newLines.push_back(l.substr(0, idx) + after);
        } else {
            newLines.push_back(l.substr(0, firstNonWs) + "// " + l.substr(firstNonWs));
        }
    }
    std::string block;
    for (size_t i = 0; i < newLines.size(); ++i) { block += newLines[i]; if (i + 1 < newLines.size()) block += '\n'; }
    int origEc = (int)lines_[el].size();
    applyReplaceTracked(sl, 0, el, origEc, block, false);
}

void EditorEngine::indentSelection() {
    int sl, sc, el, ec;
    if (hasSel_) getSelection(&sl, &sc, &el, &ec);
    else { sl = el = cursor_.line; }
    std::vector<std::string> newLines;
    for (int i = sl; i <= el; ++i) {
        newLines.push_back(lines_[i].empty() ? lines_[i] : ("    " + lines_[i]));
    }
    std::string block;
    for (size_t i = 0; i < newLines.size(); ++i) { block += newLines[i]; if (i + 1 < newLines.size()) block += '\n'; }
    int origEc = (int)lines_[el].size();
    applyReplaceTracked(sl, 0, el, origEc, block, false);
}

void EditorEngine::unindentSelection() {
    int sl, sc, el, ec;
    if (hasSel_) getSelection(&sl, &sc, &el, &ec);
    else { sl = el = cursor_.line; }
    std::vector<std::string> newLines;
    for (int i = sl; i <= el; ++i) {
        const std::string& l = lines_[i];
        int removed = 0;
        if (l.compare(0, 4, "    ") == 0) removed = 4;
        else if (!l.empty() && l[0] == '\t') removed = 1;
        else { while (removed < (int)l.size() && removed < 4 && l[removed] == ' ') ++removed; }
        newLines.push_back(l.substr(std::min((size_t)removed, l.size())));
    }
    std::string block;
    for (size_t i = 0; i < newLines.size(); ++i) { block += newLines[i]; if (i + 1 < newLines.size()) block += '\n'; }
    int origEc = (int)lines_[el].size();
    applyReplaceTracked(sl, 0, el, origEc, block, false);
}

// --- فحص/بحث --------------------------------------------------------------

int EditorEngine::checkBracketBalance() const {
    struct Open { char ch; int line; };
    std::vector<Open> stack;
    for (int i = 0; i < (int)lines_.size(); ++i) {
        for (char c : lines_[i]) {
            if (kCloserOf.count(c)) stack.push_back({c, i + 1});
            else if (kOpenerOf.count(c)) {
                if (stack.empty() || kCloserOf.at(stack.back().ch) != c) return i + 1;
                stack.pop_back();
            }
        }
    }
    if (!stack.empty()) return stack.back().line;
    return -1;
}

std::vector<Match> EditorEngine::findAll(const std::string& query, bool caseSensitive) const {
    std::vector<Match> out;
    if (query.empty()) return out;
    auto norm = [&](std::string s) {
        if (!caseSensitive) for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    std::string q = norm(query);
    for (int i = 0; i < (int)lines_.size(); ++i) {
        std::string hay = norm(lines_[i]);
        size_t pos = 0;
        while (true) {
            size_t idx = hay.find(q, pos);
            if (idx == std::string::npos) break;
            out.push_back({i, (int)idx, (int)(idx + q.size())});
            pos = idx + std::max<size_t>(q.size(), 1);
        }
    }
    return out;
}

Position EditorEngine::lineStartPosition(int oneBasedLine) const {
    int idx = clampLine(oneBasedLine - 1);
    return {idx, 0};
}

// --- تلوين نحوي حقيقي -------------------------------------------------------

static HighlightKind kindForToken(rin::TokenType t) {
    using TT = rin::TokenType;
    switch (t) {
        case TT::NUMBER: return HighlightKind::Number;
        case TT::STRING: return HighlightKind::String;
        case TT::IDENT: return HighlightKind::Ident;
        case TT::LPAREN: case TT::RPAREN: case TT::LBRACE: case TT::RBRACE:
        case TT::LBRACKET: case TT::RBRACKET:
            return HighlightKind::Bracket;
        case TT::AT: return HighlightKind::At;
        case TT::ERROR: return HighlightKind::Error;
        case TT::PLUS: case TT::MINUS: case TT::STAR: case TT::SLASH: case TT::PERCENT:
        case TT::EQUAL: case TT::EQUAL_EQUAL: case TT::BANG: case TT::BANG_EQUAL:
        case TT::LESS: case TT::LESS_EQUAL: case TT::GREATER: case TT::GREATER_EQUAL:
        case TT::COLON: case TT::COMMA: case TT::SEMICOLON: case TT::DOT: case TT::PIPE:
            return HighlightKind::Operator;
        case TT::END_OF_FILE:
            return HighlightKind::Default;
        // كلمات لغة الحاويات/الأنواع — هوية بنفسجية منفصلة عن الكلمات المفتاحية الأساسية
        // (let/if/while/fun...) التي تبقى زرقاء عبر الفرع default أدناه. بدون هذا الفرع
        // كانت كل هذه الكلمات تصل إلى default وتُلوَّن كـ Keyword خطأً (أزرق بدل بنفسجي).
        case TT::TEXT: case TT::CONTAINER: case TT::CONTAINERS: case TT::GROUP:
        case TT::VOLUME: case TT::SECTION: case TT::TRANSLATIONS: case TT::TRANSLATION:
        case TT::LINK: case TT::TYING: case TT::MERGE: case TT::INSTALLATION:
        case TT::SIMPLIFIED: case TT::SAVE: case TT::FILE_KW: case TT::END:
        case TT::PIPE_KW:
            return HighlightKind::Type;
        default:
            // كل الكلمات المحجوزة الأخرى (let/if/while/fun/return/break/continue/...)
            return HighlightKind::Keyword;
    }
}

// rin::Lexer يُسقط تعليقات "//" بصمت أثناء المسح (لا يوجد TokenType لها إطلاقاً)، لذا لا
// يمكن استخراج مواضعها من التوكنز مباشرة. لكن بما أن scanTokens() (إن لم يرمِ استثناءً) يكون
// قد فسَّر *كل* حرف في المصدر كواحد من: مسافة/تبويب/سطر جديد، جزء من توكن حقيقي (بما فيها
// النصوص متعددة الأسطر)، أو تعليق — فإن أي "فجوة" متبقية على سطر ما بعد طرح كل نطاقات
// التوكنز منه، وليست فراغاً بيضاء بالكامل، يجب أن تكون تعليقاً (يبدأ حتماً بـ"//" وحتى نهاية
// السطر). هذه الدالة تستنتج تلك الفجوات دون أي حاجة لتعديل rin::Lexer نفسه.
static std::vector<HighlightSpan> computeCommentGaps(
    const std::vector<rin::Token>& tokens, const std::vector<std::string>& lines) {
    std::vector<HighlightSpan> comments;
    // occupied[line] = نطاقات [start,end) بايتيّة مُغطّاة بتوكن حقيقي على هذا السطر (قد يمتد
    // توكن نص واحد عبر عدة أسطر إن احتوى `\n` مُهرَّباً أو حرفياً).
    std::vector<std::vector<std::pair<int, int>>> occupied(lines.size());
    for (const rin::Token& tok : tokens) {
        if (tok.type == rin::TokenType::END_OF_FILE) continue;
        int startLine0 = tok.line - 1;
        // endLine غير متوفر مباشرة في Token (فقط line/col/endCol لسطر البداية)؛ التوكن الوحيد
        // القادر على امتداد أسطر متعددة فعلياً هو STRING، ونُقدّر امتداده بعدّ '\n' داخل lexeme.
        int spannedLines = 0;
        if (tok.type == rin::TokenType::STRING) {
            for (char c : tok.lexeme) if (c == '\n') spannedLines++;
        }
        int endLine0 = startLine0 + spannedLines;
        int startCol0 = std::max(0, tok.col - 1);
        if (startLine0 < 0 || startLine0 >= (int)lines.size()) continue;
        if (spannedLines == 0) {
            int endCol0 = tok.endCol - 1;
            occupied[startLine0].push_back({startCol0, std::max(startCol0, endCol0)});
        } else {
            // أول سطر: من startCol0 حتى نهاية نص السطر الفعلي (المُقتبس يستمر بعده).
            occupied[startLine0].push_back({startCol0, (int)lines[startLine0].size()});
            // الأسطر الوسطى بالكامل داخل السلسلة النصية.
            for (int l = startLine0 + 1; l < endLine0 && l < (int)lines.size(); ++l) {
                occupied[l].push_back({0, (int)lines[l].size()});
            }
            // آخر سطر: من بدايته حتى عمود الإغلاق endCol.
            if (endLine0 >= 0 && endLine0 < (int)lines.size()) {
                occupied[endLine0].push_back({0, std::max(0, tok.endCol - 1)});
            }
        }
    }

    for (size_t line = 0; line < lines.size(); ++line) {
        const std::string& text = lines[line];
        if (text.empty()) continue;
        auto& ranges = occupied[line];
        std::sort(ranges.begin(), ranges.end());
        int cursor = 0;
        for (const auto& r : ranges) {
            if (r.first > cursor) {
                // فجوة [cursor, r.first) غير مُغطّاة بأي توكن — تحقّق إن كانت تعليقاً حقيقياً.
                int gapEnd = r.first;
                int ws = cursor;
                while (ws < gapEnd && (text[ws] == ' ' || text[ws] == '\t' || text[ws] == '\r')) ws++;
                if (ws < gapEnd && ws + 1 < (int)text.size() && text[ws] == '/' && text[ws + 1] == '/') {
                    comments.push_back({(int)line, ws, gapEnd, HighlightKind::Comment});
                }
            }
            cursor = std::max(cursor, r.second);
        }
        // الفجوة الأخيرة بعد آخر توكن حتى نهاية السطر.
        int gapEnd = (int)text.size();
        if (cursor < gapEnd) {
            int ws = cursor;
            while (ws < gapEnd && (text[ws] == ' ' || text[ws] == '\t' || text[ws] == '\r')) ws++;
            if (ws < gapEnd && ws + 1 < (int)text.size() && text[ws] == '/' && text[ws + 1] == '/') {
                comments.push_back({(int)line, ws, gapEnd, HighlightKind::Comment});
            }
        }
    }
    return comments;
}

std::vector<HighlightSpan> EditorEngine::computeHighlights() const {
    std::vector<HighlightSpan> out;
    std::string source = getText();
    try {
        rin::Lexer lexer(source);
        std::vector<rin::Token> tokens = lexer.scanTokens();
        for (size_t i = 0; i < tokens.size(); ++i) {
            const rin::Token& tok = tokens[i];
            if (tok.type == rin::TokenType::END_OF_FILE) continue;
            HighlightKind kind = kindForToken(tok.type);
            if (kind == HighlightKind::Ident && i + 1 < tokens.size() &&
                tokens[i + 1].type == rin::TokenType::LPAREN) {
                // معرِّف متبوع مباشرة بقوس فتح: نمط استدعاء دالة — يُلوَّن بلون مميّز
                // (نفس أسلوب المحررات الاحترافية مثل VS Code Dark+).
                kind = HighlightKind::Call;
            }
            if (kind == HighlightKind::Default) continue;
            int line0 = tok.line - 1;
            if (line0 < 0 || line0 >= (int)lines_.size()) continue;
            int startCol0 = tok.col - 1;
            int endCol0 = tok.endCol - 1;
            if (startCol0 < 0) startCol0 = 0;
            if (endCol0 <= startCol0) continue;
            out.push_back({line0, startCol0, endCol0, kind});
        }
        std::vector<HighlightSpan> comments = computeCommentGaps(tokens, lines_);
        out.insert(out.end(), comments.begin(), comments.end());
    } catch (...) {
        // نص مؤقتًا غير صالح نحويًا أثناء الكتابة (مثل علامة تنصيص لم تُغلَق بعد): rin::Lexer
        // يرمي استثناءً من داخل scanTokens() ولا يُرجع رموزًا جزئية، لذا نُعيد قائمة فارغة
        // (بلا تلوين) بدل تعطّل الواجهة بالكامل، إلى أن يُصلَح النص.
    }
    return out;
}

std::vector<std::string> EditorEngine::collectSuggestions(const std::string& prefix, int maxResults) const {
    std::vector<std::string> result;
    if (maxResults <= 0) return result;
    std::string prefixLower = prefix;
    for (auto& c : prefixLower) c = (char)std::tolower((unsigned char)c);

    auto startsWithPrefix = [&](const std::string& word) {
        if (word.size() < prefixLower.size()) return false;
        for (size_t i = 0; i < prefixLower.size(); ++i) {
            if (std::tolower((unsigned char)word[i]) != prefixLower[i]) return false;
        }
        return true;
    };

    // 1) الكلمات المحجوزة الفعلية للغة
    std::vector<std::string> keywordMatches;
    for (const std::string& kw : rin::keywordList()) {
        if (!prefixLower.empty() && !startsWithPrefix(kw)) continue;
        if (kw == prefix) continue; // النص المطابق تمامًا لما كُتب بالفعل لا فائدة من اقتراحه
        keywordMatches.push_back(kw);
    }
    std::sort(keywordMatches.begin(), keywordMatches.end());

    // 2) المعرِّفات (IDENT) الفريدة الظاهرة فعلاً في المستند الحالي
    std::unordered_set<std::string> identSet;
    try {
        rin::Lexer lexer(getText());
        for (const rin::Token& tok : lexer.scanTokens()) {
            if (tok.type != rin::TokenType::IDENT) continue;
            if (tok.lexeme == prefix) continue;
            if (!prefixLower.empty() && !startsWithPrefix(tok.lexeme)) continue;
            identSet.insert(tok.lexeme);
        }
    } catch (...) {
        // مستند غير صالح نحويًا مؤقتًا أثناء الكتابة: نكتفي باقتراحات الكلمات المحجوزة أعلاه.
    }
    std::vector<std::string> identMatches(identSet.begin(), identSet.end());
    std::sort(identMatches.begin(), identMatches.end());

    for (const auto& kw : keywordMatches) {
        if ((int)result.size() >= maxResults) return result;
        result.push_back(kw);
    }
    for (const auto& id : identMatches) {
        if ((int)result.size() >= maxResults) return result;
        result.push_back(id);
    }
    return result;
}

} // namespace rinedit

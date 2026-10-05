#include "rin_lexer.h"
#include "diagnostics/source_manager.h"
#include <cctype>
#include <unordered_map>

namespace rin {

static const std::unordered_map<std::string, TokenType> keywords = {
    {"let", TokenType::LET},       {"print", TokenType::PRINT},
    // 'show' مرادف إنجليزي مبسّط كامل لـ 'print' (نفس TokenType::PRINT حرفياً، فيرث كل صيغه وسماته
    // sep=/end=/level=/... وحتى show.log(...) بلا أي كود إضافي) — جزء من عائلة الكلمات السهلة
    // المرافقة لمفهوم make (انظر container.everything/make في rin_ast.h و atBlock() في rin_parser.cpp).
    {"show", TokenType::PRINT},
    {"if", TokenType::IF},         {"else", TokenType::ELSE},
    {"while", TokenType::WHILE},   {"fun", TokenType::FUN},
    {"return", TokenType::RETURN}, {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},   {"nil", TokenType::NIL},
    {"and", TokenType::AND},       {"or", TokenType::OR},
    {"break", TokenType::BREAK},   {"continue", TokenType::CONTINUE}, {"rinopen", TokenType::RINOPEN},
    {"for", TokenType::FOR}, // حلقة for على طراز C: for (init; condition; increment) { ... }

    // مفاهيم لغة الحاويات/البيانات (data container language) - حساسة لحالة الأحرف
    {"text", TokenType::TEXT},
    {"container", TokenType::CONTAINER},
    {"Containers", TokenType::CONTAINERS},
    {"Group", TokenType::GROUP},
    {"Volume", TokenType::VOLUME},
    {"Section", TokenType::SECTION},
    {"Translations", TokenType::TRANSLATIONS},
    {"translation", TokenType::TRANSLATION},
    {"link", TokenType::LINK},
    {"tying", TokenType::TYING},
    {"merge", TokenType::MERGE},
    {"installation", TokenType::INSTALLATION},
    {"simplified", TokenType::SIMPLIFIED},
    {"save", TokenType::SAVE},
    {"file", TokenType::FILE_KW},
    {"end", TokenType::END},
    {"pipe", TokenType::PIPE_KW}, // container.pipe -> خط أنابيب بيانات/إحصاء (كلمة محجوزة تاريخياً)
    // "data" / "api" / "import" / "route" ليست هنا عمداً: تُقرأ كـ IDENT عادي، ويُتعامل معها
    // سياقياً فقط في المحلل النحوي، حتى لا تصبح كلمات محجوزة تتعارض مع أسماء متغيرات المستخدم.
};

std::vector<std::string> keywordList() {
    std::vector<std::string> out;
    out.reserve(keywords.size());
    for (const auto& kv : keywords) out.push_back(kv.first);
    return out;
}

Lexer::Lexer(std::string source, std::string filename) : src(std::move(source)), file(std::move(filename)) {
    diag::globalSourceManager().addFile(file, src);
}

bool Lexer::isAtEnd() const { return current >= src.size(); }

char Lexer::advance() { return src[current++]; }

char Lexer::peek() const { return isAtEnd() ? '\0' : src[current]; }

char Lexer::peekNext() const {
    return (current + 1 >= src.size()) ? '\0' : src[current + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd() || src[current] != expected) return false;
    current++;
    return true;
}

void Lexer::addToken(TokenType type) {
    addToken(type, src.substr(start, current - start));
}

void Lexer::addToken(TokenType type, const std::string& lexeme) {
    Token t;
    t.type = type;
    t.lexeme = lexeme;
    t.line = line;
    t.col = columnOf(start);
    t.endCol = columnOf(current);
    tokens.push_back(t);
}

void Lexer::scanString() {
    std::string value;
    while (peek() != '"' && !isAtEnd()) {
        char c = peek();
        if (c == '\n') { line++; value += advance(); continue; }
        // دعم التهريب (escape sequences): \" \\ \n \t \r  -> ضروري لجعل النصوص المحفوظة
        // فعلياً عبر save/installation (والتي قد تحتوي أقواس تنصيص أو أسطر جديدة) قابلة لإعادة القراءة.
        if (c == '\\') {
            advance(); // استهلاك '\'
            if (isAtEnd()) break;
            char esc = advance();
            switch (esc) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '\n': line++; break; // backslash قبل سطر جديد: يُتجاهل (متابعة سطر)
                default: value += esc; break; // تسلسل غير معروف: يُبقي الحرف كما هو حرفياً
            }
            continue;
        }
        value += advance();
    }
    if (isAtEnd()) {
        diag::Diagnostic d(diag::Code::E0011_UnexpectedToken, "unterminated string literal",
                            diag::SourceLocation::point(file, line, columnOf(start)));
        d.withReason("a `\"` was opened here but never closed before the end of the file")
         .withHint("close the string with a matching `\"`");
        throw RinError(std::move(d));
    }
    advance(); // closing quote
    Token t;
    t.type = TokenType::STRING;
    t.lexeme = value;
    t.line = line;
    t.col = columnOf(start);
    t.endCol = columnOf(current);
    tokens.push_back(t);
}

void Lexer::pushToken(TokenType type, const std::string& lexeme, int ln, int col, int endCol) {
    Token t;
    t.type = type;
    t.lexeme = lexeme;
    t.line = ln;
    t.col = col;
    t.endCol = endCol;
    tokens.push_back(t);
}

// ─────────────────────────────────────────────────────────────────────────────
// النصوص القالبية (Template strings):   `مرحباً ${name}، عمرك ${age + 1}`
//
//   ${تعبير}                 أي تعبير Rin (متغيّر، حساب، دالة، قالب متداخل ...)
//   ${تعبير:تنسيق}           تنسيق القيمة  (.2f  ,d  05  >8  *^10  x  upper  trim|upper ...)
//   ${تعبير:.${n}f}          التنسيق نفسه قالب: يقبل ${} متداخلة (دقة/عرض ديناميكي)
//   ${تعبير ?? بديل}         بديل إن كانت القيمة nil
//   ${تعبير=}                وضع التصحيح: يطبع  تعبير=قيمة
//   ${#date} ${#version} ...  قيم جاهزة
//
//   - تبدأ وتنتهي بعلامة backtick (`) — لم تكن صالحة سابقاً (E0011)، فلا تعارض مع أي كود قديم،
//     والنصوص العادية "..." تبقى حرفية تماماً.
//   - التهريب: \` \$ \\ \n \t \r \" و \u{1F600} (حرف يونيكود بالـ hex).
//
// التنفيذ "desugaring" داخل الـ Lexer فقط: يُحوَّل القالب إلى رموز عادية
//     ( "نص" + #str(تعبير) + #fmt(تعبير, "تنسيق") + ... )
// فلا يحتاج المحلل النحوي ولا المفسِّر لأي تعديل، ويعمل في CLI وأندرويد والويب.
// يبدأ التعبير دائماً بنص (ولو فارغ) لضمان أن `+` دمج نصوص وليست جمع أرقام.
// ─────────────────────────────────────────────────────────────────────────────
static void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

void Lexer::scanTemplate() {
    scanTemplateParts('`');
}

// term == '`' : قالب كامل.   term == '}' : نص مواصفة التنسيق داخل ${expr:...} (يقبل ${} متداخلة).
void Lexer::scanTemplateParts(char term) {
    const int openLine = line;
    const int openCol = columnOf(start);

    auto unterminated = [&]() {
        diag::Diagnostic d(diag::Code::E0011_UnexpectedToken,
                            term == '`' ? "unterminated template string" : "unterminated format in template string",
                            diag::SourceLocation::point(file, openLine, openCol));
        d.withReason(term == '`' ? "a backtick ` was opened here but never closed before the end of the file"
                                 : "a `${` ... `:` format was opened here but its closing `}` is missing")
         .withHint(term == '`' ? "close the template with a matching backtick `" : "close the format with `}`");
        throw RinError(std::move(d));
    };

    const size_t openIdx = tokens.size();
    bool hadExpr = false;
    pushToken(TokenType::LPAREN, "(", openLine, openCol, openCol + 1);
    bool needPlus = false;
    bool emittedAnyText = false;

    auto flushText = [&](const std::string& text, bool force) {
        if (text.empty() && !force) return;
        if (needPlus) pushToken(TokenType::PLUS, "+", line, columnOf(current), columnOf(current));
        pushToken(TokenType::STRING, text, line, openCol, columnOf(current));
        needPlus = true;
        emittedAnyText = true;
    };

    std::string text;
    while (true) {
        if (isAtEnd()) unterminated();
        char c = peek();
        if (c == term) { advance(); break; }
        if (c == '\n') {
            if (term == '}') unterminated();
            line++;
            text += advance();
            lineStartOffset = current;
            continue;
        }
        if (c == '\\') {
            advance();
            if (isAtEnd()) unterminated();
            char esc = advance();
            switch (esc) {
                case 'n': text += '\n'; break;
                case 't': text += '\t'; break;
                case 'r': text += '\r'; break;
                case '`': text += '`'; break;
                case '$': text += '$'; break;
                case '"': text += '"'; break;
                case '\\': text += '\\'; break;
                case '}': text += '}'; break;
                case '\n': line++; lineStartOffset = current; break;
                case 'u':
                    if (peek() == '{') {
                        size_t save = current;
                        advance();
                        std::string hex;
                        while (isxdigit((unsigned char)peek()) && hex.size() < 8) hex += advance();
                        if (peek() == '}' && !hex.empty()) { advance(); appendUtf8(text, std::stoul(hex, nullptr, 16)); break; }
                        current = save; // ليست \u{hex} صحيحة: تُعامل كحرف u عادي
                    }
                    text += 'u';
                    break;
                default: text += esc; break;
            }
            continue;
        }
        if (c == '$' && peekNext() == '{') {
            const int exprLine = line;
            const int exprCol = columnOf(current);
            advance(); advance(); // ${
            hadExpr = true;
            flushText(text, /*force=*/!emittedAnyText);
            text.clear();
            if (needPlus) pushToken(TokenType::PLUS, "+", exprLine, exprCol, exprCol + 2);

            // مسح التعبير بنفس scanToken() حتى } المطابقة، مع تتبّع:
            //   depth : عمق ( [ { ؛  ternary : عدد ? المعلّقة (لتمييز : الشرط عن : التنسيق)
            //   ??    : علامتا ? متجاورتان = قيمة افتراضية ؛  = أخيرة قبل } أو : = وضع التصحيح
            const size_t exprBeginOffset = current;
            const size_t exprStart = tokens.size();
            int depth = 0, ternary = 0;
            bool hasSpec = false, closed = false;
            size_t eqOffset = std::string::npos;
            std::vector<Token> specToks;
            while (!closed) {
                if (isAtEnd()) unterminated();
                start = current;
                const size_t before = tokens.size();
                scanToken();
                for (size_t k = before; k < tokens.size() && !closed; k++) {
                    TokenType tt = tokens[k].type;
                    if (tt == TokenType::LBRACE || tt == TokenType::LPAREN || tt == TokenType::LBRACKET) depth++;
                    else if (tt == TokenType::RPAREN || tt == TokenType::RBRACKET) depth--;
                    else if (tt == TokenType::RBRACE) {
                        if (depth == 0) { tokens.erase(tokens.begin() + k); closed = true; }
                        else depth--;
                    } else if (tt == TokenType::EQUAL && depth == 0 && k + 1 == tokens.size()) {
                        eqOffset = start;
                    } else if (tt == TokenType::QUESTION && depth == 0) {
                        bool pair = k > 0 && tokens[k - 1].type == TokenType::QUESTION &&
                                    tokens[k - 1].line == tokens[k].line && tokens[k - 1].endCol == tokens[k].col;
                        if (pair) ternary--; else ternary++;
                    } else if (tt == TokenType::COLON && depth == 0) {
                        if (ternary > 0) ternary--;
                        else {
                            tokens.erase(tokens.begin() + k);
                            const size_t specStart = tokens.size();
                            start = current; // لحساب موضع الخطأ عند عدم الإغلاق
                            scanTemplateParts('}');
                            specToks.assign(tokens.begin() + specStart, tokens.end());
                            tokens.resize(specStart);
                            hasSpec = true;
                            closed = true;
                        }
                    }
                }
            }
            std::vector<Token> ex(tokens.begin() + exprStart, tokens.end());
            tokens.resize(exprStart);

            // وضع التصحيح  ${x=}  ->  "x=" + قيمة x
            std::string debugLabel;
            if (!ex.empty() && ex.back().type == TokenType::EQUAL && eqOffset != std::string::npos && eqOffset >= exprBeginOffset) {
                std::string lab = src.substr(exprBeginOffset, eqOffset - exprBeginOffset);
                size_t b0 = lab.find_first_not_of(" \t\r\n"), b1 = lab.find_last_not_of(" \t\r\n");
                debugLabel = (b0 == std::string::npos ? "" : lab.substr(b0, b1 - b0 + 1)) + "=";
                ex.pop_back();
            }
            if (ex.empty()) {
                diag::Diagnostic d(diag::Code::E0011_UnexpectedToken, "empty `${}` in template string",
                                    diag::SourceLocation::point(file, exprLine, exprCol));
                d.withReason("nothing was written between `${` and `}`")
                 .withHint("put a variable or expression inside, e.g. `${name}`");
                throw RinError(std::move(d));
            }

            auto tk = [&](TokenType ty, const std::string& lx) {
                Token t; t.type = ty; t.lexeme = lx; t.line = exprLine; t.col = exprCol; t.endCol = exprCol + 2;
                return t;
            };

            // القيم الجاهزة: ${#date} ${#time} ${#datetime} ${#year} ${#month} ${#day} ${#hour}
            //                ${#minute} ${#second} ${#weekday} ${#version} ${#line} ${#file}
            static const char* const kBuiltin[] = {"date", "time", "datetime", "year", "month", "day",
                                                    "hour", "minute", "second", "weekday", "version"};
            if (ex.size() == 1 && ex[0].type == TokenType::IDENT && ex[0].lexeme.size() > 1 && ex[0].lexeme[0] == '#') {
                const std::string nm = ex[0].lexeme.substr(1);
                if (nm == "line") {
                    Token t = tk(TokenType::NUMBER, std::to_string(exprLine)); t.number = exprLine; ex = {t};
                } else if (nm == "file") {
                    ex = {tk(TokenType::STRING, file)};
                } else {
                    for (const char* bn : kBuiltin) if (nm == bn) {
                        ex = {tk(TokenType::IDENT, "#now"), tk(TokenType::LPAREN, "("),
                              tk(TokenType::STRING, nm), tk(TokenType::RPAREN, ")")};
                        break;
                    }
                }
            }

            // القيمة الافتراضية: A ?? B  ->  #default(A, B)   (على العمق 0 وخارج أي شرط ثلاثي)
            {
                int dp = 0, tern = 0;
                for (size_t k = 0; k + 1 < ex.size(); k++) {
                    TokenType tt = ex[k].type;
                    if (tt == TokenType::LBRACE || tt == TokenType::LPAREN || tt == TokenType::LBRACKET) dp++;
                    else if (tt == TokenType::RBRACE || tt == TokenType::RPAREN || tt == TokenType::RBRACKET) dp--;
                    else if (dp == 0 && tt == TokenType::QUESTION) {
                        bool pair = ex[k + 1].type == TokenType::QUESTION && ex[k + 1].line == ex[k].line &&
                                    ex[k + 1].col == ex[k].endCol;
                        if (pair && tern == 0) {
                            std::vector<Token> out{tk(TokenType::IDENT, "#default"), tk(TokenType::LPAREN, "(")};
                            out.insert(out.end(), ex.begin(), ex.begin() + k);
                            out.push_back(tk(TokenType::COMMA, ","));
                            out.insert(out.end(), ex.begin() + k + 2, ex.end());
                            out.push_back(tk(TokenType::RPAREN, ")"));
                            ex = std::move(out);
                            break;
                        }
                        if (pair) k++; else tern++;
                    } else if (dp == 0 && tt == TokenType::COLON && tern > 0) tern--;
                }
            }

            if (!debugLabel.empty()) {
                pushToken(TokenType::STRING, debugLabel, exprLine, exprCol, exprCol + 2);
                pushToken(TokenType::PLUS, "+", exprLine, exprCol, exprCol + 2);
            }
            pushToken(TokenType::IDENT, hasSpec ? "#fmt" : "#str", exprLine, exprCol, exprCol + 2);
            pushToken(TokenType::LPAREN, "(", exprLine, exprCol, exprCol + 2);
            tokens.insert(tokens.end(), ex.begin(), ex.end());
            if (hasSpec) {
                pushToken(TokenType::COMMA, ",", exprLine, exprCol, exprCol + 2);
                tokens.insert(tokens.end(), specToks.begin(), specToks.end());
            }
            pushToken(TokenType::RPAREN, ")", line, columnOf(current) - 1, columnOf(current));
            needPlus = true;
            continue;
        }
        text += advance();
    }
    flushText(text, /*force=*/!emittedAnyText);
    if (!hadExpr) {
        // قالب بلا ${}: نص ثابت عادي -> رمز STRING واحد، فيصلح في كل المواضع التي تشترط نصاً ثابتاً
        // (translation / link.id / @import / .object("id") / on.event ...) ويدعم أسطراً متعددة وعلامات " بلا تهريب.
        std::string only = tokens[openIdx + 1].lexeme;
        tokens.resize(openIdx);
        pushToken(TokenType::STRING, only, openLine, openCol, columnOf(current));
        return;
    }
    pushToken(TokenType::RPAREN, ")", line, columnOf(current) - 1, columnOf(current));
    if (term == '`') tokens[openIdx].tpl = true; // ( افتتاحية قالب ديناميكي: يتعرّف عليها المحلل في emit / on.event / translation
}

void Lexer::scanNumber() {
    while (isdigit((unsigned char)peek())) advance();
    if (peek() == '.' && isdigit((unsigned char)peekNext())) {
        advance();
        while (isdigit((unsigned char)peek())) advance();
    }
    std::string text = src.substr(start, current - start);
    Token t;
    t.type = TokenType::NUMBER;
    t.lexeme = text;
    t.number = std::stod(text);
    t.line = line;
    t.col = columnOf(start);
    t.endCol = columnOf(current);
    tokens.push_back(t);
}

void Lexer::scanIdentifier() {
    while (isalnum((unsigned char)peek()) || peek() == '_') advance();
    std::string text = src.substr(start, current - start);
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        addToken(it->second, text);
    } else {
        addToken(TokenType::IDENT, text);
    }
}

// عائلة `#` — كل مفهوم/متغيّر/دالة جديدة تبدأ بعلامة `#` (مثل #sed #do #done #swap #ban ...).
// `#` وحدها لم تكن صالحة سابقاً (كانت خطأ E0011)، فلا تعارض مع أي كود موجود.
//   - #for / #while / #return  -> نفس توكنات for / while / return حرفياً (مرادفات كاملة).
//   - #in / #to                 -> تُقرأ كالكلمتين السياقيتين in / to (IDENT بنصّ in / to).
//   - أي #name آخر              -> IDENT يحتفظ بعلامة # في نصّه ("#sed", "#do", "#ban", "#x1" ...)،
//                                 فيمكن استعماله اسم دالة أصلية أو كلمة سياقية في المحلل النحوي
//                                 أو اسم متغيّر عادي (let #total = 0;).
void Lexer::scanHashWord() {
    if (!(isalpha((unsigned char)peek()) || peek() == '_')) {
        diag::Diagnostic d(diag::Code::E0011_UnexpectedToken, "unexpected character `#`",
                            diag::SourceLocation::point(file, line, columnOf(start)));
        d.withReason("`#` must be followed directly by a name, e.g. `#sed`, `#do`, `#swap`")
         .withHint("write the name right after `#` with no space");
        throw RinError(std::move(d));
    }
    while (isalnum((unsigned char)peek()) || peek() == '_') advance();
    std::string text = src.substr(start, current - start); // يشمل '#'
    const std::string word = text.substr(1);
    if (word == "for")        { addToken(TokenType::FOR, text); return; }
    if (word == "while")      { addToken(TokenType::WHILE, text); return; }
    if (word == "return")     { addToken(TokenType::RETURN, text); return; }
    if (word == "in") { addToken(TokenType::IDENT, word); return; } // #to يبقى "#to" (دالة أصلية + كلمة المدى في for ... in)
    addToken(TokenType::IDENT, text);
}

void Lexer::scanToken() {
    char c = advance();
    switch (c) {
        case '(': addToken(TokenType::LPAREN); break;
        case ')': addToken(TokenType::RPAREN); break;
        case '{': addToken(TokenType::LBRACE); break;
        case '}': addToken(TokenType::RBRACE); break;
        case '[': addToken(TokenType::LBRACKET); break;
        case ']': addToken(TokenType::RBRACKET); break;
        case ':': addToken(TokenType::COLON); break;
        case ',': addToken(TokenType::COMMA); break;
        case ';': addToken(TokenType::SEMICOLON); break;
        case '+': addToken(TokenType::PLUS); break;
        case '-': addToken(TokenType::MINUS); break;
        case '*': addToken(TokenType::STAR); break;
        case '%': addToken(TokenType::PERCENT); break;
        case '#': scanHashWord(); break;
        case '@': addToken(TokenType::AT); break;
        case '.': addToken(TokenType::DOT); break;
        case '?': addToken(TokenType::QUESTION); break;
        case '|':
            if (match('>')) {
                addToken(TokenType::PIPE);
            } else {
                diag::Diagnostic d(diag::Code::E0011_UnexpectedToken, "unexpected character `|`",
                                    diag::SourceLocation::point(file, line, columnOf(start)));
                d.withReason("a lone `|` is not a valid operator in Rin")
                 .withHint("did you mean the pipe operator `|>` ?");
                throw RinError(std::move(d));
            }
            break;
        case '/':
            if (match('/')) {
                while (peek() != '\n' && !isAtEnd()) advance();
            } else if (match('=')) {
                addToken(TokenType::BANG_EQUAL); // `/=` مرادف `!=` (لا يوجد إسناد مركّب في Rin)
            } else {
                addToken(TokenType::SLASH);
            }
            break;
        case '=':
            if (match('=')) {
                addToken(TokenType::EQUAL_EQUAL);
            } else if (peek() == '/' && peekNext() == '=') {
                advance(); advance();
                addToken(TokenType::NOT_SAME);   // =/=
            } else if (peek() == '/' && peekNext() != '/') {
                advance();
                addToken(TokenType::COPY_OF);    // =/   (لا يلتقط `=//` تعليقاً)
            } else {
                addToken(TokenType::EQUAL);
            }
            break;
        case '!':
            addToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);
            break;
        case '<':
            addToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);
            break;
        case '>':
            addToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);
            break;
        case ' ':
        case '\r':
        case '\t':
            break;
        case '\n':
            line++;
            lineStartOffset = current;
            break;
        case '"':
            scanString();
            break;
        case '`':
            scanTemplate();
            break;
        default:
            if (isdigit((unsigned char)c)) {
                scanNumber();
            } else if (isalpha((unsigned char)c) || c == '_') {
                scanIdentifier();
            } else {
                diag::Diagnostic d(diag::Code::E0011_UnexpectedToken,
                                    std::string("unexpected character `") + c + "`",
                                    diag::SourceLocation::point(file, line, columnOf(start)));
                d.withReason("this character does not start any valid token in Rin");
                throw RinError(std::move(d));
            }
    }
}

std::vector<Token> Lexer::scanTokens() {
    while (!isAtEnd()) {
        start = current;
        scanToken();
    }
    Token eof;
    eof.type = TokenType::END_OF_FILE;
    eof.line = line;
    eof.col = columnOf(current);
    eof.endCol = eof.col + 1;
    tokens.push_back(eof);
    return tokens;
}

} // namespace rin

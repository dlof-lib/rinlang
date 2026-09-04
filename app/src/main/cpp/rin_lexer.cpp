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
        case '@': addToken(TokenType::AT); break;
        case '.': addToken(TokenType::DOT); break;
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
            } else {
                addToken(TokenType::SLASH);
            }
            break;
        case '=':
            addToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);
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

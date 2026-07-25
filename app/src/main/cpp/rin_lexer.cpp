#include "rin_lexer.h"
#include <cctype>
#include <unordered_map>

namespace rin {

static const std::unordered_map<std::string, TokenType> keywords = {
    {"let", TokenType::LET},       {"print", TokenType::PRINT},
    {"if", TokenType::IF},         {"else", TokenType::ELSE},
    {"while", TokenType::WHILE},   {"fun", TokenType::FUN},
    {"return", TokenType::RETURN}, {"true", TokenType::TRUE},
    {"false", TokenType::FALSE},   {"nil", TokenType::NIL},
    {"and", TokenType::AND},       {"or", TokenType::OR},

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

Lexer::Lexer(std::string source) : src(std::move(source)) {}

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
    if (isAtEnd()) throw RinError("String not terminated", line);
    advance(); // closing quote
    Token t;
    t.type = TokenType::STRING;
    t.lexeme = value;
    t.line = line;
    tokens.push_back(t);
}

void Lexer::scanNumber() {
    while (isdigit(peek())) advance();
    if (peek() == '.' && isdigit(peekNext())) {
        advance();
        while (isdigit(peek())) advance();
    }
    std::string text = src.substr(start, current - start);
    Token t;
    t.type = TokenType::NUMBER;
    t.lexeme = text;
    t.number = std::stod(text);
    t.line = line;
    tokens.push_back(t);
}

void Lexer::scanIdentifier() {
    while (isalnum(peek()) || peek() == '_') advance();
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
                throw RinError("Unexpected character '|': did you mean the pipe operator '|>' ?", line);
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
            break;
        case '"':
            scanString();
            break;
        default:
            if (isdigit(c)) {
                scanNumber();
            } else if (isalpha(c) || c == '_') {
                scanIdentifier();
            } else {
                throw RinError(std::string("Unexpected character '") + c + "'", line);
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
    tokens.push_back(eof);
    return tokens;
}

} // namespace rin

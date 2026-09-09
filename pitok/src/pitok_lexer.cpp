// pitok_lexer.cpp — تنفيذ المُجزِّئ المعجمي لـ PITOK.
// يدعم بالضبط ما تحتاجه صياغة الواجهات المرصودة في عيّنات RinLang الحالية
// (@view.Kind=name، @loop=name، سمات key=value;، تعابير +/-/*، نداءات دوال، سلاسل، أرقام)
// دون أي اعتماد على lexer/parser لغة Rin العامة.

#include "pitok_lexer.h"
#include <cctype>
#include <unordered_map>

namespace pitok {

Lexer::Lexer(std::string source, std::string filename)
    : src(std::move(source)), file(std::move(filename)) {}

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
    Token t;
    t.type = type;
    t.lexeme = src.substr(start, current - start);
    t.line = line;
    t.column = columnOf(start);
    tokens.push_back(std::move(t));
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\r' || c == '\t') {
            advance();
        } else if (c == '\n') {
            advance();
            line++;
            lineStartOffset = current;
        } else if (c == '/' && peekNext() == '/') {
            // تعليق سطر واحد — يمتد حتى نهاية السطر
            while (peek() != '\n' && !isAtEnd()) advance();
        } else if (c == '/' && peekNext() == '*') {
            advance(); advance();
            while (!(peek() == '*' && peekNext() == '/') && !isAtEnd()) {
                if (peek() == '\n') { line++; lineStartOffset = current + 1; }
                advance();
            }
            if (!isAtEnd()) { advance(); advance(); } // استهلاك */
        } else {
            break;
        }
    }
}

void Lexer::scanString() {
    // start يشير إلى علامة الاقتباس الافتتاحية
    std::string value;
    while (peek() != '"' && !isAtEnd()) {
        char c = advance();
        if (c == '\\' && !isAtEnd()) {
            char esc = advance();
            switch (esc) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                default: value += esc; break;
            }
        } else {
            if (c == '\n') { line++; lineStartOffset = current; }
            value += c;
        }
    }
    if (isAtEnd()) {
        throw LexError("سلسلة نصية غير مغلقة (missing closing \")", line, columnOf(start));
    }
    advance(); // استهلاك علامة الاقتباس الختامية
    Token t;
    t.type = TokenType::String;
    t.lexeme = src.substr(start, current - start);
    t.strValue = value;
    t.line = line;
    t.column = columnOf(start);
    tokens.push_back(std::move(t));
}

void Lexer::scanNumber() {
    while (isdigit(static_cast<unsigned char>(peek()))) advance();
    if (peek() == '.' && isdigit(static_cast<unsigned char>(peekNext()))) {
        advance();
        while (isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    Token t;
    t.type = TokenType::Number;
    t.lexeme = src.substr(start, current - start);
    t.numValue = std::stod(t.lexeme);
    t.line = line;
    t.column = columnOf(start);
    tokens.push_back(std::move(t));
}

void Lexer::scanIdentifier() {
    while (isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    addToken(TokenType::Identifier);
}

void Lexer::scanToken() {
    char c = advance();
    switch (c) {
        case '@': addToken(TokenType::At); return;
        case '.': addToken(TokenType::Dot); return;
        case '/': addToken(TokenType::Slash); return;
        case '=': addToken(TokenType::Equals); return;
        case ';': addToken(TokenType::Semicolon); return;
        case ',': addToken(TokenType::Comma); return;
        case '(': addToken(TokenType::LParen); return;
        case ')': addToken(TokenType::RParen); return;
        case '+': addToken(TokenType::Plus); return;
        case '-': addToken(TokenType::Minus); return;
        case '*': addToken(TokenType::Star); return;
        case ':': addToken(TokenType::Colon); return;
        case '"': scanString(); return;
        default:
            if (isdigit(static_cast<unsigned char>(c))) { scanNumber(); return; }
            if (isalpha(static_cast<unsigned char>(c)) || c == '_') { scanIdentifier(); return; }
            throw LexError(std::string("رمز غير معروف: '") + c + "'", line, columnOf(start));
    }
}

std::vector<Token> Lexer::scanTokens() {
    tokens.clear();
    while (true) {
        skipWhitespaceAndComments();
        if (isAtEnd()) break;
        start = current;
        scanToken();
    }
    Token eof;
    eof.type = TokenType::EndOfFile;
    eof.line = line;
    eof.column = columnOf(current);
    tokens.push_back(eof);
    return tokens;
}

} // namespace pitok

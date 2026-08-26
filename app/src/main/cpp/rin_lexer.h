#pragma once
#include "rin_common.h"

namespace rin {

class Lexer {
public:
    explicit Lexer(std::string source, std::string filename = "<input>");
    std::vector<Token> scanTokens();

private:
    std::string src;
    std::string file;      // اسم الملف؛ يُستخدم في مواقع Diagnostics ويُسجَّل في SourceManager
    size_t start = 0;
    size_t current = 0;
    int line = 1;
    size_t lineStartOffset = 0; // إزاحة أول حرف في السطر الحالي؛ لحساب العمود (column) لأي موقع
    std::vector<Token> tokens;

    int columnOf(size_t offset) const { return static_cast<int>(offset - lineStartOffset) + 1; }

    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);
    void addToken(TokenType type);
    void addToken(TokenType type, const std::string& lexeme);
    void scanToken();
    void scanString();
    void scanNumber();
    void scanIdentifier();
};

} // namespace rin

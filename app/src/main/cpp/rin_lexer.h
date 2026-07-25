#pragma once
#include "rin_common.h"

namespace rin {

class Lexer {
public:
    explicit Lexer(std::string source);
    std::vector<Token> scanTokens();

private:
    std::string src;
    size_t start = 0;
    size_t current = 0;
    int line = 1;
    std::vector<Token> tokens;

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

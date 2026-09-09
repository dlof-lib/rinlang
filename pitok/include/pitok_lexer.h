#pragma once
// pitok_lexer.h — المُجزِّئ المعجمي الخاص بـ PITOK. لا يستدعي ولا يتضمن rin_lexer.*.

#include "pitok_token.h"
#include <vector>
#include <string>
#include <stdexcept>

namespace pitok {

// خطأ معجمي بموضع دقيق (سطر/عمود) — مستقل عن نظام Diagnostics الخاص بـ Rin.
struct LexError : std::runtime_error {
    int line;
    int column;
    LexError(std::string msg, int l, int c)
        : std::runtime_error(std::move(msg)), line(l), column(c) {}
};

class Lexer {
public:
    explicit Lexer(std::string source, std::string filename = "<input>");
    std::vector<Token> scanTokens();

private:
    std::string src;
    std::string file;
    size_t start = 0;
    size_t current = 0;
    int line = 1;
    size_t lineStartOffset = 0;
    std::vector<Token> tokens;

    int columnOf(size_t offset) const { return static_cast<int>(offset - lineStartOffset) + 1; }

    bool isAtEnd() const;
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);
    void addToken(TokenType type);
    void skipWhitespaceAndComments();
    void scanToken();
    void scanString();
    void scanNumber();
    void scanIdentifier();
};

} // namespace pitok

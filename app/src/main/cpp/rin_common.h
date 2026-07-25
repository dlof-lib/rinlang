#pragma once
// Rin Language - common token & value definitions
#include <string>
#include <vector>

namespace rin {

enum class TokenType {
    // literals
    NUMBER, STRING, IDENT,
    // core keywords
    LET, PRINT, IF, ELSE, WHILE, FUN, RETURN, TRUE, FALSE, NIL, AND, OR,
    // data-container language keywords (لغة الحاويات/البيانات)
    TEXT,            // text  -> إعلان قيمة نصية
    CONTAINER,       // container
    CONTAINERS,      // Containers (جزء من Containers.Group)
    GROUP,           // Group (جزء من Containers.Group)
    VOLUME,          // Volume
    SECTION,         // Section
    TRANSLATIONS,    // Translations (الكتلة)
    TRANSLATION,     // translation (السطر الواحد)
    LINK,            // link
    TYING,           // tying
    MERGE,           // merge
    INSTALLATION,    // installation
    SIMPLIFIED,      // simplified
    SAVE,            // save
    FILE_KW,         // file
    END,             // end (تُستخدم داخل .end/...)
    // single/double char tokens
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQUAL, EQUAL_EQUAL, BANG, BANG_EQUAL,
    LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
    LPAREN, RPAREN, LBRACE, RBRACE,
    LBRACKET, RBRACKET, // [ ]  (مصفوفات وفهرسة arr[i])
    COLON,              // :    (فواصل key:value في القواميس maps)
    COMMA, SEMICOLON,
    AT,              // @  (بداية كتلة container/Containers.Group/Volume)
    DOT,             // .  (تُستخدم في وسم الإغلاق .end/...)
    END_OF_FILE, ERROR
};

struct Token {
    TokenType type;
    std::string lexeme;
    double number = 0.0;
    int line = 0;
};

// Thrown by the lexer/parser/interpreter on any language error.
struct RinError {
    std::string message;
    int line;
    RinError(std::string msg, int ln) : message(std::move(msg)), line(ln) {}
};

} // namespace rin

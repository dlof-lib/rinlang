#pragma once
// pitok_token.h — تعريف التوكنات الخاصة بلغة PITOK (واجهات فقط).
// مستقل تمامًا عن rin_lexer.h / rin_common.h — لا يتضمن ولا يعتمد على أي رأس من Rin.
// الهدف: PITOK لغة DSL قائمة بذاتها لوصف الواجهات (@view / @loop / سمات key=value)،
// تُنتِج AST خاصًا بها يمكن لاحقًا تغذيته لمحرك التخطيط (Loomtime) دون المرور بمُحلِّل Rin العام.

#include <string>
#include <cstdint>

namespace pitok {

enum class TokenType {
    // رموز أحادية
    At,          // @
    Dot,         // .
    Slash,       // /
    Equals,      // =
    Semicolon,   // ;
    Comma,       // ,
    LParen,      // (
    RParen,      // )
    Plus,        // +
    Minus,       // -
    Star,        // *
    // ملاحظة: '/' يُمثَّل دائمًا بـ TokenType::Slash سواء استُخدم قسمةً حسابية أو كجزء من
    // وسم الإغلاق ".end/view" — الـ Parser هو من يفصل بين الحالتين حسب السياق (لا الـ Lexer)،
    // لأن الحالتين لا يمكن تمييزهما معجميًا بلا نظرة إلى ما قبل الرمز.
    Colon,       // : (محجوز لصياغات مستقبلية مثل ternary/تعليقات نوع)

    // حرفيات ومعرفات
    Identifier,
    Number,
    String,

    EndOfFile,
    Unknown
};

struct Token {
    TokenType type;
    std::string lexeme;   // النص الخام كما ورد في المصدر
    std::string strValue; // للـ String: القيمة بعد فك الهروب (escape)
    double numValue = 0.0; // للـ Number
    int line = 1;
    int column = 1;
};

inline const char* tokenTypeName(TokenType t) {
    switch (t) {
        case TokenType::At: return "At";
        case TokenType::Dot: return "Dot";
        case TokenType::Slash: return "Slash";
        case TokenType::Equals: return "Equals";
        case TokenType::Semicolon: return "Semicolon";
        case TokenType::Comma: return "Comma";
        case TokenType::LParen: return "LParen";
        case TokenType::RParen: return "RParen";
        case TokenType::Plus: return "Plus";
        case TokenType::Minus: return "Minus";
        case TokenType::Star: return "Star";
        case TokenType::Colon: return "Colon";
        case TokenType::Identifier: return "Identifier";
        case TokenType::Number: return "Number";
        case TokenType::String: return "String";
        case TokenType::EndOfFile: return "EndOfFile";
        default: return "Unknown";
    }
}

} // namespace pitok

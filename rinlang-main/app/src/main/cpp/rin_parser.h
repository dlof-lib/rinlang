#pragma once
#include "rin_common.h"
#include "rin_ast.h"

namespace rin {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);
    std::vector<StmtPtr> parse();

private:
    std::vector<Token> tokens;
    size_t current = 0;

    // helpers
    bool isAtEnd() const;
    const Token& peek() const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool checkNext(TokenType type) const; // ينظر إلى التوكن التالي (current+1) دون استهلاكه
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);

    // statements
    StmtPtr declaration();
    StmtPtr letDeclaration();
    StmtPtr functionDeclaration();
    StmtPtr statement();
    StmtPtr printStatement();
    StmtPtr ifStatement();
    StmtPtr whileStatement();
    StmtPtr returnStatement();
    std::shared_ptr<BlockStmt> block();
    StmtPtr expressionStatement();

    // data-container language (container / Containers.Group / Volume / Section / ...)
    std::string readTagKeyword();      // يقرأ "container" أو "Volume" أو "Containers.Group" ...
    std::string readOptionalName();    // يقرأ "=name" اختياريًا بعد كلمة مفتاحية
    bool checkClosingTag() const;      // هل التوكن الحالي بداية ".end/..."
    void consumeEndTag(const std::string& expectedTag); // يستهلك ويتحقق من ".end/<expectedTag>"

    StmtPtr textDeclaration();
    StmtPtr atBlock();                 // @container / @Containers.Group / @Volume
    StmtPtr sectionBlock();
    StmtPtr translationsBlock();
    StmtPtr translationStatement();
    StmtPtr linkStatement();
    StmtPtr tyingStatement();
    StmtPtr mergeStatement();
    StmtPtr installationStatement(bool simplifiedFlag);
    StmtPtr saveStatement(bool simplifiedFlag);
    StmtPtr fileStatement();
    StmtPtr simplifiedStatement(); // "simplified" متبوعة بـ installation أو save

    // expressions (precedence climbing)
    ExprPtr expression();
    ExprPtr assignment();
    ExprPtr pipeline();   // a |> f(b, c)  =>  f(a, b, c)  (خط الأنابيب: للتحويل/التجميع الإحصائي)
    ExprPtr logicOr();
    ExprPtr logicAnd();
    ExprPtr equality();
    ExprPtr comparison();
    ExprPtr term();
    ExprPtr factor();
    ExprPtr unary();
    ExprPtr call();
    ExprPtr primary();
};

} // namespace rin

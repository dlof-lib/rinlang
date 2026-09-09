#pragma once
// pitok_parser.h — المُحلِّل النحوي الخاص بـ PITOK.
// لا يعتمد على rin_parser.h/rin_ast.h إطلاقًا: نحو مستقل مصمَّم خصيصًا لصياغة الواجهات
// (@view / @loop / سمات / تعابير محدودة)، وليس نحو Rin العام الكامل (statements/functions/
// classes/إلخ). هذا هو جوهر "الفصل": PITOK لم يعد يستعير Parser لغة أخرى لتحليل واجهاته.

#include "pitok_token.h"
#include "pitok_ast.h"
#include <vector>
#include <string>
#include <stdexcept>

namespace pitok {

struct ParseError : std::runtime_error {
    int line;
    int column;
    ParseError(std::string msg, int l, int c)
        : std::runtime_error(std::move(msg)), line(l), column(c) {}
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, std::string filename = "<input>");

    // يحلّل ملف PITOK كاملاً إلى Program. يرمي ParseError عند أول خطأ (لا استرداد أخطاء
    // بعد — نفس القرار الذي اتخذه rin_parser أصلاً في مرحلته الأولى قبل إضافة
    // parseCollectingDiagnostics؛ يمكن إضافة استرداد مماثل لاحقًا بنفس الأسلوب).
    Program parseProgram();

private:
    std::vector<Token> tokens;
    size_t current = 0;
    std::string file;

    bool isAtEnd() const;
    const Token& peek() const;
    const Token& peekAt(size_t offset) const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool checkIdent(const std::string& text) const;
    bool match(TokenType type);
    const Token& consume(TokenType type, const std::string& message);
    const Token& consumeIdent(const std::string& expected);
    [[noreturn]] void error(const std::string& message) const;

    // نقطة توقّف: "." IDENT("end") "/" IDENT(tag) — تُستخدم بلا استهلاك (lookahead فقط)
    // للتفريق بين طفل @view جديد وإغلاق الأب الحالي.
    bool atEndTag(const std::string& tag) const;
    void consumeEndTag(const std::string& tag);

    WarpDecl parseWarp();
    // موحَّد لكل من @view.Kind=name، @loop=name، @element.Kind=name، @container=name —
    // "التوحيد" الفعلي: نحو واحد يقرر بنيته حسب tag بدل أربع دوال منفصلة متكررة.
    ViewNodePtr parseBlockByTag(const std::string& tag);
    void parseBody(ViewNodePtr node, const std::string& closingTag);
    Attribute parseAttribute();
    OnBinding parseOnBinding(); // on.target.event = expr;

    // تعابير: أولوية الجمع/الطرح فوق الضرب/القسمة فوق العناصر الأساسية (رقم/سلسلة/معرف/نداء).
    ExprPtr parseExpr();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parsePrimary();
    ExprPtr parseCallOrIdentifier();
};

} // namespace pitok

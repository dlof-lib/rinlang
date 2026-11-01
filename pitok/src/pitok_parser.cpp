// pitok_parser.cpp — تنفيذ المُحلِّل النحوي لـ PITOK.
//
// يوحِّد لهجتَي الواجهات الموجودتين فعليًا في المستودع تحت نحو واحد وAST واحد:
//   - الحديثة:  @view.Kind=name ... .end/view   و  @loop=name ... .end/loop
//   - الأقدم:   @element.Kind=name ... .end/element   و  @container=name ... .end/container
//               مع ربط أحداث: on.target.event=expr;
//
// النحو المدعوم فعليًا:
//
//   program      := (warpDecl | block)*
//   warpDecl     := "warp" IDENT "=" expr ";"
//   block        := "@" ("view" "." IDENT | "loop" | "element" "." IDENT | "container")
//                   "=" IDENT bodyItem* ".end/" tag
//   bodyItem     := block | attribute | onBinding
//   attribute    := IDENT "=" expr ";"
//   onBinding    := "on" "." IDENT "." IDENT "=" expr ";"
//   expr         := additive
//   additive     := multiplicative (("+"|"-") multiplicative)*
//   multiplicative := primary (("*"|"/") primary)*
//   primary      := NUMBER | STRING | call | IDENT | "(" expr ")"
//   call         := IDENT "(" (expr ("," expr)*)? ")"

#include "pitok_parser.h"

namespace pitok {

Parser::Parser(std::vector<Token> toks, std::string filename)
    : tokens(std::move(toks)), file(std::move(filename)) {}

bool Parser::isAtEnd() const { return peek().type == TokenType::EndOfFile; }
const Token& Parser::peek() const { return tokens[current]; }
const Token& Parser::peekAt(size_t offset) const {
    size_t idx = current + offset;
    if (idx >= tokens.size()) return tokens.back(); // EOF
    return tokens[idx];
}
const Token& Parser::previous() const { return tokens[current - 1]; }
const Token& Parser::advance() { if (!isAtEnd()) current++; return previous(); }
bool Parser::check(TokenType type) const { return !isAtEnd() && peek().type == type; }
bool Parser::checkIdent(const std::string& text) const {
    return check(TokenType::Identifier) && peek().lexeme == text;
}
bool Parser::match(TokenType type) {
    if (check(type)) { advance(); return true; }
    return false;
}
const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    error(message);
}
const Token& Parser::consumeIdent(const std::string& expected) {
    if (checkIdent(expected)) return advance();
    error("توقعت الكلمة '" + expected + "' لكن وجدت '" + peek().lexeme + "'");
}
void Parser::error(const std::string& message) const {
    throw ParseError(message, peek().line, peek().column);
}

bool Parser::atEndTag(const std::string& tag) const {
    // نمط: DOT IDENT("end") SLASH IDENT(tag) — بدون استهلاك أي توكن (lookahead بحت).
    return check(TokenType::Dot)
        && peekAt(1).type == TokenType::Identifier && peekAt(1).lexeme == "end"
        && peekAt(2).type == TokenType::Slash
        && peekAt(3).type == TokenType::Identifier && peekAt(3).lexeme == tag;
}
void Parser::consumeEndTag(const std::string& tag) {
    consume(TokenType::Dot, "توقعت '.' لبدء وسم الإغلاق .end/" + tag);
    consumeIdent("end");
    consume(TokenType::Slash, "توقعت '/' داخل وسم الإغلاق .end/" + tag);
    consumeIdent(tag);
}

// الوسوم الأربعة المدعومة على مستوى @tag — أي جذر و/أو طفل. قائمة واحدة يرجع إليها كل من
// parseProgram (الجذور) وparseBody (الأطفال) بدل تكرار نفس المقارنات في مكانين.
static bool isKnownBlockTag(const std::string& t) {
    return t == "view" || t == "loop" || t == "element" || t == "container";
}

Program Parser::parseProgram() {
    Program prog;
    while (!isAtEnd()) {
        if (checkIdent("warp")) {
            prog.warps.push_back(parseWarp());
        } else if (check(TokenType::At)) {
            if (peekAt(1).type == TokenType::Identifier && isKnownBlockTag(peekAt(1).lexeme)) {
                prog.roots.push_back(parseBlockByTag(peekAt(1).lexeme));
            } else {
                error("بعد '@' توقعت 'view' أو 'loop' أو 'element' أو 'container' لكن وجدت '"
                      + peekAt(1).lexeme + "'");
            }
        } else {
            error("توقعت 'warp' أو '@view'/'@loop'/'@element'/'@container' في بداية جملة PITOK، وجدت '"
                  + peek().lexeme + "'");
        }
    }
    return prog;
}

WarpDecl Parser::parseWarp() {
    WarpDecl w;
    w.line = peek().line;
    consumeIdent("warp");
    w.name = consume(TokenType::Identifier, "توقعت اسم متغيّر بعد warp").lexeme;
    consume(TokenType::Equals, "توقعت '=' بعد اسم متغيّر warp");
    w.initValue = parseExpr();
    consume(TokenType::Semicolon, "توقعت ';' بعد قيمة warp");
    return w;
}

ViewNodePtr Parser::parseBlockByTag(const std::string& tag) {
    auto node = std::make_shared<ViewNode>();
    node->line = peek().line;
    consume(TokenType::At, "توقعت '@'");
    consumeIdent(tag);

    std::string closingTag = tag;
    if (tag == "view" || tag == "element") {
        // @view.Kind=name  أو  @element.Kind=name
        consume(TokenType::Dot, "توقعت '.' بعد @" + tag + " (الصيغة: @" + tag + ".Kind=name)");
        node->kind = consume(TokenType::Identifier, "توقعت نوع العنصر بعد @" + tag + ".").lexeme;
        consume(TokenType::Equals, "توقعت '=' بعد @" + tag + ".Kind");
    } else {
        // @loop=name  أو  @container=name — لا نوع فرعي، فقط اسم مباشرة بعد "="
        node->kind = (tag == "loop") ? "__loop__" : "__container__";
        consume(TokenType::Equals, "توقعت '=' بعد @" + tag);
    }
    node->name = consume(TokenType::Identifier, "توقعت اسم بعد @" + tag + "=").lexeme;

    parseBody(node, closingTag);
    consumeEndTag(closingTag);
    return node;
}

void Parser::parseBody(ViewNodePtr node, const std::string& closingTag) {
    while (!atEndTag(closingTag)) {
        if (isAtEnd()) {
            error("وصلت نهاية الملف قبل إيجاد .end/" + closingTag);
        }
        if (check(TokenType::At)) {
            if (peekAt(1).type == TokenType::Identifier && isKnownBlockTag(peekAt(1).lexeme)) {
                node->children.push_back(parseBlockByTag(peekAt(1).lexeme));
            } else {
                error("بعد '@' داخل عنصر توقعت 'view' أو 'loop' أو 'element' أو 'container'");
            }
        } else if (checkIdent("on") && peekAt(1).type == TokenType::Dot) {
            node->bindings.push_back(parseOnBinding());
        } else {
            node->attributes.push_back(parseAttribute());
        }
    }
}

Attribute Parser::parseAttribute() {
    Attribute a;
    a.line = peek().line;
    a.key = consume(TokenType::Identifier, "توقعت اسم سمة (key)").lexeme;
    consume(TokenType::Equals, "توقعت '=' بعد اسم السمة '" + a.key + "'");
    a.value = parseExpr();
    consume(TokenType::Semicolon, "توقعت ';' بعد قيمة السمة '" + a.key + "'");
    return a;
}

OnBinding Parser::parseOnBinding() {
    // on.target.event = expr;  مثال حقيقي من المستودع: on.run.click=runCode();
    OnBinding b;
    b.line = peek().line;
    consumeIdent("on");
    consume(TokenType::Dot, "توقعت '.' بعد on");
    b.target = consume(TokenType::Identifier, "توقعت اسم عنصر مستهدف بعد on.").lexeme;
    consume(TokenType::Dot, "توقعت '.' بعد on." + b.target);
    b.event = consume(TokenType::Identifier, "توقعت اسم حدث بعد on." + b.target + ".").lexeme;
    consume(TokenType::Equals, "توقعت '=' بعد on." + b.target + "." + b.event);
    b.action = parseExpr();
    consume(TokenType::Semicolon, "توقعت ';' بعد ربط الحدث");
    return b;
}

ExprPtr Parser::parseExpr() { return parseAdditive(); }

ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        std::string op = advance().lexeme;
        ExprPtr right = parseMultiplicative();
        left = makeBinary(op, left, right, left->line);
    }
    return left;
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parsePrimary();
    while (check(TokenType::Star) || check(TokenType::Slash)) {
        std::string op = advance().lexeme;
        ExprPtr right = parsePrimary();
        left = makeBinary(op, left, right, left->line);
    }
    return left;
}

ExprPtr Parser::parsePrimary() {
    const Token& t = peek();
    if (check(TokenType::Number)) { advance(); return makeNumber(t.numValue, t.line); }
    if (check(TokenType::String)) { advance(); return makeString(t.strValue, t.line); }
    if (check(TokenType::LParen)) {
        advance();
        ExprPtr e = parseExpr();
        consume(TokenType::RParen, "توقعت ')' لإغلاق القوس");
        return e;
    }
    if (check(TokenType::Identifier)) return parseCallOrIdentifier();
    error("توقعت قيمة (رقم/سلسلة/معرّف/نداء دالة) لكن وجدت '" + t.lexeme + "'");
}

ExprPtr Parser::parseCallOrIdentifier() {
    const Token& nameTok = advance(); // Identifier
    if (check(TokenType::LParen)) {
        advance();
        std::vector<ExprPtr> args;
        if (!check(TokenType::RParen)) {
            args.push_back(parseExpr());
            while (match(TokenType::Comma)) args.push_back(parseExpr());
        }
        consume(TokenType::RParen, "توقعت ')' بعد وسائط الاستدعاء '" + nameTok.lexeme + "'");
        return makeCall(nameTok.lexeme, std::move(args), nameTok.line);
    }
    return makeIdentifier(nameTok.lexeme, nameTok.line);
}

} // namespace pitok

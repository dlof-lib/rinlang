#include "rin_parser.h"
#include <algorithm>

namespace rin {

Parser::Parser(std::vector<Token> toks) : tokens(std::move(toks)) {}

bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }
const Token& Parser::peek() const { return tokens[current]; }
const Token& Parser::previous() const { return tokens[current - 1]; }
const Token& Parser::advance() { if (!isAtEnd()) current++; return previous(); }
bool Parser::check(TokenType type) const { return !isAtEnd() && peek().type == type; }

bool Parser::checkNext(TokenType type) const {
    if (isAtEnd()) return false;
    if (current + 1 >= tokens.size()) return false;
    return tokens[current + 1].type == type;
}

bool Parser::match(std::initializer_list<TokenType> types) {
    for (auto t : types) {
        if (check(t)) { advance(); return true; }
    }
    return false;
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    throw RinError(message + " (got '" + peek().lexeme + "')", peek().line);
}

std::vector<StmtPtr> Parser::parse() {
    std::vector<StmtPtr> statements;
    while (!isAtEnd()) statements.push_back(declaration());
    return statements;
}

StmtPtr Parser::declaration() {
    if (match({TokenType::LET})) return letDeclaration();
    if (match({TokenType::FUN})) return functionDeclaration();

    // مفاهيم لغة الحاويات/البيانات
    if (match({TokenType::TEXT})) return textDeclaration();
    if (match({TokenType::AT})) return atBlock();
    if (match({TokenType::SECTION})) return sectionBlock();
    if (match({TokenType::TRANSLATIONS})) return translationsBlock();
    if (match({TokenType::TRANSLATION})) return translationStatement();
    if (match({TokenType::LINK})) return linkStatement();
    if (match({TokenType::TYING})) return tyingStatement();
    if (match({TokenType::MERGE})) return mergeStatement();
    if (match({TokenType::SIMPLIFIED})) return simplifiedStatement();
    if (match({TokenType::INSTALLATION})) return installationStatement(false);
    if (match({TokenType::SAVE})) return saveStatement(false);
    if (match({TokenType::FILE_KW})) return fileStatement();
    // 'route' كلمة سياقية غير محجوزة عالمياً: تُعامَل كعبارة route فقط عند ظهورها أول عبارة،
    // وإلا فهي مجرّد معرّف (IDENT) عادي كأي اسم متغير آخر.
    if (check(TokenType::IDENT) && peek().lexeme == "route") { advance(); return routeStatement(); }

    return statement();
}

StmtPtr Parser::letDeclaration() {
    auto name = consume(TokenType::IDENT, "Expected variable name after 'let'");
    ExprPtr initializer = nullptr;
    if (match({TokenType::EQUAL})) initializer = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after variable declaration");
    auto stmt = std::make_shared<LetStmt>();
    stmt->name = name.lexeme;
    stmt->initializer = initializer;
    stmt->line = name.line;
    return stmt;
}

StmtPtr Parser::functionDeclaration() {
    auto name = consume(TokenType::IDENT, "Expected function name after 'fun'");
    consume(TokenType::LPAREN, "Expected '(' after function name");
    std::vector<std::string> params;
    if (!check(TokenType::RPAREN)) {
        do {
            params.push_back(consume(TokenType::IDENT, "Expected parameter name").lexeme);
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RPAREN, "Expected ')' after parameters");
    consume(TokenType::LBRACE, "Expected '{' before function body");
    auto body = block();
    auto fn = std::make_shared<FunctionStmt>();
    fn->name = name.lexeme;
    fn->params = params;
    fn->body = body;
    fn->line = name.line;
    return fn;
}

StmtPtr Parser::statement() {
    if (match({TokenType::PRINT})) return printStatement();
    if (match({TokenType::IF})) return ifStatement();
    if (match({TokenType::WHILE})) return whileStatement();
    if (match({TokenType::RETURN})) return returnStatement();
    if (check(TokenType::LBRACE)) { advance(); return block(); }
    return expressionStatement();
}

StmtPtr Parser::printStatement() {
    auto expr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after print statement");
    auto stmt = std::make_shared<PrintStmt>();
    stmt->expr = expr;
    return stmt;
}

StmtPtr Parser::ifStatement() {
    consume(TokenType::LPAREN, "Expected '(' after 'if'");
    auto condition = expression();
    consume(TokenType::RPAREN, "Expected ')' after if condition");
    auto thenBranch = statement();
    StmtPtr elseBranch = nullptr;
    if (match({TokenType::ELSE})) elseBranch = statement();
    auto stmt = std::make_shared<IfStmt>();
    stmt->condition = condition;
    stmt->thenBranch = thenBranch;
    stmt->elseBranch = elseBranch;
    return stmt;
}

StmtPtr Parser::whileStatement() {
    consume(TokenType::LPAREN, "Expected '(' after 'while'");
    auto condition = expression();
    consume(TokenType::RPAREN, "Expected ')' after while condition");
    auto body = statement();
    auto stmt = std::make_shared<WhileStmt>();
    stmt->condition = condition;
    stmt->body = body;
    return stmt;
}

StmtPtr Parser::returnStatement() {
    ExprPtr value = nullptr;
    if (!check(TokenType::SEMICOLON)) value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after return value");
    auto stmt = std::make_shared<ReturnStmt>();
    stmt->value = value;
    return stmt;
}

std::shared_ptr<BlockStmt> Parser::block() {
    auto blk = std::make_shared<BlockStmt>();
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        blk->statements.push_back(declaration());
    }
    consume(TokenType::RBRACE, "Expected '}' after block");
    return blk;
}

StmtPtr Parser::expressionStatement() {
    auto expr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after expression");
    auto stmt = std::make_shared<ExpressionStmt>();
    stmt->expr = expr;
    return stmt;
}

// ============ لغة الحاويات/البيانات ============

std::string Parser::readTagKeyword() {
    Token first = advance();
    std::string tag = first.lexeme;
    if (first.type == TokenType::CONTAINERS) {
        consume(TokenType::DOT, "Expected '.' after 'Containers'");
        consume(TokenType::GROUP, "Expected 'Group' after 'Containers.'");
        tag = "Containers.Group";
    } else if (first.type == TokenType::CONTAINER && check(TokenType::DOT)) {
        // بعد 'container.' قد تأتي 'pipe' (كلمة محجوزة عالمياً تاريخياً) أو إحدى الكلمات السياقية
        // غير المحجوزة: data / api / import (تُقرأ كمعرّف IDENT عادي، ولا تتعارض مع متغيرات
        // المستخدم في أي مكان آخر من البرنامج، تماماً كما تُقرأ 'to'/'with' في link/tying/merge).
        bool nextIsPipe = checkNext(TokenType::PIPE_KW);
        bool nextIsContextualWord = false;
        if (!nextIsPipe && current + 1 < tokens.size() && tokens[current + 1].type == TokenType::IDENT) {
            const std::string& w = tokens[current + 1].lexeme;
            nextIsContextualWord = (w == "data" || w == "api" || w == "import");
        }
        // لا نستهلك '.' إلا إذا كانت متبوعة مباشرة بإحدى هذه الكلمات، وإلا فقد تكون في الحقيقة
        // بداية وسم إغلاق آخر مجاور مثل '.end/container' تلاه '.end/Containers.Group'
        if (nextIsPipe || nextIsContextualWord) {
            advance(); // consume '.'
            Token sub = advance(); // consume 'pipe' / 'data' / 'api' / 'import'
            tag = "container." + sub.lexeme;
        }
    }
    return tag;
}

std::string Parser::readOptionalName() {
    if (!match({TokenType::EQUAL})) return "";
    if (check(TokenType::IDENT) || check(TokenType::STRING)) return advance().lexeme;
    throw RinError("Expected a name after '='", peek().line);
}

bool Parser::checkClosingTag() const {
    if (current >= tokens.size()) return false;
    if (tokens[current].type != TokenType::DOT) return false;
    if (current + 1 >= tokens.size()) return false;
    return tokens[current + 1].type == TokenType::END;
}

void Parser::consumeEndTag(const std::string& expectedTag) {
    consume(TokenType::DOT, "Expected '.' to start a closing tag like '.end/" + expectedTag + "'");
    consume(TokenType::END, "Expected 'end' after '.' in closing tag");
    consume(TokenType::SLASH, "Expected '/' after 'end' in closing tag");
    Token before = peek();
    std::string closingTag = readTagKeyword();
    if (closingTag != expectedTag) {
        throw RinError("Closing tag '.end/" + closingTag + "' does not match the opening '" + expectedTag + "'",
                        before.line);
    }
}

StmtPtr Parser::textDeclaration() {
    Token tok = previous();
    Token name = consume(TokenType::IDENT, "Expected a name after 'text'");
    consume(TokenType::EQUAL, "Expected '=' after text name");
    ExprPtr init = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after text declaration");
    auto s = std::make_shared<TextStmt>();
    s->name = name.lexeme;
    s->initializer = init;
    s->line = tok.line;
    return s;
}

StmtPtr Parser::atBlock() {
    Token atTok = previous(); // '@'
    std::string tag = readTagKeyword();
    static const std::vector<std::string> validTags = {
        "container", "container.pipe", "container.data", "container.api", "container.import",
        "Containers.Group", "Volume"
    };
    if (std::find(validTags.begin(), validTags.end(), tag) == validTags.end()) {
        throw RinError("Unsupported block '@" + tag + "'; expected container, container.pipe, container.data, "
                        "container.api, container.import, Containers.Group, or Volume", atTok.line);
    }
    std::string name = readOptionalName();
    std::vector<StmtPtr> body;
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag(tag);

    if (tag == "container" || tag == "container.pipe" || tag == "container.data" ||
        tag == "container.api" || tag == "container.import") {
        if (tag == "container.data") validateDataContainerBody(body);
        auto s = std::make_shared<ContainerStmt>();
        s->name = name; s->body = body; s->line = atTok.line;
        if (tag == "container.pipe") s->kind = ContainerKind::PIPE;
        else if (tag == "container.data") s->kind = ContainerKind::DATA;
        else if (tag == "container.api") s->kind = ContainerKind::API;
        else if (tag == "container.import") s->kind = ContainerKind::IMPORT;
        else s->kind = ContainerKind::PLAIN;
        return s;
    }
    if (tag == "Containers.Group") {
        auto s = std::make_shared<ContainerGroupStmt>();
        s->name = name; s->body = body; s->line = atTok.line;
        return s;
    }
    auto s = std::make_shared<VolumeStmt>();
    s->name = name; s->body = body; s->line = atTok.line;
    return s;
}

// container.data يجب أن تبقى "بيانات نقية": بلا تعريف دوال وبلا حاويات/مجموعات/أحجام متداخلة،
// وبلا route (المخصصة لـ container.api فقط) — ما يضمن أن أي حاوية بيانات تبقى قابلة للتسلسل
// (serializable) بسهولة ولا تحمل منطقاً إجرائياً مخفياً بداخلها.
void Parser::validateDataContainerBody(const std::vector<StmtPtr>& body) {
    for (auto& st : body) {
        if (std::dynamic_pointer_cast<FunctionStmt>(st)) {
            throw RinError("container.data لا يسمح بتعريف دوال (fun) بداخله؛ استخدم container أو container.pipe لذلك", st->line);
        }
        if (std::dynamic_pointer_cast<ContainerStmt>(st) || std::dynamic_pointer_cast<ContainerGroupStmt>(st) ||
            std::dynamic_pointer_cast<VolumeStmt>(st)) {
            throw RinError("container.data لا يسمح بحاويات/مجموعات/أحجام متداخلة بداخله", st->line);
        }
        if (std::dynamic_pointer_cast<RouteStmt>(st)) {
            throw RinError("عبارة 'route' مخصصة لـ container.api فقط، ولا يمكن استخدامها داخل container.data", st->line);
        }
    }
}

StmtPtr Parser::sectionBlock() {
    Token secTok = previous();
    std::string name = readOptionalName();
    std::vector<StmtPtr> body;
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag("Section");
    auto s = std::make_shared<SectionStmt>();
    s->name = name; s->body = body; s->line = secTok.line;
    return s;
}

StmtPtr Parser::translationsBlock() {
    Token tTok = previous();
    std::vector<StmtPtr> body;
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag("Translations");
    auto s = std::make_shared<TranslationsStmt>();
    s->body = body; s->line = tTok.line;
    return s;
}

StmtPtr Parser::translationStatement() {
    Token tok = previous();
    Token langKey = consume(TokenType::IDENT, "Expected 'lang' after 'translation'");
    if (langKey.lexeme != "lang") throw RinError("Expected 'lang' attribute after 'translation'", langKey.line);
    consume(TokenType::EQUAL, "Expected '=' after 'lang'");
    Token langVal = consume(TokenType::STRING, "Expected a text value for 'lang'");
    consume(TokenType::TEXT, "Expected 'text' attribute after 'lang=\"...\"'");
    consume(TokenType::EQUAL, "Expected '=' after 'text'");
    Token textVal = consume(TokenType::STRING, "Expected a text value for 'text'");
    consume(TokenType::SEMICOLON, "Expected ';' after translation statement");
    auto s = std::make_shared<TranslationStmt>();
    s->lang = langVal.lexeme; s->text = textVal.lexeme; s->line = tok.line;
    return s;
}

StmtPtr Parser::linkStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'to' after 'link'");
    if (key.lexeme != "to") throw RinError("Expected 'to' attribute after 'link'", key.line);
    consume(TokenType::EQUAL, "Expected '=' after 'to'");
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw RinError("Expected a container name after 'to='", peek().line);
    std::string target = advance().lexeme;
    consume(TokenType::SEMICOLON, "Expected ';' after link statement");
    auto s = std::make_shared<LinkStmt>();
    s->target = target; s->line = tok.line;
    return s;
}

StmtPtr Parser::tyingStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'with' after 'tying'");
    if (key.lexeme != "with") throw RinError("Expected 'with' attribute after 'tying'", key.line);
    consume(TokenType::EQUAL, "Expected '=' after 'with'");
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw RinError("Expected a container name after 'with='", peek().line);
    std::string target = advance().lexeme;
    consume(TokenType::SEMICOLON, "Expected ';' after tying statement");
    auto s = std::make_shared<TyingStmt>();
    s->target = target; s->line = tok.line;
    return s;
}

StmtPtr Parser::mergeStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'with' after 'merge'");
    if (key.lexeme != "with") throw RinError("Expected 'with' attribute after 'merge'", key.line);
    consume(TokenType::EQUAL, "Expected '=' after 'with'");
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw RinError("Expected a container name after 'with='", peek().line);
    std::string target = advance().lexeme;
    consume(TokenType::SEMICOLON, "Expected ';' after merge statement");
    auto s = std::make_shared<MergeStmt>();
    s->target = target; s->line = tok.line;
    return s;
}

StmtPtr Parser::installationStatement(bool simplifiedFlag) {
    Token tok = previous();
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw RinError("Expected a name after 'installation'", peek().line);
    std::string target = advance().lexeme;
    consume(TokenType::SEMICOLON, "Expected ';' after installation statement");
    auto s = std::make_shared<InstallationStmt>();
    s->target = target; s->simplified = simplifiedFlag; s->line = tok.line;
    return s;
}

StmtPtr Parser::saveStatement(bool simplifiedFlag) {
    Token tok = previous();
    ExprPtr pathExpr = nullptr;
    if (check(TokenType::IDENT) && peek().lexeme == "path") {
        advance();
        consume(TokenType::EQUAL, "Expected '=' after 'path'");
        pathExpr = expression();
    }
    consume(TokenType::SEMICOLON, "Expected ';' after save statement");
    auto s = std::make_shared<SaveStmt>();
    s->path = pathExpr; s->simplified = simplifiedFlag; s->line = tok.line;
    return s;
}

StmtPtr Parser::fileStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'path' after 'file'");
    if (key.lexeme != "path") throw RinError("Expected 'path' attribute after 'file'", key.line);
    consume(TokenType::EQUAL, "Expected '=' after 'path'");
    ExprPtr pathExpr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after file statement");
    auto s = std::make_shared<FileStmt>();
    s->path = pathExpr; s->line = tok.line;
    return s;
}

StmtPtr Parser::routeStatement() {
    Token tok = previous();
    auto attr = [&](const std::string& expected) -> ExprPtr {
        Token key = consume(TokenType::IDENT, "Expected '" + expected + "' attribute in route statement");
        if (key.lexeme != expected) {
            throw RinError("Expected '" + expected + "' attribute in route statement", key.line);
        }
        consume(TokenType::EQUAL, "Expected '=' after '" + expected + "'");
        return expression();
    };
    ExprPtr method = attr("method");
    ExprPtr path = attr("path");
    ExprPtr status = attr("status");
    ExprPtr body = attr("body");
    consume(TokenType::SEMICOLON, "Expected ';' after route statement");
    auto s = std::make_shared<RouteStmt>();
    s->method = method; s->path = path; s->status = status; s->body = body; s->line = tok.line;
    return s;
}

StmtPtr Parser::simplifiedStatement() {
    Token tok = previous();
    if (match({TokenType::INSTALLATION})) return installationStatement(true);
    if (match({TokenType::SAVE})) return saveStatement(true);
    throw RinError("'simplified' must be followed by 'installation' or 'save'", tok.line);
}

// ============ التعبيرات (expressions) ============

ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
    auto expr = pipeline();
    if (match({TokenType::EQUAL})) {
        Token eq = previous();
        auto value = assignment();
        if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
            auto assign = std::make_shared<AssignExpr>();
            assign->name = var->name;
            assign->value = value;
            assign->line = eq.line;
            return assign;
        }
        if (auto idx = std::dynamic_pointer_cast<IndexExpr>(expr)) {
            auto set = std::make_shared<IndexSetExpr>();
            set->object = idx->object;
            set->index = idx->index;
            set->value = value;
            set->line = eq.line;
            return set;
        }
        throw RinError("Invalid assignment target", eq.line);
    }
    return expr;
}

// مُشغّل الأنابيب |> : يسمح ببناء خطوط أنابيب بيانات/إحصاء بشكل قابل للقراءة
//   input |> step1() |> step2(arg)   يُكافئ   step2(step1(input), arg)
// القيمة الموجودة على يسار |> تُمرَّر دائماً كأول وسيط للنداء الموجود على اليمين.
// يمكن كتابة اسم دالة بدون أقواس على يمين |> فتُعامل كنداء بلا وسائط إضافية: data |> mean
ExprPtr Parser::pipeline() {
    auto expr = logicOr();
    while (match({TokenType::PIPE})) {
        Token opTok = previous();
        auto rhs = logicOr();

        std::shared_ptr<CallExpr> callExpr = std::dynamic_pointer_cast<CallExpr>(rhs);
        if (!callExpr) {
            if (auto var = std::dynamic_pointer_cast<VariableExpr>(rhs)) {
                callExpr = std::make_shared<CallExpr>();
                callExpr->callee = var->name;
                callExpr->line = var->line;
            } else {
                throw RinError("Expected a function call (e.g. 'step()') after '|>'", opTok.line);
            }
        }

        auto piped = std::make_shared<CallExpr>();
        piped->callee = callExpr->callee;
        piped->line = opTok.line;
        piped->args.push_back(expr); // القيمة القادمة من يسار |> تصبح أول وسيط
        for (auto& a : callExpr->args) piped->args.push_back(a);
        expr = piped;
    }
    return expr;
}

ExprPtr Parser::logicOr() {
    auto expr = logicAnd();
    while (match({TokenType::OR})) {
        auto op = previous().type;
        auto right = logicAnd();
        auto l = std::make_shared<LogicalExpr>();
        l->left = expr; l->op = op; l->right = right;
        expr = l;
    }
    return expr;
}

ExprPtr Parser::logicAnd() {
    auto expr = equality();
    while (match({TokenType::AND})) {
        auto op = previous().type;
        auto right = equality();
        auto l = std::make_shared<LogicalExpr>();
        l->left = expr; l->op = op; l->right = right;
        expr = l;
    }
    return expr;
}

ExprPtr Parser::equality() {
    auto expr = comparison();
    while (match({TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL})) {
        auto op = previous().type;
        auto right = comparison();
        auto b = std::make_shared<BinaryExpr>();
        b->left = expr; b->op = op; b->right = right;
        expr = b;
    }
    return expr;
}

ExprPtr Parser::comparison() {
    auto expr = term();
    while (match({TokenType::LESS, TokenType::LESS_EQUAL, TokenType::GREATER, TokenType::GREATER_EQUAL})) {
        auto op = previous().type;
        auto right = term();
        auto b = std::make_shared<BinaryExpr>();
        b->left = expr; b->op = op; b->right = right;
        expr = b;
    }
    return expr;
}

ExprPtr Parser::term() {
    auto expr = factor();
    while (match({TokenType::PLUS, TokenType::MINUS})) {
        auto op = previous().type;
        auto right = factor();
        auto b = std::make_shared<BinaryExpr>();
        b->left = expr; b->op = op; b->right = right;
        expr = b;
    }
    return expr;
}

ExprPtr Parser::factor() {
    auto expr = unary();
    while (match({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
        auto op = previous().type;
        auto right = unary();
        auto b = std::make_shared<BinaryExpr>();
        b->left = expr; b->op = op; b->right = right;
        expr = b;
    }
    return expr;
}

ExprPtr Parser::unary() {
    if (match({TokenType::BANG, TokenType::MINUS})) {
        auto op = previous().type;
        auto right = unary();
        auto u = std::make_shared<UnaryExpr>();
        u->op = op; u->right = right;
        return u;
    }
    return call();
}

ExprPtr Parser::call() {
    auto expr = primary();
    for (;;) {
        if (match({TokenType::LPAREN})) {
            auto var = std::dynamic_pointer_cast<VariableExpr>(expr);
            if (!var) throw RinError("Only functions can be called", previous().line);
            auto c = std::make_shared<CallExpr>();
            c->callee = var->name;
            c->line = previous().line;
            if (!check(TokenType::RPAREN)) {
                do {
                    c->args.push_back(expression());
                } while (match({TokenType::COMMA}));
            }
            consume(TokenType::RPAREN, "Expected ')' after arguments");
            expr = c;
        } else if (match({TokenType::LBRACKET})) {
            Token bracket = previous();
            auto index = expression();
            consume(TokenType::RBRACKET, "Expected ']' after index");
            auto ie = std::make_shared<IndexExpr>();
            ie->object = expr;
            ie->index = index;
            ie->line = bracket.line;
            expr = ie;
        } else {
            break;
        }
    }
    return expr;
}

ExprPtr Parser::primary() {
    if (match({TokenType::FALSE})) {
        auto e = std::make_shared<LiteralExpr>();
        e->kind = LiteralExpr::Kind::BOOL; e->boolean = false; return e;
    }
    if (match({TokenType::TRUE})) {
        auto e = std::make_shared<LiteralExpr>();
        e->kind = LiteralExpr::Kind::BOOL; e->boolean = true; return e;
    }
    if (match({TokenType::NIL})) {
        auto e = std::make_shared<LiteralExpr>();
        e->kind = LiteralExpr::Kind::NIL; return e;
    }
    if (match({TokenType::NUMBER})) {
        auto e = std::make_shared<LiteralExpr>();
        e->kind = LiteralExpr::Kind::NUMBER; e->number = previous().number; return e;
    }
    if (match({TokenType::STRING})) {
        auto e = std::make_shared<LiteralExpr>();
        e->kind = LiteralExpr::Kind::STRING; e->str = previous().lexeme; return e;
    }
    if (match({TokenType::IDENT})) {
        auto e = std::make_shared<VariableExpr>();
        e->name = previous().lexeme;
        e->line = previous().line;
        return e;
    }
    if (match({TokenType::LPAREN})) {
        auto expr = expression();
        consume(TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }
    if (match({TokenType::LBRACKET})) {
        Token start = previous();
        auto arr = std::make_shared<ArrayExpr>();
        arr->line = start.line;
        if (!check(TokenType::RBRACKET)) {
            do {
                arr->elements.push_back(expression());
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RBRACKET, "Expected ']' after array elements");
        return arr;
    }
    if (match({TokenType::LBRACE})) {
        Token start = previous();
        auto map = std::make_shared<MapExpr>();
        map->line = start.line;
        if (!check(TokenType::RBRACE)) {
            do {
                if (check(TokenType::RBRACE)) break; // allow trailing comma
                ExprPtr keyExpr;
                if (check(TokenType::STRING)) {
                    auto lit = std::make_shared<LiteralExpr>();
                    lit->kind = LiteralExpr::Kind::STRING;
                    lit->str = advance().lexeme;
                    keyExpr = lit;
                } else if (check(TokenType::IDENT)) {
                    auto lit = std::make_shared<LiteralExpr>();
                    lit->kind = LiteralExpr::Kind::STRING;
                    lit->str = advance().lexeme;
                    keyExpr = lit;
                } else {
                    throw RinError("Expected a key (name or string) in map literal", peek().line);
                }
                consume(TokenType::COLON, "Expected ':' after map key");
                auto valueExpr = expression();
                map->entries.push_back({keyExpr, valueExpr});
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RBRACE, "Expected '}' after map entries");
        return map;
    }
    throw RinError("Expected expression", peek().line);
}

} // namespace rin

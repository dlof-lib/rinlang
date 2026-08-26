#include "rin_parser.h"
#include "diagnostics/diagnostic_engine.h"
#include "diagnostics/source_manager.h"
#include <algorithm>
#include <unordered_set>

namespace rin {

Parser::Parser(std::vector<Token> toks, std::string filename) : tokens(std::move(toks)), file(std::move(filename)) {}

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

diag::SourceLocation Parser::locOf(const Token& tok) const {
    int col = tok.col > 0 ? tok.col : 1;
    int endCol = tok.endCol > col ? tok.endCol
                 : col + std::max<int>(1, static_cast<int>(tok.lexeme.size()));
    return diag::SourceLocation(file, tok.line, col, tok.line, endCol);
}

RinError Parser::err(diag::Code code, const Token& tok, std::string message) const {
    diag::Diagnostic d(code, message, locOf(tok));
    return RinError(std::move(d));
}

RinError Parser::errAtLine(diag::Code code, int line, std::string message) const {
    diag::Diagnostic d(code, message, diag::SourceLocation::point(file, line, 1));
    return RinError(std::move(d));
}

// يستخرج النص بين أول علامتي اقتباس مفردتين من رسائل مثل "Expected ')' after ..." -> ")"
// لملء الحقل expected: تلقائياً في consume() دون حاجة لإعادة صياغة كل رسالة قديمة يدوياً.
std::string Parser::extractQuoted(const std::string& message) {
    auto first = message.find('\'');
    if (first == std::string::npos) return "";
    auto second = message.find('\'', first + 1);
    if (second == std::string::npos) return "";
    return message.substr(first + 1, second - first - 1);
}

const Token& Parser::consume(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    const Token& bad = peek();
    diag::Diagnostic d(diag::Code::E0012_MissingToken, message, locOf(bad));
    std::string quoted = extractQuoted(message);
    if (!quoted.empty()) d.expected = "`" + quoted + "`";
    d.found = (bad.type == TokenType::END_OF_FILE) ? "end of file" : ("`" + bad.lexeme + "`");
    d.withReason("the parser reached this point while still expecting the token above");
    // ملاحظة: حتى في وضع parseCollectingDiagnostics() نرمي دائماً هنا (بدل محاولة "التزييف" بإرجاع
    // bad كأنه استُهلك بنجاح) — الاسترداد (recovery) يحدث على مستوى العبارة كاملة في
    // parseCollectingDiagnostics() نفسها عبر synchronize()، ما يتجنّب تسلسل أخطاء وهمية لاحقة
    // ناتجة عن استخدام توكن لم يُستهلك فعلياً كأنه صحيح.
    throw RinError(std::move(d));
}

// نقاط التزامن: ';' / '}' / ')' / بداية '.end/...' — بعدها يُستأنف تحليل عبارات جديدة بأمان.
// يضمن التقدّم للأمام دائماً (على الأقل توكن واحد يُستهلَك) حتى لو كان checkClosingTag() صحيحاً
// فوراً (مثل وسم '.end/...' يتيماً بلا '@' مطابق في المستوى الأعلى) — بدون هذا الضمان يستدعي
// المستوى الأعلى declaration() على نفس التوكنات يتيمة إلى ما لا نهاية (حلقة لا تتقدّم أبداً).
void Parser::synchronize() {
    if (!isAtEnd()) advance();
    while (!isAtEnd()) {
        if (previous().type == TokenType::SEMICOLON) return;
        if (check(TokenType::RBRACE) || check(TokenType::RPAREN)) { advance(); return; }
        if (checkClosingTag()) return;
        advance();
    }
}

std::vector<StmtPtr> Parser::parseCollectingDiagnostics(diag::DiagnosticEngine& engine) {
    std::vector<StmtPtr> statements;
    while (!isAtEnd()) {
        try {
            statements.push_back(declaration());
        } catch (RinError& e) {
            if (e.diagnostic) engine.emit(*e.diagnostic);
            else engine.emit(diag::Diagnostic(diag::Code::E0010_ParserError, e.message,
                                               diag::SourceLocation::point(file, e.line, 1)));
            synchronize();
        }
    }
    return statements;
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
    if (check(TokenType::DOT) && checkNext(TokenType::IDENT) && current + 2 < tokens.size() && tokens[current + 1].lexeme == "object") return objectFieldStatement();
    // '@import "..."' هو عبارة مستقلة وأبسط من كتل '@container...': نتحقق من الشكل قبل تفويض
    // الأمر لـ atBlock() (الذي يتعامل حصراً مع container/Containers.Group/Volume). 'import' هنا
    // كلمة سياقية غير محجوزة (تُقرأ IDENT عادي)، فنميّزها بالنظر خطوتين للأمام: '@' ثم IDENT("import").
    if (check(TokenType::AT) && checkNext(TokenType::IDENT) &&
        current + 1 < tokens.size() && tokens[current + 1].lexeme == "import") {
        advance(); // '@'
        advance(); // 'import'
        return importStatement();
    }
    // '@view.<Kind>=name ... .end/view' -> Loomtime rendering engine (امتداد إضافي، نفس أسلوب
    // '@import' أعلاه بالضبط: نتحقق من الشكل قبل تفويض الأمر لـ atBlock() العام).
    if (check(TokenType::AT) && checkNext(TokenType::IDENT) &&
        current + 1 < tokens.size() && tokens[current + 1].lexeme == "view") {
        advance(); // '@'
        advance(); // 'view'
        return viewDeclaration();
    }
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
    // 'row' / 'style' كلمتان سياقيتان غير محجوزتان أيضاً (مفهوم الجدول: container.table / table)،
    // بنفس أسلوب 'route' أعلاه: لا تتحوّلان إلى عبارة خاصة إلا عند ظهورهما أول عبارة.
    if (check(TokenType::IDENT) && peek().lexeme == "row") { advance(); return rowStatement(); }
    if (check(TokenType::IDENT) && peek().lexeme == "style") { advance(); return styleStatement(); }
    // 'document' كلمة سياقية غير محجوزة أيضاً (مفهوم قاعدة البيانات اللاعلاقية: container.doc / doc)
    if (check(TokenType::IDENT) && peek().lexeme == "document") { advance(); return documentStatement(); }
    // 'warp' كلمة سياقية غير محجوزة أيضاً (Loomtime: خلية حالة تفاعلية يستخدمها محرّك العرض)
    if (check(TokenType::IDENT) && peek().lexeme == "warp") { advance(); return warpDeclaration(); }
    // 'plus.condition' كلمة مفتاحية مركّبة سياقية (شرط ثلاثي عام: plus.condition(cond) {..} / {..}).
    // تُفحَص هنا (declaration()) وليس في statement() حتى تعمل بنفس المستوى فوق أي إعلان حاوية
    // (@container...) بداخل كتلتيها، تماماً كأسلوب فحص '@import'/'@view' أعلاه: ننظر 3 خطوات
    // للأمام (IDENT("plus") ثم DOT ثم IDENT("condition")) قبل الاستهلاك.
    if (check(TokenType::IDENT) && peek().lexeme == "plus" &&
        checkNext(TokenType::DOT) &&
        current + 2 < tokens.size() && tokens[current + 2].type == TokenType::IDENT &&
        tokens[current + 2].lexeme == "condition") {
        advance(); // 'plus'
        advance(); // '.'
        advance(); // 'condition'
        return plusConditionStatement();
    }

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
    // جسم الدالة يبدأ سياق "حلقة" جديداً من الصفر: break/continue داخل دالة معرَّفة نصياً داخل
    // حلقة while خارجية لا يجب أن تُعتبر صالحة إلا إذا كانت هناك حلقة while أخرى داخل الدالة نفسها.
    int savedLoopDepth = loopDepth;
    loopDepth = 0;
    auto body = block();
    loopDepth = savedLoopDepth;
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
    if (match({TokenType::FOR})) return forStatement();
    if (match({TokenType::RETURN})) return returnStatement();
    if (match({TokenType::BREAK})) return breakStatement();
    if (match({TokenType::CONTINUE})) return continueStatement();
    if (match({TokenType::RINOPEN})) return rinopenStatement();
    if (check(TokenType::DOT) && checkNext(TokenType::IDENT) && current + 2 < tokens.size() && tokens[current + 1].lexeme == "object") return objectFieldStatement();
    if (check(TokenType::LBRACE)) { advance(); return block(); }
    return expressionStatement();
}

// print expr1, expr2, ... [sep=expr] [end=expr] [if=expr] [level=expr] [label=expr]
//       [repeat=expr] [pretty=expr] [upper=expr] [lower=expr] [width=expr] [align=expr];
// كل السمات اختيارية، بأي ترتيب بينها، كل واحدة مرة واحدة على الأكثر (تكرار نفس السمة خطأ صريح).
// ملاحظتان عن نوع التوكن:
//   - 'end' و'if' كلمتان محجوزتان في اللغة (TokenType::END / TokenType::IF)، فتحقّقهما هنا يكون
//     عبر نوع التوكن مباشرة، وليس عبر IDENT كبقية السمات (سلوك موروث من end، طُبِّق أيضاً على if).
//   - بقية السمات (sep/level/label/repeat/pretty/upper/lower/width/align) أسماء عادية غير محجوزة،
//     تُقرأ كـ IDENT ويُطابَق نصّها ضمن printAttrs أدناه، تماماً كسمات key=value الأخرى في اللغة.
StmtPtr Parser::rinopenStatement() {
    Token tok = previous();
    consume(TokenType::LPAREN, "Expected '(' after 'rinopen'");
    ExprPtr condition = expression();
    consume(TokenType::RPAREN, "Expected ')' after rinopen condition");
    consume(TokenType::LBRACE, "Expected '{' before rinopen body");
    loopDepth++;
    auto body = block();
    loopDepth--;
    auto st = std::make_shared<WhileStmt>();
    st->condition = condition;
    st->body = body;
    st->line = tok.line;
    return st;
}

StmtPtr Parser::objectFieldStatement() {
    consume(TokenType::DOT, "Expected '.' before '.object'");
    Token object = consume(TokenType::IDENT, "Expected 'object' after '.'");
    if (object.lexeme != "object") throw err(diag::Code::E0016_InvalidProperty, object, "expected `.object=type` inside `@container.open/object`");
    consume(TokenType::EQUAL, "Expected '=' after '.object'");
    if (match({TokenType::TEXT})) return textDeclaration();
    if (check(TokenType::DOT) && checkNext(TokenType::IDENT) && current + 2 < tokens.size() && tokens[current + 1].lexeme == "object") return objectFieldStatement();
    if (match({TokenType::LET})) return letDeclaration();
    if (match({TokenType::FUN})) return functionDeclaration();
    throw err(diag::Code::E0013_InvalidExpression, peek(),
              "expected an object member type after `.object=` (`text`, `let`, or `fun`)");
}

StmtPtr Parser::printStatement() {
    Token tok = previous(); // 'print'
    auto stmt = std::make_shared<PrintStmt>();
    // ملاحظة مهمة: القيم المفصولة بفواصل تُقرأ عبر pipeline() (مستوى أسفل assignment() مباشرة في
    // ترتيب الأسبقية)، وليس عبر expression() الكاملة. السبب: expression() تسمح بتعبير إسناد كامل
    // (IDENT = value)، وبما أن كل سمات print (sep=/end=/level=/... إلخ) هي بالضبط بصيغة
    // "IDENT = value"، فلو استُخدمت expression() هنا لَقرأ الحلقة "sep=\"-\"" كتعبير إسناد كامل
    // ضمن قائمة القيم بدل تركه للحلقة اللاحقة التي تتعرّف على سمات print — فتُبتلع السمة بأكملها
    // كقيمة يُحاول تنفيذها وقت التشغيل (وتفشل: "Undefined variable 'sep'"), ولا تصل أبداً لتُفعَّل
    // كسمة فعلية. إسناد صريح كقيمة print ما زال ممكناً عبر أقواس: print (x = 5);
    stmt->exprs.push_back(pipeline());
    while (match({TokenType::COMMA})) {
        stmt->exprs.push_back(pipeline());
    }
    static const std::unordered_set<std::string> printAttrs = {
        "sep", "level", "label", "repeat", "pretty", "upper", "lower", "width", "align"
    };
    std::unordered_set<std::string> seen;
    for (;;) {
        std::string attr;
        if (check(TokenType::END)) {
            attr = "end";
        } else if (check(TokenType::IF)) {
            attr = "if";
        } else if (check(TokenType::IDENT) && printAttrs.count(peek().lexeme)) {
            attr = peek().lexeme;
        } else {
            break;
        }
        advance(); // استهلاك توكن اسم السمة
        if (seen.count(attr)) {
            auto d = err(diag::Code::E0016_InvalidProperty, tok, "'print': `" + attr + "` attribute repeated");
            throw d;
        }
        seen.insert(attr);
        consume(TokenType::EQUAL, "Expected '=' after '" + attr + "' in print statement");
        ExprPtr value = expression();
        if (attr == "sep") stmt->sep = value;
        else if (attr == "end") stmt->end = value;
        else if (attr == "if") stmt->ifCond = value;
        else if (attr == "level") stmt->level = value;
        else if (attr == "label") stmt->label = value;
        else if (attr == "repeat") stmt->repeatN = value;
        else if (attr == "pretty") stmt->pretty = value;
        else if (attr == "upper") stmt->upper = value;
        else if (attr == "lower") stmt->lower = value;
        else if (attr == "width") stmt->width = value;
        else if (attr == "align") stmt->align = value;
    }
    consume(TokenType::SEMICOLON, "Expected ';' after print statement");
    stmt->line = tok.line;
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
    loopDepth++;
    auto body = statement();
    loopDepth--;
    auto stmt = std::make_shared<WhileStmt>();
    stmt->condition = condition;
    stmt->body = body;
    return stmt;
}

// for (initializer; condition; increment) body
// الأجزاء الثلاثة اختيارية تماماً كما في C:
//   - initializer: 'let x = ...;' أو 'x = ...;' (عبارة تعبير) أو فارغة (';' فقط)
//   - condition: تعبير منطقي؛ إن غاب يُعتبر true دائماً
//   - increment: تعبير عادي (يُنفَّذ بعد كل تكرار، حتى بعد continue)؛ اختياري
// desugaring داخلياً: نبني ForStmt مستقلة (بدل تحويلها فوراً إلى WhileStmt) حتى يبقى initializer
// محصوراً ضمن بيئة (Environment) خاصة بالحلقة كلها، تماماً كسلوك for القياسي في اللغات المشابهة.
StmtPtr Parser::forStatement() {
    Token forTok = previous(); // 'for'
    consume(TokenType::LPAREN, "Expected '(' after 'for'");

    StmtPtr initializer = nullptr;
    if (match({TokenType::SEMICOLON})) {
        initializer = nullptr; // for (;;) -> لا مُهيّئ
    } else if (match({TokenType::LET})) {
        initializer = letDeclaration(); // letDeclaration() يستهلك ';' بنفسه
    } else {
        initializer = expressionStatement(); // يستهلك ';' بنفسه أيضاً
    }

    ExprPtr condition = nullptr;
    if (!check(TokenType::SEMICOLON)) condition = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after 'for' loop condition");

    ExprPtr increment = nullptr;
    if (!check(TokenType::RPAREN)) increment = expression();
    consume(TokenType::RPAREN, "Expected ')' after 'for' clauses");

    loopDepth++;
    auto body = statement();
    loopDepth--;

    auto stmt = std::make_shared<ForStmt>();
    stmt->initializer = initializer;
    stmt->condition = condition;
    stmt->increment = increment;
    stmt->body = body;
    stmt->line = forTok.line;
    return stmt;
}

// plus.condition (condition) { trueBranch } / { falseBranch }
// كلتا الكتلتين إلزاميتان (بخلاف else الاختيارية في if) — إن غابت إحداهما فهو خطأ نحوي صريح.
// يُستدعى بعد أن يكون declaration() قد استهلك بالفعل 'plus' '.' 'condition'.
StmtPtr Parser::plusConditionStatement() {
    Token tok = previous(); // آخر توكن مُستهلَك ('condition')
    consume(TokenType::LPAREN, "Expected '(' after 'plus.condition'");
    auto condition = expression();
    consume(TokenType::RPAREN, "Expected ')' after 'plus.condition' condition");

    consume(TokenType::LBRACE, "Expected '{' to start 'plus.condition' true-branch");
    auto trueBranch = block(); // يستهلك '}' المطابقة بنفسه

    consume(TokenType::SLASH, "Expected '/' between 'plus.condition' true-branch and false-branch");

    consume(TokenType::LBRACE, "Expected '{' to start 'plus.condition' false-branch");
    auto falseBranch = block();

    auto stmt = std::make_shared<PlusConditionStmt>();
    stmt->condition = condition;
    stmt->trueBranch = trueBranch;
    stmt->falseBranch = falseBranch;
    stmt->line = tok.line;
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

// break; -> يجب أن تظهر فقط داخل جسم حلقة while (مباشرة أو متداخلة عبر if/block)؛
// وإلا فهي خطأ وقت التحليل (رسالة واضحة بدل فشل صامت وقت التنفيذ).
StmtPtr Parser::breakStatement() {
    Token tok = previous();
    if (loopDepth == 0) throw err(diag::Code::E0011_UnexpectedToken, tok, "'break' used outside of a loop");
    consume(TokenType::SEMICOLON, "Expected ';' after 'break'");
    auto stmt = std::make_shared<BreakStmt>();
    stmt->line = tok.line;
    return stmt;
}

// continue; -> نفس قيد break: صالحة فقط داخل جسم حلقة while.
StmtPtr Parser::continueStatement() {
    Token tok = previous();
    if (loopDepth == 0) throw err(diag::Code::E0011_UnexpectedToken, tok, "'continue' used outside of a loop");
    consume(TokenType::SEMICOLON, "Expected ';' after 'continue'");
    auto stmt = std::make_shared<ContinueStmt>();
    stmt->line = tok.line;
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
            nextIsContextualWord = (w == "data" || w == "api" || w == "import" || w == "table" || w == "doc" ||
                                     w == "object" || w == "portal" || w == "block" || w == "aukt" || w == "open");
        }
        // لا نستهلك '.' إلا إذا كانت متبوعة مباشرة بإحدى هذه الكلمات، وإلا فقد تكون في الحقيقة
        // بداية وسم إغلاق آخر مجاور مثل '.end/container' تلاه '.end/Containers.Group'
        if (nextIsPipe || nextIsContextualWord) {
            advance(); // consume '.'
            Token sub = advance(); // consume 'pipe' / 'data' / 'api' / 'import'
            tag = "container." + sub.lexeme;
            if (sub.lexeme == "open" && match({TokenType::SLASH})) {
                Token kind = consume(TokenType::IDENT, "Expected 'object' after 'container.open/'");
                if (kind.lexeme != "object") throw err(diag::Code::E0016_InvalidProperty, kind, "expected 'object' after 'container.open/'");
                tag = "container.open/object";
            }
        }
    }
    return tag;
}

std::string Parser::readOptionalName() {
    if (!match({TokenType::EQUAL})) return "";
    if (check(TokenType::IDENT) || check(TokenType::STRING)) return advance().lexeme;
    throw err(diag::Code::E0012_MissingToken, peek(), "expected a name after '='");
}

bool Parser::checkClosingTag() const {
    if (current >= tokens.size()) return false;
    if (tokens[current].type != TokenType::DOT) return false;
    if (current + 1 >= tokens.size()) return false;
    return tokens[current + 1].type == TokenType::END;
}

void Parser::consumeEndTag(const std::string& expectedTag, int openLine, const std::string& openName) {
    // كل رسائل الخطأ أدناه تذكر صراحةً السطر الذي فُتحت فيه الكتلة (openLine)، حتى في ملف كبير
    // بتعشيش عميق يعرف المبرمج فوراً أي '@' بالضبط لم يُغلق أو أُغلق بوسم خاطئ، بدل الاضطرار للبحث
    // يدوياً بين عشرات أسطر '.end/...' المتشابهة.
    consume(TokenType::DOT, "Expected '.' to start a closing tag like '.end/" + expectedTag +
                             "' (matches '@" + expectedTag + "' opened at line " + std::to_string(openLine) + ")");
    consume(TokenType::END, "Expected 'end' after '.' in closing tag (matches '@" + expectedTag +
                             "' opened at line " + std::to_string(openLine) + ")");
    // اختصار '.end;' : يغلق الكتلة الحالية أياً كان نوعها/اسمها دون الحاجة لتكرار كتابة الاثنين —
    // مفيد خصوصاً عند تعشيش عميق (container داخل Containers.Group داخل Volume...) حيث تكرار
    // '.end/container.pipe' في كل مستوى متعب ومصدر أخطاء نسخ/لصق شائع. الصحة البنيوية (أي كتلة
    // تُغلق فعلياً هي الصحيحة) مضمونة بالكامل بمعزل عن هذا الاختصار، لأن ترتيب الإغلاق يحدّده هيكل
    // النزول التكراري (recursive descent) نفسه في المحلل النحوي وليس ما يكتبه المبرمج بعد '.end'.
    if (match({TokenType::SEMICOLON})) return;
    consume(TokenType::SLASH, "Expected '/' after 'end' in closing tag (or ';' for the short form '.end;'), "
                               "matching '@" + expectedTag + "' opened at line " + std::to_string(openLine));
    Token beforeTag = peek();
    std::string closingTag = readTagKeyword();
    if (closingTag != expectedTag) {
        auto d = err(diag::Code::E0011_UnexpectedToken, beforeTag,
                     "closing tag `.end/" + closingTag + "` does not match the opening `@" + expectedTag + "`");
        d.diagnostic->withReason("`@" + expectedTag + "` was opened at line " + std::to_string(openLine) +
                                  " and must be closed with a matching `.end/" + expectedTag + "`")
         .withHint("replace `.end/" + closingTag + "` with `.end/" + expectedTag + "`");
        throw d;
    }
    // تحقق اختياري من الاسم: '.end/tag=name' — إن كُتب، يجب أن يطابق حرفياً اسم الفتح '@tag=name'
    // (مفيد كوسيلة أمان إضافية اختيارية في الحاويات المتداخلة الكبيرة، تماماً كتعليق
    // '} // namespace Foo' في لغات أخرى، لكن هنا يُتحقَّق منه فعلياً وقت التحليل النحوي بدل أن يكون
    // مجرد تعليق يمكن أن يفوت المبرمج تحديثه). كتابته اختيارية بالكامل ولا تُغيّر أي سلوك قديم.
    Token afterTag = peek();
    std::string closingName = readOptionalName();
    if (!closingName.empty()) {
        if (openName.empty()) {
            auto d = err(diag::Code::E0011_UnexpectedToken, afterTag,
                         "closing tag `.end/" + expectedTag + "=" + closingName + "` specifies a name, but the "
                         "opening `@" + expectedTag + "` has none");
            d.diagnostic->withReason("`@" + expectedTag + "` was opened at line " + std::to_string(openLine) + " with no name")
             .withHint("remove `=" + closingName + "` from the closing tag, or add a name to the opening `@" + expectedTag + "`");
            throw d;
        }
        if (closingName != openName) {
            auto d = err(diag::Code::E0011_UnexpectedToken, afterTag,
                         "closing tag `.end/" + expectedTag + "=" + closingName + "` does not match the opening name `" + openName + "`");
            d.diagnostic->withReason("`@" + expectedTag + "=" + openName + "` was opened at line " + std::to_string(openLine))
             .withHint("use `.end/" + expectedTag + "=" + openName + "` instead");
            throw d;
        }
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

// Loomtime rendering engine: view strands + reactive state -----------------------------
// @view.<Kind>=name  key=expr; ...  [<@view متداخلة>]  .end/view
// يُستدعى بعد أن يكون المستدعي (declaration()) قد استهلك بالفعل '@' و'view'، تماماً كما
// يفعل مع '@import'. نعيد استخدام readOptionalName()/checkClosingTag()/consumeEndTag()
// الموجودة أصلاً — readTagKeyword() تتعامل مع 'view' تلقائياً عبر مسارها العام (IDENT عادي)
// فتُرجع "view" كما هي، فتعمل consumeEndTag("view") بلا أي تعديل إضافي عليها.
std::shared_ptr<ViewStmt> Parser::viewDeclaration() {
    Token viewTok = previous(); // 'view'
    consume(TokenType::DOT, "Expected '.' after '@view' (did you mean '@view.Column=...'?)");
    Token kindTok = consume(TokenType::IDENT, "Expected a strand kind after '@view.' (e.g. Column, Text, Button)");
    std::string name = readOptionalName();

    auto s = std::make_shared<ViewStmt>();
    s->name = name;
    s->kindTag = kindTok.lexeme;
    s->line = viewTok.line;

    while (!checkClosingTag() && !isAtEnd()) {
        // كتلة @view متداخلة -> ابن Strand
        if (check(TokenType::AT) && checkNext(TokenType::IDENT) &&
            current + 1 < tokens.size() && tokens[current + 1].lexeme == "view") {
            advance(); // '@'
            advance(); // 'view'
            s->children.push_back(viewDeclaration());
            continue;
        }
        // سمة key=expr;  — المفتاح قد يكون IDENT عادياً أو أحد الكلمات المحجوزة في اللغة (مثل
        // 'text' أو 'file') التي تتصادف كونها اسم سمة طبيعياً هنا؛ لذا نعتمد على النظر للأمام
        // (وجود '=' مباشرة بعده) بدل اشتراط IDENT فقط — كما لا يوجد أي غموض مع كتلة متداخلة
        // (تبدأ بـ '@') أو وسم الإغلاق (يبدأ بـ '.'، مُستبعَد أصلاً عبر checkClosingTag أعلاه).
        if (!check(TokenType::AT) && checkNext(TokenType::EQUAL)) {
            Token key = advance();
            consume(TokenType::EQUAL, "Expected '=' after attribute key '" + key.lexeme +
                                       "' inside @view." + s->kindTag);
            ExprPtr val = expression(); // أي تعبير RIN عادي: نص/رقم/متغيّر warp/عملية/نداء دالة
            consume(TokenType::SEMICOLON, "Expected ';' after value for attribute '" + key.lexeme + "'");
            ViewAttr a;
            a.key = key.lexeme; a.value = val; a.line = key.line;
            s->attrs.push_back(a);
            continue;
        }
        throw err(diag::Code::E0012_MissingToken, peek(),
                  "expected an attribute (key=value;), a nested '@view...', or '.end/view' inside @view." + s->kindTag);
    }
    consumeEndTag("view", viewTok.line, name);
    return s;
}

// warp name = expr;
StmtPtr Parser::warpDeclaration() {
    Token tok = previous(); // 'warp'
    Token name = consume(TokenType::IDENT, "Expected a name after 'warp'");
    consume(TokenType::EQUAL, "Expected '=' after warp name");
    ExprPtr init = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after warp declaration");
    auto s = std::make_shared<WarpStmt>();
    s->name = name.lexeme;
    s->initializer = init;
    s->line = tok.line;
    return s;
}

StmtPtr Parser::atBlock() {
    Token atTok = previous(); // '@'
    std::string tag = readTagKeyword();
    static const std::vector<std::string> validTags = {
        "container", "container.pipe", "container.data", "container.api", "container.import", "container.table",
        "container.doc", "Containers.Group", "Volume", "table", "doc",
        // مفاهيم التنسيق والستايل: كائن (Object) / بوابة تنسيق (portal) / كتلة واجهة جاهزة (block)
        "container.object", "Object", "container.open/object", "container.portal", "portal", "container.block", "block",
        "container.sticker", "sticker", "container.aukt", "AUKT",
        // اختصارات مستقلة (بلا بادئة container.) لبقية أنواع الحاويات، بنفس مبدأ table/doc/Object/portal/
        // block/sticker/AUKT أعلاه: @pipe / @data / @api تُنتج بالضبط نفس ContainerKind::PIPE/DATA/API
        // التي تُنتجها container.pipe/container.data/container.api، بلا أي فرق دلالي — مجرد كتابة أقصر.
        // container.import استُثنيت عمداً من هذا النمط: '@import' مستقلة أصلاً كعبارة مختلفة تماماً
        // (استيراد ملف فعلي وتنفيذه فوراً)، فإضافة '@import=name ... .end/import' بنفس المعنى القديم
        // كانت ستتصادم مع تلك العبارة وتُربك القارئ لا أن تختصر له شيئاً.
        "pipe", "data", "api"
    };
    if (std::find(validTags.begin(), validTags.end(), tag) == validTags.end()) {
        auto d = err(diag::Code::E0015_UnknownContainer, atTok, "unsupported block `@" + tag + "`");
        d.diagnostic->withReason("`" + tag + "` is not a recognized Rin container/block kind");
        std::string best = diag::bestMatch(tag, validTags, 3);
        if (!best.empty()) {
            d.diagnostic->withSuggestion(best);
            d.diagnostic->withHint("did you mean `@" + best + "`?");
        } else {
            d.diagnostic->withHint("expected one of: container, container.pipe, container.data, container.api, "
                                    "container.import, container.table, container.doc, container.object, "
                                    "container.portal, container.block, container.sticker, container.aukt, "
                                    "Containers.Group, or Volume");
        }
        throw d;
    }
    std::string name = readOptionalName();
    std::vector<StmtPtr> body;
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag(tag, atTok.line, name);

    if (tag == "container" || tag == "container.pipe" || tag == "pipe" || tag == "container.data" || tag == "data" ||
        tag == "container.api" || tag == "api" || tag == "container.import" ||
        tag == "container.table" || tag == "table" ||
        tag == "container.doc" || tag == "doc" ||
        tag == "container.object" || tag == "Object" || tag == "container.open/object" ||
        tag == "container.portal" || tag == "portal" ||
        tag == "container.block" || tag == "block" ||
        tag == "container.sticker" || tag == "sticker" ||
        tag == "container.aukt" || tag == "AUKT") {
        // container.table/table (صفوف row + نمط style)، container.doc/doc (مستندات document)، وكذلك
        // container.object/Object، container.portal/portal، container.block/block، وأخيراً
        // container.sticker/sticker (بطاقة هوية بصرية جاهزة: أيقونة/ألوان/حواف/خلفية...) تشترك جميعاً
        // في نفس القيود: بيانات نقية، بلا دوال ولا حاويات متداخلة ولا route. عبارة 'style' مسموحة
        // بداخل أيٍّ منها (وليس فقط container.table) لضبط نمط العرض (مفهوم التنسيق/الستايل)، وكذلك
        // 'link'/'file' تعملان بداخلها بلا أي قيد إضافي (روابط links() وملف انتقال transition.file).
        if (tag != "container.open/object" && (tag == "container.data" || tag == "data" || tag == "container.table" || tag == "table" ||
            tag == "container.doc" || tag == "doc" ||
            tag == "container.object" || tag == "Object" || tag == "container.open/object" ||
            tag == "container.portal" || tag == "portal" ||
            tag == "container.block" || tag == "block" ||
            tag == "container.sticker" || tag == "sticker")) validateDataContainerBody(body);
        auto s = std::make_shared<ContainerStmt>();
        s->name = name; s->body = body; s->line = atTok.line;
        if (tag == "container.pipe" || tag == "pipe") s->kind = ContainerKind::PIPE;
        else if (tag == "container.data" || tag == "data") s->kind = ContainerKind::DATA;
        else if (tag == "container.api" || tag == "api") s->kind = ContainerKind::API;
        else if (tag == "container.import") s->kind = ContainerKind::IMPORT;
        else if (tag == "container.table" || tag == "table") s->kind = ContainerKind::TABLE;
        else if (tag == "container.doc" || tag == "doc") s->kind = ContainerKind::DOC;
        else if (tag == "container.object" || tag == "Object" || tag == "container.open/object") s->kind = ContainerKind::OBJECT;
        else if (tag == "container.portal" || tag == "portal") s->kind = ContainerKind::PORTAL;
        else if (tag == "container.block" || tag == "block") s->kind = ContainerKind::BLOCK;
        else if (tag == "container.sticker" || tag == "sticker") s->kind = ContainerKind::STICKER;
        else if (tag == "container.aukt" || tag == "AUKT") s->kind = ContainerKind::AUKT;
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

// @import "lib/data.og.rin";          -> يستدعيها declaration() بعد استهلاك '@' و'import' مسبقاً
// @import "lib/data.og.rin" as data;  -> 'as' كلمة سياقية غير محجوزة أيضاً، تُقرأ يدوياً هنا فقط.
StmtPtr Parser::importStatement() {
    Token kw = previous(); // 'import' (للحصول على رقم السطر)
    Token pathTok = consume(TokenType::STRING,
        "Expected a string path after '@import', e.g. @import \"lib/data.og.rin\";");
    auto pathExpr = std::make_shared<LiteralExpr>();
    pathExpr->kind = LiteralExpr::Kind::STRING;
    pathExpr->str = pathTok.lexeme;
    pathExpr->line = pathTok.line;

    std::string alias;
    if (check(TokenType::IDENT) && peek().lexeme == "as") {
        advance(); // 'as'
        Token aliasTok = consume(TokenType::IDENT, "Expected an alias name after 'as'");
        alias = aliasTok.lexeme;
    }
    consume(TokenType::SEMICOLON, "Expected ';' after @import statement");

    auto s = std::make_shared<ImportStmt>();
    s->path = pathExpr;
    s->alias = alias;
    s->line = kw.line;
    return s;
}

// container.data / container.table / table يجب أن تبقى "بيانات نقية": بلا تعريف دوال وبلا حاويات/مجموعات/أحجام
// متداخلة، وبلا route (المخصصة لـ container.api فقط) — ما يضمن أن أي حاوية بيانات (أو جدول) تبقى قابلة
// للتسلسل (serializable) بسهولة ولا تحمل منطقاً إجرائياً مخفياً بداخلها.
void Parser::validateDataContainerBody(const std::vector<StmtPtr>& body) {
    for (auto& st : body) {
        if (std::dynamic_pointer_cast<FunctionStmt>(st)) {
            auto d = errAtLine(diag::Code::E0014_InvalidContainer, st->line,
                               "functions (`fun`) are not allowed inside `container.data`/`container.table`/`table`");
            d.diagnostic->withReason("`container.data` containers must stay pure data (serializable)")
             .withHint("use `container` or `container.pipe` for logic instead");
            throw d;
        }
        if (std::dynamic_pointer_cast<ContainerStmt>(st) || std::dynamic_pointer_cast<ContainerGroupStmt>(st) ||
            std::dynamic_pointer_cast<VolumeStmt>(st)) {
            auto d = errAtLine(diag::Code::E0014_InvalidContainer, st->line,
                               "nested containers/groups/volumes are not allowed inside `container.data`/`container.table`/`table`");
            d.diagnostic->withReason("`object` containers cannot contain `route`, `container`, `Containers.Group`, or `Volume`");
            throw d;
        }
        if (std::dynamic_pointer_cast<RouteStmt>(st)) {
            auto d = errAtLine(diag::Code::E0014_InvalidContainer, st->line, "'route' is only valid inside `container.api`");
            d.diagnostic->withReason("`" + std::string("route") + "` defines an HTTP endpoint and only makes sense inside an API container")
             .withHint("move this `route` into a `container.api` block");
            throw d;
        }
    }
}

StmtPtr Parser::sectionBlock() {
    Token secTok = previous();
    std::string name = readOptionalName();
    std::vector<StmtPtr> body;
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag("Section", secTok.line, name);
    auto s = std::make_shared<SectionStmt>();
    s->name = name; s->body = body; s->line = secTok.line;
    return s;
}

StmtPtr Parser::translationsBlock() {
    Token tTok = previous();
    std::vector<StmtPtr> body;
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag("Translations", tTok.line, "");
    auto s = std::make_shared<TranslationsStmt>();
    s->body = body; s->line = tTok.line;
    return s;
}

StmtPtr Parser::translationStatement() {
    Token tok = previous();
    Token langKey = consume(TokenType::IDENT, "Expected 'lang' after 'translation'");
    if (langKey.lexeme != "lang") throw err(diag::Code::E0016_InvalidProperty, langKey, "expected 'lang' attribute after 'translation'");
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

    // link.id="X";  -> تسجيل معرّف ربط عام (container.link.id) للحاوية الحالية
    if (check(TokenType::DOT)) {
        advance(); // '.'
        Token idKw = consume(TokenType::IDENT, "Expected 'id' after 'link.'");
        if (idKw.lexeme != "id") {
            auto d = err(diag::Code::E0016_InvalidProperty, idKw, "unknown attribute `link." + idKw.lexeme + "`");
            d.diagnostic->withSuggestion("id").withHint("did you mean `link.id=`?");
            throw d;
        }
        consume(TokenType::EQUAL, "Expected '=' after 'link.id'");
        Token val = consume(TokenType::STRING, "Expected a text value after 'link.id='");
        consume(TokenType::SEMICOLON, "Expected ';' after link.id statement");
        auto s = std::make_shared<LinkIdDeclStmt>();
        s->id = val.lexeme; s->line = tok.line;
        return s;
    }

    Token key = consume(TokenType::IDENT, "Expected 'to' or 'id' after 'link'");
    if (key.lexeme != "to" && key.lexeme != "id")
        throw err(diag::Code::E0016_InvalidProperty, key, "expected 'to' or 'id' attribute after 'link'");
    consume(TokenType::EQUAL, "Expected '=' after '" + key.lexeme + "'");

    auto s = std::make_shared<LinkStmt>();
    s->line = tok.line;
    if (key.lexeme == "id") {
        // link id="X";  -> ربط بمعرّف عام بدل اسم الحاوية (يعمل عبر الملفات)
        Token val = consume(TokenType::STRING, "Expected a text value after 'id='");
        s->byId = val.lexeme;
    } else {
        // link to=name;  -> ربط باسم الحاوية (كما كان)
        if (!check(TokenType::IDENT) && !check(TokenType::STRING))
            throw err(diag::Code::E0012_MissingToken, peek(), "expected a container name after 'to='");
        s->target = advance().lexeme;
    }
    consume(TokenType::SEMICOLON, "Expected ';' after link statement");
    return s;
}

StmtPtr Parser::tyingStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'with' after 'tying'");
    if (key.lexeme != "with") throw err(diag::Code::E0016_InvalidProperty, key, "expected 'with' attribute after 'tying'");
    consume(TokenType::EQUAL, "Expected '=' after 'with'");
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw err(diag::Code::E0012_MissingToken, peek(), "expected a container name after 'with='");
    std::string target = advance().lexeme;
    consume(TokenType::SEMICOLON, "Expected ';' after tying statement");
    auto s = std::make_shared<TyingStmt>();
    s->target = target; s->line = tok.line;
    return s;
}

StmtPtr Parser::mergeStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'with' after 'merge'");
    if (key.lexeme != "with") {
        auto d = err(diag::Code::E0016_InvalidProperty, key, "invalid attribute `" + key.lexeme + "`");
        d.diagnostic->withReason("`merge` expects the attribute `with`");
        if (key.lexeme == "from") {
            d.diagnostic->withHint("replace `from` with `with`");
        } else {
            d.diagnostic->withSuggestion("with").withHint("did you mean `with`?");
        }
        throw d;
    }
    consume(TokenType::EQUAL, "Expected '=' after 'with'");
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw err(diag::Code::E0012_MissingToken, peek(), "expected a container name after 'with='");
    std::string target = advance().lexeme;
    consume(TokenType::SEMICOLON, "Expected ';' after merge statement");
    auto s = std::make_shared<MergeStmt>();
    s->target = target; s->line = tok.line;
    return s;
}

// يقرأ "format=IDENT" اختيارياً (مثال: format=png; أو format=zip;) — تُستخدم من save/installation.
// القيمة تُقرأ كمعرّف (IDENT) لا كنص، تماشياً مع بقية أسماء الصيغ في اللغة (pipe/data/api/import).
// تُرجع سلسلة فارغة إن لم يظهر 'format' إطلاقاً (الصيغة النصية .rin الافتراضية تبقى كما هي).
std::string Parser::readOptionalFormatAttr() {
    if (!(check(TokenType::IDENT) && peek().lexeme == "format")) return "";
    advance(); // 'format'
    consume(TokenType::EQUAL, "Expected '=' after 'format'");
    if (!check(TokenType::IDENT)) throw err(diag::Code::E0012_MissingToken, peek(), "expected a format name after 'format=' (e.g. png, zip)");
    return advance().lexeme;
}

StmtPtr Parser::installationStatement(bool simplifiedFlag) {
    Token tok = previous();
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw err(diag::Code::E0012_MissingToken, peek(), "expected a name after 'installation'");
    std::string target = advance().lexeme;
    std::string format = readOptionalFormatAttr();
    consume(TokenType::SEMICOLON, "Expected ';' after installation statement");
    auto s = std::make_shared<InstallationStmt>();
    s->target = target; s->simplified = simplifiedFlag; s->format = format; s->line = tok.line;
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
    std::string format = readOptionalFormatAttr();
    consume(TokenType::SEMICOLON, "Expected ';' after save statement");
    auto s = std::make_shared<SaveStmt>();
    s->path = pathExpr; s->simplified = simplifiedFlag; s->format = format; s->line = tok.line;
    return s;
}

// row cells=[v1, v2, ...];  -> صف واحد داخل الجدول الحالي (container.table / table)
StmtPtr Parser::rowStatement() {
    Token tok = previous(); // 'row'
    Token key = consume(TokenType::IDENT, "Expected 'cells' after 'row'");
    if (key.lexeme != "cells") throw err(diag::Code::E0016_InvalidProperty, key, "expected 'cells' attribute after 'row' (e.g. row cells=[1, 2, 3];)");
    consume(TokenType::EQUAL, "Expected '=' after 'cells'");
    ExprPtr cellsExpr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after row statement");
    auto s = std::make_shared<RowStmt>();
    s->cells = cellsExpr; s->line = tok.line;
    return s;
}

// style value="style://theme";  -> نمط عرض الجدول الحالي (container.table / table)
StmtPtr Parser::styleStatement() {
    Token tok = previous(); // 'style'
    Token key = consume(TokenType::IDENT, "Expected 'value' after 'style'");
    if (key.lexeme != "value") throw err(diag::Code::E0016_InvalidProperty, key, "expected 'value' attribute after 'style' (e.g. style value=\"style://dark\";)");
    consume(TokenType::EQUAL, "Expected '=' after 'value'");
    ExprPtr valueExpr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after style statement");
    auto s = std::make_shared<StyleStmt>();
    s->value = valueExpr; s->line = tok.line;
    return s;
}

// document id="u1" fields={ name: "Ali", age: 30 };  -> مستند واحد داخل حاوية NoSQL الحالية (container.doc / doc)
StmtPtr Parser::documentStatement() {
    Token tok = previous(); // 'document'
    Token idKey = consume(TokenType::IDENT, "Expected 'id' after 'document'");
    if (idKey.lexeme != "id") throw err(diag::Code::E0016_InvalidProperty, idKey, "expected 'id' attribute after 'document' (e.g. document id=\"u1\" fields={...};)");
    consume(TokenType::EQUAL, "Expected '=' after 'id'");
    ExprPtr idExpr = expression();
    Token fieldsKey = consume(TokenType::IDENT, "Expected 'fields' after 'document id=...'");
    if (fieldsKey.lexeme != "fields") throw err(diag::Code::E0016_InvalidProperty, fieldsKey, "expected 'fields' attribute after 'document id=...' (e.g. document id=\"u1\" fields={...};)");
    consume(TokenType::EQUAL, "Expected '=' after 'fields'");
    ExprPtr fieldsExpr = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after document statement");
    auto s = std::make_shared<DocumentStmt>();
    s->id = idExpr; s->fields = fieldsExpr; s->line = tok.line;
    return s;
}

StmtPtr Parser::fileStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'path' after 'file'");
    if (key.lexeme != "path") throw err(diag::Code::E0016_InvalidProperty, key, "expected 'path' attribute after 'file'");
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
            auto d = err(diag::Code::E0016_InvalidProperty, key, "expected '" + expected + "' attribute in route statement, found `" + key.lexeme + "`");
            d.diagnostic->withSuggestion(expected);
            throw d;
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
    throw err(diag::Code::E0011_UnexpectedToken, peek(), "'simplified' must be followed by 'installation' or 'save'");
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
        throw err(diag::Code::E0003_InvalidAssignment, eq, "invalid assignment target");
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
                throw err(diag::Code::E0013_InvalidExpression, opTok, "expected a function call (e.g. 'step()') after '|>'");
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
            if (!var) throw err(diag::Code::E0013_InvalidExpression, previous(), "only functions can be called");
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
                    throw err(diag::Code::E0013_InvalidExpression, peek(), "expected a key (name or string) in map literal");
                }
                consume(TokenType::COLON, "Expected ':' after map key");
                auto valueExpr = expression();
                map->entries.push_back({keyExpr, valueExpr});
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RBRACE, "Expected '}' after map entries");
        return map;
    }
    throw err(diag::Code::E0013_InvalidExpression, peek(), "expected expression");
}

} // namespace rin

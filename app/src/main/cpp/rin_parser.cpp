#include "rin_parser.h"
#include <algorithm>
#include <unordered_set>

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
        if (seen.count(attr)) throw RinError("'print': '" + attr + "' attribute repeated", tok.line);
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
    if (loopDepth == 0) throw RinError("'break' used outside of a loop", tok.line);
    consume(TokenType::SEMICOLON, "Expected ';' after 'break'");
    auto stmt = std::make_shared<BreakStmt>();
    stmt->line = tok.line;
    return stmt;
}

// continue; -> نفس قيد break: صالحة فقط داخل جسم حلقة while.
StmtPtr Parser::continueStatement() {
    Token tok = previous();
    if (loopDepth == 0) throw RinError("'continue' used outside of a loop", tok.line);
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
                                     w == "object" || w == "portal" || w == "block" || w == "aukt");
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
        throw RinError("Expected an attribute (key=value;), a nested '@view...', or '.end/view' inside "
                        "@view." + s->kindTag, peek().line);
    }
    consumeEndTag("view");
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
        "container.object", "Object", "container.portal", "portal", "container.block", "block",
        "container.sticker", "sticker", "container.aukt", "AUKT"
    };
    if (std::find(validTags.begin(), validTags.end(), tag) == validTags.end()) {
        throw RinError("Unsupported block '@" + tag + "'; expected container, container.pipe, container.data, "
                        "container.api, container.import, container.table, table, container.doc, doc, "
                        "container.object, Object, container.portal, portal, container.block, block, "
                        "container.sticker, sticker, container.aukt, AUKT, Containers.Group, or Volume", atTok.line);
    }
    std::string name = readOptionalName();
    std::vector<StmtPtr> body;
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag(tag);

    if (tag == "container" || tag == "container.pipe" || tag == "container.data" ||
        tag == "container.api" || tag == "container.import" ||
        tag == "container.table" || tag == "table" ||
        tag == "container.doc" || tag == "doc" ||
        tag == "container.object" || tag == "Object" ||
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
        if (tag == "container.data" || tag == "container.table" || tag == "table" ||
            tag == "container.doc" || tag == "doc" ||
            tag == "container.object" || tag == "Object" ||
            tag == "container.portal" || tag == "portal" ||
            tag == "container.block" || tag == "block" ||
            tag == "container.sticker" || tag == "sticker") validateDataContainerBody(body);
        auto s = std::make_shared<ContainerStmt>();
        s->name = name; s->body = body; s->line = atTok.line;
        if (tag == "container.pipe") s->kind = ContainerKind::PIPE;
        else if (tag == "container.data") s->kind = ContainerKind::DATA;
        else if (tag == "container.api") s->kind = ContainerKind::API;
        else if (tag == "container.import") s->kind = ContainerKind::IMPORT;
        else if (tag == "container.table" || tag == "table") s->kind = ContainerKind::TABLE;
        else if (tag == "container.doc" || tag == "doc") s->kind = ContainerKind::DOC;
        else if (tag == "container.object" || tag == "Object") s->kind = ContainerKind::OBJECT;
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
            throw RinError("لا يُسمح بتعريف دوال (fun) داخل container.data/container.table/table؛ استخدم container أو container.pipe لذلك", st->line);
        }
        if (std::dynamic_pointer_cast<ContainerStmt>(st) || std::dynamic_pointer_cast<ContainerGroupStmt>(st) ||
            std::dynamic_pointer_cast<VolumeStmt>(st)) {
            throw RinError("لا يُسمح بحاويات/مجموعات/أحجام متداخلة داخل container.data/container.table/table", st->line);
        }
        if (std::dynamic_pointer_cast<RouteStmt>(st)) {
            throw RinError("عبارة 'route' مخصصة لـ container.api فقط", st->line);
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

    // link.id="X";  -> تسجيل معرّف ربط عام (container.link.id) للحاوية الحالية
    if (check(TokenType::DOT)) {
        advance(); // '.'
        Token idKw = consume(TokenType::IDENT, "Expected 'id' after 'link.'");
        if (idKw.lexeme != "id") throw RinError("Unknown 'link." + idKw.lexeme + "': did you mean 'link.id='?", idKw.line);
        consume(TokenType::EQUAL, "Expected '=' after 'link.id'");
        Token val = consume(TokenType::STRING, "Expected a text value after 'link.id='");
        consume(TokenType::SEMICOLON, "Expected ';' after link.id statement");
        auto s = std::make_shared<LinkIdDeclStmt>();
        s->id = val.lexeme; s->line = tok.line;
        return s;
    }

    Token key = consume(TokenType::IDENT, "Expected 'to' or 'id' after 'link'");
    if (key.lexeme != "to" && key.lexeme != "id")
        throw RinError("Expected 'to' or 'id' attribute after 'link'", key.line);
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
            throw RinError("Expected a container name after 'to='", peek().line);
        s->target = advance().lexeme;
    }
    consume(TokenType::SEMICOLON, "Expected ';' after link statement");
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

// يقرأ "format=IDENT" اختيارياً (مثال: format=png; أو format=zip;) — تُستخدم من save/installation.
// القيمة تُقرأ كمعرّف (IDENT) لا كنص، تماشياً مع بقية أسماء الصيغ في اللغة (pipe/data/api/import).
// تُرجع سلسلة فارغة إن لم يظهر 'format' إطلاقاً (الصيغة النصية .rin الافتراضية تبقى كما هي).
std::string Parser::readOptionalFormatAttr() {
    if (!(check(TokenType::IDENT) && peek().lexeme == "format")) return "";
    advance(); // 'format'
    consume(TokenType::EQUAL, "Expected '=' after 'format'");
    if (!check(TokenType::IDENT)) throw RinError("Expected a format name after 'format=' (e.g. png, zip)", peek().line);
    return advance().lexeme;
}

StmtPtr Parser::installationStatement(bool simplifiedFlag) {
    Token tok = previous();
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw RinError("Expected a name after 'installation'", peek().line);
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
    if (key.lexeme != "cells") throw RinError("Expected 'cells' attribute after 'row' (e.g. row cells=[1, 2, 3];)", key.line);
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
    if (key.lexeme != "value") throw RinError("Expected 'value' attribute after 'style' (e.g. style value=\"style://dark\";)", key.line);
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
    if (idKey.lexeme != "id") throw RinError("Expected 'id' attribute after 'document' (e.g. document id=\"u1\" fields={...};)", idKey.line);
    consume(TokenType::EQUAL, "Expected '=' after 'id'");
    ExprPtr idExpr = expression();
    Token fieldsKey = consume(TokenType::IDENT, "Expected 'fields' after 'document id=...'");
    if (fieldsKey.lexeme != "fields") throw RinError("Expected 'fields' attribute after 'document id=...' (e.g. document id=\"u1\" fields={...};)", fieldsKey.line);
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

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

RinError Parser::errRich(diag::Code code, const Token& tok, std::string message, std::string reason,
                          std::string hint, std::string expected, std::string found) const {
    diag::Diagnostic d(code, std::move(message), locOf(tok));
    if (!reason.empty()) d.withReason(std::move(reason));
    if (!hint.empty()) d.withHint(std::move(hint));
    if (!expected.empty()) d.expected = std::move(expected);
    if (!found.empty()) d.found = std::move(found);
    else d.found = (tok.type == TokenType::END_OF_FILE) ? "end of file"
                   : (tok.lexeme.empty() ? "an unexpected token" : ("`" + tok.lexeme + "`"));
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
    // 'make name = expr;' / 'make name;' -> صيغة إنجليزية مبسّطة سهلة التعلّم، مرادف كامل لِـ
    // 'let name = expr;' (يفوّض مباشرة إلى letDeclaration() نفسها، فيرث كل سلوكها بلا أي فرق).
    // 'make' كلمة سياقية غير محجوزة (تُقرأ IDENT عادي، بنفس أسلوب route/row/style/document/warp
    // أدناه)، فلا تتعارض مع استخدامها اسماً لحاوية عبر '@make=...' (انظر atBlock()/readTagKeyword())
    // ولا مع استخدامها اسم دالة عادية `make(...)`: الشرط أدناه لا يتحقق إلا حين تُتبَع مباشرة باسم
    // متغيّر (IDENT)، أي بالضبط شكل إعلان متغيّر.
    if (check(TokenType::IDENT) && peek().lexeme == "make" && checkNext(TokenType::IDENT)) {
        advance();
        return letDeclaration();
    }

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
    // @element.<Kind>=name ... .end/element -> جاهز وظيفي بلا Style.
    if (check(TokenType::AT) && checkNext(TokenType::IDENT) &&
        current + 1 < tokens.size() && tokens[current + 1].lexeme == "element") {
        advance(); advance();
        return elementDeclaration();
    }
    // @loop.<Kind>=name ... .end/loop / @loop=name ... -> قماش المظهر والتخطيط.
    if (check(TokenType::AT) && checkNext(TokenType::IDENT) &&
        current + 1 < tokens.size() && tokens[current + 1].lexeme == "loop") {
        advance(); advance();
        return loopCanvasDeclaration();
    }
    // RCS-1.0 §3.6 Events (Phase 2): 'on.event' STRING '(' params ')' '{' body '}' -- يُميَّز عن
    // on.<element>.<event> أدناه بالنظر ثلاث خطوات للأمام: هنا IDENT("event") ثم STRING مباشرة،
    // بينما on.<element>.<event> يكون IDENT(اسم العنصر) ثم DOT ثم IDENT(اسم الحدث). يجب فحص هذا
    // *قبل* on.<element>.<event> أدناه (كلاهما يبدأ بـ on '.') حتى لا يُبتلَع خطأً كربط عنصر عادي.
    if (check(TokenType::IDENT) && peek().lexeme == "on" && checkNext(TokenType::DOT) &&
        current + 2 < tokens.size() && tokens[current + 2].type == TokenType::IDENT &&
        tokens[current + 2].lexeme == "event" &&
        current + 3 < tokens.size() && tokens[current + 3].type == TokenType::STRING) {
        advance(); // 'on'
        advance(); // '.'
        advance(); // 'event'
        return eventHandlerDeclaration();
    }
    // on.<element>.<event>=handler(...); is behavior owned by the surrounding container.
    if (check(TokenType::IDENT) && peek().lexeme == "on" && checkNext(TokenType::DOT)) {
        return uiBindingStatement();
    }
    // RCS-1.0 §3.2 Lifecycle: 'on' IDENT('init'/'mount'/'update'/'destroy'/'error') '(' ... — يُميَّز
    // عن on.<element>.<event> أعلاه بالنظر التالي مباشرة: هنا التالي IDENT (اسم الخُطّاف) لا DOT.
    // 'on' كلمة سياقية غير محجوزة (بنفس أسلوب route/row/document/warp)، فلا تتعارض مع استخدامها
    // اسم متغيّر عادي في أي سياق آخر.
    if (check(TokenType::IDENT) && peek().lexeme == "on" && checkNext(TokenType::IDENT)) {
        advance(); // 'on'
        return lifecycleHookDeclaration();
    }
    // RCS-1.0 §3.3 State: 'state' IDENT '=' expr ';' -- كلمة سياقية غير محجوزة أيضاً (بنفس أسلوب
    // 'route'/'row'/'document'/'warp' أعلاه)، مُميَّزة بالنظر خطوة إضافية للأمام (IDENT مباشرة
    // بعدها) حتى لا تصطدم باستخدام "state" اسم متغيّر عادي في أي سياق آخر.
    if (check(TokenType::IDENT) && peek().lexeme == "state" && checkNext(TokenType::IDENT)) {
        advance(); // 'state'
        return stateDeclaration();
    }
    // RCS-1.0 §3.5 Tree (Phase 2): 'slot' IDENT ';' -- كلمة سياقية غير محجوزة أيضاً (بنفس أسلوب
    // 'state' أعلاه)، مُميَّزة بالنظر خطوة إضافية للأمام حتى لا تصطدم باستخدام "slot" اسم متغيّر
    // عادي في أي سياق آخر.
    if (check(TokenType::IDENT) && peek().lexeme == "slot" && checkNext(TokenType::IDENT)) {
        advance(); // 'slot'
        return slotDeclaration();
    }
    // RCS-1.0 §3.6 Events (Phase 2): 'emit' STRING (',' expr)? ('bubbles')? ';' -- كلمة سياقية
    // غير محجوزة أيضاً، مُميَّزة بالنظر خطوة إضافية للأمام (STRING مباشرة بعدها، لا أي شيء آخر)
    // حتى لا تصطدم باستخدام "emit" اسم متغيّر/دالة عادية في أي سياق آخر.
    if (check(TokenType::IDENT) && peek().lexeme == "emit" && checkNext(TokenType::STRING)) {
        advance(); // 'emit'
        return emitStatement();
    }
    // RCS-1.0 §3.14 Dependency (Phase 3): 'requires' IDENT (',' IDENT)* ';' -- كلمة سياقية غير
    // محجوزة أيضاً (بنفس أسلوب 'state'/'slot' أعلاه بالضبط)، مُميَّزة بالنظر خطوة إضافية للأمام
    // (IDENT مباشرة بعدها) حتى لا تصطدم باستخدام "requires" اسم متغيّر/دالة عادية في أي سياق آخر.
    if (check(TokenType::IDENT) && peek().lexeme == "requires" && checkNext(TokenType::IDENT)) {
        advance(); // 'requires'
        return dependencyDeclaration();
    }
    // '@theme=Name key=expr; ... .end/theme' -> Rin Loom Theme (Pattern Book) declaration, نفس
    // أسلوب فحص '@import'/'@view' أعلاه بالضبط.
    if (check(TokenType::AT) && checkNext(TokenType::IDENT) &&
        current + 1 < tokens.size() && tokens[current + 1].lexeme == "theme") {
        advance(); // '@'
        advance(); // 'theme'
        return themeDeclaration();
    }
    // 'view.print/object(expr);' -> معاينة حيّة لكائن في الكونسول (امتداد إضافي، لا علاقة بـ
    // '@view.<Kind>' أعلاه رغم تشابه الاسم). كلمة سياقية غير محجوزة: 'view' تُقرأ IDENT عادي، فلا
    // تتحوّل لعبارة خاصة إلا عند ظهورها بالضبط بصيغة 'view' '.' 'print' في بداية عبارة (نفس أسلوب
    // فحص route/row/style/document/warp أعلاه) — 'print' نفسها كلمة محجوزة (TokenType::PRINT).
    if (check(TokenType::IDENT) && peek().lexeme == "view" &&
        checkNext(TokenType::DOT) &&
        current + 2 < tokens.size() && tokens[current + 2].type == TokenType::PRINT) {
        advance(); // 'view'
        advance(); // '.'
        advance(); // 'print'
        return viewPrintObjectStatement();
    }
    // Make Unit الحقيقي: @make.(name) ... .end/make[=name]
    // نميّزه قبل atBlock() لأن @make القديمة (بدون .(...)) تبقى ContainerKind::EVERYTHING للتوافق.
    if (check(TokenType::AT) && current + 5 < tokens.size() &&
        tokens[current + 1].type == TokenType::IDENT && tokens[current + 1].lexeme == "make" &&
        tokens[current + 2].type == TokenType::DOT &&
        tokens[current + 3].type == TokenType::LPAREN) {
        return makeUnitBlock();
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
    // 'reckon name(collection) [where cond] |> fn() ...;' -> انظر docs/RECKON.md و ReckonStmt في
    // rin_ast.h. كلمة سياقية غير محجوزة (بنفس أسلوب route/row/document/warp أعلاه بالضبط)، مُميَّزة
    // هنا بالنظر خطوة إضافية للأمام (IDENT مباشرة بعدها) حتى لا تصطدم باستخدام "reckon" اسم متغيّر
    // عادي في أي سياق آخر.
    if (check(TokenType::IDENT) && peek().lexeme == "reckon" && checkNext(TokenType::IDENT)) {
        advance(); // 'reckon'
        return reckonDeclaration();
    }
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

// RCS-1.0 §3.2 Lifecycle: on init/mount/update/destroy/error(params) { body }  (يُستدعى بعد
// استهلاك 'on' من declaration()). الجسم يُبنى داخلياً كـ FunctionStmt عادي (اسمه "on <hook>")
// حتى يُنفَّذ لاحقاً عبر Interpreter::callFunction الموجودة فعلاً بلا أي آلية استدعاء موازية.
StmtPtr Parser::lifecycleHookDeclaration() {
    Token hookTok = consume(TokenType::IDENT,
        "Expected a lifecycle hook name after 'on' (init/mount/update/destroy/error)");
    static const std::unordered_set<std::string> validHooks = {"init", "mount", "update", "destroy", "error"};
    if (!validHooks.count(hookTok.lexeme)) {
        auto d = err(diag::Code::E0012_MissingToken, hookTok,
                     "unknown lifecycle hook 'on " + hookTok.lexeme + "'");
        d.diagnostic->withReason("RCS-1.0 §3.2 Lifecycle defines exactly five hooks")
         .withHint("expected one of: on init(), on mount(), on update(prevState), on destroy(), on error(err)");
        throw d;
    }
    consume(TokenType::LPAREN, "Expected '(' after lifecycle hook name 'on " + hookTok.lexeme + "'");
    std::vector<std::string> params;
    if (!check(TokenType::RPAREN)) {
        do {
            params.push_back(consume(TokenType::IDENT, "Expected parameter name").lexeme);
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RPAREN, "Expected ')' after lifecycle hook parameters");
    consume(TokenType::LBRACE, "Expected '{' before lifecycle hook body 'on " + hookTok.lexeme + "'");
    // نفس حماية loopDepth الموجودة في functionDeclaration(): جسم الخُطّاف يبدأ سياق حلقة جديداً
    // من الصفر (break/continue بداخله لا يُعتبران صالحين إلا بحلقة while داخل الخُطّاف نفسه).
    int savedLoopDepth = loopDepth;
    loopDepth = 0;
    auto body = block();
    loopDepth = savedLoopDepth;

    auto fn = std::make_shared<FunctionStmt>();
    fn->name = "on " + hookTok.lexeme; // لأغراض رسائل الخطأ فقط (عدد الوسائط في callFunction)
    fn->params = params;
    fn->body = body;
    fn->line = hookTok.line;

    auto s = std::make_shared<LifecycleHookStmt>();
    s->hook = hookTok.lexeme;
    s->asFunction = fn;
    s->line = hookTok.line;
    return s;
}

// RCS-1.0 §3.3 State: state IDENT = expr;  (يُستدعى بعد استهلاك 'state' من declaration()).
// التهيئة إلزامية دائماً (مطابقة تماماً للـ EBNF: state_stmt ::= "state" IDENT "=" expr ";").
StmtPtr Parser::stateDeclaration() {
    auto name = consume(TokenType::IDENT, "Expected state field name after 'state'");
    consume(TokenType::EQUAL, "Expected '=' after state field name ('state' fields must be initialized)");
    ExprPtr initializer = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after state field declaration");
    auto stmt = std::make_shared<StateDeclStmt>();
    stmt->name = name.lexeme;
    stmt->initializer = initializer;
    stmt->line = name.line;
    return stmt;
}

// RCS-1.0 §3.5 Tree (Phase 2): slot IDENT;  (يُستدعى بعد استهلاك 'slot' من declaration()).
StmtPtr Parser::slotDeclaration() {
    Token name = consume(TokenType::IDENT, "Expected a slot name after 'slot'");
    consume(TokenType::SEMICOLON, "Expected ';' after slot declaration");
    auto stmt = std::make_shared<SlotDeclStmt>();
    stmt->name = name.lexeme;
    stmt->line = name.line;
    return stmt;
}

// RCS-1.0 §3.6 Events (Phase 2): emit STRING ("," expr)? ("bubbles")? ";"  (يُستدعى بعد استهلاك
// 'emit' من declaration()). 'bubbles' كلمة سياقية اختيارية بلا وسائط: تُقرأ IDENT عادي، لا تتعارض
// مع أي استخدام آخر لهذا الاسم لأنها تُفحَص فقط في هذا الموضع الدقيق (بين payload اختياري و';').
StmtPtr Parser::emitStatement() {
    Token nameTok = consume(TokenType::STRING, "Expected an event name string after 'emit'");
    auto stmt = std::make_shared<EmitStmt>();
    stmt->eventName = nameTok.lexeme;
    stmt->line = nameTok.line;
    if (match({TokenType::COMMA})) {
        stmt->payload = expression();
    }
    if (check(TokenType::IDENT) && peek().lexeme == "bubbles") {
        advance(); // 'bubbles'
        stmt->bubbles = true;
    }
    consume(TokenType::SEMICOLON, "Expected ';' after 'emit' statement");
    return stmt;
}

// RCS-1.0 §3.6 Events (Phase 2): on.event STRING '(' params ')' '{' body '}'  (يُستدعى بعد
// استهلاك 'on' '.' 'event' من declaration()). نفس أسلوب lifecycleHookDeclaration() تماماً: الجسم
// يُبنى كـ FunctionStmt عادي جاهز للاستدعاء عبر Interpreter::callFunction الموجودة فعلاً بلا أي
// آلية استدعاء موازية جديدة.
StmtPtr Parser::eventHandlerDeclaration() {
    Token nameTok = consume(TokenType::STRING, "Expected an event name string after 'on.event'");
    consume(TokenType::LPAREN, "Expected '(' after event name in 'on.event \"" + nameTok.lexeme + "\"'");
    std::vector<std::string> params;
    if (!check(TokenType::RPAREN)) {
        do {
            params.push_back(consume(TokenType::IDENT, "Expected parameter name").lexeme);
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RPAREN, "Expected ')' after event handler parameters");
    consume(TokenType::LBRACE, "Expected '{' before event handler body 'on.event \"" + nameTok.lexeme + "\"'");
    int savedLoopDepth = loopDepth;
    loopDepth = 0;
    auto body = block();
    loopDepth = savedLoopDepth;

    auto fn = std::make_shared<FunctionStmt>();
    fn->name = "on.event " + nameTok.lexeme; // لأغراض رسائل الخطأ فقط (عدد الوسائط في callFunction)
    fn->params = params;
    fn->body = body;
    fn->line = nameTok.line;

    auto s = std::make_shared<EventHandlerStmt>();
    s->eventName = nameTok.lexeme;
    s->asFunction = fn;
    s->line = nameTok.line;
    return s;
}

// RCS-1.0 §3.14 Dependency (Phase 3): requires IDENT (',' IDENT)* ';'  (يُستدعى بعد استهلاك
// 'requires' من declaration()). قائمة أسماء حاويات مطلوبة، فاصلة بينها فاصلة (,)، بلا أي أنواع أو
// تعبيرات -- مطابقة حرفية لـ EBNF: dependency_stmt ::= "requires" IDENT ("," IDENT)* ";".
StmtPtr Parser::dependencyDeclaration() {
    auto stmt = std::make_shared<DependencyStmt>();
    Token first = consume(TokenType::IDENT, "Expected a container name after 'requires'");
    stmt->line = first.line;
    stmt->names.push_back(first.lexeme);
    while (match({TokenType::COMMA})) {
        stmt->names.push_back(consume(TokenType::IDENT, "Expected a container name after ',' in 'requires' list").lexeme);
    }
    consume(TokenType::SEMICOLON, "Expected ';' after 'requires' statement");
    return stmt;
}

// reckon <name>(<collection>)
//     [where <condition>] |> <function>() [|> <function>() ...];
// Always exactly two lines in source (no semicolon after the header -- only one, at the very
// end); see docs/RECKON.md. `where`/`item` are contextual (never reserved), matching every other
// contextual keyword in this parser.
StmtPtr Parser::reckonDeclaration() {
    Token nameTok = consume(TokenType::IDENT, "Expected a result name after 'reckon'");
    consume(TokenType::LPAREN, "Expected '(' after the reckon result name, e.g. reckon name(collection)");
    auto stmt = std::make_shared<ReckonStmt>();
    stmt->name = nameTok.lexeme;
    stmt->line = nameTok.line;
    stmt->collection = expression();
    consume(TokenType::RPAREN, "Expected ')' after the reckon collection expression");

    if (check(TokenType::IDENT) && peek().lexeme == "where") {
        advance(); // 'where'
        // logicOr(), not pipeline(): a `where` condition is a plain boolean expression over
        // `item` and must not itself try to swallow the reckon body's own '|>' chain.
        stmt->whereCond = logicOr();
    }

    consume(TokenType::PIPE, "Expected '|>' to start the reckon pipeline body (after the optional 'where' clause)");
    do {
        Token fnTok = consume(TokenType::IDENT, "Expected a function name after '|>' in a reckon body");
        auto stage = std::make_shared<CallExpr>();
        stage->callee = fnTok.lexeme;
        stage->line = fnTok.line;
        consume(TokenType::LPAREN, "Expected '(' after function name '" + fnTok.lexeme + "' in reckon body");
        if (!check(TokenType::RPAREN)) {
            do { stage->args.push_back(expression()); } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RPAREN, "Expected ')' after arguments for '" + fnTok.lexeme + "' in reckon body");
        stmt->stages.push_back(stage);
    } while (match({TokenType::PIPE}));

    consume(TokenType::SEMICOLON, "Expected ';' after the reckon pipeline body");
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
    // print.log(...) / print.log.info/warn/error/debug(...) -> نظام Log منظَّم (انظر LogStmt في
    // rin_ast.h). يُفحَص قبل match({TokenType::PRINT}) العادي أدناه: PRINT '.' IDENT("log") هو
    // الشكل الوحيد الذي يُحوَّل لعبارة Log خاصة؛ أي 'print' أخرى (بلا '.' IDENT("log") تالياً
    // مباشرة) تبقى عبارة print العادية تماماً كما كانت، بلا أي تغيير في سلوكها.
    if (check(TokenType::PRINT) && checkNext(TokenType::DOT) &&
        current + 2 < tokens.size() && tokens[current + 2].type == TokenType::IDENT &&
        tokens[current + 2].lexeme == "log") {
        Token printTok = peek();
        advance(); // 'print'
        advance(); // '.'
        advance(); // 'log'
        return logStatement(printTok);
    }
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
    if (object.lexeme != "object")
        throw errRich(diag::Code::E0016_InvalidProperty, object,
                       "expected `.object=type` or `.object(\"id\")`",
                       "an object-field statement must start with the literal keyword `object` right "
                       "after the leading '.', either as `.object=type` (typed field) or "
                       "`.object(\"id\")` (object literal by id)",
                       "replace `" + object.lexeme + "` with `object`",
                       "the literal word `object`");
    // '.object("id") ... .end/object' -> شكل جديد بأسلوب استدعاء دوال (انظر ObjectLiteralStmt في
    // rin_ast.h)؛ يُميَّز عن الشكل القديم '.object=type' بالتوكن التالي مباشرة: '(' هنا مقابل '='.
    if (check(TokenType::LPAREN)) return objectLiteralStatement(object);
    consume(TokenType::EQUAL, "Expected '=' after '.object'");
    if (match({TokenType::TEXT})) return textDeclaration();
    if (check(TokenType::DOT) && checkNext(TokenType::IDENT) && current + 2 < tokens.size() && tokens[current + 1].lexeme == "object") return objectFieldStatement();
    if (match({TokenType::LET})) return letDeclaration();
    if (match({TokenType::FUN})) return functionDeclaration();
    throw errRich(diag::Code::E0013_InvalidExpression, peek(),
                  "expected an object member type after `.object=` (`text`, `let`, or `fun`), or an id after `.object(`",
                  "after `.object=` the parser only recognizes three member forms: `text` (a text "
                  "field), `let` (a typed value field), or `fun` (a method) — nothing else is a valid "
                  "continuation here",
                  "pick one of `.object=text`, `.object=let`, or `.object=fun`, or switch to the "
                  "id-based form `.object(\"id\") ... .end/object` if you meant an object literal",
                  "`text`, `let`, or `fun`");
}

// .object("id")
//     field(value);      -> field:(value);  (typed، مطابق وظيفياً)     field();  (بلا وسيطة => nil)
//     container.();       -> اختياري: يربط الكائن بسجل عام قابل للوصول من أي مكان عبر نفس المعرّف
// .end/object
// يُستدعى بعد أن يكون objectFieldStatement() قد استهلك بالفعل '.' 'object' ورأى '(' التالية (لم
// تُستهلَك بعد). objectTok هو توكن 'object' نفسه (لتحديد سطر الفتح في رسائل الخطأ/consumeEndTag).
StmtPtr Parser::objectLiteralStatement(const Token& objectTok) {
    consume(TokenType::LPAREN, "Expected '(' after '.object'");
    Token idTok = consume(TokenType::STRING, "Expected a quoted id, e.g. .object(\"user01\")");
    consume(TokenType::RPAREN, "Expected ')' after the object id");
    auto stmt = std::make_shared<ObjectLiteralStmt>();
    stmt->id = idTok.lexeme;
    stmt->line = objectTok.line;
    while (!checkClosingTag() && !isAtEnd()) {
        // container.();  -> يربط الكائن الحالي بسجل عام (لا يُضيف حقلاً باسم "container")
        if (check(TokenType::CONTAINER) && checkNext(TokenType::DOT) &&
            current + 2 < tokens.size() && tokens[current + 2].type == TokenType::LPAREN) {
            advance(); // 'container'
            advance(); // '.'
            advance(); // '('
            consume(TokenType::RPAREN, "Expected ')' after 'container.('");
            consume(TokenType::SEMICOLON, "Expected ';' after 'container.();'");
            stmt->linkToContainer = true;
            continue;
        }
        Token fieldTok = consume(TokenType::IDENT,
            "Expected a field call (e.g. `name(\"...\");`) or `container.();` inside `.object(...)`");
        ObjectFieldCall field;
        field.name = fieldTok.lexeme;
        field.line = fieldTok.line;
        if (match({TokenType::COLON})) field.typed = true; // field:(value);  -> شكل مُنمَّط اختياري
        consume(TokenType::LPAREN, "Expected '(' after field name '" + fieldTok.lexeme + "'");
        if (!check(TokenType::RPAREN)) field.value = expression();
        consume(TokenType::RPAREN, "Expected ')' after value for field '" + fieldTok.lexeme + "'");
        consume(TokenType::SEMICOLON, "Expected ';' after field '" + fieldTok.lexeme + "'");
        stmt->fields.push_back(std::move(field));
    }
    consumeEndTag("object", objectTok.line, stmt->id);
    return stmt;
}

// view.print/object(expr);  -> انظر ViewPrintObjectStmt في rin_ast.h
// يُستدعى بعد أن يكون المستدعي (declaration()) قد استهلك بالفعل 'view' '.' 'print'.
StmtPtr Parser::viewPrintObjectStatement() {
    Token tok = previous(); // 'print'
    consume(TokenType::SLASH, "Expected '/' after 'view.print'");
    Token objTok = consume(TokenType::IDENT, "Expected 'object' after 'view.print/'");
    if (objTok.lexeme != "object")
        throw errRich(diag::Code::E0016_InvalidProperty, objTok, "expected 'object' after 'view.print/'",
                       "`view.print/` only supports one form, `view.print/object(expr)`, which prints "
                       "an object literal's fields; the word right after the '/' must be the literal "
                       "`object`",
                       "replace `" + objTok.lexeme + "` with `object`", "the literal word `object`");
    consume(TokenType::LPAREN, "Expected '(' after 'view.print/object'");
    auto stmt = std::make_shared<ViewPrintObjectStmt>();
    stmt->target = expression();
    stmt->line = tok.line;
    consume(TokenType::RPAREN, "Expected ')' after 'view.print/object(...)'");
    consume(TokenType::SEMICOLON, "Expected ';' after 'view.print/object(...)' statement");
    return stmt;
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

// print.log(...) / print.log.info(...) / print.log.warn(...) / print.log.error(...) / print.log.debug(...);
// كل ما بين القوسين (رسائل + سمات) مفصول بفواصل، بأي ترتيب بينها:
//   print.log.info("Server started");
//   print.log.info("multi", "values", sep="-", label="AUTH");
// السمات الاختيارية المدعومة (كل واحدة مرة على الأكثر): sep=expr | if=expr | label=expr | source=expr.
// يُستدعى بعد أن يكون statement() قد استهلك بالفعل 'print' '.' 'log'. printTok هو توكن 'print'
// نفسه (لتحديد سطر بداية العبارة في stmt->line — أدقّ من سطر '(' التالية عند تعدّد الأسطر).
// ملاحظة مهمة (نفس ملاحظة printStatement() أعلاه بالضبط): كل عنصر بين القوسين إما رسالة (قيمة
// عادية تُقرأ عبر pipeline() لا expression() الكاملة، لنفس السبب: تجنّب ابتلاع "sep=..." كتعبير
// إسناد كامل) أو سمة key=value — نميّز بينهما بالنظر خطوتين للأمام: IDENT (أو 'if' المحجوزة) تليها
// '=' مباشرة تُعتبر سمة، وإلا فهي رسالة عادية.
StmtPtr Parser::logStatement(const Token& printTok) {
    auto stmt = std::make_shared<LogStmt>();
    stmt->level = "log"; // افتراضي: print.log(...) العام بلا مستوى مسبق
    static const std::unordered_set<std::string> logLevels = {"info", "warn", "error", "debug"};
    // print.log.info/warn/error/debug(...) : '.' IDENT(level) إضافية قبل '(' — اختيارية تماماً.
    if (check(TokenType::DOT) && checkNext(TokenType::IDENT) && logLevels.count(tokens[current + 1].lexeme)) {
        advance(); // '.'
        stmt->level = advance().lexeme; // IDENT(level)
    }
    consume(TokenType::LPAREN, "Expected '(' after 'print.log" + (stmt->level == "log" ? std::string() : ("." + stmt->level)) + "'");
    static const std::unordered_set<std::string> logAttrs = {"sep", "label", "source"};
    std::unordered_set<std::string> seenAttrs;
    if (!check(TokenType::RPAREN)) {
        for (;;) {
            std::string attr;
            if (check(TokenType::IF) && checkNext(TokenType::EQUAL)) {
                attr = "if"; // 'if' كلمة محجوزة (TokenType::IF)، تماماً كما في printStatement() أعلاه
            } else if (check(TokenType::IDENT) && logAttrs.count(peek().lexeme) && checkNext(TokenType::EQUAL)) {
                attr = peek().lexeme;
            }
            if (!attr.empty()) {
                advance(); // استهلاك توكن اسم السمة
                if (seenAttrs.count(attr)) {
                    throw errRich(diag::Code::E0016_InvalidProperty, printTok,
                                   "'print.log': `" + attr + "` attribute repeated",
                                   "each attribute (`sep`, `label`, `source`, `if`) may only be set "
                                   "once per `print.log(...)` call; the parser found `" + attr +
                                   "=` a second time in the same call",
                                   "remove the duplicate `" + attr + "=...` — keep only the first "
                                   "occurrence, or the last one if that's the value you actually want");
                }
                seenAttrs.insert(attr);
                advance(); // '='
                ExprPtr value = expression();
                if (attr == "sep") stmt->sep = value;
                else if (attr == "if") stmt->ifCond = value;
                else if (attr == "label") stmt->label = value;
                else if (attr == "source") stmt->source = value;
            } else {
                stmt->exprs.push_back(pipeline());
            }
            if (!match({TokenType::COMMA})) break;
        }
    }
    consume(TokenType::RPAREN, "Expected ')' after 'print.log' arguments");
    consume(TokenType::SEMICOLON, "Expected ';' after 'print.log' statement");
    stmt->line = printTok.line;
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
    if (loopDepth == 0)
        throw errRich(diag::Code::E0011_UnexpectedToken, tok, "'break' used outside of a loop",
                       "`break` exits the innermost enclosing loop, so the parser only allows it "
                       "while it is currently inside a `while`/`for`/`rinopen` loop body; this "
                       "`break` appears at a point where no loop is open",
                       "move this `break` inside a loop body, or remove it if it was left over from "
                       "refactoring");
    consume(TokenType::SEMICOLON, "Expected ';' after 'break'");
    auto stmt = std::make_shared<BreakStmt>();
    stmt->line = tok.line;
    return stmt;
}

// continue; -> نفس قيد break: صالحة فقط داخل جسم حلقة while.
StmtPtr Parser::continueStatement() {
    Token tok = previous();
    if (loopDepth == 0)
        throw errRich(diag::Code::E0011_UnexpectedToken, tok, "'continue' used outside of a loop",
                       "`continue` skips to the next iteration of the innermost enclosing loop, so "
                       "the parser only allows it while it is currently inside a `while`/`for`/"
                       "`rinopen` loop body; this `continue` appears at a point where no loop is open",
                       "move this `continue` inside a loop body, or remove it if it was left over "
                       "from refactoring");
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
                                     w == "object" || w == "portal" || w == "block" || w == "aukt" || w == "open" ||
                                     w == "chatbot" || w == "everything" || w == "make");
        }
        // لا نستهلك '.' إلا إذا كانت متبوعة مباشرة بإحدى هذه الكلمات، وإلا فقد تكون في الحقيقة
        // بداية وسم إغلاق آخر مجاور مثل '.end/container' تلاه '.end/Containers.Group'
        if (nextIsPipe || nextIsContextualWord) {
            advance(); // consume '.'
            Token sub = advance(); // consume 'pipe' / 'data' / 'api' / 'import'
            tag = "container." + sub.lexeme;
            if (sub.lexeme == "open" && match({TokenType::SLASH})) {
                Token kind = consume(TokenType::IDENT, "Expected 'object' after 'container.open/'");
                if (kind.lexeme != "object")
                    throw errRich(diag::Code::E0016_InvalidProperty, kind,
                                   "expected 'object' after 'container.open/'",
                                   "`container.open/` currently only supports one kind, "
                                   "`container.open/object`, for opening an object-backed container",
                                   "replace `" + kind.lexeme + "` with `object`",
                                   "the literal word `object`");
                tag = "container.open/object";
            }
        }
    } else if (first.type == TokenType::IDENT && first.lexeme == "Rin" && check(TokenType::DOT) &&
               current + 1 < tokens.size() && tokens[current + 1].type == TokenType::IDENT &&
               tokens[current + 1].lexeme == "make") {
        // 'Rin.make' -> شكل مساحة-اسم (namespace) اختياري لنفس @make/@Everything (انظر ContainerKind::
        // EVERYTHING في rin_ast.h)، بنفس أسلوب 'Containers.Group' أعلاه لكن بلا كلمة محجوزة عالمياً:
        // 'Rin' تبقى معرّفاً عادياً في أي مكان آخر (اسم متغير مثلاً)، ولا تتحوّل لهذا الشكل الخاص إلا
        // عند ظهورها بالضبط كـ 'Rin' '.' 'make' مباشرة بعد '@' أو '.end'.
        advance(); // consume '.'
        advance(); // consume 'make'
        tag = "Rin.make";
    }
    return tag;
}

std::string Parser::readOptionalName(const std::string& context) {
    if (!match({TokenType::EQUAL})) return "";
    if (check(TokenType::IDENT) || check(TokenType::STRING)) return advance().lexeme;

    // الاسم بعد '=' اختياري بالكامل — أي أن '=' نفسه لم يكن مُجبَراً على الظهور هنا. لذا وصول
    // القارئ إلى هذه النقطة يعني أن المبرمج كتب '=' فعلاً وقصد إعطاء اسماً، لكن ما تبعه ليس
    // معرّفاً (IDENT) ولا سلسلة نصية (STRING) — أشهر سبب: نسي كتابة الاسم فانتقل مباشرة لبقية
    // الجملة (';' أو نهاية السطر)، أو وضع رقماً/رمزاً بدل اسم صريح.
    const Token& bad = peek();
    std::string ctx = context.empty() ? "this" : context;
    diag::Diagnostic d(diag::Code::E0012_MissingToken,
                        "expected a name after '=' in " + ctx, locOf(bad));
    d.expected = "an identifier (e.g. `MyName`) or a string literal (e.g. `\"My Name\"`)";
    d.found = (bad.type == TokenType::END_OF_FILE) ? "end of file"
              : (bad.lexeme.empty() ? "an empty token" : ("`" + bad.lexeme + "`"));
    d.withReason("here '=' is being used to give a name to " + ctx +
                 "; once the parser sees '=' in this position it must be followed immediately "
                 "by exactly one identifier or one quoted string that becomes the name — "
                 "nothing else is a valid name token");
    if (bad.type == TokenType::SEMICOLON) {
        d.withHint("either remove the trailing '=' if " + ctx + " does not need a name, "
                   "or put the name right after it, e.g. `=MyName;` instead of `=;`");
    } else if (bad.type == TokenType::DOT || bad.type == TokenType::AT || bad.type == TokenType::END_OF_FILE) {
        d.withHint("either remove the trailing '=' if " + ctx + " does not need a name, "
                   "or add a name directly after it (no space issue — the token itself is "
                   "missing), e.g. `=MyName`");
    } else if (bad.type == TokenType::NUMBER) {
        d.withHint("names cannot start with a digit — wrap it in quotes to use it as a string "
                   "name instead, e.g. `=\"" + bad.lexeme + "\"`, or start the identifier with a "
                   "letter, e.g. `=Item" + bad.lexeme + "`");
    } else {
        d.withHint("a name after '=' must be a plain identifier (letters, digits, '_' — not "
                   "starting with a digit) or a double-quoted string; `" + bad.lexeme +
                   "` is neither, so wrap it in quotes if it's meant to be a literal name, e.g. "
                   "`=\"" + bad.lexeme + "\"`");
    }
    throw RinError(std::move(d));
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
    std::string closingName = readOptionalName("a closing tag (`.end/" + expectedTag + "=name`)");
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
    std::string name = readOptionalName("a `@view." + kindTok.lexeme + "` strand");

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
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected an attribute (key=value;), a nested '@view...', or '.end/view' inside @view." + s->kindTag,
                      "inside an `@view." + s->kindTag + "` body, the parser only accepts an "
                      "`attribute=value;` line, a nested `@view...` strand, or the closing "
                      "`.end/view` tag; the current token starts none of these",
                      "add an `attribute=value;` line, open a nested `@view.<kind>`, or close this "
                      "strand with `.end/view;`",
                      "an attribute, '@view', or '.end/view'");
    }
    consumeEndTag("view", viewTok.line, name);
    return s;
}

std::shared_ptr<ViewStmt> Parser::elementDeclaration() {
    Token openTok = previous();
    consume(TokenType::DOT, "Expected '.' after '@element'");
    if (isAtEnd() || peek().lexeme.empty() || peek().type == TokenType::DOT || peek().type == TokenType::EQUAL || peek().type == TokenType::SEMICOLON)
        throw errRich(diag::Code::E0012_MissingToken, peek(), "Expected an element kind after '@element.'",
                      "`@element.` must be followed immediately by the kind of UI element being "
                      "declared (e.g. `text`, `button`, `file`) — the parser found something else "
                      "(or nothing) right after the '.'",
                      "add an element kind after the '.', e.g. `@element.text` or `@element.button`",
                      "an element kind identifier");
    Token kindTok = advance(); // accepts reserved Rin words such as `text`/`file` as element kinds
    auto s = std::make_shared<ViewStmt>();
    s->name = readOptionalName("an `@element." + kindTok.lexeme + "`");
    s->kindTag = kindTok.lexeme;
    s->role = UiRole::ELEMENT;
    s->line = openTok.line;
    while (!checkClosingTag() && !isAtEnd()) {
        if (check(TokenType::AT) && checkNext(TokenType::IDENT) && tokens[current + 1].lexeme == "element") {
            advance(); advance(); s->children.push_back(elementDeclaration()); continue;
        }
        if (!check(TokenType::AT) && checkNext(TokenType::EQUAL)) {
            Token key = advance(); consume(TokenType::EQUAL, "Expected '=' after element attribute");
            ExprPtr val = expression(); consume(TokenType::SEMICOLON, "Expected ';' after element attribute");
            s->attrs.push_back({key.lexeme, val, key.line}); continue;
        }
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected an element attribute, nested '@element...', or '.end/element'",
                      "inside an `@element` body, the parser only accepts three things: an "
                      "`attribute=value;` line, a nested `@element...` child, or the closing "
                      "`.end/element` tag; the current token starts none of these",
                      "add an `attribute=value;` line, open a nested `@element.<kind>`, or close "
                      "this element with `.end/element;`",
                      "an attribute, '@element', or '.end/element'");
    }
    consumeEndTag("element", openTok.line, s->name);
    return s;
}

std::shared_ptr<ViewStmt> Parser::loopCanvasDeclaration() {
    Token openTok = previous();
    auto s = std::make_shared<ViewStmt>();
    s->role = UiRole::LOOP; s->line = openTok.line;
    if (check(TokenType::DOT)) {
        advance();
        Token kindTok = consume(TokenType::IDENT, "Expected a canvas layout kind after '@loop.'");
        s->kindTag = kindTok.lexeme;
        s->name = readOptionalName("a `@loop." + kindTok.lexeme + "` canvas");
    } else {
        s->kindTag = "Column";
        s->name = readOptionalName("a `@loop` canvas");
    }
    while (!checkClosingTag() && !isAtEnd()) {
        if (check(TokenType::AT) && checkNext(TokenType::IDENT) && tokens[current + 1].lexeme == "element") {
            advance(); advance(); s->children.push_back(elementDeclaration()); continue;
        }
        if (check(TokenType::AT) && checkNext(TokenType::IDENT) && tokens[current + 1].lexeme == "loop") {
            advance(); advance(); s->children.push_back(loopCanvasDeclaration()); continue;
        }
        if (!check(TokenType::AT) && checkNext(TokenType::EQUAL)) {
            Token key = advance(); consume(TokenType::EQUAL, "Expected '=' after loop attribute");
            ExprPtr val = expression(); consume(TokenType::SEMICOLON, "Expected ';' after loop attribute");
            s->attrs.push_back({key.lexeme, val, key.line}); continue;
        }
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected a loop canvas attribute, nested element/loop, or '.end/loop'",
                      "inside an `@loop` body, the parser only accepts an `attribute=value;` line, "
                      "a nested `@element...`/`@loop...` child, or the closing `.end/loop` tag; the "
                      "current token starts none of these",
                      "add an `attribute=value;` line, open a nested `@element`/`@loop`, or close "
                      "this loop canvas with `.end/loop;`",
                      "an attribute, '@element'/'@loop', or '.end/loop'");
    }
    consumeEndTag("loop", openTok.line, s->name);
    return s;
}

StmtPtr Parser::uiBindingStatement() {
    Token onTok = advance();
    consume(TokenType::DOT, "Expected '.' after 'on'");
    Token target = consume(TokenType::IDENT, "Expected element name after 'on.'");
    consume(TokenType::DOT, "Expected '.' before event name");
    Token event = consume(TokenType::IDENT, "Expected event name after element name");
    consume(TokenType::EQUAL, "Expected '=' after event binding");
    ExprPtr handler = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after event binding");
    auto s = std::make_shared<UiBindingStmt>();
    s->target = target.lexeme; s->event = event.lexeme; s->handler = handler; s->line = onTok.line;
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

// Rin Loom: Theme (Pattern Book) declaration -------------------------------------------
// @theme=<Name>  key=expr; ...  .end/theme
// يُستدعى بعد أن يكون declaration() قد استهلك بالفعل '@' و'theme'، بنفس أسلوب viewDeclaration()
// أعلاه تماماً لكن بلا كتل متداخلة (Theme مسطّح: أدوار لونية + العلم الاختياري 'active' فقط).
StmtPtr Parser::themeDeclaration() {
    Token themeTok = previous(); // 'theme'
    consume(TokenType::EQUAL, "Expected '=' after '@theme' (e.g. @theme=Midnight ... .end/theme)");
    Token nameTok = consume(TokenType::IDENT, "Expected a theme name after '@theme='");

    auto s = std::make_shared<ThemeStmt>();
    s->name = nameTok.lexeme;
    s->line = themeTok.line;

    while (!checkClosingTag() && !isAtEnd()) {
        if (!check(TokenType::AT) && checkNext(TokenType::EQUAL)) {
            Token key = advance();
            consume(TokenType::EQUAL, "Expected '=' after attribute key '" + key.lexeme +
                                       "' inside @theme=" + s->name);
            ExprPtr val = expression();
            consume(TokenType::SEMICOLON, "Expected ';' after value for attribute '" + key.lexeme + "'");
            ViewAttr a; a.key = key.lexeme; a.value = val; a.line = key.line;
            s->attrs.push_back(a);
            continue;
        }
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected an attribute (key=value;) or '.end/theme' inside @theme=" + s->name,
                      "the body of a `@theme` block may only contain `key=value;` attribute lines "
                      "until it is closed; the parser found a token here that starts neither an "
                      "attribute (an identifier followed by '=') nor the closing tag",
                      "add `key=value;` for the attribute you meant, or close the block with "
                      "`.end/theme" + (s->name.empty() ? "" : ("=" + s->name)) + ";` if you're done",
                      "an identifier followed by '=', or '.end/theme'");
    }
    consumeEndTag("theme", themeTok.line, s->name);
    return s;
}

static bool isMakeWord(const std::string& w) {
    return w == "kind" || w == "use" || w == "need" || w == "allow" || w == "deny" ||
           w == "input" || w == "output" || w == "public" || w == "private" ||
           w == "version" || w == "description" || w == "strict";
}

static void pushUnique(std::vector<std::string>& v, const std::string& x) {
    if (std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x);
}

// RCS-1.0 §3.13 Security — الكلمات الأربع + strict/version/description معمَّمة الآن لأي
// @container (لا فقط @make.(name)). عمداً بلا "kind"/"input"/"output"/"public"/"private":
// تلك تبقى خاصة بـ Make Unit فقط في هذه المرحلة (Phase 0 لا تغيّر إلا Security).
static bool isGeneralPolicyWord(const std::string& w) {
    return w == "use" || w == "need" || w == "allow" || w == "deny" ||
           w == "strict" || w == "version" || w == "description";
}

bool Parser::tryParsePolicyDirective(ContainerStmt& s) {
    if (!check(TokenType::IDENT)) return false;
    std::string word = peek().lexeme;
    if (!isGeneralPolicyWord(word)) return false;

    size_t savedPos = current; // نقطة رجوع كاملة إن لم يطابق الشكل المتوقع بالضبط
    advance(); // استهلاك كلمة التوجيه نفسها

    if (word == "strict") {
        if (!check(TokenType::SEMICOLON)) { current = savedPos; return false; }
        advance();
        s.strict = true;
        s.hasPolicy = true;
        return true;
    }
    if (word == "version" || word == "description") {
        if (!check(TokenType::STRING)) { current = savedPos; return false; }
        Token v = advance();
        if (!check(TokenType::SEMICOLON)) { current = savedPos; return false; }
        advance();
        if (word == "version") s.version = v.lexeme; else s.description = v.lexeme;
        s.hasPolicy = true;
        return true;
    }
    // use/need/allow/deny <capability-name> ; — نفس الاستثناء الموجود في makeUnitBlock: اسم
    // القدرة قد يكون كلمة محجوزة في Rin (container/loop/function...) لا معرِّفاً عادياً.
    bool okTok = check(TokenType::IDENT) || check(TokenType::CONTAINER) || check(TokenType::FOR) ||
                 check(TokenType::WHILE) || check(TokenType::FUN) || check(TokenType::RETURN) ||
                 check(TokenType::PRINT) || check(TokenType::TEXT) || check(TokenType::PIPE_KW);
    if (!okTok) { current = savedPos; return false; }
    Token value = advance();
    if (!check(TokenType::SEMICOLON)) { current = savedPos; return false; }
    advance();
    std::vector<std::string>* dst = nullptr;
    if (word == "use") dst = &s.uses;
    else if (word == "need") dst = &s.needs;
    else if (word == "allow") dst = &s.allows;
    else dst = &s.denies;
    pushUnique(*dst, value.lexeme);
    s.hasPolicy = true;
    return true;
}

// نفس مجموعة الوسوم التي تُنتج ContainerStmt أدناه (container/pipe/data/api/.../make) —
// فقط هذه تخضع لتوجيهات policy_block الجديدة؛ Containers.Group/Volume تبقى كما هي (خارج
// نطاق هذه المرحلة، لا حاجة/طلب لها هناك حالياً).
static bool isContainerFamilyTag(const std::string& tag) {
    static const std::unordered_set<std::string> s = {
        "container", "container.pipe", "pipe", "container.data", "data",
        "container.api", "api", "container.import",
        "container.table", "table", "container.doc", "doc",
        "container.object", "Object", "container.open/object",
        "container.portal", "portal", "container.block", "block",
        "container.sticker", "sticker", "container.aukt", "AUKT",
        "container.chatbot", "chatbot",
        "container.everything", "Everything", "container.make", "make", "Rin.make"
    };
    return s.count(tag) != 0;
}

StmtPtr Parser::makeUnitBlock() {
    Token atTok = consume(TokenType::AT, "Expected '@' before '@make.(name)'");
    Token makeTok = consume(TokenType::IDENT, "Expected 'make' after '@'");
    if (makeTok.lexeme != "make")
        throw errRich(diag::Code::E0015_UnknownContainer, makeTok, "expected `make` in `@make.(name)`",
                       "a Make Unit block always opens with the literal word `make` right after '@', "
                       "as in `@make.(UnitName)` — this token is not that keyword",
                       "replace `" + makeTok.lexeme + "` with `make`", "the literal word `make`");
    consume(TokenType::DOT, "Expected '.' after '@make'");
    consume(TokenType::LPAREN, "Expected '(' after '@make.'");
    Token nameTok = consume(TokenType::IDENT, "Expected a unit name inside '@make.(name)'");
    consume(TokenType::RPAREN, "Expected ')' after Make Unit name");

    auto s = std::make_shared<MakeStmt>();
    s->name = nameTok.lexeme;
    s->line = atTok.line;
    s->kind = ContainerKind::EVERYTHING;

    while (!checkClosingTag() && !isAtEnd()) {
        if (check(TokenType::IDENT)) {
            std::string word = peek().lexeme;
            if (isMakeWord(word)) {
                advance();
                if (word == "strict") {
                    consume(TokenType::SEMICOLON, "Expected ';' after 'strict'");
                    s->strict = true;
                    continue;
                }
                if (word == "kind") {
                    Token v = consume(TokenType::IDENT, "Expected a Make type after 'kind'");
                    s->makeType = v.lexeme;
                    consume(TokenType::SEMICOLON, "Expected ';' after 'kind'");
                    continue;
                }
                if (word == "version" || word == "description") {
                    Token v = consume(TokenType::STRING, "Expected a string after Make metadata field");
                    if (word == "version") s->version = v.lexeme;
                    else s->description = v.lexeme;
                    consume(TokenType::SEMICOLON, "Expected ';' after Make metadata");
                    continue;
                }
                // Capability names may themselves be Rin keywords (e.g. `container`, `loop`, `function`).
                // Accept their lexeme without reserving these words globally.
                if (!(check(TokenType::IDENT) || check(TokenType::CONTAINER) || check(TokenType::FOR) ||
                      check(TokenType::WHILE) || check(TokenType::FUN) || check(TokenType::RETURN) ||
                      check(TokenType::PRINT) || check(TokenType::TEXT) || check(TokenType::PIPE_KW))) {
                    throw errRich(diag::Code::E0012_MissingToken, peek(),
                                  "Expected a capability or name after Make directive",
                                  "`" + word + "` inside a `@make` block must be followed by exactly "
                                  "one capability/name token (an identifier, or one of the reserved "
                                  "words that are also valid capability names, e.g. `container`, "
                                  "`loop`, `function`)",
                                  "add a single capability or name token right after `" + word + "`",
                                  "a capability or name token");
                }
                Token value = advance();
                std::vector<std::string>* dst = nullptr;
                if (word == "use") dst = &s->uses;
                else if (word == "need") dst = &s->needs;
                else if (word == "allow") dst = &s->allows;
                else if (word == "deny") dst = &s->denies;
                else if (word == "input") dst = &s->inputs;
                else if (word == "output") dst = &s->outputs;
                else if (word == "public") dst = &s->publics;
                else if (word == "private") dst = &s->privates;
                pushUnique(*dst, value.lexeme);
                consume(TokenType::SEMICOLON, "Expected ';' after Make directive");
                continue;
            }
        }
        s->body.push_back(declaration());
    }

    consumeEndTag("make", atTok.line, s->name);
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
        "container.sticker", "sticker", "container.aukt", "AUKT", "container.chatbot", "chatbot",
        // make (اسمها الرسمي الحالي؛ سابقاً "Everything"): المفهوم الجامع — بلا أي قيود على الجسم
        // (تماماً كـ container/AUKT)، يستدعي/يفوّض إلى نفس آلية container القياسية حرفياً. ثلاث صيغ
        // مكافئة تماماً تُنتج نفس ContainerKind::EVERYTHING: "make" (الأقصر)، "Rin.make" (بمساحة اسم
        // صريحة)، و"container.make" (بنفس أسلوب بقية container.xxx). "Everything"/"container.everything"
        // ما زالتا مقبولتين كاسمين قديمين (alias) للتوافق العكسي فقط. انظر التوثيق الكامل أعلى
        // ContainerStmt في rin_ast.h.
        "container.everything", "Everything", "container.make", "make", "Rin.make",
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
                                    "container.chatbot, container.make (or make / Rin.make / the legacy "
                                    "container.everything / Everything), Containers.Group, or Volume");
        }
        throw d;
    }
    std::string name = readOptionalName("a `@" + tag + "` block");
    std::vector<StmtPtr> body;
    // RCS-1.0 §7 Phase 0: قبل كل عبارة عادية، نجرّب أولاً قراءتها كتوجيه سياسة (policy_block)
    // إن كانت هذه حاوية من عائلة container (لا Containers.Group/Volume). tryParsePolicyDirective
    // يستعيد موضع القارئ بنفسه إن لم يطابق النمط، فلا خطر على أي برنامج قديم يستخدم هذه الكلمات
    // كأسماء عادية. الحقول المُجمَّعة هنا تُطبَّق على ContainerStmt بعد إنشائها أدناه.
    ContainerStmt policyAccum;
    bool familyTag = isContainerFamilyTag(tag);
    while (!checkClosingTag() && !isAtEnd()) {
        if (familyTag && tryParsePolicyDirective(policyAccum)) continue;
        body.push_back(declaration());
    }
    consumeEndTag(tag, atTok.line, name);

    if (tag == "container" || tag == "container.pipe" || tag == "pipe" || tag == "container.data" || tag == "data" ||
        tag == "container.api" || tag == "api" || tag == "container.import" ||
        tag == "container.table" || tag == "table" ||
        tag == "container.doc" || tag == "doc" ||
        tag == "container.object" || tag == "Object" || tag == "container.open/object" ||
        tag == "container.portal" || tag == "portal" ||
        tag == "container.block" || tag == "block" ||
        tag == "container.sticker" || tag == "sticker" ||
        tag == "container.aukt" || tag == "AUKT" ||
        tag == "container.chatbot" || tag == "chatbot" ||
        tag == "container.everything" || tag == "Everything" ||
        tag == "container.make" || tag == "make" || tag == "Rin.make") {
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
        if (policyAccum.hasPolicy) {
            s->uses = policyAccum.uses; s->needs = policyAccum.needs;
            s->allows = policyAccum.allows; s->denies = policyAccum.denies;
            s->version = policyAccum.version; s->description = policyAccum.description;
            s->strict = policyAccum.strict; s->hasPolicy = true;
        }
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
        else if (tag == "container.chatbot" || tag == "chatbot") s->kind = ContainerKind::CHATBOT;
        else if (tag == "container.everything" || tag == "Everything" ||
                 tag == "container.make" || tag == "make" || tag == "Rin.make") s->kind = ContainerKind::EVERYTHING;
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
        // RCS-1.0 §3.2/§3.3: خُطّافات دورة الحياة (`on init/mount/update/destroy/error`) وحقول
        // الحالة (`state`) هما سلوك، لا بيانات نقية — نفس منطق حظر `fun` أعلاه بالضبط، ونفس
        // السبب: `container.data`/`container.table`/`table`/... يجب أن تبقى قابلة للتسلسل
        // (serializable) بالكامل بلا أي دوال مرفقة.
        if (std::dynamic_pointer_cast<LifecycleHookStmt>(st)) {
            auto d = errAtLine(diag::Code::E0014_InvalidContainer, st->line,
                               "lifecycle hooks ('on init/mount/update/destroy/error') are not allowed inside `container.data`/`container.table`/`table`");
            d.diagnostic->withReason("`container.data` containers must stay pure data (serializable)")
             .withHint("use `container` or `container.pipe` for logic instead");
            throw d;
        }
        if (std::dynamic_pointer_cast<StateDeclStmt>(st)) {
            auto d = errAtLine(diag::Code::E0014_InvalidContainer, st->line,
                               "'state' fields are not allowed inside `container.data`/`container.table`/`table`");
            d.diagnostic->withReason("`container.data` containers must stay pure data (serializable)")
             .withHint("use `container` or `container.pipe` for reactive state instead");
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
    std::string name = readOptionalName("a `Section` block");
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
    if (langKey.lexeme != "lang")
        throw errRich(diag::Code::E0016_InvalidProperty, langKey,
                       "expected 'lang' attribute after 'translation'",
                       "a `translation` statement always starts with `lang=\"...\"` followed by "
                       "`text=\"...\"`; the first attribute name must be the literal word `lang`",
                       "replace `" + langKey.lexeme + "` with `lang`, e.g. "
                       "`translation lang=\"en\" text=\"Hello\";`",
                       "the literal word `lang`");
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
        throw errRich(diag::Code::E0016_InvalidProperty, key,
                       "expected 'to' or 'id' attribute after 'link'",
                       "a `link` statement takes exactly one attribute: `to=name` (link by container "
                       "name) or `id=\"...\"` (link by global id) — `" + key.lexeme + "` is neither",
                       "replace `" + key.lexeme + "` with `to` or `id`, e.g. `link to=Other;` or "
                       "`link id=\"other-id\";`",
                       "`to` or `id`");
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
            throw errRich(diag::Code::E0012_MissingToken, peek(),
                          "expected a container name after 'to='",
                          "`link to=` must be followed by the target container's name — either a "
                          "plain identifier or a quoted string",
                          "add the target container's name right after 'to=', e.g. `link to=Other;`",
                          "an identifier or string literal");
        s->target = advance().lexeme;
    }
    consume(TokenType::SEMICOLON, "Expected ';' after link statement");
    return s;
}

StmtPtr Parser::tyingStatement() {
    Token tok = previous();
    Token key = consume(TokenType::IDENT, "Expected 'with' after 'tying'");
    if (key.lexeme != "with")
        throw errRich(diag::Code::E0016_InvalidProperty, key,
                       "expected 'with' attribute after 'tying'",
                       "a `tying` statement only takes the attribute `with=name`, naming the "
                       "container it ties to",
                       "replace `" + key.lexeme + "` with `with`, e.g. `tying with=Other;`",
                       "the literal word `with`");
    consume(TokenType::EQUAL, "Expected '=' after 'with'");
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected a container name after 'with='",
                      "`tying with=` must be followed by the target container's name — either a "
                      "plain identifier or a quoted string",
                      "add the target container's name right after 'with=', e.g. `tying with=Other;`",
                      "an identifier or string literal");
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
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected a container name after 'with='",
                      "`merge with=` must be followed by the container's name to merge with — "
                      "either a plain identifier or a quoted string",
                      "add the target container's name right after 'with=', e.g. `merge with=Other;`",
                      "an identifier or string literal");
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
    if (!check(TokenType::IDENT))
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected a format name after 'format=' (e.g. png, zip)",
                      "`format=` must be followed by a plain identifier naming the output format — "
                      "it is not quoted like other names in this language",
                      "add a format name right after 'format=', e.g. `format=zip;` "
                      "(remove any quotes if you wrote `format=\"zip\"`)",
                      "an identifier (unquoted format name)");
    return advance().lexeme;
}

StmtPtr Parser::installationStatement(bool simplifiedFlag) {
    Token tok = previous();
    if (!check(TokenType::IDENT) && !check(TokenType::STRING))
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "expected a name after 'installation'",
                      "an `installation` statement must name its target right after the keyword — "
                      "either a plain identifier or a quoted string",
                      "add the target name right after 'installation', e.g. `installation MyApp;`",
                      "an identifier or string literal");
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
    if (key.lexeme != "cells")
        throw errRich(diag::Code::E0016_InvalidProperty, key,
                       "expected 'cells' attribute after 'row' (e.g. row cells=[1, 2, 3];)",
                       "a `row` statement only takes the attribute `cells=[...]`, listing the row's "
                       "values",
                       "replace `" + key.lexeme + "` with `cells`, e.g. `row cells=[1, 2, 3];`",
                       "the literal word `cells`");
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
    if (key.lexeme != "value")
        throw errRich(diag::Code::E0016_InvalidProperty, key,
                       "expected 'value' attribute after 'style' (e.g. style value=\"style://dark\";)",
                       "a `style` statement only takes the attribute `value=...`, naming the style "
                       "to apply",
                       "replace `" + key.lexeme + "` with `value`, e.g. `style value=\"style://dark\";`",
                       "the literal word `value`");
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
    if (idKey.lexeme != "id")
        throw errRich(diag::Code::E0016_InvalidProperty, idKey,
                       "expected 'id' attribute after 'document' (e.g. document id=\"u1\" fields={...};)",
                       "a `document` statement must start with `id=...` naming the document's id, "
                       "followed by `fields={...}`",
                       "replace `" + idKey.lexeme + "` with `id`, e.g. `document id=\"u1\" fields={...};`",
                       "the literal word `id`");
    consume(TokenType::EQUAL, "Expected '=' after 'id'");
    ExprPtr idExpr = expression();
    Token fieldsKey = consume(TokenType::IDENT, "Expected 'fields' after 'document id=...'");
    if (fieldsKey.lexeme != "fields")
        throw errRich(diag::Code::E0016_InvalidProperty, fieldsKey,
                       "expected 'fields' attribute after 'document id=...' (e.g. document id=\"u1\" fields={...};)",
                       "after `document id=...`, the next attribute must be `fields={...}` giving "
                       "the document's field/value map",
                       "replace `" + fieldsKey.lexeme + "` with `fields`, e.g. "
                       "`document id=\"u1\" fields={ name: \"Ali\" };`",
                       "the literal word `fields`");
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
    if (key.lexeme != "path")
        throw errRich(diag::Code::E0016_InvalidProperty, key,
                       "expected 'path' attribute after 'file'",
                       "a `file` statement only takes the attribute `path=...`, naming the file path",
                       "replace `" + key.lexeme + "` with `path`, e.g. `file path=\"data.txt\";`",
                       "the literal word `path`");
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
    throw errRich(diag::Code::E0011_UnexpectedToken, peek(),
                  "'simplified' must be followed by 'installation' or 'save'",
                  "`simplified` is a modifier that only makes sense in front of `installation` or "
                  "`save`, selecting their shortened output form; it cannot stand before anything "
                  "else",
                  "follow `simplified` with `installation ...;` or `save ...;`, or remove "
                  "`simplified` if you didn't mean to use the shortened form",
                  "'installation' or 'save'");
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
        throw errRich(diag::Code::E0003_InvalidAssignment, eq, "invalid assignment target",
                      "only a plain variable (`x = ...`) or an index expression (`x[i] = ...`) can "
                      "appear on the left of '='; the expression the parser built for the left-hand "
                      "side here is neither",
                      "assign to a variable or an index expression instead, e.g. `x = value;` or "
                      "`x[0] = value;`",
                      "a variable name or an index expression");
    }
    return expr;
}

// مُشغّل الأنابيب |> : يسمح ببناء خطوط أنابيب بيانات/إحصاء بشكل قابل للقراءة
//   input |> step1() |> step2(arg)   يُكافئ   step2(step1(input), arg)
// القيمة الموجودة على يسار |> تُمرَّر دائماً كأول وسيط للنداء الموجود على اليمين.
// يمكن كتابة اسم دالة بدون أقواس على يمين |> فتُعامل كنداء بلا وسائط إضافية: data |> mean
ExprPtr Parser::pipeline() {
    auto expr = logicOr();
    int stageCount = 0; // عدد مراحل |> في هذه السلسلة (لأجل RinFlow -- انظر CallExpr::pipelineStageCount)
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
                throw errRich(diag::Code::E0013_InvalidExpression, opTok,
                              "expected a function call (e.g. 'step()') after '|>'",
                              "'|>' pipes its left-hand value into a function call on the right, so "
                              "the right side must be either a call like `step()` or a bare function "
                              "name like `step` (treated as a no-argument call); the expression here "
                              "is neither",
                              "put a function name or call on the right of '|>', e.g. `x |> step()` "
                              "or `x |> step`",
                              "a function call or function name");
            }
        }

        auto piped = std::make_shared<CallExpr>();
        piped->callee = callExpr->callee;
        piped->line = opTok.line;
        piped->args.push_back(expr); // القيمة القادمة من يسار |> تصبح أول وسيط
        for (auto& a : callExpr->args) piped->args.push_back(a);
        piped->isPipelineNode = true; // انظر rin_ast.h: CallExpr::isPipelineNode
        expr = piped;
        stageCount++;
    }
    // فقط الحلقة الأخيرة (جذر السلسلة الكاملة) تحمل isPipelineRoot/pipelineStageCount؛ هذا لا يغيّر
    // شيئاً في التنفيذ العادي (evaluate() العادية تتجاهل هذين الحقلين تماماً)، ويُستخدَم حصراً من
    // RinFlowEngine عند تشغيل البرنامج كـ Flow (انظر Interpreter::runProgramAsFlow في rin_interpreter.cpp).
    if (stageCount > 0) {
        if (auto rootCall = std::dynamic_pointer_cast<CallExpr>(expr)) {
            rootCall->isPipelineRoot = true;
            rootCall->pipelineStageCount = stageCount;
        }
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
            if (!var)
                throw errRich(diag::Code::E0013_InvalidExpression, previous(),
                              "only functions can be called",
                              "'(' here is being parsed as a call, but a call's callee must be a "
                              "plain name (e.g. `foo()`); the expression right before this '(' is not "
                              "a plain name, so it cannot be called",
                              "call a plain function name instead, or remove the '(' if it wasn't "
                              "meant to be a call",
                              "a function name before '('");
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
                    throw errRich(diag::Code::E0013_InvalidExpression, peek(),
                                  "expected a key (name or string) in map literal",
                                  "each entry in a `{ ... }` map literal starts with a key, which "
                                  "must be a plain identifier or a quoted string, followed by ':' and "
                                  "the value",
                                  "use a plain identifier or a quoted string as the key, e.g. "
                                  "`{ name: \"Ali\" }` or `{ \"name\": \"Ali\" }`",
                                  "an identifier or string literal");
                }
                consume(TokenType::COLON, "Expected ':' after map key");
                auto valueExpr = expression();
                map->entries.push_back({keyExpr, valueExpr});
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RBRACE, "Expected '}' after map entries");
        return map;
    }
    throw errRich(diag::Code::E0013_InvalidExpression, peek(), "expected expression",
                  "the parser reached a point where a value is required (a literal, variable, "
                  "parenthesized expression, list `[...]`, or map `{...}`) but the current token "
                  "cannot start any of those",
                  "check for a missing operand — a stray operator, an extra comma, or an unmatched "
                  "closing bracket right before this token are the most common causes",
                  "a literal, variable, '(', '[', or '{'");
}

} // namespace rin

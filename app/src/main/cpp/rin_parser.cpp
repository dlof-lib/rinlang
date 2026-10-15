#include "rin_parser.h"
#include "diagnostics/diagnostic_engine.h"
#include "diagnostics/source_manager.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <functional>

namespace rin {

Parser::Parser(std::vector<Token> toks, std::string filename) : tokens(std::move(toks)), file(std::move(filename)) {}

bool Parser::isAtEnd() const { return peek().type == TokenType::END_OF_FILE; }
const Token& Parser::peek() const { return tokens[current]; }
const Token& Parser::previous() const { return tokens[current - 1]; }
const Token& Parser::advance() { if (!isAtEnd()) current++; return previous(); }
bool Parser::check(TokenType type) const { return !isAtEnd() && peek().type == type; }

// Rin For+: هل عند الموضع i معامل مركّب `op=` أو `++`/`--` ملتصقان (يُعالَجان في assignment() لا في term/factor)؟
// kind: 1 = op=  ، 2 = ++/-- متبوعة بنهاية تعبير ( ) ; , } { أو نهاية الملف ). المعامل التالي المسافة (a - -b) لا يُلتقط.
static int rinCompoundAhead(const std::vector<Token>& t, size_t i) {
    if (i + 1 >= t.size()) return 0;
    const Token& a = t[i]; const Token& b = t[i + 1];
    if (a.line != b.line || a.endCol != b.col) return 0;
    if (b.type == TokenType::EQUAL &&
        (a.type == TokenType::PLUS || a.type == TokenType::MINUS || a.type == TokenType::STAR || a.type == TokenType::SLASH || a.type == TokenType::PERCENT))
        return 1;
    if (a.type == b.type && (a.type == TokenType::PLUS || a.type == TokenType::MINUS)) {
        TokenType n = i + 2 < t.size() ? t[i + 2].type : TokenType::END_OF_FILE;
        if (n == TokenType::RPAREN || n == TokenType::SEMICOLON || n == TokenType::COMMA || n == TokenType::RBRACE || n == TokenType::LBRACE || n == TokenType::END_OF_FILE) return 2;
    }
    return 0;
}

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
    try {
        expandLoopTemplates();
    } catch (RinError& e) {
        if (e.diagnostic) engine.emit(*e.diagnostic);
        else engine.emit(diag::Diagnostic(diag::Code::E0010_ParserError, e.message,
                                           diag::SourceLocation::point(file, e.line, 1)));
        return statements;
    }
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
    expandLoopTemplates();
    while (!isAtEnd()) statements.push_back(declaration());
    return statements;
}

StmtPtr Parser::declaration() {
    if (auto lv2 = loopsV2Statement()) return lv2; // each / repeat / loop / retry / name: loop
    // Modules: 'export let/fun/class/struct/enum ...' -- 'export' كلمة سياقية غير محجوزة (نفس
    // أسلوب class/struct/enum أدناه بالضبط)، مُميَّزة بالنظر خطوة إضافية للأمام: يجب أن يتبعها
    // مباشرة LET أو FUN أو IDENT("class"/"struct"/"enum") تحديداً، وإلا فهي مجرد اسم متغيّر/دالة
    // عادي اسمه "export" في أي سياق آخر (نداء `export(x)`، إسناد `export = 5;`، إلخ) فلا نتدخّل.
    if (check(TokenType::IDENT) && peek().lexeme == "export" && current + 1 < tokens.size()) {
        const Token& after = tokens[current + 1];
        bool followedByDeclaration =
            after.type == TokenType::LET || after.type == TokenType::FUN ||
            (after.type == TokenType::IDENT &&
             (after.lexeme == "class" || after.lexeme == "struct" || after.lexeme == "enum" ||
              after.lexeme == "interface" || after.lexeme == "trait" ||
              after.lexeme == "abstract" || after.lexeme == "final"));
        if (followedByDeclaration) {
            advance(); // 'export'
            StmtPtr inner = declaration();
            inner->exported = true;
            return inner;
        }
    }
    if (match({TokenType::LET})) return letDeclaration();
    if (match({TokenType::FUN})) return functionDeclaration();
    // OOP: 'class'/'struct'/'enum' كلمات سياقية غير محجوزة (بنفس أسلوب route/row/document/warp/
    // state أعلاه بالضبط)، مُميَّزة بالنظر خطوة إضافية للأمام (IDENT — اسم الصنف/التعداد — مباشرة
    // بعدها) حتى لا تصطدم باستخدام أي منها اسم متغيّر عادي في أي سياق آخر.
    // Rin 1.0: 'abstract class X' / 'final class X' / 'abstract final class X' (المعدِّلات قبل 'class' فقط).
    // نمسح للأمام فوق كلمات abstract/final المتتالية، ثم يجب أن نجد IDENT("class") يتبعها اسم الصنف،
    // وإلا فهي مجرد اسم متغيّر/دالة عادي (مثلاً نداء `final(x)`) فلا نتدخّل.
    if (check(TokenType::IDENT) && (peek().lexeme == "abstract" || peek().lexeme == "final")) {
        size_t k = current;
        bool abs = false, fin = false;
        while (k < tokens.size() && tokens[k].type == TokenType::IDENT &&
               (tokens[k].lexeme == "abstract" || tokens[k].lexeme == "final")) {
            if (tokens[k].lexeme == "abstract") abs = true; else fin = true;
            k++;
        }
        if (k + 1 < tokens.size() && tokens[k].type == TokenType::IDENT && tokens[k].lexeme == "class" &&
            tokens[k + 1].type == TokenType::IDENT) {
            if (abs && fin) {
                throw errRich(diag::Code::E0013_InvalidExpression, peek(),
                              "a class cannot be both 'abstract' and 'final'",
                              "an abstract class exists only to be extended, while a final class can never be extended",
                              "remove one of the two modifiers", "'abstract' or 'final'");
            }
            current = k + 1; // تجاوز المعدِّلات و'class'
            return classDeclarationEx(false, ClassKind::Class, abs, fin);
        }
    }
    if (check(TokenType::IDENT) && peek().lexeme == "interface" && checkNext(TokenType::IDENT)) {
        advance(); // 'interface'
        return classDeclarationEx(false, ClassKind::Interface, false, false);
    }
    if (check(TokenType::IDENT) && peek().lexeme == "trait" && checkNext(TokenType::IDENT)) {
        advance(); // 'trait'
        return classDeclarationEx(false, ClassKind::Trait, false, false);
    }
    if (check(TokenType::IDENT) && peek().lexeme == "class" && checkNext(TokenType::IDENT)) {
        advance(); // 'class'
        return classDeclaration(false);
    }
    if (check(TokenType::IDENT) && peek().lexeme == "struct" && checkNext(TokenType::IDENT)) {
        advance(); // 'struct'
        return classDeclaration(true);
    }
    if (check(TokenType::IDENT) && peek().lexeme == "enum" && checkNext(TokenType::IDENT)) {
        advance(); // 'enum'
        return enumDeclaration();
    }
    // ---- المتغيرات الحيّة (Living Variables) — كلمات سياقية غير محجوزة بنفس أسلوب make/class/enum ----
    // كل كلمة تُميَّز بشكل ما يليها تحديداً، فتبقى أسماء عادية في أي سياق آخر (نداء `tape(x)`، إسناد
    // `fuse = 1;`، إلخ): stone/gauge/tape/lens/fuse/bell + IDENT (اسم)، trial + '{'، undo/redo/rearm/unbell
    // + IDENT، abort + ';'. التفاصيل الكاملة في docs/living-variables.md.
    if (check(TokenType::IDENT) && checkNext(TokenType::IDENT)) {
        const std::string& kw = peek().lexeme;
        if (kw == "stone" || kw == "gauge" || kw == "tape" || kw == "lens" || kw == "fuse") return liveDeclaration();
        // عائلة الشروط (docs/base-accept.md): base/accept + inc/dec/rebase/rewind — كلها كلمات سياقية بنفس القاعدة.
        if (kw == "base" || kw == "accept") return liveDeclaration();
        // pre/post (docs/pre-post.md): ما قبل / ما بعد — عقد انتقال حول الإسناد، كلمتان سياقيتان بنفس القاعدة.
        if (kw == "pre" || kw == "post") return liveDeclaration();
        // label/define (docs/label-define.md): تسمية وتعريف — كلمتان سياقيتان بنفس القاعدة.
        if (kw == "label" || kw == "define") return liveDeclaration();
        if (kw == "bell") return bellDeclaration();
        if (kw == "undo" || kw == "redo" || kw == "rearm" || kw == "unbell" ||
            kw == "inc" || kw == "dec" || kw == "rebase" || kw == "rewind") return liveActionStatement();
    }
    if (check(TokenType::IDENT) && peek().lexeme == "trial" && checkNext(TokenType::LBRACE)) return trialStatement();
    if (check(TokenType::IDENT) && peek().lexeme == "abort" && checkNext(TokenType::SEMICOLON)) return liveActionStatement();

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
    // KEY_TERMS: declarative vocabulary registry.
    // `.KEY_TERMS = { "when": "if", ... };` registers semantic names without reserving them globally.
    // `.KEY_TERMS == { ... };` validates the current registry against the supplied map.
    // Both forms are parsed into ordinary native calls so execution stays inside the existing runtime.
    if (check(TokenType::DOT) && checkNext(TokenType::IDENT) &&
        current + 1 < tokens.size() && tokens[current + 1].lexeme == "KEY_TERMS") {
        Token keyTok = advance(); // '.'
        advance();                // KEY_TERMS
        bool validate = false;
        if (match({TokenType::EQUAL_EQUAL})) validate = true;
        else consume(TokenType::EQUAL, "Expected '=' or '==' after '.KEY_TERMS'");
        auto value = expression();
        consume(TokenType::SEMICOLON, "Expected ';' after .KEY_TERMS declaration");
        auto call = std::make_shared<CallExpr>();
        call->callee = validate ? "__key_terms.check" : "__key_terms.set";
        call->args.push_back(value);
        call->line = keyTok.line;
        auto stmt = std::make_shared<ExpressionStmt>();
        stmt->expr = call;
        stmt->line = keyTok.line;
        return stmt;
    }
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
    // Modules: 'import { a, b, c } from "path";' -- بلا '@' إطلاقاً، صياغة مستقلة تماماً عن
    // '@import' أعلاه (لا تصادم: تلك تبدأ بـ '@'، هذه تبدأ بـ IDENT("import") مباشرة تتبعه '{').
    // 'import' هنا أيضاً كلمة سياقية غير محجوزة كسابقتها بالضبط.
    if (check(TokenType::IDENT) && peek().lexeme == "import" && checkNext(TokenType::LBRACE)) {
        advance(); // 'import'
        return importSelectedStatement();
    }
    // '@view.<Kind>=name ... .end/view' -> Indsintime rendering engine (امتداد إضافي، نفس أسلوب
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
        current + 3 < tokens.size() && (tokens[current + 3].type == TokenType::STRING || atTemplate(3))) {
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
    // قناع: mask = expr; داخل الحاويات/المجموعات/Volume. داخل @view/@element/@loop
    // يبقى Attribute عادياً ويُلتقط هناك، لذلك لا يوجد تعارض بين الشكلين.
    if (check(TokenType::IDENT) && peek().lexeme == "mask" && checkNext(TokenType::EQUAL)) {
        advance();
        return maskDeclaration();
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
    if (check(TokenType::IDENT) && peek().lexeme == "emit" && (checkNext(TokenType::STRING) || atTemplate(1))) {
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
    // '@theme=Name key=expr; ... .end/theme' -> Rin Indsin Theme (Pattern Book) declaration, نفس
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
    // 'warp' كلمة سياقية غير محجوزة أيضاً (Indsintime: خلية حالة تفاعلية يستخدمها محرّك العرض)
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
    // 'when' (condition) thenBranch ['otherwise' elseBranch] -> صياغة إنجليزية مبسّطة (sugar) لِـ
    // if/else، مطابقة تماماً من ناحية الدلالة (تُبنى داخلياً كـ IfStmt نفسها تماماً، انظر
    // whenStatement() أدناه) — راجع docs/control-flow.md §1.1. 'when' كلمة سياقية غير محجوزة،
    // مُميَّزة فقط عند ظهورها IDENT("when") متبوعة مباشرة بـ '(' في بداية عبارة (تماماً كأسلوب
    // فحص plus.condition أعلاه)، فلا تتعارض مع استخدامها اسم متغيّر/دالة عادية (when(x) نداء دالة
    // عادي لأنه لا يظهر في بداية عبارة بهذا الفحص إطلاقاً... في الواقع الفحص هنا لا يميّز موضع بداية
    // العبارة من موضع آخر، لكنه يطابق تماماً أسلوب فحص match/route/document الأخرى في هذا الملف).
    if (check(TokenType::IDENT) && peek().lexeme == "when" && checkNext(TokenType::LPAREN)) {
        advance(); // 'when'
        return whenStatement();
    }
    // 'match' (subject) { case v1, v2 { .. } ... [else { .. }] } -> مطابقة أنماط (pattern
    // matching)، امتداد إضافي فوق if/else (انظر MatchStmt في rin_ast.h للشرح الكامل). 'match'
    // كلمة سياقية غير محجوزة، مُميَّزة عند ظهورها IDENT("match") متبوعة مباشرة بـ '('.
    if (check(TokenType::IDENT) && peek().lexeme == "match" && (checkNext(TokenType::LPAREN) || checkNext(TokenType::LBRACE))) {
        advance(); // 'match'
        return matchStatement();
    }
    // 'achieve' expr; / 'achieve';  -> ينهي أقرب كتلة goal {..} محيطة (انظر GoalExpr/AchieveStmt
    // في rin_ast.h). 'achieve' كلمة سياقية غير محجوزة؛ الشرط الوحيد لتمييزها عن استخدام "achieve"
    // اسم متغيّر عادي هو ألا يتبعها مباشرة '=' (حتى لا نصطدم بإسناد 'achieve = 5;' لمتغيّر بهذا
    // الاسم) -- بنفس روح فحص 'mask'/'state' أعلاه لكن بالعكس (هناك EQUAL شرط الدخول، هنا شرط الاستبعاد).
    if (check(TokenType::IDENT) && peek().lexeme == "achieve" && !checkNext(TokenType::EQUAL)) {
        advance(); // 'achieve'
        return achieveStatement();
    }

    return statement();
}

// match (subject) { case v1, v2 { .. } case v3 { .. } [else { .. }] }
StmtPtr Parser::matchStatement() {
    // match { case (c1) { ... } case (c2) { ... } else { ... } }
    // Subject-less form: the first case whose condition is truthy runs (desugars to an if / else-if chain).
    if (check(TokenType::LBRACE)) return conditionMatchStatement();
    consume(TokenType::LPAREN, "Expected '(' after 'match'");
    auto subject = expression();
    consume(TokenType::RPAREN, "Expected ')' after 'match' subject");
    consume(TokenType::LBRACE, "Expected '{' before 'match' body");

    auto stmt = std::make_shared<MatchStmt>();
    stmt->subject = subject;
    bool sawElse = false;
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        if (sawElse) {
            throw err(diag::Code::E0013_InvalidExpression, peek(),
                      "'else' must be the last branch inside a 'match' block");
        }
        if (check(TokenType::IDENT) && peek().lexeme == "case") {
            Token caseTok = advance(); // 'case'
            MatchCase mc;
            mc.line = caseTok.line;
            do { mc.values.push_back(expression()); } while (match({TokenType::COMMA}));
            mc.guard = trailingGuard(); // case A, B when (cond) { ... } : also needs cond to be truthy
            consume(TokenType::LBRACE, "Expected '{' to start 'case' body");
            mc.body = block(); // يستهلك '}' المطابقة بنفسه
            stmt->cases.push_back(std::move(mc));
            continue;
        }
        if (check(TokenType::ELSE)) {
            advance(); // 'else'
            consume(TokenType::LBRACE, "Expected '{' to start 'match' else body");
            stmt->elseBranch = block();
            sawElse = true;
            continue;
        }
        throw err(diag::Code::E0013_InvalidExpression, peek(),
                  "expected 'case' or 'else' inside a 'match' block");
    }
    consume(TokenType::RBRACE, "Expected '}' after 'match' body");
    if (stmt->cases.empty()) {
        throw err(diag::Code::E0013_InvalidExpression, previous(),
                  "'match' must have at least one 'case'");
    }
    return stmt;
}


StmtPtr Parser::conditionMatchStatement() {
    Token open = advance(); // '{'
    struct Arm { ExprPtr cond; std::shared_ptr<BlockStmt> body; };
    std::vector<Arm> arms;
    std::shared_ptr<BlockStmt> elseBody;
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        if (elseBody) {
            throw err(diag::Code::E0013_InvalidExpression, peek(),
                      "'else' must be the last branch inside a 'match' block");
        }
        if (check(TokenType::IDENT) && peek().lexeme == "case") {
            advance();
            consume(TokenType::LPAREN, "Expected '(' after 'case' in a subject-less 'match'");
            auto cond = expression();
            consume(TokenType::RPAREN, "Expected ')' after 'case' condition");
            consume(TokenType::LBRACE, "Expected '{' to start 'case' body");
            arms.push_back({cond, block()});
            continue;
        }
        if (check(TokenType::ELSE)) {
            advance();
            consume(TokenType::LBRACE, "Expected '{' to start 'match' else body");
            elseBody = block();
            continue;
        }
        throw err(diag::Code::E0013_InvalidExpression, peek(), "expected 'case' or 'else' inside a 'match' block");
    }
    consume(TokenType::RBRACE, "Expected '}' after 'match' body");
    if (arms.empty()) {
        throw err(diag::Code::E0013_InvalidExpression, previous(), "'match' must have at least one 'case'");
    }
    StmtPtr tail = elseBody;
    for (size_t i = arms.size(); i-- > 0;) {
        auto st = std::make_shared<IfStmt>();
        st->condition = arms[i].cond;
        st->thenBranch = arms[i].body;
        st->elseBranch = tail;
        st->line = arms[i].body->line ? arms[i].body->line : open.line;
        tail = st;
    }
    return tail;
}

// achieve expr; / achieve;
// achieve expr; / achieve;  -> يجب أن تظهر فقط داخل جسم 'goal { ... }' (مباشرة أو متداخلة عبر
// if/match/block)؛ وإلا فهي خطأ وقت التحليل (نفس قيد break/continue خارج حلقة تماماً).
StmtPtr Parser::achieveStatement() {
    Token tok = previous(); // 'achieve'
    if (goalDepth == 0)
        throw errRich(diag::Code::E0011_UnexpectedToken, tok, "'achieve' used outside of a 'goal' block",
                       "`achieve` ends the innermost enclosing `goal { ... }` block and becomes its "
                       "value, so the parser only allows it while it is currently inside a `goal` "
                       "block body; this `achieve` appears at a point where no `goal` block is open",
                       "move this `achieve` inside a `goal { ... }` block, or use `return` instead if "
                       "you meant to exit the enclosing function");
    auto stmt = std::make_shared<AchieveStmt>();
    stmt->line = tok.line;
    if (!check(TokenType::SEMICOLON) && !check(TokenType::IF)) stmt->value = expression();
    ExprPtr guard = trailingGuard(); // achieve x when (c);
    consume(TokenType::SEMICOLON, "Expected ';' after 'achieve'");
    return guarded(stmt, guard);
}

StmtPtr Parser::letDeclaration() {
    // let+ (docs/let-plus.md): عنصر واحد أو أكثر مفصولة بفاصلة؛ كل عنصر اسم بسيط أو نمط تفكيك.
    // الشكل القديم `let x = 5;` يمرّ هنا بنفس رسائل الخطأ السابقة حرفيًا ويُرجع LetStmt كما كان.
    int firstLine = peek().line;
    std::vector<StmtPtr> items;
    items.push_back(letDeclarator());
    while (match({TokenType::COMMA})) items.push_back(letDeclarator());
    consume(TokenType::SEMICOLON, "Expected ';' after variable declaration");
    if (items.size() == 1) return items[0];
    auto group = std::make_shared<LetGroupStmt>();
    group->items = std::move(items);
    group->line = firstLine;
    return group;
}

StmtPtr Parser::letDeclarator() {
    if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
        Token startTok = peek();
        auto st = std::make_shared<LetPatternStmt>();
        st->line = startTok.line;
        st->pattern = letPattern();
        checkPatternDuplicates(st->pattern, startTok);
        consume(TokenType::EQUAL, "Expected '=' after a destructuring pattern (a pattern needs a value to take apart)");
        st->initializer = expression();
        if (match({TokenType::ELSE})) {
            consume(TokenType::LBRACE, "Expected '{' after 'else' of a `let` declaration");
            st->elseBlock = block();
        }
        return st;
    }
    auto name = consume(TokenType::IDENT, "Expected variable name after 'let'");
    // Type System: 'let name: Type = expr;' — ':' اختيارية بالكامل (additive صرف؛ أي برنامج Rin
    // صالح سابقاً لا يحتوي ':' هنا أصلاً فيبقى يعمل حرفياً بلا أي تغيير). التحقق الفعلي وقت التشغيل
    // فقط (انظر Interpreter::checkDeclaredType) -- لا تحليل ساكن، لا استدلال، هذا كل ما في الأمر.
    std::string typeName;
    if (match({TokenType::COLON})) {
        typeName = consume(TokenType::IDENT, "Expected a type name after ':'").lexeme;
    }
    ExprPtr initializer = nullptr;
    std::shared_ptr<BlockStmt> elseBlock;
    if (match({TokenType::EQUAL})) {
        initializer = expression();
        if (match({TokenType::ELSE})) {
            consume(TokenType::LBRACE, "Expected '{' after 'else' of a `let` declaration");
            elseBlock = block();
        }
    }
    auto stmt = std::make_shared<LetStmt>();
    stmt->name = name.lexeme;
    stmt->initializer = initializer;
    stmt->typeName = typeName;
    stmt->elseBlock = elseBlock;
    stmt->line = name.line;
    return stmt;
}

// ---- let+ patterns ----
static bool atEllipsis(const std::vector<Token>& t, size_t i) {
    return i + 2 < t.size() && t[i].type == TokenType::DOT && t[i + 1].type == TokenType::DOT && t[i + 2].type == TokenType::DOT;
}

LetPattern Parser::letPatternPrimary() {
    LetPattern p;
    p.line = peek().line;
    if (check(TokenType::IDENT)) {
        Token n = advance();
        p.kind = (n.lexeme == "_") ? LetPattern::Kind::Skip : LetPattern::Kind::Name;
        p.name = n.lexeme;
        return p;
    }
    if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) return letPattern();
    throw errRich(diag::Code::E0013_InvalidExpression, peek(), "expected a name or a nested pattern inside a destructuring pattern",
                  "a pattern is made of names, `_` (skip), and nested `[...]` / `{...}` patterns",
                  "write a variable name, e.g. `let [first, second] = pair;`", "a name");
}

LetPattern Parser::letPattern() {
    LetPattern p;
    p.line = peek().line;
    auto parseRest = [&](TokenType closer, const char* closerText) {
        advance(); advance(); advance(); // '...'
        p.hasRest = true;
        p.restName = consume(TokenType::IDENT, "Expected a name after '...'").lexeme;
        if (p.restName == "_") p.restName.clear();
        if (!check(closer))
            throw errRich(diag::Code::E0013_InvalidExpression, peek(), "'...rest' must be the last item of a pattern",
                          "the rest collects everything that is left, so nothing can come after it",
                          std::string("move `...") + p.restName + "` to the end, before `" + closerText + "`", closerText);
    };
    if (match({TokenType::LBRACKET})) {
        p.kind = LetPattern::Kind::Array;
        while (!check(TokenType::RBRACKET) && !isAtEnd()) {
            if (atEllipsis(tokens, current)) { parseRest(TokenType::RBRACKET, "]"); break; }
            if (check(TokenType::COMMA)) { // خانة فارغة: [a, , c]
                LetPattern skip; skip.kind = LetPattern::Kind::Skip; skip.name = "_"; skip.line = peek().line;
                p.items.push_back(skip); advance(); continue;
            }
            LetPattern sub = letPatternPrimary();
            if (match({TokenType::EQUAL})) sub.defaultValue = expression();
            p.items.push_back(std::move(sub));
            if (!match({TokenType::COMMA})) break;
        }
        consume(TokenType::RBRACKET, "Expected ']' to close the destructuring pattern");
        return p;
    }
    consume(TokenType::LBRACE, "Expected '[' or '{' to start a destructuring pattern");
    p.kind = LetPattern::Kind::Map;
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        if (atEllipsis(tokens, current)) { parseRest(TokenType::RBRACE, "}"); break; }
        Token k = peek();
        if (k.type != TokenType::IDENT && k.type != TokenType::STRING)
            throw errRich(diag::Code::E0013_InvalidExpression, k, "expected a key name inside a `{ ... }` pattern",
                          "a map pattern lists the keys to pull out, e.g. `{name, age}` or `{city: town}`",
                          "write a key name", "a key");
        advance();
        LetPattern sub;
        if (match({TokenType::COLON})) {
            sub = letPatternPrimary();
        } else {
            if (k.type != TokenType::IDENT)
                throw errRich(diag::Code::E0013_InvalidExpression, k, "a quoted key needs a name to bind to",
                              "`{\"my key\"}` has no variable name", "write `{\"my key\": name}`", "':'");
            sub.kind = LetPattern::Kind::Name; sub.name = k.lexeme; sub.line = k.line;
        }
        if (match({TokenType::EQUAL})) sub.defaultValue = expression();
        sub.key = k.lexeme;
        p.items.push_back(std::move(sub));
        if (!match({TokenType::COMMA})) break;
    }
    consume(TokenType::RBRACE, "Expected '}' to close the destructuring pattern");
    return p;
}

void Parser::checkPatternDuplicates(const LetPattern& p, const Token& at) const {
    std::vector<std::string> names;
    letPatternNames(p, names);
    std::unordered_set<std::string> seen;
    for (auto& n : names) {
        if (!seen.insert(n).second)
            throw errRich(diag::Code::E0002_DuplicateVariable, at, "`" + n + "` appears twice in the same pattern",
                          "each name in a pattern becomes one variable, so it can only be used once",
                          "rename one of them, or use `_` to skip a position", "a unique name");
    }
}

// ============================================================================
// المتغيرات الحيّة (Living Variables) — انظر الشرح الكامل في rin_ast.h (LiveDeclStmt) وdocs/living-variables.md
// ============================================================================
// stone|gauge|tape|lens|fuse  NAME = expr [modifiers] ;
// المُعدِّلات (كلمات سياقية بعد التهيئة، بأي ترتيب): within A to B | strict | wrap | keep N | burns N
StmtPtr Parser::liveDeclaration() {
    Token kw = advance(); // الكلمة المفتاحية (stone/gauge/...)
    const std::string kind = kw.lexeme;
    Token name = consume(TokenType::IDENT, "Expected a variable name after '" + kind + "'");
    auto st = std::make_shared<LiveDeclStmt>();
    st->name = name.lexeme;
    st->line = name.line;
    st->kind = kind == "stone" ? LiveKind::Stone : kind == "gauge" ? LiveKind::Gauge
             : kind == "tape" ? LiveKind::Tape : kind == "lens" ? LiveKind::Lens
             : kind == "base" ? LiveKind::Base : kind == "accept" ? LiveKind::Accept
             : kind == "pre" ? LiveKind::Pre : kind == "post" ? LiveKind::Post
             : kind == "label" ? LiveKind::Label : kind == "define" ? LiveKind::Define : LiveKind::Fuse;
    const bool isMeta = st->kind == LiveKind::Label || st->kind == LiveKind::Define;
    const bool isGate = st->kind == LiveKind::Pre || st->kind == LiveKind::Post;

    if (match({TokenType::EQUAL})) {
        st->initializer = expression();
    } else if (st->kind != LiveKind::Tape) {
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "'" + kind + " " + name.lexeme + "' needs an initial value",
                      kind == "lens" ? "a lens is defined by the expression it is computed from"
                                     : "a " + kind + " variable must start with a value",
                      "write `" + kind + " " + name.lexeme + " = <value>;`", "'='");
    }

    bool sawWithin = false;
    // accept يستعمل 'if' و'else' (كلمتان محجوزتان) كمُعدِّلين، فيُسمح بهما هنا لهذا النوع فقط.
    auto atModifier = [&]() {
        if (check(TokenType::IDENT)) return true;
        if (st->kind == LiveKind::Pre || st->kind == LiveKind::Post) return check(TokenType::IF);
        return st->kind == LiveKind::Accept && (check(TokenType::IF) || check(TokenType::ELSE));
    };
    while (atModifier()) {
        const std::string mod = peek().lexeme;
        const Token modTok = peek();
        auto reject = [&](const std::string& why) {
            return errRich(diag::Code::E0013_InvalidExpression, modTok,
                           "'" + mod + "' cannot be used with '" + kind + "'", why,
                           "remove `" + mod + "` or pick the variable kind that supports it", "");
        };
        if (isMeta && (mod == "as" || mod == "label" || mod == "about" || mod == "unit")) {
            // as: الحقل الأساسي لنوعه (label ← التسمية، define ← الوصف)؛ والباقي حقول مساعدة من الجهة الأخرى
            const bool isLabel = st->kind == LiveKind::Label;
            ExprPtr* slot = nullptr;
            if (mod == "as") slot = isLabel ? &st->metaLabel : &st->metaAbout;
            else if (mod == "label") { if (isLabel) throw reject("`label x = v as TITLE` already takes the title with `as`"); slot = &st->metaLabel; }
            else if (mod == "about") { if (!isLabel) throw reject("`define x = v as TEXT` already takes the description with `as`"); slot = &st->metaAbout; }
            else slot = &st->metaUnit;
            if (*slot) throw reject("`" + mod + "` is given twice");
            advance();
            *slot = expression();
        } else if (mod == "within") {
            if (st->kind != LiveKind::Gauge) throw reject("only `gauge` has a range (`within A to B`)");
            advance();
            st->lo = expression();
            Token to = peek();
            if (!(to.type == TokenType::IDENT && to.lexeme == "to"))
                throw errRich(diag::Code::E0012_MissingToken, to, "expected 'to' in `within A to B`",
                              "a gauge range is written with two ends separated by `to`",
                              "e.g. `gauge hp = 80 within 0 to 100;`", "'to'");
            advance();
            st->hi = expression();
            sawWithin = true;
        } else if ((mod == "strict" && st->kind != LiveKind::Accept && !isGate) || mod == "wrap") {
            if (st->kind != LiveKind::Gauge) throw reject("only `gauge` has an out-of-range policy (strict/wrap)");
            if (!st->rangeMode.empty()) throw reject("a gauge takes only one of `strict` / `wrap`");
            advance();
            st->rangeMode = mod;
        } else if (mod == "keep") {
            if (st->kind != LiveKind::Tape && st->kind != LiveKind::Gauge)
                throw reject("only `tape` (and `gauge`) keep a history");
            advance();
            st->keep = expression();
        } else if (mod == "burns") {
            if (st->kind != LiveKind::Fuse) throw reject("only `fuse` burns after a number of reads");
            advance();
            st->burns = expression();
        } else if (mod == "to") {
            if (st->kind != LiveKind::Base) throw reject("only `base` has a goal (`to GOAL`); a gauge range is written `within A to B`");
            if (st->goal) throw reject("a base takes only one goal");
            advance();
            st->goal = expression();
        } else if (mod == "step") {
            if (st->kind != LiveKind::Base) throw reject("only `base` has a default step for `inc` / `dec`");
            if (st->step) throw reject("a base takes only one `step`");
            advance();
            st->step = expression();
        } else if (mod == "if" && isGate) { // pre/post: `if` يكتب شرط نوعه (pre → قبل، post → بعد)
            ExprPtr& slot = st->kind == LiveKind::Pre ? st->preRule : st->postRule;
            if (slot) throw reject("a " + kind + " takes only one `if` rule; join conditions with `and` / `or`");
            advance();
            slot = expression();
        } else if ((mod == "pre" || mod == "post") && isGate) { // الشرط الثاني: الجهة المقابلة
            const bool isPreMod = mod == "pre";
            if ((isPreMod && st->kind == LiveKind::Pre) || (!isPreMod && st->kind == LiveKind::Post))
                throw reject("`" + kind + " ... if RULE` already is the " + mod + " rule; the extra clause must be the opposite side (`" + (isPreMod ? "post" : "pre") + " RULE`)");
            ExprPtr& slot = isPreMod ? st->preRule : st->postRule;
            if (slot) throw reject("this declaration already has a `" + mod + "` rule");
            advance();
            slot = expression();
        } else if (mod == "if") {
            if (st->kind != LiveKind::Accept) throw reject("only `accept`, `pre` and `post` have a rule (`if RULE`)");
            if (st->rule) throw reject("an accept takes only one rule; join conditions with `and` / `or`");
            advance();
            st->rule = expression();
        } else if (mod == "else") {
            if (st->kind != LiveKind::Accept) throw reject("only `accept` has a fallback (`else VALUE`)");
            if (st->fallback) throw reject("an accept takes only one fallback");
            advance();
            st->fallback = expression();
        } else if (mod == "strict") {
            if (st->kind != LiveKind::Accept && !isGate) throw reject("only `gauge`, `accept`, `pre` and `post` have a strict mode");
            advance();
            st->strictAccept = true;
        } else {
            break; // ليس مُعدِّلاً: اتركه لـ consume(';') ليُبلِّغ الخطأ الطبيعي
        }
    }
    if (st->kind == LiveKind::Gauge && !sawWithin)
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      "gauge '" + name.lexeme + "' needs a range", "a gauge exists to keep a value inside a range",
                      "add `within <min> to <max>`, e.g. `gauge " + name.lexeme + " = 50 within 0 to 100;`", "'within'");
    if (st->kind == LiveKind::Accept) {
        if (!st->rule)
            throw errRich(diag::Code::E0012_MissingToken, peek(),
                          "accept '" + name.lexeme + "' needs an acceptance rule", "an accept exists to let in only the values its rule approves",
                          "add `if <condition>` (the name `" + name.lexeme + "` stands for the candidate value), e.g. `accept " + name.lexeme + " = 20 if " + name.lexeme + " >= 0;`", "'if'");
        if (st->strictAccept && st->fallback)
            throw errRich(diag::Code::E0013_InvalidExpression, peek(),
                          "accept '" + name.lexeme + "' cannot be both `strict` and have an `else` fallback",
                          "`strict` turns a rejection into an error, while `else` replaces the rejected value",
                          "keep only one of them", "");
    }
    if (isMeta && !(st->kind == LiveKind::Label ? st->metaLabel : st->metaAbout))
        throw errRich(diag::Code::E0012_MissingToken, peek(),
                      kind + " '" + name.lexeme + "' needs " + (st->kind == LiveKind::Label ? "a title" : "a description"),
                      st->kind == LiveKind::Label ? "a `label` gives the variable a display name (its real identifier stays `" + name.lexeme + "`)"
                                                  : "a `define` records what the variable means",
                      "add `as \"...\"`, e.g. `" + kind + " " + name.lexeme + " = 0 as \"...\";`", "'as'");
    if (isGate) {
        const bool isPre = st->kind == LiveKind::Pre;
        if (!(isPre ? st->preRule : st->postRule))
            throw errRich(diag::Code::E0012_MissingToken, peek(),
                          std::string(isPre ? "pre" : "post") + " '" + name.lexeme + "' needs a rule",
                          isPre ? "a `pre` variable asks a question BEFORE every write: may the value change at all?"
                                : "a `post` variable asks a question AFTER every write: is the new state acceptable?",
                          "add `if <condition>` (inside it `old` is the previous value, `new` the written one, and `" + name.lexeme + "` is the " +
                          (isPre ? "old" : "new") + " one), e.g. `" + kind + " " + name.lexeme + " = 0 if " + (isPre ? "old" : "new") + " >= 0;`", "'if'");
    }
    if (st->rangeMode.empty()) st->rangeMode = "clamp";
    consume(TokenType::SEMICOLON, "Expected ';' after '" + kind + "' declaration");
    return st;
}

// bell NAME [ (old, new) ] { body }  — 0 وسائط، أو 1 (= القيمة الجديدة)، أو 2 (= القديمة ثم الجديدة).
StmtPtr Parser::bellDeclaration() {
    advance(); // 'bell'
    Token target = consume(TokenType::IDENT, "Expected the variable to watch after 'bell'");
    std::vector<std::string> params;
    if (match({TokenType::LPAREN})) {
        if (!check(TokenType::RPAREN)) {
            do { params.push_back(consume(TokenType::IDENT, "Expected parameter name").lexeme); }
            while (match({TokenType::COMMA}));
        }
        consume(TokenType::RPAREN, "Expected ')' after bell parameters");
    }
    if (params.size() > 2)
        throw errRich(diag::Code::E0013_InvalidExpression, target, "a bell takes at most two parameters (old, new)",
                      "a bell is told what the value was and what it became", "use `bell x (old, new) { ... }`", "");
    consume(TokenType::LBRACE, "Expected '{' before bell body");
    int savedLoopDepth = loopDepth, savedGoalDepth = goalDepth;
    auto savedLS_ = std::move(loopStack); auto savedLB_ = std::move(labels); loopStack.clear(); labels.clear();
    loopDepth = 0; goalDepth = 0;
    auto body = block();
    loopDepth = savedLoopDepth; goalDepth = savedGoalDepth; loopStack = std::move(savedLS_); labels = std::move(savedLB_);
    auto fn = std::make_shared<FunctionStmt>();
    fn->name = "bell " + target.lexeme;
    fn->params = params;
    fn->body = body;
    fn->line = target.line;
    auto st = std::make_shared<BellStmt>();
    st->target = target.lexeme;
    st->handler = fn;
    st->line = target.line;
    return st;
}

// trial { ... } [else { ... }]
StmtPtr Parser::trialStatement() {
    Token kw = advance(); // 'trial'
    consume(TokenType::LBRACE, "Expected '{' after 'trial'");
    auto st = std::make_shared<TrialStmt>();
    st->line = kw.line;
    st->body = block();
    if (match({TokenType::ELSE})) {
        consume(TokenType::LBRACE, "Expected '{' after 'else' of a trial");
        st->elseBranch = block();
    }
    return st;
}

// undo x [n]; | redo x [n]; | rearm x; | unbell x; | abort;
StmtPtr Parser::liveActionStatement() {
    Token kw = advance();
    auto st = std::make_shared<LiveActionStmt>();
    st->action = kw.lexeme;
    st->line = kw.line;
    if (kw.lexeme != "abort") {
        st->target = consume(TokenType::IDENT, "Expected a variable name after '" + kw.lexeme + "'").lexeme;
        if ((kw.lexeme == "undo" || kw.lexeme == "redo") && !check(TokenType::SEMICOLON)) st->count = expression();
        // inc/dec: `inc x;` | `inc x by 5;` | `inc x 5;` — الخطوة اختيارية (الافتراضي: step الخاص بـ base أو 1).
        if ((kw.lexeme == "inc" || kw.lexeme == "dec") && !check(TokenType::SEMICOLON)) {
            if (check(TokenType::IDENT) && peek().lexeme == "by") advance();
            st->count = expression();
        }
    }
    consume(TokenType::SEMICOLON, "Expected ';' after '" + kw.lexeme + "'");
    return st;
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
    // نفس حماية loopDepth/goalDepth الموجودة في functionDeclaration(): جسم الخُطّاف يبدأ سياق حلقة/هدف
    // جديداً من الصفر (break/continue/achieve بداخله لا تُعتبر صالحة إلا بحلقة/goal داخل الخُطّاف نفسه).
    int savedLoopDepth = loopDepth;
    auto savedLS_ = std::move(loopStack); auto savedLB_ = std::move(labels); loopStack.clear(); labels.clear();
    int savedGoalDepth = goalDepth;
    loopDepth = 0;
    goalDepth = 0;
    auto body = block();
    loopDepth = savedLoopDepth; loopStack = std::move(savedLS_); labels = std::move(savedLB_);
    goalDepth = savedGoalDepth;

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

bool Parser::atTemplate(size_t offset) const {
    size_t i = current + offset;
    return i < tokens.size() && tokens[i].type == TokenType::LPAREN && tokens[i].tpl;
}

ExprPtr Parser::templateNameExpr() {
    consume(TokenType::LPAREN, "Expected a template string");
    ExprPtr e = expression();
    consume(TokenType::RPAREN, "Expected the end of the template string");
    return e;
}

Token Parser::consumeStaticText(const std::string& msg) {
    if (atTemplate()) {
        throw errRich(diag::Code::E0013_InvalidExpression, peek(),
                      "`${}` interpolation is not allowed here",
                      "this position is read when the file is parsed, so it needs fixed text — a template string with `${...}` is only known when the program runs",
                      "write plain text here (a template without `${}` is fine), or compute the value in a variable and use it where an expression is allowed",
                      "a fixed text");
    }
    return consume(TokenType::STRING, msg);
}

// RCS-1.0 §3.6 Events (Phase 2): emit STRING ("," expr)? ("bubbles")? ";"  (يُستدعى بعد استهلاك
// 'emit' من declaration()). 'bubbles' كلمة سياقية اختيارية بلا وسائط: تُقرأ IDENT عادي، لا تتعارض
// مع أي استخدام آخر لهذا الاسم لأنها تُفحَص فقط في هذا الموضع الدقيق (بين payload اختياري و';').
StmtPtr Parser::emitStatement() {
    auto stmt = std::make_shared<EmitStmt>();
    if (atTemplate()) {
        stmt->line = peek().line;
        stmt->nameExpr = templateNameExpr();
    } else {
        Token nameTok = consume(TokenType::STRING, "Expected an event name string after 'emit'");
        stmt->eventName = nameTok.lexeme;
        stmt->line = nameTok.line;
    }
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
    Token nameTok;
    ExprPtr nameExpr;
    if (atTemplate()) { nameTok = peek(); nameTok.lexeme = "<template>"; nameExpr = templateNameExpr(); }
    else nameTok = consume(TokenType::STRING, "Expected an event name string after 'on.event'");
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
    auto savedLS_ = std::move(loopStack); auto savedLB_ = std::move(labels); loopStack.clear(); labels.clear();
    int savedGoalDepth = goalDepth;
    loopDepth = 0;
    goalDepth = 0;
    auto body = block();
    loopDepth = savedLoopDepth; loopStack = std::move(savedLS_); labels = std::move(savedLB_);
    goalDepth = savedGoalDepth;

    auto fn = std::make_shared<FunctionStmt>();
    fn->name = "on.event " + nameTok.lexeme; // لأغراض رسائل الخطأ فقط (عدد الوسائط في callFunction)
    fn->params = params;
    fn->body = body;
    fn->line = nameTok.line;

    auto s = std::make_shared<EventHandlerStmt>();
    s->eventName = nameTok.lexeme;
    s->nameExpr = nameExpr;
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
    std::vector<std::string> paramTypes;
    if (!check(TokenType::RPAREN)) {
        do {
            params.push_back(consume(TokenType::IDENT, "Expected parameter name").lexeme);
            // Type System: 'fun f(a: Type, b) { ... }' — كل وسيط يقبل ': Type' اختيارية بشكل
            // مستقل (يمكن خلط وسائط مُنوَّعة مع أخرى غير مُنوَّعة بحرّية).
            std::string paramType;
            if (match({TokenType::COLON})) {
                paramType = consume(TokenType::IDENT, "Expected a type name after ':'").lexeme;
            }
            paramTypes.push_back(paramType);
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RPAREN, "Expected ')' after parameters");
    // Type System: 'fun f(...): ReturnType { ... }' — نوع الإرجاع اختياري أيضاً.
    std::string returnType;
    if (match({TokenType::COLON})) {
        returnType = consume(TokenType::IDENT, "Expected a type name after ':'").lexeme;
    }
    consume(TokenType::LBRACE, "Expected '{' before function body");
    // جسم الدالة يبدأ سياق "حلقة"/"هدف" جديداً من الصفر: break/continue/achieve داخل دالة معرَّفة
    // نصياً داخل حلقة while أو goal خارجية لا يجب أن تُعتبر صالحة إلا إذا كانت هناك حلقة/goal أخرى
    // داخل الدالة نفسها.
    int savedLoopDepth = loopDepth;
    auto savedLS_ = std::move(loopStack); auto savedLB_ = std::move(labels); loopStack.clear(); labels.clear();
    int savedGoalDepth = goalDepth;
    loopDepth = 0;
    goalDepth = 0;
    auto body = block();
    loopDepth = savedLoopDepth; loopStack = std::move(savedLS_); labels = std::move(savedLB_);
    goalDepth = savedGoalDepth;
    auto fn = std::make_shared<FunctionStmt>();
    fn->name = name.lexeme;
    fn->params = params;
    fn->paramTypes = paramTypes;
    fn->returnType = returnType;
    fn->body = body;
    fn->line = name.line;
    return fn;
}

// class Name [extends Base] { let field = expr; ... fun method(...) { ... } ... }
// struct Name { ... }  -> نفس الصياغة بالضبط (isStruct يفرّق بينهما دلالياً وقت التشغيل فقط).
StmtPtr Parser::classDeclaration(bool isStruct) {
    return classDeclarationEx(isStruct, ClassKind::Class, false, false);
}

// 'fun name(params)[: T] { body }'  أو، حين allowBodyless، 'fun name(params)[: T];' (توقيع بلا جسم:
// دالة abstract أو توقيع داخل interface). يُستدعى بعد استهلاك 'fun' (أو 'get'/'set').
std::shared_ptr<FunctionStmt> Parser::memberFunction(bool allowBodyless) {
    if (!allowBodyless) return std::dynamic_pointer_cast<FunctionStmt>(functionDeclaration());
    auto name = consume(TokenType::IDENT, "Expected method name after 'fun'");
    consume(TokenType::LPAREN, "Expected '(' after method name");
    std::vector<std::string> params;
    std::vector<std::string> paramTypes;
    if (!check(TokenType::RPAREN)) {
        do {
            params.push_back(consume(TokenType::IDENT, "Expected parameter name").lexeme);
            std::string paramType;
            if (match({TokenType::COLON})) paramType = consume(TokenType::IDENT, "Expected a type name after ':'").lexeme;
            paramTypes.push_back(paramType);
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RPAREN, "Expected ')' after parameters");
    std::string returnType;
    if (match({TokenType::COLON})) returnType = consume(TokenType::IDENT, "Expected a type name after ':'").lexeme;
    consume(TokenType::SEMICOLON, "Expected ';' after an abstract method signature (a signature has no body)");
    auto fn = std::make_shared<FunctionStmt>();
    fn->name = name.lexeme;
    fn->params = params;
    fn->paramTypes = paramTypes;
    fn->returnType = returnType;
    fn->body = std::make_shared<BlockStmt>();
    fn->isAbstract = true;
    fn->line = name.line;
    return fn;
}

// صيغة موسَّعة (Rin 1.0):
//   [abstract|final] class Name [extends Base] [implements A, B] [uses T1, T2] { ... }
//   interface Name [extends A, B] { fun sig(a, b); ... }
//   trait Name [uses T1] { let f = 1; fun m() { ... } ... }
// وداخل الجسم تسبق let/fun المعدِّلات: public private protected static abstract final override،
// ودوال الخصائص:  get name() { ... }   set name(v) { ... }
StmtPtr Parser::classDeclarationEx(bool isStruct, ClassKind kind, bool isAbstract, bool isFinal) {
    const char* what = kind == ClassKind::Interface ? "interface" : kind == ClassKind::Trait ? "trait"
                       : isStruct ? "struct" : "class";
    auto name = consume(TokenType::IDENT, std::string("Expected ") + what + " name after '" + what + "'");
    auto cls = std::make_shared<ClassStmt>();
    cls->name = name.lexeme;
    cls->isStruct = isStruct;
    cls->kind = kind;
    cls->isAbstract = isAbstract;
    cls->isFinal = isFinal;
    cls->line = name.line;

    auto readNameList = [&](std::vector<std::string>& out, const char* clause) {
        do {
            out.push_back(consume(TokenType::IDENT, std::string("Expected a name after '") + clause + "'").lexeme);
        } while (match({TokenType::COMMA}));
    };

    // عبارات الترويسة: extends / implements / uses — كلمات سياقية غير محجوزة (يجب أن يتبعها IDENT).
    while (check(TokenType::IDENT) && checkNext(TokenType::IDENT) &&
           (peek().lexeme == "extends" || peek().lexeme == "implements" || peek().lexeme == "uses")) {
        std::string clause = advance().lexeme;
        if (clause == "extends") {
            if (kind == ClassKind::Interface) {
                readNameList(cls->interfaces, "extends");          // interface A extends B, C
            } else if (kind == ClassKind::Trait) {
                throw errRich(diag::Code::E0013_InvalidExpression, previous(),
                              "a trait cannot use 'extends'", "traits are composed with 'uses', not inheritance",
                              "write `trait Name uses Other { ... }` instead", "'uses'");
            } else {
                cls->superclass = advance().lexeme;                 // class B extends A
            }
        } else if (clause == "implements") {
            if (kind != ClassKind::Class) {
                throw errRich(diag::Code::E0013_InvalidExpression, previous(),
                              std::string("'implements' is only valid on a class or struct, not on a ") + what,
                              "an interface extends other interfaces; a class implements them",
                              "use 'extends' for interfaces", "'extends'");
            }
            readNameList(cls->interfaces, "implements");
        } else { // uses
            if (kind == ClassKind::Interface) {
                throw errRich(diag::Code::E0013_InvalidExpression, previous(),
                              "an interface cannot use traits", "interfaces only declare signatures",
                              "move 'uses' to the implementing class", "'extends'");
            }
            readNameList(cls->traits, "uses");
        }
    }

    consume(TokenType::LBRACE, std::string("Expected '{' before ") + what + " body");
    while (!check(TokenType::RBRACE) && !isAtEnd()) {
        // ---- المعدِّلات ----
        std::string access;
        bool mStatic = false, mAbstract = false, mFinal = false, mOverride = false;
        int modCount = 0;
        Token modTok = peek();
        auto isModifierWord = [](const std::string& w) {
            return w == "public" || w == "private" || w == "protected" || w == "static" ||
                   w == "abstract" || w == "final" || w == "override";
        };
        while (check(TokenType::IDENT) && isModifierWord(peek().lexeme) && current + 1 < tokens.size() &&
               (tokens[current + 1].type == TokenType::LET || tokens[current + 1].type == TokenType::FUN ||
                tokens[current + 1].type == TokenType::IDENT)) {
            Token m = advance();
            modCount++;
            if (m.lexeme == "public" || m.lexeme == "private" || m.lexeme == "protected") {
                access = (m.lexeme == "public") ? std::string() : m.lexeme;
            } else if (m.lexeme == "static") mStatic = true;
            else if (m.lexeme == "abstract") mAbstract = true;
            else if (m.lexeme == "final") mFinal = true;
            else mOverride = true;
        }

        // ---- دوال الخصائص: get name() { } / set name(v) { } ----
        if (check(TokenType::IDENT) && (peek().lexeme == "get" || peek().lexeme == "set") &&
            checkNext(TokenType::IDENT) && current + 2 < tokens.size() &&
            tokens[current + 2].type == TokenType::LPAREN) {
            if (kind != ClassKind::Class && kind != ClassKind::Trait) {
                throw errRich(diag::Code::E0013_InvalidExpression, peek(),
                              std::string("property accessors are not allowed inside a ") + what,
                              "'get'/'set' define computed properties with a body",
                              "move the accessor into a class or trait", "'fun'");
            }
            Token kw = advance();
            auto fn = memberFunction(mAbstract);
            fn->accessorKind = (kw.lexeme == "get") ? 1 : 2;
            if (fn->accessorKind == 1 && !fn->params.empty())
                throw errRich(diag::Code::E0013_InvalidExpression, kw, "a getter takes no parameters",
                              "`get " + fn->name + "()` is read as `obj." + fn->name + "`", "remove the parameters", "')'");
            if (fn->accessorKind == 2 && fn->params.size() != 1)
                throw errRich(diag::Code::E0013_InvalidExpression, kw, "a setter takes exactly one parameter",
                              "`set " + fn->name + "(v)` is called by `obj." + fn->name + " = v`", "declare one parameter", "')'");
            if (mStatic)
                throw errRich(diag::Code::E0013_InvalidExpression, modTok, "static properties are not supported",
                              "'get'/'set' work on instances", "use a `static fun` instead", "'fun'");
            fn->access = access; fn->isFinal = mFinal; fn->isOverride = mOverride; fn->isAbstract = mAbstract;
            cls->methods.push_back(fn);
            continue;
        }

        if (match({TokenType::FUN})) {
            bool bodyless = mAbstract || kind == ClassKind::Interface;
            auto fn = memberFunction(bodyless);
            fn->access = access; fn->isStatic = mStatic; fn->isFinal = mFinal; fn->isOverride = mOverride;
            if (kind == ClassKind::Interface) {
                if (modCount > 0 && (mStatic || mFinal || mOverride || !access.empty()))
                    throw errRich(diag::Code::E0013_InvalidExpression, modTok,
                                  "interface methods are always public abstract signatures",
                                  "an interface only lists the methods an implementing class must provide",
                                  "remove the modifiers", "'fun'");
                fn->isAbstract = true;
            } else if (mAbstract) {
                if (kind == ClassKind::Class && !isAbstract)
                    throw errRich(diag::Code::E0013_InvalidExpression, modTok,
                                  "abstract method `" + fn->name + "` declared in non-abstract class `" + cls->name + "`",
                                  "only an abstract class (or a trait) may declare abstract methods",
                                  "write `abstract class " + cls->name + " { ... }`", "'abstract class'");
                if (mStatic || mFinal)
                    throw errRich(diag::Code::E0013_InvalidExpression, modTok,
                                  "an abstract method cannot be static or final",
                                  "abstract methods exist to be overridden by subclasses", "remove the modifier", "'fun'");
                fn->isAbstract = true;
            }
            if (kind == ClassKind::Trait && mStatic)
                throw errRich(diag::Code::E0013_InvalidExpression, modTok, "static members are not supported inside a trait",
                              "a trait is mixed into instances", "declare the static member in the class itself", "'fun'");
            cls->methods.push_back(fn);
            continue;
        }
        if (match({TokenType::LET})) {
            if (kind == ClassKind::Interface)
                throw errRich(diag::Code::E0013_InvalidExpression, previous(), "an interface cannot declare fields",
                              "an interface is a contract of methods only",
                              "declare the field in the implementing class", "'fun'");
            if (mAbstract || mOverride)
                throw errRich(diag::Code::E0013_InvalidExpression, modTok, "'abstract'/'override' apply to methods, not fields",
                              "fields have no body to override", "remove the modifier", "'let'");
            if (kind == ClassKind::Trait && mStatic)
                throw errRich(diag::Code::E0013_InvalidExpression, modTok, "static members are not supported inside a trait",
                              "a trait is mixed into instances", "declare the static member in the class itself", "'let'");
            auto letStmt = std::dynamic_pointer_cast<LetStmt>(letDeclaration());
            if (!letStmt || letStmt->elseBlock)
                throw errRich(diag::Code::E0013_InvalidExpression, previous(),
                              "a class field takes exactly one plain `let name = value;`",
                              "destructuring patterns, several names in one `let`, and `let ... else` apply to code, not to field declarations",
                              "declare each field on its own line", "'let name = value;'");
            ClassFieldDecl fd;
            fd.name = letStmt->name;
            fd.initializer = letStmt->initializer;
            fd.access = access; fd.isStatic = mStatic; fd.isFinal = mFinal;
            cls->fields.push_back(std::move(fd));
            continue;
        }
        throw errRich(diag::Code::E0013_InvalidExpression, peek(),
                      std::string("expected a field ('let') or method ('fun') inside ") + what + " body",
                      "only field declarations (`let name = value;`), method declarations "
                      "(`fun name(...) { ... }`) and property accessors (`get name() { ... }`) are allowed directly "
                      "inside a class/struct/trait body (modifiers: public private protected static abstract final override)",
                      "add 'let' before a field or 'fun' before a method",
                      "'let' or 'fun'");
    }
    consume(TokenType::RBRACE, std::string("Expected '}' after ") + what + " body");
    return cls;
}

// enum Name { CaseA, CaseB = expr, ... }
StmtPtr Parser::enumDeclaration() {
    auto name = consume(TokenType::IDENT, "Expected enum name after 'enum'");
    auto en = std::make_shared<EnumStmt>();
    en->name = name.lexeme;
    en->line = name.line;

    consume(TokenType::LBRACE, "Expected '{' before enum body");
    if (!check(TokenType::RBRACE)) {
        do {
            if (check(TokenType::RBRACE)) break; // يسمح بفاصلة زائدة قبل '}'
            auto caseTok = consume(TokenType::IDENT, "Expected enum case name");
            EnumCase c;
            c.name = caseTok.lexeme;
            c.line = caseTok.line;
            if (match({TokenType::EQUAL})) {
                c.value = expression();
            }
            en->cases.push_back(std::move(c));
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RBRACE, "Expected '}' after enum body");
    return en;
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
    // print.image(...) -> صورة فعلية مُضمَّنة في الكونسول (انظر PrintImageStmt في rin_ast.h)، بنفس
    // أسلوب فحص print.log أعلاه بالضبط: PRINT '.' IDENT("image") هو الشكل الوحيد الذي يُحوَّل لعبارة
    // PrintImageStmt؛ أي 'print' أخرى تبقى كما كانت بلا أي تغيير.
    if (check(TokenType::PRINT) && checkNext(TokenType::DOT) &&
        current + 2 < tokens.size() && tokens[current + 2].type == TokenType::IDENT &&
        tokens[current + 2].lexeme == "image") {
        Token printTok = peek();
        advance(); // 'print'
        advance(); // '.'
        advance(); // 'image'
        return imageStatement(printTok);
    }
    // Key-condition forms (`=if=(c) stmt`, `=unless=(c) stmt`, `=while=(c) stmt`, `=all=(a, b) stmt`, ...).
    // All of them are syntax sugar over the existing IfStmt / WhileStmt nodes — see keyConditionStatement().
    {
        std::string keyTerm;
        if (atKeyCondition(current, keyTerm)) return keyConditionStatement();
        if (check(TokenType::EQUAL) && current + 3 < tokens.size() &&
            tokens[current + 2].type == TokenType::EQUAL && tokens[current + 3].type == TokenType::LPAREN &&
            tokens[current + 1].type != TokenType::STRING && tokens[current + 1].type != TokenType::NUMBER &&
            (tokens[current + 1].lexeme == "elif" || tokens[current + 1].lexeme == "elseif")) {
            throw errRich(diag::Code::E0012_MissingToken, tokens[current + 1],
                          "'=" + tokens[current + 1].lexeme + "=' has no key condition to continue",
                          "=elif= only continues the chain of a preceding =if= / =when= / =unless= statement",
                          "start the chain with `=if=(cond) { ... }`, or use `=if=` here instead of `=" + tokens[current + 1].lexeme + "=`");
        }
    }
    if (check(TokenType::IDENT) && !peek().lexeme.empty() && peek().lexeme[0] == '#') {
        if (auto hs = hashStatement()) return hs; // عائلة #: #do #done #ban #ignorance #swap
    }
    if (match({TokenType::PRINT})) return printStatement();
    if (match({TokenType::IF})) return ifStatement();
    if (match({TokenType::WHILE})) return whileStatement();
    if (match({TokenType::FOR})) return forStatement();
    if (match({TokenType::RETURN})) return returnStatement();
    if (match({TokenType::BREAK})) return breakStatement();
    if (match({TokenType::CONTINUE})) return continueStatement();
    if (check(TokenType::IDENT) && peek().lexeme == "try" && checkNext(TokenType::LBRACE)) {
        advance();
        auto st = std::make_shared<TryCatchStmt>();
        consume(TokenType::LBRACE, "Expected '{' after 'try'");
        st->tryBranch = block();
        if (!(check(TokenType::IDENT) && peek().lexeme == "catch"))
            throw errRich(diag::Code::E0012_MissingToken, peek(), "expected 'catch' after 'try' block",
                          "a try block must be followed by a catch block",
                          "write `try { ... } catch (error) { ... }`");
        advance();
        if (match({TokenType::LPAREN})) {
            st->catchName = consume(TokenType::IDENT, "Expected catch variable name").lexeme;
            consume(TokenType::RPAREN, "Expected ')' after catch variable");
        }
        consume(TokenType::LBRACE, "Expected '{' before catch body");
        st->catchBranch = block();
        st->line = previous().line;
        return st;
    }
    if (check(TokenType::IDENT) && peek().lexeme == "throw") {
        Token t = advance();
        auto st = std::make_shared<ThrowStmt>();
        st->value = expression();
        ExprPtr guard = trailingGuard(); // throw err when (c);
        consume(TokenType::SEMICOLON, "Expected ';' after throw value");
        st->line = t.line;
        return guarded(st, guard);
    }
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
    enterLoop();
    auto body = block();
    exitLoop();
    auto st = std::make_shared<WhileStmt>();
    st->condition = condition;
    st->body = body;
    st->line = tok.line;
    return finishLoop(st);
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
    Token idTok = consumeStaticText( "Expected a quoted id, e.g. .object(\"user01\")");
    consume(TokenType::RPAREN, "Expected ')' after the object id");
    auto stmt = std::make_shared<ObjectLiteralStmt>();
    stmt->id = idTok.lexeme;
    stmt->line = objectTok.line;
    FrameGuard frame(*this, openBraceIfSameLine(), objectTok.line);
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
        "sep", "level", "label", "repeat", "pretty", "upper", "lower", "width", "align",
        "bold", "italic", "underline", "strike", "dim", "reverse", "blink", "color", "bg"   // تنسيق الخط (docs/print-fonts.md)
    };
    static const char* fxNames[9] = {"bold", "italic", "underline", "strike", "dim", "reverse", "blink", "color", "bg"};
    ExprPtr fxArgs[9];
    bool anyFx = false;
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
        else for (int fi = 0; fi < 9; fi++) if (attr == fxNames[fi]) { fxArgs[fi] = value; anyFx = true; }
    }
    if (anyFx) {   // تنسيق الخط: يُجمَّع في استدعاء واحد لـ __fxcode (يُطبَّق بعد upper/width/align، فلا يُفسد الحشو)
        auto call = std::make_shared<CallExpr>();
        call->callee = "__fxcode"; call->line = tok.line;
        for (int fi = 0; fi < 9; fi++) {
            if (fxArgs[fi]) call->args.push_back(fxArgs[fi]);
            else { auto nl = std::make_shared<LiteralExpr>(); nl->kind = LiteralExpr::Kind::NIL; nl->line = tok.line; call->args.push_back(nl); }
        }
        stmt->fx = call;
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

// print.image(path [, caption=expr] [, width=expr] [, if=expr]);
// نفس أسلوب logStatement() أعلاه تماماً: عنصر موضعي واحد (مسار الصورة، إلزامي) مقروء عبر pipeline()
// (وليس expression()، لنفس سبب تجنّب ابتلاع "caption=..." كتعبير إسناد كامل)، بالإضافة لسمات
// key=value اختيارية بأي ترتيب، كل واحدة مرة على الأكثر. يُستدعى بعد أن يكون statement() قد استهلك
// بالفعل 'print' '.' 'image'.
StmtPtr Parser::imageStatement(const Token& printTok) {
    auto stmt = std::make_shared<PrintImageStmt>();
    consume(TokenType::LPAREN, "Expected '(' after 'print.image'");
    static const std::unordered_set<std::string> imgAttrs = {"caption", "width"};
    std::unordered_set<std::string> seenAttrs;
    bool havePath = false;
    if (!check(TokenType::RPAREN)) {
        for (;;) {
            std::string attr;
            if (check(TokenType::IF) && checkNext(TokenType::EQUAL)) {
                attr = "if"; // 'if' كلمة محجوزة (TokenType::IF)، تماماً كما في logStatement() أعلاه
            } else if (check(TokenType::IDENT) && imgAttrs.count(peek().lexeme) && checkNext(TokenType::EQUAL)) {
                attr = peek().lexeme;
            }
            if (!attr.empty()) {
                advance(); // استهلاك توكن اسم السمة
                if (seenAttrs.count(attr)) {
                    throw errRich(diag::Code::E0016_InvalidProperty, printTok,
                                   "'print.image': `" + attr + "` attribute repeated",
                                   "each attribute (`caption`, `width`, `if`) may only be set once "
                                   "per `print.image(...)` call; the parser found `" + attr +
                                   "=` a second time in the same call",
                                   "remove the duplicate `" + attr + "=...` — keep only the first "
                                   "occurrence, or the last one if that's the value you actually want");
                }
                seenAttrs.insert(attr);
                advance(); // '='
                ExprPtr value = expression();
                if (attr == "if") stmt->ifCond = value;
                else if (attr == "caption") stmt->caption = value;
                else if (attr == "width") stmt->width = value;
            } else {
                if (havePath) {
                    throw errRich(diag::Code::E0016_InvalidProperty, printTok,
                                   "'print.image': only one image path is allowed per call",
                                   "`print.image(...)` takes exactly one positional value (the image "
                                   "path) — a second bare value was found where an attribute "
                                   "(`caption=`/`width=`/`if=`) or ')' was expected",
                                   "remove the extra value, or move it into `caption=\"...\"` if you "
                                   "meant it as a label for the image");
                }
                stmt->path = pipeline();
                havePath = true;
            }
            if (!match({TokenType::COMMA})) break;
        }
    }
    if (!havePath) {
        throw errRich(diag::Code::E0012_MissingToken, printTok,
                       "'print.image' requires an image path",
                       "the image file to display is mandatory — there is nothing to print without it",
                       "call it as `print.image(\"path/to/file.png\")`, optionally with "
                       "`caption=\"...\"` and/or `width=...`");
    }
    consume(TokenType::RPAREN, "Expected ')' after 'print.image' arguments");
    consume(TokenType::SEMICOLON, "Expected ';' after 'print.image' statement");
    stmt->line = printTok.line;
    return stmt;
}


// ============================================================================
// Trailing guards: `<statement> when (c);` / `unless (c);` / `if (c);`
// Applies to return / break / continue / achieve / throw. Pure sugar: the statement is wrapped in an IfStmt.
//     break when (i > 10);        ==>  if (i > 10) { break; }
//     return x unless (empty(x)); ==>  if (!empty(x)) { return x; }
// `when` / `unless` must be followed by '(' ; after `return` they only count as a guard when a value precedes
// them, so `return when(x);` keeps its old meaning (a call to `when`).
// ============================================================================
ExprPtr Parser::trailingGuard() {
    bool isIf = check(TokenType::IF);
    bool isWhen = check(TokenType::IDENT) && (peek().lexeme == "when" || peek().lexeme == "unless") && checkNext(TokenType::LPAREN);
    if (!isIf && !isWhen) return nullptr;
    Token kw = advance();
    consume(TokenType::LPAREN, "Expected '(' after '" + kw.lexeme + "' guard");
    auto cond = expression();
    consume(TokenType::RPAREN, "Expected ')' after guard condition");
    if (kw.lexeme == "unless") {
        auto neg = std::make_shared<UnaryExpr>();
        neg->op = TokenType::BANG; neg->right = cond; neg->line = cond->line;
        return neg;
    }
    return cond;
}

StmtPtr Parser::guarded(StmtPtr inner, ExprPtr guard) {
    if (!guard) return inner;
    auto st = std::make_shared<IfStmt>();
    st->condition = guard;
    st->thenBranch = inner;
    st->line = inner ? inner->line : guard->line;
    return st;
}

// ============================================================================
// Key conditions: `=term=(...) statement` — compact condition forms (docs/key_terms_and_builtins.md)
//
//   branch  : =if= =when=                  if (c)
//             =unless= =ifnot=             if (!c)
//             =elif= =elseif=              continuation of a key-condition chain (else if (c))
//   loops   : =while=                      while (c)
//             =until=                      while (!c)
//   multi   : =all=(a, b, ...)             a and b and ...      (short-circuit)
//             =any=(a, b, ...)             a or b or ...        (short-circuit)
//             =none=(a, b, ...)            !(a or b or ...)
//   value   : =nil=(x)  =notnil=(x)        x == nil / x != nil
//             =empty=(x)  =present=(x)     empty(x) / present(x)
//
// Every branch form accepts an optional tail: `else stmt`, `=else= stmt`, or `=elif=(c) stmt ...`.
// Nothing here touches the lexer keyword table: a statement can never legally start with '=',
// so the leading `= word =` shape is unambiguous.
// ============================================================================
static bool isKeyConditionTerm(const std::string& t, bool& startsStatement) {
    static const char* starters[] = {"if", "when", "unless", "ifnot", "while", "until",
                                     "all", "any", "none", "nil", "notnil", "empty", "present"};
    static const char* continuers[] = {"elif", "elseif"};
    for (const char* x : starters) if (t == x) { startsStatement = true; return true; }
    for (const char* x : continuers) if (t == x) { startsStatement = false; return true; }
    return false;
}

bool Parser::atKeyCondition(size_t at, std::string& term) const {
    if (at + 3 >= tokens.size()) return false;
    if (tokens[at].type != TokenType::EQUAL || tokens[at + 2].type != TokenType::EQUAL) return false;
    TokenType mid = tokens[at + 1].type;
    if (mid == TokenType::STRING || mid == TokenType::NUMBER) return false; // '= "if" =' is not a key term
    bool starts = false;
    if (!isKeyConditionTerm(tokens[at + 1].lexeme, starts)) return false;
    if (tokens[at + 3].type != TokenType::LPAREN) return false;
    term = tokens[at + 1].lexeme;
    return starts; // =elif= alone is only valid as a continuation (handled inside keyConditionStatement)
}

StmtPtr Parser::keyConditionStatement(bool asElseIf) {
    advance(); // =
    std::string term = advance().lexeme;
    if (asElseIf) term = "if"; // =elif= / =elseif= behave exactly like =if= (and chain further)
    advance(); // =
    const int line = previous().line;
    consume(TokenType::LPAREN, "Expected '(' after key condition '=" + term + "='");

    auto makeNot = [&](ExprPtr e) -> ExprPtr {
        auto neg = std::make_shared<UnaryExpr>();
        neg->op = TokenType::BANG; neg->right = e; neg->line = e->line;
        return neg;
    };
    auto makeCall = [&](const std::string& fn, ExprPtr arg) -> ExprPtr {
        auto call = std::make_shared<CallExpr>();
        call->callee = fn; call->args.push_back(arg); call->line = arg->line;
        return call;
    };
    auto makeNilCmp = [&](ExprPtr e, TokenType op) -> ExprPtr {
        auto lit = std::make_shared<LiteralExpr>();
        lit->kind = LiteralExpr::Kind::NIL; lit->line = e->line;
        auto b = std::make_shared<BinaryExpr>();
        b->left = e; b->op = op; b->right = lit; b->line = e->line;
        return b;
    };

    ExprPtr condition;
    if (term == "all" || term == "any" || term == "none") {
        std::vector<ExprPtr> parts;
        do { parts.push_back(expression()); } while (match({TokenType::COMMA}));
        consume(TokenType::RPAREN, "Expected ')' after key condition");
        condition = parts[0];
        for (size_t i = 1; i < parts.size(); ++i) {
            auto l = std::make_shared<LogicalExpr>();
            l->left = condition; l->right = parts[i];
            l->op = (term == "all") ? TokenType::AND : TokenType::OR;
            l->line = parts[i]->line;
            condition = l;
        }
        if (term == "none") condition = makeNot(condition);
    } else {
        condition = expression();
        consume(TokenType::RPAREN, "Expected ')' after key condition");
        if (term == "unless" || term == "ifnot" || term == "until") condition = makeNot(condition);
        else if (term == "nil")     condition = makeNilCmp(condition, TokenType::EQUAL_EQUAL);
        else if (term == "notnil")  condition = makeNilCmp(condition, TokenType::BANG_EQUAL);
        else if (term == "empty")   condition = makeCall("empty", condition);
        else if (term == "present") condition = makeCall("present", condition);
    }

    // ---- loops ----
    if (term == "while" || term == "until") {
        enterLoop();
        auto body = statement();
        exitLoop();
        auto w = std::make_shared<WhileStmt>();
        w->condition = condition;
        w->body = body;
        attachDoneBlock(w->doneBlock);
        w->line = line;
        return finishLoop(w);
    }

    // ---- branches ----
    auto thenBranch = statement();
    StmtPtr elseBranch = nullptr;
    if (check(TokenType::ELSE)) {
        advance();
        elseBranch = statement();
    } else if (check(TokenType::EQUAL) && current + 3 < tokens.size() &&
               tokens[current + 1].lexeme == "else" && tokens[current + 2].type == TokenType::EQUAL) {
        advance(); advance(); advance(); // = else =
        elseBranch = statement();
    } else if (check(TokenType::EQUAL) && current + 3 < tokens.size() &&
               tokens[current + 2].type == TokenType::EQUAL && tokens[current + 3].type == TokenType::LPAREN &&
               tokens[current + 1].type != TokenType::STRING && tokens[current + 1].type != TokenType::NUMBER &&
               (tokens[current + 1].lexeme == "elif" || tokens[current + 1].lexeme == "elseif")) {
        // =elif=(c) stmt ... : the rest of the chain becomes the else-branch (nested IfStmt)
        elseBranch = keyConditionStatement(true);
    }
    auto st = std::make_shared<IfStmt>();
    st->condition = condition;
    st->thenBranch = thenBranch;
    st->elseBranch = elseBranch;
    st->line = thenBranch ? thenBranch->line : line;
    return st;
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

// when (condition) thenBranch ['otherwise' elseBranch] -> مطابقة تماماً لـ if/else، انظر التعليق
// أعلى موقع الاستدعاء في declaration() وdocs/control-flow.md §1.1. 'otherwise' كلمة سياقية غير
// محجوزة أيضاً (بنفس أسلوب 'else' لكن غير محجوزة عالمياً)، فلا تتعارض مع استخدامها اسم متغيّر عادي.
StmtPtr Parser::whenStatement() {
    consume(TokenType::LPAREN, "Expected '(' after 'when'");
    auto condition = expression();
    consume(TokenType::RPAREN, "Expected ')' after 'when' condition");
    auto thenBranch = statement();
    StmtPtr elseBranch = nullptr;
    if (check(TokenType::IDENT) && peek().lexeme == "otherwise") {
        advance(); // 'otherwise'
        elseBranch = statement();
    }
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
    ExprPtr lim = limitClause();
    enterLoop();
    auto body = statement();
    exitLoop();
    auto stmt = std::make_shared<WhileStmt>();
    stmt->condition = condition;
    stmt->body = body;
    attachDoneBlock(stmt->doneBlock);
    return finishLoop(stmt, lim);
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
    if (!check(TokenType::LPAREN) && atEachForm(current - 1, true)) { // for x in xs { }  — صياغة بلا أقواس = each
        EachHdr h = parseEachHdr();
        ExprPtr lim = limitClause();
        enterLoop();
        auto body = statement();
        exitLoop();
        return buildEach(h, body, forTok.line, lim);
    }
    // Rin For+: for let i = 0; i < n; i++ { ... }  — C-style بلا أقواس (الجسم كتلة { })
    const size_t scanFrom = current;
    bool parenless = false;
    if (!check(TokenType::LPAREN)) {
        if (check(TokenType::SEMICOLON)) parenless = true;
        else if (check(TokenType::LET) && current + 2 < tokens.size() && tokens[current + 1].type == TokenType::IDENT &&
                 (tokens[current + 2].type == TokenType::EQUAL || tokens[current + 2].type == TokenType::COMMA || tokens[current + 2].type == TokenType::COLON)) parenless = true;
        else if (check(TokenType::IDENT) && checkNext(TokenType::EQUAL)) parenless = true;
    }
    if (!parenless) consume(TokenType::LPAREN, "Expected '(' after 'for'");

    // for (let NAME in iterable) { body } -> ForInStmt (انظر rin_ast.h). يجب فحصه *قبل* مسار
    // C-style أدناه: 'let' + IDENT + IDENT("in") تحديداً (نظرة ثلاث خطوات للأمام) يُميِّزه عن
    // `for (let i = 0; ...)` العادية بلا أي غموض -- 'in' كلمة سياقية غير محجوزة (بنفس أسلوب
    // extends/try/catch أعلاه)، فاستخدامها اسم متغيّر عادي في أي سياق آخر يبقى يعمل بلا تغيير.
    // let+: for (let [k, v] in pairs) / for (let {name} in rows) — نمط تفكيك لكل عنصر. إن لم يتبع النمطَ
    // 'in' (مثلاً `for (let [a, b] = pair; ...)`) نرجع إلى مسار C-style العادي.
    if (check(TokenType::LET) && current + 1 < tokens.size() &&
        (tokens[current + 1].type == TokenType::LBRACKET || tokens[current + 1].type == TokenType::LBRACE)) {
        size_t save = current;
        Token letTok = advance(); // 'let'
        LetPattern pat = letPattern();
        if (check(TokenType::IDENT) && peek().lexeme == "in") {
            advance(); // 'in'
            checkPatternDuplicates(pat, letTok);
            auto iterable = maybeHashRange(expression());
            consume(TokenType::RPAREN, "Expected ')' after 'for...in' iterable");
            ExprPtr lim = limitClause();
            enterLoop();
            auto body = statement();
            exitLoop();
            auto stmt = std::make_shared<ForInStmt>();
            stmt->pattern = std::make_shared<LetPattern>(std::move(pat));
            stmt->iterable = iterable;
            stmt->body = body;
            stmt->line = forTok.line;
            attachDoneBlock(stmt->doneBlock);
            return finishLoop(stmt, lim);
        }
        current = save;
    }
    if (check(TokenType::LET) && current + 2 < tokens.size() &&
        tokens[current + 1].type == TokenType::IDENT &&
        tokens[current + 2].type == TokenType::IDENT && tokens[current + 2].lexeme == "in") {
        advance(); // 'let'
        Token nameTok = advance(); // NAME
        advance(); // 'in'
        auto iterable = maybeHashRange(expression());
        consume(TokenType::RPAREN, "Expected ')' after 'for...in' iterable");
        ExprPtr lim = limitClause();
        enterLoop();
        auto body = statement();
        exitLoop();
        auto stmt = std::make_shared<ForInStmt>();
        stmt->varName = nameTok.lexeme;
        stmt->iterable = iterable;
        stmt->body = body;
        stmt->line = forTok.line;
        attachDoneBlock(stmt->doneBlock);
        return finishLoop(stmt, lim);
    }

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
    if (!check(TokenType::RPAREN) && !(parenless && check(TokenType::LBRACE))) {
        increment = expression();
        if (check(TokenType::COMMA)) { // for (...; ...; i++, j--)  — عدّة خطوات تزايد تُنفَّذ بالترتيب
            auto many = std::make_shared<ArrayExpr>(); many->line = forTok.line;
            many->elements.push_back(increment);
            while (match({TokenType::COMMA})) many->elements.push_back(expression());
            increment = many;
        }
    }
    if (!parenless) consume(TokenType::RPAREN, "Expected ')' after 'for' clauses");

    ExprPtr lim = limitClause();
    enterLoop();
    auto body = statement();
    exitLoop();

    auto stmt = std::make_shared<ForStmt>();
    stmt->initializer = initializer;
    stmt->condition = condition;
    stmt->increment = increment;
    stmt->body = body;
    stmt->line = forTok.line;
    stmt->freshIterScope = tokensMayCapture(scanFrom, current); // بلا closures => مسار سريع في المفسّر
    attachDoneBlock(stmt->doneBlock);
    return finishLoop(stmt, lim);
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

// ============================================================================
// عائلة # — انظر docs/hash-family.md
// ============================================================================

// #done { ... } اختيارية بعد while/for/for-in/#do: تُنفَّذ إن انتهت الحلقة طبيعياً (دون break/#done;).
void Parser::attachDoneBlock(StmtPtr& done) {
    if (check(TokenType::IDENT) && (peek().lexeme == "#done" || peek().lexeme == "nobreak") && checkNext(TokenType::LBRACE)) {
        advance(); // #done / nobreak (بديل مقروء، مثل else في for..else بلغة Python)
        advance(); // {
        done = block();
    }
}

// A #to B [step C] داخل for (let i in ...) -> نداء #to(A, B[, C]) (مدى شامل للطرفين).
ExprPtr Parser::maybeHashRange(ExprPtr first) {
    if (!(check(TokenType::IDENT) && (peek().lexeme == "to" || peek().lexeme == "#to" || peek().lexeme == "downto"))) return first;
    Token toTok = advance();
    auto call = std::make_shared<CallExpr>();
    call->callee = toTok.lexeme == "downto" ? "#downto" : "#to"; // downto: عدّ تنازلي فقط (فارغ إن كان الطرف الأول أصغر)
    call->line = toTok.line;
    call->args.push_back(first);
    call->args.push_back(expression());
    if (check(TokenType::IDENT) && peek().lexeme == "step") {
        advance();
        call->args.push_back(expression());
    }
    return call;
}

StmtPtr Parser::hashStatement() {
    const std::string kw = peek().lexeme;
    if (kw == "#do") return hashDoStatement();
    if (kw == "#ban") return hashBanStatement();
    if (kw == "#ignorance") return hashIgnoranceStatement();
    if (kw == "#swap") return hashSwapStatement();
    if (kw == "#done") {
        Token tok = advance();
        if (check(TokenType::LBRACE))
            throw errRich(diag::Code::E0011_UnexpectedToken, tok, "'#done { ... }' must directly follow a loop",
                          "a `#done` block runs when a loop finishes normally, so it belongs right after the loop body",
                          "write `#while (c) { ... } #done { ... }` or `for (...) { ... } #done { ... }`");
        if (loopDepth == 0)
            throw errRich(diag::Code::E0011_UnexpectedToken, tok, "'#done;' used outside of a loop",
                          "`#done;` leaves the innermost enclosing loop (like `break`) and marks the loop as finished early",
                          "move this `#done;` inside a loop body");
        consume(TokenType::SEMICOLON, "Expected ';' after '#done'");
        auto st = std::make_shared<BreakStmt>(); // #done; = خروج مبكّر من الحلقة (بلا تشغيل كتلة #done)
        st->line = tok.line;
        return st;
    }
    return nullptr;
}

// #do body #while (cond);  — تنفّذ الجسم مرة واحدة على الأقل ثم تكرر ما دام الشرط صحيحاً.
// تُفكَّك إلى ForStmt بمتغيّر "أول مرة" مخفي:  for (let #first = true; #first or cond; #first = false) body
// (فتعمل continue بالضبط كما في do-while الحقيقية: تنتقل إلى فحص الشرط، لا إلى أول الجسم).
StmtPtr Parser::hashDoStatement() {
    Token doTok = advance(); // #do
    enterLoop();
    auto body = statement();
    exitLoop();
    if (!check(TokenType::WHILE))
        throw errRich(diag::Code::E0012_MissingToken, peek(), "expected '#while' after the '#do' body",
                      "a do-loop is written `#do { ... } #while (condition);`",
                      "add `#while (condition);` right after the body", "'#while'");
    advance(); // #while (أو while)
    consume(TokenType::LPAREN, "Expected '(' after '#while'");
    auto cond = expression();
    consume(TokenType::RPAREN, "Expected ')' after '#while' condition");
    consume(TokenType::SEMICOLON, "Expected ';' after '#do ... #while (...)'");

    const std::string flag = "#first" + std::to_string(hashCounter++);
    auto lit = [&](bool b) { auto l = std::make_shared<LiteralExpr>(); l->kind = LiteralExpr::Kind::BOOL; l->boolean = b; l->line = doTok.line; return l; };
    auto var = [&]() { auto v = std::make_shared<VariableExpr>(); v->name = flag; v->line = doTok.line; return v; };
    auto init = std::make_shared<LetStmt>();
    init->name = flag; init->initializer = lit(true); init->line = doTok.line;
    auto orExpr = std::make_shared<LogicalExpr>();
    orExpr->left = var(); orExpr->op = TokenType::OR; orExpr->right = cond; orExpr->line = doTok.line;
    auto inc = std::make_shared<AssignExpr>();
    inc->name = flag; inc->value = lit(false); inc->line = doTok.line;
    auto st = std::make_shared<ForStmt>();
    st->initializer = init; st->condition = orExpr; st->increment = inc; st->body = body; st->line = doTok.line;
    attachDoneBlock(st->doneBlock);
    return finishLoop(st);
}

// #ban x;  #ban x, y;  -> يمنع الكتابة على متغيّرات موجودة (E0043) | #ban x = expr; -> يعلن متغيّراً ممنوع التعديل.
StmtPtr Parser::hashBanStatement() {
    Token kw = advance(); // #ban
    std::vector<StmtPtr> items;
    do {
        Token name = consume(TokenType::IDENT, "Expected a variable name after '#ban'");
        if (match({TokenType::EQUAL})) {
            auto st = std::make_shared<LiveDeclStmt>();
            st->kind = LiveKind::Stone;
            st->name = name.lexeme;
            st->initializer = expression();
            st->line = name.line;
            items.push_back(st);
        } else {
            auto st = std::make_shared<LiveActionStmt>();
            st->action = "ban";
            st->target = name.lexeme;
            st->line = name.line;
            items.push_back(st);
        }
    } while (match({TokenType::COMMA}));
    consume(TokenType::SEMICOLON, "Expected ';' after '#ban'");
    if (items.size() == 1) return items[0];
    auto group = std::make_shared<LetGroupStmt>();
    group->items = std::move(items);
    group->line = kw.line;
    return group;
}

// #ignorance { ... } أو #ignorance stmt — يتجاهل أي خطأ وقت التشغيل داخلها ويكمل البرنامج بصمت
// (try { ... } catch { } مختصر). break/continue/return تعمل عبرها كالمعتاد.
StmtPtr Parser::hashIgnoranceStatement() {
    Token kw = advance(); // #ignorance
    auto st = std::make_shared<TryCatchStmt>();
    st->line = kw.line;
    if (match({TokenType::LBRACE})) {
        st->tryBranch = block();
    } else {
        auto blk = std::make_shared<BlockStmt>();
        blk->statements.push_back(statement());
        st->tryBranch = blk;
    }
    st->catchBranch = std::make_shared<BlockStmt>();
    return st;
}

// #swap a, b;  أو  #swap(a, b);  -> تبديل هدفين قابلين للإسناد (متغيّر، arr[i]، obj.f) فعلاً.
// تُفكَّك على مستوى التوكنات إلى الإسناد التفكيكي الموجود أصلاً:  [a, b] = [b, a];
// (فتعمل مع المتغيرات الحيّة وstone/#ban كأي إسناد عادي). #swap(arr, i, j) بثلاث وسائط = الدالة الأصلية.
StmtPtr Parser::hashSwapStatement() {
    const size_t at = current;
    const Token swapTok = tokens[at];
    size_t i = at + 1;
    bool paren = i < tokens.size() && tokens[i].type == TokenType::LPAREN;
    if (paren) i++;
    const size_t argStart = i;
    int depth = 0;
    std::vector<size_t> commas;
    size_t end = tokens.size();
    for (; i < tokens.size(); ++i) {
        TokenType t = tokens[i].type;
        if (t == TokenType::END_OF_FILE) break;
        if (t == TokenType::LPAREN || t == TokenType::LBRACKET || t == TokenType::LBRACE) depth++;
        else if (t == TokenType::RPAREN || t == TokenType::RBRACKET || t == TokenType::RBRACE) {
            if (depth == 0) { if (paren && t == TokenType::RPAREN) { end = i; break; } break; }
            depth--;
        } else if (t == TokenType::COMMA && depth == 0) commas.push_back(i);
        else if (t == TokenType::SEMICOLON && depth == 0) { if (!paren) { end = i; } break; }
    }
    size_t semi = end;
    if (paren) {
        if (end >= tokens.size() || tokens[end].type != TokenType::RPAREN) return nullptr;
        semi = end + 1;
        if (!(semi < tokens.size() && tokens[semi].type == TokenType::SEMICOLON)) return nullptr; // جزء من تعبير أكبر
        if (commas.size() != 1) return nullptr; // 3 وسائط (arr, i, j) -> الدالة الأصلية
    } else {
        if (end >= tokens.size() || tokens[end].type != TokenType::SEMICOLON || commas.size() != 1)
            throw errRich(diag::Code::E0012_MissingToken, swapTok, "'#swap' needs exactly two targets",
                          "the statement form swaps two variables (or elements) with each other",
                          "write `#swap a, b;` or `#swap arr[i], arr[j];`");
    }
    const size_t comma = commas[0];
    const size_t aEnd = comma, bStart = comma + 1, bEnd = paren ? end : end;
    if (aEnd <= argStart || bEnd <= bStart)
        throw errRich(diag::Code::E0013_InvalidExpression, swapTok, "'#swap' has an empty target",
                      "both sides of the comma must be something that can be assigned to",
                      "write `#swap a, b;`");
    std::vector<Token> A(tokens.begin() + argStart, tokens.begin() + aEnd);
    std::vector<Token> B(tokens.begin() + bStart, tokens.begin() + bEnd);
    auto mk = [&](TokenType t, const char* lex) { Token x = swapTok; x.type = t; x.lexeme = lex; return x; };
    std::vector<Token> out;
    out.push_back(mk(TokenType::LBRACKET, "["));
    out.insert(out.end(), A.begin(), A.end());
    out.push_back(mk(TokenType::COMMA, ","));
    out.insert(out.end(), B.begin(), B.end());
    out.push_back(mk(TokenType::RBRACKET, "]"));
    out.push_back(mk(TokenType::EQUAL, "="));
    out.push_back(mk(TokenType::LBRACKET, "["));
    out.insert(out.end(), B.begin(), B.end());
    out.push_back(mk(TokenType::COMMA, ","));
    out.insert(out.end(), A.begin(), A.end());
    out.push_back(mk(TokenType::RBRACKET, "]"));
    size_t replaceEnd = paren ? semi : end; // الفاصلة المنقوطة تبقى لتُستهلك من expressionStatement
    tokens.erase(tokens.begin() + at, tokens.begin() + replaceEnd);
    tokens.insert(tokens.begin() + at, out.begin(), out.end());
    return expressionStatement();
}

StmtPtr Parser::returnStatement() {
    ExprPtr value = nullptr;
    if (!check(TokenType::SEMICOLON) && !check(TokenType::IF)) value = expression();
    ExprPtr guard = trailingGuard(); // return x when (c);  /  return if (c);
    consume(TokenType::SEMICOLON, "Expected ';' after return value");
    auto stmt = std::make_shared<ReturnStmt>();
    stmt->value = value;
    return guarded(stmt, guard);
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
    if (check(TokenType::IDENT) && checkNext(TokenType::SEMICOLON) ) return labeledJump(true, tok);
    if (check(TokenType::IDENT) && !labels.empty() && peek().lexeme != "when" && peek().lexeme != "unless") return labeledJump(true, tok);
    ExprPtr guard = trailingGuard(); // break when (c);
    consume(TokenType::SEMICOLON, "Expected ';' after 'break'");
    auto stmt = std::make_shared<BreakStmt>();
    stmt->line = tok.line;
    return guarded(stmt, guard);
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
    if (check(TokenType::IDENT) && checkNext(TokenType::SEMICOLON) ) return labeledJump(false, tok);
    if (check(TokenType::IDENT) && !labels.empty() && peek().lexeme != "when" && peek().lexeme != "unless") return labeledJump(false, tok);
    ExprPtr guard = trailingGuard(); // continue unless (c);
    consume(TokenType::SEMICOLON, "Expected ';' after 'continue'");
    auto stmt = std::make_shared<ContinueStmt>();
    stmt->line = tok.line;
    return guarded(stmt, guard);
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
                                     w == "chatbot" || w == "everything" || w == "make" || w == "sql");
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

bool Parser::openBraceIfSameLine() {
    // '{' تُعدّ فتحاً للكتلة فقط إن وقعت على نفس سطر نهاية الترويسة (K&R). '{' على سطر جديد تبقى
    // عبارة عادية (كتلة محلية) داخل الشكل القديم — فلا يتغيّر معنى أي كود قديم.
    if (current == 0 || !check(TokenType::LBRACE)) return false;
    if (peek().line != previous().line) return false;
    advance();
    return true;
}

bool Parser::checkClosingTag() const {
    if (current >= tokens.size()) return false;
    bool dotEnd = tokens[current].type == TokenType::DOT && current + 1 < tokens.size() &&
                  tokens[current + 1].type == TokenType::END;
    if (!frames.empty()) {
        // داخل كتلة: نُنهي الحلقة عند أي من الإغلاقين، ويقرّر consumeEndTag إن كان مطابقاً لأسلوب الفتح،
        // فيُعطي المبرمج رسالة دقيقة بدل خطأ "Expected expression" عامّ حين يخلط الأسلوبين.
        if (tokens[current].type == TokenType::RBRACE) return true;
        return dotEnd;
    }
    return dotEnd;
}

void Parser::consumeEndTag(const std::string& expectedTag, int openLine, const std::string& openName) {
    if (!frames.empty() && frames.back().brace) {
        // كتلة أقواس: '@tag ... {  ...  }'  — الإغلاق '}' حصراً، مع تسمية تحقّق اختيارية '}=name'.
        if (isAtEnd()) {
            auto d = err(diag::Code::E0012_MissingToken, peek(),
                         "`@" + expectedTag + "` opened with `{` at line " + std::to_string(openLine) +
                         " was never closed");
            d.diagnostic->withReason("the file ended while this block was still open")
             .withHint("add a closing `}` for the `@" + expectedTag + "` block that starts at line " + std::to_string(openLine));
            throw d;
        }
        if (!check(TokenType::RBRACE)) {
            Token bad = peek();
            auto d = err(diag::Code::E0011_UnexpectedToken, bad,
                         "`@" + expectedTag + "` opened with `{` at line " + std::to_string(openLine) +
                         " must be closed with `}`, not a `.end` tag");
            d.diagnostic->withReason("a block opened with `{` and one opened without it use different closers; "
                                     "mixing them is rejected so a block can never be closed by the wrong marker")
             .withHint("replace the `.end/...` tag with `}`");
            throw d;
        }
        advance(); // '}'
        // ذيل C++ القياسي '};' (مثل class/struct): ';' اختيارية وتُستهلك فقط إن وقعت على نفس سطر '}'.
        // الشكل الموصى به '};' ويبقى '}' وحدها صالحاً. تُعالَج بعد '=name' الاختياري أدناه أيضاً.
        auto eatTrailingSemicolon = [&]() {
            if (check(TokenType::SEMICOLON) && peek().line == previous().line) advance();
        };
        if (check(TokenType::EQUAL) && peek().line == previous().line) {
            Token afterBrace = peek();
            std::string closingName = readOptionalName("a closing brace (`}=name`)");
            if (openName.empty() && !closingName.empty()) {
                auto d = err(diag::Code::E0011_UnexpectedToken, afterBrace,
                             "closing `}=" + closingName + "` specifies a name, but the opening `@" + expectedTag + "` has none");
                d.diagnostic->withReason("`@" + expectedTag + "` was opened at line " + std::to_string(openLine) + " with no name")
                 .withHint("remove `=" + closingName + "` or name the opening block");
                throw d;
            }
            if (!closingName.empty() && closingName != openName) {
                auto d = err(diag::Code::E0011_UnexpectedToken, afterBrace,
                             "closing `}=" + closingName + "` does not match the opening name `" + openName + "`");
                d.diagnostic->withReason("`@" + expectedTag + "=" + openName + "` was opened at line " + std::to_string(openLine))
                 .withHint("use `}=" + openName + "` instead");
                throw d;
            }
        }
        eatTrailingSemicolon();
        return;
    }
    if (check(TokenType::RBRACE)) {
        Token bad = peek();
        auto d = err(diag::Code::E0011_UnexpectedToken, bad,
                     "`@" + expectedTag + "` opened at line " + std::to_string(openLine) +
                     " without `{` must be closed with `.end/" + expectedTag + "` (or `.end;`), not `}`");
        d.diagnostic->withReason("the block was opened in the `.end` style (no `{` on its header line)")
         .withHint("either add `{` at the end of the `@" + expectedTag + "` header line, or close it with `.end/" + expectedTag + "`");
        throw d;
    }
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

// Indsintime rendering engine: view strands + reactive state -----------------------------
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

    FrameGuard frame(*this, openBraceIfSameLine(), viewTok.line);
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
    FrameGuard frame(*this, openBraceIfSameLine(), openTok.line);
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
    FrameGuard frame(*this, openBraceIfSameLine(), openTok.line);
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

// mask = expr;
StmtPtr Parser::maskDeclaration() {
    Token tok = previous();
    consume(TokenType::EQUAL, "Expected '=' after mask");
    ExprPtr value = expression();
    consume(TokenType::SEMICOLON, "Expected ';' after mask declaration");
    auto s = std::make_shared<MaskStmt>();
    s->value = value; s->line = tok.line;
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

// Rin Indsin: Theme (Pattern Book) declaration -------------------------------------------
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

    FrameGuard frame(*this, openBraceIfSameLine(), themeTok.line);
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

bool Parser::tryParsePolicyDirective(PolicyAccumulator& s) {
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
// فقط هذه (+ Containers.Group بشكل مستقل، انظر groupTag في atBlock()) تخضع لتوجيهات
// policy_block؛ Volume يبقى كما هو (خارج نطاق هذه المرحلة، لا حاجة/طلب له حالياً).
static bool isContainerFamilyTag(const std::string& tag) {
    static const std::unordered_set<std::string> s = {
        "container", "container.pipe", "pipe", "container.data", "data",
        "container.api", "api", "container.import",
        "container.table", "table", "container.doc", "doc",
        "container.object", "Object", "container.open/object",
        "container.portal", "portal", "container.block", "block",
        "container.sticker", "sticker", "container.aukt", "AUKT",
        "container.chatbot", "chatbot",
        // RIN CONTAINER SQL (RCSQL): استعلام مُسمّى/قابل لإعادة الاستخدام عبر مسك — انظر
        // rin_container_sql.h وContainerKind::SQL أعلى ContainerStmt في rin_ast.h.
        "container.sql", "sql",
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

    FrameGuard frame(*this, openBraceIfSameLine(), atTok.line);
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
                    Token v = consumeStaticText( "Expected a string after Make metadata field");
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
        // RIN CONTAINER SQL: انظر ContainerKind::SQL في rin_ast.h ورين rin_container_sql.h للصياغة.
        "container.sql", "sql",
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
                                    "container.chatbot, container.sql, container.make (or make / Rin.make / the legacy "
                                    "container.everything / Everything), Containers.Group, or Volume");
        }
        throw d;
    }
    std::string name = readOptionalName("a `@" + tag + "` block");
    std::vector<StmtPtr> body;
    std::string mask;
    // RCS-1.0 §7 Phase 0/1: قبل كل عبارة عادية، نجرّب أولاً قراءتها كتوجيه سياسة (policy_block)
    // إن كانت هذه حاوية من عائلة container أو @Containers.Group (Volume تبقى خارج النطاق حالياً،
    // لا حاجة/طلب لها هناك). tryParsePolicyDirective يستعيد موضع القارئ بنفسه إن لم يطابق النمط،
    // فلا خطر على أي برنامج قديم يستخدم هذه الكلمات كأسماء عادية. الحقول المُجمَّعة هنا (في
    // PolicyAccumulator محايد النوع -- انظر rin_parser.h) تُنسَخ لاحقاً إلى ContainerStmt أو
    // ContainerGroupStmt الفعلية بعد إنشائها أدناه، حسب أيّهما ينطبق على هذا الوسم.
    PolicyAccumulator policyAccum;
    bool familyTag = isContainerFamilyTag(tag);
    bool groupTag = (tag == "Containers.Group");
    FrameGuard frame(*this, openBraceIfSameLine(), atTok.line);
    while (!checkClosingTag() && !isAtEnd()) {
        if ((familyTag || groupTag) && tryParsePolicyDirective(policyAccum)) continue;
        auto st = declaration();
        if (auto m = std::dynamic_pointer_cast<MaskStmt>(st)) {
            if (auto lit = std::dynamic_pointer_cast<LiteralExpr>(m->value); lit && lit->kind == LiteralExpr::Kind::STRING) mask = lit->str;
            else body.push_back(st);
        } else body.push_back(st);
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
        tag == "container.sql" || tag == "sql" ||
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
            tag == "container.sticker" || tag == "sticker" ||
            tag == "container.sql" || tag == "sql")) validateDataContainerBody(body);
        auto s = std::make_shared<ContainerStmt>();
        s->name = name; s->mask = mask; s->body = body; s->line = atTok.line;
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
        else if (tag == "container.sql" || tag == "sql") s->kind = ContainerKind::SQL;
        else if (tag == "container.everything" || tag == "Everything" ||
                 tag == "container.make" || tag == "make" || tag == "Rin.make") s->kind = ContainerKind::EVERYTHING;
        else s->kind = ContainerKind::PLAIN;
        return s;
    }
    if (tag == "Containers.Group") {
        auto s = std::make_shared<ContainerGroupStmt>();
        s->name = name; s->mask = mask; s->body = body; s->line = atTok.line;
        if (policyAccum.hasPolicy) {
            s->uses = policyAccum.uses; s->needs = policyAccum.needs;
            s->allows = policyAccum.allows; s->denies = policyAccum.denies;
            s->version = policyAccum.version; s->description = policyAccum.description;
            s->strict = policyAccum.strict; s->hasPolicy = true;
        }
        return s;
    }
    auto s = std::make_shared<VolumeStmt>();
    s->name = name; s->mask = mask; s->body = body; s->line = atTok.line;
    return s;
}

// @import "lib/data.og.rin";          -> يستدعيها declaration() بعد استهلاك '@' و'import' مسبقاً
// @import "lib/data.og.rin" as data;  -> 'as' كلمة سياقية غير محجوزة أيضاً، تُقرأ يدوياً هنا فقط.
StmtPtr Parser::importStatement() {
    Token kw = previous(); // 'import' (للحصول على رقم السطر)
    Token pathTok = consumeStaticText(
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

// import { a, b, c } from "path";  (يُستدعى بعد استهلاك 'import' فقط، من declaration())
StmtPtr Parser::importSelectedStatement() {
    Token kw = previous(); // 'import' (للحصول على رقم السطر)
    consume(TokenType::LBRACE, "Expected '{' after 'import'");
    std::vector<std::string> names;
    if (!check(TokenType::RBRACE)) {
        do {
            names.push_back(consume(TokenType::IDENT, "Expected a name to import").lexeme);
        } while (match({TokenType::COMMA}));
    }
    consume(TokenType::RBRACE, "Expected '}' after import list");
    if (!(check(TokenType::IDENT) && peek().lexeme == "from")) {
        throw errRich(diag::Code::E0012_MissingToken, peek(), "expected 'from' after import list",
                      "'import { ... }' must be followed by 'from \"path\";' naming the module to "
                      "import those names from",
                      "add `from \"path\";` after the closing '}', e.g. `import { a, b } from \"lib/x.og.rin\";`",
                      "'from'");
    }
    advance(); // 'from'
    Token pathTok = consumeStaticText( "Expected a string path after 'from'");
    consume(TokenType::SEMICOLON, "Expected ';' after import statement");

    auto pathExpr = std::make_shared<LiteralExpr>();
    pathExpr->kind = LiteralExpr::Kind::STRING;
    pathExpr->str = pathTok.lexeme;
    pathExpr->line = pathTok.line;

    auto s = std::make_shared<ImportSelectedStmt>();
    s->names = std::move(names);
    s->path = pathExpr;
    s->line = kw.line;
    return s;
}
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
    FrameGuard frame(*this, openBraceIfSameLine(), secTok.line);
    while (!checkClosingTag() && !isAtEnd()) body.push_back(declaration());
    consumeEndTag("Section", secTok.line, name);
    auto s = std::make_shared<SectionStmt>();
    s->name = name; s->body = body; s->line = secTok.line;
    return s;
}

StmtPtr Parser::translationsBlock() {
    Token tTok = previous();
    std::vector<StmtPtr> body;
    FrameGuard frame(*this, openBraceIfSameLine(), tTok.line);
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
    auto s = std::make_shared<TranslationStmt>();
    Token langVal;
    if (atTemplate()) { s->langExpr = templateNameExpr(); }
    else { langVal = consume(TokenType::STRING, "Expected a text value for 'lang'"); s->lang = langVal.lexeme; }
    consume(TokenType::TEXT, "Expected 'text' attribute after 'lang=\"...\"'");
    consume(TokenType::EQUAL, "Expected '=' after 'text'");
    Token textVal;
    if (atTemplate()) { s->textExpr = templateNameExpr(); }
    else { textVal = consume(TokenType::STRING, "Expected a text value for 'text'"); s->text = textVal.lexeme; }
    consume(TokenType::SEMICOLON, "Expected ';' after translation statement");
    s->line = tok.line;
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
        Token val = consumeStaticText( "Expected a text value after 'link.id='");
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
        Token val = consumeStaticText( "Expected a text value after 'id='");
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

// let+: يحوّل تعبير مصفوفة (الطرف الأيسر من `=`) إلى LetPattern لإسناد تفكيكي.
static bool exprToAssignPattern(const ExprPtr& e, LetPattern& out, std::string& why) {
    if (auto v = std::dynamic_pointer_cast<VariableExpr>(e)) {
        out.kind = (v->name == "_") ? LetPattern::Kind::Skip : LetPattern::Kind::Name;
        out.name = v->name; out.line = v->line; return true;
    }
    if (auto a = std::dynamic_pointer_cast<AssignExpr>(e)) { // [a = 1] -> قيمة افتراضية
        out.kind = LetPattern::Kind::Name; out.name = a->name; out.defaultValue = a->value; out.line = a->line; return true;
    }
    if (auto arr = std::dynamic_pointer_cast<ArrayExpr>(e)) {
        out.kind = LetPattern::Kind::Array; out.line = arr->line;
        for (auto& el : arr->elements) {
            LetPattern sub;
            if (!exprToAssignPattern(el, sub, why)) return false;
            out.items.push_back(std::move(sub));
        }
        return true;
    }
    if (auto ix = std::dynamic_pointer_cast<IndexExpr>(e)) { // [arr[0], arr[1]] = [arr[1], arr[0]];
        auto set = std::make_shared<IndexSetExpr>();
        set->object = ix->object; set->index = ix->index; set->line = ix->line;
        auto tmp = std::make_shared<VariableExpr>(); tmp->name = "__pa_value"; tmp->line = ix->line;
        set->value = tmp;
        out.kind = LetPattern::Kind::Name; out.assignExpr = set; out.line = ix->line; return true;
    }
    if (auto ge = std::dynamic_pointer_cast<GetExpr>(e)) {   // [p.x, p.y] = [p.y, p.x];
        auto set = std::make_shared<SetExpr>();
        set->object = ge->object; set->name = ge->name; set->line = ge->line;
        auto tmp = std::make_shared<VariableExpr>(); tmp->name = "__pa_value"; tmp->line = ge->line;
        set->value = tmp;
        out.kind = LetPattern::Kind::Name; out.assignExpr = set; out.line = ge->line; return true;
    }
    why = "only existing variables, `arr[i]`, `obj.field`, and nested `[...]` lists of them can be assigned this way";
    return false;
}

ExprPtr Parser::assignment() {
    auto expr = pipeline();
    if (match({TokenType::QUESTION})) {
        Token q = previous();
        auto whenTrue = expression();
        consume(TokenType::COLON, "Expected ':' in conditional expression");
        auto whenFalse = assignment();
        auto c = std::make_shared<ConditionalExpr>();
        c->condition = expr; c->whenTrue = whenTrue; c->whenFalse = whenFalse; c->line = q.line;
        expr = c;
    }
    // Rin For+: x += e  -= *= /= %=  و x++ / x--  (حلّ في المحلل إلى  x = x op e  — يعمل على المفسّر وRinVM)
    TokenType cOp = TokenType::EQUAL; bool isCompound = false, isIncDec = false;
    { int ck = rinCompoundAhead(tokens, current); if (ck == 1) { isCompound = true; cOp = tokens[current].type; } else if (ck == 2) { isIncDec = true; cOp = tokens[current].type; } }
    if ((isCompound || isIncDec) && (std::dynamic_pointer_cast<VariableExpr>(expr) || std::dynamic_pointer_cast<IndexExpr>(expr) || std::dynamic_pointer_cast<GetExpr>(expr))) {
        Token opTok = advance();
        if (!isSimpleTarget(expr))
            throw errRich(diag::Code::E0003_InvalidAssignment, opTok, "compound assignment needs a simple target",
                          "`a op= b` and `a++` read and write the target, so the target must have no side effects (it is evaluated twice)",
                          "use a variable, `arr[i]`, or `obj.field` with plain names/numbers; otherwise write `t = f(); t op= b;`");
        ExprPtr rhs;
        if (isIncDec) { advance(); { auto one = std::make_shared<LiteralExpr>(); one->kind = LiteralExpr::Kind::NUMBER; one->number = 1; one->line = opTok.line; rhs = one; } }
        else { advance(); rhs = assignment(); } // '='
        auto bin = std::make_shared<BinaryExpr>();
        bin->left = expr; bin->op = cOp; bin->right = rhs; bin->line = opTok.line;
        ExprPtr value = bin;
        if (auto var = std::dynamic_pointer_cast<VariableExpr>(expr)) {
            auto as = std::make_shared<AssignExpr>(); as->name = var->name; as->value = value; as->line = opTok.line; return as;
        }
        if (auto idx = std::dynamic_pointer_cast<IndexExpr>(expr)) {
            auto set = std::make_shared<IndexSetExpr>(); set->object = idx->object; set->index = idx->index; set->value = value; set->line = opTok.line; return set;
        }
        auto get = std::dynamic_pointer_cast<GetExpr>(expr);
        auto set = std::make_shared<SetExpr>(); set->object = get->object; set->name = get->name; set->value = value; set->line = opTok.line; return set;
    }
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
        // OOP: object.name = value  ->  SetExpr (كتابة/تعديل حقل)، بنفس أسلوب IndexExpr->IndexSetExpr
        // أعلاه بالضبط: GetExpr التي بناها call() تُعاد صياغتها كهدف كتابة بدل هدف قراءة.
        if (auto get = std::dynamic_pointer_cast<GetExpr>(expr)) {
            auto set = std::make_shared<SetExpr>();
            set->object = get->object;
            set->name = get->name;
            set->value = value;
            set->line = eq.line;
            return set;
        }
        // let+: [a, b] = [b, a];  تفكيك على متغيّرات موجودة (تبديل القيم). الطرف الأيسر يُحوَّل من ArrayExpr
        // إلى LetPattern: أسماء، `_` للتخطّي، `[x = 1]` لقيمة افتراضية، وقوائم متداخلة.
        if (auto arr = std::dynamic_pointer_cast<ArrayExpr>(expr)) {
            LetPattern pat; std::string why;
            if (exprToAssignPattern(arr, pat, why)) {
                checkPatternDuplicates(pat, eq);
                auto pa = std::make_shared<PatternAssignExpr>();
                pa->pattern = std::move(pat);
                pa->value = value;
                pa->line = eq.line;
                return pa;
            }
            throw errRich(diag::Code::E0003_InvalidAssignment, eq, "invalid destructuring assignment target", why,
                          "use variables, `arr[i]`, `obj.field`, `_`, and nested `[...]` lists, e.g. `[a, b] = [b, a];`",
                          "a list of variable names");
        }
        throw errRich(diag::Code::E0003_InvalidAssignment, eq, "invalid assignment target",
                      "only a plain variable (`x = ...`), an index expression (`x[i] = ...`), or a "
                      "property (`x.name = ...`) can appear on the left of '='; the expression the "
                      "parser built for the left-hand side here is none of those",
                      "assign to a variable, an index expression, or a property instead, e.g. "
                      "`x = value;`, `x[0] = value;`, or `x.name = value;`",
                      "a variable name, an index expression, or a property");
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
    while (match({TokenType::EQUAL_EQUAL, TokenType::BANG_EQUAL, TokenType::NOT_SAME, TokenType::COPY_OF})) {
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
    for (;;) {
        if (match({TokenType::LESS, TokenType::LESS_EQUAL, TokenType::GREATER, TokenType::GREATER_EQUAL})) {
            auto op = previous().type;
            auto right = term();
            auto b = std::make_shared<BinaryExpr>();
            b->left = expr; b->op = op; b->right = right;
            expr = b;
            continue;
        }
        // Rin 1.0: `value instanceof ClassName` -> oop.isInstance(value, "ClassName")
        // كلمة سياقية (IDENT) يتبعها اسم نوع، فلا تتعارض مع أي متغيّر يحمل الاسم نفسه.
        if (check(TokenType::IDENT) && peek().lexeme == "instanceof" && checkNext(TokenType::IDENT)) {
            Token kw = advance();
            Token cls = advance();
            auto c = std::make_shared<CallExpr>();
            c->callee = "oop.isInstance";
            c->line = kw.line;
            auto lit = std::make_shared<LiteralExpr>();
            lit->kind = LiteralExpr::Kind::STRING;
            lit->str = cls.lexeme;
            c->args.push_back(expr);
            c->args.push_back(lit);
            expr = c;
            continue;
        }
        break;
    }
    return expr;
}

ExprPtr Parser::term() {
    auto expr = factor();
    while (!rinCompoundAhead(tokens, current) && match({TokenType::PLUS, TokenType::MINUS})) {
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
    while (!rinCompoundAhead(tokens, current) && match({TokenType::STAR, TokenType::SLASH, TokenType::PERCENT})) {
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
            if (!var) {
                // Not a plain name -> call whatever VALUE this expression evaluates to instead of
                // the old hard error (see CallValueExpr in rin_ast.h): covers `arr[0]()`,
                // `(fun(x){...})(1)`, `getFn()()`, etc.
                auto cv = std::make_shared<CallValueExpr>();
                cv->callee = expr;
                cv->line = previous().line;
                if (!check(TokenType::RPAREN)) {
                    do {
                        cv->args.push_back(expression());
                    } while (match({TokenType::COMMA}));
                }
                consume(TokenType::RPAREN, "Expected ')' after arguments");
                expr = cv;
                continue;
            }
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
        } else if (match({TokenType::DOT})) {
            // Namespace calls: make.qr(), container.make.qr(), artifact.info() ...
            // تُحوَّل إلى Callee string قبل إنشاء GetExpr، حتى تصل مباشرة إلى natives.
            Token dot = previous();
            // TEXT مسموح أيضاً كاسم خاصية (r.text) -- ناتج make.ocr / make.image.text يحمل حقل "text"؛ كان
            // يُرفَض سابقاً لأن `text` كلمة مفتاحية. إضافي بحت: لم يكن أي برنامج صالح يصل إلى هنا بهذا الرمز.
            Token nameTok = check(TokenType::IDENT) ? advance() : (check(TokenType::TEXT) ? advance() : consume(TokenType::FILE_KW, "Expected property or method name after '.'"));
            if (auto root = std::dynamic_pointer_cast<VariableExpr>(expr)) {
                std::string ns = root->name + "." + nameTok.lexeme;
                if (check(TokenType::LPAREN)) {
                    advance();
                    auto c = std::make_shared<CallExpr>();
                    c->callee = ns; c->line = dot.line;
                    if (!check(TokenType::RPAREN)) {
                        do { c->args.push_back(expression()); } while (match({TokenType::COMMA}));
                    }
                    consume(TokenType::RPAREN, "Expected ')' after arguments");
                    expr = c;
                    continue;
                }
                // Keep walking the namespace: make.qr.render(...) and container.make.qr(...)
                // are handled by the next DOT iteration when there is no call yet.
                auto ge = std::make_shared<GetExpr>();
                ge->object = expr; ge->name = nameTok.lexeme; ge->line = dot.line;
                expr = ge;
            } else if (auto chain = std::dynamic_pointer_cast<GetExpr>(expr)) {
                // Namespace chain beyond two segments is represented as a GetExpr chain; if the
                // final segment is called, flatten it to the same native callee convention.
                auto callRoot = std::dynamic_pointer_cast<VariableExpr>(chain->object);
                (void)callRoot;
                if (match({TokenType::LPAREN})) {
                    std::function<std::string(const ExprPtr&)> namespaceName = [&](const ExprPtr& e) -> std::string {
                        if (auto v = std::dynamic_pointer_cast<VariableExpr>(e)) return v->name;
                        if (auto g = std::dynamic_pointer_cast<GetExpr>(e)) return namespaceName(g->object) + "." + g->name;
                        return std::string();
                    };
                    std::string ns = namespaceName(chain) + "." + nameTok.lexeme;
                    auto c = std::make_shared<CallExpr>(); c->callee = ns; c->line = dot.line;
                    if (!check(TokenType::RPAREN)) { do { c->args.push_back(expression()); } while (match({TokenType::COMMA})); }
                    consume(TokenType::RPAREN, "Expected ')' after arguments");
                    expr = c; continue;
                }
                auto ge = std::make_shared<GetExpr>(); ge->object = expr; ge->name = nameTok.lexeme; ge->line = dot.line; expr = ge;
            } else if (match({TokenType::LPAREN})) {
                auto mc = std::make_shared<MethodCallExpr>();
                mc->object = expr;
                mc->method = nameTok.lexeme;
                mc->line = dot.line;
                if (!check(TokenType::RPAREN)) {
                    do {
                        mc->args.push_back(expression());
                    } while (match({TokenType::COMMA}));
                }
                consume(TokenType::RPAREN, "Expected ')' after method arguments");
                expr = mc;
            } else {
                auto ge = std::make_shared<GetExpr>();
                ge->object = expr;
                ge->name = nameTok.lexeme;
                ge->line = dot.line;
                expr = ge;
            }
        } else {
            break;
        }
    }
    return expr;
}

ExprPtr Parser::primary() {
    if (atReducer(current)) return reduceComprehension(); // sum each x in xs yield e  (Loops v3)
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
    // fun(params) { body } as an EXPRESSION -> anonymous function literal (see LambdaExpr in
    // rin_ast.h). primary() only runs where an expression is expected, so this can never fire for
    // a statement-starting `fun name(...) { ... }` (that is consumed by Parser::declaration()/
    // functionDeclaration() before statement/expression parsing is ever reached) -- purely
    // additive, no existing 'fun' parse path changes.
    if (check(TokenType::FUN)) {
        Token funTok = advance(); // 'fun'
        consume(TokenType::LPAREN, "Expected '(' after 'fun'");
        std::vector<std::string> params;
        std::vector<std::string> paramTypes;
        if (!check(TokenType::RPAREN)) {
            do {
                params.push_back(consume(TokenType::IDENT, "Expected parameter name").lexeme);
                std::string paramType;
                if (match({TokenType::COLON})) {
                    paramType = consume(TokenType::IDENT, "Expected a type name after ':'").lexeme;
                }
                paramTypes.push_back(paramType);
            } while (match({TokenType::COMMA}));
        }
        consume(TokenType::RPAREN, "Expected ')' after parameters");
        std::string returnType;
        if (match({TokenType::COLON})) {
            returnType = consume(TokenType::IDENT, "Expected a type name after ':'").lexeme;
        }
        consume(TokenType::LBRACE, "Expected '{' before function body");
        int savedLoopDepth = loopDepth;
    auto savedLS_ = std::move(loopStack); auto savedLB_ = std::move(labels); loopStack.clear(); labels.clear();
        int savedGoalDepth = goalDepth;
        loopDepth = 0;
        goalDepth = 0;
        auto body = block();
        loopDepth = savedLoopDepth; loopStack = std::move(savedLS_); labels = std::move(savedLB_);
        goalDepth = savedGoalDepth;
        auto fn = std::make_shared<FunctionStmt>();
        fn->name = ""; // anonymous
        fn->params = params;
        fn->paramTypes = paramTypes;
        fn->returnType = returnType;
        fn->body = body;
        fn->line = funTok.line;
        auto lambda = std::make_shared<LambdaExpr>();
        lambda->decl = fn;
        lambda->line = funTok.line;
        return lambda;
    }
    // goal { ... } -> كتلة هدف تُقيَّم كتعبير (انظر GoalExpr/AchieveStmt في rin_ast.h للشرح
    // الكامل). يجب فحصها *قبل* IDENT العام أدناه وإلا لالتُقِطت "goal" كاسم متغيّر عادي. 'goal'
    // كلمة سياقية غير محجوزة، مُميَّزة فقط عند ظهورها IDENT("goal") متبوعة مباشرة بـ '{' — فاستخدام
    // "goal" اسم متغيّر عادي (goal + 1، goal(x)، let goal = 5، ...) يبقى يعمل بلا أي تغيير.
    if (check(TokenType::IDENT) && peek().lexeme == "goal" && checkNext(TokenType::LBRACE)) {
        Token goalTok = advance(); // 'goal'
        advance(); // '{'
        auto e = std::make_shared<GoalExpr>();
        e->line = goalTok.line;
        goalDepth++;
        e->body = block(); // يستهلك '}' المطابقة بنفسه
        goalDepth--;
        return e;
    }
    // `show(x)` is lexed as PRINT (keyword synonym of print) but in EXPRESSION position followed by '('
    // it is the built-in indsin action show(warpCell) (onTap=show(dialogOpen);) -- treat as identifier.
    if (check(TokenType::PRINT) && peek().lexeme == "show" && checkNext(TokenType::LPAREN)) tokens[current].type = TokenType::IDENT;
    if (match({TokenType::IDENT, TokenType::CONTAINER})) {
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
        if (atEachForm(current)) {
            auto comp = eachComprehension();
            consume(TokenType::RBRACKET, "Expected ']' after array comprehension");
            return comp;
        }
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
        if (atEachForm(current)) { // {each x in xs yield key: value}  (Loops v3)
            auto comp = mapComprehension();
            consume(TokenType::RBRACE, "Expected '}' after map comprehension");
            return comp;
        }
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


// ============================================================================
// Loops v2 — each / repeat / loop / retry / labels (`outer:` + `break outer;`) / `limit N` / [each ... => e]
// كلها تفكيك (desugaring) إلى WhileStmt/ForStmt/ForInStmt/BlockStmt/IfStmt الموجودة، فتعمل بلا أي تعديل
// على المفسّر الشجري أو RinVM. انظر docs/loops.md.
// ============================================================================
namespace {
using SP = std::shared_ptr<Stmt>;
ExprPtr lNum(double v, int line) { auto l = std::make_shared<LiteralExpr>(); l->kind = LiteralExpr::Kind::NUMBER; l->number = v; l->line = line; return l; }
ExprPtr lStr(const std::string& v, int line) { auto l = std::make_shared<LiteralExpr>(); l->kind = LiteralExpr::Kind::STRING; l->str = v; l->line = line; return l; }
ExprPtr lBool(bool v, int line) { auto l = std::make_shared<LiteralExpr>(); l->kind = LiteralExpr::Kind::BOOL; l->boolean = v; l->line = line; return l; }
ExprPtr lVar(const std::string& n, int line) { auto v = std::make_shared<VariableExpr>(); v->name = n; v->line = line; return v; }
ExprPtr lBin(ExprPtr a, TokenType op, ExprPtr b, int line) { auto e = std::make_shared<BinaryExpr>(); e->left = a; e->op = op; e->right = b; e->line = line; return e; }
ExprPtr lAssign(const std::string& n, ExprPtr v, int line) { auto e = std::make_shared<AssignExpr>(); e->name = n; e->value = v; e->line = line; return e; }
ExprPtr lCall(const std::string& f, std::vector<ExprPtr> args, int line) { auto c = std::make_shared<CallExpr>(); c->callee = f; c->args = std::move(args); c->line = line; return c; }
StmtPtr lLet(const std::string& n, ExprPtr init, int line) { auto s = std::make_shared<LetStmt>(); s->name = n; s->initializer = init; s->line = line; return s; }
StmtPtr lExpr(ExprPtr e, int line) { auto s = std::make_shared<ExpressionStmt>(); s->expr = e; s->line = line; return s; }
StmtPtr lIf(ExprPtr c, StmtPtr t, StmtPtr e, int line) { auto s = std::make_shared<IfStmt>(); s->condition = c; s->thenBranch = t; s->elseBranch = e; s->line = line; return s; }
std::shared_ptr<BlockStmt> lBlock(std::vector<StmtPtr> v, int line) { auto b = std::make_shared<BlockStmt>(); b->statements = std::move(v); b->line = line; return b; }
StmtPtr lThrow(ExprPtr e, int line) { auto s = std::make_shared<ThrowStmt>(); s->value = e; s->line = line; return s; }
ExprPtr lUnaryNot(ExprPtr e, int line) { auto n = std::make_shared<UnaryExpr>(); n->op = TokenType::BANG; n->right = e; n->line = line; return n; }
std::shared_ptr<LogicalExpr> lLogicalAnd(ExprPtr a, ExprPtr b) { auto l = std::make_shared<LogicalExpr>(); l->left = a; l->op = TokenType::AND; l->right = b; l->line = a->line; return l; }
StmtPtr lBreak(int line) { auto s = std::make_shared<BreakStmt>(); s->line = line; return s; }
StmtPtr lContinue(int line) { auto s = std::make_shared<ContinueStmt>(); s->line = line; return s; }
StmtPtr asBlock(StmtPtr s, int line) {
    if (s && s->stmtKind == StmtKind::BlockStmt) return s;
    std::vector<StmtPtr> v; if (s) v.push_back(s);
    return lBlock(std::move(v), line);
}
// يُرجع مرجع جسم الحلقة القابل للتعديل (نفس الـ AST الذي بُني للتو)
StmtPtr* loopBodySlot(const StmtPtr& loop) {
    if (loop->stmtKind == StmtKind::WhileStmt) return &std::static_pointer_cast<WhileStmt>(loop)->body;
    if (loop->stmtKind == StmtKind::ForStmt) return &std::static_pointer_cast<ForStmt>(loop)->body;
    if (loop->stmtKind == StmtKind::ForInStmt) return &std::static_pointer_cast<ForInStmt>(loop)->body;
    return nullptr;
}
} // namespace

void Parser::enterLoop() {
    loopDepth++;
    LoopCtx c; c.depth = loopDepth;
    if (!pendingLabel.empty()) {
        c.label = pendingLabel; c.flag = "#j" + std::to_string(loopSeq++);
        LabelInfo li; li.name = pendingLabel; li.flag = c.flag; li.depth = loopDepth;
        labels.push_back(li);
        pendingLabel.clear();
    }
    loopStack.push_back(c);
}

void Parser::exitLoop() {
    loopDepth--;
    if (loopStack.empty()) return;
    LoopCtx c = loopStack.back();
    loopStack.pop_back();
    if (!c.label.empty()) {
        for (size_t i = labels.size(); i-- > 0;) if (labels[i].flag == c.flag) { c.label = c.label; labels.erase(labels.begin() + i); break; }
    }
    doneStack.push_back(c);
}

// يُستدعى بعد اكتمال الحلقة (بما فيها #done): يضيف حدّ الأمان والعلامات ونقل break/continue ذوات الأسماء.
StmtPtr Parser::finishLoop(StmtPtr loop, ExprPtr limit) {
    if (doneStack.empty()) return loop;
    LoopCtx c = doneStack.back();
    doneStack.pop_back();
    int line = loop->line;
    std::vector<StmtPtr> out;
    if (!c.label.empty() && labelFlagUsed.count(c.flag)) out.push_back(lLet(c.flag, lNum(0, line), line));
    if (limit) {
        const std::string cnt = "#lc" + std::to_string(loopSeq++);
        out.push_back(lLet(cnt, lNum(0, line), line));
        if (auto* slot = loopBodySlot(loop)) {
            auto tick = lExpr(lAssign(cnt, lBin(lVar(cnt, line), TokenType::PLUS, lNum(1, line), line), line), line);
            auto msg = lBin(lStr("loop limit exceeded: more than ", line), TokenType::PLUS, limit, line);
            msg = lBin(msg, TokenType::PLUS, lStr(" iterations", line), line);
            auto guard = lIf(lBin(lVar(cnt, line), TokenType::GREATER, limit, line), lBlock({lThrow(msg, line)}, line), nullptr, line);
            *slot = lBlock({tick, guard, *slot}, line);
        }
    }
    out.push_back(loop);
    for (auto& p : c.props) {
        auto f = [&]() { return lVar(p.flag, line); };
        if (c.depth - 1 == p.targetDepth) { // الأب المباشر هو الحلقة المسمّاة
            if (p.kind & 1) out.push_back(lIf(lBin(f(), TokenType::EQUAL_EQUAL, lNum(1, line), line), lBlock({lBreak(line)}, line), nullptr, line));
            if (p.kind & 2) out.push_back(lIf(lBin(f(), TokenType::EQUAL_EQUAL, lNum(2, line), line),
                                              lBlock({lExpr(lAssign(p.flag, lNum(0, line), line), line), lContinue(line)}, line), nullptr, line));
        } else {
            out.push_back(lIf(lBin(f(), TokenType::BANG_EQUAL, lNum(0, line), line), lBlock({lBreak(line)}, line), nullptr, line));
        }
    }
    if (out.size() == 1) return loop;
    auto b = lBlock(std::move(out), line);
    return b;
}

// limit N  (بين رأس الحلقة وجسمها)، يُميَّز عن عبارة جسم تبدأ باسم limit.
ExprPtr Parser::limitClause() {
    if (!(check(TokenType::IDENT) && peek().lexeme == "limit")) return nullptr;
    size_t k = current + 1;
    if (k >= tokens.size()) return nullptr;
    bool ok = false;
    if (tokens[k].type == TokenType::NUMBER) ok = true;
    else if (tokens[k].type == TokenType::IDENT && k + 1 < tokens.size() && tokens[k + 1].type == TokenType::LBRACE) ok = true;
    else if (tokens[k].type == TokenType::LPAREN) {
        int d = 0; size_t j = k;
        for (; j < tokens.size(); ++j) {
            if (tokens[j].type == TokenType::LPAREN) d++;
            else if (tokens[j].type == TokenType::RPAREN && --d == 0) break;
        }
        ok = j + 1 < tokens.size() && tokens[j + 1].type == TokenType::LBRACE;
    }
    if (!ok) return nullptr;
    advance(); // limit
    if (check(TokenType::NUMBER)) { Token t = advance(); return lNum(t.number, t.line); }
    if (check(TokenType::IDENT)) { Token t = advance(); return lVar(t.lexeme, t.line); }
    consume(TokenType::LPAREN, "Expected '(' after 'limit'");
    auto e = expression();
    consume(TokenType::RPAREN, "Expected ')' after limit expression");
    return e;
}

// break L; / continue L;
StmtPtr Parser::labeledJump(bool isBreak, const Token& tok) {
    Token lab = advance();
    int idx = -1;
    for (size_t i = labels.size(); i-- > 0;) if (labels[i].name == lab.lexeme) { idx = (int)i; break; }
    if (idx < 0)
        throw errRich(diag::Code::E0011_UnexpectedToken, lab, "unknown loop label '" + lab.lexeme + "'",
                      "`" + std::string(isBreak ? "break" : "continue") + " " + lab.lexeme + "` can only name a loop that encloses it in the same function",
                      "label an enclosing loop first:  `" + lab.lexeme + ": for (...) { ... }`");
    ExprPtr guard = trailingGuard();
    consume(TokenType::SEMICOLON, std::string("Expected ';' after '") + (isBreak ? "break" : "continue") + " label'");
    LabelInfo li = labels[idx];
    StmtPtr jump;
    if (li.depth == loopDepth) {
        jump = isBreak ? lBreak(tok.line) : lContinue(tok.line);
    } else {
        labelFlagUsed.insert(li.flag);
        for (int k = li.depth; k < loopDepth; ++k) {
            auto& ps = loopStack[k].props;
            bool found = false;
            for (auto& p : ps) if (p.flag == li.flag) { p.kind |= (isBreak ? 1 : 2); found = true; }
            if (!found) ps.push_back({li.flag, li.depth, isBreak ? 1 : 2});
        }
        jump = lBlock({lExpr(lAssign(li.flag, lNum(isBreak ? 1 : 2, tok.line), tok.line), tok.line), lBreak(tok.line)}, tok.line);
    }
    return guarded(jump, guard);
}

// `(`...`)` متوازنة تبدأ عند at ويتبعها '{' أو كلمة سياقية من الحلقات
bool Parser::atLoopForm(size_t at) const {
    if (at >= tokens.size()) return false;
    if (tokens[at].type == TokenType::LBRACE) return true;
    if (tokens[at].type == TokenType::IDENT && (tokens[at].lexeme == "limit" || tokens[at].lexeme == "as")) return true;
    return false;
}

StmtPtr Parser::loopsV2Statement() {
    if (!check(TokenType::IDENT)) return nullptr;
    const Token kwTok = peek();
    const std::string& kw = kwTok.lexeme;
    int line = kwTok.line;
    auto closeParen = [&](size_t from) -> size_t { // index of matching ')' for '(' at from
        int d = 0;
        for (size_t j = from; j < tokens.size(); ++j) {
            if (tokens[j].type == TokenType::LPAREN) d++;
            else if (tokens[j].type == TokenType::RPAREN && --d == 0) return j;
        }
        return tokens.size();
    };

    // ---- label:  name: <loop> ----
    if (checkNext(TokenType::COLON) && current + 2 < tokens.size()) {
        const Token& nx = tokens[current + 2];
        bool loopish = nx.type == TokenType::FOR || nx.type == TokenType::WHILE || nx.type == TokenType::RINOPEN ||
                       (nx.type == TokenType::IDENT && (nx.lexeme == "each" || nx.lexeme == "repeat" || nx.lexeme == "loop" ||
                        nx.lexeme == "retry" || nx.lexeme == "#do" || nx.lexeme == "#while" || nx.lexeme == "#for"));
        if (loopish) {
            advance(); advance(); // name ':'
            pendingLabel = kw;
            StmtPtr st = declaration();
            pendingLabel.clear();
            return st;
        }
    }

    // ---- loop { } / loop (cond) { } / loop until (cond) { } ----
    if (kw == "loop") {
        if (checkNext(TokenType::LBRACE) || (current + 1 < tokens.size() && tokens[current + 1].type == TokenType::IDENT && tokens[current + 1].lexeme == "limit" && !(current + 2 < tokens.size() && tokens[current + 2].type == TokenType::EQUAL))) {
            advance();
            ExprPtr lim = limitClause();
            consume(TokenType::LBRACE, "Expected '{' after 'loop'");
            enterLoop();
            auto body = block();
            exitLoop();
            auto w = std::make_shared<WhileStmt>();
            w->condition = lBool(true, line); w->body = body; w->line = line;
            attachDoneBlock(w->doneBlock);
            return finishLoop(w, lim);
        }
        bool until = current + 2 < tokens.size() && tokens[current + 1].type == TokenType::IDENT && tokens[current + 1].lexeme == "until" && tokens[current + 2].type == TokenType::LPAREN;
        size_t lp = until ? current + 2 : current + 1;
        if (lp < tokens.size() && tokens[lp].type == TokenType::LPAREN) {
            size_t rp = closeParen(lp);
            if (rp + 1 < tokens.size() && atLoopForm(rp + 1) && tokens[rp + 1].lexeme != "as") {
                advance(); if (until) advance();
                consume(TokenType::LPAREN, "Expected '(' after 'loop'");
                auto cond = expression();
                consume(TokenType::RPAREN, "Expected ')' after loop condition");
                if (until) { auto n = std::make_shared<UnaryExpr>(); n->op = TokenType::BANG; n->right = cond; n->line = line; cond = n; }
                ExprPtr lim = limitClause();
                enterLoop();
                auto body = statement();
                exitLoop();
                auto w = std::make_shared<WhileStmt>();
                w->condition = cond; w->body = body; w->line = line;
                attachDoneBlock(w->doneBlock);
                return finishLoop(w, lim);
            }
        }
        return nullptr;
    }

    // ---- repeat N [as i] [limit M] { } ----
    if (kw == "repeat") {
        size_t k = current + 1;
        bool isForm = false;
        if (k < tokens.size()) {
            if (tokens[k].type == TokenType::NUMBER) isForm = k + 1 < tokens.size() && atLoopForm(k + 1);
            else if (tokens[k].type == TokenType::IDENT) isForm = k + 1 < tokens.size() && (tokens[k + 1].type == TokenType::LBRACE || (tokens[k + 1].type == TokenType::IDENT && (tokens[k + 1].lexeme == "as" || tokens[k + 1].lexeme == "times")));
            else if (tokens[k].type == TokenType::LPAREN) { size_t rp = closeParen(k); isForm = rp + 1 < tokens.size() && atLoopForm(rp + 1); }
        }
        if (!isForm) return nullptr;
        advance(); // repeat
        ExprPtr count;
        if (check(TokenType::NUMBER)) { Token t = advance(); count = lNum(t.number, t.line);
            if (t.number < 0 || t.number != (double)(long long)t.number)
                throw errRich(diag::Code::E0013_InvalidExpression, t, "`repeat` needs a non-negative integer count",
                              "the count of a `repeat` loop is the number of executions", "use a whole number such as `repeat 5 { ... }`"); }
        else if (check(TokenType::IDENT)) { Token t = advance(); count = lVar(t.lexeme, t.line); }
        else { consume(TokenType::LPAREN, "Expected '(' after 'repeat'"); count = expression(); consume(TokenType::RPAREN, "Expected ')' after repeat count"); }
        if (check(TokenType::IDENT) && peek().lexeme == "times") advance();
        std::string idx = "#ri" + std::to_string(loopSeq++);
        if (check(TokenType::IDENT) && peek().lexeme == "as") { advance(); idx = consume(TokenType::IDENT, "Expected index name after 'as'").lexeme; }
        ExprPtr lim = limitClause();
        const std::string n = "#rn" + std::to_string(loopSeq++);
        enterLoop();
        auto body = statement();
        exitLoop();
        auto f = std::make_shared<ForStmt>();
        f->initializer = lLet(idx, lNum(0, line), line);
        f->condition = lBin(lVar(idx, line), TokenType::LESS, lVar(n, line), line);
        f->increment = lAssign(idx, lBin(lVar(idx, line), TokenType::PLUS, lNum(1, line), line), line);
        f->body = body; f->line = line;
        attachDoneBlock(f->doneBlock);
        StmtPtr loop = finishLoop(f, lim);
        std::vector<StmtPtr> out;
        out.push_back(lLet(n, count, line));
        auto badL = std::make_shared<LogicalExpr>(); badL->left = lUnaryNot(lCall("isInt", {lVar(n, line)}, line), line); badL->op = TokenType::OR; badL->right = lBin(lVar(n, line), TokenType::LESS, lNum(0, line), line); badL->line = line; ExprPtr bad = badL;
        out.push_back(lIf(bad, lBlock({lThrow(lStr("repeat count must be a non-negative integer", line), line)}, line), nullptr, line));
        out.push_back(loop);
        return lBlock(std::move(out), line);
    }

    // ---- retry N [as n] { body } [#done { }]  — يكرّر الجسم حتى ينجح (بلا throw)؛ بعد N محاولة يُعاد رمي الخطأ الأخير ----
    if (kw == "retry") {
        size_t k = current + 1;
        bool isForm = false;
        if (k < tokens.size()) {
            if (tokens[k].type == TokenType::NUMBER) isForm = k + 1 < tokens.size() && atLoopForm(k + 1);
            else if (tokens[k].type == TokenType::IDENT) isForm = k + 1 < tokens.size() && (tokens[k + 1].type == TokenType::LBRACE || (tokens[k + 1].type == TokenType::IDENT && tokens[k + 1].lexeme == "as"));
            else if (tokens[k].type == TokenType::LPAREN) { size_t rp = closeParen(k); isForm = rp + 1 < tokens.size() && atLoopForm(rp + 1); }
        }
        if (!isForm) return nullptr;
        advance();
        ExprPtr count;
        if (check(TokenType::NUMBER)) { Token t = advance(); count = lNum(t.number, t.line); }
        else if (check(TokenType::IDENT)) { Token t = advance(); count = lVar(t.lexeme, t.line); }
        else { consume(TokenType::LPAREN, "Expected '('"); count = expression(); consume(TokenType::RPAREN, "Expected ')'"); }
        std::string att = "#ra" + std::to_string(loopSeq++);
        if (check(TokenType::IDENT) && peek().lexeme == "as") { advance(); att = consume(TokenType::IDENT, "Expected attempt name after 'as'").lexeme; }
        const std::string id = std::to_string(loopSeq++);
        const std::string n = "#rn" + id, okv = "#rok" + id, err = "#rer" + id, cnt = "#rc" + id;
        consume(TokenType::LBRACE, "Expected '{' after 'retry'");
        enterLoop();
        auto body = block();
        exitLoop();
        // for (let att = 1; !ok; att = att + 1) { try { body; ok = true; } catch (e) { err = e; if (att >= n) throw e; } }
        auto tc = std::make_shared<TryCatchStmt>();
        tc->line = line;
        body->statements.push_back(lExpr(lAssign(okv, lBool(true, line), line), line));
        tc->tryBranch = body;
        tc->catchName = "#re" + id;
        tc->catchBranch = lBlock({lIf(lBin(lVar(att, line), TokenType::GREATER_EQUAL, lVar(n, line), line),
                                       lBlock({lThrow(([&]() { auto ix = std::make_shared<IndexExpr>(); ix->object = lVar(tc->catchName, line); ix->index = lStr("value", line); ix->line = line; return ExprPtr(ix); })(), line)}, line), nullptr, line)}, line);
        auto f = std::make_shared<ForStmt>();
        f->initializer = lLet(att, lNum(1, line), line);
        f->condition = lUnaryNot(lVar(okv, line), line);
        f->increment = lAssign(att, lBin(lVar(att, line), TokenType::PLUS, lNum(1, line), line), line);
        f->body = lBlock({tc}, line); f->line = line;
        StmtPtr loop = finishLoop(f);
        return lBlock({lLet(n, count, line), lLet(okv, lBool(false, line), line), loop}, line);
    }

    // ---- each [let] x [, i] in ITER [where C] [reversed] [limit N] { }  /  each [a, b] in ... / each {k} in ... ----
    if (kw == "each") {
        if (!atEachForm(current)) return nullptr;
        advance(); // each
        EachHdr h = parseEachHdr();
        ExprPtr lim = limitClause();
        enterLoop();
        auto body = statement();
        exitLoop();
        return buildEach(h, body, line, lim);
    }
    return nullptr;
}

bool Parser::atEachForm(size_t at, bool forKw) const {
    size_t k = at + 1;
    if (at >= tokens.size() || k >= tokens.size()) return false;
    if (forKw ? tokens[at].type != TokenType::FOR : (tokens[at].type != TokenType::IDENT || tokens[at].lexeme != "each")) return false;
    if (tokens[k].type == TokenType::LET) k++;
    if (k < tokens.size() && tokens[k].type == TokenType::IDENT) {
        size_t j = k + 1;
        if (j + 2 < tokens.size() && tokens[j].type == TokenType::COMMA && tokens[j + 1].type == TokenType::IDENT) j += 2;
        return j < tokens.size() && tokens[j].type == TokenType::IDENT && tokens[j].lexeme == "in";
    }
    if (k < tokens.size() && (tokens[k].type == TokenType::LBRACKET || tokens[k].type == TokenType::LBRACE)) {
        int d = 0; size_t j = k;
        for (; j < tokens.size(); ++j) {
            auto t = tokens[j].type;
            if (t == TokenType::LBRACKET || t == TokenType::LBRACE) d++;
            else if ((t == TokenType::RBRACKET || t == TokenType::RBRACE) && --d == 0) break;
        }
        return j + 1 < tokens.size() && tokens[j + 1].type == TokenType::IDENT && tokens[j + 1].lexeme == "in";
    }
    return false;
}

// بعد 'each' (و'let' الاختيارية): الأسماء/النمط، in، المكرَّر، ثم معدِّلات بأي ترتيب (Loops v3):
//   تحويلات المصدر : reversed · sorted [by KEY] [desc] · unique · flat · chain XS
//   مرشّحات التيار : where C · skip N · take N · until C · while C   (تُطبَّق بالترتيب المكتوب)
//   أنماط (واحد فقط): by N · window N · every N · zip YS · cross YS · pairs
//   فهرس           : from N  (بداية الفهرس مع متغيّرين)
Parser::EachHdr Parser::parseEachHdr() {
    EachHdr h;
    if (check(TokenType::LET)) advance();
    if (check(TokenType::LBRACKET) || check(TokenType::LBRACE)) {
        Token open = peek();
        LetPattern pat = letPattern();
        checkPatternDuplicates(pat, open);
        h.pat = std::make_shared<LetPattern>(std::move(pat));
    } else {
        h.v1 = consume(TokenType::IDENT, "Expected loop variable after 'each'").lexeme;
        if (match({TokenType::COMMA})) h.v2 = consume(TokenType::IDENT, "Expected second loop variable").lexeme;
    }
    advance(); // 'in'
    h.iter = maybeHashRange(expression());
    // each x in xs, y in ys  — ضرب ديكارتي بحلقة واحدة (مثل itertools.product): break/continue تعمل كالمعتاد
    while (check(TokenType::COMMA) && current + 2 < tokens.size() && tokens[current + 1].type == TokenType::IDENT &&
           tokens[current + 2].type == TokenType::IDENT && tokens[current + 2].lexeme == "in") {
        advance();
        std::string nm = advance().lexeme;
        advance(); // in
        h.extra.push_back({nm, maybeHashRange(expression())});
    }
    // عدد/اسم/(تعبير) بعد كلمة المعدِّل — نفس قاعدة by/window/every القديمة
    auto countArg = [&](const std::string& kw) -> ExprPtr {
        if (check(TokenType::NUMBER)) { Token n = advance(); return lNum(n.number, n.line); }
        if (check(TokenType::IDENT)) { Token n = advance(); return lVar(n.lexeme, n.line); }
        consume(TokenType::LPAREN, "Expected a number, name or '(' after '" + kw + "'");
        ExprPtr e = expression();
        consume(TokenType::RPAREN, "Expected ')'");
        return e;
    };
    // الكلمة التالية معدِّل فعلاً لا بداية جسم/عبارة (مثل نداء take(...) أو إسناد)؟
    auto nextIsCountArg = [&]() {
        if (current + 1 >= tokens.size()) return false;
        auto t = tokens[current + 1].type;
        return t == TokenType::NUMBER || t == TokenType::IDENT || t == TokenType::LPAREN;
    };
    auto flagWordOk = [&]() { // sorted/unique/flat/pairs: ليست متبوعة بـ ( أو . أو =
        if (current + 1 >= tokens.size()) return true;
        auto t = tokens[current + 1].type;
        return t != TokenType::LPAREN && t != TokenType::DOT && t != TokenType::EQUAL;
    };
    auto oneMode = [&](const Token& t) {
        if (h.mode != 0)
            throw errRich(diag::Code::E0013_InvalidExpression, t, "only one of `by` / `window` / `every` / `zip` / `cross` / `pairs` per loop",
                          "each of them changes what one round hands you, so two cannot be combined",
                          "use one mode per loop (nest another `each` for the second one)");
    };
    for (;;) {
        const bool isWhileKw = check(TokenType::WHILE);
        if (!check(TokenType::IDENT) && !isWhileKw) break;
        const std::string w = peek().lexeme;
        if (!isWhileKw && w == "where") {
            advance();
            h.stages.push_back({0, expression()});
        } else if (!isWhileKw && w == "reversed") {
            Token t = advance();
            h.iter = lCall("reverse", {h.iter}, t.line);
        } else if (!isWhileKw && w == "sorted" && flagWordOk()) {
            Token t = advance();
            ExprPtr key;
            if (check(TokenType::IDENT) && peek().lexeme == "by") { advance(); key = expression(); }
            bool desc = false;
            if (check(TokenType::IDENT) && (peek().lexeme == "desc" || peek().lexeme == "descending")) { advance(); desc = true; }
            if (!key) {
                h.iter = lCall("#sorted", {h.iter, lBool(desc, t.line)}, t.line);
            } else {
                if (h.pat)
                    throw errRich(diag::Code::E0013_InvalidExpression, t, "`sorted by` needs a plain loop variable, not a pattern",
                                  "the key expression is evaluated once per item using the loop variable's name",
                                  "write `each p in people sorted by p[\"age\"] { ... }`");
                // iter := goal { let src = <iter>; let keys = []; for (v1 in src) push(keys, KEY); achieve #sortby(src, keys, desc); }
                const std::string id = std::to_string(loopSeq++);
                const std::string src = "#ss" + id, ks = "#sk" + id;
                auto fi = std::make_shared<ForInStmt>();
                fi->line = t.line; fi->varName = h.v1; fi->iterable = lVar(src, t.line);
                fi->body = lBlock({lExpr(lCall("push", {lVar(ks, t.line), key}, t.line), t.line)}, t.line);
                auto ach = std::make_shared<AchieveStmt>();
                ach->value = lCall("#sortby", {lVar(src, t.line), lVar(ks, t.line), lBool(desc, t.line)}, t.line); ach->line = t.line;
                auto g = std::make_shared<GoalExpr>(); g->line = t.line;
                g->body = lBlock({lLet(src, h.iter, t.line), lLet(ks, std::make_shared<ArrayExpr>(), t.line), fi, ach}, t.line);
                h.iter = g;
            }
        } else if (!isWhileKw && w == "distinct" && flagWordOk()) { // distinct  /  distinct on (expr)
            Token t = advance();
            ExprPtr key;
            if (check(TokenType::IDENT) && peek().lexeme == "on" && checkNext(TokenType::LPAREN)) {
                advance(); advance();
                key = expression();
                consume(TokenType::RPAREN, "Expected ')' after 'distinct on (...)'");
            }
            h.stages.push_back({5, key, t.line});
        } else if (!isWhileKw && w == "unique" && flagWordOk()) {
            Token t = advance();
            h.iter = lCall("#unique", {h.iter}, t.line);
        } else if (!isWhileKw && w == "flat" && flagWordOk()) {
            Token t = advance();
            h.iter = lCall("flatten", {h.iter}, t.line);
        } else if (!isWhileKw && w == "chain" && !checkNext(TokenType::LPAREN) && !checkNext(TokenType::EQUAL)) {
            Token t = advance();
            h.iter = lCall("concat", {h.iter, expression()}, t.line);
        } else if (!isWhileKw && (w == "skip" || w == "take") && nextIsCountArg()) {
            Token t = advance();
            h.stages.push_back({w == "skip" ? 1 : 2, countArg(w), t.line});
        } else if (!isWhileKw && w == "until") {
            advance();
            h.stages.push_back({3, expression()});
        } else if (isWhileKw) {
            advance();
            h.stages.push_back({4, expression()});
        } else if (!isWhileKw && w == "from" && nextIsCountArg()) {
            Token t = advance();
            h.from = countArg(w); h.line = t.line;
        } else if (!isWhileKw && (w == "by" || w == "window" || w == "every")) {
            Token t = advance();
            oneMode(t);
            h.mode = t.lexeme == "by" ? 1 : (t.lexeme == "window" ? 3 : 4);
            h.line = t.line;
            h.arg = countArg(t.lexeme);
        } else if (!isWhileKw && (w == "zip" || w == "cross")) {
            Token t = advance();
            oneMode(t);
            h.mode = w == "zip" ? 2 : 5; h.line = t.line;
            h.iter2 = expression();
        } else if (!isWhileKw && w == "pairs" && flagWordOk()) {
            Token t = advance();
            oneMode(t);
            h.mode = 6; h.line = t.line;
        } else break;
    }
    for (const Stage& sg : h.stages)
        if (sg.kind == 5 && !sg.e && h.pat)
            throw errRich(diag::Code::E0013_InvalidExpression, peek(), "`distinct` needs a key when the loop variable is a pattern",
                          "a destructuring pattern has no single value to compare", "write `distinct on (a)` using one of the pattern names");
    if (!h.extra.empty() && (h.mode || h.pat || !h.v2.empty() || h.from))
        throw errRich(diag::Code::E0013_InvalidExpression, peek(), "a multi-source `each x in xs, y in ys` takes plain names only",
                      "the product form cannot be mixed with an index variable, patterns, `from` or by/window/every/zip/cross/pairs",
                      "use one name per source:  `each x in xs, y in ys { ... }`");
    if (h.mode && h.pat)
        throw errRich(diag::Code::E0013_InvalidExpression, peek(), "destructuring patterns cannot be combined with by/window/every/zip/cross/pairs",
                      "these modes hand out whole chunks or pairs of values", "use plain names:  `each a, b in xs zip ys { ... }`");
    if ((h.mode == 2 || h.mode == 5 || h.mode == 6) && h.v2.empty()) {
        const char* what = h.mode == 2 ? "zip" : h.mode == 5 ? "cross" : "pairs";
        throw errRich(diag::Code::E0012_MissingToken, peek(), std::string("`") + what + "` needs two loop variables",
                      h.mode == 6 ? "each round hands out two neighbouring items" : "each round takes one item from each side",
                      std::string("write `each a, b in xs ") + what + (h.mode == 6 ? "" : " ys") + " { ... }`", "second variable");
    }
    if (h.from && (h.v2.empty() || h.mode || h.pat))
        throw errRich(diag::Code::E0013_InvalidExpression, peek(), "`from` needs exactly two loop variables and no mode",
                      "it sets where the index (second variable) starts counting",
                      "write `each x, i in xs from 1 { ... }`");
    return h;
}

// يبني ForInStmt (+ تغليف العدّاد عند وجود متغيّرين) ثم يمرّرها عبر finishLoop
StmtPtr Parser::buildEach(EachHdr& h, StmtPtr body, int line, ExprPtr lim) {
    auto f = std::make_shared<ForInStmt>();
    f->line = line;
    std::vector<StmtPtr> pre;
    // ---- مراحل التيار (where/skip/take/until/while) بالترتيب المكتوب: تُلفّ من الأخيرة إلى الأولى ----
    std::vector<std::string> cntVar(h.stages.size()), nVar(h.stages.size());
    for (size_t i = 0; i < h.stages.size(); ++i) {
        if (h.stages[i].kind == 5) { // distinct: خريطة "المفاتيح المرئية" لكل حلقة
            const std::string id = std::to_string(loopSeq++);
            cntVar[i] = "#sd" + id; nVar[i] = "#dk" + id;
            pre.push_back(lLet(cntVar[i], std::make_shared<MapExpr>(), h.stages[i].line ? h.stages[i].line : line));
            continue;
        }
        if (h.stages[i].kind != 1 && h.stages[i].kind != 2) continue;
        const std::string id = std::to_string(loopSeq++);
        nVar[i] = "#sn" + id; cntVar[i] = "#sc" + id;
        const int ln = h.stages[i].line ? h.stages[i].line : line;
        const char* nm = h.stages[i].kind == 1 ? "skip" : "take";
        pre.push_back(lLet(nVar[i], h.stages[i].e, ln));
        auto bad = std::make_shared<LogicalExpr>();
        bad->left = lUnaryNot(lCall("isInt", {lVar(nVar[i], ln)}, ln), ln); bad->op = TokenType::OR;
        bad->right = lBin(lVar(nVar[i], ln), TokenType::LESS, lNum(0, ln), ln); bad->line = ln;
        pre.push_back(lIf(bad, lBlock({lThrow(lStr(std::string("`") + nm + "` needs a non-negative integer", ln), ln)}, ln), nullptr, ln));
        pre.push_back(lLet(cntVar[i], lNum(0, ln), ln));
    }
    StmtPtr inner = body;
    for (size_t i = h.stages.size(); i-- > 0;) {
        const Stage& sg = h.stages[i];
        const int ln = sg.line ? sg.line : line;
        auto inc = [&]() { return lExpr(lAssign(cntVar[i], lBin(lVar(cntVar[i], ln), TokenType::PLUS, lNum(1, ln), ln), ln), ln); };
        switch (sg.kind) {
        case 0: inner = lIf(sg.e, inner, nullptr, ln); break;
        case 1: inner = lIf(lBin(lVar(cntVar[i], ln), TokenType::LESS, lVar(nVar[i], ln), ln), lBlock({inc()}, ln), inner, ln); break;
        case 2: inner = lBlock({lIf(lBin(lVar(cntVar[i], ln), TokenType::GREATER_EQUAL, lVar(nVar[i], ln), ln), lBlock({lBreak(ln)}, ln), nullptr, ln), inc(), inner}, ln); break;
        case 5: { // أول ظهور لكل مفتاح فقط؛ المفتاح الافتراضي type:str(قيمة) فيُميَّز 1 عن "1"
            ExprPtr key = sg.e;
            if (!key) key = lBin(lBin(lCall("type", {lVar(h.v1, ln)}, ln), TokenType::PLUS, lStr(":", ln), ln), TokenType::PLUS, lCall("str", {lVar(h.v1, ln)}, ln), ln);
            auto mark = std::make_shared<IndexSetExpr>(); mark->object = lVar(cntVar[i], ln); mark->index = lVar(nVar[i], ln); mark->value = lBool(true, ln); mark->line = ln;
            inner = lBlock({lLet(nVar[i], key, ln),
                            lIf(lCall("has", {lVar(cntVar[i], ln), lVar(nVar[i], ln)}, ln), lBlock({lContinue(ln)}, ln), nullptr, ln),
                            lExpr(mark, ln), inner}, ln);
            break; }
        case 3: inner = lBlock({lIf(sg.e, lBlock({lBreak(ln)}, ln), nullptr, ln), inner}, ln); break;
        default: inner = lBlock({lIf(lUnaryNot(sg.e, ln), lBlock({lBreak(ln)}, ln), nullptr, ln), inner}, ln); break;
        }
    }
    if (!h.extra.empty()) { // ضرب ديكارتي لأي عدد من المصادر بحلقة for واحدة (آخر مصدر يدور أسرع)
        const std::string id = std::to_string(loopSeq++);
        const std::string k = "#ek" + id, tot = "#et" + id;
        std::vector<std::pair<std::string, ExprPtr>> srcs; srcs.push_back({h.v1, h.iter});
        for (auto& e : h.extra) srcs.push_back(e);
        size_t m = srcs.size();
        auto lenV = [&](size_t j) { return lCall("len", {lVar("#ei" + id + "_" + std::to_string(j), line)}, line); };
        for (size_t j = 0; j < m; ++j) {
            const std::string iv = "#ei" + id + "_" + std::to_string(j);
            pre.push_back(lLet(iv, srcs[j].second, line));
            auto o = std::make_shared<LogicalExpr>();
            o->left = lCall("isArray", {lVar(iv, line)}, line); o->op = TokenType::OR; o->right = lCall("isString", {lVar(iv, line)}, line); o->line = line;
            pre.push_back(lIf(lUnaryNot(o, line), lBlock({lThrow(lStr("`each x in xs, y in ys` needs arrays or strings", line), line)}, line), nullptr, line));
        }
        ExprPtr total = lenV(0);
        for (size_t j = 1; j < m; ++j) total = lBin(total, TokenType::STAR, lenV(j), line);
        pre.push_back(lLet(tot, total, line));
        std::vector<StmtPtr> bs;
        for (size_t j = 0; j < m; ++j) {
            ExprPtr stride = lNum(1, line);
            for (size_t q = j + 1; q < m; ++q) stride = lBin(stride, TokenType::STAR, lenV(q), line);
            ExprPtr pos = lBin(lCall("floor", {lBin(lVar(k, line), TokenType::SLASH, stride, line)}, line), TokenType::PERCENT, lenV(j), line);
            auto ix = std::make_shared<IndexExpr>(); ix->object = lVar("#ei" + id + "_" + std::to_string(j), line); ix->index = pos; ix->line = line;
            bs.push_back(lLet(srcs[j].first, ix, line));
        }
        bs.push_back(inner);
        auto fs = std::make_shared<ForStmt>();
        fs->initializer = lLet(k, lNum(0, line), line);
        fs->condition = lBin(lVar(k, line), TokenType::LESS, lVar(tot, line), line);
        fs->increment = lAssign(k, lBin(lVar(k, line), TokenType::PLUS, lNum(1, line), line), line);
        fs->body = lBlock(std::move(bs), line); fs->line = line;
        attachDoneBlock(fs->doneBlock);
        pre.push_back(finishLoop(fs, lim));
        return lBlock(std::move(pre), line);
    }
    if (h.mode) {
        const std::string id = std::to_string(loopSeq++);
        const std::string it = "#ei" + id, it2 = "#ej" + id, n = "#en" + id, k = "#ek" + id, st = "#es" + id;
        auto idx = [&](const std::string& o, const std::string& i) { auto x = std::make_shared<IndexExpr>(); x->object = lVar(o, line); x->index = lVar(i, line); x->line = line; return ExprPtr(x); };
        auto idxE = [&](const std::string& o, ExprPtr i) { auto x = std::make_shared<IndexExpr>(); x->object = lVar(o, line); x->index = i; x->line = line; return ExprPtr(x); };
        auto plus = [&](ExprPtr a, ExprPtr b) { return lBin(a, TokenType::PLUS, b, line); };
        auto isSeq = [&](const std::string& v) { auto o = std::make_shared<LogicalExpr>(); o->left = lCall("isArray", {lVar(v, line)}, line); o->op = TokenType::OR; o->right = lCall("isString", {lVar(v, line)}, line); o->line = line; return lUnaryNot(o, line); };
        const char* what = h.mode == 1 ? "by" : h.mode == 2 ? "zip" : h.mode == 3 ? "window" : h.mode == 4 ? "every" : h.mode == 5 ? "cross" : "pairs";
        std::string msg = std::string("`each ... ") + what + "` needs an array or a string";
        pre.push_back(lLet(it, h.iter, line));
        pre.push_back(lIf(isSeq(it), lBlock({lThrow(lStr(msg, line), line)}, line), nullptr, line));
        ExprPtr nExpr = lCall("len", {lVar(it, line)}, line);
        if (h.mode == 2 || h.mode == 5) {
            pre.push_back(lLet(it2, h.iter2, line));
            pre.push_back(lIf(isSeq(it2), lBlock({lThrow(lStr(msg, line), line)}, line), nullptr, line));
        }
        if (h.mode == 2) {
            auto cnd = std::make_shared<ConditionalExpr>();
            ExprPtr l2 = lCall("len", {lVar(it2, line)}, line);
            cnd->condition = lBin(nExpr, TokenType::LESS, l2, line); cnd->whenTrue = lCall("len", {lVar(it, line)}, line); cnd->whenFalse = lCall("len", {lVar(it2, line)}, line); cnd->line = line;
            nExpr = cnd;
        } else if (h.mode == 5) {
            pre.push_back(lLet(st, lCall("len", {lVar(it2, line)}, line), line)); // عرض الضرب الديكارتي
            nExpr = lBin(nExpr, TokenType::STAR, lVar(st, line), line);
        } else if (h.mode == 6) {
            nExpr = lBin(nExpr, TokenType::MINUS, lNum(1, line), line);
        }
        pre.push_back(lLet(n, nExpr, line));
        if (h.arg) {
            pre.push_back(lLet(st, h.arg, line));
            auto bad = std::make_shared<LogicalExpr>();
            bad->left = lUnaryNot(lCall("isInt", {lVar(st, line)}, line), line); bad->op = TokenType::OR; bad->right = lBin(lVar(st, line), TokenType::LESS, lNum(1, line), line); bad->line = line;
            pre.push_back(lIf(bad, lBlock({lThrow(lStr(std::string("`") + what + "` needs a positive integer", line), line)}, line), nullptr, line));
        }
        std::vector<StmtPtr> bodyStmts;
        ExprPtr cond, step;
        auto chunk = [&]() { return lCall("slice", {lVar(it, line), lVar(k, line), plus(lVar(k, line), lVar(st, line))}, line); };
        if (h.mode == 1) {
            cond = lBin(lVar(k, line), TokenType::LESS, lVar(n, line), line); step = lVar(st, line);
            bodyStmts.push_back(lLet(h.v1, chunk(), line));
            if (!h.v2.empty()) bodyStmts.push_back(lLet(h.v2, lBin(lVar(k, line), TokenType::SLASH, lVar(st, line), line), line));
        } else if (h.mode == 3) {
            cond = lBin(plus(lVar(k, line), lVar(st, line)), TokenType::LESS_EQUAL, lVar(n, line), line); step = lNum(1, line);
            bodyStmts.push_back(lLet(h.v1, chunk(), line));
            if (!h.v2.empty()) bodyStmts.push_back(lLet(h.v2, lVar(k, line), line));
        } else if (h.mode == 4) {
            cond = lBin(lVar(k, line), TokenType::LESS, lVar(n, line), line); step = lVar(st, line);
            bodyStmts.push_back(lLet(h.v1, idx(it, k), line));
            if (!h.v2.empty()) bodyStmts.push_back(lLet(h.v2, lVar(k, line), line));
        } else if (h.mode == 5) { // cross: k يمرّ على 0..(n1*n2)-1  ->  (xs[k div n2], ys[k mod n2])
            cond = lBin(lVar(k, line), TokenType::LESS, lVar(n, line), line); step = lNum(1, line);
            bodyStmts.push_back(lLet(h.v1, idxE(it, lCall("floor", {lBin(lVar(k, line), TokenType::SLASH, lVar(st, line), line)}, line)), line));
            bodyStmts.push_back(lLet(h.v2, idxE(it2, lBin(lVar(k, line), TokenType::PERCENT, lVar(st, line), line)), line));
        } else if (h.mode == 6) { // pairs: (xs[k], xs[k+1])
            cond = lBin(lVar(k, line), TokenType::LESS, lVar(n, line), line); step = lNum(1, line);
            bodyStmts.push_back(lLet(h.v1, idx(it, k), line));
            bodyStmts.push_back(lLet(h.v2, idxE(it, plus(lVar(k, line), lNum(1, line))), line));
        } else {
            cond = lBin(lVar(k, line), TokenType::LESS, lVar(n, line), line); step = lNum(1, line);
            bodyStmts.push_back(lLet(h.v1, idx(it, k), line));
            bodyStmts.push_back(lLet(h.v2, idx(it2, k), line));
        }
        bodyStmts.push_back(inner);
        auto fs = std::make_shared<ForStmt>();
        fs->initializer = lLet(k, lNum(0, line), line);
        fs->condition = cond;
        fs->increment = lAssign(k, plus(lVar(k, line), step), line);
        fs->body = lBlock(std::move(bodyStmts), line); fs->line = line;
        attachDoneBlock(fs->doneBlock);
        StmtPtr loop = finishLoop(fs, lim);
        pre.push_back(loop);
        return lBlock(std::move(pre), line);
    }
    if (!h.v2.empty()) {
        const std::string id = std::to_string(loopSeq++);
        const std::string it = "#ei" + id, n = "#en" + id, e = "#ee" + id;
        pre.push_back(lLet(it, h.iter, line));
        pre.push_back(lLet(n, h.from ? lBin(h.from, TokenType::MINUS, lNum(1, line), line) : lNum(-1, line), line));
        auto idxE = std::make_shared<IndexExpr>(); idxE->object = lVar(it, line); idxE->index = lVar(e, line); idxE->line = line;
        auto cond = std::make_shared<ConditionalExpr>();
        cond->condition = lCall("isMap", {lVar(it, line)}, line); cond->whenTrue = idxE; cond->whenFalse = lVar(n, line); cond->line = line;
        f->varName = e;
        f->iterable = lVar(it, line);
        f->body = lBlock({lExpr(lAssign(n, lBin(lVar(n, line), TokenType::PLUS, lNum(1, line), line), line), line),
                          lLet(h.v1, lVar(e, line), line), lLet(h.v2, cond, line), inner}, line);
    } else {
        f->varName = h.v1; f->pattern = h.pat; f->iterable = h.iter; f->body = inner;
    }
    attachDoneBlock(f->doneBlock);
    StmtPtr loop = finishLoop(f, lim);
    if (pre.empty()) return loop;
    pre.push_back(loop);
    return lBlock(std::move(pre), line);
}

// [each x in xs where c yield expr]  ->  goal { let acc = []; each ... { push(acc, expr); } achieve acc; }
ExprPtr Parser::eachComprehension() {
    Token kwTok = advance(); // each
    int line = kwTok.line;
    EachHdr h = parseEachHdr();
    if (!(check(TokenType::IDENT) && peek().lexeme == "yield"))
        throw errRich(diag::Code::E0012_MissingToken, peek(), "expected 'yield' in an `each` comprehension",
                      "an array comprehension is written `[each x in xs where cond yield expr]`",
                      "add `yield <expression>` before the closing `]`", "'yield'");
    advance();
    enterLoop();
    ExprPtr val = expression();
    exitLoop();
    const std::string acc = "#ac" + std::to_string(loopSeq++);
    auto push = lExpr(lCall("push", {lVar(acc, line), val}, line), line);
    StmtPtr loop = buildEach(h, push, line, nullptr);
    auto ach = std::make_shared<AchieveStmt>(); ach->value = lVar(acc, line); ach->line = line;
    auto g = std::make_shared<GoalExpr>(); g->line = line;
    g->body = lBlock({lLet(acc, std::make_shared<ArrayExpr>(), line), loop, ach}, line);
    return g;
}


// ---- Loops v3: تعابير الاختزال والقواميس — كلها تفكيك إلى goal { ...حلقة... achieve } كالفهم القديم ----
bool Parser::atReducer(size_t at) const {
    if (at + 1 >= tokens.size() || tokens[at].type != TokenType::IDENT) return false;
    const std::string& w = tokens[at].lexeme;
    if (!(w == "sum" || w == "product" || w == "count" || w == "avg" || w == "min" || w == "max" ||
          w == "any" || w == "all" || w == "first" || w == "last")) return false;
    return atEachForm(at + 1);
}

ExprPtr Parser::reduceComprehension() {
    Token kindTok = advance();
    const std::string kind = kindTok.lexeme;
    Token kwTok = advance(); // each
    int line = kwTok.line;
    EachHdr h = parseEachHdr();
    ExprPtr val;
    const bool hasYield = check(TokenType::IDENT) && peek().lexeme == "yield";
    enterLoop();
    if (hasYield) { advance(); val = expression(); }
    exitLoop();
    const bool needsVal = kind != "count" && kind != "any";
    if (!val) {
        if (kind == "all")
            throw errRich(diag::Code::E0012_MissingToken, peek(), "`all each ...` needs `yield <condition>`",
                          "`all` checks a condition on every item", "write `all each x in xs yield x > 0`", "'yield'");
        if (needsVal) {
            if (!h.v2.empty() || h.pat)
                throw errRich(diag::Code::E0012_MissingToken, peek(), "`" + kind + " each ...` needs `yield <value>` here",
                              "with two variables or a pattern there is no single item to use",
                              "add `yield <expression>`", "'yield'");
            val = lVar(h.v1, line);
        }
    }
    const std::string id = std::to_string(loopSeq++);
    const std::string acc = "#rc" + id, has = "#rh" + id, tmp = "#rv" + id;
    auto nilLit = [&]() { auto l = std::make_shared<LiteralExpr>(); l->kind = LiteralExpr::Kind::NIL; l->line = line; return ExprPtr(l); };
    auto setAcc = [&](ExprPtr e) { return lExpr(lAssign(acc, e, line), line); };
    auto accV = [&]() { return lVar(acc, line); };
    auto inc = [&](const std::string& v) { return lExpr(lAssign(v, lBin(lVar(v, line), TokenType::PLUS, lNum(1, line), line), line), line); };
    std::vector<StmtPtr> head;
    StmtPtr bodyS;
    ExprPtr result = accV();
    if (kind == "sum")      { head.push_back(lLet(acc, lNum(0, line), line)); bodyS = setAcc(lBin(accV(), TokenType::PLUS, val, line)); }
    else if (kind == "product") { head.push_back(lLet(acc, lNum(1, line), line)); bodyS = setAcc(lBin(accV(), TokenType::STAR, val, line)); }
    else if (kind == "count") {
        head.push_back(lLet(acc, lNum(0, line), line));
        bodyS = val ? lIf(val, lBlock({inc(acc)}, line), nullptr, line) : inc(acc);
    } else if (kind == "avg") {
        head.push_back(lLet(acc, lNum(0, line), line)); head.push_back(lLet(has, lNum(0, line), line));
        bodyS = lBlock({setAcc(lBin(accV(), TokenType::PLUS, val, line)), inc(has)}, line);
        auto c = std::make_shared<ConditionalExpr>();
        c->condition = lBin(lVar(has, line), TokenType::EQUAL_EQUAL, lNum(0, line), line);
        c->whenTrue = nilLit(); c->whenFalse = lBin(accV(), TokenType::SLASH, lVar(has, line), line); c->line = line;
        result = c;
    } else if (kind == "min" || kind == "max") {
        head.push_back(lLet(acc, nilLit(), line)); head.push_back(lLet(has, lNum(0, line), line));
        auto better = std::make_shared<LogicalExpr>();
        better->left = lBin(lVar(has, line), TokenType::EQUAL_EQUAL, lNum(0, line), line); better->op = TokenType::OR;
        better->right = lBin(lVar(tmp, line), kind == "min" ? TokenType::LESS : TokenType::GREATER, accV(), line); better->line = line;
        bodyS = lBlock({lLet(tmp, val, line),
                        lIf(better, lBlock({setAcc(lVar(tmp, line)), lExpr(lAssign(has, lNum(1, line), line), line)}, line), nullptr, line)}, line);
    } else if (kind == "any") {
        head.push_back(lLet(acc, lBool(false, line), line));
        StmtPtr hit = lBlock({setAcc(lBool(true, line)), lBreak(line)}, line);
        bodyS = val ? lIf(val, hit, nullptr, line) : hit;
    } else if (kind == "all") {
        head.push_back(lLet(acc, lBool(true, line), line));
        bodyS = lIf(lUnaryNot(val, line), lBlock({setAcc(lBool(false, line)), lBreak(line)}, line), nullptr, line);
    } else if (kind == "first") {
        head.push_back(lLet(acc, nilLit(), line));
        bodyS = lBlock({setAcc(val), lBreak(line)}, line);
    } else { // last
        head.push_back(lLet(acc, nilLit(), line));
        bodyS = setAcc(val);
    }
    StmtPtr loop = buildEach(h, bodyS, line, nullptr);
    auto ach = std::make_shared<AchieveStmt>(); ach->value = result; ach->line = line;
    auto g = std::make_shared<GoalExpr>(); g->line = line;
    head.push_back(loop); head.push_back(ach);
    g->body = lBlock(std::move(head), line);
    return g;
}

ExprPtr Parser::mapComprehension() {
    Token kwTok = advance(); // each
    int line = kwTok.line;
    EachHdr h = parseEachHdr();
    if (!(check(TokenType::IDENT) && peek().lexeme == "yield"))
        throw errRich(diag::Code::E0012_MissingToken, peek(), "expected 'yield' in a map comprehension",
                      "a map comprehension is written `{each x in xs where cond yield key: value}`",
                      "add `yield <key>: <value>` before the closing `}`", "'yield'");
    advance();
    enterLoop();
    ExprPtr key = expression();
    consume(TokenType::COLON, "Expected ':' between key and value in a map comprehension");
    ExprPtr value = expression();
    exitLoop();
    const std::string acc = "#mc" + std::to_string(loopSeq++);
    auto set = std::make_shared<IndexSetExpr>();
    set->object = lVar(acc, line); set->index = key; set->value = value; set->line = line;
    StmtPtr loop = buildEach(h, lExpr(set, line), line, nullptr);
    auto ach = std::make_shared<AchieveStmt>(); ach->value = lVar(acc, line); ach->line = line;
    auto g = std::make_shared<GoalExpr>(); g->line = line;
    g->body = lBlock({lLet(acc, std::make_shared<MapExpr>(), line), loop, ach}, line);
    return g;
}


// ============================================================================
// Rin For+ — i++ / i-- / += -= *= /= %= ، ‎for بلا أقواس، عدّة خطوات تزايد، وحلقة for سريعة بلا closures.
// ============================================================================
bool Parser::tokensMayCapture(size_t from, size_t to) const {
    for (size_t k = from; k < to && k < tokens.size(); ++k) {
        const Token& t = tokens[k];
        if (t.type == TokenType::FUN) return true;
        if (t.type == TokenType::IDENT) {
            const std::string& w = t.lexeme;
            if (w == "bell" || w == "on" || w == "hook" || w == "defer" || w == "spawn" || w == "async" || w == "thread" ||
                w == "after" || w == "watch" || w == "trial" || w == "onload" || w == "onunload")
                return true;
        }
    }
    return false;
}

bool Parser::isSimpleTarget(const ExprPtr& e) const {
    if (!e) return false;
    if (std::dynamic_pointer_cast<VariableExpr>(e) || std::dynamic_pointer_cast<LiteralExpr>(e)) return true;
    if (auto g = std::dynamic_pointer_cast<GetExpr>(e)) return isSimpleTarget(g->object);
    if (auto ix = std::dynamic_pointer_cast<IndexExpr>(e)) return isSimpleTarget(ix->object) && isSimpleTarget(ix->index);
    if (auto b = std::dynamic_pointer_cast<BinaryExpr>(e)) return isSimpleTarget(b->left) && isSimpleTarget(b->right);
    return false;
}


// =====================================================================================
// @tmpl — قوالب الحلقات (docs/loop-templates.md)
//
//   @tmpl evens(xs) each x in xs where x % 2 == 0;          // قالب ترويسة: يُعرَّف مرة واحدة
//   @evens(nums) { print x; }                                // ويُستعمل مع أي مجموعة
//   @evens(nums) as n { print n; }                           // as = إعادة تسمية متغيّر الحلقة
//   @tmpl grid(r, c) as i, j { each i in 1 to r { each j in 1 to c { @body } } }   // قالب كتلة
//
// يُنفَّذ كمرحلة توكنات قبل Parser::declaration(): تُحذف تعريفات @tmpl من التيار وتُستبدل
// الاستعمالات @name(args) بتوكنات القالب بعد التعويض. لذلك يعمل مع كل أنواع الحلقات
// (each/for/while/loop/repeat/retry/limit/labels) وفي التعابير [@t(xs) yield e] و sum @t(xs) yield e،
// ولا يحتاج أي تعديل في المفسّر أو RinVM. برنامج بلا @tmpl لا يُمَسّ إطلاقاً.
// =====================================================================================
struct Parser::LoopTmpl {
    struct Param { std::string name; std::vector<Token> def; bool hasDef = false; };
    std::string name;
    std::vector<Param> params;
    std::vector<std::string> exposed;   // متغيّرات الحلقة القابلة لإعادة التسمية بـ `as`
    std::vector<Token> body;            // ترويسة الحلقة (قالب ترويسة) أو الكتلة كاملة {..} (قالب كتلة)
    std::vector<Token> defBody;         // جسم افتراضي {..} لقالب الترويسة (اختياري)
    bool block = false;
    // قوالب الطباعة (docs/print-templates.md): 0 = قالب حلقة · 1 = سطر print · 2 = أسطر متعددة print { }
    //                                           3 = style (سمات print) · 4 = text (تعبير نصي)
    int kind = 0;
    std::vector<std::vector<Token>> lines;  // kind 2: كل سطر = قيم print + سماتها (بلا ';')
    Token at;
};

namespace {
bool tIsWord(const Token& t, const char* w) { return t.type == TokenType::IDENT && t.lexeme == w; }
// اسم قالب: أي كلمة (حتى لو كانت كلمة محجوزة في سياق آخر مثل show)
bool tNameLike(const Token& t) {
    if (t.type == TokenType::STRING || t.type == TokenType::NUMBER || t.lexeme.empty()) return false;
    unsigned char c = (unsigned char)t.lexeme[0];
    return std::isalpha(c) || c == '_' || c >= 0x80;
}
bool tOpen(const Token& t) { return t.type == TokenType::LPAREN || t.type == TokenType::LBRACKET || t.type == TokenType::LBRACE; }
bool tClose(const Token& t) { return t.type == TokenType::RPAREN || t.type == TokenType::RBRACKET || t.type == TokenType::RBRACE; }
// فهرس القوس المقابل للقوس المفتوح عند open، أو npos.
size_t tMatch(const std::vector<Token>& v, size_t open) {
    int d = 0;
    for (size_t i = open; i < v.size(); i++) {
        if (v[i].type == TokenType::END_OF_FILE) return std::string::npos;
        if (tOpen(v[i])) d++;
        else if (tClose(v[i])) { d--; if (d == 0) return i; }
    }
    return std::string::npos;
}
Token tMake(const Token& like, TokenType ty, const char* lex) {
    Token t = like; t.type = ty; t.lexeme = lex; t.number = 0; t.tpl = false; return t;
}
bool tReserved(const std::string& n) {
    static const std::unordered_set<std::string> r = {"tmpl", "body", "import", "view", "element", "container",
        "Containers", "make", "loop", "Volume", "volume", "link", "portal", "route", "object", "install"};
    return r.count(n) > 0;
}
} // namespace

void Parser::expandLoopTemplates() {
    if (tmplDone) return;
    tmplDone = true;
    bool any = false;
    for (size_t i = 0; i + 1 < tokens.size(); i++)
        if (tokens[i].type == TokenType::AT && tIsWord(tokens[i + 1], "tmpl")) { any = true; break; }
    if (!any) return;
    std::vector<Token> in = tokens;
    tokens = expandTmplStream(in, 0);
    current = 0;
}

std::vector<Token> Parser::expandTmplStream(const std::vector<Token>& in, int depth) {
    if (depth > 64)
        throw errRich(diag::Code::E0010_ParserError, in.empty() ? Token{} : in.front(),
                      "loop template expansion is too deep",
                      "a @tmpl keeps expanding into a use of itself (directly or through another template)",
                      "remove the recursive @name(...) call from the template body");
    std::vector<Token> out;
    size_t i = 0;
    while (i < in.size()) {
        const Token& t = in[i];
        if (t.type == TokenType::AT && i + 1 < in.size() && tNameLike(in[i + 1])) {
            const std::string& nm = in[i + 1].lexeme;
            if (nm == "tmpl") { i = tmplParseDef(in, i); continue; }
            auto it = tmpls.find(nm);
            if (it != tmpls.end() && i + 2 < in.size() && in[i + 2].type == TokenType::LPAREN) {
                i = tmplExpandUse(*it->second, in, i, out, depth);
                continue;
            }
        }
        out.push_back(t);
        i++;
    }
    return out;
}

size_t Parser::tmplParseDef(const std::vector<Token>& in, size_t i) {
    const Token& atTok = in[i];
    size_t k = i + 2;
    const char* usage = "usage: @tmpl name(params) each x in xs where cond;   then   @name(args) { body }";
    if (k >= in.size() || !tNameLike(in[k]))
        throw errRich(diag::Code::E0010_ParserError, k < in.size() ? in[k] : atTok,
                      "Expected a template name after '@tmpl'", "", usage);
    auto tm = std::make_shared<LoopTmpl>();
    tm->name = in[k].lexeme; tm->at = atTok;
    const Token& nameTok = in[k];
    if (tReserved(tm->name))
        throw errRich(diag::Code::E0010_ParserError, nameTok, "'" + tm->name + "' cannot be used as a template name",
                      "it is already a Rin '@' keyword", "choose another name, e.g. @tmpl my" + tm->name + "(...)");
    if (tmpls.count(tm->name))
        throw errRich(diag::Code::E0010_ParserError, nameTok, "template '" + tm->name + "' is already defined", "", "use a different name");
    k++;
    if (k >= in.size() || in[k].type != TokenType::LPAREN)
        throw errRich(diag::Code::E0010_ParserError, k < in.size() ? in[k] : nameTok,
                      "Expected '(' after template name '" + tm->name + "'", "", usage);
    size_t pc = tMatch(in, k);
    if (pc == std::string::npos)
        throw err(diag::Code::E0010_ParserError, in[k], "Unterminated parameter list of template '" + tm->name + "'");
    // معاملات: name  |  name = default
    {
        std::vector<std::vector<Token>> segs; std::vector<Token> cur; int d = 0;
        for (size_t j = k + 1; j < pc; j++) {
            if (tOpen(in[j])) d++; else if (tClose(in[j])) d--;
            if (d == 0 && in[j].type == TokenType::COMMA) { segs.push_back(cur); cur.clear(); continue; }
            cur.push_back(in[j]);
        }
        if (!cur.empty() || !segs.empty()) segs.push_back(cur);
        bool seenDef = false;
        for (auto& s : segs) {
            if (s.empty() || s[0].type != TokenType::IDENT)
                throw err(diag::Code::E0010_ParserError, s.empty() ? in[k] : s[0], "Template parameters must be plain names");
            LoopTmpl::Param p; p.name = s[0].lexeme;
            for (auto& q : tm->params) if (q.name == p.name)
                throw err(diag::Code::E0010_ParserError, s[0], "Duplicate template parameter '" + p.name + "'");
            if (s.size() > 1) {
                if (s[1].type != TokenType::EQUAL || s.size() < 3)
                    throw err(diag::Code::E0010_ParserError, s[1], "Expected '= default' after template parameter '" + p.name + "'");
                p.hasDef = true; p.def.assign(s.begin() + 2, s.end()); seenDef = true;
            } else if (seenDef) {
                throw err(diag::Code::E0010_ParserError, s[0], "Parameter '" + p.name + "' without a default cannot follow one with a default");
            }
            tm->params.push_back(std::move(p));
        }
    }
    k = pc + 1;
    // ---- قوالب الطباعة: print / print { } / style / text (تُسجَّل ويُعاد بلا المرور بمسار قوالب الحلقات) ----
    if (k < in.size() && (in[k].type == TokenType::PRINT || in[k].lexeme == "style" || in[k].lexeme == "text")) {
        const Token& kw = in[k];
        const char* pusage = "usage: @tmpl name(p) print `..${p}..` [attrs];  |  @tmpl name(p) print { line; line; }  |  "
                             "@tmpl name(args) style width=40 align=\"center\";  |  @tmpl name(p) text expr;";
        tm->kind = kw.type == TokenType::PRINT ? 1 : (kw.lexeme == "style" ? 3 : 4);
        k++;
        auto scanEnd = [&](size_t from) {       // أول ';' على العمق 0 (يتخطّى الأقواس)
            size_t j = from;
            while (j < in.size()) {
                const Token& tk = in[j];
                if (tk.type == TokenType::END_OF_FILE) break;
                if (tOpen(tk)) {
                    size_t m = tMatch(in, j);
                    if (m == std::string::npos) throw err(diag::Code::E0010_ParserError, tk, "Unbalanced bracket in template '" + tm->name + "'");
                    j = m + 1; continue;
                }
                if (tk.type == TokenType::SEMICOLON) break;
                j++;
            }
            if (j >= in.size() || in[j].type != TokenType::SEMICOLON)
                throw errRich(diag::Code::E0010_ParserError, nameTok, "template '" + tm->name + "' is not terminated",
                              "a print/style/text template ends with ';'", pusage);
            return j;
        };
        if (tm->kind == 1 && k < in.size() && in[k].type == TokenType::LBRACE) {     // print { سطر; سطر; }
            size_t m = tMatch(in, k);
            if (m == std::string::npos) throw err(diag::Code::E0010_ParserError, in[k], "Unterminated body of template '" + tm->name + "'");
            tm->kind = 2;
            std::vector<Token> cur; int d = 0;
            for (size_t j = k + 1; j < m; j++) {
                if (tOpen(in[j])) d++; else if (tClose(in[j])) d--;
                if (d == 0 && in[j].type == TokenType::SEMICOLON) { if (!cur.empty()) tm->lines.push_back(cur); cur.clear(); continue; }
                cur.push_back(in[j]);
            }
            if (!cur.empty()) tm->lines.push_back(cur);
            if (tm->lines.empty())
                throw errRich(diag::Code::E0010_ParserError, nameTok, "template '" + tm->name + "' has no lines", "", pusage);
            k = m + 1;
            if (k < in.size() && in[k].type == TokenType::SEMICOLON) k++;
        } else {
            size_t j = scanEnd(k);
            if (j == k)
                throw errRich(diag::Code::E0010_ParserError, nameTok, "template '" + tm->name + "' has an empty body", "", pusage);
            tm->body.assign(in.begin() + k, in.begin() + j);
            k = j + 1;
        }
        tmpls[tm->name] = tm;
        return k;
    }
    // as x, i  — أسماء المتغيّرات المكشوفة صراحةً (اختياري؛ تُستنتج تلقائياً من ترويسة each/for/repeat/retry)
    bool explicitExposed = false;
    if (k < in.size() && tIsWord(in[k], "as")) {
        k++;
        while (true) {
            if (k >= in.size() || in[k].type != TokenType::IDENT)
                throw err(diag::Code::E0010_ParserError, k < in.size() ? in[k] : atTok, "Expected a variable name after 'as' in @tmpl");
            tm->exposed.push_back(in[k].lexeme); k++;
            if (k < in.size() && in[k].type == TokenType::COMMA) { k++; continue; }
            break;
        }
        explicitExposed = true;
    }
    if (k < in.size() && in[k].type == TokenType::LBRACE) {          // قالب كتلة
        size_t m = tMatch(in, k);
        if (m == std::string::npos) throw err(diag::Code::E0010_ParserError, in[k], "Unterminated body of template '" + tm->name + "'");
        tm->block = true;
        tm->body.assign(in.begin() + k, in.begin() + m + 1);
        k = m + 1;
        bool hasHole = false;
        for (size_t j = 0; j + 1 < tm->body.size(); j++)
            if (tm->body[j].type == TokenType::AT && tIsWord(tm->body[j + 1], "body")) hasHole = true;
        if (!hasHole)
            throw errRich(diag::Code::E0010_ParserError, nameTok, "block template '" + tm->name + "' has no @body hole",
                          "a template written as { ... } must say where the caller's body goes",
                          "put @body inside the innermost loop, e.g. @tmpl " + tm->name + "(xs) { each x in xs { @body } }");
    } else {                                                        // قالب ترويسة
        size_t j = k;
        while (j < in.size()) {
            const Token& tk = in[j];
            if (tk.type == TokenType::END_OF_FILE) break;
            if (tk.type == TokenType::LPAREN || tk.type == TokenType::LBRACKET) {
                size_t m = tMatch(in, j);
                if (m == std::string::npos) throw err(diag::Code::E0010_ParserError, tk, "Unbalanced bracket in template '" + tm->name + "'");
                j = m + 1; continue;
            }
            if (tk.type == TokenType::LBRACE || tk.type == TokenType::SEMICOLON) break;
            j++;
        }
        if (j >= in.size() || in[j].type == TokenType::END_OF_FILE)
            throw errRich(diag::Code::E0010_ParserError, nameTok, "template '" + tm->name + "' is not terminated",
                          "a header template ends with ';' or with a default body { ... }", usage);
        if (j == k)
            throw errRich(diag::Code::E0010_ParserError, nameTok, "template '" + tm->name + "' has an empty loop header", "", usage);
        tm->body.assign(in.begin() + k, in.begin() + j);
        if (in[j].type == TokenType::LBRACE) {
            size_t m = tMatch(in, j);
            if (m == std::string::npos) throw err(diag::Code::E0010_ParserError, in[j], "Unterminated default body of template '" + tm->name + "'");
            tm->defBody.assign(in.begin() + j, in.begin() + m + 1);
            k = m + 1;
        } else {
            k = j + 1;
        }
    }
    if (k < in.size() && in[k].type == TokenType::SEMICOLON) k++;
    // استنتاج المتغيّرات المكشوفة من ترويسة الحلقة
    if (!explicitExposed && !tm->block) {
        const auto& b = tm->body;
        size_t s = (b.size() >= 2 && b[0].type == TokenType::IDENT && b[1].type == TokenType::COLON) ? 2 : 0;  // label:
        bool forLike = s < b.size() && (tIsWord(b[s], "each") || b[s].lexeme == "for") &&
                       !(s + 1 < b.size() && b[s + 1].type == TokenType::LPAREN);
        if (forLike) {
            // each x, i in xs   |   each x in xs, y in ys  (مصادر متعددة)
            size_t j = s + 1;
            while (j < b.size()) {
                for (; j < b.size() && !tIsWord(b[j], "in"); j++)
                    if (b[j].type == TokenType::IDENT) tm->exposed.push_back(b[j].lexeme);
                if (j >= b.size()) break;
                j++;                                   // 'in'
                while (j < b.size() && b[j].type != TokenType::COMMA) {
                    if (b[j].type == TokenType::LPAREN || b[j].type == TokenType::LBRACKET) {
                        size_t m = tMatch(b, j); if (m == std::string::npos) { j = b.size(); break; } j = m;
                    }
                    j++;
                }
                if (j < b.size()) j++;                 // ','
            }
        } else {
            for (size_t j = s; j < b.size(); j++) {
                if (b[j].type == TokenType::LPAREN || b[j].type == TokenType::LBRACKET) {
                    size_t m = tMatch(b, j); if (m == std::string::npos) break; j = m; continue;
                }
                if (tIsWord(b[j], "as") && j + 1 < b.size() && b[j + 1].type == TokenType::IDENT) {
                    tm->exposed.push_back(b[j + 1].lexeme); break;
                }
            }
        }
    }
    tmpls[tm->name] = tm;
    return k;
}

size_t Parser::tmplExpandUse(const LoopTmpl& t, const std::vector<Token>& in, size_t i, std::vector<Token>& out, int depth) {
    const Token& atTok = in[i];
    size_t open = i + 2;
    size_t close = tMatch(in, open);
    if (close == std::string::npos)
        throw err(diag::Code::E0010_ParserError, in[open], "Unterminated argument list in @" + t.name + "(...)");
    // 1) الوسائط
    std::vector<std::vector<Token>> args; {
        std::vector<Token> cur; int d = 0; bool any = false;
        for (size_t j = open + 1; j < close; j++) {
            any = true;
            if (tOpen(in[j])) d++; else if (tClose(in[j])) d--;
            if (d == 0 && in[j].type == TokenType::COMMA) {
                if (cur.empty()) throw err(diag::Code::E0010_ParserError, in[j], "Empty argument in @" + t.name + "(...)");
                args.push_back(cur); cur.clear(); continue;
            }
            cur.push_back(in[j]);
        }
        if (any) {
            if (cur.empty()) throw err(diag::Code::E0010_ParserError, in[close], "Empty argument in @" + t.name + "(...)");
            args.push_back(cur);
        }
    }
    if (args.size() > t.params.size())
        throw err(diag::Code::E0010_ParserError, atTok, "template '" + t.name + "' takes " + std::to_string(t.params.size()) +
                  " argument(s) but " + std::to_string(args.size()) + " were given");
    for (size_t p = args.size(); p < t.params.size(); p++) {
        if (!t.params[p].hasDef)
            throw errRich(diag::Code::E0010_ParserError, atTok, "missing argument '" + t.params[p].name + "' for template '" + t.name + "'",
                          "", "give a value, or add a default in the definition: " + t.params[p].name + " = ...");
        args.push_back(t.params[p].def);
    }
    size_t k = close + 1;
    // ---- قوالب الطباعة (kind != 0) ----
    if (t.kind != 0) {
        static int printTmplSeq = 0;
        auto T = [&](TokenType ty, const char* lex) { return tMake(atTok, ty, lex); };
        std::vector<Token> E;
        if (t.kind == 3) {                       // style: استبدال المعاملات بالتوكنات ثم الإدراج داخل عبارة print
            std::map<std::string, size_t> pidx;
            for (size_t p = 0; p < t.params.size(); p++) pidx[t.params[p].name] = p;
            const auto& b = t.body;
            for (size_t j = 0; j < b.size(); j++) {
                const Token& tk = b[j];
                bool afterDot = j > 0 && b[j - 1].type == TokenType::DOT;
                auto pi = (tk.type == TokenType::IDENT && !afterDot) ? pidx.find(tk.lexeme) : pidx.end();
                if (pi != pidx.end()) {
                    const auto& a = args[pi->second];
                    bool raw = a.size() == 1;
                    if (!raw) E.push_back(T(TokenType::LPAREN, "("));
                    E.insert(E.end(), a.begin(), a.end());
                    if (!raw) E.push_back(T(TokenType::RPAREN, ")"));
                } else E.push_back(tk);
            }
        } else if (t.kind == 4) {                // text: ‎(fun(p..) { return EXPR; })(args..)  — معزول، ويدعم `${p}` داخل القوالب
            E.push_back(T(TokenType::LPAREN, "(")); E.push_back(T(TokenType::FUN, "fun")); E.push_back(T(TokenType::LPAREN, "("));
            for (size_t p = 0; p < t.params.size(); p++) {
                if (p) E.push_back(T(TokenType::COMMA, ","));
                Token pt = T(TokenType::IDENT, t.params[p].name.c_str()); E.push_back(pt);
            }
            E.push_back(T(TokenType::RPAREN, ")")); E.push_back(T(TokenType::LBRACE, "{")); E.push_back(T(TokenType::RETURN, "return"));
            E.insert(E.end(), t.body.begin(), t.body.end());
            E.push_back(T(TokenType::SEMICOLON, ";")); E.push_back(T(TokenType::RBRACE, "}")); E.push_back(T(TokenType::RPAREN, ")"));
            E.push_back(T(TokenType::LPAREN, "("));
            for (size_t p = 0; p < args.size(); p++) {
                if (p) E.push_back(T(TokenType::COMMA, ","));
                E.push_back(T(TokenType::LPAREN, "(")); E.insert(E.end(), args[p].begin(), args[p].end()); E.push_back(T(TokenType::RPAREN, ")"));
            }
            E.push_back(T(TokenType::RPAREN, ")"));
        } else {                                 // print / print { } : عبارات print داخل كتلة معزولة تربط المعاملات بـ let
            // سمات إضافية بعد الاستعمال (حتى ';') تُلحق بكل سطر: @t(a) upper=true @box();
            size_t j = k;
            while (j < in.size()) {
                const Token& tk = in[j];
                if (tk.type == TokenType::END_OF_FILE || tk.type == TokenType::SEMICOLON || tk.type == TokenType::RBRACE) break;
                if (tOpen(tk)) { size_t m = tMatch(in, j); if (m == std::string::npos) break; j = m + 1; continue; }
                j++;
            }
            std::vector<Token> extra(in.begin() + k, in.begin() + j);
            k = (j < in.size() && in[j].type == TokenType::SEMICOLON) ? j + 1 : j;
            bool bind = !t.params.empty();
            if (bind) {
                int id = printTmplSeq++;
                std::vector<std::string> tmp;
                E.push_back(T(TokenType::LBRACE, "{"));
                for (size_t p = 0; p < args.size(); p++) {
                    tmp.push_back("__ptmp" + std::to_string(id) + "_" + std::to_string(p));
                    E.push_back(T(TokenType::LET, "let")); E.push_back(T(TokenType::IDENT, tmp.back().c_str()));
                    E.push_back(T(TokenType::EQUAL, "="));
                    E.push_back(T(TokenType::LPAREN, "(")); E.insert(E.end(), args[p].begin(), args[p].end()); E.push_back(T(TokenType::RPAREN, ")"));
                    E.push_back(T(TokenType::SEMICOLON, ";"));
                }
                for (size_t p = 0; p < t.params.size(); p++) {
                    E.push_back(T(TokenType::LET, "let")); E.push_back(T(TokenType::IDENT, t.params[p].name.c_str()));
                    E.push_back(T(TokenType::EQUAL, "=")); E.push_back(T(TokenType::IDENT, tmp[p].c_str()));
                    E.push_back(T(TokenType::SEMICOLON, ";"));
                }
            }
            auto emitLine = [&](const std::vector<Token>& line) {
                E.push_back(T(TokenType::PRINT, "print"));
                E.insert(E.end(), line.begin(), line.end());
                E.insert(E.end(), extra.begin(), extra.end());
                E.push_back(T(TokenType::SEMICOLON, ";"));
            };
            if (t.kind == 1) emitLine(t.body); else for (auto& ln : t.lines) emitLine(ln);
            if (bind) E.push_back(T(TokenType::RBRACE, "}"));
        }
        auto res = expandTmplStream(E, depth + 1);
        out.insert(out.end(), res.begin(), res.end());
        return k;
    }
    // 2) as a, b
    std::map<std::string, std::string> rename;
    if (k < in.size() && tIsWord(in[k], "as")) {
        std::vector<std::string> names; k++;
        while (true) {
            if (k >= in.size() || in[k].type != TokenType::IDENT)
                throw err(diag::Code::E0010_ParserError, k < in.size() ? in[k] : atTok, "Expected a variable name after 'as'");
            names.push_back(in[k].lexeme); k++;
            if (k < in.size() && in[k].type == TokenType::COMMA) { k++; continue; }
            break;
        }
        if (names.size() > t.exposed.size()) {
            std::string have;
            for (size_t q = 0; q < t.exposed.size(); q++) have += (q ? ", " : "") + t.exposed[q];
            throw errRich(diag::Code::E0010_ParserError, atTok,
                          "template '" + t.name + "' exposes " + std::to_string(t.exposed.size()) + " loop variable(s) but 'as' names " + std::to_string(names.size()),
                          t.exposed.empty() ? "this template has no renamable loop variable" : "renamable variables: " + have,
                          t.exposed.empty() ? "declare them in the definition: @tmpl " + t.name + "(...) as x ..." : "");
        }
        for (size_t q = 0; q < names.size(); q++) rename[t.exposed[q]] = names[q];
    }
    // 3) جسم الاستعمال (قوالب الكتلة)
    std::vector<Token> useBody; bool hasUseBody = false;
    if (t.block) {
        if (k < in.size() && in[k].type == TokenType::LBRACE) {
            size_t m = tMatch(in, k);
            if (m == std::string::npos) throw err(diag::Code::E0010_ParserError, in[k], "Unterminated body after @" + t.name + "(...)");
            useBody.assign(in.begin() + k, in.begin() + m + 1); k = m + 1; hasUseBody = true;
        } else {
            useBody = {tMake(atTok, TokenType::LBRACE, "{"), tMake(atTok, TokenType::RBRACE, "}")};
            if (k < in.size() && in[k].type == TokenType::SEMICOLON) k++;
        }
    }
    (void)hasUseBody;
    // 4) التعويض
    std::map<std::string, size_t> pidx;
    for (size_t p = 0; p < t.params.size(); p++) pidx[t.params[p].name] = p;
    std::vector<Token> E;
    const auto& b = t.body;
    for (size_t j = 0; j < b.size(); j++) {
        const Token& tk = b[j];
        bool afterDot = j > 0 && b[j - 1].type == TokenType::DOT;
        if (t.block && tk.type == TokenType::AT && j + 1 < b.size() && tIsWord(b[j + 1], "body")) {
            E.insert(E.end(), useBody.begin(), useBody.end());
            j++;
            if (j + 1 < b.size() && b[j + 1].type == TokenType::SEMICOLON) j++;
            continue;
        }
        if (tk.type == TokenType::IDENT && !afterDot) {
            auto pi = pidx.find(tk.lexeme);
            if (pi != pidx.end()) {
                const auto& a = args[pi->second];
                bool raw = a.size() == 1 || (j > 0 && tIsWord(b[j - 1], "in"));
                if (!raw) E.push_back(tMake(a.front(), TokenType::LPAREN, "("));
                E.insert(E.end(), a.begin(), a.end());
                if (!raw) E.push_back(tMake(a.back(), TokenType::RPAREN, ")"));
                continue;
            }
            auto ri = rename.find(tk.lexeme);
            if (ri != rename.end()) { Token c = tk; c.lexeme = ri->second; E.push_back(c); continue; }
        }
        E.push_back(tk);
    }
    // 5) قالب الترويسة: الجسم الافتراضي إن لم يُكتب جسم
    if (!t.block) {
        size_t j = k; int stop = 0; // 1=جسم موجود  2=نهاية عبارة  3=سياق تعبير
        while (j < in.size()) {
            const Token& tk = in[j];
            if (tk.type == TokenType::END_OF_FILE) { stop = 2; break; }
            if (tk.type == TokenType::LPAREN || tk.type == TokenType::LBRACKET) {
                size_t m = tMatch(in, j); if (m == std::string::npos) break; j = m + 1; continue;
            }
            if (tk.type == TokenType::LBRACE) { stop = 1; break; }
            if (tIsWord(tk, "yield")) { stop = 3; break; }
            if (tk.type == TokenType::SEMICOLON || tk.type == TokenType::RBRACE) { stop = 2; break; }
            if (tk.type == TokenType::RPAREN || tk.type == TokenType::RBRACKET) { stop = 3; break; }
            j++;
        }
        // سياق التعبير (مثل `count @t(xs);` أو `[@t(xs)]`): لا جسم افتراضي ولا خطأ
        bool stmtStart = i == 0 || in[i - 1].type == TokenType::SEMICOLON || in[i - 1].type == TokenType::RBRACE ||
                         in[i - 1].type == TokenType::LBRACE || in[i - 1].type == TokenType::COLON ||
                         in[i - 1].type == TokenType::RPAREN;
        if (stop == 2 && stmtStart) {
            if (t.defBody.empty())
                throw errRich(diag::Code::E0010_ParserError, atTok, "template '" + t.name + "' needs a loop body",
                              "it has no default body", "write @" + t.name + "(...) { ... }  or give the template one: @tmpl " + t.name + "(...) each x in xs { ... }");
            E.insert(E.end(), in.begin() + k, in.begin() + j);
            E.insert(E.end(), t.defBody.begin(), t.defBody.end());
            k = (j < in.size() && in[j].type == TokenType::SEMICOLON) ? j + 1 : j;
        }
    }
    auto res = expandTmplStream(E, depth + 1);
    out.insert(out.end(), res.begin(), res.end());
    return k;
}


} // namespace rin

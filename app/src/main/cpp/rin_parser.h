#pragma once
#include "rin_common.h"
#include "rin_ast.h"
#include "diagnostics/diagnostic.h"
#include "diagnostics/diagnostic_engine.h"

namespace rin {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, std::string filename = "<input>");
    std::vector<StmtPtr> parse();

    // Parses the full token stream while collecting as many diagnostics as it safely can
    // instead of throwing (stopping) at the first error — القسم 21/22/23 من نظام Diagnostics
    // (Multiple Errors + Error Recovery). Statements that fail to parse are skipped up to the
    // next synchronization point (';', '}', ')' or a '.end/...' closing tag). The returned
    // vector of statements may be incomplete/partial when diagnostics were emitted; callers
    // that need a strict parse (throw on first error) should keep using parse() instead.
    std::vector<StmtPtr> parseCollectingDiagnostics(diag::DiagnosticEngine& engine);

private:
    std::vector<Token> tokens;
    size_t current = 0;
    std::string file;              // اسم الملف؛ يُستخدم في كل Diagnostic صادر من هذا الـ Parser

    // helpers
    bool isAtEnd() const;
    const Token& peek() const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool checkNext(TokenType type) const; // ينظر إلى التوكن التالي (current+1) دون استهلاكه
    bool match(std::initializer_list<TokenType> types);
    const Token& consume(TokenType type, const std::string& message);

    // ---- Diagnostics helpers (src/diagnostics — انظر app/src/main/cpp/diagnostics/) ----
    // يبني RinError غنياً بموقع دقيق (سطر/عمود مأخوذان من التوكن) لأي خطأ Parser مخصص لا يمرّ
    // عبر consume(). إن كان recoveryEngine مفعّلاً (داخل parseCollectingDiagnostics) يُصدِر
    // الـ Diagnostic إلى المُجمِّع بدل رميه مباشرة، ويستدعي المستدعي synchronize() بنفسه.
    RinError err(diag::Code code, const Token& tok, std::string message) const;
    // نسخة لأخطاء تُبنى من Stmt/Expr (اللذان يحملان .line فقط بلا عمود دقيق، انظر rin_ast.h)؛
    // العمود يُقارَب بـ 1 (بداية السطر) — لا يزال أدق بكثير من عدم وجود Diagnostic إطلاقاً.
    RinError errAtLine(diag::Code code, int line, std::string message) const;
    diag::SourceLocation locOf(const Token& tok) const;
    // يحاول استخراج النص بين أول علامتي اقتباس مفردتين من رسالة على شكل "Expected ')' after ..."
    // لملء حقل expected: تلقائياً دون الحاجة لإعادة كتابة كل موقع استدعاء على حدة.
    static std::string extractQuoted(const std::string& message);
    // بعد خطأ غير قابل للاستمرار الآمن، يتقدّم حتى نقطة تزامن (';' / '}' / ')' / '.end/...')
    // حتى يمكن الاستمرار بمحاولة تحليل بقية الملف وجمع مزيد من الأخطاء دفعة واحدة.
    void synchronize();

    // statements
    StmtPtr declaration();
    StmtPtr letDeclaration();
    StmtPtr functionDeclaration();
    StmtPtr statement();
    StmtPtr printStatement();
    StmtPtr ifStatement();
    StmtPtr whileStatement();
    StmtPtr forStatement(); // for (init; condition; increment) body  -> حلقة for على طراز C
    StmtPtr plusConditionStatement(); // plus.condition(cond) { .. } / { .. } -> شرط ثلاثي عام
    StmtPtr returnStatement();
    StmtPtr breakStatement();
    StmtPtr continueStatement();
    StmtPtr rinopenStatement();
    StmtPtr objectFieldStatement();
    StmtPtr objectLiteralStatement(const Token& objectTok); // .object("id") field(value); ... container.(); .end/object
    StmtPtr viewPrintObjectStatement(); // view.print/object(expr);  (يُستدعى بعد استهلاك 'view' '.' 'print')
    StmtPtr logStatement(const Token& printTok); // print.log(...) / print.log.info/warn/error/debug(...)  (يُستدعى بعد استهلاك 'print' '.' 'log')
    int loopDepth = 0; // >0 داخل جسم حلقة while؛ يُستخدم للتحقق من صحة break/continue وقت التحليل
    std::shared_ptr<BlockStmt> block();
    StmtPtr expressionStatement();

    // data-container language (container / Containers.Group / Volume / Section / ...)
    std::string readTagKeyword();      // يقرأ "container" أو "Volume" أو "Containers.Group" ...
    std::string readOptionalName();    // يقرأ "=name" اختياريًا بعد كلمة مفتاحية
    bool checkClosingTag() const;      // هل التوكن الحالي بداية ".end/..."
    void consumeEndTag(const std::string& expectedTag, int openLine, const std::string& openName = ""); // يستهلك ويتحقق من ".end/<expectedTag>" أو اختصارها ".end;"

    StmtPtr textDeclaration();
    StmtPtr objectStyleFieldDeclaration(ObjectStyleFieldKind kind); // txt/img/object.file/Fonts/background/css3 name=...; (حصراً داخل @Object)
    StmtPtr atBlock();                 // @container / @container.pipe / @container.data / @container.api / @container.import / @Containers.Group / @Volume
    StmtPtr importStatement();         // @import "path"; / @import "path" as alias;   (يُستدعى بعد استهلاك '@' و'import')
    void validateDataContainerBody(const std::vector<StmtPtr>& body); // يمنع تعريف الدوال أو الحاويات المتداخلة داخل container.data
    StmtPtr sectionBlock();
    StmtPtr translationsBlock();
    StmtPtr translationStatement();
    StmtPtr linkStatement();
    StmtPtr tyingStatement();
    StmtPtr mergeStatement();
    StmtPtr installationStatement(bool simplifiedFlag);
    StmtPtr saveStatement(bool simplifiedFlag);
    StmtPtr fileStatement();
    StmtPtr routeStatement();     // route method=... path=... status=... body=...;  (داخل container.api)
    StmtPtr simplifiedStatement(); // "simplified" متبوعة بـ installation أو save
    StmtPtr rowStatement();       // row cells=[...];              (مفهوم الجدول: container.table / table)
    StmtPtr styleStatement();     // style value="style://theme";  (مفهوم الجدول: container.table / table)
    StmtPtr documentStatement();  // document id="..." fields={...};  (قاعدة بيانات NoSQL: container.doc / doc)
    std::string readOptionalFormatAttr(); // يقرأ "format=IDENT" اختيارياً (لـ save/installation)، أو "" إن لم توجد

    // Loomtime rendering engine (view strands / reactive state) — امتداد إضافي
    std::shared_ptr<ViewStmt> viewDeclaration(); // @view.<Kind>=name ... .end/view  (يُستدعى بعد استهلاك '@' و'view')
    StmtPtr warpDeclaration();                    // warp name = expr;              (يُستدعى بعد استهلاك 'warp')
    StmtPtr themeDeclaration();                   // @theme=Name key=expr; ... .end/theme  (يُستدعى بعد استهلاك '@' و'theme')

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

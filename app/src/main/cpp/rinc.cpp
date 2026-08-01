// ============================================================================
//  rinc.cpp — RinLang Native Compiler (single file)
// ============================================================================
//
//  ملاحظة موقعه هنا: وُضع هذا الملف بجانب بقية ملفات لغة Rin في هذا المجلد
//  (rin_lexer.cpp / rin_parser.cpp / rin_interpreter.cpp / rin_ast.h / ...)
//  للرجوع إليه بسهولة كجزء من "ملفات اللغة"، لكنه أداة مستقلة تُبنى على جهاز
//  التطوير (host) بـ g++/clang عادي — وليس جزءاً من مكتبة libRinengine.so التي
//  يبنيها CMakeLists.txt هنا عبر NDK لأندرويد (لهذا لم يُضَف إلى add_library في
//  CMakeLists.txt: لا معنى لتضمينه داخل تطبيق أندرويد، لأنه يستدعي مترجم C
//  (cc/gcc/clang) عبر system() على جهاز التطوير، وهو أمر غير متاح ولا منطقي
//  داخل صندوق تطبيق أندرويد المعزول). نسخة مطابقة موجودة أيضاً في
//  ../../../../compiler/rinc.cpp مع README يشرح البناء والاستخدام.
//
//  مترجم (compiler) حقيقي للغة Rin ينتج ملفات تنفيذية أصلية (native executables)
//  بدل الاكتفاء بالتفسير (interpretation). المشروع الأصلي لا يملك أي أداة تنتج
//  ملفاً تنفيذياً مستقلاً — فقط محرك تفسير (RinEngine.kt -> libRinengine.so)
//  يُستدعى من تطبيق أندرويد. هذا الملف يسد تلك الفجوة.
//
//  الطريقة: rinc.cpp هو Front-end كامل (Lexer + Parser + Code Generator) مكتوب
//  بلغة C++. يقرأ ملف .rin ثم:
//    1) يحلّله لغوياً ونحوياً إلى AST.
//    2) يولّد كود C مكافئ (transpile حقيقي — بنى التحكم if/while/return تتحوّل
//       إلى بنى C فعلية، والدوال تتحوّل إلى دوال C حقيقية)، مع نظام قيم ديناميكي
//       صغير (runtime) مضمّن في نفس الملف المولَّد ليدعم الطباعة/النصوص/المصفوفات/
//       القواميس بنفس دلالات المفسّر الأصلي.
//    3) يستدعي مترجم C موجود على النظام (cc/gcc/clang) لبناء ملف تنفيذي أصلي
//       حقيقي من ذلك الكود المولَّد.
//
//  الاستخدام:
//      g++ -O2 -o rinc rinc.cpp        # يبني المترجم نفسه (مرة واحدة)
//      ./rinc program.rin               # ينتج ./program (تنفيذي) + program.c
//      ./rinc program.rin -o myapp       # يسمي التنفيذي myapp
//      ./rinc program.rin --emit-c-only  # يكتفي بتوليد program.c دون بنائه
//      ./rinc program.rin --cc=clang     # يفرض مترجم C محدد
//
//  ما يدعمه المترجم (اللغة الأساسية/الإجرائية في Rin بالكامل):
//      let / print (مع exprs متعددة و sep= و end=) / if-else / while / fun-return
//      المصفوفات [..] والقواميس {..} والفهرسة arr[i] وobj[k] (قراءة وكتابة)
//      كل المعاملات: + - * / % == != < <= > >= and or ! ، المشغل |> (pipeline)
//      استدعاء الدوال المعرَّفة من المستخدم + مكتبة قياسية مدمجة:
//        abs sqrt pow floor ceil round min max random len upper lower trim
//        substr split join indexOf replace contains charAt toString toNumber
//        toBool sum mean push pop sort keys values has remove
//        writeFile readFile appendFile fileExists deleteFile
//
//  ما هو خارج نطاق هذا المترجم (خاص بمحرك المفسّر/تطبيق أندرويد وليس له معنى
//  واضح كملف تنفيذي مستقل): @container / Containers.Group / Volume / Section /
//  Translations / link / tying / merge / installation / save / table / row /
//  style / document (NoSQL) / route / @container.api / @import.
//  يصدر المترجم خطأ واضحاً عند مصادفة أي منها بدل توليد سلوك غير صحيح صامت.
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <fstream>
#include <iostream>
#include <stdexcept>

// ============================================================================
// 1) الأخطاء
// ============================================================================
struct RincError {
    std::string message;
    int line;
    RincError(std::string m, int l) : message(std::move(m)), line(l) {}
};

// ============================================================================
// 2) المحلل اللغوي (Lexer)
// ============================================================================
enum class Tok {
    NUMBER, STRING, IDENT,
    LET, PRINT, IF, ELSE, WHILE, FUN, RETURN, TRUE_, FALSE_, NIL, AND, OR,
    BREAK, CONTINUE, // break / continue -> التحكّم المبكّر داخل حلقات while
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQUAL, EQUAL_EQUAL, BANG, BANG_EQUAL,
    LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    COLON, COMMA, SEMICOLON, AT, DOT, PIPE,
    END_OF_FILE
};

struct Token {
    Tok type;
    std::string lexeme;
    double number = 0.0;
    int line = 1;
};

// كلمات محجوزة خاصة "بلغة الحاويات/البيانات" في المفسّر الأصلي، غير مدعومة هنا.
// إن ظهرت في بداية عبارة نصدر خطأ واضحاً بدل تفسير خاطئ صامت.
static const std::unordered_set<std::string> kUnsupportedContainerWords = {
    "text", "container", "Containers", "Group", "Volume", "Section",
    "Translations", "translation", "link", "tying", "merge", "installation",
    "simplified", "save", "file", "route", "row", "style", "document"
};

class Lexer {
public:
    explicit Lexer(std::string s) : src(std::move(s)) {}

    std::vector<Token> scanTokens() {
        while (!isAtEnd()) { start = current; scanToken(); }
        Token eof; eof.type = Tok::END_OF_FILE; eof.line = line;
        tokens.push_back(eof);
        return tokens;
    }

private:
    std::string src;
    std::vector<Token> tokens;
    size_t start = 0, current = 0;
    int line = 1;

    bool isAtEnd() const { return current >= src.size(); }
    char advance() { return src[current++]; }
    char peek() const { return isAtEnd() ? '\0' : src[current]; }
    char peekNext() const { return (current + 1 >= src.size()) ? '\0' : src[current + 1]; }
    bool match(char c) { if (isAtEnd() || src[current] != c) return false; current++; return true; }

    void addToken(Tok t) { addToken(t, src.substr(start, current - start)); }
    void addToken(Tok t, const std::string& lex) {
        Token tok; tok.type = t; tok.lexeme = lex; tok.line = line;
        tokens.push_back(tok);
    }

    void scanString() {
        std::string value;
        while (peek() != '"' && !isAtEnd()) {
            char c = peek();
            if (c == '\n') { line++; value += advance(); continue; }
            if (c == '\\') {
                advance();
                if (isAtEnd()) break;
                char esc = advance();
                switch (esc) {
                    case 'n': value += '\n'; break;
                    case 't': value += '\t'; break;
                    case 'r': value += '\r'; break;
                    case '"': value += '"'; break;
                    case '\\': value += '\\'; break;
                    case '\n': line++; break;
                    default: value += esc; break;
                }
                continue;
            }
            value += advance();
        }
        if (isAtEnd()) throw RincError("String not terminated", line);
        advance();
        Token t; t.type = Tok::STRING; t.lexeme = value; t.line = line;
        tokens.push_back(t);
    }

    void scanNumber() {
        while (isdigit((unsigned char)peek())) advance();
        if (peek() == '.' && isdigit((unsigned char)peekNext())) {
            advance();
            while (isdigit((unsigned char)peek())) advance();
        }
        std::string text = src.substr(start, current - start);
        Token t; t.type = Tok::NUMBER; t.lexeme = text; t.number = std::stod(text); t.line = line;
        tokens.push_back(t);
    }

    void scanIdentifier() {
        while (isalnum((unsigned char)peek()) || peek() == '_') advance();
        std::string text = src.substr(start, current - start);
        static const std::unordered_map<std::string, Tok> kw = {
            {"let", Tok::LET}, {"print", Tok::PRINT}, {"if", Tok::IF}, {"else", Tok::ELSE},
            {"while", Tok::WHILE}, {"fun", Tok::FUN}, {"return", Tok::RETURN},
            {"true", Tok::TRUE_}, {"false", Tok::FALSE_}, {"nil", Tok::NIL},
            {"and", Tok::AND}, {"or", Tok::OR},
            {"break", Tok::BREAK}, {"continue", Tok::CONTINUE},
        };
        auto it = kw.find(text);
        addToken(it != kw.end() ? it->second : Tok::IDENT, text);
    }

    void scanToken() {
        char c = advance();
        switch (c) {
            case '(': addToken(Tok::LPAREN); break;
            case ')': addToken(Tok::RPAREN); break;
            case '{': addToken(Tok::LBRACE); break;
            case '}': addToken(Tok::RBRACE); break;
            case '[': addToken(Tok::LBRACKET); break;
            case ']': addToken(Tok::RBRACKET); break;
            case ':': addToken(Tok::COLON); break;
            case ',': addToken(Tok::COMMA); break;
            case ';': addToken(Tok::SEMICOLON); break;
            case '+': addToken(Tok::PLUS); break;
            case '-': addToken(Tok::MINUS); break;
            case '*': addToken(Tok::STAR); break;
            case '%': addToken(Tok::PERCENT); break;
            case '@': addToken(Tok::AT); break;
            case '.': addToken(Tok::DOT); break;
            case '|':
                if (match('>')) addToken(Tok::PIPE);
                else throw RincError("Unexpected character '|': did you mean '|>' ?", line);
                break;
            case '/':
                if (match('/')) { while (peek() != '\n' && !isAtEnd()) advance(); }
                else addToken(Tok::SLASH);
                break;
            case '=': addToken(match('=') ? Tok::EQUAL_EQUAL : Tok::EQUAL); break;
            case '!': addToken(match('=') ? Tok::BANG_EQUAL : Tok::BANG); break;
            case '<': addToken(match('=') ? Tok::LESS_EQUAL : Tok::LESS); break;
            case '>': addToken(match('=') ? Tok::GREATER_EQUAL : Tok::GREATER); break;
            case ' ': case '\r': case '\t': break;
            case '\n': line++; break;
            case '"': scanString(); break;
            default:
                if (isdigit((unsigned char)c)) scanNumber();
                else if (isalpha((unsigned char)c) || c == '_') scanIdentifier();
                else throw RincError(std::string("Unexpected character '") + c + "'", line);
        }
    }
};

// ============================================================================
// 3) AST
// ============================================================================
struct Expr { virtual ~Expr() = default; int line = 0; };
using ExprPtr = std::shared_ptr<Expr>;
struct Stmt { virtual ~Stmt() = default; int line = 0; };
using StmtPtr = std::shared_ptr<Stmt>;

struct NumberExpr : Expr { double value; };
struct StringExpr : Expr { std::string value; };
struct BoolExpr : Expr { bool value; };
struct NilExpr : Expr {};
struct VarExpr : Expr { std::string name; };
struct AssignExpr : Expr { std::string name; ExprPtr value; };
struct BinaryExpr : Expr { ExprPtr left; Tok op; ExprPtr right; };
struct LogicalExpr : Expr { ExprPtr left; Tok op; ExprPtr right; };
struct UnaryExpr : Expr { Tok op; ExprPtr right; };
struct CallExpr : Expr { std::string callee; std::vector<ExprPtr> args; };
struct ArrayExpr : Expr { std::vector<ExprPtr> elements; };
struct MapEntryNode { ExprPtr key; ExprPtr value; };
struct MapExpr : Expr { std::vector<MapEntryNode> entries; };
struct IndexExpr : Expr { ExprPtr object; ExprPtr index; };
struct IndexSetExpr : Expr { ExprPtr object; ExprPtr index; ExprPtr value; };

struct ExpressionStmt : Stmt { ExprPtr expr; };
struct PrintStmt : Stmt { std::vector<ExprPtr> exprs; ExprPtr sep; ExprPtr end; };
struct LetStmt : Stmt { std::string name; ExprPtr initializer; };
struct BlockStmt : Stmt { std::vector<StmtPtr> statements; };
struct IfStmt : Stmt { ExprPtr condition; StmtPtr thenBranch; StmtPtr elseBranch; };
struct WhileStmt : Stmt { ExprPtr condition; StmtPtr body; };
struct FunctionStmt : Stmt { std::string name; std::vector<std::string> params; std::shared_ptr<BlockStmt> body; };
struct ReturnStmt : Stmt { ExprPtr value; };
// break; / continue; -> يُترجمان مباشرة إلى break;/continue; في C المولَّد (تطابق دلالي كامل).
struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};

// ============================================================================
// 4) المحلل النحوي (Parser) — نفس قواعد نحو Rin الأساسية
// ============================================================================
class Parser {
public:
    explicit Parser(std::vector<Token> t) : tokens(std::move(t)) {}

    std::vector<StmtPtr> parse() {
        std::vector<StmtPtr> stmts;
        while (!isAtEnd()) stmts.push_back(declaration());
        return stmts;
    }

private:
    std::vector<Token> tokens;    size_t current = 0;
    int loopDepth = 0; // >0 داخل جسم حلقة while؛ للتحقق من صحة break/continue وقت التحليل

    bool isAtEnd() const { return peek().type == Tok::END_OF_FILE; }
    const Token& peek() const { return tokens[current]; }
    const Token& previous() const { return tokens[current - 1]; }
    const Token& advance() { if (!isAtEnd()) current++; return previous(); }
    bool check(Tok t) const { return !isAtEnd() && peek().type == t; }
    bool checkNext(Tok t) const { return current + 1 < tokens.size() && tokens[current + 1].type == t; }
    bool match(std::initializer_list<Tok> types) {
        for (auto t : types) if (check(t)) { advance(); return true; }
        return false;
    }
    const Token& consume(Tok t, const std::string& msg) {
        if (check(t)) return advance();
        throw RincError(msg + " (got '" + peek().lexeme + "')", peek().line);
    }

    void rejectUnsupported() {
        if (check(Tok::AT)) {
            throw RincError(
                "الميزات المبنية على '@' (container / Containers.Group / Volume / @import ...) "
                "غير مدعومة في المترجم الأصلي (rinc) — وهي خاصة بمحرّك المفسّر. "
                "استخدم تطبيق Rin (المفسّر) لتشغيل هذا الملف بدلاً من تجميعه.", peek().line);
        }
        // ملاحظة مهمة: هذه الكلمات (save/text/link/route/document/...) ليست Token محجوزة في
        // اللغة الأساسية (تُقرأ IDENT عادي)، لذا لا نُخطئها إلا حين يكون *السياق* التالي فعلاً
        // شكل عبارة حاوية (word attr=... أو word;) — وليس حين تُستخدم كاسم متغير عادي في تعبير
        // (save = 5; / save(x); / save[0] = 1;). هذا يمنع رفض برامج سليمة 100% خطأً فقط لأنها
        // تستخدم أحد هذه الأسماء كمتغيّر.
        if (check(Tok::IDENT) && kUnsupportedContainerWords.count(peek().lexeme)) {
            bool looksLikeContainerStmt =
                checkNext(Tok::IDENT) ||          // save path=...; / document id=...; / text name=...;
                checkNext(Tok::SEMICOLON) ||       // save; / installation name;  (اسم بلا متابعة)
                checkNext(Tok::STRING) ||           // installation "..."; (نادر لكن ممكن)
                checkNext(Tok::AT);                 // container.save ... إلخ (نادر)
            if (looksLikeContainerStmt) {
                throw RincError(
                    "الكلمة '" + peek().lexeme + "' جزء من \"لغة الحاويات/البيانات\" (container/table/doc/"
                    "save/installation/link/tying/merge/...) وهي غير مدعومة في المترجم الأصلي (rinc). "
                    "استخدم تطبيق Rin (المفسّر) لتشغيل هذا الملف بدلاً من تجميعه.", peek().line);
            }
        }
    }

    StmtPtr declaration() {
        rejectUnsupported();
        if (match({Tok::LET})) return letDeclaration();
        if (match({Tok::FUN})) return functionDeclaration();
        return statement();
    }

    StmtPtr letDeclaration() {
        Token name = consume(Tok::IDENT, "Expected variable name after 'let'");
        ExprPtr init = nullptr;
        if (match({Tok::EQUAL})) init = expression();
        consume(Tok::SEMICOLON, "Expected ';' after variable declaration");
        auto s = std::make_shared<LetStmt>(); s->name = name.lexeme; s->initializer = init; s->line = name.line;
        return s;
    }

    StmtPtr functionDeclaration() {
        Token name = consume(Tok::IDENT, "Expected function name after 'fun'");
        consume(Tok::LPAREN, "Expected '(' after function name");
        std::vector<std::string> params;
        if (!check(Tok::RPAREN)) {
            do { params.push_back(consume(Tok::IDENT, "Expected parameter name").lexeme); }
            while (match({Tok::COMMA}));
        }
        consume(Tok::RPAREN, "Expected ')' after parameters");
        consume(Tok::LBRACE, "Expected '{' before function body");
        int savedLoopDepth = loopDepth;
        loopDepth = 0; // جسم الدالة يبدأ سياق حلقة جديداً من الصفر (نفس منطق المفسّر)
        auto body = block();
        loopDepth = savedLoopDepth;
        auto fn = std::make_shared<FunctionStmt>();
        fn->name = name.lexeme; fn->params = params; fn->body = body; fn->line = name.line;
        return fn;
    }

    StmtPtr statement() {
        if (match({Tok::PRINT})) return printStatement();
        if (match({Tok::IF})) return ifStatement();
        if (match({Tok::WHILE})) return whileStatement();
        if (match({Tok::RETURN})) return returnStatement();
        if (match({Tok::BREAK})) return breakStatement();
        if (match({Tok::CONTINUE})) return continueStatement();
        if (check(Tok::LBRACE)) { advance(); return block(); }
        return expressionStatement();
    }

    StmtPtr printStatement() {
        Token tok = previous();
        auto s = std::make_shared<PrintStmt>();
        s->exprs.push_back(expression());
        while (match({Tok::COMMA})) s->exprs.push_back(expression());
        bool sawSep = false, sawEnd = false;
        while (check(Tok::IDENT) && (peek().lexeme == "sep" || peek().lexeme == "end")) {
            std::string attr = advance().lexeme;
            consume(Tok::EQUAL, "Expected '=' after '" + attr + "' in print statement");
            ExprPtr value = expression();
            if (attr == "sep") {
                if (sawSep) throw RincError("'print': 'sep' attribute repeated", tok.line);
                s->sep = value; sawSep = true;
            } else {
                if (sawEnd) throw RincError("'print': 'end' attribute repeated", tok.line);
                s->end = value; sawEnd = true;
            }
        }
        consume(Tok::SEMICOLON, "Expected ';' after print statement");
        s->line = tok.line;
        return s;
    }

    StmtPtr ifStatement() {
        consume(Tok::LPAREN, "Expected '(' after 'if'");
        auto cond = expression();
        consume(Tok::RPAREN, "Expected ')' after if condition");
        auto thenB = statement();
        StmtPtr elseB = nullptr;
        if (match({Tok::ELSE})) elseB = statement();
        auto s = std::make_shared<IfStmt>();
        s->condition = cond; s->thenBranch = thenB; s->elseBranch = elseB; s->line = cond->line;
        return s;
    }

    StmtPtr whileStatement() {
        consume(Tok::LPAREN, "Expected '(' after 'while'");
        auto cond = expression();
        consume(Tok::RPAREN, "Expected ')' after while condition");
        loopDepth++;
        auto body = statement();
        loopDepth--;
        auto s = std::make_shared<WhileStmt>(); s->condition = cond; s->body = body; s->line = cond->line;
        return s;
    }

    StmtPtr returnStatement() {
        Token kw = previous();
        ExprPtr value = nullptr;
        if (!check(Tok::SEMICOLON)) value = expression();
        consume(Tok::SEMICOLON, "Expected ';' after return value");
        auto s = std::make_shared<ReturnStmt>(); s->value = value; s->line = kw.line;
        return s;
    }

    StmtPtr breakStatement() {
        Token kw = previous();
        if (loopDepth == 0) throw RincError("'break' used outside of a loop", kw.line);
        consume(Tok::SEMICOLON, "Expected ';' after 'break'");
        auto s = std::make_shared<BreakStmt>(); s->line = kw.line;
        return s;
    }

    StmtPtr continueStatement() {
        Token kw = previous();
        if (loopDepth == 0) throw RincError("'continue' used outside of a loop", kw.line);
        consume(Tok::SEMICOLON, "Expected ';' after 'continue'");
        auto s = std::make_shared<ContinueStmt>(); s->line = kw.line;
        return s;
    }

    std::shared_ptr<BlockStmt> block() {
        auto b = std::make_shared<BlockStmt>();
        while (!check(Tok::RBRACE) && !isAtEnd()) b->statements.push_back(declaration());
        consume(Tok::RBRACE, "Expected '}' after block");
        return b;
    }

    StmtPtr expressionStatement() {
        auto e = expression();
        consume(Tok::SEMICOLON, "Expected ';' after expression");
        auto s = std::make_shared<ExpressionStmt>(); s->expr = e; s->line = e->line;
        return s;
    }

    // ---- expressions (same precedence chain as the original interpreter) ----
    ExprPtr expression() { return assignment(); }

    ExprPtr assignment() {
        auto expr = pipeline();
        if (match({Tok::EQUAL})) {
            Token eq = previous();
            auto value = assignment();
            if (auto v = std::dynamic_pointer_cast<VarExpr>(expr)) {
                auto a = std::make_shared<AssignExpr>(); a->name = v->name; a->value = value; a->line = eq.line;
                return a;
            }
            if (auto idx = std::dynamic_pointer_cast<IndexExpr>(expr)) {
                auto s = std::make_shared<IndexSetExpr>();
                s->object = idx->object; s->index = idx->index; s->value = value; s->line = eq.line;
                return s;
            }
            throw RincError("Invalid assignment target", eq.line);
        }
        return expr;
    }

    ExprPtr pipeline() {
        auto expr = logicOr();
        while (match({Tok::PIPE})) {
            Token opTok = previous();
            auto rhs = logicOr();
            std::shared_ptr<CallExpr> callExpr = std::dynamic_pointer_cast<CallExpr>(rhs);
            if (!callExpr) {
                if (auto v = std::dynamic_pointer_cast<VarExpr>(rhs)) {
                    callExpr = std::make_shared<CallExpr>(); callExpr->callee = v->name; callExpr->line = v->line;
                } else {
                    throw RincError("Expected a function call after '|>'", opTok.line);
                }
            }
            auto piped = std::make_shared<CallExpr>();
            piped->callee = callExpr->callee; piped->line = opTok.line;
            piped->args.push_back(expr);
            for (auto& a : callExpr->args) piped->args.push_back(a);
            expr = piped;
        }
        return expr;
    }

    ExprPtr logicOr() {
        auto expr = logicAnd();
        while (match({Tok::OR})) {
            auto op = previous().type; auto right = logicAnd();
            auto l = std::make_shared<LogicalExpr>(); l->left = expr; l->op = op; l->right = right; l->line = expr->line;
            expr = l;
        }
        return expr;
    }
    ExprPtr logicAnd() {
        auto expr = equality();
        while (match({Tok::AND})) {
            auto op = previous().type; auto right = equality();
            auto l = std::make_shared<LogicalExpr>(); l->left = expr; l->op = op; l->right = right; l->line = expr->line;
            expr = l;
        }
        return expr;
    }
    ExprPtr equality() {
        auto expr = comparison();
        while (match({Tok::EQUAL_EQUAL, Tok::BANG_EQUAL})) {
            auto op = previous().type; auto right = comparison();
            auto b = std::make_shared<BinaryExpr>(); b->left = expr; b->op = op; b->right = right; b->line = expr->line;
            expr = b;
        }
        return expr;
    }
    ExprPtr comparison() {
        auto expr = term();
        while (match({Tok::LESS, Tok::LESS_EQUAL, Tok::GREATER, Tok::GREATER_EQUAL})) {
            auto op = previous().type; auto right = term();
            auto b = std::make_shared<BinaryExpr>(); b->left = expr; b->op = op; b->right = right; b->line = expr->line;
            expr = b;
        }
        return expr;
    }
    ExprPtr term() {
        auto expr = factor();
        while (match({Tok::PLUS, Tok::MINUS})) {
            auto op = previous().type; auto right = factor();
            auto b = std::make_shared<BinaryExpr>(); b->left = expr; b->op = op; b->right = right; b->line = expr->line;
            expr = b;
        }
        return expr;
    }
    ExprPtr factor() {
        auto expr = unary();
        while (match({Tok::STAR, Tok::SLASH, Tok::PERCENT})) {
            auto op = previous().type; auto right = unary();
            auto b = std::make_shared<BinaryExpr>(); b->left = expr; b->op = op; b->right = right; b->line = expr->line;
            expr = b;
        }
        return expr;
    }
    ExprPtr unary() {
        if (match({Tok::BANG, Tok::MINUS})) {
            auto op = previous().type; auto right = unary();
            auto u = std::make_shared<UnaryExpr>(); u->op = op; u->right = right; u->line = right->line;
            return u;
        }
        return call();
    }
    ExprPtr call() {
        auto expr = primary();
        for (;;) {
            if (match({Tok::LPAREN})) {
                auto v = std::dynamic_pointer_cast<VarExpr>(expr);
                if (!v) throw RincError("Only functions can be called", previous().line);
                auto c = std::make_shared<CallExpr>(); c->callee = v->name; c->line = previous().line;
                if (!check(Tok::RPAREN)) {
                    do { c->args.push_back(expression()); } while (match({Tok::COMMA}));
                }
                consume(Tok::RPAREN, "Expected ')' after arguments");
                expr = c;
            } else if (match({Tok::LBRACKET})) {
                Token br = previous();
                auto index = expression();
                consume(Tok::RBRACKET, "Expected ']' after index");
                auto ie = std::make_shared<IndexExpr>(); ie->object = expr; ie->index = index; ie->line = br.line;
                expr = ie;
            } else break;
        }
        return expr;
    }
    ExprPtr primary() {
        if (match({Tok::FALSE_})) { auto e = std::make_shared<BoolExpr>(); e->value = false; e->line = previous().line; return e; }
        if (match({Tok::TRUE_})) { auto e = std::make_shared<BoolExpr>(); e->value = true; e->line = previous().line; return e; }
        if (match({Tok::NIL})) { auto e = std::make_shared<NilExpr>(); e->line = previous().line; return e; }
        if (match({Tok::NUMBER})) { auto e = std::make_shared<NumberExpr>(); e->value = previous().number; e->line = previous().line; return e; }
        if (match({Tok::STRING})) { auto e = std::make_shared<StringExpr>(); e->value = previous().lexeme; e->line = previous().line; return e; }
        if (match({Tok::IDENT})) { auto e = std::make_shared<VarExpr>(); e->name = previous().lexeme; e->line = previous().line; return e; }
        if (match({Tok::LPAREN})) {
            auto e = expression();
            consume(Tok::RPAREN, "Expected ')' after expression");
            return e;
        }
        if (match({Tok::LBRACKET})) {
            Token st = previous();
            auto arr = std::make_shared<ArrayExpr>(); arr->line = st.line;
            if (!check(Tok::RBRACKET)) {
                do { arr->elements.push_back(expression()); } while (match({Tok::COMMA}));
            }
            consume(Tok::RBRACKET, "Expected ']' after array elements");
            return arr;
        }
        if (match({Tok::LBRACE})) {
            Token st = previous();
            auto m = std::make_shared<MapExpr>(); m->line = st.line;
            if (!check(Tok::RBRACE)) {
                do {
                    if (check(Tok::RBRACE)) break;
                    ExprPtr key;
                    if (check(Tok::STRING)) {
                        auto lit = std::make_shared<StringExpr>(); lit->value = advance().lexeme; key = lit;
                    } else if (check(Tok::IDENT)) {
                        auto lit = std::make_shared<StringExpr>(); lit->value = advance().lexeme; key = lit;
                    } else {
                        throw RincError("Expected a key (name or string) in map literal", peek().line);
                    }
                    consume(Tok::COLON, "Expected ':' after map key");
                    auto val = expression();
                    m->entries.push_back({key, val});
                } while (match({Tok::COMMA}));
            }
            consume(Tok::RBRACE, "Expected '}' after map entries");
            return m;
        }
        rejectUnsupported();
        throw RincError("Expected expression", peek().line);
    }
};

// ============================================================================
// 5) مولّد كود C (Code generator) — التحويل (transpile) الحقيقي
// ============================================================================
// أسماء المتغيرات/الدوال في Rin تُترجم مباشرة إلى متغيرات/دوال C حقيقية (بادئة
// rv_/rf_ لتفادي أي تصادم مع كلمات C المحجوزة أو رموز الـ runtime).
class CodeGen {
public:
    std::string generate(const std::vector<StmtPtr>& program) {
        collectFunctions(program);
        out << RUNTIME_HEADER;

        // تصريحات أولية (forward declarations) لكل دوال المستخدم حتى يمكن للدوال
        // استدعاء بعضها البعض (بما فيها التكرار المتبادل) بأي ترتيب تعريف.
        for (auto& fn : functions) {
            out << "static Value rf_" << fn->name << "(Value* args, int argc);\n";
        }
        out << "\n";

        for (auto& fn : functions) emitFunction(fn);

        out << "\nint main(int argc, char** argv) {\n";
        out << "    (void)argc; (void)argv;\n";
        indent = 1;
        for (auto& s : program) {
            if (std::dynamic_pointer_cast<FunctionStmt>(s)) continue; // تم توليدها كدالة C مستقلة بالأعلى
            emitStmt(s, globalScope);
        }
        out << "    return 0;\n}\n";
        return out.str();
    }

private:
    std::ostringstream out;
    int indent = 0;
    int tmpCounter = 0;
    std::vector<std::shared_ptr<FunctionStmt>> functions;
    // بيئة تسمية بسيطة: مجموعة الأسماء المُعلَنة، فقط لتوليد أسماء C صالحة (لا حاجة لأكثر
    // من ذلك لأن نطاقات C {} تطابق نطاقات Rin بشكل طبيعي هنا).
    struct Scope { bool isGlobal; };
    Scope globalScope{true};

    void pad() { for (int i = 0; i < indent; i++) out << "    "; }

    void collectFunctions(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) {
            if (auto fn = std::dynamic_pointer_cast<FunctionStmt>(s)) {
                functions.push_back(fn);
            } else if (auto blk = std::dynamic_pointer_cast<BlockStmt>(s)) {
                collectFunctions(blk->statements);
            } else if (auto ifs = std::dynamic_pointer_cast<IfStmt>(s)) {
                std::vector<StmtPtr> v{ifs->thenBranch}; if (ifs->elseBranch) v.push_back(ifs->elseBranch);
                collectFunctions(v);
            } else if (auto w = std::dynamic_pointer_cast<WhileStmt>(s)) {
                std::vector<StmtPtr> v{w->body}; collectFunctions(v);
            }
        }
    }

    static std::string cname(const std::string& rinName) { return "rv_" + rinName; }

    void emitFunction(const std::shared_ptr<FunctionStmt>& fn) {
        out << "static Value rf_" << fn->name << "(Value* args, int argc) {\n";
        indent = 1;
        pad(); out << "(void)argc;\n";
        for (size_t i = 0; i < fn->params.size(); i++) {
            pad(); out << "Value " << cname(fn->params[i]) << " = (argc > " << i << ") ? args[" << i << "] : rt_nil();\n";
        }
        for (auto& st : fn->body->statements) emitStmt(st, globalScope);
        pad(); out << "return rt_nil();\n";
        indent = 0;
        out << "}\n\n";
    }

    void emitBlockBody(const std::shared_ptr<BlockStmt>& blk, Scope& sc) {
        out << "{\n"; indent++;
        for (auto& s : blk->statements) emitStmt(s, sc);
        indent--; pad(); out << "}\n";
    }

    void emitStmt(const StmtPtr& stmt, Scope& sc) {
        if (auto s = std::dynamic_pointer_cast<LetStmt>(stmt)) {
            pad();
            out << "Value " << cname(s->name) << " = ";
            out << (s->initializer ? exprStr(s->initializer, sc) : "rt_nil()");
            out << ";\n";
            return;
        }
        if (auto s = std::dynamic_pointer_cast<PrintStmt>(stmt)) {
            pad();
            out << "rt_print(" << s->exprs.size() << ", (Value[]){";
            for (size_t i = 0; i < s->exprs.size(); i++) { if (i) out << ", "; out << exprStr(s->exprs[i], sc); }
            out << "}, " << (s->sep ? exprStr(s->sep, sc) : "rt_str(\" \")")
                << ", " << (s->end ? exprStr(s->end, sc) : "rt_str(\"\\n\")") << ");\n";
            return;
        }
        if (auto s = std::dynamic_pointer_cast<ExpressionStmt>(stmt)) {
            pad(); out << "(void)(" << exprStr(s->expr, sc) << ");\n";
            return;
        }
        if (auto s = std::dynamic_pointer_cast<BlockStmt>(stmt)) {
            pad(); emitBlockBody(std::static_pointer_cast<BlockStmt>(stmt), sc);
            return;
        }
        if (auto s = std::dynamic_pointer_cast<IfStmt>(stmt)) {
            pad(); out << "if (rt_truthy(" << exprStr(s->condition, sc) << ")) ";
            emitBranch(s->thenBranch, sc);
            if (s->elseBranch) { pad(); out << "else "; emitBranch(s->elseBranch, sc); }
            return;
        }
        if (auto s = std::dynamic_pointer_cast<WhileStmt>(stmt)) {
            pad(); out << "while (rt_truthy(" << exprStr(s->condition, sc) << ")) ";
            emitBranch(s->body, sc);
            return;
        }
        if (auto s = std::dynamic_pointer_cast<ReturnStmt>(stmt)) {
            pad(); out << "return " << (s->value ? exprStr(s->value, sc) : "rt_nil()") << ";\n";
            return;
        }
        if (std::dynamic_pointer_cast<BreakStmt>(stmt)) {
            pad(); out << "break;\n";
            return;
        }
        if (std::dynamic_pointer_cast<ContinueStmt>(stmt)) {
            pad(); out << "continue;\n";
            return;
        }
        if (std::dynamic_pointer_cast<FunctionStmt>(stmt)) return; // مُولَّدة مسبقاً كدالة C مستقلة
        throw RincError("رعاية داخلية: عبارة غير مدعومة أثناء التوليد", stmt->line);
    }

    // يطبع الفرع كسطر واحد إن لم يكن Block، أو كتلة {}. يضمن نتائج صحيحة حتى لو كتب
    // المستخدم if(...) stmt; بلا أقواس (Rin تسمح بذلك مثل C).
    void emitBranch(const StmtPtr& s, Scope& sc) {
        if (auto blk = std::dynamic_pointer_cast<BlockStmt>(s)) { emitBlockBody(blk, sc); return; }
        out << "{\n"; indent++; emitStmt(s, sc); indent--; pad(); out << "}\n";
    }

    // ---- التعبيرات: تُبنى كنص C مضمَّن (تعبير C واحد) ----
    std::string exprStr(const ExprPtr& e, Scope& sc) {
        if (auto x = std::dynamic_pointer_cast<NumberExpr>(e)) {
            std::ostringstream ss; ss << "rt_num(" << x->value << ")"; return ss.str();
        }
        if (auto x = std::dynamic_pointer_cast<StringExpr>(e)) {
            return "rt_str(" + cLiteral(x->value) + ")";
        }
        if (auto x = std::dynamic_pointer_cast<BoolExpr>(e)) {
            return std::string("rt_bool(") + (x->value ? "1" : "0") + ")";
        }
        if (std::dynamic_pointer_cast<NilExpr>(e)) return "rt_nil()";
        if (auto x = std::dynamic_pointer_cast<VarExpr>(e)) {
            if (x->name == "PI") return "rt_num(3.14159265358979323846)";
            if (x->name == "E") return "rt_num(2.71828182845904523536)";
            return cname(x->name);
        }
        if (auto x = std::dynamic_pointer_cast<AssignExpr>(e)) {
            return "(" + cname(x->name) + " = " + exprStr(x->value, sc) + ")";
        }
        if (auto x = std::dynamic_pointer_cast<LogicalExpr>(e)) {
            std::string l = exprStr(x->left, sc), r = exprStr(x->right, sc);
            return x->op == Tok::OR ? ("rt_logic_or(" + l + ", " + r + ")")
                                     : ("rt_logic_and(" + l + ", " + r + ")");
        }
        if (auto x = std::dynamic_pointer_cast<UnaryExpr>(e)) {
            std::string r = exprStr(x->right, sc);
            return x->op == Tok::MINUS ? ("rt_neg(" + r + ")") : ("rt_not(" + r + ")");
        }
        if (auto x = std::dynamic_pointer_cast<BinaryExpr>(e)) {
            std::string l = exprStr(x->left, sc), r = exprStr(x->right, sc);
            switch (x->op) {
                case Tok::PLUS: return "rt_add(" + l + ", " + r + ")";
                case Tok::MINUS: return "rt_sub(" + l + ", " + r + ")";
                case Tok::STAR: return "rt_mul(" + l + ", " + r + ")";
                case Tok::SLASH: return "rt_div(" + l + ", " + r + ")";
                case Tok::PERCENT: return "rt_mod(" + l + ", " + r + ")";
                case Tok::GREATER: return "rt_gt(" + l + ", " + r + ")";
                case Tok::GREATER_EQUAL: return "rt_ge(" + l + ", " + r + ")";
                case Tok::LESS: return "rt_lt(" + l + ", " + r + ")";
                case Tok::LESS_EQUAL: return "rt_le(" + l + ", " + r + ")";
                case Tok::EQUAL_EQUAL: return "rt_eq(" + l + ", " + r + ")";
                case Tok::BANG_EQUAL: return "rt_neq(" + l + ", " + r + ")";
                default: break;
            }
        }
        if (auto x = std::dynamic_pointer_cast<ArrayExpr>(e)) {
            std::ostringstream ss;
            ss << "rt_array_new(" << x->elements.size() << ", (Value[]){";
            if (x->elements.empty()) ss << "rt_nil()"; // مصفوفة فارغة C{} غير مسموحة، عنصر وهمي مُتجاهَل بالطول 0
            for (size_t i = 0; i < x->elements.size(); i++) { if (i) ss << ", "; ss << exprStr(x->elements[i], sc); }
            ss << "})";
            return ss.str();
        }
        if (auto x = std::dynamic_pointer_cast<MapExpr>(e)) {
            std::ostringstream ks, vs;
            ks << "(Value[]){"; vs << "(Value[]){";
            if (x->entries.empty()) { ks << "rt_nil()"; vs << "rt_nil()"; }
            for (size_t i = 0; i < x->entries.size(); i++) {
                if (i) { ks << ", "; vs << ", "; }
                ks << exprStr(x->entries[i].key, sc);
                vs << exprStr(x->entries[i].value, sc);
            }
            ks << "}"; vs << "}";
            std::ostringstream ss;
            ss << "rt_map_new(" << x->entries.size() << ", " << ks.str() << ", " << vs.str() << ")";
            return ss.str();
        }
        if (auto x = std::dynamic_pointer_cast<IndexExpr>(e)) {
            return "rt_index_get(" + exprStr(x->object, sc) + ", " + exprStr(x->index, sc) + ", " + std::to_string(x->line) + ")";
        }
        if (auto x = std::dynamic_pointer_cast<IndexSetExpr>(e)) {
            return "rt_index_set(" + exprStr(x->object, sc) + ", " + exprStr(x->index, sc) + ", " + exprStr(x->value, sc) + ", " + std::to_string(x->line) + ")";
        }
        if (auto x = std::dynamic_pointer_cast<CallExpr>(e)) {
            return emitCall(x, sc);
        }
        throw RincError("رعاية داخلية: تعبير غير مدعوم أثناء التوليد", e->line);
    }

    std::string emitCall(const std::shared_ptr<CallExpr>& c, Scope& sc) {
        std::ostringstream args;
        args << "(Value[]){";
        if (c->args.empty()) args << "rt_nil()";
        for (size_t i = 0; i < c->args.size(); i++) { if (i) args << ", "; args << exprStr(c->args[i], sc); }
        args << "}";
        std::string argsArr = args.str();
        int n = (int)c->args.size();

        bool isUser = false;
        for (auto& fn : functions) if (fn->name == c->callee) { isUser = true; break; }
        if (isUser) {
            return "rf_" + c->callee + "(" + argsArr + ", " + std::to_string(n) + ")";
        }
        static const std::unordered_set<std::string> natives = {
            "abs","sqrt","pow","floor","ceil","round","min","max","random",
            "len","upper","lower","trim","substr","split","join","indexOf",
            "replace","contains","charAt","toString","toNumber","toBool",
            "sum","mean","push","pop","sort","keys","values","has","remove",
            "writeFile","readFile","appendFile","fileExists","deleteFile"
        };
        if (natives.count(c->callee)) {
            return "rt_native_" + c->callee + "(" + argsArr + ", " + std::to_string(n) + ")";
        }
        throw RincError("دالة غير معرَّفة: '" + c->callee + "'", c->line);
    }

    static std::string cLiteral(const std::string& s) {
        std::string r = "\"";
        for (unsigned char c : s) {
            switch (c) {
                case '"': r += "\\\""; break;
                case '\\': r += "\\\\"; break;
                case '\n': r += "\\n"; break;
                case '\t': r += "\\t"; break;
                case '\r': r += "\\r"; break;
                default:
                    if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\x%02x", c); r += buf; }
                    else r += (char)c;
            }
        }
        r += "\"";
        return r;
    }

    // ---- Runtime C مضمَّن في كل ملف مولَّد (قيم ديناميكية + مكتبة قياسية) ----
    static const char* RUNTIME_HEADER;
};

const char* CodeGen::RUNTIME_HEADER = R"RTC(// ---- Auto-generated by rinc (RinLang native compiler) — do not edit ----
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

typedef enum { RT_NIL, RT_NUM, RT_STR, RT_BOOL, RT_ARR, RT_MAP } RType;
typedef struct Value Value;
typedef struct { Value* items; int len, cap; } RArray;
typedef struct { Value* keys; Value* vals; int len, cap; } RMap;

struct Value {
    RType t;
    double num;
    char* str;
    RArray* arr;
    RMap* map;
};

static Value rt_nil(void) { Value v; v.t = RT_NIL; v.num = 0; v.str = NULL; v.arr = NULL; v.map = NULL; return v; }
static Value rt_num(double n) { Value v = rt_nil(); v.t = RT_NUM; v.num = n; return v; }
static Value rt_bool(int b) { Value v = rt_nil(); v.t = RT_BOOL; v.num = b ? 1 : 0; return v; }
static Value rt_str(const char* s) { Value v = rt_nil(); v.t = RT_STR; v.str = strdup(s ? s : ""); return v; }
static Value rt_str_own(char* s) { Value v = rt_nil(); v.t = RT_STR; v.str = s; return v; }

static void rt_fatal(int line, const char* msg) {
    fprintf(stderr, "خطأ Rin وقت التشغيل (سطر %d): %s\n", line, msg);
    exit(1);
}

static RArray* rt_arr_alloc(int cap) {
    RArray* a = (RArray*)malloc(sizeof(RArray));
    a->cap = cap < 4 ? 4 : cap; a->len = 0;
    a->items = (Value*)malloc(sizeof(Value) * a->cap);
    return a;
}
static void rt_arr_push(RArray* a, Value v) {
    if (a->len >= a->cap) { a->cap *= 2; a->items = (Value*)realloc(a->items, sizeof(Value) * a->cap); }
    a->items[a->len++] = v;
}
static Value rt_array_new(int n, Value* items) {
    RArray* a = rt_arr_alloc(n > 0 ? n : 4);
    for (int i = 0; i < n; i++) rt_arr_push(a, items[i]);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = a; return v;
}

static int rt_equals(Value a, Value b);

static RMap* rt_map_alloc(int cap) {
    RMap* m = (RMap*)malloc(sizeof(RMap));
    m->cap = cap < 4 ? 4 : cap; m->len = 0;
    m->keys = (Value*)malloc(sizeof(Value) * m->cap);
    m->vals = (Value*)malloc(sizeof(Value) * m->cap);
    return m;
}
static void rt_map_set(RMap* m, Value k, Value v) {
    for (int i = 0; i < m->len; i++) if (rt_equals(m->keys[i], k)) { m->vals[i] = v; return; }
    if (m->len >= m->cap) {
        m->cap *= 2;
        m->keys = (Value*)realloc(m->keys, sizeof(Value) * m->cap);
        m->vals = (Value*)realloc(m->vals, sizeof(Value) * m->cap);
    }
    m->keys[m->len] = k; m->vals[m->len] = v; m->len++;
}
static Value rt_map_new(int n, Value* keys, Value* vals) {
    RMap* m = rt_map_alloc(n > 0 ? n : 4);
    for (int i = 0; i < n; i++) rt_map_set(m, keys[i], vals[i]);
    Value v = rt_nil(); v.t = RT_MAP; v.map = m; return v;
}

static int rt_equals(Value a, Value b) {
    if (a.t != b.t) return 0;
    switch (a.t) {
        case RT_NIL: return 1;
        case RT_NUM: return a.num == b.num;
        case RT_BOOL: return a.num == b.num;
        case RT_STR: return strcmp(a.str, b.str) == 0;
        case RT_ARR: {
            if (a.arr == b.arr) return 1;
            if (a.arr->len != b.arr->len) return 0;
            for (int i = 0; i < a.arr->len; i++) if (!rt_equals(a.arr->items[i], b.arr->items[i])) return 0;
            return 1;
        }
        case RT_MAP: {
            if (a.map == b.map) return 1;
            if (a.map->len != b.map->len) return 0;
            for (int i = 0; i < a.map->len; i++) if (!rt_equals(a.map->vals[i], b.map->vals[i])) return 0;
            return 1;
        }
    }
    return 0;
}

static int rt_truthy(Value v) {
    if (v.t == RT_NIL) return 0;
    if (v.t == RT_BOOL) return v.num != 0;
    if (v.t == RT_NUM) return v.num != 0.0;
    return 1;
}

static char* rt_num_to_str(double n) {
    char buf[64];
    if (n == (long long)n) snprintf(buf, sizeof buf, "%lld", (long long)n);
    else snprintf(buf, sizeof buf, "%g", n);
    return strdup(buf);
}

static char* rt_repr(Value v); // forward

static char* rt_to_display(Value v) {
    switch (v.t) {
        case RT_NIL: return strdup("nil");
        case RT_BOOL: return strdup(v.num != 0 ? "true" : "false");
        case RT_STR: return strdup(v.str);
        case RT_NUM: return rt_num_to_str(v.num);
        case RT_ARR: {
            size_t cap = 64, len = 0; char* buf = (char*)malloc(cap); buf[0] = 0;
            #define APPEND(s) do { const char* _s = (s); size_t _l = strlen(_s); \
                while (len + _l + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); } \
                memcpy(buf + len, _s, _l); len += _l; buf[len] = 0; } while (0)
            APPEND("[");
            for (int i = 0; i < v.arr->len; i++) {
                if (i) APPEND(", ");
                char* r = rt_repr(v.arr->items[i]); APPEND(r); free(r);
            }
            APPEND("]");
            #undef APPEND
            return buf;
        }
        case RT_MAP: {
            size_t cap = 64, len = 0; char* buf = (char*)malloc(cap); buf[0] = 0;
            #define APPEND(s) do { const char* _s = (s); size_t _l = strlen(_s); \
                while (len + _l + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); } \
                memcpy(buf + len, _s, _l); len += _l; buf[len] = 0; } while (0)
            APPEND("{");
            for (int i = 0; i < v.map->len; i++) {
                if (i) APPEND(", ");
                char* rk = rt_repr(v.map->keys[i]); APPEND(rk); free(rk);
                APPEND(": ");
                char* rv = rt_repr(v.map->vals[i]); APPEND(rv); free(rv);
            }
            APPEND("}");
            #undef APPEND
            return buf;
        }
    }
    return strdup("nil");
}
static char* rt_repr(Value v) {
    if (v.t == RT_STR) {
        size_t l = strlen(v.str);
        char* buf = (char*)malloc(l + 3);
        buf[0] = '"'; memcpy(buf + 1, v.str, l); buf[l + 1] = '"'; buf[l + 2] = 0;
        return buf;
    }
    return rt_to_display(v);
}

static void rt_print(int n, Value* vals, Value sep, Value end) {
    const char* sepStr = (sep.t == RT_STR) ? sep.str : " ";
    const char* endStr = (end.t == RT_STR) ? end.str : "\n";
    for (int i = 0; i < n; i++) {
        if (i) fputs(sepStr, stdout);
        char* s = rt_to_display(vals[i]); fputs(s, stdout); free(s);
    }
    fputs(endStr, stdout);
}

static Value rt_logic_or(Value l, Value r) { return rt_truthy(l) ? l : r; }
static Value rt_logic_and(Value l, Value r) { return !rt_truthy(l) ? l : r; }
static Value rt_not(Value v) { return rt_bool(!rt_truthy(v)); }
static Value rt_neg(Value v) {
    if (v.t != RT_NUM) rt_fatal(0, "المعامل يجب أن يكون رقماً بعد '-' الأحادي");
    return rt_num(-v.num);
}

static void rt_need_nums(Value a, Value b, const char* op, int line) {
    if (a.t != RT_NUM || b.t != RT_NUM) {
        char msg[128]; snprintf(msg, sizeof msg, "المعاملات يجب أن تكون أرقاماً مع '%s'", op);
        rt_fatal(line, msg);
    }
}

static Value rt_add(Value a, Value b) {
    if (a.t == RT_STR || b.t == RT_STR) {
        char* sa = rt_to_display(a); char* sb = rt_to_display(b);
        size_t la = strlen(sa), lb = strlen(sb);
        char* res = (char*)malloc(la + lb + 1);
        memcpy(res, sa, la); memcpy(res + la, sb, lb); res[la + lb] = 0;
        free(sa); free(sb);
        return rt_str_own(res);
    }
    if (a.t == RT_NUM && b.t == RT_NUM) return rt_num(a.num + b.num);
    rt_fatal(0, "المعاملات يجب أن تكون أرقاماً أو نصوصاً مع '+'");
    return rt_nil();
}
static Value rt_sub(Value a, Value b) { rt_need_nums(a, b, "-", 0); return rt_num(a.num - b.num); }
static Value rt_mul(Value a, Value b) { rt_need_nums(a, b, "*", 0); return rt_num(a.num * b.num); }
static Value rt_div(Value a, Value b) {
    rt_need_nums(a, b, "/", 0);
    if (b.num == 0) rt_fatal(0, "القسمة على صفر (Division by zero)");
    return rt_num(a.num / b.num);
}
static Value rt_mod(Value a, Value b) {
    rt_need_nums(a, b, "%", 0);
    if (b.num == 0) rt_fatal(0, "القسمة على صفر (Division by zero)");
    return rt_num(fmod(a.num, b.num));
}
static Value rt_gt(Value a, Value b) { rt_need_nums(a, b, ">", 0); return rt_bool(a.num > b.num); }
static Value rt_ge(Value a, Value b) { rt_need_nums(a, b, ">=", 0); return rt_bool(a.num >= b.num); }
static Value rt_lt(Value a, Value b) { rt_need_nums(a, b, "<", 0); return rt_bool(a.num < b.num); }
static Value rt_le(Value a, Value b) { rt_need_nums(a, b, "<=", 0); return rt_bool(a.num <= b.num); }
static Value rt_eq(Value a, Value b) { return rt_bool(rt_equals(a, b)); }
static Value rt_neq(Value a, Value b) { return rt_bool(!rt_equals(a, b)); }

static Value rt_index_get(Value obj, Value idx, int line) {
    if (obj.t == RT_ARR) {
        if (idx.t != RT_NUM) rt_fatal(line, "فهرس المصفوفة يجب أن يكون رقماً");
        long i = (long)idx.num;
        if (i < 0 || i >= obj.arr->len) rt_fatal(line, "فهرس خارج الحدود (index out of range)");
        return obj.arr->items[i];
    }
    if (obj.t == RT_MAP) {
        for (int i = 0; i < obj.map->len; i++) if (rt_equals(obj.map->keys[i], idx)) return obj.map->vals[i];
        return rt_nil();
    }
    if (obj.t == RT_STR) {
        if (idx.t != RT_NUM) rt_fatal(line, "فهرس النص يجب أن يكون رقماً");
        long i = (long)idx.num;
        long n = (long)strlen(obj.str);
        if (i < 0 || i >= n) rt_fatal(line, "فهرس خارج الحدود (index out of range)");
        char buf[2] = { obj.str[i], 0 };
        return rt_str(buf);
    }
    rt_fatal(line, "لا يمكن فهرسة هذا النوع من القيم");
    return rt_nil();
}
static Value rt_index_set(Value obj, Value idx, Value val, int line) {
    if (obj.t == RT_ARR) {
        if (idx.t != RT_NUM) rt_fatal(line, "فهرس المصفوفة يجب أن يكون رقماً");
        long i = (long)idx.num;
        if (i == obj.arr->len) rt_arr_push(obj.arr, val);
        else if (i < 0 || i >= obj.arr->len) rt_fatal(line, "فهرس خارج الحدود (index out of range)");
        else obj.arr->items[i] = val;
        return val;
    }
    if (obj.t == RT_MAP) { rt_map_set(obj.map, idx, val); return val; }
    rt_fatal(line, "لا يمكن التعديل على هذا النوع من القيم عبر []");
    return rt_nil();
}

// ---- المكتبة القياسية (subset) ----
static Value rt_native_abs(Value* a, int n) { (void)n; return rt_num(fabs(a[0].num)); }
static Value rt_native_sqrt(Value* a, int n) { (void)n; return rt_num(sqrt(a[0].num)); }
static Value rt_native_pow(Value* a, int n) { (void)n; return rt_num(pow(a[0].num, a[1].num)); }
static Value rt_native_floor(Value* a, int n) { (void)n; return rt_num(floor(a[0].num)); }
static Value rt_native_ceil(Value* a, int n) { (void)n; return rt_num(ceil(a[0].num)); }
static Value rt_native_round(Value* a, int n) { (void)n; return rt_num(round(a[0].num)); }
static Value rt_native_min(Value* a, int n) { (void)n; return rt_num(a[0].num < a[1].num ? a[0].num : a[1].num); }
static Value rt_native_max(Value* a, int n) { (void)n; return rt_num(a[0].num > a[1].num ? a[0].num : a[1].num); }
static Value rt_native_random(Value* a, int n) { (void)a; (void)n; return rt_num((double)rand() / ((double)RAND_MAX + 1.0)); }
static Value rt_native_len(Value* a, int n) {
    (void)n;
    if (a[0].t == RT_STR) return rt_num((double)strlen(a[0].str));
    if (a[0].t == RT_ARR) return rt_num((double)a[0].arr->len);
    if (a[0].t == RT_MAP) return rt_num((double)a[0].map->len);
    rt_fatal(0, "'len' تحتاج نصاً أو مصفوفة أو قاموساً");
    return rt_nil();
}
static Value rt_native_upper(Value* a, int n) {
    (void)n; char* s = strdup(a[0].str); for (char* p = s; *p; p++) *p = toupper((unsigned char)*p);
    return rt_str_own(s);
}
static Value rt_native_lower(Value* a, int n) {
    (void)n; char* s = strdup(a[0].str); for (char* p = s; *p; p++) *p = tolower((unsigned char)*p);
    return rt_str_own(s);
}
static Value rt_native_trim(Value* a, int n) {
    (void)n; const char* s = a[0].str; while (isspace((unsigned char)*s)) s++;
    size_t l = strlen(s); while (l > 0 && isspace((unsigned char)s[l - 1])) l--;
    char* r = (char*)malloc(l + 1); memcpy(r, s, l); r[l] = 0; return rt_str_own(r);
}
static Value rt_native_substr(Value* a, int n) {
    const char* s = a[0].str; long len = (long)strlen(s);
    long start = (long)a[1].num;
    long count = (n >= 3 && a[2].t == RT_NUM) ? (long)a[2].num : (len - start);
    if (start < 0) start = 0; if (start > len) start = len;
    if (count < 0) count = 0; if (start + count > len) count = len - start;
    char* r = (char*)malloc(count + 1); memcpy(r, s + start, count); r[count] = 0;
    return rt_str_own(r);
}
static Value rt_native_split(Value* a, int n) {
    (void)n;
    const char* s = a[0].str; const char* sep = a[1].str;
    RArray* out = rt_arr_alloc(4);
    size_t seplen = strlen(sep);
    if (seplen == 0) {
        for (size_t i = 0; s[i]; i++) { char buf[2] = { s[i], 0 }; rt_arr_push(out, rt_str(buf)); }
    } else {
        const char* p = s;
        for (;;) {
            const char* found = strstr(p, sep);
            if (!found) { rt_arr_push(out, rt_str(p)); break; }
            size_t l = found - p;
            char* piece = (char*)malloc(l + 1); memcpy(piece, p, l); piece[l] = 0;
            rt_arr_push(out, rt_str_own(piece));
            p = found + seplen;
        }
    }
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_join(Value* a, int n) {
    const char* sep = (n >= 2 && a[1].t == RT_STR) ? a[1].str : "";
    size_t cap = 64, len = 0; char* buf = (char*)malloc(cap); buf[0] = 0;
    for (int i = 0; i < a[0].arr->len; i++) {
        if (i) { size_t l = strlen(sep); while (len + l + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); } memcpy(buf + len, sep, l); len += l; buf[len] = 0; }
        char* piece = rt_to_display(a[0].arr->items[i]);
        size_t l = strlen(piece);
        while (len + l + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        memcpy(buf + len, piece, l); len += l; buf[len] = 0;
        free(piece);
    }
    return rt_str_own(buf);
}
static Value rt_native_indexOf(Value* a, int n) {
    (void)n;
    if (a[0].t == RT_STR) {
        char* found = strstr(a[0].str, a[1].str);
        return rt_num(found ? (double)(found - a[0].str) : -1.0);
    }
    if (a[0].t == RT_ARR) {
        for (int i = 0; i < a[0].arr->len; i++) if (rt_equals(a[0].arr->items[i], a[1])) return rt_num(i);
        return rt_num(-1);
    }
    return rt_num(-1);
}
static Value rt_native_replace(Value* a, int n) {
    (void)n;
    const char* s = a[0].str; const char* from = a[1].str; const char* to = a[2].str;
    size_t fl = strlen(from);
    if (fl == 0) return rt_str(s);
    size_t cap = 64, len = 0; char* buf = (char*)malloc(cap); buf[0] = 0;
    const char* p = s;
    for (;;) {
        const char* found = strstr(p, from);
        const char* chunkEnd = found ? found : p + strlen(p);
        size_t l = chunkEnd - p;
        while (len + l + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        memcpy(buf + len, p, l); len += l; buf[len] = 0;
        if (!found) break;
        size_t tl = strlen(to);
        while (len + tl + 1 > cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        memcpy(buf + len, to, tl); len += tl; buf[len] = 0;
        p = found + fl;
    }
    return rt_str_own(buf);
}
static Value rt_native_contains(Value* a, int n) {
    (void)n;
    if (a[0].t == RT_STR) return rt_bool(strstr(a[0].str, a[1].str) != NULL);
    if (a[0].t == RT_ARR) {
        for (int i = 0; i < a[0].arr->len; i++) if (rt_equals(a[0].arr->items[i], a[1])) return rt_bool(1);
        return rt_bool(0);
    }
    if (a[0].t == RT_MAP) {
        for (int i = 0; i < a[0].map->len; i++) if (rt_equals(a[0].map->keys[i], a[1])) return rt_bool(1);
        return rt_bool(0);
    }
    return rt_bool(0);
}
static Value rt_native_charAt(Value* a, int n) {
    (void)n; long i = (long)a[1].num; long len = (long)strlen(a[0].str);
    if (i < 0 || i >= len) rt_fatal(0, "فهرس خارج الحدود (index out of range)");
    char buf[2] = { a[0].str[i], 0 }; return rt_str(buf);
}
static Value rt_native_toString(Value* a, int n) { (void)n; return rt_str_own(rt_to_display(a[0])); }
static Value rt_native_toNumber(Value* a, int n) {
    (void)n;
    if (a[0].t == RT_NUM) return a[0];
    if (a[0].t == RT_STR) { char* end; double d = strtod(a[0].str, &end); if (end == a[0].str) return rt_nil(); return rt_num(d); }
    return rt_nil();
}
static Value rt_native_toBool(Value* a, int n) { (void)n; return rt_bool(rt_truthy(a[0])); }
static Value rt_native_sum(Value* a, int n) {
    (void)n; double s = 0; for (int i = 0; i < a[0].arr->len; i++) s += a[0].arr->items[i].num; return rt_num(s);
}
static Value rt_native_mean(Value* a, int n) {
    (void)n; if (a[0].arr->len == 0) return rt_num(0);
    double s = 0; for (int i = 0; i < a[0].arr->len; i++) s += a[0].arr->items[i].num;
    return rt_num(s / a[0].arr->len);
}
static Value rt_native_push(Value* a, int n) { (void)n; rt_arr_push(a[0].arr, a[1]); return a[0]; }
static Value rt_native_pop(Value* a, int n) {
    (void)n; if (a[0].arr->len == 0) rt_fatal(0, "'pop' على مصفوفة فارغة");
    return a[0].arr->items[--a[0].arr->len];
}
static int rt_cmp_qsort(const void* pa, const void* pb) {
    const Value* a = (const Value*)pa; const Value* b = (const Value*)pb;
    if (a->t == RT_NUM && b->t == RT_NUM) return (a->num > b->num) - (a->num < b->num);
    if (a->t == RT_STR && b->t == RT_STR) return strcmp(a->str, b->str);
    return 0;
}
static Value rt_native_sort(Value* a, int n) {
    (void)n; qsort(a[0].arr->items, a[0].arr->len, sizeof(Value), rt_cmp_qsort); return a[0];
}
static Value rt_native_keys(Value* a, int n) {
    (void)n; RArray* r = rt_arr_alloc(a[0].map->len);
    for (int i = 0; i < a[0].map->len; i++) rt_arr_push(r, a[0].map->keys[i]);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = r; return v;
}
static Value rt_native_values(Value* a, int n) {
    (void)n; RArray* r = rt_arr_alloc(a[0].map->len);
    for (int i = 0; i < a[0].map->len; i++) rt_arr_push(r, a[0].map->vals[i]);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = r; return v;
}
static Value rt_native_has(Value* a, int n) {
    (void)n; for (int i = 0; i < a[0].map->len; i++) if (rt_equals(a[0].map->keys[i], a[1])) return rt_bool(1);
    return rt_bool(0);
}
static Value rt_native_remove(Value* a, int n) {
    (void)n;
    for (int i = 0; i < a[0].map->len; i++) {
        if (rt_equals(a[0].map->keys[i], a[1])) {
            for (int j = i; j < a[0].map->len - 1; j++) { a[0].map->keys[j] = a[0].map->keys[j + 1]; a[0].map->vals[j] = a[0].map->vals[j + 1]; }
            a[0].map->len--; return rt_bool(1);
        }
    }
    return rt_bool(0);
}
static Value rt_native_writeFile(Value* a, int n) {
    (void)n; FILE* f = fopen(a[0].str, "wb");
    if (!f) rt_fatal(0, "تعذّرت كتابة الملف");
    fputs(a[1].str, f); fclose(f); return rt_bool(1);
}
static Value rt_native_appendFile(Value* a, int n) {
    (void)n; FILE* f = fopen(a[0].str, "ab");
    if (!f) rt_fatal(0, "تعذّرت كتابة الملف");
    fputs(a[1].str, f); fclose(f); return rt_bool(1);
}
static Value rt_native_readFile(Value* a, int n) {
    (void)n; FILE* f = fopen(a[0].str, "rb");
    if (!f) return rt_nil();
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(sz + 1); size_t rd = fread(buf, 1, sz, f); buf[rd] = 0; fclose(f);
    return rt_str_own(buf);
}
static Value rt_native_fileExists(Value* a, int n) {
    (void)n; FILE* f = fopen(a[0].str, "rb");
    if (!f) return rt_bool(0); fclose(f); return rt_bool(1);
}
static Value rt_native_deleteFile(Value* a, int n) { (void)n; return rt_bool(remove(a[0].str) == 0); }

)RTC";

// ============================================================================
// 6) البرنامج الرئيسي: قراءة الملف -> تحليل -> توليد C -> استدعاء مترجم النظام
// ============================================================================
static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("تعذّر فتح الملف: " + path);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void printUsage() {
    std::cerr <<
        "الاستخدام: rinc <input.rin> [-o output] [--cc=COMPILER] [--emit-c-only] [--keep-c]\n"
        "  -o output        اسم الملف التنفيذي الناتج (افتراضياً اسم ملف الدخل بلا امتداد)\n"
        "  --cc=COMPILER    يفرض مترجم C محدد (مثال: --cc=clang) بدل الاكتشاف التلقائي\n"
        "  --emit-c-only    يكتفي بتوليد ملف .c دون بنائه إلى تنفيذي\n"
        "  --keep-c         يُبقي ملف .c الوسيط بعد نجاح البناء (يُحذف تلقائياً افتراضياً)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(); return 1; }
    std::string inputPath, outputPath, ccOverride;
    bool emitCOnly = false, keepC = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { outputPath = argv[++i]; }
        else if (a.rfind("--cc=", 0) == 0) { ccOverride = a.substr(5); }
        else if (a == "--emit-c-only") { emitCOnly = true; }
        else if (a == "--keep-c") { keepC = true; }
        else if (a == "-h" || a == "--help") { printUsage(); return 0; }
        else if (!a.empty() && a[0] == '-') { std::cerr << "خيار غير معروف: " << a << "\n"; printUsage(); return 1; }
        else { inputPath = a; }
    }
    if (inputPath.empty()) { printUsage(); return 1; }

    std::string base = inputPath;
    auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    auto dot = base.find_last_of('.');
    if (dot != std::string::npos) base = base.substr(0, dot);
    if (outputPath.empty()) outputPath = base;
    std::string cPath = base + ".c";

    std::string source;
    try {
        source = readFile(inputPath);
    } catch (const std::exception& ex) {
        std::cerr << "خطأ: " << ex.what() << "\n";
        return 1;
    }

    std::string generatedC;
    try {
        Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        Parser parser(tokens);
        auto program = parser.parse();
        CodeGen gen;
        generatedC = gen.generate(program);
    } catch (const RincError& e) {
        std::cerr << "خطأ تجميع (سطر " << e.line << "): " << e.message << "\n";
        return 1;
    }

    {
        std::ofstream out(cPath, std::ios::binary);
        out << generatedC;
    }
    std::cout << "✓ تم توليد الكود: " << cPath << "\n";

    if (emitCOnly) return 0;

    std::vector<std::string> compilers;
    if (!ccOverride.empty()) compilers.push_back(ccOverride);
    else { compilers.push_back("cc"); compilers.push_back("gcc"); compilers.push_back("clang"); }

    bool built = false;
    for (auto& cc : compilers) {
        std::string cmd = cc + " -O2 -o " + outputPath + " " + cPath + " -lm 2> " + outputPath + ".build.log";
        int rc = system(cmd.c_str());
        if (rc == 0) {
            built = true;
            remove((outputPath + ".build.log").c_str());
            break;
        }
    }

    if (!built) {
        std::cerr << "✗ تعذّر العثور على مترجم C يعمل (جُرِّب: ";
        for (size_t i = 0; i < compilers.size(); i++) { if (i) std::cerr << ", "; std::cerr << compilers[i]; }
        std::cerr << "). تحقّق من سجل الأخطاء في " << outputPath << ".build.log إن وُجد،\n"
                  << "أو ابنِ الكود المولَّد يدوياً: cc -O2 -o " << outputPath << " " << cPath << " -lm\n";
        return 1;
    }

    if (!keepC) remove(cPath.c_str());
    std::cout << "✓ تم بناء الملف التنفيذي: ./" << outputPath << "\n";
    return 0;
}

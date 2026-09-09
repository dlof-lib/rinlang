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
// خطأ فرعي مميَّز عن RincError العام: يُرمى فقط عند ميزة "غير مدعومة عمداً" (لغة
// حاويات/NoSQL/API/دردشة/إلخ)، لا عند خطأ نحوي حقيقي في كود المستخدم. main() يميّزه
// خصيصاً ليتحوّل تلقائياً لوضع "تضمين المفسّر" (embed fallback) بدل رفض التجميع كلياً،
// بينما تبقى الأخطاء النحوية الحقيقية (RincError العام) تفشل فوراً كما هي دائماً —
// راجع قسم "وضع تضمين المفسّر" في main() أدناه.
struct RincUnsupportedFeature : RincError {
    RincUnsupportedFeature(std::string m, int l) : RincError(std::move(m), l) {}
};

// ============================================================================
// 2) المحلل اللغوي (Lexer)
// ============================================================================
enum class Tok {
    NUMBER, STRING, IDENT,
    LET, PRINT, IF, ELSE, WHILE, FOR, FUN, RETURN, TRUE_, FALSE_, NIL, AND, OR,
    BREAK, CONTINUE, RINOPEN, // break / continue / rinopen(cond){..} -> اسم بديل لـ while
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

// أسماء الدوال الأصلية (native) الموجودة في المفسّر الأصلي (rin_interpreter.cpp) وغير المدعومة
// مباشرةً في `rinc` (مرتبطة عضوياً بمحرّك التطبيق/JNI: NoSQL، HTTP/API، دردشة/بوت، لافتات
// Loomtime، ذاكرة مؤقتة، إدارة تثبيت/مجموعات، ضغط/تجزئة bytes). عند استدعاء أيٍّ منها،
// main() يتحوّل تلقائياً لوضع "تضمين المفسّر" (embed fallback) بدل رفض التجميع كلياً — انظر
// generateEmbedFallback() أدناه. قائمة مطابقة تماماً لتدقيق natives[] في rin_interpreter.cpp.
static const std::unordered_set<std::string> kInterpreterOnlyNatives = {
    "adler32","allDocs","apiCall","apiDelete","apiGet","apiHeader","apiPatch","apiPost","apiPut",
    "apiRegister","appliedMigrations","attachToChat","bannerDismiss","bannerError","bannerInfo",
    "bannerSuccess","bannerWarning","beginTransaction","botReply","botReplyCode","botReplyMarkdown",
    "bytesFromArray","cacheClear","cacheDelete","cacheGet","cacheHas","cacheKeys","cacheSet","call",
    "callApi","chatHistory","chatMessageCount","clearChat","closeChat","commitTransaction",
    "countDocs","crc32","createIndex","defineMigration","defineRelation","defineSchema","deleteDoc",
    "docIds","dropIndex","dropSchema","exportChat","findByIndex","findDoc","getSchema",
    "groupContainers","groupMembers","hasSection","httpDelete","httpGet","httpPatch","httpPost",
    "httpPut","httpRequest","httpSetTimeout","inTransaction","insertDoc","isChatTyping",
    "isInstalled","lastChatMessage","listIndexes","listInstalled","listRelations","loadInstalled",
    "offChat","onChat","openChat","pendingMigrations","publish","queryDocs","queryOneDoc",
    "relatedDocs","rollbackMigration","rollbackTransaction","runMigration","sectionNames",
    "sectionVars","sendMessage","setChatTyping","streamReply","subscribe","tokenType","tokens",
    "unsubscribe","unwatch","updateDoc","validateDoc","watch","zlibDeflateRaw","zlibInflateRaw",

    // The "Mask" system: a live, stateful object graph owned by the running Interpreter
    // (parent/child links, tags, refs, versioning, locking...) — there is no such graph in a
    // standalone generated C program to operate on, the same reason @container/@view are out
    // of scope for this compiler already (see the top-of-file note on unsupported concepts).
    "findMask","kindOf","maskActive","maskAlias","maskAliases","maskByNamespace","maskByTag",
    "maskChildren","maskChildrenOf","maskClearRefs","maskClearTags","maskCompare","maskCount",
    "maskDepth","maskDescendants","maskDetach","maskDisable","maskEnable","maskExists",
    "maskHasAllTags","maskHasAnyTag","maskHasRef","maskHasTag","maskInfo","maskKind",
    "maskKindCount","maskKinds","maskLock","maskLocked","maskMembers","maskNames","maskNamespace",
    "maskNote","maskOf","maskParent","maskParentGet","maskParentSet","maskRef","maskRefs",
    "maskResolve","maskRoot","maskSummary","maskTag","maskTags","maskTarget","maskUnref",
    "maskUntag","maskVersion",

    // @container tree/lifecycle (spawn/inspect/call into the live container graph) and CLC
    // archive I/O (.clc container files) — same "no live graph to operate on" reasoning as Mask.
    "callFn","run","spawn","create","destroyContainer","containerNames","hasContainer",
    "childrenOf","parentOf","siblingsOf","slotsOf","getField","setField","hasField",
    "clcContainerOpen","clcContainerClose","clcContainerFileCount","clcContainerFileName",
    "clcContainerMetaName","clcContainerMetaVersion","libraryExport",

    // The interpreter's own in-memory session log (logHistory_ on the Interpreter instance) —
    // nothing for a standalone compiled program to read back from.
    "logClear","logHistory","logSave",

    // Network/Android-bridge image fetches (Loom icon/image loading) — I/O through the Android
    // app's own bridge, not something a portable generated C program has access to.
    "fetchIcon","fetchImage"
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
            {"while", Tok::WHILE}, {"for", Tok::FOR}, {"fun", Tok::FUN}, {"return", Tok::RETURN},
            {"true", Tok::TRUE_}, {"false", Tok::FALSE_}, {"nil", Tok::NIL},
            {"and", Tok::AND}, {"or", Tok::OR},
            {"break", Tok::BREAK}, {"continue", Tok::CONTINUE}, {"rinopen", Tok::RINOPEN},
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
// for (initializer; condition; increment) body -> حلقة for على طراز C، مطابقة لـ ForStmt في
// rin_parser.h/rin_interpreter.cpp: الأجزاء الثلاثة اختيارية، initializer إما 'let ...;' أو
// عبارة تعبير (expression statement)، condition الغائب يُعتبر true دائماً.
struct ForStmt : Stmt { StmtPtr initializer; ExprPtr condition; ExprPtr increment; StmtPtr body; };
// plus.condition (condition) { trueBranch } / { falseBranch } -> شرط ثلاثي عام على مستوى
// العبارات، مطابق لـ PlusConditionStmt في المفسّر الأصلي. كلتا الكتلتين إلزاميتان.
struct PlusConditionStmt : Stmt { ExprPtr condition; std::shared_ptr<BlockStmt> trueBranch; std::shared_ptr<BlockStmt> falseBranch; };
struct FunctionStmt : Stmt { std::string name; std::vector<std::string> params; std::shared_ptr<BlockStmt> body; };
struct ReturnStmt : Stmt { ExprPtr value; };
// break; / continue; -> يُترجمان مباشرة إلى break;/continue; في C المولَّد (تطابق دلالي كامل).
struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};

// @container[=name] <body> .end/container   (أو .end;)
// @container.data[=name] <body> .end/container.data   (أو .end/data ، أو .end;)
// دعم حقيقي لنوعين فقط من "لغة الحاويات" (PLAIN و DATA) بنفس قواعد نحو المفسّر الأصلي
// تماماً (readTagKeyword/readOptionalName/consumeEndTag في rin_parser.cpp) — بلا أقواس {}:
// الجسم يمتد حتى وسم إغلاق '.end/...' مطابق. كل ما هو أبعد من ذلك (pipe/api/import/table/
// doc/object/portal/block/sticker/aukt/Containers.Group/Volume/save/link/tying/merge) يبقى
// مرفوضاً بوضوح كما كان (انظر rejectUnsupported) لأنه غير قابل للترجمة لتنفيذي أصلي بمعنى واضح.
struct ContainerStmt : Stmt {
    std::string tag;   // "container" أو "container.data"
    std::string name;  // قد تكون فارغة
    std::vector<StmtPtr> body;
    bool isData = false;
};

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
            // مسموح فقط: '@container' و'@container.data'/'@data' (PLAIN/DATA) — يُعالَجان في
            // declaration() قبل الوصول هنا فعلياً (انظر containerBlockOrNull). أي '@...' آخر
            // (pipe/api/import/table/doc/object/portal/block/sticker/aukt/Containers.Group/Volume)
            // يبقى مرفوضاً بوضوح.
            bool isPlainContainer = checkNext(Tok::IDENT) && tokens[current + 1].lexeme == "container" &&
                                     !(current + 2 < tokens.size() && tokens[current + 2].type == Tok::DOT);
            bool isContainerDotData = checkNext(Tok::IDENT) && tokens[current + 1].lexeme == "container" &&
                                       current + 3 < tokens.size() && tokens[current + 2].type == Tok::DOT &&
                                       tokens[current + 3].lexeme == "data";
            bool isDataShort = checkNext(Tok::IDENT) && tokens[current + 1].lexeme == "data";
            if (isPlainContainer || isContainerDotData || isDataShort) return; // يُعالَج لاحقاً بواسطة containerBlock
            throw RincUnsupportedFeature(
                "الميزات المبنية على '@' (container.pipe/api/import/table/doc/object/portal/block/sticker/"
                "aukt، Containers.Group، Volume، @import ...) غير مدعومة في المترجم الأصلي (rinc) — "
                "المدعوم منها هنا فقط: '@container' و'@container.data'/'@data'.", peek().line);
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
                throw RincUnsupportedFeature(
                    "الكلمة '" + peek().lexeme + "' جزء من \"لغة الحاويات/البيانات\" (container/table/doc/"
                    "save/installation/link/tying/merge/...) وهي غير مدعومة في المترجم الأصلي (rinc).", peek().line);
            }
        }
    }

    StmtPtr declaration() {
        rejectUnsupported();
        if (check(Tok::AT)) return containerBlock();
        // 'plus.condition' كلمة مفتاحية مركّبة سياقية (شرط ثلاثي عام)، مطابقة لأسلوب فحصها في
        // rin_parser.cpp::declaration(): تُفحَص هنا فوق مستوى statement() بنفس المنطق، وننظر
        // 3 خطوات للأمام (IDENT("plus") ثم DOT ثم IDENT("condition")) قبل الاستهلاك، لأن 'plus'
        // ليست كلمة محجوزة (تبقى IDENT عادياً في أي سياق آخر، فيصح استخدامها كاسم متغير).
        if (check(Tok::IDENT) && peek().lexeme == "plus" &&
            checkNext(Tok::DOT) &&
            current + 2 < tokens.size() && tokens[current + 2].type == Tok::IDENT &&
            tokens[current + 2].lexeme == "condition") {
            advance(); // 'plus'
            advance(); // '.'
            advance(); // 'condition'
            return plusConditionStatement();
        }
        if (match({Tok::LET})) return letDeclaration();
        if (match({Tok::FUN})) return functionDeclaration();
        return statement();
    }

    // true فقط إن كان الموضع الحالي '.' متبوعاً مباشرة بـ IDENT بلفظ "end" (وسم إغلاق).
    bool checkContainerClosingTag() const {
        return check(Tok::DOT) && checkNext(Tok::IDENT) && tokens[current + 1].lexeme == "end";
    }

    // @container[=name] <body...> .end/container[=name]   أو  .end;
    // @container.data[=name] / @data[=name] <body...> .end/container.data (أو .end/data) [=name]  أو .end;
    // نفس قواعد المفسّر الأصلي بالضبط (readTagKeyword/readOptionalName/consumeEndTag في rin_parser.cpp)
    // لكن محصورة في تاغَين فقط (الباقي مرفوض في rejectUnsupported قبل الوصول هنا).
    StmtPtr containerBlock() {
        Token atTok = advance(); // '@'
        Token first = consume(Tok::IDENT, "Expected 'container' or 'data' after '@'");
        std::string tag;
        bool isData = false;
        if (first.lexeme == "data") {
            tag = "container.data"; isData = true;
        } else { // "container"
            if (check(Tok::DOT) && checkNext(Tok::IDENT) && tokens[current + 1].lexeme == "data") {
                advance(); advance(); // '.' 'data'
                tag = "container.data"; isData = true;
            } else {
                tag = "container"; isData = false;
            }
        }

        std::string name;
        if (match({Tok::EQUAL})) {
            if (check(Tok::IDENT) || check(Tok::STRING)) name = advance().lexeme;
            else throw RincError("Expected a name after '=' in container declaration", peek().line);
        }

        std::vector<StmtPtr> body;
        while (!checkContainerClosingTag() && !isAtEnd()) body.push_back(declaration());
        if (isAtEnd()) {
            throw RincError("'@" + tag + "' opened at line " + std::to_string(atTok.line) +
                             " was never closed with a matching '.end/" + tag + "'", atTok.line);
        }

        if (isData) validateDataContainerBody(body, atTok.line);
        else validateNoNestedFunctions(body, atTok.line); // انظر السبب في تعليق الدالة

        // --- استهلاك وسم الإغلاق ---
        consume(Tok::DOT, "Expected '.' to start closing tag");
        Token endWord = consume(Tok::IDENT, "Expected 'end' in closing tag");
        if (endWord.lexeme != "end") throw RincError("Expected 'end' in closing tag", endWord.line);
        if (!match({Tok::SEMICOLON})) { // ليست '.end;' المختصرة
            consume(Tok::SLASH, "Expected '/' after 'end' (or ';' for the short form '.end;')");
            Token closeFirst = consume(Tok::IDENT, "Expected closing tag name after '.end/'");
            std::string closeTag;
            if (closeFirst.lexeme == "data") closeTag = "container.data";
            else if (closeFirst.lexeme == "container") {
                if (check(Tok::DOT) && checkNext(Tok::IDENT) && tokens[current + 1].lexeme == "data") {
                    advance(); advance();
                    closeTag = "container.data";
                } else closeTag = "container";
            } else {
                throw RincError("unsupported closing tag '.end/" + closeFirst.lexeme + "'", closeFirst.line);
            }
            if (closeTag != tag) {
                throw RincError("closing tag '.end/" + closeTag + "' does not match opening '@" + tag +
                                 "' at line " + std::to_string(atTok.line), closeFirst.line);
            }
            std::string closeName;
            if (match({Tok::EQUAL})) {
                if (check(Tok::IDENT) || check(Tok::STRING)) closeName = advance().lexeme;
            }
            if (!closeName.empty() && closeName != name) {
                throw RincError("closing tag name '=" + closeName + "' does not match opening name" +
                                 (name.empty() ? " (opening has none)" : (" '=" + name + "'")), atTok.line);
            }
        }

        auto s = std::make_shared<ContainerStmt>();
        s->tag = tag; s->name = name; s->body = body; s->isData = isData; s->line = atTok.line;
        return s;
    }

    // مطابق لـ Parser::validateDataContainerBody في rin_parser.cpp: container.data يجب أن يبقى
    // بيانات نقية — بلا دوال وبلا حاويات متداخلة.
    void validateDataContainerBody(const std::vector<StmtPtr>& body, int openLine) {
        for (auto& st : body) {
            if (std::dynamic_pointer_cast<FunctionStmt>(st)) {
                throw RincError("functions ('fun') are not allowed inside 'container.data' "
                                 "(opened at line " + std::to_string(openLine) + ")", st->line);
            }
            if (std::dynamic_pointer_cast<ContainerStmt>(st)) {
                throw RincError("nested containers are not allowed inside 'container.data' "
                                 "(opened at line " + std::to_string(openLine) + ")", st->line);
            }
        }
    }

    // قيد خاص بـrinc فقط (لا يوجد في المفسّر الأصلي، حيث container العادية تسمح بـfun):
    // مولّد الكود هنا (collectFunctions) لا يبحث داخل أجسام ContainerStmt عن دوال متداخلة
    // لرفعها كدوال C مستقلة (نطاق هذه الإضافة محصور بيانات/منطق تسلسلي بسيط)، فبدلاً من توليد
    // سلوك خاطئ صامت (تجاهل الدالة كأنها لم تُكتب) نرفضه بوضوح هنا وقت التحليل.
    void validateNoNestedFunctions(const std::vector<StmtPtr>& body, int openLine) {
        for (auto& st : body) {
            if (std::dynamic_pointer_cast<FunctionStmt>(st)) {
                throw RincError("rinc: function declarations ('fun') nested inside '@container' are not "
                                 "supported yet (container opened at line " + std::to_string(openLine) +
                                 "). Declare the function at top level instead.", st->line);
            }
        }
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
        if (match({Tok::RINOPEN})) return rinopenStatement();
        if (match({Tok::FOR})) return forStatement();
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

    // rinopen (condition) { body } -- اسم بديل لـ while في المفسّر الأصلي (يُبنى فعلياً كـ
    // WhileStmt عادية عند التحليل هناك، انظر Parser::rinopenStatement في rin_parser.cpp)، بفارق
    // واحد فقط: الجسم هنا كتلة {} إلزامية دائماً (وليست statement() عام كما في while).
    StmtPtr rinopenStatement() {
        Token tok = previous(); // 'rinopen'
        consume(Tok::LPAREN, "Expected '(' after 'rinopen'");
        auto cond = expression();
        consume(Tok::RPAREN, "Expected ')' after rinopen condition");
        consume(Tok::LBRACE, "Expected '{' before rinopen body");
        loopDepth++;
        auto body = block();
        loopDepth--;
        auto s = std::make_shared<WhileStmt>(); s->condition = cond; s->body = body; s->line = tok.line;
        return s;
    }

    // for (initializer; condition; increment) body -- مطابقة لـ Parser::forStatement في
    // rin_parser.cpp: initializer إما فارغ (';' فقط) أو 'let x = ...;' أو عبارة تعبير (تستهلك
    // ';' بنفسها)؛ condition الغائب == true دائماً؛ increment اختياري.
    StmtPtr forStatement() {
        Token forTok = previous(); // 'for'
        consume(Tok::LPAREN, "Expected '(' after 'for'");

        StmtPtr initializer = nullptr;
        if (match({Tok::SEMICOLON})) {
            initializer = nullptr; // for (;;) -> لا مُهيّئ
        } else if (match({Tok::LET})) {
            initializer = letDeclaration(); // letDeclaration() يستهلك ';' بنفسه
        } else {
            initializer = expressionStatement(); // يستهلك ';' بنفسه أيضاً
        }

        ExprPtr condition = nullptr;
        if (!check(Tok::SEMICOLON)) condition = expression();
        consume(Tok::SEMICOLON, "Expected ';' after 'for' loop condition");

        ExprPtr increment = nullptr;
        if (!check(Tok::RPAREN)) increment = expression();
        consume(Tok::RPAREN, "Expected ')' after 'for' clauses");

        loopDepth++;
        auto body = statement();
        loopDepth--;

        auto s = std::make_shared<ForStmt>();
        s->initializer = initializer; s->condition = condition; s->increment = increment;
        s->body = body; s->line = forTok.line;
        return s;
    }

    // plus.condition (condition) { trueBranch } / { falseBranch } -- مطابقة لـ
    // Parser::plusConditionStatement في rin_parser.cpp: كلتا الكتلتين إلزاميتان.
    StmtPtr plusConditionStatement() {
        Token tok = previous(); // آخر توكن مُستهلَك ('condition')
        consume(Tok::LPAREN, "Expected '(' after 'plus.condition'");
        auto cond = expression();
        consume(Tok::RPAREN, "Expected ')' after 'plus.condition' condition");

        consume(Tok::LBRACE, "Expected '{' to start 'plus.condition' true-branch");
        auto trueBranch = block(); // يستهلك '}' المطابقة بنفسه

        consume(Tok::SLASH, "Expected '/' between 'plus.condition' true-branch and false-branch");

        consume(Tok::LBRACE, "Expected '{' to start 'plus.condition' false-branch");
        auto falseBranch = block();

        auto s = std::make_shared<PlusConditionStmt>();
        s->condition = cond; s->trueBranch = trueBranch; s->falseBranch = falseBranch; s->line = tok.line;
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

// دالة حرة (لا عضو في CodeGen) لتفادي القيد النحوي لـ raw string literals عند تضمين نص Rin
// الأصلي كاملاً داخل ملف C++ مولَّد (وضع "تضمين المفسّر" أدناه) — يستخدمها أيضاً
// CodeGen::cLiteral الداخلية لتفادي تكرار منطق الهروب (escaping) مرتين.
static std::string cLiteralEscape(const std::string& s) {
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
            } else if (auto f = std::dynamic_pointer_cast<ForStmt>(s)) {
                std::vector<StmtPtr> v; if (f->initializer) v.push_back(f->initializer);
                v.push_back(f->body); collectFunctions(v);
            } else if (auto pc = std::dynamic_pointer_cast<PlusConditionStmt>(s)) {
                std::vector<StmtPtr> v{pc->trueBranch, pc->falseBranch}; collectFunctions(v);
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
        if (auto s = std::dynamic_pointer_cast<ForStmt>(stmt)) {
            // for (init; cond; incr) body -> يُترجَم مباشرة لـ for C99 حقيقي: نطاق المُهيّئ في C
            // للـ for-clause يطابق تماماً نطاق forEnv في المفسّر (initializer مرئي في condition/
            // increment/body فقط)، فلا حاجة لأي تغليف {} إضافي هنا.
            pad(); out << "for (";
            if (auto letInit = std::dynamic_pointer_cast<LetStmt>(s->initializer)) {
                out << "Value " << cname(letInit->name) << " = "
                    << (letInit->initializer ? exprStr(letInit->initializer, sc) : "rt_nil()");
            } else if (auto exprInit = std::dynamic_pointer_cast<ExpressionStmt>(s->initializer)) {
                out << exprStr(exprInit->expr, sc);
            }
            out << "; " << (s->condition ? ("rt_truthy(" + exprStr(s->condition, sc) + ")") : "1");
            out << "; " << (s->increment ? exprStr(s->increment, sc) : "") << ") ";
            emitBranch(s->body, sc);
            return;
        }
        if (auto s = std::dynamic_pointer_cast<PlusConditionStmt>(stmt)) {
            // شرط ثلاثي عام على مستوى العبارات: كلتا الكتلتين إلزاميتان دائماً (بخلاف if/else).
            pad(); out << "if (rt_truthy(" << exprStr(s->condition, sc) << ")) ";
            emitBlockBody(s->trueBranch, sc);
            pad(); out << "else ";
            emitBlockBody(s->falseBranch, sc);
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
        if (auto s = std::dynamic_pointer_cast<ContainerStmt>(stmt)) {
            // نفس تنسيق نص المفسّر الأصلي بالضبط (containerTagName/containerIcon +
            // "✅ .end/tag" في rin_interpreter.cpp) حتى يتطابق ناتج rinc مع ناتج المفسّر حرفياً.
            std::string icon = s->isData ? "🗂️" : "📦";
            std::string openLine = icon + " " + s->tag + (s->name.empty() ? "" : (" = " + s->name));
            pad(); out << "printf(" << cLiteral(openLine + "\n") << ");\n";
            pad(); out << "{\n"; indent++;
            for (auto& st : s->body) emitStmt(st, sc);
            indent--; pad(); out << "}\n";
            std::string closeLine = std::string("\xE2\x9C\x85 .end/") + s->tag +
                                     (s->name.empty() ? "" : (" (" + s->name + ")"));
            pad(); out << "printf(" << cLiteral(closeLine + "\n") << ");\n";
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
            "writeFile","readFile","appendFile","fileExists","deleteFile",
            "maxOf","minOf","median","mode","stddev","variance","scale",
            "normalize","shift","isBool","chr","ord","jsonEncode","jsonDecode",
            // Color Engine (rin_color.h port — see the RUNTIME_HEADER block above titled
            // "Rin Color Engine — C port for the native (rinc) compiler"). "str" is a plain
            // alias for toString(), same as in rin_interpreter.cpp's natives[] — both were
            // undefined-function bugs before this (lib/colors.og.rin and others already called
            // str()/toHex() as if they existed).
            "str","toHex","colorValid","colorParse","colorRgb","colorRgba","colorHsl","colorHsla",
            "colorComponents","colorToHsl","colorToRgbaString","colorToHslaString","colorMix",
            "colorLighten","colorDarken","colorWithAlpha","colorBlend","colorInvert",
            "colorGrayscale","colorLuminance","colorContrast","colorIsLight","colorTextOn",
            // Aggregation/transformation extras (mirrors sum/mean/maxOf/median/mode/stddev/
            // variance/scale/normalize/shift already above).
            "clamp","count","product","range","geometricMean","harmonicMean","rms","percentile",
            "iqr","weightedMean","zscore","cumulativeSum","movingAverage",
            // UTF-8 codepoint helpers + Arabic/NLP text processing.
            "utf8Len","utf8CharAt","utf8Substr","utf8Reverse","utf8ToArray",
            "arabicStripDiacritics","arabicNormalize","detectScript","levenshtein",
            "tokenizeWords","splitSentences",
            // Regex (POSIX ERE via <regex.h> — see rc_compile_regex()'s doc comment above for
            // the documented dialect gap vs. the interpreter's ECMAScript std::regex).
            "regexTest","regexFind","regexFindAll","regexGroups","regexReplace","regexSplit"
        };
        if (natives.count(c->callee)) {
            return "rt_native_" + c->callee + "(" + argsArr + ", " + std::to_string(n) + ")";
        }
        if (kInterpreterOnlyNatives.count(c->callee)) {
            throw RincUnsupportedFeature(
                "الدالة '" + c->callee + "' موجودة في مفسّر Rin لكنها مرتبطة بمحرّك "
                "NoSQL/HTTP/الدردشة/إلخ، وغير مدعومة مباشرةً في المترجم الأصلي (rinc).",
                c->line);
        }
        throw RincError("دالة غير معرَّفة: '" + c->callee + "'", c->line);
    }

    static std::string cLiteral(const std::string& s) { return cLiteralEscape(s); }

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
#include <setjmp.h>
#include <regex.h>

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

// ---- إحصائيات/مصفوفات رقمية إضافية (maxOf/minOf/median/mode/stddev/variance/scale/normalize/shift) ----
// مطابقة لـ natives["maxOf"|"minOf"|...] في rin_interpreter.cpp: تعمل على مصفوفة أرقام واحدة.
static Value rt_native_maxOf(Value* a, int n) {
    (void)n; double best = a[0].arr->items[0].num;
    for (int i = 1; i < a[0].arr->len; i++) if (a[0].arr->items[i].num > best) best = a[0].arr->items[i].num;
    return rt_num(best);
}
static Value rt_native_minOf(Value* a, int n) {
    (void)n; double best = a[0].arr->items[0].num;
    for (int i = 1; i < a[0].arr->len; i++) if (a[0].arr->items[i].num < best) best = a[0].arr->items[i].num;
    return rt_num(best);
}
static int rt_cmp_num_qsort(const void* pa, const void* pb) {
    double a = *(const double*)pa, b = *(const double*)pb;
    return (a > b) - (a < b);
}
static Value rt_native_median(Value* a, int n) {
    (void)n; int len = a[0].arr->len;
    double* nums = (double*)malloc(sizeof(double) * len);
    for (int i = 0; i < len; i++) nums[i] = a[0].arr->items[i].num;
    qsort(nums, len, sizeof(double), rt_cmp_num_qsort);
    double result = (len % 2 == 1) ? nums[len / 2] : (nums[len / 2 - 1] + nums[len / 2]) / 2.0;
    free(nums);
    return rt_num(result);
}
static Value rt_native_mode(Value* a, int n) {
    (void)n; RArray* arr = a[0].arr;
    double best = arr->items[0].num; int bestCount = 0;
    for (int i = 0; i < arr->len; i++) {
        double candidate = arr->items[i].num; int count = 0;
        for (int j = 0; j < arr->len; j++) if (arr->items[j].num == candidate) count++;
        if (count > bestCount) { bestCount = count; best = candidate; }
    }
    return rt_num(best);
}
static Value rt_native_stddev(Value* a, int n) {
    (void)n; RArray* arr = a[0].arr; int len = arr->len;
    double m = 0; for (int i = 0; i < len; i++) m += arr->items[i].num; m /= (double)len;
    double sq = 0; for (int i = 0; i < len; i++) { double d = arr->items[i].num - m; sq += d * d; }
    return rt_num(sqrt(sq / (double)len));
}
static Value rt_native_variance(Value* a, int n) {
    (void)n; RArray* arr = a[0].arr; int len = arr->len;
    double m = 0; for (int i = 0; i < len; i++) m += arr->items[i].num; m /= (double)len;
    double sq = 0; for (int i = 0; i < len; i++) { double d = arr->items[i].num - m; sq += d * d; }
    return rt_num(sq / (double)len);
}
static Value rt_native_scale(Value* a, int n) {
    (void)n; RArray* arr = a[0].arr; double factor = a[1].num;
    RArray* out = rt_arr_alloc(arr->len);
    for (int i = 0; i < arr->len; i++) rt_arr_push(out, rt_num(arr->items[i].num * factor));
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_normalize(Value* a, int n) {
    (void)n; RArray* arr = a[0].arr;
    double lo = arr->items[0].num, hi = arr->items[0].num;
    for (int i = 1; i < arr->len; i++) { if (arr->items[i].num < lo) lo = arr->items[i].num; if (arr->items[i].num > hi) hi = arr->items[i].num; }
    RArray* out = rt_arr_alloc(arr->len);
    for (int i = 0; i < arr->len; i++) {
        double normalized = (hi == lo) ? 0.0 : (arr->items[i].num - lo) / (hi - lo);
        rt_arr_push(out, rt_num(normalized));
    }
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_shift(Value* a, int n) {
    (void)n; RArray* arr = a[0].arr; double delta = a[1].num;
    RArray* out = rt_arr_alloc(arr->len);
    for (int i = 0; i < arr->len; i++) rt_arr_push(out, rt_num(arr->items[i].num + delta));
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_isBool(Value* a, int n) { (void)n; return rt_bool(a[0].t == RT_BOOL); }
static Value rt_native_chr(Value* a, int n) {
    (void)n; long i = (long)a[0].num;
    if (i < 0 || i > 255) rt_fatal(0, "'chr': القيمة يجب أن تكون بين 0 و255");
    char buf[2] = { (char)(unsigned char)i, 0 }; return rt_str(buf);
}
static Value rt_native_ord(Value* a, int n) {
    (void)n; if (a[0].str[0] == 0) rt_fatal(0, "'ord': النص فارغ");
    return rt_num((double)(unsigned char)a[0].str[0]);
}

// ---- محوّل JSON حقيقي في الاتجاهين (Value <-> نص JSON)، مطابق لـ rin_json.h ----
typedef struct { char* buf; size_t len, cap; } RtStrBuf;
static void rt_sb_init(RtStrBuf* sb) { sb->cap = 64; sb->len = 0; sb->buf = (char*)malloc(sb->cap); sb->buf[0] = 0; }
static void rt_sb_append_n(RtStrBuf* sb, const char* s, size_t l) {
    while (sb->len + l + 1 > sb->cap) { sb->cap *= 2; sb->buf = (char*)realloc(sb->buf, sb->cap); }
    memcpy(sb->buf + sb->len, s, l); sb->len += l; sb->buf[sb->len] = 0;
}
static void rt_sb_append(RtStrBuf* sb, const char* s) { rt_sb_append_n(sb, s, strlen(s)); }
static void rt_sb_append_char(RtStrBuf* sb, char c) { rt_sb_append_n(sb, &c, 1); }

static void rt_json_encode_escaped(RtStrBuf* sb, const char* s) {
    rt_sb_append_char(sb, '"');
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
            case '"': rt_sb_append(sb, "\\\""); break;
            case '\\': rt_sb_append(sb, "\\\\"); break;
            case '\n': rt_sb_append(sb, "\\n"); break;
            case '\r': rt_sb_append(sb, "\\r"); break;
            case '\t': rt_sb_append(sb, "\\t"); break;
            default:
                if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); rt_sb_append(sb, buf); }
                else rt_sb_append_char(sb, (char)c);
        }
    }
    rt_sb_append_char(sb, '"');
}
static void rt_json_encode_value(RtStrBuf* sb, Value v) {
    switch (v.t) {
        case RT_NIL: rt_sb_append(sb, "null"); break;
        case RT_BOOL: rt_sb_append(sb, v.num != 0 ? "true" : "false"); break;
        case RT_NUM: { char* s = rt_num_to_str(v.num); rt_sb_append(sb, s); free(s); break; }
        case RT_STR: rt_json_encode_escaped(sb, v.str); break;
        case RT_ARR: {
            rt_sb_append_char(sb, '[');
            for (int i = 0; i < v.arr->len; i++) { if (i) rt_sb_append_char(sb, ','); rt_json_encode_value(sb, v.arr->items[i]); }
            rt_sb_append_char(sb, ']');
            break;
        }
        case RT_MAP: {
            rt_sb_append_char(sb, '{');
            for (int i = 0; i < v.map->len; i++) {
                if (i) rt_sb_append_char(sb, ',');
                char* k = rt_to_display(v.map->keys[i]); rt_json_encode_escaped(sb, k); free(k);
                rt_sb_append_char(sb, ':');
                rt_json_encode_value(sb, v.map->vals[i]);
            }
            rt_sb_append_char(sb, '}');
            break;
        }
    }
}
static Value rt_native_jsonEncode(Value* a, int n) {
    (void)n; RtStrBuf sb; rt_sb_init(&sb); rt_json_encode_value(&sb, a[0]); return rt_str_own(sb.buf);
}

typedef struct { const char* src; size_t pos, len; jmp_buf err; } RtJsonDecoder;
static void rt_json_skip_ws(RtJsonDecoder* d) { while (d->pos < d->len && (unsigned char)d->src[d->pos] <= ' ') d->pos++; }
static void rt_json_expect(RtJsonDecoder* d, char c) { if (d->pos >= d->len || d->src[d->pos] != c) longjmp(d->err, 1); d->pos++; }
static void rt_json_expect_lit(RtJsonDecoder* d, const char* lit) {
    size_t n = strlen(lit);
    if (d->pos + n > d->len || strncmp(d->src + d->pos, lit, n) != 0) longjmp(d->err, 1);
    d->pos += n;
}
static void rt_json_parse_string_raw(RtJsonDecoder* d, RtStrBuf* out) {
    rt_json_expect(d, '"');
    for (;;) {
        if (d->pos >= d->len) longjmp(d->err, 1);
        char c = d->src[d->pos++];
        if (c == '"') break;
        if (c == '\\') {
            if (d->pos >= d->len) longjmp(d->err, 1);
            char e = d->src[d->pos++];
            switch (e) {
                case '"': rt_sb_append_char(out, '"'); break;
                case '\\': rt_sb_append_char(out, '\\'); break;
                case '/': rt_sb_append_char(out, '/'); break;
                case 'n': rt_sb_append_char(out, '\n'); break;
                case 't': rt_sb_append_char(out, '\t'); break;
                case 'r': rt_sb_append_char(out, '\r'); break;
                case 'b': rt_sb_append_char(out, '\b'); break;
                case 'f': rt_sb_append_char(out, '\f'); break;
                case 'u': {
                    if (d->pos + 4 > d->len) longjmp(d->err, 1);
                    char hex[5]; memcpy(hex, d->src + d->pos, 4); hex[4] = 0; d->pos += 4;
                    unsigned code = (unsigned)strtoul(hex, NULL, 16);
                    if (code < 0x80) rt_sb_append_char(out, (char)code);
                    else if (code < 0x800) {
                        rt_sb_append_char(out, (char)(0xC0 | (code >> 6)));
                        rt_sb_append_char(out, (char)(0x80 | (code & 0x3F)));
                    } else {
                        rt_sb_append_char(out, (char)(0xE0 | (code >> 12)));
                        rt_sb_append_char(out, (char)(0x80 | ((code >> 6) & 0x3F)));
                        rt_sb_append_char(out, (char)(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: longjmp(d->err, 1);
            }
        } else rt_sb_append_char(out, c);
    }
}
static Value rt_json_parse_value(RtJsonDecoder* d);
static Value rt_json_parse_number(RtJsonDecoder* d) {
    size_t start = d->pos;
    if (d->pos < d->len && (d->src[d->pos] == '-' || d->src[d->pos] == '+')) d->pos++;
    while (d->pos < d->len && (isdigit((unsigned char)d->src[d->pos]) || d->src[d->pos] == '.' ||
           d->src[d->pos] == 'e' || d->src[d->pos] == 'E' || d->src[d->pos] == '-' || d->src[d->pos] == '+')) d->pos++;
    if (d->pos == start) longjmp(d->err, 1);
    char tmp[64]; size_t l = d->pos - start; if (l >= sizeof tmp) longjmp(d->err, 1);
    memcpy(tmp, d->src + start, l); tmp[l] = 0;
    char* end; double val = strtod(tmp, &end);
    if (end == tmp) longjmp(d->err, 1);
    return rt_num(val);
}
static Value rt_json_parse_array(RtJsonDecoder* d) {
    rt_json_expect(d, '[');
    RArray* arr = rt_arr_alloc(4);
    rt_json_skip_ws(d);
    if (d->pos < d->len && d->src[d->pos] == ']') { d->pos++; Value v = rt_nil(); v.t = RT_ARR; v.arr = arr; return v; }
    for (;;) {
        rt_arr_push(arr, rt_json_parse_value(d));
        rt_json_skip_ws(d);
        if (d->pos < d->len && d->src[d->pos] == ',') { d->pos++; rt_json_skip_ws(d); continue; }
        break;
    }
    rt_json_skip_ws(d); rt_json_expect(d, ']');
    Value v = rt_nil(); v.t = RT_ARR; v.arr = arr; return v;
}
static Value rt_json_parse_object(RtJsonDecoder* d) {
    rt_json_expect(d, '{');
    RMap* m = rt_map_alloc(4);
    rt_json_skip_ws(d);
    if (d->pos < d->len && d->src[d->pos] == '}') { d->pos++; Value v = rt_nil(); v.t = RT_MAP; v.map = m; return v; }
    for (;;) {
        rt_json_skip_ws(d);
        RtStrBuf keyBuf; rt_sb_init(&keyBuf); rt_json_parse_string_raw(d, &keyBuf);
        Value key = rt_str_own(keyBuf.buf);
        rt_json_skip_ws(d); rt_json_expect(d, ':');
        Value val = rt_json_parse_value(d);
        rt_map_set(m, key, val);
        rt_json_skip_ws(d);
        if (d->pos < d->len && d->src[d->pos] == ',') { d->pos++; continue; }
        break;
    }
    rt_json_skip_ws(d); rt_json_expect(d, '}');
    Value v = rt_nil(); v.t = RT_MAP; v.map = m; return v;
}
static Value rt_json_parse_value(RtJsonDecoder* d) {
    rt_json_skip_ws(d);
    if (d->pos >= d->len) longjmp(d->err, 1);
    char c = d->src[d->pos];
    if (c == '{') return rt_json_parse_object(d);
    if (c == '[') return rt_json_parse_array(d);
    if (c == '"') { RtStrBuf sb; rt_sb_init(&sb); rt_json_parse_string_raw(d, &sb); return rt_str_own(sb.buf); }
    if (c == 't') { rt_json_expect_lit(d, "true"); return rt_bool(1); }
    if (c == 'f') { rt_json_expect_lit(d, "false"); return rt_bool(0); }
    if (c == 'n') { rt_json_expect_lit(d, "null"); return rt_nil(); }
    return rt_json_parse_number(d);
}
// decodeOrRaw: يحاول تحليل a[0] كـ JSON صالح، وعند أي فشل تركيبي يُعيد النص الخام كما هو (Value
// نصية)، تماماً كسلوك rin::json::decodeOrRaw في المفسّر الأصلي — لا يرمي خطأً أبداً.
static Value rt_native_jsonDecode(Value* a, int n) {
    (void)n;
    RtJsonDecoder d; d.src = a[0].str; d.pos = 0; d.len = strlen(a[0].str);
    if (setjmp(d.err) == 0) {
        Value result = rt_json_parse_value(&d);
        rt_json_skip_ws(&d);
        if (d.pos == d.len) return result;
    }
    return rt_str(a[0].str);
}


// ---- Rin Color Engine — C port for the native (rinc) compiler ------------------------------
// A from-scratch reimplementation, in portable C, of the SAME algorithms as
// app/src/main/cpp/rin_color.h (the shared engine used by the interpreter's natives and by
// Loom's renderer) — this compiler emits standalone C and cannot #include that C++ header, so
// this is a parallel port kept in lockstep with it by hand. See rin_color.h's own doc comment
// for the full literal syntax rc_try_parse() below accepts: #rgb/#rgba/#rrggbb/#rrggbbaa,
// rgb()/rgba(), hsl()/hsla(), the 148 named CSS colors, "transparent".
typedef struct { unsigned char r, g, b, a; } RcColor;

static unsigned char rc_clampb(int v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
static double rc_clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static unsigned char rc_alpha_from_unit(double a) { return (unsigned char)(rc_clamp01(a) * 255.0 + 0.5); }
static double rc_unit_from_alpha(unsigned char a) { return a / 255.0; }

static int rc_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
static int rc_is_hexdigit(char c) { return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }

static void rc_trim_lower(const char* in, char* out, int outCap) {
    int a = 0, b = (int)strlen(in);
    while (a < b && isspace((unsigned char)in[a])) a++;
    while (b > a && isspace((unsigned char)in[b-1])) b--;
    int len = b - a; if (len >= outCap) len = outCap - 1;
    for (int i = 0; i < len; i++) out[i] = (char)tolower((unsigned char)in[a+i]);
    out[len] = 0;
}
static void rc_trim(const char* in, char* out, int outCap) {
    int a = 0, b = (int)strlen(in);
    while (a < b && isspace((unsigned char)in[a])) a++;
    while (b > a && isspace((unsigned char)in[b-1])) b--;
    int len = b - a; if (len >= outCap) len = outCap - 1;
    memcpy(out, in + a, len); out[len] = 0;
}

typedef struct { const char* name; unsigned char r, g, b, a; } RcNamed;
static const RcNamed RC_NAMED[] = {
{"transparent",0,0,0,0},
  {"black",0,0,0,255},
  {"white",255,255,255,255},
  {"red",255,0,0,255},
  {"green",0,128,0,255},
  {"blue",0,0,255,255},
  {"yellow",255,255,0,255},
  {"cyan",0,255,255,255},
  {"magenta",255,0,255,255},
  {"gray",128,128,128,255},
  {"grey",128,128,128,255},
  {"silver",192,192,192,255},
  {"maroon",128,0,0,255},
  {"olive",128,128,0,255},
  {"lime",0,255,0,255},
  {"aqua",0,255,255,255},
  {"teal",0,128,128,255},
  {"navy",0,0,128,255},
  {"fuchsia",255,0,255,255},
  {"purple",128,0,128,255},
  {"orange",255,165,0,255},
  {"pink",255,192,203,255},
  {"brown",165,42,42,255},
  {"gold",255,215,0,255},
  {"indigo",75,0,130,255},
  {"violet",238,130,238,255},
  {"turquoise",64,224,208,255},
  {"coral",255,127,80,255},
  {"salmon",250,128,114,255},
  {"khaki",240,230,140,255},
  {"orchid",218,112,214,255},
  {"plum",221,160,221,255},
  {"tan",210,180,140,255},
  {"beige",245,245,220,255},
  {"ivory",255,255,240,255},
  {"lavender",230,230,250,255},
  {"crimson",220,20,60,255},
  {"chocolate",210,105,30,255},
  {"tomato",255,99,71,255},
  {"orangered",255,69,0,255},
  {"hotpink",255,105,180,255},
  {"deeppink",255,20,147,255},
  {"skyblue",135,206,235,255},
  {"steelblue",70,130,180,255},
  {"royalblue",65,105,225,255},
  {"slateblue",106,90,205,255},
  {"dodgerblue",30,144,255,255},
  {"cornflowerblue",100,149,237,255},
  {"seagreen",46,139,87,255},
  {"forestgreen",34,139,34,255},
  {"limegreen",50,205,50,255},
  {"darkgreen",0,100,0,255},
  {"darkred",139,0,0,255},
  {"darkblue",0,0,139,255},
  {"darkorange",255,140,0,255},
  {"darkviolet",148,0,211,255},
  {"darkslategray",47,79,79,255},
  {"darkslategrey",47,79,79,255},
  {"darkgray",169,169,169,255},
  {"darkgrey",169,169,169,255},
  {"lightgray",211,211,211,255},
  {"lightgrey",211,211,211,255},
  {"lightblue",173,216,230,255},
  {"lightgreen",144,238,144,255},
  {"lightyellow",255,255,224,255},
  {"lightpink",255,182,193,255},
  {"lightcoral",240,128,128,255},
  {"lightsalmon",255,160,122,255},
  {"lightseagreen",32,178,170,255},
  {"lightskyblue",135,206,250,255},
  {"lightslategray",119,136,153,255},
  {"lightslategrey",119,136,153,255},
  {"mintcream",245,255,250,255},
  {"honeydew",240,255,240,255},
  {"aliceblue",240,248,255,255},
  {"azure",240,255,255,255},
  {"snow",255,250,250,255},
  {"linen",250,240,230,255},
  {"whitesmoke",245,245,245,255},
  {"gainsboro",220,220,220,255},
  {"seashell",255,245,238,255},
  {"wheat",245,222,179,255},
  {"peachpuff",255,218,185,255},
  {"navajowhite",255,222,173,255},
  {"moccasin",255,228,181,255},
  {"cornsilk",255,248,220,255},
  {"lemonchiffon",255,250,205,255},
  {"papayawhip",255,239,213,255},
  {"blanchedalmond",255,235,205,255},
  {"bisque",255,228,196,255},
  {"antiquewhite",250,235,215,255},
  {"mistyrose",255,228,225,255},
  {"thistle",216,191,216,255},
  {"mediumpurple",147,112,219,255},
  {"mediumorchid",186,85,211,255},
  {"mediumvioletred",199,21,133,255},
  {"mediumseagreen",60,179,113,255},
  {"mediumspringgreen",0,250,154,255},
  {"mediumturquoise",72,209,204,255},
  {"mediumblue",0,0,205,255},
  {"mediumslateblue",123,104,238,255},
  {"mediumaquamarine",102,205,170,255},
  {"aquamarine",127,255,212,255},
  {"springgreen",0,255,127,255},
  {"chartreuse",127,255,0,255},
  {"yellowgreen",154,205,50,255},
  {"olivedrab",107,142,35,255},
  {"darkolivegreen",85,107,47,255},
  {"darkkhaki",189,183,107,255},
  {"palegoldenrod",238,232,170,255},
  {"goldenrod",218,165,32,255},
  {"darkgoldenrod",184,134,11,255},
  {"peru",205,133,63,255},
  {"sienna",160,82,45,255},
  {"saddlebrown",139,69,19,255},
  {"firebrick",178,34,34,255},
  {"indianred",205,92,92,255},
  {"rosybrown",188,143,143,255},
  {"palevioletred",219,112,147,255},
  {"deepskyblue",0,191,255,255},
  {"cadetblue",95,158,160,255},
  {"powderblue",176,224,230,255},
  {"paleturquoise",175,238,238,255},
  {"darkcyan",0,139,139,255},
  {"darkturquoise",0,206,209,255},
  {"lightcyan",224,255,255,255},
  {"lightsteelblue",176,196,222,255},
  {"midnightblue",25,25,112,255},
  {"darkslateblue",72,61,139,255},
  {"blueviolet",138,43,226,255},
  {"darkmagenta",139,0,139,255},
  {"darkorchid",153,50,204,255},
  {"darksalmon",233,150,122,255},
  {"darkseagreen",143,188,143,255},
  {"palegreen",152,251,152,255},
  {"greenyellow",173,255,47,255},
  {"lawngreen",124,252,0,255},
  {"lightgoldenrodyellow",250,250,210,255},
  {"lightskygray",135,206,235,255},
  {"slategray",112,128,144,255},
  {"slategrey",112,128,144,255},
  {"dimgray",105,105,105,255},
  {"dimgrey",105,105,105,255},
  {"lightsalmonpink",255,160,122,255},
  {"lightgoldenrod",238,221,130,255},
  {"ghostwhite",248,248,255,255},
  {"floralwhite",255,250,240,255},
  {"oldlace",253,245,230,255},
  {"lavenderblush",255,240,245,255},
  {"lightgrayish",220,220,220,255},
  {"rebeccapurple",102,51,153,255},
};
static const int RC_NAMED_COUNT = sizeof(RC_NAMED) / sizeof(RC_NAMED[0]);

static int rc_named_lookup(const char* lowname, RcColor* out) {
    for (int i = 0; i < RC_NAMED_COUNT; i++) {
        if (strcmp(RC_NAMED[i].name, lowname) == 0) {
            out->r = RC_NAMED[i].r; out->g = RC_NAMED[i].g; out->b = RC_NAMED[i].b; out->a = RC_NAMED[i].a;
            return 1;
        }
    }
    return 0;
}

// Splits on comma/slash/whitespace into up to maxTok tokens of up to 31 chars each.
static int rc_split_args(const char* s, char toks[][32], int maxTok) {
    int count = 0, ti = 0; toks[0][0] = 0;
    for (const char* p = s; ; p++) {
        char c = *p;
        if (c == ',' || c == '/' || isspace((unsigned char)c) || c == 0) {
            if (ti > 0) { toks[count][ti] = 0; count++; if (count >= maxTok) return count; ti = 0; toks[count][0] = 0; }
            if (c == 0) break;
        } else if (ti < 31) toks[count][ti++] = c;
    }
    return count;
}
static int rc_parse_channel255(const char* tok, unsigned char* out) {
    int len = (int)strlen(tok); if (len == 0) return 0;
    char* end;
    if (tok[len-1] == '%') {
        char buf[32]; int cl = len-1 < 31 ? len-1 : 31; memcpy(buf, tok, cl); buf[cl] = 0;
        double pct = strtod(buf, &end); if (end == buf) return 0;
        *out = rc_clampb((int)(pct / 100.0 * 255.0 + 0.5));
    } else {
        double v = strtod(tok, &end); if (end == tok) return 0;
        *out = rc_clampb((int)(v + 0.5));
    }
    return 1;
}
static int rc_parse_alpha_unit(const char* tok, double* out) {
    int len = (int)strlen(tok); if (len == 0) return 0;
    char* end;
    if (tok[len-1] == '%') {
        char buf[32]; int cl = len-1 < 31 ? len-1 : 31; memcpy(buf, tok, cl); buf[cl] = 0;
        double pct = strtod(buf, &end); if (end == buf) return 0;
        *out = rc_clamp01(pct / 100.0);
    } else {
        double v = strtod(tok, &end); if (end == tok) return 0;
        *out = rc_clamp01(v);
    }
    return 1;
}
static double rc_hue_to_rgb(double p, double q, double t) {
    if (t < 0) t += 1; if (t > 1) t -= 1;
    if (t < 1.0/6.0) return p + (q-p)*6.0*t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q-p)*(2.0/3.0-t)*6.0;
    return p;
}
static RcColor rc_hsl_to_rgb(double h, double s, double l, unsigned char a) {
    h = fmod(fmod(h, 360.0) + 360.0, 360.0) / 360.0;
    s = rc_clamp01(s); l = rc_clamp01(l);
    double r, g, b;
    if (s < 1e-9) { r = g = b = l; }
    else {
        double q = (l < 0.5) ? l * (1+s) : l + s - l*s;
        double p = 2*l - q;
        r = rc_hue_to_rgb(p, q, h + 1.0/3.0);
        g = rc_hue_to_rgb(p, q, h);
        b = rc_hue_to_rgb(p, q, h - 1.0/3.0);
    }
    RcColor out; out.r = rc_clampb((int)(r*255+0.5)); out.g = rc_clampb((int)(g*255+0.5)); out.b = rc_clampb((int)(b*255+0.5)); out.a = a;
    return out;
}
static void rc_rgb_to_hsl(RcColor c, double* h, double* s, double* l) {
    double r = c.r/255.0, g = c.g/255.0, b = c.b/255.0;
    double mx = r>g ? (r>b?r:b) : (g>b?g:b);
    double mn = r<g ? (r<b?r:b) : (g<b?g:b);
    double hh = 0, ss = 0, ll = (mx+mn)/2.0;
    double d = mx - mn;
    if (d > 1e-9) {
        ss = (ll > 0.5) ? d / (2.0-mx-mn) : d / (mx+mn);
        if (mx == r) hh = fmod((g-b)/d + (g<b?6.0:0.0), 6.0);
        else if (mx == g) hh = (b-r)/d + 2.0;
        else hh = (r-g)/d + 4.0;
        hh *= 60.0;
    }
    *h = hh; *s = ss; *l = ll;
}
static int rc_try_parse(const char* raw, RcColor* out) {
    char orig[256]; rc_trim(raw, orig, sizeof(orig));
    char low[256]; rc_trim_lower(raw, low, sizeof(low));
    int len = (int)strlen(orig);
    if (len == 0) return 0;

    if (orig[0] == '#') {
        for (int i = 1; i < len; i++) if (!rc_is_hexdigit(orig[i])) return 0;
        if (len == 4) { int r=rc_hexval(orig[1]),g=rc_hexval(orig[2]),b=rc_hexval(orig[3]);
            out->r=rc_clampb(r*16+r); out->g=rc_clampb(g*16+g); out->b=rc_clampb(b*16+b); out->a=255; return 1; }
        if (len == 5) { int r=rc_hexval(orig[1]),g=rc_hexval(orig[2]),b=rc_hexval(orig[3]),a=rc_hexval(orig[4]);
            out->r=rc_clampb(r*16+r); out->g=rc_clampb(g*16+g); out->b=rc_clampb(b*16+b); out->a=rc_clampb(a*16+a); return 1; }
        if (len == 7) { out->r=rc_clampb(rc_hexval(orig[1])*16+rc_hexval(orig[2])); out->g=rc_clampb(rc_hexval(orig[3])*16+rc_hexval(orig[4])); out->b=rc_clampb(rc_hexval(orig[5])*16+rc_hexval(orig[6])); out->a=255; return 1; }
        if (len == 9) { out->r=rc_clampb(rc_hexval(orig[1])*16+rc_hexval(orig[2])); out->g=rc_clampb(rc_hexval(orig[3])*16+rc_hexval(orig[4])); out->b=rc_clampb(rc_hexval(orig[5])*16+rc_hexval(orig[6])); out->a=rc_clampb(rc_hexval(orig[7])*16+rc_hexval(orig[8])); return 1; }
        return 0;
    }
    if (strncmp(low, "rgb(", 4) == 0 || strncmp(low, "rgba(", 5) == 0) {
        const char* open = strchr(orig, '('); const char* close = strrchr(orig, ')');
        if (!open || !close || close < open) return 0;
        char inner[200]; int ilen = (int)(close-open-1); if (ilen >= (int)sizeof(inner)) ilen = sizeof(inner)-1;
        memcpy(inner, open+1, ilen); inner[ilen] = 0;
        char toks[6][32]; int nt = rc_split_args(inner, toks, 6);
        if (nt < 3) return 0;
        unsigned char r, g, b;
        if (!rc_parse_channel255(toks[0],&r) || !rc_parse_channel255(toks[1],&g) || !rc_parse_channel255(toks[2],&b)) return 0;
        unsigned char a = 255;
        if (nt >= 4) { double au; if (!rc_parse_alpha_unit(toks[3],&au)) return 0; a = rc_alpha_from_unit(au); }
        out->r=r; out->g=g; out->b=b; out->a=a; return 1;
    }
    if (strncmp(low, "hsl(", 4) == 0 || strncmp(low, "hsla(", 5) == 0) {
        const char* open = strchr(orig, '('); const char* close = strrchr(orig, ')');
        if (!open || !close || close < open) return 0;
        char inner[200]; int ilen = (int)(close-open-1); if (ilen >= (int)sizeof(inner)) ilen = sizeof(inner)-1;
        memcpy(inner, open+1, ilen); inner[ilen] = 0;
        char toks[6][32]; int nt = rc_split_args(inner, toks, 6);
        if (nt < 3) return 0;
        char* end;
        double h = strtod(toks[0], &end); if (end == toks[0]) return 0;
        int l1 = (int)strlen(toks[1]); double sPct = (l1>0 && toks[1][l1-1]=='%') ? atof(toks[1]) : atof(toks[1])*100.0;
        int l2 = (int)strlen(toks[2]); double lPct = (l2>0 && toks[2][l2-1]=='%') ? atof(toks[2]) : atof(toks[2])*100.0;
        unsigned char a = 255;
        if (nt >= 4) { double au; if (!rc_parse_alpha_unit(toks[3],&au)) return 0; a = rc_alpha_from_unit(au); }
        *out = rc_hsl_to_rgb(h, sPct/100.0, lPct/100.0, a);
        return 1;
    }
    RcColor named;
    if (rc_named_lookup(low, &named)) { *out = named; return 1; }
    return 0;
}
static RcColor rc_parse(const char* raw, RcColor fallback) { RcColor c; return rc_try_parse(raw, &c) ? c : fallback; }

static const char RC_HEXDIGITS[] = "0123456789ABCDEF";
static void rc_hexbyte(unsigned char v, char* out2) { out2[0]=RC_HEXDIGITS[(v>>4)&0xF]; out2[1]=RC_HEXDIGITS[v&0xF]; }
static void rc_hex6(RcColor c, char* out7) { out7[0]='#'; rc_hexbyte(c.r,out7+1); rc_hexbyte(c.g,out7+3); rc_hexbyte(c.b,out7+5); out7[7]=0; }
static void rc_hex8(RcColor c, char* out9) { rc_hex6(c,out9); rc_hexbyte(c.a,out9+7); out9[9]=0; }
static void rc_hexauto(RcColor c, char* outBuf) { if (c.a == 255) rc_hex6(c, outBuf); else rc_hex8(c, outBuf); }

static RcColor rc_mix(RcColor a, RcColor b, double t) {
    t = rc_clamp01(t); RcColor out;
    out.r = rc_clampb((int)(a.r + (b.r-a.r)*t + 0.5));
    out.g = rc_clampb((int)(a.g + (b.g-a.g)*t + 0.5));
    out.b = rc_clampb((int)(a.b + (b.b-a.b)*t + 0.5));
    out.a = rc_clampb((int)(a.a + (b.a-a.a)*t + 0.5));
    return out;
}
static RcColor rc_lighten(RcColor c, double amt) { RcColor white = {255,255,255,c.a}; return rc_mix(c, white, rc_clamp01(amt)); }
static RcColor rc_darken(RcColor c, double amt) { RcColor black = {0,0,0,c.a}; return rc_mix(c, black, rc_clamp01(amt)); }
static RcColor rc_with_alpha(RcColor c, double a01) { c.a = rc_alpha_from_unit(a01); return c; }
static RcColor rc_grayscale(RcColor c) { unsigned char v = rc_clampb((int)(0.299*c.r+0.587*c.g+0.114*c.b+0.5)); RcColor o={v,v,v,c.a}; return o; }
static RcColor rc_invert(RcColor c) { RcColor o = {rc_clampb(255-c.r), rc_clampb(255-c.g), rc_clampb(255-c.b), c.a}; return o; }
static RcColor rc_blend(RcColor fg, RcColor bg) {
    double af = rc_unit_from_alpha(fg.a); RcColor o;
    o.r = rc_clampb((int)(fg.r*af + bg.r*(1.0-af) + 0.5));
    o.g = rc_clampb((int)(fg.g*af + bg.g*(1.0-af) + 0.5));
    o.b = rc_clampb((int)(fg.b*af + bg.b*(1.0-af) + 0.5));
    o.a = 255;
    return o;
}
static double rc_srgb_to_linear(double c) { c = rc_clamp01(c); return (c <= 0.03928) ? c/12.92 : pow((c+0.055)/1.055, 2.4); }
static double rc_luminance(RcColor c) { return 0.2126*rc_srgb_to_linear(c.r/255.0) + 0.7152*rc_srgb_to_linear(c.g/255.0) + 0.0722*rc_srgb_to_linear(c.b/255.0); }
static double rc_contrast(RcColor a, RcColor b) { double la = rc_luminance(a)+0.05, lb = rc_luminance(b)+0.05; return la>lb ? la/lb : lb/la; }
static int rc_is_light(RcColor c) { return rc_luminance(c) > 0.5; }
static RcColor rc_best_text(RcColor bg) {
    RcColor black = {0,0,0,255}, white = {255,255,255,255};
    return (rc_contrast(bg,white) >= rc_contrast(bg,black)) ? white : black;
}
static RcColor rc_arg_color(Value v) {
    if (v.t != RT_STR) rt_fatal(0, "دالة لون تتوقّع نصاً");
    RcColor c;
    if (!rc_try_parse(v.str, &c)) rt_fatal(0, "قيمة لون غير صالحة");
    return c;
}

// ---- rt_native_* wrappers exposed to generated Rin call sites (see emitCall's natives{} set) ----
static Value rt_native_str(Value* a, int n) { (void)n; return rt_str_own(rt_to_display(a[0])); }
static Value rt_native_toHex(Value* a, int n) { (void)n; char b[3]; rc_hexbyte(rc_clampb((int)(a[0].num+0.5)), b); b[2]=0; return rt_str(b); }
static Value rt_native_colorValid(Value* a, int n) { (void)n; RcColor c; return rt_bool(a[0].t==RT_STR && rc_try_parse(a[0].str,&c)); }
static Value rt_native_colorParse(Value* a, int n) { (void)n; char buf[10]; rc_hexauto(rc_arg_color(a[0]), buf); return rt_str(buf); }
static Value rt_native_colorRgb(Value* a, int n) { (void)n; RcColor c; c.r=rc_clampb((int)a[0].num); c.g=rc_clampb((int)a[1].num); c.b=rc_clampb((int)a[2].num); c.a=255; char buf[8]; rc_hex6(c,buf); return rt_str(buf); }
static Value rt_native_colorRgba(Value* a, int n) { (void)n; RcColor c; c.r=rc_clampb((int)a[0].num); c.g=rc_clampb((int)a[1].num); c.b=rc_clampb((int)a[2].num); c.a=rc_alpha_from_unit(a[3].num); char buf[10]; rc_hex8(c,buf); return rt_str(buf); }
static Value rt_native_colorHsl(Value* a, int n) { (void)n; RcColor c = rc_hsl_to_rgb(a[0].num, a[1].num/100.0, a[2].num/100.0, 255); char buf[8]; rc_hex6(c,buf); return rt_str(buf); }
static Value rt_native_colorHsla(Value* a, int n) { (void)n; RcColor c = rc_hsl_to_rgb(a[0].num, a[1].num/100.0, a[2].num/100.0, rc_alpha_from_unit(a[3].num)); char buf[10]; rc_hex8(c,buf); return rt_str(buf); }
static Value rt_native_colorComponents(Value* a, int n) {
    (void)n; RcColor c = rc_arg_color(a[0]);
    RMap* m = rt_map_alloc(4);
    rt_map_set(m, rt_str("r"), rt_num(c.r));
    rt_map_set(m, rt_str("g"), rt_num(c.g));
    rt_map_set(m, rt_str("b"), rt_num(c.b));
    rt_map_set(m, rt_str("alpha"), rt_num(rc_unit_from_alpha(c.a)));
    Value v = rt_nil(); v.t = RT_MAP; v.map = m; return v;
}
static Value rt_native_colorToHsl(Value* a, int n) {
    (void)n; RcColor c = rc_arg_color(a[0]); double h,s,l; rc_rgb_to_hsl(c,&h,&s,&l);
    RMap* m = rt_map_alloc(3);
    rt_map_set(m, rt_str("h"), rt_num(h));
    rt_map_set(m, rt_str("s"), rt_num(s*100.0));
    rt_map_set(m, rt_str("l"), rt_num(l*100.0));
    Value v = rt_nil(); v.t = RT_MAP; v.map = m; return v;
}
static Value rt_native_colorToRgbaString(Value* a, int n) {
    (void)n; RcColor c = rc_arg_color(a[0]);
    char buf[64]; snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.3f)", c.r, c.g, c.b, rc_unit_from_alpha(c.a));
    return rt_str(buf);
}
static Value rt_native_colorToHslaString(Value* a, int n) {
    (void)n; RcColor c = rc_arg_color(a[0]); double h,s,l; rc_rgb_to_hsl(c,&h,&s,&l);
    char buf[64]; snprintf(buf, sizeof(buf), "hsla(%.1f,%.1f%%,%.1f%%,%.3f)", h, s*100.0, l*100.0, rc_unit_from_alpha(c.a));
    return rt_str(buf);
}
static Value rt_native_colorMix(Value* a, int n) { (void)n; char buf[10]; rc_hexauto(rc_mix(rc_arg_color(a[0]), rc_arg_color(a[1]), a[2].num), buf); return rt_str(buf); }
static Value rt_native_colorLighten(Value* a, int n) { (void)n; char buf[10]; rc_hexauto(rc_lighten(rc_arg_color(a[0]), a[1].num), buf); return rt_str(buf); }
static Value rt_native_colorDarken(Value* a, int n) { (void)n; char buf[10]; rc_hexauto(rc_darken(rc_arg_color(a[0]), a[1].num), buf); return rt_str(buf); }
static Value rt_native_colorWithAlpha(Value* a, int n) { (void)n; char buf[10]; rc_hex8(rc_with_alpha(rc_arg_color(a[0]), a[1].num), buf); return rt_str(buf); }
static Value rt_native_colorBlend(Value* a, int n) { (void)n; char buf[8]; rc_hex6(rc_blend(rc_arg_color(a[0]), rc_arg_color(a[1])), buf); return rt_str(buf); }
static Value rt_native_colorInvert(Value* a, int n) { (void)n; char buf[10]; rc_hexauto(rc_invert(rc_arg_color(a[0])), buf); return rt_str(buf); }
static Value rt_native_colorGrayscale(Value* a, int n) { (void)n; char buf[10]; rc_hexauto(rc_grayscale(rc_arg_color(a[0])), buf); return rt_str(buf); }
static Value rt_native_colorLuminance(Value* a, int n) { (void)n; return rt_num(rc_luminance(rc_arg_color(a[0]))); }
static Value rt_native_colorContrast(Value* a, int n) { (void)n; return rt_num(rc_contrast(rc_arg_color(a[0]), rc_arg_color(a[1]))); }
static Value rt_native_colorIsLight(Value* a, int n) { (void)n; return rt_bool(rc_is_light(rc_arg_color(a[0]))); }
static Value rt_native_colorTextOn(Value* a, int n) { (void)n; char buf[8]; rc_hex6(rc_best_text(rc_arg_color(a[0])), buf); return rt_str(buf); }



// ---- Additional pure/portable natives — closing the gap with rin_interpreter.cpp's natives[] ----
// Ported from rin_interpreter.cpp by hand (same algorithms, same edge-case handling) since this
// compiler emits standalone C and can't share the C++ implementation directly. Everything below
// is a PURE function (no container/mask/network/filesystem-beyond-argument dependency), which is
// exactly the line rinc.cpp already draws elsewhere (see kInterpreterOnlyNatives's own comment).

// ---- Aggregation/transformation extras (mirrors sum/mean/maxOf/.../shift already above) ----
static Value rt_native_clamp(Value* a, int n) {
    (void)n; RArray* arr = a[0].arr; double lo = a[1].num, hi = a[2].num;
    RArray* out = rt_arr_alloc(arr->len);
    for (int i = 0; i < arr->len; i++) { double v = arr->items[i].num; if (v<lo) v=lo; if (v>hi) v=hi; rt_arr_push(out, rt_num(v)); }
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_count(Value* a, int n) { (void)n; return rt_num(a[0].arr->len); }
static Value rt_native_product(Value* a, int n) { (void)n; double p=1; for (int i=0;i<a[0].arr->len;i++) p*=a[0].arr->items[i].num; return rt_num(p); }
static Value rt_native_range(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr; double lo=arr->items[0].num, hi=arr->items[0].num;
    for (int i=1;i<arr->len;i++) { if (arr->items[i].num<lo) lo=arr->items[i].num; if (arr->items[i].num>hi) hi=arr->items[i].num; }
    return rt_num(hi-lo);
}
static Value rt_native_geometricMean(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr; double logSum=0;
    for (int i=0;i<arr->len;i++) { if (arr->items[i].num<=0) rt_fatal(0,"'geometricMean' expects strictly positive numbers"); logSum+=log(arr->items[i].num); }
    return rt_num(exp(logSum/(double)arr->len));
}
static Value rt_native_harmonicMean(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr; double invSum=0;
    for (int i=0;i<arr->len;i++) { if (arr->items[i].num==0) rt_fatal(0,"'harmonicMean' cannot divide by a zero element"); invSum += 1.0/arr->items[i].num; }
    return rt_num((double)arr->len/invSum);
}
static Value rt_native_rms(Value* a, int n) { (void)n; RArray* arr=a[0].arr; double sq=0; for (int i=0;i<arr->len;i++) sq+=arr->items[i].num*arr->items[i].num; return rt_num(sqrt(sq/(double)arr->len)); }
static double rc_percentile_of(double* sorted, int len, double p) {
    double rank = (p/100.0) * (double)(len-1);
    int lo = (int)floor(rank), hi = (int)ceil(rank);
    if (lo == hi) return sorted[lo];
    double frac = rank - (double)lo;
    return sorted[lo] + (sorted[hi]-sorted[lo])*frac;
}
static Value rt_native_percentile(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr; double p=a[1].num;
    if (p<0.0||p>100.0) rt_fatal(0,"'percentile' expects its second argument to be between 0 and 100");
    double* nums=(double*)malloc(sizeof(double)*arr->len);
    for (int i=0;i<arr->len;i++) nums[i]=arr->items[i].num;
    qsort(nums, arr->len, sizeof(double), rt_cmp_num_qsort);
    double r = rc_percentile_of(nums, arr->len, p);
    free(nums); return rt_num(r);
}
static Value rt_native_iqr(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr;
    double* nums=(double*)malloc(sizeof(double)*arr->len);
    for (int i=0;i<arr->len;i++) nums[i]=arr->items[i].num;
    qsort(nums, arr->len, sizeof(double), rt_cmp_num_qsort);
    double r = rc_percentile_of(nums, arr->len, 75.0) - rc_percentile_of(nums, arr->len, 25.0);
    free(nums); return rt_num(r);
}
static Value rt_native_weightedMean(Value* a, int n) {
    (void)n; RArray* nums=a[0].arr; RArray* weights=a[1].arr;
    if (nums->len != weights->len) rt_fatal(0,"'weightedMean' expects its two arrays to be the same length");
    double ws=0, wsum=0;
    for (int i=0;i<nums->len;i++) { ws += nums->items[i].num*weights->items[i].num; wsum += weights->items[i].num; }
    if (wsum==0) rt_fatal(0,"'weightedMean' cannot divide by a zero total weight");
    return rt_num(ws/wsum);
}
static Value rt_native_zscore(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr; int len=arr->len;
    double m=0; for (int i=0;i<len;i++) m+=arr->items[i].num; m/=(double)len;
    double sq=0; for (int i=0;i<len;i++) { double d=arr->items[i].num-m; sq+=d*d; }
    double sd = sqrt(sq/(double)len);
    RArray* out = rt_arr_alloc(len);
    for (int i=0;i<len;i++) rt_arr_push(out, rt_num(sd==0.0 ? 0.0 : (arr->items[i].num-m)/sd));
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_cumulativeSum(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr; RArray* out = rt_arr_alloc(arr->len);
    double running=0; for (int i=0;i<arr->len;i++) { running += arr->items[i].num; rt_arr_push(out, rt_num(running)); }
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_movingAverage(Value* a, int n) {
    (void)n; RArray* arr=a[0].arr; int len=arr->len; double windowD=a[1].num;
    if (windowD<1.0 || windowD>(double)len) rt_fatal(0,"'movingAverage' expects a window size between 1 and the array length");
    int window=(int)windowD;
    RArray* out = rt_arr_alloc(len-window+1);
    double windowSum=0; for (int i=0;i<window;i++) windowSum+=arr->items[i].num;
    rt_arr_push(out, rt_num(windowSum/(double)window));
    for (int i=window;i<len;i++) { windowSum += arr->items[i].num - arr->items[i-window].num; rt_arr_push(out, rt_num(windowSum/(double)window)); }
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}

// ---- UTF-8 codepoint helpers (mirrors utf8Codepoints/utf8CodepointValue in rin_interpreter.cpp) ----
typedef struct { char bytes[5]; int len; } RcCp;
typedef struct { RcCp* items; int len; } RcCpList;
static RcCpList rc_utf8_codepoints(const char* s) {
    RcCpList out; int cap = 16; out.items = (RcCp*)malloc(sizeof(RcCp)*cap); out.len = 0;
    size_t i = 0, slen = strlen(s);
    while (i < slen) {
        unsigned char c = (unsigned char)s[i];
        size_t len = 1;
        if ((c & 0x80) == 0x00) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else len = 1;
        if (i + len > slen) len = 1;
        if (out.len >= cap) { cap *= 2; out.items = (RcCp*)realloc(out.items, sizeof(RcCp)*cap); }
        memcpy(out.items[out.len].bytes, s+i, len); out.items[out.len].bytes[len]=0; out.items[out.len].len=(int)len;
        out.len++; i += len;
    }
    return out;
}
static long rc_utf8_cp_value(const RcCp* cp) {
    unsigned char c0 = (unsigned char)cp->bytes[0];
    if ((c0 & 0x80) == 0x00) return c0;
    if ((c0 & 0xE0) == 0xC0 && cp->len >= 2) return ((c0 & 0x1F) << 6) | ((unsigned char)cp->bytes[1] & 0x3F);
    if ((c0 & 0xF0) == 0xE0 && cp->len >= 3) return ((c0 & 0x0F) << 12) | (((unsigned char)cp->bytes[1] & 0x3F) << 6) | ((unsigned char)cp->bytes[2] & 0x3F);
    if ((c0 & 0xF8) == 0xF0 && cp->len >= 4) return ((c0 & 0x07) << 18) | (((unsigned char)cp->bytes[1] & 0x3F) << 12) | (((unsigned char)cp->bytes[2] & 0x3F) << 6) | ((unsigned char)cp->bytes[3] & 0x3F);
    return c0;
}
static int rc_is_arabic_diacritic_cp(long cp) {
    if (cp == 0x0640) return 1;
    if (cp >= 0x064B && cp <= 0x065F) return 1;
    if (cp == 0x0670) return 1;
    if (cp >= 0x06D6 && cp <= 0x06ED) return 1;
    return 0;
}
static int rc_is_arabic_letter_cp(long cp) {
    return (cp >= 0x0621 && cp <= 0x064A) || (cp >= 0x0660 && cp <= 0x0669) ||
           (cp >= 0x0670 && cp <= 0x06FF) || (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF);
}
static int rc_is_latin_letter_cp(long cp) { return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z'); }

static Value rt_native_utf8Len(Value* a, int n) { (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str); int len = cps.len; free(cps.items); return rt_num(len); }
static Value rt_native_utf8ToArray(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str);
    RArray* out = rt_arr_alloc(cps.len);
    for (int i = 0; i < cps.len; i++) rt_arr_push(out, rt_str(cps.items[i].bytes));
    free(cps.items);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = out; return v;
}
static Value rt_native_utf8CharAt(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str); long i = (long)a[1].num;
    if (i < 0 || i >= cps.len) { free(cps.items); rt_fatal(0, "'utf8CharAt': index out of range"); }
    Value r = rt_str(cps.items[i].bytes); free(cps.items); return r;
}
static Value rt_native_utf8Substr(Value* a, int n) {
    RcCpList cps = rc_utf8_codepoints(a[0].str);
    long start = (long)a[1].num; if (start < 0) start = 0; if (start > cps.len) start = cps.len;
    long len = (n == 3) ? (long)a[2].num : (long)cps.len - start;
    if (len < 0) len = 0;
    if (start + len > cps.len) len = cps.len - start;
    RtStrBuf sb; rt_sb_init(&sb);
    for (long k = start; k < start + len; k++) rt_sb_append(&sb, cps.items[k].bytes);
    free(cps.items);
    return rt_str_own(sb.buf);
}
static Value rt_native_utf8Reverse(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str);
    RtStrBuf sb; rt_sb_init(&sb);
    for (int i = cps.len - 1; i >= 0; i--) rt_sb_append(&sb, cps.items[i].bytes);
    free(cps.items);
    return rt_str_own(sb.buf);
}

// ---- Arabic normalization / script detection / edit distance / tokenization ----
static Value rt_native_arabicStripDiacritics(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str);
    RtStrBuf sb; rt_sb_init(&sb);
    for (int i = 0; i < cps.len; i++) if (!rc_is_arabic_diacritic_cp(rc_utf8_cp_value(&cps.items[i]))) rt_sb_append(&sb, cps.items[i].bytes);
    free(cps.items);
    return rt_str_own(sb.buf);
}
static Value rt_native_arabicNormalize(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str);
    RtStrBuf sb; rt_sb_init(&sb);
    for (int i = 0; i < cps.len; i++) {
        long v = rc_utf8_cp_value(&cps.items[i]);
        if (rc_is_arabic_diacritic_cp(v)) continue;
        if (v == 0x0623 || v == 0x0625 || v == 0x0622 || v == 0x0671) { rt_sb_append(&sb, "\xD8\xA7"); continue; }
        if (v == 0x0649) { rt_sb_append(&sb, "\xD9\x8A"); continue; }
        rt_sb_append(&sb, cps.items[i].bytes);
    }
    free(cps.items);
    return rt_str_own(sb.buf);
}
static Value rt_native_detectScript(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str);
    long arabicN=0, latinN=0, digitN=0, otherN=0;
    for (int i = 0; i < cps.len; i++) {
        long v = rc_utf8_cp_value(&cps.items[i]);
        if (v==' '||v=='\t'||v=='\n'||v=='\r') continue;
        if (rc_is_arabic_letter_cp(v)) arabicN++;
        else if (rc_is_latin_letter_cp(v)) latinN++;
        else if (v>='0'&&v<='9') digitN++;
        else otherN++;
    }
    free(cps.items);
    long total = arabicN+latinN+digitN+otherN;
    if (total==0) return rt_str("empty");
    if (arabicN>0 && latinN>0) return rt_str("mixed");
    if (arabicN>0) return rt_str("arabic");
    if (latinN>0) return rt_str("latin");
    if (digitN>0) return rt_str("digits");
    return rt_str("other");
}
static Value rt_native_levenshtein(Value* a, int n) {
    (void)n; RcCpList x = rc_utf8_codepoints(a[0].str), y = rc_utf8_codepoints(a[1].str);
    int nx = x.len, ny = y.len;
    size_t* dp = (size_t*)malloc(sizeof(size_t)*(nx+1)*(ny+1));
    #define RC_DP(i,j) dp[(i)*(ny+1)+(j)]
    for (int i=0;i<=nx;i++) RC_DP(i,0) = i;
    for (int j=0;j<=ny;j++) RC_DP(0,j) = j;
    for (int i=1;i<=nx;i++) for (int j=1;j<=ny;j++) {
        int cost = (x.items[i-1].len==y.items[j-1].len && memcmp(x.items[i-1].bytes,y.items[j-1].bytes,x.items[i-1].len)==0) ? 0 : 1;
        size_t del = RC_DP(i-1,j)+1, ins = RC_DP(i,j-1)+1, sub = RC_DP(i-1,j-1)+cost;
        size_t best = del<ins?del:ins; if (sub<best) best=sub;
        RC_DP(i,j) = best;
    }
    size_t result = RC_DP(nx,ny);
    #undef RC_DP
    free(dp); free(x.items); free(y.items);
    return rt_num((double)result);
}
static Value rt_native_tokenizeWords(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str);
    RArray* result = rt_arr_alloc(8);
    RtStrBuf cur; rt_sb_init(&cur); int curHasContent = 0;
    for (int i = 0; i < cps.len; i++) {
        long v = rc_utf8_cp_value(&cps.items[i]);
        int isWordCp = rc_is_arabic_letter_cp(v) || rc_is_latin_letter_cp(v) || (v>='0'&&v<='9') || rc_is_arabic_diacritic_cp(v);
        if (isWordCp) { rt_sb_append(&cur, cps.items[i].bytes); curHasContent = 1; }
        else if (curHasContent) { rt_arr_push(result, rt_str(cur.buf)); rt_sb_init(&cur); curHasContent = 0; }
    }
    if (curHasContent) rt_arr_push(result, rt_str(cur.buf));
    free(cps.items);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = result; return v;
}
static Value rt_native_splitSentences(Value* a, int n) {
    (void)n; RcCpList cps = rc_utf8_codepoints(a[0].str);
    RArray* result = rt_arr_alloc(4);
    RtStrBuf cur; rt_sb_init(&cur);
    for (int i = 0; i < cps.len; i++) {
        long v = rc_utf8_cp_value(&cps.items[i]);
        int isEnder = (v=='.'||v=='!'||v=='?'||v==0x061F||v==0x06D4);
        if (isEnder) {
            char* s = cur.buf; int len = (int)strlen(s);
            int b = 0; while (b<len && isspace((unsigned char)s[b])) b++;
            int e = len; while (e>b && isspace((unsigned char)s[e-1])) e--;
            if (e > b) { char* trimmed = (char*)malloc(e-b+1); memcpy(trimmed, s+b, e-b); trimmed[e-b]=0; rt_arr_push(result, rt_str_own(trimmed)); }
            rt_sb_init(&cur);
        } else rt_sb_append(&cur, cps.items[i].bytes);
    }
    { char* s = cur.buf; int len = (int)strlen(s);
      int b = 0; while (b<len && isspace((unsigned char)s[b])) b++;
      int e = len; while (e>b && isspace((unsigned char)s[e-1])) e--;
      if (e > b) { char* trimmed = (char*)malloc(e-b+1); memcpy(trimmed, s+b, e-b); trimmed[e-b]=0; rt_arr_push(result, rt_str_own(trimmed)); } }
    free(cps.items);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = result; return v;
}

// ---- Regex (POSIX <regex.h> ERE, NOT full ECMAScript std::regex like the interpreter uses) ----
// Same "i:"/"m:" optional flag-prefix convention as compileRinRegex() in rin_interpreter.cpp
// ("i:^abc$" = case-insensitive, "im:..." = both) — but the pattern DIALECT itself is POSIX
// Extended Regular Expressions, not ECMAScript: no \d/\w/\b/non-greedy/(?:...)/lookaround. Most
// everyday patterns (literal text, character classes, anchors, */+/?/{}, alternation, groups)
// behave the same either way; anything relying on ECMAScript-only escapes needs rewriting for
// this compiler specifically (an explicit, documented limitation — not a silent behavior change).
static regex_t rc_compile_regex(const char* rawPattern, const char* fnName) {
    const char* pattern = rawPattern;
    int cflags = REG_EXTENDED;
    int plen = (int)strlen(rawPattern);
    if (plen >= 2 && rawPattern[1] == ':' && (rawPattern[0]=='i'||rawPattern[0]=='m'||rawPattern[0]=='I'||rawPattern[0]=='M')) {
        const char* colon = strchr(rawPattern, ':');
        int looksLikeFlags = 1;
        for (const char* p = rawPattern; p < colon; p++) {
            char lo = (char)tolower((unsigned char)*p);
            if (lo != 'i' && lo != 'm') { looksLikeFlags = 0; break; }
        }
        if (looksLikeFlags) {
            for (const char* p = rawPattern; p < colon; p++) {
                char lo = (char)tolower((unsigned char)*p);
                if (lo == 'i') cflags |= REG_ICASE;
                if (lo == 'm') cflags |= REG_NEWLINE;
            }
            pattern = colon + 1;
        }
    }
    regex_t re;
    int rc = regcomp(&re, pattern, cflags);
    if (rc != 0) {
        char errbuf[256]; regerror(rc, &re, errbuf, sizeof(errbuf));
        char msg[320]; snprintf(msg, sizeof(msg), "'%s': نمط regex غير صالح: %s", fnName, errbuf);
        rt_fatal(0, msg);
    }
    return re;
}
static Value rt_native_regexTest(Value* a, int n) {
    (void)n; regex_t re = rc_compile_regex(a[1].str, "regexTest");
    int matched = (regexec(&re, a[0].str, 0, NULL, 0) == 0);
    regfree(&re);
    return rt_bool(matched);
}
static Value rt_native_regexFind(Value* a, int n) {
    (void)n; regex_t re = rc_compile_regex(a[1].str, "regexFind");
    regmatch_t m[1];
    Value result;
    if (regexec(&re, a[0].str, 1, m, 0) != 0) result = rt_str("");
    else { int len = (int)(m[0].rm_eo - m[0].rm_so); char* buf = (char*)malloc(len+1); memcpy(buf, a[0].str+m[0].rm_so, len); buf[len]=0; result = rt_str_own(buf); }
    regfree(&re);
    return result;
}
static Value rt_native_regexFindAll(Value* a, int n) {
    (void)n; regex_t re = rc_compile_regex(a[1].str, "regexFindAll");
    RArray* result = rt_arr_alloc(4);
    const char* s = a[0].str; size_t off = 0; size_t slen = strlen(s);
    regmatch_t m[1];
    while (off <= slen) {
        if (regexec(&re, s+off, 1, m, off>0 ? REG_NOTBOL : 0) != 0) break;
        int start = (int)(off+m[0].rm_so), end = (int)(off+m[0].rm_eo);
        int len = end - start; char* buf = (char*)malloc(len+1); memcpy(buf, s+start, len); buf[len]=0;
        rt_arr_push(result, rt_str_own(buf));
        off = (m[0].rm_eo == m[0].rm_so) ? off + m[0].rm_eo + 1 : off + m[0].rm_eo;
    }
    regfree(&re);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = result; return v;
}
static Value rt_native_regexGroups(Value* a, int n) {
    (void)n; regex_t re = rc_compile_regex(a[1].str, "regexGroups");
    size_t nsub = re.re_nsub + 1;
    regmatch_t* m = (regmatch_t*)malloc(sizeof(regmatch_t)*nsub);
    RArray* result = rt_arr_alloc((int)nsub);
    if (regexec(&re, a[0].str, nsub, m, 0) == 0) {
        for (size_t i = 0; i < nsub; i++) {
            if (m[i].rm_so < 0) { rt_arr_push(result, rt_str("")); continue; }
            int len = (int)(m[i].rm_eo - m[i].rm_so); char* buf = (char*)malloc(len+1); memcpy(buf, a[0].str+m[i].rm_so, len); buf[len]=0;
            rt_arr_push(result, rt_str_own(buf));
        }
    }
    free(m); regfree(&re);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = result; return v;
}
// Expands $1/$2/... group references in `repl` against match array m (nsub+1 entries) into sb.
static void rc_format_replacement(RtStrBuf* sb, const char* repl, const char* subject, regmatch_t* m, size_t nsub) {
    for (const char* p = repl; *p; p++) {
        if (*p == '$' && isdigit((unsigned char)p[1])) {
            int idx = p[1] - '0'; p++;
            if ((size_t)idx < nsub && m[idx].rm_so >= 0) {
                int len = (int)(m[idx].rm_eo - m[idx].rm_so);
                char* tmp = (char*)malloc(len+1); memcpy(tmp, subject+m[idx].rm_so, len); tmp[len]=0;
                rt_sb_append(sb, tmp); free(tmp);
            }
        } else { char one[2] = {*p, 0}; rt_sb_append(sb, one); }
    }
}
static Value rt_native_regexReplace(Value* a, int n) {
    regex_t re = rc_compile_regex(a[1].str, "regexReplace");
    const char* s = a[0].str; const char* repl = a[2].str;
    int all = (n == 4) ? rt_truthy(a[3]) : 1;
    size_t nsub = re.re_nsub + 1;
    regmatch_t* m = (regmatch_t*)malloc(sizeof(regmatch_t)*nsub);
    RtStrBuf sb; rt_sb_init(&sb);
    if (!all) {
        if (regexec(&re, s, nsub, m, 0) == 0) {
            char* pre = (char*)malloc(m[0].rm_so+1); memcpy(pre, s, m[0].rm_so); pre[m[0].rm_so]=0;
            rt_sb_append(&sb, pre); free(pre);
            rc_format_replacement(&sb, repl, s, m, nsub);
            rt_sb_append(&sb, s + m[0].rm_eo);
        } else rt_sb_append(&sb, s);
    } else {
        size_t off = 0, slen = strlen(s);
        while (off <= slen) {
            if (regexec(&re, s+off, nsub, m, off>0 ? REG_NOTBOL : 0) != 0) { rt_sb_append(&sb, s+off); break; }
            char* pre = (char*)malloc(m[0].rm_so+1); memcpy(pre, s+off, m[0].rm_so); pre[m[0].rm_so]=0;
            rt_sb_append(&sb, pre); free(pre);
            // group offsets from regexec above are relative to s+off; rebase for rc_format_replacement's absolute `subject`
            regmatch_t* mrebased = (regmatch_t*)malloc(sizeof(regmatch_t)*nsub);
            for (size_t i = 0; i < nsub; i++) {
                if (m[i].rm_so < 0) { mrebased[i].rm_so = -1; mrebased[i].rm_eo = -1; }
                else { mrebased[i].rm_so = (regoff_t)(off + m[i].rm_so); mrebased[i].rm_eo = (regoff_t)(off + m[i].rm_eo); }
            }
            rc_format_replacement(&sb, repl, s, mrebased, nsub);
            size_t newOff = (m[0].rm_eo == m[0].rm_so) ? off + m[0].rm_eo + 1 : off + m[0].rm_eo;
            if (m[0].rm_eo == m[0].rm_so && off + m[0].rm_eo < slen) { char one[2] = {s[off+m[0].rm_eo], 0}; rt_sb_append(&sb, one); }
            free(mrebased);
            off = newOff;
            if (off > slen) break;
        }
    }
    free(m); regfree(&re);
    return rt_str_own(sb.buf);
}
static Value rt_native_regexSplit(Value* a, int n) {
    (void)n; regex_t re = rc_compile_regex(a[1].str, "regexSplit");
    RArray* result = rt_arr_alloc(4);
    const char* s = a[0].str; size_t off = 0, slen = strlen(s); size_t lastCut = 0;
    regmatch_t m[1]; int foundAny = 0;
    while (off <= slen) {
        if (regexec(&re, s+off, 1, m, off>0 ? REG_NOTBOL : 0) != 0) break;
        size_t start = off + m[0].rm_so, end = off + m[0].rm_eo;
        if (end == start) { off = start + 1; continue; } // avoid an infinite loop on zero-width matches
        foundAny = 1;
        int len = (int)(start - lastCut); char* buf = (char*)malloc(len+1); memcpy(buf, s+lastCut, len); buf[len]=0;
        rt_arr_push(result, rt_str_own(buf));
        lastCut = end; off = end;
    }
    if (!foundAny) { rt_arr_push(result, rt_str(s)); }
    else { char* buf = (char*)malloc(slen-lastCut+1); memcpy(buf, s+lastCut, slen-lastCut); buf[slen-lastCut]=0; rt_arr_push(result, rt_str_own(buf)); }
    regfree(&re);
    Value v = rt_nil(); v.t = RT_ARR; v.arr = result; return v;
}


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

// ============================================================================
// 5.5) وضع "تضمين المفسّر" (interpreter-embed fallback)
// ============================================================================
// عند رفض ميزة كـ RincUnsupportedFeature (حاويات/NoSQL/API/دردشة/...)، main() لا يتوقف عن
// التجميع كلياً — بدلاً من ذلك يولّد ملف C++ (وليس C) يُضمِّن المفسّر الأصلي الحقيقي
// (rin_lexer/rin_parser/rin_interpreter) وينفّذ نص Rin الأصلي كاملاً عبره وقت التشغيل. الناتج
// ملف تنفيذي واحد مستقل بلا اعتماد على تطبيق أندرويد، بدلالة مطابقة 100% لتشغيله عبر المفسّر
// (لأنه *هو* المفسّر فعلياً، لا محاكاة له) — على حساب فقدان مكسب السرعة الذي يوفّره التحويل
// الحقيقي إلى C للأجزاء الإجرائية. هذا يعني عملياً: كل ملف .rin صالح نحوياً يُنتج تنفيذياً
// عاملاً عبر rinc الآن، بلا أي رفض للميزات غير القابلة للترجمة المباشرة.
static bool fileExistsOnDisk(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// يبحث عن مجلد المصدر الحقيقي للمفسّر (يحوي rin_lexer.cpp/rin_parser.cpp/rin_interpreter.cpp/
// rin_http.cpp/diagnostics/) بالترتيب: مسار مُمرَّر صراحةً (--engine-src)، ثم بجوار الملف
// التنفيذي rinc نفسه (يغطي حالة تشغيل النسخة المتماثلة الموجودة داخل app/src/main/cpp/rinc.cpp
// من نفس المجلد)، ثم مجلد الأشقاء ../app/src/main/cpp (يغطي حالة تشغيل rinc من compiler/)، ثم
// نفس المسارين نسبةً لمجلد العمل الحالي (تغطية إضافية عند التشغيل من جذر المستودع).
static std::string findEngineSrc(const std::string& argv0, const std::string& override) {
    if (!override.empty()) return override;
    std::string exeDir = ".";
    auto slash = argv0.find_last_of("/\\");
    if (slash != std::string::npos) exeDir = argv0.substr(0, slash);
    std::vector<std::string> candidates = {
        exeDir,
        exeDir + "/../app/src/main/cpp",
        exeDir + "/../../app/src/main/cpp",
        "app/src/main/cpp",
        "../app/src/main/cpp",
        "."
    };
    for (auto& c : candidates) {
        if (fileExistsOnDisk(c + "/rin_interpreter.cpp") && fileExistsOnDisk(c + "/rin_lexer.cpp") &&
            fileExistsOnDisk(c + "/rin_parser.cpp")) {
            return c;
        }
    }
    return "";
}

static std::string generateEmbedFallback(const std::string& rinSource) {
    std::ostringstream out;
    out << "// Auto-generated by rinc (interpreter-embed fallback mode) — do not edit\n"
        << "// وُلِّد هذا الملف تلقائياً لأن البرنامج يستخدم ميزة غير قابلة للترجمة المباشرة إلى C\n"
        << "// (حاويات/NoSQL/API/دردشة/إلخ)، فبدل رفض التجميع، يُضمَّن المفسّر الأصلي الحقيقي هنا.\n"
        << "#include \"rin_lexer.h\"\n#include \"rin_parser.h\"\n#include \"rin_interpreter.h\"\n"
        << "#include <iostream>\n\nint main() {\n"
        << "    std::string source = " << cLiteralEscape(rinSource) << ";\n"
        << "    try {\n"
        << "        rin::Lexer lexer(source);\n"
        << "        auto tokens = lexer.scanTokens();\n"
        << "        rin::Parser parser(tokens);\n"
        << "        auto statements = parser.parse();\n"
        << "        rin::Interpreter interp;\n"
        << "        std::string out = interp.run(statements);\n"
        << "        std::cout << out;\n"
        << "    } catch (rin::RinError& e) {\n"
        << "        std::cerr << \"Parse/Lex error at line \" << e.line << \": \" << e.message << std::endl;\n"
        << "        return 1;\n"
        << "    }\n"
        << "    return 0;\n}\n";
    return out.str();
}

static void printUsage() {
    std::cerr <<
        "الاستخدام: rinc <input.rin> [-o output] [--cc=COMPILER] [--emit-c-only] [--keep-c]\n"
        "                 [--engine-src=PATH] [--no-embed-fallback]\n"
        "  -o output            اسم الملف التنفيذي الناتج (افتراضياً اسم ملف الدخل بلا امتداد)\n"
        "  --cc=COMPILER        يفرض مترجم C/C++ محدد (مثال: --cc=clang) بدل الاكتشاف التلقائي\n"
        "  --emit-c-only        يكتفي بتوليد ملف .c/.cpp دون بنائه إلى تنفيذي\n"
        "  --keep-c             يُبقي ملف .c/.cpp الوسيط بعد نجاح البناء (يُحذف تلقائياً افتراضياً)\n"
        "  --engine-src=PATH    مسار مجلد مصدر المفسّر الأصلي (app/src/main/cpp) لوضع تضمين\n"
        "                       المفسّر — يُكتشَف تلقائياً عادةً، استخدم هذا فقط إن فشل الاكتشاف\n"
        "  --no-embed-fallback  يرفض الميزات غير المدعومة بخطأ تجميع بدل تضمين المفسّر تلقائياً\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(); return 1; }
    std::string inputPath, outputPath, ccOverride, engineSrcOverride;
    bool emitCOnly = false, keepC = false, noEmbedFallback = false;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { outputPath = argv[++i]; }
        else if (a.rfind("--cc=", 0) == 0) { ccOverride = a.substr(5); }
        else if (a.rfind("--engine-src=", 0) == 0) { engineSrcOverride = a.substr(13); }
        else if (a == "--emit-c-only") { emitCOnly = true; }
        else if (a == "--keep-c") { keepC = true; }
        else if (a == "--no-embed-fallback") { noEmbedFallback = true; }
        else if (a == "-h" || a == "--help") { printUsage(); return 0; }
        else if (a == "-v" || a == "--version") { std::cout << "rinc 0.5.0\n"; return 0; }
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

    std::string source;
    try {
        source = readFile(inputPath);
    } catch (const std::exception& ex) {
        std::cerr << "خطأ: " << ex.what() << "\n";
        return 1;
    }

    // وضع الترجمة الطبيعي (transpile إلى C) هو المحاولة الأولى دائماً. لا يتحوّل لوضع تضمين
    // المفسّر إلا عند RincUnsupportedFeature تحديداً (ميزة مرفوضة عمداً)، وليس عند أي RincError
    // آخر (خطأ نحوي حقيقي في كود المستخدم يبقى يفشل فوراً برسالته الواضحة كما كان دائماً).
    std::string generatedC;
    bool useEmbedFallback = false;
    std::string unsupportedReason;
    int unsupportedLine = 0;
    try {
        Lexer lexer(source);
        auto tokens = lexer.scanTokens();
        Parser parser(tokens);
        auto program = parser.parse();
        CodeGen gen;
        generatedC = gen.generate(program);
    } catch (const RincUnsupportedFeature& e) {
        useEmbedFallback = true;
        unsupportedReason = e.message;
        unsupportedLine = e.line;
    } catch (const RincError& e) {
        std::cerr << "خطأ تجميع (سطر " << e.line << "): " << e.message << "\n";
        return 1;
    }

    if (useEmbedFallback && noEmbedFallback) {
        std::cerr << "خطأ تجميع (سطر " << unsupportedLine << "): " << unsupportedReason << "\n";
        return 1;
    }

    if (useEmbedFallback) {
        std::string engineSrc = findEngineSrc(argv[0], engineSrcOverride);
        if (engineSrc.empty()) {
            std::cerr << "✗ الميزة غير مدعومة مباشرةً في التحويل إلى C (سطر " << unsupportedLine
                      << "): " << unsupportedReason << "\n"
                      << "  حاولتُ التحوّل تلقائياً لوضع تضمين المفسّر لكن تعذّر العثور على مجلد\n"
                      << "  مصدر المفسّر (rin_lexer.cpp/rin_parser.cpp/rin_interpreter.cpp). مرّره\n"
                      << "  صراحةً عبر --engine-src=PATH (مثال: --engine-src=app/src/main/cpp).\n";
            return 1;
        }
        std::cout << "ℹ الميزة غير مدعومة مباشرةً في التحويل إلى C (سطر " << unsupportedLine
                  << "): " << unsupportedReason << "\n"
                  << "  تحويل تلقائي لوضع تضمين المفسّر (engine: " << engineSrc << ")...\n";

        std::string cppPath = base + ".embed.cpp";
        std::string generatedCpp = generateEmbedFallback(source);
        { std::ofstream out(cppPath, std::ios::binary); out << generatedCpp; }
        std::cout << "✓ تم توليد الكود: " << cppPath << "\n";
        if (emitCOnly) return 0;

        std::vector<std::string> compilers;
        if (!ccOverride.empty()) compilers.push_back(ccOverride);
        else { compilers.push_back("c++"); compilers.push_back("g++"); compilers.push_back("clang++"); }

        std::string diagCpp = engineSrc + "/diagnostics";
        std::string sources = " " + cppPath +
            " " + engineSrc + "/rin_lexer.cpp" +
            " " + engineSrc + "/rin_parser.cpp" +
            " " + engineSrc + "/rin_interpreter.cpp" +
            " " + engineSrc + "/rin_http.cpp" +
            " " + diagCpp + "/diagnostic.cpp" +
            " " + diagCpp + "/diagnostic_engine.cpp" +
            " " + diagCpp + "/diagnostic_renderer.cpp" +
            " " + diagCpp + "/source_manager.cpp";
        std::string includes = " -I" + engineSrc + " -I" + diagCpp;

        bool built = false;
        for (auto& cc : compilers) {
            std::string cmd = cc + " -O2 -std=c++17" + includes + " -o " + outputPath + sources +
                               " -lz 2> " + outputPath + ".build.log";
            int rc = system(cmd.c_str());
            if (rc == 0) { built = true; remove((outputPath + ".build.log").c_str()); break; }
        }
        if (!built) {
            std::cerr << "✗ تعذّر العثور على مترجم C++ يعمل (جُرِّب: ";
            for (size_t i = 0; i < compilers.size(); i++) { if (i) std::cerr << ", "; std::cerr << compilers[i]; }
            std::cerr << "). تحقّق من سجل الأخطاء في " << outputPath << ".build.log إن وُجد.\n";
            return 1;
        }
        if (!keepC) remove(cppPath.c_str());
        std::cout << "✓ تم بناء الملف التنفيذي (وضع تضمين المفسّر): ./" << outputPath << "\n";
        return 0;
    }

    std::string cPath = base + ".c";
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

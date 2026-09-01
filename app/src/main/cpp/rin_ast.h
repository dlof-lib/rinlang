#pragma once
#include "rin_common.h"
#include <memory>
#include <vector>

namespace rin {

// ---- Expressions ----
struct Expr {
    virtual ~Expr() = default;
    int line = 0;
};
using ExprPtr = std::shared_ptr<Expr>;

struct LiteralExpr : Expr {
    enum class Kind { NUMBER, STRING, BOOL, NIL } kind;
    double number = 0.0;
    std::string str;
    bool boolean = false;
};

struct VariableExpr : Expr {
    std::string name;
};

struct AssignExpr : Expr {
    std::string name;
    ExprPtr value;
};

struct BinaryExpr : Expr {
    ExprPtr left;
    TokenType op;
    ExprPtr right;
};

struct LogicalExpr : Expr {
    ExprPtr left;
    TokenType op; // AND / OR
    ExprPtr right;
};

struct UnaryExpr : Expr {
    TokenType op;
    ExprPtr right;
};

struct CallExpr : Expr {
    std::string callee;
    std::vector<ExprPtr> args;

    // ---- RinFlow (see rin_interpreter.h: FlowGraph/FlowNode/PipelineTracer) ----
    // Set only by Parser::pipeline() when this CallExpr was produced by desugaring a `|>` stage
    // (never for an ordinary `f(x)` call written directly by the user). This does NOT change what
    // gets executed -- a piped chain still evaluates as plain nested calls exactly as before -- it
    // only lets Interpreter::evaluate() *recognize*, after the fact, that a given CallExpr chain
    // came from `|>` so it can optionally drive it through the Flow engine when a flow session is
    // active. Nothing reads these fields unless a flow session is armed (see
    // Interpreter::runProgramAsFlow), so ordinary `rin::Interpreter::run()` behaves identically to
    // before this field existed.
    bool isPipelineNode = false;
    // Set once, on the outermost CallExpr of a whole `a |> b() |> c()` chain (i.e. the Expr that
    // Parser::pipeline() ultimately returns), never on the intermediate stages nested inside it.
    bool isPipelineRoot = false;
    // Number of `|>` stages in the chain rooted at this CallExpr (only meaningful when
    // isPipelineRoot is true). Does not count the original left-most input expression itself.
    int pipelineStageCount = 0;
};

// [1, 2, 3] -> مصفوفة (array)
struct ArrayExpr : Expr {
    std::vector<ExprPtr> elements;
};

// { key: value, ... } -> قاموس (map)
struct MapEntry { ExprPtr key; ExprPtr value; };
struct MapExpr : Expr {
    std::vector<MapEntry> entries;
};

// object[index] -> قراءة عنصر من مصفوفة/قاموس/نص
struct IndexExpr : Expr {
    ExprPtr object;
    ExprPtr index;
};

// object[index] = value -> كتابة/تعديل عنصر في مصفوفة أو قاموس
struct IndexSetExpr : Expr {
    ExprPtr object;
    ExprPtr index;
    ExprPtr value;
};

// ---- Statements ----
struct Stmt {
    virtual ~Stmt() = default;
    int line = 0;
};
using StmtPtr = std::shared_ptr<Stmt>;

struct ExpressionStmt : Stmt { ExprPtr expr; };
// print expr1, expr2, ... [sep=expr] [end=expr] [if=expr] [level=expr] [label=expr]
//       [repeat=expr] [pretty=expr] [upper=expr] [lower=expr] [width=expr] [align=expr];
// السلوك الافتراضي (قيمة واحدة، بلا أي سمة) مطابق تماماً للسابق: قيمة واحدة + سطر جديد "\n".
// كل السمات أدناه اختيارية وإضافية بحتة (additive)، بأي ترتيب بينها، كل واحدة مرة واحدة على
// الأكثر، وتُقيَّم كتعبيرات عادية (وليست حصراً حرفاً حرفياً) لكن يجب أن تُقيَّم إلى النوع المتوقَّع
// وقت التنفيذ وإلا خطأ صريح — تماماً كبقية سمات key=value الأخرى في اللغة (document/row/route/save):
//   1) exprs : أكثر من قيمة مفصولة بفواصل في نفس أمر print الواحد (مثال: print "x=", x;)
//   2) sep   : فاصل مخصص يُطبع بين كل قيمتين متتاليتين عند تعدّد القيم (افتراضياً مسافة واحدة " ")
//   3) end   : ما يُطبع في نهاية السطر بدل "\n" الافتراضي — end="" يمنع السطر الجديد تماماً، فيسمح
//              بعدّة أوامر print متتالية تكمل بعضها على نفس السطر (عدّادات/أشرطة تقدّم console)
//   4) if    : بوابة تنفيذ (guard) — عند تقييمها إلى قيمة زائفة (falsy) يصبح أمر print بأكمله
//              no-op تماماً: لا تُقيَّم exprs ولا أي سمة أخرى إطلاقاً (مفيد لسجلّات verbose/debug
//              بلا الحاجة لتغليف print بجملة if{} منفصلة، ودون أي أثر جانبي غير مرغوب)
//   5) level : "info" | "success" | "warn" | "error" | "debug" — يضيف رمزاً مميّزاً في بداية السطر
//              يلتقطه RinConsoleFormatter.kt (تطبيق أندرويد) لتلوين/تصنيف السطر بصرياً في الكونسول
//   6) label : وسم نصي مخصّص يُطبع كـ "[LABEL] " بعد رمز level (أو في البداية إن غاب level) —
//              مفيد لتمييز مصدر رسالة التتبّع (مثال: label="AUTH")
//   7) repeat: عدد صحيح غير سالب n — يُعيد طباعة نفس السطر كاملاً (البادئة + المحتوى + end) n مرة؛
//              n=0 لا يطبع شيئاً إطلاقاً (فواصل/بانرات console: print "-", repeat=40, end="";)
//   8) pretty: عند true، أي قيمة من نوع مصفوفة (array) أو قاموس (map) ضمن exprs تُطبع بتهيئة
//              متعددة الأسطر بمسافات بادئة (2 لكل مستوى) بدل التمثيل المضغوط سطر واحد المعتاد —
//              مفيد لتفقّد بيانات متداخلة أثناء التطوير
//   9) upper/lower: يحوّل النص النهائي المُجمَّع (بعد pretty وقبل width) بالكامل لأحرف كبيرة/صغيرة؛
//              استخدام الاثنين معاً في نفس أمر print خطأ صريح
//   10) width/align: يحشو (بمسافات) النص النهائي المُجمَّع لعرض لا يقل عن width حرفاً — align
//              يحدّد جهة الحشو: "left" (افتراضي) | "right" | "center"؛ align بلا width خطأ صريح
//              (محاذاة بلا عرض هدف لا معنى لها)؛ لا يُقصَّر المحتوى أبداً إن كان أطول من width.
struct PrintStmt : Stmt {
    std::vector<ExprPtr> exprs;
    ExprPtr sep; // nullptr = افتراضي " "
    ExprPtr end; // nullptr = افتراضي "\n"
    ExprPtr ifCond;  // nullptr = يُطبع دائماً (بلا بوابة شرط)
    ExprPtr level;   // nullptr = بلا رمز تصنيف
    ExprPtr label;   // nullptr = بلا وسم
    ExprPtr repeatN; // nullptr = افتراضي 1 (مرة واحدة)
    ExprPtr pretty;  // nullptr = افتراضي false (تمثيل مضغوط سطر واحد)
    ExprPtr upper;   // nullptr = افتراضي false
    ExprPtr lower;   // nullptr = افتراضي false
    ExprPtr width;   // nullptr = بلا حشو
    ExprPtr align;   // nullptr = افتراضي "left" (فقط له معنى مع width)
};
// print.log(...);  |  print.log.info(...);  |  print.log.warn(...);  |  print.log.error(...);  |  print.log.debug(...);
// نظام Log منظَّم (structured logging)، إضافة مستقلة تماماً عن print العادي أعلاه (لا تُعدِّل أي
// سلوك موجود). 'print' توكن محجوز (TokenType::PRINT) وليس IDENT، لذا لا يمكن أن يظهر كمستقبِل
// (receiver) في تعبير استدعاء عام receiver.method(...) — اللغة لا تملك مثل هذا التعبير أصلاً (انظر
// ملاحظة bannerConvenience في rin_interpreter.cpp). بدل ذلك، وبنفس أسلوب `view.print/object(...)`
// أعلاه بالضبط، هذه سلسلة كلمات محجوزة/سياقية خاصة يتعرّف عليها المحلل عند بداية العبارة فقط:
// PRINT '.' IDENT("log") ['.' IDENT(info|warn|error|debug)] '(' args ')' ';'
//   - `print.log(...)`         -> مستوى عام "log" (بلا تصنيف مسبق)
//   - `print.log.info(...)`    -> مستوى "info"
//   - `print.log.warn(...)`    -> مستوى "warn"
//   - `print.log.error(...)`   -> مستوى "error"
//   - `print.log.debug(...)`   -> مستوى "debug"
// داخل القوسين: رسائل (قيم عادية مفصولة بفواصل، تُجمَّع بـ sep=) وسمات key=value اختيارية، بأي
// ترتيب بينها، كل واحدة مرة على الأكثر: sep=expr (فاصل بين الرسائل، افتراضياً " ") | if=expr (بوابة
// تنفيذ، falsy = no-op تام بلا أي سجلّ) | label=expr (وسم إضافي "[LABEL] " بعد رمز المستوى) |
// source=expr (يتجاوز sourceFile الافتراضي لحقل Source فقط لهذا السجلّ). مثال:
//   print.log.info("multi", "values", sep="-", label="AUTH");
// كل استدعاء Log واحد يُنتج سجلّاً (LogEntry، انظر rin_interpreter.h) يحمل خمسة حقول دائماً:
//   Time (وقت التنفيذ الفعلي HH:MM:SS)، Level (أحد الخمسة أعلاه)، Message (exprs مُجمَّعة بـ sep)،
//   Source (اسم الملف الحالي — sourceFile، أو مصدر مخصّص عبر source=)، Line (رقم السطر في الكود).
// السجلّات تُراكَم داخلياً في Interpreter::logHistory_ (انظر logHistory()/logSave()/logClear()
// natives) — هذا ما يجعل النظام "قابلاً للتوسّع مستقبلاً إلى ملفات Log": أي ميزة لاحقة (كتابة كل
// سجلّ فور حدوثه إلى ملف، تدوير ملفات log, ...) تُبنى فوق logHistory_ الموجود بالفعل دون أي تغيير
// على شكل عبارة print.log نفسها.
struct LogStmt : Stmt {
    std::string level; // "log" | "info" | "warn" | "error" | "debug" — يُحدَّد وقت التحليل (ثابت)
    std::vector<ExprPtr> exprs;
    ExprPtr sep;    // nullptr = افتراضي " "
    ExprPtr ifCond; // nullptr = يُسجَّل دائماً (بلا بوابة شرط)
    ExprPtr label;  // nullptr = بلا وسم إضافي
    ExprPtr source; // nullptr = يُستخدم sourceFile الحالي للمفسِّر
};
struct LetStmt : Stmt { std::string name; ExprPtr initializer; };
struct BlockStmt : Stmt { std::vector<StmtPtr> statements; };
struct IfStmt : Stmt {
    ExprPtr condition;
    StmtPtr thenBranch;
    StmtPtr elseBranch; // may be null
};
struct WhileStmt : Stmt {
    ExprPtr condition;
    StmtPtr body;
};
// for (initializer; condition; increment) body -> حلقة for على طراز C (إضافة جديدة additive بحتة،
// نفس روح إضافة AUKT: لا تغيير على أي عقدة/سلوك موجود مسبقاً). الثلاثة أجزاء اختيارية تماماً كـ C:
//   - initializer: قد تكون 'let x = ...;' أو عبارة تعبير (expression;) أو فارغة (';' فقط)
//   - condition: تعبير منطقي يُقيَّم قبل كل تكرار؛ فارغ يعني true دائماً (حلقة لا نهائية إلا بـ break)
//   - increment: تعبير يُنفَّذ بعد كل تكرار (حتى بعد continue)؛ فارغ يعني لا شيء
// break/continue تعمل بالضبط كما في while (تُحسب ضمن loopDepth في المحلل النحوي).
struct ForStmt : Stmt {
    StmtPtr initializer; // may be null
    ExprPtr condition;   // may be null -> يُعامل كـ true
    ExprPtr increment;   // may be null
    StmtPtr body;
};
// plus.condition (condition) { trueBranch } / { falseBranch } -> "شرط ثلاثي" عام على مستوى
// العبارات (statement-level ternary)، إضافة جديدة additive بحتة فوق if/else الموجودة أصلاً.
// الفرق الجوهري عن if/else:
//   1) الكتلتان (true/false) **إلزاميتان دائماً** (بخلاف else الاختيارية في IfStmt) — لأن الفكرة
//      تعبيرية/ثلاثية بطبيعتها: يجب أن يكون هناك مسار واضح لكل من الحالتين.
//   2) الكلمة المفتاحية "plus.condition" (مثل "container.pipe") كلمة مركّبة سياقية غير محجوزة
//      عالمياً — لا تتعارض مع أي متغيّر موجود اسمه "plus" إلا إذا ظهر تماماً بصيغة plus.condition
//      في بداية عبارة (نفس أسلوب route/row/style/document/warp).
//   3) عام تماماً: تُبنى كتلتاه عبر block()، أي تدعم أي عبارة عادية (print, let, ...) وأيضاً أي
//      إعلان حاوية (@container, @Containers.Group, ...) بداخلها، تماماً كجسم أي دالة أو حلقة.
// الفاصل بين الكتلتين هو '/' (يُعاد استخدام توكن SLASH الموجود أصلاً لعامل القسمة — لا حاجة لتوكن
// جديد، ولا تعارض لأن '/' هنا يظهر حصراً بين '}' الأولى و'{' الثانية داخل بنية plus.condition).
struct PlusConditionStmt : Stmt {
    ExprPtr condition;
    std::shared_ptr<BlockStmt> trueBranch;  // ينفَّذ إذا كان condition صحيحاً (truthy)
    std::shared_ptr<BlockStmt> falseBranch; // ينفَّذ إذا كان condition خاطئاً
};
struct FunctionStmt : Stmt {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<BlockStmt> body;
};
struct ReturnStmt : Stmt {
    ExprPtr value; // may be null
};
// break; -> يخرج فوراً من أقرب حلقة while محيطة
struct BreakStmt : Stmt {};
// continue; -> يقفز مباشرة إلى فحص شرط أقرب حلقة while محيطة (يتجاوز باقي جسم الحلقة)
struct ContinueStmt : Stmt {};

// ---- Data-container language statements (container / Containers.Group / Volume / Section ...) ----

// text name = "..."; -> إعلان قيمة من نوع نصي (text)
struct TextStmt : Stmt {
    std::string name;
    ExprPtr initializer;
};

// @container=name  <body>  .end/container
// @container.pipe=name  <body>  .end/container.pipe   -> خط أنابيب بيانات/إحصاء
// @container.data=name  <body>  .end/container.data    -> حاوية بيانات نقية (لا دوال ولا حاويات متداخلة)
// @container.api=name   <body>  .end/container.api     -> حاوية تُعرِّف نقاط API وهمية (route ...) ويمكن استدعاؤها عبر call()
// @container.import=name  file path="..."; .end/container.import -> يستورد فعليًا محتوى ملف .rin آخر وينفّذه
// @container.table=name  <body>  .end/container.table   -> حاوية جدول (مدمجة داخل container): صفوف row + style اختياري
// @table=name             <body>  .end/table              -> نفس مفهوم الجدول، لكن بشكل مستقل (بلا بادئة container.)
//                                                             كلا الشكلين ينتجان نفس ContainerKind::TABLE
// @container.doc=name    <body>  .end/container.doc      -> حاوية NoSQL (مدمجة داخل container): مستندات document
// @doc=name               <body>  .end/doc                 -> نفس مفهوم قاعدة البيانات اللاعلاقية، بشكل مستقل
//                                                             كلا الشكلين ينتجان نفس ContainerKind::DOC.
//                                                             container هنا يمثّل "مجموعة مستندات" (collection)،
//                                                             و Containers.Group التي تحتويها تمثّل "قاعدة بيانات" (database)
//                                                             كاملة من عدّة مجموعات مستندات.
//
// ---- مفاهيم التنسيق والستايل (formatting/style) ----
// الفكرة: كائن مسمّى بحقول حرة (name/color/re/...) عبر text/let عادية، ونمط عرض اختياري عبر
// عبارة 'style' (المشتركة أصلاً مع container.table)، مع كتل واجهة جاهزة (شريط علوي/سفلي/زر).
//
// @container.object=name  <body>  .end/container.object   -> "كائن" ببيانات نقية (حقول حرة + style اختياري)
// @Object=name            <body>  .end/Object              -> نفس الشيء، بشكل مستقل (بلا بادئة container.)
//                                                              كلا الشكلين ينتجان نفس ContainerKind::OBJECT.
//                                                              مثال حقول الكائن: text name = "..."; text color = "#3498db";
//
// @container.portal=name  <body>  .end/container.portal    -> "بوابة/حاوية تنسيق" غرضها حمل نمط (style) عام
// @portal=name             <body>  .end/portal               -> نفس الشيء، بشكل مستقل. كلاهما ContainerKind::PORTAL.
//                                                               مثال: @portal=theme  style value="style://dark";  .end/portal
//
// @container.block=name    <body>  .end/container.block     -> كتلة واجهة جاهزة (شريط علوي/سفلي/زر...)
// @block=name              <body>  .end/block                -> نفس الشيء، بشكل مستقل. كلاهما ContainerKind::BLOCK.
//                                                               الاسم يحدّد نوع الكتلة، مثال:
//                                                               @block="top.bar"     ... .end/block
//                                                               @block="bottom.bar"  ... .end/block
//                                                               @block="btn"         ... .end/block
//
// object/portal/block الثلاثة تخضع لنفس قيود "البيانات النقية" الخاصة بـ container.data/container.table
// (بلا دوال وبلا حاويات متداخلة)، ويجوز استخدام عبارة 'style' بداخل أيٍّ منها (وليس فقط container.table).
//
// @container.sticker=name  <body>  .end/container.sticker  -> "ملصق" (sticker): بطاقة هوية بصرية جاهزة
// @sticker=name             <body>  .end/sticker             -> نفس الشيء، بشكل مستقل. كلاهما ContainerKind::STICKER.
//                                                               حقول حرة نموذجية (كلها اختيارية، عبر text/let):
//                                                                 text icon        = "img.png";        // الأيقونة
//                                                                 text colors      = "#7C5CFF,#00C2A8"; // colors()
//                                                                 text edges       = "rounded:16";       // edges()
//                                                                 text background  = "banner.png";       // background/banner
//                                                                 text transparent = "true";              // شفافية
//                                                                 text barrier     = "false";              // حاجز/قفل
//                                                                 text animation   = "fade-in:300ms";      // animation
//                                                                 text xml         = "layout.xml";         // xml()
//                                                                 text css         = "sticker.css";        // css()
//                                                               ثم بداخل نفس الملصق:
//                                                                 link to=otherContainer;   // links() -> ربط بلا نسخ
//                                                                 file path="license.rin";  // transition.file (مثال: ترخيص)
//                                                               ويقبل 'style' مثل object/portal/block تماماً.
//
// @container.chatbot=name  <body>  .end/container.chatbot  -> حاوية "روبوت محادثة" (chatbot). ContainerKind::CHATBOT.
// @chatbot=name             <body>  .end/chatbot             -> نفس الشيء، بشكل مستقل. كلاهما ContainerKind::CHATBOT.
//                                                               تسمح بمنطق حقيقي بداخلها (fun/دوال واستدعاءات)،
//                                                               تماماً كـ container/container.api، وليست مقيَّدة
//                                                               بقيود "البيانات النقية" (فهي بحاجة لتسجيل معالجات
//                                                               أحداث عبر onChat() ودوال Rin عادية).
//                                                               حقول متعارف عليها (عبر text/let عادي، اختيارية):
//                                                                 text model  = "rin-chat-1"; // اسم المحرّك/النموذج
//                                                                 text system = "...";        // تعليمات النظام
//                                                                 warp memory = {};           // سياق/ذاكرة حيّة
//                                                               دوال native مرتبطة (تعمل من أي مكان بذكر اسم الحاوية):
//                                                                 sendMessage(name, role, text) / botReply(name, text)
//                                                                 chatHistory(name) / lastChatMessage(name) / chatMessageCount(name)
//                                                                 clearChat(name)
//                                                                 attachToChat(name, role, fileRef, caption)
//                                                                 setChatTyping(name, true|false) / isChatTyping(name)
//                                                                 openChat(name) / closeChat(name)
//                                                                 onChat(name, "message"|"open"|"close"|"typing", fn)
//                                                               (انظر التوثيق الكامل أعلى دوال هذه الحاوية في .cpp)

enum class ObjectStyleFieldKind { Text, Image, File, Fonts, Background, Css3 };

enum class ContainerKind { PLAIN, PIPE, DATA, API, IMPORT, TABLE, DOC, OBJECT, PORTAL, BLOCK, STICKER, AUKT, CHATBOT };

// ---- AUKT: Automated Knowledge Tables (جداول المعرفة الآلية) ----
// @container.aukt=name  <body>  .end/container.aukt
// @AUKT=name             <body>  .end/AUKT
//   كلا الشكلين ينتجان نفس ContainerKind::AUKT. على عكس container.table/doc/object/portal/block/
//   sticker، فإن AUKT حاوية "مُجمِّعة" (composite) — لا تخضع لقيود validateDataContainerBody،
//   أي يُسمح بالتعشيش الكامل بداخلها تماماً كـ container العادية/Containers.Group. الغرض منها هو
//   تجميع عدة مفاهيم موجودة أصلاً معاً تحت مظلّة واحدة باسم/امتداد ملف مميّزين (.aak.rin):
//     - جدول(جداول) معرفة:      @table=... / @container.table=...   (row cells=[...]; style ...)
//     - قاموس/قواميس (NoSQL):   @doc=... / @container.doc=...        (document id=... fields={...})
//     - نمط عرض عام:            @portal=theme  style value="style://dark";  .end/portal
//     - مكتبة خطوط:             @Object=fonts  text family="Cairo"; text fallback="sans-serif"; .end/Object
//     - أيقونات:                @sticker=icons  text icon="table.png"; text colors="#3498db"; .end/sticker
//     - شريط أدوات قابل للتخصيص: @block="toolbar"  text items="new,delete,sort,filter,export"; .end/block
//   أي عدد وأي ترتيب من هذه الكتل الفرعية مقبول بداخل AUKT واحدة؛ 'style' مباشرة بداخل AUKT (خارج أي
//   جدول فرعي) تضبط النمط العام الافتراضي لكل الجداول/القواميس التابعة لها ما لم تُخصَّص محلياً.
//   الامتداد الموصى به لحفظ/تصدير حاوية AUKT هو "name.aak.rin" (بدل "name.rin" الافتراضي)؛ الترميز/
//   البنية الداخلية للملف تبقى نص Rin عادياً تماماً (لا صيغة ثنائية جديدة) — فقط الامتداد يُميِّزه
//   بصرياً وللتطبيق (يفتحه محرِّر AUKT المخصص تلقائياً بدل المحرِّر النصي العادي).

struct ContainerStmt : Stmt {
    std::string name; // قد تكون فارغة إن لم يُحدَّد اسم
    std::vector<StmtPtr> body;
    ContainerKind kind = ContainerKind::PLAIN;
};

// @Containers.Group=name  <body>  .end/Containers.Group
struct ContainerGroupStmt : Stmt {
    std::string name;
    std::vector<StmtPtr> body;
};

// @Volume=name  <body>  .end/Volume
struct VolumeStmt : Stmt {
    std::string name;
    std::vector<StmtPtr> body;
};

// Section=name  <body>  .end/Section
struct SectionStmt : Stmt {
    std::string name;
    std::vector<StmtPtr> body;
};

// Translations  <body: translation...>  .end/Translations
struct TranslationsStmt : Stmt {
    std::vector<StmtPtr> body;
};

// translation lang="ar" text="مرحبا";
struct TranslationStmt : Stmt {
    std::string lang;
    std::string text;
};

// link to=name;               -> ربط بلا نسخ عبر اسم الحاوية (كما كان)
// link id="X";                -> نفس الشيء، لكن الهدف يُحلّ عبر معرّف ربط عام (link.id) بدل الاسم؛
//                                 هذا هو الشكل الذي يجعل الربط يعمل عبر الملفات (rin/html/js/cpp)
//                                 لأن المعرّف "X" يبقى ثابتاً حتى لو اختلف اسم الحاوية من ملف لآخر.
struct LinkStmt : Stmt {
    std::string target; // فارغ إن استُخدم byId
    std::string byId;   // فارغ إن استُخدم target (link to=)
};

// link.id="X";  (بداخل جسم حاوية) -> يسجّل "X" كـ"معرّف ربط" عام (container.link.id) لهذه الحاوية،
// بحيث يمكن لاحقاً استهدافها من أي مكان عبر 'link id="X";' بدل تكرار اسمها، ويمكن لملفات خارج
// Rin (html/js/cpp) أن تُشير لنفس "X" عبر اتفاقية بسيطة (انظر tools/rin_link_index.py) لتُعتبر
// "مرتبطة" منطقياً بنفس الحاوية دون أن ينفّذها مفسّر Rin مباشرة.
struct LinkIdDeclStmt : Stmt {
    std::string id;
};

// tying with=name;
struct TyingStmt : Stmt {
    std::string target;
};

// merge with=name;
struct MergeStmt : Stmt {
    std::string target;
};

// installation name; / simplified installation name; / installation name format=zip;
struct InstallationStmt : Stmt {
    std::string target;
    bool simplified = false;
    std::string format; // فارغ = الصيغة النصية الافتراضية (.rin) ؛ "zip" = أرشيف zip حقيقي على القرص
};

// save; / save path="..."; / simplified save path="..."; / save format=png; / save path="..." format=zip;
struct SaveStmt : Stmt {
    ExprPtr path; // قد تكون فارغة (nullptr)
    bool simplified = false;
    std::string format; // فارغ = ".rin" نصي افتراضي ؛ "png" (حصراً لـ container.table/table) ؛ "zip"
};

// row cells=[v1, v2, ...];  -> يُضيف صفاً واحداً إلى الجدول الحالي (داخل container.table أو table فقط)
struct RowStmt : Stmt {
    ExprPtr cells; // يُتوقَّع أن يكون تعبير مصفوفة (ArrayExpr) لكن أي تعبير يُقيَّم إلى Value::ARRAY مقبول
};

// style value="style://<theme>";  -> يضبط نمط عرض الجدول الحالي (داخل container.table أو table فقط)
// الصيغة تتبع مخطط شبيه بالـ URI: "style://dark" / "style://light" / "style://grid" ...
struct StyleStmt : Stmt {
    ExprPtr value;
};

// document id="u1" fields={ name: "Ali", age: 30 };  -> يُدرج (أو يُحدّث إن كان الـ id موجوداً مسبقاً)
// مستنداً واحداً داخل حاوية NoSQL الحالية (container.doc أو doc فقط). 'fields' كائن/قاموس حر البنية
// (schema-less)، تماماً كمستندات JSON في قواعد البيانات اللاعلاقية.
struct DocumentStmt : Stmt {
    ExprPtr id;     // معرِّف المستند (نص)
    ExprPtr fields; // حقول المستند (map)
};

// file path="...";
struct FileStmt : Stmt {
    ExprPtr path;
};

// route method="GET" path="/users/1" status=200 body={...};  -> تُستخدم فقط داخل @container.api
struct RouteStmt : Stmt {
    ExprPtr method;
    ExprPtr path;
    ExprPtr status;
    ExprPtr body;
};

// @import "lib/data.og.rin";           -> يدمج كل عبارات المكتبة مباشرة داخل النطاق الحالي (بلا حاوية)
// @import "lib/data.og.rin" as data;   -> يسجّل الاستيراد كحاوية باسم 'data' (نفس آلية container.import)
//                                          بحيث يمكن لاحقاً ربطها بـ link/tying/merge كأي حاوية عادية.
// يُحلَّل المسار أولاً ضمن سجل المكتبات المدمجة داخل المفسّر (rin_stdlib_libs.h)، وإن لم يوجد
// يُقرأ كملف فعلي على القرص (نسبةً إلى basePath) — تماماً بكافة عمليات الملفات في اللغة.
struct ImportStmt : Stmt {
    ExprPtr path;      // مسار/اسم المكتبة (نص)
    std::string alias; // فارغ = دمج مباشر في النطاق الحالي
};

// ---- Loomtime rendering engine: view strands + reactive state (Warp) ----
// هذا امتداد إضافي بحت (additive) فوق لغة الحاويات أعلاه — لا يُعدِّل أي عقدة موجودة.
// يمنح RIN مفردات واجهة كاملة (Text/Image/Button/Card/Column/Row/Stack/...) بجانب
// container.object/portal/block/sticker الموجودة أصلاً (والتي تبقى صالحة تماماً كما هي).
//
// @view.<Kind>=name   key=expr; ...   [<@view... متداخلة>]   .end/view
//   مثال:
//     @view.Column=root
//       gap=16; padding=20;
//       @view.Text=title text="Welcome to Rin"; size=22; .end/view
//     .end/view
//
// warp name = expr;   -> خلية حالة تفاعلية (reactive state cell)، يُعاد تقييم أي وصلة
//   Strand تقرأ منها تلقائياً عبر محرّك Shuttle (انظر loom/rin_loom_shuttle.h) كلما تغيّرت،
//   سواء كان التغيير من محرِّر المستخدم (Hot Reload) أو وقت التشغيل (نقرة زر مثلاً).

// key=expr;  سمة واحدة داخل كتلة @view (القيمة أي تعبير RIN عادي: نص/رقم/متغيّر warp/نداء دالة)
struct ViewAttr {
    std::string key;
    ExprPtr value;
    int line = 0;
};

struct ViewStmt : Stmt {
    std::string name;                          // قد يكون فارغاً (Strand مجهول الاسم)
    std::string kindTag;                       // "Column" / "Text" / "Button" / ... أو وسم مخصَّص (Bolt plugin)
    std::vector<ViewAttr> attrs;
    std::vector<std::shared_ptr<ViewStmt>> children;
};

// warp name = expr;
struct WarpStmt : Stmt {
    std::string name;
    ExprPtr initializer;
};

// ---- Rin Loom: Theme (Pattern Book) declaration ----
// @theme=<Name>   key=expr; ...   .end/theme
//   مثال:
//     @theme=Midnight
//       active=true;
//       primary="#7C5CFF";
//       danger="#D14545";
//     .end/theme
//
// امتداد إضافي فوق Loomtime تماماً كـ @view/warp أعلاه: لا كتل متداخلة، فقط سمات key=expr
// مسطّحة. كل مفتاح إما اسم دور لوني دلالي (primary/secondary/success/danger/warning/info/
// neutral/surface/background/text/text_muted/border) بقيمة نصية "#RRGGBB"، أو المفتاح الخاص
// "active" (قيمة منطقية) الذي يجعل هذا الـTheme هو النشط فور تسجيله. انظر
// loom/rin_loom_tokens.h لمنطق التسجيل والتحليل (registerThemesFromProgram).
struct ThemeStmt : Stmt {
    std::string name;
    std::vector<ViewAttr> attrs; // يعاد استخدام ViewAttr (key/value/line) نفسه بدل بنية مكررة
};

// ---- Call-style object literal ----
// .object("user01")
//     name("ABOO");
//     age(19);
//     online(true);
//     image:("img.png");     -> الشكل المُنمَّط (typed) `field:(value)` مطابق وظيفياً لـ `field(value)`
//     number:();              -> بلا وسيطة => قيمة الحقل تكون nil
//     container.();            -> (اختياري) يربط هذا الكائن بسجل عام (objectRegistry في المفسّر)
//                                  بحيث يمكن الوصول إليه لاحقاً من أي مكان عبر نفس المعرّف، تماماً
//                                  كآلية link.id للحاويات، وهو ما يجعل view.print/object("user01")
//                                  قادراً على إيجاده حتى خارج النطاق الذي أُنشئ فيه.
// .end/object
//
// ملاحظة: هذا شكل مختلف تماماً عن `.object=text` (انظر أعلاه، لا يزال صالحاً بلا أي تغيير)؛
// التمييز بينهما يحدث في المحلل النحوي عبر التوكن التالي مباشرة بعد `.object`: '=' للشكل القديم،
// '(' لهذا الشكل الجديد.
//
// وقت التنفيذ: يُبنى كائن من نوع Value::MAP من كل أزواج (field, value) بالترتيب المكتوب، ويُعرَّف
// كمتغيّر باسم المعرّف (id) في النطاق الحالي (تماماً كـ let)، بحيث `print user01;` يعمل مباشرة بعد
// `.object("user01") ... .end/object` في نفس الملف/النطاق.
struct ObjectFieldCall {
    std::string name;   // اسم الحقل، مثال: "name", "age", "image", "number"
    bool typed = false;  // true عند الكتابة بصيغة `field:(value)` بدل `field(value)` (بلا فرق دلالي)
    ExprPtr value;        // قد تكون فارغة (nullptr) لصيغة `field();` بلا وسيطة => قيمة الحقل nil
    int line = 0;
};
struct ObjectLiteralStmt : Stmt {
    std::string id;                       // النص الذي مُرِّر إلى .object("...")
    std::vector<ObjectFieldCall> fields;
    bool linkToContainer = false;         // true إن ظهرت `container.();` داخل الجسم
};

// view.print/object(expr);
// معاينة حيّة (live preview) لكائن في الكونسول: يقبل expr إما نصاً (معرّف id سُجِّل مسبقاً عبر
// `container.();` داخل `.object("id") ... .end/object`) أو قيمة MAP مباشرة (مثال: نتيجة
// `.object(...)` مُعرَّفة كمتغيّر، أو أي قاموس آخر). لا يُنشئ كائناً جديداً بذاته؛ فقط يعرض ما هو
// موجود بالفعل بتهيئة بطاقة معاينة متعددة الأسطر، مسبوقة برمز 🖼️ يلتقطه RinConsoleFormatter.kt.
struct ViewPrintObjectStmt : Stmt {
    ExprPtr target;
};

} // namespace rin

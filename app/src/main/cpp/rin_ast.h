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
// print expr1, expr2, ... [sep=expr] [end=expr];
// السلوك الافتراضي (قيمة واحدة، بلا sep/end) مطابق تماماً للسابق: قيمة واحدة + سطر جديد "\n".
// 3 ميزات جديدة أضيفت فوق ذلك:
//   1) exprs: أكثر من قيمة مفصولة بفواصل في نفس أمر print الواحد (مثال: print "x=", x;)
//   2) sep: فاصل مخصص يُطبع بين كل قيمتين متتاليتين عند تعدّد القيم (افتراضياً مسافة واحدة " ")
//   3) end: ما يُطبع في نهاية السطر بدل "\n" الافتراضي — end="" يمنع السطر الجديد تماماً، فيسمح
//      بعدّة أوامر print متتالية تكمل بعضها على نفس السطر (مفيد لعدّادات/أشرطة تقدّم console)
// sep/end يُقيَّمان كتعبيرين عاديين (وليس حصراً حرفاً نصياً) لكن يجب أن يُقيَّما إلى STRING وقت
// التنفيذ، وإلا خطأ صريح؛ تماماً كبقية سمات key=value الأخرى في اللغة (document/row/route/save).
struct PrintStmt : Stmt {
    std::vector<ExprPtr> exprs;
    ExprPtr sep; // nullptr = افتراضي " "
    ExprPtr end; // nullptr = افتراضي "\n"
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
struct FunctionStmt : Stmt {
    std::string name;
    std::vector<std::string> params;
    std::shared_ptr<BlockStmt> body;
};
struct ReturnStmt : Stmt {
    ExprPtr value; // may be null
};

// ---- Data-container language statements (container / Containers.Group / Volume / Section ...) ----

// text name = "..."; -> إعلان قيمة من نوع نصي (text)
struct TextStmt : Stmt {
    std::string name;
    ExprPtr initializer;
};

// ---- حقول تنسيق/ستايل إضافية خاصة بالكائن (@container.object / @Object فقط) ----
// نفس فكرة 'text' (حقل حر باسم + قيمة نصية) لكن بستة أسماء مخصّصة لمفاهيم الستايل الشائعة:
//   txt        name = "...";  -> نص عرض (Text style)
//   img        name = "...";  -> مسار/رابط صورة (Image style)
//   object.file name = "...";  -> مسار ملف مرتبط بالكائن (Object File style) — بصيغة "object.file"
//                                  (وليس "file" وحدها) لتفادي التعارض مع الكلمة المحجوزة 'file'
//                                  الخاصة أصلاً بعبارة "file path=...;" داخل @container.import
//   Fonts      name = "...";  -> اسم/عائلة خط (Font style)
//   background name = "...";  -> قيمة خلفية: لون/تدرّج/مسار صورة (Background style)
//   css3       name = "...";  -> مقتطف CSS3 خام يُطبَّق على الكائن (CSS3 style)
// الحقول الستّة أعلاه مسموحة حصراً داخل @container.object/@Object (ليست عامة كـ 'style')، وتُخزَّن
// كبيانات حرة على الكائن تماماً كحقول 'text' العادية (يجب أن تكون قيمتها نصاً).
enum class ObjectStyleFieldKind { TXT, IMG, OBJECT_FILE, FONTS, BACKGROUND, CSS3 };
struct ObjectStyleFieldStmt : Stmt {
    ObjectStyleFieldKind kind;
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
enum class ContainerKind { PLAIN, PIPE, DATA, API, IMPORT, TABLE, DOC, OBJECT, PORTAL, BLOCK };

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

// link to=name;
struct LinkStmt : Stmt {
    std::string target;
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
// يُقرأ كملف فعلي على القرص (نسبةً إلى basePath) — تماماً كبقية عمليات الملفات في اللغة.
struct ImportStmt : Stmt {
    ExprPtr path;      // مسار/اسم المكتبة (نص)
    std::string alias; // فارغ = دمج مباشر في النطاق الحالي
};

} // namespace rin

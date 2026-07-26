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
struct PrintStmt : Stmt { ExprPtr expr; };
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
enum class ContainerKind { PLAIN, PIPE, DATA, API, IMPORT, TABLE, DOC };

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

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
enum class ContainerKind { PLAIN, PIPE, DATA, API, IMPORT };

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

// installation name; / simplified installation name;
struct InstallationStmt : Stmt {
    std::string target;
    bool simplified = false;
};

// save; / save path="..."; / simplified save path="...";
struct SaveStmt : Stmt {
    ExprPtr path; // قد تكون فارغة (nullptr)
    bool simplified = false;
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

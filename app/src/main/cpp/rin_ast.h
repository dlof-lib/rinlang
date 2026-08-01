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

enum class ObjectStyleFieldKind { Text, Image, File, Fonts, Background, Css3 };

enum class ContainerKind { PLAIN, PIPE, DATA, API, IMPORT, TABLE, DOC, OBJECT, PORTAL, BLOCK, STICKER, AUKT };

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

} // namespace rin

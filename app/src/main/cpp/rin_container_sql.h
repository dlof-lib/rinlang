#pragma once
#include <string>
#include <vector>
#include <stdexcept>

// ============================================================================================
// RIN CONTAINER SQL (اختصاراً RCSQL) — نظام استعلام مصغّر ومميّز خاص بلغة Rin، يُستخدَم للبحث/
// الفلترة/الفرز/التصفّح/التجميع/التعديل بالجملة داخل مجموعات مستندات NoSQL المُعرَّفة عبر
// @container.doc / @doc (انظر DocumentStmt في rin_ast.h وdocStore في rin_interpreter.h) — أي أن
// "بنية الحاويات" التي يُستعلَم عنها هنا هي حرفياً حاويات @ الموجودة أصلاً في اللغة (تعريف البيانات
// يبقى عبر @doc/@container.doc كالمعتاد؛ RCSQL لا يُعرِّف بيانات جديدة، فقط يستعلمها/يُشكّلها/يُعدِّلها).
//
// يمكن أيضاً "تعريف" استعلام RCSQL كحاوية @ مستقلة قابلة لإعادة الاستخدام عبر @sql/@container.sql
// (انظر ContainerKind::SQL وsqlViews في rin_interpreter.h) ثم استدعاؤه لاحقاً بقناعه (mask) بدل
// كتابة النص الخام في كل مرة — هذا ما تقصده "استدعاء RIN CONTAINER SQL بالأقنعة".
//
// الصياغة (EBNF مبسّطة) -- خمسة رموز تركيبية فقط مسموح بها، بالإضافة لحروف/أرقام المعرّفات:
//
//   query      := target ( "&" clause )*
//   target     := "#" IDENT                  -- قناع (mask) يُحلّ إلى اسم حاوية عبر containerMasks
//               | IDENT ( "/" IDENT )*        -- اسم حاوية مباشر، أو مسار Group/حاوية متداخل
//   clause     := predicate                   -- شرط فلترة عادي (AND مع بقية الشروط)
//               | group                       -- مجموعة منطقية: or(...) / and(...) / not(...)
//               | modifier                    -- order/limit/offset/select/distinct (انظر أدناه)
//   group      := ("or"|"and") "(" predicate_or_group ("&" predicate_or_group)* ")"
//               | "not" "(" predicate_or_group ")"           -- نفي شرط/مجموعة فرعية واحدة
//   predicate_or_group := predicate | group                  -- تداخل حرّ (or/and/not داخل بعضها)
//   predicate  := field ":" OP "(" ARG? ")"
//   field      := IDENT ( "/" IDENT )*        -- يدعم حقول map متداخلة: address/city
//   OP         := "eq" | "ne" | "ieq" | "gt" | "gte" | "lt" | "lte"
//               | "has" | "like" | "starts" | "ends" | "exists" | "missing"
//   ARG        := IDENT                       -- وسيط واحد فقط (لا فاصلة: ',' ليست من الرموز المسموحة)
//
// المُعدِّلات (modifiers) -- عبارات بنفس شكل predicate تماماً لكن أسماء حقول محجوزة تُعيد تشكيل
// النتيجة بدل فلترتها (تُستثنى من شروط AND/OR/NOT الفعلية):
//   order:asc(field)   / order:desc(field)   -- فرز (يمكن تكرارها لعدّة مفاتيح فرز متتالية)
//   limit:eq(N)                              -- أقصى عدد نتائج
//   offset:eq(N)                             -- تخطّي أول N نتيجة (بعد الفرز)
//   select:has(field)                        -- إسقاط: إبقاء حقل واحد محدد فقط (تُكرَّر لعدّة حقول)
//   distinct:eq(field)                       -- إزالة التكرار حسب حقل (تُكرَّر لبناء مفتاح تفريد مركّب)
//
// ترتيب التنفيذ الفعلي دوماً: فلترة (AND/OR/NOT) -> distinct -> order -> offset -> limit -> select.
//
// IDENT := تسلسل غير فارغ من: حروف/أرقام لاتينية، '_' '.' '-'، أو أي بايت UTF-8 عربي/غير-ASCII
//
// مثال: "#usersMask & role:eq(admin) & age:gte(18) & address/city:eq(Cairo)"
// مثال (OR + فرز + حد): "users & or(role:eq(admin) & role:eq(owner)) & order:desc(age) & limit:eq(5)"
// مثال (NOT): "users & not(role:eq(banned))"
// مثال (تركيب منطقي متداخل -- (A AND B) OR (C AND D)):
//   "users & or(and(role:eq(admin) & active:eq(true)) & and(role:eq(owner) & active:eq(true)))"
// ============================================================================================

namespace rin::sql {

// شرط فلترة واحد (field OP (arg))، أو مجموعة منطقية (isGroup == true): OR (يكفي تطابق شرط فرعي
// واحد)، AND (يجب تطابق كل الشروط الفرعية -- صريحة لأجل التداخل داخل or(...)/not(...))، أو NOT
// (نفي الشرط الفرعي الوحيد؛ subs.size() == 1 دوماً لمجموعات NOT الصادرة من parse()).
struct Predicate {
    bool isGroup = false;
    std::string groupOp;              // "or" | "and" | "not" -- فقط إن isGroup
    std::vector<Predicate> subs;      // الشروط الفرعية داخل المجموعة؛ فقط إن isGroup

    std::string field; // قد يحوي '/' لحقل متداخل، محفوظاً كما كُتب: "address/city" -- فقط إن !isGroup
    std::string op;    // eq|ne|ieq|gt|gte|lt|lte|has|like|starts|ends|exists|missing -- فقط إن !isGroup
    std::string arg;   // نص الوسيط الخام (قد يكون فارغاً لعمليات exists/missing) -- فقط إن !isGroup
};

// مفتاح فرز واحد ضمن order:asc(field)/order:desc(field).
struct SortKey {
    std::string field;
    bool desc = false;
};

// استعلام RCSQL كامل بعد التحليل.
struct Query {
    bool targetIsMask = false;             // true إن كُتب الهدف كـ "#mask"
    std::string targetMask;                // اسم القناع بلا '#' (فقط إن targetIsMask)
    std::vector<std::string> targetPath;   // أجزاء المسار المفصولة بـ '/' (فقط إن !targetIsMask)

    std::vector<Predicate> predicates;     // شروط فلترة فعلية فقط (بلا المُعدِّلات) -- AND فيما بينها

    std::vector<SortKey> orderBy;          // بترتيب الكتابة؛ فارغ = بلا فرز (ترتيب الإدخال كما هو)
    long limit = -1;                       // -1 = بلا حد
    long offset = 0;                       // 0 = بلا تخطٍّ
    std::vector<std::string> selectFields; // فارغ = كل الحقول؛ وإلا إسقاط لهذه الحقول فقط
    std::vector<std::string> distinctFields; // فارغ = بلا إزالة تكرار؛ عدّة حقول = مفتاح تفريد مركّب
};

// خطأ تركيبي (syntax) في نص RCSQL -- بما في ذلك استخدام أي رمز خارج القائمة المسموحة، أو مُعدِّل
// (order/limit/offset/select/distinct) بشكل غير صالح (عملية خاطئة، وسيط غير رقمي لـ limit/offset...).
struct SqlSyntaxError : std::runtime_error {
    explicit SqlSyntaxError(const std::string& msg) : std::runtime_error(msg) {}
};

// يحلّل نص استعلام RCSQL إلى Query جاهز للتنفيذ. يرمي SqlSyntaxError عند أي خلل تركيبي.
// دالة نقية بلا أي اعتماد على Interpreter/Value -- تحليل الرموز فقط؛ تحليل الهدف (مطابقة القناع/
// المسار الفعلي) وتنفيذ الشروط الفعلية على المستندات تتم في rin_interpreter.cpp (تحتاج docStore/
// containerMasks/valuesEqual، غير متوفرة هنا عمداً حفاظاً على استقلالية هذا الملف واختباره بمعزل).
Query parse(const std::string& text);

} // namespace rin::sql

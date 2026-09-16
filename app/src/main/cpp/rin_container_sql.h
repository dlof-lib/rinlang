#pragma once
#include <string>
#include <vector>
#include <stdexcept>

// ============================================================================================
// RIN CONTAINER SQL (اختصاراً RCSQL) — نظام استعلام مصغّر ومميّز خاص بلغة Rin، يُستخدَم للبحث/
// الفلترة داخل مجموعات مستندات NoSQL المُعرَّفة عبر @container.doc / @doc (انظر DocumentStmt في
// rin_ast.h وdocStore في rin_interpreter.h) — أي أن "بنية الحاويات" التي يُستعلَم عنها هنا هي حرفياً
// حاويات @ الموجودة أصلاً في اللغة (تعريف البيانات يبقى عبر @doc/@container.doc كالمعتاد؛ RCSQL لا
// يُعرِّف بيانات جديدة، فقط يستعلمها).
//
// يمكن أيضاً "تعريف" استعلام RCSQL كحاوية @ مستقلة قابلة لإعادة الاستخدام عبر @sql/@container.sql
// (انظر ContainerKind::SQL وsqlViews في rin_interpreter.h) ثم استدعاؤه لاحقاً بقناعه (mask) بدل
// كتابة النص الخام في كل مرة — هذا ما تقصده "استدعاء RIN CONTAINER SQL بالأقنعة".
//
// الصياغة (EBNF مبسّطة) -- خمسة رموز تركيبية فقط مسموح بها، بالإضافة لحروف/أرقام المعرّفات:
//
//   query      := target ( "&" predicate )*
//   target     := "#" IDENT                  -- قناع (mask) يُحلّ إلى اسم حاوية عبر containerMasks
//               | IDENT ( "/" IDENT )*        -- اسم حاوية مباشر، أو مسار Group/حاوية متداخلة
//   predicate  := field ":" OP "(" ARG? ")"
//   field      := IDENT ( "/" IDENT )*        -- يدعم حقول map متداخلة: address/city
//   OP         := "eq" | "ne" | "gt" | "gte" | "lt" | "lte" | "has" | "like"
//   ARG        := IDENT                       -- وسيط واحد فقط (لا فاصلة: ',' ليست من الرموز المسموحة)
//   IDENT      := تسلسل غير فارغ من: حروف/أرقام لاتينية، '_' '.' '-'، أو أي بايت UTF-8 عربي/غير-ASCII
//
// مثال: "#usersMask & role:eq(admin) & age:gte(18) & address/city:eq(Cairo)"
// مثال (بلا قناع، عبر مسار Group/حاوية متداخلة): "store/users & active:eq(true)"
// ============================================================================================

namespace rin::sql {

// شرط فلترة واحد: field OP (arg) -- كلها AND فيما بينها بترتيب الكتابة (بلا OR في هذه النسخة).
struct Predicate {
    std::string field; // قد يحوي '/' لحقل متداخل، محفوظاً كما كُتب: "address/city"
    std::string op;    // eq | ne | gt | gte | lt | lte | has | like
    std::string arg;   // نص الوسيط الخام (قد يكون فارغاً لعمليات لا تحتاج وسيطاً لاحقاً)
};

// استعلام RCSQL كامل بعد التحليل.
struct Query {
    bool targetIsMask = false;             // true إن كُتب الهدف كـ "#mask"
    std::string targetMask;                // اسم القناع بلا '#' (فقط إن targetIsMask)
    std::vector<std::string> targetPath;   // أجزاء المسار المفصولة بـ '/' (فقط إن !targetIsMask)
    std::vector<Predicate> predicates;     // بترتيب الكتابة، AND فيما بينها
};

// خطأ تركيبي (syntax) في نص RCSQL -- بما في ذلك استخدام أي رمز خارج القائمة المسموحة.
struct SqlSyntaxError : std::runtime_error {
    explicit SqlSyntaxError(const std::string& msg) : std::runtime_error(msg) {}
};

// يحلّل نص استعلام RCSQL إلى Query جاهز للتنفيذ. يرمي SqlSyntaxError عند أي خلل تركيبي.
// دالة نقية بلا أي اعتماد على Interpreter/Value -- تحليل الرموز فقط؛ تحليل الهدف (مطابقة القناع/
// المسار الفعلي) وتنفيذ الشروط الفعلية على المستندات تتم في rin_interpreter.cpp (تحتاج docStore/
// containerMasks/valuesEqual، غير متوفرة هنا عمداً حفاظاً على استقلالية هذا الملف واختباره بمعزل).
Query parse(const std::string& text);

} // namespace rin::sql

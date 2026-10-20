# CHANGES_LINALG_ALGO.md — بيئة المهندسين: الجبر الخطي وتطوير الخوارزميات

دمج أرشيفات number وtoken وscope_guarantees في الشجرة الرئيسية (بالترتيب: scope ← token ← number؛ كل أرشيف يحتوي سابقه في الملفات المشتركة)، ثم إضافة **76 دالة** جديدة بلا تغيير في أي دالة قائمة.

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_extra_natives10.cpp` | جديد: 53 دالة `mat*`/`vec*` (docs/LINALG.md) |
| `app/src/main/cpp/rin_extra_natives11.cpp` | جديد: 23 دالة `algo*` (docs/ALGO.md) |
| `rin_interpreter.cpp/.h` | تسجيل `registerNativesExtra10/11` وتضمين الملفين |
| `rinc.cpp`، `RinSyntax.kt`، `syntaxes/rin-keywords.json` | الأسماء الجديدة |
| `scripts/intellisense_docs.json`، `editor/rin_intellisense_data.h` | التوثيق (والملف مُعاد توليده بالمولّد) |
| `docs/LINALG.md`، `docs/ALGO.md` | الوثائق |
| `tests/verification/linalg_algo.rin/.expected` | الاختبار (146 سطراً، قيم محقَّقة مقابل NumPy + خصائص جبرية) |

إعادة استعمال لا إعادة بناء: التمثيل = مصفوفات Rin العادية؛ جمع Neumaier وتساوي `numberApprox` وتنسيق `numberFormat` من مفهوم number؛ `algoPolyFit/algoNewton/algoPolyRoots` فوق LU/SVD/القيم الذاتية نفسها.

ملاحظات: `show`/`row`/`ok`/`num` أسماء محجوزة في Rin (لا تصلح أسماء دوال)؛ الدوال الأصلية لا تُمرَّر بالاسم كقيم.

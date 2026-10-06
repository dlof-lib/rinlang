# CHANGES_INDSIN_RIN — indsin يعتمد على لغة Rin

انظر `docs/indsin_rin.md`. الملخص: قيم خصائص `@view` تُقيَّم بمفسّر Rin الحقيقي (متغيرات، دوال، مقارنات، منطق، فهرسة،
Set...) + مكتبة `indsin.*` تعرض نظام التصميم لكود Rin + حارس نقاء + توافق خلفي كامل بلا مضيف.

## تحقق
rintests 83/83 · wesscode_tests 42/42 · verification 52/53 (الفرق `input_forms` قديم) · الجديد test_indsin_rin_bridge 47/47 ·
اختبارات indsin القائمة الـ14 (actions banner button element_merge export icons live_runtime missing_components oop_bind
overlay sizing system tokens ui_library_expansion) كلها ناجحة · المثال يعمل. بُني على سطح المكتب (g++)؛ لم يُبنَ APK/WASM.

## ملاحظة دمج
مبنيّ فوق `rinlang-changed-files.zip` (الشروط + Set) مطبَّقاً على `rinlang-main_full.zip`. الملفات المعدّلة في المفسّر:
`rin_interpreter.h` (تصريحات عامة جديدة) و`rin_interpreter.cpp` (سطر `#include "rin_expr_host.cpp"` فقط).

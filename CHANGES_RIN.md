# تعديلات إصلاح أخطاء لغة Rin

## المحرك (C++)
- `cli/linux/src/main.cpp`: `runSource` يتحقق الآن من `interp.hadError()` — سابقاً كان `rin run` يعيد 0 و`rin test` يعرض PASS حتى عند فشل التنفيذ.
- `app/src/main/cpp/rin_interpreter.cpp`:
  - تنفيذ فعلي لكامل واجهة `container.*` (exists/kind/size/info/fields/fieldNames/fieldType/snapshot/parent/childrenOf/siblings/ancestors/descendants/path/depth/roots/leaves/byKind/find/findField/set/get/has/renameField/deleteField/mergeFields/clearFields/count/current/stats).
  - تنفيذ واجهة candle كاملة (light/candleExists/candleTargets/candleSources/candleCount/candleInfo/candleRelation/candleByRelation/candleChain/candleDepth/candleRoots/candleLeaves/candleTree/extinguish/extinguishAll).
  - السماح بـ `style` مباشرة داخل `@AUKT`.
  - استيراد نسبي (`./` و`../`) يُجرَّب أيضاً نسبةً إلى مجلد الملف المستورِد.
- `app/src/main/cpp/rin_interpreter.h`: عضو `candleEdges` + `#include <tuple>`.
- `app/src/main/cpp/rin_stdlib_libs.h`: مزامنة النسخة المضمَّنة من `data.og.rin` (range -> sequence). ملاحظة: `@import` يفضّل النسخة المضمَّنة على ملف `lib/` في القرص.

## المكتبات والأمثلة والاختبارات
- `lib/relyRIN.og.rin`: مزامنة ملف القرص مع النسخة المضمَّنة الفعلية.
- `lib/data.og.rin` و`linguist-submission/samples/{data,math}.og.rin`: إزالة تعارض الاسم مع دوال مبنية (range / lerp).
- `examples/loops_pro.rin`, `tests/verification/loops_pro.rin`: استخدام for/while الحقيقية بدل each/repeat/loop.
- `examples/relyRIN_live_preview.rin`, `examples/relyRIN_indsin_container_demo.rin`: استخدام أسماء relyRIN الفعلية.
- `examples/mask.rin`, `examples/container_indsin_api_demo.rin`, `examples/use_from_demo/*`, `examples/make_unit_real/main.rin`: إصلاح صياغة/تعريفات ناقصة.
- `tests/verification/container_advanced.rin`, `number_builtins.rin`: اختبارات تنشئ بياناتها وتستخدم دوالاً موجودة فعلاً.

## قيود معروفة (لم تُعدَّل)
- `mask=` على عناصر `@view/@element` لا يُسجَّل في أي سجل أقنعة (يعمل فقط لـ container/group/volume).

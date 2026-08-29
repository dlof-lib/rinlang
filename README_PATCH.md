# Rin Loom — Missing Components Patch

انسخ هذه الملفات فوق نفس المسارات في نسختك من rinlang-main.

## الملفات المعدّلة
- app/src/main/cpp/loom/rin_loom_strand.h   — StrandKind جديدة + أسماء الوسوم
- app/src/main/cpp/loom/rin_loom_layout.h   — Layout لكل مكوّن + إصلاح جذري: layoutLinear
  أصبح two-pass flex (كان Spacer يبتلع كل المساحة ويترك الأخوة بعده بعرض صفر)
- app/src/main/cpp/loom/rin_loom_paint.h    — دوال رسم مخصّصة لكل مكوّن جديد
- app/src/main/cpp/loom/rin_loom_tokens.h   — أدوار a11y + أسماء a11y للمكوّنات الجديدة
- docs/loomtime/RIN_LOOM_TOKENS.md          — توثيق محدَّث، يُغلق نقطة "Container naming collision"

## ملفات جديدة
- tools/test_loom_missing_components.cpp    — 39 فحصًا حقيقيًا (بُني وشُغِّل فعليًا، كلها PASS)
- samples/loom_missing_components_demo.rin  — تطبيق تجريبي كامل قابل للتشغيل عبر loomc

## المكوّنات المضافة
Box (=Container/Panel/Frame)، Grid، Wrap، Spacer، Badge، Progress، Checkbox، Switch،
Avatar، Input، TextArea، Dialog/Modal، Tabs/TabItem، Tooltip.

## البناء والتشغيل (من app/src/main/cpp)
```
g++ -std=c++17 -I. -Iloom ../../../../tools/test_loom_missing_components.cpp \
  rin_lexer.cpp rin_parser.cpp rin_interpreter.cpp rin_http.cpp \
  diagnostics/diagnostic.cpp diagnostics/diagnostic_engine.cpp \
  diagnostics/diagnostic_renderer.cpp diagnostics/source_manager.cpp -lz \
  -o test_loom_missing_components
./test_loom_missing_components
```
تحقّقتُ أيضًا أن كل اختبارات Loom القديمة (button/banner/sizing/tokens) ما زالت تنجح 100% بعد
هذا التعديل (لا انكسار في backward compatibility).

## ملاحظة نطاق صادقة
Dialog/Tooltip يحسبان صندوقهما فقط (بلا محرّك overlay/تموضع حقيقي بعد) — التموضع المتمركز على
الشاشة يتم بتغليفها داخل @view.Stack align="center" valign="center"، بنفس النمط الموثّق أصلاً
لـ Stack. Tabs/TabItem تعيد استخدام state="selected" الموجود أصلاً في Button، ولم تُخترع آلية
"active tab" منفصلة.

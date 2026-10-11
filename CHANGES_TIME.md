# CHANGES_TIME.md — مفهوم `time`
- جديد: `app/src/main/cpp/rin_extra_natives7.cpp` (`time()` و36 دالة `time.*`)، يُضمَّن في نهاية `rin_interpreter.cpp` فيدخل كل أهداف البناء.
- `rin_interpreter.h/.cpp`: تصريح وتسجيل `registerNativesExtra7`، واستثناء `time` من منع التسمية بدالة مدمجة، وحارس في `invokeCallee` يجعل تعريف المستخدم يغلب.
- المحرّر: كلمات `time.*` في `RinSyntaxHighlighter.kt` و`syntaxes/rin-keywords.json`، و5 مقتطفات في `RinContainerTags.kt`.
- توثيق: `docs/TIME.md`. اختبار: `tests/verification/time_basic.rin/.expected`. مثال: `examples/time_demo.rin`.

## دعم indsin
- `indsin/rin_indsin_rinbridge.h`: `time.sleep/measure/config` تُرفض داخل الخصائص (بقية `time.*` نقية).
- `indsin/rin_indsin_c_api.cpp/.h`: `rin_indsin_session_tick` يحدّث `warp clock` كل ثانية ويضيف `"ticking":true`.
- `IndsinPreviewActivity.kt`: يكرّر tick كل ثانية عند `"ticking":true`.
- اختبار `tests/tools/test_indsin_time.cpp` ومثال `examples/samples/indsin_time_demo.rin`.

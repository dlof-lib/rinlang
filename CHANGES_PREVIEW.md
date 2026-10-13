# CHANGES_PREVIEW.md — مكتبة previewkit (معاينة حيّة لـ PDF / Markdown / TypeScript)

## جديد
- `lib/previewkit.og.rin` (+ نسخة مضمّنة في `rin_stdlib_libs.h` فيعمل `@import "lib/previewkit.og.rin"` على أندرويد/ويب بلا ملف على القرص).
- native جديد `zlibInflate(data, maxBytes?)` في `rin_interpreter.cpp`: فك zlib/gzip بلا حجم متوقَّع (ضروري لـ PDF)؛ `nil` عند التلف/تجاوز السقف.
  أُضيف اسمه إلى قوائم المحرّر: `RinSyntax.kt` و`rinc.cpp` (نسختان) و`syntaxes/rin-keywords.json`.
- `docs/previewkit.md`، `tests/previewkit_tests.rin` (22 اختباراً)، `examples/previewkit_demo.rin`، وسطور فهرسة في `docs/README.md` و`docs/standard-library.md` و`README.md`.

## إصلاحات في ملفات موجودة
- `lib/relyRIN.og.rin` (+ نسخته المضمّنة): أي رابط Markdown `[نص](رابط)` كان يسقط بخطأ «'indexOf' expects 2 argument(s) but got 3» — استُبدل بـ `relyFind`.
- `RinLibrary.kt`: أُضيفت previewkit إلى سجل المكتبات المدمجة في شاشة "المكتبات" في RinStudio (فئة UI).
- **Run يفتح المعاينة تلقائياً**: عند الضغط على Run وفي المحرّر ملف `.md`/`.markdown`/`.pdf`/`.ts`/`.tsx`/`.mts` يُحفظ الملف ثم تُفتح `FilePreviewActivity` (WebView) التي تولّد الصفحة بـ `pvPreview` وتراقب الملف كل ثانية فتُحدَّث عند كل حفظ مع الإبقاء على موضع التمرير. (`FilePreviewActivity.kt` جديد، تعديل `MainActivity.runProgram`، تسجيل في `AndroidManifest.xml`، نصوص `strings_preview.xml` بأربع لغات.)
  المسارات نسبية لجذر المشروع (عزل المحرّك)، والمخرَج في `.rin_preview/` (مخفي من قوائم الملفات: تعديل `ProjectManager.kt`).

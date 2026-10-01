# تطوير مخرجات كونسول Rin

## ملفات جديدة (app/src/main/java/com/dlof/rinlang)
- `RinDiagnosticRenderer.kt` — تقرير تشخيص بأسلوب rustc: رأس `error[E0001]`، موقع `--> file:line:col`، مقطع المصدر مع أسهم `^^^` تحت الموضع الدقيق، ثم reason/expected/found/note/help/suggestion/caused by. أعمدة 1-indexed ونهاية حصرية مطابقة لـ `SourceLocation` في المحرّك. Tab يُوسَّع إلى 4 مسافات، والسهم مقيَّد بنهاية السطر، والمدى متعدد الأسطر يعطي سهماً واحداً.
- `RinOutputReport.kt` — `RinOutputSummary` (عدّادات + عنوان عربي)، `LogLevel`/`atLeast()` للتصفية، `collapseRepeats()` (×N)، `truncateMiddle()` للمخرجات الضخمة، و`RinConsoleExport` (نص عادي بأعمدة / Markdown بسياج يتكيف مع الـ backticks).

## تعديل (RinConsoleFormatter.kt) — إضافي بالكامل، لا يكسر أي مستهلك
- `RinLogLine` يكتسب `errorLine` و`indent` و`repeat` بقيم افتراضية؛ `text` لم يتغير.
- استخراج رقم السطر من `[Error line N]:` لدعم «اذهب إلى السطر».
- حفظ إزاحة السطر الأصلية (Tab = 4).
- `formatBytes` بـ `Locale.ROOT` (كان يُنتج أرقاماً هندية/فاصلة «٫» على أجهزة عربية) ولا يطبع أحجاماً سالبة.

## اختبارات (app/src/test)
`RinDiagnosticRendererTest` و`RinConsoleOutputTest` (JUnit، بلا JNI).
يلزم: `testImplementation("junit:junit:4.13.2")` و`testImplementation("org.json:json:20240303")`.

## ربط الواجهة (RinJobAdapter.kt) — منفَّذ
- تبويب Output: عنوان ملخّص ملوَّن للتشغيل المنتهي («اكتمل التشغيل بنجاح — 12 سطراً» / «فشل التشغيل — خطأ واحد (السطر 4)»)، ودمج الأسطر المكرّرة مع شارة `×N`، ومسافة بادئة للأسطر المزاحة.
- تبويب Diagnostics: تقرير rustc الكامل (مقطع المصدر + الأسهم) بدل سطرين نصيين.
- نافذة Details: مقطع المصدر مع الأسهم، وتبقى حقول reason/expected/hints بتسمياتها المترجمة كما كانت.
- نسخ Markdown: يضيف التقرير الكامل داخل سياج كود يتكيّف مع الـ backticks.
- حماية: مقطع المصدر يُعرض فقط لأخطاء البرنامج الرئيسي (`file == "<input>"`)؛ أخطاء الملفات المستوردة لا تُقرن بمصدر خاطئ.

## تنبيه
لم تُترجَم الشيفرة ولم تُشغَّل الاختبارات في بيئة الإنشاء (لا مترجم Kotlin هناك). شغّل `./gradlew testDebugUnitTest` ثم ابنِ التطبيق قبل الاعتماد عليها.

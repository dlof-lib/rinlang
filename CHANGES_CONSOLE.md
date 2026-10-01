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

## مخرجات الـ container المميَّزة
- `RinContainerOutput.kt` (جديد): `ContainerFamily` (17 عائلة: pipe/data/api/import/table/doc/object/portal/block/sticker/chatbot/make/sql + Group/Volume/Section + عامة) بأيقونة وتسمية عربية ولون مميّز لكل عائلة؛ `RinContainerParser` يتعرّف على أسطر `<icon> container.X = name` و`✅|◽ .end/X (name) [تحتوي: ...]` بنفس صيغ المفسِّر الحرفية؛ `RinContainerTree` يبني شجرة متداخلة (مطابقة بالوسم والاسم، وحاوية لم تُغلق بسبب خطأ تبقى ظاهرة بتحذير) و`toTreeText()` لنسخة نصية شجرية.
- `RinLogLine.container` (حقل إضافي): يحمل علامة الفتح/الإغلاق.
- أسطر فتح الحاويات تُصنَّف الآن `STRUCTURE` دائماً (كان `container.doc` يُصنَّف DOC_INSERT و`container` العامة IMPORT، فيظهر حدثها في تبويب Events بنوع خاطئ).
- `RinJobAdapter`: كل حاوية تُعرض ككتلة بإطار وخلفية بلون عائلتها، رأس فيه الأيقونة والاسم وعدّاد الأسطر، سطر «تحتوي: ...»، ومحتوى متداخل قابل للطيّ بالنقر (الكبيرة > 40 سطراً تبدأ مطويّة). أثناء البحث يعود العرض مسطّحاً.
- ملخّص التشغيل صار يذكر الحاويات: «اكتمل التشغيل بنجاح — 12 سطراً في 3 حاويات».
- `collapseRepeats` لا يدمج أسطر الحاويات أبداً (فتح/إغلاق متكرر داخل حلقة يبقى كتلاً منفصلة).

## ربط الواجهة (RinJobAdapter.kt) — منفَّذ
- تبويب Output: عنوان ملخّص ملوَّن للتشغيل المنتهي («اكتمل التشغيل بنجاح — 12 سطراً» / «فشل التشغيل — خطأ واحد (السطر 4)»)، ودمج الأسطر المكرّرة مع شارة `×N`، ومسافة بادئة للأسطر المزاحة.
- تبويب Diagnostics: تقرير rustc الكامل (مقطع المصدر + الأسهم) بدل سطرين نصيين.
- نافذة Details: مقطع المصدر مع الأسهم، وتبقى حقول reason/expected/hints بتسمياتها المترجمة كما كانت.
- نسخ Markdown: يضيف التقرير الكامل داخل سياج كود يتكيّف مع الـ backticks.
- حماية: مقطع المصدر يُعرض فقط لأخطاء البرنامج الرئيسي (`file == "<input>"`)؛ أخطاء الملفات المستوردة لا تُقرن بمصدر خاطئ.

## تنبيه
لم تُترجَم الشيفرة ولم تُشغَّل الاختبارات في بيئة الإنشاء (لا مترجم Kotlin هناك). شغّل `./gradlew testDebugUnitTest` ثم ابنِ التطبيق قبل الاعتماد عليها.

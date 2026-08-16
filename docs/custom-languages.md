# اللغات المخصصة في Rin — من فكرة إلى لغة رسمية

يتيح Rin الآن للمبرمجين إنشاء **لغات برمجية حقيقية خاصة بهم** فوق منصة Rin نفسها، بنفس بنية أي
لغة برمجية احترافية: محلّل لفظي (Lexer) ← محلّل تركيبي (Parser) ← مفسّر (Interpreter) و/أو مولّد
كود (CodeGen) — كل مرحلة في ملفها الخاص، وليس كل شيء مكدّساً في ملف واحد.

## 1. المكوّنات

| المكوّن | الموقع | الدور |
|---|---|---|
| `lib/langkit.og.rin` | مكتبة مدمجة في مفسّر Rin نفسه (`app/src/main/cpp/rin_stdlib_libs.h`) | اللبنات المشتركة: تصنيف محارف، Token/AST، مؤشّر Parser، أخطاء موحّدة (`langError`)، ونمط Result آمن (`ok`/`err`/`isOk`) |
| `templates/customlang/` | مجلد قالب في المستودع | القالب الأصلي الذي يُنسَخ منه كل مشروع لغة جديد |
| `examples/customlang/calc/` | مثال كامل يعمل فعلياً | لغة "CalcLang" (متغيرات + شروط + حساب) — دليل عملي مُختبَر على مفسّر Rin الحقيقي |
| `CustomLanguageTemplates.kt` / `CustomLanguageProjectScaffolder.kt` | تطبيق أندرويد | ينشئ مشروع لغة جديد من داخل IDE (نسخ القالب + استبدال الاسم/المعرّف/الامتداد) |
| `CustomLanguageManifest.kt` | تطبيق أندرويد | قراءة/كتابة `manifest.json` لمشروع لغة |
| `CustomLanguageRegistry.kt` | تطبيق أندرويد | فهرس محلي: "أي لغة تخصّ امتداد الملف هذا، وأين مشروعها؟" |
| `CustomLanguageSyntaxLoader.kt` | تطبيق أندرويد | يحمّل `syntax.rinsyntax.json` ويبني تلوين صياغة للمحرر |
| `LanguageMarketplacePublisher.kt` | تطبيق أندرويد | يحزم مشروع اللغة وينشره كـ`RinExtension` بنوع `language` |

## 2. بنية أي مشروع لغة (وليس ملفاً واحداً)

```
اسم_لغتك/
├── manifest.json
├── Lexer.rin           // مصدر -> tokens
├── Parser.rin           // tokens -> AST      (@import "./Lexer.rin";)
├── Interpreter.rin       // ينفّذ AST مباشرة    (@import "./Parser.rin";)
├── CodeGen.rin            // AST -> كود Rin حقيقي (@import "./Parser.rin";)
├── run.rin                 // نقطة التشغيل     (@import "./Interpreter.rin"; @import "./CodeGen.rin";)
├── syntax.rinsyntax.json   // تلوين الصياغة في المحرر
└── examples/
    └── hello.<ext>
```

كل مشروع لغة هو في جوهره **مشروع Rin عادي** (`ProjectManager.createProject`)، فيحصل تلقائياً على
`basePath` خاص به — تماماً كباقي مشاريع Rin. لهذا تُستخدم مسارات `@import` بصيغة `"./Lexer.rin"`
(نسبية لجذر المشروع نفسه، وليس `"../"` الممنوعة أمنياً خارج basePath)، بينما `lib/langkit.og.rin`
يُستورَد باسمه المجرّد `@import "langkit";` لأنه مكتبة **مدمجة** في المفسّر، متاحة من أي مشروع فوراً
بلا نسخ ملفات.

## 3. من فكرة إلى لغة تعمل

1. من IDE: **مشروع جديد ← لغة مخصصة جديدة** يستدعي `CustomLanguageProjectScaffolder.createLanguageProject(...)`.
2. عدّل `Lexer.rin` (كلمات مفتاحية ورموز) و`Parser.rin` (قواعد نحوية) حسب تصميم لغتك.
3. أعطِ لعقد AST الجديدة معنى في `Interpreter.rin` و/أو `CodeGen.rin`.
4. حدّث `syntax.rinsyntax.json` لتلوين عناصر لغتك الجديدة.
5. شغّل `run.rin` — سترى تنفيذاً مباشراً وكود Rin مُولَّداً معاً.

⚠️ **ملاحظة تصميم مهمة (Result آمن):** `has()`/`keys()` في Rin يفشلان إن مُرِّرت لهما قيمة ليست
خريطة (رقم/نص/منطقي خام). لذا أي دالة قد يكون ناتجها الناجح قيمة خام (`evalExpr`, `genExpr`) يجب أن
تُعيد دوماً `ok(value)` أو `err(langErrorObj)` من `langkit.og.rin` — خريطة مضمونة دوماً، آمنة لفحص
`isOk()` بلا أي احتمال فشل — بدل القيمة الخام مباشرة. راجع `Interpreter.rin`/`CodeGen.rin` في القالب
لتطبيق هذا النمط بالكامل.

## 4. من مشروع محلي إلى لغة منشورة في المتجر

`LanguageMarketplacePublisher.buildExtensionForPublish(projectDir, uid, displayName)` يحزم المشروع
(الملفات السبعة الأساسية + `examples/`) في أرشيف base64، ويبني `RinExtension` بـ
`type = "language"`. مرّر الناتج إلى `ExtensionRepository.publishExtension(...)` — بالضبط نفس مسار
نشر أي إضافة أخرى (لا حاجة لأي بنية تحتية جديدة). تظهر لغتك فوراً في "Rin Extensions Marketplace"
تحت تصنيف "لغة"، ويستطيع أي مستخدم تثبيتها عبر `ExtensionManager.install` فيصبح محرره قادراً على
فتح/تشغيل/تلوين ملفاتها مباشرة.

## 5. "لغة رسمية" (Official)

شارة `RinExtension.isOfficial` **لا يمنحها الناشر لنفسه**: قاعدة أمان مضافة في
`firebase/database.rules.json` (عقدة `extensions/$extensionId/isOfficial`) تمنع أي كتابة تحوّل
القيمة من `false` إلى `true` من داخل التطبيق — فقط تُبقيها كما هي أو تعيدها لـ`false`. اعتماد لغة
كـ"رسمية" يتطلّب اليوم تدخلاً يدوياً من فريق Rin بعد مراجعة:

- استقرار اللغة (لا أخطاء تشغيل شائعة على أمثلة متنوعة).
- عدم تعارض `fileExtension` مع لغة رسمية أخرى موجودة مسبقاً.
- توثيق كافٍ (`README.md` مكتمل، أمثلة تعمل في `examples/`).
- التزام تراخيص/محتوى الوصف بسياسات المتجر العامة نفسها المطبَّقة على كل الإضافات.

هذا يماثل تماماً كيف تُصبح لغة برمجة "معترفاً بها" في أدوات مثل GitHub Linguist: تبدأ كمشروع مجتمعي
عادي، ثم تُعتمَد رسمياً بعد مراجعة، لا تلقائياً عند النشر.

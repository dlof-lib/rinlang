# Rin Language Support — VSSDK (Visual Studio) Extension

هذا مشروع **VSSDK حقيقي** (امتداد Visual Studio كلاسيكي، MEF + AsyncPackage)، مختلف تماماً عن
حزمة VS Code التي رفعتها سابقاً.

## سبب الخطأ الذي ظهر لك

الصورة التي أرفقتها تُظهر رفع `rin-lang-1.0.0.vsix` عبر صفحة **New Visual Studio Extension**
على `marketplace.visualstudio.com`، والخطأ الناتج:

```
Error: Value cannot be null.
Parameter name: v1
```

بفحص محتوى ذلك الـ vsix، اتضح أن `extension.vsixmanifest` بداخله يحدد:

```xml
<Installation>
  <InstallationTarget Id="Microsoft.VisualStudio.Code" />
</Installation>
```

أي أنه **حزمة VS Code** (package.json + activationEvents + tmLanguage.json...) معبّأة بصيغة
vsix، وليس حزمة Visual Studio كلاسيكي. صفحة "New Visual Studio Extension" على الـ Marketplace
مخصّصة حصراً لحزم تستهدف `Microsoft.VisualStudio.Pro/Community/Enterprise`، وترفض بصمت أي شيء
آخر بهذا الخطأ العام. الحل الصحيح لكل نوع:

| نوع الامتداد | أين تُنشر | كيف |
|---|---|---|
| VS Code (الحزمة القديمة عندك) | VS Code Marketplace (`marketplace.visualstudio.com/vscode`) | `vsce publish` عبر Azure DevOps PAT، ليس عبر صفحة الرفع اليدوي |
| Visual Studio كلاسيكي (هذا المشروع) | VS Marketplace → "New Visual Studio Extension" | ابنِ VSIX من هذا المشروع بـ MSBuild/Visual Studio ثم ارفعه من نفس الصفحة |

هذا المشروع هو الخيار الثاني: بُني من الصفر بـ `InstallationTarget` صحيح
(`Microsoft.VisualStudio.Community/Pro/Enterprise`) في `src/source.extension.vsixmanifest`.

## ما الذي يوفره هذا الامتداد

- **تلوين نحوي كامل (Classifier)** لملفات `.rin` (بما فيها `*.og.rin` و`*.min.rin`)، منقول
  حرفياً من قواعد `rin.tmLanguage.json` وألوان `rin-dark-color-theme.json` الأصليين:
  تعليقات، سلاسل نصية، أرقام، `true/false/nil`، `PI/E`، كلمات التحكم
  (`if/else/while/return/and/or/print`)، `let/fun/text`، وسوم الحاويات
  (`@container`, `@container.pipe|data|api|import|table|doc`, `@Containers.Group`, `@Volume`,
  `Section`, `Translations`)، `document/route/table`، وسم الإغلاق `.end/...`, `@import [as]`,
  الدوال المدمجة (stdlib)، الدوال الرياضية الصريحة، عامل الأنابيب `|>`، وبقية العوامل والترقيم.
- **تسجيل نوع محتوى ونوع ملف** (`ContentTypeDefinition` + `FileExtensionToContentTypeDefinition`)
  لربط `.rin` بالمصنّف تلقائياً عند فتح أي ملف.
- **أمر "Rin: تشغيل الملف الحالي"** (Tools ← Run Rin File) يبحث عن `rin_cli` بجانب الحل أو ضمن
  PATH، يشغّله على الملف النشط، ويطبع الناتج في نافذة Output باسم "Rin".
- **29 مقتطف كود (Snippets)** محوّلة من `rin.code-snippets` الأصلية إلى صيغة VS الأصلية
  (`.snippet` XML) تحت `src/Snippets/Rin/`، مع تسجيل `ProvideLanguageCodeExpansion`.

## البنية

```
RinLangVSSDK.sln
src/
  RinLangVSSDK.csproj          مشروع VSSDK (net472, MEF + AsyncPackage)
  source.extension.vsixmanifest    الـ manifest الصحيح المستهدف لـ Visual Studio نفسه
  RinLangPackage.cs             الحزمة (AsyncPackage) + تسجيل القائمة والمقتطفات
  RinLangPackage.vsct           جدول أوامر القائمة (زر Run Rin File)
  Guids.cs / PackageIds.cs
  ContentDefinition/RinContentDefinition.cs   تسجيل نوع محتوى "rin" وربط الامتداد
  Classification/
    RinClassificationTypes.cs      تعريف كل فئة تلوين (Export MEF)
    RinClassificationFormats.cs    الألوان (منقولة من الثيم الأصلي)
    RinTokenizer.cs                المحلّل اللغوي (regex سطراً بسطر)
    RinClassifier.cs               IClassifier
    RinClassifierProvider.cs       IClassifierProvider (MEF)
  Commands/RunRinFileCommand.cs   أمر تشغيل الملف عبر rin_cli
  Snippets/Rin/*.snippet          29 مقتطفاً بصيغة VS الأصلية
  Resources/icon.png, RinCommand.png
samples/showcase.rin              ملف تجربة سريع لاختبار التلوين
```

## البناء والتشغيل

### الطريقة الأسهل: بناء تلقائي على GitHub (بدون تثبيت أي شيء عندك)

المشروع يتضمن `.github/workflows/build.yml` يبني الملف على خادم Windows فيه Visual Studio
جاهز مسبقاً:

1. ادفع هذا المستودع إلى GitHub (repo عادي، حتى لو خاص).
2. تبويب **Actions** ← شغّل الـ workflow (يعمل تلقائياً أيضاً بعد أي push لـ main/master).
3. بعد اكتمال التشغيل بنجاح، افتحه ← قسم **Artifacts** بالأسفل ← نزّل
   `RinLangVSSDK-vsix`. بداخله `RinLangVSSDK.vsix` جاهز للرفع مباشرة.

### الطريقة المحلية (Visual Studio 2022)

يتطلب **Visual Studio 2022** مع الحمل (Workload) التالي مُثبّتاً:
`Visual Studio extension development`.

1. افتح `RinLangVSSDK.sln`.
2. اضغط F5 (أو Debug ← Start Without Debugging). سيُفتح نسخة تجريبية من VS
   (`/rootsuffix Exp`) ومعها الامتداد محمّلاً.
3. افتح `samples/showcase.rin` في تلك النسخة التجريبية للتأكد من التلوين.
4. لتوليد الـ VSIX القابل للرفع: Build ← Build Solution (Release)، ثم ستجد
   `bin\Release\RinLangVSSDK.vsix` جاهزاً للرفع على
   `marketplace.visualstudio.com` عبر "New Visual Studio Extension" (نفس الصفحة في صورتك،
   لكن هذه المرة الـ manifest يستهدف Visual Studio فعلياً فلن يظهر خطأ `v1`).

قبل الرفع، عدّل في `source.extension.vsixmanifest`:
- `Identity/@Id` إلى معرّف فريد خاص بك (لا يجب أن يتشابه مع أي امتداد آخر على المتجر).
- `MoreInfo` / `GettingStartedGuide` إلى رابط مستودعك الفعلي.
- أضف صورًا حقيقية بدل placeholder إن رغبت بمعاينة أفضل في صفحة المتجر.

## قيود معروفة (وخطوات تالية إن رغبت بإكمالها)

- **مقتطفات الإدراج عبر Ctrl+K, X**: التسجيل عبر `ProvideLanguageCodeExpansion` يجعل ملفات
  `.snippet` مكتشَفة، لكن ظهور قائمة "Insert Snippet" الكاملة داخل المحرر لملفات `.rin` يفترض
  عادة وجود خدمة لغة كاملة (`IVsLanguageInfo`) مرتبطة بنفس نوع المحتوى. هذا المشروع لا يضيف
  خدمة لغة كاملة عمداً (لإبقائه خفيفاً وقابلاً للبناء دون أخطاء)، فإن أردت التوسع لاحقاً أضف
  `RinLanguageInfo : IVsLanguageInfo` وسجّله عبر `[ProvideLanguageService]` بنفس GUID اللغة.
- **الإكمال التلقائي/Hover/Diagnostics**: هذا الإصدار يوفر تلوينًا نحويًا فقط (Classifier)،
  وليس Language Server كامل. لإضافة إكمال تلقائي وتلميحات حقيقية بأفضل جهد، الأنسب هو تغليف
  محرّك Rin (`engine/rin_lexer.*`, `rin_parser.*`) خلف Language Server Protocol (LSP) واستهلاكه
  من VS عبر `Microsoft.VisualStudio.LanguageServer.Client` — بنية مختلفة تمامًا (وأكبر) عن
  Classifier، ويمكن بناؤها كخطوة تالية منفصلة إذا احتجتها.
- لم يُختبر البناء فعليًا داخل Visual Studio في هذه البيئة (لا تتوفر هنا أدوات MSBuild/VSSDK
  ولا اتصال شبكة لتنزيل NuGet)، لذا افحص أول Build محليًا وأخبرني بأي خطأ تصادفه لإصلاحه فورًا.

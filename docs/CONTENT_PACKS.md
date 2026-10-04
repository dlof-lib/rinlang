# حزم المحتوى القابلة للتنزيل (Content Packs)

الهدف: تقليل حجم الـ APK بنقل ما لا يحتاجه كل مستخدم إلى حزم تُنزَّل عند الحاجة.

| الحزمة | النوع | ما الذي خرج من الـ APK |
|---|---|---|
| `lang-en` , `lang-es` | language | `values-en/` و`values-es/` (≈ 220KB XML خام؛ الحزمة المضغوطة ≈ 16KB لكل لغة) |
| `illust-lang` | asset | `BundledIllustLanguage.kt` (≈ 67KB سلاسل نصية مضمّنة) |

العربية (اللغة الافتراضية) تبقى مدمجة في التطبيق.

## كيف تعمل
1. `content-packs/packs.json` يصف الحزم، و`scripts/build_content_packs.py` يبنيها إلى `*.zip` حتمي + `catalog.json`
   (فيه `sha256` وحجم كل حزمة). ينفَّذ تلقائياً في `.github/workflows/pages.yml` وينشر على
   `https://dlof-lib.github.io/rinlang/packs/catalog.json`.
2. التطبيق (`app/.../packs/`):
   * `PackCatalog` يجلب الكتالوج ويخزّن نسخة محلية للعرض دون اتصال.
   * `PackDownloader` ينزّل (https فقط) مع استئناف `Range`، ثم يتحقق من الحجم و`sha256` قبل أي فك.
   * `PackStore` يفك الـ zip بحماية (zip-slip، حدود الحجم/العدد)، يتأكد أن `pack.json` يطابق الكتالوج، ثم يثبّت ذرّياً في
     `filesDir/content_packs/<id>/`.
   * `LanguagePacks` يطبّق حزمة اللغة وقت التشغيل: `OverlayResources` يعترض `getString/getQuantityString/getStringArray`،
     و`PackLayoutInflater` يعيد ترجمة نصوص ملفات XML. يُفعَّلان عبر `RinBaseActivity` (قاعدة كل الشاشات).
   * `PackDownloadDialog.ensure(...)` مدخل واحد لأي ميزة تحتاج حزمة (يسأل، ينزّل بتقدّم حقيقي، ثم ينفّذ المتابعة).
   * `ContentPacksActivity` (الإعدادات ← «الحزم والتنزيلات») لإدارة الحزم: تنزيل/تحديث/حذف + المساحة المستخدمة.

## إضافة حزمة
* **لغة جديدة**: أنشئ `content-packs/lang/<code>/` بملفات `strings*.xml` (نفس أسماء `values/`)، أضف عنصراً `type: language`
  في `packs.json`، وأضف اللغة في `LanguageActivity.all`/`locales_config.xml`.
* **حزمة أصول**: مجلد (أو مجلد موجود أصلاً مثل `examples/customlang/illust`) وعنصر `type: asset`؛ ثم من الكود:
  `PackDownloadDialog.ensure(activity, "my-pack") { /* PackStore.dirOf(ctx, "my-pack") */ }`.
* غيّر محتوى حزمة ⇒ يتغيّر `sha256` ⇒ يظهر «يتوفر تحديث» تلقائياً في شاشة الحزم.

## الأمان
https فقط (حتى بعد redirect)، تحقق `sha256` وحجم، منع zip-slip، سقف 64MB بعد الفك، `pack.json` يجب أن يطابق الـ id/النوع
في الكتالوج، وعناصر الكتالوج غير الصالحة تُهمل.

## تقليص إضافي في Gradle (release)
`rin.minify` (R8 + shrinkResources، الافتراضي true)، `rin.abis` (الافتراضي `arm64-v8a,armeabi-v7a`؛ فارغ = الكل)،
و`resourceConfigurations` لإزالة لغات المكتبات غير المستعملة. مثال: `gradle assembleRelease -Prin.minify=false -Prin.abis=`.

## حدود معروفة
* نصوص `MenuInflater` (ملفات `res/menu`) لا تُعاد ترجمتها (غير مستعملة حالياً في الكود).
* مكتبة Rin القياسية (≈ 550KB) ما زالت مضمّنة في المحرك الأصلي `.so`؛ نقلها يتطلب تعديل `rin_stdlib_libs.h` وإعادة بناء NDK.
* إن اختار المستخدم الإنجليزية/الإسبانية من إعدادات أندرويد (لغة التطبيق) بدون حزمة تظهر العربية حتى تُنزَّل الحزمة.

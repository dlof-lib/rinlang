# CHANGES_PACKS — حزم المحتوى القابلة للتنزيل

الشرح الكامل: `docs/CONTENT_PACKS.md`.

## جديد
- `app/src/main/java/com/dlof/rinlang/packs/` — `ContentPack`, `PackStore`, `PackCatalog`, `PackDownloader`, `PackDownloadDialog`, `LanguagePacks`, `PackException`.
- `RinBaseActivity.kt` — قاعدة الشاشات التي تطبّق حزمة اللغة؛ كل `AppCompatActivity()` في المشروع صار `RinBaseActivity()`.
- `ContentPacksActivity.kt` + سطر «الحزم والتنزيلات» في الإعدادات + تسجيل في `AndroidManifest.xml`.
- `content-packs/` (مصادر الحزم) و`scripts/build_content_packs.py` + خطوة نشر في `pages.yml`.
- نصوص `strings_packs.xml` (ar في `values/` و`values-ar/`، en/es داخل حزم اللغة).

## تغيّر / أُزيل من الـ APK
- نُقل `res/values-en` و`res/values-es` إلى `content-packs/lang/{en,es}` (تُبنى حزماً).
- حُذف `BundledIllustLanguage.kt`؛ `installBundledIllust` يقرأ من حزمة `illust-lang`، و`ProjectsActivity` تنزّلها عند أول مشروع Illust.
- `LanguageActivity` تنزّل حزمة اللغة قبل تطبيقها (ما عدا العربية).
- `app/build.gradle(.kts)`: R8 + shrinkResources، `abiFilters` لـ release، `resourceConfigurations`؛ `proguard-rules.pro` محدّث.

## لم يُتحقق منه
لم يُبنَ APK هنا (لا Android SDK/NDK في بيئة العمل). تحقق من البناء والتشغيل على جهاز قبل الإصدار.

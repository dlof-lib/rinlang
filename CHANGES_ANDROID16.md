# دعم Android 15 وما فوق في RinStudio

| البند | القيمة |
|---|---|
| الحد الأدنى (minSdk) | **Android 15 — API 35** |
| الهدف (targetSdk / compileSdk) | **Android 16 — API 36** |
| AGP / Gradle / Kotlin | 8.13.0 / 8.13 / 2.0.21 (دون تغيير) |
| NDK | r28 (`28.0.13004108`) — مكتبات native بمحاذاة صفحات 16KB |

## ما تغيّر

### البناء (`app/build.gradle` و`app/build.gradle.kts`)
- `minSdk` = 35، `compileSdk`/`targetSdk` = 36. (لا حاجة لـ desugaring بعد الآن.)
- NDK من r26 إلى r28 + `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` (أجهزة 16KB، ومتطلب Google Play).
- `useLegacyPackaging = false` للمكتبات الأصلية.
- رفع `core-ktx 1.15.0` و`appcompat 1.7.1` و`activity-ktx 1.10.1`.
- سير عمل GitHub: NDK r28 + `platforms;android-36` + `build-tools;36.0.0`.

### Android 15/16
- **Edge-to-edge إجباري**: كلاس `RinSystemBars` (يُسجَّل من `RinApplication`) يطبّق حشوات أشرطة النظام
  وقطع الشاشة ولوحة المفاتيح على كل شاشة، ويرسم ألوان الثيم خلف الشريطين.
- **الرجوع التنبّؤي**: `enableOnBackInvokedCallback="true"`، ونقل `FilesActivity` و`IndsinPreviewActivity`
  إلى `OnBackPressedCallback` (Android 16 لا يستدعي `onBackPressed()`).
- `startActivityForResult` → Activity Result API في `RinStoreActivity`.
- `systemUiVisibility` → `WindowInsetsControllerCompat` (ملء الشاشة لفيديو التوثيق).
- قواعد نسخ احتياطي (`data_extraction_rules.xml`, `backup_rules.xml`).
- تثبيت APK: إن لم يُمنح إذن «مصادر غير معروفة» تُفتح شاشة الإذن مباشرة.
- تصدير APK: الخيارات API 35 و36 فقط (الحد الأدنى والهدف الافتراضي للتطبيقات المُصدَّرة 35/36)،
  لأن الحزمة المضيفة نفسها تتطلب 35.
- `ApkV1Signer`: `android.util.Base64` بدل `java.util.Base64`.

## حزمة لكل إصدار (Android 15 / 16 / 17)
- `build.gradle*`: خصائص `-Prin.targetSdk` (35–37) و`-Prin.compileSdk` و`-Prin.versionSuffix`.
- `.github/workflows/build-android-versions.yml`: مصفوفة تبني APK موقَّعاً لكل إصدار، تتحقق من
  minSdk/targetSdk/محاذاة 16KB، وتنشر Release. التفاصيل في `docs/ANDROID_VERSIONS.md`.

## لم يُتحقَّق منه بعد
البيئة بلا Android SDK، فلم يُشغَّل `./gradlew assembleDebug`. جرّب على جهاز/محاكي Android 15 و16.

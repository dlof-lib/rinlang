# دعم Android 7.0 → Android 16 في RinStudio

| البند | القيمة |
|---|---|
| الحد الأدنى (minSdk) | **Android 7.0 — API 24** |
| الموصى به | **Android 10+ — API 29** (MediaStore بدل أذونات التخزين، لغة التطبيق، سمات داكنة) |
| الهدف (targetSdk / compileSdk) | **Android 16 — API 36** |
| AGP / Gradle / Kotlin | 8.13.0 / 8.13 / 2.0.21 (دون تغيير) |
| NDK | r28 (`28.0.13004108`) — مكتبات native بمحاذاة صفحات 16KB |

## ما تغيّر

### البناء (`app/build.gradle` و`app/build.gradle.kts`)
- `compileSdk`/`targetSdk` = 36، `minSdk` يبقى 24.
- NDK من r26 إلى r28 + `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` (أجهزة 16KB، ومتطلب Google Play عند targetSdk 35+).
- `useLegacyPackaging = false` للمكتبات الأصلية (شرط المحاذاة).
- **Core library desugaring** (`desugar_jdk_libs 2.1.5`) ليبقى `java.time` وأخواتها متاحة على Android 7.0/7.1.
- رفع `core-ktx 1.15.0` و`appcompat 1.7.1` و`activity-ktx 1.10.1`.
- سير عمل GitHub: NDK r28 + `platforms;android-36` + `build-tools;36.0.0`.

### إصلاح خاص بـ Android 7.x
- `apk/ApkV1Signer.kt` كان يستخدم `java.util.Base64` (API 26+) فيتعطّل تصدير APK على Android 7.0/7.1
  بـ `NoClassDefFoundError`؛ استُبدل بـ `android.util.Base64` (`NO_WRAP`).

### Android 15/16
- **Edge-to-edge إجباري**: كلاس جديد `RinSystemBars` يُسجَّل من `RinApplication` ويطبّق حشوات
  أشرطة النظام وقطع الشاشة ولوحة المفاتيح على جذر كل شاشة، ويرسم ألوان الثيم خلف الشريطين
  ويضبط لون أيقونات الشريط تلقائياً. لا يعمل إلا على API 35+؛ الإصدارات الأقدم تبقى كما كانت.
- **زر الرجوع التنبّؤي**: `android:enableOnBackInvokedCallback="true"`؛ وAndroid 16 لا يستدعي
  `onBackPressed()` أصلاً، فنُقل `FilesActivity` و`IndsinPreviewActivity` إلى `OnBackPressedCallback`.
- `startActivityForResult`/`onActivityResult` في `RinStoreActivity` → Activity Result API.
- `systemUiVisibility` (ملء الشاشة لفيديو التوثيق) → `WindowInsetsControllerCompat`.
- قواعد نسخ احتياطي (`data_extraction_rules.xml`, `backup_rules.xml`) لـ Android 12+ و6–11.
- تثبيت APK: على Android 8+ إن لم يُمنح إذن «مصادر غير معروفة» يُفتح له مباشرةً شاشة الإذن بدل فشل صامت.
- شاشة «تصدير APK» تعرض API 35 و36، والهدف الافتراضي للتطبيقات المُصدَّرة صار 36.

## لم يُتحقَّق منه بعد (يتطلب بناءً فعلياً)
هذه البيئة بلا Android SDK ولا إنترنت، فلم أستطع تشغيل `./gradlew assembleDebug`. التعديلات
راجعتها يدوياً ووثّقتها بدقة، لكن يلزم بناء وتجربة على:
1. جهاز/محاكي Android 7.0 (API 24) — تصدير APK وفتح المحرر.
2. Android 10 — التنزيلات إلى Downloads.
3. Android 15/16 — شكل الشاشات تحت أشرطة النظام واللوحة والرجوع.

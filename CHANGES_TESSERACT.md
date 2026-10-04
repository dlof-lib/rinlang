# إصلاح فشل البناء: tesseract4android (2026-10-04)

**الخطأ (من سجل Build & sign release APK، المهمة :app:mergeReleaseNativeLibs):**
`Could not find cz.adaptech.tesseract4android:tesseract4android:4.7.0` (ثم 4.9.0) —
البحث في Google Maven وMaven Central فقط وكلاهما يعيد 404.

**السبب:** المكتبة تُنشر على JitPack (حسب README الرسمي لـ adaptech-cz/Tesseract4Android)
ولا يوجد مستودع JitPack في `settings.gradle.kts`.

**الإصلاح:**
- `settings.gradle.kts`: إضافة JitPack عبر `exclusiveContent` للمجموعة `cz.adaptech.tesseract4android` فقط.
- `app/build.gradle` و`app/build.gradle.kts`: توحيد النسخة على 4.9.0 (minSdk 24 ≥ 21 المطلوب).

بقية المراحل (Preflight، اختبارات المحرك 54+42) كانت ناجحة.

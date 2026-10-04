# حزم RinStudio لكل إصدار أندرويد

الحد الأدنى لكل الحزم: **Android 15 (API 35)**. الفرق بينها هو `targetSdk`:

| الحزمة | targetSdk | ملاحظات |
|---|---|---|
| `RinStudio-<ver>-android15-api35.apk` | 35 | edge-to-edge إجباري، صفحات 16KB |
| `RinStudio-<ver>-android16-api36.apk` | 36 | الرجوع التنبّؤي دائماً، `onBackPressed()` لا يُستدعى |
| `RinStudio-<ver>-android17-api37.apk` | 37 | سلوك Android 17 (انظر أدناه) |

سير العمل: `.github/workflows/build-android-versions.yml` — يعمل عند دفع وسم `v*` أو يدوياً (Actions ← Run workflow)،
ويتحقق لكل حزمة من التوقيع و`minSdk`/`targetSdk` ومحاذاة 16KB، ثم يُرفقها بـ GitHub Release مع `SHA256SUMS.txt`.

## بناء نسخة محلياً
```bash
gradle :app:assembleRelease -Prin.targetSdk=37 -Prin.versionSuffix=-android17
```

## توقيع دائم (مهم للتحديثات)
أضف أسرار المستودع: `RIN_KEYSTORE_BASE64` (`base64 -w0 release.keystore`)، `RIN_KEYSTORE_PASSWORD`،
`RIN_KEY_ALIAS`، `RIN_KEY_PASSWORD`. بدونها يُستخدم مفتاح مؤقت لكل تشغيلة.

## Android 17 (API 37)
- `compileSdk` يبقى 36 مع AGP 8.13؛ منصة `android-37` تتطلب **AGP 9.1+ و Gradle 9.3+** وتغييراً في
  إعداد Kotlin (AGP 9 يدمج Kotlin). عند الترقية: `-Prin.compileSdk=37` وعدّل إصدارات الإضافات في `build.gradle*`.
- تغييرات سلوك تؤثر عند targetSdk 37: إلغاء إمكانية رفض تغيير الحجم/الاتجاه على الشاشات الكبيرة (≥600dp)،
  وإذن `ACCESS_LOCAL_NETWORK` للوصول إلى الشبكة المحلية، ومنع تعديل حقول `static final` بالانعكاس.
  التطبيق لا يستخدم الانعكاس هذا، والخادم المحلي يستمع على 127.0.0.1 فقط — **اختبر النسخة على جهاز/محاكي Android 17**.

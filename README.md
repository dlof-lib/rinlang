# GETY — تأكيد الجهاز (المرحلة 2 من دخول RinStudio)

تطبيق أندرويد منفصل تماماً عن RinStudio. مهمته الوحيدة: قراءة رمز QR أو كود نصي
تعرضه شاشة "تأكيد الجهاز" في RinStudio، ثم تأكيده حتى يكمل RinStudio تسجيل الدخول.

## كيف يعمل
- لا يحتاج تسجيل دخول ولا حساب خاص به إطلاقاً.
- يمسح QR بالكاميرا (`zxing-android-embedded`) أو يقبل إدخال الكود يدوياً.
- يتواصل مباشرة مع نفس Realtime Database الذي يستخدمه RinStudio عبر REST API
  العام (`RtdbClient.kt`) — بلا Firebase SDK وبلا ملف `google-services.json` خاص به.
- لا يعرف شيئاً عن هوية المستخدم أو بريده أو كلمة سره؛ فقط يؤكد أن شخصاً يملك
  هذا الرمز المؤقت (صالح حتى 5 ساعات) قام بمسحه.

## قبل البناء
1. **لا حاجة لأي إعداد إضافي في Firebase Console** طالما لم تغيّر مشروع RinStudio
   نفسه. إن غيّرته، حدّث `DATABASE_URL` في
   `app/src/main/java/com/dlof/gety/FirebaseConfig.kt`.
2. تأكد أن قواعد Realtime Database في مشروع RinStudio مُحدَّثة لتشمل عقدة
   `pairing_codes` (راجع أرشيف `rinstudio-auth-changes.zip` المرفق).

## البناء
افتح مجلد المشروع في Android Studio مباشرة (File → Open)، سيقوم تلقائياً بتوليد
`gradlew`/`gradle-wrapper.jar` عند أول مزامنة. بديلاً، إن كان لديك Gradle مثبّتاً
محلياً: `gradle wrapper --gradle-version 8.7` ثم `./gradlew assembleDebug`.

الحزمة: `com.dlof.gety` — الحد الأدنى لأندرويد: API 24.

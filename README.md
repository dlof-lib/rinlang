# WGOM — إدارة الحساب (المرحلة 3 من دخول RinStudio)

تطبيق أندرويد منفصل تماماً عن RinStudio، لإدارة حساب Rin بعد إنشائه: تعديل الاسم
واسم المستخدم، وتغيير كلمة السر.

## كيف يعمل
- تسجيل الدخول بنفس بريد/كلمة سر حساب Rin (لا يُنشئ حسابات جديدة — الإنشاء فقط
  من RinStudio نفسه، المرحلة 1).
- يستخدم Identity Toolkit REST (نفس خدمة Firebase Auth) لتسجيل الدخول وتغيير
  كلمة السر (`AuthRestClient.kt`)، و Realtime Database REST لقراءة/تعديل
  الاسم واسم المستخدم (`RtdbRestClient.kt`) — بلا Firebase SDK وبلا ملف
  `google-services.json` خاص به؛ كلاهما عبر مفتاح الـWeb API العام لمشروع
  RinStudio نفسه (نفس المفتاح الموجود أصلاً داخل `google-services.json` لتطبيق
  RinStudio).
- الجلسة في الذاكرة فقط (لا تُحفظ بعد إغلاق التطبيق) — إجراء أمان مقصود لتطبيق
  إدارة حساب حسّاس مثل هذا.

## قبل البناء
إن غيّرت مشروع Firebase الخاص بـRinStudio مستقبلاً، حدّث القيمتين في
`app/src/main/java/com/dlof/wgom/FirebaseConfig.kt`:
- `DATABASE_URL` ← من `project_info.firebase_url` في `google-services.json`.
- `WEB_API_KEY` ← من `client[0].api_key[0].current_key` في نفس الملف.

## البناء
افتح مجلد المشروع في Android Studio مباشرة (File → Open)، سيقوم تلقائياً بتوليد
`gradlew`/`gradle-wrapper.jar` عند أول مزامنة. بديلاً: `gradle wrapper --gradle-version 8.7`
ثم `./gradlew assembleDebug`.

الحزمة: `com.dlof.wgom` — الحد الأدنى لأندرويد: API 24.

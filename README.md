# نظام تسجيل الدخول ثلاثي المراحل — RinStudio / GETY / WGOM

هذا الأرشيف يحتوي **فقط** على الملفات الجديدة/المعدَّلة اللازمة لفصل نظام الدخول إلى
3 تطبيقات أندرويد منفصلة قابلة للبناء، بالإضافة إلى GitHub Actions لبناء APK لكل واحد.

## البنية

```
rinstudio-auth/   → com.dlof.rinlang   (المرحلة 1: تسجيل/دخول + بدء المرحلة 2)
gety/             → com.dlof.gety      (المرحلة 2: تأكيد الجهاز عبر QR/كود)
wgom/             → com.dlof.wgom      (المرحلة 3: إدارة الحساب — الاسم/كلمة السر)
firebase/database.rules.json → قواعد Realtime Database المشتركة بين الثلاثة
.github/workflows/           → 3 ملفات yml لبناء APK لكل تطبيق عبر GitHub Actions
```

## ماذا كان ناقصاً وتم استكماله

الشيفرة البرمجية (Kotlin) لكل الشاشات الثلاث كانت موجودة بالفعل في المستودع الأصلي،
لكنها كانت جميعها مدمجة داخل وحدة Gradle واحدة مُعدّة لبناء GETY فقط. هذا يعني:

- `colors.xml` كان يحتوي على ألوان GETY فقط؛ ألوان RinStudio (`rin_*`) وWGOM (`wgom_*`)
  لم تكن مُعرَّفة إطلاقاً رغم استخدامها في عدّة تخطيطات — ما كان سيمنع البناء تماماً.
- بعض النصوص (`hint_new_password`, `profile_saved`, `no_connection_title`...) لم تكن موجودة.
- `activity_login.xml` الوحيد الموجود كان في الأصل مخصّصاً لـWGOM فقط؛ تم إنشاء نسخة
  مطابقة لشاشة دخول RinStudio بنفس المعرّفات التي يتوقعها `LoginActivity.kt`.
- كل تطبيق الآن له `build.gradle` / `AndroidManifest.xml` / `settings.gradle` خاص به،
  يتضمّن فقط الاعتماديات التي يحتاجها فعلاً (RinStudio يحتاج Firebase SDK، بينما
  GETY وWGOM يتواصلان عبر REST مباشرة بلا Firebase SDK كما في تصميمهما الأصلي).

لم يتم تغيير أي منطق برمجي (منطق التحقق، توليد رمز الإقران، صلاحية 5 ساعات، إلخ) —
فقط استُكملت الموارد المفقودة وأُعيد تنظيم الملفات إلى 3 مشاريع منفصلة.

## قبل البناء

1. **rinstudio-auth**: يحتاج `app/google-services.json` حقيقياً (مشروع Firebase
   `dlof-massage` مسجَّل فيه `com.dlof.rinlang` مسبقاً حسب الملف الأصلي). في GitHub
   Actions، ضَع محتواه في سرّ باسم `RINSTUDIO_GOOGLE_SERVICES_JSON`.
   يحتاج أيضاً تفعيل EmailJS (راجع `EmailJsConfig.kt`) — القيم الحالية افتراضية،
   بدّلها بقيمك الخاصة إن أردت إرسال بريد فعلي.
2. **gety / wgom**: يعملان مباشرة بلا إعداد إضافي (يستخدمان REST + مفتاح Web API
   عام الموجود مسبقاً في `FirebaseConfig.kt`).
3. لتوقيع APK إصدار (release) بدل تصحيح (debug)، أضِف keystore ومفاتيحه كأسرار
   GitHub منفصلة وحدِّث ملفات الـyml — لم تُضَف هنا تلقائياً لأسباب أمنية (لا يجب
   وضع كلمات سر التوقيع داخل المستودع).

## بناء APK

كل تطبيق له سير عمل خاص في `.github/workflows/`:
- `build-rinstudio-auth-apk.yml`
- `build-gety-apk.yml`
- `build-wgom-apk.yml`

كل سير عمل يُشغَّل تلقائياً عند تعديل مجلد التطبيق المقابل على فرع `main`، أو يدوياً
عبر "Run workflow"، وينتج APK تصحيح (debug) قابل للتحميل من تبويب Artifacts.

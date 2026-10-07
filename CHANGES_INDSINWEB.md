# CHANGES_INDSINWEB — مكتبة WebView والروابط لـ indsin (الإصدار 1.0.0)

## الجديد
- **شاشة الربط** `iwLinkScreen`: تعرض الرابط حسب نوعه (فيديو، صفحة ويب، صورة، صوت، PDF، مستند، بريد، هاتف، موقع، واتساب/تيليجرام، متجر، مكتبة Rin، محجوب) بالعربية أو الإنجليزية وبثيم داكن/فاتح.
- **أيقونة المكتبة**: `assets/branding/indsinweb_icon.png` (512×512) و`.svg`.
- أدوات إضافية: تنظيف التتبّع، حجب العناوين المحلية/الخاصة، كشف نوع الملف، شريط عنوان (رابط/بحث)، مشاركة، تضمين Spotify/Dailymotion/Drive/Maps، توجيه عميق، `iwLinkify`/`iwAuditHtml`، `iwOpenPlan`، بطاقات/خطأ/تحميل، مفضّلة JSON.

- `lib/indsinweb.og.rin` (+ نسخة مضمَّنة في `app/src/main/cpp/rin_stdlib_libs.h` و`embeddedRinLibraries()`): دوال `iw*`
  للروابط (تحليل/تطبيع/ضم/استعلام)، الأمان (قائمة سماح للمخططات والمضيفين، حارس userinfo، `iwSanitizeHtml`)،
  أنواع الروابط (mailto/tel/sms/geo/WhatsApp/Telegram/Play/intent/deep link)، روابط Rin (`@user/lib.og.rin`)،
  تضمين يوتيوب (nocookie) وVimeo، بناة HTML لخاصية `html=`، ومولّد عنصر `WebView` وسجل تنقّل.
- `docs/indsinweb.md` المرجع · `examples/indsinweb_demo.rin` مثال `@container` · `tests/indsinweb_tests.rin` (287 اختبارًا).
- صفّان في `docs/standard-library.md` و`README.md`.

## تحقق
- `tests/indsinweb_tests.rin`: 287/287 على القرص وعلى النسخة المضمَّنة (مفسّر مبني من مصدر المشروع بـ g++).
- المثال عبر `runColdPipelineForContainerWithRuntime` الحقيقي: `url=` أصبح رابط تضمين youtube-nocookie، و`html=`
  بُني بالدوال، والرابط `javascript:` ظهر كـ`<span>` محجوب.
- لم يُبنَ APK ولم يُشغَّل على جهاز.

## ملاحظات
- `text` و`style` و`to` كلمات محجوزة في Rin (لا تصلح أسماء متغيرات)، والنفي `!` لا `not`.
- أداة `rin_indsin_run render` الجاهزة تعرض التعابير غير مقيَّمة (بلا مضيف مفسّر)، وهذا سلوك قائم لكل المكتبات (مثل relyRIN).
- ملف `build-artifact/rin_run` المرفق قديم (لا يدعم `for (let x in ...)`)؛ استعمل مفسّرًا مبنيًا من المصدر.

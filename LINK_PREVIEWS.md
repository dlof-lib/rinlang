# معاينة روابط Rin (Link Previews) + الأيقونة + قابلية العثور

## لماذا لم تظهر بطاقة الرابط؟
1. **`dlof-lib.github.io` وحده** لا يخدم أي صفحة (المشروع منشور على `/rinlang/`)، فلا توجد وسوم OG لتقرأها واتساب/تيليجرام.
2. **روابط المكتبات والحسابات** (`/rinlang/@rin/math.og.rin`) يخدمها `404.html` بحالة **404** ولا تنفّذ الزواحف JavaScript،
   فلا معاينة (وحتى لو ظهرت لكانت البطاقة ثابتة لا تعرف اسم المكتبة).
3. ملفات `favicon*.png` كانت مذكورة في `index.html` وفي `pages.yml` لكنها **غير موجودة** في `web/` (كانت ستكسر خطوة النسخ).

## ما الذي أُضيف
| الملف | الوظيفة |
|---|---|
| `scripts/build_share_pages.py` | يقرأ Firebase (قراءة عامة REST) ويولّد لكل مكتبة/إضافة/حساب صفحة ثابتة `@user/name.og.rin/index.html` + صورة `og/<hash>.png` (1200×630) + `sitemap.xml` + `robots.txt` |
| `.github/workflows/pages.yml` | يشغّل السكربت عند كل نشر **وكل 3 ساعات**؛ فشل Firebase = تحذير فقط (لا يُفشل النشر) |
| `web/favicon.ico` و`favicon-16/32/.png` و`apple-touch-icon.png` و`icon-512.png` و`icon-maskable-512.png` | أيقونات الموقع كاملة (`scripts/build_icons.py` يعيد توليدها من `icon-192.png`) |
| `web/manifest.webmanifest` | بيان PWA (تثبيت على الشاشة الرئيسية + اختصار «مكتبات Rin») |
| `web/index.html` | canonical، robots، keywords، JSON-LD (WebSite/ComputerLanguage/SoftwareApplication)، أيقونات، `<noscript>` نصي للزواحف |
| `web/404.html` | بطاقة احتياطية «Rin Libraries» لأي رابط لم يُبنَ له صفحة بعد |
| `user-site/` | صفحة جذر النطاق `dlof-lib.github.io` (تُرفع لمستودع باسم `dlof-lib.github.io`) — هذا هو ما يصلح الرابط في لقطتك |
| `RinLinks.kt` | المشاركة من التطبيق ترسل «العنوان + الرابط» فقط (البطاقة تحمل الوصف) |

## النشر
1. انسخ الملفات فوق المستودع (نفس المسارات) وادفع إلى `main`.
2. ضع محتوى `user-site/` في جذر مستودع `dlof-lib.github.io` (إن لم يوجد المستودع أنشئه بهذا الاسم بالضبط).
3. اختبر: https://developers.facebook.com/tools/debug/ (يُنعش كاش واتساب أيضاً) — و https://cards-dev.twitter.com/validator

## حدود يجب معرفتها
- **الحداثة:** مكتبة نُشرت للتو تظهر بمعاينة غنية بعد أقرب تشغيل مجدول (≤ 3 ساعات) أو عند `Run workflow` يدوياً؛ قبلها تظهر بطاقة «Rin Libraries» العامة.
- **الحسابات:** `/users` في Firebase يتطلب تسجيل دخول، لذا بطاقة الحساب مبنيّة من منشوراته (عدد المكتبات/التحميلات/الإعجابات) لا من صورته أو نبذته.
- **كاش واتساب/تيليجرام:** يحتفظان بالبطاقة القديمة لأيام؛ الروابط التي أُرسلت سابقاً بلا معاينة قد لا تتحدّث.
- خطوط الصور DejaVu (نفس خط `og-image.png` الحالي)؛ لتغييرها عدّل `font()` في السكربت.

## اختبار محلي
```bash
pip install pillow arabic-reshaper python-bidi
python3 tests/web/make_share_fixture.py
python3 tests/web/test_share_pages.py
python3 scripts/build_share_pages.py --out /tmp/dist --data tests/web/share_fixture.json
```

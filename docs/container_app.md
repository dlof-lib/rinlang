# container.app / container.site — هوية التطبيقات والمواقع

حاوية بيانات تُسمّي تطبيقاً أو موقعاً وتصفه: **الاسم، الاسم المختصر، المعرّف، الإصدار (semver + رقم بناء + قناة)، الأيقونات، الوصف، ونصوص محركات البحث (SEO)**. تُقرأ بدوال `container.app*`، وتولّد `<head>` و`manifest` و`robots.txt` و`sitemap.xml`، وتُحفَظ داخل ملف `.rcl` (حاوية CLC) كي تبقى الهوية مع الحزمة.

```rin
@container.app=piano            // أو @container.site=blog  أو الاختصار  @piano.app / @blog.site
    let name = "بيانو";
    let id = "org.rinlang.piano";
    let version = "1.0.0";
    let channel = "stable";
    let lang = "ar";
    let homepage = "https://rinlang.dev/piano/";
    let description = "مكتبة لرسم المشاهد بلغة Rin.";
    let icons = {"192": "icons/192.png", "512": "icons/512.png"};
    let seo = {title: "piano — رسم بلغة Rin", keywords: ["Rin", "SVG"], og_image: "icons/512.png"};
.end/container.app
```
الإغلاق دائماً `.end/container.app` (أو `.end/container.site`) حتى مع الاختصار. الجسم بيانات نقية (`let` فقط؛ لا `fun` ولا `on` ولا `state`) كبقية حاويات البيانات.

## الحقول
| المجموعة | الحقول |
|---|---|
| هوية | `kind` (`app`\|`site`، الافتراضي app) · `id` · `name` · `short_name` · `category` |
| إصدار | `version` (semver) · `version_code` (يُشتق `major*1e6+minor*1e3+patch` إن غاب) · `channel` (stable/beta/alpha/dev/nightly/rc) |
| ملكية | `author` · `publisher` · `license` · `homepage` |
| لغة | `lang` (مثل ar، ar-SA) · `dir` (rtl/ltr؛ يُشتق من اللغة) |
| وصف | `description` · `short_description` (يُشتق من أول سطر) |
| مظهر/PWA | `theme_color` · `background_color` · `display` · `orientation` · `start_url` · `scope` |
| أيقونات | `icon` (نص) · `icons` · `favicon` |
| SEO | `seo` ← `title` `description` `keywords` `canonical` `robots` `author` `og_title` `og_description` `og_image` `og_type` `og_site_name` `twitter_card` `twitter_site` `locale` `alternates` `json_ld` — أو اختصارات مسطّحة: `seo_title` `seo_description` `keywords` `canonical` `robots` `og_image` |

أسماء الحقول تقبل `snake_case` و`camelCase` (`shortName`). `keywords` نص بفواصل (`,` أو `،` أو `;`) أو مصفوفة.

**أشكال `icons`:** مصفوفة نصوص `["a.png"]` · مصفوفة قواميس `[{src, sizes, type, purpose}]` · قاموس حجم⇒مسار `{"192": "a.png", "512": "b.png"}`. يُستنتج `type` من الامتداد، و`sizes` من ترويسة PNG الحقيقية (إن وُجد الملف) أو من اسم الملف (`icon_192.png`) أو `any` لـ SVG/ICO، و`purpose=maskable` إن ورد في الاسم.

**القيم المشتقة (الافتراضية):** الاسم الافتراضي = اسم الحاوية · `short_name` من الاسم · `seo.title` = الاسم · `seo.description` = الوصف · `canonical` = `homepage` · `og_*` من `seo` · `twitter_card` حسب وجود صورة · `og:locale` من `lang` · `robots` = `index, follow` · صورة المشاركة = أكبر أيقونة.

## الدوال
كل دالة بصيغتين: `container.appXxx` و`app.xxx` (استعمل الأولى إن وُجدت حاوية اسمها `app`). `src` = اسم حاوية أو قاموس هوية (مثل ناتج `appInfo`/`appOpen`).

| الدالة | تعيد |
|---|---|
| `container.appInfo(src)` | قاموس الهوية المكتملة (+`version_parts`، +`container`) |
| `container.appValidate(src, {dir?, info?})` | مصفوفة `{level, code, field, message}`؛ فارغة = سليمة. `dir` يفعّل فحص وجود ملفات الأيقونات وصورة المشاركة؛ `info:false` يحذف مستوى info |
| `container.appHead(src, {manifest?, indent?, charset?})` | نص `<head>`: charset/viewport/title/description/keywords/robots/author/theme-color/canonical/hreflang/أيقونات/apple-touch-icon/manifest/Open Graph/Twitter/JSON-LD |
| `container.appManifest(src)` | web app manifest (JSON) |
| `container.appRobots(src, {sitemap?, disallow?})` | `robots.txt` (`noindex` ⇒ `Disallow: /`) |
| `container.appSitemap(src, paths, {base?, lastmod?, today?})` | `sitemap.xml` |
| `container.appBump(name, "major"\|"minor"\|"patch")` | الإصدار الجديد، ويحدّث الحقل `version` (و`version_code` إن عُرِّف صراحةً +1) |
| `container.appExport(src, dir, {paths?, base?, disallow?, lastmod?})` | يكتب `head.html` و`manifest.webmanifest` و`robots.txt` (+`sitemap.xml` عند `paths` وعنوان مطلق) |
| `container.appPack(src, dir, out, {level?, entry?, rin_version?, force?})` | يبني `.rcl` من مجلد ويحفظ الهوية؛ **يرفض** عند أخطاء الهوية (مثل أيقونة ناقصة) إلا مع `force:true`، ويرفض كتابة الناتج داخل المجلد المُحزَّم. مسارات الأيقونات نسبية لـ `dir` |
| `container.appOpen(path)` | هوية من ملف `.rcl` (+`has_identity`, `files`, `entry_point`, `created`) |
| `clcContainerIdentity(handle)` | نفس `appOpen` لحاوية مفتوحة بـ `clcContainerOpen` (تستعملها شاشة فحص CLC في التطبيق) |

## قواعد التحقق (`code`)
`error`: `name.missing` · `version.missing/invalid` · `kind.invalid` · `dir.invalid` · `icon.src.missing/unsafe` · `icon.file.missing` · `seo.canonical.not_absolute` · `seo.robots.invalid` · `seo.alternate.url`.
`warn`: `name.too_long`(>45) · `short_name.too_long`(>12) · `description.missing` · `lang.invalid` · `color.invalid` · `homepage.not_absolute` · `icons.missing/too_small` · `icon.type.unknown` · `icon.sizes.unknown` · `seo.title.missing/too_long`(>60) · `seo.description.missing/too_long`(>160) · `seo.keywords.too_many`(>12) · `seo.og_image.missing_file`.
`info`: `id.missing` · `lang.missing` · `icons.no_512` · `icons.no_maskable` · `seo.title.too_short` · `seo.description.too_short` · `seo.og_image.missing`.
الأطوال تُحسب بمحارف Unicode لا بايتات (العربية صحيحة).

## التخزين داخل `.rcl`
بلا أي تغيير على التخطيط الثنائي لـ CLC 1.0: الهوية تُكتب في `Metadata::extra` بمفاتيح محجوزة `app.*` و`seo.*` و`icon.N.*` و`seo.alt.<lang>`، وتُزامَن الحقول القياسية (`name/version/description/author/license`). أي قارئ قديم يفتح الملف عادياً ويرى هذه كحقول مخصّصة. المنطق في `app/src/main/cpp/clc/clc_identity.h` (header-only، يعمل أيضاً من C++ مباشرة: `clc::applyIdentity/readIdentity/validateIdentity/buildHead/buildManifest`).

## أمثلة واختبارات
`examples/app_identity_demo.rin` · `tests/print/container_app.rin`.

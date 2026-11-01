# مصادر الصور (Image Sources)

يحدّد من أين يأتي `<Image>` و`<Avatar>` ببكسلاته. المحرّك **لا يلمس الشبكة ولا القرص**: يصنّف المصدر ويتحقق منه ويوحّده (`rin_indsin_imagesrc.h`)،
فيستلم كل مضيف (أندرويد، سطح المكتب، CLI، تصدير HTML، مُترجِم JS) قيمة `src` واحدة صالحة للعرض، والمضيف ينفّذ الإدخال/الإخراج.

| المفهوم | الكتابة | المعنى |
|---|---|---|
| **Src** | `src="…"` | السمة العامة؛ يُكتشف نوعها من قيمتها |
| **SrcSet** | `srcset="s.jpg 400w, l.jpg 1600w"` أو `"a.png, b.png 2x"` | يُختار أنسب مرشّح لعرض الخانة والكثافة (`dpr=`) |
| **URL** | `src="https://…"` / `url=` / `//cdn…` | صورة شبكية (ينزّلها المضيف ويخزّنها مؤقتاً) |
| **RemotePath** | `remotePath="/img/a.png" base="https://cdn.x.com"` | مسار خادم يُضمّ إلى `base=`/`origin=` |
| **LocalPath** | `localPath="photos/a.png"` · `file://…` · مسار نسبي | داخل مجلد المشروع/التطبيق؛ أي `..` مرفوض |
| **Base64** | `base64="iVBOR…" [mime=…]` | حمولة خام؛ يُكتشف النوع من البايتات (PNG/JPEG/GIF/WebP/AVIF/BMP/SVG) |
| **DataURI** | `src="data:image/png;base64,…"` / `dataUri=` | صورة مضمّنة؛ `image/*` فقط (`data:text/html` مرفوض) |
| **Blob** | `blob="blob:…"` / `src="blob:…"` | مقبض كائن يُنتجه المضيف وقت التشغيل |
| **Upload** | `onTap=uploadImage(cell, url)` | اسم بديل لـ `uploadMedia()` (انظر [indsin_media.md](indsin_media.md)) |
| **Download** | `onTap=downloadImage(src [, name [, cell]])` | طلب للمضيف لحفظ الصورة؛ التقدّم في `<cell>_status/_progress/_error` |

## ما يُكتب على العنصر
`src` (القيمة الصالحة للعرض) · `src_kind` (`url|remote_path|local_path|data_uri|base64|blob|none`) · `src_from` (السمة التي زوّدت المصدر) ·
`src_mime` · `src_bytes` (للمضمَّن) · `src_error` (سبب الرفض). الأولوية: `srcset` > `src` > `url` > `dataUri` > `base64` > `blob` > `remotePath` > `localPath`/`path`؛
أول مصدر **ينجح** يفوز، و`fallback=`/`placeholder=` آخر ملاذ (ويبقى سبب رفض الأصل في `src_error`).

## الأمان
مخططات `javascript:` و`ftp:` وغيرها لا تُقبل أبداً (يُفرَّغ `src`) · `data:` للصور فقط وبحدّ 8 MiB · لا `..` في المسارات المحلية ·
التحميل يرفض بيانات الدخول داخل الرابط (`https://u:p@host`) وكل ما ليس http(s)/`data:image`/`blob:`.

## ديناميكي
`src={photo}` (خلية warp): عند تغيّرها يُعاد التصنيف تلقائياً (مسار → data URI → …) عبر الـ Shuttle.

## أمثلة
```rin
<Image#hero srcset="hero-400.jpg 400w, hero-1200.jpg 1200w" fallback="placeholder.png" alt="Hero" />
<Image#logo remotePath="/brand/logo.svg" base="https://cdn.example.com" />
<Image#dot base64="iVBORw0KGgo…" width=48 alt="dot" />
<Image#pic src={photo} />                                  <!-- photo من pickMedia("image", photo) -->
<Button onTap={downloadImage("https://x.com/cat.png", "cat.png", saved)}>Save</Button>
<Text>{saved_status} {saved_progress}%</Text>
```
مثال كامل: `examples/samples/indsin_image_sources.rin` · اختبار: `tests/tools/test_indsin_imagesrc.cpp`.

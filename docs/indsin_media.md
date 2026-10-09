# Indsin Media — اختيار الوسائط ورفعها

أفعال `onTap=` (تعمل أيضاً مع `onLongPress=`/`onDoubleTap=`):

| الفعل | المعنى |
|---|---|
| `pickMedia(kind, cell [, multiple [, maxMb [, maxDim [, quality]]]])` | يفتح منتقي النظام. `kind`: `image` `video` `audio` `file` `any`، أو **`camera`** (صورة بالكاميرا) / **`camera:video`**. `maxMb` = 0 بلا حدّ. الوسيط الثالث `"append"` = اختيار متعدد يُضاف إلى السابق (للكاميرا: التقاطات متتابعة). **`maxDim`** (64–8192) يصغّر الصور إلى أطول ضلع بهذا الحجم، و`quality` (1–100، الافتراضي 85) جودة JPEG |
| `uploadMedia(cell, url [, field [, auth [, retries]]])` | يرفع الملفات المختارة في `cell` بطلب `multipart/form-data` واحد (الحقل الافتراضي `file`). `auth` يُرسَل كترويسة `Authorization` (مثل `"Bearer ..."`، بلا CR/LF). **`retries`** (0–5، الافتراضي 2) إعادة محاولة تلقائية بتراجع أسّي 1s/2s/4s.. عند فشل الشبكة أو 5xx/408/429؛ أخطاء 4xx لا تُعاد |
| `cancelUpload(cell)` | يقطع رفعاً جارياً ويُبقي الاختيار (الحالة تعود `picked`) |
| `clearMedia(cell)` | يمسح الاختيار |
| `removeMedia(cell, index)` | يحذف الملف رقم `index` (من 0) من اختيار متعدد |

## خلايا Warp المشتقة (تُزرَع تلقائياً من أول رسم)
`cell` (مسار أول ملف — يصلح لـ `Image src=`) · `_name` · `_mime` · `_size` (بايت) · `_sizeText` · `_kind` ·
`_count` · `_all` (JSON لكل الملفات) · `_status` (`idle|picked|uploading|done|error`) · `_progress` (0–100) ·
`_error` · `_attempt` (رقم محاولة الرفع: 1 الأولى، 2+ إعادة) · `_response` (ردّ الخادم، حتى 4KB).

مثال كامل: `examples/samples/indsin_media_demo.rin`.

## الكاميرا والتصغير والإعادة
- **الكاميرا**: `pickMedia("camera", photo)` تستخدم تطبيق الكاميرا عبر عقد النظام (TakePicture/CaptureVideo) + FileProvider (مسار `indsin_capture` في `file_paths.xml`). **لا تعلن صلاحية `CAMERA` في المانيفست** — بدون إعلانها لا تُطلب صلاحية وقت التشغيل؛ إعلانها يجعل أندرويد يطلبها ويفشل الالتقاط بدونها.
- **التصغير**: يُطبَّق على JPEG/PNG/WebP/HEIC فقط (GIF/SVG لا تُمَسّ). يحترم اتجاه EXIF، ويُسقط بيانات EXIF (ومنها الموقع). حدّ `maxMb` يُفحص بعد التصغير. إن لم يصغر الملف ولم تلزم المعالجة يبقى الأصل.
- **الإعادة**: `cancelUpload` يوقف الانتظار والمحاولات أيضاً.

## الأمان
- المنتقي هو Storage Access Framework: **لا صلاحيات** مطلوبة. الملفات تُنسخ إلى `<مشروع>/media/` باسم معقَّم وفريد.
- المحرّك يعيد التحقق من كل ملف: مسار نسبي بلا `..`/مطلق/مخطط، تطابق النوع، حدّ الحجم.
- الرفع `http/https` فقط، بلا بيانات اعتماد في الرابط، بلا متابعة إعادة توجيه، ولا يرفع إلا ملفات داخل جذر المشروع.
- رفض الرفع إن لم يوجد اختيار أو كان رفع آخر لنفس الخلية جارياً.
- ملاحظة: `http://` (غير مشفّر) قد يحجبه أندرويد 9+ ما لم تسمح به إعدادات الشبكة؛ يظهر الخطأ في `_error`.

## البنية
`rin_indsin_media.h` (النموذج/التحقق/الخلايا) ← `rin_indsin_needle.h` (يُخرج `TapResult.pick/.upload`) ←
`rin_indsin_c_api.cpp` (حقل `"media"` في غلاف النقر + `rin_indsin_session_media_picked/_progress`) ← JNI ←
`IndsinPreviewManager` ← `IndsinMediaHost.kt` (المنتقي + الرفع بالبث مع تقدّم). الاختبار: `tools/test_indsin_media.cpp`.

# CHANGES_INDSIN_MEDIA — اختيار الوسائط ورفعها في indsin

## الجديد
- أفعال: `pickMedia` · `uploadMedia` · `clearMedia` · `removeMedia` + خلايا Warp مشتقة (حالة/تقدّم/خطأ/ردّ).
- محرّك: `indsin/rin_indsin_media.h`، تعديلات في `rin_indsin_needle.h` / `_actions.h` / `_pipeline.h` / `_c_api.*`، ودالتان JNI.
- أندرويد: `IndsinMediaHost.kt` (منتقي SAF بلا صلاحيات، نسخ آمن إلى `media/`، رفع multipart بالبث مع تقدّم)، وربط في `IndsinPreviewActivity`.
- وثائق `docs/indsin_media.md`، مثال `examples/samples/indsin_media_demo.rin`، اختبار `tools/test_indsin_media.cpp`.

- تطوير: `cancelUpload(cell)`، وسيط `auth` (ترويسة Authorization)، وضع `"append"` للاختيار المتعدد المتتابع.
- إصلاح: رفع ثانٍ مرفوض كان يكتب `error` فوق حالة الرفع الجاري (كشفه الاختبار).

- **الكاميرا**: `pickMedia("camera" | "camera:video", ...)` عبر عقود النظام + FileProvider، بلا صلاحية.
- **تصغير/ضغط الصور**: وسيطا `maxDim` و`quality` (احترام EXIF، فك بـ inSampleSize، إسقاط EXIF).
- **إعادة المحاولة**: وسيط `retries` (افتراضي 2) بتراجع أسّي + خلية `_attempt`؛ 4xx لا تُعاد.
- ملف جديد معدَّل: `app/src/main/res/xml/file_paths.xml` (مسار `indsin_capture`).

## تحقق
- `tools/test_indsin_media.cpp`: 26/26 (مبني بـ g++ من مصدر المشروع) + `test_indsin_actions` ما زال ينجح.
- `rin_indsin_c_api.cpp` يمرّ بـ `-fsyntax-only`.
- **لم يُترجَم كود Kotlin/JNI ولم يُشغَّل على جهاز** (لا Gradle/NDK هنا) — جرّب بناء APK واختبر المنتقي والرفع فعلياً.

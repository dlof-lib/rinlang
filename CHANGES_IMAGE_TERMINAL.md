# CHANGES_IMAGE_TERMINAL — طباعة الصور في الـ terminal كنص
- جديد: `printImage(path, width?, mode?)` · `imageAnsi(...)` · `imageInfo(path)` — صورة ← نصف-كتل 24-bit أو رمادي (blocks/ascii). انظر `docs/image-terminal.md`.
- جديد: `app/src/main/cpp/rin_image_ansi.cpp` (مضمَّن في نهاية `rin_interpreter.cpp` كبقية دفعات natives) · `app/src/main/cpp/third_party/stb_image.h` v2.30 (Public Domain/MIT؛ PNG/JPEG/BMP/GIF فقط، بلا stdio).
- معدَّل: `rin_interpreter.h` (تصريح `registerNativesImage`) · `rin_interpreter.cpp` (استدعاء التسجيل + `#include`) · `syntaxes/rin-keywords.json` (الأسماء الثلاثة) · `scripts/intellisense_docs.json` + `rin_intellisense_data.h` (إكمال وتوقيع للدوال الجديدة).
- اختبار: `tests/image/` (صورة 16×12 + `.rin` + `.expected` بوضع ascii/blocks الحتمي).
- التحقق هنا: بناء CLI كاملاً على g++ (Linux) وتجربة PNG/JPEG/BMP/GIF وكل الأوضاع وكل رسائل الخطأ يدوياً + الاختبار أعلاه. لم يُجرَّب: بناء أندرويد/ويب/Windows (الكود يستعمل ioctl فقط خارج Windows)، ولا عرض الألوان على طرفية حقيقية.
- غير منفَّذ: صيغة `print image "x.png"` داخل المحلل (الدوال تغطي الحاجة)، وعرض ANSI داخل كونسول أندرويد (يستعمل ascii تلقائياً).

## الدمج مع التحديث 24
- لا `:image`: التحديث 24 فيه `:img` و`print.image` و`imageText`، فبقيت دوال `printImage/imageAnsi/imageInfo` فقط (تكمّلها بفكّ JPEG/GIF مدمج).
- إصلاح IntelliSense: إكمال سمات `print.image(...)` (mode/cols/invert/color/caption/if) + توثيق `imageText` + اختباران (115/115).

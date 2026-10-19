# CHANGES_IMAGE_DRAW — إنشاء الصور ورسمها وحفظها PNG + log كصورة
مبنيّ فوق rinlang-main_25 + (changed, _29, _31). الجديد: `img*` (27 دالة) — انظر docs/image-draw.md.
- جديد `app/src/main/cpp/rin_image_draw.cpp` (يُضمَّن في نهاية rin_interpreter.cpp بعد rin_image_ansi.cpp): لوحة RGBA، رسم منعَّم (خط/مستطيل مدوّر/دائرة/قطع/مضلّع/نص/تدرّج/فيضان)، مرمِّز PNG (مرشّحات تكيّفية + zlib، RGB أو RGBA)، BMP (24/32 بت)، PPM، نص 5×7 + خط rinfont العربي التقريبي، `imgFromText/imgFromLog`.
- معدَّل `rin_interpreter.cpp`: تسجيل `registerNativesImageDraw()`، `#include`، وفي `logSave` فرع للامتدادات png/bmp/ppm (يرسم السجلّ صورة). `rin_interpreter.h`: التصريح. `rin_image_ansi.cpp`: `printImage/imageAnsi` تقبلان معرّف لوحة.
- IntelliSense: `syntaxes/rin-keywords.json` + `scripts/intellisense_docs.json` + `rin_intellisense_data.h` (مُولَّد بـ gen_intellisense_data.py).
- اختبار `tests/image/image_draw.{rin,expected}` (حفظ ثم إعادة تحميل ومقارنة بكسلات، شفافية، تحويلات، نص، log).
- التحقق (g++ لينكس، CLI حقيقي): الاختبار الجديد متطابق على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · image_terminal · editor 127/127 + intellisense 115/115 · ملفات PNG/BMP/PPM الناتجة فُتحت بـ PIL. لم يُبنَ APK/الويب/Windows.
- حدود: النص العربي تقريبي (بلا خط مضمَّن/وصل حروف)؛ لا كتابة JPEG/GIF.

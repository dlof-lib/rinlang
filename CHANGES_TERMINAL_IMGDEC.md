# `:img` بلا ImageMagick — مفكّك صور مدمج

**الهدف:** `:img` / `rin img` لا يعتمدان على أداة خارجية، فيعملان على أندرويد (RinStudio) وعلى لينكس بلا تثبيت أي شيء.

- **جديد** `cli/linux/src/terminal/rin_imgdec.h` — header-only، يعتمد على zlib فقط (مربوط أصلاً في CMake لينكس وأندرويد، فلا تغيير في CMake):
  PNG (كل أنماط الألوان، أعماق 1–16 بت، Adam7، tRNS) · JPEG (Baseline + Progressive، 4:4:4/4:2:2/4:2:0، فواصل restart، تدوير EXIF، استيفاء خطي للألوان) ·
  GIF (الإطار الأول، شفافية، interlace) · BMP (1/4/8/16/24/32 بت). مصغِّر بمتوسط المساحة مع دمج الشفافية على الأبيض.
- **حدود الذاكرة (للهاتف):** PNG/GIF/BMP حتى 25 ميغابكسل؛ JPEG فوق 12 ميغابكسل يُفكّ بمقياس 1/8 (DC فقط) فتُقرأ صور الكاميرا الضخمة بذاكرة صغيرة؛ قنبلة PNG تُرفض فوراً.
- **معدَّل** `rin_terminal.cpp`: `loadImagePixels` يجرّب المدمج أولاً؛ ImageMagick احتياطي لـ webp/tiff فقط (أو ملف غريب فشل المدمج في فكّه). بدونه تظهر رسالة واضحة.
- **معدَّل** `EmbeddedTerminal::setColumns` + JNI `nativeSetColumns` + `RinNativeTerminal.columns` + `RinTerminalView.measureColumns()`:
  عرض `:img`/`:ocr` الافتراضي يتبع عرض شاشة الهاتف الفعلي (كان 80 عموداً فيلتفّ السطر).
- أُعيد حارس `:indsin` داخل المحرر (`cmdIndsin`) بعد أن غاب من النسخة المرفوعة (يحتاج tty).
- **اختبارات:** قسم img في `tests/terminal/run.sh` صار لا يشترط ImageMagick ويثبت الرسم مع `PATH` فارغ؛ أداتا `imgdec_dump.cpp` و`imgdec_compare.py` لمقارنة المفكّك مع PIL.

## ما تحقّقتُ منه فعلاً
- **دقة:** 41 صورة اختبار (كل أنماط PNG بما فيها 16 بت وAdam7 وpalette/tRNS، GIF عادي/شفاف/interlaced، BMP بكل الأعماق، JPEG أساسي/تدريجي/رمادي/تقليل عينات/restart/EXIF) مقابل PIL:
  PNG وGIF وBMP **متطابقة بايتاً ببايت**؛ JPEG متوسط الخطأ ≤ 0.1 من 255 (أقصاه 4).
- **متانة:** 410 ملفاً مقطوعاً/تالفاً عشوائياً تحت ASan+UBSan بلا أي خطأ ذاكرة أو سلوك غير معرَّف؛ قنبلة PNG (60000×60000) تُرفض فوراً.
- **أداء (-O2، لينكس):** JPEG 12 ميغابكسل كامل ≈ 0.95 ث؛ 20 ميغابكسل بمقياس 1/8 ≈ 0.05 ث.
- `:img` يعمل مع `PATH` خالٍ من ImageMagick في `rin img` وفي `EmbeddedTerminal` (مسار RinStudio)، وكل اختبارات `tests/terminal/run.sh` (20) ناجحة.
- **لم أستطع:** بناء أندرويد (NDK) ولا ترجمة Kotlin، ولا قياس السرعة على جهاز فعلي (الهاتف أبطأ من هذا القياس). WebP/TIFF غير مدعومين مدمجاً.

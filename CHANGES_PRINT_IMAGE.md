# CHANGES_PRINT_IMAGE — طباعة الصور كنص من داخل اللغة
مبنيّ فوق rinlang-main__23 (قوالب print وخطوطه) + حزمة Unicode/OCR (`:img` و`:ocr` وRinAnsi). لا تعديل على ملفاتها.

- `print.image(path, mode=, cols=, invert=, color=, caption=, if=)` و`imageText(path, cols, mode, invert, color)` تطبعان الصورة كنص (docs/print-image-terminal.md).
  الصيغة القديمة `print.image(path, caption=, width=)` (سطر الأندرويد) لم تتغيّر؛ يكفي ذكر أي من `mode cols invert color` ليُرسم النص.
- أنماط: `blocks`/`half` (نصف كتلة ▀ بألوان 24-بت) · `ansi256` · `shade` · `ascii` · `braille`. بلا ألوان (غير طرفية) يتحوّل blocks إلى shade رمادي نظيف.
- `rin_termimg.h` (جديد): مفكّك PNG (كل الأنماط وأعماق البتات وAdam7 وtRNS) وPPM/PGM وBMP بلا اعتماديات إلا zlib؛ JPEG/GIF/WebP عبر ImageMagick إن وُجد.
- التفكيك في المحلل إلى `print` عادي لنداء `imageText` — لا عقدة AST جديدة، فيعمل على المفسّر وRinVM وقوالب `@tmpl ... print.image(...)`.
- الملفات: جديد `app/src/main/cpp/rin_termimg.h` · `docs/print-image-terminal.md` · `tests/print/print_image_terminal.{rin,expected}` · `tests/print/img/*` ·
  معدَّل `app/src/main/cpp/rin_parser.cpp` (سمات print.image الجديدة) و`app/src/main/cpp/rin_extra_natives5.cpp` (native `imageText`).

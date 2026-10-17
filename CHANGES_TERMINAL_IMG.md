# `:img` — عرض صورة بحروف Unicode في الطرفية

- جديد: `:img <صورة> [--mode half|braille|shade|ascii] [--width N] [--plain] [--invert]` داخل `rin terminal`، و`rin img <صورة> [...]` من الشل.
- معدَّل: `cli/linux/src/terminal/rin_terminal.{h,cpp}` (كتلة مستقلة قبل `IndsinArgs` + `cmdImg` + `runImgCommand` + سطر `:help` + إكمال Tab)،
  `cli/linux/src/main.cpp` (الأمر الفرعي `img`)، `docs/terminal.md`، `tests/terminal/run.sh` (قسم img). لا تلمس `:ocr` ولا المحرّك.
- (تحديث: صار الفكّ بمفكّك مدمج — انظر CHANGES_TERMINAL_IMGDEC.md؛ ImageMagick احتياطي لـ webp/tiff فقط) فكّ الصورة عبر ImageMagick بمصفوفة argv مباشرة (بلا shell)، والصيغة تُعرَّف من البايتات (png/jpg/gif/bmp/webp/tiff) فتُرفض SVG/MVG المتنكّرة؛
  مهلة 30/60 ثانية، وسقف للحجم (400 عمود، 1600 صف بكسل) ضد الصور الضخمة.
- التحقق: ترجمة g++ بلا تحذيرات (-Wall -Wextra)، وتجربة فعلية لـ png/jpg/gif/RGBA/مسار بمسافة/صورة طويلة جداً/صغيرة جداً/أخطاء.
  لم يُجرَّب: عرض نمط half بألوان 24-bit على طرفية حقيقية (اختُبر خرج ANSI فقط)، CMake، macOS/Windows.

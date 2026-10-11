# CHANGES_INDSIN_ICONS — مكتبة أيقونات متجهية لـ indsin

انظر `docs/indsin_icons.md`.

## جديد
- `tools/icons/` (icons_def.py، gen_indsin_icons.py، gallery_template.html، raster.py، test_icons.py)
- `assets/indsin-icons/` (svg/، sprite، icons.json، gallery.html)
- `lib/indsinicons.og.rin` · `app/src/main/cpp/indsin/rin_indsin_iconpaths.h`
- `tests/indsin_icons_tests.rin` · `tools/test_indsin_icon_vectors.cpp` · `examples/indsin_icons_demo.rin`

## معدَّل
- `app/src/main/cpp/indsin/rin_indsin_icons.h`: واجهة المتجهات (vectorPath/svg/registerVector)؛ سلوك resolve/has القديم لم يتغيّر.
- `app/src/main/assets/indsin/indsin_object.html`: كتلة ICON-VECTORS، `iconHtml()`، قاعدة `.ix-svg`، والمدقق يعرف الأسماء المتجهية.

## تحقق
rin tests/indsin_icons_tests.rin 12/12 · test_icons.py كله ناجح (يشمل مطابقة Rin لـ Python حرفياً) · اختبار C++ الجديد ناجح ·
test_indsin_icons القديم ALL PASS · selfTest للمحرّك 65/65. لم يُبنَ APK/WASM.
مطبَّق على rinlang-main__3_ وملفاه المعدّلان مطابقان تماماً في rinlang-main__4_.

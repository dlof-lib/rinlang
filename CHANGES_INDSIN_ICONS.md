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

## دمج في RinStudio (إضافة)
- مكتبة مدمجة: `rin_stdlib_libs.h` (+ `tools/embed_indsinicons.py`)؛ اختُبر `@import` من مجلد بلا lib/ بمفسّر مبنيّ.
- جديد: `IconLibraryActivity.kt`، `widgets/IndsinIcons.kt`، `widgets/IndsinIconView.kt`، `assets/indsin-icons/icons.json`،
  `activity_icon_library.xml`، `item_icon_tile.xml`، `dialog_icon_detail.xml`، `ic_lib_indsinicons.xml`،
  `bg_featured_icons_card/bg_search_field/bg_lib_chip_selected/bg_btn_white/bg_icon_tile/bg_icon_preview_box.xml`،
  `assets/branding/indsinicons_icon.svg`.
- معدَّل: `LibrariesActivity.kt` (بحث + شرائح + بطاقة مميّزة)، `RinLibrary.kt` (LibCategory + مدخل المكتبة)،
  `activity_libraries.xml`، `item_builtin_library.xml`، `AndroidManifest.xml`، `strings_libraries.xml` (ar/en/es).
- لم يُبنَ APK ولم يُجرَّب الشكل على جهاز (لا Android SDK/Kotlin هنا): فُحصت الموارد والأقواس سكونياً فقط.

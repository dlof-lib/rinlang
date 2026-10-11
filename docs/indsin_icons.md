# مكتبة أيقونات indsin (متجهية)

187 أيقونة مرسومة من الصفر على شبكة 24×24، خط سماكته 2 وأطراف دائرية، كل أيقونة مسار SVG واحد
(أوامر `M L H V C A Z` فقط). المصدر الوحيد: `tools/icons/icons_def.py`، والتوليد:
`python3 tools/icons/gen_indsin_icons.py`.

| الناتج | الاستعمال |
|---|---|
| `assets/indsin-icons/svg/*.svg` | ملف لكل أيقونة |
| `assets/indsin-icons/indsin-icons.svg` | sprite (`<use href="#ic-home">`) |
| `assets/indsin-icons/icons.json` | فهرس (اسم، فئة، بدائل، مسار) |
| `assets/indsin-icons/gallery.html` | معرض مستقل بحث + نسخ |
| `lib/indsinicons.og.rin` | مكتبة Rin (`ic_*`) |
| `app/src/main/cpp/indsin/rin_indsin_iconpaths.h` | جدول C++ (`iconRegistry().vectorPath(name)`) |
| `indsin_object.html` | `@view.Icon` يرسم SVG حقيقياً في معاينة الويب |

## من Rin
```rin
@import "lib/indsinicons.og.rin";
print ic_svg("home", nil);
print ic_svg("heart", {"size": 32, "color": "#e11d48", "filled": true});
print ic_search("gear");                       // ["settings"]
ic_register("my-logo", "brand", "M4 4H20V20H4Z");
```
الدوال: `ic_count ic_names ic_has ic_resolve ic_aliasesOf ic_category ic_categories ic_categoryName
ic_byCategory ic_search ic_path ic_svg ic_svgn ic_html ic_dataUri ic_sprite ic_symbol ic_snippet
ic_audit ic_register ic_validPath`. خيارات `opts`: `size color stroke filled title class`.
الاسم المجهول يعطي أيقونة `help` مع `data-missing` بدل فراغ صامت.

## في واجهة indsin
```
@view.Icon=ico
    icon="settings";   // أو الاسم البديل gear
    size="lg";
.end/view
```
الأسماء القديمة (27 اسماً في سجل المحرّك + أسماء معاينة الويب) كلها محفوظة وتُحلّ إلى أيقونات.

## حدود معروفة
الـrasterizer الأصلي في C++ ما زال بلا عملية رسم مسارات، فيبقى الرسم الأصلي على الرموز النصية؛
الجدول جاهز لتلك العملية. لم يُبنَ APK/WASM.

## الاختبارات
`RIN_BIN=rin python3 tools/icons/test_icons.py` · `rin tests/indsin_icons_tests.rin` ·
`g++ -std=c++17 -Iapp/src/main/cpp/indsin tools/test_indsin_icon_vectors.cpp`.

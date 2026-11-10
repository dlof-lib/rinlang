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
```kotlin
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
الـrasterizer الأصلي يرسم المسارات الآن (انظر «رسم المسارات الأصلي» أدناه)؛ الأيقونات المدمجة تبقى بالرمز النصي افتراضياً حفاظاً على التوافق؛
الجدول جاهز لتلك العملية. لم يُبنَ APK/WASM.

## الاختبارات
`RIN_BIN=rin python3 tools/icons/test_icons.py` · `rin tests/indsin_icons_tests.rin` ·
`g++ -std=c++17 -Iapp/src/main/cpp/indsin tools/test_indsin_icon_vectors.cpp`.

## داخل تطبيق RinStudio
- **مكتبة مدمجة:** `lib/indsinicons.og.rin` مضمَّنة في المفسّر (`rin_stdlib_libs.h`) فيعمل `@import "lib/indsinicons.og.rin";`
  في أي مشروع دون رفع ملف. بعد أي تعديل: `python3 tools/icons/gen_indsin_icons.py && python3 tools/embed_indsinicons.py`.
- **شاشة المكتبات (`LibrariesActivity`) بتصميم جديد:** بحث حيّ، شرائح فئات (واجهات/بيانات/لغات وأدوات/ألعاب وفيزياء/نظام وأمان)،
  بطاقة مميّزة لمكتبة الأيقونات (معاينة ستّ أيقونات حقيقية + تصفّح + إدراج `@import`)، وحبّة فئة على كل مكتبة مدمجة.
- **متصفّح الأيقونات (`IconLibraryActivity`):** شبكة 187 أيقونة تُرسم من مسارات SVG عبر `IndsinIconView`، بحث بالاسم أو البديل،
  وتفاصيل مع «إدراج في الكود» (يُدرج سطر `@view.Icon` عند المؤشر).
- **أيقونة المكتبة:** `res/drawable/ic_lib_indsinicons.xml` (شبكة 2×2 من البلاطات) و`assets/branding/indsinicons_icon.svg`.
- لم يُبنَ APK هنا (لا Android SDK ولا مترجم Kotlin في بيئة العمل): تحقّق من البناء والشكل على جهاز قبل الإصدار.

## استيراد أيقونة من api
يمكن جلب أيقونة من ردّ api وتسجيلها في سجلّ indsin وقت التشغيل (المصدر: SVG كامل، أو مسار `d`، أو `data:image/svg+xml` base64/مرمَّز).
يُستخرج كل `d` وتُحوَّل `circle/rect/line/polyline/polygon` إلى مسارات، ويُرفض أي مصدر غير صالح (سكربت، أوامر مجهولة، أكبر من 20000 حرف).

**تلقائياً مع container.api:** عرِّف في الحاوية `icon` (و/أو `svg`)، واختيارياً `iconName` و`iconGlyph`:
```
@container.api=pack
    let data = nil; let icon = nil; let svg = nil;
    let iconName = "bolt"; let iconGlyph = "⚡";
    route method="GET" path="/bolt" status=200 body={"svg": "<svg><path d='M13 2L3 14H12L11 22L21 10H12Z'/></svg>"};
.end/container.api
callApi("pack", "GET", "/bolt");     // يسجّل "bolt" ويملأ pack.icon و pack.svg
```
```
<Icon icon={pack.icon} />            // أو <IconButton icon={pack.icon} .../>
```
الاسم: `iconName` ثم `name`/`id` في الردّ ثم اسم الحاوية. الردّ الفاشل (غير 2xx) أو غير الصالح لا يمسّ الأيقونة المسجَّلة.

**يدوياً:** `icons.import(name, src[, glyph])` · `icons.fromApi(container, name[, glyph])` (من آخر `data` ناجحة) ·
`icons.has(name)` · `icons.path(name)` · `icons.svg(name[, size, color])` · `icons.names()`.

## رسم المسارات الأصلي (DrawOp::STROKE_PATH)
الـrasterizer الأصلي صار يرسم المسارات فعلاً: `M L H V C S Q T A Z` (مطلقة ونسبية) تُسطَّح ثم تُرسم خطاً بأطراف ومفاصل دائرية
(مع anti-aliasing)، والتصدير: `"op":"stroke_path"` في JSON، و`<svg><path>` في HTML.
- الأيقونات **المستوردة** تُرسم متجهة افتراضياً. المدمجة تبقى بالرمز النصي كما كانت، إلا مع `vector="true"`
  (`<Icon icon="home" vector="true" />`)؛ و`vector="false"` يفرض الرمز النصي. `stroke="2"` سماكة الخط بوحدات الشبكة 24.
- `iconRegistry().vectorDefault = true` يجعل كل أيقونة لها متجه تُرسم متجهة.
- لم يُنفَّذ بعدُ: `IconButton` ما زال يرسم رمزه النصي (يُرسم داخل نص الزرّ)، ولا يوجد fill للمسارات (خطّ فقط).

## حزم أيقونات خارجية (api وروابط)
```
icons.importPack(pack[, prefix])      // يعيد أسماء ما سُجِّل
icons.fromUrl(url[, nameOrPrefix])    // https:// (أو localhost)؛ SVG واحد (يلزم name) أو حزمة
```
صيغ الحزمة: خريطة `{اسم: مصدر}` · `{"icons": خريطة|مصفوفة}` · مصفوفة `{name|id, svg|path|d}` · نص JSON · sprite (`<symbol id="x">…</symbol>`).
الإدخالات غير الصالحة تُتجاوَز، والحدّ 2000 أيقونة. مع container.api عرّف الحقل `iconList` (و`iconPrefix` اختيارياً)
فيُستورد كل ردّ ناجح كحزمة ويُملأ `iconList` بأسماء ما سُجِّل.

## مكتبات Rin من رابط
```
@import "https://example.com/libs/utils.og.rin";
@import "https://example.com/libs/utils.og.rin#sha256=<hex>";    // تثبيت السلامة (موصى به)
```
https فقط (http لـ localhost/127.0.0.1 للاختبار)، حدّ 2MB، نجاح 2xx، وعدم تطابق `#sha256` يُرفض. `RIN_REMOTE_IMPORT=0` يعطّل الميزة.
تُحمَّل مرة واحدة لكل رابط في التشغيل. **تنبيه أمان:** المكتبة تُنفَّذ بصلاحيات برنامجك — ثبّت sha256 ولا تستورد إلا من مصدر تثق به.

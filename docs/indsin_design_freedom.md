# حرية التصميم في indsin — الحجم والألوان وخلفية الشاشة

ثلاث حريّات جديدة، كلها تعمل في المحرّك نفسه فتصل لأندرويد وسطح المكتب وCLI معاً:

1. **خلفية الشاشة (canvas)** — كانت رماديّة ثابتة (`#121212` في HTML و`18` في المُرسِم)؛ صارت لوناً قابلاً للضبط.
2. **حجم الشاشة والعناصر** — أي حجم جهاز، وأحجام نسبية، ونسبة أبعاد.
3. **الألوان** — ثيم كامل من لون واحد، أو ثيم بأدوار مخصّصة.

المثال الكامل: `examples/samples/indsin_design_freedom.rin`.

## 1) لون الخلفية الافتراضي

ترتيب الأولوية (الأول يغلب):

| # | المصدر | كيف |
|---|---|---|
| 1 | الشاشة نفسها | `<Screen bg=#0B1020>` (أو `canvas="…"` على الجذر) |
| 2 | **الافتراضي من المضيف** | `rin_indsin_session_set_default_background(s, "#1E1E2E")`، أو في أندرويد قائمة الجهاز ← «خلفية الشاشة» |
| 3 | الثيم | دور `background` في الثيم الفعّال (`<Theme>` / `<Palette>` / `@theme`) |

* يقبل أي لون يعرفه المحرّك: `#RGB` `#RRGGBB` `rgb()` `hsl()` وأسماء CSS، أو اسم دور (`surface`).
* المسح: `set_default_background(s, "")` يعيد إلى لون الثيم.
* حاويات التخطيط (`Column/Row/Stack/Spacer`) **شفّافة** ما لم تُعطَ `bg=`؛ كانت تملأ نفسها بلون الثيم فتحجب
  لون الشاشة (وترسم مربعاً غامقاً خلف صفوف داخل `Card`).
* غلاف النتيجة (tap/render) يحمل الآن `"canvas":{"width","height","background","source"}`؛ و`rin_indsin_session_canvas_json()` يعطيه وحده.

## 2) الحجم

### حجم الشاشة — `<Screen>`

```rin
<Screen#app size=phone bg=#0B1020> … </Screen>
<Screen#t size="1024x768"> … </Screen>          <!-- حجم مخصّص -->
<Screen#l size=phone_landscape> … </Screen>     <!-- يقلب المحورين -->
<Screen#w width=320 height=200> … </Screen>     <!-- لكل محور على حدة -->
```

| سمة الوسم | تُكتب كـ | المعنى |
|---|---|---|
| `size=` | `screen=` | إعداد جاهز أو `"WxH"` (+ لاحقة `_landscape`) |
| `width=` | `canvas_width=` | عرض الشاشة (رقم أو `%` أو `vw`) |
| `height=` | `canvas_height=` | **حدّ أدنى** لارتفاع الشاشة؛ المحتوى الأطول يمدّها |
| `bg=`/`background=` | `canvas=` | لون الخلفية |

الإعدادات: `watch` 184×224 · `phone_small` 360×640 · `phone` 390×844 · `phone_large` 430×932 · `foldable` 673×841 ·
`square` 600×600 · `tablet` 768×1024 · `tablet_large` 1024×1366 · `laptop` 1280×720 · `desktop` 1280×800 ·
`desktop_wide` 1440×900 · `tv` 1920×1080. بلا إعلان يبقى السلوك القديم (عرض المضيف).
المضيف: `rin_indsin_session_resize(s, width, viewportHeight)`. في أندرويد تتبع المعاينة عرض اللوحة المعلَن تلقائياً.

### حجم العناصر

`width` و`height` و`min_*` و`max_*` تقبل الآن:

| القيمة | المعنى |
|---|---|
| `120` / `120px` | بكسل (كما كان) |
| `50%` | نسبة من الحاوية الأب (داخل `Row` تُحسب على عرض الصف، لا على المحور غير المحدود) |
| `full` `half` `third` `two-thirds` `quarter` | اختصارات: 100% · 50% · 33% · 67% · 25% |
| `60vw` `50vh` `100vmin` `100vmax` | نسبة من **الشاشة** (viewport) لا من الأب |

`aspect="16:9"` (أو `1.5`) على أي عنصر: الارتفاع يتبع العرض المحدَّد (صريح أو مفروض من الأب)؛ `height=` الصريح يغلب.

> في الوسوم تُقتبَس `50%` و`60vw` و`390x844` تلقائياً. في صيغة `@view` القديمة اكتبها بين علامتي اقتباس: `width="50%";`.

## 3) الألوان

```rin
<Palette seed=#6C5CE7 dark />          <!-- ثيم كامل من لون واحد (WCAG)، يُفعَّل تلقائياً؛ light للفاتح -->
<Theme#Brand primary=#FF00AA background=#101820 active />   <!-- أدوار بعينها فوق الثيم الحالي -->
```

* `<Palette seed=… [dark|light]>` ⇒ `@theme=… seed="…"; active=true;` — يولّد primary/secondary/surface/text/border…
  بتباين مضمون، ثم يمكن تجاوز أي دور بعده بـ `<Theme>`.
* `<Theme#Name role=#hex … [active]>` يبدأ من ثيم بالاسم نفسه إن وُجد (Midnight, Ocean…) وإلا من الفعّال، ويبدّل الأدوار المذكورة فقط.
* `seed=` يعمل أيضاً في كتلة `@theme=Name seed="#6C5CE7"; dark=true; active=true; .end/theme`.

## 4) تدرّجات لونية (جديد)

```rin
<Screen#app size=phone bg="#0B1020,#4B2A8A,160">           <!-- خلفية الشاشة متدرّجة: لون1,لون2[,زاوية] -->
  <Card#c width=full height=90 radius=18 gradient="#FD79A8,#6C5CE7,90" />   <!-- أي عنصر -->
  <Button#b gradient="#FDCB6E,#E17055,135">Warm</Button>
</Screen>
```

* من لونين إلى 16 لوناً بمسافات متساوية: `gradient="#FF0000,#00FF00,#0000FF,90"` (آخر رقم = الزاوية).
* الخلفية الافتراضية من المضيف تقبل تدرّجاً أيضاً: `set_default_background(s, "#0B1020,#4B2A8A,160")` (وفي أندرويد «تدرّج الشفق/المحيط» من القائمة).
* الزاوية بنمط CSS: `180` (الافتراضي) من الأعلى للأسفل، `90` من اليسار لليمين، `135` قطري.
* الفواصل داخل `rgb()` / `hsl()` محترمة؛ اقتبس القيمة في الوسوم لأنها تحوي فاصلة: `bg="#a,#b"`.
* يعمل في المُرسِم الأصلي (PNG) وتصدير HTML (`linear-gradient`) وخطة الرسم JSON (`color2` و`angle`) وأندرويد (بزاوية أيضاً).
* `canvas_json` يعيد `background2` و`angle` عند تدرّج الشاشة. `canvas_json` يعيد أيضاً `stops` (كل المحطات).
* كان `gradient=` يعمل في أندرويد فقط (عمودياً)؛ صار يظهر في كل المضيفين، فتتغيّر لقطة `indsin_showcase` عن قبل (التغيير مقصود).

## 5) إصلاح: `onTap={x = expr}`

صيغة التعيين المباشر الموثّقة في الوسوم (`onTap={count = count + 1}`) كانت ترجع خطأ
«onTap must be a handler name or call». صارت تعمل في كل الإيماءات (`onTap` / `onLongPress` / `onDoubleTap` / hover)،
وتكتب خلية `warp` موجودة فقط؛ تعيين اسم غير معرَّف يُبلَّغ كخطأ صريح بدل أن يُتجاهل.

أمثلة كاملة: `examples/samples/indsin_app_full.rin` (تطبيق لوحة مهام) و`examples/samples/indsin_gradients.rin`.

## 6) ظلال أصلية: `elevation` و`shadow` و`shadowColor`

```rin
<Card#c elevation=lg />                                   <!-- none | xs | sm | md | lg | xl أو 0..5 -->
<Card#g bg=#6C5CE7 shadowColor=#6C5CE7 elevation=xl />    <!-- توهّج ملوّن -->
<Button#b shadow=14 shadowColor=#FD79A8 shadowY=6>Glow</Button>
```

| السمة | المعنى |
|---|---|
| `elevation=` | ظل من سلّم المستويات (نفس جدول `rin_indsin_system.h`)؛ كان يُقرأ ولا يُرسَم |
| `shadow=N` | الصيغة القديمة: تمويه N بكسل (إزاحة رأسية N/2.5، أسود ~47%) |
| `shadowColor=` | لون الظل/التوهّج (أي لون أو دور ثيم) |
| `shadowY=` | تجاوز الإزاحة الرأسية |

يُرسَم في المُرسِم الأصلي (PNG) وتصدير HTML (`box-shadow`) وخطة الرسم JSON (`shadow:{blur,dy,spread,color,alpha}`) وأندرويد.
عيّنتا `indsin_showcase` و`indsin_system_demo` تتغيّران عن قبل لأنهما تستعملان `shadow`/`elevation`. مثال: `examples/samples/indsin_shadows.rin`.

## 7) مفاهيم جديدة في الوسوم: `<Tokens>` و`<Style>` + تبديل الثيم

```rin
<Tokens brand=#6C5CE7 pad=20 />                            <!-- ثوابت تصميم → let brand = "#6C5CE7"; let pad = 20; -->
<Style#card radius=16 elevation=md bg=surface padding=14 /> <!-- نمط مسمّى قابل لإعادة الاستعمال -->
<Style#hero extends="card" bg={brand} />                    <!-- وراثة: السمات اللاحقة تغلب -->

<Screen#a size=phone padding={pad}>
  <Card#c1 class="card" />                                  <!-- class="a b": اللاحق يغلب السابق -->
  <Card#c2 class="hero" height=80 />                        <!-- السمة الصريحة على العنصر تغلب النمط -->
  <Button#t onTap={toggleTheme()}>Dark / Light</Button>
  <Button#s onTap={setThemeSeed("#E17055", false)}>Orange</Button>
</Screen>
```

* `<Tokens name=value …/>` تُستعمل كمتغيّرات `{brand}` في أي سمة (هي `let` عادية في Rin، فلا تعمل مع `pitokc`).
* `<Style#name …/>` يُوسَّع وقت الترجمة؛ `class` غير معلنة ⇒ خطأ صريح بسطرها، وكذلك `extends` على نمط غير معرَّف.
* أفعال الثيم وقت التشغيل (تعمل في `onTap` وكل الإيماءات، بلا `fun`):
  `setTheme("Midnight")` · `toggleTheme()` (Dark ⇄ Light) · `setThemeSeed("#6C5CE7" [, dark])`.
  تتغيّر الألوان والخلفية في الإطار نفسه. تنبيه: سجلّ الثيمات عام على مستوى العملية، فيشمل الجلسات كلها.

مثال: `examples/samples/indsin_styles_themes.rin`.

## الاختبار

```sh
# محرّك + C API (يحتاج مصادر الـ runtime كما في CMake: هدف indsinc)
g++ -std=c++23 -I app/src/main/cpp tests/indsin_design_freedom/design_freedom_test.cpp <engine objs> -lz -o dft && ./dft
# مترجم الوسوم (header-only)
g++ -std=c++17 -I app/src/main/cpp tests/indsin_tags/tags_test.cpp -o /tmp/tags_test && /tmp/tags_test
# معاينة بلا واجهة
rin_indsin_preview examples/samples/indsin_design_freedom.rin --bg "#223344" --canvas --png out.png
```

## ما تغيّر في الملفات

`rin_indsin_tokens.h` (كلمات الحجم، `vw/vh`، `aspect`، الإعدادات، `resolveCanvasSize`، `seed=`) ·
`rin_indsin_layout.h` (ارتفاع الشاشة الأدنى، `aspect`، نسب داخل `Row`) · `rin_indsin_paint.h` (`resolveCanvas`/`paintFrame`، حاويات شفافة) ·
`rin_indsin_c_api.*` (الخلفية الافتراضية، `resize`، `canvas_json`، حجم اللوحة) · `rin_indsin_html.h` (لون اللوحة) ·
`rin_indsin_needle.h` (تعيين مباشر في المعالجات + أفعال الثيم) · `rin_indsin_tags.h` (`<Screen>/<Theme>/<Palette>`، اقتباس القيم النسبية) · أندرويد: `IndsinFabricView/PreviewActivity/PreviewManager/RinEngine` + JNI.

<div align="center">

<img src="../assets/branding/piano/piano_icon_192.png" width="96" alt="PIANO.PUO" />

# piano — مكتبة الرسم المتجهي لـ Rin

**مشهد واحد (`.puo`) ← SVG متطابق من Rin وC++ وJS**

</div>

```kotlin
@import "piano";

let c = pn_canvas(480, 270);
pn_theme(c, "dark");
pn_circle(c, 240, 135, 80, {"grad": ["#818cf8", "#22d3ee"], "shadow": true});
pn_text(c, 240, 142, "piano", {"size": 34, "anchor": "middle", "weight": 700});
pn_save(c, "hello.svg");      // أو hello.puo / hello.html
```

## الفكرة
المكتبة **مشهد** (قاموس JSON) فيه `defs` (تدرّجات، فلاتر، أنماط، قصّ) و`nodes` (شجرة أشكال). دوال `pn_*` تضيف عقداً، والمُصيِّر يحوّلها إلى SVG.
الصيغة موثّقة في [`schemas/piano-scene.schema.json`](../schemas/piano-scene.schema.json)، وامتداد ملف المشهد **`.puo`** (JSON).

| الطبقة | الملف | الدور |
|---|---|---|
| Rin (zpp) | `lib/piano.og.rin/` | الواجهة والمُصيِّر المرجعي |
| C++17 | `lib/piano.og.rin/native/piano.hpp` · `piano_cli.cpp` | تصيير `.puo` → SVG/HTML من سطر الأوامر |
| JavaScript | `lib/piano.og.rin/native/piano.js` | المعاينة الحيّة وأي موقع |
| JSON | `schemas/piano-scene.schema.json` · `data/themes.json` | المخطط والثيمات |
| HTML | `web/piano/index.html` | صفحة المعاينة الحيّة |
| indsin | `examples/piano/piano_studio.rin` | عرض الرسم داخل واجهة (`pn_data_uri`) |

`tools/piano_check.py` يضمن أن مخرجات الثلاثة **متطابقة بايتاً ببايت** على كل الأمثلة.

## الواجهة
كل دالة رسم: `pn_<شكل>(الأب, هندسة…, خيارات)`؛ الأب = `canvas` أو `group`، والخيارات قاموس (يمكن `{}`) — لا قيم افتراضية للمعاملات في Rin.

**اللوحة:** `pn_canvas(w,h)` · `pn_theme(c, اسم)` (`light dark ocean sunset neon mono piano`) · `pn_bg` · `pn_title` · `pn_font` · `pn_size`
**أشكال:** `pn_rect pn_rrect pn_circle pn_ellipse pn_line pn_polyline pn_polygon pn_path pn_text pn_image`
**مركّبة:** `pn_ngon pn_star pn_slice pn_ring pn_arc pn_arrow pn_grid pn_wave pn_spiral pn_smooth pn_label`
**مسارات:** `pn_pen` ثم `pn_move pn_lineto pn_quadto pn_cubicto pn_arcto pn_close` ثم `pn_path(c, pen, {})`
**تدرّج وفلاتر:** `pn_linear pn_radial` ← `"@id"` · `pn_shadow pn_glow pn_blur` · `pn_pattern` · `pn_clip pn_clip_rect pn_clip_circle` · `pn_ref(x)`
**تجميع وتحويل:** `pn_group pn_at pn_repeat pn_translate pn_rotate pn_scale pn_skewx pn_skewy pn_clone`
**حركة (SMIL):** `pn_animate pn_animate_values pn_spin pn_pulse`
**ألوان:** `pn_alpha pn_mix pn_lighten pn_darken pn_ramp pn_color(c, i)`
**مخططات:** `pn_bar_chart pn_line_chart pn_pie_chart pn_radar_chart pn_sparkline pn_gauge pn_legend pn_nice`
**إخراج:** `pn_svg pn_json pn_html pn_data_uri pn_save(c, path) pn_print` · **إدخال:** `pn_from_json pn_load`

### خيارات الأشكال المشتركة
`fill stroke sw op fo so dash cap join` · `grad:[ألوان] angle:90` · `shadow:true|"#لون"|{dx,dy,blur,c,op}` · `glow` · `blur` ·
`rot:درجات` · `tf` · `clip:"@id"` · `filter` · `id cls title(تلميح) anim`.
الألوان: `#rgb`, `#rrggbb`, `#rrggbbaa` (شفافية)، `none`، `@id`.

## التصميم
**تخطيط:** `pn_grid_cells(x,y,w,h,cols,rows,gap)` · `pn_split(x,y,w,h,[أوزان],gap,"row"|"col")` · `pn_pad(rect,p)` — تُرجع مستطيلات `{x,y,w,h}`.
**مكوّنات:** `pn_card` (`heading` `subtitle`) · `pn_button` (`variant`: solid/soft/outline/ghost) · `pn_badge` · `pn_avatar` · `pn_progress` · `pn_toggle` · `pn_input` · `pn_divider`
**إطارات:** `pn_phone` · `pn_browser` — تُرجع group محتوى مقصوصاً على الشاشة، وفيه `_screen = {x,y,w,h}`؛ ارسم داخله كأنه لوحة.
**خلفية:** `pn_mesh(c, w, h, [ألوان], {})` — تدرّجات ضبابية.
**مخططات انسيابية:** `pn_box` · `pn_connect` (منحنى) · `pn_elbow` (متعامد) — بأسهم وخط متقطع اختياري.
**أيقونات:** `pn_icon(c, "bell", x, y, size, {})` ، والأسماء في `pn_icon_names()` (16 أيقونة بخط على شبكة 24).
**أقنعة:** `pn_mask(c)` ثم `{"mask": pn_ref(m)}` · `pn_fade_mask(c, x, y, w, h, "right")` للتلاشي.
**عرض متجاوب:** `pn_display(c, w, h)` / `pn_display_w(c, w)` — يغيّر حجم العرض فقط (viewBox ثابت).

```kotlin
let ph = pn_phone(c, 60, 30, 250, 460, {});  let s = ph["_screen"];
pn_card(ph, s["x"] + 14, s["y"] + 100, s["w"] - 28, 96, {"heading": "٤٬٢٥٠ ر.س"});
pn_button(ph, s["x"] + 14, s["y"] + s["h"] - 130, s["w"] - 28, 42, "تحويل", {});
```

## الدمج مع indsin
واجهات indsin تعرض الصور عبر `<Image src=…>`؛ وهذه الدوال تُنتج المصدر مباشرة:

| الدالة | الناتج |
|---|---|
| `pn_chart_uri(kind, labels, data, w, h, theme, opts)` | مخطط `bar line pie donut radar spark gauge` كـ data-URI |
| `pn_icon_uri(name, size, color)` | أيقونة بلون محدد وخلفية شفافة |
| `pn_data_uri(c)` | أي مشهد piano |
| `pn_indsin_image(id, uri, w, h)` · `pn_indsin_snippet(c, id)` | نص وسم `<Image>` لتوليد ملفات indsin آلياً |
| `pn_tokens(theme)` | ألوان الثيم + `radius gap pad` لاستعمالها في سمات indsin |

وبما أن `src` يُعاد حسابه عند تغيّر `warp`، تتحرّك المخططات مع البيانات — انظر `examples/piano/piano_dashboard.rin` (لوحة بثلاثة مخططات وتبديل ثيم) و`piano_studio.rin`.
في صفحة المعاينة زر **وسم indsin** ينسخ `<Image>` لأي مشهد.

## مدمجة في اللغة
المكتبة مُضمَّنة في المفسّر (`rin_stdlib_libs.h`) فيعمل `@import "piano";` على أي منصة (أندرويد وغيره) بلا مجلد `lib`.
بعد أي تعديل في `lib/piano.og.rin/` شغّل: `python3 tools/embed_piano.py`.

## لغة الكتل — `piano.<الاسم> { … };`
صياغة تصريحية تُكتب **داخل أي كود Rin** (بعد `@import "piano";`) دون استدعاء دوال. اسم المجموعة قبل القوس، وكل المحتوى داخل الأقواس:

```kotlin
@import "piano";

piano.Poster {
    $size  => "480 270";                 // إعداد: $size $display $theme $bg $title $font $save
    $theme => "dark";
    $gap   => 14;                        // متغير مستخدم → $gap

    $Bg.Rect    => "0 0 480 270" { grad: ["#0b1020", "#1e1b4b"]; angle: 60; };
    $sky.Linear => "45 '#818cf8' '#22d3ee'";                       // تعريف تدرّج: $sky يحمل المرجع
    $Hero.Circle => "240 128 (60 + $gap)" { fill: $sky; shadow: true; };
    $Name.Text  => "240 138 'piano'" { size: 34; anchor: "middle"; };

    $Row.Repeat => 7 { $dot((40 + $i * 66), 232, ($i % 4)); };      // حلقة: $i و$n
    $dot.Fn(x, y, k) { $.Circle => "$x $y (4 + $k)" { fill: pal($k); }; };   // دالة
    $save => "poster.svg";
};

pn_save(Poster, "poster.puo");          // Poster صار متغيراً (لوحة) بعد الكتلة
```

**الجملة:** `$الاسم.النوع => "هندسة" { مفتاح: قيمة; … };`
- `$اسم => قيمة;` متغير/إعداد · `$اسم.النوع => …` عنصر · `$اسم.Fn(معاملات) { … };` دالة ثم `$اسم(قيم);` استدعاء.
- **الهندسة** كلمات مفصولة بمسافات **أو بفاصلة** (`"10, 20, 30"` أو `"10،20،30"` أو `"10 20 30"`): رقم · `$متغير` · `(تعبير)` · `'نص'` · كلمة. ما نقص منها يُؤخذ من الكتلة بالاسم (`x: 10;`).
- **الكتلة** `{ … }` للعناصر العادية = خيارات (`fill`, `stroke`, `rx`, `shadow`, `grad`, `labels`, `data`, `spin`, `pulse`…) بقيم: أرقام، نصوص، `true/false`، قوائم `[..]`، قواميس `{..}`، تعبيرات `+ - * / %`، مقارنات، ودوال `sin cos abs floor ceil round sqrt pow min max len mix alpha lighten darken pal ink accent muted`.
- **حاويات** تحوي جُمَلاً: `Group` · `At => "x y"` · `Repeat => n` · `Each => [قائمة]` (`$it` `$i`) · `If => (شرط)` · `Phone` · `Browser` (داخلهما `$sx $sy $sw $sh` = الشاشة) · `Pattern` · `Clip` · `Mask`. أزواج `مفتاح: قيمة;` في أول الحاوية تصير خيارات لها.
- **الأنواع:** Rect RRect Circle Ellipse Line Polyline Polygon Path Text Image Ngon Star Slice Ring Arc Arrow Grid Wave Spiral Smooth Label · Card Button Badge Avatar Progress Toggle Input Divider Icon Box Connect Elbow Mesh · Bar LineChart Pie Radar Spark Gauge · **Container** · تعريفات: Linear Radial Shadow Glow Blur Fade.
- التعليقات: انظر القسم أدناه. أخطاء الكتلة تحمل **رقم السطر الحقيقي** في ملفك.
- **امتداد الملفات:** `.rin` أو `.pia` (أو أي امتداد). `rin card.pia` يشغّله كبرنامج، و`@import "./card.pia";` أو `@import "card.pia";` (بجانب الملف الحالي) يستورده، و`pn_load("card.pia")` يعيد اللوحة. أما `.puo` فمشهد JSON جاهز (`pn_load("a.puo")`).
- يمكن أيضاً: `let c = pn_dsl("اسم", مصدر);` أو `pn_dsl_load("a.pia")`.

التحويل يتم في المحلل المفرداتي للغة (`rin_lexer.cpp`): الكتلة تصير `let Poster = pn_dsl_at("Poster", "<النص>", <سطر>);` قبل التحليل، لذلك لا تتأثر بقية الشيفرة. تُتجاهَل الكتل داخل النصوص والتعليقات، وتُعرَف فقط في بداية جملة.

## الرسم المتحرك أثناء التشغيل (pn_film)
أنت تبرمج الحركة: يستدعي `pn_film` دالتك **مرة لكل إطار** على لوحة جديدة، وحالتك (متغيراتك) تبقى بين الإطارات.

```kotlin
@import "piano";

fun draw(c, t, p, i, n) {          // t ثوانٍ · p ∈ [0,1) تقدّم · i رقم الإطار · n عدد الإطارات
    let x = pn_tween(40, 440, pn_pingpong(p * 2), "inout");        // تخفيف
    pn_circle(c, x, 135, 20, {"fill": pn_accent(c), "glow": 6});
}
let film = pn_film(480, 270, 30, 3, "dark", draw);   // w, h, fps, ثوانٍ, ثيم, دالة
pn_save(film, "loop.svg");       // SVG متحرك (SMIL) · .html أيضاً · .puo يحفظ كل الإطارات
```
- النتيجة لوحة عادية (تعرض الإطار الأول) فيها `anim = {fps, loop, frames}`؛ `pn_frame(film, i)` يعطي لوحة الإطار i، و`pn_frame_count(film)`، و`pn_film_loop(film, false)` لتشغيل مرة واحدة تثبت عند الأخير.
- `pn_save(film, "a.svg")` و`pn_html(film)` يخرجان **متحركين** تلقائياً؛ ولقطة ثابتة عبر `pn_svg(pn_frame(film, 0))`.
- **تخفيف ودوال زمن:** `pn_ease(اسم, t)` (`linear in out inout in3 out3 inout3 sine bounce elastic back step`) · `pn_tween(a, b, p, اسم)` · `pn_fract` · `pn_pingpong`.
- **في indsin:** `pn_frame_uri(film, i)` يعطي صورة الإطار i لـ `<Image src=…>`؛ حرّك `i` بمتغيّر warp.
- **في لغة الكتل:** عرّف `$fps => 24; $duration => 2;` ثم `$حركة.Frame { … };`: كل ما داخلها يُنفَّذ في كل إطار بقيم `$t $p $i $n`، وما خارجها ثابت. دوال إضافية: `ease("out", $p)` `tween(a, b, $p, "inout")` `fract` `pingpong` `lerp` `clamp`، والزوايا في `sin/cos` بالدرجات.
```kotlin
piano.Loader {
    $size => "360, 220";  $fps => 24;  $duration => 2;
    $spin.Frame {
        $a0 => (ease("inout", fract($p)) * 360);
        $Run.Arc => "180, 100, 54, ($a0 - 90), ($a0 + 10)" { stroke: "#1db143"; sw: 10; cap: "round"; };
    };
    $save => "loader.svg";
};
```
في المعاينة الحيّة يظهر مشغّل (▶/⏸ وشريط تنقّل) لأي مشهد فيه `anim`، ويُصدَّر SVG متحركاً أو `.puo`. مُصيِّر C++ يعرض الإطار الأول فقط.

## التعليقات في ملفات piano و.pia
ثلاثة أنماط تُغلَق من الطرفين، وتصلح **داخل الكتلة** وفي **المستوى الخارجي لملفات `.pia` و`.piano`**:

| النمط | مثال |
|---|---|
| `*// … //*` (متعدد الأسطر) | `*// شرح طويل //*` |
| `//…//` | `$size => "100, 60"; //حجم الرسم//` |
| `++ … ++` (متعدد الأسطر) | `++ ملاحظة ++` |

والتعليق غير المغلق ينتهي بنهاية السطر، وتبقى `// …` و`/* … */` القديمتان تعملان. انتبه: لا تكتب `//` داخل نص تعليق `//…//` (يُغلقه)، ولا `//*` داخل `*// … //*`. في الكود الخارجي `++` تعليق فقط إذا أُحيط بفراغين، فلا تتأثر `i++`.

## استدعاء piano داخل صفحات indsin
داخل قسم `<Rin>` في أي صفحة indsin تستطيع **استدعاء piano مباشرة** — دوالها وكتلها — وربط الناتج بـ `<Image src=…>`:

```xml
<warp score={72} />   <warp art={""} />
<Rin>
@import "piano";
fun gaugeScene(score, theme) {
    piano.Gauge(score, theme) {                 // معاملات الكتلة تصير $score و$theme داخلها
        $size => "240, 150";  $theme => $theme;
        $Dial.Gauge => "120, 112, 72, $score, 100" { color: pal(0); label: "الأداء"; };
    };
    return Gauge;
}
fun refresh() { art = pn_uri(gaugeScene(score, "dark")); }   // pn_uri: متحرك تلقائياً إن وُجدت حركة
refresh();
</Rin>
<Image#art src={art} width=240 height=150 />
<Button onTap={score = score + 9; refresh()}>تحديث</Button>
```
- `piano.Name(a, b = expr) { … }` تمرّر قيماً من Rin إلى الكتلة (`$a` `$b`). ضعها داخل دالة لتُعاد عند تغيّر `warp`.
- `pn_uri(c)` data-URI متحرك (SMIL) إن كان في اللوحة `anim`، وإلا ثابت · `pn_anim_uri` · `pn_frame_uri(film, i)` + `pn_next/pn_prev` للتنقّل بين الإطارات بأزرار.
- النص المقتبس في الهندسة يُعوِّض `$اسم` بقيمة المتغيّر: `"120, 150, 'إطار $i من $n'"`.
- **المحرّك الأصلي** (طرفية، أندرويد، سطح المكتب): يعمل كل ما سبق مباشرةً بما فيه الأزرار وتحديث `warp`.
- **الويب:** مترجم `indsin2web` لا يشغّل مكتبات Rin، فنخبز الصفحة أولاً: `tools/piano_indsin_bake.py` يشغّل قسم `<Rin>` بالمحرّك ويكتب القيم النهائية في الصفحة، فتظهر صور piano — **والمتحركة منها تتحرك داخل `<img>`** — لكن الصفحة لقطة ثابتة (تُحذف معالجات الأحداث التي تستدعي دوال Rin).
```bash
bash scripts/build_indsin2web.sh
python3 tools/piano_indsin_bake.py examples/piano/14_indsin_film.rin -o baked.rin --html page.html
```
- أمثلة: `examples/piano/13_indsin_blocks.rin` (مقياس يتفاعل مع ثيم وقيمة) و`14_indsin_film.rin` (حركة بجانب تنقّل بالإطارات).

## القواعد المهمة
- الثيم يؤثر على ما يُرسم **بعد** `pn_theme`.
- الأرقام في SVG بخانتين عشريتين، والشفافية بثلاث؛ ولا أُسّ أبداً.
- النصوص تدعم العربية و`\n` للأسطر المتعددة (`dir:"rtl"` للمحاذاة اليمنى).
- مفاتيح تبدأ بـ `_` (مثل `_ctx`) داخلية ولا تُكتب في `.puo`.
- `pn_from_json` يعيد لوحة يمكن متابعة الرسم عليها.

## سطر الأوامر C++
```bash
g++ -std=c++17 -O2 lib/piano.og.rin/native/piano_cli.cpp -o piano_cli
./piano_cli scene.puo -o scene.svg          # أو --html
```

## المعاينة الحيّة
`web/piano/index.html` (تُنشر على `/piano/`): تبويب **.puo** يعرض فورياً أثناء الكتابة، وتبويب **Rin** يشغّل الشيفرة بمحرك WASM (يتطلب `rin.js`/`rin.wasm` بجانب الموقع؛ يُحمَّلان تلقائياً). تكبير وسحب، مفتّش عقد، تصدير SVG/PNG/.puo، رابط مشاركة.
البناء: `python3 tools/piano_build.py` · الاختبار: `python3 tools/piano_check.py`.

## مع indsin (تفصيل لاحق في «استدعاء piano داخل صفحات indsin» أدناه)
```xml
<Image#art src={pn_data_uri(c)} width=320 height=200 />
```
انظر `examples/piano/piano_studio.rin`.

## رسم وطباعة بيانات الـ container
تقرأ piano بيانات أي حاوية **حيّة** (`@container` · `.data` · `.object` · `.table` · `.doc` · `.datalist`) وترسمها بطاقة SVG أو تطبعها جدولاً نصّياً. الناتج مشهد piano عادي، فيعمل مع `pn_save` (`.svg .puo .html`) وindsin والويب ومُصيِّرَي C++/JS بلا أي تغيير فيهما.

```kotlin
@import "piano";
@container.table=scores
    row cells=["name", "score"];
    row cells=["sara", 95];
.end/container.table

pn_container_print(nil, {});                                   // جداول نصّية لكل الحاويات (nil أو [] = الكل)
let c = pn_container_scene(["scores", "app"], 440, {"theme": "dark"});   // بطاقة لكل حاوية
pn_save(c, "containers.svg");
```

| الدالة | الناتج |
|---|---|
| `pn_container_model(name, o)` | نموذج `{name, kind, size, fields:[{k,v,t}], funcs, children, table:{header,rows}, docs:[{id,v}], total}` |
| `pn_container(parent, x, y, w, name, o)` | يرسم بطاقة في لوحتك؛ يُرجع group فيه `_h` = الارتفاع الفعلي |
| `pn_container_scene(names, w, o)` | لوحة جديدة بطاقة لكل اسم (نص أو قائمة؛ `nil`/`[]` = كل الحاويات) |
| `pn_container_uri(names, w, theme, o)` | data-URI لـ `<Image src=…>` في indsin |
| `pn_container_save(names, path, o)` | يحفظ بالامتداد (`o.width` افتراضي 440) |
| `pn_container_text(name, o)` · `pn_container_print(names, o)` | جدول نصّي · طباعته (`o.svg=true` تطبع الـ SVG أيضاً) |

**الخيارات (كلها اختيارية):** `theme` · `title` · `fields:[..]` (أسماء تُعرَض فقط) · `hide:[..]` · `maxRows`(10) · `maxCols`(8) · `maxDocs`(6) · `showTypes`(true) · `showFuncs`(true) · `showChildren`(true) · `color` · `size`(13) · `gap` · `pad` · `margin`.

- الحقول **مرتّبة أبجدياً** (ترتيب التصريح غير محفوظ في الحاوية). الدوال تظهر في سطر `ƒ`، والحاويات المتداخلة في سطر `⊂`.
- الجدول: الصف الأول رأس إن كانت كل خلاياه نصوصاً؛ ما زاد على `maxRows` يُختصر بسطر `… +N`. القص يحترم UTF-8 فلا تنكسر العربية.
- **لغة الكتل:** `$Inv.Container => "x y w 'اسم'" { maxRows: 5; };`
- **داخل indsin:** `<Image src={pn_container_uri("counter", 320, theme, {})} width=320 />` — أعد الاستدعاء بعد تعديل الحاوية لتتحدّث البطاقة.
- أمثلة: `examples/piano/15_container.rin` · `16_indsin_container.rin` · الاختبار: `tests/verification/piano_container.rin`.
- ملاحظة: القراءة من الحاوية تعمل في مفسّر Rin فقط؛ ملف `.puo` الناتج لقطة ثابتة من البيانات لحظة الرسم.

## هوية التطبيق/الموقع (SEO · أيقونات · إصدار)
مع `@container.app` ([`container_app.md`](./container_app.md)): `pn_app_apply(c, app)` · `pn_app_html(c, app, {})` · `pn_app_save(c, dir, app, {})` تولّد صفحة ويب بـ `<head>` كامل (title/description/keywords/canonical/Open Graph/Twitter/أيقونات/manifest/JSON-LD) و`lang`/`dir` من الهوية. مثال: `examples/piano/app_page.rin`.

## حدود الإصدار 1.0
لا تصدير PNG أصلياً من Rin (استعمل SVG أو زر PNG في المعاينة)، ولا نصوص على مسار. أسماء معاملات مثل `on` و`text` محجوزة في Rin فلا تُستعمل في شيفرتك كمعاملات.

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

## مع indsin
```xml
<Image#art src={pn_data_uri(c)} width=320 height=200 />
```
انظر `examples/piano/piano_studio.rin`.

## حدود الإصدار 1.0
لا تصدير PNG أصلياً من Rin (استعمل SVG أو زر PNG في المعاينة)، ولا أقنعة (mask) ولا نصوص على مسار.

# طباعة الصور كنص في الطرفية

```rin
print.image("logo.png", mode="blocks", cols=60);                 // نصف كتلة ▀ بألوان 24-بت (الأوضح)
print.image("logo.png", mode="braille", cols=40, caption="Rin");  // نقاط برايل (دقة أعلى، بلا ألوان)
print.image("logo.png", mode="ascii", cols=50, invert=true);      // محارف ASCII (invert للطرفية الفاتحة)
let art = imageText("logo.png", 40, "shade");                     // النص كقيمة: imageText(path, cols, mode, invert, color)
```
تظهر الصيغة القديمة `print.image(path, caption=, width=)` بلا تغيير (سطر الأندرويد). يكفي ذكر أي من `mode` `cols` `invert` `color` ليُرسم النص.

| السمة | القيم |
|---|---|
| `mode` | `blocks` (= `half` كما في `:img`، وهو الافتراضي) · `ansi256` · `shade` (░▒▓█) · `ascii` · `braille` |
| `cols` | عدد الأعمدة (افتراضي 60، حتى 400) — الارتفاع يُحسب بالنسبة الصحيحة |
| `invert` | قلب الفاتح/الغامق في `shade`/`ascii`/`braille` |
| `color` | `true` يفرض الألوان · `false` يمنعها · الافتراضي: تلقائي |
| `caption` | سطر تحت الصورة · `if=` شرط تنفيذ |

- **الألوان تلقائية:** `blocks`/`ansi256` تُخرج أكواد ANSI فقط على طرفية أو `FORCE_COLOR=1` (ويمنعها `NO_COLOR`)؛ وإلا تتحول إلى `shade` رمادي نظيف، فلا تتلوّث الملفات والأنابيب.
- **الصيغ:** PNG (كل أنماط الألوان وأعماق البتات، وAdam7، وشفافية) · PPM/PGM · BMP (8/24/32 بت) مدمجة بلا اعتماديات؛ JPEG/GIF/WebP عبر ImageMagick (`convert`/`magick`) إن وُجد.
- الشفافية تُدمج فوق الأسود. يعمل مع قوالب الطباعة: `@tmpl logo(p) print.image(p, mode="braille", cols=30);`
- يعمل على المفسّر وRinVM. اختبار: `tests/print/print_image_terminal.rin` (يُشغَّل من جذر المشروع).

## العلاقة بأمر `:img` في الطرفية
`:img` / `rin img` (فك عبر ImageMagick) يبقيان كما هما للاستعمال التفاعلي. هذه الميزة داخل **اللغة** نفسها: تُستعمل في السكربتات والقوالب، وتفكّ PNG/PPM/BMP بلا ImageMagick،
وتشارك `:img` مفردات الأنماط (`half`/`braille`/`shade`/`ascii`) وسياسة الألوان (طرفية أو `FORCE_COLOR`، و`NO_COLOR` يمنعها).

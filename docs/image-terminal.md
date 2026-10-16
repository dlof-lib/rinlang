# طباعة الصور في الـ terminal — `printImage` / `imageAnsi` / `imageInfo`

تحويل صورة إلى نص وطباعتها داخل الطرفية، بلا أي مكتبة خارجية على جهاز المستخدم (المفكّك مضمَّن: `stb_image`).

```rin
printImage("logo.png");                 // عرض تلقائي (أقل من عرض الطرفية)
printImage("photo.jpg", 60);            // 60 عموداً، الارتفاع بحسب النسبة
printImage("logo.png", 40, "ascii");    // حروف ASCII فقط
let art = imageAnsi("logo.png", 30);    // نفس النص كقيمة (احفظه في ملف أو اطبعه لاحقاً)
let info = imageInfo("logo.png");       // {width, height, channels, format}
```

| الدالة | الناتج |
|---|---|
| `printImage(path, width?, mode?)` | تطبع في الخرج وتُرجع `nil` |
| `imageAnsi(path, width?, mode?)` | تُرجع النص (أسطر مفصولة بـ `\n`، بلا سطر أخير فارغ) |
| `imageInfo(path)` | `{width, height, channels, format}` — `format`: png/jpeg/bmp/gif |

## الأوضاع (`mode`)
| الوضع | الشكل |
|---|---|
| `"auto"` (افتراضي) | `color` إن كان الخرج طرفية أو `FORCE_COLOR=1` (وبلا `NO_COLOR`)، وإلا `ascii` |
| `"color"` | ألوان 24-bit بنصف-كتل `▀`: بكسلان رأسيان لكل خلية (أعلى دقة) |
| `"blocks"` | تدرّج رمادي بكتل `░▒▓█` |
| `"ascii"` | تدرّج رمادي بحروف ASCII (آمن للملفات وكونسول أندرويد وCI) |

## ملاحظات
- `width` بالأعمدة (1..1000). الافتراضي `min(عرض الصورة, عرض الطرفية)`؛ يُقرأ عرض الطرفية من النظام ثم `COLUMNS` ثم 80.
- الحفاظ على النسبة: في `color` البكسل مربع (نصف-كتلة)، وفي الرمادي تُعوَّض أطولية خلية الحرف (~2:1).
- البكسلات الشفافة تُترك فارغة. GIF: الإطار الأول فقط. حدّ الحجم 64 ميغابكسل.
- المسار نسبي لمجلد العمل كما في `readFile`. الأخطاء (ملف غير موجود/غير صورة/وسيط خاطئ) أخطاء Rin عادية قابلة للالتقاط.
- الألوان تظهر فقط على طرفية تدعم truecolor؛ الخرج المُوجَّه لملف أو أنبوب يصير ASCII تلقائياً (لا أكواد خام في الملفات).
- التنفيذ: `app/src/main/cpp/rin_image_ansi.cpp` (يُضمَّن في `rin_interpreter.cpp`). اختبار: `tests/image/image_terminal.rin` (شغّله من داخل `tests/image`).

## العلاقة بـ `print.image` و`:img` و`imageText`
- `print.image(...)` / `imageText(...)` (docs/print-image-terminal.md) هي الواجهة الأساسية داخل اللغة؛ و`:img` للاستعمال التفاعلي.
- `printImage` / `imageAnsi` / `imageInfo` هنا تكمّلها: فكّ **JPEG وGIF** مدمج (stb_image) بلا ImageMagick، و`imageInfo` لقراءة الأبعاد والصيغة.

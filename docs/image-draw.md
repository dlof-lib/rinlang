# إنشاء الصور في Rin — رسم وحفظ PNG وlog كصورة

دوال `img*` تنشئ لوحة بكسلات (RGBA، 8 بت)، ترسم عليها، وتحفظها **PNG / BMP / PPM**، أو تطبعها في الطرفية.
بلا أي اعتماد جديد (الترميز مكتوب يدوياً + zlib الموجود). تعمل على المفسّر وRinVM بنتائج متطابقة.

اللوحة تُعرَّف بـ **معرّف رقمي (id)** تعيده `imgNew`/`imgLoad`. حدّ اللوحة الواحدة 16384 بكسل للضلع و32 ميغابكسل، وحدّ 64 لوحة حيّة (حرّر بـ `imgFree`).

```rin
let img = imgNew(320, 200, "#0b1220");
imgRect(img, 20, 24, 272, 120, "#f8fafc", {radius: 14});
imgText(img, 40, 40, "RIN IMAGE", "#0f172a", {size: 4});
imgCircle(img, 60, 120, 14, "#22c55e");
imgSave(img, "out/card.png");     // PNG حقيقي؛ الصيغة من الامتداد
imgPrint(img, 48);                // وطباعتها في الطرفية
```

## الألوان
`"#rgb"` · `"#rrggbb"` · `"#rrggbbaa"` · `rgb()` · `rgba()` · `hsl()` · أسماء CSS · رقم `0xRRGGBB` · مصفوفة `[r,g,b]` أو `[r,g,b,a]`.
الشفافية تُمزج mix فعلي (source-over). لون غير صالح = خطأ واضح.

## الإحداثيات
البكسل `(x, y)` من أعلى اليسار. في `imgLine/imgCircle/imgEllipse/imgPolygon` الإحداثي الصحيح = **مركز البكسل**؛ في `imgRect/imgText/imgPaste/imgCrop` هو **الزاوية العلوية اليسرى**. الرسم منعَّم (anti-aliased) والمستطيلات ذات الإحداثيات الصحيحة حادّة تماماً.

## الدوال

| الفئة | الدالة | ما تفعله |
|---|---|---|
| إنشاء | `imgNew(w,h,[bg])` | لوحة جديدة (شفافة إن لم تُذكر bg) |
| | `imgLoad(path)` | تحميل PNG/JPEG/BMP/GIF للتعديل |
| | `imgCopy(id)` · `imgFree(id)` | نسخ / تحرير |
| معلومات | `imgSize(id)` | `{width,height}` |
| | `imgPixel(id,x,y)` / `imgPixel(id,x,y,color)` | قراءة (`"#rrggbb"`، `nil` خارج الحدود) / كتابة بكسل |
| رسم | `imgClear(id,[color])` · `imgGradient(id,c1,c2,["v"\|"h"\|"d"])` | تعبئة / تدرّج |
| | `imgLine(id,x0,y0,x1,y1,color,[width])` | خط |
| | `imgRect(id,x,y,w,h,color,[fill \| {fill,width,radius}])` | مستطيل (ممتلئ افتراضياً، زوايا مدوّرة بـ radius) |
| | `imgCircle(id,cx,cy,r,color,[fill \| {fill,width}])` · `imgEllipse(id,cx,cy,rx,ry,color,[…])` | دائرة / قطع ناقص |
| | `imgPolygon(id,[[x,y],…],color,[fill \| {fill,width}])` | مضلّع (even-odd) |
| | `imgFloodFill(id,x,y,color)` | تعبئة بالفيضان، يعيد عدد البكسلات |
| نص | `imgText(id,x,y,text,color,[{size,align,bg,padding,radius}])` | يرسم نصاً (يدعم `\n`)، يعيد `{width,height}` |
| | `imgTextSize(text,[size])` | قياس النص دون رسم |
| تحويل | `imgResize(id,w,h)` · `imgCrop(id,x,y,w,h)` | لوحة **جديدة** (استيفاء ثنائي الخط) |
| | `imgPaste(dst,src,x,y,[opacity])` | لصق بمزج الشفافية |
| | `imgFlip(id,"h"\|"v")` · `imgFilter(id,name,[v])` | في المكان: `grayscale` `invert` `brightness` `opacity` |
| إخراج | `imgSave(id,path,[fmt])` | يكتب الملف (`png`/`bmp`/`ppm` من الامتداد)، يعيد عدد البايتات |
| | `imgEncode(id,[fmt])` | البايتات كنص ثنائي (لـ `sec.base64`/الشبكة) |
| | `imgPrint(id,[cols],[mode])` · `imgAnsi(…)` | طباعة في الطرفية / كنص (`auto\|color\|blocks\|ascii`) |
| نص/سجلّ | `imgFromText(text,[opts])` | نص متعدد الأسطر ← لوحة |
| | `imgFromLog([entries],[opts])` | سجلّ `print.log` ← لوحة ملوّنة |

`printImage`/`imageAnsi` تقبلان الآن أيضاً **معرّف لوحة** بدل المسار.

## الـ log كصورة
`print.log` / `print.log.info|warn|error|debug` تتراكم تلقائياً. ثلاث طرق لتحويلها:

```rin
print.log.error("connection lost");
logSave("out/log.png");                                    // الأبسط: الامتداد png/bmp/ppm يرسم الصورة (يستبدل الملف)
let li = imgFromLog({title: "Session", size: 2});          // أو لوحة تتحكم بها
imgFromLog(logHistory(), {level: "error", last: 20});      // أو مصفوفة مفلترة
```
خيارات `imgFromText/imgFromLog`: `size` (تكبير الخط 1–32، 2 افتراضياً) · `padding` · `fg` · `bg` · `title` · `lineNumbers` · `wrap` (أقصى عدد أحرف بالسطر) · `minWidth`؛ وللسجلّ أيضاً `source` (إظهار ملف:سطر) · `level` (مستوى واحد فقط) · `last` (آخر N).
`logSave("x.txt")` (امتدادات أخرى) يبقى كما كان: إلحاق نصي.

## حدود يجب معرفتها
- **النص العربي في الصور تقريبي**: لا يوجد خط عربي مضمَّن، فتُستعمل أشكال الحروف المتجهية المبسَّطة الموجودة في المشروع (أشكال معزولة بلا وصل، ومجموعة حروف غير كاملة؛ ما لا يُعرف يُرسم مربعاً مفرّغاً صريحاً). اللاتيني والأرقام والرموز (ASCII) واضحة وحادّة. للنص العربي الجميل استعمل واجهة indsin/HTML.
- الحفظ: PNG وBMP وPPM فقط (لا JPEG/GIF/WebP للكتابة). الشفافية تُحفظ في PNG/BMP؛ PPM يدمجها فوق الأبيض.
- المعرّفات تعيش طوال تشغيل البرنامج؛ حرّر ما لا تحتاجه.

# Illust — لغة رسم صغيرة فوق Rin

`illust.rin` لغة مكمّلة لـ RinLang: أوامر رسم نصية بسيطة (canvas/rect/circle/line/text/fill/stroke)
مع متغيرات وشروط وحلقات، مبنية بالكامل فوق `lib/langkit.og.rin` بنفس بنية أي مشروع
"لغة مخصّصة" في المستودع (راجع `docs/custom-languages.md` و`examples/customlang/calc/`).

مسارها المزدوج مطابق لمثال calc:
- **Interpreter.rin** — يُنفّذ AST مباشرة ويُخرج **SVG حقيقي** جاهز للعرض في أي متصفح.
- **CodeGen.rin** — يترجم نفس AST إلى **كود Rin مستقل بذاته** (لا يعتمد وقت التشغيل على
  مفسّر Illust ولا حتى على `langkit`)، يمكن حفظه بـ `writeFile()` وتشغيله لاحقاً بأي
  مفسّر Rin عادي وينتج نفس مخرجات SVG تماماً.

كِلا المسارين اختُبرا فعلياً ببناء مفسّر Rin من مصدر هذا المستودع (`cli/linux`) وتشغيل
`run.rin` عليه — وليس مجرد كود مكتوب نظرياً.

## البنية

```
illust/
├── manifest.json
├── Lexer.rin
├── Parser.rin
├── Interpreter.rin
├── CodeGen.rin
├── run.rin
├── syntax.rinsyntax.json
└── examples/
    └── hello.illust
```

للتثبيت داخل مستودع rinlang: انسخ هذا المجلد إلى `examples/customlang/illust/` تماماً
مثل `examples/customlang/calc/`، أو استخدمه كنقطة بداية عبر IDE أندرويد
(مشروع جديد ← لغة مخصصة جديدة) بنفس آلية `templates/customlang/`.

## الصياغة (Syntax)

```illust
canvas(300, 200);        // تحديد أبعاد اللوحة (اختياري، افتراضياً 400x300)

fill("#eaf6ff");         // لون التعبئة، أو fill(none);
stroke(none);             // بلا حدّ، أو stroke("#8a5a00", 3);  (لون، سُمك)
rect(0, 0, 300, 200);     // x, y, width, height

fill("#ffcc66");
stroke("#8a5a00", 3);
circle(150, 100, 60);     // cx, cy, r

let i = 0;
while (i < 2) {
    let ex = 0 - 20 + i * 40;
    circle(ex, -10, 6);   // إحداثيات نسبية داخل group()
    i = i + 1;
}

group(150, 100) {         // كل الرسم داخل الكتلة يُزاح بمقدار (dx, dy)
    line(-15, 25, 15, 25);
}

if (1 == 1) {
    text(90, 190, "Illust says hi");
}
print("تم بناء الرسمة");  // يُطبع في سجلّ التنفيذ، لا يظهر في SVG
```

### الكلمات المفتاحية
`let`, `canvas`, `fill`, `stroke`, `none`, `rect`, `circle`, `line`, `text`,
`if`, `else`, `while`, `group`, `print`

### التعابير
أرقام، نصوص `"..."`، متغيرات، `+ - * /`، مقارنات `== != < <= > >=`، سالب أحادي `-x`، أقواس.

## التشغيل

```
rin run run.rin --  examples/hello.illust
```

أو عدّل `SOURCE_PATH` داخل `run.rin` مباشرة. يطبع البرنامج:
1. `SVG` الناتج من `Interpreter.rin` (جاهز للحفظ كـ `.svg` وفتحه في أي متصفح).
2. كود Rin كامل مُولَّد من `CodeGen.rin` (جاهز للحفظ كـ `.rin` وتشغيله مستقلاً).

## أفكار للتوسعة لاحقاً
- أشكال إضافية: `polygon(...)`, `ellipse(...)`, مسارات `path("M ... L ...")`.
- دوال معرّفة من المستخدم (`fun`) لإعادة استخدام رسومات مركّبة.
- تدوير/تحجيم داخل `group()` (transform كامل، وليس إزاحة فقط).
- تصدير مباشر عبر `writeFile()` إلى ملف `.svg`/`.rin` من `run.rin`.

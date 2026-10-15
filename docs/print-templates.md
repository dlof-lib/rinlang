# قوالب الطباعة `@tmpl ... print` — قوالب نصية خطية وتنسيقية

تمدّ نظام `@tmpl` (انظر [loop-templates.md](loop-templates.md)) ليشمل `print`: عرّف شكل المخرجات مرة واحدة، واستعمله في كل مكان.
تُفكَّك على مستوى التوكنات قبل التحليل النحوي، فتعمل على المفسّر الشجري وRinVM وأندرويد والويب بلا أي تعديل في المفسّر.

| النوع | التعريف | الاستعمال | الناتج |
|---|---|---|---|
| سطر | `@tmpl n(p) print قيم سمات;` | `@n(args);` | عبارة `print` |
| خطي (أسطر) | `@tmpl n(p) print { سطر; سطر; }` | `@n(args);` | عدّة عبارات `print` |
| تنسيقي | `@tmpl n(p) style سمات;` | `print x @n(args);` | سمات `width/align/upper/...` |
| نصي | `@tmpl n(p) text تعبير;` | `@n(args)` في أي تعبير | قيمة نصية |

## 1) قالب سطر
```rin
@tmpl hello(name) print `Hello ${name}!`;
@hello("rin");                    // Hello rin!
@hello("rin") upper=true;         // سمات إضافية بعد الاستعمال: HELLO RIN!
```

## 2) قالب خطي (عدة أسطر)
كل سطر = قيم `print` وسماتها (بما فيها `if=`)، ويُفصل بين الأسطر بـ `;`:
```rin
@tmpl receipt(shop, total) print {
    `=== ${shop} ===` width=24 align="center";
    "Total", total sep=" : ";
    "thanks" if=total > 10;
}
@receipt("Rin Cafe", 25);
@receipt("Rin Cafe", 5) upper=true;     // السمات الإضافية تُلحق بكل سطر
```

## 3) قالب تنسيقي `style`
مجموعة سمات (`width align upper lower label level sep end pretty repeat if`) تُستعمل داخل أي `print`، ومع قوالب السطر:
```rin
@tmpl title(w = 30) style width=w align="center" upper=true;
print "report" @title();                // معاملات افتراضية
print "wide" @title(40) label="X";
@tmpl ok(msg) print msg level="success";
@ok("done") @title(20);                 // سطر + تنسيق
```
> لا تسمِّ معاملاً باسم سمة (`width`...) — التعويض على مستوى التوكنات.

## 4) قالب نصي `text`
تعبير يُستعمل في أي مكان (داخل `print` أو `let` أو شرط...). المعاملات معزولة وتعمل مع `${p}` داخل النصوص القالبية:
```rin
@tmpl tag(s) text "[" + s + "]";
print @tag("a") + @tag("b");            // [a][b]
@tmpl money(x) text `$${x}`;
print @money(3);                        // $3
```

## 5) التركيب والنطاق
- قالب يستعمل قالباً: `@tmpl banner(t) print @tag(t) @title(20);`
- قوالب السطر والخطي تُنفَّذ داخل كتلة معزولة: المعاملات تُقيَّم مرة واحدة وبترتيبها (`@swap(b, a)` آمن)، ولا تتسرّب إلى نطاقك.
  السمات الإضافية بعد الاستعمال تُقيَّم داخل نفس الكتلة، فتجنّب أسماء معاملات تطابق متغيراتك المستعملة فيها.
- تُعرَّف القوالب قبل استعمالها، وهي خاصة بالملف (لا تعبر `@import`)، ولا يحتاج التعريف إلى `{ }` إلا في النوع الخطي. ينتهي ما سواه بـ `;`.

اختبار: `tests/print/print_templates.rin`.

لتنسيق الخط (`bold` `italic` `underline` `color` ...) داخل `style` و`print`: انظر [print-fonts.md](print-fonts.md).

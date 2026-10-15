# خطوط `print` — bold / italic / underline وغيرها

سمات جديدة على `print` (وعلى قوالب `@tmpl ... style` و`print`) لتنسيق الخط:

| السمة | الأثر |
|---|---|
| `bold=true` | غامق |
| `italic=true` | مائل |
| `underline=true` | تحته خط |
| `strike=true` | يتوسطه خط |
| `dim=true` | باهت |
| `reverse=true` | عكس الألوان |
| `blink=true` | وميض (حسب الطرفية) |
| `color="tomato"` | لون النص (اسم CSS أو `#rrggbb` أو `rgb()` — كل ما يقبله محرك الألوان) |
| `bg="#112233"` | لون الخلفية |

```rin
print "Title" bold=true underline=true color="gold";
print "centered" bold=true upper=true width=20 align="center";   // التنسيق يُطبَّق بعد upper/width فلا يُفسد المحاذاة
print "flag" bold=isImportant;                                    // أي تعبير (falsy = بلا أثر)

@tmpl h1(w = 30) style bold=true underline=true color="gold" width=w align="center";
print "Report" @h1();                                             // مع قوالب الطباعة (print-templates.md)
@tmpl warn(msg) print msg level="warn" bold=true;
```

## متى تظهر الأكواد؟
يُطبَّق التنسيق (أكواد ANSI/SGR) **فقط** حين يكون الخرج طرفية، أو عند `FORCE_COLOR=1`؛ ويُعطَّل بـ `NO_COLOR`.
لذلك لا تتلوّث الملفات والأنابيب ولا كونسول أندرويد (الذي لا يفسّر ANSI) بأكواد خام: النص يُطبع عادياً وكل السمات تُتجاهَل بصمت.

## ملاحظات
- التنسيق يلفّ المحتوى فقط (بعد `width`/`align`)، أما رمز `level` ووسم `label` فخارج اللفّ.
- `color`/`bg` يجب أن يكونا نصاً صالحاً كلون، وغيره خطأ واضح؛ `color=false` = بلا لون.
- تعمل على المفسّر الشجري وRinVM. اختبار: `tests/print/print_fonts.rin`.

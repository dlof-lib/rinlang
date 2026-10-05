# CHANGES_TEMPLATE — النصوص القالبية `${}`

إضافة بحتة (additive): نصوص بين backtick تقبل `${تعبير}` داخلها.

```rin
let name = "ريم";
print `مرحباً ${name}!`;   // مرحباً ريم!
```

## الملفات المعدَّلة
- `app/src/main/cpp/rin_lexer.{h,cpp}` — `Lexer::scanTemplate()` يحوّل القالب إلى
  `("نص" + #str(expr) + "نص")` قبل المحلل النحوي (لا تغيير في Parser/Interpreter).
- `app/src/main/cpp/rin_interpreter.cpp` — native جديدة `#str` (= `toString`، لا يحجبها متغيّر المستخدم).
- المحرّرات: `RinSyntaxHighlighter.kt` (أندرويد)، `RinTokenizer.cs` (Visual Studio)، `syntaxes/rin.tmLanguage.json`.
- التوثيق: `docs/template-strings.md`، `docs/syntax.md`، `docs/README.md`.
- مثال: `examples/template_strings_demo.rin`. اختبارات: قسم "template strings" في `tests/rintests.rin`.

## التوافق
backtick كانت خطأ E0011 سابقاً، والنصوص `" "` لم تتغيّر إطلاقاً → لا كسر لأي كود موجود.
أخطاء جديدة: `unterminated template string`، ``empty `${}` in template string``.

## الإصدار 2 — تنسيق وقيم افتراضية وقيم جاهزة
- `${x:.2f}` `${x:,d}` `${x:05}` `${x:.1%}` `${s:>8}` `${s:<8}` `${s:^8}` `${s:upper|lower|trim}` → native `#fmt`.
- `${a ?? b}` → native `#default` (يُستبدل فقط عند nil).
- `${#date}` `#time` `#datetime` `#year` `#month` `#day` `#hour` `#minute` `#second` `#weekday` `#version` `#line` `#file`
  → native `#now` (أو قيمة ثابتة في Lexer لـ `#line`/`#file`).
- Lexer يميّز `:` التنسيق عن `:` الشرط الثلاثي والقاموس، و`??` عن `?` المفردة.
- اختبارات إضافية: 9 في `tests/rintests.rin` (المجموع 73 ناجحة).

## الإصدار 3 — أقوى وأوسع
- **تنسيق متقدّم:** حشو بأي حرف `*^10`، إشارة `+d`، فاصل `_`، أنواع `e x X b o s`، قصّ النص `.3`،
  كلمات `title cap ar`، وتتابع مراحل بـ `|` (مثل `trim|upper|>6`).
- **تنسيق ديناميكي متداخل:** `${x:.${n}f}` — `Lexer::scanTemplateParts('}')` يعالج المواصفة كقالب.
- **وضع التصحيح:** `${x=}` ⟶ `x=42`.
- **`\u{hex}`** داخل القوالب.
- **`render(template, data)`:** قوالب وقت التشغيل (مسارات `a.b.0`, `??` افتراضي، تنسيق، `{{}}`، `$${` للهروب)،
  آمنة (لا تنفّذ كوداً). المنطق في `templateFormat()`/`templateRender()` أعلى `rin_interpreter.cpp`.
- اختبارات إضافية: 10 → المجموع 83 ناجحة.

## الإصدار 4 — القوالب داخل مفاهيم اللغة
- **`translation`**: `lang` و`text` يقبلان قوالب ديناميكية تُقيَّم وقت التنفيذ (`TranslationStmt::langExpr/textExpr`).
- **`emit` و`on.event`**: اسم الحدث قالب ديناميكي (`EmitStmt/EventHandlerStmt::nameExpr`). التمييز عن دالة عادية اسمها
  `emit` عبر علم `Token::tpl` على ( الافتتاحية للقالب (يضعه `Lexer::scanTemplateParts`).
- **قالب بلا `${}`** صار رمز STRING واحداً، فيصلح في كل المواضع الثابتة (`@import`، `link.id`، `.object("id")`...)
  ويدعم أسطراً متعددة وعلامات `"`.
- **`${}` في موضع ثابت** يعطي خطأً واضحاً بدل رسالة عامة (`Parser::consumeStaticText`).
- خصائص `@view`/`@element` و`text x = ...` كانت تقبل القوالب أصلاً (تعبيرات) — موثَّقة الآن.
- ملفات: `rin_common.h` (Token::tpl)، `rin_ast.h`، `rin_parser.{h,cpp}`، `rin_lexer.cpp`، `rin_interpreter.cpp`،
  اختبار جديد `tests/template_concepts.rin`.

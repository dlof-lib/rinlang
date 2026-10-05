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

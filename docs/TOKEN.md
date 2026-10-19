# مفهوم `token` في Rin

الـ token هو الرمز اللغوي الذي يقسّم إليه `rin::Lexer` مصدر Rin قبل التحليل النحوي. كل دوال هذه الوثيقة تعمل على الـ Lexer الحقيقي نفسه (بلا قائمة موازية)، وهي **نقية**: تحليل لغوي فقط، لا تنفّذ الشيفرة ولا تعدّل مدخلاتها.

## الموجود سابقاً (لم يتغيّر)
- `tokens(src)` ← `[{type, lexeme, line}]`
- `tokenType(src)` ← نوع أول رمز (`tokenType("let") == "LET"`)

## الجديد
| الدالة | الوصف |
|---|---|
| `tokenList(src[, opts])` | رموز كاملة: `{index, type, category, lexeme, line, col, endCol[, value]}`. `opts`: `{eof:true, only:..., skip:...}` — القيمة نوع (`IDENT`) أو فئة (`keyword`) أو مصفوفة منها |
| `tokenCount(src[, only])` | عدد الرموز (بلا EOF) |
| `tokenStats(src)` | `{total, lines, identifiers, byType, byCategory}` |
| `tokenAt(src, line, col)` | الرمز عند الموضع (من 1) أو `nil` |
| `tokenLines(src)` | `[{line, count, tokens}]` مجمّعة بالسطر |
| `tokenCheck(src)` | `{ok:true, count}` أو `{ok:false, error, line, col}` بلا استثناء |
| `tokenDump(src)` | جدول نصي: سطر:عمود، النوع، الفئة، النص |
| `tokenCategory(type)` | `keyword / literal / identifier / operator / punctuation / special` أو `nil` |
| `tokenKinds([category])` | أسماء أنواع الرموز |
| `tokenKeywords()` | الكلمات المحجوزة الفعلية (مرتّبة) |
| `tokenIsKeyword(word)` | هل الكلمة محجوزة |

## أمثلة
```rin
let src = "let x = 42;\nprint x + 1;";
print tokenCount(src);                         // 10
print tokenList(src, {only: "keyword"});      // let, print
print tokenAt(src, 2, 7).lexeme;               // x
print tokenCheck("x = \"abc").ok;              // false
print tokenDump("let a = 1;");
```

## ملاحظات
- `col`/`endCol` تُحسب **بالبايت** (UTF-8) وendCol حصري؛ تنتبه لها بعد نص عربي في السطر نفسه.
- النص القالبي (`` `a ${x}` ``) يُفكَّك إلى رموز اصطناعية (`+` و`#str`) كما يفعل الـ Lexer.
- الحروف العربية خارج النصوص غير مسموحة في Rin؛ رسائل الخطأ تُظهر البايت غير الصالح كـ `\xHH`.
- الأسماء مسطّحة عمداً: `token` اسم متغيّر شائع فلا نُدخل مساحة أسماء `token.*` تتقاطع معه.
- أخطاء الاستعمال (نوع خاطئ، نوع/فئة غير معروفة) تُرمى كأخطاء عادية قابلة للالتقاط.

# else — «البديل» في كل مكان

كلمة `else` كانت في `if` و`match` و`let` و`trial` و`accept` فقط. الآن تعني الشيء نفسه في كل موضع: **«وإلا فهذا»**. إضافة بحتة: كل كود قديم يعمل كما هو. الاختبارات: `tests/print/else_*.rin`.

## 1) else بعد الحلقات (for / while / each)
تُنفَّذ إن انتهت الحلقة **طبيعياً دون `break`** (مثل `for…else` في Python، وهي مرادف لـ `nobreak`).
```kotlin
for (let x in xs) { if (x == t) { print "وُجد"; break; } } else { print "لم يوجد"; }
```
القائمة الفارغة تنتهي طبيعياً ⇒ تُنفَّذ else.
**else المعلّقة**: إن كانت الحلقة هي فرع `if` بلا أقواس، تبقى `else` لـ `if` كما كانت دائماً:
`if (c) for (...) {...} else {...}`. وضع الحلقة بين أقواس يجعل else لها.

## 2) try … catch … else … finally
`else` تُنفَّذ إن اكتمل `try` **دون خطأ**، وخارج حماية `catch` (خطأ فيها يصعد ولا يلتقطه catch نفسه)، وقبل `finally`. تتخطّاها `return/break/continue` داخل `try`. تتطلّب `catch`.

## 3) else كتعبير
| الصيغة | المعنى |
|---|---|
| `a else b` | `a` إن لم تكن `nil` وإلا `b`. الطرف الأيمن كسول، والأيسر يُقيَّم مرة واحدة. `0` و`false` و`""` ليست nil |
| `a else b else c` | أول قيمة غير nil |
| `if (c) a else b` | شرط كتعبير (≡ `c ? a : b`)، وتتسلسل `if (a) 1 else if (b) 2 else 3` |

تعمل في `let` والإسناد و`return` والوسائط و`print` وعناصر المصفوفات. 
**`let v = x else { … }`** ما زالت كتلة حارس كما كانت؛ فبعد `else` في `let` القوس `{` = كتلة. لقيمة افتراضية خريطة اكتب `let v = (x else {"p": 80});`. ويمكن الجمع: `let v = a else b else { return; };`.
**`accept x = v if RULE else FALLBACK`** كما كانت: else هناك للـ accept وليست معاملاً.
`return if (c);` (حارس قديم) تُميَّز عن `return if (c) a else b;` بما يلي القوس: `;` = حارس.
تنفيذاً: `a else b` تُختزل إلى `a != nil ? a : b` عند البساطة، وإلا إلى `__else(a, fun() { return b; })`.

## 4) else when / else unless
```kotlin
if (s >= 90) { ... } else when (s >= 80) { ... } else unless (ok) { ... } else { ... }
```
سلسلة مسطّحة بلا تداخل؛ `unless (c)` = `when (!c)`. تعمل مع الجمل المفردة أيضاً.

## الأخطاء (E0013 بنص أوضح)
- `else` تائهة ⇒ «`else` has nothing to attach to» مع قائمة المواضع الصالحة.
- `if (c) a;` كتعبير بلا else ⇒ «an if-expression needs an `else` branch».
- `try {…} else {…}` بلا catch ⇒ يُطلب catch.

## حدود
- `a else b` يختبر `nil` فقط (لا الأخطاء ولا الفراغ).
- فرع `if` كتعبير تعبيرٌ لا كتلة؛ للكتل استعمل `goal { }` أو الدالة.
- داخل `let`/`accept` المجرّدة يتعطّل معامل else حتى الأقواس أو الوسائط (لأن else هناك شيء آخر).
- جُرِّبت كل الصيغ على المفسِّر وعلى `rin --vm` (المخرجات متطابقة)، و`rin analyze` لا يحذّر منها (استُثنيت الدوال الداخلية `__*` من SEM003).
- القيمة البديلة في `a else b` تقف عند مستوى `pipeline`: لا تحوي `?:` ولا إسناداً دون أقواس (`x else (c ? 1 : 2)`).
- `rin build` يستعمل `compiler/rinc.cpp` وهو مترجم مستقلّ بمحلّل خاص أصغر لا يدعم أصلاً `nobreak` ولا `let … else {}`؛ فلا يدعم هذه الصيغ أيضاً (حدّ قائم قبل هذا العمل).
- محلّل `rin analyze` يبلّغ SEM002 كاذباً لأسماء `let {a, b} = …` و`accept` (سلوك قديم، مستقل عن else).

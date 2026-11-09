# دمج المفاهيم الموجودة — الدفعة الثانية (else وما حولها)

> قاعدة هذه الدفعة: **لا كلمات ولا متغيّرات ولا مفاهيم جديدة**. كل صيغة هنا اتحاد لكلمات موجودة أصلاً (`try` `else` `match` `achieve` `retry` `unless` `when` `otherwise` `let` `for`)، وتُختزل في المحلّل إلى عُقد قائمة؛ كل كود قديم يعمل كما كان. اختبارها: `tests/print/merge_*.rin` و`else_*.rin`. تكمل `docs/else.md` و`docs/merge_concepts.md`.

| الدمج | ما يجمعه | الصيغة | يحلّ مشكلة |
|---|---|---|---|
| `try` كتعبير | `try` + `else` + أي موضع قيمة | `let v = try f() else 0;` · `try f();` | «خطأ ⇒ قيمة بديلة» بلا كتلة try/catch من 5 أسطر |
| `match` كتعبير | `match` + `goal`/`achieve` | `let r = match (x) { case 1 { achieve "a"; } else { achieve "b"; } };` | تحويل قيمة إلى قيمة دون دالة مساعدة |
| `retry … else` | `retry` + `else` | `retry 3 { call(); } else { fallback(); }` | استنفاد المحاولات بلا إعادة رمي ولا try خارجي |
| `unless … else` | `unless` + `else` | `unless (ok) { … } else { … }` | شرط معكوس بسلسلة كاملة |
| سلسلة `when` | `when` + `otherwise`/`else` + `else when` | `when (a) {…} otherwise when (b) {…} otherwise {…}` | سلاسل مسطّحة بدل `else if` المتداخلة |
| `for` على قاموس | `let` (تفكيك) + `for` + قاموس | `for (let [k, v] in map) { … }` | المرور على المفتاح والقيمة معاً |
| `else` للحلقات/`try`/التعبيرات | انظر `docs/else.md` | `for … else` · `try … else` · `a else b` · `if (c) a else b` | — |

## التفاصيل
**`try EXPR [else FALLBACK]`**: القيمة هي `EXPR`، أو عند أي خطأ (`throw` أو خطأ تشغيل كالفهرس خارج المدى) `FALLBACK`، و`nil` إن غابت else. البديل كسول. تجري المحاولة عند أول كلمة بعد `try` إن كانت قيمة/اسماً (لا `{` فتلك جملة try، ولا `(` فتلك دالة اسمها try). `try f();` كجملة تبتلع الخطأ. يتركّب مع `else`: `(try f() else nil) else "افتراضي"`.
**`match (x) {…}` كتعبير**: قيمته قيمة أول `achieve` نُفِّذ، وإلا `nil`. يعمل أيضاً بلا موضوع: `match { case (n < 0) { achieve "neg"; } … }`. جملة `match` العادية كما هي.
**`retry N { … } else { … }`**: تُنفَّذ else بعد فشل المحاولة الأخيرة بدل رمي خطأها. بلا else السلوك القديم تماماً. (على خلاف `for … else` حيث else = «دون break».)
**`unless (c) S [else …]`** تُميَّز عن نداء دالة اسمها `unless` بما يلي القوس (`{` أو بداية جملة).
**سلاسل when**: `otherwise` و`else` مترادفتان بعد `when`، وكلتاهما تقبل بعدها `when (c)` أو `unless (c)`.
**`for (let [k, v] in map)`**: نمط المصفوفة على قاموس يأخذ أزواجاً `[مفتاح، قيمة]`؛ `for (let k in map)` تبقى مفاتيح كما كانت.

## حدود
- `try EXPR` يلتقط الأخطاء لا الإشارات الداخلية (`break/continue/return` تعبر كما هي).
- `for [k, v] in map` منفَّذ في المفسِّر؛ مشغِّل VM المستقلّ (`rin_bcvm_exec.cpp`) لم يُعدَّل لأنه غير مرتبط بهذا البناء.
- `rin build` (rinc المستقل) لا يدعم هذه الصيغ، كما لا يدعم `nobreak`/`let … else` أصلاً.
- `match` كتعبير يتطلب `achieve` صريحة؛ لا قيمة ضمنية لآخر تعبير في الحالة.

## الدفعة 3
| الدمج | الصيغة | ملاحظة |
|---|---|---|
| حلقة كتعبير | `let r = for (let x in xs) { if (c) { achieve x; } } else { achieve d; };` | `for`/`while`/`loop`/`retry`/`repeat` تُلفّ في `goal`: القيمة = أول `achieve`، وإلا `nil`. مع `else` = «ابحث وإلا بديل» |
| `else` مع المختزلات | `first each x in xs where c yield e else "لا شيء"` | else الآن للنتيجة كلها لا للـ`yield` (كانت ستُبتلع داخله) |
| تركيب شامل | حاوية + `ttl` + `val … = try … else …` + `getField(…) else …` | يعمل بلا كود جديد؛ انظر `merge_compose.rin` |

حدّ: `a else b` لا تُقرأ داخل نصوص القوالب `"{…}"` إن احتوت علامات اقتباس مزدوجة (قيد القوالب نفسها).

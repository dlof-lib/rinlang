# إدخال المستخدم (Input)

دوال مبنية تجعل البرنامج يسأل المستخدم ويقرأ إجابته أثناء التشغيل. لا تحتاج `@import`.

**مبدأ التصميم:** لا توجد هنا دوال أو مفاهيم جديدة. الدوال الأربع **تستدعي** مفاهيم اللغة الموجودة
(`trim` · `lower` · `toNumber` · `contains` · `callValue` · `oop.get` · `oop.set` · `setField` ·
`container.fieldNames` · `container.fieldType` · `container.snapshot` · `getField` · `setState` · `oop.toMap`)، لذلك تعمل مع `enum` و`class` و`@container` و`fun`
بنفس قواعدها (الصلاحيات، `set`، التجميد، المراقبون، الروابط) دون أي كود خاص.

| الدالة | تعيد | التوقيع الكامل |
|---|---|---|
| `input` | نصاً | `input(prompt?, validator?, target?, key?)` |
| `inputNumber` | رقماً | `inputNumber(prompt?, validator?, target?, key?)` |
| `confirm` | `true`/`false` | `confirm(prompt?, target?, key?)` |
| `choose` | قيمة الخيار | `choose(prompt, options, target?, key?)` |

التوقيعات القديمة (`input(p)` · `inputNumber(p)` · `confirm(p)` · `choose(p, array)`) تعمل كما هي حرفياً.

```rin
let name = input("ما اسمك؟ ");
let age = inputNumber("عمرك؟ ");
if (confirm("هل تريد المتابعة؟ ")) {
    let color = choose("لونك المفضل:", ["أحمر", "أخضر", "أزرق"]);
    print name + " (" + age + ") اختار " + color;
}
```

## 1) `validator` — مُدقِّق (يستدعي `fun` أو كائن OOP)
أي `fun`، أو كائن صنفه يعرّف `__call__`. يستلم القيمة بعد تحويلها (نصاً في `input`، رقماً في `inputNumber`):
يُقبل الجواب إن أعاد قيمة صادقة (`true`)، ويُرفض إن أعاد `false`/`nil`، وإن أعاد **نصاً** فهو سبب الرفض
ويظهر للمستخدم أمام السؤال المُعاد. 5 محاولات ثم خطأ؛ ونهاية `stdin` مع مُدقِّق = إلغاء.

```rin
fun isShort(s) { return len(s) <= 3; }
let w = input("كلمة قصيرة؟ ", isShort);

class Range {
    let lo = 0; let hi = 0;
    fun init(lo, hi) { self.lo = lo; self.hi = hi; }
    fun __call__(n) {
        if (n < self.lo or n > self.hi) { return "خارج " + self.lo + ".." + self.hi; }
        return true;
    }
}
let age = inputNumber("العمر؟ ", Range(0, 120));
```

## 2) `target, key` — تسليم النتيجة مباشرة (يستدعي `oop.set` / `setField`)
| `target` | ما يُستدعى | يعني |
|---|---|---|
| كائن `class`/`struct` أو قاموس | `oop.set(target, key, value)` | تُحترم `private`/`final`/`set`/`freeze` والمراقبون |
| نص = اسم حاوية | `setState(container, key, value)` | تُطلَق روابط OOP/Indsin كأنك كتبت الحقل يدوياً |

تُفحَص الحاوية **قبل** سؤال المستخدم؛ والدالة تعيد القيمة نفسها كي تبقى قابلة للاستخدام في تعبير أو `|>`.

```rin
let u = User();
input("البريد؟ ", u, "email");            // يمرّ عبر set email(v) المعرّفة في الصنف
inputNumber("الصوت؟ ", "Settings", "volume");
confirm("موافق؟ ", flags, "ok");
```

### الحاويات بعمق
- **`setState` لا `setField`:** الكتابة في حاوية تمرّ عبر `setState`، فإن كان الحقل مُعلَناً `state` يُطلَق `on update(prev)` تلقائياً — تتفاعل الحاوية مع إجابة المستخدم كما لو أُسنِد الحقل داخلها.
- **القيمة الحالية:** مع `target, key` تظهر القيمة الحالية للحقل بين `[ ]` أمام السؤال، والإجابة الفارغة (Enter) **تُبقيها** دون كتابة (نصاً في `input`، رقماً في `inputNumber`، منطقياً في `confirm`). لا تظهر للقيم الفارغة/`nil`.
- **وضع النموذج:** `input(prompt, "Container")` (وسيط واحد = اسم حاوية معروفة، أو كائناً، أو قاموساً) يسأل عن **كل حقول الحاوية** مرتبة أبجدياً، ويختار الدالة حسب `container.fieldType` لكل حقل: `number` ← `inputNumber`، `bool` ← `confirm`، `string` ← `input`؛ وتُتخطّى المصفوفات والقواميس والدوال. تعيد `container.snapshot` بعد الملء.

- **الكائنات والقواميس:** نفس وضع النموذج: `input(prompt, obj)` أو `input(prompt, map)` يسأل عن كل حقل بترتيب تعريفه/إدخاله، ويختار الدالة من نوع القيمة الحالية (`number`/`bool`/`string`)، ويتخطى الحقول `nil` أو المركّبة ما لم يُعطَ لها مخطّط (انظر أدناه). كل حقل يُكتب عبر `oop.set` فيُطبَّق `set` المعرَّف في الصنف والصلاحيات؛ ويُرفض الكائن المجمَّد *قبل* أول سؤال. تعيد الكائن/القاموس نفسه (وهو مُعدَّل مكانه).

### المخطّط (schema) — حقول `nil` وأنواع وقواعد لكل حقل
`input(prompt, target, {حقل: مواصفة, ...})` — الهدف حاوية/كائن/قاموس، والمخطّط قاموس يضيف أو يُعدّل مواصفة كل حقل. حقول المخطّط غير الموجودة في الهدف (مثل `nil`) تُسأل بعد حقوله. المواصفة:

| المواصفة | تُستدعى |
|---|---|
| `"number"` · `"bool"` · `"string"` | `inputNumber` · `confirm` · `input` صراحةً |
| مصفوفة · قاموس · `enum` | `choose(label, spec, target, key)` — النتيجة قيمة الخيار (حالة enum نفسها) |
| `fun` أو كائن بـ `__call__` (مثل أصناف inputkit) | `validator`؛ ونوع الحقل من حقل `type` في صنف المُدقِّق |
| `nil` | يُتخطّى الحقل |

```rin
@import "lib/inputkit.og.rin";
let u = User();                       // age/email/role = nil
input("User> ", u, {
    "name":  Every([Required(), Length(2, 20)]),
    "age":   Every([Integer(), Range(1, 120)]),
    "email": Email(),
    "role":  Role,                    // enum => choose
    "vip":   "bool"
});
```

### أصناف التحقق `lib/inputkit.og.rin`
مكتوبة بـ Rin نفسها (`interface Check` ← `abstract class Rule` ← الأصناف)، بلا أي دالة مبنية جديدة. كل صنف يعيد `true` أو **نصاً** هو سبب الرفض، ويقبل `.withMessage("...")` لرسالة مخصّصة:
`Required()` · `Length(min,max)` (بالمحارف عبر `utf8Len` فالعربية صحيحة) · `Range(lo,hi)` · `Integer()` · `OneOf([..])` · `Matches(regex)` · `Email()` · `Every([rules])`.
لإضافة قاعدتك: `class Even extends Rule { fun init() { self.type = "number"; } fun __call__(v) { if (v % 2 != 0) { return self.fail("يجب أن يكون زوجياً"); } return true; } }`

```rin
@container=Profile
    state name = "Ali";
    state age = 30;
    let subscribed = false;
    on update(prev) { print "changed"; }
.end/container

let snap = input("Profile> ", "Profile");     // age ثم name ثم subscribed
input("الاسم؟ ", "Profile", "name");           // يعرض [Sara]؛ Enter يُبقيه
```

## 3) `choose` — مصادر الخيارات
| `options` | الخيارات المعروضة | القيمة المُعادة |
|---|---|---|
| مصفوفة | عناصرها (للكائنات/القواميس: `name` ثم `title` ثم `label` عبر `oop.get`) | العنصر نفسه |
| قاموس | مفاتيحه | القيمة المقابلة |
| `enum` (مثل `Role`) | أسماء الحالات | **حالة الـ enum نفسها** (تعمل مع `match`/`==`) |
| كائن | حقوله (`oop.toMap`) | قيمة الحقل |
| نص = اسم حاوية | حقولها مرتبة أبجدياً (`container.fieldNames`) | `getField` للحقل |

يجيب المستخدم برقم الخيار (من 1) أو بتسميته تماماً.

```rin
enum Role { Admin, Editor, Viewer }
choose("الدور:", Role, u, "role");        // u.role == Role.Editor
match (u.role) { case Role.Admin { print "مدير"; } else { print "غيره"; } }
let who = choose("من؟", team);            // team مصفوفة User — تُعرض بـ name
```

## 4) أنابيب وتركيب
لأنها دوال عادية: `let n = input("n? ") |> toNumber;` · نموذج كامل: `for (let f in oop.fields("User")) { input(f + ": ", u, f); }`.

## أين يعمل وكيف
- **التطبيق (أندرويد):** تظهر نافذة إدخال فوق المحرر عند كل سؤال. ما طُبع قبل السؤال يظهر في الكونسول أولاً.
  أثناء انتظار إجابتك **لا تُحتسب** مهلة التنفيذ (15 ثانية). إن ضغطت "إلغاء" يتوقف البرنامج بخطأ
  `تم إلغاء الإدخال`. سقف الانتظار لسؤال واحد 10 دقائق.
- **سطر الأوامر (`rin run`):** يقرأ من `stdin` — يعمل مع الأنابيب: `printf 'Droy\n41\n' | rin run app.rin`.
  عند نهاية `stdin`: `input()` تعيد `""`، أما `inputNumber`/`confirm`/`choose` (وأي `input` بمُدقِّق) فتتوقف بخطأ إلغاء.
- **مدمجاً (`bindings/` و WASM):** لا يوجد مصدر إدخال مربوط افتراضياً، فتسلك سلوك `stdin` أعلاه.

## ملاحظات
- `inputNumber` تقبل نصاً رقمياً كاملاً فقط (`12abc` تُرفض) رغم أن `toNumber` وحدها تقبل البادئة الرقمية.
- `confirm` تقبل: `y` · `yes` · `true` · `1` · `نعم` · `ن`؛ غير ذلك لا.

انظر أيضاً: [`variables.md`](./variables.md) · [`enums.md`](./enums.md) · [`oop.md`](./oop.md) · [`containers.md`](./containers.md) · [`errors.md`](./errors.md)

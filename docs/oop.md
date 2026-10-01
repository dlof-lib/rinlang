# oop.md — البرمجة الكائنية الموسَّعة (Rin 1.0)

يبني هذا الدليل فوق `class` / `struct` / `extends` / `super` الموثَّقة في [`objects.md`](./objects.md).
كل ما هنا **إضافي بحت**: أي برنامج قديم لا يستخدم هذه الكلمات يتصرّف كما كان تماماً. الكلمات
(`interface` `trait` `abstract` `final` `override` `static` `private` `protected` `get` `set`
`implements` `uses` `instanceof`) **سياقية غير محجوزة** — يبقى استخدامها أسماء متغيّرات صالحاً.

الأمثلة المرجعية: [`examples/oop/bank.rin`](../examples/oop/bank.rin) و
[`tests/verification/oop_advanced.rin`](../tests/verification/oop_advanced.rin).

---

## 1) `interface` — عقد بتواقيع فقط

```rin
interface Shape { fun area(); fun name(); }
interface Solid extends Shape { fun volume(); }   // واجهة ترث واجهات

class Square implements Shape {
    let s = 1;
    fun init(s) { self.s = s; }
    fun area() { return self.s * self.s; }
    fun name() { return "square"; }
}
```
- التواقيع تنتهي بـ `;` بلا جسم. لا حقول داخل الواجهة.
- عند **إنشاء** الصنف يُتحقَّق أن كل توقيع له تنفيذ **بنفس عدد الوسائط**، وإلا يظهر خطأ يسمّي الدالة الناقصة والواجهة المطلوبة.
- يمكن تنفيذ أكثر من واجهة: `class A implements X, Y { ... }`.
- لا يمكن إنشاء `interface` مباشرة.

## 2) `trait` — كود قابل لإعادة الاستخدام

```rin
trait Loggable {
    let history = [];
    fun log(msg) { push(self.history, msg); }
}
class Service uses Loggable, Timestamps { ... }
```
- السمة تحمل **حقولاً ودوالاً** (وحتى `get`/`set` و`abstract fun`). حقولها تُهيَّأ لكل كائن على حدة.
- الأولوية: دوال الصنف نفسه ← دوال سماته ← دوال الأب. حقول الصنف تطغى على حقول سماته.
- سمة تستخدم سمات أخرى: `trait A uses B { ... }`.
- `x instanceof Loggable` صحيحة.

## 3) `abstract` / `final` / `override`

```rin
abstract class Account {
    abstract fun fee();                 // بلا جسم؛ يجب أن ينفّذها كل صنف فرعي حقيقي
    final fun id() { return 1; }        // لا يجوز إعادة تعريفها
}
final class Leaf extends Account { override fun fee() { return 0; } }  // لا يمكن الوراثة من Leaf
```
| الكلمة | الأثر |
|---|---|
| `abstract class` | لا يُنشأ مباشرة؛ يجوز أن يحوي `abstract fun`. |
| `abstract fun f(a);` | توقيع بلا جسم؛ مسموح فقط في `abstract class` أو `trait`. |
| `final class` | الوراثة منه خطأ **عند التعريف**. |
| `final fun` | إعادة تعريفها في صنف فرعي خطأ. |
| `final let x = ...;` | حقل يُكتب داخل `init` فقط. |
| `override fun` | تأكيد أن الدالة تستبدل أخرى من الأب/سمة/واجهة؛ خطأ إن لم توجد (يلتقط أخطاء الإملاء). |

## 4) `static`

```rin
class Counter {
    static let total = 0;
    static fun make() { return Counter(); }
    fun init() { Counter.total = Counter.total + 1; }
}
Counter.make();
print Counter.total;       // 1
```
- الحقل static مشترك بين كل النسخ، ويُهيَّأ **كسولاً** عند أول وصول (فيمكنه الإشارة لأصناف معرَّفة لاحقاً).
- يُورَّث: `Child.total` يصل لنفس حقل `Parent`.
- دالة `static fun` بلا `self`. يمكن أيضاً تخزين دالة في حقل static واستدعاؤها.
- لا `static` داخل `trait`، ولا `static get/set`.
- ملاحظة: متغيّر محلي بنفس اسم الصنف يظلّل الصنف (`let Counter = 5;` ثم `Counter.x` تقرأ المتغيّر).

## 5) الصلاحيات: `public` (افتراضي) / `private` / `protected`

```rin
class Account {
    private let balance = 0;          // داخل Account فقط
    protected let owner = "";         // داخل Account وأبنائه
    private fun audit() { ... }
    private fun init() { ... }        // مُنشئ خاص (للـ Singleton مثلاً)
}
```
- تنطبق على الحقول والدوال والخصائص والمُنشئ والأعضاء static.
- الفحص **وقت التشغيل** حسب *الصنف الذي كُتب فيه الكود* (لا حسب الكائن): كائن من `Account` يستطيع قراءة `private` لكائن آخر من `Account`، كما في Java/C#.
- الدوال المجهولة (lambda) داخل دالة صنف ترث سياق ذلك الصنف.
- بلا أي استخدام لهذه الكلمات في البرنامج، لا يُجرى أي فحص إطلاقاً (كلفة صفر).

## 6) الخصائص `get` / `set`

```rin
class Temp {
    let _c = 0;
    get f() { return self._c * 9 / 5 + 32; }
    set f(v) { self._c = (v - 32) * 5 / 9; }
    get c() { return self._c; }       // بلا set => للقراءة فقط
}
let t = Temp();
t.f = 212;      // يستدعي set f
print t.c;      // 100
```
- داخل `get f` نفسها، `self.f` تقرأ **الحقل الخام** (لا تكرار لانهائي)؛ لكن الأفضل اسم حقل مختلف (`_c`).
- الكتابة على خاصية بلا `set` خطأ واضح.

## 7) العوامل والدوال السحرية

| الدالة | يُفعِّل |
|---|---|
| `__add__` `__sub__` `__mul__` `__div__` `__mod__` `__neg__` | `+ - * / % -x` *(موجودة سابقاً)* |
| `__eq__` / `__ne__` | `==` / `!=` بين كائنين (`!=` تعكس `__eq__` إن لم تُعرَّف `__ne__`) |
| `__lt__` `__le__` `__gt__` `__ge__` | `< <= > >=` (إن غابت واحدة تُستعمل المعكوسة للطرف الآخر) |
| `__cmp__(o)` | رقم سالب/صفر/موجب — يغطي كل المقارنات الأربع |
| `__str__()` أو `toString()` | `print` والدمج النصي و`str()` |
| `__len__()` | `len(obj)` |
| `__contains__(x)` | `contains(obj, x)` |
| `__getitem__(i)` / `__setitem__(i, v)` | `obj[i]` / `obj[i] = v` |
| `__call__(...)` | `obj(...)` |
| `__iter__()` | `for (let x in obj)` — يعيد مصفوفة (أو قاموساً → مفاتيحه) |

## 8) `instanceof` وفحص الأنواع

```rin
if (shape instanceof Shape) { ... }        // صنف أب، واجهة، أو سمة
fun total(s: Shape): Number { ... }        // وسيط بنوع واجهة يعمل أيضاً
```
`instanceof` اختصار لـ `oop.isInstance(value, "Name")` وتعمل مع الأنواع المدمجة (`Number` `String` ...).

## 9) استدعاء متسلسل بأي عمق (إصلاح)

`self.engine.start()` و`a.b.c.d()` كانت تفشل بـ "is not a function" (يُدعم مقطع واحد فقط). الآن
تُقرأ المقاطع الوسيطة كحقول/خصائص ثم تُنادى الدالة الأخيرة. كذلك `Class.staticField.method()`.

---

## 10) مرجع دوال `oop.*`

> `cls` = اسم صنف (نص) أو كائن. دوال `get/set/call/new/staticGet/staticSet` **تحترم** الصلاحيات؛
> `peek/poke/toMap/inspect/assign/pick/omit` مفاتيح استبطان **تتجاوزها** عمداً (تشخيص/تسلسل/اختبار) — ولا تتجاوز `freeze`.

**أنواع ووراثة:** `oop.className(x)` · `oop.isInstance(x, T)` / `oop.is` · `oop.exists(name)` ·
`oop.isClass/isInterface/isTrait(name)` · `oop.isAbstract(cls)` · `oop.isFinal(cls)` · `oop.isStruct(cls)` ·
`oop.isSubclass(a, b)` · `oop.parent(cls)` · `oop.ancestors(cls)` · `oop.children(cls)` ·
`oop.descendants(cls)` · `oop.classes(kind?)` (`"class"`/`"interface"`/`"trait"`/`"all"`) ·
`oop.interfaces(cls)` · `oop.traits(cls)` · `oop.implements(cls, iface)`

**استبطان الأعضاء:** `oop.methods(cls, inherited=true)` · `oop.staticMethods(cls)` ·
`oop.abstractMethods(cls)` *(غير المنفَّذ فقط)* · `oop.fields(cls|obj)` · `oop.staticFields(cls)` ·
`oop.properties(cls)` · `oop.hasMethod/hasField/hasProperty/hasStatic(cls, name)` ·
`oop.arity(cls, m)` · `oop.params(cls, m)` · `oop.signature(cls, m)` · `oop.access(cls, name)` ·
`oop.describe(cls)` (قاموس شامل)

**عمليات ديناميكية:** `oop.get(obj, name, default?)` · `oop.set(obj, name, v)` · `oop.peek` · `oop.poke` ·
`oop.call(obj, "m", [args])` · `oop.callStatic(cls, "m", [args])` · `oop.new("Cls", args...)` ·
`oop.newWith("Cls", [args])` · `oop.staticGet/staticSet` · `oop.extend(obj, {name: v|fun})` · `oop.delete(obj, name)`

**أدوات الكائنات:** `oop.toMap` · `oop.fromMap("Cls", map)` *(بلا استدعاء init)* · `oop.entries` ·
`oop.pick(obj, [names])` · `oop.omit(obj, [names])` · `oop.assign(dst, src)` · `oop.clone` ·
`oop.deepClone` · `oop.equals(a, b)` *(مساواة تركيبية عميقة حتى لـ class)* · `oop.same(a, b)` *(هوية)* ·
`oop.hash(x)` *(ثابت بين التشغيلات)* · `oop.toString` · `oop.inspect` *(عرض متعدد الأسطر)* ·
`oop.freeze(obj, deep=false)` · `oop.isFrozen`

**أنماط:** `oop.singleton("Cls", args...)` · `oop.hasSingleton` · `oop.resetSingleton`

**مصفوفات كائنات:** `oop.compare(a, b)` · `oop.sort(arr, desc=false)` · `oop.sortBy(arr, field, desc=false)` ·
`oop.min` · `oop.max` · `oop.pluck(arr, field)` · `oop.groupBy(arr, field)` ·
`oop.findBy(arr, field, v)` · `oop.filterBy(arr, field, v)`

`oop.sort/min/max/compare` تعمل على الأرقام والنصوص، وعلى الكائنات عبر `__lt__`/`__gt__`/`__cmp__`.

---

## 11) دوال الربط (binding)

### أ) ربط الدوال بكائن
```rin
let inc = oop.bind(counter, "inc");     // دالة مربوطة: self ثابتة = counter
inc(); inc();                           // counter.n == 2 (تصلح callback مثل onClick)

fun getN() { return self.n; }
let g = oop.bind(getN, counter);        // ربط دالة/lambda عادية بكائن => تستطيع استخدام self
oop.callWith(getN, other, []);          // نداء لمرة واحدة بكائن آخر
oop.apply(counter.add, [1, 2]);         // نداء بمصفوفة وسائط
let loose = oop.unbind(inc);            // نسخة بلا self -> oop.bind(loose, anotherObj)
oop.bindAll(counter)                    // {اسم: دالة مربوطة} لكل الدوال العامة (بلا init/__x__/private)
oop.bindAll(counter, ["inc", "add"])    // أو أسماء محددة
oop.isBound(fn)   oop.boundTo(fn)
```
`oop.bind(obj, "m")` **تحترم** `private`/`protected` (تُعامَل كنداء من خارج الصنف).

### ب) تطبيق جزئي وCurry
```rin
fun vol(a, b, c) { return a * b * c; }
oop.partial(vol, 2)(3, 4);              // 24   — تثبيت الوسائط الأولى
oop.curry(vol)(2)(3)(4);                // 24   — وسيط واحد لكل نداء
```
تعمل أيضاً على الدوال المربوطة: `oop.partial(counter.add, 5)(6)`.

### ج) ربط الخصائص (Data binding)
```rin
let id = oop.observe(model, "name", fun(newV, oldV, obj, field) { ... });  // "*" = أي حقل
oop.bindProperty(src, "v", dst, "w");                       // اتجاه واحد + مزامنة فورية
oop.bindProperty(src, "v", dst, "label", fun(x) { return "v=" + str(x); }); // مع تحويل
oop.bindTwoWay(a, "v", b, "w");                             // اتجاهان -> [id, id]
oop.unobserve(id)   oop.unobserveAll(obj)   oop.observers(obj)
```
- تُطلَق عند أي كتابة: `obj.f = v` أو `oop.set/poke/assign` أو `set f(v)` (للخصائص المحسوبة يُعطى القيمة قبل/بعد عبر `get`).
- تُطلَق **فقط عند تغيّر القيمة فعلاً**؛ وهذا ما يُنهي حلقة `bindTwoWay` تلقائياً.
- الـ callback يقبل أي عدد وسائط من 0 إلى 4 `(new, old, obj, field)` — الزائد يُقصّ.
- المراقب لا يُبقي الكائن حياً (يعتمد `weak_ptr`)، وتُنظَّف المراقبات المنتهية تلقائياً.
- كلفة صفر على الكتابة ما دام لم تُسجَّل أي مراقبة.

### د) الأحداث
```rin
oop.on(btn, "click", fun(x, y) { ... });     // -> id
oop.once(btn, "click", fun() { ... });       // يُزال بعد أول تشغيل
oop.emit(btn, "click", 1, 2);                // -> عدد المستمعين المستدعَين
oop.listeners(btn, "click")   oop.off(btn, "click")   oop.off(btn)   // كل أحداث الكائن
```

**ملاحظات:** يُرفض الربط داخل كائن مجمَّد (`oop.freeze`). المراقبون على كائنات `struct` ينطبقون على *النسخة* التي سُجّل عليها فقط، لأن `struct` تُنسخ عند الإسناد.
---

## 12) الربط بالحاويات (container) وبـ Indsin

يربط **حقل كائن** بهدف خارجي، وكل كتابة على أحد الطرفين تنتقل إلى الآخر تلقائياً. الأمثلة المرجعية:
[`tests/verification/oop_links.rin`](../tests/verification/oop_links.rin) (حاويات + Warp عبر CLI) و
[`tests/tools/test_indsin_oop_bind.cpp`](../tests/tools/test_indsin_oop_bind.cpp) (جلسة Indsin حقيقية + tap).

### أ) الحاويات
```rin
@container=Store
    state counter = 0;
    on update(prev) { print "changed from " + str(prev); }
.end/container

let m = Model();
oop.bindContainer(m, "n", "Store", "counter");          // الكائن -> الحاوية
oop.bindContainerFrom(m, "n", "Store", "counter");      // الحاوية -> الكائن
oop.bindContainerTwoWay(m, "n", "Store", "counter");    // اتجاهان
```
- الكتابة في الحاوية تمر بنفس مسار الإسناد المراعي لـ `state`، فيُطلَق `on update(prev)` تلقائياً.
- المسارات التي تُلتقط عند الحاوية: الإسناد داخلها (`x = v`)، و`setState` و`setField`/`container.set`.
- مراقبة بحتة بلا كائن: `oop.watchContainer("Store", "counter" | "*", fun(new, old, key, container) { ... })` → id.
- نسخ لمرة واحدة: `oop.toContainer(obj, "Store", [fields]?)` (حقول الكائن **العامة** فقط ما لم تُحدَّد أسماء) و
  `oop.fromContainer(obj, "Store", [fields]?)` (الحقول الموجودة أصلاً في الكائن). كلاهما يعيد عدد الحقول المنسوخة.

### ب) Indsin (خلايا Warp)
خلايا `warp name = ...;` هي متغيّرات عامة في مفسّر الجلسة، وIndsin ينقلها إلى المفسّر قبل كل معالج
(`onTap` وغيره) ويقرؤها بعده. الربط يستفيد من هذا مباشرة:
```rin
warp count = 0;
let model = Model();
oop.bindWarpTwoWay(model, "n", "count");          // اتجاهان — الأنسب للواجهات التفاعلية
oop.bindWarp(model, "msg", "label");              // الكائن -> الشاشة فقط
oop.bindWarpFrom(model, "flag", "on");            // الشاشة -> الكائن فقط
oop.bindView(form, "ui_");                        // كل حقل بدائي عام f مع الخلية ui_f (إن كانت معرَّفة)
oop.bindView(form, {title: "ui_title"});          // أو خريطة صريحة {حقل: خلية}

fun inc() { model.n = model.n + 1; }              // معالج عادي: الخلية count تتغيّر وIndsin يُعيد رسم ما يعتمد عليها
```
- **كتابة الكائن** تظهر في `changedWarpNames` لنفس الـ tap، فتُحلَّل العناصر المشتركة في الخلية من جديد.
- **كتابة الشاشة** (خلية غيّرها Indsin) تُسحَب إلى الكائن **قبل** تشغيل المعالج التالي، فيرى المعالج حالة متّسقة.
- في تشغيل `rin run` العادي (بلا Indsin) لا أحد ينقل الخلايا، فاستدعِ `oop.syncLinks()` بعد أي كتابة خام على خلية.
- حدّ Warp: Indsin يخزّن الخلايا أرقاماً أو نصوصاً (والمنطقي نصاً "true"/"false" يُعاد منطقياً) — اربط حقولاً بدائية.
  `nil` تُعامَل كنص فارغ في الاتجاهين. الحاويات تقبل أي قيمة.

### ج) الخيارات المشتركة `opts`
```rin
oop.bindContainer(m, "name", "Store", "title", {transform: fun(v) { return "Hi " + v; }, init: "object"});
```
| الخيار | المعنى |
|---|---|
| `transform: fn(value, obj)` | تحويل القيمة العابرة في الاتجاه الوحيد (push أو pull). **مرفوض مع two-way** لأن لا معكوس له. |
| `init` | المزامنة الأولية: `"object"` (الافتراضي لـ push وtwo-way: الكائن مصدر الحقيقة)، `"target"` (الافتراضي لـ pull: القيمة تُنسخ من الهدف إلى الكائن)، `"none"` (لا شيء). |

### د) السلوك الدقيق
- **لا حلقات:** الاتجاهان يمنعان الصدى بتذكّر آخر قيمة نُقلت، والنقل لا يحدث إلا عند تغيّر القيمة فعلاً.
- الحقول `private`/`protected` والخصائص `get`/`set` تُربَط (الربط يعمل بسياق صنف الكائن). حقول `final` والكائنات المجمَّدة
  (`oop.freeze`) ترفض السحب إليها بخطأ — وكتابة الحاوية نفسها **تكون قد تمت** قبل أن يُرفَض السحب إلى الكائن.
- الإلغاء: `oop.unobserve(id)` و`oop.unobserveAll(obj)`؛ وتظهر الروابط في `oop.observers(obj)` بأنواع `container`/`warp`.
- الربط لا يُبقي الكائن حياً (`weak_ptr`)، وتُنظَّف الروابط المنتهية تلقائياً. كلفة صفر على الحاويات والـ tap ما لم يُنشأ رابط.
- `oop.syncLinks()` يسحب يدوياً كل الروابط التي تقرأ من حاوية/خلية (مفيد بعد كتابات خام لا تمر بمسارات الإسناد، مثل `container.restore`).

---

## 13) حدود معروفة (بصراحة)

- الصلاحيات و`abstract` و`final` و`override` تُفحَص **وقت التشغيل** (عند الوصول/الإنشاء)، لا ساكنياً؛
  لكن مخالفات الوراثة من `final class` تُكتشف عند التعريف.
- `rin build` (المترجم الأصلي `rinc`) لا يدعم OOP أصلاً — كما قبل؛ استخدم `rin run`.
- وراثة واحدة فقط (`extends`)، والتعدد عبر `implements`/`uses`.
- لا generics، ولا `protected` خاصة بالحزمة (package)، ولا `static get/set`، ولا `static` داخل `trait`.
- `struct` تبقى بدلالة القيمة؛ `oop.freeze` على نسخة struct يجمّد تلك النسخة فقط.
- الربط بالحاويات يلتقط الكتابات التي تمر بمسارات الإسناد (`x = v` داخل الحاوية، `setState`، `setField`)؛ أما الكتابات الخام مثل `container.restore` فتحتاج `oop.syncLinks()`.
- الربط بـ Indsin يتم على مستوى **خلايا Warp** (لا على سمات العناصر مباشرة): العنصر يقرأ الخلية بـ `{cell}` والكائن مربوط بالخلية.

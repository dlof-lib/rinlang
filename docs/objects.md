# objects.md — `class`/`struct`، `.object()`، `@Object`، `document`

في Rin أربع طرق مختلفة لتمثيل "شيء له حقول"، كل واحدة لغرض مختلف: `class`/`struct`
لبيانات + سلوك (methods)، `.object("id")` لكائن سريع بمعرِّف صريح ومعاينة حيّة،
`@Object=name` لتسجيله كحاوية داخل شجرة الحاويات، و`document` لسجل داخل حاوية NoSQL
(`@container.doc`).

## `class` / `struct`

```rin
class Animal {
    let name = "unknown";
    fun speak() {
        print self.name + " makes a sound";
    }
}
class Dog extends Animal {
    fun speak() {
        print self.name + " barks";
    }
}
let d = Dog();
d.name = "Rex";
d.speak(); // Rex barks
```

- `self` (وليس `this`) هو مرجع النسخة الحالية داخل أي طريقة (method).
- `fun init(...) { ... }` — إن عُرِّفت — هي المُنشئ (constructor)، تُستدعى تلقائيًا
  عند `ClassName(args)`:
  ```rin
  class Counter {
      let value = 0;
      fun init(start) { self.value = start; }
      fun inc() { self.value = self.value + 1; }
  }
  let c = Counter(10);
  c.inc();
  print c.value; // 11
  ```
- `extends` وراثة عادية — الصنف الفرعي يرث الحقول والطرق، ويمكنه إعادة تعريف
  (override) أي طريقة.
- `struct` بنفس صياغة `class` حرفيًا، والفرق الوحيد **دلالة وقت التشغيل**: نسخة
  `struct` لها **دلالة قيمة (value semantics)** — تُنسخ عند الإسناد — بينما `class`
  لها **دلالة مرجع (reference semantics)** الافتراضية:
  ```rin
  struct Point { let x = 0; let y = 0; }
  let p1 = Point();
  p1.x = 5;
  let p2 = p1;   // نسخة مستقلة، لا مرجع لنفس p1
  p2.x = 99;
  print p1.x; // 5  -- لم يتأثر
  print p2.x; // 99
  ```

## `.object("id") ... .end/object` — كائن سريع بمعرِّف

```rin
.object("user01")
    name("ABOO");
    age(19);
    online(true);
.end/object

print user01;          // الكائن مُعرَّف تلقائيًا كمتغيّر باسم المعرِّف نفسه
print user01["name"];  // فهرسة عادية كأي قاموس آخر
```
كل سطر بداخله هو استدعاء دالة باسم الحقل: `field(value)` يضبط الحقل، و`field()`
بلا وسيطة تعني قيمته `nil`. الصياغة المُنمَّطة `field:(value)` مرادفة تمامًا
لـ`field(value)` بلا أي فرق دلالي.

`container.();` (اختياري) يسجّل الكائن في سجل عام (objectRegistry) يمكن الوصول
إليه لاحقًا بنفس المعرِّف من أي مكان — يحتاجه `view.print/object` عند تمرير نص
(id) بدل الكائن نفسه مباشرة:
```rin
.object("user02")
    name("Sara");
    age(27);
    container.();
.end/object

view.print/object("user02");  // معاينة حيّة بالبحث عن id مسجَّل
view.print/object(user01);     // أو معاينة قيمة كائن مباشرة (بلا تسجيل مسبق)
```

## `@Object=name ... .end/Object`

اختصار لـ`@container.object=name ... .end/container.object` — يسجِّل حاوية بيانات
نقية (بلا دوال ولا حاويات متداخلة) من نوع `ContainerKind::OBJECT` داخل شجرة
الحاويات. الحقول بداخلها تُكتب بصياغة `let` عادية (لا صياغة `field(value)` كما في
`.object()`):
```rin
@Object=profile
    let name = "Sara";
    let age = 30;
.end/Object
```
بخلاف `.object("id")`، هذا الشكل **لا** يُعرِّف `profile` تلقائيًا كمتغيّر قابل
للقراءة المباشرة بعد `.end/Object` — الوصول إلى محتواه يمر عبر دوال الحاويات
الديناميكية العامة (`getField(container, key)`, `hasField(...)`, ...) الموثَّقة في
[`containers.md`](./containers.md)، لا عبر اسمه مباشرة كأي `let` عادي.

## `document` — سجل داخل حاوية NoSQL

يُستخدَم **داخل** جسم `@container.doc` (أو `@doc`) فقط — لا بعد إغلاقها:
```rin
@container.doc=users
    document id="u1" fields={ name: "Ali", age: 30 };
.end/container.doc
```
`fields` قاموس حر البنية (schema-less) تمامًا كمستندات JSON في قواعد بيانات
NoSQL — إدراج بنفس `id` موجود مسبقًا يُحدِّثه بدل تكراره.

## انظر أيضًا
- [`containers.md`](./containers.md) — `@container` بكل أنواعه (`doc`, `data`,
  `table`, ...) ودوال الوصول الديناميكي العامة للحاويات.
- [`variables.md`](./variables.md) — القواميس (map) التي تُبنى عليها كل هذه الأشكال.
- [`enums.md`](./enums.md) — بديل لبيانات مغلقة (closed set) بدل كائن حر.

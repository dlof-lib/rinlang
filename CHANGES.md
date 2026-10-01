# تعديلات على محرّك RinLang (rin_interpreter.cpp / rin_parser.cpp / rin_parser.h / rin_ast.h)

تم بناء واختبار كل ما هنا فعلياً محلياً (g++ -std=c++17) عبر `tools/rin_run.cpp` (مُشغِّل
CLI مستقل)، وأُعيد بناء `tools/test_containers.cpp` بعد كل جولة تعديلات وقورن ناتجه
حرفاً بحرف بالناتج قبل أي تعديل — **متطابق تماماً** في كل مرة (بلا أي تراجع في
container/pipeline/namespace natives).

---

## Rin 1.5 — المكتبات والحزم وجسر C++ وJSON وrintest (فوق تحديث OOP)

- **جديد:** `rin_extra_natives4.cpp`: `json.*` (مسارات/diff/patch/validate/canonical)، `semver.*`، `pkg.*` (rin.toml، ترتيب التبعيات، scaffold، api، checksum)، `cpp.*` (جسر C++ مقفل افتراضياً + `rin_abi.h`).
- **جديد:** `lib/rintest.og.rin` و`lib/packkit.og.rin` (مضمَّنتان في `rin_stdlib_libs.h`، تعملان بلا ملفات على القرص).
- **جديد:** `rin --allow-native` (linux/macos) و`${CMAKE_DL_LIBS}` في CMake. اختبار شامل: `tests/rintests.rin` (54 اختباراً، 58 مع `--allow-native`). التفاصيل: `docs/packages-and-interop.md`.
- لا تغيير في أي سلوك سابق؛ اختبارات OOP وextra_natives 1/2 تمر كما كانت.

---

## Rin 1.4 — OOP الموسَّع

ملفات جديدة: `rin_oop.cpp` (النواة) و`rin_oop_natives.cpp` (دوال `oop.*`) — تُضمَّنان تلقائياً في نهاية
`rin_interpreter.cpp` كبقية ملفات natives، فلا تغيير في أي CMake/Gradle/CI.
التفاصيل والأمثلة: [`docs/oop.md`](./docs/oop.md).

- **جديد:** `interface` + `implements`، `trait` + `uses`، `abstract class/fun`، `final class/fun/let`، `override`.
- **جديد:** `static let/fun` (تهيئة كسولة)، `public/private/protected`، خصائص `get`/`set`.
- **جديد:** `__eq__ __ne__ __lt__ __le__ __gt__ __ge__ __cmp__ __str__/toString __len__ __contains__ __getitem__ __setitem__ __call__ __iter__`.
- **جديد:** `instanceof`، وفحص الأنواع `x: Interface/Trait` صار يشمل الواجهات والسمات.
- **جديد:** 70+ دالة `oop.*` (استبطان، ديناميكية، clone/freeze/hash/equals، singleton، فرز/تجميع كائنات).
- **إصلاح:** النداء المتسلسل `self.engine.start()` / `a.b.c()` كان يفشل بـ "is not a function".
- **جديد (دوال الربط، `rin_oop_bind.cpp`):** `oop.bind/bindAll/unbind/isBound/boundTo/apply/callWith`، `oop.partial/curry`، `oop.observe/bindProperty/bindTwoWay/unobserve/unobserveAll/observers`، `oop.on/once/off/emit/listeners`. اختبار: `tests/verification/oop_binding.rin`.
- التوافق: كل الكلمات سياقية غير محجوزة؛ حزمة `tests/verification` بلا أي تغيير في نتائجها (الفشل الوحيد `container_advanced.rin` قائم قبل هذه الجولة).
- اختبار جديد: `tests/verification/oop_advanced.rin` · مثال: `examples/oop/bank.rin`.

---

## الجولة 1 (سابقاً)

### 1) إصلاح: `obj.method()` على متغيّر عادي كان يفشل دائماً
`let a = Animal(); a.speak();` كان يرمي "not a function" رغم أن class/inheritance/super
مُطبَّقة بالكامل، لأن `Parser::call()` يحوّل أي `IDENT.IDENT(...)` (جذره متغيّر بسيط) إلى
نداء namespace نصي (نفس آلية `make.qr()`) دون تفريق. أُصلح في
`Interpreter::invokeCallee` بفحص fallback: إن كان الجذر متغيّراً حقيقياً في `env` وقيمته
INSTANCE/MAP، يُنفَّذ نفس توزيع `MethodCallExpr`. نفس الإصلاح لـ `super.method()`.

### 2) ميزة جديدة: تعبيرات لامبدا `fun(params) { body }`
لم يكن للغة أي صياغة لدالة مجهولة كتعبير (فقط `fun name(...) {}` كعبارة). أُضيفت عقدة
`LambdaExpr` (rin_ast.h) + حالة في `Parser::primary()` (تُفعَّل فقط في موضع تعبير، إضافية
بحتة) + حالة في `Interpreter::evaluate()` تبني `Callable` مربوطاً بـ closure حقيقية.

---

## الجولة 2 (هذه الجولة)

### 3) إصلاح: كل closures داخل حلقة `for` كانت تتشارك نفس المتغيّر (كل واحدة تُرجع القيمة
### الأخيرة بدل قيمة تكرارتها)
```rin
let fns = [];
for (let i = 0; i < 3; i = i + 1) { fns[len(fns)] = fun() { return i; }; }
print fns[0](); print fns[1](); print fns[2]();
// قبل الإصلاح: 3 3 3   (خطأ -- كل closure تشارك نفس forEnv)
// بعد الإصلاح: 0 1 2   (صحيح -- نفس دلالة JS `let`/Swift/Kotlin/Rust)
```
**السبب:** `execute(ForStmt)` كان يُنفِّذ جسم كل تكرارة داخل نفس `forEnv` الحرفية طوال
الحلقة، فكل closure أُنشئت بداخل الجسم تلتقط نفس المتغيّر المتغيّر (لا قيمة ثابتة لكل
تكرارة). **الإصلاح:** كل تكرارة الآن تُنفَّذ داخل `iterEnv` جديدة (نسخة/snapshot من قيم
`forEnv` وقت بداية تلك التكرارة تحديداً)، مع نقل أي تعديل يحصل عليها داخل الجسم (مثل
`i = i + 10;` صريحة داخل الجسم نفسه، وليس فقط `increment` القياسية) رجوعاً إلى `forEnv`
بعد كل تكرارة، حتى تبقى `condition`/`increment` تريان أي تعديل كهذا كما كانت قبل
الإصلاح تماماً. تم التحقق أن `i = i + 10` بداخل الجسم لا يزال يقطع الحلقة مبكراً كما كان.

### 4) ميزة جديدة: استدعاء نتيجة تعبير عشوائي مباشرة (`arr[0]()`, `(fun(x){...})(1)`, `getFn()()`)
كان أي `(` بعد أي شيء غير اسم بسيط (IDENT) يرمي خطأ تحليل صريح ("only functions can be
called")، فلا يمكن استدعاء عنصر مصفوفة يحمل دالة مباشرة، أو استدعاء نتيجة دالة أخرى
مباشرة، أو استدعاء lambda فوراً (IIFE). أُضيفت عقدة `CallValueExpr` (rin_ast.h): بدل رمي
الخطأ، `Parser::call()` يبني الآن هذه العقدة (تُقيَّم أي تعبير callee إلى قيمة FUNCTION ثم
تُستدعى) — **إضافية بحتة تماماً**: كانت الحالة التي تعالجها خطأ تحليل صريح قبلاً، فلا يمكن
أن تُغيّر تحليل أي برنامج كان صالحاً سابقاً.

```rin
let fns = [fun(){return 1;}, fun(){return 2;}];
print fns[0]();              // 1
print (fun(x){return x*10;})(4);   // 40  (IIFE)
fun makeAdder(n) { return fun(x) { return x + n; }; }
print makeAdder(3)(4);       // 7  (نداء متسلسل)
```

### 5) ميزة جديدة: حلقة `for (let NAME in iterable) { body }` (لم تكن موجودة إطلاقاً)
الوسيلة الوحيدة للتكرار على مصفوفة/قاموس كانت `for` على طراز C مع فهرس عددي يدوي
(`for (let i=0; i<len(arr); i=i+1) { arr[i] ... }`). عقدة `ForInStmt` جديدة (rin_ast.h) +
تمييز في `Parser::forStatement()` (نظرة 3 رموز للأمام: `let` + IDENT + IDENT("in")، فلا
لبس مع `for (let i = 0; ...)` العادية إطلاقاً؛ "in" كلمة سياقية غير محجوزة، تبقى تعمل
اسم متغيّر عادي بلا أي تغيير) + تنفيذ في `Interpreter::execute(ForInStmt)`. يدعم:
مصفوفة (كل عنصر)، قاموس (كل مفتاح، بنفس ترتيب `keys()`)، نص (كل محرف كنص بطول 1).
`break`/`continue` يعملان بداخلها بنفس الدلالة المعتادة، وكل تكرارة لها بيئة خاصة بها من
الصفر (نفس إصلاح #3 أعلاه)، فـ closures بداخلها تلتقط القيمة الصحيحة لكل تكرارة أيضاً.

```rin
for (let x in [10, 20, 30]) { print x; }             // 10 20 30
let m = {"a": 1, "b": 2};
for (let k in m) { print k + "=" + m[k]; }            // a=1  b=2
for (let c in "abc") { print c; }                     // a b c
```

---

## ملاحظة حول `rinc.cpp`
لم يُعدَّل (لا `compiler/rinc.cpp` ولا `app/src/main/cpp/rinc.cpp`): كلاهما يُضمِّن ملفات
المفسّر الحقيقية وقت البناء عند تفعيل "وضع تضمين المفسّر"، فيستفيدان تلقائياً من كل
الإصلاحات/الميزات أعلاه بمجرد إعادة بنائهما من هذه النسخة المعدَّلة.

## طريقة التطبيق
انسخ الملفات الأربعة إلى نفس المسارات في المستودع الأصلي (استبدال كامل):
- `app/src/main/cpp/rin_ast.h`
- `app/src/main/cpp/rin_parser.h`
- `app/src/main/cpp/rin_parser.cpp`
- `app/src/main/cpp/rin_interpreter.cpp`

ثم أعد البناء بأي من المسارات الموجودة أصلاً (Gradle/NDK، `scripts/build_all.sh desktop`،
أو `tools/rin_run.cpp` كمُشغِّل CLI مستقل للاختبار السريع).

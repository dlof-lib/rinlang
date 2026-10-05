# تعديلات على محرّك RinLang (rin_interpreter.cpp / rin_parser.cpp / rin_parser.h / rin_ast.h)

تم بناء واختبار كل ما هنا فعلياً محلياً (g++ -std=c++17) عبر `tools/rin_run.cpp` (مُشغِّل
CLI مستقل)، وأُعيد بناء `tools/test_containers.cpp` بعد كل جولة تعديلات وقورن ناتجه
حرفاً بحرف بالناتج قبل أي تعديل — **متطابق تماماً** في كل مرة (بلا أي تراجع في
container/pipeline/namespace natives).

---

## Rin 1.0 — محرّك نماذج الإدخال (مفسِّر + مترجم + منفِّذ في ملف مخصّص)

ملفات جديدة: `rin_input.h` و`rin_input.cpp` — يُضمَّن الثاني تلقائياً في نهاية `rin_interpreter.cpp` (كبقية ملفات natives/OOP)، فلا تغيير في أي CMake/Gradle/CI.
**لا دوال ولا مفاهيم جديدة في اللغة**: الدوال العامة تبقى `input`/`inputNumber`/`confirm`/`choose`، والميزات كلها في وضع النموذج `input(prompt, target[, schema])`.

- **جديد (معاملة):** النموذج على حاوية/كائن/قاموس صار atomic — الإلغاء أو الفشل أو خطأ مُدقِّق يعيد الهدف كما كان (عبر `setState`/`oop.set`، فيرى المراقبون التراجع، ولا يُلمَس حقل لم يُكتب).
- **جديد (تداخل):** مواصفة الحقل في المخطّط يمكن أن تكون كائناً (تركيب OOP، يُملأ في مكانه) أو اسم حاوية (تُملأ ثم تُسلَّم لقطتها)؛ حدّ 6 مستويات وكشف الدورات.
- **جديد (مُدقِّق النموذج):** `input(prompt, validator, target[, schema])` — المُدقِّق يستلم الناتج النهائي (لقطة/كائن/قاموس)؛ نص = سبب الرفض وإعادة السؤال حتى 5 جولات. (كان المُدقِّق مع هدف يُتجاهَل صامتاً.)
- **جديد (فحص مسبق):** حاوية مقفلة (`container.isLocked`) أو كائن مجمَّد (حتى المتداخل) أو دورة تُرفض قبل أول سؤال.
- **المعمارية:** `FormInterpreter` (وسائط ← خطة) ← `FormCompiler` (خطة ← `Program` تعليمات مسطّحة + `disassemble()`) ← `FormExecutor` (تنفيذ + دفتر معاملة)، عبر `Host` رفيع فتُختبر الأصناف بلا Interpreter.
- **تعديل صغير في الملفات الموجودة:** `rin_interpreter.h` (تصريح `registerNativesInput` + علم `inputAtEof_`) و`rin_interpreter.cpp` (استدعاء التسجيل + `include` + ضبط العلم في `askRaw`).
- **التوافق:** نداءات `input` التي ليست نماذج تذهب للتنفيذ القديم حرفياً؛ اختبارات `input_*` الستة القديمة تمرّ بالناتج نفسه حرفياً، ومقارنة قبل/بعد على كل `tests/verification` و`tests/rintests.rin` و`examples` متطابقة (الفرق الوحيد توقيت التنفيذ).
- **اختبارات:** `tests/verification/input_engine_{atomic,nested,check}` (.rin/.stdin/.expected) و`tests/tools/test_input_engine.cpp` (وحدة C++ للمترجم والمنفِّذ). التفاصيل: `docs/input.md` §5.

---

## Rin 1.0 — مساعد المكتبات wesscode (فوق تحديثات OOP والكونسول وindsin)

- **جديد:** `lib/wesscode.og.rin` (مضمَّنة في `rin_stdlib_libs.h`): مساعد لإنشاء المكتبات والحزم — بناء مواصفة، فحص بالمحلّل الحقيقي، قوالب جاهزة، كتابة حزمة كاملة، وتوليد جسر C++.
- **جديد:** `lang.check(src)` و`lang.split(src)` و`lang.isBuiltin(name)` في `rin_extra_natives4.cpp`.
- **جديد (wesscode):** تحويلتان: **جزء** `wc_toParts` (ملف رئيسي + `parts/*.rin`) و**قسم** `wc_toSections` (أقسام `#region` وفهرس)، مع العكس `wc_mergeParts`/`wc_fromSections`، واستراتيجيات `kind|prefix|size|map`، و`wc_layout` لتخطيط مكتبة كاملة، و`wc_suggest` و`wc_convertFile` و`wc_fromFile`.
- **جديد:** `tests/wesscode_tests.rin` (42 اختباراً). التفاصيل: `docs/packages-and-interop.md`.
- الإصدار يبقى `1.0.0`؛ وُحِّدت كل تسميات "Rin 1.x" القديمة إلى "Rin 1.0".

---

## Rin 1.0 — المكتبات والحزم وجسر C++ وJSON وrintest (فوق تحديث OOP)

- **جديد:** `rin_extra_natives4.cpp`: `json.*` (مسارات/diff/patch/validate/canonical)، `semver.*`، `pkg.*` (rin.toml، ترتيب التبعيات، scaffold، api، checksum)، `cpp.*` (جسر C++ مقفل افتراضياً + `rin_abi.h`).
- **جديد:** `lib/rintest.og.rin` و`lib/packkit.og.rin` (مضمَّنتان في `rin_stdlib_libs.h`، تعملان بلا ملفات على القرص).
- **جديد:** `rin --allow-native` (linux/macos) و`${CMAKE_DL_LIBS}` في CMake. اختبار شامل: `tests/rintests.rin` (54 اختباراً، 58 مع `--allow-native`). التفاصيل: `docs/packages-and-interop.md`.
- لا تغيير في أي سلوك سابق؛ اختبارات OOP وextra_natives 1/2 تمر كما كانت.

---

## Rin 1.0 — OOP الموسَّع

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
- **جديد (الربط بالحاويات وIndsin، `rin_oop_link.cpp`):** `oop.bindContainer/bindContainerFrom/bindContainerTwoWay`، `oop.watchContainer`،
  `oop.toContainer/fromContainer`، `oop.bindWarp/bindWarpFrom/bindWarpTwoWay`، `oop.bindView`، `oop.syncLinks` — حقل كائن مربوط
  بحقل حاوية (يُطلق `on update`) أو بخلية Warp في Indsin (تُنقل من/إلى المعالجات عند كل tap). منع صدى بين الاتجاهين،
  خيارات `transform`/`init`. خطّافات صغيرة في `assignStateAware` و`setField` و`callTopLevelFunction` بكلفة صفر ما لم يُنشأ رابط.
  اختبارات: `tests/verification/oop_links.rin` و`tests/tools/test_indsin_oop_bind.cpp` (جلسة Indsin حقيقية). التفاصيل: `docs/oop.md` §12.

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

## Condition variables — expanded key conditions + helpers

- **Key conditions** (`=term=(...)`) grew from `=if=` / `=unless=` / `=when=` to: `=ifnot=`, `=elif=` / `=elseif=` chains,
  `=while=` / `=until=` loops, multi-condition `=all=` / `=any=` / `=none=`, and value tests `=nil=` / `=notnil=` /
  `=empty=` / `=present=`. All are parser sugar over the existing `IfStmt` / `WhileStmt` (`rin_parser.cpp`,
  `Parser::keyConditionStatement`). A stray `=elif=` now reports a dedicated error.
- **New helpers** in `app/src/main/cpp/rin_conditions.cpp` (included from `rin_interpreter.cpp`): `notNil`, `present`, `blank`,
  `isTrue`, `isFalse`, `xor`, `implies`, `oneOf`, `noneOf`, `exactly`, `atLeast`, `atMost`, `lengthIs`, `outside`,
  `isInt`, `isZero`, `closeTo`, `matches`, `key_conditions`. `empty` now also understands `Set`.
- Names already defined as functions in `lib/*.og.rin` (`isEven`, `inRange`, `startsWith`, ...) were deliberately not added natively.
- Docs: `docs/key_terms_and_builtins.md`, `docs/control-flow.md`. Editor: `RinSyntax.kt`, `syntaxes/*.json`.
- Test: `tests/verification/key_conditions_extended.rin` (+ `.expected`).

### Conditions used inside other concepts
- Trailing guards `when (c)` / `unless (c)` / `if (c)` on `return`, `break`, `continue`, `achieve`, `throw` (`Parser::trailingGuard`).
- `match` case guards: `case A, B when (cond) { }` (`MatchCase::guard`, evaluated in `rin_interpreter.cpp`).
- Subject-less `match { case (cond) { } ... else { } }` desugared to an if / else-if chain (`Parser::conditionMatchStatement`).
- Test: `tests/verification/condition_guards.rin` (+ `.expected`). Docs: "Using conditions inside other concepts".
- Predicate-based conditions: `every`, `some`, `countIf`, `findIf` (take a `fun` value; arrays and sets).

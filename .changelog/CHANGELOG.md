# سجل التغييرات — Rin

> مجمَّع آلياً من ملفات `CHANGES_*.md` المتفرّقة (انظر `cleanup-project.yml`). أضف الجديد هنا مباشرةً.

## الفهرس

- [GENERAL](#general)
- [ANDROID16](#android16)
- [BASE ACCEPT](#base-accept)
- [CHANNEL47](#channel47)
- [COMPARE](#compare)
- [CONSOLE](#console)
- [CONTAINER STYLE](#container-style)
- [CONTAINER TX](#container-tx)
- [CPP23](#cpp23)
- [EDITOR](#editor)
- [EDITOR PRO](#editor-pro)
- [ENV](#env)
- [HASH](#hash)
- [HTML](#html)
- [IMAGE DRAW](#image-draw)
- [IMAGE TERMINAL](#image-terminal)
- [INDSINWEB](#indsinweb)
- [INDSIN ICONS](#indsin-icons)
- [INDSIN MEDIA](#indsin-media)
- [INDSIN OBJECT](#indsin-object)
- [INDSIN RIN](#indsin-rin)
- [INT128](#int128)
- [INTELLISENSE](#intellisense)
- [LET](#let)
- [LINALG ALGO](#linalg-algo)
- [LIVING](#living)
- [LOOPS](#loops)
- [LOOP I](#loop-i)
- [MAIN](#main)
- [MAX MIN](#max-min)
- [NUMBER](#number)
- [PACKS](#packs)
- [PERMISSIONS FIX](#permissions-fix)
- [PREVIEW](#preview)
- [PRE POST](#pre-post)
- [PRINT IMAGE](#print-image)
- [PRINT TEMPLATES](#print-templates)
- [RIN](#rin)
- [RINPAK](#rinpak)
- [RIN AVA](#rin-ava)
- [RUN INDICATOR](#run-indicator)
- [SCOPE GUARANTEES](#scope-guarantees)
- [SET](#set)
- [STATS PLOT](#stats-plot)
- [STYLE](#style)
- [TABLE](#table)
- [TEMPLATE](#template)
- [TERMINAL](#terminal)
- [TERMINAL ANDROID](#terminal-android)
- [TERMINAL BRAND](#terminal-brand)
- [TERMINAL CLI](#terminal-cli)
- [TERMINAL IMG](#terminal-img)
- [TERMINAL IMGDEC](#terminal-imgdec)
- [TERMINAL OCR](#terminal-ocr)
- [TERMINAL PROGRESS](#terminal-progress)
- [TERMINAL UNICODE](#terminal-unicode)
- [TERMINAL XCMDS](#terminal-xcmds)
- [TESSERACT](#tesseract)
- [TIME](#time)
- [TOKEN](#token)
- [VM](#vm)
- [WEB JS LOCALHOST](#web-js-localhost)
- [BUILD REPAIR](#build-repair)
- [MANIFEST CHANGES](#manifest-changes)

---

## looptwist — حلقات Rin القوية فوق RINTWIST

- مكتبة `lib/looptwist.og.rin` (Disconom 2 بصيغة TOML، 7 أجزاء): ~70 دالة — مصادر (`range/chunk/window/zip/product/group/partition/sorted…`)، حلقات بسقف أمان و`LT_STOP`، `retry` بتراجع أُسّي، `poll`، `budget`، `timed`، شريط تقدّم مع ETA، واختصارات `LT.*`.
- الإعدادات الافتراضية من RINTWIST: `limit` حسب `app.mode`، `trace` حسب `app.debug`، الوسم من `file.stem`/`lib.name` وقت الاستدعاء، الساعة من `run.uptimeMs`، `lt_where/lt_logName` عبر `rintwist.fmt`. التفاصيل: `docs/looptwist.md`. الاختبار: `tests/verification/looptwist_basic.rin`.

## Disconom 2 + مترجم Disconom

- **الصيغة 2**: `optionalDependencies` / `peerDependencies` / `devDependencies` / `features` (+`RIN_FEATURES`) / `scripts` / `files` / `workspaces` / `private` / `bugs` / `funding` / `readme` / `categories` / `platforms` / `contributors`، مع تحقق موسَّع (دورات الميزات، مسارات آمنة، SPDX، روابط…). صيغة 1 تعمل بلا تغيير.
- **`disconom.toml`**: صيغة ثالثة متكافئة (محلّل TOML مدمج في `rin_libdir.h`)؛ `rin lib new --toml`.
- **المترجم** `rin_disconom.h`: `rin lib translate` بين json/xml/toml + استيراد/تصدير `package.json` و`rin.toml` (RinPM)، و`rin lib fmt [--check]` لإعادة الكتابة القانونية.
- **`disconom.lock`**: `rin lib lock [--check]` — إصدارات محلولة + بصمات sha256 (حتمي).
- **السكربتات**: `rin lib scripts|script --allow-scripts`.
- مخطط JSON: `schemas/disconom.schema.json`. اختبارات: `tests/verification/disconom/run_tests.sh` (61 فحصاً) و`tests/verification/disconom_pro.rin`.

## مكتبات مقسَّمة (Split Libraries)

- مكتبة = مجلد `<name>.og.rin/` فيه `disconom.json|xml` + أجزاء `.zpp` (أي اسم/مجلد) + ملفات تشغيل `.rinz` + `native/*.cpp` اختياري؛ يستوردها `@import "name"` كأي مكتبة.
- جديد: `rin_libdir.h` (JSON/XML/semver/اكتشاف الأجزاء/مولِّد)، `rin_libdir.cpp` (التكامل + `rinlib.*` + أغلفة C++ التلقائية)، `rin lib new|check|info|list|run`، وتشغيل `.rinz` مباشرة. التفاصيل: `docs/split-libraries.md`.

## GENERAL

<sub>المصدر الأصلي: `CHANGES.md`</sub>

### تعديلات على محرّك RinLang (rin_interpreter.cpp / rin_parser.cpp / rin_parser.h / rin_ast.h)

تم بناء واختبار كل ما هنا فعلياً محلياً (g++ -std=c++17) عبر `tools/rin_run.cpp` (مُشغِّل
CLI مستقل)، وأُعيد بناء `tools/test_containers.cpp` بعد كل جولة تعديلات وقورن ناتجه
حرفاً بحرف بالناتج قبل أي تعديل — **متطابق تماماً** في كل مرة (بلا أي تراجع في
container/pipeline/namespace natives).

---

#### Rin 1.0 — محرّك نماذج الإدخال (مفسِّر + مترجم + منفِّذ في ملف مخصّص)

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

#### Rin 1.0 — مساعد المكتبات wesscode (فوق تحديثات OOP والكونسول وindsin)

- **جديد:** `lib/wesscode.og.rin` (مضمَّنة في `rin_stdlib_libs.h`): مساعد لإنشاء المكتبات والحزم — بناء مواصفة، فحص بالمحلّل الحقيقي، قوالب جاهزة، كتابة حزمة كاملة، وتوليد جسر C++.
- **جديد:** `lang.check(src)` و`lang.split(src)` و`lang.isBuiltin(name)` في `rin_extra_natives4.cpp`.
- **جديد (wesscode):** تحويلتان: **جزء** `wc_toParts` (ملف رئيسي + `parts/*.rin`) و**قسم** `wc_toSections` (أقسام `#region` وفهرس)، مع العكس `wc_mergeParts`/`wc_fromSections`، واستراتيجيات `kind|prefix|size|map`، و`wc_layout` لتخطيط مكتبة كاملة، و`wc_suggest` و`wc_convertFile` و`wc_fromFile`.
- **جديد:** `tests/wesscode_tests.rin` (42 اختباراً). التفاصيل: `docs/packages-and-interop.md`.
- الإصدار يبقى `1.0.0`؛ وُحِّدت كل تسميات "Rin 1.x" القديمة إلى "Rin 1.0".

---

#### Rin 1.0 — المكتبات والحزم وجسر C++ وJSON وrintest (فوق تحديث OOP)

- **جديد:** `rin_extra_natives4.cpp`: `json.*` (مسارات/diff/patch/validate/canonical)، `semver.*`، `pkg.*` (rin.toml، ترتيب التبعيات، scaffold، api، checksum)، `cpp.*` (جسر C++ مقفل افتراضياً + `rin_abi.h`).
- **جديد:** `lib/rintest.og.rin` و`lib/packkit.og.rin` (مضمَّنتان في `rin_stdlib_libs.h`، تعملان بلا ملفات على القرص).
- **جديد:** `rin --allow-native` (linux/macos) و`${CMAKE_DL_LIBS}` في CMake. اختبار شامل: `tests/rintests.rin` (54 اختباراً، 58 مع `--allow-native`). التفاصيل: `docs/packages-and-interop.md`.
- لا تغيير في أي سلوك سابق؛ اختبارات OOP وextra_natives 1/2 تمر كما كانت.

---

#### Rin 1.0 — OOP الموسَّع

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

#### الجولة 1 (سابقاً)

##### 1) إصلاح: `obj.method()` على متغيّر عادي كان يفشل دائماً
`let a = Animal(); a.speak();` كان يرمي "not a function" رغم أن class/inheritance/super
مُطبَّقة بالكامل، لأن `Parser::call()` يحوّل أي `IDENT.IDENT(...)` (جذره متغيّر بسيط) إلى
نداء namespace نصي (نفس آلية `make.qr()`) دون تفريق. أُصلح في
`Interpreter::invokeCallee` بفحص fallback: إن كان الجذر متغيّراً حقيقياً في `env` وقيمته
INSTANCE/MAP، يُنفَّذ نفس توزيع `MethodCallExpr`. نفس الإصلاح لـ `super.method()`.

##### 2) ميزة جديدة: تعبيرات لامبدا `fun(params) { body }`
لم يكن للغة أي صياغة لدالة مجهولة كتعبير (فقط `fun name(...) {}` كعبارة). أُضيفت عقدة
`LambdaExpr` (rin_ast.h) + حالة في `Parser::primary()` (تُفعَّل فقط في موضع تعبير، إضافية
بحتة) + حالة في `Interpreter::evaluate()` تبني `Callable` مربوطاً بـ closure حقيقية.

---

#### الجولة 2 (هذه الجولة)

##### 3) إصلاح: كل closures داخل حلقة `for` كانت تتشارك نفس المتغيّر (كل واحدة تُرجع القيمة
##### الأخيرة بدل قيمة تكرارتها)
```kotlin
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

##### 4) ميزة جديدة: استدعاء نتيجة تعبير عشوائي مباشرة (`arr[0]()`, `(fun(x){...})(1)`, `getFn()()`)
كان أي `(` بعد أي شيء غير اسم بسيط (IDENT) يرمي خطأ تحليل صريح ("only functions can be
called")، فلا يمكن استدعاء عنصر مصفوفة يحمل دالة مباشرة، أو استدعاء نتيجة دالة أخرى
مباشرة، أو استدعاء lambda فوراً (IIFE). أُضيفت عقدة `CallValueExpr` (rin_ast.h): بدل رمي
الخطأ، `Parser::call()` يبني الآن هذه العقدة (تُقيَّم أي تعبير callee إلى قيمة FUNCTION ثم
تُستدعى) — **إضافية بحتة تماماً**: كانت الحالة التي تعالجها خطأ تحليل صريح قبلاً، فلا يمكن
أن تُغيّر تحليل أي برنامج كان صالحاً سابقاً.

```kotlin
let fns = [fun(){return 1;}, fun(){return 2;}];
print fns[0]();              // 1
print (fun(x){return x*10;})(4);   // 40  (IIFE)
fun makeAdder(n) { return fun(x) { return x + n; }; }
print makeAdder(3)(4);       // 7  (نداء متسلسل)
```

##### 5) ميزة جديدة: حلقة `for (let NAME in iterable) { body }` (لم تكن موجودة إطلاقاً)
الوسيلة الوحيدة للتكرار على مصفوفة/قاموس كانت `for` على طراز C مع فهرس عددي يدوي
(`for (let i=0; i<len(arr); i=i+1) { arr[i] ... }`). عقدة `ForInStmt` جديدة (rin_ast.h) +
تمييز في `Parser::forStatement()` (نظرة 3 رموز للأمام: `let` + IDENT + IDENT("in")، فلا
لبس مع `for (let i = 0; ...)` العادية إطلاقاً؛ "in" كلمة سياقية غير محجوزة، تبقى تعمل
اسم متغيّر عادي بلا أي تغيير) + تنفيذ في `Interpreter::execute(ForInStmt)`. يدعم:
مصفوفة (كل عنصر)، قاموس (كل مفتاح، بنفس ترتيب `keys()`)، نص (كل محرف كنص بطول 1).
`break`/`continue` يعملان بداخلها بنفس الدلالة المعتادة، وكل تكرارة لها بيئة خاصة بها من
الصفر (نفس إصلاح #3 أعلاه)، فـ closures بداخلها تلتقط القيمة الصحيحة لكل تكرارة أيضاً.

```kotlin
for (let x in [10, 20, 30]) { print x; }             // 10 20 30
let m = {"a": 1, "b": 2};
for (let k in m) { print k + "=" + m[k]; }            // a=1  b=2
for (let c in "abc") { print c; }                     // a b c
```

---

#### ملاحظة حول `rinc.cpp`
لم يُعدَّل (لا `compiler/rinc.cpp` ولا `app/src/main/cpp/rinc.cpp`): كلاهما يُضمِّن ملفات
المفسّر الحقيقية وقت البناء عند تفعيل "وضع تضمين المفسّر"، فيستفيدان تلقائياً من كل
الإصلاحات/الميزات أعلاه بمجرد إعادة بنائهما من هذه النسخة المعدَّلة.

#### طريقة التطبيق
انسخ الملفات الأربعة إلى نفس المسارات في المستودع الأصلي (استبدال كامل):
- `app/src/main/cpp/rin_ast.h`
- `app/src/main/cpp/rin_parser.h`
- `app/src/main/cpp/rin_parser.cpp`
- `app/src/main/cpp/rin_interpreter.cpp`

ثم أعد البناء بأي من المسارات الموجودة أصلاً (Gradle/NDK، `scripts/build_all.sh desktop`،
أو `tools/rin_run.cpp` كمُشغِّل CLI مستقل للاختبار السريع).

#### Condition variables — expanded key conditions + helpers

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

##### Conditions used inside other concepts
- Trailing guards `when (c)` / `unless (c)` / `if (c)` on `return`, `break`, `continue`, `achieve`, `throw` (`Parser::trailingGuard`).
- `match` case guards: `case A, B when (cond) { }` (`MatchCase::guard`, evaluated in `rin_interpreter.cpp`).
- Subject-less `match { case (cond) { } ... else { } }` desugared to an if / else-if chain (`Parser::conditionMatchStatement`).
- Test: `tests/verification/condition_guards.rin` (+ `.expected`). Docs: "Using conditions inside other concepts".
- Predicate-based conditions: `every`, `some`, `countIf`, `findIf` (take a `fun` value; arrays and sets).

---

## ANDROID16

<sub>المصدر الأصلي: `CHANGES_ANDROID16.md`</sub>

### دعم Android 15 وما فوق في RinStudio

| البند | القيمة |
|---|---|
| الحد الأدنى (minSdk) | **Android 15 — API 35** |
| الهدف (targetSdk / compileSdk) | **Android 16 — API 36** |
| AGP / Gradle / Kotlin | 8.13.0 / 8.13 / 2.0.21 (دون تغيير) |
| NDK | r28 (`28.0.13004108`) — مكتبات native بمحاذاة صفحات 16KB |

#### ما تغيّر

##### البناء (`app/build.gradle` و`app/build.gradle.kts`)
- `minSdk` = 35، `compileSdk`/`targetSdk` = 36. (لا حاجة لـ desugaring بعد الآن.)
- NDK من r26 إلى r28 + `-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON` (أجهزة 16KB، ومتطلب Google Play).
- `useLegacyPackaging = false` للمكتبات الأصلية.
- رفع `core-ktx 1.15.0` و`appcompat 1.7.1` و`activity-ktx 1.10.1`.
- سير عمل GitHub: NDK r28 + `platforms;android-36` + `build-tools;36.0.0`.

##### Android 15/16
- **Edge-to-edge إجباري**: كلاس `RinSystemBars` (يُسجَّل من `RinApplication`) يطبّق حشوات أشرطة النظام
  وقطع الشاشة ولوحة المفاتيح على كل شاشة، ويرسم ألوان الثيم خلف الشريطين.
- **الرجوع التنبّؤي**: `enableOnBackInvokedCallback="true"`، ونقل `FilesActivity` و`IndsinPreviewActivity`
  إلى `OnBackPressedCallback` (Android 16 لا يستدعي `onBackPressed()`).
- `startActivityForResult` → Activity Result API في `RinStoreActivity`.
- `systemUiVisibility` → `WindowInsetsControllerCompat` (ملء الشاشة لفيديو التوثيق).
- قواعد نسخ احتياطي (`data_extraction_rules.xml`, `backup_rules.xml`).
- تثبيت APK: إن لم يُمنح إذن «مصادر غير معروفة» تُفتح شاشة الإذن مباشرة.
- تصدير APK: الخيارات API 35 و36 فقط (الحد الأدنى والهدف الافتراضي للتطبيقات المُصدَّرة 35/36)،
  لأن الحزمة المضيفة نفسها تتطلب 35.
- `ApkV1Signer`: `android.util.Base64` بدل `java.util.Base64`.

#### حزمة لكل إصدار (Android 15 / 16 / 17)
- `build.gradle*`: خصائص `-Prin.targetSdk` (35–37) و`-Prin.compileSdk` و`-Prin.versionSuffix`.
- `.github/workflows/build-android-versions.yml`: مصفوفة تبني APK موقَّعاً لكل إصدار، تتحقق من
  minSdk/targetSdk/محاذاة 16KB، وتنشر Release. التفاصيل في `docs/ANDROID_VERSIONS.md`.

#### لم يُتحقَّق منه بعد
البيئة بلا Android SDK، فلم يُشغَّل `./gradlew assembleDebug`. جرّب على جهاز/محاكي Android 15 و16.

---

## BASE ACCEPT

<sub>المصدر الأصلي: `CHANGES_BASE_ACCEPT.md`</sub>

### CHANGES_BASE_ACCEPT — `base` و`accept`: متغيّران يخدمان `if`

إضافة اختيارية بالكامل (additive) من عائلة المتغيرات الحيّة: كلمات سياقية غير محجوزة، فبرنامج قديم يعمل كما كان حرفيًا
(`let base = 5;` و`fun inc(x){...}` و`let accept = 1;` ما زالت صالحة). الشرح: `docs/base-accept.md`.

- `base x = v [to GOAL] [step N];` — خط أساس: `x.base` `x.same` `x.changed` `x.up` `x.down` `x.delta` `x.goal` `x.reached` `x.togo` `x.progress` `x.step`.
- `accept x = v if RULE [else FALLBACK] [strict];` — حارس قبول: `x.ok` `x.accepted` `x.rejected` `x.refused`. الاسم داخل الشرط = القيمة المرشّحة.
- أفعال: `inc x [by n];` `dec x [by n];` (على أي متغيّر رقمي وتمرّ من حرّاسه) و`rebase x;` `rewind x;` (لـ base).
- أكواد جديدة: `E0049` (رفض accept) و`E0050` (استعمال خاطئ لـ base).

#### الملفات (app/src/main/cpp)
`diagnostics/diagnostic.h|.cpp` (E0049/E0050) · `rin_ast.h` (LiveKind::Base/Accept + حقول LiveDeclStmt) ·
`rin_parser.cpp` (التوزيع في declaration() + مُعدِّلات to/step/if/else/strict + inc/dec/rebase/rewind) ·
`rin_interpreter.h|.cpp` (حقول LiveMeta، liveCheckAccept، حارس في liveAssign، أفعال، خصائص).
لا ملفات جديدة في المحرّك (لا تعديل على CMake/Gradle).

#### أدوات وتوثيق
`syntaxes/rin-keywords.json` و`syntaxes/rin.tmLanguage.json` (+ نسخة `editors/vscode-indsin`) و`RinSyntax.kt` و`RinSyntaxHighlighter.kt` (تلوين) ·
`docs/base-accept.md` (جديد) + روابط في `variables.md` `language-reference.md` `living-variables.md` `ERROR_SYSTEM.md` ·
`examples/base_accept_demo.rin` · `tests/verification/base_basic|accept_basic|base_accept_if`.

#### التحقق
`tests/rintests.rin` 83/83 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 42/57 (الـ15 الفاشلة نفسها قبل التعديل وبعده بالضبط:
`input_*` و`let_err_*`، اسم الملف في موقع الخطأ بأداة `rin_run`)، والاختبارات الثلاثة الجديدة تنجح.
بُني على سطح المكتب (g++). لم يُبنَ APK. `rinc` (Rin → C) لا يدعم الصيغ الجديدة بعد.

---

## CHANNEL47

<sub>المصدر الأصلي: `CHANGES_CHANNEL47.md`</sub>

### CHANGES_CHANNEL47 — لغة channel47 (`.ch47`) لوصف بيانات وإعدادات المكتبات والحزم والأدوات

صيغة نصّية صارمة تمثّل النصوص والأرقام والمنطقي والتواريخ والمصفوفات والجداول. المواصفة: [`docs/channel47.md`](docs/channel47.md).

#### الملفات (كلها جديدة، لا تعديل على ملفات موجودة)
| الملف | الدور |
|---|---|
| `docs/channel47.md` | المواصفة الكاملة: الأنواع، الجداول، رموز الأخطاء `C47xxx`، EBNF |
| `tools/channel47.py` | المرجع (بايثون بلا اعتماديات): `check fmt json from-json get merge` |
| `lib/channel47.og.rin` | قارئ داخل Rin: `c47_parse c47_try c47_get c47_has c47_merge c47_check` |
| `tests/channel47_test.py` · `tests/channel47_rin_test.rin` | 20 اختباراً بايثون · 57 فحصاً Rin |
| `examples/channel47/*.ch47` | بيان مكتبة، حزمة، أداة، وإعدادات تطبيق |
| `schemas/channel47-manifest.schema.json` | مخطط JSON لتصدير البيان |
| `assets/branding/channel47/` · `docs/icons/channel47{,-file}.svg` | أيقونتا التطبيق والملف: SVG + PNG 16…512 + ICO |
| `tools/gen_channel47_icon.py` | مولّد الأيقونة |

#### التحقق
- `python3 tests/channel47_test.py` ← 20/20.
- `rin_run tests/channel47_rin_test.rin` (من الجذر) ← 57/57، بُني `rin_run` على سطح المكتب (g++) من مصادر المشروع.
- مرجع بايثون وقارئ Rin يعطيان الرمز نفسه لكل خطأ في الاختبار (17 رمزاً).

#### ما لم يُنجز
- لم تُربط الصيغة بـ RinPM/RinPak بعد (يلزم قرار: هل يصير `.ch47` بياناً بديلاً لـ `project.og.urin`؟).
- ربط الامتداد `.ch47` بالنظام (أيقونة ملف/تطبيق) وإضافة مكتبة `channel47` إلى `tools/embed_libs.py` لم يُجريا.
- كتابة (dump) channel47 من Rin غير موجودة؛ تتم عبر أداة بايثون.
- التعليقات لا تُحفظ عند `fmt`.

---

## COMPARE

<sub>المصدر الأصلي: `CHANGES_COMPARE.md`</sub>

### CHANGES_COMPARE — معاملات `/=` `=/=` `=/`
- `rin_common.h`: توكنان جديدان `NOT_SAME` (`=/=`) و`COPY_OF` (`=/`).
- `rin_lexer.cpp`: `/=` يُحوَّل إلى `BANG_EQUAL`؛ `=/=` و`=/`؛ مع حماية `=//`.
- `rin_parser.cpp`: يُضافان إلى `equality()`.
- `rin_interpreter.cpp`: `sameReference()` + تقييم المعاملين + أسماء التوكن.
- لم يُعدَّل `rinc.cpp` (المترجم) ولا ملفات Kotlin (لا تلوين بعد).
- التحقق: rintests 54/54 · wesscode_tests 42/42 · verification 50/51 (الفرق الوحيد `input_forms` القديم).

---

## CONSOLE

<sub>المصدر الأصلي: `CHANGES_CONSOLE.md`</sub>

### تطوير مخرجات كونسول Rin

#### ملفات جديدة (app/src/main/java/com/dlof/rinlang)
- `RinDiagnosticRenderer.kt` — تقرير تشخيص بأسلوب rustc: رأس `error[E0001]`، موقع `--> file:line:col`، مقطع المصدر مع أسهم `^^^` تحت الموضع الدقيق، ثم reason/expected/found/note/help/suggestion/caused by. أعمدة 1-indexed ونهاية حصرية مطابقة لـ `SourceLocation` في المحرّك. Tab يُوسَّع إلى 4 مسافات، والسهم مقيَّد بنهاية السطر، والمدى متعدد الأسطر يعطي سهماً واحداً.
- `RinOutputReport.kt` — `RinOutputSummary` (عدّادات + عنوان عربي)، `LogLevel`/`atLeast()` للتصفية، `collapseRepeats()` (×N)، `truncateMiddle()` للمخرجات الضخمة، و`RinConsoleExport` (نص عادي بأعمدة / Markdown بسياج يتكيف مع الـ backticks).

#### تعديل (RinConsoleFormatter.kt) — إضافي بالكامل، لا يكسر أي مستهلك
- `RinLogLine` يكتسب `errorLine` و`indent` و`repeat` بقيم افتراضية؛ `text` لم يتغير.
- استخراج رقم السطر من `[Error line N]:` لدعم «اذهب إلى السطر».
- حفظ إزاحة السطر الأصلية (Tab = 4).
- `formatBytes` بـ `Locale.ROOT` (كان يُنتج أرقاماً هندية/فاصلة «٫» على أجهزة عربية) ولا يطبع أحجاماً سالبة.

#### اختبارات (app/src/test)
`RinDiagnosticRendererTest` و`RinConsoleOutputTest` (JUnit، بلا JNI).
يلزم: `testImplementation("junit:junit:4.13.2")` و`testImplementation("org.json:json:20240303")`.

#### مخرجات الـ container المميَّزة
- `RinContainerOutput.kt` (جديد): `ContainerFamily` (17 عائلة: pipe/data/api/import/table/doc/object/portal/block/sticker/chatbot/make/sql + Group/Volume/Section + عامة) بأيقونة وتسمية عربية ولون مميّز لكل عائلة؛ `RinContainerParser` يتعرّف على أسطر `<icon> container.X = name` و`✅|◽ .end/X (name) [تحتوي: ...]` بنفس صيغ المفسِّر الحرفية؛ `RinContainerTree` يبني شجرة متداخلة (مطابقة بالوسم والاسم، وحاوية لم تُغلق بسبب خطأ تبقى ظاهرة بتحذير) و`toTreeText()` لنسخة نصية شجرية.
- `RinLogLine.container` (حقل إضافي): يحمل علامة الفتح/الإغلاق.
- أسطر فتح الحاويات تُصنَّف الآن `STRUCTURE` دائماً (كان `container.doc` يُصنَّف DOC_INSERT و`container` العامة IMPORT، فيظهر حدثها في تبويب Events بنوع خاطئ).
- `RinJobAdapter`: كل حاوية تُعرض ككتلة بإطار وخلفية بلون عائلتها، رأس فيه الأيقونة والاسم وعدّاد الأسطر، سطر «تحتوي: ...»، ومحتوى متداخل قابل للطيّ بالنقر (الكبيرة > 40 سطراً تبدأ مطويّة). أثناء البحث يعود العرض مسطّحاً.
- ملخّص التشغيل صار يذكر الحاويات: «اكتمل التشغيل بنجاح — 12 سطراً في 3 حاويات».
- `collapseRepeats` لا يدمج أسطر الحاويات أبداً (فتح/إغلاق متكرر داخل حلقة يبقى كتلاً منفصلة).

#### ربط الواجهة (RinJobAdapter.kt) — منفَّذ
- تبويب Output: عنوان ملخّص ملوَّن للتشغيل المنتهي («اكتمل التشغيل بنجاح — 12 سطراً» / «فشل التشغيل — خطأ واحد (السطر 4)»)، ودمج الأسطر المكرّرة مع شارة `×N`، ومسافة بادئة للأسطر المزاحة.
- تبويب Diagnostics: تقرير rustc الكامل (مقطع المصدر + الأسهم) بدل سطرين نصيين.
- نافذة Details: مقطع المصدر مع الأسهم، وتبقى حقول reason/expected/hints بتسمياتها المترجمة كما كانت.
- نسخ Markdown: يضيف التقرير الكامل داخل سياج كود يتكيّف مع الـ backticks.
- حماية: مقطع المصدر يُعرض فقط لأخطاء البرنامج الرئيسي (`file == "<input>"`)؛ أخطاء الملفات المستوردة لا تُقرن بمصدر خاطئ.

#### تنبيه
لم تُترجَم الشيفرة ولم تُشغَّل الاختبارات في بيئة الإنشاء (لا مترجم Kotlin هناك). شغّل `./gradlew testDebugUnitTest` ثم ابنِ التطبيق قبل الاعتماد عليها.

---

## CONTAINER STYLE

<sub>المصدر الأصلي: `CHANGES_CONTAINER_STYLE.md`</sub>

### CHANGES_CONTAINER_STYLE — ستايل كتل الحاويات في المحرر (نسخة هادئة)

فلسفة التصميم: لغة بصرية واحدة بلا زخارف — خط جانبي + لون للوسم. بلا كبسولات ولا تعبئة خلفية.

كل كتلة `@container.*` / `Containers.Group` / `Volume` / `Section` / `Translations` / `make`:

1. **خط إرشاد رأسي رفيع** (≈1.3dp، أطرافه مستديرة) بلون عائلة الحاوية، بزاوية ┌ عند الفتح و└ عند الإغلاق.
2. **سطر الفتح**: الوسم `@container.pipe` بلون العائلة؛ كلمة `container` **مزخرفة بتدرّج لوني** (لون العائلة ← درجة مزاحة الـHue ≈38°) بوزن ExtraBold، والـ`@` ExtraBold، ولاحقة النوع `.pipe` Medium؛ وتحته خط شعري يتلاشى تدريجياً؛ `=` باهت؛ اسم الحاوية SemiBold بلون نص المحرر.
3. **سطر الإغلاق**: `.end/` باهت وكلمة `container` بنفس التدرّج (Bold)، واللاحقة Medium. التدرّج Shader على الـPaint فقط، فلا يتغيّر عرض الخلايا.
4. **التعشيش**: لكل مستوى خطه عند إزاحته. **الطيّ**: ينتهي الخط عند سطر الكتلة المطويّة. **كتلة غير مغلقة**: خط أخفّ حتى النهاية وبلا زاوية سفلية.
5. `.object("..")` يكتفي بالخط الجانبي. الأسطر المحدَّدة تُرسم عادية حتى لا يُمحى لون التحديد.

العائلات: CORE بنفسجي · PIPE تيل · DATA أزرق · API أخضر · IO برتقالي · GROUP ذهبي (فاتح/داكن في colors.xml). `@view/@element/@loop/@theme` خارج الستايل عمداً.

#### الملفات
- `RinContainerTags.kt`: `ContainerFamily` / `ContainerBlock` / `buildContainerBlocks` (مختبَر بـ kotlinc).
- `RinCodeEditorView.kt`: `recomputeContainerBlocks`، `drawContainerBands` (الخط، قبل النص)، `drawContainerBadge` و`drawContainerClosing` (بعد النص، تعيد رسم الكلمات بنفس عرض الخلايا فلا ينزاح المؤشر).
- `AppSettings.kt` / `SettingsActivity.kt`: إعداد `container_block_style` (مفعّل افتراضياً).
- `colors.xml` (×2) و`strings_settings.xml` (ar/en/es + content-packs en/es).

> رسم `RinCodeEditorView` لم يُبنَ هنا (لا Android SDK) — ابنِه بـ Gradle وراجعه على الجهاز.

---

## CONTAINER TX

<sub>المصدر الأصلي: `CHANGES_CONTAINER_TX.md`</sub>

### CHANGES_CONTAINER_TX — container.transaction
دالة جديدة `container.transaction(name | [names], fn)`: تعديل ذرّي على حاوية أو عدة حاويات؛ لقطة عميقة قبل التنفيذ، وعند أي خطأ في `fn`
يُستعاد كل شيء حرفياً (تعديل/إضافة/حذف) ويُعاد رمي الخطأ. حاوية غائبة = خطأ قبل التنفيذ. تبقى الدوال (`fun`) داخل الحاوية دون مساس.
الملفات: `app/src/main/cpp/rin_extra_natives2.cpp` (بعد `container.restore`) · `docs/container_pro.md` · `tests/verification/container_transaction.rin|.expected`.
التحقق (g++ لينكس): الاختبار الجديد على `rin run` و`rin --vm` · container_extra/pro/advanced · rintests 83/83 · wesscode 42/42. لم يُبنَ APK/الويب.

#### container.attempt + مثال عملي
`container.attempt(name | [names], fn)` -> `{ok, value, error}` بلا رمي (يغلّف transaction: نجاح => value، فشل => تراجع كامل + error{message,line[,value][,code]}).
مثال: `examples/container_transactions.rin` (تحويل مالي بتراجع يشمل دفتر القيود). اختبار موسَّع في container_transaction.rin. الملف: rin_extra_natives2.cpp.

---

## CPP23

<sub>المصدر الأصلي: `CHANGES_CPP23.md`</sub>

### الترقية إلى C++23 وتسريع المفسِّر

#### المعيار
- `CMAKE_CXX_STANDARD 23` في `app/src/main/cpp` و`cli/{linux,macos,windows}` و`bindings`، و`-std=c++23` في
  `app/build.gradle` و`app/build.gradle.kts` (NDK 28 / clang 19 يدعمانه). رُفع `cmake_minimum_required` إلى 3.20.
- بناء الويب (Emscripten في `pages.yml`) تُرك على C++17 عمداً لأن إصدار emsdk غير مثبَّت.

#### التحسينات (نفس السلوك المرصود، أسرع)
1. **مسار عددي سريع** (`fastNumeric` في `rin_interpreter.cpp`): التعابير العددية الصرفة
   (`+ - * / %` والمقارنات على أرقام/متغيّرات عددية) تُقيَّم بلا بناء `Value` (~150 بايت). أي حالة أخرى
   (نص، كائن، قسمة على صفر، متغيّر حيّ) تعود إلى المسار الأصلي فتبقى الأخطاء والدلالات كما هي.
2. **تمرير البيئة بمرجع**: `evaluate/evaluateTree/exec/execBlockFlow` تأخذ `const EnvPtr&` بدل نسخة
   `shared_ptr` (زيادة/إنقاص عدّاد ذرّي في كل تعبير).
3. **`Environment`**: بحث بلا نسخ (`lookup`) وتكراري، تجزئة شفّافة (`StringHash` + `std::equal_to<>`)،
   `define(Value&&)`، وحجز مسبق لجدول معاملات الدالة.
4. قراءة المتغيّر تنسخ القيمة مرة واحدة بدل مرتين.

#### القياس (g++ 13 -O2، أفضل من 3 تشغيلات)
| الاختبار | قبل (C++17) | بعد (C++23) |
|---|---|---|
| fib(25) | 0.22 ث | 0.14 ث |
| حلقة 300 ألف عملية | 0.31 ث | 0.14 ث |
| str_cat | 0.04 ث | 0.03 ث |
| map_big | 0.06 ث | 0.04 ث |

مقارنة تفاضلية على 211 ملف `.rin` في `tests/` و`examples/`: الناتج مطابق (الفروق الوحيدة توقيت/مفاتيح عشوائية).
`rintests` 83/83 و`wesscode_tests` 42/42.

---

## EDITOR

<sub>المصدر الأصلي: `CHANGES_EDITOR.md`</sub>

### CHANGES_EDITOR — إضافة Set و Env والمعاملات الجديدة إلى المحرر
- `RinSyntax.kt` (RinLexer): `Set` و`Env` تُلوَّن كنوع؛ المعاملات `=/=` `=/` `/=` و`== != <= >=` تُلوَّن OPERATOR (`=//` و`=/*` تبقى تعليقاً).
- `RinSyntaxHighlighter.kt`: `Set` و`Env` ضمن كلمات الحاويات/الأنواع.
- `rin_editor_engine.cpp`: `Set` و`Env` في اقتراحات الإكمال التلقائي.
- `syntaxes/rin.tmLanguage.json`: المعاملات + قاعدة `std-namespaces` (Set/Env وأعضاؤهما).
- `syntaxes/rin-keywords.json`: أسماء `Set.*` و`Env.*` في natives.
- لم يُبنَ Kotlin هنا (لا kotlinc)؛ تحقّقت من ملف C++ فقط.

---

## EDITOR PRO

<sub>المصدر الأصلي: `CHANGES_EDITOR_PRO.md`</sub>

### CHANGES_EDITOR_PRO — تطوير المحرر (دعم الحلقات + أدوات احترافية)
- `cpp/editor/rin_editor_tools.h` (جديد، header-only): تلوين سياقي لكلمات Loops v2/v3
  (`each repeat loop retry where reversed yield limit skip take sorted ... nobreak`، تسميات الحلقات،
  `break/continue name`، عوامل الاختزال `sum each ...`)؛ نطاقات الطيّ؛ هيكل الملف؛ إعادة محاذاة المستند؛
  قوالب (snippets) للحلقات و`fun/if/while/for`.
  التلوين يتبع منطق المحلل: `let loop = 1;` و`repeat("ab", 2)` تبقى معرِّفات/استدعاءات عادية.
- `rin_editor_engine.h/.cpp`: `computeFoldRanges` `computeOutline` `reindentDocument` (أمر تراجع واحد، لا يمسّ
  النصوص متعددة الأسطر) `expandSnippetAtCursor`؛ والكلمات السياقية ضمن الإكمال التلقائي.
- `rin_editor_jni.cpp` + `RinNativeEditor.kt`: `getFoldRanges` `getOutline` `reindentDocument` `expandSnippetAtCursor`
  `isSnippetTrigger` `snippetTitle`.
- `RinCodeEditorView.kt`: اختيار اقتراح مثل `each` يوسّع قالباً كاملاً؛ دوال عامة `formatDocument()` `getOutline()` `getFoldRanges()`.
- `RinKeywordDocs.kt`: توثيق كلمات الحلقات للضغط الطويل.
- اختبار: `sh tests/editor/run.sh` (74/74 على g++ مع ملفات tests/loops).
- لم يُبنَ: Kotlin و JNI (لا kotlinc/NDK هنا) ولم يُربط أي زر واجهة بـ formatDocument/getOutline/الطيّ بعد.

#### ستايل الكود: مفاهيم دلالية + خط لكل مفهوم
- أنواع تلوين جديدة (20..26) في `rin_editor_engine.h` و`HighlightKind` (RinSyntax.kt): `Constant` (true/false/nil وثوابت `MAX_SIZE`)،
  `FunctionDef` (اسم الدالة بعد `fun`)، `Control` (if/else/while/for/return/break/continue/each/repeat/loop/retry/try/catch/throw/nobreak/`#do`/`#done`)،
  `Modifier` (where/yield/by/window/skip/take/sorted/distinct/... وعوامل `sum each`)، `Label` (تسمية الحلقة و`break outer`)،
  `Class` (كل `class X` واستخداماته + `Set`/`Env`)، `Property` (`obj.name`). `container`/`Section`/`Group`... تبقى `Type` (بنفسجي).
  المنطق في `classifyConcepts` (rin_editor_tools.h) فوق التصنيف السياقي للحلقات.
- `RinCodeEditorView.kt`: لون لكل مفهوم (ألوان فاتحة/داكنة في `colors.xml` و`values-night/colors.xml`) ونمط خط: **عريض** للكلمات المفتاحية/التحكّم/الأنواع/الأصناف/
  تعريف الدوال/الوسوم، *مائل* للتعليقات والمعدِّلات والتسميات. يُنفَّذ بـ `isFakeBoldText` + `textSkewX` (بلا خط بديل) فلا تتغيّر عروض الأحرف ويبقى المؤشر والتحديد مضبوطين.
- `AppSettings.kt`: مفتاح `syntax_font_styles` (افتراضياً مفعّل، بلا واجهة تبديل بعد).
- `syntaxes/rin.tmLanguage.json`: نفس المفاهيم لـ VS Code (`loops-and-concepts`).
- اختبار المحرر 90/90. لم يُبنَ Kotlin/XML/JNI هنا.

#### ستايل قوالب الحلقات `@tmpl` في المحرر
- ثلاثة أنواع تلوين جديدة (27..29) في `rin_editor_engine.h` و`HighlightKind` (RinSyntax.kt):
  `TemplateKw` (`@tmpl` و`@body`) · `Template` (اسم القالب في التعريف وكل استعمال `@name(` مع علامة `@`) · `Parameter` (معاملات القالب وأسماء `as` وكل ظهور لها داخل جسم القالب).
- `classifyTemplates` (rin_editor_tools.h) تُشغَّل بعد `classifyConcepts`: تقرأ التعريفات أولاً فتلوّن الاستعمال ولو سبق التعريف،
  و`let tmpl = 1;` يبقى معرِّفاً عادياً. محلل Kotlin الاحتياطي يلوّن `@tmpl/@body` واسم القالب و`@name(`.
- الخط (يتبع إعداد `syntax_font_styles`): `@tmpl/@body` **عريض** برتقالي محروق (داكن: برتقالي فاتح) · اسم القالب/الاستعمال **عريض مائل** نيلي (داكن: بنفسجي فاتح) · المعاملات *مائلة* بنّية.
- `colors.xml` و`values-night/colors.xml`: `syntax_template` `syntax_template_kw` `syntax_parameter`. `RinCodeEditorView.kt`: لون ونمط لكل نوع.
- اختبار المحرر 105/105 (g++). لم يُبنَ Kotlin/XML هنا.

#### خط مناسب لكل مفهوم (عائلة JetBrains Mono NL)
- تسعة ملفات `res/font/rin_code_*.ttf` (regular · medium · semibold · bold · extrabold · italic · medium_italic · bold_italic · light_italic)
  تحلّ محلّ `rin_tmpl_bold_italic.ttf` و`rin_tmpl_italic.ttf` (احذفهما). الرخصة OFL في `assets/licenses/JetBrainsMono-OFL.txt`.
  النسخة NL بلا ligatures، وعرض الحرف 0.6em في كل الأوزان، والمحرر كله يرسم بالعائلة نفسها (الخط الأساسي Regular) فيبقى
  عرض الخلية والمؤشر والتحديد وعلامات الأخطاء مضبوطة بلا رسم حرف بحرف.
- الوزن/النمط لكل مفهوم (`RinCodeEditorView.codeFontFor`):
  Bold: `if/each/return`… والأنواع والأصناف · ExtraBold: اسم الدالة بعد `fun` و`@tmpl/@body` · SemiBold: `let/fun/class` والثوابت و`@` ·
  Medium: النداءات `f()` والأرقام والأخطاء · Italic: المعدِّلات `where/yield/sorted` · Medium Italic: معاملات القالب ·
  Bold Italic: تسميات الحلقات وأسماء القوالب · Light Italic: التعليقات · Regular: النصوص والمعرّفات والخصائص.
- الشكل: `@tmpl/@body` داخل «حبة» مستديرة شفافة بلون الكلمة (حبة واحدة تشمل `@` و`tmpl`)، واسم القالب بخط سفلي ناعم.
- يتبع إعداد `syntax_font_styles` (معطَّلاً: Regular للكل). وإن تعذّر تحميل أي خط يعود الرسم للنمط الاصطناعي السابق. لم يُبنَ Kotlin/الموارد هنا.

#### شكل الرموز (operators / punctuation / brackets)
- ستة أنواع تلوين جديدة (30..35) في `rin_editor_engine.h` و`HighlightKind`: `Compare` (`== != < > <= >= =/= =/ !` و`and`/`or`) ·
  `Punct` (`, ; : .` خافتة) · `Flow` (`|>` و`?`) · `Bracket0/1/2` (أقواس `() [] {}` بثلاثة ألوان بالتناوب حسب العمق). رموز الحساب `+ - * / % =` تبقى `Operator` وصار لها لون.
- `classifyPunctuation` (rin_editor_tools.h) تعمل آخر مصنِّف: قوس إغلاق بلا فتح أو غير مطابق يُصنَّف `Error` (أحمر). محلل Kotlin الاحتياطي يلوّن `OPERATOR`.
- الخط: ثلاثة خطوط JetBrains Mono **بـ ligatures** (`res/font/rin_sym_medium|semibold|bold.ttf`): الحساب Medium، المقارنة SemiBold، التدفق Bold.
  `==` `!=` `<=` `>=` `|>` `=/=` تُرسم كرمز واحد بعرض خليتين بالضبط (`fontFeatureSettings = "calt, liga"` لهذه الامتدادات فقط). الأقواس Medium، والفواصل Regular.
- ألوان جديدة: `syntax_operator` `syntax_compare` `syntax_punct` `syntax_flow` `syntax_bracket0/1/2` (فاتح وداكن).
- اختبار المحرر 115/115. لم يُبنَ Kotlin/الموارد هنا.

#### نسخ / قص / لصق احترافي (RinCodeEditorView + ClipboardText.kt)
- `ClipboardText.kt` (جديد، منطق نقيّ بلا Android) + `ClipboardHistory`: توحيد نهايات الأسطر، حذف BOM/المسافات صفرية العرض/NBSP، تبويبات البداية → مسافات،
  إعادة إزاحة اللصق متعدد الأسطر ليلتحم مع إزاحة موضع المؤشر (وضع الكتلة / منتصف السطر)، سياج Markdown، معاينة، وسجلّ حافظة في الذاكرة فقط (25 عنصرًا، بلا تكرار).
- نسخ/قص بلا تحديد = السطر الحالي كاملًا (يُوسَم `rin_lines` فيُلصق فوق السطر الحالي بإزاحته ويبقى المؤشر في مكانه).
- أوامر جديدة: لصق كما هو · سجلّ الحافظة (نقرة = لصق، ضغطة مطوّلة = حذف) · نسخ/قص السطر · تكرار التحديد · تحديد السطر (يتوسّع بالتكرار) · نسخ كـ Markdown.
- لصق ملف ضخم (> 300 ألف حرف) يطلب تأكيدًا. وميض لحظي للنص الملصوق. اللصق/القص/استبدال التحديد = خطوة تراجع واحدة.
- الشريط العائم صار `ActionMode.Callback2` فيظهر عند موضع التحديد، يختفي أثناء سحب التحديد، ونقرة ثانية على المؤشر تفتحه (لصق/تحديد الكل/سجل).
- اختصارات لوحة المفاتيح: Ctrl+C/X/V · Ctrl+Shift+V (كما هو) · Ctrl+Alt+V (سجل) · Shift+Insert/Ctrl+Insert/Shift+Delete · Ctrl+A/D/L.
- قائمة «تحرير» في MainActivity: عناصر 18..26 للأوامر الجديدة. نصوص جديدة بالعربية/الإنجليزية/الإسبانية.
- اختبار: `KOTLINC=/path/kotlinc sh tests/editor/clipboard/run.sh` (28/28). لم يُبنَ المشروع كاملًا (لا Android SDK هنا)؛ فُحص تركيب Kotlin فقط.

---

## ENV

<sub>المصدر الأصلي: `CHANGES_ENV.md`</sub>

### CHANGES_ENV — دعم ملف `.env`
- جديد: `rin_env.cpp` (محلّل dotenv + `Env.*`)، `docs/env.md`، `tests/verification/env_basic.rin|.expected`.
- `rin_interpreter.h/.cpp`: تصريح `registerNativesEnv` + استدعاؤه + `#include "rin_env.cpp"`.
- لم تُعدَّل ملفات Kotlin ولا `rinc.cpp` (لا تلوين لـ `Env` بعد).

---

## HASH

<sub>المصدر الأصلي: `CHANGES_HASH.md`</sub>

### CHANGES_HASH — عائلة `#`

إضافة اختيارية بالكامل: `#sed #for #in #done #while #sum #do #swap #return #diff #add #ban #to #ignorance`.
الشرح: `docs/hash-family.md` · مثال: `examples/hash_demo.rin`.

#### الملفات
- `rin_lexer.cpp|.h` — `scanHashWord()`؛ `#for/#while/#return` ← نفس توكنات for/while/return، و`#in` ← in، وغيرها IDENT يحتفظ بـ `#`.
- `rin_ast.h` — حقل `doneBlock` في WhileStmt وForStmt وForInStmt.
- `rin_parser.h|.cpp` — `hashStatement` (#do #done #ban #ignorance #swap)، `attachDoneBlock`، `maybeHashRange` (`A #to B step C`).
- `rin_interpreter.cpp|.h` — تنفيذ كتلة `#done` في الحلقات الثلاث، و`#ban` عبر آلية stone (E0043)، وتسجيل `registerNativesExtra5`.
- `rin_extra_natives5.cpp` (جديد) — `#sed #sum #diff #add #to #swap`.
- التلوين: `syntaxes/rin-keywords.json` و`syntaxes/rin.tmLanguage.json` و`RinSyntax.kt` و`RinSyntaxHighlighter.kt`.
- اختبارات: `tests/verification/hash_*.rin|.expected` (6).

#### التحقق
`tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 36/37
(الفرق الوحيد `input_forms`: اسم الملف في موقع الخطأ بأداة `rin_run`، وكان كذلك قبل التعديل).
بُني على سطح المكتب (g++). لم يُبنَ APK ولم تُترجَم ملفات Kotlin.

---

## HTML

<sub>المصدر الأصلي: `CHANGES_HTML.md`</sub>

### مشاريع HTML — تشغيل index.html وربطه بـ Rin

نوع مشروع جديد **HTML** في حوار «مشروع جديد» ينشئ:

| الملف | الدور |
|---|---|
| `index.html` | الواجهة |
| `style.css` | التنسيق (يُدمج تلقائياً في الصفحة) |
| `container.rin` | منطق Rin: متغيّرات عامة (`let`) ودوال علوية تستدعيها الصفحة |
| `main.rin` | نقطة دخول رمزية (مطلوبة لتصدير/استيراد `.rinpak`) |

زر **تشغيل (Run)** في المحرر — عند فتح مشروع HTML، أو عند فتح `index.html` / `style.css` / `container.rin`
من أي مشروع فيه `index.html` — يحفظ الملف ثم يفتح `HtmlRunActivity`: WebView يعرض الصفحة مربوطةً بجلسة Rin حيّة.

#### الربط من HTML (بلا JavaScript)

```html
<div rin-text="count">0</div>            <!-- يعرض المتغيّر count -->
<button rin-click="add()">+1</button>    <!-- يستدعي fun add() -->
<button rin-click="addBy(5)">+5</button> <!-- وسائط: أرقام / "نص" / true|false / اسم متغيّر -->
<input rin-model="userName">             <!-- ربط ثنائي الاتجاه -->
<p rin-show="isHigh">…</p>               <!-- يظهر عندما يكون المتغيّر صحيحاً -->
```

```kotlin
let count = 0;
fun add() { count = count + 1; }
```

ومن JavaScript عند الحاجة: `rin.call("add")`، `rin.get("count")`، `rin.set("userName", "x")`.
هذه هي نفس التوجيهات التي تدعمها `web/rinhtml/rinhtml.js` على الويب.

#### الملفات المتغيّرة
- `app/src/main/cpp/jni_bridge.cpp`: دوال JNI للجلسة الحيّة (`htmlCreate/Call/SetGlobal/Globals/Free`) فوق
  `Interpreter::callTopLevelFunction` — نظير `web/rinhtml_bridge.cpp`.
- `RinEngine.kt`: الصنف `RinEngine.HtmlSession`.
- `HtmlRunActivity.kt` (جديد) + تسجيله في `AndroidManifest.xml`.
- `Project.kt` (`ProjectType.HTML`)، `ProjectManager.kt` (القوالب)، `ProjectsActivity.kt`، `MainActivity.kt` (ربط Run).
- أيقونات ملفات محلية بلا شبكة: `ic_html_file`, `ic_css_file`, `ic_container_rin_file` (يربطها `FileIconResolver.kt`).
- دليل الربط الكامل: `docs/html-projects.md`.
- موارد: شريحة النوع في `dialog_create_project.xml`، `ic_type_html.xml`، نصوص ar/en/es، ألوان.
- `schemas/rin-project.schema.json`: إضافة `html` إلى أنواع المشروع.

#### قيود معروفة
- القيم المنقولة إلى الصفحة بدائية فقط (رقم/نص/منطقي/null)؛ المصفوفات والكائنات تُعرض كنص.
- `print` أثناء التشغيل الأولي فقط يظهر في لوحة المخرجات السفلية.
- لم يُبنَ APK في هذه البيئة (لا Android SDK/NDK)؛ منطق الجلسة اختُبر على Linux بنفس واجهة المحرّك.

#### تعديل: container.rin ليس خاصاً بـ HTML
- Run على `container.rin`/أي `.rin` = تشغيل Rin عادي في أي مشروع؛ فتح الصفحة يكون من `index.html`/`style.css`/`main.rin` (مشروع HTML) أو من قائمة Run ← «تشغيل كصفحة HTML».
- ملف المنطق للصفحة اختياري التسمية: `<link rel="rin" href="app.rin">` أو `<meta name="rin" content="app.rin">`، وإلا `container.rin` ثم `main.rin`.

#### تعديل: ملف الحاوية يُعرَّف بالتوقيع + قدرات أقوى
- **التوقيع**: `//! rin:container web [persist=a,b|*]` في أول سطر غير فارغ (`RinContainerFile.kt`). الأيقونة وRun والاختيار
  التلقائي والحفظ تعتمد عليه لا على اسم الملف. `container.rin` بلا توقيع = ملف Rin عادي.
- **HTML**: `rin-for`، `rin-if`/`rin-else`، `rin-attr`، `rin-class`، وتعابير (`== != < > <= >= ! && ||`) ومسارات (`t.title`، `list.length`).
  الاسم الجديد للسكربت: `app/src/main/assets/rin_html_runtime.js` (كان مضمَّناً في Kotlin).
- **الجسر (JNI)**: نقل المصفوفات والقواميس والكائنات بالاتجاهين (JSON) بعمق ≤ 8.
- **حفظ الحالة**: `persist=` ← `.rin_state/<ملف>.json`، ضغطة مطوّلة على إعادة التحميل تمسحها، و`rin.clearState()`.
- القوالب (index.html/style.css/container.rin) تعرض الآن عدّاداً + قائمة مهام محفوظة.
- اختبارات: 18 حالة لسكربت الصفحة (jsdom)، واختبار منطق القالب والجسر على محرّك Rin الحقيقي على Linux.

---

## IMAGE DRAW

<sub>المصدر الأصلي: `CHANGES_IMAGE_DRAW.md`</sub>

### CHANGES_IMAGE_DRAW — إنشاء الصور ورسمها وحفظها PNG + log كصورة
مبنيّ فوق rinlang-main_25 + (changed, _29, _31). الجديد: `img*` (27 دالة) — انظر docs/image-draw.md.
- جديد `app/src/main/cpp/rin_image_draw.cpp` (يُضمَّن في نهاية rin_interpreter.cpp بعد rin_image_ansi.cpp): لوحة RGBA، رسم منعَّم (خط/مستطيل مدوّر/دائرة/قطع/مضلّع/نص/تدرّج/فيضان)، مرمِّز PNG (مرشّحات تكيّفية + zlib، RGB أو RGBA)، BMP (24/32 بت)، PPM، نص 5×7 + خط rinfont العربي التقريبي، `imgFromText/imgFromLog`.
- معدَّل `rin_interpreter.cpp`: تسجيل `registerNativesImageDraw()`، `#include`، وفي `logSave` فرع للامتدادات png/bmp/ppm (يرسم السجلّ صورة). `rin_interpreter.h`: التصريح. `rin_image_ansi.cpp`: `printImage/imageAnsi` تقبلان معرّف لوحة.
- IntelliSense: `syntaxes/rin-keywords.json` + `scripts/intellisense_docs.json` + `rin_intellisense_data.h` (مُولَّد بـ gen_intellisense_data.py).
- اختبار `tests/image/image_draw.{rin,expected}` (حفظ ثم إعادة تحميل ومقارنة بكسلات، شفافية، تحويلات، نص، log).
- التحقق (g++ لينكس، CLI حقيقي): الاختبار الجديد متطابق على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · image_terminal · editor 127/127 + intellisense 115/115 · ملفات PNG/BMP/PPM الناتجة فُتحت بـ PIL. لم يُبنَ APK/الويب/Windows.
- حدود: النص العربي تقريبي (بلا خط مضمَّن/وصل حروف)؛ لا كتابة JPEG/GIF.

---

## IMAGE TERMINAL

<sub>المصدر الأصلي: `CHANGES_IMAGE_TERMINAL.md`</sub>

### CHANGES_IMAGE_TERMINAL — طباعة الصور في الـ terminal كنص
- جديد: `printImage(path, width?, mode?)` · `imageAnsi(...)` · `imageInfo(path)` — صورة ← نصف-كتل 24-bit أو رمادي (blocks/ascii). انظر `docs/image-terminal.md`.
- جديد: `app/src/main/cpp/rin_image_ansi.cpp` (مضمَّن في نهاية `rin_interpreter.cpp` كبقية دفعات natives) · `app/src/main/cpp/third_party/stb_image.h` v2.30 (Public Domain/MIT؛ PNG/JPEG/BMP/GIF فقط، بلا stdio).
- معدَّل: `rin_interpreter.h` (تصريح `registerNativesImage`) · `rin_interpreter.cpp` (استدعاء التسجيل + `#include`) · `syntaxes/rin-keywords.json` (الأسماء الثلاثة) · `scripts/intellisense_docs.json` + `rin_intellisense_data.h` (إكمال وتوقيع للدوال الجديدة).
- اختبار: `tests/image/` (صورة 16×12 + `.rin` + `.expected` بوضع ascii/blocks الحتمي).
- التحقق هنا: بناء CLI كاملاً على g++ (Linux) وتجربة PNG/JPEG/BMP/GIF وكل الأوضاع وكل رسائل الخطأ يدوياً + الاختبار أعلاه. لم يُجرَّب: بناء أندرويد/ويب/Windows (الكود يستعمل ioctl فقط خارج Windows)، ولا عرض الألوان على طرفية حقيقية.
- غير منفَّذ: صيغة `print image "x.png"` داخل المحلل (الدوال تغطي الحاجة)، وعرض ANSI داخل كونسول أندرويد (يستعمل ascii تلقائياً).

#### الدمج مع التحديث 24
- لا `:image`: التحديث 24 فيه `:img` و`print.image` و`imageText`، فبقيت دوال `printImage/imageAnsi/imageInfo` فقط (تكمّلها بفكّ JPEG/GIF مدمج).
- إصلاح IntelliSense: إكمال سمات `print.image(...)` (mode/cols/invert/color/caption/if) + توثيق `imageText` + اختباران (115/115).

---

## INDSINWEB

<sub>المصدر الأصلي: `CHANGES_INDSINWEB.md`</sub>

### CHANGES_INDSINWEB — مكتبة WebView والروابط لـ indsin (الإصدار 1.0.0)

#### الجديد
- **شاشة الربط** `iwLinkScreen`: تعرض الرابط حسب نوعه (فيديو، صفحة ويب، صورة، صوت، PDF، مستند، بريد، هاتف، موقع، واتساب/تيليجرام، متجر، مكتبة Rin، محجوب) بالعربية أو الإنجليزية وبثيم داكن/فاتح.
- **أيقونة المكتبة**: `assets/branding/indsinweb_icon.png` (512×512) و`.svg`.
- أدوات إضافية: تنظيف التتبّع، حجب العناوين المحلية/الخاصة، كشف نوع الملف، شريط عنوان (رابط/بحث)، مشاركة، تضمين Spotify/Dailymotion/Drive/Maps، توجيه عميق، `iwLinkify`/`iwAuditHtml`، `iwOpenPlan`، بطاقات/خطأ/تحميل، مفضّلة JSON.

- `lib/indsinweb.og.rin` (+ نسخة مضمَّنة في `app/src/main/cpp/rin_stdlib_libs.h` و`embeddedRinLibraries()`): دوال `iw*`
  للروابط (تحليل/تطبيع/ضم/استعلام)، الأمان (قائمة سماح للمخططات والمضيفين، حارس userinfo، `iwSanitizeHtml`)،
  أنواع الروابط (mailto/tel/sms/geo/WhatsApp/Telegram/Play/intent/deep link)، روابط Rin (`@user/lib.og.rin`)،
  تضمين يوتيوب (nocookie) وVimeo، بناة HTML لخاصية `html=`، ومولّد عنصر `WebView` وسجل تنقّل.
- `docs/indsinweb.md` المرجع · `examples/indsinweb_demo.rin` مثال `@container` · `tests/indsinweb_tests.rin` (287 اختبارًا).
- صفّان في `docs/standard-library.md` و`README.md`.

#### تحقق
- `tests/indsinweb_tests.rin`: 287/287 على القرص وعلى النسخة المضمَّنة (مفسّر مبني من مصدر المشروع بـ g++).
- المثال عبر `runColdPipelineForContainerWithRuntime` الحقيقي: `url=` أصبح رابط تضمين youtube-nocookie، و`html=`
  بُني بالدوال، والرابط `javascript:` ظهر كـ`<span>` محجوب.
- لم يُبنَ APK ولم يُشغَّل على جهاز.

#### ملاحظات
- `text` و`style` و`to` كلمات محجوزة في Rin (لا تصلح أسماء متغيرات)، والنفي `!` لا `not`.
- أداة `rin_indsin_run render` الجاهزة تعرض التعابير غير مقيَّمة (بلا مضيف مفسّر)، وهذا سلوك قائم لكل المكتبات (مثل relyRIN).
- ملف `build-artifact/rin_run` المرفق قديم (لا يدعم `for (let x in ...)`)؛ استعمل مفسّرًا مبنيًا من المصدر.

---

## INDSIN ICONS

<sub>المصدر الأصلي: `CHANGES_INDSIN_ICONS.md`</sub>

### CHANGES_INDSIN_ICONS — مكتبة أيقونات متجهية لـ indsin

انظر `docs/indsin_icons.md`.

#### جديد
- `tools/icons/` (icons_def.py، gen_indsin_icons.py، gallery_template.html، raster.py، test_icons.py)
- `assets/indsin-icons/` (svg/، sprite، icons.json، gallery.html)
- `lib/indsinicons.og.rin` · `app/src/main/cpp/indsin/rin_indsin_iconpaths.h`
- `tests/indsin_icons_tests.rin` · `tools/test_indsin_icon_vectors.cpp` · `examples/indsin_icons_demo.rin`

#### معدَّل
- `app/src/main/cpp/indsin/rin_indsin_icons.h`: واجهة المتجهات (vectorPath/svg/registerVector)؛ سلوك resolve/has القديم لم يتغيّر.
- `app/src/main/assets/indsin/indsin_object.html`: كتلة ICON-VECTORS، `iconHtml()`، قاعدة `.ix-svg`، والمدقق يعرف الأسماء المتجهية.

#### تحقق
rin tests/indsin_icons_tests.rin 12/12 · test_icons.py كله ناجح (يشمل مطابقة Rin لـ Python حرفياً) · اختبار C++ الجديد ناجح ·
test_indsin_icons القديم ALL PASS · selfTest للمحرّك 65/65. لم يُبنَ APK/WASM.
مطبَّق على rinlang-main__3_ وملفاه المعدّلان مطابقان تماماً في rinlang-main__4_.

#### دمج في RinStudio (إضافة)
- مكتبة مدمجة: `rin_stdlib_libs.h` (+ `tools/embed_indsinicons.py`)؛ اختُبر `@import` من مجلد بلا lib/ بمفسّر مبنيّ.
- جديد: `IconLibraryActivity.kt`، `widgets/IndsinIcons.kt`، `widgets/IndsinIconView.kt`، `assets/indsin-icons/icons.json`،
  `activity_icon_library.xml`، `item_icon_tile.xml`، `dialog_icon_detail.xml`، `ic_lib_indsinicons.xml`،
  `bg_featured_icons_card/bg_search_field/bg_lib_chip_selected/bg_btn_white/bg_icon_tile/bg_icon_preview_box.xml`،
  `assets/branding/indsinicons_icon.svg`.
- معدَّل: `LibrariesActivity.kt` (بحث + شرائح + بطاقة مميّزة)، `RinLibrary.kt` (LibCategory + مدخل المكتبة)،
  `activity_libraries.xml`، `item_builtin_library.xml`، `AndroidManifest.xml`، `strings_libraries.xml` (ar/en/es).
- لم يُبنَ APK ولم يُجرَّب الشكل على جهاز (لا Android SDK/Kotlin هنا): فُحصت الموارد والأقواس سكونياً فقط.

---

## INDSIN MEDIA

<sub>المصدر الأصلي: `CHANGES_INDSIN_MEDIA.md`</sub>

### CHANGES_INDSIN_MEDIA — اختيار الوسائط ورفعها في indsin

#### الجديد
- أفعال: `pickMedia` · `uploadMedia` · `clearMedia` · `removeMedia` + خلايا Warp مشتقة (حالة/تقدّم/خطأ/ردّ).
- محرّك: `indsin/rin_indsin_media.h`، تعديلات في `rin_indsin_needle.h` / `_actions.h` / `_pipeline.h` / `_c_api.*`، ودالتان JNI.
- أندرويد: `IndsinMediaHost.kt` (منتقي SAF بلا صلاحيات، نسخ آمن إلى `media/`، رفع multipart بالبث مع تقدّم)، وربط في `IndsinPreviewActivity`.
- وثائق `docs/indsin_media.md`، مثال `examples/samples/indsin_media_demo.rin`، اختبار `tools/test_indsin_media.cpp`.

- تطوير: `cancelUpload(cell)`، وسيط `auth` (ترويسة Authorization)، وضع `"append"` للاختيار المتعدد المتتابع.
- إصلاح: رفع ثانٍ مرفوض كان يكتب `error` فوق حالة الرفع الجاري (كشفه الاختبار).

- **الكاميرا**: `pickMedia("camera" | "camera:video", ...)` عبر عقود النظام + FileProvider، بلا صلاحية.
- **تصغير/ضغط الصور**: وسيطا `maxDim` و`quality` (احترام EXIF، فك بـ inSampleSize، إسقاط EXIF).
- **إعادة المحاولة**: وسيط `retries` (افتراضي 2) بتراجع أسّي + خلية `_attempt`؛ 4xx لا تُعاد.
- ملف جديد معدَّل: `app/src/main/res/xml/file_paths.xml` (مسار `indsin_capture`).

#### تحقق
- `tools/test_indsin_media.cpp`: 26/26 (مبني بـ g++ من مصدر المشروع) + `test_indsin_actions` ما زال ينجح.
- `rin_indsin_c_api.cpp` يمرّ بـ `-fsyntax-only`.
- **لم يُترجَم كود Kotlin/JNI ولم يُشغَّل على جهاز** (لا Gradle/NDK هنا) — جرّب بناء APK واختبر المنتقي والرفع فعلياً.

---

## INDSIN OBJECT

<sub>المصدر الأصلي: `CHANGES_INDSIN_OBJECT.md`</sub>

### indsin-object 4.0.0 — change summary

#### Translator & interpreter
* `tools/indsin-object/src/indsin_object.js` — rewritten as the official translator + interpreter: all 65 element kinds and aliases, design tokens, 11 themes, responsive overrides, effects, overlays, RTL, accessibility, absolute (engine frame) and flow (logical tree) modes, sandboxed media, normalised event bridges for every platform.
* Verified against real frames from the compiled C++ engine and in Chromium.
* `bin/indsin-object.js` (CLI), `build.js`, `src/host.template.html` → `indsin_object.html` (+ Android asset), `package.json`, `types/*.d.ts`, `tests/` (52 tests + real-frame fixtures), `README.md`, docs.

#### Icon
* New SVG/PNG/ICO/ICNS set in `assets/branding/indsin/`; Android `ic_indsin_file.xml` redrawn, new `ic_indsin_logo.xml`.

#### `.indsin` file association
* Linux (MIME, `.desktop`, icons), Windows (per-user registry, icon, verbs), macOS (app bundle + UTI), Android (`IndsinFileViewer` alias + `IndsinPreviewActivity` reads the document URI).
* `bin/indsin-open.js` launcher: live preview with the engine, highlighted source otherwise.

#### Syntax highlighting
* `syntaxes/indsin.tmLanguage.json`, `syntaxes/indsin-lexicon.json`, VS Code extension, Linguist patches, `src/indsin_highlight.js`, Kotlin `indsin` profile in `RinSyntaxHighlighter.kt`.

#### Platform hosts
* `IndsinObjectHost.kt` (Android), `IndsinObjectHost.swift` (iOS/macOS), `rin_indsin_object_web.h` (C++/Emscripten), Node example.

#### Notes
* Kotlin and Swift sources were reviewed but not compiled in this environment (no Android/Xcode toolchain); the regular expressions used by the Kotlin highlighter were verified with the JVM, the C++ header with `g++ -std=c++17`.
* The npm registry was unreachable during the work, so the TextMate grammar was validated structurally (all regexes compile, scopes exercised) rather than through `vscode-textmate`.

---

## INDSIN RIN

<sub>المصدر الأصلي: `CHANGES_INDSIN_RIN.md`</sub>

### CHANGES_INDSIN_RIN — indsin يعتمد على لغة Rin

انظر `docs/indsin_rin.md`. الملخص: قيم خصائص `@view` تُقيَّم بمفسّر Rin الحقيقي (متغيرات، دوال، مقارنات، منطق، فهرسة،
Set...) + مكتبة `indsin.*` تعرض نظام التصميم لكود Rin + حارس نقاء + توافق خلفي كامل بلا مضيف.

#### تحقق
rintests 83/83 · wesscode_tests 42/42 · verification 52/53 (الفرق `input_forms` قديم) · الجديد test_indsin_rin_bridge 47/47 ·
اختبارات indsin القائمة الـ14 (actions banner button element_merge export icons live_runtime missing_components oop_bind
overlay sizing system tokens ui_library_expansion) كلها ناجحة · المثال يعمل. بُني على سطح المكتب (g++)؛ لم يُبنَ APK/WASM.

#### ملاحظة دمج
مبنيّ فوق `rinlang-changed-files.zip` (الشروط + Set) مطبَّقاً على `rinlang-main_full.zip`. الملفات المعدّلة في المفسّر:
`rin_interpreter.h` (تصريحات عامة جديدة) و`rin_interpreter.cpp` (سطر `#include "rin_expr_host.cpp"` فقط).

---

## INT128

<sub>المصدر الأصلي: `CHANGES_INT128.md`</sub>

### CHANGES_INT128 — إصلاح فشل بناء armeabi-v7a / x86 (`__int128 is not supported on this target`)

#### السبب
`cli/linux/src/terminal/rin_terminal_xcmds6.h` كان يستخدم `unsigned __int128` في الأوامر
`isprime` و`comb` و`lcm` و`modpow` و`frombase`. هذا النوع غير مدعوم على الأهداف 32-بت
(armeabi-v7a و x86) فتوقف بناء الـ APK الموقّع عند `buildCMakeRelWithDebInfo[armeabi-v7a]`
بـ 18 خطأ (arm64-v8a كان ينجح لأنه 64-بت).

#### الإصلاح
- `rin_terminal_xcmds6.h`: نوع `U128` محمول (حقلان `uint64_t`) مع `mul64x64` و`mul64` و`add64` و`divmod64`
  و`u128ToDec` و`mulmod64`. على الأهداف التي تعرّف `__SIZEOF_INT128__` يبقى `mulmod64` يستعمل النوع الأصلي
  (نفس السرعة)، وإلا يُستعمل المسار المحمول. يمكن فرض المسار المحمول بـ `-DRIN_NO_INT128` للاختبار.
- تصحيح جانبي في `comb`: الضرب الوسيط كان قد يطفح بصمت؛ صار يُكتشف ويُرجع "result too large".

#### ثغرات `SET` (تحذيرات `-Wswitch: enumeration value 'SET' not handled`)
- `rin_extra_natives4.cpp` (`emitJson`): المجموعة كانت لا تُكتب إطلاقاً فيخرج JSON غير صالح؛ صارت مصفوفة.
- `rin_oop_natives.cpp` (`oop.hash`): محتوى المجموعة كان مُهملاً؛ صار تمثيلاً قانونياً مستقلاً عن الترتيب.
- `rin_interpreter.cpp` (`serializeValueLiteral`): المجموعة كانت تُحفظ `nil`؛ صارت `Set([...])`.

#### التحقق
- اختبار عشوائي (400 ألف حالة) للدوال المحمولة مقابل `__int128`: 0 اختلاف (المساران).
- مقارنة `comb/lcm/modpow/frombase/isprime` مع Python الدقيق على المسارين: متطابقة.
- `rintests` 83/83 · `wesscode_tests` 42/42 · اختبار جديد `tests/verification/set_json_hash.rin`.

---

## INTELLISENSE

<sub>المصدر الأصلي: `CHANGES_INTELLISENSE.md`</sub>

### CHANGES_INTELLISENSE — IntelliSense احترافي في المحرر

استُبدل الإكمال القديم (بادئة ← 6 كلمات) بمحرك سياقي كامل. المنطق كله في C++ (قابل للاختبار بـ g++)، والواجهة في Kotlin.

#### ما الجديد
- **إكمال سياقي** (`rin_intellisense.h`): يعرف أين المؤشر فيقترح ما يناسب فقط:
  - `obj.` أعضاء: مساحات الأسماء (`Set.` `Env.` `container.` `container.artifact.` …)، متغيّر `Set` (`s.add` بلا المعامل الأول)، أصناف المستخدم (مع الوراثة، `self.`/`super.`، الثوابت `static`، حالات `enum`)، مفاتيح الكائنات الحرفية `{a: 1}`.
  - `@` وسوم (`@container.*` `@element.*` `@view` `@theme` `@import` …) وقوالب `@name` المعرّفة في الملف · `#words` · مسار `@import "lib/…"` (76 وحدة) · تسميات بعد `break/continue` · أصناف فقط بعد `extends/implements/uses`.
  - بعد `@tmpl name(…)`: `print` `style` `text` `each` · داخل `print`/`show`: سمات الطباعة (`bold` `color` `width` `if` … مع `=` والمؤشر داخل علامتي الاقتباس).
  - **نطاقات حقيقية**: `let`، معاملات الدوال، متغيّرات `each/repeat/retry/for/catch/as`، متغيّرات الـ comprehension؛ لا تظهر خارج نطاقها. الدوال/الأصناف العلوية مرفوعة (hoisted).
  - الكلمات المحجوزة حسب الموضع (`while` لا تُقترح داخل تعبير)، القوالب (snippets) في بداية العبارة فقط.
  - صامت داخل التعليقات والنصوص وعند تسمية تعريف جديد (`let |` `fun f(a, |`).
- **ترتيب ضبابي**: بادئة > مقاطع camelCase (`iN` ← `inputNumber`) > تسلسل جزئي، + أولوية للأقرب نطاقاً + تعلّم من الاستخدام (MRU).
- **أنواع تقريبية** تظهر بجانب المتغيّر (`number` `string` `array` `Set` اسم صنف …) وتعليقات `//` فوق التعريف تصير توثيقاً.
- **Signature Help**: توقيع الدالة مع المعامل النشط مُبرَز (مدمجة، مستخدم، منشئ صنف عبر `init`، قالب `@name(`، `Set` عبر المتغير). يتتبّع المؤشر أيضاً دون كتابة.
- **Hover/ضغط طويل**: يعرض توقيع وتوثيق الرمز (مستخدم/مدمج/كلمة محجوزة).
- **بيانات الدوال** (`rin_intellisense_data.h`، مولَّد بـ `scripts/gen_intellisense_data.py`): 892 دالة، عدد معاملاتها مستخرج من المفسّر نفسه؛ 120 منها موثّقة يدوياً ومحقَّقة من تنفيذها (يفشل المولّد إن تعارض التوثيق مع المصدر). الباقي يظهر بعدد المعاملات فقط.
- **واجهة** (`RinCompletionPopup.kt`): شارات أنواع ملوّنة بألوان التلوين، إبراز الحروف المطابقة، تفصيل خافت، لوحة توثيق للعنصر المحدّد، تمرير، وضع فوق/تحت السطر حسب المساحة المرئية (يراعي لوحة المفاتيح).
- **لوحة مفاتيح خارجية**: ↑↓ تنقّل · Tab يقبل · Enter يقبل بعد التنقّل بالأسهم فقط (وإلا سطر جديد) · Esc يغلق · Ctrl+Space إكمال صريح (`triggerCompletion()` عام للاستدعاء من شريط الأدوات).
- الإعداد الحالي `autocomplete` يتحكم بالقائمة والتوقيع معاً.

#### الملفات
جديد: `cpp/editor/rin_intellisense.h` · `rin_intellisense_data.h` (مولَّد) · `RinCompletionPopup.kt` · `scripts/gen_intellisense_data.py` · `scripts/intellisense_docs.json` · `tests/editor/intellisense_selftest.cpp` · هذا الملف.
معدَّل: `rin_editor_engine.h/.cpp` (`complete` `signatureHelp` `describeAt` `recordCompletionUse`) · `rin_editor_jni.cpp` · `RinNativeEditor.kt` · `RinCodeEditorView.kt` (يفترض تطبيق تعديل ستايل الحاويات قبله) · `tests/editor/run.sh`.
`collectSuggestions` القديمة باقية (تستعملها passkit).

#### التحقق
`sh tests/editor/run.sh`: المحرر 115/115 + IntelliSense 113/113، على 49 ملفاً حقيقياً (examples/ + tests/loops + tests/print) بلا أي انهيار؛ أسوأ استدعاء 9ms، ومستند 16 ألف سطر 60ms (-O0).
**لم يُبنَ**: Kotlin وJNI على Android (لا SDK/NDK هنا). فُحصت صياغة Kotlin بـ kotlinc بلا أخطاء نحوية، لكن الأنواع لم تُتحقَّق — ابنِ بـ Gradle وجرّب على الجهاز.
#### حدود معروفة
- أنواع المتغيّرات استنتاج سطحي (أول تعبير بعد `=`)؛ استدعاءات `obj.method().` لا تُحلَّل.
- الدوال المدمجة غير الموثّقة يدوياً تُعرض بأسماء معاملات عامة (`arg1…`).
- تعلّم الاستخدام (MRU) في الذاكرة فقط (يُمسح عند إغلاق التطبيق).

---

## LET

<sub>المصدر الأصلي: `CHANGES_LET.md`</sub>

### CHANGES_LET — `let` المطوَّر

إضافة اختيارية بالكامل: الصيغة القديمة `let x = 5;` لم تتغيّر. الشرح: `docs/let-plus.md`.

- تفكيك المصفوفات والقواميس والكائنات: `let [a, b = 0, ...rest] = arr;` و`let {name, city: town} = m;` (تعشيش، `_`، افتراضيات، `...rest`).
- عدة تصريحات: `let a = 1, b = a + 1;`
- حارس: `let v = f() else { return; };` (كتلة else يجب أن تغادر النطاق وإلا E0048).
- تفكيك في `for`: `for (let [k, v] in pairs)`.
- إسناد تفكيكي: `[a, b] = [b, a];` و`[arr[0], arr[1]] = [arr[1], arr[0]];`، ويعمل مع المتغيرات الحيّة.
- `export let a = 1, b = 2;` يصدّر كل الأسماء.

#### الملفات
`diagnostics/diagnostic.h|.cpp` (E0048) · `rin_ast.h` (LetPattern, LetPatternStmt, LetGroupStmt, PatternAssignExpr،
وحقل elseBlock في LetStmt وpattern في ForInStmt) · `rin_parser.h|.cpp` · `rin_interpreter.cpp`.

#### التحقق
`tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 30/31
(الفرق الوحيد `input_forms`: اسم الملف في موقع الخطأ بأداة `rin_run`، وكان كذلك قبل التعديل).
بُني على سطح المكتب (g++). لم يُبنَ APK.

---

## LINALG ALGO

<sub>المصدر الأصلي: `CHANGES_LINALG_ALGO.md`</sub>

### CHANGES_LINALG_ALGO.md — بيئة المهندسين: الجبر الخطي وتطوير الخوارزميات

دمج أرشيفات number وtoken وscope_guarantees في الشجرة الرئيسية (بالترتيب: scope ← token ← number؛ كل أرشيف يحتوي سابقه في الملفات المشتركة)، ثم إضافة **77 دالة** جديدة بلا تغيير في أي دالة قائمة.

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_extra_natives10.cpp` | جديد: 54 دالة `mat*`/`vec*` (تشمل `matEig`) (docs/LINALG.md) |
| `app/src/main/cpp/rin_extra_natives11.cpp` | جديد: 23 دالة `algo*` (docs/ALGO.md) |
| `rin_interpreter.cpp/.h` | تسجيل `registerNativesExtra10/11` وتضمين الملفين |
| `rinc.cpp`، `RinSyntax.kt`، `syntaxes/rin-keywords.json` | الأسماء الجديدة |
| `scripts/intellisense_docs.json`، `editor/rin_intellisense_data.h` | التوثيق (والملف مُعاد توليده بالمولّد) |
| `docs/LINALG.md`، `docs/ALGO.md` | الوثائق |
| `tests/verification/linalg_algo.rin/.expected` | الاختبار (~165 سطراً، قيم محقَّقة مقابل NumPy + خصائص جبرية) |

إعادة استعمال لا إعادة بناء: التمثيل = مصفوفات Rin العادية؛ جمع Neumaier وتساوي `numberApprox` وتنسيق `numberFormat` من مفهوم number؛ `algoPolyFit/algoNewton/algoPolyRoots` فوق LU/SVD/القيم الذاتية نفسها.

إضافات لاحقة: `matEig` (متجهات ذاتية عامة)، `algoMinimize({method:"bfgs"})`، `algoOde({method:"stiff"})`.

ملاحظات: `show`/`row`/`ok`/`num` أسماء محجوزة في Rin (لا تصلح أسماء دوال)؛ الدوال الأصلية لا تُمرَّر بالاسم كقيم.

---

## LIVING

<sub>المصدر الأصلي: `CHANGES_LIVING.md`</sub>

### CHANGES_LIVING — المتغيرات الحيّة

إضافة اختيارية بالكامل (additive): `stone` `gauge` `tape` `lens` `fuse` `bell` `trial` + `undo` `redo` `rearm` `unbell` `abort`.
الشرح: `docs/living-variables.md`.

#### ملفات المحرّك (app/src/main/cpp)
- `diagnostics/diagnostic.h|.cpp` — أكواد E0043..E0047.
- `rin_ast.h` — `LiveDeclStmt/BellStmt/TrialStmt/LiveActionStmt`.
- `rin_parser.h|.cpp` — `liveDeclaration/bellDeclaration/trialStatement/liveActionStatement` + التوزيع في `declaration()`.
- `rin_interpreter.h|.cpp` — `LiveMeta` داخل `Environment::live`، وخمس نقاط ربط (قراءة/إسناد/خاصية/كتابة محتوى/تنفيذ عبارات) خلف `liveActive_`.

#### أدوات وتوثيق
`syntaxes/rin-keywords.json` و`syntaxes/rin.tmLanguage.json` و`RinSyntax.kt` و`RinSyntaxHighlighter.kt` (تلوين)،
`docs/living-variables.md` (جديد) + روابط في `variables.md` `language-reference.md` `ERROR_SYSTEM.md`،
`examples/living_variables_demo.rin`، و`tests/verification/living_*.rin|.expected` (8 اختبارات).

#### التحقق
`tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 8/9 (الفرق الوحيد اسم الملف في موقع الخطأ بأداة `rin_run`).
بُني على سطح المكتب (g++). لم يُبنَ APK.

---

## LOOPS

<sub>المصدر الأصلي: `CHANGES_LOOPS.md`</sub>

### CHANGES_LOOPS — Loops v2
ميزات جديدة بالكامل additive (لا تغيير في أي صيغة قائمة): `each` (فهرس/قاموس/تفكيك/`where`/`reversed`)، `repeat N [as i]`،
`loop`/`loop (c)`/`loop until (c)`، تسميات الحلقات `name:` + `break name`/`continue name`، `limit N` على كل الحلقات،
`retry N [as a]`، وتعبير التجميع `[each x in xs where c yield e]`. كانت `each/repeat/loop` موثّقة في docs/loops.md لكنها غير منفّذة.

#### إضافة: أنماط each المتقدمة
`by N` (مجموعات) · `window N` (نوافذ) · `every N` (خطوة) · `zip` (توازٍ) · `for x in xs { }` بلا أقواس. كلها تفكيك إلى ForStmt/Block داخل المحلل أيضاً. اختبار: `tests/loops/loops_modes.rin`.

#### الملفات
`app/src/main/cpp/rin_parser.cpp|.h` فقط: كل الصيغ تُفكَّك في المحلل إلى While/For/ForIn/Block/If/TryCatch/Goal الموجودة
(enterLoop/exitLoop/finishLoop). المفسّر وRinVM لم يُعدَّلا. `docs/loops.md`، `tests/loops/*`.

#### التحقق
tests/loops 5/5 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · verification 57/63
(الستة `let_err_*` تعرض الخطأ نفسه بصيغة كاملة بدل الرمز — لا علاقة بالتعديل). بُني على سطح المكتب (g++)؛ لم يُبنَ APK.

#### Loops v3 (إضافة)
`skip/take/until/while` · `sorted [by KEY] [desc]` · `unique` · `flat` · `chain` · `downto` · `cross` · `pairs` · `from N` · `nobreak {}` ·
اختزالات `sum/product/count/avg/min/max/any/all/first/last each ...` · قاموس مفهوم `{each x in xs yield k : v}`.
الملفات: `rin_parser.cpp|.h` (تفكيك) و`rin_extra_natives5.cpp` (دوال `#downto #sorted #sortby #unique`). المفسّر وRinVM لم يُعدَّلا.
التحقق (g++ على لينكس): tests/loops 7/7 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/vm تطابق 5/5.
لم تُشغَّل: tests/verification، بناء APK/الويب. الأداء لم يتغيّر.

#### دمج تحديث rinlang-loops-changed
محلل الحلقات في الأرشيف الجديد فرعٌ مواز ينقصه ما في v3 (`nobreak`/`downto`/`cross`/`pairs`/الاختزالات/`from`) فلم يُستبدل،
وإنما نُقلت ميزتاه إلى محلل v3: `distinct [on (key)]` (مرحلة تيار) و`each x in xs, y in ys` (rin_parser.cpp|.h)، مع
`RinSyntax.kt` و`syntaxes/rin-keywords.json` وتوثيق واختبارَي `loops_distinct` و`loops_pro`.
التحقق (g++، مفسّر `rin` مبني محلياً): tests/loops 9/9 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42.
لم يُشغَّل: tests/verification وبناء APK/الويب.

#### Rin For+ (حلقة for على طراز Rin)
`i++`/`i--` و`+= -= *= %=` (تعبيرات عامة في المحلل) · `for` بلا أقواس · عدّة خطوات تزايد `i++, j--` · مسار سريع بلا closures.
الملفات: `rin_parser.cpp|.h` (تفكيك)، و`rin_ast.h` (`ForStmt::freshIterScope`)، و`rin_interpreter.cpp` (فرع ForStmt: تنفيذ الجسم في forEnv مباشرة
عندما لا توجد closures). RinVM لم يُعدَّل. اختبار: `tests/loops/loops_for.rin`. `/=` تبقى `!=` كما هي في اللغة.
التحقق (g++ لينكس): tests/loops 10/10 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/vm تطابق 5/5 · verification 57/63 (فروقات let_err_* المعروفة).

#### قوالب الحلقات `@tmpl`
`@tmpl name(params[=default]) [as vars] <ترويسة حلقة>;|{ جسم افتراضي }` ثم `@name(args) [as vars] { body }`. قالب كتلة: `@tmpl name(..) as i, j { ... @body ... }`.
تعمل مع كل أنواع الحلقات وفي التعابير (`[@t(xs) yield e]`, `sum @t(xs) yield e`) وتتركّب (قالب يستعمل قالباً) بلا تعديل في المفسّر أو RinVM:
مرحلة توكنات `Parser::expandLoopTemplates` تُنفَّذ مرة في بداية `parse()`/`parseCollectingDiagnostics()` ولا تعمل أصلاً إن لم يوجد `@tmpl`.
الملفات: `rin_parser.cpp|.h` (قسم `@tmpl` في آخر الملف) · `docs/loop-templates.md` · `docs/loops.md` · `tests/loops/loops_tmpl.rin|.expected` ·
`syntaxes/rin.tmLanguage.json` (تلوين) · `editor/rin_editor_tools.h` (قالبا `tmpl` و`tmplb`).
التحقق (g++ لينكس، بناء `cli/linux`): tests/loops 11/11 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/vm تطابق 5/5 · tests/editor 98/98.
لم يُشغَّل: tests/verification، بناء APK/الويب/Kotlin. الحدود: القالب خاص بالملف (لا يعبر `@import`) وغير معزول الأسماء.

---

## LOOP I

<sub>المصدر الأصلي: `CHANGES_LOOP_I.md`</sub>

### CHANGES_LOOP_I — `i` ضمني في الحلقات
`repeat N { ... i ... }` و`each x in xs { ... i ... }` تكشفان `i` (رقم الدورة من 0) دون `as i`.
تُفعَّل فقط إن ذكر الجسم `i` ولم يعرّفه/يسنده (`let i`, `i = `, `i++`, `+=`, `as i`) ولم يكن `i` معرَّفاً قبل الحلقة في نطاق مرئي (let/for/as/each/معامل دالة) — فلا يتغيّر معنى `i` الخارجي في الشيفرة القديمة (نجحت tests/loops كلها).
`each` ينفَّذ بعدّاد مخفي `#ciN` وسطر `let i` في بداية كل دورة (يعمل مع كل الأنماط والقواميس). `repeat` يستعمل اسم الفهرس نفسه.
الملفات: `app/src/main/cpp/rin_parser.cpp` (`bodyWantsImplicitI` داخل `loopsV2Statement`) · `docs/loops.md` · `tests/loops/loops_implicit_i.rin|.expected`.
التحقق: tests/loops على `rin run` و`rin --vm` (g++ لينكس). rintests 83/83 · wesscode 42/42. لم يُشغَّل: verification، APK/الويب.

#### y الضمني + توحيد خوارزمية الكشف
`each x in xs { ... y ... }`: `y` = قيمة x في الدورة السابقة (nil في الأولى) عبر متغيّر مخفي `#cyN` يُحدَّث في بداية كل دورة (آمن مع continue).
الكشف صار دالة واحدة معمَّمة بالاسم (`nameDeclaredBefore(nm)` / `bodyWantsImplicit(nm)`) تخدم `i` و`y`: مسح عكسي للنطاقات المرئية ثم مسح الجسم.
اختبار: `tests/loops/loops_implicit_y.rin`. التحقق: tests/loops كلها على المحرّكين · rintests · wesscode.

#### تطوير y/i: كل أشكال each
`i` و`y` يعملان الآن في `for x in` بلا أقواس، `[each ... yield ...]`، `{each ... yield k : v}`، واختزالات `sum/product/count/avg/min/max/any/all/first/last`، إضافة إلى `each` (و`repeat` لـ i).
تغيير بنيوي: الكشف صار دوالّ في Parser (`nameDeclaredBefore` · `implicitWanted` · `buildEachImplicit` في rin_parser.cpp|.h) تعمل على مدى توكنات يُحلَّل بعد الترويسة، بدل مسح الجسم قبل تحليله.
حدّ موثّق: لا يُقرآن في شروط الترويسة (where/skip/take/until/while). اختبار: `tests/loops/loops_implicit_y2.rin`.
التحقق: tests/loops (26 تشغيلة على المحرّكين) · rintests 83/83 · wesscode 42/42.

---

## MAIN

<sub>المصدر الأصلي: `CHANGES_MAIN.md`</sub>

### تطوير main.rin: يبدأ بكود يمثّل لغة Rin

كل `main.rin` جديد يبدأ الآن بكود Rin حقيقي (لا تعليق) يعرّف اللغة نفسها من المحرّك الفعلي:

```kotlin
print "◆ Rin " + rinVersion() + " · " + rinEdition();
print "▸ اسم_المشروع";
```

عند Run يكون أول ما يظهر في الـ terminal هو توقيع Rin ونسخته وإصداره ثم اسم المشروع.
ينطبق على كل الأنواع (Container / Table / UI / Free / Illust / HTML)؛ ويبقى قالب Free «فارغ» فارغاً تماماً كما اختار المستخدم.

#### قالب Free الافتراضي صار جولة سريعة في ما يميّز Rin
```kotlin
// 1) warp: متغيّر حيّ، تتحدّث أي واجهة مرتبطة به عند تغيّره
warp score = 0;
score = score + 10;
print "score:", score;

// 2) الأنابيب |> : مرّر القيمة عبر سلسلة دوال
fun double(x) { return x * 2; }
fun inc(x) { return x + 1; }
print "pipeline:", 5 |> double() |> inc();

// 3) الحاويات @container: بيانات وسلوك داخل كتلة مسمّاة
@container=Main
    warp counter = 0;
    print "counter:", counter;
.end/container
```

الملف المعدَّل: `app/src/main/java/com/dlof/rinlang/ProjectManager.kt`
(`mainRinTemplateFor` ← `rinIdentityPrologue` + `mainRinBodyFor`).

#### الاختبار
شُغِّل الناتج على محرّك Rin الحقيقي (Linux): السطران الأولان يطبعان `◆ Rin 1.0.0 · 2026` و`▸ اسم`، والجولة
تعطي `score: 10` و`pipeline: 11` و`counter: 0`. جُرِّبت أيضاً البادئة قبل قوالب UI وTable دون أي مشكلة.

---

## MAX MIN

<sub>المصدر الأصلي: `CHANGES_MAX_MIN.md`</sub>

### CHANGES_MAX_MIN
`max`/`min` صارتا: عدد غير محدود من الوسائط، أو مصفوفة واحدة، أرقاماً أو نصوصاً (مصفوفة فارغة => nil؛ خلط الأنواع = خطأ واضح). `min(a, b)` القديمة بلا تغيير.
جديد: `maxBy/minBy(arr, fn)` · `argmax/argmin(arr)` · `minmax(arr)`. الأول يفوز عند التساوي.
الملفات: `rin_interpreter.cpp` (`extremumOf` + min/max) · `rin_extra_natives2.cpp` (العائلة الجديدة) · `docs/max_min.md` · `tests/verification/max_min_family.rin|.expected`.
التحقق (g++ لينكس): الاختبار الجديد على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/loops. لم يُبنَ APK/الويب.

---

## NUMBER

<sub>المصدر الأصلي: `CHANGES_NUMBER.md`</sub>

### CHANGES_NUMBER.md — تطوير مفهوم number

إضافة بحتة: 21 دالة جديدة (`number` و`numberXxx`)؛ لا تغيير في `toNumber/round/sum/clamp/...`.

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_extra_natives9.cpp` | جديد: الدوال |
| `app/src/main/cpp/rin_interpreter.cpp/.h` | تسجيل `registerNativesExtra9` وتضمين الملف |
| `rinc.cpp`، `RinSyntax.kt`، `syntaxes/rin-keywords.json` | الأسماء الجديدة |
| `scripts/intellisense_docs.json`، `editor/rin_intellisense_data.h` | التوثيق (والملف مُعاد توليده بالمولّد) |
| `docs/NUMBER.md` | الوثيقة |
| `tests/verification/number_concept.rin/.expected` | الاختبار (78 سطراً) |

ملاحظة جانبية (غير معدّلة): Lexer ينهار (`std::out_of_range` من `stod`) على الحرفية الدنيا `5e-324`.

---

## PACKS

<sub>المصدر الأصلي: `CHANGES_PACKS.md`</sub>

### CHANGES_PACKS — حزم المحتوى القابلة للتنزيل

الشرح الكامل: `docs/CONTENT_PACKS.md`.

#### جديد
- `app/src/main/java/com/dlof/rinlang/packs/` — `ContentPack`, `PackStore`, `PackCatalog`, `PackDownloader`, `PackDownloadDialog`, `LanguagePacks`, `PackException`.
- `RinBaseActivity.kt` — قاعدة الشاشات التي تطبّق حزمة اللغة؛ كل `AppCompatActivity()` في المشروع صار `RinBaseActivity()`.
- `ContentPacksActivity.kt` + سطر «الحزم والتنزيلات» في الإعدادات + تسجيل في `AndroidManifest.xml`.
- `content-packs/` (مصادر الحزم) و`scripts/build_content_packs.py` + خطوة نشر في `pages.yml`.
- نصوص `strings_packs.xml` (ar في `values/` و`values-ar/`، en/es داخل حزم اللغة).

#### تغيّر / أُزيل من الـ APK
- نُقل `res/values-en` و`res/values-es` إلى `content-packs/lang/{en,es}` (تُبنى حزماً).
- حُذف `BundledIllustLanguage.kt`؛ `installBundledIllust` يقرأ من حزمة `illust-lang`، و`ProjectsActivity` تنزّلها عند أول مشروع Illust.
- `LanguageActivity` تنزّل حزمة اللغة قبل تطبيقها (ما عدا العربية).
- `app/build.gradle(.kts)`: R8 + shrinkResources، `abiFilters` لـ release، `resourceConfigurations`؛ `proguard-rules.pro` محدّث.

#### لم يُتحقق منه
لم يُبنَ APK هنا (لا Android SDK/NDK في بيئة العمل). تحقق من البناء والتشغيل على جهاز قبل الإصدار.

---

## PERMISSIONS FIX

<sub>المصدر الأصلي: `CHANGES_PERMISSIONS_FIX.md`</sub>

### إصلاح: Unresolved reference 'permissions' (2026-10-04)

**الخطأ (compileReleaseKotlin):**
`RinApplication.kt:15:26 Unresolved reference 'permissions'`

**السبب:** `RinApplication` يستدعي `com.dlof.rinlang.permissions.RinNotifications.createChannels(this)`
لكن الحزمة `permissions` لم تكن موجودة في المستودع (على الأرجح لم تُدمج من rinlang-main-permissions.zip).

**الإصلاح:** إنشاء `app/src/main/java/com/dlof/rinlang/permissions/RinNotifications.kt`
بقنوات: عامة، تنزيلات، مهام. آمن للاستدعاء المتكرر ولا يحتاج إذناً.
بناء الـ Tesseract/JitPack من الإصلاح السابق نجح (وصل البناء إلى compileReleaseKotlin).

---

## PREVIEW

<sub>المصدر الأصلي: `CHANGES_PREVIEW.md`</sub>

### CHANGES_PREVIEW.md — مكتبة previewkit (معاينة حيّة لـ PDF / Markdown / TypeScript)

#### جديد
- `lib/previewkit.og.rin` (+ نسخة مضمّنة في `rin_stdlib_libs.h` فيعمل `@import "lib/previewkit.og.rin"` على أندرويد/ويب بلا ملف على القرص).
- native جديد `zlibInflate(data, maxBytes?)` في `rin_interpreter.cpp`: فك zlib/gzip بلا حجم متوقَّع (ضروري لـ PDF)؛ `nil` عند التلف/تجاوز السقف.
  أُضيف اسمه إلى قوائم المحرّر: `RinSyntax.kt` و`rinc.cpp` (نسختان) و`syntaxes/rin-keywords.json`.
- `docs/previewkit.md`، `tests/previewkit_tests.rin` (22 اختباراً)، `examples/previewkit_demo.rin`، وسطور فهرسة في `docs/README.md` و`docs/standard-library.md` و`README.md`.

#### إصلاحات في ملفات موجودة
- `lib/relyRIN.og.rin` (+ نسخته المضمّنة): أي رابط Markdown `[نص](رابط)` كان يسقط بخطأ «'indexOf' expects 2 argument(s) but got 3» — استُبدل بـ `relyFind`.
- `RinLibrary.kt`: أُضيفت previewkit إلى سجل المكتبات المدمجة في شاشة "المكتبات" في RinStudio (فئة UI).
- **Run يفتح المعاينة تلقائياً**: عند الضغط على Run وفي المحرّر ملف `.md`/`.markdown`/`.pdf`/`.ts`/`.tsx`/`.mts` يُحفظ الملف ثم تُفتح `FilePreviewActivity` (WebView) التي تولّد الصفحة بـ `pvPreview` وتراقب الملف كل ثانية فتُحدَّث عند كل حفظ مع الإبقاء على موضع التمرير. (`FilePreviewActivity.kt` جديد، تعديل `MainActivity.runProgram`، تسجيل في `AndroidManifest.xml`، نصوص `strings_preview.xml` بأربع لغات.)
  المسارات نسبية لجذر المشروع (عزل المحرّك)، والمخرَج في `.rin_preview/` (مخفي من قوائم الملفات: تعديل `ProjectManager.kt`).

---

## PRE POST

<sub>المصدر الأصلي: `CHANGES_PRE_POST.md`</sub>

### CHANGES_PRE_POST — `pre` و`post` (ما قبل / ما بعد): عقد انتقال حول الإسناد

إضافة اختيارية بالكامل (additive) من عائلة المتغيرات الحيّة، فوق `base`/`accept`: كلمتان سياقيتان غير محجوزتين
(`let pre = 1;` و`fun f(post){...}` ما زالا صالحين). الشرح الكامل: `docs/pre-post.md`.

- `pre  x = v if RULE [post RULE2] [strict];` — يُسأل **قبل** الكتابة (x = القيمة القديمة)؛ إن كُذِّب لا تُكتب.
- `post x = v if RULE [pre  RULE2] [strict];` — يُسأل **بعد** الكتابة (x = الجديدة)؛ إن كُذِّب تُلغى الكتابة.
- في الشرطين: `old` = السابقة، `new` = المكتوبة. القيمة الابتدائية لا تُسأل عنها `pre`، وتُسأل عنها `post` (old = new).
- خصائص: `x.pre` `x.post` (آخر محاولة: قبل ← بعد) + `x.ok` `x.accepted` `x.rejected` `x.refused` (مشتركة مع accept) + `x.kind` `x.vitals`.
- الافتراضي صامت (`x.ok` تصير false)؛ `strict` يرمي الكود الجديد `E0051`.
- يعمل مع `inc`/`dec`، والأجراس ترنّ عند النجاح فقط، و`trial/abort` يعيد القيمة والعدّادات.

#### الملفات
**جديدة لهذه الميزة:** `docs/pre-post.md` · `examples/pre_post_demo.rin` · `tests/verification/pre_post_basic.rin|.expected` · `CHANGES_PRE_POST.md`
**معدَّلة:** `app/src/main/cpp/` ← `rin_ast.h` (LiveKind::Pre/Post + preRule/postRule) · `rin_parser.cpp` (التوزيع + `if`/`pre`/`post`/`strict` + رسالة الخطأ) ·
`rin_interpreter.h|.cpp` (حقول LiveMeta، liveGateEval/liveGateRefuse، الحارس في liveAssign، التصريح، الخصائص و vitals) · `diagnostics/diagnostic.h|.cpp` (E0051).
التلوين: `syntaxes/rin-keywords.json` · `syntaxes/rin.tmLanguage.json` (+ `editors/vscode-indsin/...`) · `RinSyntax.kt` · `RinSyntaxHighlighter.kt`.
توثيق: `docs/variables.md` `language-reference.md` `living-variables.md` `ERROR_SYSTEM.md`. لا ملفات جديدة في المحرّك (لا تعديل على CMake/Gradle).

#### ملاحظة دمج
الحزمة **تشمل أيضًا** ملفات `base`/`accept` (`docs/base-accept.md`، الاختبارات، E0049/E0050...) لأن نسخة `rinlang-main_3` لا تحتويها،
و`pre/post` مبنيّ فوقها (نفس الملفات). ضع الحزمة فوق المشروع كما هي.

#### التحقق
بُني CLI على لينكس (g++/CMake) · `tests/rintests.rin` 83/83 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 43/58
(الـ15 الفاشلة نفسها قبل التعديل: `input_*` و`let_err_*`، والاختبار الجديد ينجح) · جُرِّبت أخطاء: غياب الشرط، شرط مكرّر، `else`، `strict` قبل/بعد، قيمة ابتدائية تخالف `post`، trial/abort.
لم يُبنَ APK. `rinc` (Rin → C) لا يدعم الصيغة بعد.
> ملاحظة بناء قديمة غير مرتبطة: `rin_extra_natives7.cpp` يستعمل `unix` كاسم متغيّر فلا يُترجَم بـ `-std=gnu++17` على لينكس (يلزم `-DCMAKE_CXX_EXTENSIONS=OFF`)؛ لم أغيّره.

---
### إضافة: `label` و`define` (تسمية وتعريف)
`label x = v as TITLE [about T] [unit U];` و`define x = v as TEXT [label T] [unit U];` — بيانات وصفية للمتغيّر بلا أثر على الإسناد.
خصائص: `x.label` `x.about` `x.unit` `x.id` `x.info` (+ vitals). الشرح: `docs/label-define.md`.
الملفات: نفس ملفات المحرّك أعلاه (`rin_ast.h` `rin_parser.cpp` `rin_interpreter.h|.cpp`) + التلوين + `docs/label-define.md` + `tests/verification/label_define_basic.rin|.expected`.

---

## PRINT IMAGE

<sub>المصدر الأصلي: `CHANGES_PRINT_IMAGE.md`</sub>

### CHANGES_PRINT_IMAGE — طباعة الصور كنص من داخل اللغة
مبنيّ فوق rinlang-main__23 (قوالب print وخطوطه) + حزمة Unicode/OCR (`:img` و`:ocr` وRinAnsi). لا تعديل على ملفاتها.

- `print.image(path, mode=, cols=, invert=, color=, caption=, if=)` و`imageText(path, cols, mode, invert, color)` تطبعان الصورة كنص (docs/print-image-terminal.md).
  الصيغة القديمة `print.image(path, caption=, width=)` (سطر الأندرويد) لم تتغيّر؛ يكفي ذكر أي من `mode cols invert color` ليُرسم النص.
- أنماط: `blocks`/`half` (نصف كتلة ▀ بألوان 24-بت) · `ansi256` · `shade` · `ascii` · `braille`. بلا ألوان (غير طرفية) يتحوّل blocks إلى shade رمادي نظيف.
- `rin_termimg.h` (جديد): مفكّك PNG (كل الأنماط وأعماق البتات وAdam7 وtRNS) وPPM/PGM وBMP بلا اعتماديات إلا zlib؛ JPEG/GIF/WebP عبر ImageMagick إن وُجد.
- التفكيك في المحلل إلى `print` عادي لنداء `imageText` — لا عقدة AST جديدة، فيعمل على المفسّر وRinVM وقوالب `@tmpl ... print.image(...)`.
- الملفات: جديد `app/src/main/cpp/rin_termimg.h` · `docs/print-image-terminal.md` · `tests/print/print_image_terminal.{rin,expected}` · `tests/print/img/*` ·
  معدَّل `app/src/main/cpp/rin_parser.cpp` (سمات print.image الجديدة) و`app/src/main/cpp/rin_extra_natives5.cpp` (native `imageText`).

---

## PRINT TEMPLATES

<sub>المصدر الأصلي: `CHANGES_PRINT_TEMPLATES.md`</sub>

### CHANGES_PRINT_TEMPLATES — قوالب الطباعة (`@tmpl ... print/style/text`)
إضافة additive لنظام `@tmpl`: أربعة أنواع جديدة — سطر `print`، خطي `print { ... }` (عدة أسطر)، تنسيقي `style` (سمات print قابلة للتركيب)، نصي `text` (تعبير).
لا تغيير في أي صيغة قائمة، ولا في المفسّر أو RinVM: التفكيك يتم في مرحلة التوكنات داخل المحلل.
#### خطوط print (إضافة)
سمات جديدة على `print`: `bold italic underline strike dim reverse blink color bg` (docs/print-fonts.md). تُجمَّع في المحلل بنداء واحد لـ `__fxcode(...)` يعطي أكواد SGR،
ويلفّ المفسّر/RinVM المحتوى بها بعد `upper/width/align` (فلا تنكسر المحاذاة). تُفعَّل فقط على طرفية أو `FORCE_COLOR=1` وتُعطَّل بـ `NO_COLOR`، فلا تتلوّث الملفات ولا كونسول أندرويد.
تعمل داخل قوالب `style` و`print` أيضاً. الملفات الإضافية: `rin_ast.h` (حقل `PrintStmt::fx`) · `rin_interpreter.cpp` · `rin_bcvm_compiler.cpp` · `rin_bcvm_exec.cpp` · `tests/print/print_fonts.*`.
غير منفَّذ: عرض ANSI داخل كونسول أندرويد (RinJobAdapter) — يحتاج تحويل SGR إلى Spans.

#### الملفات
`app/src/main/cpp/rin_parser.cpp` (حقل `kind`/`lines` في `LoopTmpl`، فرع في `tmplParseDef` وآخر في `tmplExpandUse`) · `docs/print-templates.md` · `tests/print/print_templates.rin|.expected`.
#### التحقق (g++ لينكس، -O0)
tests/print 2/2 و tests/loops 11/11 على `rin run` و`rin --vm` (26/26) · rintests على المحرّكين · rintests 83/83 · wesscode 42/42. لم يُشغَّل: tests/verification وبناء APK/الويب.

---

## RIN

<sub>المصدر الأصلي: `CHANGES_RIN.md`</sub>

### تعديلات إصلاح أخطاء لغة Rin

#### المحرك (C++)
- `cli/linux/src/main.cpp`: `runSource` يتحقق الآن من `interp.hadError()` — سابقاً كان `rin run` يعيد 0 و`rin test` يعرض PASS حتى عند فشل التنفيذ.
- `app/src/main/cpp/rin_interpreter.cpp`:
  - تنفيذ فعلي لكامل واجهة `container.*` (exists/kind/size/info/fields/fieldNames/fieldType/snapshot/parent/childrenOf/siblings/ancestors/descendants/path/depth/roots/leaves/byKind/find/findField/set/get/has/renameField/deleteField/mergeFields/clearFields/count/current/stats).
  - تنفيذ واجهة candle كاملة (light/candleExists/candleTargets/candleSources/candleCount/candleInfo/candleRelation/candleByRelation/candleChain/candleDepth/candleRoots/candleLeaves/candleTree/extinguish/extinguishAll).
  - السماح بـ `style` مباشرة داخل `@AUKT`.
  - استيراد نسبي (`./` و`../`) يُجرَّب أيضاً نسبةً إلى مجلد الملف المستورِد.
- `app/src/main/cpp/rin_interpreter.h`: عضو `candleEdges` + `#include <tuple>`.
- `app/src/main/cpp/rin_stdlib_libs.h`: مزامنة النسخة المضمَّنة من `data.og.rin` (range -> sequence). ملاحظة: `@import` يفضّل النسخة المضمَّنة على ملف `lib/` في القرص.

#### المكتبات والأمثلة والاختبارات
- `lib/relyRIN.og.rin`: مزامنة ملف القرص مع النسخة المضمَّنة الفعلية.
- `lib/data.og.rin` و`linguist-submission/samples/{data,math}.og.rin`: إزالة تعارض الاسم مع دوال مبنية (range / lerp).
- `examples/loops_pro.rin`, `tests/verification/loops_pro.rin`: استخدام for/while الحقيقية بدل each/repeat/loop.
- `examples/relyRIN_live_preview.rin`, `examples/relyRIN_indsin_container_demo.rin`: استخدام أسماء relyRIN الفعلية.
- `examples/mask.rin`, `examples/container_indsin_api_demo.rin`, `examples/use_from_demo/*`, `examples/make_unit_real/main.rin`: إصلاح صياغة/تعريفات ناقصة.
- `tests/verification/container_advanced.rin`, `number_builtins.rin`: اختبارات تنشئ بياناتها وتستخدم دوالاً موجودة فعلاً.

#### قيود معروفة (لم تُعدَّل)
- `mask=` على عناصر `@view/@element` لا يُسجَّل في أي سجل أقنعة (يعمل فقط لـ container/group/volume).

---

## RINPAK

<sub>المصدر الأصلي: `CHANGES_RINPAK.md`</sub>

### RinPak — صيغة جديدة لحزم مشاريع Rin (`.rinpak`)

استبدال تصدير `.rinproj` بصيغة احترافية: ZIP بترتيب ثابت (`mimetype` ← `META-INF/manifest.json` ← `content/`) مع SHA-256 لكل ملف وبصمة جذرية. التفاصيل: [`docs/rinpak-format.md`](docs/rinpak-format.md).

#### الملفات
| الملف | التغيير |
|---|---|
| `app/.../RinPak.kt` | **جديد** — كتابة/فحص/فك الحزمة، تحقق SHA-256، **توقيع Ed25519**، بيانات وصفية، حدود وتصلّب أمني |
| `app/.../RinPakSigner.kt` | **جديد** — مفتاح توقيع خاص بالجهاز (يُنشأ عند أول تصدير) |
| `app/.../ProjectManager.kt` | التصدير عبر `RinPak.write`، الاستيراد/المعاينة تكتشف الصيغة (RinPak أو `.rinproj` القديمة) |
| `app/.../ProjectsActivity.kt` | معاينة أغنى، واستقبال `ACTION_VIEW` لفتح `.rinpak` من خارج التطبيق |
| `app/.../RinConsoleFormatter.kt` | `ArtifactKind.RINPAK` (MIME خاص بلا لاحقة `.zip`) |
| `app/.../FilesActivity.kt`، `FileIconResolver.kt` | اسم/أيقونة `.rinpak` |
| `AndroidManifest.xml` | `RinPakFileViewer` (ربط الامتداد) |
| `res/drawable/ic_rinpak_file.xml`، `strings_projects_files.xml` | أيقونة ونصوص |
| `assets/branding/rinpak/` | أيقونات SVG/PNG/ICO/ICNS (ملف + تطبيق) |
| `schemas/rinpak-manifest.schema.json`، `rinpak-signature.schema.json` | مخططا المانيفست والتوقيع |
| `packaging/rinpak/` | ربط الملف على لينكس/ويندوز/macOS |
| `tools/rinpak.py`، `tests/rinpak_test.py` | مرجع بايثون + 28 اختباراً |

**ملاحظة:** لم يُبنَ تطبيق أندرويد في بيئة العمل (لا SDK/Gradle متصل)، فشيفرة Kotlin تحتاج بناءً واحداً للتأكد؛ الصيغة نفسها مُختبرة عبر المرجع البايثوني.


---

#### تحديث: RinPak على الجوال (أندرويد)

الصيغة لم تتغيّر (`specVersion = 1`، ومرجع بايثون والمخططات كما هي). التغييرات في التطبيق:

| الملف | التغيير |
|---|---|
| `RinPak.kt` | `verify()` (تحقق كامل بلا كتابة على القرص)، تقدّم وإلغاء في `extract`/`verify`، تحويل أعطال ZIP إلى رسائل عربية |
| `RinPakTrust.kt` | **جديد** — ثقة الناشرين (TOFU): OWN / TRUSTED / KNOWN / NEW / KEY_CHANGED / SIGNATURE_REMOVED |
| `RinPakActions.kt` | **جديد** — نسخ الحزمة الواردة مرة واحدة، حوار التصدير، المشاركة (`FileProvider`)، حفظ في التنزيلات |
| `ProjectsActivity.kt` | استيراد في خيط خلفي مع تقدّم، معاينة + ثقة، «تحقق فقط»، «استبدال»، استقبال `ACTION_SEND` |
| `ProjectManager.kt` | `importProjectPackage(replaceExisting, onProgress, isCancelled)`، بيانات وصفية للحزمة لكل مشروع، المجلدات المخفية لا تظهر كمشاريع، كنس بقايا الاستيراد |
| `FilesActivity.kt` | زر التصدير يستخدم نفس حوار التصدير/المشاركة |
| `AndroidManifest.xml` | فلتر `ACTION_SEND` (rinpak / octet-stream / zip) على `RinPakFileViewer` |
| `res/xml/file_paths.xml` | `cache/project_exports/` للمشاركة |
| `res/values{,-ar,-en,-es}/strings_rinpak.xml` | **جديد** — النصوص بأربع لغات |
| `app/src/test/.../RinPakTrustTest.kt` | **جديد** — اختبارات منطق الثقة |

**التحقق:** `RinPak.kt` و`RinPakTrust.kt` رُجمتا بـ kotlinc 2.0.21 واختُبرتا على JVM (27 فحصاً: حتمية، تقدّم، إلغاء، عبث/قطع، تشغيل بيني مع `tools/rinpak.py` في الاتجاهين، منطق الثقة)، واختبارات بايثون الـ 28 ما زالت تنجح. أما الأجزاء المرتبطة بواجهة أندرويد (`RinPakActions`، `ProjectsActivity`…) فلم تُبنَ لعدم توفر Android SDK في بيئة العمل — تحتاج بناءً واحداً.

##### إضافة: أيقونة RinPak في الجهاز
- **أيقونة تطبيق مستقلة «RinPak»** في قائمة التطبيقات (`activity-alias` باسم `RinPakLauncher` → `ProjectsActivity`) بأيقونة Adaptive (`mipmap-anydpi-v26/ic_rinpak_launcher.xml`: صندوق/طرد ثلاثي الأبعاد بشريط تغليف أصفر وحرف R على وجهه فوق خلفية خضراء متدرّجة؛ المصدر `assets/branding/rinpak/rinpak_launcher_icon.svg`) مع نسخة أحادية للأيقونات المتجانسة (Android 13+). الضغط عليها يفتح شاشة المشاريع ثم منتقي ملفات الحزم مباشرة.
- **أيقونة الملف `.rinpak`**: تبقى `ic_rinpak_file` على `RinPakFileViewer` (تظهر في قائمة «فتح باستخدام»/المشاركة). ملاحظة: بعض مديري الملفات يرسمون أيقونة الملف بأنفسهم بحسب النوع ولا يسمحون للتطبيقات بتغييرها.

---

### الإصدار 2 — RinPak أقوى + `project.og.urin` موسّع + إكمال تعرّف أندرويد

بُني فوق نسخة الجوال (الثقة `RinPakTrust`، `RinPakActions`، التحقق/الاستبدال، أيقونة المشغّل) دون المساس بها.

#### RinPak (الصيغة نفسها `specVersion=1`، إضافات اختيارية)
- **أيقونة موثّقة**: `icon` صار `{path,size,sha256}`؛ تُتحقق البصمة والحجم (≤512KB، PNG) والموضع (بعد المانيفست/التوقيع مباشرة) ويغطيها التوقيع. المعاينة تعرضها؛ وكل حزمة مصدَّرة تحمل أيقونة (`icon` المشروع أو `res/raw/rinpak_pack_icon.png`).
- **هوية الحزمة**: `id` مستقر و`tags[]`.
- **تحليل أمني** `RinPak.analyze` يظهر في المعاينة: كود أصلي، سكربتات، حزم تطبيقات، انتحال اتجاه النص (RLO)، صلاحيات حساسة، وتوافق `minRinVersion` (الحزمة الأحدث من التطبيق **لا تُستورد**؛ العالية الخطورة تحتاج تأكيداً ثانياً).
- أدوات `tools/rinpak.py`: `verify --require-signature --trust --max-risk --app-version`، `info` يطبع التحليل، `meta` لتحرير `project.og.urin`، و`pack` يقرأه تلقائياً.

#### `project.og.urin` (schema=2) — `RinProjectMeta.kt` (جديد)
- الحقول: `id, name, version, description, author, license, tags, created, modified, minRinVersion, permissions, dependencies, icon` + `extra` للحقول المجهولة؛ قراءة متوافقة مع الصيغة الخام القديمة وv1، وكتابة شبه ذرية، والنوع يُكتب أولاً (توافق التطبيقات القديمة).
- `ProjectManager`: الإنشاء/إعادة التسمية/الاستيراد يحدّثون الملف **دون فقدان بقية الحقول** (كان الاستيراد يعيد كتابته بالنوع فقط)، والمانيفست المتحقَّق منه يتقدّم عند الاستيراد. حوار التصدير الحالي يحفظ الإصدار/المؤلف/الوصف/الرخصة في `project.og.urin` (والملف القديم `rinpak_meta/` احتياطي).
- المواصفة: `docs/project-og-urin.md`.

#### أندرويد
- **`AndroidManifest.xml`** (كان الكود يشير إلى عناصر غير مسجّلة): `RinPakFileViewer` صار يضم `SEND`/`SEND_MULTIPLE` وMIME `application/x-rinpak` ونمط `*.*.*.*.rinpak`، وسُجّل `RinPakLauncher` (أيقونة التطبيق المستقلة).
- `RinPakActions.incomingUri` يعالج `SEND_MULTIPLE` (الملف الأول).
- `FilesActivity`: الضغط على `.rinpak` داخل مشروع يفتح المعاينة/الاستيراد (كان يُفتح كنص خام)؛ `rinpak/rinproj` تُصنَّف ثنائية.
- نصوص جديدة بأربع لغات في `values{,-ar,-en,-es}/strings_rinpak.xml`.

#### قيود صريحة
- **لم يُبنَ تطبيق أندرويد ولم تُترجَم شيفرة Kotlin الجديدة** (لا SDK/kotlinc في بيئتي)؛ روجعت يدوياً وتحتاج بناءً واحداً. القواعد نفسها مُختبرة عبر المرجع البايثوني (`tests/rinpak_test.py`، كلها ناجحة) الذي يطابق `RinPak.kt`/`RinProjectMeta.kt`.
- أيقونة ملف `.rinpak` داخل مدير ملفات خارجي يقررها ذلك المدير؛ التطبيق يسجّل النوع ويقدّم اسم/أيقونة RinPak في «فتح باستخدام» والمشاركة وقائمة التطبيقات.
- التشفير بكلمة مرور لم يُضف.
- `rinlang-time.zip` (ميزة الوقت) لم يُدمج: هو تغيير مستقل لا علاقة له بـRinPak.

---

## RIN AVA

<sub>المصدر الأصلي: `CHANGES_RIN_AVA.md`</sub>

### CHANGES — rin_ava متعدد الخلفيات

rin_ava لم يعد مرتبطاً بـ `rinc` وحده؛ صار سائقاً موحّداً فوق مفسّرات ومترجمات Rin كلها.

#### جديد
- **خلفيات**: `interp` (rin_interpreter.cpp عبر runtime)، `vm` (toolchain/BytecodeVM ببايتكود مُضمَّن)،
  `native` (rinc)، `engine` (بناء المحرّك من المصدر بذاكرة كائنات وتوازٍ)، `go` (مفسّر gorin)، `auto`.
- `-fanalyze` (rin analyze)، `--list-backends`، `-jN`، `--engine-cache`، `--go-runtime`، `--gorin-src`.
- حمولة **v2** (`RINAVAP2`) بحقل نوع (مصدر/بايتكود)؛ v1 يبقى مدعوماً.
- `rin` (cli/linux): ينفّذ الحمولتين (مفسّر أو VM).
- `toolchain`: `readBytecodeFromMemory`/`readBytecodeStream`؛ وإصلاح عرض الثوابت الرقمية في BytecodeVM
  (`print 5;` كانت تطبع `5.000000` مخالفةً للمفسّر).
- `gorin/cmd/rin`: كشف الحمولة المُلحَقة، `--check`، ووسائط البرنامج (`args`)؛ و`rin.Check` و`Interp.SetProgramArgs`.
- CMake: أهداف `rinc` و`gorin-embedded` (اختياري) بجوار `rin`/`rin_ava`.

#### مكتبة rin_vm (صورة VM كاملة)
- `cli/rin_vm/include/rin_vm.h` (header-only): صورة RVM1 = مصدر + كل @import محلولة (+ بايتكود اختياري) + checksum.
- `rin_ava -fvm` صار يبني الصورة (كل اللغة، فوري)؛ `-fvm-bytecode` المسار القديم؛ `-fvm-auto` بايتكود ثم الصورة.
- `Interpreter::setVirtualFiles` في rin_interpreter (يُفحص قبل القرص في @import والاستيراد الانتقائي).
- `rin image prog.rvm`، حمولة `kind='M'`، أداة `rin_vm pack/info/unpack`.

- `rinvm` (`cli/rin_vm/src/rinvm_main.cpp` + هدف CMake): الآلة الافتراضية الكاملة في ملف واحد؛ `rin_ava -fvm` يفضّلها runtime.
- `cli/rin_vm/tools/convert_all.sh`: تحويل مجلد برامج إلى `.rvm` والتحقق من مطابقة المفسّر.

#### ملفات
معدّلة: `app/src/main/cpp/rin_interpreter.{h,cpp}`، `cli/rin_ava/{src/rin_ava.cpp,README.md,tests/run_tests.sh}`، `cli/linux/{CMakeLists.txt,src/main.cpp,src/rin_ava_payload.h}`،
`app/src/main/cpp/toolchain/rin_toolchain.{h,cpp}`، `gorin/rin/interp.go`، `gorin/cmd/rin/main.go`.
جديدة: `cli/rin_vm/{include/rin_vm.h,src/rin_vm_tool.cpp,src/rinvm_main.cpp,tools/convert_all.sh,README.md}`، `gorin/cmd/rin/payload.go`، `CHANGES_RIN_AVA.md`.

#### حدود معلومة
macOS/Windows لم تُغيَّر (rin_ava يعتمد POSIX/fork). `@import` تُضمَّن في vm فقط.

---

## RUN INDICATOR

<sub>المصدر الأصلي: `CHANGES_RUN_INDICATOR.md`</sub>

### مؤشر تحميل لكل نوع أمر + مرتبط بتشغيل كود Rin

يُطبَّق فوق: `rinlang-main_23` + `rinlang-progress-delta` + `rinlang-main_24` (الملفات هنا تتضمّن تعديلات الدلتا السابقة نفسها).

#### الجديد
**1) `RunIndicator` — مؤشر حيّ مرتبط بالتشغيل الفعلي (`loader_ui`)**
`⠋ Running  main.rin · importing rin.ui · Parsing · 12 lines  1.4s   Ctrl-C to stop`
- لا يظهر قبل 0.35s: `1 + 2` أو أي تشغيل سريع لا يرمش أبداً.
- يُرسم فقط والبرنامج صامت (≥150ms منذ آخر مخرجات) ويختفي فور أي سطر مخرجات ثم يعود بعدّاد الأسطر.
- الاستيراد الجاري حقيقي: يتغذّى من أحداث `LoadSession` (المكتبة + المرحلة: Reading/Parsing/Initializing...).
- `input()`: يتوقف المؤشر أثناء السؤال ويعود بعده.
- خيط الرسم لا يستقبل إشارات، فـ Ctrl-C يصل للخيط الرئيسي ويقاطع التنفيذ/القراءة كالمعتاد.
- خارج tty (pipe/ملف/CI)، أو `RIN_PROGRESS=plain`، أو الوضع المضمَّن (أندرويد)، أو stdin ليس tty: لا يُفعَّل أي شيء
  ويبقى الناتج والسلوك القديم حرفياً. `NO_COLOR` يُبقي الحركة بلا ألوان؛ بلا UTF-8 => ASCII.

**2) سبعة أنواع أوامر جديدة في `Task` (الفعل/اللون/الشكل لكل نوع)**
| النوع | الفعل | الأوامر | المؤشر |
|---|---|---|---|
| `Run` | Running | `rin run`، `:run`، `:edit` | RunIndicator (spinner slide) |
| `Load` | Loading | `:load` `:source` `:reload` | RunIndicator (dots) |
| `Eval` | Evaluating | أي شيفرة تُكتب في الطرفية | RunIndicator (arrow) |
| `Project` | Scaffolding | `:new` `:init` | RunIndicator (bounce) |
| `Image` | Rendering | `:img` `:ocr` | RunIndicator (dots) |
| `Files` | Processing | `$ cp mv rm find grep du diff sort ...` | RunIndicator (line) |
| `Hash` | Hashing | `$ sha256 md5 crc32 base64 hexdump ...` | RunIndicator (bounce) |
| `Bench` | Benchmarking | `:time -n N` | شريط حيّ `4/6 runs 67% · ETA · runs/s` |
| `Test` (موجود) | Testing | `:test` | شريط حيّ (جديد في الطرفية) + ناتج الفاشل بعد الشريط |

`taskForCommand(name)` يربط اسم أي أمر بنوعه (غير المعروف => Generic "Working").

#### الربط
- `cli/linux/src/main.cpp` (`rin run`): المؤشر على stderr + stream sink + sink للاستيراد + مزوّد إدخال. مع `--import-progress` يُعطَّل (له شريطه).
  عند تفعيله يصل الناتج تدريجياً (بعد كل عبارة علوية) بدل دفعة واحدة؛ والتشغيل غير الحي كما كان.
- `cli/linux/src/terminal/rin_terminal.cpp`: `print()` المركزية تمرّ عبر المؤشر الحيّ؛ `runParsed` يبني المؤشر بنوع التشغيل
  (`runTask_/runTarget_`)، و`handleCommand` يغلّف الأوامر البطيئة بمؤشر نوعها. `:test` و`:time -n` يرسمان شريطاً حياً فقط على tty.
- `cli/linux/CMakeLists.txt`: `Threads::Threads` لهدف `rinvm` أيضاً (يترجم `library_loader_ui.cpp`).
- المعرض: `loader_ui_demo --run` (محاكاة تشغيل) و`--tasks` (الأنواع الجديدة أيضاً).

#### الملفات
معدَّل: `app/src/main/cpp/loader_ui/{library_loader_ui.h,library_loader_ui.cpp,demo_main.cpp}`،
`cli/linux/src/main.cpp`، `cli/linux/src/terminal/rin_terminal.cpp`، `cli/linux/CMakeLists.txt`.

#### التحقق
- بناء `rin` كاملاً (CMake, g++ 13, C++23) بلا تحذيرات في الملفات المعدّلة.
- `tests/terminal/run.sh`: كل الاختبارات نجحت (الدفعية + pty: Ctrl-C، input()، tab، indsin...).
- اختبار على pty حقيقية (محاكي طرفية): تأخير الظهور، اختفاؤه عند المخرجات، اسم المكتبة أثناء الاستيراد، pause عند `input()`،
  Ctrl-C مع المؤشر ظاهراً، `:load/:run/$ sleep/:test/:time -n/:new`، وخروج نظيف بلا ESC عند pipe و`RIN_PROGRESS=plain`.
- **لم يُجرَّب:** ويندوز/macOS/أندرويد. الوضع المضمَّن (محرّر RinStudio) لا يفعّل المؤشر عمداً (لا tty/تحكم بالمؤشر هناك)؛
  ربطه بمحرّر أندرويد يحتاج جانب Kotlin.

---

## SCOPE GUARANTEES

<sub>المصدر الأصلي: `CHANGES_SCOPE_GUARANTEES.md`</sub>

### CHANGES_SCOPE_GUARANTEES.md — ضمانات النطاق (finally / defer / catch-when / rethrow)

إضافة بحتة (additive): لا تغيير في سلوك أي `try/catch` قديم، والكلمات الجديدة سياقية غير محجوزة.

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_ast.h` | `CatchClause`؛ حقول `catchGuard/moreCatches/finallyBranch/needsInterp/fromDefer` في `TryCatchStmt`؛ `ThrowStmt::rethrow` |
| `app/src/main/cpp/rin_parser.cpp` | تحليل `finally` و`catch … when` و`rethrow` و`defer` (+ `applyDefers` يختزل defer إلى try/finally) |
| `app/src/main/cpp/rin_interpreter.h/.cpp` | تنفيذ finally بكل مسارات الخروج، فلترة catch، `rethrow` (`activeCatches_`)، `e.code` لقواميس `throw` |
| `app/src/main/cpp/rin_bcvm_compiler.cpp` | تفويض العبارات الجديدة للمفسِّر (بلا opcodes جديدة) |
| `app/src/main/cpp/rin_make.cpp`، `toolchain/rin_toolchain.cpp` | المرور على `finallyBranch` وبنود catch الإضافية (+ الحارس في المحلّل الدلالي) |
| `rin_editor_engine.cpp`، `RinRepl.kt`، `RinSyntax.kt` | تلوين/إكمال `finally` `defer` `rethrow` |
| `docs/scope-guarantees.md` | الوثيقة |
| `tests/verification/scope_guarantees.rin/.expected` | الاختبار |

---

## SET

<sub>المصدر الأصلي: `CHANGES_SET.md`</sub>

### CHANGES_SET — `Set` نوع أساسي

نوع جديد `Value::Type::SET` (انظر `docs/set.md`). الاسم العام الوحيد الجديد `Set`؛ الباقي تحت `Set.` فلا يتعارض مع `set()/setAdd()` في boat.

#### الملفات
- جديد: `app/src/main/cpp/rin_set.cpp` (SetData بفهرس تجزئة، المعاملات، ~45 عملية، تسجيل natives)، `docs/set.md`، `tests/verification/set_{basic,ops,mutate}.rin|.expected`.
- `rin_interpreter.h`: `Type::SET`، `SetData`، `setops`، تصريحات `registerNativesSet/tryCallSetMethod`.
- `rin_interpreter.cpp`: valuesEqual/typeName/عرض/طباعة منسّقة/نسخ عميق/schema، المعاملات `+ - * < <= > >=`، `for..in`، النوع `Set`، الطرق النقطية (invokeCallee + MethodCall)، رسالة فهرسة، التسجيل والتضمين.
- `rin_oop.cpp` (`callMethodOn`)، `rin_json.h` (SET ← مصفوفة)، `docs/boat.md` (ملاحظة).

#### تغيير سلوك
`class Set` صار `E0002` (الاسم مأخوذ).

#### التحقق
`rintests` 54/54 · `wesscode_tests` 42/42 · verification 49/50 (الفرق الوحيد `input_forms` قديم) · boat `set()` يعمل. بُني على سطح المكتب (g++)؛ لم يُبنَ APK ولم تُعدَّل ملفات Kotlin (لا تلوين لـ `Set` بعد).

---

## STATS PLOT

<sub>المصدر الأصلي: `CHANGES_STATS_PLOT.md`</sub>

### CHANGES_STATS_PLOT.md — الإحصاء والرسوم البيانية

مبني فوق شجرة scope ← token ← number ← linalg_algo. **لا تغيير في أي دالة قائمة** — مكتبات جديدة بلغة Rin، والتغيير الوحيد في C++ هو الترويسة المولَّدة rin_stdlib_libs.h:

| الملف | التغيير |
|---|---|
| `lib/rinstat.og.rin` | جديد: توزيعات واختبارات وانحدار وPCA وbootstrap (docs/STATS.md) |
| `lib/rinplot.og.rin` | جديد: رسوم SVG/PNG معقّدة (docs/PLOT.md) |
| `lib/rinstat_adv.og.rin` | جديد: logit/ROC/لامعلمي/Fisher/سلاسل زمنية/بقاء/قوة/KDE2 |
| `lib/rinplot_adv.og.rin` | جديد: ROC/KM/ACF/forest/density2d/ridgeline وتقارير |
| `lib/rinauto.og.rin` | جديد: st_auto + pl_auto_report (تقرير HTML آلي ثنائي اللغة) |
| `lib/rinplot.og.rin` | توسيع: طبقتا raster/hrange وycats |
| `docs/STATS.md` `docs/PLOT.md` `docs/STATS_ADV.md` | جديد |
| `examples/auto_report.rin` | مثال التقرير الآلي |
| `tests/verification/stats_adv.rin/.expected` | اختبار محقَّق مقابل SciPy/NumPy |
| `tools/embed_ai_libs.py` `app/src/main/cpp/rin_stdlib_libs.h` | تضمين المكتبات في المفسّر |
| `docs/standard-library.md` | إضافة صفّين |
| `examples/stats_dashboard.rin` | مثال شامل |
| `tests/verification/stats_plot.rin/.expected` | اختبار محقَّق مقابل SciPy |

إعادة استعمال: الدوال الأصلية الإحصائية، `lib/rindf`، `mat*` (LINALG)، `img*`، `color*`، `number*`.
التضمين: أُضيفت المكتبات الخمس (rinstat, rinstat_adv, rinplot, rinplot_adv, rinauto) إلى `tools/embed_ai_libs.py` وولّدتُ `app/src/main/cpp/rin_stdlib_libs.h`؛ فصار `@import` يعمل بلا مجلد `lib/` (وأُدرجت معها rindf/rinml/rinai التي تعتمد عليها ولم تكن مضمَّنة في هذه الشجرة). بعد أي تعديل على هذه الملفات شغّل `python3 tools/embed_ai_libs.py`. هذا تغيير في C++ (الترويسة المولَّدة فقط)، فيلزم إعادة بناء المفسّر.

---

## STYLE

<sub>المصدر الأصلي: `CHANGES_STYLE.md`</sub>

### تطوير ستايل القوائم والصفحة وكتلة الكود

#### MarkdownLite.kt
**القوائم**
- النقطية: علامة تُرسم حسب العمق (دائرة ممتلئة ← حلقة مفرغة ← مربّع مدوَّر) بدل BulletSpan الصغير، مع إزاحة/هامش معلَّق موحَّد بوحدة dp.
- المرقّمة: الرقم يُرسم في هامش ثابت، الأرقام متراصّة على حافة واحدة، والأسطر الملتفّة تتحاذى تحت النص.
- المهام: مربّع اختيار مرسوم (مدوَّر بحدّ للمعلَّق، ممتلئ مع علامة صحّ للمُنجَز) بدل الحرفين ☐/☑.

**كتلة الكود**
- رأس على شريط بلون مختلف مع خط فاصل رفيع (بدل سطر النبضة القديم).
- حشوة أفقية للرأس والجسم، وفراغ علوي/سفلي صغير.
- أرقام أسطر خافتة للكتل من 3 أسطر فأكثر (لا تتكرّر على الأسطر الملتفّة، والنسخ يبقى للكود الخام).
- زوايا وحدّ بقيَم dp، وحدّ متّصل الجوانب بلا خطوط أفقية داخلية (RoundedCardSpan أُعيدت كتابته، وهو يخدم أيضاً الاقتباسات والجداول والتنبيهات).
- لونان جديدان في اللوحتين الفاتحة والداكنة: COLOR_CODE_HEADER_BG وCOLOR_CODE_GUTTER.

#### PackageDetailActivity.kt
- صفحة README بطاقة مدوَّرة بحشوة (خلفية مخصّصة صلبة/متدرّجة أو بطاقة افتراضية بحدّ رفيع تتبع الثيم).
- تباعد أفضل بين المقاطع وبطاقة المعاينة الحيّة.

ملاحظة: لم يُبنَ المشروع ولم يُشغَّل على جهاز/محاكي (لا Android SDK ولا مترجم Kotlin هنا).

#### خريطة الشجرة (Tree Map) — MarkdownLite.kt
كتل `├── └── │` لم تعد تُعرض ككود أحادي المسافة، بل كبطاقة "MAP" مستقلّة:
- **التعرّف**: تلقائياً على الكتل بلا لغة (أو text) التي فيها ≥2 سطر بموصّلات `├`/`└` (أو ≥3 أسطر ASCII بنمط `|--` / `+--`)، أو صراحةً بلغة `tree` / `map` / `structure` / `files`... (كتل الجداول ذات `┼ ┤ ┬` مستثناة).
- **خطوط توجيه مرسومة** (`TreeGuideSpan`) بدل محارف الصندوق: خطوط رأسية ملوَّنة بحسب المستوى (نفس ألوان أعماق القوائم)، وصلة `├` مستقيمة ووصلة `└` بزاوية مدوَّرة، وتستمرّ الخطوط على الأسطر الملتفّة بلا فجوات.
- **أيقونات**: مجلد (ذهبي) / ملف (ملوَّن بحسب الامتداد) / نقطة للعقد الفارغة (هيكل خريطة بلا أسماء).
- **نص**: المجلدات عريضة، امتداد الملف ملوَّن (كود/إعدادات/وسائط/سكربتات/وثائق)، والتعليقات (`# ...`, `// ...`, `←`, `—`) مائلة خافتة.
- **الرأس**: `● MAP` + عدّاد "N مجلد · M ملف" + زر نسخ (ينسخ النص الخام الأصلي).
- يدعم مسافات المستوى 2 أو 4، وأسلوب ASCII، وسطر الجذر، وأسطر الملاحظات داخل الشجرة.
- لونان جديدان في اللوحتين: COLOR_TREE_FOLDER وCOLOR_TREE_FILE.

مثال:
```tree
rinlang/
├── app/
│   ├── src/main/
│   │   ├── java/com/dlof/rinlang/store/
│   │   │   └── MarkdownLite.kt   # المعالج
│   │   └── AndroidManifest.xml
│   └── build.gradle
├── README.md
└── LICENSE
```
ملاحظة: لم يُبنَ الكود ولم يُجرَّب على جهاز (لا Kotlin ولا Android SDK هنا) — تحقّقتُ فقط من الأنماط النمطية (regex) ومن توازن الأقواس.

---

## TABLE

<sub>المصدر الأصلي: `CHANGES_TABLE.md`</sub>

### CHANGES_TABLE — توسعة مفهوم الجدول (بلا دوال جديدة)

المبدأ: **لا اسم دالة جديد في اللغة.** كل شيء أدناه هو تعليم دوال موجودة أن تفهم `@table`.

#### الملفات
- `app/src/main/cpp/rin_table.cpp` (جديد، يُضمَّن في نهاية `rin_interpreter.cpp` مثل `rin_extra_natives*.cpp`): `tableNative` (الخطّاف الموحَّد)، `tableExport`، `tableLoadCsv`، `tableAppendRecords`، `tableOrderRecord`، `tableColumnValues`، `tableCopy/Move/Forget`.
- `rin_interpreter.h` — تصريحات الأعضاء الجديدة.
- `rin_interpreter.cpp` — أعمدة كحقول وكتابة `body`/`columns` في `tableVirtual*`؛ `row` بقاموس يستعمل `tableOrderRecord`؛ `save format=csv|json|md|html|txt`؛ خطّافات `insertDoc updateDoc deleteDoc findDoc queryDocs queryOneDoc docIds allDocs countDocs container.renameField`؛ `destroyContainer` ينظّف حالة الجدول.
- `rin_extra_natives*.cpp` — سطر خطّاف في أول: `container.sum avg min max distinct countBy pluck groupBy sortBy top paginate query search stats toRows toCsv exportCsv toJson fromJson exportToFile importFromFile mergeFrom diff equals checksum contains push pop getOr`؛ و`clone/rename/remove` تنسخ/تنقل/تمسح صفوف الجدول ونمطه (كانت تُفقد).

#### تغيير سلوك واحد مقصود
`container.set(t, "columns"|"body", مصفوفة)` كان `E0004` دائماً (للقراءة فقط)؛ صار مسموحاً. بقي `E0004` لغير المصفوفات. عُدّل سطر واحد في `table_bridge.rin` و`table_sql.rin` ليختبر القيمة غير المصفوفة.

#### التحقق
`tests/verification/table_{bridge,sql,columns,docs,aggregate,export,lifecycle}` ✓ · `tests/rintests.rin` 54/54 · `tests/wesscode_tests.rin` 42/42 · `tests/verification` 43/44 (الفرق الوحيد `input_forms` قديم: اسم الملف في موقع الخطأ) · `tools/test_table|containers|container_sql` تعمل. بُني على سطح المكتب (g++)؛ لم يُبنَ APK.

---

## TEMPLATE

<sub>المصدر الأصلي: `CHANGES_TEMPLATE.md`</sub>

### CHANGES_TEMPLATE — النصوص القالبية `${}`

إضافة بحتة (additive): نصوص بين backtick تقبل `${تعبير}` داخلها.

```kotlin
let name = "ريم";
print `مرحباً ${name}!`;   // مرحباً ريم!
```

#### الملفات المعدَّلة
- `app/src/main/cpp/rin_lexer.{h,cpp}` — `Lexer::scanTemplate()` يحوّل القالب إلى
  `("نص" + #str(expr) + "نص")` قبل المحلل النحوي (لا تغيير في Parser/Interpreter).
- `app/src/main/cpp/rin_interpreter.cpp` — native جديدة `#str` (= `toString`، لا يحجبها متغيّر المستخدم).
- المحرّرات: `RinSyntaxHighlighter.kt` (أندرويد)، `RinTokenizer.cs` (Visual Studio)، `syntaxes/rin.tmLanguage.json`.
- التوثيق: `docs/template-strings.md`، `docs/syntax.md`، `docs/README.md`.
- مثال: `examples/template_strings_demo.rin`. اختبارات: قسم "template strings" في `tests/rintests.rin`.

#### التوافق
backtick كانت خطأ E0011 سابقاً، والنصوص `" "` لم تتغيّر إطلاقاً → لا كسر لأي كود موجود.
أخطاء جديدة: `unterminated template string`، ``empty `${}` in template string``.

#### الإصدار 2 — تنسيق وقيم افتراضية وقيم جاهزة
- `${x:.2f}` `${x:,d}` `${x:05}` `${x:.1%}` `${s:>8}` `${s:<8}` `${s:^8}` `${s:upper|lower|trim}` → native `#fmt`.
- `${a ?? b}` → native `#default` (يُستبدل فقط عند nil).
- `${#date}` `#time` `#datetime` `#year` `#month` `#day` `#hour` `#minute` `#second` `#weekday` `#version` `#line` `#file`
  → native `#now` (أو قيمة ثابتة في Lexer لـ `#line`/`#file`).
- Lexer يميّز `:` التنسيق عن `:` الشرط الثلاثي والقاموس، و`??` عن `?` المفردة.
- اختبارات إضافية: 9 في `tests/rintests.rin` (المجموع 73 ناجحة).

#### الإصدار 3 — أقوى وأوسع
- **تنسيق متقدّم:** حشو بأي حرف `*^10`، إشارة `+d`، فاصل `_`، أنواع `e x X b o s`، قصّ النص `.3`،
  كلمات `title cap ar`، وتتابع مراحل بـ `|` (مثل `trim|upper|>6`).
- **تنسيق ديناميكي متداخل:** `${x:.${n}f}` — `Lexer::scanTemplateParts('}')` يعالج المواصفة كقالب.
- **وضع التصحيح:** `${x=}` ⟶ `x=42`.
- **`\u{hex}`** داخل القوالب.
- **`render(template, data)`:** قوالب وقت التشغيل (مسارات `a.b.0`, `??` افتراضي، تنسيق، `{{}}`، `$${` للهروب)،
  آمنة (لا تنفّذ كوداً). المنطق في `templateFormat()`/`templateRender()` أعلى `rin_interpreter.cpp`.
- اختبارات إضافية: 10 → المجموع 83 ناجحة.

#### الإصدار 4 — القوالب داخل مفاهيم اللغة
- **`translation`**: `lang` و`text` يقبلان قوالب ديناميكية تُقيَّم وقت التنفيذ (`TranslationStmt::langExpr/textExpr`).
- **`emit` و`on.event`**: اسم الحدث قالب ديناميكي (`EmitStmt/EventHandlerStmt::nameExpr`). التمييز عن دالة عادية اسمها
  `emit` عبر علم `Token::tpl` على ( الافتتاحية للقالب (يضعه `Lexer::scanTemplateParts`).
- **قالب بلا `${}`** صار رمز STRING واحداً، فيصلح في كل المواضع الثابتة (`@import`، `link.id`، `.object("id")`...)
  ويدعم أسطراً متعددة وعلامات `"`.
- **`${}` في موضع ثابت** يعطي خطأً واضحاً بدل رسالة عامة (`Parser::consumeStaticText`).
- خصائص `@view`/`@element` و`text x = ...` كانت تقبل القوالب أصلاً (تعبيرات) — موثَّقة الآن.
- ملفات: `rin_common.h` (Token::tpl)، `rin_ast.h`، `rin_parser.{h,cpp}`، `rin_lexer.cpp`، `rin_interpreter.cpp`،
  اختبار جديد `tests/template_concepts.rin`.

---

## TERMINAL

<sub>المصدر الأصلي: `CHANGES_TERMINAL.md`</sub>

### Terminal داخل المحرر + الإدخال داخله

#### ما الجديد
- **طرفية حقيقية في ذيل المحرر** (تبويب `Terminal`، وهو الافتراضي؛ التبويب الثاني `Runs` يحتفظ ببطاقات الطابور القديمة كما هي):
  مخرجات حيّة مُلوَّنة بحسب نوع السطر (خطأ/نجاح/حاوية…)، سطر `── run #N ──` لكل تشغيل، وسطر حالة في النهاية (`✓ finished in … ms` / `✗ failed after …`).
- **الإدخال داخل الـ terminal بدل النافذة المنبثقة:** عندما يستدعي برنامج `input` / `inputNumber` / `confirm` / `choose` يظهر السؤال في سجل الـ terminal، ويُضيء سطر الإدخال في الأسفل (`›`)؛
  جوابك يظهر بعد السؤال على السطر نفسه (بلون مميّز). زر `^C` يلغي السؤال (يتوقف البرنامج بخطأ «تم إلغاء الإدخال»). Enter على سطر فارغ يُبقي القيمة الحالية (سلوك المحرك كما هو).
- **سطر أوامر عندما لا ينتظر شيء** (`$`): `run` · `clear` · `history` · `version` · `help`، وأي نص آخر يُنفَّذ كشيفرة Rin (تُضاف `;` إن لزم)، مثل `print 1 + 2;`.
  أزرار ↑/↓ لتاريخ الأوامر. كل أمر يُنفَّذ كتشغيل مستقل (المحرك لا يحتفظ بحالة بين التشغيلات).
- زر **Copy** ينسخ كل محتوى الـ terminal، وزر **Clear** يمسحه (في تبويب Runs يمسح الطابور كما كان).
- النافذة المنبثقة القديمة تبقى **احتياطاً فقط** حين يكون الكونسول مخفياً (إعداد «إظهار الطرفية» معطّل أو تخطيط «تركيز»).

#### الملفات
- جديد: `RinTerminal.kt` (سجل `RinTerminalBuffer` الخالص + singleton `RinTerminal`)، `RinTerminalView.kt`، `res/values*/strings_terminal.xml` (en/ar/es).
- معدَّل: `RinJobScheduler.kt` (4 نقاط ربط: بدء، مخرجات حيّة، سؤال/جواب، ختام)، `MainActivity.kt` (التبويبان + presenter الإدخال)، `activity_main.xml`.
- لم يتغيّر: المحرك C++، `RinInputBridge.kt`، `RinEngine.kt`، `RinJobAdapter.kt`.

#### ملاحظات تصميم
- السجل في singleton يعيش أطول من الـ Activity، فلا يضيع النص ولا السؤال المعلّق عند تدوير الشاشة (`RinInputBridge` يعيد عرض السؤال على الواجهة الجديدة).
- ترتيب «مخرجات ← سؤال ← جواب» مضمون لأن المحرك يُفرغ كل ما طُبع قبل أن يسأل (`askRaw`).
- مهلة التنفيذ (15 ث) لا تُحتسب أثناء انتظارك للإجابة (كما كان).
- التشغيل التلقائي (Auto-run) والتشغيل اليدوي يكتبان في الـ terminal؛ «مسح الكونسول عند التشغيل» يمسحه أيضاً.

#### اختبارات / تنبيه
`app/src/test/.../RinTerminalBufferTest.kt` (JUnit، بلا JNI). **لم تُترجَم الشيفرة ولم تُشغَّل الاختبارات في بيئة الإنشاء** (لا مترجم Kotlin/Android هناك): شغّل `./gradlew testDebugUnitTest` ثم ابنِ التطبيق وجرّب على جهاز قبل الاعتماد عليها.

---

### تحديث: REPL حقيقي + terminal أقوى

#### ما الجديد
- **جلسة REPL** (`RinRepl.kt`): التصريحات الناجحة (`let`/`fun`/`class`…) تبقى بين الأوامر، والتعبير بلا `;` (مثل `1 + 2`) يُطبع ناتجه، والأمر ناقص الأقواس (`fun f() {`) يكمل على أسطر `…`. أوامر الـ REPL بلا ترويسة `run #N` ونجاحها صامت (الفشل يظهر دائماً).
- **`stop` / ^C يوقفان التشغيل الجاري فعلاً** (ويسحبان ما ينتظر في الطابور). المحرك الأصلي لا يقبل المقاطعة، فيُعلَّم التشغيل `CANCELLED` فوراً ويُترك خيطه يكمل وحده ويُتجاهل ما يبثّه (نفس عقد المهلة).
- **النقر على `[Error line N]` يقفز إلى السطر N في المحرر**، وأمر `goto N` يفعل الشيء نفسه. أخطاء أوامر الـ REPL لا تُربط بالمحرر عمداً (أرقام أسطرها تُحسب بعد prelude الجلسة).
- **`!!` و`!N`** لإعادة آخر أمر أو الأمر رقم N من `history`.
- **تاريخ الأوامر محفوظ بين تشغيلات التطبيق** (آخر 100 أمر).
- **`grep <نص>`** بحث في سجل الشاشة، **`session`/`reset`**، **`share`/`copy`**، قرص بإصبعين لتغيير الخط، وإكمال ⇥.
- زر ^C يتلوّن بالأحمر أثناء تشغيل جارٍ.

#### إصلاح في `RinRepl.kt` (من الملف الذي رفعتَه)
إعادة تصريح متغير (`let x = 10;` بعد `let x = 1; x = x + 1;`) كانت تُبقي التعيين القديم `x = x + 1` في الـ prelude فيُعاد تطبيقه فوق القيمة الجديدة فتصير `11`. صار التصريح الجديد يُسقط تعييناته القديمة؛ وإن كان التصريح الجديد غير نقي (مثل `input`) يُحذف القديم أيضاً بدل أن يبقى مضلِّلاً.

#### الملفات
- جديد: `RinRepl.kt`، `RinReplSessionTest.kt`
- معدَّل: `RinTerminal.kt` (`TermRole.CONT`، `TermLine.errorLine`، `echo`/`remember`/`grep`/`restoreHistory`، `RinTerminal.repl`)، `RinTerminalView.kt`، `RinJob.kt` (`repl`, `stopRequested`)، `RinJobScheduler.kt` (`submit(source, repl, onFinished)`, `stopAll()`, `isRunning()`)، `MainActivity.kt` (ربط `onExecuteRequested`/`onGoToLine`)، `strings_terminal.xml` ×4، `RinTerminalBufferTest.kt`.

#### تنبيه
لم تُترجَم الشيفرة ولم تُشغَّل الاختبارات هنا (لا يوجد مترجم Kotlin/Android في بيئة العمل؛ راجعتُها يدوياً: توازن الأقواس، وكل مفاتيح `R.string` موجودة في الملفات الأربعة). شغّل `./gradlew testDebugUnitTest` ثم جرّب على جهاز.

---

## TERMINAL ANDROID

<sub>المصدر الأصلي: `CHANGES_TERMINAL_ANDROID.md`</sub>

### ربط طرفية Linux (rin terminal) بمحرّر RinStudio (أندرويد)

**C++ (مشترك):** `cli/linux/src/terminal/rin_terminal.{h,cpp}`
- `Session` صار قابلاً للتضمين: المخرجات عبر `outSink_` بدل stdout، و`input()` عبر callback، وإدخال سطر بسطر (`feedLine`) بنفس منطق `--batch`.
  مجلد العملية يُحفظ ويُستعاد حول كل سطر، فـ`:cd` يغيّر مجلد الجلسة فقط.
- الصنف العام `rin::terminal::EmbeddedTerminal` (في الهيدر). في التضمين تُحجب `:sh :edit :pkg :indsin` برسالة. `:pkg` يُستبعد بالعلم `RIN_TERMINAL_EMBED`.
- سلوك `rin terminal` على لينكس لم يتغيّر (كل اختبارات `tests/terminal/run.sh` ناجحة).

**أندرويد:**
- `app/src/main/cpp/terminal/rin_terminal_jni.cpp` (جديد): جسر JNI (create/feed/cancelPending/continuing/variables/destroy).
- `app/src/main/cpp/CMakeLists.txt`: يضيف `rin_terminal.cpp` وجسر JNI إلى `rinengine` مع `RIN_TERMINAL_EMBED`.
- `RinNativeTerminal.kt` (جديد): خيط عامل واحد، ويكتب في `RinTerminal` عبر نفس مسار المخرجات والإدخال المستعمل في التشغيل العادي.
- `RinTerminalView.kt`: أوامر `:` (وكل شيفرة بعد `native on`) تذهب للجلسة الأصلية؛ `reset` و`session`/`vars` و`^C` و`help` محدَّثة؛ أمر جديد `native on|off`.
- `res/values{,-ar,-en,-es}/strings_terminal_native.xml` (جديد): نصوص المساعدة والرسائل.

**ما تحقّقتُ منه فعلاً:** ترجمة `rin_terminal.cpp` على لينكس بالنسختين (عادية و`-DRIN_TERMINAL_EMBED`) بلا تحذيرات؛ تشغيل `EmbeddedTerminal` على لينكس
(حالة دائمة، أسطر متعددة، `:vars :type :load :cd :pwd :ocr :img :reset`، `input()`، حجب `:sh/:edit/:pkg/:indsin`، نص عربي وإيموجي، مجلد العملية لا يتغيّر)؛
فحص نحوي لملف JNI مقابل `jni.h` وهمي.
**ما لم أستطع التحقق منه (لا NDK ولا kotlinc ولا جهاز هنا):** بناء أندرويد (CMake/NDK)، ترجمة ملفي Kotlin، وتجربة الواجهة على جهاز. راجع الفحوص أدناه عند أول بناء.

#### الدمج مع النسخة الأحدث (rinlang-main_25)
- دُمجت تعديلات التضمين فوق `rin_terminal.cpp` الأحدث مع الإبقاء على إطار `:ocr` الملوّن (Unicode) وأمر `ocr` في `RinTerminalView.kt` وعرض ألوان ANSI (`RinAnsi`).
- في وضع التضمين تُفعَّل ألوان ANSI (256 لوناً) وUnicode دائماً (لا tty ولا locale على أندرويد)، فتظهر ألوان `:ocr` و`:img` الأصلية في الطرفية.
- `ocr` (أمر RinStudio القديم) يبقى يعمل في الـ REPL العادي؛ `:ocr` يعمل في الجلسة الأصلية.
- الاختبار: ترجمة `rin_terminal.cpp` بالنسختين (عادية و`-DRIN_TERMINAL_EMBED`) بلا تحذيرات. لم يُبنَ أندرويد هنا (لا NDK/kotlinc).

#### تطوير ستايل الطرفية (RinTerminalView)
- شريط حالة علوي: نقطة ملوّنة + READY / RUNNING / INPUT / MULTI-LINE، وعلى اليمين الوضع (REPL أو NATIVE) وعدد الأسطر وحجم الخط.
- موجّه `❯` (بدل `$`) مع خلفية خفيفة لكل أمر تفصله عن مخرجاته وسطر فارغ بين الأوامر؛ النسخ/المشاركة تبقى بصيغة `$ ` النصية.
- أسطر النجاح `✔` والخطأ `✖` والنظام بخط مائل خافت؛ شاشة ترحيب بعنوان `Rin ▸ terminal`.
- أزرار الأدوات بتأثير لمس (Ripple) وتباعد أسطر أوضح. لم يُترجَم Kotlin هنا (لا kotlinc) — راجعه عند أول بناء.
- أزرار سريعة في شريط الحالة: ↓ للأسفل · ⎘ نسخ · ⇪ مشاركة · ⌫ مسح؛ ولا يتكرر رمز ✓/✗ في أسطر الحالة.

#### محاكاة terminal حقيقي
- موجّه shell كامل `rin@studio:<المجلد>$` (مستخدم أخضر، مجلد أزرق) في سطر الإدخال وفي صدى كل أمر، وموجّه المتابعة `>` كـ PS2 في bash؛ أُزيلت خلفية الأوامر.
- سطر تنفيذ حيّ فوق الإدخال أثناء أي تشغيل (REPL أو الجلسة الأصلية): مؤشر Braille دوّار + الزمن المنقضي `⠹ Running… 2.4s`، يظهر ويختفي تلقائياً.

---

## TERMINAL BRAND

<sub>المصدر الأصلي: `CHANGES_TERMINAL_BRAND.md`</sub>

### Rin Shell + Rin Toolbox — الأسماء الرسمية والهوية البصرية

#### الأسماء الرسمية
| الاسم | ما هو |
|---|---|
| **Rin Shell** | الطرفية التفاعلية للغة Rin (الأوامر `:` و`$`، وتنفيذ شيفرة Rin مباشرة) |
| **Rin Toolbox** | مكتبة الأوامر المدمجة في Rin Shell — 615 أمراً (`$ cmd … ~/`) في 13 عائلة |

الاسمان علامتان ولا يُترجمان (في الواجهة العربية والإسبانية تبقى «Rin Shell» و«Rin Toolbox»).

#### الأيقونات
- `assets/branding/shell/rin-shell.svg` (+ PNG: 48 / 192 / 512): نافذة طرفية داكنة بنقاط التحكم الثلاث، علامة `❯` بنفسجية (لون Rin) ومؤشر أخضر.
- `assets/branding/shell/rin-toolbox.svg` (+ PNG): صندوق أدوات بنفسجي يحمل `>_`.
- أندرويد: `drawable/ic_rin_shell.xml` و`drawable/ic_rin_toolbox.xml` (vector). أيقونة Rin Shell تظهر الآن بجانب تبويب الطرفية في `activity_main.xml`.
- لينكس: `packaging/linux/rin-shell.desktop` + `rin-shell.svg` (+ README بأمرَي التثبيت).

#### الأيقونة المرسومة داخل الطرفية
`cli/linux/src/terminal/rin_terminal_brand.h`: الشعار نفسه 24×24 بكسل يُطبع بحروف نصف-كتلة (▀ ▄ █) بنقطتين لكل حرف = 12 سطراً × 24 عموداً، بألوان xterm-256 (ويتراجع إلى 16 لوناً حسب `TERM`). بلا ألوان أو بلا UTF-8: شعار نصي صغير (Unicode أو ASCII).

#### التصميم
- **ترويسة جديدة** عند الإقلاع: الشعار يساراً، ويمينه: الاسم والإصدار، الوصف، خط فاصل، `Toolbox` (عدد الأوامر)، `Shell` (الأوامر الأهم)، `Keys`، `Session` (السمة والمجلد الحالي). الطرفية الضيقة (أقل من 68 عموداً) تعرض ترويسة مضغوطة بلا شعار.
- عنوان نافذة/تبويب الطرفية = **Rin Shell** (OSC 0)، فقط في طرفية تفاعلية بألوان.
- **`:about`** — الشعار + الاسم والإصدار والألوان وUnicode.
- **`:toolbox`** — جدول العائلات (الاسم، العدد، أمثلة)؛ `:toolbox math` يعرض كل أوامر العائلة بأعمدة منسّقة؛ البادئة تكفي، والاسم الغامض (`dat`) يُرفض مع القائمة.
- أندرويد: شاشة الترحيب «Rin Shell» وسطر التعريف (`terminal_welcome` بالأربع لغات) وتسمية التبويب.

#### الملفات
جديد: `rin_terminal_brand.h`، `assets/branding/shell/*`، `drawable/ic_rin_{shell,toolbox}.xml`، `packaging/linux/*`.
معدَّل: `rin_terminal.cpp`، `rin_terminal_xcmds7.h` (عائلات Toolbox)، `activity_main.xml`، `strings_terminal.xml` ×4، `RinTerminalView.kt`، `tests/terminal/xcmds4_session.{in,expected}`، `docs/terminal.md`.

#### التحقق
ترجمة بلا تحذيرات؛ `tests/terminal/run.sh` كاملاً ناجح (22)؛ صوّرتُ الترويسة و`:about` و`:toolbox` من طرفية pty حقيقية (256 لون). **لم يُجرَّب:** بناء أندرويد (لا NDK) — تغيير `ImageView` والموارد بسيط لكن راجعه عند أول بناء.

---

## TERMINAL CLI

<sub>المصدر الأصلي: `CHANGES_TERMINAL_CLI.md`</sub>

### طرفية Rin الكاملة لسطر الأوامر

- جديد: `cli/linux/src/terminal/rin_terminal.{h,cpp}` (طرفية كاملة، انظر `docs/terminal.md`) تحل محل REPL السطر الواحد.
- معدَّل: `cli/linux/src/main.cpp` (`rin` بلا وسائط + `rin terminal|repl|shell`)، `cli/linux/CMakeLists.txt`، `cli/linux/README.md`.
- إصلاح في المحرك `app/src/main/cpp/rin_interpreter.cpp`: بداية `run()` تصفّر `lastDiagnostic_/lastErrorMessage_/lastErrorLine_`؛ كانت حالة الفشل تبقى عالقة فيُبلَّغ كل تشغيل لاحق على نفس الكائن أنه فاشل. (ومخرجات REPL القديم كانت ستتكرر لأن `run()` تعيد كل المخرجات المتراكمة؛ الطرفية الجديدة تستخدم stream sink.)
- جديد (هذه الدفعة): **مشاريع** (`:new/:init/:project/:test/:tree/:run`) و**indsin داخل الطرفية** (`:indsin` و`rin indsin`): عرض بنصف-كتل + فأرة + تحميل حيّ، عبر `rin_indsin_session_*` نفسها. CMake يضيف `rin_c_api.cpp` و`indsin/rin_indsin_c_api.cpp` لهدف `rin`.
- مبنيّ فوق أرشيف rinlang-main__1_ (فيه دعم الوسائط في indsin؛ طلباته تُعرض كرسالة في الطرفية).
- اختبارات: `tests/terminal/` (دفعي + pty).
- التحقق هنا: ترجمة g++ 13 بلا تحذيرات (-Wall -Wextra)، `rintests` 83/83، `wesscode_tests` 42/42، `tests/terminal` كلها ناجحة. لم يُجرَّب بناء CMake (غير مثبّت هنا؛ رُجمت الملفات يدوياً بنفس القائمة) ولا macOS/Windows/Android.

---

## TERMINAL IMG

<sub>المصدر الأصلي: `CHANGES_TERMINAL_IMG.md`</sub>

### `:img` — عرض صورة بحروف Unicode في الطرفية

- جديد: `:img <صورة> [--mode half|braille|shade|ascii] [--width N] [--plain] [--invert]` داخل `rin terminal`، و`rin img <صورة> [...]` من الشل.
- معدَّل: `cli/linux/src/terminal/rin_terminal.{h,cpp}` (كتلة مستقلة قبل `IndsinArgs` + `cmdImg` + `runImgCommand` + سطر `:help` + إكمال Tab)،
  `cli/linux/src/main.cpp` (الأمر الفرعي `img`)، `docs/terminal.md`، `tests/terminal/run.sh` (قسم img). لا تلمس `:ocr` ولا المحرّك.
- (تحديث: صار الفكّ بمفكّك مدمج — انظر CHANGES_TERMINAL_IMGDEC.md؛ ImageMagick احتياطي لـ webp/tiff فقط) فكّ الصورة عبر ImageMagick بمصفوفة argv مباشرة (بلا shell)، والصيغة تُعرَّف من البايتات (png/jpg/gif/bmp/webp/tiff) فتُرفض SVG/MVG المتنكّرة؛
  مهلة 30/60 ثانية، وسقف للحجم (400 عمود، 1600 صف بكسل) ضد الصور الضخمة.
- التحقق: ترجمة g++ بلا تحذيرات (-Wall -Wextra)، وتجربة فعلية لـ png/jpg/gif/RGBA/مسار بمسافة/صورة طويلة جداً/صغيرة جداً/أخطاء.
  لم يُجرَّب: عرض نمط half بألوان 24-bit على طرفية حقيقية (اختُبر خرج ANSI فقط)، CMake، macOS/Windows.

---

## TERMINAL IMGDEC

<sub>المصدر الأصلي: `CHANGES_TERMINAL_IMGDEC.md`</sub>

### `:img` بلا ImageMagick — مفكّك صور مدمج

**الهدف:** `:img` / `rin img` لا يعتمدان على أداة خارجية، فيعملان على أندرويد (RinStudio) وعلى لينكس بلا تثبيت أي شيء.

- **جديد** `cli/linux/src/terminal/rin_imgdec.h` — header-only، يعتمد على zlib فقط (مربوط أصلاً في CMake لينكس وأندرويد، فلا تغيير في CMake):
  PNG (كل أنماط الألوان، أعماق 1–16 بت، Adam7، tRNS) · JPEG (Baseline + Progressive، 4:4:4/4:2:2/4:2:0، فواصل restart، تدوير EXIF، استيفاء خطي للألوان) ·
  GIF (الإطار الأول، شفافية، interlace) · BMP (1/4/8/16/24/32 بت). مصغِّر بمتوسط المساحة مع دمج الشفافية على الأبيض.
- **حدود الذاكرة (للهاتف):** PNG/GIF/BMP حتى 25 ميغابكسل؛ JPEG فوق 12 ميغابكسل يُفكّ بمقياس 1/8 (DC فقط) فتُقرأ صور الكاميرا الضخمة بذاكرة صغيرة؛ قنبلة PNG تُرفض فوراً.
- **معدَّل** `rin_terminal.cpp`: `loadImagePixels` يجرّب المدمج أولاً؛ ImageMagick احتياطي لـ webp/tiff فقط (أو ملف غريب فشل المدمج في فكّه). بدونه تظهر رسالة واضحة.
- **معدَّل** `EmbeddedTerminal::setColumns` + JNI `nativeSetColumns` + `RinNativeTerminal.columns` + `RinTerminalView.measureColumns()`:
  عرض `:img`/`:ocr` الافتراضي يتبع عرض شاشة الهاتف الفعلي (كان 80 عموداً فيلتفّ السطر).
- أُعيد حارس `:indsin` داخل المحرر (`cmdIndsin`) بعد أن غاب من النسخة المرفوعة (يحتاج tty).
- **اختبارات:** قسم img في `tests/terminal/run.sh` صار لا يشترط ImageMagick ويثبت الرسم مع `PATH` فارغ؛ أداتا `imgdec_dump.cpp` و`imgdec_compare.py` لمقارنة المفكّك مع PIL.

#### ما تحقّقتُ منه فعلاً
- **دقة:** 41 صورة اختبار (كل أنماط PNG بما فيها 16 بت وAdam7 وpalette/tRNS، GIF عادي/شفاف/interlaced، BMP بكل الأعماق، JPEG أساسي/تدريجي/رمادي/تقليل عينات/restart/EXIF) مقابل PIL:
  PNG وGIF وBMP **متطابقة بايتاً ببايت**؛ JPEG متوسط الخطأ ≤ 0.1 من 255 (أقصاه 4).
- **متانة:** 410 ملفاً مقطوعاً/تالفاً عشوائياً تحت ASan+UBSan بلا أي خطأ ذاكرة أو سلوك غير معرَّف؛ قنبلة PNG (60000×60000) تُرفض فوراً.
- **أداء (-O2، لينكس):** JPEG 12 ميغابكسل كامل ≈ 0.95 ث؛ 20 ميغابكسل بمقياس 1/8 ≈ 0.05 ث.
- `:img` يعمل مع `PATH` خالٍ من ImageMagick في `rin img` وفي `EmbeddedTerminal` (مسار RinStudio)، وكل اختبارات `tests/terminal/run.sh` (20) ناجحة.
- **لم أستطع:** بناء أندرويد (NDK) ولا ترجمة Kotlin، ولا قياس السرعة على جهاز فعلي (الهاتف أبطأ من هذا القياس). WebP/TIFF غير مدعومين مدمجاً.

---

## TERMINAL OCR

<sub>المصدر الأصلي: `CHANGES_TERMINAL_OCR.md`</sub>

### `:ocr` — تنسيق الخرج (فوق تحديث `:img`)

- مبنيّ فوق أرشيف rinlang-main__22 (الذي فيه `:img` و`:ocr` الأوليان). لا أمر `:image` ثانٍ: رسم الصورة يبقى لـ `:img` الموجود.
- معدَّل: `cli/linux/src/terminal/rin_terminal.cpp` (فرع الطباعة في `cmdOcr` فقط)، `docs/terminal.md`.
- الشكل: إطار `┌─ اسم الملف ─ … │ … └─ الثقة · الكلمات · اللغة`، التفاف بـ `wrapText` الموجودة (يحفظ الفقرات والأسطر الفارغة)، وASCII احتياطاً عبر `sym()`. لا دوال أو مفاهيم جديدة.
- التحقق: بناء بلا تحذيرات، `tests/terminal/run.sh` كلها PASS (بما فيها قسم img)، وتجربة `:ocr` بعرض 40 عموداً وبلا tty، و`:img` ما زال يعمل. العربية لم تُجرَّب (حزمة ara غير مثبّتة هنا).

---

## TERMINAL PROGRESS

<sub>المصدر الأصلي: `CHANGES_TERMINAL_PROGRESS.md`</sub>

### مؤشر التحميل في الطرفية (Progress) — احترافي وحقيقي

#### ما تغيّر
`app/src/main/cpp/loader_ui/library_loader_ui.{h,cpp}` أُعيدت كتابته، ويشمل:

**1) `ConsoleBarSink` (شريط `@import`، يُفعَّل بـ `rin run file.rin --import-progress`)**
- طرفية حقيقية: سطر حيّ متحرّك: spinner بريلي + شريط ناعم بدقة نصف خانة (`━╸─`) بلون Rin البنفسجي + نسبة + مرحلة + مدة.
- شجرة التبعيات بأي عمق (`├─ └─ │`) تتحدّث في مكانها، ثم يُستبدل الكل عند الانتهاء بملخص نظيف:
  `✔ rin.ui  loaded · 724ms` مع الشجرة النهائية. الفشل: `✘ name  failed: …` بالأحمر. المكرَّر: `↺ already imported`.
- pipe / ملف / CI: وضع نظيف تلقائياً (سطر لكل حدث، بلا ESC ولا `\r`).
- كان الحد السابق «شجرة بعمق واحد فقط» — أُزيل.

**2) `ProgressBar` الجديد (عام لأي عملية متعددة الخطوات)**
`Installing  ━━━━━╸─────  2/5  40%  http 1.2.0 · verifying  1.4s`
خيط خفيف يحرّك الـ spinner والمدة كل 80ms حتى أثناء التنزيل الحاجب. `note()` تطبع سطراً دائماً فوق الشريط.

**3) ربطه بـ RinPM (`rin pkg install/add/update`)**
`installResolvedPackage(..., onPhase)` يبلّغ المراحل الفعلية بالترتيب الحقيقي: `cached → downloading → verifying → unpacking`.
العدّاد = حزم أُنجزت فعلاً؛ لا أنيميشن وهمي ولا نسب مخترعة.

#### الكشف التلقائي (`detectTermCaps`)
| الشرط | النتيجة |
|---|---|
| ليس TTY | وضع نظيف |
| `NO_COLOR` أو `TERM=dumb` | بلا ألوان (`CLICOLOR_FORCE=1` يفرضها) |
| لغة صريحة غير UTF-8 أو `TERM=linux` | رموز ASCII (`[=====>---]`, `+`, `x`) |
| `RIN_PROGRESS=plain` / `fancy` | تعطيل / فرض الأنيميشن |
| العرض | `ioctl(TIOCGWINSZ)` ← `COLUMNS` ← 80؛ لا يلتف السطر أبداً (اقتطاع بـ `…`) |
ويندوز: تفعيل VT تلقائياً + UTF-8.

#### الملفات
معدَّل: `loader_ui/library_loader_ui.{h,cpp}`، `loader_ui/demo_main.cpp`، `cli/linux/src/pkg/{cli_pkg.cpp,package_ops.h,package_ops.cpp}`، `cli/{linux,macos,windows}/CMakeLists.txt` (Threads).
واجهة `ConsoleBarSink(out, barWidth, useCr)` القديمة ما زالت تعمل (`useCr=false` ⇒ وضع نظيف).

#### التحقق
- بناء `rin` على لينكس كاملاً؛ تشغيل `rin run … --import-progress` و`rin pkg add/install` على سجل محلي حقيقي.
- صوّرتُ الإخراج من pty حقيقية (256 لون، عرض 100 و42 عموداً) عبر محاكي طرفية: الشريط، الشجرة المتحدّثة، الملخص النهائي، الاقتطاع.
- ترجمة الملف بـ `-D__EMSCRIPTEN__` (بلا خيوط) سليمة.
- **لم يُجرَّب:** ويندوز/macOS/أندرويد (لا بيئة بناء هنا) — كود ويندوز (VT/UTF-8) مكتوب بحذر لكنه غير مترجَم.

#### أنواع المؤشرات (إضافة)
- **5 أنواع شريط** (`BarStyle`): `line` ━━╸── (افتراضي) · `block` ████▌░░ (دقة 1/8) · `dots` ●●●○○ · `segments` ▰▰▰▱▱ · `shade` ▓▓░░.
- **9 أنواع spinner** (`SpinnerStyle`): `braille` · `dots` · `line` · `arc` · `bounce` · `pulse` · `arrow` · `grow` · `slide` (شريط منزلق ▰▰▰▱▱▱).
- **`Spinner` جديد** لعمل غير محدَّد المدة (بلا نسبة): `succeed/fail/warn/stop`. مستخدم الآن في «Resolving dependencies» في `rin pkg install`.
- الاختيار: `RIN_PROGRESS_BAR=block RIN_PROGRESS_SPINNER=arc` أو برمجياً عبر `IndicatorStyle` (ctor لـ `ProgressBar`/`Spinner` و`ConsoleBarSink::Options::style`). بلا Unicode: ASCII تلقائياً.
- المعرض: `loader_ui_demo --gallery`.

#### مؤشر لكل نوع أمر (Task) — مع الوقت والكمية
`profileFor(Task)` يعطي لكل أمر هويّة: فعل، وحدة عدّ، لون، شكل شريط وspinner، وهل يُعرض ETA والسرعة.

| الأمر | الفعل | الوحدة | اللون | شريط + spinner | يعرض |
|---|---|---|---|---|---|
| `rin pkg install/add/update` | Installing | pkgs | بنفسجي | line + braille | المنقضي، ETA |
| `rin test` | Testing | tests | أخضر | shade + arc | المنقضي، ETA، tests/s |
| `rin build` | Building | steps | برتقالي | block + grow | ملخّص: الزمن + حجم الناتج |
| `rin analyze` / check | Checking | files | سماوي | segments + dots | spinner + النتيجة |
| `rin fmt` | Formatting | files | وردي | dots + line | الزمن + عدد الأسطر المتغيّرة (على stderr) |
| `rin clean` | Cleaning | items | أزرق | segments + bounce | الزمن + عدد الملفات المحذوفة وحجمها |
| `rin doctor` | Diagnosing | checks | أخضر فاتح | dots + pulse | 5 فحوص بنتيجة كل منها |
| `rin run --time` | Running | — | بنفسجي | slide | زمن التشغيل الفعلي (stderr) |

- **ETA** = (المنقضي ÷ المنجَز) × المتبقي، من معدّل الإنجاز الفعلي؛ لا يظهر قبل إنجاز أول عنصر.
- **الكميات حقيقية:** ملفات/بايتات `rin clean` تُعدّ فعلاً قبل الحذف، وحجم ناتج `rin build` يُقاس بـ `stat`.
- `rin test`: يكتب الشريط على دفق مستقل (fd=1) لأن الاختبار يعيد توجيه `std::cout` مؤقتاً؛ ناتج الاختبارات الفاشلة يُطبع بعد الشريط.
- `rin build`: الأمر الفرعي (rinc) يكتب في نفس الطرفية فلا يصلح شريط حي؛ لذا ملخّص نهائي بهويّة Build.
- واجهات جديدة: `Task`، `TaskProfile`، `profileFor`، `ProgressBar(out, profile, total, fd)`، `Spinner(out, profile, msg, fd)`، `report()`، `formatBytes()`.
- المعرض: `loader_ui_demo --tasks`.

---

## TERMINAL UNICODE

<sub>المصدر الأصلي: `CHANGES_TERMINAL_UNICODE.md`</sub>

### Unicode ملوّن في RinStudio + `ocr` في طرفيته

- **أمر `ocr` في طرفية RinStudio** (`RinTerminalView.kt`): `ocr <صورة> [--lang ara+eng] [--psm N] [--enhance] [--min N]`.
  لا منطق OCR جديد ولا دوال محرّك: يبني استدعاء `make.image.text` الموجودة ويشغّله بنفس مسار أي أمر في الطرفية
  (`onExecuteRequested`)، فيُطبع النص بـ Unicode كما هو، وتحته سطر الثقة · الكلمات · اللغة. المعاملات تُفحص (أرقام، أسماء لغات)
  والمسار يُهرَّب (`\` `"` `$` والأسطر) قبل دخوله نص الشيفرة. ملف الصورة نسبي لمجلد المشروع كما في `make.image.text`.
- **ألوان Unicode**: `RinAnsi.kt` (جديد، منطق خالص) يحوّل أكواد ANSI SGR في المخرجات — مثل ناتج `colorAnsi(...)` الموجودة في المحرّك —
  إلى مقاطع ملوّنة في الطرفية: 16 لوناً، 256، truecolor، خلفية، غامق/مائل/تسطير. كانت تظهر قبلاً أكواداً خاماً.
  سطر الثقة يُلوَّن بـ `colorAnsi` (أخضر ≥80، أصفر ≥50، أحمر دون ذلك).
- **أمان**: كل تسلسل غير SGR (تحريك المؤشر، مسح الشاشة، OSC، أحرف C0/C1) يُسقَط، فلا يعبث نص صورة أو ملف بالشاشة.
  النسخ (`copy`/`share`) و`grep` وتصنيف السطر ورابط `[Error line N]` تعمل على النص بلا أكواد.
- **CLI (Linux)**: `:ocr` (الإطار من ocr-on-main22) صار سطر الثقة فيه بألوان `Style` الموجودة (أخضر/أصفر/أحمر)؛ لا دوال جديدة.
- ملفات: جديد `RinAnsi.kt`، `RinAnsiTest.kt`؛ معدَّل `RinTerminal.kt` (3 مواضع تستعمل `RinAnsi.strip`)، `RinTerminalView.kt`،
  `strings_terminal.xml` ×6 (`terminal_ocr_usage` + سطر في `terminal_help`)، `cli/linux/src/terminal/rin_terminal.cpp`، `docs/terminal.md`.
- الأساس: rinlang-main__21 + أرشيف main_22 (`:img`/`:ocr`) + ocr-on-main22 (إطار `:ocr`).
- **تحقق**: `RinAnsi.kt` رُجم فعلاً بـ kotlinc 2.0 وجُرّبت حالاته (truecolor، 256، OSC/CSI عدائية، تسلسلات مقطوعة)؛ بناء `rin` بـ CMake ناجح،
  `tests/terminal/run.sh` كلها PASS، و`:ocr` جُرّب في pty حقيقي (ألوان ظاهرة)، ومقتطف Rin الذي يولّده `ocr` شُغّل على المحرّك المبني
  (نجاح، مسار بمسافة، ملف مفقود، معاملات خاطئة ← null).
- **لم يُتحقق**: ترجمة `RinTerminalView.kt` و`RinAnsiTest.kt` (JUnit) داخل Gradle/Android (لا SDK هنا)، والعربية في OCR (حزمة ara غير مثبّتة)،
  وOCR على جهاز أندرويد (يحتاج tessdata). شغّل `./gradlew testDebugUnitTest` وجرّب `ocr` على جهاز.
- حدّ: حالة اللون لا تمتد بين الأسطر (كل سطر يبدأ بلا تنسيق).

---

## TERMINAL XCMDS

<sub>المصدر الأصلي: `CHANGES_TERMINAL_XCMDS.md`</sub>

### 100 أمر جديد للطرفية (لينكس + RinStudio)

الأسلوب: `$ أمر … ` (أو `:`)، `*` توسيع ملفات، `~/` المنزل، `A:B` / `A…B` مدى. النصوص كلها إنجليزية.

**الدفعة 1 (50):** cat head tail wc grep find mkdir rmdir rm cp mv touch stat du realpath basename dirname echo sort uniq tac rev nl upper lower cut cmp sha256 base64 hexdump crc32 env export unset date whoami hostname uname which uptime free nproc df seq sleep alias unalias pushd popd dirs

**الدفعة 2 (50):** ll ln chmod readlink file mktemp exists tee append split diff comm paste tr fold replace trim shuf wordfreq count rot13 md5 sha1 urlencode urldecode hex unhex ascii conv calc factor primes fib gcd rand uuid timestamp cal datediff id ps cpuinfo mounts printenv yes true false term path numfmt

#### الملفات
- **جديد** `cli/linux/src/terminal/rin_terminal_xcmds2.h` — الدفعة الثانية (يُضمَّن تلقائياً من `rin_terminal_xcmds.h`، فلا تغيير في CMake).
- **جديد** `cli/linux/src/terminal/rin_terminal_xcmds.h` — الدفعة 1 + header-only (POSIX + std)، لا CMake جديد ولا مكتبات؛ لا يشغّل أي عملية خارجية فيعمل على أندرويد كما هو.
- **معدَّل** `cli/linux/src/terminal/rin_terminal.cpp` — `$` كبادئة أمر (تشغيل/متابعة الأسطر/الإكمال/rc)، دمج الأوامر في `commands()` و`:help`، `alias`، وفشل الأمر يُحتسب في رمز خروج `--batch`.
- **معدَّل** `app/src/main/java/com/dlof/rinlang/RinTerminalView.kt` — أسطر `$ cmd` تُوجَّه للجلسة الأصلية كأسطر `:`.
- **معدَّل** `res/values{,-ar,-en,-es}/strings_terminal_native.xml` — سطر مساعدة (إنجليزي).
- **معدَّل** `docs/terminal.md`، `tests/terminal/run.sh`؛ **جديد** `tests/terminal/xcmds_session.{in,expected}`.

#### ما تحقّقتُ منه فعلاً
- ترجمة `rin_terminal.cpp` بلا تحذيرات (`-Wall -Wextra`) بالنسختين العادية و`-DRIN_TERMINAL_EMBED`.
- بناء `rin` كاملاً وتشغيل جميع الأوامر الـ 50 في جلسة `rin terminal` حقيقية، وفي `EmbeddedTerminal` (مسار RinStudio): glob، `~/`، المدى، alias، pushd/popd دون تغيير مجلد العملية، حجب `:sh`، أخطاء ورموز خروج، وحماية `rm /`. اختبار `xcmds_session` ناجح.
- **لم أستطع:** بناء أندرويد (لا NDK/kotlinc) — تغيير Kotlin صغير (دالة + Regex)، فراجعه عند أول بناء. اختبار pty التفاعلي لإكمال Tab بـ `$` لم يُجرَّب.

**الدفعة 3 (15 — متقدمة):** `bigfiles recent dupes empty loc todo` (ملفات) · `trash backup sync rename` (أمان ونسخ) · `json csv stats context verify` (بيانات).

كلها بالأسلوب نفسه: `$ أمر …`، `*` توسيع، `~/` المنزل، بلا تشغيل أي عملية خارجية (تعمل في RinStudio/أندرويد):
- `$ sync . ~/backup` ينسخ الجديد/المتغيّر فقط (يحفظ وقت التعديل فيصير التكرار فورياً) و`-n` معاينة؛ يرفض تداخل المصدر والوجهة.
- `$ trash f ~/old` حذف آمن إلى `~/.local/share/rin/trash` مع `-l` عرض و`-u <id>` استرجاع و`-e -y` تفريغ؛ يرفض `/` والمنزل.
- `$ backup main.rin -d ~/bk` ← `main.rin.bak-YYYYmmdd-HHMMSS`.
- `$ rename '(.*)\.txt' '$1.rin' *.txt` (`-n` معاينة، يرفض الكتابة فوق موجود إلا `-f`). **استعمل علامات اقتباس مفردة** كي لا يُوسَّع `$1` كمتغيّر.
- `$ json f.json users[0].name` تحقق/تنسيق/تصغير (`-m`)/استعلام مسار، مع رقم السطر والعمود عند الخطأ. `$ csv f.csv` جدول محاذى (فاصل تلقائي). `$ stats -e "3 1 4"` إحصاءات ومئينات و`-h` مدرّج. `$ verify sums.txt` مثل `sha256sum -c`.
- **جديد** `cli/linux/src/terminal/rin_terminal_xcmds3.h` (يُضمَّن من `rin_terminal_xcmds.h`، فلا تغيير في CMake)؛ **معدَّل** `rin_terminal_xcmds.h` (تسجيل الدفعة)؛ **جديد** `tests/terminal/xcmds3_session.{in,expected}` + فقرة في `tests/terminal/run.sh`.
- التحقق: ترجمة `-Wall -Wextra` بلا تحذيرات (عادية و`-DRIN_TERMINAL_EMBED`)، `tests/terminal/run.sh` كاملاً ناجح (بما فيه اختبارات pty: Tab، Ctrl-C، التاريخ)، وجرّبتُ الإكمال بـ Tab لـ `$ bigf⇥ -n 2 ~/pr⇥` في pty حقيقي.

---

#### الدفعة 4 — **500 أمر جديد** (المجموع الآن 615)

كلها بالأسلوب نفسه `$ أمر …`، header-only، بلا تشغيل أي عملية (تعمل في RinStudio/أندرويد)، ونصوص الإخراج إنجليزية. لا تعارض أسماء مع الدفعات السابقة (تُفحص آلياً).

| العائلة | العدد | أمثلة |
|---|---|---|
| تحويل الوحدات | 157 | `$ convert 5 kg lb` · `$ units` (القائمة) · اختصارات `km2mi mi2km kg2lb c2f f2c kwh2j gb2mib psi2bar hp2kw …` |
| أدوات النص | 84 | `camel snake kebab pascal slugify wordwrap center levenshtein jaro soundex roman numword caesar vigenere morse nato tashkeel arnorm fancy upsidedown …` |
| ترميز | 15 | `base32 base58 base64url binary octal htmlesc jsonesc qp utf8bytes codepoints …` |
| هاشات وفحص | 21 | `sha512 sha384 sha224 hmac crc16 crc8 adler32 fnv1a32/64 murmur3 xxh32 luhn isbn ean13 iban entropy strength …` |
| ألوان | 15 | `hex2rgb rgb2hsl hsl2rgb lighten darken mix contrast luminance palette colorname …` |
| شبكة ومراجع | 16 | `cidr netmask ipcheck ip2int urlparse domainof emailcheck port httpstatus mime semverbump semvercmp …` |
| رياضيات وإحصاء | 124 | `sqrt fact isprime totient divisors comb perm stdev percentile zscore linreg det solve2 modpow modinv rpn const …` |
| مالية وهندسة | (ضمن الرياضيات) | `loan compound cagr roi tip vat discount circle sphere heron polygon …` |
| تواريخ | 23 | `now epoch fromepoch dayofweek weekno addays addmonths age countdown workdays easter hijri fromhijri datefmt zodiac parsedur moonphase …` |
| مولّدات | 12 | `dice coin pick password lorem randhex token ulid nanoid randint lottery randcolor` |
| ملفات | 13 | `tree filesize extstats oldest longlines badws eol fixeol bom magic sums countin lines` |
| مصادر Rin | 9 | `rinfiles rinelements rinlint rinblocks rindepth rincomments rinattrs rinstrings rinids` |
| بيانات | 11 | `spark barchart columns transpose csvcol csvsort csv2json csv2md jsonkeys iniget linefreq` |

**صدق في العدّ:** ≈157 أمراً من الوحدات و≈120 من الرياضيات مبنية على جداول/دوال متشابهة (اختصارات مثل `km2mi`)؛ والباقي (~220) أوامر فردية بمنطق خاص. كل أمر يعمل ويُختبر، لكن لا تتوقّع 500 خوارزمية مستقلة.

- `$ help <أمر>` و`:help` يعرضان الاستعمال؛ Tab يُكمل الأسماء والملفات.
- **ملاحظات:** `ln` محجوز لـ symlink فاللوغاريتم الطبيعي اسمه `loge`؛ `pascal` لتحويل النص، وصف الصفوف باسم `pascaltri`. أوامر `rin*` تحليل نصي للكتل `@ns.Type … .end/ns` ولا تشغّل المفسّر. التقويم الهجري هو الحسابي (tabular) وقد يختلف يوماً عن أم القرى.
- **ملفات:** جديد `rin_terminal_xcmds4.h` … `rin_terminal_xcmds7.h`؛ معدَّل `rin_terminal_xcmds.h` (ربط `registry4()`) و`rin_terminal.cpp` (إزالة تحذير متغيّر غير مستعمل)؛ جديد `tests/terminal/xcmds4_session.{in,expected}` + فقرة في `run.sh`.
- **التحقق:** ترجمة `-Wall -Wextra` بلا تحذيرات (عادية و`-DRIN_TERMINAL_EMBED`)؛ مقارنة مع Python لـ 66 حالة هاش/ترميز (SHA-224/384/512، adler32، base32، base64url بأطوال حدّية) وإحصاءات ورياضيات (comb/perm/fact/modpow/modinv…) كلها مطابقة؛ `run.sh` كاملاً ناجح. **لم يُجرَّب:** بناء أندرويد (لا NDK).

---

## TESSERACT

<sub>المصدر الأصلي: `CHANGES_TESSERACT.md`</sub>

### إصلاح فشل البناء: tesseract4android (2026-10-04)

**الخطأ (من سجل Build & sign release APK، المهمة :app:mergeReleaseNativeLibs):**
`Could not find cz.adaptech.tesseract4android:tesseract4android:4.7.0` (ثم 4.9.0) —
البحث في Google Maven وMaven Central فقط وكلاهما يعيد 404.

**السبب:** المكتبة تُنشر على JitPack (حسب README الرسمي لـ adaptech-cz/Tesseract4Android)
ولا يوجد مستودع JitPack في `settings.gradle.kts`.

**الإصلاح:**
- `settings.gradle.kts`: إضافة JitPack عبر `exclusiveContent` للمجموعة `cz.adaptech.tesseract4android` فقط.
- `app/build.gradle` و`app/build.gradle.kts`: توحيد النسخة على 4.9.0 (minSdk 24 ≥ 21 المطلوب).

بقية المراحل (Preflight، اختبارات المحرك 54+42) كانت ناجحة.

---

## TIME

<sub>المصدر الأصلي: `CHANGES_TIME.md`</sub>

### CHANGES_TIME.md — مفهوم `time`
- جديد: `app/src/main/cpp/rin_extra_natives7.cpp` (`time()` و36 دالة `time.*`)، يُضمَّن في نهاية `rin_interpreter.cpp` فيدخل كل أهداف البناء.
- `rin_interpreter.h/.cpp`: تصريح وتسجيل `registerNativesExtra7`، واستثناء `time` من منع التسمية بدالة مدمجة، وحارس في `invokeCallee` يجعل تعريف المستخدم يغلب.
- المحرّر: كلمات `time.*` في `RinSyntaxHighlighter.kt` و`syntaxes/rin-keywords.json`، و5 مقتطفات في `RinContainerTags.kt`.
- توثيق: `docs/TIME.md`. اختبار: `tests/verification/time_basic.rin/.expected`. مثال: `examples/time_demo.rin`.

#### دعم indsin
- `indsin/rin_indsin_rinbridge.h`: `time.sleep/measure/config` تُرفض داخل الخصائص (بقية `time.*` نقية).
- `indsin/rin_indsin_c_api.cpp/.h`: `rin_indsin_session_tick` يحدّث `warp clock` كل ثانية ويضيف `"ticking":true`.
- `IndsinPreviewActivity.kt`: يكرّر tick كل ثانية عند `"ticking":true`.
- اختبار `tests/tools/test_indsin_time.cpp` ومثال `examples/samples/indsin_time_demo.rin`.

---

## TOKEN

<sub>المصدر الأصلي: `CHANGES_TOKEN.md`</sub>

### CHANGES_TOKEN.md — تطوير مفهوم token

إضافة بحتة: لا تغيير في `tokens`/`tokenType`. (يتضمّن هذا الأرشيف أيضاً ملفات ضمانات النطاق finally/defer/catch-when/rethrow لأن `rin_interpreter.cpp` مدموج معها.)

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/rin_extra_natives8.cpp` | جديد: 11 دالة token |
| `app/src/main/cpp/rin_interpreter.cpp/.h` | تسجيل `registerNativesExtra8` وتضمين الملف |
| `app/src/main/cpp/rinc.cpp`، `RinSyntax.kt` | الأسماء الجديدة |
| `syntaxes/rin-keywords.json`، `scripts/intellisense_docs.json` | الأسماء والتوثيق |
| `app/src/main/cpp/editor/rin_intellisense_data.h` | مُعاد توليده بالمولّد (+11 سطراً) |
| `docs/TOKEN.md` | الوثيقة |
| `tests/verification/token_concept.rin/.expected` | الاختبار |

---

## VM

<sub>المصدر الأصلي: `CHANGES_VM.md`</sub>

### CHANGES_VM
- Added RinVM (rin_bcvm*.cpp/h), engine switch in Interpreter, `rin --vm`, `rin vm` (real VM), `rinvm` defaults to RinVM, tests/vm, docs/vm.md.

---

## WEB JS LOCALHOST

<sub>المصدر الأصلي: `CHANGES_WEB_JS_LOCALHOST.md`</sub>

### تغييرات: إصلاح rin_html_runtime.js + HTML+JS + أيقونات + صفحة إنشاء + localhost:7700

- **الإصلاح**: `app/src/main/assets/rin_html_runtime.js` كان مفقوداً من المشروع (مجلد assets غير موجود) فظهر «تعذّر تحميل rin_html_runtime.js». أُعيد كتابته.
- **خيار JS بديل**: نوع مشروع `html_js` ينشئ index.html + style.css + script.js + container.rin (حاوية Rin موقَّعة تستدعيها JS عبر rin.call/rin.get). `HtmlProjectRuntime.kt` مشترك.
- **أيقونات**: HTML5 / CSS3 / JS ملوّنة (`ic_html_file`, `ic_css_file`, `ic_js_file`).
- **صفحة إنشاء المشروع**: صفوف موحّدة بأقسام (الويب / Rin).
- **http://localhost:7700/**: `RinLocalServer.kt` لمشاريع HTML وRin؛ زر الكرة الأرضية في شاشة التشغيل أو Run ← «فتح على localhost:7700».
- لم يُبنَ APK هنا (لا Android SDK).

---

## BUILD REPAIR

<sub>المصدر الأصلي: `BUILD_REPAIR.md`</sub>

### Rin build repair — 2026-09-14

Fixed:
- Gradle plugin resolution failure at app/build.gradle:2.
- Root Groovy build.gradle now declares Android/Kotlin/Google Services plugin versions.
- Removed duplicate build.gradle.kts files that competed with the Groovy build.
- Aligned Gradle wrapper and GitHub Actions with AGP 8.13.0 -> Gradle 8.13.
- Kept JDK 17, NDK 26.1.10909125 and CMake 3.22.1.
- Gave duplicate signed-release workflows distinct names.
- Added Gradle toolchain verification to Android workflows.

Primary failure from supplied log:
Plugin [id: 'com.android.application'] was not found ... plugin dependency must include a version number.
The cause was that the active root build.gradle had no plugin versions while app/build.gradle requested an unversioned Android plugin.

Official AGP compatibility: AGP 8.13 requires Gradle 8.13 and JDK 17.

---

## MANIFEST CHANGES

<sub>المصدر الأصلي: `MANIFEST_CHANGES.md`</sub>

### ملخص التغييرات (لم تُطبّق على GitHub فعلياً — راجع الرسالة)

#### ملفات تم إنشاؤها
- .gitignore (جديد/محدّث)
- .github/workflows/cleanup-organize.yml (جديد)
- scripts/upload-secrets-local.sh (جديد)

#### مجلدات/ملفات تم نقلها (المسار القديم -> الجديد)
- tools/test_*.cpp -> tests/tools/
- samples/* -> examples/samples/
- verification/* -> tests/verification/

#### ملفات تم حذفها من المستودع (لن تجدها في هذا الـ zip لأنها محذوفة)
- signing/keystore-credentials.txt  (انقلها لـ GitHub Secret: ANDROID_KEYSTORE_CREDENTIALS)
- signing/release.keystore          (انقلها لـ GitHub Secret: ANDROID_KEYSTORE_BASE64)
- build-artifact/rin_run            (تُرفع كمرفق GitHub Release بدلاً من ذلك)
- build-artifact/rincheck           (تُرفع كمرفق GitHub Release بدلاً من ذلك)

#### Simple Container/Mask Style
- Added `container(name)`, `container(name, field)`, and `container(name, field, value)` runtime sugar.
- Added `mask(name)` runtime sugar returning `{mask,name,kind}` or `nil`.
- Added `docs/simple-style.md` and `examples/simple_container_mask.rin`.
- Existing `@container=...`, `mask = ...;`, and low-level APIs remain compatible.

## COMPOSE

- **`lib/compose.og.rin` (مكتبة أساسية مدمجة)**: تحويل موحَّد (`valueKind` `asMap` `asArray` `asSet` `asFn`)، تركيب (`compose` `chain` `through` `juxt` `branch` `attempt` وجبر الشروط)، دمج (`combine` `mergeWith` `mergeAll` `mergeInto` `mergeContainers` `joinRecords`)، تفاعل (`mapOver` `filterOver` `reduceOver` `connect` `sync`)، وجسر token↔number. التوثيق: `docs/compose.md`، الاختبار: `tests/verification/compose_basic.rin`.
- **توسعة compose (الإصدار 2.0.0)**: قسم «أدوات عامة» في `compose.og.rin` (`sizeOf` `isEmptyValue` `kindIs` `toKind` `hasKey` `getOr` `keysOf` `valuesOf` `describeValue` `composeVersion` `composeLibraries`) دون كسر أي دالة قائمة. وست مكتبات مساعدة مدمجة: `compose_seq` (ترتيب مستقر وتجميع وتقطيع وضمّ وبحث)، `compose_path` (مسارات `"a.b.0"`: قراءة وكتابة غير مُعدِّلة وتسطيح ونسخ عميق)، `compose_flow` (curry وmemoize وretry وcond وmatchKind ونتائج `Result` وتتبّع المراحل)، `compose_schema` (مخططات تحقّق وتطبيع مختصرة)، `compose_diff` (`diffDeep` `patchDeep` `invertChanges` `merge3`)، `compose_store` (مخزن حالة تفاعلي بتراجع وربط بالحاويات). أُعيدت تسمية ما يتعارض مع دوال مدمجة أو مكتبات أخرى (`groupOver` `repeatOver` `minOver` `maxOver` `keepKeys` `dropKeys` `mapOk` `resultOr` `firstNonNil`). `tools/embed_compose.py` يدمج العائلة كلها (`--check` للتحقق). التوثيق: `docs/compose-helpers.md`، الأمثلة: `examples/compose_advanced.rin`، الاختبارات: `tests/verification/compose_{core,seq,path,flow,schema,diff,store}.rin`.

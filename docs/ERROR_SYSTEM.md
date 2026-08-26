# نظام Diagnostics في Rin (Compiler Diagnostics)

هذا التوثيق يشرح نظام الأخطاء/التحذيرات الموحّد (`app/src/main/cpp/diagnostics/`)
الذي يستخدمه Lexer وParser وInterpreter في Rin، بأسلوب قريب من رسائل
`rustc`/`swiftc` الحديثة: كود ثابت لكل خطأ، موقع دقيق (سطر + عمود)، شرح
"لماذا"، اقتراحات تلقائية ("did you mean؟")، ومخرجات آلية (JSON/LSP) بجانب
العرض النصي الملوّن للطرفية.

> **حالة التغطية الحالية (مهم):** هذا النظام مبنيّ ومُدمَج فعلياً في الكود
> (Lexer/Parser/Interpreter الثلاثة يستخدمونه ويُصرَّفون بنجاح)، وكل الأمثلة في
> هذا الملف نواتج حقيقية شُغِّلت فعلياً وليست نصاً افتراضياً. مع ذلك: من أصل
> ~150 موقع رمي خطأ (throw) في المحرّك الأصلي، حوالي 35 موقعاً (الأخطاء
> "الرئيسية": متغير غير معرَّف، دالة غير معروفة، عدد وسائط خاطئ، فهرس خارج
> الحدود، مخالفة Schema، علاقة/ترحيل/استيراد غير موجود، أخطاء الحاويات
> السياقية...) حصلت على معالجة يدوية كاملة (reason + expected/found + help +
> suggestions حيثما ينفع). الباقي (~115 موقعاً، أغلبها فحوصات نوع الوسائط في
> الدوال المدمجة مثل `'push' expects an array`) حُوِّل آلياً إلى النظام
> الجديد فيحمل الآن **كوداً وموقعاً دقيقاً ورسالة** (ويظهر بشكل صحيح في
> plain/short/json/lsp)، لكن بلا حقول `reason`/`help`/`suggestions` إضافية
> مخصَّصة له بعد. توسيع أيٍّ منها لاحقاً هو مجرد إضافة `.withReason(...)`
> ونحوها على موقع `throw diagErr(...)` الموجود مسبقاً — لا حاجة لإعادة هيكلة.

---

## 1) نظرة معمارية سريعة

```
app/src/main/cpp/diagnostics/
├── source_location.h        # SourceLocation: سطر/عمود البداية والنهاية
├── source_manager.h/.cpp    # SourceManager: يحتفظ بنص كل ملف مصدر لعرض السطر المخالف
├── diagnostic.h/.cpp        # Diagnostic + جدول أكواد E0001..E0040 وW0001..W0008
├── diagnostic_engine.h/.cpp # DiagnosticEngine (تجميع أخطاء متعددة) + Levenshtein/suggestions
└── diagnostic_renderer.h/.cpp # plain/short/json/lsp rendering
```

- **Diagnostic** هو الكائن الموحَّد: `severity` (error/warning/note/help)،
  `code` (enum `rin::diag::Code`)، `message`، وحقول اختيارية `reason`،
  `expected`/`found`، `notes`، `hints` (أسطر `help:`)، `suggestions`
  ("did you mean")، و`causedBy` (سلسلة "Caused by:").
- **SourceLocation** يحمل `file` + `startLine/startCol/endLine/endCol`
  (1-indexed، النهاية حصرية) — يُستخدَم لرسم السهم `^^^^` تحت المقطع الصحيح
  بالضبط من السطر، لا مجرد رقم السطر.
- **SourceManager** (نسخة عامة واحدة عبر `rin::diag::globalSourceManager()`)
  يحتفظ بنص كل ملف سجَّله `Lexer` (كل `Lexer` يسجِّل ملفه تلقائياً في مُنشِئه)
  حتى يستطيع الـ renderer عرض سطر الكود الفعلي.
- **DiagnosticEngine** يجمع عدة `Diagnostic` بدل التوقف عند أول خطأ — يُستخدَم
  في `Parser::parseCollectingDiagnostics()` (انظر القسم 6).
- **RinError** (في `rin_common.h`) وسِّع ليحمل `std::optional<diag::Diagnostic>`
  بجانب `.message`/`.line` الأصليين — **توافقية خلفية كاملة**: أي كود قديم
  (JNI bridge، C API، wasm bridge) يقرأ `.message`/`.line` فقط يستمر بالعمل
  دون أي تعديل، وأي كود جديد يستطيع فحص `e.diagnostic` للحصول على التمثيل
  الغني الكامل.

## 2) جدول أكواد الأخطاء (E-codes)

| الكود | الاسم | يُستخدَم فعلياً في |
|---|---|---|
| E0001 | UndefinedVariable | قراءة/تعيين متغير غير معرَّف (مع اقتراح Levenshtein) |
| E0002 | DuplicateVariable | *(محجوز — غير مُفعَّل بعد؛ Rin تسمح حالياً بإعادة `let` لنفس الاسم كـ shadowing)* |
| E0003 | InvalidAssignment | هدف تعيين غير صالح (`10 = x;`) |
| E0004 | InvalidType | فحوصات نوع الوسائط/العوامل (unary/binary، دوال مدمجة، فهرسة قيمة غير قابلة للفهرسة...) |
| E0005 | TypeMismatch | *(محجوز لفحص أنواع مستقبلي؛ Rin حالياً بلا تعليقات نوع ثابتة في `let`)* |
| E0006 | UnknownFunction | استدعاء اسم غير مُعرَّف كدالة (مع اقتراح من أسماء natives) |
| E0007 | InvalidArguments | عدد وسائط خاطئ (دوال المستخدم والدوال المدمجة) |
| E0008/E0009 | InvalidReturn/MissingReturn | *(محجوزان — Rin بلا نوع رجوع مُعلَن يُتحقَّق منه ساكنياً)* |
| E0010 | ParserError | خطأ Lexer عام لا يندرج تحت كود أدق |
| E0011 | UnexpectedToken | حرف/رمز غير متوقَّع (Lexer)، سوء استخدام `break`/`continue`، تعارض وسم إغلاق `.end/...` |
| E0012 | MissingToken | أي فشل `Parser::consume()` (';' / ')' / '=' مفقودة...) — أهم كود عملياً لأخطاء بنية الجملة |
| E0013 | InvalidExpression | تعبير متوقَّع غير موجود، استدعاء قيمة غير قابلة للاستدعاء، مفتاح map غير صالح |
| E0014 | InvalidContainer | مخالفة قواعد سياق الحاوية (`fun`/حاوية متداخلة داخل `container.data`، `route` خارج `container.api`...) |
| E0015 | UnknownContainer | `@tag` غير معروف (مع اقتراح Levenshtein من قائمة الأنواع الصحيحة) |
| E0016 | InvalidProperty | اسم خاصية/attribute خاطئ داخل DSL الحاويات (`merge`/`tying`/`link`/`route`/`document`/`row`/`style`/`file`...) |
| E0017 | UndefinedProperty | *(محجوز)* |
| E0018 | InvalidSchema | `defineSchema` بوسائط غير صالحة |
| E0019 | SchemaViolation | `insertDoc`/`updateDoc`/`document` تخالف Schema مُعرَّفة |
| E0020 | InvalidDocument | وسيط مستند غير صالح (`insertDoc` بلا map...) |
| E0021 | InvalidIndex | فهرس مصفوفة/نص خارج الحدود أو من نوع خاطئ (وأيضاً `createIndex` بفهرس DB خاطئ) |
| E0022 | InvalidRelation | `relatedDocs` باسم علاقة غير مُعرَّفة عبر `defineRelation` |
| E0023 | TransactionError | *(محجوز — Rin الحالية بلا `beginTransaction`/`commitTransaction` صريحة)* |
| E0024 | MigrationError | `defineMigration`/`runMigration` بوسائط/اسم غير صالح |
| E0025 | CacheError | *(محجوز)* |
| E0026/E0027 | AsyncError/AwaitOutsideAsync | *(محجوزان — Rin الحالية بلا `async`/`await`)* |
| E0028 | ImportError | فشل عام في `container.import`/`@import` (فتح ملف، تحليل، تنفيذ محتوى الملف/المكتبة المستوردة) |
| E0029 | ModuleNotFound | مكتبة `@import` غير موجودة لا كمكتبة مدمجة ولا في `lib/` |
| E0030 | CircularDependency | *(محجوز — لا يُكتشَف بعد آلياً؛ راجع "قيود معروفة" أدناه)* |
| E0031 | GenericError | القيمة الافتراضية لأي `Diagnostic` مبني دون كود صريح (لا يظهر عملياً) |
| E0032–E0034 | InvalidGenericArgument/OwnershipError/BorrowError | *(محجوزة — Rin بلا generics/ownership)* |
| E0035 | RuntimeError | قسمة على صفر، تجاوز عمق الاستدعاء، أخطاء تشغيل عامة أخرى |
| E0036 | IOFailure | فشل فتح/قراءة/كتابة ملف فعلي (`appendFile`/`readFile`/`container.import`...) |
| E0037 | NetworkError | استدعاء API غير مسجَّل (`apiHeader`/`apiCall` بلا `apiRegister` مسبق) |
| E0038 | PackageError | *(محجوز)* |
| E0039 | InternalCompilerError | *(محجوز — لا استخدام حالياً؛ أي `throw` غير متوقَّع يصل للمستخدم يجب ألا يظهر كـ crash خام) |
| E0040 | UnsupportedFeature | *(محجوز)* |

الأكواد "المحجوزة" **معرَّفة بالكامل** في `diagnostic.h`/`diagnostic.cpp` (يمكن
استخدامها فوراً بلا أي تعديل بنيوي) لكن لا يوجد اليوم مسار كود فعلي في اللغة
يُصدرها، غالباً لأن الميزة اللغوية المقابلة (transactions صريحة، async/await،
generics...) غير موجودة أصلاً في Rin حالياً. أُبقيت في الجدول حرفياً كما وردت
في الطلب الأصلي حتى تكون جاهزة فور إضافة أي من تلك الميزات مستقبلاً.

### أكواد التحذير (W-codes)

`W0001..W0008` (UnusedVariable, UnusedImport, UnreachableCode,
DeprecatedFeature, ShadowedVariable, UnnecessaryConversion, UnusedFunction,
SuspiciousComparison) **معرَّفة بالكامل** في `diagnostic.h` (تصنَّف Warning
افتراضياً عبر `defaultSeverity()`) لكن **لا يوجد اليوم أي تمريرة تحليل ساكن
(static analysis pass) في Rin** تكتشفها فعلياً — Rin مفسَّرة مباشرة بلا
تمريرة "compile check" منفصلة عن التنفيذ. إضافتها تتطلب تمريرة جديدة كاملة
(walk على AST قبل التنفيذ) وليست موجودة ضمن نطاق هذا التسليم؛ الأكواد جاهزة
والـ renderer يدعمها بالكامل (`severity: "warning"`، إلخ) فور توفر تلك
التمريرة.

## 3) مثال ناتج فعلي (rustc-style)

هذا ناتج حقيقي شُغِّل فعلياً (`rin check merge_wrong_attribute.rin`) على:

```rin
merge from=app;
```

```
error[E0016]: invalid attribute `from`
  --> merge_wrong_attribute.rin:1:7

 1 | merge from=app;
   |       ^^^^

reason:
  `merge` expects the attribute `with`

help:
  replace `from` with `with`

1 error emitted
```

ومثال آخر (خطأ تشغيل حقيقي — متغير غير معرَّف، عبر `rin undefined_variable.rin`):

```
error[E0001]: undefined variable `y`
  --> undefined_variable.rin:2:1

 2 | print y;
   | ^

reason:
  no variable named `y` exists in this scope

possible matches:
  `E`
  `x`
  `PI`

help:
  did you mean `E`?
```

(الاقتراح هنا يستخدم فعلياً كل الأسماء المرئية في النطاق الحالي عبر
`Environment::collectVisibleNames()` + مسافة Levenshtein — وليس نصاً ثابتاً.)

## 4) أشكال العرض الأربعة

عرَّفها `diagnostic_renderer.h`:

| الشكل | الاستخدام |
|---|---|
| `plain` (الافتراضي) | نمط rustc/swiftc الكامل مع سطر الكود والسهم — للطرفية |
| `short` | سطر واحد `file:line:col: error[E0001]: message` — لعرض قوائم مطوَّلة بسرعة أو لِـ grep |
| `json` | مصفوفة JSON، كائن واحد لكل Diagnostic، بكل الحقول (`severity`, `code`, `codeName`, `message`, `file`, `line`, `column`, `endLine`, `endColumn`, `reason`, `expected`, `found`, `notes`, `help`, `suggestions`, `causedBy`) |
| `lsp` | مصفوفة JSON بصيغة LSP Diagnostic (`range`/`severity`/`code`/`source`/`message`/`relatedInformation`) — جاهزة للـ IDE/Language Server مباشرة |

مثال JSON فعلي:

```json
[{"severity":"error","code":"E0016","codeName":"InvalidProperty","message":"invalid attribute `from`","file":"merge_wrong_attribute.rin","line":1,"column":7,"endLine":1,"endColumn":11,"reason":"`merge` expects the attribute `with`","notes":[],"help":["replace `from` with `with`"],"suggestions":[],"causedBy":[]}]
```

## 5) `rin check` — أداة سطر الأوامر

أُضيفت لثلاثة أماكن:

1. **CLI لينكس/macOS/ويندوز** (`cli/*/src/main.cpp`): `rin check <file> [--format=plain|short|json|lsp]`
2. **أداة مطوّر مستقلة** `tools/rin_check.cpp` (تُبنى أيضاً كهدف CMake منفصل
   `rincheck` في `app/src/main/cpp/CMakeLists.txt`، بجانب `rinc`/`loomc`
   الموجودين مسبقاً — لا يُربط بـ `rinengine` ولا يُحزَم داخل APK).

```
rin check app.rin                     # عرض rustc-style كامل
rin check app.rin --format=short      # سطر واحد لكل خطأ
rin check app.rin --format=json       # rin check app.rin --format=json (القسم 26 من الطلب)
rin check app.rin --format=lsp
```

كود الخروج: `0` بلا أخطاء، `1` مع خطأ واحد أو أكثر، `2` تعذّر فتح الملف.

**قيد مهم:** `rin check` يقوم بـ **lex + parse فقط، بلا تنفيذ**. لذا فهو
يكتشف كل أخطاء Lexer/Parser (E0011/E0012/E0013/E0014/E0015/E0016...) لكن
**لا** يكتشف الأخطاء التي لا تظهر إلا أثناء التنفيذ الفعلي (E0001 متغير غير
معرَّف، E0006 دالة غير معروفة، E0007 عدد وسائط عند الاستدعاء الفعلي، E0021
فهرس خارج الحدود...) — هذا لأن Rin **لغة مفسَّرة ديناميكياً بالكامل بلا
تمريرة تحليل ساكن منفصلة** (لا يوجد "type checker" يمشي على AST قبل
التشغيل). فحص تلك الأخطاء ساكنياً (دون تنفيذ الكود فعلياً) يتطلب بناء
تمريرة تحليل ساكن كاملة (تتبّع تدفق التحكّم لمعرفة أي المتغيرات ستكون معرَّفة
عند كل نقطة، بلا تنفيذ فعلي) وهذا خارج نطاق التسليم الحالي (كان يمكن تنفيذه
كطبقة إضافية منفصلة لاحقاً فوق نفس `DiagnosticEngine` دون تغيير أي شيء في
النظام الحالي). لرؤية تلك الأخطاء اليوم استخدم `rin <file>` العادي (ينفِّذ
الكود فعلياً).

## 6) أخطاء متعددة + Error Recovery

`Parser::parseCollectingDiagnostics(DiagnosticEngine&)` (تُستخدَم حصراً من
`rin check`، بينما `Parser::parse()` العادي — المُستخدَم من التنفيذ الفعلي —
يبقى يرمي عند أول خطأ كما كان تماماً، بلا أي تغيير سلوكي): تحاول تحليل كل
عبارات الملف، وعند فشل عبارة تستدعي `Parser::synchronize()` للتقدّم حتى نقطة
تزامن آمنة (`;` / `}` / `)` / بداية `.end/...`) قبل محاولة العبارة التالية،
بدل التوقف عند أول خطأ. هذا يعني أن `rin check` قد يطبع عدة أخطاء من تشغيل
واحد، كما في هذا الناتج الفعلي:

```
$ rin check stress1.rin --format=short
stress1.rin:1:1: error[E0015]: unsupported block `@unknown1`
stress1.rin:6:11: error[E0012]: Expected '=' after 'with'
```

**قيد معروف (recovery غير مثالي):** الاسترداد granularity هو مستوى العبارة
الكاملة، وليس granularity أدق (على مستوى الرمز المفرد كما في بعض
الـ parsers المتقدمة) — لذا قد يُبتلَع أكثر من خطأ حقيقي ضمن نفس عملية
`synchronize()` الواحدة إن كانت متقاربة جداً (مثال: 3 أسطر متتالية كلها
تفتقد الفاصلة المنقوطة قد تُبلَّغ كخطأ واحد فقط بدل ثلاثة). هذا محافظ ومتعمَّد
(الأولوية: عدم رمي diagnostics خاطئة/مضلِّلة على حساب عدم إظهار كل خطأ
ممكن)، وليس عطلاً؛ تحسينه يتطلب granularity أدق على مستوى كل عبارة فرعية،
وهو تحسين مستقبلي ممكن دون أي تغيير بنيوي.

> ⚠️ **ملاحظة تصحيح مهمة:** أثناء تطوير `synchronize()` ظهر خطأ حقيقي
> (infinite loop → `std::bad_alloc`) حين يحتوي الملف على وسم إغلاق يتيم
> `.end/...` بلا `@` مطابقة في المستوى الأعلى (لأن `synchronize()` كانت تتوقف
> فوراً دون استهلاك أي توكن إن كان `checkClosingTag()` صحيحاً، فيعيد المستوى
> الأعلى محاولة تحليل نفس التوكنات إلى ما لا نهاية). أُصلِح بضمان استهلاك
> توكن واحد على الأقل قبل إعادة فحص شروط التوقف. اختبار الانحدار الخاص به:
> `tests/diagnostics/golden/unknown_block_tag.rin` (`.end/containr` يتيمة).

## 7) الاقتراحات التلقائية (Suggestion Engine)

`diagnostics/diagnostic_engine.h` يوفّر `levenshteinDistance()` و
`nearestMatches()`/`bestMatch()` (مسافة Levenshtein، O(min(n,m)) ذاكرة). تُستخدَم في:

- **E0001** (متغير غير معرَّف): تُقارَن مع كل الأسماء المرئية فعلياً في
  النطاق الحالي (`Environment::collectVisibleNames()` — تمشي على سلسلة
  `parent` كاملة).
- **E0006** (دالة غير معروفة): تُقارَن مع كل أسماء `natives` المسجَّلة فعلياً.
- **E0015** (`@tag` غير معروف): تُقارَن مع قائمة الأنواع الصحيحة الفعلية.
- **E0016** بعض المواقع (`link.id=`، `merge`'s `with`): اقتراح مباشر مبني
  على معرفة سياقية (وليس Levenshtein بحت) لأن السياق يجعل النية شبه مؤكدة
  (مثال: `merge from=` → الأغلب أن القصد `with=`).

**قيد معروف:** الأسماء القصيرة جداً (حرف واحد) قد تولّد اقتراحات غير مفيدة
عملياً (رأينا `y` غير المعرَّف يقترح الثابت العام `E`، لأن مسافة Levenshtein
بينهما تساوي 1) — هذا سلوك صحيح خوارزمياً لكنه أحياناً غير مفيد عملياً لأسماء
قصيرة جداً؛ يمكن تحسينه لاحقاً باشتراط طول أدنى أو نسبة مسافة/طول أصغر.

## 8) كيف تضيف كوداً جديداً

1. أضف عضواً جديداً في `enum class Code` (`diagnostic.h`).
2. أضف حالته في `codeString()`/`codeName()`/`defaultSeverity()` (إن لم يكن `E`/`W` قياسياً) في `diagnostic.cpp`.
3. حدِّث الجدول في القسم 2 أعلاه.
4. استخدمه من موقع الخطأ الفعلي (انظر القسم 9/10 أدناه).

## 9) كيف تضيف Diagnostic لموقع Parser

استخدم `Parser::err(code, token, message)` (يبني الموقع تلقائياً من
`token.line/col/endCol`) ثم أثرِه اختيارياً:

```cpp
auto d = err(diag::Code::E0016_InvalidProperty, key, "invalid attribute `" + key.lexeme + "`");
d.diagnostic->withReason("`merge` expects the attribute `with`")
 .withHint("replace `from` with `with`");
throw d;
```

أو، لأخطاء "توكن مفقود" العادية، استخدم `consume(type, message)` مباشرة —
تبني E0012 غنياً تلقائياً (تستخرج `expected`/`found` من الرسالة والتوكن
الفعلي) بلا أي كود إضافي.

## 10) كيف تضيف Diagnostic لموقع Interpreter

- داخل دالة/تابع عضو حقيقي في `Interpreter` (وليس lambda بلا `[this]`):
  استخدم `err(code, line, message)` أو `errWithReason(code, line, message, reason)`
  (أعضاء `Interpreter`، تستخدم `sourceFile` الحالي).
- داخل أي دالة حرة (`asNumber`, `expectArgs`, ...) أو lambda داخل
  `natives[...]` (سواء كانت `[this]` أو لا): استخدم الدالة الحرة
  `diagErr(code, line, message)` (تستخدم `g_diagFile` العام، الذي تتم
  مزامنته مع `sourceFile` في بداية `Interpreter::run()`).
- لأخطاء "متغير غير معرَّف"/"دالة غير معروفة" تحديداً استخدم
  `undefinedVariableErr(name, line, env)` / `unknownFunctionErr(name, line)`
  الجاهزتين (تبنيان الاقتراحات تلقائياً).

## 11) قيود معروفة (Known Limitations) — بصراحة كاملة

- **E0002/E0005/E0008/E0009/E0017/E0023/E0025–E0027/E0030/E0032–E0034/E0038/E0040
  محجوزة بلا مسار كود فعلي يُصدرها اليوم** (انظر القسم 2) — إما لأن الميزة
  اللغوية المقابلة غير موجودة في Rin حالياً، أو (E0030 التبعية الدائرية في
  `@import`) لأنها تتطلب تتبّع رسم بياني لمسارات الاستيراد الجاري تحليلها
  حالياً، وهو غير مبني بعد.
- **W0001–W0008 (كل التحذيرات) محجوزة بلا تمريرة تحليل ساكن تُصدرها** — Rin
  مفسَّرة مباشرة بلا "compile check" منفصل عن التنفيذ اليوم.
- **~115 من أصل ~150 موقع خطأ في Interpreter** حُوِّلت آلياً (كود + موقع +
  رسالة أصلية) بلا `reason`/`help`/`suggestions` مخصَّصة إضافية بعد (انظر
  ملاحظة "حالة التغطية" أعلى الملف) — القيمة الأساسية (كود مستقر + سطر دقيق
  + JSON/LSP قابل للاستهلاك آلياً) موجودة بالكامل لكل هذه المواقع، لكن جودة
  شرح "help" النصي لكل منها متفاوتة.
- **الأعمدة (columns) في Diagnostics الصادرة من Interpreter تُقارَب بـ 1**
  (بداية السطر) وليست دقيقة كتلك الصادرة من Lexer/Parser، لأن عُقَد AST
  (`Stmt`/`Expr`) تحمل `.line` فقط بلا عمود دقيق حالياً (خلافاً لـ `Token`
  الذي وسَّعناه بـ `col`/`endCol`). توسيع AST بعمود دقيق لكل عقدة تحسين
  مستقبلي ممكن لكنه خارج نطاق هذا التسليم (يتطلب تعديل كل مُنشئات AST في
  `rin_parser.cpp`، وهو تغيير أوسع من نظام Diagnostics نفسه).
- **`rin check` لا يكتشف أخطاء التنفيذ** (انظر القسم 5) — قيد بنيوي في
  فلسفة Rin الحالية (لغة مفسَّرة بلا static type checker)، وليس نقصاً في
  نظام Diagnostics نفسه.
- **Error recovery في `synchronize()` بمستوى العبارة كاملة** (انظر القسم 6)
  — قد يُبتلَع أكثر من خطأ حقيقي متقارب ضمن نفس عملية استرداد واحدة.
- **الاقتراحات لأسماء قصيرة جداً (حرف واحد) قد تكون غير مفيدة عملياً** (انظر
  القسم 7).
- **JSON output ليس RFC 8259 كاملاً لكل حالة Unicode نادرة** (يُصرَّف `\uXXXX`
  فقط لأحرف التحكم دون 0x20؛ النص العربي/UTF-8 العادي يمر كما هو، وهو سلوك
  صحيح لـ JSON UTF-8 قياسي، لكن لم يُختبَر ضد fuzzer شامل).

## 12) الاختبارات

`tests/diagnostics/golden/*.rin` + `*.expected` (8 حالات تغطي: `E0001`,
`E0006`, `E0007`, `E0012`, `E0015`, `E0016` ×2, `E0021`) — تُقارَن حرفياً
بمُشغِّل `tests/diagnostics/run_golden_tests.sh`:

```bash
# بعد بناء الأدوات (rin و rin_check، أو rincheck):
tests/diagnostics/run_golden_tests.sh /path/to/rin_check /path/to/rin
```

الاختبارات الأربعة الأولى (`undefined_variable`, `type_mismatch_index`,
`wrong_arg_count`, `unknown_function`) هي أخطاء **تشغيل** (تُقارَن عبر
`rin <file>`)، والأربعة الأخرى (`parser_missing_semicolon`,
`merge_wrong_attribute`, `unknown_block_tag`, `link_unknown_attribute`) هي
أخطاء **lex/parse** (تُقارَن عبر `rin_check <file> --format=plain`) — انظر
القسم 5 لتفسير هذا الفرق. هذه التغطية جزئية (8 حالات من أصل ~150 موقع خطأ
ممكن)، وليست شاملة لكل كود مذكور في القسم 2؛ توسيعها هو ببساطة تكرار نفس
النمط (ملف `.rin` صغير + `.expected` مولَّد من تشغيل حقيقي للأداة) لكل كود
إضافي تريد تغطيته.

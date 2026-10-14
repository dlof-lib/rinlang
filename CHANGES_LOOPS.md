# CHANGES_LOOPS — Loops v2
ميزات جديدة بالكامل additive (لا تغيير في أي صيغة قائمة): `each` (فهرس/قاموس/تفكيك/`where`/`reversed`)، `repeat N [as i]`،
`loop`/`loop (c)`/`loop until (c)`، تسميات الحلقات `name:` + `break name`/`continue name`، `limit N` على كل الحلقات،
`retry N [as a]`، وتعبير التجميع `[each x in xs where c yield e]`. كانت `each/repeat/loop` موثّقة في docs/loops.md لكنها غير منفّذة.

## إضافة: أنماط each المتقدمة
`by N` (مجموعات) · `window N` (نوافذ) · `every N` (خطوة) · `zip` (توازٍ) · `for x in xs { }` بلا أقواس. كلها تفكيك إلى ForStmt/Block داخل المحلل أيضاً. اختبار: `tests/loops/loops_modes.rin`.

## الملفات
`app/src/main/cpp/rin_parser.cpp|.h` فقط: كل الصيغ تُفكَّك في المحلل إلى While/For/ForIn/Block/If/TryCatch/Goal الموجودة
(enterLoop/exitLoop/finishLoop). المفسّر وRinVM لم يُعدَّلا. `docs/loops.md`، `tests/loops/*`.

## التحقق
tests/loops 5/5 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · verification 57/63
(الستة `let_err_*` تعرض الخطأ نفسه بصيغة كاملة بدل الرمز — لا علاقة بالتعديل). بُني على سطح المكتب (g++)؛ لم يُبنَ APK.

## Loops v3 (إضافة)
`skip/take/until/while` · `sorted [by KEY] [desc]` · `unique` · `flat` · `chain` · `downto` · `cross` · `pairs` · `from N` · `nobreak {}` ·
اختزالات `sum/product/count/avg/min/max/any/all/first/last each ...` · قاموس مفهوم `{each x in xs yield k : v}`.
الملفات: `rin_parser.cpp|.h` (تفكيك) و`rin_extra_natives5.cpp` (دوال `#downto #sorted #sortby #unique`). المفسّر وRinVM لم يُعدَّلا.
التحقق (g++ على لينكس): tests/loops 7/7 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/vm تطابق 5/5.
لم تُشغَّل: tests/verification، بناء APK/الويب. الأداء لم يتغيّر.

## دمج تحديث rinlang-loops-changed
محلل الحلقات في الأرشيف الجديد فرعٌ مواز ينقصه ما في v3 (`nobreak`/`downto`/`cross`/`pairs`/الاختزالات/`from`) فلم يُستبدل،
وإنما نُقلت ميزتاه إلى محلل v3: `distinct [on (key)]` (مرحلة تيار) و`each x in xs, y in ys` (rin_parser.cpp|.h)، مع
`RinSyntax.kt` و`syntaxes/rin-keywords.json` وتوثيق واختبارَي `loops_distinct` و`loops_pro`.
التحقق (g++، مفسّر `rin` مبني محلياً): tests/loops 9/9 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42.
لم يُشغَّل: tests/verification وبناء APK/الويب.

## Rin For+ (حلقة for على طراز Rin)
`i++`/`i--` و`+= -= *= %=` (تعبيرات عامة في المحلل) · `for` بلا أقواس · عدّة خطوات تزايد `i++, j--` · مسار سريع بلا closures.
الملفات: `rin_parser.cpp|.h` (تفكيك)، و`rin_ast.h` (`ForStmt::freshIterScope`)، و`rin_interpreter.cpp` (فرع ForStmt: تنفيذ الجسم في forEnv مباشرة
عندما لا توجد closures). RinVM لم يُعدَّل. اختبار: `tests/loops/loops_for.rin`. `/=` تبقى `!=` كما هي في اللغة.
التحقق (g++ لينكس): tests/loops 10/10 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/vm تطابق 5/5 · verification 57/63 (فروقات let_err_* المعروفة).

## قوالب الحلقات `@tmpl`
`@tmpl name(params[=default]) [as vars] <ترويسة حلقة>;|{ جسم افتراضي }` ثم `@name(args) [as vars] { body }`. قالب كتلة: `@tmpl name(..) as i, j { ... @body ... }`.
تعمل مع كل أنواع الحلقات وفي التعابير (`[@t(xs) yield e]`, `sum @t(xs) yield e`) وتتركّب (قالب يستعمل قالباً) بلا تعديل في المفسّر أو RinVM:
مرحلة توكنات `Parser::expandLoopTemplates` تُنفَّذ مرة في بداية `parse()`/`parseCollectingDiagnostics()` ولا تعمل أصلاً إن لم يوجد `@tmpl`.
الملفات: `rin_parser.cpp|.h` (قسم `@tmpl` في آخر الملف) · `docs/loop-templates.md` · `docs/loops.md` · `tests/loops/loops_tmpl.rin|.expected` ·
`syntaxes/rin.tmLanguage.json` (تلوين) · `editor/rin_editor_tools.h` (قالبا `tmpl` و`tmplb`).
التحقق (g++ لينكس، بناء `cli/linux`): tests/loops 11/11 على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/vm تطابق 5/5 · tests/editor 98/98.
لم يُشغَّل: tests/verification، بناء APK/الويب/Kotlin. الحدود: القالب خاص بالملف (لا يعبر `@import`) وغير معزول الأسماء.

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

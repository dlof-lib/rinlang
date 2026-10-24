# `looptwist` — حلقات Rin القوية فوق RINTWIST

مكتبة Disconom 2 (`lib/looptwist.og.rin/`) تضيف إلى حلقات Rin ما ينقصها: مصادر تكرار جاهزة، حلقات بسقف أمان،
إعادة محاولة وانتظار وميزانية زمنية، شريط تقدّم، واختصارات — وكل **إعداداتها الافتراضية تأتي من [RINTWIST](rintwist.md)**
فلا تحتاج ضبطاً ولا ملفات إعداد.

```rin
@import "looptwist";

for c in lt_chunk(lt_upto(10), 4) { print c; }            // [0,1,2,3] [4,5,6,7] [8,9]
lt_each(["a", "b"], fun(x) { print x; });
print LT.sum(LT.map(LT.irange(1, 4), fun(x) { return x * x; }));   // 30  ← الاختصارات: LT.<اسم بلا lt_>
```

## ما الذي يأخذه من RINTWIST؟
| الإعداد | القيمة الافتراضية (من RINTWIST) |
|---|---|
| `limit` — سقف الدورات | `5,000,000`؛ و`200,000` إن كان `RINTWIST.app.mode` هو `android` أو `wasm` |
| `trace` — تتبّع الحلقات | `true` إن كان `RINTWIST.app.debug` |
| `tag` — وسم الرسائل | يُحسب **وقت الاستدعاء**: اسم المكتبة الجارية أو اسم ملف المستدعي (`RINTWIST.file.stem`) |
| الساعة | `RINTWIST.run.uptimeMs` (في `lt_timed` و`lt_budget` و`lt_poll` والتقدّم) |
| `lt_where()` / `lt_logName()` | قوالب `rintwist.fmt`: `"{lib.name}@{lib.version} {file.part}"` و`"{file.stem}-{date.date}.log"` |

`lt_config({limit: 1000, trace: true, tag: "job", sleep: false})` يغيّرها (مفتاح مجهول = خطأ واضح)، و`lt_settings()` تقرؤها.

## قواعد مهمّة (حدود اللغة)
- Rin يشترط **تطابق عدد الوسائط** ولا يدعم القيم الافتراضية، لذلك كل صيغة اختيارية لها اسم صريح:
  `lt_upto(n)` / `lt_range(a, b)` / `lt_rangeBy(a, b, step)` ، `lt_retry(n, fn)` / `lt_retryWith(n, fn, opts)` ، `lt_sorted` / `lt_sortedDesc` / `lt_sortedBy` ...
- دوال الحلقة (callbacks) تُرجع `LT_STOP` للإيقاف المبكر: `return LT_STOP;`. اللاحقة `i` تمرّر الفهرس: `lt_eachi(xs, fun(x, i) {...})`.
- لا شيء يعدّل مدخلاته: كل الدوال تُرجع قيماً جديدة. المصادر تقبل مصفوفات ونصوصاً (حروفاً).
- كل حلقة محروسة بـ `limit`؛ تجاوزه يرمي: `looptwist: loop limit exceeded in lt_loopDo (...)` (قابل للالتقاط).
- الأسماء بالحروف اللاتينية (المحلّل لا يقبل معرّفات عربية بعد).

## المصادر (تُرجع مصفوفات)
| الدالة | مثال → ناتج |
|---|---|
| `lt_upto(n)` · `lt_range(a, b)` · `lt_rangeBy(a, b, s)` | `lt_rangeBy(10, 0, -4)` → `[10, 6, 2]` |
| `lt_irange(a, b)` · `lt_irangeBy(a, b, s)` (شاملة، اتجاه تلقائي) | `lt_irange(5, 1)` → `[5,4,3,2,1]` |
| `lt_linspace(a, b, n)` | `lt_linspace(0, 1, 5)` → `[0, .25, .5, .75, 1]` |
| `lt_enumerate(xs)` · `lt_enumerateFrom(xs, k)` | `[["a",1],["b",2]]` |
| `lt_zip(a, b)` · `lt_zipAll([a, b, c])` | `[[1,"x"],[2,"y"]]` (بأقصر طول) |
| `lt_chunk(xs, n)` | `[[1,2],[3,4],[5]]` |
| `lt_window(xs, n)` · `lt_windowBy(xs, n, step)` · `lt_pairs(xs)` | `[[1,2],[2,3],[3,4]]` |
| `lt_flatten` · `lt_unique` · `lt_uniqueBy(xs, keyFn)` | أول ظهور يُحفظ |
| `lt_product(a, b)` · `lt_cycle(xs, n)` · `lt_fill(v, n)` | ضرب ديكارتي / تكرار |
| `lt_take(xs, n)` · `lt_skip(xs, n)` · `lt_reversed(xs)` | |
| `lt_group(xs, keyFn)` · `lt_partition(xs, pred)` · `lt_tally(xs)` · `lt_countBy(xs, keyFn)` | `{"1": [...]}` · `[[مطابقة],[غيرها]]` |
| `lt_sorted` · `lt_sortedDesc` · `lt_sortedBy(xs, keyFn)` · `lt_sortedByDesc` | نسخة مرتّبة؛ الأصل لا يتغيّر |

## الحلقات
`lt_times(n, fn(i))` · `lt_each(xs, fn(x))` · `lt_eachi(xs, fn(x, i))` · `lt_eachKV(map, fn(k, v))` · `lt_map` · `lt_mapi` · `lt_filter` · `lt_filteri` ·
`lt_reduce(xs, fn(acc, x))` · `lt_fold(xs, init, fn)` · `lt_sum` · `lt_sumBy` · `lt_count` · `lt_countIf` · `lt_find` · `lt_findIndex` · `lt_any` · `lt_all` ·
`lt_whileDo(cond, body(i))` · `lt_untilDo(cond, body(i))` · `lt_loopDo(body(i))` · `lt_every(n, i, fn)` (كل n-ياً دورة).

## الزمن والمتانة
```rin
let r = lt_timed("work", fun() { return heavy(); });         // {value, ms, label}
lt_budget(50, fun(i) { step(i); });                          // يكرّر 50ms → {iterations, ms, stopped: "budget"|"stop"}
lt_retryWith(5, fun(a) { return call(a); }, {delay: 100, factor: 2, maxDelay: 2000,
             onRetry: fun(a, err, wait) { print "retry " + a + " in " + wait; }});   // تراجع أُسّي؛ بعد الفشل الأخير يُعاد رمي الخطأ الأصلي
lt_pollEvery(fun() { return ready(); }, 100, 3000);          // true عند التحقق، false عند المهلة
let p = lt_progressWidth(200, 20);  lt_tick(p);  print lt_progressLine(p);   // [##------------------] 10% 20/200 1.2s eta 10.8s
```
`lt_config({sleep: false})` يجعل الانتظار فورياً (للاختبارات).

## سياق RINTWIST
`lt_where()` → `"looptwist@1.0.0 core/control.zpp"` أو اسم ملفك · `lt_logName()` → `app-2026-10-24.log` · `lt_trace(label)` / `lt_traceWith(label, details)` تطبعان
`[tag] label: details` عندما `trace` مفعّل (تلقائياً في وضع التصحيح).

## القيود المعروفة
- تعتمد على `RINTWIST`، و`RINTWIST` لا يعمل حالياً تحت `rin --vm` (قيد قائم في المحرّك)؛ استعمل المفسّر الشجري.
- لا دوال متغيّرة الوسائط ولا قيم افتراضية في اللغة، ولا معرّفات عربية — لذلك الواجهة بأسماء لاتينية صريحة.

الاختبارات: `tests/verification/looptwist_basic.rin` (+`.expected`)، أو `rin lib script lib/looptwist.og.rin test --allow-scripts (من جذر المستودع)`.

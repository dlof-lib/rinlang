# `__rsf__` — منع التكرار تلقائياً وكشف الدورات (RINTWIST + Disconom)

مكتبة Disconom 2 (`lib/__rsf__.og.rin/`، 8 أجزاء) تُستدعى **من RINTWIST** مباشرة، وتجمع شيئين:

1. **منع التكرار تلقائياً** — مفتاح قانوني لأي قيمة (مصفوفات وخرائط متداخلة) فتتساوى القيم بنيوياً: `[1,{a:1,b:2}] == [1,{b:2,a:1}]` و`1 ≠ "1"`.
2. **كشف الدورات** — في التتابعات (`x → f(x)`)، وفي الرسوم/التبعيات/الروابط، وفي تبعيات Disconom وسلسلة الاستيراد.

```rin
print rintwist.rsf("dedupe", [3, 1, 3, 2]);                       // [3, 1, 2]
print rintwist.rsf("cycles", {a: ["b"], b: ["c"], c: ["a"]});     // [["a","b","c","a"]]
let R = rintwist.rsf();                                           // الواجهة كاملة (تُحمَّل المكتبة عند أول استدعاء)
print R.hasCycle(0, fun(x) { return (x + 1) % 3; });              // true
```
`rintwist.rsf()` يحمّل `__rsf__` عند الحاجة ويُرجع الواجهة `RSF`؛ و`rintwist.rsf("اسم", وسائط...)` يستدعي العضو مباشرة.
أو بالاستيراد: `@import "__rsf__";` ثم `rsf_dedupe(...)` أو `RSF.dedupe(...)`.

## منع التكرار
| الدالة | الوصف |
|---|---|
| `rsf_dedupe(xs)` · `rsf_dedupeBy(xs, keyFn)` | يُبقي أول ظهور؛ لا يعدّل الأصل. تقبل نصاً (حروفاً) |
| `rsf_duplicates(xs)` · `rsf_hasDuplicates(xs)` · `rsf_counts(xs)` | المكرَّر / هل يوجد / `[[قيمة, عدد]]` |
| `rsf_set()` · `rsf_add(s, v)` · `rsf_has` · `rsf_size` · `rsf_items` | مجموعة رؤية: `rsf_add` تُرجع `true` للجديد و`false` للمكرَّر (ولا تضيفه) |
| `rsf_memo(fn)` | دالة بوسيط واحد تُنفَّذ **مرة لكل قيمة**؛ وإن عادت إلى نفس الوسيط أثناء تنفيذها تُرمى `cycle detected in rsf_memo: 1 -> 2 -> 1` بدل التكرار اللانهائي |
| `rsf_key(v)` · `rsf_equal(a, b)` | المفتاح القانوني والتساوي البنيوي |

## كشف الدورات
| الدالة | الوصف |
|---|---|
| `rsf_iterate(start, step)` | يتتبّع `x → step(x)` حتى `nil`: `{status: "end"\|"cycle"\|"limit", steps, path, mu, lambda, cycle}` (ذاكرة O(n)) |
| `rsf_brent(start, step)` · `rsf_hasCycle(start, step)` | Brent بذاكرة ثابتة: `{cycle, mu, lambda}` |
| `rsf_cycles(g)` · `rsf_isAcyclic(g)` | رسم `{عقدة: [جيران]}`: دورة واحدة لكل حافة رجوع `[["a","b","a"]]` (نفس الدورة لا تتكرر مهما كان مدخلها) |
| `rsf_graph(g)` | `{ok, order, cycles, nodes, edges, duplicateEdges, missing}` — الحروف المكرَّرة تُزال تلقائياً ويُبلَّغ عددها |
| `rsf_topo(g)` | ترتيب تبعيات (التبعيات أولاً)؛ يرمي عند وجود دورة |
| `rsf_walk(start, nbrsFn)` | مشي بدالة جيران: كل عقدة تُزار **مرة واحدة** تلقائياً + الدورات: `{visited, order, cycles, steps}` |

DFS تكراري (لا استدعاء ذاتي)، فلا خطر تجاوز العمق في رسوم كبيرة.

## RINTWIST + Disconom
| الدالة | المصدر |
|---|---|
| `rsf_importStack()` | `RINTWIST.stack` → `{chain, duplicates, cycle}` |
| `rsf_loadedDuplicates()` | `RINTWIST.lib.loaded` ← مكتبة محمَّلة مرتين |
| `rsf_libGraph(name)` | `rinlib.graph` (Disconom) مع إزالة المكرَّر: `{ok, order, cycles, problems, missing}` |
| `rsf_selfCheck()` | يجمع الثلاثة للمكتبة الجارية (`RINTWIST.lib.name`): `{ok, lib, importStack, loadedDuplicates, graph}` |

الإعدادات الافتراضية من RINTWIST: `limit` = `5,000,000` (و`200,000` في `android`/`wasm`)، `trace` عند `RINTWIST.app.debug`، والوسم من `lib.name`/`file.stem`.
`rsf_config({limit, depth, strict, trace})`: `strict: true` يجعل كشف دورة **خطأً** بدل تقرير (مفتاح مجهول = خطأ واضح).

## حدود
- محرّك Rin يرفض أصلاً دورة `@import` ويتجاهل الاستيراد المكرَّر؛ `__rsf__` يضيف التقرير/التشخيص ويغطّي بقية الحالات (بيانات المستخدم، التتابعات، رسوم التبعيات).
- القيم الذاتية المرجع (خريطة تحوي نفسها) تُرفض عند عمق `depth` (64) بدل التعليق.
- Rin يشترط تطابق عدد الوسائط، لذلك `rsf_memo` بوسيط واحد (مرّر مصفوفة لعدّة وسائط).
- الأسماء بحروف لاتينية (المحلّل لا يقبل معرّفات عربية بعد).

الاختبار: `tests/verification/rsf_basic.rin` (+`.expected`).

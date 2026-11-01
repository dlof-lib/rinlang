# compose-helpers.md — المكتبات المساعدة لـ compose

> كل مثال هنا جُرِّب فعليًا عبر المفسِّر (انظر `tests/verification/compose_*.rin` و`examples/compose_advanced.rin`).

`compose` (راجع [`compose.md`](./compose.md)) هي الجسر الأساسي. هذه ست مكتبات مدمجة توسّعها، وكل واحدة
**تستورد `compose` بنفسها**، فيكفي استيراد ما تحتاجه:

```kotlin
@import "compose_seq";      // أو "lib/compose_seq.og.rin"
@import "compose_store";
```

| المكتبة | الغرض | الحجم |
|---|---|---|
| `compose_seq` | ترتيب، تجميع، تقطيع، ضمّ، بحث فوق أي مفهوم | 13.7 KB |
| `compose_path` | مسارات `"a.b.0"`: قراءة، كتابة غير مُعدِّلة، تسطيح، نسخ عميق | 13.2 KB |
| `compose_flow` | curry، ذاكرة، إعادة محاولة، توجيه، نتائج `Result`، تتبّع المراحل | 10.7 KB |
| `compose_schema` | مخططات تحقّق وتطبيع (أنواع، حقول، عناصر، enum، default، coerce) | 11.1 KB |
| `compose_diff` | `diffDeep` و`patchDeep` و`invertChanges` ودمج ثلاثي `merge3` | 5.4 KB |
| `compose_store` | مخزن حالة تفاعلي: اشتراكات، قيم مشتقّة، batch، تراجع، ربط بالحاويات | 6.3 KB |

**مبادئ مشتركة**
- لا تُعدِّل أي دالة مدخلاتها، إلا ما اسمه `put…` أو `store…` (يُعدِّل في المكان ويُوثَّق ذلك).
- كل مرحلة/شرط/مفتاح يمرّ عبر `asFn`: دالة، `map`، `enum`، `Set`، `array` كلها تصلح.
- لا معاملات افتراضية في Rin، لذا لا تُمرَّر `nil` للمعامل الاختياري إلا حيث يُذكر.
- المقارنة `==` في Rin عميقة لـ `array` و`map` و`Set`، وتعتمد عليها هذه المكتبات.
- أسماء الدوال العامة فُحصت فلا تتعارض مع دوال Rin المدمجة ولا مع أي مكتبة أخرى في `lib/`
  (لذلك `groupOver` لا `groupBy`، و`minOver` لا `minBy`، و`keepKeys` لا `pickKeys`).

---

## compose_seq — التسلسلات

كل الدوال تقبل `array` أو `Set` أو `map` أو كائن أو نصًا (`seqItems`) وتُرجع `array`.

| الفئة | الدوال |
|---|---|
| ترتيب | `cmpValues(a,b)` `sortWith(x,cmp)` `sortBy(x,keyFn)` `sortAsc` `sortDesc` `isSorted` `minOver(x,keyFn)` `maxOver(x,keyFn)` |
| تجميع | `groupOver(x,keyFn)` `indexBy` `countBy` `tally(x)` `partitionOver(x,pred)` `uniqBy` `uniqOver` |
| تقطيع | `takeOver` `dropOver` `sliceOver(x,from,to)` `takeWhileOver` `dropWhileOver` `chunkOver(x,n)` `windowOver(x,n)` `rangeOf(from,to,step)` `repeatOver(v,n)` |
| ضمّ | `zipWith(a,b,f)` `zipAll(a,b)` `unzipPairs` `flatMapOver` `flattenDeep` `intersperse(x,sep)` |
| بحث وعدّ | `findOver` `findIndexOver` `anyOver` `allOver` `noneOver` `countOver` `sumBy` `scan(x,f,init)` `firstOf(x,fb)` `lastOf(x,fb)` |

```kotlin
print sortAsc(["banana", "apple", "Apple", "app"]);   // ["Apple", "app", "apple", "banana"]
print sortBy(people, fun(p) { return p["age"]; });     // ترتيب مستقر: المتساوون يحتفظون بترتيبهم
print tally("hello");                                  // {"h": 1, "e": 1, "l": 2, "o": 1}
print windowOver([1, 2, 3, 4], 2);                     // [[1, 2], [2, 3], [3, 4]]
print anyOver([1, 2], Set([2]));                       // true  (Set كشرط)
```

- النصوص تُقارن حرفًا بحرف بكود الحرف (Rin لا تدعم `<` على النصوص)؛ فالحروف الكبيرة قبل الصغيرة.
- ترتيب الأنواع المختلطة: `nil < bool < number < string < غيرها`.
- مفاتيح `groupOver`/`indexBy`/`countBy` تتحوّل إلى نص (`str`).

## compose_path — المسارات

المسار مصفوفة `["a", 0, "b"]` أو نص `"a.0.b"`. المقطع الرقمي فهرسٌ حين يكون الهدف `array` ومفتاحٌ حين يكون `map`.

| الفئة | الدوال |
|---|---|
| قراءة | `getIn(x,path,fallback)` `hasIn(x,path)` `pathOf(p)` `leafPaths(x)` `findPaths(x,pred)` |
| كتابة | `setIn(x,path,v)` (غير مُعدِّلة) · `putIn(x,path,v)` (في المكان) · `updateIn(x,path,f)` · `deleteIn(x,path)` |
| تسطيح | `flattenKeys(x,sep)` `unflattenKeys(m,sep)` |
| مشي ونسخ | `walkValues(x,f)` `mapLeaves(x,f)` `cloneDeep(x)` |
| أشكال map | `keepKeys` `dropKeys` `renameKeys(x,{old:new})` `mapKeys(x,f)` `invertMap` `dropNil` `withDefaults(x,defaults)` |

```kotlin
let cfg = {"a": {"b": [10, {"c": 5}]}, "x": 1};
print getIn(cfg, "a.b.1.c", 0);              // 5
print setIn({}, ["l", 0, "k"], 1);           // {"l": [{"k": 1}]}   (المقطع الرقمي ينشئ array)
print deleteIn(cfg, "a.b.0");                // {"a": {"b": [{"c": 5}]}, "x": 1}
print flattenKeys(cfg, ".");                 // {"a.b.0": 10, "a.b.1.c": 5, "x": 1}
print unflattenKeys({"a.b": 1, "a.c": 2}, "."); // {"a": {"b": 1, "c": 2}}
```

`setIn` تنسخ فقط ما على المسار وتُشارك الباقي، فهي رخيصة وتصلح أساسًا للتراجع (يعتمد عليها `compose_store`).
`unflattenKeys` تُنتج دائمًا `map` (المقاطع الرقمية تبقى مفاتيح نصية).

## compose_flow — التدفّق والنتائج

| الفئة | الدوال |
|---|---|
| وسطاء | `applyArgs(f,args)` `spread(f)` `curry2` `curry3` `curryN(f,n)` `partialN(f,pre)` `rpartial1(f,b)` |
| ذاكرة | `memoize1(f)` `memoizeWith(f,keyFn)` `once(f)` `lazy(f)` |
| تكرار | `times(n,f)` `retry(f,attempts)` `iterate(f,x,n)` `until(pred,f)` |
| توجيه | `cond([[pred,f],...])` `matchKind({kind: f, "default": f})` `juxtMap({name: f})` `converge(f,[g,h])` `firstNonNil(list)` `defaultTo(v)` `guard(pred,msg)` |
| نتائج | `okResult` `failResult` `isOkResult` `tryResult(f)` `mapOk(r,f)` `thenResult(r,f)` `resultOr(r,fb)` `safeChain(stages)` `collectResults(list)` |
| تشخيص | `traceChain(stages)` `timed(f)` |

`Result` هي `map` عادية: `{"ok": true, "value": v}` أو `{"ok": false, "error": e}`.

```kotlin
print safeChain([inc, fun(x) { throw "stop"; }, dbl])(1);   // {"ok": false, "error": "stop"}
print matchKind({"array": fun(a) { return len(a); }, "default": constant("?")})([1, 2, 3]);  // 3
print traceChain([inc, dbl])(1)["steps"];    // [{"stage": 0, "in": 1, "out": 2}, {"stage": 1, "in": 2, "out": 4}]
print curryN(add3, 3)(1)(2)(3);              // 6
```

- `retry` ترمي آخر خطأ بعد استنفاد المحاولات، و`until` لها حدّ أمان 100000 خطوة.
- `once`/`lazy`/`memoize1` تحتفظ بحالتها في إغلاق (closure) خاص بكل نسخة.

## compose_schema — المخططات

المخطّط `map` له اختصارات تُحوَّل تلقائيًا (`schemaNormalize`):

| الاختصار | المعنى |
|---|---|
| `"number"` | نوع فقط (`valueKind`: `nil bool number string array map set function instance any`) |
| `Set(["a","b"])` | قيمة من مجموعة (enum) |
| `fun(v) {...}` | شرط مخصّص |
| `["string"]` | `array` عناصرها كلها بهذا المخطّط |
| `{"name": "string", "age": "number"}` | `map` بهذه الحقول، كلها مطلوبة (تقبل كائن `class` أيضًا) |

المفاتيح الكاملة: `type` `nullable` `required` `fields` `items` `min` `max` `length` `enum` `equals` `check` `oneOf` `allOf`
`default` `coerce` `message`. (`min`/`max` تقيس القيمة للأرقام والطول للنص/array/Set/map.)

| الدالة | الوظيفة |
|---|---|
| `schemaCheck(schema, v)` | `{"ok": bool, "errors": [{"path": "a.b", "message": "..."}]}` |
| `isValid(schema, v)` | `true`/`false` |
| `schemaConform(schema, v)` | يطبّق `default` و`coerce` ويُرجع `{"ok": true, "value": ...}` أو الأخطاء |
| `schemaAssert(schema, v)` | يُرجع `v` أو يرمي نص الأخطاء |
| `schemaErrors(result)` | `["path: message", ...]` |
| `schemaOf(v)` | مخطّط مُستنتَج من قيمة نموذجية |

```kotlin
let s = {"age": {"type": "number", "coerce": true}, "on": {"type": "bool", "coerce": true}};
print schemaConform(s, {"age": "42", "on": "yes"});   // {"ok": true, "value": {"age": 42, "on": true}}
print isValid({"oneOf": ["number", "string"]}, true);  // false
```

> `coerce` تعمل في `schemaConform` فقط؛ `schemaCheck` لا تُحوِّل شيئًا أبدًا.
> إن كان لحقلك اسم من كلمات المخطّط (`type` `fields` ...) فاستعمل الصيغة الكاملة `{"type": "map", "fields": {...}}`.

## compose_diff — الفروق والدمج الثلاثي

الفرق `map`: `{"op": "add"|"remove"|"change", "path": [...], "from": ..., "to": ...}`.

| الدالة | الوظيفة |
|---|---|
| `diffDeep(a, b)` | فروق من `a` إلى `b` (map/array/كائن بعمق، والباقي بـ `==`) |
| `patchDeep(x, changes)` | بنية جديدة بعد التطبيق |
| `invertChanges(changes)` | العكس: `patchDeep(b, invertChanges(diffDeep(a,b))) == a` |
| `changedPaths(a, b)` | المسارات فقط |
| `describeChanges(changes)` | `["+ a.b = 1", "- a.c", "~ a.d: 1 -> 2"]` |
| `merge3(base, mine, theirs)` | `{"value": ..., "conflicts": [مسارات]}` — عند التعارض تفوز `mine` |

```kotlin
let m = merge3({"a": 1, "b": 2}, {"a": 10, "b": 2}, {"a": 1, "b": 20});
print m;   // {"value": {"a": 10, "b": 20}, "conflicts": []}
```

## compose_store — مخزن الحالة

الحالة تتغيّر دائمًا عبر `setIn` (غير مُعدِّلة)، فكل حالة سابقة تبقى سليمة للتراجع.

| الدالة | الوظيفة |
|---|---|
| `storeNew(initial)` | ينشئ مخزنًا (`map` — لا تلمس حقوله مباشرة) |
| `storeGet(s,path)` `storeState(s)` | قراءة |
| `storeSet` `storeUpdate` `storeMerge` `storeReplace` | كتابة (تُسجَّل للتراجع وتُشعر المشتركين) |
| `storeSubscribe(s,path,fn)` / `storeUnsubscribe(s,id)` | `fn(new, old, path)` عند تغيّر قيمة المسار فقط |
| `storeComputed(s,name,fn)` / `storeRead(s,name)` | قيمة مشتقّة تُحسب عند الطلب وتُخزَّن حتى يتغيّر المخزن |
| `storeBatch(s,fn)` | إشعار واحد في النهاية بالمقارنة بين أول حالة وآخر حالة |
| `storeUndo` `storeRedo` `storeCanUndo` `storeCanRedo` `storeHistoryLength` | التراجع والإعادة |
| `storeToContainer(s,path,name,key)` | يدفع قيمة المسار إلى حقل حاوية كلما تغيّرت |
| `storeFromContainer(s,path,name,key)` | يراقب حقل حاوية ويكتب قيمه في المخزن |

```kotlin
let s = storeNew({"count": 0});
storeSubscribe(s, "count", fun(n, o, p) { print str(o) + " -> " + str(n); });
storeSet(s, "count", 1);      // 0 -> 1
storeUndo(s);                 // 1 -> 0
```

---

## حدود معروفة
- لا شيء هنا يضيف نوع قيمة جديدًا أو يغيّر سلوك Rin: كل شيء دوال و`map` وإغلاقات.
- `compose_store` لا تستعمل خيوطًا ولا مؤقتات؛ الإشعارات متزامنة داخل نداء `store…` نفسه.
- `diffDeep` على `array` تقارن بالفهرس (لا تكتشف النقل/إعادة الترتيب).
- لا تفعيل تلقائي بلا `@import`، حتى لا تتعارض الأسماء مع دوال المستخدم.
- بعد أي تعديل على ملفات `lib/compose*.og.rin` شغّل `python3 tools/embed_compose.py` (أو `--check` للتحقق فقط).

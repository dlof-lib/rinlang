# nite.moje — ما كانت تنقصه اللغة

مكتبة واحدة تجمع دوالّ صغيرة مشتركة **غير موجودة** لا في اللغة ولا في المكتبات الرسمية الأخرى (فُحصت الأسماء كلها
قبل إضافتها: ~2700 دالة مكتبات و~1400 دالة أصلية). مكتوبة بـ Rin خالصة، ومضمَّنة في المفسِّر فلا حاجة لنسخ ملف.
الاختبار: `tests/print/nite_moje.rin`.

```kotlin
@import "lib/nite.moje.og.rin";
@import "lib/nite.moje.og.rin" as nm;   // مع مساحة اسم
```

دوال Rin ثابتة العدد (لا وسائط افتراضية)، لذا الصيغ الاختيارية دالتان: `enumerate`/`enumerateFrom` وهكذا.
لا دالة تعدّل مدخلاتها؛ كلها تُعيد قيماً جديدة.

## مصفوفات
| الدالة | المثال ← النتيجة |
|---|---|
| `enumerate(a)` / `enumerateFrom(a, s)` | `["a","b"]` ← `[[0,"a"],[1,"b"]]` |
| `zipArrays(a, b)` / `unzipPairs(p)` | `[1,2],["x","y"]` ← `[[1,"x"],[2,"y"]]` وعكسها |
| `compact(a)` | يحذف `nil` فقط: `[1,nil,0]` ← `[1,0]` |
| `cumsum(a)` | `[1,2,3]` ← `[1,3,6]` |
| `windowed(a, n)` / `pairwise(a)` | `([1,2,3],2)` ← `[[1,2],[2,3]]` |
| `transpose(rows)` | `[[1,2],[3,4]]` ← `[[1,3],[2,4]]` |
| `rotate(a, k)` | `([1,2,3,4],1)` ← `[2,3,4,1]` (السالب يميناً) |
| `nth(a, i)` / `nthOr(a, i, d)` | فهرس سالب من النهاية؛ خارج المدى ← `nil`/`d` بلا خطأ |
| `interleave(a, b)` | `[1,2,3],["a","b"]` ← `[1,"a",2,"b",3]` |
| `frequencies(a)` | `["a","b","a"]` ← `{a: 2, b: 1}` |
| `keyBy(a, fn)` | يفهرس العناصر بمفتاح `fn(item)` |
| `rangeOf(a, b)` / `rangeStep(a, b, s)` | `[a, b)` بخطوة (تقبل السالب) |
| `fillArr(n, v)` · `toArray(v)` | n نسخة · تغليف قيمة مفردة (`nil` ← `[]`) |

## كائنات
| الدالة | الوصف |
|---|---|
| `mapValues(m, fn)` · `filterValues(m, fn)` | `fn(value, key)` |
| `invert(m)` · `toEntries(m)` | تبديل المفاتيح بالقيم · `[[k, v], …]` |
| `deepEqual(a, b)` | مساواة بالقيمة للمتداخل (مصفوفات وخرائط) |
| `tryGet(root, "a.b.0.c", fallback)` | قراءة آمنة بمسار: لا خطأ أبداً، والسالب فهرس من النهاية |

`tryGet` هي رفيقة `?.` من [`null_safe.md`](./null_safe.md) حين يكون المسار نصاً ديناميكياً.

## نصوص وأرقام وتدفّق
`words(s)` · `lines(s)` · `snakeCase` · `camelCase` · `kebabCase` · `substring(s, lo, hi)` / `substringFrom(s, lo)`
(بنمط JavaScript: سالب من النهاية وقصّ بلا خطأ) · `divmod(a, b)` ← `[خارج, باقي]` · `approxEqual(a, b)` / `approxWithin(a, b, eps)` ·
`pipeThrough(v, [f, g, h])` ≡ `h(g(f(v)))` · `once(fn)` (تنفيذ مرة واحدة).

> الاسم `pipe` كلمة محجوزة في Rin، لذلك سُمّيت `pipeThrough`.

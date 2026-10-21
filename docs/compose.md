# compose.md — تركيب ودمج وتفاعل كل مفاهيم Rin (مكتبة أساسية)

> كل مثال هنا جُرِّب فعليًا عبر المفسِّر (انظر `tests/verification/compose_basic.rin`).

لكل مفهوم في Rin طريقته: دالة، `map`، `array`، `Set`، `enum`، كائن `class`، حاوية، سجلات جدول.
`compose` جسر واحد بينها. مدمجة داخل المفسِّر، فتعمل على كل المنصات بلا ملفات:

```kotlin
@import "lib/compose.og.rin";   // أو  @import "compose";
```

## 1) التحويل الموحَّد
| الدالة | الوظيفة |
|---|---|
| `valueKind(x)` | `nil bool number string array map set function instance` (الـ`enum` يظهر `map`) |
| `asMap(x)` | map→نفسها · كائن→حقوله · array→`{index: v}` · Set→`{e: true}` |
| `asArray(x)` | Set→عناصرها · map/كائن→قيمها · nil→`[]` · غير ذلك→`[x]` |
| `asSet(x)` | map/كائن→مفاتيحها · وغير ذلك عبر `asArray` |
| `entriesOf(x)` / `entriesToMap(pairs)` | أزواج `[مفتاح, قيمة]` وعكسها |
| `asFn(x)` | دالة→نفسها · map/enum/كائن→بحث · array→فهرسة · Set→عضوية · قيمة→ثابت |

## 2) التركيب
كل مرحلة تمر عبر `asFn`، فالمراحل **أي مفهوم**:
```kotlin
print through(3, [inc, [10, 20, 30, 40, 50], dbl]);   // inc→4 ثم فهرسة→50 ثم dbl→100
print through("Red", [Color, lbl]);                     // enum كمرحلة
print branch(Set([1, 2]), dbl, inc)(2);                 // Set كشرط → 4
```
`compose(stages)` (من اليمين) · `chain(stages)` (من اليسار) · `through(x, stages)` · `juxt` · `branch(pred, f, g)` ·
`tap(f)` · `attempt(f, fallback)` · `partial1` · `flip2` · `identity` · `constant` · جبر الشروط
`allOf` `anyOf` `neitherOf` `negate`.

> `pipe` و`merge` كلمتان محجوزتان في Rin، ولذلك الأسماء `chain` و`combine`.

## 3) الدمج
سياسة المفتاح المتعارض: `"deep"` (الافتراضي: map بعمق، array لصق، Set اتحاد) · `"override"` · `"keep"` · أو دالة `fn(old, new, key)`.

| الدالة | الوظيفة |
|---|---|
| `combine(a, b)` | دمج عميق لأي نوعين من العائلة نفسها (map/كائن/array/Set/نص/رقم) |
| `mergeWith(a, b, policy)` | دمج map/كائن بسياسة → map جديدة |
| `mergeAll(list)` / `mergeAllWith(list, policy)` | دمج قائمة |
| `mergeInto(target, src)` / `mergeIntoWith(..., policy)` | دمج في المكان (map، array، Set، كائن) |
| `mergeContainers(dst, src)` / `mergeContainersWith(dst, src, overwrite)` | دمج حاوية في حاوية بالاسم |
| `joinRecords(left, right, key)` / `joinRecordsWith(..., policy)` | ضمّ سجلات جدول على مفتاح |
| `fieldsOf(x)` | حقول كائن أو حاوية (اسمها نص) كـ map |

## 4) التفاعل
`mapOver` و`filterOver` و`reduceOver` تعمل على أي نوع وتحفظ شكله (array→array، Set→Set، map→map، نص→نص).
الشرط في `filterOver` أي مفهوم: `filterOver([1,2,3], Set([1,3]))` → `[1, 3]`.

`connect(src, srcKey, dst, dstKey)` و`connectWith(..., transform)` يربطان حقلًا بحقل، وكل طرف كائن أو **اسم حاوية** (نص):
كائن→كائن، كائن→حاوية، حاوية→كائن، حاوية→حاوية. و`sync(a, ka, b, kb)` للاتجاهين.

## 5) token ↔ number
`numbersIn(src)` أرقام المصدر الحرفية، و`identifiersIn(src)` المعرّفات، عبر الـ Lexer الحقيقي.

## 6) أدوات عامة
| الدالة | الوظيفة |
|---|---|
| `sizeOf(x)` / `isEmptyValue(x)` | الحجم الموحَّد لأي مفهوم، وفحص الفراغ |
| `kindIs(x, kinds)` | هل نوع `x` أحد الأنواع (نص أو مصفوفة أنواع) |
| `toKind(x, kind)` | تحويل صريح إلى `map array set string number bool function` |
| `hasKey(x, k)` / `getOr(x, k, fallback)` | وجود وقراءة آمنة بمفتاح واحد من map/كائن/array/Set/نص |
| `keysOf(x)` / `valuesOf(x)` | مفاتيح وقيم موحَّدة (array→الفهارس، Set→العناصر) |
| `describeValue(x)` | وصف مقروء: `array(3)` `map(2)` `number` |
| `composeVersion()` / `composeLibraries()` | إصدار الطبقة وأسماء مكتباتها |

## مكتبات مساعدة
ست مكتبات مدمجة توسّع `compose` (كل واحدة تستورد `compose` بنفسها). التفصيل الكامل في [`compose-helpers.md`](./compose-helpers.md):

| المكتبة | الغرض |
|---|---|
| `compose_seq` | ترتيب (`sortBy`) وتجميع (`groupOver` `tally`) وتقطيع (`chunkOver` `windowOver`) وضمّ وبحث |
| `compose_path` | مسارات `"a.b.0"`: `getIn` `setIn` `updateIn` `deleteIn` `flattenKeys` `cloneDeep` |
| `compose_flow` | `curryN` `memoize1` `retry` `cond` `matchKind` وجبر `Result` (`safeChain` `traceChain`) |
| `compose_schema` | `schemaCheck` `schemaConform` `schemaOf` بمخططات مختصرة |
| `compose_diff` | `diffDeep` `patchDeep` `invertChanges` `merge3` |
| `compose_store` | `storeNew` + اشتراكات وقيم مشتقّة وbatch وتراجع وربط بالحاويات |

## حدود معروفة
- المتغيرات الحيّة (`gauge` `tape` `lens` `bell`) و`base`/`accept`/`pre`/`post` صياغات نحوية لا تُمرَّر كقيم، فلا تدخل هذه الطبقة.
- لا معاملات افتراضية في Rin: لكل دالة اختيارية نسخة مختصرة ونسخة `*With`.
- الدوال الأصلية (مثل `str`) لا تُمرَّر كقيم؛ غلّفها بدالة: `fun lbl(x) { return str(x); }`.
- لم تُفعَّل تلقائيًا بلا `@import`، حتى لا تتعارض أسماؤها (`sync` `identity`...) مع دوال المستخدم.

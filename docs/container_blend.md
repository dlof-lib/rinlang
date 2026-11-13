# دمج الحاويات وتكوين ميزة جديدة (`container.blend` · الوصفات)

لا مفهوم جديد في اللغة: هو الدمج الذي كانت مكتبة `compose` تؤدّيه للحقول فقط (`mergeContainers`)، منقولاً إلى
الحاوية كلها — **حقول + مستندات (doc) + صفوف جدول + ستايل + النوع** — مع سياسة تعارض وتقرير كامل.
المصدر أي مفهوم حاويات موجود: اسم حاوية، مصفوفة أسماء، `Containers.Group`، أو `Volume`.

```rin
let r = container.blend("account", ["auth", "billing"]);   // الهدف يُنشأ إن غاب
print r["fields"], r["conflicts"];                          // تقرير: ما دُمج وما تعارض
```

## `container.blend(target, sources, opts?)`
| الخيار | الأثر |
|---|---|
| `policy` | `"deep"` (الافتراضي: map بعمق، array لصق، Set اتحاد، والقيمة العادية الأحدث تفوز) · `"override"` · `"keep"` · `"error"` (يرفض عند أول تعارض) |
| `only` / `omit` | قائمة حقول تُدمج / تُستثنى |
| `prefix` / `prefixes` | بادئة لأسماء الحقول (لكل المصادر، أو `{مصدر: بادئة}`) لتفادي التعارض |
| `kind` | نوع الهدف إن أُنشئ (الافتراضي auto = نوع أول مصدر غير عادي) |
| `documents` / `rows` / `style` | `false` لتعطيل دمج المستندات / صفوف الجدول / الستايل |
| `dry` | تقرير بلا أي كتابة |

التقرير: `{ok, target, created, kind, policy, from, fields, merged, documents, rows, styled, conflicts:[{key, from, action}], dry}`
حيث `action` = `merged` · `override` · `keep`، والمستندات المتعارضة بمفتاح `doc:<id>`.

- كل شيء يُحسَب أولاً ثم يُعتمد دفعة واحدة: خطأ في منتصف الدمج (مثل `policy:"error"`) لا يترك الهدف نصف مكتوب.
- القيم تُنسخ نسخاً عميقاً: تعديل الهدف لاحقاً لا يمسّ المصادر.
- مستندات بنفس `id` تُدمج بالسياسة نفسها؛ صفوف الجدول تُلصق (إن كان الهدف جدولاً).
- الهدف إن كان ضمن المصادر يُعامل كأساس ولا يُدمج في نفسه.

## الوصفات: ميزة مسمّاة مركّبة من ميزات
```rin
container.recipe("identity", {from: ["auth"]});
container.recipe("commerce", {from: ["billing"], include: ["identity"], policy: "override"});
container.cook("commerce", "shop");        // يدمج auth ثم billing في shop
// تغيّر billing لاحقاً؟ أعد الطبخ فيتحدّث shop
```
`container.recipe(name, spec)` · `container.cook(name, target?)` · `container.recipes()` · `container.recipeInfo(name)` · `container.dropRecipe(name)`.
`spec` = `{from, target?, include?, ...opts}`؛ `include` وصفات أخرى تُضاف مصادرها أولاً (يُكشف الدوران). الوصفات في الذاكرة طوال تشغيل البرنامج.

## تكامل مع الموجود
`container.zip("shop", "shop.zip")` على نتيجة الدمج، وGroup كمصدر: `container.blend("whole", "suite")`.
الاختبار: `tests/print/container_blend.rin`.

---

# الدمج العام: لكل مفهوم، لا للحاويات فقط

نفس الفكرة (دمج مفاهيم موجودة لتكوين شيء جديد) متاحة لأي قيمة في Rin، بلا مفاهيم أو كلمات محجوزة جديدة:

| الدالة | الوظيفة |
|---|---|
| `blend(a, b, ...)` | يدمج قيماً من النوع نفسه: map **بعمق** · array لصق · Set اتحاد · نص لصق · رقم جمع · `instance` → map حقوله. `nil` يُهمَل |
| `blendWith(opts, a, b, ...)` | نفسه بخيارات: نص سياسة `"deep"`/`"override"`/`"keep"`/`"error"` أو map `{policy, unique, sep}` |
| `blendInto(target, a, ...)` | **في المكان**: map/array/Set/كائن تُعدَّل وتُعاد؛ واسم حاوية (نص) يُمرَّر إلى `container.blend` |
| `blendRecipe(name, {parts, include?, policy?, unique?, sep?})` · `blendCook(name)` | وصفة قيم مسمّاة تُنفَّذ بالدمج (`include` أولاً، والدوران مكشوف) |

```rin
let cfg = blend({theme: "dark", in: {x: 1}}, {lang: "ar", in: {y: 2}});          // map عميق
let all = blendWith({unique: true}, [1, 2], [2, 3]);                              // [1, 2, 3]
blendInto(settings, {size: 3});                                                   // تعديل في المكان
blendRecipe("base", {parts: [{theme: "dark"}]});
blendRecipe("full", {include: ["base"], parts: [{lang: "en"}], policy: "override"});
print blendCook("full");                                                          // {theme, lang}
```

- `override`/`keep` على map: لكل مفتاح على حدة (لا يُستبدل الـ map كله)؛ `deep`/`error` ينزلان داخل الـ map.
- القيم تُنسخ نسخاً عميقاً فلا تتأثر المصادر، وأنواع غير متوافقة (map مع array مثلاً) تعطي خطأ واضحاً.
- **الدوال** تُدمج أيضاً (انظر «الدمج الأقوى» أدناه): `blend(f, g)` = خط أنابيب.
- الاختبار: `tests/print/blend_any.rin`.

---

# الدمج في indsin

indsin مبني على قيم Rin (خرائط ألوان، توكنز، ثيمات)، فكل ما سبق يعمل فوقه مباشرة. وأضيفت له دالة دمج خاصة بالثيمات:

`indsin.blendTheme(name, sources, opts?)` — يكوّن **ثيماً جديداً** بدمج ثيمات موجودة: ألوان الأدوار الاثنا عشر + «شكل» الثيم (`radius` · `cardElevation` · `buttonElevation` · `hairline`)، ويسجّله في كتاب الأنماط فيصير متاحاً لـ `indsin.useTheme`/`setTheme`.

| الخيار | الأثر |
|---|---|
| `sources` | اسم ثيم أو مصفوفة؛ الألوان: **الأحدث يفوز** (`override`) أو الأول (`keep`)؛ الشكل: القيمة غير الافتراضية الأحدث (0/false = «غير مُعيَّن» فلا يمحو شكلاً سابقاً) |
| `colors` / `shape` | خذ كل الألوان / كل الشكل من ثيم بعينه (ألوان Ocean بشكل Aurora) |
| `policy` | `"override"` (الافتراضي) أو `"keep"` |
| `only` / `omit` | حقول مسموحة/مستثناة (`primary`… `radius` `hairline`…) |
| `overrides` | `{primary: "#FF6600", radius: 20}` تُطبَّق آخراً (لون غير صالح أو حقل مجهول = خطأ واضح) |
| `activate` | `true` لتفعيله فوراً |

```rin
indsin.blendTheme("OceanSoft", ["Ocean"], {shape: "Aurora"});                 // ألوان Ocean + شكل Aurora
indsin.blendTheme("Brand", ["Aurora"], {overrides: {primary: "#FF6600"}, activate: true});
let pal = blend(indsin.themeColors("Dark"), {primary: "#123456"});            // blend العام على خرائط indsin
```
يعيد map: `{name, sources, colors, radius, cardElevation, buttonElevation, hairline, score, active}` (`score` = فحص WCAG).

**داخل خصائص `@view`:** `blend`/`blendWith`/`blendCook` نقية فتصلح هناك، أما `blendInto`/`blendRecipe`/`indsin.blendTheme` فلها أثر جانبي (مثل `indsin.defineTheme`) فهي ممنوعة داخل الخصائص ومسموحة في البرنامج.
الاختبار: `tests/print/indsin_blend.rin`.


---

# الدمج الأقوى

كل ما يلي يعمل في `blendWith` و`blendIntoWith` و`blendCook`/`blendRecipe` و`container.blend` (للحقول والمستندات).

## قواعد لكل مسار — `rules`
```rin
blendWith({rules: {"hits": "sum", "tags": "union", "limits.max": "max", "stats.*": "sum", "name": "keep"}}, a, b);
```
المفتاح مسار منقّط (يقبل `*` و`?`)، والقيمة استراتيجية أو **دالة**:
`deep` · `override`/`last` · `keep`/`first` · `error` · `concat` (مصفوفات/نصوص مع `sep`/map) · `union` (مصفوفة بلا تكرار/Set) · `sum` · `max` · `min`.
القاعدة تُطبَّق حتى على قيمتين متساويتين (مثل `sum`)، واستراتيجية مجهولة أو غير منطبقة على النوع = خطأ واضح.

## سياسة بدالة — `policy: fn(old, new, path)`
```rin
fun pick(old, nw, path) { if (path == "score") { return old > nw ? old : nw; } return nw; }
blendWith({policy: pick}, {score: 7}, {score: 3});     // {score: 7}
```
تُستدعى عند كل تعارض حقيقي (الـ map تُنزَل داخلها أولاً)؛ وفي `container.blend` المسار = اسم الحقل (أو `doc:<id>`) والتقرير يسجّل `action: "rule"`.

## دمج الدوال
`blend(f, g)` أو `blendFn(mode, f, g, ...)` أو `blendWith({fn: mode}, f, g)` تنتج **دالة جديدة حقيقية**:
| mode | السلوك |
|---|---|
| `chain` (الافتراضي) | خط أنابيب يسار→يمين: الأولى تأخذ الوسائط، والباقي يأخذ ناتج السابقة |
| `all` | تُستدعى كلها بالوسائط نفسها وتُعاد مصفوفة النتائج |
| `first` | أول نتيجة غير `nil` |
| `each` | تُستدعى كلها وتُعاد نتيجة الأخيرة |

عدد وسائط الدالة الجديدة = أكبر عدد وسائط بين الأجزاء، وكل جزء يتلقى عدد وسائطه هو. (تُعرَّف الدالة باسم داخلي `__blendfn_N` في النطاق العام، ولذلك `blendFn` ممنوعة داخل خصائص `@view`.)

## `blendDiff(a, b)` — ماذا سيتغيّر؟
يقارن بعمق (map) ويعيد `{equal, same, added:[{path,to}], removed:[{path,from}], changed:[{path,from,to}]}`.

## `blendIntoWith(opts, target, a, ...)`
نفس `blendInto` بالخيارات أعلاه. الاختبار: `tests/print/blend_power.rin`.

---

# الترتيب الشامل: `compare` و`orderBy`

مفهوم المقارنة الثلاثية (أصغر/يساوي/أكبر) لأي قيمتين، ويتكامل مع الدمج (`max`/`min` في `rules` تعمل على أي نوع عبره).

`compare(a, b, opts?)` → `-1` | `0` | `1`. ترتيب كلي ثابت: `nil < bool < number < string < array < Set < map < instance`، وداخل النوع: أرقام بالقيمة · نصوص بايتياً · مصفوفات معجمياً ثم بالطول · Set/map بعد فرزها ثم بالحجم.

`orderBy(list, spec?, opts?)` → مصفوفة **جديدة** مرتّبة ثباتياً (الأصل لا يتغيّر؛ يقبل Set).
- `spec`: مسار `"a.b"` (بادئة `-` = تنازلي) · دالة مفتاح · `{by, desc}` · مصفوفة مواصفات (عدة مفاتيح).
- `opts` (وفي `compare` أيضاً): `caseless` · `natural` (`"a2" < "a10"`) · `nilsFirst` (الافتراضي `true`) · `desc` · و`by` في `compare`.

```rin
orderBy(people, ["-age", "n"]);                       // العمر تنازلياً ثم الاسم
orderBy(["a10", "a2", "a1"], nil, {natural: true});   // ["a1", "a2", "a10"]
compare({n: {v: 3}}, {n: {v: 9}}, {by: "n.v"});       // -1
blendWith({rules: {"ver": "min"}}, {ver: [1, 2]}, {ver: [1, 0, 9]});   // الأصغر بالترتيب الشامل
```
الاختبار: `tests/print/compare_order.rin`. وله معامل في اللغة: `a <!=g=?> b` (انظر docs/shorthand.md).

## العائلة الموسّعة

كلها فوق المحرّك نفسه ونفس مواصفات `orderBy` (`spec`: مسار / دالة / مصفوفة / `{by,desc}` — و`opts`: `caseless` `natural` `nilsFirst` `desc`):

| الدالة | الوظيفة |
|---|---|
| `relate(a, b, opts?)` | علاقة غنية: `{order, relation, less, equal, greater, identical, sameKind}` — `equal` ترتيبياً، و`identical` بتساوي القيم الحرفي |
| `inRange(x, lo, hi, opts?)` | داخل المدى لأي نوع؛ مغلق افتراضياً، `{exclusive:true}` لاستبعاد الطرفين؛ يقلب `lo/hi` تلقائياً |
| `isSorted(list, spec?, opts?)` | هل القائمة مرتّبة؟ `{strict:true}` يمنع التكرار |
| `sortedIndex(list, v, spec?, opts?)` | موضع الإدراج بالبحث الثنائي؛ `{right:true}` بعد المتساويات |
| `mergeSorted(lists, spec?, opts?)` | دمج ثباتي لقوائم مرتّبة (التعادل لصالح القائمة الأولى) |
| `rank(list, spec?, opts?)` | الرتب بترتيب العناصر الأصلي، المتعادلون يتشاركون الرتبة (`1,2,2,4`) |
| `topN(list, n, spec?, opts?)` | أكبر `n` عنصراً؛ `{desc:true}` تعكسها إلى أصغر `n` |
| `uniqueBy(list, spec?, opts?)` | أول عنصر من كل مجموعة متعادلة مع حفظ ترتيب الظهور |

```rin
topN(players, 3, "score");                    // الأعلى نتيجة
rank([30, 10, 20, 10]);                       // [4, 1, 3, 1]
uniqueBy(["a", "A", "b"], nil, {caseless: true});   // ["a", "b"]
sortedIndex([10, 20, 20, 30], 20);            // 1
```
الاختبار: `tests/print/compare_family.rin`.

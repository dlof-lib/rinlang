# Rin Candle (شمعة)

## الفكرة

`candle` طبقة جديدة تقع **تحت mask مباشرة**، وتعمل على طبقة `id` الداخلية التي يستخدمها Loom (`StrandId`)، لا على `name` ولا على `mask` نفسه.

النموذج الحالي الموثّق في `mask.md`:

```text
name  -> اسم المصدر / السجل
mask  -> الهوية المنطقية
id    -> المعرف الداخلي الذي يستخدمه Loom
```

`candle` يضيف طبقة رابعة فوق `id`:

```text
name   -> اسم المصدر / السجل
mask   -> الهوية المنطقية (اسم واحد ثابت لكل id)
id     -> المعرف الداخلي لـ Loom (StrandId)
candle -> علاقة id إلى id، غير محدودة الاتجاه الصادر (id -> ∞ من id)
```

بينما `mask` يمنح كل `id` **اسماً منطقياً واحداً**، `candle` يسمح لأي `id` أن **"يُشعل" عدداً غير محدود من الـ id الأخرى** — علاقة موجَّهة (source → target) بلا حد على عدد الأهداف.

**الاستعارة**: الشمعة لا تخسر شيئاً حين تُشعل شمعة أخرى. من `id` واحد يمكن إشعال (ربط) عدد لا نهائي من الـ id الأخرى، دون أن يُستهلك أو "ينطفئ" المصدر، وكل هدف مُشعَل يمكن بدوره أن يُشعل غيره — فتنتشر الشعلة عبر سلسلة/شجرة من العلاقات.

## الخصائص الأساسية

1. **موجَّهة (directed)**: `light(from, to)` — `from` يُشعل `to`.
2. **∞ من جهة الصادر (unbounded fan-out)**: لا حد مبرمج على عدد الأهداف التي يشعلها id واحد؛ الحد الوحيد هو الذاكرة المتاحة (∞ مفهومية، لا حسابية).
3. **مرتبطة بـ mask**: أي طرف في `light`/الاستعلامات يُقبل إما كـ mask (يُحل إلى id عبر `findMask`/`maskTarget` الحاليين) أو كمعرّف داخلي مباشر — لا حاجة لتعديل mask نفسه.
4. **قابلة للتسلسل (chainable)**: الهدف المُشعَل يمكن أن يصبح مصدراً لإشعال أهداف أخرى، فيتكوّن رسم بياني/شجرة انتشار.
5. **جذور وأوراق**: عناصر لم تُشعَل قط (جذور الشعلة الأصلية) وعناصر أُشعلت ولا تُشعل غيرها (أطراف السلسلة).
6. **علاقة اختيارية موسومة**: كل وصلة يمكن أن تحمل وسماً نصياً (مثل `"contains"`, `"inherits"`, `"watches"`) بنفس روح `maskTag`.

## الصيغة المقترحة

نفس نمط `mask` بالضبط، لكن كاستدعاءات دوال لا كخاصية تصريحية (لأن candle علاقة بين عنصرين، لا صفة لعنصر واحد):

```rin
@view.Column="screen"
mask="main-screen";
.end/view

@element.button="saveButton"
mask="save-button";
.end/element

light("main-screen", "save-button", "contains");
```

## API المقترح (يحاكي أسلوب mask API الحالي)

### الإشعال والإطفاء

- `light(fromMaskOrId, toMaskOrId, relation?)` — ينشئ وصلة candle من from إلى to، مع وسم علاقة اختياري.
- `extinguish(fromMaskOrId, toMaskOrId)` — يحذف وصلة محددة.
- `extinguishAll(maskOrId)` — يطفئ كل الوصلات **الصادرة** من عنصر.

### الاستعلام المباشر

- `candleExists(from, to)` — هل توجد وصلة مباشرة؟
- `candleTargets(maskOrId)` — كل ما أُشعل من هذا العنصر مباشرة (يمكن أن تكون قائمة كبيرة جداً/∞ عملياً).
- `candleSources(maskOrId)` — من أشعل هذا العنصر مباشرة.
- `candleCount(maskOrId)` — عدد الوصلات الصادرة.
- `candleInfo(maskOrId)` — ملخص: `{targets, sources, isRoot, isLeaf}`.

### البنية والانتشار

- `candleChain(from, to)` — بحث (BFS مع set زيارة لمنع الحلقات) هل `to` قابل للوصول من `from` عبر سلسلة إشعالات، ويعيد المسار إن وُجد.
- `candleDepth(maskOrId)` — أقصر عدد قفزات من أقرب جذر.
- `candleRoots()` — عناصر تُشعل غيرها فقط، ولم تُشعَل من أحد.
- `candleLeaves()` — عناصر أُشعلت فقط، ولا تُشعل غيرها.
- `candleTree(maskOrId)` — تمثيل شجري كامل بدءاً من عنصر كجذر.

### العلاقات الموسومة

- `candleRelation(from, to)` — تعيد وسم العلاقة الذي مُرِّر إلى `light()`، أو `nil`.
- `candleByRelation(relation)` — كل الوصلات الموسومة بعلاقة معيّنة.

## قواعد السلامة

- **الحلقة الذاتية**: `light(x, x)` يُرفض افتراضياً (يتطلب علماً صريحاً لاحقاً إن احتيج فعلاً).
- **الدورات (cycles)**: `candleChain`/`candleDepth`/`candleTree` تستخدم `visited set` أثناء الـ BFS لتفادي أي تكرار لانهائي فعلي حتى لو كوّن المستخدم دورة في الرسم البياني (a يشعل b يشعل a).
- **∞ مفهومية فقط**: لا حد صريح مبرمج على `candleTargets`، لكنها تبقى محكومة عملياً بذاكرة الجهاز — تماماً كأي `unordered_map`/`vector` آخر في المحرك.

## مبدأ التصميم

`candle` لا يستبدل `mask` ولا `id`؛ هو طبقة علاقات إضافية فوقهما:

```text
name   -> يعرّف الكائن في بنية المصدر
mask   -> يعرّف الكائن بهوية منطقية مستقرة (واحد لكل id)
id     -> المعرف الداخلي الذي يستخدمه Loom
candle -> يربط id بعدد غير محدود من الـ id الأخرى، ويُقرأ/يُكتب عبر mask
```

## التنفيذ الفعلي

تم التنفيذ في المحرّك الأصلي (لا كمكتبة `.rin` منفصلة، لأن `light`/`candle*` دوال مدمجة native
تماماً كـ `mask*`):

- `app/src/main/cpp/rin_candle.h` (ملف جديد): سجل عمومي `rin::CandleRegistry` مستقل تماماً عن
  `rin_interpreter.*` — لا يعرف شيئاً عن `mask` أو `Value`. مفاتيحه نصوص (`std::string`) وليست
  `StrandId` خام، لأنه لا يوجد أصلاً أي `StrandId` مكشوف لسكربت Rin على مستوى الـ natives
  الحالية (حتى `mask*` نفسها تعمل على أسماء container/group/volume المُحلولة، لا على
  `Strand::id` الخام في `rin_loom_strand.h`). النموذج المفهومي في الأعلى (`id -> id`) يبقى
  صحيحاً؛ التمثيل العملي هو نفس "المعرّف النهائي بعد الحل" الذي تستخدمه mask v3/v4 فعلياً.
  - `outgoing: unordered_map<std::string, vector<CandleEdge>>`
  - `incoming: unordered_map<std::string, vector<std::string>>` (لتسريع `candleSources`)
  - `CandleEdge { std::string to; std::string relation; }`
- `app/src/main/cpp/rin_interpreter.h`: عضو واحد جديد `rin::CandleRegistry candleRegistry;` في
  `Interpreter` (بجانب أعضاء `mask*` الحالية).
  - `app/src/main/cpp/rin_interpreter.cpp` (داخل `registerNatives()`، مباشرة بعد قسم mask v4):
  - `candleResolve(token)`: يعيد استخدام `maskResolveInternal` + `maskTarget4` الموجودتين
    فعلاً لحلّ mask v3/v4 — mask معروف يُحل إلى targetه، وإلا يُعامَل النص كمعرّف خام كما هو.
  - كل الدوال المدمجة (`light`, `extinguish`, `extinguishAll`, `candleExists`, `candleTargets`,
    `candleSources`, `candleCount`, `candleInfo`, `candleRelation`, `candleByRelation`,
    `candleChain`, `candleDepth`, `candleRoots`, `candleLeaves`, `candleTree`) مسجَّلة في
    `natives[...]` بنفس النمط الحرفي لدوال `mask*` المجاورة لها (لا في `rin_stdlib_libs.h`؛ ذلك
    الملف يحتضن فقط سكربتات `.rin` مضمَّنة قابلة للاستيراد مثل `lib/movingmask.og.rin`، وليس
    تسجيل دوال native — نفس ما يفعله `maskkit.og.rin`/`movingmask.og.rin` فوق `mask*` الأصلية).
  - `candleTree` تُرجع خريطة متداخلة `{id, relation, children:[...]}` عبر `Value::makeMap`.
- مثال كامل: `examples/candle.rin` (على غرار `examples/mask.rin`).

## ملخص القدرات

| القدرة | الوصف |
|---|---|
| id إلى id | علاقة موجَّهة مباشرة بين معرّفين داخليين |
| ∞ من id | لا حد على عدد الأهداف الصادرة من عنصر واحد |
| مرتبط بـ mask | كل طرف يُقبل كـ mask ويُحل تلقائياً إلى id |
| قابل للتسلسل | الأهداف يمكن أن تصبح مصادر لسلسلة/شجرة انتشار |
| آمن من الحلقات | BFS بحماية visited set في كل استعلامات المسار |

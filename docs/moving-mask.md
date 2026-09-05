# Moving Mask — أقنعة متحركة فوق الحاويات والحلقات

> راجع أيضاً: [`mask.md`](./mask.md) لنظام القناع (mask) الأصلي الساكن الذي تُبنى فوقه هذه
> المكتبة، و[`standard-library.md`](./standard-library.md) للخريطة العامة لمكتبات Rin.

**الوحدة:** `lib/movingmask.og.rin`
**الاستيراد:**
```rin
@import "lib/movingmask.og.rin";
```

## الفكرة

نظام `mask` الأصلي في Rin (انظر [`mask.md`](./mask.md)) يمنح كل حاوية/مجموعة/حجم **هوية
منطقية ثابتة**: اسم، نوع، أب، أبناء، وسوم... لكنها هوية *ساكنة* — لا موضع لها، ولا سرعة،
ولا مفهوم لِـ"الآن" أو "لاحقاً".

**Moving Mask** يضيف طبقة حركية فوق تلك الهوية: قناع يملك **موضعاً وسرعة وتسارعاً**،
يتحرك عبر الزمن ضمن **حلقة (loop)**، وينتقل بين **الحاويات (containers)** — إضافة إلى
معنى ثانٍ كلاسيكي لعبارة "قناع متحرك" مستعار من معالجة الإشارات/الصور: **نافذة صغيرة
(kernel/stencil) تنزلق فوق حاوية بيانات أكبر** (مصفوفة أو شبكة).

المكتبة مستقلة بذاتها بالكامل (لا تعتمد على أي `lib/*.og.rin` آخر)، وحالتها صريحة دائماً:
تُنشئ محرّكاً بـ `mm_new()` وتُمرّره لكل دالة — بلا أي حالة عامة مخفية، بنفس روح
[`lib/cachekit.og.rin`](../lib/cachekit.og.rin).

## مثال سريع

```rin
@import "lib/movingmask.og.rin";

let world = mm_new();
mm_setBounds(world, 0, 0, 390, 700);   // حدود شاشة @loop=canvas مثلاً
mm_spawn(world, "player", 20, 20);
mm_setVelocity(world, "player", 5, 2);

mm_tick(world, 1, true);                // خطوة زمنية واحدة، ارتداد عند الحدود
print mm_position(world, "player");     // {x: 25, y: 22}
```

## المفاهيم الأساسية التي تضيفها المكتبة

### 1) القناع الحركي (Kinetic Mask)
كل قناع مسجَّل في محرّك عبر `mm_spawn(mm, name, x, y)` يملك سجلاً كاملاً: موضع، سرعة،
تسارع، حالة نشاط، مسار، مدار، أثر، وسوم، بيانات حرّة (`meta`). هذا هو الفرق الجوهري عن
قناع Rin الأصلي: الأخير هوية فقط، وهذا هوية + **حالة متغيّرة عبر الزمن**.

- `mm_new()` / `mm_spawn` / `mm_destroy` / `mm_exists` / `mm_count` / `mm_names`
- `mm_setActive` / `mm_isActive` — تعطيل مؤقت بلا حذف
- `mm_addTag` / `mm_hasTag` / `mm_withTag` — تصنيف الأقنعة المتحركة نفسها
- `mm_setMeta` / `mm_getMeta` — بيانات تطبيق حرّة (صحة، مالك، نوع...)

### 2) الفيزياء الأساسية (موضع/سرعة/تسارع)
نموذج تكامل فيزيائي مبسّط (`v += a·dt`, `pos += v·dt`) يُطبَّق دورة بعد دورة:

- `mm_position` / `mm_setPosition` / `mm_translate`
- `mm_velocity` / `mm_setVelocity`
- `mm_acceleration` / `mm_setAcceleration` / `mm_applyForce`
- `mm_stop` — تصفير السرعة والتسارع دفعة واحدة
- `mm_integrate` (قناع واحد) و`mm_integrateAll` (كل الأقنعة النشطة دفعة واحدة)

### 3) المسارات ونقاط الطريق (Paths / Waypoints)
مسار = مصفوفة نقاط `{x,y}` يتبعها القناع نقطة فنقطة، بسرعة محدودة لكل خطوة:

- `mm_setPath(mm, name, points, loopFlag)` / `mm_resetPath` / `mm_pathDone` / `mm_pathProgress`
- `mm_followPath(mm, name, speed)` — خطوة واحدة نحو نقطة الطريق الحالية

### 4) أنماط حركة جاهزة (Motion Patterns)
سلوكيات توجيه ذاتي (steering) ونمذجة حركة كلاسيكية، جاهزة للاستخدام المباشر:

| الدالة | السلوك |
|---|---|
| `mm_seek(mm, name, tx, ty, speed)` | التوجّه المباشر نحو هدف |
| `mm_flee(mm, name, dx, dy, speed)` | الابتعاد عن نقطة خطر |
| `mm_patrol(mm, name, ax, ay, bx, by, speed)` | دورية ذهاباً وإياباً بين نقطتين |
| `mm_setOrbit` + `mm_orbitStep` | حركة مدارية حول مركز بنصف قطر ثابت |
| `mm_wander(mm, name, jitter)` | تجوال عشوائي (جسيمات، حركة خلفية) |

### 5) الحدود والمناطق (Bounds / Regions)
حدود عالم واحدة عامة (`mm_setBounds`)، وعدد حرّ من المناطق المُسمّاة الإضافية:

- `mm_inBounds` / `mm_clampToBounds` / `mm_bounceAtBounds` (تُطلق حدث `"bounce"`)
- `mm_setRegion` / `mm_removeRegion` / `mm_inRegion` / `mm_regionsContaining`

### 6) التكامل مع الحاويات (Containers) — القلب من الطلب
طبقتان متكاملتان:

**أ) عضوية منطقية** — دِلاء تجميع حرّة داخل المحرّك نفسه، تعمل دوماً بلا شرط:
`mm_attach` / `mm_detach` / `mm_transfer` / `mm_containerOf` / `mm_membersOf` /
`mm_containerSize` / `mm_containerNames` (تُطلق أحداث `"enterContainer"`/`"leaveContainer"`).

**ب) جسر مع الحاويات الفعلية في Rin** — عبر الدوال الأصلية `hasContainer`/`setField`/
`getField`/`childrenOf`:
- `mm_syncToNativeContainer(mm, name, containerName)` — يكتب `mm_x`/`mm_y`/`mm_vx`/`mm_vy`
  كحقول فعلية على حاوية `@container` حقيقية، فيصبح موضع القناع المتحرك قابلاً للقراءة من
  أي كود Rin آخر لا يعرف شيئاً عن movingmask، عبر `getField(containerName, "mm_x")` العادية.
- `mm_syncFromNativeContainer` — الاتجاه المعاكس.
- `mm_transferNativeContainer` — نقل فعلي بين حاويتين حقيقيتين في استدعاء واحد.
- `mm_movingChildrenOf(mm, containerName)` — تقاطع شجرة الحاويات الفعلية (`childrenOf`)
  مع عالم الأقنعة المتحركة.
- `mm_identityOf` / `mm_attachByIdentity` — جسر اختياري مع نظام `mask` الأصلي
  (`findMask`/`maskExists`/`maskTarget`)، للانضمام منطقياً لنفس حاوية قناع هوية موجود.

### 7) القناع المنزلق فوق المصفوفات (Sliding Window / 1D Convolution)
المعنى الكلاسيكي الآخر لـ"moving mask": نافذة ثابتة الحجم تنزلق خانة خانة فوق مصفوفة:

- `mm_windowAt(arr, centerIndex, radius)` — نافذة آمنة حول فهرس (تُقصّ عند الأطراف)
- `mm_slideOverArray(arr, windowSize, fn)` — محرّك الانزلاق العام؛ `fn(slice, startIndex)`
- `mm_movingSum` / `mm_movingMax` / `mm_movingMin` (تكميلاً لـ`movingAverage` الأصلية)
- `mm_convolve1D(arr, kernel)` — التفاف حقيقي بنواة أوزان صغيرة

### 8) القناع المنزلق فوق الشبكات (Grid Stencil / 2D Convolution)
نفس الفكرة بُعدين (اتفاقية `grid[row][col]` من `gridkit.og.rin`) — الاستخدام الأكلاسيكي في
معالجة الصور (بلور/حواف) وأتمتة الخلايا ولوحات الألعاب:

- `mm_gridWindowAt` / `mm_forEachGridWindow` / `mm_convolve2D`
- `mm_gridStamp` / `mm_gridClearStamp` / `mm_gridMoveStamp` — "ختم" شكل كامل (بصمة/قطعة)
  فوق شبكة عند موضع مُعيَّن، ومحوه/نقله — التطبيق الحرفي لـ"تحريك قناع على لوحة"

### 9) الأثر والتاريخ (Trail / History)
كل حركة فعلية تُسجِّل الموضع الجديد تلقائياً (بحد أقصى `historyLimit` نقطة):

- `mm_trail` / `mm_clearTrail` / `mm_setHistoryLimit`
- `mm_distanceTraveled` — المسافة الفعلية المقطوعة (لا نسبة نقاط الطريق فقط)
- `mm_undoLastMove` — تراجع بخطوة واحدة (يتطلّب حركتين مسجَّلتين على الأقل)

### 10) القرب والتصادم (Proximity / Collision)
- `mm_distanceTo` / `mm_isNear`
- `mm_collidesAABB` — تصادم صندوقي محاذٍ للمحاور (مستطيلان بعرض/ارتفاع)
- `mm_nearestTo` — أقرب قناع آخر نشط

### 11) التكامل مع الحلقات (Loop Integration) — القلب الثاني من الطلب
- `mm_tick(mm, dt, bounceMode)` — دورة واحدة كاملة (فيزياء + حدود + حدث `"tick"`)
- `mm_runLoop(mm, steps, dt, bounceMode, fn)` — حلقة `while` جاهزة تُشغِّل عدّة دورات
- `mm_animateTo(mm, name, tx, ty, steps, easingFn)` — حركة مُتحكَّم بها عبر عدد دورات،
  مع دالة تسهيل (easing) اختيارية
- `mm_forEach` / `mm_forEachInContainer` — حلقات جاهزة فوق كل الأقنعة أو أعضاء حاوية واحدة

### 12) الأحداث (Event Hooks)
نظام استماع خفيف الوزن: `mm_on(mm, eventName, fn)` / `mm_off`، مع أحداث جاهزة تُطلقها
المكتبة نفسها: `"enterContainer"`, `"leaveContainer"`, `"bounce"`, `"tick"`.

### 13) الفحص والتلخيص (Inspection)
`mm_describe` (قناع واحد)، `mm_summary` (إحصاء عام)، `mm_toString` (كل الأقنعة كنص).

## يتكامل طبيعياً مع

- [`lib/loopkit.og.rin`](../lib/loopkit.og.rin) — مرّر دالة صغيرة تستدعي `mm_tick` إلى
  `repeatTimes`/`stepLoop`.
- [`lib/iterkit.og.rin`](../lib/iterkit.og.rin) — لفّ `mm_names(world)` بمُكرِّر `iterNew`.
- [`lib/gridkit.og.rin`](../lib/gridkit.og.rin) — استخدم `makeGrid` كحاوية لأقسام الشبكة هنا.
- [`lib/animation.og.rin`](../lib/animation.og.rin) — دوال Easing جاهزة لـ`mm_animateTo`.
- [`lib/maskkit.og.rin`](../lib/maskkit.og.rin) و[`mask.md`](./mask.md) — طبقة الهوية
  الساكنة تحت هذه الطبقة الحركية.

## انظر أيضاً

- [`mask.md`](./mask.md) — نظام القناع الأصلي (هوية ساكنة).
- [`standard-library.md`](./standard-library.md) — خريطة كل مكتبات Rin القياسية.
- [`../examples/moving_mask_demo.rin`](../examples/moving_mask_demo.rin) — مثال تشغيلي كامل.

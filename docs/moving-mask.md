# Moving Mask — أقنعة متحركة فوق الحاويات والحلقات

> راجع أيضاً: [`mask.md`](./mask.md) لنظام القناع (mask) الأصلي الساكن الذي تُبنى فوقه هذه
> المكتبة، و[`standard-library.md`](./standard-library.md) للخريطة العامة لمكتبات Rin.

**الوحدة:** `lib/movingmask.og.rin`
**الإصدار:** `1.2.0` (مدمجة embedded داخل مفسّر RinStudio — انظر
[`CHANGELOG.md`](../CHANGELOG.md))
**الاستيراد:**
```rin
@import "lib/movingmask.og.rin";
```

**مدمجة في تطبيق RinStudio:** هذه المكتبة متاحة فوراً من داخل شاشة "المكتبات" (زر
"المكتبات" في المحرِّر ← تبويب المكتبات القياسية) بلا حاجة لرفعها يدوياً — اضغط "إدراج"
ليُضاف سطر `@import` تلقائياً عند مكان المؤشر. رقم إصدارها الحالي متاح برمجياً عبر
`mm_version()`.

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

### 14) التكامل مع Loom (Warp / Strand / Fabric / Needle / Shuttle)

محرّك Loomtime (`app/src/main/cpp/loom/*.h`) نظام C++ مستقل تماماً بمفرداته الخاصة: **Fabric**
(شجرة عرض حيّة من Strand)، **Warp** (خلايا حالة تفاعلية يقرأها `@view.*` تلقائياً)، **Needle**
(محرّك اللمس/الإصابة)، و**Shuttle** (مقارنة شجرتين وإنتاج `Patch[]`). movingmask لا يستدعي ذلك
المحرّك مباشرة ولا يُعدِّله — يبقى ملفاً Rin خالصاً كبقية هذه المكتبة — لكنه يستعير نفس
المفاهيم والمفردات على مستوى بياناته الخاصة، لتسهيل ربط عالم الأقنعة المتحركة يدوياً بواجهة
Loomtime حقيقية:

| مفهوم Loom | دالة movingmask | ماذا تفعل |
|---|---|---|
| **Warp** (خلايا حالة) | `mm_warpFieldNames(prefix)` | أسماء خلايا `warp` المقترحة لبادئة معيّنة |
| | `mm_warpFields(mm, name, prefix)` | يُصدِّر x/y/vx/vy/active لقناع كحقول مُسطَّحة |
| **Strand** (نوع بصري) | `mm_setStrandKind` / `mm_strandKind` | تسمية بصرية اختيارية للقناع (Text/Image/Button/Card/Icon/Box...) |
| **Fabric** (شجرة عرض) | `mm_toFabric(mm)` | لقطة مسطَّحة بكل الأقنعة (name/kind/x/y/active/tags) |
| **Needle** (لمس/إصابة) | `mm_needleHit(mm, x, y, radius)` | أقرب قناع نشط ضمن دائرة إصابة حول نقطة |
| | `mm_dispatchNeedle(mm, x, y, radius)` | ينفّذ الإصابة أعلاه ويُطلق حدث `"needleTap"` |
| **Shuttle** (تفاضل) | `mm_snapshot(mm)` | لقطة خفيفة (name/x/y/active) لكل الأقنعة |
| | `mm_shuttleDiff(mm, previousSnapshot)` | يقارن باللقطة الحالية ويُعيد `Patch[]`‏: `"Insert"`/`"Remove"`/`"Move"` |
| **Actions** (Action Engine) | `mm_defineAction(mm, actionName, fn)` | يُسجِّل فعلاً مخصَّصاً `fn(mm, name, payload)` باسم |
| | `mm_applyAction(mm, name, actionName, payload)` | يُطبِّق فعلاً مخصَّصاً (إن وُجد) وإلا فعلاً جاهزاً: `activate`/`deactivate`/`toggleActive`/`stop`/`teleport`/`nudge`/`addTag`/`removeTag` |
| **Navigation** (مشاهد) | `mm_navigate(mm, sceneName)` | ينتقل لمشهد جديد (يدفع الحالي إلى مكدّس)، يُنشِّط أقنعته ويُعطِّل مشاهد أخرى معروفة |
| | `mm_navBack(mm)` / `mm_navReplace(mm, s)` / `mm_navReload(mm)` / `mm_currentScene(mm)` | تراجع/استبدال/إعادة تطبيق/استعلام عن المشهد الحالي |
| **Overlay** (طبقة علوية) | `mm_setOverlay(mm, name, flag)` / `mm_isOverlay` | يضع/يستعلم عن وسم "overlay" الخاص |
| | `mm_needleHitTopmost(mm, x, y, radius)` | يفحص طبقة overlay أولاً، ثم يقع على `mm_needleHit` العادية |
| **Dye** (لون) | `mm_setColor(mm, name, hex)` / `mm_color(mm, name, fallback)` | لون بصري اختياري للقناع |
| **Theme** (Pattern Book) | `mm_defineTheme(mm, themeName, colors)` / `mm_setActiveTheme` / `mm_activeTheme` | لوحة ألوان مُسمّاة نشطة على مستوى المحرّك، تُطلق `"themeChanged"` |
| | `mm_themeColor(mm, roleName, fallback)` | لون دور (`primary`, `danger`...) من الـTheme النشط |
| **Object Inspector** | `mm_toObjectInspector(mm, name)` | بطاقة `{id, fields: [...]}` كاملة لقناع واحد (تشمل meta الحرّة) |

**قيد مهم:** Loomtime لا يوفّر حالياً تعيين خلية `warp` بالاسم النصّي ديناميكياً من كود Rin
عادي (لا يوجد `eval`/`setGlobal` في stdlib)، لذا الإسناد النهائي `اسم_الخلية = القيمة;` يبقى
بيد المستخدم كل دورة — `mm_warpFields` يُجهِّز القيم فقط. مثال نمط الاستخدام الموصى به:

```rin
@import "lib/movingmask.og.rin";

warp player_x = 20;
warp player_y = 20;

let world = mm_new();
mm_spawn(world, "player", 20, 20);
mm_setVelocity(world, "player", 5, 2);

fun onTick() {
    mm_tick(world, 1, true);
    let f = mm_warpFields(world, "player", "player");
    player_x = f["player_x"];
    player_y = f["player_y"];
}
```

مثال ثانٍ يجمع المشاهد (Navigation) مع الطبقة العلوية (Overlay) وTheme:

```rin
mm_addTag(world, "menuButton", "menu");
mm_addTag(world, "playerCharacter", "game");
mm_navigate(world, "menu");   // ينشّط menuButton، يُعطِّل playerCharacter

mm_setOverlay(world, "helpTooltip", true);
let hit = mm_needleHitTopmost(world, tapX, tapY, 24);  // يُصاب helpTooltip دوماً أولاً

mm_defineTheme(world, "Midnight", { primary: "#7C5CFF", danger: "#D14545" });
mm_setActiveTheme(world, "Midnight");
mm_setColor(world, "menuButton", mm_themeColor(world, "primary", "#000000"));
```

### 15) التكوين الجماعي والانسيابية (Flocking / Rigid Formations) — جديد في 1.0.0
Boids الكلاسيكية (Craig Reynolds) فوق قناع واحد مقابل مصفوفة جيران، بالإضافة لتشكيلات
جامدة (إزاحة ثابتة تابع↔قائد):

- `mm_flockSeparation` / `mm_flockAlignment` / `mm_flockCohesion` — قوى توجيهية فردية،
  تُطبَّق مباشرة عبر `mm_applyForce` وتُعاد أيضاً كـ `{x, y}`
- `mm_flockStep(mm, name, others, desiredSeparation, maxForce, weights)` — يُركِّب الثلاثة
  بأوزان `{separation, alignment, cohesion}` دفعة واحدة
- `mm_setFormationOffset(mm, follower, leader, dx, dy)` / `mm_clearFormationOffset` —
  إزاحة ثابتة لتابع عن قائده
- `mm_applyFormations(mm)` — يُحرِّك كل الأتباع إلى موضع قادتهم + إزاحتهم دفعة واحدة

### 16) آلة حالات محدودة لكل قناع (Finite State Machine) — جديد في 1.0.0
حالة + جدول انتقالات مخزَّنان في `meta` كل قناع — بلا سلاسل `if` متكرّرة لسلوك idle/chase/
attack أو أي دورة حياة أخرى:

- `mm_fsmDefine(mm, name, initialState)` — يُهيّئ آلة حالة على قناع
- `mm_fsmAddTransition(mm, name, fromState, event, toState)` — يُسجِّل انتقالاً
- `mm_fsmFire(mm, name, event)` — يُطلق حدثاً؛ ينتقل إن وُجد انتقال مطابق، ويُطلق حدث محرّك
  `"fsmChanged"`
- `mm_fsmState` / `mm_fsmIs` — استعلام عن الحالة الحالية

### 17) التسلسل والاستعادة (Serialization / Save & Load) — جديد في 1.0.0
حفظ/استعادة حالة محرّك كامل عبر JSON (`jsonEncode`/`jsonDecode` الأصليتين):

- `mm_toPlainData(mm)` — بيانات خام قابلة للتسلسل (بلا handlers/actions/themes الدالية)
- `mm_serialize(mm)` — نص JSON كامل، جاهز للكتابة على القرص
- `mm_deserialize(jsonText)` — يبني محرّكاً جديداً تماماً من نص محفوظ
- `mm_deserializeInto(mm, jsonText)` — يستعيد بيانات الأقنعة داخل محرّك قائم، محتفظاً
  بـ handlers/actions المُسجَّلة عليه مسبقاً

**قيد:** الدوال المسجَّلة (`mm_on`, `mm_defineAction`) لا تُسلسَل — يبقى إعادة تسجيلها بعد
الاستعادة مسؤولية المستخدم، تماماً كقيد خلايا `warp` في القسم 15.

### 18) الفهرسة المكانية لتسريع استعلامات الجوار (Spatial Grid Index) — جديد في 1.0.0
يقسم `mm_nearestTo`/`mm_needleHit` العالمَ بحثاً خطياً O(n) في كل استعلام؛ الفهرس المكاني
هنا يقسم العالم إلى خلايا شبكية بحجم `cellSize` فيجعل البحث عن الجيران يفحص فقط الخلايا
المجاورة الثماني:

- `mm_buildSpatialIndex(mm, cellSize)` — يبني/يُعيد بناء الفهرس من الأقنعة النشطة حالياً
  (لقطة لحظية؛ أعد بناءه كل دورة بعد التحريك)
- `mm_spatialNeighbors(mm, name, radius)` — الجيران الفعليون ضمن `radius`، بالبحث في
  الخلية الحالية وجيرانها الثماني فقط

### 19) المؤقتات والتهدئة لكل قناع (Timers / Cooldowns) — جديد في 1.0.0
مؤقتات مُسمّاة تُعَدّ تنازلياً بوحدة دورات، مخزَّنة في `meta` كل قناع:

- `mm_setTimer(mm, name, timerName, durationTicks)`
- `mm_isTimerActive` / `mm_timerRemaining`
- `mm_tickTimers(mm, dt)` — يُنقِص كل المؤقتات، ويُطلق حدث محرّك `"timerDone"` عند البلوغ

### 20) قياس الإطارات في الثانية وخطوة زمنية ثابتة (FPS Meter / Fixed Timestep) — جديد في 1.1.0
كل دوال المكتبة "منطقية" بحتة (تأخذ `dt` كوسيط بلا قراءة ساعة نظام فعلية — لا يوفّر مفسّر
Rin دالة وقت أصلية). هذا القسم يبني *فوق* `dt` الذي يمرّره التطبيق المضيف عدّاداً لـFPS
الفعلي، ومُجمِّعاً لخطوة زمنية ثابتة (النمط القياسي في محركات الألعاب لفصل الفيزياء
المحدَّدة عن معدّل العرض المتغيّر):

- `mm_fpsInit(mm)` / `mm_fpsUpdate(mm, dtSeconds)` — تُحدَّث القيمة كل نافذة نصف ثانية
  (`MM_DEFAULT_FPS_WINDOW`) لقراءة مستقرة بلا اهتزاز
- `mm_fps(mm)` / `mm_fpsFrameCount(mm)` / `mm_fpsReset(mm)`
- `mm_fixedStepInit(mm, fixedDt)` — يهيّئ مُجمِّعاً بخطوة ثابتة (مثلاً `1/60`)
- `mm_fixedStepRun(mm, realDt, maxSteps, stepFn)` — يستدعي `stepFn(fixedDt)` بقدر ما
  يسمح المُجمِّع، بحد أقصى `maxSteps` (حماية من "spiral of death" عند تجمّد التطبيق للحظة)
- `mm_fixedStepAlpha(mm)` — نسبة الزمن المتبقي غير المُستهلَك، لاستكمال العرض البصري
  (interpolation) بين خطوتَي فيزياء

### 21) أحجام الشاشة/الصفحة والتصميم المتجاوب (Viewport / Responsive) — جديد في 1.2.0
مختلف عن `bounds` (منطقة اللعب المنطقية، القسم 6): هذا حجم الشاشة/الصفحة الفعلي، للتموضع
النسبي وتحجيم القيم بين أجهزة مختلفة:

- `mm_setViewport(mm, width, height)` / `mm_viewport(mm)`
- `mm_toViewportPercent` / `mm_fromViewportPercent` / `mm_setPositionPercent` — تموضع
  بنسبة مئوية من الشاشة بدل إحداثيات مطلقة
- `mm_viewportClass(mm)` — `"compact"`/`"medium"`/`"expanded"` (حدود Material Design 3)
- `mm_scaleForViewport(mm, baseValue, baseWidth)` — تحجيم تناسبي لقيمة صُمِّمت لعرض مرجعي

### 22) أنواع شريط التحميل (Progress / Loading Bar Kinds) — جديد في 1.2.0
خمسة أنواع (`kind`): `"linear"`، `"circular"` (نفس منطق linear، الفرق بصري عند الرسم)،
`"indeterminate"` (يتأرجح بلا مدة معروفة)، `"segmented"` (مراحل معدودة)، `"buffer"`
(كشريط الفيديو: قيمتان played/buffered):

- `mm_progressCreate(mm, name, kind, duration)` / `mm_progressSet` / `mm_progressGet`
- `mm_progressTick(mm, name, dt)` — تقدُّم تلقائي حسب النوع
- `mm_progressSetSegments` / `mm_progressCompletedSegments` (لِـ`"segmented"`)
- `mm_progressSetBuffer` (لِـ`"buffer"`) / `mm_progressIsDone`
- `mm_progressWarp(mm, name)` — لقطة مسطَّحة جاهزة لعنصر واجهة Loom

### 23) اللمس وسلاسة الحركة (Touch Gestures & Motion Smoothing) — جديد في 1.2.0
تفسير لمس خام إلى بادرات، بالإضافة لتنعيم حركة مستقل عن معدّل الإطارات:

- `mm_touchBegin` / `mm_touchMove` / `mm_touchDrag` (سحب لتحريك مباشر) / `mm_isTouching`
- `mm_touchEnd(mm, name)` — يُصنِّف `"tap"` أو `"swipe"` حسب مسافة العتبة
  `MM_TAP_DISTANCE_THRESHOLD`، ويُعيد `{type, dx, dy, distance}`
- `mm_smoothFollow(mm, name, targetX, targetY, remainPerSecond, dt)` — تنعيم أُسّي
  مستقل عن الإطارات (framerate-independent) لموضع القناع
- `mm_smoothVelocity` — نفس المنطق على متجه السرعة

### 24) العملات والنقاط القابلة للجمع (Coins / Collectibles / Score) — جديد في 1.2.0
- `mm_spawnCoin(mm, name, x, y, value)` / `mm_isCoin` / `mm_isCoinCollected`
- `mm_collectCoinsNear(mm, collectorName, radius)` — يجمع كل عملة قريبة تلقائياً، يُخفيها،
  يُضيف قيمتها للنقاط، ويُطلق حدث محرّك `"coinCollected"` لكل عملة
- `mm_addScore` / `mm_score` / `mm_resetScore` — عدّاد نقاط عام على مستوى المحرّك، يُطلق
  حدث `"scoreChanged"`

### 25) أزرار التحكم الافتراضية (Virtual Joystick & Control Buttons) — جديد في 1.2.0
- `mm_joystickCreate(mm, name, centerX, centerY, maxRadius)` / `mm_joystickUpdate` /
  `mm_joystickRelease` / `mm_joystickVector` — عصا تحكّم مستمرّة مُطبَّعة (-1..1)
- `mm_joystickApplyToVelocity(mm, name, maskName, maxSpeed)` — تحكّم ثنائي العصا فوري
- `mm_buttonDefine` / `mm_buttonPress` / `mm_buttonRelease` / `mm_buttonIsPressed`
- `mm_buttonConsumeJustPressed` / `mm_buttonConsumeJustReleased` — نمط "لحظة الضغط/الإفلات"
  (edge-triggered) لتفادي تكرار تنفيذ فعل الزر كل إطار طالما الإصبع ضاغط

مثال شامل لكل هذه المفاهيم: [`../examples/moving_mask_extended_demo.rin`](../examples/moving_mask_extended_demo.rin).


## يتكامل طبيعياً مع

- [`lib/loopkit.og.rin`](../lib/loopkit.og.rin) — مرّر دالة صغيرة تستدعي `mm_tick` إلى
  `repeatTimes`/`stepLoop`.
- [`lib/iterkit.og.rin`](../lib/iterkit.og.rin) — لفّ `mm_names(world)` بمُكرِّر `iterNew`.
- [`lib/gridkit.og.rin`](../lib/gridkit.og.rin) — استخدم `makeGrid` كحاوية لأقسام الشبكة هنا.
- [`lib/animation.og.rin`](../lib/animation.og.rin) — دوال Easing جاهزة لـ`mm_animateTo`.
- [`lib/maskkit.og.rin`](../lib/maskkit.og.rin) و[`mask.md`](./mask.md) — طبقة الهوية
  الساكنة تحت هذه الطبقة الحركية.

## انظر أيضاً

- [`loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md`](./loomtime/RIN_LOOM_ENGINE_ARCHITECTURE.md) —
  معمارية محرّك Loomtime نفسه (Fabric/Warp/Needle/Shuttle) التي يستعير منها القسم 14 مفرداته.
- [`mask.md`](./mask.md) — نظام القناع الأصلي (هوية ساكنة).
- [`standard-library.md`](./standard-library.md) — خريطة كل مكتبات Rin القياسية.
- [`../examples/moving_mask_demo.rin`](../examples/moving_mask_demo.rin) — مثال تشغيلي كامل.
- [`../examples/moving_mask_extended_demo.rin`](../examples/moving_mask_extended_demo.rin) —
  مثال المفاهيم الخمسة الجديدة (15-19) في الإصدار 1.0.0.

# TIMER.md — `timer` والزمن في الحاويات

مفهوما **time** (الزمن كقيمة، انظر `TIME.md`) و**timer** (الزمن كحدث) يجتمعان هنا: ساعة قابلة للتحكّم،
مجدوِل داخل المفسّر، و`timer` متغيّر حيّ من عائلة `val` / `dialog` يعمل داخل `@container`.

```kotlin
@shop.container
    let stock = 10;
    timer promo = 1m every 20s;                           // عدّ تنازلي دقيقة، tick كل 20 ثانية
    val banner = "ينتهي العرض بعد " + promo.text;         // يتحدّث تلقائياً مع كل tick
    dialog over = "انتهى العرض" buttons ["حسناً"] timeout 5s;
    tick promo (n, v) { print "tick", n, banner; }
    ring promo (info) { open over; }                      // يرنّ عند الاكتمال
.end;
pause shop.promo;   resume shop.promo;   print shop.promo.left;
```

## 1) الساعة القابلة للتحكّم (`time.*`)
| الدالة | المعنى |
|---|---|
| `time.freeze([t])` | يجمّد الساعة (عند الآن أو عند `t`): تتحرك فقط بـ `advance`/`sleep`/`timers.run` ⇒ **مخرجات حتمية** |
| `time.advance(مدّة)` | يقدّم الساعة المُجمَّدة ويُطلق كل مؤقّت استحقّ بترتيبه وبزمنه الصحيح (يرمي خطأً إن كانت الساعة تجري) |
| `time.unfreeze()` / `time.frozen()` | العودة للزمن الحقيقي / هل هي مُجمَّدة؟ |
| `time.sleep(ms)` | (موجودة) صارت تُطلق المؤقّتات المستحقّة أثناء النوم، وتقدّم الساعة فوراً إن كانت مُجمَّدة |

كل دوال `time.*` (`time()`, `time.now`, `stopwatch`, `measure` ...) تتبع هذه الساعة.

**المدد المباشرة** تعمل في أي تعبير: `30s` `500ms` `1.5m` `2h` `1h30m` `3d` `1w` ⇒ رقم بالميلي ثانية
(الالتصاق شرط: `30s` مدّة، `30 s` لا). وفي `timer` تُقبل أيضاً `5 minutes`، ونص `"1h30m"`، وخريطة `{minutes: 5}`.

## 2) `timer` — متغيّر حيّ
```
timer NAME = المدّة [every خطوة] [after تأخير] [times N | loop] [until شرط] [up] [idle];
timer NAME up;                         // ساعة إيقاف مفتوحة
```
| المُعدِّل | الأثر |
|---|---|
| `every S` | خطوة `tick`؛ عندها تتحدّث الـ val/dialog/bell المعتمدة على المؤقّت |
| `after D` | تأخير قبل بدء العدّ (الحالة `armed`) |
| `times N` / `loop` | عدد الدورات / تكرار بلا نهاية؛ كل دورة تُطلق `ring` |
| `until COND` | إنهاء مبكر عندما يصدق الشرط (يُفحص عند كل tick؛ `reason` = `"until"`) |
| `up` | عدّ تصاعدي (القيمة = ما انقضى). بلا مدّة = ساعة إيقاف مفتوحة |
| `idle` | لا يبدأ تلقائياً؛ انتظر `start` (الافتراضي: يبدأ فور التصريح) |

**القيمة**: `t` = ما بقي بالميلي ثانية (أو ما انقضى مع `up`)، تُقرأ حيّة من الساعة، **وللقراءة فقط** (الإسناد مرفوض).

**الأفعال** (تقبل مساراً مؤهَّلاً `shop.promo`): `start` · `pause` · `resume` · `stop` · `reset` · `restart` · `lap` · `extend t 10s`.
`stop` يجمّد دون `ring`؛ `reset` يعيده `idle` بقيمته الكاملة؛ `extend` يزيد المدّة (ويوقظ مؤقّتاً منتهياً).

**المعالجات**: `tick t (n, value) { }` كل خطوة · `ring t (info) { }` عند كل اكتمال.
`info` = `{name, reason: "complete"|"until", cycles, ticks, elapsed, total, again}`. يمكن تعدّد المعالجات.

**الخصائص**: `left elapsed total progress percent text state running paused armed stopped done idle ready fires cycles starts every laps eta reason started finished info`
والحالات: `idle armed running paused done stopped`. `text` يعطي `01:30` / `1:02:03` / `2d 03:00:00`. `eta` لحظة الانتهاء المتوقعة.
و`ready` = ليس جارياً (idle/done/stopped) — يصلح لنمط **cooldown**:
```kotlin
timer cool = 5s idle;
fun act() { if (cool.ready) { doIt(); restart cool; } }
```

**التكامل مع عائلة الحيّة**: `val x = t.text` و`dialog ... opens when t.done` و`bell t (old, new)` تعتمد على المؤقّت كأي مصدر.
خارج الحاوية تعمل المسارات: `shop.promo.left`، ومن الدوال: `timerList(c)` · `timerInfo(c, n)` · `timerStart/Pause/Resume/Stop/Reset/Restart/Lap(c, n)` · `timerExtend(c, n, مدّة)`.

> **متى تجري المؤقّتات؟** الساعة الحقيقية: عند `time.sleep` و`timers.run/tick` و`time.advance` (ساعة مُجمَّدة)، وفي جلسات indsin عند كل `tick` للمضيف.
> البرنامج الذي ينتهي لا ينتظر مؤقّتاته؛ استعمل `timers.run(10s)` للانتظار. القراءة (`t.left`) دقيقة دائماً لأنها تُحسب من الساعة.
> ملاحظة: أفعال المؤقّت لا يتراجع عنها `trial` (الزمن لا يُرجَع).

## 3) المجدوِل `timers.*`
```kotlin
let id = timers.after(2s, fun() { print "بعد ثانيتين"; });           // يمرّر بقية الوسائط للدالة
timers.every(1s, fun(x) { print x; }, {times: 3, after: 500ms, args: [7]});
timers.at("tomorrow 09:00", fun() { ... });                          // أي لحظة تفهمها time
timers.cancel(id)   timers.pending()   timers.next()   timers.list([c])   timers.clear([c])
timers.tick()   timers.run()   timers.run(5s)                          // run() بلا مدّة ترفض ما لا ينتهي
```

## 4) الزمن في الحاويات
- **العمر**: `containerTime("shop")` ⇒ `{name, created, updated, age, idle, writes, timers, ttls}` (لحظات ومدد `time`).
  كتابات المؤقّت الداخلية لا تُحسب «تعديلاً».
- **انتهاء صلاحية حقل (ttl)**: `timers.expire(c, "field", 5s [, قيمةبديلة | دالة(old)])` — عند الانتهاء يصير الحقل `nil` (أو البديل)
  وتُستدعى الدالة. **أي كتابة جديدة على الحقل تُلغي الصلاحية** (كـ SET في Redis). `timers.ttl(c, f)` المتبقّي · `timers.persist(c, f)` إلغاء.
- **جماعي**: `timers.pauseAll(c)` · `timers.resumeAll(c)` · `timers.stopAll(c)` لمؤقّتات الحاوية؛ و`destroyContainer` يلغي مؤقّتاتها.
- **مهلة حوار**: `dialog d = "..." timeout 10s;` يُغلق الحوار المفتوح وحده بلا اختيار (`reply` يستلم `nil`)؛ `ask` لا يتأثر (إدخال متزامن).

## 5) اندماج المفاهيم (بلا كلمات جديدة)
المؤقّت لا يعيش وحده؛ التقاؤه بالمفاهيم القائمة يعطي سلوكاً جديداً بخيارات على دوال موجودة:
| الدمج | ما ينتج |
|---|---|
| `timer` ✕ `dialog` | `dialog ... timeout 10s` إغلاق تلقائي · `opens when t.done` يفتح الحوار عند انتهاء المؤقّت |
| `timer` ✕ `val` | `val x = t.text` تتحدّث مع كل tick |
| `timer` ✕ حرّاس التأكيد | `Intent.guard(c, act, dlg, {cooldown: "t"})` و`Navigation.guard(..., {cooldown: "t"})`: ما دام المؤقّت جارياً يُمنع الإجراء **بلا سؤال** (`{ok:false, blocked:true, reason:"cooldown", left}`)، وكلما سُمح به أُعيد تشغيل المؤقّت. يُتحقَّق من أن الاسم مؤقّت في الحاوية عند التسجيل. `when: "t"` يسأل فقط ما دام المؤقّت يحمل قيمة صادقة |
| `timer` ✕ `Intent` | `Intent.send(i, {after: 10s})` · `{at: لحظة}` · `{every: 1s, times: 3}`؛ تعيد `{ok, scheduled, id, in}` ويمرّ كل إرسال بالحارس عند لحظته |
| `timer` ✕ الحاوية | ما جدولته الحاوية (نيّات/ttl) يُلغى بـ `timers.clear(c)` و`destroyContainer(c)` |

## حدود وأخطاء
أقصى 10000 مدخل معلّق؛ أقصى مليون إطلاق في خطوة واحدة؛ `timers.run()` بلا مدّة يرفض المؤقّتات اللانهائية (`loop`/`every` بلا نهاية)؛
`timers.run` الحقيقية لا تنتظر أكثر من 10 دقائق (جمّد الساعة لجداول طويلة). الاختبارات: `tests/print/timer_*.rin` و`timers_scheduler.rin`.

### اندماج إضافي: timer ✕ Navigation ✕ Intent.atomic (بلا كلمات جديدة)

- `Navigation.re(c, route, params, {after: 3s})` أو `{at: …}` — انتقال مؤجَّل (شاشة افتتاحية)؛ يمرّ بالحارس عند لحظته. يعيد `{ok, scheduled, kind:"navigation", target, in}`.
- `Intent.atomic([...], {after: 10s})` أو `{at: …}` — الدفعة كلها تُؤجَّل وتُنفَّذ ذرّياً عند لحظتها.
- `after` و`at` معاً خطأ، وأي مفتاح آخر مجهول يُرفض. المؤجَّل يُلغى بـ `timers.clear(container)` أو بحذف الحاوية.
- الاختبار: `tests/print/timer_nav_atomic.rin`.

### اندماج `let` و`if` مع timer
- **`let … ttl`**: داخل الحاوية `let token = "abc" ttl 30s;` يصير الحقل `nil` بعد المدّة (مكافئ `timers.expire`). يعمل مع النوع (`let c: String = "1" ttl 1m;`) ومع التصريحات المتعددة (لكل عنصر `ttl` خاص) وقبل `else`. أي كتابة جديدة تُلغي الصلاحية. خارج حاوية ⇒ E0007.
- **تفكيك**: `let {state, left} = timerInfo("c", "t");` و`let [a, b] = [c.t.left, c.t.state];`.
- **`if`** على حالة المؤقّت مباشرة: `if (c.t.ready) …` · `if (c.t.left > 5s) …`.
- **`let … else`** مع `timers.ttl` يعطي حارساً لغياب الصلاحية.
- الاختبار: `tests/print/timer_let_if.rin`.

### اندماج `api` مع timer
خيارات اختيارية في آخر `callApi(c, method, path, opts)` و`call(method, path, opts)` (بدونها السلوك القديم حرفياً):
- `cache: 30s` — يعيد آخر ردّ **ناجح** للمسار نفسه (مع `cached: true`) حتى تنتهي المدّة؛ `cache: 0` يتجاوز الكاش ويحذفه. الفشل (404…) لا يُخزَّن.
- `after: 10s` / `at: لحظة` / `every: 5s` + `times: n` — استدعاء مؤجَّل أو دوري، ملك الحاوية (يُلغى بـ `timers.clear(c)` أو بحذفها)؛ يعيد `{ok, scheduled, id, kind:"api", target, in}`.
- `then: fn` — تُستدعى بالردّ بعد كل استدعاء؛ مع `every` تصير استطلاعاً دورياً (polling).
- الاختبار: `tests/print/timer_api.rin`. الساعة المُجمَّدة تجعل الكاش والاستطلاع حتميَّين.

### اندماج الـ API الحقيقي مع timer
في آخر `apiGet/apiPost/apiPut/apiPatch/apiDelete/apiCall` وسيط خيارات اختياري (بدونه السلوك القديم حرفياً):
- `retry: n` (0–20) + `backoff: 2s` — يعيد المحاولة عند الفشل (`ok=false` أو `status>=500`)؛ بين المحاولات يُنام بـ `time.sleep` (فتتقدّم الساعة المُجمَّدة وتُطلَق المؤقّتات المستحقّة) ويُضاف `attempts` للردّ.
- `timeout: 3s` — مهلة هذا الطلب وحده (بدل `httpSetTimeout`).
- `cache: 30s` — لـ GET فقط، يعيد الردّ الناجح الأخير مع `cached: true`؛ `cache: 0` يتجاوزه.
- `after` / `at` / `every` + `times` / `then: fn` — كما في `callApi`؛ الاستطلاع الدوري يُلغى بـ `timers.clear()`.
- الاختبار الحتمي بلا شبكة خارجية: `tests/print/timer_api_real.rin` (نقطة 127.0.0.1:9 غير متاحة).

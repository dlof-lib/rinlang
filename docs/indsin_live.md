# Indsin Live — Rin يُحرّك واجهات indsin ويستجيب للمستخدم

> لا تلتبس بأداة **المعاينة الحيّة** (`indsin live`، انظر [indsin_live_preview.md](./indsin_live_preview.md)): تلك تعرض الواجهة على الأجهزة، وهذه مكتبة Rin لسلوك الواجهة.

`indsin` يرسم الواجهة. `lib/indsinlive.og.rin` هي **الجانب الحيّ من Rin** فوقه: ماذا يحدث عند النقر،
عند إرسال نموذج، وعند تغيّر المحتوى — **بلا إعادة تحميل** — مع حركة تربط ذلك كله. مكتبة Rin خالصة بلا أي
تبعية، مضمَّنة في المفسّر:

```kotlin
@import "lib/indsinlive.og.rin";      // أو @import "indsinlive";
let app = ilApp("shop");
```

الفكرة في سطر: **المعالجات دوال Rin عادية تكتب في خلايا `warp`، وindsin يعيد رسم ما تغيّر.**
المثال الكامل: [`examples/samples/indsin_live.rin`](../examples/samples/indsin_live.rin) ·
الاختبار: `tests/print/indsin_live.rin` (75 فحصاً).

```
حدث المستخدم ─▶ ilEmit ─▶ معالجات Rin ─▶ ilSet / ilUpdate / ilFormSubmit / ilPlay
                                              │                    │
                                       setter(v) يكتب في warp    فرق مفتاحي (عمليات فقط)
                                              ▼
                                      indsin يعيد تقييم الخصائص ويرسم
```

## ١) الأحداث

اسم الحدث `"النوع:الهدف"` — `click:save` · `input:search` · `submit:login`. النمط يقبل `*` لقطعة كاملة.

| الدالة | الأثر |
|---|---|
| `ilOn(app, pattern, fn)` → id · `ilOnce` · `ilOff(app, id)` | تسجيل/حذف معالج. `click:*` · `*:save` · `*` |
| `ilEmit(app, "click:save", payload)` | يبثّ الحدث. يعيد `{handled, cancelled, results, errors}` |
| `ilClick(app, target, payload)` · `ilInput` · `ilChange` · `ilEvent(app, kind, target, payload)` | اختصارات |
| `ilUse(app, fn)` | حارس `fn(ev)`: يُرجع `false` ⇒ يُلغى الحدث قبل المعالجات |
| `ilThrottle(app, pattern, ms)` | يمنع تكرار حدث أسرع من `ms` (نقر مزدوج على «شراء») |
| `ilDebounce(app, pattern, ms, fn)` | ينفّذ `fn(ev)` بعد هدوء `ms` من آخر حدث (بحث أثناء الكتابة) — يُطلَق من `ilTick` |

المعالج يستلم خريطة `{name, type, target, payload}`. **خطأ داخل معالج لا يوقف البقية**: يُسجَّل في `ilErrors(app)`
ويظهر في `errors` من `ilEmit`. عمق البثّ المتداخل محدود بـ 16 (يمنع الدوران اللانهائي).

```kotlin
ilOn(app, "click:save", fun(ev) { saveCount = saveCount + 1; });
fun onSave() { ilClick(app, "save", nil); }          // في الواجهة: <Button onTap={onSave()}>
```

## ٢) محتوى يتحدّث بلا إعادة تحميل

### خانات قيم (نص، عدّاد، رسالة…)

```kotlin
warp banner = "";
ilBind(app, "banner", fun(v) { banner = v; });        // يُستدعى فوراً ثم عند كل تغيّر
ilSet(app, "banner", "Saved");                        // true فقط إن تغيّرت القيمة؛ ويبثّ "set:banner"
ilUpdateSlot(app, "count", fun(c) { return c + 1; });
```

### مناطق القوائم (فرق مفتاحي)

`ilRegion(app, name, items, {"key": "id"})` ثم أي تغيير عبر `ilUpdate / ilAdd / ilRemove / ilPatchItem` يحسب
**عمليات الفرق فقط** ويبثّ `patch:<name>`، فلا يُعاد بناء ما لم يتغيّر (وتبقى حالة العنصر في الواجهة):

| العملية | معناها |
|---|---|
| `{op:"remove", index, key}` | حذف (من النهاية إلى البداية) |
| `{op:"add", index, key, item}` | إضافة |
| `{op:"move", from, index, key}` | نقل — بدل حذف وإعادة إنشاء |
| `{op:"update", index, key, item, changed:[حقول]}` | تعديل عنصر |

العمليات **تُطبَّق بالترتيب** على القائمة القديمة فتعطي الجديدة تماماً: `ilApply(قديم, ilDiff(قديم, جديد, "id")) == جديد`
(مختبَرة على 60 حالة عشوائية). بلا `key` تُقارَن العناصر بالموضع. مفتاح مكرّر أو مفقود ⇒ خطأ صريح.
`ilPatchesJson(patches)` يعطي JSON للمضيف.

| الدالة | الأثر |
|---|---|
| `ilRegionBind(app, name, setter)` | `setter(items, patches)` عند كل تغيّر فعلي (وفوراً بالعناصر الحالية) |
| `ilLoad(app, name, loader)` | `loading:` ← `loaded:` أو `error:`؛ `loader()` يعيد مصفوفة أو `{items}` أو `{ok:false, error}`؛ الفشل **يُبقي العناصر القديمة** |
| `ilRefresh(app, name)` | يعيد تشغيل آخر `loader` |
| `ilOptimistic(app, name, applyFn, commitFn)` | يطبّق فوراً ثم `commitFn()`؛ فشل ⇒ تراجع تلقائي + `rollback:<name>` |
| `ilPoll(app, name, ms)` · `ilPollStop` | تحديث دوري يشغّله `ilTick` |
| `ilRegionState` → `empty ready loading error` · `ilRegionItems` · `ilRegionVersion` · `ilFind` | قراءة |

## ٣) النماذج

```kotlin
ilForm(app, "signup", {
    "email":    {"required": true, "kind": "email"},
    "password": {"required": true, "minLen": 8},
    "confirm":  {"required": true, "equals": "password"},
    "age":      {"kind": "integer", "min": 18, "max": 99}
}, {"lang": "ar", "validateOn": "change", "resetOnSuccess": true});
```

قواعد الحقل: `required` · `kind` (`email phone url number integer`) · `min max` · `minLen maxLen` · `pattern` (regex) ·
`equals` (حقل آخر) · `oneOf` · `check: fun(value, values)` (يعيد `""` أو رسالة أو `false`) · `message` (تتغلّب على الرسائل الجاهزة) ·
`initial` · `trim:false`. الرسائل الجاهزة بالعربية والإنجليزية.

| الدالة | الأثر |
|---|---|
| `ilFieldSet(app, form, field, value)` | يضبط القيمة. الخطأ يظهر بعد اللمس (`ilFieldBlur`) أو فور الكتابة مع `validateOn:"change"`؛ وما إن يظهر خطأ يُعاد الفحص مع كل ضغطة حتى يزول |
| `ilFieldBlur` · `ilFormValidate` | فحص حقل / كل الحقول |
| `ilFormSubmit(app, form, handler)` | انظر أدناه |
| `ilFormResolve(app, form, result)` | إنهاء إرسال معلّق |
| `ilFormSnapshot` → `{values, errors, state, valid, dirty, submits, first}` · `ilFormBind(app, form, setter)` | لقطة واحدة تكفي لملء كل خلايا warp |
| `ilFormValues` · `ilFormErrors` · `ilFormError(app, form, field)` · `ilFormFirstError` · `ilFormState` · `ilFormReset` | قراءة/تصفير |

**دورة الإرسال:** `idle → invalid` (لا يُستدعى `handler`) أو `idle → submitting → success | failed`.
نتيجة `handler(values)`: أي قيمة = نجاح · `{ok:false, errors:{حقل:رسالة}, error}` = رفض الخادم (الأخطاء تُربط بالحقول) ·
`{pending:true}` = ما زال جارياً (أنهِه بـ `ilFormResolve`) · استثناء = `failed`. **أثناء `submitting` أي إرسال ثانٍ يُرفض**
`{blocked:true}` (حماية من النقر المزدوج). الأحداث: `submitting:` `submit:` `invalid:` `failed:` `change:form.field` `blur:form.field` `form:`.

## ٤) الحركة

محرّك tween بزمن صريح (لا مؤقّتات نظام ⇒ اختبار حتمي). كل خاصية متحرّكة تُربط بـ setter يكتب في warp:

```kotlin
warp cardOpacity = 0;
ilBindProp(app, "card", "opacity", fun(v) { cardOpacity = v; });   // <Card opacity={cardOpacity}>
ilPreset(app, "card", "fadeIn", {"dur": 500});
ilAnim(app, "save", "bg", "#E74C3C", "#6C5CE7", 600, {"ease": "cubicOut"});   // الألوان تُمزَج أيضاً
```

| الدالة | الأثر |
|---|---|
| `ilAnim(app, target, prop, from, to, dur, opts)` | `opts`: `delay ease loop yoyo round backfill onDone`. أرقام أو ألوان `#rgb/#rrggbb` |
| `ilSequence(app, steps)` · `ilParallel` · `ilStagger(app, targets, prop, from, to, dur, gap, opts)` | خطوات `{target, prop, from, to, dur, delay, ease, …}` |
| `ilCountUp(app, target, from, to, dur, opts)` | عدّاد صحيح يكتب في الخاصية `"value"` |
| `ilPreset(app, target, name, opts)` | `fadeIn fadeOut slideUp slideDown slideLeft slideRight pop pulse press flash shake bounce` على `opacity/x/y/scale` |
| `ilEase(name, t)` | `linear in out inOut cubicIn cubicOut smooth back bounce elastic spring` أو دالة |
| `ilAnimateOn(app, pattern, target, preset)` | عند وقوع الحدث شغّل حركة (`target=nil` ⇒ هدف الحدث): `ilAnimateOn(app, "invalid:login", "card", "shake")` |
| `ilPlay(app, target, preset, opts)` | `"host"` (محرّك indsin الأصلي) ثم `"tween"` (محرّك Rin إن رُبطت الخصائص) ثم `"none"` (تُسجَّل في `app["effectLog"]`) |
| `ilDefineHostEffects(app)` | يعرّف المسبقات كتأثيرات `il_*` في Effects Studio عبر `indsin.defineEffect` — يعيد عددها (0 بلا مضيف) |
| `ilTick(app, t)` | خطوة إطار: `t` بالمللي ثانية أو `nil` لساعة النظام. تُشغّل الحركات والـ debounce والـ poll. تعيد عدد الحركات النشطة |
| `ilStart(app, ms)` · `ilStop(app)` | تشغيل `ilTick` تلقائياً عبر `timers.every` (يعيد `-1` إن لم يتوفّر مجدول) |
| `ilAnimStop(app, id)` · `ilAnimating(app, target)` · `ilValue(app, target, prop)` | تحكّم/قراءة |

* `backfill` (افتراضي `true`): تُكتب القيمة الابتدائية فوراً حتى أثناء `delay`، فيبدأ العنصر المتأخر مخفياً. وفي `ilSequence` لا تُعيد
  الخطوات اللاحقة على نفس الخاصية كتابة قيمتها فور الإنشاء (لا قفزات).
* أول `ilSetTime/ilTick(app, t)` بعد العمل بساعة النظام **يُعيد ضبط** أزمنة الحركات المعلّقة على ساعة المضيف.

## ٥) الزمن والفحص

`ilSetTime(app, ms)` ساعة يدوية (للاختبار أو لتمرير طابع زمن المضيف) · `ilNowMs(app)` · `ilStats(app)` · `ilLog(app, n)` ·
`ilErrors(app)` · `ilClearErrors(app)`.

## ما تغطّيه وما لا تغطّيه

* مكتبة **Rin**: تعمل أينما يعمل مفسّر Rin (أندرويد، سطح المكتب، CLI، المعاينة الحيّة). مترجم `indsin2web` لا ينقل `@import` لمكتبات Rin
  إلى المتصفح (يحذّر بذلك — انظر [`indsin2web.md`](./indsin2web.md))؛ الصفحات المستقلة تستعمل معالجات `onTap/onSubmit/onChange`
  المترجمة إلى JS مباشرة، وتبقى `ilDiff/ilPatchesJson` عقد بيانات صالحاً لأي واجهة خلفية بـ Rin.
* مسار `host` في `ilPlay` يحتاج جلسة indsin (`indsin.playEffect`)؛ بدونها يسقط تلقائياً إلى محرّك Rin.
* الأحداث التي ينتجها المضيف (`onChange` وغيرها) تتبع ما يدعمه كل مضيف؛ المكتبة تستقبل أي حدث تُمرّره إليها بـ `ilEmit`.
* قيود Rin: الدوال بعدد وسائط ثابت (المعالج 1، setter الخانة والخاصية 1، setter المنطقة 2، معالج النموذج 1)، و`text/style/to/from`
  كلمات محجوزة كأسماء متغيّرات (تصلح مفاتيح خرائط نصّية كما في `{"from": 0, "to": 1}`).

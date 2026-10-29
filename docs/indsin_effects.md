# Effects Studio — إنشاء التأثيرات الحركية بالكامل من Rin ومن indsin

كان `effect=` سابقاً قائمة ثابتة من 10 حركات دخول. الآن التأثير **تعريف قابل للإنشاء**: keyframes ومعادلات
رياضية وألوان وتوهّج وتوقيت كامل، يُكتب بالوسوم أو بكتلة `@effect` أو من كود Rin، ثم يُشغَّل عند الدخول أو
الإيماءات أو تغيّر قيمة أو برمجياً. الكود: `app/src/main/cpp/indsin/rin_indsin_effects.h` (+ `…_effects_paint.h`).

> **إصلاح مهم:** المحرّك القديم كان يعدّل هندسة الـFabric في مكانها فتتراكم الإزاحة مع كل tick
> (عنصر `slideUp` كان ينتهي على y=83 بدل 14). المحرّك الجديد لا يمسّ الـFabric إطلاقاً: يحسب «إطاراً» لكل
> عنصر ويطبّقه على قائمة الرسم وقت الطلاء. لا انجراف، ولا تعارض مع كاش التخطيط، ويتحرّك العنصر مع كل أبنائه.

## 1) تعريف تأثير

### أ) بالوسوم
```xml
<Effect#hop duration=520 easing=cubicOut>
  <Keyframe at=0%  y=0 />
  <Keyframe at=45% y=-26 scaleY=1.08 />
  <Keyframe at=100% y=0 />
</Effect>

<!-- مختصر بسطر واحد -->
<Effect#swoosh frames="from{x:-100%,opacity:0} to{x:0,opacity:1}" duration=450 easing=expoOut />
```
قيم `{expr}` داخل `<Keyframe>` تُقيَّم وقت التسجيل (`<Keyframe at=50% y={-dist} />`).

### ب) كتلة `@effect`
```
@effect=wobble
  duration=900; easing="easeInOut"; iterations="infinite"; direction="alternate";
  from="x:-6"; at50="x:6,opacity:.8"; to="x:0";
.end/effect
```
`atNN` نسبة مئوية (`at12_5` = 12.5%). `base="bounce"` يبدأ من تأثير موجود ثم تعدّله.

### ج) من Rin
```
indsin.defineEffect("kick", "0%{x:0} 40%{x:36,scaleX:1.15} 100%{x:0}", {"duration": 480, "easing": "spring"});
```
مع `indsin.removeEffect(name)` · `indsin.effects()` · `indsin.hasEffect(name)`. ومن المضيف (C API):
`rin_indsin_effect_define(name, frames, "duration=600;easing=spring")` و`rin_indsin_effects_json()`.

### د) معادلات رياضية
بدل keyframes لقناة معيّنة: `x_fn · y_fn · opacity_fn · scale_fn · scaleX_fn · scaleY_fn · glow_fn`.
المتغيّرات: `t` تقدّم الدورة 0..1 · `n` رقم الدورة · `w`,`h` حجم العنصر · `pi`.
الدوال: `sin cos tan abs sqrt floor ceil round exp log sign fract tri noise pow min max mod step atan2 clamp mix smooth`.
```
@effect=float  duration=2400; iterations="infinite"; easing="linear"; y_fn="sin(t*pi*2)*6"; .end/effect
```
محلّل آمن: لا حلقات ولا استدعاءات خارجية؛ الأخطاء تُسجَّل في `rin_indsin_effects_json().error` ولا توقف العرض.

## 2) القنوات داخل keyframe
| القناة | المعنى |
|---|---|
| `opacity` | شفافية 0..1 (أو %) |
| `x` / `y` | إزاحة px، أو `%` من حجم العنصر نفسه |
| `scale` / `scaleX` / `scaleY` | تكبير حول مركز العنصر |
| `glow` + `glowColor` | توهّج (ظل بلا إزاحة) |
| `bg` | يمزج لون تعبئة العنصر نحو لون (حرفي أو دور ثيم: `primary`, `danger`…) ثم يعود |
| `color` | يمزج لون النصوص (للعنصر وأبنائه) |
| `easing` | easing لهذا المقطع فقط |
القنوات غير المذكورة تبقى على قيمتها الطبيعية (كما في CSS). لا يوجد `rotate`: قائمة الرسم لا تدعم الدوران.

## 3) التوقيت
`duration` (`400` · `0.4s` · `fast|slow`) · `delay` · `iterations` (رقم | `infinite`) · `direction`
(`normal|reverse|alternate|alternate-reverse`) · `fill` (`none|forwards|backwards|both`) ·
`easing`: `linear easeIn easeOut easeInOut cubicIn/Out/InOut sine back bounce elastic spring expo circ ease`
+ `cubic-bezier(.4,0,.2,1)` + `steps(4)`.
الافتراضي: الدخول `fill=both`، والإيماءات `fill=none`، وتأثيرات الخروج `forwards`.

## 4) التشغيل
| الطريقة | مثال |
|---|---|
| دخول | `effect="fadeUp"` · قائمة `effect="fadeUp glow"` · مضمَّن `effect="from{opacity:0} to{opacity:1}"` |
| ضبط الدخول | `duration=` `delay=` `easing=` (أو `effect_duration` …، و`effect_iterations` `effect_direction` `effect_fill`) |
| تتابع | `stagger=90` + `effect=` على الحاوية: لا تتحرّك هي، وأبناؤها المباشرون يتتابعون |
| إيماءات | `effect_tap=` `effect_hover=` `effect_longpress=` `effect_doubletap=` (+ `effect_tap_duration=` …) |
| عند تغيّر قيمة | `<Text text={n} effect_on_change=heartbeat />` (القيمة المراقَبة: `effect_key={…}` وإلا text/value/label) |
| من Rin | `indsin.playEffect("card", "kick"[, {options}])` · `indsin.stopEffect("card"[, "kick"])` |
| من onTap | `onTap={effect(card, "rubber")}` · `onTap={stopEffect(card)}` |
| من المضيف | `rin_indsin_session_play_effect(s, "card", "shake")` · `rin_indsin_session_stop_effect(s, "card")` |

العنصر المخفي (`visible=false`، أو Dialog/Drawer مغلق) يُعدّ غائباً، فيُعاد تشغيل دخوله عند ظهوره.

## 5) المدمجة (42)
دخول: `fade scale zoom slideUp slideDown slideLeft slideRight grow pop fadeUp fadeDown fadeLeft fadeRight zoomIn zoomOut slideInFull rise drop reveal`
· انتباه: `pulse shake bounce flash heartbeat rubber jelly wobble tick highlight ping`
· حلقات: `float breathe glow blink flicker sway`
· خروج: `fadeOut scaleOut slideOutUp slideOutDown slideOutLeft slideOutRight`.
الستة القديمة تحتفظ بنفس منحنياتها. المدمجات معرَّفة بنفس لغة التعريف أعلاه — يمكنك كتابة مثلها تماماً.

## ملاحظات
- الحلقات اللانهائية تُبقي `"animating":true` فيستمر المضيف بنداء `tick` (كلفة بطارية عمداً).
- التحويل يُطبَّق على الرسم فقط؛ الـhit-test يستعمل الموضع الأصلي (فارق بسيط أثناء الحركة).
- طابور `playEffect` عامّ للعملية ويستهلكه أول جلسة تعيد التخطيط.

## الاختبار
```
g++ -std=c++23 -I app/src/main/cpp tests/indsin_effects/effects_test.cpp app/src/main/cpp/indsin/rin_indsin_c_api.cpp <engine objs> -lz -lpthread -o fxtest && ./fxtest
```
المثال: `examples/samples/indsin_effects.rin`.

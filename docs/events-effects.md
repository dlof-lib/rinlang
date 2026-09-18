# الأحداث والتأثيرات (Events & Effects) — محرك Loom UI

هذا التوثيق يغطي ميزتين جديدتين أُضيفتا إلى محرك Loomtime:

1. **الأحداث (Events)**: إيماءات تفاعل جديدة إلى جانب `onTap=` الموجودة أصلاً —
   `onLongPress=`، `onDoubleTap=`، و`onHoverEnter=`/`onHoverExit=`.
2. **التأثيرات (Effects)**: انتقالات بصرية (Enter Transitions) عند ظهور عنصر —
   `effect=` مع `duration=`/`easing=`/`delay=`.

كلتاهما تعملان فوق نفس محرك Needle الذي يُشغّل `onTap=` أصلاً (نفس القواعد: عنصر
`state="disabled"` لا يستهلك أي إيماءة، ونفس الأولوية لدالة `fun` حقيقية أو أحد
أفعال محرك الإجراءات المدمجة Action Engine).

---

## 1. الأحداث (Events)

### `onLongPress=` و`onDoubleTap=`

نفس صياغة `onTap=` بالضبط — استدعاء دالة حقيقية، أو فعل مدمج:

```rin
warp likes = 0;
fun bump() { likes = likes + 1; }

@view.Card=post
  padding=16;
  @view.Text=body text="منشور تجريبي"; .end/view
  @view.Button=likeBtn label="أعجبني"; onDoubleTap=bump(); .end/view
  @view.Button=likeBtn2 label="خيارات"; onLongPress=show(menuOpen); .end/view
.end/view
```

- عنصر بلا `onLongPress=`/`onDoubleTap=` لا يُعتبر خطأ عند استقبال تلك الإيماءة —
  ببساطة `handled:false`، تماماً كما لو ضغطتَ على مساحة فارغة.
- استشعار الإيماءة نفسها (مدة الضغط الطويل، التوقيت بين نقرتين) هو مسؤولية
  المضيف (`GestureDetector` في `LoomFabricView.kt`) — محرك Needle لا يفعل أكثر
  من: "هذه إيماءة X وقعت عند (x,y)، ماذا أُشغّل؟".
- عنصر `onLongPress=` على نفس Strand الذي يحمل `onTap=` لا يستدعي `onTap=` عند
  الضغط الطويل، والعكس صحيح — إيماءتان مستقلتان تماماً.

### `onHoverEnter=` و`onHoverExit=`

مخصصة لمضيف يملك مؤشراً حقيقياً (فأرة، قلم رقمي، أو جهاز أندرويد بفأرة
متصلة) — لا معنى حقيقياً لها على لمس الإصبع المباشر:

```rin
@view.Card=row
  @view.Text=label text="مرّر المؤشر هنا"; .end/view
  onHoverEnter=highlight(rowHovered);
  onHoverExit=unhighlight(rowHovered);
.end/view
```

الجانب الأصلي (`LoomFabricView.kt`) يتتبّع اسم آخر Strand اعتُبر "تحت المؤشر"،
ويستدعي `onHoverExit=` عليه فور انتقال المؤشر إلى عنصر آخر، ثم `onHoverEnter=`
على العنصر الجديد — تماماً كما تفعل أي حزمة واجهات حقيقية.

---

## 2. التأثيرات (Effects)

خاصية `effect=` على أي Strand تجعله "يدخل" المشهد بانتقال بصري بدل الظهور
الفجائي، أول مرة يظهر فيها ضمن الشجرة (مثلاً: عند فتح Dialog، أو تحقق شرط
`if` لم يكن محققاً سابقاً):

```rin
@view.Dialog=confirm
  open=dialogOpen;
  effect="scale"; duration="200"; easing="easeOut";
  @view.Text=msg text="هل أنت متأكد؟"; .end/view
.end/view
```

### الأنواع المتاحة لـ `effect=`

| القيمة | الوصف |
|---|---|
| `fade` | تعتيم من الشفافية الكاملة (0) إلى الوضوح الكامل (1) |
| `scale` (أو `zoom`) | تكبير تدريجي من 85% إلى 100% مع تعتيم مرافق |
| `slideUp` | انزلاق من الأسفل إلى موضعه الطبيعي |
| `slideDown` | انزلاق من الأعلى |
| `slideLeft` | انزلاق من اليمين (بصرياً يبدأ العنصر إلى يمين موضعه) |
| `slideRight` | انزلاق من اليسار |

### الخصائص المرافقة (اختيارية)

- `duration="250"` — المدة بالميلي ثانية (الافتراضي 250).
- `easing="easeOut"` — منحنى التسارع: `linear` / `easeIn` / `easeOut`
  (الافتراضي) / `easeInOut`.
- `delay="0"` — تأخير بالميلي ثانية قبل بدء التأثير فعلياً (الافتراضي 0).

### الوراثة

`effect=` (ومعه `duration=`/`easing=`/`delay=`) على عنصر أب يُطبَّق تلقائياً
على كل أبنائه الذين لا يملكون `effect=` خاصاً بهم — فبطاقة كاملة تدخل مع
محتواها معاً بسطر واحد بدل تكرار الخاصية على كل عنصر فرعي:

```rin
@view.Card=welcome
  effect="fade"; duration="300";
  @view.Text=title text="أهلاً بك"; .end/view
  @view.Text=subtitle text="سعداء بانضمامك"; .end/view
.end/view
```

### كيف يعمل داخلياً

- محرك التأثيرات (`rin_loom_effects.h`) يُعيد استخدام آلية `opacity=`
  الموجودة أصلاً في `rin_loom_paint.h` بدل اختراع خط أنابيب رسم موازٍ — كل
  إطار من التأثير يُركّب مضاعِف شفافية على `opacity=` الأصلية للعنصر (إن
  وُجدت)، ويُزيح/يُكبّر هندسته (`geometry`) مباشرة بعد التخطيط (Layout)
  وقبل الرسم — فأي مستهلك لمخرجات `paint` (سواء الرسم الحي، أو تصدير PNG)
  يحصل على الإطار الحالي مجاناً دون أي تعديل عليه.
- عنصر يختفي من الشجرة (إغلاق Dialog مثلاً) يُعاد ضبط حالته — المرة القادمة
  التي يظهر فيها، يدخل بالتأثير من جديد، لا يُكمل من حيث توقف.
- تعديل `duration=`/`easing=` في مصدر مفتوح للتعديل الحي أثناء انتقال جارٍ
  لا يُعيد الانتقال من الصفر — فقط يُحدّث السرعة/المنحنى المتبقيين.

### الدفع من جانب Kotlin (Loom Session)

جلسة `RinEngine.LoomSession` الآن تعرض:

```kotlin
val session = RinEngine.LoomSession(source, rootWidthPx)
// إيماءات:
session.longPress(x, y)
session.doubleTap(x, y)
session.hover(x, y, entering = true)
// تحريك:
val json = session.tick() // استدعِها من Choreographer طالما "animating" في آخر نتيجة = true
```

كل استجابة (`tap`/`longPress`/`doubleTap`/`hover`/`tick`) تحمل الآن حقل
`"animating":bool` على المستوى الأعلى — `true` يعني وجود انتقال لم يكتمل
بعد (يستحق جدولة إطار آخر عبر `tick()`)، `false` يعني استقرار تام (يمكن
إيقاف حلقة التحريك بأمان).

`LoomPreviewActivity.kt` يفعل هذا تلقائياً عبر `Choreographer.FrameCallback`
يُعاد جدولته من `onFabricUpdated` طالما `animating == true`، ويتوقف من تلقاء
نفسه بمجرد استقرار كل شيء — لا حلقة استطلاع (polling) دائمة.

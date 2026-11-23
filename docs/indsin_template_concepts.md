# مفاهيم القوالب في indsin — `Match` · `Unless` · فتحات مسمّاة · `Prop` · `...attrs`

خمس إضافات لطبقة الوسوم (`rin_indsin_tags.h`). كلها **سكّر نحوي وقت الترجمة** إلى ما يفهمه المحرّك أصلاً (`If`/`Else` و`Define`)،
لذلك تعمل بلا أي تعديل في المحرّك الأصلي ومترجم الويب (`indsin2web`) والمعاينة الحيّة، وما لا يستعملها يخرج مطابقاً حرفاً بحرف
(اختُبر على كل ملفات `.rin` في المستودع: 730 ملفاً).

## 1) `<Match>` — اختيار فرع واحد
```rin
<Match on={tab}>
  <Case is=0><Text#a>الرئيسية</Text></Case>
  <Case in="1,2"><Text#b>تفاصيل</Text></Case>      <!-- عدّة قيم مفصولة بفاصلة -->
  <Case when={tab > 5}><Text#c>كبير</Text></Case>    <!-- شرط حر -->
  <Default><Text#d>غير ذلك</Text></Default>
</Match>
```
* يتحوّل إلى سلسلة `<If>`/`<Else>` فيظهر **فرع واحد فقط** دائماً ويُعاد التقييم عند تغيّر `warp`.
* `is=` قيمة حرفية (`0` · `"dark"` · `{تعبير}`)، و`in="a,b,1"` قائمة قيم، و`when={…}` شرط كامل (لا يحتاج `on`).
* `on={…}` تعبير Rin بين أقواس معقوصة. `<Default>` اختياري ويجب أن يكون الأخير. يمكن تداخل `Match` داخل `Case`.
* داخل `<Define>` يرى `on={prop}` خصائص القالب كأي تعبير آخر.

## 2) `<Unless>` — عكس `<If>`
```rin
<Unless cond={ready}><Text#w>جارٍ التحميل…</Text></Unless>
<Else><Text#r>جاهز</Text></Else>
```
`<Unless cond={c}>` ≡ `<If cond={!(c)}>` و`<Else>` بعده يعمل كالمعتاد.

## 3) فتحات مسمّاة مع محتوى احتياطي
```rin
<Define#Panel>
  <Card#p>
    <Slot />                                              <!-- الفتحة الافتراضية (أبناء الاستعمال) -->
    <Slot name="footer"><Text#fd>بلا تذييل</Text></Slot>   <!-- محتوى احتياطي إن لم يُملأ -->
  </Card>
</Define>

<Panel>
  <Text#body>المحتوى</Text>
  <Fill name="footer"><Button#ok>حسناً</Button></Fill>
</Panel>
```
* `<Fill name="x">` يملأ `<Slot name="x">`؛ ما عداه يذهب إلى `<Slot />` الافتراضية.
* احتياطي الفتحة الافتراضية يُستعمل فقط إن كان الاستعمال بلا أبناء.
* أخطاء واضحة: فتحة غير موجودة (`did you mean 'footer'?`)، `<Fill>` مكرَّر، `<Fill>` خارج استعمال قالب.

## 4) `<Prop>` — خصائص مطلوبة ومنوَّعة
```rin
<Define#Alert>
  <Prop name=title required />
  <Prop name=tone  enum="info,warn,error" default=info />
  <Prop name=count type=int default=1 />
  <Text#t>{title} · {tone} · {count}</Text>
</Define>

<Alert title="تنبيه" tone="warn" count=3 />
```
| السمة | المعنى |
|---|---|
| `name` | اسم الخاصية (يُنشئها إن لم تُعلَن بين سمات `<Define>`) |
| `required` | إلزامية — غيابها خطأ: `<Alert> requires property 'title'` |
| `type` | `int` · `number` · `bool` · `string` · `color` |
| `enum="a,b"` | قيم مسموحة، مع اقتراح الأقرب: `must be one of: info, warn, error — did you mean 'info'?` |
| `default` | قيمة افتراضية (لا تجتمع مع `required`) |

الفحص وقت الترجمة للقيم **الحرفية** فقط؛ التعابير `{…}` لا تُفحَص لأنها تُقيَّم وقت التشغيل.
وسم `<Prop>` يُحذف من الجسم ولا يظهر في الناتج. كذلك صار خطأ «خاصية غير معروفة» يقترح الأقرب (`did you mean 'title'?`).

## 5) `...attrs` — تمرير السمات الزائدة إلى عنصر داخل القالب
```rin
<Define#Chip label="x">
  <Card#c padding=8 ...attrs><Text#t>{label}</Text></Card>
</Define>

<Chip label="Hi" width=120 radius=4 />     <!-- width/radius تصلان إلى Card -->
<Chip label="Hi" padding=20 />             <!-- الأحدث يغلب: padding=20 بدل 8 -->
```
* قالب يحوي `...attrs` يقبل سمات غير معلنة بدل خطأ «no property»، ويحقنها حيث كُتب `...attrs`.
* الترتيب مهم: ما يلي `...attrs` يغلب الممرَّر، والممرَّر يغلب ما سبقه.
* `...attrs` مسموحة داخل جسم `<Define>` فقط، وأي صيغة `...` أخرى خطأ صريح.

## الاختبار
* `tests/indsin_tags/tags_test.cpp` — 34 حالة جديدة (فروع، أخطاء بأرقام أسطر، تداخل).
* `tests/indsin_tags/concepts_demo.rin` + `concepts_e2e.sh` — يبني `indsin2web` ويتحقق (بـ Node) أن فرع `Match` واحداً بالضبط يظهر لكل قيمة، وأن `Unless/Else` يتبادلان، وأن `width` الممرَّر وصل.

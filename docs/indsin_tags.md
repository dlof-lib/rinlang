# صيغة الوسوم `<>` لواجهات indsin

صيغة ثانية لكتابة واجهات indsin بأقواس الزاوية، بدل `@view.Kind=name … .end/view`. هي **سكّر نحوي**:
تُحوَّل نصّاً إلى اللهجة الحالية قبل التحليل (`app/src/main/cpp/indsin/rin_indsin_tags.h`)، لذلك يعمل كل
شيء كما هو — المحرّك، المعاينة الحيّة (Shuttle)، `indsinc`، وPITOK — بلا أي تعديل في المحلّلات.

* الصيغتان تُخلَطان في الملف نفسه.
* عدد الأسطر محفوظ، فأرقام أخطاء Rin والمعاينة تبقى صحيحة.
* أي ملف لا يحوي وسوماً يخرج مطابقاً حرفاً بحرف (اختُبر على كل ملفات `.rin` في المستودع).
* الوسم لا يُعرَّف إلا في **بداية عبارة** (أول السطر أو بعد وسم آخر)، لذا `a < b` داخل التعابير لا تتأثّر.

## مثال

```rin
<warp count={0} />

<Column#root gap=16 padding=spacious>
  <Card#card elevation=lg radius="soft">
    <Text#value size=heading>Count: {count}</Text>
    <Button#inc variant=gradient width=120px onTap={count = count + 1}>Add</Button>
    <Divider />
  </Card>
</Column>
```

## المفاهيم

| الوسم | يُترجَم إلى |
|---|---|
| `<Column#root gap=16>` … `</Column>` | `@view.Column=root gap=16;` … `.end/view` |
| `<Divider />` | عنصر بلا أبناء (self-closing) |
| `#name` بعد اسم الوسم | اسم العنصر (إن حُذف يُولَّد اسم تلقائي مثل `divider_2`) |
| `<Text>Count: {n}</Text>` | `text="Count: " + (n);` — النص كابن، و`{ }` داخله تُدمَج في سلسلة |
| `<Button>Add</Button>` | `label="Add";` (وفي `Banner` يصبح `message=`) |
| `<Banner dismissible />` | `dismissible=true;` (سمة منطقية مختصرة) |
| `<warp count={0} />` | `warp count = 0;` (عدة خلايا في وسم واحد مسموحة) |
| `<Import src="lib/ui.og.rin" />` | `@import "lib/ui.og.rin";` |
| `<Loop#canvas>` … `</Loop>` | `@loop=canvas` … `.end/loop` |
| `<Container#App>` … `</Container>` | `@container=App` … `.end/container` |
| `<Element.Button#run>` … `</Element>` | `@element.Button=run` … `.end/element` |
| `<> … </>` | تجميع شفاف (Fragment): لا يُنتج عنصراً |
| `<Define#N p=v>` … `<N p=x />` | قالب مكوّن بخصائص و`<Slot />` (انظر «مفاهيم متقدمة») |
| `<Repeat times=3 as=k>` | تكرار وقت الترجمة |
| `<If cond={c}>` … `<Else>` | شروط تفاعلية عبر `visible=` |
| `<Fun#f params="a,b">` | `fun f(a, b) { … }` |
| `<Screen#app size=phone bg=#0B1020>` … `</Screen>` | جذر شاشة بحجم وخلفية حرّين (انظر [indsin_design_freedom.md](indsin_design_freedom.md)) |
| `<Palette seed=#6C5CE7 dark />` | ثيم كامل من لون واحد، يُفعَّل تلقائياً |
| `<Theme#Brand primary=#… active />` | `@theme=Brand …; .end/theme` |
| `<Tokens brand=#6C5CE7 />` | `let brand = "#6C5CE7";` — ثوابت تصميم |
| `<Style#card radius=16 />` + `class="card"` | أنماط مسمّاة قابلة للوراثة (`extends=`) |
| `<Hero>` `<Stat>` `<Chip>` `<Alert>` `<Section>` `<ListTile>` `<Toggle>` `<EmptyState>` | مكوّنات جاهزة تتوسّع وقت الترجمة (انظر [indsin_design_freedom.md](indsin_design_freedom.md) §8) |
| `<!-- تعليق -->` | `// تعليق` |

## قيم السمات

| الكتابة | المعنى |
|---|---|
| `"نص"` أو `'نص'` | سلسلة حرفية |
| `{تعبير Rin}` | تعبير يُقيَّم ويُعاد تقييمه عند تغيّر warp: `visible={count > 3 and !dark}`، `onTap={count = count + 1}` |
| `12` `120px` `250ms` | أرقام وأطوال وأزمنة كما هي |
| `50%` `60vw` `50vh` `390x844` | تُقتبَس تلقائياً كنص (نسبة/وحدة شاشة/حجم) |
| `#1E1E2E` | لون (يُقتبَس تلقائياً) |
| `true` `false` `null` | ثوابت |
| `spacious` (كلمة مجرّدة) | توكن نصّي كما في HTML، أي `"spacious"` |

> متغيّر Rin يُكتب دائماً بين أقواس معقوصة: `visible={flag}` وليس `visible=flag`.

الكيانات داخل نص الابن: `&lt;` `&gt;` `&amp;` `&lbrace;` `&rbrace;` `&quot;`.

## مفاهيم متقدمة (تُوسَّع وقت الترجمة)

### قوالب المكوّنات: `<Define>` و`<Slot />`
```rin
<Define#Pill label="pill" color=#3498db>      <!-- الخصائص وقيمها الافتراضية -->
  <Card#box padding=10 bg={color}>
    <Text#t>{label}</Text>
    <Slot />                                  <!-- مكان أبناء الاستعمال -->
  </Card>
</Define>

<Pill label="Hello" />
<Pill#warn label={"n=" + count} color=#e74c3c><Text>child</Text></Pill>
```
* كل معرِّف في `{ }` داخل القالب يطابق اسم خاصية يُستبدل بقيمتها.
* أسماء `#id` داخل القالب تصير فريدة لكل نسخة (`box_warn`، `box_2` …).
* خاصية غير معلنة على `Define` خطأ صريح؛ والقالب المتعاود ذاتياً يُرفض (`too deep`).

### التكرار: `<Repeat times=N as=k [start=0]>`
يُنسَخ الجسم N مرة وقت الترجمة، و`{k}` تُستبدل بالرقم، والأسماء تُلحَق بـ `_0 _1 …` (حتى 500 نسخة).

### الشروط: `<If cond={…}>` / `<Else>` / `<Else cond={…}>`
تُترجَم إلى `visible=(شرط)` على كل عنصر ابن (وتُدمَج مع `visible=` الخاصة بالعنصر بـ `and`)، فتبقى تفاعلية
وتُعاد عند كل تغيّر في warp. `<Else>` يجب أن يلي `</If>` مباشرة، وسلاسل else-if تعمل.
> يتطلّب أن يطوي المحرّك `visible=false` لكل العناصر — أضفتُ ذلك في `rin_indsin_layout.h` و`rin_indsin_paint.h`
> (كان يعمل سابقاً لـ Banner/Tag فقط): العنصر المخفي حجمه صفر ولا يُرسَم.

### الدوال: `<Fun>` و`<Rin>`
```rin
<Fun#bump params="step">
  count = count + step;      <!-- كود Rin عادي يُنسَخ حرفياً -->
</Fun>
<Button onTap={bump(1)}>+1</Button>

<Rin> let theme = "dark"; </Rin>   <!-- كتلة Rin خام بلا تغليف -->
```
`<Fun#name params="a, b">` ⇒ `fun name(a, b) { … }`. الجسم لا يُحلَّل كوسوم (يمكن أن يحوي `<` للمقارنة).

> PITOK يرفض الكود العام (`fun`/`let`) بالتصميم، لذا ملفات تستخدم `<Fun>/<Rin>` تُشغَّل عبر `indsinc`/المحرّك، لا `pitokc`.

المثال الكامل: `examples/samples/indsin_tags_concepts.rin`.

## الأخطاء

`closing tag </Row> does not match <Column> opened at line 3` · `unclosed tag <Column>` ·
`unexpected closing tag` · `unterminated '{' expression` — كلها بالسطر الصحيح.

## الاختبار

```sh
g++ -std=c++17 -I app/src/main/cpp tests/indsin_tags/tags_test.cpp -o /tmp/tags_test
/tmp/tags_test $(find . -name '*.rin')     # الوسائط: ملفات للتحقق من التوافق العكسي
```

المثال الكامل: `examples/samples/indsin_tags_demo.rin`، وإضافة VS Code تلوّن الوسوم وتوفّر snippets (`<column`, `<text`, `<warp` …).

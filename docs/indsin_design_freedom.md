# حرية التصميم في indsin — الحجم والألوان وخلفية الشاشة

ثلاث حريّات جديدة، كلها تعمل في المحرّك نفسه فتصل لأندرويد وسطح المكتب وCLI معاً:

1. **خلفية الشاشة (canvas)** — كانت رماديّة ثابتة (`#121212` في HTML و`18` في المُرسِم)؛ صارت لوناً قابلاً للضبط.
2. **حجم الشاشة والعناصر** — أي حجم جهاز، وأحجام نسبية، ونسبة أبعاد.
3. **الألوان** — ثيم كامل من لون واحد، أو ثيم بأدوار مخصّصة.

المثال الكامل: `examples/samples/indsin_design_freedom.rin`.

## 1) لون الخلفية الافتراضي

ترتيب الأولوية (الأول يغلب):

| # | المصدر | كيف |
|---|---|---|
| 1 | الشاشة نفسها | `<Screen bg=#0B1020>` (أو `canvas="…"` على الجذر) |
| 2 | **الافتراضي من المضيف** | `rin_indsin_session_set_default_background(s, "#1E1E2E")`، أو في أندرويد قائمة الجهاز ← «خلفية الشاشة» |
| 3 | الثيم | دور `background` في الثيم الفعّال (`<Theme>` / `<Palette>` / `@theme`) |

* يقبل أي لون يعرفه المحرّك: `#RGB` `#RRGGBB` `rgb()` `hsl()` وأسماء CSS، أو اسم دور (`surface`).
* المسح: `set_default_background(s, "")` يعيد إلى لون الثيم.
* حاويات التخطيط (`Column/Row/Stack/Spacer`) **شفّافة** ما لم تُعطَ `bg=`؛ كانت تملأ نفسها بلون الثيم فتحجب
  لون الشاشة (وترسم مربعاً غامقاً خلف صفوف داخل `Card`).
* غلاف النتيجة (tap/render) يحمل الآن `"canvas":{"width","height","background","source"}`؛ و`rin_indsin_session_canvas_json()` يعطيه وحده.

## 2) الحجم

### حجم الشاشة — `<Screen>`

```rin
<Screen#app size=phone bg=#0B1020> … </Screen>
<Screen#t size="1024x768"> … </Screen>          <!-- حجم مخصّص -->
<Screen#l size=phone_landscape> … </Screen>     <!-- يقلب المحورين -->
<Screen#w width=320 height=200> … </Screen>     <!-- لكل محور على حدة -->
```

| سمة الوسم | تُكتب كـ | المعنى |
|---|---|---|
| `size=` | `screen=` | إعداد جاهز أو `"WxH"` (+ لاحقة `_landscape`) |
| `width=` | `canvas_width=` | عرض الشاشة (رقم أو `%` أو `vw`) |
| `height=` | `canvas_height=` | **حدّ أدنى** لارتفاع الشاشة؛ المحتوى الأطول يمدّها |
| `bg=`/`background=` | `canvas=` | لون الخلفية |

الإعدادات: `watch` 184×224 · `phone_small` 360×640 · `phone` 390×844 · `phone_large` 430×932 · `foldable` 673×841 ·
`square` 600×600 · `tablet` 768×1024 · `tablet_large` 1024×1366 · `laptop` 1280×720 · `desktop` 1280×800 ·
`desktop_wide` 1440×900 · `tv` 1920×1080. بلا إعلان يبقى السلوك القديم (عرض المضيف).
المضيف: `rin_indsin_session_resize(s, width, viewportHeight)`. في أندرويد تتبع المعاينة عرض اللوحة المعلَن تلقائياً.

### حجم العناصر

`width` و`height` و`min_*` و`max_*` تقبل الآن:

| القيمة | المعنى |
|---|---|
| `120` / `120px` | بكسل (كما كان) |
| `50%` | نسبة من الحاوية الأب (داخل `Row` تُحسب على عرض الصف، لا على المحور غير المحدود) |
| `full` `half` `third` `two-thirds` `quarter` | اختصارات: 100% · 50% · 33% · 67% · 25% |
| `60vw` `50vh` `100vmin` `100vmax` | نسبة من **الشاشة** (viewport) لا من الأب |

`aspect="16:9"` (أو `1.5`) على أي عنصر: الارتفاع يتبع العرض المحدَّد (صريح أو مفروض من الأب)؛ `height=` الصريح يغلب.

> في الوسوم تُقتبَس `50%` و`60vw` و`390x844` تلقائياً. في صيغة `@view` القديمة اكتبها بين علامتي اقتباس: `width="50%";`.

## 3) الألوان

```rin
<Palette seed=#6C5CE7 dark />          <!-- ثيم كامل من لون واحد (WCAG)، يُفعَّل تلقائياً؛ light للفاتح -->
<Theme#Brand primary=#FF00AA background=#101820 active />   <!-- أدوار بعينها فوق الثيم الحالي -->
```

* `<Palette seed=… [dark|light]>` ⇒ `@theme=… seed="…"; active=true;` — يولّد primary/secondary/surface/text/border…
  بتباين مضمون، ثم يمكن تجاوز أي دور بعده بـ `<Theme>`.
* `<Theme#Name role=#hex … [active]>` يبدأ من ثيم بالاسم نفسه إن وُجد (Midnight, Ocean…) وإلا من الفعّال، ويبدّل الأدوار المذكورة فقط.
* `seed=` يعمل أيضاً في كتلة `@theme=Name seed="#6C5CE7"; dark=true; active=true; .end/theme`.

## 4) تدرّجات لونية (جديد)

```rin
<Screen#app size=phone bg="#0B1020,#4B2A8A,160">           <!-- خلفية الشاشة متدرّجة: لون1,لون2[,زاوية] -->
  <Card#c width=full height=90 radius=18 gradient="#FD79A8,#6C5CE7,90" />   <!-- أي عنصر -->
  <Button#b gradient="#FDCB6E,#E17055,135">Warm</Button>
</Screen>
```

* من لونين إلى 16 لوناً بمسافات متساوية: `gradient="#FF0000,#00FF00,#0000FF,90"` (آخر رقم = الزاوية).
* الخلفية الافتراضية من المضيف تقبل تدرّجاً أيضاً: `set_default_background(s, "#0B1020,#4B2A8A,160")` (وفي أندرويد «تدرّج الشفق/المحيط» من القائمة).
* الزاوية بنمط CSS: `180` (الافتراضي) من الأعلى للأسفل، `90` من اليسار لليمين، `135` قطري.
* الفواصل داخل `rgb()` / `hsl()` محترمة؛ اقتبس القيمة في الوسوم لأنها تحوي فاصلة: `bg="#a,#b"`.
* يعمل في المُرسِم الأصلي (PNG) وتصدير HTML (`linear-gradient`) وخطة الرسم JSON (`color2` و`angle`) وأندرويد (بزاوية أيضاً).
* `canvas_json` يعيد `background2` و`angle` عند تدرّج الشاشة. `canvas_json` يعيد أيضاً `stops` (كل المحطات).
* كان `gradient=` يعمل في أندرويد فقط (عمودياً)؛ صار يظهر في كل المضيفين، فتتغيّر لقطة `indsin_showcase` عن قبل (التغيير مقصود).

## 5) إصلاح: `onTap={x = expr}`

صيغة التعيين المباشر الموثّقة في الوسوم (`onTap={count = count + 1}`) كانت ترجع خطأ
«onTap must be a handler name or call». صارت تعمل في كل الإيماءات (`onTap` / `onLongPress` / `onDoubleTap` / hover)،
وتكتب خلية `warp` موجودة فقط؛ تعيين اسم غير معرَّف يُبلَّغ كخطأ صريح بدل أن يُتجاهل.

أمثلة كاملة: `examples/samples/indsin_app_full.rin` (تطبيق لوحة مهام) و`examples/samples/indsin_gradients.rin`.

## 6) ظلال أصلية: `elevation` و`shadow` و`shadowColor`

```rin
<Card#c elevation=lg />                                   <!-- none | xs | sm | md | lg | xl أو 0..5 -->
<Card#g bg=#6C5CE7 shadowColor=#6C5CE7 elevation=xl />    <!-- توهّج ملوّن -->
<Button#b shadow=14 shadowColor=#FD79A8 shadowY=6>Glow</Button>
```

| السمة | المعنى |
|---|---|
| `elevation=` | ظل من سلّم المستويات (نفس جدول `rin_indsin_system.h`)؛ كان يُقرأ ولا يُرسَم |
| `shadow=N` | الصيغة القديمة: تمويه N بكسل (إزاحة رأسية N/2.5، أسود ~47%) |
| `shadowColor=` | لون الظل/التوهّج (أي لون أو دور ثيم) |
| `shadowY=` | تجاوز الإزاحة الرأسية |

يُرسَم في المُرسِم الأصلي (PNG) وتصدير HTML (`box-shadow`) وخطة الرسم JSON (`shadow:{blur,dy,spread,color,alpha}`) وأندرويد.
عيّنتا `indsin_showcase` و`indsin_system_demo` تتغيّران عن قبل لأنهما تستعملان `shadow`/`elevation`. مثال: `examples/samples/indsin_shadows.rin`.

## 7) مفاهيم جديدة في الوسوم: `<Tokens>` و`<Style>` + تبديل الثيم

```rin
<Tokens brand=#6C5CE7 pad=20 />                            <!-- ثوابت تصميم → let brand = "#6C5CE7"; let pad = 20; -->
<Style#card radius=16 elevation=md bg=surface padding=14 /> <!-- نمط مسمّى قابل لإعادة الاستعمال -->
<Style#hero extends="card" bg={brand} />                    <!-- وراثة: السمات اللاحقة تغلب -->

<Screen#a size=phone padding={pad}>
  <Card#c1 class="card" />                                  <!-- class="a b": اللاحق يغلب السابق -->
  <Card#c2 class="hero" height=80 />                        <!-- السمة الصريحة على العنصر تغلب النمط -->
  <Button#t onTap={toggleTheme()}>Dark / Light</Button>
  <Button#s onTap={setThemeSeed("#E17055", false)}>Orange</Button>
</Screen>
```

* `<Tokens name=value …/>` تُستعمل كمتغيّرات `{brand}` في أي سمة (هي `let` عادية في Rin، فلا تعمل مع `pitokc`).
* `<Style#name …/>` يُوسَّع وقت الترجمة؛ `class` غير معلنة ⇒ خطأ صريح بسطرها، وكذلك `extends` على نمط غير معرَّف.
* أفعال الثيم وقت التشغيل (تعمل في `onTap` وكل الإيماءات، بلا `fun`):
  `setTheme("Midnight")` · `toggleTheme()` (Dark ⇄ Light) · `setThemeSeed("#6C5CE7" [, dark])`.
  تتغيّر الألوان والخلفية في الإطار نفسه. تنبيه: سجلّ الثيمات عام على مستوى العملية، فيشمل الجلسات كلها.

مثال: `examples/samples/indsin_styles_themes.rin`.

## 8) عناصر جاهزة (مكتبة المكوّنات)

مكوّنات مركّبة تتوسّع وقت الترجمة إلى عناصر indsin الموجودة، فتعمل في كل المضيفين، وتُحقَن فقط عند استعمالها
(مصدر بلا مكوّنات يبقى مطابقاً حرفياً، وأرقام الأسطر محفوظة). تعريفك لنفس الاسم (`<Define#Chip …>`) يحلّ محلّها.

| المكوّن | الخصائص | الاستعمال |
|---|---|---|
| `<Hero>` | `title` `subtitle` `grad` + أبناء | بطاقة بتدرّج وتوهّج |
| `<Stat>` | `label` `value` `tint` `w` | بطاقة رقم (`w=30%` داخل Row) |
| `<Chip>` | `label` `tint` | حبّة صغيرة تلتفّ حول نصّها |
| `<Alert>` | `tone` (info/success/warning/danger) `title` `message` + أبناء | تنبيه بلون دور ثيم |
| `<Section>` | `title` + أبناء | عنوان قسم وأبناؤه |
| `<ListTile>` | `title` `subtitle` `trailing` | سطر قائمة |
| `<Toggle>` | `label` `cell={flag}` | سطر بمفتاح تشغيل مرتبط بخلية warp |
| `<EmptyState>` | `title` `message` + أبناء | حالة فارغة |

```rin
<warp notify={true} />
<Screen#a size=phone padding=20 gap=14>
  <Hero title="Library" subtitle="built-in components"><Chip label="new" tint=#00B894 /></Hero>
  <Row#r gap=10><Stat label="Users" value="128" w=30% /><Stat label="Sales" value="42" tint=success w=30% /></Row>
  <Alert tone=warning title="Heads up" message="Disk almost full" />
  <Section title="Settings">
    <ListTile title="Account" subtitle="you@example.com" trailing="›" />
    <Toggle label="Notifications" cell={notify} />
  </Section>
</Screen>
```

مثال كامل: `examples/samples/indsin_components.rin`.

### إصلاحات في العناصر الأصلية
* **`Switch` و`Checkbox`** كانا يُرسمان بلون الخلفية عند التشغيل (مسار «on» غير مرئي) ويتمدّدان لعرض الشاشة داخل جذر `Column`؛
  صارا بلون `primary` وبحجمهما الثابت (44×24 و22×22) ما لم يُعطَ `width=`/`sizing=fill`. المقبض أبيض عند التشغيل.

## 9) عناصر تسجيل الدخول

**خصائص `Input`/`TextArea` الجديدة:** `secure=true` يخفي القيمة بنقاط «•» (و`reveal=true` يُظهرها)، `error="رسالة"` يلوّن الحدّ بدور `danger`
(بسماكة 2)، `valid=true` حدّ `success`، `disabled=true` يخفّت الحقل. تعمل في الرسم والـ JSON وأندرويد.

**التحرير المرتبط:** اربط الحقل بخلية `warp` بكتابة `value={cell}` (مرجع مباشر للخلية). لمس الحقل يُرجع في غلاف `tap` الحقل
`"focus":{"id","name","kind","bind","value","placeholder","inputType","secure","multiline"}`؛ يفتح المضيف لوحة/نافذة تحرير ثم يستدعي

```c
char* rin_indsin_session_set_input(void* session, const char* name /* focus.bind */, const char* text);
```

فتتحدّث الخلية وكل ما يعتمد عليها (`changed:[name]`). خلية غير موجودة تُرجع `error`. أندرويد: `IndsinPreviewManager.setInput` + نافذة `showInputDialog`.

**المكوّنات الجاهزة** (تُحقن عند الاستعمال فقط):

| الوسم | الخصائص |
|---|---|
| `<Field>` | `label value={cell} placeholder helper error secure` |
| `<PasswordField>` | `label value={cell} show={cell} error meter` (زر إظهار/إخفاء + مقياس قوة) |
| `<LoginForm>` | `title subtitle email password show remember error emailError passError loading action submit forgot` |
| `<SocialButton>` | `provider tap` |
| `<OrDivider>` | `text` |
| `<OtpBoxes>` | `code` (ست خانات) |

### ربط Rin بعناصر الدخول: دوال `indsin.*` للنماذج

دوال نقية تعمل داخل أي خاصية (`error=`, `visible=`, `color=`) فتُعاد عند تغيّر الخلية المرتبطة:

| الدالة | الناتج |
|---|---|
| `indsin.isEmail(s)` · `isPhone(s)` · `isUrl(s)` | `true/false` |
| `indsin.passwordScore(s)` | 0..4 |
| `indsin.passwordLabel(s)` | `empty/weak/fair/good/strong` |
| `indsin.passwordTone(s)` | دور ثيم: `danger/warning/info/success` |
| `indsin.fieldError(kind, v)` | `""` أو رسالة (`email/password/phone/url/required`) |
| `indsin.maskEmail(s)` · `initials(name)` · `slug(s)` | نص |

```
<Field label="Email" value={email} error={indsin.fieldError("email", email)} />
<PasswordField value={password} show={showPw} meter=true />   <!-- مقياس + كلمة قوة ملوّنة -->
```

### الحفظ في حاويات Rin (`store*`)

واجهات indsin تحفظ بياناتها في حاويات `@container.doc` الحقيقية (وتستعلمها بـ RCSQL) عبر مكتبة صغيرة بلغة Rin تُحقن تلقائياً
عند استعمال أي دالة منها (لا حاجة لأي استيراد):

| الدالة | الوظيفة |
|---|---|
| `storeOpen(coll, file)` | ينشئ الحاوية إن لزم ويستعيد مستنداتها من ملف JSON (إن وُجد) ويربطها بالملف؛ يعيد العدد. `file=""` = ذاكرة فقط |
| `storeSave(coll, id, map)` | يحفظ/يستبدل مستنداً (`_id` تلقائي) ويكتب الملف |
| `storeGet(coll, id)` · `storeHas` · `storeAll(coll)` · `storeCount(coll)` | قراءة |
| `storeDelete(coll, id)` · `storeFlush(coll)` | حذف / كتابة يدوية للملف |
| `storeFind(rcsql)` | استعلام RCSQL مثل `"users&age:gte(18)&order:desc(age)"` |
| `storeHash(text)` | sha256 — لتخزين كلمات المرور مجزّأة |

الحاوية تعمل أيضاً مع `sql / sqlCount / sqlPluck / insertDoc / queryDocs` مباشرة. القيمة الابتدائية لخلية يمكن أن تستدعي دالة:
`<warp total={loadUsers()} />` فتُستعاد البيانات عند فتح الشاشة. على أندرويد مرّر مساراً مطلقاً داخل `filesDir` للملف.
مثال كامل (تسجيل + دخول + استعادة): `examples/samples/indsin_accounts.rin`.
تنبيه: كلمات محجوزة لا تصلح أسماء دوال/معاملات: `save`, `go`, `show`, `fields`.

مثال كامل: `examples/samples/indsin_login.rin`. ملاحظة: داخل `Fun` استعمل `and`/`or` (لا `&&`)، ولا تسمِّ خلية `show` (كلمة محجوزة).

### مكوّنات عامة إضافية

| الوسم | الخصائص |
|---|---|
| `<Tabs>` | `tab={cell} a b c` — ثلاث تبويبات؛ المحدّد `fill` والباقي `outline`، والضغط يكتب 0/1/2 في الخلية |
| `<Stepper>` | `step={cell}` (1–4) `tint` — أربع شرائح تمتلئ حتى الخطوة الحالية |
| `<Badge>` | `text tint` — شارة صغيرة بدور ثيم |
| `<ProfileCard>` | `name email tint` + `<Slot/>` — صورة رمزية بالحروف الأولى (`indsin.initials`) |
| `<ConfirmCard>` | `title message yes no onYes onNo` — بطاقة تأكيد بزرّين |

مثال: `examples/samples/indsin_widgets.rin` (واختباره بسكربت Rin في `design_freedom_test.cpp` §17).

### التنقّل والهيكل

| الوسم | الخصائص |
|---|---|
| `<TopBar>` | `title back action onBack onAction` — شريط علوي: رجوع اختياري + عنوان + إجراء |
| `<Header>` | `title subtitle` + `<Slot/>` — ترويسة صفحة |
| `<NavBar>` | `tab={cell} a b c d` — شريط سفلي حتى 4 عناصر؛ المحدّد `fill`، والضغط يكتب 0..3 |
| `<Breadcrumb>` | `a b c` — مسار |
| `<SearchBar>` | `value={cell} placeholder` — حقل بحث + زر Clear يظهر عند وجود نص |
| `<Pagination>` | `page={cell} pages` — Prev/Next يختفيان عند الحدّين |
| `<Footer>` | `text` + `<Slot/>` — تذييل |
| `<Fab>` | `label tap tint` — زر إجراء بارز |

لتثبيت `NavBar` أسفل الشاشة ضع `<Spacer />` قبله داخل `<Screen>`: جذر الشاشة الآن يوزّع الفراغ على العناصر المرنة
(Spacer / `sizing=fill`) بحسب الحدّ الأدنى لارتفاع الشاشة (`size=`/`height=`). مثال: `examples/samples/indsin_navigation.rin`.
الإغلاق في الوسوم يكون `</Column>` بلا `#id`.

## 10) Rin ⇄ indsin: التكامل في الاتجاهين

* **indsin يستدعي Rin:** معالجات `<Fun>` وتعابير الخصائص ودوال `indsin.*` ومكتبة `store*` والحاويات وRCSQL — كلها لغة Rin نفسها.
* **Rin يقود indsin (جديد):** أي برنامج Rin عادي (لا داخل indsin فقط) يملك الآن:

| الدالة | الوظيفة |
|---|---|
| `indsin.mount(src[, width])` | يركّب شاشة ويعيد مقبضاً (رقماً)؛ المصدر وسوم أو لهجة `@view` |
| `indsin.input(h, cell, text)` · `indsin.set(h, cell, v)` · `indsin.get(h, cell)` | كتابة/قراءة خلايا warp |
| `indsin.tap(h, x, y)` · `indsin.tapText(h, "Sign in")` | ضغط بإحداثيات أو بنص الزر/العنصر (`"#name"` باسم العنصر) → `{handled, changed, error}` |
| `indsin.find(h, text)` | `{x,y,w,h,cx,cy}` أو `nil` |
| `indsin.texts(h)` | كل النصوص الظاهرة (Text + Button) |
| `indsin.png(h, path)` · `indsin.html(h)` · `indsin.json(h)` · `indsin.free(h)` | تصدير وتحرير |

يفتح هذا اختبار الواجهات والأتمتة والتقارير بلغة Rin نفسها (`examples/rin_drives_indsin.rin` يختبر شاشة الحسابات كاملة: تسجيل، تكرار، استعادة من الملف، دخول).
آلية التثبيت: `rin::interpreterInstallers()` في `rin_interpreter.h` — كل `Interpreter` جديد يستدعي مثبّتات المضيف، فتصل دوال `indsin.*` إلى أي برنامج في المحرّك.

## 11) Box / Container: صندوق مرن وتخصيص شامل

`<Box>` (أسماء بديلة: `Container` · `Panel` · `Frame`) صندوق متعدّد الأبناء. **وضع المرن:** أي من `direction · gap · justify · align · valign`
يحوّله إلى Row/Column كاملة الميزات؛ بدونها يبقى التكديس الرأسي القديم.

| الخاصية | القيم |
|---|---|
| `direction` | `column` (افتراضي) · `row` |
| `gap` · `padding` · `align` · `valign` | كما في Row/Column |
| `justify` | `start` · `center` · `end` · `between` · `around` · `evenly` — يوزّع فراغ المحور الرئيسي (يلزم عرض/ارتفاع للصندوق: `width=full` مثلاً) |
| `shape` | `circle` (قطر = العرض، aspect 1) · `square` · `pill` |
| `border` + `borderColor` + `border_style` | `solid` · `dashed` · `dotted` (الزوايا المستديرة تُهمل في المتقطع) |
| `border_top/right/bottom/left` | `"3 #FF7675"` أو `"3 primary"` — حدّ لجهة واحدة |
| `outline` · `outline_color` · `outline_offset` | حلقة خارج الصندوق |
| `opacity` | `0..1` أو `50%` — للصندوق وأبنائه كلهم (حدود، نص، ظلال) |
| `bg_opacity` | شفافية الخلفية فقط |
| `pattern` · `pattern_color` · `pattern_size` | `stripes` · `vstripes` · `dots` · `grid` · `checker` |
| `bg` · `gradient` · `radius` · `elevation` · `shadow` · `shadowColor` · `min_*/max_*` · `aspect` | كما سبق |

تعمل خصائص المظهر على أي عنصر لا الصندوق وحده، وهي في الرسم الأصلي وHTML وJSON وأندرويد.
**تغيير:** `opacity` كان يخفّف الخلفية فقط؛ صار يشمل العنصر وأبناءه (تأثيرات `effect="grow"` تظهر الآن متدرّجة كاملة).
مثال: `examples/samples/indsin_box.rin`. الملاحظة: لا يوجد padding لكل جهة بعد (استعمل `margin_*` على الأبناء).

## الاختبار

```sh
# محرّك + C API (يحتاج مصادر الـ runtime كما في CMake: هدف indsinc)
g++ -std=c++23 -I app/src/main/cpp tests/indsin_design_freedom/design_freedom_test.cpp <engine objs> -lz -o dft && ./dft
# مترجم الوسوم (header-only)
g++ -std=c++17 -I app/src/main/cpp tests/indsin_tags/tags_test.cpp -o /tmp/tags_test && /tmp/tags_test
# معاينة بلا واجهة
rin_indsin_preview examples/samples/indsin_design_freedom.rin --bg "#223344" --canvas --png out.png
```

## ما تغيّر في الملفات

`rin_indsin_tokens.h` (كلمات الحجم، `vw/vh`، `aspect`، الإعدادات، `resolveCanvasSize`، `seed=`) ·
`rin_indsin_layout.h` (ارتفاع الشاشة الأدنى وتوزيع Spacer عليه، `aspect`، نسب داخل `Row`) · `rin_indsin_paint.h` (`resolveCanvas`/`paintFrame`، حاويات شفافة) ·
`rin_indsin_c_api.*` (`set_input` + `focus`، الخلفية الافتراضية، `resize`، `canvas_json`، حجم اللوحة) · `rin_indsin_html.h` (لون اللوحة) ·
`rin_interpreter.h/.cpp` (خطّاف `interpreterInstallers`) · `rin_indsin_needle.h` (تعيين مباشر في المعالجات + أفعال الثيم) · `rin_indsin_tags.h` (`<Screen>/<Theme>/<Palette>`، اقتباس القيم النسبية) · أندرويد: `IndsinFabricView/PreviewActivity/PreviewManager/RinEngine` + JNI.

## أنواع حقول الإدخال (Text / TextArea / Password / Number / Email / Search / Tel)

| المكوّن | الأساس | ما يفعله |
|---|---|---|
| `<TextField label value placeholder maxlength>` | `Input type=text` | نص قصير (اسم، عنوان) |
| `<TextAreaField label value height maxlength>` | `TextArea` | فقرات/أكواد متعددة الأسطر + عدّاد `n/max` |
| `<PasswordField label value meter>` | `Input type=password` | إخفاء الأحرف + مؤشر قوة اختياري |
| `<NumberField label value min max prefix suffix>` | `Input type=number` | أرقام/كميات، تنقية فورية، تحقق من النطاق |
| `<EmailField label value>` | `Input type=email` | خطأ تلقائي "Enter a valid email" |
| `<SearchField value placeholder>` | `Input type=search` | أيقونة بحث + زر مسح |
| `<PhoneField label value>` | `Input type=tel` | أرقام الهواتف (`+ ( ) -` ومسافات) |

- **الربط**: `value={cell}` يربط الحقل بخلية warp؛ عند الإدخال تُنقّى القيمة حسب النوع (number: أرقام ونقطة واحدة وإشارة بادئة، tel: أرقام ورموز الهاتف، email/url: بلا فراغات) ثم تُقصّ بـ `maxlength`.
- **التحقق التلقائي**: حدّ أحمر + رسالة للقيم غير الصالحة (`validate=false` يعطّله؛ `min`/`max` للأرقام).
- **الرسم**: `type=password` يخفي القيمة، `prefix`/`suffix`، عدّاد `maxlength` (`counter=true` لغير TextArea).
- **focus**: غلاف `tap` يحمل `inputType` و`maxLength` و`multiline` و`min`/`max` ليختار تطبيق الأندرويد لوحة المفاتيح المناسبة.
- **Rin**: `indsin.isNumber(s)` و`indsin.fieldError(kind, value[, min, max])` (email/password/phone/tel/url/number/required).
- مثال كامل: `examples/samples/indsin_fields.rin`.

### أنواع إضافية

| المكوّن | ما يفعله |
|---|---|
| `<UrlField>` | رابط، يتحقق من `http(s)://` تلقائياً |
| `<DateField>` / `<TimeField>` | تاريخ `YYYY-MM-DD` / وقت `HH:MM` مع أيقونة؛ على أندرويد يفتح منتقياً أصلياً |
| `<MultilineField>` | نص طويل (TextArea بعدّاد `maxlength`) |
| `<ReadOnlyField>` | عرض قيمة مظلّلة لا تقبل التعديل (`readonly=true` على أي حقل) |
| `<AutocompleteField options="a,b,c">` | حتى 4 اقتراحات أثناء الكتابة؛ اللمس يملأ الحقل (`indsin.suggest`) |
| `<CodeField language=rin height=180>` | `<CodeEditor>` بأرقام أسطر وتلوين الصياغة (كلمات مفتاحية/نصوص/أرقام/تعليقات) |
| `<RichTextField>` | شريط تنسيق (B / I / U / H / List / Link) يضيف رموز Markdown؛ ليس WYSIWYG |
| `<HiddenField name value>` | قيمة غير ظاهرة تبقى في الـ fabric (الخلية نفسها هي التخزين) |
| `<ValidatedField rule=…>` | `rule`: required · email · url · phone · number · date · time · length (`min`/`max` أطوال) · pattern (`pattern` regex + `message`) |

دوال Rin: `indsin.validate(rule, value[, min, max, pattern, message])` و`indsin.suggest(value, "a,b,c", i)`، و`indsin.fieldError` تقبل أيضاً `date` و`time`.

### الكتابة داخل الحقل (أندرويد)
- الافتراضي: لمس حقل نصي يجعله محرراً حقيقياً في مكانه (لوحة المفاتيح + مؤشر كتابة) بلا نافذة منبثقة. التنقية و`maxlength` تُطبَّق أثناء الكتابة بنفس قواعد المحرك.
- زر لوحة المفاتيح في شريط المعاينة يبدّل إلى النافذة المنبثقة (الاختيار يُحفظ). Done/Back ينهيان الكتابة، ولمس خارج الحقل أيضاً.
- حقول التاريخ والوقت تستعمل دائماً المنتقي الأصلي.
- غلاف `focus` صار يحمل `rect` و`language` و`kind` (`code`).

## فيديو من الإنترنت (رابط أو API)

```
<VideoPlayer src={cell} title="…" autoplay=false loop=false muted=false controls=true start=0 poster="…" caption="…" />
<Video video_url="https://…" ratio=1.7778 />
```
- **المصادر المدعومة**: YouTube (`watch` · `youtu.be` · `shorts` · `embed` · `live`، مع `t=`/`start=`) · Vimeo · Dailymotion (تضمين) · ملفات `mp4/webm/mov/mkv/3gp` و HLS `.m3u8` (تشغيل مباشر) · أي صفحة تضمين أخرى (WebView).
- **خيارات**: `autoplay` · `loop` · `muted` · `controls` · `start` (ثوانٍ) · `poster` (وإلا مصغّرة YouTube/Dailymotion) · `title` · `error`. يظهر مؤشّر تحميل، وعند الفشل رسالة تُعيد المحاولة باللمس.
- **من API**: `src` خلية عادية، فاجلبها بـ Rin: `let r = httpGet(url); src = indsin.videoUrl(r.json, "data.items[0].video.url");` — بلا مسار يبحث تلقائياً في `video_url · stream_url · hls · mp4 · embed_url · url · src …`.
- **دوال Rin**: `indsin.videoInfo(url)` → `{provider,id,embed,thumbnail,direct,start,valid,label}` · `indsin.videoProvider(url)` · `indsin.videoThumb(url)` · `indsin.isVideoUrl(url)` · `indsin.videoUrl(data[, path])`.
- المعاينة في المحرك ترسم شارة المزوّد وزر التشغيل؛ التشغيل الفعلي على أندرويد. مثال: `examples/samples/indsin_video.rin`.

### المصغّرات (ثابتة ومتحركة)
- **ثابتة**: `poster="…"` أو `thumbnail="…"`، وإلا تُستعمل مصغّرة YouTube/Dailymotion تلقائياً. تظهر مع زر تشغيل وشارة المزوّد وعنوان، واللمس يبدأ المشغّل (بصوت). `facade=false` أو `autoplay=true` يحمّل المشغّل فوراً.
- **متحركة**: `animated=true` تتناوب لقطات (YouTube: الرئيسية + 1/2/3.jpg؛ الملف المباشر: 4 لقطات تُستخرج من الفيديو نفسه)، أو `preview="…"` صورة متحركة `gif/webp` أو مقطع قصير صامت `mp4/webm` يُعاد تشغيله.
- دوال Rin: `indsin.videoThumb(url)` · `indsin.videoThumbs(url)` (مصفوفة اللقطات). المعاينة في المحرك تضع شارة «Preview» عند التحريك.

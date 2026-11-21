# `field ==> body` · `head.X` · `rin--g==>` — ثلاثة مفاهيم وسوم

سكّر نحوي في [مترجم الوسوم](indsin_tags.md) (`rin_indsin_tags.h`): تُترجَم وقت الترجمة إلى وسوم/عناصر موجودة، فلا يتغيّر المحرّك ولا المحلّلات، والأسطر محفوظة، وأي ملف لا يستعملها يخرج مطابقاً حرفاً بحرف (اختُبر على كل ملفات `.rin` في المستودع).

| المفهوم | الفكرة | أقصر كتابة |
|---|---|---|
| `field ==> body` | حقول حاوية تتدفّق إلى جسم (قالب) | `<field.nm_container[];>` · `<field[]>` |
| `head` | هوية `@container.app` مربوطة بعناصر التصميم | `<head.name_app />` |
| `rin--g` (تُقرأ «رِنغ») | قائمة ← قالب، عامة وليست مقيّدة بحاوية | `<rin--g==>[]&{{}};>` |

حروف الأوسمة **صغيرة فقط**: `<Field>` (حقل الإدخال الجاهز) لا يتأثّر.

---

## 1) `field ==> body`

```rin
@container.data=inv
    let apples = 5;
    let owner = "متجر النور";
    let tags = ["fresh", "local"];
.end/container.data

<field.inv[];>                          // كل الحقول، قالب افتراضي (سطر: اسم + قيمة حيّة)
<field.inv[apples, owner];>             // حقول مختارة بالترتيب المكتوب
<field.inv[!tags];>                     // الكل عدا tags
<field[];>                              // حقول الحاوية المحيطة (<Container#X> أو @container=X)
<field.inv[] type="number,bool" />      // فلترة بالنوع المستنتج من القيمة الابتدائية

<field.inv[apples, owner] tick={n}>     // جسم مخصّص: يُطبَّق على كل حقل
  <Row#line gap=8>
    <Text#k size=small>{label}</Text>
    <Text#v>{value}</Text>
  </Row>
</field>
```

**القاعدة:** `;` أو `/>` ⇒ لا جسم (القالب الافتراضي). بدونهما ⇒ جسم حتى `</field>`.

**ما بين `[ ]`:** فارغ = الكل · `a, b` = مختارة · `!a` = استثناء. يُفحَص اسم كل حقل مقابل الحاوية فيُكشف الخطأ الإملائي مع قائمة الحقول الصحيحة.

**متغيّرات الجسم** (داخل `{ }` والنص، وتُستبدل وقت الترجمة):

| المتغيّر | قيمته |
|---|---|
| `{key}` | اسم الحقل `"apples"` |
| `{value}` | قراءة حيّة `container.get("inv", "apples")` |
| `{shown}` | للعرض: `jsonEncode(...)` للمصفوفات والقواميس، وإلا كـ `value` |
| `{kind}` | `string` `number` `bool` `list` `map` `nil` `expr` (من القيمة الابتدائية) |
| `{label}` | اسم مقروء: `short_name` ← `Short name` |
| `{index}` | رقم الحقل من 0 |

**السمات:** `tick={خلية}` يُمرِّر الخلية إلى `fieldGet(name, key, tick)` لتُعاد القراءة عند تغيّرها (كما في `ContainerView`؛ الدالة تُلحَق بآخر الملف عند الحاجة) · `as="k, v"` يعيد تسمية `key`/`value` · `type="…"` فلتر الأنواع (حقل نوعه `expr` يمرّ دائماً).

**الأسماء:** كل عنصر في القالب الافتراضي يأخذ اسماً فريداً `fld_<حاوية>_<حقل>`؛ ومن الاستعمال الثاني لنفس الحاوية يُضاف رقم (`fld_inv_2_apples`).

**ما يُعدّ حقلاً:** `let` / `state` / `const` على مستوى جسم الحاوية فقط، بترتيب الإعلان (لا الدوال، ولا ما بداخلها، ولا حاويات متداخلة، ولا `<<let>>` لأن المترجم يرفعها خارج الحاوية). يعمل المسح على حاويات **نفس الملف** (يشمل الأسماء العربية). حاوية من ملف مستورد: اكتب أسماء الحقول صراحةً `<field.ext[p, q];>`.

---

## 2) `head` — هوية التطبيق ↔ التصميم

يقابل `@container.app=name_app` (انظر [container_app.md](container_app.md)). بعد الإعلان يصبح `head.<حقل>` مرجعاً حيّاً في أي تعبير: `{head.name}` · `<<head.version>>` · `bg={head.theme_color}` · `{head.seo.title}`. يُترجَم إلى `container.appInfo("name_app")["name"]` فيشمل القيم **المشتقّة** (الاسم المختصر، الاتجاه، عنوان SEO…).

```rin
@container.app=shop
    let name = "متجر النور";  let version = "1.2.0";
    let icon = "icons/192.png"; let theme_color = "#6C5CE7";
.end/container.app

<head.shop apply=theme dark />          // ثيم كامل من theme_color (يتطلب قيمة حرفية #RRGGBB)
<head.shop#bar show />                  // شريط جاهز: أيقونة + الاسم + الإصدار (يُحذف ما لم يُعرَّف)
<head.shop apply=tokens />              // let head_name / head_version / … لاستعمالها في كود Rin

<head.shop#top padding=8>               // جسم: الأبناء داخل Row (as="Column" لغيره)
  <Text#t>{head.name}</Text>
</head>

<Text#title size=heading>{head.name}</Text>
<Image#logo src={head.icon} />
```

| الشكل | الأثر |
|---|---|
| `<head.X />` أو `<head.X;>` | تفعيل `head.*` فقط (+ `apply=`) بلا عنصر مرسوم |
| `… show` | يرسم شريط الهوية؛ سمات أخرى (`padding=…`) تذهب إلى الصف |
| `<head.X> … </head>` | الأبناء داخل صف؛ لا يجتمع مع `show` |
| `apply=theme\|tokens` | `theme` ← `<Palette seed=…>` ؛ `tokens` ← متغيّرات `head_*` |
| `extern` | الحاوية في ملف مستورد (يتخطّى الفحص الساكن) |

**الحماية:** حقل غير معروف `head.nme` ⇒ خطأ باقتراح `head.name`؛ حاوية ليست `app`/`site` ⇒ خطأ؛ `head` محجوز كاسم متغيّر بعد إعلان `<head.X>`. ولأن `head.*` قراءة حيّة تُنفَّذ بعد تشغيل الشيفرة، فلا يهمّ هل أُعلنت الحاوية قبل `<head.X>` أم بعده.

**الويب/التغليف:** رأس HTML الحقيقي (`<title>`، OG، manifest، أيقونات) يبقى من `container.appHead` / `rin app inject` كما هو؛ `<head.X>` لا يكتب HTML بل يربط **عناصر الواجهة** بنفس الهوية.

---

## 3) `rin--g==>` — قائمة ← قالب

صيغة عامة: `<rin--g==> مصدر & مصدر & {{ قالب }};>`

| المصدر | المعنى |
|---|---|
| `[]` | حقول الحاوية المحيطة (يطابق `<field[]>`) |
| `["a", "b", 3]` | قائمة حرفية |
| `NAME[a, !b]` | حقول حاوية (كما في `field`) |
| `{expr}` أو `3` | قائمة/رقم من Rin — الحجم من التشغيل التجريبي كـ `<<each>>`، أو رقم حرفي |
| `&` | ضمّ المصادر بالترتيب |
| `{{ … }}` | القالب (الأخير)؛ فارغ = الافتراضي |

داخل القالب `{{x}}` ≡ `{x}`: `{{it}}` العنصر · `{{i}}` الرقم · وكذلك `key` `value` `shown` `kind` `label` `index`. خارج علامات الاقتباس فقط؛ داخل `"…"` استعمل `<<it>>`.

```rin
<rin--g==>[]&{{}};>                                   // الأبسط: حقول الحاوية المحيطة بالقالب الافتراضي
<rin--g==>["fresh", "local"] & {{ <Chip label={{it}} /> }};>
<rin--g==>inv[apples, owner] & {{ <Text#t>{{label}}: {{shown}}</Text> }};>
<rin--g==>3 & {{ <Text#n>سطر {{i + 1}}</Text> }};>
<rin--g==>{users} & {{ <ListTile title={{it}} /> }};>   // يحتاج خط الأنابيب (قياس الحجم)
```

`<field.X[];>` هو `rin--g` بمصدر حاوية؛ والقالب الافتراضي لعنصر غير حقل هو `<Text>{shown}</Text>`. القالب الواحد يُكتب مرة ويأخذ عدة أسطر بلا مشكلة (الأسطر محفوظة). لا يوجد شكل «جسم + إغلاق» لـ `rin--g`، لأن `--` ليست جزءاً من اسم وسم الإغلاق.

---

## أخطاء شائعة
`no container named 'x'` · `container 'inv' has no field 'zz' (fields: …)` · `<field[]> has no container` · `unknown head field 'head.nme' — did you mean 'head.name'?` · `<head.X> needs an app/site container` · `rin--g needs '==>'` · `the {{ template }} must be the last item` · `<rin--g==>[]> has no list`.

## الاختبار
```sh
g++ -std=c++17 -I app/src/main/cpp tests/indsin_tags/tags_test.cpp -o /tmp/tags_test && /tmp/tags_test $(find . -name '*.rin')
```
المثال: `examples/samples/field_head_ring.rin`.

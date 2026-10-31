# Indsin — مفاهيم تخطيط صفحات الويب (Web Layout)

150 مفهوماً من عالم تخطيط الويب صارت متاحة في indsin، **كلها إضافية**: لا تتغيّر أي خاصية قديمة، وأي ملف
لا يستعملها يُخطَّط حرفياً كما كان. الاختبارات: `tests/tools/test_indsin_web_layout.cpp` (118 فحصاً)،
والملفات: `indsin/rin_indsin_webkeys.h` (الأسماء) · `rin_indsin_webcore.h` (التفسير) ·
`rin_indsin_webconcepts.h` (المرور اللاحق، التصادم، Selector…) + `IndsinFabricView.kt` (المعاينة).

## كتابة الأسماء
أي خاصية تُكتب بأي من: `MinWidth` · `minWidth` · `min_width` · `min-width` (الأخيرة داخل صيغة الوسوم `<>` فقط).
تتحوّل عند البناء إلى المفتاح القانوني (`min_width`) مع بقاء ربط Warp، وتعمل معها لواحق التجاوب (`MinWidth_md`).

## 1) هيكل الصفحة
| المفهوم | يُكتب | يُترجم إلى |
|---|---|---|
| Body · Page · Main · Content · Article · Aside · Group | `<Main>` / `@view.Main=…` | Column (الاسم الأصلي يبقى في الـ inspector) |
| Navigation (Nav) | `<Navigation>` | Row |
| Wrapper · Widget · Component · Child | `<Wrapper>` | Box |
| Header · TopBar · NavBar · Footer · Sidebar · Section · Container | موجودة سابقاً | — |

## 2) الاتجاهات والمواقع
| المفهوم | الاستعمال |
|---|---|
| Position | `position="static\|relative\|absolute\|fixed\|sticky"` |
| Top / Left / Right / Bottom / Start / End | إزاحات (`top=10`، `right="5%"`، `start=8` يتبع `direction`) |
| TopLeft / TopRight / BottomLeft / BottomRight | `top_left=8` (أو `top-right=4` في `<>`) يثبّت الزاوية |
| Center / Middle | `center=true` (أفقي+عمودي) · `middle=true` (عمودي) على العنصر المطلق؛ وعلى ابن Stack يوسّطه |
| Relative | يزيح العنصر بصرياً (`top`/`left`) ويحفظ مكانه في التدفق |
| Absolute | خارج التدفق، يتموضع داخل أقرب سلف له `position`، و`left`+`right` معاً = تمدّد |
| Fixed | بالنسبة للشاشة؛ يتبع `rin_indsin_set_scroll_window(session, x, y)` |
| Sticky | `top=0` يثبّته عند حافة النافذة أثناء التمرير داخل أبيه |
| Static | الافتراضي (يتجاهل الإزاحات) |
| Anchor · Offset | `anchor="btn" anchor_side="bottom" anchor_align="center"` يلصق عنصراً بآخر؛ `offset=4` أو `offset="4,8"` / `offset_x` / `offset_y` |

## 3) الأبعاد
`width` `height` `min_width` `max_width` `min_height` `max_height` `size` `aspect="16:9"` (`AspectRatio`)،
`scale=1.2` (`Scale`)، `resize="none\|both\|horizontal\|vertical"` (سمة تُحمَل للمضيف: حقل النص/الصندوق).
قيم المحتوى: `width="min-content\|max-content\|fit-content"` (**MinContent/MaxContent/FitContent**)،
`intrinsic_size="160x90"` حجم افتراضي لعنصر بلا محتوى ذاتي (**IntrinsicSize**)،
`box_sizing="content-box"` يجعل `width` للمحتوى فقط (**BoxSizing**، الافتراضي border-box).

## 4) المساحات
`margin` (OuterSpace) · `padding` (InnerSpace) · `gap` · `row_gap` · `column_gap` · `spacing`.
**SpaceManager** (قراءة): `rin_indsin_space_report_json(session, name)` أو `indsin::webSpaceReport(strand)` ⇒
`innerW/H` (InnerSpace) · `outerW/H` (OuterSpace) · `occupied` (OccupiedSpace، مساحة اتحاد الأبناء) ·
`empty` (EmptySpace) · `availableW/H` (AvailableSpace: الشريط الحرّ بعد آخر ابن) · `occupiedRatio`.

## 5) توزيع العناصر
| المفهوم | الاستعمال |
|---|---|
| Layout · LayoutEngine | `layout="row\|column\|stack\|grid\|wrap\|flex\|block"` على أي حاوية |
| Flex · FlexDirection · FlexWrap · Flow | `<Flex flex_direction="column-reverse" flex_wrap="wrap">` أو `flow="row wrap"` (`row-reverse` = مرآة، `column-reverse` يعكس الأبناء) |
| Grid · GridRow · GridColumn | `columns=3`، `grid_column="1 / span 2"`، `grid_row="2"`، `grid_row="span 2"` |
| AutoFit · AutoFill | `columns="auto-fit"` أو `columns="auto-fill"` + `min_column_width=120` (auto-fit يمدّد الخلايا) |
| Row · Column · Stack · Wrap | موجودة |
| Align · Justify · Distribute · Place | `justify="between"` · `distribute=` (مرادف) · `place="center"` أو `place="top right"` |
| Order | `order=2` على الأبناء (ترتيب مستقر؛ يتبعه الرسم) |
| SpaceDistribution | مرادف `justify` · `auto_space=true` = توزيع الفراغ بين العناصر |
| SpaceAllocation · Priority | `space_allocation="equal\|proportional"` مع `weight=` على الأبناء |

## 6) التداخل والطبقات
| المفهوم | الاستعمال |
|---|---|
| ZIndex · Layer · LayerIndex | `z=10` أو `layer="dialog"` — يغيّر ترتيب **الرسم** فقط لا التخطيط |
| Isolation | كل حاوية تشكّل سياق تكديس لأبنائها (z داخل الأخ فقط)؛ `isolation="isolate"` مقبولة وتوثّق النية |
| Overflow · Clip · ScrollContainer | `overflow="hidden\|clip\|scroll\|auto"`، `overflow_x/_y`، `clip=true` |
| Collision · CollisionPolicy | `collision_policy="push\|shift\|hide\|none"` (+`collision_axis="x\|y"`، `collision_gap`) |
| CollisionDetection | `collision_detection="push"` على الأب، أو `indsin::webDetectCollisions` / `rin_indsin_collisions_json` |
| Avoid | `avoid="a,b"`: يبتعد عن العناصر المسمّاة فقط |
| Overlap | `overlap=false` على الأب (يمنع تداخل أبنائه) · `overlap=true` على الابن (معفى تماماً) |
| Priority | `priority=5`: الأعلى يُوضَع أولاً فيُزاح غيره |
| Bounds · Constraints | `bounds="parent\|viewport\|root"` يحصر العنصر داخل الصندوق |

## 7) المحاذاة
`text_align` (`TextAlign`) · `valign` (`VerticalAlign`) · `align` (`HorizontalAlign`) ·
`align_items` (`AlignItems`، على Row = عمودي وعلى Column = أفقي) · `align_content` · `align_self` (`AlignSelf`/`SelfAlign`،
يتفوّق على `align_items`) · `justify_content` (= `justify`) · `justify_items` · `justify_self`.
أسماء CSS مقبولة كقيم: `flex-start` `flex-end` `space-between` `space-around` `space-evenly`.
في **Stack** و**Grid**: `justify_self` أفقي و`align_self` عمودي (مثل CSS grid).

## 8) العرض والاستجابة
| المفهوم | الاستعمال |
|---|---|
| Display · Hidden · Visible | `display="none"` أو `hidden=true` يطوي العنصر (بلا فجوة)؛ `visible=false` كما كان |
| Visibility | `visibility="hidden"` يحجز مساحته ولا يُرسم |
| MediaQuery | `media="(min-width: 600px) and (max-width: 900px), (orientation: portrait)"` · `media=">=lg"` |
| DeviceSize | `device_size="phone,tablet"` (phone <600 · tablet <1024 · desktop) |
| Breakpoint · Responsive | لواحق `_sm/_md/_lg/_xl` (موجودة) |
| Viewport · ScreenWidth · ScreenHeight | `vw/vh/vmin/vmax` في الأبعاد (موجودة)، وتُغذّي `media` |

## 9) التحكم بالعناصر
**Selector**: `indsin::webQuery(root, "Column > Text.title")` أو `rin_indsin_query_selector_json` —
النوع، `#name`، `.class`، `[attr=v]` (`^=` `$=` `*=` `~=`)، `:first-child` `:last-child` `:nth-child(n)` `:empty` `:only-child`،
والروابط ` ` `>` `+` `~` والقائمة بفاصلة. **Parent/Child/Children**: `webParentOf` و`strand->children`.
Element · Component · Widget · Group · State: وسوم/مفاهيم موجودة أو مرادفات أعلاه.
**Transform**: `transform="translate(10,5) rotate(30) scale(1.2)"` أو `translate_x/_y` `rotate` `scale` —
الإزاحة تُطبَّق في المحرك (تظهر في كل الواجهات)، والتدوير/التحجيم في معاينة أندرويد (Canvas).

## 10) الحدود والمظهر
`border` · `radius` (`BorderRadius`) · `outline` · `shadow` / `elevation` (`BoxShadow`/`Shadow`) · `background` · `opacity` ·
`color` · `cursor` — موجودة؛ المرادفات الجديدة تحوّلها تلقائياً.

## 11) التمرير
`scroll_y=true` / `scroll_x=true` / `scroll_container=true` (أو `overflow="scroll\|auto"`): الحاوية تقيس أبناءها بحجمهم
الطبيعي وتبقى بحجم نافذتها (`height=`)، `scroll_top` / `scroll_left` إزاحة التمرير،
`scroll_snap="start\|center\|end"` يلتقط أقرب ابن، `scroll_bar="auto\|hidden\|thick"` مؤشر التمرير،
`scroll_behavior="smooth"` تلميح للمضيف. للتمرير برمجياً: `rin_indsin_scroll_to(session, "name", x, y)`.

## حدود صريحة
* **القصّ** (`overflow`) هندسي: المستطيلات تُقصّ فعلاً، أما النص فيبقى إن ظهر نصفه على الأقل وإلا يُحذف (لا قصّ حرف بحرف) — في معاينة أندرويد يُستعمل `clipRect` الحقيقي.
* **التدوير والتحجيم** يُرسمان في معاينة أندرويد فقط؛ تصدير PNG/HTML يطبّق الإزاحة (translate) فقط.
* **الإزاحة الافتراضية** للعنصر المطلق بلا top/left/right/bottom = زاوية محتوى أبيه (وليس موضعه في التدفق).
* **Isolation**: لا يوجد تكديس عالمي للـ z؛ كل حاوية سياق مستقل (سلوك `isolate` دائماً).
* **Fixed/Sticky** يحتاجان من المضيف استدعاء `rin_indsin_set_scroll_window` عند تمرير الصفحة.

# القوائم القابلة للفتح والإغلاق

`Accordion` · `ExpandableList` · `TreeView` · `NestedList` — أربعة مكوّنات لإظهار/إخفاء المحتوى. كلٌّ منها **سكّر بنيوي**:
عند البناء (`rin_indsin_collapsible.h`) يتوسّع إلى `Row/Column/Text/Divider/Button` حقيقية مرتبطة بخلايا warp، فيعمل في كل
المضيفين (أندرويد، سطح المكتب، CLI، تصدير HTML) بلا أي كود قياس/رسم جديد. تعمل بصيغتي `@view.X=name` و`<X#name>`.

## نموذج الحالة
* لكل عقدة قابلة للطي خلية `"<name>_open"` (`"true"`/`"false"`). `open=<خليتك>` يربط خليتك أنت (تُقلب من الخارج أيضاً).
* الجسم المطويّ `visible=<خلية>` فيُطوى إلى حجم صفر ولا يُرسم.
* ترويسة قابلة للنقر (`onTap` حقيقي) + `expanded=` + `a11y_label` لقارئات الشاشة.
* `chevron=arrow|plus|ascii` (الأخيران بحروف ASCII للمُرسِم الأصلي).

## Accordion
```rin
<Accordion#faq multiple=false variant=outlined>
  <AccordionItem#q1 title="What is Rin?" subtitle="Basics" badge="2" open=true> … </AccordionItem>
  <AccordionItem#q2 title="Where?" disabled=true> … </AccordionItem>
</Accordion>
```
| السمة | المعنى |
|---|---|
| `multiple` | `false` (افتراضي): قسم واحد مفتوح؛ فتح قسم يغلق الباقي (`accordion()`). `true`: أقسام مستقلة. |
| `variant` | `outlined` (إطار + استدارة) · `flush` (بلا إطار) · `filled` (ترويسة بخلفية surface). |
| `controls` | مع `multiple=true`: أزرار «Expand all / Collapse all». |
| على AccordionItem | `title` `subtitle` `icon` `badge` `open` `disabled`. |
`AccordionItem` وحده (أو `Collapsible`/`Details`/`Disclosure`) = قسم قابل للطي مستقل. التداخل بأي عمق مدعوم.

## ExpandableList
```rin
<ExpandableList#inbox title="Inbox" items="Mail 1, Mail 2" marker=number />   <!-- يطوي القائمة كلها، والعنوان يعرض العدد -->
<ExpandableList#feed title="Feed" items="a;b;c;d;e" limit=2 />                <!-- معاينة: أول صفّين + «Show more (3)» -->
```
`items` تفصل بـ`,` أو بـ`;` (إن وُجدت `;` — لعناصر تحوي فواصل)، أو أبناء حقيقيون. `marker=bullet|number|dash|none`،
`count=false` يخفي العدد، `more=`/`less=` نصوص الزر.

## TreeView
```rin
<TreeView#files controls=true expanded=none sort=true selected="src/main.rin"
          paths="src/main.rin, src/lib/math.rin, docs/guide.md, README.md" />
<TreeView#t><TreeNode label="src" open=true><TreeNode label="main.rin" badge="2KB" /></TreeNode></TreeView>
```
* العقدة ذات الأبناء فرع (خلية `<name>_open`)، وإلا ورقة.
* نقر الصف = اختيار (+ فتح/إغلاق للفرع)؛ نقر السهم = فتح/إغلاق فقط. الاختيار في خلية `<tree>_selected` (أو `bind=<خلية>`)،
  والصف المحدَّد `bg=primary`. اقرأها مثلاً `{files_selected}`.
* `paths=` يبني الشجرة من مسارات (`/` فاصل، `/` في النهاية = مجلد)؛ `sort=true` مجلدات أولاً؛ `expanded=all` يفتح الكل؛
  `selectable=false`؛ `indent=16`؛ `folderIcon=`/`fileIcon=`؛ للعقدة `label value icon badge open disabled`.

## NestedList
```rin
<NestedList#plan items="Design, -Wireframes, --Colors, Build" marker=number />   <!-- كل «-» مستوى -->
<NestedList#toc collapsible=true open=false><NestedItem label="Ch 1"><NestedItem label="Sec 1.1" /></NestedItem></NestedList>
```
`marker=bullet` (• ◦ ▪ حسب العمق) · `number` (1. / 1.1. / 1.1.1.) · `alpha` (A. a. i.) · `dash` · `none`.
`collapsible=true` يحوّل الأب إلى صف قابل للنقر بسهم؛ `open=false` يبدأ مطويّاً؛ `indent=16`.

## Actions جديدة
| الأمر | الأثر |
|---|---|
| `accordion(cell, others…)` | يقلب `cell`؛ إن صار مفتوحاً يغلق `others`. |
| `expandAll(cells…)` / `collapseAll(cells…)` | يفتح/يغلق كل الخلايا المذكورة. |
تُستعمل يدوياً أيضاً: `onTap=expandAll(a_open, b_open)`.

## ملاحظات
* الخلايا التلقائية تُنشأ بعد بناء الشجرة؛ الخصائص التي تقرؤها (`{files_selected}`) تُعاد حلّها مرة واحدة عند البدء.
* مثال: `examples/samples/indsin_collapsible.rin` · اختبار: `tests/tools/test_indsin_collapsible.cpp`.

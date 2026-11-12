# container للأعمال — فواتير · طلاب · عمّال · رواتب · ملف شخصي + CV

قالب جاهز `templates/business/business_types.rin` مبنيّ **بالكامل على ما في اللغة أصلاً**، بلا أي دالة أو مفهوم أو متغيّر جديد في المفسّر:
`defineContainerType` (وراثة وتحقق) · `onCreate`/`check` · `defineSchema` · `container.datalist` + RCSQL · `container.attempt` · `container.zip`.

```rin
@import "templates/business/business_types.rin";     // المسار من جذر المشروع
```

## الأنواع
| النوع | يرث | أهم الحقول |
|---|---|---|
| `Profile` | — | `name`(إلزامي، ≥2) `email` `phone` `birth` `address` `nationalId` `photo` |
| `Worker` | `Profile` | `position`(إلزامي) `department` `hired` `salary`(>0) `allowance` `iban` `status`(`active/leave/left`) `cv` `cvFile` |
| `Student` | `Profile` | `level`(إلزامي) `enrolled` `guardian` `guardianPhone` `fees` `paid` `status` `marks`(`map<number>`) |
| `Invoice` | — | `number` `date` `customer` `kind`(`sale/tuition`) `items:[{desc,qty,price}]` `discount` `tax` `total` `paid` `status` |
| سجل `payroll` | — | `datalist` بمخطط: `worker month base allowance deduction net` |

كل كتابة (`setField` / `container.set` / `newContainer`) تُفحص: بريد خاطئ، تاريخ غير صالح، راتب سالب، حالة خارج القائمة، درجة غير رقمية... تُرفض بخطأ `E0004`.

## الملف الشخصي والسيرة الذاتية
- بيانات الملف: حقول `Profile` + الوظيفة والراتب في `Worker`.
- **CV منظَّم**: الحقل `cv` = `{summary, skills, languages, education, experience, certificates}`؛ يُقرأ بـ`getField("sara","cv")["skills"]`.
- **CV ملف مرفق**: الحقل `cvFile` مسار الملف (pdf/docx...)؛ يُضمَّن عند الأرشفة:
```rin
container.zip(["sara", "payroll"], "hr.zip", {include: [getField("sara", "cvFile")]});
container.unzipAll("hr.zip", {extract: "restored/"});     // يعيد الحاويات ويستخرج الملف المرفق
```
النوع (`Worker`) وكل حقوله تُستعاد كما كانت. الملف المرفق يُستخرج باسمه المسطّح (`cv_sara.txt`).

## الفواتير والدفع
`total = (مجموع qty×price − discount) × (1 + tax)` يُحسب في `onCreate` عند الإنشاء. الدفع ذرّي بـ`container.attempt` فيتراجع كل شيء إن زاد المدفوع أو فشلت خطوة:
```rin
container.attempt(["fee1", "lina"], fun() {
    container.set("fee1", "paid", 3000);
    container.set("lina", "paid", container.get("lina", "paid") + 3000);   // رسوم الطالب
});
```

## الرواتب
قسيمة لكل عامل لكل شهر داخل `payroll` (`insertDoc`)، والتقارير بـRCSQL:
```rin
sql("payroll&group:by(month)&agg:sum(net)&order:asc(month)")   // إجمالي الصافي لكل شهر
sql("payroll&worker:eq(sara)&agg:sum(net)")                    // إجمالي عامل
container.sum("payroll", "net");
```
المخطط (`defineSchema`) يرفض قسيمة بحقل من نوع خاطئ.

## الحفظ كصور (PNG) — بتخطيط indsin ورسمه
بلا أي دالة جديدة: اللغة تملك `indsin.mount` و`indsin.png` و`indsin.html`، والصورة العامة `containerImage` + `imgSave`.

**1) أي حاوية كبطاقة حقول** (النظام الموجود):
```rin
let im = containerImage("sara"); imgSave(im, "sara_card.png"); imgFree(im);
```
**2) بطاقة بتخطيط indsin**: التخطيطات ملفات indsin جاهزة في `templates/business/cards/`:
`worker.rin` (ملف شخصي + CV) · `student.rin` · `invoice.rin` · `payslip.rin` · `payroll.rin` (تقرير).
جلسة indsin **معزولة** عن حاويات البرنامج، فالبيانات تُمرَّر كمتغيّر `d` عبر `json.stringify` (يعمل مع نصوص فيها `{` و`"` و`<` بلا تهريب):
```rin
let src = "<Rin>let d = " + json.stringify(container.fields("sara")) + ";</Rin>\n"
        + readFile("templates/business/cards/worker.rin");
let h = indsin.mount(src, 390);          // العرض بالنقاط
indsin.png(h, "sara.png");               // يرسم التخطيط ويحفظ PNG
indsin.html(h);                          // نفس التخطيط HTML (العربية الدقيقة)
indsin.free(h);
```
قسيمة راتب: `d = findDoc("payroll", "2026-11-sara")`. تقرير الرواتب: `d = {rows: sql("payroll&order:asc(_id)"), total: container.sum("payroll", "net")}`.
لكل العمّال دفعة واحدة: `for (let n in containersOfType("Worker")) { ... }`.

**3) الصور مع الحاوية**: `container.zip(["sara"], "pack.zip", {include: ["sara.png", "slip.png"]})` يحفظ الحاوية وصورها في أرشيف واحد (الصور تحت `files/`).

معاينة: `docs/business_worker_card.png` · `docs/business_invoice_card.png`.

**حدود:** الرسّام الأصلي لـPNG يرسم **العربية أشرطة** (لا خط عربي مدمج، انظر `indsin_live_preview.md`)، فاستعمل نصاً لاتينياً في PNG، و`indsin.html` للعربية الصحيحة. والفاصل `·` غير مرسوم في الخط المدمج (استعمل `-`). القيم العشرية تُقرَّب للعرض بـ`round`.

## الاختبار والحدود
`tests/print/business_containers.rin` · `tests/print/business_images.rin`. القائمة تُستخرج بـ`containersOfType("Worker")`. القيم المسندة مباشرة داخل جسم `@container` لا تُفحص (استعمل `setField` أو `validateContainer`)؛ ملف CV يُرفَق بالأرشفة ولا يُخزَّن داخل الحاوية نفسها.

# Link

Link مفهوم واجهة (UI) لعنصر رابط (Hyperlink) — نص ملوّن بلون الروابط مع خط
تحته افتراضياً، يُستخدم داخل [@loop](./syntax.md) مثل أي عنصر جاهز آخر
(انظر [`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md#ready-elements)):

```rin
@element.link=docsLink
    text="اقرأ التوثيق";
    href="https://example.com/docs";
.end/element
```

## الخصائص (Attributes)

- `text` — نص الرابط الظاهر.
- `href` — الوجهة. اختصار عن `onTap`: قيمة تبدأ بمخطط رابط خارجي
  (`http://`, `https://`, `mailto:`, `tel:`) تُفتح خارج التطبيق، وأي قيمة
  أخرى (اسم ملف داخل المشروع مثل `"mu.rin"`، أو مسار يبدأ بـ `/`) تُعامل
  كتنقّل داخلي عبر [`navigate()`](./control-flow.md)، تماماً مثل
  `onTap="navigate:mu.rin"` الموجود مسبقاً. وجود `onTap` صريح على نفس
  الرابط يبقى له الأولوية على `href`.
- `onTap="open:URL"` — نفس فتح الرابط الخارجي، لكن بصياغة `onTap` الصريحة
  بدل `href` (مفيد إن أردت أن يبقى `href` غير معرّف لسبب ما).
- `underline` — `"true"` (افتراضي) يرسم خطاً تحت النص، `"false"` يعطّله —
  مفيد عندما يُستخدم Link كعنصر تنقّل عادي لا يجب أن يبدو كرابط داخل نص.
- `visited` — `"true"` يحوّل لون الرابط إلى لون بنفسجي خافت للدلالة على
  أن المستخدم زاره من قبل (نفس عرف الويب)، ما لم يكن هناك `tone=`/`color=`
  صريح على العنصر (لهما الأولوية دائماً).
- `tone=` / `color=` — نفس آلية تلوين أي عنصر آخر (انظر
  [`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md)) — تتجاوز اللون الافتراضي
  ولون `visited` معاً.

## انظر أيضاً

- [`RIN_ELEMENTS.md`](./RIN_ELEMENTS.md)
- [`control-flow.md`](./control-flow.md)
- [`android.md`](./android.md)
- [`language-reference.md`](./language-reference.md)

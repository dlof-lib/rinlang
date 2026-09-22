# cross-file-containers.md — استدعاء إعلانات من ملف آخر

## ملاحظة أمانة صريحة (نتيجة تحقّق فعلي، ليست افتراضًا)
اسم هذا الملف والمثال المرافق له في المستودع (`examples/use_from_demo/`) كانا
يفترضان عبارة استيراد مخصَّصة `use A, B from "file.rin";` لاستدعاء عنصر واجهة
أو حاوية بيانات مُعرَّفة في ملف آخر **بالاسم مباشرة كأنه مكوّن قابل لإعادة
الاستخدام**. جرّبتُ هذا فعليًا عبر المفسِّر الحقيقي، والنتيجة:
- `use A, B from "file.rin";` — **خطأ تحليل نحوي حقيقي فوري** (`E0012`، `use` غير
  معروفة كعبارة مستقلة على مستوى الملف — هي محجوزة فقط داخل سياسة
  `@container`/`@make.(name)`، انظر [`MAKE_UNIT.md`](./MAKE_UNIT.md)).
- استدعاء `@view`/`@container` معرَّفة سابقًا **بالاسم المجرَّد كوسم** (`Sidebar
  role="sidebar";` كأنها مكوّن) داخل `@view` أخرى — **خطأ تحليل نحوي حقيقي** أيضًا
  (المحلِّل يقبل فقط `attribute=value;`، تعشيشًا حقيقيًا لـ`@view...` جديدة، أو
  `.end/view` — لا هوية مجرَّدة).
- اسم `@container` نفسه **لا** يُعرَّف تلقائيًا كمتغيّر قابل للقراءة المباشرة بعد
  `.end/container` (نفس النتيجة الموثَّقة في [`objects.md`](./objects.md#Object%3Dname--endObject) لـ`@Object`).

بكلمة أخرى: **إعادة استخدام مكوّن واجهة أو حاوية عبر عدّة ملفات بأسلوب "استيراد
مُسمَّى" ليس مدعومًا في هذا الإصدار من اللغة**، رغم أنه كان مخطَّطًا له. الآلية
الوحيدة التي **تحقّقتُ أنها تعمل فعليًا** لمشاركة كود بين ملفات هي `@import`
(الموثَّقة أصلًا في [`standard-library.md`](./standard-library.md#import--استيراد-وحدة)
لاستيراد وحدات `lib/`، لكنها تعمل بنفس الآلية لأي ملف `.rin` عادي أيضًا).

## `@import` — الآلية الفعلية الوحيدة
```rin
// examples/use_from_demo/sidebar.rin
fun sidebarGreeting() {
    return "Hello from sidebar.rin";
}
@container=SidebarState
    let open = true;
.end/container
```
```rin
// examples/use_from_demo/main.rin -- شغّله من جذر المستودع:
//   ./run_rin examples/use_from_demo/main.rin
@import "examples/use_from_demo/sidebar.rin";
print sidebarGreeting(); // Hello from sidebar.rin
```
```
📦 container = SidebarState
✅ .end/container (SidebarState)
📥 @import: تم استيراد "examples/use_from_demo/sidebar.rin" (من القرص)
Hello from sidebar.rin
```
`@import` ينفِّذ الملف المستورَد بالكامل داخل نفس البرنامج (كل `fun`/`@container`/
`@view` بداخله يُعرَّف على نفس المستوى، تمامًا كأنك نسخت محتواه ولصقته في بداية
الملف الحالي) — **لا** إعادة استخدام انتقائي بالاسم، ولا مساحة اسم (namespace)
منفصلة.

### تنبيه دقيق: كيف يُحلَّل المسار
المسار الممرَّر لـ`@import` **لا** يُحلَّل نسبةً إلى موقع الملف المستورِد نفسه، بل
نسبةً إلى مجلد عمل العملية (working directory) وقت التشغيل، مع قواعد بحث مختلفة
حسب شكل النص:
- مسار يحتوي `/` (مثل `"examples/use_from_demo/sidebar.rin"`) → يُعامَل كمسار قرص
  حرفي نسبةً لمجلد العمل مباشرة.
- اسم مجرَّد بلا `/` (مثل `"sidebar.rin"`) → يُعامَل كـ**اسم مكتبة**: يبحث عن
  `lib/sidebar.rin.og.rin` (يُضيف `.og.rin` تلقائيًا!) ضمن المكتبات المدمجة، مجلد
  `lib/` الخاص بالمشروع، وحزم RinPM المثبَّتة — وليس كمسار قرص حرفي إطلاقًا.
  تحقّقتُ من هذا فعليًا: `@import "sidebar.rin";` من نفس مجلد الملف المستورِد
  فشل بخطأ `E0029: module not found` يذكر صراحة أنه بحث عن `lib/sidebar.rin.og.rin`.

## انظر أيضًا
- [`standard-library.md`](./standard-library.md) — `@import` لوحدات `lib/*.og.rin`.
- [`objects.md`](./objects.md) — لماذا لا يُقرَأ اسم `@Object`/`@container` كمتغيّر مباشرة.
- [`containers.md`](./containers.md) — `@container` وأنواعه.

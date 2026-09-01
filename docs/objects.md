# الكائنات (Objects)

> راجع أيضاً: [`variables.md`](./variables.md#4-القواميسالخرائط-maps) لأساس
> القاموس الذي تُبنى عليه الكائنات، [`containers.md`](./containers.md) للفرق
> بين كائن مفرد وحاوية بيانات كاملة، و[`control-flow.md`](./control-flow.md)
> لفحص شروط على حقول الكائن.

## 1) كائن تنسيق/ستايل: `@Object`

```rin
@Object=card
    text name = "Title";
    text color = "#3498db";
.end/Object
```

يستخدم نفس صيغة [`text`/`let`](./variables.md#2-متغيّر-نصي-مُنمَّط-text) لتعريف حقول
حرة داخل كائن مُسمّى.

## 2) الأسلوب الثاني: نداء دوال `.object("id")`

```rin
.object("user01")
    name("ABOO");
    age(19);
    online(true);
.end/object

print user01;          // الكائن مُعرَّف كمتغيّر عادي باسم المعرّف نفسه
print user01["name"];  // فهرسة عادية بالمفتاح، تماماً كأي قاموس آخر في اللغة
```

- `field(value);` و`field:(value);` متكافئتان تماماً (الثانية صيغة مُنمَّطة اختيارية).
- `field();` بلا وسيطة يجعل قيمة الحقل `nil`.
- `container.();` اختياري: يسجّل الكائن في سجل عام (objectRegistry) قابل للوصول
  من أي مكان لاحقاً بنفس المعرّف — وهو ما تحتاجه `view.print/object` عند تمرير
  نص (id) بدل الكائن مباشرة.

هذا الشكل الإضافي يتعايش بلا أي تعارض مع الشكل الأول `@Object=name`، ومع كائن
القاموس الحرفي `{ key: value }`.

## 3) الكائن كقاموس حرفي

```rin
let m = { name: "Rin", age: 2 };
print m["name"];
print keys(m);
```

هذا هو الشكل الأبسط، ويُفهرس بنفس طريقة الأشكال أعلاه — لأن الكائنات في Rin هي
في جوهرها [قواميس](./variables.md#4-القواميسالخرائط-maps) مع طبقة تسجيل/بناء إضافية.

## 4) الكائنات والشروط معاً

```rin
if (user01["online"]) {
    print user01["name"] + " متصل الآن";
}
```

## انظر أيضاً

- [`variables.md`](./variables.md) — القواميس كأساس للكائنات.
- [`containers.md`](./containers.md) — حاويات بيانات أكبر (أقسام، ترجمات، حفظ لملف).
- [`control-flow.md`](./control-flow.md) — فحص حقول كائن ضمن شرط.
- [`standard-library.md`](./standard-library.md) — دوال قواميس (`mapGet`, `mapMerge`, `pickKeys`, ...).
- [`language-reference.md`](./language-reference.md) — الخريطة الكاملة للغة.

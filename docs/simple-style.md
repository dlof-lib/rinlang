# Rin Simple Style

الهدف: جعل Rin سهلة القراءة عند بناء التطبيقات، مع إبقاء البنية القوية القديمة متوافقة.

## الأساس

```rin
@container=Home
mask = "home-root";
let title = "Rin";
.end/container
```

## استدعاء Container

بدلاً من استعمال عدة دوال منخفضة المستوى:

```rin
container("Home")
```

تُعيد وصفاً صغيراً للحاوية. ويمكن اختصار القراءة والكتابة:

```rin
container("Home", "title")
container("Home", "title", "Hello")
```

القيمة غير الموجودة أو الحاوية غير الموجودة تعيد `nil` بدلاً من الانهيار.

## استدعاء Mask

```rin
mask("home-root")
```

يعيد:

```text
{ mask: "home-root", name: "Home", kind: "container" }
```

أو `nil` إذا لم يوجد القناع.

## أسلوب التطبيق المقترح

```rin
@container=App
mask = "app";
let title = "Welcome";
.end/container

app = container("App");
root = mask("app");

print app;
print root;
print container("App", "title");
```

هذا الأسلوب لا يلغي `@container` أو `mask = ...` القديمة؛ هو طبقة كتابة مختصرة فوق Runtime الحالي.

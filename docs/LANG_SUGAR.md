# LANG_SUGAR — خمس صيغ جديدة في لغة Rin

كلها «سكّر نحوي» يُحوَّل في المحلّل (`rin_parser.cpp`) إلى بنى موجودة، فلا يتغيّر سلوك أي برنامج قديم.
الاختبار: `tests/print/lang_sugar.rin` (لا يجب أن يظهر `FAIL`).

## 1) `**` — القوة
```kotlin
print 2 ** 10;        // 1024
print 2 ** 3 ** 2;    // 512  (يمين الترابط)
print -2 ** 2;         // -4   (أقوى من السالب)
```
يُحوَّل إلى `pow(a, b)`. `*=` و`*` لا يتأثران.

## 2) قيم المعاملات الافتراضية
```kotlin
fun greet(name, greeting = "مرحباً", punct = "!") { return greeting + " " + name + punct; }
print greet("Rin");                 // مرحباً Rin!
print greet("Rin", "أهلاً");        // أهلاً Rin!
let f = fun(x, y = x * 2) { return x + y; };
```
القيمة الافتراضية تُقيَّم عند النداء وترى المعاملات السابقة، وتُطبَّق أيضاً حين يُمرَّر `nil` صراحةً. يعمل مع الدوال المسمّاة والمجهولة والطرق.

## 3) دوال السهم `=>`
```kotlin
let dbl = x => x * 2;
let add = (a, b) => a + b;
let now = () => "hi";
let big = (x) => { let y = x * 3; return y + 1; };
```
تُحوَّل إلى دالة مجهولة عادية (`fun(x) { return x * 2; }`).

## 4) الشرائح `a[i:j]`
```kotlin
let a = [10, 20, 30, 40, 50];
print a[1:3];     // [20, 30]
print a[:2];      // [10, 20]
print a[3:];      // [40, 50]
print a[-2:];     // [40, 50]  (فهرس سالب = من النهاية)
print "héllo"[1:3];  // "él"   (النص بالأحرف لا البايتات)
```
خارج المدى يُقصّ بصمت (كبايثون). الفهرس المفرد `a[i]` كما كان. الدالة الداخلية: `__sliceRange(x, lo, hi)` في `rin_extra_natives14.cpp`.

## 5) المعامل `in`
```kotlin
print 2 in [1, 2, 3];      // true
print "ell" in "hello";     // true
for x in items { ... }      // كما كان (لا تعارض)
```
يُحوَّل إلى `contains(b, a)`.

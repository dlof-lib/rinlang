# Rin Professional Libraries — Complete Edition

هذه الحزمة تحتوي 30 مكتبة Rin مستقلة في `lib/`. جميع الملفات مكتوبة بصيغة Rin الحالية، وتم فحصها نحويًا بواسطة `rin_check`.

| # | Library | Purpose | Main API |
|---|---|---|---|
| 1 | collections | معالجة المصفوفات | map, filter, reduce, chunk, unique, zip |
| 2 | numbers | أرقام وحسابات | clamp, gcd, lcm, prime, factorial |
| 3 | format | تنسيق النص | pad, left, right, center, repeat |
| 4 | csv | CSV | escape, row, rows, column, toObjects |
| 5 | query | استعلامات | first, last, count, any, all, distinct, page |
| 6 | validate_plus | التحقق | required, minLen, digits, email, integer |
| 7 | search | البحث | linear, all, firstBy, prefix, suffix, sorted |
| 8 | sortkit | الترتيب | asc, desc, unique, min, max, insert, top |
| 9 | stats_plus | الإحصاء | sum, mean, median, variance, stddev |
| 10 | pagination | الصفحات | count, slice, meta, next, prev |
| 11 | queue | FIFO | push, pop, peek, clear, pushMany |
| 12 | stack | LIFO | push, pop, peek, clear, pushMany |
| 13 | router | المسارات | match, segments, add, find |
| 14 | colors | الألوان | hex, rgb, rgba, gray, alpha |
| 15 | units | الوحدات | metric, mass, temperature, time, speed |
| 16 | geometry | هندسة 2D | point, rect, distance, overlap, dot, cross |
| 17 | matrix | المصفوفات | zeros, fill, identity, add, sub, scale, transpose, mul |
| 18 | graph | Graph | add, remove, neighbors, degree, edge, BFS |
| 19 | tree | Tree | node, add, find, depth, values, leaf |
| 20 | objectkit | Maps/Objects | get, set, remove, merge, pick, omit |
| 21 | config | الإعدادات | set, get, merge, defaults, require |
| 22 | cachekit | Cache | set, get, has, delete, clear, metadata |
| 23 | events | الأحداث | on, emit, count, clear |
| 24 | logger | التسجيل | info, warn, error, debug, trace, value |
| 25 | jsonkit | JSON | parse, stringify, encode, get, set |
| 26 | httpkit | HTTP | URL helpers, GET, POST, JSON GET |
| 27 | urlkit | URL | scheme, host, path, join, query/fragment |
| 28 | maskkit | Mask | normalize, equality, namespace, descriptor, search |
| 29 | layout | Layout | center, gap, grid, clamp, row |
| 30 | animation | Animation | easing, lerp, remap, keyframe, pingPong |

## قواعد الحزمة

- لا تعتمد المكتبات على ملفات خارج `lib/` إلا على دوال Rin القياسية المتاحة للمفسّر.
- كل مكتبة تستخدم بادئة أسماء خاصة بها لتقليل تصادم الأسماء.
- لا تضيف المكتبات أنواعًا جديدة إلى النواة؛ هي طبقة Rin قابلة لإعادة الاستخدام.
- تم تجنب الصيغ غير المدعومة في محلل Rin الحالي.

## مثال

```rin
@import "lib/numbers.og.rin";
@import "lib/collections.og.rin";

let values = [1, 2, 3, 4, 5];
let even = col_filter(values, fun(x) { return num_isEven(x); });
print even;
```

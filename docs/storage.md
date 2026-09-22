# storage.md — تخزين دائم

Rin تفرّق بوضوح بين **تخزين دائم فعليًا على القرص** (ملفات) و**ذاكرة تخزين
مؤقت داخل العملية** (Cache، تُفرَّغ عند انتهاء التشغيل). لتخزين هيكلي أكثر (مستندات
NoSQL، جداول SQL) انظر [`containers.md`](./containers.md) و[`objects.md`](./objects.md#document--سجل-داخل-حاوية-nosql).

## ملفات (تخزين دائم فعليًا)
```rin
writeFile("data.txt", "hello persistent world");
print fileExists("data.txt");  // true
print readFile("data.txt");    // hello persistent world

appendFile("data.txt", "\nsecond line");
print readFile("data.txt");    // hello persistent world\nsecond line

deleteFile("data.txt");
print fileExists("data.txt");  // false
```
| الدالة | الوصف |
|---|---|
| `writeFile(path, content)` | يكتب (يستبدل) محتوى ملف كاملًا. |
| `appendFile(path, content)` | يُلحِق محتوى بنهاية ملف موجود. |
| `readFile(path)` | يقرأ محتوى ملف كاملًا كنص. |
| `fileExists(path)` | `true`/`false`. |
| `deleteFile(path)` | يحذف الملف. |

## ذاكرة تخزين مؤقت (Cache — **غير دائمة**، داخل العملية فقط)
```rin
cacheSet("k1", "v1");        // بلا مهلة (ttl) => لا تنتهي صلاحيته أبدًا
print cacheGet("k1");         // v1
print cacheHas("k1");         // true
print cacheKeys();            // ["k1"]
cacheDelete("k1");
print cacheHas("k1");         // false
```
| الدالة | الوصف |
|---|---|
| `cacheSet(key, value)` | يخزِّن قيمة بلا مهلة انتهاء (تبقى حتى `cacheDelete`/`cacheClear` أو انتهاء العملية). |
| `cacheSet(key, value, ttlSeconds)` | نفسها، لكن تنتهي صلاحيتها تلقائيًا بعد `ttlSeconds` ثانية من الآن. |
| `cacheGet(key)` | القيمة، أو `nil` إن لم توجد أو انتهت صلاحيتها (تُحذَف تلقائيًا حينها). |
| `cacheHas(key)` | `true`/`false` (بنفس منطق انتهاء الصلاحية اللحظي). |
| `cacheDelete(key)` | يحذف مفتاحًا واحدًا. |
| `cacheClear()` | يفرّغ كل التخزين المؤقت. |
| `cacheKeys()` | مصفوفة كل المفاتيح غير منتهية الصلاحية حاليًا. |

**تنبيه دقيق تحقّقتُ منه فعليًا**: تمرير `ttlSeconds = 0` صراحةً لا يعني "بلا
انتهاء" — يعني "تنتهي صلاحيته فورًا" (يُخزَّن `expiresAt = الآن`، وأي وقت لاحق
يُعتبَر منتهيًا). "بلا انتهاء صلاحية" هو **حذف الوسيط الثالث بالكامل** من
`cacheSet`، لا تمرير `0` له.

**`cacheStore` نفسه ذاكرة فقط (`std::unordered_map` عضو في `Interpreter`)** —
لا يُكتَب على القرص، ويُفرَّغ تمامًا عند انتهاء عملية التشغيل. لأي شيء يحتاج
البقاء بين مرات تشغيل مختلفة، استخدم ملفات (`writeFile`/`readFile`) أو حاوية
[`@container.doc`](./objects.md#document--سجل-داخل-حاوية-nosql).

## انظر أيضًا
- [`objects.md`](./objects.md) — `document` داخل `@container.doc` لتخزين مستندات NoSQL.
- [`containers.md`](./containers.md) — أنواع الحاويات الأخرى المرتبطة بالبيانات (`table`, `data`, `sql`).
- [`errors.md`](./errors.md) — أكواد أخطاء الإدخال/الإخراج (E0036) إن فشلت عملية ملف.

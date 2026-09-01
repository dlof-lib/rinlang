# المكتبة القياسية (Standard Library)

> راجع أيضاً: [`functions.md`](./functions.md) لآلية تعريف الدوال نفسها،
> و[`control-flow.md`](./control-flow.md) لأن كل دالة أدناه مبنية من `if`/`while`.

كل دوال المكتبة القياسية دوال Rin عادية (`fun ...`) مكتوبة بنفس القواعد
الموضّحة في [`functions.md`](./functions.md) و[`control-flow.md`](./control-flow.md)،
ومُجمَّعة في وحدات `lib/*.og.rin` تُستورد بـ `@import`:

```rin
@import "lib/math.og.rin";
@import "lib/math.og.rin" as mathx;   // كحاوية باسم مستعار
```

## أهم الوحدات

| الوحدة | أمثلة دوال | تعتمد على |
|---|---|---|
| `lib/math.og.rin` | `factorial`, `gcd`, `lcm`, `isPrime`, `clamp`, `lerp` | [شروط](./control-flow.md) + [حلقات](./control-flow.md#3-الحلقات-التكرارية) + [تكرار ذاتي](./functions.md) |
| `lib/data.og.rin` | `range`, `unique`, `chunk`, `zip`, `first`, `last`, `mapGet`, `groupBy` | [مصفوفات/قواميس](./variables.md) |
| `lib/strings.og.rin` | `capitalize`, `startsWith`, `padLeft`, `slugify`, `isPalindrome` | [نصوص `text`](./variables.md#2-متغيّر-نصي-مُنمَّط-text) |
| `lib/opskit.og.rin` | `copyFile`, `mergeArrays`, `mergeMaps`, `bubble` (فرز) | [حلقات](./control-flow.md) + [كائنات](./objects.md) |
| `lib/functional.og.rin` | دوال تُستخدم كخطوات في [الأنابيب](./pipelines.md) `|>` | — |

## مثال: `isPrime` يجمع كل المفاهيم في دالة واحدة

```rin
fun isPrime(n) {
    if (n < 2) { return false; }
    if (n < 4) { return true; }
    if (n % 2 == 0) { return false; }
    let i = 3;
    while (i * i <= n) {
        if (n % i == 0) { return false; }
        i = i + 2;
    }
    return true;
}
```

هنا: [تعريف دالة](./functions.md) + عدة [شروط `if`](./control-flow.md#1-العامل-الأساسي-للشرط-if--else)
+ [متغيّر](./variables.md) `i` + [حلقة `while`](./control-flow.md#3-الحلقات-التكرارية)
+ [عوامل مقارنة](./control-flow.md#4-عوامل-المقارنة-والمنطق) — مثال مصغّر لكيف
تترابط جميع مفاهيم اللغة داخل دالة مكتبة قياسية واحدة.

## انظر أيضاً

- [`functions.md`](./functions.md) — تعريف الدوال.
- [`control-flow.md`](./control-flow.md) — الشروط والحلقات المستخدمة داخلها.
- [`variables.md`](./variables.md) — المصفوفات/القواميس التي تُعالجها هذه الدوال.
- [`pipelines.md`](./pipelines.md) — استخدام دوال المكتبة كخطوات أنبوب.
- [`language-reference.md`](./language-reference.md) — الخريطة الكاملة للغة.

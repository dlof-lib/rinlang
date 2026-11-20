# `from` مع `let` — أسماء محلية من ملف آخر

بدل كلمة `import`، تُعرِّف الأسماء بـ `let` ثم تقول من أين تأتي بـ `from`:
«ليكن `double` و`VERSION` من هذا الملف». تتحوّل داخلياً إلى نفس عقد الاستيراد الموجودة،
فلا يتغيّر أي سلوك قائم (المُفسِّر وVM وrinc).

| الصيغة | تعادل |
|---|---|
| `let a, b from "p";` | `import { a, b } from "p";` |
| `let a as x, b from "p";` | `import { a as x, b } from "p";` |
| `let { a, b as c } from "p";` | (الأقواس اختيارية، والفاصلة الأخيرة مسموحة) |
| `let * from "p";` | `@import "p";` — دمج كل المُصدَّر في النطاق الحالي |
| `let * as ns from "p";` | `import * as ns from "p";` |

```kotlin
// util.rin
export fun double(x) { return x * 2; }
export let VERSION = "1.0";

// main.rin
let double, VERSION as V from "./util.rin";
print double(21);   // 42
print V;            // 1.0

let * as util from "./util.rin";
print util.double(5); // 10
```

## قواعد
- المسار نص ثابت (بلا `${}`)، وتنتهي العبارة بـ `;`.
- الأسماء يجب أن تكون مُصدَّرة بـ `export`، وإلا خطأ واضح (كما في `import { }`).
- `from` كلمة **سياقية**: لا تُفسَّر كمفهوم إلا حين تأتي بعد قائمة أسماء في `let` ويتبعها نص قبل أي `=`.
  لذلك يبقى `let from = 5;` و`let x = 1;` و`let [a, b] = ...;` كما هي، وكذلك `each x in xs from 1` و`Set.from(...)`.
- `let a, b from;` أو نسيان `;` يعطي خطأ نحوياً واضحاً.

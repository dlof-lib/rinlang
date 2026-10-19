# مفهوم `number` في Rin

الرقم في Rin من نوع `double`. الدوال القديمة (`toNumber`، `round`، `sum`، …) تبقى كما هي، وهذه الدفعة تضيف **دوالّ عملية تسدّ فجوات الاستعمال اليومي**. كلها نقية (لا تعدّل مدخلاتها)، والأخطاء تُرمى كأخطاء عادية قابلة للالتقاط. الأسماء مسطّحة (`numberX`) لأن `number` اسم متغيّر شائع.

## لماذا؟ (فجوات الموجود)
| المشكلة | قبل | الآن |
|---|---|---|
| `toNumber("12abc")` | يمرّ بصمت كـ 12 | `number("12abc")` خطأ واضح |
| `"١٢٣"`، `"1,234"`، `"1.5e3"`، `"0xff"`، `"50%"` | غير مفهومة | `number` تفهمها |
| `round(1.005*100)/100` | 1 | `numberRound(1.005, 2)` = 1.01 |
| `0.1+0.2 == 0.3` | false | `numberApprox(0.1+0.2, 0.3)` = true |
| جمع عشر قيم 0.1 | 0.9999999999999999 | `numberSum` = 1 |

## التحويل والفحص
| الدالة | الوصف |
|---|---|
| `number(x[, opts\|default])` | تحويل **صارم**. يقبل رقماً/نصاً/منطقياً. النص: يقصّ الفراغات، يفهم الأرقام العربية والفارسية (`٫` `٬`)، `1,234` (بفحص المجموعات)، `1_000`، الأسّ، `0x/0b/0o`، `%`. وسيط ثانٍ غير قاموس = قيمة افتراضية عند الفشل |
| `numberCheck(x[, opts])` | الفحص نفسه بلا استثناء: `{ok:true,value,kind,exact}` أو `{ok:false,error}` |
| `numberKind(x)` | `integer / float / nan / infinity` أو `nil` |
| `numberParts(n)` | `{sign,int,frac,digits,scale,exponent,kind}` من التمثيل العشري الأقصر |

خيارات `number`: `default`، `int` (عدد صحيح فقط)، `min`/`max` (+`clamp`)، `base` (2..36)، `exact` (يرفض عدداً صحيحاً يتجاوز 2^53)، `special` (يقبل NaN/inf)، `decimal`/`group` (للصيغ الأوروبية)، `percent`.

```rin
print number("١٢٣٫٥");                                   // 123.5
print number("1.234,5", {decimal: ",", group: "."});     // 1234.5
print number("abc", 0);                                  // 0
print number("150", {max: 100, clamp: true});            // 100
print numberCheck("1,5").ok;                             // false (لا يُقرأ 15 بصمت)
```

## التقريب والتنسيق
| الدالة | الوصف |
|---|---|
| `numberRound(n[, places[, mode]])` | تقريب عشري صحيح يطابق ما تراه. `mode`: `half-up` (افتراضي) `half-even` `half-down` `up` `down` `floor` `ceil`. `places` سالب يقرّب للعشرات/المئات |
| `numberFormat(n[, opts])` | `decimals` أو `maxDecimals`/`minDecimals`، `group`، `decimal`، `groupSize`، `digits:"arabic"\|"persian"`، `sign`، `percent`، `compact` (`K M B T` أو `"ar"`)، `prefix`، `suffix`، `pad`/`padChar`/`align`، `mode` |
| `numberPercent(part, whole[, places])` | النسبة المئوية (افتراضياً خانتان) |
| `numberChange(from, to[, places])` | نسبة التغيّر المئوية |

```rin
print numberFormat(1234567.891, {decimals: 2});          // 1,234,567.89
print numberFormat(1500000, {compact: true});            // 1.5M
print numberFormat(3200000, {compact: "ar"});            // 3.2 مليون
print numberFormat(1234.5, {decimals: 2, digits: "arabic"}); // ١٬٢٣٤٫٥٠
print numberFormat(42, {pad: 6, padChar: "0"});          // 000042
```

## المقارنة والنطاقات
`numberApprox(a, b[, rel[, abs]])` · `numberMap(n, inMin, inMax, outMin, outMax[, clamp])` · `numberWrap(n, lo, hi)` (لفّ دوري: `numberWrap(370, 0, 360) == 10`).

## الجمع والإحصاء
`numberSum(list)` جمع مُعوَّض (Neumaier) · `numberStats(list[, {sample, skip}])` ← `{count,sum,min,max,range,mean,median,mode,variance,stdev}`. القوائم تقبل نصوصاً رقمية؛ `skip:true` يتجاهل غير الصالح.

## الأعداد الصحيحة
`numberGcd(...)` · `numberLcm(...)` · `numberIsPrime(n)` · `numberFactors(n[, grouped])` · `numberDivisors(n)` · `numberDigits(n[, base])` · `numberToBase(n, base[, {prefix,upper,pad,group}])` · `numberFromBase(text, base)`.

الأعداد الصحيحة تُقبل حتى 2^53؛ ما فوقه يُرمى بخطأ واضح بدل نتيجة خاطئة صامتة.

## ملاحظات
- `numberFormat` يعرض أرقام التمثيل العشري الأقصر للـ double (مثلاً `123456789012345678` يظهر `…680`)، وهو أقصى ما يحفظه النوع.
- `number("1.")` مقبولة (= 1)؛ `"1,5"` مرفوضة عمداً لأنها ملتبسة (فاصلة آلاف أم عشرية)، استعمل `{decimal:","}`.
- الاختبار: `tests/verification/number_concept.rin` / `.expected`.

# bigint — أرقام صحيحة ضخمة لـ Rin

مكتبة (مجلد `bigint.og.rin/`) بلا حدّ لحجم الرقم. الاستيراد: `@import "bigint";`

القيمة الضخمة خريطة `{s, d}` (الإشارة + خانات base 10^7). أنشئها بـ `big(x)` من رقم صحيح آمن أو من **سلسلة نصية** بأي طول
(الأرقام العادية فوق 2^53 تفقد دقتها قبل وصولها للمكتبة، فمرّرها كنص). كل الدوال تقبل أرقاماً عادية أو سلاسل أو قيماً ضخمة، ولا تغيّر مدخلاتها.

| الدالة | الوصف |
|---|---|
| `big(x)` `big_str(a)` `big_num(a)` `big_digits(a)` | إنشاء / نص عشري / تقريب لرقم عادي / عدد الخانات |
| `big_add` `big_sub` `big_mul` `big_neg` `big_abs` | الحساب الأساسي (الضرب Karatsuba للأرقام الكبيرة) |
| `big_divmod(a,b)` → `[q,r]` `big_div` | قسمة مقتطَعة نحو الصفر (Knuth D) |
| `big_mod(a,m)` | باقٍ إقليدي دائماً `0 <= r < |m|` |
| `big_cmp` `big_eq` `big_lt` `big_lte` `big_gt` `big_gte` `big_min` `big_max` `big_sign` `big_isZero` `big_isEven` `big_isOdd` | المقارنة والفحص |
| `big_pow(a, n)` `big_powmod(a, e, m)` `big_isqrt(a)` | أسّ، أسّ بالمقياس (أُسّ ضخم)، جذر صحيح |
| `big_gcd` `big_lcm` `big_fact(n)` `big_fib(n)` `big_sum([..])` `big_prod([..])` | نظرية الأعداد وعمليات المجاميع |

الأخطاء ترمي نصاً يبدأ بـ `bigint:` (قسمة على صفر، رقم غير صالح، ...) ويمكن التقاطها بـ `try/catch` (`e.message`).

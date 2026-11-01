# compare_ops.md — معاملات المقارنة `/=` و `=/=` و `=/` و `=::=`

تُحلَّل بنفس أولوية `==` و`!=` (نفس مستوى equality).

| المعامل | المعنى | مثال |
|---|---|---|
| `a /= b` | غير متساوٍ بالقيمة (مرادف `!=`) | `[1] /= [1]` ← false |
| `a =/= b` | **ليس نفس الكائن** (هوية مرجعية). للأنواع القيمية (nil/number/string/bool) يُقارَن بالقيمة | `a =/= b` ← false إن كان `b = a` |
| `a =/ b` | **نسخة**: متساويان بالقيمة لكنهما كائنان مختلفان | `[1,2] =/ [1,2]` ← true، `5 =/ 5` ← false |

| `a =::= b` | **نفس الشكل**: نفس البنية والأنواع، مع تجاهل القيم الورقية | `[1,"a"] =::= [9,"z"]` ← true، `[1,2] =::= [1,2,3]` ← false، `1 =::= "1"` ← false |

قواعد `=::=` (تطابق الشكل):
- الأعداد والنصوص والمنطقيات والدوال: يكفي تطابق **النوع**؛ و`nil =::= nil` فقط.
- المصفوفة: نفس الطول وكل عنصر بنفس شكل نظيره (بالترتيب).
- القاموس `map`: نفس المفاتيح (تُقارَن بالقيمة وبلا اعتبار للترتيب) وكل قيمة بنفس شكل نظيرتها.
- الكائن `instance`: نفس الصنف ونفس أسماء الحقول وأشكال قيمها.
- المجموعة `Set`: نفس العدد ولكل عنصر نظير بنفس الشكل (بلا ترتيب). الدالة المعرَّفة في Rin: نفس عدد المعاملات.
- محمي من البنى الدائرية (عمق أقصى 64).
- نافعة للتحقق من شكل بيانات (JSON/استجابة API) مقابل نموذج: `if (reply =::= {"id": 0, "tags": []})`.
- بنفس أولوية `==`؛ تعمل في المفسّر وفي RinVM (`Op::SameShape`) وتُلوَّن كمعامل مقارنة.

### أدوات الشكل: `shapeOf` و`shapeDiff`

```rin
print shapeOf({"b": 1, "a": ["x"]});          // {a:[string],b:number}   (مفاتيح مرتبة)

let diff = shapeDiff(reply, {"user": {"id": 0, "tags": [""]}, "ok": true});
// قائمة مخالفات فارغة = value =::= model. كل عنصر: path · reason · expected · actual
for (let d in diff) { print d["path"], d["reason"], d["expected"], d["actual"]; }
// $.user.tags[0]  type mismatch  string  number
// $.ok            missing key    bool    (absent)
```
الأسباب: `type mismatch` · `length N != M` · `missing key/field` · `unexpected key/field` · `class A != B` · `size` · `arity`. المسار بصيغة `$.user.tags[1]`.

تعمل الهوية على `array` و`map` و`Set` و`instance` و`function`.
ملاحظة لغوية: `=/` لا تلتقط `=//` فتبقى `x =// تعليق` تعليقاً كما كانت.
الاختبار: `tests/verification/compare_ops.rin|.expected` و`tests/verification/same_shape.rin|.expected`.

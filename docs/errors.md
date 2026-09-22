# errors.md — نظام الأخطاء: أكواد، `try`/`catch`/`throw`

## التقاط الأخطاء وقت التشغيل: `try` / `catch`
```rin
try {
    let arr = [1, 2, 3];
    print arr[100];
} catch (e) {
    print "caught: " + e;
}
print "after";
```
```
caught: {"message": "[E0021] index out of range: 100", "line": 3, "code": "E0021"}
after
```
خطأ حقيقي (مثل فهرسة خارج المدى) يُلتقَط كقاموس (map) بحقول `code`/`message`/`line`
— الوصول إليها عبر حقل عادي:
```rin
try {
    let x = [1, 2][10];
} catch (e) {
    print e.code;    // E0021
    print e.message; // [E0021] index out of range: 10
    print e.line;    // رقم السطر الذي حدث فيه الخطأ فعليًا
}
```

## `throw` — رمي قيمة مخصَّصة
```rin
try {
    throw "custom error";
} catch (err) {
    print err.value;   // custom error
    print err.message; // custom error (نفس القيمة، لسهولة التعامل الموحَّد مع أي خطأ)
    print err.line;
}
```
`throw <expr>;` يقبل أي قيمة (نص، رقم، قاموس...) — الحقل `value` يحمل القيمة
الأصلية كما رُميت، بينما `message` نسخة نصية موحَّدة تسهّل معاملتها بنفس طريقة أي
خطأ حقيقي مُلتقَط.

## جدول أكواد الأخطاء
القائمة الكاملة لكل كود `E00xx`/`W000x` ومعناه انتقلت إلى
[`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) (نفس الملف الذي يشير إليه `diagnostic.h`
مصدريًا كـ"جدول كامل بكل الأكواد") مع بنية النظام الداخلية (`DiagnosticEngine`،
تجميع عدّة أخطاء دفعة واحدة، الفرق بين `Diagnostic` القابل للاستمرار و`RinError`
غير القابل للاستمرار).

## رسائل الخطأ الغنيّة (Rich Diagnostics)
أخطاء وقت التحليل/الترجمة (وبعض أخطاء وقت التشغيل) تُعرَض بصيغة غنيّة بأربعة أقسام
— نفس الشكل الذي ظهر في كل مثال خطأ حقيقي رأيته أثناء التحقّق من هذه الوثائق:
```
error[E0006]: `typeof` is not a function
  --> <input>:15:1

 15 | print typeof(5);
    | ^

reason:
  no function (built-in or user-defined) named `typeof` is callable here

did you mean:
  `type`

help:
  did you mean `type()`?
```
القسمان `reason`/`help` (و`did you mean` حين متاح) اختياريان حسب نوع الخطأ —
`errRich(...)` في `rin_parser.cpp`/`rin_interpreter.cpp` هي نقطة الإنشاء الموحَّدة
لكل هذه الرسائل.

## انظر أيضًا
- [`ERROR_SYSTEM.md`](./ERROR_SYSTEM.md) — الجدول الكامل لكل الأكواد + بنية النظام الداخلية.
- [`MAKE_UNIT.md`](./MAKE_UNIT.md) — أمثلة حقيقية لأخطاء E0016 من سياسة القدرات.
- [`control-flow.md`](./control-flow.md) — بقية عبارات التحكّم التي قد تحيط بـ`try`.

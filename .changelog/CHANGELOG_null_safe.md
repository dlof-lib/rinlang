## السلامة من nil — `?.` · `??` · `??=`
- `a?.b` · `a?.[i]` · `a?.(x)`: سلسلة اختيارية؛ أول nil يُنهي السلسلة كلها بـ `nil` (بدل خطأ «cannot access property on nil»).
- `a ?? b`: قيمة افتراضية كسولة (نفس دلالة `a else b`)، متسلسلة من اليسار، أدنى من `or`.
- `a ??= b`: إسناد شرطي للمتغيّر و`arr[i]` و`obj.field`.
- رموز ملتصقة فقط فلا تعارض مع `c ? a : b`؛ كل كود قديم يعمل كما هو.
- حدّ معروف: المسار `--vm` لا يدعم هذه الصيغ بعد.
- الملفات: `rin_parser.cpp/.h` (`nullish()` + `call()` + `??=`) · `rin_interpreter.cpp` (`__optchain`) · `docs/null_safe.md` · `tests/print/null_safe.rin`.

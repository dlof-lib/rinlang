# CHANGES_SET — `Set` نوع أساسي

نوع جديد `Value::Type::SET` (انظر `docs/set.md`). الاسم العام الوحيد الجديد `Set`؛ الباقي تحت `Set.` فلا يتعارض مع `set()/setAdd()` في boat.

## الملفات
- جديد: `app/src/main/cpp/rin_set.cpp` (SetData بفهرس تجزئة، المعاملات، ~45 عملية، تسجيل natives)، `docs/set.md`، `tests/verification/set_{basic,ops,mutate}.rin|.expected`.
- `rin_interpreter.h`: `Type::SET`، `SetData`، `setops`، تصريحات `registerNativesSet/tryCallSetMethod`.
- `rin_interpreter.cpp`: valuesEqual/typeName/عرض/طباعة منسّقة/نسخ عميق/schema، المعاملات `+ - * < <= > >=`، `for..in`، النوع `Set`، الطرق النقطية (invokeCallee + MethodCall)، رسالة فهرسة، التسجيل والتضمين.
- `rin_oop.cpp` (`callMethodOn`)، `rin_json.h` (SET ← مصفوفة)، `docs/boat.md` (ملاحظة).

## تغيير سلوك
`class Set` صار `E0002` (الاسم مأخوذ).

## التحقق
`rintests` 54/54 · `wesscode_tests` 42/42 · verification 49/50 (الفرق الوحيد `input_forms` قديم) · boat `set()` يعمل. بُني على سطح المكتب (g++)؛ لم يُبنَ APK ولم تُعدَّل ملفات Kotlin (لا تلوين لـ `Set` بعد).

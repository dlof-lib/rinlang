# CHANGES_INT128 — إصلاح فشل بناء armeabi-v7a / x86 (`__int128 is not supported on this target`)

## السبب
`cli/linux/src/terminal/rin_terminal_xcmds6.h` كان يستخدم `unsigned __int128` في الأوامر
`isprime` و`comb` و`lcm` و`modpow` و`frombase`. هذا النوع غير مدعوم على الأهداف 32-بت
(armeabi-v7a و x86) فتوقف بناء الـ APK الموقّع عند `buildCMakeRelWithDebInfo[armeabi-v7a]`
بـ 18 خطأ (arm64-v8a كان ينجح لأنه 64-بت).

## الإصلاح
- `rin_terminal_xcmds6.h`: نوع `U128` محمول (حقلان `uint64_t`) مع `mul64x64` و`mul64` و`add64` و`divmod64`
  و`u128ToDec` و`mulmod64`. على الأهداف التي تعرّف `__SIZEOF_INT128__` يبقى `mulmod64` يستعمل النوع الأصلي
  (نفس السرعة)، وإلا يُستعمل المسار المحمول. يمكن فرض المسار المحمول بـ `-DRIN_NO_INT128` للاختبار.
- تصحيح جانبي في `comb`: الضرب الوسيط كان قد يطفح بصمت؛ صار يُكتشف ويُرجع "result too large".

## ثغرات `SET` (تحذيرات `-Wswitch: enumeration value 'SET' not handled`)
- `rin_extra_natives4.cpp` (`emitJson`): المجموعة كانت لا تُكتب إطلاقاً فيخرج JSON غير صالح؛ صارت مصفوفة.
- `rin_oop_natives.cpp` (`oop.hash`): محتوى المجموعة كان مُهملاً؛ صار تمثيلاً قانونياً مستقلاً عن الترتيب.
- `rin_interpreter.cpp` (`serializeValueLiteral`): المجموعة كانت تُحفظ `nil`؛ صارت `Set([...])`.

## التحقق
- اختبار عشوائي (400 ألف حالة) للدوال المحمولة مقابل `__int128`: 0 اختلاف (المساران).
- مقارنة `comb/lcm/modpow/frombase/isprime` مع Python الدقيق على المسارين: متطابقة.
- `rintests` 83/83 · `wesscode_tests` 42/42 · اختبار جديد `tests/verification/set_json_hash.rin`.

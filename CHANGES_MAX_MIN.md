# CHANGES_MAX_MIN
`max`/`min` صارتا: عدد غير محدود من الوسائط، أو مصفوفة واحدة، أرقاماً أو نصوصاً (مصفوفة فارغة => nil؛ خلط الأنواع = خطأ واضح). `min(a, b)` القديمة بلا تغيير.
جديد: `maxBy/minBy(arr, fn)` · `argmax/argmin(arr)` · `minmax(arr)`. الأول يفوز عند التساوي.
الملفات: `rin_interpreter.cpp` (`extremumOf` + min/max) · `rin_extra_natives2.cpp` (العائلة الجديدة) · `docs/max_min.md` · `tests/verification/max_min_family.rin|.expected`.
التحقق (g++ لينكس): الاختبار الجديد على `rin run` و`rin --vm` · rintests 83/83 · wesscode 42/42 · tests/loops. لم يُبنَ APK/الويب.

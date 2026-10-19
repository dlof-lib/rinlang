# CHANGES_LOOP_I — `i` ضمني في الحلقات
`repeat N { ... i ... }` و`each x in xs { ... i ... }` تكشفان `i` (رقم الدورة من 0) دون `as i`.
تُفعَّل فقط إن ذكر الجسم `i` ولم يعرّفه/يسنده (`let i`, `i = `, `i++`, `+=`, `as i`) ولم يكن `i` معرَّفاً قبل الحلقة في نطاق مرئي (let/for/as/each/معامل دالة) — فلا يتغيّر معنى `i` الخارجي في الشيفرة القديمة (نجحت tests/loops كلها).
`each` ينفَّذ بعدّاد مخفي `#ciN` وسطر `let i` في بداية كل دورة (يعمل مع كل الأنماط والقواميس). `repeat` يستعمل اسم الفهرس نفسه.
الملفات: `app/src/main/cpp/rin_parser.cpp` (`bodyWantsImplicitI` داخل `loopsV2Statement`) · `docs/loops.md` · `tests/loops/loops_implicit_i.rin|.expected`.
التحقق: tests/loops على `rin run` و`rin --vm` (g++ لينكس). rintests 83/83 · wesscode 42/42. لم يُشغَّل: verification، APK/الويب.

## y الضمني + توحيد خوارزمية الكشف
`each x in xs { ... y ... }`: `y` = قيمة x في الدورة السابقة (nil في الأولى) عبر متغيّر مخفي `#cyN` يُحدَّث في بداية كل دورة (آمن مع continue).
الكشف صار دالة واحدة معمَّمة بالاسم (`nameDeclaredBefore(nm)` / `bodyWantsImplicit(nm)`) تخدم `i` و`y`: مسح عكسي للنطاقات المرئية ثم مسح الجسم.
اختبار: `tests/loops/loops_implicit_y.rin`. التحقق: tests/loops كلها على المحرّكين · rintests · wesscode.

## تطوير y/i: كل أشكال each
`i` و`y` يعملان الآن في `for x in` بلا أقواس، `[each ... yield ...]`، `{each ... yield k : v}`، واختزالات `sum/product/count/avg/min/max/any/all/first/last`، إضافة إلى `each` (و`repeat` لـ i).
تغيير بنيوي: الكشف صار دوالّ في Parser (`nameDeclaredBefore` · `implicitWanted` · `buildEachImplicit` في rin_parser.cpp|.h) تعمل على مدى توكنات يُحلَّل بعد الترويسة، بدل مسح الجسم قبل تحليله.
حدّ موثّق: لا يُقرآن في شروط الترويسة (where/skip/take/until/while). اختبار: `tests/loops/loops_implicit_y2.rin`.
التحقق: tests/loops (26 تشغيلة على المحرّكين) · rintests 83/83 · wesscode 42/42.

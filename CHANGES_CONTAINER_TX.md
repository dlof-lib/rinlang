# CHANGES_CONTAINER_TX — container.transaction
دالة جديدة `container.transaction(name | [names], fn)`: تعديل ذرّي على حاوية أو عدة حاويات؛ لقطة عميقة قبل التنفيذ، وعند أي خطأ في `fn`
يُستعاد كل شيء حرفياً (تعديل/إضافة/حذف) ويُعاد رمي الخطأ. حاوية غائبة = خطأ قبل التنفيذ. تبقى الدوال (`fun`) داخل الحاوية دون مساس.
الملفات: `app/src/main/cpp/rin_extra_natives2.cpp` (بعد `container.restore`) · `docs/container_pro.md` · `tests/verification/container_transaction.rin|.expected`.
التحقق (g++ لينكس): الاختبار الجديد على `rin run` و`rin --vm` · container_extra/pro/advanced · rintests 83/83 · wesscode 42/42. لم يُبنَ APK/الويب.

## container.attempt + مثال عملي
`container.attempt(name | [names], fn)` -> `{ok, value, error}` بلا رمي (يغلّف transaction: نجاح => value، فشل => تراجع كامل + error{message,line[,value][,code]}).
مثال: `examples/container_transactions.rin` (تحويل مالي بتراجع يشمل دفتر القيود). اختبار موسَّع في container_transaction.rin. الملف: rin_extra_natives2.cpp.

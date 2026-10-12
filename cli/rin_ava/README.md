# RIN_AVA — أداة بناء Rin بواجهة g++

```
rin_ava prog.rin -o prog      # ينتج تنفيذياً مستقلاً
./prog arg1 arg2              # يعمل في أي مكان، الوسائط تصل للبرنامج
rin_ava -fsyntax-only prog.rin
rin_ava --help
```

- لا لغة جديدة: الفحص بـ `rin check` والتنفيذ بمفسّر `rin` نفسه.
- الناتج = نسخة من `rin` + البرنامج ملحقاً في النهاية (`cli/linux/src/rin_ava_payload.h`)؛ `rin` العادي لا يتأثر.
- الأخطاء بصيغة `file:line:col: error: message`، والخروج 0 نجاح / 1 فشل.
- البحث عن `rin`: `--runtime=` ← `RIN_AVA_RUNTIME` ← بجوار rin_ava ← `RIN_HOME/bin` ← PATH.
- الخلفية `-fnative` (`-S`/`-c`) تحتاج `rinc` ومترجم C.
- الملفات المستورَدة (`@import`) لا تُضمَّن؛ تُحلّ وقت التشغيل (يظهر تحذير).
- البناء: `cd cli/linux && ./build.sh` ← `build/rin` و`build/rin_ava`.
- الاختبار: `cli/rin_ava/tests/run_tests.sh build/rin_ava build/rin`.

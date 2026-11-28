## Container.TB — منشئ الأدوات والقوالب
- صيغة جديدة `@Container.TB( ... )` تجمع `@import <easy_key>` و`#token.<name>=` و`[[ #import ... ]]` (rin/C++) و`@make.<name>` مع بقاء `let/for/from` تعمل بداخلها.
- يجب أن تكون أول عبارة في الملف (خطأ `E0015` إن جاءت في الآخر أو بعد الأداة).
- `link to=X` يقبل الآن `easy_key` والمسار الكامل.
- التوثيق: `docs/CONTAINER_TB.md` · الاختبار: `tests/print/container_tb.rin`.

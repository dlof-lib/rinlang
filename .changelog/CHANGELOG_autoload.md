## Autoload — تحميل كسول للحاويات والمكتبات
- دوال جديدة: `Autoload.dir register unregister include load loaded isLoaded loaders clear`.
- الاسم الغائب (متغيّر/حاوية/فضاء أسماء، قراءة أو كتابة أو نداء) يستدعي المحمِّلات ثم يُكمَل التعبير؛ بلا محمِّلات لا كلفة (`autoloadActive_`).
- محمِّل مجلد يدعم `.rin` و`.rcl` مع `prefix`، وتنقية الأسماء، وتنفيذ كل ملف مرة واحدة، وحارس إعادة دخول.
- المحرّك في `rin_autoload.cpp` (مضمَّن من `rin_interpreter.cpp`)؛ نقاط الاعتراض في المفسّر الشجري وRinVM (`nsResolveExpr`، قراءة المتغيّر، `invokeCallee`).
- التوثيق: `docs/autoload.md` · الاختبار: `tests/print/autoload.rin`.

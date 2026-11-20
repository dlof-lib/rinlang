# piano — رسم وطباعة بيانات الـ container

- جزء جديد `lib/piano.og.rin/core/container.zpp` (مسجَّل في `disconom.json`، ومُضمَّن عبر `tools/embed_piano.py`).
- دوال: `pn_container_model` · `pn_container` · `pn_container_scene` · `pn_container_uri` · `pn_container_save` · `pn_container_text` · `pn_container_print`.
- نوع جديد في لغة الكتل: `$X.Container => "x y w 'name'"`.
- يدعم الحاويات العادية و`.data/.object` و`.table` (رأس + صفوف) و`.doc/.datalist` (مستندات)، والدوال والحاويات المتداخلة.
- أمثلة: `examples/piano/15_container.rin` · `16_indsin_container.rin`؛ اختبار: `tests/verification/piano_container.rin`.
- أرقام عدد الأجزاء في `tests/verification/piano_*.expected` صارت «13 أجزاء».
- لا تغيير في المفسّر ولا في مُصيِّرات C++/JS (`tools/piano_check.py` ينجح كاملاً).

## indsin
- مكوّن جاهز `<ContainerView name="..." tick={cell} />` في `rin_indsin_tags.h` (Define مدمج + مكتبة Rin `containerKind/containerBody` تُحقَن عند الاستعمال فقط، الأسطر محفوظة).
- مثال `examples/indsin_container_view.rin`؛ اختبار في `tests/indsin_tags/tags_test.cpp` (75/75)؛ توثيق في `docs/indsin_tags.md` و`indsin.md`.

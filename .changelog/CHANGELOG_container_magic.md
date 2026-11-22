## Container magic hooks — خُطّافات الأعضاء الغائبة (الخطوة 3)
- خُطّافات جديدة داخل `@container`: `on get(name)` · `on set(name, value)` · `on has(name)` · `on drop(name)` · `on call(name, args)` · `on invoke(args)`.
- تُستدعى فقط لعضو **غير معرَّف**؛ العضو الموجود يغلب، وحاوية بلا خُطّافات لا يتغيّر سلوكها (علم `magicActive_` = بلا كلفة).
- نقاط الاعتراض: `nsGet/nsSet/nsCall/nsTryCallDotted` (المفسّر الشجري وRinVM) و`invokeCallee` (نداء الحاوية نفسها) وغلاف حول `container.has/deleteField`.
- حارس إعادة الدخول لكل (حاوية، خُطّاف، اسم). المحرّك: `rin_container_magic.cpp`.
- التوثيق: `docs/container_magic.md` · الاختبار: `tests/print/container_magic.rin`.

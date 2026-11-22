## Services — حاوية الخدمات
- دوال جديدة: `Services.bind singleton instance alias fromContainer has get make call extend tag tagged tags forget reset flush names count info`.
- حقن تلقائي بأسماء معاملات المصنع، كشف الدوران (`E0030`) باقتراح «هل تقصد» عند غياب الخدمة، مُزخرِفات، وسوم، وخدمة = لقطة حيّة من `@container`.
- المحرّك في `rin_container_services.cpp` (مضمَّن من `rin_interpreter.cpp` فيدخل كل أهداف البناء بلا تعديل في CMake/Gradle)، وحالته لكل مفسّر (`servicesState_` في `rin_interpreter.h`).
- التوثيق: `docs/container_services.md` · الاختبار: `tests/print/container_services.rin`.

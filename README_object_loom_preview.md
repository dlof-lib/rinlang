# ربط المعاينة الحيّة لـ Object بمحرّك Loom

يضيف هذا التعديل معاينة حيّة *مرئية* (بطاقة داخل إطار الجهاز) لكائنات `.object("id")`، بدل
المعاينة النصّية في الكونسول فقط (`view.print/object`). التعديل إضافي بالكامل: لا يغيّر أي سلوك
قديم (تحقّقتُ بتشغيل كل ملفات `tools/test_loom_*.cpp` و`object_literal_selftest.cpp` الحالية،
وكلها ناجحة).

## الاستخدام

```rin
.object("user01")
    name("ABOO");
    age(19);
    container.();   // إلزامي — نفس شرط view.print/object تماماً
.end/object

@view.Object=user01_card
    source="user01";     // إلزامي: معرّف كائن مسجَّل عبر container.();
    // title="نص اختياري";  -> يستبدل العنوان الافتراضي "🧩 user01"
.end/view
```

هذا يُبنى فعلياً عبر خط أنابيب Fabric → Loom → Dye نفسه الذي تمر منه Text/Card/Column، وليس
مساراً منفصلاً — راجع `examples/object_loom_preview_demo.rin` لمثال كامل يربط:
`.object()` + `container.();` + `view.print/object` (الكونسول) + `@view.Object` (Loom).

## الملفات

- **جديد**: `app/src/main/cpp/loom/rin_loom_object.h` — سجل الكائنات الخاص بـ Loom
  (`objectLiteralRegistry`) + `applyObjectConveniences` التي تحوّل `source=` إلى Strand عنوان
  + سطر Text لكل حقل (بنفس أسلوب `applyBannerConveniences` الموجود أصلاً لـ Banner).
- **معدَّل**: `rin_loom_strand.h` — `StrandKind::OBJECT` جديد + وسم `@view.Object`.
- **معدَّل**: `rin_loom_layout.h` — قياس/تخطيط Object كصندوق مبطَّن (نفس قواعد Card).
- **معدَّل**: `rin_loom_paint.h` — لون افتراضي (نفس Card/Box).
- **معدَّل**: `rin_loom_pipeline.h` — استدعاء التسجيل والتوليف في كل من المسارين
  Cold/Cold-for-container (نفس قيود Banner الموثَّقة أصلاً: لا يُعاد التوليف في المسار الساخن
  Hot/Shuttle إلا عند "Run" جديد).
- **معدَّل**: `LoomFabricView.kt` — رسم Object كبطاقة (`Kind.OBJECT`، سطر واحد فقط).
- **معدَّل**: `RinContainerTags.kt` — أُضيف مقتطف `@view.Object` إلى كتالوج الإدراج السريع.
- **جديد**: `examples/object_loom_preview_demo.rin` — مثال تجريبي كامل.

لم يلزم أي تعديل في المحلّل النحوي (`rin_parser.cpp`) ولا في المفسّر (`rin_interpreter.cpp`):
صياغة `@view.<Kind>=name key=expr; .end/view` عامة أصلاً لأي Kind، فـ`@view.Object=...` تعمل
بلا أي تغيير نحوي.

## التحقق

تم تصريف وتشغيل الأداة الأصلية فعلياً (وليس تخميناً):

```bash
g++ -std=c++17 -O0 -o rin_loom_run tools/rin_loom_run.cpp \
    app/src/main/cpp/loom/rin_loom_c_api.cpp app/src/main/cpp/rin_lexer.cpp \
    app/src/main/cpp/rin_parser.cpp app/src/main/cpp/rin_interpreter.cpp \
    app/src/main/cpp/rin_c_api.cpp app/src/main/cpp/rin_http.cpp \
    app/src/main/cpp/diagnostics/diagnostic.cpp app/src/main/cpp/diagnostics/diagnostic_engine.cpp \
    app/src/main/cpp/diagnostics/diagnostic_renderer.cpp app/src/main/cpp/diagnostics/source_manager.cpp \
    -I app/src/main/cpp -lz

./rin_loom_run render examples/object_loom_preview_demo.rin 390
```

يُنتج JSON فيه `"kind":"Object"` مع بطاقتين محتوييتين على العنوان وأسطر الحقول، وبطاقة ثالثة
تُظهر تحذيراً واضحاً لمعرّف غير مسجَّل بدل صندوق فارغ.

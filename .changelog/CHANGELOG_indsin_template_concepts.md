## indsin — مفاهيم القوالب (Match · Unless · فتحات مسمّاة · Prop · ...attrs)
- `<Match on={x}>` مع `<Case is=/in=/when=>` و`<Default>`: فرع واحد فقط (يُترجَم إلى سلسلة If/Else).
- `<Unless cond={c}>`: عكس `<If>` ويقبل `<Else>` بعده.
- `<Slot name="x">احتياطي</Slot>` و`<Fill name="x">`: فتحات مسمّاة داخل `<Define>` مع محتوى احتياطي (الفتحة الافتراضية كما كانت).
- `<Prop name= required type= enum= default= />`: خصائص مطلوبة/منوَّعة؛ أخطاء بأرقام أسطر واقتراح «did you mean».
- `...attrs` داخل جسم `<Define>`: تمرير السمات غير المعلنة إلى عنصر القالب (الأحدث يغلب).
- أخطاء «no property» تقترح الأقرب. المُلوِّن يتعرّف على الوسوم الجديدة كتوجيهات.
- الناتج مطابق حرفياً للأصل على كل ملفات `.rin` في المستودع (730 ملفاً) ⇒ لا تغيير لأي كود قديم.
- الملفات: `indsin/rin_indsin_tags.h` · `editor/rin_indsin_highlight.h` · `docs/indsin_template_concepts.md` · `tests/indsin_tags/`.

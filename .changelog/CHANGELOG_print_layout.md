## print layout — مسافات · تنسيق · تعليم · تخطيط
- دفعة 2: `tree bars diff tabs dedent maxlines ends ruler focus hlcase`.
- ميزات إضافية: `table leader inalign numbers rule shadow beside justify cutat`.
- سمات `print` جديدة: `indent gap pad before after trim squeeze visible` · `fmt case wrap cut` · `mark hl hlwrap` · `box title cols colgap page place`، ودوال `layoutText` `fmtNumber` `showSpaces`.
- تفكيك في المحلل إلى `__pfx(...)` (بلا تعديل في المفسّر/RinVM)؛ المحرّك في `rin_print_layout.cpp` (مضمَّن من `rin_interpreter.cpp`). التوثيق: `docs/print-layout.md`، الاختبار: `tests/print/print_layout.rin`.

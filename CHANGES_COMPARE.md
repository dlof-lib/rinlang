# CHANGES_COMPARE — معاملات `/=` `=/=` `=/`
- `rin_common.h`: توكنان جديدان `NOT_SAME` (`=/=`) و`COPY_OF` (`=/`).
- `rin_lexer.cpp`: `/=` يُحوَّل إلى `BANG_EQUAL`؛ `=/=` و`=/`؛ مع حماية `=//`.
- `rin_parser.cpp`: يُضافان إلى `equality()`.
- `rin_interpreter.cpp`: `sameReference()` + تقييم المعاملين + أسماء التوكن.
- لم يُعدَّل `rinc.cpp` (المترجم) ولا ملفات Kotlin (لا تلوين بعد).
- التحقق: rintests 54/54 · wesscode_tests 42/42 · verification 50/51 (الفرق الوحيد `input_forms` القديم).

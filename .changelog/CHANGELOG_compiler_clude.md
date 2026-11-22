# compiler_clude

## 1.1.0
- مكتبة `lib/compiler_clude.og.rin/` (مجلد: disconom + أجزاء .zpp) مضمَّنة في المفسّر: `@import "compiler_clude";` (`tools/embed_compiler_clude.py`).
- ملف الكلمات المحجوزة `ccludelike.txt`: `$token[]` · `$token[ ; : / ]` · `container.inlr [ img , text ]@` · `print` · `@import` · `@inlr.compiler_clude.main[];` · `@option/@ext/@title/@version` · `$infix/$infixr/$prefix`.
- محلّل مفرداتي + محلّل Pratt ومقيِّم + أدوات (JSON، تصدير، فرق، فحص، اقتراح، تلوين، TextMate) + قوالب + مولّد لغات/قوالب/مكتبات.
- **ربط بالمحلّل المفرداتي** (`rin_lexer.cpp: expandCludeDirective`): السطر `@inlr.compiler_clude.main[ "spec.txt" ];` في أي ملف `.rin` يصير `@import "compiler_clude"; let cc_spec = cc_attach(...)`، فيُحمَّل المخطط ويُنبَّه إلى أسماء let/fun التي تصطدم بكلمة محجوزة.
- اختبارات: `tests/compiler_clude/run.sh` · دليل: `docs/compiler_clude.md`.
- مثال لغة كاملة `examples/mini_lang/` (let/print/if/while/fun/return + عوامل بأولويات + دوال مدمجة + أخطاء بأسطر) واختبارها `tests/compiler_clude/mini_lang.rin`.
- **RinStudio:** `cc_export_studio` / `cc_new_language` يولّدان مشروع لغة مخصصة كاملاً (manifest + syntax.rinsyntax.json + Lexer/Parser/Interpreter/CodeGen/run) من ccludelike.txt؛ `cc_ast_to_rin`؛ مثال `examples/customlang/mini` (يحلّ محلّ examples/mini_lang).

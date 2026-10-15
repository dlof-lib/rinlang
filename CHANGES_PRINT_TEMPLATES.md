# CHANGES_PRINT_TEMPLATES — قوالب الطباعة (`@tmpl ... print/style/text`)
إضافة additive لنظام `@tmpl`: أربعة أنواع جديدة — سطر `print`، خطي `print { ... }` (عدة أسطر)، تنسيقي `style` (سمات print قابلة للتركيب)، نصي `text` (تعبير).
لا تغيير في أي صيغة قائمة، ولا في المفسّر أو RinVM: التفكيك يتم في مرحلة التوكنات داخل المحلل.
## خطوط print (إضافة)
سمات جديدة على `print`: `bold italic underline strike dim reverse blink color bg` (docs/print-fonts.md). تُجمَّع في المحلل بنداء واحد لـ `__fxcode(...)` يعطي أكواد SGR،
ويلفّ المفسّر/RinVM المحتوى بها بعد `upper/width/align` (فلا تنكسر المحاذاة). تُفعَّل فقط على طرفية أو `FORCE_COLOR=1` وتُعطَّل بـ `NO_COLOR`، فلا تتلوّث الملفات ولا كونسول أندرويد.
تعمل داخل قوالب `style` و`print` أيضاً. الملفات الإضافية: `rin_ast.h` (حقل `PrintStmt::fx`) · `rin_interpreter.cpp` · `rin_bcvm_compiler.cpp` · `rin_bcvm_exec.cpp` · `tests/print/print_fonts.*`.
غير منفَّذ: عرض ANSI داخل كونسول أندرويد (RinJobAdapter) — يحتاج تحويل SGR إلى Spans.

## الملفات
`app/src/main/cpp/rin_parser.cpp` (حقل `kind`/`lines` في `LoopTmpl`، فرع في `tmplParseDef` وآخر في `tmplExpandUse`) · `docs/print-templates.md` · `tests/print/print_templates.rin|.expected`.
## التحقق (g++ لينكس، -O0)
tests/print 2/2 و tests/loops 11/11 على `rin run` و`rin --vm` (26/26) · rintests على المحرّكين · rintests 83/83 · wesscode 42/42. لم يُشغَّل: tests/verification وبناء APK/الويب.

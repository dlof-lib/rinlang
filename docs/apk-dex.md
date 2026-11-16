# تصدير APK مع تحويل Rin → DEX

عند تفعيل خيار «تحويل الكود إلى DEX (حقيقي)» في شاشة تصدير APK، يُصرَّف ملف الدخول إلى **Dalvik bytecode (DEX 035)** على الجهاز نفسه ويُضاف للحزمة كـ `classesN.dex` (N = أكبر رقم موجود في الحزمة المضيفة + 1). تشغّله `ExportedRunActivity` بالانعكاس: `rin.export.Program.run()` — بلا `rinengine.so` ولا مفسِّر.

## البنية
- `apk/dex/DexBuilder.kt` — كاتب ملف DEX كامل (header، ids مرتبة، class_def، code_item، map_list، MUTF-8، Adler-32، SHA-1).
- `apk/dex/CodeBuilder.kt` — مُجمِّع Dalvik: سجلات رمزية، ترميز بأقصر عرض، تمديد القفزات، invoke/range، const-wide…
- `apk/dex/DexFrontEnd.kt` — lexer + parser + AST للمجموعة المدعومة.
- `apk/dex/DexCodegen.kt` — توليد الكود: `let` علوي → حقول static، الدوال → `f_<name>`، الثوابت الرقمية مجمَّعة في `<clinit>`.
- `apk/dex/RinDexCompiler.kt` — الواجهة: `compile(source, file)` ⇒ `Ok(dex)` أو `Unsupported(reason, line)`.
- `dexrt/RinRt.kt` — وقت التشغيل (يعيش في dex التطبيق المضيف، تحفظه قاعدة ProGuard `com.dlof.rinlang.**`): العمليات الديناميكية، `%g` مطابق لـ C++، وكل الدوال المدمجة (~40).

## المجموعة المدعومة
`let/print/if/else/when/while/rinopen/for/fun/return/break/continue`، المصفوفات والقواميس والفهرسة، `|>`, `and/or`، والدوال المدمجة: len, upper, lower, trim, substr, split, join, indexOf, replace, contains, charAt, toString, str, toNumber, toBool, isBool, abs, sqrt, pow, floor, ceil, round, min, max, push, pop, sort, keys, values, has, remove, sum, mean, maxOf, minOf, chr, ord.

غير المدعوم (كلاسات، import، حاويات، lambdas، دوال متداخلة، قوالب نصية، `1e3`، `return` علوي، دوال كقيم…) ⇒ `Unsupported`:
- `DexMode.AUTO` (الافتراضي): تحذير في السجل ثم تراجع لوضع المفسِّر.
- `DexMode.REQUIRED`: يفشل التصدير.
- `DexMode.OFF`: وضع المفسِّر دائماً.

## بيان التشغيل
`rin_export_manifest.json` يحوي `mode: "dex"` + `dex_class` + `dex_method`. في وضع DEX لا تُحقَن ملفات `.rin` في assets.

## التحقق
`tools/dexc-test/` — اختبار تفاضلي مقابل ثنائي `rin` الحقيقي (61 حالة)، مع DexVm مستقل (مدقّق بنية + مدقّق أنواع سجلات شبيه بـ ART + مفسِّر):
`bash build; RIN=/path/to/rin ./run-diff.sh` ⇒ `pass=61 fail=0`.
> لم يُشغَّل على جهاز/محاكي أندرويد فعلي في بيئة التطوير.

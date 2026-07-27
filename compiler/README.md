# rinc — مترجم Rin الأصلي (Native Compiler)

هذا المجلد يضيف **مترجماً** (compiler) حقيقياً إلى مشروع RinLang، ليكمّل محرّك
المفسّر (interpreter) الموجود في `app/src/main/cpp/` (rin_lexer.cpp / rin_parser.cpp /
rin_interpreter.cpp، المُصدَّر عبر JNI في `RinEngine.kt`).

نسخة مطابقة من `rinc.cpp` موجودة أيضاً داخل `app/src/main/cpp/rinc.cpp` بجانب بقية
ملفات اللغة مباشرة، للرجوع إليها بسهولة عند العمل على المحرّك — لكنها أداة مستقلة
تُبنى على جهاز التطوير (host) بـ g++/clang عادي، وليست جزءاً من `libRinengine.so`
التي يبنيها `CMakeLists.txt` عبر NDK لأندرويد (لم تُضَف لقائمة `add_library` هناك
عمداً: لا معنى لتشغيلها داخل تطبيق أندرويد نفسه لأنها تستدعي مترجم C عبر `system()`
على جهاز التطوير).

`rinc.cpp` ملف واحد قائم بذاته: يقرأ نفس نحو اللغة (نفس lexer/parser منطقياً)
ثم يولّد كود C ويستدعي مترجم C موجود على النظام (`cc`/`gcc`/`clang`) لإنتاج
**ملف تنفيذي أصلي (native executable)** حقيقي — وهو ما لم يكن متوفراً في المشروع
سابقاً (المفسّر ينفّذ الكود مباشرة، ولا ينتج ملفات تنفيذية).

## البناء والاستخدام

```bash
# 1) ابنِ المترجم نفسه مرة واحدة (لا يحتاج أي ملف آخر من المشروع)
g++ -O2 -std=c++17 -o rinc compiler/rinc.cpp

# 2) حوّل أي برنامج .rin إلى تنفيذي أصلي
./rinc path/to/program.rin          # ينتج ./program
./rinc path/to/program.rin -o app   # يسمي التنفيذي app
./rinc path/to/program.rin --emit-c-only   # فقط يولّد ملف .c دون بنائه
```

## النطاق

يدعم اللغة الإجرائية الأساسية في Rin بالكامل (let / print / if-else / while /
fun-return / المصفوفات والقواميس والفهرسة / كل المعاملات + `|>` / مكتبة قياسية
رياضيات ونصوص ومصفوفات وملفات).

لا يدعم "لغة الحاويات/البيانات" الخاصة بمحرّك المفسّر وتطبيق أندرويد
(`@container`, `Containers.Group`, `Volume`, `Section`, `Translations`, `link`,
`tying`, `merge`, `installation`, `save`, `table`/`row`/`style`, `document`
(NoSQL), `route`, `@import`) — هذه الميزات مرتبطة عضوياً بتخزين التطبيق ولا
معنى مستقل لها في تنفيذي native، ويصدر `rinc` خطأ واضحاً عند مصادفتها بدل
توليد سلوك خاطئ صامت، مع توجيه لاستخدام المفسّر الأصلي لتلك الملفات.

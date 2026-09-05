# illust.rin — ملفات معدَّلة/جديدة فقط (patch)

كل الملفات هنا بنفس المسار النسبي الذي تحتله داخل مستودع rinlang. انسخها فوق نسختك
مباشرة (نفس البنية)، ثم ابنِ المشروع بـ Android Studio كالمعتاد.

## اختبار حقيقي تم إجراؤه
لغة illust نفسها (Lexer/Parser/Interpreter/CodeGen) شُغِّلت فعلياً على مفسّر Rin حقيقي
مبنيّ من مصدر app/src/main/cpp عبر:

```
g++ -std=c++17 -O1 -I app/src/main/cpp cli/linux/src/main.cpp \
  app/src/main/cpp/rin_lexer.cpp app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp app/src/main/cpp/rin_make.cpp \
  app/src/main/cpp/loader_ui/library_loader_ui.cpp app/src/main/cpp/rin_http.cpp \
  app/src/main/cpp/diagnostics/*.cpp app/src/main/cpp/clc/*.cpp \
  cli/linux/src/pkg/*.cpp -lz -o rin

./rin run examples/customlang/illust/run.rin
```

كل من مسار Interpreter (SVG مباشر) ومسار CodeGen (كود Rin مستقل يُولَّد ثم يُشغَّل لوحده)
أعطيا نفس مخرجات SVG بالضبط.

**ملاحظة مهمة:** الجزء الخاص بتعديلات Android (Kotlin/XML) لم يُبنَ فعلياً في هذا الـsandbox
لعدم توفر Android SDK/Gradle هنا — فقط g++ للمفسّر النصي. راجع كل ملف Kotlin/XML أدناه
وابنِ المشروع بنفسك للتأكد من نجاح الـcompile قبل الاعتماد عليه.

## الملفات الجديدة
- `app/src/main/java/com/dlof/rinlang/store/languages/BundledIllustLanguage.kt`
  يضمّن كل ملفات illust (Lexer/Parser/Interpreter/CodeGen/run/syntax/README/hello.illust)
  كسلاسل Kotlin خام — هي نفسها المُختبرة أعلاه حرفياً.
- `app/src/main/res/drawable/ic_illust_file.xml`
  أيقونة vector (لوحة ألوان+فرشاة، برتقالي #E67E22) لملفات .illust ولشارة نوع المشروع.
- `examples/customlang/illust/` (مجلد كامل)
  نفس اللغة كملفات .rin عادية قابلة للقراءة/التعديل مباشرة على القرص، بنفس بنية
  `examples/customlang/calc/` الموجودة أصلاً — مرجع مستقل عن BundledIllustLanguage.kt.

## الملفات المعدَّلة
- `CustomLanguageProjectScaffolder.kt`
  أُضيفت دالة `installBundledIllust(context, projectDir)`: تكتب ملفات illust الجاهزة في
  مشروع حديث الإنشاء وتسجّلها في CustomLanguageRegistry فوراً.
- `Project.kt`
  أُضيف `ProjectType.ILLUST("illust")` لعدّاد أنواع المشاريع.
- `ProjectManager.kt`
  فرع احتياطي لـ`ProjectType.ILLUST` داخل `mainRinTemplateFor` (يُستبدَل فوراً بملفات
  illust الحقيقية عبر installBundledIllust، لكن لازم لإبقاء `when` شاملاً في Kotlin).
- `ProjectsActivity.kt`
  - شريحة `chipTypeIllust` أُضيفت لخريطة `chips` في حوار "مشروع جديد".
  - عند الإنشاء واختيار Illust: يُستدعى `installBundledIllust` بعد `ProjectManager.createProject`.
  - فرعان جديدان لـ`ProjectType.ILLUST` في `typeLabel` و`typeIconAndColor` (لعرض شارة
    المشروع في قائمة المشاريع بنفس أيقونة/لون Illust).
- `FileIconResolver.kt`
  فرع جديد: امتداد `illust` -> `R.drawable.ic_illust_file` محلياً بلا أي طلب شبكة،
  بنفس أسلوب معالجة `.rin` تماماً.
- `dialog_create_project.xml`
  الشبكة أصبحت 3 صفوف بدل 2 (`rowCount="3"`)، وأُضيفت شريحة خامسة بعرض الصفّين معاً
  (`chipTypeIllust`, `layout_columnSpan="2"`) تحت الشرائح الأربع الأصلية.
- `strings_projects_files.xml` (القيم الافتراضية + ar + en + es)
  `project_type_illust` و `project_type_illust_desc` أُضيفا لكل لغات الواجهة الأربع.
- `colors.xml`
  `project_type_illust_color = #E67E22` (نفس برتقالي الأيقونة، لشارة نوع المشروع).

## ما لم يُلمَس عمداً
لا تغيير على أي ملف آخر (لا ProjectType آخر، لا AndroidManifest، لا Gradle) — فقط
الإضافات الدنيا اللازمة لجعل "Illust" نوع مشروع فعلي قابل للاختيار والتلوين والأيقونة.

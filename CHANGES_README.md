# الملفات المعدَّلة/المُنشَأة — rinlang

انسخ كل ملف إلى نفس المسار المقابل له داخل مشروعك (المسارات كما هي، بنفس البنية).

## 1) مفهوم اللغة الجديد: container.link.id (معدَّل — مبني ومُختبر فعلياً)
- app/src/main/cpp/rin_ast.h            — LinkStmt + LinkIdDeclStmt
- app/src/main/cpp/rin_parser.cpp       — تحليل link.id="" و link id=""
- app/src/main/cpp/rin_interpreter.h    — سجل linkIdToContainer + getLinkIds()
- app/src/main/cpp/rin_interpreter.cpp  — تنفيذ التسجيل/الربط بالمعرّف

الصياغة:
    link.id="core.user";   // بداخل حاوية: يسجّلها تحت معرّف عام
    link id="core.user";   // من أي مكان: يربط بها عبر المعرّف بدل اسمها
    link to=name;          // كما كانت، بلا أي تغيير (متوافقة للخلف بالكامل)

## 2) Snippets لـ VS (جديدة)
- src/Snippets/Rin/link.id.snippet
- src/Snippets/Rin/link.byid.snippet

## 3) فهرسة الربط عبر rin/html/js/cpp (جديد)
- tools/rin_link_index.py
  الاستخدام: python3 tools/rin_link_index.py <مجلد المشروع> [--json out.json]
  الاتفاقية بكل نوع ملف:
    .rin   -> link.id="X"; / link id="X";
    .html  -> data-rin-link-id="X"
    .js/.ts/.jsx/.tsx -> // @rin-link-id: X
    .cpp/.h -> // @rin-link-id: X

## 4) أيقونات حقيقية للملفات المرفوعة عبر API (جديد/معدَّل)
- app/src/main/java/com/dlof/rinlang/FileIconResolver.kt  (جديد)
  يعرض مصغّرة حقيقية للصور/الفيديو محلياً، ويجلب الشعار الرسمي (Python/JS/HTML/C++...)
  والخطوط عبر Iconify API (https://api.iconify.design) مع تخزين مؤقت على القرص.
- app/src/main/java/com/dlof/rinlang/FilesActivity.kt      (معدَّل)
  فلاتر رفع أوسع (image/*, video/*, font/*) + ربط FileIconResolver بكل عنصر في القائمة.
- app/src/main/res/layout/item_file.xml                    (معدَّل)
  أضيف id (imgFileIcon) لعنصر الأيقونة ليمكن للـ Adapter تحديثه ديناميكياً.

جميع تعديلات C++ بُنيت واختُبرت فعلياً بـ g++ (lexer+parser+interpreter) وكل اختبارات
tools/test_*.cpp القديمة ما زالت تنجح دون أي كسر. لم يتوفر NDK/Android SDK في هذه البيئة
لبناء الـ APK كاملاً أو تجربة Kotlin على جهاز حقيقي، فراجع كود Kotlin بصرياً/عبر Android
Studio قبل الدمج النهائي.

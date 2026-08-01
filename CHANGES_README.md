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

## 5) ميزة لغوية جديدة: break / continue داخل حلقات while (جديد — مبني ومُختبر فعلياً)
عبارتان جديدتان للتحكّم المبكّر بتدفّق حلقة while (كانت اللغة تفتقدهما كلياً؛ لا توجد وسيلة
سابقة للخروج المبكر من حلقة أو تخطّي بقية جسمها):

    break;      // يخرج فوراً من أقرب حلقة while محيطة
    continue;    // يتخطّى باقي جسم الحلقة الحالية ويعود مباشرة لفحص الشرط

مثال:
    let total = 0;
    let i = 0;
    while (i < 20) {
        i = i + 1;
        if (i % 2 == 0) { continue; }  // يتجاهل الأزواج
        if (i > 15) { break; }          // يتوقف بعد 15
        total = total + i;
    }
    print total; // 64

قواعد التحقق: استخدام break/continue خارج أي حلقة while هو خطأ وقت التحليل (رسالة واضحة:
"'break' used outside of a loop")، وليس فشلاً صامتاً وقت التشغيل. كذلك إن عُرِّفت دالة نصياً
داخل جسم حلقة while، فإن break/continue بداخل تلك الدالة تُعتبر خارج أي حلقة (ما لم تحتوِ
الدالة نفسها على حلقة while خاصة بها) — تماماً كما تتصرف return أصلاً مع حدود الدوال.

### محرّك المفسّر (interpreter)
- app/src/main/cpp/rin_common.h       — توكنان جديدان: BREAK / CONTINUE
- app/src/main/cpp/rin_lexer.cpp      — تسجيل الكلمتين المحجوزتين break/continue
- app/src/main/cpp/rin_ast.h          — عقدتا BreakStmt / ContinueStmt
- app/src/main/cpp/rin_parser.h/.cpp  — تحليل break;/continue; + عدّاد loopDepth للتحقق
                                          وقت التحليل (يُصفَّر داخل جسم كل دالة)
- app/src/main/cpp/rin_interpreter.h  — BreakSignal / ContinueSignal (نفس أسلوب ReturnSignal)
- app/src/main/cpp/rin_interpreter.cpp — تنفيذ العقدتين برمي الإشارة المناسبة + التقاطها
                                          داخل حلقة تنفيذ while

### المترجم الأصلي (rinc — ينتج تنفيذي C أصلي)
- compiler/rinc.cpp             — نفس الإضافات (Tok/AST/Parser) + توليد break;/continue;
                                    مباشرة في كود C المولَّد (تطابق دلالي كامل مع C نفسها)
- app/src/main/cpp/rinc.cpp      — نسخة مطابقة مُحدَّثة بنفس التغييرات (كما ينص README الخاص
                                    بمجلد compiler/)

### تلوين الصياغة (syntax highlighting) في كل الأدوات
- src/Classification/RinTokenizer.cs                         — إضافة break|continue لتعبير
                                                                  الكلمات المحجوزة النمطي (VS)
- syntaxes/rin.tmLanguage.json                                — نفس الإضافة (TextMate grammar)
- app/src/main/java/com/dlof/rinlang/RinSyntaxHighlighter.kt  — نفس الإضافة (محرِّر تطبيق أندرويد)

### Snippets لـ VS (جديدة)
- src/Snippets/Rin/break.snippet
- src/Snippets/Rin/continue.snippet

### توثيق
- README.md — مثال break/continue أُضيف داخل دليل اللغة السريع

تم بناء المفسّر (lexer+parser+interpreter) والمترجم rinc كليهما فعلياً بـ g++ -std=c++17
واختبارهما بعدة برامج (تخطٍّ/خروج مبكر متداخلَين مع if، وخطأ break خارج حلقة، ودالة مُعرَّفة
داخل حلقة تحتوي break خاصة بها) وأعطيا نفس النتائج تماماً (المفسّر عبر rin_c_api + المترجم
عبر تنفيذي C فعلي تم بناؤه وتشغيله). لم يتوفر NDK/Android SDK في هذه البيئة لبناء الـ APK
كاملاً، فراجع كود Kotlin (RinSyntaxHighlighter.kt) بصرياً قبل الدمج النهائي.


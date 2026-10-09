# طرفية Rin الكاملة لسطر الأوامر

- جديد: `cli/linux/src/terminal/rin_terminal.{h,cpp}` (طرفية كاملة، انظر `docs/terminal.md`) تحل محل REPL السطر الواحد.
- معدَّل: `cli/linux/src/main.cpp` (`rin` بلا وسائط + `rin terminal|repl|shell`)، `cli/linux/CMakeLists.txt`، `cli/linux/README.md`.
- إصلاح في المحرك `app/src/main/cpp/rin_interpreter.cpp`: بداية `run()` تصفّر `lastDiagnostic_/lastErrorMessage_/lastErrorLine_`؛ كانت حالة الفشل تبقى عالقة فيُبلَّغ كل تشغيل لاحق على نفس الكائن أنه فاشل. (ومخرجات REPL القديم كانت ستتكرر لأن `run()` تعيد كل المخرجات المتراكمة؛ الطرفية الجديدة تستخدم stream sink.)
- جديد (هذه الدفعة): **مشاريع** (`:new/:init/:project/:test/:tree/:run`) و**indsin داخل الطرفية** (`:indsin` و`rin indsin`): عرض بنصف-كتل + فأرة + تحميل حيّ، عبر `rin_indsin_session_*` نفسها. CMake يضيف `rin_c_api.cpp` و`indsin/rin_indsin_c_api.cpp` لهدف `rin`.
- مبنيّ فوق أرشيف rinlang-main__1_ (فيه دعم الوسائط في indsin؛ طلباته تُعرض كرسالة في الطرفية).
- اختبارات: `tests/terminal/` (دفعي + pty).
- التحقق هنا: ترجمة g++ 13 بلا تحذيرات (-Wall -Wextra)، `rintests` 83/83، `wesscode_tests` 42/42، `tests/terminal` كلها ناجحة. لم يُجرَّب بناء CMake (غير مثبّت هنا؛ رُجمت الملفات يدوياً بنفس القائمة) ولا macOS/Windows/Android.

# ربط طرفية Linux (rin terminal) بمحرّر RinStudio (أندرويد)

**C++ (مشترك):** `cli/linux/src/terminal/rin_terminal.{h,cpp}`
- `Session` صار قابلاً للتضمين: المخرجات عبر `outSink_` بدل stdout، و`input()` عبر callback، وإدخال سطر بسطر (`feedLine`) بنفس منطق `--batch`.
  مجلد العملية يُحفظ ويُستعاد حول كل سطر، فـ`:cd` يغيّر مجلد الجلسة فقط.
- الصنف العام `rin::terminal::EmbeddedTerminal` (في الهيدر). في التضمين تُحجب `:sh :edit :pkg :indsin` برسالة. `:pkg` يُستبعد بالعلم `RIN_TERMINAL_EMBED`.
- سلوك `rin terminal` على لينكس لم يتغيّر (كل اختبارات `tests/terminal/run.sh` ناجحة).

**أندرويد:**
- `app/src/main/cpp/terminal/rin_terminal_jni.cpp` (جديد): جسر JNI (create/feed/cancelPending/continuing/variables/destroy).
- `app/src/main/cpp/CMakeLists.txt`: يضيف `rin_terminal.cpp` وجسر JNI إلى `rinengine` مع `RIN_TERMINAL_EMBED`.
- `RinNativeTerminal.kt` (جديد): خيط عامل واحد، ويكتب في `RinTerminal` عبر نفس مسار المخرجات والإدخال المستعمل في التشغيل العادي.
- `RinTerminalView.kt`: أوامر `:` (وكل شيفرة بعد `native on`) تذهب للجلسة الأصلية؛ `reset` و`session`/`vars` و`^C` و`help` محدَّثة؛ أمر جديد `native on|off`.
- `res/values{,-ar,-en,-es}/strings_terminal_native.xml` (جديد): نصوص المساعدة والرسائل.

**ما تحقّقتُ منه فعلاً:** ترجمة `rin_terminal.cpp` على لينكس بالنسختين (عادية و`-DRIN_TERMINAL_EMBED`) بلا تحذيرات؛ تشغيل `EmbeddedTerminal` على لينكس
(حالة دائمة، أسطر متعددة، `:vars :type :load :cd :pwd :ocr :img :reset`، `input()`، حجب `:sh/:edit/:pkg/:indsin`، نص عربي وإيموجي، مجلد العملية لا يتغيّر)؛
فحص نحوي لملف JNI مقابل `jni.h` وهمي.
**ما لم أستطع التحقق منه (لا NDK ولا kotlinc ولا جهاز هنا):** بناء أندرويد (CMake/NDK)، ترجمة ملفي Kotlin، وتجربة الواجهة على جهاز. راجع الفحوص أدناه عند أول بناء.

## الدمج مع النسخة الأحدث (rinlang-main_25)
- دُمجت تعديلات التضمين فوق `rin_terminal.cpp` الأحدث مع الإبقاء على إطار `:ocr` الملوّن (Unicode) وأمر `ocr` في `RinTerminalView.kt` وعرض ألوان ANSI (`RinAnsi`).
- في وضع التضمين تُفعَّل ألوان ANSI (256 لوناً) وUnicode دائماً (لا tty ولا locale على أندرويد)، فتظهر ألوان `:ocr` و`:img` الأصلية في الطرفية.
- `ocr` (أمر RinStudio القديم) يبقى يعمل في الـ REPL العادي؛ `:ocr` يعمل في الجلسة الأصلية.
- الاختبار: ترجمة `rin_terminal.cpp` بالنسختين (عادية و`-DRIN_TERMINAL_EMBED`) بلا تحذيرات. لم يُبنَ أندرويد هنا (لا NDK/kotlinc).

## تطوير ستايل الطرفية (RinTerminalView)
- شريط حالة علوي: نقطة ملوّنة + READY / RUNNING / INPUT / MULTI-LINE، وعلى اليمين الوضع (REPL أو NATIVE) وعدد الأسطر وحجم الخط.
- موجّه `❯` (بدل `$`) مع خلفية خفيفة لكل أمر تفصله عن مخرجاته وسطر فارغ بين الأوامر؛ النسخ/المشاركة تبقى بصيغة `$ ` النصية.
- أسطر النجاح `✔` والخطأ `✖` والنظام بخط مائل خافت؛ شاشة ترحيب بعنوان `Rin ▸ terminal`.
- أزرار الأدوات بتأثير لمس (Ripple) وتباعد أسطر أوضح. لم يُترجَم Kotlin هنا (لا kotlinc) — راجعه عند أول بناء.
- أزرار سريعة في شريط الحالة: ↓ للأسفل · ⎘ نسخ · ⇪ مشاركة · ⌫ مسح؛ ولا يتكرر رمز ✓/✗ في أسطر الحالة.

## محاكاة terminal حقيقي
- موجّه shell كامل `rin@studio:<المجلد>$` (مستخدم أخضر، مجلد أزرق) في سطر الإدخال وفي صدى كل أمر، وموجّه المتابعة `>` كـ PS2 في bash؛ أُزيلت خلفية الأوامر.
- سطر تنفيذ حيّ فوق الإدخال أثناء أي تشغيل (REPL أو الجلسة الأصلية): مؤشر Braille دوّار + الزمن المنقضي `⠹ Running… 2.4s`، يظهر ويختفي تلقائياً.

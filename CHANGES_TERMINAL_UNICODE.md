# Unicode ملوّن في RinStudio + `ocr` في طرفيته

- **أمر `ocr` في طرفية RinStudio** (`RinTerminalView.kt`): `ocr <صورة> [--lang ara+eng] [--psm N] [--enhance] [--min N]`.
  لا منطق OCR جديد ولا دوال محرّك: يبني استدعاء `make.image.text` الموجودة ويشغّله بنفس مسار أي أمر في الطرفية
  (`onExecuteRequested`)، فيُطبع النص بـ Unicode كما هو، وتحته سطر الثقة · الكلمات · اللغة. المعاملات تُفحص (أرقام، أسماء لغات)
  والمسار يُهرَّب (`\` `"` `$` والأسطر) قبل دخوله نص الشيفرة. ملف الصورة نسبي لمجلد المشروع كما في `make.image.text`.
- **ألوان Unicode**: `RinAnsi.kt` (جديد، منطق خالص) يحوّل أكواد ANSI SGR في المخرجات — مثل ناتج `colorAnsi(...)` الموجودة في المحرّك —
  إلى مقاطع ملوّنة في الطرفية: 16 لوناً، 256، truecolor، خلفية، غامق/مائل/تسطير. كانت تظهر قبلاً أكواداً خاماً.
  سطر الثقة يُلوَّن بـ `colorAnsi` (أخضر ≥80، أصفر ≥50، أحمر دون ذلك).
- **أمان**: كل تسلسل غير SGR (تحريك المؤشر، مسح الشاشة، OSC، أحرف C0/C1) يُسقَط، فلا يعبث نص صورة أو ملف بالشاشة.
  النسخ (`copy`/`share`) و`grep` وتصنيف السطر ورابط `[Error line N]` تعمل على النص بلا أكواد.
- **CLI (Linux)**: `:ocr` (الإطار من ocr-on-main22) صار سطر الثقة فيه بألوان `Style` الموجودة (أخضر/أصفر/أحمر)؛ لا دوال جديدة.
- ملفات: جديد `RinAnsi.kt`، `RinAnsiTest.kt`؛ معدَّل `RinTerminal.kt` (3 مواضع تستعمل `RinAnsi.strip`)، `RinTerminalView.kt`،
  `strings_terminal.xml` ×6 (`terminal_ocr_usage` + سطر في `terminal_help`)، `cli/linux/src/terminal/rin_terminal.cpp`، `docs/terminal.md`.
- الأساس: rinlang-main__21 + أرشيف main_22 (`:img`/`:ocr`) + ocr-on-main22 (إطار `:ocr`).
- **تحقق**: `RinAnsi.kt` رُجم فعلاً بـ kotlinc 2.0 وجُرّبت حالاته (truecolor، 256، OSC/CSI عدائية، تسلسلات مقطوعة)؛ بناء `rin` بـ CMake ناجح،
  `tests/terminal/run.sh` كلها PASS، و`:ocr` جُرّب في pty حقيقي (ألوان ظاهرة)، ومقتطف Rin الذي يولّده `ocr` شُغّل على المحرّك المبني
  (نجاح، مسار بمسافة، ملف مفقود، معاملات خاطئة ← null).
- **لم يُتحقق**: ترجمة `RinTerminalView.kt` و`RinAnsiTest.kt` (JUnit) داخل Gradle/Android (لا SDK هنا)، والعربية في OCR (حزمة ara غير مثبّتة)،
  وOCR على جهاز أندرويد (يحتاج tessdata). شغّل `./gradlew testDebugUnitTest` وجرّب `ocr` على جهاز.
- حدّ: حالة اللون لا تمتد بين الأسطر (كل سطر يبدأ بلا تنسيق).

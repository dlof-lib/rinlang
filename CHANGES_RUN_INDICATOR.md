# مؤشر تحميل لكل نوع أمر + مرتبط بتشغيل كود Rin

يُطبَّق فوق: `rinlang-main_23` + `rinlang-progress-delta` + `rinlang-main_24` (الملفات هنا تتضمّن تعديلات الدلتا السابقة نفسها).

## الجديد
**1) `RunIndicator` — مؤشر حيّ مرتبط بالتشغيل الفعلي (`loader_ui`)**
`⠋ Running  main.rin · importing rin.ui · Parsing · 12 lines  1.4s   Ctrl-C to stop`
- لا يظهر قبل 0.35s: `1 + 2` أو أي تشغيل سريع لا يرمش أبداً.
- يُرسم فقط والبرنامج صامت (≥150ms منذ آخر مخرجات) ويختفي فور أي سطر مخرجات ثم يعود بعدّاد الأسطر.
- الاستيراد الجاري حقيقي: يتغذّى من أحداث `LoadSession` (المكتبة + المرحلة: Reading/Parsing/Initializing...).
- `input()`: يتوقف المؤشر أثناء السؤال ويعود بعده.
- خيط الرسم لا يستقبل إشارات، فـ Ctrl-C يصل للخيط الرئيسي ويقاطع التنفيذ/القراءة كالمعتاد.
- خارج tty (pipe/ملف/CI)، أو `RIN_PROGRESS=plain`، أو الوضع المضمَّن (أندرويد)، أو stdin ليس tty: لا يُفعَّل أي شيء
  ويبقى الناتج والسلوك القديم حرفياً. `NO_COLOR` يُبقي الحركة بلا ألوان؛ بلا UTF-8 => ASCII.

**2) سبعة أنواع أوامر جديدة في `Task` (الفعل/اللون/الشكل لكل نوع)**
| النوع | الفعل | الأوامر | المؤشر |
|---|---|---|---|
| `Run` | Running | `rin run`، `:run`، `:edit` | RunIndicator (spinner slide) |
| `Load` | Loading | `:load` `:source` `:reload` | RunIndicator (dots) |
| `Eval` | Evaluating | أي شيفرة تُكتب في الطرفية | RunIndicator (arrow) |
| `Project` | Scaffolding | `:new` `:init` | RunIndicator (bounce) |
| `Image` | Rendering | `:img` `:ocr` | RunIndicator (dots) |
| `Files` | Processing | `$ cp mv rm find grep du diff sort ...` | RunIndicator (line) |
| `Hash` | Hashing | `$ sha256 md5 crc32 base64 hexdump ...` | RunIndicator (bounce) |
| `Bench` | Benchmarking | `:time -n N` | شريط حيّ `4/6 runs 67% · ETA · runs/s` |
| `Test` (موجود) | Testing | `:test` | شريط حيّ (جديد في الطرفية) + ناتج الفاشل بعد الشريط |

`taskForCommand(name)` يربط اسم أي أمر بنوعه (غير المعروف => Generic "Working").

## الربط
- `cli/linux/src/main.cpp` (`rin run`): المؤشر على stderr + stream sink + sink للاستيراد + مزوّد إدخال. مع `--import-progress` يُعطَّل (له شريطه).
  عند تفعيله يصل الناتج تدريجياً (بعد كل عبارة علوية) بدل دفعة واحدة؛ والتشغيل غير الحي كما كان.
- `cli/linux/src/terminal/rin_terminal.cpp`: `print()` المركزية تمرّ عبر المؤشر الحيّ؛ `runParsed` يبني المؤشر بنوع التشغيل
  (`runTask_/runTarget_`)، و`handleCommand` يغلّف الأوامر البطيئة بمؤشر نوعها. `:test` و`:time -n` يرسمان شريطاً حياً فقط على tty.
- `cli/linux/CMakeLists.txt`: `Threads::Threads` لهدف `rinvm` أيضاً (يترجم `library_loader_ui.cpp`).
- المعرض: `loader_ui_demo --run` (محاكاة تشغيل) و`--tasks` (الأنواع الجديدة أيضاً).

## الملفات
معدَّل: `app/src/main/cpp/loader_ui/{library_loader_ui.h,library_loader_ui.cpp,demo_main.cpp}`،
`cli/linux/src/main.cpp`، `cli/linux/src/terminal/rin_terminal.cpp`، `cli/linux/CMakeLists.txt`.

## التحقق
- بناء `rin` كاملاً (CMake, g++ 13, C++23) بلا تحذيرات في الملفات المعدّلة.
- `tests/terminal/run.sh`: كل الاختبارات نجحت (الدفعية + pty: Ctrl-C، input()، tab، indsin...).
- اختبار على pty حقيقية (محاكي طرفية): تأخير الظهور، اختفاؤه عند المخرجات، اسم المكتبة أثناء الاستيراد، pause عند `input()`،
  Ctrl-C مع المؤشر ظاهراً، `:load/:run/$ sleep/:test/:time -n/:new`، وخروج نظيف بلا ESC عند pipe و`RIN_PROGRESS=plain`.
- **لم يُجرَّب:** ويندوز/macOS/أندرويد. الوضع المضمَّن (محرّر RinStudio) لا يفعّل المؤشر عمداً (لا tty/تحكم بالمؤشر هناك)؛
  ربطه بمحرّر أندرويد يحتاج جانب Kotlin.

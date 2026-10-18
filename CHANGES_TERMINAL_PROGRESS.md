# مؤشر التحميل في الطرفية (Progress) — احترافي وحقيقي

## ما تغيّر
`app/src/main/cpp/loader_ui/library_loader_ui.{h,cpp}` أُعيدت كتابته، ويشمل:

**1) `ConsoleBarSink` (شريط `@import`، يُفعَّل بـ `rin run file.rin --import-progress`)**
- طرفية حقيقية: سطر حيّ متحرّك: spinner بريلي + شريط ناعم بدقة نصف خانة (`━╸─`) بلون Rin البنفسجي + نسبة + مرحلة + مدة.
- شجرة التبعيات بأي عمق (`├─ └─ │`) تتحدّث في مكانها، ثم يُستبدل الكل عند الانتهاء بملخص نظيف:
  `✔ rin.ui  loaded · 724ms` مع الشجرة النهائية. الفشل: `✘ name  failed: …` بالأحمر. المكرَّر: `↺ already imported`.
- pipe / ملف / CI: وضع نظيف تلقائياً (سطر لكل حدث، بلا ESC ولا `\r`).
- كان الحد السابق «شجرة بعمق واحد فقط» — أُزيل.

**2) `ProgressBar` الجديد (عام لأي عملية متعددة الخطوات)**
`Installing  ━━━━━╸─────  2/5  40%  http 1.2.0 · verifying  1.4s`
خيط خفيف يحرّك الـ spinner والمدة كل 80ms حتى أثناء التنزيل الحاجب. `note()` تطبع سطراً دائماً فوق الشريط.

**3) ربطه بـ RinPM (`rin pkg install/add/update`)**
`installResolvedPackage(..., onPhase)` يبلّغ المراحل الفعلية بالترتيب الحقيقي: `cached → downloading → verifying → unpacking`.
العدّاد = حزم أُنجزت فعلاً؛ لا أنيميشن وهمي ولا نسب مخترعة.

## الكشف التلقائي (`detectTermCaps`)
| الشرط | النتيجة |
|---|---|
| ليس TTY | وضع نظيف |
| `NO_COLOR` أو `TERM=dumb` | بلا ألوان (`CLICOLOR_FORCE=1` يفرضها) |
| لغة صريحة غير UTF-8 أو `TERM=linux` | رموز ASCII (`[=====>---]`, `+`, `x`) |
| `RIN_PROGRESS=plain` / `fancy` | تعطيل / فرض الأنيميشن |
| العرض | `ioctl(TIOCGWINSZ)` ← `COLUMNS` ← 80؛ لا يلتف السطر أبداً (اقتطاع بـ `…`) |
ويندوز: تفعيل VT تلقائياً + UTF-8.

## الملفات
معدَّل: `loader_ui/library_loader_ui.{h,cpp}`، `loader_ui/demo_main.cpp`، `cli/linux/src/pkg/{cli_pkg.cpp,package_ops.h,package_ops.cpp}`، `cli/{linux,macos,windows}/CMakeLists.txt` (Threads).
واجهة `ConsoleBarSink(out, barWidth, useCr)` القديمة ما زالت تعمل (`useCr=false` ⇒ وضع نظيف).

## التحقق
- بناء `rin` على لينكس كاملاً؛ تشغيل `rin run … --import-progress` و`rin pkg add/install` على سجل محلي حقيقي.
- صوّرتُ الإخراج من pty حقيقية (256 لون، عرض 100 و42 عموداً) عبر محاكي طرفية: الشريط، الشجرة المتحدّثة، الملخص النهائي، الاقتطاع.
- ترجمة الملف بـ `-D__EMSCRIPTEN__` (بلا خيوط) سليمة.
- **لم يُجرَّب:** ويندوز/macOS/أندرويد (لا بيئة بناء هنا) — كود ويندوز (VT/UTF-8) مكتوب بحذر لكنه غير مترجَم.

## أنواع المؤشرات (إضافة)
- **5 أنواع شريط** (`BarStyle`): `line` ━━╸── (افتراضي) · `block` ████▌░░ (دقة 1/8) · `dots` ●●●○○ · `segments` ▰▰▰▱▱ · `shade` ▓▓░░.
- **9 أنواع spinner** (`SpinnerStyle`): `braille` · `dots` · `line` · `arc` · `bounce` · `pulse` · `arrow` · `grow` · `slide` (شريط منزلق ▰▰▰▱▱▱).
- **`Spinner` جديد** لعمل غير محدَّد المدة (بلا نسبة): `succeed/fail/warn/stop`. مستخدم الآن في «Resolving dependencies» في `rin pkg install`.
- الاختيار: `RIN_PROGRESS_BAR=block RIN_PROGRESS_SPINNER=arc` أو برمجياً عبر `IndicatorStyle` (ctor لـ `ProgressBar`/`Spinner` و`ConsoleBarSink::Options::style`). بلا Unicode: ASCII تلقائياً.
- المعرض: `loader_ui_demo --gallery`.

## مؤشر لكل نوع أمر (Task) — مع الوقت والكمية
`profileFor(Task)` يعطي لكل أمر هويّة: فعل، وحدة عدّ، لون، شكل شريط وspinner، وهل يُعرض ETA والسرعة.

| الأمر | الفعل | الوحدة | اللون | شريط + spinner | يعرض |
|---|---|---|---|---|---|
| `rin pkg install/add/update` | Installing | pkgs | بنفسجي | line + braille | المنقضي، ETA |
| `rin test` | Testing | tests | أخضر | shade + arc | المنقضي، ETA، tests/s |
| `rin build` | Building | steps | برتقالي | block + grow | ملخّص: الزمن + حجم الناتج |
| `rin analyze` / check | Checking | files | سماوي | segments + dots | spinner + النتيجة |
| `rin fmt` | Formatting | files | وردي | dots + line | الزمن + عدد الأسطر المتغيّرة (على stderr) |
| `rin clean` | Cleaning | items | أزرق | segments + bounce | الزمن + عدد الملفات المحذوفة وحجمها |
| `rin doctor` | Diagnosing | checks | أخضر فاتح | dots + pulse | 5 فحوص بنتيجة كل منها |
| `rin run --time` | Running | — | بنفسجي | slide | زمن التشغيل الفعلي (stderr) |

- **ETA** = (المنقضي ÷ المنجَز) × المتبقي، من معدّل الإنجاز الفعلي؛ لا يظهر قبل إنجاز أول عنصر.
- **الكميات حقيقية:** ملفات/بايتات `rin clean` تُعدّ فعلاً قبل الحذف، وحجم ناتج `rin build` يُقاس بـ `stat`.
- `rin test`: يكتب الشريط على دفق مستقل (fd=1) لأن الاختبار يعيد توجيه `std::cout` مؤقتاً؛ ناتج الاختبارات الفاشلة يُطبع بعد الشريط.
- `rin build`: الأمر الفرعي (rinc) يكتب في نفس الطرفية فلا يصلح شريط حي؛ لذا ملخّص نهائي بهويّة Build.
- واجهات جديدة: `Task`، `TaskProfile`، `profileFor`، `ProgressBar(out, profile, total, fd)`، `Spinner(out, profile, msg, fd)`، `report()`، `formatBytes()`.
- المعرض: `loader_ui_demo --tasks`.

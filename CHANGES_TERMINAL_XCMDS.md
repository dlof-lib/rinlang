# 50 أمراً جديداً للطرفية (لينكس + RinStudio)

الأسلوب: `$ أمر … ` (أو `:`)، `*` توسيع ملفات، `~/` المنزل، `A:B` / `A…B` مدى. النصوص كلها إنجليزية.

**الأوامر (50):** cat head tail wc grep find mkdir rmdir rm cp mv touch stat du realpath basename dirname echo sort uniq tac rev nl upper lower cut cmp sha256 base64 hexdump crc32 env export unset date whoami hostname uname which uptime free nproc df seq sleep alias unalias pushd popd dirs

## الملفات
- **جديد** `cli/linux/src/terminal/rin_terminal_xcmds.h` — header-only (POSIX + std)، لا CMake جديد ولا مكتبات؛ لا يشغّل أي عملية خارجية فيعمل على أندرويد كما هو.
- **معدَّل** `cli/linux/src/terminal/rin_terminal.cpp` — `$` كبادئة أمر (تشغيل/متابعة الأسطر/الإكمال/rc)، دمج الأوامر في `commands()` و`:help`، `alias`، وفشل الأمر يُحتسب في رمز خروج `--batch`.
- **معدَّل** `app/src/main/java/com/dlof/rinlang/RinTerminalView.kt` — أسطر `$ cmd` تُوجَّه للجلسة الأصلية كأسطر `:`.
- **معدَّل** `res/values{,-ar,-en,-es}/strings_terminal_native.xml` — سطر مساعدة (إنجليزي).
- **معدَّل** `docs/terminal.md`، `tests/terminal/run.sh`؛ **جديد** `tests/terminal/xcmds_session.{in,expected}`.

## ما تحقّقتُ منه فعلاً
- ترجمة `rin_terminal.cpp` بلا تحذيرات (`-Wall -Wextra`) بالنسختين العادية و`-DRIN_TERMINAL_EMBED`.
- بناء `rin` كاملاً وتشغيل جميع الأوامر الـ 50 في جلسة `rin terminal` حقيقية، وفي `EmbeddedTerminal` (مسار RinStudio): glob، `~/`، المدى، alias، pushd/popd دون تغيير مجلد العملية، حجب `:sh`، أخطاء ورموز خروج، وحماية `rm /`. اختبار `xcmds_session` ناجح.
- **لم أستطع:** بناء أندرويد (لا NDK/kotlinc) — تغيير Kotlin صغير (دالة + Regex)، فراجعه عند أول بناء. اختبار pty التفاعلي لإكمال Tab بـ `$` لم يُجرَّب.

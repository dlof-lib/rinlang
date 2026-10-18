# 100 أمر جديد للطرفية (لينكس + RinStudio)

الأسلوب: `$ أمر … ` (أو `:`)، `*` توسيع ملفات، `~/` المنزل، `A:B` / `A…B` مدى. النصوص كلها إنجليزية.

**الدفعة 1 (50):** cat head tail wc grep find mkdir rmdir rm cp mv touch stat du realpath basename dirname echo sort uniq tac rev nl upper lower cut cmp sha256 base64 hexdump crc32 env export unset date whoami hostname uname which uptime free nproc df seq sleep alias unalias pushd popd dirs

**الدفعة 2 (50):** ll ln chmod readlink file mktemp exists tee append split diff comm paste tr fold replace trim shuf wordfreq count rot13 md5 sha1 urlencode urldecode hex unhex ascii conv calc factor primes fib gcd rand uuid timestamp cal datediff id ps cpuinfo mounts printenv yes true false term path numfmt

## الملفات
- **جديد** `cli/linux/src/terminal/rin_terminal_xcmds2.h` — الدفعة الثانية (يُضمَّن تلقائياً من `rin_terminal_xcmds.h`، فلا تغيير في CMake).
- **جديد** `cli/linux/src/terminal/rin_terminal_xcmds.h` — الدفعة 1 + header-only (POSIX + std)، لا CMake جديد ولا مكتبات؛ لا يشغّل أي عملية خارجية فيعمل على أندرويد كما هو.
- **معدَّل** `cli/linux/src/terminal/rin_terminal.cpp` — `$` كبادئة أمر (تشغيل/متابعة الأسطر/الإكمال/rc)، دمج الأوامر في `commands()` و`:help`، `alias`، وفشل الأمر يُحتسب في رمز خروج `--batch`.
- **معدَّل** `app/src/main/java/com/dlof/rinlang/RinTerminalView.kt` — أسطر `$ cmd` تُوجَّه للجلسة الأصلية كأسطر `:`.
- **معدَّل** `res/values{,-ar,-en,-es}/strings_terminal_native.xml` — سطر مساعدة (إنجليزي).
- **معدَّل** `docs/terminal.md`، `tests/terminal/run.sh`؛ **جديد** `tests/terminal/xcmds_session.{in,expected}`.

## ما تحقّقتُ منه فعلاً
- ترجمة `rin_terminal.cpp` بلا تحذيرات (`-Wall -Wextra`) بالنسختين العادية و`-DRIN_TERMINAL_EMBED`.
- بناء `rin` كاملاً وتشغيل جميع الأوامر الـ 50 في جلسة `rin terminal` حقيقية، وفي `EmbeddedTerminal` (مسار RinStudio): glob، `~/`، المدى، alias، pushd/popd دون تغيير مجلد العملية، حجب `:sh`، أخطاء ورموز خروج، وحماية `rm /`. اختبار `xcmds_session` ناجح.
- **لم أستطع:** بناء أندرويد (لا NDK/kotlinc) — تغيير Kotlin صغير (دالة + Regex)، فراجعه عند أول بناء. اختبار pty التفاعلي لإكمال Tab بـ `$` لم يُجرَّب.

**الدفعة 3 (15 — متقدمة):** `bigfiles recent dupes empty loc todo` (ملفات) · `trash backup sync rename` (أمان ونسخ) · `json csv stats context verify` (بيانات).

كلها بالأسلوب نفسه: `$ أمر …`، `*` توسيع، `~/` المنزل، بلا تشغيل أي عملية خارجية (تعمل في RinStudio/أندرويد):
- `$ sync . ~/backup` ينسخ الجديد/المتغيّر فقط (يحفظ وقت التعديل فيصير التكرار فورياً) و`-n` معاينة؛ يرفض تداخل المصدر والوجهة.
- `$ trash f ~/old` حذف آمن إلى `~/.local/share/rin/trash` مع `-l` عرض و`-u <id>` استرجاع و`-e -y` تفريغ؛ يرفض `/` والمنزل.
- `$ backup main.rin -d ~/bk` ← `main.rin.bak-YYYYmmdd-HHMMSS`.
- `$ rename '(.*)\.txt' '$1.rin' *.txt` (`-n` معاينة، يرفض الكتابة فوق موجود إلا `-f`). **استعمل علامات اقتباس مفردة** كي لا يُوسَّع `$1` كمتغيّر.
- `$ json f.json users[0].name` تحقق/تنسيق/تصغير (`-m`)/استعلام مسار، مع رقم السطر والعمود عند الخطأ. `$ csv f.csv` جدول محاذى (فاصل تلقائي). `$ stats -e "3 1 4"` إحصاءات ومئينات و`-h` مدرّج. `$ verify sums.txt` مثل `sha256sum -c`.
- **جديد** `cli/linux/src/terminal/rin_terminal_xcmds3.h` (يُضمَّن من `rin_terminal_xcmds.h`، فلا تغيير في CMake)؛ **معدَّل** `rin_terminal_xcmds.h` (تسجيل الدفعة)؛ **جديد** `tests/terminal/xcmds3_session.{in,expected}` + فقرة في `tests/terminal/run.sh`.
- التحقق: ترجمة `-Wall -Wextra` بلا تحذيرات (عادية و`-DRIN_TERMINAL_EMBED`)، `tests/terminal/run.sh` كاملاً ناجح (بما فيه اختبارات pty: Tab، Ctrl-C، التاريخ)، وجرّبتُ الإكمال بـ Tab لـ `$ bigf⇥ -n 2 ~/pr⇥` في pty حقيقي.

---

## الدفعة 4 — **500 أمر جديد** (المجموع الآن 615)

كلها بالأسلوب نفسه `$ أمر …`، header-only، بلا تشغيل أي عملية (تعمل في RinStudio/أندرويد)، ونصوص الإخراج إنجليزية. لا تعارض أسماء مع الدفعات السابقة (تُفحص آلياً).

| العائلة | العدد | أمثلة |
|---|---|---|
| تحويل الوحدات | 157 | `$ convert 5 kg lb` · `$ units` (القائمة) · اختصارات `km2mi mi2km kg2lb c2f f2c kwh2j gb2mib psi2bar hp2kw …` |
| أدوات النص | 84 | `camel snake kebab pascal slugify wordwrap center levenshtein jaro soundex roman numword caesar vigenere morse nato tashkeel arnorm fancy upsidedown …` |
| ترميز | 15 | `base32 base58 base64url binary octal htmlesc jsonesc qp utf8bytes codepoints …` |
| هاشات وفحص | 21 | `sha512 sha384 sha224 hmac crc16 crc8 adler32 fnv1a32/64 murmur3 xxh32 luhn isbn ean13 iban entropy strength …` |
| ألوان | 15 | `hex2rgb rgb2hsl hsl2rgb lighten darken mix contrast luminance palette colorname …` |
| شبكة ومراجع | 16 | `cidr netmask ipcheck ip2int urlparse domainof emailcheck port httpstatus mime semverbump semvercmp …` |
| رياضيات وإحصاء | 124 | `sqrt fact isprime totient divisors comb perm stdev percentile zscore linreg det solve2 modpow modinv rpn const …` |
| مالية وهندسة | (ضمن الرياضيات) | `loan compound cagr roi tip vat discount circle sphere heron polygon …` |
| تواريخ | 23 | `now epoch fromepoch dayofweek weekno addays addmonths age countdown workdays easter hijri fromhijri datefmt zodiac parsedur moonphase …` |
| مولّدات | 12 | `dice coin pick password lorem randhex token ulid nanoid randint lottery randcolor` |
| ملفات | 13 | `tree filesize extstats oldest longlines badws eol fixeol bom magic sums countin lines` |
| مصادر Rin | 9 | `rinfiles rinelements rinlint rinblocks rindepth rincomments rinattrs rinstrings rinids` |
| بيانات | 11 | `spark barchart columns transpose csvcol csvsort csv2json csv2md jsonkeys iniget linefreq` |

**صدق في العدّ:** ≈157 أمراً من الوحدات و≈120 من الرياضيات مبنية على جداول/دوال متشابهة (اختصارات مثل `km2mi`)؛ والباقي (~220) أوامر فردية بمنطق خاص. كل أمر يعمل ويُختبر، لكن لا تتوقّع 500 خوارزمية مستقلة.

- `$ help <أمر>` و`:help` يعرضان الاستعمال؛ Tab يُكمل الأسماء والملفات.
- **ملاحظات:** `ln` محجوز لـ symlink فاللوغاريتم الطبيعي اسمه `loge`؛ `pascal` لتحويل النص، وصف الصفوف باسم `pascaltri`. أوامر `rin*` تحليل نصي للكتل `@ns.Type … .end/ns` ولا تشغّل المفسّر. التقويم الهجري هو الحسابي (tabular) وقد يختلف يوماً عن أم القرى.
- **ملفات:** جديد `rin_terminal_xcmds4.h` … `rin_terminal_xcmds7.h`؛ معدَّل `rin_terminal_xcmds.h` (ربط `registry4()`) و`rin_terminal.cpp` (إزالة تحذير متغيّر غير مستعمل)؛ جديد `tests/terminal/xcmds4_session.{in,expected}` + فقرة في `run.sh`.
- **التحقق:** ترجمة `-Wall -Wextra` بلا تحذيرات (عادية و`-DRIN_TERMINAL_EMBED`)؛ مقارنة مع Python لـ 66 حالة هاش/ترميز (SHA-224/384/512، adler32، base32، base64url بأطوال حدّية) وإحصاءات ورياضيات (comb/perm/fact/modpow/modinv…) كلها مطابقة؛ `run.sh` كاملاً ناجح. **لم يُجرَّب:** بناء أندرويد (لا NDK).

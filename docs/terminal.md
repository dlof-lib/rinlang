# الطرفية التفاعلية — `rin terminal`

طرفية C++ حقيقية فوق نفس محرّك المفسّر (`rin::Interpreter`)، بلا أي مكتبة خارجية (لا readline).
الملفات: `cli/linux/src/terminal/rin_terminal.{h,cpp}`.

## التشغيل
```bash
rin                       # عند وجود tty
rin terminal [خيارات] [ملفات.rin ...]     # أو rin repl / rin shell
rin terminal -i lib.rin -e 'let x = 1;'   # تحميل ملف وتنفيذ شيفرة قبل أول مؤشّر
printf '1 + 2\n' | rin terminal --batch   # وضع غير تفاعلي (يعيد 1 إن فشل أي أمر)
```
خيارات: `--no-color --no-highlight --no-hints --no-history --no-banner --no-autoprint --timing=auto|on|off --theme=dark|light --history-file P --batch`.

## ما تفعله
- **حالة تبقى بين الأوامر**: `let` / `fun` / `class` تبقى طوال الجلسة.
- **تعبير حرّ يطبع قيمته**: `x * 2` ← `=> 10` (`;` في آخره تمنع الطباعة؛ `_` = آخر قيمة). تُضاف `;` تلقائياً إن لزم.
- **أسطر متتابعة**: أقواس/نصوص/قوالب مفتوحة، أو سطر ينتهي بمُشغّل (`1 +`، `and`)، مع مسافة بادئة تلقائية وفك `}`.
- **تحرير**: الأسهم، Home/End، Ctrl-A/E/B/F/W/U/K/Y/T/L، Alt-B/F/D، لصق متعدد الأسطر (bracketed paste).
- **تاريخ** دائم (`~/.local/state/rin/history`)، ↑/↓ ببحث بالبادئة، Ctrl-R بحث عكسي، اقتراح باهت يُقبل بـ → أو End، `!!` و`!N`.
- **Tab**: كلمات اللغة، متغيراتك ودوالك، الأوامر، ومسارات الملفات (حتى داخل `"..."`).
- **تلوين الصيغة** أثناء الكتابة.
- **Ctrl-C** أثناء تشغيل برنامج يوقفه والحالة محفوظة (ضغطتان: خروج فوري). `input()` يعمل داخل الطرفية.

## الأوامر (`:help` داخل الطرفية)
`:load :reload :save :edit :vars :type :time -n N :check :history :keywords :set :pkg :sh :cd :pwd :ls :ocr :img :session :reset :clear :version :quit`

### `:set` — إعدادات الطرفية

| الصيغة | المعنى |
|---|---|
| `:set` | جدول كل الإعدادات (القيمة الحالية، `*` = معدَّلة عن الافتراضي، الوصف) |
| `:set <خيار>` أو `<خيار>?` | تفاصيل خيار (القيم المسموحة والافتراضي) |
| `:set <خيار> <قيمة>` أو `<خيار>=<قيمة>` | تعيين (`on/off/true/false/yes/no/1/0/toggle`) |
| `:set no<خيار>` / `<خيار>!` | إيقاف / تبديل |
| `:set theme light hints off noautoprint` | عدّة تعيينات في سطر واحد (تُتحقَّق كلها قبل تطبيق أي منها) |
| `:set reset [خيار]` | استرجاع الافتراضي |
| `:set save` | حفظ القيم المعدَّلة في `terminal.rc` (كتلة مُدارة، لا تمسّ بقية الملف) فتُطبَّق عند كل بدء |

الخيارات: `color highlight hints autoprint timing(auto|on|off) indent theme(dark|light)`.
الخيار الخاطئ يعيد اقتراح "هل تقصد؟". إن لم تدعم الطرفية الألوان (ليست tty أو `NO_COLOR`) تُحفَظ القيمة المطلوبة ويُنبَّه أنها غير فعّالة الآن.

وكلمات `exit` / `quit` / `help` / `clear` تعمل كما هي ما لم تعرّف متغيراً بنفس الاسم.

## المشاريع
```
:new hello                         # مشروع console (rin.toml + src/main.rin + tests/ + README + .gitignore)
:new ui-app --template indsin      # مشروع واجهة (عدّاد جاهز بـ @view + onTap)
:new mylib -t lib                  # مكتبة
:init [console|indsin|lib]         # تحويل المجلد الحالي إلى مشروع (لا يكتب فوق ملف موجود)
:project   :tree   :run   :test
```
`:new` يدخل المجلد الجديد تلقائياً. `:run` بلا وسائط يشغّل `src/main.rin` (ومشروع الواجهة يُفتح في indsin).
`:test` يشغّل كل `tests/*.rin`، كلٌّ في مفسّر **نظيف**، وينجح الملف إن لم يُطلق خطأ ولم يطبع `FAIL`.

## طباعة نص صورة (`:ocr`)
```
:ocr صورة.png [--lang ara+eng] [--psm N] [--enhance] [--min N]
:ocr "مسار فيه مسافات.png"
```
يطبع النص المقروء من الصورة بـ Unicode كما هو (عربي/لاتيني) داخل إطار خفيف عنوانه اسم الملف، ملتفّاً عند عرض الطرفية مع حفظ الفقرات، وفي سطره السفلي: الثقة · عدد الكلمات · اللغة (ASCII `+ - |` إن لم تدعم الطرفية UTF-8). لرسم الصورة نفسها استعمل `:img`.
لا منطق OCR جديد: الأمر يستدعي الدالة الموجودة `make.image.text` (انظر `docs/MAKE_MEDIA.md`) داخل المفسّر نفسه، فنفس الأدوات (tesseract) ونفس شروط الأمان والأخطاء الواضحة (أداة/لغة مفقودة).
تُحذف أحرف التحكم (ESC وC0/C1) من النص الخارج حتى لا تحقن الصورة تسلسلات طرفية؛ يبقى `\n` و`\t`. العربية تحتاج `tesseract-ocr-ara`.

## indsin داخل الطرفية
```
:indsin [ملف.rin] [--width N]      # تفاعلي (بلا ملف: src/main.rin للمشروع)
:indsin --dump | --plain           # إطار واحد (ANSI | نص فقط) بلا TUI
rin indsin ملف.rin [...]           # الأمر نفسه من الشل مباشرة
```
- نفس مسار المحرّك (`rin_indsin_session_*`: layout + Dye + الـ rasterizer الوحيد) — لا منطق رسم مكرّر ولا نافذة.
- يُعرَض بنصف-كتل `▀` ملوّنة (24-bit؛ 256 لوناً احتياطاً، `RIN_TRUECOLOR=0` لفرضها)، والنصوص حروف حقيقية فوق الخلفية.
- **الفأرة (SGR 1006)**: نقرة = `onTap` فعلي (تتغيّر حالة warp)، نقرتان = double-tap، زر أيمن = long-press، حركة = hover، العجلة تمرّر.
- **لوحة المفاتيح**: `q`/Esc خروج، `r` إعادة تحميل، `+`/`-` عرض الواجهة (يعيد الجلسة)، الأسهم/PgUp/PgDn/Home/End تمرير، `s` يحفظ PNG.
- **تحميل حيّ**: حفظ الملف في أي محرّر يحدّث العرض (Shuttle) مع بقاء الحالة.
- حدود: طلبات الوسائط (`pickMedia`/`uploadMedia`) تحتاج منتقي ملفات المضيف فتظهر رسالة في الشريط؛ النص العربي يعتمد على bidi في الطرفية نفسها.

## عرض صورة بحروف Unicode
```
:img صورة.png                          # الأنسب تلقائياً: نصف-كتل ملوّنة، أو braille إن لم تتوفر ألوان
:img "صورة بمسافة.jpg" --width 60      # عرض بالأعمدة (8–400؛ الافتراضي عرض الطرفية حتى 100)
:img logo.png --mode braille           # نقاط براي ⣿ — أدق تفصيلاً
:img logo.png --mode shade --invert    # ظلال ░▒▓█ (--invert لطرفية فاتحة الخلفية)
rin img logo.png --plain > logo.txt    # من الشل: بلا ألوان، مناسب للحفظ أو الأنابيب
```
- الأنماط: `half` (▀ بلونين، 24-bit أو 256 تلقائياً) · `braille` (2×4 نقطة لكل خانة) · `shade` · `ascii` (للطرفيات غير UTF-8).
- يعتمد النمط الافتراضي على الألوان: `half` معها، و`braille` بدونها (أنبوب، `NO_COLOR`، `--plain`).
- الصيغ: png jpg gif bmp (مدمجة) · webp tiff (عبر ImageMagick إن وُجد) — الإطار الأول للمتحركة. تُعرَّف الصيغة من محتوى الملف لا من امتداده، فملف SVG بامتداد png يُرفض.
- مفكّك الصور مدمج (`rin_imgdec.h`، يعتمد على zlib فقط): لا ImageMagick ولا أدوات خارجية لـ png/jpg/gif/bmp. JPEG الأساسي والتدريجي مع تدوير EXIF، وصور الكاميرا الضخمة (>12 ميغابكسل) تُفكّ بمقياس 1/8 بذاكرة صغيرة. webp/tiff فقط تحتاج ImageMagick إن وُجد (احتياطي، يُشغَّل بلا shell). رموز الخروج لـ `rin img`: 0 نجاح · 1 فشل · 2 وسائط خاطئة.

## 100 أمر إضافي (`$ …`)
أوامر نظام وملفات ونصوص مبنيّة داخل الطرفية نفسها (بلا تشغيل أي برنامج خارجي)، فتعمل **بالمثل في لينكس وفي RinStudio**. الأسلوب:
```
$ cat -n main.rin            # يبدأ الأمر بـ $   (أو بـ : كما قبل — $ ls == :ls)
$ cp *.rin ~/                # *  ?  [..] تُوسَّع (glob)، و ~/ هو مجلد المنزل
$ seq 1:10        $ seq 1…10:3         # المدى A:B أو A…B  (… للتكرار/المدى)
$ cat main.rin -r 5:12       $ cut -d , -f 2:4 data.csv
$ grep -rn "let" . | ...     # لا أنابيب: كل أمر مستقل، ويقبل -e "نص" بدل الملف
$ alias ll='cat -n'          # ثم:  $ ll main.rin
```
النصوص المُقتبسة `"…"` و`'…'` تمنع التوسيع؛ `$VAR` و`${VAR}` تُوسَّع من البيئة؛ `\` للهروب. `$ help <أمر>` أو `:help` يعرضان الاستخدام. جميع نصوص هذه الأوامر ومخرجاتها بالإنجليزية.

| المجموعة | الأوامر |
|---|---|
| قراءة | `cat head tail wc` |
| بحث | `grep find` |
| ملفات ومجلدات | `mkdir rmdir rm cp mv touch stat du realpath basename dirname` |
| نصوص | `echo sort uniq tac rev nl upper lower cut cmp` |
| تجزئة وترميز | `sha256 base64 hexdump crc32` |
| نظام وبيئة | `env export unset date whoami hostname uname which uptime free nproc df` |
| أدوات | `seq sleep alias unalias pushd popd dirs` |

**الدفعة الثانية (50 أمراً أخرى):**

| المجموعة | الأوامر |
|---|---|
| ملفات | `ll ln chmod readlink file mktemp exists tee append split` |
| نصوص | `diff comm paste tr fold replace trim shuf wordfreq count rot13` |
| تجزئة وترميز | `md5 sha1 urlencode urldecode hex unhex ascii conv` |
| رياضيات | `calc factor primes fib gcd rand uuid` |
| وقت | `timestamp cal datediff` |
| نظام | `id ps cpuinfo mounts printenv yes true false term path numfmt` |

أمثلة: `$ calc 2*(3+4)^2 / sqrt(16)` · `$ conv 255 -t 2` · `$ replace -i old new *.rin` · `$ diff a.rin b.rin` · `$ cal 10 2026` · `$ timestamp "2026-10-17 12:00:00" -u` · `$ ll -h ~/` · `$ chmod u+x run.sh`.
`replace -i` يعدّل الملفات في مكانها؛ `tee`/`append` تكتبان نصاً في ملف (بديل `echo > file`)؛ `diff` حتى 3000 سطر؛ `calc` يدعم `+ - * / % ^ ( )` و`sqrt sin cos tan ln log exp abs floor ceil round pow min max pi e`.
الكود في `rin_terminal_xcmds.h` (الدفعة 1) و`rin_terminal_xcmds2.h` (الدفعة 2).

ملاحظات: `rm` يرفض `/` ومجلد المنزل و`.`/`..`؛ `grep -E` تعبير منتظم، وبدونه بحث نصي؛ `find` يستعمل `-name/-iname/-type f|d/-maxdepth`؛ `sleep` حتى 300 ثانية؛ `seq` حتى 100000 عنصر؛ الملفات الأكبر من 64MiB تُرفض. ملف `terminal.rc` يقبل أسطر `$` أيضاً. الكود في `cli/linux/src/terminal/rin_terminal_xcmds.h`.
في RinStudio يلزم البادئة `$` (أو `:`) فتذهب للجلسة الأصلية؛ أمر `grep` بلا بادئة يبقى أمر السجل القديم في المحرر.

## بيئة
`RIN_HISTFILE`، `RIN_TERMINAL_RC` (أو `~/.config/rin/terminal.rc`: أوامر `:` تُنفَّذ عند البدء)، `RIN_LANG=en`، `RIN_THEME=light`، `NO_COLOR`.

## حدود معروفة
- المعرّفات العربية غير مقبولة من Lexer اللغة نفسها (النصوص العربية تعمل).
- المحرّك يراكم مخرجاته داخلياً طوال الجلسة؛ `:reset` يبدأ جلسة نظيفة.
- لا إلغاء تعاوني في المحرك: Ctrl-C يستخدم سقف الجمل (`setExecutionBudget`)، فلا يقطع استدعاء native عالقاً (الضغطة الثانية تفعل).
- Linux/POSIX فقط حالياً (macOS قابل للنقل بإضافة الملف وسطر الاستدعاء؛ Windows يحتاج ConPTY).

## الاختبار
`RIN_BIN=./build/rin tests/terminal/run.sh` (جلستان دفعيتان مقارنتان بملف متوقَّع، وفحوص pty حقيقية تشمل نقرات فأرة على واجهة indsin).

## داخل محرّر RinStudio (أندرويد)
طرفية لينكس نفسها مبنيّة داخل `librinengine.so` (الصنف `EmbeddedTerminal`، جسر JNI في `app/src/main/cpp/terminal/`) ومربوطة بتبويب Terminal في المحرّر:
```
:vars                     # أي أمر : يذهب لجلسة لينكس الأصلية (مفسّر دائم الحالة)
:load main.rin            # مسارات الملفات نسبةً لمجلد المشروع؛ :cd يغيّر مجلد الجلسة لا مجلد التطبيق
:ocr scan.png             # يعمل بمحرّك OCR في التطبيق
native on                 # كل شيفرة Rin تعمل في الجلسة الأصلية (حالة حقيقية بدل إعادة تشغيل التصريحات)
native off                # العودة للـ REPL المعتاد (أوامر : تبقى تعمل)
```
- `:ocr` تطبع الإطار الملوّن (الثقة بالأخضر/الأصفر/الأحمر) كما في لينكس، وتُرسم ألوان ANSI في الشاشة.
- لا تعمل في المحرّر: `:sh` و`:edit` و`:pkg` و`:indsin` (تحتاج شلاً أو tty) — تردّ برسالة واضحة. `:img` يعمل على أندرويد أيضاً (png/jpg/gif/bmp) بالمفكّك المدمج.
- المحرّك الأصلي لا يقبل المقاطعة: `^C` أثناء أمر طويل يعرض رسالة بدل الإيقاف. `^C` يلغي الأمر الناقص (متعدد الأسطر).
- الأمر الناقص (قوس/نص مفتوح) يكمل في السطر التالي داخل الجلسة نفسها، و`input()` يظهر في سطر الـ terminal.
- بُني بالعلم `RIN_TERMINAL_EMBED` (بدون RinPM). اختبار الجانب C++: `tests/terminal/embed_harness.cpp`.

## RinStudio
طرفية RinStudio تدعم `ocr <صورة> [--lang ara+eng] [--psm N] [--enhance] [--min N]` (نفس `make.image.text`)، وتعرض ألوان ANSI
(`colorAnsi` وغيرها) فوق Unicode؛ التفاصيل في `../.changelog/CHANGELOG.md`.

### أوامر `$` المتقدمة (الدفعة 3)

`$ bigfiles` `$ recent` `$ dupes` `$ empty` `$ loc` `$ todo` — استكشاف الملفات · `$ trash` `$ backup` `$ sync` `$ rename` — أمان ونسخ · `$ json` `$ csv` `$ stats` `$ context` `$ verify` — بيانات.
أمثلة: `$ sync . ~/backup` · `$ trash old.rin ~/tmp` · `$ rename '(.*)\.txt' '$1.rin' *.txt` · `$ json ~/cfg.json server.port` · `$ csv ~/sales.csv -n 20` · `$ stats -e "3 1 4 1 5" -h`.
تفاصيل في `../.changelog/CHANGELOG.md`.

### الدفعة 4 — 500 أمر إضافي
`$ convert 5 kg lb` وعشرات الاختصارات (`km2mi`، `c2f`…) · نصوص (`camel`، `slugify`، `levenshtein`، `roman`، `morse`…) · ترميز/هاش (`base58`، `sha512`، `hmac`، `crc16`، `luhn`…) · ألوان (`hex2rgb`، `contrast`…) · شبكة (`cidr`، `urlparse`…) · رياضيات وإحصاء ومالية (`fact`، `stdev`، `percentile`، `loan`، `rpn`…) · تواريخ (`age`، `workdays`، `hijri`، `easter`…) · مولّدات (`dice`، `password`، `ulid`…) · ملفات (`tree`، `eol`، `fixeol`، `magic`، `sums`…) · أدوات Rin (`rinlint`، `rinblocks`، `rinids`…) · بيانات (`csv2json`، `csvsort`، `jsonkeys`، `spark`…). التفاصيل في `../.changelog/CHANGELOG.md`.

## الهوية: Rin Shell و Rin Toolbox
الطرفية اسمها الرسمي **Rin Shell**، ومكتبة أوامرها المدمجة **Rin Toolbox** (615 أمراً في 13 عائلة). `:about` يعرض الشعار والإصدار، و`:toolbox` يعرض العائلات (`:toolbox math` يسرد أوامر عائلة). الأيقونات في `assets/branding/shell/`، والتفاصيل في `../.changelog/CHANGELOG.md`.


## مؤشرات التشغيل والتحميل (حسب نوع الأمر)
أثناء أي تشغيل يطول (> ~0.35s) على طرفية حقيقية يظهر سطر حيّ بهويّة نوع الأمر:
`⠋ Loading  slow.rin · importing rin.ui · Parsing · 2 lines  1.1s`. يختفي عند أي مخرجات وعند `input()`، ويعرض المكتبة
الجارية فعلاً. `:test` و`:time -n N` يرسمان شريط تقدّم (عدّاد + ETA + سرعة). التعطيل: `RIN_PROGRESS=plain`.
التفاصيل: `../.changelog/CHANGELOG.md`.

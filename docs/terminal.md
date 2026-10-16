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
- الصيغ: png jpg gif bmp webp tiff (الإطار الأول للمتحركة). تُعرَّف الصيغة من محتوى الملف لا من امتداده، فملف SVG بامتداد png يُرفض.
- يحتاج ImageMagick (`sudo apt install imagemagick`) ويُشغَّل بلا shell. رموز الخروج لـ `rin img`: 0 نجاح · 1 فشل · 2 وسائط خاطئة.

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
- لا تعمل في المحرّر: `:sh` و`:edit` و`:pkg` و`:indsin` (تحتاج شلاً أو tty) — تردّ برسالة واضحة. `:img` يحتاج ImageMagick فلا يعمل على أندرويد.
- المحرّك الأصلي لا يقبل المقاطعة: `^C` أثناء أمر طويل يعرض رسالة بدل الإيقاف. `^C` يلغي الأمر الناقص (متعدد الأسطر).
- الأمر الناقص (قوس/نص مفتوح) يكمل في السطر التالي داخل الجلسة نفسها، و`input()` يظهر في سطر الـ terminal.
- بُني بالعلم `RIN_TERMINAL_EMBED` (بدون RinPM). اختبار الجانب C++: `tests/terminal/embed_harness.cpp`.

## RinStudio
طرفية RinStudio تدعم `ocr <صورة> [--lang ara+eng] [--psm N] [--enhance] [--min N]` (نفس `make.image.text`)، وتعرض ألوان ANSI
(`colorAnsi` وغيرها) فوق Unicode؛ التفاصيل في `CHANGES_TERMINAL_UNICODE.md`.

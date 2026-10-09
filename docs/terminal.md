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
`:load :reload :save :edit :vars :type :time -n N :check :history :keywords :set :pkg :sh :cd :pwd :ls :session :reset :clear :version :quit`
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

## بيئة
`RIN_HISTFILE`، `RIN_TERMINAL_RC` (أو `~/.config/rin/terminal.rc`: أوامر `:` تُنفَّذ عند البدء)، `RIN_LANG=en`، `RIN_THEME=light`، `NO_COLOR`.

## حدود معروفة
- المعرّفات العربية غير مقبولة من Lexer اللغة نفسها (النصوص العربية تعمل).
- المحرّك يراكم مخرجاته داخلياً طوال الجلسة؛ `:reset` يبدأ جلسة نظيفة.
- لا إلغاء تعاوني في المحرك: Ctrl-C يستخدم سقف الجمل (`setExecutionBudget`)، فلا يقطع استدعاء native عالقاً (الضغطة الثانية تفعل).
- Linux/POSIX فقط حالياً (macOS قابل للنقل بإضافة الملف وسطر الاستدعاء؛ Windows يحتاج ConPTY).

## الاختبار
`RIN_BIN=./build/rin tests/terminal/run.sh` (جلستان دفعيتان مقارنتان بملف متوقَّع، وفحوص pty حقيقية تشمل نقرات فأرة على واجهة indsin).

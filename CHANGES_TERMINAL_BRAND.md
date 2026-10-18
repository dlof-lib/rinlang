# Rin Shell + Rin Toolbox — الأسماء الرسمية والهوية البصرية

## الأسماء الرسمية
| الاسم | ما هو |
|---|---|
| **Rin Shell** | الطرفية التفاعلية للغة Rin (الأوامر `:` و`$`، وتنفيذ شيفرة Rin مباشرة) |
| **Rin Toolbox** | مكتبة الأوامر المدمجة في Rin Shell — 615 أمراً (`$ cmd … ~/`) في 13 عائلة |

الاسمان علامتان ولا يُترجمان (في الواجهة العربية والإسبانية تبقى «Rin Shell» و«Rin Toolbox»).

## الأيقونات
- `assets/branding/shell/rin-shell.svg` (+ PNG: 48 / 192 / 512): نافذة طرفية داكنة بنقاط التحكم الثلاث، علامة `❯` بنفسجية (لون Rin) ومؤشر أخضر.
- `assets/branding/shell/rin-toolbox.svg` (+ PNG): صندوق أدوات بنفسجي يحمل `>_`.
- أندرويد: `drawable/ic_rin_shell.xml` و`drawable/ic_rin_toolbox.xml` (vector). أيقونة Rin Shell تظهر الآن بجانب تبويب الطرفية في `activity_main.xml`.
- لينكس: `packaging/linux/rin-shell.desktop` + `rin-shell.svg` (+ README بأمرَي التثبيت).

## الأيقونة المرسومة داخل الطرفية
`cli/linux/src/terminal/rin_terminal_brand.h`: الشعار نفسه 24×24 بكسل يُطبع بحروف نصف-كتلة (▀ ▄ █) بنقطتين لكل حرف = 12 سطراً × 24 عموداً، بألوان xterm-256 (ويتراجع إلى 16 لوناً حسب `TERM`). بلا ألوان أو بلا UTF-8: شعار نصي صغير (Unicode أو ASCII).

## التصميم
- **ترويسة جديدة** عند الإقلاع: الشعار يساراً، ويمينه: الاسم والإصدار، الوصف، خط فاصل، `Toolbox` (عدد الأوامر)، `Shell` (الأوامر الأهم)، `Keys`، `Session` (السمة والمجلد الحالي). الطرفية الضيقة (أقل من 68 عموداً) تعرض ترويسة مضغوطة بلا شعار.
- عنوان نافذة/تبويب الطرفية = **Rin Shell** (OSC 0)، فقط في طرفية تفاعلية بألوان.
- **`:about`** — الشعار + الاسم والإصدار والألوان وUnicode.
- **`:toolbox`** — جدول العائلات (الاسم، العدد، أمثلة)؛ `:toolbox math` يعرض كل أوامر العائلة بأعمدة منسّقة؛ البادئة تكفي، والاسم الغامض (`dat`) يُرفض مع القائمة.
- أندرويد: شاشة الترحيب «Rin Shell» وسطر التعريف (`terminal_welcome` بالأربع لغات) وتسمية التبويب.

## الملفات
جديد: `rin_terminal_brand.h`، `assets/branding/shell/*`، `drawable/ic_rin_{shell,toolbox}.xml`، `packaging/linux/*`.
معدَّل: `rin_terminal.cpp`، `rin_terminal_xcmds7.h` (عائلات Toolbox)، `activity_main.xml`، `strings_terminal.xml` ×4، `RinTerminalView.kt`، `tests/terminal/xcmds4_session.{in,expected}`، `docs/terminal.md`.

## التحقق
ترجمة بلا تحذيرات؛ `tests/terminal/run.sh` كاملاً ناجح (22)؛ صوّرتُ الترويسة و`:about` و`:toolbox` من طرفية pty حقيقية (256 لون). **لم يُجرَّب:** بناء أندرويد (لا NDK) — تغيير `ImageView` والموارد بسيط لكن راجعه عند أول بناء.

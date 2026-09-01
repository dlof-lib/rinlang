# Rin — Library Loader UI (مربوطة فعليًا بـ @import، ومُختبَرة)

كل الملفات هنا بمسارات مطابقة تمامًا لمستودعك — انسخها فوق الأصلية مباشرة.

## الحالة: **مربوطة ومُختبَرة على المفسّر الحقيقي**، لا محاكاة

كل سيناريو أدناه بُني وشُغِّل فعليًا (g++) على `rin_interpreter.cpp` الحقيقي، وليس على عرض توضيحي منفصل:

1. **الافتراضي (بلا أي علم جديد)** — تأكدت أن الناتج **مطابق حرفيًا** لما كان قبل هذه الميزة.
2. **`--import-progress`** — استيراد ناجح، ينهي بـ `✓ math loaded (embedded)`.
3. **استيراد مكرر** — يعرض `↺ math already imported (skipped)`.
4. **مكتبة غير موجودة** — يفشل بشكل صحيح دون تجميد الشريط، نفس رسالة الخطأ الأصلية.
5. **استيراد متداخل حقيقي** (مكتبة تستورد مكتبتين فرعيتين من القرص فعليًا) — الشجرة تُبنى تدريجيًا من التنفيذ الفعلي:
   ```
   Importing parent
   [██████████████------] 70%  Resolving dependencies...

   Loading:
   └── child_a   ...

   [████████████████████] 100%
   ✓ child_a loaded

   Loading:
   ├── child_a   ✓
   └── child_b   ...
   ...
   ```

## الملفات

| الملف | التغيير |
|---|---|
| `app/src/main/cpp/loader_ui/library_loader_ui.h/.cpp` | الوحدة نفسها (جديدة) |
| `app/src/main/cpp/loader_ui/demo_main.cpp` | عرض توضيحي مستقل (جديد، اختياري) |
| `app/src/main/cpp/rin_interpreter.h/.cpp` | إضافة `setImportUISink/setImportUIMode` + ربط المراحل الثمانية داخل `ImportStmt` الفعلي |
| `app/src/main/cpp/CMakeLists.txt` | إضافة `loader_ui/library_loader_ui.cpp` لأهداف `rinengine` و`rincheck` (أندرويد) |
| `cli/linux/CMakeLists.txt`, `cli/macos/CMakeLists.txt`, `cli/windows/CMakeLists.txt` | نفس الإضافة لكل CLI |
| `cli/linux/src/main.cpp`, `cli/macos/src/main.cpp`, `cli/windows/src/main.cpp` | علم `--import-progress` جديد، اختياري تمامًا |

## الاستخدام

```bash
rin run file.rin --import-progress   # linux
rin file.rin --import-progress       # macos/windows
rin -c "..." --import-progress       # يعمل مع -c وcheck أيضًا
```

بلا العلم = **نفس السلوك القديم تمامًا**، بلا أي فرق في الناتج أو الأداء (`importUiSink_` يبقى `nullptr`).

## نقاط صدق (لم أخفِها)

- **لا بث حي حقيقي على مستوى كل مرحلة لأي مستهلك خارجي بعد.** آلية `setStreamSink()` الحالية في `run()` تبث على مستوى "كل statement علوي كامل"، وربط كل مرحلة بها مباشرة كان سيُرسل نفس البايتات مرتين (اكتشفته وتراجعت عنه أثناء التنفيذ). النتيجة: الشريط يظهر بكامل نصّه (بما فيه كل رموز `\r`) دفعة واحدة في نهاية عبارة `@import`، وليس متحركًا حرفيًا بفواصل زمنية حقيقية — لأن مراحل تحليل/تحميل مكتبة مضمّنة أو من القرص تستغرق مايكروثانية فعليًا، لا ثوانٍ. هذا سلوك **صحيح وغير مخادع** (لا padding وهمي)، لكنه ليس "أنيميشن حي" بالمعنى البصري لمستهلك خارجي بعد. يصبح هذا مفيدًا بصريًا حقيقةً في سياق له زمن I/O فعلي (تنزيل حزمة RinPM عبر الشبكة).
- **شجرة التبعيات محدودة لمستوى واحد** (`depth==1` تحت أي مكتبة أعلى مستوى). أحفاد أعمق (مستوى 2+) لا يظهرون كصف شجرة مستقل، لكن شريطهم الخاص يعمل بشكل طبيعي.
- **RinPM (`rin pkg install`) لم تُربط** — لهذا مراحل حقيقية مختلفة (Resolve→Fetch→Extract→Register)، خارج نطاق هذا الربط بـ `@import`.

## البناء اليدوي (بلا CMake) للتجربة السريعة

```bash
CORE=app/src/main/cpp
g++ -std=c++17 -I "$CORE" cli/linux/src/main.cpp \
  "$CORE"/rin_lexer.cpp "$CORE"/rin_parser.cpp "$CORE"/rin_interpreter.cpp \
  "$CORE"/loader_ui/library_loader_ui.cpp "$CORE"/rin_http.cpp \
  "$CORE"/diagnostics/*.cpp "$CORE"/clc/*.cpp \
  cli/linux/src/pkg/*.cpp -lz -o rin
./rin run yourfile.rin --import-progress
```

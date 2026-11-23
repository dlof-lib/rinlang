## Container sequence hooks — حاوية تُفهرَس وتُعَدّ وتُكرَّر (الخطوة 4)
- خُطّافات جديدة: `on index(key)` · `on store(key, value)` · `on count()` · `on each()` (وتُستعمل `on has(key)` للـ `contains`).
- `Shelf[k]` · `Shelf[k] = v` · `len(Shelf)` · `contains(Shelf, k)` · `for x in Shelf` على اسم الحاوية المجرّد.
- `each()` يعيد مصفوفة/قاموساً/Set ويُكرَّر بقواعد `for` المعتادة (بما فيها `for (let [k, v] in C)`).
- المتغيّر الحقيقي يغلب الحاوية؛ حاوية بلا خُطّافات = سلوك قديم؛ حارس إعادة الدخول؛ يعمل مع `Autoload`.
- حدّ معروف: المسار `--vm` لا يدعم هذه الصيغ بعد.
- الملفات: `rin_container_magic.cpp` · `rin_interpreter.cpp/.h` · `rin_parser.cpp` · `docs/container_sequence.md` · `tests/print/container_sequence.rin`.

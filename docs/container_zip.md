# container.zip — ضغط الحاويات وأرشيفات ZIP

ضغط **ZIP حقيقي** (Deflate عبر zlib) يفتحه `unzip` و7-Zip ومستكشف الملفات. إضافة بحتة: لا يتغيّر شيء في الحاويات ولا في الكود القديم.
المحرّك مستقل في `app/src/main/cpp/rin_zip.h` (يُختبَر منفرداً: `tools/test_rin_zip.cpp`)، والـ natives في `rin_container_zip.cpp`.

## أسهل استعمال
```rin
container.zip("shop", "backup.zip");          // يضغط الحاوية (حقول + مستندات + صفوف جدول)
container.unzip("shop", "backup.zip");        // يستعيدها (تُنشأ إن غابت)
container.zipAll("all.zip");                  // كل الحاويات
container.unzipAll("all.zip");
```
ما يُحفَظ بدقّة كاملة: الأرقام العشرية (17 خانة)، `nil`، `Set` (يعود Set لا مصفوفة)، النصوص الثنائية، الحقول المتداخلة، **مستندات** `container.doc/datalist`، **صفوف** `table` ونمطها. لا تُحفَظ الدوال.

## container.*
| الدالة | تفعل |
|---|---|
| `container.zip(name \| [names], path, opts?)` | يضغط حاوية أو عدّة حاويات في ملف. يعيد `{ok, path, containers:[{name,kind,fields,documents,rows}], files, bytes, originalBytes, ratio, saved}` |
| `container.zipAll(path, opts?)` | كل الحاويات المسمّاة (`opts.kind` للتصفية، `opts.anonymous` لتشمل `#0..`) |
| `container.unzip(name, path, opts?)` | يستعيد حاوية. أرشيف بحاوية واحدة يُحمَّل بأي اسم؛ وإلا استعمل `opts.from` |
| `container.unzipAll(path, opts?)` | يستعيد كل ما في الأرشيف (`opts.only`، `opts.prefix`) |
| `container.zipInfo(path)` | فهرس الأرشيف بلا استعادة: `{format, version, rin, created, containers, attached, size, uncompressed, comment}` |
| `container.pack(name \| [names], opts?)` | مثل `zip` لكن يعيد **البايتات** (نصاً) — للشبكة أو التخزين في حقل |
| `container.unpack(name, bytes, opts?)` | مثل `unzip` من بايتات في الذاكرة |

**خيارات الضغط:** `level` (0 تخزين … 9 أقصى، الافتراضي 6) · `password` · `comment` · `time` (ثانيات epoch لأرشيف حتمي) · `only:[حقول]` / `omit:[حقول]` · `include:[مسارات]` و`dir:"مجلد"` (ملفات مرفقة تحت `files/`) · `exclude:[glob]`.
**خيارات الاستعادة:** `overwrite` (الافتراضي true؛ false = يحفظ الموجود ويضيف الناقص) · `from` · `password` · `extract:"مجلد"` (يستخرج الملفات المرفقة) · `maxFiles` `maxFileSize` `maxSize`.
تعيد الاستعادة `{ok, name, from, kind, created, fields, documents, rows, issues}`؛ `issues` من `validateContainer` إن وُجد نوع معرَّف.

## zip.* — أرشيفات عامة (بلا حاويات)
| الدالة | تفعل |
|---|---|
| `zip.create(path, source, opts?)` | ينشئ أرشيفاً. `source`: مسار \| `[مسارات ملفات/مجلدات]` \| `{اسم: محتوى}` (اسم ينتهي بـ`/` = مجلد؛ قيمة غير نصية تُحفَظ JSON) |
| `zip.add(path, source, opts?)` | يضيف/يستبدل بالاسم؛ الباقي يُنسخ **بلا فكّ ولا إعادة ضغط** (`opts.prefix`) |
| `zip.list(path)` | `[{name,size,packed,method,ratio,crc,dir,encrypted,time,comment}]` |
| `zip.find(path, pattern)` | أسماء مطابقة (`*` `?` أو بادئة مجلد) |
| `zip.info(path)` | ملخص: ملفات، مجلدات، أحجام، نسبة، تشفير، تعليق |
| `zip.read(path, entry, opts?)` / `zip.readJson` / `zip.exists` | قراءة ملف |
| `zip.extract(path, dest, opts?)` | `only:[glob]` `overwrite` `strip:N` `password` |
| `zip.remove(path, patterns)` · `zip.rename(path, from, to)` · `zip.combine(dest, [paths])` · `zip.comment(path, text?)` | تعديل بلا إعادة ضغط |
| `zip.test(path, opts?)` | فحص CRC لكل ملف: `{ok, entries, checked, skipped, errors:[{name,error}]}` — **لا يرمي** عند التلف |
| `zip.bytes(source, opts?)` / `zip.unbytes(bytes, opts?)` | أرشيف في الذاكرة ↔ `{اسم: محتوى}` |
| `zip.gzip(text, level?)` · `zip.gunzip(bytes)` · `zip.gzipFile(src, dst?, level?)` · `zip.gunzipFile(src, dst?)` | gzip |

كل كتابة ذرّية (ملف مؤقت ثم rename): لا يبقى أرشيف نصف مكتوب. الملفات الكبيرة والروابط الرمزية: لا تُتبَع الروابط أبداً.

## صيغة أرشيف الحاويات (`rin-container-zip` v1)
```
rin-container.json         {format, version:1, rin, created, containers:[{name,kind,file,fields,documents,rows}], files}
containers/<اسم>.json      {name, kind, fields:{…}, documents:[{id,doc}], table:[[…]], style}
files/…                    ملفات مرفقة (include/dir)
```
أرشيف ZIP عادي تماماً: يمكن فتحه وقراءة JSON بأي أداة. القيم الخاصة: `{"$set":[…]}` للمجموعات، `{"$bytes":"hex"}` للنصوص الثنائية.

## الأمان
- **Zip-Slip:** أي اسم فيه `..` أو مسار مطلق أو حرف قرص يُرفض؛ في الاستخراج يُلغى كله قبل كتابة أي ملف. كذلك تُرفض الكتابة عبر رابط رمزي يخرج من المجلد الهدف. المسارات تمرّ بعزل `resolvePath`.
- **قنابل الضغط:** حدود افتراضية: 50 ألف ملف، 256MB للملف، 1GB للمجموع بعد الفكّ (قابلة للتعديل بـ `maxFiles/maxFileSize/maxSize`)؛ الفكّ يُقيَّد بالحجم المعلن ويُتحقَّق منه.
- **التلف:** CRC-32 وحجم كل ملف يُفحصان عند كل قراءة؛ أرشيف مبتور أو ليس ZIP يعطي خطأ واضحاً.
- **كلمة المرور:** ZipCrypto (متوافق مع الأدوات القديمة) — **حماية خفيفة وليست تشفيراً قوياً**؛ لا تعتمد عليها لأسرار حقيقية. تشفير AES غير مدعوم (يُرفض بوضوح).

## حدود
ZIP64 (أرشيف/ملف > 4GB) غير مدعوم ويُرفض بوضوح · الأنواع غير القابلة للتمثيل (دوال، كائنات class) تُحفَظ كحقول map/`nil` · الأرشيف يُقرأ كاملاً في الذاكرة.
الاختبارات: `tests/print/container_zip.rin` · `zip_tools.rin` · `zip_security.rin` · `tools/test_rin_zip.cpp`.

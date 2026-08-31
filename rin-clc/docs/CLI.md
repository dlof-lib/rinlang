# clc CLI — التوثيق الكامل

## `clc pack <dir> -o <out.rcl> [options]`

يحزم شجرة مجلدات إلى حاوية CLC واحدة.

| الخيار | الوصف | الافتراضي |
|---|---|---|
| `-o <path>` | مسار ملف الإخراج | `<اسم المجلد>.rcl` |
| `--level 0..4\|ultra` | مستوى الضغط (انظر الجدول أدناه) | `2` |
| `--name <n>` | اسم المكتبة/المشروع (Metadata) | اسم المجلد |
| `--version <v>` | إصدار دلالي | `0.1.0` |
| `--author <a>` | المؤلف | (فارغ) |
| `--description <d>` أو `--desc <d>` | وصف | (فارغ) |
| `--license <l>` | الترخيص | (فارغ) |
| `--rin-version <v>` | إصدار Rin المستهدَف | (فارغ) |
| `--entry <file>` | نقطة الدخول (مثال: `src/main.rin`) | (فارغ) |
| `--dep <name:constraint>` | تبعية (يمكن تكراره) | — |

مستويات الضغط:

| المستوى | zlib level | تجزئة الملفات (chunk) | الوصف |
|---|---|---|---|
| `0` | بلا ضغط | — | STORE فقط، أسرع pack ممكن |
| `1` | 1 | 1 MiB | سريع |
| `2` | 6 | 1 MiB | متوازن (افتراضي) |
| `3` | 8 | 1 MiB | قوي |
| `4` | 9 | 1 MiB | أعلى |
| `ultra` | 9 | 8 MiB | أقصى ضغط، سياق أطول لكل كتلة، وقت أطول قليلاً |

مثال:
```
clc pack ./my_project -o my_project.rcl --level ultra \
  --name my_project --version 1.0.0 --author "Droy" --license MIT \
  --dep "math:>=1.2.0" --dep "core:^2.0.0"
```

## `clc unpack <file.rcl> -o <dir> [-q|--quiet]`

يفك الحاوية بالكامل. يتحقق من `content_hash` (sha256) لكل ملف أثناء
الاستخراج — أي عدم تطابق يوقف العملية برسالة خطأ واضحة (لا استخراج صامت
لملف تالف).

## `clc extract <file.rcl> <entry_path> -o <dir>`

يستخرج ملفاً واحداً فقط بمساره النسبي داخل الحاوية، **بلا** فك بقية
المشروع — يقرأ فقط الكتل المملوكة لذلك الملف تحديداً.

```
clc extract project.rcl src/main.rin -o ./out
```

## `clc list <file.rcl>`

يطبع جدولاً بكل الملفات: الحجم الأصلي، الحجم المضغوط، نسبة الضغط لكل ملف.

## `clc info <file.rcl>`

يطبع الرأس + الـ Metadata + جدول التبعيات + الإحصاءات الإجمالية.

## `clc check <file.rcl>`

فحص **سريع** (CRC32 لكل كتلة + اتساق بنيوي للفهرس/الرأس/Footer). لا يفكّ
أي محتوى فعلياً — مناسب كفحص دوري سريع على حاويات كبيرة.

خرج عند وجود تلف:
```
CLC Structural Check

Header        OK
Metadata      OK
Index         OK
Blocks        FAIL: 1 corrupted block(s)
Files         OK

ERROR:
Block 0001 is corrupted.
  Expected CRC32: 77f47a68
  Found CRC32:    31461ead

Result: CORRUPTED
```
(هذا خرج حقيقي مُلتقَط من تشغيل فعلي أثناء التطوير، وليس مثالاً وهمياً.)

## `clc verify <file.rcl>`

فحص **كامل**: كل ما في `check` + إعادة بناء كل ملف والتحقق من SHA-256 الخاص
به + التحقق من SHA-256 للحاوية الكلية.

```
CLC Integrity Check

Header                  OK
Metadata                OK
Index                   OK
Blocks                  OK
Files                   OK
File Hashes (sha256)    OK
Container Hash (sha256) OK

Integrity: VALID
```

## `clc convert <in.zip> <out.rcl>`

يستورد أرشيف ZIP قياسي (Store/Deflate فقط، بلا تشفير ولا Zip64) عبر قارئ ZIP
داخلي مبسَّط، ثم يبنيه كحاوية CLC عادية (بكل مزايا CLC: dedup، قاموس Rin،
تحقق). اتجاه واحد فقط في هذا الإصدار (zip → rcl).

## `clc test`

يشغّل حزمة الاختبارات الذاتية الحقيقية (pack → unpack → مقارنة byte-for-byte
+ اختبارات أمان/تلف). رمز الخروج `0` عند نجاح الكل.

## رموز الخروج

| الكود | المعنى |
|---|---|
| `0` | نجاح |
| `1` | خطأ أثناء التنفيذ (تلف، محتوى غير صالح...) |
| `2` | استخدام خاطئ لسطر الأوامر (وسائط ناقصة/غير معروفة) |

# CLC — Rin Compact Library Container (`.rcl`)

**CLC 1.0.0** هو صيغة حاويات ملفات جديدة ومستقلة بالكامل، مصمَّمة من الصفر
لمشاريع ومكتبات لغة **Rin**. ليست ZIP، ولا تستخدم ZIP كحاوية داخلية — التخطيط
الثنائي (magic number، الرأس، الفهرس، جدول الكتل) مصمَّم خصيصاً لـ CLC ويُشرَح
بالتفصيل في [`docs/FORMAT.md`](docs/FORMAT.md).

```
$ clc pack ./project -o project.rcl
CLC 1.0.0

Scanning project...
Found 42 files
Found 18 Rin files
Building dictionary...
Deduplicating blocks...
Compressing (level 2 (balanced))...
Writing container...

Done.

Original:   4.82 KB
CLC:        1.17 KB
Saved:      75.7%
```

## لماذا C++17؟

اخترنا **C++17** لتنفيذ المحرّك الأساسي والـ CLI، للأسباب التالية:

1. **تكامل مستقبلي مباشر مع Rin**: مفسّر Rin نفسه (`rin_interpreter.cpp`) مكتوب
   بـ C++17. بناء CLC بنفس اللغة يعني أنه يمكن ربطه لاحقاً كدوال native
   (`container.open`, `library.import`) بلا أي طبقة توصيل بين لغتين — انظر
   [`docs/RIN_INTEGRATION.md`](docs/RIN_INTEGRATION.md) لتصميم جاهز للتنفيذ.
2. **تحكّم دقيق بالبايت**: صيغة حاوية ثنائية من الصفر (رأس بحقول ثابتة الحجم،
   جداول offsets/checksums) تحتاج تحكماً صريحاً بالتخطيط الثنائي — C++ يوفّر
   هذا بلا أي طبقة تسلسل (serialization) خارجية.
3. **أداء حقيقي للـ streaming/chunking**: قراءة/ضغط/كتابة ملفات كبيرة على شكل
   دفعات بذاكرة محدودة (انظر §الأمان والذاكرة أدناه) يحتاج تحكماً منخفض
   المستوى بالـ I/O.
4. **لا اعتماديات خارجية تقريباً**: فقط zlib (لخوارزمية DEFLATE كمرحلة ضغط،
   وليست كصيغة حاوية) — وهي نفس الاعتمادية المستخدَمة بالفعل في مشروع Rin
   نفسه (`lib/rinzip.og.rin` عبر `zlibDeflateRaw`/`zlibInflateRaw`).

## البناء

```bash
./build.sh          # يبني build/clc عبر g++ مباشرة (أو CMake إذا كان متوفراً)
./build/clc test    # يشغّل حزمة الاختبارات الذاتية (pack/unpack/تحقق/أمان)
```

المتطلبات: مترجم يدعم C++17 (g++ 9+/clang 10+)، `zlib1g-dev` (أو ما يعادلها).

## الأوامر

```
clc pack <dir> -o out.rcl [--level 0..4|ultra] [--name] [--version] [--author]
                          [--description] [--license] [--rin-version] [--entry]
                          [--dep name:constraint]...
clc unpack <file.rcl> -o <dir>
clc extract <file.rcl> <entry_path> -o <dir>
clc list <file.rcl>
clc info <file.rcl>
clc check <file.rcl>
clc verify <file.rcl>
clc convert <in.zip> <out.rcl>
clc test
```

تفاصيل كاملة: [`docs/CLI.md`](docs/CLI.md).

## البنية

```
rin-clc/
├── README.md          (هذا الملف)
├── LICENSE             MIT
├── CHANGELOG.md
├── CMakeLists.txt / build.sh
├── docs/
│   ├── FORMAT.md        مواصفة CLC 1.0 الرسمية (Binary Layout الكامل)
│   ├── CLI.md            توثيق كل أوامر clc
│   ├── API.md            واجهة C++ (clc_container.h) + تصميم Rin API
│   ├── SECURITY.md       نموذج التهديد والحماية عند unpack
│   └── RIN_INTEGRATION.md كيف تُربَط CLC بمفسّر Rin كدوال native (تصميم جاهز)
├── src/                  المصدر الكامل (SHA-256، الضغط، Rin optimizer، الحاوية، CLI)
├── tests/                بيانات اختبار يدوية إضافية (الاختبار الحقيقي عبر `clc test`)
├── benchmarks/           سكربت قياس + نتائج حقيقية مُقاسة فعلياً (`results.md`)
├── examples/             مشروع Rin تجريبي (`rin_project/`) لتجربة pack/unpack
└── assets/               أيقونة المشروع (icon.png، من تصميم المستخدم)
```

## الأمان والذاكرة (ملخّص، التفاصيل في `docs/SECURITY.md`)

- يُرفَض أي مسار مطلق أو يحتوي `..` عند فك الضغط (Path Traversal) — مُختبَر في
  `clc test`.
- كل الأعداد المقروءة من الرأس/الفهرس تُقارَن بحجم الملف الفعلي قبل أي تخصيص
  ذاكرة (يمنع Memory Bomb من رأس مزوَّر).
- فك الضغط يُخصِّص بالضبط الحجم الأصلي المُعلَن لكل كتلة (لا نمو غير محدود
  Decompression Bomb).
- الملفات الثنائية الأكبر من 8MB تُبَثّ (streamed) بذاكرة محدودة (~1MB في كل
  لحظة عند level عادي، ~8MB عند `--ultra`) بلا تحميل الملف كاملاً؛ نفس الأمر
  عند unpack (كتلة كتلة).

## القيود الحالية (بصراحة تامة، بلا تجميل)

هذه الحدود موثّقة صراحة بدل تجاهلها، حسب طلب صريح من صاحب المشروع (§22):

1. **الفهرس/الجداول الوصفية تبقى في الذاكرة أثناء pack** (بحجم يتناسب مع عدد
   الملفات، وليس حجم محتواها) — تدفّق كامل بلا أي جزء في الذاكرة (0-RAM index)
   يتطلّب دمج خارجي (external merge) غير منفَّذ في 1.0؛ مُقترَح كتحسين CLC 1.1.
2. **الضغط "غير صلب" (non-solid)**: كل ملف/كتلة تُضغَط منفصلة (يشبه ZIP من
   هذه الناحية تحديداً، رغم اختلاف الصيغة كلياً)، لأجل: (أ) استخراج ملف واحد
   بلا فك المشروع كاملاً (§5 من المتطلبات)، و(ب) streaming حقيقي بذاكرة
   محدودة. **الأثر**: على مشاريع نصوص/كود كثيرة الملفات الصغيرة، أدوات
   "solid archive" (مثل `tar.gz` الذي يضغط كل الملفات كتدفّق واحد) قد تحقّق
   نسبة ضغط أعلى — قِسنا هذا فعلياً في `benchmarks/results.md` بدل الادّعاء:
   على مكتبة Rin الحقيقية (23 ملف .rin)، حقّق CLC نسبة أفضل من ZIP القياسي
   لكن أقل من `tar.gz` بسبب هذا بالضبط. **المقترَح**: وضع "Solid Block Mode"
   اختياري لمستوى `--ultra` في CLC 1.1 (يُجمَّع كل النصوص/Rin في كتلة واحدة
   قبل الضغط، مع فقدان القدرة على استخراج ملف واحد من تلك الكتلة بلا فكّها
   بالكامل — نفس المقايضة التي تعتمدها 7z الحقيقية).
3. **قاموس Rin/النصوص عام لكل الحزمة** (لا تخصيص per-directory)، وسقفه 250
   رمزاً — كافٍ لمشاريع متوسطة الحجم، وموثَّق كحدّ صريح وليس خطأً خفياً.
4. **`clc convert` يدعم استيراد zip باتجاه واحد** (zip → rcl) عبر قارئ ZIP
   داخلي مبسَّط (Store/Deflate فقط، بلا تشفير ولا Zip64). لا يوجد مُصدِّر
   rcl → zip في هذا الإصدار (لم يُطلَب صراحة، ويمكن إضافته بسهولة لاحقاً
   بإعادة استخدام نفس `deflateRaw`).
5. **الروابط الرمزية (symlinks)** تُتجاهَل بأمان أثناء `pack` في هذا الإصدار
   (لا تُتبَع ولا تُخزَّن) — تجنّباً لمخاطر أمنية إضافية قبل تصميم معالجة
   مخصّصة لها.

## الترخيص

MIT — انظر [`LICENSE`](LICENSE)، بنفس ترخيص مشروع RinLang الأساسي.

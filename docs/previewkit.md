# previewkit — معاينة حيّة لملفات PDF / Markdown / TypeScript

مكتبة Rin خالصة (`lib/previewkit.og.rin`، مضمّنة في كل البنى) تحوّل ملفاً إلى **صفحة HTML كاملة** تُعرض في WebView أو المتصفح،
وتُعيد توليدها تلقائياً عند أي تعديل. كل ما فيها مكتوب بـ Rin فوق natives موجودة، مع native واحد جديد: `zlibInflate`.

```kotlin
@import "lib/previewkit.og.rin";

pvRun("notes.md", {});                                   // يكتب notes.md.preview.html ويعيد الملخص
pvPreview("main.ts", "main.html", {"lang": "en"});        // صفحة بإخراج تختاره
pvLive("report.pdf", "report.html", {"embedSrc": "report.pdf", "intervalMs": 800, "maxTicks": 300});
```

## الأنواع
| النوع | الامتدادات | ما تعرضه الصفحة |
|---|---|---|
| Markdown | `md markdown mdown mkd` | HTML عبر `relyRIN` + فهرس عناوين جانبي بروابط + كلمات/زمن القراءة |
| PDF | `pdf` (أو توقيع `%PDF-`) | نص كل صفحة + بيانات الوثيقة (عنوان/مؤلف/مُنتِج/تاريخ/خطوط/صور) + تبويب «الأصل» (`embedSrc`) |
| TypeScript / JavaScript | `ts tsx mts cts js jsx mjs cjs` | تلوين، **هيكل الملف** (class/interface/type/enum/function/const + أعضاء الأصناف)، الاستيرادات، فحص أقواس ونصوص غير مغلقة |
| JSON | `json jsonc webmanifest` | تلوين (المفاتيح مميّزة) + حكم الصلاحية |
| كود عام | `rin kt java c cpp go rs py ...` | عرض ملوّن بأرقام أسطر |

## الواجهة
| الدالة | الوصف |
|---|---|
| `pvRender(path, opts)` | يقرأ الملف ويعيد `{ok, kind, html, ...}` |
| `pvRenderText(kind, src, opts)` | نفسه من نص مباشر (`"markdown"`, `"typescript"`, `"json"`, ...) |
| `pvPreview(path, outPath, opts)` | يولّد ويكتب الصفحة؛ يعيد `{ok, kind, output, bytes, ms, stamp}` |
| `pvRun(path, opts)` | اختصار: يكتب `path + ".preview.html"` (أو `opts.output`) |
| `pvPollOnce(path, out, opts, state)` | فحص واحد **غير حاجب** لبصمة الملف (CRC32): يعيد `{changed, state, result}` — مناسب لحلقة المضيف |
| `pvLive(path, out, opts)` | مراقبة حاجبة: تولّد ثم تفحص كل `intervalMs` (50–60000) حتى `maxTicks`؛ `onUpdate(result)` اختيارية |
| `pvStamp(path)` | بصمة المحتوى |
| `pvDetect(path, bytes)` / `pvSupported()` | كشف النوع / الأنواع المدعومة |
| `pvPdfText(path)` / `pvPdfInfo(path)` / `pvPdfAnalyze(bytes)` | استخراج نص PDF وبياناته بلا HTML |
| `pvTsOutline(src, "ts"\|"js")` | `{outline, imports, problems, functions, types}` |

### الخيارات (`opts`)
`lang` (`"ar"` الافتراضي أو `"en"`) · `live` (يضيف تحديثاً ذاتياً للصفحة مع حفظ موضع التمرير) · `intervalSec` ·
`dark` (`"auto"` / `"on"`) · ألوان الثيم (`accent`, `bg`, `text`, ... كما في `relyTheme`) · `embedSrc` (مسار نسبي لـ PDF الأصلي) · `kind` (فرض النوع).

## كيف تعمل المعاينة الحيّة
1. `pvLive`/`pvPollOnce` يقارنان بصمة الملف ويعيدان توليد HTML عند التغيّر فقط.
2. الصفحة الناتجة بخيار `live: true` تُحدّث نفسها (`meta refresh`) وتحفظ موضع التمرير، فيظهر التعديل في العارض دون تدخّل.

## استخراج PDF — ما يدعمه وما لا يدعمه
- يدعم: جداول xref وxref streams (لا يعتمد عليها أصلاً؛ يقرأ الأجسام مباشرة)، object streams، `FlateDecode` و`ASCII85Decode` و`ASCIIHexDecode`،
  ترتيب الصفحات من شجرة `/Pages`، موارد الخطوط الموروثة، خطوط بسيطة (WinAnsi) وخطوط Type0 مع `ToUnicode`.
- المسافات بين الكلمات تُحسب من **عروض الخطوط** (`/Widths` و`/W`) والفجوات الفعلية، ومصفوفات `cm`/`Tm` تُتابَع لتحديد الأسطر.
- العربية/العبرية: يُفضَّل `/ActualText` (Chrome/Skia)، وإلا تُرتَّب المقاطع بصرياً بحسب موضع x ثم تُحوَّل إلى الترتيب المنطقي.
- غير مدعوم (يُبلَّغ بوضوح بدل التخمين): الملفات المشفّرة، `LZWDecode`/`RunLengthDecode`، خطوط Type0 بلا `ToUnicode`، الصفحات الممسوحة (لا OCR).
  للمعاينة البصرية الحقيقية استخدم تبويب «الأصل» (يعرضه عارض PDF في المتصفح).
- **TypeScript**: لا تنفيذ للشيفرة (لا native لتشغيل عمليات)؛ المعاينة = تلوين + هيكل + فحص. regex literal يُكتشف بعد `= ( , : [ ! & | ? { } ;` فقط.

## native جديد: `zlibInflate(data, maxBytes?)`
يفك تغليف zlib/gzip **دون معرفة الحجم الأصلي** (على عكس `zlibInflateRaw`)؛ يُرجع `nil` للبيانات التالفة أو عند تجاوز السقف
(افتراضي 64MB، أقصى 512MB — حماية من قنابل الضغط).

## الاختبار
`rin tests/previewkit_tests.rin` — 22 اختباراً (تُبنى ملفات PDF داخل الاختبار نفسه). مثال: `examples/previewkit_demo.rin`.

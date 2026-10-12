# gorin — طبقة API والربط فوق محرك Rin

gorin **ليس بديلاً** لمحرك Rin الأصلي (C++)، بل طبقة فوقه تتولى ما يصعب في C++: خادم HTTP/JSON،
المهل الزمنية الصارمة، سقف التزامن والمخرجات، والمصادقة. يشغّل gorin ملف `rin` الحقيقي كعملية فرعية
لكل طلب، فالنتائج هي نتائج المحرك الأصلي نفسه. وبجانبه مفسّر Go مدمج (حزمة `rin/`) يعمل كمحرك
**احتياطي** عند غياب ملف `rin` أو عند طلبه صراحةً.

> **حالة الإصدار 0.1:** كُتب في بيئة بلا مترجم Go، فلم يُبنَ بعد. أول خطوة: `./run_tests.sh`.

## البوابة (الأهم)
```
go build -o gorin ./cmd/gorin
./gorin serve --rin /path/to/rin --token SECRET          # 127.0.0.1:8787
curl -s localhost:8787/run -H "Authorization: Bearer SECRET" \
     -d '{"code":"print 1e3 + 1;","max_depth":5000,"budget":1000000,"timeout_ms":2000}'
curl -s localhost:8787/health
./gorin run --rin /path/to/rin file.rin                   # تشغيل ملف عبر نفس الطبقة
```
`POST /run` يقبل: `code` (إلزامي)، `engine` (`cpp`|`go`)، `max_depth` (حتى 50000)، `budget`، `timeout_ms`، `stdin`.
يعيد: `ok, engine, stdout, stderr, exit_code, duration_ms, timed_out, truncated`.

| الحماية | التفصيل |
|---|---|
| المهلة | تُقتل العملية عند تجاوز `--timeout` (الافتراضي 10 ث) ويُرجَع `timed_out: true` |
| التزامن | `--max-concurrent` (الافتراضي 4)؛ الزائد يأخذ 503 بعد ثانيتين |
| المخرجات | تُقتطع عند `--max-output` (1MB) مع `truncated: true` بلا تعطيل العملية |
| المصادقة | `Authorization: Bearer`؛ ويرفض الاستماع خارج loopback بلا توكن |
| العزل | كل طلب في مجلد مؤقت يُحذف بعده (لكنه **ليس sandbox**: سكربتات Rin قد تصل للملفات والشبكة، فلا تعرض البوابة للإنترنت العام) |

## خدمات الربط `/svc/*` (يستدعيها Rin نفسه)
سكربت Rin يصل لما تقدّمه Go عبر دوال الـ API الموجودة أصلاً في اللغة (`apiRegister`/`apiPost`)، بلا أي تعديل في C++:
```rin
apiRegister("go", "http://127.0.0.1:8787");
apiHeader("go", "Authorization", "Bearer SECRET");
let r = apiPost("go", "/svc/hash", {"algo": "sha256", "data": "abc"});
print r.json["result"];     // ba7816bf8f01cfea...
```
كل الردود `{"ok": true, "result": ...}` أو `{"ok": false, "error": "..."}` (مع رمز HTTP مناسب).

| الخدمة | الجسم | الناتج |
|---|---|---|
| `hash` | `algo` (md5/sha1/sha256/sha512)، `data` | سداسي عشري |
| `hmac` | `algo`، `key`، `data` | سداسي عشري |
| `base64` | `op` (encode/decode)، `data`، `url` | نص |
| `uuid` | — | UUID v4 آمن تشفيرياً |
| `random` | `n` (1..1024) | بايتات عشوائية آمنة بصيغة hex |
| `json` | `op` (pretty/minify)، `data` | نص |
| `regex` | `op` (match/find/findall/replace/split)، `pattern`، `text`، `repl` | محرك RE2: زمن خطّي، لا ReDoS |
| `time` | — | `unix_ms` و`utc` |
| `fetch` | `url`، `method`، `headers`، `body`، `timeout_ms` | `status`، `headers`، `body` |

`fetch` **معطّل افتراضياً**: لا يعمل إلا للمضيفين الذين تسمح بهم صراحةً
`./gorin serve --allow-host api.example.com --allow-host "*.internal.example"`
(يمنع SSRF؛ وإعادة التوجيه إلى مضيف غير مسموح تُرفض؛ الجسم المُرجَع حتى 2MB).

### من داخل Rin: `lib/gorinkit.og.rin`
غلاف جاهز فوق `/svc/*` و`/run` (بلا أي تعديل في C++):
```rin
@import "lib/gorinkit.og.rin";
gorin_connect("http://127.0.0.1:8787", "SECRET");   // اختياري
print gorin_hash("sha256", "abc");
print gorin_regexFindAll("[0-9]+", "a1b22");         // ["1", "22"]
let r = gorin_run("print 1 + 2;", 0, 0, 2000);       // {ok, stdout, stderr, exit_code, ...}
try { gorin_uuid(); } catch (e) { print e["message"]; }   // "gorin: ..." عند أي فشل
```
اختُبر الغلاف فعلياً مع محرك C++ الحقيقي ضد بوابة وهمية بـ Python تحاكي عقد gorin
(`bridge_test/run.sh`، والمخرجات المتوقعة في `bridge_test/expected.txt`). انتبه: `text` كلمة محجوزة في Rin فلا تُستعمل اسم وسيط.

## المحرك المدمج بـ Go (احتياطي)
```
go build -o gorin-embedded ./cmd/rin
./gorin-embedded file.rin
./gorin-embedded -e 'print 1e3 + 1;'
./gorin-embedded --max-depth=50000 --budget=1000000 file.rin
```

## ما يدعمه (النواة)
| المجال | المدعوم |
|---|---|
| قيم | رقم (double)، نص، منطقي، `nil`، مصفوفة، قاموس مرتّب بفهرس تجزئة O(1)، دوال، كائنات |
| أرقام | `5`، `3.14`، **`1e18` / `2.5E-3`** |
| نصوص | `"…"` مع `\n \t \\ \"`، والقالبية `` `a ${x + 1}` `` |
| تحكّم | `if/else`، `when/otherwise`، `while`، `for(;;)`، `for (let x in …)` (مصفوفة/قاموس/نص)، `break/continue/return` |
| دوال | `fun`، دوال مجهولة `fun(x){}`، إغلاقات (closures)، تلميحات الأنواع تُقرأ وتُتجاهل |
| أخطاء | `try/catch(e)` و`throw`؛ قيمة `e` قاموس `{value,message,line}` أو `{message,line,code}` كالأصل |
| OOP | `class`، `extends`، `init`، `self`، حقول بقيم ابتدائية |
| مدمجات | `len type str toString toNumber toBool upper lower trim substr split join indexOf replace contains charAt chr ord push pop sort keys values has remove abs sqrt floor ceil round trunc sin cos tan exp log log10 pow min max random sum mean rinVersion` |
| حدود | `--max-depth` (E0035 قابل للالتقاط)، `--budget` (سقف عدد العبارات) |

عرض القيم مطابق للأصل: الأعداد الصحيحة بلا كسر، والكسور بدقة 6 خانات، والنصوص داخل المصفوفات مقتبسة.

## ما لا يدعمه بعد
`match`، `enum`، `struct`، `@container/@view…` وكتل الواجهات، الحلقات الحيّة (`stone/gauge/…`)،
`import`/الحزم، مكتبة الدوال الواسعة (ألوان، regex، http…)، المجموعات `set`.
هذه مجالات المحرك الأصلي؛ المحرك Go مخصّص للنواة والتضمين في برامج Go.

## التضمين في برنامج Go
```go
in := rin.New(os.Stdout)
in.MaxDepth = 5000
in.Budget = 1_000_000
if err := in.Run(`print "hi";`); err != nil { /* *rin.RinError{Code, Msg, Line} */ }
```

## البنية
```
rin/lexer.go   المحلل المعجمي (أرقام علمية، قوالب)
rin/parser.go  المحلل النحوي → AST (ast.go)
rin/interp.go  المفسّر (return/break/continue كقيمة flow، لا panic)
rin/env.go     النطاقات: مسح خطّي للصغير، تجزئة للكبير
rin/value.go   القيم + Map مرتّب بفهرس
rin/builtins.go المدمجات
cmd/gorin      البوابة: serve / run
cmd/rin        CLI للمفسّر المدمج
gateway/       خادم HTTP + تشغيل rin كعملية فرعية + حدود
```

## فروقات مقصودة عن الأصل
- `split(s, "")` و`for ch in "نص"` يعملان على **أحرف UTF-8** (الأصل يقطع بايتات).
- لم يُنفَّذ بعد تسريع `s = s + x` (نصوص Go ثابتة؛ التجميع تربيعي كالأصل قبل التحسين).

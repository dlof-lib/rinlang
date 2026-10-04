# MAKE_MEDIA.md — فيديو · صوت · API · إزالة الخلفية · قراءة النص من الصور

كلها دوال داخل مساحة الاسم `make` (جنب `make.qr` / `make.file` / `make.hash`)، وتخضع لسياسة قدرات
[`@make.(name)`](./MAKE_UNIT.md).

**قاعدة الأخطاء:** فشل البيئة (أداة مفقودة، شبكة، ملف غير صالح) **لا يرمي خطأً** — يُرجع `{ok: false, error: "..."}`.
سوء استخدام الدالة (نوع وسيط خاطئ، عدد وسائط خاطئ) يرمي خطأً صريحاً كبقية اللغة.
كل المسارات المحلية تمر عبر `resolvePath` فتبقى داخل مجلد المشروع. لا shell إطلاقاً (argv مباشرة).

## القدرات (للسياسة)
| الاستدعاء | القدرة |
|---|---|
| `make.video`, `make.video.info/thumbnail/stop` | `video` |
| `make.audio`, `make.audio.info/stop` | `audio` |
| `make.api`, `make.api.download/clearCache` | `api` |
| `make.image.removeBg` | `image` |
| `make.ocr`, `make.image.text` | `ocr` |
| `make.media.tools` | — (لا قدرة) |

الكشف يعمل داخل أي تعبير (let / print / if / return / lambda / match ...).

```rin
@make.(poster)
    kind task;
    use image; use ocr; use io;
    need image;
    deny api;        // أي make.api هنا = خطأ فوري
    strict;
    let r = make.image.removeBg("photo.png", "photo.nobg.png");
    print r.ok;
.end/make=poster
```

الأنواع التي تسمح افتراضياً: `page` (api, video, audio, image, ocr) · `component` (video, audio, image) ·
`service` (api, image, ocr) · `task` (api, audio, image, ocr) · `plugin` (الكل) · `app` (بلا قيود).
`library` / `module` / `data` لا تسمح بشيء من هذه إلا بـ `allow` صريحة.

## make.media.tools()
يُرجع ما يعمل فعلاً على هذا الجهاز: `platform`, `bridge`, `tools{...}`, `videoPlayers[]`, `audioPlayers[]`,
`ocrLangs[]`, `display`, `removeBg` (`rembg` | `imagemagick` | `bridge` | `none`), `ocr`.

## الفيديو
| الدالة | الوصف |
|---|---|
| `make.video(src, opts?)` | يفتح/يشغّل فيديو (ملف أو http/https) في مشغّل النظام |
| `make.video.info(src)` | `duration, width, height, fps, videoCodec, audioCodec, bitrate, size, format...` (ffprobe) |
| `make.video.thumbnail(src, out, opts?)` | إطار كصورة `.png/.jpg/.webp` — `at` (ثوانٍ، 1)، `width` |
| `make.video.stop()` | يوقف المشغّلات التي فتحها `make.video` |

خيارات `make.video`: `player` (`mpv`/`vlc`/`ffplay`/`xdg-open`/`open` — تلقائي إن غاب)، `fullscreen`, `loop`, `mute`,
`start` (ثوانٍ), `volume` (0–100), `wait` (انتظر انتهاء التشغيل), `timeout`, `dryRun` (يعرض الأمر فقط).
النتيجة: `{ok, backend, pid, playing, command[], ignored[]}` — `ignored` الخيارات التي لا يدعمها المشغّل المختار.

## الصوت
`make.audio(src, opts?)` · `make.audio.info(src)` · `make.audio.stop()`.
الخيارات: `player` (`mpv`/`ffplay`/`vlc`/`afplay`/`paplay`/`aplay`)، `loop`, `start`, `volume`, `wait`, `timeout`, `dryRun`.
`paplay/aplay/afplay` لا تقرأ الروابط — لن تُختار تلقائياً لمصدر http.

## make.api — جلب البيانات
```rin
let r = make.api("https://api.example.com/users", {
    "query":   {"page": 2, "q": "علي"},          // ?page=2&q=%D8%B9... تلقائياً
    "auth":    "TOKEN",                           // Bearer | {"basic":"u:p"} | {"key":"K","header":"X-API-Key"}
    "path":    "data.items[0].name",              // يستخرج جزءاً من JSON (json.get)
    "retries": 3, "backoff": 400,                 // إعادة محاولة عند فشل الاتصال/429/5xx بتراجع أسّي (يحترم Retry-After)
    "cache":   60,                                // ثوانٍ (GET فقط)
    "expect":  [200, 404],                        // رقم | مصفوفة | "2xx" | "any"
    "fallback": []                                // قيمة بديلة في data عند الفشل
});
print r.ok; print r.data; print r.attempts; print r.cached;
```
بقية الخيارات: `method`, `headers`, `body` (قاموس/مصفوفة ⇒ JSON تلقائياً), `timeout`.
النتيجة: `ok, status, statusText, data, json, body, headers, error, attempts, cached, ms, url`.
الافتراضي: `retries = 2` لـ GET و`0` لغيره (تجنباً لتكرار POST).

`make.api.download(url, path, opts?)` — تنزيل ثنائي سليم بايت ببايت: `overwrite` (true), `maxBytes`, `timeout`, `headers`, `auth`.
النتيجة: `ok, path, bytes, status, contentType`. `make.api.clearCache()` يفرّغ الكاش.

## make.image.removeBg(src, out?, opts?)
الإخراج `.png` أو `.webp` (شفافية). الطرق (`method`):
- `auto` (الافتراضي): `rembg` (ذكاء اصطناعي) إن وُجد، وإلا الملء الفيضي.
- `ai`: `rembg` حصراً. · `color`: الملء الفيضي حصراً.

الملء الفيضي يكتشف لون الخلفية من الزوايا الأربع ويزيل **المتصل بالحواف فقط** — فتبقى المناطق الداخلية بنفس اللون
(عين بيضاء، نص...). مناسب للخلفيات الموحّدة؛ للصور المعقدة ثبّت `rembg`.
الخيارات: `tolerance` (0–100، 12), `feather` (0–10، 1), `erode` (0–5), `trim` (قصّ الأطراف الشفافة),
`color` (لون الخلفية يدوياً), `bg` (تعبئة بلون بدل الشفافية، يسمح بـ `.jpg`), `out`.
النتيجة: `ok, path, method, background, tolerance, width, height, sourceWidth, sourceHeight, bytes, removedPercent`.
الصور تُعرَّف من **البايتات** لا الامتداد، فملف SVG/MVG متنكّر بامتداد صورة يُرفض.

## make.ocr(src, opts?) = make.image.text
```rin
let o = make.ocr("scan.png", {"lang": "ara+eng", "enhance": true, "words": true});
print o.text; print o.confidence; print o.lines;
```
الخيارات: `lang` (الافتراضي `ara+eng` المثبّتتان فعلاً)، `psm` (0–13، 3; استخدم 6 لكتلة و7 لسطر), `minConfidence`,
`words` (إحداثيات وثقة كل كلمة), `enhance` (تدرّج رمادي + تكبير + تحسين تباين قبل القراءة), `timeout`.
النتيجة: `ok, text, confidence, wordCount, lines[], lang, psm, enhanced[, words[]]`.
حزمة لغة غير مثبّتة ⇒ `ok:false` مع قائمة المتاح وأمر التثبيت.

## OCR على أندرويد (عربي + إنجليزي)
ML Kit **لا يدعم العربية** إطلاقاً (لاتيني/صيني/ديفاناغاري/ياباني/كوري فقط)، لذا العربية تمر عبر Tesseract
(`cz.adaptech.tesseract4android`). المحرّك يُختار تلقائياً (`engine`: `auto` | `tesseract` | `mlkit`):

| الطلب | المحرّك |
|---|---|
| بلا `lang` | `ara+eng` عبر Tesseract إن كان الملفان متاحين، وإلا ML Kit (لاتيني) مع `warning` يشرح كيف تفعّل العربية |
| `lang` فيه أي لغة غير `eng`/`latin` (مثل `ara`, `ara+eng`) | Tesseract |
| `lang: "eng"` أو `"latin"` | ML Kit (أسرع وبلا ملفات) |

**ملفات اللغة** (`<code>.traineddata`، نماذج tessdata_fast ~1–4MB للغة) تُبحث بالترتيب:
1. `filesDir/tessdata/` (نُسخت سابقاً)
2. `assets/tessdata/` داخل الـ APK — ضمّنها وقت البناء بـ `scripts/fetch_tessdata.sh` (يعمل بعدها بلا إنترنت)
3. تنزيل مرة واحدة من tessdata_fast إن مرّرت `download: true`

```rin
let o = make.ocr("receipt.png", {"lang": "ara+eng", "enhance": true, "download": true});
print o.text; print o.backend;   // android.tesseract
```
`make.media.tools().ocrLangs` على أندرويد يعرض ما هو متاح الآن (مثل `["ara","eng","latin"]`).
قيود: `minConfidence` و`words` لا يُطبَّقان على Tesseract في أندرويد (الثقة العامة فقط).

## ما يلزم تثبيته
| المنصة | المتطلبات |
|---|---|
| لينكس | `sudo apt install ffmpeg mpv imagemagick tesseract-ocr tesseract-ocr-ara` (+ `pip install rembg[cli]` اختياري) |
| macOS | `brew install ffmpeg mpv imagemagick tesseract tesseract-lang` |
| أندرويد | بلا تثبيت للفيديو والصوت وإزالة الخلفية (`RinMediaBridge.kt`). **OCR:** الإنجليزية عبر ML Kit بلا ملفات؛ **العربية** عبر Tesseract وتحتاج `ara.traineddata` + `eng.traineddata` (انظر «OCR على أندرويد») |
| ويندوز / WASM | غير مدعوم بعد: تُرجع الدوال `ok:false` برسالة واضحة |

## حدود معروفة
- `make.video`/`make.audio` يحتاجان شاشة/جهاز صوت؛ على خادم بلا `DISPLAY` يُرجع `make.video` خطأً واضحاً (استعمل `dryRun`).
- إزالة الخلفية بالملء الفيضي لا تفصل جسماً عن خلفية متدرجة/مزدحمة.
- كاش `make.api` في الذاكرة لعملية واحدة (حتى 200 مدخل).
- `httpGet` / `apiCall` / `net.*` القديمة لا تُعدّ قدرة `api` (حفاظاً على توافق الوحدات `strict` الحالية)؛ `make.api` وحدها تُعدّ.

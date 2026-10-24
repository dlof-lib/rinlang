# `RINTWIST` — متغيّر السياق المضمَّن

متغيّر **للقراءة فقط** يعرّف نفسه في كل برنامج Rin بلا استيراد ولا تعريف. كل قراءة تعطيك لقطة جديدة تصف
**الملف** الجاري تنفيذه الآن، و**الخادم** (الجهاز/العملية)، و**التاريخ**، و**التطبيق**، و**المكتبة** الجارية.

```rin
print RINTWIST.name;            // اسم الملف الجاري تنفيذه
print RINTWIST.date.iso;        // 2026-10-24T13:05:09+03:00
print RINTWIST.app.version;     // 1.0.0
print RINTWIST.server.host;     // اسم الجهاز
print RINTWIST.lib.name;        // مكتبة/توكن أداة جارٍ استيراده، أو "" في الملف الرئيسي
```

## الحقول

| الحقل | المحتوى |
|---|---|
| `name`, `path` | اسم/مسار الملف الجاري تنفيذه (اختصار لـ `file.name` / `file.path`) |
| `file` | `{path, name, stem, ext, dir, kind, part, main, depth}` — `kind`: `main` \| `library-part` \| `tool-token`؛ `part` = الجزء داخل المكتبة (`core/convert.zpp`)؛ `main` = السكربت الرئيسي؛ `depth` = عمق الاستيراد |
| `lib` | `{active, kind, name, version, root, part, loaded}` — `kind`: `library` (مجلد) \| `tool-token` (ملف واحد)؛ `active=false` وحقول فارغة في الملف الرئيسي؛ `loaded` = أسماء المكتبات المحمَّلة |
| `app` | `{name, version, platform, arch, mode, nativeAllowed}` — `platform`: `linux`/`android`/`windows`/`macos`/`wasm`؛ `mode`: `cli`/`android`/`wasm` |
| `server` | `{host, pid, os, cwd, root}` — الجهاز والعملية المضيفة؛ `root` = `basePath` |
| `date` | `{iso, utc, date, time, year, month, day, hour, minute, second, weekday, weekdayName, monthName, dayOfYear, unix, ms, tzMinutes}` — `iso` محلّي، `utc` بصيغة `…Z`؛ `weekday` 0 = الأحد |
| `run` | `{uptimeMs, startedUnixMs, args}` — عمر المفسّر ووسائط البرنامج |
| `env` | `{lang, tz}` — `LANG` و`TZ` فقط (لا تُكشف بقية متغيرات البيئة عمداً) |
| `stack` | مصفوفة `{depth, kind, name, version, path, part}` لسلسلة الاستيراد: العنصر 0 الملف الرئيسي، وآخر عنصر الحزمة الجارية |
| `app` (إضافة) | `debug` و`compiler` و`cppStd` إلى جانب الحقول السابقة |

## الدوال المساعدة

| الدالة | الوصف |
|---|---|
| `rintwist.get("lib.name", dflt)` | قراءة حقل بمسار منقَّط، تدعم فهارس المصفوفات (`"stack.1.name"`)، وتُرجع `dflt` (أو `nil`) إن لم يوجد |
| `rintwist.fmt(template, extra?)` | قالب نصي: `rintwist.fmt("{file.stem}-{date.date}.log")`. `{{` و`}}` أقواس حرفية. الوسيط الثاني (map) تتقدّم مفاتيحه على حقول RINTWIST |
| `rintwist.stack()` | سلسلة الاستيراد (نفس `RINTWIST.stack`) |

```rin
let logName = rintwist.fmt("{file.stem}-{date.date}.log");   // app-2026-10-24.log
print rintwist.fmt("{user}@{server.host} [{app.name} {app.version}]", {user: "jo"});
```

## متى تُقرأ القيمة؟
القيمة لقطة **وقت القراءة** وليست وقت تعريف الدالة. لذلك تعرف المكتبة نفسها من داخل دوالها بالتقاط السياق عند التحميل:

```rin
// داخل main.zpp لمكتبة
export let MY = RINTWIST;                 // يُلتقط أثناء الاستيراد: MY.lib.name, MY.file.part ...
export fun whoami() { return MY.lib.name + " " + MY.lib.version; }
```

## ملاحظات
- الإسناد `RINTWIST = ...` خطأ صريح (للقراءة فقط). انسخه بـ `let ctx = RINTWIST;` إن أردت تعديل نسخة.
- يمكن لبرنامج أن يعرّف متغيّراً محلياً بنفس الاسم؛ عندها يحجب المضمَّن (كأي متغيّر مضمَّن).
- لا كلفة على البرامج التي لا تستعمله: لا شيء يُبنى إلا عند القراءة.

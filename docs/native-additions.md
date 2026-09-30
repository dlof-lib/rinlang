# دوال Rin الأصلية الجديدة (Rin 1.0)

أُضيفت هذه الدوال في `app/src/main/cpp/rin_extra_natives2.cpp` (مُضمَّن بعد
`rin_extra_natives.cpp` في نهاية `rin_interpreter.cpp`، فيدخل تلقائياً في كل أهداف
البناء: APK/CLI/WASM/CI) دون تعديل أي ملف موجود. كلها مغطّاة باختبار فعلي:

```text
rin_run tests/verification/extra_natives2.rin
```

يطبع `ALL PASSED (extra_natives2)` عند النجاح، وأي فشل يوقف التشغيل برمز خروج `1`.

> ملاحظة تسمية: الكلمتان `file` و`pipe` محجوزتان في Rin (لبنية `@file` وتاريخياً في
> `container.pipe`)، فاستُخدم `fs.*` بدل `file.*` و`automation.chain` بدل
> `automation.pipe` تفادياً للتعارض مع المحلّل اللغوي.

## `sec.*` — أدوات أمن (تجزئة، ترميز، تحقق)

| الدالة | الوصف |
|---|---|
| `sec.sha256(s)` | بصمة SHA-256 (hex، 64 محرفاً) — تُعيد استخدام `clc::sha256` الموجودة أصلاً في المشروع |
| `sec.md5(s)` | بصمة MD5 (hex، 32 محرفاً) — توافقية/checksum فقط، ليست لأي استخدام أمني حديث |
| `sec.crc32(s)` | CRC-32 كرقم (للتحقق السريع من سلامة بيانات، ليست أمنية) |
| `sec.hmacSha256(key, msg)` | HMAC-SHA256 (hex) — للتحقق من أصالة رسالة بمفتاح مشترك |
| `sec.base64Encode(s)` / `sec.base64Decode(s)` | ترميز/فك Base64 القياسي |
| `sec.hexEncode(s)` / `sec.hexDecode(s)` | ترميز/فك Hex |
| `sec.randomToken(bytes?)` | رمز عشوائي (hex) بطول `bytes` بايت (افتراضي 16)، عبر `std::random_device` |
| `sec.constantTimeEqual(a, b)` | مقارنة نصّين بزمن ثابت (تفادي هجمات التوقيت عند مقارنة أسرار) |
| `sec.xorCipher(s, key)` | XOR بسيط قابل للعكس بنفس الاستدعاء — للتعتيم فقط، **ليس تشفيراً آمناً** |
| `sec.rot13(s)` | ROT13 الكلاسيكي |
| `sec.passwordStrength(s)` | `{score:0..4, length, hasUpper, hasLower, hasDigit, hasSymbol, feedback:[...]}` |

```rin
let token = sec.randomToken(16);
let sig = sec.hmacSha256("shared-secret", "payload");
assert(sec.constantTimeEqual(sig, expectedSig));
```

## `fs.*` — تحليل ملفات ومجلدات

كل المسارات تمرّ عبر `resolvePath()` نفسها المستخدمة في `readFile`/`writeFile`، فتبقى
معزولة داخل مجلد المشروع (`basePath`) بنفس سياسة بقية اللغة.

| الدالة | الوصف |
|---|---|
| `fs.hash(path, algo?)` | بصمة محتوى ملف كامل؛ `algo` من `"sha256"` (افتراضي) / `"md5"` / `"crc32"` |
| `fs.lineCount(path)` | عدد الأسطر، أو `nil` إن تعذّر فتح الملف |
| `fs.tail(path, n)` | آخر `n` سطراً كمصفوفة (نافذة دائرية، بلا تحميل الملف كاملاً في الذاكرة) |
| `fs.grep(path, pattern)` | `[{line, text}, ...]` لأسطر تطابق نمط regex (نفس صيغة `regexTest`، تدعم بادئة `i:`/`m:`) |
| `fs.scanDir(path, recursive?)` | `[{name, path, isDir, size, extension}, ...]` |
| `fs.extensionStats(path, recursive?)` | `{".ext": عدد, ...}` |
| `fs.find(path, namePattern, recursive?)` | مسارات الملفات التي يطابق **اسمها** نمط regex |

```rin
let bigLogs = fs.find("logs", "\\.log$", true);
for (let p of bigLogs) {
    let hits = fs.grep(p, "i:error");
    if (len(hits) > 0) print p + ": " + len(hits) + " error line(s)";
}
```

## `net.*` — الشبكة: تحليل + اتصال حقيقي بالإنترنت

مجموعتان: (أ) دوال تحليل بلا اتصال (هذا الجدول)، و(ب) دوال **تتصل فعلياً بالإنترنت** موثّقة أدناه
في [`net.*` — الاتصال الحقيقي](#net--الاتصال-الحقيقي-بالإنترنت). لا تُرمى أخطاء عند فشل الشبكة، بل تُرجَع نتيجة `ok: false`.

| الدالة | الوصف |
|---|---|
| `net.parseUrl(url)` | `{scheme, host, port, path, query, fragment}` أو `nil` إن كان الرابط غير صالح (يتطلب `scheme://`) |
| `net.parseQuery(qs)` | يفكّك `a=1&b=2` إلى قاموس (يدعم `%XX` و`+`) |
| `net.buildQuery(map)` | يبني نص query من قاموس |
| `net.isValidIPv4(s)` / `net.isValidIPv6(s)` | تحقق من صيغة العنوان |
| `net.ipToInt(ipv4)` / `net.intToIp(n)` | تحويل IPv4 ⇄ رقم 32-bit |
| `net.cidrContains(cidr, ip)` | هل عنوان IPv4 ضمن مدى CIDR (مثل `"10.0.0.0/8"`) |
| `net.isPrivateIp(ip)` | عناوين خاصة/loopback/link-local (RFC 1918 + `127.0.0.0/8` + `169.254.0.0/16`) |

```rin
let u = net.parseUrl("https://api.example.com:8443/v1/users?active=true");
print u.host;                              // api.example.com
print net.isPrivateIp("192.168.1.5");      // true
print net.cidrContains("10.0.0.0/8", "10.2.3.4"); // true
```

## `net.*` — الاتصال الحقيقي بالإنترنت

كل الدوال التالية تُجري **اتصالاً شبكياً فعلياً** (لا محاكاة). لا ترمي خطأً عند انقطاع الشبكة:
تُرجع `{ok: false, error: "..."}` (أو `nil` للدوال التي تُرجع قيمة واحدة). HTTP مقصور على
`http://` و`https://` (أي مخطط آخر مثل `file://` يُرفض).

### HTTP / HTTPS (يعمل على CLI وأندرويد عبر نفس عميل `httpGet`)
| الدالة | الوصف |
|---|---|
| `net.fetch(url, opts?)` | طلب كامل. `opts`: `{method, headers, body, timeout, retries}` (`retries` حتى 5). يُرجع `{ok, status, statusText, body, json, error, ms, size, headers, url}` |
| `net.get(url, headers?)` / `net.head(url, headers?)` | GET / HEAD |
| `net.post(url, body, headers?)` | POST (قاموس/مصفوفة تُرمَّز JSON تلقائياً) |
| `net.getJson(url, headers?)` / `net.postJson(url, body, headers?)` | القيمة المُحلَّلة مباشرة، أو `nil` عند الفشل/رد غير JSON/حالة غير 2xx |
| `net.headers(url)` | قاموس ترويسات الرد (مفاتيح بحروف صغيرة) أو `nil` |
| `net.status(url)` | رمز الحالة (0 عند الفشل) |
| `net.isUp(url)` | هل يستجيب الخادوم (أي رد بحالة < 500) |
| `net.measure(url, count=3)` | زمن استجابة فعلي: `{ok, failed, minMs, avgMs, medianMs, maxMs}` |
| `net.checkUrls([urls])` | فحص حتى 50 رابطاً: `[{url, ok, status, ms, error}]` |
| `net.download(url, path)` | تنزيل حقيقي إلى ملف → `{ok, bytes, path, status, ms, error}` |
| `net.publicIp()` | عنوانك العام كما يراه الإنترنت |
| `net.ipInfo(ip?)` | معلومات عنوان (بلد/مدينة/مزوّد) عبر ipinfo.io |
| `net.dnsQuery(name, type="A")` | سجلات DNS حقيقية عبر DNS-over-HTTPS (A, AAAA, MX, TXT, NS, CNAME, SOA, CAA, PTR, SRV) |
| `net.setTimeout(ms)` / `net.getTimeout()` | مهلة الطلبات (100–60000، افتراضي 10000) |

> `net.headers` وحقل `headers` في `net.fetch` يعملان على CLI (curl). على أندرويد يكون `headers` فارغاً
> لأن جسر JNI الحالي لا يُعيد ترويسات الرد.

### مقابس TCP / DNS / النظام (لينكس، macOS، أندرويد — تُرجع خطأً واضحاً على غيرها)
| الدالة | الوصف |
|---|---|
| `net.resolve(host)` | حلّ DNS محلي: `{ok, addresses, ipv4, ipv6, ms}` |
| `net.reverseDns(ip)` | اسم المضيف لعنوان، أو `nil` |
| `net.tcpCheck(host, port, timeoutMs=3000)` | اتصال TCP واحد إلى منفذ واحد: `{ok, ip, ms, error}` |
| `net.tcpPing(host, port=443, count=4, timeoutMs=2000)` | "ping" عبر TCP: `{sent, received, lossPct, minMs, avgMs, maxMs}` (ICMP يتطلب root) |
| `net.tcpSend(host, port, data, timeoutMs=5000)` | يرسل بايتات خاماً ويقرأ الرد (حتى 64KB) |
| `net.whois(domain)` | WHOIS حقيقي (IANA ثم خادوم السجل): `{ok, server, text}` |
| `net.isOnline()` | اتصال TCP فعلي بـ 1.1.1.1 / 8.8.8.8 / 9.9.9.9 |
| `net.localIPs()` / `net.hostname()` | واجهات الجهاز وعناوينها / اسم الجهاز |

### مساعدات بلا اتصال
`net.urlEncode/urlDecode`، `net.joinUrl(base, rel)`، `net.isValidUrl/isValidDomain/isValidEmail/isValidPort/isValidMac`،
`net.ipVersion(ip)` (4/6/nil)، `net.domainOf(url)`، `net.cidrInfo("192.168.1.0/24")`
(`{network, broadcast, netmask, first, last, prefix, total, hosts}`)، `net.httpStatusText(code)`،
`net.portService(443)` → `"https"` / `net.servicePort("ssh")` → `22`، `net.parseHeaders(text)`، `net.basicAuth(user, pass)`.

```rin
if (net.isOnline()) {
    let r = net.fetch("https://api.github.com/repos/torvalds/linux",
                      {headers: {"User-Agent": "rin"}, retries: 2});
    print r.status + " in " + r.ms + "ms";
    print r.json.stargazers_count;

    let dns = net.dnsQuery("example.com", "A");
    for (let rec of dns.answers) print rec.data;

    let chk = net.tcpCheck("example.com", 443);
    print chk.ok;
    net.download("https://example.com/", "example.html");
}
```

## `log.*` — تحليل سجلات (logs)

| الدالة | الوصف |
|---|---|
| `log.parseLines(text)` | يفكّك نصاً كاملاً إلى مصفوفة أسطر (يدعم `\r\n`) |
| `log.parseCommonLog(line)` | يفكّك سطر Apache/Nginx Common/Combined Log Format إلى `{ip, ident, user, time, method, path, protocol, status, size}`، أو `nil` إن لم يطابق |
| `log.extractIPs(text)` | كل عناوين IPv4 الفريدة الموجودة في نص، بترتيب ظهورها |
| `log.extractTimestamps(text, pattern?)` | طوابع زمنية مطابقة (افتراضياً صيغ ISO8601 و`[dd/Mon/yyyy:HH:MM:SS]` الشائعة، أو نمط مخصّص) |
| `log.countBy(lines, fn)` | `{مفتاح: عدد}` حيث `fn(line)` تُرجع المفتاح لكل سطر |
| `log.topN(countsMap, n)` | أعلى `n` أزواج `[مفتاح, عدد]` مرتّبة تنازلياً |
| `log.filterLines(lines, pattern)` | الأسطر المطابقة لنمط regex فقط |
| `log.levelCounts(lines)` | `{FATAL, ERROR, WARN, INFO, DEBUG, TRACE, OTHER}` — عدّ حسب أول كلمة مستوى موجودة (case-insensitive) |
| `log.detectSpikes(counts, factor?)` | فهارس عناصر مصفوفة أرقام تتجاوز `متوسط × factor` (افتراضي `2.0`) — كشف ارتفاعات بسيط |

```rin
let lines = log.parseLines(readFile("access.log"));
let byStatus = log.countBy(lines, fun(l) {
    let rec = log.parseCommonLog(l);
    return rec == nil ? "unparsed" : rec.status;
});
print log.topN(byStatus, 5);
```

## `automation.*` — أتمتة سير العمل

| الدالة | الوصف |
|---|---|
| `automation.retry(fn, times, delayMs?)` | يستدعي `fn()` حتى النجاح أو استنفاد `times` محاولة؛ يعيد رمي آخر خطأ عند الفشل الكامل؛ `delayMs` (سقف 60000) إيقاف فعلي بين المحاولات |
| `automation.chain(value, [fn1, fn2, ...])` | أنبوب دوال: `fnN(...fn2(fn1(value)))` |
| `automation.timeIt(fn)` | `{result, ms}` — قيمة `fn()` مع مدة التنفيذ بالميلي‑ثانية |
| `automation.batch(items, size)` | يقسّم مصفوفة إلى دفعات بحجم `size` |
| `automation.sequence([fn1, fn2, ...])` | يشغّل الدوال بالترتيب ويجمع نتائجها في مصفوفة؛ يتوقف عند أول خطأ |
| `automation.sleepMs(ms)` | إيقاف فعلي محدود السقف (60000 مللي‑ثانية كحد أقصى) |

```rin
let data = automation.retry(fun() { return fetchSomething(); }, 3, 500);
let pipeline = automation.chain(rawInput, [normalize, validate, save]);
```

## امتدادات `container.*`

فوق ما ورد في `native-additions.md` (Rin 1.1):

| الدالة | الوصف |
|---|---|
| `container.snapshot(name)` | نسخة عميقة من كل الحقول (بلا الدوال) كقاموس، أو `nil` إن غابت الحاوية |
| `container.restore(name, snap)` | يكتب حقول `snap` فوق الحاوية (لا يحذف حقولاً غائبة من `snap`)؛ `false` إن غابت |
| `container.lock(name)` / `container.unlock(name)` / `container.isLocked(name)` | علم قفل بسيط (حقل داخلي `__locked`) |
| `container.logEvent(name, msg)` | يضيف `{t, msg}` إلى سجل أحداث الحاوية (`__events`)؛ يُعيد الطول الجديد |
| `container.history(name)` | يُعيد مصفوفة أحداث `container.logEvent` (فارغة إن لا شيء) |
| `container.filter(kind, fn)` | أسماء الحاويات من `kind` حيث `fn(fieldsMap)` صحيحة |
| `container.stats(kind)` | `{count, fields: {field: {count, numeric, min?, max?, sum?, avg?}}}` — الحقول الداخلية (`__`) مُستبعدة |
| `container.validate(name, schema)` | `{valid, errors:[...]}`؛ `schema` قاموس `{field: "type"}`، أضف `؟` (`"number?"`) لحقل اختياري؛ الأنواع: `number/string/bool/array/map/function/any` |
| `container.exportToFile(name, path)` | يكتب حقول الحاوية JSON فعلياً على القرص (عبر `container.toJson` + الكتابة المعزولة) |
| `container.importFromFile(name, path, overwrite?)` | يقرأ JSON من القرص ويدمجه داخل حاوية موجودة (عبر `container.fromJson`) |

```rin
spawn("host", "srv1");
setField("srv1", "cpu", 10);
let before = container.snapshot("srv1");
container.incr("srv1", "cpu", 90);
container.restore("srv1", before);          // cpu = 10 من جديد

let check = container.validate("srv1", {cpu: "number", name: "string?"});
if (!check.valid) print check.errors;

container.exportToFile("srv1", "backups/srv1.json");
```

## دوال `container.*` الإضافية (Rin 1.3)

الدوال المُعدِّلة هنا تحترم `container.lock(name)`: تُرمى `E0035` عند الكتابة على حاوية مقفلة.

| المجموعة | الدوال |
|---|---|
| إدارة | `container.names(kind?)`، `container.remove(name)`، `container.removeAll(kind)` |
| نسخ/نقل حقول | `container.copyField(src, field, dst, as?)`، `container.moveField(src, field, dst)`، `container.mergeFrom(dst, src, overwrite=true)` |
| مسارات متداخلة | `container.getPath(name, "a.b.0.c", default?)`، `container.setPath(name, "a.b.0.c", value)` |
| قيم افتراضية/تعديل | `container.setDefault(name, field, v)`، `container.defaults(name, {..})`، `container.decr(name, field, by?)`، `container.toggle(name, field)` |
| مصفوفات | `container.push(name, field, v)`، `container.pop(name, field)`، `container.contains(name, field, x)` |
| استعلام | `container.search(text, kind?)`، `container.sortBy(kind, field, desc?)`، `container.top(kind, field, n)`، `container.paginate(kind, page, size)` |
| تجميع | `container.avg/min/max(kind, field)`، `container.distinct(kind, field)`، `container.countBy(kind, field)` |
| فحص | `container.equals(a, b)`، `container.checksum(name)` (SHA-256)، `container.requireFields(name, [..])`، `container.fieldsOfType(name, type)`، `container.summary()` |
| تصدير | `container.entries(name)`، `container.valuesOf(name)`، `container.toRows(kind, fields?)`، `container.toCsv(kind, fields?)`، `container.exportCsv(kind, path, fields?)` |

`toCsv/exportCsv` تُسبق النصوص التي تبدأ بـ `= + - @` بفاصلة عليا لمنع حقن الصيغ في Excel.

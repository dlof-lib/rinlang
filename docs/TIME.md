# TIME.md — مفهوم `time`: الزمن كقيمة من الدرجة الأولى

`time` دالة وفضاء أسماء معاً. **اللحظة** (Time) خريطة عادية فيها `epoch` (ميلي ثانية) و`offset` (دقائق)
وكل أجزائها جاهزة، فتُقرأ مباشرة: `t.year t.month t.day t.hour t.minute t.second t.ms t.weekday
t.weekdayName t.monthName t.dayOfYear t.week t.quarter t.date t.time t.tz t.iso`.

كل دالة تقبل أي شيء يشبه الزمن:

| المدخل | المعنى |
|---|---|
| لحظة | تُستعمل كما هي |
| رقم | ثوانٍ إن كان أقل من 1e11، وإلا ميلي ثانية (تلقائياً) |
| نص | ISO · `11/10/2026` (اليوم أولاً) · `11 أكتوبر 2026` · `October 11, 2026` · `14:30` · `today/tomorrow/yesterday/اليوم/غداً/أمس` · `in 3 days / بعد 3 أيام` · `2 hours ago / قبل ساعتين` · `next friday / الجمعة القادمة` · `tomorrow 14:30` (الأرقام العربية ٠-٩ مقبولة) |
| خريطة أجزاء | `{year: 2026, month: 1}` (الناقص يُملأ بالمنطقي: الشهر 1، اليوم 1، الوقت 00:00) |
| `nil` | الآن |

لا تُعدِّل أي دالة مدخلاتها؛ تُرجع قيمة جديدة. سوء الاستخدام يرمي خطأً صريحاً فيه الصيغ المقبولة،
أما `time.parse` فتُرجع `nil` و`time.valid` تُرجع `true/false` لفحص مدخل المستخدم.

## الدوال
```rin
time()                     time(x)                  time(2026, 10, 11, 14, 30)
time(x, "YYYY-MM-DD")      // نص منسّق مباشرة
time.now([zone]) time.utc([x]) time.of(y[,m,d,h,mi,s,ms][,{tz:"+03:00"}])
time.parse(text[, "DD/MM/YYYY HH:mm" | {tz, format}])   time.valid(text[, fmt])
time.unix([x])  time.ms([x])
```
- **التنسيق** `time.format(t[, pattern[, {lang, digits, tz}]])`: رموز `YYYY YY MMMM MMM MM M dddd ddd DD D HH H hh h mm m ss s SSS A a Z ZZ Q W X x d`، والنص الحرفي بين `[ ]`.
  قوالب جاهزة: `iso date time datetime short long date.long time12 rfc compact`.
- **الحساب** `time.add(t, 3, "days")` · `time.add(t, "1h30m")` · `time.add(t, {months: 1, days: 20})` · `time.sub(...)`.
  الأشهر والسنوات تقويمية وتثبّت اليوم عند آخر الشهر (31 يناير + شهر = 28 فبراير).
- **الفرق** `time.diff(a, b)` يعطي مدة (`.text .long .totalHours ...`)، و`time.diff(a, b, "days")` رقماً.
- **الحدود** `time.startOf(t, "week")` · `time.endOf(t, "month")` (وحدات: ms s m h d w M q y أو بالعربية).
- **المقارنة** `before after same(a,b,"day") between(t,a,b) min max is(t,"today|past|future|weekend|weekday|leap")`.
- **المدد** `time.span(3725000)` · `time.span("1h30m")` · `time.span({hours: 2})` · `time.span(90, "s")`.
- **القوائم** `time.range(a, b[, "1 month"])` (حتى 100000 عنصر).
- **الصياغة البشرية** `time.ago(t[, "ar" | {lang, digits, now}])` → «منذ 3 ساعات» / `in 2 days`، مع مثنّى العربية.
- **المناطق** `time.zone(t, "+03:00" | "Riyadh" | "Asia/Riyadh")` · `time.zones()`.
  أسماء المدن بلا توقيت صيفي مدمجة؛ أسماء IANA الأخرى تُقرأ من tzdata الجهاز عند توفّره، والإزاحة ثابتة عند لحظة التحويل.
- **الهجري** `time.hijri(t[, {lang, digits}])` · `time.fromHijri(y, m, d)` — تقويم حسابي (جدولي)، قد يختلف يوماً عن رؤية الهلال.
- **التقويم** `time.isLeap(y)` · `time.daysIn(y, m)`.
- **القياس** `time.sleep(ms | "2s")` (حد أقصى 10 دقائق) · `time.stopwatch()` · `time.elapsed(sw[, "s"])` · `time.measure(fun, ...args)` → `{result, ms, seconds, text}`.
- **الإعداد** `time.config({lang: "ar", digits: "arabic", weekStart: "sat", weekend: ["fri","sat"]})`. الافتراضي: `en`، أرقام لاتينية، الأسبوع يبدأ الاثنين، العطلة السبت والأحد.

## أمثلة
```rin
let t = time.parse("tomorrow 14:30");
print time(t, "dddd D MMMM YYYY, hh:mm A");
print time.ago(t, "ar");                       // بعد يوم
print time.hijri(t, {lang: "ar"}).text;
print time.diff(time.of(2026, 12, 31), time(), "days");
```

## التوافق
`time` اسم مدمج جديد، لكن أي `fun time()` أو `class time` أو `let time = {...}` يعرّفه المستخدم يغلب في نطاقه
(استثناء مقصود من قاعدة منع التسمية بدالة مدمجة)، فلا يتعطل كود موجود. `${#time}` في النصوص القالبية كما هي.

## داخل indsin (الواجهات)
قيمة أي خاصية في `@view` تُقيَّم بمفسّر Rin (docs/indsin_rin.md)، فكل دوال `time.*` النقية تعمل مباشرة:
```rin
let target = time.parse("2027-01-01");
@view.Text=Left  text="تبقّى " + time.diff(target, time(), "days") + " يوماً";  .end/view
```
- **ممنوع داخل الخصائص** (حارس النقاء): `time.sleep` و`time.measure` و`time.config`.
- **ساعة حيّة**: عرّف `warp clock = time.ms();` وستتحدث الخلية `clock` كل ثانية، فتُعاد تلقائياً كل خاصية تقرؤها:
```rin
warp clock = time.ms();
@view.Text=Now  text=time(clock, "HH:mm:ss");  .end/view
@view.Text=Left text=time.diff(target, clock, "s") + " ثانية";  .end/view
```
  على المضيف أن يستدعي `rin_indsin_session_tick` كل ثانية ما دام الغلاف فيه `"ticking":true`
  (أندرويد: `IndsinPreviewActivity` يفعل ذلك). الاختبار: `tests/tools/test_indsin_time.cpp`، المثال: `examples/samples/indsin_time_demo.rin`.
- القيمة التي تُقرأ من `time()` مباشرة داخل خاصية تُحسب لحظة البناء ولا تتجدد وحدها؛ استعمل `clock` للتحديث الحيّ.

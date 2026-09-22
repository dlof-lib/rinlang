# rinflow.md — RinFlow: طبقة تنفيذ التدفّق المهيكل

RinFlow طبقة **اختيارية** فوق عامل الأنابيب `|>` (انظر [`pipelines.md`](./pipelines.md))
— لا تغيّر نتيجة أي حساب، بل تضيف تتبّعًا خطوة بخطوة (رسم بياني Graph)، إلغاءً،
مهلة زمنية، وإعادة تشغيل (replay)، لأي برنامج يحتوي سلسلة `|>`. هي **واجهة
استضافة (host API)** بلغة C++ (`namespace rin::flow` في `rin_interpreter.h`)، وليست
بنية جديدة في لغة Rin نفسها — أي كود `.rin` عادي يعمل تحتها بلا أي تعديل.

## التشغيل الأساسي
```cpp
Interpreter interp;
auto res = interp.runProgramAsFlow(program, flow::FlowRunOptions{},
    [&](const flow::FlowEvent& e) { /* اختياري: رد نداء لحظي لكل حدث */ });
```
عوضًا عن `interp.run(program)` العادية. النتيجة `FlowSessionResult` تحمل:
- `status` — حالة الجلسة الكلية.
- `output` — نفس نص المخرجات (`print`/`show`) كالتشغيل العادي تمامًا.
- `graph` — الرسم البياني: عقدة (`FlowNode`) لكل حلقة `|>` في البرنامج.
- `metrics` — عدد العقد الكلي/الناجح/الفاشل.

## مثال حقيقي (من `flow_selftest.cpp`، تحقّقتُ من تشغيله فعليًا)
```rin
fun isAdult(x) { return x > 18; }
fun myDouble(x) { return x * 2; }
let data = [10, 20, 30, 5, 40];
let result = data |> filter(isAdult) |> map(myDouble) |> sum;
print result;
```
تشغيله عبر `runProgramAsFlow` ينتج:
```
status=SUCCESS events=18
output: 180
  [0] INPUT 'input' SUCCESS 0ms out=[10, 20, 30, 5, 40]
  [1] FILTER 'filter' SUCCESS 1ms in=[10, 20, 30, 5, 40] out=[20, 30, 40]
  [2] MAP 'map' SUCCESS 0ms in=[20, 30, 40] out=[40, 60, 80]
  [3] REDUCE 'sum' SUCCESS 0ms in=[40, 60, 80] out=180
metrics: total=4 ok=4 failed=0
```
لاحظ أن نوع كل عقدة (`FILTER`/`MAP`/`REDUCE`) استُنتج تلقائيًا من **اسم الدالة**
نفسها (مطابقة بأسماء معروفة مثل `filter`/`map`/`sum`/`mean`) لأغراض العرض فقط —
لا يغيّر التنفيذ الفعلي بأي شكل؛ دالة باسم غير معروف تُصنَّف `CUSTOM`.

## الأخطاء
خطأ حقيقي (`RinError`) في أي مرحلة يُسجَّل على عقدتها كـ`NodeError` (كود مثل
`RIN-F-E0021`، مبني من كود اللغة `E0021` الحقيقي) بدل إيقاف الجلسة بلا تفصيل:
```
status=ERROR
  [1] OUTPUT 'boom' ERROR 0ms in=[1, 2, 3] out= ERROR(RIN-F-E0021): [E0021] index out of range: 100
```

## الإلغاء والمهلة الزمنية
`FlowRunOptions.timeoutMs` (`<= 0` = بلا مهلة) يوقف الجلسة بحالة `TIMEOUT` إن
تجاوزت المدة المحدَّدة. `RinFlowEngine::requestCancel(sessionId)` يطلب إلغاءً
تعاونيًا (يُفحَص قبل كل عقدة تالية، لا يقطع عقدة قيد التنفيذ في تلك اللحظة نفسها
— التنفيذ متزامن أحادي الخيط):
```
status=CANCELLED
  [0] INPUT 'input' SUCCESS 0ms out=1
  [1] CUSTOM 'a' CANCELLED 0ms
  [2] CUSTOM 'b' SKIPPED 0ms
  [3] CUSTOM 'c' SKIPPED 0ms
```

## الجلسات وإعادة التشغيل (Replay)
`RinFlowEngine` يدير كل الجلسات النشطة/المنتهية لمفسِّر واحد (يبقي فقط آخر عدد
محدود منها لتفادي تراكم غير محدود بالذاكرة). كل جلسة لها معرِّف فريد
(`flow-<timestamp>-<n>`)؛ يمكن إعادة تشغيل جلسة سابقة بمعرِّف جديد
(`flow-replay-<timestamp>-<n>`) لإعادة إنتاج نفس النتيجة بلا إعادة تحليل البرنامج.

## جدول القيم الكاملة
| التعداد | القيم |
|---|---|
| `NodeType` | `INPUT`, `OUTPUT`, `FILTER`, `MAP`, `TRANSFORM`, `SORT`, `REDUCE`, `CONTAINER`, `FILE`, `NETWORK`, `PIPELINE`, `CUSTOM` |
| `NodeStatus` | `QUEUED`, `RUNNING`, `SUCCESS`, `ERROR`, `SKIPPED`, `CANCELLED`, `TIMEOUT` |
| `SessionStatus` | `RUNNING`, `SUCCESS`, `ERROR`, `CANCELLED`, `TIMEOUT` |
| `EventType` | `FLOW_STARTED`, `NODE_QUEUED`, `NODE_STARTED`, `NODE_OUTPUT`, `NODE_FINISHED`, `NODE_ERROR`, `FLOW_FINISHED`, `FLOW_CANCELLED`, `FLOW_TIMEOUT` |

## انظر أيضًا
- [`pipelines.md`](./pipelines.md) — عامل `|>` الذي يبني RinFlow رسمه البياني فوقه.
- [`RECKON.md`](./RECKON.md) — `reckon` يُنتج نفس نوع العقد (`REDUCE` لدوال مثل `mean`) عند تشغيله عبر Flow.
- `app/src/main/cpp/rin_interpreter.h` (`namespace flow`) — التعريفات الكاملة للأنواع/الهياكل.
- `app/src/main/cpp/flow_selftest.cpp` — سادة اختبار قائمة بذاتها لتجربة كل هذا فعليًا.

# تطوير الخوارزميات والحساب العددي — `algo*`

23 دالة أصلية (`rin_extra_natives11.cpp`) تغطي ما يحتاجه المهندس حول خوارزميته: حلّ وتحسين، تفاضل وتكامل، معادلات تفاضلية، استيفاء وتوفيق، إشارات، وأدوات **قياس وتحقق** لخوارزميتك أنت.

## كيف تتكامل مع الموجود
- `algoPolyFit` و`algoNewton` و`algoPolyRoots` تقوم على محرّك الجبر الخطي نفسه (SVD، LU، القيم الذاتية في [`LINALG.md`](./LINALG.md)) — لا نسخة ثانية.
- `algoCheck` يقارن بدلالة `numberApprox`؛ `algoTrapz` يجمع بـ Neumaier كـ `numberSum`.
- الدوال الممرَّرة (`f`, `fn`, `gen`) دوال Rin عادية أو lambdas أو كائنات `__call__` (نفس مسار `map/filter`). الدوال الأصلية (مثل `numberSum`) لا تُمرَّر بالاسم؛ لُفّها: `fun(a) { return numberSum(a); }`.
- الأخطاء بنظام التشخيص وقابلة للالتقاط؛ مع `try/finally` و`defer` (ضمانات النطاق) يمكن تنظيف الموارد حول القياس.

## الجذور والتحسين
| الدالة | الوصف |
|---|---|
| `algoRoot(f, a, b[,{method:"brent"\|"bisect", tol, maxIter}])` | جذر في قوس يغيّر الإشارة (Brent افتراضياً). بلا تغيّر إشارة ← خطأ يقترح `algoBracket` |
| `algoRoot(f, x0[,{method:"newton"\|"secant", df}])` | من نقطة؛ `df` مشتقة تحليلية اختيارية (وإلا عددية) |
| `algoBracket(f, x0[,{step,grow}])` | يوسّع حول `x0` حتى تتغيّر الإشارة ← `{a,b,fa,fb}` |
| `algoMinimize(f, a, b)` | متغيّر واحد (Brent) |
| `algoMinimize(f, [x0...][,{tol,maxIter,step}])` | عدة متغيّرات (Nelder–Mead)؛ `f` تستقبل مصفوفة |
| `algoMinimize(f, [x0...],{method:"bfgs", grad, gtol})` | شبه نيوتن BFGS مع بحث خطي؛ `grad` اختيارية (وإلا تدرّج عددي)، وتقلّل التقييمات كثيراً |
| `algoNewton(F, x0[,{jacobian,tol,maxIter}])` | جملة `F(x)=0`: جاكوبيان عددي + LU + بحث خطي (backtracking) |

## التفاضل والتكامل
`algoDiff(f, x[,{order:1\|2, h, info}])` (ريدرز: استكمال ريتشاردسون مع تقدير الخطأ) · `algoGrad(f, xVec)` · `algoJacobian(F, xVec)` · `algoIntegrate(f, a, b[,{tol,limit}])` (Gauss–Kronrod 7/15 تكيّفي، يدعم حدوداً لانهائية، يُرجع `{value, error, evaluations, converged}`) · `algoTrapz(ys[, xs\|dx])`.

اللانهاية في Rin: `number("inf", {special:true})`.

## المعادلات التفاضلية
`algoOde(f, y0, t0, t1[,{method:"rk45"\|"rk4"\|"stiff", rtol, atol, h, steps, maxSteps}])` حيث `f(t, y)`؛ `y` رقم أو متجه، والناتج `{t, y, steps, rejected, evaluations}`. الافتراضي Dormand–Prince 5(4) تكيّفي؛ `rk4` بخطوات ثابتة. `stiff` للمسائل الصلبة (Euler ضمني تكيّفي بمضاعفة الخطوة + استكمال محلي، جاكوبيان عددي + LU): على `y'=-1000y+3000-2000e^{-t}` يحتاج عشرات الخطوات بينما RK45 يحتاج مئات. يدعم التكامل للخلف (`t1 < t0`).

## الاستيفاء والتوفيق والإشارات
`algoInterp(xs, ys, x[,{method:"linear"\|"spline"\|"nearest", extrapolate}])` · `algoPolyFit(xs, ys, deg)` ← `{coeffs (صعوداً), r2, rmse, rank}` · `algoPolyEval(c, x)` · `algoPolyRoots(c)` ← `[{re,im}]` · `algoFFT(signal[,{inverse}])` ← `{re, im}` لأي طول (Radix-2 + Bluestein) · `algoRandom(n,{seed,min,max,int,sorted,unique})` (حتمية).

## القياس والتحقق (لخوارزميتك)
```kotlin
fun mySort(a) { /* ... */ return a; }
fun refSort(a) { return sort(a); }   // أي مرجع تثق به

// 1) صحة: اختبار تفاضلي على مدخلات عشوائية
let inputs = [];
for i in 1 to 50 { push(inputs, algoRandom(i, {seed: i, int: true, max: 20})); }
print algoCheck(mySort, refSort, inputs);      // {ok, tested, failed, failures:[{index,input,got,expected}]}

// 2) سرعة
print algoBench(mySort, {args: [algoRandom(1000, {seed: 1})], runs: 20});

// 3) رتبة النمو تجريبياً
let c = algoComplexity(mySort, [500, 1000, 2000, 4000, 8000]);
print c.best, c.exponent;                      // مثلاً O(n^2)  1.97

// 4) مقارنة
print algoCompare({mine: mySort, ref: refSort}, {args: [algoRandom(2000, {seed: 2})]});
```
- `algoBench` ← `{runs, meanMs, minMs, maxMs, medianMs, stdevMs, totalMs}`؛ `warmup` افتراضياً 1.
- `algoComplexity(fn, sizes[,{gen: fn(n), runs, seed}])`: يقيس **أقل** زمن لكل حجم، ويُرجع `exponent` (ميل log-log) و`best` (أبسط نموذج متّسق مع القياس ضمن ضجيج ≈ 8%: `O(1) O(log n) O(n) O(n log n) O(n^2) O(n^3)`) و`candidates` و`fits`. تقدير إرشادي: استعمل 4–6 أحجام تتضاعف، وأحجاماً كبيرة بما يكفي ليتجاوز الزمن ضجيج المؤقّت. `gen` يولّد مدخلاً مخصصاً (الافتراضي مصفوفة أرقام عشوائية)، وتُمرَّر `fn` نسخة جديدة في كل تشغيل.
- `algoCheck(fn, ref, inputs[,{spread, rel, abs, maxFailures}])`: كل عنصر في `inputs` وسيط واحد (أو قائمة وسائط مع `spread:true`). إن رمى الاثنان خطأ يُعدّان متطابقين؛ وإن رمى واحد فقط فهو فشل.

## المخططات (على مصفوفة الأوزان)
`algoGraphMatrix(edges, n[,{directed}])` ← مصفوفة أوزان (القطر 0، غياب الحافة `Infinity`) · `algoDijkstra(W, src[, dst])` ← `{dist, prev, path}` (يرفض الأوزان السالبة) · `algoFloyd(W)` ← `{dist, negativeCycle}`. المصفوفة نفسها تُغذّي `matPowerIter` (PageRank) و`matEigSym` (طيف لابلاس).

## ملاحظات
- كل التحسين والجذور تُرجع `converged` ولا ترمي عند عدم التقارب (إلا الحالات المستحيلة: مشتقة صفرية، جاكوبيان منفرد، تباعد، تجاوز `maxSteps`).
- الاختبار: `tests/verification/linalg_algo.rin`.

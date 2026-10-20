# الجبر الخطي للمهندسين — `mat*` و`vec*`

53 دالة أصلية (C++) فوق **المصفوفة العادية في Rin** (مصفوفة صفوف، والمتجه مصفوفة أرقام). لا نوع جديد، فتعمل مباشرةً مع `lib/matrix.og.rin` (`mat_*`) و`lib/math.og.rin` (`matrixFrom`, `solveLinear`) و`lib/rinml.og.rin` (`nd_*`) دون أي تحويل. المصدر: `rin_extra_natives10.cpp`.

## كيف تتكامل مع ما هو موجود
| الموجود | كيف تعيد الدوال الجديدة استعماله |
|---|---|
| `numberSum` (جمع Neumaier) | `matTrace`، `vecSum`، `vecNorm(p)` تجمع بنفس الخوارزمية — لا تراكم أخطاء |
| `numberApprox` (rel=1e-9, abs=1e-12) | `matApprox` يطبّق نفس الدلالة على كل عنصر |
| `numberFormat` | `matFormat` يستدعيه لكل خلية: `matFormat(M, {decimals: 3, group: ","})` |
| نظام التشخيص `E0004/E0007/E0035` | كل الأخطاء قابلة للالتقاط بـ `try/catch` وتذكر الدالة والصف/البُعد |
| `lib/matrix.og.rin` | `mat_mul` تبقى كما هي (بطيئة وصغيرة)؛ `matMul` أصلية وتقبل متجهات وسلسلة |

## الإنشاء والفحص
`matZeros(r[,c])` · `matOnes` · `matEye(n[,m])` · `matDiag(v|M)` · `matBuild(r,c,fn(i,j))` · `matRandom(r,c,{seed,min,max,normal,mean,std,int})` (حتمية بـ seed على كل المنصّات) · `matHilbert(n)` · `matVandermonde(v[,cols])` · `matShape(M)` · `matCheck(M)` (بلا استثناء) · `matIs(M, "symmetric"|"orthogonal"|"diagonal"|"identity"|"upper"|"lower"|"positive-definite"|"singular"|"square"|"zero"[,tol])`.

## العمليات
`matAdd` `matSub` `matHadamard` `matScale` `matTranspose` `matTrace` `matKron` `matMap(M, fn(x,i,j))` · `matPow(M,k)` (سالبة عبر المعكوس) · `matNorm(M, 1|2|"inf"|"fro"|"max"|"nuc")` · `matApprox` · `matFormat`.

`matMul(A, B, ...)`: مصفوفة×مصفوفة، مصفوفة×متجه ← متجه، متجه×مصفوفة ← متجه. متجه×متجه يُرفض برسالة تدلّ على `vecDot`.

## التحليلات
| الدالة | الناتج |
|---|---|
| `matLU(M)` | `{L, U, P, perm, sign, singular}` حيث `P·M = L·U` |
| `matQR(M[,{reduced}])` | `{Q, R}` (Householder) |
| `matCholesky(M)` | `L` حيث `M = L·Lᵀ` (خطأ واضح إن لم تكن SPD) |
| `matEigSym(M)` | `{values (تنازلياً), vectors (أعمدة)}` — Jacobi |
| `matEigvals(M[,{real}])` | قيم ذاتية لمصفوفة حقيقية عامة: `[{re,im}]` (توازن + Hessenberg + QR مُزاح) |
| `matSVD(M)` | `{U, S, V, rank}` حيث `M = U·diag(S)·Vᵀ` |
| `matRank` · `matCond(M[,p])` · `matDet` · `matInv` | رتبة، شرطية (`Infinity` للمنفردة)، محدّد، معكوس |
| `matPinv(M)` · `matNull(M)` · `matRref(M)` | مور–بنروز، فضاء الصفر، الصيغة الدرجية المختزلة |

## الحل
- `matSolve(A, b[,{info:true}])`: مربعة ← LU بتدوير جزئي (منفردة ← خطأ يقترح `matLstsq`)، غير مربعة ← أقل المربعات. `b` متجه أو مصفوفة.
- `matLstsq(A, b)` ← `{x, residual, rank}` (أصغر نظيم عند نقص الرتبة).
- `matSolveIter(A, b, {method: "cg"|"jacobi"|"gauss-seidel"|"sor", tol, maxIter, omega, x0})` ← `{x, iterations, residual, converged}`.
- `matPowerIter(M)` ← القيمة الذاتية المهيمنة.

## المتجهات
`vecDot` (مُعوَّض الدقة Dot2: `vecDot([1e16,1,-1e16],[1,1,1]) == 1`) · `vecNorm(v[,p])` (بلا فيض: `vecNorm([3e200,4e200]) == 5e200`) · `vecCross` · `vecNormalize` · `vecAngle` (مستقرّة) · `vecProject` · `vecAdd` `vecSub` `vecScale` · `vecDist` · `vecLinspace` · `vecSum`.

## مثال
```rin
let A = [[4, 1, 0], [1, 3, 1], [0, 1, 2]];
let b = [1, 2, 3];
let x = matSolve(A, b);
print matFormat([x], {decimals: 4});                  // [ 0.2222  0.1111  1.4444 ]
print matApprox(matMul(A, x), b);                      // true
print matCond(A);                                      // رقم الشرطية
let e = matEigSym(A);  print e.values;                 // تنازلياً
let s = matSVD(A);     print matRank(A);
```

## الدقة والحدود
- الأعمدة/الصفوف تبدأ من 0. المصفوفات كثيفة؛ الكلفة O(n³) لمعظم التحليلات (`matSVD` بـ Jacobi أبطأ لكنه دقيق جداً للقيم الصغيرة).
- منفردة تعني ركيزة ≤ `n·eps·max|A|`. الرتبة/الشرطية/`pinv` تستعمل عتبة `max(m,n)·σ₁·eps` أو `tol` الممرَّر.
- اتفاقيات الإشارة: أكبر مركّبة في كل متجه ذاتي موجبة؛ قطر `R` في QR قد يكون سالباً.
- `matEigvals` للقيم فقط (المتجهات الذاتية لغير المتناظرة غير مُضمَّنة).
- الاختبار: `tests/verification/linalg_algo.rin` (تحقّق من القيم مقابل NumPy، وخصائص `P·A=L·U` و`Q·R=A` و`A·v=λ·v` و`U·S·Vᵀ=A` على مصفوفات عشوائية طويلة وعريضة ومربعة).

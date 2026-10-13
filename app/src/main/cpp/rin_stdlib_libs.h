#pragma once
// المكتبات القياسية المدمجة (embedded) القابلة للاستيراد عبر @import "lib/..."
// تُولَّد هذه الثوابت من ملفات lib/*.rin الحقيقية الموجودة في جذر المشروع (نُسخة طبق الأصل)،
// وتُضمَّن هنا مباشرة داخل ثنائي المفسّر حتى يعمل @import فوراً على أي منصة (بما فيها أندرويد)
// دون الحاجة لنسخ ملفات .rin إضافية إلى تخزين التطبيق. يمكن للمستخدم أيضاً استبدال أي منها
// بوضع ملف بنفس المسار فعلياً على القرص (basePath) لن يُستخدم لأن سجل embedded يُفحص أولاً —
// أو استيراد مسار مختلف تماماً لملفه الخاص، فيُقرأ حينها من القرص كالمعتاد.
#include <string>
#include <unordered_map>

namespace rin {

static const char* kLib_math_og_rin = R"MATHOGRIN(
// ============================================================================
//  lib/math.og.rin — مكتبة رياضية احترافية متكاملة فوق stdlib الأساسية
//  (stdlib الأساسية توفر: abs/sqrt/pow/floor/ceil/round/min/max/random/len/
//   sum/mean/median/mode/variance/stddev/geometricMean/harmonicMean/rms/
//   percentile/iqr/weightedMean/zscore/range/clamp(مصفوفة)/normalize/scale
//   minOf/maxOf/count/product/PI — هذا الملف لا يكرّرها بل يبني فوقها)
//
//  الأقسام:
//    1) ثوابت
//    2) نظرية أعداد وتوافيقيات (number theory & combinatorics)
//    3) دوال مساعدة عامة (utility)
//    4) مثلثات (trigonometry) — sin/cos/tan وما يتفرّع عنها، بلا دعم فطري
//       من المفسّر، لذا مبنية هنا بسلاسل تايلور مع اختزال المجال (range
//       reduction) لدقة كاملة عملياً على مدى double
//    5) أسّية ولوغاريتمات (exp/ln) — نفس المبدأ (سلاسل + اختزال مجال)
//    6) دوال زائدية (hyperbolic)
//    7) متجهات ثنائية/ثلاثية الأبعاد (vec2/vec3) كمصفوفات [x,y]/[x,y,z]
//    8) دوال Easing (لمنحنيات الحركة في واجهات Indsin أو محرك الألعاب)
//    9) إحصاء إضافي (عيّنة/تباين مشترك/ارتباط بيرسون)
//    10) عشوائية مساعدة (فوق random() الفطرية)
//
//  استيراد:
//    @import "lib/math.og.rin";              // دمج مباشر في النطاق الحالي
//    @import "lib/math.og.rin" as mathx;      // كحاوية باسم مستعار
// ============================================================================

// ---------------------------------------------------------------------------
// 1) ثوابت
// ---------------------------------------------------------------------------
let E       = 2.71828182845904523536;   // أساس اللوغاريتم الطبيعي
let TAU     = 6.28318530717958647692;   // 2*PI
let PHI     = 1.61803398874989484820;   // النسبة الذهبية
let SQRT2   = 1.41421356237309504880;
let SQRT3   = 1.73205080756887729353;
let LN2     = 0.69314718055994530942;
let LN10    = 2.30258509299404568402;
let EPSILON = 0.0000001;                // فرق افتراضي لمقارنة الأعداد العشرية

// ---------------------------------------------------------------------------
// 2) نظرية أعداد وتوافيقيات
// ---------------------------------------------------------------------------

// n! — المضروب (0! = 1)
fun factorial(n) {
    if (n < 0) { print "factorial: n يجب أن يكون >= 0"; return nil; }
    if (n < 2) { return 1; }
    return n * factorial(n - 1);
}

// القاسم المشترك الأكبر (خوارزمية إقليدس)
fun gcd(a, b) {
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) {
        let t = b;
        b = a % b;
        a = t;
    }
    return a;
}

// المضاعف المشترك الأصغر
fun lcm(a, b) {
    let g = gcd(a, b);
    if (g == 0) { return 0; }
    let result = (a / g) * b;
    if (result < 0) { result = -result; }
    return result;
}

// خوارزمية إقليدس الموسّعة: تُعيد [g, x, y] بحيث a*x + b*y = g = gcd(a,b)
fun extendedGcd(a, b) {
    if (b == 0) { return [a, 1, 0]; }
    let r = extendedGcd(b, a % b);
    let g = r[0];
    let x1 = r[1];
    let y1 = r[2];
    return [g, y1, x1 - floor(a / b) * y1];
}

// هل a و b أوّليان فيما بينهما (coprime)؟
fun isCoprime(a, b) {
    return gcd(a, b) == 1;
}

// هل n عدد أوّلي؟
fun isPrime(n) {
    if (n < 2) { return false; }
    if (n < 4) { return true; }
    if (n % 2 == 0) { return false; }
    let i = 3;
    while (i * i <= n) {
        if (n % i == 0) { return false; }
        i = i + 2;
    }
    return true;
}

// أصغر عدد أوّلي أكبر تماماً من n
fun nextPrime(n) {
    let v = floor(n) + 1;
    while (!isPrime(v)) {
        v = v + 1;
    }
    return v;
}

// تحليل n إلى عوامله الأوّلية (مصفوفة مرتّبة تصاعدياً، مع التكرار)
fun primeFactors(n) {
    let result = [];
    let v = n;
    if (v < 0) { v = -v; }
    let d = 2;
    while (d * d <= v) {
        while (v % d == 0) {
            push(result, d);
            v = v / d;
        }
        d = d + 1;
    }
    if (v > 1) { push(result, v); }
    return result;
}

// كل قواسم |n| الموجبة (مرتّبة تصاعدياً)
fun divisors(n) {
    let result = [];
    let v = n;
    if (v < 0) { v = -v; }
    if (v == 0) { return result; }
    let i = 1;
    while (i * i <= v) {
        if (v % i == 0) {
            push(result, i);
            let other = v / i;
            if (other != i) { push(result, other); }
        }
        i = i + 1;
    }
    return sort(result);
}

// هل n مربّع كامل؟
fun isPerfectSquare(n) {
    if (n < 0) { return false; }
    let r = round(sqrt(n));
    return r * r == n;
}

// هل n مكعّب كامل؟
fun isPerfectCube(n) {
    let v = n;
    if (v < 0) { v = -v; }
    let r = round(pow(v, 1 / 3));
    let cubed = r * r * r;
    if (n < 0) { return -cubed == n; }
    return cubed == n;
}

// أُس معياري: (base^exp) mod m — بدون تجاوز سعة الأعداد الكبيرة
fun modPow(base, exp, m) {
    if (m == 1) { return 0; }
    let result = 1;
    let b = base % m;
    if (b < 0) { b = b + m; }
    let e = exp;
    while (e > 0) {
        if (e % 2 == 1) {
            result = (result * b) % m;
        }
        e = floor(e / 2);
        b = (b * b) % m;
    }
    return result;
}

// مجموع أرقام |n| (بالنظام العشري)
fun digitSum(n) {
    let v = n;
    if (v < 0) { v = -v; }
    v = floor(v);
    let total = 0;
    while (v > 0) {
        total = total + (v % 10);
        v = floor(v / 10);
    }
    return total;
}

// عكس ترتيب أرقام n (يحافظ على الإشارة)
fun reverseDigits(n) {
    let neg = n < 0;
    let v = n;
    if (neg) { v = -v; }
    v = floor(v);
    let result = 0;
    while (v > 0) {
        result = result * 10 + (v % 10);
        v = floor(v / 10);
    }
    if (neg) { return -result; }
    return result;
}

// هل n (غير سالب) مطابق لنفسه عند عكس أرقامه؟
fun isPalindromeNumber(n) {
    if (n < 0) { return false; }
    return n == reverseDigits(n);
}

// الحد رقم n من متتالية فيبوناتشي (تكراري، بلا استدعاء ذاتي بطيء)
fun fibonacci(n) {
    let a = 0;
    let b = 1;
    let i = 0;
    while (i < n) {
        let next = a + b;
        a = b;
        b = next;
        i = i + 1;
    }
    return a;
}

// عدد التبديلات (permutations) لاختيار r من أصل n مرتَّبة: nPr
fun permutationsCount(n, r) {
    if (r < 0 or r > n) { return 0; }
    let result = 1;
    let i = 0;
    while (i < r) {
        result = result * (n - i);
        i = i + 1;
    }
    return result;
}

// عدد التوافيق (combinations) لاختيار r من أصل n: nCr
fun combinationsCount(n, r) {
    if (r < 0 or r > n) { return 0; }
    if (r > n - r) { r = n - r; }
    let result = 1;
    let i = 0;
    while (i < r) {
        result = (result * (n - i)) / (i + 1);
        i = i + 1;
    }
    return round(result);
}

// مرادف عرفي لـ combinationsCount
fun binomialCoefficient(n, r) {
    return combinationsCount(n, r);
}

// عدد كاتالان رقم n (0-indexed)
fun catalanNumber(n) {
    return combinationsCount(2 * n, n) / (n + 1);
}

// ---------------------------------------------------------------------------
// 3) دوال مساعدة عامة
// ---------------------------------------------------------------------------

// يحصر x بين lo و hi (نسخة عددية؛ clamp الفطرية تعمل على مصفوفة كاملة)
fun clampNum(x, lo, hi) {
    if (x < lo) { return lo; }
    if (x > hi) { return hi; }
    return x;
}

// ملاحظة: lerp(a, b, t) بات الآن دالة أساسية مدمجة في المفسّر (RMF §6/§28) متاحة دوماً بلا حتى
// @import "math" — أُزيل تعريفها المكرَّر من هنا (كان بنفس الصيغة بالضبط: a + (b - a) * t).

// عكس lerp: عند أي نسبة t تقع القيمة v بين a و b؟
fun invLerp(a, b, v) {
    if (a == b) { return 0; }
    return (v - a) / (b - a);
}

// يعيد ترسيم value من مجال [inMin, inMax] إلى مجال [outMin, outMax]
fun remap(value, inMin, inMax, outMin, outMax) {
    return lerp(outMin, outMax, invLerp(inMin, inMax, value));
}

// يلفّ x ضمن المجال [lo, hi) (مفيد لتدوير القيم الدورية كالزوايا)
fun wrap(x, lo, hi) {
    let span = hi - lo;
    if (span == 0) { return lo; }
    let v = x - lo;
    v = v - span * floor(v / span);
    return lo + v;
}

// يلفّ زاوية بالراديان إلى المجال (-PI, PI]
fun wrapAngle(angle) {
    return wrap(angle, -PI, PI);
}

// يحرّك current نحو target بخطوة أقصاها maxDelta (لا يتجاوز target)
fun moveToward(current, target, maxDelta) {
    let diff = target - current;
    if (abs(diff) <= maxDelta) { return target; }
    return current + sign(diff) * maxDelta;
}

// إشارة الرقم: 1 موجب، -1 سالب، 0 صفر

// يقصّ الجزء العشري من x نحو الصفر (بخلاف floor الذي يتجه لأسفل دوماً)

// الجزء الكسري من x (دوماً >= 0)
fun fract(x) {
    return x - floor(x);
}

// قسمة آمنة: تعيد fallback بدل الانهيار عند b == 0
fun safeDiv(a, b, fallback) {
    if (b == 0) { return fallback; }
    return a / b;
}

// تساوٍ تقريبي باستخدام EPSILON الافتراضي
fun approxEqual(a, b) {
    return abs(a - b) < EPSILON;
}

// تساوٍ تقريبي بفارق eps مخصّص
fun approxEqualEps(a, b, eps) {
    return abs(a - b) < eps;
}

// تقريب x إلى عدد محدد من الخانات العشرية
fun roundTo(x, decimals) {
    let factor = pow(10, decimals);
    return round(x * factor) / factor;
}

// هل x بين lo و hi ضمناً (inclusive)؟
fun inRange(x, lo, hi) {
    return x >= lo and x <= hi;
}

// النسبة المئوية لـ part من total
fun percentOf(part, total) {
    if (total == 0) { return 0; }
    return (part / total) * 100;
}

// متوسط مصفوفة أرقام (اسم بديل مريح لـ mean الفطرية)
fun average(arr) {
    return mean(arr);
}

// القاسم المشترك الأكبر لعناصر مصفوفة كاملة (0 إن كانت فارغة)
fun gcdArr(arr) {
    if (len(arr) == 0) { return 0; }
    let result = arr[0];
    let i = 1;
    while (i < len(arr)) {
        result = gcd(result, arr[i]);
        i = i + 1;
    }
    return result;
}

// المضاعف المشترك الأصغر لعناصر مصفوفة كاملة (0 إن كانت فارغة)
fun lcmArr(arr) {
    if (len(arr) == 0) { return 0; }
    let result = arr[0];
    let i = 1;
    while (i < len(arr)) {
        result = lcm(result, arr[i]);
        i = i + 1;
    }
    return result;
}

// مجموع مربعات عناصر مصفوفة أرقام
fun sumOfSquares(arr) {
    let total = 0;
    let i = 0;
    while (i < len(arr)) {
        total = total + arr[i] * arr[i];
        i = i + 1;
    }
    return total;
}

// طول الوتر لمثلث قائم بضلعين a وb: sqrt(a^2 + b^2)
fun hypot(a, b) {
    return sqrt(a * a + b * b);
}

// يحوّل زاوية من درجات إلى راديان
fun degToRad(deg) {
    return deg * (PI / 180);
}

// يحوّل زاوية من راديان إلى درجات
fun radToDeg(rad) {
    return rad * (180 / PI);
}

// ---------------------------------------------------------------------------
// 4) مثلثات — بلا دعم فطري من المفسّر: سلاسل تايلور مع اختزال مجال (range
//    reduction) لضمان دقّة عملية كاملة (double) على أي مدخل معقول
// ---------------------------------------------------------------------------

// يلفّ زاوية إلى المجال (-PI, PI] استعداداً لسلسلة تايلور (تقارب أسرع وأدق)
fun _reduceAngle(x) {
    return x - TAU * floor((x + PI) / TAU);
}



fun cot(x) { return cos(x) / sin(x); }
fun sec(x) { return 1 / cos(x); }
fun csc(x) { return 1 / sin(x); }

// نسخ مريحة تأخذ زاوية بالدرجات
fun sinDeg(deg) { return sin(degToRad(deg)); }
fun cosDeg(deg) { return cos(degToRad(deg)); }
fun tanDeg(deg) { return tan(degToRad(deg)); }

// سلسلة تايلور لـ atan تفترض |x| صغيرة (تُستخدم داخلياً بعد اختزال المجال)
fun _atanTaylor(x) {
    let x2 = x * x;
    let term = x;
    let total = x;
    let i = 1;
    while (i <= 12) {
        term = term * (-x2);
        total = total + term / (2 * i + 1);
        i = i + 1;
    }
    return total;
}

// atan(x) عبر اختزال نصف-الزاوية المتكرر: atan(x) = 2*atan(x/(1+sqrt(1+x^2)))
// حتى تصغر القيمة كفاية لتقارب سريع لسلسلة تايلور



// atan2(y, x): زاوية النقطة (x, y) مع مراعاة الربع الصحيح

// ---------------------------------------------------------------------------
// 5) أسّية ولوغاريتمات — نفس منهج القسم السابق (سلاسل + اختزال مجال)
// ---------------------------------------------------------------------------

// سلسلة تايلور لـ exp تفترض |x| <= 0.5 (تُستخدم داخلياً بعد اختزال المجال)
fun _expTaylor(x) {
    let term = 1;
    let total = 1;
    let i = 1;
    while (i <= 25) {
        term = term * x / i;
        total = total + term;
        i = i + 1;
    }
    return total;
}

// exp(x) عبر اختزال المجال: نقسم x على 2 حتى تصغر ثم نربّع النتيجة بالعدد
// نفسه من المرّات (exp(x) = exp(x/2^k)^(2^k))

// اللوغاريتم الطبيعي: نختزل x إلى [1,2) عبر تتبّع الأس e (x = m * 2^e) ثم
// نستخدم سلسلة atanh السريعة التقارب: ln(m) = 2*atanh((m-1)/(m+1))

fun log2(x) { return ln(x) / LN2; }
fun logBase(x, base) { return ln(x) / ln(base); }

// ---------------------------------------------------------------------------
// 6) دوال زائدية (hyperbolic)
// ---------------------------------------------------------------------------
fun sinh(x) { return (exp(x) - exp(-x)) / 2; }
fun cosh(x) { return (exp(x) + exp(-x)) / 2; }
fun tanh(x) { return sinh(x) / cosh(x); }

// ---------------------------------------------------------------------------
// 7) متجهات ثنائية/ثلاثية الأبعاد — كمصفوفات [x,y] / [x,y,z]
// ---------------------------------------------------------------------------
fun vec2(x, y) { return [x, y]; }
fun vec2Add(a, b) { return [a[0] + b[0], a[1] + b[1]]; }
fun vec2Sub(a, b) { return [a[0] - b[0], a[1] - b[1]]; }
fun vec2Scale(a, s) { return [a[0] * s, a[1] * s]; }
fun vec2Dot(a, b) { return a[0] * b[0] + a[1] * b[1]; }
fun vec2LengthSq(a) { return a[0] * a[0] + a[1] * a[1]; }
fun vec2Length(a) { return sqrt(vec2LengthSq(a)); }
fun vec2Normalize(a) {
    let l = vec2Length(a);
    if (l == 0) { return [0, 0]; }
    return [a[0] / l, a[1] / l];
}
fun vec2Distance(a, b) { return vec2Length(vec2Sub(b, a)); }
fun vec2Lerp(a, b, t) { return [lerp(a[0], b[0], t), lerp(a[1], b[1], t)]; }
fun vec2Angle(a) { return atan2(a[1], a[0]); }

fun vec3(x, y, z) { return [x, y, z]; }
fun vec3Add(a, b) { return [a[0] + b[0], a[1] + b[1], a[2] + b[2]]; }
fun vec3Sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
fun vec3Scale(a, s) { return [a[0] * s, a[1] * s, a[2] * s]; }
fun vec3Dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
fun vec3Cross(a, b) {
    return [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    ];
}
fun vec3LengthSq(a) { return a[0] * a[0] + a[1] * a[1] + a[2] * a[2]; }
fun vec3Length(a) { return sqrt(vec3LengthSq(a)); }
fun vec3Normalize(a) {
    let l = vec3Length(a);
    if (l == 0) { return [0, 0, 0]; }
    return [a[0] / l, a[1] / l, a[2] / l];
}
fun vec3Distance(a, b) { return vec3Length(vec3Sub(b, a)); }
fun vec3Lerp(a, b, t) {
    return [lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t)];
}

// ---------------------------------------------------------------------------
// 8) دوال Easing — منحنيات حركة قياسية لواجهات Indsin أو محرك الألعاب (t في [0,1])
// ---------------------------------------------------------------------------
fun smoothstep(t) {
    let c = clampNum(t, 0, 1);
    return c * c * (3 - 2 * c);
}
fun smootherstep(t) {
    let c = clampNum(t, 0, 1);
    return c * c * c * (c * (c * 6 - 15) + 10);
}
fun easeInQuad(t) { return t * t; }
fun easeOutQuad(t) { return 1 - (1 - t) * (1 - t); }
fun easeInOutQuad(t) {
    if (t < 0.5) { return 2 * t * t; }
    return 1 - pow(-2 * t + 2, 2) / 2;
}
fun easeInCubic(t) { return t * t * t; }
fun easeOutCubic(t) { return 1 - pow(1 - t, 3); }
fun easeInOutCubic(t) {
    if (t < 0.5) { return 4 * t * t * t; }
    return 1 - pow(-2 * t + 2, 3) / 2;
}
fun easeInSine(t) { return 1 - cos((t * PI) / 2); }
fun easeOutSine(t) { return sin((t * PI) / 2); }
fun easeInOutSine(t) { return -(cos(PI * t) - 1) / 2; }
fun easeOutBounce(t) {
    let n1 = 7.5625;
    let d1 = 2.75;
    let x = t;
    if (x < 1 / d1) { return n1 * x * x; }
    if (x < 2 / d1) {
        x = x - 1.5 / d1;
        return n1 * x * x + 0.75;
    }
    if (x < 2.5 / d1) {
        x = x - 2.25 / d1;
        return n1 * x * x + 0.9375;
    }
    x = x - 2.625 / d1;
    return n1 * x * x + 0.984375;
}
fun easeInBounce(t) { return 1 - easeOutBounce(1 - t); }

// ---------------------------------------------------------------------------
// 9) إحصاء إضافي فوق stdlib (عيّنة/تباين مشترك/ارتباط بيرسون)
// ---------------------------------------------------------------------------

// تباين العيّنة (يقسم على n-1 بخلاف variance الفطرية التي تقسم على n)
fun sampleVariance(arr) {
    let n = len(arr);
    if (n < 2) { return 0; }
    let m = mean(arr);
    let total = 0;
    let i = 0;
    while (i < n) {
        let diff = arr[i] - m;
        total = total + diff * diff;
        i = i + 1;
    }
    return total / (n - 1);
}
fun sampleStdDev(arr) { return sqrt(sampleVariance(arr)); }

// التباين المشترك (population covariance) بين مصفوفتين متساويتي الطول
fun covariance(xs, ys) {
    let n = len(xs);
    if (n == 0 or n != len(ys)) { return 0; }
    let mx = mean(xs);
    let my = mean(ys);
    let total = 0;
    let i = 0;
    while (i < n) {
        total = total + (xs[i] - mx) * (ys[i] - my);
        i = i + 1;
    }
    return total / n;
}

// معامل ارتباط بيرسون (Pearson correlation) بين -1 و 1
fun correlation(xs, ys) {
    let sx = stddev(xs);
    let sy = stddev(ys);
    if (sx == 0 or sy == 0) { return 0; }
    return covariance(xs, ys) / (sx * sy);
}

// ---------------------------------------------------------------------------
// 10) عشوائية مساعدة — فوق random() الفطرية التي تعيد رقماً في [0,1)
// ---------------------------------------------------------------------------
fun randomRange(lo, hi) { return lo + random() * (hi - lo); }
fun randomInt(lo, hi) { return floor(lo + random() * (hi - lo + 1)); }
fun randomBool(p) { return random() < p; }
fun randomSign() {
    if (random() < 0.5) { return -1; }
    return 1;
}
fun randomChoice(arr) {
    let n = len(arr);
    if (n == 0) { return nil; }
    let i = floor(random() * n);
    if (i >= n) { i = n - 1; }
    return arr[i];
}
// خلط مصفوفة في مكانها (Fisher–Yates) وتُعيدها أيضاً
fun shuffle(arr) {
    let i = len(arr) - 1;
    while (i > 0) {
        let j = floor(random() * (i + 1));
        let tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
        i = i - 1;
    }
    return arr;
}

// ════════════════════════════════════════════════════════════════════════
// RMF (Rin Math Fabric) Phase 1 §7 — Vector2 / Vector3
// بُنيت فوق struct + Operator Overloading (__add__/__sub__/__mul__/__div__/__neg__، انظر
// tryOperatorOverload في rin_interpreter.cpp) بدل أي دعم خاص داخل المفسّر لكل نوع رياضي على حدة —
// هذا بالضبط ما يجعل RMF قابلاً للتوسّع (§41) بأنواع رياضية إضافية (Matrix/Complex/Fraction/Unit
// لاحقاً) بنفس الأسلوب، بلا أي حاجة لتعديل C++ إضافي لكل نوع جديد. struct (لا class): دلالة قيمة
// (تُنسَخ عند الإسناد/تمرير كوسيط) هي الأنسب لكيان رياضي كالمتجه، تماماً كـ int/float.
// ════════════════════════════════════════════════════════════════════════

struct Vector2 {
    let x = 0;
    let y = 0;

    fun init(x, y) {
        self.x = x;
        self.y = y;
    }

    fun __add__(o) { return Vector2(self.x + o.x, self.y + o.y); }
    fun __sub__(o) { return Vector2(self.x - o.x, self.y - o.y); }
    fun __mul__(k) { return Vector2(self.x * k, self.y * k); }
    fun __div__(k) { return Vector2(self.x / k, self.y / k); }
    fun __neg__() { return Vector2(-self.x, -self.y); }

    fun lengthSquared() { return self.x * self.x + self.y * self.y; }
    fun length() { return sqrt(self.lengthSquared()); }

    fun normalize() {
        let len = self.length();
        if (len == 0) { return Vector2(0, 0); }
        return Vector2(self.x / len, self.y / len);
    }

    fun dot(o) { return self.x * o.x + self.y * o.y; }
    fun distance(o) { return (self - o).length(); }
    fun angle() { return atan2(self.y, self.x); }

    fun lerp(o, t) {
        return Vector2(self.x + (o.x - self.x) * t, self.y + (o.y - self.y) * t);
    }

    fun toStr() { return "Vector2(" + self.x + ", " + self.y + ")"; }
}

struct Vector3 {
    let x = 0;
    let y = 0;
    let z = 0;

    fun init(x, y, z) {
        self.x = x;
        self.y = y;
        self.z = z;
    }

    fun __add__(o) { return Vector3(self.x + o.x, self.y + o.y, self.z + o.z); }
    fun __sub__(o) { return Vector3(self.x - o.x, self.y - o.y, self.z - o.z); }
    fun __mul__(k) { return Vector3(self.x * k, self.y * k, self.z * k); }
    fun __div__(k) { return Vector3(self.x / k, self.y / k, self.z / k); }
    fun __neg__() { return Vector3(-self.x, -self.y, -self.z); }

    fun lengthSquared() { return self.x * self.x + self.y * self.y + self.z * self.z; }
    fun length() { return sqrt(self.lengthSquared()); }

    fun normalize() {
        let len = self.length();
        if (len == 0) { return Vector3(0, 0, 0); }
        return Vector3(self.x / len, self.y / len, self.z / len);
    }

    fun dot(o) { return self.x * o.x + self.y * o.y + self.z * o.z; }

    fun cross(o) {
        return Vector3(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x
        );
    }

    fun distance(o) { return (self - o).length(); }

    fun lerp(o, t) {
        return Vector3(
            self.x + (o.x - self.x) * t,
            self.y + (o.y - self.y) * t,
            self.z + (o.z - self.z) * t
        );
    }

    fun toStr() { return "Vector3(" + self.x + ", " + self.y + ", " + self.z + ")"; }
}

// ════════════════════════════════════════════════════════════════════════
// RMF (Rin Math Fabric) Phase 2 §8 — Matrix (مصفوفة عامة بأي حجم rows×cols)
// نفس أسلوب Vector2/Vector3 أعلاه بالضبط (struct + __add__/__sub__/__mul__/__neg__)، مع فرق واحد:
// __mul__ يحتاج التمييز بين ثلاث حالات (Matrix*Matrix، Matrix*Vector، Matrix*رقم) -- هنا بالضبط
// تظهر فائدة type() الجديدة (انظر natives["type"] في rin_interpreter.cpp): بدل ثلاث دوال منفصلة
// (mulScalar/mulVector/mulMatrix)، دالة سحرية واحدة تفحص type(other) وتتصرف بما يناسب -- هذا هو
// "الحساب الذكي" (§3) عملياً.
// ════════════════════════════════════════════════════════════════════════

struct Matrix {
    let rows = 0;
    let cols = 0;
    let data = [];

    fun init(rows, cols) {
        self.rows = rows;
        self.cols = cols;
        let d = [];
        let i = 0;
        while (i < rows) {
            let row = [];
            let j = 0;
            while (j < cols) {
                push(row, 0);
                j = j + 1;
            }
            push(d, row);
            i = i + 1;
        }
        self.data = d;
    }

    fun get(i, j) { return self.data[i][j]; }
    fun set(i, j, v) { self.data[i][j] = v; }

    fun __add__(o) {
        let m = Matrix(self.rows, self.cols);
        let i = 0;
        while (i < self.rows) {
            let j = 0;
            while (j < self.cols) {
                m.set(i, j, self.get(i, j) + o.get(i, j));
                j = j + 1;
            }
            i = i + 1;
        }
        return m;
    }

    fun __sub__(o) {
        let m = Matrix(self.rows, self.cols);
        let i = 0;
        while (i < self.rows) {
            let j = 0;
            while (j < self.cols) {
                m.set(i, j, self.get(i, j) - o.get(i, j));
                j = j + 1;
            }
            i = i + 1;
        }
        return m;
    }

    fun __neg__() {
        let m = Matrix(self.rows, self.cols);
        let i = 0;
        while (i < self.rows) {
            let j = 0;
            while (j < self.cols) {
                m.set(i, j, -self.get(i, j));
                j = j + 1;
            }
            i = i + 1;
        }
        return m;
    }

    fun __mul__(o) {
        let t = type(o);
        if (t == "number") {
            let m = Matrix(self.rows, self.cols);
            let i = 0;
            while (i < self.rows) {
                let j = 0;
                while (j < self.cols) {
                    m.set(i, j, self.get(i, j) * o);
                    j = j + 1;
                }
                i = i + 1;
            }
            return m;
        }
        if (t == "Vector2" or t == "Vector3") {
            let vals = [o.x, o.y];
            if (t == "Vector3") { vals = [o.x, o.y, o.z]; }
            if (len(vals) != self.cols) {
                print "Matrix * Vector: dimension mismatch";
                return nil;
            }
            let out = [];
            let i = 0;
            while (i < self.rows) {
                let total = 0;
                let j = 0;
                while (j < self.cols) {
                    total = total + self.get(i, j) * vals[j];
                    j = j + 1;
                }
                push(out, total);
                i = i + 1;
            }
            if (t == "Vector2") { return Vector2(out[0], out[1]); }
            return Vector3(out[0], out[1], out[2]);
        }
        // Matrix * Matrix
        if (self.cols != o.rows) {
            print "Matrix multiplication: dimension mismatch";
            return nil;
        }
        let m = Matrix(self.rows, o.cols);
        let i = 0;
        while (i < self.rows) {
            let j = 0;
            while (j < o.cols) {
                let total = 0;
                let k = 0;
                while (k < self.cols) {
                    total = total + self.get(i, k) * o.get(k, j);
                    k = k + 1;
                }
                m.set(i, j, total);
                j = j + 1;
            }
            i = i + 1;
        }
        return m;
    }

    fun transpose() {
        let m = Matrix(self.cols, self.rows);
        let i = 0;
        while (i < self.rows) {
            let j = 0;
            while (j < self.cols) {
                m.set(j, i, self.get(i, j));
                j = j + 1;
            }
            i = i + 1;
        }
        return m;
    }

    // محدد (determinant) — صيغة مباشرة لـ 1×1/2×2، وتوسيع بالإشارات المتعاقبة (cofactor expansion)
    // لأي حجم أكبر عبر _minor أدناه -- أبسط تطبيق ممكن (O(n!))، كافٍ عملياً للأحجام الشائعة
    // (رسوميات/فيزياء نادراً ما تتجاوز 4×4).
    fun determinant() {
        if (self.rows != self.cols) {
            print "determinant: matrix must be square";
            return nil;
        }
        let n = self.rows;
        if (n == 1) { return self.get(0, 0); }
        if (n == 2) {
            return self.get(0,0) * self.get(1,1) - self.get(0,1) * self.get(1,0);
        }
        let total = 0;
        let j = 0;
        let sign = 1;
        while (j < n) {
            let sub = self._minor(0, j);
            total = total + sign * self.get(0, j) * sub.determinant();
            sign = -sign;
            j = j + 1;
        }
        return total;
    }

    // المصفوفة الفرعية بعد حذف الصف skipRow والعمود skipCol (تُستخدم داخلياً في determinant فقط)
    fun _minor(skipRow, skipCol) {
        let m = Matrix(self.rows - 1, self.cols - 1);
        let ri = 0;
        let i = 0;
        while (i < self.rows) {
            if (i != skipRow) {
                let rj = 0;
                let j = 0;
                while (j < self.cols) {
                    if (j != skipCol) {
                        m.set(ri, rj, self.get(i, j));
                        rj = rj + 1;
                    }
                    j = j + 1;
                }
                ri = ri + 1;
            }
            i = i + 1;
        }
        return m;
    }

    fun toStr() {
        let s = "";
        let i = 0;
        while (i < self.rows) {
            let rowStr = "[";
            let j = 0;
            while (j < self.cols) {
                rowStr = rowStr + self.get(i, j);
                if (j < self.cols - 1) { rowStr = rowStr + ", "; }
                j = j + 1;
            }
            rowStr = rowStr + "]";
            s = s + rowStr;
            if (i < self.rows - 1) { s = s + "\n"; }
            i = i + 1;
        }
        return s;
    }
}

// مصفوفة الوحدة (identity) بحجم n×n — دالة حرة بدل static method (غير مدعومة على الأصناف حالياً)
fun matrixIdentity(n) {
    let m = Matrix(n, n);
    let i = 0;
    while (i < n) {
        m.set(i, i, 1);
        i = i + 1;
    }
    return m;
}

// يبني Matrix من مصفوفة صفوف عادية (array of arrays)، مثال: matrixFrom([[1,2],[3,4]])
fun matrixFrom(rowsData) {
    let r = len(rowsData);
    let c = 0;
    if (r > 0) { c = len(rowsData[0]); }
    let m = Matrix(r, c);
    let i = 0;
    while (i < r) {
        let j = 0;
        while (j < c) {
            m.set(i, j, rowsData[i][j]);
            j = j + 1;
        }
        i = i + 1;
    }
    return m;
}


// ════════════════════════════════════════════════════════════════════════
// RMF (Rin Math Fabric) Phase 3 §13/§14 — Fraction / Complex
// نفس أسلوب Vector/Matrix أعلاه بالضبط: struct + دوال سحرية (__add__/__sub__/__mul__/__div__/
// __neg__). Fraction تبسّط نفسها تلقائياً لأبسط صورة (عبر gcd) في كل init() -- هذا يجعل مقارنة
// == بين كسرين متكافئين (1/2 و2/4 مثلاً) تعمل صحيحة مجاناً عبر المساواة التركيبية العادية لـ
// struct (نفس الحقول = متساويان، انظر valuesEqual في rin_interpreter.cpp)، بلا أي كود مقارنة
// إضافي مطلوب هنا.
// ════════════════════════════════════════════════════════════════════════

struct Fraction {
    let num = 0;
    let den = 1;

    fun init(num, den) {
        if (den == 0) {
            print "Fraction: denominator cannot be 0";
            den = 1;
        }
        if (den < 0) {
            num = -num;
            den = -den;
        }
        let g = gcd(abs(num), abs(den));
        if (g == 0) { g = 1; }
        self.num = num / g;
        self.den = den / g;
    }

    fun __add__(o) { return Fraction(self.num * o.den + o.num * self.den, self.den * o.den); }
    fun __sub__(o) { return Fraction(self.num * o.den - o.num * self.den, self.den * o.den); }
    fun __mul__(o) { return Fraction(self.num * o.num, self.den * o.den); }
    fun __div__(o) { return Fraction(self.num * o.den, self.den * o.num); }
    fun __neg__() { return Fraction(-self.num, self.den); }

    fun toNumber() { return self.num / self.den; }
    fun toStr() { return self.num + "/" + self.den; }
}

struct Complex {
    let re = 0;
    let im = 0;

    fun init(re, im) {
        self.re = re;
        self.im = im;
    }

    fun __add__(o) { return Complex(self.re + o.re, self.im + o.im); }
    fun __sub__(o) { return Complex(self.re - o.re, self.im - o.im); }

    fun __mul__(o) {
        if (type(o) == "number") { return Complex(self.re * o, self.im * o); }
        return Complex(self.re * o.re - self.im * o.im, self.re * o.im + self.im * o.re);
    }

    fun __div__(o) {
        if (type(o) == "number") { return Complex(self.re / o, self.im / o); }
        let denom = o.re * o.re + o.im * o.im;
        return Complex((self.re * o.re + self.im * o.im) / denom, (self.im * o.re - self.re * o.im) / denom);
    }

    fun __neg__() { return Complex(-self.re, -self.im); }

    fun magnitude() { return sqrt(self.re * self.re + self.im * self.im); }
    fun phase() { return atan2(self.im, self.re); }
    fun conjugate() { return Complex(self.re, -self.im); }

    fun toStr() {
        if (self.im >= 0) { return self.re + " + " + self.im + "i"; }
        return self.re + " - " + (-self.im) + "i";
    }
}

// ════════════════════════════════════════════════════════════════════════
// RMF (Rin Math Fabric) Phase 4 §19 — Geometry: Circle / Rectangle
// مبنية فوق Vector2 (نفس فلسفة كل RMF: كل طبقة تُبنى فوق طبقة أدنى منطقياً، §41) --
// Circle.contains تستخدم Vector2.distance مباشرة بدل إعادة كتابة حساب المسافة من الصفر.
// ════════════════════════════════════════════════════════════════════════

struct Circle {
    let center = nil;
    let radius = 0;

    fun init(center, radius) {
        self.center = center;
        self.radius = radius;
    }

    fun area() { return PI * self.radius * self.radius; }
    fun circumference() { return TAU * self.radius; }

    fun contains(point) { return self.center.distance(point) <= self.radius; }

    fun intersects(other) { return self.center.distance(other.center) <= (self.radius + other.radius); }

    fun toStr() { return "Circle(center=" + self.center.toStr() + ", radius=" + self.radius + ")"; }
}

struct Rectangle {
    let x = 0;
    let y = 0;
    let width = 0;
    let height = 0;

    fun init(x, y, width, height) {
        self.x = x;
        self.y = y;
        self.width = width;
        self.height = height;
    }

    fun area() { return self.width * self.height; }
    fun perimeter() { return 2 * (self.width + self.height); }

    fun contains(point) {
        if (point.x < self.x) { return false; }
        if (point.x > self.x + self.width) { return false; }
        if (point.y < self.y) { return false; }
        if (point.y > self.y + self.height) { return false; }
        return true;
    }

    fun intersects(other) {
        if (self.x >= other.x + other.width) { return false; }
        if (self.x + self.width <= other.x) { return false; }
        if (self.y >= other.y + other.height) { return false; }
        if (self.y + self.height <= other.y) { return false; }
        return true;
    }

    fun centerPoint() { return Vector2(self.x + self.width / 2, self.y + self.height / 2); }

    fun toStr() { return "Rectangle(x=" + self.x + ", y=" + self.y + ", w=" + self.width + ", h=" + self.height + ")"; }
}

// مساحة مثلث برؤوس ثلاث نقاط Vector2 (صيغة shoelace/المحدد)
fun triangleArea(a, b, c) {
    return abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2;
}

// محيط مثلث برؤوس ثلاث نقاط Vector2
fun trianglePerimeter(a, b, c) {
    return a.distance(b) + b.distance(c) + c.distance(a);
}

// ════════════════════════════════════════════════════════════════════════
// RMF (Rin Math Fabric) Phase 5 §28 — Interpolation إضافية
// lerp/invLerp/remap موجودة أصلاً (lerp أصبحت أصلية في المفسّر منذ Phase 1). smoothstep/
// smootherstep إضافتان قياسيتان في الرسوميات (منحنى-S بدل خط مستقيم)، مبنيتان فوق clamp الأصلية.
// ════════════════════════════════════════════════════════════════════════

fun smoothstep(edge0, edge1, x) {
    let t = clamp((x - edge0) / (edge1 - edge0), 0, 1);
    return t * t * (3 - 2 * t);
}

fun smootherstep(edge0, edge1, x) {
    let t = clamp((x - edge0) / (edge1 - edge0), 0, 1);
    return t * t * t * (t * (t * 6 - 15) + 10);
}

// ════════════════════════════════════════════════════════════════════════
// RMF Phase 5 §20 — Calculus (عددي/numerical فقط -- لا اشتقاق/تكامل رمزي، انظر تعليق solve أدناه
// لشرح لماذا). f هنا قيمة دالة عادية من Rin (fun أو تعبير lambda مُمرَّر كوسيط) تُستدعى مباشرة
// داخل هذه الدوال كـ f(x) -- ممكنة بفضل كون الدوال قيماً من الدرجة الأولى (first-class) في Rin.
// ════════════════════════════════════════════════════════════════════════

// مشتقة عددية عند نقطة x (الفرق المركزي/central difference -- أدق من الفرق الأمامي البسيط)
fun differentiate(f, x) {
    let h = 0.0001;
    return (f(x + h) - f(x - h)) / (2 * h);
}

// تكامل عددي محدَّد من a إلى b (قاعدة سيمبسون/Simpson's rule بـ 1000 فترة -- دقيقة وسريعة عملياً)
fun integrate(f, a, b) {
    let n = 1000;
    let h = (b - a) / n;
    let total = f(a) + f(b);
    let i = 1;
    while (i < n) {
        let x = a + i * h;
        if (i % 2 == 0) {
            total = total + 2 * f(x);
        } else {
            total = total + 4 * f(x);
        }
        i = i + 1;
    }
    return total * h / 3;
}

// نهاية f عند x تقريبياً (تقترب من كلا الجانبين وتأخذ المتوسط -- تقريب عددي، ليس إثباتاً رمزياً
// لوجود النهاية). direction اختيارية: "left"/"right"/بلا شيء (الافتراضي، من الجهتين).
fun limitAt(f, x, direction) {
    let h = 0.00001;
    if (direction == "left") { return f(x - h); }
    if (direction == "right") { return f(x + h); }
    return (f(x - h) + f(x + h)) / 2;
}

// ════════════════════════════════════════════════════════════════════════
// RMF Phase 5 §4/§5 — حل المعادلات (numerical/عددي مباشر، وليس جبراً رمزياً كاملاً)
// بناء نظام جبر رمزي حقيقي (تمثيل تعبير كشجرة، simplify/expand/factor لأي تعبير عام) مشروع بحثي
// ضخم بذاته (محرك CAS كامل) يتجاوز نطاق أي إضافة واحدة معقولة. هنا الجزء العملي الأهم من §4/§5
// تحديداً: حل معادلات خطية/تربيعية فعلياً بمعاملاتها الرقمية (solveLinear/solveQuadratic) --
// يغطي "لا حل / حل واحد / حلين / حلول تقريبية" المطلوبة صراحة، ويُعيد جذوراً من نوع Complex
// (Phase 3) بدل تجاهلها أو الفشل عندما يكون المميّز سالباً، فيغطي أيضاً "حلول رمزية عند الإمكان".
// ════════════════════════════════════════════════════════════════════════

// حل ax + b = 0. يُعيد رقماً (الحل الوحيد)، أو النص "none" (لا حل)، أو "infinite" (كل رقم حل).
fun solveLinear(a, b) {
    if (a == 0) {
        if (b == 0) { return "infinite"; }
        return "none";
    }
    return -b / a;
}

// حل ax^2 + bx + c = 0. يُعيد array بالحلول: عنصر واحد (جذر مضاعف)، عنصران (Number أو Complex
// حسب إشارة المميّز)، أو -- إن كانت a=0 فعلياً معادلة خطية -- يُفوّض لـ solveLinear (وقد يُعيد نصاً
// بدل array في تلك الحالة الحدّية، فتحقّق من type() قبل الافتراض أنها array دائماً).
fun solveQuadratic(a, b, c) {
    if (a == 0) { return solveLinear(b, c); }
    let disc = b * b - 4 * a * c;
    if (disc > 0) {
        let sq = sqrt(disc);
        return [(-b + sq) / (2 * a), (-b - sq) / (2 * a)];
    }
    if (disc == 0) {
        return [-b / (2 * a)];
    }
    let sq = sqrt(-disc);
    let re = -b / (2 * a);
    let im = sq / (2 * a);
    return [Complex(re, im), Complex(re, -im)];
}

// ════════════════════════════════════════════════════════════════════════
// RMF Phase 5 §10/§11 — Units & Dimensions
// أبسط تصميم ممكن: كل Unit يحمل (value, name). جدول واحد (_unitInfo) يربط كل اسم وحدة بِـ
// [البُعد، معامل التحويل إلى وحدة القياس الأساسية لذلك البُعد]. الجمع/الطرح يتطلبان نفس البُعد
// (وإلا: خطأ رياضي واضح -- meter + second مرفوضة صراحة، تماماً كما طُلب). القسمة/الضرب بعدد عادي
// تُغيّر value فقط. قسمة وحدتين من نفس البُعد تُوحَّدان لنفس وحدة القياس الأساسية أولاً ثم تُعطيان
// نسبة رقمية عادية (بلا وحدة). ضرب/قسمة بين بُعدين مختلفين (مثل distance/time) تُنتج Unit بوحدة
// مركَّبة باسمها ("m/s") -- بلا اشتقاق نظام أبعاد كامل (Velocity/Force كأنواع منفصلة)؛ القيمة
// والوحدة المركَّبة كافيتان عملياً لنتيجة "صحيحة الأبعاد" كما طُلب صراحة في المثال (distance/time).
// ════════════════════════════════════════════════════════════════════════

fun _unitInfo(name) {
    if (name == "m") { return ["Length", 1]; }
    if (name == "km") { return ["Length", 1000]; }
    if (name == "cm") { return ["Length", 0.01]; }
    if (name == "mm") { return ["Length", 0.001]; }
    if (name == "s") { return ["Time", 1]; }
    if (name == "ms") { return ["Time", 0.001]; }
    if (name == "min") { return ["Time", 60]; }
    if (name == "hr") { return ["Time", 3600]; }
    if (name == "kg") { return ["Mass", 1]; }
    if (name == "g") { return ["Mass", 0.001]; }
    if (name == "deg") { return ["Angle", 1]; }
    if (name == "rad") { return ["Angle", 57.29577951308232]; }
    return nil;
}

struct Unit {
    let value = 0;
    let name = "";

    fun init(value, name) {
        self.value = value;
        self.name = name;
        // لا تُحذِّر لو كانت الوحدة مركَّبة (تحتوي '*' أو '/') -- هذه تُنتَج داخلياً تلقائياً من
        // __mul__/__div__ نفسها (مثل "km/hr")، وليست خطأ مبرمج؛ التحذير مخصَّص فقط لاسم وحدة
        // أساسي غير معروف كتبه المبرمج مباشرة (خطأ إملائي مثلاً).
        if (_unitInfo(name) == nil and !contains(name, "*") and !contains(name, "/")) {
            print "Unit: unknown unit '" + name + "'";
        }
    }

    fun dimension() {
        let info = _unitInfo(self.name);
        if (info == nil) { return "Unknown"; }
        return info[0];
    }

    fun _factor() {
        let info = _unitInfo(self.name);
        if (info == nil) { return 1; }
        return info[1];
    }

    fun _baseValue() { return self.value * self._factor(); }

    fun __add__(o) {
        if (self.dimension() != o.dimension()) {
            print "InvalidUnitOperation: cannot add '" + self.name + "' and '" + o.name + "' (dimensions: " + self.dimension() + " vs " + o.dimension() + ")";
            return nil;
        }
        return Unit((self._baseValue() + o._baseValue()) / self._factor(), self.name);
    }

    fun __sub__(o) {
        if (self.dimension() != o.dimension()) {
            print "InvalidUnitOperation: cannot subtract '" + self.name + "' and '" + o.name + "' (dimensions: " + self.dimension() + " vs " + o.dimension() + ")";
            return nil;
        }
        return Unit((self._baseValue() - o._baseValue()) / self._factor(), self.name);
    }

    fun __neg__() { return Unit(-self.value, self.name); }

    fun __mul__(o) {
        if (type(o) == "number") { return Unit(self.value * o, self.name); }
        return Unit(self.value * o.value, self.name + "*" + o.name);
    }

    fun __div__(o) {
        if (type(o) == "number") { return Unit(self.value / o, self.name); }
        if (self.dimension() == o.dimension()) {
            if (self.dimension() != "Unknown") {
                return self._baseValue() / o._baseValue();
            }
        }
        return Unit(self.value / o.value, self.name + "/" + o.name);
    }

    fun convertTo(targetName) {
        let info = _unitInfo(targetName);
        if (info == nil) {
            print "Unit.convertTo: unknown unit '" + targetName + "'";
            return nil;
        }
        if (info[0] != self.dimension()) {
            print "InvalidUnitOperation: cannot convert '" + self.name + "' to '" + targetName + "' (different dimensions)";
            return nil;
        }
        return Unit(self._baseValue() / info[1], targetName);
    }

    fun toStr() { return self.value + self.name; }
}
)MATHOGRIN";

static const char* kLib_strings_og_rin = R"STRINGSOGRIN(
// ============================================================================
//  lib/strings.og.rin — امتدادات نصوص فوق stdlib الأساسية (upper/lower/trim/substr/split/join...)
//  استيراد:
//    @import "lib/strings.og.rin";
//    @import "lib/strings.og.rin" as strx;
// ============================================================================

// يجعل أول حرف كبيراً وبقية النص كما هو: "rin" -> "Rin"
fun capitalize(s) {
    if (len(s) == 0) { return s; }
    return upper(charAt(s, 0)) + substr(s, 1);
}

// يعكس ترتيب أحرف النص: "abc" -> "cba"
fun reverseStr(s) {
    let result = "";
    let i = len(s) - 1;
    while (i >= 0) {
        result = result + charAt(s, i);
        i = i - 1;
    }
    return result;
}

// هل s يبدأ بـ prefix؟
fun startsWith(s, prefix) {
    if (len(prefix) > len(s)) { return false; }
    return substr(s, 0, len(prefix)) == prefix;
}

// هل s ينتهي بـ suffix؟
fun endsWith(s, suffix) {
    let sl = len(s);
    let pl = len(suffix);
    if (pl > sl) { return false; }
    return substr(s, sl - pl, pl) == suffix;
}

// يكمل النص من اليسار حتى يصل طوله إلى width باستخدام حرف الحشو ch
fun padLeft(s, width, ch) {
    let result = s;
    while (len(result) < width) {
        result = ch + result;
    }
    return result;
}

// يكمل النص من اليمين حتى يصل طوله إلى width باستخدام حرف الحشو ch
fun padRight(s, width, ch) {
    let result = s;
    while (len(result) < width) {
        result = result + ch;
    }
    return result;
}

// يكرر النص s عدد n من المرات
fun repeatStr(s, n) {
    let result = "";
    let i = 0;
    while (i < n) {
        result = result + s;
        i = i + 1;
    }
    return result;
}

// يجعل أول حرف من كل كلمة كبيراً: "hello rin lang" -> "Hello Rin Lang"
fun titleCase(s) {
    let words = split(s, " ");
    let result = [];
    let i = 0;
    while (i < len(words)) {
        push(result, capitalize(words[i]));
        i = i + 1;
    }
    return join(result, " ");
}

// هل النص فارغ أو يحتوي على مسافات فقط؟
fun isBlank(s) {
    return trim(s) == "";
}

// يحسب عدد مرات ظهور sub داخل s (بلا تداخل بين المطابقات)
fun countOccurrences(s, sub) {
    if (len(sub) == 0) { return 0; }
    let count = 0;
    let rest = s;
    let idx = indexOf(rest, sub);
    while (idx != -1) {
        count = count + 1;
        rest = substr(rest, idx + len(sub));
        idx = indexOf(rest, sub);
    }
    return count;
}

// يزيل جميع الفراغات (المسافات) من النص
fun stripSpaces(s) {
    return replace(s, " ", "");
}

// يحوّل نص فاصل مثل "a-b-c" إلى مصفوفة عبر separator، بعد تقليم الفراغات من كل عنصر
fun splitTrim(s, separator) {
    let parts = split(s, separator);
    let result = [];
    let i = 0;
    while (i < len(parts)) {
        push(result, trim(parts[i]));
        i = i + 1;
    }
    return result;
}

// يقتطع s إلى maxLen حرفاً كحد أقصى مضيفاً suffix (مثل "...") عند الاقتطاع الفعلي؛
// إن كان s أقصر من أو يساوي maxLen يُعاد كما هو دون أي إضافة
fun truncate(s, maxLen, suffix) {
    if (len(s) <= maxLen) { return s; }
    let cut = maxLen - len(suffix);
    if (cut < 0) { cut = 0; }
    return substr(s, 0, cut) + suffix;
}

// عدد الكلمات في s (مفصولة بمسافات، بعد تجاهل الفراغات الزائدة في البداية/النهاية)
fun wordCount(s) {
    let t = trim(s);
    if (t == "") { return 0; }
    let words = split(t, " ");
    let count = 0;
    let i = 0;
    while (i < len(words)) {
        if (trim(words[i]) != "") { count = count + 1; }
        i = i + 1;
    }
    return count;
}

// يحوّل نصاً إلى شكل "slug" مناسب لروابط URL: أحرف صغيرة، الفراغات والفواصل
// السفلية تتحول إلى "-"، وتُزال أي أحرف ليست حروفاً/أرقاماً/"-"
fun slugify(s) {
    let lowered = lower(trim(s));
    let allowed = "abcdefghijklmnopqrstuvwxyz0123456789-";
    let result = "";
    let i = 0;
    let lastWasDash = false;
    while (i < len(lowered)) {
        let c = charAt(lowered, i);
        if (c == " " or c == "_") { c = "-"; }
        if (contains(allowed, c)) {
            if (c == "-") {
                if (lastWasDash == false and result != "") {
                    result = result + c;
                    lastWasDash = true;
                }
            } else {
                result = result + c;
                lastWasDash = false;
            }
        }
        i = i + 1;
    }
    while (endsWith(result, "-")) {
        result = substr(result, 0, len(result) - 1);
    }
    return result;
}

// هل s يقرأ نفسه بنفس الطريقة من الجهتين (متناظر/palindrome)؟ يتجاهل حالة الأحرف
fun isPalindrome(s) {
    let normalized = lower(s);
    return normalized == reverseStr(normalized);
}

// يزيل prefix من بداية s إن وُجد فعلاً في البداية، وإلا يُعيد s كما هو
fun removePrefix(s, prefix) {
    if (startsWith(s, prefix)) { return substr(s, len(prefix)); }
    return s;
}

// يزيل suffix من نهاية s إن وُجد فعلاً في النهاية، وإلا يُعيد s كما هو
fun removeSuffix(s, suffix) {
    if (endsWith(s, suffix)) { return substr(s, 0, len(s) - len(suffix)); }
    return s;
}

// يحشو s من الجهتين بحرف ch حتى يصل طوله إلى width (الحشو الزائد يوضع يميناً عند العدد الفردي)
fun center(s, width, ch) {
    let total = width - len(s);
    if (total <= 0) { return s; }
    let leftPad = total / 2;
    if (leftPad < 0) { leftPad = 0; }
    let leftCount = floor(leftPad);
    let result = s;
    let i = 0;
    while (i < leftCount) { result = ch + result; i = i + 1; }
    while (len(result) < width) { result = result + ch; }
    return result;
}
)STRINGSOGRIN";

static const char* kLib_data_og_rin = R"DATAOGRIN(
// ============================================================================
//  lib/data.og.rin — أدوات مصفوفات وقواميس (arrays/maps) فوق stdlib الأساسية
//  استيراد:
//    @import "lib/data.og.rin";
//    @import "lib/data.og.rin" as data;
// ============================================================================

// مصفوفة [0, 1, ..., n-1]
// ملاحظة: الاسم sequence وليس range لأن `range` اسم دالة مبنية مسبقاً (تحسب الفرق بين أكبر
// وأصغر قيمة في مصفوفة أرقام)، ودالة بنفس اسم دالة مبنية غير مسموحة في Rin.
fun sequence(n) {
    let result = [];
    let i = 0;
    while (i < n) {
        push(result, i);
        i = i + 1;
    }
    return result;
}

// مصفوفة [start, start+1, ..., endExclusive-1]
fun rangeFrom(start, endExclusive) {
    let result = [];
    let i = start;
    while (i < endExclusive) {
        push(result, i);
        i = i + 1;
    }
    return result;
}

// عناصر فريدة من arr (يحافظ على أول ظهور لكل عنصر بالترتيب)
fun unique(arr) {
    let result = [];
    let i = 0;
    while (i < len(arr)) {
        if (!contains(result, arr[i])) {
            push(result, arr[i]);
        }
        i = i + 1;
    }
    return result;
}

// يقسّم arr إلى مصفوفات فرعية بحجم size (الأخيرة قد تكون أقصر)
fun chunk(arr, size) {
    let result = [];
    let current = [];
    let i = 0;
    while (i < len(arr)) {
        push(current, arr[i]);
        if (len(current) == size) {
            push(result, current);
            current = [];
        }
        i = i + 1;
    }
    if (len(current) > 0) {
        push(result, current);
    }
    return result;
}

// يدمج a و b عنصراً بعنصر إلى مصفوفة أزواج [a[i], b[i]] (بطول أقصر المصفوفتين)
fun zip(a, b) {
    let result = [];
    let n = len(a);
    if (len(b) < n) { n = len(b); }
    let i = 0;
    while (i < n) {
        push(result, [a[i], b[i]]);
        i = i + 1;
    }
    return result;
}

// أول عنصر (أو nil إن كانت المصفوفة فارغة)
fun first(arr) {
    if (len(arr) == 0) { return nil; }
    return arr[0];
}

// آخر عنصر (أو nil إن كانت المصفوفة فارغة)
fun last(arr) {
    if (len(arr) == 0) { return nil; }
    return arr[len(arr) - 1];
}

// أول n عنصر من arr
fun take(arr, n) {
    let result = [];
    let i = 0;
    while (i < n) {
        if (i >= len(arr)) { return result; }
        push(result, arr[i]);
        i = i + 1;
    }
    return result;
}

// arr بعد إسقاط أول n عنصر
fun drop(arr, n) {
    let result = [];
    let i = n;
    while (i < len(arr)) {
        push(result, arr[i]);
        i = i + 1;
    }
    return result;
}

// arr بترتيب معكوس (بدون تعديل الأصل)
fun reverseArr(arr) {
    let result = [];
    let i = len(arr) - 1;
    while (i >= 0) {
        push(result, arr[i]);
        i = i - 1;
    }
    return result;
}

// قيمة المفتاح key من m، أو defaultValue إن لم يكن موجوداً
fun mapGet(m, key, defaultValue) {
    if (has(m, key)) {
        return m[key];
    }
    return defaultValue;
}

// قاموس جديد يدمج m1 و m2 (عند تعارض مفتاح يفوز m2)
fun mapMerge(m1, m2) {
    let result = {};
    let k1 = keys(m1);
    let i = 0;
    while (i < len(k1)) {
        result[k1[i]] = m1[k1[i]];
        i = i + 1;
    }
    let k2 = keys(m2);
    i = 0;
    while (i < len(k2)) {
        result[k2[i]] = m2[k2[i]];
        i = i + 1;
    }
    return result;
}

// عدد مرات ظهور value داخل arr
fun countOf(arr, value) {
    let count = 0;
    let i = 0;
    while (i < len(arr)) {
        if (valuesMatch(arr[i], value)) {
            count = count + 1;
        }
        i = i + 1;
    }
    return count;
}

// مقارنة قيمتين (تدعم الأرقام/النصوص/المنطقية مباشرة)؛ دالة مساعدة داخلية لـ countOf
fun valuesMatch(a, b) {
    return a == b;
}

// هل تحتوي arr على value؟ (اختصار مريح فوق countOf، يدعم نفس مقارنة valuesMatch)
fun includesValue(arr, value) {
    return countOf(arr, value) > 0;
}

// مصفوفة جديدة من arr بعد حذف أول ظهور لـ value فقط (بلا تعديل الأصل)؛ تُعيد نسخة
// كاملة دون تغيير إن لم يكن value موجوداً أصلاً
fun removeFirst(arr, value) {
    let result = [];
    let removed = false;
    let i = 0;
    while (i < len(arr)) {
        if (removed == false and valuesMatch(arr[i], value)) {
            removed = true;
        } else {
            push(result, arr[i]);
        }
        i = i + 1;
    }
    return result;
}

// قاموس جديد من m يحتوي فقط على المفاتيح الموجودة في allowedKeys
fun pickKeys(m, allowedKeys) {
    let result = {};
    let i = 0;
    while (i < len(allowedKeys)) {
        let k = allowedKeys[i];
        if (has(m, k)) { result[k] = m[k]; }
        i = i + 1;
    }
    return result;
}

// قاموس جديد من m بدون المفاتيح الموجودة في excludedKeys
fun omitKeys(m, excludedKeys) {
    let result = {};
    let ks = keys(m);
    let i = 0;
    while (i < len(ks)) {
        let k = ks[i];
        let skip = false;
        let j = 0;
        while (j < len(excludedKeys)) {
            if (excludedKeys[j] == k) { skip = true; }
            j = j + 1;
        }
        if (!skip) { result[k] = m[k]; }
        i = i + 1;
    }
    return result;
}

// يجمع arr إلى قاموس مجموعات: المفتاح = keyFn(element)، والقيمة = مصفوفة العناصر
// المطابقة لهذا المفتاح بترتيب ظهورها
fun groupBy(arr, keyFn) {
    let result = {};
    let i = 0;
    while (i < len(arr)) {
        let k = keyFn(arr[i]);
        if (!has(result, k)) { result[k] = []; }
        push(result[k], arr[i]);
        i = i + 1;
    }
    return result;
}
)DATAOGRIN";

static const char* kLib_validate_og_rin = R"VALIDATEOGRIN(
// ============================================================================
//  lib/validate.og.rin — دوال تحقّق (validation) شائعة الاستخدام
//  ملاحظة: لغة Rin لا تملك try/catch، لذا كل دالة هنا "آمنة" (لا ترمي أخطاء) وتُعيد true/false دائماً.
//  استيراد:
//    @import "lib/validate.og.rin";
//    @import "lib/validate.og.rin" as validate;
// ============================================================================

// هل القيمة فارغة (nil، أو نص فارغ تحديداً "")؟ ملاحظة: هذه الدالة لا تفحص مصفوفات
// أو قواميس لأن len() ترمي خطأً على الأرقام/nil ولا توجد دالة typeof في Rin للتمييز
// الآمن بين الأنواع مسبقاً؛ لفحص مصفوفة أو قاموس تحديداً استخدم isEmptyArr/isEmptyMap
fun isEmpty(v) {
    if (v == nil) { return true; }
    if (v == "") { return true; }
    return false;
}

// هل arr مصفوفة بلا عناصر؟ (استدعِها فقط على قيمة تعرف أنها مصفوفة فعلاً)
fun isEmptyArr(arr) {
    return len(arr) == 0;
}

// هل m قاموس بلا مفاتيح؟ (استدعِها فقط على قيمة تعرف أنها قاموس فعلاً)
fun isEmptyMap(m) {
    return len(keys(m)) == 0;
}

// هل النص فارغ أو مسافات فقط؟
fun isBlankStr(s) {
    return trim(s) == "";
}

// هل s يمثّل رقماً صالحاً بالكامل (يقبل علامة سالبة في البداية وفاصلة عشرية واحدة)؟
fun isNumeric(s) {
    if (len(s) == 0) { return false; }
    let digits = "0123456789";
    let i = 0;
    let dotSeen = false;
    let digitSeen = false;
    while (i < len(s)) {
        let c = charAt(s, i);
        if (c == "-" and i == 0) {
            // إشارة سالبة مسموحة فقط في أول النص
        } else if (c == "." and !dotSeen) {
            dotSeen = true;
        } else if (contains(digits, c)) {
            digitSeen = true;
        } else {
            return false;
        }
        i = i + 1;
    }
    return digitSeen;
}

// فحص بسيط وعملي لصيغة بريد إلكتروني (وليس تحققاً كاملاً وفق معيار RFC)
fun isEmail(s) {
    if (isBlankStr(s)) { return false; }
    if (!contains(s, "@")) { return false; }
    if (!contains(s, ".")) { return false; }
    let atIndex = indexOf(s, "@");
    if (atIndex <= 0) { return false; }
    if (atIndex == len(s) - 1) { return false; }
    let afterAt = substr(s, atIndex + 1);
    if (!contains(afterAt, ".")) { return false; }
    return true;
}

// هل طول s بين min و max ضمناً؟
fun lengthBetween(s, minLen, maxLen) {
    return len(s) >= minLen and len(s) <= maxLen;
}

// هل x رقم يقع بين lo و hi ضمناً؟
fun isInRange(x, lo, hi) {
    return x >= lo and x <= hi;
}

// هل s يحتوي حرفاً واحداً على الأقل من كل نوع: حرف، رقم؟ (فحص أساسي لقوة كلمة مرور بلا رموز خاصة)
fun hasLetterAndDigit(s) {
    let letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    let digits = "0123456789";
    let hasLetter = false;
    let hasDigit = false;
    let i = 0;
    while (i < len(s)) {
        let c = charAt(s, i);
        if (contains(letters, c)) { hasLetter = true; }
        if (contains(digits, c)) { hasDigit = true; }
        i = i + 1;
    }
    return hasLetter and hasDigit;
}

// يتحقق من كلمة مرور بحد أدنى للطول وشرط وجود حرف ورقم معاً
fun isStrongPassword(s, minLen) {
    if (len(s) < minLen) { return false; }
    return hasLetterAndDigit(s);
}

// هل كل أحرف s حروف أبجدية فقط (إنجليزية)، وs غير فارغ؟
fun isAlpha(s) {
    if (len(s) == 0) { return false; }
    let letters = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    let i = 0;
    while (i < len(s)) {
        if (!contains(letters, charAt(s, i))) { return false; }
        i = i + 1;
    }
    return true;
}

// هل كل أحرف s حروف أبجدية أو أرقام فقط، وs غير فارغ؟
fun isAlphaNumeric(s) {
    if (len(s) == 0) { return false; }
    let allowed = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    let i = 0;
    while (i < len(s)) {
        if (!contains(allowed, charAt(s, i))) { return false; }
        i = i + 1;
    }
    return true;
}

// هل s يمثّل عدداً صحيحاً بالكامل (بلا فاصلة عشرية، يقبل إشارة سالبة في البداية)؟
fun isInteger(s) {
    if (len(s) == 0) { return false; }
    let digits = "0123456789";
    let i = 0;
    let digitSeen = false;
    while (i < len(s)) {
        let c = charAt(s, i);
        if (c == "-" and i == 0) {
            // إشارة سالبة مسموحة فقط في أول النص
        } else if (contains(digits, c)) {
            digitSeen = true;
        } else {
            return false;
        }
        i = i + 1;
    }
    return digitSeen;
}

// هل url يبدو رابطاً صالحاً بصيغة بسيطة (يبدأ بـ http:// أو https:// ويحتوي نقطة بعدها)؟
// فحص عملي وليس تحققاً كاملاً وفق معيار RFC
fun isUrl(url) {
    let s = trim(url);
    let scheme = "";
    if (startsWith(s, "https://")) { scheme = "https://"; }
    else if (startsWith(s, "http://")) { scheme = "http://"; }
    else { return false; }
    let rest = substr(s, len(scheme));
    if (isBlankStr(rest)) { return false; }
    if (!contains(rest, ".")) { return false; }
    return true;
}
)VALIDATEOGRIN";

static const char* kLib_functional_og_rin = R"FUNCTIONALOGRIN(
// ============================================================================
//  lib/functional.og.rin — دوال ترتيبية عليا (higher-order functions) على المصفوفات
//  تستفيد من أن الدوال في Rin قيم من الدرجة الأولى (first-class): يمكن تمرير اسم أي
//  دالة fn معرَّفة بـ fun كوسيط عادي، ثم استدعاؤها بداخل الدالة المستقبِلة.
//
//  استيراد:
//    @import "lib/functional.og.rin";
//    @import "lib/functional.og.rin" as fx;
//
//  مثال:
//    fun double(x) { return x * 2; }
//    print mapArr([1, 2, 3], double);      // [2, 4, 6]
//    print filterArr([1, 2, 3, 4], isEven); // [2, 4]  (isEven مُعرَّفة أدناه)
// ============================================================================

// يطبّق fn على كل عنصر من arr ويُعيد مصفوفة جديدة بالنتائج
fun mapArr(arr, fn) {
    let result = [];
    let i = 0;
    while (i < len(arr)) {
        push(result, fn(arr[i]));
        i = i + 1;
    }
    return result;
}

// يُبقي فقط العناصر التي تُعيد fn(element) قيمة true من أجلها
fun filterArr(arr, fn) {
    let result = [];
    let i = 0;
    while (i < len(arr)) {
        if (fn(arr[i])) {
            push(result, arr[i]);
        }
        i = i + 1;
    }
    return result;
}

// يُلخّص arr إلى قيمة واحدة عبر تطبيق fn(accumulator, element) تتابعياً بدءاً من initial
fun reduceArr(arr, fn, initial) {
    let acc = initial;
    let i = 0;
    while (i < len(arr)) {
        acc = fn(acc, arr[i]);
        i = i + 1;
    }
    return acc;
}

// ينفّذ fn(element, index) على كل عنصر من أجل تأثير جانبي (side effect) مثل print، بلا نتيجة مُرجعة
fun forEachArr(arr, fn) {
    let i = 0;
    while (i < len(arr)) {
        fn(arr[i], i);
        i = i + 1;
    }
    return nil;
}

// أول عنصر يحقق fn(element) == true، أو nil إن لم يوجد
fun findArr(arr, fn) {
    let i = 0;
    while (i < len(arr)) {
        if (fn(arr[i])) { return arr[i]; }
        i = i + 1;
    }
    return nil;
}

// فهرس أول عنصر يحقق fn(element) == true، أو -1 إن لم يوجد
fun findIndexArr(arr, fn) {
    let i = 0;
    while (i < len(arr)) {
        if (fn(arr[i])) { return i; }
        i = i + 1;
    }
    return -1;
}

// هل كل عناصر arr تحقق fn(element) == true؟
fun everyArr(arr, fn) {
    let i = 0;
    while (i < len(arr)) {
        if (!fn(arr[i])) { return false; }
        i = i + 1;
    }
    return true;
}

// هل يوجد عنصر واحد على الأقل يحقق fn(element) == true؟
fun someArr(arr, fn) {
    let i = 0;
    while (i < len(arr)) {
        if (fn(arr[i])) { return true; }
        i = i + 1;
    }
    return false;
}

// يستدعي fn(i) لكل i من 0 إلى n-1، ويجمع النتائج في مصفوفة (مفيد لتوليد بيانات)
fun timesRun(n, fn) {
    let result = [];
    let i = 0;
    while (i < n) {
        push(result, fn(i));
        i = i + 1;
    }
    return result;
}

// يركّب دالتين: composeTwo(f, g)(x) تُعيد دالة... (Rin لا تدعم إرجاع دوال مجهولة الاسم مباشرة،
// لذا composeApply تُطبّق التركيب فوراً بدل إعادة دالة جديدة): composeApply(f, g, x) = f(g(x))
fun composeApply(f, g, x) {
    return f(g(x));
}

// دوال شرطية جاهزة يمكن تمريرها مباشرة إلى filterArr/everyArr/someArr/findArr
fun isEven(x) {
    return x % 2 == 0;
}

fun isOdd(x) {
    return x % 2 != 0;
}

fun isPositive(x) {
    return x > 0;
}

fun isNegative(x) {
    return x < 0;
}

// يقسّم arr إلى قاموس { yes: [...], no: [...] } حسب fn(element) == true أو false
fun partitionArr(arr, fn) {
    let yes = [];
    let no = [];
    let i = 0;
    while (i < len(arr)) {
        if (fn(arr[i])) {
            push(yes, arr[i]);
        } else {
            push(no, arr[i]);
        }
        i = i + 1;
    }
    return { yes: yes, no: no };
}

// يطبّق fn على كل عنصر (وهي تُعيد مصفوفة فرعية لكل عنصر) ويدمج كل النتائج في مصفوفة واحدة مسطّحة
fun flatMapArr(arr, fn) {
    let result = [];
    let i = 0;
    while (i < len(arr)) {
        let sub = fn(arr[i]);
        let j = 0;
        while (j < len(sub)) {
            push(result, sub[j]);
            j = j + 1;
        }
        i = i + 1;
    }
    return result;
}

// يأخذ عناصر arr من البداية طالما fn(element) == true، ويتوقف عند أول عنصر يفشل الشرط
fun takeWhileArr(arr, fn) {
    let result = [];
    let i = 0;
    while (i < len(arr)) {
        if (!fn(arr[i])) { return result; }
        push(result, arr[i]);
        i = i + 1;
    }
    return result;
}

// يُسقط عناصر arr من البداية طالما fn(element) == true، ويُعيد الباقي بدءاً من أول عنصر يفشل الشرط
fun dropWhileArr(arr, fn) {
    let start = len(arr);
    let i = 0;
    while (i < len(arr)) {
        if (!fn(arr[i])) { start = i; break; }
        i = i + 1;
    }
    let result = [];
    let j = start;
    while (j < len(arr)) {
        push(result, arr[j]);
        j = j + 1;
    }
    return result;
}

// عدد العناصر التي تحقق fn(element) == true
fun countArr(arr, fn) {
    let count = 0;
    let i = 0;
    while (i < len(arr)) {
        if (fn(arr[i])) { count = count + 1; }
        i = i + 1;
    }
    return count;
}
)FUNCTIONALOGRIN";

static const char* kLib_oglang_og_rin = R"OGLANGOGRIN(
// ============================================================================
//  lib/oglang.og.rin — أداة صناعة الحزم (packages) واللغات المصغّرة (mini-languages) فوق Rin
//  استيراد:
//    @import "lib/oglang.og.rin";
//    @import "lib/oglang.og.rin" as og;
//
//  هذه المكتبة قسمان مستقلّان، ولا تحتاج أي تعديل في مترجم Rin نفسه (C++) لتعمل:
//
//  1) توصيف حزمة .og.rin (pkgInfo / pkgHeader / describePkg):
//     أدوات صغيرة لتوليد نفس ترويسة التعليق القياسية المستخدمة في lib/*.og.rin، ولوصف
//     حزمة (اسم/إصدار/وصف/exports) بشكل مقروء — تساعدك عند إنشاء حزمة/مكتبة جديدة خاصة بك.
//
//  2) محرّك لغة مصغّرة عام (rule / langNew / runLine / runProgram):
//     تُعرّف "لغتك المصغّرة" الخاصة كقائمة قواعد — كل قاعدة = كلمة أولى (أمر) + دالة تُنفَّذ
//     عند مطابقتها — ثم تُشغّل "برنامجاً" كاملاً (مصفوفة أسطر نصية) عبر هذه القواعد. بهذا
//     تبني DSL/لغة مصغّرة كاملة (لغة أوامر، لغة تهيئة، لغة قواعد...) فوق Rin مباشرة، دون
//     الحاجة لكتابة محلّل (lexer/parser) جديد بلغة C++.
//
//  مثال سريع (لغة أوامر بأمرين "add" و"greet"):
//    fun onAdd(line, tokens) { return toNumber(tokens[1]) + toNumber(tokens[2]); }
//    fun onGreet(line, tokens) { return "أهلاً يا " + tokens[1]; }
//    fun onUnknown(line, tokens) { return "?? أمر غير معروف: " + line; }
//
//    let myLang = langNew("cmd", [rule("add", onAdd), rule("greet", onGreet)], onUnknown);
//    print runProgram(myLang, ["add 2 3", "greet رنين", "foo bar"]);
//    // -> [5, "أهلاً يا رنين", "?? أمر غير معروف: foo bar"]
// ============================================================================

// ---- الجزء 1: توصيف الحزم (packages) ---------------------------------------

// يبني معلومات حزمة واحدة: الاسم (بلا امتداد، مثل "math")، الإصدار، وصف مختصر،
// ومصفوفة أسماء الدوال المصدَّرة (exports) التي يراها مستورِد الحزمة
fun pkgInfo(name, version, description, exportsArr) {
    return { name: name, version: version, description: description, exports: exportsArr };
}

// يبني نص ترويسة قياسية (تعليق) بنفس أسلوب lib/*.og.rin الحالية، جاهزة للصق أعلى ملف جديد
fun pkgHeader(info) {
    let name = info["name"];
    let lines = [];
    push(lines, "// ============================================================================");
    push(lines, "//  lib/" + name + ".og.rin — " + info["description"]);
    push(lines, "//  الإصدار: " + info["version"]);
    push(lines, "//  استيراد:");
    push(lines, "//    @import \"lib/" + name + ".og.rin\";");
    push(lines, "//    @import \"lib/" + name + ".og.rin\" as " + name + ";");
    push(lines, "// ============================================================================");
    return join(lines, "\n");
}

// وصف مقروء لحزمة (شبيه بمخرجات "npm info")، جاهز للطباعة مباشرة عبر print
fun describePkg(info) {
    let lines = [];
    push(lines, "📦 " + info["name"] + "  (v" + info["version"] + ")");
    push(lines, "   " + info["description"]);
    let exp = info["exports"];
    push(lines, "   exports (" + len(exp) + "):");
    let i = 0;
    while (i < len(exp)) {
        push(lines, "     - " + exp[i]);
        i = i + 1;
    }
    return join(lines, "\n");
}

// ---- الجزء 2: محرّك لغة مصغّرة (mini-language engine) -----------------------

// قاعدة واحدة: كلمة أولى (أمر) تُطابقها + دالة تُنفَّذ عند المطابقة، بتوقيع action(line, tokens)
fun rule(matchWord, action) {
    return { match: matchWord, action: action };
}

// لغة مصغّرة كاملة: اسم + مصفوفة قواعد (تُفحص بالترتيب، أول تطابق يفوز) + دالة احتياطية
// fallback(line, tokens) تُستدعى عندما لا تطابق أي قاعدة أمر السطر
fun langNew(name, rulesArr, fallback) {
    return { name: name, rules: rulesArr, fallback: fallback };
}

// دالة احتياطية جاهزة تُعيد رسالة خطأ نصية عند عدم التعرّف على الأمر؛ مفيدة كقيمة افتراضية لـ langNew
fun unknownCommand(line, tokens) {
    return "?? أمر غير معروف: " + line;
}

// يقسّم سطراً إلى كلمات (tokens)، متجاهلاً الفراغات الزائدة والعناصر الفارغة الناتجة عنها
fun tokenize(line) {
    let raw = split(trim(line), " ");
    let result = [];
    let i = 0;
    while (i < len(raw)) {
        if (trim(raw[i]) != "") {
            push(result, raw[i]);
        }
        i = i + 1;
    }
    return result;
}

// الكلمة الأولى (اسم الأمر) من سطر، أو "" إن كان السطر فارغاً/مسافات فقط
fun commandOf(line) {
    let toks = tokenize(line);
    if (len(toks) == 0) { return ""; }
    return toks[0];
}

// ينفّذ سطراً واحداً عبر لغة lang: يبحث عن أول قاعدة تُطابق أمر السطر وينفّذها،
// وإلا يستدعي fallback(line, tokens) المسجَّلة في اللغة
fun runLine(lang, line) {
    let cmd = commandOf(line);
    let rules = lang["rules"];
    let toks = tokenize(line);
    let i = 0;
    while (i < len(rules)) {
        if (rules[i]["match"] == cmd) {
            let action = rules[i]["action"];
            return action(line, toks);
        }
        i = i + 1;
    }
    let fb = lang["fallback"];
    return fb(line, toks);
}

// ينفّذ "برنامجاً" كاملاً (مصفوفة أسطر نصية) عبر لغة lang، ويتجاهل الأسطر الفارغة تماماً؛
// يُعيد مصفوفة نتائج بنفس ترتيب الأسطر غير الفارغة المُدخَلة
fun runProgram(lang, lines) {
    let results = [];
    let i = 0;
    while (i < len(lines)) {
        if (trim(lines[i]) != "") {
            push(results, runLine(lang, lines[i]));
        }
        i = i + 1;
    }
    return results;
}

// عدد القواعد المسجَّلة في لغة lang (مفيد للتشخيص أو الطباعة عند وصف اللغة المصغّرة)
fun ruleCount(lang) {
    return len(lang["rules"]);
}

// مصفوفة أسماء كل الأوامر المسجَّلة في lang بترتيبها (مفيدة لبناء أمر "help" تلقائي)
fun ruleNames(lang) {
    let result = [];
    let rules = lang["rules"];
    let i = 0;
    while (i < len(rules)) {
        push(result, rules[i]["match"]);
        i = i + 1;
    }
    return result;
}

// هل لغة lang تملك قاعدة مسجَّلة لأمر باسم cmdName تحديداً؟
fun hasRule(lang, cmdName) {
    return contains(ruleNames(lang), cmdName);
}

// يُضيف قاعدة جديدة (matchWord, action) إلى نهاية قواعد lang مباشرة (بما أن الخرائط في
// Rin قيم مُشتركة بالمرجع، هذا يُعدّل lang فعلياً دون حاجة لإعادة إسناده يدوياً من المستدعي)
fun addRule(lang, matchWord, action) {
    push(lang["rules"], rule(matchWord, action));
    return lang;
}

// وصف مقروء للغة مصغّرة (اسمها وعدد قواعدها وأسماء أوامرها)، جاهز للطباعة عبر print
fun describeLang(lang) {
    let lines = [];
    push(lines, "🔤 " + lang["name"] + "  (" + toString(ruleCount(lang)) + " أمر)");
    push(lines, "   الأوامر: " + join(ruleNames(lang), ", "));
    return join(lines, "\n");
}
)OGLANGOGRIN";

static const char* kLib_ringo_og_rin = R"RINGOOGRIN(
// ============================================================================
//  lib/ringo.og.rin — Ringo: لغة ترميز خفيفة بوسوم [tag]، تُصيَّر إلى HTML أو نص عادي
//  استيراد:
//    @import "lib/ringo.og.rin";
//    @import "lib/ringo.og.rin" as ringo;
//
//  مكتبة مدمجة (embedded) داخل ثنائي المحرّك نفسه (راجع rin_stdlib_libs.h) — تعمل عبر
//  @import فوراً على أي جهاز/منصة دون أي خطوة تثبيت إضافية، تماماً كباقي مكتبات lib/*.og.rin.
//
//  صيغة Ringo (BBCode-like، بلا تداخل وسم من نفس النوع داخل نفسه):
//    [b]...[/b]        عريض            -> <strong>
//    [i]...[/i]        مائل            -> <em>
//    [u]...[/u]        تسطير           -> <u>
//    [s]...[/s]        يتوسّطه خط      -> <del>
//    [code]...[/code]  كود مضمّن       -> <code>
//    [quote]...[/quote] اقتباس         -> <blockquote>
//    [h1]/[h2]/[h3]     عناوين         -> <h1>/<h2>/<h3>
//    [color=#hex أو اسم]...[/color]     -> <span style="color:...">
//    [link=URL]...[/link]               -> <a href="URL">
//    [list] [*] عنصر  [*] عنصر [/list] -> <ul><li>...</li>...</ul>
//    [br]  سطر جديد صريح   [hr]  خط فاصل أفقي
//
//  مثال:
//    let src = "[h1]عنوان[/h1]\n[b]مرحباً[/b] يا [color=#7C5CFF]رنين[/color]!\n" +
//              "[list][*]أول[*]ثاني[/list]";
//    print ringoToHtml(src);
//    print ringoToPlain(src);
//
//  ملاحظة (حد معروف v1، بنفس أسلوب توثيق القيود في هذا المشروع): الوسوم لا تتحقق من
//  التطابق أو التداخل — إغلاق وسم بلا فتح مطابق يُصيَّر بأمان (يُطبع وسم HTML المقابل
//  فقط) لكن دون تحقّق صحة كامل؛ القوائم [list] لا تدعم التداخل (قائمة داخل قائمة) بعد.
// ============================================================================

// يحوّل مصدر Ringo إلى مصفوفة "tokens" مسطّحة: كل عنصر إما نص خام، أو وسم فتح/إغلاق،
// أو عنصر قائمة [*]. لا يبني شجرة متداخلة عمداً — التصيير (render) أدناه يتعامل مع
// الترتيب الخطي مباشرة، وهذا يكفي لصياغة BBCode غير متداخلة العناصر من نفس النوع.
fun ringoTokenize(source) {
    let tokens = [];
    let pos = 0;
    let n = len(source);

    while (pos < n) {
        let rest = substr(source, pos);
        let nextBracket = indexOf(rest, "[");

        if (nextBracket == -1) {
            push(tokens, { kind: "text", value: rest });
            pos = n;
        } else {
            if (nextBracket > 0) {
                push(tokens, { kind: "text", value: substr(rest, 0, nextBracket) });
            }

            let tagStart = pos + nextBracket + 1;
            let afterTagStart = substr(source, tagStart);
            let closeBracket = indexOf(afterTagStart, "]");

            if (closeBracket == -1) {
                // "[" بلا "]" مقابل: اعتبر الباقي كله نصاً خاماً (لا يوجد وسم صالح)
                push(tokens, { kind: "text", value: substr(rest, nextBracket) });
                pos = n;
            } else {
                let raw = substr(afterTagStart, 0, closeBracket);
                pos = tagStart + closeBracket + 1;

                if (raw == "*") {
                    push(tokens, { kind: "item" });
                } else if (len(raw) > 0 and charAt(raw, 0) == "/") {
                    push(tokens, { kind: "close", name: lower(trim(substr(raw, 1))) });
                } else {
                    let eq = indexOf(raw, "=");
                    if (eq == -1) {
                        push(tokens, { kind: "open", name: lower(trim(raw)), attr: "" });
                    } else {
                        push(tokens, {
                            kind: "open",
                            name: lower(trim(substr(raw, 0, eq))),
                            attr: trim(substr(raw, eq + 1))
                        });
                    }
                }
            }
        }
    }

    return tokens;
}

// وسم HTML المقابل لفتح وسم Ringo باسم name (ومَعامل attr إن وُجد لـ color/link)؛
// وسم غير معروف يُتجاهَل بصمت (نص فارغ) بدل رمي خطأ، اتساقاً مع فلسفة المكتبة الآمنة
fun ringoHtmlOpenTag(name, attr) {
    if (name == "b") { return "<strong>"; }
    if (name == "i") { return "<em>"; }
    if (name == "u") { return "<u>"; }
    if (name == "s") { return "<del>"; }
    if (name == "code") { return "<code>"; }
    if (name == "quote") { return "<blockquote>"; }
    if (name == "h1") { return "<h1>"; }
    if (name == "h2") { return "<h2>"; }
    if (name == "h3") { return "<h3>"; }
    if (name == "color") { return "<span style=\"color:" + attr + "\">"; }
    if (name == "link") { return "<a href=\"" + attr + "\">"; }
    return "";
}

// وسم HTML الخاص بإغلاق وسم Ringo باسم name — مطابق لـ ringoHtmlOpenTag
fun ringoHtmlCloseTag(name) {
    if (name == "b") { return "</strong>"; }
    if (name == "i") { return "</em>"; }
    if (name == "u") { return "</u>"; }
    if (name == "s") { return "</del>"; }
    if (name == "code") { return "</code>"; }
    if (name == "quote") { return "</blockquote>"; }
    if (name == "h1") { return "</h1>"; }
    if (name == "h2") { return "</h2>"; }
    if (name == "h3") { return "</h3>"; }
    if (name == "color") { return "</span>"; }
    if (name == "link") { return "</a>"; }
    return "";
}

// يهرب أحرف HTML الخاصة داخل نص خام (& أولاً، ثم < > ") حتى لا يُفسَّر كوسم HTML فعلي
fun ringoEscapeHtml(raw) {
    let out = raw;
    out = replace(out, "&", "&amp;");
    out = replace(out, "<", "&lt;");
    out = replace(out, ">", "&gt;");
    out = replace(out, "\"", "&quot;");
    return out;
}

// يحوّل مصدر Ringo كاملاً إلى HTML جاهز للعرض (مثلاً داخل WebView في تطبيق DLoF/RinLang)
fun ringoToHtml(source) {
    let tokens = ringoTokenize(source);
    let out = "";
    let liOpen = false;
    let i = 0;

    while (i < len(tokens)) {
        let t = tokens[i];
        let kind = t["kind"];

        if (kind == "text") {
            let escaped = ringoEscapeHtml(t["value"]);
            out = out + replace(escaped, "\n", "<br>\n");
        } else if (kind == "open") {
            let name = t["name"];
            if (name == "list") {
                out = out + "<ul>\n";
            } else if (name == "br") {
                out = out + "<br>\n";
            } else if (name == "hr") {
                out = out + "<hr>\n";
            } else {
                out = out + ringoHtmlOpenTag(name, t["attr"]);
            }
        } else if (kind == "item") {
            if (liOpen) { out = out + "</li>\n"; }
            out = out + "<li>";
            liOpen = true;
        } else if (kind == "close") {
            let name = t["name"];
            if (name == "list") {
                if (liOpen) { out = out + "</li>\n"; liOpen = false; }
                out = out + "</ul>\n";
            } else {
                out = out + ringoHtmlCloseTag(name);
            }
        }

        i = i + 1;
    }

    return out;
}

// يحوّل مصدر Ringo إلى نص عادي (كل الوسوم تُزال، [*] تصبح "- "، [br]/[hr] تصبح أسطراً)؛
// مفيد للمعاينة السريعة، أو للبحث/الفهرسة داخل نصوص Ringo دون HTML
fun ringoToPlain(source) {
    let tokens = ringoTokenize(source);
    let out = "";
    let i = 0;

    while (i < len(tokens)) {
        let t = tokens[i];
        let kind = t["kind"];

        if (kind == "text") {
            out = out + t["value"];
        } else if (kind == "item") {
            out = out + "\n- ";
        } else if (kind == "open") {
            let name = t["name"];
            if (name == "br") { out = out + "\n"; }
            else if (name == "hr") { out = out + "\n----------\n"; }
        }

        i = i + 1;
    }

    return out;
}

// معلومات وصفية عن المكتبة (اسم/إصدار/وصف/دوال مصدَّرة)، بنفس أسلوب pkgInfo في oglang.og.rin،
// جاهزة للطباعة المباشرة أو للعرض في شاشة "المكتبات" داخل التطبيق
fun ringoInfo() {
    return {
        name: "ringo",
        version: "1.0.0",
        description: "لغة ترميز خفيفة بوسوم [tag] تُصيَّر إلى HTML أو نص عادي",
        exports: ["ringoTokenize", "ringoToHtml", "ringoToPlain", "ringoEscapeHtml", "ringoInfo"]
    };
}
)RINGOOGRIN";

static const char* kLib_langkit_og_rin = R"LANGKITOGRIN(
// ============================================================================
//  lib/langkit.og.rin — عدّة صناعة اللغات (Language Toolkit) فوق Rin
//  استيراد:
//    @import "lib/langkit.og.rin";
//    @import "lib/langkit.og.rin" as lk;
//
//  هذه المكتبة هي الأخ الأكبر لـ lib/oglang.og.rin: بينما oglang.og.rin يبني "لغات أوامر"
//  مصغّرة (سطر = أمر واحد، بلا محلّل حقيقي)، توفّر langkit.og.rin اللبنات الأساسية لبناء
//  لغة برمجة حقيقية كاملة بثلاث مراحل كلاسيكية منفصلة، بنفس مفاهيم أي لغة برمجة حقيقية:
//
//    المصدر (نص) --[Lexer]--> tokens --[Parser]--> AST --[Interpreter/CodeGen]--> نتيجة/كود
//
//  لا تحتاج أي تعديل في مترجم Rin نفسه (C++): كل شيء هنا دوال Rin عادية فوق stdlib الحالية
//  (charAt/substr/len/push/keys/contains...). الاستخدام المُوصى به هو داخل "مشروع لغة" —
//  مجلد يحوي عدة ملفات .rin منفصلة (وليس ملفاً واحداً): Lexer.rin / Parser.rin /
//  Interpreter.rin / CodeGen.rin (اختياري) / manifest.json / syntax.rinsyntax.json / run.rin.
//  راجع templates/customlang/ لقالب جاهز، وexamples/customlang/calc/ لمثال كامل يعمل فعلياً.
// ============================================================================

// ---- الجزء 1: تصنيف المحارف (Character classification) ---------------------
// تُستخدم داخل حلقة Lexer الخاصة بلغتك لفحص كل محرف من المصدر.

fun isDigitChar(ch) {
    let code = ord(ch);
    return code >= ord("0") and code <= ord("9");
}

fun isAlphaChar(ch) {
    let code = ord(ch);
    let isLower = code >= ord("a") and code <= ord("z");
    let isUpper = code >= ord("A") and code <= ord("Z");
    return isLower or isUpper or ch == "_";
}

fun isAlnumChar(ch) {
    return isAlphaChar(ch) or isDigitChar(ch);
}

fun isSpaceChar(ch) {
    return ch == " " or ch == "\t" or ch == "\r";
}

fun isNewlineChar(ch) {
    return ch == "\n";
}

// ---- الجزء 2: الرموز (Tokens) ------------------------------------------------
// tok = { type: "NUMBER"|"IDENT"|"STRING"|"OP"|"KEYWORD"|"EOF"|..., value: "...", line: N }

fun makeToken(type, value, line) {
    return { type: type, value: value, line: line };
}

fun eofToken(line) {
    return makeToken("EOF", "", line);
}

fun tokIs(tok, type) {
    return tok["type"] == type;
}

fun tokIsValue(tok, type, value) {
    return tok["type"] == type and tok["value"] == value;
}

// تمثيل نصي لمصفوفة tokens، مفيد أثناء تطوير/تصحيح Lexer.rin الخاص بلغتك
fun formatTokens(tokens) {
    let lines = [];
    let i = 0;
    while (i < len(tokens)) {
        let t = tokens[i];
        push(lines, "[" + toString(t["line"]) + "] " + t["type"] + " '" + toString(t["value"]) + "'");
        i = i + 1;
    }
    return join(lines, "\n");
}

// ---- الجزء 3: عقد الشجرة التركيبية (AST nodes) ------------------------------
// node = { kind: "BinaryExpr"|"NumberLit"|..., line: N, ...حقول خاصة بالعقدة }
// props هي خريطة الحقول الإضافية الخاصة بنوع العقدة (مثال: {left:..., op:"+", right:...})

fun astNode(kind, line, props) {
    let node = { kind: kind, line: line };
    let ks = keys(props);
    let i = 0;
    while (i < len(ks)) {
        node[ks[i]] = props[ks[i]];
        i = i + 1;
    }
    return node;
}

fun nodeIs(node, kind) {
    return node["kind"] == kind;
}

// طباعة شجرة AST بشكل هرمي مقروء لأغراض التصحيح (لا تفترض شكلاً معيناً للحقول،
// فقط تطبع كل مفتاح في العقدة؛ العقد الفرعية المتداخلة تُمرَّر يدوياً عبر childKeys)
fun formatAstShallow(node) {
    let ks = keys(node);
    let lines = [];
    push(lines, "(" + node["kind"] + ")");
    let i = 0;
    while (i < len(ks)) {
        if (ks[i] != "kind") {
            push(lines, "  ." + ks[i] + " = " + toString(node[ks[i]]));
        }
        i = i + 1;
    }
    return join(lines, "\n");
}

// ---- الجزء 4: أخطاء موحّدة عبر مراحل اللغة (Lexer/Parser/Interpreter) -------
// بدلاً من كل مرحلة تخترع صيغة خطأ خاصة بها، عقدة/قيمة خطأ موحّدة تفهمها كل مرحلة تالية

fun langError(stage, message, line) {
    return { kind: "LangError", stage: stage, message: message, line: line };
}

fun isLangError(value) {
    if (value == nil) { return false; }
    if (has(value, "kind") == false) { return false; }
    return value["kind"] == "LangError";
}

fun formatLangError(err) {
    return "[" + err["stage"] + " error][line " + toString(err["line"]) + "] " + err["message"];
}

// ---- الجزء 4.1: قيمة نتيجة آمنة (Result) — بديل isLangError على قيم غير خرائط -----
// has()/keys() في Rin يفشلان إن مُرِّرت لهما قيمة ليست خريطة (رقم/نص/منطقي)، وقيم
// evalExpr/genExpr الناجحة غالباً أرقام أو نصوص خام، لا خرائط. لذا كل دالة قد تفشل
// (evalExpr, genExpr, execStatement...) يجب أن تُعيد دوماً خريطة Result عبر ok()/err()
// بدل قيمة خام مباشرة، حتى يبقى فحص النجاح آمناً دوماً عبر isOk() بلا استثناء أبداً.

fun ok(value) {
    return { ok: true, value: value };
}

fun err(langErrorObj) {
    return { ok: false, error: langErrorObj };
}

fun isOk(result) {
    return result["ok"];
}

// يستخرج langError الجاهز للطباعة من نتيجة فاشلة (isOk(result) == false)
fun resultError(result) {
    return result["error"];
}

// ---- الجزء 5: مؤشّر أسطر عام لمحلّل نازل بالتكرار (Parser cursor helpers) ---
// لأن Rin يمرّر القيم بالقيمة، تُعيد هذه الدوال دوماً خريطة {value: ..., pos: ...}
// كي يحدّث المستدعي متغيّر pos الخاص به يدوياً: let r = pAdvance(toks,pos); pos = r["pos"];

fun pAtEnd(tokens, pos) {
    return pos >= len(tokens) or tokIs(tokens[pos], "EOF");
}

fun pPeek(tokens, pos) {
    if (pos >= len(tokens)) { return eofToken(0); }
    return tokens[pos];
}

fun pCheck(tokens, pos, type) {
    if (pAtEnd(tokens, pos)) { return false; }
    return tokIs(pPeek(tokens, pos), type);
}

fun pCheckValue(tokens, pos, type, value) {
    if (pAtEnd(tokens, pos)) { return false; }
    return tokIsValue(pPeek(tokens, pos), type, value);
}

// يستهلك الرمز الحالي بلا شرط، ويُعيد {tok: الرمز المستهلَك, pos: الموضع التالي}
fun pAdvance(tokens, pos) {
    let t = pPeek(tokens, pos);
    if (pAtEnd(tokens, pos)) { return { tok: t, pos: pos }; }
    return { tok: t, pos: pos + 1 };
}

// إن طابق الرمز الحالي type يستهلكه (match)، وإلا يبني langError عبر expect()
fun pExpect(tokens, pos, type, stage) {
    if (pCheck(tokens, pos, type)) {
        return pAdvance(tokens, pos);
    }
    let got = pPeek(tokens, pos);
    return {
        tok: langError(stage, "متوقَّع '" + type + "' لكن وُجد '" + got["type"] + " (" + toString(got["value"]) + ")'", got["line"]),
        pos: pos
    };
}

// ---- الجزء 6: تفريغ/تحميل مانِفست مشروع لغة (manifest.json) ----------------
// manifest.json لأي مشروع لغة مخصصة يصف: id/name/version/developer/fileExtension/
// entry (أسماء ملفات Lexer/Parser/Interpreter/CodeGen)/description/official

fun loadLanguageManifest(projectDir) {
    let raw = readFile(projectDir + "/manifest.json");
    return jsonDecode(raw);
}

fun manifestField(manifest, key, defaultValue) {
    if (has(manifest, key)) { return manifest[key]; }
    return defaultValue;
}

// ---- الجزء 7: توصيف لغة (نفس روح pkgInfo في oglang.og.rin) -----------------

fun languageInfo(id, name, version, developer, fileExtension, description) {
    return {
        id: id,
        name: name,
        version: version,
        developer: developer,
        fileExtension: fileExtension,
        description: description
    };
}

fun describeLanguage(info) {
    let lines = [];
    push(lines, "🧩 " + info["name"] + "  (." + info["fileExtension"] + ")  v" + info["version"]);
    push(lines, "   " + info["description"]);
    push(lines, "   المطوّر: " + info["developer"]);
    return join(lines, "\n");
}

// ---- الجزء 8: تركيب نتائج Result (monadic-style pipeline helpers) ----------
// evalExpr/genExpr وأي دالة تتبع أسلوب ok()/err() تحتاج غالباً سلسلة خطوات متتالية:
// كل خطوة تعمل فقط إن نجحت السابقة، وأول فشل يُوقف السلسلة فوراً وتُمرَّر رسالة
// خطأه كما هي حتى النهاية دون أي تكرار يدوي لفحص isOk() في كل مرحلة.

// إن كانت result ناجحة، يطبّق fn(result["value"]) عليها ويُغلّف الناتج بـ ok() تلقائياً؛
// وإلا يُعيد result كما هي (الخطأ يمرّ دون تغيير). يعادل map() على Result في اللغات الوظيفية
fun mapResult(result, fn) {
    if (isOk(result)) {
        return ok(fn(result["value"]));
    }
    return result;
}

// إن كانت result ناجحة، يستدعي fn(result["value"]) التي يجب أن تُعيد Result أخرى بنفسها
// (لا تُغلَّف تلقائياً)؛ مفيد لتسلسل خطوات قد تفشل كل منها بشكل مستقل (evalLeft ثم evalRight...).
// يعادل andThen/bind على Result في اللغات الوظيفية
fun andThen(result, fn) {
    if (isOk(result)) {
        return fn(result["value"]);
    }
    return result;
}

// يستخرج result["value"] إن كانت ناجحة، وإلا defaultValue عند الفشل (بدل التحقق يدوياً
// من isOk() ثم resultError() في كل موضع استدعاء)
fun unwrapOr(result, defaultValue) {
    if (isOk(result)) { return result["value"]; }
    return defaultValue;
}

// ---- الجزء 9: مساعدات إضافية لمؤشّر التوكِنز (Parser cursor) ----------------

// هل نوع الرمز الحالي واحد من مصفوفة types؟ (مفيد لتحليل "أي من عدة عمليات بنفس
// الأسبقية" دون سلسلة pCheck يدوية طويلة، مثال: pCheckAny(toks,pos,["PLUS","MINUS"]))
fun pCheckAny(tokens, pos, types) {
    let i = 0;
    while (i < len(types)) {
        if (pCheck(tokens, pos, types[i])) { return true; }
        i = i + 1;
    }
    return false;
}

// إن طابق الرمز الحالي type يستهلكه، وإلا لا يفعل شيئاً (بعكس pExpect لا يبني خطأ
// أبداً)؛ يُعيد دوماً {tok: الرمز المستهلَك أو الحالي بلا استهلاك, pos: الموضع الجديد}
// مفيد لعناصر نحوية اختيارية مثل فاصلة زائدة أخيرة أو ";" اختيارية آخر السطر
fun pOptional(tokens, pos, type) {
    if (pCheck(tokens, pos, type)) {
        return pAdvance(tokens, pos);
    }
    return { tok: pPeek(tokens, pos), pos: pos };
}
)LANGKITOGRIN";


static const char* kLib_astwalk_og_rin = R"ASTWALKOGRIN(
// ============================================================================
//  lib/astwalk.og.rin — طواف وزيارة شجرة AST (visitor pattern) لمفسّر/مولّد كود لغتك
//  استيراد:
//    @import "lib/astwalk.og.rin";
//    @import "lib/astwalk.og.rin" as walk;
//
//  lib/langkit.og.rin توفّر formatAstShallow (طباعة عقدة واحدة بلا نزول لأبنائها، لأن
//  شكل الحقول يختلف حسب kind). هذه المكتبة تضيف نمط "visitor": جدول توزيع (dispatch
//  table) يربط kind بدالة معالجة خاصة به، ودالة visit() تختار المعالج المناسب تلقائياً
//  — نفس الفكرة التي يعمل بها أي evalExpr/genExpr حقيقي (switch كبير على node.kind)
//  لكن بشكل جدول بيانات بدل سلسلة if/else طويلة يدوية.
//
//  مثال سريع:
//    fun onLit(node) { return node["value"]; }
//    fun onBin(node) { return evalExpr(node["left"]) + evalExpr(node["right"]); } // تبسيط
//    let handlers = dispatchTable([["Literal", onLit], ["BinaryExpr", onBin]]);
//    fun onUnknown(node) { return langError("eval", "نوع عقدة غير مدعوم: " + node["kind"], node["line"]); }
//    print visit(someNode, handlers, onUnknown);
//
//    // عدّ/طباعة الشجرة بعمق: مرّر أسماء الحقول التي تحوي عقدة فرعية واحدة (singleKeys)
//    // منفصلة عن أسماء الحقول التي تحوي مصفوفة عقد (listKeys):
//    print countNodesDeep(program, ["left", "right", "operand"], ["statements", "args"]);
//    print formatAstDeep(program, ["left", "right", "operand"], ["statements", "args"]);
//
//  ملاحظة: formatAstDeep/formatAstDeepInto تستخدمان repeatStr() من lib/strings.og.rin
//  لبناء المسافة البادئة، لذا استورد lib/strings.og.rin أيضاً إن أردت استخدامهما.
// ============================================================================

// يبني جدول توزيع من مصفوفة أزواج [kind, handlerFn]
fun dispatchTable(pairs) {
    let table = {};
    let i = 0;
    while (i < len(pairs)) {
        table[pairs[i][0]] = pairs[i][1];
        i = i + 1;
    }
    return table;
}

// يستدعي المعالج المناسب لـ node["kind"] من table ويُمرّر له node، أو يستدعي
// fallbackFn(node) إن لم يوجد معالج مسجَّل لهذا النوع (بدل توقّف بخطأ غامض)
fun visit(node, table, fallbackFn) {
    let kind = node["kind"];
    if (has(table, kind)) {
        let handler = table[kind];
        return handler(node);
    }
    return fallbackFn(node);
}

// يطبّق visit على كل عقدة من مصفوفة nodes (مفيد لزيارة قائمة statements في جسم دالة/برنامج)
// ويجمع نتائج كل زيارة في مصفوفة يُعيدها
fun visitAll(nodes, table, fallbackFn) {
    let results = [];
    let i = 0;
    while (i < len(nodes)) {
        push(results, visit(nodes[i], table, fallbackFn));
        i = i + 1;
    }
    return results;
}

// هل يوجد معالج مسجَّل لنوع kind في جدول التوزيع table؟ (فحص مسبق قبل visit عند
// الحاجة لتفرّع منطقي مختلف بدل الاعتماد فقط على fallbackFn)
fun dispatchHas(table, kind) {
    return has(table, kind);
}

// يعدّ العقد داخل شجرة AST بعمق كامل. لأن keys()/has() في Rin يفشلان على قيمة ليست
// خريطة، ولا توجد دالة isArray/isMap لتمييز شكل حقل فرعي في وقت التشغيل، تفصل هذه
// الدالة صراحة بين نوعين من الحقول بدل تخمين شكلها:
//   singleKeys: أسماء حقول تحوي عقدة فرعية واحدة أو nil (مثل "left"/"right"/"operand")
//   listKeys:   أسماء حقول تحوي مصفوفة عقد فرعية أو nil (مثل "args"/"statements"/"body")
// يستخدم مكدّساً (stack كمصفوفة) بدل استدعاء متكرر لأن أشكال العقد تختلف بحرّية
fun countNodesDeep(root, singleKeys, listKeys) {
    let stack = [root];
    let count = 0;
    while (len(stack) > 0) {
        let node = pop(stack);
        if (node != nil) {
            count = count + 1;
            let i = 0;
            while (i < len(singleKeys)) {
                let key = singleKeys[i];
                if (has(node, key)) {
                    let child = node[key];
                    if (child != nil) { push(stack, child); }
                }
                i = i + 1;
            }
            i = 0;
            while (i < len(listKeys)) {
                let key = listKeys[i];
                if (has(node, key)) {
                    let childArr = node[key];
                    if (childArr != nil) {
                        let j = 0;
                        while (j < len(childArr)) {
                            push(stack, childArr[j]);
                            j = j + 1;
                        }
                    }
                }
                i = i + 1;
            }
        }
    }
    return count;
}

// يطبع شجرة AST كاملة بعمق مع مسافات بادئة تعكس المستوى، بنفس مفهوم singleKeys/listKeys
// في countNodesDeep. يُعيد نصاً متعدد الأسطر جاهزاً للطباعة (print) أو الحفظ في ملف
fun formatAstDeep(root, singleKeys, listKeys) {
    let lines = [];
    formatAstDeepInto(root, singleKeys, listKeys, 0, lines);
    return join(lines, "\n");
}

// دالة مساعدة داخلية لـ formatAstDeep: تملأ lines (مصفوفة) بتمثيل node ثم أبنائه
// بشكل متكرر (recursion)، بمسافة بادئة تتناسب مع depth
fun formatAstDeepInto(node, singleKeys, listKeys, depth, lines) {
    if (node == nil) { return nil; }
    let indent = repeatStr("  ", depth);
    push(lines, indent + "(" + node["kind"] + ")");
    let i = 0;
    while (i < len(singleKeys)) {
        let key = singleKeys[i];
        if (has(node, key)) {
            let child = node[key];
            if (child != nil) {
                formatAstDeepInto(child, singleKeys, listKeys, depth + 1, lines);
            }
        }
        i = i + 1;
    }
    i = 0;
    while (i < len(listKeys)) {
        let key = listKeys[i];
        if (has(node, key)) {
            let childArr = node[key];
            if (childArr != nil) {
                let j = 0;
                while (j < len(childArr)) {
                    formatAstDeepInto(childArr[j], singleKeys, listKeys, depth + 1, lines);
                    j = j + 1;
                }
            }
        }
        i = i + 1;
    }
    return nil;
}

// يجمع كل العقد من النوع kind الموجودة داخل شجرة root في أي عمق (بحث بالعرض عبر
// مكدّس)، ويُعيدها كمصفوفة بترتيب اكتشافها. مفيد لتحليلات مثل "أعطني كل الاستدعاءات
// CallExpr في البرنامج" دون كتابة تكرار متخصّص لكل نوع بحث
fun findNodes(root, kind, singleKeys, listKeys) {
    let stack = [root];
    let found = [];
    while (len(stack) > 0) {
        let node = pop(stack);
        if (node != nil) {
            if (node["kind"] == kind) {
                push(found, node);
            }
            let i = 0;
            while (i < len(singleKeys)) {
                let key = singleKeys[i];
                if (has(node, key)) {
                    let child = node[key];
                    if (child != nil) { push(stack, child); }
                }
                i = i + 1;
            }
            i = 0;
            while (i < len(listKeys)) {
                let key = listKeys[i];
                if (has(node, key)) {
                    let childArr = node[key];
                    if (childArr != nil) {
                        let j = 0;
                        while (j < len(childArr)) {
                            push(stack, childArr[j]);
                            j = j + 1;
                        }
                    }
                }
                i = i + 1;
            }
        }
    }
    return found;
}
)ASTWALKOGRIN";

static const char* kLib_envkit_og_rin = R"ENVKITOGRIN(
// ============================================================================
//  lib/envkit.og.rin — بيئة تنفيذ (Environment / نطاقات متداخلة) لمفسّر لغتك
//  استيراد:
//    @import "lib/envkit.og.rin";
//    @import "lib/envkit.og.rin" as envkit;
//
//  أي مفسّر (Interpreter.rin) للغة حقيقية يحتاج نطاقات متغيّرات متداخلة: نطاق برنامج
//  عام (global scope)، ونطاق فرعي جديد لكل استدعاء دالة أو كتلة { ... } يبحث أولاً في
//  نفسه ثم يصعد لأبيه إن لم يجد المتغيّر. لأن الخرائط في Rin قيم مُشتركة بالمرجع، فإن
//  envDefine/envSet تُعدّلان النطاق مباشرة دون حاجة لإعادته وإعادة إسناده يدوياً.
//
//  مثال سريع:
//    let global = envNew(nil);
//    envDefine(global, "x", 10);
//    let local = envChild(global);
//    envDefine(local, "y", 20);
//    print envGet(local, "x");   // 10 (وُجدت في نطاق الأب)
//    envSet(local, "x", 99);     // يُعدّل x في نطاق الأب لأنها معرَّفة هناك، لا في local
//    print envGet(global, "x");  // 99
// ============================================================================

// يبني نطاقاً جديداً؛ parentEnv هو النطاق الأب أو nil للنطاق العام الجذري
fun envNew(parentEnv) {
    return { vars: {}, parent: parentEnv };
}

// يبني نطاقاً فرعياً أبوه parentEnv (اختصار لـ envNew(parentEnv))، يُستخدم عند دخول
// كتلة { ... } جديدة أو تنفيذ جسم دالة
fun envChild(parentEnv) {
    return envNew(parentEnv);
}

// يُعرّف متغيّراً جديداً باسم name وقيمة value في نطاق env نفسه تحديداً (بلا صعود للأب)،
// حتى لو كان متغيّر بنفس الاسم معرَّفاً بالفعل في نطاق أب (يُظلّله shadowing، كما let عادية)
fun envDefine(env, name, value) {
    env["vars"][name] = value;
    return value;
}

// هل name معرَّف في env نفسه تحديداً (بلا صعود للأب)؟
fun envHasOwn(env, name) {
    return has(env["vars"], name);
}

// هل name معرَّف في env أو أي نطاق أب له (صعوداً حتى الجذر)؟
fun envHas(env, name) {
    let current = env;
    while (current != nil) {
        if (envHasOwn(current, name)) { return true; }
        current = current["parent"];
    }
    return false;
}

// يقرأ قيمة name بالبحث في env ثم الصعود للآباء عند اللزوم. يُعيد خريطة نتيجة بأسلوب
// langkit ({ok:true,value:...} أو {ok:false,error:...}) بدل توقّف بخطأ غامض عند عدم الوجود
fun envGet(env, name) {
    let current = env;
    while (current != nil) {
        if (envHasOwn(current, name)) {
            return { ok: true, value: current["vars"][name] };
        }
        current = current["parent"];
    }
    return { ok: false, error: "envGet: المتغيّر غير معرَّف: " + name };
}

// يحدّث قيمة name الموجودة مسبقاً في env أو أحد آبائه (يُعدّل أقرب نطاق يملكها فعلياً).
// إن لم يكن name معرَّفاً في أي نطاق، لا يُنشئه تلقائياً بل يُعيد false (استخدم envDefine
// للإنشاء الصريح، تماماً كفارق "x = 5" عن "let x = 5" في Rin نفسها)
fun envSet(env, name, value) {
    let current = env;
    while (current != nil) {
        if (envHasOwn(current, name)) {
            current["vars"][name] = value;
            return true;
        }
        current = current["parent"];
    }
    return false;
}

// عمق النطاق الحالي عن الجذر (0 للنطاق العام نفسه، 1 لأول نطاق فرعي، وهكذا)
fun envDepth(env) {
    let depth = 0;
    let current = env["parent"];
    while (current != nil) {
        depth = depth + 1;
        current = current["parent"];
    }
    return depth;
}

// أسماء كل المتغيّرات المرئية من env (نطاقه + كل آبائه)، بلا تكرار، الأقرب أولاً
fun envVisibleNames(env) {
    let result = [];
    let current = env;
    while (current != nil) {
        let names = keys(current["vars"]);
        let i = 0;
        while (i < len(names)) {
            if (contains(result, names[i]) == false) {
                push(result, names[i]);
            }
            i = i + 1;
        }
        current = current["parent"];
    }
    return result;
}

// أسماء المتغيّرات المعرَّفة في env نفسه تحديداً فقط (بلا صعود للآباء)
fun envOwnNames(env) {
    return keys(env["vars"]);
}

// نطاق الجذر (الأب الأبعد بلا parent) الذي ينتمي إليه env — أي النطاق العام الحقيقي
fun envRoot(env) {
    let current = env;
    while (current["parent"] != nil) {
        current = current["parent"];
    }
    return current;
}

// دلالة الإسناد "=" العادية: يحدّث name في أقرب نطاق يملكها فعلاً عبر envSet، وإن لم
// تكن معرَّفة في أي نطاق يُعرّفها بدلاً من ذلك في env الحالي نفسه عبر envDefine
// (بخلاف envSet وحدها التي تُعيد false بصمت دون أي تأثير عند عدم الوجود)
fun envSetOrDefine(env, name, value) {
    if (envSet(env, name, value)) { return value; }
    return envDefine(env, name, value);
}
)ENVKITOGRIN";

static const char* kLib_gridkit_og_rin = R"GRIDKITOGRIN(
// ============================================================================
//  lib/gridkit.og.rin — حلقات متداخلة على شبكات ثنائية الأبعاد (2D grids / matrices)
//  استيراد:
//    @import "lib/gridkit.og.rin";
//    @import "lib/gridkit.og.rin" as grid;
//
//  الشبكة هنا مصفوفة صفوف، كل صف مصفوفة قيم: grid[row][col]. تُغلّف هذه المكتبة نمط
//  الحلقة المزدوجة "while (row) { while (col) { ... } }" المتكرر عند التعامل مع
//  مصفوفات ثنائية الأبعاد (لوحات ألعاب، مصفوفات رياضية، شاشات نصية...).
//
//  مثال سريع:
//    let g = makeGrid(3, 3, 0);
//    setCell(g, 1, 1, 9);
//    fun show(value, r, c) { print toString(r) + "," + toString(c) + " = " + toString(value); }
//    forEachCell(g, show);
// ============================================================================

// ينشئ شبكة بحجم rows×cols وكل خلاياها تساوي fillValue
fun makeGrid(rows, cols, fillValue) {
    let g = [];
    let r = 0;
    while (r < rows) {
        let row = [];
        let c = 0;
        while (c < cols) {
            push(row, fillValue);
            c = c + 1;
        }
        push(g, row);
        r = r + 1;
    }
    return g;
}

// عدد الصفوف
fun gridRows(g) {
    return len(g);
}

// عدد الأعمدة (بحسب أول صف؛ يفترض أن كل الصفوف بنفس الطول)
fun gridCols(g) {
    if (len(g) == 0) { return 0; }
    return len(g[0]);
}

// هل (row, col) داخل حدود الشبكة؟
fun gridInBounds(g, row, col) {
    if (row < 0 or row >= gridRows(g)) { return false; }
    if (col < 0 or col >= gridCols(g)) { return false; }
    return true;
}

// قراءة خلية بأمان: تُعيد fallback إن كانت (row, col) خارج الحدود بدل توقف بخطأ
fun getCell(g, row, col, fallback) {
    if (gridInBounds(g, row, col) == false) { return fallback; }
    return g[row][col];
}

// كتابة خلية بأمان: لا تفعل شيئاً إن كانت (row, col) خارج الحدود، وإلا تُعدّل الشبكة
// مباشرة بالمرجع (المصفوفات في Rin مُشتركة بالمرجع) وتُعيد true للنجاح
fun setCell(g, row, col, value) {
    if (gridInBounds(g, row, col) == false) { return false; }
    g[row][col] = value;
    return true;
}

// يستدعي fn(value, row, col) على كل خلية بترتيب صف فصف من اليسار لليمين (بلا قيمة مُرجعة)
fun forEachCell(g, fn) {
    let r = 0;
    while (r < gridRows(g)) {
        let c = 0;
        while (c < gridCols(g)) {
            fn(g[r][c], r, c);
            c = c + 1;
        }
        r = r + 1;
    }
    return nil;
}

// يبني شبكة جديدة بنفس الأبعاد حيث كل خلية = fn(value, row, col) المطبَّقة على الأصلية
fun mapGrid(g, fn) {
    let result = [];
    let r = 0;
    while (r < gridRows(g)) {
        let newRow = [];
        let c = 0;
        while (c < gridCols(g)) {
            push(newRow, fn(g[r][c], r, c));
            c = c + 1;
        }
        push(result, newRow);
        r = r + 1;
    }
    return result;
}

// ينقل (transpose) الشبكة: يصبح الصف عموداً والعكس
fun transposeGrid(g) {
    let rows = gridRows(g);
    let cols = gridCols(g);
    let result = makeGrid(cols, rows, nil);
    let r = 0;
    while (r < rows) {
        let c = 0;
        while (c < cols) {
            result[c][r] = g[r][c];
            c = c + 1;
        }
        r = r + 1;
    }
    return result;
}

// يُسطّح الشبكة إلى مصفوفة واحدة بُعدية بترتيب صف فصف
fun flattenGrid(g) {
    let result = [];
    let r = 0;
    while (r < gridRows(g)) {
        let c = 0;
        while (c < gridCols(g)) {
            push(result, g[r][c]);
            c = c + 1;
        }
        r = r + 1;
    }
    return result;
}

// جيران أربعة اتجاهات (فوق/تحت/يسار/يمين) داخل حدود الشبكة فقط، كمصفوفة {row,col}
fun neighbors4(g, row, col) {
    let candidates = [
        { row: row - 1, col: col },
        { row: row + 1, col: col },
        { row: row, col: col - 1 },
        { row: row, col: col + 1 }
    ];
    let result = [];
    let i = 0;
    while (i < len(candidates)) {
        let p = candidates[i];
        if (gridInBounds(g, p["row"], p["col"])) {
            push(result, p);
        }
        i = i + 1;
    }
    return result;
}
)GRIDKITOGRIN";

static const char* kLib_iterkit_og_rin = R"ITERKITOGRIN(
// ============================================================================
//  lib/iterkit.og.rin — مكرِّرات (iterators) بنمط hasNext/next فوق المصفوفات
//  استيراد:
//    @import "lib/iterkit.og.rin";
//    @import "lib/iterkit.og.rin" as iter;
//
//  المكرِّر هنا خريطة عادية { data: array, pos: number }. ولأن الخرائط في Rin قيم
//  مُشتركة بالمرجع (على عكس الأرقام/النصوص التي تُنسخ بالقيمة)، فإن تعديل it["pos"]
//  بداخل أي دالة من هذه المكتبة يبقى مرئياً لدى المستدعي مباشرة، دون الحاجة لإعادة
//  الخريطة وإعادة إسنادها يدوياً (كما تفعل lib/langkit.og.rin مع مؤشر pos الخام).
//
//  مثال سريع:
//    let it = iterNew([10, 20, 30]);
//    while (iterHasNext(it)) {
//        print iterNext(it);   // 10 ثم 20 ثم 30
//    }
// ============================================================================

// يبني مكرّراً جديداً يبدأ من أول عنصر في arr
fun iterNew(arr) {
    return { data: arr, pos: 0 };
}

// هل تبقّى عنصر واحد على الأقل لم يُزَر بعد؟
fun iterHasNext(it) {
    return it["pos"] < len(it["data"]);
}

// يُعيد العنصر الحالي بلا تقدّم (أو nil إن انتهى المكرّر)
fun iterPeek(it) {
    if (iterHasNext(it) == false) { return nil; }
    return it["data"][it["pos"]];
}

// يُعيد العنصر الحالي ويُقدّم المكرّر خطوة واحدة (يُعدّل it مباشرة بالمرجع)
fun iterNext(it) {
    let value = iterPeek(it);
    if (iterHasNext(it)) {
        it["pos"] = it["pos"] + 1;
    }
    return value;
}

// عدد العناصر المتبقية التي لم تُزَر بعد
fun iterRemaining(it) {
    let left = len(it["data"]) - it["pos"];
    if (left < 0) { return 0; }
    return left;
}

// يُعيد المكرّر إلى بدايته من جديد (يُعدّل it مباشرة)
fun iterReset(it) {
    it["pos"] = 0;
    return it;
}

// يتخطّى n عنصر دفعة واحدة (يتوقف عند نهاية البيانات دون خطأ إن كان n أكبر من المتبقي)
fun iterSkip(it, n) {
    let target = it["pos"] + n;
    let dataLen = len(it["data"]);
    if (target > dataLen) { target = dataLen; }
    if (target < it["pos"]) { target = it["pos"]; }
    it["pos"] = target;
    return it;
}

// يستهلك كل ما تبقّى من المكرّر ويُعيده كمصفوفة عادية (المكرّر يصبح فارغاً بعدها)
fun iterToArray(it) {
    let result = [];
    while (iterHasNext(it)) {
        push(result, iterNext(it));
    }
    return result;
}

// يستهلك كل ما تبقّى مستدعياً fn(value, index) على كل عنصر (index يبدأ من 0 لكل استدعاء)
fun iterForEach(it, fn) {
    let i = 0;
    while (iterHasNext(it)) {
        fn(iterNext(it), i);
        i = i + 1;
    }
    return nil;
}

// يبني مكرّراً جديداً (مستقلاً) يمرّ فقط على العناصر التي تحقق fn(element) == true من it
// الحالي فصاعداً — يستهلك it الأصلي بالكامل في هذه العملية
fun iterFilterToArray(it, fn) {
    let result = [];
    while (iterHasNext(it)) {
        let value = iterNext(it);
        if (fn(value)) {
            push(result, value);
        }
    }
    return result;
}

// يستهلك حتى n عنصر فقط من it (أو أقل إن انتهى المكرّر أولاً) ويُعيدها كمصفوفة
fun iterTake(it, n) {
    let result = [];
    let i = 0;
    while (i < n and iterHasNext(it)) {
        push(result, iterNext(it));
        i = i + 1;
    }
    return result;
}

// يستهلك كل ما تبقّى من it ويُعيد عدد العناصر التي تحقق fn(element) == true
fun iterCount(it, fn) {
    let count = 0;
    while (iterHasNext(it)) {
        if (fn(iterNext(it))) { count = count + 1; }
    }
    return count;
}

// يطبّق fn على كل عنصر من العناصر المتبقية في it ويُعيد النتائج كمصفوفة جديدة
// (لا يُعدّل arr المصدر؛ يستهلك it بالكامل)
fun iterMapToArray(it, fn) {
    let result = [];
    while (iterHasNext(it)) {
        push(result, fn(iterNext(it)));
    }
    return result;
}
)ITERKITOGRIN";

static const char* kLib_lexkit_og_rin = R"LEXKITOGRIN(
// ============================================================================
//  lib/lexkit.og.rin — لبنات محرّك Lexer عام قابل لإعادة الاستخدام لصناعة لغتك
//  استيراد:
//    @import "lib/lexkit.og.rin";
//    @import "lib/lexkit.og.rin" as lex;
//
//  تُكمّل lib/langkit.og.rin (التي توفّر تصنيف محارف مفردة + بناء tok واحد) بأدوات على
//  مستوى "مصدر اللغة كاملاً": جدول كلمات مفتاحية (لتمييز IDENT عن KEYWORD)، جدول
//  عمليات (operators) مع مطابقة أطول تطابق (longest match) بحيث "==" لا تُقرأ كعلامتي
//  "=" منفصلتين، ومؤشر عام على نص المصدر (source cursor) لتخطّي الفراغات والتعليقات.
//  يُستخدم عادة مع lib/langkit.og.rin داخل حلقة lexer الرئيسية لملف Lexer.rin الخاص بلغتك.
//
//  مثال سريع:
//    let kw = newKeywordTable(["let", "if", "else", "while", "fun"]);
//    print classifyWord("if", kw);      // "KEYWORD"
//    print classifyWord("total", kw);   // "IDENT"
//
//    let ops = newOperatorTable(["==", "!=", "<=", ">=", "+", "-", "*", "/", "=", "<", ">"]);
//    print matchLongestOp("== 3", 0, ops); // { matched: "==", length: 2 }
// ============================================================================

// ---- جدول الكلمات المفتاحية -------------------------------------------------

// يبني جدول كلمات مفتاحية من مصفوفة نصوص، كل كلمة تُصبح مفتاحاً بقيمة true
fun newKeywordTable(words) {
    let table = {};
    let i = 0;
    while (i < len(words)) {
        table[words[i]] = true;
        i = i + 1;
    }
    return table;
}

// يُعيد "KEYWORD" إن كانت word موجودة في الجدول، وإلا identType (عادة "IDENT")
fun classifyWord(word, table, identType) {
    if (has(table, word)) { return "KEYWORD"; }
    return identType;
}

// ---- جدول العمليات (operators) مع مطابقة أطول تطابق -------------------------

// يبني جدول عمليات من مصفوفة رموز نصية (["==", "!=", "+", ...]) ويُرتّبها من الأطول
// إلى الأقصر داخلياً كي تُختبر "==" قبل "=" عند المطابقة (وإلا ستُقتطع خطأً كعامل مفرد)
fun newOperatorTable(symbols) {
    let sorted = [];
    let i = 0;
    while (i < len(symbols)) {
        push(sorted, symbols[i]);
        i = i + 1;
    }
    // فرز إدراج تنازلي حسب الطول (الأطول أولاً)؛ الجداول عادة قصيرة فلا مشكلة أداء
    let a = 1;
    while (a < len(sorted)) {
        let current = sorted[a];
        let b = a - 1;
        while (b >= 0 and len(sorted[b]) < len(current)) {
            sorted[b + 1] = sorted[b];
            b = b - 1;
        }
        sorted[b + 1] = current;
        a = a + 1;
    }
    return sorted;
}

// يبحث عن أطول رمز عملية من opTable يطابق بداية source ابتداءً من pos، ويُعيد
// { matched: الرمز المطابَق, length: طوله } أو { matched: "", length: 0 } إن لم يطابق شيء
fun matchLongestOp(source, pos, opTable) {
    let i = 0;
    while (i < len(opTable)) {
        let symbol = opTable[i];
        let symLen = len(symbol);
        if (pos + symLen <= len(source)) {
            if (substr(source, pos, symLen) == symbol) {
                return { matched: symbol, length: symLen };
            }
        }
        i = i + 1;
    }
    return { matched: "", length: 0 };
}

// ---- مؤشّر مصدر عام (source cursor) -----------------------------------------
// عكس pAdvance في langkit (الذي يتحرك فوق tokens جاهزة)، هذه الدوال تتحرك فوق نص
// المصدر الخام قبل أي تقطيع إلى tokens

// هل وصل pos لنهاية source؟
fun sAtEnd(source, pos) {
    return pos >= len(source);
}

// المحرف الحالي بلا تقدّم، أو "" إن انتهى المصدر
fun sPeek(source, pos) {
    if (sAtEnd(source, pos)) { return ""; }
    return charAt(source, pos);
}

// نظرة على المحرف التالي (lookahead بمقدار 1)، أو "" إن لم يوجد
fun sPeekNext(source, pos) {
    if (pos + 1 >= len(source)) { return ""; }
    return charAt(source, pos + 1);
}

// يتخطّى الفراغات (مسافة/تبويب/سطر جديد) والتعليقات أحادية السطر التي تبدأ بـ
// lineCommentStart (مثل "//")، ويُعيد الموضع الجديد بعد كل ما تمّ تخطّيه
fun skipWhitespaceAndComments(source, pos, lineCommentStart) {
    let p = pos;
    let commentLen = len(lineCommentStart);
    let continueSkip = true;
    while (continueSkip) {
        continueSkip = false;
        while (sAtEnd(source, p) == false and (sPeek(source, p) == " " or sPeek(source, p) == "\t" or sPeek(source, p) == "\r" or sPeek(source, p) == "\n")) {
            p = p + 1;
        }
        if (commentLen > 0 and p + commentLen <= len(source)) {
            if (substr(source, p, commentLen) == lineCommentStart) {
                while (sAtEnd(source, p) == false and sPeek(source, p) != "\n") {
                    p = p + 1;
                }
                continueSkip = true;
            }
        }
    }
    return p;
}

// يستهلك كل المحارف المتتالية التي تحقق fn(ch) == true بدءاً من pos، ويُعيد
// { matched: النص المُستهلَك, pos: الموضع بعده } (يُستخدم لقراءة أرقام/معرّفات كاملة
// بالاعتماد على isDigitChar/isAlnumChar من lib/langkit.og.rin كدالة fn)
fun consumeWhile(source, pos, fn) {
    let start = pos;
    let p = pos;
    while (sAtEnd(source, p) == false and fn(sPeek(source, p))) {
        p = p + 1;
    }
    return { matched: substr(source, start, p - start), pos: p };
}

// ---- مساعدات إضافية للمؤشّر العام ------------------------------------------

// يستهلك المحرف الحالي بلا شرط (بنفس روح pAdvance في langkit لكن فوق نص خام)،
// ويُعيد { ch: المحرف المستهلَك أو "" إن انتهى المصدر, pos: الموضع التالي }
fun sAdvance(source, pos) {
    let c = sPeek(source, pos);
    if (sAtEnd(source, pos)) { return { ch: c, pos: pos }; }
    return { ch: c, pos: pos + 1 };
}

// إن كان المحرف الحالي يساوي expected تحديداً يستهلكه، وإلا لا يفعل شيئاً؛ يُعيد
// { matched: true/false, pos: الموضع الجديد }. مفيد لاستهلاك محرف مفرد اختياري
// (مثل "!" قبل "=" عند تمييز "!=" عن "!")
fun sMatch(source, pos, expected) {
    if (sAtEnd(source, pos)) { return { matched: false, pos: pos }; }
    if (sPeek(source, pos) == expected) {
        return { matched: true, pos: pos + 1 };
    }
    return { matched: false, pos: pos };
}

// رقم السطر (1-based) الذي يقع فيه الموضع pos داخل source، بعدّ محارف "\n" السابقة
// له؛ يُستخدم لملء حقل line في makeToken بدل تتبّع عدّاد سطر يدوي منفصل أثناء اللَكْس
fun lineAt(source, pos) {
    let limit = pos;
    if (limit > len(source)) { limit = len(source); }
    let line = 1;
    let i = 0;
    while (i < limit) {
        if (charAt(source, i) == "\n") { line = line + 1; }
        i = i + 1;
    }
    return line;
}
)LEXKITOGRIN";

static const char* kLib_loopkit_og_rin = R"LOOPKITOGRIN(
// ============================================================================
//  lib/loopkit.og.rin — تحكّم عام بالحلقات (loop control primitives) فوق while/for
//  استيراد:
//    @import "lib/loopkit.og.rin";
//    @import "lib/loopkit.og.rin" as loop;
//
//  دوال جاهزة لأنماط حلقات متكررة: تكرار بعدد ثابت، تكرار بشرط توقف مع حدّ أقصى أمان
//  (لمنع حلقة لا نهائية)، إعادة محاولة حتى النجاح، حلقة تنازلية، وحلقة بخطوة مخصّصة.
//  كل الدوال هنا تأخذ دالة fn كوسيط (Rin يدعم الدوال كقيم من الدرجة الأولى) وتُطبّقها
//  داخل حلقة while واحدة، بدل تكرار نفس صيغة "let i = 0; while (...) { ... i = i+1; }"
//  يدوياً في كل مكان من برنامجك.
//
//  مثال سريع:
//    fun printIt(i) { print "خطوة " + toString(i); }
//    repeatTimes(3, printIt);           // خطوة 0 / خطوة 1 / خطوة 2
//    print stepLoopCollect(0, 10, 2, printIt); // [0,2,4,6,8] (طبعت كل قيمة أيضاً)
// ============================================================================

// ينفّذ fn(i) بالضبط n مرة، من i=0 حتى n-1 (بلا قيمة مُرجعة، للتأثير الجانبي فقط)
fun repeatTimes(n, fn) {
    let i = 0;
    while (i < n) {
        fn(i);
        i = i + 1;
    }
    return nil;
}

// حلقة تنازلية: ينفّذ fn(i) بدءاً من "from" نزولاً حتى 1 شاملة (from, from-1, ..., 1)
fun countdown(from, fn) {
    let i = from;
    while (i >= 1) {
        fn(i);
        i = i - 1;
    }
    return nil;
}

// حلقة بخطوة مخصّصة (تعمّم حلقة for الكلاسيكية): ينفّذ fn(i) لأجل
// i = start, start+step, ... طالما (step > 0 و i < endExclusive) أو (step < 0 و i > endExclusive)
// step يجب ألا يساوي صفراً وإلا تُعاد قيمة خطأ نصية بدل الدخول بحلقة لا نهائية
fun stepLoop(start, endExclusive, step, fn) {
    if (step == 0) { return "stepLoop: step لا يجوز أن يساوي صفراً"; }
    let i = start;
    if (step > 0) {
        while (i < endExclusive) {
            fn(i);
            i = i + step;
        }
    } else {
        while (i > endExclusive) {
            fn(i);
            i = i + step;
        }
    }
    return nil;
}

// نفس stepLoop لكن يجمع نتائج fn(i) في مصفوفة ويُعيدها (مفيد عند إرادة القيم لا فقط التأثير)
fun stepLoopCollect(start, endExclusive, step, fn) {
    let result = [];
    if (step == 0) { return result; }
    let i = start;
    if (step > 0) {
        while (i < endExclusive) {
            push(result, fn(i));
            i = i + step;
        }
    } else {
        while (i > endExclusive) {
            push(result, fn(i));
            i = i + step;
        }
    }
    return result;
}

// حلقة "حتى تحقق الشرط" مع حدّ أقصى آمن للتكرارات: تستدعي fn(attempt) بدءاً من attempt=0
// وتتوقف حين تُعيد fn قيمة true، أو عند بلوغ maxIters (أيهما أولاً). تُعيد خريطة توضّح
// النتيجة، بعكس حلقة while عادية قد لا تتوقف أبداً لو نُسي تحديث شرطها
fun loopUntil(fn, maxIters) {
    let i = 0;
    while (i < maxIters) {
        if (fn(i)) {
            return { done: true, iterations: i + 1 };
        }
        i = i + 1;
    }
    return { done: false, iterations: maxIters };
}

// إعادة محاولة عملية قد تفشل حتى maxAttempts مرة: fn(attempt) يجب أن تُعيد خريطة نتيجة
// بأسلوب langkit ({ok:true,value:...} أو {ok:false,error:...})، وتتوقف retryUntil عند
// أول نجاح أو بعد استنفاد المحاولات (وعندها تُعيد آخر نتيجة فاشلة كما هي)
fun retryUntil(fn, maxAttempts) {
    let attempt = 0;
    let lastResult = { ok: false, error: "retryUntil: لم تُنفَّذ أي محاولة (maxAttempts <= 0)" };
    while (attempt < maxAttempts) {
        lastResult = fn(attempt);
        if (lastResult["ok"]) {
            return lastResult;
        }
        attempt = attempt + 1;
    }
    return lastResult;
}

// حلقة while عامة: تستدعي condFn() قبل كل دورة، وطالما أعادت true تستدعي bodyFn()
// وتجمع ناتجها في مصفوفة تُعيدها في النهاية. يفصل شرط التوقف عن جسم الحلقة بدل خلطهما
fun whileCollect(condFn, bodyFn) {
    let result = [];
    while (condFn()) {
        push(result, bodyFn());
    }
    return result;
}
)LOOPKITOGRIN";

static const char* kLib_loopstats_og_rin = R"LOOPSTATSOGRIN(
// ============================================================================
//  lib/loopstats.og.rin — تجميع إحصاءات وتقدّم بشكل تدريجي أثناء تنفيذ حلقة
//  استيراد:
//    @import "lib/loopstats.og.rin";
//    @import "lib/loopstats.og.rin" as stats;
//
//  دوال math.og.rin (mean/stddev...) تحتاج مصفوفة كاملة جاهزة مسبقاً. هذه المكتبة
//  بالمقابل مخصّصة لحلقات "تدفّق" (streaming) حيث تصلك القيم واحدة تلو الأخرى ولا تريد
//  تخزينها كلها أولاً: مُجمِّع إحصاء تراكمي (عدّاد/مجموع/متوسط/أصغر/أكبر يتحدّث مع كل
//  قيمة جديدة)، عدّاد تكرارات حسب مفتاح (tally/histogram)، وشريط تقدّم نصّي بسيط.
//
//  مثال سريع:
//    let s = runningStatsNew();
//    let i = 0;
//    while (i < 5) { runningStatsAdd(s, i * 2); i = i + 1; }
//    print s;  // { count:5, sum:20, mean:4, min:0, max:8 }
// ============================================================================

// ---- إحصاء تراكمي (running stats) ------------------------------------------

// يبني مُجمِّعاً تراكمياً فارغاً
fun runningStatsNew() {
    return { count: 0, sum: 0, mean: 0, min: nil, max: nil };
}

// يُضيف قيمة جديدة للمُجمِّع s ويُحدّث count/sum/mean/min/max فوراً (يُعدّل s بالمرجع،
// ويُعيده أيضاً للراحة عند الاستخدام المتسلسل)
fun runningStatsAdd(s, value) {
    s["count"] = s["count"] + 1;
    s["sum"] = s["sum"] + value;
    s["mean"] = s["sum"] / s["count"];
    if (s["min"] == nil or value < s["min"]) { s["min"] = value; }
    if (s["max"] == nil or value > s["max"]) { s["max"] = value; }
    return s;
}

// يُطبّق runningStatsAdd على كل عناصر arr بحلقة واحدة، ويُعيد المُجمِّع النهائي
fun runningStatsFromArray(arr) {
    let s = runningStatsNew();
    let i = 0;
    while (i < len(arr)) {
        runningStatsAdd(s, arr[i]);
        i = i + 1;
    }
    return s;
}

// ---- عدّاد تكرارات حسب مفتاح (tally / histogram) ----------------------------

// يبني عدّاداً فارغاً (خريطة مفتاح -> عدد مرات ظهوره)
fun tallyNew() {
    return {};
}

// يزيد عدّاد key بمقدار واحد (أو ينشئه بقيمة 1 إن لم يكن موجوداً). يُعدّل t بالمرجع
fun tallyAdd(t, key) {
    if (has(t, key)) {
        t[key] = t[key] + 1;
    } else {
        t[key] = 1;
    }
    return t;
}

// عدد مرات ظهور key حتى الآن (0 إن لم يظهر بعد)
fun tallyGet(t, key) {
    if (has(t, key)) { return t[key]; }
    return 0;
}

// يُحوّل العدّاد إلى مصفوفة {key, count} مرتّبة تنازلياً حسب count (الأكثر تكراراً أولاً)
fun tallyToSortedArray(t) {
    let ks = keys(t);
    let entries = [];
    let i = 0;
    while (i < len(ks)) {
        push(entries, { key: ks[i], count: t[ks[i]] });
        i = i + 1;
    }
    // فرز فقاعي بسيط تنازلياً حسب count (المصفوفات صغيرة عادة في هذا الاستخدام)
    let n = len(entries);
    let a = 0;
    while (a < n) {
        let b = 0;
        while (b < n - a - 1) {
            if (entries[b]["count"] < entries[b + 1]["count"]) {
                let tmp = entries[b];
                entries[b] = entries[b + 1];
                entries[b + 1] = tmp;
            }
            b = b + 1;
        }
        a = a + 1;
    }
    return entries;
}

// المفتاح الأكثر تكراراً حتى الآن، أو nil إن كان العدّاد فارغاً
fun tallyMostCommon(t) {
    let sorted = tallyToSortedArray(t);
    if (len(sorted) == 0) { return nil; }
    return sorted[0]["key"];
}

// ---- شريط تقدّم نصّي --------------------------------------------------------

// يبني نصّاً مثل "[####------] 40% (4/10)" يمثّل تقدّم current من أصل total
fun progressBar(current, total, width) {
    let ratio = 0;
    if (total > 0) { ratio = current / total; }
    if (ratio > 1) { ratio = 1; }
    if (ratio < 0) { ratio = 0; }
    let filled = round(ratio * width);
    let bar = "";
    let i = 0;
    while (i < width) {
        if (i < filled) {
            bar = bar + "#";
        } else {
            bar = bar + "-";
        }
        i = i + 1;
    }
    let percent = round(ratio * 100);
    return "[" + bar + "] " + toString(percent) + "% (" + toString(current) + "/" + toString(total) + ")";
}
)LOOPSTATSOGRIN";

static const char* kLib_parsekit_og_rin = R"PARSEKITOGRIN(
// ============================================================================
//  lib/parsekit.og.rin — لبنات محلِّل (Parser) بأسلوب أسبقية العمليات (precedence climbing)
//  استيراد:
//    @import "lib/parsekit.og.rin";
//    @import "lib/parsekit.og.rin" as parse;
//
//  تُكمّل lib/langkit.og.rin (التي توفّر مؤشّر tokens: pPeek/pAdvance/pExpect...) بأدوات
//  خاصة بتحليل التعبيرات (expressions) ذات أولويات عمليات مختلفة (مثال: * قبل +). توفّر
//  جدول أسبقية قابلاً للتخصيص، ومُنشِئات عقد AST قياسية لتعبيرات ثنائية/أحادية/تجميعية،
//  ودالة "طيّ" (fold) تحوّل نتائج حلقة تحليل مسطّحة (عامل، معامل، عامل، معامل...) إلى
//  شجرة تعبير يسارية الترابط (left-associative) بلا حاجة لاستدعاء متكرر معقّد.
//
//  مثال سريع (طيّ 1 + 2 * لاحقاً... عادة يُبنى الطرف الأيمن بأسبقية أعلى قبل الطيّ):
//    let ops = precTable([["+", 1], ["-", 1], ["*", 2], ["/", 2]]);
//    print precOf(ops, "*", 0);   // 2
//    print precOf(ops, "?", 0);   // 0  (عملية غير معروفة -> الافتراضي)
//
//    let tree = foldBinaryLeft(1, [{ op: "+", right: 2 }, { op: "+", right: 3 }]);
//    // يكافئ (1 + 2) + 3 كشجرة AST متداخلة
// ============================================================================

// ---- جدول أسبقية العمليات (precedence table) --------------------------------

// يبني جدول أسبقية من مصفوفة أزواج [رمز_العملية, رتبة_الأسبقية] (رتبة أعلى = تُنفَّذ أولاً)
fun precTable(pairs) {
    let table = {};
    let i = 0;
    while (i < len(pairs)) {
        table[pairs[i][0]] = pairs[i][1];
        i = i + 1;
    }
    return table;
}

// رتبة أسبقية op في الجدول، أو defaultPrec إن لم تكن op معرَّفة فيه
fun precOf(table, op, defaultPrec) {
    if (has(table, op)) { return table[op]; }
    return defaultPrec;
}

// ---- مُنشِئات عقد AST لتعبيرات (expression nodes) ----------------------------
// نفس روح astNode في langkit لكن بحقول جاهزة خاصة بأنواع تعبير شائعة، بلا حاجة لتمرير
// خريطة props في كل استدعاء

fun litNode(value, line) {
    return { kind: "Literal", value: value, line: line };
}

fun identNode(name, line) {
    return { kind: "Identifier", name: name, line: line };
}

fun unaryNode(op, operand, line) {
    return { kind: "UnaryExpr", op: op, operand: operand, line: line };
}

fun binNode(op, left, right, line) {
    return { kind: "BinaryExpr", op: op, left: left, right: right, line: line };
}

fun groupNode(inner, line) {
    return { kind: "GroupExpr", inner: inner, line: line };
}

fun callNode(callee, args, line) {
    return { kind: "CallExpr", callee: callee, args: args, line: line };
}

// وصول لخاصية/حقل بنمط obj.prop: object هي عقدة التعبير الأساسي، property اسم نصي
fun memberNode(object, property, line) {
    return { kind: "MemberExpr", object: object, property: property, line: line };
}

// وصول بفهرس بنمط obj[expr]: indexExpr عقدة تعبير كاملة (وليست اسماً نصياً ثابتاً)
fun indexNode(object, indexExpr, line) {
    return { kind: "IndexExpr", object: object, index: indexExpr, line: line };
}

// إسناد بنمط target = value (target عادة عقدة Identifier أو Member/IndexExpr)
fun assignNode(target, value, line) {
    return { kind: "AssignExpr", target: target, value: value, line: line };
}

// تعبير ثلاثي شرطي بنمط cond ? thenExpr : elseExpr
fun ternaryNode(cond, thenExpr, elseExpr, line) {
    return { kind: "TernaryExpr", cond: cond, thenBranch: thenExpr, elseBranch: elseExpr, line: line };
}

// حرفي مصفوفة [e1, e2, ...]: elements مصفوفة عقد تعبير
fun arrayLitNode(elements, line) {
    return { kind: "ArrayLit", elements: elements, line: line };
}

// حرفي قاموس {k1: e1, k2: e2, ...}: pairs مصفوفة أزواج [مفتاح_نصي, عقدة_تعبير]
fun mapLitNode(pairs, line) {
    return { kind: "MapLit", pairs: pairs, line: line };
}

// ---- طيّ نتائج حلقة تحليل مسطّحة إلى شجرة يسارية الترابط -------------------
// نمط شائع جداً عند تحليل تعبير بعمليات ثنائية بنفس الأسبقية داخل حلقة while واحدة:
// تُحلَّل أول عامل (firstOperand)، ثم تُجمَع أزواج {op, right} تباعاً أثناء حلقة while
// طالما رمز العملية التالي معروفاً، ثم تُطوى النتيجة أخيراً بهذه الدالة إلى شجرة واحدة
// نظير: ((( firstOperand op1 right1 ) op2 right2 ) op3 right3 ) ...
fun foldBinaryLeft(firstOperand, opRightPairs) {
    let tree = firstOperand;
    let i = 0;
    while (i < len(opRightPairs)) {
        let pair = opRightPairs[i];
        tree = binNode(pair["op"], tree, pair["right"], 0);
        i = i + 1;
    }
    return tree;
}

// نفس foldBinaryLeft لكن يسمح بتمرير رقم سطر لكل عقدة (بدل 0 دوماً)، لرسائل خطأ أدقّ.
// opRightPairs كل عنصر فيها {op, right, line}
fun foldBinaryLeftWithLines(firstOperand, opRightPairs) {
    let tree = firstOperand;
    let i = 0;
    while (i < len(opRightPairs)) {
        let pair = opRightPairs[i];
        tree = binNode(pair["op"], tree, pair["right"], pair["line"]);
        i = i + 1;
    }
    return tree;
}
)PARSEKITOGRIN";

static const char* kLib_runkit_og_rin = R"RUNKITOGRIN(
// ============================================================================
//  lib/runkit.og.rin — تشغيل ملفات/أسطر لغتك المخصّصة وبناء تقرير REPL موحّد
//  استيراد:
//    @import "lib/runkit.og.rin";
//    @import "lib/runkit.og.rin" as run;
//
//  آخر حلقة الفريق (Lexer -> Parser -> Interpreter من lib/langkit.og.rin): تشغيل ملف
//  اللغة الجديدة فعلياً سطراً بسطر أو دفعة واحدة، وتجميع نتيجة موحّدة (نجاح/فشل لكل سطر)
//  بدل أن يكتب كل مشروع لغة منطق REPL وتنسيق الأخطاء من الصفر. runFn التي تُمرَّر لدوال
//  هذه المكتبة هي دالة تشغيل سطر واحد من مشروعك (عادة: Lexer.rin + Parser.rin +
//  Interpreter.rin مجتمعين)، ويجب أن تُعيد دوماً خريطة نتيجة بأسلوب langkit
//  ({ok:true,value:...} أو {ok:false,error:langErrorObj}).
//
//  مثال سريع:
//    fun runOneLine(line) { return ok(evalSource(line)); }  // مبسّط، عادة تستدعي lexer/parser
//    let report = runLines(["1 + 2;", "print x;"], runOneLine);
//    print formatRunReport(report);
//
//  ملاحظة: formatRunReport تستخدم formatLangError() من lib/langkit.og.rin، لذا استورد
//  lib/langkit.og.rin أيضاً (النتائج التي تُنتجها runFn يجب أن تتبع شكل ok()/err() منها).
// ============================================================================

// ينفّذ runFn(line) على كل سطر من lines بالترتيب، ويجمع لكل سطر { line, lineNumber,
// result } في مصفوفة، بلا توقّف عند أول فشل (خلافاً لبرنامج حقيقي، مفيد لتشخيص كل
// أخطاء ملف اختبار دفعة واحدة بدل تصحيحها خطأً خطأً)
fun runLines(lines, runFn) {
    let entries = [];
    let i = 0;
    while (i < len(lines)) {
        let result = runFn(lines[i]);
        push(entries, { line: lines[i], lineNumber: i + 1, result: result });
        i = i + 1;
    }
    return entries;
}

// مثل runLines لكن يتوقّف فوراً عند أول سطر فاشل (result["ok"] == false)، ويُعيد
// خريطة { entries: ما نُفِّذ حتى التوقف, stoppedEarly: true/false }
fun runLinesUntilError(lines, runFn) {
    let entries = [];
    let i = 0;
    let stoppedEarly = false;
    while (i < len(lines) and stoppedEarly == false) {
        let result = runFn(lines[i]);
        push(entries, { line: lines[i], lineNumber: i + 1, result: result });
        if (result["ok"] == false) {
            stoppedEarly = true;
        }
        i = i + 1;
    }
    return { entries: entries, stoppedEarly: stoppedEarly };
}

// يقرأ ملف مصدر بالكامل عبر readFile ثم يشغّله سطراً بسطر (تقسيم بالسطر الجديد \n)
// عبر runLines، مفيد لتشغيل ملف اختبار كامل بمشروع لغة (راجع lib/langkit.og.rin
// لتحميل manifest.json، وexamples/customlang/calc/ لمثال تشغيل حقيقي)
fun runFile(path, runFn) {
    let source = readFile(path);
    let lines = split(source, "\n");
    return runLines(lines, runFn);
}

// عدد الأسطر الناجحة داخل تقرير أنتجته runLines/runLinesUntilError["entries"]
fun countSucceeded(entries) {
    let count = 0;
    let i = 0;
    while (i < len(entries)) {
        if (entries[i]["result"]["ok"]) {
            count = count + 1;
        }
        i = i + 1;
    }
    return count;
}

// عدد الأسطر الفاشلة داخل تقرير
fun countFailed(entries) {
    return len(entries) - countSucceeded(entries);
}

// مصفوفة الإدخالات الفاشلة فقط من التقرير (كل عنصر { line, lineNumber, result })
fun failedEntries(entries) {
    let result = [];
    let i = 0;
    while (i < len(entries)) {
        if (entries[i]["result"]["ok"] == false) {
            push(result, entries[i]);
        }
        i = i + 1;
    }
    return result;
}

// يبني نصاً موجزاً متعدد الأسطر يلخّص تقرير تشغيل: عدد الناجح/الفاشل، ثم كل خطأ
// برقم سطره ورسالته (عبر formatLangError من lib/langkit.og.rin على كل result["error"])
fun formatRunReport(entries) {
    let lines = [];
    push(lines, "نجح: " + toString(countSucceeded(entries)) + " / فشل: " + toString(countFailed(entries)) + " / الإجمالي: " + toString(len(entries)));
    let failed = failedEntries(entries);
    let i = 0;
    while (i < len(failed)) {
        let entry = failed[i];
        push(lines, "  سطر " + toString(entry["lineNumber"]) + ": " + entry["line"]);
        push(lines, "    -> " + formatLangError(entry["result"]["error"]));
        i = i + 1;
    }
    return join(lines, "\n");
}

// نسبة النجاح المئوية (0 إن كان التقرير فارغاً بدل قسمة على صفر)
fun successRate(entries) {
    if (len(entries) == 0) { return 0; }
    return (countSucceeded(entries) / len(entries)) * 100;
}

// مصفوفة كل result["value"] للأسطر الناجحة فقط (يتجاهل الفاشلة تماماً بصمت)، مفيدة
// لتجميع نتائج تقييم برنامج كامل كمصفوفة قيم جاهزة دون التعامل مع خريطة entry الكاملة
fun succeededValues(entries) {
    let result = [];
    let i = 0;
    while (i < len(entries)) {
        let r = entries[i]["result"];
        if (r["ok"]) {
            push(result, r["value"]);
        }
        i = i + 1;
    }
    return result;
}

// نسخة مطوّلة من formatRunReport تطبع كل سطر (ناجحاً كان أو فاشلاً) بدل الفاشل فقط،
// مفيدة أثناء تطوير مشروع اللغة نفسه لمراجعة كل نتيجة سطراً بسطر
fun formatRunReportVerbose(entries) {
    let lines = [];
    push(lines, "نجح: " + toString(countSucceeded(entries)) + " / فشل: " + toString(countFailed(entries)) + " / الإجمالي: " + toString(len(entries)));
    let i = 0;
    while (i < len(entries)) {
        let entry = entries[i];
        let r = entry["result"];
        if (r["ok"]) {
            push(lines, "  ✅ سطر " + toString(entry["lineNumber"]) + ": " + entry["line"] + " -> " + toString(r["value"]));
        } else {
            push(lines, "  ❌ سطر " + toString(entry["lineNumber"]) + ": " + entry["line"]);
            push(lines, "     -> " + formatLangError(r["error"]));
        }
        i = i + 1;
    }
    return join(lines, "\n");
}
)RUNKITOGRIN";

static const char* kLib_seqkit_og_rin = R"SEQKITOGRIN(
// ============================================================================
//  lib/seqkit.og.rin — توليد متتاليات جاهزة كمدخلات لحلقات for/while
//  استيراد:
//    @import "lib/seqkit.og.rin";
//    @import "lib/seqkit.og.rin" as seq;
//
//  lib/data.og.rin توفّر range(n)/rangeFrom(start,end) بخطوة ثابتة تساوي 1 فقط. هذه
//  المكتبة تكمّلها بمتتاليات بخطوة مخصّصة (موجبة أو سالبة)، متتاليات هندسية، وتكرار/
//  تدوير مصفوفة بأكملها — مفيدة كمصدر بيانات جاهز تُمرَّر إلى حلقة for أو forEachArr.
//
//  مثال سريع:
//    print rangeStep(0, 10, 2);      // [0,2,4,6,8]
//    print rangeStep(10, 0, -2);     // [10,8,6,4,2]
//    print linspace(0, 1, 5);        // [0, 0.25, 0.5, 0.75, 1]
// ============================================================================

// مصفوفة [start, start+step, ...] طالما (step>0 و القيمة < endExclusive) أو
// (step<0 و القيمة > endExclusive). step=0 يُعيد مصفوفة فارغة بدل حلقة لا نهائية
fun rangeStep(start, endExclusive, step) {
    let result = [];
    if (step == 0) { return result; }
    let i = start;
    if (step > 0) {
        while (i < endExclusive) {
            push(result, i);
            i = i + step;
        }
    } else {
        while (i > endExclusive) {
            push(result, i);
            i = i + step;
        }
    }
    return result;
}

// n قيمة موزّعة بانتظام بين start وend شاملَين الطرفين (يشمل fromValue وtoValue معاً).
// عند n<=1 تُعيد [start] فقط
fun linspace(start, endValue, n) {
    let result = [];
    if (n <= 1) {
        push(result, start);
        return result;
    }
    let step = (endValue - start) / (n - 1);
    let i = 0;
    while (i < n) {
        push(result, start + (step * i));
        i = i + 1;
    }
    return result;
}

// متتالية هندسية: n حداً بدءاً من "first" وكل حد يساوي السابق × ratio
fun geometricSeq(first, ratio, n) {
    let result = [];
    let current = first;
    let i = 0;
    while (i < n) {
        push(result, current);
        current = current * ratio;
        i = i + 1;
    }
    return result;
}

// مصفوفة من n نسخة من نفس القيمة (مفيد كقيمة ابتدائية لتراكم في حلقة)
fun repeatValue(value, n) {
    let result = [];
    let i = 0;
    while (i < n) {
        push(result, value);
        i = i + 1;
    }
    return result;
}

// يُكرّر محتوى arr بأكمله times مرة متتالية: cycleArr([1,2],3) -> [1,2,1,2,1,2]
fun cycleArr(arr, times) {
    let result = [];
    let t = 0;
    while (t < times) {
        let i = 0;
        while (i < len(arr)) {
            push(result, arr[i]);
            i = i + 1;
        }
        t = t + 1;
    }
    return result;
}

// يمدّد أو يقتطع arr إلى طول targetLen بالضبط: يُكرّر عناصره إن كان أقصر، أو يقتطعه
// إن كان أطول (مفيد لمزامنة طول مصفوفتين قبل حلقة تُعالجهما معاً عنصراً بعنصر)
fun cycleToLength(arr, targetLen) {
    let result = [];
    if (len(arr) == 0) { return result; }
    let i = 0;
    while (len(result) < targetLen) {
        push(result, arr[i % len(arr)]);
        i = i + 1;
    }
    return result;
}

// عدد صحيح عشوائي بين lo وhi ضمناً (يعتمد على random() المبني في اللغة، والذي يُعيد
// كسراً عشرياً بين 0 و1)
fun randomInt(lo, hi) {
    let span = hi - lo + 1;
    return lo + floor(random() * span);
}

// يخلط ترتيب عناصر arr عشوائياً (خوارزمية Fisher–Yates) ويُعيد مصفوفة جديدة دون
// تعديل الأصل
fun shuffleArr(arr) {
    let result = [];
    let i = 0;
    while (i < len(arr)) {
        push(result, arr[i]);
        i = i + 1;
    }
    let n = len(result);
    i = n - 1;
    while (i > 0) {
        let j = randomInt(0, i);
        let tmp = result[i];
        result[i] = result[j];
        result[j] = tmp;
        i = i - 1;
    }
    return result;
}

// يختار n عنصر عشوائي بلا تكرار من arr (n لا يتجاوز طول arr؛ يُقتطع تلقائياً إن كان أكبر)
fun sampleArr(arr, n) {
    let shuffled = shuffleArr(arr);
    if (n > len(shuffled)) { n = len(shuffled); }
    let result = [];
    let i = 0;
    while (i < n) {
        push(result, shuffled[i]);
        i = i + 1;
    }
    return result;
}
)SEQKITOGRIN";

static const char* kLib_bob_og_rin = R"BOBOGRIN(
// ============================================================================
//  lib/bob.og.rin — Bob: لغة ترميز خفيفة بأسطر بادئة (Markdown-lite)، تُصيَّر إلى HTML أو نص عادي
//  استيراد:
//    @import "lib/bob.og.rin";
//    @import "lib/bob.og.rin" as bob;
//
//  مكتبة مدمجة (embedded) داخل ثنائي المحرّك نفسه (راجع rin_stdlib_libs.h) — تعمل عبر
//  @import فوراً على أي جهاز/منصة دون أي خطوة تثبيت إضافية، تماماً كباقي مكتبات lib/*.og.rin.
//
//  صيغة Bob (سطرية على مستوى الكتلة block، ورموز بسيطة على مستوى السطر inline):
//    # عنوان     -> <h1>       ## عنوان -> <h2>      ### عنوان -> <h3>
//    > اقتباس    -> <blockquote>
//    - عنصر      -> <li> (عناصر متتالية تُجمَع تلقائياً داخل <ul> واحدة)
//    ---         -> <hr>  (سطر يحوي "---" فقط)
//    سطر عادي    -> <p>
//    **عريض**    -> <strong>        *مائل*    -> <em>
//    `كود`       -> <code>          [نص](URL) -> <a href="URL">نص</a>
//
//  مثال:
//    let src = "# عنوان\n" +
//              "مرحباً يا **رنين**! هذا *مائل* و`كود` وزيارة [الموقع](https://example.com).\n" +
//              "- أول\n- ثاني\n" +
//              "> اقتباس قصير\n" +
//              "---\n";
//    print bobToHtml(src);
//    print bobToPlain(src);
//
//  ملاحظة (حد معروف v1، بنفس أسلوب توثيق القيود في هذا المشروع): لا تداخل بين رموز
//  inline من نفس النوع (مثال: **عريض فيه **عريض آخر** بالخطأ**)، ولا قوائم مرقّمة أو
//  متداخلة بعد؛ كل سطر يُصنَّف ككتلة واحدة فقط حسب بادئته الأولى.
// ============================================================================

// أدنى مساعد نصي: هل يبدأ s بالسابقة prefix؟ (لا توجد startsWith مدمجة في core Rin)
fun bobStartsWith(s, prefix) {
    if (len(s) < len(prefix)) { return false; }
    return substr(s, 0, len(prefix)) == prefix;
}

// يهرب أحرف HTML الخاصة داخل نص خام (& أولاً، ثم < > ") حتى لا يُفسَّر كوسم HTML فعلي
fun bobEscapeHtml(raw) {
    let out = raw;
    out = replace(out, "&", "&amp;");
    out = replace(out, "<", "&lt;");
    out = replace(out, ">", "&gt;");
    out = replace(out, "\"", "&quot;");
    return out;
}

// يقسّم مصدر Bob إلى مصفوفة "كتل" (blocks) بحسب بادئة كل سطر: عنوان/اقتباس/عنصر
// قائمة/خط فاصل/فقرة نصية عادية. الأسطر الفارغة تُتجاهَل (تُستخدَم كفواصل فقرات فقط).
fun bobTokenize(source) {
    let rawLines = split(source, "\n");
    let blocks = [];
    let i = 0;

    while (i < len(rawLines)) {
        let trimmed = trim(rawLines[i]);

        if (trimmed == "") {
            // سطر فارغ: فاصل فقرات بلا كتلة خاصة به
        } else if (trimmed == "---") {
            push(blocks, { kind: "hr", content: "" });
        } else if (bobStartsWith(trimmed, "### ")) {
            push(blocks, { kind: "h3", content: trim(substr(trimmed, 4)) });
        } else if (bobStartsWith(trimmed, "## ")) {
            push(blocks, { kind: "h2", content: trim(substr(trimmed, 3)) });
        } else if (bobStartsWith(trimmed, "# ")) {
            push(blocks, { kind: "h1", content: trim(substr(trimmed, 2)) });
        } else if (bobStartsWith(trimmed, "> ")) {
            push(blocks, { kind: "quote", content: trim(substr(trimmed, 2)) });
        } else if (bobStartsWith(trimmed, "- ")) {
            push(blocks, { kind: "item", content: trim(substr(trimmed, 2)) });
        } else {
            push(blocks, { kind: "text", content: trimmed });
        }

        i = i + 1;
    }

    return blocks;
}

// يحوّل نص سطر واحد (inline) إلى HTML: **عريض**، *مائل*، `كود`، [نص](URL)؛ أي نص
// خارج هذه الرموز يُهرَب بأمان عبر bobEscapeHtml حرفاً حرفاً
fun bobInlineToHtml(ln) {
    let out = "";
    let i = 0;
    let n = len(ln);
    let boldOpen = false;
    let italicOpen = false;
    let codeOpen = false;

    while (i < n) {
        let c = charAt(ln, i);

        if (c == "`") {
            if (codeOpen) { out = out + "</code>"; } else { out = out + "<code>"; }
            codeOpen = !codeOpen;
            i = i + 1;
        } else if (c == "*" and i + 1 < n and charAt(ln, i + 1) == "*") {
            if (boldOpen) { out = out + "</strong>"; } else { out = out + "<strong>"; }
            boldOpen = !boldOpen;
            i = i + 2;
        } else if (c == "*") {
            if (italicOpen) { out = out + "</em>"; } else { out = out + "<em>"; }
            italicOpen = !italicOpen;
            i = i + 1;
        } else if (c == "[") {
            let rest = substr(ln, i);
            let closeBracket = indexOf(rest, "]");
            let handled = false;

            if (closeBracket != -1) {
                let afterBracket = i + closeBracket + 1;
                if (afterBracket < n and charAt(ln, afterBracket) == "(") {
                    let afterParen = substr(ln, afterBracket + 1);
                    let closeParen = indexOf(afterParen, ")");
                    if (closeParen != -1) {
                        let linkText = substr(ln, i + 1, closeBracket - 1);
                        let url = substr(afterParen, 0, closeParen);
                        out = out + "<a href=\"" + bobEscapeHtml(url) + "\">" + bobEscapeHtml(linkText) + "</a>";
                        i = afterBracket + 1 + closeParen + 1;
                        handled = true;
                    }
                }
            }

            if (!handled) {
                out = out + bobEscapeHtml(c);
                i = i + 1;
            }
        } else {
            out = out + bobEscapeHtml(c);
            i = i + 1;
        }
    }

    return out;
}

// يحوّل نص سطر واحد (inline) إلى نص عادي: يزيل رموز **/*/` ويحوّل [نص](URL) إلى
// "نص (URL)"؛ يُستخدم داخلياً في bobToPlain
fun bobInlineToPlain(ln) {
    let out = "";
    let i = 0;
    let n = len(ln);

    while (i < n) {
        let c = charAt(ln, i);

        if (c == "`") {
            i = i + 1;
        } else if (c == "*" and i + 1 < n and charAt(ln, i + 1) == "*") {
            i = i + 2;
        } else if (c == "*") {
            i = i + 1;
        } else if (c == "[") {
            let rest = substr(ln, i);
            let closeBracket = indexOf(rest, "]");
            let handled = false;

            if (closeBracket != -1) {
                let afterBracket = i + closeBracket + 1;
                if (afterBracket < n and charAt(ln, afterBracket) == "(") {
                    let afterParen = substr(ln, afterBracket + 1);
                    let closeParen = indexOf(afterParen, ")");
                    if (closeParen != -1) {
                        let linkText = substr(ln, i + 1, closeBracket - 1);
                        let url = substr(afterParen, 0, closeParen);
                        out = out + linkText + " (" + url + ")";
                        i = afterBracket + 1 + closeParen + 1;
                        handled = true;
                    }
                }
            }

            if (!handled) {
                out = out + c;
                i = i + 1;
            }
        } else {
            out = out + c;
            i = i + 1;
        }
    }

    return out;
}

// يحوّل مصدر Bob كاملاً إلى HTML جاهز للعرض (مثلاً داخل WebView في تطبيق DLoF/RinLang)
fun bobToHtml(source) {
    let blocks = bobTokenize(source);
    let out = "";
    let listOpen = false;
    let i = 0;

    while (i < len(blocks)) {
        let b = blocks[i];
        let kind = b["kind"];

        if (kind == "item") {
            if (!listOpen) { out = out + "<ul>\n"; listOpen = true; }
            out = out + "<li>" + bobInlineToHtml(b["content"]) + "</li>\n";
        } else {
            if (listOpen) { out = out + "</ul>\n"; listOpen = false; }

            if (kind == "h1") { out = out + "<h1>" + bobInlineToHtml(b["content"]) + "</h1>\n"; }
            else if (kind == "h2") { out = out + "<h2>" + bobInlineToHtml(b["content"]) + "</h2>\n"; }
            else if (kind == "h3") { out = out + "<h3>" + bobInlineToHtml(b["content"]) + "</h3>\n"; }
            else if (kind == "quote") { out = out + "<blockquote>" + bobInlineToHtml(b["content"]) + "</blockquote>\n"; }
            else if (kind == "hr") { out = out + "<hr>\n"; }
            else { out = out + "<p>" + bobInlineToHtml(b["content"]) + "</p>\n"; }
        }

        i = i + 1;
    }

    if (listOpen) { out = out + "</ul>\n"; }
    return out;
}

// يحوّل مصدر Bob إلى نص عادي (بلا HTML)؛ العناوين تبقى كنص، الاقتباس بادئته "> "،
// عناصر القائمة بادئتها "- "، والخط الفاصل يصبح سطر شرطات
fun bobToPlain(source) {
    let blocks = bobTokenize(source);
    let out = "";
    let i = 0;

    while (i < len(blocks)) {
        let b = blocks[i];
        let kind = b["kind"];
        let plainContent = bobInlineToPlain(b["content"]);

        if (kind == "hr") { out = out + "----------\n"; }
        else if (kind == "item") { out = out + "- " + plainContent + "\n"; }
        else if (kind == "quote") { out = out + "> " + plainContent + "\n"; }
        else { out = out + plainContent + "\n"; }

        i = i + 1;
    }

    return out;
}

// معلومات وصفية عن المكتبة (اسم/إصدار/وصف/دوال مصدَّرة)، بنفس أسلوب pkgInfo في
// oglang.og.rin و ringoInfo في ringo.og.rin — جاهزة للطباعة أو للعرض في شاشة "المكتبات"
fun bobInfo() {
    return {
        name: "bob",
        version: "1.0.0",
        description: "لغة ترميز خفيفة (Markdown-lite) بأسطر بادئة، تُصيَّر إلى HTML أو نص عادي",
        exports: ["bobTokenize", "bobToHtml", "bobToPlain", "bobEscapeHtml", "bobInfo"]
    };
}
)BOBOGRIN";

static const char* kLib_ghpublish_og_rin = R"GHPUBLISHOGRIN(
// ============================================================================
//  lib/ghpublish.og.rin — تطبيق نشر مشاريع GitHub حقيقي (REST API حقيقي فعلي، وليس محاكاة):
//  تسجيل دخول بـ Personal Access Token (ghp_...)، رفع أرشيف .zip وفكّ ضغطه، نشره كمستودع
//  GitHub جديد أو تحديث مستودع موجود، وتحميل مستودع كامل محلياً.
//
//  استيراد:
//    @import "lib/ghpublish.og.rin";
//    @import "lib/ghpublish.og.rin" as ghp;
//
//  يعتمد على natives الشبكة الحقيقية (apiRegister/apiGet/apiPost/apiPut، انظر registerNatives()
//  في rin_interpreter.cpp) وعلى base64Encode/base64Decode وunzipEntries (rin_binutils.h/.cpp) —
//  كلها اتصالات/عمليات حقيقية فعلية، لا محاكاة: تسجيل دخول فعلي، رفع ملفات فعلي، نشر فعلي.
//
//  مثال استخدام كامل (تسجيل دخول -> نشر أرشيف zip كمستودع جديد -> تحميله لاحقاً):
//    @import "lib/ghpublish.og.rin";
//
//    let me = ghpLogin("ghp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
//    if (!me["ok"]) { print "فشل تسجيل الدخول: " + me["error"]; }
//    else {
//        print "مرحباً " + me["login"] + "!";
//        let result = ghpPublishProject(me["login"], "my-rin-project",
//                                        "مشروع Rin منشور تلقائياً", false,
//                                        "myproject.zip", "نشر أولي عبر ghpublish");
//        if (result["ok"]) { print "نُشر بنجاح: " + result["repoUrl"]; }
//        else { print "فشل النشر في مرحلة " + result["stage"] + ": " + result["error"]; }
//
//        // لاحقاً، تحميل نفس المستودع كاملاً كملفات محلية حقيقية:
//        let dl = ghpDownloadRepo(me["login"], "my-rin-project", "main", "downloaded_project");
//        print "حُمِّل " + toString(len(dl["downloaded"])) + " ملفاً";
//    }
//
//  ملاحظات (حدود معروفة v1، بنفس أسلوب توثيق القيود في هذا المشروع):
//   * "تحميل" هنا يُعيد بناء ملفات المستودع الحقيقية على القرص محلياً (عبر Contents API نصاً
//     Base64 داخل JSON، آمن تماماً عبر الشبكة)، وليس أرشيف .zip واحداً جاهزاً — تنزيل رابط
//     zipball الخام مباشرة غير آمن حالياً على أندرويد تحديداً لأن جسر HTTP هناك
//     (RinHttpBridge.kt) يفترض أن جسم الرد نص UTF-8، وبيانات .zip الخام ثنائية فتُتلَف لو مرّت
//     منه؛ المسار عبر Contents API (نص/JSON بالكامل) يتفادى هذه المشكلة تماماً على كل المنصات.
//   * مسارات الملفات ذات المسافات فقط تُرمَّز تلقائياً (%20)؛ رموز خاصة أخرى (#، ?، محارف غير
//     ASCII في اسم الملف نفسه) قد تحتاج ترميزاً يدوياً إضافياً قبل الاستدعاء.
//   * لا معالجة لـ pagination عند git/trees الضخمة جداً (git trees API نفسها تُرجِع truncated:true
//     حينها) — يكفي لمعظم مشاريع Rin العادية.
// ============================================================================

fun ghpStartsWith(s, prefix) {
    if (len(s) < len(prefix)) { return false; }
    return substr(s, 0, len(prefix)) == prefix;
}

fun ghpEndsWithSlash(s) {
    if (len(s) == 0) { return false; }
    return substr(s, len(s) - 1, 1) == "/";
}

// يرمّز الفراغات فقط داخل مسار ملف لاستخدامه في رابط Contents API (انظر الملاحظات أعلاه)
fun ghpUrlEncodePath(path) {
    return replace(path, " ", "%20");
}

// يبني رسالة خطأ بشرية واضحة من رد apiGet/apiPost/apiPut (يفرّق بين فشل الاتصال نفسه وبين رد
// GitHub بخطأ منطقي مثل 401/404/422 يحمل حقل "message")
fun ghpErrorFromResult(res) {
    if (res["ok"] == false) {
        return "تعذّر الاتصال بـ GitHub: " + res["error"];
    }
    let j = res["json"];
    if (typeOf(j) == "map" and has(j, "message")) {
        return "GitHub (" + toString(res["status"]) + "): " + j["message"];
    }
    let bodyText = res["body"];
    if (len(bodyText) > 300) { bodyText = substr(bodyText, 0, 300) + "..."; }
    return "GitHub (" + toString(res["status"]) + "): " + bodyText;
}

// تسجيل الدخول: يسجّل API باسم "github" (baseUrl + ترويسة Authorization بالتوكن)، ثم يتحقّق
// فعلياً من صلاحية التوكن عبر GET /user. يعيد { ok, login, name, id } أو { ok: false, error }
fun ghpLogin(token) {
    apiRegister("github", "https://api.github.com");
    apiHeader("github", "Authorization", "token " + token);
    apiHeader("github", "Accept", "application/vnd.github+json");
    apiHeader("github", "User-Agent", "RinLang-GitHub-Publisher");

    let res = apiGet("github", "/user");
    if (res["ok"] and res["status"] == 200) {
        let u = res["json"];
        return { ok: true, login: u["login"], name: u["name"], id: u["id"] };
    }
    return { ok: false, error: ghpErrorFromResult(res) };
}

// ينشئ مستودعاً جديداً على حساب المستخدم المسجَّل دخوله حالياً (يجب استدعاء ghpLogin أولاً)
fun ghpCreateRepo(name, description, isPrivate) {
    let body = { name: name, description: description, private: isPrivate, auto_init: true };
    let res = apiPost("github", "/user/repos", body);
    if (res["ok"] and res["status"] == 201) {
        let j = res["json"];
        return { ok: true, fullName: j["full_name"], htmlUrl: j["html_url"], defaultBranch: j["default_branch"] };
    }
    return { ok: false, status: res["status"], error: ghpErrorFromResult(res) };
}

// يتحقّق هل مستودع owner/repo موجود فعلاً، ويعيد فرعه الافتراضي إن وُجد
fun ghpRepoInfo(owner, repo) {
    let res = apiGet("github", "/repos/" + owner + "/" + repo);
    if (res["ok"] and res["status"] == 200) {
        let j = res["json"];
        return { ok: true, exists: true, defaultBranch: j["default_branch"], htmlUrl: j["html_url"] };
    }
    if (res["ok"] and res["status"] == 404) {
        return { ok: true, exists: false };
    }
    return { ok: false, error: ghpErrorFromResult(res) };
}

// يجلب sha الحالي لملف موجود مسبقاً على الفرع (لازم لتحديثه لا لإنشائه)؛ يعيد nil إن لم يوجد
fun ghpGetFileSha(owner, repo, repoPath, branch) {
    let path = "/repos/" + owner + "/" + repo + "/contents/" + ghpUrlEncodePath(repoPath) + "?ref=" + branch;
    let res = apiGet("github", path);
    if (res["ok"] and res["status"] == 200) {
        return res["json"]["sha"];
    }
    return nil;
}

// يرفع/يحدّث ملفاً واحداً فعلياً على GitHub عبر Contents API (PUT). rawContent بايتات خام
// (نص أو ثنائي، مثلاً محتوى من unzipEntries أو readFile) — يُرمَّز Base64 تلقائياً هنا.
fun ghpUploadFile(owner, repo, branch, repoPath, rawContent, commitMessage) {
    let sha = ghpGetFileSha(owner, repo, repoPath, branch);
    let body = { message: commitMessage, content: base64Encode(rawContent), branch: branch };
    if (sha != nil) { body["sha"] = sha; }

    let path = "/repos/" + owner + "/" + repo + "/contents/" + ghpUrlEncodePath(repoPath);
    let res = apiPut("github", path, body);
    if (res["ok"] and (res["status"] == 200 or res["status"] == 201)) {
        return { ok: true, path: repoPath, sha: res["json"]["content"]["sha"] };
    }
    return { ok: false, path: repoPath, error: ghpErrorFromResult(res) };
}

// إن شارك كل عنصر بادئة مجلد جذر واحدة (حالة شائعة: أرشيف مُصدَّر يحوي "projectName/" كمجلد
// أب لكل شيء)، يعيدها لتُستخدَم في ghpUploadZip لتجريدها تلقائياً؛ وإلا يعيد ""
fun ghpCommonRootPrefix(entries) {
    if (len(entries) == 0) { return ""; }
    let firstName = entries[0]["name"];
    let slashIdx = indexOf(firstName, "/");
    if (slashIdx == -1) { return ""; }
    let prefix = substr(firstName, 0, slashIdx + 1);

    let i = 0;
    while (i < len(entries)) {
        if (!ghpStartsWith(entries[i]["name"], prefix)) { return ""; }
        i = i + 1;
    }
    return prefix;
}

// يفكّ أرشيف .zip محلي (رفعه المستخدم مسبقاً إلى المشروع) عبر unzipEntries، ثم يرفع كل ملف
// غير-مجلد بداخله فعلياً كملف على GitHub (تحديث أو إنشاء حسب وجوده مسبقاً). stripRoot=true
// يجرّد بادئة المجلد الجذر المشتركة تلقائياً إن وُجدت (انظر ghpCommonRootPrefix).
fun ghpUploadZip(owner, repo, branch, zipPath, commitMessage, stripRoot) {
    let entries = unzipEntries(zipPath);
    let prefix = "";
    if (stripRoot) { prefix = ghpCommonRootPrefix(entries); }

    let uploaded = [];
    let failed = [];
    let i = 0;
    while (i < len(entries)) {
        let e = entries[i];
        if (!e["isDir"]) {
            let repoPath = e["name"];
            if (prefix != "") { repoPath = substr(repoPath, len(prefix)); }
            if (repoPath != "") {
                let r = ghpUploadFile(owner, repo, branch, repoPath, e["content"], commitMessage);
                if (r["ok"]) { push(uploaded, repoPath); } else { push(failed, r); }
            }
        }
        i = i + 1;
    }
    return { ok: len(failed) == 0, uploaded: uploaded, failed: failed };
}

// التدفّق الكامل بخطوة واحدة: يُنشئ المستودع إن لم يكن موجوداً (أو يستخدم الموجود بفرعه
// الافتراضي)، ثم يرفع أرشيف .zip كاملاً إليه. استدعِ ghpLogin أولاً.
fun ghpPublishProject(owner, repoName, description, isPrivate, zipPath, commitMessage) {
    let info = ghpRepoInfo(owner, repoName);
    if (!info["ok"]) { return { ok: false, stage: "checkRepo", error: info["error"] }; }

    let branch = "main";
    if (info["exists"]) {
        branch = info["defaultBranch"];
    } else {
        let created = ghpCreateRepo(repoName, description, isPrivate);
        if (!created["ok"]) { return { ok: false, stage: "createRepo", error: created["error"] }; }
        if (created["defaultBranch"] != nil) { branch = created["defaultBranch"]; }
    }

    let result = ghpUploadZip(owner, repoName, branch, zipPath, commitMessage, true);
    result["stage"] = "upload";
    result["repoUrl"] = "https://github.com/" + owner + "/" + repoName;
    return result;
}

// يجلب قائمة كل الملفات (blobs) داخل فرع مستودع عبر Git Trees API (استدعاء واحد بحث متكرر
// recursive=1) — يعيد { ok, files: [مسارات نصية] }
fun ghpDownloadTree(owner, repo, branch) {
    let res = apiGet("github", "/repos/" + owner + "/" + repo + "/git/trees/" + branch + "?recursive=1");
    if (!(res["ok"] and res["status"] == 200)) {
        return { ok: false, error: ghpErrorFromResult(res) };
    }

    let tree = res["json"]["tree"];
    let blobs = [];
    let i = 0;
    while (i < len(tree)) {
        if (tree[i]["type"] == "blob") { push(blobs, tree[i]["path"]); }
        i = i + 1;
    }
    return { ok: true, files: blobs };
}

// يحمّل ملفاً واحداً فعلياً من GitHub (Contents API، Base64 داخل JSON، آمن عبر أي منصة) ويكتبه
// محلياً عبر writeFile
fun ghpDownloadFile(owner, repo, branch, repoPath, localPath) {
    let path = "/repos/" + owner + "/" + repo + "/contents/" + ghpUrlEncodePath(repoPath) + "?ref=" + branch;
    let res = apiGet("github", path);
    if (!(res["ok"] and res["status"] == 200)) {
        return { ok: false, path: repoPath, error: ghpErrorFromResult(res) };
    }

    let raw = base64Decode(res["json"]["content"]);
    writeFile(localPath, raw);
    return { ok: true, path: repoPath, localPath: localPath };
}

// يحمّل مستودعاً كاملاً محلياً: يجلب شجرة الملفات ثم يحمّل كل ملف فعلياً تحت destDir (بنفس
// بنية المجلدات الأصلية). يعيد { ok, downloaded: [مسارات محلية], failed: [...] }
fun ghpDownloadRepo(owner, repo, branch, destDir) {
    let treeResult = ghpDownloadTree(owner, repo, branch);
    if (!treeResult["ok"]) { return treeResult; }

    let dest = destDir;
    if (!ghpEndsWithSlash(dest)) { dest = dest + "/"; }

    let downloaded = [];
    let failed = [];
    let i = 0;
    while (i < len(treeResult["files"])) {
        let repoPath = treeResult["files"][i];
        let r = ghpDownloadFile(owner, repo, branch, repoPath, dest + repoPath);
        if (r["ok"]) { push(downloaded, r["localPath"]); } else { push(failed, r); }
        i = i + 1;
    }
    return { ok: len(failed) == 0, downloaded: downloaded, failed: failed };
}

// معلومات وصفية عن المكتبة، بنفس أسلوب bobInfo/ringoInfo/pkgInfo في هذا المشروع
fun ghpInfo() {
    return {
        name: "ghpublish",
        version: "1.0.0",
        description: "نشر وتحميل مشاريع GitHub حقيقية: دخول بتوكن (ghp_...)، رفع أرشيف zip وفكّ ضغطه ونشره كمستودع، تحميل مستودع كاملاً",
        exports: [
            "ghpLogin", "ghpCreateRepo", "ghpRepoInfo", "ghpUploadFile", "ghpUploadZip",
            "ghpPublishProject", "ghpDownloadTree", "ghpDownloadFile", "ghpDownloadRepo"
        ]
    };
}
)GHPUBLISHOGRIN";

static const char* kLib_rinxg_og_rin = R"RINXGOGRIN(// ============================================================================
//  lib/rinxg.og.rin — RinXG: لغة برمجة تصريحية (declarative) لتصميم واجهات الويب فوق Rin.
//  محرّك لغة كامل مكتوب بالكامل بـ Rin (Lexer+Parser+AST+مُصيِّر HTML/CSS حقيقي)، وليس مجرّد
//  قوالب نصية — يصف المستخدم الواجهة بصيغة RinXG فتُترجَم إلى صفحة HTML+CSS كاملة جاهزة للعرض
//  في أي متصفّح أو WebView.
//
//  استيراد:
//    @import "lib/rinxg.og.rin";
//    @import "lib/rinxg.og.rin" as rinxg;
//
//  ---------------------------- صيغة RinXG ----------------------------
//  page "عنوان الصفحة" {
//      style {
//          bg: #f5f5f5;
//          font: sans-serif;
//      }
//
//      container column gap=16 padding=24 {
//          heading level=1 color=#222 { "مرحباً بلغة RinXG" }
//          text color=#666 { "لغة تصميم واجهات ويب تصريحية فوق Rin" }
//
//          container row gap=8 {
//              button variant=success size=lg { "ابدأ الآن" }
//              button variant=outline { "تعلّم المزيد" }
//          }
//
//          banner type=warning title="تنبيه" {
//              text { "النسخة الحالية قديمة." }
//              button variant=ghost size=sm { "تحديث" }
//          }
//
//          input placeholder="بريدك الإلكتروني...";
//          image src="logo.png" width=120;
//          link href="https://example.com" { "زيارة الموقع" }
//
//          list {
//              item { "عنصر أول" }
//              item { "عنصر ثانٍ" }
//          }
//      }
//  }
//
//  العناصر المدعومة (v1.1 — مكتبة أزرار/حاويات/تنبيهات احترافية):
//  • container: row|column، wrap، gap، padding، bg أو variant (نفس ألوان الزر)، border+
//    borderColor، radius، shadow، width، height، align، justify.
//  • button: variant (primary الافتراضي | secondary | success | danger/error | warning |
//    info | dark | light | outline | ghost | link)، size (sm|md|lg)، bg=/color= صريحان
//    يتفوّقان دوماً على variant، radius (رقم أو "pill")، width، block/fullWidth، shadow،
//    disabled، href (يُصيَّر كرابط <a> بمظهر زر). حالات hover/active/focus/disabled مُعرَّفة
//    مرة واحدة في <style> عبر class="rinxg-btn" (لا يمكن التعبير عنها بـ style= مضمّن).
//  • banner: type (info الافتراضي | success | warning | error/danger | action) — يحدّد لوناً
//    وأيقونة معاً، title= اختياري، ثم إمّا أبناء (text/button/...) أو text= مختصر بلا أبناء؛
//    اللون يُورَّث للأبناء تلقائياً عبر color على الحاوية (لا حاجة لتكراره بكل عنصر ابن).
//  • heading (level، color) • text (color، size) • input (placeholder، عنصر مغلق بـ ';' بلا
//    محتوى) • image (src، width، عنصر مغلق بـ ';') • link (href) • list/item.
//  أي وسم غير معروف يُصيَّر كـ <div data-rinxg-tag="..."> بدل أن يُسقَط بصمت، ليسهل اكتشاف
//  الأخطاء الإملائية في الوسوم.
//
//  الاستخدام:
//    @import "lib/rinxg.og.rin";
//    let html = rxToHtml(source);   // يعيد صفحة HTML+CSS كاملة جاهزة (<!DOCTYPE html>...)
//    writeFile("out.html", html);
//
//  ملاحظات (حدود معروفة v1، بنفس أسلوب توثيق القيود في هذا المشروع): لا تعبيرات/شروط/حلقات
//  داخل RinXG نفسها (هي لغة وصف تصميم تصريحية بحتة، لا لغة برمجة عامة) — أي منطق ديناميكي
//  (توليد عناصر بحلقة، ربط بيانات) يُكتَب بـ Rin نفسها قبل استدعاء rxToHtml عبر بناء نص
//  RinXG المصدر برمجياً (تسلسل نصوص) ثم تمريره.
// ============================================================================

// ----------------------------- قارئ محارف (Lexer/Scanner) -----------------------------
// حالة القراءة تُمرَّر كخريطة (map) بمرجعية مشتركة فتتحوّل كل الدوال أدناه لتُحدّثها في مكانها

fun rxAtEnd(st) { return st["pos"] >= st["len"]; }

fun rxPeek(st) {
    if (rxAtEnd(st)) { return ""; }
    return charAt(st["src"], st["pos"]);
}

fun rxPeekAt(st, offset) {
    let p = st["pos"] + offset;
    if (p >= st["len"]) { return ""; }
    return charAt(st["src"], p);
}

fun rxAdvance(st) {
    let c = rxPeek(st);
    st["pos"] = st["pos"] + 1;
    return c;
}

fun rxIsSpace(c) {
    return c == " " or c == "\n" or c == "\t" or c == "\r";
}

fun rxSkipWs(st) {
    while (!rxAtEnd(st)) {
        let c = rxPeek(st);
        if (rxIsSpace(c)) {
            rxAdvance(st);
        } else if (c == "/" and rxPeekAt(st, 1) == "/") {
            while (!rxAtEnd(st) and rxPeek(st) != "\n") { rxAdvance(st); }
        } else {
            break;
        }
    }
}

// عمليات المقارنة >=/<= في Rin تعمل على الأرقام فقط، لذا تُقارَن المحارف عبر ord() (نفس
// أسلوب lib/langkit.og.rin: code >= ord("a") and code <= ord("z"))
fun rxIsIdentStart(c) {
    if (c == "") { return false; }
    let code = ord(c);
    return (code >= ord("a") and code <= ord("z")) or (code >= ord("A") and code <= ord("Z")) or c == "_";
}

fun rxIsIdentChar(c) {
    if (c == "") { return false; }
    let code = ord(c);
    return rxIsIdentStart(c) or (code >= ord("0") and code <= ord("9")) or c == "-";
}

fun rxReadIdent(st) {
    let start = st["pos"];
    while (!rxAtEnd(st) and rxIsIdentChar(rxPeek(st))) { rxAdvance(st); }
    return substr(st["src"], start, st["pos"] - start);
}

fun rxReadString(st) {
    rxAdvance(st); // يستهلك علامة الاقتباس الافتتاحية
    let out = "";
    while (!rxAtEnd(st) and rxPeek(st) != "\"") {
        let c = rxAdvance(st);
        if (c == "\\" and !rxAtEnd(st)) {
            let nc = rxAdvance(st);
            if (nc == "n") { out = out + "\n"; }
            else if (nc == "\"") { out = out + "\""; }
            else if (nc == "\\") { out = out + "\\"; }
            else { out = out + nc; }
        } else {
            out = out + c;
        }
    }
    if (!rxAtEnd(st)) { rxAdvance(st); } // يستهلك علامة الاقتباس الختامية
    return out;
}

// قيمة سمة بلا اقتباس (مثال: gap=16 أو bg=#4CAF50): تمتد حتى مسافة أو ; أو { أو } أو =
fun rxReadBareValue(st) {
    let start = st["pos"];
    while (!rxAtEnd(st)) {
        let c = rxPeek(st);
        if (rxIsSpace(c) or c == ";" or c == "{" or c == "}" or c == "=") { break; }
        rxAdvance(st);
    }
    return substr(st["src"], start, st["pos"] - start);
}

// قيمة داخل كتلة style { key: value; } — تمتد حتى ; أو } أو نهاية السطر
fun rxReadStyleValue(st) {
    let start = st["pos"];
    while (!rxAtEnd(st)) {
        let c = rxPeek(st);
        if (c == ";" or c == "}" or c == "\n") { break; }
        rxAdvance(st);
    }
    return trim(substr(st["src"], start, st["pos"] - start));
}

// ----------------------------- المحلِّل (Parser) -----------------------------

fun rxParseAttrs(st) {
    let attrs = {};
    while (true) {
        rxSkipWs(st);
        if (rxAtEnd(st)) { break; }
        let c = rxPeek(st);
        if (c == "{" or c == ";" or c == "}") { break; }
        if (!rxIsIdentStart(c)) { break; }

        let name = rxReadIdent(st);
        rxSkipWs(st);
        if (!rxAtEnd(st) and rxPeek(st) == "=") {
            rxAdvance(st);
            rxSkipWs(st);
            let val = "";
            if (!rxAtEnd(st) and rxPeek(st) == "\"") { val = rxReadString(st); }
            else { val = rxReadBareValue(st); }
            attrs[name] = val;
        } else {
            attrs[name] = "true"; // سمة علم بلا قيمة (مثل row أو column)
        }
    }
    return attrs;
}

// يقرأ عنصراً واحداً: وسم + سمات، ثم إما ';' (عنصر مغلق ذاتياً بلا محتوى) أو '{' نص/عناصر أبناء '}'
fun rxParseElement(st) {
    rxSkipWs(st);
    let tag = rxReadIdent(st);
    let attrs = rxParseAttrs(st);
    rxSkipWs(st);

    let node = { "tag": tag, "attrs": attrs, "text": "", "children": [] };

    if (!rxAtEnd(st) and rxPeek(st) == ";") {
        rxAdvance(st);
        return node;
    }

    if (!rxAtEnd(st) and rxPeek(st) == "{") {
        rxAdvance(st);
        rxSkipWs(st);
        if (!rxAtEnd(st) and rxPeek(st) == "\"") {
            node["text"] = rxReadString(st);
            rxSkipWs(st);
        } else {
            let kids = [];
            while (true) {
                rxSkipWs(st);
                if (rxAtEnd(st)) { break; }
                if (rxPeek(st) == "}") { break; }
                push(kids, rxParseElement(st));
                rxSkipWs(st);
            }
            node["children"] = kids;
        }
        rxSkipWs(st);
        if (!rxAtEnd(st) and rxPeek(st) == "}") { rxAdvance(st); }
    }

    return node;
}

fun rxParseStyleBlock(st) {
    let props = {};
    while (true) {
        rxSkipWs(st);
        if (rxAtEnd(st)) { break; }
        if (rxPeek(st) == "}") { break; }

        let name = rxReadIdent(st);
        rxSkipWs(st);
        if (!rxAtEnd(st) and rxPeek(st) == ":") { rxAdvance(st); }
        rxSkipWs(st);

        let val = "";
        if (!rxAtEnd(st) and rxPeek(st) == "\"") { val = rxReadString(st); }
        else { val = rxReadStyleValue(st); }
        props[name] = val;

        rxSkipWs(st);
        if (!rxAtEnd(st) and rxPeek(st) == ";") { rxAdvance(st); }
    }
    rxSkipWs(st);
    if (!rxAtEnd(st) and rxPeek(st) == "}") { rxAdvance(st); }
    return props;
}

// يحلّل مصدر RinXG كاملاً إلى AST: { title, style: {...}, children: [عناصر] }
fun rxParse(source) {
    let st = { "src": source, "pos": 0, "len": len(source) };
    rxSkipWs(st);

    rxReadIdent(st); // "page" (لا نتحقّق من قيمتها بصرامة؛ أي اسم بديل يُقبَل بنفس المعاملة)
    rxSkipWs(st);

    let title = "";
    if (!rxAtEnd(st) and rxPeek(st) == "\"") { title = rxReadString(st); }
    rxSkipWs(st);

    let styleProps = {};
    let children = [];

    if (!rxAtEnd(st) and rxPeek(st) == "{") {
        rxAdvance(st);
        while (true) {
            rxSkipWs(st);
            if (rxAtEnd(st)) { break; }
            if (rxPeek(st) == "}") { break; }

            let savePos = st["pos"];
            let ident = rxReadIdent(st);
            if (ident == "style") {
                rxSkipWs(st);
                if (!rxAtEnd(st) and rxPeek(st) == "{") {
                    rxAdvance(st);
                    styleProps = rxParseStyleBlock(st);
                }
            } else {
                st["pos"] = savePos; // تراجع ليُعاد تحليله كعنصر عادي بواسطة rxParseElement
                push(children, rxParseElement(st));
            }
            rxSkipWs(st);
        }
        if (!rxAtEnd(st) and rxPeek(st) == "}") { rxAdvance(st); }
    }

    return { "title": title, "style": styleProps, "children": children };
}

// ----------------------------- أدوات مساعدة للمُصيِّر -----------------------------

fun rxEscapeHtml(raw) {
    let out = raw;
    out = replace(out, "&", "&amp;");
    out = replace(out, "<", "&lt;");
    out = replace(out, ">", "&gt;");
    out = replace(out, "\"", "&quot;");
    return out;
}

fun rxIsNumeric(s) {
    if (len(s) == 0) { return false; }
    let i = 0;
    if (charAt(s, 0) == "-") { i = 1; }
    if (i >= len(s)) { return false; }
    while (i < len(s)) {
        let c = charAt(s, i);
        let code = ord(c);
        if (!((code >= ord("0") and code <= ord("9")) or c == ".")) { return false; }
        i = i + 1;
    }
    return true;
}

// يضيف "px" تلقائياً للقيم الرقمية الخام (gap=16 -> "16px")؛ يترك القيم الجاهزة (16px، 50%) كما هي
fun rxPx(s) {
    if (rxIsNumeric(s)) { return s + "px"; }
    return s;
}

// ----------------------------- المُصيِّر (Renderer -> HTML/CSS) -----------------------------

// لوحة ألوان موحّدة (design tokens) يستخدمها button/container/banner معاً عبر variant=، بدل أن
// يضطر كل عنصر لتكرار قيم hex خاصة به — هذا ما يجعل الثلاثة "مترابطة" فعلياً في نفس نظام الألوان.
// fg غير موجود إلا حين يختلف عن الأبيض الافتراضي (warning/light تحتاج نصاً داكناً للتباين).
fun rxVariantPalette(variant) {
    if (variant == "secondary") { return { "bg": "#5a5f73", "fg": "#ffffff" }; }
    if (variant == "success")   { return { "bg": "#2e9f43", "fg": "#ffffff" }; }
    if (variant == "danger" or variant == "error") { return { "bg": "#d14545", "fg": "#ffffff" }; }
    if (variant == "warning")   { return { "bg": "#d4a72c", "fg": "#2a2a20" }; }
    if (variant == "info")      { return { "bg": "#3a6ec4", "fg": "#ffffff" }; }
    if (variant == "dark")      { return { "bg": "#1e1f26", "fg": "#ffffff" }; }
    if (variant == "light")     { return { "bg": "#eceef4", "fg": "#20222b" }; }
    // "primary" (الافتراضي) — وأيضاً القاعدة اللونية لِـ outline/ghost/link ما لم يُحدَّد bg=/color=
    return { "bg": "#7c5cff", "fg": "#ffffff" };
}

// أحجام الزر: sm/md(افتراضي)/lg — تضبط الحشو (padding) وحجم الخط معاً حتى لا يبدو الزر
// "مقصوصاً" (padding ثابت مهما كبر/صغر النص، وهي إحدى مشاكل الحجم/الطول التي وردت في الطلب).
fun rxButtonSizeMetrics(size) {
    if (size == "sm" or size == "small") { return { "pad": "6px 14px", "font": "13px" }; }
    if (size == "lg" or size == "large") { return { "pad": "14px 28px", "font": "18px" }; }
    return { "pad": "10px 22px", "font": "15px" };
}

// يبني CSS الزر كاملاً: يبدأ من ألوان الـ variant، ثم شكل المتغيّر (filled/outline/ghost/link)،
// ثم يسمح لـ bg=/color= الصريحين بتجاوز أي منهما — نفس ترتيب الأولوية الذي توثّقه بقية المكتبة.
fun rxButtonStyle(attrs) {
    let variant = "primary";
    if (has(attrs, "variant")) { variant = attrs["variant"]; }
    let size = "md";
    if (has(attrs, "size")) { size = attrs["size"]; }
    let disabled = has(attrs, "disabled") and attrs["disabled"] == "true";

    let palette = rxVariantPalette(variant);
    let baseColor = palette["bg"];
    if (has(attrs, "bg")) { baseColor = attrs["bg"]; }

    // اللون النصّي الافتراضي حسب شكل الـ variant: outline/ghost/link بلا خلفية فتستخدم baseColor
    // نفسه كنص، وبقية الأشكال (filled) تستخدم لون التباين fg من اللوحة. color= يتفوّق دوماً على
    // كليهما — يُحسَب مرة واحدة هنا بدل تكرار خاصية color: في الـ CSS الناتج.
    let textColor = palette["fg"];
    if (variant == "outline" or variant == "ghost" or variant == "link") { textColor = baseColor; }
    if (has(attrs, "color")) { textColor = attrs["color"]; }

    let metrics = rxButtonSizeMetrics(size);
    let css = "display:inline-block;border:2px solid transparent;cursor:pointer;font-weight:600;" +
              "font-size:" + metrics["font"] + ";padding:" + metrics["pad"] + ";" +
              "color:" + textColor + ";" +
              "transition:filter .15s ease, transform .05s ease;";

    if (variant == "outline") {
        css = css + "background:transparent;border-color:" + baseColor + ";";
    } else if (variant == "ghost") {
        css = css + "background:transparent;";
    } else if (variant == "link") {
        css = css + "background:transparent;text-decoration:underline;padding:2px 0;border:none;";
    } else {
        css = css + "background:" + baseColor + ";border-color:" + baseColor + ";";
    }

    let radius = "8px";
    if (has(attrs, "radius")) {
        if (attrs["radius"] == "pill") { radius = "999px"; }
        else { radius = rxPx(attrs["radius"]); }
    }
    css = css + "border-radius:" + radius + ";";

    if (has(attrs, "width")) { css = css + "width:" + rxPx(attrs["width"]) + ";"; }
    let full = (has(attrs, "block") and attrs["block"] == "true") or
               (has(attrs, "fullWidth") and attrs["fullWidth"] == "true");
    if (full) { css = css + "display:block;width:100%;text-align:center;"; }
    if (has(attrs, "shadow") and attrs["shadow"] == "true") { css = css + "box-shadow:0 2px 8px rgba(0,0,0,.18);"; }
    if (disabled) { css = css + "opacity:.5;cursor:not-allowed;pointer-events:none;"; }

    return css;
}

// نفس نظام variant الخاص بالزر، لكن لصندوق (container/banner): خلفية + حدود اختياريّة + ظل،
// بدل تكرار سلسلة "bg=#..." يدوياً في كل مكان — وهذا هو الرابط الفعلي بين container والألوان.
fun rxContainerStyle(attrs) {
    let css = "display:flex;";
    if (has(attrs, "row")) { css = css + "flex-direction:row;"; }
    else { css = css + "flex-direction:column;"; }
    if (has(attrs, "wrap") and attrs["wrap"] == "true") { css = css + "flex-wrap:wrap;"; }
    if (has(attrs, "gap")) { css = css + "gap:" + rxPx(attrs["gap"]) + ";"; }
    if (has(attrs, "padding")) { css = css + "padding:" + rxPx(attrs["padding"]) + ";"; }

    let bg = "";
    if (has(attrs, "variant")) { bg = rxVariantPalette(attrs["variant"])["bg"]; }
    if (has(attrs, "bg")) { bg = attrs["bg"]; }
    if (bg != "") { css = css + "background:" + bg + ";"; }

    if (has(attrs, "radius")) { css = css + "border-radius:" + rxPx(attrs["radius"]) + ";"; }
    if (has(attrs, "border")) {
        let borderColor = "#33333f";
        if (has(attrs, "borderColor")) { borderColor = attrs["borderColor"]; }
        css = css + "border:" + rxPx(attrs["border"]) + " solid " + borderColor + ";";
    }
    if (has(attrs, "shadow") and attrs["shadow"] == "true") { css = css + "box-shadow:0 2px 10px rgba(0,0,0,.15);"; }
    if (has(attrs, "width")) { css = css + "width:" + rxPx(attrs["width"]) + ";"; }
    if (has(attrs, "height")) { css = css + "height:" + rxPx(attrs["height"]) + ";"; }
    if (has(attrs, "align")) { css = css + "align-items:" + attrs["align"] + ";"; }
    if (has(attrs, "justify")) { css = css + "justify-content:" + attrs["justify"] + ";"; }
    return css;
}

// أيقونة/ألوان Banner حسب type= — نفس الأنواع المستخدمة في محرّك Indsin الأصلي (info/success/
// warning/error/action) حتى تبقى دلالة "type" واحدة عبر المشروع كله لا نظامين مختلفين.
fun rxBannerIcon(type) {
    if (type == "success") { return "✔"; }
    if (type == "warning") { return "⚠"; }
    if (type == "error" or type == "danger") { return "✕"; }
    if (type == "action") { return "★"; }
    return "ℹ"; // info أو غير معروف
}
fun rxBannerColors(type) {
    if (type == "success") { return { "bg": "#173a22", "accent": "#2e9f43", "fg": "#dff5e4" }; }
    if (type == "warning") { return { "bg": "#3a3115", "accent": "#d4a72c", "fg": "#f7edd0" }; }
    if (type == "error" or type == "danger") { return { "bg": "#3a1c1c", "accent": "#d14545", "fg": "#f8dcdc" }; }
    if (type == "action")  { return { "bg": "#241f3a", "accent": "#7c5cff", "fg": "#e6e1ff" }; }
    return { "bg": "#1c2436", "accent": "#3a6ec4", "fg": "#dbe6f7" }; // info (الافتراضي)
}

// Banner: شريط تنبيه — إما بشكل مركّب (children من text/button/... مثل Indsin تماماً) أو بشكل
// مختصر مُغلَق ذاتياً (title=/text= بلا أبناء) للاستخدام السريع. اللون يُورَث للأبناء عبر
// CSS inheritance العادي (color على الحاوية الخارجية) بدل تكراره في كل عنصر ابن يدوياً.
fun rxRenderBanner(node) {
    let attrs = node["attrs"];
    let type = "info";
    if (has(attrs, "type")) { type = attrs["type"]; }
    let colors = rxBannerColors(type);
    let bg = colors["bg"];
    if (has(attrs, "bg")) { bg = attrs["bg"]; }

    let radius = "10px";
    if (has(attrs, "radius")) { radius = rxPx(attrs["radius"]); }

    let css = "display:flex;align-items:flex-start;gap:12px;padding:14px 16px;" +
              "border-radius:" + radius + ";border-right:4px solid " + colors["accent"] + ";" +
              "background:" + bg + ";color:" + colors["fg"] + ";";

    let inner = "<div style=\"font-size:20px;line-height:1;\">" + rxBannerIcon(type) + "</div>\n";
    inner = inner + "<div style=\"flex:1;\">";
    if (has(attrs, "title")) {
        inner = inner + "<div style=\"font-weight:700;margin-bottom:4px;\">" + rxEscapeHtml(attrs["title"]) + "</div>";
    }
    if (len(node["children"]) > 0) { inner = inner + rxRenderChildren(node); }
    else if (has(attrs, "text")) { inner = inner + "<div>" + rxEscapeHtml(attrs["text"]) + "</div>"; }
    inner = inner + "</div>\n";

    return "<div class=\"rinxg-banner\" style=\"" + css + "\">" + inner + "</div>\n";
}

fun rxRenderChildren(node) {
    let inner = "";
    let i = 0;
    while (i < len(node["children"])) {
        inner = inner + rxRenderElement(node["children"][i]);
        i = i + 1;
    }
    return inner;
}

fun rxRenderElement(node) {
    let tag = node["tag"];
    let attrs = node["attrs"];
    let txt = rxEscapeHtml(node["text"]);

    if (tag == "container") {
        return "<div class=\"rinxg-container\" style=\"" + rxContainerStyle(attrs) + "\">" + rxRenderChildren(node) + "</div>\n";
    }
    if (tag == "banner") {
        return rxRenderBanner(node);
    }
    if (tag == "heading") {
        let level = "2";
        if (has(attrs, "level")) { level = attrs["level"]; }
        let css = "";
        if (has(attrs, "color")) { css = css + "color:" + attrs["color"] + ";"; }
        return "<h" + level + " style=\"" + css + "\">" + txt + "</h" + level + ">\n";
    }
    if (tag == "text") {
        let css = "";
        if (has(attrs, "color")) { css = css + "color:" + attrs["color"] + ";"; }
        if (has(attrs, "size")) { css = css + "font-size:" + rxPx(attrs["size"]) + ";"; }
        return "<p style=\"" + css + "\">" + txt + "</p>\n";
    }
    if (tag == "button") {
        let css = rxButtonStyle(attrs);
        let disabled = has(attrs, "disabled") and attrs["disabled"] == "true";
        if (has(attrs, "href") and !disabled) {
            return "<a class=\"rinxg-btn\" href=\"" + rxEscapeHtml(attrs["href"]) + "\" style=\"" + css + "\">" + txt + "</a>\n";
        }
        let disAttr = "";
        if (disabled) { disAttr = " disabled"; }
        return "<button class=\"rinxg-btn\" style=\"" + css + "\"" + disAttr + ">" + txt + "</button>\n";
    }
    if (tag == "input") {
        let placeholder = "";
        if (has(attrs, "placeholder")) { placeholder = attrs["placeholder"]; }
        return "<input type=\"text\" placeholder=\"" + rxEscapeHtml(placeholder) +
               "\" style=\"padding:8px;border:1px solid #ccc;border-radius:6px;\">\n";
    }
    if (tag == "image") {
        let src = "";
        if (has(attrs, "src")) { src = attrs["src"]; }
        let css = "";
        if (has(attrs, "width")) { css = css + "width:" + rxPx(attrs["width"]) + ";"; }
        return "<img src=\"" + rxEscapeHtml(src) + "\" style=\"" + css + "\">\n";
    }
    if (tag == "link") {
        let href = "#";
        if (has(attrs, "href")) { href = attrs["href"]; }
        return "<a href=\"" + rxEscapeHtml(href) + "\">" + txt + "</a>\n";
    }
    if (tag == "list") {
        return "<ul>\n" + rxRenderChildren(node) + "</ul>\n";
    }
    if (tag == "item") {
        return "<li>" + txt + "</li>\n";
    }

    // وسم غير معروف: لا يُسقَط بصمت، بل يُصيَّر كـ <div> يحمل اسمه كسمة بيانات (لتشخيص الأخطاء الإملائية)
    return "<div data-rinxg-tag=\"" + rxEscapeHtml(tag) + "\">" + txt + rxRenderChildren(node) + "</div>\n";
}

fun rxRenderStyleBlock(styleProps) {
    let css = ":root{";
    let ks = keys(styleProps);
    let i = 0;
    while (i < len(ks)) {
        let k = ks[i];
        css = css + "--rinxg-" + k + ":" + styleProps[k] + ";";
        i = i + 1;
    }
    css = css + "}\n";
    css = css + "*{box-sizing:border-box;}\n";
    css = css + "body{background:var(--rinxg-bg,#ffffff);font-family:var(--rinxg-font,sans-serif);margin:0;padding:24px;}\n";
    // ستايل أساسي مشترك للأزرار: حالات hover/active/focus/disabled لا يمكن التعبير عنها عبر
    // style= المضمّن (inline)، فتُعرَّف مرة واحدة هنا وتُطبَّق على كل زر عبر class="rinxg-btn".
    css = css + ".rinxg-btn{text-decoration:none;}\n";
    css = css + ".rinxg-btn:hover{filter:brightness(1.1);}\n";
    css = css + ".rinxg-btn:active{transform:translateY(1px);filter:brightness(0.95);}\n";
    css = css + ".rinxg-btn:focus-visible{outline:2px solid #7c5cff;outline-offset:2px;}\n";
    css = css + ".rinxg-btn[disabled]{filter:grayscale(.3);}\n";
    return css;
}

// الدالة الرئيسية: تحوّل مصدر RinXG كاملاً إلى صفحة HTML+CSS جاهزة للعرض في أي متصفّح/WebView
fun rxToHtml(source) {
    let ast = rxParse(source);
    let body = rxRenderChildren({ "children": ast["children"] });
    let html = "<!DOCTYPE html>\n<html lang=\"ar\" dir=\"rtl\">\n<head>\n<meta charset=\"utf-8\">\n" +
               "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n" +
               "<title>" + rxEscapeHtml(ast["title"]) + "</title>\n<style>\n" +
               rxRenderStyleBlock(ast["style"]) + "</style>\n</head>\n<body>\n" + body + "</body>\n</html>\n";
    return html;
}

// يحلّل مصدر RinXG إلى AST خام (map) بلا تصيير — مفيد للأدوات (فاحص أخطاء، محرِّر مرئي، إلخ)
fun rxParseToAst(source) {
    return rxParse(source);
}

// معلومات وصفية عن اللغة، بنفس أسلوب bobInfo/ringoInfo/pkgInfo في هذا المشروع
fun rxInfo() {
    return {
        "name": "rinxg",
        "version": "1.1.0",
        "description": "لغة تصريحية لتصميم واجهات الويب فوق Rin: تُترجَم إلى صفحة HTML+CSS حقيقية جاهزة للعرض، مع مكتبة أزرار/حاويات/تنبيهات احترافية (variant/size/radius/disabled/shadow...)",
        "exports": ["rxToHtml", "rxParseToAst", "rxInfo"]
    };
}
)RINXGOGRIN";

static const char* kLib_rinzip_og_rin = R"RINZIPOGRIN(
// ============================================================================
//  lib/rinzip.og.rin — RINZIP v2: أرشيفات ZIP حقيقية بضغط DEFLATE فعلي (zlib)
//  استيراد:
//    @import "lib/rinzip.og.rin";
//    @import "lib/rinzip.og.rin" as rinzip;
//
//  مكتبة Rin خالصة فوق طبقة native رقيقة جداً (4 دوال فقط جديدة عن v1):
//    crc32/chr/ord/substr/readFile/writeFile  (كما في v1)
//    zlibDeflateRaw(bytes) -> بايتات مضغوطة بـ DEFLATE خام (RFC 1951، بلا رأس zlib/gzip)
//    zlibInflateRaw(compressed, expectedSize) -> فكّ الضغط إلى الحجم الأصلي بالضبط
//  الاثنتان الأخيرتان native حقيقي (C++ / zlib النظامي)، تماماً بنفس الصيغة التي
//  تتوقّعها ZIP لكل entry بطريقة Deflate (method=8) — فالأرشيف الناتج هنا مضغوط
//  فعلياً (لا "تخزين" فارغ كما في v1) ويُفتح مباشرة بأي أداة ZIP قياسية (unzip,
//  7-Zip, مستكشف الملفات، Windows Explorer...)، والعكس صحيح: RINZIP يقرأ أي
//  أرشيف .zip حقيقي من مصدر خارجي طالما اعتمد Store (method=0) أو Deflate
//  (method=8) — وهما الطريقتان الوحيدتان اللتان يُنتجهما أي أداة ZIP عادية تقريباً.
//
//  اختيار الطريقة تلقائي وذكي لكل entry على حدة (بنفس منطق أدوات ZIP الحقيقية):
//  يُضغَط المحتوى أولاً، فإن كانت النتيجة المضغوطة أصغر فعلاً من الأصل يُكتَب
//  method=8 (Deflate)، وإلا (محتوى فارغ/صغير جداً/عشوائي غير قابل للضغط) يُكتَب
//  method=0 (Store) مباشرة بلا داعٍ لهدر وقت/مساحة على ضغط لا يفيد.
//
//  مثال سريع (إنشاء أرشيف مضغوط فعلياً ثم قراءته مباشرة):
//    let entries = [
//        rzFileEntry("hello.txt", "أهلاً من RINZIP، هذا نص طويل بما يكفي ليُضغَط فعلياً..."),
//        rzFileEntry("data/notes.txt", "سطر أول\nسطر ثانٍ"),
//        rzDirEntry("data")
//    ];
//    let info = rzSaveArchive(entries, "out.zip");
//    print info;  // {ok:true, path:"out.zip", bytes:.., originalBytes:.., entries:3, ratio:0.62}
//
//    let data = readFile("out.zip");
//    let listing = rzParseEntries(data);
//    print rzFormatListing(listing);          // جدول مقروء بالاسم/الحجم/الطريقة/نسبة الضغط/CRC
//    print rzExtractEntry(data, listing[0]);  // {ok:true, name:"hello.txt", content:"أهلاً من..."}
//
//  أو مباشرة من القرص + استخراج كل شيء إلى مجلد:
//    let entries2 = rzListArchive("out.zip");
//    print rzSaveExtracted(readFile("out.zip"), entries2, "extracted");
//
//  محتوى ثنائي جاهز مضغوط أصلاً (PNG/JPEG/MP3/ZIP آخر...) لا يستفيد من إعادة
//  ضغطه (وقد يكبر قليلاً)؛ لهذا الحالة استخدم rzFileEntryStored بدل rzFileEntry
//  لإجبار method=0 صراحة وتوفير وقت CPU الضائع على محاولة ضغط لن تفيد.
// ============================================================================

// ---- الجزء 1: توقيعات ZIP الثابتة (بايتات خام لتمييز كل سجل) ----------------
fun rzSigLocal()   { return chr(80) + chr(75) + chr(3) + chr(4); }  // "PK\x03\x04"
fun rzSigCentral()  { return chr(80) + chr(75) + chr(1) + chr(2); } // "PK\x01\x02"
fun rzSigEocd()     { return chr(80) + chr(75) + chr(5) + chr(6); } // "PK\x05\x06"

// ---- الجزء 2: ترميز/فكّ أعداد صحيحة little-endian (لبنات صيغة ZIP الثنائية) ---

// يُرمّز n كعدد 16-بت little-endian (بايتان خام)
fun rzLE16(n) {
    return chr(n % 256) + chr(floor(n / 256) % 256);
}

// يُرمّز n كعدد 32-بت little-endian (4 بايتات خام)
fun rzLE32(n) {
    let b0 = n % 256;
    let b1 = floor(n / 256) % 256;
    let b2 = floor(n / 65536) % 256;
    let b3 = floor(n / 16777216) % 256;
    return chr(b0) + chr(b1) + chr(b2) + chr(b3);
}

// يقرأ عدداً 16-بت little-endian من data ابتداءً من الموضع pos
fun rzReadU16(data, pos) {
    return ord(charAt(data, pos)) + ord(charAt(data, pos + 1)) * 256;
}

// يقرأ عدداً 32-بت little-endian من data ابتداءً من الموضع pos
fun rzReadU32(data, pos) {
    let b0 = ord(charAt(data, pos));
    let b1 = ord(charAt(data, pos + 1));
    let b2 = ord(charAt(data, pos + 2));
    let b3 = ord(charAt(data, pos + 3));
    return b0 + b1 * 256 + b2 * 65536 + b3 * 16777216;
}

// ---- الجزء 3: بناء مُدخَلات (entries) قبل الأرشفة ---------------------------
// مُدخَل = { name, content, isDir, forceStore }
//   forceStore=true  -> method=0 (Store) دائماً، بلا محاولة ضغط إطلاقاً
//   forceStore=false -> يُحاوَل الضغط أولاً، ويُستخدَم فقط إن كان مفيداً فعلاً

// مُدخَل ملف عادي؛ يُضغَط تلقائياً بـ Deflate إن كان ذلك يُصغّر الحجم فعلاً،
// وإلا يُخزَّن بلا ضغط (Store) تلقائياً — الاختيار الأمثل بلا أي تدخّل يدوي
fun rzFileEntry(name, content) {
    return { name: name, content: content, isDir: false, forceStore: false };
}

// مُدخَل ملف يُجبَر على Store (بلا أي محاولة ضغط) — مناسب لمحتوى ثنائي مضغوط
// أصلاً (PNG/JPEG/MP4/ZIP متداخل...) لتوفير وقت CPU الذي لن يُصغّر شيئاً أصلاً
fun rzFileEntryStored(name, content) {
    return { name: name, content: content, isDir: false, forceStore: true };
}

// مُدخَل مجلد فارغ (بلا محتوى)؛ يضيف "/" لنهاية الاسم تلقائياً إن لم توجد أصلاً،
// تماماً كما تتوقّع أدوات ZIP القياسية لتمييز إدخالات المجلدات عن الملفات
fun rzDirEntry(name) {
    let normalized = name;
    if (len(normalized) == 0 or charAt(normalized, len(normalized) - 1) != "/") {
        normalized = normalized + "/";
    }
    return { name: normalized, content: "", isDir: true, forceStore: true };
}

// ---- الجزء 4: إنشاء أرشيف (كتابة، بضغط Deflate حقيقي عند الإفادة) -----------

// يبني محتوى أرشيف ZIP كامل (بايتات خام كنص) من مصفوفة entries. لكل ملف غير
// مُجبَر على Store: يُضغَط عبر zlibDeflateRaw (native)، وتُقارَن النتيجة بالحجم
// الأصلي؛ يُعتمَد الضغط (method=8) فقط إن كان أصغر فعلاً، وإلا يُخزَّن الأصل خاماً
// (method=0). يُعيد النص الخام مباشرة دون كتابته على القرص؛ استخدم rzSaveArchive
// للكتابة المباشرة إلى ملف
fun rzCreateArchive(entries) {
    let body = "";
    let central = "";
    let offset = 0;
    let count = 0;
    let i = 0;
    while (i < len(entries)) {
        let e = entries[i];
        let name = e["name"];
        let isDir = has(e, "isDir") and e["isDir"];
        let forceStore = has(e, "forceStore") and e["forceStore"];

        let content = "";
        if (isDir == false) { content = e["content"]; }
        let size = len(content);
        let crc = 0;
        if (isDir == false) { crc = crc32(content); }

        // اختيار الطريقة: نحاول الضغط أولاً (إن لم يكن مُجبَراً على Store وله محتوى
        // فعلي)، ونعتمده فقط إن أصغر النتيجة فعلاً — تماماً كسلوك zip/7z الحقيقي
        let method = 0;
        let payload = content;
        let compSize = size;
        if (isDir == false and forceStore == false and size > 0) {
            let compressed = zlibDeflateRaw(content);
            if (len(compressed) < size) {
                method = 8;
                payload = compressed;
                compSize = len(compressed);
            }
        }

        let localHeader = rzSigLocal()
            + rzLE16(20) + rzLE16(0) + rzLE16(method)
            + rzLE16(0) + rzLE16(0)
            + rzLE32(crc)
            + rzLE32(compSize) + rzLE32(size)
            + rzLE16(len(name)) + rzLE16(0)
            + name;

        body = body + localHeader + payload;

        let centralHeader = rzSigCentral()
            + rzLE16(20) + rzLE16(20) + rzLE16(0) + rzLE16(method)
            + rzLE16(0) + rzLE16(0)
            + rzLE32(crc)
            + rzLE32(compSize) + rzLE32(size)
            + rzLE16(len(name)) + rzLE16(0) + rzLE16(0)
            + rzLE16(0) + rzLE16(0) + rzLE32(0)
            + rzLE32(offset)
            + name;

        central = central + centralHeader;
        offset = offset + len(localHeader) + compSize;
        count = count + 1;
        i = i + 1;
    }

    let endRecord = rzSigEocd()
        + rzLE16(0) + rzLE16(0)
        + rzLE16(count) + rzLE16(count)
        + rzLE32(len(central)) + rzLE32(offset)
        + rzLE16(0);

    return body + central + endRecord;
}

// يبني الأرشيف عبر rzCreateArchive ثم يكتبه مباشرة إلى outPath على القرص، ويُعيد
// ملخّصاً يتضمّن حجم الأرشيف الناتج والحجم الأصلي قبل الضغط ونسبة الضغط الإجمالية
fun rzSaveArchive(entries, outPath) {
    let bytes = rzCreateArchive(entries);
    writeFile(outPath, bytes);
    let originalBytes = 0;
    let i = 0;
    while (i < len(entries)) {
        let e = entries[i];
        if ((has(e, "isDir") and e["isDir"]) == false) {
            originalBytes = originalBytes + len(e["content"]);
        }
        i = i + 1;
    }
    let ratio = 1.0;
    if (originalBytes > 0) { ratio = len(bytes) / originalBytes; }
    return {
        ok: true, path: outPath, bytes: len(bytes),
        originalBytes: originalBytes, entries: len(entries), ratio: ratio
    };
}

// ---- الجزء 5: قراءة/تحليل أرشيف (Central Directory + EOCD) -----------------

// يبحث عن موضع سجل "نهاية الدليل المركزي" (EOCD) داخل data بالمسح من آخر الملف
// إلى الوراء (لأن حقل تعليق الأرشيف اختياري ومتغيّر الطول في آخره)، أو -1 إن لم
// يُعثر على توقيع EOCD إطلاقاً (أرشيف تالف أو ليس ZIP أصلاً)
fun rzFindEocd(data) {
    let n = len(data);
    if (n < 22) { return -1; }
    let sig = rzSigEocd();
    let minPos = n - 22 - 65557; // 65535 (أقصى تعليق) + 22 (حجم السجل الثابت)
    if (minPos < 0) { minPos = 0; }
    let pos = n - 22;
    while (pos >= minPos) {
        if (substr(data, pos, 4) == sig) { return pos; }
        pos = pos - 1;
    }
    return -1;
}

// يحلّل الدليل المركزي الكامل لأرشيف ZIP خام (نص data من readFile("x.zip") مثلاً)،
// ويُعيد مصفوفة مُدخَلات وصفية { name, method, crc, compressedSize, size,
// localHeaderOffset, isDir }. تُعيد مصفوفة فارغة إن لم يكن data أرشيف ZIP صالحاً
fun rzParseEntries(data) {
    let eocdPos = rzFindEocd(data);
    if (eocdPos == -1) { return []; }

    let totalEntries = rzReadU16(data, eocdPos + 10);
    let centralOffset = rzReadU32(data, eocdPos + 16);
    let centralSig = rzSigCentral();

    let entries = [];
    let pos = centralOffset;
    let i = 0;
    while (i < totalEntries) {
        if (substr(data, pos, 4) != centralSig) {
            // دليل مركزي غير متّسق (ملف تالف) — نتوقّف بأمان بما جُمع حتى الآن بدل الانهيار
            i = totalEntries;
        } else {
            let method = rzReadU16(data, pos + 10);
            let crc = rzReadU32(data, pos + 16);
            let compSize = rzReadU32(data, pos + 20);
            let uncompSize = rzReadU32(data, pos + 24);
            let nameLen = rzReadU16(data, pos + 28);
            let extraLen = rzReadU16(data, pos + 30);
            let commentLen = rzReadU16(data, pos + 32);
            let localOffset = rzReadU32(data, pos + 42);
            let name = substr(data, pos + 46, nameLen);
            let isDir = len(name) > 0 and charAt(name, len(name) - 1) == "/";

            push(entries, {
                name: name,
                method: method,
                crc: crc,
                compressedSize: compSize,
                size: uncompSize,
                localHeaderOffset: localOffset,
                isDir: isDir
            });

            pos = pos + 46 + nameLen + extraLen + commentLen;
            i = i + 1;
        }
    }
    return entries;
}

// اختصار مريح: يقرأ ملف .zip من القرص ويُحلّله مباشرة (readFile + rzParseEntries)
fun rzListArchive(path) {
    return rzParseEntries(readFile(path));
}

// هل طريقة ضغط entry مدعومة للاستخراج؟ RINZIP v2 يدعم Store (method=0) وDeflate
// (method=8) — وهما الطريقتان الوحيدتان اللتان تُنتجهما أغلب أدوات ZIP الشائعة
fun rzIsSupported(entry) {
    return entry["method"] == 0 or entry["method"] == 8;
}

// ---- الجزء 6: استخراج محتوى مُدخَل واحد --------------------------------------

// يستخرج المحتوى الخام لـ entry واحد (من نتائج rzParseEntries) من data الأصلية.
// يفكّ ضغط Deflate فعلياً عبر zlibInflateRaw عند method=8. يُعيد
// { ok:true, name, content, isDir } عند النجاح، أو { ok:false, name, error }
// عند فشل التحقّق (CRC غير مطابق) أو عدم دعم طريقة الضغط (method != 0 و != 8)
fun rzExtractEntry(data, entry) {
    if (entry["isDir"]) {
        return { ok: true, name: entry["name"], content: "", isDir: true };
    }
    if (rzIsSupported(entry) == false) {
        return {
            ok: false,
            name: entry["name"],
            error: "rzExtractEntry: طريقة ضغط غير مدعومة (method=" + toString(entry["method"]) + "). يدعم RINZIP التخزين (0) وDeflate (8) فقط."
        };
    }
    let base = entry["localHeaderOffset"];
    if (substr(data, base, 4) != rzSigLocal()) {
        return { ok: false, name: entry["name"], error: "rzExtractEntry: توقيع رأس محلي غير صالح عند الإزاحة المحدَّدة (أرشيف تالف؟)" };
    }
    let nameLen = rzReadU16(data, base + 26);
    let extraLen = rzReadU16(data, base + 28);
    let dataStart = base + 30 + nameLen + extraLen;
    let raw = substr(data, dataStart, entry["compressedSize"]);

    let content = raw;
    if (entry["method"] == 8) {
        content = zlibInflateRaw(raw, entry["size"]);
    }

    let actualCrc = crc32(content);
    if (actualCrc != entry["crc"]) {
        return { ok: false, name: entry["name"], error: "rzExtractEntry: فشل التحقّق CRC-32 (المحتوى تالف أو موضع القراءة خاطئ)" };
    }
    return { ok: true, name: entry["name"], content: content, isDir: false };
}

// يستخرج كل entries من data ويُعيد مصفوفة نتائج rzExtractEntry بنفس الترتيب
// (بلا توقّف عند أول فشل، مفيد لتشخيص كل مشاكل أرشيف دفعة واحدة)
fun rzExtractAll(data, entries) {
    let results = [];
    let i = 0;
    while (i < len(entries)) {
        push(results, rzExtractEntry(data, entries[i]));
        i = i + 1;
    }
    return results;
}

// يستخرج كل entries فعلياً إلى القرص تحت destDir (writeFile تُنشئ المجلدات
// الأب تلقائياً)، ويُعيد ملخّصاً { written: [أسماء نجحت], errors: [نتائج فشلت] }.
// مُدخَلات المجلدات (isDir) تُتجاهَل بصمت (لا تحتاج إنشاءً صريحاً هنا)
fun rzSaveExtracted(data, entries, destDir) {
    let written = [];
    let errors = [];
    let i = 0;
    while (i < len(entries)) {
        let entry = entries[i];
        if (entry["isDir"] == false) {
            let r = rzExtractEntry(data, entry);
            if (r["ok"]) {
                writeFile(destDir + "/" + entry["name"], r["content"]);
                push(written, entry["name"]);
            } else {
                push(errors, r);
            }
        }
        i = i + 1;
    }
    return { written: written, errors: errors };
}

// ---- الجزء 7: تحقّق وتقارير مقروءة ------------------------------------------

// يتحقّق من سلامة كل مُدخَل مدعوم داخل الأرشيف (يفكّ الضغط فعلياً عند method=8
// ويتحقّق من CRC-32 لكل ملف) دون كتابة أي شيء على القرص؛ يُعيد { ok: لا يوجد أي
// خطأ إطلاقاً, invalidCount, results: مصفوفة كل rzExtractEntry }
fun rzValidateArchive(data, entries) {
    let results = rzExtractAll(data, entries);
    let invalidCount = 0;
    let i = 0;
    while (i < len(results)) {
        if (results[i]["ok"] == false) { invalidCount = invalidCount + 1; }
        i = i + 1;
    }
    return { ok: invalidCount == 0, invalidCount: invalidCount, results: results };
}

// جدول نصي مقروء لمصفوفة entries (اسم، حجم أصلي، حجم مضغوط، طريقة، نسبة ضغط،
// CRC)، بنفس روح "unzip -lv"، جاهز للطباعة مباشرة عبر print
fun rzFormatListing(entries) {
    let lines = [];
    push(lines, "الاسم                                   الحجم    مضغوط    الطريقة   النسبة   CRC-32");
    let i = 0;
    while (i < len(entries)) {
        let e = entries[i];
        let methodLabel = "Store";
        if (e["method"] == 8) { methodLabel = "Deflate"; }
        if (e["method"] != 0 and e["method"] != 8) { methodLabel = "#" + toString(e["method"]); }
        let ratioLabel = "-";
        if (e["size"] > 0) {
            let pct = floor((1.0 - (e["compressedSize"] / e["size"])) * 100);
            ratioLabel = toString(pct) + "%";
        }
        push(lines, e["name"] + "  " + toString(e["size"]) + "  " + toString(e["compressedSize"]) + "  " + methodLabel + "  " + ratioLabel + "  " + toString(e["crc"]));
        i = i + 1;
    }
    push(lines, "-- المجموع: " + toString(len(entries)) + " مُدخَل --");
    return join(lines, "\n");
}

// ---- الجزء 8: معلومات وصفية عن المكتبة (بنفس أسلوب bobInfo/ringoInfo/rxInfo) --
fun rzInfo() {
    return {
        name: "rinzip",
        version: "2.0.0",
        description: "قراءة وكتابة أرشيفات ZIP حقيقية (صيغة PKWARE) بلغة Rin، بضغط Deflate فعلي عبر zlib الأصلي (native) مع تخزين تلقائي (Store) عند عدم إفادة الضغط",
        exports: [
            "rzFileEntry", "rzFileEntryStored", "rzDirEntry", "rzCreateArchive", "rzSaveArchive",
            "rzParseEntries", "rzListArchive", "rzExtractEntry", "rzExtractAll",
            "rzSaveExtracted", "rzValidateArchive", "rzFormatListing", "rzInfo"
        ]
    };
}
)RINZIPOGRIN";


// ============================================================================
// Embedded RelyRIN — generated from lib/relyRIN.og.rin
// ============================================================================
static const char* kLib_relyRIN_og_rin = R"RELYRINOGRIN(
// ============================================================================
// lib/relyRIN.og.rin — RelyRIN Media + Live Markdown Preview
// ============================================================================
// مكتبة وسائط ومعاينة حية مكتوبة بالكامل بلغة Rin.
// لا تعتمد على Java/Kotlin/JS داخل المكتبة نفسها؛ تُخرج HTML/CSS قياسيين يمكن
// عرضه في WebView/متصفح، بما في ذلك YouTube عبر iframe.
// 
// الاستيراد:
//   @import "lib/relyRIN.og.rin";
//   let page = relyLive("# Hello\n\n**Rin**");
//   let yt = relyYoutube("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
//   let md = relyMarkdownFile("README.md");
//
// المبادئ:
//   - Markdown -> HTML
//   - Theme/Style -> CSS
//   - Image/Audio/Video -> HTML5 media
//   - YouTube -> privacy-friendly embed URL
//   - Live preview -> يعيد وثيقة HTML كاملة في كل استدعاء
//   - لا تنفيذ JavaScript من Markdown؛ الروابط والنصوص تُهَرَّب لمنع HTML injection
// ============================================================================

@import "lib/strings.og.rin";

// ----------------------------- Utilities -----------------------------------

fun relyEscape(s) {
    let x = replace(s, "&", "&amp;");
    x = replace(x, "<", "&lt;");
    x = replace(x, ">", "&gt;");
    x = replace(x, "\"", "&quot;");
    x = replace(x, "'", "&#39;");
    return x;
}

fun relyAttr(s) {
    return relyEscape(toString(s));
}

fun relyStyleValue(s) {
    // قيم style المخصصة تُمرّر كنص؛ لا تُفسّر كـ HTML.
    return relyAttr(s);
}

fun relyLineArray(md) {
    return split(replace(md, "\r\n", "\n"), "\n");
}

fun relyStarts(s, p) { return startsWith(s, p); }

fun relyHeadingLevel(s) {
    let n = 0;
    while (n < len(s) and charAt(s, n) == "#") { n = n + 1; }
    return n;
}

fun relyStripHeading(s, n) {
    let x = substr(s, n);
    if (startsWith(x, " ")) { x = substr(x, 1); }
    return trim(x);
}

fun relyFind(s, needle, start) {
    let r = indexOf(substr(s, start), needle);
    if (r < 0) { return -1; }
    return r + start;
}

fun relyYoutubeId(url) {
    let u = trim(url);
    // youtu.be/<id>
    let p = indexOf(u, "youtu.be/");
    if (p >= 0) {
        let x = substr(u, p + 9);
        let q = indexOf(x, "?");
        if (q >= 0) { x = substr(x, 0, q); }
        q = indexOf(x, "&");
        if (q >= 0) { x = substr(x, 0, q); }
        q = indexOf(x, "#");
        if (q >= 0) { x = substr(x, 0, q); }
        return x;
    }
    // youtube.com/watch?v=<id>
    p = indexOf(u, "v=");
    if (p >= 0) {
        let x = substr(u, p + 2);
        let q = indexOf(x, "&");
        if (q >= 0) { x = substr(x, 0, q); }
        q = indexOf(x, "#");
        if (q >= 0) { x = substr(x, 0, q); }
        return x;
    }
    // /embed/<id> or /shorts/<id>
    p = indexOf(u, "/embed/");
    if (p < 0) { p = indexOf(u, "/shorts/"); }
    if (p >= 0) {
        let x = substr(u, p + 7);
        let q = indexOf(x, "?");
        if (q >= 0) { x = substr(x, 0, q); }
        q = indexOf(x, "&");
        if (q >= 0) { x = substr(x, 0, q); }
        return x;
    }
    return "";
}

// ----------------------------- Style ---------------------------------------

fun relyThemeDefault() {
    return {
        bg: "#ffffff",
        "text": "#202124",
        "muted": "#6b7280",
        "accent": "#7c5cff",
        "accent2": "#22c88e",
        "border": "#e5e7eb",
        "codeBg": "#f5f7fa",
        "quoteBg": "#f8f7ff",
        "cardBg": "#ffffff",
        "link": "#2563eb",
        "radius": "14px",
        "maxWidth": "920px",
        "font": "system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif"
    };
}

fun relyTheme(overrides) {
    let t = relyThemeDefault();
    let ks = keys(overrides);
    let i = 0;
    while (i < len(ks)) {
        t[ks[i]] = overrides[ks[i]];
        i = i + 1;
    }
    return t;
}

fun relyCss(theme) {
    return "<style>\n" +
    ":root{color-scheme:light;}\n" +
    "*{box-sizing:border-box;}\n" +
    "html,body{margin:0;padding:0;background:" + relyStyleValue(theme["bg"]) + ";color:" + relyStyleValue(theme["text"]) + ";}\n" +
    "body{font-family:" + relyStyleValue(theme["font"]) + ";line-height:1.72;}\n" +
    ".rely-page{max-width:" + relyStyleValue(theme["maxWidth"]) + ";margin:0 auto;padding:32px 22px 64px;}\n" +
    ".rely-page h1{font-size:2.15rem;line-height:1.18;margin:0 0 20px;padding-bottom:14px;border-bottom:2px solid " + relyStyleValue(theme["accent"]) + ";}\n" +
    ".rely-page h2{font-size:1.55rem;margin-top:32px;padding-bottom:8px;border-bottom:1px solid " + relyStyleValue(theme["border"]) + ";}\n" +
    ".rely-page h3{font-size:1.25rem;margin-top:26px;}\n" +
    ".rely-page p{margin:12px 0;}\n" +
    ".rely-page a{color:" + relyStyleValue(theme["link"]) + ";text-decoration:none;}\n" +
    ".rely-page a:hover{text-decoration:underline;}\n" +
    ".rely-code{background:" + relyStyleValue(theme["codeBg"]) + ";border:1px solid " + relyStyleValue(theme["border"]) + ";border-radius:" + relyStyleValue(theme["radius"]) + ";padding:16px;overflow:auto;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.92em;}\n" +
    ".rely-inline-code{background:" + relyStyleValue(theme["codeBg"]) + ";border-radius:6px;padding:2px 6px;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;}\n" +
    ".rely-quote{margin:18px 0;padding:12px 16px;border-left:4px solid " + relyStyleValue(theme["accent"]) + ";background:" + relyStyleValue(theme["quoteBg"]) + ";border-radius:0 " + relyStyleValue(theme["radius"]) + " " + relyStyleValue(theme["radius"]) + " 0;}\n" +
    ".rely-media{display:block;width:100%;max-width:100%;margin:18px auto;border-radius:" + relyStyleValue(theme["radius"]) + ";overflow:hidden;}\n" +
    ".rely-media img,.rely-media video{display:block;width:100%;height:auto;}\n" +
    ".rely-audio{width:100%;}\n" +
    ".rely-video{background:#000;}\n" +
    ".rely-youtube{position:relative;width:100%;aspect-ratio:16/9;background:#000;border-radius:" + relyStyleValue(theme["radius"]) + ";overflow:hidden;margin:20px 0;}\n" +
    ".rely-youtube iframe{position:absolute;inset:0;width:100%;height:100%;border:0;}\n" +
    ".rely-card{background:" + relyStyleValue(theme["cardBg"]) + ";border:1px solid " + relyStyleValue(theme["border"]) + ";border-radius:" + relyStyleValue(theme["radius"]) + ";padding:18px;margin:18px 0;}\n" +
    ".rely-hr{border:0;border-top:1px solid " + relyStyleValue(theme["border"]) + ";margin:28px 0;}\n" +
    ".rely-table{width:100%;border-collapse:collapse;margin:18px 0;}\n" +
    ".rely-table th,.rely-table td{border:1px solid " + relyStyleValue(theme["border"]) + ";padding:9px 11px;text-align:start;}\n" +
    ".rely-table th{background:" + relyStyleValue(theme["codeBg"]) + ";}\n" +
    ".rely-task{list-style:none;margin-left:-24px;}\n" +
    "</style>\n";
}

// ----------------------------- Inline Markdown -----------------------------

fun relyInline(s) {
    let x = relyEscape(s);

    // Images and links are handled before emphasis.
    // ![alt](src)
    while (true) {
        let p = indexOf(x, "![");
        if (p < 0) { break; }
        let a = relyFind(x, "](", p + 2);
        if (a < 0) { break; }
        let e = relyFind(x, ")", a + 2);
        if (e < 0) { break; }
        let alt = substr(x, p + 2, a - (p + 2));
        let src = substr(x, a + 2, e - (a + 2));
        let tag = "<span class=\"rely-media\"><img src=\"" + relyAttr(src) + "\" alt=\"" + relyAttr(alt) + "\" loading=\"lazy\"></span>";
        x = substr(x, 0, p) + tag + substr(x, e + 1);
    }

    while (true) {
        let p = indexOf(x, "[");
        if (p < 0) { break; }
        let a = relyFind(x, "](", p + 1);
        if (a < 0) { break; }
        let e = relyFind(x, ")", a + 2);
        if (e < 0) { break; }
        let label = substr(x, p + 1, a - (p + 1));
        let href = substr(x, a + 2, e - (a + 2));
        let tag = "<a href=\"" + relyAttr(href) + "\" target=\"_blank\" rel=\"noopener noreferrer\">" + label + "</a>";
        x = substr(x, 0, p) + tag + substr(x, e + 1);
    }

    // Inline code first.
    while (true) {
        let p = indexOf(x, "`");
        if (p < 0) { break; }
        let e = relyFind(x, "`", p + 1);
        if (e < 0) { break; }
        let c = substr(x, p + 1, e - p - 1);
        x = substr(x, 0, p) + "<code class=\"rely-inline-code\">" + c + "</code>" + substr(x, e + 1);
    }

    // Strong / emphasis / strike.
    x = relyReplacePair(x, "**", "<strong>", "</strong>");
    x = relyReplacePair(x, "__", "<strong>", "</strong>");
    x = relyReplacePair(x, "~~", "<del>", "</del>");
    x = relyReplacePair(x, "*", "<em>", "</em>");
    x = relyReplacePair(x, "_", "<em>", "</em>");
    return x;
}

fun relyReplacePair(s, marker, openTag, closeTag) {
    let x = s;
    let p = indexOf(x, marker);
    while (p >= 0) {
        let e = relyFind(x, marker, p + len(marker));
        if (e < 0) { break; }
        let inner = substr(x, p + len(marker), e - p - len(marker));
        x = substr(x, 0, p) + openTag + inner + closeTag + substr(x, e + len(marker));
        p = relyFind(x, marker, p + len(openTag) + len(inner) + len(closeTag));
    }
    return x;
}

// ----------------------------- Media API -----------------------------------

fun relyImage(src, alt) {
    return "<figure class=\"rely-media\"><img src=\"" + relyAttr(src) + "\" alt=\"" + relyAttr(alt) + "\" loading=\"lazy\"></figure>";
}

fun relyAudio(src, controls) {
    let c = controls;
    if (c == nil) { c = true; }
    let attrs = "";
    if (c) { attrs = " controls"; }
    return "<div class=\"rely-media\"><audio class=\"rely-audio\" src=\"" + relyAttr(src) + "\"" + attrs + " preload=\"metadata\"></audio></div>";
}

fun relyVideo(src, controls, autoplay, muted, loop) {
    let attrs = "";
    if (controls == nil or controls) { attrs = attrs + " controls"; }
    if (autoplay) { attrs = attrs + " autoplay"; }
    if (muted) { attrs = attrs + " muted"; }
    if (loop) { attrs = attrs + " loop"; }
    return "<div class=\"rely-media\"><video class=\"rely-video\" src=\"" + relyAttr(src) + "\"" + attrs + " playsinline preload=\"metadata\"></video></div>";
}

fun relyYoutube(url) {
    let id = relyYoutubeId(url);
    if (id == "") {
        return "<div class=\"rely-card\">RelyRIN: YouTube URL غير صالحة.</div>";
    }
    let src = "https://www.youtube-nocookie.com/embed/" + relyAttr(id) + "?rel=0";
    return "<div class=\"rely-youtube\"><iframe src=\"" + src + "\" title=\"YouTube video\" allow=\"accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share\" allowfullscreen loading=\"lazy\"></iframe></div>";
}

fun relyYoutubeIdBlock(id) {
    return relyYoutube("https://www.youtube.com/watch?v=" + id);
}

fun relyMedia(kind, src, options) {
    if (kind == "image") {
        let alt = "RelyRIN media";
        if (has(options, "alt")) { alt = options["alt"]; }
        return relyImage(src, alt);
    }
    if (kind == "audio") {
        let controls = true;
        if (has(options, "controls")) { controls = options["controls"]; }
        return relyAudio(src, controls);
    }
    if (kind == "video") {
        let controls = true;
        let autoplay = false;
        let muted = false;
        let loop = false;
        if (has(options, "controls")) { controls = options["controls"]; }
        if (has(options, "autoplay")) { autoplay = options["autoplay"]; }
        if (has(options, "muted")) { muted = options["muted"]; }
        if (has(options, "loop")) { loop = options["loop"]; }
        return relyVideo(src, controls, autoplay, muted, loop);
    }
    if (kind == "youtube") { return relyYoutube(src); }
    return "<div class=\"rely-card\">RelyRIN: نوع وسائط غير معروف: " + relyEscape(kind) + "</div>";
}

// ----------------------------- Markdown renderer ---------------------------

fun relyRenderMarkdown(md) {
    let lines = relyLineArray(md);
    let out = "";
    let i = 0;
    let inCode = false;
    let codeLang = "";
    let code = "";

    while (i < len(lines)) {
        let raw = lines[i];
        let line = trim(raw);

        if (startsWith(line, "```")) {
            if (inCode) {
                out = out + "<pre class=\"rely-code\" data-lang=\"" + relyAttr(codeLang) + "\"><code>" + relyEscape(code) + "</code></pre>\n";
                inCode = false;
                codeLang = "";
                code = "";
            } else {
                inCode = true;
                codeLang = trim(substr(line, 3));
            }
            i = i + 1;
            continue;
        }

        if (inCode) {
            if (code != "") { code = code + "\n"; }
            code = code + raw;
            i = i + 1;
            continue;
        }

        if (line == "") {
            i = i + 1;
            continue;
        }

        // RelyRIN media directives:
        // ::youtube URL
        // ::image URL | ALT
        // ::audio URL
        // ::video URL
        if (startsWith(line, "::youtube ")) {
            out = out + relyYoutube(trim(substr(line, 10))) + "\n";
            i = i + 1;
            continue;
        }
        if (startsWith(line, "::image ")) {
            let spec = trim(substr(line, 8));
            let parts = split(spec, "|");
            let src = trim(parts[0]);
            let alt = "image";
            if (len(parts) > 1) { alt = trim(parts[1]); }
            out = out + relyImage(src, alt) + "\n";
            i = i + 1;
            continue;
        }
        if (startsWith(line, "::audio ")) {
            out = out + relyAudio(trim(substr(line, 8)), true) + "\n";
            i = i + 1;
            continue;
        }
        if (startsWith(line, "::video ")) {
            out = out + relyVideo(trim(substr(line, 8)), true, false, false, false) + "\n";
            i = i + 1;
            continue;
        }

        let level = relyHeadingLevel(line);
        if (level > 0 and level <= 6 and (len(line) == level or charAt(line, level) == " ")) {
            let title = relyStripHeading(line, level);
            out = out + "<h" + toString(level) + ">" + relyInline(title) + "</h" + toString(level) + ">\n";
            i = i + 1;
            continue;
        }

        if (line == "---" or line == "***" or line == "___") {
            out = out + "<hr class=\"rely-hr\">\n";
            i = i + 1;
            continue;
        }

        if (startsWith(line, ">")) {
            let q = trim(substr(line, 1));
            out = out + "<blockquote class=\"rely-quote\">" + relyInline(q) + "</blockquote>\n";
            i = i + 1;
            continue;
        }

        // unordered / task list
        if (startsWith(line, "- ") or startsWith(line, "* ") or startsWith(line, "+ ")) {
            let marker = substr(line, 0, 1);
            let body = trim(substr(line, 2));
            if (startsWith(body, "[ ] ")) {
                body = substr(body, 4);
                out = out + "<ul><li class=\"rely-task\">☐ " + relyInline(body) + "</li></ul>\n";
            } else if (startsWith(body, "[x] ") or startsWith(body, "[X] ")) {
                body = substr(body, 4);
                out = out + "<ul><li class=\"rely-task\">☑ " + relyInline(body) + "</li></ul>\n";
            } else {
                out = out + "<ul><li>" + relyInline(body) + "</li></ul>\n";
            }
            i = i + 1;
            continue;
        }

        // ordered list
        let firstSpace = indexOf(line, " ");
        if (firstSpace > 0) {
            let prefix = substr(line, 0, firstSpace);
            let dot = substr(prefix, len(prefix) - 1, 1);
            if (dot == "." or dot == ")") {
                let number = substr(prefix, 0, len(prefix) - 1);
                if (relyIsDigits(number)) {
                    out = out + "<ol><li>" + relyInline(trim(substr(line, firstSpace + 1))) + "</li></ol>\n";
                    i = i + 1;
                    continue;
                }
            }
        }

        // table: detect | and next separator line.
        if (indexOf(line, "|") >= 0 and i + 1 < len(lines) and indexOf(lines[i + 1], "|") >= 0) {
            let sep = split(lines[i + 1], "|");
            let validSep = true;
            let si = 0;
            while (si < len(sep)) {
                let cell = trim(sep[si]);
                if (cell != "" and cell != "---" and cell != ":---" and cell != "---:" and cell != ":---:") {
                    validSep = false;
                }
                si = si + 1;
            }
            if (validSep) {
                let cells = split(line, "|");
                out = out + "<table class=\"rely-table\"><thead><tr>";
                let ci = 0;
                while (ci < len(cells)) {
                    let c = trim(cells[ci]);
                    if (c != "") { out = out + "<th>" + relyInline(c) + "</th>"; }
                    ci = ci + 1;
                }
                out = out + "</tr></thead><tbody>\n";
                i = i + 2;
                while (i < len(lines) and indexOf(lines[i], "|") >= 0 and trim(lines[i]) != "") {
                    let row = split(lines[i], "|");
                    out = out + "<tr>";
                    ci = 0;
                    while (ci < len(row)) {
                        let c = trim(row[ci]);
                        if (c != "") { out = out + "<td>" + relyInline(c) + "</td>"; }
                        ci = ci + 1;
                    }
                    out = out + "</tr>\n";
                    i = i + 1;
                }
                out = out + "</tbody></table>\n";
                continue;
            }
        }

        // Paragraph with soft line breaks.
        let para = relyInline(line);
        i = i + 1;
        while (i < len(lines)) {
            let next = trim(lines[i]);
            if (next == "" or startsWith(next, "#") or startsWith(next, ">") or
                startsWith(next, "```") or startsWith(next, "::")) {
                break;
            }
            para = para + "<br>\n" + relyInline(next);
            i = i + 1;
        }
        out = out + "<p>" + para + "</p>\n";
    }

    return out;
}

fun relyIsDigits(s) {
    if (s == "") { return false; }
    let i = 0;
    while (i < len(s)) {
        let c = charAt(s, i);
        if (indexOf("0123456789", c) < 0) { return false; }
        i = i + 1;
    }
    return true;
}

// ----------------------------- Live document API ----------------------------

fun relyDocument(md, theme) {
    let t = relyTheme(theme);
    return "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>RelyRIN Preview</title>" +
        relyCss(t) + "</head><body><main class=\"rely-page\">" +
        relyRenderMarkdown(md) +
        "</main></body></html>";
}

fun relyLive(md) {
    return relyDocument(md, {});
}

fun relyLiveStyled(md, style) {
    return relyDocument(md, style);
}

fun relyMarkdownFile(path) {
    return relyLive(readFile(path));
}

fun relyMarkdownFileStyled(path, style) {
    return relyLiveStyled(readFile(path), style);
}

fun relyWritePreview(md, outputPath) {
    writeFile(outputPath, relyLive(md));
    return outputPath;
}

fun relyWritePreviewStyled(md, outputPath, style) {
    writeFile(outputPath, relyLiveStyled(md, style));
    return outputPath;
}

// تحديث/معاينة ملف MD: القراءة والإخراج إلى HTML في عملية واحدة.
fun relyBuildMarkdown(path, outputPath) {
    let md = readFile(path);
    writeFile(outputPath, relyLive(md));
    return { source: path, output: outputPath, ok: true };
}

fun relyInfo() {
    return {
        "name": "relyRIN",
        "version": "1.0.0",
        "description": "Live Markdown preview, styling and media rendering for Rin, including YouTube embeds",
        "features": [
            "Markdown to HTML",
            "Live HTML document generation",
            "Custom themes and CSS",
            "Images",
            "Audio",
            "HTML5 video",
            "YouTube embeds",
            "Code blocks",
            "Links",
            "Tables",
            "Task lists",
            "Rin media directives"
        ]
    };
}

)RELYRINOGRIN";
static const char* kLib_movingmask_og_rin = R"MOVINGMASKOGRIN(
// ============================================================================
//  lib/movingmask.og.rin  —  Moving Mask: أقنعة متحركة فوق الحاويات والحلقات
// ============================================================================
//  استيراد:
//    @import "lib/movingmask.og.rin";
//    @import "lib/movingmask.og.rin" as mm;
//
//  الفكرة (المفهوم الأساسي):
//  ---------------------------------------------------------------------------
//  "القناع" (mask) في Rin هوية منطقية ثابتة لعنصر/حاوية/مجموعة (انظر lib/maskkit.og.rin
//  و docs/mask.md والدوال الأصلية findMask/maskOf/maskExists...). لكن تلك الهوية بحد
//  ذاتها *ساكنة*: لا موضع لها، ولا سرعة، ولا مسار تتحرك عبره بمرور الزمن.
//
//  "Moving Mask" يضيف طبقة فوق ذلك: قناع يملك حالة حركية (موضع/سرعة/تسارع/مسار)
//  ويمكن أن:
//    1) يتحرك عبر الزمن ضمن حلقة (loop) — كل "دورة" (tick) تُحرّك كل الأقنعة النشطة.
//    2) ينتقل بين الحاويات (containers) — منطقياً عبر سجل داخلي، أو فعلياً عبر ربط
//       مباشر بدوال الحاويات الأصلية في Rin: spawn/create, hasContainer, containerNames,
//       parentOf, childrenOf, siblingsOf, setField, getField, destroyContainer — وأيضاً
//       عبر نظام mask الأصلي (maskParentSet/maskDetach/maskTag/...) حين يكون متاحاً.
//    3) "ينزلق" (slides) كقناع/نافذة فوق مصفوفة أو شبكة (grid) — هذا هو المعنى الكلاسيكي
//       الآخر لـ"moving mask" في معالجة الإشارات/الصور: نافذة صغيرة (kernel/stencil)
//       تتحرك خانة خانة فوق حاوية بيانات أكبر (convolution, moving average/max/min).
//
//  هذا الملف مستقل بذاته (لا يعتمد على أي lib/*.og.rin آخر) ليعمل فور الاستيراد،
//  لكنه يتكامل بشكل طبيعي مع:
//    - lib/loopkit.og.rin   (repeatTimes/stepLoop/loopUntil) لتشغيل mm_tick داخل حلقاتها.
//    - lib/iterkit.og.rin   (iterNew/iterForEach) للمرور على أقنعة متحركة كمُكرِّر.
//    - lib/gridkit.og.rin   (makeGrid/forEachCell) كحاوية ثنائية الأبعاد لقسم الشبكات هنا.
//    - lib/animation.og.rin (anim_lerp/anim_easeInOut...) كدوال Easing لـ mm_animateTo.
//    - lib/maskkit.og.rin + docs/mask.md (هوية القناع الساكنة) كطبقة هوية تحت هذه الطبقة.
//
//  الحالة (state) هنا صريحة دائماً: تُنشئ محرّكاً بـ mm_new() وتُمرّره لكل دالة — لا توجد
//  حالة عامة (global) مخفية، بنفس روح lib/cachekit.og.rin و lib/iterkit.og.rin.
//
//  مثال سريع:
//    let world = mm_new();
//    mm_setBounds(world, 0, 0, 390, 700);
//    mm_spawn(world, "player", 20, 20);
//    mm_setVelocity(world, "player", 5, 2);
//    mm_tick(world, 1);                    // خطوة زمنية واحدة
//    print mm_position(world, "player");   // {x:25, y:22}
//
//  فهرس الأقسام:
//    1)  ثوابت ومساعدات داخلية عامة
//    2)  المحرّك: إنشاء / تدمير / استعلام عن الأقنعة المتحركة
//    3)  الموضع، السرعة، التسارع (الفيزياء الأساسية)
//    4)  المسارات ونقاط الطريق (paths / waypoints)
//    5)  أنماط حركة جاهزة (patrol / orbit / wander / seek / flee)
//    6)  الحدود والمناطق (bounds / regions)
//    7)  التكامل مع الحاويات (containers) — منطقي وفعلي (native bridge)
//    8)  القناع المنزلق فوق المصفوفات (sliding window / 1D convolution)
//    9)  القناع المنزلق فوق الشبكات (grid stencil / 2D convolution / نقل القناع بصرياً)
//    10) الأثر والتاريخ (trail / history)
//    11) القرب والتصادم (distance / proximity / AABB collision)
//    12) التكامل مع الحلقات (loop integration: tick / runLoop / animateTo / forEach)
//    13) الأحداث (lightweight event hooks)
//    14) الفحص والتلخيص (inspection / debug)
//    15) التكامل مع Indsin — مفاهيم مستعارة من محرّك الواجهات Indsintime
//        15.1) Warp   — تصدير حالة قناع كحقول مُسطَّحة
//        15.2) Strand — تسمية بصرية اختيارية للقناع
//        15.3) Fabric — لقطة مسطَّحة بكل الأقنعة
//        15.4) Needle — اختبار إصابة نقطة لمس/نقرة
//        15.5) Shuttle — مقارنة لقطتين وإنتاج Patch[]
//        15.6) Actions — نظام أفعال جاهزة قابل للتوسعة (Action Engine)
//        15.7) Navigation — مشاهد (scenes) عبر الوسوم، بمكدّس تنقّل
//        15.8) Overlay — طبقة علوية فوق الجميع (أولوية في اختبار الإصابة)
//        15.9) Dye — لون بصري اختياري للقناع
//        15.10) Theme — لوحات ألوان مُسمّاة على مستوى المحرّك (Pattern Book)
//        15.11) Object Inspector — بطاقة فحص كاملة لقناع واحد
//    16) التكوين الجماعي والانسيابية (Flocking / Rigid Formations) — جديد
//    17) آلة حالات محدودة لكل قناع (Per-Mask Finite State Machine) — جديد
//    18) التسلسل والاستعادة (Serialization / Save & Load عبر JSON) — جديد
//    19) الفهرسة المكانية لتسريع استعلامات الجوار (Spatial Grid Index) — جديد
//    20) المؤقتات والتهدئة لكل قناع (Timers / Cooldowns) — جديد
//    21) قياس الإطارات في الثانية وخطوة زمنية ثابتة (FPS Meter / Fixed Timestep) — جديد
//    22) أحجام الشاشة/الصفحة والتصميم المتجاوب (Viewport / Responsive Sizing) — جديد
//    23) أنواع شريط التحميل (Progress / Loading Bar Kinds) — جديد
//    24) اللمس وسلاسة الحركة (Touch Gestures & Motion Smoothing) — جديد
//    25) العملات والنقاط القابلة للجمع (Coins / Collectibles / Score) — جديد
//    26) أزرار التحكم الافتراضية (Virtual Joystick & Control Buttons) — جديد
//
//  الإصدار: 1.2.0 — انظر CHANGELOG.md لسجل التغييرات الكامل بين الإصدارات. مدمجة embedded
//  داخل مفسّر RinStudio، متاحة فوراً عبر شاشة "المكتبات" بلا رفع يدوي.
// ============================================================================


// ============================================================================
// 1) ثوابت ومساعدات داخلية عامة
// ============================================================================

let MM_EPSILON = 0.0000001;      // فرق افتراضي لمقارنة الأعداد العشرية
let MM_DEFAULT_HISTORY_LIMIT = 30; // عدد نقاط الأثر (trail) المحفوظة كحد أقصى لكل قناع
let MM_TAU = 6.28318530717958647692; // 2*PI — يُستخدم لاختزال زوايا mm_orbitStep فقط
let MM_VERSION = "1.2.0"; // إصدار مكتبة movingmask نفسها (انظر CHANGELOG.md)
let MM_DEFAULT_FPS_WINDOW = 0.5; // مدة نافذة حساب FPS الافتراضية بالثواني (كل نصف ثانية يُعاد الحساب)

// نص/رقم إصدار مكتبة movingmask الحالي — استخدمه لعرض "عن هذه المكتبة" في تطبيقك
fun mm_version() {
    return MM_VERSION;
}

// يحصر x بين lo و hi (نسخة عددية بسيطة؛ clamp() الأصلية في Rin تعمل على مصفوفات فقط)
fun mm_clampNum(x, lo, hi) {
    if (x < lo) { return lo; }
    if (x > hi) { return hi; }
    return x;
}

// استيفاء خطي بسيط بين a و b عند النسبة t (0..1) — بلا حصر لـ t، للسماح بالاستقراء (extrapolate)
fun mm_lerp(a, b, t) {
    return a + (b - a) * t;
}

// إشارة الرقم: 1 موجب، -1 سالب، 0 صفر
fun mm_sign(x) {
    if (x > 0) { return 1; }
    if (x < 0) { return -1; }
    return 0;
}

// المسافة الإقليدية بين نقطتين {x,y}
fun mm_distance(p1, p2) {
    let dx = p1["x"] - p2["x"];
    let dy = p1["y"] - p2["y"];
    return sqrt(dx * dx + dy * dy);
}

// هل الرقمان متقاربان عملياً (أقل من MM_EPSILON فرقاً)؟
fun mm_nearlyEqual(a, b) {
    let d = a - b;
    if (d < 0) { d = 0 - d; }
    return d < MM_EPSILON;
}

// نسخة سطحية (shallow copy) من نقطة {x,y} — مفيدة كي لا تُعدَّل نقطة مصدر بالمرجع بالخطأ
fun mm_point(x, y) {
    return { x: x, y: y };
}

// هل تحتوي مصفوفة الأسماء arr على القيمة value؟ (مساعد صغير فوق contains() الأصلية،
// موجود هنا فقط توضيحاً؛ contains() الأصلية تدعم المصفوفات مباشرة)
fun mm__arrayHas(arr, value) {
    return contains(arr, value);
}

// يحذف أول ظهور لـ value من مصفوفة arr ويُعيد مصفوفة جديدة بلا تلك القيمة (بلا تعديل arr
// الأصلية بالمرجع؛ remove() الأصلية تعمل على مفاتيح الخرائط فقط لا عناصر المصفوفات)
fun mm__arrayWithout(arr, value) {
    let out = [];
    let i = 0;
    while (i < len(arr)) {
        if (arr[i] != value) { push(out, arr[i]); }
        i = i + 1;
    }
    return out;
}

// --- جيب وجيب تمام داخليان لأجل mm_orbitStep فقط ------------------------------------
// المفسّر لا يوفّر sin/cos فطرياً؛ نفس أسلوب lib/math.og.rin (سلسلة تايلور + اختزال مجال
// إلى المجال (-PI, PI]) لكن بأسماء mm__ خاصة كي لا تتصادم مع sin/cos لو استوردهما المستخدم
// من lib/math.og.rin في نفس الملف

fun mm__reduceAngle(x) {
    return x - MM_TAU * floor((x + PI) / MM_TAU);
}

fun mm__sin(x) {
    let v = mm__reduceAngle(x);
    let v2 = v * v;
    let term = v;
    let total = v;
    let i = 1;
    while (i <= 12) {
        term = term * (0 - v2) / ((2 * i) * (2 * i + 1));
        total = total + term;
        i = i + 1;
    }
    return total;
}

fun mm__cos(x) {
    let v = mm__reduceAngle(x);
    let v2 = v * v;
    let term = 1;
    let total = 1;
    let i = 1;
    while (i <= 12) {
        term = term * (0 - v2) / ((2 * i - 1) * (2 * i));
        total = total + term;
        i = i + 1;
    }
    return total;
}


// ============================================================================
// 2) المحرّك: إنشاء / تدمير / استعلام عن الأقنعة المتحركة
// ============================================================================
//  المحرّك (engine) خريطة واحدة تحمل كل حالة النظام:
//    items:      خريطة اسم القناع -> سجل حالته الكاملة (موضع/سرعة/مسار/حاوية/...)
//    order:      مصفوفة أسماء الأقنعة بترتيب إنشائها (لأجل mm_forEach/mm_names)
//    containers: خريطة اسم حاوية منطقية -> مصفوفة أسماء الأقنعة بداخلها
//    bounds:     حدود العالم {minX,minY,maxX,maxY} أو nil إن لم تُضبط
//    regions:    خريطة اسم منطقة -> حدودها {minX,minY,maxX,maxY}
//    tick:       عدّاد الدورات (frames) التي نُفِّذت عبر mm_tick/mm_runLoop
//    historyLimit: أقصى عدد نقاط أثر تُحفظ لكل قناع (mm_recordHistory)
//    handlers:   خريطة اسم حدث -> مصفوفة دوال مستمعة (mm_on)
//    actions:    خريطة اسم فعل مخصَّص -> دالة مُسجَّلة (mm_defineAction، قسم 15.6)
//    themes:     خريطة اسم Theme -> خريطة أدوار لونية (mm_defineTheme، قسم 15.10)
//    activeTheme: اسم الـTheme النشط حالياً، أو nil (mm_setActiveTheme، قسم 15.10)
//    nav:        حالة التنقّل بين المشاهد: { stack: [...], current: نص أو nil,
//                knownScenes: [...] } (قسم 15.7)

// ينشئ محرّك أقنعة متحركة جديداً وفارغاً
fun mm_new() {
    return {
        items: {},
        order: [],
        containers: {},
        bounds: nil,
        regions: {},
        tick: 0,
        historyLimit: MM_DEFAULT_HISTORY_LIMIT,
        handlers: {},
        actions: {},
        themes: {},
        activeTheme: nil,
        nav: { stack: [], current: nil, knownScenes: [] }
    };
}

// يبني سجل قناع متحرك جديد بحالة ابتدائية كاملة عند (x, y)
fun mm__newRecord(x, y) {
    return {
        x: x, y: y,
        vx: 0, vy: 0,
        ax: 0, ay: 0,
        active: true,
        "container": nil,
        tags: [],
        path: [],
        pathIndex: 0,
        pathLoop: false,
        patrolDir: 1,
        orbitAngle: 0,
        orbitCenterX: 0,
        orbitCenterY: 0,
        orbitRadius: 0,
        history: [],
        meta: {}
    };
}

// يُنشئ قناعاً متحركاً جديداً باسم name عند الموضع (x, y) ويُسجّله في المحرّك mm.
// إن كان هناك قناع بنفس الاسم مسبقاً يُستبدل بالكامل بسجل جديد (بلا تراكم حالة قديمة).
fun mm_spawn(mm, name, x, y) {
    if (has(mm["items"], name) == false) {
        push(mm["order"], name);
    }
    mm["items"][name] = mm__newRecord(x, y);
    return mm["items"][name];
}

// هل يوجد قناع متحرك مسجَّل بهذا الاسم في mm؟
fun mm_exists(mm, name) {
    return has(mm["items"], name);
}

// يُعيد السجل الخام (الخريطة الكاملة) لقناع، أو nil إن لم يوجد — للاستخدام المتقدّم فقط؛
// يُفضَّل استخدام الدوال المتخصصة (mm_position, mm_velocity...) بدل التلاعب المباشر بالسجل
fun mm_get(mm, name) {
    if (mm_exists(mm, name) == false) { return nil; }
    return mm["items"][name];
}

// يحذف قناعاً متحركاً بالكامل: من items، من order، ومن أي حاوية منطقية كان منضمّاً إليها
fun mm_destroy(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    if (rec["container"] != nil) {
        mm_detach(mm, name);
    }
    remove(mm["items"], name);
    mm["order"] = mm__arrayWithout(mm["order"], name);
    return true;
}

// عدد الأقنعة المتحركة المسجَّلة حالياً في mm
fun mm_count(mm) {
    return len(mm["order"]);
}

// مصفوفة بكل أسماء الأقنعة المسجَّلة بترتيب إنشائها (نسخة مستقلة آمنة للتعديل)
fun mm_names(mm) {
    let out = [];
    let i = 0;
    while (i < len(mm["order"])) {
        push(out, mm["order"][i]);
        i = i + 1;
    }
    return out;
}

// يُفعِّل/يُعطِّل قناعاً: الأقنعة غير النشطة (active=false) يتجاهلها mm_tick/mm_integrateAll
fun mm_setActive(mm, name, activeFlag) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["active"] = activeFlag;
    return true;
}

// هل القناع نشط حالياً؟
fun mm_isActive(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    return mm["items"][name]["active"];
}

// يُلصق وسماً (tag) نصّياً بقناع متحرك (لا علاقة له بوسوم نظام mask الأصلي؛ وسم محلي بسيط
// لتصنيف/تصفية الأقنعة المتحركة نفسها — انظر mm_withTag)
fun mm_addTag(mm, name, tag) {
    if (mm_exists(mm, name) == false) { return false; }
    let tags = mm["items"][name]["tags"];
    if (mm__arrayHas(tags, tag) == false) { push(tags, tag); }
    return true;
}

// هل يحمل القناع الوسم tag؟
fun mm_hasTag(mm, name, tag) {
    if (mm_exists(mm, name) == false) { return false; }
    return mm__arrayHas(mm["items"][name]["tags"], tag);
}

// مصفوفة بأسماء كل الأقنعة النشطة التي تحمل الوسم tag
fun mm_withTag(mm, tag) {
    let out = [];
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        if (mm_hasTag(mm, name, tag)) { push(out, name); }
        i = i + 1;
    }
    return out;
}

// يخزّن/يقرأ بيانات مستخدم حرّة على القناع (meta) — مفيد لإرفاق حالة خاصة بلعبتك/تطبيقك
// (مثال: نوع الشخصية، صحتها، مالكها...) دون تعديل بنية سجل movingmask نفسه
fun mm_setMeta(mm, name, key, value) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["meta"][key] = value;
    return true;
}

fun mm_getMeta(mm, name, key, fallback) {
    if (mm_exists(mm, name) == false) { return fallback; }
    let metaMap = mm["items"][name]["meta"];
    if (has(metaMap, key) == false) { return fallback; }
    return metaMap[key];
}


// ============================================================================
// 3) الموضع، السرعة، التسارع (الفيزياء الأساسية)
// ============================================================================

// يُعيد موضع القناع كنقطة {x,y} مستقلة (نسخة، وليست مرجعاً للسجل الداخلي)
fun mm_position(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return nil; }
    return mm_point(rec["x"], rec["y"]);
}

// يضبط موضع القناع مباشرة عند (x, y) — قفزة فورية، بلا تفعيل السرعة
fun mm_setPosition(mm, name, x, y) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["x"] = x;
    mm["items"][name]["y"] = y;
    return true;
}

// يُزيح القناع نسبياً بمقدار (dx, dy) عن موضعه الحالي
fun mm_translate(mm, name, dx, dy) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["x"] = mm["items"][name]["x"] + dx;
    mm["items"][name]["y"] = mm["items"][name]["y"] + dy;
    return true;
}

// يُعيد سرعة القناع الحالية {x,y} (المكوّنان vx/vy مُمثَّلان كنقطة لتماثل mm_position)
fun mm_velocity(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return nil; }
    return mm_point(rec["vx"], rec["vy"]);
}

// يضبط سرعة القناع مباشرة
fun mm_setVelocity(mm, name, vx, vy) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["vx"] = vx;
    mm["items"][name]["vy"] = vy;
    return true;
}

// يضبط تسارع القناع مباشرة (يُطبَّق تلقائياً على السرعة داخل mm_integrate/mm_tick)
fun mm_setAcceleration(mm, name, ax, ay) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["ax"] = ax;
    mm["items"][name]["ay"] = ay;
    return true;
}

// يُعيد تسارع القناع الحالي {x,y}
fun mm_acceleration(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return nil; }
    return mm_point(rec["ax"], rec["ay"]);
}

// يُضيف قوة (fx, fy) إلى تسارع القناع الحالي (كتلة=1 دائماً هنا؛ نموذج فيزيائي مبسّط
// يناسب واجهات وألعاب ثنائية الأبعاد وليس محاكاة فيزيائية دقيقة)
fun mm_applyForce(mm, name, fx, fy) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["ax"] = mm["items"][name]["ax"] + fx;
    mm["items"][name]["ay"] = mm["items"][name]["ay"] + fy;
    return true;
}

// يُصفّر سرعة القناع وتسارعه دفعة واحدة (يبقيه في مكانه الحالي)
fun mm_stop(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["vx"] = 0;
    mm["items"][name]["vy"] = 0;
    mm["items"][name]["ax"] = 0;
    mm["items"][name]["ay"] = 0;
    return true;
}

// خطوة تكامل فيزيائي واحدة لقناع واحد بخطوة زمنية dt (عادة 1 لكل "دورة"/frame منطقية):
//   v += a * dt
//   pos += v * dt
// ثم يُسجَّل الموضع الجديد في الأثر (history) إن كان محرّك التسجيل مفعَّلاً ضمنياً.
// لا تُطبَّق على الأقنعة غير النشطة (active=false).
fun mm_integrate(mm, name, dt) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    if (rec["active"] == false) { return false; }
    rec["vx"] = rec["vx"] + rec["ax"] * dt;
    rec["vy"] = rec["vy"] + rec["ay"] * dt;
    rec["x"] = rec["x"] + rec["vx"] * dt;
    rec["y"] = rec["y"] + rec["vy"] * dt;
    mm_recordHistory(mm, name);
    return true;
}

// يُطبِّق mm_integrate على كل الأقنعة النشطة في mm بخطوة زمنية dt واحدة — هذا هو "قلب"
// التكامل مع الحلقات: استدعِها مرة كل دورة/frame من حلقتك الخاصة، أو استخدم mm_tick/mm_runLoop
// أدناه (قسم 12) لِلف ذلك تلقائياً في حلقة while جاهزة.
fun mm_integrateAll(mm, dt) {
    let i = 0;
    while (i < len(mm["order"])) {
        mm_integrate(mm, mm["order"][i], dt);
        i = i + 1;
    }
    return nil;
}


// ============================================================================
// 4) المسارات ونقاط الطريق (paths / waypoints)
// ============================================================================
//  المسار مصفوفة من نقاط {x,y} يتبعها القناع نقطة فنقطة. mm_followPath تُحرِّك القناع
//  بمقدار "speed" وحدة بحد أقصى نحو نقطة الطريق الحالية، وتنتقل تلقائياً للنقطة التالية
//  عند الوصول إليها (أو عند الاقتراب منها ضمن MM_EPSILON).

// يضبط مسار القناع الكامل (مصفوفة نقاط {x,y}) ويُعيد مؤشر التقدّم إلى البداية.
// loopFlag=true يجعل المسار يعيد الكرّة من أوّله بعد بلوغ آخر نقطة بدل التوقف.
fun mm_setPath(mm, name, points, loopFlag) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["path"] = points;
    mm["items"][name]["pathIndex"] = 0;
    mm["items"][name]["pathLoop"] = loopFlag;
    return true;
}

// يُعيد مؤشر التقدّم الحالي على المسار إلى 0 دون تغيير نقاط المسار نفسها
fun mm_resetPath(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["pathIndex"] = 0;
    return true;
}

// هل انتهى القناع من اتّباع مساره بالكامل؟ (لا معنى لهذا إن كان pathLoop=true، فهو لا ينتهي أبداً)
fun mm_pathDone(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return true; }
    return rec["pathIndex"] >= len(rec["path"]);
}

// نسبة التقدّم على المسار (0..1) بحسب عدد نقاط الطريق المُجتازة، وليس المسافة الفعلية —
// مقياس مبسّط وسريع؛ استخدم mm_distanceTraveled (قسم 10) للمسافة الحقيقية المقطوعة
fun mm_pathProgress(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return 0; }
    let total = len(rec["path"]);
    if (total == 0) { return 1; }
    let progress = rec["pathIndex"] / total;
    return mm_clampNum(progress, 0, 1);
}

// يُحرِّك القناع خطوة واحدة نحو نقطة طريقه الحالية بمسافة أقصاها speed وحدة:
//   - إن كانت المسافة المتبقية <= speed: يصل تماماً إلى نقطة الطريق، ويتقدّم المؤشر للتالية
//     (أو يعود للبداية إن كان pathLoop=true وبلغ آخر نقطة، وإلا يتوقف هناك).
//   - وإلا: يتحرّك بمقدار speed باتجاه نقطة الطريق فقط (بلا تجاوزها).
// لا يفعل شيئاً (ويُعيد false) إن لم يكن للقناع مسار متبقٍّ.
fun mm_followPath(mm, name, speed) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return false; }
    if (mm_pathDone(mm, name)) { return false; }
    let target = rec["path"][rec["pathIndex"]];
    let here = mm_point(rec["x"], rec["y"]);
    let remaining = mm_distance(here, target);
    if (remaining <= speed or mm_nearlyEqual(remaining, speed)) {
        rec["x"] = target["x"];
        rec["y"] = target["y"];
        rec["pathIndex"] = rec["pathIndex"] + 1;
        if (rec["pathIndex"] >= len(rec["path"]) and rec["pathLoop"]) {
            rec["pathIndex"] = 0;
        }
    } else {
        let dx = target["x"] - here["x"];
        let dy = target["y"] - here["y"];
        let mag = sqrt(dx * dx + dy * dy);
        rec["x"] = here["x"] + (dx / mag) * speed;
        rec["y"] = here["y"] + (dy / mag) * speed;
    }
    mm_recordHistory(mm, name);
    return true;
}


// ============================================================================
// 5) أنماط حركة جاهزة (motion patterns)
// ============================================================================

// يُحرِّك القناع مباشرة نحو (targetX, targetY) بمسافة أقصاها speed لهذه الخطوة فقط —
// نمط "seek" الكلاسيكي في التوجيه الذاتي (steering behaviors)
fun mm_seek(mm, name, targetX, targetY, speed) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    let here = mm_point(rec["x"], rec["y"]);
    let target = mm_point(targetX, targetY);
    let remaining = mm_distance(here, target);
    if (remaining < MM_EPSILON) { return true; }
    let step = speed;
    if (step > remaining) { step = remaining; }
    let dx = target["x"] - here["x"];
    let dy = target["y"] - here["y"];
    rec["x"] = here["x"] + (dx / remaining) * step;
    rec["y"] = here["y"] + (dy / remaining) * step;
    mm_recordHistory(mm, name);
    return true;
}

// عكس mm_seek: يُبعِد القناع عن (dangerX, dangerY) بمسافة speed لهذه الخطوة —
// نمط "flee" الكلاسيكي؛ إن كان القناع بالضبط عند نقطة الخطر، يبتعد افتراضياً على محور x
fun mm_flee(mm, name, dangerX, dangerY, speed) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    let here = mm_point(rec["x"], rec["y"]);
    let danger = mm_point(dangerX, dangerY);
    let dx = here["x"] - danger["x"];
    let dy = here["y"] - danger["y"];
    let mag = sqrt(dx * dx + dy * dy);
    if (mag < MM_EPSILON) {
        rec["x"] = rec["x"] + speed;
        mm_recordHistory(mm, name);
        return true;
    }
    rec["x"] = here["x"] + (dx / mag) * speed;
    rec["y"] = here["y"] + (dy / mag) * speed;
    mm_recordHistory(mm, name);
    return true;
}

// حركة "دورية" بين نقطتين A و B: يتحرّك القناع نحو الطرف الحالي بمقدار speed، وعند بلوغه
// يعكس اتجاهه (patrolDir) ليعود نحو الطرف الآخر — نمط حراسة/دوريات كلاسيكي في الألعاب
fun mm_patrol(mm, name, ax, ay, bx, by, speed) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    let target = mm_point(bx, by);
    if (rec["patrolDir"] < 0) { target = mm_point(ax, ay); }
    let here = mm_point(rec["x"], rec["y"]);
    let remaining = mm_distance(here, target);
    if (remaining <= speed or mm_nearlyEqual(remaining, speed)) {
        rec["x"] = target["x"];
        rec["y"] = target["y"];
        rec["patrolDir"] = 0 - rec["patrolDir"];
    } else {
        let dx = target["x"] - here["x"];
        let dy = target["y"] - here["y"];
        rec["x"] = here["x"] + (dx / remaining) * speed;
        rec["y"] = here["y"] + (dy / remaining) * speed;
    }
    mm_recordHistory(mm, name);
    return true;
}

// يُهيّئ حركة مدارية (orbit) حول مركز (cx, cy) بنصف قطر radius، بدءاً من الزاوية startAngle
// (بالراديان). استدعِها مرة واحدة قبل mm_orbitStep لأول مرة على قناع معيّن.
fun mm_setOrbit(mm, name, cx, cy, radius, startAngle) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    rec["orbitCenterX"] = cx;
    rec["orbitCenterY"] = cy;
    rec["orbitRadius"] = radius;
    rec["orbitAngle"] = startAngle;
    rec["x"] = cx + radius * mm__cos(startAngle);
    rec["y"] = cy + radius * mm__sin(startAngle);
    return true;
}

// يُقدِّم القناع خطوة واحدة على مداره الحالي بزيادة زاوية angularStep (بالراديان؛ موجبة
// = عكس اتجاه عقارب الساعة، سالبة = معه) — يتطلّب استدعاء mm_setOrbit أولاً على هذا القناع
fun mm_orbitStep(mm, name, angularStep) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    rec["orbitAngle"] = rec["orbitAngle"] + angularStep;
    rec["x"] = rec["orbitCenterX"] + rec["orbitRadius"] * mm__cos(rec["orbitAngle"]);
    rec["y"] = rec["orbitCenterY"] + rec["orbitRadius"] * mm__sin(rec["orbitAngle"]);
    mm_recordHistory(mm, name);
    return true;
}

// خطوة "تجوال" عشوائي (wander): تُزيح القناع بمقدار عشوائي بين -jitter و +jitter على كل
// من x وy — بسيط وسريع، مناسب لحركة خلفية غير موجَّهة (جسيمات، كائنات ثانوية...)
fun mm_wander(mm, name, jitter) {
    if (mm_exists(mm, name) == false) { return false; }
    let dx = (random() * 2 - 1) * jitter;
    let dy = (random() * 2 - 1) * jitter;
    mm_translate(mm, name, dx, dy);
    mm_recordHistory(mm, name);
    return true;
}


// ============================================================================
// 6) الحدود والمناطق (bounds / regions)
// ============================================================================
//  bounds: حدود "عالم" واحدة عامة للمحرّك كله (مثلاً أبعاد @loop=canvas: width/height).
//  regions: عدد حرّ من المناطق المُسمّاة الإضافية (منطقة هدف، منطقة خطر...)، كل واحدة
//  بحدودها الخاصة، للاستعلام عبر mm_inRegion/mm_regionsContaining.

// يضبط حدود عالم المحرّك بالكامل (مستطيل [minX..maxX] × [minY..maxY])
fun mm_setBounds(mm, minX, minY, maxX, maxY) {
    mm["bounds"] = { minX: minX, minY: minY, maxX: maxX, maxY: maxY };
    return true;
}

// يُزيل حدود العالم (mm_clampToBounds/mm_bounceAtBounds/mm_inBounds لن تفعل شيئاً بعدها)
fun mm_clearBounds(mm) {
    mm["bounds"] = nil;
    return true;
}

// هل موضع القناع داخل حدود العالم؟ true دائماً إن لم تُضبط حدود أصلاً
fun mm_inBounds(mm, name) {
    if (mm["bounds"] == nil) { return true; }
    let rec = mm_get(mm, name);
    if (rec == nil) { return false; }
    let b = mm["bounds"];
    if (rec["x"] < b["minX"] or rec["x"] > b["maxX"]) { return false; }
    if (rec["y"] < b["minY"] or rec["y"] > b["maxY"]) { return false; }
    return true;
}

// يحصر موضع القناع داخل حدود العالم (بلا تغيير سرعته) — لا يفعل شيئاً إن لم تُضبط حدود
fun mm_clampToBounds(mm, name) {
    if (mm["bounds"] == nil) { return false; }
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    let b = mm["bounds"];
    rec["x"] = mm_clampNum(rec["x"], b["minX"], b["maxX"]);
    rec["y"] = mm_clampNum(rec["y"], b["minY"], b["maxY"]);
    return true;
}

// يحصر موضع القناع داخل الحدود، ويعكس مكوّن السرعة (vx أو vy) المسؤول عن الخروج عند
// الاصطدام بحافة — سلوك "ارتداد" (bounce) كلاسيكي (كرة تصطدم بجدران الشاشة مثلاً).
// يُطلق حدث "bounce" (انظر قسم 13) عند حدوث أي ارتداد فعلي.
fun mm_bounceAtBounds(mm, name) {
    if (mm["bounds"] == nil) { return false; }
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    let b = mm["bounds"];
    let bounced = false;
    if (rec["x"] < b["minX"]) { rec["x"] = b["minX"]; rec["vx"] = 0 - rec["vx"]; bounced = true; }
    if (rec["x"] > b["maxX"]) { rec["x"] = b["maxX"]; rec["vx"] = 0 - rec["vx"]; bounced = true; }
    if (rec["y"] < b["minY"]) { rec["y"] = b["minY"]; rec["vy"] = 0 - rec["vy"]; bounced = true; }
    if (rec["y"] > b["maxY"]) { rec["y"] = b["maxY"]; rec["vy"] = 0 - rec["vy"]; bounced = true; }
    if (bounced) { mm__fire(mm, "bounce", { name: name, x: rec["x"], y: rec["y"] }); }
    return bounced;
}

// يُسجِّل/يُحدِّث منطقة مُسمّاة بحدودها الخاصة (مستقلة عن bounds العالم العام)
fun mm_setRegion(mm, regionName, minX, minY, maxX, maxY) {
    mm["regions"][regionName] = { minX: minX, minY: minY, maxX: maxX, maxY: maxY };
    return true;
}

// يحذف منطقة مُسمّاة
fun mm_removeRegion(mm, regionName) {
    if (has(mm["regions"], regionName) == false) { return false; }
    remove(mm["regions"], regionName);
    return true;
}

// هل موضع القناع داخل منطقة مُسمّاة معيّنة؟ false إن لم توجد منطقة بهذا الاسم أصلاً
fun mm_inRegion(mm, name, regionName) {
    if (has(mm["regions"], regionName) == false) { return false; }
    let rec = mm_get(mm, name);
    if (rec == nil) { return false; }
    let r = mm["regions"][regionName];
    if (rec["x"] < r["minX"] or rec["x"] > r["maxX"]) { return false; }
    if (rec["y"] < r["minY"] or rec["y"] > r["maxY"]) { return false; }
    return true;
}

// مصفوفة بأسماء كل المناطق المُسمّاة التي يقع موضع القناع داخلها حالياً (قد تكون أكثر من
// واحدة إن تداخلت المناطق)
fun mm_regionsContaining(mm, name) {
    let out = [];
    let names = keys(mm["regions"]);
    let i = 0;
    while (i < len(names)) {
        if (mm_inRegion(mm, name, names[i])) { push(out, names[i]); }
        i = i + 1;
    }
    return out;
}


// ============================================================================
// 7) التكامل مع الحاويات (containers) — منطقي وفعلي (native bridge)
// ============================================================================
//  طبقتان هنا:
//
//  أ) عضوية منطقية (logical membership) — تُدار بالكامل داخل mm["containers"]، تعمل دوماً
//     بلا أي شرط، ولا تحتاج وجود @container حقيقية في المصدر. مفيدة لتجميع أقنعة متحركة
//     في "دِلاء" منطقية عشوائية (فريق أ/فريق ب، شاشة1/شاشة2...).
//
//  ب) جسر مع الحاويات الفعلية في Rin (native bridge) — تستخدم الدوال الأصلية
//     hasContainer/containerNames/setField/getField/childrenOf/parentOf/siblingsOf/spawn
//     لمزامنة موضع القناع المتحرك كحقل فعلي (field) على حاوية @container حقيقية بنفس
//     الاسم، حين تكون هذه الحاوية موجودة فعلاً في البرنامج. هذا يسمح لبقية كودك (الذي لا
//     يعرف شيئاً عن movingmask) بقراءة موضع/سرعة القناع عبر getField(name, "mm_x") العادية.

// يُلحق قناعاً منطقياً بحاوية منطقية containerName (ينشئ الدلو إن لم يكن موجوداً). يُزيله
// أولاً من أي حاوية منطقية سابقة كان بها (عضوية واحدة فقط في كل مرة). يُطلق حدث
// "enterContainer" بعد الإلحاق (وحدث "leaveContainer" عن الحاوية القديمة إن وُجدت).
fun mm_attach(mm, name, containerName) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    let previous = rec["container"];
    if (previous != nil) { mm_detach(mm, name); }
    if (has(mm["containers"], containerName) == false) {
        mm["containers"][containerName] = [];
    }
    push(mm["containers"][containerName], name);
    rec["container"] = containerName;
    mm__fire(mm, "enterContainer", { name: name, "container": containerName });
    return true;
}

// يُخرج قناعاً من حاويته المنطقية الحالية (إن كان منضمّاً لأي واحدة). يُطلق حدث "leaveContainer"
fun mm_detach(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    let containerName = rec["container"];
    if (containerName == nil) { return false; }
    if (has(mm["containers"], containerName)) {
        mm["containers"][containerName] = mm__arrayWithout(mm["containers"][containerName], name);
    }
    rec["container"] = nil;
    mm__fire(mm, "leaveContainer", { name: name, "container": containerName });
    return true;
}

// اسم الحاوية المنطقية الحالية للقناع، أو nil إن لم يكن منضمّاً لأي واحدة
fun mm_containerOf(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return nil; }
    return rec["container"];
}

// مصفوفة بأسماء كل الأقنعة المنضمّة حالياً لحاوية منطقية معيّنة (مصفوفة فارغة إن لم توجد)
fun mm_membersOf(mm, containerName) {
    if (has(mm["containers"], containerName) == false) { return []; }
    let out = [];
    let src = mm["containers"][containerName];
    let i = 0;
    while (i < len(src)) { push(out, src[i]); i = i + 1; }
    return out;
}

// عدد الأقنعة داخل حاوية منطقية معيّنة
fun mm_containerSize(mm, containerName) {
    return len(mm_membersOf(mm, containerName));
}

// ينقل قناعاً من حاويته المنطقية الحالية إلى toContainer مباشرة (اختصار لِـ
// mm_detach ثم mm_attach معاً في استدعاء واحد؛ يعمل حتى لو لم يكن منضمّاً لأي حاوية أصلاً)
fun mm_transfer(mm, name, toContainer) {
    if (mm_exists(mm, name) == false) { return false; }
    return mm_attach(mm, name, toContainer);
}

// مصفوفة بأسماء كل الحاويات المنطقية المُنشأة حتى الآن في mm (بها أقنعة أو كانت بها سابقاً)
fun mm_containerNames(mm) {
    return keys(mm["containers"]);
}

// ---- جسر الحاويات الفعلية (native bridge) ---------------------------------------------

// هل توجد حاوية Rin فعلية (@container أو spawn) بهذا الاسم في البرنامج الحالي؟
// غلاف رقيق فوق hasContainer() الأصلية، بالاسم موحَّد مع بقية هذه المكتبة
fun mm_nativeContainerExists(containerName) {
    return hasContainer(containerName);
}

// يكتب موضع/سرعة القناع المتحرك كحقول فعلية (mm_x, mm_y, mm_vx, mm_vy) على حاوية Rin
// فعلية بنفس الاسم containerName (عبر setField الأصلية) — فقط إن كانت هذه الحاوية موجودة
// فعلاً (وإلا تُعاد false بلا أي تأثير). هذا يجعل موضع القناع المتحرك قابلاً للقراءة من
// أي كود Rin آخر لا يستورد movingmask إطلاقاً، طالما يستخدم getField(containerName, "mm_x").
fun mm_syncToNativeContainer(mm, name, containerName) {
    if (mm_exists(mm, name) == false) { return false; }
    if (hasContainer(containerName) == false) { return false; }
    let rec = mm["items"][name];
    setField(containerName, "mm_x", rec["x"]);
    setField(containerName, "mm_y", rec["y"]);
    setField(containerName, "mm_vx", rec["vx"]);
    setField(containerName, "mm_vy", rec["vy"]);
    setField(containerName, "mm_mask", name);
    return true;
}

// عكس مزامنة mm_syncToNativeContainer: يقرأ mm_x/mm_y (إن وُجدا كحقلين على الحاوية
// الفعلية) ويضبط بهما موضع القناع المتحرك — مفيد حين تُعدِّل موضع الحاوية من مكان آخر
// (مثلاً حدث UI) وتريد أن يلحق movingmask بهذا التعديل عند الدورة التالية
fun mm_syncFromNativeContainer(mm, name, containerName) {
    if (mm_exists(mm, name) == false) { return false; }
    if (hasContainer(containerName) == false) { return false; }
    if (hasField(containerName, "mm_x") == false) { return false; }
    if (hasField(containerName, "mm_y") == false) { return false; }
    let x = getField(containerName, "mm_x");
    let y = getField(containerName, "mm_y");
    mm_setPosition(mm, name, x, y);
    return true;
}

// ينقل قناعاً "فعلياً" بين حاويتين حقيقيتين في نفس الاستدعاء: يزيل حقول mm_* عن
// fromContainer (إن كانت موجودة) ويكتبها على toContainer، بالإضافة لتحديث العضوية
// المنطقية عبر mm_transfer — بهذا يبقى المصدران (المنطقي والفعلي) متوافقين معاً دوماً.
// يُعيد false إن لم تكن toContainer موجودة فعلياً (fromContainer اختيارية: مرّر nil لتجاهلها)
fun mm_transferNativeContainer(mm, name, fromContainer, toContainer) {
    if (mm_exists(mm, name) == false) { return false; }
    if (hasContainer(toContainer) == false) { return false; }
    if (fromContainer != nil and hasContainer(fromContainer)) {
        if (hasField(fromContainer, "mm_mask")) {
            setField(fromContainer, "mm_mask", nil);
        }
    }
    mm_syncToNativeContainer(mm, name, toContainer);
    mm_transfer(mm, name, toContainer);
    return true;
}

// يبني مصفوفة بأسماء أبناء حاوية فعلية containerName (عبر childrenOf الأصلية) الذين لهم
// أيضاً قناع متحرك مسجَّل بنفس الاسم في mm — تقاطع مباشر بين شجرة الحاويات الفعلية وعالم
// movingmask، مفيد لمعرفة "أيّ أبناء هذه الحاوية يتحرّكون فعلياً؟"
fun mm_movingChildrenOf(mm, containerName) {
    let out = [];
    let kids = childrenOf(containerName);
    let i = 0;
    while (i < len(kids)) {
        if (mm_exists(mm, kids[i])) { push(out, kids[i]); }
        i = i + 1;
    }
    return out;
}

// ---- جسر هوية القناع الأصلي (mask registry bridge) -------------------------------------
// (انظر lib/maskkit.og.rin و docs/mask.md — نظام mask الأصلي هوية منطقية ساكنة منفصلة تماماً
// عن هذه المكتبة. الدوال التالية تربط الاثنين معاً اختيارياً، دون أن يفرض أحدهما الآخر)

// يربط قناعاً متحركاً باسمه name بهوية قناع Rin أصلية موجودة مسبقاً (عبر findMask/maskExists)،
// ويُعيد معلومات تلك الهوية (kind/target) إن وُجدت، أو nil إن لم يكن القناع الأصلي مسجَّلاً
fun mm_identityOf(name) {
    if (maskExists(name) == false) { return nil; }
    return { mask: name, kind: maskKind(name), target: maskTarget(name) };
}

// يُلحق قناعاً متحركاً بالحاوية المنطقية نفسها التي يستهدفها قناع Rin الأصلي identityMask
// (عبر maskTarget) — أي: "انضم منطقياً إلى نفس الحاوية التي يشير إليها هذا القناع الأصلي"
fun mm_attachByIdentity(mm, name, identityMask) {
    if (mm_exists(mm, name) == false) { return false; }
    if (maskExists(identityMask) == false) { return false; }
    let target = maskTarget(identityMask);
    if (target == nil) { return false; }
    return mm_attach(mm, name, target);
}


// ============================================================================
// 8) القناع المنزلق فوق المصفوفات (sliding window / 1D convolution)
// ============================================================================
//  هذا هو المعنى الكلاسيكي الآخر لِـ"moving mask": نافذة صغيرة الحجم تنزلق خانة خانة
//  فوق مصفوفة أكبر، وتُطبَّق عملية عندها في كل موضع (مجموع، متوسط، أقصى/أدنى، أو حاصل
//  ضرب مرجَّح مع "قناع/نواة" kernel — أي convolution أحادية البُعد).
//
//  ملاحظة: المفسّر يوفّر movingAverage(arr, window) أصلياً (متوسط منزلق فقط)؛ الدوال هنا
//  تُكمِّلها بعمليات أخرى (مجموع/أقصى/أدنى/عام/التفاف) لا توفّرها stdlib.

// يُعيد نافذة القناع الآمنة حول centerIndex بنصف قطر radius من مصفوفة arr، أي العناصر
// من (centerIndex-radius) إلى (centerIndex+radius) شاملة — مع قص الطرفين الخارجين عن
// حدود arr تلقائياً بدل توقّف بخطأ (نافذة أصغر قرب الأطراف بدل fillValue وهمية)
fun mm_windowAt(arr, centerIndex, radius) {
    let lo = centerIndex - radius;
    if (lo < 0) { lo = 0; }
    let hi = centerIndex + radius;
    if (hi > len(arr) - 1) { hi = len(arr) - 1; }
    let out = [];
    let i = lo;
    while (i <= hi) {
        push(out, arr[i]);
        i = i + 1;
    }
    return out;
}

// يُنزلق بنافذة ثابتة الحجم windowSize فوق كامل arr من اليسار لليمين، خطوة واحدة كل مرة،
// مستدعياً fn(windowSlice, startIndex) عند كل موضع، ويجمع نواتج fn في مصفوفة يُعيدها —
// هذا هو "محرّك" القناع المنزلق العام: أي عملية نافذة (مجموع/أقصى/بحث نمط...) تُبنى فوقه
fun mm_slideOverArray(arr, windowSize, fn) {
    let result = [];
    if (windowSize < 1 or windowSize > len(arr)) { return result; }
    let start = 0;
    while (start + windowSize <= len(arr)) {
        let slice = [];
        let i = start;
        while (i < start + windowSize) {
            push(slice, arr[i]);
            i = i + 1;
        }
        push(result, fn(slice, start));
        start = start + 1;
    }
    return result;
}

fun mm__sumArray(arr) {
    let total = 0;
    let i = 0;
    while (i < len(arr)) { total = total + arr[i]; i = i + 1; }
    return total;
}

fun mm__maxArray(arr) {
    let best = arr[0];
    let i = 1;
    while (i < len(arr)) { if (arr[i] > best) { best = arr[i]; } i = i + 1; }
    return best;
}

fun mm__minArray(arr) {
    let best = arr[0];
    let i = 1;
    while (i < len(arr)) { if (arr[i] < best) { best = arr[i]; } i = i + 1; }
    return best;
}

// مجموع كل نافذة بحجم windowSize منزلقة فوق arr (تكميلي لِـ movingAverage الأصلية)
fun mm_movingSum(arr, windowSize) {
    fun sumOnly(slice, startIndex) { return mm__sumArray(slice); }
    return mm_slideOverArray(arr, windowSize, sumOnly);
}

// أقصى قيمة في كل نافذة منزلقة (مفيد لكشف قمم محلية أو "أسوأ حالة" على مدى نافذة زمنية)
fun mm_movingMax(arr, windowSize) {
    fun maxOnly(slice, startIndex) { return mm__maxArray(slice); }
    return mm_slideOverArray(arr, windowSize, maxOnly);
}

// أدنى قيمة في كل نافذة منزلقة
fun mm_movingMin(arr, windowSize) {
    fun minOnly(slice, startIndex) { return mm__minArray(slice); }
    return mm_slideOverArray(arr, windowSize, minOnly);
}

// التفاف أحادي البُعد (1D convolution): يُنزلق "قناع/نواة" kernel (مصفوفة أوزان صغيرة)
// فوق arr، وعند كل موضع يحسب مجموع حاصل ضرب النافذة المطابقة في أوزان kernel عنصراً
// بعنصر. طول kernel يجب أن يقسم يساوي حجم النافذة المستخدمة. هذا التعميم الرياضي الدقيق
// لعبارة "قناع متحرك": نفس kernel يمرّ فوق كل موضع من الحاوية، بوزن ثابت لكل خانة نسبية.
fun mm_convolve1D(arr, kernel) {
    let k = len(kernel);
    fun applyKernel(slice, startIndex) {
        let total = 0;
        let i = 0;
        while (i < k) {
            total = total + slice[i] * kernel[i];
            i = i + 1;
        }
        return total;
    }
    return mm_slideOverArray(arr, k, applyKernel);
}


// ============================================================================
// 9) القناع المنزلق فوق الشبكات (2D grid stencil / convolution / نقل القناع بصرياً)
// ============================================================================
//  الشبكة هنا بنفس اتفاقية lib/gridkit.og.rin: مصفوفة صفوف grid[row][col]. القسم السابق
//  عمّم بُعداً واحداً؛ هذا القسم يعمّمه إلى بُعدين — الاستخدام الأكلاسيكي لِـ"moving mask"
//  في معالجة الصور (بلور/كشف حواف)، الأتمتة الخلوية (game of life ونحوه)، ولوحات الألعاب
//  (تحريك شكل/قطعة كاملة الشكل across the board، مثل قطعة Tetris أو منطقة تأثير سحر).

// أبعاد شبكة مساعِدة (بنفس منطق gridRows/gridCols من gridkit، مُعادة هنا كي يبقى هذا
// الملف مستقلاً بذاته)
fun mm_gridRows(g) { return len(g); }
fun mm_gridCols(g) {
    if (len(g) == 0) { return 0; }
    return len(g[0]);
}
fun mm_gridInBounds(g, row, col) {
    if (row < 0 or row >= mm_gridRows(g)) { return false; }
    if (col < 0 or col >= mm_gridCols(g)) { return false; }
    return true;
}

// يُعيد نافذة القناع المستطيلة حول (row, col) بنصف قطر radius (أي مربّع طوله
// (2*radius+1) تقريباً)، مع استبدال الخانات الواقعة خارج حدود الشبكة بـ fillValue بدل
// حذفها — على عكس mm_windowAt الأحادية البُعد، النافذة هنا دائماً بنفس الحجم الثابت
// (2*radius+1)×(2*radius+1) مهما كانت (row, col) قريبة من حافة الشبكة، وهذا مهم لأي
// عملية تعتمد على حجم نافذة موحّد (مثل mm_convolve2D).
fun mm_gridWindowAt(g, row, col, radius, fillValue) {
    let out = [];
    let r = row - radius;
    while (r <= row + radius) {
        let rowOut = [];
        let c = col - radius;
        while (c <= col + radius) {
            if (mm_gridInBounds(g, r, c)) {
                push(rowOut, g[r][c]);
            } else {
                push(rowOut, fillValue);
            }
            c = c + 1;
        }
        push(out, rowOut);
        r = r + 1;
    }
    return out;
}

// يُنزلق بنافذة ثابتة الحجم windowRows×windowCols فوق كامل الشبكة g (صفاً فصفاً، عموداً
// فعموداً من اليسار لليمين داخل كل صف)، مستدعياً fn(windowGrid, row, col) عند كل موضع
// يُشكِّل فيه أعلى-يسار النافذة الخانة (row, col) — بلا توسيع افتراضي؛ فقط المواضع التي
// تسع فيها النافذة كاملة داخل الشبكة (استخدم mm_gridWindowAt يدوياً إن أردت حشو الأطراف)
fun mm_forEachGridWindow(g, windowRows, windowCols, fn) {
    let rows = mm_gridRows(g);
    let cols = mm_gridCols(g);
    let r = 0;
    while (r + windowRows <= rows) {
        let c = 0;
        while (c + windowCols <= cols) {
            let win = [];
            let wr = 0;
            while (wr < windowRows) {
                let winRow = [];
                let wc = 0;
                while (wc < windowCols) {
                    push(winRow, g[r + wr][c + wc]);
                    wc = wc + 1;
                }
                push(win, winRow);
                wr = wr + 1;
            }
            fn(win, r, c);
            c = c + 1;
        }
        r = r + 1;
    }
    return nil;
}

// التفاف ثنائي البُعد (2D convolution / stencil): يُنزلق "قناع/نواة" kernel (شبكة أوزان
// صغيرة مربّعة) فوق كامل الشبكة g، وعند كل موضع (خارج الحافة بمقدار نصف حجم kernel) يحسب
// مجموع حاصل ضرب النافذة المطابقة في أوزان kernel خانة بخانة. يُعيد شبكة نتائج جديدة
// بنفس أبعاد g (الخانات القريبة جداً من الحافة التي لا تسع النواة كاملة تُملأ بـ edgeValue).
// هذا هو التطبيق الأدق لِمصطلح "moving mask" في معالجة الصور: مثال جاهز لبلور بسيط عبر
// kernel = [[1/9,1/9,1/9],[1/9,1/9,1/9],[1/9,1/9,1/9]].
fun mm_convolve2D(g, kernel, edgeValue) {
    let kRows = len(kernel);
    let kCols = len(kernel[0]);
    let radius = floor(kRows / 2);
    let rows = mm_gridRows(g);
    let cols = mm_gridCols(g);
    let out = [];
    let r = 0;
    while (r < rows) {
        let outRow = [];
        let c = 0;
        while (c < cols) {
            if (r - radius < 0 or r + radius > rows - 1 or c - radius < 0 or c + radius > cols - 1) {
                push(outRow, edgeValue);
            } else {
                let total = 0;
                let kr = 0;
                while (kr < kRows) {
                    let kc = 0;
                    while (kc < kCols) {
                        total = total + g[r - radius + kr][c - radius + kc] * kernel[kr][kc];
                        kc = kc + 1;
                    }
                    kr = kr + 1;
                }
                push(outRow, total);
            }
            c = c + 1;
        }
        push(out, outRow);
        r = r + 1;
    }
    return out;
}

// "يختم" (stamps) قناعاً/نموذجاً stencilGrid (شبكة قيم صغيرة، مثل بصمة قطعة أو منطقة
// تأثير) فوق شبكة g عند الزاوية العلوية اليسرى (row, col): لكل خانة من stencilGrid لا
// تساوي skipValue، يكتب قيمتها في الخانة المقابلة من g (تجاهل الخانات خارج حدود g بأمان
// بلا خطأ). يُعدِّل g مباشرة بالمرجع ويُعيده أيضاً لتسلسل الاستدعاءات (chaining).
fun mm_gridStamp(g, stencilGrid, row, col, skipValue) {
    let sr = 0;
    while (sr < len(stencilGrid)) {
        let sc = 0;
        while (sc < len(stencilGrid[sr])) {
            let value = stencilGrid[sr][sc];
            if (value != skipValue) {
                let targetRow = row + sr;
                let targetCol = col + sc;
                if (mm_gridInBounds(g, targetRow, targetCol)) {
                    g[targetRow][targetCol] = value;
                }
            }
            sc = sc + 1;
        }
        sr = sr + 1;
    }
    return g;
}

// "يمحو" بصمة stencilGrid من شبكة g عند (row, col): يُعيد كل خانة كانت ستُختم بها (أي لا
// تساوي skipValue) إلى clearValue — يُستخدم عادة قبل mm_gridStamp عند موضع جديد، معاً
// يُشكِّلان أساس "تحريك قناع على شبكة" الحقيقي (امسح القديم، اختم الجديد)
fun mm_gridClearStamp(g, stencilGrid, row, col, skipValue, clearValue) {
    let sr = 0;
    while (sr < len(stencilGrid)) {
        let sc = 0;
        while (sc < len(stencilGrid[sr])) {
            if (stencilGrid[sr][sc] != skipValue) {
                let targetRow = row + sr;
                let targetCol = col + sc;
                if (mm_gridInBounds(g, targetRow, targetCol)) {
                    g[targetRow][targetCol] = clearValue;
                }
            }
            sc = sc + 1;
        }
        sr = sr + 1;
    }
    return g;
}

// "تحريك القناع" الحرفي فوق حاوية-شبكة: يمحو بصمة stencilGrid من موضعها القديم
// (fromRow, fromCol) بقيمة clearValue، ثم يختمها في موضعها الجديد (toRow, toCol) — استدعاء
// واحد يكفي لكل "خطوة" حركة مرئية لقطعة/كائن كامل الشكل فوق لوحة (لعبة، شاشة نصية، أتمتة
// خلوية بمنطقة تأثير...)، بدل تحريك خانة مفردة كما تفعل setCell التقليدية.
fun mm_gridMoveStamp(g, stencilGrid, fromRow, fromCol, toRow, toCol, skipValue, clearValue) {
    mm_gridClearStamp(g, stencilGrid, fromRow, fromCol, skipValue, clearValue);
    mm_gridStamp(g, stencilGrid, toRow, toCol, skipValue);
    return g;
}


// ============================================================================
// 10) الأثر والتاريخ (trail / history)
// ============================================================================
//  كل حركة فعلية (mm_integrate, mm_followPath, mm_seek, mm_flee, mm_patrol, mm_orbitStep,
//  mm_wander) تستدعي mm_recordHistory تلقائياً، فتتراكم نقاط أثر (trail) محدودة الطول
//  (mm["historyLimit"]) لكل قناع — مفيدة للرسم (خط أثر خلف كائن متحرك)، أو لحساب المسافة
//  الفعلية المقطوعة، أو للتراجع عن آخر خطوة (undo مبسّط).

// يضبط أقصى عدد نقاط أثر تُحفظ لكل قناع في هذا المحرّك (القيمة الافتراضية
// MM_DEFAULT_HISTORY_LIMIT). القيم الأقدم من الحد تُحذف تلقائياً عند كل تسجيل جديد.
fun mm_setHistoryLimit(mm, limit) {
    mm["historyLimit"] = limit;
    return true;
}

// يُسجِّل الموضع الحالي للقناع في أثره — تُستدعى تلقائياً من دوال الحركة أعلاه، لكن يمكن
// استدعاؤها يدوياً أيضاً بعد أي تعديل موضع مباشر (مثل mm_setPosition) إن أردت تتبّعه
fun mm_recordHistory(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    push(rec["history"], mm_point(rec["x"], rec["y"]));
    while (len(rec["history"]) > mm["historyLimit"]) {
        shift(rec["history"]);
    }
    return true;
}

// مصفوفة نقاط أثر القناع الكاملة (من الأقدم إلى الأحدث)، أو مصفوفة فارغة إن لم يتحرّك بعد
fun mm_trail(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return []; }
    return rec["history"];
}

// يمسح أثر القناع بالكامل بلا تغيير موضعه الحالي
fun mm_clearTrail(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    mm["items"][name]["history"] = [];
    return true;
}

// مجموع المسافات بين كل نقطتين متتاليتين في أثر القناع — تقريب جيد "للمسافة الفعلية
// المقطوعة" (على عكس mm_pathProgress التي تقيس نسبة نقاط الطريق المُجتازة فقط)
fun mm_distanceTraveled(mm, name) {
    let trail = mm_trail(mm, name);
    if (len(trail) < 2) { return 0; }
    let total = 0;
    let i = 1;
    while (i < len(trail)) {
        total = total + mm_distance(trail[i - 1], trail[i]);
        i = i + 1;
    }
    return total;
}

// يُعيد القناع إلى الموضع الذي كان عليه قبل آخر حركة مسجَّلة (يتراجع بخطوة واحدة فقط)،
// ويحذف من الأثر النقطة المكرِّرة للموضع الحالي بعد ذلك. يتطلّب نقطتي أثر على الأقل
// (حركتين مسجَّلتين) ليعرف "الموضع السابق"؛ وإلا يُعيد false بلا أي تغيير في الموضع
// (تراجع واحد لا يكفي معلومات لمعرفة موضع القناع عند الإنشاء، قبل أول حركة له إطلاقاً).
fun mm_undoLastMove(mm, name) {
    if (mm_exists(mm, name) == false) { return false; }
    let rec = mm["items"][name];
    if (len(rec["history"]) < 2) { return false; }
    pop(rec["history"]);
    let previous = rec["history"][len(rec["history"]) - 1];
    rec["x"] = previous["x"];
    rec["y"] = previous["y"];
    return true;
}


// ============================================================================
// 11) القرب والتصادم (distance / proximity / AABB collision)
// ============================================================================

// المسافة الإقليدية بين موضعي قناعين متحركين
fun mm_distanceTo(mm, name1, name2) {
    let p1 = mm_position(mm, name1);
    let p2 = mm_position(mm, name2);
    if (p1 == nil or p2 == nil) { return nil; }
    return mm_distance(p1, p2);
}

// هل المسافة بين قناعين أقل من أو تساوي threshold؟
fun mm_isNear(mm, name1, name2, threshold) {
    let d = mm_distanceTo(mm, name1, name2);
    if (d == nil) { return false; }
    return d <= threshold;
}

// تصادم صندوقي محاذٍ للمحاور (AABB): يفترض أن موضع كل قناع هو مركز مستطيله، بعرض w
// وارتفاع h. يُعيد true إن تداخل مستطيلا القناعين (تصادم كلاسيكي وسريع في الألعاب
// ثنائية الأبعاد، أدق من مجرّد فحص المسافة بين مركزين).
fun mm_collidesAABB(mm, name1, w1, h1, name2, w2, h2) {
    let p1 = mm_position(mm, name1);
    let p2 = mm_position(mm, name2);
    if (p1 == nil or p2 == nil) { return false; }
    let dx = p1["x"] - p2["x"];
    if (dx < 0) { dx = 0 - dx; }
    let dy = p1["y"] - p2["y"];
    if (dy < 0) { dy = 0 - dy; }
    let overlapX = dx < (w1 + w2) / 2;
    let overlapY = dy < (h1 + h2) / 2;
    return overlapX and overlapY;
}

// يبحث عن أقرب قناع آخر (نشط) إلى name ضمن كل الأقنعة المسجَّلة في mm، ويُعيد
// { name: أقرب اسم أو nil, distance: المسافة أو nil } — nil في الحقلين إن لم يوجد أي قناع
// آخر على الإطلاق
fun mm_nearestTo(mm, name) {
    let here = mm_position(mm, name);
    if (here == nil) { return { name: nil, distance: nil }; }
    let bestName = nil;
    let bestDistance = nil;
    let i = 0;
    while (i < len(mm["order"])) {
        let other = mm["order"][i];
        if (other != name and mm_isActive(mm, other)) {
            let d = mm_distance(here, mm_position(mm, other));
            if (bestDistance == nil or d < bestDistance) {
                bestDistance = d;
                bestName = other;
            }
        }
        i = i + 1;
    }
    return { name: bestName, distance: bestDistance };
}


// ============================================================================
// 12) التكامل مع الحلقات (loop integration)
// ============================================================================
//  هذا القسم هو نقطة الدمج المباشرة مع أي حلقة while/for في برنامجك، أو مع دوال
//  lib/loopkit.og.rin (repeatTimes/stepLoop تقبل دالة fn(i) — مرّر لها دالة صغيرة تستدعي
//  mm_tick(world, 1) بداخلها لتشغيل movingmask من داخل تلك المكتبة مباشرة).

// "دورة" واحدة كاملة لكل شيء في العالم: تُكامل فيزياء كل الأقنعة النشطة بخطوة dt، ثم
// تُطبِّق الحدود (حصر أو ارتداد بحسب bounceMode)، ثم تزيد عدّاد الدورات، ثم تُطلق حدث
// "tick". استدعِها مرة واحدة لكل frame/دورة من حلقة تشغيل تطبيقك (game loop).
// bounceMode: true = ارتداد عند الحدود (mm_bounceAtBounds)، false = حصر فقط (mm_clampToBounds)
fun mm_tick(mm, dt, bounceMode) {
    mm_integrateAll(mm, dt);
    if (mm["bounds"] != nil) {
        let i = 0;
        while (i < len(mm["order"])) {
            let name = mm["order"][i];
            if (bounceMode) { mm_bounceAtBounds(mm, name); } else { mm_clampToBounds(mm, name); }
            i = i + 1;
        }
    }
    mm["tick"] = mm["tick"] + 1;
    mm__fire(mm, "tick", { tick: mm["tick"] });
    return mm["tick"];
}

// يُشغِّل steps دورة كاملة عبر mm_tick تباعاً (حلقة while جاهزة، بنفس روح
// lib/loopkit.og.rin/repeatTimes)، مستدعياً fn(mm, tickIndex) اختيارياً بعد كل دورة (مرّر
// nil لِـfn لتجاهل ذلك) — الطريقة الأسرع لتشغيل محاكاة/لعبة كاملة الحركة بسطر واحد.
fun mm_runLoop(mm, steps, dt, bounceMode, fn) {
    let i = 0;
    while (i < steps) {
        mm_tick(mm, dt, bounceMode);
        if (fn != nil) { fn(mm, i); }
        i = i + 1;
    }
    return mm["tick"];
}

// يُحرِّك قناعاً من موضعه الحالي إلى (targetX, targetY) عبر steps دورة متتالية، باستخدام
// دالة تسهيل (easing) easingFn تأخذ t بين 0 و1 وتُعيد t "مُنعَّماً" بين 0 و1 (مرّر nil
// لحركة خطية بلا تنعيم — mm_lerp مباشرة). يُعيد مصفوفة كل المواضع الوسيطة التي مرّ بها
// (بنفس فكرة stepLoopCollect من lib/loopkit.og.rin)، ويترك القناع عند targetX/targetY تماماً
// في النهاية بلا انجراف عددي (floating point drift).
fun mm_animateTo(mm, name, targetX, targetY, steps, easingFn) {
    let result = [];
    if (mm_exists(mm, name) == false) { return result; }
    let start = mm_position(mm, name);
    if (steps < 1) { steps = 1; }
    let i = 1;
    while (i <= steps) {
        let t = i / steps;
        if (easingFn != nil) { t = easingFn(t); }
        let x = mm_lerp(start["x"], targetX, t);
        let y = mm_lerp(start["y"], targetY, t);
        mm_setPosition(mm, name, x, y);
        mm_recordHistory(mm, name);
        push(result, mm_point(x, y));
        i = i + 1;
    }
    mm_setPosition(mm, name, targetX, targetY);
    return result;
}

// يستدعي fn(mm, name, record, index) على كل قناع مسجَّل في mm بترتيب الإنشاء — حلقة
// عامة موحّدة، بنفس روح iterForEach من lib/iterkit.og.rin لكن فوق سجلّات movingmask
fun mm_forEach(mm, fn) {
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        fn(mm, name, mm["items"][name], i);
        i = i + 1;
    }
    return nil;
}

// نفس mm_forEach لكن مقصورة على أعضاء حاوية منطقية واحدة فقط (mm_membersOf)
fun mm_forEachInContainer(mm, containerName, fn) {
    let members = mm_membersOf(mm, containerName);
    let i = 0;
    while (i < len(members)) {
        let name = members[i];
        fn(mm, name, mm["items"][name], i);
        i = i + 1;
    }
    return nil;
}


// ============================================================================
// 13) الأحداث (lightweight event hooks)
// ============================================================================
//  أحداث جاهزة تُطلقها هذه المكتبة تلقائياً: "enterContainer", "leaveContainer", "bounce",
//  "tick" (انظر الأقسام 6، 7، 12 أعلاه لمكان إطلاق كل واحد بالضبط). يمكنك أيضاً إطلاق
//  أحداثك الخاصة عبر mm__fire من كود تطبيقك مباشرة لأي اسم حدث تختاره.

// يُسجِّل دالة استماع fn(payload) تُستدعى في كل مرة يُطلَق فيها الحدث eventName —
// يمكن تسجيل أكثر من دالة على نفس الحدث؛ تُستدعى كلها بترتيب التسجيل
fun mm_on(mm, eventName, fn) {
    if (has(mm["handlers"], eventName) == false) {
        mm["handlers"][eventName] = [];
    }
    push(mm["handlers"][eventName], fn);
    return true;
}

// يحذف كل الدوال المسجَّلة على حدث eventName معيّن (بلا التأثير على أحداث أخرى)
fun mm_off(mm, eventName) {
    if (has(mm["handlers"], eventName) == false) { return false; }
    remove(mm["handlers"], eventName);
    return true;
}

// (داخلية) يُطلق الحدث eventName فعلياً: يستدعي كل الدوال المسجَّلة عليه بالترتيب،
// مُمرِّراً لها payload كوسيط وحيد — لا تفعل شيئاً إن لم تُسجَّل أي دالة على هذا الحدث
fun mm__fire(mm, eventName, payload) {
    if (has(mm["handlers"], eventName) == false) { return nil; }
    let list = mm["handlers"][eventName];
    let i = 0;
    while (i < len(list)) {
        let handler = list[i];
        handler(payload);
        i = i + 1;
    }
    return nil;
}


// ============================================================================
// 14) الفحص والتلخيص (inspection / debug)
// ============================================================================

// نص وصفي مختصر لقناع متحرك واحد (موضع، سرعة، حاوية، حالة نشاط) — مناسب لِـ print مباشرة
fun mm_describe(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return "mm: لا يوجد قناع متحرك باسم '" + name + "'"; }
    let containerText = "بلا حاوية";
    if (rec["container"] != nil) { containerText = "داخل " + rec["container"]; }
    let stateText = "نشط";
    if (rec["active"] == false) { stateText = "متوقّف"; }
    return name + " @ (" + toString(rec["x"]) + ", " + toString(rec["y"]) + ")"
        + " سرعة(" + toString(rec["vx"]) + ", " + toString(rec["vy"]) + ")"
        + " — " + containerText + " — " + stateText;
}

// خريطة تلخيص عامة لحالة المحرّك بالكامل: عدد الأقنعة، عدد النشطة منها، عدد الحاويات
// المنطقية، عدد المناطق المُسمّاة، عدد الدورات المُنفَّذة، وهل توجد حدود عالم مضبوطة
fun mm_summary(mm) {
    let activeCount = 0;
    let i = 0;
    while (i < len(mm["order"])) {
        if (mm_isActive(mm, mm["order"][i])) { activeCount = activeCount + 1; }
        i = i + 1;
    }
    return {
        total: mm_count(mm),
        active: activeCount,
        containers: len(keys(mm["containers"])),
        regions: len(keys(mm["regions"])),
        tick: mm["tick"],
        hasBounds: mm["bounds"] != nil
    };
}

// نص تسلسلي كامل بحالة كل قناع متحرك مسجَّل في mm، سطر لكل قناع — مفيد لـ print تشخيصي
// سريع لكامل العالم دفعة واحدة
fun mm_toString(mm) {
    let out = "";
    let i = 0;
    while (i < len(mm["order"])) {
        out = out + mm_describe(mm, mm["order"][i]);
        if (i < len(mm["order"]) - 1) { out = out + "\n"; }
        i = i + 1;
    }
    return out;
}


// ============================================================================
// 15) التكامل مع Indsin — مفاهيم مستعارة من محرّك الواجهات Indsintime
// ============================================================================
//  محرّك Indsintime (app/src/main/cpp/indsin/*.h) نظام مستقل تماماً بمفرداته الخاصة: الـ Fabric
//  (شجرة عرض حيّة من Strand)، الـ Warp (خلايا حالة تفاعلية يقرأها @view.* تلقائياً)، الـ
//  Needle (محرّك اللمس/الإصابة الذي يحوّل نقطة لمس إلى Strand وحدث)، والـ Shuttle (يقارن
//  شجرتين ويُنتج Patch[] segmentية). هذه المكتبة (movingmask) لا تُعدِّل ذلك المحرّك ولا
//  تستدعيه مباشرة — فهو C++ منفصل عن هذا الملف الذي يبقى Rin خالصاً مستقلاً بذاته كما في
//  بقية الأقسام أعلاه. بدل ذلك، هذا القسم يستعير *نفس المفاهيم والمفردات* على مستوى بيانات
//  movingmask نفسها، بحيث يسهل ربط عالم الأقنعة المتحركة يدوياً بواجهة Indsintime حقيقية:
//
//    - Warp:    mm_warpFields/mm_warpFieldNames — تُصدِّر حالة قناع كحقول مُسطَّحة بأسماء
//                جاهزة لتُسنَد إلى خلايا `warp` التي يُعلنها المستخدم بنفسه في ملف .rin
//                (Indsintime لا يوفّر تعيين خلية warp بالاسم النصّي ديناميكياً من كود Rin
//                عادي، لذا الإسناد النهائي `اسم_الخلية = القيمة;` يبقى بيد المستخدم كل
//                دورة — انظر المثال في docs/moving-mask.md).
//    - Strand:  mm_setStrandKind/mm_strandKind — تسمية بصرية اختيارية للقناع (نفس أسماء
//                indsin::StrandKind قدر الإمكان: "Text","Image","Button","Card","Icon","Box"...)
//                تُخزَّن في meta العادية؛ مفيدة إن أردت لاحقاً رسم/تصدير القناع بشكل مختلف
//                بحسب نوعه المُعلن.
//    - Fabric:  mm_toFabric — لقطة مسطَّحة (بلا تعشيش أب/أبناء، على عكس شجرة Fabric
//                الحقيقية) بكل الأقنعة النشطة وغير النشطة، بنفس روح "شجرة عرض" جاهزة
//                للطباعة/التصدير/رسم مخصَّص.
//    - Needle:  mm_needleHit/mm_dispatchNeedle — اختبار إصابة نقطة (لمسة/نقرة) ضد كل
//                الأقنعة النشطة (بدائرة نصف قطرها radius حول موضع كل قناع)، وإطلاق حدث
//                "needleTap" على أقرب إصابة — نفس فكرة Needle الحقيقي (تحويل نقطة لمس
//                إلى هدف ثم تنفيذ فعلي) لكن فوق عالم الأقنعة المتحركة بدل شجرة Strand.
//    - Shuttle: mm_snapshot/mm_shuttleDiff — يقارن لقطة سابقة (من mm_snapshot) بحالة mm
//                الحالية، ويُنتج مصفوفة Patch شبيهة بما يُنتجه Shuttle الحقيقي: "Insert"
//                لقناع جديد، "Remove" لقناع اختفى، "Move" لقناع تغيّر موضعه أو نشاطه —
//                مفيد لمعرفة "ماذا تغيّر بالضبط بين دورتين" (مزامنة شبكية، إعادة تشغيل،
//                رسم تفاضلي مخصَّص) بدل إعادة رسم/إرسال كل شيء من الصفر كل دورة.
//    - Actions: mm_defineAction/mm_applyAction — سجل أفعال قابل للتوسعة، بنفس فكرة Needle
//                الحقيقي (يجرِّب دالة مستخدم مُسجَّلة أولاً، ثم يقع على أفعال جاهزة مبنية:
//                activate/deactivate/toggleActive/stop/teleport/nudge/addTag/removeTag)
//                بدل إعادة كتابة نفس سلسلة `if` في كل مكان يُطبَّق فيه فعل على قناع باسمه.
//    - Navigation: mm_navigate/mm_navBack/mm_navReplace/mm_navReload — مشاهد (scenes) مبنية
//                فوق نظام الوسوم (tags) الموجود أصلاً: الانتقال لمشهد يُنشِّط كل قناع يحمل
//                وسم ذلك المشهد ويُعطِّل أقنعة المشاهد الأخرى المعروفة (الأقنعة بلا وسم مشهد
//                تبقى كما هي دوماً — عناصر عامة كالـHUD) — بنفس فكرة NavigationManager
//                الحقيقي (navigate/back/replace/reload) لكن فوق عالم الأقنعة لا شجرة Strand.
//    - Overlay: mm_setOverlay/mm_isOverlay/mm_needleHitTopmost — طبقة أقنعة "علوية" (وسم
//                "overlay" خاص) تُفحَص أولاً في اختبار الإصابة قبل بقية الأقنعة، بنفس فكرة
//                OverlayLayer الحقيقية (نوافذ/تلميحات تعلو المحتوى العادي وتُصاب أولاً).
//    - Dye:     mm_setColor/mm_color — لون بصري اختياري للقناع (نصّ حرّ مثل "#RRGGBB")،
//                يوازي سمة اللون التي يرسمها Dye الحقيقي على كل Strand.
//    - Theme:   mm_defineTheme/mm_setActiveTheme/mm_activeTheme/mm_themeColor — لوحات ألوان
//                مُسمّاة على مستوى المحرّك كله (أدوار مثل primary/secondary/danger...)، بنفس
//                فكرة Pattern Book الحقيقي (`@theme=...`)، مع حدث "themeChanged" عند التبديل.
//    - Object Inspector: mm_toObjectInspector — بطاقة فحص كاملة لقناع واحد (id + مصفوفة
//                حقول مُسمّاة، تشمل meta الحرّة) بنفس شكل بطاقة .object(id) الحيّة التي
//                يرسمها Indsin الحقيقي (rin_indsin_object.h) لكن دون الاعتماد على أي AST حقيقي.

// ---- Warp: تصدير حالة قناع كحقول مُسطَّحة جاهزة لخلايا warp -----------------------------

// أسماء الحقول الأربعة/الخمسة التي يُنتجها mm_warpFields لبادئة prefix مُعطاة — مفيدة
// لمعرفة أسماء خلايا warp الواجب إعلانها مسبقاً في ملف .rin قبل الإسناد إليها
fun mm_warpFieldNames(prefix) {
    return [prefix + "_x", prefix + "_y", prefix + "_vx", prefix + "_vy", prefix + "_active"];
}

// يُصدِّر موضع/سرعة/حالة نشاط قناع كخريطة حقول مُسطَّحة بأسماء prefix+"_x" وهكذا (prefix
// الافتراضي هو اسم القناع نفسه إن مُرِّر nil) — جاهزة لتُسنَد يدوياً إلى خلايا `warp` معلَنة
// بنفس الاسم كل دورة، فتُحدَّث واجهة @view.* التي تقرأ تلك الخلايا تلقائياً عبر إعادة الرسم
// التفاعلية العادية لِـ Indsintime؛ يُعيد nil إن لم يوجد القناع
fun mm_warpFields(mm, name, prefix) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return nil; }
    let p = prefix;
    if (p == nil) { p = name; }
    let out = {};
    out[p + "_x"] = rec["x"];
    out[p + "_y"] = rec["y"];
    out[p + "_vx"] = rec["vx"];
    out[p + "_vy"] = rec["vy"];
    out[p + "_active"] = rec["active"];
    return out;
}

// ---- Strand: تسمية بصرية اختيارية للقناع ------------------------------------------------

// يُسمّي "نوع Strand" مُقترَح لقناع (نصّ حرّ، لكن يُفضَّل استخدام نفس مفردات indsin::StrandKind
// مثل "Text"/"Image"/"Button"/"Card"/"Icon"/"Box"...) — تخزين بسيط فوق meta العادية،
// لا يُنشئ أي Strand حقيقي ولا يتحقّق من صحة القيمة
fun mm_setStrandKind(mm, name, kind) {
    return mm_setMeta(mm, name, "strandKind", kind);
}

// نوع الـStrand المُقترَح لقناع، أو "Unknown" إن لم يُضبط أي نوع له مسبقاً
fun mm_strandKind(mm, name) {
    return mm_getMeta(mm, name, "strandKind", "Unknown");
}

// ---- Fabric: لقطة مسطَّحة بكل الأقنعة ----------------------------------------------------

// مصفوفة بلقطة كل قناع مسجَّل في mm (بترتيب الإنشاء): {name, kind (من mm_strandKind), x, y,
// active, tags} — لقطة مسطَّحة بلا تعشيش أب/أبناء (على عكس شجرة Fabric الحقيقية، فأقنعة
// movingmask لا تملك علاقة أب/ابن أصلاً)، جاهزة للطباعة أو التصدير أو تغذية رسم مخصَّص
fun mm_toFabric(mm) {
    let out = [];
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        let rec = mm["items"][name];
        push(out, {
            name: name,
            kind: mm_strandKind(mm, name),
            x: rec["x"],
            y: rec["y"],
            active: rec["active"],
            tags: rec["tags"]
        });
        i = i + 1;
    }
    return out;
}

// ---- Needle: اختبار إصابة نقطة لمس/نقرة ضد الأقنعة النشطة --------------------------------

// يبحث عن أقرب قناع نشط لنقطة (x, y) ضمن نصف قطر radius (دائرة إصابة حول موضع كل قناع)،
// ويُعيد { name: أقرب اسم أو nil, distance: المسافة أو nil } — nil في الحقلين إن لم يقع أي
// قناع نشط ضمن radius من تلك النقطة إطلاقاً
fun mm_needleHit(mm, x, y, radius) {
    let point = mm_point(x, y);
    let bestName = nil;
    let bestDistance = nil;
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        if (mm_isActive(mm, name)) {
            let d = mm_distance(point, mm_position(mm, name));
            if (d <= radius) {
                if (bestDistance == nil or d < bestDistance) {
                    bestDistance = d;
                    bestName = name;
                }
            }
        }
        i = i + 1;
    }
    return { name: bestName, distance: bestDistance };
}

// يُنفِّذ mm_needleHit عند (x, y) بنصف قطر radius، وإن وُجدت إصابة يُطلق حدث "needleTap"
// (payload: { name, x, y, distance }) — سجِّل مستمعاً عبر mm_on(mm, "needleTap", fn) لتنفيذ
// فعل حقيقي عند الإصابة، بنفس روح Needle الحقيقي (تحويل نقطة لمس إلى تنفيذ فعلي). يُعيد اسم
// القناع المُصاب، أو nil إن لم تُصب أي دائرة إصابة
fun mm_dispatchNeedle(mm, x, y, radius) {
    let hit = mm_needleHit(mm, x, y, radius);
    if (hit["name"] != nil) {
        mm__fire(mm, "needleTap", { name: hit["name"], x: x, y: y, distance: hit["distance"] });
    }
    return hit["name"];
}

// ---- Shuttle: مقارنة لقطتين وإنتاج Patch[] ----------------------------------------------

// لقطة خفيفة بكل الأقنعة (name/x/y/active فقط، بلا سرعة أو مسار أو أثر) — احتفظ بناتج هذه
// الدالة جانباً، ثم مرِّره لاحقاً إلى mm_shuttleDiff لمعرفة ما تغيّر بالضبط منذ تلك اللحظة
fun mm_snapshot(mm) {
    let out = [];
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        let rec = mm["items"][name];
        push(out, { name: name, x: rec["x"], y: rec["y"], active: rec["active"] });
        i = i + 1;
    }
    return out;
}

// يقارن لقطة سابقة previousSnapshot (من mm_snapshot) بحالة mm الحالية، ويُعيد مصفوفة Patch
// (كل عنصر { kind, name, x, y }): "Insert" لقناع موجود الآن ولم يكن في اللقطة السابقة،
// "Remove" لقناع كان في اللقطة السابقة واختفى الآن (x/y يكونان nil)، "Move" لقناع موجود في
// الحالتين لكن تغيّر موضعه أو نشاطه. الأقنعة التي لم تتغيّر إطلاقاً لا تظهر في الناتج —
// تماماً كما لا يُنتج Shuttle الحقيقي Patch لأي Strand لم يتغيّر شيء في سماته
fun mm_shuttleDiff(mm, previousSnapshot) {
    let prevIndex = {};
    let j = 0;
    while (j < len(previousSnapshot)) {
        let snap = previousSnapshot[j];
        prevIndex[snap["name"]] = snap;
        j = j + 1;
    }
    let patches = [];
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        let rec = mm["items"][name];
        if (has(prevIndex, name)) {
            let old = prevIndex[name];
            let moved = mm_nearlyEqual(old["x"], rec["x"]) == false or mm_nearlyEqual(old["y"], rec["y"]) == false or old["active"] != rec["active"];
            if (moved) {
                push(patches, { kind: "Move", name: name, x: rec["x"], y: rec["y"] });
            }
            remove(prevIndex, name);
        } else {
            push(patches, { kind: "Insert", name: name, x: rec["x"], y: rec["y"] });
        }
        i = i + 1;
    }
    let removedNames = keys(prevIndex);
    let k = 0;
    while (k < len(removedNames)) {
        push(patches, { kind: "Remove", name: removedNames[k], x: nil, y: nil });
        k = k + 1;
    }
    return patches;
}


// ---- Actions: نظام أفعال جاهزة قابل للتوسعة (Action Engine) ----------------------------

// يُسجِّل دالة فعل مخصَّصة fn(mm, name, payload) باسم actionName على محرّك mm بالكامل (وليس
// على قناع واحد) — تُستدعى لاحقاً عبر mm_applyAction بنفس الاسم على أي قناع. تستبدل أي فعل
// مخصَّص مسجَّل سابقاً بنفس الاسم بالكامل (لا تراكم)
fun mm_defineAction(mm, actionName, fn) {
    mm["actions"][actionName] = fn;
    return true;
}

// يُطبِّق فعلاً باسم actionName على قناع name، بترتيب محاولتين بالضبط (بنفس منطق Needle
// الحقيقي: دالة مستخدم أولاً، ثم أفعال جاهزة مبنية):
//   1) إن كان actionName مُسجَّلاً عبر mm_defineAction: تُستدعى تلك الدالة fn(mm, name, payload)
//      وتُعاد قيمتها كما هي (أي شيء تُعيده الدالة المخصَّصة).
//   2) وإلا: يُحاول الأفعال الجاهزة التالية (payload بحسب الفعل، أو نادَته بلا حاجة إليه):
//      "activate"     -> mm_setActive(mm, name, true)
//      "deactivate"   -> mm_setActive(mm, name, false)
//      "toggleActive" -> يعكس mm_isActive الحالية
//      "stop"         -> mm_stop(mm, name)
//      "teleport"     -> mm_setPosition(mm, name, payload["x"], payload["y"])
//      "nudge"        -> mm_translate(mm, name, payload["dx"], payload["dy"])
//      "addTag"       -> mm_addTag(mm, name, payload)  (payload نصّ الوسم مباشرة)
//      "removeTag"    -> يُزيل payload من قائمة وسوم القناع (لا يوجد mm_removeTag أصلاً؛
//                        يُعاد بناء المصفوفة هنا عبر mm__arrayWithout)
//   يُعيد false إن لم يكن actionName معروفاً بأي من الطريقتين، أو إن لم يكن القناع موجوداً
fun mm_applyAction(mm, name, actionName, payload) {
    if (has(mm["actions"], actionName)) {
        let fn = mm["actions"][actionName];
        return fn(mm, name, payload);
    }
    if (mm_exists(mm, name) == false) { return false; }
    if (actionName == "activate") { return mm_setActive(mm, name, true); }
    if (actionName == "deactivate") { return mm_setActive(mm, name, false); }
    if (actionName == "toggleActive") { return mm_setActive(mm, name, mm_isActive(mm, name) == false); }
    if (actionName == "stop") { return mm_stop(mm, name); }
    if (actionName == "teleport") { return mm_setPosition(mm, name, payload["x"], payload["y"]); }
    if (actionName == "nudge") { return mm_translate(mm, name, payload["dx"], payload["dy"]); }
    if (actionName == "addTag") { return mm_addTag(mm, name, payload); }
    if (actionName == "removeTag") {
        mm["items"][name]["tags"] = mm__arrayWithout(mm["items"][name]["tags"], payload);
        return true;
    }
    return false;
}


// ---- Navigation: مشاهد (scenes) عبر الوسوم، بمكدّس تنقّل -------------------------------
//  الأقنعة التي لا تحمل أي وسم مشهد مُعرَّف من قبل (عبر mm_navigate/mm_navReplace) تبقى
//  دوماً كما هي — لا يُغيِّر التنقّل نشاطها إطلاقاً؛ فقط الأقنعة الموسومة صراحة بأحد أسماء
//  المشاهد المعروفة (mm["nav"]["knownScenes"]) هي التي تُنشَّط/تُعطَّل عند كل انتقال.

// (داخلية) تُطبِّق ظهور مشهد sceneName فعلياً: تُنشِّط أقنعته وتُعطِّل بقية المشاهد المعروفة
fun mm__applyScene(mm, sceneName) {
    if (mm__arrayHas(mm["nav"]["knownScenes"], sceneName) == false) {
        push(mm["nav"]["knownScenes"], sceneName);
    }
    let scenes = mm["nav"]["knownScenes"];
    let s = 0;
    while (s < len(scenes)) {
        let members = mm_withTag(mm, scenes[s]);
        let m = 0;
        while (m < len(members)) {
            mm_setActive(mm, members[m], scenes[s] == sceneName);
            m = m + 1;
        }
        s = s + 1;
    }
    return true;
}

// ينتقل إلى مشهد جديد sceneName: يدفع المشهد الحالي (إن وُجد) إلى مكدّس التنقّل، يُنشِّط كل
// قناع موسوم بـsceneName، ويُعطِّل أقنعة أي مشهد آخر معروف. يُطلق حدث "navigate" (payload:
// { to: sceneName, from: المشهد السابق أو nil })
fun mm_navigate(mm, sceneName) {
    let previous = mm["nav"]["current"];
    if (previous != nil) { push(mm["nav"]["stack"], previous); }
    mm["nav"]["current"] = sceneName;
    mm__applyScene(mm, sceneName);
    mm__fire(mm, "navigate", { to: sceneName, from: previous });
    return true;
}

// يعود إلى المشهد الذي كان قبل المشهد الحالي مباشرة (من رأس مكدّس التنقّل)، بلا دفع المشهد
// الحالي إلى المكدّس (على عكس mm_navigate). لا يفعل شيئاً ويُعيد false إن كان المكدّس فارغاً
fun mm_navBack(mm) {
    if (len(mm["nav"]["stack"]) == 0) { return false; }
    let previous = mm["nav"]["current"];
    let target = pop(mm["nav"]["stack"]);
    mm["nav"]["current"] = target;
    mm__applyScene(mm, target);
    mm__fire(mm, "navigate", { to: target, from: previous });
    return true;
}

// ينتقل إلى مشهد جديد بديلاً عن الحالي دون دفعه إلى المكدّس (المشهد الحالي "يُستبدَل"، فلن
// يعود إليه mm_navBack) — مفيد لشاشات لا يجب العودة إليها (تسجيل الدخول بعد نجاحه مثلاً)
fun mm_navReplace(mm, sceneName) {
    let previous = mm["nav"]["current"];
    mm["nav"]["current"] = sceneName;
    mm__applyScene(mm, sceneName);
    mm__fire(mm, "navigate", { to: sceneName, from: previous });
    return true;
}

// يُعيد تطبيق ظهور/اختفاء المشهد الحالي من جديد (بلا تغيير المكدّس أو المشهد الحالي)،
// ويُطلق حدث "navigate" بنفس المصدر والهدف — مفيد بعد إضافة أقنعة جديدة موسومة بمشهد قائم
fun mm_navReload(mm) {
    let current = mm["nav"]["current"];
    if (current == nil) { return false; }
    mm__applyScene(mm, current);
    mm__fire(mm, "navigate", { to: current, from: current });
    return true;
}

// اسم المشهد الحالي، أو nil إن لم يُستدعَ mm_navigate/mm_navReplace بعد على الإطلاق
fun mm_currentScene(mm) {
    return mm["nav"]["current"];
}


// ---- Overlay: طبقة علوية فوق الجميع (أولوية في اختبار الإصابة) -------------------------
//  طبقة الـ"overlay" هنا مجرّد وسم خاص محجوز باسم "overlay" فوق نظام الوسوم العادي —
//  mm_needleHitTopmost تفحص أولاً كل الأقنعة الموسومة به قبل بقية العالم، فتُطابق أول
//  إصابة عليها بصرف النظر عمّا إذا كان هناك قناع أقرب في الطبقة العادية تحتها.

// يضع/يُزيل وسم "overlay" عن قناع (flag=true يضعه، flag=false يُزيله)
fun mm_setOverlay(mm, name, flag) {
    if (mm_exists(mm, name) == false) { return false; }
    if (flag) { return mm_addTag(mm, name, "overlay"); }
    mm["items"][name]["tags"] = mm__arrayWithout(mm["items"][name]["tags"], "overlay");
    return true;
}

// هل يحمل القناع وسم "overlay" حالياً؟
fun mm_isOverlay(mm, name) {
    return mm_hasTag(mm, name, "overlay");
}

// نفس mm_needleHit، لكنها تفحص أولاً حصرياً الأقنعة الموسومة "overlay" (طبقة علوية)؛ إن
// أصابت واحداً منها تُعيده فوراً بلا فحص الطبقة العادية إطلاقاً، وإلا تقع على mm_needleHit
// العادية فوق كل الأقنعة المتبقية — بهذا تُصاب الطبقة العلوية دوماً أولاً كما في Needle
// الحقيقي مع OverlayLayer
fun mm_needleHitTopmost(mm, x, y, radius) {
    let overlayNames = mm_withTag(mm, "overlay");
    let point = mm_point(x, y);
    let bestName = nil;
    let bestDistance = nil;
    let i = 0;
    while (i < len(overlayNames)) {
        let name = overlayNames[i];
        if (mm_isActive(mm, name)) {
            let d = mm_distance(point, mm_position(mm, name));
            if (d <= radius) {
                if (bestDistance == nil or d < bestDistance) {
                    bestDistance = d;
                    bestName = name;
                }
            }
        }
        i = i + 1;
    }
    if (bestName != nil) { return { name: bestName, distance: bestDistance }; }
    return mm_needleHit(mm, x, y, radius);
}


// ---- Dye: لون بصري اختياري للقناع -------------------------------------------------------

// يضبط لوناً بصرياً اختيارياً لقناع (نصّ حرّ، عادة "#RRGGBB") — تخزين بسيط فوق meta العادية
fun mm_setColor(mm, name, colorHex) {
    return mm_setMeta(mm, name, "color", colorHex);
}

// لون القناع الحالي، أو fallback إن لم يُضبط له لون بعد (مرّر nil لِـfallback افتراضياً)
fun mm_color(mm, name, fallback) {
    return mm_getMeta(mm, name, "color", fallback);
}


// ---- Theme: لوحات ألوان مُسمّاة على مستوى المحرّك (Pattern Book) -----------------------

// يُعرِّف/يُحدِّث Theme باسم themeName على مستوى المحرّك كله: خريطة أدوار لونية حرّة (مثل
// primary/secondary/success/danger/warning/info/neutral...) بقيم نصّية "#RRGGBB". يستبدل أي
// Theme سابق بنفس الاسم بالكامل
fun mm_defineTheme(mm, themeName, colorsMap) {
    mm["themes"][themeName] = colorsMap;
    return true;
}

// يجعل Theme المُعرَّف مسبقاً باسم themeName هو النشط حالياً على مستوى المحرّك؛ يُعيد false
// بلا أي تأثير إن لم يكن ذلك الـTheme مُعرَّفاً أصلاً عبر mm_defineTheme. يُطلق حدث
// "themeChanged" (payload: { theme: themeName }) عند النجاح
fun mm_setActiveTheme(mm, themeName) {
    if (has(mm["themes"], themeName) == false) { return false; }
    mm["activeTheme"] = themeName;
    mm__fire(mm, "themeChanged", { theme: themeName });
    return true;
}

// اسم الـTheme النشط حالياً، أو nil إن لم يُضبط أي Theme بعد
fun mm_activeTheme(mm) {
    return mm["activeTheme"];
}

// قيمة دور لوني roleName من الـTheme النشط حالياً (مثل "primary")، أو fallback إن لم يوجد
// Theme نشط أصلاً، أو لم يحمل ذلك الـTheme هذا الدور
fun mm_themeColor(mm, roleName, fallback) {
    let active = mm["activeTheme"];
    if (active == nil) { return fallback; }
    if (has(mm["themes"], active) == false) { return fallback; }
    let palette = mm["themes"][active];
    if (has(palette, roleName) == false) { return fallback; }
    return palette[roleName];
}


// ---- Object Inspector: بطاقة فحص كاملة لقناع واحد --------------------------------------

// بطاقة فحص حيّة لقناع واحد، بنفس روح بطاقة `.object(id) ... container.(); .end/object`
// التي يرسمها Indsin الحقيقي (rin_indsin_object.h) لكن مبنية فوق سجلّ movingmask مباشرة بلا أي
// AST: { id: name, fields: [ {name, value}, ... ] } — الحقول الأساسية أولاً (x/y/vx/vy/
// active/container/strandKind/color)، ثم كل مفتاح إضافي من meta الحرّة (بترتيب مفاتيحه)
// بلا تكرار (يُتجاوَز أي مفتاح meta اسمه "strandKind" أو "color" لأنه أُضيف مسبقاً أعلاه
// بقيمته الصحيحة عبر mm_strandKind/mm_color بدل قراءته مباشرة من meta مرّتين). يُعيد nil
// إن لم يوجد القناع
fun mm_toObjectInspector(mm, name) {
    let rec = mm_get(mm, name);
    if (rec == nil) { return nil; }
    let fields = [];
    push(fields, { name: "x", value: rec["x"] });
    push(fields, { name: "y", value: rec["y"] });
    push(fields, { name: "vx", value: rec["vx"] });
    push(fields, { name: "vy", value: rec["vy"] });
    push(fields, { name: "active", value: rec["active"] });
    push(fields, { name: "container", value: rec["container"] });
    push(fields, { name: "strandKind", value: mm_strandKind(mm, name) });
    push(fields, { name: "color", value: mm_color(mm, name, nil) });
    let metaKeys = keys(rec["meta"]);
    let i = 0;
    while (i < len(metaKeys)) {
        let key = metaKeys[i];
        if (key != "strandKind" and key != "color") {
            push(fields, { name: key, value: rec["meta"][key] });
        }
        i = i + 1;
    }
    return { id: name, fields: fields };
}


// ============================================================================
// 16) التكوين الجماعي والانسيابية (Flocking / Rigid Formations)
// ============================================================================
//  Boids الكلاسيكية (Craig Reynolds): كل قناع "طائر" يعدّل تسارعه بناءً على جيرانه القريبين
//  عبر ثلاث قواعد بسيطة تُركَّب معاً: الابتعاد (separation)، المحاذاة (alignment)، والتماسك
//  (cohesion). القوى هنا "توجيهية" (steering): تُضاف إلى تسارع القناع عبر mm_applyForce ولا
//  تُحرِّكه مباشرة — استدعِ mm_integrate/mm_tick بعدها لتطبيق الحركة الفعلية. مرّر لهذه الدوال
//  مصفوفة أسماء الجيران (others) بدل كل القناع (استخدم mm_spatialNeighbors من القسم 19
//  لحساب هذه القائمة بكفاءة بدل المرور على كل الأقنعة في كل خطوة).

// (داخلية) يحصر متجه قوة (fx, fy) بحيث لا يتجاوز مقداره maxForce، مع الحفاظ على اتجاهه
fun mm__limitMagnitude(fx, fy, maxForce) {
    let mag = sqrt(fx * fx + fy * fy);
    if (mag <= maxForce or mag < MM_EPSILON) { return mm_point(fx, fy); }
    return mm_point((fx / mag) * maxForce, (fy / mag) * maxForce);
}

// قوة الابتعاد: تدفع القناع بعيداً عن كل جار أقرب من desiredSeparation، بوزن يتناسب عكسياً
// مع المسافة (الجيران الأقرب يدفعون أقوى). تُطبَّق مباشرة عبر mm_applyForce وتُعاد أيضاً.
fun mm_flockSeparation(mm, name, others, desiredSeparation, maxForce) {
    let here = mm_position(mm, name);
    if (here == nil) { return mm_point(0, 0); }
    let steerX = 0;
    let steerY = 0;
    let count = 0;
    let i = 0;
    while (i < len(others)) {
        let otherName = others[i];
        if (otherName != name) {
            let otherPos = mm_position(mm, otherName);
            if (otherPos != nil) {
                let d = mm_distance(here, otherPos);
                if (d > 0 and d < desiredSeparation) {
                    steerX = steerX + (here["x"] - otherPos["x"]) / d;
                    steerY = steerY + (here["y"] - otherPos["y"]) / d;
                    count = count + 1;
                }
            }
        }
        i = i + 1;
    }
    if (count > 0) {
        steerX = steerX / count;
        steerY = steerY / count;
    }
    let limited = mm__limitMagnitude(steerX, steerY, maxForce);
    mm_applyForce(mm, name, limited["x"], limited["y"]);
    return limited;
}

// قوة المحاذاة: تُقرِّب سرعة القناع من متوسط سرعات جيرانه (يتّجه القطيع بنفس الاتجاه تقريباً)
fun mm_flockAlignment(mm, name, others, maxForce) {
    let sumVx = 0;
    let sumVy = 0;
    let count = 0;
    let i = 0;
    while (i < len(others)) {
        let otherName = others[i];
        if (otherName != name and mm_exists(mm, otherName)) {
            let v = mm_velocity(mm, otherName);
            sumVx = sumVx + v["x"];
            sumVy = sumVy + v["y"];
            count = count + 1;
        }
        i = i + 1;
    }
    if (count == 0) { return mm_point(0, 0); }
    let limited = mm__limitMagnitude(sumVx / count, sumVy / count, maxForce);
    mm_applyForce(mm, name, limited["x"], limited["y"]);
    return limited;
}

// قوة التماسك: تسحب القناع نحو مركز ثقل جيرانه (يبقى القطيع مجتمعاً بلا تشتت)
fun mm_flockCohesion(mm, name, others, maxForce) {
    let here = mm_position(mm, name);
    if (here == nil) { return mm_point(0, 0); }
    let sumX = 0;
    let sumY = 0;
    let count = 0;
    let i = 0;
    while (i < len(others)) {
        let otherName = others[i];
        if (otherName != name) {
            let otherPos = mm_position(mm, otherName);
            if (otherPos != nil) {
                sumX = sumX + otherPos["x"];
                sumY = sumY + otherPos["y"];
                count = count + 1;
            }
        }
        i = i + 1;
    }
    if (count == 0) { return mm_point(0, 0); }
    let limited = mm__limitMagnitude((sumX / count) - here["x"], (sumY / count) - here["y"], maxForce);
    mm_applyForce(mm, name, limited["x"], limited["y"]);
    return limited;
}

// يُطبِّق القواعد الثلاث معاً بأوزان قابلة للتخصيص (weights: {separation, alignment, cohesion})
// دفعة واحدة على قناع name بين جيرانه others — استدعِها لكل قناع كل دورة قبل mm_tick لمحاكاة
// سرب/قطيع كامل، ثم mm_tick لتطبيق الحركة الناتجة فعلياً
fun mm_flockStep(mm, name, others, desiredSeparation, maxForce, weights) {
    let sep = mm_flockSeparation(mm, name, others, desiredSeparation, maxForce * weights["separation"]);
    let ali = mm_flockAlignment(mm, name, others, maxForce * weights["alignment"]);
    let coh = mm_flockCohesion(mm, name, others, maxForce * weights["cohesion"]);
    return { separation: sep, alignment: ali, cohesion: coh };
}

// ---- التكوينات الجماعية الجامدة (Rigid Formations) -------------------------------------
// خلافاً للانسيابية (flocking) القائمة على قوى تقريبية، التكوين الجامد يُثبِّت إزاحة دقيقة
// (dx, dy) لكل تابع عن قائده — مفيد لتشكيلات عسكرية/مركبات مرافقة تحافظ على شكل ثابت تماماً.

// يُسجِّل إزاحة ثابتة (dx, dy) لقناع "follower" نسبةً لموضع قناع "leader" الحالي
fun mm_setFormationOffset(mm, followerName, leaderName, dx, dy) {
    return mm_setMeta(mm, followerName, "formation", { leader: leaderName, dx: dx, dy: dy });
}

// يُزيل التكوين عن قناع تابع (يعود للحركة الحرّة العادية دون تدخّل mm_applyFormations)
fun mm_clearFormationOffset(mm, followerName) {
    if (mm_exists(mm, followerName) == false) { return false; }
    let metaMap = mm["items"][followerName]["meta"];
    if (has(metaMap, "formation")) { remove(metaMap, "formation"); }
    return true;
}

// يُحرِّك فوراً كل قناع يملك تكوين مُسجَّل (عبر mm_setFormationOffset) إلى موضع قائده الحالي
// زائد إزاحته الثابتة — استدعِها بعد تحريك القادة (عبر seek/path/تحكّم مباشر) في كل دورة
fun mm_applyFormations(mm) {
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        let formation = mm_getMeta(mm, name, "formation", nil);
        if (formation != nil) {
            let leaderPos = mm_position(mm, formation["leader"]);
            if (leaderPos != nil) {
                mm_setPosition(mm, name, leaderPos["x"] + formation["dx"], leaderPos["y"] + formation["dy"]);
                mm_recordHistory(mm, name);
            }
        }
        i = i + 1;
    }
    return nil;
}


// ============================================================================
// 17) آلة حالات محدودة لكل قناع (Per-Mask Finite State Machine)
// ============================================================================
//  آلة حالة بسيطة (FSM) مخزَّنة داخل meta كل قناع: حالة حالية + جدول انتقالات
//  (fromState + event -> toState). مفيدة لسلوك كائنات الألعاب/الواجهات (idle/walk/attack،
//  أو draft/submitted/approved لعنصر واجهة) بلا كتابة سلاسل if متكرّرة يدوياً في كل مكان.

// يُهيّئ آلة حالة لقناع بحالة ابتدائية initialState وجدول انتقالات فارغ. يستبدل أي آلة
// حالة كانت مُسجَّلة سابقاً على هذا القناع بالكامل.
fun mm_fsmDefine(mm, name, initialState) {
    return mm_setMeta(mm, name, "fsm", { state: initialState, transitions: {} });
}

// يُسجِّل انتقالاً: عند وقوع الحدث event والقناع في الحالة fromState، ينتقل إلى toState.
// يتطلّب استدعاء mm_fsmDefine على هذا القناع أولاً؛ يُعيد false وبلا تأثير إن لم توجد آلة
// حالة عليه بعد.
fun mm_fsmAddTransition(mm, name, fromState, event, toState) {
    let fsm = mm_getMeta(mm, name, "fsm", nil);
    if (fsm == nil) { return false; }
    let key = fromState + "::" + event;
    fsm["transitions"][key] = toState;
    return true;
}

// الحالة الحالية لآلة حالة قناع، أو nil إن لم تُعرَّف له آلة حالة أصلاً
fun mm_fsmState(mm, name) {
    let fsm = mm_getMeta(mm, name, "fsm", nil);
    if (fsm == nil) { return nil; }
    return fsm["state"];
}

// هل آلة حالة القناع في الحالة state بالضبط؟ false أيضاً إن لم توجد آلة حالة عليه
fun mm_fsmIs(mm, name, state) {
    return mm_fsmState(mm, name) == state;
}

// يُطلق حدثاً event على آلة حالة القناع: إن وُجد انتقال مُسجَّل من حالتها الحالية بهذا
// الحدث، تنتقل إلى الحالة الجديدة وتُطلق حدث محرّك "fsmChanged" (payload: {name, from, to,
// event})، وتُعيد الحالة الجديدة. وإلا (لا انتقال مطابق، أو لا آلة حالة على القناع أصلاً)
// تبقى الحالة كما هي وتُعاد الحالة الحالية (أو nil إن لم توجد آلة حالة أصلاً).
fun mm_fsmFire(mm, name, event) {
    let fsm = mm_getMeta(mm, name, "fsm", nil);
    if (fsm == nil) { return nil; }
    let key = fsm["state"] + "::" + event;
    if (has(fsm["transitions"], key)) {
        let from = fsm["state"];
        let to = fsm["transitions"][key];
        fsm["state"] = to;
        mm__fire(mm, "fsmChanged", { name: name, from: from, to: to, event: event });
        return to;
    }
    return fsm["state"];
}


// ============================================================================
// 18) التسلسل والاستعادة (Serialization / Save & Load عبر JSON)
// ============================================================================
//  يحوِّل حالة المحرّك (أو يستعيدها) إلى/من نص JSON عبر jsonEncode/jsonDecode الأصليتين —
//  مفيد لحفظ تقدّم لعبة/محاكاة على القرص (عبر دوال storage الأصلية) واستعادتها لاحقاً
//  بالضبط. **قيد مهم**: القيم الدالية (handlers عبر mm_on، actions المخصَّصة عبر
//  mm_defineAction) لا يمكن تسلسلها في JSON — تُستبعَد تلقائياً من الناتج (mm_toPlainData)،
//  وتبقى مسؤولية المستخدم إعادة تسجيلها بعد الاستعادة إن احتاج إليها.

// يُصدِّر البيانات الخام القابلة للتسلسل من محرّك mm بالكامل كخريطة عادية (بلا handlers/
// actions/themes التي تحوي دوال) — استخدمها مباشرة أو مرّرها لـjsonEncode بنفسك
fun mm_toPlainData(mm) {
    return {
        items: mm["items"],
        order: mm["order"],
        containers: mm["containers"],
        bounds: mm["bounds"],
        regions: mm["regions"],
        tick: mm["tick"],
        historyLimit: mm["historyLimit"],
        nav: mm["nav"]
    };
}

// نص JSON كامل لحالة محرّك mm (عبر mm_toPlainData + jsonEncode) — جاهز للكتابة في ملف عبر
// دوال storage الأصلية على منصتك
fun mm_serialize(mm) {
    return jsonEncode(mm_toPlainData(mm));
}

// يبني محرّكاً جديداً تماماً من نص JSON أنتجه mm_serialize سابقاً — بلا أي handlers/actions/
// themes مُسجَّلة (استخدم mm_deserializeInto بدلاً منه للإبقاء على مستمعين/أفعال محرّك قائم
// مع استعادة بيانات الأقنعة فقط)
fun mm_deserialize(jsonText) {
    let data = jsonDecode(jsonText);
    let mm = mm_new();
    mm["items"] = data["items"];
    mm["order"] = data["order"];
    mm["containers"] = data["containers"];
    mm["bounds"] = data["bounds"];
    mm["regions"] = data["regions"];
    mm["tick"] = data["tick"];
    mm["historyLimit"] = data["historyLimit"];
    mm["nav"] = data["nav"];
    return mm;
}

// يستعيد بيانات الأقنعة/الحدود/المناطق من نص JSON *داخل* محرّك mm قائم بالفعل، محتفظاً
// بكل handlers/actions/themes المُسجَّلة عليه مسبقاً بلا تغيير — الخيار الأنسب لاستعادة حفظ
// أثناء تشغيل التطبيق (بدل استبدال المحرّك بالكامل كما تفعل mm_deserialize)
fun mm_deserializeInto(mm, jsonText) {
    let data = jsonDecode(jsonText);
    mm["items"] = data["items"];
    mm["order"] = data["order"];
    mm["containers"] = data["containers"];
    mm["bounds"] = data["bounds"];
    mm["regions"] = data["regions"];
    mm["tick"] = data["tick"];
    mm["historyLimit"] = data["historyLimit"];
    mm["nav"] = data["nav"];
    return true;
}


// ============================================================================
// 19) الفهرسة المكانية لتسريع استعلامات الجوار (Spatial Grid Index)
// ============================================================================
//  mm_nearestTo وmm_needleHit (القسمان 11 و15) تفحصان كل قناع مسجَّل في كل استدعاء — O(n)
//  لكل استعلام، وهذا يصبح بطيئاً مع مئات/آلاف الأقنعة النشطة كل دورة. الفهرس المكاني هنا
//  يقسم العالم إلى خلايا مربّعة بحجم cellSize، ويُصنِّف كل قناع في خليته الحالية، فيصبح
//  إيجاد "جيران قريبين" يفحص فقط الخلايا المجاورة مباشرة بدل كل قناع في العالم.

// (داخلية) مفتاح نصّي فريد لخلية الشبكة المكانية التي تقع فيها نقطة (x, y) بحجم خلية cellSize
fun mm__spatialCellKey(x, y, cellSize) {
    return toString(floor(x / cellSize)) + "," + toString(floor(y / cellSize));
}

// يبني (أو يُعيد بناء) الفهرس المكاني لمحرّك mm بالكامل من مواضع كل الأقنعة *النشطة* حالياً،
// بحجم خلية cellSize. استدعِها مرة كل دورة (بعد mm_tick) قبل أي استعلام عبر mm_spatialNeighbors
// في نفس تلك الدورة — الفهرس لقطة لحظية، لا يتحدَّث تلقائياً بعد تحريك أي قناع لاحقاً.
fun mm_buildSpatialIndex(mm, cellSize) {
    let buckets = {};
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        if (mm_isActive(mm, name)) {
            let rec = mm["items"][name];
            let key = mm__spatialCellKey(rec["x"], rec["y"], cellSize);
            if (has(buckets, key) == false) { buckets[key] = []; }
            push(buckets[key], name);
        }
        i = i + 1;
    }
    mm["spatial"] = { cellSize: cellSize, buckets: buckets };
    return true;
}

// مصفوفة بأسماء كل الأقنعة النشطة الأخرى الواقعة فعلياً ضمن مسافة radius من قناع name،
// بالبحث فقط ضمن خليته الحالية وثماني خلايا مجاورة (بدل كل قناع في العالم) — يتطلّب استدعاء
// mm_buildSpatialIndex أولاً في نفس الدورة، وإلا يُعيد مصفوفة فارغة دوماً
fun mm_spatialNeighbors(mm, name, radius) {
    let out = [];
    if (has(mm, "spatial") == false) { return out; }
    let here = mm_position(mm, name);
    if (here == nil) { return out; }
    let spatial = mm["spatial"];
    let cellSize = spatial["cellSize"];
    let cx = floor(here["x"] / cellSize);
    let cy = floor(here["y"] / cellSize);
    let dxCell = 0 - 1;
    while (dxCell <= 1) {
        let dyCell = 0 - 1;
        while (dyCell <= 1) {
            let key = toString(cx + dxCell) + "," + toString(cy + dyCell);
            if (has(spatial["buckets"], key)) {
                let bucket = spatial["buckets"][key];
                let i = 0;
                while (i < len(bucket)) {
                    let otherName = bucket[i];
                    if (otherName != name) {
                        let d = mm_distance(here, mm_position(mm, otherName));
                        if (d <= radius) { push(out, otherName); }
                    }
                    i = i + 1;
                }
            }
            dyCell = dyCell + 1;
        }
        dxCell = dxCell + 1;
    }
    return out;
}


// ============================================================================
// 20) المؤقتات والتهدئة لكل قناع (Timers / Cooldowns)
// ============================================================================
//  مؤقتات مُسمّاة مخزَّنة داخل meta كل قناع، تُعَدّ تنازلياً بوحدة "دورات" (ticks) — مفيدة
//  لتهدئة قدرة (cooldown)، عدّاد استعداد قبل تفعيل، أو أي حدث مؤجَّل مرتبط بقناع بعينه.

// يبدأ (أو يُعيد ضبط) مؤقتاً باسم timerName على قناع name لمدة durationTicks دورة
fun mm_setTimer(mm, name, timerName, durationTicks) {
    let timers = mm_getMeta(mm, name, "timers", nil);
    if (timers == nil) {
        timers = {};
        mm_setMeta(mm, name, "timers", timers);
    }
    timers[timerName] = durationTicks;
    return true;
}

// هل المؤقت timerName لا يزال يعدّ (لم يبلغ الصفر بعد) على قناع name؟ false أيضاً إن لم
// يُبدَأ هذا المؤقت أصلاً على هذا القناع
fun mm_isTimerActive(mm, name, timerName) {
    let timers = mm_getMeta(mm, name, "timers", nil);
    if (timers == nil) { return false; }
    if (has(timers, timerName) == false) { return false; }
    return timers[timerName] > 0;
}

// عدد الدورات المتبقية للمؤقت timerName على قناع name، أو 0 إن لم يُبدَأ أصلاً
fun mm_timerRemaining(mm, name, timerName) {
    let timers = mm_getMeta(mm, name, "timers", nil);
    if (timers == nil) { return 0; }
    if (has(timers, timerName) == false) { return 0; }
    return timers[timerName];
}

// يُنقِص كل المؤقتات النشطة على كل الأقنعة بمقدار dt دورة واحدة (استدعِها مرة كل دورة، عادة
// مباشرة بعد mm_tick). كل مؤقت يبلغ الصفر أو أقل يُطلِق حدث محرّك "timerDone" (payload:
// {name: اسم القناع, timer: اسم المؤقت}) مرة واحدة فقط عند لحظة البلوغ، ثم يبقى عند 0.
fun mm_tickTimers(mm, dt) {
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        let timers = mm_getMeta(mm, name, "timers", nil);
        if (timers != nil) {
            let timerNames = keys(timers);
            let t = 0;
            while (t < len(timerNames)) {
                let timerName = timerNames[t];
                if (timers[timerName] > 0) {
                    timers[timerName] = timers[timerName] - dt;
                    if (timers[timerName] <= 0) {
                        timers[timerName] = 0;
                        mm__fire(mm, "timerDone", { name: name, timer: timerName });
                    }
                }
                t = t + 1;
            }
        }
        i = i + 1;
    }
    return nil;
}


// ============================================================================
// 21) قياس الإطارات في الثانية وخطوة زمنية ثابتة (FPS Meter / Fixed Timestep)
// ============================================================================
//  كل دوال movingmask (mm_tick, mm_integrate, mm_tickTimers...) "منطقية" بحتة: تأخذ dt كوسيط
//  بلا أي علاقة بالزمن الفعلي — المكتبة نفسها لا تقرأ ساعة النظام (لا يوفّر مفسّر Rin دالة
//  وقت أصلية حتى الآن). هذا القسم يبني *فوق* dt الذي يُمرِّره التطبيق المضيف (من حلقة الرسم
//  في Android/الـCLI) عدّاداً لقياس FPS الفعلي، ومُجمِّعاً (accumulator) لخطوة زمنية ثابتة —
//  النمط القياسي في محركات الألعاب لفصل الفيزياء المحدَّدة (deterministic) عن معدّل العرض
//  المتغيّر (انظر مقالة "Fix Your Timestep" الشهيرة لـGlenn Fiedler لخلفية كاملة عن النمط).

// ---- عدّاد FPS (FPS Meter) --------------------------------------------------------------
// يُهيّئ عدّاد FPS على محرّك mm. استدعِها مرة واحدة عند البدء، قبل أي استدعاء لـmm_fpsUpdate.
fun mm_fpsInit(mm) {
    mm["fps"] = { frames: 0, elapsed: 0, value: 0, totalFrames: 0 };
    return true;
}

// استدعِها مرة واحدة كل إطار حقيقي (frame)، مُمرِّراً لها dtSeconds = الزمن الفعلي المنقضي
// منذ الإطار السابق بالثواني (كما يقيسه المضيف: Choreographer على أندرويد، أو ساعة نظام
// التشغيل في الـCLI). تُراكِم المكتبة الإطارات والزمن ضمن نافذة MM_DEFAULT_FPS_WINDOW
// (نصف ثانية افتراضياً)، وعند اكتمال النافذة تُعيد حساب mm_fps() الحالي وتصفّر النافذة —
// هذا يمنع رقماً "مهتزاً" يتغيّر كل إطار، ويُعيد بدلاً منه قراءة مستقرة كل نصف ثانية.
// تتطلّب استدعاء mm_fpsInit أولاً؛ بلا تأثير (وتُعيد 0) إن لم يكن العدّاد مُهيَّأ بعد.
fun mm_fpsUpdate(mm, dtSeconds) {
    if (has(mm, "fps") == false) { return 0; }
    let fps = mm["fps"];
    fps["frames"] = fps["frames"] + 1;
    fps["elapsed"] = fps["elapsed"] + dtSeconds;
    fps["totalFrames"] = fps["totalFrames"] + 1;
    if (fps["elapsed"] >= MM_DEFAULT_FPS_WINDOW) {
        fps["value"] = fps["frames"] / fps["elapsed"];
        fps["frames"] = 0;
        fps["elapsed"] = 0;
    }
    return fps["value"];
}

// آخر قيمة FPS محسوبة (تتحدَّث كل نافذة MM_DEFAULT_FPS_WINDOW عبر mm_fpsUpdate) — 0 حتى
// اكتمال أول نافذة، أو إن لم يُستدعَ mm_fpsInit أصلاً
fun mm_fps(mm) {
    if (has(mm, "fps") == false) { return 0; }
    return mm["fps"]["value"];
}

// إجمالي عدد الإطارات المُعدودة منذ mm_fpsInit (تراكمي، لا يتأثر بتصفير النافذة الداخلية) —
// مفيد كعدّاد تشخيصي عام لعمر الجلسة
fun mm_fpsFrameCount(mm) {
    if (has(mm, "fps") == false) { return 0; }
    return mm["fps"]["totalFrames"];
}

// يُصفِّر عدّاد FPS بالكامل (بما فيه totalFrames وmm_fps الحالي) دون الحاجة لإعادة mm_fpsInit
fun mm_fpsReset(mm) {
    if (has(mm, "fps") == false) { return false; }
    mm["fps"] = { frames: 0, elapsed: 0, value: 0, totalFrames: 0 };
    return true;
}

// ---- خطوة زمنية ثابتة (Fixed Timestep Accumulator) -------------------------------------
// يُهيّئ مُجمِّعاً (accumulator) لخطوة زمنية ثابتة مقدارها fixedDt ثانية (مثلاً 1/60 لفيزياء
// بمعدّل 60 خطوة/ثانية بغضّ النظر عن معدّل عرض الشاشة الفعلي). استدعِها مرة واحدة عند البدء.
fun mm_fixedStepInit(mm, fixedDt) {
    mm["fixedStep"] = { fixedDt: fixedDt, accumulator: 0 };
    return true;
}

// نسبة الزمن المتراكم غير المُستهلَك بعد من fixedDt (بين 0 و1) — مفيدة لِـ"استكمال" (interpolate)
// موضع العرض بصرياً بين خطوتَي فيزياء متتاليتين بدل عرض حركة متقطّعة (jitter) على شاشات عالية
// التردد. تُعيد 0 إن لم يُستدعَ mm_fixedStepInit أصلاً.
fun mm_fixedStepAlpha(mm) {
    if (has(mm, "fixedStep") == false) { return 0; }
    let fs = mm["fixedStep"];
    let alpha = fs["accumulator"] / fs["fixedDt"];
    if (alpha > 1) { return 1; }
    if (alpha < 0) { return 0; }
    return alpha;
}

// يُراكِم الزمن الفعلي المنقضي realDtSeconds (منذ آخر إطار) على المُجمِّع، ثم يستدعي الدالة
// stepFn(fixedDt) بقدر ما يسمح المُجمِّع من خطوات ثابتة كاملة — بحد أقصى maxSteps خطوة في
// نفس الاستدعاء (يحمي من "دوّامة الموت" spiral of death عند تجمّد التطبيق للحظة، إذ يمنع
// محاولة تعويض كل الوقت الضائع دفعة واحدة). عادةً stepFn هي دالة تستدعي mm_tick/mm_tickTimers
// بنفسها بمعدّل ثابت. تُعيد عدد الخطوات المُنفَّذة فعلياً؛ لا تفعل شيئاً وتُعيد 0 إن لم يُستدعَ
// mm_fixedStepInit أصلاً.
//
// مثال:
//   mm_fixedStepInit(mm, 1 / 60);
//   fun onFixedStep(fixedDt) { mm_tick(mm, fixedDt, "bounce"); mm_tickTimers(mm, fixedDt); }
//   // كل إطار عرض (قد يكون معدّله متغيّراً):
//   mm_fixedStepRun(mm, realDtThisFrame, 5, onFixedStep);
fun mm_fixedStepRun(mm, realDtSeconds, maxSteps, stepFn) {
    if (has(mm, "fixedStep") == false) { return 0; }
    let fs = mm["fixedStep"];
    fs["accumulator"] = fs["accumulator"] + realDtSeconds;
    let steps = 0;
    while (fs["accumulator"] >= fs["fixedDt"] and steps < maxSteps) {
        stepFn(fs["fixedDt"]);
        fs["accumulator"] = fs["accumulator"] - fs["fixedDt"];
        steps = steps + 1;
    }
    return steps;
}


// ============================================================================
// 22) أحجام الشاشة/الصفحة والتصميم المتجاوب (Viewport / Responsive Sizing)
// ============================================================================
//  "الحدود" (bounds، القسم 6) تمنع القناع من مغادرة منطقة اللعب منطقياً — أما "الإطار
//  المرئي" (viewport) هنا فهو مفهوم مختلف: حجم شاشة/صفحة التطبيق الفعلي بالبكسل المنطقي،
//  يُستخدم للتموضع النسبي (٪ من الشاشة) وتحجيم القيم تناسبياً بين أحجام أجهزة مختلفة (هاتف
//  صغير مقابل تابلت)، بدل إحداثيات مطلقة تنكسر على شاشة بحجم مختلف عمّا صُمِّمت له.

// يُسجِّل حجم الشاشة/الصفحة الحالي (width×height بالبكسل المنطقي، كما يقيسه المضيف) على
// محرّك mm — استدعِها عند بدء التشغيل وكل مرة يتغيّر فيها الحجم (تدوير الجهاز مثلاً)
fun mm_setViewport(mm, width, height) {
    mm["viewport"] = { width: width, height: height };
    return true;
}

// حجم الشاشة/الصفحة الحالي المُسجَّل، أو نقاط الحجم الافتراضية (0,0) إن لم يُستدعَ
// mm_setViewport بعد
fun mm_viewport(mm) {
    if (has(mm, "viewport") == false) { return { width: 0, height: 0 }; }
    return mm["viewport"];
}

// يحوِّل نقطة مطلقة (x, y) إلى نسبة مئوية (0..1) من أبعاد الشاشة الحالية — مفيد لتخزين
// مواضع عناصر واجهة بصيغة تتكيّف تلقائياً مع أي حجم شاشة لاحقاً بدل إحداثيات ثابتة
fun mm_toViewportPercent(mm, x, y) {
    let vp = mm_viewport(mm);
    if (vp["width"] <= 0 or vp["height"] <= 0) { return mm_point(0, 0); }
    return mm_point(x / vp["width"], y / vp["height"]);
}

// عكس mm_toViewportPercent: يحوِّل نسبة مئوية (0..1) إلى نقطة مطلقة على أبعاد الشاشة
// الحالية — استخدمها لوضع عنصر عند "20% من العرض، 80% من الارتفاع" بغضّ النظر عن حجم الجهاز
fun mm_fromViewportPercent(mm, percentX, percentY) {
    let vp = mm_viewport(mm);
    return mm_point(percentX * vp["width"], percentY * vp["height"]);
}

// تصنيف تقريبي لحجم الشاشة الحالية إلى فئة نصية شائعة في التصميم المتجاوب: "compact"
// (هاتف عمودي، العرض أقل من 600)، "medium" (هاتف أفقي/تابلت صغير، أقل من 840)، أو
// "expanded" (تابلت كبير/سطح مكتب) — تُطابق حدود فئات Material Design 3 لسهولة اتخاذ قرارات
// تخطيط (كعدد أعمدة الشبكة أو حجم عناصر التحكم) دون كتابة أرقام سحرية متكرّرة يدوياً
fun mm_viewportClass(mm) {
    let vp = mm_viewport(mm);
    if (vp["width"] < 600) { return "compact"; }
    if (vp["width"] < 840) { return "medium"; }
    return "expanded";
}

// يُحجِّم قيمة baseValue (مصمَّمة أصلاً لعرض مرجعي baseWidth) تناسبياً مع عرض الشاشة الحالي
// — مثلاً سرعة/حجم قناع صُمِّم لعرض 400 نقطة يُكبَّر تلقائياً على شاشة أعرض بنفس النسبة،
// فيبدو التصرّف متسقاً بصرياً عبر أحجام أجهزة مختلفة بدل بدو أصغر/أسرع نسبياً على الشاشات
// الكبيرة. يُعيد baseValue بلا تغيير إن لم يُضبَط viewport بعد.
fun mm_scaleForViewport(mm, baseValue, baseWidth) {
    let vp = mm_viewport(mm);
    if (vp["width"] <= 0 or baseWidth <= 0) { return baseValue; }
    return baseValue * (vp["width"] / baseWidth);
}

// يُحرِّك (يُثبِّت مباشرة، بلا فيزياء) قناع name إلى موضع نسبي (percentX, percentY) من
// أبعاد الشاشة الحالية — مختصر لِـ mm_fromViewportPercent + mm_setPosition، مفيد لعناصر
// واجهة تلتصق بزاوية/مركز الشاشة (كزر تحكّم في الزاوية السفلى) بغضّ النظر عن حجمها
fun mm_setPositionPercent(mm, name, percentX, percentY) {
    let p = mm_fromViewportPercent(mm, percentX, percentY);
    return mm_setPosition(mm, name, p["x"], p["y"]);
}


// ============================================================================
// 23) أنواع شريط التحميل (Progress / Loading Bar Kinds)
// ============================================================================
//  أشرطة تقدُّم/تحميل مُسمّاة يديرها محرّك mm بمعزل عن الأقنعة (وإن أمكن ربطها بقناع لعرضه
//  بصرياً عبر Indsin). أربعة أنواع شائعة مدعومة عبر kind واحد:
//    "linear"       — شريط عادي محدَّد المدة (تحميل ملف، شريط صحة/طاقة)
//    "circular"     — نفس منطق linear رقمياً؛ الفرق بصري بحت عند الرسم (حلقي بدل مستطيل)
//    "indeterminate"— بلا مدة معروفة (انتظار استجابة خادم)؛ يتأرجح ذهاباً وإياباً بلا توقف
//    "segmented"    — بمراحل منفصلة معدودة (خطوات معالج، مهام مرحلة يومية)
//    "buffer"       — بقيمتين (المُشغَّل played والمُخزَّن مسبقاً buffered)، كشريط الفيديو

// ينشئ شريط تقدُّم باسم name من نوع kind ("linear"/"circular"/"indeterminate"/"segmented"/
// "buffer") بمدة duration (بالثواني أو أي وحدة زمن يستخدمها mm_progressTick لاحقاً؛
// تُتجاهَل duration لنوع "indeterminate" حيث تمثّل بدلاً منها مدة دورة تأرجح واحدة)
fun mm_progressCreate(mm, name, kind, duration) {
    if (has(mm, "progress") == false) { mm["progress"] = {}; }
    mm["progress"][name] = { kind: kind, value: 0, duration: duration, direction: 1, segments: 1, buffered: 0 };
    return true;
}

// (داخلية) يجلب سجل شريط تقدُّم أو nil إن لم يُنشَأ بهذا الاسم
fun mm__progressGet(mm, name) {
    if (has(mm, "progress") == false) { return nil; }
    if (has(mm["progress"], name) == false) { return nil; }
    return mm["progress"][name];
}

// يضبط قيمة شريط تقدُّم يدوياً مباشرة إلى value (0..1، تُحصَر ضمن هذا المدى تلقائياً) —
// مفيد لأشرطة تُحدَّث من مصدر خارجي (تقدّم تنزيل حقيقي) بدل عدّاد زمني داخلي عبر
// mm_progressTick
fun mm_progressSet(mm, name, value) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return false; }
    p["value"] = mm_clampNum(value, 0, 1);
    return true;
}

// القيمة الحالية لشريط تقدُّم (0..1)، أو 0 إن لم يُنشَأ بهذا الاسم
fun mm_progressGet(mm, name) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return 0; }
    return p["value"];
}

// يُقدِّم شريط تقدُّم تلقائياً بمقدار dt زمنياً وفق نوعه:
// - "linear"/"circular"/"segmented": القيمة += dt/duration، تُحصَر عند 1 (لا تتجاوزه)
// - "indeterminate": تتأرجح القيمة بين 0 و1 ذهاباً وإياباً باستمرار كل duration (ping-pong)
// - "buffer": القيمة (played) تتقدَّم كالخطي، لكن لا تتجاوز أبداً قيمة buffered المضبوطة
//   عبر mm_progressSetBuffer (كشريط فيديو لا يشغّل ما لم يُخزَّن مسبقاً)
fun mm_progressTick(mm, name, dt) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return 0; }
    if (p["kind"] == "indeterminate") {
        let step = (dt / p["duration"]) * p["direction"];
        p["value"] = p["value"] + step;
        if (p["value"] >= 1) { p["value"] = 1; p["direction"] = 0 - 1; }
        if (p["value"] <= 0) { p["value"] = 0; p["direction"] = 1; }
        return p["value"];
    }
    let next = p["value"] + (dt / p["duration"]);
    if (p["kind"] == "buffer" and next > p["buffered"]) { next = p["buffered"]; }
    p["value"] = mm_clampNum(next, 0, 1);
    return p["value"];
}

// يضبط عدد المراحل الكلي لشريط من نوع "segmented" (مثلاً 5 خطوات معالج تسجيل)
fun mm_progressSetSegments(mm, name, totalSegments) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return false; }
    p["segments"] = totalSegments;
    return true;
}

// عدد المراحل المكتملة فعلياً لشريط "segmented" بناءً على قيمته الحالية (0..segments)
fun mm_progressCompletedSegments(mm, name) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return 0; }
    return floor(p["value"] * p["segments"]);
}

// يضبط مقدار المخزَّن مسبقاً (buffered، 0..1) لشريط من نوع "buffer" — القيمة المُشغَّلة
// (mm_progressGet) لن تتجاوزه أبداً عبر mm_progressTick اللاحقة
fun mm_progressSetBuffer(mm, name, bufferedValue) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return false; }
    p["buffered"] = mm_clampNum(bufferedValue, 0, 1);
    return true;
}

// هل اكتمل شريط تقدُّم محدَّد المدة (value >= 1)؟ يُعيد false دوماً لنوع "indeterminate"
// (لا معنى للاكتمال هناك) وكذلك إن لم يُنشَأ الشريط أصلاً
fun mm_progressIsDone(mm, name) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return false; }
    if (p["kind"] == "indeterminate") { return false; }
    return p["value"] >= 1;
}

// لقطة مسطَّحة كاملة لشريط تقدُّم (نمط mm_warp من القسم 15) جاهزة للتغذية مباشرة لعنصر
// واجهة Indsin — تتضمّن دائماً kind/value/percent/done، بالإضافة إلى completedSegments/segments
// لنوع "segmented"، أو buffered لنوع "buffer"
fun mm_progressWarp(mm, name) {
    let p = mm__progressGet(mm, name);
    if (p == nil) { return nil; }
    let out = { kind: p["kind"], value: p["value"], percent: round(p["value"] * 100), done: mm_progressIsDone(mm, name) };
    if (p["kind"] == "segmented") {
        out["segments"] = p["segments"];
        out["completedSegments"] = mm_progressCompletedSegments(mm, name);
    }
    if (p["kind"] == "buffer") {
        out["buffered"] = p["buffered"];
    }
    return out;
}


// ============================================================================
// 24) اللمس وسلاسة الحركة (Touch Gestures & Motion Smoothing)
// ============================================================================
//  تفسير خام لأحداث لمس/نقر مضيفة (down/move/up) إلى بادرات (tap مقابل swipe) مخزَّنة في
//  meta كل قناع، بالإضافة لتنعيم حركة مستقل عن اللمس (اقتفاء هدف بسلاسة بدل انتقال مفاجئ).

// (داخلية) عتبة المسافة (بوحدة نقاط) التي تفصل tap عن swipe في mm_touchEnd
let MM_TAP_DISTANCE_THRESHOLD = 12;

// يبدأ تتبّع لمسة على قناع name عند نقطة (x, y) — استدعِها عند حدث "لمسة بدأت" من المضيف
fun mm_touchBegin(mm, name, x, y) {
    return mm_setMeta(mm, name, "touch", { active: true, startX: x, startY: y, curX: x, curY: y, distance: 0 });
}

// يُحدِّث موضع اللمسة الجارية إلى (x, y)، ويُراكِم المسافة المقطوعة منذ آخر تحديث (لتمييز
// tap عن swipe لاحقاً في mm_touchEnd) — بلا تأثير إن لم تبدأ لمسة على هذا القناع بعد
fun mm_touchMove(mm, name, x, y) {
    let touch = mm_getMeta(mm, name, "touch", nil);
    if (touch == nil or touch["active"] == false) { return false; }
    touch["distance"] = touch["distance"] + mm_distance(mm_point(touch["curX"], touch["curY"]), mm_point(x, y));
    touch["curX"] = x;
    touch["curY"] = y;
    return true;
}

// اختصار شائع لأنماط "السحب لتحريك": يُحدِّث موضع اللمسة عبر mm_touchMove ثم يُحرِّك
// القناع مباشرة إلى نفس نقطة اللمسة (drag-to-move بلا أي فيزياء وسيطة)
fun mm_touchDrag(mm, name, x, y) {
    mm_touchMove(mm, name, x, y);
    mm_setPosition(mm, name, x, y);
    mm_recordHistory(mm, name);
    return true;
}

// هل توجد لمسة جارية حالياً على هذا القناع؟
fun mm_isTouching(mm, name) {
    let touch = mm_getMeta(mm, name, "touch", nil);
    if (touch == nil) { return false; }
    return touch["active"];
}

// ينهي تتبّع اللمسة على قناع name، ويُصنِّف البادرة: إن كانت المسافة الكلية المقطوعة أقل
// من MM_TAP_DISTANCE_THRESHOLD تُصنَّف "tap"، وإلا "swipe" مع اتجاه ومسافة الخط المستقيم من
// البداية للنهاية (بصرف النظر عن مسار السحب الفعلي). يُعيد الخريطة {type, dx, dy, distance}
// (dx/dy تساويان 0 لِـ"tap")، ويمسح حالة اللمسة عن القناع. يُعيد نتيجة "tap" فارغة المسافة
// بلا مسح أي شيء إن لم تبدأ لمسة على هذا القناع أصلاً.
fun mm_touchEnd(mm, name) {
    let touch = mm_getMeta(mm, name, "touch", nil);
    if (touch == nil or touch["active"] == false) { return { type: "tap", dx: 0, dy: 0, distance: 0 }; }
    let result = { type: "tap", dx: 0, dy: 0, distance: touch["distance"] };
    if (touch["distance"] >= MM_TAP_DISTANCE_THRESHOLD) {
        result["type"] = "swipe";
        result["dx"] = touch["curX"] - touch["startX"];
        result["dy"] = touch["curY"] - touch["startY"];
    }
    touch["active"] = false;
    return result;
}

// ---- سلاسة الحركة (Motion Smoothing) ----------------------------------------------------
// تنعيم أُسّي مستقل عن معدّل الإطارات (framerate-independent exponential smoothing): يقترب
// القناع من هدفه بنسبة ثابتة من المسافة المتبقية كل ثانية بدل خطوة ثابتة كل إطار، فتبدو
// الحركة سلسة ومتّسقة السرعة النسبية بصرف النظر عن تفاوت معدّل الإطارات الفعلي.
// remainPerSecond (بين 0 و1): نسبة المسافة المتبقية بعد مرور ثانية كاملة — كلما اقترب من 0
// كانت المتابعة أسرع/ألصق بالهدف، وكلما اقترب من 1 كانت أبطأ/أكثر "تراخياً" (لزوجة أعلى).
fun mm_smoothFollow(mm, name, targetX, targetY, remainPerSecond, dt) {
    let here = mm_position(mm, name);
    if (here == nil) { return false; }
    let t = 1 - pow(remainPerSecond, dt);
    mm_setPosition(mm, name, here["x"] + (targetX - here["x"]) * t, here["y"] + (targetY - here["y"]) * t);
    mm_recordHistory(mm, name);
    return true;
}

// نفس منطق التنعيم الأُسّي أعلاه، لكن على متجه السرعة بدل الموضع مباشرة — مفيد لتنعيم
// استجابة عصا تحكّم افتراضية (القسم 26) بحيث لا تتغيّر سرعة القناع بقفزة مفاجئة عند تحريك
// العصا بسرعة، بل تنتقل بسلاسة نحو السرعة المستهدفة
fun mm_smoothVelocity(mm, name, targetVx, targetVy, remainPerSecond, dt) {
    let v = mm_velocity(mm, name);
    let t = 1 - pow(remainPerSecond, dt);
    return mm_setVelocity(mm, name, v["x"] + (targetVx - v["x"]) * t, v["y"] + (targetVy - v["y"]) * t);
}


// ============================================================================
// 25) العملات والنقاط القابلة للجمع (Coins / Collectibles / Score)
// ============================================================================
//  آلية "عملات" جاهزة فوق مفاهيم mm الأساسية: قناع عادي (mm_spawn) بوسم meta.coin، تُجمَع
//  تلقائياً عند اقتراب قناع "جامع" (لاعب) منها ضمن نصف قطر معيّن، مع عدّاد نقاط على مستوى
//  المحرّك بالكامل — نمط شائع في ألعاب المنصّات/الأركيد بلا كتابة منطق تصادم يدوي متكرّر.

// ينشئ عملة/قابلاً للجمع باسم name عند (x, y) بقيمة value نقطة — قناع عادي فعلياً (يعمل
// عليه mm_position/mm_setActive... إلخ كأي قناع) موسوم فقط بأنه "قابل للجمع"
fun mm_spawnCoin(mm, name, x, y, value) {
    mm_spawn(mm, name, x, y);
    mm_setMeta(mm, name, "coin", { value: value, collected: false });
    return true;
}

// هل هذا القناع عملة/قابل للجمع (أُنشئ عبر mm_spawnCoin)؟
fun mm_isCoin(mm, name) {
    return mm_getMeta(mm, name, "coin", nil) != nil;
}

// هل جُمعت هذه العملة بالفعل (عبر mm_collectCoinsNear)؟ false أيضاً إن لم تكن عملة أصلاً
fun mm_isCoinCollected(mm, name) {
    let coin = mm_getMeta(mm, name, "coin", nil);
    if (coin == nil) { return false; }
    return coin["collected"];
}

// يفحص كل العملات النشطة غير المجموعة بعد، ويجمع كل عملة تقع ضمن radius من موضع قناع
// collectorName: يُعلِّمها "مجموعة"، يُخفيها (mm_setActive إلى false)، يُضيف قيمتها لعدّاد
// النقاط الكلي عبر mm_addScore تلقائياً، ويُطلق حدث محرّك "coinCollected" لكل عملة
// (payload: {name, value, collector}). يُعيد {totalValue, names}: مجموع قيم ما جُمِع في هذا
// الاستدعاء وأسماء العملات المجموعة (مصفوفة فارغة إن لم تُجمَع أي عملة).
fun mm_collectCoinsNear(mm, collectorName, radius) {
    let collectorPos = mm_position(mm, collectorName);
    let totalValue = 0;
    let collectedNames = [];
    if (collectorPos == nil) { return { totalValue: totalValue, names: collectedNames }; }
    let i = 0;
    while (i < len(mm["order"])) {
        let name = mm["order"][i];
        if (mm_isCoin(mm, name) and mm_isCoinCollected(mm, name) == false and mm_isActive(mm, name)) {
            let coinPos = mm_position(mm, name);
            if (mm_distance(collectorPos, coinPos) <= radius) {
                let coin = mm_getMeta(mm, name, "coin", nil);
                coin["collected"] = true;
                mm_setActive(mm, name, false);
                totalValue = totalValue + coin["value"];
                push(collectedNames, name);
                mm__fire(mm, "coinCollected", { name: name, value: coin["value"], collector: collectorName });
            }
        }
        i = i + 1;
    }
    if (totalValue > 0) { mm_addScore(mm, totalValue); }
    return { totalValue: totalValue, names: collectedNames };
}

// ---- عدّاد نقاط عام على مستوى المحرّك (Score) --------------------------------------------
// يُضيف amount (قد تكون سالبة لخصم نقاط) إلى عدّاد نقاط محرّك mm الكلي، ويُطلق حدث محرّك
// "scoreChanged" (payload: {amount, total})
fun mm_addScore(mm, amount) {
    if (has(mm, "score") == false) { mm["score"] = 0; }
    mm["score"] = mm["score"] + amount;
    mm__fire(mm, "scoreChanged", { amount: amount, total: mm["score"] });
    return mm["score"];
}

// مجموع النقاط الحالي، أو 0 إن لم تُضَف أي نقطة بعد
fun mm_score(mm) {
    if (has(mm, "score") == false) { return 0; }
    return mm["score"];
}

// يُصفِّر عدّاد النقاط إلى 0 (بداية لعبة جديدة مثلاً)
fun mm_resetScore(mm) {
    mm["score"] = 0;
    return true;
}


// ============================================================================
// 26) أزرار التحكم الافتراضية (Virtual Joystick & Control Buttons)
// ============================================================================
//  عنصرا تحكّم لمسي جاهزان لأي لعبة/تطبيق تفاعلي: عصا تحكّم افتراضية مستمرّة (اتجاه+قوة)،
//  وأزرار منفصلة (اضغط/أفلت) بحالة "لحظة الضغط/الإفلات" القياسية (edge-triggered) لتفادي
//  تكرار تنفيذ فعل الزر في كل إطار طالما الإصبع لا يزال ضاغطاً عليه.

// ---- عصا تحكّم افتراضية (Virtual Joystick) ----------------------------------------------
// ينشئ عصا تحكّم افتراضية باسم name، مركزها (centerX, centerY) ونصف قطرها الأقصى maxRadius
// (بالنقاط) — هذا هو موضع/حجم "القرص" المرسوم على الشاشة (عادة زاوية سفلية ثابتة)
fun mm_joystickCreate(mm, name, centerX, centerY, maxRadius) {
    if (has(mm, "joysticks") == false) { mm["joysticks"] = {}; }
    mm["joysticks"][name] = { centerX: centerX, centerY: centerY, maxRadius: maxRadius, x: 0, y: 0, active: false };
    return true;
}

// يُحدِّث عصا تحكّم بموضع لمسة خام (touchX, touchY): يحسب المتجه من مركز العصا إلى نقطة
// اللمسة، يحصر مقداره ضمن maxRadius (لا يمكن سحب "المقبض" خارج حدود القرص)، ويخزّنه
// مُطبَّعاً بين -1 و1 على كل محور (0,0 يعني عصا في مركزها تماماً، 1 يعني أقصى انحراف)
fun mm_joystickUpdate(mm, name, touchX, touchY) {
    if (has(mm, "joysticks") == false or has(mm["joysticks"], name) == false) { return false; }
    let j = mm["joysticks"][name];
    let dx = touchX - j["centerX"];
    let dy = touchY - j["centerY"];
    let mag = sqrt(dx * dx + dy * dy);
    if (mag > j["maxRadius"]) {
        dx = (dx / mag) * j["maxRadius"];
        dy = (dy / mag) * j["maxRadius"];
    }
    j["x"] = dx / j["maxRadius"];
    j["y"] = dy / j["maxRadius"];
    j["active"] = true;
    return true;
}

// يُعيد عصا تحكّم إلى مركزها (يُستدعى عند رفع الإصبع عنها)
fun mm_joystickRelease(mm, name) {
    if (has(mm, "joysticks") == false or has(mm["joysticks"], name) == false) { return false; }
    let j = mm["joysticks"][name];
    j["x"] = 0;
    j["y"] = 0;
    j["active"] = false;
    return true;
}

// المتجه المُطبَّع الحالي لعصا تحكّم {x, y} (كل محور بين -1 و1)، أو نقطة صفرية إن لم تُنشَأ
fun mm_joystickVector(mm, name) {
    if (has(mm, "joysticks") == false or has(mm["joysticks"], name) == false) { return mm_point(0, 0); }
    let j = mm["joysticks"][name];
    return mm_point(j["x"], j["y"]);
}

// اختصار شائع: يقرأ متجه عصا تحكّم name ويضبط سرعة قناع maskName مباشرة إلى
// (المتجه × maxSpeed) — نمط "تحكّم ثنائي العصا" (twin-stick) الفوري بلا تنعيم؛ للتحكّم
// السلس استخدم mm_smoothVelocity على النتيجة بدلاً من ضبط السرعة مباشرة
fun mm_joystickApplyToVelocity(mm, name, maskName, maxSpeed) {
    let v = mm_joystickVector(mm, name);
    return mm_setVelocity(mm, maskName, v["x"] * maxSpeed, v["y"] * maxSpeed);
}

// ---- أزرار تحكّم منفصلة (Discrete Control Buttons) ---------------------------------------
// يُعرِّف زرَّ تحكّم باسم buttonName بحالة غير مضغوطة ابتدائية (مثل "jump" أو "attack")
fun mm_buttonDefine(mm, buttonName) {
    if (has(mm, "buttons") == false) { mm["buttons"] = {}; }
    mm["buttons"][buttonName] = { pressed: false, justPressed: false, justReleased: false };
    return true;
}

// يُسجِّل ضغطاً على زر (من حدث لمس "بدأ" على منطقة الزر في المضيف) — يضبط pressed وjustPressed
// معاً؛ استهلك justPressed عبر mm_buttonConsumeJustPressed لتفادي إعادة تنفيذ الفعل كل إطار
fun mm_buttonPress(mm, buttonName) {
    if (has(mm, "buttons") == false or has(mm["buttons"], buttonName) == false) { mm_buttonDefine(mm, buttonName); }
    let b = mm["buttons"][buttonName];
    b["pressed"] = true;
    b["justPressed"] = true;
    return true;
}

// يُسجِّل إفلات زر (من حدث لمس "انتهى")
fun mm_buttonRelease(mm, buttonName) {
    if (has(mm, "buttons") == false or has(mm["buttons"], buttonName) == false) { return false; }
    let b = mm["buttons"][buttonName];
    b["pressed"] = false;
    b["justReleased"] = true;
    return true;
}

// هل الزر مضغوط حالياً (سواء لحظة الضغط أو مُستمرّاً منذ إطارات سابقة)؟ مناسب لحركة
// مستمرّة طالما الإصبع فوق الزر (مثل "تحرّك يميناً")
fun mm_buttonIsPressed(mm, buttonName) {
    if (has(mm, "buttons") == false or has(mm["buttons"], buttonName) == false) { return false; }
    return mm["buttons"][buttonName]["pressed"];
}

// يُعيد true لمرة واحدة بالضبط عند لحظة الضغط الأولى، ثم يستهلك (يمسح) العلم فوراً —
// استدعِها مرة كل دورة لتنفيذ فعل "لمرة واحدة" (كالقفز) لا يتكرّر طالما الإصبع باقياً ضاغطاً
fun mm_buttonConsumeJustPressed(mm, buttonName) {
    if (has(mm, "buttons") == false or has(mm["buttons"], buttonName) == false) { return false; }
    let b = mm["buttons"][buttonName];
    let result = b["justPressed"];
    b["justPressed"] = false;
    return result;
}

// نظير mm_buttonConsumeJustPressed للحظة الإفلات — مفيد لأفعال تُنفَّذ عند رفع الإصبع
// (مثل "اشحن ثم أطلق" عند الإفلات)
fun mm_buttonConsumeJustReleased(mm, buttonName) {
    if (has(mm, "buttons") == false or has(mm["buttons"], buttonName) == false) { return false; }
    let b = mm["buttons"][buttonName];
    let result = b["justReleased"];
    b["justReleased"] = false;
    return result;
}

)MOVINGMASKOGRIN";
static const char* kLib_nlpkit_og_rin = R"NLPKITOGRIN(
// ============================================================================
//  lib/nlpkit.og.rin — معالجة نصوص متقدمة: NLP بدائي + تحليل ملفات/لغات
//  فوق نواة C++ الجديدة: utf8Len/utf8CharAt/utf8Substr/utf8Reverse/utf8ToArray،
//  arabicStripDiacritics/arabicNormalize/detectScript، levenshtein،
//  tokenizeWords/splitSentences، وعائلة regex* (regexTest/regexFind/regexFindAll/
//  regexGroups/regexReplace/regexSplit).
//
//  استيراد:
//    @import "lib/nlpkit.og.rin";
//    @import "lib/nlpkit.og.rin" as nlp;
//
//  ملاحظة: كل الدوال هنا تعمل على نقاط ترميز UTF-8 حقيقية (عبر النواة الجديدة)، فتبقى صحيحة
//  مع النصوص العربية/RTL، بخلاف بعض دوال strings.og.rin القديمة (charAt/substr/reverseStr)
//  التي تعمل على البايتات وتكسر مع أي حرف غير-ASCII.
// ============================================================================

// ---- قوائم كلمات وقف (stopwords) صغيرة مدمجة: عربي + إنجليزي — تُستخدم في keywordExtract ----
fun nlp_stopwordsAr() {
    return ["في","من","إلى","على","عن","مع","هذا","هذه","ذلك","تلك","الذي","التي","الذين",
        "و","أو","ثم","لكن","إن","أن","كان","كانت","يكون","لا","لم","لن","ما","هل","كل",
        "بعض","غير","بين","عند","قد","لقد","إذا","كما","حتى","أنا","أنت","هو","هي","نحن",
        "هم","له","لها","لهم","به","بها","فيه","فيها","هناك","هنا"];
}
fun nlp_stopwordsEn() {
    return ["the","a","an","and","or","but","if","of","to","in","on","for","with","at","by",
        "from","up","about","into","over","after","is","are","was","were","be","been","being",
        "this","that","these","those","it","its","as","not","no","so","than","then","there",
        "here","i","you","he","she","we","they","them","his","her","their","our","your"];
}

// هل word كلمة وقف بحسب lang ("ar" أو "en")؟ يقارن بعد lower/arabicNormalize حتى تُطابَق
// الكلمة العربية بصرف النظر عن شكل الألف/التشكيل.
fun nlp_isStopword(word, lang) {
    let normalized = arabicNormalize(lower(word));
    let list = nlp_stopwordsEn();
    if (lang == "ar") { list = nlp_stopwordsAr(); }
    let i = 0;
    while (i < len(list)) {
        if (arabicNormalize(lower(list[i])) == normalized) { return true; }
        i = i + 1;
    }
    return false;
}

// يزيل كلمات الوقف من مصفوفة كلمات (ناتجة عادةً عن tokenizeWords)
fun nlp_removeStopwords(tokens, lang) {
    let out = [];
    let i = 0;
    while (i < len(tokens)) {
        if (nlp_isStopword(tokens[i], lang) == false) { push(out, tokens[i]); }
        i = i + 1;
    }
    return out;
}

// ---- تكرار الكلمات (word frequency) ----
// يُعيد قاموساً: الكلمة (بعد lower + arabicNormalize لتوحيد الأشكال) -> عدد مرات ظهورها.
fun nlp_wordFrequency(s) {
    let words = tokenizeWords(s);
    let freq = {};
    let i = 0;
    while (i < len(words)) {
        let w = arabicNormalize(lower(words[i]));
        if (has(freq, w)) { freq[w] = freq[w] + 1; } else { freq[w] = 1; }
        i = i + 1;
    }
    return freq;
}

// ---- n-grams حرفية (character n-grams) — مبنية على نقاط ترميز UTF-8 حقيقية ----
// مفيدة كأساس بسيط لكشف اللغة/التشابه الضبابي دون الاعتماد على تقسيم الكلمات.
fun nlp_charNgrams(s, n) {
    let cps = utf8ToArray(s);
    let out = [];
    let i = 0;
    while (i + n <= len(cps)) {
        let g = "";
        let j = i;
        while (j < i + n) { g = g + cps[j]; j = j + 1; }
        push(out, g);
        i = i + 1;
    }
    return out;
}

// n-grams على مستوى الكلمات (تسلسل n كلمات متتابعة مفصولة بمسافة) — مفيدة لاستخراج عبارات
// شائعة (collocations) بدل كلمات مفردة فقط.
fun nlp_wordNgrams(s, n) {
    let words = tokenizeWords(s);
    let out = [];
    let i = 0;
    while (i + n <= len(words)) {
        let parts = [];
        let j = i;
        while (j < i + n) { push(parts, words[j]); j = j + 1; }
        push(out, join(parts, " "));
        i = i + 1;
    }
    return out;
}

// ---- استخراج كلمات مفتاحية (keyword extraction) ----
// تحليل تكراري بسيط: يُرمّز النص، يزيل كلمات الوقف، يحسب التكرار، ثم يعيد أعلى topN كلمة
// كمصفوفة من قواميس {word, count} مرتّبة تنازلياً حسب التكرار (فرز إدراج بسيط — بلا حاجة
// لدالة sortBy غير موجودة في النواة الأساسية).
fun nlp_keywordExtract(s, topN, lang) {
    let words = tokenizeWords(s);
    let filtered = nlp_removeStopwords(words, lang);
    let freq = {};
    let i = 0;
    while (i < len(filtered)) {
        let w = arabicNormalize(lower(filtered[i]));
        if (len(w) < 2) { i = i + 1; continue; } // يتجاهل الأحرف المفردة الضجيجية
        if (has(freq, w)) { freq[w] = freq[w] + 1; } else { freq[w] = 1; }
        i = i + 1;
    }
    let ks = keys(freq);
    let ranked = [];
    i = 0;
    while (i < len(ks)) {
        let entry = {};
        entry["word"] = ks[i];
        entry["count"] = freq[ks[i]];
        // إدراج entry في مكانه الصحيح داخل ranked (تنازلياً حسب count) — فرز إدراج O(n^2)
        // مقبول تماماً لعدد الكلمات المفتاحية النموذجي في نص واحد.
        let pos = 0;
        while (pos < len(ranked) and ranked[pos]["count"] >= entry["count"]) { pos = pos + 1; }
        let before = [];
        let after = [];
        let k = 0;
        while (k < pos) { push(before, ranked[k]); k = k + 1; }
        while (k < len(ranked)) { push(after, ranked[k]); k = k + 1; }
        ranked = before;
        push(ranked, entry);
        k = 0;
        while (k < len(after)) { push(ranked, after[k]); k = k + 1; }
        i = i + 1;
    }
    if (len(ranked) > topN) {
        let top = [];
        i = 0;
        while (i < topN) { push(top, ranked[i]); i = i + 1; }
        return top;
    }
    return ranked;
}

// ---- كشف لغة تقريبي (script + كلمات وقف) ----
// يعيد: "ar" (عربية غالبة) | "en" (لاتينية مع تراكب واضح مع كلمات وقف إنجليزية) |
// "latin" (لاتينية بلا تراكب كافٍ لتأكيد الإنجليزية تحديداً — قد تكون لغة لاتينية أخرى) |
// "mixed" (عربي+لاتيني معاً بنسب متقاربة) | "unknown" (نص فارغ أو بلا حروف).
fun nlp_detectLanguage(s) {
    let script = detectScript(s);
    if (script == "arabic") { return "ar"; }
    if (script == "empty" or script == "digits" or script == "other") { return "unknown"; }
    if (script == "mixed") { return "mixed"; }
    // script == "latin": نحاول تمييز الإنجليزية عبر نسبة تراكب كلمات الوقف الإنجليزية الشائعة
    let words = tokenizeWords(s);
    if (len(words) == 0) { return "unknown"; }
    let hits = 0;
    let i = 0;
    while (i < len(words)) {
        if (nlp_isStopword(words[i], "en")) { hits = hits + 1; }
        i = i + 1;
    }
    if (hits / len(words) >= 0.12) { return "en"; }
    return "latin";
}

// ---- تشابه نصوص (text similarity) ----
// 1 = متطابقان تماماً، 0 = بلا أي تشابه (مبني على مسافة Levenshtein على نقاط ترميز UTF-8).
fun nlp_textSimilarity(a, b) {
    let la = utf8Len(a);
    let lb = utf8Len(b);
    let maxLen = la;
    if (lb > maxLen) { maxLen = lb; }
    if (maxLen == 0) { return 1.0; }
    let dist = levenshtein(a, b);
    let sim = 1.0 - (dist / maxLen);
    if (sim < 0) { sim = 0; }
    return sim;
}

// تشابه Jaccard على مستوى الكلمات (بعد إزالة التكرار): |تقاطع| / |اتحاد| — مقياس مختلف عن
// Levenshtein، أنسب لمقارنة فقرات/جمل طويلة نسبياً حيث الترتيب أقل أهمية من مجرد وجود الكلمات.
fun nlp_jaccardSimilarity(a, b) {
    let wa = nlp_wordFrequency(a);
    let wb = nlp_wordFrequency(b);
    let ka = keys(wa);
    let kb = keys(wb);
    let inter = 0;
    let i = 0;
    while (i < len(ka)) {
        if (has(wb, ka[i])) { inter = inter + 1; }
        i = i + 1;
    }
    let unionCount = len(ka) + len(kb) - inter;
    if (unionCount == 0) { return 1.0; }
    return inter / unionCount;
}

// أقرب سلسلة إلى query داخل candidates حسب Levenshtein (مطابقة ضبابية / تصحيح إملائي تقريبي)؛
// يُعيد قاموساً {match, distance}، أو {match: nil, distance: -1} إن كانت candidates فارغة.
fun nlp_fuzzyClosest(query, candidates) {
    let best = "";
    let bestDist = -1;
    let i = 0;
    while (i < len(candidates)) {
        let d = levenshtein(query, candidates[i]);
        if (bestDist == -1 or d < bestDist) { bestDist = d; best = candidates[i]; }
        i = i + 1;
    }
    let result = {};
    if (bestDist == -1) { result["match"] = nil; } else { result["match"] = best; }
    result["distance"] = bestDist;
    return result;
}

// ---- إحصاءات/تحليل نص عام ----
// يعيد قاموساً بإحصاءات نص واحد: عدد نقاط الترميز، عدد البايتات الخام، عدد الكلمات، عدد
// الجمل، متوسط طول الكلمة (بنقاط الترميز)، ونوع الكتابة الغالب (عبر detectScript).
fun nlp_analyzeText(s) {
    let words = tokenizeWords(s);
    let sentences = splitSentences(s);
    let totalWordChars = 0;
    let i = 0;
    while (i < len(words)) { totalWordChars = totalWordChars + utf8Len(words[i]); i = i + 1; }
    let avgWordLen = 0;
    if (len(words) > 0) { avgWordLen = totalWordChars / len(words); }
    let result = {};
    result["charsUtf8"] = utf8Len(s);
    result["charsBytes"] = len(s);
    result["words"] = len(words);
    result["sentences"] = len(sentences);
    result["avgWordLen"] = avgWordLen;
    result["script"] = detectScript(s);
    result["language"] = nlp_detectLanguage(s);
    return result;
}

// هل s ينتهي بـ suffix؟ (نسخة محلية صغيرة كي تبقى هذه المكتبة مستقلة بلا اعتماد على
// @import "lib/strings.og.rin" — انظر endsWith هناك لنسخة عامة الغرض إن كانت مستوردة أصلاً)
fun nlp_endsWith(s, suffix) {
    let sl = len(s);
    let pl = len(suffix);
    if (pl > sl) { return false; }
    return substr(s, sl - pl, pl) == suffix;
}

// ---- تحليل ملفات: تخمين لغة برمجة/ملف من امتداده ----
// جدول امتدادات شائع؛ يعيد اسم اللغة كنص، أو "unknown" إن لم يُعرف الامتداد. لا تفتح الملف
// فعلياً (لا تحتاج قراءة محتواه) — انظر nlp_analyzeFile أدناه لتحليل يقرأ المحتوى أيضاً.
fun nlp_detectFileLanguage(path) {
    let lower_path = lower(path);
    let table = {};
    table[".rin"] = "Rin"; table[".og.rin"] = "Rin";
    table[".kt"] = "Kotlin"; table[".kts"] = "Kotlin";
    table[".java"] = "Java";
    table[".py"] = "Python";
    table[".js"] = "JavaScript"; table[".mjs"] = "JavaScript";
    table[".ts"] = "TypeScript"; table[".tsx"] = "TypeScript (JSX)";
    table[".jsx"] = "JavaScript (JSX)";
    table[".c"] = "C";
    table[".h"] = "C/C++ Header";
    table[".cpp"] = "C++"; table[".cc"] = "C++"; table[".cxx"] = "C++"; table[".hpp"] = "C++ Header";
    table[".cs"] = "C#";
    table[".go"] = "Go";
    table[".rs"] = "Rust";
    table[".rb"] = "Ruby";
    table[".php"] = "PHP";
    table[".swift"] = "Swift";
    table[".m"] = "Objective-C";
    table[".html"] = "HTML"; table[".htm"] = "HTML";
    table[".css"] = "CSS";
    table[".xml"] = "XML";
    table[".json"] = "JSON";
    table[".yaml"] = "YAML"; table[".yml"] = "YAML";
    table[".toml"] = "TOML";
    table[".md"] = "Markdown";
    table[".sh"] = "Shell"; table[".bash"] = "Shell";
    table[".sql"] = "SQL";
    table[".gradle"] = "Gradle";
    let ks = keys(table);
    let bestExt = "";
    let i = 0;
    while (i < len(ks)) {
        let ext = ks[i];
        if (nlp_endsWith(lower_path, ext) and len(ext) > len(bestExt)) { bestExt = ext; }
        i = i + 1;
    }
    if (bestExt == "") { return "unknown"; }
    return table[bestExt];
}

// يقرأ ملفاً نصياً فعلياً (عبر readFile) ويحلّله: كل حقول nlp_analyzeText بالإضافة إلى
// {path, language, lines, blankLines}. يرمي نفس خطأ readFile إن تعذّرت القراءة.
fun nlp_analyzeFile(path) {
    let content = readFile(path);
    let result = nlp_analyzeText(content);
    result["path"] = path;
    result["language"] = nlp_detectFileLanguage(path);
    let rawLines = split(content, "\n");
    result["lines"] = len(rawLines);
    let blanks = 0;
    let i = 0;
    while (i < len(rawLines)) {
        if (trim(rawLines[i]) == "") { blanks = blanks + 1; }
        i = i + 1;
    }
    result["blankLines"] = blanks;
    return result;
}
)NLPKITOGRIN";
static const char* kLib_syskit_og_rin = R"SYSKITOGRIN(
// ============================================================================
//  lib/syskit.og.rin — عدّة نظام (System Kit): معلومات المحرّك + مسارات +
//                       ملفات آمنة + إعدادات دائمة (JSON) + سجلّ + فحوصات
//  استيراد:
//    @import "lib/syskit.og.rin";
//    @import "lib/syskit.og.rin" as sys;
//
//  مكتبة Rin خالصة فوق natives الأساسية الموجودة فعلاً في المفسّر
//  (readFile/writeFile/appendFile/deleteFile/fileExists/rinVersion/rinEdition/
//   jsonEncode/jsonDecode/split/join/trim/substr/indexOf/has/keys...) — بلا أي
//  تعديل على محرّك C++. الهدف: كل ما يحتاجه سكربت "نظامي" (يقرأ/يكتب ملفات،
//  يحفظ إعداداته، يسجّل أحداثه، ويتحقّق من حالته) بأسلوب {ok:true/value:...}
//  أو {ok:false/error:...} بدل توقّف مفاجئ بخطأ، بنفس اتفاقية opskit/envkit.
//
//  ملاحظة لغوية: لا تملك Rin عامل نفي "!" ولا كلمة "not"؛ النفي دائماً
//  بمقارنة صريحة x == false (كبقية مكتبات lib/*.og.rin).
//
//  مثال سريع:
//    let info = sysInfo();                         // {version:"...", edition:"..."}
//    let cfg  = sysConfigLoad("app.cfg.json", {theme:"dark"});
//    cfg = sysConfigSet(cfg, "lang", "ar");
//    sysConfigSave("app.cfg.json", cfg);
//    sysLogInfo("app.log", "بدأ التطبيق");
// ============================================================================

// ---- 1) معلومات النظام/المحرّك (Engine / system info) -----------------------

// {version: rinVersion(), edition: rinEdition()} — لعرضها في واجهة سكربتك
fun sysInfo() {
    return { version: rinVersion(), edition: rinEdition() };
}

// نص جاهز للعرض: "Rin v<version> (<edition>)"
fun sysVersionString() {
    return "Rin v" + rinVersion() + " (" + rinEdition() + ")";
}

// تحقّق بسيط: هل إصدار المحرّك الحالي يساوي النص expected بالضبط؟
fun sysIsVersion(expected) {
    return rinVersion() == expected;
}

// ---- 2) مسارات الملفات (Path utilities) — نصوص خالصة، بلا لمس القرص --------

// يدمج جزأي مسار بفاصل "/" واحد بالضبط بينهما (يتعامل مع "/" الزائدة في a أو الناقصة)
fun pathJoin(a, b) {
    if (a == "") { return b; }
    if (b == "") { return a; }
    let left = a;
    if (substr(left, len(left) - 1, 1) == "/") {
        left = substr(left, 0, len(left) - 1);
    }
    let right = b;
    if (substr(right, 0, 1) == "/") {
        right = substr(right, 1, len(right) - 1);
    }
    return left + "/" + right;
}

// يدمج عدّة أجزاء دفعة واحدة: pathJoinAll(["a", "b", "c.txt"]) -> "a/b/c.txt"
fun pathJoinAll(parts) {
    if (len(parts) == 0) { return ""; }
    let result = parts[0];
    let i = 1;
    while (i < len(parts)) {
        result = pathJoin(result, parts[i]);
        i = i + 1;
    }
    return result;
}

// آخر جزء في المسار (اسم الملف أو المجلد الأخير): "a/b/c.txt" -> "c.txt"
fun pathBaseName(p) {
    let idx = lastIndexOfChar(p, "/");
    if (idx == -1) { return p; }
    return substr(p, idx + 1, len(p) - idx - 1);
}

// كل ما قبل آخر "/": "a/b/c.txt" -> "a/b"، وإن لم يوجد "/" يُعيد "."
fun pathDirName(p) {
    let idx = lastIndexOfChar(p, "/");
    if (idx == -1) { return "."; }
    if (idx == 0) { return "/"; }
    return substr(p, 0, idx);
}

// الامتداد بلا نقطة: "a/b/c.txt" -> "txt"، وبلا امتداد -> ""
fun pathExtension(p) {
    let base = pathBaseName(p);
    let idx = lastIndexOfChar(base, ".");
    if (idx == -1) { return ""; }
    if (idx == 0) { return ""; }
    return substr(base, idx + 1, len(base) - idx - 1);
}

// اسم الملف بلا امتداد: "a/b/c.txt" -> "c"
fun pathStem(p) {
    let base = pathBaseName(p);
    let idx = lastIndexOfChar(base, ".");
    if (idx <= 0) { return base; }
    return substr(base, 0, idx);
}

// هل المسار مطلق (يبدأ بـ "/")؟
fun pathIsAbsolute(p) {
    return substr(p, 0, 1) == "/";
}

// يبسّط مساراً: يحذف الأجزاء الفارغة و"." ويُنفّذ ".." (صعود مجلد) نصّياً بحت
// (بلا لمس القرص فعلياً)، ويحافظ على "/" الأولى إن كان المسار مطلقاً
fun pathNormalize(p) {
    let absolute = pathIsAbsolute(p);
    let rawParts = split(p, "/");
    let stack = [];
    let i = 0;
    while (i < len(rawParts)) {
        let part = rawParts[i];
        if (part != "" and part != ".") {
            if (part == ".." and len(stack) > 0 and stack[len(stack) - 1] != "..") {
                pop(stack);
            } else {
                push(stack, part);
            }
        }
        i = i + 1;
    }
    let joined = join(stack, "/");
    if (absolute) { return "/" + joined; }
    if (joined == "") { return "."; }
    return joined;
}

// موقع آخر ظهور لمحرف ch داخل s (مساعد داخلي لدوال المسارات أعلاه)، -1 إن لم يوجد
fun lastIndexOfChar(s, ch) {
    let last = -1;
    let i = 0;
    while (i < len(s)) {
        if (substr(s, i, 1) == ch) { last = i; }
        i = i + 1;
    }
    return last;
}

// ---- 3) ملفات آمنة (Safe file ops) — لا تتوقّف بخطأ عند غياب الملف --------

// قراءة آمنة: {ok:true, value:"..."} أو {ok:false, error:"..."} بدل توقّف readFile الخام
fun fileRead(path) {
    if (fileExists(path) == false) {
        return { ok: false, error: "fileRead: الملف غير موجود: " + path };
    }
    return { ok: true, value: readFile(path) };
}

// كتابة آمنة (تنشئ الملف أو تستبدل محتواه بالكامل)
fun fileWrite(path, content) {
    writeFile(path, content);
    return { ok: true, path: path };
}

// إلحاق نص بنهاية ملف (يُنشئه إن لم يكن موجوداً)
fun fileAppend(path, content) {
    appendFile(path, content);
    return { ok: true, path: path };
}

// حذف آمن: لا يفشل إن كان الملف غير موجود أصلاً
fun fileDelete(path) {
    if (fileExists(path) == false) {
        return { ok: true, path: path, existed: false };
    }
    deleteFile(path);
    return { ok: true, path: path, existed: true };
}

// يضمن وجود الملف: ينشئه بمحتوى defaultContent إن لم يكن موجوداً، ولا يلمسه إن كان موجوداً
fun fileEnsure(path, defaultContent) {
    if (fileExists(path)) {
        return { ok: true, created: false };
    }
    writeFile(path, defaultContent);
    return { ok: true, created: true };
}

// نسخ ملف كامل (نصّي) من src إلى dest
fun sysCopyFile(src, dest) {
    if (fileExists(src) == false) {
        return { ok: false, error: "sysCopyFile: الملف غير موجود: " + src };
    }
    writeFile(dest, readFile(src));
    return { ok: true, from: src, to: dest };
}

// يقرأ ملفاً كمصفوفة أسطر (يقبل نهايات "\n" أو "\r\n")
fun fileReadLines(path) {
    let res = fileRead(path);
    if (res["ok"] == false) { return res; }
    let normalized = replace(res["value"], "\r\n", "\n");
    if (normalized == "") { return { ok: true, value: [] }; }
    return { ok: true, value: split(normalized, "\n") };
}

// يكتب مصفوفة أسطر كملف واحد مفصول بـ "\n"
fun fileWriteLines(path, lines) {
    return fileWrite(path, join(lines, "\n"));
}

// يلحق سطراً واحداً (مع "\n" تلقائياً) بنهاية ملف
fun fileAppendLine(path, line) {
    return fileAppend(path, line + "\n");
}

// حجم محتوى الملف بعدد المحارف (تقريبي بحسب len على النص المقروء)
fun fileSizeChars(path) {
    let res = fileRead(path);
    if (res["ok"] == false) { return res; }
    return { ok: true, value: len(res["value"]) };
}

// ---- 4) إعدادات نظام دائمة (Persistent JSON-backed system config) ---------

// يحمّل إعدادات من ملف JSON؛ إن لم يكن الملف موجوداً أو كان تالفاً يُعيد defaults كما هي
fun sysConfigLoad(path, defaults) {
    if (fileExists(path) == false) { return defaults; }
    let parsed = jsonDecode(readFile(path));
    if (parsed == nil) { return defaults; }
    return parsed;
}

// يحفظ خريطة إعدادات كاملة إلى ملف JSON
fun sysConfigSave(path, cfg) {
    writeFile(path, jsonEncode(cfg));
    return { ok: true, path: path };
}

// قراءة مفتاح من خريطة إعدادات في الذاكرة، بقيمة افتراضية عند غيابه
fun sysConfigGet(cfg, key, fallback) {
    if (has(cfg, key) == false) { return fallback; }
    return cfg[key];
}

// تعديل مفتاح في خريطة إعدادات في الذاكرة (لا يكتب للقرص، فقط يُعيد الخريطة المعدَّلة)
fun sysConfigSet(cfg, key, value) {
    cfg[key] = value;
    return cfg;
}

// دورة كاملة: تحميل من القرص -> تعديل مفتاح واحد -> حفظ فوري -> إعادة الخريطة الناتجة
fun sysConfigUpdate(path, key, value, defaults) {
    let cfg = sysConfigLoad(path, defaults);
    cfg = sysConfigSet(cfg, key, value);
    sysConfigSave(path, cfg);
    return cfg;
}

// ---- 5) سجلّ نظام بسيط (Simple system log) ---------------------------------

// يلحق سطر سجلّ منسّق "[LEVEL] message" بملف السجلّ (يُنشئه إن لم يوجد)
fun sysLog(path, level, message) {
    return fileAppendLine(path, "[" + level + "] " + message);
}

fun sysLogInfo(path, message) {
    return sysLog(path, "INFO", message);
}

fun sysLogWarn(path, message) {
    return sysLog(path, "WARN", message);
}

fun sysLogError(path, message) {
    return sysLog(path, "ERROR", message);
}

// يفرّغ ملف السجلّ (أو يُنشئه فارغاً إن لم يكن موجوداً)
fun sysLogClear(path) {
    return fileWrite(path, "");
}

// يقرأ كل أسطر السجلّ كمصفوفة نصوص؛ مصفوفة فارغة إن لم يوجد الملف أو كان فارغاً
fun sysLogRead(path) {
    if (fileExists(path) == false) { return []; }
    let res = fileReadLines(path);
    if (res["ok"] == false) { return []; }
    return res["value"];
}

// ---- 6) فحوصات وحراسات نظام (System guards) --------------------------------
// كلها بأسلوب {ok:.., error:..} الموحَّد بدل رمي أخطاء غامضة منتصف سكربت طويل.

// تحقّق عام: يُعيد {ok:true} إن كان cond صحيحاً، وإلا {ok:false, error:message}
fun sysAssert(cond, message) {
    if (cond) { return { ok: true }; }
    return { ok: false, error: message };
}

// يتحقّق أن ملفاً موجوداً فعلاً قبل استخدامه
fun sysRequireFile(path) {
    if (fileExists(path)) { return { ok: true, path: path }; }
    return { ok: false, error: "sysRequireFile: الملف غير موجود: " + path };
}

// يشغّل قائمة فحوصات (مصفوفة نتائج بصيغة {ok,...} كالتي تُعيدها الدوال أعلاه)
// ويُعيد أول فحص فاشل، أو {ok:true} إن نجحت كلها
fun sysCheckAll(checks) {
    let i = 0;
    while (i < len(checks)) {
        if (checks[i]["ok"] == false) { return checks[i]; }
        i = i + 1;
    }
    return { ok: true };
}
)SYSKITOGRIN";
static const char* kLib_requirekit_og_rin = R"REQUIREKITOGRIN(
// ============================================================================
//  lib/requirekit.og.rin — عدّة الحقول والاشتراطات الإلزامية (Mandatory / Required Kit)
//  استيراد:
//    @import "lib/requirekit.og.rin";
//    @import "lib/requirekit.og.rin" as require;
//
//  مكتبة Rin خالصة (بلا أي تعديل على محرّك C++) للتحقّق من "الإلزامية": حقول
//  إلزامية في خريطة (نموذج/طلب/كائن)، مجموعات إلزامية شرطية (أحدها على الأقل /
//  واحد فقط منها)، واشتراطات عامة بأسلوب "Design by Contract" (require/ensure).
//  كلها بنفس اتفاقية {ok:true/...} أو {ok:false, error/errors:...} المستخدمة في
//  validate.og.rin/opskit.og.rin/syskit.og.rin — بلا توقّف مفاجئ بخطأ أبداً.
//
//  الفارق عن lib/validate.og.rin: تلك تتحقق من *شكل* قيمة مفردة (بريد صالح؟
//  رقم صالح؟)، بينما هذه تتحقق من *وجود/إلزامية* حقول كاملة داخل خريطة، وتُجمِّع
//  كل الحقول/الاشتراطات الناقصة دفعة واحدة بدل التوقف عند أول خطأ — مفيد لعرض
//  كل مشاكل نموذج إدخال للمستخدم مرة واحدة بدل رسالة واحدة في كل مرة.
//
//  مثال سريع:
//    let form = { name: "ريما", email: "" };
//    let r = requireNonEmptyFields(form, ["name", "email", "phone"]);
//    print r;  // {ok:false, missing:["email","phone"], error:"..."}
//
//    let checks = [
//        requireThat(len(form["name"]) > 0, "الاسم مطلوب"),
//        requireInRange(17, 18, 99, "العمر")
//    ];
//    print requireAll(checks);  // {ok:false, errors:["العمر: يجب أن يكون بين 18 و99"]}
// ============================================================================

// هل القيمة "مفقودة" فعلياً (nil أو نص فارغ)؟ مساعد داخلي لهذه المكتبة تحديداً
// (كل مكتبة lib/*.og.rin قائمة بذاتها، بلا اعتماد متبادل على مكتبات أخرى)
fun rq_isMissing(v) {
    if (v == nil) { return true; }
    if (v == "") { return true; }
    return false;
}

// ---- 1) حقل إلزامي واحد على خريطة (map/object/form) ------------------------

// هل المفتاح key موجود أصلاً في obj (بصرف النظر عن قيمته، حتى لو فارغة)؟
fun requireField(obj, key) {
    if (has(obj, key) == false) {
        return { ok: false, error: "requireField: الحقل الإلزامي غير موجود: " + key };
    }
    return { ok: true, value: obj[key] };
}

// هل المفتاح موجود *وله قيمة فعلية* (ليست nil ولا نصاً فارغاً)؟
fun requireNonEmptyField(obj, key) {
    if (has(obj, key) == false) {
        return { ok: false, error: "requireNonEmptyField: الحقل الإلزامي غير موجود: " + key };
    }
    if (rq_isMissing(obj[key])) {
        return { ok: false, error: "requireNonEmptyField: الحقل الإلزامي فارغ: " + key };
    }
    return { ok: true, value: obj[key] };
}

// ---- 2) دفعة حقول إلزامية — تجمع كل الحقول الناقصة دفعة واحدة --------------

// keysArr = ["name", "email", ...] — يُعيد كل المفاتيح غير الموجودة أصلاً في obj
fun requireFields(obj, keysArr) {
    let missing = [];
    let i = 0;
    while (i < len(keysArr)) {
        if (has(obj, keysArr[i]) == false) { push(missing, keysArr[i]); }
        i = i + 1;
    }
    if (len(missing) == 0) { return { ok: true }; }
    return { ok: false, missing: missing, error: "requireFields: حقول إلزامية ناقصة: " + join(missing, "، ") };
}

// كـrequireFields لكن تعتبر القيمة الفارغة (nil/"") ناقصة أيضاً، لا وجود المفتاح فقط
fun requireNonEmptyFields(obj, keysArr) {
    let missing = [];
    let i = 0;
    while (i < len(keysArr)) {
        let k = keysArr[i];
        if (has(obj, k) == false or rq_isMissing(obj[k])) { push(missing, k); }
        i = i + 1;
    }
    if (len(missing) == 0) { return { ok: true }; }
    return { ok: false, missing: missing, error: "requireNonEmptyFields: حقول إلزامية فارغة أو ناقصة: " + join(missing, "، ") };
}

// ---- 3) مجموعات إلزامية شرطية (اختيار واحد بين عدّة حقول) ------------------

// يكفي أن يكون حقل واحد على الأقل من keysArr موجوداً وله قيمة (مثال: هاتف أو بريد)
fun requireAtLeastOne(obj, keysArr) {
    let i = 0;
    while (i < len(keysArr)) {
        let k = keysArr[i];
        if (has(obj, k) and rq_isMissing(obj[k]) == false) { return { ok: true, matched: k }; }
        i = i + 1;
    }
    return { ok: false, error: "requireAtLeastOne: يجب توفير أحد الحقول التالية على الأقل: " + join(keysArr, "، ") };
}

// حقل واحد فقط بالضبط من keysArr يجب أن يكون موجوداً وله قيمة (حقول متعارضة/متبادلة)
fun requireExactlyOne(obj, keysArr) {
    let present = [];
    let i = 0;
    while (i < len(keysArr)) {
        let k = keysArr[i];
        if (has(obj, k) and rq_isMissing(obj[k]) == false) { push(present, k); }
        i = i + 1;
    }
    if (len(present) == 1) { return { ok: true, matched: present[0] }; }
    if (len(present) == 0) {
        return { ok: false, error: "requireExactlyOne: يجب توفير أحد الحقول التالية: " + join(keysArr, "، ") };
    }
    return { ok: false, error: "requireExactlyOne: حقول متعارضة، يُسمح بواحد فقط من: " + join(present, "، ") };
}

// ---- 4) اشتراطات عامة على قيمة مفردة (بأسلوب Design by Contract) ----------

// اشتراط عام: يُعيد {ok:true} إن كان cond صحيحاً، وإلا {ok:false, error:message}
fun requireThat(cond, message) {
    if (cond) { return { ok: true }; }
    return { ok: false, error: message };
}

// يشترط أن تقع value بين minVal وmaxVal (بما فيهما)
fun requireInRange(value, minVal, maxVal, fieldName) {
    if (value >= minVal and value <= maxVal) { return { ok: true }; }
    return { ok: false, error: fieldName + ": يجب أن يكون بين " + toString(minVal) + " و" + toString(maxVal) };
}

// يشترط أن تكون value إحدى القيم المسموحة في allowed (مصفوفة)
fun requireOneOf(value, allowed, fieldName) {
    if (contains(allowed, value)) { return { ok: true }; }
    return { ok: false, error: fieldName + ": قيمة غير مسموحة (" + toString(value) + ")" };
}

// يشترط ألا يقل طول value (نص أو مصفوفة) عن minLen
fun requireMinLen(value, minLen, fieldName) {
    if (len(value) >= minLen) { return { ok: true }; }
    return { ok: false, error: fieldName + ": طول أقل من الحد الأدنى المطلوب (" + toString(minLen) + ")" };
}

// يشترط ألا يزيد طول value (نص أو مصفوفة) عن maxLen
fun requireMaxLen(value, maxLen, fieldName) {
    if (len(value) <= maxLen) { return { ok: true }; }
    return { ok: false, error: fieldName + ": طول أكبر من الحد الأقصى المسموح (" + toString(maxLen) + ")" };
}

// ---- 5) تجميع عدّة اشتراطات معاً --------------------------------------------

// checks = مصفوفة نتائج {ok,...} (كالتي تُعيدها الدوال أعلاه) — يجمع كل رسائل
// الفشل دفعة واحدة بدل التوقف عند أول خطأ؛ مثالي لعرض كل أخطاء نموذج معاً
fun requireAll(checks) {
    let errors = [];
    let i = 0;
    while (i < len(checks)) {
        if (checks[i]["ok"] == false) { push(errors, checks[i]["error"]); }
        i = i + 1;
    }
    if (len(errors) == 0) { return { ok: true }; }
    return { ok: false, errors: errors };
}

// كـrequireAll لكن يتوقّف عند أول اشتراط فاشل ويُعيده مباشرة (أسرع حين يكفيك أول خطأ فقط)
fun requireFirstFailure(checks) {
    let i = 0;
    while (i < len(checks)) {
        if (checks[i]["ok"] == false) { return checks[i]; }
        i = i + 1;
    }
    return { ok: true };
}
)REQUIREKITOGRIN";
static const char* kLib_passkit_og_rin = R"PASSKITOGRIN(
// ============================================================================
//  lib/passkit.og.rin — Password kit (Password Kit): concepts + variables + functions
//  Import:
//    @import "lib/passkit.og.rin";
//    @import "lib/passkit.og.rin" as pass;
//
//  Pure Rin library (with no change to the C++ engine) built on natives that already exist
//  (sec.randomToken / sec.sha256 / sec.hmacSha256 / sec.constantTimeEqual).
//  Every function returns {ok:true,...} or {ok:false, error/errors:...} using the same convention as
//  validate / requirekit / syskit — with no sudden stop on errors.
//
//  Concepts (Concepts) it covers:
//    1) Constants and variables: character sets, the common-password list, strength labels
//    2) Policy (Policy)     : mergeable rules + 4 ready-made policies (pkPolicy*)
//    3) Analysis and strength: entropy, weak patterns, guess time, feedback
//    4) Secure generation: password / PIN / passphrase (passphrase) with secure randomness
//    5) Storage: salted hash + key stretching (key stretching) + constant-time verification
//    6) Lifecycle: password record, history, expiry, lockout after attempts
//    7) Recovery: temporary reset tokens
//    8) Masking: display mask + redaction (redact) for logs
//
//  Quick example:
//    let p = pkPolicyStandard();
//    let r = pkCheck("abc12345", p, { username: "Rima" });
//    print r["errors"];
//    let g = pkGenerate({ length: 18 });
//    let h = pkHash(g["password"], {});
//    print pkVerify(g["password"], h);   // true
//
//  Honest security notes:
//    - Time: Rin has no wall clock (now() a monotonic clock), so every lifecycle function
//      takes nowSec (seconds, e.g. an epoch from your app) explicitly; the default pkNow() is fine
//      for a single session only and is not kept between runs.
//    - pkHash uses PBKDF2-HMAC-SHA256 (RFC 8018, native engine primitive, 600,000 iterations by default,
//      the OWASP 2023 minimum for this algorithm) in the format pk2$<iterations>$<salt>$<hash>.
//      Old pk1 hashes still verify and pkNeedsRehash flags them. PBKDF2 is NIST-approved but is not
//      memory-hard: bcrypt/scrypt/argon2 resist GPU cracking better, so for a high-risk server prefer those.
// ============================================================================

// ---- 1) Constants and variables --------------------------------------------------

let PK_LOWER = "abcdefghijklmnopqrstuvwxyz";
let PK_UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
let PK_DIGITS = "0123456789";
let PK_SYMBOLS = "!@#$%^&*()-_=+[]{};:,.?/";
let PK_AMBIGUOUS = "O0oIl1|";                 // visually similar characters
let PK_HEX = "0123456789abcdef";
let PK_HASH_VERSION = "pk1";                  // legacy storage format prefix (HMAC chain, still verifiable)
let PK_HASH_V2 = "pk2";                       // current format: PBKDF2-HMAC-SHA256 (native, RFC 8018)
let PK_DEFAULT_ITERATIONS = 600000;            // OWASP 2023 minimum for PBKDF2-HMAC-SHA256
let PK_STRENGTH_LABELS = ["Very weak", "Weak", "Fair", "Strong", "Very strong"];
let PK_KEYBOARD_ROWS = ["qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890"];
let PK_SENSITIVE_KEYS = ["password", "passwd", "pwd", "pass", "secret", "token", "pin"];
let PK_COMMON = [
    "password", "123456", "12345678", "123456789", "1234567890", "12345", "111111",
    "000000", "123123", "654321", "qwerty", "qwerty123", "qwertyuiop", "abc123",
    "password1", "passw0rd", "p@ssw0rd", "letmein", "welcome", "admin", "admin123",
    "root", "login", "master", "monkey", "dragon", "football", "iloveyou",
    "sunshine", "princess", "1q2w3e4r", "1qaz2wsx", "test", "test123", "guest",
    "changeme", "default", "secret", "pass", "user"
];

// word list for passphrases (128 words = 7 bits per word)
let PK_WORDS = [
    "apple", "river", "cloud", "stone", "tiger", "lemon", "panda", "storm",
    "maple", "eagle", "ocean", "flame", "sugar", "piano", "robot", "cedar",
    "amber", "coral", "daisy", "ember", "fable", "grape", "haven", "ivory",
    "jolly", "karma", "lunar", "mango", "noble", "olive", "pearl", "quilt",
    "raven", "solar", "tulip", "unity", "velvet", "willow", "xenon", "yacht",
    "zebra", "anchor", "bridge", "candle", "desert", "engine", "forest", "garden",
    "harbor", "island", "jungle", "kitten", "ladder", "meadow", "needle", "orange",
    "pillow", "rabbit", "silver", "temple", "umbrella", "valley", "winter", "yellow",
    "breeze", "castle", "dancer", "falcon", "glacier", "hammer", "insect", "jacket",
    "kernel", "lantern", "marble", "nectar", "oyster", "pepper", "quartz", "rocket",
    "saddle", "thunder", "uplift", "violet", "walnut", "zephyr", "acorn", "beacon",
    "comet", "dolphin", "echo", "feather", "galaxy", "horizon", "indigo", "jasmine",
    "koala", "lotus", "magnet", "nebula", "orchid", "phoenix", "quasar", "ripple",
    "summit", "trophy", "orbit", "voyage", "whistle", "crystal", "yonder", "zenith",
    "blossom", "compass", "diamond", "emerald", "firefly", "gazelle", "harvest", "iceberg",
    "journey", "kingdom", "lullaby", "mystery", "network", "odyssey", "paradox", "rainbow"
];

// ---- Internal helpers -----------------------------------------------------------

// Split text into real UTF-8 characters (len/charAt in Rin work on bytes, so a password
// of 6 Arabic letters counts as 12). slice works with character indexes, so we walk it until it returns empty.
fun pkChars(s) {
    let out = [];
    if (s == nil) { return out; }
    let i = 0;
    let c = slice(s, 0, 1);
    while (c != "") {
        push(out, c);
        i = i + 1;
        c = slice(s, i, i + 1);
    }
    return out;
}

fun pkLen(s) {
    return len(pkChars(s));
}


fun pkNow() {
    return now() / 1000;          // monotonic seconds (for a single session only)
}

fun pkCopyMap(m) {
    let out = {};
    for (let k in keys(m)) { out[k] = m[k]; }
    return out;
}

fun pkCopyArr(a) {
    let out = [];
    let i = 0;
    while (i < len(a)) { push(out, a[i]); i = i + 1; }
    return out;
}

fun pkOpt(opts, key, fallback) {
    if (opts == nil) { return fallback; }
    if (has(opts, key)) { return opts[key]; }
    return fallback;
}

fun pkLog2(x) {
    if (x <= 1) { return 0; }
    return log(x) / log(2);
}

// ---- 2) Policy (Policy) -------------------------------------------------------

// Default policy (balanced). overrides are merged on top, and any unknown key is kept.
fun pkPolicy(overrides) {
    let p = {
        minLength: 8, maxLength: 128,
        requireLower: true, requireUpper: true, requireDigit: true, requireSymbol: false,
        allowSpaces: true, minUnique: 4, maxRepeat: 3,
        noSequence: true, noCommon: true, noPersonal: true,
        minScore: 2,
        historyCount: 5, maxAgeDays: 0,
        maxAttempts: 5, lockSeconds: 900,
        hashIterations: PK_DEFAULT_ITERATIONS
    };
    if (overrides != nil) {
        for (let k in keys(overrides)) { p[k] = overrides[k]; }
    }
    return p;
}

// Basic: for low-risk apps (length only + reject common passwords)
fun pkPolicyBasic() {
    return pkPolicy({
        minLength: 6, requireLower: false, requireUpper: false, requireDigit: false,
        minUnique: 3, noSequence: false, minScore: 1, historyCount: 0
    });
}

// Standard: the recommended default for most apps
fun pkPolicyStandard() {
    return pkPolicy({});
}

// Strict: for sensitive accounts (12+, symbols, expiry, longer lockout)
fun pkPolicyStrict() {
    return pkPolicy({
        minLength: 12, requireSymbol: true, minUnique: 8, maxRepeat: 2, minScore: 3,
        historyCount: 10, maxAgeDays: 90, maxAttempts: 3, lockSeconds: 1800,
        hashIterations: 1000000
    });
}

// PIN PIN: digits only with a nearly fixed length
fun pkPolicyPin(length) {
    let n = 6;
    if (length != nil) { n = length; }
    return pkPolicy({
        minLength: n, maxLength: n, requireLower: false, requireUpper: false,
        requireDigit: true, requireSymbol: false, allowSpaces: false, minUnique: 3,
        maxRepeat: 2, noCommon: false, noPersonal: false, minScore: 0, historyCount: 0,
        maxAttempts: 3, lockSeconds: 600
    });
}

// ---- 3) Analysis and strength ----------------------------------------------------------

// Count of each character class inside the password
fun pkCharClasses(pw) {
    let c = { lower: 0, upper: 0, digit: 0, symbol: 0, space: 0, other: 0 };
    let chars = pkChars(pw);
    let i = 0;
    while (i < len(chars)) {
        let ch = chars[i];
        if (contains(PK_LOWER, ch)) { c["lower"] = c["lower"] + 1; }
        else if (contains(PK_UPPER, ch)) { c["upper"] = c["upper"] + 1; }
        else if (contains(PK_DIGITS, ch)) { c["digit"] = c["digit"] + 1; }
        else if (ch == " ") { c["space"] = c["space"] + 1; }
        else if (contains(PK_SYMBOLS, ch)) { c["symbol"] = c["symbol"] + 1; }
        else { c["other"] = c["other"] + 1; }
        i = i + 1;
    }
    return c;
}

// Size of the possible character pool (to compute theoretical entropy)
fun pkPoolSize(classes) {
    let n = 0;
    if (classes["lower"] > 0) { n = n + 26; }
    if (classes["upper"] > 0) { n = n + 26; }
    if (classes["digit"] > 0) { n = n + 10; }
    if (classes["symbol"] > 0) { n = n + 24; }
    if (classes["space"] > 0) { n = n + 1; }
    if (classes["other"] > 0) { n = n + 64; }
    return n;
}

// Number of distinct characters
fun pkUniqueCount(pw) {
    let chars = pkChars(pw);
    let seen = [];
    let i = 0;
    while (i < len(chars)) {
        if (contains(seen, chars[i]) == false) { push(seen, chars[i]); }
        i = i + 1;
    }
    return len(seen);
}

// Longest consecutive run of the same character (aaaa → 4)
fun pkLongestRepeat(pw) {
    let chars = pkChars(pw);
    let best = 0;
    let run = 0;
    let prev = "";
    let i = 0;
    while (i < len(chars)) {
        if (i > 0 and chars[i] == prev) { run = run + 1; } else { run = 1; }
        if (run > best) { best = run; }
        prev = chars[i];
        i = i + 1;
    }
    return best;
}

// Does it contain a sequence of 3+ characters (abc / 321 / qwe) ascending or descending?
fun pkHasSequence(pw) {
    let s = lower(pw);
    let sources = [PK_LOWER, PK_DIGITS];
    let r = 0;
    while (r < len(PK_KEYBOARD_ROWS)) { push(sources, PK_KEYBOARD_ROWS[r]); r = r + 1; }
    let i = 0;
    while (i + 2 < len(s)) {
        let tri = substr(s, i, 3);
        let rev = "";
        let j = 2;
        while (j >= 0) { rev = rev + charAt(tri, j); j = j - 1; }
        let k = 0;
        while (k < len(sources)) {
            if (contains(sources[k], tri) or contains(sources[k], rev)) { return true; }
            k = k + 1;
        }
        i = i + 1;
    }
    return false;
}

// Simplify "leet": p@ssw0rd → password (to detect disguised common passwords)
fun pkDeLeet(s) {
    let t = lower(s);
    t = replace(t, "@", "a");
    t = replace(t, "0", "o");
    t = replace(t, "1", "l");
    t = replace(t, "3", "e");
    t = replace(t, "$", "s");
    t = replace(t, "5", "s");
    t = replace(t, "7", "t");
    return t;
}

// Is it (or very close to) a common password?
fun pkIsCommon(pw) {
    let a = lower(pw);
    let b = pkDeLeet(pw);
    let i = 0;
    while (i < len(PK_COMMON)) {
        let c = PK_COMMON[i];
        if (a == c or b == c) { return true; }
        // a common word + digits/symbol at the end only (password2024!)
        if (len(c) >= 6 and (indexOf(a, c) == 0 or indexOf(b, c) == 0) and len(pw) <= len(c) + 4) { return true; }
        i = i + 1;
    }
    return false;
}

// Does it contain personal info (username/email/name)? items = array of strings
fun pkContainsPersonal(pw, items) {
    if (items == nil) { return false; }
    let a = lower(pw);
    let i = 0;
    while (i < len(items)) {
        let it = items[i];
        if (it != nil and len(it) >= 3) {
            let low = lower(it);
            if (contains(a, low)) { return true; }
            // the part before @ in an email
            let at = indexOf(low, "@");
            if (at >= 3 and contains(a, substr(low, 0, at))) { return true; }
        }
        i = i + 1;
    }
    return false;
}

// Readable text for a duration in seconds
fun pkHumanTime(sec) {
    if (sec < 1) { return "instantly"; }
    if (sec < 60) { return "under a minute"; }
    if (sec < 3600) { return toString(floor(sec / 60)) + " minutes"; }
    if (sec < 86400) { return toString(floor(sec / 3600)) + " hours"; }
    if (sec < 2592000) { return toString(floor(sec / 86400)) + " days"; }
    if (sec < 31536000) { return toString(floor(sec / 2592000)) + " months"; }
    if (sec < 3153600000) { return toString(floor(sec / 31536000)) + " years"; }
    return "many centuries";
}

// Approximate guess time for a fast offline attack (10 billion guesses/s, half of them on average)
fun pkCrackSeconds(bits) {
    if (bits >= 80) { return 3153600001; }
    return pow(2, bits) / 10000000000 / 2;
}

// Full analysis. ctx is optional: { username, email, name, personal:[...] }
fun pkAnalyze(pw, ctx) {
    let classes = pkCharClasses(pw);
    let pool = pkPoolSize(classes);
    let length = pkLen(pw);
    let rawBits = 0;
    if (pool > 0) { rawBits = length * pkLog2(pool); }

    let unique = pkUniqueCount(pw);
    let longestRun = pkLongestRepeat(pw);
    let hasSeq = pkHasSequence(pw);
    let common = pkIsCommon(pw);

    let personalItems = [];
    if (ctx != nil) {
        if (has(ctx, "username")) { push(personalItems, ctx["username"]); }
        if (has(ctx, "email")) { push(personalItems, ctx["email"]); }
        if (has(ctx, "name")) { push(personalItems, ctx["name"]); }
        if (has(ctx, "personal")) {
            let q = 0;
            while (q < len(ctx["personal"])) { push(personalItems, ctx["personal"][q]); q = q + 1; }
        }
    }
    let personal = pkContainsPersonal(pw, personalItems);

    // Penalties: repetition, sequences and low variety reduce effective entropy
    let bits = rawBits;
    if (length > 0 and unique < length) { bits = bits * (unique / length) + (rawBits * 0.25); }
    if (bits > rawBits) { bits = rawBits; }
    if (hasSeq) { bits = bits - 12; }
    if (longestRun >= 3) { bits = bits - (longestRun * 3); }
    if (personal) { bits = bits - 15; }
    if (common) { bits = 8; }
    if (bits < 0) { bits = 0; }
    bits = round(bits * 10) / 10;

    let score = 0;
    if (bits >= 28) { score = 1; }
    if (bits >= 40) { score = 2; }
    if (bits >= 60) { score = 3; }
    if (bits >= 80) { score = 4; }
    if (common) { score = 0; }

    let feedback = [];
    if (length < 8) { push(feedback, "short - use 12 or more characters"); }
    if (common) { push(feedback, "a common password or close to one"); }
    if (hasSeq) { push(feedback, "contains a predictable sequence (abc / 123 / qwe)"); }
    if (longestRun >= 3) { push(feedback, "contains consecutive repeats of the same character"); }
    if (personal) { push(feedback, "contains personal info that is easy to guess"); }
    let variety = 0;
    if (classes["lower"] > 0) { variety = variety + 1; }
    if (classes["upper"] > 0) { variety = variety + 1; }
    if (classes["digit"] > 0) { variety = variety + 1; }
    if (classes["symbol"] > 0) { variety = variety + 1; }
    if (variety < 3 and length < 16) { push(feedback, "mix upper and lower case, digits and symbols, or make the phrase longer"); }

    return {
        length: length, classes: classes, poolSize: pool, uniqueChars: unique,
        longestRepeat: longestRun, hasSequence: hasSeq, isCommon: common, hasPersonal: personal,
        rawEntropyBits: round(rawBits * 10) / 10, entropyBits: bits,
        score: score, label: PK_STRENGTH_LABELS[score],
        crackTime: pkHumanTime(pkCrackSeconds(bits)), feedback: feedback
    };
}

// Shortcut: strength 0..4 only
fun pkScore(pw) {
    return pkAnalyze(pw, nil)["score"];
}

// ---- 4) Policy validation -------------------------------------------------------

// Checks pw against policy and collects all violations at once.
// ctx optional: { username, email, name, personal:[...], history:[array of previous hashes] }
fun pkCheck(pw, policy, ctx) {
    let p = policy;
    if (p == nil) { p = pkPolicyStandard(); }
    let a = pkAnalyze(pw, ctx);
    let errors = [];
    let failed = [];
    let c = a["classes"];

    if (a["length"] < p["minLength"]) {
        push(errors, "Length is below the minimum (" + toString(p["minLength"]) + ")");
        push(failed, "too_short");
    }
    if (a["length"] > p["maxLength"]) {
        push(errors, "Length is above the maximum (" + toString(p["maxLength"]) + ")");
        push(failed, "too_long");
    }
    if (p["requireLower"] and c["lower"] == 0) { push(errors, "At least one lowercase letter is required"); push(failed, "no_lower"); }
    if (p["requireUpper"] and c["upper"] == 0) { push(errors, "At least one uppercase letter is required"); push(failed, "no_upper"); }
    if (p["requireDigit"] and c["digit"] == 0) { push(errors, "At least one digit is required"); push(failed, "no_digit"); }
    if (p["requireSymbol"] and c["symbol"] == 0) { push(errors, "At least one symbol is required"); push(failed, "no_symbol"); }
    if (p["allowSpaces"] == false and c["space"] > 0) { push(errors, "Spaces are not allowed"); push(failed, "spaces"); }
    if (a["uniqueChars"] < p["minUnique"]) {
        push(errors, "Too few distinct characters (minimum " + toString(p["minUnique"]) + " distinct characters)");
        push(failed, "low_unique");
    }
    if (a["longestRepeat"] > p["maxRepeat"]) {
        push(errors, "Consecutive repeat of more than " + toString(p["maxRepeat"]) + " times for the same character");
        push(failed, "repeat");
    }
    if (p["noSequence"] and a["hasSequence"]) { push(errors, "contains a predictable sequence such as abc or 123"); push(failed, "sequence"); }
    if (p["noCommon"] and a["isCommon"]) { push(errors, "Common password"); push(failed, "common"); }
    if (p["noPersonal"] and a["hasPersonal"]) { push(errors, "contains personal info (name/email)"); push(failed, "personal"); }
    if (a["score"] < p["minScore"]) {
        push(errors, "Strength (" + a["label"] + ") is below the required level (" + PK_STRENGTH_LABELS[p["minScore"]] + ")");
        push(failed, "weak");
    }

    // Block reuse of previous passwords
    if (ctx != nil and has(ctx, "history") and p["historyCount"] > 0) {
        let h = ctx["history"];
        let i = 0;
        while (i < len(h) and i < p["historyCount"]) {
            if (pkVerify(pw, h[i])) {
                push(errors, "This password was used before - choose a new one");
                push(failed, "reused");
                i = len(h);
            }
            i = i + 1;
        }
    }

    if (len(errors) == 0) { return { ok: true, analysis: a }; }
    return { ok: false, errors: errors, failed: failed, analysis: a, error: join(errors, "; ") };
}

// Password confirmation (do both fields match?) with constant-time comparison
fun pkConfirm(a, b) {
    if (a == nil or b == nil) { return { ok: false, error: "pkConfirm: missing value" }; }
    if (sec.constantTimeEqual(a, b)) { return { ok: true }; }
    return { ok: false, error: "The passwords do not match" };
}

// ---- 5) Secure generation (CSPRNG via sec.randomToken, not random()) -----------------

fun pkRandByte() {
    let hx = sec.randomToken(1);
    return indexOf(PK_HEX, charAt(hx, 0)) * 16 + indexOf(PK_HEX, charAt(hx, 1));
}

// uniform integer in [0, n) using rejection sampling to avoid bias
fun pkRandInt(n) {
    if (n <= 1) { return 0; }
    if (n <= 256) {
        let limit = floor(256 / n) * n;
        let b = pkRandByte();
        while (b >= limit) { b = pkRandByte(); }
        return b % n;
    }
    let limit2 = floor(65536 / n) * n;
    let v = pkRandByte() * 256 + pkRandByte();
    while (v >= limit2) { v = pkRandByte() * 256 + pkRandByte(); }
    return v % n;
}

fun pkPick(set) {
    return charAt(set, pkRandInt(len(set)));
}

fun pkShuffle(arr) {
    let a = pkCopyArr(arr);
    let i = len(a) - 1;
    while (i > 0) {
        let j = pkRandInt(i + 1);
        let t = a[i];
        a[i] = a[j];
        a[j] = t;
        i = i - 1;
    }
    return a;
}

fun pkRemoveChars(set, bad) {
    let out = "";
    let i = 0;
    while (i < len(set)) {
        let ch = charAt(set, i);
        if (contains(bad, ch) == false) { out = out + ch; }
        i = i + 1;
    }
    return out;
}

// opts: { length:16, lower:true, upper:true, digits:true, symbols:true,
//         avoidAmbiguous:false, exclude:"", custom:"" }
// Guarantees at least one character from every enabled class, then shuffles the result.
fun pkGenerate(opts) {
    let length = pkOpt(opts, "length", 16);
    let avoid = pkOpt(opts, "avoidAmbiguous", false);
    let exclude = pkOpt(opts, "exclude", "");
    let sets = [];
    if (pkOpt(opts, "lower", true)) { push(sets, PK_LOWER); }
    if (pkOpt(opts, "upper", true)) { push(sets, PK_UPPER); }
    if (pkOpt(opts, "digits", true)) { push(sets, PK_DIGITS); }
    if (pkOpt(opts, "symbols", true)) { push(sets, PK_SYMBOLS); }
    let custom = pkOpt(opts, "custom", "");
    if (len(custom) > 0) { push(sets, custom); }

    let cleaned = [];
    let pool = "";
    let i = 0;
    while (i < len(sets)) {
        let s = sets[i];
        if (avoid) { s = pkRemoveChars(s, PK_AMBIGUOUS); }
        if (len(exclude) > 0) { s = pkRemoveChars(s, exclude); }
        if (len(s) > 0) { push(cleaned, s); pool = pool + s; }
        i = i + 1;
    }
    if (len(cleaned) == 0) { return { ok: false, error: "pkGenerate: No character classes are enabled" }; }
    if (length < len(cleaned)) { return { ok: false, error: "pkGenerate: length is smaller than the number of required classes (" + toString(len(cleaned)) + ")" }; }
    if (length > 1024) { return { ok: false, error: "pkGenerate: maximum length is 1024" }; }

    let chars = [];
    let j = 0;
    while (j < len(cleaned)) { push(chars, pkPick(cleaned[j])); j = j + 1; }
    while (len(chars) < length) { push(chars, pkPick(pool)); }
    let out = join(pkShuffle(chars), "");
    let bits = round(length * pkLog2(len(pool)) * 10) / 10;
    return { ok: true, password: out, length: length, poolSize: len(pool), entropyBits: bits, strength: pkScore(out) };
}

// n several passwords at once (array of strings)
fun pkGenerateMany(count, opts) {
    let out = [];
    let i = 0;
    while (i < count) {
        let g = pkGenerate(opts);
        if (g["ok"] == false) { return g; }
        push(out, g["password"]);
        i = i + 1;
    }
    return { ok: true, passwords: out };
}

// PIN random digits; regenerates (up to 50 attempts) if it came out sequential or fully repeated
fun pkGeneratePin(length) {
    let n = 6;
    if (length != nil) { n = length; }
    if (n < 4 or n > 32) { return { ok: false, error: "pkGeneratePin: length must be between 4 and 32" }; }
    let tries = 0;
    while (tries < 50) {
        let pin = "";
        let i = 0;
        while (i < n) { pin = pin + pkPick(PK_DIGITS); i = i + 1; }
        if (pkHasSequence(pin) == false and pkLongestRepeat(pin) <= 2) {
            return { ok: true, pin: pin, length: n, entropyBits: round(n * pkLog2(10) * 10) / 10 };
        }
        tries = tries + 1;
    }
    return { ok: false, error: "pkGeneratePin: could not generate a suitable PIN" };
}

// Passphrase (passphrase): opts { words:6, separator:"-", capitalize:false, number:false }
fun pkPassphrase(opts) {
    let n = pkOpt(opts, "words", 6);
    let sep = pkOpt(opts, "separator", "-");
    let cap = pkOpt(opts, "capitalize", false);
    let addNum = pkOpt(opts, "number", false);
    if (n < 3 or n > 20) { return { ok: false, error: "pkPassphrase: number of words must be between 3 and 20" }; }
    let picked = [];
    let i = 0;
    while (i < n) {
        let w = PK_WORDS[pkRandInt(len(PK_WORDS))];
        if (cap) { w = upper(charAt(w, 0)) + substr(w, 1, len(w) - 1); }
        push(picked, w);
        i = i + 1;
    }
    let phrase = join(picked, sep);
    let bits = n * pkLog2(len(PK_WORDS));
    if (addNum) {
        phrase = phrase + sep + toString(pkRandInt(100));
        bits = bits + pkLog2(100);
    }
    return { ok: true, passphrase: phrase, words: n, entropyBits: round(bits * 10) / 10 };
}

// ---- 6) Storage: salted hash and key stretching + constant-time verification --------------------------

fun pkSalt() {
    return sec.randomToken(16);
}

// Returns a string in the format  pk2$<iterations>$<salt hex>$<hash hex>  (PBKDF2-HMAC-SHA256)
// opts optional: { iterations, salt }
fun pkHashLegacy(pw, opts) {
    let iter = pkOpt(opts, "iterations", 3000);
    let salt = pkOpt(opts, "salt", pkSalt());
    if (iter < 1) { iter = 1; }
    let h = sec.hmacSha256(salt, pw);
    let i = 1;
    while (i < iter) {
        h = sec.hmacSha256(h, salt + pw);
        i = i + 1;
    }
    return PK_HASH_VERSION + "$" + toString(iter) + "$" + salt + "$" + h;
}
// pk2$<iterations>$<salt hex>$<PBKDF2-HMAC-SHA256 hex>.  opts: iterations, salt (hex), legacy (true => pk1)
fun pkHash(pw, opts) {
    if (opts != nil and has(opts, "legacy") and opts["legacy"] == true) { return pkHashLegacy(pw, opts); }
    let iter = pkOpt(opts, "iterations", PK_DEFAULT_ITERATIONS);
    let salt = pkOpt(opts, "salt", pkSalt());
    if (iter < 1) { iter = 1; }
    return PK_HASH_V2 + "$" + toString(iter) + "$" + salt + "$" + sec.pbkdf2Sha256(pw, salt, iter, 32);
}
fun pkParseHash(stored) {
    if (stored == nil) { return { ok: false, error: "pkParseHash: empty value" }; }
    let parts = split(stored, "$");
    if (len(parts) != 4 or (parts[0] != PK_HASH_VERSION and parts[0] != PK_HASH_V2)) {
        return { ok: false, error: "pkParseHash: unknown hash format" };
    }
    // toNumber throws an error on non-numeric text, so check the digits first (corrupt hash => false, no crash)
    if (len(parts[1]) == 0 or len(parts[1]) > 8) { return { ok: false, error: "pkParseHash: invalid iteration count" }; }
    let d = 0;
    while (d < len(parts[1])) {
        if (contains(PK_DIGITS, charAt(parts[1], d)) == false) { return { ok: false, error: "pkParseHash: invalid iteration count" }; }
        d = d + 1;
    }
    let iter = toNumber(parts[1]);
    if (iter < 1 or iter > 10000000) { return { ok: false, error: "pkParseHash: invalid iteration count" }; }
    return { ok: true, version: parts[0], iterations: iter, salt: parts[2], hash: parts[3] };
}

// Constant-time check: does pw match the stored hash? (false on any corrupt format)
fun pcIsHexLocal(t) {
    if (type(t) != "string" or len(t) == 0 or len(t) % 2 != 0) { return false; }
    let i = 0;
    while (i < len(t)) { if (contains("0123456789abcdefABCDEF", charAt(t, i)) == false) { return false; } i = i + 1; }
    return true;
}
fun pkVerify(pw, stored) {
    let p = pkParseHash(stored);
    if (p["ok"] == false) { return false; }
    if (p["version"] == PK_HASH_V2) {
        if (pcIsHexLocal(p["salt"]) == false) { return false; }
        return sec.constantTimeEqual(pkHash(pw, { iterations: p["iterations"], salt: p["salt"] }), stored);
    }
    return sec.constantTimeEqual(pkHashLegacy(pw, { iterations: p["iterations"], salt: p["salt"] }), stored);
}

// Should it be rehashed (iterations lower than today's policy)? call it after a successful login
fun pkNeedsRehash(stored, policy) {
    let p = pkParseHash(stored);
    if (p["ok"] == false) { return true; }
    if (p["version"] != PK_HASH_V2) { return true; }   // legacy pk1 hashes are re-hashed to PBKDF2 at the next login
    let target = PK_DEFAULT_ITERATIONS;
    if (policy != nil and has(policy, "hashIterations")) { target = policy["hashIterations"]; }
    return p["iterations"] < target;
}

// ---- 7) Lifecycle: password record (state variables) -------------------------

// Creates a new record after checking the policy. ctx is optional as in pkCheck.
// The record: { hash, createdAt, changedAt, expiresAt, history[], failedAttempts, lockedUntil, mustChange }
fun pkRecordNew(pw, policy, ctx, nowSec) {
    let p = policy;
    if (p == nil) { p = pkPolicyStandard(); }
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    let chk = pkCheck(pw, p, ctx);
    if (chk["ok"] == false) { return chk; }
    let expires = 0;
    if (p["maxAgeDays"] > 0) { expires = t + p["maxAgeDays"] * 86400; }
    return {
        ok: true,
        record: {
            hash: pkHash(pw, { iterations: p["hashIterations"] }),
            createdAt: t, changedAt: t, expiresAt: expires,
            history: [], failedAttempts: 0, lockedUntil: 0, mustChange: false
        },
        analysis: chk["analysis"]
    };
}

fun pkIsLocked(record, nowSec) {
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    return record["lockedUntil"] > t;
}

fun pkIsExpired(record, nowSec) {
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    if (record["expiresAt"] <= 0) { return false; }
    return t >= record["expiresAt"];
}

// Days left before expiry (-1 = no expiry), 0 = expired
fun pkDaysLeft(record, nowSec) {
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    if (record["expiresAt"] <= 0) { return -1; }
    if (t >= record["expiresAt"]) { return 0; }
    return ceil((record["expiresAt"] - t) / 86400);
}

// Login attempt: returns an updated record (does not modify the original) with the reason for the result.
// reason: "ok" | "wrong" | "locked" | "expired" (ok:true with mustChange on expiry)
fun pkLogin(record, pw, policy, nowSec) {
    let p = policy;
    if (p == nil) { p = pkPolicyStandard(); }
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    let r = pkCopyMap(record);

    if (pkIsLocked(r, t)) {
        return { ok: false, reason: "locked", retryAfter: ceil(r["lockedUntil"] - t), record: r,
                 error: "The account is temporarily locked because of failed attempts" };
    }

    if (pkVerify(pw, r["hash"]) == false) {
        r["failedAttempts"] = r["failedAttempts"] + 1;
        let left = p["maxAttempts"] - r["failedAttempts"];
        if (left <= 0) {
            r["lockedUntil"] = t + p["lockSeconds"];
            r["failedAttempts"] = 0;
            return { ok: false, reason: "locked", retryAfter: p["lockSeconds"], attemptsLeft: 0, record: r,
                     error: "The account was locked after repeated failed attempts" };
        }
        return { ok: false, reason: "wrong", attemptsLeft: left, record: r, error: "Incorrect password" };
    }

    r["failedAttempts"] = 0;
    r["lockedUntil"] = 0;
    if (pkNeedsRehash(r["hash"], p)) {
        r["hash"] = pkHash(pw, { iterations: p["hashIterations"] });
    }
    if (pkIsExpired(r, t)) {
        r["mustChange"] = true;
        return { ok: true, reason: "expired", mustChange: true, record: r };
    }
    return { ok: true, reason: "ok", mustChange: r["mustChange"], record: r };
}

// Change password: verifies the old one, applies the policy + blocks reuse, and updates the history.
fun pkChange(record, oldPw, newPw, policy, ctx, nowSec) {
    let p = policy;
    if (p == nil) { p = pkPolicyStandard(); }
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    if (pkVerify(oldPw, record["hash"]) == false) {
        return { ok: false, error: "The current password is incorrect" };
    }
    if (sec.constantTimeEqual(oldPw, newPw)) {
        return { ok: false, error: "The new password is the same as the current one" };
    }
    let c = {};
    if (ctx != nil) { c = pkCopyMap(ctx); }
    // stored history + the current password itself count as not reusable
    let hist = pkCopyArr(record["history"]);
    let all = [record["hash"]];
    let i = 0;
    while (i < len(hist)) { push(all, hist[i]); i = i + 1; }
    c["history"] = all;
    let chk = pkCheck(newPw, p, c);
    if (chk["ok"] == false) { return chk; }

    // the current one goes to the front of the history, and the history is trimmed to the last historyCount
    let newHist = [record["hash"]];
    let k = 0;
    while (k < len(hist) and len(newHist) < p["historyCount"]) { push(newHist, hist[k]); k = k + 1; }
    if (p["historyCount"] <= 0) { newHist = []; }

    let r = pkCopyMap(record);
    r["hash"] = pkHash(newPw, { iterations: p["hashIterations"] });
    r["history"] = newHist;
    r["changedAt"] = t;
    r["failedAttempts"] = 0;
    r["lockedUntil"] = 0;
    r["mustChange"] = false;
    if (p["maxAgeDays"] > 0) { r["expiresAt"] = t + p["maxAgeDays"] * 86400; } else { r["expiresAt"] = 0; }
    return { ok: true, record: r, analysis: chk["analysis"] };
}

// ---- 8) Recovery: temporary reset tokens ---------------------------------

// Returns token (sent to the user once) + tokenHash (only stored) + expiresAt
fun pkResetToken(ttlSec, nowSec) {
    let ttl = 900;
    if (ttlSec != nil) { ttl = ttlSec; }
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    let token = sec.randomToken(32);
    return { ok: true, token: token, tokenHash: sec.sha256(token), expiresAt: t + ttl };
}

fun pkCheckResetToken(token, tokenHash, expiresAt, nowSec) {
    let t = nowSec;
    if (t == nil) { t = pkNow(); }
    if (t >= expiresAt) { return { ok: false, reason: "expired", error: "The recovery token has expired" }; }
    if (sec.constantTimeEqual(sec.sha256(token), tokenHash) == false) {
        return { ok: false, reason: "invalid", error: "The recovery token is incorrect" };
    }
    return { ok: true };
}

// ---- 9) Masking: display mask + log redaction ---------------------------------

// ●●●●●●●● same number of characters as the password (real characters, not bytes)
fun pkMask(pw) {
    if (pw == nil) { return ""; }
    return repeat("•", pkLen(pw));
}

// Keeps the last keep characters visible: ••••••ab12
fun pkMaskKeep(pw, keep) {
    if (pw == nil) { return ""; }
    let n = pkLen(pw);
    let k = keep;
    if (k > n - 4) { k = 0; }       // do not reveal most of a short password
    if (k < 0) { k = 0; }
    return repeat("•", n - k) + slice(pw, n - k, n);
}

// Is the field name sensitive (password/token/...)?
fun pkIsSensitiveKey(key) {
    let low = lower(toString(key));
    let i = 0;
    while (i < len(PK_SENSITIVE_KEYS)) {
        if (contains(low, PK_SENSITIVE_KEYS[i])) { return true; }
        i = i + 1;
    }
    return false;
}

// A copy of the map with sensitive fields hidden (redact logs before print/log)
fun pkRedact(obj) {
    let out = {};
    for (let k in keys(obj)) {
        if (pkIsSensitiveKey(k)) { out[k] = "***"; } else { out[k] = obj[k]; }
    }
    return out;
}
)PASSKITOGRIN";
static const char* kLib_passkitcrypt_og_rin = R"PKCRYPTOGRIN(
// ============================================================================
//  lib/passkitcrypt.og.rin — Encryption, signing and key generation for the Passkit family (prefix pc)
//  Import:  @import "lib/passkitcrypt.og.rin";
//
//  Built on standard primitives implemented natively in the engine (app/src/main/cpp/rin_crypto_aead.h) and
//  verified against official vectors: AES-256-GCM (NIST SP 800-38D), PBKDF2-HMAC-SHA256 (RFC 7914),
//  plus sec.hmacSha256 / sec.sha256 and sec.randomToken (OS CSPRNG, /dev/urandom).
//  All binary values are passed as hexadecimal text (hex) in lowercase.
//
//  Verified with official test vectors in the tests:
//    AES-GCM (NIST) · HMAC-SHA256 (RFC 4231) · HKDF (RFC 5869) · PBKDF2-HMAC-SHA256 (RFC 7914) ·
//    TOTP-SHA256 (RFC 6238) · Base32 (RFC 4648)
//  pcSeal/pcOpen = AES-256-GCM (format pc2, per-message random 96-bit nonce, key separated by HKDF).
//  The older HMAC-CTR construction (format pc1) is still readable by pcOpen and available as pcSealLegacy.
//  Honest limits: AES uses lookup tables (not hardened against cache-timing attacks from a co-located attacker);
//  keep a single key under ~2^32 messages (rotate with the keyring).
//
//  Convention: every function returns {ok:true,...} or {ok:false,error:"..."} and never crashes on corrupt input.
// ============================================================================

let PC_HEXCHARS = "0123456789abcdef";
let PC_B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
let PC_ZERO32 = "0000000000000000000000000000000000000000000000000000000000000000";

// ---- 1) Encoding and numbers ----------------------------------------------------------------

fun pcHexVal(c) { return indexOf(PC_HEXCHARS, lower(c)); }

fun pcIsHex(s) {
    if (type(s) != "string") { return false; }
    if (len(s) % 2 != 0) { return false; }
    let i = 0;
    while (i < len(s)) {
        if (pcHexVal(charAt(s, i)) < 0) { return false; }
        i = i + 1;
    }
    return true;
}

// F: number -> hex with fixed width
fun pcToHex(n, width) {
    let s = "";
    let v = floor(n);
    while (v > 0) {
        s = charAt(PC_HEXCHARS, v % 16) + s;
        v = floor(v / 16);
    }
    while (len(s) < width) { s = "0" + s; }
    if (len(s) == 0) { s = "0"; }
    return s;
}

// F: hex -> number
fun pcHexToNum(hex) {
    let n = 0;
    let i = 0;
    while (i < len(hex)) { n = n * 16 + pcHexVal(charAt(hex, i)); i = i + 1; }
    return n;
}

fun pcByteAt(hex, i) {
    return pcHexVal(charAt(hex, i * 2)) * 16 + pcHexVal(charAt(hex, i * 2 + 1));
}

fun pcHexOf(txt) { return sec.hexEncode(toString(txt)); }

// F: Base64 URL-safe (without = ) from hex
fun pcB64UrlEncodeHex(hex) {
    let b = sec.base64Encode(sec.hexDecode(hex));
    b = replace(b, "+", "-");
    b = replace(b, "/", "_");
    return replace(b, "=", "");
}

fun pcB64UrlDecodeHex(s) {
    if (type(s) != "string") { return nil; }
    let i = 0;
    while (i < len(s)) {
        let c = charAt(s, i);
        if (isAlnumChar(c) == false and c != "-" and c != "_") { return nil; }
        i = i + 1;
    }
    let b = replace(replace(s, "-", "+"), "_", "/");
    while (len(b) % 4 != 0) { b = b + "="; }
    return sec.hexEncode(sec.base64Decode(b));
}

// F: Base64 URL-safe from text
fun pcB64UrlEncode(txt) { return pcB64UrlEncodeHex(pcHexOf(txt)); }

fun pcB64UrlDecode(s) {
    let h = pcB64UrlDecodeHex(s);
    if (h == nil) { return nil; }
    return sec.hexDecode(h);
}

// F: Base32 (RFC 4648, no padding) from hex
fun pcBase32EncodeHex(hex) {
    let out = "";
    let buf = 0;
    let bits = 0;
    let n = len(hex) / 2;
    let i = 0;
    while (i < n) {
        buf = buf * 256 + pcByteAt(hex, i);
        bits = bits + 8;
        while (bits >= 5) {
            let idx = floor(buf / pow(2, bits - 5)) % 32;
            out = out + charAt(PC_B32, idx);
            bits = bits - 5;
            buf = buf % pow(2, bits);
        }
        i = i + 1;
    }
    if (bits > 0) { out = out + charAt(PC_B32, (buf * pow(2, 5 - bits)) % 32); }
    return out;
}

fun pcBase32DecodeHex(s) {
    let out = "";
    let buf = 0;
    let bits = 0;
    let up = upper(s);
    let i = 0;
    while (i < len(up)) {
        let c = charAt(up, i);
        if (c != "=" and c != " " and c != "-") {
            let v = indexOf(PC_B32, c);
            if (v < 0) { return nil; }
            buf = buf * 32 + v;
            bits = bits + 5;
            if (bits >= 8) {
                out = out + pcToHex(floor(buf / pow(2, bits - 8)), 2);
                bits = bits - 8;
                buf = buf % pow(2, bits);
            }
        }
        i = i + 1;
    }
    return out;
}

fun pcBase32Encode(txt) { return pcBase32EncodeHex(pcHexOf(txt)); }

fun pcBase32Decode(s) {
    let h = pcBase32DecodeHex(s);
    if (h == nil) { return nil; }
    return sec.hexDecode(h);
}

// ---- 2) Secure randomness ------------------------------------------------------------------

// F: n secure random bytes (hex)
fun pcRandomHex(n) { return sec.randomToken(n); }

// F: uniform integer in [0,n) using rejection sampling
fun pcRandomInt(n) {
    if (n <= 1) { return 0; }
    let range = 4294967296;
    let limit = floor(range / n) * n;
    let v = pcHexToNum(sec.randomToken(4));
    while (v >= limit) { v = pcHexToNum(sec.randomToken(4)); }
    return v % n;
}

// F: random id with a prefix: user_9f3a...
fun pcRandomId(prefix, bytes) {
    let b = 8;
    if (bytes != nil) { b = bytes; }
    return prefix + "_" + sec.randomToken(b);
}

// F: UUID v4
fun pcUuid4() {
    let h = sec.randomToken(16);
    let variant = charAt("89ab", pcRandomInt(4));
    return substr(h, 0, 8) + "-" + substr(h, 8, 4) + "-4" + substr(h, 13, 3) + "-" + variant + substr(h, 17, 3) + "-" + substr(h, 20, 12);
}

// F: XOR for two equal-length hex strings
fun pcXorHex(a, b) {
    if (len(a) != len(b)) { return nil; }
    if (len(a) == 0) { return ""; }
    return sec.hexEncode(sec.xorCipher(sec.hexDecode(a), sec.hexDecode(b)));
}

// F: constant-time comparison
fun pcEqual(a, b) {
    return sec.constantTimeEqual(toString(a), toString(b));
}

// ---- 3) HMAC and hashing ------------------------------------------------------------------

// F: HMAC-SHA256 with a hex key and hex data (RFC 4231 vector, tested)
fun pcHmacHex(keyHex, dataHex) {
    let d = "";
    if (len(dataHex) > 0) { d = sec.hexDecode(dataHex); }
    let k = "";
    if (len(keyHex) > 0) { k = sec.hexDecode(keyHex); }
    return sec.hmacSha256(k, d);
}

fun pcHmacText(keyHex, txt) { return pcHmacHex(keyHex, pcHexOf(txt)); }

fun pcSha256Hex(dataHex) {
    if (len(dataHex) == 0) { return sec.sha256(""); }
    return sec.sha256(sec.hexDecode(dataHex));
}

// F: sha256 double
fun pcDoubleSha(txt) {
    return pcSha256Hex(sec.sha256(toString(txt)));
}

// F: readable fingerprint ab:cd:ef:... (10 bytes)
fun pcFingerprint(data) {
    let h = sec.sha256(toString(data));
    let parts = [];
    let i = 0;
    while (i < 10) { push(parts, substr(h, i * 2, 2)); i = i + 1; }
    return join(parts, ":");
}

// F: short hash (n bytes)
fun pcShortHash(data, bytes) {
    return substr(sec.sha256(toString(data)), 0, bytes * 2);
}

// F: file hash
fun pcHashFile(path) {
    if (fileExists(path) == false) { return { ok: false, error: "File not found: " + path }; }
    return { ok: true, sha256: sec.sha256(readFile(path)) };
}

// F: hash chain (hash chain) of items; any change breaks everything after it
fun pcHashChain(items) {
    let prev = PC_ZERO32;
    let links = [];
    let i = 0;
    while (i < len(items)) {
        let h = sec.sha256(prev + "|" + toString(items[i]));
        push(links, { item: toString(items[i]), prev: prev, hash: h });
        prev = h;
        i = i + 1;
    }
    return { head: prev, links: links };
}

fun pcHashChainVerify(links) {
    let prev = PC_ZERO32;
    let i = 0;
    while (i < len(links)) {
        let l = links[i];
        if (l["prev"] != prev) { return { ok: false, brokenAt: i }; }
        if (sec.sha256(prev + "|" + l["item"]) != l["hash"]) { return { ok: false, brokenAt: i }; }
        prev = l["hash"];
        i = i + 1;
    }
    return { ok: true, head: prev };
}

// F: Merkle tree root (domain separation L:/N: against leaf/node attacks)
fun pcMerkleLevel(nodes) {
    let next = [];
    let i = 0;
    while (i < len(nodes)) {
        if (i + 1 < len(nodes)) { push(next, sec.sha256("N:" + nodes[i] + nodes[i + 1])); }
        else { push(next, nodes[i]); }
        i = i + 2;
    }
    return next;
}

fun pcMerkleRoot(leaves) {
    if (len(leaves) == 0) { return sec.sha256("empty"); }
    let level = [];
    let i = 0;
    while (i < len(leaves)) { push(level, sec.sha256("L:" + toString(leaves[i]))); i = i + 1; }
    while (len(level) > 1) { level = pcMerkleLevel(level); }
    return level[0];
}

// F: proof that a leaf belongs (index) to the tree
fun pcMerkleProof(leaves, index) {
    if (index < 0 or index >= len(leaves)) { return nil; }
    let level = [];
    let i = 0;
    while (i < len(leaves)) { push(level, sec.sha256("L:" + toString(leaves[i]))); i = i + 1; }
    let proof = [];
    let idx = index;
    while (len(level) > 1) {
        if (idx % 2 == 0) {
            if (idx + 1 < len(level)) { push(proof, { side: "r", hash: level[idx + 1] }); }
        } else {
            push(proof, { side: "l", hash: level[idx - 1] });
        }
        level = pcMerkleLevel(level);
        idx = floor(idx / 2);
    }
    return proof;
}

fun pcMerkleVerify(leaf, proof, root) {
    let h = sec.sha256("L:" + toString(leaf));
    let i = 0;
    while (i < len(proof)) {
        if (proof[i]["side"] == "r") { h = sec.sha256("N:" + h + proof[i]["hash"]); }
        else { h = sec.sha256("N:" + proof[i]["hash"] + h); }
        i = i + 1;
    }
    return sec.constantTimeEqual(h, root);
}

// ---- 4) Key derivation (KDF) -------------------------------------------------------------

// F: HKDF-Extract (RFC 5869)
fun pcHkdfExtract(saltHex, ikmHex) {
    let s = saltHex;
    if (s == nil or s == "") { s = PC_ZERO32; }
    return pcHmacHex(s, ikmHex);
}

// F: HKDF-Expand (RFC 5869) — length in bytes
fun pcHkdfExpand(prkHex, infoHex, length) {
    let n = ceil(length / 32);
    if (n > 255 or n < 1) { return nil; }
    let t = "";
    let okm = "";
    let i = 1;
    while (i <= n) {
        t = pcHmacHex(prkHex, t + infoHex + pcToHex(i, 2));
        okm = okm + t;
        i = i + 1;
    }
    return substr(okm, 0, length * 2);
}

// F: HKDF complete
fun pcHkdf(ikmHex, saltHex, infoHex, length) {
    return pcHkdfExpand(pcHkdfExtract(saltHex, ikmHex), infoHex, length);
}

// F: PBKDF2-HMAC-SHA256 — password text, saltHex, result hex (native C++ engine primitive, RFC 8018)
fun pcPbkdf2(password, saltHex, iterations, bytes) {
    return sec.pbkdf2Sha256(password, saltHex, iterations, bytes);
}

// F: random key (hex)
fun pcGenerateKey(bytes) {
    let b = 32;
    if (bytes != nil) { b = bytes; }
    return sec.randomToken(b);
}

// F: key id (kid) of 4 bytes that does not reveal the key
fun pcKeyId(keyHex) {
    return substr(sec.sha256("pc-kid:" + keyHex), 0, 8);
}

// F: sub-key for a specific purpose from the master key (domain separation)
fun pcDeriveSubkey(masterHex, purpose) {
    return pcHkdf(masterHex, "", pcHexOf("pc-sub:" + purpose), 32);
}

// F: key quality check (32 bytes, not repeated/zero)
fun pcKeyCheck(keyHex) {
    if (pcIsHex(keyHex) == false or len(keyHex) != 64) { return { ok: false, error: "The key must be 64 hex characters (32 bytes)" }; }
    let seen = "";
    let i = 0;
    while (i < 64) {
        let c = charAt(keyHex, i);
        if (contains(seen, c) == false) { seen = seen + c; }
        i = i + 1;
    }
    if (len(seen) < 6) { return { ok: false, error: "The key is weak (very low variety)" }; }
    return { ok: true };
}

// F: key from a password (PBKDF2) — returns the salt and iterations so it can be re-derived
fun pcKeyFromPassword(password, opts) {
    let iter = 2000;
    let saltHex = sec.randomToken(16);
    if (opts != nil) {
        if (has(opts, "iterations")) { iter = opts["iterations"]; }
        if (has(opts, "salt")) { saltHex = opts["salt"]; }
    }
    if (iter < 1) { return { ok: false, error: "iterations must be >= 1" }; }
    return { ok: true, key: pcPbkdf2(password, saltHex, iter, 32), salt: saltHex, iterations: iter };
}

// F: Pass the password through a "secret salt" (pepper) kept private before the stored hashing
fun pcPepper(password, pepperHex) {
    return pcHmacText(pepperHex, password);
}

// ---- 5) Authenticated encryption (AEAD composite) ---------------------------------------------------------

fun pcSubkeys(keyHex, nonceHex) {
    let prk = pcHkdfExtract(nonceHex, keyHex);
    return { enc: pcHkdfExpand(prk, pcHexOf("pc-enc-v1"), 32), mac: pcHkdfExpand(prk, pcHexOf("pc-mac-v1"), 32) };
}

fun pcKeystream(encHex, nonceHex, nbytes) {
    let blocks = ceil(nbytes / 32);
    let ks = "";
    let i = 0;
    while (i < blocks) {
        ks = ks + pcHmacHex(encHex, nonceHex + pcToHex(i, 8));
        i = i + 1;
    }
    return substr(ks, 0, nbytes * 2);
}

fun pcTag(macHex, nonceHex, aadHex, ctHex) {
    return pcHmacHex(macHex, pcHexOf("pc1|") + nonceHex + pcToHex(len(aadHex) / 2, 8) + aadHex + ctHex);
}

// F: authenticated encryption; aad (optional) = associated data that is signed but not encrypted (binds the ciphertext to its context)
// Format: pc1.<nonce>.<ciphertext>.<tag>
// ---- AES-256-GCM (NIST SP 800-38D) — the default since v2; the engine primitive is verified against NIST vectors ----
// Format: pc2.<nonce:12 bytes hex>.<ciphertext hex>.<tag:16 bytes hex>   (random 96-bit nonce per message;
// keep a single key under ~2^32 messages, or rotate with the keyring)
fun pcAeadKey(keyHex) { return pcDeriveSubkey(keyHex, "aes-256-gcm"); }

fun pcSeal(keyHex, plaintext, aad) {
    let kc = pcKeyCheck(keyHex);
    if (kc["ok"] == false) { return { ok: false, error: kc["error"] }; }
    let nonce = sec.randomToken(12);
    let aadHex = "";
    if (aad != nil) { aadHex = pcHexOf(aad); }
    let ctTag = sec.aesGcmSeal(pcAeadKey(keyHex), nonce, pcHexOf(plaintext), aadHex);
    let n = len(ctTag);
    return { ok: true, sealed: "pc2." + nonce + "." + substr(ctTag, 0, n - 32) + "." + substr(ctTag, n - 32, 32) };
}

// Opens pc2 (AES-GCM) and the legacy pc1 (HMAC-CTR) format, so data sealed by older versions stays readable
fun pcOpen(keyHex, sealed, aad) {
    if (type(sealed) == "string" and indexOf(sealed, "pc1.") == 0) { return pcOpenLegacy(keyHex, sealed, aad); }
    let kc = pcKeyCheck(keyHex);
    if (kc["ok"] == false) { return { ok: false, error: kc["error"] }; }
    if (type(sealed) != "string") { return { ok: false, error: "The ciphertext is invalid" }; }
    let parts = split(sealed, ".");
    if (len(parts) != 4 or parts[0] != "pc2") { return { ok: false, error: "Unknown format" }; }
    let nonce = parts[1];
    let ct = parts[2];
    let tag = parts[3];
    if (len(nonce) != 24 or pcIsHex(nonce) == false or pcIsHex(ct) == false or len(tag) != 32 or pcIsHex(tag) == false) {
        return { ok: false, error: "Corrupt format" };
    }
    let aadHex = "";
    if (aad != nil) { aadHex = pcHexOf(aad); }
    let pt = sec.aesGcmOpen(pcAeadKey(keyHex), nonce, ct + tag, aadHex);
    if (pt == nil) { return { ok: false, error: "Integrity check failed (wrong key, modified data or different context)" }; }
    return { ok: true, plaintext: sec.hexDecode(pt) };
}

// ---- legacy format (pc1): HMAC-SHA256 counter-mode stream + HMAC tag, kept for reading old data ----
fun pcSealLegacy(keyHex, plaintext, aad) {
    let kc = pcKeyCheck(keyHex);
    if (kc["ok"] == false) { return { ok: false, error: kc["error"] }; }
    let nonce = sec.randomToken(16);
    let sk = pcSubkeys(keyHex, nonce);
    let ptHex = pcHexOf(plaintext);
    let ct = "";
    if (len(ptHex) > 0) { ct = pcXorHex(ptHex, pcKeystream(sk["enc"], nonce, len(ptHex) / 2)); }
    let aadHex = "";
    if (aad != nil) { aadHex = pcHexOf(aad); }
    let tag = pcTag(sk["mac"], nonce, aadHex, ct);
    return { ok: true, sealed: "pc1." + nonce + "." + ct + "." + tag };
}

// F: decrypt after verifying the tag in constant time (any tampering, different aad or wrong key fails)
fun pcOpenLegacy(keyHex, sealed, aad) {
    let kc = pcKeyCheck(keyHex);
    if (kc["ok"] == false) { return { ok: false, error: kc["error"] }; }
    if (type(sealed) != "string") { return { ok: false, error: "The ciphertext is invalid" }; }
    let parts = split(sealed, ".");
    if (len(parts) != 4 or parts[0] != "pc1") { return { ok: false, error: "Unknown format" }; }
    let nonce = parts[1];
    let ct = parts[2];
    let tag = parts[3];
    if (len(nonce) != 32 or pcIsHex(nonce) == false or pcIsHex(ct) == false or len(tag) != 64 or pcIsHex(tag) == false) {
        return { ok: false, error: "Corrupt format" };
    }
    let sk = pcSubkeys(keyHex, nonce);
    let aadHex = "";
    if (aad != nil) { aadHex = pcHexOf(aad); }
    if (sec.constantTimeEqual(pcTag(sk["mac"], nonce, aadHex, ct), tag) == false) {
        return { ok: false, error: "Integrity check failed (wrong key, modified data or different context)" };
    }
    if (len(ct) == 0) { return { ok: true, plaintext: "" }; }
    let ptHex = pcXorHex(ct, pcKeystream(sk["enc"], nonce, len(ct) / 2));
    return { ok: true, plaintext: sec.hexDecode(ptHex) };
}

// F: encrypt an object (map/array) as JSON
fun pcSealObject(keyHex, obj, aad) {
    return pcSeal(keyHex, jsonEncode(obj), aad);
}

fun pcOpenObject(keyHex, sealed, aad) {
    let r = pcOpen(keyHex, sealed, aad);
    if (r["ok"] == false) { return r; }
    return { ok: true, value: jsonDecode(r["plaintext"]) };
}

// F: encrypt with a password (PBKDF2 + salt embedded in the output): pcp1.<iter>.<salt>.<pc1...>
fun pcSealWithPassword(password, plaintext, opts) {
    let kd = pcKeyFromPassword(password, opts);
    if (kd["ok"] == false) { return kd; }
    let s = pcSeal(kd["key"], plaintext, "pcp1");
    if (s["ok"] == false) { return s; }
    return { ok: true, sealed: "pcp1." + toString(kd["iterations"]) + "." + kd["salt"] + "." + s["sealed"] };
}

fun pcOpenWithPassword(password, sealed) {
    if (type(sealed) != "string") { return { ok: false, error: "The ciphertext is invalid" }; }
    let parts = split(sealed, ".");
    if (len(parts) != 7 or parts[0] != "pcp1") { return { ok: false, error: "Unknown format" }; }
    let iterTxt = parts[1];
    let i = 0;
    while (i < len(iterTxt)) {
        if (contains("0123456789", charAt(iterTxt, i)) == false) { return { ok: false, error: "Corrupt iteration count" }; }
        i = i + 1;
    }
    if (len(iterTxt) == 0 or len(iterTxt) > 7 or pcIsHex(parts[2]) == false) { return { ok: false, error: "Corrupt format" }; }
    let key = pcPbkdf2(password, parts[2], toNumber(iterTxt), 32);
    return pcOpen(key, join([parts[3], parts[4], parts[5], parts[6]], "."), "pcp1");
}

// ---- 6) Keyring (Keyring) and key rotation ---------------------------------------------------

// F: a new keyring with an active key
fun pcKeyringNew() {
    let k = pcGenerateKey(32);
    let kid = pcKeyId(k);
    let ring = { active: kid, keys: {} };
    ring["keys"][kid] = k;
    return ring;
}

// F: add a key (and optionally make it active)
fun pcKeyringAdd(ring, keyHex, makeActive) {
    let kc = pcKeyCheck(keyHex);
    if (kc["ok"] == false) { return kc; }
    let kid = pcKeyId(keyHex);
    ring["keys"][kid] = keyHex;
    if (makeActive == true) { ring["active"] = kid; }
    return { ok: true, kid: kid };
}

// F: rotation: a new active key, and the old ones stay for decryption only
fun pcKeyringRotate(ring) {
    return pcKeyringAdd(ring, pcGenerateKey(32), true);
}

// F: encrypt with the active key and embed the kid: pck1.<kid>.<pc1...>
fun pcKeyringSeal(ring, plaintext, aad) {
    let kid = ring["active"];
    let s = pcSeal(ring["keys"][kid], plaintext, aad);
    if (s["ok"] == false) { return s; }
    return { ok: true, sealed: "pck1." + kid + "." + s["sealed"], kid: kid };
}

fun pcKeyringKid(sealed) {
    if (type(sealed) != "string") { return nil; }
    let parts = split(sealed, ".");
    if (len(parts) != 6 or parts[0] != "pck1") { return nil; }
    return parts[1];
}

// F: decrypt, picking the right key automatically from the kid
fun pcKeyringOpen(ring, sealed, aad) {
    let kid = pcKeyringKid(sealed);
    if (kid == nil) { return { ok: false, error: "Unknown keyring format" }; }
    if (has(ring["keys"], kid) == false) { return { ok: false, error: "Unknown key in the keyring: " + kid }; }
    let parts = split(sealed, ".");
    let r = pcOpen(ring["keys"][kid], join([parts[2], parts[3], parts[4], parts[5]], "."), aad);
    if (r["ok"]) { r["kid"] = kid; }
    return r;
}

// F: re-encrypt with the active key (after rotation) without changing the content
fun pcKeyringRewrap(ring, sealed, aad) {
    let o = pcKeyringOpen(ring, sealed, aad);
    if (o["ok"] == false) { return o; }
    if (o["kid"] == ring["active"]) { return { ok: true, sealed: sealed, rewrapped: false }; }
    let s = pcKeyringSeal(ring, o["plaintext"], aad);
    if (s["ok"]) { s["rewrapped"] = true; }
    return s;
}

// ---- 7) Envelope encryption (Envelope) ------------------------------------------------------------------

// F: a random data key wrapped by a master key: pce1~<wrapped>~<body>
fun pcEnvelopeSeal(masterHex, plaintext, aad) {
    let dk = pcGenerateKey(32);
    let w = pcSeal(masterHex, dk, "pc-wrap");
    if (w["ok"] == false) { return w; }
    let b = pcSeal(dk, plaintext, aad);
    return { ok: true, sealed: "pce1~" + w["sealed"] + "~" + b["sealed"] };
}

fun pcEnvelopeOpen(masterHex, envelope, aad) {
    if (type(envelope) != "string") { return { ok: false, error: "Invalid envelope" }; }
    let parts = split(envelope, "~");
    if (len(parts) != 3 or parts[0] != "pce1") { return { ok: false, error: "Unknown envelope format" }; }
    let w = pcOpen(masterHex, parts[1], "pc-wrap");
    if (w["ok"] == false) { return w; }
    return pcOpen(w["plaintext"], parts[2], aad);
}

// F: master key rotation: only the data key is re-wrapped, the large body is untouched
fun pcEnvelopeRewrap(oldMasterHex, newMasterHex, envelope) {
    let parts = split(envelope, "~");
    if (len(parts) != 3 or parts[0] != "pce1") { return { ok: false, error: "Unknown envelope format" }; }
    let w = pcOpen(oldMasterHex, parts[1], "pc-wrap");
    if (w["ok"] == false) { return w; }
    let nw = pcSeal(newMasterHex, w["plaintext"], "pc-wrap");
    if (nw["ok"] == false) { return nw; }
    return { ok: true, sealed: "pce1~" + nw["sealed"] + "~" + parts[2] };
}

// ---- 8) Signing and tokens ---------------------------------------------------------------------------

// F: detached signature (HMAC) in base64url format
fun pcSign(keyHex, data) {
    return pcB64UrlEncodeHex(pcHmacText(keyHex, toString(data)));
}

fun pcVerifySig(keyHex, data, sig) {
    return sec.constantTimeEqual(pcSign(keyHex, data), toString(sig));
}

// F: signed token (JWT-HS256 simplified). opts: { now, ttl, iss, aud, nbf }
fun pcTokenSign(claims, keyHex, opts) {
    let kc = pcKeyCheck(keyHex);
    if (kc["ok"] == false) { return { ok: false, error: kc["error"] }; }
    let now = 0;
    if (opts != nil and has(opts, "now")) { now = opts["now"]; }
    let p = {};
    for (let k in keys(claims)) { p[k] = claims[k]; }
    p["iat"] = now;
    p["jti"] = sec.randomToken(8);
    if (opts != nil) {
        if (has(opts, "ttl")) { p["exp"] = now + opts["ttl"]; }
        if (has(opts, "nbf")) { p["nbf"] = opts["nbf"]; }
        if (has(opts, "iss")) { p["iss"] = opts["iss"]; }
        if (has(opts, "aud")) { p["aud"] = opts["aud"]; }
    }
    let h = pcB64UrlEncode("{\"alg\":\"HS256\",\"typ\":\"PCT\"}");
    let b = pcB64UrlEncode(jsonEncode(p));
    return { ok: true, token: h + "." + b + "." + pcSign(keyHex, h + "." + b), claims: p };
}

// F: verification: the HS256 algorithm only (rejects alg:none), signature, exp/nbf/iss/aud
fun pcTokenVerify(token, keyHex, opts) {
    if (type(token) != "string") { return { ok: false, reason: "malformed", error: "Invalid token" }; }
    let parts = split(token, ".");
    if (len(parts) != 3) { return { ok: false, reason: "malformed", error: "Invalid token" }; }
    let hj = pcB64UrlDecode(parts[0]);
    if (hj == nil) { return { ok: false, reason: "malformed", error: "Corrupt header" }; }
    let header = jsonDecode(hj);
    if (type(header) != "map" or has(header, "alg") == false or header["alg"] != "HS256") {
        return { ok: false, reason: "alg", error: "Algorithm not allowed" };
    }
    if (pcVerifySig(keyHex, parts[0] + "." + parts[1], parts[2]) == false) {
        return { ok: false, reason: "signature", error: "Incorrect signature" };
    }
    let pj = pcB64UrlDecode(parts[1]);
    if (pj == nil) { return { ok: false, reason: "malformed", error: "Corrupt payload" }; }
    let c = jsonDecode(pj);
    if (type(c) != "map") { return { ok: false, reason: "malformed", error: "Corrupt payload" }; }
    let now = 0;
    if (opts != nil and has(opts, "now")) { now = opts["now"]; }
    if (has(c, "exp") and now >= c["exp"]) { return { ok: false, reason: "expired", error: "The token has expired" }; }
    if (has(c, "nbf") and now < c["nbf"]) { return { ok: false, reason: "nbf", error: "The token is not valid yet" }; }
    if (opts != nil and has(opts, "iss") and (has(c, "iss") == false or c["iss"] != opts["iss"])) { return { ok: false, reason: "iss", error: "Unexpected issuer" }; }
    if (opts != nil and has(opts, "aud") and (has(c, "aud") == false or c["aud"] != opts["aud"])) { return { ok: false, reason: "aud", error: "Unexpected audience" }; }
    return { ok: true, claims: c };
}

// F: signed URL with expiry: ...?exp=N&sig=...
fun pcSignUrl(url, keyHex, expiresAt) {
    let sep = "?";
    if (contains(url, "?")) { sep = "&"; }
    let base = url + sep + "exp=" + toString(expiresAt);
    return base + "&sig=" + pcSign(keyHex, base);
}

fun pcVerifyUrl(url, keyHex, nowSec) {
    let parts = split(url, "&sig=");
    if (len(parts) < 2) { return { ok: false, error: "No signature" }; }
    let sig = parts[len(parts) - 1];
    let base = parts[0];
    let i = 1;
    while (i < len(parts) - 1) { base = base + "&sig=" + parts[i]; i = i + 1; }
    if (pcVerifySig(keyHex, base, sig) == false) { return { ok: false, error: "The URL signature is incorrect" }; }
    let ep = split(base, "exp=");
    let expTxt = ep[len(ep) - 1];
    let j = 0;
    if (len(expTxt) == 0) { return { ok: false, error: "Missing expiry" }; }
    while (j < len(expTxt)) {
        if (contains("0123456789", charAt(expTxt, j)) == false) { return { ok: false, error: "Corrupt expiry" }; }
        j = j + 1;
    }
    if (nowSec >= toNumber(expTxt)) { return { ok: false, error: "The URL has expired" }; }
    return { ok: true, expiresAt: toNumber(expTxt) };
}

// F: API request signing (canonical): METHOD\npath\nsha256(body)\nts\nnonce
fun pcRequestCanonical(method, path, body, ts, nonce) {
    return upper(method) + "\n" + path + "\n" + sec.sha256(toString(body)) + "\n" + toString(ts) + "\n" + nonce;
}

fun pcSignRequest(method, path, body, keyHex, ts, nonce) {
    let n = nonce;
    if (n == nil) { n = sec.randomToken(8); }
    let sig = pcSign(keyHex, pcRequestCanonical(method, path, body, ts, n));
    return { ok: true, ts: ts, nonce: n, sig: sig, headers: { "X-Pk-Timestamp": toString(ts), "X-Pk-Nonce": n, "X-Pk-Signature": sig } };
}

// F: verify with a time window and replay prevention (replay) via a shared nonces map (mutated in place)
fun pcVerifyRequest(method, path, body, keyHex, ts, nonce, sig, nowSec, skewSec, seen) {
    let d = nowSec - ts;
    if (d < 0) { d = 0 - d; }
    if (d > skewSec) { return { ok: false, reason: "skew", error: "The request is outside the time window" }; }
    if (seen != nil and has(seen, nonce)) { return { ok: false, reason: "replay", error: "nonce already used (replay)" }; }
    if (pcVerifySig(keyHex, pcRequestCanonical(method, path, body, ts, nonce), sig) == false) {
        return { ok: false, reason: "signature", error: "Incorrect request signature" };
    }
    if (seen != nil) { seen[nonce] = ts; }
    return { ok: true };
}

// ---- 9) One-time passwords (HOTP/TOTP) and recovery --------------------------------------------------------

// F: HOTP (RFC 4226 with HMAC-SHA256)
fun pcHotp(secretHex, counter, digits) {
    let d = 6;
    if (digits != nil) { d = digits; }
    let mac = pcHmacHex(secretHex, pcToHex(counter, 16));
    let off = pcByteAt(mac, 31) % 16;
    let bin = (pcByteAt(mac, off) % 128) * 16777216 + pcByteAt(mac, off + 1) * 65536 + pcByteAt(mac, off + 2) * 256 + pcByteAt(mac, off + 3);
    let code = toString(bin % pow(10, d));
    while (len(code) < d) { code = "0" + code; }
    return code;
}

// F: TOTP (RFC 6238 with SHA256)
fun pcTotp(secretHex, nowSec, step, digits) {
    let s = 30;
    if (step != nil) { s = step; }
    return pcHotp(secretHex, floor(nowSec / s), digits);
}

// F: verify with a window ±window (compensates for clock drift) in constant time
fun pcTotpVerify(secretHex, code, nowSec, window, digits) {
    let w = 1;
    if (window != nil) { w = window; }
    let found = false;
    let at = 0;
    let o = 0 - w;
    while (o <= w) {
        if (sec.constantTimeEqual(pcTotp(secretHex, nowSec + o * 30, 30, digits), toString(code))) { found = true; at = o; }
        o = o + 1;
    }
    return { ok: found, offset: at };
}

fun pcUrlEncode(s) {
    let hex = sec.hexEncode(toString(s));
    let keep = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
    let out = "";
    let i = 0;
    while (i < len(hex) / 2) {
        let b = pcByteAt(hex, i);
        if (b < 128 and contains(keep, chr(b))) { out = out + chr(b); }
        else { out = out + "%" + upper(pcToHex(b, 2)); }
        i = i + 1;
    }
    return out;
}

// F: an otpauth:// link for authenticator apps (QR)
fun pcOtpAuthUri(issuer, account, secretHex, opts) {
    let digits = 6;
    let period = 30;
    if (opts != nil) {
        if (has(opts, "digits")) { digits = opts["digits"]; }
        if (has(opts, "period")) { period = opts["period"]; }
    }
    return "otpauth://totp/" + pcUrlEncode(issuer) + ":" + pcUrlEncode(account) + "?secret=" + pcBase32EncodeHex(secretHex)
        + "&issuer=" + pcUrlEncode(issuer) + "&algorithm=SHA256&digits=" + toString(digits) + "&period=" + toString(period);
}

fun pcRecoveryNormalize(code) {
    return replace(replace(upper(toString(code)), "-", ""), " ", "");
}

// F: one-time recovery codes; shown once, only the hash is stored
fun pcRecoveryCodes(n) {
    let codes = [];
    let hashes = [];
    let i = 0;
    while (i < n) {
        let c = "";
        let j = 0;
        while (j < 10) { c = c + charAt(PC_B32, pcRandomInt(32)); j = j + 1; }
        push(codes, substr(c, 0, 5) + "-" + substr(c, 5, 5));
        push(hashes, sec.sha256("pcrc1:" + c));
        i = i + 1;
    }
    return { codes: codes, hashes: hashes };
}

// F: consume a recovery code: returns the remaining list (the code is used once)
fun pcRecoveryVerify(code, hashes) {
    let h = sec.sha256("pcrc1:" + pcRecoveryNormalize(code));
    let remaining = [];
    let hit = false;
    let i = 0;
    while (i < len(hashes)) {
        if (hit == false and sec.constantTimeEqual(hashes[i], h)) { hit = true; }
        else { push(remaining, hashes[i]); }
        i = i + 1;
    }
    return { ok: hit, remaining: remaining };
}

// ---- 10) Secret sharing (Shamir) over GF(256) -------------------------------------------------------------------

let PC_GF = { exp: [], log: [], ready: false };

fun pcGfAdd(a, b) {
    let r = 0;
    let p = 1;
    let i = 0;
    while (i < 8) {
        let ba = floor(a / p) % 2;
        let bb = floor(b / p) % 2;
        if (ba != bb) { r = r + p; }
        p = p * 2;
        i = i + 1;
    }
    return r;
}

fun pcGfInit() {
    if (PC_GF["ready"]) { return nil; }
    let i = 0;
    while (i < 256) { push(PC_GF["log"], 0); i = i + 1; }
    let x = 1;
    i = 0;
    while (i < 255) {
        push(PC_GF["exp"], x);
        PC_GF["log"][x] = i;
        let two = x * 2;
        if (two >= 256) { two = pcGfAdd(two - 256, 27); }
        x = pcGfAdd(x, two);
        i = i + 1;
    }
    PC_GF["ready"] = true;
    return nil;
}

fun pcGfMul(a, b) {
    if (a == 0 or b == 0) { return 0; }
    return PC_GF["exp"][(PC_GF["log"][a] + PC_GF["log"][b]) % 255];
}

fun pcGfDiv(a, b) {
    if (a == 0) { return 0; }
    return PC_GF["exp"][(PC_GF["log"][a] - PC_GF["log"][b] + 255) % 255];
}

// F: split a secret (hex) into n shares; k are enough to recover, and fewer than k reveal nothing
fun pcShamirSplit(secretHex, n, k) {
    if (pcIsHex(secretHex) == false or len(secretHex) == 0) { return { ok: false, error: "The secret must be non-empty hex" }; }
    if (k < 2 or n < k or n > 255) { return { ok: false, error: "need 2 <= k <= n <= 255" }; }
    pcGfInit();
    let nb = len(secretHex) / 2;
    let ys = [];
    let s = 0;
    while (s < n) { push(ys, ""); s = s + 1; }
    let b = 0;
    while (b < nb) {
        let coef = [pcByteAt(secretHex, b)];
        let c = 1;
        while (c < k) { push(coef, pcRandomInt(256)); c = c + 1; }
        let xi = 1;
        while (xi <= n) {
            let y = 0;
            let d = k - 1;
            while (d >= 0) {
                y = pcGfAdd(pcGfMul(y, xi), coef[d]);
                d = d - 1;
            }
            ys[xi - 1] = ys[xi - 1] + pcToHex(y, 2);
            xi = xi + 1;
        }
        b = b + 1;
    }
    let shares = [];
    let i = 0;
    while (i < n) { push(shares, pcToHex(i + 1, 2) + "-" + ys[i]); i = i + 1; }
    return { ok: true, shares: shares, threshold: k };
}

// F: recover the secret from k or more shares (Lagrange at zero)
fun pcShamirCombine(shares) {
    if (len(shares) < 2) { return { ok: false, error: "at least two shares" }; }
    pcGfInit();
    let xs = [];
    let yh = [];
    let i = 0;
    while (i < len(shares)) {
        let p = split(shares[i], "-");
        if (len(p) != 2 or pcIsHex(p[0]) == false or len(p[0]) != 2 or pcIsHex(p[1]) == false) { return { ok: false, error: "Corrupt share" }; }
        let x = pcHexToNum(p[0]);
        if (x == 0 or contains(xs, x)) { return { ok: false, error: "Duplicate or invalid share" }; }
        if (len(yh) > 0 and len(p[1]) != len(yh[0])) { return { ok: false, error: "Share lengths differ" }; }
        push(xs, x);
        push(yh, p[1]);
        i = i + 1;
    }
    let nb = len(yh[0]) / 2;
    let out = "";
    let b = 0;
    while (b < nb) {
        let acc = 0;
        let j = 0;
        while (j < len(xs)) {
            let num = 1;
            let den = 1;
            let m = 0;
            while (m < len(xs)) {
                if (m != j) {
                    num = pcGfMul(num, xs[m]);
                    den = pcGfMul(den, pcGfAdd(xs[m], xs[j]));
                }
                m = m + 1;
            }
            acc = pcGfAdd(acc, pcGfMul(pcByteAt(yh[j], b), pcGfDiv(num, den)));
            j = j + 1;
        }
        out = out + pcToHex(acc, 2);
        b = b + 1;
    }
    return { ok: true, secretHex: out };
}
)PKCRYPTOGRIN";
static const char* kLib_passkitdb_og_rin = R"PKDBOGRIN(
// ============================================================================
//  lib/passkitdb.og.rin — Database and container layer for the Passkit family (prefix pd)
//  Import:  @import "lib/passkitdb.og.rin";
//
//  Builds on Rin's native document containers (spawn("doc",...) + insertDoc/findDoc/queryDocs/
//  updateDoc/deleteDoc/docIds + RCSQL) and on passkit (pk*) and passkitcrypt (pc*).
//  Does not modify the engine. Time is always passed explicitly (nowSec) because Rin has no wall clock.
//
//  Sections:  1) CRUD and rules  2) Field encryption and the blind index  3) security models (users/sessions/
//          API keys/audit/rate limiting/2FA)  4) Generic container (container) linking
//  Convention: {ok:true,...} or {ok:false,error}.
// ============================================================================

@import "passkit";
@import "passkitcrypt";

let PDS = { tx: {} };

// ---- Helpers ---------------------------------------------------------------------------

fun pdOpt(opts, k, fallback) {
    if (opts == nil) { return fallback; }
    if (has(opts, k)) { return opts[k]; }
    return fallback;
}

fun pdCopy(m) {
    let out = {};
    for (let k in keys(m)) { out[k] = m[k]; }
    return out;
}

// Canonical text representation (sorted keys) of any value — the basis of integrity checks and hashing
fun pdCanon(v) {
    let t = type(v);
    if (t == "map") {
        let parts = [];
        for (let k in sort(keys(v))) { push(parts, toString(k) + ":" + pdCanon(v[k])); }
        return "{" + join(parts, ",") + "}";
    }
    if (t == "array") {
        let parts = [];
        let i = 0;
        while (i < len(v)) { push(parts, pdCanon(v[i])); i = i + 1; }
        return "[" + join(parts, ",") + "]";
    }
    if (t == "string") { return "\"" + v + "\""; }
    return toString(v);
}

fun pdPad(n, width) {
    let s = toString(n);
    while (len(s) < width) { s = "0" + s; }
    return s;
}

// ---- 1) CRUD and databases ------------------------------------------------------------------

// F: create a table (a doc container) if it does not exist
fun pdCreate(table) {
    if (hasContainer(table)) { return { ok: true, created: false }; }
    spawn("doc", table);
    return { ok: true, created: true };
}

fun pdExists(table) { return hasContainer(table); }

// F: drop a whole table
fun pdDrop(table) {
    if (hasContainer(table) == false) { return { ok: false, error: "Table not found: " + table }; }
    container.remove(table);
    return { ok: true };
}

// F: list of tables (doc containers only)
fun pdTables() {
    let out = [];
    for (let n in container.names()) {
        if (kindOf(n) == "doc") { push(out, n); }
    }
    return out;
}

fun pdCount(table) {
    if (hasContainer(table) == false) { return 0; }
    return len(docIds(table));
}

fun pdIds(table) {
    if (hasContainer(table) == false) { return []; }
    return docIds(table);
}

// F: insert a document; opts: { id, now, unique:"field" }
fun pdInsert(table, rw, opts) {
    pdCreate(table);
    let id = pdOpt(opts, "id", sec.randomToken(8));
    if (findDoc(table, id) != nil) { return { ok: false, error: "The id already exists: " + id }; }
    let uf = pdOpt(opts, "unique", nil);
    if (uf != nil and has(rw, uf) and len(queryDocs(table, uf, rw[uf])) > 0) {
        return { ok: false, error: "Duplicate value in the unique field: " + uf };
    }
    let r = pdCopy(rw);
    let t = pdOpt(opts, "now", nil);
    if (t != nil) { r["createdAt"] = t; r["updatedAt"] = t; }
    insertDoc(table, id, r);
    return { ok: true, id: id };
}

// F: read a document by its id
fun pdGet(table, id) {
    if (hasContainer(table) == false) { return { ok: false, error: "Table not found: " + table }; }
    let d = findDoc(table, id);
    if (d == nil) { return { ok: false, error: "Not found: " + id }; }
    let out = pdCopy(d);
    out["_id"] = id;
    return { ok: true, doc: out };
}

// F: update fields (opts.now opts.now updates updatedAt)
fun pdUpdate(table, id, fields, opts) {
    let g = pdGet(table, id);
    if (g["ok"] == false) { return g; }
    let f = pdCopy(fields);
    let t = pdOpt(opts, "now", nil);
    if (t != nil) { f["updatedAt"] = t; }
    updateDoc(table, id, f);
    return { ok: true, id: id };
}

// F: insert or update
fun pdUpsert(table, id, rw, opts) {
    if (hasContainer(table) and findDoc(table, id) != nil) { return pdUpdate(table, id, rw, opts); }
    let o = {};
    if (opts != nil) { o = pdCopy(opts); }
    o["id"] = id;
    return pdInsert(table, rw, o);
}

fun pdDelete(table, id) {
    if (hasContainer(table) == false or findDoc(table, id) == nil) { return { ok: false, error: "Not found: " + id }; }
    deleteDoc(table, id);
    return { ok: true };
}

// F: all documents (with _id)
fun pdAll(table) {
    let out = [];
    if (hasContainer(table) == false) { return out; }
    for (let id in docIds(table)) {
        let d = pdCopy(findDoc(table, id));
        d["_id"] = id;
        push(out, d);
    }
    return out;
}

// F: search by exact equality (accepts any characters such as @ and .)
fun pdFind(table, field, value) {
    if (hasContainer(table) == false) { return []; }
    return queryDocs(table, field, value);
}

fun pdFindOne(table, field, value) {
    let r = pdFind(table, field, value);
    if (len(r) == 0) { return nil; }
    return r[0];
}

// F: RCSQL query validated beforehand
fun pdWhere(table, rcsql) {
    if (hasContainer(table) == false) { return { ok: true, rows: [] }; }
    let q = table;
    if (rcsql != nil and rcsql != "") { q = table + " & " + rcsql; }
    let v = sqlValidate(q);
    if (v["ok"] == false) { return { ok: false, error: "RCSQL: " + toString(v["error"]) }; }
    return { ok: true, rows: sql(q) };
}

// F: pagination (page starts from 1)
fun pdPage(table, page, size) {
    let all = pdAll(table);
    let start = (page - 1) * size;
    let rows = [];
    let i = start;
    while (i < len(all) and i < start + size) { push(rows, all[i]); i = i + 1; }
    return { rows: rows, page: page, size: size, total: len(all), pages: ceil(len(all) / size) };
}

// F: insert with a unique field
fun pdInsertUnique(table, rw, field, opts) {
    let o = {};
    if (opts != nil) { o = pdCopy(opts); }
    o["unique"] = field;
    return pdInsert(table, rw, o);
}

// F: validate a document against a schema: { field: { type, required, min, max, oneOf, startsWith } }
fun pdValidate(rw, schema) {
    let errors = [];
    for (let f in keys(schema)) {
        let rule = schema[f];
        let present = has(rw, f) and rw[f] != nil;
        if (present == false) {
            if (has(rule, "required") and rule["required"]) { push(errors, f + ": required"); }
        } else {
            let v = rw[f];
            if (has(rule, "type") and type(v) != rule["type"]) { push(errors, f + ": type must be " + rule["type"]); }
            else {
                let size = v;
                if (type(v) == "string") { size = len(v); }
                if (type(v) == "array") { size = len(v); }
                if (type(size) == "number") {
                    if (has(rule, "min") and size < rule["min"]) { push(errors, f + ": below the minimum " + toString(rule["min"])); }
                    if (has(rule, "max") and size > rule["max"]) { push(errors, f + ": above the maximum " + toString(rule["max"])); }
                }
                if (has(rule, "oneOf") and contains(rule["oneOf"], v) == false) { push(errors, f + ": value not allowed"); }
                if (has(rule, "startsWith") and type(v) == "string" and indexOf(v, rule["startsWith"]) != 0) { push(errors, f + ": must start with " + rule["startsWith"]); }
            }
        }
    }
    if (len(errors) == 0) { return { ok: true }; }
    return { ok: false, errors: errors, error: join(errors, "; ") };
}

// F: soft delete, restore and cleanup
fun pdSoftDelete(table, id, nowSec) {
    return pdUpdate(table, id, { deletedAt: nowSec }, nil);
}

fun pdRestore(table, id) {
    let g = pdGet(table, id);
    if (g["ok"] == false) { return g; }
    updateDoc(table, id, { deletedAt: nil });
    return { ok: true, id: id };
}

fun pdActive(table) {
    let out = [];
    for (let d in pdAll(table)) {
        if (has(d, "deletedAt") == false or d["deletedAt"] == nil) { push(out, d); }
    }
    return out;
}

// F: permanent delete of what was soft-deleted more than olderThanSec seconds ago
fun pdPurgeDeleted(table, olderThanSec, nowSec) {
    let n = 0;
    for (let d in pdAll(table)) {
        if (has(d, "deletedAt") and d["deletedAt"] != nil and nowSec - d["deletedAt"] >= olderThanSec) {
            deleteDoc(table, d["_id"]);
            n = n + 1;
        }
    }
    return { ok: true, purged: n };
}

// F: persistent sequential counter per (table, name)
fun pdNextId(table, name) {
    pdCreate("_pd_seq");
    let id = table + "." + name;
    let d = findDoc("_pd_seq", id);
    let n = 1;
    if (d == nil) { insertDoc("_pd_seq", id, { n: 1 }); }
    else { n = d["n"] + 1; updateDoc("_pd_seq", id, { n: n }); }
    return n;
}

// F: transactions (Transactions) with snapshot and rollback
fun pdBegin(table) {
    if (hasContainer(table) == false) { return { ok: false, error: "Table not found" }; }
    if (has(PDS["tx"], table)) { return { ok: false, error: "A transaction is already open" }; }
    PDS["tx"][table] = pdAll(table);
    return { ok: true };
}

fun pdInTx(table) { return has(PDS["tx"], table); }

fun pdCommit(table) {
    if (has(PDS["tx"], table) == false) { return { ok: false, error: "No transaction is open" }; }
    let fresh = {};
    for (let k in keys(PDS["tx"])) { if (k != table) { fresh[k] = PDS["tx"][k]; } }
    PDS["tx"] = fresh;
    return { ok: true };
}

fun pdRollback(table) {
    if (has(PDS["tx"], table) == false) { return { ok: false, error: "No transaction is open" }; }
    let snap = PDS["tx"][table];
    for (let id in docIds(table)) { deleteDoc(table, id); }
    for (let d in snap) {
        let id = d["_id"];
        let r = pdCopy(d);
        let fresh = {};
        for (let k in keys(r)) { if (k != "_id") { fresh[k] = r[k]; } }
        insertDoc(table, id, fresh);
    }
    return pdCommit(table);
}

// F: full JSON snapshot and import
fun pdExportJson(table) {
    return jsonEncode(pdAll(table));
}

fun pdImportJson(table, json, replace) {
    let rows = jsonDecode(json);
    if (type(rows) != "array") { return { ok: false, error: "JSON must be an array of documents" }; }
    pdCreate(table);
    if (replace == true) { for (let id in docIds(table)) { deleteDoc(table, id); } }
    let n = 0;
    for (let r in rows) {
        if (type(r) == "map" and has(r, "_id")) {
            let f = {};
            for (let k in keys(r)) { if (k != "_id") { f[k] = r[k]; } }
            insertDoc(table, r["_id"], f);
            n = n + 1;
        }
    }
    return { ok: true, imported: n };
}

// F: integrity fingerprint for the whole table (id order does not matter)
fun pdChecksum(table) {
    let parts = [];
    for (let d in pdAll(table)) { push(parts, d["_id"] + "=" + sec.sha256(pdCanon(d))); }
    return sec.sha256(join(sort(parts), "|"));
}

// F: schema migrations (migrations): steps = [{version:1, run: fun(){...}}] are applied in order, once
fun pdMigrate(name, steps) {
    pdCreate("_pd_meta");
    let id = "mig:" + name;
    let cur = 0;
    let d = findDoc("_pd_meta", id);
    if (d != nil) { cur = d["version"]; }
    let from = cur;
    let applied = [];
    for (let st in sort_by_version(steps)) {
        if (st["version"] > cur) {
            callFn(st["run"], []);
            cur = st["version"];
            push(applied, cur);
        }
    }
    if (d == nil) { insertDoc("_pd_meta", id, { version: cur }); } else { updateDoc("_pd_meta", id, { version: cur }); }
    return { ok: true, from: from, to: cur, applied: applied };
}

fun sort_by_version(steps) {
    let out = [];
    for (let s in steps) { push(out, s); }
    let i = 0;
    while (i < len(out)) {
        let j = i + 1;
        while (j < len(out)) {
            if (out[j]["version"] < out[i]["version"]) { let t = out[i]; out[i] = out[j]; out[j] = t; }
            j = j + 1;
        }
        i = i + 1;
    }
    return out;
}

// ---- 2) Field encryption and the blind index -------------------------------------------------------------

// F: blind index: an HMAC of a normalized value with a sub-key — allows equality search without storing the plaintext
fun pdBlindIndex(keyHex, field, value) {
    let sub = pcDeriveSubkey(keyHex, "blind:" + field);
    return substr(pcHmacText(sub, lower(trim(toString(value)))), 0, 32);
}

fun pdFieldAad(table, id, field) { return table + "." + id + "." + field; }

// F: insert with encryption of certain fields (bound to the position through AAD so they cannot be moved between rows/fields)
// opts: { id, now, blind:[fields] }
fun pdInsertEnc(table, rw, keyHex, encFields, opts) {
    let id = pdOpt(opts, "id", sec.randomToken(8));
    let r = pdCopy(rw);
    let blind = pdOpt(opts, "blind", []);
    let done = [];
    for (let f in encFields) {
        if (has(r, f)) {
            let s = pcSealObject(keyHex, { v: r[f] }, pdFieldAad(table, id, f));
            if (s["ok"] == false) { return s; }
            if (contains(blind, f)) { r[f + "_bi"] = pdBlindIndex(keyHex, f, r[f]); }
            r[f] = s["sealed"];
            push(done, f);
        }
    }
    r["_enc"] = done;
    let o = {};
    if (opts != nil) { o = pdCopy(opts); }
    o["id"] = id;
    return pdInsert(table, r, o);
}

fun pdDecryptRow(table, id, rw, keyHex) {
    let out = pdCopy(rw);
    out["_id"] = id;
    if (has(rw, "_enc")) {
        for (let f in rw["_enc"]) {
            let o = pcOpenObject(keyHex, rw[f], pdFieldAad(table, id, f));
            if (o["ok"] == false) { return { ok: false, error: "Could not decrypt the field " + f + ": " + o["error"] }; }
            out[f] = o["value"]["v"];
        }
    }
    return { ok: true, doc: out };
}

// F: read with decryption
fun pdGetDec(table, id, keyHex) {
    let g = pdGet(table, id);
    if (g["ok"] == false) { return g; }
    return pdDecryptRow(table, id, g["doc"], keyHex);
}

// F: search by the blind index (the plain value is not stored)
fun pdFindByBlind(table, field, value, keyHex) {
    let rows = pdFind(table, field + "_bi", pdBlindIndex(keyHex, field, value));
    let out = [];
    for (let r in rows) { push(out, r); }
    return out;
}

// F: database key rotation: re-encrypt all rows from an old key to a new one
fun pdReencrypt(table, oldKey, newKey, encFields, blind) {
    let n = 0;
    for (let d in pdAll(table)) {
        let id = d["_id"];
        let dec = pdDecryptRow(table, id, d, oldKey);
        if (dec["ok"] == false) { return dec; }
        let upd = {};
        for (let f in encFields) {
            if (has(dec["doc"], f)) {
                let s = pcSealObject(newKey, { v: dec["doc"][f] }, pdFieldAad(table, id, f));
                upd[f] = s["sealed"];
                if (contains(blind, f)) { upd[f + "_bi"] = pdBlindIndex(newKey, f, dec["doc"][f]); }
            }
        }
        updateDoc(table, id, upd);
        n = n + 1;
    }
    return { ok: true, reencrypted: n };
}

// F: encrypted backup of the whole table (authenticated and bound to the table name)
fun pdExportEncrypted(table, keyHex) {
    return pcSeal(keyHex, pdExportJson(table), "pdexport:" + table);
}

// sourceTable (optional): the table name the backup was exported from, when restoring under a new name
fun pdImportEncrypted(table, keyHex, sealed, replace, sourceTable) {
    let src = table;
    if (sourceTable != nil) { src = sourceTable; }
    let o = pcOpen(keyHex, sealed, "pdexport:" + src);
    if (o["ok"] == false) { return o; }
    return pdImportJson(table, o["plaintext"], replace);
}

// ---- 3) Ready-made security models ----------------------------------------------------------------------------

fun pdEmailNorm(email) { return lower(trim(toString(email))); }

fun pdEmailLooksValid(email) {
    let p = split(email, "@");
    if (len(p) != 2 or len(p[0]) < 1 or len(p[1]) < 3 or contains(p[1], ".") == false or contains(email, " ")) { return false; }
    return true;
}

// F: users: the email is encrypted + a blind index for search, and the password is only a hash (pkHash) with a policy
// opts: { policy, now, id, extra:{...} }
fun pdUserCreate(table, keyHex, email, password, opts) {
    let em = pdEmailNorm(email);
    if (pdEmailLooksValid(em) == false) { return { ok: false, error: "Invalid email" }; }
    if (hasContainer(table) and len(pdFindByBlind(table, "email", em, keyHex)) > 0) {
        return { ok: false, error: "The email is already registered" };
    }
    let policy = pdOpt(opts, "policy", pkPolicyStandard());
    let nowSec = pdOpt(opts, "now", 0);
    let rec = pkRecordNew(password, policy, { email: em }, nowSec);
    if (rec["ok"] == false) { return rec; }
    let rw = pdCopy(rec["record"]);
    rw["email"] = em;
    let extra = pdOpt(opts, "extra", {});
    for (let k in keys(extra)) { rw[k] = extra[k]; }
    return pdInsertEnc(table, rw, keyHex, ["email"], { blind: ["email"], id: pdOpt(opts, "id", sec.randomToken(8)), now: nowSec });
}

fun pdUserRecord(d) {
    return { hash: d["hash"], createdAt: d["createdAt"], changedAt: d["changedAt"], expiresAt: d["expiresAt"],
             history: d["history"], failedAttempts: d["failedAttempts"], lockedUntil: d["lockedUntil"], mustChange: d["mustChange"] };
}

// F: login: a uniform message (does not reveal whether the email exists) + runs a dummy hash to equalize timing + lockout after attempts
fun pdUserLogin(table, keyHex, email, password, opts) {
    let em = pdEmailNorm(email);
    let policy = pdOpt(opts, "policy", pkPolicyStandard());
    let nowSec = pdOpt(opts, "now", 0);
    let rows = [];
    if (hasContainer(table)) { rows = pdFindByBlind(table, "email", em, keyHex); }
    if (len(rows) == 0) {
        pkVerify(password, pkHash("dummy", { iterations: policy["hashIterations"], salt: "00000000000000000000000000000000" }));
        return { ok: false, reason: "wrong", error: "Incorrect login details" };
    }
    let rw = rows[0];
    let id = "";
    for (let d in pdAll(table)) { if (d["hash"] == rw["hash"]) { id = d["_id"]; } }
    let r = pkLogin(pdUserRecord(rw), password, policy, nowSec);
    let upd = {};
    for (let k in keys(r["record"])) { upd[k] = r["record"][k]; }
    updateDoc(table, id, upd);
    if (r["ok"] == false) {
        if (r["reason"] == "locked") { return { ok: false, reason: "locked", retryAfter: r["retryAfter"], error: "The account is temporarily locked" }; }
        return { ok: false, reason: "wrong", error: "Incorrect login details" };
    }
    return { ok: true, id: id, reason: r["reason"], mustChange: r["mustChange"] };
}

// F: change password (policy + reuse prevention + history)
fun pdUserChangePassword(table, keyHex, id, oldPw, newPw, opts) {
    let g = pdGet(table, id);
    if (g["ok"] == false) { return g; }
    let policy = pdOpt(opts, "policy", pkPolicyStandard());
    let nowSec = pdOpt(opts, "now", 0);
    let c = pkChange(pdUserRecord(g["doc"]), oldPw, newPw, policy, nil, nowSec);
    if (c["ok"] == false) { return c; }
    updateDoc(table, id, c["record"]);
    return { ok: true, id: id };
}

// F: sessions: the token is delivered once and only its sha256 is stored (a database leak does not reveal sessions)
fun pdSessionCreate(table, userId, ttlSec, nowSec, meta) {
    pdCreate(table);
    let token = sec.randomToken(32);
    let rw = { userId: userId, createdAt: nowSec, expiresAt: nowSec + ttlSec, revoked: false };
    if (meta != nil) { for (let k in keys(meta)) { rw["meta_" + k] = meta[k]; } }
    insertDoc(table, sec.sha256(token), rw);
    return { ok: true, token: token, expiresAt: nowSec + ttlSec };
}

fun pdSessionCheck(table, token, nowSec) {
    if (hasContainer(table) == false or type(token) != "string") { return { ok: false, reason: "invalid" }; }
    let d = findDoc(table, sec.sha256(token));
    if (d == nil) { return { ok: false, reason: "invalid" }; }
    if (d["revoked"]) { return { ok: false, reason: "revoked" }; }
    if (nowSec >= d["expiresAt"]) { return { ok: false, reason: "expired" }; }
    return { ok: true, userId: d["userId"], expiresAt: d["expiresAt"] };
}

fun pdSessionRevoke(table, token) {
    let id = sec.sha256(token);
    if (hasContainer(table) == false or findDoc(table, id) == nil) { return { ok: false, error: "Session not found" }; }
    updateDoc(table, id, { revoked: true });
    return { ok: true };
}

// F: revoke all of a user's sessions (sign out from all devices)
fun pdSessionRevokeUser(table, userId) {
    let n = 0;
    for (let d in pdFind(table, "userId", userId)) {
        if (d["revoked"] == false) { n = n + 1; }
    }
    for (let id in pdIds(table)) {
        let d = findDoc(table, id);
        if (d["userId"] == userId and d["revoked"] == false) { updateDoc(table, id, { revoked: true }); }
    }
    return { ok: true, revoked: n };
}

fun pdSessionPurge(table, nowSec) {
    let n = 0;
    for (let id in pdIds(table)) {
        let d = findDoc(table, id);
        if (d["revoked"] or nowSec >= d["expiresAt"]) { deleteDoc(table, id); n = n + 1; }
    }
    return { ok: true, purged: n };
}

// F: API keys: <prefix>_<id>_<secret>; only sha256 is stored + scopes (scopes) + owner + expiry
fun pdApiKeyCreate(table, owner, scopes, opts) {
    pdCreate(table);
    let prefix = pdOpt(opts, "prefix", "pk");
    let nowSec = pdOpt(opts, "now", 0);
    let id = sec.randomToken(4);
    let key = prefix + "_" + id + "_" + sec.randomToken(24);
    let rw = { owner: owner, scopes: scopes, hash: sec.sha256(key), createdAt: nowSec, revoked: false, expiresAt: 0 };
    if (has(opts, "ttl")) { rw["expiresAt"] = nowSec + opts["ttl"]; }
    insertDoc(table, id, rw);
    return { ok: true, key: key, id: id };
}

fun pdApiKeyVerify(table, key, nowSec) {
    if (type(key) != "string" or hasContainer(table) == false) { return { ok: false, reason: "invalid" }; }
    // the prefix itself may contain underscores (sk_live), so read id and secret from the END
    let parts = split(key, "_");
    if (len(parts) < 3) { return { ok: false, reason: "invalid" }; }
    let kid = parts[len(parts) - 2];
    let d = findDoc(table, kid);
    if (d == nil) { return { ok: false, reason: "invalid" }; }
    if (sec.constantTimeEqual(sec.sha256(key), d["hash"]) == false) { return { ok: false, reason: "invalid" }; }
    if (d["revoked"]) { return { ok: false, reason: "revoked" }; }
    if (d["expiresAt"] > 0 and nowSec >= d["expiresAt"]) { return { ok: false, reason: "expired" }; }
    return { ok: true, owner: d["owner"], scopes: d["scopes"], id: kid };
}

fun pdApiKeyHasScope(result, scope) {
    if (result["ok"] == false) { return false; }
    return contains(result["scopes"], scope) or contains(result["scopes"], "*");
}

fun pdApiKeyRevoke(table, id) {
    if (hasContainer(table) == false or findDoc(table, id) == nil) { return { ok: false, error: "Key not found" }; }
    updateDoc(table, id, { revoked: true });
    return { ok: true };
}

// F: key rotation: a new one is created with the same scopes and the old one is revoked (replacedBy)
fun pdApiKeyRotate(table, id, nowSec) {
    let d = findDoc(table, id);
    if (d == nil) { return { ok: false, error: "Key not found" }; }
    let n = pdApiKeyCreate(table, d["owner"], d["scopes"], { now: nowSec });
    updateDoc(table, id, { revoked: true, replacedBy: n["id"] });
    return n;
}

// F: audit log linked by a hash chain (any edit/delete is detected)
fun pdAuditLog(table, event, actor, data, nowSec) {
    pdCreate(table);
    pdCreate("_pd_meta");
    let hid = "audit:" + table;
    let head = findDoc("_pd_meta", hid);
    let prev = PC_ZERO32;
    if (head != nil) { prev = head["hash"]; }
    let seq = pdNextId(table, "audit");
    let entry = { seq: seq, event: event, actor: actor, data: data, at: nowSec, prev: prev };
    let h = sec.sha256(prev + "|" + pdCanon(entry));
    entry["hash"] = h;
    insertDoc(table, pdPad(seq, 10), entry);
    if (head == nil) { insertDoc("_pd_meta", hid, { hash: h }); } else { updateDoc("_pd_meta", hid, { hash: h }); }
    return { ok: true, seq: seq, hash: h };
}

fun pdAuditVerify(table) {
    let prev = PC_ZERO32;
    let n = 0;
    let ids = sort(pdIds(table));
    for (let id in ids) {
        let e = pdCopy(findDoc(table, id));
        let stored = e["hash"];
        let body = {};
        for (let k in keys(e)) { if (k != "hash") { body[k] = e[k]; } }
        if (e["prev"] != prev or sec.sha256(prev + "|" + pdCanon(body)) != stored) { return { ok: false, brokenAt: id }; }
        prev = stored;
        n = n + 1;
    }
    let head = findDoc("_pd_meta", "audit:" + table);
    if (head != nil and head["hash"] != prev) { return { ok: false, brokenAt: "head (deleted from the end?)" }; }
    return { ok: true, entries: n };
}

// F: rate limit with a fixed window per key (IP/user/API key)
fun pdRateLimit(table, key, maxHits, windowSec, nowSec) {
    pdCreate(table);
    let id = substr(sec.sha256(toString(key)), 0, 16);
    let d = findDoc(table, id);
    if (d == nil or nowSec - d["start"] >= windowSec) {
        if (d == nil) { insertDoc(table, id, { count: 1, start: nowSec }); }
        else { updateDoc(table, id, { count: 1, start: nowSec }); }
        return { ok: true, remaining: maxHits - 1, retryAfter: 0 };
    }
    if (d["count"] >= maxHits) { return { ok: false, remaining: 0, retryAfter: d["start"] + windowSec - nowSec }; }
    updateDoc(table, id, { count: d["count"] + 1 });
    return { ok: true, remaining: maxHits - d["count"] - 1, retryAfter: 0 };
}

// F: one-time reset tokens (stored hashed)
fun pdResetCreate(table, userId, ttlSec, nowSec) {
    pdCreate(table);
    let token = sec.randomToken(32);
    insertDoc(table, sec.sha256(token), { userId: userId, expiresAt: nowSec + ttlSec, used: false });
    return { ok: true, token: token, expiresAt: nowSec + ttlSec };
}

fun pdResetConsume(table, token, nowSec) {
    if (hasContainer(table) == false or type(token) != "string") { return { ok: false, reason: "invalid" }; }
    let id = sec.sha256(token);
    let d = findDoc(table, id);
    if (d == nil) { return { ok: false, reason: "invalid" }; }
    if (d["used"]) { return { ok: false, reason: "used" }; }
    if (nowSec >= d["expiresAt"]) { return { ok: false, reason: "expired" }; }
    updateDoc(table, id, { used: true });
    return { ok: true, userId: d["userId"] };
}

// F: replay prevention: true only for the first use of a nonce
fun pdNonceUse(table, nonce, nowSec, ttlSec) {
    pdCreate(table);
    let id = substr(sec.sha256(nonce), 0, 32);
    let d = findDoc(table, id);
    if (d != nil and nowSec < d["expiresAt"]) { return false; }
    if (d == nil) { insertDoc(table, id, { expiresAt: nowSec + ttlSec }); }
    else { updateDoc(table, id, { expiresAt: nowSec + ttlSec }); }
    return true;
}

// F: enable 2FA (TOTP): the secret is encrypted with the key, and recovery codes are hashed
fun pdTotpEnroll(table, keyHex, userId, issuer, account) {
    pdCreate(table);
    let secret = sec.randomToken(20);
    let s = pcSeal(keyHex, secret, pdFieldAad(table, userId, "secret"));
    let rc = pcRecoveryCodes(8);
    let rw = { secret: s["sealed"], recovery: rc["hashes"], lastStep: 0, enabled: true };
    if (findDoc(table, userId) == nil) { insertDoc(table, userId, rw); } else { updateDoc(table, userId, rw); }
    return { ok: true, uri: pcOtpAuthUri(issuer, account, secret, nil), secretBase32: pcBase32EncodeHex(secret), recoveryCodes: rc["codes"] };
}

// F: 2FA check that prevents reuse of the same code (lastStep) or one-time recovery alternatives
fun pdTotpCheck(table, keyHex, userId, code, nowSec) {
    let d = findDoc(table, userId);
    if (d == nil or d["enabled"] == false) { return { ok: false, reason: "not_enrolled" }; }
    let o = pcOpen(keyHex, d["secret"], pdFieldAad(table, userId, "secret"));
    if (o["ok"] == false) { return { ok: false, reason: "key" }; }
    let step = floor(nowSec / 30);
    let v = pcTotpVerify(o["plaintext"], code, nowSec, 1, 6);
    if (v["ok"]) {
        let used = step + v["offset"];
        if (used <= d["lastStep"]) { return { ok: false, reason: "replay" }; }
        updateDoc(table, userId, { lastStep: used });
        return { ok: true, method: "totp" };
    }
    let rv = pcRecoveryVerify(code, d["recovery"]);
    if (rv["ok"]) {
        updateDoc(table, userId, { recovery: rv["remaining"] });
        return { ok: true, method: "recovery", remaining: len(rv["remaining"]) };
    }
    return { ok: false, reason: "invalid" };
}

// ---- 4) Linking generic containers (container) ------------------------------------------------------------------

// F: ensure a container of a kind exists (plain by default)
fun pdContEnsure(name, kind) {
    if (hasContainer(name)) { return { ok: true, created: false, kind: kindOf(name) }; }
    let k = "plain";
    if (kind != nil) { k = kind; }
    spawn(k, name);
    return { ok: true, created: true, kind: k };
}

fun pdContGet(name, field) {
    if (hasContainer(name) == false) { return nil; }
    return getField(name, field);
}

fun pdContSet(name, field, value) {
    pdContEnsure(name, nil);
    setField(name, field, value);
    return { ok: true };
}

fun pdContHas(name, field) {
    if (hasContainer(name) == false) { return false; }
    return getField(name, field) != nil;
}

fun pdContDelete(name, field) {
    if (hasContainer(name) == false) { return { ok: false, error: "The container does not exist" }; }
    return { ok: container.deleteField(name, field) };
}

// F: all container fields as a map
fun pdContToMap(name) {
    let out = {};
    if (hasContainer(name) == false) { return out; }
    for (let e in container.entries(name)) { out[e[0]] = e[1]; }
    return out;
}

// F: write a whole map into a container (prefix optional)
fun pdContFromMap(name, m, prefix) {
    pdContEnsure(name, nil);
    let p = "";
    if (prefix != nil) { p = prefix; }
    let n = 0;
    for (let k in keys(m)) { setField(name, p + k, m[k]); n = n + 1; }
    return { ok: true, written: n };
}

fun pdContFields(name) {
    return keys(pdContToMap(name));
}

fun pdContChecksum(name) {
    return sec.sha256(pdCanon(pdContToMap(name)));
}

fun pdContExport(name) {
    return jsonEncode(pdContToMap(name));
}

fun pdContImport(name, json) {
    let m = jsonDecode(json);
    if (type(m) != "map") { return { ok: false, error: "JSON must be an object" }; }
    return pdContFromMap(name, m, nil);
}

fun pdContClone(name, newName) {
    if (hasContainer(name) == false) { return { ok: false, error: "The container does not exist" }; }
    pdContEnsure(newName, kindOf(name));
    return pdContFromMap(newName, pdContToMap(name), nil);
}

// F: encrypt a field inside the container in place (AAD = container name.field) and decrypt it
fun pdContSeal(name, field, keyHex) {
    if (pdContHas(name, field) == false) { return { ok: false, error: "Field not found: " + field }; }
    let s = pcSealObject(keyHex, { v: getField(name, field) }, name + "." + field);
    if (s["ok"] == false) { return s; }
    setField(name, field, s["sealed"]);
    return { ok: true };
}

fun pdContOpen(name, field, keyHex) {
    if (pdContHas(name, field) == false) { return { ok: false, error: "Field not found: " + field }; }
    let o = pcOpenObject(keyHex, getField(name, field), name + "." + field);
    if (o["ok"] == false) { return o; }
    return { ok: true, value: o["value"]["v"] };
}

// F: encrypt all container fields at once / read them decrypted as a map without modifying the container
fun pdContSealAll(name, keyHex) {
    let n = 0;
    for (let f in pdContFields(name)) {
        let r = pdContSeal(name, f, keyHex);
        if (r["ok"] == false) { return r; }
        n = n + 1;
    }
    return { ok: true, sealed: n };
}

fun pdContOpenAll(name, keyHex) {
    let out = {};
    for (let f in pdContFields(name)) {
        let r = pdContOpen(name, f, keyHex);
        if (r["ok"] == false) { return r; }
        out[f] = r["value"];
    }
    return { ok: true, values: out };
}
)PKDBOGRIN";
static const char* kLib_passkitlang_og_rin = R"PASSKITLANGOGRIN(
// ============================================================================
//  lib/passkitlang.og.rin — interpreter of the <passkit> tag language as a library (auto-generated - do not edit by hand)
//  Source: examples/customlang/passkit/  ·  Generator: tools/gen_passkitlang.py
//  Import:
//    @import "lib/passkitlang.og.rin";
//
//  From Rin:
//    let r = passkitRun("signup.passkit", { email: "a@b.com" });   // {ok, output, vars, value, error, message}
//    print passkitGet(r, "pw.strength");
//    passkitRegister("double", fun(x) { return x * 2; });           // called from .passkit with <call fn="double" arg0="21"/>
//  From .passkit:  <import file> · <run file name in.k=...> · <call fn=...> · <input> · <return>
// ============================================================================

@import "langkit";
@import "passkit";
@import "passkitcrypt";
@import "passkitdb";

// ───────── Lexer.rin ─────────
// ============================================================================
//  Lexer.rin — Stage 1 of the language "Passkit": text .passkit -> tokens
//  Symbols: LT '<'  LTSLASH '</'  GT '>'  SLASHGT '/>'  IDENT  EQ  STRING
// ============================================================================


fun pkIdentChar(ch) {
    return isAlnumChar(ch) or ch == "-" or ch == "." or ch == ":";
}

fun pkLex(source) {
    let tokens = [];
    let i = 0;
    let line = 1;
    let n = len(source);
    let afterEq = false;

    while (i < n) {
        let ch = charAt(source, i);

        if (isSpaceChar(ch)) {
            i = i + 1;
        } else if (isNewlineChar(ch)) {
            line = line + 1;
            i = i + 1;

        // comment <!-- ... -->
        } else if (ch == "<" and substr(source, i, 4) == "<!--") {
            let close = indexOf(substr(source, i, n - i), "-->");
            if (close < 0) {
                push(tokens, langError("Lexer", "unterminated comment <!-- -->", line));
                i = n;
            } else {
                let body = substr(source, i, close + 3);
                let k = 0;
                while (k < len(body)) { if (charAt(body, k) == "\n") { line = line + 1; } k = k + 1; }
                i = i + close + 3;
            }

        } else if (ch == "<" and i + 1 < n and charAt(source, i + 1) == "/") {
            push(tokens, makeToken("LTSLASH", "</", line)); i = i + 2; afterEq = false;
        } else if (ch == "<") {
            push(tokens, makeToken("LT", "<", line)); i = i + 1; afterEq = false;
        } else if (ch == "/" and i + 1 < n and charAt(source, i + 1) == ">") {
            push(tokens, makeToken("SLASHGT", "/>", line)); i = i + 2; afterEq = false;
        } else if (ch == ">") {
            push(tokens, makeToken("GT", ">", line)); i = i + 1; afterEq = false;
        } else if (ch == "=") {
            push(tokens, makeToken("EQ", "=", line)); i = i + 1; afterEq = true;

        // attribute value between quotes "..." (supports any text including Arabic)
        } else if (ch == "\"") {
            let start = i + 1;
            i = i + 1;
            while (i < n and charAt(source, i) != "\"") {
                if (charAt(source, i) == "\n") { line = line + 1; }
                i = i + 1;
            }
            if (i >= n) {
                push(tokens, langError("Lexer", "unterminated quoted string", line));
            } else {
                push(tokens, makeToken("STRING", substr(source, start, i - start), line));
            }
            i = i + 1;
            afterEq = false;

        // unquoted value after = (e.g. length=16)
        } else if (afterEq) {
            let start = i;
            while (i < n and isSpaceChar(charAt(source, i)) == false and isNewlineChar(charAt(source, i)) == false
                   and charAt(source, i) != ">" and pkNotSlashGt(source, i, n)) {
                i = i + 1;
            }
            push(tokens, makeToken("STRING", substr(source, start, i - start), line));
            afterEq = false;

        // tag and attribute names: email / password / header.X-Key
        } else if (isAlphaChar(ch)) {
            let start = i;
            while (i < n and pkIdentChar(charAt(source, i))) { i = i + 1; }
            push(tokens, makeToken("IDENT", substr(source, start, i - start), line));

        } else {
            push(tokens, langError("Lexer", "unexpected character outside tags: '" + ch + "' (text is only written inside attribute values)", line));
            i = i + 1;
        }
    }

    push(tokens, eofToken(line));
    return tokens;
}

fun pkNotSlashGt(source, i, n) {
    if (charAt(source, i) == "/" and i + 1 < n and charAt(source, i + 1) == ">") { return false; }
    return true;
}

// ───────── Parser.rin ─────────
// ============================================================================
//  Parser.rin — Stage 2 of "Passkit": tokens -> AST
//
//    program  -> element* EOF
//    element  -> "<" IDENT attr* "/>"
//              | "<" IDENT attr* ">" element* "</" IDENT ">"
//    attr     -> IDENT ("=" STRING)?            // no value = flag (flag) with value "true"
// ============================================================================


fun pkKnownTags() {
    return ["passkit", "set", "print", "if", "else", "for", "assert",
            "email", "password", "apikey", "link", "api", "sql",
            "input", "return", "import", "run", "call",
            "crypt", "token", "otp", "db", "container"];
}

fun pkParseElement(tokens, pos) {
    let line = pPeek(tokens, pos)["line"];
    if (pCheck(tokens, pos, "LT") == false) {
        let got = pPeek(tokens, pos);
        return { node: langError("Parser", "expected a tag starting with '<' but found " + got["type"], got["line"]), pos: pos };
    }
    let a = pAdvance(tokens, pos); pos = a["pos"];
    if (pCheck(tokens, pos, "IDENT") == false) {
        return { node: langError("Parser", "expected a tag name after '<'", line), pos: pos };
    }
    let nameTok = pAdvance(tokens, pos); pos = nameTok["pos"];
    let name = nameTok["tok"]["value"];
    if (contains(pkKnownTags(), name) == false) {
        return { node: langError("Parser", "unknown tag <" + name + ">", line), pos: pos };
    }

    let attrs = {};
    while (pCheck(tokens, pos, "IDENT")) {
        let k = pAdvance(tokens, pos); pos = k["pos"];
        let key = k["tok"]["value"];
        if (pCheck(tokens, pos, "EQ")) {
            let e = pAdvance(tokens, pos); pos = e["pos"];
            if (pCheck(tokens, pos, "STRING") == false) {
                return { node: langError("Parser", "expected a string value for attribute '" + key + "'", line), pos: pos };
            }
            let v = pAdvance(tokens, pos); pos = v["pos"];
            attrs[key] = v["tok"]["value"];
        } else {
            attrs[key] = "true";
        }
    }

    if (pCheck(tokens, pos, "SLASHGT")) {
        let c = pAdvance(tokens, pos);
        return { node: astNode("Tag", line, { name: name, attrs: attrs, children: [] }), pos: c["pos"] };
    }
    if (pCheck(tokens, pos, "GT") == false) {
        return { node: langError("Parser", "expected '>' or '/>' to end the tag <" + name + ">", line), pos: pos };
    }
    let g = pAdvance(tokens, pos); pos = g["pos"];

    let children = [];
    while (pCheck(tokens, pos, "LTSLASH") == false) {
        if (pAtEnd(tokens, pos)) {
            return { node: langError("Parser", "tag <" + name + "> is not closed, expected </" + name + ">", line), pos: pos };
        }
        let r = pkParseElement(tokens, pos);
        if (isLangError(r["node"])) { return r; }
        push(children, r["node"]);
        pos = r["pos"];
    }
    let cl = pAdvance(tokens, pos); pos = cl["pos"];            // </
    if (pCheck(tokens, pos, "IDENT") == false) {
        return { node: langError("Parser", "expected a tag name after '</'", line), pos: pos };
    }
    let cn = pAdvance(tokens, pos); pos = cn["pos"];
    if (cn["tok"]["value"] != name) {
        return { node: langError("Parser", "closing </" + cn["tok"]["value"] + "> does not match <" + name + ">", cn["tok"]["line"]), pos: pos };
    }
    if (pCheck(tokens, pos, "GT") == false) {
        return { node: langError("Parser", "expected '>' after </" + name, line), pos: pos };
    }
    let fin = pAdvance(tokens, pos);
    return { node: astNode("Tag", line, { name: name, attrs: attrs, children: children }), pos: fin["pos"] };
}

fun pkParse(tokens) {
    let pos = 0;
    let body = [];
    while (pAtEnd(tokens, pos) == false) {
        let r = pkParseElement(tokens, pos);
        if (isLangError(r["node"])) { return r["node"]; }
        push(body, r["node"]);
        pos = r["pos"];
    }
    return astNode("Program", 1, { body: body });
}

// ───────── Interpreter.rin ─────────
// ============================================================================
//  Interpreter.rin — Executing the language "Passkit" (tags <>): AST -> results
//
//  Tags:  <passkit> <set> <print> <if>/<else> <for> <assert>
//           <email> <password> <apikey> <link> <api> <sql>
//  every tag carries name="x" so that it stores its result in variable x, and it is read later with $x or $x.field
//  and variables go inside the text in the form {x.field}.
// ============================================================================


let PKS = { vars: {}, out: [], handlers: {}, base: "", depth: 0, stack: [], returned: false, ret: nil, binds: [] };

// ---- Values and variables ------------------------------------------------------------

fun psReset() {
    PKS["vars"] = {};
    PKS["out"] = [];
    PKS["returned"] = false;
    PKS["ret"] = nil;
}

fun psIsPathChar(ch) {
    return isAlnumChar(ch) or ch == ".";
}

fun psIsPath(s) {
    if (len(s) == 0) { return false; }
    let i = 0;
    while (i < len(s)) {
        if (psIsPathChar(charAt(s, i)) == false) { return false; }
        i = i + 1;
    }
    return true;
}

fun psAllDigits(s) {
    if (len(s) == 0) { return false; }
    let i = 0;
    while (i < len(s)) {
        if (contains("0123456789", charAt(s, i)) == false) { return false; }
        i = i + 1;
    }
    return true;
}

// x.y.z inside any root map (maps and arrays); returns nil if not found
fun psLookupIn(root, path) {
    let parts = split(path, ".");
    let cur = root;
    let i = 0;
    while (i < len(parts)) {
        let p = parts[i];
        if (type(cur) == "map") {
            if (has(cur, p) == false) { return nil; }
            cur = cur[p];
        } else if (type(cur) == "array") {
            if (psAllDigits(p) == false) { return nil; }
            let idx = toNumber(p);
            if (idx >= len(cur)) { return nil; }
            cur = cur[idx];
        } else {
            return nil;
        }
        i = i + 1;
    }
    return cur;
}

fun psLookup(path) {
    return psLookupIn(PKS["vars"], path);
}

// "$x.y" as a whole => the value with its original type; "text {x} text" => text substitution
fun psResolve(raw) {
    if (type(raw) != "string") { return raw; }
    if (len(raw) > 1 and charAt(raw, 0) == "$" and psIsPath(substr(raw, 1, len(raw) - 1))) {
        return psLookup(substr(raw, 1, len(raw) - 1));
    }
    if (contains(raw, "{") == false) { return raw; }
    let out = "";
    let i = 0;
    let n = len(raw);
    while (i < n) {
        let ch = charAt(raw, i);
        if (ch == "{") {
            let rest = substr(raw, i + 1, n - i - 1);
            let close = indexOf(rest, "}");
            if (close > 0 and psIsPath(substr(rest, 0, close))) {
                let v = psLookup(substr(rest, 0, close));
                if (v == nil) { out = out + ""; } else { out = out + toString(v); }
                i = i + close + 2;
            } else {
                out = out + ch;
                i = i + 1;
            }
        } else {
            out = out + ch;
            i = i + 1;
        }
    }
    return out;
}

fun psNum(v, fallback) {
    if (type(v) == "number") { return v; }
    if (type(v) == "string") {
        let t = trim(v);
        if (psAllDigits(t)) { return toNumber(t); }
        if (len(t) > 2 and charAt(t, 0) == "-" and psAllDigits(substr(t, 1, len(t) - 1))) { return toNumber(t); }
        let dot = indexOf(t, ".");
        if (dot > 0 and psAllDigits(substr(t, 0, dot)) and psAllDigits(substr(t, dot + 1, len(t) - dot - 1))) { return toNumber(t); }
    }
    return fallback;
}

fun psTruthy(v) {
    if (v == nil) { return false; }
    if (v == false) { return false; }
    if (v == "" or v == "false" or v == 0) { return false; }
    return true;
}

fun psIsFlag(v) {
    return v == "true" or v == true;
}

// ---- Attribute helpers ---------------------------------------------------------------

fun psHas(node, key) { return has(node["attrs"], key); }

fun psAttr(node, key) {
    if (has(node["attrs"], key) == false) { return nil; }
    return psResolve(node["attrs"][key]);
}

fun psAttrOr(node, key, fallback) {
    let v = psAttr(node, key);
    if (v == nil) { return fallback; }
    return v;
}

fun psFail(node, message) {
    return langError("Interpreter", "<" + node["name"] + "> " + message, node["line"]);
}

fun psStore(node, value) {
    if (has(node["attrs"], "name")) {
        PKS["vars"][node["attrs"]["name"]] = value;
    }
}

fun psEmit(msg) {
    push(PKS["out"], msg);
}

// ---- URL encoding/decoding ---------------------------------------------------

fun psUrlEncode(s) {
    let keep = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
    let chars = pkChars(s);
    let out = "";
    let i = 0;
    while (i < len(chars)) {
        let c = chars[i];
        if (contains(keep, c)) {
            out = out + c;
        } else {
            let hx = sec.hexEncode(c);            // the UTF-8 bytes of the whole character
            let j = 0;
            while (j < len(hx)) { out = out + "%" + upper(substr(hx, j, 2)); j = j + 2; }
        }
        i = i + 1;
    }
    return out;
}

fun psUrlDecode(s) {
    let out = "";
    let i = 0;
    let n = len(s);
    let bytes = "";
    while (i < n) {
        let ch = charAt(s, i);
        if (ch == "+") { out = out + " "; i = i + 1; }
        else if (ch == "%" and i + 2 < n + 0 and contains("0123456789abcdefABCDEF", charAt(s, i + 1)) and contains("0123456789abcdefABCDEF", charAt(s, i + 2))) {
            out = out + sec.hexDecode(lower(substr(s, i + 1, 2)));
            i = i + 3;
        } else { out = out + ch; i = i + 1; }
    }
    return out;
}

// ---- <email> ----------------------------------------------------------------------

fun psEmailCheck(raw) {
    let a = lower(trim(raw));
    let r = { ok: false, input: raw, address: a, local: "", domain: "", masked: "", error: "" };
    let parts = split(a, "@");
    if (len(parts) != 2) { r["error"] = "must contain exactly one @"; return r; }
    let local = parts[0];
    let domain = parts[1];
    if (len(local) < 1 or len(local) > 64) { r["error"] = "the part before @ must be 1 to 64 long"; return r; }
    if (len(domain) < 4 or len(domain) > 253) { r["error"] = "the domain is too short/long"; return r; }
    let okLocal = "abcdefghijklmnopqrstuvwxyz0123456789._%+-";
    let okDomain = "abcdefghijklmnopqrstuvwxyz0123456789.-";
    let i = 0;
    while (i < len(local)) {
        if (contains(okLocal, charAt(local, i)) == false) { r["error"] = "Character not allowed before @: " + charAt(local, i); return r; }
        i = i + 1;
    }
    i = 0;
    while (i < len(domain)) {
        if (contains(okDomain, charAt(domain, i)) == false) { r["error"] = "Character not allowed in the domain"; return r; }
        i = i + 1;
    }
    if (charAt(local, 0) == "." or charAt(local, len(local) - 1) == ".") { r["error"] = "The local part must not start/end with a dot"; return r; }
    if (contains(local, "..") or contains(domain, "..")) { r["error"] = "Two consecutive dots are not allowed"; return r; }
    let labels = split(domain, ".");
    if (len(labels) < 2) { r["error"] = "The domain has no extension (like .com)"; return r; }
    let k = 0;
    while (k < len(labels)) {
        let lb = labels[k];
        if (len(lb) == 0 or charAt(lb, 0) == "-" or charAt(lb, len(lb) - 1) == "-") { r["error"] = "Invalid domain part"; return r; }
        k = k + 1;
    }
    if (len(labels[len(labels) - 1]) < 2) { r["error"] = "The domain extension is too short"; return r; }
    r["ok"] = true;
    r["local"] = local;
    r["domain"] = domain;
    r["tld"] = labels[len(labels) - 1];
    r["masked"] = charAt(local, 0) + "***@" + domain;
    r["error"] = "";
    return r;
}

fun psTagEmail(node) {
    // <email name="e" address="a@b.com" />   |   <email name="m" mailto="a@b.com" subject="..." body="..." />
    if (psHas(node, "mailto")) {
        let to = psAttr(node, "mailto");
        if (to == nil) { return psFail(node, "mailto: the variable is not defined"); }
        let chk = psEmailCheck(toString(to));
        if (chk["ok"] == false) { psStore(node, { ok: false, error: chk["error"] }); return nil; }
        let url = "mailto:" + chk["address"];
        let sep = "?";
        if (psHas(node, "subject")) { url = url + sep + "subject=" + psUrlEncode(toString(psAttr(node, "subject"))); sep = "&"; }
        if (psHas(node, "body")) { url = url + sep + "body=" + psUrlEncode(toString(psAttr(node, "body"))); }
        psStore(node, { ok: true, url: url, to: chk["address"] });
        return nil;
    }
    let addr = psAttr(node, "address");
    if (addr == nil) { return psFail(node, "The address attribute is required (or its variable is not defined)"); }
    psStore(node, psEmailCheck(toString(addr)));
    return nil;
}

// ---- <password> -------------------------------------------------------------------

fun psPolicyByName(name) {
    if (name == "basic") { return pkPolicyBasic(); }
    if (name == "strict") { return pkPolicyStrict(); }
    if (name == "pin") { return pkPolicyPin(6); }
    return pkPolicyStandard();
}

fun psTagPassword(node) {
    if (psHas(node, "generate")) {
        let opts = { length: psNum(psAttr(node, "length"), 16) };
        if (psAttr(node, "symbols") == "false") { opts["symbols"] = false; }
        if (psAttr(node, "ambiguous") == "false") { opts["avoidAmbiguous"] = true; }
        let g = pkGenerate(opts);
        if (g["ok"] == false) { return psFail(node, g["error"]); }
        psStore(node, g);
        return nil;
    }
    if (psHas(node, "passphrase")) {
        let g = pkPassphrase({ words: psNum(psAttr(node, "words"), 6), separator: psAttrOr(node, "separator", "-") });
        if (g["ok"] == false) { return psFail(node, g["error"]); }
        psStore(node, g);
        return nil;
    }
    if (psHas(node, "pin")) {
        let g = pkGeneratePin(psNum(psAttr(node, "length"), 6));
        if (g["ok"] == false) { return psFail(node, g["error"]); }
        psStore(node, g);
        return nil;
    }
    if (psHas(node, "check") or psHas(node, "strength")) {
        let key = "check";
        if (psHas(node, "strength")) { key = "strength"; }
        let pw = psAttr(node, key);
        if (pw == nil) { return psFail(node, key + ": the variable is not defined"); }
        let ctx = {};
        if (psHas(node, "username")) { ctx["username"] = toString(psAttr(node, "username")); }
        if (psHas(node, "email")) { ctx["email"] = toString(psAttr(node, "email")); }
        if (key == "strength") { psStore(node, pkAnalyze(toString(pw), ctx)); return nil; }
        psStore(node, pkCheck(toString(pw), psPolicyByName(psAttrOr(node, "policy", "standard")), ctx));
        return nil;
    }
    if (psHas(node, "hash")) {
        let pw = psAttr(node, "hash");
        if (pw == nil) { return psFail(node, "hash: the variable is not defined"); }
        psStore(node, { ok: true, hash: pkHash(toString(pw), { iterations: psNum(psAttr(node, "iterations"), PK_DEFAULT_ITERATIONS) }) });
        return nil;
    }
    if (psHas(node, "verify")) {
        let pw = psAttr(node, "verify");
        let against = psAttr(node, "against");
        if (pw == nil or against == nil) { return psFail(node, "verify needs the password and the against attribute (the hash)"); }
        psStore(node, { ok: pkVerify(toString(pw), toString(against)) });
        return nil;
    }
    if (psHas(node, "mask")) {
        let pw = psAttr(node, "mask");
        if (pw == nil) { return psFail(node, "mask: the variable is not defined"); }
        psStore(node, { ok: true, masked: pkMask(toString(pw)) });
        return nil;
    }
    return psFail(node, "choose an operation: generate | passphrase | pin | check | strength | hash | verify | mask");
}

// ---- <apikey> ---------------------------------------------------------------------

fun psKeyMask(key) {
    let n = len(key);
    if (n <= 12) { return "****"; }
    return substr(key, 0, 7) + "…" + substr(key, n - 4, 4);
}

fun psTagApikey(node) {
    if (psHas(node, "generate")) {
        let prefix = toString(psAttrOr(node, "prefix", "pk"));
        let bytes = psNum(psAttr(node, "bytes"), 24);
        if (bytes < 16 or bytes > 64) { return psFail(node, "bytes between 16 and 64"); }
        let secret = sec.randomToken(bytes);
        let key = prefix + "_" + secret;
        psStore(node, { ok: true, key: key, id: substr(secret, 0, 8), hash: sec.sha256(key), masked: psKeyMask(key), prefix: prefix });
        return nil;
    }
    if (psHas(node, "verify")) {
        let key = psAttr(node, "verify");
        let against = psAttr(node, "against");
        if (key == nil or against == nil) { return psFail(node, "verify needs the key and the against attribute (hash)"); }
        psStore(node, { ok: sec.constantTimeEqual(sec.sha256(toString(key)), toString(against)) });
        return nil;
    }
    if (psHas(node, "check")) {
        let key = psAttr(node, "check");
        if (key == nil) { return psFail(node, "check: the variable is not defined"); }
        let k = toString(key);
        let prefix = toString(psAttrOr(node, "prefix", "pk"));
        let ok = indexOf(k, prefix + "_") == 0 and len(k) >= len(prefix) + 1 + 32;
        let r = { ok: ok };
        if (ok == false) { r["error"] = "Invalid key format (must start with " + prefix + "_ and be followed by 32+ characters)"; }
        psStore(node, r);
        return nil;
    }
    if (psHas(node, "mask")) {
        let key = psAttr(node, "mask");
        if (key == nil) { return psFail(node, "mask: the variable is not defined"); }
        psStore(node, { ok: true, masked: psKeyMask(toString(key)) });
        return nil;
    }
    return psFail(node, "choose an operation: generate | verify | check | mask");
}

// ---- <link> -----------------------------------------------------------------------

fun psLinkParse(url) {
    let r = { ok: false, url: url, scheme: "", host: "", port: "", path: "", query: {}, fragment: "", secure: false, error: "" };
    let u = trim(url);
    let sp = indexOf(u, "://");
    if (sp < 1) { r["error"] = "The link has no scheme (like https://)"; return r; }
    let scheme = lower(substr(u, 0, sp));
    if (scheme != "http" and scheme != "https") { r["error"] = "Unsupported scheme: " + scheme; return r; }
    let rest = substr(u, sp + 3, len(u) - sp - 3);
    if (contains(rest, " ")) { r["error"] = "The link contains a space (encode it as %20)"; return r; }
    let frag = "";
    let h = indexOf(rest, "#");
    if (h >= 0) { frag = substr(rest, h + 1, len(rest) - h - 1); rest = substr(rest, 0, h); }
    let q = "";
    let qi = indexOf(rest, "?");
    if (qi >= 0) { q = substr(rest, qi + 1, len(rest) - qi - 1); rest = substr(rest, 0, qi); }
    let path = "";
    let pi = indexOf(rest, "/");
    let hostport = rest;
    if (pi >= 0) { path = substr(rest, pi, len(rest) - pi); hostport = substr(rest, 0, pi); }
    let host = hostport;
    let port = "";
    let ci = indexOf(hostport, ":");
    if (ci >= 0) { host = substr(hostport, 0, ci); port = substr(hostport, ci + 1, len(hostport) - ci - 1); }
    if (len(host) == 0 or contains(host, "@")) { r["error"] = "Invalid host"; return r; }
    if (port != "" and psAllDigits(port) == false) { r["error"] = "Invalid port"; return r; }
    let query = {};
    if (len(q) > 0) {
        let pairs = split(q, "&");
        let i = 0;
        while (i < len(pairs)) {
            if (len(pairs[i]) > 0) {
                let eq = indexOf(pairs[i], "=");
                if (eq < 0) { query[psUrlDecode(pairs[i])] = ""; }
                else { query[psUrlDecode(substr(pairs[i], 0, eq))] = psUrlDecode(substr(pairs[i], eq + 1, len(pairs[i]) - eq - 1)); }
            }
            i = i + 1;
        }
    }
    r["ok"] = true;
    r["scheme"] = scheme;
    r["host"] = lower(host);
    r["port"] = port;
    r["path"] = path;
    r["query"] = query;
    r["fragment"] = frag;
    r["secure"] = scheme == "https";
    r["error"] = "";
    return r;
}

fun psTagLink(node) {
    // <link name="l" url="https://x.com/a?b=1" [https] />
    // <link name="u" build base="https://x.com" path="/a" query.id="5" />
    if (psHas(node, "build")) {
        let base = psAttr(node, "base");
        if (base == nil) { return psFail(node, "build needs base"); }
        let url = toString(base);
        while (len(url) > 0 and charAt(url, len(url) - 1) == "/") { url = substr(url, 0, len(url) - 1); }
        if (psHas(node, "path")) {
            let p = toString(psAttr(node, "path"));
            if (len(p) > 0 and charAt(p, 0) != "/") { p = "/" + p; }
            url = url + p;
        }
        let sep = "?";
        for (let k in keys(node["attrs"])) {
            if (indexOf(k, "query.") == 0) {
                url = url + sep + psUrlEncode(substr(k, 6, len(k) - 6)) + "=" + psUrlEncode(toString(psResolve(node["attrs"][k])));
                sep = "&";
            }
        }
        let parsed = psLinkParse(url);
        parsed["url"] = url;
        psStore(node, parsed);
        return nil;
    }
    let raw = psAttr(node, "url");
    if (raw == nil) { return psFail(node, "The url attribute is required (or its variable is not defined)"); }
    let r = psLinkParse(toString(raw));
    if (r["ok"] and psHas(node, "https") and r["secure"] == false) {
        r["ok"] = false;
        r["error"] = "https is required";
    }
    psStore(node, r);
    return nil;
}

// ---- <api> ------------------------------------------------------------------------

fun psTagApi(node) {
    let raw = psAttr(node, "url");
    if (raw == nil) { return psFail(node, "The url attribute is required (or its variable is not defined)"); }
    let url = toString(raw);
    let method = upper(toString(psAttrOr(node, "method", "GET")));
    if (contains(["GET", "POST", "PUT", "PATCH", "DELETE"], method) == false) { return psFail(node, "method is not supported: " + method); }
    let headers = {};
    let shown = {};
    for (let k in keys(node["attrs"])) {
        if (indexOf(k, "header.") == 0) {
            let hn = substr(k, 7, len(k) - 7);
            let hv = toString(psResolve(node["attrs"][k]));
            headers[hn] = hv;
            shown[hn] = hv;
        }
    }
    let key = psAttr(node, "key");
    let auth = toString(psAttrOr(node, "auth", "bearer"));
    if (key != nil) {
        let ks = toString(key);
        if (auth == "bearer") { headers["Authorization"] = "Bearer " + ks; shown["Authorization"] = "Bearer " + psKeyMask(ks); }
        else if (auth == "header") {
            let hn = toString(psAttrOr(node, "header", "X-API-Key"));
            headers[hn] = ks; shown[hn] = psKeyMask(ks);
        } else if (auth == "query") {
            let pn = toString(psAttrOr(node, "param", "api_key"));
            let sepq = "?";
            if (contains(url, "?")) { sepq = "&"; }
            url = url + sepq + psUrlEncode(pn) + "=" + psUrlEncode(ks);
        } else {
            return psFail(node, "auth is unknown: " + auth + " (bearer | header | query)");
        }
    }
    let chk = psLinkParse(url);
    if (chk["ok"] == false) { psStore(node, { ok: false, error: chk["error"] }); return nil; }
    if (chk["secure"] == false and psHas(node, "insecure") == false) {
        psStore(node, { ok: false, error: "insecure http link — use https or add insecure explicitly" });
        return nil;
    }
    let body = "";
    if (psHas(node, "body")) { body = toString(psAttr(node, "body")); }
    // copy for display/logging without secrets: the key in the URL is hidden
    let shownUrl = url;
    if (key != nil and auth == "query") { shownUrl = replace(url, psUrlEncode(toString(key)), psKeyMask(toString(key))); }
    let request = { method: method, url: shownUrl, headers: shown, body: body };

    if (psHas(node, "dry")) {
        psStore(node, { ok: true, dry: true, request: request });
        return nil;
    }
    let resp = httpRequest(method, url, headers, body);
    psStore(node, { ok: true, dry: false, request: request, response: resp });
    return nil;
}

// ---- <sql> — RCSQL link (doc containers in Rin) -------------------------------------------

fun psSqlReserved() {
    return ["table", "name", "id", "insert", "select", "find", "count", "exists", "update", "delete", "create",
            "where", "order", "limit", "field", "equals"];
}

fun psSqlEnsure(table) {
    if (hasContainer(table) == false) { spawn("doc", table); }
}

fun psSqlQuery(node, table) {
    let q = table;
    if (psHas(node, "where")) { q = q + " & " + toString(psAttr(node, "where")); }
    if (psHas(node, "order")) { q = q + " & order:" + toString(psAttr(node, "order")); }
    if (psHas(node, "limit")) { q = q + " & limit:eq(" + toString(psAttr(node, "limit")) + ")"; }
    let v = sqlValidate(q);
    if (v["ok"] == false) { return { ok: false, error: "RCSQL: " + toString(v["error"]), query: q }; }
    return { ok: true, query: q };
}

fun psTagSql(node) {
    let table = psAttr(node, "table");
    if (table == nil) { return psFail(node, "The table attribute is required"); }
    table = toString(table);

    if (psHas(node, "create")) {
        psSqlEnsure(table);
        psStore(node, { ok: true, table: table });
        return nil;
    }

    if (psHas(node, "insert") or psHas(node, "update")) {
        let fields = {};
        for (let k in keys(node["attrs"])) {
            if (contains(psSqlReserved(), k) == false) {
                let v = psResolve(node["attrs"][k]);
                // we never store a raw password/secret — hash only (pk1$...) or sha256
                if (pkIsSensitiveKey(k) and indexOf(toString(v), "pk1$") != 0) {
                    return psFail(node, "Refusing to store the field '" + k + "' as raw text — pass its hash (<password hash=...> or <apikey ... hash>)");
                }
                fields[k] = v;
            }
        }
        if (psHas(node, "insert")) {
            psSqlEnsure(table);
            let id = toString(psAttrOr(node, "id", sec.randomToken(6)));
            insertDoc(table, id, fields);
            psStore(node, { ok: true, id: id, table: table });
            return nil;
        }
        let uid = psAttr(node, "id");
        if (uid == nil) { return psFail(node, "update needs id"); }
        if (hasContainer(table) == false) { psStore(node, { ok: false, error: "Table not found: " + table }); return nil; }
        let done = updateDoc(table, toString(uid), fields);
        psStore(node, { ok: done, id: toString(uid), table: table });
        return nil;
    }

    // reading: a missing table => a silent empty result (like RCSQL behavior)
    if (hasContainer(table) == false) {
        if (psHas(node, "count")) { psStore(node, 0); }
        else if (psHas(node, "exists")) { psStore(node, false); }
        else { psStore(node, []); }
        return nil;
    }

    if (psHas(node, "find")) {
        // search by a value containing symbols the RCSQL text does not accept (like @ or .) => filtering inside Rin
        let field = psAttr(node, "field");
        let want = psAttr(node, "equals");
        if (field == nil or want == nil) { return psFail(node, "find needs field and equals"); }
        let rows = sql(table);
        let hit = nil;
        let i = 0;
        while (i < len(rows) and hit == nil) {
            if (has(rows[i], toString(field)) and toString(rows[i][toString(field)]) == toString(want)) { hit = rows[i]; }
            i = i + 1;
        }
        psStore(node, { ok: hit != nil, found: hit != nil, row: hit });
        return nil;
    }

    let qr = psSqlQuery(node, table);
    if (qr["ok"] == false) { psStore(node, { ok: false, error: qr["error"] }); return nil; }
    if (psHas(node, "count")) { psStore(node, sqlCount(qr["query"])); return nil; }
    if (psHas(node, "exists")) { psStore(node, sqlExists(qr["query"])); return nil; }
    if (psHas(node, "delete")) {
        if (psHas(node, "where") == false) { return psFail(node, "delete without where is refused (prevents wiping the whole table by mistake)"); }
        psStore(node, { ok: true, deleted: sqlDelete(qr["query"]) });
        return nil;
    }
    psStore(node, sql(qr["query"]));          // select (the default)
    return nil;
}

// ---- Core tags --------------------------------------------------------------

fun psSplitElse(children) {
    let thenPart = [];
    let elsePart = [];
    let seenElse = false;
    let i = 0;
    while (i < len(children)) {
        if (children[i]["name"] == "else") { seenElse = true; }
        else if (seenElse) { push(elsePart, children[i]); }
        else { push(thenPart, children[i]); }
        i = i + 1;
    }
    return { thenPart: thenPart, elsePart: elsePart };
}

fun psCondition(node) {
    let v = psAttr(node, "var");
    if (psHas(node, "eq")) { return toString(v) == toString(psAttr(node, "eq")); }
    if (psHas(node, "neq")) { return toString(v) != toString(psAttr(node, "neq")); }
    if (psHas(node, "gt")) { return psNum(v, 0) > psNum(psAttr(node, "gt"), 0); }
    if (psHas(node, "gte")) { return psNum(v, 0) >= psNum(psAttr(node, "gte"), 0); }
    if (psHas(node, "lt")) { return psNum(v, 0) < psNum(psAttr(node, "lt"), 0); }
    if (psHas(node, "lte")) { return psNum(v, 0) <= psNum(psAttr(node, "lte"), 0); }
    if (psHas(node, "has")) { return v != nil and contains(toString(v), toString(psAttr(node, "has"))); }
    if (psHas(node, "empty")) { return v == nil or v == "" or (type(v) == "array" and len(v) == 0); }
    return psTruthy(v);
}

fun psExecNodes(nodes) {
    let i = 0;
    while (i < len(nodes) and PKS["returned"] == false) {
        let e = psExecTag(nodes[i]);
        if (e != nil) { return e; }
        i = i + 1;
    }
    return nil;
}

fun psExecTag(node) {
    let n = node["name"];
    if (n == "passkit") { return psExecNodes(node["children"]); }
    if (n == "set") {
        if (psHas(node, "name") == false) { return psFail(node, "The name attribute is required"); }
        PKS["vars"][node["attrs"]["name"]] = psAttrOr(node, "value", "");
        return nil;
    }
    if (n == "print") {
        let v = psAttr(node, "value");
        if (v == nil) { v = "(undefined)"; }
        psEmit(toString(v));
        return nil;
    }
    if (n == "assert") {
        if (psCondition(node) == false) { return psFail(node, "Assertion failed: " + toString(psAttrOr(node, "message", "assert"))); }
        return nil;
    }
    if (n == "if") {
        let parts = psSplitElse(node["children"]);
        if (psCondition(node)) { return psExecNodes(parts["thenPart"]); }
        return psExecNodes(parts["elsePart"]);
    }
    if (n == "for") {
        let list = psAttr(node, "in");
        let loopVar = toString(psAttrOr(node, "each", "item"));
        if (type(list) != "array") { return psFail(node, "in must point to an array (like $rows)"); }
        let i = 0;
        while (i < len(list) and i < 10000 and PKS["returned"] == false) {
            PKS["vars"][loopVar] = list[i];
            let e = psExecNodes(node["children"]);
            if (e != nil) { return e; }
            i = i + 1;
        }
        return nil;
    }
    if (n == "else") { return psFail(node, "<else/> outside <if>"); }
    if (n == "email") { return psTagEmail(node); }
    if (n == "password") { return psTagPassword(node); }
    if (n == "apikey") { return psTagApikey(node); }
    if (n == "link") { return psTagLink(node); }
    if (n == "api") { return psTagApi(node); }
    if (n == "sql") { return psTagSql(node); }
    if (n == "input") { return psTagInput(node); }
    if (n == "return") { return psTagReturn(node); }
    if (n == "import") { return psTagImport(node); }
    if (n == "run") { return psTagRun(node); }
    if (n == "call") { return psTagCall(node); }
    if (n == "crypt") { return psTagCrypt(node); }
    if (n == "token") { return psTagToken(node); }
    if (n == "otp") { return psTagOtp(node); }
    if (n == "db") { return psTagDb(node); }
    if (n == "container") { return psTagContainer(node); }
    return psFail(node, "Unsupported tag");
}

// ============================================================================
//  File linking (File Linking) — both directions
//   .passkit -> .passkit :  <import file>  (include in the same scope)  /  <run file name in.k=...>  (isolated call)
//   .passkit -> Rin      :  <call fn="x" arg0=...>  for functions that Rin registered explicitly via passkitRegister
//   Rin -> .passkit      :  passkitRun(path, inputs) / passkitRunSource(src, inputs)
//   File contract:  <input name default required/>  and  <return value="..."/>
// ============================================================================

// Safe relative path only: not absolute, no .., no scheme, no \ (an untrusted .passkit file cannot read outside its folder)
fun psSafeRel(rel) {
    if (rel == nil) { return false; }
    let f = toString(rel);
    if (len(f) == 0) { return false; }
    if (charAt(f, 0) == "/" or contains(f, "..") or contains(f, ":") or contains(f, "\\")) { return false; }
    return true;
}

fun psDirName(path) {
    let last = -1;
    let i = 0;
    while (i < len(path)) {
        if (charAt(path, i) == "/") { last = i; }
        i = i + 1;
    }
    if (last < 0) { return ""; }
    return substr(path, 0, last);
}

fun psJoinPath(base, rel) {
    if (base == "") { return rel; }
    return base + "/" + rel;
}

fun psSave() {
    return { vars: PKS["vars"], out: PKS["out"], returned: PKS["returned"], ret: PKS["ret"], base: PKS["base"], depth: PKS["depth"], binds: PKS["binds"] };
}

fun psRestore(saved) {
    PKS["vars"] = saved["vars"];
    PKS["out"] = saved["out"];
    PKS["returned"] = saved["returned"];
    PKS["ret"] = saved["ret"];
    PKS["base"] = saved["base"];
    PKS["depth"] = saved["depth"];
    PKS["binds"] = saved["binds"];
}

// parses the source and runs it on the current state; nil on success or a langError
fun psParseExec(source) {
    let tokens = pkLex(source);
    let i = 0;
    while (i < len(tokens)) {
        if (isLangError(tokens[i])) { return tokens[i]; }
        i = i + 1;
    }
    let ast = pkParse(tokens);
    if (isLangError(ast)) { return ast; }
    return psExecNodes(ast["body"]);
}

fun psFailResult(msg) {
    let e = langError("Interpreter", msg, 0);
    return { ok: false, output: [], vars: {}, value: nil, error: e, message: formatLangError(e) };
}

// isolated run with a new variable scope (state-protected: suits nested Rin<->passkit calls)
fun psRunChild(source, inputs, base) {
    if (PKS["depth"] >= 8) { return psFailResult("call depth between files is greater than 8 (a link cycle?)"); }
    let saved = psSave();
    PKS["vars"] = {};
    if (inputs != nil) {
        for (let k in keys(inputs)) { PKS["vars"][k] = inputs[k]; }
    }
    PKS["out"] = [];
    PKS["returned"] = false;
    PKS["ret"] = nil;
    PKS["base"] = base;
    PKS["depth"] = saved["depth"] + 1;
    PKS["binds"] = [];
    let e = psParseExec(source);
    let res = { ok: e == nil, output: PKS["out"], vars: PKS["vars"], value: PKS["ret"], error: e, message: "" };
    if (e != nil) { res["message"] = formatLangError(e); }
    psRestore(saved);
    return res;
}

// ---- Tags: <input> <return> ----------------------------------------------------

fun psTagInput(node) {
    if (psHas(node, "name") == false) { return psFail(node, "The name attribute is required"); }
    let nm = node["attrs"]["name"];
    if (has(PKS["vars"], nm)) { return nil; }
    if (psHas(node, "default")) { PKS["vars"][nm] = psAttr(node, "default"); return nil; }
    if (psHas(node, "required")) { return psFail(node, "The input '" + nm + "' is required and was not passed"); }
    return nil;
}

fun psTagReturn(node) {
    PKS["ret"] = psAttr(node, "value");
    PKS["returned"] = true;
    return nil;
}

// ---- <import file="x.passkit"/> : include in the same scope -------------------------------

fun psLoadLinked(node) {
    let rel = psAttr(node, "file");
    if (psSafeRel(rel) == false) { return { err: psFail(node, "file must be a relative path inside the file's folder (with no / or .. or :)") }; }
    let path = psJoinPath(PKS["base"], toString(rel));
    if (fileExists(path) == false) { return { err: psFail(node, "File not found: " + path) }; }
    if (contains(PKS["stack"], path)) { return { err: psFail(node, "Link cycle: the file calls itself (" + path + ")") }; }
    return { path: path, source: readFile(path) };
}

fun psTagImport(node) {
    let ld = psLoadLinked(node);
    if (has(ld, "err")) { return ld["err"]; }
    if (PKS["depth"] >= 8) { return psFail(node, "call depth between files is greater than 8"); }
    let oldBase = PKS["base"];
    PKS["base"] = psDirName(ld["path"]);
    PKS["depth"] = PKS["depth"] + 1;
    push(PKS["stack"], ld["path"]);
    let e = psParseExec(ld["source"]);
    pop(PKS["stack"]);
    PKS["depth"] = PKS["depth"] - 1;
    PKS["base"] = oldBase;
    PKS["returned"] = false;                  // return inside an included file ends only that file
    if (e != nil) { return psFail(node, ld["path"] + " → " + formatLangError(e)); }
    return nil;
}

// ---- <run file name in.k="v" [quiet] [strict]/> : isolated call with inputs and output -------

fun psTagRun(node) {
    let ld = psLoadLinked(node);
    if (has(ld, "err")) { return ld["err"]; }
    let inputs = {};
    for (let k in keys(node["attrs"])) {
        if (indexOf(k, "in.") == 0) { inputs[substr(k, 3, len(k) - 3)] = psResolve(node["attrs"][k]); }
    }
    push(PKS["stack"], ld["path"]);
    let r = psRunChild(ld["source"], inputs, psDirName(ld["path"]));
    pop(PKS["stack"]);
    if (psHas(node, "quiet") == false) {
        let i = 0;
        while (i < len(r["output"])) { psEmit(r["output"][i]); i = i + 1; }
    }
    if (r["ok"] == false and psHas(node, "strict")) { return psFail(node, ld["path"] + " → " + r["message"]); }
    psStore(node, { ok: r["ok"], value: r["value"], vars: r["vars"], output: r["output"], message: r["message"] });
    return nil;
}

// ---- <call fn="name" arg0=".." arg1=".." | args="$list" name="r"/> : call a Rin function ----

fun psTagCall(node) {
    let fname = psAttr(node, "fn");
    if (fname == nil) { return psFail(node, "The fn attribute is required"); }
    fname = toString(fname);
    if (has(PKS["handlers"], fname) == false) {
        if (psHas(node, "optional")) { psStore(node, { ok: false, error: "The function is not registered: " + fname }); return nil; }
        return psFail(node, "The function '" + fname + "' is not registered from Rin (use passkitRegister)");
    }
    let args = [];
    if (psHas(node, "args")) {
        let a = psAttr(node, "args");
        if (type(a) != "array") { return psFail(node, "args must point to an array (like $list)"); }
        let j = 0;
        while (j < len(a)) { push(args, a[j]); j = j + 1; }
    } else {
        let n = 0;
        while (n < 10) {
            if (psHas(node, "arg" + toString(n)) == false) { n = 10; }
            else { push(args, psAttr(node, "arg" + toString(n))); n = n + 1; }
        }
    }
    psStore(node, callFn(PKS["handlers"][fname], args));
    return nil;
}

// ---- Rin API: register functions callable from .passkit ------------------------------------

fun passkitRegister(name, handler) {
    if (isFunction(handler) == false) { return false; }
    PKS["handlers"][name] = handler;
    return true;
}

// passkitHandlers({ double: dbl, greet: greetFn }) -> the number registered
fun passkitHandlers(map) {
    let n = 0;
    for (let k in keys(map)) { if (passkitRegister(k, map[k])) { n = n + 1; } }
    return n;
}

fun passkitUnregister(name) {
    let fresh = {};
    for (let k in keys(PKS["handlers"])) { if (k != name) { fresh[k] = PKS["handlers"][k]; } }
    PKS["handlers"] = fresh;
    return true;
}

fun passkitClearHandlers() {
    PKS["handlers"] = {};
    return true;
}

fun passkitHandlerNames() {
    return keys(PKS["handlers"]);
}

// ---- Rin API: run .passkit ------------------------------------------------------
// The result: { ok, output:[..], vars:{..}, return, error, message }

fun passkitRunSource(source, inputs) {
    return psRunChild(source, inputs, PKS["base"]);
}

fun passkitRun(path, inputs) {
    if (fileExists(path) == false) { return psFailResult("File not found: " + path); }
    if (contains(PKS["stack"], path)) { return psFailResult("Link cycle: " + path); }
    push(PKS["stack"], path);
    let r = psRunChild(readFile(path), inputs, psDirName(path));
    pop(PKS["stack"]);
    return r;
}

// read a value from a run result by a dotted path:  passkitGet(r, "e.address")
fun passkitGet(result, path) {
    if (type(result) != "map" or has(result, "vars") == false) { return nil; }
    return psLookupIn(result["vars"], path);
}

// ---- language entry points (run.rin / test.rin) --------------------------------------------

fun pkInterpret(ast) {
    psReset();
    let e = psExecNodes(ast["body"]);
    if (e != nil) { return { ok: false, error: e, output: PKS["out"] }; }
    return { ok: true, output: PKS["out"], vars: PKS["vars"] };
}

fun pkRunSource(source) {
    return psRunChild(source, {}, PKS["base"]);
}


// ============================================================================
//  Tags for encryption, databases and container linking
//  <crypt op=...>  <token op=...>  <otp op=...>  <db table op=...>  <container of op=...>
// ============================================================================

fun psList(v) {
    if (type(v) == "array") { return v; }
    let out = [];
    if (v == nil or toString(v) == "") { return out; }
    for (let p in split(toString(v), ",")) { push(out, trim(p)); }
    return out;
}

fun psOpOf(node) {
    let op = psAttr(node, "op");
    if (op == nil) { return nil; }
    return toString(op);
}

fun psStrAttr(node, k) {
    let v = psAttr(node, k);
    if (v == nil) { return nil; }
    return toString(v);
}

// a map from attributes starting with prefix. (like claim.sub="u1" or set.name="Rima")
fun psPrefixed(node, prefix) {
    let out = {};
    for (let k in keys(node["attrs"])) {
        if (indexOf(k, prefix) == 0) { out[substr(k, len(prefix), len(k) - len(prefix))] = psResolve(node["attrs"][k]); }
    }
    return out;
}

// ---- <crypt op=...> -----------------------------------------------------------------

fun psTagCrypt(node) {
    let op = psOpOf(node);
    if (op == nil) { return psFail(node, "The op attribute is required: keygen|seal|open|sealpw|openpw|envelope|unenvelope|hmac|hkdf|pbkdf2|random|uuid|b64|b64d|b32|b32d|fingerprint|merkle|pepper|derive|shamir|combine"); }
    let val = psStrAttr(node, "value");
    let key = psStrAttr(node, "key");
    let aad = psStrAttr(node, "aad");
    let needKey = contains(["seal", "open", "envelope", "unenvelope", "hmac", "derive", "pepper"], op);
    if (needKey and key == nil) { return psFail(node, "op=" + op + " needs key"); }
    let needVal = contains(["seal", "open", "sealpw", "openpw", "envelope", "unenvelope", "hmac", "b64", "b64d", "b32", "b32d", "fingerprint"], op);
    if (needVal and val == nil) { return psFail(node, "op=" + op + " needs value"); }

    if (op == "keygen") { let k = pcGenerateKey(psNum(psAttr(node, "bytes"), 32)); psStore(node, { ok: true, key: k, id: pcKeyId(k) }); return nil; }
    if (op == "seal") { psStore(node, pcSeal(key, val, aad)); return nil; }
    if (op == "open") { psStore(node, pcOpen(key, val, aad)); return nil; }
    if (op == "sealpw") {
        let pw = psStrAttr(node, "password");
        if (pw == nil) { return psFail(node, "sealpw needs password"); }
        psStore(node, pcSealWithPassword(pw, val, { iterations: psNum(psAttr(node, "iterations"), 2000) }));
        return nil;
    }
    if (op == "openpw") {
        let pw2 = psStrAttr(node, "password");
        if (pw2 == nil) { return psFail(node, "openpw needs password"); }
        psStore(node, pcOpenWithPassword(pw2, val));
        return nil;
    }
    if (op == "envelope") { psStore(node, pcEnvelopeSeal(key, val, aad)); return nil; }
    if (op == "unenvelope") { psStore(node, pcEnvelopeOpen(key, val, aad)); return nil; }
    if (op == "hmac") {
        if (pcIsHex(key) == false) { psStore(node, { ok: false, error: "key must be hex" }); return nil; }
        psStore(node, { ok: true, hmac: pcHmacText(key, val) });
        return nil;
    }
    if (op == "derive") {
        if (pcKeyCheck(key)["ok"] == false) { psStore(node, pcKeyCheck(key)); return nil; }
        psStore(node, { ok: true, key: pcDeriveSubkey(key, toString(psAttrOr(node, "purpose", "default"))) });
        return nil;
    }
    if (op == "pepper") {
        let pw3 = psStrAttr(node, "password");
        if (pw3 == nil or pcIsHex(key) == false) { psStore(node, { ok: false, error: "pepper needs password and key (hex)" }); return nil; }
        psStore(node, { ok: true, peppered: pcPepper(pw3, key) });
        return nil;
    }
    if (op == "hkdf") {
        let ikm = psStrAttr(node, "ikm");
        let salt = toString(psAttrOr(node, "salt", ""));
        if (ikm == nil or pcIsHex(ikm) == false or pcIsHex(salt) == false) { psStore(node, { ok: false, error: "ikm/salt must be hex" }); return nil; }
        let okm = pcHkdf(ikm, salt, pcHexOf(toString(psAttrOr(node, "info", ""))), psNum(psAttr(node, "length"), 32));
        if (okm == nil) { psStore(node, { ok: false, error: "length invalid (1..8160)" }); return nil; }
        psStore(node, { ok: true, okm: okm });
        return nil;
    }
    if (op == "pbkdf2") {
        let pw4 = psStrAttr(node, "password");
        let saltHex = toString(psAttrOr(node, "salt", ""));
        if (pw4 == nil or pcIsHex(saltHex) == false) { psStore(node, { ok: false, error: "pbkdf2 needs password and salt (hex)" }); return nil; }
        psStore(node, { ok: true, key: pcPbkdf2(pw4, saltHex, psNum(psAttr(node, "iterations"), 2000), psNum(psAttr(node, "bytes"), 32)) });
        return nil;
    }
    if (op == "random") { psStore(node, { ok: true, hex: pcRandomHex(psNum(psAttr(node, "bytes"), 16)) }); return nil; }
    if (op == "uuid") { psStore(node, { ok: true, uuid: pcUuid4() }); return nil; }
    if (op == "b64") { psStore(node, { ok: true, value: pcB64UrlEncode(val) }); return nil; }
    if (op == "b64d") {
        let d = pcB64UrlDecode(val);
        if (d == nil) { psStore(node, { ok: false, error: "base64url invalid" }); return nil; }
        psStore(node, { ok: true, value: d });
        return nil;
    }
    if (op == "b32") { psStore(node, { ok: true, value: pcBase32Encode(val) }); return nil; }
    if (op == "b32d") {
        let d2 = pcBase32Decode(val);
        if (d2 == nil) { psStore(node, { ok: false, error: "base32 invalid" }); return nil; }
        psStore(node, { ok: true, value: d2 });
        return nil;
    }
    if (op == "fingerprint") { psStore(node, { ok: true, fingerprint: pcFingerprint(val) }); return nil; }
    if (op == "merkle") {
        let leaves = psList(psAttr(node, "leaves"));
        psStore(node, { ok: true, root: pcMerkleRoot(leaves), count: len(leaves) });
        return nil;
    }
    if (op == "shamir") {
        let sh = pcShamirSplit(toString(psAttrOr(node, "secret", "")), psNum(psAttr(node, "n"), 5), psNum(psAttr(node, "k"), 3));
        psStore(node, sh);
        return nil;
    }
    if (op == "combine") {
        let shares = psAttr(node, "shares");
        if (type(shares) != "array") { shares = psList(shares); }
        psStore(node, pcShamirCombine(shares));
        return nil;
    }
    return psFail(node, "op is unknown: " + op);
}

// ---- <token op=sign|verify|signurl|verifyurl> --------------------------------------------

fun psTagToken(node) {
    let op = psOpOf(node);
    let key = psStrAttr(node, "key");
    if (op == nil or key == nil) { return psFail(node, "The op and key attributes are required"); }
    let nowv = psNum(psAttr(node, "now"), 0);
    if (op == "sign") {
        let opts = { now: nowv };
        if (psHas(node, "ttl")) { opts["ttl"] = psNum(psAttr(node, "ttl"), 0); }
        if (psHas(node, "iss")) { opts["iss"] = psStrAttr(node, "iss"); }
        if (psHas(node, "aud")) { opts["aud"] = psStrAttr(node, "aud"); }
        psStore(node, pcTokenSign(psPrefixed(node, "claim."), key, opts));
        return nil;
    }
    if (op == "verify") {
        let tok = psStrAttr(node, "value");
        if (tok == nil) { return psFail(node, "verify needs value (the token)"); }
        let opts2 = { now: nowv };
        if (psHas(node, "iss")) { opts2["iss"] = psStrAttr(node, "iss"); }
        if (psHas(node, "aud")) { opts2["aud"] = psStrAttr(node, "aud"); }
        if (pcKeyCheck(key)["ok"] == false) { psStore(node, pcKeyCheck(key)); return nil; }
        psStore(node, pcTokenVerify(tok, key, opts2));
        return nil;
    }
    if (op == "signurl") {
        let u = psStrAttr(node, "url");
        if (u == nil) { return psFail(node, "signurl needs url"); }
        psStore(node, { ok: true, url: pcSignUrl(u, key, nowv + psNum(psAttr(node, "ttl"), 300)) });
        return nil;
    }
    if (op == "verifyurl") {
        let u2 = psStrAttr(node, "url");
        if (u2 == nil) { return psFail(node, "verifyurl needs url"); }
        psStore(node, pcVerifyUrl(u2, key, nowv));
        return nil;
    }
    return psFail(node, "op is unknown: " + op + " (sign|verify|signurl|verifyurl)");
}

// ---- <otp op=secret|code|verify|uri|recovery|recoverycheck> ---------------------------------

fun psTagOtp(node) {
    let op = psOpOf(node);
    if (op == nil) { return psFail(node, "The op attribute is required: secret|code|verify|uri|recovery|recoverycheck"); }
    if (op == "secret") { let sx = pcRandomHex(20); psStore(node, { ok: true, secret: sx, base32: pcBase32EncodeHex(sx) }); return nil; }
    if (op == "recovery") { let rc = pcRecoveryCodes(psNum(psAttr(node, "n"), 8)); psStore(node, { ok: true, codes: rc["codes"], hashes: rc["hashes"] }); return nil; }
    if (op == "recoverycheck") {
        let hs = psAttr(node, "hashes");
        if (type(hs) != "array") { return psFail(node, "hashes must point to an array"); }
        let rv = pcRecoveryVerify(toString(psAttrOr(node, "code", "")), hs);
        psStore(node, { ok: rv["ok"], remaining: rv["remaining"] });
        return nil;
    }
    let secret = psStrAttr(node, "secret");
    if (secret == nil or pcIsHex(secret) == false or len(secret) == 0) { psStore(node, { ok: false, error: "secret must be hex (from op=secret)" }); return nil; }
    let digits = psNum(psAttr(node, "digits"), 6);
    let nowv = psNum(psAttr(node, "now"), 0);
    if (op == "code") { psStore(node, { ok: true, code: pcTotp(secret, nowv, 30, digits) }); return nil; }
    if (op == "verify") {
        let v = pcTotpVerify(secret, toString(psAttrOr(node, "code", "")), nowv, psNum(psAttr(node, "window"), 1), digits);
        psStore(node, v);
        return nil;
    }
    if (op == "uri") {
        psStore(node, { ok: true, uri: pcOtpAuthUri(toString(psAttrOr(node, "issuer", "Passkit")), toString(psAttrOr(node, "account", "user")), secret, { digits: digits }) });
        return nil;
    }
    return psFail(node, "op is unknown: " + op);
}

// ---- <db table=... op=...> ----------------------------------------------------------------------

fun psTagDb(node) {
    let op = psOpOf(node);
    let table = psStrAttr(node, "table");
    if (op == nil) { return psFail(node, "The op attribute is required"); }
    let key = psStrAttr(node, "key");
    let id = psStrAttr(node, "id");
    let nowv = psNum(psAttr(node, "now"), 0);
    if (op == "tables") { psStore(node, pdTables()); return nil; }
    if (table == nil) { return psFail(node, "The table attribute is required"); }

    if (op == "create") { psStore(node, pdCreate(table)); return nil; }
    if (op == "drop") { psStore(node, pdDrop(table)); return nil; }
    if (op == "exists") { psStore(node, pdExists(table)); return nil; }
    if (op == "count") { psStore(node, pdCount(table)); return nil; }
    if (op == "all") { psStore(node, pdAll(table)); return nil; }
    if (op == "active") { psStore(node, pdActive(table)); return nil; }
    if (op == "checksum") { psStore(node, pdChecksum(table)); return nil; }
    if (op == "export") { psStore(node, pdExportJson(table)); return nil; }
    if (op == "import") { psStore(node, pdImportJson(table, toString(psAttrOr(node, "json", "[]")), psHas(node, "replace"))); return nil; }
    if (op == "begin") { psStore(node, pdBegin(table)); return nil; }
    if (op == "commit") { psStore(node, pdCommit(table)); return nil; }
    if (op == "rollback") { psStore(node, pdRollback(table)); return nil; }
    if (op == "nextid") { psStore(node, pdNextId(table, toString(psAttrOr(node, "seq", "id")))); return nil; }
    if (op == "where") { psStore(node, pdWhere(table, toString(psAttrOr(node, "where", "")))); return nil; }
    if (op == "page") { psStore(node, pdPage(table, psNum(psAttr(node, "page"), 1), psNum(psAttr(node, "size"), 10))); return nil; }
    if (op == "find" or op == "findone") {
        let f = psStrAttr(node, "field");
        let want = psAttr(node, "equals");
        if (f == nil or want == nil) { return psFail(node, "find needs field and equals"); }
        if (key != nil and psHas(node, "blind")) { psStore(node, pdFindByBlind(table, f, want, key)); return nil; }
        if (op == "find") { psStore(node, pdFind(table, f, want)); return nil; }
        psStore(node, pdFindOne(table, f, want));
        return nil;
    }

    if (op == "insert" or op == "upsert") {
        let fields = psPrefixed(node, "set.");
        let opts = {};
        if (id != nil) { opts["id"] = id; }
        if (psHas(node, "now")) { opts["now"] = nowv; }
        if (psHas(node, "unique")) { opts["unique"] = psStrAttr(node, "unique"); }
        for (let fk in keys(fields)) {
            if (pkIsSensitiveKey(fk) and indexOf(toString(fields[fk]), "pk1$") != 0 and psHas(node, "enc") == false) {
                return psFail(node, "Refusing to store the field '" + fk + "' as raw text — pass its hash or encrypt it via enc= and key=");
            }
        }
        if (psHas(node, "enc")) {
            if (key == nil) { return psFail(node, "enc needs key"); }
            opts["blind"] = psList(psAttr(node, "blind"));
            psStore(node, pdInsertEnc(table, fields, key, psList(psAttr(node, "enc")), opts));
            return nil;
        }
        if (op == "upsert") {
            if (id == nil) { return psFail(node, "upsert needs id"); }
            psStore(node, pdUpsert(table, id, fields, opts));
            return nil;
        }
        psStore(node, pdInsert(table, fields, opts));
        return nil;
    }
    if (op == "get") {
        if (id == nil) { return psFail(node, "get needs id"); }
        if (key != nil) { psStore(node, pdGetDec(table, id, key)); } else { psStore(node, pdGet(table, id)); }
        return nil;
    }
    if (op == "update") {
        if (id == nil) { return psFail(node, "update needs id"); }
        psStore(node, pdUpdate(table, id, psPrefixed(node, "set."), { now: nowv }));
        return nil;
    }
    if (op == "delete") {
        if (id == nil) { return psFail(node, "delete needs id"); }
        psStore(node, pdDelete(table, id));
        return nil;
    }
    if (op == "softdelete") { if (id == nil) { return psFail(node, "softdelete needs id"); } psStore(node, pdSoftDelete(table, id, nowv)); return nil; }
    if (op == "restore") { if (id == nil) { return psFail(node, "restore needs id"); } psStore(node, pdRestore(table, id)); return nil; }
    if (op == "exportenc") { if (key == nil) { return psFail(node, "exportenc needs key"); } psStore(node, pdExportEncrypted(table, key)); return nil; }
    if (op == "importenc") {
        if (key == nil) { return psFail(node, "importenc needs key"); }
        psStore(node, pdImportEncrypted(table, key, toString(psAttrOr(node, "sealed", "")), psHas(node, "replace"), psStrAttr(node, "source")));
        return nil;
    }
    if (op == "reencrypt") {
        let ok1 = psStrAttr(node, "oldkey");
        let nk = psStrAttr(node, "newkey");
        if (ok1 == nil or nk == nil) { return psFail(node, "reencrypt needs oldkey and newkey"); }
        psStore(node, pdReencrypt(table, ok1, nk, psList(psAttr(node, "enc")), psList(psAttr(node, "blind"))));
        return nil;
    }

    // ---- security models ----
    if (op == "user.create") {
        if (key == nil) { return psFail(node, "user.create needs key"); }
        let pol = psPolicyByName(psAttrOr(node, "policy", "standard"));
        psStore(node, pdUserCreate(table, key, toString(psAttrOr(node, "email", "")), toString(psAttrOr(node, "password", "")), { policy: pol, now: nowv, extra: psPrefixed(node, "set.") }));
        return nil;
    }
    if (op == "user.login") {
        if (key == nil) { return psFail(node, "user.login needs key"); }
        let pol2 = psPolicyByName(psAttrOr(node, "policy", "standard"));
        psStore(node, pdUserLogin(table, key, toString(psAttrOr(node, "email", "")), toString(psAttrOr(node, "password", "")), { policy: pol2, now: nowv }));
        return nil;
    }
    if (op == "user.change") {
        if (key == nil or id == nil) { return psFail(node, "user.change needs key and id"); }
        let pol3 = psPolicyByName(psAttrOr(node, "policy", "standard"));
        psStore(node, pdUserChangePassword(table, key, id, toString(psAttrOr(node, "old", "")), toString(psAttrOr(node, "new", "")), { policy: pol3, now: nowv }));
        return nil;
    }
    if (op == "session.create") { psStore(node, pdSessionCreate(table, toString(psAttrOr(node, "user", "")), psNum(psAttr(node, "ttl"), 3600), nowv, nil)); return nil; }
    if (op == "session.check") { psStore(node, pdSessionCheck(table, toString(psAttrOr(node, "token", "")), nowv)); return nil; }
    if (op == "session.revoke") { psStore(node, pdSessionRevoke(table, toString(psAttrOr(node, "token", "")))); return nil; }
    if (op == "session.revokeuser") { psStore(node, pdSessionRevokeUser(table, toString(psAttrOr(node, "user", "")))); return nil; }
    if (op == "session.purge") { psStore(node, pdSessionPurge(table, nowv)); return nil; }
    if (op == "apikey.create") {
        let kopts = { now: nowv, prefix: toString(psAttrOr(node, "prefix", "pk")) };
        if (psHas(node, "ttl")) { kopts["ttl"] = psNum(psAttr(node, "ttl"), 0); }
        psStore(node, pdApiKeyCreate(table, toString(psAttrOr(node, "owner", "")), psList(psAttr(node, "scopes")), kopts));
        return nil;
    }
    if (op == "apikey.verify") { psStore(node, pdApiKeyVerify(table, toString(psAttrOr(node, "apikey", "")), nowv)); return nil; }
    if (op == "apikey.revoke") { if (id == nil) { return psFail(node, "apikey.revoke needs id"); } psStore(node, pdApiKeyRevoke(table, id)); return nil; }
    if (op == "apikey.rotate") { if (id == nil) { return psFail(node, "apikey.rotate needs id"); } psStore(node, pdApiKeyRotate(table, id, nowv)); return nil; }
    if (op == "audit") { psStore(node, pdAuditLog(table, toString(psAttrOr(node, "event", "")), toString(psAttrOr(node, "actor", "")), psAttrOr(node, "data", ""), nowv)); return nil; }
    if (op == "audit.verify") { psStore(node, pdAuditVerify(table)); return nil; }
    if (op == "ratelimit") {
        psStore(node, pdRateLimit(table, toString(psAttrOr(node, "rlkey", "")), psNum(psAttr(node, "max"), 5), psNum(psAttr(node, "window"), 60), nowv));
        return nil;
    }
    if (op == "reset.create") { psStore(node, pdResetCreate(table, toString(psAttrOr(node, "user", "")), psNum(psAttr(node, "ttl"), 900), nowv)); return nil; }
    if (op == "reset.consume") { psStore(node, pdResetConsume(table, toString(psAttrOr(node, "token", "")), nowv)); return nil; }
    if (op == "nonce") { psStore(node, { ok: pdNonceUse(table, toString(psAttrOr(node, "nonce", "")), nowv, psNum(psAttr(node, "ttl"), 300)) }); return nil; }
    if (op == "totp.enroll") {
        if (key == nil) { return psFail(node, "totp.enroll needs key"); }
        psStore(node, pdTotpEnroll(table, key, toString(psAttrOr(node, "user", "")), toString(psAttrOr(node, "issuer", "Passkit")), toString(psAttrOr(node, "account", "user"))));
        return nil;
    }
    if (op == "totp.check") {
        if (key == nil) { return psFail(node, "totp.check needs key"); }
        psStore(node, pdTotpCheck(table, key, toString(psAttrOr(node, "user", "")), toString(psAttrOr(node, "code", "")), nowv));
        return nil;
    }
    return psFail(node, "op is unknown: " + op);
}

// ---- <container of=... op=...> : Link Rin containers to a .passkit file --------------------------------------

fun psTagContainer(node) {
    let op = psOpOf(node);
    let cname = psStrAttr(node, "of");
    if (op == nil) { return psFail(node, "The op attribute is required"); }
    if (op == "names") { psStore(node, container.names()); return nil; }
    if (op == "sync") { psStore(node, { ok: true, synced: psSyncBinds(toString(psAttrOr(node, "dir", "push"))) }); return nil; }
    if (cname == nil) { return psFail(node, "The of attribute (the container name) is required"); }
    let field = psStrAttr(node, "field");
    let key = psStrAttr(node, "key");

    if (op == "ensure") { psStore(node, pdContEnsure(cname, psStrAttr(node, "kind"))); return nil; }
    if (op == "exists") { psStore(node, hasContainer(cname)); return nil; }
    if (op == "kind") { if (hasContainer(cname) == false) { psStore(node, nil); return nil; } psStore(node, kindOf(cname)); return nil; }
    if (op == "fields") { psStore(node, pdContFields(cname)); return nil; }
    if (op == "tojson") { psStore(node, pdContExport(cname)); return nil; }
    if (op == "fromjson") { psStore(node, pdContImport(cname, toString(psAttrOr(node, "json", "{}")))); return nil; }
    if (op == "checksum") { psStore(node, pdContChecksum(cname)); return nil; }
    if (op == "clone") { psStore(node, pdContClone(cname, toString(psAttrOr(node, "to", cname + "_copy")))); return nil; }
    if (op == "sealall") { if (key == nil) { return psFail(node, "sealall needs key"); } psStore(node, pdContSealAll(cname, key)); return nil; }
    if (op == "openall") { if (key == nil) { return psFail(node, "openall needs key"); } psStore(node, pdContOpenAll(cname, key)); return nil; }

    // field -> variables (load) / variables -> container (save)
    if (op == "load") {
        let prefix = toString(psAttrOr(node, "prefix", ""));
        let m = pdContToMap(cname);
        let n = 0;
        for (let k in keys(m)) { PKS["vars"][prefix + k] = m[k]; n = n + 1; }
        psStore(node, { ok: true, loaded: n });
        return nil;
    }
    if (op == "save") {
        let names = psList(psAttr(node, "vars"));
        let n2 = 0;
        for (let vn in names) {
            if (has(PKS["vars"], vn)) { pdContSet(cname, vn, PKS["vars"][vn]); n2 = n2 + 1; }
        }
        psStore(node, { ok: true, saved: n2 });
        return nil;
    }

    if (field == nil) { return psFail(node, "op=" + op + " needs field"); }
    if (op == "get") { psStore(node, pdContGet(cname, field)); return nil; }
    if (op == "set") { psStore(node, pdContSet(cname, field, psAttrOr(node, "value", ""))); return nil; }
    if (op == "has") { psStore(node, pdContHas(cname, field)); return nil; }
    if (op == "delete") { psStore(node, pdContDelete(cname, field)); return nil; }
    if (op == "seal") { if (key == nil) { return psFail(node, "seal needs key"); } psStore(node, pdContSeal(cname, field, key)); return nil; }
    if (op == "open") { if (key == nil) { return psFail(node, "open needs key"); } psStore(node, pdContOpen(cname, field, key)); return nil; }

    // run a .passkit source stored inside a container field (code as data)
    if (op == "exec") {
        let src = pdContGet(cname, field);
        if (src == nil) { psStore(node, { ok: false, message: "The field is empty or missing" }); return nil; }
        let r = psRunChild(toString(src), psPrefixed(node, "in."), PKS["base"]);
        if (psHas(node, "quiet") == false) { let i = 0; while (i < len(r["output"])) { psEmit(r["output"][i]); i = i + 1; } }
        psStore(node, { ok: r["ok"], value: r["value"], vars: r["vars"], output: r["output"], message: r["message"] });
        return nil;
    }

    // two-way binding variable <-> field: bind registers and pulls the value, and sync pushes/pulls
    if (op == "bind") {
        let vn2 = psStrAttr(node, "var");
        if (vn2 == nil) { return psFail(node, "bind needs var"); }
        push(PKS["binds"], { of: cname, field: field, var: vn2 });
        if (pdContHas(cname, field)) { PKS["vars"][vn2] = pdContGet(cname, field); }
        psStore(node, { ok: true, bound: len(PKS["binds"]) });
        return nil;
    }
    return psFail(node, "op is unknown: " + op);
}

// <container of="x" op="sync" dir="push|pull"/> without field: syncs all links registered with bind
fun psSyncBinds(dir) {
    let n = 0;
    for (let b in PKS["binds"]) {
        if (dir == "pull") {
            if (pdContHas(b["of"], b["field"])) { PKS["vars"][b["var"]] = pdContGet(b["of"], b["field"]); n = n + 1; }
        } else {
            if (has(PKS["vars"], b["var"])) { pdContSet(b["of"], b["field"], PKS["vars"][b["var"]]); n = n + 1; }
        }
    }
    return n;
}

// ---- Rin API for container linking ---------------------------------------------------------------------------

// container fields as ready-made inputs for passkitRun
fun passkitFromContainer(name) {
    return pdContToMap(name);
}

// write the variables of a run result into a container (prefix optional)
fun passkitToContainer(result, name, prefix) {
    return pdContFromMap(name, result["vars"], prefix);
}

// run a source stored in a container field; inputs are optional (by default the container's other fields)
fun passkitRunContainer(name, field, inputs) {
    let src = pdContGet(name, field);
    if (src == nil) { return psFailResult("Field not found: " + name + "." + field); }
    let inp = inputs;
    if (inp == nil) {
        inp = {};
        let m = pdContToMap(name);
        for (let k in keys(m)) { if (k != field) { inp[k] = m[k]; } }
    }
    return passkitRunSource(toString(src), inp);
}

// run + write the outputs back into the container with the same names prefixed by outPrefix (default "out_")
fun passkitRunLinked(name, field, outPrefix) {
    let r = passkitRunContainer(name, field, nil);
    if (r["ok"]) {
        let p = "out_";
        if (outPrefix != nil) { p = outPrefix; }
        pdContFromMap(name, r["vars"], p);
        if (r["value"] != nil) { pdContSet(name, p + "value", r["value"]); }
    }
    return r;
}
)PASSKITLANGOGRIN";
static const char* kLib_physics_og_rin = R"PHYSICSOGRIN(
// ============================================================================
//  lib/physics.og.rin — مكتبة فيزياء متكاملة فوق stdlib الأساسية
//  استيراد:
//    @import "lib/physics.og.rin";
//    @import "lib/physics.og.rin" as physics;
//
//  مكتبة Rin خالصة (بلا أي تعديل على محرّك C++)، قائمة بذاتها بالكامل مثل باقي
//  lib/*.og.rin (لا تعتمد على lib/math.og.rin ولا أي مكتبة أخرى، حتى تعمل بمجرد
//  استيرادها وحدها). لتفادي أي تعارض أسماء مع lib/math.og.rin عند استيراد
//  الاثنتين معاً في نفس النطاق (مثال شائع جداً في سكربتات الفيزياء/الألعاب)،
//  كل دالة هنا مسبوقة بـ px (Physics) — فمتجهاتها px* منفصلة تماماً عن vec2/vec3
//  في math.og.rin، وحتى المثلثات الداخلية هنا pxSin/pxCos منفصلة عن sin/cos هناك.
//
//  الوحدات المستخدمة افتراضياً في كل الصيغ: SI — كتلة بالكيلوغرام (kg)،
//  مسافة بالمتر (m)، زمن بالثانية (s)، قوة بالنيوتن (N)، طاقة بالجول (J).
//
//  الأقسام:
//    0) ثوابت فيزيائية
//    1) زوايا ومثلثات داخلية (Taylor + اختزال مجال، لأن المفسّر لا يدعمها فطرياً)
//    2) متجهات فيزيائية ثنائية الأبعاد (موضع/سرعة/قوة كمصفوفة [x, y])
//    3) حركة خطية أحادية البعد (kinematics)
//    4) حركة إسقاطية (projectile motion) — متجهية فوق الجاذبية
//    5) قوى ونيوتن (Newton's laws)
//    6) طاقة وشغل وزخم واصطدامات
//    7) حركة دائرية ودورانية
//    8) نوابض واهتزاز توافقي بسيط (SHM)
//    9) كثافة وضغط وطفو (سوائل)
//
//  مثال سريع (قذيفة تنطلق بسرعة 20م/ث وزاوية 45°):
//    let range = pxProjectileRange(20, 45, PX_G_EARTH);
//    let pos = pxProjectilePositionAt([0, 0], pxProjectileVelocity(20, 45), PX_G_EARTH, 1.0);
//    print range; print pos;
// ============================================================================

// ---------------------------------------------------------------------------
// 0) ثوابت فيزيائية (وحدات SI)
// ---------------------------------------------------------------------------
let PX_G_EARTH  = 9.81;          // تسارع الجاذبية على سطح الأرض (m/s²)
let PX_G_MOON   = 1.62;          // على سطح القمر (m/s²)
let PX_G_MARS   = 3.71;          // على سطح المريخ (m/s²)
let PX_C        = 299792458;     // سرعة الضوء في الفراغ (m/s)
let PX_G_CONST  = 0.0000000000667430; // ثابت الجذب العام G (m³·kg⁻¹·s⁻²)
let PX_ATM      = 101325;        // ضغط جوي قياسي (باسكال Pa)
let PX_WATER_DENSITY = 1000;     // كثافة الماء العذب (kg/m³)

// ---------------------------------------------------------------------------
// 1) زوايا ومثلثات داخلية — لا تعتمد على lib/math.og.rin عمداً (استقلالية كاملة)
// ---------------------------------------------------------------------------

fun pxDegToRad(deg) { return deg * (PI / 180); }
fun pxRadToDeg(rad) { return rad * (180 / PI); }

// يلفّ زاوية إلى المجال (-PI, PI] لتسريع/تدقيق تقارب سلاسل تايلور أدناه
fun _pxReduceAngle(x) {
    let tau = 2 * PI;
    return x - tau * floor((x + PI) / tau);
}

fun pxSin(x) {
    let v = _pxReduceAngle(x);
    let v2 = v * v;
    let term = v;
    let total = v;
    let i = 1;
    while (i <= 15) {
        term = term * (-v2) / ((2 * i) * (2 * i + 1));
        total = total + term;
        i = i + 1;
    }
    return total;
}

fun pxCos(x) {
    let v = _pxReduceAngle(x);
    let v2 = v * v;
    let term = 1;
    let total = 1;
    let i = 1;
    while (i <= 15) {
        term = term * (-v2) / ((2 * i - 1) * (2 * i));
        total = total + term;
        i = i + 1;
    }
    return total;
}

// سلسلة تايلور لـ atan تفترض |x| صغيرة (تُستخدم داخلياً بعد اختزال المجال)
fun _pxAtanTaylor(x) {
    let x2 = x * x;
    let term = x;
    let total = x;
    let i = 1;
    while (i <= 12) {
        term = term * (-x2);
        total = total + term / (2 * i + 1);
        i = i + 1;
    }
    return total;
}

fun _pxAtan(x) {
    let neg = x < 0;
    let v = x;
    if (neg) { v = -v; }
    let k = 0;
    while (v > 0.1 and k < 60) {
        v = v / (1 + sqrt(1 + v * v));
        k = k + 1;
    }
    let result = _pxAtanTaylor(v) * pow(2, k);
    if (neg) { return -result; }
    return result;
}

// atan2(y, x): زاوية النقطة (x, y) بالراديان، مع مراعاة الربع الصحيح
fun pxAtan2(y, x) {
    if (x > 0) { return _pxAtan(y / x); }
    if (x < 0) {
        if (y >= 0) { return _pxAtan(y / x) + PI; }
        return _pxAtan(y / x) - PI;
    }
    if (y > 0) { return PI / 2; }
    if (y < 0) { return -(PI / 2); }
    return 0;
}

// ---------------------------------------------------------------------------
// 2) متجهات فيزيائية ثنائية الأبعاد — موضع/سرعة/تسارع/قوة كمصفوفة [x, y]
// ---------------------------------------------------------------------------

fun pxVec(x, y) { return [x, y]; }
fun pxVecZero() { return [0, 0]; }
fun pxVecAdd(a, b) { return [a[0] + b[0], a[1] + b[1]]; }
fun pxVecSub(a, b) { return [a[0] - b[0], a[1] - b[1]]; }
fun pxVecScale(a, s) { return [a[0] * s, a[1] * s]; }
fun pxVecDot(a, b) { return a[0] * b[0] + a[1] * b[1]; }
fun pxVecLengthSq(a) { return a[0] * a[0] + a[1] * a[1]; }
fun pxVecLength(a) { return sqrt(pxVecLengthSq(a)); }

fun pxVecNormalize(a) {
    let len = pxVecLength(a);
    if (len == 0) { return [0, 0]; }
    return [a[0] / len, a[1] / len];
}

fun pxVecDistance(a, b) { return pxVecLength(pxVecSub(b, a)); }

// زاوية المتجه بالراديان (اتجاه حركته)
fun pxVecAngle(a) { return pxAtan2(a[1], a[0]); }

// يبني متجهاً من زاوية (راديان) ومقدار (magnitude) — عكس pxVecAngle/pxVecLength
fun pxVecFromAngle(angleRad, magnitude) {
    return [magnitude * pxCos(angleRad), magnitude * pxSin(angleRad)];
}

// مجموع مصفوفة متجهات (لجمع عدّة قوى مثلاً) دفعة واحدة
fun pxVecSum(vectors) {
    let total = [0, 0];
    let i = 0;
    while (i < len(vectors)) {
        total = pxVecAdd(total, vectors[i]);
        i = i + 1;
    }
    return total;
}

// ---------------------------------------------------------------------------
// 3) حركة خطية أحادية البعد (kinematics) — v0: سرعة ابتدائية، a: تسارع، t: زمن
// ---------------------------------------------------------------------------

// السرعة بعد زمن t بتسارع ثابت a
fun pxVelocityAfter(v0, a, t) { return v0 + a * t; }

// الإزاحة بعد زمن t بتسارع ثابت a
fun pxDisplacement(v0, a, t) { return v0 * t + 0.5 * a * t * t; }

// مربّع السرعة النهائية بعد إزاحة d بتسارع ثابت a (v² = v0² + 2ad) — بلا جذر
fun pxVelocitySquaredAfterDistance(v0, a, d) { return v0 * v0 + 2 * a * d; }

// السرعة النهائية (المقدار) بعد إزاحة d بتسارع ثابت a
fun pxFinalVelocity(v0, a, d) {
    let vSq = pxVelocitySquaredAfterDistance(v0, a, d);
    if (vSq < 0) { return 0; }
    return sqrt(vSq);
}

// متوسط السرعة خلال إزاحة d في زمن t
fun pxAverageVelocity(d, t) {
    if (t == 0) { return 0; }
    return d / t;
}

// الزمن اللازم لتوقّف جسم يتباطأ بتسارع a (سالب) من سرعة ابتدائية v0
fun pxTimeToStop(v0, a) {
    if (a == 0) { return nil; }
    return -v0 / a;
}

// ---------------------------------------------------------------------------
// 4) حركة إسقاطية (Projectile motion) — متجهية فوق الجاذبية g (موجبة دائماً)
// ---------------------------------------------------------------------------

// متجه السرعة الابتدائية من سرعة إطلاق (speed) وزاوية بالدرجات (angleDeg)
fun pxProjectileVelocity(speed, angleDeg) {
    return pxVecFromAngle(pxDegToRad(angleDeg), speed);
}

// المدى الأفقي الكلي على أرض مستوية: v²sin(2θ)/g
fun pxProjectileRange(speed, angleDeg, g) {
    if (g == 0) { return nil; }
    return (speed * speed * pxSin(2 * pxDegToRad(angleDeg))) / g;
}

// أقصى ارتفاع يصله المقذوف: v²sin²(θ)/(2g)
fun pxProjectileMaxHeight(speed, angleDeg, g) {
    if (g == 0) { return nil; }
    let s = pxSin(pxDegToRad(angleDeg));
    return (speed * speed * s * s) / (2 * g);
}

// زمن الطيران الكلي حتى العودة لنفس ارتفاع الإطلاق: 2·v·sin(θ)/g
fun pxProjectileTimeOfFlight(speed, angleDeg, g) {
    if (g == 0) { return nil; }
    return (2 * speed * pxSin(pxDegToRad(angleDeg))) / g;
}

// موضع المقذوف عند الزمن t، انطلاقاً من pos0 بسرعة ابتدائية vel0 وجاذبية g
// (خطوة محاكاة جاهزة للاستخدام داخل حلقة لعبة/محرّك فيزياء)
fun pxProjectilePositionAt(pos0, vel0, g, t) {
    let x = pos0[0] + vel0[0] * t;
    let y = pos0[1] + vel0[1] * t - 0.5 * g * t * t;
    return [x, y];
}

// سرعة المقذوف عند الزمن t (مكوّن x ثابت، y يتناقص بفعل الجاذبية)
fun pxProjectileVelocityAt(vel0, g, t) {
    return [vel0[0], vel0[1] - g * t];
}

// ---------------------------------------------------------------------------
// 5) قوى ونيوتن (Newton's laws) — F = m·a
// ---------------------------------------------------------------------------

fun pxForceScalar(mass, accel) { return mass * accel; }
fun pxForceVec(mass, accelVec) { return pxVecScale(accelVec, mass); }

// التسارع الناتج عن قوة على كتلة (F = m·a -> a = F/m)
fun pxAccelFromForce(force, mass) {
    if (mass == 0) { return nil; }
    return force / mass;
}

// وزن جسم (قوة الجاذبية عليه) بكتلة mass تحت تسارع جاذبية g
fun pxWeight(mass, g) { return mass * g; }

// محصّلة عدّة قوى متجهية (مصفوفة متجهات [x,y]) دفعة واحدة
fun pxNetForceVec(forces) { return pxVecSum(forces); }

// قوة الاحتكاك القصوى: μ (معامل الاحتكاك) × القوة العمودية
fun pxFriction(normalForce, mu) { return mu * normalForce; }

// القوة العمودية لجسم على سطح أفقي مستوٍ (بلا قوى رأسية أخرى)
fun pxNormalForceOnFlat(mass, g) { return mass * g; }

// ---------------------------------------------------------------------------
// 6) طاقة وشغل وزخم واصطدامات
// ---------------------------------------------------------------------------

fun pxKineticEnergy(mass, v) { return 0.5 * mass * v * v; }
fun pxPotentialEnergyGravity(mass, g, height) { return mass * g * height; }

// الشغل المبذول بقوة تصنع زاوية angleDeg مع اتجاه الإزاحة
fun pxWork(force, distance, angleDeg) {
    return force * distance * pxCos(pxDegToRad(angleDeg));
}

fun pxPower(work, time) {
    if (time == 0) { return nil; }
    return work / time;
}

fun pxSpringPotentialEnergy(k, displacement) { return 0.5 * k * displacement * displacement; }

fun pxMomentum(mass, v) { return mass * v; }
fun pxImpulse(force, time) { return force * time; }

// اصطدام عديم المرونة تماماً (الجسمان يلتصقان): سرعة مشتركة بعد الاصطدام
fun pxInelasticCollisionVelocity(m1, v1, m2, v2) {
    let totalMass = m1 + m2;
    if (totalMass == 0) { return nil; }
    return (m1 * v1 + m2 * v2) / totalMass;
}

// اصطدام مرن تماماً (1D): يُعيد {v1: السرعة الجديدة للجسم الأول, v2: للثاني}
fun pxElasticCollision(m1, v1, m2, v2) {
    let totalMass = m1 + m2;
    if (totalMass == 0) { return { v1: v1, v2: v2 }; }
    let newV1 = ((m1 - m2) * v1 + 2 * m2 * v2) / totalMass;
    let newV2 = ((m2 - m1) * v2 + 2 * m1 * v1) / totalMass;
    return { v1: newV1, v2: newV2 };
}

// ---------------------------------------------------------------------------
// 7) حركة دائرية ودورانية
// ---------------------------------------------------------------------------

// التسارع الجذبي (المركزي) لجسم يتحرك بسرعة v على مسار دائري نصف قطره r
fun pxCentripetalAcceleration(v, r) {
    if (r == 0) { return nil; }
    return (v * v) / r;
}

fun pxCentripetalForce(mass, v, r) {
    let a = pxCentripetalAcceleration(v, r);
    if (a == nil) { return nil; }
    return mass * a;
}

// السرعة الزاوية (راديان/ثانية) من الدور الزمني (الزمن الدوري) T
fun pxAngularVelocityFromPeriod(period) {
    if (period == 0) { return nil; }
    return (2 * PI) / period;
}

fun pxPeriodFromFrequency(freq) {
    if (freq == 0) { return nil; }
    return 1 / freq;
}

fun pxFrequencyFromPeriod(period) {
    if (period == 0) { return nil; }
    return 1 / period;
}

// السرعة الخطية المماسّية من السرعة الزاوية ونصف القطر (v = ω·r)
fun pxTangentialSpeed(angularVelocity, r) { return angularVelocity * r; }

// قوة الجذب العام بين كتلتين على بُعد r (قانون نيوتن للجاذبية الكونية)
fun pxGravitationalForce(m1, m2, r) {
    if (r == 0) { return nil; }
    return (PX_G_CONST * m1 * m2) / (r * r);
}

// ---------------------------------------------------------------------------
// 8) نوابض واهتزاز توافقي بسيط (SHM — Simple Harmonic Motion)
// ---------------------------------------------------------------------------

// قوة النابض حسب قانون هوك (سالبة الاتجاه دائماً نحو موضع الاتزان)
fun pxHookeForce(k, displacement) { return -k * displacement; }

// الدور الزمني لنابض-كتلة: T = 2π√(m/k)
fun pxSpringPeriod(mass, k) {
    if (k == 0) { return nil; }
    return 2 * PI * sqrt(mass / k);
}

// الدور الزمني لبندول بسيط بطول length تحت جاذبية g: T = 2π√(L/g)
fun pxPendulumPeriod(length, g) {
    if (g == 0) { return nil; }
    return 2 * PI * sqrt(length / g);
}

// موضع جسم يتحرك بحركة توافقية بسيطة عند الزمن t
// (amplitude: أقصى إزاحة، angularFreq: التردد الزاوي ω، phase: طور ابتدائي بالراديان)
fun pxSHMPositionAt(amplitude, angularFreq, t, phase) {
    return amplitude * pxCos(angularFreq * t + phase);
}

// سرعة نفس الجسم عند الزمن t (مشتقّة الموضع)
fun pxSHMVelocityAt(amplitude, angularFreq, t, phase) {
    return -amplitude * angularFreq * pxSin(angularFreq * t + phase);
}

// ---------------------------------------------------------------------------
// 9) كثافة وضغط وطفو (سوائل)
// ---------------------------------------------------------------------------

fun pxDensity(mass, volume) {
    if (volume == 0) { return nil; }
    return mass / volume;
}

fun pxPressure(force, area) {
    if (area == 0) { return nil; }
    return force / area;
}

// قوة الطفو (مبدأ أرخميدس): كثافة السائل × الحجم المُزاح × الجاذبية
fun pxBuoyantForce(fluidDensity, displacedVolume, g) {
    return fluidDensity * displacedVolume * g;
}

// هل الجسم يطفو على سائل كثافته fluidDensity؟ (يطفو إن كانت كثافته أقل)
fun pxWillFloat(objectDensity, fluidDensity) {
    return objectDensity < fluidDensity;
}
)PHYSICSOGRIN";

static const char* kLib_archivekit_og_rin = R"ARCHIVEKITOGRIN(
// ============================================================================
//  lib/archivekit.og.rin — إنشاء ملفات أرشيف وضغط حقيقية بلغة Rin خالصة
//  استيراد:
//    @import "lib/archivekit.og.rin";
//    @import "lib/archivekit.og.rin" as arc;
//
//  تبني هذه المكتبة الصيغ الثنائية الخام بنفسها (رؤوس + محاذاة + مجاميع تحقّق)
//  فوق natives الأساسية الموجودة فعلاً في المحرّك: crc32/zlibDeflateRaw/
//  zlibInflateRaw/chr/ord/substr/readFile/writeFile — بلا أي تعديل على C++،
//  بنفس روح lib/rinzip.og.rin (والتي تُستخدَم هنا مباشرة لصيغة .zip).
//
//  الصيغ المدعومة بضغط/تحزيم حقيقي 100% (تُفتح بأي أداة قياسية: tar, gzip,
//  ar, cpio, unzip, 7-Zip...):
//      .zip   — عبر lib/rinzip.og.rin (Store/Deflate حقيقي)
//      .tar   — تحزيم USTAR خام (بلا ضغط، هذا طبيعة TAR نفسه)
//      .gz    — ضغط GZIP حقيقي (DEFLATE + رأس/تذييل GZIP + CRC-32)
//      .tar.gz / .tgz — TAR ثم GZIP فوقه (نفس ما يفعله "tar czf")
//      .ar    — أرشيف Unix ar الكلاسيكي (بلا ضغط)
//      .cpio  — صيغة newc (SVR4) الحديثة (بلا ضغط)
//
//  صيغ تحتاج خوارزميات ضغط غير متوفرة كـ native في هذا المحرّك (لا توجد إلا
//  DEFLATE عبر zlib)؛ دوالها هنا تُعيد بوضوح {ok:false, error:"..."} بدل
//  إنتاج ملف تالف يُوهم بالنجاح: .bz2 (BZIP2) .xz (LZMA2) .zst (Zstandard)
//  .7z (LZMA) .rar (خوارزمية RAR مملوكة، لا مُرمِّز مفتوح أصلاً) .cab
//  (MSZIP/LZX) .lzh/.lha (متعدد خوارزميات LZSS الخاصة بـ LHA) .z (LZW).
//  راجع archFormatSupport() لجدول كامل وسبب كل حالة.
//
//  مثال سريع:
//    let files = [ arcFile("hello.txt", "أهلاً من أرشيف Rin!"),
//                  arcFile("data/notes.txt", "سطر أول\nسطر ثانٍ") ];
//    print arcTarSave(files, "out.tar");        // {ok:true, path:"out.tar", ...}
//    print arcGzipSaveFile("hello.txt", "أهلاً!"); // ضغط ملف مفرد إلى .gz
//    print arcTarGzSave(files, "out.tar.gz");   // .tar.gz كامل بضغط حقيقي
//    print arcArSave(files, "out.ar");          // أرشيف Unix ar
//    print arcCpioSave(files, "out.cpio");      // أرشيف cpio (newc)
// ============================================================================

// ---- 0) أدوات مشتركة: أعداد ثنائية/عشرية/ثمانية/سداسية عشرية ثابتة الطول ----

// يُرمّز n في القاعدة base كنص أرقام بلا صفر بادئ ("0" إن كان n صفراً)
fun arcDigits(n, base) {
    if (n == 0) { return "0"; }
    let alphabet = "0123456789abcdef";
    let result = "";
    let x = n;
    while (x > 0) {
        let d = x % base;
        result = substr(alphabet, d, 1) + result;
        x = floor(x / base);
    }
    return result;
}

// يُكمِل s بأصفار من اليسار حتى الطول width (لا يقصّ إن كان أطول أصلاً)
fun arcZeroPad(s, width) {
    let r = s;
    while (len(r) < width) { r = "0" + r; }
    return r;
}

// يُكمِل s بمسافات من اليمين حتى الطول width (محاذاة يسار، شائع في رؤوس ar/cpio)
fun arcSpacePadRight(s, width) {
    let r = s;
    while (len(r) < width) { r = r + " "; }
    return r;
}

// نص n العشري (base 10) مُكمَّل بأصفار من اليسار لعرض width — لرؤوس TAR الثمانية
fun arcOctalField(n, width) {
    return arcZeroPad(arcDigits(n, 8), width);
}

// نص n السداسي عشري (حروف صغيرة) مُكمَّل بأصفار من اليسار لعرض width — لرؤوس cpio
fun arcHexField(n, width) {
    return arcZeroPad(arcDigits(n, 16), width);
}

// n مُكرَّراً كمحرف ch عدد count مرة (نص خام، لا علاقة له بترميز اللغة نفسها)
fun arcRepeatChar(ch, count) {
    let r = "";
    let i = 0;
    while (i < count) { r = r + ch; i = i + 1; }
    return r;
}

// n كعدد 32-بت little-endian (4 بايتات خام) — لرأس/تذييل GZIP
fun arcLE32(n) {
    let b0 = n % 256;
    let b1 = floor(n / 256) % 256;
    let b2 = floor(n / 65536) % 256;
    let b3 = floor(n / 16777216) % 256;
    return chr(b0) + chr(b1) + chr(b2) + chr(b3);
}

// ---- 1) مُدخَل عام (يُستخدَم لكل من TAR/AR/CPIO) -----------------------------
// مُدخَل = { name, content, isDir }

// مُدخَل ملف عادي
fun arcFile(name, content) {
    return { name: name, content: content, isDir: false };
}

// مُدخَل مجلد (فارغ من المحتوى)
fun arcDir(name) {
    return { name: name, content: "", isDir: true };
}

// ============================================================================
//  2) .tar — صيغة USTAR القياسية (بلا ضغط؛ TAR أصلاً مجرّد تحزيم/تسلسل ملفات)
// ============================================================================

// مجموع كل بايتات s كأعداد صحيحة (crc بدائي يُستخدَم لمجموع تحقّق رأس TAR)
fun arcByteSum(s) {
    let sum = 0;
    let i = 0;
    while (i < len(s)) {
        sum = sum + ord(charAt(s, i));
        i = i + 1;
    }
    return sum;
}

// يبني كتلة رأس TAR واحدة (512 بايت بالضبط) لمُدخَل name/size/isDir معطى
fun arcTarHeader(name, size, isDir) {
    if (len(name) > 100) {
        return { ok: false, error: "arcTarHeader: اسم أطول من 100 محرف غير مدعوم (بلا حقل prefix): " + name };
    }
    let typeflag = "0";
    if (isDir) { typeflag = "5"; }

    let nameField = name;
    if (isDir and (len(nameField) == 0 or charAt(nameField, len(nameField) - 1) != "/")) {
        nameField = nameField + "/";
    }

    let fName     = arcSpacePadRight(nameField, 100);   // نستخدم مسافات هنا وسنستبدلها أصفاراً أدناه
    // اسم الملف والحقول النصية في TAR تُكمَّل فعلياً بـ NUL لا بمسافة؛ نبني عبر تكرار NUL مباشرة:
    fName = nameField + arcRepeatChar(chr(0), 100 - len(nameField));

    let fMode = arcOctalField(420, 7) + chr(0);         // 0644 مبنيّة كعدد عشري 420 = 0644 ثمانياً
    let fUid  = arcOctalField(0, 7) + chr(0);
    let fGid  = arcOctalField(0, 7) + chr(0);
    let fSize = arcOctalField(size, 11) + chr(0);
    let fMtime = arcOctalField(0, 11) + chr(0);
    let fChksumPlaceholder = arcRepeatChar(" ", 8);      // 8 مسافات مؤقتاً لحساب المجموع
    let fTypeflag = typeflag;
    let fLinkname = arcRepeatChar(chr(0), 100);
    let fMagic = "ustar" + chr(0);
    let fVersion = "00";
    let fUname = "root" + arcRepeatChar(chr(0), 32 - 4);
    let fGname = "root" + arcRepeatChar(chr(0), 32 - 4);
    let fDevmajor = arcOctalField(0, 7) + chr(0);
    let fDevminor = arcOctalField(0, 7) + chr(0);
    let fPrefix = arcRepeatChar(chr(0), 155);
    let fPad = arcRepeatChar(chr(0), 12);

    let preChecksum = fName + fMode + fUid + fGid + fSize + fMtime + fChksumPlaceholder
        + fTypeflag + fLinkname + fMagic + fVersion + fUname + fGname
        + fDevmajor + fDevminor + fPrefix + fPad;

    let chk = arcByteSum(preChecksum);
    let fChksum = arcOctalField(chk, 6) + chr(0) + " ";  // 6 أرقام ثمانية + NUL + مسافة = 8 بايت

    let header = fName + fMode + fUid + fGid + fSize + fMtime + fChksum
        + fTypeflag + fLinkname + fMagic + fVersion + fUname + fGname
        + fDevmajor + fDevminor + fPrefix + fPad;

    return { ok: true, value: header };
}

// يُكمِل s بأصفار NUL خام حتى يصبح طوله مضاعفاً لـ 512 (محاذاة كتل TAR)
fun arcPadTo512(s) {
    let rem = len(s) % 512;
    if (rem == 0) { return s; }
    return s + arcRepeatChar(chr(0), 512 - rem);
}

// يبني محتوى أرشيف .tar كامل (بايتات خام كنص) من مصفوفة entries (arcFile/arcDir).
// يُعيد {ok:true, value:bytes} أو {ok:false, error:...} عند اسم غير صالح
fun arcTarBuild(entries) {
    let body = "";
    let i = 0;
    while (i < len(entries)) {
        let e = entries[i];
        let isDir = has(e, "isDir") and e["isDir"];
        let content = "";
        if (isDir == false) { content = e["content"]; }
        let size = len(content);

        let hdr = arcTarHeader(e["name"], size, isDir);
        if (hdr["ok"] == false) { return hdr; }

        body = body + hdr["value"];
        if (isDir == false) {
            body = body + arcPadTo512(content);
        }
        i = i + 1;
    }
    // نهاية الأرشيف: كتلتان فارغتان (1024 بايت صفر) حسب مواصفة TAR
    body = body + arcRepeatChar(chr(0), 1024);
    return { ok: true, value: body };
}

// يبني الأرشيف عبر arcTarBuild ثم يكتبه مباشرة إلى outPath على القرص
fun arcTarSave(entries, outPath) {
    let built = arcTarBuild(entries);
    if (built["ok"] == false) { return built; }
    writeFile(outPath, built["value"]);
    return { ok: true, path: outPath, bytes: len(built["value"]), entries: len(entries) };
}

// ============================================================================
//  3) .gz — ضغط GZIP حقيقي لمحتوى مفرد (RFC 1952: رأس + DEFLATE خام + تذييل)
// ============================================================================

// يضغط content بالكامل إلى بايتات .gz خام (بلا اسم ملف داخلي، أبسط حالة صالحة)
fun arcGzipCompress(content) {
    let compressed = zlibDeflateRaw(content);
    let header = chr(31) + chr(139) + chr(8) + chr(0) + arcLE32(0) + chr(0) + chr(255);
    let crc = crc32(content);
    let isize = len(content) % 4294967296;
    let footer = arcLE32(crc) + arcLE32(isize);
    return header + compressed + footer;
}

// يضغط content ويكتبه مباشرة كملف .gz إلى outPath
fun arcGzipSave(content, outPath) {
    let bytes = arcGzipCompress(content);
    writeFile(outPath, bytes);
    let ratio = 1.0;
    if (len(content) > 0) { ratio = len(bytes) / len(content); }
    return { ok: true, path: outPath, bytes: len(bytes), originalBytes: len(content), ratio: ratio };
}

// يقرأ ملفاً موجوداً من القرص ثم يضغطه إلى outPath (اختصار مريح لضغط ملف قائم)
fun arcGzipCompressFile(srcPath, outPath) {
    if (fileExists(srcPath) == false) {
        return { ok: false, error: "arcGzipCompressFile: الملف غير موجود: " + srcPath };
    }
    return arcGzipSave(readFile(srcPath), outPath);
}

// ---- فكّ ضغط .gz (للتحقّق الذاتي ولقراءة أرشيفات .gz خارجية بسيطة) ---------

// يفكّ ضغط بايتات .gz خام (رأس أساسي بلا FEXTRA/FNAME/FCOMMENT) إلى {ok, value/error}
fun arcGunzip(data) {
    if (len(data) < 18 or ord(charAt(data, 0)) != 31 or ord(charAt(data, 1)) != 139) {
        return { ok: false, error: "arcGunzip: ليس رأس GZIP صالحاً" };
    }
    let flg = ord(charAt(data, 3));
    if (flg != 0) {
        return { ok: false, error: "arcGunzip: أعلام GZIP إضافية (FNAME/FEXTRA/...) غير مدعومة هنا" };
    }
    let isize = ord(charAt(data, len(data) - 4))
        + ord(charAt(data, len(data) - 3)) * 256
        + ord(charAt(data, len(data) - 2)) * 65536
        + ord(charAt(data, len(data) - 1)) * 16777216;
    let compressed = substr(data, 10, len(data) - 10 - 8);
    let content = zlibInflateRaw(compressed, isize);
    if (crc32(content) != (ord(charAt(data, len(data) - 8))
        + ord(charAt(data, len(data) - 7)) * 256
        + ord(charAt(data, len(data) - 6)) * 65536
        + ord(charAt(data, len(data) - 5)) * 16777216)) {
        return { ok: false, error: "arcGunzip: فشل التحقّق CRC-32 (بيانات تالفة)" };
    }
    return { ok: true, value: content };
}

// ============================================================================
//  4) .tar.gz / .tgz — TAR ثم GZIP فوقه دفعة واحدة (نفس أثر "tar czf")
// ============================================================================

fun arcTarGzSave(entries, outPath) {
    let built = arcTarBuild(entries);
    if (built["ok"] == false) { return built; }
    return arcGzipSave(built["value"], outPath);
}

// ============================================================================
//  5) .ar — أرشيف Unix ar الكلاسيكي (بلا ضغط؛ صيغة الرأس العام لملفات .a/.deb)
// ============================================================================

// رأس عنصر ar واحد (60 بايت بالضبط) — الاسم محدود بـ 16 محرفاً في هذا التطبيق
// المبسّط (بلا جدول أسماء ممتد GNU/BSD)
fun arcArHeader(name, size) {
    if (len(name) > 16) {
        return { ok: false, error: "arcArHeader: اسم أطول من 16 محرفاً غير مدعوم هنا: " + name };
    }
    let fName  = arcSpacePadRight(name, 16);
    let fMtime = arcSpacePadRight("0", 12);
    let fUid   = arcSpacePadRight("0", 6);
    let fGid   = arcSpacePadRight("0", 6);
    let fMode  = arcSpacePadRight(arcDigits(420, 8), 8); // 0644 عشري=420 بالقاعدة الثمانية أعلاه؛ منسوخ decimal-octal كنص فقط
    // ملاحظة: حقل mode في ar ثماني الأساس نصّياً (كنص "100644" مثلاً)، لذا نبنيه صراحة:
    fMode = arcSpacePadRight("100644", 8);
    let fSize  = arcSpacePadRight(toString(size), 10);
    let fEnd   = chr(96) + chr(10); // "`\n"
    return { ok: true, value: fName + fMtime + fUid + fGid + fMode + fSize + fEnd };
}

// يبني محتوى أرشيف .ar كامل من مصفوفة entries (المجلدات تُتجاهَل، ar لا يدعمها)
fun arcArBuild(entries) {
    let body = "!<arch>\n";
    let i = 0;
    while (i < len(entries)) {
        let e = entries[i];
        let isDir = has(e, "isDir") and e["isDir"];
        if (isDir == false) {
            let content = e["content"];
            let hdr = arcArHeader(e["name"], len(content));
            if (hdr["ok"] == false) { return hdr; }
            body = body + hdr["value"] + content;
            if (len(content) % 2 == 1) { body = body + "\n"; } // محاذاة إلى عدد زوجي من البايتات
        }
        i = i + 1;
    }
    return { ok: true, value: body };
}

fun arcArSave(entries, outPath) {
    let built = arcArBuild(entries);
    if (built["ok"] == false) { return built; }
    writeFile(outPath, built["value"]);
    return { ok: true, path: outPath, bytes: len(built["value"]), entries: len(entries) };
}

// ============================================================================
//  6) .cpio — صيغة "newc" (SVR4 بلا CRC) الحديثة، بلا ضغط
// ============================================================================

// يُكمِل s بأصفار NUL حتى يصبح طوله مضاعفاً لـ 4 (محاذاة cpio لكل من الرأس+الاسم والبيانات)
fun arcPadTo4(s) {
    let rem = len(s) % 4;
    if (rem == 0) { return s; }
    return s + arcRepeatChar(chr(0), 4 - rem);
}

// رأس + اسم عنصر cpio واحد (newc)، مُكمَّل بالفعل لمضاعف 4
fun arcCpioHeader(name, size, isDir) {
    let mode = 33188; // 0100644 ثمانياً = ملف عادي rw-r--r--
    if (isDir) { mode = 16877; } // 0040755 ثمانياً = مجلد rwxr-xr-x
    let nameWithNul = name + chr(0);
    let magic = "070701";
    let header = magic
        + arcHexField(0, 8)              // ino
        + arcHexField(mode, 8)           // mode
        + arcHexField(0, 8)              // uid
        + arcHexField(0, 8)              // gid
        + arcHexField(1, 8)              // nlink
        + arcHexField(0, 8)              // mtime
        + arcHexField(size, 8)           // filesize
        + arcHexField(0, 8)              // devmajor
        + arcHexField(0, 8)              // devminor
        + arcHexField(0, 8)              // rdevmajor
        + arcHexField(0, 8)              // rdevminor
        + arcHexField(len(nameWithNul), 8) // namesize (بما فيه NUL الفاصل)
        + arcHexField(0, 8);             // check (0 في newc)
    return arcPadTo4(header + nameWithNul);
}

// يبني محتوى أرشيف .cpio كامل (newc) من مصفوفة entries، بما فيها المجلدات
fun arcCpioBuild(entries) {
    let body = "";
    let i = 0;
    while (i < len(entries)) {
        let e = entries[i];
        let isDir = has(e, "isDir") and e["isDir"];
        let content = "";
        if (isDir == false) { content = e["content"]; }
        body = body + arcCpioHeader(e["name"], len(content), isDir);
        if (isDir == false) {
            body = body + arcPadTo4(content);
        }
        i = i + 1;
    }
    // عنصر النهاية الإلزامي "TRAILER!!!"
    body = body + arcCpioHeader("TRAILER!!!", 0, false);
    // محاذاة تقليدية لمضاعف 512 بايت لكامل الأرشيف (كما تفعل أدوات cpio الحقيقية)
    let rem = len(body) % 512;
    if (rem != 0) { body = body + arcRepeatChar(chr(0), 512 - rem); }
    return { ok: true, value: body };
}

fun arcCpioSave(entries, outPath) {
    let built = arcCpioBuild(entries);
    writeFile(outPath, built["value"]);
    return { ok: true, path: outPath, bytes: len(built["value"]), entries: len(entries) };
}

// ============================================================================
//  7) .bz2 / .xz / .zst — ضغط حقيقي عبر natives bz2Compress/xzCompress/zstdCompress
// ============================================================================
// خلافاً لـ zlibDeflateRaw (خام، يحتاج تغليف ZIP/GZIP يدوياً)، هذه الثلاثة natives تُنتج
// مباشرة تدفّق/إطار/حاوية كاملة وصالحة بذاتها (.bz2/.xz/.zst الرسميين)، فلا حاجة لأي
// رأس/تذييل إضافي هنا — الناتج يُكتَب للقرص كما هو ويُفتح بأي أداة قياسية (bzip2, xz, zstd,
// 7-Zip...) مباشرة. natives هذا القسم مسجَّلة فقط عندما بُني المحرّك بأعلام RIN_HAVE_BZ2/
// RIN_HAVE_ZSTD/RIN_HAVE_LZMA (متوفرة في بنية سطح المكتب/لينكس الحالية؛ انظر تعليق natives
// نفسها في rin_interpreter.cpp لسبب عدم تفعيلها افتراضياً على Android/WASM). إن استُدعيت
// هذه الدوال على محرّك بُني بلا هذه الأعلام، ستفشل باستدعاء دالة native غير مسجَّلة —
// استخدم archFormatSupportTable() لمعرفة ما هو مُفعَّل فعلياً في نسختك الحالية من المحرّك.

// يضغط content بالكامل إلى بايتات .bz2 خام جاهزة للكتابة مباشرة
fun arcBzip2Compress(content) {
    return bz2Compress(content);
}

// يضغط content ويكتبه مباشرة كملف .bz2 إلى outPath
fun arcBzip2Save(content, outPath) {
    let bytes = arcBzip2Compress(content);
    writeFile(outPath, bytes);
    let ratio = 1.0;
    if (len(content) > 0) { ratio = len(bytes) / len(content); }
    return { ok: true, path: outPath, bytes: len(bytes), originalBytes: len(content), ratio: ratio };
}

// يفكّ ضغط بايتات .bz2 خام (يعرف الحجم الأصلي مسبقاً originalSize، إذ لا يُخزَّن داخل bz2 نفسه)
fun arcBunzip2(data, originalSize) {
    return { ok: true, value: bz2Decompress(data, originalSize) };
}

// TAR ثم BZIP2 فوقه دفعة واحدة (نفس أثر "tar cjf")
fun arcTarBz2Save(entries, outPath) {
    let built = arcTarBuild(entries);
    if (built["ok"] == false) { return built; }
    return arcBzip2Save(built["value"], outPath);
}

// يضغط content بالكامل إلى بايتات .xz خام جاهزة للكتابة مباشرة
fun arcXzCompress(content) {
    return xzCompress(content);
}

fun arcXzSave(content, outPath) {
    let bytes = arcXzCompress(content);
    writeFile(outPath, bytes);
    let ratio = 1.0;
    if (len(content) > 0) { ratio = len(bytes) / len(content); }
    return { ok: true, path: outPath, bytes: len(bytes), originalBytes: len(content), ratio: ratio };
}

fun arcUnxz(data, originalSize) {
    return { ok: true, value: xzDecompress(data, originalSize) };
}

// TAR ثم XZ فوقه دفعة واحدة (نفس أثر "tar cJf")
fun arcTarXzSave(entries, outPath) {
    let built = arcTarBuild(entries);
    if (built["ok"] == false) { return built; }
    return arcXzSave(built["value"], outPath);
}

// يضغط content بالكامل إلى بايتات .zst خام جاهزة للكتابة مباشرة
fun arcZstdCompress(content) {
    return zstdCompress(content);
}

fun arcZstdSave(content, outPath) {
    let bytes = arcZstdCompress(content);
    writeFile(outPath, bytes);
    let ratio = 1.0;
    if (len(content) > 0) { ratio = len(bytes) / len(content); }
    return { ok: true, path: outPath, bytes: len(bytes), originalBytes: len(content), ratio: ratio };
}

fun arcUnzstd(data, originalSize) {
    return { ok: true, value: zstdDecompress(data, originalSize) };
}

// TAR ثم Zstandard فوقه دفعة واحدة (نفس أثر "tar --zstd -cf")
fun arcTarZstSave(entries, outPath) {
    let built = arcTarBuild(entries);
    if (built["ok"] == false) { return built; }
    return arcZstdSave(built["value"], outPath);
}

// ============================================================================
//  8) صيغ ما تزال غير مدعومة (تحتاج خوارزميات/حاويات أعقد من مجرد stream ضغط واحد)
// ============================================================================
// هذه تحتاج فوق خوارزمية الضغط نفسها حاوية ملف خاصة معقّدة (فهرس مركزي مشفَّر
// اختيارياً في 7z، جدول أسماء طويلة في RAR5، بنية CFHEADER/CFFOLDER متعددة الأجزاء
// في CAB) — أو، في حالة RAR، لا مُرمِّز مفتوح المصدر أصلاً يمكن الاستناد إليه.

fun arc7zBuild(entries) {
    return { ok: false, error: ".7z يحتاج مُرمِّز LZMA وحاوية 7z معقّدة (رؤوس مشفَّرة اختيارياً)، غير متوفرة كـ native." };
}

fun arcRarBuild(entries) {
    return { ok: false, error: ".rar خوارزمية RAR مملوكة (WinRAR/RARLAB)؛ لا يوجد مُرمِّز مفتوح المصدر أصلاً، فضلاً عن native هنا." };
}

fun arcCabBuild(entries) {
    return { ok: false, error: ".cab (Microsoft Cabinet) يحتاج ضغط MSZIP/LZX ورأس CFHEADER/CFFOLDER/CFDATA متخصّص، غير متوفر كـ native." };
}

fun arcLhaBuild(entries) {
    return { ok: false, error: ".lzh/.lha يحتاج خوارزميات LZSS الخاصة بـ LHA (lh5/lh6/lh7)، غير متوفرة كـ native." };
}

fun arcCompressLzw(content) {
    return { ok: false, error: ".Z (Unix compress) يحتاج مُرمِّز LZW متغيّر العرض، غير متوفر كـ native حالياً." };
}

// ============================================================================
//  8) جدول دعم شامل + معلومات المكتبة
// ============================================================================

// جدول كامل بكل امتداد مطلوب: هل مدعوم فعلياً بضغط/تحزيم حقيقي، والدالة المسؤولة
fun archFormatSupport() {
    return [
        { ext: ".zip",           supported: true,  via: "lib/rinzip.og.rin (rzSaveArchive)" },
        { ext: ".tar",           supported: true,  via: "arcTarSave" },
        { ext: ".tar.gz/.tgz",   supported: true,  via: "arcTarGzSave" },
        { ext: ".tar.bz2/.tbz2", supported: true,  via: "arcTarBz2Save (يحتاج بناء المحرّك بعلم RIN_HAVE_BZ2)" },
        { ext: ".tar.xz/.txz",   supported: true,  via: "arcTarXzSave (يحتاج بناء المحرّك بعلم RIN_HAVE_LZMA)" },
        { ext: ".tar.zst",       supported: true,  via: "arcTarZstSave (يحتاج بناء المحرّك بعلم RIN_HAVE_ZSTD)" },
        { ext: ".gz",            supported: true,  via: "arcGzipSave" },
        { ext: ".bz2",           supported: true,  via: "arcBzip2Save (يحتاج بناء المحرّك بعلم RIN_HAVE_BZ2)" },
        { ext: ".xz",            supported: true,  via: "arcXzSave (يحتاج بناء المحرّك بعلم RIN_HAVE_LZMA)" },
        { ext: ".zst",           supported: true,  via: "arcZstdSave (يحتاج بناء المحرّك بعلم RIN_HAVE_ZSTD)" },
        { ext: ".7z",            supported: false, via: "arc7zBuild — يحتاج native LZMA + حاوية 7z" },
        { ext: ".rar",           supported: false, via: "arcRarBuild — خوارزمية مملوكة، لا مُرمِّز مفتوح" },
        { ext: ".cab",           supported: false, via: "arcCabBuild — يحتاج native MSZIP/LZX" },
        { ext: ".ar",            supported: true,  via: "arcArSave" },
        { ext: ".cpio",          supported: true,  via: "arcCpioSave" },
        { ext: ".lzh/.lha",      supported: false, via: "arcLhaBuild — يحتاج خوارزميات LZSS الخاصة بـ LHA" },
        { ext: ".z",             supported: false, via: "arcCompressLzw — يحتاج مُرمِّز LZW" }
    ];
}

// نص جدول مقروء جاهز للطباعة مباشرة عبر print
fun archFormatSupportTable() {
    let rows = archFormatSupport();
    let lines = [];
    push(lines, "الامتداد            الحالة       عبر");
    let i = 0;
    while (i < len(rows)) {
        let r = rows[i];
        let status = "❌ غير مدعوم";
        if (r["supported"]) { status = "✅ مدعوم"; }
        push(lines, arcSpacePadRight(r["ext"], 18) + arcSpacePadRight(status, 13) + r["via"]);
        i = i + 1;
    }
    return join(lines, "\n");
}

fun archInfo() {
    return {
        name: "archivekit",
        version: "1.0.0",
        description: "إنشاء أرشيفات/ضغط حقيقية بلغة Rin خالصة: zip (عبر rinzip)، tar، tar.gz/tgz، tar.bz2/xz/zst، gz/bz2/xz/zst، ar، cpio — مع جدول واضح لما لا يزال يحتاج حاوية أعقد (7z/rar/cab/lha/Z)",
        exports: [
            "arcFile", "arcDir",
            "arcTarBuild", "arcTarSave",
            "arcGzipCompress", "arcGzipSave", "arcGzipCompressFile", "arcGunzip", "arcTarGzSave",
            "arcBzip2Compress", "arcBzip2Save", "arcBunzip2", "arcTarBz2Save",
            "arcXzCompress", "arcXzSave", "arcUnxz", "arcTarXzSave",
            "arcZstdCompress", "arcZstdSave", "arcUnzstd", "arcTarZstSave",
            "arcArBuild", "arcArSave",
            "arcCpioBuild", "arcCpioSave",
            "archFormatSupport", "archFormatSupportTable", "archInfo"
        ]
    };
}

)ARCHIVEKITOGRIN";

static const char* kLib_rintest_og_rin = R"RINTESTOGRIN(
// rintest — إطار اختبارات Rin (rintests)
// ---------------------------------------------------------------------------
//   @import "rintest";
//
//   rt_describe("الحساب", fun() {
//       rt_test("الجمع", fun() { rt_expect(1 + 1).toBe(2); });
//       rt_test("خطأ متوقَّع", fun() { rt_expectThrows(fun() { fail("boom"); }, "boom"); });
//   });
//   rt_done();            // يطبع الملخص ويُفشل الملف (rin test) إن فشل أي اختبار
//
// Matchers:  toBe toEqual toBeTrue toBeFalse toBeNil toBeTruthy toBeFalsy toContain
//            toHaveLength toBeGreaterThan toBeLessThan toBeCloseTo toMatch toHaveKey
//            toBeType toThrow       (rt_expectNot(x) يعكس كل matcher)
// Hooks:     rt_beforeEach(fn) rt_afterEach(fn)        Tools: rt_skip rt_each rt_bench(name,fn,times) rt_bench1(name,fn)
// ملاحظة: دوال Rin تتطلب عدد وسائط مطابقاً تماماً، لذا مرّر nil للوسيط الاختياري (toThrow(nil) أو toThrowAny()).
// ---------------------------------------------------------------------------

let __rt = {
    passed: 0, failed: 0, skipped: 0,
    failures: [], stack: [], beforeEach: [], afterEach: [],
    verbose: true, startedAt: now()
};

fun rt_show(v) {
    if (type(v) == "string") { return "\"" + v + "\""; }
    if (type(v) == "function") { return "<function>"; }
    return json.stringify(v);
}

fun rt_fullName(name) {
    let parts = __rt["stack"];
    if (len(parts) == 0) { return name; }
    return join(parts, " › ") + " › " + name;
}

fun rt_quiet(flag) { __rt["verbose"] = flag; }

fun rt_reset() {
    __rt["passed"] = 0; __rt["failed"] = 0; __rt["skipped"] = 0;
    __rt["failures"] = []; __rt["stack"] = [];
    __rt["beforeEach"] = []; __rt["afterEach"] = [];
    __rt["startedAt"] = now();
}

fun rt_stats() {
    return {
        passed: __rt["passed"], failed: __rt["failed"], skipped: __rt["skipped"],
        total: __rt["passed"] + __rt["failed"] + __rt["skipped"],
        failures: __rt["failures"]
    };
}

fun rt_beforeEach(fn) { push(__rt["beforeEach"], fn); }
fun rt_afterEach(fn) { push(__rt["afterEach"], fn); }

fun rt_describe(name, fn) {
    if (__rt["verbose"]) { print name; }
    push(__rt["stack"], name);
    let saveB = __rt["beforeEach"];
    let saveA = __rt["afterEach"];
    __rt["beforeEach"] = slice(saveB, 0);
    __rt["afterEach"] = slice(saveA, 0);
    try { fn(); } catch (e) {
        __rt["failed"] = __rt["failed"] + 1;
        push(__rt["failures"], {name: rt_fullName("(describe body)"), message: rt_errText(e)});
        print "  ✗ (describe body) — " + rt_errText(e);
    }
    __rt["beforeEach"] = saveB;
    __rt["afterEach"] = saveA;
    pop(__rt["stack"]);
}

fun rt_errText(e) {
    let msg = "" + e;
    if (type(e) == "map" and has(e, "message")) { msg = e["message"]; }
    return regexReplace(msg, "^\\[E[0-9]+\\] ", "");
}

fun rt_test(name, fn) {
    let t0 = now();
    let err = nil;
    try {
        for (let h in __rt["beforeEach"]) { h(); }
        fn();
    } catch (e) { err = rt_errText(e); }
    try {
        for (let h in __rt["afterEach"]) { h(); }
    } catch (e2) { if (err == nil) { err = "afterEach: " + rt_errText(e2); } }
    let ms = now() - t0;
    if (err == nil) {
        __rt["passed"] = __rt["passed"] + 1;
        if (__rt["verbose"]) { print "  ✓ " + name + " (" + round(ms) + "ms)"; }
        return true;
    }
    __rt["failed"] = __rt["failed"] + 1;
    push(__rt["failures"], {name: rt_fullName(name), message: err});
    print "  ✗ " + name;
    print "      " + err;
    return false;
}

fun rt_skip(name, fn) {
    __rt["skipped"] = __rt["skipped"] + 1;
    if (__rt["verbose"]) { print "  - " + name + " (skipped)"; }
}

// اختبار بجدول حالات: rt_each("جمع", [[1,2,3],[2,2,4]], fun(a,b,c){ ... })
fun rt_each(name, cases, fn) {
    let i = 0;
    for (let c in cases) {
        let label = name + " #" + i + " " + json.stringify(c);
        rt_test(label, fun() { callFn(fn, c); });
        i = i + 1;
    }
}

fun rt_bench1(name, fn) { return rt_bench(name, fn, 100); }

fun rt_bench(name, fn, times) {
    let n = times;
    if (n == nil) { n = 100; }
    let t0 = now();
    let i = 0;
    while (i < n) { fn(); i = i + 1; }
    let total = now() - t0;
    print "  ⏱ " + name + ": " + total + "ms / " + n + " runs";
    return total;
}

// ------------------------------------------------------------------ matchers
fun rt_check(ok, negate, msgPositive, msgNegative) {
    let pass = ok;
    if (negate) { pass = !ok; }
    if (!pass) {
        if (negate) { fail(msgNegative); }
        fail(msgPositive);
    }
    return true;
}

fun rt_threwNote(threw, etxt) {
    if (threw) { return " (it threw: " + etxt + ")"; }
    return " (it did not throw)";
}

fun rt_makeExpect(actual, negate) {
    let m = {};
    m["toBe"] = fun(expected) {
        return rt_check(actual == expected, negate,
            "expected " + rt_show(expected) + " but got " + rt_show(actual),
            "expected value not to be " + rt_show(expected));
    };
    m["toEqual"] = m["toBe"];
    m["toBeTrue"] = fun() { return rt_check(actual == true, negate, "expected true but got " + rt_show(actual), "expected not true"); };
    m["toBeFalse"] = fun() { return rt_check(actual == false, negate, "expected false but got " + rt_show(actual), "expected not false"); };
    m["toBeNil"] = fun() { return rt_check(actual == nil, negate, "expected nil but got " + rt_show(actual), "expected a non-nil value"); };
    m["toBeTruthy"] = fun() {
        let t = false;
        if (actual) { t = true; }
        return rt_check(t, negate, "expected a truthy value but got " + rt_show(actual), "expected a falsy value but got " + rt_show(actual));
    };
    m["toBeFalsy"] = fun() {
        let t = true;
        if (actual) { t = false; }
        return rt_check(t, negate, "expected a falsy value but got " + rt_show(actual), "expected a truthy value but got " + rt_show(actual));
    };
    m["toContain"] = fun(item) {
        return rt_check(contains(actual, item), negate,
            rt_show(actual) + " should contain " + rt_show(item),
            rt_show(actual) + " should not contain " + rt_show(item));
    };
    m["toHaveLength"] = fun(n) {
        return rt_check(len(actual) == n, negate,
            "expected length " + n + " but got " + len(actual),
            "expected length not to be " + n);
    };
    m["toBeGreaterThan"] = fun(x) { return rt_check(actual > x, negate, rt_show(actual) + " should be > " + rt_show(x), rt_show(actual) + " should not be > " + rt_show(x)); };
    m["toBeLessThan"] = fun(x) { return rt_check(actual < x, negate, rt_show(actual) + " should be < " + rt_show(x), rt_show(actual) + " should not be < " + rt_show(x)); };
    m["toBeCloseTo"] = fun(x, eps) {
        let e = eps;
        if (e == nil) { e = 0.000001; }
        return rt_check(abs(actual - x) <= e, negate,
            rt_show(actual) + " should be within " + e + " of " + rt_show(x),
            rt_show(actual) + " should not be within " + e + " of " + rt_show(x));
    };
    m["toMatch"] = fun(pattern) {
        return rt_check(regexTest(actual, pattern), negate,
            rt_show(actual) + " should match /" + pattern + "/",
            rt_show(actual) + " should not match /" + pattern + "/");
    };
    m["toHaveKey"] = fun(k) { return rt_check(has(actual, k), negate, "expected key " + rt_show(k) + " in " + rt_show(actual), "unexpected key " + rt_show(k)); };
    m["toBeType"] = fun(t) { return rt_check(type(actual) == t, negate, "expected type " + t + " but got " + type(actual), "expected type not to be " + t); };
    m["toThrow"] = fun(fragment) {
        let threw = false;
        let etxt = "";
        try { actual(); } catch (e) { threw = true; etxt = rt_errText(e); }
        let ok = threw;
        if (threw and fragment != nil) { ok = contains(etxt, fragment); }
        let want = "";
        if (fragment != nil) { want = " containing " + rt_show(fragment); }
        return rt_check(ok, negate, "expected function to throw" + want + rt_threwNote(threw, etxt), "expected function not to throw" + want);
    };
    m["toThrowAny"] = fun() { return m["toThrow"](nil); };
    return m;
}

fun rt_expect(actual) { return rt_makeExpect(actual, false); }
fun rt_expectNot(actual) { return rt_makeExpect(actual, true); }

fun rt_expectThrows(fn, fragment) {
    let threw = false;
    let etxt = "";
    try { fn(); } catch (e) { threw = true; etxt = rt_errText(e); }
    if (!threw) { fail("expected function to throw but it did not"); }
    if (fragment != nil and !contains(etxt, fragment)) {
        fail("expected error containing " + rt_show(fragment) + " but got: " + etxt);
    }
    return etxt;
}

// ------------------------------------------------------------------ summary
fun rt_done() {
    let s = rt_stats();
    let ms = now() - __rt["startedAt"];
    print "";
    print "rintest: " + s["passed"] + " passed, " + s["failed"] + " failed, " + s["skipped"] + " skipped (" + s["total"] + " total, " + ms + "ms)";
    if (s["failed"] > 0) {
        print "Failures:";
        for (let f in s["failures"]) { print "  - " + f["name"] + ": " + f["message"]; }
        fail("rintest: " + s["failed"] + " test(s) failed");
    }
    print "ALL PASSED";
    return true;
}
)RINTESTOGRIN";

static const char* kLib_packkit_og_rin = R"PACKKITOGRIN(
// packkit — نظام مكتبات/حزم/إضافات داخل Rin نفسها (بلا ملفات، بلا شبكة)
// ---------------------------------------------------------------------------
//   @import "packkit";
//
//   pk_define("mathx", "1.2.0", {
//       description: "دوال رياضية",
//       exports: fun(ctx) { return { twice: fun(x) { return x * 2; } }; }
//   });
//   pk_define("app", "0.1.0", {
//       deps: { mathx: "^1.0.0" },
//       exports: fun(ctx) { let m = ctx["deps"]["mathx"]; return { run: fun() { return m.twice(21); } }; }
//   });
//   let app = pk_require("app");   // يحمّل mathx أولاً تلقائياً (مرة واحدة فقط)
//   print app.run();               // 42
//
// يعتمد على: semver.* و pkg.depOrder و json.mergeDeep (دوال المحرك الأصلية، Rin 1.0).
// ---------------------------------------------------------------------------

let __pk = { defs: {}, loaded: {}, loading: [], hooks: {}, config: {}, warned: {}, nextId: 1 };

fun pk_reset() {
    __pk["defs"] = {}; __pk["loaded"] = {}; __pk["loading"] = [];
    __pk["hooks"] = {}; __pk["config"] = {}; __pk["warned"] = {}; __pk["nextId"] = 1;
}

// ------------------------------------------------------------------ define
fun pk_define(name, version, spec) {
    if (type(name) != "string" or !regexTest(name, "^[a-z][a-z0-9_-]{1,63}$")) {
        fail("pk_define: invalid package name " + json.stringify(name) + " (lowercase, digits, - and _)");
    }
    if (!semver.valid(version)) { fail("pk_define: invalid version '" + version + "' for " + name); }
    let s = spec;
    if (s == nil) { s = {}; }
    if (has(s, "deps")) {
        for (let d in keys(s["deps"])) {
            if (!semver.validRange(s["deps"][d])) {
                fail("pk_define: " + name + " has an invalid constraint for '" + d + "': " + s["deps"][d]);
            }
            if (d == name) { fail("pk_define: " + name + " cannot depend on itself"); }
        }
    }
    if (!has(__pk["defs"], name)) { __pk["defs"][name] = {}; }
    if (has(__pk["defs"][name], version) and !(has(s, "replace") and s["replace"])) {
        fail("pk_define: " + name + "@" + version + " is already defined (pass replace: true to override)");
    }
    __pk["defs"][name][version] = s;
    if (has(__pk["loaded"], name + "@" + version)) { remove(__pk["loaded"], name + "@" + version); }
    pk_emit("define", { name: name, version: version });
    return name + "@" + version;
}

// يعرّف حزمة من rin.toml (أو من قاموس المانيفست) مع صادراتها
fun pk_defineFromManifest(manifest, exportsOrFn) {
    let m = manifest;
    if (type(m) == "string") { m = pkg.readManifest(m); }
    if (m == nil) { fail("pk_defineFromManifest: manifest not found"); }
    let v = pkg.validateManifest(m);
    if (!v["valid"]) { fail("pk_defineFromManifest: " + join(v["errors"], "; ")); }
    let deps = json.get(m, "dependencies", {});
    let spec = { deps: deps, exports: exportsOrFn, description: json.get(m, "package.description", "") };
    return pk_define(m["package"]["name"], m["package"]["version"], spec);
}

// ------------------------------------------------------------------ query
fun pk_has(name, constraint) {
    if (!has(__pk["defs"], name)) { return false; }
    if (constraint == nil) { return true; }
    return semver.maxSatisfying(keys(__pk["defs"][name]), constraint) != nil;
}

fun pk_versions(name) {
    if (!has(__pk["defs"], name)) { return []; }
    return semver.sort(keys(__pk["defs"][name]), true);
}

fun pk_latest(name, constraint) {
    if (!has(__pk["defs"], name)) { return nil; }
    let c = constraint;
    if (c == nil) { c = "*"; }
    return semver.maxSatisfying(keys(__pk["defs"][name]), c);
}

fun pk_info(name, constraint) {
    let v = pk_latest(name, constraint);
    if (v == nil) { return nil; }
    let spec = __pk["defs"][name][v];
    return {
        name: name, version: v,
        description: json.get(spec, "description", ""),
        deps: json.get(spec, "deps", {}),
        loaded: has(__pk["loaded"], name + "@" + v),
        versions: pk_versions(name)
    };
}

fun pk_list() {
    let out = [];
    for (let n in sort(keys(__pk["defs"]))) { push(out, pk_info(n, nil)); }
    return out;
}

// ترتيب التحميل الصحيح لكل الحزم المعرّفة (تبعيات أولاً) + كشف الدورات
fun pk_order() {
    let g = {};
    for (let n in keys(__pk["defs"])) {
        let v = pk_latest(n, nil);
        g[n] = keys(json.get(__pk["defs"][n][v], "deps", {}));
    }
    return pkg.depOrder(g);
}

// ------------------------------------------------------------------ load
fun pk_require(name, constraint) {
    if (!has(__pk["defs"], name)) { fail("pk_require: package '" + name + "' is not defined"); }
    let c = constraint;
    if (c == nil) { c = "*"; }
    let v = semver.maxSatisfying(keys(__pk["defs"][name]), c);
    if (v == nil) {
        fail("pk_require: no version of '" + name + "' satisfies " + c + " (available: " + join(pk_versions(name), ", ") + ")");
    }
    let key = name + "@" + v;
    if (has(__pk["loaded"], key)) { return __pk["loaded"][key]; }
    if (contains(__pk["loading"], key)) {
        fail("pk_require: circular dependency: " + join(__pk["loading"], " -> ") + " -> " + key);
    }
    push(__pk["loading"], key);
    let spec = __pk["defs"][name][v];
    let ctx = { name: name, version: v, deps: {}, config: pk_configOf(name, json.get(spec, "config", {})) };
    try {
        let deps = json.get(spec, "deps", {});
        for (let d in keys(deps)) { ctx["deps"][d] = pk_require(d, deps[d]); }
        let ex = json.get(spec, "exports", {});
        let result = ex;
        if (type(ex) == "function") { result = ex(ctx); }
        if (has(spec, "init")) { spec["init"](ctx); }
        __pk["loaded"][key] = result;
    } catch (e) {
        pop(__pk["loading"]);
        fail(rt_msgOf(e));
    }
    pop(__pk["loading"]);
    pk_emit("load", { name: name, version: v });
    return __pk["loaded"][key];
}

fun rt_msgOf(e) {
    if (type(e) == "map" and has(e, "message")) { return regexReplace(e["message"], "^\\[E[0-9]+\\] ", ""); }
    return "" + e;
}

fun pk_loadAll() {
    let ord = pk_order();
    if (!ord["ok"]) { fail("pk_loadAll: circular dependency: " + join(ord["cycle"], " -> ")); }
    for (let n in ord["order"]) { pk_require(n, nil); }
    return ord["order"];
}

fun pk_call(name, fnName, args) {
    let ex = pk_require(name, nil);
    if (!has(ex, fnName)) { fail("pk_call: '" + name + "' has no export '" + fnName + "'"); }
    let a = args;
    if (a == nil) { a = []; }
    return callFn(ex[fnName], a);
}

fun pk_undefine(name) {
    if (!has(__pk["defs"], name)) { return false; }
    for (let v in keys(__pk["defs"][name])) {
        if (has(__pk["loaded"], name + "@" + v)) { remove(__pk["loaded"], name + "@" + v); }
    }
    remove(__pk["defs"], name);
    return true;
}

// ------------------------------------------------------------------ config
fun pk_configOf(name, defaults) {
    let over = {};
    if (has(__pk["config"], name)) { over = __pk["config"][name]; }
    return json.mergeDeep(defaults, over);
}
fun pk_setConfig(name, values) {
    let cur = {};
    if (has(__pk["config"], name)) { cur = __pk["config"][name]; }
    __pk["config"][name] = json.mergeDeep(cur, values);
    return __pk["config"][name];
}

// ------------------------------------------------------------------ hooks / plugins
fun pk_on(event, fn) {
    if (!has(__pk["hooks"], event)) { __pk["hooks"][event] = []; }
    let id = __pk["nextId"];
    __pk["nextId"] = id + 1;
    push(__pk["hooks"][event], { id: id, fn: fn });
    return id;
}
fun pk_off(event, id) {
    if (!has(__pk["hooks"], event)) { return false; }
    let keep = [];
    let removed = false;
    for (let h in __pk["hooks"][event]) {
        if (h["id"] == id) { removed = true; } else { push(keep, h); }
    }
    __pk["hooks"][event] = keep;
    return removed;
}
fun pk_emit(event, data) {
    if (!has(__pk["hooks"], event)) { return 0; }
    let n = 0;
    for (let h in __pk["hooks"][event]) { h["fn"](data); n = n + 1; }
    return n;
}

// ------------------------------------------------------------------ utilities
// تخزين مؤقت لدالة بوسيط واحد أو أكثر (المفتاح = JSON الوسائط)
fun pk_memoize(fn) {
    let cache = {};
    let wrapper = fun(a) {
        let k = json.canonical(a);
        if (!has(cache, k)) { cache[k] = fn(a); }
        return cache[k];
    };
    return wrapper;
}

// يطبع تحذير إهمال مرة واحدة فقط لكل رسالة
fun pk_deprecated(message) {
    if (has(__pk["warned"], message)) { return false; }
    __pk["warned"][message] = true;
    print "⚠ deprecated: " + message;
    return true;
}

// يتحقق أن ما صدّرته الحزمة يطابق الأسماء المتوقعة (عقد واجهة)
fun pk_implements(exportsMap, names) {
    let missing = [];
    for (let n in names) { if (!has(exportsMap, n)) { push(missing, n); } }
    return { ok: len(missing) == 0, missing: missing };
}

// اختصارات بوسيط واحد (دوال Rin تتطلب عدد وسائط مطابقاً تماماً — مرّر nil حيث لا قيد)
fun pk_use(name) { return pk_require(name, nil); }
fun pk_ver(name) { return pk_latest(name, nil); }
fun pk_get(name) { return pk_info(name, nil); }
)PACKKITOGRIN";

static const char* kLib_wesscode_og_rin = R"WESSCODEOGRIN(
// wesscode — مساعد إنشاء المكتبات والحزم في Rin
// ---------------------------------------------------------------------------
//   @import "wesscode";
//
//   let lib = wc_new("mathx", "0.1.0", "دوال رياضية");
//   wc_fn(lib, "twice", ["x"], "return x * 2;", "يضاعف الرقم");
//   wc_const(lib, "PI2", "6.283185307179586", "ضعف باي");
//   wc_test(lib, "twice يضاعف", "twice(21)", 42);
//   let r = wc_write(lib, "out/mathx");        // حزمة كاملة: rin.toml + src + tests + docs
//   wc_writeOg(lib, ".");                      // أو ./lib/mathx.og.rin لـ @import "mathx"
//
// قوالب جاهزة:  wc_template("math"|"strings"|"collections"|"validate", "اسم-المكتبة")
// جسر C++:      wc_cpp(lib, "fast_sum", "double t=0; for(auto&v:args.arr) t+=v.num; return rin::Json::number(t);")
// ملاحظة: دوال Rin تتطلب عدد وسائط مطابقاً تماماً — مرّر nil للاختياري.
// يعتمد على: lang.check و json.* و semver.* و pkg.* (محرك Rin الأصلي).
// ---------------------------------------------------------------------------

let __wc_nl = "\n";

// ------------------------------------------------------------------ helpers
fun wc_ident(name) { return replace(name, "-", "_"); }

fun wc_indent(code, n) {
    let pad = "";
    let i = 0;
    while (i < n) { pad = pad + " "; i = i + 1; }
    let out = [];
    for (let ln in split(code, __wc_nl)) {
        if (trim(ln) == "") { push(out, ""); } else { push(out, pad + ln); }
    }
    return join(out, __wc_nl);
}

// يحوّل قيمة Rin إلى نص مصدر Rin صالح
fun wc_lit(v) {
    let t = type(v);
    if (t == "nil") { return "nil"; }
    if (t == "bool") { if (v) { return "true"; } return "false"; }
    if (t == "number") { return "" + v; }
    if (t == "string") {
        let s = replace(v, "\\", "\\\\");
        s = replace(s, "\"", "\\\"");
        s = replace(s, __wc_nl, "\\n");
        return "\"" + s + "\"";
    }
    if (t == "array") {
        let parts = [];
        for (let x in v) { push(parts, wc_lit(x)); }
        return "[" + join(parts, ", ") + "]";
    }
    if (t == "map") {
        let parts = [];
        for (let k in keys(v)) { push(parts, wc_lit(k) + ": " + wc_lit(v[k])); }
        return "{" + join(parts, ", ") + "}";
    }
    fail("wc_lit: cannot render a value of type " + t);
}

fun wc_docLines(doc) {
    if (doc == nil or doc == "") { return ""; }
    let out = [];
    for (let ln in split(doc, __wc_nl)) { push(out, "/// " + ln); }
    return join(out, __wc_nl) + __wc_nl;
}

fun wc_msg(e) {
    let m = "" + e;
    if (type(e) == "map" and has(e, "message")) { m = e["message"]; }
    return regexReplace(m, "^\\[E[0-9]+\\] ", "");
}

// اسم صالح فعلاً: يمرّ على نفس محلّل Rin (فتُكشف الكلمات المحجوزة مثل text/merge/file)
fun wc_validIdent(name) {
    if (type(name) != "string" or !regexTest(name, "^[A-Za-z_][A-Za-z0-9_]*$")) { return false; }
    return lang.check("let " + name + " = 1;")["ok"];
}

// اسم دالة صالح: اسم صالح + لا يتعارض مع دالة مدمجة في المحرك (مثل clamp/len/max)
fun wc_validFnName(name) {
    return wc_validIdent(name) and !lang.isBuiltin(name);
}

fun wc_validName(name) {
    return type(name) == "string" and regexTest(name, "^[a-z][a-z0-9_-]{1,63}$");
}

// ------------------------------------------------------------------ build the spec
fun wc_new(name, version, description) {
    if (!wc_validName(name)) { fail("wc_new: invalid library name '" + name + "' (lowercase, digits, - and _; 2+ chars)"); }
    if (!semver.valid(version)) { fail("wc_new: invalid version '" + version + "'"); }
    return {
        name: name, version: version, description: description,
        author: "", license: "MIT",
        deps: {}, funcs: [], consts: [], classes: [], tests: [], cpp: [], raw: [],
        layout: {mode: "single", strategy: "kind", opts: nil}
    };
}

fun wc_meta(lib, author, license) {
    lib["author"] = author;
    lib["license"] = license;
    return lib;
}

fun wc_dep(lib, depName, constraint) {
    if (!semver.validRange(constraint)) { fail("wc_dep: invalid constraint '" + constraint + "' for " + depName); }
    lib["deps"][depName] = constraint;
    return lib;
}

fun wc_findIn(list, name) {
    let i = 0;
    for (let x in list) {
        if (x["name"] == name) { return i; }
        i = i + 1;
    }
    return -1;
}

fun wc_fn(lib, name, params, body, doc) {
    if (!wc_validFnName(name)) { fail("wc_fn: '" + name + "' is invalid, reserved, or collides with a built-in function"); }
    for (let p in params) {
        if (!wc_validIdent(p)) { fail("wc_fn: invalid or reserved parameter name '" + p + "' in " + name); }
    }
    let entry = {name: name, params: params, body: body, doc: doc};
    let i = wc_findIn(lib["funcs"], name);
    if (i >= 0) { lib["funcs"][i] = entry; } else { push(lib["funcs"], entry); }
    return lib;
}

fun wc_const(lib, name, expr, doc) {
    if (!wc_validIdent(name)) { fail("wc_const: invalid or reserved name '" + name + "'"); }
    let entry = {name: name, expr: expr, doc: doc};
    let i = wc_findIn(lib["consts"], name);
    if (i >= 0) { lib["consts"][i] = entry; } else { push(lib["consts"], entry); }
    return lib;
}

fun wc_class(lib, name, fields, doc) {
    if (!wc_validIdent(name)) { fail("wc_class: invalid or reserved class name '" + name + "'"); }
    let entry = {name: name, fields: fields, doc: doc, methods: []};
    let i = wc_findIn(lib["classes"], name);
    if (i >= 0) { lib["classes"][i] = entry; } else { push(lib["classes"], entry); }
    return lib;
}

// fields: قاموس {اسم: تعبير_القيمة_الافتراضية_كنص}
fun wc_method(lib, className, name, params, body) {
    let i = wc_findIn(lib["classes"], className);
    if (i < 0) { fail("wc_method: class '" + className + "' is not defined"); }
    push(lib["classes"][i]["methods"], {name: name, params: params, body: body});
    return lib;
}

// expr: تعبير Rin كنص، expected: قيمة Rin عادية (تُحوَّل إلى مصدر تلقائياً)
fun wc_test(lib, name, expr, expected) {
    push(lib["tests"], {name: name, expr: expr, expected: expected, native: false});
    return lib;
}

// اختبار يعتمد على جسر C++: يُتخطّى تلقائياً إن لم يُفعَّل (rin --allow-native)
fun wc_testNative(lib, name, expr, expected) {
    push(lib["tests"], {name: name, expr: expr, expected: expected, native: true});
    return lib;
}

// يضيف مصدر Rin خاماً كما هو (لدمج كود موجود)
fun wc_raw(lib, src) {
    push(lib["raw"], src);
    return lib;
}

// دالة C++ بنمط JSON ABI: body يرى args (مصفوفة) ويُرجع rin::Json
fun wc_cpp(lib, name, body) {
    if (!wc_validFnName(name)) { fail("wc_cpp: '" + name + "' is invalid, reserved, or collides with a built-in function"); }
    push(lib["cpp"], {name: name, body: body});
    return lib;
}

// ------------------------------------------------------------------ rendering
fun wc_cppSource(lib) {
    let src = "#include \"rin_abi.h\"" + __wc_nl + __wc_nl;
    for (let c in lib["cpp"]) {
        src = src + "RIN_FN(" + c["name"] + ") {" + __wc_nl + wc_indent(c["body"], 4) + __wc_nl + "}" + __wc_nl + __wc_nl;
    }
    return src;
}

fun wc_renderFn(f) {
    return wc_docLines(f["doc"]) + "fun " + f["name"] + "(" + join(f["params"], ", ") + ") {" + __wc_nl
         + wc_indent(f["body"], 4) + __wc_nl + "}" + __wc_nl;
}

fun wc_renderClass(c) {
    let out = wc_docLines(c["doc"]) + "class " + c["name"] + " {" + __wc_nl;
    for (let k in keys(c["fields"])) { out = out + "    let " + k + " = " + c["fields"][k] + ";" + __wc_nl; }
    for (let m in c["methods"]) {
        out = out + "    fun " + m["name"] + "(" + join(m["params"], ", ") + ") {" + __wc_nl
            + wc_indent(m["body"], 8) + __wc_nl + "    }" + __wc_nl;
    }
    return out + "}" + __wc_nl;
}

fun wc_render(lib) {
    let id = wc_ident(lib["name"]);
    let out = "// " + lib["name"] + " " + lib["version"] + " — " + lib["description"] + __wc_nl
            + "// مولَّدة بواسطة wesscode (Rin)" + __wc_nl + __wc_nl;
    for (let c in lib["consts"]) { out = out + wc_docLines(c["doc"]) + "let " + c["name"] + " = " + c["expr"] + ";" + __wc_nl; }
    if (len(lib["consts"]) > 0) { out = out + __wc_nl; }
    for (let r in lib["raw"]) { out = out + r + __wc_nl + __wc_nl; }
    for (let f in lib["funcs"]) { out = out + wc_renderFn(f) + __wc_nl; }
    for (let c in lib["classes"]) { out = out + wc_renderClass(c) + __wc_nl; }
    if (len(lib["cpp"]) > 0) {
        out = out + "// ---- جسر C++ (يتطلب rin --allow-native) ----" + __wc_nl;
        out = out + "let __" + id + "_native = {h: nil, src: " + wc_lit(wc_cppSource(lib)) + "};" + __wc_nl + __wc_nl;
        out = out + "fun " + id + "_load() {" + __wc_nl
            + "    if (__" + id + "_native[\"h\"] == nil) {" + __wc_nl
            + "        let r = cpp.lib(__" + id + "_native[\"src\"], nil);" + __wc_nl
            + "        if (!r[\"ok\"]) { fail(\"" + lib["name"] + ": C++ bridge unavailable: \" + r[\"error\"]); }" + __wc_nl
            + "        __" + id + "_native[\"h\"] = r[\"handle\"];" + __wc_nl
            + "    }" + __wc_nl
            + "    return __" + id + "_native[\"h\"];" + __wc_nl + "}" + __wc_nl + __wc_nl;
        for (let c in lib["cpp"]) {
            out = out + "/// نداء الدالة الأصلية " + c["name"] + " (JSON ABI)" + __wc_nl
                + "fun " + c["name"] + "(args) {" + __wc_nl
                + "    return cpp.call(" + id + "_load(), \"" + c["name"] + "\", args)[\"value\"];" + __wc_nl + "}" + __wc_nl + __wc_nl;
        }
    }
    out = out + "/// إصدار المكتبة." + __wc_nl + "fun " + id + "_version() {" + __wc_nl
        + "    return \"" + lib["version"] + "\";" + __wc_nl + "}" + __wc_nl;
    return out;
}

fun wc_renderTests(lib) {
    let id = wc_ident(lib["name"]);
    let out = "// اختبارات " + lib["name"] + " — rin tests/" + id + "_test.rin" + __wc_nl
            + "@import \"rintest\";" + __wc_nl + "@import \"../src/lib.rin\";" + __wc_nl + __wc_nl;
    out = out + "rt_describe(" + wc_lit(lib["name"]) + ", fun() {" + __wc_nl;
    out = out + "    rt_test(\"version صالح\", fun() { rt_expect(semver.valid(" + id + "_version())).toBeTrue(); });" + __wc_nl;
    for (let t in lib["tests"]) {
        let line = "rt_test(" + wc_lit(t["name"]) + ", fun() { rt_expect(" + t["expr"] + ").toBe(" + wc_lit(t["expected"]) + "); });";
        if (t["native"]) {
            out = out + "    if (cpp.enabled()) { " + line + " } else { rt_skip(" + wc_lit(t["name"]) + ", fun() {}); }" + __wc_nl;
        } else {
            out = out + "    " + line + __wc_nl;
        }
    }
    out = out + "});" + __wc_nl + __wc_nl + "rt_done();" + __wc_nl;
    return out;
}

fun wc_manifest(lib) {
    let pk = {name: lib["name"], version: lib["version"], description: lib["description"], license: lib["license"]};
    if (lib["author"] != "") { pk["authors"] = [lib["author"]]; }
    return pkg.manifest({package: pk, dependencies: lib["deps"]});
}

fun wc_docs(lib) {
    let out = "# " + lib["name"] + " " + lib["version"] + __wc_nl + __wc_nl + lib["description"] + __wc_nl + __wc_nl;
    out = out + "```rin" + __wc_nl + "@import \"" + lib["name"] + "\";" + __wc_nl + "```" + __wc_nl + __wc_nl;
    if (len(lib["consts"]) > 0) {
        out = out + "## الثوابت" + __wc_nl + __wc_nl;
        for (let c in lib["consts"]) { out = out + "- `" + c["name"] + "` = `" + c["expr"] + "` — " + c["doc"] + __wc_nl; }
        out = out + __wc_nl;
    }
    if (len(lib["funcs"]) + len(lib["cpp"]) > 0) {
        out = out + "## الدوال" + __wc_nl + __wc_nl;
        for (let f in lib["funcs"]) {
            out = out + "### `" + f["name"] + "(" + join(f["params"], ", ") + ")`" + __wc_nl + f["doc"] + __wc_nl + __wc_nl;
        }
        for (let c in lib["cpp"]) { out = out + "### `" + c["name"] + "(args)` (C++)" + __wc_nl + "دالة أصلية بنمط JSON ABI." + __wc_nl + __wc_nl; }
    }
    if (len(lib["classes"]) > 0) {
        out = out + "## الأصناف" + __wc_nl + __wc_nl;
        for (let c in lib["classes"]) {
            out = out + "### `" + c["name"] + "`" + __wc_nl + c["doc"] + __wc_nl + __wc_nl;
            for (let m in c["methods"]) { out = out + "- `" + m["name"] + "(" + join(m["params"], ", ") + ")`" + __wc_nl; }
            out = out + __wc_nl;
        }
    }
    return out;
}

// ------------------------------------------------------------------ check
fun wc_check(lib) {
    let errors = [];
    let warnings = [];
    if (len(lib["funcs"]) + len(lib["classes"]) + len(lib["consts"]) + len(lib["cpp"]) + len(lib["raw"]) == 0) {
        push(warnings, "المكتبة فارغة (لا دوال ولا أصناف ولا ثوابت)");
    }
    let seen = {};
    for (let f in lib["funcs"]) {
        if (has(seen, f["name"])) { push(errors, "اسم مكرر: " + f["name"]); }
        seen[f["name"]] = true;
        if (f["doc"] == nil or f["doc"] == "") { push(warnings, "الدالة " + f["name"] + " بلا وثيقة"); }
        let one = lang.check(wc_renderFn(f));
        if (!one["ok"]) { push(errors, "الدالة " + f["name"] + ": " + one["message"]); }
    }
    for (let c in lib["classes"]) {
        if (has(seen, c["name"])) { push(errors, "اسم مكرر: " + c["name"]); }
        seen[c["name"]] = true;
        let one = lang.check(wc_renderClass(c));
        if (!one["ok"]) { push(errors, "الصنف " + c["name"] + ": " + one["message"]); }
    }
    for (let c in lib["consts"]) {
        if (has(seen, c["name"])) { push(errors, "اسم مكرر: " + c["name"]); }
        seen[c["name"]] = true;
    }
    for (let c in lib["cpp"]) {
        if (has(seen, c["name"])) { push(errors, "اسم مكرر: " + c["name"]); }
        seen[c["name"]] = true;
    }
    if (len(errors) == 0) {
        let all = lang.check(wc_render(lib));
        if (!all["ok"]) { push(errors, "المصدر الكامل: سطر " + all["line"] + ": " + all["message"]); }
        let tt = lang.check(wc_renderTests(lib));
        if (!tt["ok"]) { push(errors, "الاختبارات: سطر " + tt["line"] + ": " + tt["message"]); }
    }
    if (len(errors) == 0 and lib["layout"]["mode"] != "single") {
        let cv = wc_convert(wc_render(lib), lib["name"], lib["layout"]["mode"], lib["layout"]["strategy"], lib["layout"]["opts"]);
        if (!cv["ok"]) { push(errors, "التخطيط " + lib["layout"]["mode"] + ": " + cv["error"]); }
        else { for (let w in cv["warnings"]) { push(warnings, w); } }
    }
    if (len(lib["tests"]) == 0) { push(warnings, "لا اختبارات مضافة (wc_test)"); }
    return {ok: len(errors) == 0, errors: errors, warnings: warnings};
}

// ------------------------------------------------------------------ write
fun wc_write(lib, dir) {
    let c = wc_check(lib);
    if (!c["ok"]) { return {ok: false, errors: c["errors"], files: []}; }
    let id = wc_ident(lib["name"]);
    let files = [];
    let out = {};
    out["rin.toml"] = wc_manifest(lib);
    let lay = lib["layout"];
    let rendered = wc_render(lib);
    if (lay["mode"] == "parts") {
        let popts = {importBase: "src/"};
        if (lay["opts"] != nil) { popts = json.mergeDeep(lay["opts"], popts); }
        let pr = wc_toParts(rendered, lib["name"], lay["strategy"], popts);
        out["src/lib.rin"] = pr["entry"];
        for (let pp in pr["order"]) { out["src/" + pp] = pr["parts"][pp]; }
    } else if (lay["mode"] == "sections") {
        out["src/lib.rin"] = wc_toSections(rendered, lib["name"], lay["strategy"], lay["opts"])["source"];
    } else {
        out["src/lib.rin"] = rendered;
    }
    out["tests/" + id + "_test.rin"] = wc_renderTests(lib);
    out["docs/API.md"] = wc_docs(lib);
    out["README.md"] = "# " + lib["name"] + __wc_nl + __wc_nl + lib["description"] + __wc_nl + __wc_nl
        + "راجع [docs/API.md](docs/API.md)." + __wc_nl + __wc_nl + "## الاختبار" + __wc_nl + __wc_nl + "```" + __wc_nl + "rin tests/" + id + "_test.rin" + __wc_nl + "```" + __wc_nl;
    out[".gitignore"] = "build/" + __wc_nl + "dist/" + __wc_nl + "*.rcl" + __wc_nl;
    if (len(lib["cpp"]) > 0) { out["native/" + id + ".cpp"] = wc_cppSource(lib); }
    for (let p in keys(out)) {
        if (!writeFile(dir + "/" + p, out[p])) { return {ok: false, errors: ["cannot write " + dir + "/" + p], files: files}; }
        push(files, dir + "/" + p);
    }
    return {ok: true, errors: [], files: files, warnings: c["warnings"]};
}

// مكتبة واحدة بصيغة lib/<name>.og.rin ليعمل @import "<name>" داخل المشروع
fun wc_writeOg(lib, projectDir) {
    let c = wc_check(lib);
    if (!c["ok"]) { return {ok: false, errors: c["errors"], files: []}; }
    let path = projectDir + "/lib/" + lib["name"] + ".og.rin";
    let ok = writeFile(path, wc_render(lib));
    return {ok: ok, errors: [], files: [path]};
}

// ------------------------------------------------------------------ evolve
fun wc_bump(lib, kind) {
    lib["version"] = semver.bump(lib["version"], kind, "rc");
    return lib;
}

fun wc_stats(lib) {
    let methods = 0;
    for (let c in lib["classes"]) { methods = methods + len(c["methods"]); }
    return {
        name: lib["name"], version: lib["version"], functions: len(lib["funcs"]), constants: len(lib["consts"]),
        classes: len(lib["classes"]), methods: methods, native: len(lib["cpp"]), tests: len(lib["tests"]),
        lines: len(split(wc_render(lib), __wc_nl))
    };
}

// يبني مواصفة من مصدر Rin موجود (كل ما فيه يُنسخ كما هو + تُستخرج الواجهة للتوثيق)
fun wc_fromSource(name, version, description, src) {
    let lib = wc_new(name, version, description);
    wc_raw(lib, src);
    return lib;
}

fun wc_apiOf(lib) { return pkg.api(wc_render(lib)); }

// ------------------------------------------------------------------ templates
fun wc_template(kind, name) {
    let lib = nil;
    if (kind == "math") {
        lib = wc_new(name, "0.1.0", "دوال رياضية مساعدة");
        wc_fn(lib, "clampTo", ["x", "lo", "hi"], "if (x < lo) { return lo; }" + __wc_nl + "if (x > hi) { return hi; }" + __wc_nl + "return x;", "يقيّد x بين lo و hi");
        wc_fn(lib, "lerpNum", ["a", "b", "t"], "return a + (b - a) * t;", "استيفاء خطي بين a و b");
        wc_fn(lib, "sumList", ["xs"], "let t = 0;" + __wc_nl + "for (let x in xs) { t = t + x; }" + __wc_nl + "return t;", "مجموع عناصر مصفوفة");
        wc_fn(lib, "meanOf", ["xs"], "if (len(xs) == 0) { return 0; }" + __wc_nl + "return " + wc_ident(name) + "_sum(xs) / len(xs);", "المتوسط الحسابي");
        wc_fn(lib, wc_ident(name) + "_sum", ["xs"], "return sumList(xs);", "مرادف لـ sumList");
        wc_test(lib, "clampTo أعلى", "clampTo(15, 0, 10)", 10);
        wc_test(lib, "clampTo أدنى", "clampTo(-5, 0, 10)", 0);
        wc_test(lib, "lerpNum", "lerpNum(0, 10, 0.5)", 5);
        wc_test(lib, "sumList", "sumList([1, 2, 3])", 6);
        wc_test(lib, "meanOf", "meanOf([2, 4, 6])", 4);
    }
    if (kind == "strings") {
        lib = wc_new(name, "0.1.0", "أدوات نصوص");
        wc_fn(lib, "slugify", ["s"], "let r = lower(trim(s));" + __wc_nl + "r = regexReplace(r, \"[^a-z0-9]+\", \"-\");" + __wc_nl + "return regexReplace(r, \"^-+|-+$\", \"\");", "يحوّل نصاً إلى slug صالح للروابط");
        wc_fn(lib, "capitalize", ["s"], "if (len(s) == 0) { return s; }" + __wc_nl + "return upper(substr(s, 0, 1)) + substr(s, 1, len(s) - 1);", "يكبّر أول حرف");
        wc_fn(lib, "countWord", ["s", "w"], "return len(split(s, w)) - 1;", "عدد مرات ظهور كلمة");
        wc_test(lib, "slugify", "slugify(\"Hello World!\")", "hello-world");
        wc_test(lib, "capitalize", "capitalize(\"rin\")", "Rin");
        wc_test(lib, "countWord", "countWord(\"a b a c a\", \"a\")", 3);
    }
    if (kind == "collections") {
        lib = wc_new(name, "0.1.0", "أدوات مصفوفات وقواميس");
        wc_fn(lib, "uniq", ["xs"], "let out = [];" + __wc_nl + "for (let x in xs) { if (!contains(out, x)) { push(out, x); } }" + __wc_nl + "return out;", "يزيل التكرار مع حفظ الترتيب");
        wc_fn(lib, "chunk", ["xs", "n"], "let out = [];" + __wc_nl + "let cur = [];" + __wc_nl + "for (let x in xs) {" + __wc_nl + "    push(cur, x);" + __wc_nl + "    if (len(cur) == n) { push(out, cur); cur = []; }" + __wc_nl + "}" + __wc_nl + "if (len(cur) > 0) { push(out, cur); }" + __wc_nl + "return out;", "يقسّم المصفوفة إلى أجزاء بطول n");
        wc_fn(lib, "pluck", ["rows", "k"], "let out = [];" + __wc_nl + "for (let r in rows) { push(out, r[k]); }" + __wc_nl + "return out;", "يستخرج حقلاً من مصفوفة قواميس");
        wc_test(lib, "uniq", "uniq([1, 2, 1, 3, 2])", [1, 2, 3]);
        wc_test(lib, "chunk", "chunk([1, 2, 3, 4, 5], 2)", [[1, 2], [3, 4], [5]]);
        wc_test(lib, "pluck", "pluck([{a: 1}, {a: 2}], \"a\")", [1, 2]);
    }
    if (kind == "validate") {
        lib = wc_new(name, "0.1.0", "التحقق من البيانات بمخططات JSON");
        wc_fn(lib, "isEmail", ["s"], "return net.isValidEmail(s);", "هل النص بريد صالح");
        wc_fn(lib, "check", ["value", "schema"], "return json.validate(value, schema);", "يتحقق من قيمة بمخطط");
        wc_fn(lib, "assertValid", ["value", "schema"], "let r = json.validate(value, schema);" + __wc_nl + "if (!r[\"valid\"]) { fail(\"invalid: \" + json.stringify(r[\"errors\"])); }" + __wc_nl + "return value;", "يرمي خطأ إن لم تطابق القيمة المخطط");
        wc_test(lib, "isEmail صالح", "isEmail(\"a@b.com\")", true);
        wc_test(lib, "isEmail فاسد", "isEmail(\"nope\")", false);
        wc_test(lib, "check صالح", "check(5, {type: \"integer\"})[\"valid\"]", true);
        wc_test(lib, "check فاسد", "check(\"x\", {type: \"integer\"})[\"valid\"]", false);
    }
    if (lib == nil) { fail("wc_template: unknown kind '" + kind + "' (math|strings|collections|validate)"); }
    return lib;
}

fun wc_templates() { return ["math", "strings", "collections", "validate"]; }

// ===========================================================================
//  التحويلتان: جزء (Parts) وقسم (Sections)
// ---------------------------------------------------------------------------
//  wc_toParts(src, name, strategy, opts)     → ملف واحد  ⇒  lib.rin + parts/*.rin (ملفات منفصلة تُستورد)
//  wc_toSections(src, name, strategy, opts)  → ملف واحد  ⇒  ملف واحد بأقسام #region وفهرس
//  العكس:  wc_mergeParts(entry, parts)  و  wc_fromSections(src)
//  strategy:  "kind" (ثوابت/دوال/أنواع) | "prefix" (بادئة الاسم str_ / strTrim) | "size" (opts.size تصريحاً لكل جزء) | "map"
//  opts:      nil أو {size: 8, map: {partName: ["اسم1","اسم2"]}, importBase: "src/"}   (مرّر nil حين لا حاجة)
//  مهم: @import المتداخل يُحلّ نسبةً إلى مجلد التشغيل (CWD) لا مجلد الملف المستورِد؛ لذلك wc_write يكتب
//  مسارات الأجزاء كـ "src/parts/x.rin" (شغّل الاختبارات من جذر المشروع). للمجلدات الأخرى استخدم opts.importBase.
// ===========================================================================

fun wc_concat(a, b) {
    let out = [];
    for (let x in a) { push(out, x); }
    for (let x in b) { push(out, x); }
    return out;
}

fun wc_slug(s) {
    let r = lower(s);
    r = regexReplace(r, "[^a-z0-9]+", "-");
    r = regexReplace(r, "^-+|-+$", "");
    if (r == "") { return "core"; }
    return r;
}

fun wc_unit(ch) {
    if (ch["lead"] == "") { return ch["text"]; }
    return ch["lead"] + __wc_nl + ch["text"];
}

fun wc_isTypeKind(k) { return k == "class" or k == "interface" or k == "trait" or k == "enum" or k == "struct"; }

// التصريحات التي تُنفَّذ فور التحميل (ترتيبها يهم): كل شيء عدا الدوال والاستيرادات
fun wc_isEager(k) { return k != "fun" and k != "import"; }

fun wc_prefixOf(name) {
    if (name == "") { return "core"; }
    let body = name;
    if (regexTest(body, "^_+")) { body = regexReplace(body, "^_+", ""); }
    if (contains(body, "_")) {
        let first = split(body, "_")[0];
        if (len(first) >= 2) { return wc_slug(first); }
    }
    let camel = regexReplace(body, "^([a-z]{2,}?)[A-Z].*$", "$1");
    if (camel != body) { return wc_slug(camel); }
    return "core";
}

fun wc_groupKey(ch, strategy, opts) {
    if (strategy == "map") {
        let m = {};
        if (opts != nil and has(opts, "map")) { m = opts["map"]; }
        for (let k in keys(m)) { if (contains(m[k], ch["name"])) { return wc_slug(k); } }
        return "core";
    }
    if (strategy == "kind") {
        if (ch["kind"] == "let") { return "constants"; }
        if (ch["kind"] == "fun") { return "functions"; }
        if (wc_isTypeKind(ch["kind"])) { return "types"; }
        return "core";
    }
    if (strategy == "prefix") {
        if (wc_isTypeKind(ch["kind"])) { return wc_slug(ch["name"]); }
        return wc_prefixOf(ch["name"]);
    }
    fail("wesscode: unknown strategy '" + strategy + "' (kind|prefix|size|map)");
}

// يجمّع التصريحات في مجموعات بترتيب أول ظهور: [{key, items}]
fun wc_group(decls, strategy, opts) {
    let groups = [];
    let index = {};
    if (strategy == "size") {
        let n = 8;
        if (opts != nil and has(opts, "size")) { n = opts["size"]; }
        if (n < 1) { fail("wesscode: size must be >= 1"); }
        let i = 0;
        for (let ch in decls) {
            let g = floor(i / n);
            let key = "part" + (g + 1);
            if (!has(index, key)) { index[key] = len(groups); push(groups, {key: key, items: []}); }
            push(groups[index[key]]["items"], ch);
            i = i + 1;
        }
        return groups;
    }
    for (let ch in decls) {
        let key = wc_groupKey(ch, strategy, opts);
        if (!has(index, key)) { index[key] = len(groups); push(groups, {key: key, items: []}); }
        push(groups[index[key]]["items"], ch);
    }
    return groups;
}

// يتحقق أن إعادة الترتيب لم تقدّم/تؤخّر تصريحاً فوري التنفيذ على آخر
fun wc_orderWarnings(original, emitted) {
    let warn = [];
    let a = [];
    let b = [];
    for (let ch in original) { if (wc_isEager(ch["kind"])) { push(a, ch["line"]); } }
    for (let ch in emitted) { if (wc_isEager(ch["kind"])) { push(b, ch["line"]); } }
    let i = 0;
    while (i < len(a)) {
        if (a[i] != b[i]) {
            let moved = "";
            for (let ch in original) { if (ch["line"] == b[i]) { moved = ch["kind"] + " " + ch["name"]; } }
            push(warn, "تغيّر ترتيب تصريح فوري التنفيذ (" + moved + " صار قبل موضعه الأصلي) — قد يعتمد على ما بعده؛ راجع الاستراتيجية أو استخدم map");
            return warn;
        }
        i = i + 1;
    }
    return warn;
}

fun wc_splitSource(src) {
    let r = lang.split(src);
    if (!r["ok"]) { return {ok: false, error: "تعذّر تحليل المصدر: " + r["error"]}; }
    return r;
}

fun wc_partsBanner(name, key) {
    return "// " + name + " — جزء: " + key + __wc_nl + "// مولَّد بواسطة wesscode (wc_toParts)" + __wc_nl + __wc_nl;
}

// ------------------------------------------------------------------ تحويلة 1: جزء
fun wc_toParts(src, name, strategy, opts) {
    let r = wc_splitSource(src);
    if (!r["ok"]) { return r; }
    let imports = [];
    let stmts = [];
    let decls = [];
    for (let ch in r["chunks"]) {
        if (ch["kind"] == "import") { push(imports, ch); }
        else if (ch["kind"] == "stmt") { push(stmts, ch); }
        else { push(decls, ch); }
    }
    let groups = wc_group(decls, strategy, opts);
    let importBase = "";
    if (opts != nil and has(opts, "importBase")) { importBase = opts["importBase"]; }
    let parts = {};
    let order = [];
    let emitted = [];
    let used = {};
    let entry = "";
    if (r["header"] != "") { entry = r["header"] + __wc_nl + __wc_nl; }
    for (let ch in imports) { entry = entry + wc_unit(ch) + __wc_nl; }
    if (len(imports) > 0) { entry = entry + __wc_nl; }
    for (let g in groups) {
        let slug = wc_slug(g["key"]);
        let path = "parts/" + slug + ".rin";
        let n = 2;
        while (has(used, path)) { path = "parts/" + slug + "-" + n + ".rin"; n = n + 1; }
        used[path] = true;
        let units = [];
        for (let ch in g["items"]) { push(units, wc_unit(ch)); push(emitted, ch); }
        parts[path] = wc_partsBanner(name, g["key"]) + join(units, __wc_nl + __wc_nl) + __wc_nl;
        push(order, path);
        entry = entry + "@import \"" + importBase + path + "\";" + __wc_nl;
    }
    if (len(stmts) > 0) {
        entry = entry + __wc_nl;
        for (let ch in stmts) { entry = entry + wc_unit(ch) + __wc_nl; push(emitted, ch); }
    }
    if (r["tail"] != "") { entry = entry + __wc_nl + r["tail"] + __wc_nl; }
    let warnings = wc_orderWarnings(wc_concat(decls, stmts), emitted);
    if (len(stmts) > 0) { push(warnings, len(stmts) + " تعليمة على المستوى الأعلى بقيت في الملف الرئيسي بعد الأجزاء"); }
    // فحص كل ملف ناتج بالمحلّل الحقيقي
    let chk = lang.check(entry);
    if (!chk["ok"]) { return {ok: false, error: "الملف الرئيسي: سطر " + chk["line"] + ": " + chk["message"]}; }
    for (let p in order) {
        let c2 = lang.check(parts[p]);
        if (!c2["ok"]) { return {ok: false, error: p + ": سطر " + c2["line"] + ": " + c2["message"]}; }
    }
    return {ok: true, entry: entry, parts: parts, order: order, warnings: warnings,
            stats: {chunks: len(r["chunks"]), parts: len(order), imports: len(imports), statements: len(stmts)}};
}

// يعيد الأجزاء إلى ملف واحد (عكس wc_toParts). يطابق سطر الاستيراد بنهاية مسار الجزء فيصلح مع importBase.
fun wc_partBody(body) {
    let lines = split(body, __wc_nl);
    let keep = [];
    let i = 0;
    let skipping = len(lines) > 1 and contains(lines[1], "wc_toParts");
    for (let ln in lines) {
        if (skipping and i < 3) { i = i + 1; } else { push(keep, ln); i = i + 1; }
    }
    return trim(join(keep, __wc_nl));
}

fun wc_endsWith(s, suffix) {
    return len(s) >= len(suffix) and substr(s, len(s) - len(suffix), len(suffix)) == suffix;
}

fun wc_mergeParts(entry, parts) {
    let out = [];
    for (let ln in split(entry, __wc_nl)) {
        let replaced = false;
        if (regexTest(ln, "^@import \"[^\"]*\";$")) {
            let path = regexReplace(ln, "^@import \"([^\"]*)\";$", "$1");
            for (let p in keys(parts)) {
                if (!replaced and wc_endsWith(path, p)) { push(out, wc_partBody(parts[p])); replaced = true; }
            }
        }
        if (!replaced) { push(out, ln); }
    }
    return join(out, __wc_nl);
}

// ------------------------------------------------------------------ تحويلة 2: قسم
fun wc_toSections(src, name, strategy, opts) {
    let r = wc_splitSource(src);
    if (!r["ok"]) { return r; }
    let imports = [];
    let stmts = [];
    let decls = [];
    for (let ch in r["chunks"]) {
        if (ch["kind"] == "import") { push(imports, ch); }
        else if (ch["kind"] == "stmt") { push(stmts, ch); }
        else { push(decls, ch); }
    }
    let groups = wc_group(decls, strategy, opts);
    if (len(stmts) > 0) { push(groups, {key: "main", items: stmts}); }
    let emitted = [];
    let toc = "// #toc" + __wc_nl + "// فهرس الأقسام — " + name + ":" + __wc_nl;
    let i = 1;
    let sections = [];
    for (let g in groups) {
        toc = toc + "//   " + i + ". " + g["key"] + " (" + len(g["items"]) + ")" + __wc_nl;
        push(sections, {name: g["key"], count: len(g["items"])});
        i = i + 1;
    }
    toc = toc + "// #endtoc";
    let out = "";
    if (r["header"] != "") { out = r["header"] + __wc_nl + __wc_nl; }
    out = out + toc + __wc_nl + __wc_nl;
    for (let ch in imports) { out = out + wc_unit(ch) + __wc_nl; }
    if (len(imports) > 0) { out = out + __wc_nl; }
    for (let g in groups) {
        out = out + "// #region " + g["key"] + __wc_nl;
        out = out + "// ───── قسم: " + g["key"] + " ─────" + __wc_nl + __wc_nl;
        let units = [];
        for (let ch in g["items"]) { push(units, wc_unit(ch)); if (ch["kind"] != "stmt") { push(emitted, ch); } }
        out = out + join(units, __wc_nl + __wc_nl) + __wc_nl + "// #endregion" + __wc_nl + __wc_nl;
    }
    for (let ch in stmts) { push(emitted, ch); }
    if (r["tail"] != "") { out = out + r["tail"] + __wc_nl; }
    let warnings = wc_orderWarnings(wc_concat(decls, stmts), emitted);
    let chk = lang.check(out);
    if (!chk["ok"]) { return {ok: false, error: "المصدر الناتج: سطر " + chk["line"] + ": " + chk["message"]}; }
    return {ok: true, source: out, sections: sections, warnings: warnings, stats: {chunks: len(r["chunks"]), sections: len(sections)}};
}

// أقسام موجودة في مصدر: [{name, line}]
fun wc_sectionsOf(src) {
    let out = [];
    let n = 1;
    for (let ln in split(src, __wc_nl)) {
        if (regexTest(ln, "^// #region ")) { push(out, {name: replace(ln, "// #region ", ""), line: n}); }
        n = n + 1;
    }
    return out;
}

// يزيل الفهرس وعلامات الأقسام فيعود المصدر كما كان قبل wc_toSections
fun wc_fromSections(src) {
    let keep = [];
    let inToc = false;
    for (let ln in split(src, __wc_nl)) {
        if (ln == "// #toc") { inToc = true; }
        else if (inToc) { if (ln == "// #endtoc") { inToc = false; } }
        else if (regexTest(ln, "^// #region ") or ln == "// #endregion" or regexTest(ln, "^// ───── قسم: ")) { }
        else { push(keep, ln); }
    }
    return trim(regexReplace(join(keep, __wc_nl), "\n{3,}", "\n\n")) + __wc_nl;
}

// ------------------------------------------------------------------ أدوات مساندة
fun wc_convert(src, name, mode, strategy, opts) {
    if (mode == "parts") { return wc_toParts(src, name, strategy, opts); }
    if (mode == "sections") { return wc_toSections(src, name, strategy, opts); }
    if (mode == "single") {
        let r = wc_splitSource(src);
        if (!r["ok"]) { return r; }
        return {ok: true, source: src, warnings: [], stats: {chunks: len(r["chunks"])}};
    }
    fail("wc_convert: unknown mode '" + mode + "' (parts|sections|single)");
}

// هل مصدران متكافئان؟ نفس التصريحات (النوع/الاسم/النص) بغض النظر عن الترتيب والفواصل
fun wc_equivalent(a, b) {
    let ra = wc_splitSource(a);
    let rb = wc_splitSource(b);
    if (!ra["ok"] or !rb["ok"]) { return false; }
    if (len(ra["chunks"]) != len(rb["chunks"])) { return false; }
    let sa = [];
    let sb = [];
    for (let ch in ra["chunks"]) { push(sa, ch["kind"] + "|" + ch["name"] + "|" + ch["text"]); }
    for (let ch in rb["chunks"]) { push(sb, ch["kind"] + "|" + ch["name"] + "|" + ch["text"]); }
    return json.canonical(sort(sa)) == json.canonical(sort(sb));
}

// يحلّل مصدراً ويقترح التخطيط الأنسب
fun wc_suggest(src) {
    let r = wc_splitSource(src);
    if (!r["ok"]) { return r; }
    let funcs = 0;
    let types = 0;
    let consts = 0;
    let stmts = 0;
    let documented = 0;
    for (let ch in r["chunks"]) {
        if (ch["kind"] == "fun") { funcs = funcs + 1; }
        else if (wc_isTypeKind(ch["kind"])) { types = types + 1; }
        else if (ch["kind"] == "let") { consts = consts + 1; }
        else if (ch["kind"] == "stmt") { stmts = stmts + 1; }
        if (regexTest(ch["lead"], "^///")) { documented = documented + 1; }
    }
    let total = funcs + types + consts;
    let mode = "single";
    let why = "الملف صغير (" + total + " تصريحاً) — يكفي ملف واحد";
    if (total > 12) { mode = "sections"; why = total + " تصريحاً — أقسام داخل ملف واحد تسهّل التنقل"; }
    if (total > 40) { mode = "parts"; why = total + " تصريحاً — الأفضل تقسيمه إلى أجزاء منفصلة"; }
    let cover = 0;
    if (funcs + types > 0) { cover = round(100 * documented / (funcs + types)); }
    return {ok: true, functions: funcs, types: types, constants: consts, statements: stmts,
            docCoverage: cover, mode: mode, reason: why, lines: len(split(src, __wc_nl))};
}

// يقرأ ملف Rin موجوداً ويحوّله إلى مواصفة مكتبة (المحتوى يُنسخ كما هو)
fun wc_fromFile(name, version, description, path) {
    if (!fileExists(path)) { fail("wc_fromFile: file not found: " + path); }
    let src = readFile(path);
    let chk = lang.check(src);
    if (!chk["ok"]) { fail("wc_fromFile: " + path + ": سطر " + chk["line"] + ": " + chk["message"]); }
    return wc_fromSource(name, version, description, src);
}

// يختار تخطيط مخرجات المكتبة: single | sections | parts
fun wc_layout(lib, mode, strategy, opts) {
    if (mode != "single" and mode != "sections" and mode != "parts") { fail("wc_layout: mode must be single|sections|parts"); }
    lib["layout"] = {mode: mode, strategy: strategy, opts: opts};
    return lib;
}

// يحوّل ملف على القرص ويكتب الناتج: outDir/<base>.rin (+ outDir/parts/*.rin)
fun wc_convertFile(path, outDir, mode, strategy, opts) {
    if (!fileExists(path)) { return {ok: false, error: "file not found: " + path}; }
    let base = regexReplace(path, "^.*/", "");
    let name = wc_slug(replace(base, ".rin", ""));
    let r = wc_convert(readFile(path), name, mode, strategy, opts);
    if (!r["ok"]) { return r; }
    let files = [];
    if (mode == "parts") {
        writeFile(outDir + "/" + base, r["entry"]);
        push(files, outDir + "/" + base);
        for (let p in r["order"]) { writeFile(outDir + "/" + p, r["parts"][p]); push(files, outDir + "/" + p); }
    } else {
        writeFile(outDir + "/" + base, r["source"]);
        push(files, outDir + "/" + base);
    }
    r["files"] = files;
    return r;
}
)WESSCODEOGRIN";

static const char* kLib_inputkit_og_rin = R"INPUTKITOGRIN(
// ============================================================================
//  lib/inputkit.og.rin — أصناف (OOP) جاهزة للتحقق من إدخال المستخدم
//  لا دوال مبنية جديدة: كل صنف يعرّف __call__ فيُمرَّر مباشرة كـ validator لدوال الإدخال:
//      input(prompt, validator?, target?, key?)   inputNumber(prompt, validator?, target?, key?)
//  أو كقيمة في مخطّط النموذج:  input(prompt, target, {"age": Range(0, 120)})
//  استيراد:  @import "lib/inputkit.og.rin";
//
//  العقد: __call__(v) تعيد true للقبول، أو نصاً = سبب الرفض (يظهر للمستخدم).
//  الحقل  type  ("string" | "number" | "bool") يخبر النموذج أي دالة إدخال يستدعي لهذا الحقل.
// ============================================================================

interface Check { fun __call__(v); }

abstract class Rule implements Check {
    let type = "string";
    let message = "";
    abstract fun __call__(v);
    // رسالة مخصّصة تغلب الافتراضية:  Range(0,120).withMessage("العمر غير معقول")
    fun withMessage(m) { self.message = m; return self; }
    fun fail(fallback) {
        if (self.message != "") { return self.message; }
        return fallback;
    }
}

// غير فارغ (بعد trim)
class Required extends Rule {
    fun __call__(v) {
        if (trim(toString(v)) == "") { return self.fail("مطلوب"); }
        return true;
    }
}

// طول النص بالمحارف (utf8Len، فالعربية تُحسب صحيحاً لا بالبايتات)
class Length extends Rule {
    let lo = 0;
    let hi = 1000000;
    fun init(lo, hi) { self.lo = lo; self.hi = hi; }
    fun __call__(v) {
        let n = utf8Len(toString(v));
        if (n < self.lo or n > self.hi) {
            return self.fail("الطول يجب أن يكون بين " + self.lo + " و " + self.hi);
        }
        return true;
    }
}

// رقم ضمن [lo, hi]
class Range extends Rule {
    let lo = 0;
    let hi = 0;
    fun init(lo, hi) { self.type = "number"; self.lo = lo; self.hi = hi; }
    fun __call__(v) {
        if (v < self.lo or v > self.hi) { return self.fail("خارج " + self.lo + ".." + self.hi); }
        return true;
    }
}

// رقم صحيح (بلا كسر)
class Integer extends Rule {
    fun init() { self.type = "number"; }
    fun __call__(v) {
        if (floor(v) != v) { return self.fail("يجب أن يكون عدداً صحيحاً"); }
        return true;
    }
}

// واحد من قائمة (مصفوفة)
class OneOf extends Rule {
    let options = [];
    fun init(options) { self.options = options; }
    fun __call__(v) {
        if (!contains(self.options, v)) { return self.fail("اختر من: " + toString(self.options)); }
        return true;
    }
}

// يطابق تعبيراً نمطياً (regexTest)
class Matches extends Rule {
    let pattern = "";
    fun init(pattern) { self.pattern = pattern; }
    fun __call__(v) {
        if (!regexTest(toString(v), self.pattern)) { return self.fail("صيغة غير صحيحة"); }
        return true;
    }
}

class Email extends Matches {
    fun init() { self.pattern = "^[^@ ]+@[^@ ]+\\.[^@ ]+$"; }
    fun __call__(v) {
        if (!regexTest(toString(v), self.pattern)) { return self.fail("بريد إلكتروني غير صالح"); }
        return true;
    }
}

// تركيب عدة قواعد: أول رفض يُعاد؛ النوع يؤخذ من أول قاعدة
class Every extends Rule {
    let rules = [];
    fun init(rules) {
        self.rules = rules;
        if (len(rules) > 0) { self.type = oop.get(rules[0], "type", "string"); }
    }
    fun __call__(v) {
        for (let r in self.rules) {
            let res = r(v);
            if (res != true) { return res; }
        }
        return true;
    }
}
)INPUTKITOGRIN";

// ============================================================================
// Embedded IndsinWeb — generated from lib/indsinweb.og.rin
// ============================================================================
static const char* kLib_indsinweb_og_rin = R"INDSINWEBOGRIN(
// ============================================================================
// lib/indsinweb.og.rin — IndsinWeb: مكتبة WebView والروابط لمحرّك indsin
// ============================================================================
// مكتبة مكتوبة بالكامل بلغة Rin (بلا Java/Kotlin/JS داخل المكتبة نفسها). تُجهّز كل ما يحتاجه
// عنصر `WebView` في indsin (`url=` / `src=` / `html=` / `ratio=`) وكل أنواع الروابط، بأمان:
//
//   @import "lib/indsinweb.og.rin";
//
//   @container=Demo
//       warp site = "youtube.com/watch?v=dQw4w9WgXcQ";
//       @view.Column=Root
//           @view.WebView=Player
//               url=iwEmbedUrl(iwNormalize(site));   // رابط تضمين آمن
//               ratio="16:9";
//           .end/view
//           @view.WebView=Card
//               html=iwFragment(iwLinkHtml("Rin", "https://dlof-lib.github.io/rinlang/"), {"dir": "rtl"});
//           .end/view
//       .end/view
//   .end/container
//
// الأقسام (كل الدوال بادئتها iw):
//   1) نصوص وترميز      iwEscape · iwUrlEncode · iwUrlDecode
//   2) تحليل الروابط     iwParseUrl · iwNormalize · iwHost · iwOrigin · iwScheme · iwJoin · iwDisplay
//   3) الأمان            iwIsSafe · iwIsHttps · iwHostAllowed · iwSameOrigin · iwSanitizeHtml
//   4) الاستعلام         iwQuery · iwParseQuery · iwWithParams · iwGetParam · iwRemoveParam
//   5) أنواع الروابط     iwLinkKind · iwMailto · iwTel · iwSms · iwGeo · iwMapsUrl · iwWhatsApp
//                        iwTelegram · iwPlayStore · iwAndroidIntent · iwDeepLink
//   6) روابط Rin         iwRinProfile · iwRinLibrary · iwParseRinLink
//   7) الفيديو/التضمين   iwYoutubeId · iwYoutubeEmbedUrl · iwYoutubeThumb · iwVimeoEmbedUrl · iwEmbedUrl
//   8) HTML لـ html=     iwLinkHtml · iwIframeHtml · iwEmbedHtml · iwTapButton · iwFragment · iwPage
//   9) عنصر WebView      iwRatio · iwWebViewAttrs · iwWebViewSource · iwYoutubeWebView
//  10) سجل التنقّل       iwNavNew · iwNavVisit · iwNavBack · iwNavForward · iwNavCurrent
//  11) تنظيف وكشف        iwStripTracking · iwIsPrivateHost · iwIsPublicWeb · iwExtension · iwMediaKind
//  12) شريط العنوان      iwSearchUrl · iwResolveInput
//  13) مشاركة            iwShareUrl
//  14) مزوّدون إضافيون   iwSpotifyEmbedUrl · iwDailymotionEmbedUrl · iwDriveEmbedUrl · iwMapsEmbedUrl · iwIsEmbeddable
//  15) توجيه عميق        iwRoute
//  16) روابط في النصوص   iwExtractUrls · iwLinkify · iwAuditHtml
//  17) خطة الفتح         iwOpenPlan  (webview / external / blocked)
//  18) صفحات WebView     iwCardHtml · iwErrorHtml · iwLoadingHtml
//  19) المفضّلة          iwBookmarksNew · iwBookmarkAdd/Remove/Has/List · iwBookmarksSave/Load
//  20) شاشة الربط        iwLinkScreen · iwLinkScreenAttrs · iwLinkScreenSource  (فيديو/ويب/صورة/صوت/PDF/بريد/...)
//
// مبادئ الأمان: قائمة سماح للمخططات (https/http افتراضيًا؛ يُرفض javascript: و data: و file: ...)،
// رفض الرموز التحكّمية والمسافات داخل المخطط، تنبيه على الرابط الذي يحمل userinfo
// (https://google.com@evil.com)، وكل نص يدخل في HTML يُهرَّب. لا تنفيذ JavaScript من المكتبة.
// ============================================================================

@import "lib/strings.og.rin";

// ----------------------------- ثوابت ----------------------------------------

fun iwRinLinksBase() { return "https://dlof-lib.github.io/rinlang/"; }

fun iwDefaultSchemes() { return ["https", "http"]; }

// ----------------------------- 1) نصوص وترميز -------------------------------

// تهريب HTML (للنصوص والقيم داخل السمات).
fun iwEscape(s) {
    let x = replace(toString(s), "&", "&amp;");
    x = replace(x, "<", "&lt;");
    x = replace(x, ">", "&gt;");
    x = replace(x, "\"", "&quot;");
    x = replace(x, "'", "&#39;");
    return x;
}

fun iwHexDigit(n) { return charAt("0123456789ABCDEF", n); }

fun iwHexValue(c) {
    let o = ord(c);
    if (o >= 48 and o <= 57) { return o - 48; }
    if (o >= 65 and o <= 70) { return o - 55; }
    if (o >= 97 and o <= 102) { return o - 87; }
    return -1;
}

fun iwIsUnreserved(o) {
    if (o >= 48 and o <= 57) { return true; }
    if (o >= 65 and o <= 90) { return true; }
    if (o >= 97 and o <= 122) { return true; }
    return o == 45 or o == 46 or o == 95 or o == 126;
}

// ترميز نسبة مئوية على مستوى البايت (UTF-8 صحيح للعربية). keep: محارف إضافية تُترك كما هي.
fun iwUrlEncodeKeep(s, keep) {
    let src = toString(s);
    let out = "";
    let i = 0;
    while (i < len(src)) {
        let c = charAt(src, i);
        let o = ord(c);
        if (iwIsUnreserved(o) or indexOf(keep, c) >= 0) {
            out = out + c;
        } else {
            out = out + "%" + iwHexDigit(floor(o / 16)) + iwHexDigit(o % 16);
        }
        i = i + 1;
    }
    return out;
}

fun iwUrlEncode(s) { return iwUrlEncodeKeep(s, ""); }

// فكّ الترميز النسبي. plus=true يحوّل + إلى مسافة (نمط الاستعلام/النماذج).
fun iwUrlDecodeEx(s, plus) {
    let src = toString(s);
    let out = "";
    let i = 0;
    while (i < len(src)) {
        let c = charAt(src, i);
        if (c == "%" and i + 2 < len(src) + 0 and iwHexValue(charAt(src, i + 1)) >= 0 and iwHexValue(charAt(src, i + 2)) >= 0) {
            out = out + chr(iwHexValue(charAt(src, i + 1)) * 16 + iwHexValue(charAt(src, i + 2)));
            i = i + 3;
        } else {
            if (plus and c == "+") { out = out + " "; } else { out = out + c; }
            i = i + 1;
        }
    }
    return out;
}

fun iwUrlDecode(s) { return iwUrlDecodeEx(s, false); }

// ----------------------------- 2) تحليل الروابط -----------------------------

fun iwHasControl(s) {
    let i = 0;
    while (i < len(s)) {
        let o = ord(charAt(s, i));
        if (o < 33 or o == 127) { return true; }
        i = i + 1;
    }
    return false;
}

// يحلّل الرابط إلى: ok, raw, scheme, userinfo, host, port, path, query, fragment, hasAuthority.
// لا يرمي أخطاء: رابط فارغ/تالف => ok=false.
fun iwParseUrl(url) {
    let raw = trim(toString(url));
    let res = {"ok": false, "raw": raw, "scheme": "", "userinfo": "", "host": "", "port": "", "path": "", "query": "", "fragment": "", "hasAuthority": false};
    if (raw == "") { return res; }
    let rest = raw;
    let h = indexOf(rest, "#");
    if (h >= 0) { res["fragment"] = substr(rest, h + 1, len(rest) - h - 1); rest = substr(rest, 0, h); }
    let q = indexOf(rest, "?");
    if (q >= 0) { res["query"] = substr(rest, q + 1, len(rest) - q - 1); rest = substr(rest, 0, q); }
    let c = indexOf(rest, ":");
    if (c > 0 and regexTest(substr(rest, 0, c), "^[A-Za-z][A-Za-z0-9+.-]*$")) {
        res["scheme"] = lower(substr(rest, 0, c));
        rest = substr(rest, c + 1, len(rest) - c - 1);
    }
    if (startsWith(rest, "//")) {
        res["hasAuthority"] = true;
        rest = substr(rest, 2, len(rest) - 2);
        let sl = indexOf(rest, "/");
        let auth = rest;
        if (sl >= 0) { auth = substr(rest, 0, sl); res["path"] = substr(rest, sl, len(rest) - sl); } else { res["path"] = ""; }
        let at = lastIndexOf(auth, "@");
        if (at >= 0) { res["userinfo"] = substr(auth, 0, at); auth = substr(auth, at + 1, len(auth) - at - 1); }
        if (startsWith(auth, "[")) {
            let rb = indexOf(auth, "]");
            if (rb < 0) { return res; }
            res["host"] = lower(substr(auth, 0, rb + 1));
            let after = substr(auth, rb + 1, len(auth) - rb - 1);
            if (startsWith(after, ":")) { res["port"] = substr(after, 1, len(after) - 1); }
        } else {
            let pc = lastIndexOf(auth, ":");
            if (pc >= 0) {
                res["host"] = lower(substr(auth, 0, pc));
                res["port"] = substr(auth, pc + 1, len(auth) - pc - 1);
            } else {
                res["host"] = lower(auth);
            }
        }
        if (res["port"] != "" and !regexTest(res["port"], "^[0-9]{1,5}$")) { return res; }
        if (res["scheme"] != "" and res["host"] == "" and (res["scheme"] == "http" or res["scheme"] == "https")) { return res; }
    } else {
        res["path"] = rest;
    }
    res["ok"] = true;
    return res;
}

fun iwScheme(url) { return iwParseUrl(url)["scheme"]; }
fun iwHost(url) { return iwParseUrl(url)["host"]; }

fun iwDefaultPort(scheme) {
    if (scheme == "https") { return "443"; }
    if (scheme == "http") { return "80"; }
    return "";
}

// المصدر (origin) = scheme://host[:port]. فارغ لما لا مصدر له (mailto/tel...).
fun iwOrigin(url) {
    let p = iwParseUrl(url);
    if (!p["ok"] or !p["hasAuthority"] or p["scheme"] == "" or p["host"] == "") { return ""; }
    let o = p["scheme"] + "://" + p["host"];
    if (p["port"] != "" and p["port"] != iwDefaultPort(p["scheme"])) { o = o + ":" + p["port"]; }
    return o;
}

// يوحّد الرابط: يقصّ المسافات، يضيف https:// للنطاق المجرّد (example.com/x)، يصغّر المخطط والمضيف،
// يحذف المنفذ الافتراضي. يعيد "" إن لم يكن رابطًا مفهومًا.
fun iwNormalize(url) {
    let raw = trim(toString(url));
    if (raw == "") { return ""; }
    if (startsWith(raw, "//")) { raw = "https:" + raw; }
    if (regexTest(raw, "^localhost(:[0-9]{1,5})?([/?#].*)?$")) { raw = "https://" + raw; }
    let p = iwParseUrl(raw);
    if (p["scheme"] == "") {
        if (regexTest(raw, "^[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?(\\.[A-Za-z0-9]([A-Za-z0-9-]*[A-Za-z0-9])?)*\\.[A-Za-z]{2,}(:[0-9]{1,5})?([/?#].*)?$")
            or regexTest(raw, "^localhost(:[0-9]{1,5})?([/?#].*)?$")) {
            raw = "https://" + raw;
            p = iwParseUrl(raw);
        } else {
            return "";
        }
    }
    if (!p["ok"]) { return ""; }
    if (!p["hasAuthority"]) { return raw; }
    let out = p["scheme"] + "://";
    if (p["userinfo"] != "") { out = out + p["userinfo"] + "@"; }
    out = out + p["host"];
    if (p["port"] != "" and p["port"] != iwDefaultPort(p["scheme"])) { out = out + ":" + p["port"]; }
    let path = p["path"];
    if (path == "" and (p["scheme"] == "http" or p["scheme"] == "https")) { path = "/"; }
    out = out + path;
    if (p["query"] != "") { out = out + "?" + p["query"]; }
    if (p["fragment"] != "") { out = out + "#" + p["fragment"]; }
    return out;
}

// يزيل المقاطع . و .. من مسار مطلق.
fun iwRemoveDots(path) {
    let parts = split(path, "/");
    let out = [];
    let i = 0;
    while (i < len(parts)) {
        let seg = parts[i];
        if (seg == "..") {
            if (len(out) > 1) { pop(out); }
        } else {
            if (seg != ".") { push(out, seg); }
        }
        i = i + 1;
    }
    let r = join(out, "/");
    if (!startsWith(r, "/")) { r = "/" + r; }
    let last = parts[len(parts) - 1];
    if ((last == "." or last == "..") and !endsWith(r, "/")) { r = r + "/"; }
    return r;
}

// حلّ رابط نسبي على رابط أساس (RFC 3986 مبسّط).
fun iwJoin(base, rel) {
    let r = trim(toString(rel));
    if (r == "") { return toString(base); }
    let rp = iwParseUrl(r);
    if (rp["scheme"] != "") { return r; }
    let bp = iwParseUrl(base);
    if (!bp["ok"] or bp["scheme"] == "") { return ""; }
    if (startsWith(r, "//")) { return bp["scheme"] + ":" + r; }
    let origin = iwOrigin(base);
    if (origin == "") { return ""; }
    if (startsWith(r, "#")) {
        let b = origin + bp["path"];
        if (bp["query"] != "") { b = b + "?" + bp["query"]; }
        return b + r;
    }
    if (startsWith(r, "?")) { return origin + bp["path"] + r; }
    let tail = "";
    let path = r;
    let qi = indexOf(r, "?");
    let hi = indexOf(r, "#");
    let cut = -1;
    if (qi >= 0) { cut = qi; }
    if (hi >= 0 and (cut < 0 or hi < cut)) { cut = hi; }
    if (cut >= 0) { tail = substr(r, cut, len(r) - cut); path = substr(r, 0, cut); }
    if (startsWith(path, "/")) { return origin + iwRemoveDots(path) + tail; }
    let bpath = bp["path"];
    if (bpath == "") { bpath = "/"; }
    let dir = substr(bpath, 0, lastIndexOf(bpath, "/") + 1);
    return origin + iwRemoveDots(dir + path) + tail;
}

// نص مختصر للعرض: host + path بلا مخطط ولا "www." ولا "/" أخيرة.
fun iwDisplay(url) {
    let p = iwParseUrl(url);
    if (!p["ok"]) { return toString(url); }
    if (!p["hasAuthority"]) { return p["raw"]; }
    let h = p["host"];
    if (startsWith(h, "www.")) { h = substr(h, 4, len(h) - 4); }
    let path = p["path"];
    if (path == "/") { path = ""; }
    let out = h + path;
    if (p["query"] != "") { out = out + "?" + p["query"]; }
    return out;
}

fun iwShorten(s, maxLen) {
    let t = toString(s);
    if (utf8Len(t) <= maxLen) { return t; }
    return utf8Substr(t, 0, maxLen - 1) + "…";
}

// ----------------------------- 3) الأمان ------------------------------------

// هل الرابط آمن للتحميل في WebView/الفتح؟ allowed = قائمة مخططات (الافتراضي https/http).
// يرفض: فارغ، رموز تحكّم/مسافات، مخطط خارج القائمة، http(s) بلا مضيف، userinfo (تصيّد).
fun iwIsSafeEx(url, allowed, allowUserinfo) {
    let raw = trim(toString(url));
    if (raw == "" or iwHasControl(raw)) { return false; }
    let p = iwParseUrl(raw);
    if (!p["ok"] or p["scheme"] == "") { return false; }
    let okScheme = false;
    let i = 0;
    while (i < len(allowed)) {
        if (lower(allowed[i]) == p["scheme"]) { okScheme = true; }
        i = i + 1;
    }
    if (!okScheme) { return false; }
    if ((p["scheme"] == "http" or p["scheme"] == "https") and p["host"] == "") { return false; }
    if (p["userinfo"] != "" and !allowUserinfo) { return false; }
    return true;
}

fun iwIsSafe(url) { return iwIsSafeEx(url, iwDefaultSchemes(), false); }
fun iwIsHttps(url) { return iwIsSafeEx(url, ["https"], false); }

// هل مضيف الرابط ضمن قائمة نطاقات؟ العنصر "example.com" يطابق example.com ومنه .example.com،
// و"*.example.com" يطابق النطاقات الفرعية فقط.
fun iwHostAllowed(url, hosts) {
    let h = iwHost(url);
    if (h == "") { return false; }
    let i = 0;
    while (i < len(hosts)) {
        let e = lower(trim(hosts[i]));
        if (startsWith(e, "*.")) {
            if (endsWith(h, substr(e, 1, len(e) - 1))) { return true; }
        } else {
            if (h == e or endsWith(h, "." + e)) { return true; }
        }
        i = i + 1;
    }
    return false;
}

fun iwSameOrigin(a, b) {
    let oa = iwOrigin(a);
    return oa != "" and oa == iwOrigin(b);
}

// تنظيف HTML (دفاع إضافي، ليس بديلًا عن التهريب): يحذف script/iframe/object/embed/base/meta/link،
// ومعالجات on*=، ومراجع javascript:/vbscript:/data:text/html. الأفضل دائمًا iwEscape للنص المجهول.
fun iwSanitizeHtml(html) {
    let x = toString(html);
    x = regexReplace(x, "i:<!--[\\s\\S]*?-->", "");
    x = regexReplace(x, "i:<(script|iframe|object|embed|applet|style)\\b[\\s\\S]*?</\\1\\s*>", "");
    x = regexReplace(x, "i:<(script|iframe|object|embed|applet|base|meta|link|style)\\b[^>]*>", "");
    x = regexReplace(x, "i:\\s+on[a-z]+\\s*=\\s*\"[^\"]*\"", "");
    x = regexReplace(x, "i:\\s+on[a-z]+\\s*=\\s*'[^']*'", "");
    x = regexReplace(x, "i:\\s+on[a-z]+\\s*=\\s*[^\\s>]+", "");
    x = regexReplace(x, "i:(href|src|action|formaction|xlink:href|srcdoc)\\s*=\\s*(\"|')?\\s*(javascript|vbscript|data:text/html)[^\"'>\\s]*(\"|')?", "$1=\"#\"");
    return x;
}

// ----------------------------- 4) الاستعلام ---------------------------------

// map -> "a=1&b=x%20y" (بترتيب الإدراج). القيم الفارغة/nil تُحذف.
fun iwQuery(params) {
    let parts = [];
    let ks = keys(params);
    let i = 0;
    while (i < len(ks)) {
        let v = params[ks[i]];
        if (!isNil(v) and toString(v) != "") {
            push(parts, iwUrlEncode(ks[i]) + "=" + iwUrlEncode(toString(v)));
        }
        i = i + 1;
    }
    return join(parts, "&");
}

// "a=1&b=x%20y" -> {"a": "1", "b": "x y"} (آخر قيمة تغلب؛ يتجاهل ? في البداية).
fun iwParseQuery(q) {
    let s = toString(q);
    if (startsWith(s, "?")) { s = substr(s, 1, len(s) - 1); }
    let out = {};
    if (s == "") { return out; }
    let parts = split(s, "&");
    let i = 0;
    while (i < len(parts)) {
        if (parts[i] != "") {
            let e = indexOf(parts[i], "=");
            if (e >= 0) {
                out[iwUrlDecodeEx(substr(parts[i], 0, e), true)] = iwUrlDecodeEx(substr(parts[i], e + 1, len(parts[i]) - e - 1), true);
            } else {
                out[iwUrlDecodeEx(parts[i], true)] = "";
            }
        }
        i = i + 1;
    }
    return out;
}

fun iwRebuild(p, query) {
    let out = "";
    if (p["scheme"] != "") { out = p["scheme"] + ":"; }
    if (p["hasAuthority"]) {
        out = out + "//";
        if (p["userinfo"] != "") { out = out + p["userinfo"] + "@"; }
        out = out + p["host"];
        if (p["port"] != "") { out = out + ":" + p["port"]; }
    }
    out = out + p["path"];
    if (query != "") { out = out + "?" + query; }
    if (p["fragment"] != "") { out = out + "#" + p["fragment"]; }
    return out;
}

// يضيف/يستبدل معاملات الاستعلام في رابط موجود ويحفظ الـ fragment.
fun iwWithParams(url, params) {
    let p = iwParseUrl(url);
    if (!p["ok"]) { return toString(url); }
    let cur = iwParseQuery(p["query"]);
    let ks = keys(params);
    let i = 0;
    while (i < len(ks)) { cur[ks[i]] = params[ks[i]]; i = i + 1; }
    return iwRebuild(p, iwQuery(cur));
}

fun iwGetParam(url, name, fallback) {
    let q = iwParseQuery(iwParseUrl(url)["query"]);
    if (has(q, name)) { return q[name]; }
    return fallback;
}

fun iwRemoveParam(url, name) {
    let p = iwParseUrl(url);
    if (!p["ok"]) { return toString(url); }
    let q = iwParseQuery(p["query"]);
    let out = {};
    let ks = keys(q);
    let i = 0;
    while (i < len(ks)) { if (ks[i] != name) { out[ks[i]] = q[ks[i]]; } i = i + 1; }
    return iwRebuild(p, iwQuery(out));
}

// ----------------------------- 5) أنواع الروابط -----------------------------

// يصنّف الرابط: web · mail · tel · sms · geo · whatsapp · telegram · rin · market · intent ·
// file · script (javascript:/vbscript:) · data · anchor (#...) · relative · other · none
fun iwLinkKind(url) {
    let raw = trim(toString(url));
    if (raw == "") { return "none"; }
    if (startsWith(raw, "#")) { return "anchor"; }
    let p = iwParseUrl(raw);
    let s = p["scheme"];
    if (s == "") { return "relative"; }
    if (s == "http" or s == "https") {
        let h = p["host"];
        if (h == "wa.me" or h == "api.whatsapp.com" or h == "chat.whatsapp.com") { return "whatsapp"; }
        if (h == "t.me" or h == "telegram.me") { return "telegram"; }
        if (h == "dlof-lib.github.io" and startsWith(p["path"], "/rinlang/")) { return "rin"; }
        return "web";
    }
    if (s == "mailto") { return "mail"; }
    if (s == "tel") { return "tel"; }
    if (s == "sms" or s == "smsto" or s == "mms") { return "sms"; }
    if (s == "geo") { return "geo"; }
    if (s == "whatsapp") { return "whatsapp"; }
    if (s == "tg") { return "telegram"; }
    if (s == "market") { return "market"; }
    if (s == "intent") { return "intent"; }
    if (s == "file" or s == "content") { return "file"; }
    if (s == "javascript" or s == "vbscript") { return "script"; }
    if (s == "data" or s == "blob") { return "data"; }
    return "other";
}

fun iwDigits(s) { return regexReplace(toString(s), "[^0-9]", ""); }

// mailto:addr?subject=..&body=..  — opts: subject, body, cc, bcc. يعيد "" إن لم يكن البريد صالحًا.
fun iwMailto(addr0, opts) {
    let addr = trim(toString(addr0));
    if (!regexTest(addr, "^[^@\\s,;<>]+@[^@\\s,;<>]+\\.[A-Za-z]{2,}$")) { return ""; }
    let q = {};
    if (!isNil(opts)) {
        if (has(opts, "cc")) { q["cc"] = opts["cc"]; }
        if (has(opts, "bcc")) { q["bcc"] = opts["bcc"]; }
        if (has(opts, "subject")) { q["subject"] = opts["subject"]; }
        if (has(opts, "body")) { q["body"] = opts["body"]; }
    }
    let qs = iwQuery(q);
    let out = "mailto:" + iwUrlEncodeKeep(addr, "@");
    if (qs != "") { out = out + "?" + qs; }
    return out;
}

// tel:+9611234567 — يحتفظ بـ + في البداية فقط ويحذف المسافات والشرطات. 3..15 رقمًا وإلا "".
fun iwTel(number) {
    let raw = trim(toString(number));
    let d = iwDigits(raw);
    if (len(d) < 3 or len(d) > 15) { return ""; }
    if (startsWith(raw, "+")) { return "tel:+" + d; }
    return "tel:" + d;
}

fun iwSms(number, body) {
    let t = iwTel(number);
    if (t == "") { return ""; }
    let out = "sms:" + substr(t, 4, len(t) - 4);
    if (!isNil(body) and toString(body) != "") { out = out + "?body=" + iwUrlEncode(toString(body)); }
    return out;
}

fun iwValidLatLon(lat, lon) {
    return isNumber(lat) and isNumber(lon) and lat >= -90 and lat <= 90 and lon >= -180 and lon <= 180;
}

// geo:lat,lon?q=lat,lon(label)
fun iwGeo(lat, lon, label) {
    if (!iwValidLatLon(lat, lon)) { return ""; }
    let out = "geo:" + toString(lat) + "," + toString(lon);
    if (!isNil(label) and toString(label) != "") {
        out = out + "?q=" + toString(lat) + "," + toString(lon) + "(" + iwUrlEncode(toString(label)) + ")";
    }
    return out;
}

// رابط خرائط ويب: نص بحث أو إحداثيات.
fun iwMapsUrl(query) {
    if (isArray(query) and len(query) == 2 and iwValidLatLon(query[0], query[1])) {
        return "https://www.google.com/maps/search/?api=1&query=" + toString(query[0]) + "%2C" + toString(query[1]);
    }
    return "https://www.google.com/maps/search/?api=1&query=" + iwUrlEncode(toString(query));
}

// https://wa.me/<digits>?text=...
fun iwWhatsApp(phone, msg) {
    let d = iwDigits(phone);
    if (len(d) < 6 or len(d) > 15) { return ""; }
    let out = "https://wa.me/" + d;
    if (!isNil(msg) and toString(msg) != "") { out = out + "?text=" + iwUrlEncode(toString(msg)); }
    return out;
}

fun iwTelegram(username) {
    let u = toString(username);
    if (startsWith(u, "@")) { u = substr(u, 1, len(u) - 1); }
    if (!regexTest(u, "^[A-Za-z][A-Za-z0-9_]{4,31}$")) { return ""; }
    return "https://t.me/" + u;
}

fun iwValidPackage(pkg) { return regexTest(toString(pkg), "^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z][A-Za-z0-9_]*)+$"); }

// market://details?id=pkg  (يفتح متجر Play) و iwPlayStoreWeb لنسخة الويب.
fun iwPlayStore(pkg) {
    if (!iwValidPackage(pkg)) { return ""; }
    return "market://details?id=" + toString(pkg);
}

fun iwPlayStoreWeb(pkg) {
    if (!iwValidPackage(pkg)) { return ""; }
    return "https://play.google.com/store/apps/details?id=" + toString(pkg);
}

// intent://host/path#Intent;scheme=https;package=pkg;S.browser_fallback_url=...;end
fun iwAndroidIntent(host, path, scheme, pkg, fallbackUrl) {
    if (!iwValidPackage(pkg) or !regexTest(toString(scheme), "^[A-Za-z][A-Za-z0-9+.-]*$")) { return ""; }
    let out = "intent://" + toString(host) + toString(path) + "#Intent;scheme=" + toString(scheme) + ";package=" + toString(pkg) + ";";
    if (!isNil(fallbackUrl) and iwIsSafe(fallbackUrl)) { out = out + "S.browser_fallback_url=" + iwUrlEncode(toString(fallbackUrl)) + ";"; }
    return out + "end";
}

// رابط عميق مخصّص: myapp://host/path?x=1
fun iwDeepLink(scheme, host, path, params) {
    if (!regexTest(toString(scheme), "^[A-Za-z][A-Za-z0-9+.-]*$")) { return ""; }
    let p = toString(path);
    if (p != "" and !startsWith(p, "/")) { p = "/" + p; }
    let out = toString(scheme) + "://" + toString(host) + iwUrlEncodeKeep(p, "/");
    if (!isNil(params)) {
        let qs = iwQuery(params);
        if (qs != "") { out = out + "?" + qs; }
    }
    return out;
}

// ----------------------------- 6) روابط Rin ---------------------------------

fun iwSlug(s) {
    let x = trim(toString(s));
    x = regexReplace(x, "\\s+", "-");
    return x;
}

// https://dlof-lib.github.io/rinlang/@username
fun iwRinProfile(username) {
    let u = iwSlug(username);
    if (startsWith(u, "@")) { u = substr(u, 1, len(u) - 1); }
    if (u == "" or !regexTest(u, "^[A-Za-z0-9_.-]+$")) { return ""; }
    return iwRinLinksBase() + "@" + u;
}

// https://dlof-lib.github.io/rinlang/@username/library.og.rin  (تُضاف .og.rin مرة واحدة، والمسافات -)
fun iwRinLibrary(username, library) {
    let prof = iwRinProfile(username);
    let l = iwSlug(library);
    if (prof == "" or l == "" or !regexTest(l, "^[A-Za-z0-9_.-]+$")) { return ""; }
    if (endsWith(lower(l), ".og.rin")) { l = substr(l, 0, len(l) - 7); }
    return prof + "/" + l + ".og.rin";
}

// يفكّ رابط Rin (القانوني أو القديم ?@user/lib.og.rin) إلى {ok, user, library}.
fun iwParseRinLink(url) {
    let raw = trim(toString(url));
    let res = {"ok": false, "user": "", "library": ""};
    let base = iwRinLinksBase();
    let rest = "";
    if (startsWith(raw, base)) {
        rest = substr(raw, len(base), len(raw) - len(base));
    } else {
        let qi = indexOf(raw, "?@");
        if (qi >= 0 and startsWith(raw, "https://dlof-lib.github.io/")) { rest = substr(raw, qi + 1, len(raw) - qi - 1); }
        else { return res; }
    }
    if (startsWith(rest, "?@")) { rest = substr(rest, 1, len(rest) - 1); }
    let h = indexOf(rest, "#");
    if (h >= 0) { rest = substr(rest, 0, h); }
    let q = indexOf(rest, "?");
    if (q >= 0) { rest = substr(rest, 0, q); }
    if (!startsWith(rest, "@")) { return res; }
    let parts = split(substr(rest, 1, len(rest) - 1), "/");
    if (len(parts) == 0 or parts[0] == "") { return res; }
    res["user"] = iwUrlDecode(parts[0]);
    if (len(parts) >= 2 and parts[1] != "") {
        let l = iwUrlDecode(parts[1]);
        if (endsWith(lower(l), ".og.rin")) { l = substr(l, 0, len(l) - 7); }
        res["library"] = l;
    }
    res["ok"] = true;
    return res;
}

// ----------------------------- 7) الفيديو/التضمين ---------------------------

// معرّف يوتيوب (11 محرفًا) من watch?v= / youtu.be / embed / shorts / live. يعيد "" إن لم يوجد.
fun iwYoutubeId(url) {
    let p = iwParseUrl(url);
    if (!p["ok"]) { return ""; }
    let h = p["host"];
    if (startsWith(h, "www.")) { h = substr(h, 4, len(h) - 4); }
    if (startsWith(h, "m.")) { h = substr(h, 2, len(h) - 2); }
    let id = "";
    if (h == "youtu.be") {
        id = substr(p["path"], 1, len(p["path"]) - 1);
    } else if (h == "youtube.com" or h == "youtube-nocookie.com" or h == "music.youtube.com") {
        let pp = p["path"];
        if (pp == "/watch") {
            id = iwGetParam(url, "v", "");
        } else {
            let segs = split(pp, "/");
            if (len(segs) >= 3 and (segs[1] == "embed" or segs[1] == "shorts" or segs[1] == "live" or segs[1] == "v")) { id = segs[2]; }
        }
    }
    if (regexTest(id, "^[A-Za-z0-9_-]{11}$")) { return id; }
    return "";
}

fun iwIsYoutube(url) { return iwYoutubeId(url) != ""; }

// رابط تضمين youtube-nocookie. opts: start, autoplay, mute, loop, controls(false لإخفائها).
fun iwYoutubeEmbedUrl(url, opts) {
    let id = iwYoutubeId(url);
    if (id == "") { return ""; }
    let q = {"rel": "0", "playsinline": "1"};
    if (isNil(opts)) { opts = {}; }
    if (has(opts, "start") and isNumber(opts["start"]) and opts["start"] > 0) { q["start"] = toString(floor(opts["start"])); }
    if (has(opts, "autoplay") and opts["autoplay"] == true) { q["autoplay"] = "1"; }
    if (has(opts, "mute") and opts["mute"] == true) { q["mute"] = "1"; }
    if (has(opts, "loop") and opts["loop"] == true) { q["loop"] = "1"; q["playlist"] = id; }
    if (has(opts, "controls") and opts["controls"] == false) { q["controls"] = "0"; }
    return "https://www.youtube-nocookie.com/embed/" + id + "?" + iwQuery(q);
}

fun iwYoutubeThumb(url) {
    let id = iwYoutubeId(url);
    if (id == "") { return ""; }
    return "https://i.ytimg.com/vi/" + id + "/hqdefault.jpg";
}

fun iwVimeoId(url) {
    let p = iwParseUrl(url);
    if (!p["ok"]) { return ""; }
    let h = p["host"];
    if (startsWith(h, "www.")) { h = substr(h, 4, len(h) - 4); }
    if (h != "vimeo.com" and h != "player.vimeo.com") { return ""; }
    let segs = split(p["path"], "/");
    let i = len(segs) - 1;
    while (i >= 0) {
        if (regexTest(segs[i], "^[0-9]{5,12}$")) { return segs[i]; }
        i = i - 1;
    }
    return "";
}

fun iwVimeoEmbedUrl(url) {
    let id = iwVimeoId(url);
    if (id == "") { return ""; }
    return "https://player.vimeo.com/video/" + id;
}

// رابط تضمين مناسب لمزوّد معروف (يوتيوب/Vimeo)، أو الرابط نفسه إن كان http(s) آمنًا، وإلا "".
fun iwEmbedUrl(url) {
    let y = iwYoutubeEmbedUrl(url, {});
    if (y != "") { return y; }
    let v = iwVimeoEmbedUrl(url);
    if (v != "") { return v; }
    let sp = iwSpotifyEmbedUrl(url);
    if (sp != "") { return sp; }
    let dm = iwDailymotionEmbedUrl(url);
    if (dm != "") { return dm; }
    let gd = iwDriveEmbedUrl(url);
    if (gd != "") { return gd; }
    if (iwIsSafe(url)) { return trim(toString(url)); }
    return "";
}

// ----------------------------- 8) HTML لخاصية html= ------------------------

// <a> آمن: الرابط غير الآمن يتحوّل إلى <span> بلا href. opts: target ("_blank" افتراضيًا), class, title.
fun iwLinkHtml(label, url) { return iwLinkHtmlEx(label, url, {}); }

fun iwLinkHtmlEx(label, url, opts) {
    if (isNil(opts)) { opts = {}; }
    let lbl = iwEscape(label);
    let u = trim(toString(url));
    let safeUrl = iwIsSafe(u);
    let kind = iwLinkKind(u);
    if (kind == "mail" or kind == "tel" or kind == "sms" or kind == "geo") { safeUrl = !iwHasControl(u); }
    if (!safeUrl) { return "<span class=\"iw-link iw-link-blocked\">" + lbl + "</span>"; }
    let out = "<a class=\"iw-link";
    if (has(opts, "class")) { out = out + " " + iwEscape(opts["class"]); }
    out = out + "\" href=\"" + iwEscape(u) + "\"";
    if (kind == "web" or kind == "rin" or kind == "whatsapp" or kind == "telegram") {
        let target = "_blank";
        if (has(opts, "target")) { target = opts["target"]; }
        out = out + " target=\"" + iwEscape(target) + "\" rel=\"noopener noreferrer\"";
    }
    if (has(opts, "title")) { out = out + " title=\"" + iwEscape(opts["title"]) + "\""; }
    return out + ">" + lbl + "</a>";
}

// "16:9" | "16/9" | "1.5" | 1.5 -> رقم (العرض/الارتفاع)، أو 0 إن لم يُفهم.
fun iwRatio(r) {
    if (isNumber(r)) { if (r > 0) { return r; } return 0; }
    let s = trim(toString(r));
    let sep = indexOf(s, ":");
    if (sep < 0) { sep = indexOf(s, "/"); }
    if (sep > 0) {
        let a = substr(s, 0, sep);
        let b = substr(s, sep + 1, len(s) - sep - 1);
        if (regexTest(a, "^[0-9]+(\\.[0-9]+)?$") and regexTest(b, "^[0-9]+(\\.[0-9]+)?$") and toNumber(b) > 0) { return toNumber(a) / toNumber(b); }
        return 0;
    }
    if (regexTest(s, "^[0-9]+(\\.[0-9]+)?$") and toNumber(s) > 0) { return toNumber(s); }
    return 0;
}

fun iwHeightForRatio(width, ratio) {
    let r = iwRatio(ratio);
    if (r <= 0) { return 0; }
    return round(width / r);
}

// <iframe> متجاوب بنسبة ثابتة. يرفض الرابط غير الآمن (يعيد "").
// opts: ratio ("16:9")، title، fullscreen (true)، lazy (true).
fun iwIframeHtml(src, opts) {
    if (isNil(opts)) { opts = {}; }
    if (!iwIsSafe(src)) { return ""; }
    let ratio = 16 / 9;
    if (has(opts, "ratio") and iwRatio(opts["ratio"]) > 0) { ratio = iwRatio(opts["ratio"]); }
    let pad = round(10000 / ratio) / 100;
    let title = "Embedded content";
    if (has(opts, "title")) { title = opts["title"]; }
    let allow = "accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture";
    let out = "<div style=\"position:relative;width:100%;padding-top:" + toString(pad) + "%;overflow:hidden\">";
    out = out + "<iframe src=\"" + iwEscape(trim(toString(src))) + "\" title=\"" + iwEscape(title) + "\"";
    out = out + " style=\"position:absolute;top:0;left:0;width:100%;height:100%;border:0\"";
    out = out + " loading=\"lazy\" referrerpolicy=\"strict-origin-when-cross-origin\" allow=\"" + allow + "\"";
    if (!(has(opts, "fullscreen") and opts["fullscreen"] == false)) { out = out + " allowfullscreen"; }
    return out + "></iframe></div>";
}

// تضمين تلقائي لرابط فيديو معروف أو رابط ويب آمن؛ "" إن لم يصلح.
fun iwEmbedHtml(url, opts) {
    let e = iwEmbedUrl(url);
    if (e == "") { return ""; }
    return iwIframeHtml(e, opts);
}

// زر داخل HTML يمرّر نقرة إلى حدث indsin الأصلي عبر جسر RinPreview.tap() (إن وُجد الجسر).
fun iwTapButton(label, opts) {
    if (isNil(opts)) { opts = {}; }
    let sty = "padding:10px 18px;border:0;border-radius:10px;font:inherit;cursor:pointer;background:#6C5CE7;color:#fff";
    if (has(opts, "style")) { sty = opts["style"]; }
    return "<button type=\"button\" style=\"" + iwEscape(sty) + "\" onclick=\"if(window.RinPreview){window.RinPreview.tap();}\">" + iwEscape(label) + "</button>";
}

// يلفّ مقطع HTML في حاوية جاهزة لـ html=. opts: dir ("rtl"/"ltr")، lang، color، background، font، padding.
fun iwFragment(bodyHtml, opts) {
    if (isNil(opts)) { opts = {}; }
    let dir = "auto";
    if (has(opts, "dir")) { dir = opts["dir"]; }
    let sty = "box-sizing:border-box;width:100%;height:100%;font-family:" + "system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;";
    if (has(opts, "font")) { sty = "box-sizing:border-box;width:100%;height:100%;font-family:" + opts["font"] + ";"; }
    if (has(opts, "theme") and opts["theme"] == "dark") { sty = sty + "color:#E6E6F0;background:#181920;"; }
    if (has(opts, "theme") and opts["theme"] == "light") { sty = sty + "color:#202124;background:#ffffff;"; }
    if (has(opts, "color")) { sty = sty + "color:" + opts["color"] + ";"; }
    if (has(opts, "background")) { sty = sty + "background:" + opts["background"] + ";"; }
    if (has(opts, "padding")) { sty = sty + "padding:" + toString(opts["padding"]) + "px;"; }
    let lang = "";
    if (has(opts, "lang")) { lang = " lang=\"" + iwEscape(opts["lang"]) + "\""; }
    return "<div dir=\"" + iwEscape(dir) + "\"" + lang + " style=\"" + iwEscape(sty) + "\">" + bodyHtml + "</div>";
}

// صفحة HTML كاملة (للحفظ بـ writeFile أو HtmlRunActivity). opts كما في iwFragment + title، css.
fun iwPage(bodyHtml, opts) {
    if (isNil(opts)) { opts = {}; }
    let title = "";
    if (has(opts, "title")) { title = opts["title"]; }
    let dir = "auto";
    if (has(opts, "dir")) { dir = opts["dir"]; }
    let css = "html,body{margin:0;padding:0}body{font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;line-height:1.6;padding:16px}a.iw-link{color:#6C5CE7}.iw-link-blocked{opacity:.6;text-decoration:line-through}";
    if (has(opts, "css")) { css = css + opts["css"]; }
    let out = "<!doctype html><html dir=\"" + iwEscape(dir) + "\"><head><meta charset=\"utf-8\">";
    out = out + "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    out = out + "<meta http-equiv=\"Content-Security-Policy\" content=\"script-src 'none'; object-src 'none'; base-uri 'none'\">";
    out = out + "<title>" + iwEscape(title) + "</title><style>" + css + "</style></head><body>" + bodyHtml + "</body></html>";
    return out;
}

// ----------------------------- 9) عنصر WebView ------------------------------

fun iwBlockedHtml(reason) {
    return iwFragment("<p style=\"margin:0;padding:12px\">" + iwEscape(reason) + "</p>", {"dir": "auto"});
}

fun iwLooksLikeHtml(s) { return regexTest(toString(s), "^\\s*<[A-Za-z!/]"); }

// يجهّز سمات @view.WebView من مصدر واحد: رابط (يُطبَّع ويُفحص) أو HTML (يُنظَّف).
// opts: ratio، height، width، allow (قائمة نطاقات)، schemes (قائمة مخططات)، raw (true: لا تنظيف HTML)،
//       embed (true: حوّل روابط الفيديو المعروفة إلى روابط تضمين).
// الناتج: {"ok": bool, "mode": "url"|"html"|"blocked", "attrs": {...}, "error": "..."}
// حتى عند الفشل تكون attrs صالحة (html يشرح السبب) فلا ينهار العنصر.
fun iwWebViewAttrs(src, opts) {
    if (isNil(opts)) { opts = {}; }
    let attrs = {};
    let res = {"ok": true, "mode": "html", "attrs": attrs, "error": ""};
    let s = trim(toString(src));
    if (s == "") {
        res["ok"] = false; res["mode"] = "blocked"; res["error"] = "empty source";
        attrs["html"] = iwBlockedHtml("No content");
    } else if (iwLooksLikeHtml(s)) {
        if (has(opts, "raw") and opts["raw"] == true) { attrs["html"] = s; } else { attrs["html"] = iwSanitizeHtml(s); }
    } else {
        let u = iwNormalize(s);
        let schemes = iwDefaultSchemes();
        if (has(opts, "schemes")) { schemes = opts["schemes"]; }
        if (has(opts, "stripTracking") and opts["stripTracking"] == true) { u = iwStripTracking(u); }
        if (u == "" or !iwIsSafeEx(u, schemes, false)) {
            res["ok"] = false; res["mode"] = "blocked"; res["error"] = "unsafe or invalid url";
            attrs["html"] = iwBlockedHtml("Blocked: unsafe or invalid link");
        } else if (has(opts, "blockPrivate") and opts["blockPrivate"] == true and iwIsPrivateHost(iwHost(u))) {
            res["ok"] = false; res["mode"] = "blocked"; res["error"] = "private or local address";
            attrs["html"] = iwBlockedHtml("Blocked: local or private address");
        } else if (has(opts, "allow") and !iwHostAllowed(u, opts["allow"])) {
            res["ok"] = false; res["mode"] = "blocked"; res["error"] = "host !allowed";
            attrs["html"] = iwBlockedHtml("Blocked: this site is !allowed here");
        } else {
            res["mode"] = "url";
            if (has(opts, "embed") and opts["embed"] == true) {
                let e = iwEmbedUrl(u);
                if (e != "") { u = e; }
            }
            attrs["url"] = u;
        }
    }
    if (has(opts, "ratio") and iwRatio(opts["ratio"]) > 0) { attrs["ratio"] = toString(opts["ratio"]); }
    if (has(opts, "height")) { attrs["height"] = opts["height"]; }
    if (has(opts, "width")) { attrs["width"] = opts["width"]; }
    return res;
}

// نص Rin صحيح لقيمة سلسلة (يهرّب \ و " والأسطر).
fun iwRinString(s) {
    let x = replace(toString(s), "\\", "\\\\");
    x = replace(x, "\"", "\\\"");
    x = replace(x, "\r", "");
    x = replace(x, "\n", "\\n");
    x = replace(x, "\t", "\\t");
    return "\"" + x + "\"";
}

fun iwIsIdent(s) { return regexTest(toString(s), "^[A-Za-z_][A-Za-z0-9_]*$"); }

// يولّد كود Rin جاهزًا لعنصر WebView (يُلصق في ملفك أو يُكتب بـ writeFile).
// opts كما في iwWebViewAttrs + dialect: "view" (الافتراضي) أو "element".
fun iwWebViewSource(name, src, opts) {
    if (isNil(opts)) { opts = {}; }
    if (!iwIsIdent(name)) { return ""; }
    let r = iwWebViewAttrs(src, opts);
    let dialect = "view";
    if (has(opts, "dialect") and opts["dialect"] == "element") { dialect = "element"; }
    let out = "@" + dialect + ".WebView=" + name + "\n";
    let order = ["url", "html", "ratio", "width", "height"];
    let i = 0;
    while (i < len(order)) {
        let k = order[i];
        if (has(r["attrs"], k)) {
            let v = r["attrs"][k];
            if (isNumber(v)) { out = out + "    " + k + "=" + toString(v) + ";\n"; }
            else { out = out + "    " + k + "=" + iwRinString(v) + ";\n"; }
        }
        i = i + 1;
    }
    return out + ".end/" + dialect + "\n";
}

// اختصار: WebView لفيديو يوتيوب (تضمين nocookie، نسبة 16:9).
fun iwYoutubeWebView(name, url, opts) {
    if (isNil(opts)) { opts = {}; }
    let e = iwYoutubeEmbedUrl(url, opts);
    if (e == "") { return ""; }
    let o = {"ratio": "16:9", "dialect": "view"};
    if (has(opts, "dialect")) { o["dialect"] = opts["dialect"]; }
    if (has(opts, "height")) { o["height"] = opts["height"]; }
    return iwWebViewSource(name, e, o);
}

// ----------------------------- 10) سجل التنقّل ------------------------------
// سجل تنقّل صغير (أمام/خلف) لواجهة متصفّح فوق WebView: خريطة قابلة للتعديل المباشر.

fun iwNavNew() { return {"stack": [], "index": -1}; }

fun iwNavCurrent(nav) {
    if (nav["index"] < 0) { return ""; }
    return nav["stack"][nav["index"]];
}

fun iwCanBack(nav) { return nav["index"] > 0; }
fun iwCanForward(nav) { return nav["index"] < len(nav["stack"]) - 1; }

// زيارة رابط جديد: يقصّ تاريخ "الأمام" ويتجاهل الرابط المكرّر المتتالي والروابط غير الآمنة.
fun iwNavVisit(nav, url) {
    let u = iwNormalize(url);
    if (u == "" or !iwIsSafe(u)) { return false; }
    if (iwNavCurrent(nav) == u) { return true; }
    let keepN = nav["index"] + 1;
    let ns = [];
    let i = 0;
    while (i < keepN) { push(ns, nav["stack"][i]); i = i + 1; }
    push(ns, u);
    nav["stack"] = ns;
    nav["index"] = len(ns) - 1;
    return true;
}

fun iwNavBack(nav) {
    if (iwCanBack(nav)) { nav["index"] = nav["index"] - 1; }
    return iwNavCurrent(nav);
}

fun iwNavForward(nav) {
    if (iwCanForward(nav)) { nav["index"] = nav["index"] + 1; }
    return iwNavCurrent(nav);
}


// ----------------------------- 11) تنظيف وكشف ------------------------------

fun iwTrackingParams() { return ["fbclid", "gclid", "dclid", "msclkid", "mc_cid", "mc_eid", "igshid", "yclid", "_ga", "ref_src", "si"]; }

// يحذف معاملات التتبّع (utm_* وfbclid وgclid ...) ويحفظ بقية الرابط.
fun iwStripTracking(url) {
    let p = iwParseUrl(url);
    if (!p["ok"] or p["query"] == "") { return toString(url); }
    let q = iwParseQuery(p["query"]);
    let bad = iwTrackingParams();
    let out = {};
    let ks = keys(q);
    let i = 0;
    while (i < len(ks)) {
        let k = ks[i];
        let drop = startsWith(lower(k), "utm_");
        let j = 0;
        while (j < len(bad)) { if (lower(k) == bad[j]) { drop = true; } j = j + 1; }
        if (!drop) { out[k] = q[k]; }
        i = i + 1;
    }
    return iwRebuild(p, iwQuery(out));
}

fun iwIsIpv4(h) {
    if (!regexTest(h, "^[0-9]{1,3}(\\.[0-9]{1,3}){3}$")) { return false; }
    let parts = split(h, ".");
    let i = 0;
    while (i < 4) { if (toNumber(parts[i]) > 255) { return false; } i = i + 1; }
    return true;
}

fun iwIsIp(host) { return iwIsIpv4(host) or startsWith(host, "["); }

// عناوين محلية/خاصة: localhost و*.local و*.internal و127/10/192.168/172.16-31/169.254/0 و IPv6 المحلية.
fun iwIsPrivateHost(host) {
    let h = lower(toString(host));
    if (h == "") { return true; }
    if (h == "localhost" or endsWith(h, ".localhost") or endsWith(h, ".local") or endsWith(h, ".internal") or endsWith(h, ".lan")) { return true; }
    if (iwIsIpv4(h)) {
        let a = toNumber(split(h, ".")[0]);
        let b = toNumber(split(h, ".")[1]);
        if (a == 10 or a == 127 or a == 0) { return true; }
        if (a == 192 and b == 168) { return true; }
        if (a == 172 and b >= 16 and b <= 31) { return true; }
        if (a == 169 and b == 254) { return true; }
        if (a == 100 and b >= 64 and b <= 127) { return true; }
        return false;
    }
    if (startsWith(h, "[")) {
        return h == "[::1]" or h == "[::]" or startsWith(h, "[fc") or startsWith(h, "[fd") or startsWith(h, "[fe80") or startsWith(h, "[::ffff:");
    }
    return false;
}

// رابط ويب عام آمن: https/http صالح ومضيفه ليس محليًا/خاصًا.
fun iwIsPublicWeb(url) { return iwIsSafe(url) and !iwIsPrivateHost(iwHost(url)); }

fun iwFileName(url) {
    let p = iwParseUrl(url);
    let segs = split(p["path"], "/");
    if (len(segs) == 0) { return ""; }
    return iwUrlDecode(segs[len(segs) - 1]);
}

fun iwExtension(url) {
    let f = iwFileName(url);
    let d = lastIndexOf(f, ".");
    if (d < 0 or d == len(f) - 1) { return ""; }
    return lower(substr(f, d + 1, len(f) - d - 1));
}

fun iwInList(x, arr) {
    let i = 0;
    while (i < len(arr)) { if (arr[i] == x) { return true; } i = i + 1; }
    return false;
}

// نوع المحتوى من الامتداد/المزوّد: image · video · audio · pdf · document · archive · page
fun iwMediaKind(url) {
    if (iwYoutubeId(url) != "" or iwVimeoId(url) != "") { return "video"; }
    let e = iwExtension(url);
    if (iwInList(e, ["png", "jpg", "jpeg", "gif", "webp", "svg", "bmp", "avif", "ico"])) { return "image"; }
    if (iwInList(e, ["mp4", "webm", "mkv", "mov", "m3u8", "3gp", "avi"])) { return "video"; }
    if (iwInList(e, ["mp3", "wav", "ogg", "m4a", "aac", "flac", "opus"])) { return "audio"; }
    if (e == "pdf") { return "pdf"; }
    if (iwInList(e, ["doc", "docx", "xls", "xlsx", "ppt", "pptx", "txt", "csv", "odt", "rtf"])) { return "document"; }
    if (iwInList(e, ["zip", "rar", "7z", "tar", "gz", "apk"])) { return "archive"; }
    return "page";
}

// ----------------------------- 12) شريط العنوان والبحث ----------------------

// رابط بحث. engine: google · duckduckgo · bing · brave · wikipedia · youtube (الافتراضي duckduckgo).
fun iwSearchUrl(engine, query) {
    let q = iwUrlEncode(trim(toString(query)));
    let e = lower(toString(engine));
    if (e == "google") { return "https://www.google.com/search?q=" + q; }
    if (e == "bing") { return "https://www.bing.com/search?q=" + q; }
    if (e == "brave") { return "https://search.brave.com/search?q=" + q; }
    if (e == "wikipedia") { return "https://wikipedia.org/w/index.php?search=" + q; }
    if (e == "youtube") { return "https://www.youtube.com/results?search_query=" + q; }
    return "https://duckduckgo.com/?q=" + q;
}

// يتصرّف كشريط عنوان المتصفح: رابط صالح => url، نص عادي => بحث، مخطط خطر => blocked.
// الناتج: {"kind": "url"|"search"|"blocked"|"empty", "url": "...", "reason": "..."}. opts: engine.
fun iwResolveInput(input, opts) {
    if (isNil(opts)) { opts = {}; }
    let engine = "duckduckgo";
    if (has(opts, "engine")) { engine = opts["engine"]; }
    let raw = trim(toString(input));
    if (raw == "") { return {"kind": "empty", "url": "", "reason": "empty input"}; }
    let hasSpace = indexOf(raw, " ") >= 0;
    if (!hasSpace) {
        let u = iwNormalize(raw);
        if (u != "") {
            if (iwIsSafe(u)) { return {"kind": "url", "url": u, "reason": ""}; }
            return {"kind": "blocked", "url": "", "reason": "unsafe link"};
        }
        let k = iwLinkKind(raw);
        if (k == "script" or k == "data" or k == "file") { return {"kind": "blocked", "url": "", "reason": "scheme not allowed"}; }
    }
    return {"kind": "search", "url": iwSearchUrl(engine, raw), "reason": ""};
}

// ----------------------------- 13) مشاركة ------------------------------------

// رابط مشاركة: twitter/x · facebook · linkedin · telegram · whatsapp · reddit · email · sms. "" إن كان الرابط غير آمن.
fun iwShareUrl(network, url, msg) {
    if (!iwIsSafe(url)) { return ""; }
    let u = iwUrlEncode(trim(toString(url)));
    let m = "";
    if (!isNil(msg)) { m = iwUrlEncode(toString(msg)); }
    let n = lower(toString(network));
    if (n == "twitter" or n == "x") { return "https://twitter.com/intent/tweet?url=" + u + "&text=" + m; }
    if (n == "facebook") { return "https://www.facebook.com/sharer/sharer.php?u=" + u; }
    if (n == "linkedin") { return "https://www.linkedin.com/sharing/share-offsite/?url=" + u; }
    if (n == "telegram") { return "https://t.me/share/url?url=" + u + "&text=" + m; }
    if (n == "whatsapp") { return "https://wa.me/?text=" + iwUrlEncode(toString(msg) + " " + trim(toString(url))); }
    if (n == "reddit") { return "https://www.reddit.com/submit?url=" + u + "&title=" + m; }
    if (n == "email") { return "mailto:?subject=" + m + "&body=" + u; }
    if (n == "sms") { return "sms:?body=" + iwUrlEncode(toString(msg) + " " + trim(toString(url))); }
    return "";
}

// ----------------------------- 14) مزوّدو تضمين إضافيون ----------------------

fun iwStripWww(h) {
    if (startsWith(h, "www.")) { return substr(h, 4, len(h) - 4); }
    return h;
}

// open.spotify.com/(track|album|playlist|episode|show|artist)/ID
fun iwSpotifyEmbedUrl(url) {
    let p = iwParseUrl(url);
    if (!p["ok"] or iwStripWww(p["host"]) != "open.spotify.com") { return ""; }
    let segs = split(p["path"], "/");
    let i = 1;
    if (len(segs) > 1 and startsWith(segs[1], "intl-")) { i = 2; }
    if (len(segs) < i + 2) { return ""; }
    if (!iwInList(segs[i], ["track", "album", "playlist", "episode", "show", "artist"])) { return ""; }
    if (!regexTest(segs[i + 1], "^[A-Za-z0-9]{10,30}$")) { return ""; }
    return "https://open.spotify.com/embed/" + segs[i] + "/" + segs[i + 1];
}

fun iwDailymotionEmbedUrl(url) {
    let p = iwParseUrl(url);
    if (!p["ok"]) { return ""; }
    let h = iwStripWww(p["host"]);
    let id = "";
    if (h == "dai.ly") { id = substr(p["path"], 1, len(p["path"]) - 1); }
    else if (h == "dailymotion.com") {
        let segs = split(p["path"], "/");
        if (len(segs) >= 3 and segs[1] == "video") { id = segs[2]; }
    }
    if (regexTest(id, "^[A-Za-z0-9]{5,12}$")) { return "https://www.dailymotion.com/embed/video/" + id; }
    return "";
}

// drive.google.com/file/d/ID/view  ->  .../preview
fun iwDriveEmbedUrl(url) {
    let p = iwParseUrl(url);
    if (!p["ok"] or p["host"] != "drive.google.com") { return ""; }
    let segs = split(p["path"], "/");
    if (len(segs) >= 4 and segs[1] == "file" and segs[2] == "d" and regexTest(segs[3], "^[A-Za-z0-9_-]{10,}$")) {
        return "https://drive.google.com/file/d/" + segs[3] + "/preview";
    }
    return "";
}

// خريطة مضمَّنة: نص بحث أو [lat, lon].
fun iwMapsEmbedUrl(query) {
    if (isArray(query) and len(query) == 2 and iwValidLatLon(query[0], query[1])) {
        return "https://www.google.com/maps?q=" + toString(query[0]) + "," + toString(query[1]) + "&output=embed";
    }
    return "https://www.google.com/maps?q=" + iwUrlEncode(toString(query)) + "&output=embed";
}

fun iwIsEmbeddable(url) {
    return iwYoutubeId(url) != "" or iwVimeoId(url) != "" or iwSpotifyEmbedUrl(url) != "" or iwDailymotionEmbedUrl(url) != "" or iwDriveEmbedUrl(url) != "";
}

// ----------------------------- 15) توجيه الروابط العميقة ---------------------

// يطابق رابطًا مع أنماط مثل "/item/:id" أو "open/item/:id" (الأخير يشمل المضيف: myapp://open/item/5).
// الناتج: {"matched": bool, "pattern": "...", "params": {...}, "query": {...}}
fun iwTrimSlash(s) { return regexReplace(toString(s), "^/+|/+$", ""); }

fun iwRoute(url, patterns) {
    let p = iwParseUrl(url);
    let res = {"matched": false, "pattern": "", "params": {}, "query": {}};
    if (!p["ok"]) { return res; }
    let i = 0;
    while (i < len(patterns)) {
        let pat = patterns[i];
        let target = p["path"];
        if (!startsWith(pat, "/")) { target = p["host"] + p["path"]; }
        let a = split(iwTrimSlash(target), "/");
        let b = split(iwTrimSlash(pat), "/");
        let params = {};
        let ok = len(a) == len(b);
        let j = 0;
        while (ok and j < len(b)) {
            if (startsWith(b[j], ":")) { params[substr(b[j], 1, len(b[j]) - 1)] = iwUrlDecode(a[j]); }
            else if (b[j] != a[j]) { ok = false; }
            j = j + 1;
        }
        if (ok) {
            res["matched"] = true; res["pattern"] = pat; res["params"] = params; res["query"] = iwParseQuery(p["query"]);
            return res;
        }
        i = i + 1;
    }
    return res;
}

// ----------------------------- 16) روابط داخل النصوص -------------------------

// يستخرج روابط http(s) من نص (بدون علامات الترقيم الأخيرة).
fun iwExtractUrls(txt) {
    let found = regexFindAll(toString(txt), "https?://[^\\s<>\"']*[^\\s<>\"'.,;:!?)\\]،؛]");
    let out = [];
    let i = 0;
    while (i < len(found)) { push(out, found[i]); i = i + 1; }
    return out;
}

// يحوّل نصًا عاديًا إلى HTML مهرَّب مع تحويل الروابط الآمنة إلى <a> (الأسطر => <br>).
fun iwLinkify(txt, opts) {
    let rest = toString(txt);
    let urls = iwExtractUrls(rest);
    let out = "";
    let i = 0;
    while (i < len(urls)) {
        let idx = indexOf(rest, urls[i]);
        if (idx >= 0) {
            out = out + iwEscape(substr(rest, 0, idx)) + iwLinkHtmlEx(iwShorten(urls[i], 60), urls[i], opts);
            rest = substr(rest, idx + len(urls[i]), len(rest) - idx - len(urls[i]));
        }
        i = i + 1;
    }
    out = out + iwEscape(rest);
    return replace(out, "\n", "<br>");
}

// يفحص كل href في HTML: [{"url", "kind", "safe"}] — لتدقيق محتوى قبل عرضه.
fun iwAuditHtml(html) {
    let found = regexFindAll(toString(html), "i:href\\s*=\\s*(\"[^\"]*\"|'[^']*')");
    let out = [];
    let i = 0;
    while (i < len(found)) {
        let m = found[i];
        let eq = indexOf(m, "=");
        let v = trim(substr(m, eq + 1, len(m) - eq - 1));
        v = substr(v, 1, len(v) - 2);
        let safe = iwIsSafe(v);
        let k = iwLinkKind(v);
        if (k == "mail" or k == "tel" or k == "sms" or k == "geo") { safe = true; }
        push(out, {"url": v, "kind": k, "safe": safe});
        i = i + 1;
    }
    return out;
}

// ----------------------------- 17) خطة الفتح ---------------------------------

// يقرّر ماذا يُفعل برابط: {"action": "webview"|"external"|"blocked", "kind", "url", "reason"}.
// webview: ويب عام آمن (opts.allow قائمة نطاقات، opts.allowPrivate=true يسمح بالمحلي)
// external: تطبيق خارجي (بريد/اتصال/رسائل/خرائط/واتساب/تيليجرام/متجر؛ opts.allowIntent لـ intent:)
fun iwOpenPlan(url, opts) {
    if (isNil(opts)) { opts = {}; }
    let u = trim(toString(url));
    let k = iwLinkKind(u);
    let res = {"action": "blocked", "kind": k, "url": u, "reason": ""};
    if (k == "none") { res["reason"] = "empty link"; return res; }
    if (iwHasControl(u)) { res["reason"] = "control characters"; return res; }
    if (k == "web" or k == "rin" or k == "whatsapp" or k == "telegram") {
        if (!iwIsSafe(u)) { res["reason"] = "unsafe link"; return res; }
        if (!(has(opts, "allowPrivate") and opts["allowPrivate"] == true) and iwIsPrivateHost(iwHost(u))) { res["reason"] = "private or local address"; return res; }
        if (has(opts, "allow") and !iwHostAllowed(u, opts["allow"])) { res["reason"] = "host not allowed"; return res; }
        if (k == "whatsapp" or k == "telegram") { res["action"] = "external"; } else { res["action"] = "webview"; }
        return res;
    }
    if (k == "mail" or k == "tel" or k == "sms" or k == "geo" or k == "market") { res["action"] = "external"; return res; }
    if (k == "intent" and has(opts, "allowIntent") and opts["allowIntent"] == true) { res["action"] = "external"; return res; }
    if (k == "anchor" or k == "relative") { res["reason"] = "relative link needs a base url"; return res; }
    res["reason"] = "scheme not allowed";
    return res;
}

// ----------------------------- 18) صفحات جاهزة لـ WebView --------------------

fun iwCardHtml(title, desc, url, image) {
    let out = "<a class=\"iw-card\" style=\"display:block;text-decoration:none;color:inherit;border:1px solid #8884;border-radius:14px;overflow:hidden\"";
    if (iwIsSafe(url)) { out = out + " href=\"" + iwEscape(trim(toString(url))) + "\" target=\"_blank\" rel=\"noopener noreferrer\""; }
    out = out + ">";
    if (!isNil(image) and iwIsHttps(image)) {
        out = out + "<img src=\"" + iwEscape(trim(toString(image))) + "\" alt=\"\" loading=\"lazy\" referrerpolicy=\"no-referrer\" style=\"display:block;width:100%;height:auto\">";
    }
    out = out + "<div style=\"padding:12px 14px\"><div style=\"font-weight:700\">" + iwEscape(title) + "</div>";
    if (!isNil(desc) and toString(desc) != "") { out = out + "<div style=\"opacity:.75;margin-top:4px\">" + iwEscape(desc) + "</div>"; }
    if (iwIsSafe(url)) { out = out + "<div style=\"opacity:.55;margin-top:6px;font-size:.85em\">" + iwEscape(iwDisplay(url)) + "</div>"; }
    return out + "</div></a>";
}

// صفحة خطأ (اتصال/حجب) مع زر إعادة المحاولة عبر الجسر الأصلي RinPreview.tap().
fun iwErrorHtml(title, message, opts) {
    if (isNil(opts)) { opts = {}; }
    let dir = "auto";
    if (has(opts, "dir")) { dir = opts["dir"]; }
    let body = "<div style=\"display:flex;flex-direction:column;align-items:center;justify-content:center;height:100%;gap:10px;text-align:center;padding:20px\">";
    body = body + "<div style=\"font-size:2.2em\">⚠️</div><div style=\"font-weight:700\">" + iwEscape(title) + "</div><div style=\"opacity:.75\">" + iwEscape(message) + "</div>";
    if (has(opts, "retry")) { body = body + iwTapButton(opts["retry"], {}); }
    body = body + "</div>";
    let f = {"dir": dir};
    if (has(opts, "theme")) { f["theme"] = opts["theme"]; }
    return iwFragment(body, f);
}

fun iwLoadingHtml(message, opts) {
    if (isNil(opts)) { opts = {}; }
    let body = "<div style=\"display:flex;align-items:center;justify-content:center;height:100%;gap:10px\"><div style=\"width:18px;height:18px;border-radius:50%;border:3px solid #8886;border-top-color:#6C5CE7;animation:iwspin 1s linear infinite\"></div><span>" + iwEscape(message) + "</span></div><style>@keyframes iwspin{to{transform:rotate(360deg)}}</style>";
    let f = {"dir": "auto"};
    if (has(opts, "theme")) { f["theme"] = opts["theme"]; }
    return iwFragment(body, f);
}

// ----------------------------- 19) المفضّلة ----------------------------------

fun iwBookmarksNew() { return {"items": []}; }

fun iwBookmarkIndex(b, url) {
    let u = iwNormalize(url);
    let i = 0;
    while (i < len(b["items"])) { if (b["items"][i]["url"] == u) { return i; } i = i + 1; }
    return -1;
}

// يضيف/يحدّث مفضّلة. يرفض الروابط غير الآمنة. العنوان الفارغ يصير host.
fun iwBookmarkAdd(b, url, title) {
    let u = iwNormalize(url);
    if (u == "" or !iwIsSafe(u)) { return false; }
    let t = trim(toString(title));
    if (t == "") { t = iwDisplay(u); }
    let idx = iwBookmarkIndex(b, u);
    if (idx >= 0) { b["items"][idx]["title"] = t; return true; }
    push(b["items"], {"url": u, "title": t});
    return true;
}

fun iwBookmarkHas(b, url) { return iwBookmarkIndex(b, url) >= 0; }

fun iwBookmarkRemove(b, url) {
    let idx = iwBookmarkIndex(b, url);
    if (idx < 0) { return false; }
    let ns = [];
    let i = 0;
    while (i < len(b["items"])) { if (i != idx) { push(ns, b["items"][i]); } i = i + 1; }
    b["items"] = ns;
    return true;
}

// بحث في العنوان والرابط (غير حسّاس لحالة الأحرف). q فارغ => الكل.
fun iwBookmarkList(b, q) {
    let needle = lower(trim(toString(q)));
    let out = [];
    let i = 0;
    while (i < len(b["items"])) {
        let it = b["items"][i];
        if (needle == "" or contains(lower(it["title"]), needle) or contains(lower(it["url"]), needle)) { push(out, it); }
        i = i + 1;
    }
    return out;
}

fun iwBookmarksSave(b) { return jsonEncode(b["items"]); }

// تحميل من JSON مع إعادة التحقق من كل رابط (يتجاهل التالف).
fun iwBookmarksLoad(js) {
    let b = iwBookmarksNew();
    let arr = jsonDecode(js);
    if (!isArray(arr)) { return b; }
    let i = 0;
    while (i < len(arr)) {
        if (isMap(arr[i]) and has(arr[i], "url")) {
            let t = "";
            if (has(arr[i], "title")) { t = arr[i]["title"]; }
            iwBookmarkAdd(b, arr[i]["url"], t);
        }
        i = i + 1;
    }
    return b;
}

fun iwNavList(nav) { return nav["stack"]; }


// ----------------------------- 20) شاشة الربط (Link Screen) -------------------
// شاشة جاهزة تعرض الرابط حسب نوعه قبل فتحه: فيديو (صورة مصغّرة + تشغيل أو مشغّل مضمَّن)، صفحة ويب،
// صورة، صوت، PDF، مستند، ملف مضغوط، بريد، اتصال، رسالة، موقع، واتساب/تيليجرام، متجر، مكتبة Rin.
// الروابط الخطرة تظهر كشاشة "محجوب" بدل الفتح. لا JavaScript (الأزرار روابط عادية).

fun iwLabels(lang) {
    if (lang == "ar") {
        return {"web": "صفحة ويب", "video": "فيديو", "image": "صورة", "audio": "صوت", "pdf": "ملف PDF", "document": "مستند",
                "archive": "ملف مضغوط", "mail": "بريد إلكتروني", "tel": "اتصال هاتفي", "sms": "رسالة", "geo": "موقع", "whatsapp": "واتساب",
                "telegram": "تيليجرام", "market": "متجر التطبيقات", "rin": "مكتبة Rin", "intent": "تطبيق",
                "a_open": "فتح", "a_play": "تشغيل", "a_call": "اتصال", "a_mail": "إرسال بريد", "a_sms": "إرسال رسالة",
                "a_geo": "فتح الموقع", "a_download": "تنزيل", "a_store": "فتح المتجر", "a_app": "فتح في التطبيق",
                "secure": "اتصال آمن (HTTPS)", "insecure": "غير مشفّر (HTTP)", "external": "يُفتح في تطبيق آخر",
                "blocked": "الرابط محجوب", "dir": "rtl"};
    }
    return {"web": "Web page", "video": "Video", "image": "Image", "audio": "Audio", "pdf": "PDF file", "document": "Document",
            "archive": "Archive", "mail": "Email", "tel": "Phone call", "sms": "Message", "geo": "Location", "whatsapp": "WhatsApp",
            "telegram": "Telegram", "market": "App store", "rin": "Rin library", "intent": "App",
            "a_open": "Open", "a_play": "Play", "a_call": "Call", "a_mail": "Send email", "a_sms": "Send message",
            "a_geo": "Open location", "a_download": "Download", "a_store": "Open store", "a_app": "Open in app",
            "secure": "Secure connection (HTTPS)", "insecure": "Not encrypted (HTTP)", "external": "Opens in another app",
            "blocked": "Link blocked", "dir": "ltr"};
}

fun iwKindIcon(k) {
    if (k == "video") { return "🎬"; }
    if (k == "image") { return "🖼️"; }
    if (k == "audio") { return "🎧"; }
    if (k == "pdf") { return "📕"; }
    if (k == "document") { return "📄"; }
    if (k == "archive") { return "🗜️"; }
    if (k == "mail") { return "✉️"; }
    if (k == "tel") { return "📞"; }
    if (k == "sms") { return "💬"; }
    if (k == "geo") { return "📍"; }
    if (k == "whatsapp") { return "🟢"; }
    if (k == "telegram") { return "✈️"; }
    if (k == "market") { return "🛍️"; }
    if (k == "rin") { return "🌿"; }
    if (k == "intent") { return "📱"; }
    return "🌐";
}

// نوع الشاشة: يجمع kind الرابط مع نوع الوسائط. الناتج واحد من مفاتيح iwLabels.
fun iwScreenKind(url) {
    let k = iwLinkKind(url);
    if (k == "web") {
        let mk = iwMediaKind(url);
        if (iwSpotifyEmbedUrl(url) != "") { return "audio"; }
        if (iwDriveEmbedUrl(url) != "") { return "document"; }
        if (iwDailymotionEmbedUrl(url) != "") { return "video"; }
        if (mk == "page") { return "web"; }
        return mk;
    }
    return k;
}

fun iwScreenBtn(label, href, primary) {
    let bg = "background:#6C5CE7;color:#fff;";
    if (!primary) { bg = "background:transparent;color:inherit;border:1px solid #8886;"; }
    return "<a href=\"" + iwEscape(href) + "\" target=\"_blank\" rel=\"noopener noreferrer\" style=\"display:inline-block;padding:10px 22px;border-radius:12px;text-decoration:none;font-weight:700;" + bg + "\">" + iwEscape(label) + "</a>";
}

// شاشة الربط كـ HTML لخاصية html=. opts: lang ("en"|"ar")، theme ("dark"|"light")، title، mode ("preview"|"embed")،
// allow / allowPrivate / allowIntent (كما في iwOpenPlan)، dir.
fun iwLinkScreen(url, opts) {
    if (isNil(opts)) { opts = {}; }
    let lang = "en";
    if (has(opts, "lang")) { lang = opts["lang"]; }
    let L = iwLabels(lang);
    let theme = "dark";
    if (has(opts, "theme")) { theme = opts["theme"]; }
    let fopts = {"theme": theme, "dir": L["dir"]};
    if (has(opts, "dir")) { fopts["dir"] = opts["dir"]; }
    let u = trim(toString(url));
    let plan = iwOpenPlan(u, opts);
    if (plan["action"] == "blocked") {
        let eo = {"dir": fopts["dir"], "theme": theme};
        return iwErrorHtml(L["blocked"], iwShorten(u, 80), eo);
    }
    let kind = iwScreenKind(u);
    let label = L["web"];
    if (has(L, kind)) { label = L[kind]; }
    let isHttp = (plan["kind"] == "web" or plan["kind"] == "rin");
    let host = iwHost(u);
    let title = "";
    if (has(opts, "title")) { title = opts["title"]; }
    if (title == "") {
        title = iwDisplay(u);
        if (plan["kind"] == "mail") { title = substr(u, 7, len(u) - 7); let qi = indexOf(title, "?"); if (qi >= 0) { title = substr(title, 0, qi); } title = iwUrlDecode(title); }
        if (plan["kind"] == "tel" or plan["kind"] == "sms") { title = iwUrlDecode(substr(u, indexOf(u, ":") + 1, len(u) - indexOf(u, ":") - 1)); let qj = indexOf(title, "?"); if (qj >= 0) { title = substr(title, 0, qj); } }
        if (kind == "rin") { let rl = iwParseRinLink(u); if (rl["ok"]) { title = "@" + rl["user"]; if (rl["library"] != "") { title = title + " / " + rl["library"]; } } }
        if (kind == "image" or kind == "pdf" or kind == "document" or kind == "archive" or kind == "audio") { let fnm = iwFileName(u); if (fnm != "") { title = fnm; } }
    }
    let primaryLabel = L["a_open"];
    let primaryHref = u;
    let media = "";
    let embed = iwEmbedUrl(u);
    let embeddable = iwIsEmbeddable(u);
    let mode = "preview";
    if (has(opts, "mode")) { mode = opts["mode"]; }

    if (kind == "video" or kind == "audio" or (kind == "document" and embeddable)) {
        if (embeddable and mode == "embed") {
            media = "<div style=\"width:100%;max-width:560px\">" + iwIframeHtml(embed, {"ratio": "16:9", "title": title}) + "</div>";
            primaryHref = u;
        } else {
            let thumb = iwYoutubeThumb(u);
            let play = "<div style=\"position:absolute;inset:0;display:flex;align-items:center;justify-content:center\"><div style=\"width:64px;height:64px;border-radius:50%;background:#000a;color:#fff;font-size:28px;line-height:64px\">▶</div></div>";
            let box = "position:relative;width:100%;max-width:560px;aspect-ratio:16/9;border-radius:14px;overflow:hidden;background:linear-gradient(135deg,#6C5CE7,#2d2a4a);display:flex;align-items:center;justify-content:center;font-size:48px";
            let inner = iwKindIcon(kind);
            if (thumb != "") { inner = "<img src=\"" + iwEscape(thumb) + "\" alt=\"\" loading=\"lazy\" referrerpolicy=\"no-referrer\" style=\"width:100%;height:100%;object-fit:cover\">"; }
            media = "<div style=\"" + box + "\">" + inner + play + "</div>";
            if (embeddable) { primaryHref = embed; }
        }
        primaryLabel = L["a_play"];
    } else if (kind == "image") {
        if (iwIsHttps(u)) {
            media = "<img src=\"" + iwEscape(u) + "\" alt=\"\" loading=\"lazy\" referrerpolicy=\"no-referrer\" style=\"max-width:100%;max-height:55%;border-radius:14px;object-fit:contain\">";
        } else {
            media = "<div style=\"font-size:56px\">" + iwKindIcon(kind) + "</div>";
        }
    } else {
        media = "<div style=\"font-size:56px\">" + iwKindIcon(kind) + "</div>";
    }
    if (kind == "pdf" or kind == "archive" or (kind == "document" and !embeddable)) { primaryLabel = L["a_download"]; }
    if (kind == "mail") { primaryLabel = L["a_mail"]; }
    if (kind == "tel") { primaryLabel = L["a_call"]; }
    if (kind == "sms") { primaryLabel = L["a_sms"]; }
    if (kind == "geo") { primaryLabel = L["a_geo"]; }
    if (kind == "market") { primaryLabel = L["a_store"]; }
    if (kind == "intent" or kind == "whatsapp" or kind == "telegram") { primaryLabel = L["a_app"]; }

    let badge = "<div style=\"display:inline-block;padding:3px 12px;border-radius:99px;background:#6C5CE733;font-size:.8em;font-weight:700\">" + iwKindIcon(kind) + " " + iwEscape(label) + "</div>";
    let info = "";
    if (isHttp) {
        let sec = "🔒 " + L["secure"];
        if (!iwIsHttps(u)) { sec = "⚠️ " + L["insecure"]; }
        info = "<div style=\"opacity:.65;font-size:.85em\">" + iwEscape(host) + " · " + iwEscape(sec) + "</div>";
    } else {
        info = "<div style=\"opacity:.65;font-size:.85em\">" + iwEscape(L["external"]) + "</div>";
    }
    let body = "<div class=\"iw-screen\" style=\"display:flex;flex-direction:column;align-items:center;justify-content:center;gap:12px;min-height:100%;padding:20px;text-align:center;box-sizing:border-box\">";
    body = body + media + badge;
    body = body + "<div style=\"font-weight:800;font-size:1.15em;word-break:break-word;max-width:100%\">" + iwEscape(iwShorten(title, 90)) + "</div>" + info;
    body = body + "<div style=\"display:flex;gap:10px;flex-wrap:wrap;justify-content:center;margin-top:4px\">" + iwScreenBtn(primaryLabel, primaryHref, true);
    if (primaryHref != u and isHttp) { body = body + iwScreenBtn(L["a_open"], u, false); }
    body = body + "</div></div>";
    return iwFragment(body, fopts);
}

// سمات عنصر WebView لشاشة الربط (نفس شكل iwWebViewAttrs). opts تدعم height.
fun iwLinkScreenAttrs(url, opts) {
    if (isNil(opts)) { opts = {}; }
    let plan = iwOpenPlan(url, opts);
    let attrs = {"html": iwLinkScreen(url, opts)};
    if (has(opts, "height")) { attrs["height"] = opts["height"]; }
    if (has(opts, "ratio") and iwRatio(opts["ratio"]) > 0) { attrs["ratio"] = toString(opts["ratio"]); }
    return {"ok": plan["action"] != "blocked", "mode": "html", "kind": iwScreenKind(url), "action": plan["action"], "attrs": attrs, "error": plan["reason"]};
}

// كود Rin جاهز لعنصر WebView يعرض شاشة الربط.
fun iwLinkScreenSource(name, url, opts) {
    if (isNil(opts)) { opts = {}; }
    if (!iwIsIdent(name)) { return ""; }
    let r = iwLinkScreenAttrs(url, opts);
    let dialect = "view";
    if (has(opts, "dialect") and opts["dialect"] == "element") { dialect = "element"; }
    let out = "@" + dialect + ".WebView=" + name + "\n    html=" + iwRinString(r["attrs"]["html"]) + ";\n";
    if (has(r["attrs"], "height")) { out = out + "    height=" + toString(r["attrs"]["height"]) + ";\n"; }
    if (has(r["attrs"], "ratio")) { out = out + "    ratio=" + iwRinString(r["attrs"]["ratio"]) + ";\n"; }
    return out + ".end/" + dialect + "\n";
}

// ----------------------------- معلومات --------------------------------------

fun iwInfo() {
    return {
        "name": "indsinweb",
        "version": "1.0.0",
        "description": "WebView and link toolkit for Indsin: safe URLs, link kinds, embeds, html= builders, WebView source generation",
        "features": [
            "URL parse / normalize / join",
            "Scheme allowlist, host allowlist, userinfo phishing guard",
            "Query build / parse / edit",
            "mailto / tel / sms / geo / WhatsApp / Telegram / Play Store / Android intent / deep links",
            "Rin library and profile links",
            "YouTube (nocookie) and Vimeo embeds",
            "Safe link / iframe / fragment / page HTML",
            "WebView attrs and Rin source generator",
            "Back/forward navigation history",
            "Omnibox resolver (URL or search), share links, bookmarks",
            "Private/local host blocking, tracking-param stripping, media kind detection",
            "Spotify / Dailymotion / Google Drive embeds, route matching, linkify, link audit",
            "Card, error and loading pages for WebView",
            "Link screen: video / web page / image / audio / PDF / mail / phone / location / Rin library"
        ]
    };
}
)INDSINWEBOGRIN";

static const char* kLib_indsinicons_og_rin = R"INDSINICONSOGRIN(
// ============================================================================
// lib/indsinicons.og.rin — IndsinIcons: مكتبة أيقونات متجهية (SVG) لمحرّك indsin
// ============================================================================
// 187 أيقونة مرسومة من الصفر على شبكة 24×24 (خط سماكته 2، أطراف دائرية)، كل أيقونة مسار SVG
// واحد. مكتوبة بالكامل بلغة Rin؛ تُولَّد البيانات من tools/icons/icons_def.py (لا تعدّلها يدوياً).
//
//   @import "lib/indsinicons.og.rin";
//
//   print ic_svg("home", nil);                                  // <svg ...> جاهز للصق في html=
//   print ic_svg("heart", {"size": 32, "color": "#e11d48", "filled": true});
//   print ic_has("gear");                                       // true  (اسم بديل لـ settings)
//   print ic_search("arrow");                                   // كل الأيقونات التي تحوي arrow
//   ic_register("my-logo", "brand", "M4 4H20V20H4Z");           // أيقونتك الخاصة
//
// الأقسام (كل الدوال بادئتها ic_):
//   1) الفهرس        ic_count · ic_names · ic_has · ic_resolve · ic_aliasesOf · ic_category
//   2) الفئات        ic_categories · ic_categoryName · ic_byCategory
//   3) البحث         ic_search
//   4) التوليد       ic_path · ic_svg · ic_svgn · ic_html · ic_dataUri · ic_sprite · ic_symbol
//   5) تكامل indsin  ic_snippet · ic_audit
//   6) التوسعة       ic_register · ic_validPath
//
// ملاحظة: دوال Rin تتطلب عدداً مطابقاً من الوسائط؛ مرّر nil لـ opts الاختيارية.
// خيارات opts: size (24) · color ("currentColor") · stroke (2) · filled (false) · title (nil) · class (nil)
// ============================================================================

let __ic_version = "1.0.0";

let __ic = {
    "activity": ["status", "M3 12H7L10 4L14 20L17 12H21"],
    "align-center": ["design", "M4 6H20M7 10H17M4 14H20M7 18H17"],
    "align-left": ["design", "M4 6H20M4 10H14M4 14H20M4 18H14"],
    "align-right": ["design", "M4 6H20M10 10H20M4 14H20M10 18H20"],
    "archive": ["files", "M4 4H20A1 1 0 0 1 21 5V8A1 1 0 0 1 20 9H4A1 1 0 0 1 3 8V5A1 1 0 0 1 4 4ZM5 9V19A1 1 0 0 0 6 20H18A1 1 0 0 0 19 19V9M10 13H14"],
    "arrow-down": ["arrows", "M12 5V19M6 13L12 19L18 13"],
    "arrow-down-left": ["arrows", "M17 7L7 17M7 8V17H16"],
    "arrow-down-right": ["arrows", "M7 7L17 17M17 8V17H8"],
    "arrow-left": ["arrows", "M19 12H5M11 6L5 12L11 18"],
    "arrow-right": ["arrows", "M5 12H19M13 6L19 12L13 18"],
    "arrow-up": ["arrows", "M12 19V5M6 11L12 5L18 11"],
    "arrow-up-left": ["arrows", "M17 17L7 7M16 7H7V16"],
    "arrow-up-right": ["arrows", "M7 17L17 7M8 7H17V16"],
    "at-sign": ["comms", "M8 12A4 4 0 1 0 16 12A4 4 0 1 0 8 12ZM16 8V13A2.5 2.5 0 0 0 21 13V12A9 9 0 1 0 17 19.5"],
    "award": ["status", "M6 9A6 6 0 1 0 18 9A6 6 0 1 0 6 9ZM8.5 14L7 21L12 18.5L17 21L15.5 14"],
    "battery": ["design", "M4 7H18A2 2 0 0 1 20 9V15A2 2 0 0 1 18 17H4A2 2 0 0 1 2 15V9A2 2 0 0 1 4 7ZM22 11V13M6 10.5H14V13.5H6Z"],
    "bell": ["status", "M6 10A6 6 0 0 1 18 10C18 14 19 16 20 17H4C5 16 6 14 6 10ZM10 20.5A2.2 2.2 0 0 0 14 20.5"],
    "bell-off": ["status", "M6 10A6 6 0 0 1 18 10C18 14 19 16 20 17H4C5 16 6 14 6 10ZM10 20.5A2.2 2.2 0 0 0 14 20.5M4 4L20 20"],
    "bold": ["design", "M7 4H13A4 4 0 0 1 13 12H7ZM7 12H14A4 4 0 0 1 14 20H7Z"],
    "book": ["dev", "M3 5A1 1 0 0 1 4 4H10A2 2 0 0 1 12 6V20A2 2 0 0 0 10 19H4A1 1 0 0 1 3 18ZM21 5A1 1 0 0 0 20 4H14A2 2 0 0 0 12 6V20A2 2 0 0 1 14 19H20A1 1 0 0 0 21 18Z"],
    "bookmark": ["status", "M6 4H18V21L12 17L6 21Z"],
    "braces": ["dev", "M9 4C6.5 4 6 5 6 7V9C6 10.5 5 12 3.5 12C5 12 6 13.5 6 15V17C6 19 6.5 20 9 20M15 4C17.5 4 18 5 18 7V9C18 10.5 19 12 20.5 12C19 12 18 13.5 18 15V17C18 19 17.5 20 15 20"],
    "bug": ["dev", "M12 8C14.5 8 16 9.5 16 12V15A4 4 0 0 1 8 15V12C8 9.5 9.5 8 12 8ZM9.5 6.5A2.5 2.5 0 0 1 14.5 6.5M8 12H4M16 12H20M8 16H4.5M16 16H19.5M8.5 9L5.5 6.5M15.5 9L18.5 6.5M12 12V19"],
    "calendar": ["files", "M6 5H18A2 2 0 0 1 20 7V18A2 2 0 0 1 18 20H6A2 2 0 0 1 4 18V7A2 2 0 0 1 6 5ZM4 10H20M8 3V7M16 3V7"],
    "camera": ["media", "M4 8H7L9 5H15L17 8H20A1 1 0 0 1 21 9V19A1 1 0 0 1 20 20H4A1 1 0 0 1 3 19V9A1 1 0 0 1 4 8ZM8.5 13.5A3.5 3.5 0 1 0 15.5 13.5A3.5 3.5 0 1 0 8.5 13.5Z"],
    "cart": ["dev", "M3 4H6L8 15H18L20 7H7M8.2 19A1.3 1.3 0 1 0 10.8 19A1.3 1.3 0 1 0 8.2 19ZM15.7 19A1.3 1.3 0 1 0 18.3 19A1.3 1.3 0 1 0 15.7 19Z"],
    "chart-bar": ["charts", "M5 11H7A1 1 0 0 1 8 12V19A1 1 0 0 1 7 20H5A1 1 0 0 1 4 19V12A1 1 0 0 1 5 11ZM11 4H13A1 1 0 0 1 14 5V19A1 1 0 0 1 13 20H11A1 1 0 0 1 10 19V5A1 1 0 0 1 11 4ZM17 8H19A1 1 0 0 1 20 9V19A1 1 0 0 1 19 20H17A1 1 0 0 1 16 19V9A1 1 0 0 1 17 8Z"],
    "chart-line": ["charts", "M3 3V21H21M7 15L11 10L14 13L20 6"],
    "chart-pie": ["charts", "M12 3A9 9 0 1 0 21 12H12ZM15 3.5A9 9 0 0 1 20.5 9H15Z"],
    "check": ["actions", "M5 12.5L10 17.5L19 7"],
    "check-circle": ["actions", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM8 12.5L11 15.5L16 9"],
    "chevron-down": ["arrows", "M5 9L12 16L19 9"],
    "chevron-left": ["arrows", "M15 5L8 12L15 19"],
    "chevron-right": ["arrows", "M9 5L16 12L9 19"],
    "chevron-up": ["arrows", "M5 15L12 8L19 15"],
    "chevrons-down": ["arrows", "M6 5L12 11L18 5M6 13L12 19L18 13"],
    "chevrons-left": ["arrows", "M11 6L5 12L11 18M19 6L13 12L19 18"],
    "chevrons-right": ["arrows", "M13 6L19 12L13 18M5 6L11 12L5 18"],
    "chevrons-up": ["arrows", "M6 11L12 5L18 11M6 19L12 13L18 19"],
    "circle": ["charts", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12Z"],
    "clipboard": ["actions", "M7 5H17A2 2 0 0 1 19 7V19A2 2 0 0 1 17 21H7A2 2 0 0 1 5 19V7A2 2 0 0 1 7 5ZM9 5V4A1 1 0 0 1 10 3H14A1 1 0 0 1 15 4V5M9 12H15M9 16H13"],
    "clock": ["files", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 7V12L15.5 14"],
    "close": ["actions", "M6 6L18 18M18 6L6 18"],
    "cloud": ["comms", "M7 18A4.5 4.5 0 0 1 6.5 9A6 6 0 0 1 18 8.5A4.8 4.8 0 0 1 17.5 18Z"],
    "cloud-download": ["comms", "M7 17A4.5 4.5 0 0 1 6.5 8A6 6 0 0 1 18 7.5A4.8 4.8 0 0 1 18 17M12 12V20M9 17L12 20L15 17"],
    "cloud-upload": ["comms", "M7 17A4.5 4.5 0 0 1 6.5 8A6 6 0 0 1 18 7.5A4.8 4.8 0 0 1 18 17M12 20V12M9 15L12 12L15 15"],
    "code": ["dev", "M8 7L3 12L8 17M16 7L21 12L16 17M13.5 5L10.5 19"],
    "coffee": ["dev", "M5 9H16V15A5 5 0 0 1 11 20H10A5 5 0 0 1 5 15ZM16 10H18A2.5 2.5 0 0 1 18 15H16M8 3C8 5 9.5 5 9.5 7M12 3C12 5 13.5 5 13.5 7"],
    "compass": ["comms", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM15.5 8.5L13.5 13.5L8.5 15.5L10.5 10.5Z"],
    "container": ["dev", "M7 5H4V19H7M17 5H20V19H17M10 8.5H14A1.5 1.5 0 0 1 15.5 10V14A1.5 1.5 0 0 1 14 15.5H10A1.5 1.5 0 0 1 8.5 14V10A1.5 1.5 0 0 1 10 8.5Z"],
    "copy": ["actions", "M10 8H18A2 2 0 0 1 20 10V18A2 2 0 0 1 18 20H10A2 2 0 0 1 8 18V10A2 2 0 0 1 10 8ZM16 8V6A2 2 0 0 0 14 4H6A2 2 0 0 0 4 6V14A2 2 0 0 0 6 16H8"],
    "corner-down-right": ["arrows", "M5 5V10A5 5 0 0 0 10 15H19M15 11L19 15L15 19"],
    "cpu": ["dev", "M8 6H16A2 2 0 0 1 18 8V16A2 2 0 0 1 16 18H8A2 2 0 0 1 6 16V8A2 2 0 0 1 8 6ZM10.5 9.5H13.5A1 1 0 0 1 14.5 10.5V13.5A1 1 0 0 1 13.5 14.5H10.5A1 1 0 0 1 9.5 13.5V10.5A1 1 0 0 1 10.5 9.5ZM9 2V6M15 2V6M9 18V22M15 18V22M2 9H6M2 15H6M18 9H22M18 15H22"],
    "credit-card": ["dev", "M5 5H19A2 2 0 0 1 21 7V17A2 2 0 0 1 19 19H5A2 2 0 0 1 3 17V7A2 2 0 0 1 5 5ZM3 10H21M7 15H11"],
    "cursor": ["actions", "M5 3L19 11L12 13L10 20Z"],
    "database": ["files", "M4 6C4 4.3 7.6 3 12 3C16.4 3 20 4.3 20 6C20 7.7 16.4 9 12 9C7.6 9 4 7.7 4 6ZM4 6V18C4 19.7 7.6 21 12 21C16.4 21 20 19.7 20 18V6M4 12C4 13.7 7.6 15 12 15C16.4 15 20 13.7 20 12"],
    "download": ["actions", "M12 4V15M7 10L12 15L17 10M5 20H19"],
    "droplet": ["design", "M12 3C12 3 5.5 10 5.5 14.5A6.5 6.5 0 0 0 18.5 14.5C18.5 10 12 3 12 3Z"],
    "edit": ["actions", "M4 20L4 16L15 5L19 9L8 20ZM13 7L17 11"],
    "error": ["status", "M15.67 3.13L20.87 8.33L20.87 15.67L15.67 20.87L8.33 20.87L3.13 15.67L3.13 8.33L8.33 3.13ZM12 8V13M12 16L12.01 16"],
    "external-link": ["arrows", "M14 4H20V10M20 4L11 13M18 14V19A1 1 0 0 1 17 20H5A1 1 0 0 1 4 19V7A1 1 0 0 1 5 6H10"],
    "eye": ["security", "M2 12C4.5 7.5 8 5.5 12 5.5C16 5.5 19.5 7.5 22 12C19.5 16.5 16 18.5 12 18.5C8 18.5 4.5 16.5 2 12ZM9 12A3 3 0 1 0 15 12A3 3 0 1 0 9 12Z"],
    "eye-off": ["security", "M2 12C4.5 7.5 8 5.5 12 5.5C16 5.5 19.5 7.5 22 12C19.5 16.5 16 18.5 12 18.5C8 18.5 4.5 16.5 2 12ZM9 12A3 3 0 1 0 15 12A3 3 0 1 0 9 12ZM4 4L20 20"],
    "fast-forward": ["media", "M3 6L11 12L3 18ZM12 6L20 12L12 18Z"],
    "file": ["files", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3ZM14 3V8H19"],
    "file-code": ["files", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3ZM14 3V8H19M10 13L8 15L10 17M14 13L16 15L14 17"],
    "file-plus": ["files", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3ZM14 3V8H19M12 12V18M9 15H15"],
    "file-text": ["files", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3ZM14 3V8H19M8.5 13H15.5M8.5 17H13"],
    "film": ["media", "M6 3H18A2 2 0 0 1 20 5V19A2 2 0 0 1 18 21H6A2 2 0 0 1 4 19V5A2 2 0 0 1 6 3ZM4 8H8M4 12H8M4 16H8M16 8H20M16 12H20M16 16H20M8 3V21M16 3V21"],
    "filter": ["actions", "M4 5H20L14 12.5V19L10 17V12.5Z"],
    "flag": ["status", "M5 21V4M5 4H17L15 8.5L17 13H5"],
    "folder": ["files", "M3 7A2 2 0 0 1 5 5H9.5L12 8H19A2 2 0 0 1 21 10V17A2 2 0 0 1 19 19H5A2 2 0 0 1 3 17Z"],
    "folder-open": ["files", "M3 18V6.5A1.5 1.5 0 0 1 4.5 5H9L11.5 7.5H18A1.5 1.5 0 0 1 19.5 9V11M3 18L5.5 11.5H21.5L19 18.5A1.5 1.5 0 0 1 17.5 19.5H4.5A1.5 1.5 0 0 1 3 18Z"],
    "function": ["dev", "M14 4C11 4 10 5.5 10 8V20M7 11H14M15 14L20 20M20 14L15 20"],
    "gift": ["status", "M4 8H20A1 1 0 0 1 21 9V11A1 1 0 0 1 20 12H4A1 1 0 0 1 3 11V9A1 1 0 0 1 4 8ZM5 12V20H19V12M12 8V20M12 8C9 8 7.5 6.5 7.5 5.2A2 2 0 0 1 11 4.5C12 6 12 8 12 8M12 8C15 8 16.5 6.5 16.5 5.2A2 2 0 0 0 13 4.5C12 6 12 8 12 8"],
    "git-branch": ["dev", "M6 3V15M3 18A3 3 0 1 0 9 18A3 3 0 1 0 3 18ZM15 6A3 3 0 1 0 21 6A3 3 0 1 0 15 6ZM18 9C18 14 14 18 9 18"],
    "globe": ["comms", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM3 12H21M12 3C9 6 8 9 8 12C8 15 9 18 12 21C15 18 16 15 16 12C16 9 15 6 12 3Z"],
    "graduation": ["dev", "M2 9L12 4L22 9L12 14ZM6 11.5V16C6 17.5 9 19 12 19C15 19 18 17.5 18 16V11.5M22 9V15"],
    "grid": ["actions", "M5.5 4H9A1.5 1.5 0 0 1 10.5 5.5V9A1.5 1.5 0 0 1 9 10.5H5.5A1.5 1.5 0 0 1 4 9V5.5A1.5 1.5 0 0 1 5.5 4ZM15 4H18.5A1.5 1.5 0 0 1 20 5.5V9A1.5 1.5 0 0 1 18.5 10.5H15A1.5 1.5 0 0 1 13.5 9V5.5A1.5 1.5 0 0 1 15 4ZM5.5 13.5H9A1.5 1.5 0 0 1 10.5 15V18.5A1.5 1.5 0 0 1 9 20H5.5A1.5 1.5 0 0 1 4 18.5V15A1.5 1.5 0 0 1 5.5 13.5ZM15 13.5H18.5A1.5 1.5 0 0 1 20 15V18.5A1.5 1.5 0 0 1 18.5 20H15A1.5 1.5 0 0 1 13.5 18.5V15A1.5 1.5 0 0 1 15 13.5Z"],
    "headphones": ["media", "M4 15V12A8 8 0 0 1 20 12V15M4.5 14H6A1.5 1.5 0 0 1 7.5 15.5V19A1.5 1.5 0 0 1 6 20.5H4.5A1.5 1.5 0 0 1 3 19V15.5A1.5 1.5 0 0 1 4.5 14ZM18 14H19.5A1.5 1.5 0 0 1 21 15.5V19A1.5 1.5 0 0 1 19.5 20.5H18A1.5 1.5 0 0 1 16.5 19V15.5A1.5 1.5 0 0 1 18 14Z"],
    "heart": ["status", "M12 20L4.6 12.4A4.9 4.9 0 0 1 12 6.2A4.9 4.9 0 0 1 19.4 12.4Z"],
    "help": ["status", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM9.5 9.5A2.6 2.6 0 0 1 14.6 10.2C14.6 12 12 12 12 14M12 17L12.01 17"],
    "hexagon": ["charts", "M12 2.8L19.97 7.4L19.97 16.6L12 21.2L4.03 16.6L4.03 7.4Z"],
    "history": ["files", "M6.86 5.87A8 8 0 1 0 14.74 4.48M18.26 3.06L14.74 4.48L16.52 7.84M12 8V12L15 14"],
    "home": ["actions", "M3 11L12 4L21 11M5.5 9.5V20H18.5V9.5M10 20V14H14V20"],
    "hourglass": ["files", "M6 3H18M6 21H18M7 3C7 8 10 10 12 12C14 14 17 16 17 21M17 3C17 8 14 10 12 12C10 14 7 16 7 21"],
    "image": ["media", "M5 4H19A2 2 0 0 1 21 6V18A2 2 0 0 1 19 20H5A2 2 0 0 1 3 18V6A2 2 0 0 1 5 4ZM7 9.5A1.5 1.5 0 1 0 10 9.5A1.5 1.5 0 1 0 7 9.5ZM3.5 17L9 12L13 16L16 13L20.5 17.5"],
    "inbox": ["files", "M3 13H8L9.5 16H14.5L16 13H21M3 13L6 5H18L21 13V19A1 1 0 0 1 20 20H4A1 1 0 0 1 3 19Z"],
    "info": ["status", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 11V16M12 8L12.01 8"],
    "italic": ["design", "M10 4H19M5 20H14M15 4L9 20"],
    "key": ["security", "M4 15A4 4 0 1 0 12 15A4 4 0 1 0 4 15ZM11 12L20 3M16 7L19 10"],
    "layers": ["dev", "M12 3L21 7.5L12 12L3 7.5ZM3 12L12 16.5L21 12M3 16.5L12 21L21 16.5"],
    "layout": ["charts", "M5 3H19A2 2 0 0 1 21 5V19A2 2 0 0 1 19 21H5A2 2 0 0 1 3 19V5A2 2 0 0 1 5 3ZM3 9H21M9 9V21"],
    "library": ["dev", "M4.5 4H7A1 1 0 0 1 8 5V19A1 1 0 0 1 7 20H4.5A1 1 0 0 1 3.5 19V5A1 1 0 0 1 4.5 4ZM10.5 4H13A1 1 0 0 1 14 5V19A1 1 0 0 1 13 20H10.5A1 1 0 0 1 9.5 19V5A1 1 0 0 1 10.5 4ZM16.2 6.5L20 5.5L22 18L18.2 19Z"],
    "lightbulb": ["dev", "M9 18H15M10 21H14M12 3A6 6 0 0 0 8.5 14C9.3 14.7 9.5 15.5 9.5 16.5V18H14.5V16.5C14.5 15.5 14.7 14.7 15.5 14A6 6 0 0 0 12 3Z"],
    "link": ["actions", "M10 14A4 4 0 0 0 15.7 14.3L18.7 11.3A4 4 0 0 0 13 5.6L12 6.6M14 10A4 4 0 0 0 8.3 9.7L5.3 12.7A4 4 0 0 0 11 18.4L12 17.4"],
    "list": ["actions", "M9 6H20M9 12H20M9 18H20M4.5 6L4.51 6M4.5 12L4.51 12M4.5 18L4.51 18"],
    "loader": ["actions", "M17 12L21 12M15.54 15.54L18.36 18.36M12 17L12 21M8.46 15.54L5.64 18.36M7 12L3 12M8.46 8.46L5.64 5.64M12 7L12 3M15.54 8.46L18.36 5.64"],
    "lock": ["security", "M7 11H17A2 2 0 0 1 19 13V19A2 2 0 0 1 17 21H7A2 2 0 0 1 5 19V13A2 2 0 0 1 7 11ZM8 11V8A4 4 0 0 1 16 8V11M12 16L12.01 16"],
    "login": ["arrows", "M10 8L15 12L10 16M15 12H3M13 4H19A1 1 0 0 1 20 5V19A1 1 0 0 1 19 20H13"],
    "logout": ["arrows", "M9 4H5A1 1 0 0 0 4 5V19A1 1 0 0 0 5 20H9M16 8L21 12L16 16M21 12H9"],
    "loop": ["dev", "M4.48 9.26A8 8 0 0 1 19.52 9.26M16.16 7.48L19.52 9.26L20.94 5.74M19.52 14.74A8 8 0 0 1 4.48 14.74M7.84 16.52L4.48 14.74L3.06 18.26"],
    "mail": ["comms", "M5 5H19A2 2 0 0 1 21 7V17A2 2 0 0 1 19 19H5A2 2 0 0 1 3 17V7A2 2 0 0 1 5 5ZM3.5 7L12 13L20.5 7"],
    "map-pin": ["comms", "M12 21C12 21 5 14.5 5 9.5A7 7 0 0 1 19 9.5C19 14.5 12 21 12 21ZM9.5 9.5A2.5 2.5 0 1 0 14.5 9.5A2.5 2.5 0 1 0 9.5 9.5Z"],
    "maximize": ["arrows", "M4 9V4H9M15 4H20V9M20 15V20H15M9 20H4V15"],
    "menu": ["actions", "M4 6H20M4 12H20M4 18H20"],
    "menu-left": ["actions", "M4 7H20M4 12H13M4 17H17"],
    "message": ["comms", "M5 4H19A2 2 0 0 1 21 6V15A2 2 0 0 1 19 17H11L6.5 20.5V17H5A2 2 0 0 1 3 15V6A2 2 0 0 1 5 4Z"],
    "mic": ["media", "M12 3H12A3 3 0 0 1 15 6V11A3 3 0 0 1 12 14H12A3 3 0 0 1 9 11V6A3 3 0 0 1 12 3ZM5.5 11A6.5 6.5 0 0 0 18.5 11M12 17.5V21M9 21H15"],
    "minimize": ["arrows", "M9 4V9H4M15 4V9H20M20 15H15V20M9 20V15H4"],
    "minus": ["actions", "M5 12H19"],
    "minus-circle": ["actions", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM8 12H16"],
    "moon": ["design", "M20 14.5A8.5 8.5 0 1 1 9.5 4A6.5 6.5 0 0 0 20 14.5Z"],
    "more": ["actions", "M5 12L5.01 12M12 12L12.01 12M19 12L19.01 12"],
    "more-vertical": ["actions", "M12 5L12.01 5M12 12L12.01 12M12 19L12.01 19"],
    "move": ["arrows", "M12 3V21M3 12H21M9 6L12 3L15 6M9 18L12 21L15 18M6 9L3 12L6 15M18 9L21 12L18 15"],
    "music": ["media", "M9 18V6L20 4V16M4 18A2.5 2.5 0 1 0 9 18A2.5 2.5 0 1 0 4 18ZM15 16A2.5 2.5 0 1 0 20 16A2.5 2.5 0 1 0 15 16Z"],
    "package": ["dev", "M12 3L20 7.5V16.5L12 21L4 16.5V7.5ZM4 7.5L12 12L20 7.5M12 12V21"],
    "palette": ["design", "M12 3A9 9 0 0 0 12 21A2.5 2.5 0 0 0 14 17A2 2 0 0 1 16 15H18A3 3 0 0 0 21 12A9 9 0 0 0 12 3ZM7.5 11L7.51 11M10 7L10.01 7M14.5 7L14.51 7M17.5 10.5L17.51 10.5"],
    "paperclip": ["actions", "M20 11L12 19A5 5 0 0 1 5 12L13.5 3.5A3.3 3.3 0 0 1 18.2 8.2L9.7 16.7A1.7 1.7 0 0 1 7.3 14.3L15 6.6"],
    "pause": ["media", "M7 5H9A1 1 0 0 1 10 6V18A1 1 0 0 1 9 19H7A1 1 0 0 1 6 18V6A1 1 0 0 1 7 5ZM15 5H17A1 1 0 0 1 18 6V18A1 1 0 0 1 17 19H15A1 1 0 0 1 14 18V6A1 1 0 0 1 15 5Z"],
    "phone": ["comms", "M5 4H9L11 9L8.5 10.5A11 11 0 0 0 13.5 15.5L15 13L20 15V19A1.5 1.5 0 0 1 18.5 20.5C10 20 4 14 3.5 5.5A1.5 1.5 0 0 1 5 4Z"],
    "pin": ["status", "M9 4H15L14 10L17 13H7L10 10ZM12 13V21"],
    "play": ["media", "M7 4.5L19 12L7 19.5Z"],
    "plus": ["actions", "M12 5V19M5 12H19"],
    "plus-circle": ["actions", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM12 8V16M8 12H16"],
    "power": ["actions", "M12 3V12M7 6.5A8 8 0 1 0 17 6.5"],
    "printer": ["actions", "M7 8V3H17V8M7 17H5A1 1 0 0 1 4 16V9A1 1 0 0 1 5 8H19A1 1 0 0 1 20 9V16A1 1 0 0 1 19 17H17M7 14H17V21H7Z"],
    "qr": ["actions", "M4 3H9A1 1 0 0 1 10 4V9A1 1 0 0 1 9 10H4A1 1 0 0 1 3 9V4A1 1 0 0 1 4 3ZM15 3H20A1 1 0 0 1 21 4V9A1 1 0 0 1 20 10H15A1 1 0 0 1 14 9V4A1 1 0 0 1 15 3ZM4 14H9A1 1 0 0 1 10 15V20A1 1 0 0 1 9 21H4A1 1 0 0 1 3 20V15A1 1 0 0 1 4 14ZM14 14H17V17M21 14V14.01M14 21H17M21 17V21M17 21H21"],
    "redo": ["arrows", "M15 14L20 9L15 4M20 9H9A5 5 0 0 0 9 19H14"],
    "refresh": ["arrows", "M17.14 5.87A8 8 0 1 1 9.26 4.48M7.48 7.84L9.26 4.48L5.74 3.06"],
    "repeat": ["media", "M4 11V10A3 3 0 0 1 7 7H19M16 4L19 7L16 10M20 13V14A3 3 0 0 1 17 17H5M8 14L5 17L8 20"],
    "rewind": ["media", "M21 6L13 12L21 18ZM12 6L4 12L12 18Z"],
    "rocket": ["dev", "M12 3C16 5 18 9 17 14L12 19L7 14C6 9 8 5 12 3ZM10.2 10A1.8 1.8 0 1 0 13.8 10A1.8 1.8 0 1 0 10.2 10ZM7.4 13L4.5 15.5V19L9.5 16.5M16.6 13L19.5 15.5V19L14.5 16.5M12 19.5V22"],
    "rotate": ["arrows", "M6.86 5.87A8 8 0 1 0 14.74 4.48M18.26 3.06L14.74 4.48L16.52 7.84"],
    "save": ["actions", "M6 4H16L20 8V18A2 2 0 0 1 18 20H6A2 2 0 0 1 4 18V6A2 2 0 0 1 6 4ZM8 4V9H15V4M8 20V14H16V20"],
    "search": ["actions", "M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20"],
    "send": ["actions", "M21 3L3 10.5L10 13.5L13 21ZM10 13.5L21 3"],
    "settings": ["actions", "M10.33 4.79L10.8 2.48L13.2 2.48L13.67 4.79L15.92 5.72L17.88 4.41L19.59 6.12L18.28 8.08L19.21 10.33L21.52 10.8L21.52 13.2L19.21 13.67L18.28 15.92L19.59 17.88L17.88 19.59L15.92 18.28L13.67 19.21L13.2 21.52L10.8 21.52L10.33 19.21L8.08 18.28L6.12 19.59L4.41 17.88L5.72 15.92L4.79 13.67L2.48 13.2L2.48 10.8L4.79 10.33L5.72 8.08L4.41 6.12L6.12 4.41L8.08 5.72ZM9.2 12A2.8 2.8 0 1 0 14.8 12A2.8 2.8 0 1 0 9.2 12Z"],
    "share": ["actions", "M3.5 12A2.5 2.5 0 1 0 8.5 12A2.5 2.5 0 1 0 3.5 12ZM15.5 6A2.5 2.5 0 1 0 20.5 6A2.5 2.5 0 1 0 15.5 6ZM15.5 18A2.5 2.5 0 1 0 20.5 18A2.5 2.5 0 1 0 15.5 18ZM8.2 10.8L15.8 7.2M8.2 13.2L15.8 16.8"],
    "share-out": ["actions", "M9 10H6V20H18V10H15M12 15V4M8 8L12 4L16 8"],
    "shield": ["security", "M12 3L20 6V12C20 16.5 16.5 19.5 12 21C7.5 19.5 4 16.5 4 12V6Z"],
    "shield-check": ["security", "M12 3L20 6V12C20 16.5 16.5 19.5 12 21C7.5 19.5 4 16.5 4 12V6ZM8.5 12L11 14.5L15.5 9.5"],
    "shuffle": ["media", "M3 7H6C12 7 12 17 18 17H20M17 14L20 17L17 20M3 17H6C8 17 9.4 15.7 10.4 14.4M13.6 9.6C14.6 8.3 16 7 18 7H20M17 4L20 7L17 10"],
    "sidebar": ["charts", "M5 4H19A2 2 0 0 1 21 6V18A2 2 0 0 1 19 20H5A2 2 0 0 1 3 18V6A2 2 0 0 1 5 4ZM9 4V20"],
    "skip-next": ["media", "M6 5L15 12L6 19ZM18 5V19"],
    "skip-prev": ["media", "M18 5L9 12L18 19ZM6 5V19"],
    "sliders": ["actions", "M4 7H12M16 7H20M4 17H8M12 17H20M12 7A2 2 0 1 0 16 7A2 2 0 1 0 12 7ZM8 17A2 2 0 1 0 12 17A2 2 0 1 0 8 17Z"],
    "smile": ["status", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM8 14A5 5 0 0 0 16 14M9 9.5L9.01 9.5M15 9.5L15.01 9.5"],
    "sort": ["arrows", "M7 4V20M3 8L7 4L11 8M17 20V4M13 16L17 20L21 16"],
    "sparkles": ["status", "M12 3C12.5 8 14 10.5 19 12C14 13.5 12.5 16 12 21C11.5 16 10 13.5 5 12C10 10.5 11.5 8 12 3ZM19 3V7M17 5H21"],
    "square": ["charts", "M6 4H18A2 2 0 0 1 20 6V18A2 2 0 0 1 18 20H6A2 2 0 0 1 4 18V6A2 2 0 0 1 6 4Z"],
    "star": ["status", "M12 3L14.47 9.1L21.04 9.56L15.99 13.8L17.58 20.19L12 16.7L6.42 20.19L8.01 13.8L2.96 9.56L9.53 9.1Z"],
    "stop": ["media", "M7 5H17A2 2 0 0 1 19 7V17A2 2 0 0 1 17 19H7A2 2 0 0 1 5 17V7A2 2 0 0 1 7 5Z"],
    "sun": ["design", "M8 12A4 4 0 1 0 16 12A4 4 0 1 0 8 12ZM19 12L21.5 12M16.95 16.95L18.72 18.72M12 19L12 21.5M7.05 16.95L5.28 18.72M5 12L2.5 12M7.05 7.05L5.28 5.28M12 5L12 2.5M16.95 7.05L18.72 5.28"],
    "table": ["charts", "M5 4H19A2 2 0 0 1 21 6V18A2 2 0 0 1 19 20H5A2 2 0 0 1 3 18V6A2 2 0 0 1 5 4ZM3 9.5H21M3 14.5H21M9.5 9.5V20"],
    "tag": ["status", "M4 4H12L21 13L13 21L4 12ZM8.5 8.5L8.51 8.5"],
    "target": ["status", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM7 12A5 5 0 1 0 17 12A5 5 0 1 0 7 12ZM12 12L12.01 12"],
    "terminal": ["dev", "M5 4H19A2 2 0 0 1 21 6V18A2 2 0 0 1 19 20H5A2 2 0 0 1 3 18V6A2 2 0 0 1 5 4ZM7 9L11 12L7 15M13 16H17"],
    "thumbs-up": ["status", "M7 11V20H4V11ZM7 11L11 3A2 2 0 0 1 14 5L13 9H19A2 2 0 0 1 21 11.4L19.8 18.4A2 2 0 0 1 17.8 20H7"],
    "toggle-off": ["actions", "M7 7H17A5 5 0 0 1 22 12V12A5 5 0 0 1 17 17H7A5 5 0 0 1 2 12V12A5 5 0 0 1 7 7ZM5 12A2.5 2.5 0 1 0 10 12A2.5 2.5 0 1 0 5 12Z"],
    "toggle-on": ["actions", "M7 7H17A5 5 0 0 1 22 12V12A5 5 0 0 1 17 17H7A5 5 0 0 1 2 12V12A5 5 0 0 1 7 7ZM14 12A2.5 2.5 0 1 0 19 12A2.5 2.5 0 1 0 14 12Z"],
    "trash": ["actions", "M4 7H20M9 7V4H15V7M6 7L7 20H17L18 7M10 11V16M14 11V16"],
    "trending-down": ["charts", "M3 7L9 13L13 9L21 17M15 17H21V11"],
    "trending-up": ["charts", "M3 17L9 11L13 15L21 7M15 7H21V13"],
    "triangle": ["charts", "M12 4L21 19H3Z"],
    "trophy": ["status", "M7 4H17V10A5 5 0 0 1 7 10ZM7 6H4V8A3 3 0 0 0 7 11M17 6H20V8A3 3 0 0 1 17 11M12 15V18M8 21H16M9.5 18H14.5"],
    "type": ["design", "M5 7V5H19V7M12 5V19M9 19H15"],
    "underline": ["design", "M6 4V11A6 6 0 0 0 18 11V4M4 21H20"],
    "undo": ["arrows", "M9 14L4 9L9 4M4 9H15A5 5 0 0 1 15 19H10"],
    "unlock": ["security", "M7 11H17A2 2 0 0 1 19 13V19A2 2 0 0 1 17 21H7A2 2 0 0 1 5 19V13A2 2 0 0 1 7 11ZM8 11V8A4 4 0 0 1 15.5 6.5M12 16L12.01 16"],
    "upload": ["actions", "M12 15V4M7 9L12 4L17 9M5 20H19"],
    "user": ["users", "M8 8A4 4 0 1 0 16 8A4 4 0 1 0 8 8ZM4 20C4 16 7.5 14 12 14C16.5 14 20 16 20 20"],
    "user-circle": ["users", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM9 10A3 3 0 1 0 15 10A3 3 0 1 0 9 10ZM6.5 18.5C8 16 10 15 12 15C14 15 16 16 17.5 18.5"],
    "user-plus": ["users", "M6 8A4 4 0 1 0 14 8A4 4 0 1 0 6 8ZM3 20C3 16.5 6 14.5 10 14.5C13 14.5 15 15.5 16 17M19 8V14M16 11H22"],
    "users": ["users", "M5.5 8A3.5 3.5 0 1 0 12.5 8A3.5 3.5 0 1 0 5.5 8ZM2.5 19C2.5 16 5.5 14.5 9 14.5C12.5 14.5 15.5 16 15.5 19M15 4.6A3.5 3.5 0 0 1 15 11.4M17.5 14.7C20 15.3 21.5 16.8 21.5 19"],
    "video": ["media", "M5 6H14A2 2 0 0 1 16 8V16A2 2 0 0 1 14 18H5A2 2 0 0 1 3 16V8A2 2 0 0 1 5 6ZM16 10.5L21 7.5V16.5L16 13.5"],
    "view": ["dev", "M5 4H19A2 2 0 0 1 21 6V18A2 2 0 0 1 19 20H5A2 2 0 0 1 3 18V6A2 2 0 0 1 5 4ZM3 9H21M7 14H13M7 17H10"],
    "volume": ["media", "M4 9.5H8L13 5V19L8 14.5H4ZM16.5 9A4 4 0 0 1 16.5 15M19 6.5A8 8 0 0 1 19 17.5"],
    "volume-off": ["media", "M4 9.5H8L13 5V19L8 14.5H4ZM17 9.5L22 14.5M22 9.5L17 14.5"],
    "wallet": ["dev", "M4 7A2 2 0 0 1 6 5H17V8M4 7V18A2 2 0 0 0 6 20H20V8H6A2 2 0 0 1 4 7ZM16 14L16.01 14"],
    "warning": ["status", "M12 4L21 20H3ZM12 10V14M12 17L12.01 17"],
    "wifi": ["comms", "M2.5 9A14 14 0 0 1 21.5 9M5.5 12.5A9.5 9.5 0 0 1 18.5 12.5M8.5 16A5 5 0 0 1 15.5 16M12 19.5L12.01 19.5"],
    "x-circle": ["actions", "M3 12A9 9 0 1 0 21 12A9 9 0 1 0 3 12ZM9 9L15 15M15 9L9 15"],
    "zap": ["status", "M13 3L5 14H11L10 21L19 9.5H13Z"],
    "zoom-in": ["actions", "M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20M10.5 8V13M8 10.5H13"],
    "zoom-out": ["actions", "M4 10.5A6.5 6.5 0 1 0 17 10.5A6.5 6.5 0 1 0 4 10.5ZM15.5 15.5L20 20M8 10.5H13"]
};

let __icAlias = {
    "add": "plus",
    "alert": "warning",
    "analytics": "chart-line",
    "apps": "grid",
    "attach": "paperclip",
    "back": "arrow-left",
    "bin": "trash",
    "bolt": "zap",
    "bookmarks": "bookmark",
    "brightness": "sun",
    "cancel": "close",
    "chat": "message",
    "cog": "settings",
    "comment": "message",
    "danger": "error",
    "dark": "moon",
    "dashboard": "layout",
    "date": "calendar",
    "debug": "bug",
    "delete": "trash",
    "disk": "save",
    "dots": "more",
    "down": "arrow-down",
    "email": "mail",
    "exit": "logout",
    "favorite": "star",
    "find": "search",
    "fn": "function",
    "gear": "settings",
    "hamburger": "menu",
    "hide": "eye-off",
    "idea": "lightbulb",
    "internet": "wifi",
    "json": "braces",
    "kebab": "more-vertical",
    "lib": "library",
    "light": "sun",
    "light-bulb": "lightbulb",
    "lightning": "zap",
    "like": "thumbs-up",
    "location": "map-pin",
    "love": "heart",
    "magic": "sparkles",
    "magnifier": "search",
    "microphone": "mic",
    "movie": "film",
    "mute": "volume-off",
    "next": "arrow-right",
    "notification": "bell",
    "ok": "check",
    "open": "external-link",
    "password": "key",
    "pencil": "edit",
    "person": "user",
    "photo": "image",
    "picture": "image",
    "pkg": "package",
    "play-circle": "play",
    "preferences": "settings",
    "privacy": "shield",
    "profile": "user-circle",
    "question": "help",
    "reload": "refresh",
    "remove": "minus",
    "show": "eye",
    "signin": "login",
    "sound": "volume",
    "sparkle": "sparkles",
    "stats": "chart-bar",
    "sync": "refresh",
    "time": "clock",
    "ui": "view",
    "up": "arrow-up",
    "window": "view",
    "world": "globe",
    "x": "close"
};

let __icCats = [
    ["actions", "إجراءات وواجهة", "Actions & UI"],
    ["arrows", "أسهم وتنقّل", "Arrows & navigation"],
    ["status", "حالة وتنبيه", "Status & alerts"],
    ["security", "أمان", "Security"],
    ["users", "مستخدمون", "Users"],
    ["comms", "تواصل وشبكة", "Communication & network"],
    ["media", "وسائط", "Media"],
    ["files", "ملفات وبيانات", "Files & data"],
    ["dev", "مطوّرون وRin", "Developer & Rin"],
    ["charts", "مخططات وتخطيط", "Charts & layout"],
    ["design", "تصميم ونص", "Design & text"]
];

// ---- 1) الفهرس ----------------------------------------------------------------------------
fun ic_version() { return __ic_version; }
fun ic_count() { return len(keys(__ic)); }
fun ic_names() { return sort(keys(__ic)); }

// ic_resolve(name) -> الاسم الأساسي (يقبل الأسماء البديلة وأي حالة أحرف) أو nil إن لم يوجد.
fun ic_resolve(name) {
    if (name == nil) { return nil; }
    let n = lower(trim(str(name)));
    if (has(__ic, n)) { return n; }
    if (has(__icAlias, n)) { return __icAlias[n]; }
    return nil;
}
fun ic_has(name) { return ic_resolve(name) != nil; }

fun ic_aliasesOf(name) {
    let n = ic_resolve(name);
    let out = [];
    if (n == nil) { return out; }
    let ks = sort(keys(__icAlias));
    let i = 0;
    while (i < len(ks)) {
        if (__icAlias[ks[i]] == n) { push(out, ks[i]); }
        i = i + 1;
    }
    return out;
}

fun ic_category(name) {
    let n = ic_resolve(name);
    if (n == nil) { return nil; }
    return __ic[n][0];
}

// ---- 2) الفئات ----------------------------------------------------------------------------
fun ic_categories() {
    let out = [];
    let i = 0;
    while (i < len(__icCats)) { push(out, __icCats[i][0]); i = i + 1; }
    return out;
}
// ic_categoryName(cat, lang) -> الاسم المعروض ("ar" أو "en")؛ يعيد المعرّف نفسه إن لم تكن الفئة معروفة.
fun ic_categoryName(cat, lang) {
    let i = 0;
    while (i < len(__icCats)) {
        if (__icCats[i][0] == cat) {
            if (lang == "en") { return __icCats[i][2]; }
            return __icCats[i][1];
        }
        i = i + 1;
    }
    return cat;
}
fun ic_byCategory(cat) {
    let out = [];
    let ks = ic_names();
    let i = 0;
    while (i < len(ks)) {
        if (__ic[ks[i]][0] == cat) { push(out, ks[i]); }
        i = i + 1;
    }
    return out;
}

// ---- 3) البحث -----------------------------------------------------------------------------
// ic_search(q) -> أسماء أساسية يحوي اسمها أو أحد أسمائها البديلة النص q (بلا حساسية لحالة الأحرف).
fun ic_search(q) {
    let out = [];
    let needle = lower(trim(str(q)));
    // مرّة واحدة على الأسماء البديلة (لا مرور كامل لكل أيقونة): الأيقونات التي أحد بدائلها يطابق
    let viaAlias = {};
    if (needle != "") {
        let aks = keys(__icAlias);
        let k = 0;
        while (k < len(aks)) {
            if (contains(aks[k], needle)) { viaAlias[__icAlias[aks[k]]] = true; }
            k = k + 1;
        }
    }
    let ks = ic_names();
    let i = 0;
    while (i < len(ks)) {
        let hit = needle == "" or contains(ks[i], needle) or contains(__ic[ks[i]][0], needle) or has(viaAlias, ks[i]);
        if (hit) { push(out, ks[i]); }
        i = i + 1;
    }
    return out;
}

// ---- 4) التوليد ---------------------------------------------------------------------------
fun __ic_opt(opts, key, fallback) {
    if (opts == nil) { return fallback; }
    if (has(opts, key)) {
        let v = opts[key];
        if (v != nil) { return v; }
    }
    return fallback;
}
fun __ic_attr(s) {
    // تهريب قيم الخصائص داخل علامات الاقتباس
    return replace(replace(replace(replace(str(s), "&", "&amp;"), "\"", "&quot;"), "<", "&lt;"), ">", "&gt;");
}

// ic_path(name) -> قيمة d للمسار ("" إن لم توجد الأيقونة).
fun ic_path(name) {
    let n = ic_resolve(name);
    if (n == nil) { return ""; }
    return __ic[n][1];
}

// ic_svg(name, opts) -> وسم <svg> كامل. الاسم المجهول يعطي أيقونة "help" (علامة استفهام مرئية)
// بدل فراغ صامت — نفس فلسفة سجل الأيقونات في المحرّك (rin_indsin_icons.h). تحقّق بـ ic_has قبلها.
fun ic_svg(name, opts) {
    let n = ic_resolve(name);
    let missing = n == nil;
    if (missing) { n = "help"; }
    let size = __ic_opt(opts, "size", 24);
    let color = __ic_opt(opts, "color", "currentColor");
    let stroke = __ic_opt(opts, "stroke", 2);
    let filled = __ic_opt(opts, "filled", false);
    let title = __ic_opt(opts, "title", nil);
    let cls = __ic_opt(opts, "class", nil);
    let fill = "none";
    if (filled) { fill = __ic_attr(color); }
    let s = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"" + str(size) + "\" height=\"" + str(size) + "\"";
    s = s + " fill=\"" + fill + "\" stroke=\"" + __ic_attr(color) + "\" stroke-width=\"" + str(stroke) + "\"";
    s = s + " stroke-linecap=\"round\" stroke-linejoin=\"round\"";
    if (cls != nil) { s = s + " class=\"" + __ic_attr(cls) + "\""; }
    if (missing) { s = s + " data-missing=\"" + __ic_attr(name) + "\""; }
    if (title != nil) { s = s + " role=\"img\" aria-label=\"" + __ic_attr(title) + "\">" + "<title>" + __ic_attr(title) + "</title>"; }
    else { s = s + " aria-hidden=\"true\">"; }
    return s + "<path d=\"" + __ic[n][1] + "\"/></svg>";
}
// ic_svgn(name, size, color) -> اختصار للحالة الشائعة.
fun ic_svgn(name, size, color) { return ic_svg(name, {"size": size, "color": color}); }

// ic_html(name, opts) -> <span> مضمَّن بحجم نصّي (1em) يرث لون الخط؛ مناسب لـ html= في WebView.
fun ic_html(name, opts) {
    let size = __ic_opt(opts, "size", 24);
    let o = {"size": size, "color": __ic_opt(opts, "color", "currentColor"), "stroke": __ic_opt(opts, "stroke", 2),
             "filled": __ic_opt(opts, "filled", false), "title": __ic_opt(opts, "title", nil), "class": nil};
    return "<span style=\"display:inline-flex;vertical-align:middle;width:" + str(size) + "px;height:" + str(size) + "px\">" + ic_svg(name, o) + "</span>";
}

// ic_dataUri(name, opts) -> رابط data: يصلح لـ src= أو background-image (اللون يجب أن يكون صريحاً لا currentColor).
fun ic_dataUri(name, opts) {
    let s = ic_svg(name, opts);
    s = replace(s, "%", "%25");
    s = replace(s, "#", "%23");
    s = replace(s, "<", "%3C");
    s = replace(s, ">", "%3E");
    s = replace(s, "\"", "'");
    return "data:image/svg+xml;charset=utf-8," + s;
}

// ic_symbol(name) -> <symbol id="ic-NAME"> واحد؛ ic_sprite(names) -> sprite لمجموعة أسماء (أو كل الأيقونات إن مرّرت nil).
fun ic_symbol(name) {
    let n = ic_resolve(name);
    if (n == nil) { return ""; }
    return "<symbol id=\"ic-" + n + "\" viewBox=\"0 0 24 24\"><path d=\"" + __ic[n][1] + "\"/></symbol>";
}
fun ic_sprite(names) {
    let list = names;
    if (list == nil) { list = ic_names(); }
    let s = "<svg xmlns=\"http://www.w3.org/2000/svg\" style=\"display:none\" aria-hidden=\"true\">";
    let i = 0;
    while (i < len(list)) { s = s + ic_symbol(list[i]); i = i + 1; }
    return s + "</svg>";
}

// ---- 5) تكامل indsin ----------------------------------------------------------------------
// ic_snippet(name, opts) -> سطر indsin جاهز:  @view.Icon icon="home" size="lg"
// opts (اختياري): size ("sm"/"lg"/رقم) · color
fun ic_snippet(name, opts) {
    let n = ic_resolve(name);
    if (n == nil) { n = str(name); }
    let s = "@view.Icon=icon_" + replace(n, "-", "_") + "\n    icon=\"" + n + "\";";
    let size = __ic_opt(opts, "size", nil);
    if (size != nil) {
        if (type(size) == "string") { s = s + "\n    size=\"" + size + "\";"; } else { s = s + "\n    size=" + str(size) + ";"; }
    }
    let color = __ic_opt(opts, "color", nil);
    if (color != nil) { s = s + "\n    color=\"" + str(color) + "\";"; }
    return s + "\n.end/view";
}

// ic_audit(names) -> {ok, known:[...], unknown:[...]}  يفحص قائمة أسماء أيقونات قبل استعمالها في واجهة.
fun ic_audit(names) {
    let known = [];
    let unknown = [];
    let i = 0;
    while (i < len(names)) {
        if (ic_has(names[i])) { push(known, ic_resolve(names[i])); } else { push(unknown, names[i]); }
        i = i + 1;
    }
    return {"ok": len(unknown) == 0, "known": known, "unknown": unknown};
}

// ---- 6) التوسعة ---------------------------------------------------------------------------
// ic_validPath(d) -> true إن كان المسار مبنياً من الأوامر المدعومة (M L H V C A Z) والأرقام فقط.
fun ic_validPath(d) {
    if (type(d) != "string" or len(d) < 3) { return false; }
    return regexTest(d, "^[MLHVCAZ0-9 .,\\-]+$") and regexTest(d, "^M");
}

// ic_register(name, category, d) -> الاسم المسجَّل. أيقونتك تدخل البحث والفئات مثل المدمجة؛
// إعادة تسجيل اسم موجود تستبدله (آخر كتابة تفوز)، كما في registerIcon في سجل المحرّك.
fun ic_register(name, category, d) {
    let n = lower(trim(str(name)));
    if (!regexTest(n, "^[a-z][a-z0-9-]{0,47}$")) { fail("ic_register: اسم غير صالح " + json.stringify(name) + " (أحرف صغيرة وأرقام و - فقط)"); }
    if (!ic_validPath(d)) { fail("ic_register: مسار SVG غير صالح للأيقونة " + n); }
    __ic[n] = [category, d];
    return n;
}
)INDSINICONSOGRIN";

static const char* kLib_previewkit_og_rin = R"PREVIEWKITOGRIN(// ============================================================================
//  lib/previewkit.og.rin — PreviewKit: تشغيل ومعاينة حيّة لملفات PDF / Markdown / TypeScript
//
//  استيراد:
//    @import "lib/previewkit.og.rin";
//    @import "lib/previewkit.og.rin" as pv;
//
//  مكتبة Rin خالصة (بلا JavaScript/Kotlin داخلها) فوق natives موجودة في المفسّر:
//    readFile/writeFile/fileExists، fs.hash (كشف التغيّر)، automation.sleepMs (حلقة المراقبة)،
//    regexFindAll/regexGroups/regexReplace (محلّلات سريعة)، zlibInflate (فك ضغط PDF — جديد)،
//    utf8Len/detectScript/jsonDecode. تُخرج صفحة HTML كاملة قياسية تُعرض في WebView/متصفح.
//
//  الأنواع المدعومة (يُكشف النوع من الامتداد، وPDF أيضاً من التوقيع "%PDF-"):
//    • Markdown (.md/.markdown)  → HTML عبر relyRIN + فهرس عناوين جانبي وإحصاءات
//    • PDF (.pdf)                → استخراج النص فعلياً (تدفّقات Flate، object streams، خطوط Type0 +
//                                  ToUnicode كالعربية، ترميز WinAnsi) + بيانات الوثيقة + تبويب
//                                  "الأصل" يعرض الـ PDF نفسه عبر عارض المتصفح عند تمرير embedSrc
//    • TypeScript/JavaScript (.ts .tsx .mts .cts .js .jsx .mjs .cjs) و .json
//                                → تلوين صياغي حقيقي، هيكل الملف (class/interface/type/enum/function/
//                                  const + أعضاء الأصناف)، قائمة الاستيرادات، وفحص أقواس/نصوص غير مغلقة
//    • أي ملف نصي آخر (.rin .kt .java .c .cpp .py .go .rs .css .html .xml .yml .sh ...) → عرض كود ملوّن
//
//  الاستخدام السريع:
//    let r = pvPreview("notes.md", "notes.preview.html", {live: true});   // يكتب الصفحة
//    print r;                                                             // {ok, kind, output, bytes, ms, stamp}
//    pvLive("main.ts", "main.preview.html", {intervalMs: 800, maxTicks: 300});  // معاينة حيّة بالمراقبة
//
//  الحلقة الحيّة تعمل بطريقتين معاً: (1) pvLive/pvTick يراقبان بصمة الملف ويعيدان توليد HTML عند أي
//  تعديل، (2) الصفحة الناتجة بخيار live:true تُحدّث نفسها (meta refresh + حفظ موضع التمرير) فتظهر
//  التعديلات في العارض دون تدخّل.
//
//  ملاحظة لغوية: لا نفي بـ "!"؛ المقارنات الصريحة x == false، كبقية lib/*.og.rin.
//  حدود صريحة: لا تشغيل لشيفرة TypeScript (لا يوجد native لتنفيذ عمليات خارجية)؛ المعاينة = تلوين +
//  هيكل + فحص. استخراج PDF يعيد النص لا الرسم: للمعاينة البصرية استخدم تبويب "الأصل".
// ============================================================================

@import "lib/relyRIN.og.rin";

// ---- 1) أدوات عامة ----------------------------------------------------------

fun pvEsc(s) { return relyEscape(toString(s)); }

fun pvBaseName(path) {
    let p = replace(path, "\\", "/");
    let i = lastIndexOf(p, "/");
    if (i < 0) { return p; }
    return substr(p, i + 1);
}

fun pvExt(path) {
    let b = pvBaseName(path);
    let i = lastIndexOf(b, ".");
    if (i < 0) { return ""; }
    return lower(substr(b, i + 1));
}

fun pvStartsWith(s, p) {
    if (len(s) < len(p)) { return false; }
    return substr(s, 0, len(p)) == p;
}

// عدد مرّات ظهور ch في s
fun pvCountChar(s, ch) {
    return len(s) - len(replace(s, ch, ""));
}

fun pvFmtBytes(n) {
    if (n < 1024) { return toString(n) + " B"; }
    if (n < 1048576) { return toString(round(n / 102.4) / 10) + " KB"; }
    return toString(round(n / 104857.6) / 10) + " MB";
}

// ---- 2) كشف النوع ------------------------------------------------------------

fun pvKindOfExt(ext) {
    if (ext == "md" or ext == "markdown" or ext == "mdown" or ext == "mkd") { return "markdown"; }
    if (ext == "pdf") { return "pdf"; }
    if (ext == "ts" or ext == "tsx" or ext == "mts" or ext == "cts") { return "typescript"; }
    if (ext == "js" or ext == "jsx" or ext == "mjs" or ext == "cjs") { return "javascript"; }
    if (ext == "json" or ext == "jsonc" or ext == "webmanifest") { return "json"; }
    return "code";
}

// النوع من المسار + (اختياري) بايتات الملف: توقيع %PDF- يغلب الامتداد
fun pvDetect(path, bytes) {
    if (type(bytes) == "string" and len(bytes) >= 5) {
        if (substr(bytes, 0, 5) == "%PDF-") { return "pdf"; }
    }
    return pvKindOfExt(pvExt(path));
}

fun pvSupported() {
    return {
        "markdown": ["md", "markdown", "mdown", "mkd"],
        "pdf": ["pdf"],
        "typescript": ["ts", "tsx", "mts", "cts"],
        "javascript": ["js", "jsx", "mjs", "cjs"],
        "json": ["json", "jsonc", "webmanifest"],
        "code": ["rin", "kt", "java", "c", "cpp", "h", "go", "rs", "py", "sh", "css", "html", "xml", "yml", "yaml", "toml", "..."]
    };
}

// ---- 3) النصوص (عربي/إنجليزي) -------------------------------------------------

fun pvLabels(lang) {
    if (lang == "en") {
        return {
            "file": "File", "kind": "Type", "size": "Size", "lines": "Lines", "words": "Words",
            "pages": "Pages", "outline": "Outline", "imports": "Imports", "problems": "Problems",
            "noProblems": "No problems found", "original": "Original", "text": "Extracted text",
            "live": "Live", "toc": "Contents", "reading": "min read", "encrypted": "This PDF is encrypted; text cannot be extracted.",
            "noText": "No extractable text on this page (scanned image or unsupported font).",
            "info": "Document", "title": "Title", "author": "Author", "producer": "Producer",
            "created": "Created", "images": "Images", "fonts": "Fonts", "version": "Version",
            "page": "Page", "tooBig": "File too large for syntax highlighting; shown as plain text.",
            "missing": "File not found", "empty": "(empty file)", "chars": "Characters",
            "functions": "Functions", "classes": "Types", "unmapped": "unmapped glyphs",
            "invalidJson": "Invalid JSON", "validJson": "Valid JSON"
        };
    }
    return {
        "file": "الملف", "kind": "النوع", "size": "الحجم", "lines": "الأسطر", "words": "الكلمات",
        "pages": "الصفحات", "outline": "هيكل الملف", "imports": "الاستيرادات", "problems": "المشاكل",
        "noProblems": "لا توجد مشاكل", "original": "الأصل", "text": "النص المستخرج",
        "live": "حيّ", "toc": "المحتويات", "reading": "د للقراءة", "encrypted": "ملف PDF مشفّر؛ تعذّر استخراج النص.",
        "noText": "لا نص قابل للاستخراج في هذه الصفحة (صورة ممسوحة أو خط غير مدعوم).",
        "info": "الوثيقة", "title": "العنوان", "author": "المؤلف", "producer": "المُنتِج",
        "created": "تاريخ الإنشاء", "images": "الصور", "fonts": "الخطوط", "version": "الإصدار",
        "page": "صفحة", "tooBig": "الملف كبير جداً للتلوين؛ يُعرض كنص عادي.",
        "missing": "الملف غير موجود", "empty": "(ملف فارغ)", "chars": "الأحرف",
        "functions": "الدوال", "classes": "الأنواع", "unmapped": "رموز غير مُعيَّنة",
        "invalidJson": "JSON غير صالح", "validJson": "JSON صالح"
    };
}

fun pvKindLabel(kind, lang) {
    if (kind == "markdown") { return "Markdown"; }
    if (kind == "pdf") { return "PDF"; }
    if (kind == "typescript") { return "TypeScript"; }
    if (kind == "javascript") { return "JavaScript"; }
    if (kind == "json") { return "JSON"; }
    if (lang == "en") { return "Code"; }
    return "كود";
}

// ---- 4) الثيم والـ CSS ---------------------------------------------------------

fun pvThemeDefault() {
    let t = relyThemeDefault();
    t["lang"] = "ar";
    t["live"] = false;
    t["intervalSec"] = 2;
    t["codeFont"] = "ui-monospace, SFMono-Regular, 'Cascadia Code', Consolas, monospace";
    t["dark"] = "auto";
    return t;
}

fun pvTheme(overrides) {
    let t = pvThemeDefault();
    if (type(overrides) != "map") { return t; }
    let ks = keys(overrides);
    let i = 0;
    while (i < len(ks)) {
        t[ks[i]] = overrides[ks[i]];
        i = i + 1;
    }
    return t;
}

fun pvCss(t) {
    let acc = pvEsc(t["accent"]);
    let css = "<style>\n" +
    ":root{--bg:" + t["bg"] + ";--fg:" + t["text"] + ";--muted:" + t["muted"] + ";--accent:" + acc + ";--accent2:" + t["accent2"] + ";--bd:" + t["border"] + ";--code:" + t["codeBg"] + ";--card:" + t["cardBg"] + ";--link:" + t["link"] + ";--r:" + t["radius"] + ";" +
    "--k:#8250df;--ty:#0a7ea4;--s:#0a7d4a;--n:#b35900;--c:#6e7781;--f:#1f5fbf;--d:#bf3989;--p:#0550ae;--err:#cf222e;--ok:#1a7f37;}\n";
    let darkVars = "--bg:#0f1115;--fg:#e6e8eb;--muted:#9aa3ad;--bd:#272b33;--code:#161a21;--card:#14181f;--link:#6aa6ff;--k:#c4a0ff;--ty:#5ad1f0;--s:#7ee2a8;--n:#ffb86b;--c:#8b949e;--f:#79b8ff;--d:#ff8ac6;--p:#7cc0ff;--err:#ff7b72;--ok:#56d364;";
    if (t["dark"] == "auto") {
        css = css + "@media (prefers-color-scheme:dark){:root{" + darkVars + "}}\n";
    }
    if (t["dark"] == "on") {
        css = css + ":root{" + darkVars + "}\n";
    }
    css = css +
    "*{box-sizing:border-box}html,body{margin:0;padding:0;background:var(--bg);color:var(--fg)}\n" +
    "body{font-family:" + t["font"] + ";line-height:1.7}\n" +
    ".pv-top{position:sticky;top:0;z-index:5;display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;padding:10px 18px;background:var(--card);border-bottom:1px solid var(--bd)}\n" +
    ".pv-name{font-weight:700;font-size:1.02rem;direction:ltr;unicode-bidi:isolate}\n" +
    ".pv-badge{font-size:.78rem;padding:2px 10px;border-radius:999px;background:var(--accent);color:#fff}\n" +
    ".pv-meta{font-size:.82rem;color:var(--muted)}\n" +
    ".pv-live{margin-inline-start:auto;font-size:.78rem;color:var(--ok)}.pv-live:before{content:'';display:inline-block;width:8px;height:8px;border-radius:50%;background:var(--ok);margin-inline-end:6px;animation:pvp 1.4s infinite}\n" +
    "@keyframes pvp{50%{opacity:.25}}\n" +
    ".pv-wrap{display:flex;align-items:flex-start;gap:0;max-width:1400px;margin:0 auto}\n" +
    ".pv-side{position:sticky;top:52px;flex:0 0 260px;max-height:calc(100vh - 52px);overflow:auto;padding:18px 14px;border-inline-end:1px solid var(--bd);font-size:.88rem}\n" +
    ".pv-side h4{margin:0 0 8px;font-size:.78rem;letter-spacing:.04em;color:var(--muted)}\n" +
    ".pv-side a{display:block;color:var(--fg);text-decoration:none;padding:2px 6px;border-radius:6px}.pv-side a:hover{background:var(--code)}\n" +
    ".pv-side .l2{padding-inline-start:16px}.pv-side .l3{padding-inline-start:28px}.pv-side .l4{padding-inline-start:40px}\n" +
    ".pv-main{flex:1;min-width:0}\n" +
    ".pv-md{max-width:" + t["maxWidth"] + ";margin:0 auto;padding:28px 24px 72px}\n" +
    ".pv-md h1{font-size:2rem;line-height:1.2;margin:0 0 18px;padding-bottom:12px;border-bottom:2px solid var(--accent)}\n" +
    ".pv-md h2{font-size:1.5rem;margin-top:30px;padding-bottom:6px;border-bottom:1px solid var(--bd)}.pv-md h3{font-size:1.2rem;margin-top:24px}\n" +
    ".pv-md a{color:var(--link);text-decoration:none}.pv-md a:hover{text-decoration:underline}\n" +
    ".rely-code{background:var(--code);border:1px solid var(--bd);border-radius:var(--r);padding:14px;overflow:auto;direction:ltr;text-align:left;font-family:" + t["codeFont"] + ";font-size:.9em}\n" +
    ".rely-inline-code{background:var(--code);border-radius:6px;padding:2px 6px;font-family:" + t["codeFont"] + "}\n" +
    ".rely-quote{margin:16px 0;padding:10px 16px;border-inline-start:4px solid var(--accent);background:var(--code);border-radius:var(--r)}\n" +
    ".rely-table{width:100%;border-collapse:collapse;margin:16px 0}.rely-table th,.rely-table td{border:1px solid var(--bd);padding:8px 10px;text-align:start}.rely-table th{background:var(--code)}\n" +
    ".rely-hr{border:0;border-top:1px solid var(--bd);margin:26px 0}.rely-media{display:block;max-width:100%;margin:16px auto;border-radius:var(--r);overflow:hidden}.rely-media img,.rely-media video{display:block;width:100%;height:auto}\n" +
    ".rely-youtube{position:relative;width:100%;aspect-ratio:16/9;background:#000;border-radius:var(--r);overflow:hidden;margin:18px 0}.rely-youtube iframe{position:absolute;inset:0;width:100%;height:100%;border:0}\n" +
    ".rely-card{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:16px;margin:16px 0}.rely-task{list-style:none}\n" +
    ".pv-code{display:flex;direction:ltr;font-family:" + t["codeFont"] + ";font-size:.86rem;line-height:1.55;background:var(--code);border-bottom:1px solid var(--bd);overflow:auto}\n" +
    ".pv-code pre{margin:0;padding:14px 0}.pv-gut{color:var(--muted);text-align:right;padding:14px 12px!important;user-select:none;border-inline-end:1px solid var(--bd);position:sticky;left:0;background:var(--code)}\n" +
    ".pv-src{padding:14px 18px!important;flex:1;tab-size:4;white-space:pre}\n" +
    ".tk-k{color:var(--k)}.tk-t{color:var(--ty)}.tk-s{color:var(--s)}.tk-n{color:var(--n)}.tk-c{color:var(--c);font-style:italic}.tk-f{color:var(--f)}.tk-d{color:var(--d)}.tk-p{color:var(--p)}.tk-r{color:var(--d)}\n" +
    ".pv-chips{display:flex;flex-wrap:wrap;gap:6px;margin:6px 0 12px}.pv-chip{font-size:.74rem;padding:1px 8px;border-radius:999px;border:1px solid var(--bd);direction:ltr}\n" +
    ".pv-side a.pv-ol,.pv-ol{display:flex;gap:8px;align-items:baseline;direction:ltr;text-align:left;padding:1px 4px;border-radius:6px;font-size:.84rem}.pv-ol:hover{background:var(--code)}\n" +
    ".pv-ol i{font-style:normal;font-size:.68rem;color:#fff;border-radius:4px;padding:0 5px;background:var(--accent)}.pv-ol b{font-weight:600}.pv-ol em{margin-inline-start:auto;color:var(--muted);font-style:normal;font-size:.72rem}\n" +
    ".pv-ol.m{padding-inline-start:18px}.pv-ol.m i{background:var(--muted)}\n" +
    ".pv-diag{padding:10px 18px;font-size:.86rem;direction:ltr;text-align:left}.pv-diag .e{color:var(--err)}.pv-diag .ok{color:var(--ok)}\n" +
    ".pv-pdf{max-width:920px;margin:0 auto;padding:22px 20px 70px}\n" +
    ".pv-page{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);margin:0 0 18px;padding:18px 24px;box-shadow:0 1px 4px rgba(0,0,0,.05)}\n" +
    ".pv-pn{font-size:.75rem;color:var(--muted);margin-bottom:8px}.pv-page p{margin:2px 0}.pv-page p.gap{margin-top:14px}\n" +
    ".pv-empty{color:var(--muted);font-style:italic}.pv-info{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:8px;margin-bottom:18px}\n" +
    ".pv-info div{background:var(--card);border:1px solid var(--bd);border-radius:var(--r);padding:8px 12px;font-size:.84rem;overflow-wrap:anywhere}.pv-info small{display:block;color:var(--muted);font-size:.7rem}\n" +
    ".pv-tabs input{position:absolute;opacity:0;pointer-events:none}.pv-tabbar{display:flex;gap:6px;padding:12px 20px 0;border-bottom:1px solid var(--bd)}\n" +
    ".pv-tabbar label{cursor:pointer;padding:7px 16px;border-radius:10px 10px 0 0;font-size:.88rem;color:var(--muted)}\n" +
    "#pv-t1:checked~.pv-tabbar label[for=pv-t1],#pv-t2:checked~.pv-tabbar label[for=pv-t2]{background:var(--accent);color:#fff}\n" +
    ".pv-panel{display:none}#pv-t1:checked~.pv-panels .p1,#pv-t2:checked~.pv-panels .p2{display:block}.pv-orig{width:100%;height:calc(100vh - 110px);border:0;display:block}\n" +
    "@media(max-width:820px){.pv-wrap{display:block}.pv-side{position:static;max-height:none;border-inline-end:0;border-bottom:1px solid var(--bd)}}\n" +
    "</style>\n";
    return css;
}

// سكربت صغير يحفظ موضع التمرير بين تحديثات الصفحة الحيّة (لا يؤثر على غير الحيّة)
fun pvLiveScript() {
    return "<script>try{var k='pv:'+location.pathname;var y=sessionStorage.getItem(k);if(y){window.scrollTo(0,+y)}" +
        "addEventListener('scroll',function(){try{sessionStorage.setItem(k,String(scrollY))}catch(e){}})}catch(e){}</script>";
}

// الغلاف: شريط علوي + (اختياري) جانب + المحتوى. meta: مصفوفة نصوص صغيرة تُعرض بجانب الاسم.
fun pvShell(title, badge, meta, sideHtml, mainHtml, theme) {
    let t = pvTheme(theme);
    let lang = t["lang"];
    let lb = pvLabels(lang);
    let dir = "rtl";
    if (lang == "en") { dir = "ltr"; }
    let head = "<!doctype html><html lang=\"" + pvEsc(lang) + "\" dir=\"" + dir + "\"><head><meta charset=\"utf-8\">" +
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" + pvEsc(title) + " — PreviewKit</title>";
    if (t["live"] == true) {
        head = head + "<meta http-equiv=\"refresh\" content=\"" + toString(t["intervalSec"]) + "\">";
    }
    head = head + pvCss(t) + "</head><body>";
    let top = "<header class=\"pv-top\"><span class=\"pv-name\">" + pvEsc(title) + "</span><span class=\"pv-badge\">" + pvEsc(badge) + "</span>";
    let i = 0;
    while (i < len(meta)) {
        top = top + "<span class=\"pv-meta\">" + pvEsc(meta[i]) + "</span>";
        i = i + 1;
    }
    if (t["live"] == true) { top = top + "<span class=\"pv-live\">" + pvEsc(lb["live"]) + "</span>"; }
    top = top + "</header>";
    let body = "";
    if (sideHtml != "") {
        body = "<div class=\"pv-wrap\"><aside class=\"pv-side\">" + sideHtml + "</aside><main class=\"pv-main\">" + mainHtml + "</main></div>";
    } else {
        body = "<main class=\"pv-main\">" + mainHtml + "</main>";
    }
    let tail = "";
    if (t["live"] == true) { tail = pvLiveScript(); }
    return head + top + body + tail + "</body></html>";
}

fun pvErrorPage(path, message, theme) {
    let t = pvTheme(theme);
    let main = "<div class=\"pv-md\"><h1>" + pvEsc(message) + "</h1><p dir=\"ltr\"><code>" + pvEsc(path) + "</code></p></div>";
    return pvShell(pvBaseName(path), "!", [], "", main, theme);
}

// ---- 5) Markdown ---------------------------------------------------------------

// يضيف id="pv-hN" لكل <h1..h6> الناتجة عن relyRenderMarkdown (بالترتيب نفسه) لروابط الفهرس.
// عناوين الأكواد تخرج مُهرَّبة (&lt;h1&gt;) فلا تتأثر.
fun pvAddHeadingIds(html) {
    let parts = split(html, "<h");
    let out = [parts[0]];
    let n = 0;
    let i = 1;
    while (i < len(parts)) {
        let p = parts[i];
        let ok = false;
        if (len(p) >= 2) {
            let d = charAt(p, 0);
            if (indexOf("123456", d) >= 0 and charAt(p, 1) == ">") { ok = true; }
        }
        if (ok) {
            n = n + 1;
            push(out, substr(p, 0, 1) + " id=\"pv-h" + toString(n) + "\"" + substr(p, 1));
        } else {
            push(out, p);
        }
        i = i + 1;
    }
    return join(out, "<h");
}

// عناوين المستند [{level, title}] بنفس منطق relyRenderMarkdown (خارج أسوار ```)
fun pvMdHeadings(md) {
    let lines = relyLineArray(md);
    let res = [];
    let inCode = false;
    let i = 0;
    while (i < len(lines)) {
        let line = trim(lines[i]);
        if (pvStartsWith(line, "```")) {
            inCode = (inCode == false);
            i = i + 1;
            continue;
        }
        if (inCode) { i = i + 1; continue; }
        let level = relyHeadingLevel(line);
        if (level > 0 and level <= 6) {
            if (len(line) == level or charAt(line, level) == " ") {
                let title = relyStripHeading(line, level);
                title = regexReplace(title, "\\[([^\\]]*)\\]\\([^)]*\\)", "$1");
                title = regexReplace(title, "[*_`]", "");
                push(res, {"level": level, "title": trim(title)});
            }
        }
        i = i + 1;
    }
    return res;
}

fun pvMdWords(md) {
    return len(regexFindAll(md, "[^\\s]+"));
}

// يعيد {ok, kind:"markdown", html, headings, words, lines}
fun pvRenderMarkdown(md, theme, name) {
    let t = pvTheme(theme);
    let lb = pvLabels(t["lang"]);
    let heads = pvMdHeadings(md);
    let words = pvMdWords(md);
    let body = pvAddHeadingIds(relyRenderMarkdown(md));
    let side = "";
    if (len(heads) > 0) {
        let parts = ["<h4>" + pvEsc(lb["toc"]) + "</h4>"];
        let i = 0;
        while (i < len(heads)) {
            let h = heads[i];
            let lv = h["level"];
            if (lv > 4) { lv = 4; }
            push(parts, "<a class=\"l" + toString(lv) + "\" href=\"#pv-h" + toString(i + 1) + "\">" + pvEsc(h["title"]) + "</a>");
            i = i + 1;
        }
        side = join(parts, "");
    }
    let mins = ceil(words / 200);
    if (mins < 1) { mins = 1; }
    let lines = len(split(replace(md, "\r\n", "\n"), "\n"));
    let meta = [toString(words) + " " + lb["words"], toString(mins) + " " + lb["reading"], toString(len(heads)) + " #"];
    let main = "<article class=\"pv-md\" dir=\"auto\">" + body + "</article>";
    let html = pvShell(name, "Markdown", meta, side, main, theme);
    return {"ok": true, "kind": "markdown", "html": html, "headings": len(heads), "words": words, "lines": lines};
}

// ---- 6) المُرمِّز (tokenizer) للكود: TypeScript/JavaScript/JSON وعائلة C/Rin/Python... -------------

fun pvWordSet(list) {
    let m = {};
    let i = 0;
    while (i < len(list)) {
        m[list[i]] = true;
        i = i + 1;
    }
    return m;
}

fun pvKeywordsFor(lang) {
    if (lang == "typescript" or lang == "javascript") {
        return split("abstract as asserts async await break case catch class const continue debugger declare default delete do else enum export extends false finally for from function get if implements import in infer instanceof interface is keyof let namespace new null of override private protected public readonly return satisfies set static super switch this throw true try type typeof undefined var void while with yield module require unique accessor out global", " ");
    }
    if (lang == "json") { return ["true", "false", "null"]; }
    if (lang == "rin") {
        return split("fun let if else while for in return break continue print show true false nil and or class struct enum import export try recover finally match when otherwise container view element loop make create run set new this self extends implements", " ");
    }
    if (lang == "kt") {
        return split("fun val var if else when while for in return break continue class object interface enum data sealed open override abstract private public protected internal import package is as try catch finally throw null true false this super companion init by lateinit const suspend inline", " ");
    }
    if (lang == "py") {
        return split("def class if elif else while for in return break continue import from as try except finally raise with lambda pass yield None True False and or not is global nonlocal async await del assert", " ");
    }
    if (lang == "go") {
        return split("func var const type struct interface map chan if else for range switch case default return break continue go defer import package select nil true false fallthrough goto", " ");
    }
    if (lang == "rs") {
        return split("fn let mut const static struct enum impl trait use mod pub crate self Self super if else match while for in loop return break continue as ref move async await dyn where type unsafe true false", " ");
    }
    // C / C++ / Java / وغيرها
    return split("if else while for do switch case default return break continue class struct enum union interface public private protected static final const void new delete this null nullptr true false try catch finally throw throws import package using namespace template typename virtual override extends implements abstract inline extern typedef sizeof include define", " ");
}

fun pvTypesFor(lang) {
    if (lang == "typescript") {
        return split("string number boolean any unknown never object symbol bigint void null undefined Array Promise Record Partial Required Readonly Pick Omit Exclude Extract ReturnType Parameters Map Set WeakMap Date Error RegExp Function Object String Number Boolean", " ");
    }
    if (lang == "javascript") {
        return split("Array Promise Map Set WeakMap Date Error RegExp Function Object String Number Boolean Symbol BigInt JSON Math Intl", " ");
    }
    if (lang == "kt") { return split("Int Long Short Byte Float Double Boolean Char String Unit Any Nothing List Map Set Array", " "); }
    if (lang == "go") { return split("int int8 int16 int32 int64 uint uint8 uint16 uint32 uint64 float32 float64 string bool byte rune error any", " "); }
    if (lang == "rs") { return split("i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str String Vec Option Result Box", " "); }
    if (lang == "py") { return split("int float str bool list dict set tuple bytes object type", " "); }
    return split("int long short char float double bool boolean byte string String size_t uint8_t uint32_t int32_t int64_t", " ");
}

// تُحدَّد لغة التلوين من الامتداد
fun pvLangOfExt(ext) {
    if (ext == "ts" or ext == "tsx" or ext == "mts" or ext == "cts") { return "typescript"; }
    if (ext == "js" or ext == "jsx" or ext == "mjs" or ext == "cjs") { return "javascript"; }
    if (ext == "json" or ext == "jsonc" or ext == "webmanifest") { return "json"; }
    if (ext == "rin") { return "rin"; }
    if (ext == "kt" or ext == "kts") { return "kt"; }
    if (ext == "py") { return "py"; }
    if (ext == "go") { return "go"; }
    if (ext == "rs") { return "rs"; }
    return "c";
}

fun pvHashComments(lang) {
    return lang == "py";
}

// يُعيد نمط regex للتجزئة. ترتيب البدائل مهم (الأولى المطابقة تفوز):
// تعليق سطر، تعليق كتلة، نص "..."، نص '...'، قالب `...`، رقم، معرّف، مُزخرِف @x،
// [سياق + regex literal]، مسافات، أي رمز مفرد.
fun pvTokenRegex(hash) {
    let re = "";
    if (hash) { re = "#[^\\n]*|"; }
    re = re + "//[^\\n]*|/\\*[\\s\\S]*?\\*/|\"(?:\\\\.|[^\"\\\\\\n])*\"|'(?:\\\\.|[^'\\\\\\n])*'|`(?:\\\\[\\s\\S]|[^`\\\\])*`" +
        "|0[xX][0-9a-fA-F_]+n?|0[bB][01_]+n?|0[oO][0-7_]+n?|(?:\\d[\\d_]*\\.?[\\d_]*|\\.\\d[\\d_]*)(?:[eE][+-]?\\d+)?n?" +
        "|[A-Za-z_$][\\w$]*|@[A-Za-z_$][\\w$.]*" +
        "|[=(,:\\[!&|?{};]\\s*/(?![*/])(?:\\\\.|\\[(?:\\\\.|[^\\]\\\\\\n])*\\]|[^/\\\\\\n\\[])+/[a-z]*" +
        "|\\s+|\\S";
    return re;
}

// تصنيف أولي للرمز: c تعليق، s نص، t قالب، r regex، n رقم، i معرّف، d مُزخرِف، w مسافة، p رمز، u نص يونيكود
fun pvTokenize(src, lang) {
    let hash = pvHashComments(lang);
    let raw = regexFindAll(src, pvTokenRegex(hash));
    let tt = [];
    let ts = [];
    let tl = [];
    let line = 1;
    let i = 0;
    let n = len(raw);
    while (i < n) {
        let s = raw[i];
        let c = charAt(s, 0);
        let kind = "p";
        let o = ord(c);
        if (o >= 128) {
            // بايتات UTF-8: تُدمَج المتتالية في رمز واحد كي لا تُقسَم محارف متعددة البايتات بين أوسمة
            let last = len(tt) - 1;
            if (last >= 0) {
                if (tt[last] == "u") {
                    ts[last] = ts[last] + s;
                    i = i + 1;
                    continue;
                }
            }
            kind = "u";
        } else if (c == "/" and len(s) > 1 and (charAt(s, 1) == "/" or charAt(s, 1) == "*")) {
            kind = "c";
        } else if (c == "#" and hash) {
            kind = "c";
        } else if (c == "\"" or c == "'") {
            if (len(s) >= 2 and charAt(s, len(s) - 1) == c) { kind = "s"; } else { kind = "p"; }
        } else if (c == "`") {
            if (len(s) >= 2) { kind = "t"; } else { kind = "p"; }
        } else if (c == "@" and len(s) > 1) {
            kind = "d";
        } else if (indexOf("0123456789", c) >= 0) {
            kind = "n";
        } else if (c == "." and len(s) > 1) {
            kind = "n";
        } else if (c == "_" or c == "$" or (o >= 65 and o <= 90) or (o >= 97 and o <= 122)) {
            kind = "i";
        } else if (o <= 32) {
            kind = "w";
        } else if (len(s) > 1) {
            // [سياق + مسافات + /regex/flags]: يُفصَل إلى رمز سياق + مسافة + regex
            push(tt, "p"); push(ts, c); push(tl, line);
            let rest = substr(s, 1);
            let rr = trim(rest);
            let wsLen = len(rest) - len(rr);
            // trim يزيل الجانبين؛ regex لا ينتهي بمسافة فنحسب البادئة فقط
            let lead = 0;
            while (lead < len(rest) and ord(charAt(rest, lead)) <= 32) { lead = lead + 1; }
            if (lead > 0) {
                let wsTok = substr(rest, 0, lead);
                push(tt, "w"); push(ts, wsTok); push(tl, line);
                line = line + pvCountChar(wsTok, "\n");
            }
            push(tt, "r"); push(ts, substr(rest, lead)); push(tl, line);
            i = i + 1;
            continue;
        }
        push(tt, kind);
        push(ts, s);
        push(tl, line);
        if (kind == "c" or kind == "t" or kind == "w" or kind == "u") {
            line = line + pvCountChar(s, "\n");
        }
        i = i + 1;
    }
    return {"tt": tt, "ts": ts, "tl": tl};
}

// فهرس أقرب رمز مهمّ (ليس مسافة/تعليق) بعد/قبل الفهرس i؛ -1 إن لم يوجد
fun pvNextSig(tt, i) {
    let j = i + 1;
    while (j < len(tt)) {
        if (tt[j] != "w" and tt[j] != "c") { return j; }
        j = j + 1;
    }
    return -1;
}

// تصنيف ثانٍ للمعرّفات: k كلمة مفتاحية، t نوع، f استدعاء/تعريف دالة، C اسم صنف (PascalCase)، i عادي
fun pvClassifyIdents(tk, lang) {
    let kw = pvWordSet(pvKeywordsFor(lang));
    let ty = pvWordSet(pvTypesFor(lang));
    let tt = tk["tt"];
    let ts = tk["ts"];
    let cls = [];
    let n = len(tt);
    let i = 0;
    while (i < n) {
        let k = "";
        if (tt[i] == "i") {
            let s = ts[i];
            if (has(kw, s)) {
                k = "k";
            } else if (has(ty, s)) {
                k = "t";
            } else {
                let j = pvNextSig(tt, i);
                let nxt = "";
                if (j >= 0) { nxt = ts[j]; }
                let o = ord(charAt(s, 0));
                if (nxt == "(" and lang != "json") {
                    k = "f";
                } else if (o >= 65 and o <= 90 and len(s) > 1 and lang != "json") {
                    k = "t";
                }
            }
        } else if (tt[i] == "s" and lang == "json") {
            let j2 = pvNextSig(tt, i);
            if (j2 >= 0) {
                if (ts[j2] == ":") { k = "f"; }
            }
        } else if (tt[i] == "s" and (lang == "typescript" or lang == "javascript")) {
            // مفاتيح الكائنات "key": تُعرَض كخاصية
            let j3 = pvNextSig(tt, i);
            if (j3 >= 0) {
                if (ts[j3] == ":") { k = "p"; }
            }
        }
        push(cls, k);
        i = i + 1;
    }
    tk["cls"] = cls;
    return tk;
}

// ---- 7) هيكل ملف TypeScript/JavaScript + الاستيرادات + التشخيص (من الرموز لا من أسطر) -----------

fun pvIsModifier(s) {
    return s == "export" or s == "default" or s == "declare" or s == "abstract" or s == "async" or s == "public" or s == "private" or s == "protected" or s == "static" or s == "readonly" or s == "override" or s == "get" or s == "set" or s == "accessor";
}

// قائمة فهارس الرموز المهمّة فقط
fun pvSigIndexes(tt) {
    let sig = [];
    let i = 0;
    while (i < len(tt)) {
        if (tt[i] != "w" and tt[i] != "c") { push(sig, i); }
        i = i + 1;
    }
    return sig;
}

// هل بعد "=" عند الفهرس (في sig) تعبير دالة/سهم؟
fun pvLooksLikeFunctionInit(ts, tt, sig, k) {
    // k فهرس "=" داخل sig
    let j = k + 1;
    let depth = 0;
    let steps = 0;
    if (j >= len(sig)) { return false; }
    if (ts[sig[j]] == "async") { j = j + 1; }
    if (j >= len(sig)) { return false; }
    if (ts[sig[j]] == "function") { return true; }
    while (j < len(sig) and steps < 80) {
        let s = ts[sig[j]];
        let ty = tt[sig[j]];
        if (ty == "p") {
            if (s == "(" or s == "[" or s == "{") { depth = depth + 1; }
            if (s == ")" or s == "]" or s == "}") { depth = depth - 1; }
            if (depth < 0) { return false; }
            if (depth == 0 and s == ";") { return false; }
            if (depth == 0 and s == "=" and j + 1 < len(sig)) {
                if (ts[sig[j + 1]] == ">") { return true; }
            }
        }
        j = j + 1;
        steps = steps + 1;
    }
    return false;
}

// أول رمز غير مسافة بعد فهرس k في sig (نص فارغ إن لم يوجد)
fun pvSigText(ts, sig, k) {
    if (k < 0 or k >= len(sig)) { return ""; }
    return ts[sig[k]];
}

// يعيد {outline:[{kind,name,line,depth}], imports:[modules], problems:[{line,msg}], counts:{...}}
//   kind: class interface enum namespace type function const let var method property
//   depth: 0 = أعلى مستوى، 1 = عضو داخل class/interface
fun pvAnalyzeCode(tk, lang) {
    let tt = tk["tt"];
    let ts = tk["ts"];
    let tl = tk["tl"];
    let sig = pvSigIndexes(tt);
    let outline = [];
    let imports = [];
    let problems = [];
    let frames = [];       // أطر الأقواس المعقوفة: kind = block | class | iface | enum | ns
    let pstack = [];       // توازن ( [ { : {ch, line}
    let isCode = (lang == "typescript" or lang == "javascript");
    let pending = "";      // نوع الإطار الذي سيفتحه أقرب "{"
    let lastDecor = -100;
    let k = 0;
    while (k < len(sig)) {
        let idx = sig[k];
        let ty = tt[idx];
        let s = ts[idx];
        let line = tl[idx];

        // ---- تشخيصات الأقواس والنصوص غير المغلقة ----
        if (ty == "p") {
            if (s == "(" or s == "[" or s == "{") {
                push(pstack, {"ch": s, "line": line});
            } else if (s == ")" or s == "]" or s == "}") {
                if (len(pstack) == 0) {
                    push(problems, {"line": line, "msg": "Unmatched '" + s + "'"});
                } else {
                    let top = pstack[len(pstack) - 1];
                    pop(pstack);
                    let want = "(";
                    if (s == "]") { want = "["; }
                    if (s == "}") { want = "{"; }
                    if (top["ch"] != want) {
                        push(problems, {"line": line, "msg": "Mismatched '" + s + "' (opened '" + top["ch"] + "' at line " + toString(top["line"]) + ")"});
                    }
                }
            } else if (s == "\"" or s == "'" or s == "`") {
                push(problems, {"line": line, "msg": "Unterminated string literal (" + s + ")"});
            } else if (s == "/") {
                if (idx + 1 < len(tt)) {
                    if (tt[idx + 1] == "p" and ts[idx + 1] == "*") {
                        push(problems, {"line": line, "msg": "Unterminated block comment"});
                    }
                }
            }
        }

        if (isCode) {
            let top2 = "";
            if (len(frames) > 0) { top2 = frames[len(frames) - 1]["kind"]; }
            if (ty == "d") { lastDecor = k; }
            if (ty == "p" and s == "{") {
                if (pending != "") {
                    push(frames, {"kind": pending, "line": line});
                    pending = "";
                } else {
                    push(frames, {"kind": "block", "line": line});
                }
            } else if (ty == "p" and s == "}") {
                if (len(frames) > 0) { pop(frames); }
            } else if (ty == "p" and s == ";" and pending != "") {
                pending = "";
            } else if (ty == "i") {
                let atTop = (len(frames) == 0 or top2 == "ns");
                let prev = pvSigText(ts, sig, k - 1);
                let stmtStart = (k == 0 or prev == "{" or prev == ";" or prev == "}");
                if (atTop) {
                    if (s == "import") {
                        let m = k + 1;
                        let limit = 0;
                        while (m < len(sig) and limit < 80) {
                            if (tt[sig[m]] == "s") {
                                push(imports, substr(ts[sig[m]], 1, len(ts[sig[m]]) - 2));
                                break;
                            }
                            if (ts[sig[m]] == ";") { break; }
                            m = m + 1;
                            limit = limit + 1;
                        }
                    } else if (s == "class" or s == "interface" or s == "enum" or s == "namespace" or s == "module" or s == "function") {
                        let nm = k + 1;
                        if (pvSigText(ts, sig, nm) == "*") { nm = nm + 1; }
                        if (nm < len(sig)) {
                            let isName = (tt[sig[nm]] == "i" or tt[sig[nm]] == "s");
                            // "function"/"class" كتعبير (بعد = ( , : return) ليست تصريحاً
                            let isDecl = true;
                            if (prev == "=" or prev == "(" or prev == "," or prev == ":" or prev == "return" or prev == "?") { isDecl = false; }
                            if (s == "class" and prev == "<") { isDecl = false; }
                            if (isName and isDecl) {
                                let kindName = s;
                                if (s == "module") { kindName = "namespace"; }
                                push(outline, {"kind": kindName, "name": ts[sig[nm]], "line": line, "depth": 0});
                                if (s == "class") { pending = "class"; }
                                if (s == "interface") { pending = "iface"; }
                                if (s == "enum") { pending = "enum"; }
                                if (s == "namespace" or s == "module") { pending = "ns"; }
                            }
                        }
                    } else if (s == "type") {
                        let n1 = pvSigText(ts, sig, k + 1);
                        let n2 = pvSigText(ts, sig, k + 2);
                        if (k + 1 < len(sig)) {
                            if (tt[sig[k + 1]] == "i" and (n2 == "=" or n2 == "<")) {
                                push(outline, {"kind": "type", "name": n1, "line": line, "depth": 0});
                            }
                        }
                    } else if (s == "const" or s == "let" or s == "var") {
                        if (k + 1 < len(sig)) {
                            if (tt[sig[k + 1]] == "i") {
                                let scan = k + 2;
                                let lim = 0;
                                let found = -1;
                                let dd = 0;
                                while (scan < len(sig) and lim < 80) {
                                    let q = ts[sig[scan]];
                                    if (tt[sig[scan]] == "p") {
                                        if (q == "(" or q == "[" or q == "{" or q == "<") { dd = dd + 1; }
                                        if (q == ")" or q == "]" or q == "}" or q == ">") { dd = dd - 1; }
                                        if (dd == 0 and q == "=") { found = scan; break; }
                                        if (dd < 0 or (dd == 0 and q == ";")) { break; }
                                    }
                                    scan = scan + 1;
                                    lim = lim + 1;
                                }
                                let kindName2 = s;
                                if (found >= 0) {
                                    if (pvLooksLikeFunctionInit(ts, tt, sig, found)) { kindName2 = "function"; }
                                }
                                push(outline, {"kind": kindName2, "name": ts[sig[k + 1]], "line": line, "depth": 0});
                            }
                        }
                    }
                } else if (top2 == "class" or top2 == "iface") {
                    let decorated = (prev == ")" and k - lastDecor < 12);
                    if (stmtStart or decorated or tt[sig[k - 1]] == "d") {
                        // تخطّي المُعدِّلات: public static async ... ثم الاسم
                        let m2 = k;
                        let guard = 0;
                        while (m2 < len(sig) and guard < 8) {
                            let cur = ts[sig[m2]];
                            let nx = pvSigText(ts, sig, m2 + 1);
                            if (pvIsModifier(cur) and nx != "(" and nx != ":" and nx != "=" and nx != ";" and nx != "?" and nx != "<") {
                                m2 = m2 + 1;
                            } else {
                                break;
                            }
                            guard = guard + 1;
                        }
                        if (m2 < len(sig)) {
                            let star = 0;
                            if (ts[sig[m2]] == "*") { m2 = m2 + 1; }
                            if (m2 < len(sig)) {
                                if (tt[sig[m2]] == "i") {
                                    let nmText = ts[sig[m2]];
                                    let after = pvSigText(ts, sig, m2 + 1);
                                    if (after == "(" or after == "<") {
                                        push(outline, {"kind": "method", "name": nmText, "line": tl[sig[m2]], "depth": 1});
                                    } else if (after == ":" or after == "=" or after == "?" or after == "!" or after == ";") {
                                        push(outline, {"kind": "property", "name": nmText, "line": tl[sig[m2]], "depth": 1});
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        k = k + 1;
    }
    let ps = 0;
    while (ps < len(pstack)) {
        push(problems, {"line": pstack[ps]["line"], "msg": "Unclosed '" + pstack[ps]["ch"] + "'"});
        ps = ps + 1;
    }
    let nf = 0;
    let nc = 0;
    let oi = 0;
    while (oi < len(outline)) {
        let kd = outline[oi]["kind"];
        if (kd == "function" or kd == "method") { nf = nf + 1; }
        if (kd == "class" or kd == "interface" or kd == "enum" or kd == "type") { nc = nc + 1; }
        oi = oi + 1;
    }
    return {"outline": outline, "imports": imports, "problems": problems, "functions": nf, "types": nc};
}

// ---- 8) عرض الكود ---------------------------------------------------------------

fun pvKindTag(kind) {
    if (kind == "class") { return "class"; }
    if (kind == "interface") { return "iface"; }
    if (kind == "namespace") { return "ns"; }
    if (kind == "function") { return "fn"; }
    if (kind == "method") { return "fn"; }
    if (kind == "property") { return "prop"; }
    return kind;
}

fun pvHighlightHtml(tk) {
    let tt = tk["tt"];
    let ts = tk["ts"];
    let cls = tk["cls"];
    let parts = [];
    let i = 0;
    while (i < len(tt)) {
        let ty = tt[i];
        let s = relyEscape(ts[i]);
        let c = "";
        if (ty == "c") { c = "tk-c"; }
        else if (ty == "s" or ty == "t") { c = "tk-s"; }
        else if (ty == "r") { c = "tk-r"; }
        else if (ty == "n") { c = "tk-n"; }
        else if (ty == "d") { c = "tk-d"; }
        else if (ty == "i") {
            if (cls[i] == "k") { c = "tk-k"; }
            else if (cls[i] == "t") { c = "tk-t"; }
            else if (cls[i] == "f") { c = "tk-f"; }
        } else if (ty == "s" ) { c = "tk-s"; }
        if (ty == "s" and cls[i] == "p") { c = "tk-p"; }
        if (ty == "s" and cls[i] == "f") { c = "tk-p"; }
        if (c == "") { push(parts, s); }
        else { push(parts, "<span class=\"" + c + "\">" + s + "</span>"); }
        i = i + 1;
    }
    return join(parts, "");
}

fun pvGutter(lines) {
    let nums = [];
    let i = 1;
    while (i <= lines) {
        push(nums, toString(i));
        i = i + 1;
    }
    return join(nums, "\n");
}

fun pvOutlineHtml(an, lb) {
    let out = [];
    if (len(an["imports"]) > 0) {
        push(out, "<h4>" + pvEsc(lb["imports"]) + "</h4><div class=\"pv-chips\">");
        let i = 0;
        while (i < len(an["imports"]) and i < 60) {
            push(out, "<span class=\"pv-chip\">" + pvEsc(an["imports"][i]) + "</span>");
            i = i + 1;
        }
        push(out, "</div>");
    }
    push(out, "<h4>" + pvEsc(lb["outline"]) + "</h4>");
    if (len(an["outline"]) == 0) {
        push(out, "<div class=\"pv-meta\">—</div>");
    }
    let j = 0;
    while (j < len(an["outline"]) and j < 400) {
        let o = an["outline"][j];
        let cl = "pv-ol";
        if (o["depth"] > 0) { cl = "pv-ol m"; }
        push(out, "<a class=\"" + cl + "\" href=\"#L" + toString(o["line"]) + "\" style=\"text-decoration:none;color:inherit\"><i>" + pvEsc(pvKindTag(o["kind"])) + "</i><b>" + pvEsc(o["name"]) + "</b><em>" + toString(o["line"]) + "</em></a>");
        j = j + 1;
    }
    return join(out, "");
}

fun pvDiagHtml(problems, lb) {
    if (len(problems) == 0) {
        return "<div class=\"pv-diag\"><span class=\"ok\">✓ " + pvEsc(lb["noProblems"]) + "</span></div>";
    }
    let out = ["<div class=\"pv-diag\"><b>" + pvEsc(lb["problems"]) + " (" + toString(len(problems)) + ")</b>"];
    let i = 0;
    while (i < len(problems) and i < 50) {
        push(out, "<div class=\"e\">L" + toString(problems[i]["line"]) + ": " + pvEsc(problems[i]["msg"]) + "</div>");
        i = i + 1;
    }
    push(out, "</div>");
    return join(out, "");
}

// يعيد {ok, kind, html, lines, problems, outline, imports}
fun pvRenderCode(src, ext, kind, theme, name) {
    let t = pvTheme(theme);
    let lb = pvLabels(t["lang"]);
    let lang = pvLangOfExt(ext);
    if (kind == "typescript" or kind == "javascript") { lang = kind; }
    if (kind == "json") { lang = "json"; }
    let txt = replace(src, "\r\n", "\n");
    let lines = len(split(txt, "\n"));
    let notice = "";
    let an = {"outline": [], "imports": [], "problems": [], "functions": 0, "types": 0};
    let codeHtml = "";
    if (len(txt) > 400000) {
        codeHtml = relyEscape(txt);
        notice = "<div class=\"pv-diag\">" + pvEsc(lb["tooBig"]) + "</div>";
    } else {
        let tk = pvTokenize(txt, lang);
        tk = pvClassifyIdents(tk, lang);
        codeHtml = pvHighlightHtml(tk);
        an = pvAnalyzeCode(tk, lang);
    }
    if (lang == "json") {
        let probs = [];
        let trimmed = trim(txt);
        if (trimmed != "") {
            let parsed = jsonDecode(trimmed);
            let c0 = charAt(trimmed, 0);
            if (type(parsed) == "string" and (c0 == "{" or c0 == "[")) {
                probs = [{"line": 1, "msg": lb["invalidJson"]}];
            }
        }
        // مشاكل التوازن من المُحلّل تبقى، ونضيف حكم jsonDecode
        let ai = 0;
        while (ai < len(probs)) { push(an["problems"], probs[ai]); ai = ai + 1; }
    }
    let body = "";
    if (trim(txt) == "") {
        body = "<div class=\"pv-diag\">" + pvEsc(lb["empty"]) + "</div>";
    } else {
        body = notice + pvDiagHtml(an["problems"], lb) +
            "<div class=\"pv-code\"><pre class=\"pv-gut\">" + pvGutter(lines) + "</pre><pre class=\"pv-src\"><code>" + codeHtml + "</code></pre></div>";
    }
    let side = "";
    if (kind == "typescript" or kind == "javascript") { side = pvOutlineHtml(an, lb); }
    let meta = [toString(lines) + " " + lb["lines"], pvFmtBytes(len(src))];
    if (an["functions"] > 0) { push(meta, toString(an["functions"]) + " " + lb["functions"]); }
    if (an["types"] > 0) { push(meta, toString(an["types"]) + " " + lb["classes"]); }
    if (len(an["problems"]) > 0) { push(meta, toString(len(an["problems"])) + " " + lb["problems"]); }
    let html = pvShell(name, pvKindLabel(kind, t["lang"]), meta, side, body, theme);
    return {"ok": true, "kind": kind, "html": html, "lines": lines, "problems": len(an["problems"]),
            "outline": an["outline"], "imports": an["imports"], "functions": an["functions"], "types": an["types"]};
}

// ---- 9) PDF: محلّل أجسام + استخراج نص --------------------------------------------------------
//  يعمل على بايتات الملف مباشرة. الدعم: ملفات PDF 1.x بجداول xref أو xref streams، object streams،
//  تدفّقات FlateDecode (عبر native zlibInflate)، الصفحات بترتيب شجرة /Pages، موارد الخطوط الموروثة،
//  خطوط بسيطة (WinAnsi/Latin-1) وخطوط Type0 مع ToUnicode (CMap: bfchar/bfrange).
//  غير مدعوم (يُبلَّغ عنه بوضوح بدل التخمين): التشفير، LZW/ASCII85 كتدفّق وحيد، الصور الممسوحة (لا OCR).

fun pvHexDigit(c) {
    return indexOf("0123456789abcdef", lower(c));
}

fun pvHexToNum(h) {
    let n = 0;
    let i = 0;
    while (i < len(h)) {
        let d = pvHexDigit(charAt(h, i));
        if (d < 0) { d = 0; }
        n = n * 16 + d;
        i = i + 1;
    }
    return n;
}

// نقطة ترميز -> بايتات UTF-8 (chr يقبل 0..255 فقط)
fun pvUtf8(cp) {
    if (cp < 128) { return chr(cp); }
    if (cp < 2048) { return chr(192 + floor(cp / 64)) + chr(128 + cp % 64); }
    if (cp < 65536) { return chr(224 + floor(cp / 4096)) + chr(128 + floor(cp / 64) % 64) + chr(128 + cp % 64); }
    return chr(240 + floor(cp / 262144)) + chr(128 + floor(cp / 4096) % 64) + chr(128 + floor(cp / 64) % 64) + chr(128 + cp % 64);
}

// سلسلة سداسية عشرية لـ UTF-16BE -> UTF-8 (مع الأزواج البديلة surrogate)
fun pvUtf16HexToUtf8(hex) {
    let out = [];
    let i = 0;
    while (i + 4 <= len(hex)) {
        let u = pvHexToNum(substr(hex, i, 4));
        i = i + 4;
        if (u >= 55296 and u <= 56319 and i + 4 <= len(hex)) {
            let lo = pvHexToNum(substr(hex, i, 4));
            if (lo >= 56320 and lo <= 57343) {
                u = 65536 + (u - 55296) * 1024 + (lo - 56320);
                i = i + 4;
            }
        }
        if (u > 0) { push(out, pvUtf8(u)); }
    }
    return join(out, "");
}

fun pvCp1252Extra() {
    return [8364, 0, 8218, 402, 8222, 8230, 8224, 8225, 710, 8240, 352, 8249, 338, 0, 381, 0,
            0, 8216, 8217, 8220, 8221, 8226, 8211, 8212, 732, 8482, 353, 8250, 339, 0, 382, 376];
}

// بايت واحد من خط بسيط (WinAnsi ≈ Windows-1252) -> UTF-8
fun pvWinAnsi(b, extra) {
    if (b >= 32 and b < 127) { return chr(b); }
    if (b == 9 or b == 10 or b == 13) { return " "; }
    if (b >= 160) { return pvUtf8(b); }
    if (b >= 128 and b < 160) {
        let cp = extra[b - 128];
        if (cp > 0) { return pvUtf8(cp); }
    }
    return "";
}

// ---- فك سلاسل PDF النصية ----

fun pvPdfLiteral(tok) {
    // tok يشمل القوسين الخارجيين
    let inner = substr(tok, 1, len(tok) - 2);
    if (indexOf(inner, "\\") < 0) { return inner; }
    let out = [];
    let i = 0;
    let n = len(inner);
    while (i < n) {
        let c = charAt(inner, i);
        if (c != "\\") {
            push(out, c);
            i = i + 1;
            continue;
        }
        i = i + 1;
        if (i >= n) { break; }
        let d = charAt(inner, i);
        if (d == "n") { push(out, "\n"); i = i + 1; }
        else if (d == "r") { push(out, "\r"); i = i + 1; }
        else if (d == "t") { push(out, "\t"); i = i + 1; }
        else if (d == "b") { push(out, chr(8)); i = i + 1; }
        else if (d == "f") { push(out, chr(12)); i = i + 1; }
        else if (d == "\n") { i = i + 1; }
        else if (d == "\r") {
            i = i + 1;
            if (i < n) { if (charAt(inner, i) == "\n") { i = i + 1; } }
        }
        else if (indexOf("01234567", d) >= 0) {
            let v = 0;
            let cnt = 0;
            while (i < n and cnt < 3) {
                let o = indexOf("01234567", charAt(inner, i));
                if (o < 0) { break; }
                v = v * 8 + o;
                i = i + 1;
                cnt = cnt + 1;
            }
            push(out, chr(v % 256));
        }
        else { push(out, d); i = i + 1; }
    }
    return join(out, "");
}

fun pvPdfHexString(tok) {
    let h = regexReplace(substr(tok, 1, len(tok) - 2), "\\s", "");
    if (len(h) % 2 == 1) { h = h + "0"; }
    let out = [];
    let i = 0;
    while (i < len(h)) {
        push(out, chr(pvHexToNum(substr(h, i, 2))));
        i = i + 2;
    }
    return join(out, "");
}

// نص من قيمة سلسلة PDF (للبيانات الوصفية): دعم UTF-16BE بعلامة BOM
fun pvPdfTextValue(tok) {
    let raw = "";
    if (charAt(tok, 0) == "(") { raw = pvPdfLiteral(tok); } else { raw = pvPdfHexString(tok); }
    if (len(raw) >= 2) {
        if (ord(charAt(raw, 0)) == 254 and ord(charAt(raw, 1)) == 255) {
            let hex = [];
            let i = 2;
            while (i + 1 < len(raw)) {
                let hi = ord(charAt(raw, i));
                let lo = ord(charAt(raw, i + 1));
                push(hex, padStart(toHex(hi), 2, "0") + padStart(toHex(lo), 2, "0"));
                i = i + 2;
            }
            return pvUtf16HexToUtf8(join(hex, ""));
        }
    }
    // PDFDocEncoding ≈ Latin-1 لمعظم الحالات
    let out = [];
    let j = 0;
    while (j < len(raw)) {
        let b = ord(charAt(raw, j));
        if (b >= 128) { push(out, pvUtf8(b)); } else { push(out, charAt(raw, j)); }
        j = j + 1;
    }
    return join(out, "");
}

// ---- تحميل الأجسام ----

// يعيد {"ok", "objs": {"12": {"d": dictText, "s": rawStream|nil, "os": bool}}, "count"}
fun pvPdfLoad(data) {
    let objs = {};
    let chunks = split(data, "endobj");
    let i = 0;
    let count = 0;
    while (i < len(chunks)) {
        let ch = chunks[i];
        let m = regexGroups(ch, "(\\d+)\\s+(\\d+)\\s+obj");
        if (len(m) == 3) {
            let p = indexOf(ch, m[0]);
            let body = substr(ch, p + len(m[0]));
            let dict = body;
            let stream = nil;
            let sp = indexOf(body, "stream");
            if (sp >= 0) {
                let before = substr(body, 0, sp);
                // "stream" الحقيقية تلي نهاية القاموس ">>" (قد تفصلها مسافات)
                if (len(regexFindAll(before, ">>\\s*$")) > 0) {
                    dict = before;
                    let st = sp + 6;
                    let c1 = substr(body, st, 1);
                    if (c1 == "\r") {
                        st = st + 1;
                        if (substr(body, st, 1) == "\n") { st = st + 1; }
                    } else if (c1 == "\n") {
                        st = st + 1;
                    }
                    let rest = substr(body, st);
                    let ep = lastIndexOf(rest, "endstream");
                    if (ep >= 0) { stream = substr(rest, 0, ep); } else { stream = rest; }
                }
            }
            objs[m[1]] = {"d": dict, "s": stream, "os": false};
            count = count + 1;
        }
        i = i + 1;
    }
    return {"objs": objs, "count": count};
}

// ASCII85 (Adobe) -> بايتات. يقبل البادئة <~ واللاحقة ~> والمسافات. nil عند محرف غير صالح.
fun pvAscii85(s) {
    let src = regexReplace(s, "\\s", "");
    if (len(src) >= 2) { if (substr(src, 0, 2) == "<~") { src = substr(src, 2); } }
    let e = indexOf(src, "~>");
    if (e >= 0) { src = substr(src, 0, e); }
    let out = [];
    let grp = [];
    let i = 0;
    while (i < len(src)) {
        let c = charAt(src, i);
        i = i + 1;
        if (c == "z" and len(grp) == 0) {
            push(out, chr(0) + chr(0) + chr(0) + chr(0));
            continue;
        }
        let o = ord(c);
        if (o < 33 or o > 117) { return nil; }
        push(grp, o - 33);
        if (len(grp) == 5) {
            let v = (((grp[0] * 85 + grp[1]) * 85 + grp[2]) * 85 + grp[3]) * 85 + grp[4];
            push(out, chr(floor(v / 16777216) % 256) + chr(floor(v / 65536) % 256) + chr(floor(v / 256) % 256) + chr(v % 256));
            grp = [];
        }
    }
    let n = len(grp);
    if (n > 1) {
        let k = n;
        while (k < 5) { push(grp, 84); k = k + 1; }
        let v2 = (((grp[0] * 85 + grp[1]) * 85 + grp[2]) * 85 + grp[3]) * 85 + grp[4];
        let all4 = chr(floor(v2 / 16777216) % 256) + chr(floor(v2 / 65536) % 256) + chr(floor(v2 / 256) % 256) + chr(v2 % 256);
        push(out, substr(all4, 0, n - 1));
    }
    return join(out, "");
}

fun pvAsciiHex(s) {
    let h = regexReplace(s, "[\\s>]", "");
    return pvPdfHexString("<" + h + ">");
}

// أسماء الفلاتر بالترتيب: /Filter /A أو /Filter [/A /B]
fun pvPdfFilters(d) {
    let arr = regexGroups(d, "/Filter\\s*\\[([^\\]]*)\\]");
    let src = "";
    if (len(arr) == 2) { src = arr[1]; }
    else {
        let one = regexGroups(d, "/Filter\\s*(/[A-Za-z0-9]+)");
        if (len(one) == 2) { src = one[1]; }
    }
    return regexFindAll(src, "/[A-Za-z0-9]+");
}

// يفكّ تدفّق جسم عبر سلسلة فلاتره: nil إن كان فلتر غير مدعوم (LZW/RunLength/Crypt/صور DCT...) أو تالفاً
fun pvPdfStream(obj) {
    if (type(obj["s"]) != "string") { return nil; }
    let d = obj["d"];
    let data = obj["s"];
    let fl = pvPdfFilters(d);
    let i = 0;
    while (i < len(fl)) {
        let f = fl[i];
        if (f == "/FlateDecode" or f == "/Fl") {
            if (len(regexFindAll(d, "/Predictor\\s+(?:1[0-5])")) > 0) { return nil; }
            data = zlibInflate(data);
            if (type(data) != "string") { return nil; }
        } else if (f == "/ASCII85Decode" or f == "/A85") {
            data = pvAscii85(data);
            if (type(data) != "string") { return nil; }
        } else if (f == "/ASCIIHexDecode" or f == "/AHx") {
            data = pvAsciiHex(data);
        } else {
            return nil;
        }
        i = i + 1;
    }
    return data;
}

// يضيف أجسام /ObjStm (الأجسام المضغوطة داخل تدفّق) إلى objs
fun pvPdfExpandObjStms(objs) {
    let ks = keys(objs);
    let i = 0;
    while (i < len(ks)) {
        let o = objs[ks[i]];
        if (len(regexFindAll(o["d"], "/Type\\s*/ObjStm")) > 0) {
            let dec = pvPdfStream(o);
            if (type(dec) == "string") {
                let nm = regexGroups(o["d"], "/N\\s+(\\d+)");
                let fm = regexGroups(o["d"], "/First\\s+(\\d+)");
                if (len(nm) == 2 and len(fm) == 2) {
                    let n = toNumber(nm[1]);
                    let first = toNumber(fm[1]);
                    let nums = regexFindAll(substr(dec, 0, first), "\\d+");
                    let j = 0;
                    while (j < n and j * 2 + 1 < len(nums)) {
                        let onum = nums[j * 2];
                        let off = toNumber(nums[j * 2 + 1]);
                        let endOff = len(dec) - first;
                        if (j + 1 < n and (j + 1) * 2 + 1 < len(nums)) { endOff = toNumber(nums[(j + 1) * 2 + 1]); }
                        if (has(objs, onum) == false) {
                            objs[onum] = {"d": substr(dec, first + off, endOff - off), "s": nil, "os": true};
                        }
                        j = j + 1;
                    }
                }
            }
        }
        i = i + 1;
    }
    return objs;
}

// مراجع "N G R" داخل نص -> مصفوفة أرقام الأجسام (نصوص)
fun pvPdfRefs(src) {
    let all = regexFindAll(src, "\\d+\\s+\\d+\\s+R\\b");
    let out = [];
    let i = 0;
    while (i < len(all)) {
        push(out, regexGroups(all[i], "(\\d+)")[1]);
        i = i + 1;
    }
    return out;
}

fun pvPdfRefOf(dict, key) {
    let m = regexGroups(dict, "/" + key + "\\s+(\\d+)\\s+\\d+\\s+R\\b");
    if (len(m) == 2) { return m[1]; }
    return "";
}

// ---- CMap (ToUnicode) ----

fun pvHexTokens(s) {
    // "<0041> <0042>" -> ["0041","0042"]
    let all = regexFindAll(s, "<[0-9A-Fa-f\\s]*>");
    let out = [];
    let i = 0;
    while (i < len(all)) {
        push(out, regexReplace(substr(all[i], 1, len(all[i]) - 2), "\\s", ""));
        i = i + 1;
    }
    return out;
}

fun pvParseCMap(src, defaultLen) {
    let codeLen = defaultLen;
    let cs = regexGroups(src, "begincodespacerange\\s*<([0-9A-Fa-f]+)>");
    if (len(cs) == 2) { codeLen = floor(len(cs[1]) / 2); }
    if (codeLen < 1) { codeLen = 1; }
    let map = {};
    let ranges = [];
    let parts = split(src, "beginbfchar");
    let i = 1;
    while (i < len(parts)) {
        let sec = split(parts[i], "endbfchar")[0];
        let hx = pvHexTokens(sec);
        let j = 0;
        while (j + 1 < len(hx)) {
            map[toString(pvHexToNum(hx[j]))] = pvUtf16HexToUtf8(hx[j + 1]);
            j = j + 2;
        }
        i = i + 1;
    }
    let rparts = split(src, "beginbfrange");
    let r = 1;
    while (r < len(rparts)) {
        let sec2 = split(rparts[r], "endbfrange")[0];
        let items = regexFindAll(sec2, "<[0-9A-Fa-f\\s]*>\\s*<[0-9A-Fa-f\\s]*>\\s*(?:<[0-9A-Fa-f\\s]*>|\\[[^\\]]*\\])");
        let k = 0;
        while (k < len(items)) {
            let it = items[k];
            let hx2 = pvHexTokens(it);
            if (len(hx2) >= 3) {
                let lo = pvHexToNum(hx2[0]);
                let hi = pvHexToNum(hx2[1]);
                if (indexOf(it, "[") >= 0) {
                    let c = lo;
                    let q = 2;
                    while (c <= hi and q < len(hx2)) {
                        map[toString(c)] = pvUtf16HexToUtf8(hx2[q]);
                        c = c + 1;
                        q = q + 1;
                    }
                } else {
                    let dst = hx2[2];
                    let prefix = "";
                    let lastUnit = pvHexToNum(dst);
                    if (len(dst) > 4) {
                        prefix = substr(dst, 0, len(dst) - 4);
                        lastUnit = pvHexToNum(substr(dst, len(dst) - 4, 4));
                    }
                    if (hi - lo <= 512) {
                        let c2 = lo;
                        while (c2 <= hi) {
                            map[toString(c2)] = pvUtf16HexToUtf8(prefix + padStart(toHex(lastUnit + (c2 - lo)), 4, "0"));
                            c2 = c2 + 1;
                        }
                    } else {
                        push(ranges, [lo, hi, prefix, lastUnit]);
                    }
                }
            }
            k = k + 1;
        }
        r = r + 1;
    }
    return {"len": codeLen, "map": map, "ranges": ranges};
}

fun pvCMapLookup(cm, code) {
    let key = toString(code);
    if (has(cm["map"], key)) { return cm["map"][key]; }
    let i = 0;
    while (i < len(cm["ranges"])) {
        let rg = cm["ranges"][i];
        if (code >= rg[0] and code <= rg[1]) {
            return pvUtf16HexToUtf8(rg[2] + padStart(toHex(rg[3] + (code - rg[0])), 4, "0"));
        }
        i = i + 1;
    }
    return nil;
}

// المحتوى بين قوسين مربعين متوازنين يبدأ عند الفهرس start (حيث s[start] == "[")
fun pvBracketBlock(s, start) {
    let depth = 0;
    let i = start;
    let n = len(s);
    while (i < n) {
        let c = charAt(s, i);
        if (c == "[") { depth = depth + 1; }
        else if (c == "]") {
            depth = depth - 1;
            if (depth == 0) { return substr(s, start + 1, i - start - 1); }
        }
        i = i + 1;
    }
    return substr(s, start + 1);
}

// نص مصفوفة /Key [...] أو /Key N 0 R من قاموس (nil إن غابت)
fun pvPdfArrayValue(doc, d, key) {
    let inl = regexFindAll(d, "/" + key + "\\s*\\[");
    if (len(inl) > 0) {
        let p = indexOf(d, inl[0]);
        return pvBracketBlock(d, p + len(inl[0]) - 1);
    }
    let ref = pvPdfRefOf(d, key);
    if (ref != "" and has(doc["objs"], ref)) {
        let od = doc["objs"][ref]["d"];
        let p2 = indexOf(od, "[");
        if (p2 >= 0) { return pvBracketBlock(od, p2); }
    }
    return nil;
}

// عرض الرموز: خريطة code -> عرض (1/1000 em) + العرض الافتراضي
fun pvPdfWidths(doc, d, type0) {
    let w = {};
    let dw = 500;
    if (type0) {
        let desc = regexGroups(d, "/DescendantFonts\\s*\\[\\s*(\\d+)\\s+\\d+\\s+R");
        let dd = "";
        if (len(desc) == 2 and has(doc["objs"], desc[1])) { dd = doc["objs"][desc[1]]["d"]; }
        else {
            let dref = pvPdfRefOf(d, "DescendantFonts");
            if (dref != "" and has(doc["objs"], dref)) {
                let r2 = pvPdfRefs(doc["objs"][dref]["d"]);
                if (len(r2) > 0 and has(doc["objs"], r2[0])) { dd = doc["objs"][r2[0]]["d"]; }
            }
        }
        if (dd == "") { return {"w": w, "dw": 1000}; }
        let dwm = regexGroups(dd, "/DW\\s+(\\d+)");
        dw = 1000;
        if (len(dwm) == 2) { dw = toNumber(dwm[1]); }
        let wa = pvPdfArrayValue(doc, dd, "W");
        if (type(wa) == "string") {
            let toks = regexFindAll(wa, "\\[[^\\]]*\\]|-?\\d+\\.?\\d*");
            let i = 0;
            while (i < len(toks)) {
                let c0 = pvNum(toks[i]);
                if (type(c0) != "number") { i = i + 1; continue; }
                if (i + 1 >= len(toks)) { break; }
                let nx = toks[i + 1];
                if (charAt(nx, 0) == "[") {
                    let ws = regexFindAll(nx, "-?\\d+\\.?\\d*");
                    let k = 0;
                    while (k < len(ws)) {
                        w[toString(c0 + k)] = toNumber(ws[k]);
                        k = k + 1;
                    }
                    i = i + 2;
                } else {
                    if (i + 2 >= len(toks)) { break; }
                    let c1 = pvNum(nx);
                    let wv = pvNum(toks[i + 2]);
                    if (type(c1) == "number" and type(wv) == "number") {
                        let c = c0;
                        while (c <= c1 and c - c0 < 4096) {
                            w[toString(c)] = wv;
                            c = c + 1;
                        }
                    }
                    i = i + 3;
                }
            }
        }
        return {"w": w, "dw": dw};
    }
    let fc = regexGroups(d, "/FirstChar\\s+(\\d+)");
    let wa2 = pvPdfArrayValue(doc, d, "Widths");
    if (len(fc) == 2 and type(wa2) == "string") {
        let first = toNumber(fc[1]);
        let ws2 = regexFindAll(wa2, "-?\\d+\\.?\\d*");
        let k2 = 0;
        while (k2 < len(ws2)) {
            w[toString(first + k2)] = toNumber(ws2[k2]);
            k2 = k2 + 1;
        }
    }
    return {"w": w, "dw": dw};
}

// معلومات خط: {"cm": cmap|nil, "type0": bool}
fun pvPdfFontInfo(doc, fontNum, cache) {
    if (has(cache, fontNum)) { return cache[fontNum]; }
    let info = {"cm": nil, "type0": false, "w": {}, "dw": 500};
    if (has(doc["objs"], fontNum)) {
        let d = doc["objs"][fontNum]["d"];
        if (len(regexFindAll(d, "/Subtype\\s*/Type0")) > 0) { info["type0"] = true; }
        let wd = pvPdfWidths(doc, d, info["type0"]);
        info["w"] = wd["w"];
        info["dw"] = wd["dw"];
        if (info["type0"] == false) {
            // جدول سريع للأحرف ASCII المطبوعة: الحرف نفسه -> عرضه (مسار سريع في pvPdfDecodeShown)
            let wc = {};
            let ci = 32;
            while (ci < 127) {
                let key0 = toString(ci);
                if (has(wd["w"], key0)) { wc[chr(ci)] = wd["w"][key0]; } else { wc[chr(ci)] = wd["dw"]; }
                ci = ci + 1;
            }
            info["wc"] = wc;
        }
        let tu = pvPdfRefOf(d, "ToUnicode");
        if (tu != "" and has(doc["objs"], tu)) {
            let dec = pvPdfStream(doc["objs"][tu]);
            if (type(dec) == "string") {
                let dl = 1;
                if (info["type0"]) { dl = 2; }
                info["cm"] = pvParseCMap(dec, dl);
            }
        }
    }
    cache[fontNum] = info;
    return info;
}

// ---- موارد الصفحة (الخطوط) ----

// قاموس اسم الخط -> رقم الجسم، من نص موارد (قاموس مباشر أو مرجع)
fun pvPdfFontMap(doc, resText) {
    let fm = {};
    let fontDict = "";
    let inl = regexGroups(resText, "/Font\\s*<<([\\s\\S]*?)>>");
    if (len(inl) == 2) { fontDict = inl[1]; }
    else {
        let ref = pvPdfRefOf(resText, "Font");
        if (ref != "" and has(doc["objs"], ref)) { fontDict = doc["objs"][ref]["d"]; }
    }
    if (fontDict == "") { return fm; }
    let pairs = regexFindAll(fontDict, "/[^\\s/\\[\\]()<>]+\\s+\\d+\\s+\\d+\\s+R\\b");
    let i = 0;
    while (i < len(pairs)) {
        let nm = regexGroups(pairs[i], "(/[^\\s/\\[\\]()<>]+)\\s+(\\d+)\\s+\\d+\\s+R");
        if (len(nm) == 3) { fm[nm[1]] = nm[2]; }
        i = i + 1;
    }
    return fm;
}

// نص الموارد لصفحة: مباشر، أو مرجع، مع وراثة من الأب
fun pvPdfResources(doc, pageNum, depth) {
    if (depth > 12 or has(doc["objs"], pageNum) == false) { return ""; }
    let d = doc["objs"][pageNum]["d"];
    let ri = regexGroups(d, "/Resources\\s*(<<[\\s\\S]*>>)");
    let ref = pvPdfRefOf(d, "Resources");
    if (ref != "" and has(doc["objs"], ref)) { return doc["objs"][ref]["d"]; }
    if (len(ri) == 2) { return ri[1]; }
    let par = pvPdfRefOf(d, "Parent");
    if (par != "") { return pvPdfResources(doc, par, depth + 1); }
    return "";
}

// ---- ترتيب الصفحات ----

fun pvPdfCollectPages(doc, num, out, seen, depth) {
    if (depth > 40 or has(doc["objs"], num) == false) { return out; }
    if (has(seen, num)) { return out; }
    seen[num] = true;
    let d = doc["objs"][num]["d"];
    let isPages = len(regexFindAll(d, "/Type\\s*/Pages\\b")) > 0;
    let kids = regexGroups(d, "/Kids\\s*\\[([^\\]]*)\\]");
    if (isPages or len(kids) == 2) {
        if (len(kids) == 2) {
            let refs = pvPdfRefs(kids[1]);
            let i = 0;
            while (i < len(refs)) {
                out = pvPdfCollectPages(doc, refs[i], out, seen, depth + 1);
                i = i + 1;
            }
        }
        return out;
    }
    push(out, num);
    return out;
}

fun pvPdfPageList(doc, data) {
    let roots = regexFindAll(data, "/Root\\s+\\d+\\s+\\d+\\s+R");
    let pages = [];
    if (len(roots) > 0) {
        let rootNum = regexGroups(roots[len(roots) - 1], "/Root\\s+(\\d+)")[1];
        if (has(doc["objs"], rootNum)) {
            let pr = pvPdfRefOf(doc["objs"][rootNum]["d"], "Pages");
            if (pr != "") { pages = pvPdfCollectPages(doc, pr, [], {}, 0); }
        }
    }
    if (len(pages) == 0) {
        // احتياطي: كل جسم /Type /Page بترتيب رقمه
        let ks = keys(doc["objs"]);
        let nums = [];
        let i = 0;
        while (i < len(ks)) {
            if (len(regexFindAll(doc["objs"][ks[i]]["d"], "/Type\\s*/Page\\b")) > 0) { push(nums, toNumber(ks[i])); }
            i = i + 1;
        }
        nums = sort(nums);
        let j = 0;
        while (j < len(nums)) {
            push(pages, toString(nums[j]));
            j = j + 1;
        }
    }
    return pages;
}

// ---- استخراج نص صفحة من تدفّق المحتوى ----

fun pvPdfContentRegex() {
    return "\\((?:\\\\[\\s\\S]|[^\\\\()]|\\((?:\\\\[\\s\\S]|[^\\\\()])*\\))*\\)|<[0-9A-Fa-f\\s]*>|<<|>>|\\[|\\]|/[^\\s/\\[\\]()<>{}%]*|[-+]?(?:\\d+\\.?\\d*|\\.\\d+)|[A-Za-z'\"*]+";
}

fun pvAbs(x) {
    if (x < 0) { return 0 - x; }
    return x;
}

// يفكّ بايتات سلسلة معروضة حسب الخط -> {text, unmapped}
fun pvPdfDecodeShown(raw, finfo, extra) {
    let out = [];
    let unmapped = 0;
    let adv = 0;
    let n = len(raw);
    let cm = finfo["cm"];
    // مسار سريع: خط بسيط بلا ToUnicode ونص ASCII مطبوع فقط -> النص كما هو، والعرض من جدول الحرف
    if (type(cm) != "map" and finfo["type0"] == false and has(finfo, "wc")) {
        if (n > 0 and regexTest(raw, "^[ -~]+$")) {
            let wc = finfo["wc"];
            let cs = utf8ToArray(raw);
            let a2 = 0;
            let k = 0;
            while (k < n) { a2 = a2 + wc[cs[k]]; k = k + 1; }
            return {"text": raw, "unmapped": 0, "adv": a2};
        }
    }
    let wm = finfo["w"];
    let dw = finfo["dw"];
    let cl = 1;
    if (type(cm) == "map") { cl = cm["len"]; }
    else if (finfo["type0"]) { cl = 2; }
    let i = 0;
    while (i + cl <= n) {
        let code = 0;
        let q = 0;
        while (q < cl) {
            code = code * 256 + ord(charAt(raw, i + q));
            q = q + 1;
        }
        let key = toString(code);
        if (has(wm, key)) { adv = adv + wm[key]; } else { adv = adv + dw; }
        if (type(cm) == "map") {
            let u = pvCMapLookup(cm, code);
            if (type(u) == "string") { push(out, u); } else { unmapped = unmapped + 1; }
        } else if (finfo["type0"]) {
            unmapped = unmapped + 1;
        } else {
            push(out, pvWinAnsi(code, extra));
        }
        i = i + cl;
    }
    return {"text": join(out, ""), "unmapped": unmapped, "adv": adv};
}

// رقم من رمز PDF أو nil (toNumber يرمي خطأً على غير الأرقام)
fun pvNum(tok) {
    if (type(tok) != "string") { return nil; }
    if (regexTest(tok, "^[-+]?(?:\\d+\\.?\\d*|\\.\\d+)$") == false) { return nil; }
    let s = tok;
    if (substr(s, 0, 1) == "+") { s = substr(s, 1); }
    if (substr(s, len(s) - 1, 1) == ".") { s = s + "0"; }
    return toNumber(s);
}

// هل السطر يميني-يساري في الغالب؟ (عربي/عبري/سريانية بحسب بايت البداية في UTF-8)
fun pvIsRtlDominant(s) {
    let cs = utf8ToArray(s);
    let rtl = 0;
    let latin = 0;
    let i = 0;
    while (i < len(cs)) {
        let o = ord(cs[i]);
        if (o >= 214 and o <= 223) { rtl = rtl + 1; }
        else if ((o >= 65 and o <= 90) or (o >= 97 and o <= 122)) { latin = latin + 1; }
        i = i + 1;
    }
    return rtl > 0 and rtl >= latin;
}

// يعكس مصفوفة عناصر نصية (كل عنصر يبقى كما هو)
fun pvReverseParts(parts) {
    let out = [];
    let i = len(parts) - 1;
    while (i >= 0) {
        push(out, parts[i]);
        i = i - 1;
    }
    return out;
}

// وضع إنهاء السطر: 1 إن كان فيه ReversedChars، 2 إن فيه RTL بلا ActualText (رسم بصري)، وإلا 0
fun pvLineMode(parts, lineMode, hasAct) {
    if (lineMode == 1) { return 1; }
    if (pvHasRtl(join(parts, ""))) { return 2; }
    return 0;
}

fun pvIsAlnumByte(o) {
    return (o >= 48 and o <= 57) or (o >= 65 and o <= 90) or (o >= 97 and o <= 122);
}

// سطر بصري (رُسم يساراً->يميناً) فيه RTL: يُحوَّل إلى الترتيب المنطقي: عكس العناصر كلها ثم إعادة عكس
// مقاطع اللاتيني/الأرقام (بما بينها من مسافات/ترقيم داخلي) لتبقى مقروءة. العناصر الذرّية (عناقيد ActualText)
// تُعكَس كوحدة واحدة فلا تنقلب حروفها الداخلية.
fun pvVisualToLogical(parts) {
    let els = [];
    let i = 0;
    while (i < len(parts)) {
        let p = parts[i];
        if (len(p) > 0 and ord(charAt(p, 0)) == 1) { push(els, substr(p, 1)); }
        else {
            let cs = utf8ToArray(p);
            let k = 0;
            while (k < len(cs)) { push(els, cs[k]); k = k + 1; }
        }
        i = i + 1;
    }
    let rev = pvReverseParts(els);
    let out = [];
    let j = 0;
    let n = len(rev);
    while (j < n) {
        let e = rev[j];
        if (len(e) == 1 and pvIsAlnumByte(ord(e))) {
            let runEnd = j;
            let k2 = j;
            while (k2 < n) {
                let ek = rev[k2];
                if (len(ek) == 1 and pvIsAlnumByte(ord(ek))) { runEnd = k2; k2 = k2 + 1; }
                else if (len(ek) == 1 and indexOf(" .,:/_'-%", ek) >= 0) { k2 = k2 + 1; }
                else { break; }
            }
            let run = [];
            let m = runEnd;
            while (m >= j) { push(run, rev[m]); m = m - 1; }
            push(out, join(run, ""));
            j = runEnd + 1;
        } else {
            push(out, e);
            j = j + 1;
        }
    }
    return join(out, "");
}

// يُنهي سطراً. mode: 0 عادي، 1 ReversedChars (مقاطع معكوسة -> تُعكَس الكلمات)، 2 بصري خام (RTL بلا ActualText)
fun pvFinishLine(parts, mode) {
    if (mode == 2) {
        let s2 = pvVisualToLogical(parts);
        return trim(regexReplace(s2, "[ \\t]{2,}", " "));
    }
    let s = join(parts, "");
    s = replace(s, chr(1), "");
    s = trim(regexReplace(s, "[ \\t]{2,}", " "));
    if (mode == 0 or s == "") { return s; }
    if (pvIsRtlDominant(s) == false) { return s; }
    let words = split(s, " ");
    let out = [];
    let i = len(words) - 1;
    while (i >= 0) {
        let w = words[i];
        if (regexTest(w, "^(?:[.,;:!?)(]|،|؛|؟)+$") and len(out) > 0) {
            out[len(out) - 1] = out[len(out) - 1] + w;
        } else {
            push(out, w);
        }
        i = i - 1;
    }
    return join(out, " ");
}

// هل في النص محارف RTL (عربي/عبري/سريانية، وأشكال العرض FB50–FEFF)؟
fun pvHasRtl(s) {
    let cs = utf8ToArray(s);
    let i = 0;
    while (i < len(cs)) {
        let o = ord(cs[i]);
        if (o >= 214 and o <= 223) { return true; }
        if (o == 239 and len(cs[i]) >= 2) {
            let o2 = ord(charAt(cs[i], 1));
            if (o2 == 173 or o2 == 174 or o2 == 175 or o2 == 183 or o2 == 184 or o2 == 185 or o2 == 186 or o2 == 187 or o2 == 188 or o2 == 189) { return true; }
        }
        i = i + 1;
    }
    return false;
}

// يرتّب مقاطع سطر (كل مقطع {x0, x1, e, t, atom}) بحسب x ثم يبني النص المنطقي:
//  1) ترتيب بصري (يسار->يمين) مهما كان ترتيب الإصدار في التدفق
//  2) مسافة بين مقطعين متتاليين إن زادت الفجوة الفعلية (من عروض الخطوط) عن 0.18 em
//  3) سطر فيه RTL: عكس بصري->منطقي (يُعاد عكس مقاطع اللاتيني والأرقام)
fun pvFinishChunks(chunks) {
    let n = len(chunks);
    if (n == 0) { return ""; }
    // فرز إدراجي مستقر حسب x0
    let ord2 = [];
    let i = 0;
    while (i < n) {
        let c = chunks[i];
        let j = len(ord2);
        push(ord2, c);
        while (j > 0 and ord2[j - 1]["x0"] > c["x0"]) {
            ord2[j] = ord2[j - 1];
            j = j - 1;
        }
        ord2[j] = c;
        i = i + 1;
    }
    let parts = [];
    let all = [];
    let k = 0;
    while (k < n) {
        let cc = ord2[k];
        if (k > 0) {
            let pv = ord2[k - 1];
            let gap = cc["x0"] - pv["x1"];
            let endsSp = len(pv["t"]) > 0 and substr(pv["t"], len(pv["t"]) - 1, 1) == " ";
            let startsSp = len(cc["t"]) > 0 and substr(cc["t"], 0, 1) == " ";
            if (gap > pv["e"] * 0.18 and endsSp == false and startsSp == false) { push(parts, " "); push(all, " "); }
        }
        if (cc["atom"]) { push(parts, chr(1) + cc["t"]); } else { push(parts, cc["t"]); }
        push(all, cc["t"]);
        k = k + 1;
    }
    let s = "";
    if (pvHasRtl(join(all, ""))) { s = pvVisualToLogical(parts); }
    else { s = replace(join(parts, ""), chr(1), ""); }
    return trim(regexReplace(s, "[ \\t]{2,}", " "));
}

// يستخرج أسطر نص صفحة -> {"lines": [..], "unmapped": n}. سطر فارغ "" = فاصل فقرة.
//  • مصفوفة التحويل CTM + مصفوفة النص تُتابَع، فيُحدَّد السطر من y الفعلي في الجهاز
//  • عروض الخطوط (/Widths و/W) تُحسَب لموضع نهاية كل مقطع، فالمسافات من الفجوات لا من تخمين
//  • /ActualText يُفضَّل على فك الرموز (يعالج Chrome/Skia والعناقيد)، والـ RTL يُرتَّب بصرياً ثم يُحوَّل منطقياً
fun pvPdfPageText(doc, pageNum, fontCache) {
    let d = doc["objs"][pageNum]["d"];
    let refs = [];
    let cr = regexGroups(d, "/Contents\\s*\\[([^\\]]*)\\]");
    if (len(cr) == 2) { refs = pvPdfRefs(cr[1]); }
    else {
        let one = pvPdfRefOf(d, "Contents");
        if (one != "") {
            if (has(doc["objs"], one)) {
                let od = doc["objs"][one];
                if (type(od["s"]) != "string" and len(regexFindAll(od["d"], "^\\s*\\[")) > 0) { refs = pvPdfRefs(od["d"]); }
                else { refs = [one]; }
            }
        }
    }
    let content = [];
    let i = 0;
    while (i < len(refs)) {
        if (has(doc["objs"], refs[i])) {
            let dec = pvPdfStream(doc["objs"][refs[i]]);
            if (type(dec) == "string") { push(content, dec); }
        }
        i = i + 1;
    }
    let stream = join(content, "\n");
    let fm = pvPdfFontMap(doc, pvPdfResources(doc, pageNum, 0));
    let extra = pvCp1252Extra();
    let toks = regexFindAll(stream, pvPdfContentRegex());
    let lines = [];
    let chunks = [];
    let noFont = {"cm": nil, "type0": false, "w": {}, "dw": 500};
    let curFont = noFont;
    let fsz = 12;
    let sc = 1;
    let lastY = nil;
    let tlmX = 0;
    let tlmY = 0;
    let tmA = 1;
    let tmD = 1;
    let curX = 0;
    let ctm = [1, 0, 0, 1, 0, 0];
    let ctmStack = [];
    let operands = [];
    let inArr = false;
    let arr = [];
    let unmapped = 0;
    let mc = [];
    let actDepth = 0;
    let actChunk = nil;
    let inDict = false;
    let wantActual = false;
    let dictActual = nil;
    let n = len(toks);
    let t = 0;
    while (t < n) {
        let tok = toks[t];
        let c = charAt(tok, 0);
        t = t + 1;
        if (tok == "<<") { inDict = true; dictActual = nil; wantActual = false; continue; }
        if (tok == ">>") { inDict = false; continue; }
        if (inDict) {
            if (tok == "/ActualText") { wantActual = true; }
            else if (wantActual and (c == "(" or c == "<")) {
                dictActual = pvPdfTextValue(tok);
                wantActual = false;
            }
            continue;
        }
        if (tok == "[") { inArr = true; arr = []; continue; }
        if (tok == "]") {
            inArr = false;
            push(operands, arr);
            continue;
        }
        if (c == "(" or c == "<" or c == "/" or c == "-" or c == "+" or c == "." or indexOf("0123456789", c) >= 0) {
            if (inArr) { push(arr, tok); } else { push(operands, tok); }
            continue;
        }
        let op = tok;
        let no = len(operands);
        let hscale = pvAbs(ctm[0]);
        if (hscale == 0) { hscale = 1; }
        let vscale = pvAbs(ctm[3]);
        if (vscale == 0) { vscale = pvAbs(ctm[1]); }
        if (vscale == 0) { vscale = 1; }
        let esz = fsz * sc;
        if (op == "BMC" or op == "BDC") {
            let isAct = false;
            if (op == "BDC" and type(dictActual) == "string") {
                isAct = true;
                if (actDepth == 0) { actChunk = {"x0": nil, "x1": 0, "e": esz * hscale, "t": dictActual, "atom": true}; }
                actDepth = actDepth + 1;
            }
            push(mc, isAct);
            dictActual = nil;
        } else if (op == "EMC") {
            if (len(mc) > 0) {
                let wasAct = mc[len(mc) - 1];
                pop(mc);
                if (wasAct) {
                    actDepth = actDepth - 1;
                    if (actDepth == 0 and type(actChunk) == "map") {
                        if (actChunk["x0"] == nil) { actChunk["x0"] = curX; actChunk["x1"] = curX; }
                        push(chunks, actChunk);
                        actChunk = nil;
                    }
                }
            }
        } else if (op == "Tf") {
            if (no >= 2) {
                let fname = operands[no - 2];
                if (type(fname) == "string") {
                    if (has(fm, fname)) { curFont = pvPdfFontInfo(doc, fm[fname], fontCache); }
                    else { curFont = noFont; }
                }
                let sz = pvNum(operands[no - 1]);
                if (type(sz) == "number") { if (sz != 0) { fsz = pvAbs(sz); } }
            }
        } else if (op == "Tj" or op == "'" or op == "\"" or op == "TJ") {
            if (op == "'" or op == "\"") {
                if (len(chunks) > 0) { push(lines, pvFinishChunks(chunks)); chunks = []; }
            }
            // عناصر العرض: سلاسل أو (للـ TJ) سلاسل وتعديلات رقمية
            let items = [];
            if (no >= 1) {
                if (op == "TJ") { if (type(operands[no - 1]) == "array") { items = operands[no - 1]; } }
                else { items = [operands[no - 1]]; }
            }
            let q = 0;
            while (q < len(items)) {
                let e = items[q];
                q = q + 1;
                if (type(e) != "string") { continue; }
                let ec = charAt(e, 0);
                if (ec == "(" or ec == "<") {
                    let raw = "";
                    if (ec == "(") { raw = pvPdfLiteral(e); } else { raw = pvPdfHexString(e); }
                    let dr = pvPdfDecodeShown(raw, curFont, extra);
                    let x0 = curX;
                    curX = curX + dr["adv"] / 1000 * esz * hscale;
                    if (actDepth > 0) {
                        if (type(actChunk) == "map") {
                            if (actChunk["x0"] == nil) { actChunk["x0"] = x0; }
                            actChunk["x1"] = curX;
                        }
                    } else {
                        push(chunks, {"x0": x0, "x1": curX, "e": esz * hscale, "t": dr["text"], "atom": false});
                        unmapped = unmapped + dr["unmapped"];
                    }
                } else {
                    let adj = pvNum(e);
                    if (type(adj) == "number") { curX = curX - adj / 1000 * esz * hscale; }
                }
            }
        } else if (op == "BT") {
            tlmX = 0;
            tlmY = 0;
            tmA = 1;
            tmD = 1;
            sc = 1;
        } else if (op == "q") {
            push(ctmStack, [ctm[0], ctm[1], ctm[2], ctm[3], ctm[4], ctm[5]]);
        } else if (op == "Q") {
            if (len(ctmStack) > 0) {
                ctm = ctmStack[len(ctmStack) - 1];
                pop(ctmStack);
            }
        } else if (op == "cm") {
            if (no >= 6) {
                let m0 = pvNum(operands[no - 6]);
                let m1 = pvNum(operands[no - 5]);
                let m2 = pvNum(operands[no - 4]);
                let m3 = pvNum(operands[no - 3]);
                let m4 = pvNum(operands[no - 2]);
                let m5 = pvNum(operands[no - 1]);
                if (type(m0) == "number" and type(m1) == "number" and type(m2) == "number" and type(m3) == "number" and type(m4) == "number" and type(m5) == "number") {
                    ctm = [m0 * ctm[0] + m1 * ctm[2], m0 * ctm[1] + m1 * ctm[3],
                           m2 * ctm[0] + m3 * ctm[2], m2 * ctm[1] + m3 * ctm[3],
                           m4 * ctm[0] + m5 * ctm[2] + ctm[4], m4 * ctm[1] + m5 * ctm[3] + ctm[5]];
                }
            }
        } else if (op == "Td" or op == "TD" or op == "Tm") {
            let okPos = false;
            if (op == "Tm") {
                if (no >= 6) {
                    let ma = pvNum(operands[no - 6]);
                    let md = pvNum(operands[no - 3]);
                    let ne = pvNum(operands[no - 2]);
                    let nf = pvNum(operands[no - 1]);
                    if (type(ma) == "number" and type(md) == "number" and type(ne) == "number" and type(nf) == "number") {
                        tmA = ma;
                        tmD = md;
                        sc = pvAbs(ma);
                        if (sc == 0) { sc = pvAbs(md); }
                        if (sc == 0) { sc = 1; }
                        tlmX = ne;
                        tlmY = nf;
                        okPos = true;
                    }
                }
            } else if (no >= 2) {
                let dx = pvNum(operands[no - 2]);
                let dy = pvNum(operands[no - 1]);
                if (type(dx) == "number" and type(dy) == "number") {
                    tlmX = tlmX + dx * tmA;
                    tlmY = tlmY + dy * tmD;
                    okPos = true;
                }
            }
            if (okPos) {
                let esz2 = fsz * sc;
                let devX = ctm[0] * tlmX + ctm[2] * tlmY + ctm[4];
                let devY = ctm[1] * tlmX + ctm[3] * tlmY + ctm[5];
                if (lastY != nil and actDepth == 0) {
                    let dyAbs = pvAbs(devY - lastY);
                    if (dyAbs > esz2 * vscale * 0.6) {
                        if (len(chunks) > 0) { push(lines, pvFinishChunks(chunks)); chunks = []; }
                        if (dyAbs > esz2 * vscale * 1.9) { push(lines, ""); }
                    }
                }
                curX = devX;
                if (actDepth == 0) { lastY = devY; }
            }
        } else if (op == "T*") {
            if (len(chunks) > 0) { push(lines, pvFinishChunks(chunks)); chunks = []; }
        }
        operands = [];
    }
    if (len(chunks) > 0) { push(lines, pvFinishChunks(chunks)); }
    let clean = [];
    let li = 0;
    while (li < len(lines)) {
        let ln = trim(regexReplace(lines[li], " ((?:[.,;:!?)]|،|؛|؟))", "$1"));
        if (ln == "") {
            if (len(clean) > 0) {
                if (clean[len(clean) - 1] != "") { push(clean, ""); }
            }
        } else {
            push(clean, ln);
        }
        li = li + 1;
    }
    if (len(clean) > 0) {
        if (clean[len(clean) - 1] == "") { pop(clean); }
    }
    return {"lines": clean, "unmapped": unmapped};
}

// ---- بيانات الوثيقة ----

fun pvPdfMeta(doc, data) {
    let meta = {"title": "", "author": "", "producer": "", "created": "", "version": ""};
    let hv = regexGroups(substr(data, 0, 16), "%PDF-(\\d\\.\\d)");
    if (len(hv) == 2) { meta["version"] = hv[1]; }
    let infos = regexFindAll(data, "/Info\\s+\\d+\\s+\\d+\\s+R");
    if (len(infos) > 0) {
        let inum = regexGroups(infos[len(infos) - 1], "/Info\\s+(\\d+)")[1];
        if (has(doc["objs"], inum)) {
            let d = doc["objs"][inum]["d"];
            let valRe = "\\s*(\\((?:\\\\[\\s\\S]|[^\\\\)])*\\)|<[0-9A-Fa-f]*>)";
            let keysM = ["Title", "Author", "Producer", "CreationDate"];
            let outK = ["title", "author", "producer", "created"];
            let i = 0;
            while (i < len(keysM)) {
                let g = regexGroups(d, "/" + keysM[i] + valRe);
                if (len(g) == 2) { meta[outK[i]] = pvPdfTextValue(g[1]); }
                i = i + 1;
            }
        }
    }
    let cd = meta["created"];
    let dm = regexGroups(cd, "D:(\\d{4})(\\d{2})?(\\d{2})?");
    if (len(dm) >= 2) {
        let ymd = dm[1];
        if (len(dm) >= 3 and dm[2] != "") { ymd = ymd + "-" + dm[2]; }
        if (len(dm) >= 4 and dm[3] != "") { ymd = ymd + "-" + dm[3]; }
        meta["created"] = ymd;
    }
    return meta;
}

// يحلّل ملف PDF كاملاً -> {ok, error?, pages:[{num, lines, unmapped}], meta, encrypted, images, fonts, version}
fun pvPdfAnalyze(data) {
    if (len(data) < 8 or substr(data, 0, 5) != "%PDF-") {
        return {"ok": false, "error": "not a PDF file", "pages": []};
    }
    let doc = pvPdfLoad(data);
    doc["objs"] = pvPdfExpandObjStms(doc["objs"]);
    let encrypted = len(regexFindAll(data, "/Encrypt\\s+\\d+\\s+\\d+\\s+R|/Encrypt\\s*<<")) > 0;
    let meta = pvPdfMeta(doc, data);
    let images = 0;
    let fonts = 0;
    let ks = keys(doc["objs"]);
    let i = 0;
    while (i < len(ks)) {
        let dd = doc["objs"][ks[i]]["d"];
        if (len(regexFindAll(dd, "/Subtype\\s*/Image")) > 0) { images = images + 1; }
        if (len(regexFindAll(dd, "/Type\\s*/Font\\b")) > 0) { fonts = fonts + 1; }
        i = i + 1;
    }
    let pagesOut = [];
    if (encrypted == false) {
        let list = pvPdfPageList(doc, data);
        let cache = {};
        let p = 0;
        while (p < len(list)) {
            let pt = pvPdfPageText(doc, list[p], cache);
            push(pagesOut, {"num": p + 1, "obj": list[p], "lines": pt["lines"], "unmapped": pt["unmapped"]});
            p = p + 1;
        }
    }
    return {"ok": true, "encrypted": encrypted, "meta": meta, "pages": pagesOut, "images": images,
            "fonts": fonts, "objects": doc["count"], "version": meta["version"]};
}

// نص PDF كاملاً (صفحة بعد صفحة، فاصل \f بين الصفحات) من مسار ملف
fun pvPdfText(path) {
    if (fileExists(path) == false) { return ""; }
    let a = pvPdfAnalyze(readFile(path));
    let out = [];
    let i = 0;
    while (i < len(a["pages"])) {
        push(out, join(a["pages"][i]["lines"], "\n"));
        i = i + 1;
    }
    return join(out, "\n" + chr(12) + "\n");
}

fun pvPdfInfo(path) {
    if (fileExists(path) == false) { return {"ok": false, "error": "file not found"}; }
    let a = pvPdfAnalyze(readFile(path));
    if (a["ok"] == false) { return a; }
    a["pageCount"] = len(a["pages"]);
    let chars = 0;
    let i = 0;
    while (i < len(a["pages"])) {
        chars = chars + utf8Len(join(a["pages"][i]["lines"], ""));
        i = i + 1;
    }
    a["chars"] = chars;
    a["pages"] = nil;
    return a;
}

// ---- 10) عرض PDF ---------------------------------------------------------------------------

fun pvInfoCell(label, value) {
    if (value == "") { return ""; }
    return "<div><small>" + pvEsc(label) + "</small>" + pvEsc(value) + "</div>";
}

// يعيد {ok, kind:"pdf", html, pages, chars, encrypted, ...}
fun pvRenderPdf(data, theme, name) {
    let t = pvTheme(theme);
    let lb = pvLabels(t["lang"]);
    let a = pvPdfAnalyze(data);
    if (a["ok"] == false) {
        return {"ok": false, "kind": "pdf", "error": a["error"], "html": pvErrorPage(name, a["error"], theme)};
    }
    let info = "<div class=\"pv-info\">" +
        pvInfoCell(lb["title"], a["meta"]["title"]) + pvInfoCell(lb["author"], a["meta"]["author"]) +
        pvInfoCell(lb["producer"], a["meta"]["producer"]) + pvInfoCell(lb["created"], a["meta"]["created"]) +
        pvInfoCell(lb["version"], a["version"]) + pvInfoCell(lb["fonts"], toString(a["fonts"])) +
        pvInfoCell(lb["images"], toString(a["images"])) + "</div>";
    let body = [info];
    let chars = 0;
    let unmapped = 0;
    if (a["encrypted"]) {
        push(body, "<p class=\"pv-empty\">" + pvEsc(lb["encrypted"]) + "</p>");
    }
    let i = 0;
    while (i < len(a["pages"])) {
        let pg = a["pages"][i];
        push(body, "<section class=\"pv-page\" id=\"p" + toString(pg["num"]) + "\"><div class=\"pv-pn\">" + pvEsc(lb["page"]) + " " + toString(pg["num"]) + " / " + toString(len(a["pages"])) + "</div>");
        if (len(pg["lines"]) == 0) {
            push(body, "<p class=\"pv-empty\">" + pvEsc(lb["noText"]) + "</p>");
        }
        let j = 0;
        let gap = false;
        while (j < len(pg["lines"])) {
            let ln = pg["lines"][j];
            if (ln == "") { gap = true; }
            else {
                let cl = "";
                if (gap) { cl = " class=\"gap\""; }
                push(body, "<p dir=\"auto\"" + cl + ">" + pvEsc(ln) + "</p>");
                chars = chars + utf8Len(ln);
                gap = false;
            }
            j = j + 1;
        }
        unmapped = unmapped + pg["unmapped"];
        push(body, "</section>");
        i = i + 1;
    }
    let textPanel = "<div class=\"pv-pdf\">" + join(body, "") + "</div>";
    let main = textPanel;
    let embed = "";
    if (type(theme) == "map") {
        if (has(theme, "embedSrc")) { embed = theme["embedSrc"]; }
    }
    if (embed != "") {
        main = "<div class=\"pv-tabs\"><input type=\"radio\" name=\"pvtab\" id=\"pv-t1\" checked><input type=\"radio\" name=\"pvtab\" id=\"pv-t2\">" +
            "<div class=\"pv-tabbar\"><label for=\"pv-t1\">" + pvEsc(lb["original"]) + "</label><label for=\"pv-t2\">" + pvEsc(lb["text"]) + "</label></div>" +
            "<div class=\"pv-panels\"><div class=\"pv-panel p1\"><object class=\"pv-orig\" data=\"" + pvEsc(embed) + "\" type=\"application/pdf\"><p class=\"pv-empty\">" + pvEsc(embed) + "</p></object></div>" +
            "<div class=\"pv-panel p2\">" + textPanel + "</div></div></div>";
    }
    let meta = [toString(len(a["pages"])) + " " + lb["pages"], pvFmtBytes(len(data)), toString(chars) + " " + lb["chars"]];
    if (unmapped > 0) { push(meta, toString(unmapped) + " " + lb["unmapped"]); }
    let html = pvShell(name, "PDF", meta, "", main, theme);
    return {"ok": true, "kind": "pdf", "html": html, "pageCount": len(a["pages"]), "chars": chars,
            "encrypted": a["encrypted"], "unmapped": unmapped, "meta": a["meta"], "images": a["images"], "fonts": a["fonts"]};
}

// ---- 11) الواجهة العامة: التوليد + الحفظ + المراقبة الحيّة --------------------------------------

// يولّد الصفحة لملف موجود. opts (اختياري، كلها اختيارية): lang "ar"|"en"، live، intervalSec،
// accent/bg/... (ثيم relyRIN)، embedSrc (مسار نسبي لعرض PDF الأصلي)، kind (فرض النوع).
// يعيد {ok, kind, html, ...خاص بالنوع} أو {ok:false, error}
fun pvRender(path, opts) {
    if (fileExists(path) == false) {
        return {"ok": false, "error": "file not found: " + path, "kind": "missing",
                "html": pvErrorPage(path, pvLabels(pvTheme(opts)["lang"])["missing"], opts)};
    }
    let bytes = readFile(path);
    let kind = pvDetect(path, bytes);
    if (type(opts) == "map") {
        if (has(opts, "kind")) { kind = opts["kind"]; }
    }
    let name = pvBaseName(path);
    let ext = pvExt(path);
    if (kind == "markdown") { return pvRenderMarkdown(bytes, opts, name); }
    if (kind == "pdf") { return pvRenderPdf(bytes, opts, name); }
    return pvRenderCode(bytes, ext, kind, opts, name);
}

// يولّد من نص مباشرة (بلا ملف): pvRenderText("markdown"|"typescript"|..., src, opts)
fun pvRenderText(kind, src, opts) {
    let ext = "txt";
    if (kind == "typescript") { ext = "ts"; }
    if (kind == "javascript") { ext = "js"; }
    if (kind == "json") { ext = "json"; }
    if (kind == "markdown") { return pvRenderMarkdown(src, opts, "preview.md"); }
    if (kind == "pdf") { return pvRenderPdf(src, opts, "preview.pdf"); }
    return pvRenderCode(src, ext, kind, opts, "preview." + ext);
}

// بصمة سريعة لمحتوى الملف (CRC32): تتغيّر عند أي تعديل
fun pvStamp(path) {
    if (fileExists(path) == false) { return "missing"; }
    return fs.hash(path, "crc32");
}

// يولّد ويكتب الصفحة إلى outPath. يعيد {ok, kind, output, bytes, ms, stamp, ...}
fun pvPreview(path, outPath, opts) {
    let t0 = now();
    let r = pvRender(path, opts);
    writeFile(outPath, r["html"]);
    r["output"] = outPath;
    r["bytes"] = len(r["html"]);
    r["ms"] = now() - t0;
    r["stamp"] = pvStamp(path);
    r["source"] = path;
    r["html"] = nil;
    return r;
}

// خطوة مراقبة واحدة غير حاجبة: يقارن بصمة path بـ state.stamp؛ إن تغيّرت يعيد التوليد.
// state أولي: nil أو {}. يعيد {changed, state, result}. مناسب لحلقات المضيف (أندرويد/واجهة).
fun pvPollOnce(path, outPath, opts, state) {
    let st = state;
    if (type(st) != "map") { st = {"stamp": "", "runs": 0}; }
    let stamp = pvStamp(path);
    if (stamp == st["stamp"] and fileExists(outPath)) {
        return {"changed": false, "state": st, "result": nil};
    }
    let res = pvPreview(path, outPath, opts);
    st["stamp"] = stamp;
    st["runs"] = st["runs"] + 1;
    return {"changed": true, "state": st, "result": res};
}

// مراقبة حيّة حاجبة: تولّد أولاً ثم تفحص الملف كل intervalMs وتعيد التوليد عند أي تغيّر.
// opts إضافية: intervalMs (افتراضي 1000، 50..60000)، maxTicks (افتراضي 600 فحصاً)،
// onUpdate: دالة fun(result) تُستدعى بعد كل توليد (الأول أيضاً)، stopWhenMissing (افتراضي true).
// تنتهي عند maxTicks أو اختفاء الملف. يعيد {ok, runs, ticks, last}
fun pvLive(path, outPath, opts) {
    let o = opts;
    if (type(o) != "map") { o = {}; }
    let interval = 1000;
    if (has(o, "intervalMs")) { interval = o["intervalMs"]; }
    if (interval < 50) { interval = 50; }
    if (interval > 60000) { interval = 60000; }
    let maxTicks = 600;
    if (has(o, "maxTicks")) { maxTicks = o["maxTicks"]; }
    let page = {};
    let pk = keys(o);
    let ki = 0;
    while (ki < len(pk)) {
        let key = pk[ki];
        if (key != "intervalMs" and key != "maxTicks" and key != "onUpdate" and key != "stopWhenMissing") { page[key] = o[key]; }
        ki = ki + 1;
    }
    if (has(page, "live") == false) { page["live"] = true; }
    if (has(page, "intervalSec")) {
        // يُحتفظ بما حدّده المستخدم
    } else {
        page["intervalSec"] = max(1, ceil(interval / 1000));
    }
    let state = {"stamp": "", "runs": 0};
    let last = nil;
    let ticks = 0;
    while (ticks < maxTicks) {
        if (fileExists(path) == false) {
            if (has(o, "stopWhenMissing")) {
                if (o["stopWhenMissing"] == false) {
                    automation.sleepMs(interval);
                    ticks = ticks + 1;
                    continue;
                }
            }
            break;
        }
        let step = pvPollOnce(path, outPath, page, state);
        state = step["state"];
        if (step["changed"]) {
            last = step["result"];
            if (has(o, "onUpdate")) {
                let cb = o["onUpdate"];
                cb(last);
            }
        }
        ticks = ticks + 1;
        if (ticks < maxTicks) { automation.sleepMs(interval); }
    }
    return {"ok": true, "runs": state["runs"], "ticks": ticks, "last": last};
}

// "تشغيل" ملف بسطر واحد: يولّد معاينة بجوار الملف (name.preview.html) ويعيد الملخص.
fun pvRun(path, opts) {
    let out = path + ".preview.html";
    if (type(opts) == "map") {
        if (has(opts, "output")) { out = opts["output"]; }
    }
    return pvPreview(path, out, opts);
}

fun pvInfo() {
    return {
        "name": "previewkit",
        "version": "1.0.0",
        "description": "Live preview for PDF, Markdown and TypeScript/JavaScript/JSON/code files — pure Rin",
        "kinds": pvSupported(),
        "api": ["pvRun", "pvPreview", "pvRender", "pvRenderText", "pvLive", "pvPollOnce", "pvStamp",
                "pvDetect", "pvSupported", "pvPdfText", "pvPdfInfo", "pvTsOutline", "pvInfo"]
    };
}

// واجهات مساعدة سريعة --------------------------------------------------------------

// هيكل ملف TS/JS كمصفوفة {kind,name,line,depth} (من النص)
fun pvTsOutline(src, lang) {
    let l = "typescript";
    if (lang == "js" or lang == "javascript") { l = "javascript"; }
    let tk = pvTokenize(replace(src, "\r\n", "\n"), l);
    tk = pvClassifyIdents(tk, l);
    return pvAnalyzeCode(tk, l);
}
)PREVIEWKITOGRIN";

inline const std::unordered_map<std::string, std::string>& embeddedRinLibraries() {
    static const std::unordered_map<std::string, std::string> libs = {
        {"lib/math.og.rin", kLib_math_og_rin},
        {"lib/strings.og.rin", kLib_strings_og_rin},
        {"lib/data.og.rin", kLib_data_og_rin},
        {"lib/validate.og.rin", kLib_validate_og_rin},
        {"lib/inputkit.og.rin", kLib_inputkit_og_rin},
        {"lib/functional.og.rin", kLib_functional_og_rin},
        {"lib/oglang.og.rin", kLib_oglang_og_rin},
        {"lib/ringo.og.rin", kLib_ringo_og_rin},
        {"lib/langkit.og.rin", kLib_langkit_og_rin},
        {"lib/astwalk.og.rin", kLib_astwalk_og_rin},
        {"lib/envkit.og.rin", kLib_envkit_og_rin},
        {"lib/gridkit.og.rin", kLib_gridkit_og_rin},
        {"lib/iterkit.og.rin", kLib_iterkit_og_rin},
        {"lib/lexkit.og.rin", kLib_lexkit_og_rin},
        {"lib/loopkit.og.rin", kLib_loopkit_og_rin},
        {"lib/loopstats.og.rin", kLib_loopstats_og_rin},
        {"lib/parsekit.og.rin", kLib_parsekit_og_rin},
        {"lib/runkit.og.rin", kLib_runkit_og_rin},
        {"lib/seqkit.og.rin", kLib_seqkit_og_rin},
        {"lib/bob.og.rin", kLib_bob_og_rin},
        {"lib/ghpublish.og.rin", kLib_ghpublish_og_rin},
        {"lib/rinxg.og.rin", kLib_rinxg_og_rin},
        {"lib/rinzip.og.rin", kLib_rinzip_og_rin},
        {"lib/relyRIN.og.rin", kLib_relyRIN_og_rin},
        {"lib/movingmask.og.rin", kLib_movingmask_og_rin},
        {"lib/nlpkit.og.rin", kLib_nlpkit_og_rin},
        {"lib/syskit.og.rin", kLib_syskit_og_rin},
        {"lib/requirekit.og.rin", kLib_requirekit_og_rin},
        {"lib/physics.og.rin", kLib_physics_og_rin},
        {"lib/indsinicons.og.rin", kLib_indsinicons_og_rin},
        {"lib/passkitlang.og.rin", kLib_passkitlang_og_rin},
        {"lib/passkitdb.og.rin", kLib_passkitdb_og_rin},
        {"lib/passkitcrypt.og.rin", kLib_passkitcrypt_og_rin},
        {"lib/passkit.og.rin", kLib_passkit_og_rin},
        {"lib/archivekit.og.rin", kLib_archivekit_og_rin},
        {"lib/rintest.og.rin", kLib_rintest_og_rin},
        {"lib/packkit.og.rin", kLib_packkit_og_rin},
        {"lib/wesscode.og.rin", kLib_wesscode_og_rin},
        {"lib/indsinweb.og.rin", kLib_indsinweb_og_rin},
        {"lib/previewkit.og.rin", kLib_previewkit_og_rin},
    };
    return libs;
}

} // namespace rin

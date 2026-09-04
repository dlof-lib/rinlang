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
//    8) دوال Easing (لمنحنيات الحركة في واجهات Loom أو محرك الألعاب)
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

// استيفاء خطي (linear interpolation) بين a و b عند النسبة t (0..1)
fun lerp(a, b, t) {
    return a + (b - a) * t;
}

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
fun sign(x) {
    if (x > 0) { return 1; }
    if (x < 0) { return -1; }
    return 0;
}

// يقصّ الجزء العشري من x نحو الصفر (بخلاف floor الذي يتجه لأسفل دوماً)
fun trunc(x) {
    if (x < 0) { return ceil(x); }
    return floor(x);
}

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

fun sin(x) {
    let v = _reduceAngle(x);
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

fun cos(x) {
    let v = _reduceAngle(x);
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

fun tan(x) { return sin(x) / cos(x); }
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
fun atan(x) {
    let neg = x < 0;
    let v = x;
    if (neg) { v = -v; }
    let k = 0;
    while (v > 0.1 and k < 60) {
        v = v / (1 + sqrt(1 + v * v));
        k = k + 1;
    }
    let result = _atanTaylor(v) * pow(2, k);
    if (neg) { return -result; }
    return result;
}

fun asin(x) {
    if (x < -1 or x > 1) { print "asin: x يجب أن يكون بين -1 و 1"; return nil; }
    if (x == 1) { return PI / 2; }
    if (x == -1) { return -(PI / 2); }
    return atan(x / sqrt(1 - x * x));
}

fun acos(x) {
    if (x < -1 or x > 1) { print "acos: x يجب أن يكون بين -1 و 1"; return nil; }
    return (PI / 2) - asin(x);
}

// atan2(y, x): زاوية النقطة (x, y) مع مراعاة الربع الصحيح
fun atan2(y, x) {
    if (x > 0) { return atan(y / x); }
    if (x < 0) {
        if (y >= 0) { return atan(y / x) + PI; }
        return atan(y / x) - PI;
    }
    if (y > 0) { return PI / 2; }
    if (y < 0) { return -(PI / 2); }
    return 0;
}

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
fun exp(x) {
    if (x == 0) { return 1; }
    let neg = x < 0;
    let v = x;
    if (neg) { v = -v; }
    let k = 0;
    while (v > 0.5) {
        v = v / 2;
        k = k + 1;
    }
    let result = _expTaylor(v);
    let i = 0;
    while (i < k) {
        result = result * result;
        i = i + 1;
    }
    if (neg) { return 1 / result; }
    return result;
}

// اللوغاريتم الطبيعي: نختزل x إلى [1,2) عبر تتبّع الأس e (x = m * 2^e) ثم
// نستخدم سلسلة atanh السريعة التقارب: ln(m) = 2*atanh((m-1)/(m+1))
fun ln(x) {
    if (x <= 0) { print "ln: x يجب أن يكون > 0"; return nil; }
    let v = x;
    let e = 0;
    while (v >= 2) { v = v / 2; e = e + 1; }
    while (v < 1) { v = v * 2; e = e - 1; }
    let z = (v - 1) / (v + 1);
    let z2 = z * z;
    let term = z;
    let total = 0;
    let k = 1;
    while (k <= 39) {
        total = total + term / k;
        term = term * z2;
        k = k + 2;
    }
    return 2 * total + e * LN2;
}

fun log2(x) { return ln(x) / LN2; }
fun log10(x) { return ln(x) / LN10; }
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
// 8) دوال Easing — منحنيات حركة قياسية لواجهات Loom أو محرك الألعاب (t في [0,1])
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
fun range(n) {
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

// أيقونة/ألوان Banner حسب type= — نفس الأنواع المستخدمة في محرّك Loom الأصلي (info/success/
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

// Banner: شريط تنبيه — إما بشكل مركّب (children من text/button/... مثل Loom تماماً) أو بشكل
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
        let a = indexOf(x, "](", p + 1);
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
inline const std::unordered_map<std::string, std::string>& embeddedRinLibraries() {
    static const std::unordered_map<std::string, std::string> libs = {
        {"lib/math.og.rin", kLib_math_og_rin},
        {"lib/strings.og.rin", kLib_strings_og_rin},
        {"lib/data.og.rin", kLib_data_og_rin},
        {"lib/validate.og.rin", kLib_validate_og_rin},
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
    };
    return libs;
}

} // namespace rin

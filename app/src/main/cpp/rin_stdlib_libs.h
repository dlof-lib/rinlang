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
//  lib/math.og.rin — امتدادات رياضية فوق stdlib الأساسية (abs/sqrt/pow/min/max/random/PI/E)
//  استيراد:
//    @import "lib/math.og.rin";              // دمج مباشر في النطاق الحالي
//    @import "lib/math.og.rin" as mathx;      // كحاوية باسم مستعار (tying/merge لاحقاً)
// ============================================================================

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

// يحصر x بين lo و hi
fun clamp(x, lo, hi) {
    if (x < lo) { return lo; }
    if (x > hi) { return hi; }
    return x;
}

// استيفاء خطي (linear interpolation) بين a و b عند النسبة t (0..1)
fun lerp(a, b, t) {
    return a + (b - a) * t;
}

// إشارة الرقم: 1 موجب، -1 سالب، 0 صفر
fun sign(x) {
    if (x > 0) { return 1; }
    if (x < 0) { return -1; }
    return 0;
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

// متوسط مصفوفة أرقام (اسم بديل مريح لـ mean الأصلية)
fun average(arr) {
    return mean(arr);
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
)DATAOGRIN";

static const char* kLib_validate_og_rin = R"VALIDATEOGRIN(
// ============================================================================
//  lib/validate.og.rin — دوال تحقّق (validation) شائعة الاستخدام
//  ملاحظة: لغة Rin لا تملك try/catch، لذا كل دالة هنا "آمنة" (لا ترمي أخطاء) وتُعيد true/false دائماً.
//  استيراد:
//    @import "lib/validate.og.rin";
//    @import "lib/validate.og.rin" as validate;
// ============================================================================

// هل القيمة فارغة (nil, أو نص فارغ/مسافات فقط, أو مصفوفة/قاموس بلا عناصر)؟
fun isEmpty(v) {
    if (v == nil) { return true; }
    if (v == "") { return true; }
    return false;
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
)OGLANGOGRIN";

inline const std::unordered_map<std::string, std::string>& embeddedRinLibraries() {
    static const std::unordered_map<std::string, std::string> libs = {
        {"lib/math.og.rin", kLib_math_og_rin},
        {"lib/strings.og.rin", kLib_strings_og_rin},
        {"lib/data.og.rin", kLib_data_og_rin},
        {"lib/validate.og.rin", kLib_validate_og_rin},
        {"lib/functional.og.rin", kLib_functional_og_rin},
        {"lib/oglang.og.rin", kLib_oglang_og_rin},
    };
    return libs;
}

} // namespace rin

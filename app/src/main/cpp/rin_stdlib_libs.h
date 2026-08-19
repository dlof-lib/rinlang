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
    return ch == " " or ch == "	" or ch == "
";
}

fun isNewlineChar(ch) {
    return ch == "
";
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
    return join(lines, "
");
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
    return join(lines, "
");
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
    return join(lines, "
");
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

static const char* kLib_rinxg_og_rin = R"RINXGOGRIN(
// ============================================================================
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
//              button bg=#4CAF50 color=#ffffff radius=8 { "ابدأ الآن" }
//              button bg=#ffffff color=#4CAF50 radius=8 { "تعلّم المزيد" }
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
//  العناصر المدعومة: container (row|column، gap، padding، bg، radius، align، justify) •
//  heading (level، color) • text (color، size) • button (bg، color، radius) • input
//  (placeholder، عنصر مغلق بـ ';' بلا محتوى) • image (src، width، عنصر مغلق بـ ';') •
//  link (href) • list/item. أي وسم غير معروف يُصيَّر كـ <div data-rinxg-tag="..."> بدل أن
//  يُسقَط بصمت، ليسهل اكتشاف الأخطاء الإملائية في الوسوم.
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

fun rxContainerStyle(attrs) {
    let css = "display:flex;";
    if (has(attrs, "row")) { css = css + "flex-direction:row;"; }
    else { css = css + "flex-direction:column;"; }
    if (has(attrs, "gap")) { css = css + "gap:" + rxPx(attrs["gap"]) + ";"; }
    if (has(attrs, "padding")) { css = css + "padding:" + rxPx(attrs["padding"]) + ";"; }
    if (has(attrs, "bg")) { css = css + "background:" + attrs["bg"] + ";"; }
    if (has(attrs, "radius")) { css = css + "border-radius:" + rxPx(attrs["radius"]) + ";"; }
    if (has(attrs, "align")) { css = css + "align-items:" + attrs["align"] + ";"; }
    if (has(attrs, "justify")) { css = css + "justify-content:" + attrs["justify"] + ";"; }
    return css;
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
        let css = "border:none;padding:10px 20px;cursor:pointer;font-size:15px;";
        if (has(attrs, "bg")) { css = css + "background:" + attrs["bg"] + ";"; }
        if (has(attrs, "color")) { css = css + "color:" + attrs["color"] + ";"; }
        if (has(attrs, "radius")) { css = css + "border-radius:" + rxPx(attrs["radius"]) + ";"; }
        return "<button style=\"" + css + "\">" + txt + "</button>\n";
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
        "version": "1.0.0",
        "description": "لغة تصريحية لتصميم واجهات الويب فوق Rin: تُترجَم إلى صفحة HTML+CSS حقيقية جاهزة للعرض",
        "exports": ["rxToHtml", "rxParseToAst", "rxInfo"]
    };
}
)RINXGOGRIN";

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
    };
    return libs;
}

} // namespace rin

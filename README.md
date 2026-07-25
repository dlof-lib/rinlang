# Rin Lang

لغة برمجة كاملة صغيرة اسمها **Rin**، محرّكها (Lexer + Parser + Interpreter) مكتوب بلغة **C++17**، ومُدمَج داخل تطبيق أندرويد مكتوب بـ **Kotlin** عبر **JNI/NDK**. التطبيق نفسه محرر أكواد (IDE مصغّر) لكتابة وتشغيل برامج Rin مباشرة على الهاتف، وهناك **GitHub Action** يبني ملف **APK** تلقائياً عند كل `push`.

## هيكل المشروع

```
RinLang/
├── app/
│   ├── build.gradle                  # إعداد Gradle + ربط CMake بالـ NDK
│   ├── src/main/
│   │   ├── AndroidManifest.xml
│   │   ├── cpp/                      # محرّك اللغة بالكامل بلغة C++
│   │   │   ├── rin_common.h          # التوكنز + الأخطاء
│   │   │   ├── rin_lexer.h/.cpp      # المحلّل اللفظي (Lexer)
│   │   │   ├── rin_ast.h             # عقد شجرة البرنامج (AST)
│   │   │   ├── rin_parser.h/.cpp     # المحلّل النحوي (Parser)
│   │   │   ├── rin_interpreter.h/.cpp# المفسّر (Interpreter)
│   │   │   ├── jni_bridge.cpp        # جسر JNI بين C++ و Kotlin
│   │   │   └── CMakeLists.txt
│   │   ├── java/com/dlof/rinlang/
│   │   │   ├── RinEngine.kt          # واجهة Kotlin تستدعي C++ عبر JNI
│   │   │   └── MainActivity.kt       # محرر الأكواد + زر التشغيل + الكونسول
│   │   └── res/                      # XML: تخطيطات، ألوان، نصوص، أيقونة
├── tools/test_main.cpp               # تشغيل المحرّك خارج أندرويد لأغراض الاختبار
├── tools/test_containers.cpp         # اختبار مفاهيم لغة الحاويات (container, Section, link, merge...)
├── tools/test_groups.cpp             # اختبار Containers.Group المُقوّاة (تتبّع الأعضاء، التداخل، tying/merge على مستوى مجموعة)
├── tools/test_pipeline.cpp           # اختبار container.pipe والمُشغّل |> والدوال الإحصائية
├── .github/workflows/build-apk.yml   # بناء APK تلقائي عبر GitHub Actions
├── build.gradle / settings.gradle / gradle.properties
└── README.md
```

## لماذا C++ مع Kotlin؟

- **C++ (native/cpp)**: يحتوي المحرّك الحقيقي للغة Rin — القراءة اللفظية، التحليل النحوي، والتنفيذ. هذا الجزء مستقل تماماً عن أندرويد ويمكن تجربته على أي جهاز فيه g++ (انظر `tools/test_main.cpp`).
- **Kotlin**: يوفّر واجهة المستخدم (محرر الكود + زر Run + الكونسول)، ويستدعي المحرّك الأصلي عبر **JNI** بدالة واحدة بسيطة: `RinEngine.runSource(code)`.
- **الدمج**: تعريف `external fun runSource(...)` في `RinEngine.kt` يقابله تنفيذ باسم `Java_com_dlof_rinlang_RinEngine_runSource` في `jni_bridge.cpp`. هذا هو "الدمج" بين اللغتين — Kotlin يستدعي، وC++ ينفّذ.

## لغة Rin — دليل سريع

```rin
// المتغيرات
let x = 10;
let name = "Rin";

// الطباعة
print "Hello, " + name + "!";

// الشروط
if (x > 5) {
    print "big";
} else {
    print "small";
}

// الحلقات
let i = 0;
while (i < 5) {
    print i;
    i = i + 1;
}

// الدوال (تدعم الاستدعاء الذاتي/التكرار)
fun fib(n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
print fib(10);
```

الأنواع المدعومة: أرقام (double)، نصوص (string)، منطقية (true/false)، nil، **مصفوفات (arrays)**، و**قواميس (maps)**.
العمليات: `+ - * / %`، المقارنة `== != < <= > >=` (تدعم أيضاً المصفوفات/القواميس بمقارنة تركيبية)، المنطقية `and or !`، والتجميع بالأقواس.

### مصفوفات (Arrays) وقواميس (Maps)

```rin
// مصفوفات
let arr = [1, 2, 3];
print arr[0];        // 1
arr[1] = 20;         // arr = [1, 20, 3]
push(arr, 4);        // arr = [1, 20, 3, 4]
print pop(arr);       // 4  (ويصبح arr = [1, 20, 3])
print len(arr);       // 3
print sort([3, 1, 2]); // [1, 2, 3]

// قواميس
let m = {name: "Rin", age: 2};
print m["name"];      // Rin
m["age"] = 3;         // تعديل
m["lang"] = "ar";     // إضافة مفتاح جديد
print keys(m);        // ["name", "age", "lang"]
print values(m);       // ["Rin", 3, "ar"]
print has(m, "name");  // true
remove(m, "lang");     // يحذف المفتاح
```

الفهرسة تعمل أيضاً على النصوص: `"abc"[1]` تُعطي `"b"`.

### المكتبة القياسية (stdlib)

| الفئة | الدوال |
|---|---|
| رياضيات | `abs` `sqrt` `pow` `floor` `ceil` `round` `min` `max` `random`، والثوابت `PI` و`E` |
| نصوص | `len` `upper` `lower` `trim` `substr` `split` `join` `indexOf` `replace` `contains` `charAt` `toString` `toNumber` |
| مصفوفات | `len` `push` `pop` `sort` `contains` `join` |
| قواميس | `keys` `values` `has` `remove` `len` |
| إحصاء (statistics) | `sum` `mean` `median` `variance` `stddev` `mode` `minOf` `maxOf` (تجميع/Aggregation) — `normalize` `scale` `shift` (تحويل/Transformation) |

يمكنك تجربة كل هذا عبر:

```bash
g++ -std=c++17 -o rin_stdlib_test \
  tools/test_stdlib.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_stdlib_test
```

## لغة الحاويات/البيانات (Data Container Language)

فوق أساس Rin العام، تدعم اللغة أيضاً مجموعة مفاهيم مخصّصة لتنظيم وهيكلة **البيانات** على شكل حاويات (containers) قابلة للتضمين، الربط، الدمج، الترجمة، والحفظ. كل كتلة تبدأ بكلمة مفتاحية وتُغلق بوسم `.end/الكلمة`، تماماً مثل:

```rin
@container=my_data

.end/container
```

### المفاهيم المدعومة

| المفهوم | الصياغة | الوصف |
|---|---|---|
| `container` | `@container=name ... .end/container` | حاوية بيانات مستقلة، لها بيئة متغيرات خاصة بها. |
| `container.pipe` | `@container.pipe=name ... .end/container.pipe` | نفس `container` لكن مخصّصة لخطوط أنابيب البيانات/الإحصاء (انظر القسم التالي). |
| `Containers.Group` | `@Containers.Group=name ... .end/Containers.Group` | مجموعة تضم عدة حاويات (`@container=...`) و/أو مجموعات فرعية متداخلة، ولها بيئة متغيرات خاصة بها، وتُتبِّع أعضاءها (انظر القسم التالي). |
| `Volume` | `@Volume=name ... .end/Volume` | مستوى تنظيم أعلى يضمّ مجموعات أو حاويات (كـ"مجلد/جزء"). |
| `Section` | `Section=name ... .end/Section` | قسم داخلي لتقسيم بيانات الحاوية إلى أجزاء منطقية. |
| `text` | `text name = "قيمة";` | إعلان قيمة **نصية** (يتحقق المفسّر أن القيمة نص فعلاً). |
| `print` | `print expr;` | طباعة/عرض أي قيمة أو نص. |
| `Addition` / `Subtraction` / `Multiplication` / `Equal` | `Addition(a, b)` … | دوال مدمجة بأسمائها الصريحة للعمليات الحسابية والمقارنة، تُستخدم مثل أي نداء دالة. |
| `link` | `link to=name;` | يربط الحاوية الحالية بحاوية **أو مجموعة (Containers.Group)** أخرى موجودة (دون نسخ بياناتها). |
| `tying` | `tying with=name;` | ربط وثيق: ينسخ متغيرات حاوية أخرى — **أو كل حاويات مجموعة (Containers.Group) كاملة دفعة واحدة** — إلى الحاوية الحالية. |
| `merge` | `merge with=name;` | دمج كامل لمتغيرات حاوية أخرى — **أو مجموعة كاملة** — داخل الحاوية الحالية. |
| `translation` / `Translations` | `Translations translation lang="ar" text="..."; .end/Translations` | كتلة `Translations` تجمّع عدّة أسطر `translation` (لغة + نص) لدعم تعدد اللغات. |
| `installation` | `installation name;` | "تثبيت"/تسجيل حاوية أو اسم لجعله معروفاً ومتاحاً للمفسّر. |
| `simplified` | `simplified installation ...;` / `simplified save ...;` | مُعدِّل (modifier) يسبق `installation` أو `save` لطلب نسخة مبسّطة/مصغّرة. |
| `save` | `save;` / `save path="...";` | حفظ الحاوية الحالية، مع تحديد مسار اختياري. |
| `file` | `file path="...";` | تعريف/تسجيل مسار ملف مرتبط بالحاوية الحالية. |
| `path` | `path="..."` | سمة (attribute) تُستخدم مع `file` و`save` لتحديد المسار. |

### مثال كامل

```rin
@container=my_data
    text title = "بيانات ريـن";
    print title;

    Section=numbers
        let a = 10;
        let b = 4;
        print Addition(a, b);       // 14
        print Subtraction(a, b);    // 6
        print Multiplication(a, b); // 40
        print Equal(a, b);          // false
    .end/Section

    Translations
        translation lang="ar" text="مرحبا";
        translation lang="en" text="Hello";
    .end/Translations

    file path="data/output.rin";
    installation my_data;
    simplified save path="data/output.min.rin";
.end/container

@container=extra
    text note = "بيانات إضافية";
.end/container

@container=my_data2
    link to=my_data;      // ربط بدون نسخ
    tying with=extra;     // نسخ متغيرات extra
    merge with=extra;     // دمج كامل مع extra
.end/container

@Containers.Group=my_group
    @container=g1
        text label = "عنصر داخل مجموعة";
    .end/container
.end/Containers.Group

@Volume=vol1
    @Containers.Group=g2
        @container=c2
            text v = "داخل مجلد";
        .end/container
    .end/Containers.Group
.end/Volume
```

يمكنك تجربة هذا المثال كاملاً عبر:

```bash
g++ -std=c++17 -o rin_container_test \
  tools/test_containers.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_container_test
```

**ملاحظات مهمة:**
- الكلمات المفتاحية لهذه اللغة **حساسة لحالة الأحرف**: `container` (صغيرة) للحاوية نفسها، بينما `Section`، `Volume`، `Translations`، و`Containers.Group` تبدأ بحرف كبير لأنها كتل هيكلية.
- وسم الإغلاق `.end/الكلمة` يجب أن يطابق نوع الكتلة المفتوحة تماماً (مثلاً `@container=x ... .end/Section` يُعتبر خطأً نحوياً صريحاً).
- `text` تفرض أن تكون القيمة المُسندة نصية فعلاً، وإلا يرمي المفسّر خطأً واضحاً بدلاً من قبول أي نوع.
- `link`/`tying`/`merge` تتطلّب أن يكون الهدف (حاوية **أو مجموعة Containers.Group**) معرَّفاً مسبقاً في البرنامج (تُنفَّذ الحاويات والمجموعات بالترتيب من الأعلى للأسفل).
- بما أن هذا محرّك تعليمي (Lexer + Parser + Interpreter) يعمل داخل الذاكرة فقط، فإن `file`/`save`/`installation` لا تكتب فعلياً على نظام الملفات؛ إنما تُسجَّل وتُطبَع كرسائل توضّح ما "كان سيحدث"، وهي نقطة انطلاق جاهزة لربطها لاحقاً بتخزين حقيقي (كما هو مقترح أصلاً في قسم "أفكار للتوسعة").

## Containers.Group بالتفصيل

`Containers.Group` ليست مجرد وسم زخرفي حول عدّة `container` — لها الآن سلوك حقيقي:

- **بيئة متغيرات خاصة بها**: أي `let`/`text` مُعلَن مباشرة داخل المجموعة (خارج أي `container` بداخلها) يبقى داخل نطاقها ولا يتسرّب للخارج، تماماً مثل `container` و`Section`.
- **تتبُّع الأعضاء تلقائياً**: كل `@container=...` (أو `@Containers.Group=...` متداخلة) داخلها تُسجَّل كعضو بالترتيب، وتظهر عند وسم الإغلاق: `✅ .end/Containers.Group (my_group) [تحتوي: g1, g2]`.
- **مجموعات متداخلة**: مجموعة داخل مجموعة تُسجَّل كعضو في الأب أيضاً، وتُفَكّ (تُفلطَح) تلقائياً عند الاستعلام عن كل الحاويات الفعلية بداخلها.
- **`link` / `tying` / `merge` تقبل اسم مجموعة كاملة**، وليس فقط اسم حاوية مفردة:
  - `link to=my_group;` — تحقّق فقط من وجود المجموعة، دون نسخ.
  - `tying with=my_group;` / `merge with=my_group;` — تنسخ متغيرات **كل** الحاويات الأعضاء داخل المجموعة (بما فيها المجموعات الفرعية المتداخلة) إلى الحاوية الحالية، بالترتيب؛ عند تعارض اسم متغير بين عضوين، يفوز آخر عضو تم نسخه.
- **دالتان جاهزتان للاستعلام البرمجي** عن أعضاء أي مجموعة:

| الدالة | الوصف |
|---|---|
| `groupMembers(name)` | أسماء الأعضاء **المباشرين** فقط (حاويات أو مجموعات فرعية) كما ظهروا داخل المجموعة. |
| `groupContainers(name)` | كل أسماء **الحاويات الفعلية** داخل المجموعة، مع تفكيك أي مجموعات فرعية متداخلة بالكامل. |

```rin
@Containers.Group=team_alpha
    @container=alpha_1
        text label = "أول";
    .end/container
    @container=alpha_2
        text label = "ثاني";
    .end/container
.end/Containers.Group

print groupMembers("team_alpha");    // ["alpha_1", "alpha_2"]
print groupContainers("team_alpha"); // ["alpha_1", "alpha_2"]

@container=summary
    tying with=team_alpha; // ينسخ label من alpha_1 ثم من alpha_2
    print label;           // "ثاني" (آخر عضو نُسخ يفوز عند تعارض الاسم)
.end/container
```

يمكنك تجربة هذا كاملاً عبر:

```bash
g++ -std=c++17 -o rin_groups_test \
  tools/test_groups.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_groups_test
```

## خط الأنابيب (Pipeline) والبيانات الإحصائية

فوق `container` العادية، تضيف اللغة **`container.pipe`** ومُشغّل الأنابيب **`|>`** لبناء خطوط معالجة بيانات على شكل: **بيانات مُدخَلة (Input) → تحويل (Transformation) → تجميع (Aggregation) → نتيجة نهائية (Final Output)** — بنفس الترتيب في المخطّط الآتي:

```
[Input Data] -> [Step 1 (Transformation)] -> [Step 2 (Aggregation)] -> [Final Output]
```

### مُشغّل الأنابيب `|>`

يمرّر القيمة الموجودة على يسار `|>` كأول وسيط (argument) للنداء الموجود على يمينه، فتصبح سلسلة من التحويلات قابلة للقراءة من اليسار إلى اليمين بدل تعشيش الأقواس:

```rin
let data = [10, 20, 30, 40, 50];

// data |> normalize() |> mean();  يُكافئ تماماً  mean(normalize(data))
let result = data |> normalize() |> mean();
print result;
```

يمكن أيضاً تمرير وسائط إضافية بعد القيمة المنقولة: `data |> scale(2)` تعادل `scale(data, 2)`. كما يمكن كتابة اسم الدالة بدون أقواس إن لم تحتَج وسائط إضافية: `data |> mean`.

### الدوال الإحصائية الجاهزة

| النوع | الدوال | الاستخدام النموذجي |
|---|---|---|
| تجميع (Aggregation) | `sum(arr)` `mean(arr)` `median(arr)` `variance(arr)` `stddev(arr)` `mode(arr)` `minOf(arr)` `maxOf(arr)` | نهاية الأنبوب: تُلخّص المصفوفة إلى قيمة واحدة |
| تحويل (Transformation) | `normalize(arr)` `scale(arr, factor)` `shift(arr, delta)` | منتصف الأنبوب: تُنتج مصفوفة جديدة بنفس الحجم |

جميعها تتوقّع مصفوفة أرقام (array of numbers) وتَرمي خطأً واضحاً لو كانت فارغة أو تحتوي عناصر غير رقمية.

### `container.pipe`

هي نفس `@container=name ... .end/container` تماماً في آلية العمل (بيئة متغيرات خاصة، تدعم `link`/`tying`/`merge`)، لكنها تُستخدم لتنظيم خطوط الأنابيب صراحةً، وتُطبع بشكل مختلف (`🧵 container.pipe` بدل `📦 container`) لتمييزها بصرياً في المخرجات:

```rin
@container.pipe=sales_pipeline
    let raw = [10, 20, 30, 40, 50];   // Input Data

    fun transform(data) {             // Step 1 (Transformation)
        return normalize(data);
    }

    fun aggregate(data) {             // Step 2 (Aggregation)
        return mean(data);
    }

    let final_output = raw |> transform() |> aggregate();  // Final Output
    print final_output;
.end/container.pipe
```

يمكنك تجربة هذا كاملاً عبر:

```bash
g++ -std=c++17 -o rin_pipeline_test \
  tools/test_pipeline.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_pipeline_test
```

## البناء محلياً

يحتاج Android Studio (Arctic Fox أو أحدث) مع NDK و CMake مثبّتين من SDK Manager:

```bash
./gradlew assembleDebug
# الناتج: app/build/outputs/apk/debug/app-debug.apk
```

## البناء التلقائي عبر GitHub Actions

كل `push` على أي فرع يشغّل `.github/workflows/build-apk.yml` الذي:
1. يجهّز JDK 17 و Android SDK و NDK و CMake.
2. يبني `assembleDebug`.
3. يرفع ملف الـ APK الناتج كـ **artifact** باسم `rin-lang-debug-apk` يمكن تحميله من تبويب Actions في المستودع.

## اختبار المحرّك بمفرده (بدون أندرويد)

```bash
g++ -std=c++17 -o rin_test \
  tools/test_main.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_test
```

## ميزات المحرر (IDE)

- **تلوين الصيغة النحوية (syntax highlighting)**: يُلوَّن الكود مباشرة أثناء الكتابة (كلمات مفتاحية، نصوص، أرقام، تعليقات، دوال المكتبة القياسية، ووسوم لغة الحاويات مثل `@container` و`.end/...`)، عبر `RinSyntaxHighlighter.kt`.
- **حفظ/فتح ملفات `.rin`**: زرّا "فتح" و"حفظ" في الشريط العلوي يستخدمان Storage Access Framework (SAF) لقراءة/كتابة ملفات `.rin` من تخزين الجهاز مباشرة (Google Drive، التخزين المحلي، إلخ)، مع تذكّر آخر ملف مفتوح لإعادة الحفظ عليه مباشرة أو "حفظ باسم" لملف جديد.

## أفكار للتوسعة لاحقاً

- دوال بأكثر من قيمة تُرجَع (multiple return / tuples).
- حلقة `for` مخصّصة للتكرار على المصفوفات والقواميس.
- تراجع/إعادة (undo/redo) في المحرر، وترقيم للأسطر.
- تنفيذ فعلي لـ `save`/`file`/`installation` داخل لغة الحاويات على تخزين الجهاز (حالياً تُسجَّل وتُطبَع فقط كما هو موضّح أعلاه).

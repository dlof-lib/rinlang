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
│   │   │   ├── rin_stdlib_libs.h     # سجل المكتبات المدمجة (embedded) لعبارة @import — نسخة من lib/*.rin
│   │   │   ├── jni_bridge.cpp        # جسر JNI بين C++ و Kotlin
│   │   │   └── CMakeLists.txt
│   │   ├── java/com/dlof/rinlang/
│   │   │   ├── RinEngine.kt          # واجهة Kotlin تستدعي C++ عبر JNI
│   │   │   └── MainActivity.kt       # محرر الأكواد + زر التشغيل + الكونسول
│   │   └── res/                      # XML: تخطيطات، ألوان، نصوص، أيقونة
├── lib/                               # مكتبات/حزم Rin جاهزة قابلة للاستيراد عبر @import "lib/...og.rin"
│   ├── math.og.rin                   # دوال رياضية إضافية
│   ├── strings.og.rin                # دوال نصوص إضافية
│   ├── data.og.rin                   # دوال مصفوفات/قواميس إضافية
│   ├── validate.og.rin               # دوال تحقّق (validation) آمنة (لا ترمي أخطاء)
│   └── functional.og.rin             # دوال ترتيبية عليا (map/filter/reduce...) على المصفوفات
├── tools/test_main.cpp               # تشغيل المحرّك خارج أندرويد لأغراض الاختبار
├── tools/test_containers.cpp         # اختبار مفاهيم لغة الحاويات (container, Section, link, merge...)
├── tools/test_groups.cpp             # اختبار Containers.Group المُقوّاة (تتبّع الأعضاء، التداخل، tying/merge على مستوى مجموعة)
├── tools/test_pipeline.cpp           # اختبار container.pipe والمُشغّل |> والدوال الإحصائية
├── tools/test_persistence.cpp        # اختبار استمرارية save/installation فعلياً عبر تشغيلين منفصلين (Interpreter جديد في كل مرة)
├── tools/test_import.cpp             # اختبار @import: المكتبات المدمجة الخمس + الدمج المباشر + الاستيراد باسم مستعار
├── tools/test_nosql.cpp              # اختبار قاعدة البيانات اللاعلاقية (container.doc/doc, document, insertDoc/updateDoc/queryDocs...)
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
| `installation` | `installation name;` | يحفظ الحاوية (أو مجموعة Containers.Group كاملة) **فعلياً على القرص** داخل `rin_installed/`، ويسجّلها في فهرس مستمر يبقى محفوظاً بين التشغيلات المختلفة. |
| `simplified` | `simplified installation ...;` / `simplified save ...;` | مُعدِّل (modifier) يسبق `installation` أو `save` لطلب نسخة مبسّطة/مصغّرة (سطر واحد مضغوط، امتداد `.min.rin`). |
| `save` | `save;` / `save path="...";` | يكتب متغيرات الحاوية الحالية **فعلياً** كملف `.rin` قابل لإعادة القراءة، بمسار صريح أو افتراضي باسم الحاوية. |
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
- **`file`/`save`/`installation` تعمل فعلياً على القرص** (وليست رسائل توضيحية فقط): راجع قسم "التخزين الحقيقي على القرص" أدناه للتفاصيل الكاملة.

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

## قاعدة البيانات اللاعلاقية (NoSQL) — `container.doc` / `doc`

فوق `container` و`Containers.Group`، تضيف اللغة مفهوم **قاعدة بيانات لاعلاقية (NoSQL) حقيقية بالكامل**، بنفس فلسفة `container.table`: بيانات مُدارة داخل المفسّر نفسه (لا داخل بيئة متغيرات الحاوية)، قابلة للحفظ والاستعادة والاستعلام برمجياً.

**المصطلحات:**

| مفهوم NoSQL | يُمثَّل في Rin بـ |
|---|---|
| قاعدة بيانات (database) | `Containers.Group` |
| مجموعة مستندات (collection) | `@container.doc=name ... .end/container.doc` (أو `@doc=name ... .end/doc` بشكل مستقل) |
| مستند (document) | `document id="..." fields={ ... };` — كائن حر البنية (schema-less) بلا شكل ثابت |

```rin
@Containers.Group=shop_db
    @container.doc=users
        document id="u1" fields={ name: "سارة", age: 28, city: "الرياض" };
        document id="u2" fields={ name: "أحمد", age: 35, city: "جدة" };
    .end/container.doc

    @container.doc=orders
        document id="o1" fields={ user: "u1", total: 120 };
    .end/container.doc
.end/Containers.Group

print groupContainers("shop_db"); // ["users", "orders"] -> كل مجموعات المستندات داخل قاعدة البيانات
```

- `document id=EXPR fields=EXPR;` يعمل كـ **upsert**: إن كان `id` موجوداً مسبقاً داخل نفس المجموعة يُستبدَل مستنده بالكامل، وإلا يُضاف كمستند جديد بترتيب الإدخال.
- `container.doc`/`doc` (تماماً كـ `container.table`/`table`) لا تسمح بدوال أو حاويات/مجموعات متداخلة أو `route` بداخلها — تبقى "بيانات نقية" قابلة للتسلسل.

### الاستعلام والتعديل وقت التشغيل (Runtime CRUD)

بالإضافة إلى `document` الوصفي، تتوفّر دوال جاهزة يمكن استدعاؤها من أي مكان في البرنامج:

| الدالة | الوصف |
|---|---|
| `insertDoc(collection, id, fields)` | إدراج/استبدال كامل لمستند (upsert حيّ). تُرجع `true` إن كان إدراجاً جديداً، `false` إن استبدل مستنداً موجوداً. |
| `updateDoc(collection, id, partialFields)` | تحديث جزئي (patch): يدمج الحقول الجديدة فقط مع المستند الموجود دون حذف بقية حقوله. تُرجع `false` إن لم يوجد المستند. |
| `deleteDoc(collection, id)` | يحذف مستنداً بمعرّفه. تُرجع `true`/`false` حسب وجوده. |
| `findDoc(collection, id)` | يُرجع حقول المستند (map) أو `nil` إن لم يوجد. |
| `queryDocs(collection, field, value)` | يُرجع مصفوفة كل المستندات التي يساوي فيها الحقل `field` القيمة `value`. |
| `queryOneDoc(collection, field, value)` | نفس الشيء لكن يُرجع أول مطابقة فقط (أو `nil`). |
| `docIds(collection)` | مصفوفة كل معرِّفات (ids) المستندات بترتيب الإدخال. |
| `allDocs(collection)` | مصفوفة كل المستندات (كل عنصر map) بترتيب الإدخال. |
| `countDocs(collection)` | عدد المستندات داخل المجموعة. |

```rin
print insertDoc("users", "u3", { name: "منى", city: "الرياض" }); // true
print updateDoc("users", "u1", { city: "مكة" });                  // true (تحديث جزئي، بقية حقول u1 كما هي)
print queryDocs("users", "city", "الرياض");                       // كل مستندات users في الرياض
print deleteDoc("orders", "o1");                                  // true
```

### الحفظ والاستعادة

`save` و`installation` تعملان مع `container.doc`/`doc` تماماً كأي حاوية أخرى: تُكتب كل مستنداتها كسلسلة عبارات `document id=... fields=...;` داخل الملف الناتج، بحيث تُعاد قراءتها بالكامل لاحقاً عبر `container.import` أو `loadInstalled()`.

يمكنك تجربة هذا كاملاً عبر:

```bash
g++ -std=c++17 -o rin_nosql_test \
  tools/test_nosql.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_nosql_test
```

## التخزين الحقيقي على القرص (save / installation / file)

`file`، `save`، و`installation` تنفّذ **قراءة/كتابة فعلية على القرص**، وليست رسائل توضيحية فقط. كل مسار نسبي يُبنى فوق جذر اختياري (`basePath`) يحدَّده المُضيف:

- **على أندرويد**: `RinEngine.init(context)` (استُدعيت تلقائياً من `MainActivity`/`PipelineRunnerActivity`) تمرّر `filesDir` الخاص بالتطبيق — تخزين معزول لا يحتاج أي إذن (permission).
- **خارج أندرويد (`tools/test_*.cpp`)**: `interpreter.setBasePath("...")`، أو تُترَك فارغة فتُستخدَم المسارات كما هي بالنسبة للمجلد الحالي (CWD).

### `save` — حفظ حاوية كملف `.rin` قابل لإعادة القراءة

```rin
@container=my_data
    text title = "بيانات ريـن";
    let count = 3;
    save;                          // يكتب my_data.rin (الاسم الافتراضي = اسم الحاوية)
    save path="exports/data.rin";  // أو مساراً صريحاً (تُنشأ المجلدات الوسيطة تلقائياً)
    simplified save path="exports/data.min.rin"; // نسخة مضغوطة بسطر واحد
.end/container
```

يُسلسِل `save` كل متغيرات الحاوية المباشرة (`text`/`let`، وليس ما بداخل `Section` الفرعية) إلى نص Rin صالح — بما فيه المصفوفات والقواميس المتداخلة — بحيث يمكن إعادة تحميل الملف لاحقاً عبر `@container.import=... file path="...";` أو عبر `loadInstalled()`. الدوال (functions) لا يمكن تمثيلها كقيمة محفوظة فتُتجاهَل مع تنبيه في النسخة الكاملة (غير المبسّطة).

### `installation` — تثبيت مستمر عبر التشغيلات المختلفة

```rin
@container=profile
    text name = "Droy";
    let level = 7;
    installation profile;   // يكتب rin_installed/profile.rin ويُسجَّل في فهرس دائم
.end/container
```

كل `installation` تكتب نسخة فعلية من الحاوية (أو من **كل** حاويات مجموعة `Containers.Group` كاملة إن كان الهدف مجموعة) داخل `rin_installed/`، وتضيف سطراً في فهرس `rin_installed/index.rininstall`. هذا الفهرس **يُقرأ تلقائياً في بداية أي تشغيل لاحق** يستخدم نفس `basePath` — أي أن `isInstalled(...)` تعرف عن تثبيتات حصلت في عملية/تشغيل سابق تماماً، وليس فقط ضمن نفس التشغيل الحالي.

### الوصول للتثبيتات والملفات من كود Rin

| الدالة | الوصف |
|---|---|
| `isInstalled(name)` | هل هذا الاسم مثبَّت فعلياً (بما فيها تثبيتات من تشغيلات سابقة)؟ |
| `listInstalled()` | مصفوفة بكل الأسماء المثبَّتة حالياً. |
| `loadInstalled(name)` | يقرأ نسخة الحاوية المحفوظة فعلياً من `rin_installed/` وينفّذها (تعود متاحة للـ `tying`/`merge` بنفس اسمها). يُرجع `true`/`false`. |
| `writeFile(path, content)` / `readFile(path)` | كتابة/قراءة ملف نصي عام فعلي على القرص. |
| `appendFile(path, content)` | إضافة محتوى لنهاية ملف موجود (أو إنشاؤه إن لم يوجد). |
| `fileExists(path)` / `deleteFile(path)` | فحص وجود ملف، أو حذفه فعلياً. |

```rin
let ok = loadInstalled("profile");
if (ok) {
    @container=check
        tying with=profile;   // نسخ متغيرات profile المحمَّلة فعلياً من القرص
        print name;
        print level;
    .end/container
}

writeFile("logs/run.txt", "started\n");
appendFile("logs/run.txt", "step 1 done\n");
print readFile("logs/run.txt");
```

يمكنك تجربة استمرارية التثبيت عبر تشغيلين منفصلين تماماً (Interpreter جديد في كل مرة، تماماً كتشغيلين مختلفين للتطبيق) عبر:

```bash
g++ -std=c++17 -o rin_persist_test \
  tools/test_persistence.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_persist_test
```

### تهريب النصوص (string escapes)

لدعم حفظ/إعادة قراءة أي نص فعلياً (بما فيه نصوص تحتوي علامات تنصيص أو أسطر جديدة)، تدعم النصوص الآن: `\"` `\\` `\n` `\t` `\r`.

```rin
print "قال: \"مرحباً\"\nسطر ثانٍ";
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

## نظام المكتبات (@import) والمكتبات الجاهزة

فوق كل ما سبق، تدعم اللغة عبارة **`@import`** لاستيراد مكتبات/حزم Rin جاهزة (أو ملفاتك الخاصة) واستخدام
دوالها مباشرة، دون الحاجة لتكرار كتابتها في كل مشروع.

### الصياغة

```rin
@import "lib/data.og.rin";            // دمج مباشر: كل fun/let/text أعلى مستوى في المكتبة
                                       // تصبح متاحة فوراً في النطاق الحالي (تماماً كـ #include)

@import "lib/data.og.rin" as data;    // استيراد باسم مستعار: يُسجَّل كحاوية باسم 'data'،
                                       // فيمكن لاحقاً ربطها بـ link/tying/merge كأي حاوية أخرى
                                       // دون تلويث النطاق الحالي بأسماء المكتبة مباشرة.
```

```rin
@import "lib/math.og.rin";
print factorial(5);      // 120
print isPrime(17);       // true

@import "lib/math.og.rin" as mathx;
@container=viewer
    tying with=mathx;    // ينسخ كل دوال/متغيرات mathx إلى viewer
    print gcd(48, 18);   // 6
.end/container
```

### من أين تُقرأ المكتبة؟

عند تنفيذ `@import "المسار"`, يبحث المفسّر بالترتيب:

1. **سجل المكتبات المدمجة (embedded)** داخل ثنائي المفسّر نفسه (`app/src/main/cpp/rin_stdlib_libs.h`) —
   هذا ما يجعل استيراد المكتبات القياسية الخمس أدناه يعمل فوراً على أي منصة (بما فيها أندرويد) دون
   الحاجة لنسخ أي ملف `.rin` إضافي إلى تخزين التطبيق.
2. **ملف فعلي على القرص** (نسبةً إلى `basePath`، بنفس آلية `file`/`save`/`installation`) إن لم يكن
   المسار موجوداً في السجل المدمج — وهذا ما يتيح لك كتابة مكتباتك ومشاريعك الخاصة واستيرادها بنفس
   العبارة تماماً، مثلاً `@import "utils/my_helpers.rin";`.

استيراد نفس المسار (بنفس أسلوب الاستيراد: مباشر أو بنفس الاسم المستعار) أكثر من مرة في نفس التشغيل
يُتجاهَل تلقائياً بلا خطأ (تماماً كأنظمة الوحدات/modules المعتادة).

### المكتبات الخمس الجاهزة (`lib/*.og.rin`)

| المكتبة | تحتوي على |
|---|---|
| `lib/math.og.rin` | `factorial` `gcd` `lcm` `isPrime` `clamp` `lerp` `sign` `fibonacci` `average` `roundTo` `inRange` `percentOf` |
| `lib/strings.og.rin` | `capitalize` `reverseStr` `startsWith` `endsWith` `padLeft` `padRight` `repeatStr` `titleCase` `isBlank` `countOccurrences` `stripSpaces` `splitTrim` |
| `lib/data.og.rin` | `range` `rangeFrom` `unique` `chunk` `zip` `first` `last` `take` `drop` `reverseArr` `mapGet` `mapMerge` `countOf` |
| `lib/validate.og.rin` | `isEmpty` `isBlankStr` `isNumeric` `isEmail` `lengthBetween` `isInRange` `hasLetterAndDigit` `isStrongPassword` |
| `lib/functional.og.rin` | `mapArr` `filterArr` `reduceArr` `forEachArr` `findArr` `findIndexArr` `everyArr` `someArr` `timesRun` `composeApply` `isEven` `isOdd` `isPositive` `isNegative` |

`lib/functional.og.rin` يوظّف كون الدوال في Rin **قيماً من الدرجة الأولى (first-class)**: يمكن تمرير
اسم أي دالة `fun` كوسيط عادي (مثل `mapArr([1,2,3], double)`)، وتُستدعى بداخل الدالة المستقبِلة تماماً
كأي متغير آخر يحمل دالة.

يمكنك تجربة كل هذا عبر:

```bash
g++ -std=c++17 -o rin_import_test \
  tools/test_import.cpp \
  app/src/main/cpp/rin_lexer.cpp \
  app/src/main/cpp/rin_parser.cpp \
  app/src/main/cpp/rin_interpreter.cpp \
  -I app/src/main/cpp
./rin_import_test
```

**ملاحظة:** لا تملك Rin عبارة `try`/`catch`، لذا مكتبة `validate.og.rin` مصمَّمة بحيث لا ترمي أي دالة
فيها خطأً أبداً — كل دالة تحقّق تُعيد `true`/`false` دائماً مهما كانت المدخلات.

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

## شاشات المشاريع والملفات (Projects / Files)

فوق نافذة المحرر الواحدة، يدعم التطبيق الآن تنظيم العمل في **مشاريع** متعددة:

- **شاشة "أنشئ مشروع" (`ProjectsActivity`)**: إنشاء/إعادة تسمية/حذف/فتح مشاريع Rin، كل مشروع مجلد مستقل داخل تخزين التطبيق الخاص (`filesDir/projects/<name>/`) بحيث تعمل `save`/`installation` الخاصة به بمعزل عن باقي المشاريع.
- **شاشة "الملفات" (`FilesActivity`)**: تعرض كل ملفات `.rin` داخل مشروع مُحدَّد، مع:
  - **إضافة ملف** جديد فارغ بالاسم المطلوب.
  - **رفع (استيراد) ملف** موجود بالفعل على الجهاز أو Google Drive عبر SAF — يُنسخ محتواه داخل المشروع.
  - فتح أي ملف مباشرة في المحرر ([MainActivity]) أو حذفه.

انظر `ProjectManager.kt` للتفاصيل، و`INTEGRATION_NOTES.md` لخطوات دمج هاتين الشاشتين مع بقية المشروع (Manifest/layout/strings).

## استدعاء Rin من لغات برمجة أخرى

بالإضافة لدمج Rin داخل Kotlin عبر JNI، يمكن الآن استدعاء نفس محرّك C++ من **أي لغة برمجة** (بايثون، Node.js، C/C++، ولاحقاً أي لغة تدعم FFI) عبر واجهة C مسطّحة (`app/src/main/cpp/rin_c_api.h`) تُبنى كمكتبة مشتركة عامة مستقلة عن أندرويد. انظر مجلد `bindings/` لأمثلة جاهزة وتفاصيل البناء.

## أفكار للتوسعة لاحقاً

- دوال بأكثر من قيمة تُرجَع (multiple return / tuples).
- حلقة `for` مخصّصة للتكرار على المصفوفات والقواميس.
- تراجع/إعادة (undo/redo) في المحرر، وترقيم للأسطر.
- ترتيب المتغيرات المحفوظة حسب ترتيب التعريف الفعلي بدل الترتيب الأبجدي (يتطلب تحويل `Environment::values` من `unordered_map` إلى بنية تحافظ على الترتيب).
- واجهة داخل التطبيق لتصفّح/حذف/تصدير محتويات `rin_installed/` مباشرة (حالياً تُدار فقط عبر كود Rin أو يدوياً على الجهاز).

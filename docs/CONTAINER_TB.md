# `@Container.TB( ... )` — منشئ الأدوات والقوالب الكاملة (Tool/Template Builder)

مفهوم يجمع في كتلة واحدة: المكتبات (بمفتاحها السهل `easy_key`)، ورموز القالب `#token`، وملفات `rin`/`C++`، واسم الأداة `@make`،
مع بقاء كل عبارات اللغة (`let` · `for` · `from` · `fun` ...) تعمل بداخله. هو «سكّر نحوي» يُحوَّل في المحلّل (`rin_parser.cpp`)
إلى حاوية `make` + `link` + `@import` + `let`، فلا يتغيّر سلوك أي برنامج قديم.

## الصيغة
```kotlin
@Container.TB(
    [[ #import ./container.img_sev.rin;
       #import ./main.container_run.rin;
       #import ./logic.rin_container.cpp ]];     // ملفات rin (تُنفَّذ) أو C++ (تُسجَّل)
    @import cfgkey;                              // مكتبة بمفتاحها السهل easy_key
    #token.title = "Hello";                      // رمز/ثابت قالب
    let total = 0;                               // أي عبارة عادية
    for i in [1, 2, 3] { total += i; }
    fun go() { return title + " " + total; }
    @make.Greeter                                // اسم الأداة = اسم الحاوية الناتجة
)
print Greeter.go();
```

## القاعدة الأساسية: أول سطر فقط
الاستدعاء **أول عبارة في الملف** (التعليقات وحدها قبله مسموحة). لا يصحّ في آخر الملف ولا بعد الأداة ولا داخل كتلة أخرى،
ومحاولة ذلك خطأ بناء `E0015` برسالة واضحة. كذلك لا تُكرَّر الكتلة في ملف واحد.

## العناصر
| الصيغة | تُحوَّل إلى | ملاحظات |
|---|---|---|
| `@import key;` / `@import a, b;` | `link to=key;` | المفتاح هو `easy_key` للحاوية (أو اسمها أو مسارها الكامل). المكتبة تُعرَّف قبل السطر (مثلاً بملف `#import` يسبقه) |
| `@import "./x.rin";` | `@import "./x.rin";` | نص فيه `/` أو ينتهي بـ `.rin` يُعامَل مساراً لا مفتاحاً |
| `#token.name = expr;` | `let name = expr;` | يُسجَّل اسمه في `__tb.tokens` |
| `#import ./f.rin;` | استيراد وتنفيذ | بلا اقتباس؛ `#Import` و`#import` سواء |
| `#import ./f.cpp;` (`.cc .cxx .c .h .hpp`) | تسجيل فقط | يظهر في `__tb.native` — لا يُترجَم ولا يُنفَّذ داخل المفسّر؛ يُسلَّم لسلسلة البناء |
| `[[ #import a; #import b ]];` | عدة `#import` | الفاصل `;` أو `,`، وآخر عنصر بلا فاصل مقبول |
| `@make.Name` | اسم الحاوية | **إلزامي** ومرّة واحدة |
| `let` · `for` · `from` · `fun` · `easy_key = "k";` ... | كما هي | تعمل بداخل الكتلة كما في أي حاوية |

## البيانات الوصفية
تُنشأ داخل الأداة `__tb`:
```kotlin
Greeter.__tb["name"]      // "Greeter"
Greeter.__tb["tokens"]    // ["title", ...]
Greeter.__tb["libs"]      // مفاتيح @import
Greeter.__tb["imports"]   // ملفات rin المستوردة
Greeter.__tb["native"]    // مصادر C++ المسجَّلة
```

## تحسين مرافق
`link to=X;` صار يقبل أيضاً `easy_key` والمسار الكامل للحاوية (كان يقبل الاسم الداخلي فقط).

## الملفات
`rin_parser.cpp` (`containerTBBlock`) · `rin_parser.h` · `rin_interpreter.cpp` (`LinkStmt`) ·
الاختبار: `tests/print/container_tb.rin` (+ `tests/print/tb_lib/`).

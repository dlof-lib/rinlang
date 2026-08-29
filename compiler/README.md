# rinc — مترجم Rin الأصلي (Native Compiler)

هذا المجلد يضيف **مترجماً** (compiler) حقيقياً إلى مشروع RinLang، ليكمّل محرّك
المفسّر (interpreter) الموجود في `app/src/main/cpp/` (rin_lexer.cpp / rin_parser.cpp /
rin_interpreter.cpp، المُصدَّر عبر JNI في `RinEngine.kt`).

نسخة مطابقة من `rinc.cpp` موجودة أيضاً داخل `app/src/main/cpp/rinc.cpp` بجانب بقية
ملفات اللغة مباشرة، للرجوع إليها بسهولة عند العمل على المحرّك — لكنها أداة مستقلة
تُبنى على جهاز التطوير (host) بـ g++/clang عادي، وليست جزءاً من `libRinengine.so`
التي يبنيها `CMakeLists.txt` عبر NDK لأندرويد (لم تُضَف لقائمة `add_library` هناك
عمداً: لا معنى لتشغيلها داخل تطبيق أندرويد نفسه لأنها تستدعي مترجم C عبر `system()`
على جهاز التطوير).

`rinc.cpp` ملف واحد قائم بذاته: يقرأ نفس نحو اللغة (نفس lexer/parser منطقياً)
ثم يولّد كود C ويستدعي مترجم C موجود على النظام (`cc`/`gcc`/`clang`) لإنتاج
**ملف تنفيذي أصلي (native executable)** حقيقي — وهو ما لم يكن متوفراً في المشروع
سابقاً (المفسّر ينفّذ الكود مباشرة، ولا ينتج ملفات تنفيذية).

## البناء والاستخدام

```bash
# 1) ابنِ المترجم نفسه مرة واحدة (لا يحتاج أي ملف آخر من المشروع)
g++ -O2 -std=c++17 -o rinc compiler/rinc.cpp

# 2) حوّل أي برنامج .rin إلى تنفيذي أصلي
./rinc path/to/program.rin          # ينتج ./program
./rinc path/to/program.rin -o app   # يسمي التنفيذي app
./rinc path/to/program.rin --emit-c-only   # فقط يولّد ملف .c دون بنائه
```

## النطاق

يدعم اللغة الإجرائية الأساسية في Rin بالكامل (let / print / if-else / while /
`for` على طراز C / `rinopen` (اسم بديل لـ `while`) / `plus.condition` (شرط
ثلاثي عام) / fun-return / المصفوفات
والقواميس والفهرسة / كل المعاملات + `|>` / مكتبة قياسية موسّعة: رياضيات وإحصاء
(`abs/sqrt/pow/floor/ceil/round/min/max/random/maxOf/minOf/median/mode/stddev/
variance/scale/normalize/shift/sum/mean`) ونصوص (`upper/lower/trim/substr/
split/join/indexOf/replace/contains/charAt/chr/ord/toString/toNumber/toBool/
isBool`) ومصفوفات/قواميس (`len/push/pop/sort/keys/values/has/remove`) وملفات
(`readFile/writeFile/appendFile/fileExists/deleteFile`) و**JSON حقيقي في
الاتجاهين** (`jsonEncode/jsonDecode`، محلِّل recursive-descent كامل مطابق
لـ `rin_json.h`، بما فيها fallback إلى نص خام عند JSON غير صالح)).

**دعم جديد — حلقة `for` على طراز C:**

```
for (let i = 0; i < 10; i = i + 1) {
    print i;
}
```

بنفس نحو/دلالة المفسّر الأصلي بالضبط (`ForStmt` في `rin_parser.cpp`/
`rin_interpreter.cpp`): الأجزاء الثلاثة اختيارية (`for (;;) { .. }` صالحة)،
`initializer` إما `let x = ...;` أو عبارة تعبير (`x = ...;`)، `condition`
الغائب يُعتبر `true` دائماً، و`break`/`continue` يعملان بداخلها تماماً كما في
`while` (بما في ذلك أن `increment` ينفَّذ دائماً بعد `continue`). تُترجَم مباشرة
إلى حلقة `for` C99 حقيقية، فنطاق المُتغيّر المُهيَّأ في الجملة يطابق تماماً
نطاق `forEnv` في المفسّر.

**دعم جديد — `plus.condition` (شرط ثلاثي عام على مستوى العبارات):**

```
plus.condition (x > 5) {
    print "big";
} / {
    print "small";
}
```

بنفس نحو المفسّر الأصلي بالضبط (`PlusConditionStmt`): كلتا الكتلتين إلزاميتان
(بخلاف `else` الاختيارية في `if`)، ويمكن تعشيشها. تُترجَم إلى `if`/`else` C
حقيقي بنفس الدلالة.

**دعم جديد — `rinopen (condition) { body }`:**

```
let i = 0;
rinopen (i < 5) {
    print i;
    i = i + 1;
}
```

اسم بديل لـ `while` في المفسّر الأصلي بالضبط (يُبنى فعلياً كعقدة `WhileStmt`
عادية عند التحليل هناك أيضاً — انظر `Parser::rinopenStatement` في
`rin_parser.cpp`)، بفارق نحوي واحد فقط: الجسم هنا كتلة `{ }` إلزامية دائماً
(بخلاف `while` التي تقبل عبارة سطر واحد بلا أقواس). `break`/`continue` يعملان
بداخلها بنفس دلالة `while` تماماً.

كل الأوامر أعلاه (`for`/`plus.condition`/`rinopen`) تم التحقق من مطابقة
ناتجهما لناتج المفسّر الأصلي حرفياً (بناء `tools/test_main.cpp`-style host
runner لمقارنة الإخراج سطراً بسطر على عدة برامج اختبار، منها حلقات متداخلة،
تعشيش `plus.condition`، واستخدام `plus` كاسم متغيّر عادي خارج سياق
`plus.condition` النحوي (الكلمة الوحيدة من الثلاث التي تبقى غير محجوزة خارج
ذلك السياق؛ `for`/`rinopen` أصبحتا محجوزتين بالكامل كما في المفسّر الأصلي) —
بلا فروقات).

لا يدعم مُعظم "لغة الحاويات/البيانات" الخاصة بمحرّك المفسّر وتطبيق أندرويد
(`Containers.Group`, `Volume`, `Section`, `Translations`, `link`,
`tying`, `merge`, `installation`, `save`, `table`/`row`/`style`, `document`
(NoSQL), `route`, `container.pipe/api/import/table/doc/object/portal/block/sticker/aukt`,
`@import`) — هذه الميزات مرتبطة عضوياً بتخزين التطبيق ولا
معنى مستقل لها في تنفيذي native، ويصدر `rinc` خطأ واضحاً عند مصادفتها بدل
توليد سلوك خاطئ صامت، مع توجيه لاستخدام المفسّر الأصلي لتلك الملفات.

**دعم جديد وحقيقي — `@container` و `@container.data`/`@data` (PLAIN/DATA):**
هذان النوعان فقط من الحاويات مدعومان الآن فعلياً في `rinc`، بنفس قواعد نحو
المفسّر الأصلي بالضبط (بلا أقواس `{}`؛ الجسم يمتد حتى وسم إغلاق `.end/...`
مطابق، أو الاختصار `.end;`):

```
@container=inventory
let apples = 10;
print apples * 3;
.end/container=inventory

@data=config
let mode = "production";
.end;
```

يُترجَم كل منهما إلى نطاق C حقيقي (`{ ... }`) مع نفس نص السرد بالضبط الذي
يطبعه المفسّر (الأيقونة + اسم الوسم + اسم الحاوية، ثم `✅ .end/...`) — ناتج
`rinc` وناتج المفسّر متطابقان حرفياً لنفس البرنامج (تم التحقق فعلياً).

قيود صريحة على هذا الدعم (وليست افتراضات — مفروضة وقت التحليل، لا سلوك خاطئ صامت):
- `container.data`/`data` يجب أن تبقى "بيانات نقية": بلا `fun` وبلا حاويات متداخلة
  بداخلها (تماماً كقيد `validateDataContainerBody` في المفسّر الأصلي).
- `fun` غير مدعومة أيضاً بداخل `@container` العادية في `rinc` تحديداً (خلافاً
  للمفسّر الذي يسمح بها) — لأن مولّد الكود هنا لا يرفع الدوال المتداخلة في
  حاويات إلى دوال C مستقلة بعد؛ يُصدَر خطأ واضح بدل تجاهل الدالة صامتاً.
  عرّف الدالة على المستوى الأعلى بدلاً من ذلك.
- التعشيش (`@container` داخل `@container` أخرى) مدعوم ويعمل فعلياً.
- اسم الإغلاق الاختياري (`.end/container=name`) يُتحقَّق منه فعلياً ويجب أن
  يطابق اسم الفتح تماماً، كما في المفسّر الأصلي.

## تدقيق المكتبة القياسية (native functions)

المفسّر الأصلي (`rin_interpreter.cpp`) يسجّل 145 دالة أصلية (native). تمت
مراجعتها جميعاً؛ التصنيف:

- **مدعومة في `rinc` الآن (50 دالة):** كل الرياضيات/الإحصاء/النصوص/المصفوفات/
  القواميس/الملفات/JSON المذكورة أعلاه في "النطاق".
- **غير مدعومة عمداً (~95 دالة) ولن تُدعَم في مترجم native مستقل:** كل ما هو
  مرتبط عضوياً بمحرّك المفسّر/تطبيق أندرويد ولا معنى مستقل له في ملف تنفيذي —
  NoSQL/مستندات (`insertDoc/queryDocs/findByIndex/...`)، مخطط/ترحيلات/علاقات
  قاعدة بيانات (`defineSchema/runMigration/defineRelation/...`)، معاملات
  (`beginTransaction/...`)، الدردشة/البوت (`botReply/sendMessage/onChat/...`)،
  HTTP/API (`httpGet/apiRegister/callApi/...` — تحتاج شبكة حقيقية وسياق
  اعتماديات لا معنى له في transpile مباشر لملف C)، لافتات/تنبيهات Loomtime
  (`bannerInfo/...`)، إدارة تثبيت/مجموعات الحاويات (`isInstalled/
  groupMembers/...`)، والذاكرة المؤقتة (`cacheGet/...`). `rinc` يرفض استدعاء
  أيٍّ منها وقت التحليل برسالة "دالة غير معرَّفة" بدل توليد سلوك خاطئ صامت.

## مزامنة `app/src/main/cpp/rinc.cpp`

النسخة الموجودة داخل `app/src/main/cpp/rinc.cpp` مطابقة الآن حرفياً لهذا
الملف (باستثناء تعليق الترويسة الذي يشرح سبب وجودها هناك) — تم التحقق ببناء
كلتا النسختين وتشغيل نفس برامج الاختبار عليهما ومقارنة الناتج حرفياً (بلا
فروقات).

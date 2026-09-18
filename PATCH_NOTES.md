# تعديلات: `@Program` + تحسين مخرجات بداية/نهاية `Containers.Group`

## طريقة التركيب
انسخ كل ملف إلى نفس المسار **بالضبط** داخل مستودعك (استبدال الملف الأصلي بالكامل، وليس دمجاً):

```
app/src/main/cpp/rin_interpreter.h
app/src/main/cpp/rin_ast.h
app/src/main/cpp/rin_parser.cpp
app/src/main/cpp/rin_interpreter.cpp
app/src/main/cpp/rin_make.cpp
app/src/main/cpp/indsin/rin_indsin_pipeline.h
```

أي: فك ضغط الملف مباشرة في جذر المستودع (root) وسيحل كل ملف مكان نظيره تلقائياً بنفس اسم ومسار المجلد.

## الملفات الخمسة المعدَّلة، وماذا تغيَّر في كل واحد

### 0) `app/src/main/cpp/rin_interpreter.h`
إضافة `std::vector<std::string> programStack` — يتتبّع أي `@Program` مفتوحة حالياً (يدعم التعشيش)، تستخدمه natives `programName()`/`programDepth()`/`inProgram()` أدناه.

### 2) `app/src/main/cpp/rin_ast.h`
إضافة بنية AST جديدة `ProgramStmt` (name / mask / body / **recoverName** / **recoverBody**) — لتمثيل كتلة `@Program ... .end/Program`، بما فيها عبارة الاسترداد الاختيارية `recover`.

### 3) `app/src/main/cpp/rin_parser.cpp`
- إضافة `"Program"` إلى قائمة الوسوم الصالحة بعد `@`.
- فرع تحليل جديد يبني `ProgramStmt` من الجسم (بنفس آلية `atBlock()` العامة المستخدمة لـ `Volume`/`Containers.Group`، فيرث `mask="..."` مجاناً).
- **جديد: تحليل `recover (err) { ... }` أو `recover { ... }`** — عبارة اختيارية خاصة بـ `@Program` فقط، تُكتَب داخل الجسم (عادة قبل `.end/Program` مباشرة، بنفس مكان `catch` بعد `try`)، وتُستخرَج من body إلى `s->recoverName`/`s->recoverBody` (لا تبقى ضمن body العادي، بنفس أسلوب استخراج `mask` تماماً).
- منع تعشيش `Program` داخل حاويات البيانات النقية (`container.data`/`table`)، بنفس قاعدة `Volume`/`Containers.Group`.
- تحديث رسالة الخطأ الإرشادية عند وسم غير معروف لتذكر `Program`.

### 4) `app/src/main/cpp/rin_interpreter.cpp`
- إضافة `#include <chrono>`.
- **تنفيذ `ProgramStmt`** (يدعم التعشيش — `@Program` داخل `@Program` أخرى، مثلاً لتقسيم برنامج كبير إلى مراحل/phases كل منها بداية/نهاية خاصة بها):
  - يطبع `🚀 Program = name` عند البداية (مع مسافة بادئة حسب عمق التعشيش، وذكر الأب المباشر `↳ ضمن: ...` عند التعشيش).
  - ينفّذ الجسم في نفس البيئة المحيطة (لا نطاق منفصل خلافاً لـ Group — Program إطار عرض لا حاوية بيانات).
  - عند النجاح: `🏁 .end/Program (name)  ⏱️ Nms` مع قياس زمن التنفيذ الفعلي.
  - **بلا `recover`** (السلوك الافتراضي، كما كان): عند فشل غير مُدار بداخله يطبع `❌ .end/Program (name) — فشل بعد ⏱️ Nms` ثم يُعيد رمي نفس الاستثناء كما هو تماماً، فتستمر معالجة الخطأ المعتادة (diagnostic كامل) بلا أي تغيير في ذلك السلوك.
  - **مع `recover`** (جديد): الخطأ (RinError أو throw صريح عبر ThrowSignal) يُلتَقط فعلياً هنا بدل إعادة رميه — تُطبَع علامة `🩹 .end/Program (name) — استُرِدَّ بعد ⏱️ Nms`، ثم يُنفَّذ جسم `recover` (مع متغيّر الخطأ إن سُمِّي: map فيه `message`/`line`/`code`/`value` حسب نوع الخطأ، بنفس شكل متغيّر `catch` في `try/catch` القائمة أصلاً)، ثم **يستمر تنفيذ البرنامج بعد `.end/Program` بشكل طبيعي** كأن شيئاً لم يحدث.
  - ثلاث دوال native جديدة للاستعلام أثناء التنفيذ: `inProgram()` (bool)، `programName()` (اسم أقرب Program مفتوحة، أو "")، `programDepth()` (عدد كتل Program المتعشّشة المفتوحة حالياً).
- **تحسين `ContainerGroupStmt`** (بداية/نهاية `Containers.Group`):
  - مسافة بادئة بصرية تعكس عمق التعشيش.
  - سطر البداية يذكر الأب المباشر (`↳ ضمن: ...`) والـ mask إن وُجدا.
  - سطر النهاية يفصل الحاويات المباشرة عن المجموعات الفرعية بدل قائمة "تحتوي:" مختلطة، ويضيف إجمالي الحاويات الفعلية بعد التفرّع الكامل.

### 4) `app/src/main/cpp/rin_make.cpp`
فحص القدرات (`use`/`need`/...) الخاص بـ `make` أصبح يتجاوز داخل جسم `@Program` بحثاً عن `container`/`loop`/... بدل تجاهله بالكامل.

### 5) `app/src/main/cpp/indsin/rin_indsin_pipeline.h`
- `findContainerBody`: يبحث الآن داخل `@Program` أيضاً عن أي `@container` مُغلَّف بداخله (كان سيفشل في إيجاده سابقاً).
- `collectViewStmts`: يعامل `@Program` كامتداد للنطاق المحيط (وليس نطاقاً منفصلاً كـ Group/Volume)، فيجمع أي `@view` بداخله بشكل صحيح.

## مثال استخدام سريع (تعشيش، الدوال الجديدة، والاسترداد recover)
```rin
@Program=Outer
    print("depth=" + programDepth() + " name=" + programName());
    @Program=Inner
        print("داخل Inner: " + programName());
    .end/Program
    print("رجعنا لـ Outer: " + programName());
.end/Program
print("خارج أي Program: in=" + inProgram());
```
بلا `recover`: عند حدوث خطأ غير مُدار بداخل `@Program`، تُطبَع علامة فشل صريحة قبل رسالة الخطأ، ثم يتوقف البرنامج (السلوك الافتراضي، كما كان):
```
🚀 Program = WillFail
قبل الخطأ
❌ .end/Program (WillFail)  — فشل بعد ⏱️ 0ms

error[E0004]: ...
```
**مع `recover`** (جديد): نفس الخطأ يُلتَقط بدل إيقاف البرنامج، ويستمر التنفيذ طبيعياً بعد `.end/Program`:
```rin
@Program=WithRecovery
    text x = 5;   // خطأ نوع
recover (err) {
    print("تم الاسترداد: " + err.message + " عند السطر " + err.line);
}
.end/Program
print("استمر البرنامج بعد الاسترداد");
```
```
🚀 Program = WithRecovery
🩹 .end/Program (WithRecovery)  — استُرِدَّ بعد ⏱️ 0ms
تم الاسترداد: [E0004] 'x' من نوع text ويجب أن تكون قيمته نصاً (string) عند السطر 2
استمر البرنامج بعد الاسترداد
```
`err` هو map بنفس شكل متغيّر `catch` في `try/catch` القائمة أصلاً: `message`/`line` دائماً، `code` لأخطاء التشخيص، أو `value` لـ `throw` صريح. `recover` بلا اسم متغيّر (`recover { ... }` فقط) مسموح أيضاً إن لم تكن تفاصيل الخطأ مطلوبة.

## ملاحظات
- `@Program` اختيارية تماماً؛ أي ملف `.rin` لا يستخدمها يعمل بلا أي تغيير.
- تدعم التعشيش الكامل (`@Program` داخل `@Program`).
- `recover` اختيارية أيضاً؛ بدونها السلوك كما كان (فشل + إعادة رمي).
- لم تُعدَّل أي ملفات أخرى غير الستة المذكورة أعلاه.
- تم بناء المشروع بالكامل (g++ -std=c++17) واختباره عبر `tests/tools/test_groups.cpp`,
  `test_containers.cpp`, `test_table.cpp`, `test_container_sql.cpp`, `test_nosql.cpp` — كلها
  ناجحة (exit 0) بعد التعديل، بالإضافة لاختبارات يدوية للتعشيش، الدوال الجديدة، مسار الفشل بلا
  recover، واسترداد كل من RinError و throw الصريح (بمتغيّر خطأ مسمّى وبدونه).

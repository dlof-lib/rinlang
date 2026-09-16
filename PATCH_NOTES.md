# تعديلات: `@Program` + تحسين مخرجات بداية/نهاية `Containers.Group`

## طريقة التركيب
انسخ كل ملف إلى نفس المسار **بالضبط** داخل مستودعك (استبدال الملف الأصلي بالكامل، وليس دمجاً):

```
app/src/main/cpp/rin_interpreter.h
app/src/main/cpp/rin_ast.h
app/src/main/cpp/rin_parser.cpp
app/src/main/cpp/rin_interpreter.cpp
app/src/main/cpp/rin_make.cpp
app/src/main/cpp/loom/rin_loom_pipeline.h
```

أي: فك ضغط الملف مباشرة في جذر المستودع (root) وسيحل كل ملف مكان نظيره تلقائياً بنفس اسم ومسار المجلد.

## الملفات الخمسة المعدَّلة، وماذا تغيَّر في كل واحد

### 0) `app/src/main/cpp/rin_interpreter.h`
إضافة `std::vector<std::string> programStack` — يتتبّع أي `@Program` مفتوحة حالياً (يدعم التعشيش)، تستخدمه natives `programName()`/`programDepth()`/`inProgram()` أدناه.

### 1) `app/src/main/cpp/rin_ast.h`
إضافة بنية AST جديدة `ProgramStmt` (name / mask / body) — نفس شكل `VolumeStmt` تماماً — لتمثيل كتلة `@Program ... .end/Program` الجديدة.

### 2) `app/src/main/cpp/rin_parser.cpp`
- إضافة `"Program"` إلى قائمة الوسوم الصالحة بعد `@`.
- فرع تحليل جديد يبني `ProgramStmt` من الجسم (بنفس آلية `atBlock()` العامة المستخدمة لـ `Volume`/`Containers.Group`، فيرث `mask="..."` مجاناً).
- منع تعشيش `Program` داخل حاويات البيانات النقية (`container.data`/`table`)، بنفس قاعدة `Volume`/`Containers.Group`.
- تحديث رسالة الخطأ الإرشادية عند وسم غير معروف لتذكر `Program`.

### 3) `app/src/main/cpp/rin_interpreter.cpp`
- إضافة `#include <chrono>`.
- **تنفيذ `ProgramStmt`** (يدعم التعشيش الآن — `@Program` داخل `@Program` أخرى، مثلاً لتقسيم برنامج كبير إلى مراحل/phases كل منها بداية/نهاية خاصة بها):
  - يطبع `🚀 Program = name` عند البداية (مع مسافة بادئة حسب عمق التعشيش، وذكر الأب المباشر `↳ ضمن: ...` عند التعشيش).
  - ينفّذ الجسم في نفس البيئة المحيطة (لا نطاق منفصل خلافاً لـ Group — Program إطار عرض لا حاوية بيانات).
  - عند النجاح: `🏁 .end/Program (name)  ⏱️ Nms` مع قياس زمن التنفيذ الفعلي.
  - عند فشل غير مُدار بداخله (خطأ نوع، استثناء، ...): يطبع أولاً `❌ .end/Program (name) — فشل بعد ⏱️ Nms` ثم يُعيد رمي نفس الاستثناء كما هو تماماً، فتستمر معالجة الخطأ المعتادة (diagnostic كامل) بلا أي تغيير في ذلك السلوك — الفرق الوحيد أن الخرج الآن لا "ينقطع" بصمت بلا أي علامة إغلاق.
  - ثلاث دوال native جديدة للاستعلام أثناء التنفيذ: `inProgram()` (bool)، `programName()` (اسم أقرب Program مفتوحة، أو "")، `programDepth()` (عدد كتل Program المتعشّشة المفتوحة حالياً).
- **تحسين `ContainerGroupStmt`** (بداية/نهاية `Containers.Group`):
  - مسافة بادئة بصرية تعكس عمق التعشيش.
  - سطر البداية يذكر الأب المباشر (`↳ ضمن: ...`) والـ mask إن وُجدا.
  - سطر النهاية يفصل الحاويات المباشرة عن المجموعات الفرعية بدل قائمة "تحتوي:" مختلطة، ويضيف إجمالي الحاويات الفعلية بعد التفرّع الكامل.

### 4) `app/src/main/cpp/rin_make.cpp`
فحص القدرات (`use`/`need`/...) الخاص بـ `make` أصبح يتجاوز داخل جسم `@Program` بحثاً عن `container`/`loop`/... بدل تجاهله بالكامل.

### 5) `app/src/main/cpp/loom/rin_loom_pipeline.h`
- `findContainerBody`: يبحث الآن داخل `@Program` أيضاً عن أي `@container` مُغلَّف بداخله (كان سيفشل في إيجاده سابقاً).
- `collectViewStmts`: يعامل `@Program` كامتداد للنطاق المحيط (وليس نطاقاً منفصلاً كـ Group/Volume)، فيجمع أي `@view` بداخله بشكل صحيح.

## مثال استخدام سريع (بما فيه التعشيش والدوال الجديدة)
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
عند حدوث خطأ غير مُدار بداخل `@Program`، تُطبَع علامة فشل صريحة قبل رسالة الخطأ:
```
🚀 Program = WillFail
قبل الخطأ
❌ .end/Program (WillFail)  — فشل بعد ⏱️ 0ms

error[E0004]: ...
```

## ملاحظات
- `@Program` اختيارية تماماً؛ أي ملف `.rin` لا يستخدمها يعمل بلا أي تغيير.
- تدعم التعشيش الكامل (`@Program` داخل `@Program`).
- لم تُعدَّل أي ملفات أخرى غير الستة المذكورة أعلاه.
- تم بناء المشروع بالكامل (g++ -std=c++17) واختباره عبر `tests/tools/test_groups.cpp`,
  `test_containers.cpp`, `test_table.cpp`, `test_container_sql.cpp`, `test_nosql.cpp` — كلها
  ناجحة (exit 0) بعد التعديل، بالإضافة لاختبارات يدوية للتعشيش، الدوال الجديدة، ومسار الفشل.

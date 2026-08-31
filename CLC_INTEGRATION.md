# ربط CLC بمفسّر Rin (`rin_interpreter.cpp`) — **مُنفَّذ ومُختبَر فعلياً**

**حالة هذا المستند (محدَّثة):** تم تنفيذ هذا الدمج فعلياً داخل شجرة
`app/src/main/cpp` (وليس فقط تصميماً كما في النسخة السابقة من هذا الملف).
تم بناؤه واختباره فعلياً عبر `cli/linux` (compile + link ناجحان بـ g++ 13،
C++17، بلا أي تحذير)، وتشغيل حقيقي لـ `library.export`/`library.import`
أنتج ونجح في قراءة حاوية `.rcl` فعلية وتنفيذ دالة مستورَدة منها بنجاح.
البناء الفعلي لتطبيق Android (`app/src/main/cpp/CMakeLists.txt`) **يتضمّن
الآن** مصادر `clc/` ضمن مكتبة `rinengine`، لكنه لم يُختبَر عملياً هنا داخل
بيئة Android/Gradle كاملة (لا تتوفر بيئة بناء Android في هذه الجلسة) — فقط
تم التحقق من تطابق أسماء الملفات/التضمينات مع ما يبنيه `cli/linux` بنجاح
فعلياً بنفس المصادر بالحرف.

## ما تغيّر فعلياً عن التصميم الأصلي أدناه

1. **`clc/clc_io.h` يجب تضمينه صراحة** في `rin_interpreter.h` — `clc_container.h`
   لا يتضمّنه تلقائياً، و`clc::ClcFormatError` معرَّف هناك فقط. بدون هذا
   السطر فشل البناء فعلياً بخطأ parse غريب (`expected unqualified-id before
   '&' token`) عند `catch (const clc::ClcFormatError& e)`.
2. **`library.import` لا يمكن أن تكون native عادية بالتوقيع الثابت
   `std::function<Value(std::vector<Value>&, int)>`** لأنها تحتاج `env` كي
   تدمج تعريفات كل ملف `.rin` مستورَد في نفس نطاق نداء `library.import`
   نفسه (تماماً كسلوك `@import` المباشر). الحل الفعلي: اعتراضها في
   `Interpreter::invokeCallee()` مباشرة (نفس مكان اعتراض `builtinOps`)
   بدل تسجيلها في خريطة `natives`، وتنفيذها في دالة عضو منفصلة
   `Interpreter::doLibraryImport(args, line, env)`.
3. تمت إضافة عدد صغير من natives إضافية للفائدة العملية (لم ترد في
   التصميم الأصلي): `clcContainerFileCount`، `clcContainerFileName`،
   `clcContainerMetaName`، `clcContainerMetaVersion` — لاستكشاف محتوى حاوية
   مفتوحة عبر `container.open` بلا الحاجة لاستيرادها بالكامل.
4. لم يُنفَّذ أي تعديل في `rin_parser.cpp` لصياغة `container.open "x.rcl"`
   بلا أقواس (تبقى خارج النطاق كما ورد أصلاً أدناه) — الاستدعاء الفعلي هو
   دالة عادية: `container.open("x.rcl")` / `clcContainerOpen("x.rcl")`.

## مثال فعلي مُختبَر (كود Rin حقيقي، خرجه منسوخ حرفياً)

```rin
// mylib/greet.rin
fun greet(name) {
    return "Hello, " + name + "!";
}
let libVersion = "1.0.0";
```

```rin
// تصدير المجلد إلى حاوية .rcl
let size = libraryExport("mylib.rcl", "mylib");
print "exported bytes:", size;   // -> exported bytes: 450
```

```rin
// استيراد الحاوية واستخدام ما بداخلها مباشرة
let imported = libraryImport("mylib.rcl");
print "imported files:", imported;  // -> imported files: ["greet.rin"]
print greet("Rin");                 // -> Hello, Rin!
print libVersion;                   // -> 1.0.0
```

استيراد حاوية غير موجودة يُنتج تشخيصاً (diagnostic) نظيفاً بنفس أسلوب بقية
اللغة (`E0028`)، وليس تعطّلاً صامتاً:

```
error[E0028]: library.import: تعذّرت قراءة الحاوية 'does_not_exist.rcl'
  --> bad_import.rin:1:1
 1 | libraryImport("does_not_exist.rcl");
   | ^
reason:
  cannot open container: does_not_exist.rcl
```

## natives المُسجَّلة فعلياً (registerNatives()/invokeCallee() في rin_interpreter.cpp)

| Native | يحتاج env؟ | الوصف |
|---|---|---|
| `clcContainerOpen(path)` | لا | يفتح حاوية `.rcl`، يعيد handle رقمي |
| `clcContainerClose(handle)` | لا | يغلق handle مفتوحاً |
| `clcContainerFileCount(handle)` | لا | عدد الإدخالات داخل الحاوية |
| `clcContainerFileName(handle, i)` | لا | مسار الإدخال رقم i |
| `clcContainerMetaName(handle)` / `clcContainerMetaVersion(handle)` | لا | حقول Metadata |
| `libraryImport(path)` | **نعم** | يستخرج وينفّذ كل ملفات `.rin` داخل الحاوية في نطاق النداء، يعيد array بأسمائها |
| `libraryExport(outPath, srcDir)` | لا | يبني حاوية `.rcl` من مجلد، يعيد الحجم المضغوط |

## ملاحظة أمان (لم تتغيّر عن النسخة الأصلية أدناه)

`library.import` من حاوية `.rcl` غير موثوقة يمنح ملفاتها نفس ثقة أي
`@import` عادي في المفسّر — بلا أي sandbox إضافي على ما تفعله تلك الملفات
*بعد* التنفيذ (نفس القيد الموثَّق أصلاً عند `@import` في
`rin_interpreter.cpp`، سطر ~2439). مسار الحاوية نفسه (وليس ما بداخلها بعد
التنفيذ) يمرّ عبر `resolvePath()` فيُمنَع من الخروج خارج `basePath` المعزول
حين يكون هذا الأخير مضبوطاً (كما في تطبيق Android)؛ على `cli/linux` حيث
`basePath` فارغ دائماً، لا يوجد عزل مسارات أصلاً (سلوك موجود مسبقاً، غير
خاص بـ CLC).

## ملاحظة توافق مع RinPM (Package Manager)

نسخة الشجرة التي طُبِّق عليها هذا الدمج (2) تتضمّن أيضاً نظام إدارة حزم
جديد (`cli/linux/src/pkg/`، RinPM) له نقطة تكامل خاصة به داخل
`rin_interpreter.cpp` (حل `@import` عبر حزم مثبَّتة، دوال قرب السطر ~2630
و~3850). هذا **منفصل تماماً** عن natives CLC ولا يتقاطع معها: تم البناء
والاختبار الفعلي مع تفعيل كلا النظامين معاً (RinPM + CLC) بنجاح، بما في ذلك
التأكد من أن `rin pkg --help` وبقية أوامر RinPM تعمل كما هي دون أي تغيير.





## لماذا هذا التصميم تحديداً؟

بدل إعادة تطبيق قراءة/كتابة `.rin` أو منطق `@import` من الصفر داخل CLC،
الفكرة: CLC يبقى **مكتبة C++ مستقلة تماماً** (كما هي الآن في `src/`)، ويُربَط
بالمفسّر عبر **طبقة رقيقة جداً** من natives تستدعي دوال `clc::` مباشرة —
بالضبط نفس النمط المستخدَم فعلياً لـ `crc32`/`zlibDeflateRaw` في الكود
الحالي (`app/src/main/cpp/rin_interpreter.cpp`, سطور ~813 وما بعدها).

## خطوات الدمج الفعلي (عند التنفيذ لاحقاً)

1. أضف `src/*.cpp` (عدا `cli/`) لقائمة المصادر في `CMakeLists.txt` الخاص
   بـ `app/src/main/cpp` (أو `cli/linux/CMakeLists.txt` للنسخة السطحية)،
   بجانب `rin_lexer.cpp`/`rin_parser.cpp`/`rin_interpreter.cpp` الحاليين.
2. أضف `#include "clc_container.h"` في أعلى `rin_interpreter.cpp`.
3. أضف الكتلة أدناه ضمن نفس الدالة التي تُسجِّل بقية الـ `natives[...]`
   (حيث `natives["crc32"] = ...` معرَّفة حالياً).

```cpp
// ---- CLC: Rin Compact Library Container — .rcl (natives خام فوق clc::) ----
// container.open(path) -> handle رقمي (index في جدول عالمي)، أو -1 عند الفشل.
natives["clcContainerOpen"] = [](std::vector<Value>& a, int line) -> Value {
    expectArgs("clcContainerOpen", a, 1, line);
    std::string path = asString(a[0], "clcContainerOpen", line);
    try {
        auto info = clc::readContainerInfo(path);
        int handle = registerOpenContainer(std::move(info), path); // جدول عالمي بسيط، انظر أدناه
        return Value::num(double(handle));
    } catch (const clc::ClcFormatError& e) {
        throw diagErr(diag::Code::E0035_RuntimeError, line, std::string("container.open: ") + e.what());
    }
};

// container.close(handle) -> بلا قيمة إرجاع مفيدة (يحرر الموارد فقط).
natives["clcContainerClose"] = [](std::vector<Value>& a, int line) -> Value {
    expectArgs("clcContainerClose", a, 1, line);
    int handle = int(asNumber(a[0], "clcContainerClose", line));
    closeOpenContainer(handle);
    return Value::nil();
};

// library.import(path) -> يستخرج كل ملفات .rin من الحاوية إلى الذاكرة، ثم
// يمرّرها لنفس مسار @import الموجود فعلياً (parseLibrary الداخلي) — إعادة
// استخدام كاملة لمنطق التنفيذ الحالي، CLC فقط يوفّر البايتات الخام.
natives["libraryImport"] = [this](std::vector<Value>& a, int line) -> Value {
    expectArgs("libraryImport", a, 1, line);
    std::string rclPath = asString(a[0], "libraryImport", line);
    try {
        auto info = clc::readContainerInfo(rclPath);
        for (auto& f : info.files) {
            if (f.flags & clc::FILE_FLAG_IS_DIR) continue;
            if (f.path.size() < 4 || f.path.compare(f.path.size()-4, 4, ".rin") != 0) continue;
            // نستخرج كل ملف .rin على حدة (بلا فك المشروع كاملاً على القرص).
            std::string tmpOut = makeTempDir(); // مسار مؤقت حقيقي عبر أداة مساعدة موجودة/تُضاف
            clc::extractOneFile(rclPath, f.path, tmpOut);
            std::string src; readFileUtf8(tmpOut + "/" + f.path, src); // دالة قراءة موجودة أصلاً
            executeImportedSource(src, rclPath + "::" + f.path, line); // نفس المسار الداخلي لـ @import
        }
    } catch (const clc::ClcFormatError& e) {
        throw diagErr(diag::Code::E0028_ImportError, line, std::string("library.import: ") + e.what());
    }
    return Value::nil();
};

// library.export(outPath, sourceDir) -> يبني حاوية CLC من مجلد (تغليف packDirectory مباشرة).
natives["libraryExport"] = [](std::vector<Value>& a, int line) -> Value {
    expectArgs("libraryExport", a, 2, line);
    std::string outPath = asString(a[0], "libraryExport", line);
    std::string srcDir  = asString(a[1], "libraryExport", line);
    try {
        clc::PackOptions opts; // القيم الافتراضية (level=2) كافية هنا؛ يمكن توسيعها لاحقاً بوسائط إضافية
        auto stats = clc::packDirectory(srcDir, outPath, opts);
        return Value::num(double(stats.compressedSize));
    } catch (const clc::ClcFormatError& e) {
        throw diagErr(diag::Code::E0035_RuntimeError, line, std::string("library.export: ") + e.what());
    }
};
```

4. صياغة `container.open "x.rcl"` (بلا أقواس، كما في مثال المتطلبات الأصلي)
   بدل `container.open("x.rcl")` تتطلّب إضافة قاعدة نحوية صغيرة في
   `rin_parser.cpp` (نفس ما تم فعلاً مع `@import "..."`) — تحويلها إلى نداء
   `clcContainerOpen(...)` عادي أثناء التحليل. **غير مُنفَّذ هنا** لأنه تعديل
   في الـ parser يحتاج اختباراً كاملاً مقابل بقية اللغة، خارج نطاق هذا
   التسليم — لكنه تعديل صغير ومحدود النطاق إن أُريد لاحقاً.

## ملاحظة أمان مهمة عند الدمج الفعلي

نفس الكود الحالي في `rin_interpreter.cpp` (السطر ~2439 في نسخة المشروع
المرفوعة) يذكر صراحة أن `@import` من مصدر غير موثوق يمكنه القراءة/الكتابة
بلا رادع حالياً — أي دمج فعلي لـ `library.import` من حاوية `.rcl` خارجية
يجب أن يمر عبر **نفس** سياسة الصلاحيات المستقبلية المخطَّطة لتلك النقطة
(sandbox المسارات)، وليس فقط حماية CLC الداخلية (Path Traversal إلخ، والتي
تحمي *فك* الحاوية نفسها، لا ما يحدث *بعد* تنفيذ الكود المُستورَد).

# ربط CLC بمفسّر Rin (`rin_interpreter.cpp`) — تصميم جاهز للتنفيذ

**حالة هذا المستند:** تصميم مكتمل وكود C++ جاهز للنسخ، لكنه **غير مُدمَج ولا
مُختبَر فعلياً** داخل شجرة `app/src/main/cpp` الحقيقية في هذا التسليم — لأن
دمجه يتطلّب إعادة بناء تطبيق Android/CLI الكامل والتحقق من عدم كسر أي شيء
موجود، وهو خارج ما يمكن التحقق منه فعلياً هنا. **هذا موثَّق بصراحة بدل
الادّعاء بأنه يعمل.**

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

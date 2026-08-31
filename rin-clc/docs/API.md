# API — الواجهة البرمجية

## 1. واجهة C++ (متاحة الآن، مُختبَرة)

المصدر: [`src/clc_container.h`](../src/clc_container.h). كل شيء في
`namespace clc`.

```cpp
#include "clc_container.h"
using namespace clc;

PackOptions opts;
opts.level = Level::L2;
opts.metadata.name = "math";
opts.metadata.version = "1.2.0";
opts.dependencies.push_back({"core", "^2.0.0"});

PackStats stats = packDirectory("./my_project", "my_project.rcl", opts);
// stats.originalSize / compressedSize / fileCount / blockCount / dedupedFiles / packSeconds

UnpackStats u = unpackContainer("my_project.rcl", "./out");
extractOneFile("my_project.rcl", "src/main.rin", "./out_single");

ContainerInfo info = readContainerInfo("my_project.rcl"); // بنية فقط، بلا فك بيانات
CheckReport quick = checkContainer("my_project.rcl");      // crc32 سريع
CheckReport full  = verifyContainer("my_project.rcl");     // sha256 كامل
```

كل الدوال ترمي `clc::ClcFormatError` (مُشتقّة من `std::runtime_error`) عند أي
خطأ بنيوي/تلف/انتهاك أمني — لا توجد قيم إرجاع صامتة عند الفشل.

## 2. واجهة Rin المستقبلية (تصميم، غير مُفعَّلة بعد في المفسّر)

الغرض المطلوب صراحة:

```rin
library.import "math.rcl";
library.export "graphics.rcl";
container.open "library.rcl";
container.close();
```

**لماذا لم تُفعَّل داخل `rin_interpreter.cpp` في هذا التسليم؟** لأن ربطها
الفعلي يتطلّب إعادة بناء واختبار تطبيق Android/CLI الكامل (وهو خارج نطاق ما
يمكن التحقق منه هنا فعلياً)، بينما طُلب صراحة عدم الادّعاء وتقديم عمل حقيقي
فقط. البديل الصادق: تصميم **جاهز للنسخ واللصق** يتّبع بالضبط نفس نمط الدوال
الأصلية الموجودة فعلاً (`natives["crc32"] = ...`) — انظر
[`RIN_INTEGRATION.md`](RIN_INTEGRATION.md) للكود الكامل ولخطوات الربط.

ملخّص الخريطة المقترحة (Rin ↔ C++):

| دالة Rin | تُترجَم إلى |
|---|---|
| `container.open(path)` | `clc::readContainerInfo` + الاحتفاظ بمقبض (handle) في جدول عالمي داخل المفسّر |
| `container.close()` | تحرير المقبض الحالي |
| `library.import(path)` | `clc::extractOneFile` لكل ملف `.rin` داخل الحاوية ثم تمريره لنفس مسار `@import` الموجود فعلياً (`parseLibrary`/`fromEmbedded` في `rin_interpreter.cpp`) |
| `library.export(path, files[])` | `clc::packDirectory` على مجلد مؤقت يُبنى من قيم Rin (Array/Object) |

هذا يعيد استخدام **كل** منطق استيراد/تنفيذ المكتبات الموجود فعلياً في
`rin_interpreter.cpp` (سطور ~3617 وما بعدها في نسخة المشروع وقت كتابة هذا
التسليم) بلا تكراره — CLC فقط يُوفِّر البيانات الخام (bytes) التي كانت تأتي
سابقاً من نظام الملفات العادي أو من `EMBEDDED_LIBS`.

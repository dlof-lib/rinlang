# RINZIP v2 — ضغط ZIP حقيقي (Deflate عبر zlib native)

تم اختبار كل هذا فعلياً: بناء + تشغيل + قراءة بـ `unzip` النظامي + قراءة أرشيف
أنشأه أمر `zip` الحقيقي — انظر تفاصيل الاختبار في الرد المرافق.

## الملفات في هذه الحزمة (انسخها فوق نفس المسارات في rinlang-main)

- `lib/rinzip.og.rin` — المكتبة الجديدة (تُضغط تلقائياً بـ Deflate، وتتراجع إلى
  Store تلقائياً إن لم يكن الضغط مفيداً). إضافات: `rzFileEntryStored()` لإجبار
  Store صراحة (للمحتوى الثنائي المضغوط أصلاً كالصور)، وحقول `originalBytes`/
  `ratio` في نتيجة `rzSaveArchive`.
- `app/src/main/cpp/rin_stdlib_libs.h` — نفس المكتبة أعلاه مُضمَّنة (النسخة
  المطابقة تماماً، طبقاً لتعليق الملف الأصلي "نُسخة طبق الأصل").
- `app/src/main/cpp/rin_interpreter.cpp` — أضاف native جديدين فقط:
  `zlibDeflateRaw(bytes)` و`zlibInflateRaw(compressed, expectedSize)`، بضغط
  DEFLATE خام (windowBits=-15) مطابق تماماً لما تتوقّعه صيغة ZIP الرسمية
  (method=8). أُضيف أيضاً `#include <zlib.h>`.
- `app/src/main/cpp/CMakeLists.txt` — ربط `libz` (متوفّرة دوماً في NDK sysroot،
  لا حاجة لأي تنزيل/توريد) لهدفي `rinengine` و`rincheck`.
- `cli/linux/CMakeLists.txt`, `cli/macos/CMakeLists.txt`,
  `cli/windows/CMakeLists.txt`, `bindings/CMakeLists.txt` — نفس الربط عبر
  `find_package(ZLIB REQUIRED)` القياسي لبناء سطر الأوامر خارج أندرويد
  (لينكس/macOS تأتي بـ zlib النظام عادة؛ ويندوز يحتاج vcpkg — ملاحظة داخل الملف).

## لا حاجة لأي تعديل آخر
لا JNI جديد، لا تبعيات خارجية، لا تغيير على `jni_bridge.cpp` أو Kotlin —
الإضافة native خالصة داخل نفس `rin_interpreter.cpp` بنفس أسلوب `natives[...]`
الموجود أصلاً (crc32/adler32/readFile...)، وlibz مكتبة نظام موجودة أصلاً على كل
جهاز أندرويد عبر NDK.

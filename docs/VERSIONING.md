# نظام الإصدار الرسمي لـ Rin

هذا المستند يشرح كيف يُرقَّم Rin، ولماذا بعض مكوّناته مستقلة عن هذا الترقيم
عمداً. قبل هذا المستند لم يكن هناك مصدر وحيد: كل واجهة كانت تحمل رقمها
الخاص المنسوخ يدوياً، وانحرفت عن بعضها فعلياً (مثال حقيقي كان موجوداً قبل
توحيدها: `rin --version` كان يطبع `0.2.0` على لينكس، و`0.1.0` على macOS
وويندوز، لنفس أداة CLI بالضبط، بينما التطبيق كان يعرض `Rin Engine 1.1` من
مكانين مختلفين في الكود).

## المصدر الوحيد: `rin_version.h`

[`app/src/main/cpp/rin_version.h`](../app/src/main/cpp/rin_version.h) هو
**المصدر الرسمي الوحيد** لرقم إصدار محرّك Rin (lexer/parser/interpreter/C
API). يُعرِّف:

```cpp
#define RIN_VERSION_MAJOR 1
#define RIN_VERSION_MINOR 0
#define RIN_VERSION_PATCH 0
#define RIN_VERSION_STRING "1.0.0"
#define RIN_VERSION_EDITION "2026"
```

هذا الرقم يظهر **حرفياً بنفس القيمة** في كل واجهة مبنية فوق نفس ملفات
المحرّك (`rin_lexer.cpp`/`rin_parser.cpp`/`rin_interpreter.cpp`)، لأنها كلها
تُضمِّن `rin_version.h` وتقرأ `RIN_VERSION_STRING` بدل تعريف رقمها الخاص —
**بما في ذلك كود Rin نفسه وقت التشغيل**، عبر دالتين مُضمَّنتين في المفسّر:
`rinVersion()` و`rinEdition()` (مسجَّلتان في `registerNatives()` داخل
`rin_interpreter.cpp`، وقابلتان للاستدعاء من أي سكربت `.rin`):

```rin
print "الإصدار: " + rinVersion();   // "1.0.0"
print "الدورة: " + rinEdition();     // "2026"
```

| الواجهة | أين يظهر الرقم |
|---|---|
| تطبيق أندرويد | `jni_bridge.cpp` → `RinEngine.engineVersion()`، و`app/build.gradle` → `versionName` (يُقرأ من ملف `VERSION`، انظر تحت) |
| مكتبة الربط للغات أخرى (`bindings/`) | `rin_c_api.cpp` → `rin_engine_version()` |
| CLI (لينكس/macOS/ويندوز) | `cli/*/src/main.cpp` → `rin --version` |
| كود Rin نفسه (سكربتات `.rin`) | الدالتان المُضمَّنتان `rinVersion()`/`rinEdition()` — تعملان بنفس النتيجة سواء نُفِّذ السكربت بالمفسّر (`rin_interpreter.cpp`) أو تُرجِم بـ`rinc` إلى تنفيذي أصلي (انظر التحذير عن `rinc` تحت) |

ملف [`VERSION`](../VERSION) في جذر المستودع يجب أن يطابق `RIN_VERSION_STRING`
**حرفياً** دائماً — هو النسخة القابلة للقراءة من أدوات خارج C++ (حالياً:
`app/build.gradle` يقرأه مباشرة لحساب `versionName`/`versionCode`؛ مناسب أيضاً
لأي سكربت CI مستقبلي).

## عند رفع الإصدار (checklist)

1. عدّل `RIN_VERSION_MAJOR`/`MINOR`/`PATCH`/`STRING` في `rin_version.h` فقط.
2. اجعل محتوى `VERSION` مطابقاً حرفياً للقيمة الجديدة (سطر واحد، بلا `v` أو
   مسافات، مثال: `1.2.0`).
3. أضف فقرة جديدة في [`CHANGELOG.md`](../CHANGELOG.md) (حوّل `[Unreleased]`
   الحالية إلى `[1.2.0] - YYYY-MM-DD` وابدأ قسم `[Unreleased]` جديد فوقها).
4. لا تلمس `jni_bridge.cpp`/`rin_c_api.cpp`/أي `cli/*/src/main.cpp` — كلها
   تقرأ الرقم تلقائياً من `rin_version.h` عبر `#include`.
5. **استثناء يدوي وحيد:** حدِّث السطرين الحرفيّين
   `rt_native_rinVersion`/`rt_native_rinEdition` في **كلتا** نسختَي `rinc.cpp`
   (`app/src/main/cpp/rinc.cpp` و`compiler/rinc.cpp`) يدوياً ليطابقا القيمة
   الجديدة. هذان الملفان **لا** يُضمِّنان `rin_version.h` عمداً (يجب أن يبقى
   كل منهما ملفاً واحداً قائماً بذاته يُبنى بـ `g++ -o rinc compiler/rinc.cpp`
   بلا أي اعتماد خارجي، انظر `compiler/README.md`) — هذه هي الحالة الوحيدة في
   كل هذا النظام التي فيها نسخ يدوي حقيقي للرقم، لذا لا تنسها.

`versionCode` في أندرويد يُحسَب تلقائياً من `VERSION` بالصيغة
`major*10000 + minor*100 + patch` (مثال: `1.0.0` → `10000`) — لا حاجة لرفعه
يدوياً طالما رفعت `VERSION`.

## ما هو خارج هذا النظام عمداً

الترقيم الموحّد أعلاه يغطي **محرّك اللغة نفسه** فقط. ثلاثة أشياء أخرى في
المستودع لها ترقيم مستقل بتصميم متعمّد، وليس إهمالاً:

- **`rinc`** (المترجِم الأصلي الجزئي، `compiler/rinc.cpp` ونسخته المطابقة
  `app/src/main/cpp/rinc.cpp`): أداة مطوّر منفصلة تُبنى على جهاز التطوير عبر
  `g++`/`clang` مباشرة، ولا تدخل في `libRinengine.so` (لا تُضاف إلى أي
  `CMakeLists.txt` هناك). تغطّي عمداً حالياً مجموعة فرعية فقط من اللغة
  (procedural core، بدون `container.pipe`/`api`/`table`/... بعد) — لذلك رقمها
  `rinc 0.5.0` يعكس تقدّم هذا الـ backend الجزئي تحديداً، لا نضج اللغة ككل.
- **مكتبات `.rin` المُضمَّنة** مثل `lib/movingmask.og.rin`: لها رقم إصدار
  خاص بها موثَّق في `CHANGELOG.md` (مثال: "v1.2.0")، منفصل عن إصدار المحرّك
  الذي يشغّلها — نفس المحرّك (إصدار واحد) قد يشغّل عدة إصدارات من نفس
  المكتبة بمرور الوقت.
- **حزم RinPM** المثبَّتة عبر `rin pkg install` (`cli/linux/src/pkg/semver.*`):
  لكل حزمة SemVer خاص بها يديره ناشرها، لا علاقة له بإصدار محرّك Rin الذي
  يشغّل تلك الحزمة.

## لماذا SemVer

`MAJOR.MINOR.PATCH`:
- **MAJOR**: تغيير غير متوافق للخلف في بنية اللغة أو الـ C ABI
  (`rin_c_api.h`) — سكربت أو برنامج رابط قديم قد يتوقف عن العمل.
- **MINOR**: ميزة لغة جديدة أو دالة C API جديدة، متوافقة للخلف بالكامل.
- **PATCH**: إصلاح خلل بدون أي تغيير في السلوك الموثَّق.

`EDITION` ("2026" حالياً) اسم تسويقي/دوري منفصل تماماً عن SemVer، يظهر
بجانب الرقم في `rin --version` — يمكن أن يبقى ثابتاً عبر عدة إصدارات MINOR/
PATCH متتالية.

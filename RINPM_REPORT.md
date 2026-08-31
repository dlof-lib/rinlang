# RinPM — تقرير التسليم (مُحدَّث ليتوافق مع rinlang-main__26_.zip)

**تحديث هذا الإصدار:** نفس تكامل RinPM بالضبط، أُعيد تطبيقه على قاعدة الكود
الجديدة (`rinlang-main__26_.zip`) التي أضافت ميزة "Object literal" الجديدة
(`.object("id") ... .end/object`) وتغييرات في `rin_ast.h`/`rin_parser.cpp`/
`rin_interpreter.cpp`/Loom/واجهة أندرويد. تم التحقق أن:

- منطقة `@import` (حيث يتكامل RinPM) ومنطقة `resolvePath`/`ensureParentDir`
  في `rin_interpreter.cpp`/`.h` **متطابقة حرفياً** بين النسختين القديمة
  والجديدة (نفس أرقام الأسطر تقريباً)، فأُعيد تطبيق نفس الرقعة (patch) بلا أي
  تعديل على منطقها.
- `cli/linux/src/main.cpp` و`cli/linux/CMakeLists.txt` في النسخة الجديدة
  **مطابقان تماماً** لنسخة `rinlang-main__13_` الأصلية (لم يمسّهما أي تغيير
  من المطوّر)، فنُسِخت نفس النسخة المعدَّلة منهما مباشرة دون حاجة لأي دمج يدوي.
- أُعيد بناء الثنائي كاملاً من قاعدة الكود الجديدة (تتضمّن `rin_ast.h` الجديد
  و`rin_parser.cpp` الجديد وميزة الكائنات الجديدة) **بنجاح وبدون أي خطأ ترجمة**.
- أُعيد تشغيل نفس سيناريو القبول الكامل (init → publish → add → install →
  @import → run) على الثنائي الجديد ونجح بنفس النتيجة تماماً.
- تحقّقتُ أيضاً أن مثال الميزة الجديدة نفسه (`examples/object_literal_demo.rin`)
  ما زال يعمل بشكل صحيح بعد الدمج (لا تعارض بين الميزتين).

كل الأقسام التالية من التقرير الأصلي تبقى صحيحة دون تغيير في المضمون.

---

# RinPM — تقرير التسليم

هذا التسليم يحتوي **فقط** على الملفات التي أُنشئت أو عُدِّلت لإضافة RinPM
(Rin Package Manager) إلى مشروع Rin. طبّق الملفات في نفس المسارات النسبية
داخل نسخة `rinlang-main` لديك (استبدال/إضافة).

## 1) الملفات المُنشأة (جديدة بالكامل)

كل شيء تحت `cli/linux/src/pkg/`:

| الملف | الدور |
|---|---|
| `errors.h` | هرم أخطاء RinPM + exit codes (0..5) |
| `semver.h/.cpp` | تحليل ومطابقة SemVer (كان مبنياً مسبقاً، مُدمَج هنا كما هو) |
| `toml_lite.h/.cpp` | قارئ/كاتب TOML مبسَّط (كان مبنياً مسبقاً، مُدمَج هنا كما هو) |
| `sha256.h/.cpp` | SHA-256 (كان مبنياً مسبقاً، مُدمَج هنا كما هو) |
| `json_lite.h/.cpp` | JSON صغير مستقل، خاص ببروتوكول HttpRegistry فقط |
| `manifest.h/.cpp` | قراءة/كتابة/تحقق `rin.toml` |
| `lockfile.h/.cpp` | قراءة/كتابة `rin.lock` |
| `archive.h/.cpp` | تنسيق أرشيف `.rinpkg` بسيط وقابل للتحقق (pack/unpack) |
| `cache.h/.cpp` | تخطيط `~/.rin/{cache,registry,packages,config}` وحساب الجذر لكل منصة |
| `registry.h/.cpp` | تجريد `Registry` + `LocalRegistry`/`FileRegistry` (تعمل بالكامل بلا شبكة) + `HttpRegistry` (عميل حقيقي عبر `rin_http.cpp`، غير مُختبَر بلا شبكة حية) |
| `resolver.h/.cpp` | Dependency Resolver: تقاطع القيود، اختيار الإصدار، كشف الدورات، كشف تعارض الإصدار |
| `package_ops.h/.cpp` | Validator / Builder / Installer / Publisher |
| `auth.h/.cpp` | تخزين جلسة Token فقط (0600)، بلا كلمات مرور مطلقاً |
| `cli_pkg.h/.cpp` | منفّذ كل أوامر `rin pkg <command>` |

## 2) الملفات المُعدَّلة

| الملف | التعديل |
|---|---|
| `cli/linux/src/main.cpp` | استدعاء `rinpm::cli::run(...)` عند `rin pkg ...`، وإضافة سطر مساعدة |
| `cli/linux/CMakeLists.txt` | إضافة كل ملفات `src/pkg/*.cpp` إلى هدف البناء `rin` |
| `app/src/main/cpp/rin_interpreter.h` | إعلان `Interpreter::tryLoadInstalledPackageEntry(...)` |
| `app/src/main/cpp/rin_interpreter.cpp` | نقطة التكامل الوحيدة مع `@import`: بعد فشل المكتبات المدمجة و`lib/`، يُبحث عن الاسم في `rin.lock` ثم في كاش RinPM العالمي |

**ملاحظة معمارية مهمة:** محرك اللغة (`rin_interpreter.cpp`) **لا يتضمن** أي رأس من
`pkg/*` — التكامل عبر دالة صغيرة ذاتية الاكتفاء تقرأ `rin.lock` نصياً فقط، حفاظاً
على استقلالية محرك اللغة (يُبنى أيضاً على أندرويد/macOS/ويندوز حيث RinPM Linux
CLI غير موجود إطلاقاً). هذا يطابق قسم 26 من الطلب: "Resolver طبقة منفصلة تغذّي
Interpreter و Compiler".

## 3) البنية الناتجة

```
~/.rin/
├── cache/      أرشيفات .rinpkg الخام المُنزَّلة
├── registry/local/<name>/<version>/{meta.toml,<name>-<version>.rinpkg}
├── packages/<name>/<version>/{rin.toml,src/…,.checksum}
└── config/credentials      (token فقط، صلاحيات 0600)
```

المشروع نفسه:
```
my-app/
├── rin.toml
├── rin.lock
├── src/main.rin
├── tests/
└── packages/README.md     (إعلامي فقط — لا تخزين فعلي هنا، عمداً، انظر قسم 3 من الطلب)
```

## 4) أوامر CLI المُنفَّذة فعلياً

`init add remove install update upgrade search info list tree build publish
clean login logout whoami --help --version` — كلها منفَّذة بمنطق حقيقي (لا طباعة
وهمية)، وأُختبِرت جميعها تشغيلياً (انظر §6).

Exit codes: `0` نجاح، `1` عام، `2` مانيفست غير صالح، `3` تعارض تبعيات (دورة أو
تعارض إصدار)، `4` حزمة غير موجودة، `5` فشل أمني/checksum — كلها مطابقة تماماً
لما طُلب.

## 5) rin.toml / rin.lock — كما طُلب حرفياً

`rin.toml` يدعم `[package]`, `[dependencies]`, `[dev-dependencies]`,
`[package.rin]`, `[package.platforms]` بالضبط كما في المواصفة، مع رسائل خطأ
واضحة عند أي حقل ناقص أو غير صالح (مُختبَر: اسم فارغ، إصدار غير صالح، قيد
إصدار غير صالح، منصة غير معروفة).

`rin.lock` يحتوي `[[package]] name/version/source/checksum/dependencies` تماماً
كما طُلب.

## 6) الاختبارات التي نُفِّذت فعلياً (لا افتراضات)

كل ما يلي شُغِّل فعلياً على الملف الثنائي المبني `rin` وأعطى نفس النتائج المذكورة:

1. **السيناريو الكامل من قسم 29**: `rin pkg init my-app` → بناء حزمة `greetings`
   ونشرها إلى LocalRegistry فعلياً → `rin pkg add greetings` (يحل وينزّل ويثبّت
   ويكتب rin.lock تلقائياً) → `@import "greetings";` → `rin run` ينفّذ
   `greet("RinPM")` بنجاح فعلياً.
2. **Diamond dependency** (مطابق تماماً لمثال قسم 6: A يعتمد B(^1.0) وC(^2.0)،
   وB يعتمد C(^2.1)) → اختار الحل C 2.5.0 (أعلى إصدار يحقق كِلا القيدين) بشكل صحيح.
3. **Circular dependency** حقيقي (A→B→C→A عبر 3 حزم منشورة فعلياً) → رسالة الخطأ
   بنفس شكل الشجرة المطلوب حرفياً في قسم 6، ورمز خروج 3.
4. **Version conflict** حقيقي (حزمتان تطلبان `json ^1.0.0` و`json ^3.0.0`) →
   رسالة خطأ بنفس تنسيق قسم 25 تقريباً، ورمز خروج 3.
5. **Checksum mismatch أمني حقيقي**: تلاعب فعلي ببايتات الأرشيف في الـ Registry
   → رُفِض التثبيت، `RinChecksumError`، ورمز خروج 5. بعد استعادة الأرشيف السليم
   نجح التثبيت مباشرة.
6. **Package not found** حقيقي → `RinPackageNotFound`، رمز خروج 4.
7. **Invalid manifest** حقيقي (بلا `version`) → `RinManifestError`، رمز خروج 2.
8. `list` / `tree` / `info` / `search` / `login` / `whoami` — كلها اختُبِرت
   وأعطت مخرجات صحيحة مطابقة للبيانات الفعلية في الكاش/الـ Registry.
9. الكاش يعمل فعلياً: إعادة `rin pkg install` بعد حذف `src/` فقط (مع بقاء
   الأرشيف المضغوط) أعاد الاستخدام دون إعادة تنزيل.

## 7) المشاكل التي اكتُشِفت وأُصلِحت أثناء التطوير (بصدق، لا إخفاء)

- **خلل ازدواج المسار عند فك الأرشيف**: كان `installer` يفك الأرشيف داخل
  `<installDir>/src` رغم أن الأرشيف نفسه يحوي مجلد `src/` داخلي، فيصبح المسار
  الفعلي `<installDir>/src/src/lib.rin` ولا يجده `@import` أبداً. أُصلِح بفك
  الأرشيف مباشرة في `<installDir>` (اكتُشِف وأُصلِح عبر اختبار §6.1 الفعلي).
- **خلل جوهري في نشر قيود التبعيات المتعدية**: `PackageMeta` كانت تُخزِّن أسماء
  التبعيات فقط بلا قيود الإصدار الحقيقية، فاستُبدلت داخلياً بقيد `"*"` دائماً —
  هذا كان يُسقِط أي تعارض إصدار حقيقي بصمت (اختبار §6.4 فشل أول مرة: النظام
  اختار `json 3.0.0` رغم أن حزمة أخرى تطلب `json ^1.0.0` بشكل صريح). أُصلِح
  بإضافة القيد الحقيقي إلى `PackageMeta` وكتابته/قراءته من `meta.toml`
  والنشر، وأُعيد الاختبار وأعطى النتيجة الصحيحة (رفض بتعارض إصدار).

كلا الخللين اكتُشِفا فقط لأن الاختبارات نُفِّذت فعلياً على ثنائي مبني حقيقي، لا
افتراضاً — وهذا هو الهدف من قسم 22 من الطلب.

## 8) ما لم يُنفَّذ في هذا التسليم، ولماذا (بصراحة)

- **خادم HTTP Registry فعلي**: `HttpRegistry` بُني كعميل حقيقي فوق `rin_http.cpp`
  الموجود مسبقاً في المشروع، لكن **لم يُختبَر** لأن بيئة التطوير هنا بلا شبكة،
  ولا يوجد خادم `registry.rinlang.org` فعلي لأختبر ضده. البروتوكول موثَّق داخل
  `registry.cpp` وقابل للتفعيل الكامل فور توفر خادم متوافق.
- **Android/JNI**: لم أُدمِج RinPM في `app/src/main/cpp` (تطبيق أندرويد) لأن ذلك
  يحتاج بيئة بناء Gradle/NDK غير متاحة هنا، وموقع تخزين مختلف تماماً (Termux
  مقابل تطبيق مُقيَّد بصلاحيات Android). كود `cache.cpp` يدعم فعلاً اكتشاف بيئة
  Android/Termux عبر `RIN_HOME`/`ANDROID_DATA`، لكن ربط CLI بواجهة الأندرويد لم
  يُنفَّذ.
- **cli/macos و cli/windows**: نفس منطق `cli/linux` قابل للنسخ إليهما (الكود
  مستقل عن نظام التشغيل باستثناء استخدام `<unistd.h>`/`<dirent.h>` التي تحتاج
  بديلاً على ويندوز)، لكن لم أُطبِّقه فعلياً في هذا التسليم لضيق الوقت.
- **Workspace متعدد الحزم** (قسم 19): لم يُنفَّذ حتى كهيكل أولي؛ البنية الحالية
  (Registry/Resolver/Cache كطبقات منفصلة) لا تمنع إضافته لاحقاً.
- **توثيق `docs/package-manager/*`**: لم أكتب ملفات التوثيق الأربعة عشر
  المطلوبة في قسم 24 في هذا التسليم — أولوية الوقت ذهبت لتنفيذ واختبار الكود
  الفعلي. أستطيع كتابتها في تسليم لاحق إن رغبت.
- **Workspace/Backtracking resolver كامل**: الـ Resolver الحالي يحل بخوارزمية
  تراكمية (fixpoint) تتعامل بشكل صحيح مع كل الحالات المختبرة (بما فيها diamond
  dependencies) لكنها ليست SAT-solver كاملاً؛ حالات مرضية جداً (تحتاج تراجعاً
  فعلياً عن اختيار إصدار سابق) قد لا تُحل تلقائياً بل ستُبلَّغ كتعارض إصدار.

## 9) طريقة البناء والتشغيل يدوياً (بلا CMake، لأنه غير متاح هنا كذلك)

```bash
cd cli/linux
CORE=../../app/src/main/cpp
g++ -std=c++17 -O2 -I "$CORE" \
  src/main.cpp \
  "$CORE"/rin_lexer.cpp "$CORE"/rin_parser.cpp "$CORE"/rin_interpreter.cpp "$CORE"/rin_http.cpp \
  "$CORE"/diagnostics/diagnostic.cpp "$CORE"/diagnostics/source_manager.cpp \
  "$CORE"/diagnostics/diagnostic_engine.cpp "$CORE"/diagnostics/diagnostic_renderer.cpp \
  src/pkg/semver.cpp src/pkg/toml_lite.cpp src/pkg/sha256.cpp src/pkg/json_lite.cpp \
  src/pkg/manifest.cpp src/pkg/lockfile.cpp src/pkg/archive.cpp src/pkg/cache.cpp \
  src/pkg/registry.cpp src/pkg/resolver.cpp src/pkg/package_ops.cpp src/pkg/auth.cpp \
  src/pkg/cli_pkg.cpp \
  -lz -o rin
```

(أو عبر `cmake`/`make` العاديين إن كان `cmake` متاحاً لديك؛ `CMakeLists.txt`
المرفق مُحدَّث بالفعل بكل ملفات `pkg/`.)

```bash
./rin pkg init my-app && cd my-app
./rin pkg add json     # مثال: يفترض وجود حزمة "json" في LocalRegistry لديك
./rin run src/main.rin
```

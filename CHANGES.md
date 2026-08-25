# ملفات مُعدَّلة / مُنشأة حتى الآن — نظام API حقيقي

هذه ليست المشروع كاملاً، فقط الملفات التي لمستها حتى الآن (المسارات كما هي داخل رينلانغ، انسخها
فوق نفس المسارات في نسختك من المشروع):

## ملفات جديدة (Real HTTP layer)
- `app/src/main/cpp/rin_http.h`   — تعريف عميل HTTP حقيقي (HttpResult + performRequest + نقطة حقن جسر أندرويد)
- `app/src/main/cpp/rin_http.cpp` — التنفيذ الفعلي: curl حقيقي عبر fork/exec (لينكس/macOS) أو _popen (ويندوز)، وجسر Android مُهيَّأ لاحقاً من jni_bridge.cpp
- `app/src/main/cpp/rin_json.h`   — محوّل Value <-> JSON حقيقي (encode/decode فعليين، وليس تمثيل عرض فقط)

## ملفات مُعدَّلة
- `app/src/main/cpp/rin_interpreter.h`  — أُضيف: `struct ApiEndpoint`، `apiEndpoints`، `defaultHttpTimeoutMs`، وتصريح `performRealApiCall`
- `app/src/main/cpp/rin_interpreter.cpp` — أُضيفت natives الجديدة:
  - `httpGet/httpPost/httpPut/httpPatch/httpDelete/httpRequest`
  - `apiRegister(name, baseUrl)` / `apiHeader(name, key, value)` — تسجيل API حقيقي خاص بالمبرمج (رابط + ترويسات/مفتاح API)
  - `apiGet/apiPost/apiPut/apiPatch/apiDelete/apiCall` — استدعاء الـAPI المسجَّل فعلياً عبر الشبكة
  - `jsonEncode/jsonDecode`, `httpSetTimeout`
  - كل النظام القديم (container.api / route / call / callApi المحاكى) بقي كما هو بلا أي تغيير — إضافة فقط.

## ⚠️ لم يكتمل بعد (الخطوة القادمة)
هذه الملفات وحدها **لن تُبنى/تعمل على أندرويد بعد** لأن الربط JNI <-> Kotlin لم يُنجز:
1. `app/src/main/cpp/jni_bridge.cpp` — يحتاج `JNI_OnLoad` يخزّن `JavaVM*` ويُسجِّل `rin::http::setAndroidBridge(...)` لينادي فعلياً `RinHttpBridge.request(...)` في Kotlin.
2. ملف Kotlin جديد `RinHttpBridge.kt` — تنفيذ الطلب الفعلي عبر `HttpURLConnection`.
3. `app/src/main/cpp/CMakeLists.txt` — إضافة `rin_http.cpp` إلى قائمة مصادر `rinengine`.
4. تحسينات preview loop/style (`LoomPreviewManager.kt`, `LoomFabricView.kt`) — لم تبدأ بعد.

سأكمل هذه في الرسالة/الحزمة القادمة.

---

## ✅ اكتمل (تحديث لاحق)

الأجزاء التي كانت "لم تكتمل بعد" أعلاه اكتملت جميعاً واختُبرت (lexer→parser→interpreter→rin_http
كاملاً عبر apiRegister/apiHeader/apiGet + jsonEncode/jsonDecode، ببناء g++ حقيقي):

1. `app/src/main/cpp/jni_bridge.cpp` — أُضيف `JNI_OnLoad` (يخزّن `JavaVM*`، يجلب صف/توقيع
   `RinHttpBridge.request` كـ global ref) + `callKotlinHttpBridge` (المُسجَّل عبر
   `rin::http::setAndroidBridge`)، مع Attach/Detach صحيح للترد الحالي.
2. `app/src/main/java/com/dlof/rinlang/RinHttpBridge.kt` (جديد) — تنفيذ فعلي عبر
   `HttpURLConnection`.
3. `app/src/main/cpp/CMakeLists.txt` — أُضيف `rin_http.cpp` إلى مصادر `rinengine`.
4. **إصلاح كان لازماً لتصحيح البناء**: `headersFromValue`/`bodyToString`/`httpResultToValue` في
   `rin_interpreter.cpp` كانت تُستخدَم قبل تعريفها (use-before-declaration) — أُضيفت تصريحات أمامية
   (forward declarations) أعلى الملف.
5. تحسينات preview loop/style:
   - `LoomPreviewManager.kt`: `Listener.onBusyChanged(Boolean)` و `onSlowOperation()` (كلاهما
     افتراضياً no-op) — مؤشر انتظار مُنقَّى (debounced 150ms) حول كل استدعاء أصلي (start/
     pushLiveEdit/tap)، مع تنبيه إضافي بعد 4 ثوانٍ (يعني عملياً استدعاء شبكة حقيقياً عبر apiCall/
     httpGet وليس حساباً محلياً).
   - `LoomFabricView.kt`: شارة (badge) صغيرة + قوس دوّار في الزاوية العلوية اليمنى من الشاشة
     (مثبتة بمعزل عن zoom)، تتغيّر نصّها/لونها إلى "بانتظار رد شبكة حقيقي…" بعد تجاوز 4 ثوانٍ.
   - `LoomPreviewActivity.kt`: يربط الاثنين أعلاه بـ `fabricView.isBusy` /
     `fabricView.isWaitingOnNetwork`.
   - `res/values{,​-en,-ar}/strings.xml`: نصوص `loom_busy_hint` و `loom_busy_network_hint`
     (لم تُضَف لـ values-es لأنها أصلاً لا تحتوي مفاتيح loom_ الأخرى — تتساقط تلقائياً إلى
     values الافتراضية).

---

## ✅ تطوير وتقوية مفهوم `print` (احترافي، مع خيارات كثيرة جديدة)

### ملفات مُعدَّلة (المحرّك الأساسي — C++)
- `app/src/main/cpp/rin_ast.h` — `PrintStmt` وُسِّعت بحقول اختيارية جديدة: `ifCond`, `level`,
  `label`, `repeatN`, `pretty`, `upper`, `lower`, `width`, `align` (فوق `exprs`/`sep`/`end`
  الموجودة أصلاً)، مع تعليق توثيقي كامل لكل سمة أعلى البنية.
- `app/src/main/cpp/rin_parser.cpp` — `Parser::printStatement()` يقرأ الآن كل السمات الجديدة
  (`key=value`، بأي ترتيب، كل واحدة مرة واحدة على الأكثر وإلا خطأ صريح "attribute repeated").
  **إصلاح جذري مصاحب**: قائمة القيم المفصولة بفواصل كانت تُقرأ عبر `expression()` الكاملة (تشمل
  الإسناد)، فكانت تبتلع أي `attr=value` كتعبير إسناد كامل قبل أن تصل حلقة اكتشاف السمات إليه —
  هذا كان يجعل `sep=`/`end=` الأصليتين **غير قابلتين للاستخدام إطلاقاً** (خطأ تحليل نحوي أو
  "Undefined variable" وقت التشغيل، بحسب الحالة). صُحِّح باستخدام `pipeline()` (مستوى أسفل
  `assignment()` مباشرة) لقراءة القيم، فأصبحت `sep=`/`end=` (وكل السمات الجديدة) تعمل فعلياً كما
  كانت مُوثَّقة دائماً.
- `app/src/main/cpp/rin_interpreter.cpp`:
  - دالة جديدة `prettyPrintValue(const Value&, int indent)` — تهيئة متعددة الأسطر لمصفوفة/قاموس.
  - إعادة كتابة كاملة لتنفيذ `PrintStmt` في `Interpreter::execute()` لتفعيل كل السمات الجديدة:
    بوابة `if=`، رمز `level=` (info/success/warn/error/debug)، وسم `label=`، تكرار `repeat=`،
    تهيئة `pretty=`، تحويل حالة أحرف `upper=`/`lower=`، وحشو/محاذاة `width=`/`align=`.
  - **اختُبر فعلياً** ببناء g++ مستقل (lexer→parser→interpreter كاملاً، بلا JNI/أندرويد) على كل
    سمة على حدة: القيم الافتراضية، الأخطاء الصريحة (سمة مكررة، `level` غير معروف، `upper`+`lower`
    معاً، `align` بلا `width`، `repeat` سالب)، وتأكيد أن `if=false` يمنع تقييم القيم بالكامل
    (بلا أي أثر جانبي، بما فيها متغيرات غير معرَّفة داخل القيم المطبوعة).

### ملفات كونسول أندرويد (لتلوين/تصنيف مخرجات `level=` تلقائياً)
- `app/src/main/res/drawable/ic_log_info.xml` **(جديد)** — أيقونة `level="info"`.
- `app/src/main/res/drawable/ic_log_debug.xml` **(جديد)** — أيقونة `level="debug"`.
- `app/src/main/res/values/colors.xml` — ألوان جديدة: `log_kind_info`, `log_kind_warning`,
  `log_kind_debug`.
- `app/src/main/java/com/dlof/rinlang/RinConsoleFormatter.kt` — تصنيفات `LogKind` جديدة
  `INFO`/`WARNING`/`DEBUG` (تُعيد استخدام أيقونة `ic_status_warning.xml` الموجودة أصلاً
  لـ`WARNING`)، مسجَّلة ضمن `PREFIX_ORDER` لتلتقط رموز ℹ️/⚠️/🐞 التي يطبعها المحرّك؛ `level="success"`
  و`level="error"` تُصنَّفان تلقائياً ضمن `SUCCESS`/`ERROR` الموجودتين أصلاً (نفس الرمزين ✅/❌).

### أدوات المطوّر
- `src/Snippets/Rin/print.pro.snippet` **(جديد)** — قصاصة VS تُدرج `print` بكل السمات
  الاحترافية (level/label) جاهزة للتعديل، إلى جانب `print.snippet` البسيطة الموجودة أصلاً (بلا
  أي تغيير عليها).

### توثيق
- `README.md` — قسم جديد كامل "### `print` — دليل كامل" (أمثلة مُختبَرة فعلياً لكل سمة + جدول
  مرجعي بالسمة/النوع المتوقَّع/الافتراضي/الوصف)، وتحديث سطر `print` في جدول لغة الحاويات ليُشير
  إليه.

---

## ✅ تطوير وتقوية وسم الإغلاق `.end/...`

### ملف مُعدَّل: `app/src/main/cpp/rin_parser.h`
- توقيع `consumeEndTag` تغيّر من `(const std::string& expectedTag)` إلى
  `(const std::string& expectedTag, int openLine, const std::string& openName = "")`.

### ملف مُعدَّل: `app/src/main/cpp/rin_parser.cpp`
إعادة كتابة `Parser::consumeEndTag()` وتحديث كل مواضع استدعائها الأربعة
(`atBlock()`, `viewDeclaration()`, `sectionBlock()`, `translationsBlock()`) لتمرير سطر/اسم الفتح.
ثلاث ميزات جديدة، إضافية بالكامل ومتوافقة مع كل ملفات `.rin` القديمة بلا أي تعديل عليها
(تحقّق فعلياً بتشغيل `site.rin` و`web/site.rin` الحقيقيين كاملَين بعد التعديل — لا رجعة/regression):

1. **رسائل خطأ تذكر سطر الفتح**: أي وسم إغلاق ناقص أو غير مطابق (`'.'`/`'end'`/`'/'` مفقودة، أو
   كلمة الإغلاق لا تطابق كلمة الفتح) يذكر الآن صراحة "opened at line N" — لا حاجة للبحث يدوياً في
   ملف كبير عن أي `@` بالضبط لم يُغلق.
2. **اختصار `.end;`**: يغلق الكتلة الحالية أياً كان نوعها/اسمها بلا تكرار كتابتهما؛ الصحة البنيوية
   (أي كتلة تُغلق فعلياً) تبقى مضمونة بالكامل عبر ترتيب النزول التكراري في المحلل النحوي نفسه، بمعزل
   عن هذا الاختصار.
3. **تحقق اختياري من الاسم `.end/tag=name`**: طبقة أمان إضافية اختيارية — إن كُتب اسم بعد كلمة
   الإغلاق، يُتحقَّق فعلياً وقت التحليل النحوي أنه يطابق حرفياً اسم `@tag=name` الذي فتح الكتلة (خطأ
   صريح عند عدم التطابق، أو عند إعطاء اسم لإغلاق كتلة فُتحت بلا اسم أصلاً).

**اختُبر فعلياً** ببناء g++ مستقل: الشكل الكلاسيكي `.end/container` (بلا تغيير)، الشكل المختصر
`.end;`، التحقق الاسمي الناجح والفاشل، خطأ نوع الوسم غير المطابق مع ذكر سطر الفتح، تعشيش
`Volume`/`container`/`Section` بخليط من الأشكال المطوَّلة والمختصرة معاً، و`Translations` (بلا اسم)
بالشكل المختصر.

### توثيق: `README.md`
قسم جديد "### وسم الإغلاق `.end/...` — دليل كامل" (أمثلة مُختبَرة فعلياً لكل ميزة: رسائل الخطأ
الحقيقية، `.end;`، والتحقق الاسمي) أعلى جدول "المفاهيم المدعومة" في قسم لغة الحاويات.

## إضافات: opskit.og.rin + الهوية البصرية
- ملف جديد `lib/opskit.og.rin` يضيف: copyFile/copyFiles، mergeArrays/mergeMaps،
  createZip، download، vote، snapshot/restore، difference، groupABC، numberBlocks/
  numberBlockText، bubble، tenilOrder.
- تم اعتماد `assets/branding/*` كالأيقونات الرسمية للغة، وتوليد أيقونات أندرويد
  (كل الكثافات)، أيقونة إضافة VS، وfavicon الموقع منها تلقائياً.

## تحديث: ربط دوال opskit بالحاويات
- أُعيد تنظيم `lib/opskit.og.rin`: كل الدوال تبقى قابلة للاستدعاء من أي مكان
  (بما أن fun داخل @container.open/object محصورة بنطاق حاويتها ولا تُستدعى من
  خارجها في Rin)، لكنها مصمَّمة للاستخدام من *داخل* حاوياتك الخاصة.
- أُضيف `examples/opskit_containers_demo.rin`: يستخدم كل دالة (copyFile، merge،
  createZip، download، vote، snapshot/restore، difference، classifyIntoGroups،
  numberBlocks، bubble، tenilOrder) من داخل @container.open/object حقيقية،
  ويعرض أيضاً Group A/B/C كبنية @Containers.Group رسمية (GroupA/GroupB/GroupC).
- "Snapshots" (الاستعادة) أصبحت @container.doc حقيقية (NoSQL) بدل map عادي.
- تم بناء المفسّر واختبار كل دالة فعلياً (g++ -std=c++17، cli/linux) والتحقق
  من صحة ملف ZIP الناتج عبر Python zipfile (CRC صحيح).

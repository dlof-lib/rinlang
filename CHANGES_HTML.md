# مشاريع HTML — تشغيل index.html وربطه بـ Rin

نوع مشروع جديد **HTML** في حوار «مشروع جديد» ينشئ:

| الملف | الدور |
|---|---|
| `index.html` | الواجهة |
| `style.css` | التنسيق (يُدمج تلقائياً في الصفحة) |
| `container.rin` | منطق Rin: متغيّرات عامة (`let`) ودوال علوية تستدعيها الصفحة |
| `main.rin` | نقطة دخول رمزية (مطلوبة لتصدير/استيراد `.rinproj`) |

زر **تشغيل (Run)** في المحرر — عند فتح مشروع HTML، أو عند فتح `index.html` / `style.css` / `container.rin`
من أي مشروع فيه `index.html` — يحفظ الملف ثم يفتح `HtmlRunActivity`: WebView يعرض الصفحة مربوطةً بجلسة Rin حيّة.

## الربط من HTML (بلا JavaScript)

```html
<div rin-text="count">0</div>            <!-- يعرض المتغيّر count -->
<button rin-click="add()">+1</button>    <!-- يستدعي fun add() -->
<button rin-click="addBy(5)">+5</button> <!-- وسائط: أرقام / "نص" / true|false / اسم متغيّر -->
<input rin-model="userName">             <!-- ربط ثنائي الاتجاه -->
<p rin-show="isHigh">…</p>               <!-- يظهر عندما يكون المتغيّر صحيحاً -->
```

```rin
let count = 0;
fun add() { count = count + 1; }
```

ومن JavaScript عند الحاجة: `rin.call("add")`، `rin.get("count")`، `rin.set("userName", "x")`.
هذه هي نفس التوجيهات التي تدعمها `web/rinhtml/rinhtml.js` على الويب.

## الملفات المتغيّرة
- `app/src/main/cpp/jni_bridge.cpp`: دوال JNI للجلسة الحيّة (`htmlCreate/Call/SetGlobal/Globals/Free`) فوق
  `Interpreter::callTopLevelFunction` — نظير `web/rinhtml_bridge.cpp`.
- `RinEngine.kt`: الصنف `RinEngine.HtmlSession`.
- `HtmlRunActivity.kt` (جديد) + تسجيله في `AndroidManifest.xml`.
- `Project.kt` (`ProjectType.HTML`)، `ProjectManager.kt` (القوالب)، `ProjectsActivity.kt`، `MainActivity.kt` (ربط Run).
- أيقونات ملفات محلية بلا شبكة: `ic_html_file`, `ic_css_file`, `ic_container_rin_file` (يربطها `FileIconResolver.kt`).
- دليل الربط الكامل: `docs/html-projects.md`.
- موارد: شريحة النوع في `dialog_create_project.xml`، `ic_type_html.xml`، نصوص ar/en/es، ألوان.
- `schemas/rin-project.schema.json`: إضافة `html` إلى أنواع المشروع.

## قيود معروفة
- القيم المنقولة إلى الصفحة بدائية فقط (رقم/نص/منطقي/null)؛ المصفوفات والكائنات تُعرض كنص.
- `print` أثناء التشغيل الأولي فقط يظهر في لوحة المخرجات السفلية.
- لم يُبنَ APK في هذه البيئة (لا Android SDK/NDK)؛ منطق الجلسة اختُبر على Linux بنفس واجهة المحرّك.

## تعديل: container.rin ليس خاصاً بـ HTML
- Run على `container.rin`/أي `.rin` = تشغيل Rin عادي في أي مشروع؛ فتح الصفحة يكون من `index.html`/`style.css`/`main.rin` (مشروع HTML) أو من قائمة Run ← «تشغيل كصفحة HTML».
- ملف المنطق للصفحة اختياري التسمية: `<link rel="rin" href="app.rin">` أو `<meta name="rin" content="app.rin">`، وإلا `container.rin` ثم `main.rin`.

## تعديل: ملف الحاوية يُعرَّف بالتوقيع + قدرات أقوى
- **التوقيع**: `//! rin:container web [persist=a,b|*]` في أول سطر غير فارغ (`RinContainerFile.kt`). الأيقونة وRun والاختيار
  التلقائي والحفظ تعتمد عليه لا على اسم الملف. `container.rin` بلا توقيع = ملف Rin عادي.
- **HTML**: `rin-for`، `rin-if`/`rin-else`، `rin-attr`، `rin-class`، وتعابير (`== != < > <= >= ! && ||`) ومسارات (`t.title`، `list.length`).
  الاسم الجديد للسكربت: `app/src/main/assets/rin_html_runtime.js` (كان مضمَّناً في Kotlin).
- **الجسر (JNI)**: نقل المصفوفات والقواميس والكائنات بالاتجاهين (JSON) بعمق ≤ 8.
- **حفظ الحالة**: `persist=` ← `.rin_state/<ملف>.json`، ضغطة مطوّلة على إعادة التحميل تمسحها، و`rin.clearState()`.
- القوالب (index.html/style.css/container.rin) تعرض الآن عدّاداً + قائمة مهام محفوظة.
- اختبارات: 18 حالة لسكربت الصفحة (jsdom)، واختبار منطق القالب والجسر على محرّك Rin الحقيقي على Linux.

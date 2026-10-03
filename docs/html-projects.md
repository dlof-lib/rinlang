# مشاريع HTML في Rin — الربط بين index.html وملف الحاوية

## الفكرة
مشروع HTML يفصل ثلاث طبقات:

| الملف | الدور |
|---|---|
| `index.html` | الواجهة (عرض فقط) |
| `style.css` | التنسيق — يُدمج تلقائياً في الصفحة |
| ملف الحاوية (`container.rin` افتراضياً) | المنطق: متغيّرات عامة (الحالة) + دوال علوية (الأحداث) |
| `main.rin` | نقطة دخول رمزية، مطلوبة لتصدير/استيراد `.rinproj` |

## ملف الحاوية يُعرَّف بتوقيعه لا باسمه

يستطيع أي مستخدم تسمية ملف أو حاوية `container` (ولغة Rin فيها أصلاً `@container=Name`)، لذلك الاسم وحده لا يدل على شيء.
ملف الحاوية الحقيقي هو أي ملف `.rin` **أول سطر غير فارغ فيه توقيع**:

```rin
//! rin:container web
//! rin:container web persist=count,todos     // حفظ هذه المتغيرات بين التشغيلات
//! rin:container web persist=*               // حفظ كل المتغيرات العامة
```

السطر تعليق عادي في Rin فلا يتأثر المحرّك به. التطبيق يستعمل التوقيع في:

- **الأيقونة**: الملفات الموقَّعة تظهر بأيقونة خاصة، وملف اسمه `container.rin` بلا توقيع يبقى ملف Rin عادياً.
- **Run**: على ملف موقَّع داخل مشروع فيه `index.html` يفتح الصفحة مربوطةً به. بلا توقيع يعمل Run كبرنامج Rin عادي.
- **الاختيار التلقائي** لملف المنطق عند تشغيل الصفحة.
- **حفظ الحالة** (`persist=`).

ترتيب اختيار ملف المنطق للصفحة:
1. الملف المفتوح عند اختيار «تشغيل كصفحة HTML» من قائمة Run.
2. ما يحدده `index.html`: `<link rel="rin" href="app.rin">` أو `<meta name="rin" content="app.rin">`.
3. ملف موقَّع في جذر المشروع (`container.rin` أولاً، ثم أبجدياً).
4. `container.rin` ثم `main.rin`.

## كيف يتم الربط

```
زر Run ─▶ MainActivity ─▶ HtmlRunActivity
   1) يقرأ ملف الحاوية ويُنشئ جلسة حيّة: RinEngine.HtmlSession.create()
      ──JNI──▶ htmlCreateNative()  (Lexer → Parser → Interpreter.run، ثم exportGlobals)
      ثم يستعيد المتغيرات المحفوظة (persist=) إن وُجدت
   2) يقرأ index.html، يدمج style.css، ويحقن assets/rin_html_runtime.js قبل </body>
   3) WebView.loadDataWithBaseURL(...) + window.RinBridge

نقرة على  <button rin-click="addTodo()">
   JS ─▶ RinBridge.call("addTodo", "[]")
   ─JNI▶ Interpreter::callTopLevelFunction(...)   ← ينفّذ fun addTodo() الحقيقية
   ◀─ {"ok":true,"globals":{ "todos":[...], ... }}
   JS يعيد رسم الصفحة كلها من الحالة الجديدة، ويُحفظ ما طُلب حفظه
```

الجلسة تبقى حيّة بين النقرات فلا يُعاد تشغيل البرنامج. زر إعادة التحميل يبدأ جلسة جديدة
(مع استعادة المحفوظ)، والضغطة المطوّلة عليه تمسح الحالة المحفوظة وتبدأ نظيفة.

## التوجيهات في HTML

| التوجيه | المعنى |
|---|---|
| `rin-text="expr"` | يعرض قيمة: `count`، `user.name`، `todos.length` |
| `rin-click="f(a, b)"` | يستدعي `fun f` — الوسائط حرفيات (`5`، `'نص'`، `true`) أو مسارات (`t.id`) |
| `rin-model="x"` | ربط ثنائي الاتجاه لـ `input` / `textarea` / `checkbox` |
| `rin-if="expr"` / `rin-else` | إظهار بشرط؛ `rin-else` يتبع عنصراً فيه `rin-if` مباشرة |
| `rin-show="expr"` | مثل `rin-if` (إظهار/إخفاء) |
| `rin-for="t in list"` أو `"t, i in list"` | يكرّر العنصر لكل عنصر في مصفوفة (أو مفاتيح قاموس)؛ `$index` متاح تلقائياً |
| `rin-attr="href:t.url; title:t.title"` | يضبط خصائص من الحالة (`null/false` تحذفها) — دون `on*` |
| `rin-class="done:t.done; first:i == 0"` | يضيف/يزيل أصنافاً بشرط |

التعبيرات: مسارات منقّطة، حرفيات، `== != < > <= >=`، `!`، `&&`، `||`. المصفوفة الفارغة تُعدّ خطأً (false).
داخل `rin-for` يرى كل عنصر مُكرَّر متغيّره (`t`) في `rin-text` و`rin-click` وغيرهما.

من JavaScript عند الحاجة: `rin.call("f", a, b)`، `rin.get("x")`، `rin.set("x", v)`، `rin.globals()`، `rin.clearState()`.

## مثال

`container.rin`
```rin
//! rin:container web persist=todos,nextId
let todos = [];
let nextId = 1;
let newTodo = "";

fun addTodo() {
    if (newTodo != "") {
        push(todos, {"id": nextId, "title": newTodo, "done": false});
        nextId = nextId + 1;
        newTodo = "";
    }
}

fun toggle(id) {
    for (let t in todos) {
        if (t["id"] == id) { t["done"] = !t["done"]; }
    }
}
```

`index.html`
```html
<input rin-model="newTodo"> <button rin-click="addTodo()">إضافة</button>
<ul>
  <li rin-for="t in todos" rin-class="done:t.done">
    <span rin-click="toggle(t.id)" rin-text="t.title"></span>
    <small rin-if="t.done">منجزة</small><small rin-else>قيد التنفيذ</small>
  </li>
</ul>
<p rin-if="todos.length == 0">لا مهام</p>
```

## الملفات والشبكة
دوال Rin المعتادة تعمل داخل ملف الحاوية على مجلد المشروع: `readFile` و`writeFile` و`appendFile` و`httpGet`… وتُستدعى من أي دالة
تناديها الصفحة، فيمكن مثلاً حفظ بيانات في ملف ضمن المشروع من `fun save()`.

## حفظ الحالة
`persist=a,b` (أو `*`) يحفظ هذه المتغيرات بعد كل استدعاء في `<المشروع>/.rin_state/<الملف>.json` ويستعيدها عند التشغيل التالي
(للمتغيرات الموجودة في الملف فقط). المجلد مخفي من المستكشف ومن تصدير `.rinproj`. الثوابت المدمجة (`PI`…) لا تُحفظ مع `*`.

## ماذا تحتاج
- لا مكتبات ولا أذونات جديدة (WebView النظام، `minSdk 24`).
- إعادة بناء المكتبة الأصلية (NDK) لأن `jni_bridge.cpp` تغيّر.
- ملف `app/src/main/assets/rin_html_runtime.js` ضمن الـ APK (مجلد assets الافتراضي في Gradle).
- الدوال المستدعاة من الصفحة يجب أن تكون **علوية** (ليست داخل `@container` أو `if`)، والمتغيرات المعروضة عامة.

## القيود
- المتغيرات المنقولة: أرقام ونصوص ومنطقية وnull ومصفوفات وقواميس/كائنات (عمق ≤ 8). الدوال لا تُنقل.
- القيمة المرسلة من الصفحة إلى Rin بنفس الأنواع (تُحوَّل المصفوفات والكائنات إلى مصفوفات وقواميس).
- `print` يظهر في اللوحة السفلية أثناء التشغيل الأولي فقط.
- `rin-for` على عناصر مباشرة داخل نفس الأب؛ والمتداخل مدعوم.

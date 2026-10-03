# مشاريع HTML في Rin — الربط بين index.html و container.rin

## الفكرة
مشروع HTML يفصل ثلاث طبقات:

| الملف | الطبقة | الدور |
|---|---|---|
| `index.html` | عرض | الواجهة. لا يحتوي منطقاً |
| `style.css` | تنسيق | يُدمج تلقائياً في الصفحة |
| `container.rin` | منطق | متغيّرات عامة (الحالة) + دوال علوية (الأحداث) |
| `main.rin` | دخول رمزي | مطلوب فقط لتصدير/استيراد `.rinproj` |

## كيف يتم الربط (من الضغط إلى الشاشة)

```
زر Run ─▶ MainActivity.runHtmlProjectIfApplicable() ─▶ HtmlRunActivity
                                                          │
   1) يقرأ container.rin ويُنشئ جلسة حيّة:               │
      RinEngine.HtmlSession.create()  ──JNI──▶  htmlCreateNative()
      (Lexer → Parser → Interpreter.run، ثم exportGlobals)
   2) يقرأ index.html، يدمج style.css في <style>،
      ويحقن سكربت الربط قبل </body>
   3) WebView.loadDataWithBaseURL(...) + window.RinBridge

نقرة على  <button rin-click="add()">
   JS (سكربت الربط)  ─▶ RinBridge.call("add", "[]")
   ─JNI▶ Interpreter::callTopLevelFunction(...)   ← ينفّذ fun add() الحقيقية
   ◀─ {"ok":true,"globals":{"count":1,...}}
   JS يحدّث كل عناصر rin-text / rin-show / rin-model
```

الجلسة تبقى حيّة بين النقرات: لا يُعاد تشغيل البرنامج من الصفر، فتبقى قيم المتغيّرات.
زر «إعادة تحميل» في الشاشة يبدأ جلسة جديدة بحالة نظيفة.

## التوجيهات المتاحة في HTML

| التوجيه | المعنى |
|---|---|
| `rin-text="x"` | يعرض قيمة المتغيّر `x` كنص |
| `rin-click="f()"` | يستدعي `fun f()` عند النقر |
| `rin-click="f(5, 'نص', true, x)"` | وسائط: رقم / نص / منطقي / اسم متغيّر عام (تُستبدل بقيمته الحالية) |
| `rin-model="x"` | ربط ثنائي الاتجاه لـ `input` / `textarea` / `checkbox` |
| `rin-show="x"` | يُظهر العنصر عندما تكون `x` صحيحة ويُخفيه عندما لا |

من JavaScript عند الحاجة: `rin.call("f", a, b)`، `rin.get("x")`، `rin.set("x", v)`، `rin.globals()`.

## مثال

`container.rin`
```rin
let count = 0;
let isHigh = false;

fun add() {
    count = count + 1;
    isHigh = count >= 10;
}
```

`index.html`
```html
<link rel="stylesheet" href="style.css">
<div rin-text="count">0</div>
<button rin-click="add()">+1</button>
<p rin-show="isHigh">وصلت إلى 10!</p>
```

## ماذا تحتاج
- **لا مكتبات ولا أذونات جديدة**: يستخدم WebView النظام. `minSdk 24` كما هو.
- **إعادة بناء المكتبة الأصلية (NDK)** لأن `jni_bridge.cpp` تغيّر (دوال `html*Native`).
- لا تغيير في `CMakeLists.txt` ولا `build.gradle.kts`.
- الاستدعاء يعمل على **الدوال العلوية فقط** (ليست داخل `@container` أو `if`)، والمتغيّرات المعروضة يجب أن تكون عامة.

## أين تُنشأ المشاريع
حوار «مشروع جديد» ← النوع **HTML** (أيقونة `</>`). تظهر الملفات في المستكشف بأيقونات محلية
(`ic_html_file` / `ic_css_file` / `ic_container_rin_file`) تعمل دون اتصال، وللمحرر تلوين HTML/CSS جاهز.

## التشغيل
- داخل مشروع HTML: زر **Run** في المحرر (من أي ملف فيه).
- في أي مشروع آخر فيه `index.html`: Run يعمل عند فتح `index.html` أو `style.css` أو `container.rin`.
- روابط `http(s)` داخل الصفحة تُفتح في المتصفح الخارجي. الصور وملفات المشروع النسبية تعمل.

## القيود
- قيم الحالة المنقولة بدائية فقط (رقم/نص/منطقي/null)؛ المصفوفات والكائنات تُعرض نصاً.
- `print` يظهر في اللوحة السفلية أثناء التشغيل الأولي فقط.
- `style.css` وأي `<link rel="stylesheet">` محلي يُدمجان داخل الصفحة؛ ملفات الخطوط المشار إليها بـ `url()` تُحمَّل من مجلد المشروع.
- المنطق الذي يتطلب شبكة داخل Rin (`httpGet`...) يتبع إعدادات التطبيق المعتادة.

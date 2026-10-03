# تغييرات: إصلاح rin_html_runtime.js + HTML+JS + أيقونات + صفحة إنشاء + localhost:7700

- **الإصلاح**: `app/src/main/assets/rin_html_runtime.js` كان مفقوداً من المشروع (مجلد assets غير موجود) فظهر «تعذّر تحميل rin_html_runtime.js». أُعيد كتابته.
- **خيار JS بديل**: نوع مشروع `html_js` ينشئ index.html + style.css + script.js + container.rin (حاوية Rin موقَّعة تستدعيها JS عبر rin.call/rin.get). `HtmlProjectRuntime.kt` مشترك.
- **أيقونات**: HTML5 / CSS3 / JS ملوّنة (`ic_html_file`, `ic_css_file`, `ic_js_file`).
- **صفحة إنشاء المشروع**: صفوف موحّدة بأقسام (الويب / Rin).
- **http://localhost:7700/**: `RinLocalServer.kt` لمشاريع HTML وRin؛ زر الكرة الأرضية في شاشة التشغيل أو Run ← «فتح على localhost:7700».
- لم يُبنَ APK هنا (لا Android SDK).

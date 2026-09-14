# محرّك Rin لصفحة playground.html

هذا المجلد فارغ من ملفَي `rin.js` و`rin.wasm` في هذه النسخة من الموقع، لأن
بناءهما يحتاج Emscripten SDK (غير متاح في بيئة إعداد هذا الموقع). الصفحة
`playground.html` مكتوبة لتكتشفهما تلقائياً بمجرد وضعهما هنا — بلا أي تعديل
آخر مطلوب في HTML/CSS/JS.

## لتفعيل التشغيل الحقيقي داخل المتصفح

```bash
# من جذر مستودع rinlang (وليس من داخل docs/)
bash docs/assets/engine/build-engine.sh
```

يبني هذا نفس محرّك Rin المستخدَم في تطبيق أندرويد وسطر الأوامر بالضبط —
`rin_lexer.cpp` / `rin_parser.cpp` / `rin_interpreter.cpp` بلا أي تعديل —
مُصرَّفاً WebAssembly، بنفس قائمة المصادر المستخدمة فعلياً في
`.github/workflows/pages.yml`.

بعد نجاح البناء سيظهر هنا:

```
docs/assets/engine/rin.js
docs/assets/engine/rin.wasm
```

وعند فتح `playground.html` بعدها، يتحوّل شريط الحالة أعلى المحرّر من
"وضع المعاينة" إلى "المحرّك الحقيقي مُحمَّل"، ويعمل زر «تشغيل» فعلياً —
بلا أي خادوم، بلا شبكة، التنفيذ كامل داخل متصفح الزائر.

## أتمتة البناء في CI (اختياري)

لنشر الموقع بمحرّك جاهز تلقائياً مع كل push، أضف قبل خطوة رفع `docs/` في
سير عمل GitHub Pages الخاص بكم خطوتين: تثبيت Emscripten (`mymindstorm/setup-emsdk`)
ثم تشغيل `bash docs/assets/engine/build-engine.sh` — تماماً كما تفعل خطوة
"Compile the official Rin engine" الموجودة أصلاً في
`.github/workflows/pages.yml` لمسار `web/` المختلف.

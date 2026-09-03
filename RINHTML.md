# RinHTML

مكتبة ربط رسمية بين HTML وRin، مبنية فوق محرك Rin الحقيقي (C++17 → WebAssembly) —
وليست إعادة تنفيذ للغة بجافاسكربت. تجعل من الممكن كتابة:

```html
<script src="rin_engine.js"></script>
<script src="rinhtml/rinhtml.js"></script>

<rin-app src="main.rin" allow="dom"></rin-app>

<button rin-click="add()">+</button>
<div rin-text="count"></div>
```

```rin
// main.rin
let count = 0;
fun add() { count = count + 1; }
```

بدون أي جافاسكربت مكتوب يدوياً.

## البنية

```
HTML  →  <rin-app>/rin-*  →  rinhtml.js  →  rin_engine.wasm  →  Rin C++17 Engine
```

- **`web/rinhtml_bridge.cpp`** — جسر C ذو *جلسة* فوق `rin_lexer/parser/interpreter.cpp`
  الرسمية (بلا تعديل عليها). يبقي برنامج Rin وحالته حيّين بين الاستدعاءات، مستخدِماً
  دالتين موجودتين أصلاً في `rin_interpreter.h` لغرض Loomtime:
  `Interpreter::callTopLevelFunction` (استدعاء دالة Rin حقيقية باسمها) و
  `Interpreter::exportGlobals` (قراءة الحالة).
- **`web/rinhtml/rinhtml.js`** — المكتبة نفسها: تحميل المحرك، إدارة الجلسات، توجيهات
  HTML (`rin-click`, `rin-text`, `rin-model`, `rin-show`)، والعناصر المخصصة
  `<rin-app>` / `<rin-container>` / `<rin-run>`.
- **`web/rinhtml/rinhtml.d.ts`** — تعريفات TypeScript لواجهة `window.rinhtml`.
- **`web/build_rinhtml_wasm.sh`** — بناء `rin_engine.js`/`.wasm` بـ Emscripten.
- **`web/rinhtml-demo/`** — مثال عدّاد كامل يعمل فوراً بعد البناء.

هذا يتعايش مع `web/rin_wasm_bridge.cpp` القديم (تشغيل لمرة واحدة، لبناء صفحة عبر
`writeFile`) دون أي تعارض — كل ملف يخدم غرضاً مختلفاً؛ لم يُعدَّل القديم.

## واجهة JavaScript (`window.rinhtml`)

| الدالة | الوصف |
|---|---|
| `rinhtml.run(source, opts)` | يشغّل نص .rin كسلسلة نصية، يعيد `Promise<App>` |
| `rinhtml.runFile(src, opts)` | يجلب ملفاً ثم يشغّله |
| `rinhtml.mount(app, root?)` | يربط توجيهات `rin-*` داخل `root` (افتراضياً `document`) |
| `rinhtml.unmount(app)` | يزيل كل الروابط |
| `rinhtml.reload(app)` | يعيد الجلب والتشغيل من `opts.src` |
| `rinhtml.call(app, fn, ...args)` | يستدعي دالة Rin علوية حقيقية |
| `rinhtml.get(app, name)` / `set(app, name, v)` | قراءة/كتابة متغيّر عام (محلياً في JS) |
| `rinhtml.on(app, event, fn)` / `emit(...)` | أحداث `'update'` / `'error'` |

`opts = { allow: "dom storage", root: el, src: "main.rin" }`.

## توجيهات HTML

| السمة | المعنى |
|---|---|
| `rin-click="fn(arg1, arg2)"` | يستدعي دالة Rin عند النقر. الوسائط: أرقام / `"نص"` / `true`\|`false` / اسم متغيّر عام |
| `rin-text="name"` | يعرض قيمة متغيّر عام كنص |
| `rin-model="name"` | ربط ثنائي الاتجاه مع `<input>`/`<textarea>` |
| `rin-show="name"` | يُظهر/يُخفي العنصر حسب صدق القيمة |

## نظام الصلاحيات (`allow="..."`)

بدون `allow=` إطلاقاً → البرنامج يعمل، لكن **بلا أي وصول لـ DOM** (Sandbox كامل).
القيم المدعومة حالياً: `dom` (يُفعِّل كل توجيهات `rin-*` وتحديث العرض)،
`storage` و`fetch` محجوزتان لتوسعات لاحقة (`fetch` غير مفعّلة عمداً: نسخة WASM من
Rin تُرجع خطأً واضحاً لأي طلب `http*`، كما في `rin_http_wasm_stub.cpp` الأصلي).

## البناء

```bash
cd web
./build_rinhtml_wasm.sh   # يحتاج Emscripten (emcc) في PATH
```

> **ملاحظة مهمة:** لم تُبنَ ملفات `rin_engine.js`/`.wasm` هنا فعلياً — بيئة العمل التي
> جهّزت فيها هذه الملفات لا تملك Emscripten ولا اتصال شبكة لتثبيته. كل كود JS/C++
> أعلاه صحيح ومتوافق مع تواقيع محرككم الفعلية (تحقّقتُ من `rin_interpreter.h` مباشرة)،
> لكنه غير مُختبَر بتشغيل فعلي. شغّل `build_rinhtml_wasm.sh` على جهاز فيه Emscripten،
> ثم افتح `web/rinhtml-demo/index.html` عبر خادوم محلي (fetch لا يعمل من `file://`
> مباشرة) — مثلاً: `python3 -m http.server` من داخل `web/`.

## القيود الحالية (v1)

- القيم المدعومة عبر الجسر: أرقام/نصوص/منطقية/`nil` فقط. المصفوفات والقوائم (Map)
  تُعرض كنص (`toDisplayString`) لكن لا يمكن تمريرها كوسائط لدوال عبر `rin-click` بعد.
- `rin-click` يفسّر وسائط ثابتة/أسماء متغيرات فقط، وليس تعبيرات Rin كاملة.
- كل `<rin-app>` بلا `scope="self"` يربط توجيهاته على مستوى المستند كله (يطابق مثال
  العدّاد في المواصفة، حيث الزر خارج `<rin-app>`)؛ استخدم `id="..."` + `scope="self"`
  لعزل تطبيقات متعددة في نفس الصفحة.

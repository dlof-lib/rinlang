<div align="center">

# 🔗 استدعاء Rin من لغات برمجة أخرى

### Language Bindings — نفس محرّك C++17، من أي لغة تدعم FFI

<p>
<img alt="ABI" src="https://img.shields.io/badge/C%20ABI-rin__c__api.h-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" />
<img alt="Python" src="https://img.shields.io/badge/Python-ctypes-3776AB?style=for-the-badge&logo=python&logoColor=white" />
<img alt="Node.js" src="https://img.shields.io/badge/Node.js-ffi--napi-339933?style=for-the-badge&logo=node.js&logoColor=white" />
<img alt="Platform" src="https://img.shields.io/badge/Platform-Linux%20%7C%20macOS%20%7C%20Windows-4E4E4E?style=for-the-badge" />
</p>

</div>

---

هذا المجلد يجعل محرّك Rin (Lexer + Parser + Interpreter) قابلاً للاستدعاء من **أي لغة برمجة**، وليس فقط من Kotlin عبر JNI داخل التطبيق. الفكرة: بناء المحرّك كمكتبة مشتركة عامة (`librin.so` / `librin.dylib` / `rin.dll`) تُصدِّر واجهة C بسيطة (`rin_c_api.h` في `app/src/main/cpp/`)، ثم ربط أي لغة بها عبر آلية استدعاء C ABI القياسية في تلك اللغة.

```
[كود Rin كنص] --> [python/rin.py أو node/rin.js أو ...] --> [librin.so] --> [نفس محرّك C++]
```

> ✅ هذا **نفس المحرّك بالضبط** المستخدم داخل تطبيق أندرويد — لا يوجد تكرار للمنطق، فقط طبقة C رقيقة (`rin_c_api.cpp`) تُغلِّف نفس تسلسل `lex -> parse -> interpret` الموجود في `jni_bridge.cpp`.

## الخطوات

### 1) ابنِ المكتبة المشتركة مرة واحدة

```bash
cd bindings
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

الناتج حسب نظام التشغيل: `build/librin.so` (لينكس)، `build/librin.dylib` (macOS)، `build/rin.dll` (ويندوز).

### 2) استدعِ Rin من لغتك

| اللغة | الملف | طريقة الربط |
|---|---|---|
| Python | `python/rin.py` | `ctypes` (مدمجة في بايثون، لا حزم خارجية) |
| Node.js | `node/rin.js` | `ffi-napi` + `ref-napi` (`npm install ffi-napi ref-napi`) |
| C / C++ | `c/example.c` | ربط مباشر (`-lrin`) |
| أي لغة أخرى (Rust, Go, C#, Java خارج أندرويد...) | — | نفس الفكرة: حمّل `rin_c_api.h` عبر آلية FFI الخاصة باللغة (cgo لـ Go، `extern "C"` لـ Rust، P/Invoke لـ C#, JNA/JNI لجافا عادية) |

### مثال بايثون

```python
from rin import Rin

rin = Rin("./build/librin.so")
print(rin.version())
print(rin.run('print "Hello from Rin!";'))
```

### مثال Node.js

```js
const { Rin } = require("./rin");
const rin = new Rin("../build/librin.so");
console.log(rin.run('print "Hello from Rin!";'));
```

### 3) واجهة C المتاحة (`rin_c_api.h`)

| الدالة | الوصف |
|---|---|
| `rin_run(source, basePath)` | يشغّل برنامج Rin كاملاً مرة واحدة، يُرجع كل ما طُبع (`char*` يجب تحريره). |
| `rin_free_string(s)` | يحرِّر أي `char*` أعادته هذه الواجهة. |
| `rin_engine_version()` | نص النسخة (لا يحتاج تحريراً). |
| `rin_session_create(basePath)` | ينشئ جلسة تحافظ على `basePath` ثابت بين عدة تشغيلات. |
| `rin_session_run(session, source)` | يشغّل مصدراً داخل جلسة موجودة. |
| `rin_session_free(session)` | يحرِّر الجلسة. |

`basePath` هو نفس الجذر المستخدم داخل التطبيق (`filesDir`) لعمليات `save`/`installation`/`file`/`writeFile`/`readFile` في لغة الحاويات — مرِّر أي مجلد على القرص، أو `""` لاستخدام مجلد العمل الحالي.

## ملاحظات

- المكتبة المبنية هنا **مستقلة تماماً عن أندرويد/NDK** — تعمل على أي جهاز فيه CMake ومترجم C++17 (لينكس/macOS/ويندوز)، وهي نفس الفكرة المستخدمة في `tools/test_*.cpp` لكن مُغلَّفة كمكتبة قابلة للاستدعاء بدل ملف تنفيذي واحد.
- لإعادة استخدام نفس المكتبة **داخل تطبيق أندرويد نفسه من كود Kotlin/Java خارج هذا المستودع**، استورد `.aar`/`.so` الناتج من بناء NDK العادي (`app/src/main/cpp`) بدل هذا المسار — هذا المجلد مخصَّص للاستدعاء من **خارج** أندرويد.

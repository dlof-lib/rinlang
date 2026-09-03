#!/usr/bin/env bash
# يبني rin_engine.js + rin_engine.wasm من نفس ملفات محرك Rin الرسمية (Lexer/Parser/Interpreter)
# زائد web/rinhtml_bridge.cpp (جسر الجلسات) وweb/rin_http_wasm_stub.cpp (بديل الشبكة في WASM).
#
# يتطلب: Emscripten SDK مفعّل في PATH (emcc). راجع https://emscripten.org/docs/getting_started/downloads.html
#
# الاستخدام:
#   cd web
#   ./build_rinhtml_wasm.sh
# الناتج: web/rin_engine.js + web/rin_engine.wasm — ضعهما بجانب rinhtml/rinhtml.js في صفحتك:
#   <script src="rin_engine.js"></script>
#   <script src="rinhtml/rinhtml.js"></script>

set -euo pipefail
cd "$(dirname "$0")"

CPP_DIR="../app/src/main/cpp"

# نفس قائمة المصادر تماماً المستخدمة في cli/linux/CMakeLists.txt (المُفسِّر الرسمي لسطر
# الأوامر)، باستثناءين اثنين فقط، كلاهما لأسباب بيئة المتصفح المعزولة (Sandbox) لا اللغة نفسها:
#   - rin_http.cpp  -> rin_http_wasm_stub.cpp  (لا fork/exec ولا مقابس TCP خام داخل WASM؛
#                       انظر تعليق ذلك الملف. site.rin/برامج RinHTML العادية لا تستخدم http* أصلاً)
#   - src/pkg/*.cpp (RinPM) غير مُضمَّنة: تفترض وصول شبكة/نظام ملفات حقيقي لسجلّات الحزم؛
#     ملفات .rin التي يشغّلها RinHTML داخل صفحة HTML لا تستورد حزماً عن بُعد.
emcc \
  -std=c++17 -O2 \
  -I "$CPP_DIR" \
  "$CPP_DIR"/rin_lexer.cpp \
  "$CPP_DIR"/rin_parser.cpp \
  "$CPP_DIR"/rin_make.cpp \
  "$CPP_DIR"/rin_interpreter.cpp \
  "$CPP_DIR"/loader_ui/library_loader_ui.cpp \
  "$CPP_DIR"/diagnostics/diagnostic.cpp \
  "$CPP_DIR"/diagnostics/source_manager.cpp \
  "$CPP_DIR"/diagnostics/diagnostic_engine.cpp \
  "$CPP_DIR"/diagnostics/diagnostic_renderer.cpp \
  "$CPP_DIR"/clc/clc_container.cpp \
  "$CPP_DIR"/clc/clc_compress.cpp \
  "$CPP_DIR"/clc/clc_security.cpp \
  "$CPP_DIR"/clc/clc_rin_opt.cpp \
  "$CPP_DIR"/clc/clc_zip_import.cpp \
  "$CPP_DIR"/clc/sha256.cpp \
  rin_http_wasm_stub.cpp \
  rinhtml_bridge.cpp \
  -o rin_engine.js \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=RinHTMLEngine \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s USE_ZLIB=1 \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
  -s EXPORTED_FUNCTIONS='["_malloc","_free"]'

echo "OK: web/rin_engine.js + web/rin_engine.wasm"

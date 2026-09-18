#!/usr/bin/env bash
# يبني assets/engine/rin.js + assets/engine/rin.wasm لصفحة playground.html —
# نفس محرّك Rin الحقيقي (rin_lexer.cpp / rin_parser.cpp / rin_interpreter.cpp،
# بلا أي تعديل) مُصرَّفاً WebAssembly، بنفس قائمة المصادر ونفس الدوال المُصدَّرة
# التي تستخدمها بالفعل .github/workflows/pages.yml في هذا المستودع.
#
# لا علاقة لهذا بمسار web/build_rinhtml_wasm.sh (ذاك يبني rinhtml_bridge.cpp
# لصفحات RinHTML/@view)؛ هذا المسار أبسط: دالة واحدة rin_run_source(source)
# تُشغِّل الشيفرة وتُعيد نص أي print() صدر عنها — نفس ما يفعله سطر الأوامر
# rin_run بالضبط، لكن داخل المتصفح.
#
# المتطلبات: Emscripten SDK مفعّل في PATH (emcc/em++).
#   https://emscripten.org/docs/getting_started/downloads.html
#
# الاستخدام (من جذر مستودع rinlang):
#   bash docs/assets/engine/build-engine.sh
#
# الناتج: docs/assets/engine/rin.js + docs/assets/engine/rin.wasm
# لا حاجة لأي خطوة أخرى بعدها — playground.html يكتشفهما تلقائياً.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
CPP_DIR="$REPO_ROOT/app/src/main/cpp"
WEB_DIR="$REPO_ROOT/web"

if ! command -v em++ >/dev/null 2>&1; then
  echo "خطأ: em++ غير موجود في PATH. فعِّل Emscripten SDK أولاً (source emsdk_env.sh)." >&2
  exit 1
fi

echo "== بناء محرّك Rin لـ playground.html (rin_run_source) =="

em++ -O2 -std=c++17 -fexceptions \
  -I "$CPP_DIR" \
  "$CPP_DIR"/rin_lexer.cpp \
  "$CPP_DIR"/rin_parser.cpp \
  "$CPP_DIR"/rin_make.cpp \
  "$CPP_DIR"/rin_interpreter.cpp \
  "$CPP_DIR"/rin_c_api.cpp \
  "$CPP_DIR"/indsin/rin_indsin_c_api.cpp \
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
  "$WEB_DIR"/rin_http_wasm_stub.cpp \
  "$WEB_DIR"/rin_wasm_bridge.cpp \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=RinModule \
  -s EXPORTED_RUNTIME_METHODS='["ccall","FS"]' \
  -s EXPORTED_FUNCTIONS='["_rin_run_source","_malloc","_free"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s STACK_SIZE=8388608 \
  -s DISABLE_EXCEPTION_CATCHING=0 \
  -s ENVIRONMENT=web \
  -s USE_ZLIB=1 \
  -o "$SCRIPT_DIR/rin.js"

echo "تم: docs/assets/engine/rin.js + docs/assets/engine/rin.wasm"
echo "افتح playground.html — سيُحمِّل المحرّك تلقائياً ويُفعِّل زر «تشغيل»."

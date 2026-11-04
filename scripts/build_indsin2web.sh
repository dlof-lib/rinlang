#!/usr/bin/env bash
# يبني مترجم Indsin → الويب (tools/indsin2web.cpp) إلى build/indsin2web.
# يحتاج g++ بدعم C++20 فقط؛ لا Gradle ولا NDK ولا X11. الاستعمال: bash scripts/build_indsin2web.sh [CXX]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; CXX="${1:-${CXX:-g++}}"
C="$ROOT/app/src/main/cpp"; OBJ="$ROOT/build/indsin2web_obj"; mkdir -p "$OBJ"
for f in rin_lexer rin_parser diagnostics/diagnostic diagnostics/diagnostic_engine diagnostics/diagnostic_renderer diagnostics/source_manager; do
  o="$OBJ/$(echo "$f" | tr / _).o"
  if [ ! -f "$o" ] || [ "$C/$f.cpp" -nt "$o" ]; then "$CXX" -std=c++20 -O1 -w -I"$C" -c "$C/$f.cpp" -o "$o"; fi
done
"$CXX" -std=c++20 -O1 -w -I"$C" -I"$ROOT/tools" "$ROOT/tools/indsin2web.cpp" "$OBJ"/*.o -o "$ROOT/build/indsin2web"
echo "built: $ROOT/build/indsin2web"

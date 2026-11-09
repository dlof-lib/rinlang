#!/usr/bin/env bash
# يبني «indsin» الموحّدة (tools/indsin.cpp): المحرّك الأصلي + مترجم الويب في ملف واحد → build/indsin
# كائنات وقت التشغيل تُخزَّن في build/ بأسماء dir_file.o وتُعاد عند تغيّر المصدر. الاستعمال: bash scripts/build_indsin.sh [CXX]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; CXX="${1:-${CXX:-g++}}"; C="$ROOT/app/src/main/cpp"; B="$ROOT/build"; mkdir -p "$B"
SRCS="rin_lexer rin_parser rin_make rin_container_sql rin_artifact rin_interpreter binfmt/elf_format binfmt/ar_format binfmt/pe_format binfmt/macho_format binfmt/coff_obj loader_ui/library_loader_ui rin_http diagnostics/diagnostic diagnostics/source_manager diagnostics/diagnostic_engine diagnostics/diagnostic_renderer clc/clc_container clc/clc_compress clc/clc_security clc/clc_rin_opt clc/clc_zip_import clc/sha256 indsin/rin_indsin_c_api rin_c_api"
FLAGS="-std=c++23 -O1 -w -I$C -I$ROOT/cli/rin_vm/include"
OBJS=""; PIDS=""
for s in $SRCS; do
  o="$B/$(echo "$s" | tr / _).o"; OBJS="$OBJS $o"
  stale=0; [ ! -f "$o" ] && stale=1; [ "$C/$s.cpp" -nt "$o" ] && stale=1
  if [ "$s" = "indsin/rin_indsin_c_api rin_c_api" ] && [ -n "$(find "$C/indsin" -name '*.h' -newer "$o" 2>/dev/null | head -1)" ]; then stale=1; fi
  if [ $stale = 1 ]; then ( "$CXX" $FLAGS -c "$C/$s.cpp" -o "$o" ) & PIDS="$PIDS $!"; fi
done
for p in $PIDS; do wait "$p"; done
"$CXX" $FLAGS -I"$ROOT/tools" "$ROOT/tools/indsin.cpp" $OBJS -lz -lpthread -ldl -o "$B/indsin"
echo "built: $B/indsin"

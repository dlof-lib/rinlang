#!/usr/bin/env bash
# Builds rin_indsin_live (the cross-platform Indsin Live Preview server).
#   Linux / macOS : tools/indsin-live/build.sh
#   Windows       : run it from an MSYS2 UCRT64 / MinGW64 shell (pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-zlib make)
# Output: dist/live/rin_indsin_live[.exe]      Env: CXX (default g++, else clang++), JOBS, OUT, OPT (default -O2)
# Object files are cached in $OUT/obj and rebuilt only when their source changed (the first build compiles the whole
# engine and takes a few minutes; later builds only relink + recompile the server).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
CORE="$REPO/app/src/main/cpp"
OUT="${OUT:-$REPO/dist/live}"
OPT="${OPT:--O2}"
CXX="${CXX:-}"
if [ -z "$CXX" ]; then if command -v g++ >/dev/null 2>&1; then CXX=g++; elif command -v clang++ >/dev/null 2>&1; then CXX=clang++; else echo "error: no g++/clang++ found" >&2; exit 1; fi; fi
JOBS="${JOBS:-$( (nproc || sysctl -n hw.ncpu || echo 2) 2>/dev/null | head -1)}"

EXE="rin_indsin_live"; LIBS="-lz -pthread"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE="rin_indsin_live.exe"; LIBS="-lz -lws2_32 -lshell32 -pthread" ;; esac

# Same engine source list as app/src/main/cpp/CMakeLists.txt's desktop tools (rin_check / indsinc).
ENGINE=(
  indsin/rin_indsin_c_api.cpp rin_c_api.cpp rin_lexer.cpp rin_parser.cpp rin_make.cpp rin_container_sql.cpp rin_artifact.cpp rin_interpreter.cpp
  binfmt/elf_format.cpp binfmt/ar_format.cpp binfmt/pe_format.cpp binfmt/macho_format.cpp binfmt/coff_obj.cpp
  loader_ui/library_loader_ui.cpp rin_http.cpp
  diagnostics/diagnostic.cpp diagnostics/source_manager.cpp diagnostics/diagnostic_engine.cpp diagnostics/diagnostic_renderer.cpp
  clc/clc_container.cpp clc/clc_compress.cpp clc/clc_security.cpp clc/clc_rin_opt.cpp clc/clc_zip_import.cpp clc/sha256.cpp
)
mkdir -p "$OUT/obj"
[ -f "$HERE/live_client.h" ] || python3 "$HERE/gen_client_h.py"
# the client is embedded: regenerate if index.html is newer than the header (needs python3 only in that case)
if [ "$HERE/client/index.html" -nt "$HERE/live_client.h" ]; then python3 "$HERE/gen_client_h.py"; fi

FLAGS="-std=c++23 $OPT -I $CORE"
pids=(); fail=0; objs=()
compile() { local src="$1" obj="$2"; "$CXX" $FLAGS -c "$src" -o "$obj" || return 1; }
export -f compile 2>/dev/null || true
for rel in "${ENGINE[@]}"; do
  src="$CORE/$rel"; obj="$OUT/obj/$(echo "$rel" | tr '/' '_').o"; objs+=("$obj")
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
    echo "[live] CC $rel"
    compile "$src" "$obj" & pids+=($!)
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do sleep 0.2; done
  fi
done
for p in "${pids[@]:-}"; do [ -n "$p" ] && { wait "$p" || fail=1; }; done
[ "$fail" -eq 0 ] || { echo "error: engine compile failed" >&2; exit 1; }

echo "[live] CC rin_indsin_live.cpp"
"$CXX" $FLAGS -c "$HERE/rin_indsin_live.cpp" -o "$OUT/obj/live_main.o"
echo "[live] LD $EXE"
"$CXX" -o "$OUT/$EXE" "$OUT/obj/live_main.o" "${objs[@]}" $LIBS
echo "built: $OUT/$EXE"
echo "try  : $OUT/$EXE examples/samples/indsin_showcase.rin"

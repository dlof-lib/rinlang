#!/bin/bash
# Rebuild rin + rinc from the rinlang-main source tree (g++ >= 13, zlib).
# Usage: ./build_from_source.sh /path/to/rinlang-main
set -e
R="${1:?path to rinlang-main}"; cd "$R"; mkdir -p build; C=app/src/main/cpp; L=cli/linux/src
core="rin_lexer rin_parser rin_make rin_container_sql rin_artifact rin_interpreter toolchain/rin_toolchain binfmt/elf_format binfmt/ar_format binfmt/pe_format binfmt/macho_format binfmt/coff_obj loader_ui/library_loader_ui rin_http rin_c_api indsin/rin_indsin_c_api diagnostics/diagnostic diagnostics/source_manager diagnostics/diagnostic_engine diagnostics/diagnostic_renderer clc/clc_container clc/clc_compress clc/clc_security clc/clc_rin_opt clc/clc_zip_import clc/sha256"
cli="main terminal/rin_terminal pkg/semver pkg/toml_lite pkg/sha256 pkg/json_lite pkg/manifest pkg/lockfile pkg/archive pkg/cache pkg/registry pkg/resolver pkg/package_ops pkg/auth pkg/cli_pkg"
objs=""
for s in $core; do o=build/$(echo $s|tr / _).o; g++ -std=c++23 -O2 -w -I$C -c $C/$s.cpp -o $o; objs="$objs $o"; done
for s in $cli; do o=build/cli_$(echo $s|tr / _).o; g++ -std=c++23 -O2 -w -I$L -I$C -Icli/rin_vm/include -c $L/$s.cpp -o $o; objs="$objs $o"; done
g++ $objs -o build/rin -lz -lpthread -ldl
g++ -std=c++23 -O2 -w compiler/rinc.cpp -o build/rinc
echo "built: build/rin build/rinc"

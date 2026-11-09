#!/bin/bash
# يبني C API للمحرّك ويشغّل اختبار الرسوم الجديدة. يفترض كائنات وقت التشغيل في build/ (انظر build/build.sh).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"; C=app/src/main/cpp; T=$(mktemp -d)
g++ -std=c++23 -O1 -w -I$C -Icli/rin_vm/include -c $C/indsin/rin_indsin_c_api.cpp -o $T/c_api.o
OBJS=$(ls build/*.o | grep -v -E "cli_main|indsin_rin_indsin_c_api|cli_terminal")
g++ -std=c++23 -O1 -w -I$C -Icli/rin_vm/include tests/indsin_pro_native/pro_native_test.cpp $T/c_api.o $OBJS -lz -lpthread -ldl -o $T/pnt
$T/pnt

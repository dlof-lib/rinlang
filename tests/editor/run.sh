#!/bin/sh
# يبني ويشغّل الاختبار الذاتي لأدوات المحرر على سطح المكتب (g++). من جذر المشروع: sh tests/editor/run.sh
set -e
C=app/src/main/cpp; O=$(mktemp -d)
for f in rin_lexer rin_parser; do g++ -std=c++17 -O0 -I$C -I$C/diagnostics -c $C/$f.cpp -o $O/$f.o & done
for f in diagnostic source_manager diagnostic_engine diagnostic_renderer; do g++ -std=c++17 -O0 -I$C -I$C/diagnostics -c $C/diagnostics/$f.cpp -o $O/$f.o & done
g++ -std=c++17 -O0 -I$C -I$C/diagnostics -I$C/editor -c $C/editor/rin_editor_engine.cpp -o $O/engine.o &
g++ -std=c++17 -O0 -I$C -I$C/diagnostics -c tests/editor/editor_selftest.cpp -o $O/t.o &
wait
g++ $O/*.o -o $O/selftest && $O/selftest tests/loops/*.rin

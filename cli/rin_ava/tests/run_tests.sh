#!/usr/bin/env bash
# اختبارات end-to-end لـ rin_ava.  الاستخدام:  tests/run_tests.sh <rin_ava> <rin>
set -u
AVA=$(realpath "${1:?usage: run_tests.sh <rin_ava> <rin>}"); RIN=$(realpath "${2:?usage: run_tests.sh <rin_ava> <rin>}")
export RIN_AVA_RUNTIME="$RIN" NO_COLOR=1 RIN_AVA_LANG=en
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT; cd "$T"
pass=0; fail=0
ok()  { pass=$((pass+1)); echo "  ok   - $1"; }
bad() { fail=$((fail+1)); echo "  FAIL - $1"; }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (expected [$2] got [$3])"; fi; }

printf 'print "Hello from Rin!";\nprint 2 + 2;\n' > hello.rin
printf 'let = ;\nprint 1;\n' > bad.rin
printf 'print "boom";\nlet a = [1];\nprint a[9];\n' > rt.rin

"$AVA" hello.rin -o hello; check "build rc" 0 $?
check "output" "Hello from Rin!
4" "$(./hello)"
check "default name a.out" 0 "$("$AVA" hello.rin >/dev/null 2>&1; [ -x a.out ] && echo 0 || echo 1)"
mkdir sub && (cd sub && check "runs from other cwd" "Hello from Rin!
4" "$(../hello)")
mv hello moved && check "relocatable" "Hello from Rin!
4" "$(./moved)"
"$AVA" -fsyntax-only hello.rin; check "-fsyntax-only ok" 0 $?
rm -f nope; "$AVA" bad.rin -o nope 2>err.txt; check "syntax error rc" 1 $?
check "no output on error" 1 "$([ -e nope ] && echo 0 || echo 1)"
check "g++-style diagnostic" 1 "$(grep -c '^bad.rin:1:[0-9]*: error:' err.txt)"
"$AVA" hello.txt -o x 2>/dev/null; check "bad extension rc" 1 $?
cp hello.rin hello.txt; "$AVA" -x rin hello.txt -o fx; check "-x rin" 0 $?
"$AVA" 2>/dev/null; check "no input rc" 1 $?
"$AVA" -### hello.rin -o dry >/dev/null 2>&1; check "-### writes nothing" 1 "$([ -e dry ] && echo 0 || echo 1)"
"$AVA" -Wall -O2 -pipe -std=c++17 hello.rin -o w; check "g++ flags accepted" 0 $?
"$AVA" --bogus hello.rin 2>/dev/null; check "unknown option rc" 1 $?
"$AVA" -dumpversion | grep -q . ; check "-dumpversion" 0 $?
"$AVA" hello.rin -o rt_prog && "$AVA" rt.rin -o rt_prog; ./rt_prog >/dev/null 2>&1; check "runtime error → nonzero" 1 $?
"$RIN" hello.rin >/dev/null; check "plain rin unaffected" 0 $?
echo; echo "passed=$pass failed=$fail"; [ "$fail" = 0 ]

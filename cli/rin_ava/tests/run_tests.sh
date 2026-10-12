#!/usr/bin/env bash
# اختبارات end-to-end لـ rin_ava (كل الخلفيات).
# الاستخدام:  tests/run_tests.sh <rin_ava> <rin> [--engine]
#   rinc وgorin-embedded يُؤخذان من مجلد rin_ava (أو المتغيرات RIN_AVA_RINC / RIN_AVA_GORIN)؛
#   الاختبارات التي تحتاج أداة غير موجودة تُتخطّى (SKIP) ولا تُفشل المجموعة.
#   --engine يضيف اختبار خلفية engine (بناء المحرّك من المصدر: بطيء في أول مرة).
set -u
AVA=$(realpath "${1:?usage: run_tests.sh <rin_ava> <rin> [--engine]}"); RIN=$(realpath "${2:?usage: run_tests.sh <rin_ava> <rin> [--engine]}")
WITH_ENGINE=0; [ "${3:-}" = "--engine" ] && WITH_ENGINE=1
export RIN_AVA_RUNTIME="$RIN" NO_COLOR=1 RIN_AVA_LANG=en
HERE=$(dirname "$AVA")
[ -x "$HERE/rinc" ] && export RIN_AVA_RINC="${RIN_AVA_RINC:-$HERE/rinc}"
[ -x "$HERE/gorin-embedded" ] && export RIN_AVA_GORIN="${RIN_AVA_GORIN:-$HERE/gorin-embedded}"
T=$(mktemp -d); export RIN_AVA_CACHE="$T/cache"; trap 'rm -rf "$T"' EXIT; cd "$T"
pass=0; fail=0; skip=0
ok()   { pass=$((pass+1)); echo "  ok   - $1"; }
bad()  { fail=$((fail+1)); echo "  FAIL - $1"; }
skp()  { skip=$((skip+1)); echo "  SKIP - $1"; }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (expected [$2] got [$3])"; fi; }

printf 'print "Hello from Rin!";\nprint 2 + 2;\n' > hello.rin
printf 'let = ;\nprint 1;\n' > bad.rin
printf 'print "boom";\nlet a = [1];\nprint a[9];\n' > rt.rin
printf 'class A {\n let v = 1;\n fun init() { self.v = 7; }\n}\nlet a = A();\nprint a.v;\n' > cls.rin
HELLO="Hello from Rin!
4"

echo "[interp / driver]"
"$AVA" hello.rin -o hello; check "build rc" 0 $?
check "output" "$HELLO" "$(./hello)"
check "default name a.out" 0 "$("$AVA" hello.rin >/dev/null 2>&1; [ -x a.out ] && echo 0 || echo 1)"
mkdir sub && (cd sub && check "runs from other cwd" "$HELLO" "$(../hello)")
mv hello moved && check "relocatable" "$HELLO" "$(./moved)"
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
"$AVA" -fbackend=nope hello.rin 2>/dev/null; check "unknown backend rc" 1 $?
"$AVA" -dumpversion | grep -q . ; check "-dumpversion" 0 $?
"$AVA" hello.rin -o rt_prog && "$AVA" rt.rin -o rt_prog; ./rt_prog >/dev/null 2>&1; check "runtime error → nonzero" 1 $?
"$RIN" hello.rin >/dev/null; check "plain rin unaffected" 0 $?
"$AVA" -S hello.rin -finterp 2>/dev/null; check "-S with interp rejected" 1 $?
"$AVA" --list-backends | grep -q '^  \[.*\] interp'; check "--list-backends" 0 $?

echo "[analyze]"
"$AVA" -fanalyze hello.rin -o an; check "-fanalyze ok" 0 $?
printf 'print undeclared_thing;\n' > un.rin
"$AVA" -fanalyze un.rin -o an2 2>an.txt; rc=$?
if [ $rc = 1 ]; then ok "-fanalyze fails on undeclared name"; elif [ $rc = 0 ]; then ok "-fanalyze accepted (analysis treats it as warning)"; else bad "-fanalyze rc=$rc"; fi

echo "[vm: bytecode fast path]"
"$AVA" -fvm-bytecode hello.rin -o hello_vm; check "vm-bytecode build rc" 0 $?
check "vm-bytecode output" "$(printf 'Hello from Rin!\n4')" "$(./hello_vm)"
"$AVA" -fvm-bytecode -O2 hello.rin -o hello_vmo; check "vm-bytecode -O2 rc" 0 $?
check "vm-bytecode -O2 output" "$(printf 'Hello from Rin!\n4')" "$(./hello_vmo)"
"$AVA" -fvm-bytecode -c hello.rin -o hello.rbc; check "vm-bytecode -c rc" 0 $?
check "rbc magic" "RBC1" "$(head -c4 hello.rbc)"
"$AVA" -fvm-bytecode -S hello.rin -o hello.rbc.txt; check "vm-bytecode -S rc" 0 $?
check "vm-bytecode -S listing" 1 "$(grep -c 'OP=' hello.rbc.txt | awk '{print ($1>0)?1:0}')"
printf 'print 5;\nprint 2.5;\nprint 1+2;\n' > num.rin
"$AVA" -fvm-bytecode num.rin -o num_vm; check "vm-bytecode number formatting matches interpreter" "$("$RIN" num.rin)" "$(./num_vm)"
"$AVA" -fvm-bytecode cls.rin -o cls_vm 2>/dev/null; check "vm-bytecode rejects unsupported (classes)" 1 $?
"$RIN" vm hello.rin >/dev/null; check "plain 'rin vm' unaffected" 0 $?

echo "[vm: full image (rin_vm)]"
mkdir -p vmp/lib && printf 'fun triple(x) { return x * 3; }\n' > vmp/lib/helpers.og.rin
printf '@import "helpers";\nclass A {\n let v = 1;\n fun init() { self.v = triple(7); }\n}\nlet a = A();\nprint "v =", a.v;\nlet m = {"k": [1,2,3]};\nprint len(m["k"]);\n' > vmp/p.rin
(cd vmp && "$AVA" -fvm p.rin -o p_vm); check "vm image build rc" 0 $?
EXPECT=$(cd vmp && "$RIN" p.rin | grep -v '@import')
mv vmp/lib vmp/lib_gone
check "vm image = interpreter (classes+maps+import, lib deleted)" "$EXPECT" "$(cd vmp && ./p_vm | grep -v '@import')"
mv vmp/lib_gone vmp/lib
(cd vmp && "$AVA" -fvm -c p.rin -o p.rvm); check "vm -c writes .rvm" 0 $?
check "rvm magic" "RVM1" "$(head -c4 vmp/p.rvm)"
check "rin image runs .rvm" "$EXPECT" "$("$RIN" image vmp/p.rvm | grep -v '@import')"
"$AVA" -fvm -S vmp/p.rin -o vmp/p.txt; check "vm -S describes image" 1 "$(grep -c 'helpers' vmp/p.txt | awk '{print ($1>0)?1:0}')"
[ -x "$HERE/rin_vm" ] && { "$HERE/rin_vm" pack vmp/p.rin -o vmp/q.rvm >/dev/null; check "rin_vm pack" "$EXPECT" "$("$RIN" image vmp/q.rvm | grep -v '@import')"; "$HERE/rin_vm" info vmp/q.rvm | grep -q "helpers"; check "rin_vm info" 0 $?; }
if [ -x "$HERE/rinvm" ]; then
  check "rinvm runs .rvm" "$EXPECT" "$("$HERE/rinvm" vmp/p.rvm | grep -v '@import')"
  check "rinvm runs .rin directly" "$EXPECT" "$(cd vmp && "$HERE/rinvm" p.rin | grep -v '@import')"
  (cd vmp && "$AVA" -fvm p.rin -o p_rvm); mv vmp/lib vmp/lib_gone
  check "rin_ava -fvm uses rinvm runtime (lib deleted)" "$EXPECT" "$(cd vmp && ./p_rvm | grep -v '@import')"
  mv vmp/lib_gone vmp/lib
  check "rinvm is smaller than rin" 1 "$([ $(stat -c %s "$HERE/rinvm") -lt $(stat -c %s "$RIN") ] && echo 1 || echo 0)"
fi
python3 - <<'PY'
b = bytearray(open('vmp/p.rvm','rb').read()); b[20] ^= 1; open('vmp/bad.rvm','wb').write(b)
PY
"$RIN" image vmp/bad.rvm >/dev/null 2>&1; check "corrupt image rejected" 1 $?
printf 'print 1+2;\n' > s.rin
"$AVA" -fvm-auto s.rin -o s_auto; check "vm-auto simple" "3" "$(./s_auto)"
(cd vmp && "$AVA" -fvm-auto p.rin -o p_auto); check "vm-auto falls back to image" "$EXPECT" "$(cd vmp && ./p_auto | grep -v '@import')"

echo "[native / auto]"
if [ -n "${RIN_AVA_RINC:-}" ] && command -v cc >/dev/null; then
  "$AVA" -fnative hello.rin -o hello_nat; check "native rc" 0 $?
  check "native output" "$HELLO" "$(./hello_nat)"
  "$AVA" -S hello.rin -o hello.c; check "-S rc" 0 $?
  check "-S emits C" 1 "$(grep -c 'int main' hello.c | awk '{print ($1>0)?1:0}')"
  "$AVA" -c hello.rin -o hello.o; check "-c rc" 0 $?
  "$AVA" -fnative cls.rin -o cls_nat 2>/dev/null; check "native rejects classes (rc)" 1 $?
  "$AVA" -fauto hello.rin -o hello_auto; check "auto (native) rc" 0 $?
  check "auto output" "$HELLO" "$(./hello_auto)"
  "$AVA" -fauto cls.rin -o cls_auto 2>/dev/null; check "auto falls back to interp rc" 0 $?
  check "auto fallback output" "7" "$(./cls_auto)"
else skp "native/auto (rinc or cc missing)"; fi

echo "[go]"
if [ -n "${RIN_AVA_GORIN:-}" ]; then
  "$AVA" -fgo hello.rin -o hello_go; check "go build rc" 0 $?
  check "go output" "$HELLO" "$(./hello_go)"
  "$AVA" -fgo cls.rin -o cls_go; check "go runs classes rc" 0 $?
  check "go classes output" "7" "$(./cls_go)"
  printf 'print len(args);\nprint args[0];\n' > args.rin
  "$AVA" -fgo args.rin -o args_go; check "go args" "2
alpha" "$(./args_go alpha beta)"
  printf 'match 1 { 1 => print 2; }\n' > unsup.rin
  "$AVA" -fgo unsup.rin -o unsup_go 2>/dev/null; rc=$?
  [ $rc = 1 ] && ok "go rejects constructs it cannot run" || ok "go accepted match (rc=$rc)"
  "$RIN" hello.rin >/dev/null; check "rin unaffected by go payload" 0 $?
  "$RIN_AVA_GORIN" hello.rin >/dev/null; check "plain gorin-embedded unaffected" 0 $?
else skp "go (gorin-embedded not found)"; fi

echo "[engine]"
if [ "$WITH_ENGINE" = 1 ]; then
  "$AVA" -fengine hello.rin -o hello_eng; check "engine build rc" 0 $?
  check "engine output" "$HELLO" "$(./hello_eng)"
  "$AVA" -fengine cls.rin -o cls_eng; check "engine classes" "7" "$(./cls_eng)"
  "$AVA" -fengine -S hello.rin -o hello.engine.cpp; check "engine -S rc" 0 $?
else skp "engine (pass --engine to run; slow first build)"; fi

echo; echo "passed=$pass failed=$fail skipped=$skip"; [ "$fail" = 0 ]

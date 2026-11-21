#!/bin/bash
# اختبار نظام القيم: وحدات (Value + vx) + قواعد التدقيق VALUE-001/002/003 + غياب الإنذارات الكاذبة على كل الأمثلة.
set -u; ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"; FAIL=0
ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; FAIL=1; }
for t in value_test value_ext_test; do
  g++ -std=c++20 -Wall -Wextra -I app/src/main/cpp tests/indsin_value/$t.cpp -o /tmp/$t 2>/dev/null && /tmp/$t | grep -q "ALL PASSED" && ok "unit: $t" || bad "unit: $t"
done
bash scripts/build_indsin.sh >/dev/null 2>&1 && ok "build indsin" || { bad "build indsin"; exit 1; }
OUT=$(build/indsin check tests/indsin_value/bad_values.rin 2>&1)
[ "$(echo "$OUT" | grep -c 'VALUE-001')" = 2 ] && ok "VALUE-001 ×2 (hex + rgb)" || bad "VALUE-001"
echo "$OUT" | grep -q 'VALUE-002.*text_duration' && ok "VALUE-002 (bad duration)" || bad "VALUE-002"
echo "$OUT" | grep -q 'VALUE-003.*type_duration' && ok "VALUE-003 (unit ignored)" || bad "VALUE-003"
echo "$OUT" | grep -q '#e\|text_muted' && bad "valid line must stay silent" || ok "valid values stay silent"
FP=0; for f in examples/samples/*.rin; do build/indsin check "$f" 2>&1 | grep -q 'VALUE-00' && { echo "  false positive: $f"; FP=1; }; done
[ $FP = 0 ] && ok "no VALUE warnings on any shipped sample" || bad "false positives on samples"
[ $FAIL = 0 ] && echo "ALL PASSED" || { echo "SOME FAILED"; exit 1; }

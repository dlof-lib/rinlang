#!/bin/bash
# اختبار الأداة الموحّدة indsin: بناء، build/native/check، صفحة هجينة، تكافؤ نصوص المحرّكين.
set -u; ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; cd "$ROOT"; FAIL=0
ok(){ echo "ok   $1"; }; bad(){ echo "FAIL $1"; FAIL=1; }
bash scripts/build_indsin.sh >/dev/null 2>&1 && ok "build indsin" || { bad "build indsin"; exit 1; }
BIN=build/indsin; OUT=$(mktemp -d)
$BIN build examples/samples/indsin_pro_native.rin -o "$OUT" --hybrid >/dev/null 2>&1 && [ -s "$OUT/indsin_pro_native.html" ] && [ -s "$OUT/indsin_pro_native.native.png" ] && [ -s "$OUT/indsin_pro_native.native.json" ] && [ -s "$OUT/indsin_pro_native.native.html" ] && ok "build --hybrid writes web+native files" || bad "build outputs"
grep -q 'id="indsin-native"' "$OUT/indsin_pro_native.html" && ok "hybrid page embeds draw-list" || bad "embed"
$BIN build examples/samples/indsin_pro_native.rin -o "$OUT/plain" >/dev/null 2>&1; grep -q 'id="indsin-native"' "$OUT/plain/indsin_pro_native.html" && bad "plain build must not embed" || ok "plain build has no native embed"
$BIN native examples/samples/indsin_pro_native.rin --json "$OUT/n.json" --width 320 && grep -q '"width":320' "$OUT/n.json" && ok "native --width" || bad "native width"
$BIN check examples/samples/indsin_pro_native.rin >/dev/null 2>&1 && ok "check passes" || bad "check"
$BIN bogus >/dev/null 2>&1; [ $? -eq 64 ] && ok "unknown command → 64" || bad "unknown command"
printf '<Column#r><Text>x' > "$OUT/bad.rin"; $BIN build "$OUT/bad.rin" -o "$OUT/b" >/dev/null 2>&1; [ $? -ne 0 ] && ok "syntax error → non-zero" || bad "syntax error rc"
if node -e "require('playwright')" 2>/dev/null; then
  node tests/indsin_unified/browser_hybrid.js "$OUT/indsin_pro_native.html" "$OUT/indsin_pro_native.native.json" && ok "browser: hybrid bridge" || bad "browser hybrid"
else echo "skip browser (playwright غير متاح)"; fi
[ $FAIL = 0 ] && echo "ALL PASSED" || { echo "SOME FAILED"; exit 1; }

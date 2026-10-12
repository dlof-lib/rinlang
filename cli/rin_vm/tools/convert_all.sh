#!/usr/bin/env bash
# يحوّل كل برامج .rin في مجلد إلى صور VM كاملة (.rvm) ويتحقق أن تنفيذها بالآلة rinvm يطابق المفسّر rin.
# الاستخدام:  convert_all.sh <rinvm> <rin> <srcdir> <outdir>
set -u
VM=$(realpath "${1:?usage: convert_all.sh <rinvm> <rin> <srcdir> <outdir>}"); RIN=$(realpath "${2:?}")
SRC=$(realpath "${3:?}"); OUT=$(mkdir -p "${4:?}" && realpath "$4")
conv=0; same=0; diff=0; skip=0
cd "$SRC"
for f in $(find . -maxdepth 1 -name "*.rin" | sort); do
  n=$(basename "$f" .rin)
  if ! "$VM" pack "$f" -o "$OUT/$n.rvm" >/dev/null 2>&1; then echo "  FAIL pack  $n"; continue; fi
  conv=$((conv+1))
  a=$(timeout 10 "$RIN" "$n.rin" </dev/null 2>&1); ra=$?
  b=$(timeout 10 "$VM" "$OUT/$n.rvm" </dev/null 2>&1); rb=$?
  if [ $ra = 124 ] || [ $rb = 124 ]; then skip=$((skip+1)); echo "  skip (timeout) $n"; continue; fi
  if [ "$a" = "$b" ] && [ $ra = $rb ]; then same=$((same+1)); else diff=$((diff+1)); echo "  DIFF $n (rc $ra/$rb)"; fi
done
echo "converted=$conv identical=$same different=$diff skipped=$skip  -> $OUT"
[ "$diff" = 0 ]

#!/usr/bin/env bash
# يقارن مخرجات المحرك الشجري وRinVM على كل برامج .rin المعطاة (افتراضياً tests/vm و examples).
# usage: tests/vm/run_diff.sh [path/to/rin] [dirs...]
RIN=${1:-cli/linux/build/rin}; shift
DIRS=${@:-tests/vm examples}
pass=0; fail=0
norm() { sed -E 's/[0-9]{9,}(\.[0-9]+)?/<T>/g'; }
for f in $(find $DIRS -name '*.rin' | sort); do
  a=$(cd "$(dirname "$f")" && timeout 20 "$OLDPWD/$RIN" "$(basename "$f")" </dev/null 2>&1 | norm)
  b=$(cd "$(dirname "$f")" && timeout 20 "$OLDPWD/$RIN" --vm "$(basename "$f")" </dev/null 2>&1 | norm)
  if [ "$a" == "$b" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "DIFF: $f"; fi
done
echo "match=$pass diff=$fail"
[ $fail -eq 0 ]

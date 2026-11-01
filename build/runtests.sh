#!/bin/bash
cd /home/claude/rinlang-main
R=build/rin_run; pass=0; fail=0; failed=()
for f in tests/verification/*.rin tests/loops/*.rin tests/print/*.rin tests/image/*.rin tests/bench/numeric_edge.rin; do
  e="${f%.rin}.expected"; [ -f "$e" ] || continue
  in="${f%.rin}.stdin"; [ -f "$in" ] || in=/dev/null
  out=$(timeout 30 $R "$f" < "$in" 2>&1)
  if [ "$out" == "$(cat "$e")" ]; then pass=$((pass+1)); else fail=$((fail+1)); failed+=("$f"); fi
done
echo "pass=$pass fail=$fail"; printf '%s\n' "${failed[@]}"

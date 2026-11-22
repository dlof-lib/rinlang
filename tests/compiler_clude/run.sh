#!/bin/bash
# tests/compiler_clude/run.sh — يشغّل كل *.rin ويقارن بـ *.expected   (RIN=/path/to/rin ؛ --update لإعادة التوليد)
cd "$(dirname "$0")"
RIN="${RIN:-rin}"
rm -rf .out
pass=0; fail=0
for t in *.rin; do
  n="${t%.rin}"
  out="$("$RIN" "$t" 2>&1 | grep -v -e '^📥' -e '^📦')"
  if [ "$1" = "--update" ]; then printf '%s\n' "$out" > "$n.expected"; echo "updated $n"; continue; fi
  if [ "$out" == "$(cat "$n.expected")" ]; then pass=$((pass+1)); echo "PASS $n"; else fail=$((fail+1)); echo "FAIL $n"; diff <(printf '%s\n' "$out") "$n.expected" | head -20; fi
done
echo "pass=$pass fail=$fail"; [ $fail -eq 0 ]

#!/bin/bash
R=$1; shift
for f in "$@"; do
  s=$(date +%s.%N); out=$(timeout 120 $R $f.rin 2>&1 | head -3 | tr '\n' ' '); e=$(date +%s.%N)
  printf "%-10s %6.2fs  %s\n" $f $(echo "$e - $s" | bc) "$out"
done

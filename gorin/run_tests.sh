#!/bin/bash
# يشغّل اختبارات Go ثم يقارن gorin بملفات testdata. يتطلب Go >= 1.21.
set -e
cd "$(dirname "$0")"
go vet ./...
go test ./...
go build -o gorin ./cmd/gorin && go build -o gorin-embedded ./cmd/rin
for f in testdata/*.rin; do
  out=$(./gorin-embedded "$f" 2>&1)
  if [ "$out" == "$(cat "${f%.rin}.expected")" ]; then echo "PASS $f"; else echo "FAIL $f"; fi
done
echo "--- bench"; for b in fib str_cat map_big; do s=$(date +%s.%N); ./gorin-embedded bench/$b.rin >/dev/null; e=$(date +%s.%N); printf "%-8s %.2fs\n" $b $(echo "$e - $s" | bc); done

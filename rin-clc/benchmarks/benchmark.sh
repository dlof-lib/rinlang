#!/usr/bin/env bash
# benchmark.sh <corpus_dir> — يقيس CLC مقابل zip -9 وtar.gz على مجلد حقيقي،
# ويطبع نتائج حقيقية (بلا أي رقم مُختلَق). يتطلّب: build/clc, zip, tar, gzip.
set -euo pipefail
CORPUS="${1:?usage: benchmark.sh <corpus_dir>}"
CLC="$(dirname "$0")/../build/clc"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "Corpus: $CORPUS"
du -sh "$CORPUS"
echo

echo "--- CLC (level 2) ---"
$CLC pack "$CORPUS" -o "$TMP/out.rcl" --name bench --level 2

echo
echo "--- CLC (--ultra) ---"
$CLC pack "$CORPUS" -o "$TMP/out_ultra.rcl" --name bench --level ultra

echo
echo "--- zip -9 ---"
( cd "$(dirname "$CORPUS")" && zip -qr9 "$TMP/out.zip" "$(basename "$CORPUS")" )
ls -la "$TMP/out.zip"

echo
echo "--- tar.gz ---"
tar czf "$TMP/out.tar.gz" -C "$(dirname "$CORPUS")" "$(basename "$CORPUS")"
ls -la "$TMP/out.tar.gz"

echo
echo "=== Summary (bytes) ==="
printf "%-12s %s\n" "CLC(2):"     "$(stat -c%s "$TMP/out.rcl")"
printf "%-12s %s\n" "CLC(ultra):" "$(stat -c%s "$TMP/out_ultra.rcl")"
printf "%-12s %s\n" "zip -9:"     "$(stat -c%s "$TMP/out.zip")"
printf "%-12s %s\n" "tar.gz:"     "$(stat -c%s "$TMP/out.tar.gz")"

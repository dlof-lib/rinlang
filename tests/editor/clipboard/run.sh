#!/bin/sh
# يتطلب kotlinc في PATH (أو KOTLINC=/path/to/kotlinc). يشغّل اختبار منطق النسخ/اللصق النقي.
set -e
D=$(cd "$(dirname "$0")" && pwd)
ROOT="$D/../../.."
OUT=$(mktemp -d)
${KOTLINC:-kotlinc} "$ROOT/app/src/main/java/com/dlof/rinlang/ClipboardText.kt" "$D/ClipboardTextTest.kt" -include-runtime -d "$OUT/t.jar" 2>&1 | grep -v "^warning" || true
java -jar "$OUT/t.jar"

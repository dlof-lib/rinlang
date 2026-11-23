#!/usr/bin/env bash
# يشغّل اختبارات المنطق الخالص (tools/kotlin-tests/*Test.kt) بلا أندرويد.
# الاستخدام:  tools/run-kotlin-tests.sh            (يبحث عن kotlinc أو ينزّله تلقائياً)
#             KOTLINC=/path/to/kotlinc tools/run-kotlin-tests.sh
#             KOTLIN_VERSION=2.1.20 tools/run-kotlin-tests.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/app/src/main/java/com/dlof/rinlang"
TESTS="$ROOT/tools/kotlin-tests"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
VERSION="${KOTLIN_VERSION:-2.1.20}"

# 1) kotlinc: من البيئة، أو من الكاش، أو تنزيل مرة واحدة
if [ -z "${KOTLINC:-}" ]; then
  if command -v kotlinc >/dev/null 2>&1; then KOTLINC="$(command -v kotlinc)"
  else
    CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/rin-kotlinc/$VERSION"
    if [ ! -x "$CACHE/kotlinc/bin/kotlinc" ]; then
      echo "تنزيل kotlinc $VERSION ..."
      mkdir -p "$CACHE"
      curl -fsSL "https://github.com/JetBrains/kotlin/releases/download/v$VERSION/kotlin-compiler-$VERSION.zip" -o "$CACHE/k.zip"
      unzip -q -o "$CACHE/k.zip" -d "$CACHE" && rm "$CACHE/k.zip"
    fi
    KOTLINC="$CACHE/kotlinc/bin/kotlinc"
  fi
fi
STDLIB="$(dirname "$KOTLINC")/../lib/kotlin-stdlib.jar"
[ -f "$STDLIB" ] || STDLIB="$(find "$(dirname "$KOTLINC")/.." -name 'kotlin-stdlib.jar' | head -1)"

# 2) ملفات المنطق الخالص = ما لا يستورد android/androidx/org.json ولا يستخدم موارد R
PURE=()
for f in "$SRC"/*.kt; do
  if ! grep -qE '^import (android|androidx|com\.google|org\.json)|R\.(string|id|color|drawable)' "$f"; then PURE+=("$f"); fi
done
echo "ملفات المنطق الخالص: ${#PURE[@]}"
"$KOTLINC" "${PURE[@]}" -d "$WORK/pure.jar" -nowarn 2>&1 | grep -v '^warning' || true
[ -f "$WORK/pure.jar" ] || { echo "فشل ترجمة ملفات المنطق الخالص"; exit 1; }

# 3) كل اختبار يُترجَم ويُشغَّل منفرداً (لكل منها main في الحزمة الافتراضية)
FAIL=0
for t in "$TESTS"/*Test.kt; do
  n="$(basename "$t" .kt)"
  if "$KOTLINC" "$t" -cp "$WORK/pure.jar" -d "$WORK/$n.jar" -nowarn 2>&1 | grep -E '^.*error:' ; then FAIL=1; echo "✗ $n (ترجمة)"; continue; fi
  if out="$(java -cp "$WORK/pure.jar:$WORK/$n.jar:$STDLIB" "${n}Kt" 2>&1)" && [ "$(echo "$out" | tail -1)" = "OK" ]; then echo "✓ $n"
  else echo "✗ $n"; echo "$out" | head -5; FAIL=1; fi
done
exit $FAIL

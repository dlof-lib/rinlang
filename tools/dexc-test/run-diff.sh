#!/usr/bin/env bash
# مقارنة تفاضلية: كل cases/*.rin يُنفَّذ بالمفسّر الأصلي (RIN=مسار rin) وبمسار DEX (تصريف → تحقّق → تنفيذ) والمخرجات يجب أن تتطابق.
# الاستخدام: RIN=/path/to/rin CLASSES=/path/to/compiled/classes ./run-diff.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
RIN=${RIN:-rin}; CLASSES=${CLASSES:-/tmp/out/classes}; STD=${STD:-/opt/gradle-8.14.3/lib/kotlin-stdlib-2.0.21.jar}; H=${H:-/tmp/out/h}
pass=0; fail=0; unsup=0
for f in "$HERE"/cases/*.rin; do
  n=$(basename "$f" .rin)
  real=$("$RIN" run "$f" 2>&1 | awk '/^ +--> /{exit} {print}' | sed -e '/./,$!d')
  mine=$(java -cp "$CLASSES:$STD:$H" Driver "$f" 2>&1 | grep -v JAVA_TOOL)
  if [[ "$mine" == UNSUPPORTED* ]]; then echo "UNSUP  $n: $mine"; unsup=$((unsup+1)); continue; fi
  if [[ "$real" == "$mine" ]]; then pass=$((pass+1)); else
    fail=$((fail+1)); echo "FAIL   $n"; diff <(echo "$real") <(echo "$mine") | head -8; fi
done
echo "pass=$pass fail=$fail unsupported=$unsup"

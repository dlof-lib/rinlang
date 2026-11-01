#!/bin/bash
# يختبر lib/gorinkit.og.rin مع محرك C++ الحقيقي ضد بوابة وهمية (Python) تحاكي عقد gorin.
# الاستعمال من جذر المشروع:  bash gorin/bridge_test/run.sh /path/to/rin
RIN=${1:-rin}
python3 gorin/bridge_test/fake_gateway.py & PID=$!
sleep 1
cp gorin/bridge_test/gorinkit_test.rin ./_kit_test.rin
"$RIN" _kit_test.rin
rm -f _kit_test.rin; kill $PID

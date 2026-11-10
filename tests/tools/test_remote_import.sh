#!/bin/bash
# @import من رابط + icons.fromUrl ضدّ خادم محلي. usage: RIN=build/rin_run tests/tools/test_remote_import.sh
RIN=${RIN:-rin}; D=$(mktemp -d); trap 'kill $SP 2>/dev/null; rm -rf $D' EXIT
echo 'fun greetExt(n) { return "hello " + n + " from remote"; }' > $D/hello.og.rin
echo '{"icons":{"star":"M12 2L15 9L22 9Z","dot":{"svg":"<svg><circle cx=\"12\" cy=\"12\" r=\"3\"/></svg>"}}}' > $D/pack.json
echo '<svg><symbol id="sq"><rect x="4" y="4" width="16" height="16"/></symbol></svg>' > $D/sprite.svg
PORT=$((20000 + RANDOM % 20000)); python3 -m http.server $PORT --directory $D --bind 127.0.0.1 >/dev/null 2>&1 & SP=$!; sleep 1
H=$(sha256sum $D/hello.og.rin | cut -d' ' -f1); fail=0
chk() { if [ "$2" == "$3" ]; then echo "  [PASS] $1"; else echo "  [FAIL] $1 (got: $3)"; fail=1; fi; }
U=http://127.0.0.1:$PORT
printf '@import "%s/hello.og.rin#sha256=%s";\nprint greetExt("rin");\n' $U $H > $D/a.rin
chk "pinned remote @import works" "hello rin from remote" "$($RIN $D/a.rin 2>&1 | grep '^hello')"
printf '@import "%s/hello.og.rin#sha256=deadbeef";\n' $U > $D/b.rin
chk "sha256 mismatch is rejected" 1 "$($RIN $D/b.rin 2>&1 | grep -c 'cannot load remote')"
printf '@import "%s/nope.og.rin";\n' $U > $D/c.rin
chk "HTTP 404 is rejected" 1 "$($RIN $D/c.rin 2>&1 | grep -c 'cannot load remote')"
printf '@import "http://example.com/x.og.rin";\n' > $D/d.rin
chk "plain http to a non-local host is rejected" 1 "$($RIN $D/d.rin 2>&1 | grep -c 'cannot load remote')"
chk "RIN_REMOTE_IMPORT=0 disables it" 1 "$(RIN_REMOTE_IMPORT=0 $RIN $D/a.rin 2>&1 | grep -c 'cannot load remote')"
printf 'print icons.fromUrl("%s/pack.json", "u:");\nprint icons.fromUrl("%s/sprite.svg");\nprint icons.path("u:star"), icons.path("sq");\n' $U $U > $D/e.rin
chk "icons.fromUrl pack + sprite" '["u:star", "u:dot"]
["sq"]
M12 2L15 9L22 9Z M4 4H20V20H4Z' "$($RIN $D/e.rin 2>&1)"
[ $fail == 0 ] && echo ALL PASS || { echo FAILED; exit 1; }

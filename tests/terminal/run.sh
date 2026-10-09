#!/usr/bin/env bash
# يشغّل اختبارات طرفية Rin:  RIN_BIN=./cli/linux/build/rin tests/terminal/run.sh
set -u
cd "$(dirname "$0")"
RIN_BIN="${RIN_BIN:-rin}"
export RIN_BIN
status=0

echo "== batch session =="
actual="$(RIN_LANG=en "$RIN_BIN" terminal --batch --no-color --no-history --no-banner < batch_session.in 2>&1)"
if [ "$actual" == "$(cat batch_session.expected)" ]; then echo "PASS batch_session"; else
  echo "FAIL batch_session"; diff <(echo "$actual") batch_session.expected | head -20; status=1; fi

echo "== projects + indsin (batch) =="
tmp="$(mktemp -d)"
pactual="$(cd "$tmp" && RIN_LANG=en "$RIN_BIN" terminal --batch --no-color --no-history --no-banner < "$OLDPWD/project_session.in" 2>&1 | sed -e "s#$tmp#<TMP>#g" -e "s/  [0-9]* ms/  N ms/")"
rm -rf "$tmp"
if [ "$pactual" == "$(cat project_session.expected)" ]; then echo "PASS project_session"; else
  echo "FAIL project_session"; diff <(echo "$pactual") project_session.expected | head -30; status=1; fi

echo "== interactive (pty) =="
python3 pty_test.py || status=1
exit $status

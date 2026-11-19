#!/bin/bash
# اختبار `rin app` (docs/container_app.md): new/check/bump/inject(--check)/export/urin.  الاستعمال: tests/rin_app_cli_test.sh [path/to/rin]
RIN="${1:-./build/rin}"; RIN="$(cd "$(dirname "$RIN")" && pwd)/$(basename "$RIN")"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT; cd "$T" || exit 1
fail() { echo "FAIL: $*"; exit 1; }
"$RIN" app new demo --site >/dev/null                       || fail new
[ -f demo.app.rin ]                                         || fail "new: file"
"$RIN" app new demo >/dev/null 2>&1                         && fail "new must refuse overwrite"
"$RIN" app check >/dev/null 2>&1                            && fail "check must fail: icons_dir missing"
mkdir icons; printf 'x' > icons/icon_192.png
"$RIN" app check >/dev/null                                 || fail "check ok"
"$RIN" app bump minor --notes "first" >/dev/null            || fail bump
grep -q 'let version = "0.2.0"' demo.app.rin                || fail "bump: version"
grep -q 'notes: \["first"\]' demo.app.rin                   || fail "bump: release entry"
"$RIN" app check >/dev/null                                 || fail "check after bump"
printf '<html><head>\n<meta charset="utf-8">\n<title>x</title>\n</head><body></body></html>\n' > index.html
"$RIN" app inject --into index.html >/dev/null              || fail inject
"$RIN" app inject --into index.html --check >/dev/null      || fail "in sync after inject"
grep -q 'rin:app-head' index.html                           || fail "markers"
"$RIN" app bump patch >/dev/null                            || fail bump2
"$RIN" app inject --into index.html --check >/dev/null 2>&1 && fail "drift must be detected"
"$RIN" app export --out out --paths / >/dev/null            || fail export
[ -f out/head.html ] && [ -f out/CHANGELOG.md ] && [ -f out/robots.txt ] || fail "export files"
"$RIN" app urin | grep -q 'type="html"'                     || fail urin
echo "rin_app_cli_test: OK"

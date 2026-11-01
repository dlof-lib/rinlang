#!/usr/bin/env bash
# اختبارات Disconom (المترجم، القفل، الميزات، السكربتات، التحقق) عبر أداة rin lib.
# التشغيل من جذر المستودع:   RIN=build/rin bash tests/verification/disconom/run_tests.sh
set -u
RIN="${RIN:-rin}"
D="tests/verification/disconom"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
ok()   { pass=$((pass+1)); echo "PASS  $1"; }
bad()  { fail=$((fail+1)); echo "FAIL  $1"; [ -n "${2:-}" ] && echo "      $2"; }
check() { # check <name> <command...>   (ينجح عند رمز 0)
  local n="$1"; shift
  if out=$("$@" 2>&1); then ok "$n"; else bad "$n" "$(echo "$out" | head -5 | tr '\n' '|')"; fi
}
expect_fail() { local n="$1"; shift; if out=$("$@" 2>&1); then bad "$n" "كان يجب أن يفشل"; else ok "$n"; fi; }
has() { # has <name> <needle> <text>
  if printf '%s' "$3" | grep -qF -- "$2"; then ok "$1"; else bad "$1" "لم أجد: $2"; fi
}

# ---------- 1) تحويل دائري بين json / xml / toml بلا فقدان ----------
$RIN lib translate $D/pro.og.rin --to json -q > "$TMP/a.json"
$RIN lib translate "$TMP/a.json" --to xml  -q > "$TMP/b.xml"
$RIN lib translate "$TMP/b.xml"  --to toml -q > "$TMP/c.toml"
$RIN lib translate "$TMP/c.toml" --to json -q > "$TMP/d.json"
if cmp -s "$TMP/a.json" "$TMP/d.json"; then ok "round-trip json→xml→toml→json متطابق"; else bad "round-trip json→xml→toml→json" "$(diff "$TMP/a.json" "$TMP/d.json" | head -5 | tr '\n' '|')"; fi
has "الحقول المخصّصة x-custom محفوظة" '"x-custom"' "$(cat "$TMP/d.json")"
has "features.default محفوظة"        '"default"'  "$(cat "$TMP/d.json")"
has "native.functions بلا params محفوظة" '"pro_ping"' "$(cat "$TMP/d.json")"

# XML الكامل → JSON → XML
$RIN lib translate $D/xmlfull.og.rin --to json -q > "$TMP/x1.json"
$RIN lib translate "$TMP/x1.json" --to xml -q > "$TMP/x2.xml"
$RIN lib translate "$TMP/x2.xml" --to json -q > "$TMP/x3.json"
if cmp -s "$TMP/x1.json" "$TMP/x3.json"; then ok "round-trip xml→json→xml→json متطابق"; else bad "round-trip xml"; fi
has "private محفوظة من XML" '"private": true' "$(cat "$TMP/x1.json")"

# ---------- 2) npm (package.json) ----------
$RIN lib translate $D/pro.og.rin --to npm -q > "$TMP/package.json"
has "npm: engines.rin"       '"rin": ">=1.0.0 <2.0.0"' "$(cat "$TMP/package.json")"
has "npm: امتداد disconom"   '"disconom": {'            "$(cat "$TMP/package.json")"
$RIN lib translate "$TMP/package.json" --to json -q > "$TMP/n.json"
if cmp -s "$TMP/a.json" "$TMP/n.json"; then ok "round-trip disconom→npm→disconom متطابق"; else bad "round-trip npm" "$(diff "$TMP/a.json" "$TMP/n.json" | head -8 | tr '\n' '|')"; fi

cat > "$TMP/foreign.json" <<'JSON'
{ "name": "@acme/Cool Lib", "version": "3.1.0", "description": "x", "license": "MIT",
  "author": "Jane <jane@x.org> (https://x.org)", "keywords": ["a","b"],
  "repository": { "type": "git", "url": "git+https://github.com/acme/cool.git" },
  "bugs": { "url": "https://github.com/acme/cool/issues" }, "main": "index.js",
  "scripts": { "test": "node t.js" },
  "dependencies": { "left": "^1.2.0", "wk": "workspace:*", "tagged": "latest", "gitdep": "github:a/b" },
  "devDependencies": { "dev": "~2.0.0" }, "engines": { "node": ">=18" }, "os": ["darwin", "win32"] }
JSON
out=$($RIN lib translate "$TMP/foreign.json" --to json 2>&1)
has "npm→disconom: الاسم المعقَّم"   '"name": "acme_Cool_Lib"' "$out"
has "npm→disconom: workspace:* → *"  '"wk": "*"' "$out"
has "npm→disconom: latest → *"       '"tagged": "*"' "$out"
has "npm→disconom: darwin→macos"     '"macos"' "$out"
has "npm→disconom: المؤلف بلا بريد"  '"Jane"' "$out"
has "npm→disconom: ملاحظات الفقد"    'ملاحظة' "$out"

# ---------- 3) RinPM (rin.toml) ----------
out=$($RIN lib translate $D/pro.og.rin --to rinpm 2>&1)
has "rinpm: [package]"        '[package]' "$out"
has "rinpm: ملاحظة عن scripts" "'scripts'" "$out"
printf '%s' "$out" | grep -v '^  ملاحظة' > "$TMP/rin.toml"
$RIN lib translate "$TMP/rin.toml" --to json -q > "$TMP/r.json" && has "rinpm→disconom يقرأ" '"name": "pro"' "$(cat "$TMP/r.json")" || bad "rinpm→disconom"

# ---------- 4) fmt ----------
cp "$TMP/a.json" "$TMP/disconom.json"
check "fmt --check على ملف قانوني" $RIN lib fmt "$TMP/disconom.json" --check
printf '{"name":"zz","version":"1.0.0","rin":">=1.0.0"}' > "$TMP/messy.json"
expect_fail "fmt --check يكشف الملف غير المنسّق" $RIN lib fmt "$TMP/messy.json" --check
check "fmt --write" $RIN lib fmt "$TMP/messy.json" --write
check "fmt --check بعد --write" $RIN lib fmt "$TMP/messy.json" --check

# ---------- 5) --write / --force ----------
mkdir -p "$TMP/w.og.rin"; cp $D/xmlfull.og.rin/disconom.xml "$TMP/w.og.rin/"
check "translate --write ينشئ disconom.toml" $RIN lib translate "$TMP/w.og.rin" --to toml --write -q
[ -f "$TMP/w.og.rin/disconom.toml" ] && ok "الملف الجديد موجود" || bad "الملف الجديد غير موجود"
expect_fail "translate --write لا يستبدل بلا --force" $RIN lib translate "$TMP/w.og.rin" --to toml --write -q
check "translate --write --force" $RIN lib translate "$TMP/w.og.rin" --to toml --write --force -q

# ---------- 6) التحقق ----------
check "check على pro (TOML) ينجح" $RIN lib check $D/pro.og.rin
out=$($RIN lib check $D/badfeat.og.rin 2>&1); has "badfeat: دورة ميزات" "feature cycle" "$out"
has "badfeat: ميزة تشير لشيء مجهول" "neither a feature nor an optional dependency" "$out"
has "badfeat: اسم script سيّئ" "invalid script name" "$out"
has "badfeat: مسار files غير آمن" "unsafe 'files' path" "$out"
expect_fail "check على badfeat يفشل" $RIN lib check $D/badfeat.og.rin
expect_fail "translate --strict على badfeat يفشل" $RIN lib translate $D/badfeat.og.rin --to json --strict -q
printf '{"name":"t","version":"1.0.0","private":"yes"}' > "$TMP/badtype.json"
expect_fail "نوع private خاطئ يُرفض" $RIN lib translate "$TMP/badtype.json" --to json -q

# ---------- 7) الميزات والتبعيات الاختيارية ----------
out=$($RIN lib check $D/optfeat.og.rin 2>&1);  if echo "$out" | grep -q "extra"; then bad "optfeat: extra لا يجب أن تُحمَّل افتراضياً"; else ok "optfeat: الاختيارية خلف ميزة غير مفعّلة لا تُحمَّل"; fi
out=$(RIN_FEATURES=gpu $RIN lib check $D/optfeat.og.rin 2>&1); has "RIN_FEATURES=gpu تفعّل التبعية" "extra → optfeat" "$out"
out=$($RIN lib check $D/optauto.og.rin 2>&1);  has "optauto: اختيارية موجودة تُحمَّل" "extra → optauto" "$out"
has "optauto: اختيارية مفقودة ليست خطأً" "OK" "$out"
out=$($RIN lib check $D/pro.og.rin 2>&1);     has "pro: الميزة الافتراضية text تُفعّل extra" "base → extra → pro" "$out"
has "pro: peer مفقودة تحذير لا خطأ" "peer dependency" "$out"

# ---------- 8) disconom.lock ----------
cp -r $D "$TMP/lk"; L="$TMP/lk/pro.og.rin"
check "lock يولّد الملف" $RIN lib lock "$L"
[ -f "$L/disconom.lock" ] && ok "disconom.lock موجود" || bad "disconom.lock غير موجود"
has "lock يحوي بصمة sha256" '"fingerprint": "sha256:' "$(cat "$L/disconom.lock")"
has "lock يحوي extra الاختيارية المفعّلة" '"name": "extra"' "$(cat "$L/disconom.lock")"
check "lock --check متطابق" $RIN lib lock "$L" --check
$RIN lib lock "$L" -o "$TMP/second.lock" >/dev/null; cmp -s "$L/disconom.lock" "$TMP/second.lock" && ok "lock حتمي (نفس الناتج مرتين)" || bad "lock غير حتمي"
echo 'fun base_id(x) { return x + 0; }' > "$TMP/lk/base.og.rin/main.zpp"
out=$($RIN lib lock "$L" --check 2>&1); has "تغيّر محتوى تبعية يُكتشف" "fingerprint mismatch" "$out"
expect_fail "lock --check يفشل عند الانحراف" $RIN lib lock "$L" --check
sed -i 's/"version": "1.4.0"/"version": "1.5.0"/' "$TMP/lk/base.og.rin/disconom.json"
out=$($RIN lib lock "$L" --check 2>&1); has "تغيّر إصدار تبعية يُكتشف" "version: base locked 1.4.0 but found 1.5.0" "$out"
check "lock يُحدَّث بعد التغيير" $RIN lib lock "$L"
check "lock --check بعد التحديث" $RIN lib lock "$L" --check
expect_fail "lock يرفض مكتبة فيها دورة" $RIN lib lock $D/../libsplit/cyca.og.rin

# ---------- 9) scripts ----------
out=$($RIN lib scripts $D/pro.og.rin 2>&1); has "scripts: سرد" "lint:all" "$out"
expect_fail "script لا يُنفَّذ دون --allow-scripts" $RIN lib script $D/pro.og.rin test
mkdir -p "$TMP/sc.og.rin"; cat > "$TMP/sc.og.rin/disconom.toml" <<'T'
disconom = 2
name = "sc"
version = "1.0.0"
rin = ">=1.0.0"
[scripts]
hi = "echo script-ran-in-$(basename $PWD)"
fail = "exit 3"
T
echo 'fun sc_ok() { return 1; }' > "$TMP/sc.og.rin/main.zpp"
out=$($RIN lib script "$TMP/sc.og.rin" hi --allow-scripts 2>&1); has "script يعمل داخل مجلد المكتبة" "script-ran-in-sc.og.rin" "$out"
$RIN lib script "$TMP/sc.og.rin" fail --allow-scripts >/dev/null 2>&1; [ $? -eq 3 ] && ok "رمز خروج script يُمرَّر" || bad "رمز خروج script"

# ---------- 10) new --toml ----------
check "new --toml" $RIN lib new tomlbox --dir "$TMP" --toml
[ -f "$TMP/tomlbox.og.rin/disconom.toml" ] && ok "disconom.toml أُنشئ" || bad "disconom.toml لم يُنشأ"
check "المكتبة المولَّدة بـ TOML تجتاز check" $RIN lib check "$TMP/tomlbox.og.rin"
check "new --xml"  $RIN lib new xmlbox --dir "$TMP" --xml
check "xml المولَّد يجتاز check" $RIN lib check "$TMP/xmlbox.og.rin"

echo; echo "pass=$pass fail=$fail"
[ "$fail" -eq 0 ]

#!/usr/bin/env bash
# اختبارات مترجم indsin2web. الاستعمال: bash tests/indsin2web/run.sh   (يبني المترجم أولاً)
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; BIN="$ROOT/build/indsin2web"; OUT="$(mktemp -d)"; fail=0
bash "$ROOT/scripts/build_indsin2web.sh" >/dev/null || { echo "FAIL build"; exit 1; }
ok() { echo "ok   $1"; }; bad() { echo "FAIL $1"; fail=1; }

# 1) كل ملفات indsin_* في العينات تُترجَم، عدا ما يحتاج مفسّر Rin كاملاً (<<each>> على قوائم غير حرفية)
for f in "$ROOT"/examples/samples/indsin_*.rin; do
  n=$(basename "$f" .rin)
  if "$BIN" "$f" -o "$OUT/$n.html" 2>"$OUT/$n.log"; then ok "sample $n"
  elif grep -q "<<each" "$OUT/$n.log"; then echo "skip $n (<<each>> يحتاج مفسّراً)"
  else bad "sample $n: $(head -1 "$OUT/$n.log")"; fi
done

# 2) كل خاصية في kProps لها معالج في جدول P بالـ runtime (لا يتناقض المترجم مع الصفحة المولَّدة)
props=$(python3 - "$ROOT/tools/indsin2web.cpp" <<'PY'
import re,sys
s=open(sys.argv[1],encoding='utf-8').read()
m=re.search(r'const std::set<std::string> kProps = \{(.*?)\};',s,re.S)
print("\n".join(re.findall(r'"([^"]+)"',m.group(1))))
PY
)
miss=""; for p in $props; do grep -qE "(^|[ ,{])\"?$p\"?:" "$ROOT/tools/indsin2web_runtime.inc" || miss="$miss $p"; done
[ -z "$miss" ] && ok "kProps ⊆ P" || bad "خصائص بلا معالج في runtime:$miss"

# 3) أخطاء المصدر: رمز خروج 1 ورقم سطر
printf '<Column#a>\n<Text>hi</Colunm>\n' > "$OUT/bad.rin"
"$BIN" "$OUT/bad.rin" -o "$OUT/bad.html" 2>"$OUT/bad.log"; rc=$?
[ $rc -eq 1 ] && grep -q "bad.rin:2" "$OUT/bad.log" && ok "syntax error → exit 1 + line" || bad "syntax error handling (rc=$rc)"

# 4) --strict يفشل عند وجود تحذيرات، و--check لا يكتب ملفاً
printf '<Column#a>\n<Text#t foo=1>hi</Text>\n</Column>\n' > "$OUT/w.rin"
"$BIN" "$OUT/w.rin" --check --strict 2>/dev/null; [ $? -eq 2 ] && ok "--strict exit 2" || bad "--strict"

# 5) الخروج آمن: نص يحوي </script> (بالكيان أو داخل تعبير) لا يكسر الصفحة
printf '<Column#a>\n<Text#t>x &lt;/script&gt; {"</script>"}</Text>\n</Column>\n' > "$OUT/x.rin"
"$BIN" "$OUT/x.rin" -o "$OUT/x.html" 2>/dev/null && [ "$(grep -o '</script>' "$OUT/x.html" | wc -l)" = "1" ] && ok "script injection escaped" || bad "script escaping"

# 5b) المشغّل الموحّد scripts/indsin
bash "$ROOT/scripts/indsin" tags "$ROOT/examples/samples/indsin_tags_demo.rin" | grep -q "^@view.Column=root" && ok "indsin tags" || bad "indsin tags"
bash "$ROOT/scripts/indsin" bogus 2>/dev/null; [ $? -eq 64 ] && ok "indsin unknown cmd → 64" || bad "indsin unknown cmd"

# 5c) المحوّل view2tags: ناتجه بلا @view ويمرّ على فاحص indsin، والنوع الخام يُكتب View.Kind
printf '@view.Column=root gap=8;\n  @view.TopBar=bar @view.Text=t text="x"; .end/view .end/view\n.end/view\n' > "$OUT/old.rin"
python3 "$ROOT/tools/view2tags.py" "$OUT/old.rin" -o "$OUT/new.rin" && ! grep -q "@view" "$OUT/new.rin" && grep -q "<View.TopBar#bar>" "$OUT/new.rin" \
  && "$BIN" "$OUT/new.rin" --check 2>/dev/null && ok "view2tags" || bad "view2tags"

# 5d) ميزات Pro: لاحقة بلا أساس تُحذَّر، --pwa يكتب manifest+sw ويربطهما، SEO meta، الإجراءات الجديدة
printf '<Column#a padding_md=8>\n<Text#t>x</Text>\n</Column>\n' > "$OUT/sfx.rin"
"$BIN" "$OUT/sfx.rin" --check --strict 2>"$OUT/sfx.log"; [ $? -eq 2 ] && grep -q "بلا قيمة أساسية" "$OUT/sfx.log" && ok "suffix without base warns" || bad "suffix warning"
mkdir -p "$OUT/pwa"; "$BIN" "$ROOT/examples/samples/indsin_pro_web.rin" -o "$OUT/pwa/app.html" --pwa --description "d" --theme-color "#112233" 2>/dev/null
[ -f "$OUT/pwa/manifest.webmanifest" ] && [ -f "$OUT/pwa/sw.js" ] && grep -q 'rel="manifest"' "$OUT/pwa/app.html" && grep -q 'name="description" content="d"' "$OUT/pwa/app.html" \
  && grep -q 'theme-color" content="#112233"' "$OUT/pwa/app.html" && node -e "JSON.parse(require('fs').readFileSync('$OUT/pwa/manifest.webmanifest','utf8'))" 2>/dev/null && ok "--pwa files + meta" || bad "--pwa"
"$BIN" "$ROOT/examples/samples/indsin_pro_web.rin" --pwa 2>/dev/null; [ $? -eq 64 ] && ok "--pwa without -o → 64" || bad "--pwa needs -o"

# 6) اختبار متصفح (اختياري: يحتاج node + playwright)
if command -v node >/dev/null && node -e "require('playwright')" 2>/dev/null; then
  "$BIN" "$ROOT/examples/samples/indsin_tags_demo.rin" -o "$OUT/demo.html" 2>/dev/null
  node "$ROOT/tests/indsin2web/browser.js" "$OUT/demo.html" && ok "browser: reactive state" || bad "browser test"
  "$BIN" "$ROOT/examples/samples/indsin_pro_web.rin" -o "$OUT/pro.html" --pwa 2>/dev/null
  node "$ROOT/tests/indsin2web/browser_pro.js" "$OUT/pro.html" && ok "browser: pro features" || bad "browser pro test"
  "$BIN" "$ROOT/examples/samples/indsin_pro2_showcase.rin" -o "$OUT/pro2.html" 2>/dev/null
  node "$ROOT/tests/indsin2web/browser_pro2.js" "$OUT/pro2.html" && ok "browser: pro2 features" || bad "browser pro2 test"
else echo "skip browser test (node/playwright غير متوفر)"; fi
echo; [ $fail -eq 0 ] && echo "ALL PASSED" || { echo "SOME FAILED"; exit 1; }

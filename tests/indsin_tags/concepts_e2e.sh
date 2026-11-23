#!/usr/bin/env bash
# اختبار شامل لمفاهيم القوالب: يبني indsin2web ويترجم concepts_demo.rin ثم يقيّم شروط الظهور في Node.
#   bash tests/indsin_tags/concepts_e2e.sh      (يحتاج g++ و node)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
command -v node >/dev/null || { echo "SKIP: node غير متوفر"; exit 0; }
[ -x "$ROOT/build/indsin2web" ] || bash "$ROOT/scripts/build_indsin2web.sh" >/dev/null
OUT="$(mktemp -d)"; "$ROOT/build/indsin2web" "$ROOT/tests/indsin_tags/concepts_demo.rin" -o "$OUT/app.html"
node - "$OUT/app.html" <<'JS'
const h = require('fs').readFileSync(process.argv[2], 'utf8'); let bad = 0;
const ok = (c, m) => { if (!c) { bad++; console.error('FAIL', m); } else console.log('OK  ', m); };
const fns = {}; for (const m of h.matchAll(/\["(i-[a-z0-9_]+)", "visible", (\(\)=>.+?)\],\n/g)) fns[m[1]] = eval(m[2].replace(/^\(\)=>/, '()=>'));
let S; const shown = () => Object.keys(fns).filter(k => fns[k]());
for (const tab of [0, 1, 2, 3]) for (const ready of [false, true]) {
  S = { tab, ready }; const v = shown(); const arms = v.filter(k => /^i-m/.test(k)), ur = v.filter(k => /^i-(loading|done)$/.test(k));
  ok(arms.length === 1, `tab=${tab}: exactly one Match arm (${arms})`);
  ok(ur.length === 1 && ur[0] === (ready ? 'i-done' : 'i-loading'), `ready=${ready}: Unless/Else exclusive (${ur})`);
}
ok(/\["i-p_1", "width", 300\]/.test(h), '...attrs forwarded width=300 to first Panel');
ok(!/\["i-p_2", "width"/.test(h), 'second Panel has no forwarded width');
ok(h.includes('no footer') && h.includes('Save'), 'slot fallback + Fill both rendered');
process.exit(bad ? 1 : 0);
JS

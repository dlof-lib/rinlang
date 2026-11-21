// يختبر ميزتي rinhtml.js الجديدتين في Chromium حقيقي بمحرك WASM وهمي (لا يحتاج emscripten):
//   persist="..." (allow="storage")  و  rin-fetch / rin-each (allow="fetch") مع تحديث جزئي للـ DOM.
const { chromium } = require('playwright');
const http = require('http'), fs = require('fs'), path = require('path');
const lib = fs.readFileSync(path.join(__dirname, '../../web/rinhtml/rinhtml.js'), 'utf8');
const fail = m => { console.error('FAIL ' + m); process.exit(1); };
const eq = (a, e, m) => { if (JSON.stringify(a) !== JSON.stringify(e)) fail(`${m}: expected ${JSON.stringify(e)}, got ${JSON.stringify(a)}`); };
const ok = m => console.log('  ok ' + m);

// محرك وهمي بنفس واجهة ccall التي يصدّرها rinhtml_bridge.cpp
const STUB = `
window.RinHTMLEngine = () => Promise.resolve({ _s: {}, _n: 0,
  ccall(fn, ret, types, a) {
    if (fn === 'rinhtml_create') { const g = {}; a[0].replace(/let\\s+(\\w+)\\s*=\\s*([^;]+);/g, (m, k, v) => { g[k] = JSON.parse(v); }); this._s[++this._n] = g; return this._n; }
    const g = this._s[a[0]];
    if (fn === 'rinhtml_get_globals') return JSON.stringify(g);
    if (fn === 'rinhtml_set_global') { g[a[1]] = JSON.parse(a[2]); return; }
    if (fn === 'rinhtml_call') { const F = { add: () => g.count++, toggle: () => { g.dark = !g.dark; } }; F[a[1]](); return JSON.stringify({ ok: true, globals: g }); }
    if (fn === 'rinhtml_boot_output') return '';
    if (fn === 'rinhtml_free_session') { delete this._s[a[0]]; return; }
    if (fn === 'rinhtml_last_error') return '';
  } });`;

(async () => {
  let rateHits = 0, qs = [], down = false;
  const page = `<!doctype html><meta charset=utf-8><body>
<script>${STUB}</script><script src="/rinhtml.js"></script>
<rin-app id="main" src="main.rin" allow="dom storage fetch" persist="count dark"></rin-app>
<h1 id="title" rin-text="title">t</h1>
<b id="count" rin-text="count"></b><button id="add" rin-click="add()">+</button><button id="tog" rin-click="toggle()">t</button><i id="dark" rin-text="dark"></i>
<span id="eur" rin-fetch="/api/rates" rin-pick="rates.EUR" rin-into="eur" rin-every="1s" rin-error="err" rin-text="eur"></span><u id="err" rin-text="err"></u>
<input id="q" rin-fetch="/api/search?q={q}" rin-fetch-on="input" rin-debounce="150" rin-into="results" rin-model="q">
<ul id="list" rin-each="results"><template><li rin-field="name"></li></template></ul>`;
  const srv = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x'); res.setHeader('Access-Control-Allow-Origin', '*');
    if (u.pathname === '/') { res.setHeader('Content-Type', 'text/html; charset=utf-8'); return res.end(page); }
    if (u.pathname === '/rinhtml.js') { res.setHeader('Content-Type', 'text/javascript; charset=utf-8'); return res.end(lib); }
    if (u.pathname === '/main.rin') return res.end('let count = 0;\nlet dark = false;\nlet title = "RinHTML";\nlet eur = 0;\nlet err = "";\nlet q = "";\n');
    if (u.pathname === '/api/rates') { rateHits++; if (down) { res.statusCode = 500; return res.end('x'); } res.setHeader('Content-Type', 'application/json'); return res.end(JSON.stringify({ rates: { EUR: 0.9 + rateHits / 100 } })); }
    if (u.pathname === '/api/search') { const q = u.searchParams.get('q'); qs.push(q); res.setHeader('Content-Type', 'application/json'); return res.end(JSON.stringify([{ name: q + '1' }, { name: q + '2' }])); }
    res.statusCode = 404; res.end();
  });
  await new Promise(r => srv.listen(0, '127.0.0.1', r)); const base = 'http://127.0.0.1:' + srv.address().port + '/';
  const b = await chromium.launch(), ctx = await b.newContext(), errs = [];
  const open = async () => { const p = await ctx.newPage(); p.on('pageerror', e => errs.push(String(e))); await p.goto(base); await p.waitForTimeout(250); return p; };
  const txt = (p, id) => p.textContent('#' + id);

  let p = await open();
  eq(await txt(p, 'count'), '0', 'initial');
  await p.click('#add'); await p.click('#add'); await p.click('#tog'); await p.waitForTimeout(50);
  eq([await txt(p, 'count'), await txt(p, 'dark')], ['2', 'true'], 'state after clicks');
  await p.close(); p = await open();
  eq([await txt(p, 'count'), await txt(p, 'dark')], ['2', 'true'], 'persist restores globals after reopening'); ok('persist="count dark" survives reopening');
  eq(await txt(p, 'title'), 'RinHTML', 'non-persisted global untouched');
  eq(Object.keys(JSON.parse(await p.evaluate(() => localStorage.getItem('rinhtml:main'))).d), ['count', 'dark'], 'only named globals stored'); ok('only the named globals are stored');

  // rin-fetch + rin-every: scalar picked into the Rin global; only #eur (and #err) change, not #title/#count
  if (!/^0\.9/.test(await txt(p, 'eur'))) fail('eur not fetched: ' + await txt(p, 'eur'));
  await p.evaluate(() => { window.__m = { title: 0, count: 0, eur: 0 }; const w = (id, k) => new MutationObserver(m => { window.__m[k] += m.length; }).observe(document.getElementById(id), { childList: true, characterData: true, subtree: true }); w('title', 'title'); w('count', 'count'); w('eur', 'eur'); });
  const e1 = await txt(p, 'eur'); await p.waitForTimeout(2300);
  const m = await p.evaluate(() => window.__m); if (!(m.eur >= 1) || m.title !== 0 || m.count !== 0) fail('partial update broken: ' + JSON.stringify(m));
  if (e1 === await txt(p, 'eur')) fail('eur did not change'); ok('rin-every polls and touches only the bound node ' + JSON.stringify(m));
  down = true; await p.waitForTimeout(1500);
  if (!/HTTP 500/.test(await txt(p, 'err')) || !/^0\.9/.test(await txt(p, 'eur'))) fail('error handling: ' + await txt(p, 'err') + ' / ' + await txt(p, 'eur'));
  down = false; await p.waitForTimeout(1500); eq(await txt(p, 'err'), '', 'recovers'); ok('rin-error shows failure, stale value stays, then recovers');

  // rin-fetch-on + rin-debounce + rin-each
  qs = []; await p.click('#q'); await p.keyboard.type('abc', { delay: 20 }); await p.waitForTimeout(500);
  eq(qs, ['abc'], 'debounced to one request using {q} template'); eq(await p.$$eval('#list li', l => l.map(x => x.textContent)), ['abc1', 'abc2'], 'rin-each renders array'); ok('rin-fetch-on + rin-debounce + rin-each');
  const same = await p.$eval('#list li', l => { l.dataset.keep = '1'; return true; }); await p.fill('#q', 'abc'); await p.waitForTimeout(400);
  eq(await p.$eval('#list li', l => l.dataset.keep), '1', 'identical result does not rebuild the list'); ok('unchanged list is not re-rendered');

  // بلا allow="storage"/"fetch": لا حفظ ولا جلب (آمن افتراضياً)
  await p.close(); const p2 = await ctx.newPage(); await p2.route('**/locked', r => r.fulfill({ contentType: 'text/html', body: page.replace('allow="dom storage fetch"', 'allow="dom"') }));
  down = false; const hits0 = rateHits; await p2.goto(base.replace(/\/$/, '') + '/locked'); await p2.waitForTimeout(400);
  eq(rateHits, hits0, 'no request without allow="fetch"'); eq(await p2.textContent('#count'), '0', 'no restore without allow="storage"'); ok('capabilities are opt-in (no allow ⇒ no fetch/persist)');
  eq(errs.length, 0, 'page errors: ' + errs.join(' | ')); await b.close(); srv.close(); console.log('PASS rinhtml storage+fetch');
})().catch(e => { console.error(e); process.exit(1); });

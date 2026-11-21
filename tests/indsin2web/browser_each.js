// <<each>> على بيانات حيّة (container.api) في Chromium حقيقي: node browser_each.js <indsin2web> <indsin_live_list.rin>
const { chromium } = require('playwright');
const http = require('http'), fs = require('fs'), os = require('os'), path = require('path'), cp = require('child_process');
const [bin, rinFile] = process.argv.slice(2);
const fail = m => { console.error('FAIL ' + m); process.exit(1); };
const eq = (a, e, m) => { if (JSON.stringify(a) !== JSON.stringify(e)) fail(`${m}: expected ${JSON.stringify(e)}, got ${JSON.stringify(a)}`); };
const ok = m => console.log('  ok ' + m);
(async () => {
  let items = [{ id: 1, title: 'A' }, { id: 2, title: 'B' }], down = false, html = '';
  const srv = http.createServer((req, res) => {
    res.setHeader('Access-Control-Allow-Origin', '*');
    if (req.url === '/') { res.setHeader('Content-Type', 'text/html; charset=utf-8'); return res.end(html); }
    if (req.url.startsWith('/notifications')) { if (down) { res.statusCode = 500; return res.end('x'); } res.setHeader('Content-Type', 'application/json'); return res.end(JSON.stringify(items)); }
    res.statusCode = 404; res.end();
  });
  await new Promise(r => srv.listen(0, '127.0.0.1', r)); const base = 'http://127.0.0.1:' + srv.address().port;
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'ea-')), f = path.join(tmp, 'd.rin');
  fs.writeFileSync(f, fs.readFileSync(rinFile, 'utf8').replace('https://api.example.com', base).replace('let every = 15s;', 'let every = 1s;'));
  cp.execFileSync(bin, [f, '-o', path.join(tmp, 'd.html')], { stdio: ['ignore', 'ignore', 'pipe'] }); html = fs.readFileSync(path.join(tmp, 'd.html'), 'utf8');
  const b = await chromium.launch(), ctx = await b.newContext(), errs = [];
  const open = async () => { const p = await ctx.newPage(); p.on('pageerror', e => errs.push(String(e))); p.on('console', m => { if (m.type() === 'error' && !/500/.test(m.text())) errs.push(m.text()); }); await p.goto(base + '/'); await p.waitForTimeout(400); return p; };
  const msgs = p => p.$$eval('[id^="i-msg_w~"]', l => l.map(x => x.textContent));
  let p = await open();
  eq(await msgs(p), ['● A', '● B'], 'initial list rendered from fetched data'); eq(await p.textContent('#i-Head'), 'الإشعارات (2)', 'header'); ok('list rendered from live data');
  await p.$eval('#i-note_w\\~0', e => { e.dataset.keep = '1'; });
  await p.click('#i-ok_w\\~0'); await p.waitForTimeout(80);
  eq(await msgs(p), ['A', '● B'], 'per-item handler closes over the item'); ok('per-item handler + binding (only item 0 changed)');
  await p.waitForTimeout(2300);
  eq(await p.$eval('#i-note_w\\~0', e => e.dataset.keep), '1', 'unchanged poll must not rebuild the list'); ok('identical poll keeps DOM nodes');
  items = [...items, { id: 3, title: 'C' }]; await p.waitForTimeout(1600);
  eq(await p.$eval('#i-note_w\\~0', e => e.dataset.keep), '1', 'appending an item must not rebuild earlier ones');
  eq(await msgs(p), ['A', '● B', '● C'], 'new item appears'); eq(await p.textContent('#i-Head'), 'الإشعارات (3)', 'header updated'); ok('new data on the server shows up without reload');
  items = items.slice(0, 2); await p.waitForTimeout(1500); eq((await msgs(p)).length, 2, 'removed item disappears'); ok('removal handled, earlier nodes kept');
  down = true; await p.waitForTimeout(1500);
  if (!/HTTP 500/.test(await p.textContent('#i-Err'))) fail('error not shown'); eq((await msgs(p)).length, 2, 'list kept on error'); ok('error shown, list kept'); down = false;
  await p.close(); p = await open(); eq(await msgs(p), ['A', '● B'], 'seen state + cached list restored'); ok('seen (container.storage) survives reopening');
  // ربط ثنائي الاتجاه لحقل حاوية داخل <<each>> (القيمة مشتركة، فيتحدّث أخ العنصر وبقية الصفحة)
  fs.writeFileSync(f, '@container.storage=f\n  let q = "";\n  let rows = ["a", "b"];\n.end/container.storage\n<Column#R>\n<Text#Echo text={"q=" + f.q} />\n<<each f.rows as r, i>>\n<Input#inp value={f.q} />\n<</each>>\n</Column>\n');
  cp.execFileSync(bin, [f, '-o', path.join(tmp, 'b.html'), '--strict'], { stdio: ['ignore', 'ignore', 'pipe'] }); html = fs.readFileSync(path.join(tmp, 'b.html'), 'utf8');
  const p3 = await open(); await p3.fill('#i-inp_w\\~0', 'xyz'); await p3.waitForTimeout(100);
  eq([await p3.textContent('#i-Echo'), await p3.inputValue('#i-inp_w\\~1')], ['q=xyz', 'xyz'], 'two-way binding inside each'); ok('value={container.field} works inside <<each>>');
  eq(errs.length, 0, 'page errors: ' + errs.join(' | ')); await b.close(); srv.close(); console.log('PASS each');
})().catch(e => { console.error(e); process.exit(1); });

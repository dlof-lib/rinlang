// بث SSE + تحميل تدريجي (append/prepend/dedupe/maxItems) في Chromium حقيقي: node browser_stream.js <indsin2web> <indsin_feed_stream.rin>
const { chromium } = require('playwright');
const http = require('http'), fs = require('fs'), os = require('os'), path = require('path'), cp = require('child_process');
const [bin, rinFile] = process.argv.slice(2);
const fail = m => { console.error('FAIL ' + m); process.exit(1); };
const eq = (a, e, m) => { if (JSON.stringify(a) !== JSON.stringify(e)) fail(`${m}: expected ${JSON.stringify(e)}, got ${JSON.stringify(a)}`); };
const ok = m => console.log('  ok ' + m);
(async () => {
  let html = '', clients = [], pages = [];
  const post = n => ({ id: n, title: 'post-' + n });
  const srv = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x'); res.setHeader('Access-Control-Allow-Origin', '*');
    if (u.pathname === '/') { res.setHeader('Content-Type', 'text/html; charset=utf-8'); return res.end(html); }
    if (u.pathname === '/posts') { const pg = Number(u.searchParams.get('page') || 1); pages.push(pg); res.setHeader('Content-Type', 'application/json'); return res.end(JSON.stringify([post(pg * 10 + 1), post(pg * 10 + 2), post(pg * 10 + 3)])); }
    if (u.pathname === '/posts/stream') { res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', 'Access-Control-Allow-Origin': '*' }); res.write(':ok\n\n'); clients.push(res); return; }
    res.statusCode = 404; res.end();
  });
  await new Promise(r => srv.listen(0, '127.0.0.1', r)); const base = 'http://127.0.0.1:' + srv.address().port;
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'st-')), f = path.join(tmp, 'd.rin');
  const build = max => { fs.writeFileSync(f, fs.readFileSync(rinFile, 'utf8').replace('https://api.example.com', base).replace('let maxItems = 200;', 'let maxItems = ' + max + ';')); cp.execFileSync(bin, [f, '-o', path.join(tmp, 'd.html'), '--strict'], { stdio: ['ignore', 'ignore', 'pipe'] }); html = fs.readFileSync(path.join(tmp, 'd.html'), 'utf8'); };
  build(200);
  const send = o => clients.forEach(c => c.write('data: ' + JSON.stringify(o) + '\n\n'));
  const b = await chromium.launch(), ctx = await b.newContext(), errs = [];
  const open = async () => { const p = await ctx.newPage(); p.on('pageerror', e => errs.push(String(e))); await p.goto(base + '/'); await p.waitForTimeout(500); return p; };
  const titles = p => p.$$eval('[id^="i-Title_w~"]', l => l.map(x => x.textContent));
  let p = await open();
  eq(await titles(p), ['post-11', 'post-12', 'post-13'], 'first page'); eq(await p.textContent('#i-Conn'), '● متصل', 'stream connected'); ok('first page + stream connected');
  await p.click('#i-More'); await p.waitForTimeout(300);
  eq(await titles(p), ['post-11', 'post-12', 'post-13', 'post-21', 'post-22', 'post-23'], 'page 2 appended'); eq(pages, [1, 2], 'pages requested'); ok('feed.more() appends the next page');
  await p.$eval('#i-Title_w\\~0', e => { e.dataset.keep = '1'; });
  send(post(99)); await p.waitForTimeout(250);
  eq((await titles(p))[0], 'post-99', 'streamed post prepended'); eq((await titles(p)).length, 7, 'count after stream'); ok('SSE message prepends instantly (no polling)');
  send(post(99)); send([post(99), post(98)]); await p.waitForTimeout(250);
  eq((await titles(p)).slice(0, 2), ['post-98', 'post-99'], 'dedupe keeps one copy'); eq((await titles(p)).length, 8, 'dedupe count'); ok('dedupe="id" drops repeats');
  clients.forEach(c => c.end()); await p.waitForTimeout(400);
  if (!/stream disconnected/.test(await p.textContent('#i-Conn'))) fail('disconnect not shown: ' + await p.textContent('#i-Conn')); ok('disconnect shown'); 
  await p.waitForTimeout(3500); clients = clients.filter(c => !c.writableEnded); send(post(100)); await p.waitForTimeout(300);
  eq((await titles(p))[0], 'post-100', 'auto-reconnect receives new posts'); eq(await p.textContent('#i-Conn'), '● متصل', 'error cleared after message'); ok('EventSource reconnects and error clears');
  await p.close(); clients = []; p = await open();
  eq((await titles(p)).length >= 3, true, 'persisted list shown'); ok('persist shows the last list at once'); await p.close();
  build(4); await ctx.clearCookies(); const p2 = await ctx.newPage(); await p2.evaluate(() => 0).catch(() => {}); await p2.goto(base + '/'); await p2.evaluate(() => localStorage.clear()); await p2.reload(); await p2.waitForTimeout(500);
  await p2.click('#i-More'); await p2.waitForTimeout(300); eq((await titles(p2)).length, 4, 'maxItems caps the list'); ok('maxItems trims the merged list');
  eq(errs.length, 0, 'page errors: ' + errs.join(' | ')); await b.close(); srv.close(); console.log('PASS stream');
})().catch(e => { console.error(e); process.exit(1); });

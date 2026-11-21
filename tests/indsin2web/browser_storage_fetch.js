// يختبر container.storage (بقاء البيانات بعد إغلاق الصفحة) و container.api (جلب خلفي + تحديث جزئي) في Chromium حقيقي.
// الاستعمال: node browser_storage_fetch.js <demo.rin المترجَم إلى html عبر indsin2web بعد استبدال العناوين> — يتولّى الاختبار بنفسه
// ترجمة المثال بخادم محلي وهمي: node browser_storage_fetch.js <indsin2web-bin> <examples/samples/indsin_storage_fetch.rin>
const { chromium } = require('playwright');
const http = require('http'), fs = require('fs'), os = require('os'), path = require('path'), cp = require('child_process');
const [bin, rinFile] = process.argv.slice(2);
const fail = m => { console.error('FAIL ' + m); process.exit(1); };
const eq = (a, e, m) => { if (JSON.stringify(a) !== JSON.stringify(e)) fail(`${m}: expected ${JSON.stringify(e)}, got ${JSON.stringify(a)}`); };
const ok = m => console.log('  ok ' + m);

(async () => {
  // ── خادم وهمي: /v6/latest/USD (EUR يزيد مع كل طلب) و /search?q= (ردّ أبطأ للاستعلام الأقصر) ──
  let rateHits = 0, searchQs = [], failRates = false, html = '';
  const srv = http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    res.setHeader('Access-Control-Allow-Origin', '*');
    if (u.pathname === '/') { res.setHeader('Content-Type', 'text/html; charset=utf-8'); return res.end(html); }
    if (u.pathname === '/v6/latest/USD') {
      rateHits++; if (failRates) { res.statusCode = 503; return res.end('down'); }
      res.setHeader('Content-Type', 'application/json'); return res.end(JSON.stringify({ result: 'success', rates: { EUR: 0.9 + rateHits / 100, GBP: 0.8 } }));
    }
    if (u.pathname === '/search') {
      const q = u.searchParams.get('q'); searchQs.push(q);
      setTimeout(() => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify([q + '-1', q + '-2'])); }, q.length === 2 ? 400 : 10); return;
    }
    res.statusCode = 404; res.end('nf');
  });
  await new Promise(r => srv.listen(0, '127.0.0.1', r));
  const base = 'http://127.0.0.1:' + srv.address().port;
  // نترجم نسخة اختبار: عناوين الخادم الوهمي + تحديث كل ثانية + اسم تخزين مميّز
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'sf-'));
  const build = (version) => {
    let src = fs.readFileSync(rinFile, 'utf8').replace('https://open.er-api.com/v6', base + '/v6').replace('https://api.example.com', base)
      .replace('let every = 30s;', 'let every = 1s;').replace('let version = 1;', 'let version = ' + version + ';');
    const f = path.join(tmp, 'd.rin'); fs.writeFileSync(f, src);
    cp.execFileSync(bin, [f, '-o', path.join(tmp, 'd.html')], { stdio: ['ignore', 'ignore', 'pipe'] });
    html = fs.readFileSync(path.join(tmp, 'd.html'), 'utf8');
  };
  build(1);

  const b = await chromium.launch(), ctx = await b.newContext(), errs = [];
  const open = async () => { const p = await ctx.newPage(); p.on('pageerror', e => errs.push(String(e))); await p.goto(base + '/'); await p.waitForTimeout(150); return p; };
  const txt = (p, id) => p.textContent('#i-' + id);

  // 1) أول زيارة: visits=1 ثم بعد "إغلاق" الصفحة وفتحها تصير 2
  let p = await open();
  eq(await txt(p, 'Title'), 'مرحباً ضيف — زيارتك رقم 1', 'first visit');
  await p.close(); p = await open();
  eq(await txt(p, 'Title'), 'مرحباً ضيف — زيارتك رقم 2', 'visits persisted across page close'); ok('visits persist after reopening');

  // 2) الوضع الليلي + الاسم (ربط ثنائي الاتجاه) + السلة تبقى بعد إعادة الفتح
  await p.click('#i-Dark'); await p.fill('#i-Name', 'سارة'); await p.click('#i-Add'); await p.click('#i-Add'); await p.waitForTimeout(80);
  eq(await p.$eval('#i-Root', e => e.style.background), 'rgb(15, 23, 42)', 'dark bg applied');
  eq(await txt(p, 'CartN'), 'في السلة: 2', 'cart push is reactive (deep proxy)');
  await p.close(); p = await open();
  eq(await p.$eval('#i-Root', e => e.style.background), 'rgb(15, 23, 42)', 'dark mode restored'); eq(await p.$eval('#i-Dark', e => e.checked), true, 'switch restored');
  eq(await p.inputValue('#i-Name'), 'سارة', 'input restored'); eq(await txt(p, 'CartN'), 'في السلة: 2', 'cart restored');
  eq(await txt(p, 'Title'), 'مرحباً سارة — زيارتك رقم 3', 'title uses restored name'); ok('dark mode / name / cart restored');
  const saved = JSON.parse(await p.evaluate(() => localStorage.getItem('rin:prefs'))); eq([saved.v, saved.d.cart.length, saved.d.dark], [1, 2, true], 'stored record shape');

  // 3) إفراغ السلة بـ prefs.reset("cart")
  await p.click('#i-Clear'); await p.waitForTimeout(60); eq(await txt(p, 'CartN'), 'في السلة: 0', 'reset(field)');
  eq(JSON.parse(await p.evaluate(() => localStorage.getItem('rin:prefs'))).d.cart, [], 'reset persisted'); ok('prefs.reset("cart")');

  // 4) الجلب الخلفي: يظهر السعر، ثم يتحدّث دورياً — وعقدة العنوان لا تُلمَس (تحديث جزئي)
  await p.waitForTimeout(300);
  if (!/^EUR: 0\.9\d/.test(await txt(p, 'Eur'))) fail('rates not shown: ' + await txt(p, 'Eur'));
  const first = await txt(p, 'Eur');
  await p.evaluate(() => { window.__m = { title: 0, eur: 0, gbp: 0, cart: 0 };
    const w = (id, k) => new MutationObserver(m => { window.__m[k] += m.length; }).observe(document.getElementById(id), { childList: true, characterData: true, subtree: true, attributes: true });
    w('i-Title', 'title'); w('i-Eur', 'eur'); w('i-Gbp', 'gbp'); w('i-CartN', 'cart'); });
  await p.waitForTimeout(2300);
  const m = await p.evaluate(() => window.__m);
  if (!(m.eur >= 1)) fail('EUR never re-rendered during polling: ' + JSON.stringify(m));
  eq([m.title, m.gbp, m.cart], [0, 0, 0], 'only the bound node changed (partial update)');
  if (first === await txt(p, 'Eur')) fail('EUR did not change after polling'); ok('background polling updates only bound nodes (' + JSON.stringify(m) + ')');

  const pr = JSON.parse(await p.evaluate(() => localStorage.getItem('rin:api:rates')) || 'null');
  if (!pr || !pr.data || typeof pr.data.EUR !== 'number') fail('persist:true did not store last data'); ok('persist:true keeps last api data for instant display');

  // 5) فشل الخادم: يبقى آخر سعر ويظهر الخطأ، ثم يتعافى
  failRates = true; await p.waitForTimeout(1500);
  if (!/^خطأ: HTTP 503/.test(await txt(p, 'Stamp'))) fail('error not shown: ' + await txt(p, 'Stamp'));
  if (!/^EUR: 0\.9\d/.test(await txt(p, 'Eur'))) fail('stale data should remain on error');
  failRates = false; await p.waitForTimeout(1500);
  if (!/^آخر تحديث/.test(await txt(p, 'Stamp'))) fail('did not recover: ' + await txt(p, 'Stamp')); ok('error keeps stale data, then recovers');

  // 6) البحث الفوري: debounce يدمج الطرق السريعة في طلب واحد، والردّ الأحدث يربح
  searchQs = [];
  await p.click('#i-Q'); await p.keyboard.type('a', { delay: 20 }); await p.keyboard.type('b', { delay: 20 }); await p.keyboard.type('c', { delay: 20 });
  await p.waitForTimeout(700);
  eq(searchQs, ['abc'], 'debounce collapsed keystrokes into one request');
  eq(await txt(p, 'Res'), '2 نتيجة: abc-1، abc-2', 'search result shown'); ok('debounced instant search');
  // سباق الردود: "ab" بطيء (400ms) ثم "abc" سريع بعد مهلة debounce ⇒ الأحدث يبقى
  searchQs = []; await p.fill('#i-Q', 'xy'); await p.waitForTimeout(330); await p.fill('#i-Q', 'xyz'); await p.waitForTimeout(900);
  eq(searchQs, ['xy', 'xyz'], 'both requests issued'); eq(await txt(p, 'Res'), '2 نتيجة: xyz-1، xyz-2', 'latest response wins (slow older one aborted)'); ok('latest-wins cancels the stale request');

  // 7) مزامنة التبويبات: تعديل في تبويب يظهر في الآخر
  const p2 = await open(); await p.fill('#i-Name', 'ليلى'); await p.waitForTimeout(250);
  eq(await p2.evaluate(() => document.getElementById('i-Title').textContent.startsWith('مرحباً ليلى')), true, 'cross-tab sync'); ok('cross-tab storage sync'); await p2.close();

  // 8) رفع version يُسقط البيانات القديمة (هجرة آمنة)
  await p.close(); build(2); p = await open();
  eq(await txt(p, 'Title'), 'مرحباً ضيف — زيارتك رقم 1', 'version bump discards old data'); eq(await p.evaluate(() => JSON.parse(localStorage.getItem('rin:prefs')).v), 2, 'new version stored'); ok('version mismatch resets data');
  await p.close();

  // 9) تخزين معطَّل (وضع خاص/حصّة ممتلئة): الصفحة تعمل بالذاكرة ولا تنهار
  const ctx2 = await b.newContext(); await ctx2.addInitScript(() => { Storage.prototype.setItem = function () { throw new DOMException('quota', 'QuotaExceededError'); }; });
  const q = await ctx2.newPage(); q.on('pageerror', e => errs.push('blocked-storage: ' + e)); await q.goto(base + '/'); await q.waitForTimeout(150);
  await q.click('#i-Add'); await q.waitForTimeout(60); eq(await txt(q, 'CartN'), 'في السلة: 1', 'works in-memory when storage throws'); ok('storage failure degrades gracefully');
  eq(errs.length, 0, 'page errors: ' + errs.join(' | '));
  await b.close(); srv.close(); console.log('PASS storage+fetch');
})().catch(e => { console.error(e); process.exit(1); });

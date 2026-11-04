// يتحقق أن الصفحة المولَّدة تتفاعل فعلاً: النقر يغيّر warp فتُعاد التعابير وتُطبَّق على الـ DOM.
const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch(), p = await b.newPage(), errs = [];
  p.on('pageerror', e => errs.push(String(e)));
  await p.goto('file://' + process.argv[2]);
  const eq = (a, e, m) => { if (a !== e) { console.error(`${m}: expected ${e}, got ${a}`); process.exit(1); } };
  eq(await p.textContent('#i-state'), 'pressed=false zoom=96 x=20 y=20', 'initial text');
  await p.click('#i-zoomIn'); await p.click('#i-zoomIn'); await p.click('#i-right');
  await p.waitForTimeout(50);
  eq(await p.textContent('#i-state'), 'pressed=false zoom=120 x=40 y=20', 'after clicks');
  eq(await p.$eval('#i-card', e => e.style.width), '120px', 'width binding');
  await p.click('#i-colorBtn'); await p.waitForTimeout(50);
  eq(await p.$eval('#i-card', e => e.style.background), 'rgb(27, 31, 42)', 'conditional background');
  await p.click('#i-visBtn'); await p.waitForTimeout(50);
  eq(await p.$eval('#i-card', e => e.hidden), true, 'visible=false hides');
  eq(errs.length, 0, 'page errors: ' + errs.join('|'));
  await b.close();
})();

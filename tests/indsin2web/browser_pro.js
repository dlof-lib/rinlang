// اختبار متصفح لميزات الويب الاحترافية: لواحق الاستجابة والداكن، حالات hover، الحركة، المكوّنات المركّبة، toast، الثيم.
const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch(), errs = [];
  const eq = (a, e, m) => { if (a !== e) { console.error(`${m}: expected ${e}, got ${a}`); process.exit(1); } };
  const ctx = await b.newContext({ viewport: { width: 400, height: 800 } });
  const p = await ctx.newPage();
  p.on('pageerror', e => errs.push(String(e)));
  await p.goto('file://' + process.argv[2]);
  const css = (sel, prop) => p.$eval(sel, (e, pr) => getComputedStyle(e)[pr], prop);
  // 1) لواحق الاستجابة: <600 → base، ≥600 → _md، ثم العودة
  eq(await p.$eval('#i-root', e => e.style.paddingLeft || e.style.padding), '16px', 'base padding @400');
  await p.setViewportSize({ width: 700, height: 800 }); await p.waitForTimeout(80);
  eq(await p.$eval('#i-root', e => e.style.padding), '32px', '_md padding @700');
  eq(await css('#i-cards', 'gridTemplateColumns').then(v => v.split(' ').length), 2, 'columns_md=2');
  await p.setViewportSize({ width: 900, height: 800 }); await p.waitForTimeout(80);
  eq(await css('#i-cards', 'gridTemplateColumns').then(v => v.split(' ').length), 3, 'columns_lg=3');
  await p.setViewportSize({ width: 400, height: 800 }); await p.waitForTimeout(80);
  eq(await css('#i-cards', 'gridTemplateColumns').then(v => v.split(' ').length), 1, 'back to 1 column');
  // 2) الوضع الداكن عبر setTheme/toggleTheme + لاحقة _dark
  eq(await p.$eval('#i-root', e => e.style.background.includes('245')), true, 'light bg');
  await p.click('#i-theme'); await p.waitForTimeout(80);
  eq(await p.evaluate(() => document.documentElement.getAttribute('data-theme')), 'dark', 'theme attr');
  eq(await p.$eval('#i-root', e => e.style.background), 'rgb(11, 11, 20)', 'bg_dark applied');
  await p.click('#i-theme'); await p.waitForTimeout(80);
  eq(await p.$eval('#i-root', e => e.style.background.includes('245')), true, 'light again');
  // 3) hover
  await p.hover('#i-hov'); await p.waitForTimeout(250);
  eq(await css('#i-hov', 'backgroundColor'), 'rgb(47, 125, 196)', 'hover_bg');
  // 4) toast
  await p.click('#i-hov'); await p.waitForTimeout(60);
  eq(await p.textContent('#in-toasts .in-toast'), 'تم!', 'toast text');
  // 5) حركة: stagger يوزّع التأخير على الأبناء
  eq(await css('#i-c2', 'animationDelay'), '0.08s', 'stagger delay on child 2');
  eq(await css('#i-h', 'animationName'), 'in-slideUp', 'animate slideUp');
  // 6) tabs/selected + visible
  eq(await p.$eval('#i-ta', e => e.getAttribute('aria-selected')), 'true', 'tab a selected');
  await p.click('#i-tb'); await p.waitForTimeout(50);
  eq(await p.$eval('#i-panel', e => e.hidden), false, 'panel visible after tab b');
  eq(await p.$eval('#i-tb', e => e.getAttribute('aria-selected')), 'true', 'tab b selected');
  // 7) Rating ثنائي الاتجاه
  eq(await p.$$eval('#i-rate .in-star.on', n => n.length), 3, 'rating initial 3');
  await p.click('#i-rate .in-star:nth-child(5)'); await p.waitForTimeout(60);
  eq(await p.textContent('#i-rated'), 'stars=5', 'rating → warp');
  // 8) Pagination ثنائي الاتجاه + Steps + Breadcrumb
  await p.click('#i-pager button[aria-label],#i-pager button:last-child'); await p.waitForTimeout(60);
  eq(await p.textContent('#i-pg'), 'page=3', 'pagination next → warp');
  eq(await p.$$eval('#i-steps li', n => n.map(x => x.dataset.state).join(',')), 'done,current,todo', 'steps states');
  eq(await p.$$eval('#i-crumbs a, #i-crumbs span:not(.sep)', n => n.length), 3, 'breadcrumb items');
  // 9) Accordion (details) يتبع open
  eq(await p.$eval('#i-acc', e => e.open), false, 'accordion closed');
  eq(await p.$eval('#i-acc > summary', e => e.textContent), 'FAQ', 'accordion title');
  // 10) PWA: manifest مربوط
  eq(await p.$eval('link[rel=manifest]', e => e.getAttribute('href')), 'manifest.webmanifest', 'manifest link');
  await ctx.close();
  // 11) prefers-color-scheme: dark يفعّل _dark
  const c2 = await b.newContext({ viewport: { width: 400, height: 800 }, colorScheme: 'dark' }), p2 = await c2.newPage();
  await p2.goto('file://' + process.argv[2]); await p2.waitForTimeout(80);
  eq(await p2.$eval('#i-root', e => e.style.background), 'rgb(11, 11, 20)', 'system dark → bg_dark');
  eq(errs.length, 0, 'page errors: ' + errs.join('|'));
  await b.close(); console.log('pro browser OK');
})();

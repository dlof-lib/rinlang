// اختبار متصفح للجولة الثانية: مسارات، DataTable، عدّاد، رسوم، كشف عند التمرير، اختصارات، Dialog، Dropdown، تنزيل
const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch(), errs = [];
  const eq = (a, e, m) => { if (a !== e) { console.error(`${m}: expected ${JSON.stringify(e)}, got ${JSON.stringify(a)}`); process.exit(1); } };
  const ctx = await b.newContext({ viewport: { width: 900, height: 700 }, acceptDownloads: true });
  const p = await ctx.newPage(); p.on('pageerror', e => errs.push(String(e)));
  await p.goto('file://' + process.argv[2]); await p.waitForTimeout(1100);
  // 1) router: "/" مرئي فقط، ثم /data
  eq(await p.$eval('#i-home', e => e.hidden), false, 'home visible');
  eq(await p.$eval('#i-data', e => e.hidden), true, 'data hidden');
  eq(await p.$eval('#i-n1', e => e.getAttribute('aria-current')), 'page', 'nav home active');
  await p.click('#i-n2'); await p.waitForTimeout(80);
  eq(await p.evaluate(() => location.hash), '#/data', 'hash after go');
  eq(await p.$eval('#i-home', e => e.hidden), true, 'home hidden after nav');
  eq(await p.$eval('#i-data', e => e.hidden), false, 'data visible after nav');
  // 2) DataTable: صفحات، فرز، تصفية ثنائية الاتجاه مع warp
  eq(await p.$$eval('#i-tbl tbody tr', n => n.length), 4, 'page_size=4');
  eq(await p.textContent('#i-tbl .dt-foot span'), '1–4 / 6', 'footer info');
  await p.click('#i-tbl th:nth-child(3)'); await p.waitForTimeout(40);
  eq(await p.$eval('#i-tbl tbody tr td', e => e.textContent), 'Adam', 'sort asc by score (70)');
  await p.click('#i-tbl th:nth-child(3)'); await p.waitForTimeout(40);
  eq(await p.$eval('#i-tbl tbody tr td', e => e.textContent), 'Maya', 'sort desc (95)');
  await p.fill('#i-flt', 'dev'); await p.waitForTimeout(80);
  eq(await p.$$eval('#i-tbl tbody tr', n => n.length), 2, 'filter dev → 2');
  // 3) رسوم
  await p.click('#i-n3'); await p.waitForTimeout(80);
  eq(await p.$$eval('#i-sl svg polyline', n => n.length), 1, 'sparkline polyline');
  eq(await p.$$eval('#i-bc svg rect', n => n.length), 5, 'bar rects');
  eq((await p.$eval('#i-dn', e => e.getAttribute('aria-label'))).includes('45, 30, 25'), true, 'donut aria');
  // 4) رجوع للرئيسية: عدّاد، stat، marquee، tooltip
  await p.click('#i-n1'); await p.waitForTimeout(1200);
  eq(await p.textContent('#i-c1'), '$12,840', 'count_to final');
  eq(await p.$eval('#i-s2 .sd', e => e.className), 'sd down', 'stat delta down');
  eq(await p.$eval('#i-s1 .sd', e => e.className), 'sd up', 'stat delta up');
  eq(await p.$$eval('#i-mq .in-marquee-track', n => n.length), 2, 'marquee cloned track');
  eq(await p.$eval('#i-rv1', e => e.dataset.tip), 'Reveals on scroll', 'tip data');
  eq(await p.$eval('#i-pr', e => e.getAttribute('aria-valuenow')), '72', 'ring value');
  // 5) reveal: مخفي قبل الظهور ثم يظهر + onVisible
  eq(await p.$eval('#i-rv2', e => e.style.opacity), '0', 'reveal starts hidden');
  await p.evaluate(() => document.getElementById('i-rv2').scrollIntoView({ block: 'center' })); await p.waitForTimeout(900);
  eq(await p.$eval('#i-rv2', e => e.style.opacity), '', 'reveal shown');
  eq(await p.textContent('#i-rv2 .in-text, #i-rv2 > *'), 'visible events: 1', 'onVisible fired once');
  // 6) scroll_progress
  await p.evaluate(() => window.scrollTo(0, document.documentElement.scrollHeight)); await p.waitForTimeout(100);
  eq(Math.round(await p.$eval('#i-sp', e => e.value)), 100, 'scroll progress 100');
  await p.evaluate(() => window.scrollTo(0, 0));
  // 7) hotkey + dialog + dismiss (Esc → onClose)
  await p.keyboard.press('Control+k'); await p.waitForTimeout(150);
  eq(await p.$eval('#i-dlgbox', e => e.hidden), false, 'ctrl+k opens dialog');
  eq(await p.$eval('#i-dlgbox', e => e.classList.contains('in-scrim')), true, 'scrim class');
  await p.keyboard.press('Escape'); await p.waitForTimeout(150);
  eq(await p.$eval('#i-dlgbox', e => e.hidden), true, 'Esc closes via onClose');
  // 8) dropdown يفتح ويُغلق بالنقر خارجاً + تنزيل ملف
  await p.click('#i-dd > summary'); eq(await p.$eval('#i-dd', e => e.open), true, 'dropdown open');
  const [dl] = await Promise.all([p.waitForEvent('download'), p.click('#i-d2')]);
  eq(dl.suggestedFilename(), 'hello.txt', 'downloadText filename');
  await p.click('#i-up', { force: true }).catch(() => {}); await p.mouse.click(5, 300); await p.waitForTimeout(80);
  eq(await p.$eval('#i-dd', e => e.open), false, 'dropdown closes on outside click');
  eq(errs.length, 0, 'page errors: ' + errs.join('|'));
  await b.close(); console.log('pro2 browser OK');
})();

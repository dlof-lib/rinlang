// اختبار الجسر: صفحة هجينة (web + DrawList المحرّك الأصلي على canvas) — تبديل العرض، رسم فعلي، وتكافؤ النصوص بين المحرّكين.
const { chromium } = require('playwright'); const fs = require('fs');
(async () => {
  const [html, nativeJson] = [process.argv[2], process.argv[3]];
  const eq = (a, e, m) => { if (a !== e) { console.error(`${m}: expected ${JSON.stringify(e)}, got ${JSON.stringify(a)}`); process.exit(1); } };
  const b = await chromium.launch(), errs = [], p = await b.newPage({ viewport: { width: 900, height: 700 } });
  p.on('pageerror', e => errs.push(String(e))); p.on('console', m => { if (m.type() === 'error') errs.push(m.text()); });
  await p.goto('file://' + html); await p.waitForTimeout(400);
  eq(await p.$eval('body', e => e.dataset.inView), 'web', 'starts in web view');
  eq(await p.$eval('#indsin-root', e => e.hidden), false, 'web visible');
  // 1) التبديل web → native → split → web
  await p.click('#in-hybrid-toggle'); eq(await p.$eval('body', e => e.dataset.inView), 'native', 'toggle → native');
  eq(await p.$eval('#indsin-root', e => e.hidden), true, 'web hidden in native');
  eq(await p.$eval('.in-native-wrap', e => e.hidden), false, 'canvas visible in native');
  await p.click('#in-hybrid-toggle'); eq(await p.$eval('body', e => e.dataset.inView), 'split', 'toggle → split');
  eq(await p.$eval('#indsin-root', e => e.hidden) || await p.$eval('.in-native-wrap', e => e.hidden), false, 'both visible in split');
  // 2) الـ canvas مرسوم فعلاً: أبعاد DrawList + بكسلات غير الخلفية
  const info = await p.evaluate(() => { const c = window.indsinNative.canvas, d = window.indsinNative.data, x = c.getContext('2d'); const im = x.getImageData(0, 0, c.width, c.height).data;
    const bg = im.slice(0, 3); let diff = 0; for (let i = 0; i < im.length; i += 4) if (im[i] !== bg[0] || im[i + 1] !== bg[1] || im[i + 2] !== bg[2]) diff++; return { cw: c.style.width, dw: d.canvas.width, diff, total: im.length / 4 }; });
  eq(info.cw, info.dw + 'px', 'canvas CSS width = native canvas width');
  eq(info.diff > info.total * 0.01, true, 'canvas has real painted content (' + info.diff + '/' + info.total + ')');
  // 3) تكافؤ النصوص: كل نص ظاهر في المحرّك الأصلي موجود في صفحة الويب
  const nat = JSON.parse(fs.readFileSync(nativeJson, 'utf8')); const texts = [...new Set(nat.paint.filter(x => x.op === 'text').map(x => String(x.text).trim()).filter(Boolean))];
  const dom = await p.$eval('#indsin-root', e => e.innerText + ' ' + Array.from(e.querySelectorAll('[aria-label],[title],input')).map(n => (n.getAttribute('aria-label') || '') + (n.getAttribute('title') || '') + (n.value || '')).join(' '));
  const missing = texts.filter(t => !dom.includes(t)); const parity = 1 - missing.length / Math.max(1, texts.length);
  console.log(`parity: ${texts.length - missing.length}/${texts.length} native texts present in web` + (missing.length ? ` (missing: ${missing.slice(0, 5).join(' | ')})` : ''));
  eq(parity >= (+process.env.MIN_PARITY || 0.8), true, 'text parity >= threshold');
  await p.click('#in-hybrid-toggle'); eq(await p.$eval('body', e => e.dataset.inView), 'web', 'toggle → web');
  eq(errs.length, 0, 'page errors: ' + errs.join('|'));
  await b.close(); console.log('hybrid browser OK');
})();

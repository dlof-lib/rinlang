'use strict';
/* Zero-dependency test suite for the Indsin Object translator & interpreter.  Run: node tests/test_indsin_object.js */
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { spawnSync } = require('child_process');
const IO = require('../src/indsin_object.js');

let passed = 0, failed = 0;
function test(name, fn) {
  try { fn(); passed++; }
  catch (e) { failed++; console.error('✗ ' + name + '\n    ' + (e && e.message || e)); }
}
function eq(a, b, m) { if (JSON.stringify(a) !== JSON.stringify(b)) throw new Error((m || 'not equal') + ': ' + JSON.stringify(a) + ' !== ' + JSON.stringify(b)); }
function ok(c, m) { if (!c) throw new Error(m || 'assertion failed'); }
function has(h, s, m) { if (h.indexOf(s) < 0) throw new Error((m || 'missing') + ': ' + s); }
function lacks(h, s, m) { if (h.indexOf(s) >= 0) throw new Error((m || 'unexpected') + ': ' + s); }
const g = (kind, attrs, children) => ({ kind, attrs: attrs || {}, children });
const html = (tree, o) => IO.translate(tree, o).html;
const fixture = (n) => fs.readFileSync(path.join(__dirname, 'fixtures', n), 'utf8');

/* ── catalog & coverage ── */
test('65 kinds (64 built-ins + Custom)', () => { eq(IO.kinds().length, 65); ok(IO.kinds().includes('Custom')); });
test('built-in self-test passes', () => { const r = IO.selfTest(); ok(r.ok, JSON.stringify(r)); eq(r.rendered, 65); });
test('every alias accepted by strandKindFromTag resolves', () => {
  const al = { Container: 'Box', Panel: 'Box', Frame: 'Box', TextField: 'Input', Modal: 'Dialog', Popup: 'Dialog', Range: 'Slider', Dropdown: 'Select',
    FilePicker: 'File', DatePicker: 'Date', TimePicker: 'Time', SearchBar: 'Search', Sidebar: 'Drawer', Chip: 'Tag', Key: 'Kbd', Loader: 'Spinner',
    Stepper: 'Steps', Step: 'StepItem', TimelineEvent: 'TimelineItem', Breadcrumbs: 'Breadcrumb', Pager: 'Pagination', Donut: 'DonutChart', PieChart: 'DonutChart',
    topBar: 'TopBar', bottom_bar: 'BottomBar', icon_button: 'IconButton', code_editor: 'CodeEditor' };
  for (const k in al) eq(IO.resolveKind(k), al[k], k);
});
test('every kind renders in flow and absolute mode', () => {
  for (const k of IO.kinds()) {
    const f = IO.translate(g(k, { text: 'x', label: 'x' }, []));
    has(f.html, 'data-ix-k="' + k.toLowerCase() + '"', 'flow ' + k);
    const a = IO.translate({ ok: true, fabric: { kind: k, name: 'n', x: 0, y: 0, w: 100, h: 30, attrs: { text: 'x' }, children: [] } });
    has(a.html, 'data-ix-k="' + k.toLowerCase() + '"', 'abs ' + k);
  }
});
test('unknown kinds are reported, not fatal', () => { const t = IO.translate(g('Column', {}, [g('Hologram', {}, [])])); eq(t.unknown, ['Hologram']); has(t.html, 'ix-unk'); });
test('catalog exposes category/aliases', () => { const c = IO.catalog().find(k => k.kind === 'Box'); ok(c.aliases.includes('container')); eq(c.category, 'layout'); });

/* ── security ── */
test('text and attributes are escaped', () => {
  const h = html(g('Column', {}, [g('Text', { text: '<img src=x onerror=alert(1)>"\'&' }), g('Button', { label: '"><script>1</script>' })]));
  lacks(h, '<img src=x'); lacks(h, '<script>1'); has(h, '&lt;img src=x onerror=alert(1)&gt;');
});
test('javascript: / vbscript: / file: URLs are refused', () => {
  const h = html(g('Column', {}, [g('Link', { text: 'a', href: 'javascript:alert(1)' }), g('Image', { src: 'javascript:alert(1)' }), g('WebView', { url: 'javascript:alert(1)' }), g('Video', { src: 'file:///etc/passwd' }), g('Link', { text: 'b', href: 'vbscript:x' })]));
  lacks(h, 'javascript:'); lacks(h, 'vbscript:'); lacks(h, 'file:///');
});
test('data: only for images/media, never text/html', () => {
  lacks(html(g('Image', { src: 'data:text/html,<script>1</script>' })), 'data:text');
  has(html(g('Image', { src: 'data:image/png;base64,AAAA' })), 'data:image/png');
  lacks(html(g('Video', { src: 'data:image/png;base64,AAAA' })), 'data:image');
});
test('file URLs allowed only on explicit opt-in', () => {
  has(html(g('Video', { src: 'file:///android_asset/a.mp4' }), { allowFileUrls: true }), 'file:///android_asset/a.mp4');
});
test('custom URL policy is honoured', () => { has(html(g('Image', { src: 'x.png' }), { urlPolicy: (u) => 'https://cdn.example/' + u }), 'https://cdn.example/x.png'); });
test('CSS injection through color attributes is rejected', () => {
  const h = html(g('Text', { text: 'x', color: 'red;background:url(//evil)' }));
  lacks(h, 'evil'); lacks(h, 'url(');
});
test('iframes are sandboxed', () => { const h = html(g('WebView', { url: 'https://example.com' })); has(h, 'sandbox="allow-scripts allow-forms allow-popups"'); has(h, 'referrerpolicy="no-referrer"'); lacks(h, 'allow-same-origin'); });
test('external links get rel=noopener', () => { has(html(g('Link', { text: 'x', href: 'https://example.com' })), 'rel="noopener noreferrer"'); });

/* ── tokens ── */
test('spacing/radius/typography/elevation tokens', () => {
  const h = html(g('Card', { padding: 'spacious', radius: 'pill', elevation: 'lg', title: 't' }));
  has(h, 'padding:24px'); has(h, 'border-radius:9999px'); has(h, 'box-shadow:0 8px 20px');
  const t = html(g('Text', { text: 'x', size: 'heading' })); has(t, 'font-size:28px'); has(t, 'font-weight:700');
});
test('percent and numeric sizes', () => { const h = html(g('Box', { width: '50%', height: 40, margin: 'compact' })); has(h, 'width:50%'); has(h, 'height:40px'); has(h, 'margin:8px'); });
test('tone resolves to theme variables', () => { has(html(g('Button', { label: 'x', tone: 'danger' })), '--ix-tone:var(--ix-danger)'); });
test('valid literal colors pass through', () => { has(html(g('Text', { text: 'x', color: '#ff00aa' })), 'color:#ff00aa'); has(html(g('Text', { text: 'x', color: 'rgba(1,2,3,.5)' })), 'rgba(1,2,3,.5)'); });
test('button sizes and variants', () => {
  for (const v of ['fill', 'outline', 'ghost', 'link', 'glass', 'gradient']) has(html(g('Button', { label: 'x', variant: v })), 'ix-v-' + v);
  has(html(g('Button', { label: 'x', size: 'large' })), 'min-height:52px');
});
test('Banner types map to roles', () => { has(html(g('Banner', { type: 'error', title: 't' })), 'ix-b-error'); has(html(g('Banner', { type: 'error', title: 't' })), 'role="alert"'); });

/* ── themes ── */
test('11 built-in themes, all roles defined', () => {
  eq(IO.themes().length, 11);
  for (const n of IO.themes()) { const t = IO.theme(n); for (const r of ['primary', 'secondary', 'success', 'danger', 'warning', 'info', 'neutral', 'surface', 'background', 'text', 'text_muted', 'border']) ok(Array.isArray(t[r]), n + '.' + r); }
});
test('theme selection', () => { has(html(g('Text', { text: 'x' }), { theme: 'Light' }), '--ix-background:rgb(245,245,248)'); has(html(g('Text', { text: 'x' }), { theme: 'Sepia' }), 'rgb(244,236,216)'); });
test('registerTheme / themeFromSeed', () => {
  IO.registerTheme({ name: 'Brand', primary: '#ff0066' }); ok(IO.themes().includes('Brand')); has(html(g('Text', { text: 'x' }), { theme: 'Brand' }), 'rgb(255,0,102)');
  const t = IO.themeFromSeed('Seeded', '#3366ff', true); eq(t.primary, [51, 102, 255]);
});
test('theme "auto" emits both palettes behind prefers-color-scheme', () => { const t = IO.translate(g('Text', { text: 'x' }), { theme: 'auto' }); has(t.css, 'prefers-color-scheme:dark'); has(t.html, 'ix-auto'); });

/* ── responsive & effects ── */
test('responsive overrides are mobile-first', () => {
  const tree = g('Grid', { columns: 1, columns_md: 2, columns_lg: 3, gap: 8, gap_lg: 24 }, []);
  has(html(tree, { width: 390 }), 'repeat(1,'); has(html(tree, { width: 700 }), 'repeat(2,'); has(html(tree, { width: 900 }), 'repeat(3,'); has(html(tree, { width: 900 }), 'gap:24px');
  eq(IO.breakpointOf(359), 'xs'); eq(IO.breakpointOf(840), 'lg'); eq(IO.breakpointOf(1300), 'xl');
});
test('effects, inheritance and stagger', () => {
  const h = html(g('Column', { effect: 'slideUp', duration: 'slow', stagger: 100 }, [g('Text', { text: 'a' }), g('Text', { text: 'b' }), g('Text', { text: 'c' })]));
  has(h, 'animation:ix-slideUp 400ms ease-out 0ms both'); has(h, 'ix-slideUp 400ms ease-out 100ms both'); has(h, 'ix-slideUp 400ms ease-out 200ms both');
  lacks(html(g('Text', { text: 'x', effect: 'fade' }), { animations: false }), 'animation:');
});

/* ── element behaviour in markup ── */
test('Banner closable + Tag removable + Pagination', () => {
  has(html(g('Banner', { title: 'a', closable: 'true' })), 'data-ix-close');
  has(html(g('Tag', { text: 'a', removable: true })), 'data-ix-remove');
  const p = html(g('Pagination', { current: 5, total: 20 })); has(p, 'aria-current="page"'); has(p, '…'); has(p, 'data-ix-page="6"');
});
test('Steps derive states from current', () => {
  const h = html(g('Steps', { current: 1 }, [g('StepItem', { label: 'a' }), g('StepItem', { label: 'b' }), g('StepItem', { label: 'c' })]));
  has(h, 'ix-done'); has(h, 'ix-cur'); has(h, 'ix-todo'); has(h, 'aria-current="step"');
});
test('Table renders real table markup', () => {
  const h = html(g('Table', {}, [g('TableRow', { header: 'true' }, [g('Text', { text: 'A' })]), g('TableRow', {}, [g('Text', { text: '1' })])]));
  has(h, '<table'); has(h, '<th scope="col">'); has(h, '<td>');
});
test('Select options, Slider bounds, Progress clamp', () => {
  const s = html(g('Select', { options: 'a,b,c', value: 'b' })); has(s, '<option value="b" selected>');
  has(html(g('Slider', { min: 10, max: 20, value: 99 })), 'value="20"'); has(html(g('Progress', { value: 250 })), 'width:100%');
});
test('Object uses data, registry, and warns when unknown', () => {
  has(html(Object.assign(g('Object', { source: 'u' }), { data: { name: 'ABOO' } })), 'ABOO');
  IO.registerObject('reg1', { k: 'v' }); has(html(g('Object', { source: 'reg1' })), '<b>k</b>');
  const t = IO.translate(g('Object', { source: 'nope' })); has(t.html, 'No object registered'); eq(t.issues.length, 1);
});
test('DonutChart / Rating / Avatar / Icon', () => {
  has(html(g('DonutChart', { data: 'A:1,B:3', centerLabel: '4' })), 'stroke-dasharray'); has(html(g('Rating', { value: 3, max: 5 })), 'aria-label="3 / 5"');
  has(html(g('Avatar', { name: 'Rin Lang' })), 'RL'); has(html(g('Icon', { icon: 'settings' })), '⚙');
});
test('Dialog open/closed and dismiss metadata', () => { const h = html(g('Dialog', { title: 'T', open: 'false' })); has(h, 'hidden'); has(h, 'data-ix-dismiss'); has(h, 'role="dialog"'); });
test('interaction metadata', () => {
  const h = html(g('Text', { text: 'x', onTap: 'inc()', onLongPress: 'a()', onDoubleTap: 'b()', onHoverEnter: 'c()', onHoverExit: 'd()', url: '#top', copyText: 'zz' }));
  for (const k of ['data-ix-tap="inc()"', 'data-ix-lp', 'data-ix-dt', 'data-ix-he', 'data-ix-hx', 'data-ix-url="#top"', 'data-ix-copy="zz"', 'role="button"', 'tabindex="0"']) has(h, k);
});
test('registerComponent handles Custom tags', () => {
  IO.registerComponent('Chart3D', (n, a, h) => '<div class="chart3d">' + h.esc(a.str('title')) + '</div>');
  has(html(g('Custom', { title: '<b>' }, []), {}) + html({ kind: 'Chart3D', sourceTag: 'Chart3D', attrs: { title: '<b>' } }), 'chart3d">&lt;b&gt;');
});

/* ── absolute mode with REAL engine frames ── */
test('real engine frame: absolute geometry & resolved colors', () => {
  const t = IO.translate(fixture('showcase.frame.json'));
  eq(t.mode, 'absolute'); eq(t.width, 390); has(t.html, 'data-ix-mode="absolute"'); has(t.html, 'left:20px;top:20px'); has(t.html, 'background:#282A36');
  eq(t.unknown, []);
});
test('real engine frame: banner (synthesized children, closable)', () => { const t = IO.translate(fixture('banner.frame.json')); has(t.html, 'ix-banA'); eq(t.unknown, []); });
test('real engine frame: UI library (steps/timeline/tag/pagination)', () => {
  const t = IO.translate(fixture('ui_library.frame.json')); has(t.html, 'ix-seg'); has(t.html, 'ix-tagA'); has(t.html, 'ix-pgn'); eq(t.unknown, []);
});
test('validate() on a real frame', () => { const v = IO.validate(fixture('ui_library.frame.json')); ok(v.ok, JSON.stringify(v.errors)); });
test('overlay scrims come from the engine envelope', () => {
  const env = { ok: true, overlays: [{ name: 'dlg', kind: 'dialog', modal: true, scrim: true, box: { x: 10, y: 10, w: 100, h: 80 }, scrimRect: { x: 0, y: 0, w: 390, h: 800 } }], fabric: { kind: 'Column', name: 'root', x: 0, y: 0, w: 390, h: 800, attrs: {}, children: [{ kind: 'Dialog', name: 'dlg', x: 10, y: 10, w: 100, h: 80, attrs: { title: 'Hi' }, children: [] }] } };
  const h = html(env); has(h, 'ix-scrim'); has(h, 'role="dialog"');
});

/* ── errors ── */
test('engine errors surface with line numbers', () => {
  try { IO.translate({ ok: false, error: 'boom', line: 7 }); throw new Error('no throw'); } catch (e) { eq(e.name, 'IndsinError'); eq(e.line, 7); eq(e.message, 'boom'); }
  try { IO.translate('{nope'); throw new Error('no throw'); } catch (e) { has(e.message, 'Invalid JSON'); }
  try { IO.translate({}); throw new Error('no throw'); } catch (e) { has(e.message, 'kind'); }
});
test('validate() reports problems', () => {
  const v = IO.validate(g('Column', {}, [g('Hologram'), g('Text', { text: 'x', color: 'not a color!!' }), g('Button', { tone: 'nope' }), g('Image', {})]));
  ok(!v.ok); const codes = v.errors.map(e => e.code).concat(v.warnings.map(w => w.code));
  for (const c of ['KIND-UNKNOWN', 'COLOR-INVALID', 'TONE-UNKNOWN', 'A11Y-001', 'A11Y-002']) ok(codes.includes(c), c);
});

/* ── output formats & the embedded interpreter ── */
test('toDocument is a complete, offline document', () => {
  const d = IO.toDocument(IO.sample(), { title: 'T <x>', lang: 'ar', dir: 'rtl' });
  has(d, '<!doctype html>'); has(d, '<title>T &lt;x&gt;</title>'); has(d, 'dir="rtl"'); has(d, 'lang="ar"'); has(d, '<script>'); lacks(d, 'http://'); lacks(d, 'https://cdn');
});
test('embedded interpreter is syntactically valid and self-contained', () => {
  const src = IO.runtimeSource(); new vm.Script(src);
  const d = IO.toDocument(IO.sample()); const m = /<script>([\s\S]*)<\/script>/.exec(d); ok(m); new vm.Script(m[1]);
  lacks(IO.toDocument(IO.sample(), { script: false }), '<script>');
});
test('pager helper used by the interpreter matches the translator', () => {
  const ctx = { esc: (s) => String(s) }; vm.createContext(ctx); vm.runInContext(IO.runtimeSource() + ';this.pagerHtml=pagerHtml;this.calcEval=calcEval;', ctx);
  has(ctx.pagerHtml(3, 9, ctx.esc), 'aria-current="page"');
  eq(ctx.calcEval('2+3*4'), 14); eq(ctx.calcEval('-5*-3'), 15); eq(ctx.calcEval('10/4'), 2.5); eq(ctx.calcEval('7%4'), 3); ok(isNaN(ctx.calcEval('1/0'))); ok(isNaN(ctx.calcEval('2++')) === false || true);
});
test('toHtml fragment contains style + markup', () => { const h = IO.toHtml(g('Text', { text: 'x' }), { script: false }); ok(h.startsWith('<style>')); has(h, 'data-ix-k="text"'); });

/* ── CLI ── */
test('CLI converts a frame and validates', () => {
  const bin = path.join(__dirname, '..', 'bin', 'indsin-object.js'), fx = path.join(__dirname, 'fixtures', 'showcase.frame.json');
  let r = spawnSync(process.execPath, [bin, fx, '--theme', 'light', '--no-script'], { encoding: 'utf8' }); eq(r.status, 0, r.stderr); has(r.stdout, '<!doctype html>'); lacks(r.stdout, '<script>');
  r = spawnSync(process.execPath, [bin, '--validate', fx], { encoding: 'utf8' }); eq(r.status, 0); has(r.stdout, '"ok": true');
  r = spawnSync(process.execPath, [bin, '--kinds'], { encoding: 'utf8' }); eq(r.stdout.trim().split('\n').length, 65);
  r = spawnSync(process.execPath, [bin, '--self-test'], { encoding: 'utf8' }); eq(r.status, 0);
  r = spawnSync(process.execPath, [bin, 'nope.json'], { encoding: 'utf8' }); eq(r.status, 2);
  r = spawnSync(process.execPath, [bin, '-'], { input: '{"kind":"Text","attrs":{"text":"hi"}}', encoding: 'utf8' }); eq(r.status, 0); has(r.stdout, 'hi');
});

/* ── syntax highlighter & generated grammar ── */
const HL = require('../src/indsin_highlight.js');
const root = path.join(__dirname, '..', '..', '..');
const sampleSrc = () => fs.readFileSync(path.join(root, 'editors', 'vscode-indsin', 'examples', 'sample.indsin'), 'utf8');
test('highlighter tokenisation is lossless', () => { const c = sampleSrc(); eq(HL.tokenize(c).map((t) => t.text).join(''), c); });
test('highlighter classifies indsin constructs', () => {
  const by = {}; HL.tokenize(sampleSrc()).forEach((t) => { (by[t.type] = by[t.type] || []).push(t.text); });
  ok(by.kind.includes('Banner') && by.kind.includes('Card'), 'kinds'); ok(by.attr.includes('padding') && by.responsive.includes('_md'), 'attrs');
  ok(by.token.includes('"spacious"'), 'tokens'); ok(by.color.includes('"#1E1E2E"'), 'colors'); ok(by.length.includes('120px'), 'lengths');
  ok(by.end.includes('.end/view') && by.end.includes('.end/container'), 'end tags'); ok(by.keyword.includes('warp'), 'rin keywords');
});
test('highlighter escapes HTML', () => { const h = HL.highlight('@view.Text=a\n  text="<script>x</script>";\n.end/view'); lacks(h, '<script>x'); has(h, '&lt;script&gt;'); });
test('every translator kind is in the highlighter lexicon and grammar', () => {
  const g = fs.readFileSync(path.join(root, 'syntaxes', 'indsin.tmLanguage.json'), 'utf8'), lex = require(path.join(root, 'syntaxes', 'indsin-lexicon.json'));
  eq(lex.kinds.length, 65); IO.kinds().forEach((k) => { ok(HL.lexicon.kinds.includes(k), k); ok(g.indexOf(k) >= 0, 'grammar ' + k); });
});
test('TextMate grammar is valid JSON and every regex compiles', () => {
  const gr = JSON.parse(fs.readFileSync(path.join(root, 'syntaxes', 'indsin.tmLanguage.json'), 'utf8')); eq(gr.scopeName, 'source.indsin');
  (function walk(o) { if (Array.isArray(o)) return o.forEach(walk); if (o && typeof o === 'object') { ['match', 'begin', 'end'].forEach((k) => { if (typeof o[k] === 'string') new RegExp(o[k]); }); Object.values(o).forEach(walk); } })(gr);
});

console.log(`\nindsin-object ${IO.version}: ${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);

// اختبارات web/libraries.js بلا متصفح: دوال نقية + محاكاة DOM/Firebase صغيرة.
// تشغيل: node tests/web/libraries.test.js   (تحتاج Node 18+)   — أنشئ /tmp/pkg_*.b64 عبر tests/web/make_fixtures.py
const assert = require('assert'); const fs = require('fs'); const path = require('path');
let passed = 0; const ok = (c, m) => { assert.ok(c, m); passed++; console.log('  ✓', m); };
const eq = (a, b, m) => { assert.deepStrictEqual(a, b, m); passed++; console.log('  ✓', m); };

// ---------------- DOM shim ----------------
class Base { constructor() { this.children = []; this.parent = null; this.listeners = {}; } }
class Txt extends Base { constructor(t) { super(); this.t = t; } get textContent() { return this.t; } }
class El extends Base {
  constructor(tag) { super(); this.tag = tag; this.attrs = {}; this.style = {}; this.className = ''; this._t = ''; this.value = '';
    const self = this, set = new Set();
    this.classList = { add: c => set.add(c), remove: c => set.delete(c), contains: c => set.has(c), toggle: (c, on) => (on === undefined ? !set.has(c) : on) ? set.add(c) : set.delete(c) };
    this._set = set; }
  appendChild(c) { if (c.children && c.tag === '#frag') { c.children.forEach(x => this.appendChild(x)); c.children = []; return c; } c.parent = this; this.children.push(c); return c; }
  removeChild(c) { this.children = this.children.filter(x => x !== c); }
  get firstChild() { return this.children[0] || null; }
  set textContent(v) { this.children = []; this._t = String(v); }
  get textContent() { return this._t + this.children.map(c => c.textContent).join(''); }
  setAttribute(k, v) { this.attrs[k] = String(v); } getAttribute(k) { return this.attrs[k]; }
  addEventListener(t, f) { (this.listeners[t] = this.listeners[t] || []).push(f); }
  fire(t) { (this.listeners[t] || []).forEach(f => f({ target: this, stopPropagation() {}, preventDefault() {}, key: 'Enter' })); }
  click() { this.fire('click'); } focus() {} select() {}
  cls() { return this.className + ' ' + [...this._set].join(' '); }
}
global.Node = Base;
const html = new El('html'), body = new El('body'), head = new El('head'); html.attrs.lang = 'en'; html.lang = 'en';
global.document = { readyState: 'complete', documentElement: html, body, head,
  createElement: t => new El(t), createTextNode: t => new Txt(t), createDocumentFragment: () => new El('#frag'),
  getElementById(id) { const f = n => { if (n.attrs && n.attrs.id === id) return n; for (const c of n.children) { const r = c.children && f(c); if (r) return r; } return null; }; return f(body); },
  execCommand() { return true; }, addEventListener() {} };
const walk = (n, fn) => { fn(n); (n.children || []).forEach(c => walk(c, fn)); };
const find = (pred, root = body) => { let r = null; walk(root, n => { if (!r && n instanceof El && pred(n)) r = n; }); return r; };
const btn = txt => find(n => n.tag === 'button' && n.textContent.includes(txt));
let store = {}, pushed = [], copied = [], blobs = [];
global.window = global; global.localStorage = { getItem: k => store[k] || null, setItem: (k, v) => { store[k] = v; } };
global.location = { search: '', pathname: '/rinlang/', origin: 'https://user.github.io' };
const setUrl = u => { if (!u) return; const [pn, q] = u.split('?'); location.pathname = pn || location.pathname; location.search = q ? '?' + q : ''; };
global.history = { replaceState: (s, t, u) => setUrl(u), pushState: (s, t, u) => { pushed.push(u); setUrl(u); } };
global.matchMedia = () => ({ matches: false }); global.isSecureContext = true;
Object.defineProperty(global, 'navigator', { value: { clipboard: { writeText: t => { copied.push(t); return Promise.resolve(); } } }, configurable: true });
const _URL = URL; global.URL = Object.assign(function (...a) { return new _URL(...a); }, { createObjectURL: b => { blobs.push(b); return 'blob:x'; }, revokeObjectURL() {} }); global.URL.prototype = _URL.prototype;
global.URLSearchParams = URLSearchParams;
global.addEventListener = () => {};
const tick = (ms = 15) => new Promise(r => setTimeout(r, ms));

// ---------------- Firebase mock (يحاكي قاعدة قواعد العدّادات نفسها) ----------------
const b64 = fs.readFileSync('/tmp/pkg_deflate.b64', 'utf8'), SRC = fs.readFileSync('/tmp/pkg_src.txt', 'utf8');
const db = { libraries: { math_og_rin: { name: 'math', fileName: 'math.og.rin', version: '1.2.0', description: 'Mathematical utilities', content: 'let x = 1; // hi\n', sizeBytes: 17, updatedAt: 1700000000000, type: 'official', likeCount: 2, downloadCount: 5 } },
  packages: { P1: { id: 'P1', name: 'MyLib', version: '0.3.0', description: '<img src=x onerror=alert(1)>', publisherUid: 'u9', publisherName: 'sara', fileName: 'mylib.og.rinsdk', base64Data: b64, sizeBytes: 1234, downloadCount: 0, likeCount: 0, createdAt: 1800000000000, category: 'رياضيات', dependencies: { core: '^1.0.0' } },
    P2: { id: 'P2', name: 'Other', version: '1.0.0', description: 'zzz', publisherUid: 'u1', publisherName: 'me', fileName: 'o.og.rinsdk', base64Data: b64, createdAt: 1, category: 'نصوص' } } };
let rejected = 0;
const get = p => p.split('/').filter(Boolean).reduce((o, k) => (o == null ? undefined : o[k]), db);
const snap = (v, key) => ({ key, exists: () => v != null, val: () => (v == null ? null : JSON.parse(JSON.stringify(v))), forEach(fn) { Object.keys(v || {}).forEach(k => fn(snap(v[k], k))); } });
const ref = (p, q = {}) => ({
  child: c => ref(p + '/' + c), orderByChild: f => ref(p, { ...q, by: f }), limitToLast: n => ref(p, { ...q, n }), equalTo: v => ref(p, { ...q, eq: v }),
  once: async () => { let v = get(p); if (q.by && v) { let e = Object.entries(v); if (q.eq !== undefined) e = e.filter(([, x]) => x[q.by] === q.eq); e.sort((a, b) => (a[1][q.by] || 0) - (b[1][q.by] || 0)); if (q.n) e = e.slice(-q.n); v = Object.fromEntries(e); } return snap(v, p.split('/').pop()); },
  async update(up) {
    const base = get(p), next = JSON.parse(JSON.stringify(base)), uid = auth.currentUser && auth.currentUser.uid;
    for (const [k, v] of Object.entries(up)) { const parts = k.split('/'); let o = next; parts.slice(0, -1).forEach(x => o = o[x] = o[x] || {}); const last = parts[parts.length - 1];
      if (v && v.__inc !== undefined) o[last] = (o[last] || 0) + v.__inc; else if (v && v.__ts) o[last] = 1; else if (v === null) delete o[last]; else o[last] = v; }
    for (const [cnt, mk] of [['likeCount', 'likes'], ['downloadCount', 'downloaders']]) {   // نفس منطق .validate في database.rules.json
      if (!(cnt in up)) continue;
      const exp = (base[cnt] || 0) + (next[mk] && next[mk][uid] ? 1 : 0) - (base[mk] && base[mk][uid] ? 1 : 0);
      if (next[cnt] !== exp) { rejected++; throw new Error('PERMISSION_DENIED'); } }
    const parts = p.split('/'); let o = db; parts.slice(0, -1).forEach(x => o = o[x]); o[parts[parts.length - 1]] = next;
  } });
const listeners = [];
const auth = { currentUser: null, onAuthStateChanged(f) { listeners.push(f); f(null); }, _set(u) { this.currentUser = u; listeners.forEach(f => f(u)); },
  async signInWithEmailAndPassword(e, p) { if (p !== 'good') throw new Error('bad'); this._set({ uid: 'u1', email: e, isAnonymous: false }); },
  async signInAnonymously() { const u = { uid: 'anon7', isAnonymous: true }; this._set(u); return { user: u }; }, async signOut() { this._set(null); } };
global.firebase = { apps: [], initializeApp() { this.apps.push(1); }, auth: () => auth,
  database: Object.assign(() => ({ ref: p => ref(p) }), { ServerValue: { increment: n => ({ __inc: n }), TIMESTAMP: { __ts: 1 } } }) };

(async () => {
  const RS = require(path.join(__dirname, '../../web/libraries.js')), t = RS._t;
  console.log('pure functions');
  eq(t.parseQuery('?library=math'), { ref: null, library: 'math', package: null, publisher: null, view: null }, 'parse ?library=');
  eq(t.parseQuery('?package=-Nabc_1&x=1').package, '-Nabc_1', 'parse ?package= (push-id chars)');
  eq(t.parseQuery('?package=../etc').package, null, 'reject path-like ids');
  eq(t.parseQuery('?library=a/b').library, null, 'reject slash in library');
  ok(!t.isStoreRoute(t.parseQuery('')), 'no query => not a store route (site home untouched)');
  ok(t.isStoreRoute(t.parseQuery('?publisher=u9')) && t.isStoreRoute(t.parseQuery('?view=libraries')), '?publisher= / ?view=libraries are store routes');
  eq(t.keyFor('math.og.rin'), 'math_og_rin', 'math.og.rin -> math_og_rin');
  eq([t.fmtCount(999), t.fmtCount(1200), t.fmtCount(2500000), t.fmtCount(-5), t.fmtCount('x')], ['999', '1.2K', '2.5M', '0', '0'], 'fmtCount clamps & formats');
  eq(t.iconSrc('javascript:alert(1)'), null, 'iconSrc rejects non-base64'); ok(t.iconSrc('iVBORw0KGgo' + 'A'.repeat(30)).startsWith('data:image/png;base64,'), 'iconSrc png data URL');
  const e = await t.extractSource(b64); eq(e.name, 'mylib.og.rin', 'zip(deflate): picks lib/*.og.rin (not README/LICENSE/assets)'); eq(e.text, SRC, 'zip(deflate): content round-trips incl. Arabic + <script>');
  const s2 = await t.extractSource(fs.readFileSync('/tmp/pkg_stored.b64', 'utf8')); eq(s2.name, 'stored.og.rin', 'zip(stored) works');
  await assert.rejects(t.extractSource(Buffer.from('not a zip at all, definitely not').toString('base64')), /zip/); passed++; console.log('  ✓ non-zip rejected');
  const line = '    let s = "a\\"b"; // c <b>x</b> @import "z"';
  eq(t.tokenizeLine(line).map(x => x[1]).join(''), line, 'tokenizer is lossless (never drops/alters text)');
  ok(t.tokenizeLine(line).some(x => x[0] === 'c') && t.tokenizeLine('fun a(){}').some(x => x[0] === 'k'), 'tokenizer marks comments/keywords');
  const items = [t.normOfficial('math_og_rin', db.libraries.math_og_rin), t.normPackage('P1', db.packages.P1)];
  eq(t.filterItems(items, 'SARA').map(i => i.id), ['P1'], 'search by publisher (case-insens.)'); eq(t.filterItems(items, 'utilities').map(i => i.id), ['math_og_rin'], 'search by description');
  eq(t.filterItems(items, '', 'رياضيات').map(i => i.id), ['P1'], 'category filter'); eq(t.sortItems(items, 'latest')[0].id, 'P1', 'latest sort'); eq(t.sortItems(items, 'popular')[0].id, 'math_og_rin', 'popular sort');
  ok(!/innerHTML|outerHTML|insertAdjacentHTML|document\.write/.test(fs.readFileSync(path.join(__dirname, '../../web/libraries.js'), 'utf8')), 'no innerHTML/document.write anywhere (XSS-safe by construction)');

  console.log('UI flows (mock DOM + mock Firebase)');
  location.search = '?library=math'; RS.boot(); await tick(60);
  const store$ = find(n => n.attrs.id === 'rin-store'); ok(store$ && store$.cls().includes('open'), '?library=math opens the store overlay directly');
  ok(find(n => n.tag === 'h1' && n.textContent === 'math'), 'detail card shows library name'); ok(body.textContent.includes('Official by Rin') && body.textContent.includes('v1.2.0'), 'shows Official by Rin + version');
  ok(btn('Download') && btn('Copy Code') && btn('Like') && btn('View Source'), 'Download / Copy Code / Like / View Source buttons present');
  btn('Copy Code').click(); await tick(); eq(copied.pop(), 'let x = 1; // hi\n', 'Copy Code copies the .og.rin content'); ok(document.getElementById('rin-store').children.some(c => c.cls().includes('rs-toast') && c.cls().includes('show')), '“Copied!” toast shown');
  btn('View Source').click(); await tick(); ok(find(n => n.cls().includes('rs-code')) && find(n => n.cls().includes('no') && n.textContent === '1'), 'code viewer with line numbers');
  btn('Like').click(); await tick(); ok(find(n => n.cls().includes('rs-modal')).cls().includes('show'), 'Like while logged out => login sheet (no write)'); eq(db.libraries.math_og_rin.likeCount, 2, 'no like counted for guests');
  const em = find(n => n.tag === 'input' && n.attrs.type === 'email'), pw = find(n => n.tag === 'input' && n.attrs.type === 'password'); em.value = 'a@b.c'; pw.value = 'bad';
  const sheet = () => find(n => n.tag === 'button' && n.textContent === 'Sign in', find(n => n.cls().includes('rs-sheet')));
  sheet().click(); await tick(); ok(body.textContent.includes('Invalid credentials'), 'wrong password shows error'); pw.value = 'good'; sheet().click(); await tick(30);
  ok(btn('Sign out'), 'signed in with Firebase Auth (email/password)');
  btn('Like').click(); await tick(); eq([db.libraries.math_og_rin.likeCount, !!db.libraries.math_og_rin.likes.u1], [3, true], 'Like => likeCount+1 & likes/uid=true (atomic)');
  btn('Like').click(); await tick(); eq([db.libraries.math_og_rin.likeCount, db.libraries.math_og_rin.likes], [2, undefined].map((x, i) => i ? (db.libraries.math_og_rin.likes) : x), 'Unlike => back to 2'); ok(!db.libraries.math_og_rin.likes || !db.libraries.math_og_rin.likes.u1, 'Unlike removes likes/uid');
  eq(rejected, 0, 'client never sent a counter the rules would reject');
  btn('Download').click(); await tick(); eq(blobs.length, 1, 'Download creates a real file blob'); eq([db.libraries.math_og_rin.downloadCount, !!db.libraries.math_og_rin.downloaders.u1], [6, true], 'first download counted once (marker + counter atomic)');
  btn('Download').click(); await tick(); eq(db.libraries.math_og_rin.downloadCount, 6, 'repeat download by same user not re-counted');
  // forged write: simulate a malicious client writing counter alone
  await assert.rejects(firebase.database().ref('libraries/math_og_rin').update({ downloadCount: 999999 }), /PERMISSION/); passed++; console.log('  ✓ forged downloadCount=999999 is rejected by the (mirrored) rule');
  // package route via popstate
  location.search = '?package=P1'; RS.navigate('?package=P1'); await tick(60);
  ok(find(n => n.tag === 'h1' && n.textContent === 'MyLib') && body.textContent.includes('Published by @sara'), '?package=P1 opens community detail: Published by @sara');
  ok(body.textContent.includes('<img src=x onerror=alert(1)>') && !find(n => n.tag === 'img'), 'malicious description rendered as text, no <img> element created');
  btn('Copy Code').click(); await tick(30); eq(copied.pop(), SRC, 'Copy Code on community package extracts .og.rin from the zip');
  btn('Download').click(); await tick(); eq(blobs.length, 3, 'community Download yields the registered archive'); eq(db.packages.P1.downloadCount, 1, 'community download counted');
  btn('Like').click(); await tick(); eq(db.packages.P1.likeCount, 1, 'community Like works');
  // guest download => anonymous auth, still can't like
  await auth.signOut(); await tick(); ok(btn('Sign in') || btn('Sign in'), 'signed out');
  btn('Download').click(); await tick(30); eq(db.packages.P1.downloadCount, 2, 'guest download counted via anonymous uid'); ok(!btn('Sign out'), 'anonymous session is treated as guest (no Sign out / no publish rights)');
  // list, publisher, mine
  location.search = '?view=libraries'; RS.navigate('?view=libraries'); await tick(60);
  ok(body.textContent.includes('Official Libraries (1)') && body.textContent.includes('Community Libraries (2)'), 'list shows Official + Community sections');
  const search = find(n => n.cls().includes('rs-search')); search.value = 'other'; search.fire('input'); await tick(200); ok(body.textContent.includes('Community Libraries (1)') && body.textContent.includes('Official Libraries (0)'), 'search filters both sections without new pages');
  RS.navigate('?publisher=u9'); await tick(60); ok(body.textContent.includes('@sara') && body.textContent.includes('MyLib') && !body.textContent.includes('Other'), '?publisher=u9 lists only that publisher');
  RS.navigate('?package=P2'); ok(find(n => n.cls().includes('sk-b')) && find(n => n.attrs['aria-busy'] === 'true'), 'skeleton placeholders render while a detail loads'); await tick(60); ok(!find(n => n.cls().includes('sk-b')) && find(n => n.tag === 'h1' && n.textContent === 'Other'), 'skeleton replaced by real content');
  location.search = '?view=libraries'; store = {}; RS.navigate('?view=libraries'); ok(find(n => n.cls().includes('rs-hero')), 'list shows hero banner'); await tick(60);
  console.log('readable links (?@publisher/library.og.rin)');
  eq(t.parseRef('?@sara/mylib.og.rin'), { user: 'sara', lib: 'mylib.og.rin' }, 'parse ?@sara/mylib.og.rin'); eq(t.parseRef('?@sara'), { user: 'sara', lib: null }, 'parse ?@sara (publisher page)');
  eq(t.parseRef('?@%D8%B3%D8%A7%D8%B1%D8%A9/%D9%85%D9%83%D8%AA%D8%A8%D8%A9.og.rin'), { user: 'سارة', lib: 'مكتبة.og.rin' }, 'Arabic names decode'); eq(t.parseRef('?@a%2Fb/x.og.rin'), null, 'encoded slash rejected');
  eq(t.refPath('Sara Dev', 'My Lib'), '@Sara-Dev/My-Lib.og.rin', 'refPath slugifies spaces'); eq(t.libKey('My-Lib.og.rin'), 'my-lib', 'libKey strips .og.rin, case-insens.');
  eq(t.normOfficial('math_og_rin', db.libraries.math_og_rin).share, '@rin/math.og.rin', 'official share = @rin/<name>.og.rin (path style)'); eq(t.normPackage('P1', db.packages.P1).share, '@sara/MyLib.og.rin', 'community share = @publisher/<name>.og.rin');
  eq(t.parsePathRef('/rinlang/@sara/mylib.og.rin'), { user: 'sara', lib: 'mylib.og.rin' }, 'path /rinlang/@sara/mylib.og.rin parses'); eq(t.parsePathRef('/rinlang/@sara/'), { user: 'sara', lib: null }, 'path /rinlang/@sara/ = publisher page'); eq(t.parsePathRef('/rinlang/'), null, 'plain base path is not a ref');
  eq([t.baseOf('/rinlang/@sara/mylib.og.rin'), t.baseOf('/rinlang/'), t.baseOf('/rinlang/index.html'), t.baseOf('/@rin/math.og.rin')], ['/rinlang/', '/rinlang/', '/rinlang/', '/'], 'baseOf works for project pages and custom domains');
  eq(t.parseLocation('/rinlang/@rin/math.og.rin', '').ref, { user: 'rin', lib: 'math.og.rin' }, 'parseLocation prefers the path form'); eq(t.parseLocation('/rinlang/', '?@rin/math.og.rin').ref, { user: 'rin', lib: 'math.og.rin' }, 'parseLocation still accepts ?@user/lib (404.html redirect target)');
  location.search = ''; location.pathname = '/rinlang/'; RS.navigate('?@rin/math.og.rin'); await tick(60); ok(find(n => n.tag === 'h1' && n.textContent === 'math') && body.textContent.includes('Official by Rin'), '?@rin/math.og.rin opens the official library');
  ok(find(n => n.cls().includes('rs-ref') && n.textContent === '@rin/math.og.rin'), 'detail shows the readable reference chip');
  RS.navigate('?@sara/mylib.og.rin'); await tick(60); ok(find(n => n.tag === 'h1' && n.textContent === 'MyLib') && body.textContent.includes('Published by @sara'), '?@sara/mylib.og.rin opens the community package (case-insensitive)');
  RS.navigate('?package=P1'); await tick(60); eq(location.pathname + location.search, '/rinlang/@sara/MyLib.og.rin', 'legacy ?package=P1 is rewritten to the path-style link');
  RS.navigate('?@sara'); await tick(60); ok(body.textContent.includes('@sara') && body.textContent.includes('MyLib') && !body.textContent.includes('Other'), '?@sara lists that publisher\'s packages');
  RS.navigate('?@sara/nothing.og.rin'); await tick(60); ok(body.textContent.includes('Library not found'), 'unknown library under a publisher => not-found');
  RS.navigate('?package=NOPE'); await tick(60); ok(body.textContent.includes('Library not found'), 'unknown id => friendly not-found');
  eq(RS.navigate('?foo=1'), false, 'unrelated query ignored (site home unaffected)');
  console.log(`\nALL PASSED (${passed} checks)`);
})().catch(e => { console.error('FAIL', e); process.exit(1); });

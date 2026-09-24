/* Rin Library Store — طبقة ويب فوق Firebase تُعرَض داخل نفس تطبيق GitHub Pages (index.html).
 * لا صفحات HTML مستقلة: كل مكتبة تُفتح عبر ?library=NAME أو ?package=ID أو ?publisher=UID.
 * - الرسمية: /libraries (تُنشَر من lib/*.og.rin عبر scripts/publish_libraries_firebase.py)
 * - المجتمع: /packages (نفس بيانات RinPM/RinStudio، بلا نشر ثانٍ)
 * - كل محتوى قادم من Firebase يُدرَج عبر textContent/setAttribute فقط (لا حقن HTML خام بأي بيانات خارجية).
 */
(function (root) {
  'use strict';

  var CFG = {
    firebase: {
      apiKey: 'AIzaSyDY5CTUtM5DgP7hvBIWdvEQ9jqE3lE3vSg',
      authDomain: 'dlof-massage.firebaseapp.com',
      databaseURL: 'https://dlof-massage-default-rtdb.firebaseio.com',
      projectId: 'dlof-massage'
    },
    sdk: 'https://www.gstatic.com/firebasejs/10.12.5/',
    communityLimit: 100
  };

  // ------------------------------- نصوص (عربي/English) -------------------------------
  var T = {
    ar: { title: 'مكتبات Rin', back: 'رجوع', home: 'الموقع', search: 'ابحث بالاسم أو الوصف أو التصنيف أو الناشر…',
      all: 'الكل', official: 'الرسمية', community: 'المجتمع', mine: 'منشوراتي', latest: 'الأحدث', popular: 'الأشهر',
      categories: 'التصنيفات', officialLibs: 'المكتبات الرسمية', communityLibs: 'مكتبات المجتمع', noResults: 'لا نتائج',
      loading: 'جارٍ التحميل…', loadErr: 'تعذّر الاتصال بـ Firebase', notFound: 'لم يتم العثور على هذه المكتبة',
      view: 'عرض', download: 'تحميل', copy: 'نسخ الكود', copied: 'تم النسخ!', like: 'إعجاب', source: 'عرض الكود',
      officialBy: 'رسمية من Rin', publishedBy: 'نشرها', version: 'الإصدار', size: 'الحجم', downloads: 'التحميلات',
      likes: 'الإعجابات', rating: 'التقييم', deps: 'الاعتماديات', date: 'التحديث', category: 'التصنيف', license: 'الترخيص',
      login: 'دخول', logout: 'خروج', email: 'البريد الإلكتروني', password: 'كلمة السر', signin: 'تسجيل الدخول',
      needLogin: 'سجّل الدخول أولاً', authErr: 'بيانات الدخول غير صحيحة', loginHint: 'أنشئ حسابك من تطبيق Rin ثم ادخل هنا.',
      share: 'نسخ الرابط', mineHint: 'تُنشر الحزم من RinStudio (RinPM) وتظهر هنا تلقائياً.', mineLogin: 'سجّل الدخول لعرض منشوراتك.',
      more: 'عرض المزيد', heroT: 'مكتبات Rin', heroS: 'مكتبات رسمية ومن المجتمع — تصفّح، انسخ، حمّل، وشارك رابطاً مباشراً لأي مكتبة.', none: 'لا يوجد', lines: 'سطر', zipErr: 'تعذّر فك الأرشيف — نزّل الحزمة بدلاً من ذلك.', by: 'بواسطة' },
    en: { title: 'Rin Libraries', back: 'Back', home: 'Site', search: 'Search by name, description, category or publisher…',
      all: 'All', official: 'Official', community: 'Community', mine: 'My Published', latest: 'Latest', popular: 'Popular',
      categories: 'Categories', officialLibs: 'Official Libraries', communityLibs: 'Community Libraries', noResults: 'No results',
      loading: 'Loading…', loadErr: 'Could not reach Firebase', notFound: 'Library not found',
      view: 'View', download: 'Download', copy: 'Copy Code', copied: 'Copied!', like: 'Like', source: 'View Source',
      officialBy: 'Official by Rin', publishedBy: 'Published by', version: 'Version', size: 'Size', downloads: 'Downloads',
      likes: 'Likes', rating: 'Rating', deps: 'Dependencies', date: 'Updated', category: 'Category', license: 'License',
      login: 'Sign in', logout: 'Sign out', email: 'Email', password: 'Password', signin: 'Sign in',
      needLogin: 'Please sign in first', authErr: 'Invalid credentials', loginHint: 'Create your account in the Rin app, then sign in here.',
      share: 'Copy link', mineHint: 'Packages published from RinStudio (RinPM) show up here automatically.', mineLogin: 'Sign in to see your published packages.',
      more: 'Show more', heroT: 'Rin Libraries', heroS: 'Official and community libraries — browse, copy, download and share a direct link to any of them.', none: 'None', lines: 'lines', zipErr: 'Could not unpack the archive — download the package instead.', by: 'by' }
  };

  // ------------------------------- دوال نقية (قابلة للاختبار) -------------------------------
  var RE_LIB = /^[A-Za-z0-9_.\-~]{1,100}$/, RE_ID = /^[A-Za-z0-9_\-]{1,128}$/;

  // رابط مقروء: ?@publisher/library.og.rin  (الرسمية: ?@rin/math.og.rin)  —  و ?@publisher لصفحة الناشر
  var RE_REF = /^\?@([^\/&#?]{1,100})(?:\/([^&#?\/]{1,140}))?(?:[&#].*)?$/;
  // رابط Rin الرسمي: /@اسم-المستخدم/اسم-المكتبة.og.rin
  // نُبقي @ جزءاً من المسار، ونضيف .og.rin مرة واحدة فقط.
  function slug(s) { return String(s || '').trim().replace(/^@+/, '').replace(/\s+/g, '-').replace(/-+/g, '-').replace(/^-|-$/g, ''); }
  function libKey(s) { return String(s || '').replace(/\.og\.rin(sdk)?$/i, '').replace(/\s+/g, '-').replace(/-+/g, '-').replace(/^-|-$/g, '').toLowerCase(); }
  function cleanLibName(s) { return slug(String(s || '').replace(/\.og\.rin(sdk)?$/i, '')); }
  function safeSegment(s) { return /^[A-Za-z0-9._~-]{1,100}$/.test(String(s || '')); }
  function refPath(user, lib) { var u = slug(user), l = lib ? cleanLibName(lib) : ''; if (!safeSegment(u) || (l && !safeSegment(l))) return ''; return '@' + encodeURIComponent(u) + (l ? '/' + encodeURIComponent(l) + '.og.rin' : ''); }
  function mkRef(m) {
    if (!m) return null;
    try {
      var u = decodeURIComponent(m[1]), l = m[2] ? decodeURIComponent(m[2]) : null;
      if (/[\u0000-\u001f\/\\]/.test(u + (l || ''))) return null;
      return { user: u, lib: l };
    } catch (e) { return null; }
  }
  function parseRef(search) { return mkRef(RE_REF.exec(search || '')); }
  // المسار الجميل: https://host/rinlang/@user/library.og.rin  (تخدمه 404.html عبر إعادة توجيه إلى ?@user/library.og.rin)
  var RE_PATH = /\/@([^\/?#]{1,100})(?:\/([^\/?#]{1,140}))?\/?$/;
  function parsePathRef(pathname) { return mkRef(RE_PATH.exec(pathname || '')); }
  function baseOf(pathname) {
    var i = String(pathname || '/').indexOf('/@');
    return i >= 0 ? pathname.slice(0, i + 1) : String(pathname || '/').replace(/[^\/]*$/, '');
  }
  function parseLocation(pathname, search) {
    var r = parsePathRef(pathname);
    return r ? { ref: r, library: null, package: null, publisher: null, view: null } : parseQuery(search);
  }
  function parseQuery(search) {
    var ref = parseRef(search);
    if (ref) return { library: null, package: null, publisher: null, view: null, ref: ref };
    var p = new URLSearchParams(search || '');
    function pick(k, re) { var v = p.get(k); return v && re.test(v) ? v : null; }
    return { ref: null, library: pick('library', RE_LIB), package: pick('package', RE_ID), publisher: pick('publisher', RE_ID),
      view: p.get('view') === 'libraries' || p.get('view') === 'mine' ? p.get('view') : null };
  }
  function isStoreRoute(q) { return !!(q.ref || q.library || q.package || q.publisher || q.view); }
  function keyFor(fileName) { return String(fileName).replace(/[.$#\[\]\/]/g, '_'); }
  function num(v) { v = Number(v); return isFinite(v) && v > 0 ? Math.floor(v) : 0; }
  function fmtCount(n) { n = num(n); return n >= 1e6 ? (n / 1e6).toFixed(1).replace(/\.0$/, '') + 'M' : n >= 1e3 ? (n / 1e3).toFixed(1).replace(/\.0$/, '') + 'K' : String(n); }
  function fmtSize(b) { b = num(b); return b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : b >= 1024 ? (b / 1024).toFixed(1) + ' KB' : b + ' B'; }

  function b64ToBytes(b64) {
    var bin = atob(String(b64).replace(/\s+/g, '')), u8 = new Uint8Array(bin.length);
    for (var i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
    return u8;
  }
  function iconSrc(b64) {
    if (typeof b64 !== 'string' || b64.length < 20 || b64.length > 600000 || !/^[A-Za-z0-9+\/=\s]+$/.test(b64)) return null;
    var mime = b64.indexOf('iVBOR') === 0 ? 'image/png' : b64.indexOf('/9j/') === 0 ? 'image/jpeg' : b64.indexOf('UklG') === 0 ? 'image/webp' : null;
    return mime ? 'data:' + mime + ';base64,' + b64.replace(/\s+/g, '') : null;
  }

  // قارئ ZIP صغير: يقرأ الفهرس المركزي ثم يفك deflate-raw بـ DecompressionStream (بلا مكتبات).
  function zipEntries(u8) {
    var dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength), eocd = -1;
    for (var i = u8.length - 22; i >= Math.max(0, u8.length - 65557); i--) if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    if (eocd < 0) throw new Error('not a zip');
    var n = dv.getUint16(eocd + 10, true), off = dv.getUint32(eocd + 16, true), out = [], dec = new TextDecoder();
    for (var k = 0; k < n; k++) {
      if (dv.getUint32(off, true) !== 0x02014b50) break;
      var nl = dv.getUint16(off + 28, true), el = dv.getUint16(off + 30, true), cl = dv.getUint16(off + 32, true);
      out.push({ name: dec.decode(u8.subarray(off + 46, off + 46 + nl)), method: dv.getUint16(off + 10, true),
        csize: dv.getUint32(off + 20, true), lho: dv.getUint32(off + 42, true) });
      off += 46 + nl + el + cl;
    }
    return out;
  }
  function pickSource(entries) {
    var names = entries.filter(function (e) { return !/\/$/.test(e.name) && !/(^|\/)(README|LICENSE)/i.test(e.name); });
    return names.filter(function (e) { return /^lib\/.+\.og\.rin$/.test(e.name); })[0] ||
      names.filter(function (e) { return /\.og\.rin$/.test(e.name); })[0] ||
      names.filter(function (e) { return /\.rin$/.test(e.name); })[0] || null;
  }
  function zipRead(u8, e) {
    var dv = new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
    var start = e.lho + 30 + dv.getUint16(e.lho + 26, true) + dv.getUint16(e.lho + 28, true), data = u8.subarray(start, start + e.csize);
    if (e.method === 0) return Promise.resolve(new TextDecoder().decode(data));
    if (e.method !== 8 || typeof DecompressionStream !== 'function') return Promise.reject(new Error('unsupported'));
    return new Response(new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'))).text();
  }
  function extractSource(b64) {
    try {  // أي خطأ متزامن (base64/zip تالف) يصبح رفضاً للـ Promise كي تعرض الواجهة رسالة بدل استثناء
      var u8 = b64ToBytes(b64), e = pickSource(zipEntries(u8));
      if (!e) return Promise.reject(new Error('no source'));
      return zipRead(u8, e).then(function (text) { return { name: e.name.replace(/^.*\//, ''), text: text }; });
    } catch (err) { return Promise.reject(err); }
  }

  var KW = /^(?:fun|let|make|warp|if|else|for|while|return|import|when|otherwise|match|true|false|null|and|or|not|break|continue|use|rely)$/;
  var TOK = /(\/\/.*$)|("(?:\\.|[^"\\])*"?)|(@[A-Za-z_.]+)|(\b\d+(?:\.\d+)?\b)|(\b[A-Za-z_]\w*\b)/g;
  function tokenizeLine(line) {
    var out = [], last = 0, m;
    TOK.lastIndex = 0;
    while ((m = TOK.exec(line)) !== null) {
      var cls = m[1] ? 'c' : m[2] ? 's' : m[3] ? 'd' : m[4] ? 'n' : KW.test(m[5]) ? 'k' : null;
      if (!cls) continue;
      if (m.index > last) out.push([null, line.slice(last, m.index)]);
      out.push([cls, m[0]]); last = m.index + m[0].length;
    }
    if (last < line.length) out.push([null, line.slice(last)]);
    return out;
  }

  function filterItems(items, term, cat) {
    var t = String(term || '').trim().toLowerCase();
    return items.filter(function (it) {
      if (cat && it.category !== cat) return false;
      return !t || [it.name, it.description, it.category, it.publisher, it.fileName].some(function (f) { return String(f || '').toLowerCase().indexOf(t) >= 0; });
    });
  }
  function sortItems(items, mode) {
    return items.slice().sort(mode === 'popular'
      ? function (a, b) { return (b.likes - a.likes) || (b.downloads - a.downloads) || (b.ts - a.ts); }
      : function (a, b) { return b.ts - a.ts; });
  }

  function normOfficial(key, v) {
    v = v || {};
    return { kind: 'official', id: key, name: String(v.name || key), fileName: String(v.fileName || key), version: String(v.version || '1.0.0'),
      description: String(v.description || ''), category: 'Official', publisher: 'Rin', size: num(v.sizeBytes), downloads: num(v.downloadCount),
      likes: num(v.likeCount), ts: num(v.updatedAt), content: typeof v.content === 'string' ? v.content : '', ref: 'libraries/' + key,
      share: refPath('rin', String(v.name || key)), deps: {}, license: 'MIT', rating: 0, ratingCount: 0 };
  }
  function normPackage(id, v) {
    v = v || {};
    var rc = num(v.ratingCount);
    return { kind: 'community', id: id, name: String(v.name || id), fileName: String(v.fileName || id), version: String(v.version || '1.0.0'),
      description: String(v.description || ''), category: String(v.category || 'عام'), publisher: String(v.publisherName || ''), publisherUsername: String(v.publisherUsername || ''), publisherUid: String(v.publisherUid || ''),
      size: num(v.sizeBytes), downloads: num(v.downloadCount), likes: num(v.likeCount), ts: num(v.createdAt), b64: typeof v.base64Data === 'string' ? v.base64Data : '',
      icon: iconSrc(v.iconBase64), ref: 'packages/' + id, share: (v.publisherUsername || v.publisherName) && v.name ? refPath(String(v.publisherUsername || v.publisherName), String(v.name)) : '?package=' + encodeURIComponent(id),
      deps: v.dependencies && typeof v.dependencies === 'object' ? v.dependencies : {}, license: String(v.license || ''),
      rating: rc ? num(v.ratingSum) / rc : 0, ratingCount: rc };
  }

  // ------------------------------- الحالة والواجهة -------------------------------
  var S = { base: '/', lang: 'ar', theme: 'dark', officialList: null, communityList: null, mineList: null, user: null, tab: 'all', sort: 'latest',
    cat: '', term: '', route: null, el: null, fb: null, fbP: null, likeState: {}, srcCache: {} };
  var $ = {};
  function tr(k) { return (T[S.lang] || T.en)[k] || k; }
  function store(k, v) { try { if (v === undefined) return root.localStorage.getItem(k); root.localStorage.setItem(k, v); } catch (e) { return null; } }

  function h(tag, a, kids) {
    var el = document.createElement(tag);
    a = a || {};
    Object.keys(a).forEach(function (k) {
      if (k === 'class') el.className = a[k];
      else if (k === 'text') el.textContent = a[k];
      else if (k.indexOf('on') === 0) el.addEventListener(k.slice(2), a[k]);
      else if (a[k] !== null && a[k] !== undefined) el.setAttribute(k, a[k]);
    });
    (kids || []).forEach(function (c) { if (c) el.appendChild(typeof c === 'string' ? document.createTextNode(c) : c); });
    return el;
  }
  function clear(el) { while (el.firstChild) el.removeChild(el.firstChild); }
  function toast(msg) {
    $.toast.textContent = msg; $.toast.classList.add('show');
    clearTimeout(toast.t); toast.t = setTimeout(function () { $.toast.classList.remove('show'); }, 1800);
  }

  // ---- Firebase (تحميل كسول للـ SDK عند فتح المتجر فقط) ----
  function loadScript(src) {
    return new Promise(function (res, rej) {
      var s = document.createElement('script'); s.src = src; s.async = true; s.onload = res; s.onerror = function () { rej(new Error('sdk')); };
      document.head.appendChild(s);
    });
  }
  function fbReady() {
    if (S.fbP) return S.fbP;
    S.fbP = (root.firebase ? Promise.resolve() : loadScript(CFG.sdk + 'firebase-app-compat.js')
      .then(function () { return Promise.all([loadScript(CFG.sdk + 'firebase-auth-compat.js'), loadScript(CFG.sdk + 'firebase-database-compat.js')]); }))
      .then(function () {
        var fb = root.firebase;
        if (!fb.apps.length) fb.initializeApp(CFG.firebase);
        S.fb = fb;
        fb.auth().onAuthStateChanged(function (u) { S.user = u && !u.isAnonymous ? u : null; S.mineList = null; S.likeState = {}; renderAccount(); if (S.route && S.route.kind === 'detail') refreshLike(); else if (S.route) render(); });
        return fb;
      });
    S.fbP.catch(function () { S.fbP = null; });
    return S.fbP;
  }
  function dbRef(p) { return S.fb.database().ref(p); }

  function loadOfficial() {
    if (S.officialList) return Promise.resolve(S.officialList);
    return fbReady().then(function () { return dbRef('libraries').once('value'); }).then(function (snap) {
      var v = snap.val() || {}; S.officialList = Object.keys(v).map(function (k) { return normOfficial(k, v[k]); }); return S.officialList;
    });
  }
  function loadCommunity() {
    if (S.communityList) return Promise.resolve(S.communityList);
    return fbReady().then(function () { return dbRef('packages').orderByChild('createdAt').limitToLast(CFG.communityLimit).once('value'); }).then(function (snap) {
      var out = []; snap.forEach(function (c) { out.push(normPackage(c.key, c.val())); }); S.communityList = out; return out;
    });
  }
  function loadByPublisher(uid) {
    return fbReady().then(function () { return dbRef('packages').orderByChild('publisherUid').equalTo(uid).once('value'); }).then(function (snap) {
      var out = []; snap.forEach(function (c) { out.push(normPackage(c.key, c.val())); }); return out;
    });
  }
  function loadByPublisherName(name) {
    function q(n) { return fbReady().then(function () { return dbRef('packages').orderByChild('publisherName').equalTo(n).once('value'); }).then(function (snap) { var out = []; snap.forEach(function (c) { out.push(normPackage(c.key, c.val())); }); return out.filter(function (i) { return slug(i.publisher).toLowerCase() === slug(n).toLowerCase(); }); }); }
    return q(name).then(function (l) { return l.length || name.indexOf('-') < 0 ? l : q(name.replace(/-/g, ' ')); }).then(function (l) {
      if (l.length) return l;
      return loadCommunity().then(function (all) { return all.filter(function (i) { return slug(i.publisher).toLowerCase() === slug(name).toLowerCase(); }); });
    });
  }
  function loadByRef(user, lib) {
    var k = libKey(lib);
    function community() { return loadByPublisherName(user).then(function (l) { return l.filter(function (i) { return libKey(i.name) === k; })[0] || null; }); }
    if (user.toLowerCase() === 'rin') return loadLibraryByName(k).then(function (it) { return it || community(); });
    return community();
  }
  function loadPackage(id) {
    return fbReady().then(function () { return dbRef('packages/' + id).once('value'); }).then(function (s) { return s.exists() ? normPackage(id, s.val()) : null; });
  }
  function loadLibraryByName(name) {
    return loadOfficial().then(function (list) {
      var n = name.toLowerCase().replace(/\.og\.rin$/, '').replace(/_og_rin$/, '');
      return list.filter(function (l) { return l.name.toLowerCase() === n; })[0] || null;
    });
  }

  // ---- التوجيه (query parameters فقط) ----
  function routeFromQuery(q) {
    if (q.ref) return q.ref.lib ? { kind: 'detail', type: 'ref', user: q.ref.user, id: q.ref.lib } : { kind: 'publisher', name: q.ref.user };
    if (q.package) return { kind: 'detail', type: 'package', id: q.package };
    if (q.library) return { kind: 'detail', type: 'library', id: q.library };
    if (q.publisher) return { kind: 'publisher', id: q.publisher };
    return { kind: 'list' };
  }
  function urlFor(r) {
    if (r.kind === 'detail' && r.item && r.item.share) return r.item.share;
    if (r.kind === 'detail' && r.type === 'ref') return refPath(r.user, r.id);
    if (r.kind === 'publisher' && r.name) return refPath(r.name);
    if (r.kind === 'detail') return r.type === 'package' ? '?package=' + encodeURIComponent(r.id) : '?library=' + encodeURIComponent(r.id);
    if (r.kind === 'publisher') return '?publisher=' + encodeURIComponent(r.id);
    return '?view=libraries';
  }
  function go(r, push) {
    S.route = r;
    if (push !== false) { try { root.history.pushState({ rin: 1 }, '', S.base + urlFor(r)); } catch (e) {} }
    S.el.classList.add('open'); document.documentElement.classList.add('rs-lock');
    render();
  }
  function openDetail(it) { go({ kind: 'detail', type: it.kind === 'official' ? 'library' : 'package', id: it.kind === 'official' ? it.name : it.id, item: it }); }
  function canon(r, rel) {
    var to = S.base + rel;
    if (S.route === r && rel && root.location.pathname + root.location.search !== to) { try { root.history.replaceState({ rin: 1 }, '', to); } catch (e) {} }
  }
  function closeStore() {
    S.el.classList.remove('open'); document.documentElement.classList.remove('rs-lock');
    try { root.history.pushState({}, '', S.base); } catch (e) {}
  }
  function navigate(url) {
    var q = parseQuery(String(url).replace(/^[^?]*/, ''));
    if (!isStoreRoute(q)) return false;
    mount(); go(routeFromQuery(q)); return true;
  }
  function onPop() {
    var q = parseLocation(root.location.pathname, root.location.search);
    if (isStoreRoute(q)) { mount(); go(routeFromQuery(q), false); }
    else if (S.el) { S.el.classList.remove('open'); document.documentElement.classList.remove('rs-lock'); }
  }

  // ---- رسم ----
  function mount() {
    if (S.el) return;
    S.lang = store('rin.store.lang') || (document.documentElement.lang === 'en' ? 'en' : 'ar');
    S.theme = store('rin.store.theme') || (root.matchMedia && root.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark');
    S.el = h('div', { id: 'rin-store' });
    $.top = h('header', { class: 'rs-top' }); $.body = h('main', { class: 'rs-main' });
    $.toast = h('div', { class: 'rs-toast', role: 'status' }); $.modal = h('div', { class: 'rs-modal' });
    S.el.appendChild($.top); S.el.appendChild($.body); S.el.appendChild($.toast); S.el.appendChild($.modal);
    document.body.appendChild(S.el); applyLocale();
  }
  function applyLocale() {
    S.el.setAttribute('dir', S.lang === 'ar' ? 'rtl' : 'ltr'); S.el.setAttribute('lang', S.lang); S.el.setAttribute('data-theme', S.theme);
  }
  function renderTop() {
    clear($.top);
    var acct = h('div', { class: 'rs-acct' }); $.acct = acct;
    $.top.appendChild(h('button', { class: 'rs-btn ghost', text: S.lang === 'ar' ? '→' : '←', 'aria-label': S.route && S.route.kind !== 'list' ? tr('back') : tr('home'), title: S.route && S.route.kind !== 'list' ? tr('back') : tr('home'),
      onclick: function () { if (S.route && S.route.kind !== 'list') go({ kind: 'list' }); else closeStore(); } }));
    $.top.appendChild(h('div', { class: 'rs-brand', text: '⧉ ' + tr('title') }));
    $.top.appendChild(h('button', { class: 'rs-btn ghost', 'aria-label': 'language', text: S.lang === 'ar' ? 'EN' : 'ع',
      onclick: function () { S.lang = S.lang === 'ar' ? 'en' : 'ar'; store('rin.store.lang', S.lang); applyLocale(); render(); } }));
    $.top.appendChild(h('button', { class: 'rs-btn ghost', 'aria-label': 'theme', text: S.theme === 'dark' ? '☀' : '☾',
      onclick: function () { S.theme = S.theme === 'dark' ? 'light' : 'dark'; store('rin.store.theme', S.theme); applyLocale(); renderTop(); } }));
    $.top.appendChild(acct); renderAccount();
  }
  function renderAccount() {
    if (!$.acct) return; clear($.acct);
    $.acct.appendChild(S.user
      ? h('button', { class: 'rs-btn ghost', title: S.user.email || '', text: tr('logout'), onclick: function () { S.fb.auth().signOut(); } })
      : h('button', { class: 'rs-btn', text: tr('login'), onclick: openLogin }));
  }
  function stat(icon, val) { return h('span', { class: 'rs-stat', text: icon + ' ' + val }); }
  function badge(it) { return h('span', { class: 'rs-badge ' + it.kind, text: it.kind === 'official' ? tr('official') : tr('community') }); }
  var EMOJI = { math: '🧮', strings: '🔤', colors: '🎨', csv: '📊', data: '🗂', graph: '🕸', geometry: '📐', physics: '⚛', http: '🌐', httpkit: '🌐', json: '🧾', jsonkit: '🧾', logger: '📝', animation: '🎞', matrix: '▦', queue: '⏳', router: '🧭', search: '🔎' };
  function iconEl(it) {
    if (it.icon) return h('img', { class: 'rs-ico', src: it.icon, alt: '', loading: 'lazy' });
    return h('div', { class: 'rs-ico txt', text: it.kind === 'official' ? (EMOJI[it.name.toLowerCase()] || '📦') : (it.name.charAt(0) || '?').toUpperCase() });
  }
  function card(it) {
    return h('article', { class: 'rs-card', tabindex: '0', onclick: function () { openDetail(it); } }, [
      h('div', { class: 'rs-card-h' }, [iconEl(it), h('div', { class: 'rs-card-t' }, [h('h3', { text: it.name }), badge(it)])]),
      h('p', { class: 'rs-desc', text: it.description || '—' }),
      h('div', { class: 'rs-stats' }, [h('span', { class: 'rs-stat', text: 'v' + it.version }), stat('↓', fmtCount(it.downloads)), stat('♥', fmtCount(it.likes))]),
      h('div', { class: 'rs-actions' }, [
        h('button', { class: 'rs-btn', text: tr('view'), onclick: function (e) { e.stopPropagation(); openDetail(it); } }),
        h('button', { class: 'rs-btn ghost', text: '↓', 'aria-label': tr('download'), onclick: function (e) { e.stopPropagation(); download(it); } })
      ])
    ]);
  }
  // ---- هياكل التحميل (skeleton) ----
  function bar(cls) { return h('div', { class: 'sk-b ' + cls }); }
  function skelCards(n) {
    var g = h('div', { class: 'rs-grid' });
    for (var i = 0; i < n; i++) g.appendChild(h('div', { class: 'rs-card sk', 'aria-hidden': 'true' }, [
      h('div', { class: 'rs-card-h' }, [h('div', { class: 'rs-ico sk-b' }), h('div', { class: 'rs-card-t sk-grow' }, [bar('sk-l w60'), bar('sk-l w30')])]),
      bar('sk-l w100'), bar('sk-l w80'), h('div', { class: 'rs-stats' }, [bar('sk-l w30'), bar('sk-l w20')]), bar('sk-btn')]));
    return g;
  }
  function skelSection(n) { return h('section', { 'aria-busy': 'true' }, [bar('sk-h2'), skelCards(n)]); }
  function skelDetail() {
    return h('article', { class: 'rs-detail sk', 'aria-busy': 'true' }, [
      h('div', { class: 'rs-dmain' }, [h('div', { class: 'rs-card-h big' }, [h('div', { class: 'rs-ico sk-b' }), h('div', { class: 'rs-card-t sk-grow' }, [bar('sk-l w60'), bar('sk-l w30')])]),
        bar('sk-l w100'), bar('sk-l w90'), bar('sk-l w50'), h('div', { class: 'rs-actions wrap' }, [bar('sk-btn'), bar('sk-btn'), bar('sk-btn')])]),
      h('aside', { class: 'rs-dside' }, [bar('sk-l w100 tall'), bar('sk-l w100 tall'), bar('sk-l w100 tall'), bar('sk-l w100 tall')])]);
  }

  function section(title, items, note) {
    var grid = h('div', { class: 'rs-grid' });
    items.forEach(function (it) { grid.appendChild(card(it)); });
    return h('section', {}, [h('h2', { class: 'rs-h2', text: title + ' (' + items.length + ')' }), note ? h('p', { class: 'rs-note', text: note }) : null, items.length ? grid : h('p', { class: 'rs-note', text: tr('noResults') })]);
  }

  function render() {
    if (!S.el) return; renderTop(); clear($.body);
    var r = S.route || { kind: 'list' };
    if (r.kind === 'detail') return renderDetail(r);
    if (r.kind === 'publisher') return renderPublisher(r);
    renderList();
  }
  function renderList() {
    var searchEl = h('input', { class: 'rs-search', type: 'search', value: S.term, placeholder: tr('search'), 'aria-label': tr('search'),
      oninput: function (e) { S.term = e.target.value; clearTimeout(renderList.t); renderList.t = setTimeout(fill, 120); } });
    var tabs = h('div', { class: 'rs-chips' }), cats = h('div', { class: 'rs-chips' }), out = h('div', { class: 'rs-results' });
    $.body.appendChild(h('div', { class: 'rs-hero' }, [h('div', { class: 'rs-hero-t', text: tr('heroT') }), h('p', { class: 'rs-hero-s', text: tr('heroS') })]));
    $.body.appendChild(h('div', { class: 'rs-tools' }, [searchEl, tabs, cats])); $.body.appendChild(out);
    function chip(box, label, on, fn) { box.appendChild(h('button', { class: 'rs-chip' + (on ? ' on' : ''), text: label, onclick: fn })); }
    function fill() {
      clear(tabs); clear(cats); clear(out);
      [['all', 'all'], ['official', 'official'], ['community', 'community']].concat(S.user ? [['mine', 'mine']] : []).forEach(function (t) { chip(tabs, tr(t[1]), S.tab === t[0], function () { S.tab = t[0]; fill(); }); });
      tabs.appendChild(h('span', { class: 'rs-sep' }));
      [['latest', 'latest'], ['popular', 'popular']].forEach(function (t) { chip(tabs, tr(t[1]), S.sort === t[0], function () { S.sort = t[0]; fill(); }); });
      var all = (S.officialList || []).concat(S.communityList || []), seen = {};
      chip(cats, tr('categories') + ': ' + tr('all'), !S.cat, function () { S.cat = ''; fill(); });
      all.forEach(function (i) { if (!seen[i.category]) { seen[i.category] = 1; chip(cats, i.category, S.cat === i.category, function () { S.cat = S.cat === i.category ? '' : i.category; fill(); }); } });
      if (S.tab === 'mine') {
        if (!S.user) { out.appendChild(h('p', { class: 'rs-note', text: tr('mineLogin') })); return; }
        if (!S.mineList) { out.appendChild(skelSection(3)); loadByPublisher(S.user.uid).then(function (l) { S.mineList = l; if (S.route.kind === 'list') fill(); }); return; }
        out.appendChild(section(tr('mine'), sortItems(filterItems(S.mineList, S.term, S.cat), S.sort), tr('mineHint'))); return;
      }
      if (!S.officialList && !S.communityList) { out.appendChild(skelSection(6)); out.appendChild(skelSection(3)); return; }
      if (S.tab !== 'community') out.appendChild(section(tr('officialLibs'), sortItems(filterItems(S.officialList || [], S.term, S.cat), S.sort)));
      if (S.tab !== 'official') out.appendChild(section(tr('communityLibs'), sortItems(filterItems(S.communityList || [], S.term, S.cat), S.sort)));
    }
    fill();
    Promise.all([loadOfficial(), loadCommunity()]).then(function () { if (S.route && S.route.kind === 'list') fill(); })
      .catch(function () { clear(out); out.appendChild(h('p', { class: 'rs-note err', text: tr('loadErr') })); });
  }
  function renderPublisher(r) {
    $.body.appendChild(skelSection(3));
    (r.name && !r.id ? loadByPublisherName(r.name) : loadByPublisher(r.id)).then(function (list) {
      if (S.route !== r) return;
      clear($.body); var nm = list[0] ? list[0].publisher : (r.name || r.id);
      if (nm && (list[0] || r.name)) canon(r, refPath(nm));
      $.body.appendChild(h('h1', { class: 'rs-h1', text: '@' + nm }));
      $.body.appendChild(section(tr('communityLibs'), sortItems(list, S.sort)));
    }).catch(function () { clear($.body); $.body.appendChild(h('p', { class: 'rs-note err', text: tr('loadErr') })); });
  }
  function renderDetail(r) {
    if (r.item) return drawDetail(r.item);
    $.body.appendChild(skelDetail());
    (r.type === 'ref' ? loadByRef(r.user, r.id) : r.type === 'package' ? loadPackage(r.id) : loadLibraryByName(r.id)).then(function (it) {
      if (S.route !== r) return; clear($.body);
      if (!it) { $.body.appendChild(h('p', { class: 'rs-note err', text: tr('notFound') })); return; }
      r.item = it; canon(r, it.share); drawDetail(it);
    }).catch(function () { clear($.body); $.body.appendChild(h('p', { class: 'rs-note err', text: tr('loadErr') })); });
  }
  function row(k, v) { return h('div', { class: 'rs-row' }, [h('span', { class: 'k', text: tr(k) }), v instanceof Node ? v : h('span', { class: 'v', text: v })]); }
  function drawDetail(it) {
    var likeBtn = h('button', { class: 'rs-btn like', id: 'rs-like', onclick: function () { toggleLike(it); } });
    var srcBox = h('div', { class: 'rs-src' });
    var by = it.kind === 'official' ? h('span', { class: 'v', text: tr('officialBy') })
      : h('a', { class: 'v link', href: S.base + (it.publisher ? refPath(it.publisher) : '?publisher=' + encodeURIComponent(it.publisherUid)), text: tr('publishedBy') + ' @' + (it.publisher || '—'),
        onclick: function (e) { e.preventDefault(); go({ kind: 'publisher', id: it.publisherUid, name: it.publisher }); } });
    var deps = Object.keys(it.deps || {}).map(function (k) { return k + ' ' + it.deps[k]; }).join(', ') || tr('none');
    var rating = it.ratingCount ? '★ ' + it.rating.toFixed(1) + ' (' + it.ratingCount + ')' : tr('none');
    $.body.appendChild(h('article', { class: 'rs-detail' }, [
      h('div', { class: 'rs-dmain' }, [
      h('div', { class: 'rs-card-h big' }, [iconEl(it), h('div', { class: 'rs-card-t' }, [h('h1', { class: 'rs-h1', text: it.name }), badge(it), by,
        h('button', { class: 'rs-ref', type: 'button', title: tr('share'), text: decodeURIComponent(it.share.replace(/^\?/, '')), dir: 'ltr', onclick: function () { copyText(root.location.origin + S.base + it.share); } })])]),
      h('p', { class: 'rs-desc full', text: it.description || '—' }),
      h('div', { class: 'rs-actions wrap' }, [
        h('button', { class: 'rs-btn', text: '↓ ' + tr('download'), onclick: function () { download(it); } }),
        h('button', { class: 'rs-btn', text: '⧉ ' + tr('copy'), onclick: function () { getSource(it).then(function (s) { copyText(s.text); }).catch(function () { toast(tr('zipErr')); }); } }),
        likeBtn,
        h('button', { class: 'rs-btn', text: '</> ' + tr('source'), onclick: function () { showSource(it, srcBox); } }),
        h('button', { class: 'rs-btn ghost', text: '🔗 ' + tr('share'), onclick: function () { copyText(root.location.origin + S.base + it.share); } })
      ]),
      srcBox
      ]),
      h('aside', { class: 'rs-dside' }, [h('div', { class: 'rs-rows' }, [
        row('version', 'v' + it.version), row('category', it.category), row('size', fmtSize(it.size)),
        row('downloads', h('span', { class: 'v', id: 'rs-dl', text: fmtCount(it.downloads) })), row('likes', h('span', { class: 'v', id: 'rs-lk', text: fmtCount(it.likes) })),
        row('rating', rating), row('deps', deps), row('license', it.license || '—'),
        row('date', it.ts ? new Date(it.ts).toLocaleDateString(S.lang === 'ar' ? 'ar' : 'en') : '—')
      ])])
    ]));
    refreshLike();
  }

  // ---- الكود ----
  function getSource(it) {
    if (it.kind === 'official') return Promise.resolve({ name: it.fileName, text: it.content });
    if (S.srcCache[it.id]) return Promise.resolve(S.srcCache[it.id]);
    return extractSource(it.b64).then(function (s) { S.srcCache[it.id] = s; return s; });
  }
  function showSource(it, box) {
    if (box.firstChild) { clear(box); return; }
    box.appendChild(h('div', { class: 'rs-viewer sk', 'aria-busy': 'true' }, [bar('sk-l w40 tall'), bar('sk-l w90'), bar('sk-l w70'), bar('sk-l w80'), bar('sk-l w60')]));
    getSource(it).then(function (s) { clear(box); box.appendChild(codeViewer(s)); }).catch(function () { clear(box); box.appendChild(h('p', { class: 'rs-note err', text: tr('zipErr') })); });
  }
  function codeViewer(s) {
    var lines = s.text.replace(/\r\n?/g, '\n').split('\n'), shown = 0, CH = 400, body = h('pre', { class: 'rs-code', dir: 'ltr' }), more = h('button', { class: 'rs-btn ghost', text: tr('more') });
    function chunk() {
      var frag = document.createDocumentFragment(), end = Math.min(lines.length, shown + CH);
      for (var i = shown; i < end; i++) {
        var code = h('code');
        tokenizeLine(lines[i]).forEach(function (t) { code.appendChild(t[0] ? h('span', { class: 't-' + t[0], text: t[1] }) : document.createTextNode(t[1])); });
        frag.appendChild(h('div', { class: 'ln' }, [h('span', { class: 'no', text: String(i + 1) }), code]));
      }
      body.appendChild(frag); shown = end; more.style.display = shown < lines.length ? '' : 'none';
    }
    more.addEventListener('click', chunk); chunk();
    return h('div', { class: 'rs-viewer' }, [
      h('div', { class: 'rs-vbar' }, [h('span', { class: 'rs-vname', text: s.name + ' · ' + lines.length + ' ' + tr('lines') }),
        h('button', { class: 'rs-btn ghost', text: tr('copy'), onclick: function () { copyText(s.text); } }),
        h('button', { class: 'rs-btn ghost', text: tr('download'), onclick: function () { saveBlob(new Blob([s.text], { type: 'text/plain;charset=utf-8' }), s.name); } })]),
      body, more]);
  }
  function copyText(text) {
    function done() { toast(tr('copied')); }
    if (root.navigator.clipboard && root.isSecureContext) { root.navigator.clipboard.writeText(text).then(done, fallback); } else fallback();
    function fallback() {
      var ta = h('textarea', { style: 'position:fixed;opacity:0;top:0' }); ta.value = text; document.body.appendChild(ta); ta.select();
      try { document.execCommand('copy'); done(); } catch (e) {} document.body.removeChild(ta);
    }
  }
  function saveBlob(blob, name) {
    var a = h('a', { href: URL.createObjectURL(blob), download: name }); document.body.appendChild(a); a.click();
    setTimeout(function () { URL.revokeObjectURL(a.href); document.body.removeChild(a); }, 1000);
  }

  // ---- تحميل / إعجاب (العدّادات تتغير فقط عبر multi-path update تتحقق منه Firebase Rules) ----
  function download(it) {
    try {
      if (it.kind === 'official') saveBlob(new Blob([it.content], { type: 'text/plain;charset=utf-8' }), it.fileName);
      else saveBlob(new Blob([b64ToBytes(it.b64)], { type: 'application/zip' }), it.fileName);
    } catch (e) { toast(tr('zipErr')); return; }
    countDownload(it);
  }
  function countDownload(it) {
    fbReady().then(function (fb) {
      var a = fb.auth(); return a.currentUser ? a.currentUser : a.signInAnonymously().then(function (c) { return c.user; });
    }).then(function (u) {
      var base = dbRef(it.ref);
      return base.child('downloaders/' + u.uid).once('value').then(function (m) {
        if (m.exists()) return;
        var up = {}; up['downloaders/' + u.uid] = S.fb.database.ServerValue.TIMESTAMP; up.downloadCount = S.fb.database.ServerValue.increment(1);
        return base.update(up).then(function () { it.downloads++; var e = document.getElementById('rs-dl'); if (e) e.textContent = fmtCount(it.downloads); });
      });
    }).catch(function () { /* التحميل نفسه تمّ؛ العدّ اختياري (يتطلب Anonymous Auth مفعّلاً) */ });
  }
  function refreshLike() {
    var btn = document.getElementById('rs-like'), r = S.route; if (!btn || !r || !r.item) return;
    var it = r.item, liked = !!S.likeState[it.ref];
    function paint() { btn.textContent = (S.likeState[it.ref] ? '♥ ' : '♡ ') + tr('like'); btn.classList.toggle('on', !!S.likeState[it.ref]); var e = document.getElementById('rs-lk'); if (e) e.textContent = fmtCount(it.likes); }
    paint();
    if (S.user && !(it.ref in S.likeState)) dbRef(it.ref + '/likes/' + S.user.uid).once('value').then(function (s) { S.likeState[it.ref] = s.exists(); paint(); }).catch(function () {});
    return liked;
  }
  function toggleLike(it) {
    if (!S.user) { toast(tr('needLogin')); openLogin(); return; }
    var was = !!S.likeState[it.ref], up = {}, uid = S.user.uid;
    up['likes/' + uid] = was ? null : true; up.likeCount = S.fb.database.ServerValue.increment(was ? -1 : 1);
    S.likeState[it.ref] = !was; it.likes = Math.max(0, it.likes + (was ? -1 : 1)); refreshLike();
    dbRef(it.ref).update(up).catch(function () { S.likeState[it.ref] = was; it.likes = Math.max(0, it.likes + (was ? 1 : -1)); refreshLike(); toast(tr('loadErr')); });
  }

  // ---- تسجيل الدخول (نفس Firebase Auth بالبريد/كلمة السر — الإنشاء من تطبيق Rin) ----
  function openLogin() {
    clear($.modal);
    var em = h('input', { type: 'email', autocomplete: 'email', placeholder: tr('email'), 'aria-label': tr('email') });
    var pw = h('input', { type: 'password', autocomplete: 'current-password', placeholder: tr('password'), 'aria-label': tr('password') });
    var err = h('p', { class: 'rs-note err' });
    function submit() {
      err.textContent = '';
      fbReady().then(function (fb) { return fb.auth().signInWithEmailAndPassword(em.value.trim(), pw.value); })
        .then(function () { $.modal.classList.remove('show'); }).catch(function () { err.textContent = tr('authErr'); });
    }
    pw.addEventListener('keydown', function (e) { if (e.key === 'Enter') submit(); });
    $.modal.appendChild(h('div', { class: 'rs-sheet' }, [h('h2', { class: 'rs-h2', text: tr('signin') }), em, pw, err,
      h('p', { class: 'rs-note', text: tr('loginHint') }),
      h('div', { class: 'rs-actions' }, [h('button', { class: 'rs-btn', text: tr('signin'), onclick: submit }), h('button', { class: 'rs-btn ghost', text: tr('back'), onclick: function () { $.modal.classList.remove('show'); } })])]));
    $.modal.classList.add('show'); em.focus();
  }

  // ---- نقطة الدخول ----
  function boot() {
    S.base = baseOf(root.location.pathname);
    var q = parseLocation(root.location.pathname, root.location.search);
    root.addEventListener('popstate', onPop);
    if (isStoreRoute(q)) { mount(); go(routeFromQuery(q), false); }
  }
  var api = { open: function () { mount(); go({ kind: 'list' }); }, navigate: navigate, boot: boot,
    _t: { parseQuery: parseQuery, isStoreRoute: isStoreRoute, keyFor: keyFor, fmtCount: fmtCount, fmtSize: fmtSize, iconSrc: iconSrc, extractSource: extractSource,
      zipEntries: zipEntries, pickSource: pickSource, tokenizeLine: tokenizeLine, filterItems: filterItems, sortItems: sortItems, normOfficial: normOfficial, normPackage: normPackage, b64ToBytes: b64ToBytes, urlFor: urlFor, routeFromQuery: routeFromQuery, parseRef: parseRef, parsePathRef: parsePathRef, parseLocation: parseLocation, baseOf: baseOf, refPath: refPath, libKey: libKey, slug: slug } };
  root.RinStore = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else if (root.document) { if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot); else boot(); }
})(typeof window !== 'undefined' ? window : globalThis);

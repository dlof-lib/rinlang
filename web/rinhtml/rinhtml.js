/*!
 * RinHTML — تشغيل ملفات .rin مباشرة داخل HTML، بدون كتابة JavaScript.
 *
 *   HTML  → واجهة عرض فقط
 *   Rin   → منطق التطبيق (يعمل عبر محرك Rin C++ المُصرَّف WebAssembly)
 *   هذا الملف → الجسر الوحيد بينهما (لا يعيد تنفيذ اللغة، فقط ينقل الأحداث/الحالة)
 *
 * الاستخدام الأدنى:
 *   <script src="rinhtml.js"></script>
 *   <rin-app src="main.rin" allow="dom"></rin-app>
 *   <button rin-click="add()">+</button>
 *   <div rin-text="count"></div>
 *
 * main.rin:
 *   let count = 0;
 *   fun add() { count = count + 1; }
 *
 * يعتمد على محرك WASM مبني من web/rinhtml_bridge.cpp (انظر build_rinhtml_wasm.sh)، والذي يُصدِّر:
 *   rinhtml_create, rinhtml_last_error, rinhtml_boot_output,
 *   rinhtml_get_globals, rinhtml_call, rinhtml_set_global, rinhtml_free_session
 *
 * تخزين محلي (allow="storage") — حالة Rin الدائمة داخل المتصفح (الوضع الليلي، العدّادات…):
 *   <rin-app src="main.rin" allow="dom storage" persist="theme count"></rin-app>
 *   المتغيّرات العامة المسمّاة في persist تُحفَظ في localStorage بعد كل تغيير وتُستعاد قبل أول رسم (بنفس النوع فقط).
 *   مفتاح التخزين: persist-key="..." (الافتراضي rinhtml:<id أو src>)، ويمكن persist-store="session" لتخزين الجلسة.
 *
 * جلب في الخلفية (allow="fetch") — يحدّث الجزء المرتبط وحده:
 *   <span rin-fetch="/api/rates" rin-pick="rates.EUR" rin-into="eur" rin-every="30s" rin-text="eur"></span>
 *   <input rin-fetch="/api/search?q={q}" rin-fetch-on="input" rin-debounce="250" rin-into="results" rin-model="q">
 *   <ul rin-each="results"><template><li rin-field="name"></li></template></ul>
 *   rin-into: اسم النتيجة (قيمة بسيطة ⇒ تُكتب أيضاً في متغيّر Rin العام بنفس الاسم؛ كائن/مصفوفة ⇒ app.getData(name))
 *   rin-pick: مسار منقّط داخل JSON · rin-every: تكرار دوري (30s/1m، يتوقف والتبويب مخفي) · rin-fetch-on: حدث يطلق الجلب
 *   rin-debounce: مهلة بالميلي ثانية · rin-error: متغيّر يستقبل نص الخطأ · طلب أحدث يلغي الأقدم.
 *   في كل رسم لا يُكتب في الـ DOM إلا ما تغيّرت قيمته (rin-text/rin-show/rin-each).
 *
 * أيقونة الموقع (favicon): إن عرّف main.rin حاوية @sticker (أو @container.sticker) بحقل icon،
 * تُستخدم تلقائياً كـ<link rel="icon"> للصفحة الحالية — بلا أي إعداد إضافي. مثال:
 *   @sticker=app  text icon = "icon.png";  .end/sticker
 * (نفس المنطق تماماً، بصيغة Kotlin، في RinStickerIcon.kt — يُستخدم هناك لأيقونة تصدير APK.)
 */
(function (global) {
  'use strict';

  // ===========================================================================================
  // 1) rinhtml-security — الصلاحيات المسموحة لكل تطبيق (allow="dom storage fetch")
  // ===========================================================================================
  var ALL_CAPS = ['dom', 'storage', 'fetch'];

  function parseAllow(attrValue) {
    var granted = {};
    ALL_CAPS.forEach(function (c) { granted[c] = false; });
    if (!attrValue) return granted; // بلا allow= إطلاقاً = بلا أي صلاحية (Sandbox كامل، آمن افتراضياً)
    attrValue.trim().split(/\s+/).forEach(function (tok) {
      if (ALL_CAPS.indexOf(tok) !== -1) granted[tok] = true;
    });
    return granted;
  }

  function requireCap(app, cap, what) {
    if (!app.allow[cap]) {
      console.warn('[RinHTML] "' + what + '" يتطلب صلاحية "' + cap +
        '" غير الممنوحة لهذا <rin-app> (أضف allow="' + cap + '" لتفعيلها).');
      return false;
    }
    return true;
  }

  // ===========================================================================================
  // 2) rinhtml-loader — تحميل نص .rin ومحرك WASM (مرة واحدة، مشتركة بين كل التطبيقات في الصفحة)
  // ===========================================================================================
  var enginePromise = null;

  function loadEngine() {
    if (enginePromise) return enginePromise;
    enginePromise = new Promise(function (resolve, reject) {
      if (typeof RinHTMLEngine !== 'function') {
        reject(new Error(
          'محرك Rin (rin_engine.js) غير محمَّل. تأكد من تضمين <script src="rin_engine.js"></script> ' +
          'قبل rinhtml.js — انظر web/build_rinhtml_wasm.sh لبناء rin_engine.js/.wasm من rinhtml_bridge.cpp.'
        ));
        return;
      }
      RinHTMLEngine().then(resolve, reject);
    });
    return enginePromise;
  }

  function fetchSource(src) {
    return fetch(src, { cache: 'no-store' }).then(function (res) {
      if (!res.ok) throw new Error('تعذّر جلب ' + src + ' (HTTP ' + res.status + ')');
      return res.text();
    });
  }

  // ===========================================================================================
  // 2ب) rinhtml-sticker-icon — حاوية @sticker/@container.sticker تصبح مسؤولة تلقائياً عن أيقونة
  // الموقع (favicon)، بنفس التوأم بالضبط بلغة Kotlin: RinStickerIcon.kt (استخدامه لأيقونة تصدير
  // APK). كلا الطرفين يقرأان نفس الحقل الموثَّق في rin_ast.h (سطح "sticker"):
  //   @sticker=name  text icon = "path/to/icon.png";  ...  .end/sticker
  // مسح نصّي خفيف على مصدر .rin كما جُلب (لا حاجة لتشغيل المحرّك WASM أولاً)، فيُطبَّق فوراً بمجرد
  // جلب الملف — قبل انتظار boot الجلسة — حتى تظهر الأيقونة بأسرع ما يمكن أثناء تحميل الصفحة.
  // ===========================================================================================
  var STICKER_BLOCK_RE = /@(?:container\.)?sticker\s*=\s*"?[A-Za-z_][\w.]*"?[\s\S]*?\.end\/(?:container\.)?sticker/i;
  var ICON_FIELD_RE = /\bicon\s*=\s*"((?:\\.|[^"\\])*)"/;

  function findStickerIconPath(source) {
    var block = STICKER_BLOCK_RE.exec(source || '');
    if (!block) return null;
    var m = ICON_FIELD_RE.exec(block[0]);
    if (!m) return null;
    var raw = m[1].replace(/\\"/g, '"').replace(/\\\\/g, '\\').trim();
    return raw || null;
  }

  // يضبط <link rel="icon"> في <head> الصفحة الحالية، نسبةً إلى [baseSrc] (مسار main.rin نفسه)
  // إن كانت الأيقونة نسبية — بنفس منطق fetch العادي للمتصفح. لا يفعل شيئاً إن لم توجد أيقونة sticker
  // (يترك أي favicon معرَّف يدوياً في الصفحة كما هو).
  function applyStickerFavicon(source, baseSrc) {
    var path = findStickerIconPath(source);
    if (!path || typeof document === 'undefined') return;
    var href = baseSrc ? new URL(path, new URL(baseSrc, document.baseURI)).href : path;
    var link = document.querySelector('link[rel~="icon"]');
    if (!link) {
      link = document.createElement('link');
      link.rel = 'icon';
      document.head.appendChild(link);
    }
    link.href = href;
  }

  // ===========================================================================================
  // 3) rinhtml-runtime — جلسة تطبيق واحدة: تُنشأ من نص .rin وتُبقي الحالة والدوال حيّة
  // ===========================================================================================
  function App(Module, sessionId, opts) {
    this.Module = Module;
    this.sessionId = sessionId;
    this.allow = opts.allow || parseAllow(null);
    this.root = opts.root || document;
    this.src = opts.src || null;
    this.globals = {};
    this._listeners = {};   // rinhtml.on/emit
    this._bound = [];       // عناصر DOM المربوطة حالياً (لأجل unmount)
    this.data = {};         // نتائج rin-fetch غير البسيطة (كائنات/مصفوفات): app.getData(name)
    this._persist = (opts.persist || []).slice();
    this._storeArea = opts.persistStore === 'session' ? 'sessionStorage' : 'localStorage';
    this._storeKey = opts.persistKey || ('rinhtml:' + (opts.id || opts.src || 'app'));
    this._timers = [];      // مؤقّتات/مستمعات الجلب (لأجل unmount)
    this._refreshGlobals();
  }

  App.prototype._refreshGlobals = function () {
    var json = this.Module.ccall('rinhtml_get_globals', 'string', ['number'], [this.sessionId]);
    try { this.globals = JSON.parse(json); } catch (e) { this.globals = {}; }
  };

  // call(fnName, ...args): يستدعي دالة Rin علوية حقيقية، يحدِّث الحالة، ثم يعيد رسم الروابط.
  App.prototype.call = function (fnName) {
    var args = Array.prototype.slice.call(arguments, 1);
    var argsJson = JSON.stringify(args);
    var resJson = this.Module.ccall(
      'rinhtml_call', 'string',
      ['number', 'string', 'string'],
      [this.sessionId, fnName, argsJson]
    );
    var res;
    try { res = JSON.parse(resJson); } catch (e) { res = { ok: false, error: 'رد غير صالح من المحرك' }; }
    if (res.globals) this.globals = res.globals;
    if (!res.ok) {
      console.error('[RinHTML] خطأ أثناء تنفيذ "' + fnName + '()": ' + res.error);
      this.emit('error', { fn: fnName, message: res.error });
    } else {
      this._persistSave();
      this._render();
      this.emit('update', { fn: fnName, globals: this.globals });
    }
    return res;
  };

  // get(name): آخر قيمة معروفة لمتغيّر عام (من الذاكرة المحلية — لا يستدعي WASM في كل قراءة).
  App.prototype.get = function (name) {
    return this.globals[name];
  };

  // set(name, value): يضبط قيمة محلياً في الجلسة (بدون تشغيل كود Rin) ويعيد الرسم — مفيد لـ
  // rin-model (حقول الإدخال) قبل استدعاء دالة تعتمد على القيمة الجديدة.
  App.prototype.set = function (name, value) {
    this.globals[name] = value;
    this.Module.ccall(
      'rinhtml_set_global', null,
      ['number', 'string', 'string'],
      [this.sessionId, name, JSON.stringify(value)]
    );
    this._persistSave();
    this._render();
    this.emit('update', { fn: null, globals: this.globals });
  };

  // getData(name): نتيجة rin-fetch (كائن/مصفوفة) المخزّنة في جهة المتصفح.
  App.prototype.getData = function (name) { return this.data[name]; };

  // ===========================================================================================
  // 3ب) rinhtml-storage — حفظ المتغيّرات العامة المسمّاة في persist داخل localStorage/sessionStorage
  // ===========================================================================================
  function safeStore(area) {
    try { var st = global[area], k = '__rinhtml_probe__'; st.setItem(k, '1'); st.removeItem(k); return st; } catch (e) { return null; }
  }
  var sameKind = function (a, b) { return a === null || a === undefined || b === null || typeof a === typeof b; };

  // تُستدعى مرة قبل أول رسم: تستعيد القيم المحفوظة (فقط للمتغيّرات المعرَّفة فعلاً في main.rin وبنفس النوع).
  App.prototype._persistLoad = function () {
    if (!this._persist.length || !requireCap(this, 'storage', 'persist="..." (حفظ الحالة)')) { this._persist = []; return; }
    var st = safeStore(this._storeArea); if (!st) return;
    var rec; try { rec = JSON.parse(st.getItem(this._storeKey) || 'null'); } catch (e) { rec = null; }
    if (!rec || typeof rec !== 'object' || !rec.d) return;
    for (var i = 0; i < this._persist.length; i++) {
      var n = this._persist[i];
      if (!Object.prototype.hasOwnProperty.call(rec.d, n) || !(n in this.globals) || !sameKind(this.globals[n], rec.d[n])) continue;
      this.globals[n] = rec.d[n];
      this.Module.ccall('rinhtml_set_global', null, ['number', 'string', 'string'], [this.sessionId, n, JSON.stringify(rec.d[n])]);
    }
  };

  App.prototype._persistSave = function () {
    if (!this._persist.length) return;
    var st = safeStore(this._storeArea); if (!st) return;
    var d = {};
    for (var i = 0; i < this._persist.length; i++) { var n = this._persist[i]; if (n in this.globals) d[n] = this.globals[n]; }
    var json = JSON.stringify({ t: Date.now(), d: d });
    var sig = JSON.stringify(d); if (sig === this._lastSavedSig) return;   // لا كتابة إن لم يتغيّر شيء
    try { st.setItem(this._storeKey, json); this._lastSavedSig = sig; } catch (e) { console.warn('[RinHTML] تعذّر حفظ الحالة:', e); }
  };

  // ===========================================================================================
  // 3ج) rinhtml-fetch — rin-fetch: جلب في الخلفية يحدّث الجزء المرتبط فقط
  // ===========================================================================================
  var DUR = { ms: 1, s: 1000, m: 60000, h: 3600000 };
  function parseDur(v) {
    if (v == null || v === '') return 0;
    var t = 0, ok = false;
    String(v).replace(/(\d+(?:\.\d+)?)(ms|s|m|h)/g, function (_, n, u) { t += parseFloat(n) * DUR[u]; ok = true; return ''; });
    return ok ? t : (parseFloat(v) > 0 ? parseFloat(v) : 0);
  }
  function pickPath(j, p) {
    if (!p) return j;
    var parts = String(p).split('.');
    for (var i = 0; i < parts.length; i++) { if (j == null) return undefined; j = j[parts[i]]; }
    return j;
  }
  var isScalar = function (v) { return v === null || typeof v === 'string' || typeof v === 'number' || typeof v === 'boolean'; };

  App.prototype._setScalar = function (name, value) {
    this.globals[name] = value;
    this.Module.ccall('rinhtml_set_global', null, ['number', 'string', 'string'], [this.sessionId, name, JSON.stringify(value)]);
  };

  App.prototype._bindFetch = function (el) {
    var app = this;
    var tpl = el.getAttribute('rin-fetch');
    var into = el.getAttribute('rin-into'), pick = el.getAttribute('rin-pick'), errVar = el.getAttribute('rin-error');
    var every = parseDur(el.getAttribute('rin-every')), deb = parseDur(el.getAttribute('rin-debounce'));
    var on = el.getAttribute('rin-fetch-on');
    var ctl = null, seq = 0, debT = 0, pollT = 0, alive = true;

    function url() {
      return tpl.replace(/\{([A-Za-z_][\w]*)\}/g, function (m, k) {
        var v = app.globals[k]; return encodeURIComponent(v == null ? '' : v);
      });
    }
    function apply(fn) { fn(); app._persistSave(); app._render(); app.emit('update', { fn: null, globals: app.globals }); }

    function run() {
      var u = url(), mine = ++seq;
      if (/^\s*[a-z][a-z0-9+.-]*:/i.test(u) && !/^\s*https?:/i.test(u)) { console.warn('[RinHTML] rin-fetch: يُسمح بروابط http/https والنسبية فقط:', u); return; }
      if (ctl) ctl.abort();                               // الطلب الأحدث يلغي الأقدم
      var c = ctl = new AbortController();
      el.setAttribute('aria-busy', 'true');
      return fetch(u, { signal: c.signal, headers: { Accept: 'application/json' }, credentials: 'same-origin' })
        .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status + (r.statusText ? ': ' + r.statusText : '')); return r.json(); })
        .then(function (j) {
          if (mine !== seq || !alive) return;
          var v = pickPath(j, pick); if (v === undefined) throw new Error('rin-pick: المسار غير موجود في الرد');
          el.removeAttribute('aria-busy');
          apply(function () {
            if (into) { if (isScalar(v)) { app._setScalar(into, v); delete app.data[into]; } else app.data[into] = v; }
            if (errVar) app._setScalar(errVar, '');
          });
        })
        .catch(function (e) {
          if (mine !== seq || !alive || (e && e.name === 'AbortError')) return;
          el.removeAttribute('aria-busy');
          apply(function () { if (errVar) app._setScalar(errVar, String((e && e.message) || e)); });   // البيانات السابقة تبقى ظاهرة
          app.emit('error', { fn: 'rin-fetch', message: String((e && e.message) || e) });
        });
    }
    function schedule() {
      clearTimeout(pollT); if (!alive || !every) return;
      pollT = setTimeout(function () {
        if (document.hidden || (navigator.onLine === false)) { schedule(); return; }   // مخفي/بلا اتصال: لا طلبات
        Promise.resolve(run()).then(schedule, schedule);
      }, every);
    }
    function trigger() { if (!deb) { run(); return; } clearTimeout(debT); debT = setTimeout(run, deb); }

    if (on) { var h = function () { trigger(); }; el.addEventListener(on, h); app._timers.push(function () { el.removeEventListener(on, h); }); }
    else { Promise.resolve(run()).then(schedule, schedule); }
    if (on && every) schedule();
    var vis = function () { if (every && !document.hidden) { clearTimeout(pollT); Promise.resolve(run()).then(schedule, schedule); } };
    if (every) { document.addEventListener('visibilitychange', vis); app._timers.push(function () { document.removeEventListener('visibilitychange', vis); }); }
    app._timers.push(function () { alive = false; clearTimeout(pollT); clearTimeout(debT); if (ctl) ctl.abort(); });
  };

  App.prototype.on = function (event, handler) {
    (this._listeners[event] = this._listeners[event] || []).push(handler);
  };

  App.prototype.emit = function (event, detail) {
    (this._listeners[event] || []).forEach(function (h) { try { h(detail); } catch (e) { console.error(e); } });
  };

  App.prototype.destroy = function () {
    this.unmount();
    this.Module.ccall('rinhtml_free_session', null, ['number'], [this.sessionId]);
  };

  // ===========================================================================================
  // 4) rinhtml-dom + rinhtml-events — ربط توجيهات HTML (rin-*) بالحالة والدوال
  // ===========================================================================================
  var DIRECTIVE_SELECTOR = '[rin-click],[rin-text],[rin-model],[rin-show],[rin-each]';

  // يفسّر "add()" أو "increment(count, 1)" إلى {name, args} — الوسائط: أرقام / "نص" / true|false
  // / أو اسم متغيّر عام حالي (يُستبدَل بقيمته الحالية من app.globals).
  function parseCallExpr(app, expr) {
    var m = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)\s*$/.exec(expr || '');
    if (!m) return { name: (expr || '').trim(), args: [] };
    var name = m[1];
    var rawArgs = m[2].trim();
    if (!rawArgs) return { name: name, args: [] };
    var args = rawArgs.split(',').map(function (tok) {
      tok = tok.trim();
      if (/^-?\d+(\.\d+)?$/.test(tok)) return parseFloat(tok);
      if (/^".*"$/.test(tok) || /^'.*'$/.test(tok)) return tok.slice(1, -1);
      if (tok === 'true') return true;
      if (tok === 'false') return false;
      if (Object.prototype.hasOwnProperty.call(app.globals, tok)) return app.globals[tok];
      return tok;
    });
    return { name: name, args: args };
  }

  App.prototype.mount = function (root) {
    if (!requireCap(this, 'dom', 'ربط عناصر HTML (rin-click/rin-text/...)')) return;
    this.root = root || this.root;
    var app = this;
    var els = this.root.querySelectorAll(DIRECTIVE_SELECTOR);
    els.forEach(function (el) {
      if (el.hasAttribute('rin-click')) {
        var handler = function (ev) {
          var call = parseCallExpr(app, el.getAttribute('rin-click'));
          app.call.apply(app, [call.name].concat(call.args));
        };
        el.addEventListener('click', handler);
        app._bound.push({ el: el, type: 'click', handler: handler });
      }
      if (el.hasAttribute('rin-model')) {
        var varName = el.getAttribute('rin-model');
        var inputHandler = function () {
          var v = el.type === 'checkbox' ? el.checked
                : el.type === 'number' ? parseFloat(el.value)
                : el.value;
          app.set(varName, v);
        };
        el.addEventListener('input', inputHandler);
        app._bound.push({ el: el, type: 'input', handler: inputHandler });
      }
    });
    this._render();
    if (this.allow.fetch) {
      this.root.querySelectorAll('[rin-fetch]').forEach(function (el) { app._bindFetch(el); });
    } else if (this.root.querySelector && this.root.querySelector('[rin-fetch]')) {
      requireCap(this, 'fetch', 'rin-fetch (جلب في الخلفية)');
    }
  };

  App.prototype.unmount = function () {
    this._bound.forEach(function (b) { b.el.removeEventListener(b.type, b.handler); });
    this._bound = [];
    this._timers.forEach(function (f) { try { f(); } catch (e) {} });
    this._timers = [];
  };

  App.prototype.reload = function () {
    var app = this;
    if (!this.src) return Promise.resolve(app);
    return fetchSource(this.src).then(function (source) {
      applyStickerFavicon(source, app.src);
      app.unmount();
      app.Module.ccall('rinhtml_free_session', null, ['number'], [app.sessionId]);
      var newId = createSession(app.Module, source);
      app.sessionId = newId;
      app._refreshGlobals();
      app._persistLoad();
      app.mount(app.root);
      return app;
    });
  };

  // إعادة رسم كل عناصر rin-text/rin-show حسب الحالة الحالية (globals) — لا يمس rin-model أثناء
  // كتابة المستخدم فيه، لتجنّب "قفز المؤشر" في حقول الإدخال.
  App.prototype._render = function () {
    if (!this.root || !this.allow.dom) return;
    var app = this;
    // لا يُكتب في الـ DOM إلا ما تغيّرت قيمته: التحديث يلمس الجزء المرتبط بالمتغيّر المتغيّر وحده.
    this.root.querySelectorAll('[rin-text]').forEach(function (el) {
      var name = el.getAttribute('rin-text');
      var v = app.globals[name];
      var t = (v === undefined || v === null) ? '' : String(v);
      if (el.textContent !== t) el.textContent = t;
    });
    this.root.querySelectorAll('[rin-show]').forEach(function (el) {
      var name = el.getAttribute('rin-show');
      var d = app.globals[name] ? '' : 'none';
      if (el.style.display !== d) el.style.display = d;
    });
    // rin-each="name": يكرّر <template> الداخلي لكل عنصر من app.data[name]؛ حقول rin-field="a.b" ("." = العنصر نفسه)
    this.root.querySelectorAll('[rin-each]').forEach(function (el) {
      var items = app.data[el.getAttribute('rin-each')];
      var sig = JSON.stringify(items === undefined ? null : items);
      if (el._rinSig === sig) return;
      el._rinSig = sig;
      var tpl = el.querySelector('template'); if (!tpl) return;
      Array.prototype.slice.call(el.children).forEach(function (c) { if (c !== tpl) c.remove(); });
      (Array.isArray(items) ? items : []).forEach(function (it) {
        var frag = tpl.content.cloneNode(true);
        frag.querySelectorAll('[rin-field]').forEach(function (f) {
          var p = f.getAttribute('rin-field'), v = p === '.' ? it : pickPath(it, p);
          f.textContent = v == null ? '' : (typeof v === 'object' ? JSON.stringify(v) : String(v));
        });
        el.appendChild(frag);
      });
    });
    this.root.querySelectorAll('[rin-model]').forEach(function (el) {
      if (document.activeElement === el) return; // لا تُعدّل حقلاً يكتب فيه المستخدم الآن
      var name = el.getAttribute('rin-model');
      var v = app.globals[name];
      if (el.type === 'checkbox') el.checked = !!v;
      else el.value = (v === undefined || v === null) ? '' : String(v);
    });
  };

  function createSession(Module, source) {
    var id = Module.ccall('rinhtml_create', 'number', ['string'], [source]);
    if (id < 0) {
      var err = Module.ccall('rinhtml_last_error', 'string', [], []);
      throw new Error('فشل تشغيل برنامج Rin: ' + err);
    }
    return id;
  }

  // ===========================================================================================
  // 5) الواجهة العامة rinhtml.*  (load / run / mount / unmount / reload / call / get / set / on / emit)
  // ===========================================================================================
  var rinhtml = {
    apps: {}, // id -> App، للوصول برمجياً (rinhtml.apps.main.call('add'))

    load: fetchSource,

    // run(source, opts) -> Promise<App>
    //   opts: { allow: "dom storage", root: HTMLElement, src: "main.rin" (لأجل reload) }
    run: function (source, opts) {
      opts = opts || {};
      applyStickerFavicon(source, opts.src);
      return loadEngine().then(function (Module) {
        var id = createSession(Module, source);
        var app = new App(Module, id, {
          allow: parseAllow(opts.allow),
          root: opts.root || document,
          src: opts.src || null,
          id: opts.id || null,
          persist: typeof opts.persist === 'string' ? opts.persist.trim().split(/\s+/).filter(Boolean) : (opts.persist || []),
          persistKey: opts.persistKey || null,
          persistStore: opts.persistStore || null,
        });
        app._persistLoad();
        var boot = Module.ccall('rinhtml_boot_output', 'string', ['number'], [id]);
        if (boot) console.log('[RinHTML]', boot);
        return app;
      });
    },

    // runFile(src, opts) -> Promise<App> — يجلب ثم يشغّل
    runFile: function (src, opts) {
      opts = Object.assign({ src: src }, opts || {});
      return fetchSource(src).then(function (source) { return rinhtml.run(source, opts); });
    },

    mount: function (app, root) { return app.mount(root); },
    unmount: function (app) { return app.unmount(); },
    reload: function (app) { return app.reload(); },
    call: function (app, fnName) { return app.call.apply(app, [fnName].concat(Array.prototype.slice.call(arguments, 2))); },
    get: function (app, name) { return app.get(name); },
    set: function (app, name, value) { return app.set(name, value); },
    on: function (app, event, handler) { return app.on(event, handler); },
    emit: function (app, event, detail) { return app.emit(event, detail); },
    bind: function (app, root) { return app.mount(root || document); }, // اسم بديل يطابق المواصفة
  };

  global.rinhtml = rinhtml;

  // ===========================================================================================
  // 6) عناصر HTML مخصّصة: <rin-app>, <rin-container>, <rin-run> — كلها تسلك نفس السلوك
  // ===========================================================================================
  function defineRinElement(tagName) {
    if (customElements.get(tagName)) return;
    customElements.define(tagName, class extends HTMLElement {
      connectedCallback() {
        var el = this;
        var src = this.getAttribute('src') || this.getAttribute('file');
        var allow = this.getAttribute('allow');
        var appId = this.getAttribute('id') || this.getAttribute('name') || null;
        // الافتراضي: الربط على مستوى المستند كله (زر/عنصر عرض قد يكون خارج <rin-app> في DOM،
        // تماماً كما في مثال العدّاد). حدِّد scope="self" صراحة لحصر الربط داخل هذا العنصر فقط.
        var root = this.getAttribute('scope') === 'self' ? this : document;

        if (!src) { console.error('[RinHTML] <' + tagName + '> يحتاج src="ملف.rin"'); return; }

        rinhtml.runFile(src, {
          allow: allow, root: root, id: appId,
          persist: this.getAttribute('persist') || '',
          persistKey: this.getAttribute('persist-key'),
          persistStore: this.getAttribute('persist-store'),
        })
          .then(function (app) {
            el.rinApp = app;
            if (appId) rinhtml.apps[appId] = app;
            app.mount(root);
            el.dispatchEvent(new CustomEvent('rinhtml:ready', { detail: app, bubbles: true }));
          })
          .catch(function (err) {
            console.error('[RinHTML]', err);
            el.dispatchEvent(new CustomEvent('rinhtml:error', { detail: err, bubbles: true }));
          });
      }
      disconnectedCallback() {
        if (this.rinApp) this.rinApp.unmount();
      }
    });
  }

  ['rin-app', 'rin-container', 'rin-run'].forEach(defineRinElement);

})(typeof window !== 'undefined' ? window : this);

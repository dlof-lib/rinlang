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
    this._render();
    this.emit('update', { fn: null, globals: this.globals });
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
  var DIRECTIVE_SELECTOR = '[rin-click],[rin-text],[rin-model],[rin-show]';

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
  };

  App.prototype.unmount = function () {
    this._bound.forEach(function (b) { b.el.removeEventListener(b.type, b.handler); });
    this._bound = [];
  };

  App.prototype.reload = function () {
    var app = this;
    if (!this.src) return Promise.resolve(app);
    return fetchSource(this.src).then(function (source) {
      app.unmount();
      app.Module.ccall('rinhtml_free_session', null, ['number'], [app.sessionId]);
      var newId = createSession(app.Module, source);
      app.sessionId = newId;
      app._refreshGlobals();
      app.mount(app.root);
      return app;
    });
  };

  // إعادة رسم كل عناصر rin-text/rin-show حسب الحالة الحالية (globals) — لا يمس rin-model أثناء
  // كتابة المستخدم فيه، لتجنّب "قفز المؤشر" في حقول الإدخال.
  App.prototype._render = function () {
    if (!this.root || !this.allow.dom) return;
    var app = this;
    this.root.querySelectorAll('[rin-text]').forEach(function (el) {
      var name = el.getAttribute('rin-text');
      var v = app.globals[name];
      el.textContent = (v === undefined || v === null) ? '' : String(v);
    });
    this.root.querySelectorAll('[rin-show]').forEach(function (el) {
      var name = el.getAttribute('rin-show');
      var truthy = !!app.globals[name];
      el.style.display = truthy ? '' : 'none';
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
      return loadEngine().then(function (Module) {
        var id = createSession(Module, source);
        var app = new App(Module, id, {
          allow: parseAllow(opts.allow),
          root: opts.root || document,
          src: opts.src || null,
        });
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

        rinhtml.runFile(src, { allow: allow, root: root })
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

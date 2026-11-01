/*!
 * rin_html_runtime.js — جسر index.html ↔ Rin (يُحقن تلقائياً في الصفحة).
 *
 * HTML تعرض فقط، والمنطق في ملف حاوية Rin. هذا الملف ينقل الأحداث والحالة بين الاثنين:
 *   rin-text="expr"          rin-click="f(a, b)"      rin-model="x"
 *   rin-if / rin-show="e"    rin-else                 rin-for="t, i in list"
 *   rin-attr="href:u; ..."   rin-class="done:t.done"
 * ومن JavaScript: rin.call / rin.get / rin.set / rin.globals / rin.clearState / rin.refresh
 *
 * النقل (Transport):
 *   - داخل التطبيق: window.RinBridge (WebView JavascriptInterface).
 *   - على http://localhost:7700 : window.RIN_HTTP_BASE (يضعه الخادم المحلي) وطلبات XHR متزامنة.
 */
(function (global) {
  'use strict';
  if (global.rin && global.rin.__runtime) return;

  // ------------------------------------------------------------------ النقل
  function makeTransport() {
    if (global.RinBridge) {
      var b = global.RinBridge;
      return {
        name: 'app',
        globals: function () { return b.globals(); },
        call: function (fn, argsJson) { return b.call(fn, argsJson); },
        set: function (name, json) { b.set(name, json); },
        clearState: function () { b.clearState(); },
        error: function (m) { try { b.error(String(m)); } catch (e) {} }
      };
    }
    if (typeof global.RIN_HTTP_BASE === 'string') {
      var base = global.RIN_HTTP_BASE.replace(/\/*$/, '/');
      var post = function (path, body) {
        var x = new XMLHttpRequest();
        x.open('POST', base + path, false);
        x.setRequestHeader('Content-Type', 'application/json');
        x.setRequestHeader('X-Rin-Token', global.RIN_HTTP_TOKEN || '');
        try { x.send(JSON.stringify(body || {})); } catch (e) { return '{"ok":false,"error":"network","globals":{}}'; }
        return x.responseText || '{}';
      };
      return {
        name: 'http',
        globals: function () { return post('__rin/globals'); },
        call: function (fn, argsJson) { return post('__rin/call', { fn: fn, args: JSON.parse(argsJson || '[]') }); },
        set: function (name, json) { post('__rin/set', { name: name, value: JSON.parse(json) }); },
        clearState: function () { post('__rin/clear'); },
        error: function (m) { try { post('__rin/error', { message: String(m) }); } catch (e) {} }
      };
    }
    return null;
  }

  var transport = makeTransport();
  var state = {};
  var rootScope = Object.create(null);

  function logError(msg) {
    try { console.error('[Rin] ' + msg); } catch (e) {}
    if (transport) transport.error(msg);
  }

  function parseJson(text, fallback) {
    try { return JSON.parse(text); } catch (e) { return fallback; }
  }

  // ------------------------------------------------------------------ التعابير
  // المحلّل: مسارات منقّطة، حرفيات، == != < > <= >= ! && || وأقواس.
  function tokenize(src) {
    var t = [], i = 0, n = src.length, c;
    while (i < n) {
      c = src[i];
      if (/\s/.test(c)) { i++; continue; }
      if (c === '"' || c === "'") {
        var q = c, s = ''; i++;
        while (i < n && src[i] !== q) {
          if (src[i] === '\\' && i + 1 < n) { i++; var e = src[i]; s += e === 'n' ? '\n' : e === 't' ? '\t' : e; }
          else s += src[i];
          i++;
        }
        i++; t.push({ k: 'lit', v: s }); continue;
      }
      if (/[0-9]/.test(c) || (c === '-' && /[0-9]/.test(src[i + 1] || '') && (!t.length || t[t.length - 1].k === 'op'))) {
        var j = i + 1;
        while (j < n && /[0-9.]/.test(src[j])) j++;
        t.push({ k: 'lit', v: parseFloat(src.slice(i, j)) }); i = j; continue;
      }
      if (/[A-Za-z_$؀-ۿ]/.test(c)) {
        var k = i + 1;
        while (k < n && /[A-Za-z0-9_$.؀-ۿ]/.test(src[k])) k++;
        var w = src.slice(i, k);
        if (w === 'true') t.push({ k: 'lit', v: true });
        else if (w === 'false') t.push({ k: 'lit', v: false });
        else if (w === 'null') t.push({ k: 'lit', v: null });
        else t.push({ k: 'path', v: w });
        i = k; continue;
      }
      var two = src.substr(i, 2);
      if (two === '==' || two === '!=' || two === '<=' || two === '>=' || two === '&&' || two === '||') {
        t.push({ k: 'op', v: two }); i += 2; continue;
      }
      if ('<>!()'.indexOf(c) !== -1) { t.push({ k: 'op', v: c }); i++; continue; }
      throw new Error('رمز غير متوقّع «' + c + '» في التعبير: ' + src);
    }
    return t;
  }

  var exprCache = Object.create(null);
  function compile(src) {
    src = String(src).trim();
    if (exprCache[src]) return exprCache[src];
    var toks = tokenize(src), p = 0;
    function peek() { return toks[p]; }
    function isOp(v) { var x = toks[p]; return x && x.k === 'op' && x.v === v; }
    function parseOr() {
      var l = parseAnd();
      while (isOp('||')) { p++; var r = parseAnd(); l = (function (a, b) { return function (s) { return truthy(a(s)) || truthy(b(s)); }; })(l, r); }
      return l;
    }
    function parseAnd() {
      var l = parseCmp();
      while (isOp('&&')) { p++; var r = parseCmp(); l = (function (a, b) { return function (s) { return truthy(a(s)) && truthy(b(s)); }; })(l, r); }
      return l;
    }
    function parseCmp() {
      var l = parseUnary(), x = peek();
      while (x && x.k === 'op' && ['==', '!=', '<', '>', '<=', '>='].indexOf(x.v) !== -1) {
        p++; var r = parseUnary();
        l = (function (a, b, op) {
          return function (s) {
            var u = a(s), v = b(s);
            switch (op) {
              case '==': return eq(u, v);
              case '!=': return !eq(u, v);
              case '<': return u < v;
              case '>': return u > v;
              case '<=': return u <= v;
              default: return u >= v;
            }
          };
        })(l, r, x.v);
        x = peek();
      }
      return l;
    }
    function parseUnary() {
      if (isOp('!')) { p++; var e = parseUnary(); return function (s) { return !truthy(e(s)); }; }
      return parsePrimary();
    }
    function parsePrimary() {
      var x = toks[p++];
      if (!x) throw new Error('تعبير ناقص: ' + src);
      if (x.k === 'lit') return function () { return x.v; };
      if (x.k === 'path') return function (s) { return resolvePath(s, x.v); };
      if (x.k === 'op' && x.v === '(') {
        var e = parseOr();
        if (!isOp(')')) throw new Error('قوس غير مغلق في: ' + src);
        p++; return e;
      }
      throw new Error('تعبير غير صالح: ' + src);
    }
    var fn = parseOr();
    if (p < toks.length) throw new Error('بقية غير مفهومة في التعبير: ' + src);
    exprCache[src] = fn;
    return fn;
  }

  function eq(a, b) {
    if (a === b) return true;
    if (a == null && b == null) return true;
    if (typeof a === 'object' || typeof b === 'object') return JSON.stringify(a) === JSON.stringify(b);
    return false;
  }

  // المصفوفة الفارغة تُعدّ false (كما في Rin).
  function truthy(v) {
    if (Array.isArray(v)) return v.length > 0;
    return !!v;
  }

  function resolvePath(scope, path) {
    var parts = path.split('.'), first = parts[0], cur;
    if (scope && first in scope) cur = scope[first];
    else cur = state[first];
    for (var i = 1; i < parts.length; i++) {
      if (cur == null) return undefined;
      if (parts[i] === 'length' && (Array.isArray(cur) || typeof cur === 'string')) cur = cur.length;
      else cur = cur[parts[i]];
    }
    return cur;
  }

  function evalExpr(src, scope) {
    try { return compile(src)(scope); }
    catch (e) { logError(e.message); return undefined; }
  }

  function show(v) {
    if (v == null) return '';
    if (typeof v === 'object') return JSON.stringify(v);
    return String(v);
  }

  // rin-click="f(a, 'b', t.id)" → { fn, args:[exprs] }
  function parseCall(src) {
    var m = /^\s*([A-Za-z_$؀-ۿ][A-Za-z0-9_$؀-ۿ]*)\s*(?:\(([\s\S]*)\))?\s*;?\s*$/.exec(src);
    if (!m) throw new Error('rin-click غير صالح: ' + src);
    var args = [], body = m[2] || '', depth = 0, quote = null, cur = '';
    for (var i = 0; i < body.length; i++) {
      var c = body[i];
      if (quote) { cur += c; if (c === quote && body[i - 1] !== '\\') quote = null; continue; }
      if (c === '"' || c === "'") { quote = c; cur += c; continue; }
      if (c === '(') depth++;
      if (c === ')') depth--;
      if (c === ',' && depth === 0) { args.push(cur); cur = ''; continue; }
      cur += c;
    }
    if (cur.trim()) args.push(cur);
    return { fn: m[1], args: args };
  }

  // ------------------------------------------------------------------ الاستدعاء
  function applyResult(res) {
    if (res && res.globals && typeof res.globals === 'object') state = res.globals;
  }

  function callRin(fn, args) {
    if (!transport) { logError('لا يوجد جسر Rin في هذه الصفحة'); return { ok: false, error: 'no bridge' }; }
    var res = parseJson(transport.call(fn, JSON.stringify(args || [])), { ok: false, error: 'bad response' });
    if (res.ok === false) logError('Rin: ' + fn + '() — ' + (res.error || 'خطأ'));
    applyResult(res);
    render();
    return res;
  }

  function setGlobal(path, value) {
    var parts = String(path).split('.'), root = parts[0];
    if (parts.length === 1) state[root] = value;
    else {
      var o = state[root];
      for (var i = 1; i < parts.length - 1 && o != null; i++) o = o[parts[i]];
      if (o != null) o[parts[parts.length - 1]] = value;
    }
    if (transport) transport.set(root, JSON.stringify(state[root] === undefined ? null : state[root]));
  }

  // ------------------------------------------------------------------ العرض
  var BLOCKED_ATTR = /^on/i;

  function prepare(root) {
    // يحوّل كل عنصر rin-for داخل root إلى مرساة (تعليق) + قالب.
    var hosts = [];
    if (root.querySelectorAll) hosts = Array.prototype.slice.call(root.querySelectorAll('[rin-for]'));
    hosts.forEach(function (host) {
      if (host.__prepared) return;
      var spec = /^\s*([^\s,]+)\s*(?:,\s*([^\s]+)\s*)?\s+in\s+([\s\S]+?)\s*$/.exec(host.getAttribute('rin-for') || '');
      if (!spec) { logError('rin-for غير صالح: ' + host.getAttribute('rin-for')); return; }
      var anchor = document.createComment('rin-for');
      var tpl = host.cloneNode(true);
      tpl.removeAttribute('rin-for');
      anchor.__for = { tpl: tpl, item: spec[1], idx: spec[2] || null, list: spec[3], clones: [] };
      host.parentNode.replaceChild(anchor, host);
      host.__prepared = true;
    });
  }

  function renderChildren(parent, scope) {
    var kids = Array.prototype.slice.call(parent.childNodes);
    var lastIf = null; // نتيجة آخر rin-if لعنصر شقيق (لـ rin-else)
    for (var i = 0; i < kids.length; i++) {
      var node = kids[i];
      if (node.nodeType === 8 && node.__for) { renderFor(node, scope); continue; }
      if (node.nodeType !== 1 || node.__rinClone) continue;
      if (node.hasAttribute('rin-else')) {
        var vis = lastIf === false;
        node.style.display = vis ? '' : 'none';
        lastIf = null;
        if (!vis) { node.__rinScope = scope; continue; }
      } else if (node.hasAttribute('rin-if') || node.hasAttribute('rin-show')) {
        var e = node.getAttribute('rin-if') || node.getAttribute('rin-show');
        var on = truthy(evalExpr(e, scope));
        node.style.display = on ? '' : 'none';
        lastIf = node.hasAttribute('rin-if') ? on : null;
        if (!on) { node.__rinScope = scope; continue; }
      } else lastIf = null;
      renderElement(node, scope);
    }
  }

  function renderFor(anchor, scope) {
    var f = anchor.__for;
    f.clones.forEach(function (c) { if (c.parentNode) c.parentNode.removeChild(c); });
    f.clones = [];
    var list = evalExpr(f.list, scope), items = [];
    if (Array.isArray(list)) items = list.map(function (v, i) { return [i, v]; });
    else if (list && typeof list === 'object') items = Object.keys(list).map(function (k) { return [k, k]; });
    var ref = anchor.nextSibling, parent = anchor.parentNode;
    items.forEach(function (pair) {
      var el = f.tpl.cloneNode(true);
      el.__rinClone = true;
      prepare(el);
      var s = Object.create(scope);
      s[f.item] = pair[1];
      if (f.idx) s[f.idx] = pair[0];
      s.$index = pair[0];
      parent.insertBefore(el, ref);
      f.clones.push(el);
      // ظهور/إخفاء على العنصر المكرَّر نفسه
      if (el.hasAttribute('rin-if') || el.hasAttribute('rin-show')) {
        var on = truthy(evalExpr(el.getAttribute('rin-if') || el.getAttribute('rin-show'), s));
        el.style.display = on ? '' : 'none';
        el.__rinScope = s;
        if (!on) return;
      }
      renderElement(el, s);
    });
  }

  function renderElement(el, scope) {
    el.__rinScope = scope;
    var a;
    if ((a = el.getAttribute('rin-text')) != null) {
      var t = show(evalExpr(a, scope));
      if (el.textContent !== t) el.textContent = t;
    }
    if ((a = el.getAttribute('rin-class')) != null) {
      a.split(';').forEach(function (pair) {
        var ix = pair.indexOf(':');
        if (ix < 0) return;
        var cls = pair.slice(0, ix).trim(), ex = pair.slice(ix + 1).trim();
        if (cls) el.classList.toggle(cls, truthy(evalExpr(ex, scope)));
      });
    }
    if ((a = el.getAttribute('rin-attr')) != null) {
      a.split(';').forEach(function (pair) {
        var ix = pair.indexOf(':');
        if (ix < 0) return;
        var name = pair.slice(0, ix).trim(), ex = pair.slice(ix + 1).trim();
        if (!name || BLOCKED_ATTR.test(name)) return;
        var v = evalExpr(ex, scope);
        if (v == null || v === false) el.removeAttribute(name);
        else el.setAttribute(name, v === true ? '' : show(v));
      });
    }
    if ((a = el.getAttribute('rin-model')) != null) {
      var mv = evalExpr(a, scope);
      if (el.type === 'checkbox') el.checked = truthy(mv);
      else if (el.type === 'radio') el.checked = show(mv) === el.value;
      else {
        var sv = show(mv);
        if (el.value !== sv && global.document.activeElement !== el) el.value = sv;
      }
    }
    renderChildren(el, scope);
  }

  var booted = false;
  function render() {
    if (!booted) return;
    renderChildren(document.body, rootScope);
  }

  // ------------------------------------------------------------------ الأحداث
  function onClick(ev) {
    var el = ev.target && ev.target.closest ? ev.target.closest('[rin-click]') : null;
    if (!el) return;
    if (el.disabled) return;
    var scope = el.__rinScope || rootScope, call;
    try { call = parseCall(el.getAttribute('rin-click')); }
    catch (e) { logError(e.message); return; }
    if (el.tagName === 'A' || el.type === 'submit') ev.preventDefault();
    var args = call.args.map(function (x) { return evalExpr(x, scope); });
    callRin(call.fn, args.map(function (v) { return v === undefined ? null : v; }));
  }

  function onInput(ev) {
    var el = ev.target;
    if (!el || !el.getAttribute) return;
    var path = el.getAttribute('rin-model');
    if (!path) return;
    var v;
    if (el.type === 'checkbox') v = !!el.checked;
    else if (el.type === 'radio') { if (!el.checked) return; v = el.value; }
    else if (el.type === 'number' || el.type === 'range') v = el.value === '' ? 0 : parseFloat(el.value);
    else v = el.value;
    var first = path.split('.')[0];
    var scope = el.__rinScope || rootScope;
    if (scope && first in scope) { // متغيّر حلقة: تعديل محلي فقط
      var o = scope[first], parts = path.split('.');
      for (var i = 1; i < parts.length - 1 && o != null; i++) o = o[parts[i]];
      if (o != null && parts.length > 1) o[parts[parts.length - 1]] = v;
    } else setGlobal(path, v);
    render();
  }

  // ------------------------------------------------------------------ التشغيل
  function boot() {
    if (booted) return;
    if (transport) state = parseJson(transport.globals(), {});
    else logError('لا يوجد جسر Rin — تُعرض الصفحة بلا منطق.');
    prepare(document.body);
    booted = true;
    document.addEventListener('click', onClick);
    document.addEventListener('input', onInput);
    document.addEventListener('change', onInput);
    render();
    try { document.dispatchEvent(new Event('rin:ready')); } catch (e) {}
  }

  global.rin = {
    __runtime: true,
    call: function (fn) { return callRin(fn, Array.prototype.slice.call(arguments, 1)); },
    get: function (path) { return resolvePath(null, String(path)); },
    set: function (name, value) { setGlobal(name, value); render(); },
    globals: function () { return JSON.parse(JSON.stringify(state)); },
    clearState: function () { if (transport) transport.clearState(); },
    refresh: function () {
      if (transport) state = parseJson(transport.globals(), state);
      render();
    }
  };

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
  else boot();
})(window);

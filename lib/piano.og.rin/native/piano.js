/*! piano.js — مُصيِّر JavaScript لمشاهد piano (.puo / JSON) → SVG.
 *  نسخة حرفية من core/render.zpp (المواصفة المرجعية) — مخرجاته تساوي مخرجات Rin وC++ بايتاً ببايت.
 *  الاستعمال:  Piano.render(sceneObject [, {inspect:true}])  →  نص SVG
 *  inspect: يضيف data-pn="مسار.العقدة" لكل عنصر (لمفتّش صفحة المعاينة فقط؛ يغيّر الناتج).
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.Piano = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';
  var HEXD = '0123456789abcdef';
  function has(o, k) { return o != null && o[k] !== undefined && o[k] !== null; }
  function opt(o, k, d) { return has(o, k) ? o[k] : d; }
  function nf(v, d) {
    if (v !== v) return '0';
    if (v > 1e9) v = 1e9; if (v < -1e9) v = -1e9;
    var neg = v < 0, n = Math.floor(Math.abs(v) * Math.pow(10, d) + 0.5);
    if (n === 0) return '0';
    var s = String(n);
    if (d > 0) {
      while (s.length <= d) s = '0' + s;
      var ip = s.slice(0, s.length - d), fp = s.slice(s.length - d);
      fp = fp.replace(/0+$/, '');
      s = fp.length ? ip + '.' + fp : ip;
    }
    return neg ? '-' + s : s;
  }
  function n2(v) { return nf(v, 2); }
  function str(v) { return typeof v === 'string' ? v : (typeof v === 'number' ? nf(v, 6) : String(v)); }
  function esc(s) {
    return str(s).split('&').join('&amp;').split('<').join('&lt;').split('>').join('&gt;').split('"').join('&quot;');
  }
  function hex2(s, i) { return HEXD.indexOf(s[i].toLowerCase()) * 16 + HEXD.indexOf(s[i + 1].toLowerCase()); }
  function normColor(c) {
    if (typeof c !== 'string') return '#000000';
    if (c.length === 4 && c[0] === '#') return '#' + c[1] + c[1] + c[2] + c[2] + c[3] + c[3];
    return c;
  }
  function attr(name, v) { return ' ' + name + '="' + esc(v) + '"'; }
  function attrn(name, v) { return ' ' + name + '="' + n2(v) + '"'; }
  function anum(n, k, name) { return has(n, k) ? attrn(name, n[k]) : ''; }
  function astr(n, k, name) { return has(n, k) ? attr(name, n[k]) : ''; }
  function url(v) { return v[0] === '@' ? 'url(#' + v.slice(1) + ')' : v; }
  function paint(n, key, a, opkey, opAttr) {
    var s = '', op = null;
    if (has(n, key)) {
      var v = n[key];
      if (typeof v === 'string') {
        if (v[0] === '@') s = attr(a, url(v));
        else {
          v = normColor(v);
          if (v.length === 9 && v[0] === '#') { s = attr(a, v.slice(0, 7)); op = hex2(v, 7) / 255; }
          else s = attr(a, v);
        }
      } else s = attr(a, v);
    }
    if (has(n, opkey)) op = n[opkey];
    if (op !== null) s += attr(opAttr, nf(op, 3));
    return s;
  }
  function tfstr(tf) {
    return tf.map(function (op) { return op[0] + '(' + op.slice(1).map(n2).join(' ') + ')'; }).join(' ');
  }
  function tail(n) {
    var s = paint(n, 'fill', 'fill', 'fo', 'fill-opacity');
    s += paint(n, 'stroke', 'stroke', 'so', 'stroke-opacity');
    s += anum(n, 'sw', 'stroke-width');
    if (has(n, 'dash')) s += attr('stroke-dasharray', n.dash.map(n2).join(' '));
    s += astr(n, 'cap', 'stroke-linecap') + astr(n, 'join', 'stroke-linejoin') + anum(n, 'op', 'opacity');
    if (has(n, 'tf')) s += attr('transform', tfstr(n.tf));
    if (has(n, 'filter')) s += attr('filter', url(n.filter));
    if (has(n, 'clip')) s += attr('clip-path', url(n.clip));
    return s;
  }
  function head(n) { return astr(n, 'id', 'id') + astr(n, 'cls', 'class'); }
  function av(v) { return typeof v === 'number' ? n2(v) : esc(v); }
  function repv(v) { if (v == null) return 'indefinite'; return typeof v === 'number' ? n2(v) : v; }
  function animStr(a) {
    if (opt(a, 'k', 'attr') === 'rotate') {
      var cx = n2(opt(a, 'cx', 0)), cy = n2(opt(a, 'cy', 0));
      return '<animateTransform attributeName="transform" type="rotate" from="' + n2(a.from) + ' ' + cx + ' ' + cy +
        '" to="' + n2(a.to) + ' ' + cx + ' ' + cy + '" dur="' + n2(a.dur) + 's" repeatCount="' + repv(a.rep) + '"/>';
    }
    var s = '<animate attributeName="' + esc(a.name) + '"';
    if (has(a, 'values')) s += ' values="' + a.values.map(av).join(';') + '"';
    else s += ' from="' + av(a.from) + '" to="' + av(a.to) + '"';
    return s + ' dur="' + n2(a.dur) + 's" repeatCount="' + repv(a.rep) + '"/>';
  }
  function points(f) {
    var p = [];
    for (var i = 0; i + 1 < f.length; i += 2) p.push(n2(f[i]) + ',' + n2(f[i + 1]));
    return p.join(' ');
  }
  function textAttrs(n) {
    var s = attrn('x', n.x) + attrn('y', n.y) + anum(n, 'size', 'font-size') + astr(n, 'family', 'font-family');
    if (has(n, 'weight')) s += attr('font-weight', av(n.weight));
    if (has(n, 'italic') && n.italic === true) s += attr('font-style', 'italic');
    return s + astr(n, 'anchor', 'text-anchor') + astr(n, 'base', 'dominant-baseline') + anum(n, 'ls', 'letter-spacing') + astr(n, 'dir', 'direction');
  }
  function textBody(n) {
    var txt = str(n.text), ls = txt.split('\n');
    if (ls.length < 2) return esc(txt);
    var size = opt(n, 'size', 16), lh = opt(n, 'lh', 1.2), out = '';
    for (var i = 0; i < ls.length; i++)
      out += '<tspan' + attrn('x', n.x) + attrn('dy', i > 0 ? size * lh : 0) + '>' + esc(ls[i]) + '</tspan>';
    return out;
  }
  function emit(out, n, ind, path, ins) {
    var pad = ' '.repeat(ind), t = n.t, a = head(n);
    if (ins) a += ' data-pn="' + path + '"';
    if (t === 'rect') a += anum(n, 'x', 'x') + anum(n, 'y', 'y') + anum(n, 'w', 'width') + anum(n, 'h', 'height') + anum(n, 'rx', 'rx') + anum(n, 'ry', 'ry');
    else if (t === 'circle') a += anum(n, 'cx', 'cx') + anum(n, 'cy', 'cy') + anum(n, 'r', 'r');
    else if (t === 'ellipse') a += anum(n, 'cx', 'cx') + anum(n, 'cy', 'cy') + anum(n, 'rx', 'rx') + anum(n, 'ry', 'ry');
    else if (t === 'line') a += anum(n, 'x1', 'x1') + anum(n, 'y1', 'y1') + anum(n, 'x2', 'x2') + anum(n, 'y2', 'y2');
    else if (t === 'polyline' || t === 'polygon') a += attr('points', points(n.pts));
    else if (t === 'path') a += attr('d', n.d);
    else if (t === 'text') a += textAttrs(n);
    else if (t === 'image') a += anum(n, 'x', 'x') + anum(n, 'y', 'y') + anum(n, 'w', 'width') + anum(n, 'h', 'height') + attr('href', n.href) + astr(n, 'par', 'preserveAspectRatio');
    a += tail(n);
    var kids = [];
    if (has(n, 'title')) kids.push('<title>' + esc(n.title) + '</title>');
    if (has(n, 'anim')) n.anim.forEach(function (an) { kids.push(animStr(an)); });
    if (t === 'text') { out.push(pad + '<text' + a + '>' + textBody(n) + kids.join('') + '</text>'); return out; }
    var sub = [];
    if (t === 'g' && n.nodes) n.nodes.forEach(function (ch, i) { emit(sub, ch, ind + 2, path + '.' + i, ins); });
    if (!kids.length && !sub.length) { out.push(pad + '<' + t + a + '/>'); return out; }
    out.push(pad + '<' + t + a + '>');
    kids.forEach(function (k) { out.push(pad + '  ' + k); });
    sub.forEach(function (s) { out.push(s); });
    out.push(pad + '</' + t + '>');
    return out;
  }
  function stopLines(out, stops, pad) {
    stops.forEach(function (s) {
      var col = normColor(s[1]), a = attrn('offset', s[0]);
      if (col.length === 9 && col[0] === '#') a += attr('stop-color', col.slice(0, 7)) + attr('stop-opacity', nf(hex2(col, 7) / 255, 3));
      else a += attr('stop-color', col);
      out.push(pad + '<stop' + a + '/>');
    });
  }
  function flood(c, op) {
    var col = normColor(c);
    if (col.length === 9 && col[0] === '#') col = col.slice(0, 7);
    return '<feFlood' + attr('flood-color', col) + attr('flood-opacity', nf(op, 3)) + '/>';
  }
  function emitDef(out, d, ind) {
    var pad = ' '.repeat(ind), t = d.t, id = attr('id', d.id);
    var region = ' x="-50%" y="-50%" width="200%" height="200%"';
    if (t === 'linear') {
      out.push(pad + '<linearGradient' + id + attrn('x1', d.x1) + attrn('y1', d.y1) + attrn('x2', d.x2) + attrn('y2', d.y2) + '>');
      stopLines(out, d.stops, pad + '  '); out.push(pad + '</linearGradient>');
    } else if (t === 'radial') {
      out.push(pad + '<radialGradient' + id + attrn('cx', d.cx) + attrn('cy', d.cy) + attrn('r', d.r) + '>');
      stopLines(out, d.stops, pad + '  '); out.push(pad + '</radialGradient>');
    } else if (t === 'shadow') {
      out.push(pad + '<filter' + id + region + '>');
      out.push(pad + '  <feGaussianBlur in="SourceAlpha"' + attrn('stdDeviation', d.blur / 2) + '/>');
      out.push(pad + '  <feOffset' + attrn('dx', d.dx) + attrn('dy', d.dy) + ' result="o"/>');
      out.push(pad + '  ' + flood(d.c, opt(d, 'op', 0.3)));
      out.push(pad + '  <feComposite in2="o" operator="in"/>');
      out.push(pad + '  <feMerge><feMergeNode/><feMergeNode in="SourceGraphic"/></feMerge>');
      out.push(pad + '</filter>');
    } else if (t === 'glow') {
      out.push(pad + '<filter' + id + region + '>');
      out.push(pad + '  <feGaussianBlur in="SourceAlpha"' + attrn('stdDeviation', d.r) + ' result="b"/>');
      out.push(pad + '  ' + flood(d.c, 1));
      out.push(pad + '  <feComposite in2="b" operator="in" result="g"/>');
      out.push(pad + '  <feMerge><feMergeNode in="g"/><feMergeNode in="g"/><feMergeNode in="SourceGraphic"/></feMerge>');
      out.push(pad + '</filter>');
    } else if (t === 'blur') {
      out.push(pad + '<filter' + id + region + '>');
      out.push(pad + '  <feGaussianBlur' + attrn('stdDeviation', d.r) + '/>');
      out.push(pad + '</filter>');
    } else if (t === 'pattern') {
      out.push(pad + '<pattern' + id + attrn('width', d.w) + attrn('height', d.h) + ' patternUnits="userSpaceOnUse">');
      (d.nodes || []).forEach(function (ch) { emit(out, ch, ind + 2, '', false); });
      out.push(pad + '</pattern>');
    } else if (t === 'clip') {
      out.push(pad + '<clipPath' + id + '>');
      (d.nodes || []).forEach(function (ch) { emit(out, ch, ind + 2, '', false); });
      out.push(pad + '</clipPath>');
    }
  }
  function render(c, o) {
    var ins = !!(o && o.inspect);
    if (!c || typeof c !== 'object' || !has(c, 'piano')) throw new Error('هذا ليس مشهد piano (حقل piano مفقود)');
    var w = n2(c.w), h = n2(c.h), out = [];
    var root = '<svg xmlns="http://www.w3.org/2000/svg" width="' + w + '" height="' + h + '" viewBox="0 0 ' + w + ' ' + h + '"';
    if (has(c, 'font')) root += attr('font-family', c.font);
    out.push(root + '>');
    if (has(c, 'title') && c.title !== '') out.push('  <title>' + esc(c.title) + '</title>');
    if (has(c, 'defs') && c.defs.length) {
      out.push('  <defs>');
      c.defs.forEach(function (d) { emitDef(out, d, 4); });
      out.push('  </defs>');
    }
    if (has(c, 'bg') && c.bg !== 'none') out.push('  <rect width="100%" height="100%"' + paint({ fill: c.bg }, 'fill', 'fill', '_', '_') + '/>');
    (c.nodes || []).forEach(function (n, i) { emit(out, n, 2, String(i), ins); });
    out.push('</svg>');
    return out.join('\n');
  }
  return { version: '1.0.0', render: render, nf: nf };
});

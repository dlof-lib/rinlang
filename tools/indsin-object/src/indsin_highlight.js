/*!
 * indsin_highlight — dependency-free syntax highlighter for indsin (.indsin) source.
 * MIT License. Works in browsers, Node, WebViews and workers (UMD, ES5).
 *
 *   IndsinHighlight.highlight(source, {lineNumbers:false, theme:'dark'|'light'}) → HTML string (escaped)
 *   IndsinHighlight.tokenize(source)  → [{type, text, line}]
 *   IndsinHighlight.stylesheet()      → CSS for the `.ih-*` classes (light + dark, honours prefers-color-scheme)
 *   IndsinHighlight.lexicon           → { kinds, attributes, tokenValues, keywords, ... }
 *
 * The element/attribute/token lists between the LEX markers are generated from the translator by
 * `node scripts/gen_syntax.js`; do not edit them by hand.
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.IndsinHighlight = factory();
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  /*LEX:BEGIN*/
  var LEX = {"version":"4.0.0","kinds":["Text","Image","Button","Card","Column","Row","Stack","Divider","Header","TopBar","BottomBar","Drawer","Menu","MenuItem","Table","TableRow","Video","Audio","WebView","Scaffold","Splash","Banner","Box","Grid","Wrap","Spacer","Badge","Progress","Checkbox","Switch","Avatar","Input","TextArea","Dialog","Tabs","TabItem","Tooltip","Icon","IconButton","Object","Link","Radio","Slider","Select","List","ListItem","File","Date","Time","CodeEditor","Calculator","Search","Tag","Kbd","Rating","Skeleton","Spinner","Steps","StepItem","Timeline","TimelineItem","Breadcrumb","Pagination","DonutChart","Custom"],"aliases":["breadcrumbs","chip","container","datepicker","donut","dropdown","element","filepicker","frame","key","loader","modal","pager","panel","piechart","popup","range","searchbar","sidebar","step","stepper","textfield","timelineevent","timepicker"],"attributes":["a11yLabel","a11y_hint","a11y_label","accept","active","align","alt","anchor","ariaLabel","autoplay","background","badge","bg","bold","border","borderColor","centerLabel","checked","closable","color","colors","columns","controls","copyText","count","current","data","date","decorative","delay","desc","description","dir","direction","disabled","dismissible","duration","easing","effect","elevation","fit","font","fontWeight","format","gap","glyph","gradient","group","header","height","href","icon","indeterminate","index","initials","italic","items","kind","label","labelSize","lang","layer","letterSpacing","lineHeight","loading","loop","margin","markerSize","max","maxHeight","maxLength","maxLines","maxWidth","max_height","max_width","message","min","minHeight","minWidth","min_height","min_width","modal","mono","multiple","muted","name","onChange","onClose","onDoubleTap","onHoverEnter","onHoverExit","onLongPress","onSelect","onSubmit","onTap","opacity","open","options","padding","placeholder","placement","poster","radius","ratio","readonly","removable","rows","scrim","selected","separator","shadow","size","sizing","source","sourceTag","src","stagger","state","step","subtitle","text","textAlign","textColor","text_align","theme","thickness","title","tone","total","trend","type","underline","url","valign","value","variant","vertical","visible","visited","weight","width"],"tokenValues":["action","auto","back","background","body","bold","bottom","bounce","bounceout","caption","center","code","comfortable","compact","contain","cover","cubicin","cubicinout","cubicout","custom","danger","dark","display","ease","easein","easeinout","easeout","elastic","elevated","end","error","fade","fast","fill","filled","forest","ghost","glass","gradient","grow","heading","headline","highcontrastdark","highcontrastlight","info","instant","italic","label","large","left","lg","light","linear","link","ltr","md","medium","midnight","neutral","none","normal","ocean","outline","outlined","pill","primary","progress","pulse","right","rose","round","rtl","secondary","sepia","shake","sharp","sine","slate","slideDown","slideLeft","slideRight","slideUp","slow","slower","sm","small","soft","spacious","spring","start","stretch","subtitle","success","sunset","surface","title","tonal","top","underline","warning","xl","xs"]};
  /*LEX:END*/

  var KEYWORDS = ('let print show rinopen if else while for fun return break continue true false nil and or reckon where ' +
    'stone gauge tape lens fuse bell trial undo redo rearm unbell abort warp set define declare create var text').split(' ');
  var BUILTIN_BLOCKS = 'container Containers Group Volume Section row style document route data api import table doc portal block pipe plus condition loop function view chatbot on'.split(' ');

  function toSet(a) { var o = Object.create(null); for (var i = 0; i < a.length; i++) o[a[i]] = 1; return o; }
  var S = {
    kind: toSet((LEX.kinds || []).concat(LEX.aliases || [])), attr: toSet(LEX.attributes || []),
    tok: toSet(LEX.tokenValues || []), kw: toSet(KEYWORDS), blk: toSet(BUILTIN_BLOCKS)
  };

  var RE_ID = /[A-Za-z_][A-Za-z0-9_]*/y;
  function isIdStart(c) { return /[A-Za-z_]/.test(c); }
  function isDigit(c) { return c >= '0' && c <= '9'; }

  function tokenize(src) {
    src = String(src == null ? '' : src);
    var out = [], i = 0, n = src.length, line = 1, lineStart = true;
    function push(type, text) { out.push({ type: type, text: text, line: line }); }
    while (i < n) {
      var c = src.charAt(i), rest;
      if (c === '\n') { push('ws', c); i++; line++; lineStart = true; continue; }
      if (c === ' ' || c === '\t' || c === '\r') { var j = i; while (j < n && ' \t\r'.indexOf(src.charAt(j)) >= 0) j++; push('ws', src.slice(i, j)); i = j; continue; }
      var atStart = lineStart; lineStart = false;
      if (c === '/' && src.charAt(i + 1) === '/') { var e = src.indexOf('\n', i); if (e < 0) e = n; push('comment', src.slice(i, e)); i = e; continue; }
      if (c === '/' && src.charAt(i + 1) === '*') {
        var e2 = src.indexOf('*/', i + 2); e2 = e2 < 0 ? n : e2 + 2; var chunk = src.slice(i, e2);
        push('comment', chunk); line += (chunk.match(/\n/g) || []).length; i = e2; continue;
      }
      if (c === '"' || c === "'" || c === '`') {
        var q = c, k = i + 1;
        while (k < n && src.charAt(k) !== q) { if (src.charAt(k) === '\\') k++; else if (src.charAt(k) === '\n' && q !== '`') break; k++; }
        var end = k < n && src.charAt(k) === q ? k + 1 : k, text = src.slice(i, end), inner = text.slice(1, text.charAt(text.length - 1) === q && text.length > 1 ? -1 : undefined);
        if (q === '"' && S.tok[inner]) push('token', text);
        else if (/^#(?:[0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(inner)) push('color', text);
        else push('string', text);
        line += (text.match(/\n/g) || []).length; i = end; continue;
      }
      if (c === '@') {
        rest = src.slice(i, i + 160);
        var m = /^@view\.([A-Za-z_][\w.]*)(?:(\s*)(=)(\s*)([A-Za-z_][\w.\-]*))?/.exec(rest);
        if (m) {
          push('view', '@view'); push('punct', '.'); push(S.kind[m[1]] ? 'kind' : 'custom', m[1]);
          if (m[3]) { if (m[2]) push('ws', m[2]); push('op', '='); if (m[4]) push('ws', m[4]); push('viewname', m[5]); }
          i += m[0].length; continue;
        }
        m = /^@container(\s*)(=)(\s*)([A-Za-z_][\w.\-]*)/.exec(rest);
        if (m) { push('view', '@container'); if (m[1]) push('ws', m[1]); push('op', '='); if (m[3]) push('ws', m[3]); push('viewname', m[4]); i += m[0].length; continue; }
        m = /^@[A-Za-z_][\w.]*/.exec(rest);
        if (m) { push('view', m[0]); i += m[0].length; continue; }
      }
      if (c === '.' && src.substr(i, 5) === '.end/') {
        m = /^\.end\/(?:view|container)\b/.exec(src.slice(i, i + 20));
        if (m) { push('end', m[0]); i += m[0].length; continue; }
      }
      if (c === '#') {
        m = /^#(?:[0-9a-fA-F]{8}|[0-9a-fA-F]{6})(?![\w])/.exec(src.slice(i, i + 12));
        if (m) { push('color', m[0]); i += m[0].length; continue; }
        m = /^#[A-Za-z_][A-Za-z0-9_]*/.exec(src.slice(i, i + 40));
        if (m) { push('keyword', m[0]); i += m[0].length; continue; }
      }
      if (isDigit(c) || (c === '-' && isDigit(src.charAt(i + 1)) && /[=(,\s:\[]/.test(src.charAt(i - 1) || ' '))) {
        m = /^-?\d+(?:\.\d+)?(px|dp|sp|pt|ms|s|%|vw|vh)?(?![\w])/.exec(src.slice(i, i + 32));
        if (m) { push(m[1] ? 'length' : 'number', m[0]); i += m[0].length; continue; }
      }
      if (isIdStart(c)) {
        var s = i + 1; while (s < n && /[A-Za-z0-9_]/.test(src.charAt(s))) s++;
        var word = src.slice(i, s), next = src.slice(s, s + 40), prev = out.length ? out[out.length - 1] : null;
        var asg = /^(?:\s*)=(?!=)/.test(next);
        if (atStart && asg) {
          var base = word, suf = '';
          var rm = /^(.*?)(_(?:sm|md|lg|xl))$/.exec(word);
          if (rm && rm[1]) { base = rm[1]; suf = rm[2]; }
          push(S.attr[base] ? 'attr' : 'prop', base); if (suf) push('responsive', suf);
        } else if (prev && prev.type === 'punct' && prev.text === '.') push('prop', word);
        else if (S.kw[word]) push('keyword', word);
        else if (S.blk[word] && atStart) push('block', word);
        else if (/^\s*\(/.test(next)) push('func', word);
        else push('ident', word);
        i = s; continue;
      }
      if (c === '{' && src.charAt(i + 1) === '{') {
        var close = src.indexOf('}}', i); if (close > 0 && close - i < 120 && src.slice(i, close).indexOf('\n') < 0) { push('binding', src.slice(i, close + 2)); i = close + 2; continue; }
      }
      if ('=+-*/%<>!&|?:'.indexOf(c) >= 0) { var o = i + 1; while (o < n && '=<>&|'.indexOf(src.charAt(o)) >= 0 && o - i < 3) o++; push('op', src.slice(i, o)); i = o; continue; }
      if ('{}[]();,.'.indexOf(c) >= 0) { push('punct', c); i++; continue; }
      push('text', c); i++;
    }
    return out;
  }

  function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;'); }

  function highlight(src, opt) {
    opt = opt || {};
    var toks = tokenize(src), html = '', theme = opt.theme === 'light' || opt.theme === 'dark' ? ' data-theme="' + opt.theme + '"' : '';
    for (var i = 0; i < toks.length; i++) {
      var t = toks[i];
      html += (t.type === 'ws' || t.type === 'text' || t.type === 'ident') ? esc(t.text) : '<span class="ih-' + t.type + '">' + esc(t.text) + '</span>';
    }
    if (opt.lineNumbers) {
      var lines = html.split('\n'), w = String(lines.length).length;
      html = lines.map(function (l, k) { return '<span class="ih-ln" aria-hidden="true">' + ('        ' + (k + 1)).slice(-w) + '</span>' + l; }).join('\n');
    }
    return '<pre class="ih"' + theme + ' dir="ltr"><code class="ih-code" translate="no">' + html + '</code></pre>';
  }

  var PALETTE = {
    dark:  { bg: '#1e1e2e', fg: '#cdd6f4', comment: '#7f849c', string: '#a6e3a1', number: '#fab387', length: '#fab387', color: '#f5c2e7', keyword: '#cba6f7', view: '#89b4fa', kind: '#f9e2af', custom: '#f38ba8', viewname: '#94e2d5', attr: '#89dceb', prop: '#a6adc8', responsive: '#f5c2e7', token: '#f2cdcd', end: '#89b4fa', block: '#cba6f7', func: '#74c7ec', op: '#9399b2', punct: '#9399b2', binding: '#eba0ac', ln: '#585b70' },
    light: { bg: '#ffffff', fg: '#24292f', comment: '#6b7280', string: '#1d7a4c', number: '#b45f06', length: '#b45f06', color: '#a0348a', keyword: '#6a1b9a', view: '#0b4fcc', kind: '#8a6d00', custom: '#c62828', viewname: '#00796b', attr: '#00668c', prop: '#57606a', responsive: '#a0348a', token: '#b33a3a', end: '#0b4fcc', block: '#6a1b9a', func: '#0b6fa4', op: '#57606a', punct: '#57606a', binding: '#b3261e', ln: '#9ca3af' }
  };
  function vars(p) { var s = ''; for (var k in p) s += '--ih-' + k + ':' + p[k] + ';'; return s; }
  function stylesheet() {
    var types = 'comment string number length color keyword view kind custom viewname attr prop responsive token end block func op punct binding ln'.split(' ');
    var css = '.ih{' + vars(PALETTE.light) + 'margin:0;padding:12px 14px;background:var(--ih-bg);color:var(--ih-fg);border-radius:8px;overflow:auto;font:13px/1.55 ui-monospace,SFMono-Regular,Menlo,Consolas,"Liberation Mono",monospace;tab-size:2}' +
      '@media (prefers-color-scheme:dark){.ih:not([data-theme="light"]){' + vars(PALETTE.dark) + '}}' +
      '.ih[data-theme="dark"]{' + vars(PALETTE.dark) + '}' +
      '.ih-code{font:inherit;white-space:pre}.ih-ln{display:inline-block;margin-right:14px;text-align:right;user-select:none}';
    types.forEach(function (t) { css += '.ih-' + t + '{color:var(--ih-' + t + ')}'; });
    return css + '.ih-comment{font-style:italic}.ih-view,.ih-end,.ih-keyword,.ih-kind{font-weight:600}.ih-responsive{opacity:.9}';
  }

  return { highlight: highlight, tokenize: tokenize, stylesheet: stylesheet, palette: PALETTE, lexicon: { kinds: LEX.kinds, attributes: LEX.attributes, tokenValues: LEX.tokenValues, keywords: KEYWORDS }, version: LEX.version };
}));

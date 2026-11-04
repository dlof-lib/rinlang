// indsin2web_runtime.inc — الجزء الثابت من الصفحة المولَّدة: CSS الأساسي + runtime صغير (بلا اعتماديات).
// يُضمَّن في indsin2web.cpp. أي خاصية يضيفها المترجم إلى kProps يجب أن يقابلها مدخل في الجدول P أدناه
// (يتحقق منه tests/indsin2web/run.sh).

const char* kBaseCss = R"CSS(
*,*::before,*::after{box-sizing:border-box}
:root{--in-primary:#6C5CE7;--in-secondary:#8E8AA8;--in-success:#2EB872;--in-danger:#D14545;--in-warning:#E5A100;--in-info:#2F7DC4;
--in-neutral:#8B899C;--in-surface:#FFFFFF;--in-background:#F5F5FA;--in-text:#1E1E2E;--in-text_muted:#6B6A7A;--in-border:#D9D8E3}
@media(prefers-color-scheme:dark){:root{--in-surface:#1E1E2E;--in-background:#121220;--in-text:#F2F2FA;--in-text_muted:#A9A8BA;--in-border:#3A3A52}}
html,body{margin:0}
body{font-family:system-ui,-apple-system,"Segoe UI",Tahoma,"Noto Sans Arabic",sans-serif;background:var(--in-background);color:var(--in-text);line-height:1.4}
[hidden]{display:none!important}
.in-screen{min-height:100vh}
.in-col,.in-row,.in-box,.in-card,.in-stack,.in-scaffold,.in-header,.in-topbar,.in-bottombar,.in-drawer,.in-menu,.in-list,.in-banner,.in-tabs,.in-dialog,.in-splash{display:flex}
.in-col,.in-box,.in-card,.in-scaffold,.in-drawer,.in-menu,.in-list,.in-dialog,.in-tabs{flex-direction:column;align-items:flex-start}
.in-screen>.in-col,.in-screen>.in-box{align-items:stretch}
.in-row,.in-header,.in-topbar,.in-bottombar,.in-banner{flex-direction:row;align-items:flex-start}
.in-stack{position:relative;display:grid}.in-stack>*{grid-area:1/1}
.in-grid{display:grid;gap:0}.in-wrap{display:flex;flex-wrap:wrap}
.in-card{background:var(--in-surface);border-radius:12px;padding:12px}
.in-list{list-style:none;margin:0;padding:0}
.in-text,.in-link{margin:0}
h1.in-text,h2.in-text,h3.in-text{margin:0}
.in-button,.in-menuitem,.in-tabitem{font:inherit;cursor:pointer;border:0;border-radius:8px;min-height:44px;padding:0 16px;background:var(--in-primary);color:var(--in-on-primary,#fff)}
.in-button:disabled{opacity:.5;cursor:not-allowed}
.in-menuitem,.in-tabitem{background:transparent;color:inherit;text-align:start}
.in-input{font:inherit;padding:10px 12px;border:1px solid var(--in-border);border-radius:8px;background:var(--in-surface);color:inherit}
.in-divider{border:0;border-top:1px solid var(--in-border);width:100%;margin:0}
.in-image{max-width:100%}
.in-progress{width:100%;accent-color:var(--in-primary)}
.in-badge,.in-tag{display:inline-block;padding:2px 8px;border-radius:999px;background:var(--in-primary);color:var(--in-on-primary,#fff);font-size:12px}
.in-avatar{width:40px;height:40px;border-radius:50%;background:var(--in-secondary);display:inline-flex;align-items:center;justify-content:center;color:#fff}
.in-spacer{flex:1}
.in-dialog{position:fixed;inset:0;margin:auto;max-width:90vw;max-height:90vh;height:fit-content;width:fit-content;background:var(--in-surface);padding:24px;border-radius:12px;box-shadow:0 24px 48px rgba(0,0,0,.3)}
.in-webview{border:0;width:100%}
.in-switch{appearance:none;width:44px;height:26px;border-radius:13px;background:var(--in-border);position:relative;cursor:pointer;transition:background .15s}
.in-switch::after{content:"";position:absolute;top:3px;inset-inline-start:3px;width:20px;height:20px;border-radius:50%;background:#fff;transition:transform .15s}
.in-switch:checked{background:var(--in-primary)}.in-switch:checked::after{transform:translateX(18px)}
[dir=rtl] .in-switch:checked::after{transform:translateX(-18px)}
@media(prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
)CSS";

const char* kRuntimeJs = R"JS(
const IN = (() => {
  const $ = id => document.getElementById(id);
  const ROLES = new Set(["primary","secondary","success","danger","warning","info","neutral","surface","background","text","text_muted","border"]);
  const SP = {compact:8, normal:12, comfortable:16, spacious:24}, RAD = {sharp:0, soft:6, round:12, pill:9999};
  const FS = {caption:[12,400], body:[14,400], label:[13,500], subtitle:[16,500], title:[20,600], heading:[28,700], code:[13,400]};
  const ELEV = [[0,0,0,0],[2,1,0,.12],[4,2,0,.14],[8,4,0,.16],[16,8,1,.18],[24,12,2,.22]];
  const ELEVN = {none:0, xs:1, sm:2, md:3, lg:4, xl:5};
  const AL = {left:"flex-start", start:"flex-start", top:"flex-start", center:"center", middle:"center", right:"flex-end", end:"flex-end", bottom:"flex-end", stretch:"stretch"};
  const JU = {start:"flex-start", left:"flex-start", top:"flex-start", center:"center", end:"flex-end", right:"flex-end", bottom:"flex-end",
              space_between:"space-between", between:"space-between", space_around:"space-around", around:"space-around", space_evenly:"space-evenly", evenly:"space-evenly"};
  const num = v => typeof v === "number" ? v : parseFloat(v);
  const px = v => (typeof v === "number" || /^-?\d+(\.\d+)?$/.test(String(v))) ? v + "px" : String(v);
  const sp = v => SP[v] !== undefined ? SP[v] + "px" : px(v);
  const len = v => v === "half" ? "50%" : v === "full" || v === "fill" ? "100%" : px(v);
  const col = v => ROLES.has(v) ? "var(--in-" + v + ")" : String(v);
  // مواصفة تدرّج المحرّك: "c1,c2[,angle]" (الفواصل داخل rgb()/hsl() محترمة) وإلا لون عادي
  const bgCss = v => { v = String(v); if (!v.includes(",") && !/gradient\(/.test(v)) return col(v); if (/gradient\(/.test(v)) return v;
    const parts = []; let d = 0, cur = ""; for (const ch of v) { if (ch === "(") d++; if (ch === ")") d--; if (ch === "," && d === 0) { parts.push(cur.trim()); cur = ""; } else cur += ch; } parts.push(cur.trim());
    let ang = 180; if (parts.length > 2 && /^-?\d+(\.\d+)?$/.test(parts[parts.length - 1])) ang = parseFloat(parts.pop());
    return parts.length < 2 ? col(parts[0]) : "linear-gradient(" + ang + "deg," + parts.map(col).join(",") + ")"; };
  const truthy = v => v === true || v === "true" || (v !== false && v !== "false" && v !== null && v !== undefined && v !== 0 && v !== "");
  const kind = el => (el.className.match(/\bin-([a-z]+)/) || [])[1] || "";
  const isField = k => k === "input" || k === "checkbox" || k === "switch" || k === "slider" || k === "radio";
  const setS = (el, p, v) => { el.style.setProperty(p, v); };
  const box = (el, a, b) => { const k = kind(el); return (k === "col" || k === "row" || k === "box" || k === "card" || k === "stack" || k === "scaffold"); };
  // ثلاثية (الجانب) لخصائص padding/margin
  const side = (name, s) => (el, v) => setS(el, name + "-" + s, sp(v));
  const sides = (name, ss) => (el, v) => ss.forEach(s => setS(el, name + "-" + s, sp(v)));

  const P = {
    text: (el, v) => { if (!isField(kind(el))) el.textContent = v == null ? "" : String(v); },
    label: (el, v) => { const k = kind(el); if (isField(k)) el.setAttribute("aria-label", v); else el.textContent = v == null ? "" : String(v); },
    title: (el, v) => { el.title = v; }, tooltip: (el, v) => { el.title = v; },
    description: (el, v) => el.setAttribute("aria-description", v), ariaLabel: (el, v) => el.setAttribute("aria-label", v), aria_label: (el, v) => el.setAttribute("aria-label", v),
    value: (el, v) => { const k = kind(el);
      if (k === "progress") { el.max = 100; el.value = num(v) || 0; }
      else if (k === "text") el.textContent = v;
      else if (el.type === "checkbox") el.checked = truthy(v);
      else if (String(el.value) !== String(v == null ? "" : v)) el.value = v == null ? "" : v; },
    checked: (el, v) => { el.checked = truthy(v); },
    placeholder: (el, v) => { el.placeholder = v; },
    type: (el, v) => { if (el.tagName === "INPUT" && kind(el) === "input") el.type = v; },
    src: (el, v) => { el.setAttribute("src", v); }, url: (el, v) => { el.setAttribute(el.tagName === "A" ? "href" : "src", v); },
    href: (el, v) => { el.setAttribute("href", v); }, alt: (el, v) => { el.alt = v; }, fit: (el, v) => setS(el, "object-fit", v),
    visible: (el, v) => { el.hidden = !truthy(v); },
    open: (el, v) => { el.hidden = !truthy(v); },
    enabled: (el, v) => { const d = !truthy(v); if ("disabled" in el) el.disabled = d; el.setAttribute("aria-disabled", d); },
    readonly: (el, v) => { el.readOnly = truthy(v); }, autofocus: (el, v) => { if (truthy(v)) el.autofocus = true; },
    multiline: () => {}, rows: (el, v) => { if (el.tagName === "TEXTAREA") el.rows = v; },
    name: (el, v) => { el.setAttribute("name", v); }, id: () => {}, class: (el, v) => { String(v).split(/\s+/).filter(Boolean).forEach(c => el.classList.add(c)); },
    role: (el, v) => el.setAttribute("role", v), lang: (el, v) => el.setAttribute("lang", v), dir: (el, v) => el.setAttribute("dir", v),
    min: (el, v) => { el.min = v; }, max: (el, v) => { el.max = v; }, step: (el, v) => { el.step = v; },
    icon: (el, v) => { el.dataset.icon = v; if (!el.textContent) el.setAttribute("aria-hidden", "true"); },
    ratio: (el, v) => setS(el, "aspect-ratio", String(v).replace(":", "/")),
    aspect: (el, v) => setS(el, "aspect-ratio", String(v).replace(":", "/")),
    width: (el, v) => setS(el, "width", len(v)), height: (el, v) => setS(el, "height", len(v)),
    min_width: (el, v) => setS(el, "min-width", len(v)), max_width: (el, v) => setS(el, "max-width", len(v)),
    min_height: (el, v) => setS(el, "min-height", len(v)), max_height: (el, v) => setS(el, "max-height", len(v)),
    weight: (el, v) => { if (kind(el) === "text") setS(el, "font-weight", v); else setS(el, "flex", num(v) + " 1 0"); },
    flex: (el, v) => setS(el, "flex", v),
    padding: (el, v) => setS(el, "padding", sp(v)), padding_left: side("padding", "left"), padding_right: side("padding", "right"),
    padding_top: side("padding", "top"), padding_bottom: side("padding", "bottom"),
    padding_x: sides("padding", ["left", "right"]), padding_y: sides("padding", ["top", "bottom"]),
    margin: (el, v) => setS(el, "margin", sp(v)), margin_left: side("margin", "left"), margin_right: side("margin", "right"),
    margin_top: side("margin", "top"), margin_bottom: side("margin", "bottom"),
    margin_x: sides("margin", ["left", "right"]), margin_y: sides("margin", ["top", "bottom"]),
    gap: (el, v) => setS(el, "gap", sp(v)), row_gap: (el, v) => setS(el, "row-gap", sp(v)), column_gap: (el, v) => setS(el, "column-gap", sp(v)),
    align: (el, v) => { const k = kind(el);
      if (k === "text" || k === "button" || k === "link") { setS(el, "text-align", v === "center" ? "center" : (v === "right" || v === "end") ? "end" : "start"); return; }
      if (k === "row" || k === "header" || k === "topbar" || k === "bottombar") { setS(el, "justify-content", JU[v] || v); return; }
      if (k === "box" || k === "card") { setS(el, "align-items", AL[v] || v); return; }
      setS(el, "align-items", AL[v] || v); },
    valign: (el, v) => { const k = kind(el); setS(el, k === "col" || k === "box" || k === "card" ? "justify-content" : "align-items", (JU[v] || AL[v] || v)); },
    justify: (el, v) => setS(el, kind(el) === "row" ? "justify-content" : "justify-content", JU[v] || v),
    direction: (el, v) => { setS(el, "flex-direction", v === "row" || v === "horizontal" ? "row" : "column"); },
    columns: (el, v) => { setS(el, "display", "grid"); setS(el, "grid-template-columns", "repeat(" + num(v) + ", minmax(0, 1fr))"); },
    wrap: (el, v) => { if (kind(el) === "text") setS(el, "white-space", truthy(v) ? "normal" : "nowrap"); else setS(el, "flex-wrap", truthy(v) ? "wrap" : "nowrap"); },
    background: (el, v) => setS(el, "background", bgCss(v)), bg: (el, v) => setS(el, "background", bgCss(v)),
    fill: (el, v) => setS(el, "background", bgCss(v)),
    canvas: (el, v) => { const b = bgCss(v); el.style.background = b; document.body.style.background = b; },
    color: (el, v) => { const k = kind(el);
      if ((k === "button" || k === "badge" || k === "tag") && !el.dataset.hasBg) setS(el, "background", col(v)); else setS(el, "color", col(v)); },
    textColor: (el, v) => setS(el, "color", col(v)),
    radius: (el, v) => setS(el, "border-radius", RAD[v] !== undefined ? RAD[v] + "px" : px(v)),
    border: (el, v) => { setS(el, "border", px(v) + " solid var(--in-border)"); }, border_color: (el, v) => setS(el, "border-color", col(v)),
    opacity: (el, v) => setS(el, "opacity", v),
    elevation: (el, v) => { let l = typeof v === "number" ? v : (ELEVN[v] !== undefined ? ELEVN[v] : parseInt(v) || 0); l = Math.max(0, Math.min(5, l));
      const e = ELEV[l]; setS(el, "box-shadow", l ? "0 " + e[1] + "px " + e[0] + "px " + e[2] + "px rgba(0,0,0," + (e[3] * 2) + ")" : "none"); },
    shadow: (el, v) => { const n = num(v); if (n > 0) setS(el, "box-shadow", "0 " + n / 2.5 + "px " + n + "px rgba(0,0,0,.47)"); },
    shadowColor: (el, v) => setS(el, "--in-shadow-color", col(v)), shadowY: () => {},
    z: (el, v) => { setS(el, "z-index", v); if (!el.style.position) setS(el, "position", "relative"); },
    size: (el, v) => { const t = FS[v]; if (t) { setS(el, "font-size", t[0] + "px"); setS(el, "font-weight", t[1]); } else setS(el, "font-size", px(v)); },
    bold: (el, v) => setS(el, "font-weight", truthy(v) ? 700 : 400), italic: (el, v) => setS(el, "font-style", truthy(v) ? "italic" : "normal"),
    underline: (el, v) => setS(el, "text-decoration", truthy(v) ? "underline" : "none"),
    font: (el, v) => setS(el, "font-family", v), family: (el, v) => setS(el, "font-family", v),
    line_height: (el, v) => setS(el, "line-height", v), letter_spacing: (el, v) => setS(el, "letter-spacing", px(v)),
    transform: (el, v) => setS(el, "text-transform", v === "upper" ? "uppercase" : v === "lower" ? "lowercase" : v === "title" ? "capitalize" : v),
    max_lines: (el, v) => { setS(el, "display", "-webkit-box"); setS(el, "-webkit-line-clamp", v); setS(el, "-webkit-box-orient", "vertical"); setS(el, "overflow", "hidden"); },
    variant: (el, v) => { el.dataset.variant = v; const k = kind(el);
      if (k === "button") { if (v === "outline" || v === "outlined") { setS(el, "background", "transparent"); setS(el, "color", "var(--in-primary)"); setS(el, "border", "1px solid var(--in-primary)"); }
        else if (v === "ghost" || v === "text" || v === "link") { setS(el, "background", "transparent"); setS(el, "color", "var(--in-primary)"); }
        else if (v === "gradient") setS(el, "background", "linear-gradient(135deg,var(--in-primary),var(--in-info))"); } },
    tone: (el, v) => { const k = kind(el); const c = "var(--in-" + (ROLES.has(v) ? v : "primary") + ")";
      if (k === "progress") setS(el, "accent-color", c); else if (k === "button" || k === "badge" || k === "tag") { if (!el.dataset.hasBg) setS(el, "background", c); } else setS(el, "color", c); },
    gradient: (el, v) => setS(el, "background", bgCss(Array.isArray(v) ? v.join(",") : v)),
    message: (el, v) => { el.textContent = v == null ? "" : String(v); }, initials: (el, v) => { el.textContent = v == null ? "" : String(v); el.setAttribute("aria-label", v); },
    maxlength: (el, v) => { el.maxLength = num(v); }, secure: (el, v) => { if (el.tagName === "INPUT") el.type = truthy(v) ? "password" : "text"; },
    loading: (el, v) => { const l = truthy(v); el.setAttribute("aria-busy", l); if ("disabled" in el) el.disabled = l; el.style.opacity = l ? .6 : ""; },
    error: (el, v) => { const e = v != null && v !== "" && v !== false; el.setAttribute("aria-invalid", e); el.style.borderColor = e ? "var(--in-danger)" : ""; if (typeof v === "string") el.title = v; },
    cursor: (el, v) => setS(el, "cursor", v), overflow: (el, v) => setS(el, "overflow", v === "scroll" ? "auto" : v),
    position: (el, v) => setS(el, "position", v), left: (el, v) => setS(el, "left", px(v)), top: (el, v) => setS(el, "top", px(v)),
    right: (el, v) => setS(el, "right", px(v)), bottom: (el, v) => setS(el, "bottom", px(v)),
    rotate: (el, v) => setS(el, "rotate", num(v) + "deg"), scale: (el, v) => setS(el, "scale", v), display: (el, v) => setS(el, "display", v)
  };
  const FALLBACK = (el, key, v) => { el.dataset["in" + key.replace(/(^|_)(\w)/g, (m, a, b) => b.toUpperCase())] = v; };

  function state(init) {
    const raw = Object.assign({}, init); let queued = false;
    const flush = () => { queued = false; run(); };
    const px_ = new Proxy(raw, {
      get: (t, k) => t[k],
      set: (t, k, v) => { if (!Object.is(t[k], v)) { t[k] = v; if (!queued) { queued = true; queueMicrotask(flush); } } return true; }
    });
    state.proxy = px_; state.raw = raw; return px_;
  }
  function actions(S) {
    const num0 = v => Number(v) || 0;
    const flag = val => c => { S[c] = val; };
    return {
      increment: (c, n = 1) => { S[c] = num0(S[c]) + num0(n); }, decrement: (c, n = 1) => { S[c] = num0(S[c]) - num0(n); },
      toggle: c => { S[c] = !truthy(S[c]); }, set: (c, v) => { S[c] = v; }, seek: (c, v) => { S[c] = num0(v); }, scrollTo: (c, v) => { S[c] = num0(v); },
      show: flag(true), hide: flag(false), enable: flag(true), disable: flag(false), select: flag(true), unselect: flag(false),
      focus: flag(true), blur: flag(false), reset: (c, d) => { S[c] = d !== undefined ? d : (typeof S[c] === "number" ? 0 : ""); },
      submit: c => { S[c + "_submitted"] = true; }, multiply: (c, n = 2) => { S[c] = num0(S[c]) * num0(n); }, negate: c => { S[c] = -num0(S[c]); },
      swap: (a, b) => { const t = S[a]; S[a] = S[b]; S[b] = t; }, copy: (d, s) => { S[d] = S[s]; },
      cycle: (c, ...vs) => { const i = vs.findIndex(v => String(v) === String(S[c])); S[c] = vs[(i + 1) % vs.length]; },
      clamp: (c, lo, hi) => { if (lo > hi) [lo, hi] = [hi, lo]; S[c] = Math.min(hi, Math.max(lo, num0(S[c]))); },
      append: (c, t) => { S[c] = String(S[c] == null ? "" : S[c]) + t; }, backspace: c => { S[c] = Array.from(String(S[c] || "")).slice(0, -1).join(""); },
      seq: () => {}
    };
  }
  const natives = {
    len: v => v == null ? 0 : v.length !== undefined ? v.length : Object.keys(v).length, str: v => v == null ? "" : String(v), num: v => Number(v), int: v => Math.trunc(Number(v)),
    upper: v => String(v).toUpperCase(), lower: v => String(v).toLowerCase(), trim: v => String(v).trim(),
    round: (v, d = 0) => { const m = Math.pow(10, d); return Math.round(v * m) / m; }, floor: Math.floor, ceil: Math.ceil, abs: Math.abs,
    min: (...a) => Math.min(...a.flat()), max: (...a) => Math.max(...a.flat()), sum: a => a.reduce((x, y) => x + y, 0),
    sqrt: Math.sqrt, pow: Math.pow, sin: Math.sin, cos: Math.cos, tan: Math.tan, random: () => Math.random(),
    contains: (h, n) => h != null && h.includes(n), join: (a, s = "") => a.join(s), split: (s, d = "") => String(s).split(d),
    keys: o => Object.keys(o), values: o => Object.values(o), range: (a, b, s = 1) => { if (b === undefined) { b = a; a = 0; } const r = []; for (let i = a; s > 0 ? i < b : i > b; i += s) r.push(i); return r; },
    push: (a, v) => { a.push(v); return a; }, reverse: a => a.slice().reverse(), now: () => Date.now(), time: () => Date.now() / 1000,
    startsWith: (s, p) => String(s).startsWith(p), endsWith: (s, p) => String(s).endsWith(p), replace: (s, a, b) => String(s).split(a).join(b),
    isNil: v => v == null, typeof: v => v === null ? "nil" : Array.isArray(v) ? "array" : typeof v, clamp: (v, lo, hi) => Math.min(hi, Math.max(lo, v)),
    print: (...a) => console.log(...a),
    substr: (s, st, n) => { const a = Array.from(String(s)); return (n === undefined ? a.slice(st) : a.slice(st, st + n)).join(""); },
    indexOf: (s, x) => String(s).indexOf(x), repeat: (s, n) => String(s).repeat(Math.max(0, n)),
    capitalize: s => { s = String(s); return s.charAt(0).toUpperCase() + s.slice(1); },
    iter: v => typeof v === "number" ? Array.from({length: v}, (_, i) => i) : typeof v === "string" ? Array.from(v) : Array.isArray(v) ? v : Object.keys(v || {}),
    copyOf: (a, b) => a !== b && JSON.stringify(a) === JSON.stringify(b),
    method: (o, m, ...a) => { if (o == null) throw new TypeError("nil." + m + "()"); const f = o[m]; if (typeof f === "function") return f.apply(o, a);
      const alias = {append: "push", length: "length", upper: "toUpperCase", lower: "toLowerCase", trim: "trim", has: "includes", contains: "includes"}[m];
      if (alias && typeof o[alias] === "function") return o[alias](...a); throw new TypeError("method " + m + " is not available on this value"); }
  };

  // نقل دوال indsin.* النقية (نماذج وألوان) من rin_indsin_rinlib.h / rin_indsin_validate.h
  const looksEmail = s => { const at = s.indexOf("@"); if (at <= 0 || s.indexOf("@", at + 1) >= 0 || /[\x00-\x20]/.test(s)) return false; const d = s.indexOf(".", at + 1); return d > at + 1 && d + 1 < s.length && !s.endsWith("."); };
  const looksPhone = s => { let n = 0; for (let i = 0; i < s.length; i++) { const c = s[i]; if (c >= "0" && c <= "9") n++; else if (!(c === " " || c === "-" || c === "(" || c === ")" || (c === "+" && i === 0))) return false; } return n >= 7 && n <= 15; };
  const looksUrl = s => /^https?:\/\/\S{3,}$/.test(s);
  const looksNumber = s => /^[+-]?(\d+\.?\d*|\.\d+)$/.test(s);
  const looksDate = s => { if (!/^\d{4}-\d{2}-\d{2}$/.test(s)) return false; const [y, m, d] = s.split("-").map(Number); const dm = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]; if (m < 1 || m > 12 || d < 1) return false; return d <= dm[m - 1] + (m === 2 && ((y % 4 === 0 && y % 100 !== 0) || y % 400 === 0) ? 1 : 0); };
  const looksTime = s => { if (!/^\d{2}:\d{2}(:\d{2})?$/.test(s)) return false; const [h, m, x] = s.split(":").map(Number); return h < 24 && m < 60 && (x || 0) < 60; };
  const pwScore = s => { s = String(s == null ? "" : s); if (!s) return 0; const lo = /[a-z]/.test(s), up = /[A-Z]/.test(s), dg = /[0-9]/.test(s), sy = /[^a-zA-Z0-9]/.test(s);
    const cl = lo + up + dg + sy; let pts = (s.length >= 8) + (s.length >= 12) + (cl >= 2) + (cl >= 3) + (cl >= 4); if (s.length < 6) pts = Math.min(pts, 1); return Math.min(4, pts); };
  const hexRgb = h => { h = String(h).replace("#", ""); if (h.length === 3) h = h.split("").map(c => c + c).join(""); return [0, 2, 4].map(i => parseInt(h.substr(i, 2), 16) || 0); };
  const lum = c => { const f = v => { v /= 255; return v <= .03928 ? v / 12.92 : Math.pow((v + .055) / 1.055, 2.4); }; return .2126 * f(c[0]) + .7152 * f(c[1]) + .0722 * f(c[2]); };
  const indsin = {
    isEmail: looksEmail, isPhone: looksPhone, isUrl: looksUrl, isNumber: looksNumber, passwordScore: pwScore,
    passwordLabel: s => s ? ["weak", "weak", "fair", "good", "strong"][pwScore(s)] : "empty",
    passwordTone: s => ["danger", "danger", "warning", "info", "success"][pwScore(s)],
    maskEmail: e => { e = String(e); const at = e.indexOf("@"); return at < 2 ? e : e[0] + "*".repeat(at - 1) + e.slice(at); },
    initials: n => String(n).split(/\s+/).filter(Boolean).slice(0, 3).map(w => Array.from(w)[0].toUpperCase()).join(""),
    slug: n => String(n).toLowerCase().replace(/[^a-z0-9\u0080-\uffff]+/g, "-").replace(/^-|-$/g, ""),
    fieldError: (k, v, lo, hi) => { v = String(v == null ? "" : v);
      if (k === "required") return v ? "" : "Required"; if (!v) return ""; if (k === "password") return v.length >= 6 ? "" : "At least 6 characters";
      if (k === "email") return looksEmail(v) ? "" : "Enter a valid email"; if (k === "phone" || k === "tel") return looksPhone(v) ? "" : "Enter a valid phone number";
      if (k === "url") return looksUrl(v) ? "" : "Enter a valid URL"; if (k === "date") return looksDate(v) ? "" : "Enter a valid date (YYYY-MM-DD)";
      if (k === "time") return looksTime(v) ? "" : "Enter a valid time (HH:MM)";
      if (k === "number") { if (!looksNumber(v)) return "Enter a valid number"; const n = Number(v); if (typeof lo === "number" && n < lo) return "Must be at least " + Math.trunc(lo); if (typeof hi === "number" && n > hi) return "Must be at most " + Math.trunc(hi); }
      return ""; },
    themeColor: r => "var(--in-" + r + ")", onColor: c => lum(hexRgb(c)) > .179 ? "#000000" : "#ffffff", isLight: c => lum(hexRgb(c)) > .179
  };

  let B = [], lastV = [];
  function run() {
    for (let i = 0; i < B.length; i++) {
      const b = B[i]; let v;
      try { v = typeof b[2] === "function" ? b[2]() : b[2]; } catch (e) { console.error("[indsin] " + b[0] + "." + b[1] + ":", e); continue; }
      const sv = typeof v === "object" ? JSON.stringify(v) : v;
      if (i in lastV && lastV[i] === sv) continue;
      lastV[i] = sv;
      const el = $(b[0]); if (!el) continue;
      if (b[1] === "bg" || b[1] === "background") el.dataset.hasBg = "1";
      (P[b[1]] || ((e, v2) => FALLBACK(e, b[1], v2)))(el, v);
    }
  }
  function boot(S, bindings, handlers) {
    B = bindings;
    // الخلفية أولاً كي يعرف color الزر أنه نص لا تعبئة
    B.forEach(b => { if (b[1] === "bg" || b[1] === "background") { const e = $(b[0]); if (e) e.dataset.hasBg = "1"; } });
    handlers.forEach(h => {
      const el = $(h[0]); if (!el) return;
      if (h[1] === "__bind__") {
        const name = h[2], key = h[3];
        if (key === "checked") el.addEventListener("change", () => { S[name] = el.checked; });
        else el.addEventListener("input", () => { const cur = S[name]; S[name] = (typeof cur === "number" || el.type === "range" || el.type === "number") && el.value !== "" ? Number(el.value) : el.value; });
        return;
      }
      el.addEventListener(h[1], ev => { if (h[1] === "submit") ev.preventDefault(); try { h[2](ev); } catch (e) { console.error("[indsin] handler:", e); } });
    });
    run();
  }
  return {state, actions, natives, boot, P, indsin};
})();
)JS";

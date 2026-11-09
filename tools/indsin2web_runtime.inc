// indsin2web_runtime.inc — الجزء الثابت من الصفحة المولَّدة: CSS الأساسي + runtime صغير (بلا اعتماديات).
// يُضمَّن في indsin2web.cpp. أي خاصية يضيفها المترجم إلى kProps يجب أن يقابلها مدخل في الجدول P أدناه
// (يتحقق منه tests/indsin2web/run.sh).

const char* kBaseCss = R"CSS(
*,*::before,*::after{box-sizing:border-box}
:root{--in-primary:#6C5CE7;--in-secondary:#8E8AA8;--in-success:#2EB872;--in-danger:#D14545;--in-warning:#E5A100;--in-info:#2F7DC4;
--in-neutral:#8B899C;--in-surface:#FFFFFF;--in-background:#F5F5FA;--in-text:#1E1E2E;--in-text_muted:#6B6A7A;--in-border:#D9D8E3}
:root{color-scheme:light dark}
@media(prefers-color-scheme:dark){:root:not([data-theme=light]){--in-surface:#1E1E2E;--in-background:#121220;--in-text:#F2F2FA;--in-text_muted:#A9A8BA;--in-border:#3A3A52}}
:root[data-theme=dark]{color-scheme:dark;--in-surface:#1E1E2E;--in-background:#121220;--in-text:#F2F2FA;--in-text_muted:#A9A8BA;--in-border:#3A3A52}
:root[data-theme=light]{color-scheme:light}
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

/* ===== Pro layer: حالات، حركة، مكوّنات إضافية ===== */
:focus-visible{outline:2px solid var(--in-primary);outline-offset:2px}
::selection{background:color-mix(in srgb,var(--in-primary) 30%,transparent)}
.in-hbg:hover{background:var(--h-bg)!important}.in-hco:hover{color:var(--h-color)!important}
.in-hsc:hover{scale:var(--h-scale)!important}.in-hop:hover{opacity:var(--h-opacity)!important}
.in-hsh:hover{box-shadow:var(--h-shadow)!important}.in-hlf:hover{translate:0 var(--h-lift)!important}
.in-asc:active{scale:var(--a-scale)!important}.in-fr:focus-visible{outline:2px solid var(--fr,var(--in-primary))!important;outline-offset:2px}
.in-snap>*{scroll-snap-align:start;flex:0 0 auto}
@keyframes in-fadeIn{from{opacity:0}to{opacity:1}}
@keyframes in-slideUp{from{opacity:0;transform:translateY(16px)}to{opacity:1;transform:none}}
@keyframes in-slideDown{from{opacity:0;transform:translateY(-16px)}to{opacity:1;transform:none}}
@keyframes in-slideLeft{from{opacity:0;transform:translateX(16px)}to{opacity:1;transform:none}}
@keyframes in-slideRight{from{opacity:0;transform:translateX(-16px)}to{opacity:1;transform:none}}
@keyframes in-scaleIn{from{opacity:0;transform:scale(.9)}to{opacity:1;transform:none}}
@keyframes in-pulse{0%,100%{transform:scale(1)}50%{transform:scale(1.06)}}
@keyframes in-shake{0%,100%{transform:none}20%,60%{transform:translateX(-6px)}40%,80%{transform:translateX(6px)}}
@keyframes in-bounce{0%,100%{transform:none}40%{transform:translateY(-12px)}60%{transform:translateY(-6px)}}
@keyframes in-spin{to{transform:rotate(360deg)}}
@keyframes in-float{0%,100%{transform:translateY(0)}50%{transform:translateY(-8px)}}
@keyframes in-blink{0%,100%{opacity:1}50%{opacity:.25}}
@keyframes in-shimmer{from{background-position:200% 0}to{background-position:-200% 0}}
.in-skeleton{min-height:16px;width:100%;border-radius:8px;background:linear-gradient(90deg,var(--in-border) 25%,color-mix(in srgb,var(--in-border) 40%,var(--in-surface)) 50%,var(--in-border) 75%);background-size:200% 100%;animation:in-shimmer 1.4s linear infinite}
.in-spinner{width:24px;height:24px;border-radius:50%;border:3px solid var(--in-border);border-top-color:var(--in-primary);animation:in-spin .8s linear infinite}
.in-tooltip{display:inline-block;padding:4px 8px;border-radius:6px;background:var(--in-text);color:var(--in-background);font-size:12px}
.in-icon{display:inline-block}
.in-table{display:flex;flex-direction:column;width:100%;border:1px solid var(--in-border);border-radius:8px;overflow:hidden}
.in-tablerow{display:flex}.in-tablerow>*{flex:1;padding:8px 12px;margin:0}
.in-tablerow+.in-tablerow{border-top:1px solid var(--in-border)}
.in-tablerow[data-variant=header]{font-weight:600;background:color-mix(in srgb,var(--in-text) 6%,transparent)}
.in-tabs{flex-direction:row;align-items:stretch;border-bottom:1px solid var(--in-border)}
.in-tabitem{border-radius:0;min-height:40px;border-bottom:2px solid transparent}
.in-tabitem[aria-selected=true]{border-bottom-color:var(--in-primary);color:var(--in-primary);font-weight:600}
.in-menuitem[aria-current=true]{color:var(--in-primary);font-weight:600}
.in-accordion{width:100%;border:1px solid var(--in-border);border-radius:10px;padding:0;background:var(--in-surface)}
.in-accordion>.in-summary{cursor:pointer;padding:12px 16px;font-weight:600;list-style:none;display:flex;justify-content:space-between;align-items:center}
.in-accordion>.in-summary::-webkit-details-marker{display:none}
.in-accordion>.in-summary::after{content:"\25BE";transition:transform .15s}.in-accordion[open]>.in-summary::after{transform:rotate(180deg)}
.in-accordion>*:not(summary){margin:0 16px 12px}
.in-stat{display:flex}.in-section,.in-main,.in-article,.in-sidebar,.in-footer,.in-nav,.in-form,.in-hero,.in-empty,.in-center,.in-container,.in-timeline,.in-carousel,.in-sheet,.in-fab{display:flex}
.in-section,.in-main,.in-article,.in-sidebar,.in-form,.in-timeline,.in-container{flex-direction:column;align-items:flex-start}
.in-footer,.in-nav,.in-carousel{flex-direction:row;align-items:flex-start}
.in-container{width:100%;max-width:1200px;margin-inline:auto;padding-inline:16px}
.in-center{align-items:center;justify-content:center;flex-direction:column;text-align:center}
.in-hero{flex-direction:column;align-items:center;justify-content:center;text-align:center;padding:64px 24px;gap:16px}
.in-empty{flex-direction:column;align-items:center;text-align:center;padding:32px;gap:8px;color:var(--in-text_muted)}
.in-sidebar{width:260px;flex:0 0 auto;padding:16px;border-inline-end:1px solid var(--in-border);background:var(--in-surface)}
.in-carousel{overflow-x:auto;scroll-snap-type:x mandatory;gap:12px;width:100%}.in-carousel>*{flex:0 0 auto;scroll-snap-align:start}
.in-timeline{border-inline-start:2px solid var(--in-border);padding-inline-start:16px;gap:16px;margin:0}
.in-timeline{list-style:none}.in-timelineitem{position:relative;list-style:none}.in-timelineitem::before{content:"";position:absolute;inset-inline-start:-22px;top:7px;width:10px;height:10px;border-radius:50%;background:var(--in-primary)}
.in-code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;background:color-mix(in srgb,var(--in-text) 8%,transparent);padding:2px 6px;border-radius:4px}
pre.in-code{padding:12px 16px;border-radius:8px;overflow:auto;width:100%;margin:0}
.in-quote{margin:0;padding:4px 16px;border-inline-start:4px solid var(--in-primary);color:var(--in-text_muted);font-style:italic}
.in-sheet{position:fixed;inset-inline:0;bottom:0;flex-direction:column;align-items:stretch;max-height:85vh;overflow:auto;background:var(--in-surface);padding:20px 24px 28px;border-radius:20px 20px 0 0;box-shadow:0 -12px 32px rgba(0,0,0,.25);z-index:500}
.in-fab{position:fixed;inset-inline-end:20px;bottom:20px;width:56px;height:56px;border-radius:50%;align-items:center;justify-content:center;padding:0;box-shadow:0 8px 20px rgba(0,0,0,.3);z-index:400}
.in-meter{width:100%}.in-label{font-size:13px;font-weight:500}
.in-rating{display:inline-flex;gap:2px}.in-star{background:none;border:0;padding:0 2px;font-size:22px;line-height:1;cursor:pointer;color:var(--in-border)}
.in-star.on{color:#F5B301}.in-rating[data-readonly=true] .in-star{cursor:default;pointer-events:none}
.in-steps{display:flex;list-style:none;margin:0;padding:0;gap:8px;counter-reset:s;width:100%}
.in-steps>li{flex:1;display:flex;flex-direction:column;align-items:center;gap:6px;font-size:13px;color:var(--in-text_muted);counter-increment:s;position:relative}
.in-steps>li::before{content:counter(s);width:28px;height:28px;border-radius:50%;display:flex;align-items:center;justify-content:center;background:var(--in-border);color:var(--in-text);font-weight:600}
.in-steps>li[data-state=done]::before{content:"\2713";background:var(--in-success);color:#fff}
.in-steps>li[data-state=current]{color:var(--in-text);font-weight:600}.in-steps>li[data-state=current]::before{background:var(--in-primary);color:var(--in-on-primary,#fff)}
.in-breadcrumb{display:flex;flex-wrap:wrap;gap:6px;align-items:center;font-size:14px}.in-breadcrumb .sep{color:var(--in-text_muted)}
.in-breadcrumb [aria-current=page]{font-weight:600}
.in-pagination{display:flex;gap:4px;flex-wrap:wrap}
.in-pagination button{min-width:36px;height:36px;border:1px solid var(--in-border);border-radius:8px;background:var(--in-surface);color:inherit;font:inherit;cursor:pointer}
.in-pagination button[aria-current=page]{background:var(--in-primary);color:var(--in-on-primary,#fff);border-color:var(--in-primary)}
.in-pagination button:disabled{opacity:.4;cursor:not-allowed}
.in-toasts{position:fixed;inset-inline:0;bottom:16px;display:flex;flex-direction:column;align-items:center;gap:8px;pointer-events:none;z-index:1000}
.in-toast{background:var(--in-text);color:var(--in-background);padding:10px 16px;border-radius:10px;box-shadow:0 8px 24px rgba(0,0,0,.3);animation:in-slideUp .25s ease both;max-width:90vw}
.in-sr{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0);white-space:nowrap}
@media print{.in-fab,.in-toasts,.in-sheet{display:none!important}}

/* ===== Pro 2: tooltip، كشف عند التمرير، رسوم، جداول، مسارات ===== */
:where(.in-tip){position:relative}
.in-tip:hover::after,.in-tip:focus-visible::after{content:attr(data-tip);position:absolute;bottom:calc(100% + 8px);left:50%;transform:translateX(-50%);background:var(--in-text);color:var(--in-background);padding:4px 10px;border-radius:6px;font-size:12px;font-weight:500;line-height:1.4;white-space:nowrap;pointer-events:none;z-index:900;box-shadow:0 4px 12px rgba(0,0,0,.25)}
.in-tip[data-tip-pos=bottom]:hover::after,.in-tip[data-tip-pos=bottom]:focus-visible::after{bottom:auto;top:calc(100% + 8px)}
.in-hbd:hover{border-color:var(--h-bd)!important}.in-fbg:focus,.in-fbg:focus-within{background:var(--f-bg)!important}.in-fbd:focus,.in-fbd:focus-within{border-color:var(--f-bd)!important}
.in-scrim{box-shadow:0 0 0 100vmax rgba(0,0,0,.5),0 24px 48px rgba(0,0,0,.3)!important}
.in-stat{display:flex;flex-direction:column;gap:2px;padding:16px 20px;background:var(--in-surface);border:1px solid var(--in-border);border-radius:14px;min-width:150px}
.in-stat .sl{font-size:13px;color:var(--in-text_muted)}.in-stat .sv{font-size:30px;font-weight:700;line-height:1.2}
.in-stat .sd{font-size:13px;font-weight:600}.in-stat .sd.up{color:var(--in-success)}.in-stat .sd.down{color:var(--in-error)}
.in-progressring{--p:0;width:84px;height:84px;border-radius:50%;background:conic-gradient(var(--in-primary) calc(var(--p)*1%),var(--in-border) 0);display:grid;place-items:center;position:relative;font-weight:700;flex:0 0 auto}
.in-progressring::before{content:"";position:absolute;inset:9px;border-radius:50%;background:var(--in-surface)}.in-progressring span{position:relative;font-size:16px}
.in-sparkline{display:inline-block;width:120px;height:32px}.in-sparkline svg,.in-barchart svg{width:100%;height:100%;display:block;overflow:visible}
.in-barchart{width:100%;height:180px;display:flex;flex-direction:column}.in-barchart .bc-plot{position:relative;flex:1;min-height:0;margin-top:14px}.in-barchart .bc-plot svg{position:absolute;inset:0}.in-barchart .bc-val{position:absolute;transform:translateX(-50%);font-size:12px;color:var(--in-text)}.in-barchart .bc-labels{display:flex;font-size:12px;color:var(--in-text_muted);margin-top:4px}.in-barchart .bc-labels span{flex:1;text-align:center}
.in-donut{width:120px;height:120px;border-radius:50%;position:relative;flex:0 0 auto}.in-donut::before{content:"";position:absolute;inset:22%;border-radius:50%;background:var(--in-surface)}
.in-legend{display:flex;flex-wrap:wrap;gap:6px 14px;font-size:12px;color:var(--in-text_muted);margin-top:8px}.in-legend i{display:inline-block;width:10px;height:10px;border-radius:3px;margin-inline-end:6px}
.in-datatable{width:100%;overflow:auto;border:1px solid var(--in-border);border-radius:12px;background:var(--in-surface)}
.in-datatable table{width:100%;border-collapse:collapse;font-size:14px}.in-datatable th,.in-datatable td{padding:10px 14px;text-align:start;border-bottom:1px solid var(--in-border)}
.in-datatable th{font-weight:600;cursor:pointer;user-select:none;white-space:nowrap;background:color-mix(in srgb,var(--in-text) 5%,transparent)}
.in-datatable th[aria-sort=ascending]::after{content:" \25B2";font-size:10px}.in-datatable th[aria-sort=descending]::after{content:" \25BC";font-size:10px}
.in-datatable tbody tr:hover{background:color-mix(in srgb,var(--in-primary) 7%,transparent)}.in-datatable tbody tr:last-child td{border-bottom:0}
.in-datatable .dt-foot{display:flex;justify-content:space-between;align-items:center;padding:8px 14px;font-size:13px;color:var(--in-text_muted)}.in-datatable .dt-foot button{margin-inline-start:6px;padding:4px 10px;border:1px solid var(--in-border);border-radius:6px;background:var(--in-surface);color:inherit;cursor:pointer}
.in-datatable .dt-foot button:disabled{opacity:.4;cursor:not-allowed}.in-datatable .dt-empty{padding:24px;text-align:center;color:var(--in-text_muted)}
.in-dropdown{position:relative;display:inline-block}.in-dropdown>summary{list-style:none;cursor:pointer;padding:8px 14px;border:1px solid var(--in-border);border-radius:8px;background:var(--in-surface)}
.in-dropdown>summary::-webkit-details-marker{display:none}.in-dropdown>summary::after{content:" \25BE"}
.in-dropdown>.in-dd-panel{white-space:nowrap;position:absolute;top:calc(100% + 6px);inset-inline-start:0;min-width:100%;background:var(--in-surface);border:1px solid var(--in-border);border-radius:10px;padding:6px;box-shadow:0 12px 28px rgba(0,0,0,.2);z-index:600;display:flex;flex-direction:column;align-items:stretch;gap:2px}
.in-marquee{display:flex;overflow:hidden;width:100%}.in-marquee-track{display:flex;flex:0 0 auto;gap:32px;padding-inline-end:32px;animation:in-marq var(--mq,20s) linear infinite}
.in-marquee:hover .in-marquee-track{animation-play-state:paused}@keyframes in-marq{to{transform:translateX(-100%)}}[dir=rtl] .in-marquee-track{animation-direction:reverse}
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

  // ---- مكوّنات مركّبة تُبنى بالـ DOM الآمن (بلا innerHTML) ----
  const mk = (tag, cls, txt) => { const n = document.createElement(tag); if (cls) n.className = cls; if (txt != null) n.textContent = txt; return n; };
  function renderRating(el) {
    const max = Math.max(1, Math.min(10, Math.trunc(Number(el.dataset.max) || 5))), val = Number(el.dataset.val) || 0;
    el.replaceChildren(); el.setAttribute("role", "radiogroup"); el.setAttribute("aria-label", val + " / " + max);
    for (let i = 1; i <= max; i++) {
      const b = mk("button", "in-star" + (i <= val ? " on" : ""), "★"); b.type = "button"; b.setAttribute("aria-label", String(i));
      b.addEventListener("click", () => { el.dataset.val = i; renderRating(el); el.dispatchEvent(new CustomEvent("in-change", {detail: i})); });
      el.appendChild(b);
    }
  }
  function renderSteps(el) {
    let items = []; try { items = JSON.parse(el.dataset.items || "[]"); } catch (e) {}
    const cur = Math.trunc(Number(el.dataset.current) || 0);
    el.replaceChildren(); el.setAttribute("role", "list");
    items.forEach((t, i) => { const li = mk("li", "", String(t)); li.dataset.state = i < cur ? "done" : i === cur ? "current" : "todo"; if (i === cur) li.setAttribute("aria-current", "step"); el.appendChild(li); });
  }
  function renderCrumbs(el) {
    let items = []; try { items = JSON.parse(el.dataset.items || "[]"); } catch (e) {}
    el.replaceChildren(); el.setAttribute("aria-label", "breadcrumb");
    items.forEach((t, i) => { if (i) el.appendChild(mk("span", "sep", "/")); const last = i === items.length - 1;
      const n = last ? mk("span", "", String(t)) : mk("a", "", String(t)); if (last) n.setAttribute("aria-current", "page"); else { n.href = "#"; n.addEventListener("click", e => { e.preventDefault(); el.dispatchEvent(new CustomEvent("in-change", {detail: i})); }); } el.appendChild(n); });
  }
  function renderPages(el) {
    const total = Math.max(1, Math.trunc(Number(el.dataset.pages) || 1)), cur = Math.min(total, Math.max(1, Math.trunc(Number(el.dataset.page) || 1)));
    el.replaceChildren(); el.setAttribute("aria-label", "pagination");
    const go = n => { n = Math.min(total, Math.max(1, n)); if (n === cur) return; el.dataset.page = n; renderPages(el); el.dispatchEvent(new CustomEvent("in-change", {detail: n})); };
    const btn = (txt, n, dis, current) => { const b = mk("button", "", txt); b.type = "button"; if (dis) b.disabled = true; if (current) b.setAttribute("aria-current", "page"); b.addEventListener("click", () => go(n)); el.appendChild(b); };
    btn("‹", cur - 1, cur === 1);
    const shown = new Set([1, total, cur - 1, cur, cur + 1]); let prev = 0;
    for (let n = 1; n <= total; n++) { if (!shown.has(n)) continue; if (n - prev > 1) { const e = mk("span", "", "…"); e.style.alignSelf = "center"; el.appendChild(e); } btn(String(n), n, false, n === cur); prev = n; }
    btn("›", cur + 1, cur === total);
  }
  const ANIM_INF = new Set(["pulse", "spin", "float", "blink"]);
  function applyAnim(el) {
    const d = el.dataset, name = d.anim, kids = d.stagger ? Array.from(el.children) : [el];
    kids.forEach((t, i) => {
      if (!name) { t.style.animation = ""; return; }
      const inf = ANIM_INF.has(name), dl = (Number(d.animDelay) || 0) + (d.stagger ? i * Number(d.stagger) : 0);
      t.style.animation = "in-" + name + " " + (d.animDur || (inf ? 1200 : 400)) + "ms ease " + dl + "ms " + (d.animRep || (inf ? "infinite" : "1")) + " both";
    });
  }
  const TRD = {instant: 0, fast: 120, normal: 250, slow: 400, slower: 600};
  const trans = el => { if (!el.dataset.tr) setS(el, "transition", "background-color .15s ease,color .15s ease,box-shadow .15s ease,opacity .15s ease,scale .15s ease,translate .15s ease"); };
  const elevCss = v => { let l = typeof v === "number" ? v : (ELEVN[v] !== undefined ? ELEVN[v] : parseInt(v) || 0); l = Math.max(0, Math.min(5, l)); const e = ELEV[l]; return l ? "0 " + e[1] + "px " + e[0] + "px " + e[2] + "px rgba(0,0,0," + (e[3] * 2) + ")" : "none"; };
  const cls = (el, c) => el.classList.add(c);
  const edge = n => (el, v) => setS(el, n, px(v) + " solid var(--in-border)");
  const ovf = n => (el, v) => setS(el, n, v === "scroll" ? "auto" : v);
  // ---- نظام الثيم (تبديل فوري + حفظ) ----
  const theme = { set(m) { const r = document.documentElement; if (m === "dark" || m === "light") r.setAttribute("data-theme", m); else r.removeAttribute("data-theme");
      try { if (m === "dark" || m === "light") localStorage.setItem("in-theme", m); else localStorage.removeItem("in-theme"); } catch (e) {} lastV = []; run(); },
    isDark() { const a = document.documentElement.getAttribute("data-theme"); return a === "dark" || (a !== "light" && window.matchMedia("(prefers-color-scheme: dark)").matches); },
    toggle() { theme.set(theme.isDark() ? "light" : "dark"); } };
  const BP = {sm: 360, md: 600, lg: 840, xl: 1200}, SFX = /^(.+)_(sm|md|lg|xl|dark|print)$/, RANK = {sm: 1, md: 2, lg: 3, xl: 4, dark: 5, print: 6};
  const sfxMatch = s => s === "dark" ? theme.isDark() : s === "print" ? window.matchMedia("print").matches : window.innerWidth >= BP[s];

  const P = {
    text: (el, v) => { if (!isField(kind(el))) el.textContent = v == null ? "" : String(v); },
    label: (el, v) => { const k = kind(el); if (isField(k)) el.setAttribute("aria-label", v); else el.textContent = v == null ? "" : String(v); },
    title: (el, v) => { if (el.tagName === "DETAILS") { let sm = el.querySelector(":scope>summary"); if (!sm) { sm = document.createElement("summary"); sm.className = "in-summary"; el.prepend(sm); } sm.textContent = v == null ? "" : String(v); } else el.title = v; }, tooltip: (el, v) => { el.title = v; },
    description: (el, v) => el.setAttribute("aria-description", v), ariaLabel: (el, v) => el.setAttribute("aria-label", v), aria_label: (el, v) => el.setAttribute("aria-label", v),
    value: (el, v) => { const k = kind(el);
      if (k === "progress") { el.max = 100; el.value = num(v) || 0; }
      else if (k === "rating") { el.dataset.val = num(v) || 0; renderRating(el); }
      else if (k === "meter") { el.value = num(v) || 0; }
      else if (k === "text") el.textContent = v;
      else if (el.type === "checkbox") el.checked = truthy(v);
      else if (String(el.value) !== String(v == null ? "" : v)) el.value = v == null ? "" : v; },
    checked: (el, v) => { el.checked = truthy(v); },
    placeholder: (el, v) => { el.placeholder = v; },
    type: (el, v) => { if (el.tagName === "INPUT" && kind(el) === "input") el.type = v; },
    src: (el, v) => { el.setAttribute("src", v); }, url: (el, v) => { el.setAttribute(el.tagName === "A" ? "href" : "src", v); },
    href: (el, v) => { el.setAttribute("href", v); }, alt: (el, v) => { el.alt = v; }, fit: (el, v) => setS(el, "object-fit", v),
    visible: (el, v) => { el.hidden = !truthy(v); },
    open: (el, v) => { if (el.tagName === "DETAILS") el.open = truthy(v); else el.hidden = !truthy(v); },
    enabled: (el, v) => { const d = !truthy(v); if ("disabled" in el) el.disabled = d; el.setAttribute("aria-disabled", d); },
    readonly: (el, v) => { el.readOnly = truthy(v); }, autofocus: (el, v) => { if (truthy(v)) el.autofocus = true; },
    multiline: () => {}, rows: (el, v) => { if (el.tagName === "TEXTAREA") el.rows = v; },
    name: (el, v) => { el.setAttribute("name", v); }, id: () => {}, class: (el, v) => { String(v).split(/\s+/).filter(Boolean).forEach(c => el.classList.add(c)); },
    role: (el, v) => el.setAttribute("role", v), lang: (el, v) => el.setAttribute("lang", v), dir: (el, v) => el.setAttribute("dir", v),
    min: (el, v) => { el.min = v; }, max: (el, v) => { if (kind(el) === "rating") { el.dataset.max = num(v) || 5; renderRating(el); } else el.max = v; }, step: (el, v) => { el.step = v; },
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
      if (k === "progress") setS(el, "accent-color", c); else if (k === "banner") { setS(el, "border-inline-start", "4px solid " + c); setS(el, "background", "color-mix(in srgb," + c + " 12%, transparent)"); setS(el, "padding", "10px 14px"); setS(el, "border-radius", "8px"); } else if (k === "button" || k === "badge" || k === "tag") { if (!el.dataset.hasBg) setS(el, "background", c); } else setS(el, "color", c); },
    gradient: (el, v) => setS(el, "background", bgCss(Array.isArray(v) ? v.join(",") : v)),
    message: (el, v) => { el.textContent = v == null ? "" : String(v); }, initials: (el, v) => { el.textContent = v == null ? "" : String(v); el.setAttribute("aria-label", v); },
    maxlength: (el, v) => { el.maxLength = num(v); }, secure: (el, v) => { if (el.tagName === "INPUT") el.type = truthy(v) ? "password" : "text"; },
    loading: (el, v) => { const l = truthy(v); el.setAttribute("aria-busy", l); if ("disabled" in el) el.disabled = l; el.style.opacity = l ? .6 : ""; },
    error: (el, v) => { const e = v != null && v !== "" && v !== false; el.setAttribute("aria-invalid", e); el.style.borderColor = e ? "var(--in-danger)" : ""; if (typeof v === "string") el.title = v; },
    cursor: (el, v) => setS(el, "cursor", v), overflow: (el, v) => setS(el, "overflow", v === "scroll" ? "auto" : v),
    position: (el, v) => setS(el, "position", v), left: (el, v) => setS(el, "left", px(v)), top: (el, v) => setS(el, "top", px(v)),
    right: (el, v) => setS(el, "right", px(v)), bottom: (el, v) => setS(el, "bottom", px(v)),
    rotate: (el, v) => setS(el, "rotate", num(v) + "deg"), scale: (el, v) => setS(el, "scale", v), display: (el, v) => setS(el, "display", v),
    // ---- Pro: تخطيط ----
    col_span: (el, v) => setS(el, "grid-column", "span " + num(v)), row_span: (el, v) => setS(el, "grid-row", "span " + num(v)),
    columns_min: (el, v) => { setS(el, "display", "grid"); setS(el, "grid-template-columns", "repeat(auto-fill, minmax(min(" + px(v) + ", 100%), 1fr))"); },
    align_self: (el, v) => setS(el, "align-self", AL[v] || v), order: (el, v) => setS(el, "order", v),
    sticky: (el, v) => { if (v === false || v === "false") { setS(el, "position", "static"); return; } setS(el, "position", "sticky"); setS(el, "top", v === true || v === "true" ? "0px" : px(v)); setS(el, "z-index", 100); },
    inset: (el, v) => setS(el, "inset", px(v)), center: (el, v) => { if (truthy(v)) { setS(el, "margin-inline", "auto"); } },
    overflow_x: ovf("overflow-x"), overflow_y: ovf("overflow-y"),
    snap: (el, v) => { setS(el, "scroll-snap-type", (v === true || v === "true" ? "x" : v) + " mandatory"); setS(el, "overflow-" + (v === "y" ? "y" : "x"), "auto"); cls(el, "in-snap"); },
    border_top: edge("border-top"), border_bottom: edge("border-bottom"), border_left: edge("border-left"), border_right: edge("border-right"),
    // ---- Pro: مظهر ----
    blur: (el, v) => setS(el, "filter", "blur(" + px(v) + ")"), filter: (el, v) => setS(el, "filter", v),
    backdrop_blur: (el, v) => { setS(el, "backdrop-filter", "blur(" + px(v) + ")"); setS(el, "-webkit-backdrop-filter", "blur(" + px(v) + ")"); },
    glass: (el, v) => { if (!truthy(v)) return; setS(el, "backdrop-filter", "blur(16px) saturate(1.4)"); setS(el, "-webkit-backdrop-filter", "blur(16px) saturate(1.4)");
      setS(el, "background", "color-mix(in srgb, var(--in-surface) 60%, transparent)"); setS(el, "border", "1px solid color-mix(in srgb, var(--in-border) 60%, transparent)"); },
    outline: (el, v) => setS(el, "outline", px(v) + " solid var(--in-primary)"), outline_color: (el, v) => setS(el, "outline-color", col(v)),
    ring: (el, v) => setS(el, "box-shadow", "0 0 0 " + px(v) + " color-mix(in srgb, var(--in-primary) 35%, transparent)"),
    text_gradient: (el, v) => { setS(el, "background", bgCss(v)); setS(el, "-webkit-background-clip", "text"); setS(el, "background-clip", "text"); setS(el, "-webkit-text-fill-color", "transparent"); setS(el, "color", "transparent"); },
    clip: (el, v) => { setS(el, "overflow", "hidden"); setS(el, "border-radius", v === "circle" ? "50%" : v === "pill" ? "9999px" : px(v)); },
    truncate: (el, v) => { if (truthy(v)) { setS(el, "overflow", "hidden"); setS(el, "text-overflow", "ellipsis"); setS(el, "white-space", "nowrap"); } },
    nowrap: (el, v) => setS(el, "white-space", truthy(v) ? "nowrap" : "normal"), text_align: (el, v) => setS(el, "text-align", v === "right" ? "end" : v === "left" ? "start" : v),
    select: (el, v) => setS(el, "user-select", truthy(v) ? "text" : "none"), pointer_events: (el, v) => setS(el, "pointer-events", truthy(v) ? "auto" : "none"),
    // ---- Pro: حالات التفاعل (CSS vars + أصناف؛ بلا JS عند المرور) ----
    hover_bg: (el, v) => { setS(el, "--h-bg", bgCss(v)); cls(el, "in-hbg"); trans(el); }, hover_color: (el, v) => { setS(el, "--h-color", col(v)); cls(el, "in-hco"); trans(el); },
    hover_scale: (el, v) => { setS(el, "--h-scale", v); cls(el, "in-hsc"); trans(el); }, hover_opacity: (el, v) => { setS(el, "--h-opacity", v); cls(el, "in-hop"); trans(el); },
    hover_elevation: (el, v) => { setS(el, "--h-shadow", elevCss(v)); cls(el, "in-hsh"); trans(el); }, hover_lift: (el, v) => { setS(el, "--h-lift", "-" + Math.abs(num(v)) + "px"); cls(el, "in-hlf"); trans(el); },
    active_scale: (el, v) => { setS(el, "--a-scale", v); cls(el, "in-asc"); trans(el); },
    focus_ring: (el, v) => { if (v !== true && v !== "true") setS(el, "--fr", col(v)); cls(el, "in-fr"); },
    transition: (el, v) => { el.dataset.tr = "1"; const ms = TRD[v] !== undefined ? TRD[v] : num(v); setS(el, "transition", "all " + (ms || 0) + "ms ease"); },
    // ---- Pro: حركة ----
    animate: (el, v) => { el.dataset.anim = v === false || v === "false" || v === "none" ? "" : String(v); applyAnim(el); },
    duration: (el, v) => { el.dataset.animDur = TRD[v] !== undefined ? TRD[v] : num(v); applyAnim(el); },
    delay: (el, v) => { el.dataset.animDelay = num(v) || 0; applyAnim(el); }, repeat: (el, v) => { el.dataset.animRep = v === true || v === "true" ? "infinite" : String(v); applyAnim(el); },
    stagger: (el, v) => { el.dataset.stagger = num(v) || 0; applyAnim(el); },
    // ---- Pro: مكوّنات ----
    selected: (el, v) => { const t = truthy(v); el.setAttribute("aria-selected", t); el.classList.toggle("is-selected", t); },
    active: (el, v) => { const t = truthy(v); if (t) el.setAttribute("aria-current", "true"); else el.removeAttribute("aria-current"); el.classList.toggle("is-active", t); },
    level: (el, v) => el.setAttribute("aria-level", v),
    items: (el, v) => { if (typeof v === "string") v = v.split(",").map(x => x.trim()).filter(Boolean); el.dataset.items = JSON.stringify(v || []); const k = kind(el); if (k === "steps") renderSteps(el); else if (k === "breadcrumb") renderCrumbs(el); },
    current: (el, v) => { if (kind(el) === "pagination") { el.dataset.page = num(v) || 1; renderPages(el); return; } el.dataset.current = num(v) || 0; if (kind(el) === "steps") renderSteps(el); },
    total: (el, v) => { el.dataset.pages = num(v) || 1; renderPages(el); },
    pages: (el, v) => { el.dataset.pages = num(v) || 1; renderPages(el); }, page: (el, v) => { el.dataset.page = num(v) || 1; renderPages(el); },
    // ---- Pro 2 ----
    tip: (el, v) => { el.dataset.tip = v == null ? "" : v; cls(el, "in-tip"); if (!el.hasAttribute("tabindex") && !/^(BUTTON|A|INPUT|SELECT|TEXTAREA|SUMMARY)$/.test(el.tagName)) el.tabIndex = 0; },
    tip_pos: (el, v) => { el.dataset.tipPos = v; },
    hover_border: (el, v) => { setS(el, "--h-bd", col(v)); cls(el, "in-hbd"); trans(el); },
    focus_bg: (el, v) => { setS(el, "--f-bg", bgCss(v)); cls(el, "in-fbg"); trans(el); }, focus_border: (el, v) => { setS(el, "--f-bd", col(v)); cls(el, "in-fbd"); trans(el); },
    scrim: (el, v) => el.classList.toggle("in-scrim", truthy(v)), dismissible: (el, v) => { if (truthy(v)) el.setAttribute("data-dismiss", ""); else el.removeAttribute("data-dismiss"); },
    reveal: (el, v) => { if (v === false || v === "false" || v === "none") { el.dataset.reveal = ""; el.style.opacity = ""; return; } if (el.dataset.reveal) return; el.dataset.reveal = v === true || v === "true" ? "slideUp" : String(v); if (!REDUCED() && io) el.style.opacity = "0"; observe(el); },
    scroll_progress: (el, v) => { if (!truthy(v)) return; if (!spEls.includes(el)) spEls.push(el); if (!spOn) { spOn = true; window.addEventListener("scroll", spUpdate, {passive: true}); window.addEventListener("resize", spUpdate); } spUpdate(); },
    count_to: (el, v) => countTo(el, num(v) || 0), count_prefix: (el, v) => { el.dataset.cpre = v == null ? "" : v; }, count_suffix: (el, v) => { el.dataset.csuf = v == null ? "" : v; }, count_decimals: (el, v) => { el.dataset.cdec = num(v) || 0; },
    hotkey: (el, v) => { const parts = String(v).toLowerCase().split("+").map(x => x.trim()); const k = parts[parts.length - 1]; const mod = parts.includes("ctrl") || parts.includes("mod") || parts.includes("cmd");
      hotkeys.push({el, k, mod, shift: parts.includes("shift"), alt: parts.includes("alt")}); if (!el.title) el.title = String(v); },
    route: (el, v) => { el.dataset.route = v; if (!route.els.includes(el)) route.els.push(el); route.apply(); },
    go: (el, v) => { el.dataset.go = v; if (!route.links.includes(el)) { route.links.push(el); el.addEventListener("click", e => { e.preventDefault(); route.go(el.dataset.go); }); if (el.tagName === "A") el.setAttribute("href", "#" + v); if (!/^(BUTTON|A|INPUT)$/.test(el.tagName)) { el.setAttribute("role", "link"); el.tabIndex = 0; el.style.cursor = "pointer"; el.addEventListener("keydown", e => { if (e.key === "Enter") { e.preventDefault(); route.go(el.dataset.go); } }); } } route.apply(); },
    delta: (el, v) => { if (kind(el) === "stat") { el.dataset.sd = v == null ? "" : v; renderStat(el); } },
    data: (el, v) => { if (typeof v === "string") { const lb = [], vs = []; v.split(",").map(x => x.trim()).filter(Boolean).forEach(e => { const c = e.indexOf(":"); if (c < 0) { lb.push(""); vs.push(Number(e)); } else { lb.push(e.slice(0, c)); vs.push(Number(e.slice(c + 1))); } }); v = vs; if (lb.some(Boolean) && !el.dataset.labels) el.dataset.labels = JSON.stringify(lb); } el.dataset.data = JSON.stringify(Array.isArray(v) ? v : []); renderChart(el); },
    diameter: (el, v) => { setS(el, "width", px(v)); setS(el, "height", px(v)); },
    values: (el, v) => { el.dataset.showValues = truthy(v) ? "true" : ""; if (kind(el) === "barchart") renderChart(el); }, labels: (el, v) => { el.dataset.labels = JSON.stringify(typeof v === "string" ? v.split(",").map(x => x.trim()) : (v || [])); if (/^(sparkline|barchart|donut)$/.test(kind(el))) renderChart(el); },
    page_size: (el, v) => { el.dataset.ps = num(v) || 0; if (kind(el) === "datatable") renderTable(el); },
    speed: (el, v) => { initMarquee(el); el.style.setProperty("--mq", (num(v) || 20) + "s"); }
  };


  // ---- Pro 2: كشف عند التمرير، سحب، مسارات، رسوم، جداول ----
  const REDUCED = () => { try { return window.matchMedia("(prefers-reduced-motion: reduce)").matches; } catch (e) { return false; } };
  const io = "IntersectionObserver" in window ? new IntersectionObserver(es => es.forEach(en => { if (!en.isIntersecting) return; const el = en.target; io.unobserve(el);
    if (el.dataset.reveal) { el.style.opacity = ""; el.style.animation = "in-" + el.dataset.reveal + " " + (el.dataset.animDur || 500) + "ms ease " + (Number(el.dataset.animDelay) || 0) + "ms 1 both"; }
    el.dispatchEvent(new CustomEvent("in-visible")); }), {threshold: 0.15}) : null;
  const observe = el => { if (io && !REDUCED()) io.observe(el); else { if (el.dataset.reveal) el.style.opacity = ""; setTimeout(() => el.dispatchEvent(new CustomEvent("in-visible")), 0); } };
  function watchSwipe(el) {
    if (el._sw) return; el._sw = 1; let x0 = 0, y0 = 0, t0 = 0;
    el.addEventListener("pointerdown", e => { x0 = e.clientX; y0 = e.clientY; t0 = Date.now(); });
    el.addEventListener("pointerup", e => { const dx = e.clientX - x0, dy = e.clientY - y0; if (Date.now() - t0 < 700 && Math.abs(dx) > 50 && Math.abs(dy) < 40) el.dispatchEvent(new CustomEvent(dx < 0 ? "in-swipel" : "in-swiper")); });
  }
  const route = { els: [], links: [],
    cur() { const h = location.hash.replace(/^#/, ""); return h === "" ? "/" : (h[0] === "/" ? h : "/" + h); },
    match(pat, cur) { if (pat === "*") return true; const a = String(pat).split("/").filter(Boolean), b = cur.split("/").filter(Boolean);
      for (let i = 0; i < a.length; i++) { if (a[i] === "*") return true; if (i >= b.length) return false; if (a[i][0] !== ":" && a[i] !== b[i]) return false; } return a.length === b.length; },
    apply() { const c = route.cur(); route.els.forEach(e => { e.hidden = !route.match(e.dataset.route, c); });
      route.links.forEach(e => { const on = e.dataset.go === c || (e.dataset.go !== "/" && c.startsWith(e.dataset.go + "/")); if (on) e.setAttribute("aria-current", "page"); else e.removeAttribute("aria-current"); e.classList.toggle("is-active", on); }); },
    go(p) { p = String(p); location.hash = "#" + (p[0] === "/" ? p : "/" + p); } };
  window.addEventListener("hashchange", () => route.apply());
  let deferredInstall = null; window.addEventListener("beforeinstallprompt", e => { e.preventDefault(); deferredInstall = e; });
  const hotkeys = []; document.addEventListener("keydown", e => { for (const h of hotkeys) { if (e.key.toLowerCase() === h.k && !!(e.ctrlKey || e.metaKey) === h.mod && e.shiftKey === h.shift && e.altKey === h.alt) { if (!document.body.contains(h.el)) continue; e.preventDefault(); if (h.el.matches("input,textarea,select")) h.el.focus(); else h.el.click(); return; } } });
  document.addEventListener("click", e => document.querySelectorAll("details.in-dropdown[open]").forEach(d => { if (!d.contains(e.target)) d.open = false; }));
  document.addEventListener("keydown", e => { if (e.key === "Escape") { document.querySelectorAll("details.in-dropdown[open]").forEach(d => { d.open = false; }); document.querySelectorAll(".in-dialog[data-dismiss]:not([hidden])").forEach(d => d.dispatchEvent(new CustomEvent("in-close"))); } });
  document.addEventListener("pointerdown", e => document.querySelectorAll(".in-dialog[data-dismiss]:not([hidden])").forEach(d => { if (!d.contains(e.target)) d.dispatchEvent(new CustomEvent("in-close")); }));
  const spEls = []; let spOn = false;
  const spUpdate = () => { const h = document.documentElement, max = h.scrollHeight - h.clientHeight, pct = max > 0 ? Math.min(100, Math.max(0, (window.scrollY / max) * 100)) : 0; spEls.forEach(e => { if (e.tagName === "PROGRESS") { e.max = 100; e.value = pct; } else setS(e, "width", pct + "%"); }); };
  const PAL = ["var(--in-primary)", "#2F7DC4", "#2EB872", "#E5A100", "#D14545", "#8E44AD", "#16A085", "#7F8C8D"];
  const svgNS = "http://www.w3.org/2000/svg", sv = (t, a) => { const n = document.createElementNS(svgNS, t); for (const k in a) n.setAttribute(k, a[k]); return n; };
  const nums = el => { try { return JSON.parse(el.dataset.data || "[]").map(Number).filter(x => isFinite(x)); } catch (e) { return []; } };
  const labs = el => { try { return JSON.parse(el.dataset.labels || "[]").map(String); } catch (e) { return []; } };
  function renderChart(el) {
    const k = kind(el), d = nums(el), lb = labs(el); el.replaceChildren(); el.setAttribute("role", "img"); el.setAttribute("aria-label", (el.dataset.alabel || k) + ": " + d.join(", "));
    if (!d.length) return;
    if (k === "sparkline") { const mx = Math.max(...d), mn = Math.min(...d), r = mx - mn || 1, n = d.length;
      const pts = d.map((y, i) => [n === 1 ? 50 : (i / (n - 1)) * 100, 28 - ((y - mn) / r) * 26]);
      const svg = sv("svg", {viewBox: "0 0 100 30", preserveAspectRatio: "none"});
      svg.appendChild(sv("polygon", {points: "0,30 " + pts.map(p => p.join(",")).join(" ") + " 100,30", fill: "var(--in-primary)", opacity: ".15"}));
      svg.appendChild(sv("polyline", {points: pts.map(p => p.join(",")).join(" "), fill: "none", stroke: "var(--in-primary)", "stroke-width": "1.6", "vector-effect": "non-scaling-stroke", "stroke-linejoin": "round", "stroke-linecap": "round"}));
      el.appendChild(svg);
    } else if (k === "barchart") { const mx = Math.max(...d, 0) || 1, n = d.length, W = 100, H = 50, bw = W / n;
      const svg = sv("svg", {viewBox: "0 0 " + W + " " + H, preserveAspectRatio: "none"});
      d.forEach((y, i) => { const h = Math.max(0, (y / mx) * H); const r = sv("rect", {x: i * bw + bw * 0.15, y: H - h, width: bw * 0.7, height: h, rx: 1.2, fill: PAL[i % PAL.length]}); const t = sv("title", {}); t.textContent = (lb[i] || i + 1) + ": " + y; r.appendChild(t); svg.appendChild(r);
      });
      const plot = mk("div", "bc-plot"); plot.appendChild(svg);
      if (el.dataset.showValues) d.forEach((y, i) => { const t = mk("span", "bc-val", String(Math.round(y * 100) / 100)); t.style.left = ((i + 0.5) / n * 100) + "%"; t.style.bottom = "calc(" + (Math.max(0, y) / mx * 100) + "% + 2px)"; plot.appendChild(t); });
      el.appendChild(plot);
      if (lb.length) { const row = mk("div", "bc-labels"); d.forEach((_, i) => row.appendChild(mk("span", "", lb[i] || ""))); el.appendChild(row); }
    } else if (k === "donut") { const tot = d.reduce((a, b) => a + Math.max(0, b), 0) || 1; let acc = 0, stops = [];
      d.forEach((y, i) => { const a = acc / tot * 100; acc += Math.max(0, y); stops.push(PAL[i % PAL.length] + " " + a + "% " + (acc / tot * 100) + "%"); });
      el.style.background = "conic-gradient(" + stops.join(",") + ")";
      if (lb.length) { const lg = mk("div", "in-legend"); lb.forEach((t, i) => { const s = mk("span", ""); const sw = mk("i", ""); sw.style.background = PAL[i % PAL.length]; s.append(sw, t + " " + Math.round((d[i] || 0) / tot * 100) + "%"); lg.appendChild(s); }); el.appendChild(lg); el.style.height = "auto"; el.style.borderRadius = "0"; el.style.background = ""; const hole = mk("div", "in-donut"); hole.style.background = "conic-gradient(" + stops.join(",") + ")"; el.prepend(hole); }
    }
  }
  function renderStat(el) { const d = el.dataset; el.replaceChildren(); el.appendChild(mk("div", "sl", d.sl || "")); el.appendChild(mk("div", "sv", d.sv == null ? "" : d.sv));
    if (d.sd) { const t = /^\s*-/.test(d.sd) || d.sdTrend === "down" ? "down" : "up"; el.appendChild(mk("div", "sd " + t, (t === "up" && !/^\s*[+↑]/.test(d.sd) ? "↑ " : t === "down" && !/^\s*[-↓]/.test(d.sd) ? "↓ " : "") + d.sd)); } }
  function renderRing(el) { const v = Math.max(0, Math.min(100, Number(el.dataset.rv) || 0)); el.style.setProperty("--p", v); el.replaceChildren(mk("span", "", el.dataset.rl != null ? el.dataset.rl : Math.round(v) + "%")); el.setAttribute("role", "progressbar"); el.setAttribute("aria-valuenow", Math.round(v)); }
  function renderTable(el) {
    const t = el._t || (el._t = {sort: -1, dir: 1, page: 0});
    const cols = (() => { try { return JSON.parse(el.dataset.cols || "[]"); } catch (e) { return []; } })(), rows0 = (() => { try { return JSON.parse(el.dataset.rows || "[]"); } catch (e) { return []; } })();
    const q = (el.dataset.q || "").toLowerCase(), ps = Math.trunc(Number(el.dataset.ps) || 0);
    let rows = rows0.filter(r => !q || r.some(c => String(c).toLowerCase().includes(q)));
    if (t.sort >= 0) rows = rows.slice().sort((a, b) => { const x = a[t.sort], y = b[t.sort], nx = typeof x === "number" && typeof y === "number"; return (nx ? x - y : String(x).localeCompare(String(y), undefined, {numeric: true})) * t.dir; });
    const pages = ps ? Math.max(1, Math.ceil(rows.length / ps)) : 1; if (t.page >= pages) t.page = pages - 1;
    const shown = ps ? rows.slice(t.page * ps, t.page * ps + ps) : rows;
    el.replaceChildren(); const tb = mk("table"), th = mk("thead"), hr = mk("tr");
    cols.forEach((c, i) => { const h = mk("th", "", String(c)); h.scope = "col"; if (t.sort === i) h.setAttribute("aria-sort", t.dir > 0 ? "ascending" : "descending"); h.tabIndex = 0;
      const go = () => { if (t.sort === i) t.dir = -t.dir; else { t.sort = i; t.dir = 1; } renderTable(el); }; h.addEventListener("click", go); h.addEventListener("keydown", e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); go(); } }); hr.appendChild(h); });
    th.appendChild(hr); tb.appendChild(th); const body = mk("tbody"); shown.forEach(r => { const tr = mk("tr"); cols.forEach((_, i) => tr.appendChild(mk("td", "", r[i] == null ? "" : String(r[i])))); body.appendChild(tr); }); tb.appendChild(body); el.appendChild(tb);
    if (!shown.length) el.appendChild(mk("div", "dt-empty", el.dataset.empty || "—"));
    if (ps) { const f = mk("div", "dt-foot"), info = mk("span", "", rows.length ? (t.page * ps + 1) + "–" + Math.min(rows.length, t.page * ps + ps) + " / " + rows.length : "0"), nav = mk("span");
      const pb = mk("button", "", "‹"), nb = mk("button", "", "›"); pb.type = nb.type = "button"; pb.disabled = t.page === 0; nb.disabled = t.page >= pages - 1; pb.setAttribute("aria-label", "prev"); nb.setAttribute("aria-label", "next");
      pb.addEventListener("click", () => { t.page--; renderTable(el); }); nb.addEventListener("click", () => { t.page++; renderTable(el); }); nav.append(pb, nb); f.append(info, nav); el.appendChild(f); }
  }
  function initDropdown(el) { if (el._dd) return; el._dd = 1; const pn = mk("div", "in-dd-panel"); Array.from(el.children).forEach(c => { if (c.tagName !== "SUMMARY") pn.appendChild(c); }); el.appendChild(pn); }
  function initMarquee(el) { if (el._mq) return; el._mq = 1; const tr = mk("div", "in-marquee-track"); while (el.firstChild) tr.appendChild(el.firstChild); const cl = tr.cloneNode(true); cl.setAttribute("aria-hidden", "true"); el.append(tr, cl); }
  const fmtN = (n, d) => new Intl.NumberFormat(undefined, {minimumFractionDigits: d, maximumFractionDigits: d}).format(n);
  function countTo(el, to) {
    const d = el.dataset, from = Number(d.cur) || 0, dur = Number(d.animDur) || 800;
    const set = n => { el.textContent = (d.cpre || "") + fmtN(n, Math.max(0, Math.trunc(Number(d.cdec) || 0))) + (d.csuf || ""); };
    if (REDUCED() || from === to) { d.cur = to; set(to); return; }
    cancelAnimationFrame(el._cf); const t0 = performance.now();
    const step = now => { const k = Math.min(1, (now - t0) / dur), e = 1 - Math.pow(1 - k, 3); const n = from + (to - from) * e; d.cur = n; set(n); if (k < 1) el._cf = requestAnimationFrame(step); else { d.cur = to; set(to); } };
    el._cf = requestAnimationFrame(step);
  }
  const FALLBACK = (el, key, v) => { el.dataset["in" + key.replace(/(^|_)(\w)/g, (m, a, b) => b.toUpperCase())] = v; };

  // ---- Pro 2: خصائص تتشارك أسماءها مع قديمة → تُوجَّه حسب نوع العنصر ----
  const wrapK = (name, kn, fn) => { const o = P[name]; P[name] = (el, v) => kind(el) === kn ? fn(el, v) : (o || ((e, x) => FALLBACK(e, name, x)))(el, v); };
  wrapK("label", "stat", (el, v) => { el.dataset.sl = v == null ? "" : v; renderStat(el); });
  wrapK("value", "stat", (el, v) => { el.dataset.sv = v == null ? "" : v; renderStat(el); });
  wrapK("value", "progressring", (el, v) => { el.dataset.rv = num(v) || 0; renderRing(el); });
  wrapK("label", "progressring", (el, v) => { el.dataset.rl = v == null ? "" : v; renderRing(el); });
  wrapK("columns", "datatable", (el, v) => { el.dataset.cols = JSON.stringify(typeof v === "string" ? v.split(",").map(x => x.trim()) : (v || [])); renderTable(el); });
  wrapK("rows", "datatable", (el, v) => { el.dataset.rows = JSON.stringify(v || []); renderTable(el); });
  wrapK("filter", "datatable", (el, v) => { el.dataset.q = v == null ? "" : String(v); if (el._t) el._t.page = 0; renderTable(el); });
  wrapK("title", "dropdown", (el, v) => { let sm = el.querySelector(":scope>summary"); if (!sm) { sm = mk("summary", ""); el.prepend(sm); } sm.textContent = v == null ? "" : String(v); });
  const _open = P.open; P.open = (el, v) => { _open(el, v); if (kind(el) === "dialog" && truthy(v)) { if (!el.hasAttribute("tabindex")) el.tabIndex = -1; setTimeout(() => { try { el.focus({preventScroll: true}); } catch (e) {} }, 0); } };

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
      toast: (m, ms = 2600) => { let h = document.getElementById("in-toasts"); if (!h) { h = mk("div", "in-toasts"); h.id = "in-toasts"; h.setAttribute("role", "status"); h.setAttribute("aria-live", "polite"); document.body.appendChild(h); }
        const t = mk("div", "in-toast", m == null ? "" : String(m)); h.appendChild(t); setTimeout(() => t.remove(), Math.max(300, num0(ms))); },
      copyText: t => { try { navigator.clipboard.writeText(String(t == null ? "" : t)).catch(() => {}); } catch (e) {} },
      openUrl: (u, t = "_blank") => { window.open(String(u), t, "noopener"); }, scrollTop: () => { window.scrollTo({top: 0, behavior: "smooth"}); },
      share: (t, x, u) => { if (navigator.share) navigator.share({title: t, text: x, url: u}).catch(() => {}); else if (u) navigator.clipboard && navigator.clipboard.writeText(String(u)).catch(() => {}); },
      setTheme: m => theme.set(m), toggleTheme: () => theme.toggle(),
      navigate: p => route.go(p), back: () => history.back(),
      downloadText: (name, text, mime) => { const a = document.createElement("a"), u = URL.createObjectURL(new Blob([text == null ? "" : String(text)], {type: mime || "text/plain;charset=utf-8"})); a.href = u; a.download = String(name || "download.txt"); document.body.appendChild(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(u), 1000); },
      installApp: () => { if (deferredInstall) { deferredInstall.prompt(); deferredInstall = null; } },
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
      const b = B[i]; let v, key = b[1];
      if (!(key in P)) { const m = SFX.exec(key); if (m && m[1] in P) { if (!sfxMatch(m[2])) continue; key = m[1]; } }
      try { v = typeof b[2] === "function" ? b[2]() : b[2]; } catch (e) { console.error("[indsin] " + b[0] + "." + b[1] + ":", e); continue; }
      const sv = typeof v === "object" ? JSON.stringify(v) : v;
      if (i in lastV && lastV[i] === sv) continue;
      lastV[i] = sv;
      const el = $(b[0]); if (!el) continue;
      if (/^(bg|background)(_|$)/.test(b[1])) el.dataset.hasBg = "1";
      (P[key] || ((e, v2) => FALLBACK(e, key, v2)))(el, v);
    }
  }
  function boot(S, bindings, handlers) {
    B = bindings.map((b, i) => [b, i]).sort((x, y) => { const rk = k => { const m = SFX.exec(k[0][1]); return m && !(k[0][1] in P) ? RANK[m[2]] : 0; }; return rk(x) - rk(y) || x[1] - y[1]; }).map(x => x[0]);
    try { const t = localStorage.getItem("in-theme"); if (t === "dark" || t === "light") document.documentElement.setAttribute("data-theme", t); } catch (e) {}
    document.querySelectorAll(".in-marquee").forEach(initMarquee);
    document.querySelectorAll("details.in-dropdown").forEach(initDropdown);
    let rq = 0; const redo = () => { if (rq) return; rq = requestAnimationFrame(() => { rq = 0; lastV = []; run(); }); };
    window.addEventListener("resize", redo); try { window.matchMedia("(prefers-color-scheme: dark)").addEventListener("change", redo); } catch (e) {}
    // الخلفية أولاً كي يعرف color الزر أنه نص لا تعبئة
    B.forEach(b => { if (/^(bg|background)(_|$)/.test(b[1])) { const e = $(b[0]); if (e) e.dataset.hasBg = "1"; } });
    handlers.forEach(h => {
      const el = $(h[0]); if (!el) return;
      if (h[1] === "__bind__") {
        const name = h[2], key = h[3];
        if (el.classList.contains("in-rating") || el.classList.contains("in-pagination")) { el.addEventListener("in-change", e => { S[name] = e.detail; }); return; }
        if (key === "checked") el.addEventListener("change", () => { S[name] = el.checked; });
        else el.addEventListener("input", () => { const cur = S[name]; S[name] = (typeof cur === "number" || el.type === "range" || el.type === "number") && el.value !== "" ? Number(el.value) : el.value; });
        return;
      }
      if (h[1] === "in-visible") observe(el); else if (h[1] === "in-swipel" || h[1] === "in-swiper") watchSwipe(el);
      el.addEventListener(h[1], ev => { if (h[1] === "submit") ev.preventDefault(); try { h[2](ev); } catch (e) { console.error("[indsin] handler:", e); } });
    });
    run();
  }
  return {state, actions, natives, boot, P, indsin, theme};
})();
)JS";

// ---- جسر المحرّك الأصلي ↔ الويب: يرسم DrawList (JSON من rin_indsin_render_json) على <canvas> داخل الصفحة نفسها ----
const char* kHybridJs = R"JS(
(function () {
  const holder = document.getElementById("indsin-native"); if (!holder) return;
  let data; try { data = JSON.parse(holder.textContent); } catch (e) { console.error("[indsin] native json:", e); return; }
  const root = document.getElementById("indsin-root"), cv = document.createElement("canvas"), wrap = document.createElement("div");
  wrap.className = "in-native-wrap"; wrap.hidden = true; wrap.appendChild(cv);
  const W = Math.max(1, data.canvas.width), H = Math.max(1, data.canvas.height), dpr = window.devicePixelRatio || 1;
  cv.width = Math.round(W * dpr); cv.height = Math.round(H * dpr); cv.style.width = W + "px"; cv.style.height = H + "px"; cv.setAttribute("role", "img"); cv.setAttribute("aria-label", "Native Indsin engine render");
  const ctx = cv.getContext("2d"); ctx.scale(dpr, dpr);
  const rr = (x, y, w, h, r) => { r = Math.max(0, Math.min(r || 0, w / 2, h / 2)); ctx.beginPath(); if (r > 0 && ctx.roundRect) ctx.roundRect(x, y, w, h, r); else ctx.rect(x, y, w, h); };
  const wrapText = (t, w) => { const out = []; String(t).split("\n").forEach(par => { let line = ""; par.split(" ").forEach(wd => { const tr = line ? line + " " + wd : wd; if (line && ctx.measureText(tr).width > w) { out.push(line); line = wd; } else line = tr; }); out.push(line); }); return out; };
  ctx.fillStyle = data.canvas.background || "#fff"; ctx.fillRect(0, 0, W, H);
  (data.paint || []).forEach(p => {
    ctx.save(); ctx.globalAlpha = p.alpha == null ? 1 : p.alpha; ctx.fillStyle = ctx.strokeStyle = p.color || "#000";
    if (p.op === "fill_rect" || p.op === "scrim_rect") { rr(p.x, p.y, p.w, p.h, p.radius); ctx.fill(); }
    else if (p.op === "stroke_rect") { ctx.lineWidth = p.strokeWidth || 1; rr(p.x + ctx.lineWidth / 2, p.y + ctx.lineWidth / 2, p.w - ctx.lineWidth, p.h - ctx.lineWidth, p.radius); ctx.stroke(); }
    else if (p.op === "stroke_arc") { const a = Math.PI / 180, cx = p.x + p.w / 2, cy = p.y + p.h / 2; ctx.lineWidth = p.strokeWidth || 1; ctx.lineCap = "butt"; ctx.beginPath();
      ctx.ellipse(cx, cy, Math.max(0, p.w / 2), Math.max(0, p.h / 2), 0, (p.start || 0) * a, ((p.start || 0) + (p.sweep == null ? 360 : p.sweep)) * a); ctx.stroke(); }
    else if (p.op === "text") { const fs = Math.max(6, p.fontSize || 14); ctx.font = (p.bold ? "700 " : "400 ") + fs + "px system-ui,-apple-system,Segoe UI,sans-serif";
      const al = p.align === 1 ? "center" : p.align === 2 ? "right" : "left"; ctx.textAlign = al; const tx = al === "center" ? p.x + p.w / 2 : al === "right" ? p.x + p.w : p.x;
      ctx.beginPath(); ctx.rect(p.x, p.y, p.w, Math.max(p.h, fs * 1.4)); ctx.clip();
      if (p.singleLine || p.h < fs * 1.9) { ctx.textBaseline = "middle"; ctx.fillText(String(p.text), tx, p.y + p.h / 2); }
      else { ctx.textBaseline = "top"; wrapText(p.text, p.w).forEach((l, i) => ctx.fillText(l, tx, p.y + i * fs * 1.3)); } }
    ctx.restore();
  });
  const btn = document.createElement("button"); btn.id = "in-hybrid-toggle"; btn.type = "button"; btn.className = "in-hybrid-btn";
  const modes = ["web", "native", "split"]; let mi = 0;
  const apply = () => { const m = modes[mi]; root.hidden = m === "native"; wrap.hidden = m === "web"; document.body.classList.toggle("in-split", m === "split"); document.body.dataset.inView = m; btn.textContent = "View: " + m; };
  btn.addEventListener("click", () => { mi = (mi + 1) % modes.length; apply(); });
  const st = document.createElement("style"); st.textContent = ".in-native-wrap{padding:8px;overflow:auto}.in-native-wrap canvas{display:block;border-radius:8px;box-shadow:0 0 0 1px rgba(128,128,128,.35)}body.in-split{display:flex;gap:16px;align-items:flex-start}body.in-split #indsin-root{flex:1 1 0;min-width:0}.in-hybrid-btn{position:fixed;left:12px;bottom:12px;z-index:2000;padding:6px 12px;border-radius:99px;border:1px solid rgba(128,128,128,.5);background:rgba(20,20,30,.85);color:#fff;font:12px system-ui;cursor:pointer}@media print{.in-hybrid-btn{display:none}}";
  document.head.appendChild(st); root.after(wrap); document.body.appendChild(btn); apply();
  window.indsinNative = { data, canvas: cv, mode: () => modes[mi] };
})();
)JS";

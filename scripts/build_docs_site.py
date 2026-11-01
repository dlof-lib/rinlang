#!/usr/bin/env python3
"""يبني موقع توثيق Rin (HTML ثابت) من ملفات docs/*.md.
الاستخدام:  pip install markdown && python3 scripts/build_docs_site.py [مجلد_الإخراج]
الافتراضي: docs/manual   (يُنشر تلقائياً مع GitHub Pages على /manual/)"""
import html, json, re, sys
from pathlib import Path
import markdown

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "docs" / "manual"
REPO = "https://github.com/dlof-lib/rinlang"
GROUPS = [
 ("ابدأ هنا", "getting-started syntax variables template-strings input control-flow loops functions enums"),
 ("اللغة", "language-reference objects oop containers table set pipelines RECKON let-plus living-variables label-define block-frames compare_ops max_min scope-guarantees key_terms_and_builtins number_builtins"),
 ("الواجهات (Indsin)", "indsin indsin_rin indsin_expansion indsin_object indsin_media indsin_icons indsin_tooling indsinweb RIN_ELEMENTS events-effects banner simple-style"),
 ("بيئة التشغيل", "runtime standard-library errors ERROR_SYSTEM storage http env terminal vm rinflow MAKE_UNIT MAKE_MEDIA TIME native-additions"),
 ("الحاويات وقواعد البيانات", "container_advanced container_pro container_sql cross-file-containers"),
 ("الحزم والتكامل", "api android packages-and-interop rinpak-format html-projects project-og-urin CONTENT_PACKS previewkit"),
 ("المكتبات المتخصصة", "rinml STATS STATS_ADV LINALG PLOT ALGO NUMBER boat passkit passkit-crypto-db hash-family image-draw image-terminal"),
 ("المشروع", "CONTRIBUTING CODE_OF_CONDUCT SECURITY SUPPORT TRADEMARKS THIRD_PARTY_NOTICES"),
]
ROOTMD = {"indsin": ROOT/"indsin.md", **{n: ROOT/f"{n}.md" for n in
          "CONTRIBUTING CODE_OF_CONDUCT SECURITY SUPPORT TRADEMARKS THIRD_PARTY_NOTICES".split()}}

def sources():
    d = {p.stem: p for p in (ROOT/"docs").glob("*.md") if p.stem != "README"}
    for k, p in ROOTMD.items():
        if p.exists(): d[k] = p
    return d

def title_of(text, stem):
    m = re.search(r"^#\s+(.+)$", text, re.M)
    t = m.group(1).strip() if m else stem
    return re.sub(r"^[\w.\-]+\.md\s*[—–-]\s*", "", t).strip("` ") or stem

def plain(h): return re.sub(r"\s+", " ", html.unescape(re.sub(r"<[^>]+>", " ", h))).strip()

def build():
    src = sources()
    order, seen = [], set()
    nav = []
    for g, names in GROUPS:
        items = [n for n in names.split() if n in src]
        seen.update(items); nav.append((g, items))
    rest = sorted(n for n in src if n not in seen)
    if rest: nav.append(("مراجع إضافية", rest))
    flat = [n for _, its in nav for n in its]
    titles = {n: title_of(src[n].read_text(encoding="utf-8"), n) for n in flat}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/"assets").mkdir(exist_ok=True)
    (OUT/"assets"/"style.css").write_text(CSS, encoding="utf-8")
    (OUT/"assets"/"app.js").write_text(JS, encoding="utf-8")

    def fix_links(h, base):
        def r(m):
            u = m.group(2)
            if re.match(r"(https?:|mailto:|#)", u): return m.group(0)
            path, _, frag = u.partition("#"); name = Path(path).stem
            if path.endswith(".md") and name in src:
                return f'{m.group(1)}"{name}.html{"#"+frag if frag else ""}"'
            if path and not path.endswith((".png",".jpg",".svg",".gif")):
                rel = (src.get(base, ROOT/"docs"/"x").parent / path).resolve()
                try: return f'{m.group(1)}"{REPO}/blob/main/{rel.relative_to(ROOT).as_posix()}"'
                except ValueError: pass
            return m.group(0)
        return re.sub(r'(href=)"([^"]+)"', r, h)

    index = []
    for i, n in enumerate(flat):
        text = src[n].read_text(encoding="utf-8")
        md = markdown.Markdown(extensions=["fenced_code", "tables", "toc", "sane_lists"],
                               extension_configs={"toc": {"toc_depth": "2-3", "permalink": "#"}})
        body = fix_links(md.convert(text), n)
        body = re.sub(r"<table>", '<div class="tw"><table>', body).replace("</table>", "</table></div>")
        body = re.sub(r'src="(?!https?:)(?:\.\./)?(?:\./)?(assets/[^"]+)"', r'src="../\1"', body) if src[n].parent.name == "docs" else body
        toc = md.toc
        index.append({"u": f"{n}.html", "t": titles[n], "x": plain(body)[:2500]})
        prev = flat[i-1] if i else None; nxt = flat[i+1] if i+1 < len(flat) else None
        pn = "".join([f'<a class="pn" href="{prev}.html">→ {html.escape(titles[prev])}</a>' if prev else "<span></span>",
                      f'<a class="pn" href="{nxt}.html">{html.escape(titles[nxt])} ←</a>' if nxt else ""])
        edit = f"{REPO}/edit/main/{src[n].relative_to(ROOT).as_posix()}"
        (OUT/f"{n}.html").write_text(page(titles[n], body, toc, nav, titles, n, pn, edit), encoding="utf-8")

    cards = "".join(f'<a class="card" href="{n}.html"><b>{html.escape(titles[n])}</b><span>{g}</span></a>'
                    for g, its in nav[:2] for n in its[:6])
    home = f'''<div class="hero"><img src="../assets/brand/icon-192.png" alt="" width="88"><h1>تعلّم لغة Rin</h1>
<p>لغة برمجة صغيرة وواضحة بمحرّك واحد يعمل على سطر الأوامر والهاتف والمتصفح.</p>
<div class="cta"><a class="btn" href="getting-started.html">ابدأ الآن</a><a class="btn alt" href="language-reference.html">المرجع</a>
<a class="btn alt" href="../playground.html">جرّب في المتصفح</a></div></div>
<pre><code>fun greet(name) {{
    return "مرحباً يا " + name + "!";
}}
print greet("Rin");</code></pre><h2>مسار التعلّم</h2><div class="cards">{cards}</div>'''
    (OUT/"index.html").write_text(page("توثيق Rin", home, "", nav, titles, "", "", REPO, True), encoding="utf-8")
    (OUT/"search.json").write_text(json.dumps(index, ensure_ascii=False), encoding="utf-8")
    (OUT/".nojekyll").write_text("")
    print(f"✔ {len(flat)+1} صفحة → {OUT}")

def page(title, body, toc, nav, titles, cur, pn, edit, home=False):
    side = "".join(f'<details {"open" if cur in its or home and g==nav[0][0] else ""}><summary>{g}</summary>' +
        "".join(f'<a href="{n}.html"{" class=on" if n==cur else ""}>{html.escape(titles[n])}</a>' for n in its) +
        "</details>" for g, its in nav)
    return f'''<!doctype html><html lang="ar" dir="rtl"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(title)} — Rin</title>
<meta name="description" content="توثيق لغة البرمجة Rin"><link rel="icon" href="../assets/brand/icon-32.png">
<link rel="stylesheet" href="assets/style.css"></head><body>
<header><button id="mn" aria-label="القائمة">☰</button><a class="logo" href="index.html"><img src="../assets/brand/icon-48.png" alt="" width="26">Rin</a>
<input id="q" type="search" placeholder="ابحث في التوثيق…" autocomplete="off"><button id="th" aria-label="الوضع">◐</button>
<a class="gh" href="{REPO}">GitHub</a><div id="res"></div></header>
<div class="wrap"><nav id="sb">{side}</nav>
<main><article>{body}</article>
{'' if home else f'<div class="pnav">{pn}</div><p class="edit"><a href="{edit}">✎ عدّل هذه الصفحة على GitHub</a></p>'}
<footer>Rin 1.0.0 · رخصة MIT</footer></main>
<aside id="toc">{'' if home else '<b>في هذه الصفحة</b>'+toc}</aside></div>
<script src="assets/app.js"></script></body></html>'''

CSS = r''':root{--bg:#fff;--fg:#1c2128;--mu:#64707d;--bd:#e3e7ec;--ac:#169c3c;--cb:#f4f6f8;--sb:#fafbfc}
[data-t=dark]{--bg:#0f1318;--fg:#e6edf3;--mu:#8b98a5;--bd:#262d36;--ac:#3ddc6a;--cb:#171c23;--sb:#12171d}
*{box-sizing:border-box}html{scroll-padding-top:70px}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.85 "IBM Plex Sans Arabic",system-ui,sans-serif}
a{color:var(--ac);text-decoration:none}a:hover{text-decoration:underline}
header{position:sticky;top:0;z-index:20;display:flex;gap:12px;align-items:center;padding:10px 18px;background:var(--bg);border-bottom:1px solid var(--bd)}
.logo{display:flex;align-items:center;gap:8px;font-weight:700;font-size:20px;color:var(--fg)}
#q{flex:1;max-width:420px;margin-inline-start:auto;padding:8px 14px;border:1px solid var(--bd);border-radius:10px;background:var(--cb);color:var(--fg);font:inherit}
header button{background:none;border:1px solid var(--bd);border-radius:8px;color:var(--fg);padding:5px 10px;cursor:pointer;font-size:16px}#mn{display:none}
#res{position:absolute;top:56px;inset-inline-start:50%;width:min(480px,92vw);background:var(--bg);border:1px solid var(--bd);border-radius:12px;box-shadow:0 12px 40px #0003;max-height:60vh;overflow:auto;display:none}
#res a{display:block;padding:10px 14px;color:var(--fg);border-bottom:1px solid var(--bd)}#res small{display:block;color:var(--mu)}
.wrap{display:grid;grid-template-columns:270px minmax(0,1fr) 220px;max-width:1400px;margin:auto}
#sb{position:sticky;top:57px;height:calc(100vh - 57px);overflow:auto;padding:16px;background:var(--sb);border-inline-end:1px solid var(--bd);font-size:14.5px}
#sb summary{cursor:pointer;font-weight:700;padding:6px 0}#sb a{display:block;padding:3px 12px;color:var(--mu);border-inline-start:2px solid transparent}
#sb a.on{color:var(--ac);border-color:var(--ac);font-weight:600}
main{padding:28px 40px;min-width:0}#toc{position:sticky;top:57px;height:fit-content;padding:24px 8px;font-size:13.5px}
#toc ul{list-style:none;padding-inline-start:12px;margin:4px 0}#toc a{color:var(--mu)}
article h1{font-size:2rem;margin-top:0}article h2{margin-top:2.2em;padding-bottom:.3em;border-bottom:1px solid var(--bd)}
.headerlink{opacity:0;margin-inline-start:6px}h2:hover .headerlink,h3:hover .headerlink{opacity:.6}
pre{position:relative;direction:ltr;text-align:left;background:var(--cb);border:1px solid var(--bd);border-radius:12px;padding:16px;overflow:auto;font-size:14px;line-height:1.65}
code{font-family:"JetBrains Mono",ui-monospace,Consolas,monospace;font-size:.88em}:not(pre)>code{background:var(--cb);padding:2px 6px;border-radius:6px;direction:ltr;unicode-bidi:embed}
.cp{position:absolute;top:8px;right:8px;font:12px sans-serif;padding:3px 9px;border:1px solid var(--bd);border-radius:6px;background:var(--bg);color:var(--mu);cursor:pointer}
.k{color:#c2185b}.s{color:#0b7a3b}.c{color:#7d8b99;font-style:italic}.n{color:#b45309}.f{color:#1565c0}
[data-t=dark] .k{color:#ff7ab2}[data-t=dark] .s{color:#7ee787}[data-t=dark] .n{color:#ffb86c}[data-t=dark] .f{color:#79c0ff}
.tw{overflow:auto}table{border-collapse:collapse;width:100%}th,td{border:1px solid var(--bd);padding:8px 12px;text-align:start}th{background:var(--cb)}
blockquote{margin:1em 0;padding:4px 16px;border-inline-start:4px solid var(--ac);background:var(--cb);border-radius:8px}img{max-width:100%}
.pnav{display:flex;justify-content:space-between;gap:12px;margin-top:48px}.pn{padding:12px 18px;border:1px solid var(--bd);border-radius:12px}
.edit,footer{color:var(--mu);font-size:14px;text-align:center;margin-top:28px}
.hero{text-align:center;padding:30px 0}.hero h1{font-size:2.6rem;margin:.3em 0}.hero p{color:var(--mu);font-size:1.15rem}
.cta{display:flex;gap:10px;justify-content:center;flex-wrap:wrap}.btn{background:var(--ac);color:#fff;padding:10px 24px;border-radius:12px;font-weight:700}
.btn.alt{background:none;color:var(--fg);border:1px solid var(--bd)}[data-t=dark] .btn:not(.alt){color:#04210d}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px}.card{border:1px solid var(--bd);border-radius:14px;padding:14px 16px;color:var(--fg)}
.card span{display:block;color:var(--mu);font-size:13px}.card:hover{border-color:var(--ac);text-decoration:none}
@media(max-width:1100px){.wrap{grid-template-columns:250px 1fr}#toc{display:none}}
@media(max-width:760px){.wrap{display:block}#mn{display:block}main{padding:20px 16px}.gh{display:none}
#sb{position:fixed;inset:57px 0 0 30%;z-index:15;height:auto;transform:translateX(120%);transition:.2s}#sb.open{transform:none}}'''

JS = r'''(function(){var d=document,r=d.documentElement,t=localStorage.getItem("rt")||(matchMedia("(prefers-color-scheme:dark)").matches?"dark":"light");r.dataset.t=t;
d.getElementById("th").onclick=function(){t=t=="dark"?"light":"dark";r.dataset.t=t;localStorage.setItem("rt",t)};
d.getElementById("mn").onclick=function(){d.getElementById("sb").classList.toggle("open")};
var K=/\b(fun|return|let|text|if|else|when|otherwise|while|for|in|match|case|enum|print|true|false|null|use|from|as|goal|achieve|interface|trait|abstract|final|override|static|private|protected|class|new|reckon|need|allow|deny|strict|import)\b/g;
d.querySelectorAll("pre code").forEach(function(c){var p=c.parentNode,b=d.createElement("button");b.className="cp";b.textContent="نسخ";
b.onclick=function(){navigator.clipboard.writeText(c.innerText);b.textContent="تم ✓";setTimeout(function(){b.textContent="نسخ"},1200)};p.appendChild(b);
if(/language-(bash|text|json|sh)/.test(c.className))return;
var s=c.textContent.replace(/&/g,"&amp;").replace(/</g,"&lt;"),o=[],i=0;
s=s.replace(/(\/\/.*|\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\])*"|`(?:\\.|[^`\\])*`)|(@[A-Za-z_.]+)|\b(\d+(?:\.\d+)?)\b|([A-Za-z_]\w*)(?=\()/g,
function(m,cm,st,at,nu,fn){var x=cm?"c":st?"s":at?"k":nu?"n":"f";o.push('<span class="'+x+'">'+m+'</span>');return"\u0001"+(o.length-1)+"\u0002"});
s=s.replace(K,'<span class="k">$1</span>').replace(/\u0001(\d+)\u0002/g,function(_,n){return o[n]});c.innerHTML=s});
var q=d.getElementById("q"),res=d.getElementById("res"),db=null;
function load(){return db?Promise.resolve(db):fetch("search.json").then(function(x){return x.json()}).then(function(j){return db=j})}
q.oninput=function(){var v=q.value.trim().toLowerCase();if(!v){res.style.display="none";return}
load().then(function(D){var h=D.map(function(e){var ti=e.t.toLowerCase().indexOf(v)>=0,ix=e.x.toLowerCase().indexOf(v);return{e:e,s:(ti?100:0)+(ix>=0?10:0),i:ix}}).filter(function(a){return a.s}).sort(function(a,b){return b.s-a.s}).slice(0,8);
res.innerHTML=h.length?h.map(function(a){var sn=a.i>=0?a.e.x.substr(Math.max(0,a.i-30),90):"";return'<a href="'+a.e.u+'">'+a.e.t+"<small>"+sn.replace(/</g,"&lt;")+"</small></a>"}).join(""):"<a>لا نتائج</a>";res.style.display="block"})};
d.addEventListener("keydown",function(e){if(e.key=="/"&&d.activeElement!=q){e.preventDefault();q.focus()}if(e.key=="Escape")res.style.display="none"});
d.addEventListener("click",function(e){if(!res.contains(e.target)&&e.target!=q)res.style.display="none"})})();'''

if __name__ == "__main__": build()

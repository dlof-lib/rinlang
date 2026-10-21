(function(){var d=document,r=d.documentElement,t=localStorage.getItem("rt")||(matchMedia("(prefers-color-scheme:dark)").matches?"dark":"light");r.dataset.t=t;
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
d.addEventListener("click",function(e){if(!res.contains(e.target)&&e.target!=q)res.style.display="none"})})();
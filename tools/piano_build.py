#!/usr/bin/env python3
"""يبني: (1) web/piano/piano.bundle.rin  (2) lib/piano.og.rin/data/themes.json  (3) web/piano/index.html
من lib/piano.og.rin + examples/piano. يستعمل محرك rin لتوليد مشاهد الأمثلة الحقيقية.
الاستعمال: python3 tools/piano_build.py [--rin /path/to/rin]
"""
import base64, json, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIB = os.path.join(ROOT, "lib/piano.og.rin")
RIN = "rin"
if "--rin" in sys.argv:
    RIN = sys.argv[sys.argv.index("--rin") + 1]

def rd(p, enc="utf-8"):
    with open(p, encoding=enc) as f:
        return f.read()

dis = json.loads(rd(os.path.join(LIB, "disconom.json")))
bundle = "\n".join(rd(os.path.join(LIB, p)) for p in dis["parts"])
open(os.path.join(ROOT, "web/piano/piano.bundle.rin"), "w", encoding="utf-8").write(bundle)

# الثيمات → data/themes.json (مصدرها pn_themes في Rin). بلا محرك rin يُترك الملف الحالي كما هو.
try:
    r = subprocess.run([RIN, "-c", '@import "piano"; print jsonEncode(pn_themes());'], cwd=ROOT, capture_output=True, text=True)
    themes = json.loads(r.stdout[r.stdout.index("{"):])
    json.dump(themes, open(os.path.join(LIB, "data/themes.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=2)
except (OSError, ValueError):
    print("تنبيه: rin غير متاح — بقي data/themes.json دون تحديث")

# الأمثلة: نزيل @import وأسطر pn_save ونضيف عنواناً
ex = []
titles = {"01_hello": "مرحباً — بطاقة وتدرّج", "02_shapes": "معرض الأشكال", "03_dashboard": "لوحة مؤشرات", "04_art": "فن توليدي متحرّك"}
for k, title in titles.items():
    src = rd(os.path.join(ROOT, f"examples/piano/{k}.rin"))
    src = "\n".join(l for l in src.splitlines() if not l.startswith("@import") and "pn_save(" not in l).strip() + "\n"
    puo = rd(os.path.join(ROOT, f"examples/piano/out/{k}.puo")).strip()
    ex.append({"title": title, "rin": src, "puo": puo})

def b64(p):
    return base64.b64encode(open(p, "rb").read()).decode()

tpl = rd(os.path.join(ROOT, "web/piano/index.template.html"))
js = rd(os.path.join(LIB, "native/piano.js"))
html = (tpl.replace("__PIANO_JS__", js.replace("</script", "<\\/script"))
           .replace("__BUNDLE__", json.dumps(bundle, ensure_ascii=False).replace("</", "<\\/"))
           .replace("__EXAMPLES__", json.dumps(ex, ensure_ascii=False).replace("</", "<\\/"))
           .replace("__ICON64__", b64(os.path.join(ROOT, "assets/branding/piano/piano_icon_64.png")))
           .replace("__ICON32__", b64(os.path.join(ROOT, "assets/branding/piano/piano_icon_32.png")))
           .replace("__VERSION__", dis["version"]))
open(os.path.join(ROOT, "web/piano/index.html"), "w", encoding="utf-8").write(html)
print("built:", len(bundle), "bytes bundle,", len(html), "bytes index.html")

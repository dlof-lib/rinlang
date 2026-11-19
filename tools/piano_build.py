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

# هوية الصفحة: web/piano/piano.app.rin (container.app) هي مصدر الحقيقة للاسم/الإصدار/الأيقونات/الوصف/SEO.
#  (1) نتحقق أن disconom.json يطابقها (الاسم والإصدار والرخصة) — لا نسختان مختلفتان لنفس المعلومة.
#  (2) نحقن <head> المُولَّد في index.template.html بين <!-- rin:app-head --> و<!-- /rin:app-head -->.
# بلا rin (أو rin قديم بلا `rin app`) تبقى الكتلة المحقونة سابقاً في القالب كما هي (CI لا يبني rin) مع تنبيه.
APP = os.path.join(ROOT, "web/piano/piano.app.rin")
APP_DIR = "assets/branding/piano"
APP_URL = "https://dlof-lib.github.io/rinlang/piano/"   # يطابق homepage في piano.app.rin (مسار النشر على Pages)
TEMPLATE = os.path.join(ROOT, "web/piano/index.template.html")

def rin_app(*args):
    try:
        return subprocess.run([RIN, "app", *args], cwd=ROOT, capture_output=True, text=True)
    except OSError:
        return None

_info = rin_app("info", APP, "--json", "--dir", APP_DIR)
if _info is not None and _info.returncode == 0 and "{" in _info.stdout:
    ident = json.loads(_info.stdout[_info.stdout.index("{"):])
    bad = [f"{k}: app.rin={ident.get(k)!r} ≠ disconom={dis.get(k)!r}" for k in ("name", "version", "license") if ident.get(k) != dis.get(k)]
    if bad:
        sys.exit("تعارض بين web/piano/piano.app.rin و lib/piano.og.rin/disconom.json:\n  " + "\n  ".join(bad))
    chk = rin_app("check", APP, "--dir", APP_DIR)
    if chk.returncode != 0:
        sys.exit("هوية piano بها أخطاء:\n" + chk.stdout + chk.stderr)
    inj = rin_app("inject", APP, "--into", TEMPLATE, "--dir", APP_DIR, "--embed", "4096", "--assets-url", APP_URL)
    if inj.returncode != 0:
        sys.exit("تعذّر حقن الهوية في القالب:\n" + inj.stdout + inj.stderr)
    print("identity:", inj.stdout.strip())
else:
    print("تنبيه: `rin app` غير متاح — بقيت كتلة <head> المحقونة سابقاً في القالب دون تحديث ولم تُفحص الهوية")

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
titles = {"01_hello": "مرحباً — بطاقة وتدرّج", "02_shapes": "معرض الأشكال", "03_dashboard": "لوحة مؤشرات", "04_art": "فن توليدي متحرّك", "06_ui_kit": "تصميم واجهة: هاتف + مكوّنات", "07_diagram": "مخطط انسيابي", "08_mask_fade": "قناع تلاشٍ", "09_dsl": "لغة الكتل piano.Poster { … }", "11_film": "حركة بكود المستخدم (pn_film)", "12_film": "حركة بلغة الكتل ($x.Frame)"}
for k, title in titles.items():
    pth = os.path.join(ROOT, f"examples/piano/{k}.rin")
    if not os.path.exists(pth):
        pth = os.path.join(ROOT, f"examples/piano/{k}.pia")
    src = rd(pth)
    if pth.endswith(".pia"):  # نحذف رأس التعليق الخارجي بنمط *// … //* (ليس صالحاً في تبويب Rin)
        i = src.find("@import")
        src = src[i:] if i >= 0 else src
    src = "\n".join(l for l in src.splitlines() if not l.startswith("@import") and "pn_save(" not in l and "$save" not in l and not l.startswith("print ")).strip() + "\n"
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

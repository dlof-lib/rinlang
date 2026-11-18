#!/usr/bin/env python3
"""فحص piano الشامل — يتأكد أن Rin وC++ وJS تنتج SVG متطابقة بايتاً ببايت، وأن الثيمات متطابقة.
   python3 tools/piano_check.py [--rin /path/to/rin]      (يحتاج g++ وnode)
"""
import glob, json, os, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RIN = sys.argv[sys.argv.index("--rin") + 1] if "--rin" in sys.argv else "rin"
LIB = os.path.join(ROOT, "lib/piano.og.rin")
fail = 0

def run(cmd, **kw):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, **kw)

def check(ok, msg):
    global fail
    print(("✓ " if ok else "✗ ") + msg)
    if not ok:
        fail += 1

# 1) اختبار النواة
r = run([RIN, "tests/verification/piano_basic.rin"])
want = open(os.path.join(ROOT, "tests/verification/piano_basic.expected"), encoding="utf-8").read()
check(r.stdout.strip() == want.strip(), "tests/verification/piano_basic.rin == .expected")
r = run([RIN, "tests/verification/piano_dsl.rin"])
want = open(os.path.join(ROOT, "tests/verification/piano_dsl.expected"), encoding="utf-8").read()
check(r.stdout.strip() == want.strip(), "tests/verification/piano_dsl.rin == .expected (كتل piano.X { })")

r = run([RIN, "tests/verification/piano_film.rin"])
want = open(os.path.join(ROOT, "tests/verification/piano_film.expected"), encoding="utf-8").read()
check(r.stdout.strip() == want.strip(), "tests/verification/piano_film.rin == .expected (حركة + تعليقات)")

# تضمين المفسّر مطابق للأجزاء الحالية (شغّل tools/embed_piano.py إن فشل)
meta = json.load(open(os.path.join(LIB, "disconom.json"), encoding="utf-8"))
body = "\n".join(open(os.path.join(LIB, p), encoding="utf-8").read().rstrip("\n") + "\n" for p in meta["parts"]).rstrip("\n")
hdr = open(os.path.join(ROOT, "app/src/main/cpp/rin_stdlib_libs.h"), encoding="utf-8").read()
check(body in hdr, "rin_stdlib_libs.h يحوي أجزاء piano الحالية")

# 2) تشغيل الأمثلة
for f in sorted(glob.glob(os.path.join(ROOT, "examples/piano/[01][0-9]_*.rin")) + glob.glob(os.path.join(ROOT, "examples/piano/[01][0-9]_*.pia"))):
    r = run([RIN, os.path.relpath(f, ROOT)])
    check(r.returncode == 0 and "error" not in r.stdout.lower(), "example " + os.path.basename(f))

# 3) C++
tmp = tempfile.mkdtemp()
cli = os.path.join(tmp, "piano_cli")
r = run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", os.path.join(LIB, "native/piano_cli.cpp"), "-o", cli])
check(r.returncode == 0 and r.stderr == "", "g++ piano_cli (بلا تحذيرات)")

# 4) تطابق الثلاثة
js = os.path.join(LIB, "native/piano.js")
for puo in sorted(glob.glob(os.path.join(ROOT, "examples/piano/out/[01][0-9]_*.puo"))):
    base = puo[:-4]
    if not os.path.exists(base + ".svg"):
        continue
    want = open(base + ".svg", encoding="utf-8").read()
    n = os.path.basename(base)
    animated = isinstance(json.load(open(puo, encoding="utf-8")).get("anim"), dict)
    fn = "renderAnim" if animated else "render"
    node = run(["node", os.path.join(ROOT, "tools/piano_render.js"), js, puo, fn]).stdout
    check(node == want, f"{n}: JS  == Rin" + (" (متحرك)" if animated else ""))
    if not animated:  # مُصيِّر C++ للمشاهد الثابتة فقط (يتجاهل anim)
        check(run([cli, puo]).stdout == want, f"{n}: C++ == Rin")

# 5) الثيمات
r = run([RIN, "-c", '@import "piano"; print jsonEncode(pn_themes());'])
a = json.loads(r.stdout[r.stdout.index("{"):])
b = json.load(open(os.path.join(LIB, "data/themes.json"), encoding="utf-8"))
check(a == b, "data/themes.json == pn_themes()")

# 6) .puo مخالف: C++ يرفض
bad = os.path.join(tmp, "bad.puo"); open(bad, "w").write('{"a":1}')
check(run([cli, bad]).returncode == 1, "C++ يرفض مشهداً غير صالح")

# 7) خبز صفحات indsin (يتطلب build/indsin2web؛ يُتخطّى إن لم يوجد)
i2w = os.path.join(ROOT, "build/indsin2web")
if os.path.exists(i2w):
    for pg in ("13_indsin_blocks", "14_indsin_film"):
        out_html = os.path.join(tmp, pg + ".html")
        r = run(["python3", "tools/piano_indsin_bake.py", f"examples/piano/{pg}.rin", "-o", os.path.join(tmp, pg + ".rin"), "--rin", RIN, "--html", out_html, "--indsin2web", i2w])
        html = open(out_html, encoding="utf-8").read() if os.path.exists(out_html) else ""
        check("data:image/svg+xml;base64," in html and "<img" in html, f"bake {pg}: الصور مخبوزة داخل صفحة الويب")
else:
    print("· تخطّي فحص الخبز: build/indsin2web غير مبني (bash scripts/build_indsin2web.sh)")

print("\nالنتيجة:", "نجح كل شيء" if not fail else f"{fail} إخفاق")
sys.exit(1 if fail else 0)

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
r = run([RIN, "tests/piano/core.rin"])
out = "\n".join(l for l in r.stdout.splitlines() if not l.startswith("📥")) + "\n"
check(out == open(os.path.join(ROOT, "tests/piano/core.expected"), encoding="utf-8").read(), "tests/piano/core.rin == core.expected")

# 2) تشغيل الأمثلة
for f in sorted(glob.glob(os.path.join(ROOT, "examples/piano/0*.rin"))):
    r = run([RIN, os.path.relpath(f, ROOT)])
    check(r.returncode == 0 and "error" not in r.stdout.lower(), "example " + os.path.basename(f))

# 3) C++
tmp = tempfile.mkdtemp()
cli = os.path.join(tmp, "piano_cli")
r = run(["g++", "-std=c++17", "-O1", "-Wall", "-Wextra", os.path.join(LIB, "native/piano_cli.cpp"), "-o", cli])
check(r.returncode == 0 and r.stderr == "", "g++ piano_cli (بلا تحذيرات)")

# 4) تطابق الثلاثة
js = os.path.join(LIB, "native/piano.js")
for puo in sorted(glob.glob(os.path.join(ROOT, "examples/piano/out/0[1-4]*.puo"))):
    base = puo[:-4]
    want = open(base + ".svg", encoding="utf-8").read()
    cpp = run([cli, puo]).stdout
    node = run(["node", "-e", "const P=require(process.argv[1]);const fs=require('fs');process.stdout.write(P.render(JSON.parse(fs.readFileSync(process.argv[2],'utf8')))+'\\n')", js, puo]).stdout
    n = os.path.basename(base)
    check(cpp == want, f"{n}: C++ == Rin")
    check(node == want, f"{n}: JS  == Rin")

# 5) الثيمات
r = run([RIN, "-c", '@import "piano"; print jsonEncode(pn_themes());'])
a = json.loads(r.stdout[r.stdout.index("{"):])
b = json.load(open(os.path.join(LIB, "data/themes.json"), encoding="utf-8"))
check(a == b, "data/themes.json == pn_themes()")

# 6) .puo مخالف: C++ يرفض
bad = os.path.join(tmp, "bad.puo"); open(bad, "w").write('{"a":1}')
check(run([cli, bad]).returncode == 1, "C++ يرفض مشهداً غير صالح")

print("\nالنتيجة:", "نجح كل شيء" if not fail else f"{fail} إخفاق")
sys.exit(1 if fail else 0)

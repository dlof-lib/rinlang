#!/usr/bin/env python3
"""test_icons.py — اختبارات مكتبة أيقونات indsin.   python3 tools/icons/test_icons.py   (يستعمل RIN_BIN أو rin من PATH إن وُجد)"""
import os, re, shutil, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import icons_def as D, raster, gen_indsin_icons as G

fails = 0
def check(cond, label):
    global fails
    print(("  [PASS] " if cond else "  [FAIL] ") + label)
    if not cond: fails += 1

print("-- تعريفات --")
check(len(D.ICONS) >= 150, "%d أيقونة (>= 150)" % len(D.ICONS))
bad_name = [n for n in D.ICONS if not re.fullmatch(r"[a-z][a-z0-9]*(-[a-z0-9]+)*", n)]
check(not bad_name, "كل الأسماء kebab-case صالحة %s" % bad_name)
bad_cat = [n for n, (c, _) in D.ICONS.items() if c not in {x[0] for x in D.CATEGORIES}]
check(not bad_cat, "كل فئة معرّفة %s" % bad_cat)
check(not (set(D.ALIASES) & set(D.ICONS)), "لا اسم بديل يطابق اسماً أساسياً")
check(all(t in D.ICONS for t in D.ALIASES.values()), "كل اسم بديل يشير لأيقونة موجودة")
check(all(f in D.ICONS for f in D.FILLABLE), "قائمة FILLABLE كلها موجودة")

print("-- المسارات --")
bad_chars = [n for n, (_, p) in D.ICONS.items() if not re.fullmatch(r"[MLHVCAZ0-9 .\-]+", p) or not p.startswith("M")]
check(not bad_chars, "الأوامر المدعومة فقط (M L H V C A Z) %s" % bad_chars)
oob = []; empty = []
for n, (_, p) in D.ICONS.items():
    pts = [pt for sub, _ in raster.parse(p) for pt in sub]
    if not pts: empty.append(n); continue
    xs = [x for x, _ in pts]; ys = [y for _, y in pts]
    if min(xs) < 1 or max(xs) > 23 or min(ys) < 1 or max(ys) > 23: oob.append((n, round(min(xs), 1), round(max(xs), 1), round(min(ys), 1), round(max(ys), 1)))
check(not empty, "لا مسار فارغ %s" % empty)
check(not oob, "كل المسارات داخل الهامش الآمن [1,23] %s" % oob)
dups = len({p for _, p in D.ICONS.values()}) != len(D.ICONS)
check(not dups, "لا أيقونتان بنفس المسار")

print("-- توافق سجل المحرّك --")
engine = "home menu close search settings user edit delete download upload play pause stop check plus minus arrow-left arrow-right arrow-up arrow-down eye eye-off lock unlock refresh share more".split()
web = "star heart info warning error bell mail trash calendar camera phone link folder file back next up down".split()
res = lambda n: n in D.ICONS or n in D.ALIASES
check(all(res(n) for n in engine), "أسماء المحرّك الـ27 كلها لها أيقونة")
check(all(res(n) for n in web), "أسماء معاينة الويب كلها لها أيقونة")

print("-- الملفات المولَّدة --")
svgdir = os.path.join(ROOT, "assets", "indsin-icons", "svg")
check(len(os.listdir(svgdir)) == len(D.ICONS), "ملف SVG لكل أيقونة")
sprite = open(os.path.join(ROOT, "assets", "indsin-icons", "indsin-icons.svg"), encoding="utf-8").read()
check(sprite.count("<symbol ") == len(D.ICONS), "sprite فيه symbol لكل أيقونة")
hdr = open(os.path.join(ROOT, "app", "src", "main", "cpp", "indsin", "rin_indsin_iconpaths.h"), encoding="utf-8").read()
check(hdr.count('{"') >= len(D.ICONS) + len(D.ALIASES), "ترويسة C++ فيها كل الأيقونات والأسماء البديلة")

print("-- مكتبة Rin مقابل Python --")
rin = os.environ.get("RIN_BIN") or shutil.which("rin")
if not rin: print("  [SKIP] لا يوجد مفسّر rin (عيّن RIN_BIN)")
else:
    names = sorted(D.ICONS)
    body = '@import "lib/indsinicons.og.rin";\nlet names = ic_names();\nlet i = 0;\nwhile (i < len(names)) { print "@@" + ic_svg(names[i], nil); i = i + 1; }\n' \
           'print "##" + str(ic_count());\nprint "##" + ic_svg("gear", {"size": 32, "color": "#e11d48", "filled": false, "stroke": 1.5});\n' \
           'print "##" + str(ic_has("nope"));\nprint "##" + str(len(ic_search("arrow")));\nprint "##" + ic_register("t-x", "dev", "M1 1L2 2");\nprint "##" + str(ic_count());\n' \
           'print "##" + str(ic_validPath("<script>"));\n'
    with tempfile.NamedTemporaryFile("w", suffix=".rin", delete=False, encoding="utf-8") as fh: fh.write(body); tmp = fh.name
    out = subprocess.run([rin, tmp], cwd=ROOT, capture_output=True, text=True, timeout=120).stdout
    os.unlink(tmp)
    got = [l[2:] for l in out.splitlines() if l.startswith("@@")]; extra = [l[2:] for l in out.splitlines() if l.startswith("##")]
    check(got == [G.svg(n).replace('stroke-linejoin="round">', 'stroke-linejoin="round" aria-hidden="true">') for n in names], "ic_svg() لكل الأيقونات يطابق مولّد Python حرفياً")
    check(extra[:1] == [str(len(D.ICONS))], "ic_count() = %d" % len(D.ICONS))
    check("width=\"32\"" in extra[1] and "stroke=\"#e11d48\"" in extra[1] and "stroke-width=\"1.5\"" in extra[1], "خيارات size/color/stroke تعمل (عبر الاسم البديل gear)")
    check(extra[2] == "false", "ic_has(اسم مجهول) = false")
    check(int(extra[3]) >= 20, "ic_search('arrow') يجد %s أيقونة" % extra[3])
    check(extra[4] == "t-x" and extra[5] == str(len(D.ICONS) + 1), "ic_register يضيف أيقونة جديدة")
    check(extra[6] == "false", "ic_validPath يرفض مسارات فيها وسوم")

print("ALL PASSED" if not fails else "FAILED: %d" % fails)
sys.exit(1 if fails else 0)

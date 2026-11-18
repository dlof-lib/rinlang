#!/usr/bin/env python3
"""يخبز صفحة indsin فيها شيفرة Rin/piano إلى صفحة ويب جاهزة.

مترجم indsin2web لا يشغّل مكتبات Rin (@import)، لذلك نشغّل قسم <Rin> بالمحرّك الأصلي مرة واحدة، ونكتب القيم النهائية
(للـ warp ولكل تعبير src={…}) داخل الصفحة. النتيجة صفحة ثابتة المحتوى — لكن صور piano المتحركة (SVG/SMIL) تتحرك داخل <img>.
الاستعمال:
    python3 tools/piano_indsin_bake.py page.rin -o baked.rin [--rin build/rin]
    build/indsin2web baked.rin -o page.html --lang ar --dir rtl
(أو أضف  --html page.html [--indsin2web build/indsin2web]  ليفعل الخطوتين.)
"""
import argparse, json, os, re, subprocess, sys, tempfile

MARK = "@@PIANO_BAKE@@"

def balanced(s, i):
    """يُرجع فهرس القوس } المغلق للقوس { عند i (يتجاوز النصوص)."""
    depth = 0; q = None; j = i
    while j < len(s):
        c = s[j]
        if q:
            if c == "\\": j += 1
            elif c == q: q = None
        elif c in "\"'": q = c
        elif c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0: return j
        j += 1
    raise SystemExit("قوس { غير مغلق في الصفحة")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("page"); ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--rin", default="rin"); ap.add_argument("--html"); ap.add_argument("--indsin2web", default="build/indsin2web")
    a = ap.parse_args()
    src = open(a.page, encoding="utf-8").read()
    # نبحث في نسخة تُحجب فيها تعليقات HTML (بنفس الأطوال) كي لا يلتقط «<Rin>» المذكور داخل تعليق
    scan = re.sub(r"<!--.*?-->", lambda mm: " " * len(mm.group(0)), src, flags=re.S)

    m = re.search(r"<Rin>(.*?)</Rin>", scan, re.S)
    code = src[m.start(1):m.end(1)] if m else ""
    warps = re.findall(r"<warp\s+(\w+)=\{", src)
    warp_spans = []
    for w in re.finditer(r"<warp\s+(\w+)=\{", scan):
        b = w.end() - 1; e = balanced(src, b)
        warp_spans.append((w.group(1), w.start(), src.index("/>", e) + 2, src[b + 1:e]))
    # تعابير src={…} المباشرة
    exprs = []
    for s in re.finditer(r"\bsrc=\{", scan):
        b = s.end() - 1; e = balanced(src, b)
        exprs.append((s.start(), e + 1, src[b + 1:e]))

    lines = [f"let {n} = {init};" for n, _, _, init in warp_spans]
    lines.append(code)
    for n, _, _, _ in warp_spans:
        lines.append(f'print "{MARK}W:{n}"; print jsonEncode({n});')
    for i, (_, _, ex) in enumerate(exprs):
        lines.append(f'print "{MARK}E:{i}"; print jsonEncode({ex});')
    with tempfile.NamedTemporaryFile("w", suffix=".rin", delete=False, encoding="utf-8") as f:
        f.write("\n".join(lines)); tmp = f.name
    r = subprocess.run([a.rin, tmp], capture_output=True, text=True, encoding="utf-8", errors="replace")
    os.unlink(tmp)
    out = r.stdout
    if MARK not in out:
        sys.stderr.write(out[-1500:] + r.stderr[-800:]); raise SystemExit("فشل تشغيل قسم <Rin>")
    vals = {}
    for part in out.split(MARK)[1:]:
        key, _, rest = part.partition("\n")
        vals[key.strip()] = rest.strip().split("\n")[0]

    # نبني الصفحة المخبوزة بالاستبدال من النهاية إلى البداية (كي لا تتزحزح المواضع)
    edits = []
    for n, s, e, _ in warp_spans:
        edits.append((s, e, f"<warp {n}={{{vals['W:' + n]}}} />"))
    for i, (s, e, ex) in enumerate(exprs):
        edits.append((s, e, f"src={{{vals['E:' + str(i)]}}}"))
    if m: edits.append((m.start(), m.end(), ""))
    for s, e, rep in sorted(edits, reverse=True):
        src = src[:s] + rep + src[e:]
    # معالجات تستدعي دوال معرَّفة في <Rin> لا وجود لها في الويب: تُحذف (الصفحة لقطة)
    funs = set(re.findall(r"\bfun\s+(\w+)", code))
    removed = 0
    def drop(mm):
        nonlocal removed
        body = mm.group(0)
        if any(re.search(rf"\b{f}\s*\(", body) for f in funs):
            removed += 1; return ""
        return body
    src = re.sub(r"\son\w+=\{[^{}]*\}", drop, src)
    open(a.out, "w", encoding="utf-8").write(src)
    print(f"baked: {a.out}  (warp: {len(warp_spans)}, src: {len(exprs)}, معالجات محذوفة: {removed})")
    if a.html:
        r2 = subprocess.run([a.indsin2web, a.out, "-o", a.html, "--lang", "ar", "--dir", "rtl"], capture_output=True, text=True)
        sys.stdout.write(r2.stdout + r2.stderr[-600:])

if __name__ == "__main__":
    main()

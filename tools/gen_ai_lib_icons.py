#!/usr/bin/env python3
"""يولّد هوية بصرية كاملة لمكتبات rindf / rinml / rinai من وصف واحد (مصدر وحيد للحقيقة):

  app/src/main/res/drawable/ic_lib_<n>.xml        رمز أبيض 24dp متعدّد الشفافية — يُعرَض فوق البلاطة (يحترم tint الأبيض)
  app/src/main/res/drawable/ic_lib_<n>_badge.xml  شارة ملوّنة كاملة 48dp: تدرّج + لمعة + ظل + حدّ داخلي + لون مميِّز
  app/src/main/res/drawable/bg_icon_gradient_<c>.xml  بلاطة تدرّج (نفس شكل bg_icon_gradient_green: 13dp وزاوية 135)
  docs/icons/<n>.svg                              الشارة بصيغة SVG للوثائق
  docs/icons/preview.png                          ورقة معاينة (تُرسَم بمُصيِّر PIL مطابق للمواصفات)

تشغيل:  python3 tools/gen_ai_lib_icons.py   (يحتاج numpy و Pillow للمعاينة فقط)
المبدأ: "الرمز" يُرسَم بعناصر هندسية (rrect/circle/poly/line/ring) بأدوار base/accent؛
في وضع البلاطة كل شيء أبيض (الدور accent = أبيض كامل)، وفي وضع الشارة يصير accent كهرمانياً."""
import pathlib
import numpy as np
from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parent.parent
RES = ROOT / "app/src/main/res/drawable"
ICONS_DIR = ROOT / "docs/icons"
AMBER = "#FFD166"

# ----------------------------------------------------------------------------- عناصر
def _p(d, k):
    d.update(k); return d

def rrect(x, y, w, h, r=0, **k): return _p(dict(k="rrect", x=x, y=y, w=w, h=h, r=r, st="fill", a=1, role="base"), k)
def circle(cx, cy, r, **k): return _p(dict(k="circle", cx=cx, cy=cy, r=r, st="fill", a=1, role="base"), k)
def poly(pts, **k): return _p(dict(k="poly", pts=pts, st="fill", a=1, role="base"), k)
def line(pts, w, **k): return _p(dict(k="line", pts=pts, st="stroke", sw=w, a=1, role="base"), k)
def ring(cx, cy, r, w, **k): return _p(dict(k="ring", cx=cx, cy=cy, r=r, st="stroke", sw=w, a=1, role="base"), k)
def frame(x, y, w, h, r, sw, **k): return _p(dict(k="rrect", x=x, y=y, w=w, h=h, r=r, st="stroke", sw=sw, a=1, role="base"), k)

def star(cx, cy, R, r, **k):
    return poly([(cx, cy - R), (cx + r, cy - r), (cx + R, cy), (cx + r, cy + r),
                 (cx, cy + R), (cx - r, cy + r), (cx - R, cy), (cx - r, cy - r)], **k)

# ----------------------------------------------------------------------------- الرموز (إطار 24x24)
def glyph_rindf():  # جدول بيانات: ترويسة، شبكة، خلية مميَّزة
    return [
        frame(3.2, 4.2, 17.6, 15.6, 2.8, 1.6),
        rrect(3.2, 4.2, 17.6, 5.6, 2.8), rrect(3.2, 7.0, 17.6, 2.8, 0),
        line([(9.1, 9.8), (9.1, 18.9)], 1.1, a=.5), line([(14.9, 9.8), (14.9, 18.9)], 1.1, a=.5),
        line([(4.1, 14.8), (19.9, 14.8)], 1.1, a=.5),
        rrect(15.7, 15.6, 4.4, 3.5, 1.0, role="accent"),
        rrect(4.4, 15.6, 3.6, 3.5, 1.0, a=.55),
    ]

def glyph_rinml():  # شبكة عصبية 3-2-1
    conns = []
    for yi in (6, 12, 18):
        for yh in (9, 15):
            conns.append(line([(4.5, yi), (12, yh)], 1.0, a=.45))
    for yh in (9, 15):
        conns.append(line([(12, yh), (19.5, 12)], 1.0, a=.45))
    nodes = [circle(4.5, y, 1.9, a=.8) for y in (6, 12, 18)]
    nodes += [circle(12, y, 2.2) for y in (9, 15)]
    nodes += [ring(19.5, 12, 4.0, 1.0, a=.35), circle(19.5, 12, 2.7, role="accent")]
    return conns + nodes

def glyph_rinai():  # حاوية متساوية القياس (container) + شرارة ذكاء
    cx, cy, R = 10.8, 14.4, 8.2
    top, ur, lr = (cx, cy - R), (cx + 7.1, cy - R / 2), (cx + 7.1, cy + R / 2)
    bot, ll, ul, c = (cx, cy + R), (cx - 7.1, cy + R / 2), (cx - 7.1, cy - R / 2), (cx, cy)
    return [
        poly([top, ur, c, ul], a=1.0),
        poly([ul, c, bot, ll], a=.68),
        poly([ur, lr, bot, c], a=.42),
        star(18.6, 5.4, 3.7, 0.95, role="accent"),
        star(4.6, 4.6, 1.8, 0.5, a=.75),
    ]

LIBS = {
    "rindf": dict(glyph=glyph_rindf, light="#2DD4BF", dark="#0F766E", color="teal",
                  desc="جدول بيانات (DataFrame): ترويسة وشبكة وخلية مميَّزة"),
    "rinml": dict(glyph=glyph_rinml, light="#A78BFA", dark="#5B21B6", color="violet",
                  desc="شبكة عصبية 3-2-1 بعقدة خرج مميَّزة"),
    "rinai": dict(glyph=glyph_rinai, light="#60A5FA", dark="#3730A3", color="indigo",
                  desc="حاوية متساوية القياس (container) تحمل شرارة الذكاء الاصطناعي"),
}

# ----------------------------------------------------------------------------- تحويل إلى pathData
def f(v): return ("%.2f" % v).rstrip("0").rstrip(".")

def circle_d(cx, cy, r):
    return f"M{f(cx - r)},{f(cy)} a{f(r)},{f(r)} 0 1,0 {f(2 * r)},0 a{f(r)},{f(r)} 0 1,0 {f(-2 * r)},0 Z"

def rrect_d(x, y, w, h, r):
    if r <= 0: return f"M{f(x)},{f(y)} H{f(x + w)} V{f(y + h)} H{f(x)} Z"
    return (f"M{f(x + r)},{f(y)} H{f(x + w - r)} A{f(r)},{f(r)} 0 0,1 {f(x + w)},{f(y + r)} V{f(y + h - r)} "
            f"A{f(r)},{f(r)} 0 0,1 {f(x + w - r)},{f(y + h)} H{f(x + r)} A{f(r)},{f(r)} 0 0,1 {f(x)},{f(y + h - r)} "
            f"V{f(y + r)} A{f(r)},{f(r)} 0 0,1 {f(x + r)},{f(y)} Z")

def path_d(p):
    k = p["k"]
    if k == "rrect": return rrect_d(p["x"], p["y"], p["w"], p["h"], p["r"])
    if k in ("circle", "ring"): return circle_d(p["cx"], p["cy"], p["r"])
    pts = p["pts"]
    d = "M" + " L".join(f"{f(x)},{f(y)}" for x, y in pts)
    return d + (" Z" if k == "poly" else "")

# ----------------------------------------------------------------------------- Android VectorDrawable
def vec_path(p, color, alpha_mul=1.0, indent="    "):
    a = p["a"] * alpha_mul
    L = [f"{indent}<path"]
    if p["st"] == "fill":
        L.append(f'{indent}    android:fillColor="{color}"')
        if a < 0.999: L.append(f'{indent}    android:fillAlpha="{f(a)}"')
    else:
        L += [f'{indent}    android:strokeColor="{color}"', f'{indent}    android:strokeWidth="{f(p["sw"])}"',
              f'{indent}    android:strokeLineCap="round"', f'{indent}    android:strokeLineJoin="round"']
        if a < 0.999: L.append(f'{indent}    android:strokeAlpha="{f(a)}"')
    L.append(f'{indent}    android:pathData="{path_d(p)}" />')
    return "\n".join(L)

def vec_gradient_path(d, x0, y0, x1, y1, stops, indent="    "):
    items = "\n".join(f'{indent}            <item android:offset="{o}" android:color="{c}" />' for o, c in stops)
    return (f'{indent}<path android:pathData="{d}">\n{indent}    <aapt:attr name="android:fillColor">\n'
            f'{indent}        <gradient android:type="linear" android:startX="{f(x0)}" android:startY="{f(y0)}" '
            f'android:endX="{f(x1)}" android:endY="{f(y1)}">\n{items}\n{indent}        </gradient>\n'
            f'{indent}    </aapt:attr>\n{indent}</path>')

def glyph_xml(name, spec):
    body = "\n".join(vec_path(p, "#FFFFFF") for p in spec["glyph"]())
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            f"<!-- رمز مكتبة {name} ({spec['desc']}).\n"
            "     أبيض متعدّد الشفافية ليُعرَض فوق بلاطة التدرّج في شاشة \"المكتبات\" (ImageView بـ tint أبيض).\n"
            "     مولَّد بـ tools/gen_ai_lib_icons.py — لا تعدّله يدوياً. -->\n"
            '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    android:width="24dp" android:height="24dp"\n'
            '    android:viewportWidth="24" android:viewportHeight="24">\n\n' + body + "\n\n</vector>\n")

def badge_xml(name, spec):
    g = spec["glyph"]()
    sh = "\n".join(vec_path(p, "#000000", 0.22, "            ") for p in g)
    fg = "\n".join(vec_path(p, AMBER if p["role"] == "accent" else "#FFFFFF", 1.0, "        ") for p in g)
    bg = vec_gradient_path(rrect_d(2, 2, 44, 44, 12), 40, 44, 8, 4, [(0, spec["dark"]), (1, spec["light"])])
    gloss = vec_gradient_path(rrect_d(2, 2, 44, 26, 12), 24, 2, 24, 28, [(0, "#3DFFFFFF"), (1, "#00FFFFFF")])
    depth = vec_gradient_path(rrect_d(2, 22, 44, 24, 12), 24, 22, 24, 46, [(0, "#00000000"), (1, "#33000000")])
    edge = ('    <path\n        android:pathData="' + rrect_d(2.5, 2.5, 43, 43, 11.5) + '"\n'
            '        android:strokeColor="#33FFFFFF"\n        android:strokeWidth="1" />')
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            f"<!-- شارة مكتبة {name} الملوّنة ({spec['desc']}): تدرّج + لمعة علوية + عمق سفلي + حدّ داخلي + ظل للرمز.\n"
            "     للاستخدام خارج بلاطة القائمة (بطاقات/مشاركة/شاشات تعريفية). مولَّدة بـ tools/gen_ai_lib_icons.py. -->\n"
            '<vector xmlns:android="http://schemas.android.com/apk/res/android"\n'
            '    xmlns:aapt="http://schemas.android.com/aapt"\n'
            '    android:width="48dp" android:height="48dp"\n'
            '    android:viewportWidth="48" android:viewportHeight="48">\n\n'
            f"{bg}\n{gloss}\n{depth}\n{edge}\n\n"
            '    <group android:translateX="8.9" android:translateY="8.9" android:scaleX="1.26" android:scaleY="1.26">\n'
            '        <group android:translateY="0.7">\n' + sh + "\n        </group>\n"
            + fg + "\n    </group>\n\n</vector>\n")

def tile_xml(color, light, dark):
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            f"<!-- بلاطة أيقونة {color} لمكتبات الذكاء الاصطناعي (RinDF/RinML/RinAI) — نفس شكل bg_icon_gradient_green\n"
            "     (زاوية 135، نصف قطر 13dp). مولَّدة بـ tools/gen_ai_lib_icons.py. -->\n"
            '<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">\n'
            f'    <gradient android:type="linear" android:angle="135" android:startColor="{light}" android:endColor="{dark}" />\n'
            '    <corners android:radius="13dp" />\n</shape>\n')

# ----------------------------------------------------------------------------- SVG
def svg_el(p, color, alpha_mul=1.0):
    a = p["a"] * alpha_mul
    d = path_d(p)
    if p["st"] == "fill":
        return f'<path d="{d}" fill="{color}" fill-opacity="{f(a)}"/>'
    return (f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{f(p["sw"])}" stroke-linecap="round" '
            f'stroke-linejoin="round" stroke-opacity="{f(a)}"/>')

def badge_svg(name, spec):
    g = spec["glyph"]()
    sh = "".join(svg_el(p, "#000", 0.22) for p in g)
    fg = "".join(svg_el(p, AMBER if p["role"] == "accent" else "#fff") for p in g)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="96" height="96" role="img" aria-label="{name}">\n'
            '<defs>'
            f'<linearGradient id="bg" gradientUnits="userSpaceOnUse" x1="40" y1="44" x2="8" y2="4"><stop offset="0" stop-color="{spec["dark"]}"/><stop offset="1" stop-color="{spec["light"]}"/></linearGradient>'
            '<linearGradient id="gl" gradientUnits="userSpaceOnUse" x1="24" y1="2" x2="24" y2="28"><stop offset="0" stop-color="#fff" stop-opacity=".24"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient>'
            '<linearGradient id="dp" gradientUnits="userSpaceOnUse" x1="24" y1="22" x2="24" y2="46"><stop offset="0" stop-color="#000" stop-opacity="0"/><stop offset="1" stop-color="#000" stop-opacity=".2"/></linearGradient>'
            '</defs>\n'
            f'<path d="{rrect_d(2, 2, 44, 44, 12)}" fill="url(#bg)"/>\n'
            f'<path d="{rrect_d(2, 2, 44, 26, 12)}" fill="url(#gl)"/>\n'
            f'<path d="{rrect_d(2, 22, 44, 24, 12)}" fill="url(#dp)"/>\n'
            f'<path d="{rrect_d(2.5, 2.5, 43, 43, 11.5)}" fill="none" stroke="#fff" stroke-opacity=".2"/>\n'
            f'<g transform="translate(8.9 8.9) scale(1.26)"><g transform="translate(0 .7)">{sh}</g>{fg}</g>\n</svg>\n')

# ----------------------------------------------------------------------------- مُصيِّر المعاينة (PIL)
SS = 6

def hexa(h, a=1.0):
    h = h.lstrip("#"); return tuple(int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)) + (a,)

def lin_fill(W, H, x0, y0, x1, y1, stops):
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    dx, dy = x1 - x0, y1 - y0
    t = np.clip(((xs - x0) * dx + (ys - y0) * dy) / (dx * dx + dy * dy + 1e-9), 0, 1)
    offs = [s[0] for s in stops]
    out = np.zeros((H, W, 4), np.float32)
    for ch in range(4):
        out[..., ch] = np.interp(t, offs, [s[1][ch] for s in stops])
    return out

def paint(canvas, mask, fill):
    m = np.asarray(mask, np.float32) / 255.0
    if fill[0] == "solid":
        r, g, b, a = fill[1]
        col = np.zeros(canvas.shape, np.float32); col[..., 0], col[..., 1], col[..., 2] = r, g, b
        al = m * a
    else:
        col = fill[1]; al = m * col[..., 3]
    inv = 1 - al
    ca = canvas[..., 3]
    out_a = al + ca * inv
    for ch in range(3):
        canvas[..., ch] = np.where(out_a > 0, (col[..., ch] * al + canvas[..., ch] * ca * inv) / np.maximum(out_a, 1e-9), 0)
    canvas[..., 3] = out_a

def prim_mask(p, W, H, tf):
    sc, ox, oy = tf
    im = Image.new("L", (W, H), 0); d = ImageDraw.Draw(im)
    X = lambda x: x * sc + ox
    Y = lambda y: y * sc + oy
    k = p["k"]
    if k == "rrect":
        box = (X(p["x"]), Y(p["y"]), X(p["x"] + p["w"]), Y(p["y"] + p["h"]))
        if p["st"] == "fill": d.rounded_rectangle(box, p["r"] * sc, fill=255)
        else: d.rounded_rectangle(box, p["r"] * sc, outline=255, width=max(1, int(p["sw"] * sc)))
    elif k in ("circle", "ring"):
        box = (X(p["cx"] - p["r"]), Y(p["cy"] - p["r"]), X(p["cx"] + p["r"]), Y(p["cy"] + p["r"]))
        if k == "circle": d.ellipse(box, fill=255)
        else: d.ellipse(box, outline=255, width=max(1, int(p["sw"] * sc)))
    elif k == "poly":
        d.polygon([(X(x), Y(y)) for x, y in p["pts"]], fill=255)
    elif k == "line":
        pts = [(X(x), Y(y)) for x, y in p["pts"]]; w = p["sw"] * sc
        d.line(pts, fill=255, width=int(w))
        for x, y in pts: d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=255)
    return im

def render_glyph(canvas, spec, tf, accent_color, shadow):
    H, W = canvas.shape[:2]
    prims = spec["glyph"]()
    sc, ox, oy = tf
    if shadow:
        for p in prims:
            paint(canvas, prim_mask(p, W, H, (sc, ox, oy + 0.7 * sc)), ("solid", (0, 0, 0, 0.22 * p["a"])))
    for p in prims:
        col = hexa(accent_color if p["role"] == "accent" else "#FFFFFF")
        paint(canvas, prim_mask(p, W, H, tf), ("solid", (col[0], col[1], col[2], p["a"])))

def render_badge(spec, px):
    W = px * SS; s = W / 48
    canvas = np.zeros((W, W, 4), np.float32)
    tf0 = (s, 0, 0)
    bgm = prim_mask(rrect(2, 2, 44, 44, 12), W, W, tf0)
    lo, hi = hexa(spec["dark"]), hexa(spec["light"])
    paint(canvas, bgm, ("arr", lin_fill(W, W, 40 * s, 44 * s, 8 * s, 4 * s, [(0, lo), (1, hi)])))
    paint(canvas, prim_mask(rrect(2, 2, 44, 26, 12), W, W, tf0),
          ("arr", lin_fill(W, W, 24 * s, 2 * s, 24 * s, 28 * s, [(0, (1, 1, 1, .24)), (1, (1, 1, 1, 0))])))
    paint(canvas, prim_mask(rrect(2, 22, 44, 24, 12), W, W, tf0),
          ("arr", lin_fill(W, W, 24 * s, 22 * s, 24 * s, 46 * s, [(0, (0, 0, 0, 0)), (1, (0, 0, 0, .2))])))
    paint(canvas, prim_mask(frame(2.5, 2.5, 43, 43, 11.5, 1), W, W, tf0), ("solid", (1, 1, 1, .2)))
    render_glyph(canvas, spec, (1.26 * s, 8.9 * s, 8.9 * s), AMBER, True)
    return canvas

def render_tile(spec, px):
    W = px * SS
    canvas = np.zeros((W, W, 4), np.float32)
    m = Image.new("L", (W, W), 0); ImageDraw.Draw(m).rounded_rectangle((0, 0, W - 1, W - 1), 13 / 38 * W, fill=255)
    lo, hi = hexa(spec["dark"]), hexa(spec["light"])
    # Android angle=135 → BR_TL: البداية (الفاتح) أسفل-يمين والنهاية (الداكن) أعلى-يسار — كما في البلاطة الخضراء
    paint(canvas, m, ("arr", lin_fill(W, W, W, W, 0, 0, [(0, hi), (1, lo)])))
    gs = (20 / 38 * W) / 24
    off = (W - 20 / 38 * W) / 2
    render_glyph(canvas, spec, (gs, off, off), "#FFFFFF", False)
    return canvas

def to_img(canvas, px, bg=None):
    out = (np.clip(canvas, 0, 1) * 255).astype(np.uint8)
    im = Image.fromarray(out, "RGBA").resize((px, px), Image.LANCZOS)
    if bg:
        base = Image.new("RGBA", (px, px), bg); base.alpha_composite(im); return base
    return im

def preview():
    names = list(LIBS)
    cell, gap = 200, 24
    sheet = Image.new("RGBA", (3 * cell + 4 * gap, 3 * cell + 4 * gap), (24, 26, 32, 255))
    for c, n in enumerate(names):
        x = gap + c * (cell + gap)
        sheet.alpha_composite(to_img(render_badge(LIBS[n], cell), cell), (x, gap))
        sheet.alpha_composite(to_img(render_tile(LIBS[n], cell), cell), (x, 2 * gap + cell))
        sheet.alpha_composite(to_img(render_badge(LIBS[n], cell), cell, (246, 247, 250, 255)), (x, 3 * gap + 2 * cell))
    return sheet

# ----------------------------------------------------------------------------- كتابة الملفات
def main():
    ICONS_DIR.mkdir(parents=True, exist_ok=True)
    for old in list(ICONS_DIR.glob("*_dark.svg")) + list(ICONS_DIR.glob("*_glyph.svg")): old.unlink()
    for n, spec in LIBS.items():
        (RES / f"ic_lib_{n}.xml").write_text(glyph_xml(n, spec), encoding="utf-8")
        (RES / f"ic_lib_{n}_badge.xml").write_text(badge_xml(n, spec), encoding="utf-8")
        (RES / f"bg_icon_gradient_{spec['color']}.xml").write_text(tile_xml(spec["color"], spec["light"], spec["dark"]), encoding="utf-8")
        (ICONS_DIR / f"{n}.svg").write_text(badge_svg(n, spec), encoding="utf-8")
    preview().convert("RGB").save(ICONS_DIR / "preview.png")
    print("icons:", list(LIBS))

if __name__ == "__main__":
    main()

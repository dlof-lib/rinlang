#!/usr/bin/env python3
"""يولّد أيقونتَي channel47 (تطبيق + ملف .ch47) بصيغ SVG/PNG/ICO من هندسة واحدة.
الاستعمال: gen_channel47_icon.py <out_dir>

الشعار: قوسان متحدا المركز (قناتان) يفتحان نحو اليمين، نواة عنبرية في المركز، وثلاثة خطوط بيانات تخرج من الفتحة."""
import math, os, sys
import numpy as np
from PIL import Image, ImageDraw

NAVY, CYAN, AMBER = (0x0A, 0x2A, 0x5E), (0x1F, 0xC8, 0xE0), (0xFF, 0xC8, 0x57)
CX, CY = 21.0, 24.0
ARCS = [(14.0, 4.0), (7.6, 3.6)]            # (نصف القطر، السُّمك)
GAP = 42                                      # نصف زاوية الفتحة بالدرجات
OUT = [(18.0, 36.0, 42.0), (24.0, 34.0, 43.5), (30.0, 36.0, 42.0)]   # (y، x0، x1) خطوط البيانات
OUT_W, CORE_R = 2.6, 3.0


def pt(r, deg):
    a = math.radians(deg)
    return CX + r * math.cos(a), CY + r * math.sin(a)


def mark_svg(color="#fff", core="#FFC857", op=1.0):
    parts = []
    for r, w in ARCS:
        x1, y1 = pt(r, GAP); x2, y2 = pt(r, -GAP)
        parts.append(f'<path d="M{x1:.2f},{y1:.2f} A{r},{r} 0 1 1 {x2:.2f},{y2:.2f}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linecap="round"/>')
    for k, (y, x0, x1) in enumerate(OUT):
        parts.append(f'<path d="M{x0},{y} H{x1}" stroke="{color}" stroke-opacity="{[.55, 1, .55][k]}" stroke-width="{OUT_W}" stroke-linecap="round"/>')
    parts.append(f'<circle cx="{CX}" cy="{CY}" r="{CORE_R}" fill="{core}"/>')
    return "".join(parts)


DEFS = ('<defs><linearGradient id="bg" gradientUnits="userSpaceOnUse" x1="40" y1="44" x2="8" y2="4">'
        f'<stop offset="0" stop-color="#{CYAN[0]:02X}{CYAN[1]:02X}{CYAN[2]:02X}"/><stop offset="1" stop-color="#{NAVY[0]:02X}{NAVY[1]:02X}{NAVY[2]:02X}"/></linearGradient>'
        '<linearGradient id="gl" gradientUnits="userSpaceOnUse" x1="24" y1="2" x2="24" y2="28">'
        '<stop offset="0" stop-color="#fff" stop-opacity=".22"/><stop offset="1" stop-color="#fff" stop-opacity="0"/></linearGradient></defs>')
SQ = "M14,2 H34 A12,12 0 0,1 46,14 V34 A12,12 0 0,1 34,46 H14 A12,12 0 0,1 2,34 V14 A12,12 0 0,1 14,2 Z"
GLOSS = "M14,2 H34 A12,12 0 0,1 46,14 V16 A12,12 0 0,1 34,28 H14 A12,12 0 0,1 2,16 V14 A12,12 0 0,1 14,2 Z"
RIM = "M14,2.5 H34 A11.5,11.5 0 0,1 45.5,14 V34 A11.5,11.5 0 0,1 34,45.5 H14 A11.5,11.5 0 0,1 2.5,34 V14 A11.5,11.5 0 0,1 14,2.5 Z"


def app_svg():
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="96" height="96" role="img" aria-label="channel47">'
            f'{DEFS}<path d="{SQ}" fill="url(#bg)"/><path d="{GLOSS}" fill="url(#gl)"/><path d="{RIM}" fill="none" stroke="#fff" stroke-opacity=".2"/>'
            f'<g>{mark_svg()}</g></svg>\n')


# أيقونة الملف: ورقة بطيّة علوية، الشعار، وشارة «47» بخطوط مرسومة (بلا خطوط نصية)
PAGE = "M10,3 H29 L40,14 V43 A2,2 0 0,1 38,45 H10 A2,2 0 0,1 8,43 V5 A2,2 0 0,1 10,3 Z"
FOLD = "M29,3 V12 A2,2 0 0,0 31,14 H40 Z"
DIG = [[(15.5, 36.2), (12.2, 41.6), (18.2, 41.6)], [(16.4, 36.2), (16.4, 43)], [(20.4, 36.2), (24.4, 36.2), (21.6, 43)]]


def file_svg():
    d = "".join(f'<path d="M{" L".join(f"{x},{y}" for x, y in s)}" fill="none" stroke="#fff" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>' for s in DIG)
    sc = 0.5
    m = f'<g transform="translate(12.6 8.2) scale({sc})">{mark_svg()}</g>'
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48" width="96" height="96" role="img" aria-label="channel47 file">'
            f'{DEFS}<path d="{PAGE}" fill="url(#bg)"/><path d="{FOLD}" fill="#fff" fill-opacity=".28"/>'
            f'<path d="M10,3.5 H29 L39.5,14 V43 A1.5,1.5 0 0,1 38,44.5 H10 A1.5,1.5 0 0,1 8.5,43 V5 A1.5,1.5 0 0,1 10,3.5 Z" fill="none" stroke="#fff" stroke-opacity=".22"/>'
            f'{m}<rect x="12" y="33.6" width="15.5" height="11" rx="2.4" fill="#000" fill-opacity=".28"/>{d}</svg>\n')


# ───────── التصيير النقطي ─────────
def _bg(S, k):
    y, x = np.mgrid[0:S, 0:S]
    X, Y = x / k, y / k
    vx, vy = -32.0, -40.0
    t = np.clip(((X - 40) * vx + (Y - 44) * vy) / (vx * vx + vy * vy), 0, 1)[..., None]
    rgb = np.array(CYAN, float) * (1 - t) + np.array(NAVY, float) * t
    gl = (np.clip(1 - (Y - 2) / 26, 0, 1) * 0.22 * (Y < 28))[..., None]
    return Image.fromarray((rgb * (1 - gl) + 255 * gl).astype("uint8"), "RGB").convert("RGBA")


def _mark(d, k, ox=0.0, oy=0.0, sc=1.0):
    f = lambda x, y: ((ox + x * sc) * k, (oy + y * sc) * k)
    W = (255, 255, 255, 255)
    for r, w in ARCS:
        cx, cy = f(CX, CY); rr = r * sc * k; ww = w * sc * k
        ro = rr + ww / 2
        d.arc([cx - ro, cy - ro, cx + ro, cy + ro], start=GAP, end=360 - GAP, fill=W, width=int(round(ww)))
        for ang in (GAP, -GAP):
            px, py = f(*pt(r, ang)); d.ellipse([px - ww / 2, py - ww / 2, px + ww / 2, py + ww / 2], fill=W)
    for n, (y, x0, x1) in enumerate(OUT):
        a = int(255 * [.55, 1, .55][n]); w = OUT_W * sc * k
        p0, p1 = f(x0, y), f(x1, y)
        d.line([p0, p1], fill=(255, 255, 255, a), width=int(round(w)))
        for px, py in (p0, p1): d.ellipse([px - w / 2, py - w / 2, px + w / 2, py + w / 2], fill=(255, 255, 255, a))
    cx, cy = f(CX, CY); r = CORE_R * sc * k
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=AMBER + (255,))


def render_app(size=512, ss=4):
    S = size * ss; k = S / 48
    img = _bg(S, k)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([2 * k, 2 * k, 46 * k, 46 * k], radius=12 * k, fill=255)
    over = Image.new("RGBA", (S, S), (0, 0, 0, 0)); _mark(ImageDraw.Draw(over), k)
    img = Image.alpha_composite(img, over); img.putalpha(mask)
    return img.resize((size, size), Image.LANCZOS)


def render_file(size=512, ss=4):
    S = size * ss; k = S / 48
    img = _bg(S, k)
    mask = Image.new("L", (S, S), 0)
    md = ImageDraw.Draw(mask)
    pts = [(10, 3), (29, 3), (40, 14), (40, 43), (38, 45), (10, 45), (8, 43), (8, 5)]
    md.polygon([(x * k, y * k) for x, y in pts], fill=255)
    for cx, cy in ((10, 5), (38, 43), (10, 43)): md.ellipse([(cx - 2) * k, (cy - 2) * k, (cx + 2) * k, (cy + 2) * k], fill=255)
    over = Image.new("RGBA", (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(over)
    d.polygon([(29 * k, 3 * k), (29 * k, 12 * k), (31 * k, 14 * k), (40 * k, 14 * k)], fill=(255, 255, 255, 72))
    _mark(d, k, 12.6, 8.2, 0.5)
    d.rounded_rectangle([12 * k, 33.6 * k, 27.5 * k, 44.6 * k], radius=2.4 * k, fill=(0, 0, 0, 72))
    for s in DIG:
        pts2 = [(x * k, y * k) for x, y in s]; w = 1.7 * k
        d.line(pts2, fill=(255, 255, 255, 255), width=int(round(w)), joint="curve")
        for px, py in pts2: d.ellipse([px - w / 2, py - w / 2, px + w / 2, py + w / 2], fill=(255, 255, 255, 255))
    img = Image.alpha_composite(img, over); img.putalpha(mask)
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, svg, fn in (("channel47", app_svg(), render_app), ("channel47-file", file_svg(), render_file)):
        open(os.path.join(out, f"{name}.svg"), "w", encoding="utf-8").write(svg)
        big = fn(512)
        for s in (16, 32, 48, 64, 128, 256, 512):
            (big if s == 512 else big.resize((s, s), Image.LANCZOS)).save(os.path.join(out, f"{name}-{s}.png"))
        big.save(os.path.join(out, f"{name}.ico"), sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("ok", out)

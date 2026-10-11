"""raster.py — مُرسِّم مسارات بسيط (PIL) للمعاينة والاختبار: يدعم M L H V C A Z المطلقة."""
import math, re
from PIL import Image, ImageDraw

TOK = re.compile(r"([MLHVCAZ])|(-?\d*\.?\d+)")

def parse(d):
    toks = [(m.group(1) or float(m.group(2))) for m in TOK.finditer(d)]
    i, subs, cur, cmd, start = 0, [], None, None, None
    def num():
        nonlocal i
        v = toks[i]; i += 1; return v
    x = y = 0.0
    while i < len(toks):
        t = toks[i]
        if isinstance(t, str): cmd = t; i += 1
        if cmd == "M":
            x, y = num(), num(); cur = [(x, y)]; subs.append((cur, False)); start = (x, y); cmd = "L"
        elif cmd == "L":
            x, y = num(), num(); cur.append((x, y))
        elif cmd == "H":
            x = num(); cur.append((x, y))
        elif cmd == "V":
            y = num(); cur.append((x, y))
        elif cmd == "C":
            x1, y1, x2, y2, x3, y3 = [num() for _ in range(6)]
            for k in range(1, 17):
                t_ = k / 16; u = 1 - t_
                cur.append((u**3*x + 3*u*u*t_*x1 + 3*u*t_*t_*x2 + t_**3*x3, u**3*y + 3*u*u*t_*y1 + 3*u*t_*t_*y2 + t_**3*y3))
            x, y = x3, y3
        elif cmd == "A":
            rx, ry, rot, la, sw, ex, ey = [num() for _ in range(7)]
            cur.extend(arc(x, y, rx, ry, int(la), int(sw), ex, ey)); x, y = ex, ey
        elif cmd == "Z":
            cur.append(start); subs[-1] = (cur, True); x, y = start; cmd = None
    return subs

def arc(x1, y1, rx, ry, la, sw, x2, y2):
    if x1 == x2 and y1 == y2: return []
    rx = abs(rx); dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    lam = (dx*dx + dy*dy) / (rx*rx)
    if lam > 1: rx *= math.sqrt(lam)
    sq = max(0, (rx*rx*rx*rx - rx*rx*(dy*dy) - rx*rx*(dx*dx)) / (rx*rx*(dy*dy) + rx*rx*(dx*dx)))
    co = (-1 if la == sw else 1) * math.sqrt(sq)
    cxp, cyp = co * dy, -co * dx
    cx, cy = cxp + (x1 + x2) / 2, cyp + (y1 + y2) / 2
    a1 = math.atan2(y1 - cy, x1 - cx); a2 = math.atan2(y2 - cy, x2 - cx)
    da = a2 - a1
    if sw and da < 0: da += 2 * math.pi
    if not sw and da > 0: da -= 2 * math.pi
    n = max(8, int(abs(da) * 12))
    return [(cx + rx * math.cos(a1 + da * k / n), cy + rx * math.sin(a1 + da * k / n)) for k in range(1, n + 1)]

def render(d, size=96, stroke=2.0, fg=(20, 20, 30), bg=(255, 255, 255), ss=4, fill=False):
    S = size * ss; k = S / 24.0
    im = Image.new("RGB", (S, S), bg); dr = ImageDraw.Draw(im)
    w = max(1, int(round(stroke * k)))
    for pts, closed in parse(d):
        P = [(px * k, py * k) for px, py in pts]
        if fill and closed and len(P) > 2: dr.polygon(P, fill=fg)
        if len(P) == 1: P = P * 2
        dr.line(P, fill=fg, width=w, joint="curve")
        r = w / 2
        for px, py in (P[0], P[-1]) if not closed else P[::max(1, len(P) // 40)]:
            dr.ellipse([px - r, py - r, px + r, py + r], fill=fg)
        if not closed:
            pass
        else:
            for px, py in P:
                pass
    return im.resize((size, size), Image.LANCZOS)

def sheet(icons, path, cols=12, cell=84, size=44, label=True):
    from PIL import ImageFont
    rows = (len(icons) + cols - 1) // cols
    im = Image.new("RGB", (cols * cell, rows * cell), (255, 255, 255)); dr = ImageDraw.Draw(im)
    try: fnt = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 9)
    except Exception: fnt = ImageFont.load_default()
    for n, (name, d) in enumerate(icons):
        cx, cy = (n % cols) * cell, (n // cols) * cell
        im.paste(render(d, size), (cx + (cell - size) // 2, cy + 8))
        if label: dr.text((cx + 3, cy + size + 14), name[:15], fill=(90, 90, 100), font=fnt)
    im.save(path)

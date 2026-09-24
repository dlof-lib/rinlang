#!/usr/bin/env python3
"""يولّد أيقونات الموقع كلها من web/icon-192.png (شعار Rin): favicon.ico + 16/32/48 + apple-touch + 512 + maskable.
تُشغَّل مرة واحدة ويُلتزَم بالناتج داخل web/ (لا تحتاج CI).   python3 scripts/build_icons.py"""
from pathlib import Path
from PIL import Image, ImageDraw

WEB = Path(__file__).resolve().parent.parent / "web"
BG = (17, 70, 44)
src = Image.open(WEB / "icon-192.png").convert("L")
# الشعار أبيض على أخضر داكن: نستخرج قناعاً ونكبّره بنعومة ثم نقطعه => حواف حادة عند أي حجم
big_mask = src.resize((1536, 1536), Image.BICUBIC).point(lambda v: 255 if v > 150 else 0).resize((1024, 1024), Image.LANCZOS)


def compose(size, radius_ratio=0.0, glyph_scale=1.0, bg=BG):
    canvas = Image.new("RGB", (size, size), bg)
    g = big_mask.resize((int(size * glyph_scale), int(size * glyph_scale)), Image.LANCZOS)
    off = (size - g.size[0]) // 2
    canvas.paste((255, 255, 255), (off, off), g)
    if radius_ratio:
        m = Image.new("L", (size * 4, size * 4), 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, size * 4 - 1, size * 4 - 1), int(size * 4 * radius_ratio), fill=255)
        out = canvas.convert("RGBA")
        out.putalpha(m.resize((size, size), Image.LANCZOS))
        return out
    return canvas


# أيقونات المتصفح: أركان دائرية، والشعار أكبر قليلاً ليبقى مقروءاً عند 16px
for name, size in (("favicon-16.png", 16), ("favicon-32.png", 32), ("favicon.png", 48)):
    compose(size, 0.22, 1.12).save(WEB / name, optimize=True)
ico = [compose(s, 0.22, 1.12) for s in (16, 32, 48)]
ico[2].save(WEB / "favicon.ico", sizes=[(16, 16), (32, 32), (48, 48)], append_images=ico[:2])
compose(180, 0, 1.0).convert("RGB").save(WEB / "apple-touch-icon.png", optimize=True)      # iOS يقصّ الأركان بنفسه
compose(512, 0.2, 1.0).save(WEB / "icon-512.png", optimize=True)
compose(512, 0, 0.72).convert("RGB").save(WEB / "icon-maskable-512.png", optimize=True)     # منطقة أمان 80%
print("icons written to", WEB)

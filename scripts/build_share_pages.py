#!/usr/bin/env python3
"""يولّد صفحات «معاينة الروابط» الثابتة لكل مكتبة / إضافة / حساب في Rin.

المشكلة: الموقع تطبيق صفحة واحدة (SPA) على GitHub Pages، وزواحف واتساب/تيليجرام/X/
ديسكورد/فيسبوك/Slack لا تشغّل JavaScript ولا تقبل استجابة 404، فيظهر الرابط
`https://dlof-lib.github.io/rinlang/@rin/math.og.rin` بلا بطاقة معاينة.

الحل: عند كل نشر نقرأ Firebase (قراءة عامة عبر REST، بلا مفاتيح) ونكتب لكل كيان:

  dist/@user/name.og.rin/index.html   صفحة ثابتة: title/description/OG/Twitter/JSON-LD/canonical
  dist/og/<hash>.png                  صورة معاينة 1200x630 مخصّصة (اسم، ناشر، وصف، إصدار، أرقام)
  dist/@user/index.html               صفحة الحساب (تجميع منشورات الناشر)
  dist/sitemap.xml + dist/robots.txt  ليجد محرّكات البحث كل شيء

الصفحة الثابتة تحوّل الزائر البشري (JS) فوراً إلى ?@user/name.og.rin ثم يعيد libraries.js كتابة
العنوان إلى المسار الجميل؛ فالتطبيق الوحيد يبقى index.html.

الاستخدام:
  python3 scripts/build_share_pages.py --out dist                    # من Firebase مباشرة
  python3 scripts/build_share_pages.py --out dist --data fx.json     # بلا شبكة (اختبار)
  --strict : يفشل عند تعذّر Firebase (الافتراضي: تحذير ومتابعة النشر بلا صفحات المشاركة)

المتطلبات: pillow. للعربية الصحيحة: libraqm (apt: libfribidi0) أو arabic-reshaper + python-bidi.
"""
import argparse
import base64
import concurrent.futures as cf
import hashlib
import html
import io
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont, features

ROOT = Path(__file__).resolve().parent.parent
SITE = "https://dlof-lib.github.io/rinlang/"
DB_URL = "https://dlof-massage-default-rtdb.firebaseio.com"
LIB_SUFFIX, EXT_SUFFIX = ".og.rin", ".rinex"
W, H = 1200, 630

# ----------------------------------------------------------------------------- نصوص
L10N = {
    "ar": {
        "official": "مكتبة رسمية", "community": "مكتبة مجتمع", "extension": "إضافة", "profile": "حساب",
        "store": "مكتبات Rin", "open": "افتح في متجر Rin", "by": "بواسطة", "lang_name": "لغة Rin",
        "def_lib": "مكتبة {n} للغة Rin — انسخ الكود أو حمّلها أو شاركها برابط مباشر.",
        "def_ext": "إضافة {n} لبيئة Rin — تُثبَّت من تطبيق Rin.",
        "def_prof": "حساب @{n} على Rin — {c}",
        "pkgs": "{n} مكتبة", "pkg1": "مكتبة واحدة", "exts": "{n} إضافة", "ext1": "إضافة واحدة", "noscript": "هذه الصفحة تعمل بأفضل شكل مع JavaScript.",
        "tag": "لغة برمجة صغيرة، حقيقية بالكامل",
    },
    "en": {
        "official": "Official library", "community": "Community library", "extension": "Extension", "profile": "Profile",
        "store": "Rin Libraries", "open": "Open in Rin store", "by": "by", "lang_name": "Rin language",
        "def_lib": "{n}, a library for the Rin language — copy the code, download it, or share a direct link.",
        "def_ext": "{n}, an extension for the Rin environment — install it from the Rin app.",
        "def_prof": "@{n} on Rin — {c}",
        "pkgs": "{n} libraries", "pkg1": "1 library", "exts": "{n} extensions", "ext1": "1 extension", "noscript": "This page works best with JavaScript.",
        "tag": "A small, fully real programming language",
    },
}
BADGE = {"store": "official", "official": "official", "community": "community", "library": "community", "extension": "extension", "profile": "profile"}


# ----------------------------------------------------------------------------- أدوات نقية
def num(v):
    try:
        v = float(v)
    except (TypeError, ValueError):
        return 0
    return int(v) if v == v and v > 0 and v != float("inf") else 0


def fmt_count(n):
    n = num(n)
    if n >= 1_000_000:
        return re.sub(r"\.0$", "", f"{n / 1e6:.1f}") + "M"
    if n >= 1000:
        return re.sub(r"\.0$", "", f"{n / 1e3:.1f}") + "K"
    return str(n)


def fmt_size(b):
    b = num(b)
    return f"{b / 1048576:.1f} MB" if b >= 1048576 else f"{b / 1024:.1f} KB" if b >= 1024 else f"{b} B"


def slug(s):
    return re.sub(r"\s+", "-", str(s or "").strip())


def lib_key(s):
    return re.sub(r"\s+", "-", re.sub(r"\.(og\.rin(sdk)?|rinex)$", "", str(s or ""), flags=re.I)).lower()


def enc(s):  # مطابق لـ encodeURIComponent
    return urllib.parse.quote(s, safe="-_.!~*'()")


def clean_text(s, limit):
    s = re.sub(r"\s+", " ", re.sub(r"[\u0000-\u001f\u007f]", " ", str(s or ""))).strip()
    return s if len(s) <= limit else s[: limit - 1].rstrip() + "…"


def has_rtl(s):
    return bool(re.search(r"[\u0590-\u08ff\ufb1d-\ufdff\ufe70-\ufeff]", s or ""))


def lang_of(*texts):
    t = " ".join(x for x in texts if x)
    letters = re.findall(r"[^\W\d_]", t)
    if not letters:
        return "en"
    return "ar" if sum(1 for c in letters if has_rtl(c)) * 3 >= len(letters) else "en"


def safe_segment(s):
    """يرفض ما لا يصلح اسم مجلد على GitHub Pages/أنظمة الملفات."""
    return bool(s) and s not in (".", "..") and len(s) <= 120 and not re.search(r'[\u0000-\u001f/\\<>:"|?*%#]', s)


def iso(ms):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ms / 1000)) if ms else ""


# ----------------------------------------------------------------------------- جلب Firebase (REST عامة)
LIB_FIELDS = ["name", "fileName", "version", "description", "sizeBytes", "updatedAt", "downloadCount", "likeCount"]
PKG_FIELDS = ["name", "version", "description", "category", "publisherUsername", "publisherName", "sizeBytes", "createdAt",
              "downloadCount", "likeCount", "ratingSum", "ratingCount", "license", "iconBase64"]
EXT_FIELDS = ["name", "version", "description", "type", "developer", "developerUsername", "sizeBytes", "releaseDate",
              "downloadCount", "likeCount", "ratingSum", "ratingCount"]


def http_json(url, tries=3):
    err = None
    for i in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "rin-share-pages/1"}), timeout=30) as r:
                return json.loads(r.read().decode("utf-8"))
        except (urllib.error.URLError, TimeoutError, ValueError) as e:
            err = e
            time.sleep(1.5 * (i + 1))
    raise RuntimeError(f"{url}: {err}")


def fetch_node(db, node, fields, workers=16):
    """يجلب العقدة كحقول صغيرة فقط (يتجنّب base64Data/content الضخمة)."""
    keys = list((http_json(f"{db}/{node}.json?shallow=true") or {}).keys())
    out = {k: {} for k in keys}

    def one(kf):
        k, f = kf
        return k, f, http_json(f"{db}/{node}/{urllib.parse.quote(k, safe='')}/{f}.json")

    with cf.ThreadPoolExecutor(workers) as ex:
        for k, f, v in ex.map(one, [(k, f) for k in keys for f in fields]):
            if v is not None:
                out[k][f] = v
    return out


def fetch_all(db):
    return {"libraries": fetch_node(db, "libraries", LIB_FIELDS),
            "packages": fetch_node(db, "packages", PKG_FIELDS),
            "extensions": fetch_node(db, "extensions", EXT_FIELDS)}


# ----------------------------------------------------------------------------- تطبيع الكيانات
def decode_icon(b64):
    if not isinstance(b64, str) or len(b64) < 20 or len(b64) > 600000 or not re.fullmatch(r"[A-Za-z0-9+/=\s]+", b64):
        return None
    try:
        im = Image.open(io.BytesIO(base64.b64decode(re.sub(r"\s+", "", b64))))
        im.load()
        return im.convert("RGBA")
    except Exception:
        return None


def build_entities(data):
    ents = {}  # path(exact) -> entity

    def add(e):
        if not (safe_segment(e["user"]) and safe_segment(e["slug"] or "x")):
            print(f"skip (unsafe name): {e['user']!r}/{e['slug']!r}", file=sys.stderr)
            return
        prev = ents.get(e["path"])
        if prev is None or e["ts"] >= prev["ts"]:
            ents[e["path"]] = e

    for key, v in (data.get("libraries") or {}).items():
        name = str(v.get("name") or key)
        add(dict(kind="official", user="rin", name=name, slug=slug(name) + LIB_SUFFIX, path=f"@rin/{slug(name)}{LIB_SUFFIX}",
                 desc=v.get("description", ""), version=str(v.get("version") or "1.0.0"), size=num(v.get("sizeBytes")),
                 downloads=num(v.get("downloadCount")), likes=num(v.get("likeCount")), ts=num(v.get("updatedAt")),
                 category="Official", icon=None, rating=0))
    for pid, v in (data.get("packages") or {}).items():
        user = str(v.get("publisherUsername") or v.get("publisherName") or "").strip()
        name = str(v.get("name") or "").strip()
        if not user or not name:
            continue
        rc = num(v.get("ratingCount"))
        add(dict(kind="community", user=slug(user), name=name, slug=slug(name) + LIB_SUFFIX, path=f"@{slug(user)}/{slug(name)}{LIB_SUFFIX}",
                 desc=v.get("description", ""), version=str(v.get("version") or "1.0.0"), size=num(v.get("sizeBytes")),
                 downloads=num(v.get("downloadCount")), likes=num(v.get("likeCount")), ts=num(v.get("createdAt")),
                 category=str(v.get("category") or ""), icon=decode_icon(v.get("iconBase64")),
                 rating=(num(v.get("ratingSum")) / rc) if rc else 0))
    for eid, v in (data.get("extensions") or {}).items():
        user = str(v.get("developerUsername") or v.get("developer") or "").strip()
        name = str(v.get("name") or "").strip()
        if not user or not name:
            continue
        rc = num(v.get("ratingCount"))
        add(dict(kind="extension", user=slug(user), name=name, slug=slug(name) + EXT_SUFFIX, path=f"@{slug(user)}/{slug(name)}{EXT_SUFFIX}",
                 desc=v.get("description", ""), version=str(v.get("version") or "1.0.0"), size=num(v.get("sizeBytes")),
                 downloads=num(v.get("downloadCount")), likes=num(v.get("likeCount")), ts=num(v.get("releaseDate")),
                 category=str(v.get("type") or "extension"), icon=None,
                 rating=(num(v.get("ratingSum")) / rc) if rc else 0))

    # حسابات = تجميع كل ما ينشره الناشر
    profiles = {}
    for e in ents.values():
        p = profiles.setdefault(e["user"].lower(), dict(kind="profile", user=e["user"], name="@" + e["user"], slug="", path="@" + e["user"],
                                                       items=[], downloads=0, likes=0, ts=0, icon=None, version="", size=0, rating=0, category="", desc=""))
        p["items"].append(e)
        p["downloads"] += e["downloads"]
        p["likes"] += e["likes"]
        p["ts"] = max(p["ts"], e["ts"])
    for p in profiles.values():
        p["items"].sort(key=lambda i: (-i["downloads"], -i["likes"], i["name"].lower()))
        nl = sum(1 for i in p["items"] if i["kind"] != "extension")
        ne = len(p["items"]) - nl
        p["counts"] = (nl, ne)
    return list(ents.values()), list(profiles.values())


# ----------------------------------------------------------------------------- الخطوط والرسم
FONT_DIRS = ["/usr/share/fonts/truetype/dejavu/", "/usr/share/fonts/dejavu/", "/Library/Fonts/", "C:/Windows/Fonts/"]
HAS_RAQM = features.check("raqm")
try:  # احتياط عند غياب libraqm
    import arabic_reshaper
    from bidi.algorithm import get_display
except Exception:  # pragma: no cover
    arabic_reshaper = get_display = None
_font_cache = {}


def font(bold=False, mono=False, size=32):
    name = ("DejaVuSansMono-Bold.ttf" if bold else "DejaVuSansMono.ttf") if mono else ("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf")
    k = (name, size)
    if k not in _font_cache:
        f = None
        for d in FONT_DIRS:
            try:
                f = ImageFont.truetype(d + name, size)
                break
            except OSError:
                continue
        _font_cache[k] = f or ImageFont.load_default()
    return _font_cache[k]


def shape(t):
    if HAS_RAQM or not has_rtl(t) or not (arabic_reshaper and get_display):
        return t
    return get_display(arabic_reshaper.reshape(t))


def tlen(d, t, f):
    return d.textlength(shape(t), font=f)


def wrap(d, text, f, width, max_lines):
    words, lines, cur = text.split(" "), [], ""
    for w in words:
        trial = (cur + " " + w).strip()
        if tlen(d, trial, f) <= width or not cur:
            cur = trial
        else:
            lines.append(cur)
            cur = w
        if len(lines) == max_lines:
            break
    else:
        lines.append(cur)
    if len(lines) > max_lines or (len(lines) == max_lines and " ".join(lines) != text):
        lines = lines[:max_lines]
        last = lines[-1]
        while last and tlen(d, last + "…", f) > width:
            last = last[:-1]
        lines[-1] = last.rstrip() + "…"
    return lines


def fit(d, text, width, start, minimum, bold=True, mono=False):
    s = start
    while s > minimum and tlen(d, text, font(bold, mono, s)) > width:
        s -= 4
    f = font(bold, mono, s)
    if tlen(d, text, f) > width:  # قصّ نهائي
        while text and tlen(d, text + "…", f) > width:
            text = text[:-1]
        text += "…"
    return f, text


_bg = None
_logo = None
GREEN, GREEN2, INK, MUTED, LINE = (34, 197, 94), (74, 222, 128), (244, 246, 243), (154, 163, 156), (38, 52, 42)
PALETTES = [((17, 70, 44), (134, 239, 172)), ((14, 62, 66), (125, 231, 233)), ((23, 45, 89), (147, 197, 253)),
            ((52, 35, 92), (196, 181, 253)), ((84, 52, 12), (252, 211, 77)), ((88, 28, 48), (253, 164, 175))]


def background():
    global _bg
    if _bg is None:
        base = Image.new("RGB", (W, H), (6, 8, 7))
        glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        gd = ImageDraw.Draw(glow)
        gd.ellipse((640, -320, 1400, 300), fill=(20, 92, 52, 210))
        gd.ellipse((-360, 380, 300, 900), fill=(16, 70, 40, 130))
        glow = glow.filter(ImageFilter.GaussianBlur(120))
        base.paste(glow, (0, 0), glow)
        grid = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        gr = ImageDraw.Draw(grid)
        for x in range(0, W, 40):
            gr.line((x, 0, x, H), fill=(255, 255, 255, 7))
        for y in range(0, H, 40):
            gr.line((0, y, W, y), fill=(255, 255, 255, 7))
        base.paste(grid, (0, 0), grid)
        _bg = base
    return _bg.copy()


def logo(size):
    global _logo
    if _logo is None:
        p = ROOT / "web" / "icon-192.png"
        _logo = Image.open(p).convert("RGBA") if p.exists() else Image.new("RGBA", (192, 192), (17, 70, 44, 255))
    return _logo.resize((size, size), Image.LANCZOS)


def rounded(im, radius):
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, im.size[0] - 1, im.size[1] - 1), radius, fill=255)
    out = im.convert("RGBA")
    out.putalpha(m)
    return out


def monogram(name, size, key):
    bg, fg = PALETTES[int(hashlib.sha1(key.encode("utf-8")).hexdigest(), 16) % len(PALETTES)]
    im = Image.new("RGB", (size, size), bg)
    d = ImageDraw.Draw(im)
    ch = next((c for c in name if c.isalnum()), "#").upper()
    d.text((size / 2, size / 2), shape(ch), font=font(True, False, int(size * 0.56)), fill=fg, anchor="mm")
    return im


def pill(d, x, y, text, f, fill=None, outline=LINE, color=INK, pad=22, h=52):
    w = int(tlen(d, text, f)) + pad * 2
    d.rounded_rectangle((x, y, x + w, y + h), h // 2, fill=fill, outline=outline, width=2)
    d.text((x + w / 2, y + h / 2), shape(text), font=f, fill=color, anchor="mm")
    return w


def counts_text(t, nl, ne):
    return ([t["pkg1"] if nl == 1 else t["pkgs"].format(n=nl)] if nl else []) + ([t["ext1"] if ne == 1 else t["exts"].format(n=ne)] if ne else [])


def draw_card(e, lang, host_path):
    """بطاقة 1200x630. RTL يعكس التخطيط كاملاً (أيقونة يميناً، نص محاذى يميناً)."""
    im = background()
    d = ImageDraw.Draw(im)
    kind = e["kind"]
    desc = clean_text(e["desc"], 220)
    rtl = has_rtl(desc) if desc else has_rtl(e["name"])
    M = 72

    def X(x, w=0):  # مرآة أفقية عند RTL
        return W - x - w if rtl else x

    def text_at(x, y, t, f, fill):
        """x = هامش من الحافة الأمامية (يسار في LTR / يمين في RTL)."""
        d.text((W - x if rtl else x, y), shape(t), font=f, fill=fill, anchor="ra" if rtl else "la")
        return tlen(d, t, f)

    # ---- الشريط العلوي: شعار + اسم المتجر / شارة النوع
    lg = rounded(logo(64), 16)
    im.paste(lg, (X(M, 64), 52), lg)
    bf = font(True, False, 34)
    store_name = "Rin Libraries"
    text_at(M + 64 + 18, 62, store_name, bf, INK)
    label = L10N[lang][BADGE.get(kind, "official")]
    pf = font(True, False, 26)
    pw = int(tlen(d, label, pf)) + 52
    px = M if rtl else W - M - pw  # الشارة على الجهة المقابلة للشعار
    styles = {"official": dict(fill=GREEN, outline=GREEN, color=(4, 20, 10)), "community": dict(fill=None, outline=GREEN, color=GREEN2),
              "extension": dict(fill=None, outline=(94, 183, 255), color=(147, 205, 255)), "profile": dict(fill=None, outline=(120, 132, 124), color=INK)}
    if kind != "store":
        pill(d, px, 58, label, pf, **styles[kind], pad=26, h=52)

    # ---- الأيقونة الكبيرة
    S = 216
    if kind in ("official", "store"):
        big = rounded(logo(S), 44)
    elif e.get("icon") is not None:
        ic = e["icon"].resize((S, S), Image.LANCZOS)
        back = Image.new("RGBA", (S, S), (14, 20, 16, 255))
        back.alpha_composite(ic)
        big = rounded(back, 44)
    else:
        big = rounded(monogram(e["name"].lstrip("@"), S, e["path"]), S // 2 if kind == "profile" else 44)
    iy = 178
    glow = Image.new("RGBA", (S + 120, S + 120), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rounded_rectangle((60, 60, 60 + S, 60 + S), 44, fill=(34, 197, 94, 70))
    glow = glow.filter(ImageFilter.GaussianBlur(28))
    im.paste(glow, (X(M, S) - 60, iy - 60), glow)
    im.paste(big, (X(M, S), iy), big)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((X(M, S), iy, X(M, S) + S, iy + S), S // 2 if kind == "profile" else 44, outline=(52, 74, 58), width=2)

    # ---- كتلة النص
    tx = M + S + 44  # مسافة من حافة الأيقونة
    avail = W - tx - M
    name = e["name"]
    nf = font(True, False, 104)
    while nf.size > 60 and tlen(d, name, nf) > avail:
        nf = font(True, False, nf.size - 4)
    name_lines = wrap(d, name, nf, avail, 2) if tlen(d, name, nf) > avail else [name]
    y = 168 if len(name_lines) == 1 else 150
    for ln in name_lines:
        text_at(tx, y, ln, nf, INK)
        y += nf.size + (14 if len(name_lines) > 1 else 22)
    ref = host_path if kind == "store" else f"@{e['user']}/{e['slug']}" if kind != "profile" else f"@{e['user']}"
    rf, ref = fit(d, ref, avail, 34, 22, bold=False, mono=True)
    # المسار يُرسم LTR دائماً (اسم مستخدم عربي داخل مسار URL لا يجب أن يتبدّل ترتيبه)
    d.text((W - tx if rtl else tx, y), ref, font=rf, fill=GREEN2, anchor="ra" if rtl else "la", **({"direction": "ltr"} if HAS_RAQM else {}))
    y += rf.size + 34

    body = desc
    if kind == "profile":
        nl, ne = e["counts"]
        body = " · ".join(counts_text(L10N[lang], nl, ne))
        top = ", ".join(i["name"] for i in e["items"][:4])
        body = body + ("\n" + top if top else "")
    if not body:
        body = L10N[lang]["def_ext"].format(n=e["name"]) if kind == "extension" else L10N[lang]["def_lib"].format(n=e["name"])
    df = font(False, False, 32)
    lines = []
    for chunk in body.split("\n"):
        lines += wrap(d, chunk, df, avail, 2 if kind != "profile" else 1)
    for ln in lines[:3]:
        text_at(tx, y, ln, df, (181, 190, 182))
        y += 46

    # ---- الصف السفلي: أرقام
    chips = []
    if kind == "store":
        chips = [L10N[lang]["official"], L10N[lang]["community"], L10N[lang]["extension"]]
    elif kind == "profile":
        chips = [f"↓ {fmt_count(e['downloads'])}", f"♥ {fmt_count(e['likes'])}"]
    else:
        chips = [f"v{e['version']}"]
        if e["downloads"]:
            chips.append(f"↓ {fmt_count(e['downloads'])}")
        if e["likes"]:
            chips.append(f"♥ {fmt_count(e['likes'])}")
        if e["rating"]:
            chips.append(f"★ {e['rating']:.1f}")
        if e["size"]:
            chips.append(fmt_size(e["size"]))
    cf_ = font(True, False, 26)
    cy = 500
    cx = M
    order = chips
    for c in order:
        w = int(tlen(d, c, cf_)) + 44
        pill(d, X(cx, w), cy, c, cf_, fill=(14, 20, 16), pad=22, h=52)
        cx += w + 14
    hf = font(False, True, 22)
    hw = tlen(d, host_path, hf)
    if kind != "store" and hw + cx < W - M:
        hx = (W - M - hw) if not rtl else M
        d.text((hx, cy + 14), host_path, font=hf, fill=(112, 124, 116))
    return im.convert("RGB")


def draw_store_card(lang):
    e = dict(kind="store", user="rin", name="Rin Libraries", slug="", path="@rin", desc={
        "ar": "مكتبات رسمية ومن المجتمع لـ Rin — تصفّح، انسخ الكود، حمّل، وشارك رابطاً مباشراً.",
        "en": "Official and community libraries for Rin — browse, copy, download and share a direct link."}[lang],
             version="", size=0, downloads=0, likes=0, rating=0, icon=None)
    im = draw_card(e, lang, "dlof-lib.github.io/rinlang")
    return im


# ----------------------------------------------------------------------------- HTML
def jld(obj):
    # كل "<" و">" و"&" تُهرَّب بصيغة \\uXXXX: آمنة داخل <script> ومقروءة كما هي لدى JSON.parse
    return json.dumps(obj, ensure_ascii=False).replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")


def ref_query(e):
    if e["kind"] == "profile":
        return "?@" + enc(e["user"])
    return "?@" + enc(e["user"]) + "/" + enc(e["slug"])


def page_url(e):
    if e["kind"] == "profile":
        return SITE + "@" + enc(e["user"]) + "/"
    return SITE + "@" + enc(e["user"]) + "/" + enc(e["slug"]) + "/"


def describe(e, lang):
    t = L10N[lang]
    if e["kind"] == "profile":
        nl, ne = e["counts"]
        tail = " · ".join(counts_text(t, nl, ne) + [f"↓ {fmt_count(e['downloads'])}", f"♥ {fmt_count(e['likes'])}"])
        top = ", ".join(i["name"] for i in e["items"][:3])
        return clean_text(t["def_prof"].format(n=e["user"], c=tail) + (f" — {top}" if top else ""), 200)
    d = clean_text(e["desc"], 150) or (t["def_ext"] if e["kind"] == "extension" else t["def_lib"]).format(n=e["name"])
    bits = [f"v{e['version']}"] + ([f"↓ {fmt_count(e['downloads'])}"] if e["downloads"] else []) + ([f"♥ {fmt_count(e['likes'])}"] if e["likes"] else [])
    return clean_text(f"{d} — " + " · ".join(bits), 200)


def title_of(e, lang):
    t = L10N[lang]
    if e["kind"] == "profile":
        return f"@{e['user']} — {t['profile']} Rin"
    kind_l = t["official"] if e["kind"] == "official" else t["extension"] if e["kind"] == "extension" else t["community"]
    return f"{e['name']} — {kind_l} Rin · @{e['user']}"


def jsonld_for(e, url, img, desc, lang):
    if e["kind"] == "profile":
        return {"@context": "https://schema.org", "@type": "ProfilePage", "url": url, "name": f"@{e['user']}", "inLanguage": lang,
                "mainEntity": {"@type": "Person", "name": e["user"], "alternateName": "@" + e["user"], "url": url, "image": img,
                               "description": desc}}
    base = {"@context": "https://schema.org", "url": url, "name": e["name"], "description": clean_text(e["desc"], 300) or desc,
            "image": img, "version": e["version"], "inLanguage": lang,
            "author": {"@type": "Person" if e["kind"] != "official" else "Organization", "name": e["user"], "url": SITE + "@" + enc(e["user"]) + "/"},
            "isPartOf": {"@type": "WebSite", "name": "Rin Libraries", "url": SITE}}
    if e["ts"]:
        base["dateModified"] = iso(e["ts"])
    if e["downloads"] or e["likes"]:
        base["interactionStatistic"] = [{"@type": "InteractionCounter", "interactionType": "https://schema.org/DownloadAction", "userInteractionCount": e["downloads"]},
                                        {"@type": "InteractionCounter", "interactionType": "https://schema.org/LikeAction", "userInteractionCount": e["likes"]}]
    if e["kind"] == "extension":
        base.update({"@type": "SoftwareApplication", "applicationCategory": "DeveloperApplication", "operatingSystem": "Android",
                     "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"}})
    else:
        base.update({"@type": "SoftwareSourceCode", "programmingLanguage": {"@type": "ComputerLanguage", "name": "Rin"}, "codeRepository": url})
    return base


PAGE = """<!doctype html>
<html lang="{lang}" dir="{dir}">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>{title}</title>
<meta name="description" content="{desc}"/>
<link rel="canonical" href="{url}"/>
<meta name="robots" content="index,follow,max-image-preview:large"/>
<meta name="theme-color" content="#060807"/>
<meta name="color-scheme" content="dark light"/>
<meta property="og:type" content="{og_type}"/>
<meta property="og:site_name" content="Rin Libraries"/>
<meta property="og:title" content="{title}"/>
<meta property="og:description" content="{desc}"/>
<meta property="og:url" content="{url}"/>
<meta property="og:image" content="{img}"/>
<meta property="og:image:secure_url" content="{img}"/>
<meta property="og:image:type" content="image/png"/>
<meta property="og:image:width" content="1200"/>
<meta property="og:image:height" content="630"/>
<meta property="og:image:alt" content="{alt}"/>
<meta property="og:locale" content="{locale}"/>
{extra_og}<meta name="twitter:card" content="summary_large_image"/>
<meta name="twitter:title" content="{title}"/>
<meta name="twitter:description" content="{desc}"/>
<meta name="twitter:image" content="{img}"/>
<meta name="twitter:image:alt" content="{alt}"/>
<link rel="icon" href="{site}favicon.ico" sizes="any"/>
<link rel="icon" type="image/png" sizes="32x32" href="{site}favicon-32.png"/>
<link rel="icon" type="image/png" sizes="16x16" href="{site}favicon-16.png"/>
<link rel="apple-touch-icon" href="{site}apple-touch-icon.png"/>
<link rel="manifest" href="{site}manifest.webmanifest"/>
<script type="application/ld+json">{jsonld}</script>
<style>
  :root{{color-scheme:dark}}
  *{{box-sizing:border-box}}
  html,body{{margin:0;min-height:100%;background:#060807;color:#f4f6f3;font-family:system-ui,-apple-system,"Segoe UI",Tahoma,sans-serif}}
  body{{display:flex;align-items:center;justify-content:center;padding:24px;
    background:radial-gradient(900px 500px at 85% -10%,rgba(20,92,52,.55),transparent 60%),#060807}}
  main{{width:100%;max-width:560px;border:1px solid #1f2a22;border-radius:22px;background:#0a0c0a;padding:28px}}
  .top{{display:flex;align-items:center;gap:14px}}
  .top img{{width:56px;height:56px;border-radius:14px}}
  .badge{{margin-inline-start:auto;font-size:13px;font-weight:700;padding:5px 12px;border-radius:99px;border:1.5px solid #22c55e;color:#4ade80}}
  .badge.official{{background:#22c55e;color:#04140a}}
  h1{{font-size:clamp(28px,7vw,40px);margin:22px 0 6px;line-height:1.15;word-break:break-word}}
  .ref{{direction:ltr;text-align:start;font-family:ui-monospace,Consolas,monospace;color:#4ade80;font-size:15px;word-break:break-all;margin:0}}
  .desc{{color:#b5bdb6;line-height:1.7;margin:16px 0}}
  ul{{list-style:none;padding:0;margin:0 0 22px;display:flex;flex-wrap:wrap;gap:8px}}
  li{{background:#0e1410;border:1px solid #1f2a22;border-radius:99px;padding:6px 14px;font-size:14px;font-weight:600}}
  .btn{{display:inline-block;background:#22c55e;color:#04140a;font-weight:800;text-decoration:none;padding:12px 22px;border-radius:12px}}
  .btn:focus-visible{{outline:3px solid #86efac;outline-offset:3px}}
  @media (prefers-reduced-motion:no-preference){{.btn{{transition:filter .15s}}.btn:hover{{filter:brightness(1.1)}}}}
</style>
</head>
<body>
<main>
  <div class="top"><img src="{site}icon-192.png" alt="Rin" width="56" height="56"/><strong>Rin Libraries</strong><span class="badge {kind_cls}">{badge}</span></div>
  <h1>{name}</h1>
  <p class="ref">{ref}</p>
  <p class="desc">{body}</p>
  <ul>{chips}</ul>
  <a class="btn" href="{open_url}">{open}</a>
  <noscript><p class="desc">{noscript}</p></noscript>
</main>
<script>
  // زائر بشري: افتح تطبيق المتجر (نفس index.html)؛ الزواحف لا تنفّذ JS فتقرأ الوسوم أعلاه.
  (function(){{ try {{ location.replace({redirect} + location.hash); }} catch (e) {{}} }})();
</script>
</body>
</html>
"""


def render_page(e, img_url, lang=None):
    lang = lang or lang_of(e["desc"], e["name"])
    t = L10N[lang]
    url = page_url(e)
    desc = describe(e, lang)
    kind = e["kind"]
    badge = t["profile"] if kind == "profile" else t["official"] if kind == "official" else t["extension"] if kind == "extension" else t["community"]
    chips = ([f"<li>{html.escape(c)}</li>" for c in (
        [f"↓ {fmt_count(e['downloads'])}", f"♥ {fmt_count(e['likes'])}"] if kind == "profile" else
        [f"v{e['version']}"] + ([f"↓ {fmt_count(e['downloads'])}"] if e["downloads"] else []) + ([f"♥ {fmt_count(e['likes'])}"] if e["likes"] else [])
        + ([fmt_size(e["size"])] if e["size"] else []))])
    extra = ""
    if kind == "profile":
        extra = f'<meta property="profile:username" content="{html.escape(e["user"], True)}"/>\n'
    elif e["ts"]:
        extra = f'<meta property="og:updated_time" content="{iso(e["ts"])}"/>\n'
    body_txt = clean_text(e["desc"], 600) if kind != "profile" else describe(e, lang)
    if kind != "profile" and not body_txt:
        body_txt = desc
    ref = f"@{e['user']}" + ("" if kind == "profile" else f"/{e['slug']}")
    name = e["name"]
    return PAGE.format(
        lang=lang, dir="rtl" if lang == "ar" else "ltr", title=html.escape(title_of(e, lang), True), desc=html.escape(desc, True), url=html.escape(url, True),
        img=html.escape(img_url, True), alt=html.escape(f"{name} — Rin", True), locale="ar_AR" if lang == "ar" else "en_US",
        og_type="profile" if kind == "profile" else "website", extra_og=extra, site=SITE, jsonld=jld(jsonld_for(e, url, img_url, desc, lang)),
        kind_cls=kind, badge=html.escape(badge), name=html.escape(name), ref=html.escape(ref), body=html.escape(body_txt), chips="".join(chips),
        open=html.escape(t["open"]), open_url=html.escape(SITE + ref_query(e), True), noscript=html.escape(t["noscript"]),
        redirect=jld(SITE + ref_query(e)))


# ----------------------------------------------------------------------------- الكتابة
def write(path, data, mode="w"):
    path.parent.mkdir(parents=True, exist_ok=True)
    if mode == "w":
        path.write_text(data, encoding="utf-8")
    else:
        path.write_bytes(data)


def save_png(im, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    im.save(path, "PNG", optimize=True)


def generate(data, out, limit=0):
    ents, profs = build_entities(data)
    allents = sorted(ents, key=lambda e: (-e["ts"], e["path"]))
    if limit:
        allents = allents[:limit]
    urls = []  # (loc, lastmod, priority)
    written = {}

    def emit(e):
        rel = e["path"]
        lang = lang_of(e["desc"], e["name"])
        h = hashlib.sha1(rel.encode("utf-8")).hexdigest()[:14]
        img_rel = f"og/{h}.png"
        save_png(draw_card(e, lang, "dlof-lib.github.io/rinlang"), out / img_rel)
        page = render_page(e, SITE + img_rel)
        for variant in dict.fromkeys([rel, rel.lower()]):  # المسار كما نُشر + نسخة بأحرف صغيرة
            if variant not in written:
                write(out / variant / "index.html", page)
                written[variant] = True
        urls.append((page_url(e), iso(e["ts"]), "0.7" if e["kind"] != "profile" else "0.6"))

    for e in allents:
        emit(e)
    for p in profs:
        if limit and not any(i in allents for i in p["items"]):
            continue
        emit(p)

    save_png(draw_store_card("ar"), out / "og-store.png")
    save_png(draw_store_card("en"), out / "og-store-en.png")
    urls.insert(0, (SITE, "", "1.0"))
    sm = ['<?xml version="1.0" encoding="UTF-8"?>', '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">']
    for loc, lm, pr in urls:
        sm.append(f"<url><loc>{html.escape(loc)}</loc>" + (f"<lastmod>{lm}</lastmod>" if lm else "") + f"<priority>{pr}</priority></url>")
    sm.append("</urlset>")
    write(out / "sitemap.xml", "\n".join(sm) + "\n")
    write(out / "robots.txt", f"User-agent: *\nAllow: /\n\nSitemap: {SITE}sitemap.xml\n")
    return len(ents), len(profs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=str(ROOT / "dist"))
    ap.add_argument("--data", help="JSON بنفس شكل Firebase (libraries/packages/extensions) بدل الجلب من الشبكة")
    ap.add_argument("--db-url", default=DB_URL)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--strict", action="store_true")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    try:
        data = json.loads(Path(a.data).read_text(encoding="utf-8")) if a.data else fetch_all(a.db_url.rstrip("/"))
    except Exception as ex:
        msg = f"share pages skipped: {ex}"
        if a.strict:
            sys.exit(msg)
        print(f"::warning::{msg}")
        # حتى بلا Firebase: نُنتج بطاقتي المتجر + robots/sitemap الأساسية كي لا ينكسر النشر
        save_png(draw_store_card("ar"), out / "og-store.png")
        save_png(draw_store_card("en"), out / "og-store-en.png")
        write(out / "robots.txt", f"User-agent: *\nAllow: /\n\nSitemap: {SITE}sitemap.xml\n")
        write(out / "sitemap.xml", f'<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"><url><loc>{SITE}</loc><priority>1.0</priority></url></urlset>\n')
        return 0
    n, p = generate(data, out, a.limit)
    print(f"share pages: {n} items + {p} profiles -> {out}  (raqm={HAS_RAQM})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

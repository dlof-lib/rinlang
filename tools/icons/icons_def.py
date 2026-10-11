"""
icons_def.py — تعريفات أيقونات indsin (مرسومة من الصفر على شبكة 24x24).

القواعد (نظام التصميم):
  • شبكة 24×24، هامش آمن 2px، سماكة الخط الافتراضية 2، أطراف ووصلات دائرية (round).
  • كل أيقونة = مسار SVG واحد (سلسلة d) بأوامر مطلقة M L H V C A Z فقط (بلا عناصر أخرى)،
    فيسهل تمريرها كنص إلى Rin وC++ وJS دون محلّل XML.
  • النقطة = DOT (قطعة طولها 0.01 بطرف دائري).

الاستخدام: ICONS = {name: (category, path)} + ALIASES = {alias: name}
"""
import math

# ------------------------------------------------------------------ أدوات الرسم
def f(v):
    s = ("%.2f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s

def C(cx, cy, r):
    return "M%s %sA%s %s 0 1 0 %s %sA%s %s 0 1 0 %s %sZ" % (
        f(cx - r), f(cy), f(r), f(r), f(cx + r), f(cy), f(r), f(r), f(cx - r), f(cy))

def R(x, y, w, h, r=0):
    if r <= 0:
        return "M%s %sH%sV%sH%sZ" % (f(x), f(y), f(x + w), f(y + h), f(x))
    a = "A%s %s 0 0 1 " % (f(r), f(r))
    return ("M%s %sH%s%s%s %sV%s%s%s %sH%s%s%s %sV%s%s%s %sZ" % (
        f(x + r), f(y), f(x + w - r), a, f(x + w), f(y + r), f(y + h - r),
        a, f(x + w - r), f(y + h), f(x + r), a, f(x), f(y + h - r), f(y + r), a, f(x + r), f(y)))

def L(*p):                      # خط/خطوط متصلة مفتوحة: L(x1,y1,x2,y2,...)
    pts = list(zip(p[0::2], p[1::2]))
    return "M" + "L".join("%s %s" % (f(x), f(y)) for x, y in pts)

def P(*p):                      # مضلّع مغلق
    return L(*p) + "Z"

def DOT(x, y):
    return "M%s %sL%s %s" % (f(x), f(y), f(x + 0.01), f(y))

def pol(cx, cy, r, deg):        # نقطة على دائرة (الزاوية بالدرجات، 0 = اليمين، تزداد مع عقارب الساعة)
    a = math.radians(deg)
    return cx + r * math.cos(a), cy + r * math.sin(a)

def ARC(cx, cy, r, a0, a1):     # قوس مفتوح من a0 إلى a1 (مع عقارب الساعة إن كان a1 > a0)
    x0, y0 = pol(cx, cy, r, a0); x1, y1 = pol(cx, cy, r, a1)
    sweep = 1 if a1 > a0 else 0
    large = 1 if abs(a1 - a0) > 180 else 0
    return "M%s %sA%s %s 0 %d %d %s %s" % (f(x0), f(y0), f(r), f(r), large, sweep, f(x1), f(y1))

def HEAD(x, y, ang, s=3.6, spread=40):   # رأس سهم عند (x,y) متّجه نحو الزاوية ang
    a = math.radians(ang)
    pts = []
    for sg in (-1, 1):
        b = a + math.pi + sg * math.radians(spread)
        pts.append((x + s * math.cos(b), y + s * math.sin(b)))
    return "M%s %sL%s %sL%s %s" % (f(pts[0][0]), f(pts[0][1]), f(x), f(y), f(pts[1][0]), f(pts[1][1]))

def ARROWARC(cx, cy, r, a0, a1, head=3.8):
    x1, y1 = pol(cx, cy, r, a1)
    tang = a1 + (90 if a1 > a0 else -90)
    return ARC(cx, cy, r, a0, a1) + HEAD(x1, y1, tang, head, 42)

def RAYS(cx, cy, r0, r1, n, off=0):
    out = []
    for i in range(n):
        d = off + i * 360.0 / n
        x0, y0 = pol(cx, cy, r0, d); x1, y1 = pol(cx, cy, r1, d)
        out.append("M%s %sL%s %s" % (f(x0), f(y0), f(x1), f(y1)))
    return "".join(out)

def STAR(cx, cy, ro, ri, n=5, off=-90):
    pts = []
    for i in range(n * 2):
        r = ro if i % 2 == 0 else ri
        x, y = pol(cx, cy, r, off + i * 180.0 / n)
        pts += [x, y]
    return P(*pts)

def POLY(cx, cy, r, n, off=-90):
    pts = []
    for i in range(n):
        x, y = pol(cx, cy, r, off + i * 360.0 / n)
        pts += [x, y]
    return P(*pts)

def GEAR(cx, cy, r_in, r_out, teeth=8, tw=0.36):
    # ترس: كل سنّ شبه منحرف؛ tw = نصف عرض السنّ كجزء من الخطوة الزاوية
    pts = []
    step = 360.0 / teeth
    for i in range(teeth):
        c = -90 + i * step
        for d, r in ((-tw * 1.45, r_in), (-tw * 0.8, r_out), (tw * 0.8, r_out), (tw * 1.45, r_in)):
            x, y = pol(cx, cy, r, c + d * step)
            pts += [x, y]
    return P(*pts)

# ------------------------------------------------------------------ الأيقونات
I = {}
def add(cat, name, *parts):
    I[name] = (cat, "".join(parts))

# ---- أسهم وتنقّل
add("arrows", "arrow-left",  "M19 12H5M11 6L5 12L11 18")
add("arrows", "arrow-right", "M5 12H19M13 6L19 12L13 18")
add("arrows", "arrow-up",    "M12 19V5M6 11L12 5L18 11")
add("arrows", "arrow-down",  "M12 5V19M6 13L12 19L18 13")
add("arrows", "arrow-up-right",   "M7 17L17 7M8 7H17V16")
add("arrows", "arrow-up-left",    "M17 17L7 7M16 7H7V16")
add("arrows", "arrow-down-right", "M7 7L17 17M17 8V17H8")
add("arrows", "arrow-down-left",  "M17 7L7 17M7 8V17H16")
add("arrows", "chevron-left",  "M15 5L8 12L15 19")
add("arrows", "chevron-right", "M9 5L16 12L9 19")
add("arrows", "chevron-up",    "M5 15L12 8L19 15")
add("arrows", "chevron-down",  "M5 9L12 16L19 9")
add("arrows", "chevrons-left",  "M11 6L5 12L11 18M19 6L13 12L19 18")
add("arrows", "chevrons-right", "M13 6L19 12L13 18M5 6L11 12L5 18")
add("arrows", "chevrons-up",    "M6 11L12 5L18 11M6 19L12 13L18 19")
add("arrows", "chevrons-down",  "M6 5L12 11L18 5M6 13L12 19L18 13")
add("arrows", "corner-down-right", "M5 5V10A5 5 0 0 0 10 15H19M15 11L19 15L15 19")
add("arrows", "undo", "M9 14L4 9L9 4M4 9H15A5 5 0 0 1 15 19H10")
add("arrows", "redo", "M15 14L20 9L15 4M20 9H9A5 5 0 0 0 9 19H14")
add("arrows", "refresh", ARROWARC(12, 12, 8, -50, 250))
add("arrows", "rotate", ARROWARC(12, 12, 8, 230, -70))
add("arrows", "sort", "M7 4V20M3 8L7 4L11 8M17 20V4M13 16L17 20L21 16")
add("arrows", "move", "M12 3V21M3 12H21M9 6L12 3L15 6M9 18L12 21L15 18M6 9L3 12L6 15M18 9L21 12L18 15")
add("arrows", "maximize", "M4 9V4H9M15 4H20V9M20 15V20H15M9 20H4V15")
add("arrows", "minimize", "M9 4V9H4M15 4V9H20M20 15H15V20M9 20V15H4")
add("arrows", "external-link", "M14 4H20V10M20 4L11 13M18 14V19A1 1 0 0 1 17 20H5A1 1 0 0 1 4 19V7A1 1 0 0 1 5 6H10")
add("arrows", "login",  "M10 8L15 12L10 16M15 12H3M13 4H19A1 1 0 0 1 20 5V19A1 1 0 0 1 19 20H13")
add("arrows", "logout", "M9 4H5A1 1 0 0 0 4 5V19A1 1 0 0 0 5 20H9M16 8L21 12L16 16M21 12H9")

# ---- واجهة وإجراءات
add("actions", "home", "M3 11L12 4L21 11M5.5 9.5V20H18.5V9.5M10 20V14H14V20")
add("actions", "menu", "M4 6H20M4 12H20M4 18H20")
add("actions", "menu-left", "M4 7H20M4 12H13M4 17H17")
add("actions", "more", DOT(5, 12), DOT(12, 12), DOT(19, 12))
add("actions", "more-vertical", DOT(12, 5), DOT(12, 12), DOT(12, 19))
add("actions", "grid", R(4, 4, 6.5, 6.5, 1.5), R(13.5, 4, 6.5, 6.5, 1.5), R(4, 13.5, 6.5, 6.5, 1.5), R(13.5, 13.5, 6.5, 6.5, 1.5))
add("actions", "list", "M9 6H20M9 12H20M9 18H20", DOT(4.5, 6), DOT(4.5, 12), DOT(4.5, 18))
add("actions", "close", "M6 6L18 18M18 6L6 18")
add("actions", "plus", "M12 5V19M5 12H19")
add("actions", "minus", "M5 12H19")
add("actions", "check", "M5 12.5L10 17.5L19 7")
add("actions", "check-circle", C(12, 12, 9), "M8 12.5L11 15.5L16 9")
add("actions", "x-circle", C(12, 12, 9), "M9 9L15 15M15 9L9 15")
add("actions", "plus-circle", C(12, 12, 9), "M12 8V16M8 12H16")
add("actions", "minus-circle", C(12, 12, 9), "M8 12H16")
add("actions", "search", C(10.5, 10.5, 6.5), "M15.5 15.5L20 20")
add("actions", "zoom-in", C(10.5, 10.5, 6.5), "M15.5 15.5L20 20M10.5 8V13M8 10.5H13")
add("actions", "zoom-out", C(10.5, 10.5, 6.5), "M15.5 15.5L20 20M8 10.5H13")
add("actions", "settings", GEAR(12, 12, 7.4, 9.6, 8, 0.2), C(12, 12, 2.8))
add("actions", "sliders", "M4 7H12M16 7H20M4 17H8M12 17H20", C(14, 7, 2), C(10, 17, 2))
add("actions", "filter", "M4 5H20L14 12.5V19L10 17V12.5Z")
add("actions", "edit", P(4, 20, 4, 16, 15, 5, 19, 9, 8, 20), "M13 7L17 11")
add("actions", "trash", "M4 7H20M9 7V4H15V7M6 7L7 20H17L18 7M10 11V16M14 11V16")
add("actions", "copy", R(8, 8, 12, 12, 2), "M16 8V6A2 2 0 0 0 14 4H6A2 2 0 0 0 4 6V14A2 2 0 0 0 6 16H8")
add("actions", "clipboard", R(5, 5, 14, 16, 2), "M9 5V4A1 1 0 0 1 10 3H14A1 1 0 0 1 15 4V5M9 12H15M9 16H13")
add("actions", "save", "M6 4H16L20 8V18A2 2 0 0 1 18 20H6A2 2 0 0 1 4 18V6A2 2 0 0 1 6 4Z", "M8 4V9H15V4M8 20V14H16V20")
add("actions", "download", "M12 4V15M7 10L12 15L17 10M5 20H19")
add("actions", "upload", "M12 15V4M7 9L12 4L17 9M5 20H19")
add("actions", "share", C(6, 12, 2.5), C(18, 6, 2.5), C(18, 18, 2.5), "M8.2 10.8L15.8 7.2M8.2 13.2L15.8 16.8")
add("actions", "share-out", "M9 10H6V20H18V10H15M12 15V4M8 8L12 4L16 8")
add("actions", "link", "M10 14A4 4 0 0 0 15.7 14.3L18.7 11.3A4 4 0 0 0 13 5.6L12 6.6M14 10A4 4 0 0 0 8.3 9.7L5.3 12.7A4 4 0 0 0 11 18.4L12 17.4")
add("actions", "paperclip", "M20 11L12 19A5 5 0 0 1 5 12L13.5 3.5A3.3 3.3 0 0 1 18.2 8.2L9.7 16.7A1.7 1.7 0 0 1 7.3 14.3L15 6.6")
add("actions", "power", "M12 3V12M7 6.5A8 8 0 1 0 17 6.5")
add("actions", "toggle-on", R(2, 7, 20, 10, 5), C(16.5, 12, 2.5))
add("actions", "toggle-off", R(2, 7, 20, 10, 5), C(7.5, 12, 2.5))
add("actions", "loader", RAYS(12, 12, 5, 9, 8))
add("actions", "cursor", "M5 3L19 11L12 13L10 20Z")
add("actions", "printer", "M7 8V3H17V8M7 17H5A1 1 0 0 1 4 16V9A1 1 0 0 1 5 8H19A1 1 0 0 1 20 9V16A1 1 0 0 1 19 17H17M7 14H17V21H7Z")
add("actions", "send", "M21 3L3 10.5L10 13.5L13 21ZM10 13.5L21 3")
add("actions", "qr", R(3, 3, 7, 7, 1), R(14, 3, 7, 7, 1), R(3, 14, 7, 7, 1), "M14 14H17V17M21 14V14.01M14 21H17M21 17V21M17 21H21")

# ---- حالة وتنبيه
add("status", "info", C(12, 12, 9), "M12 11V16", DOT(12, 8))
add("status", "help", C(12, 12, 9), "M9.5 9.5A2.6 2.6 0 0 1 14.6 10.2C14.6 12 12 12 12 14", DOT(12, 17))
add("status", "warning", "M12 4L21 20H3Z", "M12 10V14", DOT(12, 17))
add("status", "error", POLY(12, 12, 9.6, 8, -67.5), "M12 8V13", DOT(12, 16))
add("status", "bell", "M6 10A6 6 0 0 1 18 10C18 14 19 16 20 17H4C5 16 6 14 6 10Z", "M10 20.5A2.2 2.2 0 0 0 14 20.5")
add("status", "bell-off", "M6 10A6 6 0 0 1 18 10C18 14 19 16 20 17H4C5 16 6 14 6 10Z", "M10 20.5A2.2 2.2 0 0 0 14 20.5M4 4L20 20")
add("status", "star", STAR(12, 12.5, 9.5, 4.2))
add("status", "heart", "M12 20L4.6 12.4A4.9 4.9 0 0 1 12 6.2A4.9 4.9 0 0 1 19.4 12.4Z")
add("status", "thumbs-up", "M7 11V20H4V11ZM7 11L11 3A2 2 0 0 1 14 5L13 9H19A2 2 0 0 1 21 11.4L19.8 18.4A2 2 0 0 1 17.8 20H7")
add("status", "smile", C(12, 12, 9), "M8 14A5 5 0 0 0 16 14", DOT(9, 9.5), DOT(15, 9.5))
add("status", "flag", "M5 21V4M5 4H17L15 8.5L17 13H5")
add("status", "bookmark", "M6 4H18V21L12 17L6 21Z")
add("status", "tag", "M4 4H12L21 13L13 21L4 12Z", DOT(8.5, 8.5))
add("status", "pin", "M9 4H15L14 10L17 13H7L10 10ZM12 13V21")
add("status", "zap", "M13 3L5 14H11L10 21L19 9.5H13Z")
add("status", "sparkles", "M12 3C12.5 8 14 10.5 19 12C14 13.5 12.5 16 12 21C11.5 16 10 13.5 5 12C10 10.5 11.5 8 12 3Z", "M19 3V7M17 5H21")
add("status", "target", C(12, 12, 9), C(12, 12, 5), DOT(12, 12))
add("status", "activity", "M3 12H7L10 4L14 20L17 12H21")
add("status", "award", C(12, 9, 6), "M8.5 14L7 21L12 18.5L17 21L15.5 14")
add("status", "trophy", "M7 4H17V10A5 5 0 0 1 7 10Z", "M7 6H4V8A3 3 0 0 0 7 11M17 6H20V8A3 3 0 0 1 17 11M12 15V18M8 21H16M9.5 18H14.5")
add("status", "gift", R(3, 8, 18, 4, 1), "M5 12V20H19V12M12 8V20M12 8C9 8 7.5 6.5 7.5 5.2A2 2 0 0 1 11 4.5C12 6 12 8 12 8M12 8C15 8 16.5 6.5 16.5 5.2A2 2 0 0 0 13 4.5C12 6 12 8 12 8")

# ---- أمان ومستخدمون
add("security", "lock", R(5, 11, 14, 10, 2), "M8 11V8A4 4 0 0 1 16 8V11", DOT(12, 16))
add("security", "unlock", R(5, 11, 14, 10, 2), "M8 11V8A4 4 0 0 1 15.5 6.5", DOT(12, 16))
add("security", "key", C(8, 15, 4), "M11 12L20 3M16 7L19 10")
add("security", "shield", "M12 3L20 6V12C20 16.5 16.5 19.5 12 21C7.5 19.5 4 16.5 4 12V6Z")
add("security", "shield-check", "M12 3L20 6V12C20 16.5 16.5 19.5 12 21C7.5 19.5 4 16.5 4 12V6Z", "M8.5 12L11 14.5L15.5 9.5")
add("security", "eye", "M2 12C4.5 7.5 8 5.5 12 5.5C16 5.5 19.5 7.5 22 12C19.5 16.5 16 18.5 12 18.5C8 18.5 4.5 16.5 2 12Z", C(12, 12, 3))
add("security", "eye-off", "M2 12C4.5 7.5 8 5.5 12 5.5C16 5.5 19.5 7.5 22 12C19.5 16.5 16 18.5 12 18.5C8 18.5 4.5 16.5 2 12Z", C(12, 12, 3), "M4 4L20 20")
add("users", "user", C(12, 8, 4), "M4 20C4 16 7.5 14 12 14C16.5 14 20 16 20 20")
add("users", "users", C(9, 8, 3.5), "M2.5 19C2.5 16 5.5 14.5 9 14.5C12.5 14.5 15.5 16 15.5 19", "M15 4.6A3.5 3.5 0 0 1 15 11.4M17.5 14.7C20 15.3 21.5 16.8 21.5 19")
add("users", "user-plus", C(10, 8, 4), "M3 20C3 16.5 6 14.5 10 14.5C13 14.5 15 15.5 16 17M19 8V14M16 11H22")
add("users", "user-circle", C(12, 12, 9), C(12, 10, 3), "M6.5 18.5C8 16 10 15 12 15C14 15 16 16 17.5 18.5")

# ---- تواصل
add("comms", "mail", R(3, 5, 18, 14, 2), "M3.5 7L12 13L20.5 7")
add("comms", "message", "M5 4H19A2 2 0 0 1 21 6V15A2 2 0 0 1 19 17H11L6.5 20.5V17H5A2 2 0 0 1 3 15V6A2 2 0 0 1 5 4Z")
add("comms", "phone", "M5 4H9L11 9L8.5 10.5A11 11 0 0 0 13.5 15.5L15 13L20 15V19A1.5 1.5 0 0 1 18.5 20.5C10 20 4 14 3.5 5.5A1.5 1.5 0 0 1 5 4Z")
add("comms", "at-sign", C(12, 12, 4), "M16 8V13A2.5 2.5 0 0 0 21 13V12A9 9 0 1 0 17 19.5")
add("comms", "globe", C(12, 12, 9), "M3 12H21M12 3C9 6 8 9 8 12C8 15 9 18 12 21C15 18 16 15 16 12C16 9 15 6 12 3Z")
add("comms", "wifi", "M2.5 9A14 14 0 0 1 21.5 9M5.5 12.5A9.5 9.5 0 0 1 18.5 12.5M8.5 16A5 5 0 0 1 15.5 16", DOT(12, 19.5))
add("comms", "cloud", "M7 18A4.5 4.5 0 0 1 6.5 9A6 6 0 0 1 18 8.5A4.8 4.8 0 0 1 17.5 18Z")
add("comms", "cloud-upload", "M7 17A4.5 4.5 0 0 1 6.5 8A6 6 0 0 1 18 7.5A4.8 4.8 0 0 1 18 17M12 20V12M9 15L12 12L15 15")
add("comms", "cloud-download", "M7 17A4.5 4.5 0 0 1 6.5 8A6 6 0 0 1 18 7.5A4.8 4.8 0 0 1 18 17M12 12V20M9 17L12 20L15 17")
add("comms", "map-pin", "M12 21C12 21 5 14.5 5 9.5A7 7 0 0 1 19 9.5C19 14.5 12 21 12 21Z", C(12, 9.5, 2.5))
add("comms", "compass", C(12, 12, 9), "M15.5 8.5L13.5 13.5L8.5 15.5L10.5 10.5Z")

# ---- وسائط
add("media", "play", "M7 4.5L19 12L7 19.5Z")
add("media", "pause", R(6, 5, 4, 14, 1), R(14, 5, 4, 14, 1))
add("media", "stop", R(5, 5, 14, 14, 2))
add("media", "skip-next", "M6 5L15 12L6 19ZM18 5V19")
add("media", "skip-prev", "M18 5L9 12L18 19ZM6 5V19")
add("media", "fast-forward", "M3 6L11 12L3 18ZM12 6L20 12L12 18Z")
add("media", "rewind", "M21 6L13 12L21 18ZM12 6L4 12L12 18Z")
add("media", "volume", "M4 9.5H8L13 5V19L8 14.5H4Z", "M16.5 9A4 4 0 0 1 16.5 15M19 6.5A8 8 0 0 1 19 17.5")
add("media", "volume-off", "M4 9.5H8L13 5V19L8 14.5H4Z", "M17 9.5L22 14.5M22 9.5L17 14.5")
add("media", "mic", R(9, 3, 6, 11, 3), "M5.5 11A6.5 6.5 0 0 0 18.5 11M12 17.5V21M9 21H15")
add("media", "headphones", "M4 15V12A8 8 0 0 1 20 12V15", R(3, 14, 4.5, 6.5, 1.5), R(16.5, 14, 4.5, 6.5, 1.5))
add("media", "music", "M9 18V6L20 4V16", C(6.5, 18, 2.5), C(17.5, 16, 2.5))
add("media", "image", R(3, 4, 18, 16, 2), C(8.5, 9.5, 1.5), "M3.5 17L9 12L13 16L16 13L20.5 17.5")
add("media", "camera", "M4 8H7L9 5H15L17 8H20A1 1 0 0 1 21 9V19A1 1 0 0 1 20 20H4A1 1 0 0 1 3 19V9A1 1 0 0 1 4 8Z", C(12, 13.5, 3.5))
add("media", "video", R(3, 6, 13, 12, 2), "M16 10.5L21 7.5V16.5L16 13.5")
add("media", "film", R(4, 3, 16, 18, 2), "M4 8H8M4 12H8M4 16H8M16 8H20M16 12H20M16 16H20M8 3V21M16 3V21")
add("media", "repeat", "M4 11V10A3 3 0 0 1 7 7H19M16 4L19 7L16 10M20 13V14A3 3 0 0 1 17 17H5M8 14L5 17L8 20")
add("media", "shuffle", "M3 7H6C12 7 12 17 18 17H20M17 14L20 17L17 20M3 17H6C8 17 9.4 15.7 10.4 14.4M13.6 9.6C14.6 8.3 16 7 18 7H20M17 4L20 7L17 10")

# ---- ملفات وبيانات
add("files", "file", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3Z", "M14 3V8H19")
add("files", "file-text", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3Z", "M14 3V8H19M8.5 13H15.5M8.5 17H13")
add("files", "file-code", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3Z", "M14 3V8H19M10 13L8 15L10 17M14 13L16 15L14 17")
add("files", "file-plus", "M6 3H14L19 8V20A1 1 0 0 1 18 21H6A1 1 0 0 1 5 20V4A1 1 0 0 1 6 3Z", "M14 3V8H19M12 12V18M9 15H15")
add("files", "folder", "M3 7A2 2 0 0 1 5 5H9.5L12 8H19A2 2 0 0 1 21 10V17A2 2 0 0 1 19 19H5A2 2 0 0 1 3 17Z")
add("files", "folder-open", "M3 18V6.5A1.5 1.5 0 0 1 4.5 5H9L11.5 7.5H18A1.5 1.5 0 0 1 19.5 9V11M3 18L5.5 11.5H21.5L19 18.5A1.5 1.5 0 0 1 17.5 19.5H4.5A1.5 1.5 0 0 1 3 18Z")
add("files", "archive", R(3, 4, 18, 5, 1), "M5 9V19A1 1 0 0 0 6 20H18A1 1 0 0 0 19 19V9M10 13H14")
add("files", "inbox", "M3 13H8L9.5 16H14.5L16 13H21M3 13L6 5H18L21 13V19A1 1 0 0 1 20 20H4A1 1 0 0 1 3 19Z")
add("files", "database", "M4 6C4 4.3 7.6 3 12 3C16.4 3 20 4.3 20 6C20 7.7 16.4 9 12 9C7.6 9 4 7.7 4 6ZM4 6V18C4 19.7 7.6 21 12 21C16.4 21 20 19.7 20 18V6M4 12C4 13.7 7.6 15 12 15C16.4 15 20 13.7 20 12")
add("files", "calendar", R(4, 5, 16, 15, 2), "M4 10H20M8 3V7M16 3V7")
add("files", "clock", C(12, 12, 9), "M12 7V12L15.5 14")
add("files", "hourglass", "M6 3H18M6 21H18M7 3C7 8 10 10 12 12C14 14 17 16 17 21M17 3C17 8 14 10 12 12C10 14 7 16 7 21")
add("files", "history", ARROWARC(12, 12, 8, 230, -70), "M12 8V12L15 14")

# ---- مطوّرون ولغة Rin
add("dev", "code", "M8 7L3 12L8 17M16 7L21 12L16 17M13.5 5L10.5 19")
add("dev", "terminal", R(3, 4, 18, 16, 2), "M7 9L11 12L7 15M13 16H17")
add("dev", "braces", "M9 4C6.5 4 6 5 6 7V9C6 10.5 5 12 3.5 12C5 12 6 13.5 6 15V17C6 19 6.5 20 9 20M15 4C17.5 4 18 5 18 7V9C18 10.5 19 12 20.5 12C19 12 18 13.5 18 15V17C18 19 17.5 20 15 20")
add("dev", "bug", "M12 8C14.5 8 16 9.5 16 12V15A4 4 0 0 1 8 15V12C8 9.5 9.5 8 12 8Z", "M9.5 6.5A2.5 2.5 0 0 1 14.5 6.5M8 12H4M16 12H20M8 16H4.5M16 16H19.5M8.5 9L5.5 6.5M15.5 9L18.5 6.5M12 12V19")
add("dev", "cpu", R(6, 6, 12, 12, 2), R(9.5, 9.5, 5, 5, 1), "M9 2V6M15 2V6M9 18V22M15 18V22M2 9H6M2 15H6M18 9H22M18 15H22")
add("dev", "package", "M12 3L20 7.5V16.5L12 21L4 16.5V7.5Z", "M4 7.5L12 12L20 7.5M12 12V21")
add("dev", "layers", "M12 3L21 7.5L12 12L3 7.5Z", "M3 12L12 16.5L21 12M3 16.5L12 21L21 16.5")
add("dev", "git-branch", "M6 3V15", C(6, 18, 3), C(18, 6, 3), "M18 9C18 14 14 18 9 18")
add("dev", "container", "M7 5H4V19H7M17 5H20V19H17", R(8.5, 8.5, 7, 7, 1.5))
add("dev", "view", R(3, 4, 18, 16, 2), "M3 9H21M7 14H13M7 17H10")
add("dev", "function", "M14 4C11 4 10 5.5 10 8V20M7 11H14M15 14L20 20M20 14L15 20")
add("dev", "loop", ARROWARC(12, 12, 8, 200, 340), ARROWARC(12, 12, 8, 20, 160))
add("dev", "library", R(3.5, 4, 4.5, 16, 1), R(9.5, 4, 4.5, 16, 1), "M16.2 6.5L20 5.5L22 18L18.2 19Z")
add("dev", "book", "M3 5A1 1 0 0 1 4 4H10A2 2 0 0 1 12 6V20A2 2 0 0 0 10 19H4A1 1 0 0 1 3 18ZM21 5A1 1 0 0 0 20 4H14A2 2 0 0 0 12 6V20A2 2 0 0 1 14 19H20A1 1 0 0 0 21 18Z")
add("dev", "graduation", "M2 9L12 4L22 9L12 14ZM6 11.5V16C6 17.5 9 19 12 19C15 19 18 17.5 18 16V11.5M22 9V15")
add("dev", "rocket", "M12 3C16 5 18 9 17 14L12 19L7 14C6 9 8 5 12 3Z", C(12, 10, 1.8), "M7.4 13L4.5 15.5V19L9.5 16.5M16.6 13L19.5 15.5V19L14.5 16.5M12 19.5V22")
add("dev", "lightbulb", "M9 18H15M10 21H14M12 3A6 6 0 0 0 8.5 14C9.3 14.7 9.5 15.5 9.5 16.5V18H14.5V16.5C14.5 15.5 14.7 14.7 15.5 14A6 6 0 0 0 12 3Z")
add("dev", "cart", "M3 4H6L8 15H18L20 7H7", C(9.5, 19, 1.3), C(17, 19, 1.3))
add("dev", "credit-card", R(3, 5, 18, 14, 2), "M3 10H21M7 15H11")
add("dev", "wallet", "M4 7A2 2 0 0 1 6 5H17V8M4 7V18A2 2 0 0 0 6 20H20V8H6A2 2 0 0 1 4 7Z", DOT(16, 14))
add("dev", "coffee", "M5 9H16V15A5 5 0 0 1 11 20H10A5 5 0 0 1 5 15ZM16 10H18A2.5 2.5 0 0 1 18 15H16M8 3C8 5 9.5 5 9.5 7M12 3C12 5 13.5 5 13.5 7")

# ---- مخططات وتخطيط
add("charts", "chart-bar", R(4, 11, 4, 9, 1), R(10, 4, 4, 16, 1), R(16, 8, 4, 12, 1))
add("charts", "chart-line", "M3 3V21H21M7 15L11 10L14 13L20 6")
add("charts", "chart-pie", "M12 3A9 9 0 1 0 21 12H12ZM15 3.5A9 9 0 0 1 20.5 9H15Z")
add("charts", "trending-up", "M3 17L9 11L13 15L21 7M15 7H21V13")
add("charts", "trending-down", "M3 7L9 13L13 9L21 17M15 17H21V11")
add("charts", "layout", R(3, 3, 18, 18, 2), "M3 9H21M9 9V21")
add("charts", "sidebar", R(3, 4, 18, 16, 2), "M9 4V20")
add("charts", "table", R(3, 4, 18, 16, 2), "M3 9.5H21M3 14.5H21M9.5 9.5V20")
add("charts", "square", R(4, 4, 16, 16, 2))
add("charts", "circle", C(12, 12, 9))
add("charts", "triangle", "M12 4L21 19H3Z")
add("charts", "hexagon", POLY(12, 12, 9.2, 6, -90))

# ---- تصميم ونص
add("design", "palette", "M12 3A9 9 0 0 0 12 21A2.5 2.5 0 0 0 14 17A2 2 0 0 1 16 15H18A3 3 0 0 0 21 12A9 9 0 0 0 12 3Z", DOT(7.5, 11), DOT(10, 7), DOT(14.5, 7), DOT(17.5, 10.5))
add("design", "droplet", "M12 3C12 3 5.5 10 5.5 14.5A6.5 6.5 0 0 0 18.5 14.5C18.5 10 12 3 12 3Z")
add("design", "sun", C(12, 12, 4), RAYS(12, 12, 7, 9.5, 8))
add("design", "moon", "M20 14.5A8.5 8.5 0 1 1 9.5 4A6.5 6.5 0 0 0 20 14.5Z")
add("design", "type", "M5 7V5H19V7M12 5V19M9 19H15")
add("design", "bold", "M7 4H13A4 4 0 0 1 13 12H7ZM7 12H14A4 4 0 0 1 14 20H7Z")
add("design", "italic", "M10 4H19M5 20H14M15 4L9 20")
add("design", "underline", "M6 4V11A6 6 0 0 0 18 11V4M4 21H20")
add("design", "align-left", "M4 6H20M4 10H14M4 14H20M4 18H14")
add("design", "align-center", "M4 6H20M7 10H17M4 14H20M7 18H17")
add("design", "align-right", "M4 6H20M10 10H20M4 14H20M10 18H20")
add("design", "battery", R(2, 7, 18, 10, 2), "M22 11V13M6 10.5H14V13.5H6Z")

# ------------------------------------------------------------------ أسماء بديلة (توافق مع سجل المحرّك القديم)
ALIASES = {
    "delete": "trash", "remove": "minus", "add": "plus", "x": "close", "cancel": "close", "ok": "check",
    "back": "arrow-left", "next": "arrow-right", "up": "arrow-up", "down": "arrow-down",
    "gear": "settings", "cog": "settings", "preferences": "settings", "person": "user", "profile": "user-circle",
    "magnifier": "search", "find": "search", "bolt": "zap", "lightning": "zap", "bin": "trash",
    "email": "mail", "chat": "message", "comment": "message", "notification": "bell", "alert": "warning",
    "danger": "error", "question": "help", "like": "thumbs-up", "love": "heart", "favorite": "star",
    "bookmarks": "bookmark", "time": "clock", "date": "calendar", "location": "map-pin", "world": "globe",
    "internet": "wifi", "picture": "image", "photo": "image", "movie": "film", "sound": "volume",
    "mute": "volume-off", "microphone": "mic", "dashboard": "layout", "stats": "chart-bar", "analytics": "chart-line",
    "reload": "refresh", "sync": "refresh", "open": "external-link", "exit": "logout", "signin": "login",
    "hamburger": "menu", "dots": "more", "kebab": "more-vertical", "apps": "grid", "pencil": "edit",
    "disk": "save", "attach": "paperclip", "idea": "lightbulb", "light-bulb": "lightbulb", "debug": "bug",
    "json": "braces", "pkg": "package", "lib": "library", "ui": "view", "window": "view", "fn": "function",
    "dark": "moon", "light": "sun", "brightness": "sun", "sparkle": "sparkles", "magic": "sparkles",
    "play-circle": "play", "show": "eye", "hide": "eye-off", "password": "key", "privacy": "shield",
}

CATEGORIES = [
    ("actions", "إجراءات وواجهة", "Actions & UI"),
    ("arrows", "أسهم وتنقّل", "Arrows & navigation"),
    ("status", "حالة وتنبيه", "Status & alerts"),
    ("security", "أمان", "Security"),
    ("users", "مستخدمون", "Users"),
    ("comms", "تواصل وشبكة", "Communication & network"),
    ("media", "وسائط", "Media"),
    ("files", "ملفات وبيانات", "Files & data"),
    ("dev", "مطوّرون وRin", "Developer & Rin"),
    ("charts", "مخططات وتخطيط", "Charts & layout"),
    ("design", "تصميم ونص", "Design & text"),
]

# أيقونات تبدو أفضل مملوءة (يستخدمها خيار filled)
FILLABLE = ["star", "heart", "play", "stop", "pause", "bookmark", "flag", "zap", "skip-next", "skip-prev",
            "fast-forward", "rewind", "moon", "droplet", "sparkles", "warning", "error", "info", "square",
            "circle", "triangle", "hexagon", "tag", "pin", "thumbs-up", "cursor", "send", "bell", "lock",
            "home", "folder", "file", "cloud", "message", "mail", "user", "shield", "database", "image"]

ICONS = I

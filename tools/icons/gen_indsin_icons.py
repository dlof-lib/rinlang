#!/usr/bin/env python3
"""
gen_indsin_icons.py — يولّد مكتبة أيقونات indsin كلها من مصدر واحد (icons_def.py):

  assets/indsin-icons/svg/<name>.svg        ملف SVG لكل أيقونة
  assets/indsin-icons/indsin-icons.svg       sprite واحد (<symbol id="ic-<name>">)
  assets/indsin-icons/icons.json             فهرس JSON (الاسم، الفئة، المسار، الأسماء البديلة)
  lib/indsinicons.og.rin                     مكتبة Rin (ic_*)
  app/src/main/cpp/indsin/rin_indsin_iconpaths.h   جدول مسارات C++ (يُضمَّن في rin_indsin_icons.h)
  app/src/main/assets/indsin/indsin_object.html    يُحقن بين علامتي ICON-VECTORS (معاينة الويب)
  assets/indsin-icons/gallery.html           معرض عرض مستقل

التشغيل من جذر المستودع:   python3 tools/icons/gen_indsin_icons.py
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import icons_def as D

OUT = os.path.join(ROOT, "assets", "indsin-icons")
NAMES = sorted(D.ICONS)
VERSION = "1.0.0"

def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)

def aliases_of(name):
    return sorted(a for a, t in D.ALIASES.items() if t == name)

def svg(name, size=24, stroke=2, color="currentColor", filled=False):
    d = D.ICONS[name][1]
    fill = color if filled else "none"
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="%s" height="%s" fill="%s" stroke="%s" '
            'stroke-width="%s" stroke-linecap="round" stroke-linejoin="round"><path d="%s"/></svg>'
            % (size, size, fill, color, stroke, d))

# ------------------------------------------------------------------ SVG + sprite + JSON
def gen_svgs():
    for n in NAMES:
        w(os.path.join(OUT, "svg", n + ".svg"), svg(n) + "\n")
    parts = ['<svg xmlns="http://www.w3.org/2000/svg" style="display:none" aria-hidden="true">']
    for n in NAMES:
        parts.append('<symbol id="ic-%s" viewBox="0 0 24 24"><path d="%s"/></symbol>' % (n, D.ICONS[n][1]))
    parts.append("</svg>\n")
    w(os.path.join(OUT, "indsin-icons.svg"), "\n".join(parts))
    idx = {"version": VERSION, "grid": 24, "stroke": 2, "count": len(NAMES),
           "categories": [{"id": c, "ar": a, "en": e} for c, a, e in D.CATEGORIES],
           "icons": [{"name": n, "category": D.ICONS[n][0], "aliases": aliases_of(n),
                      "filled": n in D.FILLABLE, "path": D.ICONS[n][1]} for n in NAMES]}
    w(os.path.join(OUT, "icons.json"), json.dumps(idx, ensure_ascii=False, indent=1) + "\n")
    # نسخة مضغوطة داخل assets التطبيق (يقرؤها IndsinIcons.kt في RinStudio)
    w(os.path.join(ROOT, "app", "src", "main", "assets", "indsin-icons", "icons.json"),
      json.dumps(idx, ensure_ascii=False, separators=(",", ":")) + "\n")

# ------------------------------------------------------------------ مكتبة Rin
RIN_HEAD = '''// ============================================================================
// lib/indsinicons.og.rin — IndsinIcons: مكتبة أيقونات متجهية (SVG) لمحرّك indsin
// ============================================================================
// %(count)d أيقونة مرسومة من الصفر على شبكة 24×24 (خط سماكته 2، أطراف دائرية)، كل أيقونة مسار SVG
// واحد. مكتوبة بالكامل بلغة Rin؛ تُولَّد البيانات من tools/icons/icons_def.py (لا تعدّلها يدوياً).
//
//   @import "lib/indsinicons.og.rin";
//
//   print ic_svg("home", nil);                                  // <svg ...> جاهز للصق في html=
//   print ic_svg("heart", {"size": 32, "color": "#e11d48", "filled": true});
//   print ic_has("gear");                                       // true  (اسم بديل لـ settings)
//   print ic_search("arrow");                                   // كل الأيقونات التي تحوي arrow
//   ic_register("my-logo", "brand", "M4 4H20V20H4Z");           // أيقونتك الخاصة
//
// الأقسام (كل الدوال بادئتها ic_):
//   1) الفهرس        ic_count · ic_names · ic_has · ic_resolve · ic_aliasesOf · ic_category
//   2) الفئات        ic_categories · ic_categoryName · ic_byCategory
//   3) البحث         ic_search
//   4) التوليد       ic_path · ic_svg · ic_svgn · ic_html · ic_dataUri · ic_sprite · ic_symbol
//   5) تكامل indsin  ic_snippet · ic_audit
//   6) التوسعة       ic_register · ic_validPath
//
// ملاحظة: دوال Rin تتطلب عدداً مطابقاً من الوسائط؛ مرّر nil لـ opts الاختيارية.
// خيارات opts: size (24) · color ("currentColor") · stroke (2) · filled (false) · title (nil) · class (nil)
// ============================================================================

let __ic_version = "%(version)s";
'''

def rin_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

RIN_BODY = r'''
// ---- 1) الفهرس ----------------------------------------------------------------------------
fun ic_version() { return __ic_version; }
fun ic_count() { return len(keys(__ic)); }
fun ic_names() { return sort(keys(__ic)); }

// ic_resolve(name) -> الاسم الأساسي (يقبل الأسماء البديلة وأي حالة أحرف) أو nil إن لم يوجد.
fun ic_resolve(name) {
    if (name == nil) { return nil; }
    let n = lower(trim(str(name)));
    if (has(__ic, n)) { return n; }
    if (has(__icAlias, n)) { return __icAlias[n]; }
    return nil;
}
fun ic_has(name) { return ic_resolve(name) != nil; }

fun ic_aliasesOf(name) {
    let n = ic_resolve(name);
    let out = [];
    if (n == nil) { return out; }
    let ks = sort(keys(__icAlias));
    let i = 0;
    while (i < len(ks)) {
        if (__icAlias[ks[i]] == n) { push(out, ks[i]); }
        i = i + 1;
    }
    return out;
}

fun ic_category(name) {
    let n = ic_resolve(name);
    if (n == nil) { return nil; }
    return __ic[n][0];
}

// ---- 2) الفئات ----------------------------------------------------------------------------
fun ic_categories() {
    let out = [];
    let i = 0;
    while (i < len(__icCats)) { push(out, __icCats[i][0]); i = i + 1; }
    return out;
}
// ic_categoryName(cat, lang) -> الاسم المعروض ("ar" أو "en")؛ يعيد المعرّف نفسه إن لم تكن الفئة معروفة.
fun ic_categoryName(cat, lang) {
    let i = 0;
    while (i < len(__icCats)) {
        if (__icCats[i][0] == cat) {
            if (lang == "en") { return __icCats[i][2]; }
            return __icCats[i][1];
        }
        i = i + 1;
    }
    return cat;
}
fun ic_byCategory(cat) {
    let out = [];
    let ks = ic_names();
    let i = 0;
    while (i < len(ks)) {
        if (__ic[ks[i]][0] == cat) { push(out, ks[i]); }
        i = i + 1;
    }
    return out;
}

// ---- 3) البحث -----------------------------------------------------------------------------
// ic_search(q) -> أسماء أساسية يحوي اسمها أو أحد أسمائها البديلة النص q (بلا حساسية لحالة الأحرف).
fun ic_search(q) {
    let out = [];
    let needle = lower(trim(str(q)));
    // مرّة واحدة على الأسماء البديلة (لا مرور كامل لكل أيقونة): الأيقونات التي أحد بدائلها يطابق
    let viaAlias = {};
    if (needle != "") {
        let aks = keys(__icAlias);
        let k = 0;
        while (k < len(aks)) {
            if (contains(aks[k], needle)) { viaAlias[__icAlias[aks[k]]] = true; }
            k = k + 1;
        }
    }
    let ks = ic_names();
    let i = 0;
    while (i < len(ks)) {
        let hit = needle == "" or contains(ks[i], needle) or contains(__ic[ks[i]][0], needle) or has(viaAlias, ks[i]);
        if (hit) { push(out, ks[i]); }
        i = i + 1;
    }
    return out;
}

// ---- 4) التوليد ---------------------------------------------------------------------------
fun __ic_opt(opts, key, fallback) {
    if (opts == nil) { return fallback; }
    if (has(opts, key)) {
        let v = opts[key];
        if (v != nil) { return v; }
    }
    return fallback;
}
fun __ic_attr(s) {
    // تهريب قيم الخصائص داخل علامات الاقتباس
    return replace(replace(replace(replace(str(s), "&", "&amp;"), "\"", "&quot;"), "<", "&lt;"), ">", "&gt;");
}

// ic_path(name) -> قيمة d للمسار ("" إن لم توجد الأيقونة).
fun ic_path(name) {
    let n = ic_resolve(name);
    if (n == nil) { return ""; }
    return __ic[n][1];
}

// ic_svg(name, opts) -> وسم <svg> كامل. الاسم المجهول يعطي أيقونة "help" (علامة استفهام مرئية)
// بدل فراغ صامت — نفس فلسفة سجل الأيقونات في المحرّك (rin_indsin_icons.h). تحقّق بـ ic_has قبلها.
fun ic_svg(name, opts) {
    let n = ic_resolve(name);
    let missing = n == nil;
    if (missing) { n = "help"; }
    let size = __ic_opt(opts, "size", 24);
    let color = __ic_opt(opts, "color", "currentColor");
    let stroke = __ic_opt(opts, "stroke", 2);
    let filled = __ic_opt(opts, "filled", false);
    let title = __ic_opt(opts, "title", nil);
    let cls = __ic_opt(opts, "class", nil);
    let fill = "none";
    if (filled) { fill = __ic_attr(color); }
    let s = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" width=\"" + str(size) + "\" height=\"" + str(size) + "\"";
    s = s + " fill=\"" + fill + "\" stroke=\"" + __ic_attr(color) + "\" stroke-width=\"" + str(stroke) + "\"";
    s = s + " stroke-linecap=\"round\" stroke-linejoin=\"round\"";
    if (cls != nil) { s = s + " class=\"" + __ic_attr(cls) + "\""; }
    if (missing) { s = s + " data-missing=\"" + __ic_attr(name) + "\""; }
    if (title != nil) { s = s + " role=\"img\" aria-label=\"" + __ic_attr(title) + "\">" + "<title>" + __ic_attr(title) + "</title>"; }
    else { s = s + " aria-hidden=\"true\">"; }
    return s + "<path d=\"" + __ic[n][1] + "\"/></svg>";
}
// ic_svgn(name, size, color) -> اختصار للحالة الشائعة.
fun ic_svgn(name, size, color) { return ic_svg(name, {"size": size, "color": color}); }

// ic_html(name, opts) -> <span> مضمَّن بحجم نصّي (1em) يرث لون الخط؛ مناسب لـ html= في WebView.
fun ic_html(name, opts) {
    let size = __ic_opt(opts, "size", 24);
    let o = {"size": size, "color": __ic_opt(opts, "color", "currentColor"), "stroke": __ic_opt(opts, "stroke", 2),
             "filled": __ic_opt(opts, "filled", false), "title": __ic_opt(opts, "title", nil), "class": nil};
    return "<span style=\"display:inline-flex;vertical-align:middle;width:" + str(size) + "px;height:" + str(size) + "px\">" + ic_svg(name, o) + "</span>";
}

// ic_dataUri(name, opts) -> رابط data: يصلح لـ src= أو background-image (اللون يجب أن يكون صريحاً لا currentColor).
fun ic_dataUri(name, opts) {
    let s = ic_svg(name, opts);
    s = replace(s, "%", "%25");
    s = replace(s, "#", "%23");
    s = replace(s, "<", "%3C");
    s = replace(s, ">", "%3E");
    s = replace(s, "\"", "'");
    return "data:image/svg+xml;charset=utf-8," + s;
}

// ic_symbol(name) -> <symbol id="ic-NAME"> واحد؛ ic_sprite(names) -> sprite لمجموعة أسماء (أو كل الأيقونات إن مرّرت nil).
fun ic_symbol(name) {
    let n = ic_resolve(name);
    if (n == nil) { return ""; }
    return "<symbol id=\"ic-" + n + "\" viewBox=\"0 0 24 24\"><path d=\"" + __ic[n][1] + "\"/></symbol>";
}
fun ic_sprite(names) {
    let list = names;
    if (list == nil) { list = ic_names(); }
    let s = "<svg xmlns=\"http://www.w3.org/2000/svg\" style=\"display:none\" aria-hidden=\"true\">";
    let i = 0;
    while (i < len(list)) { s = s + ic_symbol(list[i]); i = i + 1; }
    return s + "</svg>";
}

// ---- 5) تكامل indsin ----------------------------------------------------------------------
// ic_snippet(name, opts) -> سطر indsin جاهز:  @view.Icon icon="home" size="lg"
// opts (اختياري): size ("sm"/"lg"/رقم) · color
fun ic_snippet(name, opts) {
    let n = ic_resolve(name);
    if (n == nil) { n = str(name); }
    let s = "@view.Icon=icon_" + replace(n, "-", "_") + "\n    icon=\"" + n + "\";";
    let size = __ic_opt(opts, "size", nil);
    if (size != nil) {
        if (type(size) == "string") { s = s + "\n    size=\"" + size + "\";"; } else { s = s + "\n    size=" + str(size) + ";"; }
    }
    let color = __ic_opt(opts, "color", nil);
    if (color != nil) { s = s + "\n    color=\"" + str(color) + "\";"; }
    return s + "\n.end/view";
}

// ic_audit(names) -> {ok, known:[...], unknown:[...]}  يفحص قائمة أسماء أيقونات قبل استعمالها في واجهة.
fun ic_audit(names) {
    let known = [];
    let unknown = [];
    let i = 0;
    while (i < len(names)) {
        if (ic_has(names[i])) { push(known, ic_resolve(names[i])); } else { push(unknown, names[i]); }
        i = i + 1;
    }
    return {"ok": len(unknown) == 0, "known": known, "unknown": unknown};
}

// ---- 6) التوسعة ---------------------------------------------------------------------------
// ic_validPath(d) -> true إن كان المسار مبنياً من الأوامر المدعومة (M L H V C A Z) والأرقام فقط.
fun ic_validPath(d) {
    if (type(d) != "string" or len(d) < 3) { return false; }
    return regexTest(d, "^[MLHVCAZ0-9 .,\\-]+$") and regexTest(d, "^M");
}

// ic_register(name, category, d) -> الاسم المسجَّل. أيقونتك تدخل البحث والفئات مثل المدمجة؛
// إعادة تسجيل اسم موجود تستبدله (آخر كتابة تفوز)، كما في registerIcon في سجل المحرّك.
fun ic_register(name, category, d) {
    let n = lower(trim(str(name)));
    if (!regexTest(n, "^[a-z][a-z0-9-]{0,47}$")) { fail("ic_register: اسم غير صالح " + json.stringify(name) + " (أحرف صغيرة وأرقام و - فقط)"); }
    if (!ic_validPath(d)) { fail("ic_register: مسار SVG غير صالح للأيقونة " + n); }
    __ic[n] = [category, d];
    return n;
}
'''

def gen_rin():
    lines = []
    for n in NAMES:
        cat, d = D.ICONS[n]
        lines.append("    %s: [%s, %s]" % (rin_str(n), rin_str(cat), rin_str(d)))
    ic = "let __ic = {\n" + ",\n".join(lines) + "\n};\n"
    al = "let __icAlias = {\n" + ",\n".join("    %s: %s" % (rin_str(a), rin_str(t)) for a, t in sorted(D.ALIASES.items())) + "\n};\n"
    cats = "let __icCats = [\n" + ",\n".join("    [%s, %s, %s]" % (rin_str(c), rin_str(a), rin_str(e)) for c, a, e in D.CATEGORIES) + "\n];\n"
    head = RIN_HEAD % {"count": len(NAMES), "version": VERSION}
    w(os.path.join(ROOT, "lib", "indsinicons.og.rin"), head + "\n" + ic + "\n" + al + "\n" + cats + RIN_BODY)

# ------------------------------------------------------------------ C++
def gen_cpp():
    out = ['// indsin/rin_indsin_iconpaths.h — جدول مسارات الأيقونات المتجهية (مُولَّد: tools/icons/gen_indsin_icons.py).',
           '// لا تعدّل هذا الملف يدوياً؛ عدّل tools/icons/icons_def.py ثم أعد التوليد.',
           '// كل مسار SVG بأوامر مطلقة M L H V C A Z على شبكة 24x24 (سماكة خط 2، أطراف دائرية).',
           '#pragma once', '#include <cstddef>', '', 'namespace indsin {', '',
           'struct IconVec { const char* name; const char* category; const char* path; };',
           'struct IconAlias { const char* alias; const char* target; };', '',
           'static const IconVec kIconVecs[] = {']
    for n in NAMES:
        cat, d = D.ICONS[n]
        out.append('    {"%s", "%s", "%s"},' % (n, cat, d))
    out += ['};', 'static const size_t kIconVecCount = sizeof(kIconVecs) / sizeof(kIconVecs[0]);', '',
            'static const IconAlias kIconAliases[] = {']
    for a, t in sorted(D.ALIASES.items()):
        out.append('    {"%s", "%s"},' % (a, t))
    out += ['};', 'static const size_t kIconAliasCount = sizeof(kIconAliases) / sizeof(kIconAliases[0]);', '', '} // namespace indsin', '']
    w(os.path.join(ROOT, "app", "src", "main", "cpp", "indsin", "rin_indsin_iconpaths.h"), "\n".join(out))

# ------------------------------------------------------------------ معاينة الويب (indsin_object.html)
def js_str(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"

def patch_html():
    p = os.path.join(ROOT, "app", "src", "main", "assets", "indsin", "indsin_object.html")
    src = open(p, encoding="utf-8").read()
    vec = ["  /* ICON-VECTORS:BEGIN (مُولَّد: tools/icons/gen_indsin_icons.py — لا تعدّل يدوياً) */",
           "  var ICON_VEC = {"]
    vec += ["    %s: %s," % (js_str(n), js_str(D.ICONS[n][1])) for n in NAMES]
    vec += ["  };", "  var ICON_ALIAS = {"]
    vec += ["    %s: %s," % (js_str(a), js_str(t)) for a, t in sorted(D.ALIASES.items())]
    vec += ["  };",
            "  function vecKey(name) { name = String(name == null ? '' : name).toLowerCase(); if (hasOwn.call(ICON_VEC, name)) return name; return hasOwn.call(ICON_ALIAS, name) ? ICON_ALIAS[name] : null; }",
            "  function svgIcon(key) { return '<svg class=\"ix-svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\"><path d=\"' + ICON_VEC[key] + '\"/></svg>'; }",
            "  /* iconHtml(name): أيقونة متجهية إن وُجدت، وإلا الرمز النصي القديم (توافق خلفي كامل) */",
            "  function iconHtml(name) { var k = vecKey(name); return k ? svgIcon(k) : esc(glyph(name)); }",
            "  function registerVectorIcon(name, path) { ICON_VEC[String(name).toLowerCase()] = String(path); }",
            "  /* ICON-VECTORS:END */"]
    block = "\n".join(vec)
    if "/* ICON-VECTORS:BEGIN" in src:
        src = re.sub(r"  /\* ICON-VECTORS:BEGIN.*?/\* ICON-VECTORS:END \*/", lambda m: block, src, flags=re.S)
    else:
        # 1) استبدال esc(glyph(X)) بـ iconHtml(X) في مواضع الاستعمال *قبل* حقن الكتلة
        #    (وإلا طال الاستبدال جسم iconHtml نفسه فصار يستدعي نفسه بلا نهاية)
        out, pos = [], 0
        while True:
            k = src.find("esc(glyph(", pos)
            if k < 0: out.append(src[pos:]); break
            out.append(src[pos:k])
            depth, m = 1, k + len("esc(glyph(")
            while depth:
                ch = src[m]
                depth += (ch == "(") - (ch == ")")
                m += 1
            inner = src[k + len("esc(glyph("):m - 1]
            assert src[m] == ")", src[k:m + 20]
            out.append("iconHtml(" + inner + ")")
            pos = m + 1
        src = "".join(out)
        # 2) حقن الكتلة بعد تعريف glyph()
        anchor = "  function glyph(name) {"
        i = src.index(anchor); j = src.index("\n", i) + 1
        src = src[:j] + block + "\n" + src[j:]
    # 3) قاعدة CSS للأيقونة المتجهية (idempotent)
    css_anchor = "    '.ix-ico{display:inline-grid;place-items:center;font-size:18px;line-height:1;color:var(--ix-text)}',"
    css_rule = "    '.ix-svg{width:1em;height:1em;display:block;flex:none;overflow:visible}',"
    if css_rule not in src:
        assert css_anchor in src
        src = src.replace(css_anchor, css_anchor + "\n" + css_rule, 1)
    # 4) المدقّق: الأسماء المتجهية معروفة (لا تحذير ICON-UNKNOWN لها) — idempotent
    v_old = "a.has('icon') && ENGINE_ICONS.indexOf(String(a.get('icon'))) < 0)"
    v_new = "a.has('icon') && ENGINE_ICONS.indexOf(String(a.get('icon'))) < 0 && !vecKey(a.get('icon')))"
    if v_new not in src:
        assert v_old in src
        src = src.replace(v_old, v_new, 1)
    open(p, "w", encoding="utf-8", newline="\n").write(src)

# ------------------------------------------------------------------ المعرض
def gallery_order():
    # المعرض يجمع الأيقونات تحت عنوان كل فئة (بترتيب CATEGORIES) ثم أبجدياً داخل الفئة
    rank = {c: i for i, (c, _, _) in enumerate(D.CATEGORIES)}
    return sorted(NAMES, key=lambda n: (rank[D.ICONS[n][0]], n))

def gen_gallery():
    tpl = open(os.path.join(HERE, "gallery_template.html"), encoding="utf-8").read()
    data = {"v": VERSION, "cats": [[c, a, e] for c, a, e in D.CATEGORIES],
            "icons": [[n, D.ICONS[n][0], D.ICONS[n][1], 1 if n in D.FILLABLE else 0, aliases_of(n)] for n in gallery_order()]}
    html = tpl.replace("/*__DATA__*/null", json.dumps(data, ensure_ascii=False, separators=(",", ":")))
    w(os.path.join(OUT, "gallery.html"), html)

if __name__ == "__main__":
    gen_svgs(); gen_rin(); gen_cpp(); patch_html(); gen_gallery()
    print("generated %d icons, %d aliases" % (len(NAMES), len(D.ALIASES)))

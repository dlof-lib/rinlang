#!/usr/bin/env python3
"""
يبني حزم المحتوى القابلة للتنزيل (content-packs/) إلى:
  <out>/catalog.json                 قائمة الحزم (id, type, size, sha256, url ...)
  <out>/<id>-<sha8>.zip              كل حزمة كملف zip حتمي (نفس المدخلات => نفس الـ sha256)

الاستخدام:
  python3 scripts/build_content_packs.py --out dist/packs
  python3 scripts/build_content_packs.py --check          # تحقق فقط (تغطية مفاتيح اللغات) بلا إخراج

أنواع الحزم:
  language : يحوّل ملفات values-*/strings*.xml (string / plurals / string-array) إلى strings.json
             يقرؤه التطبيق وقت التشغيل (انظر LanguagePacks.kt). لا يدخل XML في الـ APK إطلاقاً.
  asset    : يضم ملفات المجلد كما هي.
كل حزمة تحوي pack.json (id/type/version[/language]) في جذر الـ zip.
"""
import argparse, glob, hashlib, json, os, re, sys, zipfile
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PACKS_DIR = os.path.join(ROOT, "content-packs")
DEFAULT_RES = os.path.join(ROOT, "app", "src", "main", "res", "values")
FIXED_DATE = (2024, 1, 1, 0, 0, 0)  # زمن ثابت => zip حتمي


def unescape_android(raw):
    """نفس قواعد aapt لنص <string>: تقليص المسافات خارج علامات الاقتباس ثم فك \\n \\' \\" \\uXXXX..."""
    s = raw if raw is not None else ""
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        s = s[1:-1]
    else:
        s = re.sub(r"\s+", " ", s.strip())
    out, i = [], 0
    while i < len(s):
        ch = s[i]
        if ch == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "n": out.append("\n"); i += 2
            elif n == "t": out.append("\t"); i += 2
            elif n == "u" and re.fullmatch(r"[0-9a-fA-F]{4}", s[i + 2:i + 6] or ""):
                out.append(chr(int(s[i + 2:i + 6], 16))); i += 6
            else: out.append(n); i += 2
        else:
            out.append(ch); i += 1
    return "".join(out)


def element_text(e):
    if len(e):
        raise SystemExit("'%s' يحوي وسوماً داخلية (span/xliff) وهذا غير مدعوم في حزم اللغات" % e.get("name"))
    return unescape_android(e.text)


def parse_values_dir(directory):
    strings, plurals, arrays = {}, {}, {}
    for path in sorted(glob.glob(os.path.join(directory, "*.xml"))):
        root = ET.parse(path).getroot()
        for e in root:
            name = e.get("name")
            if e.tag == "string":
                strings[name] = element_text(e)
            elif e.tag == "plurals":
                plurals[name] = {i.get("quantity"): element_text(i) for i in e}
            elif e.tag == "string-array":
                arrays[name] = [element_text(i) for i in e]
    return strings, plurals, arrays


def default_keys():
    s, p, a = parse_values_dir(DEFAULT_RES)
    return set(s), set(p), set(a)


def build_language(pack, check_only):
    src = os.path.join(PACKS_DIR, pack["source"])
    strings, plurals, arrays = parse_values_dir(src)
    ds, dp, da = default_keys()
    # صحة: كل مفتاح في الحزمة يجب أن يكون له أصل في values/ (وإلا فهو مفتاح ميت)
    dead = (set(strings) - ds) | (set(plurals) - dp) | (set(arrays) - da)
    missing = ds - set(strings)
    print("  %-10s strings=%d plurals=%d arrays=%d  | بلا أصل=%d  غير مترجم=%d"
          % (pack["id"], len(strings), len(plurals), len(arrays), len(dead), len(missing)))
    # الحزمة تحمل فقط ما له أصل (يقلل الحجم ويمنع تضارب أسماء)
    strings = {k: v for k, v in strings.items() if k in ds}
    plurals = {k: v for k, v in plurals.items() if k in dp}
    arrays = {k: v for k, v in arrays.items() if k in da}
    payload = {"strings": strings, "plurals": plurals, "arrays": arrays}
    data = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return {"strings.json": data}


def build_asset(pack):
    src = os.path.normpath(os.path.join(PACKS_DIR, pack["source"]))
    files = {}
    for dirpath, _, names in os.walk(src):
        for n in names:
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            with open(full, "rb") as f:
                files[rel] = f.read()
    if not files:
        raise SystemExit("حزمة الأصول '%s' فارغة: %s" % (pack["id"], src))
    return files


def make_zip(files):
    import io
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, FIXED_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, files[name])
    return buf.getvalue()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--base-url", default="", help="يُضاف أمام اسم الـ zip في catalog.json (افتراضياً نسبي لمكان catalog.json)")
    args = ap.parse_args()
    if not args.out and not args.check:
        ap.error("حدّد --out أو --check")

    with open(os.path.join(PACKS_DIR, "packs.json"), encoding="utf-8") as f:
        spec = json.load(f)

    catalog = {"schema": spec.get("schema", 1), "packs": []}
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    print("حزم المحتوى:")
    for pack in spec["packs"]:
        if pack["type"] == "language":
            files = build_language(pack, args.check)
        elif pack["type"] == "asset":
            files = build_asset(pack)
        else:
            raise SystemExit("نوع غير معروف: %s" % pack["type"])
        meta = {"id": pack["id"], "type": pack["type"], "version": pack["version"]}
        if pack["type"] == "language":
            meta["language"] = pack["language"]
        files["pack.json"] = json.dumps(meta, ensure_ascii=False, sort_keys=True).encode("utf-8")

        blob = make_zip(files)
        sha = hashlib.sha256(blob).hexdigest()
        fname = "%s-%s.zip" % (pack["id"], sha[:8])
        entry = {
            "id": pack["id"], "type": pack["type"], "version": pack["version"],
            "title": pack["title"], "description": pack.get("description", ""),
            "sizeBytes": len(blob), "installedBytes": sum(len(v) for v in files.values()),
            "sha256": sha, "url": (args.base_url.rstrip("/") + "/" if args.base_url else "") + fname,
        }
        if pack["type"] == "language":
            entry["language"] = pack["language"]
        catalog["packs"].append(entry)
        print("  -> %-12s %8d B  sha256=%s" % (pack["id"], len(blob), sha[:16]))
        if args.out:
            with open(os.path.join(args.out, fname), "wb") as f:
                f.write(blob)
    if args.out:
        with open(os.path.join(args.out, "catalog.json"), "w", encoding="utf-8") as f:
            json.dump(catalog, f, ensure_ascii=False, indent=2)
        print("كُتب %s" % os.path.join(args.out, "catalog.json"))


if __name__ == "__main__":
    main()

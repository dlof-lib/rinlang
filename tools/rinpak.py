#!/usr/bin/env python3
"""rinpak — مرجع بايثون لصيغة RinPak (.rinpak). المواصفة: docs/rinpak-format.md

  rinpak.py keygen <key.pem>
  rinpak.py pack   <project_dir> [-o out.rinpak] [--name N] [--type T] [--sign key.pem]
                   [--version V --author A --license L --description D --permission P --dependency D]
  rinpak.py info   <file.rinpak> [--app-version V]
  rinpak.py verify <file.rinpak> [--require-signature] [--trust KEYID ...] [--max-risk low|medium|high]
  rinpak.py unpack <file.rinpak> <dest_dir>
  rinpak.py meta   <project_dir> [--set key=value ...]      # قراءة/تعديل project.og.urin

`pack` يقرأ project.og.urin من المجلد تلقائياً (النوع/الاسم/الإصدار/...)؛ الخيارات الصريحة تتقدّم عليه.

يطابق منطق app/.../RinPak.kt بايتاً ببايت (الترتيب، البصمات، قواعد الرفض).
"""
import argparse, base64, hashlib, io, json, os, re, sys, zipfile, datetime

MIME = "application/vnd.rin.pak"
FORMAT, SPEC = "rinpak", 1
MANIFEST, SIGFILE, ICON, ROOT, ENTRY = "META-INF/manifest.json", "META-INF/signature.json", "META-INF/icon.png", "content/", "main.rin"
SIGN_DOMAIN = b"RinPak-v1\0"
MAX_FILE = 64 * 1024 * 1024
MAX_ENTRIES, MAX_BYTES = 10_000, 256 * 1024 * 1024
STORED_EXT = {"png","jpg","jpeg","webp","gif","heic","mp3","aac","m4a","ogg","opus","flac","mp4","m4v",
              "mkv","webm","mov","zip","jar","apk","gz","xz","7z","rar","woff","woff2"}
FIXED = (1980, 1, 1, 0, 0, 0)
MAX_ICON = 512 * 1024
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
NATIVE_EXT = {"so", "dex", "exe", "dll", "dylib", "msi", "scr"}
PACKAGE_EXT = {"apk", "jar", "aab", "xapk"}
SCRIPT_EXT = {"sh", "bat", "cmd", "ps1", "vbs", "command"}
SENSITIVE_PERMS = {"exec", "shell", "install", "sms", "contacts", "location", "camera", "microphone",
                   "accessibility", "overlay", "files-all", "phone", "calendar"}
BIDI = set("\u202a\u202b\u202c\u202d\u202e\u2066\u2067\u2068\u2069")
LEVEL = {"info": 0, "medium": 1, "high": 2}

class RinPakError(Exception): pass

def rb(path):
    with open(path, "rb") as f: return f.read()

def safe_path(p):
    if not p or "\\" in p or ":" in p or "\0" in p or p.startswith("/"): return None
    segs = p.split("/")
    if any(s in ("", ".", "..") for s in segs): return None
    if any(s.endswith((".", " ")) or any(ord(c) < 32 or c in '<>"|?*' for c in s) for s in segs): return None
    return p

def check_collisions(paths):
    seen = {}
    for p in paths:
        prev = seen.setdefault(p.lower(), p)
        if prev != p: raise RinPakError(f"name collision (case): {prev} ~ {p}")

def key_id(pub): return hashlib.sha256(pub).hexdigest()[:16]

def keygen(path):
    """يولّد مفتاح Ed25519 خاصاً (PEM) ويعيد keyId."""
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives import serialization as S
    k = Ed25519PrivateKey.generate()
    with open(path, "wb") as f: f.write(k.private_bytes(S.Encoding.PEM, S.PrivateFormat.PKCS8, S.NoEncryption()))
    os.chmod(path, 0o600)
    return key_id(k.public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw))

def _sign_json(key_path, manifest_bytes):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives import serialization as S
    k = S.load_pem_private_key(rb(key_path), None)
    pub = k.public_key().public_bytes(S.Encoding.Raw, S.PublicFormat.Raw)
    return {"algorithm": "Ed25519", "signs": MANIFEST, "keyId": key_id(pub),
            "publicKey": base64.b64encode(pub).decode(), "signature": base64.b64encode(k.sign(SIGN_DOMAIN + manifest_bytes)).decode()}

def verify_signature(raw, manifest_bytes):
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    from cryptography.exceptions import InvalidSignature
    try:
        o = json.loads(raw)
        if o.get("algorithm") != "Ed25519" or o.get("signs") != MANIFEST: raise RinPakError("unsupported signature algorithm/target")
        pub, sig = base64.b64decode(o["publicKey"], validate=True), base64.b64decode(o["signature"], validate=True)
        if len(pub) != 32 or len(sig) != 64: raise RinPakError("bad key/signature size")
        if str(o.get("keyId", "")).lower() != key_id(pub): raise RinPakError("keyId does not match public key")
        Ed25519PublicKey.from_public_bytes(pub).verify(sig, SIGN_DOMAIN + manifest_bytes)
        return {"keyId": key_id(pub), "publicKey": o["publicKey"]}
    except RinPakError: raise
    except InvalidSignature: raise RinPakError("invalid digital signature - package was modified after signing")
    except Exception: raise RinPakError("could not verify digital signature")

def root_digest(files):
    h = hashlib.sha256()
    for f in sorted(files, key=lambda f: f["path"].encode()):
        h.update(f'{f["path"]}\0{f["size"]}\0{f["sha256"].lower()}\n'.encode())
    return h.hexdigest()


# ───────────────────────── الإصدارات والتحليل الأمني ─────────────────────────

def parse_version(v):
    """يحوّل "1.2.3-beta" إلى (1, 2, 3) — الأجزاء غير الرقمية تُهمَل."""
    out = []
    for part in str(v or "").split("."):
        m = re.match(r"\d+", part.strip())
        out.append(int(m.group()) if m else 0)
    while out and out[-1] == 0: out.pop()
    return tuple(out)

def version_gt(a, b): return parse_version(a) > parse_version(b)

def make_id(name):
    """معرّف حزمة ثابت من الاسم: [a-z0-9-]؛ للأسماء غير اللاتينية يُشتق من SHA-256."""
    s = re.sub(r"[^a-z0-9]+", "-", (name or "").lower()).strip("-")[:48].strip("-")
    return s or "project-" + hashlib.sha256((name or "").encode()).hexdigest()[:8]

def analyze(m, app_version=None):
    """تحليل أمني للمانيفست (بلا فك الملفات). يعيد {risk, incompatible, findings[{level,code,detail}]}."""
    f = []
    def add(level, code, detail=""): f.append({"level": level, "code": code, "detail": detail})
    files = (m.get("integrity") or {}).get("files", [])
    by = lambda exts: [x["path"] for x in files if x["path"].rsplit(".", 1)[-1].lower() in exts and "." in x["path"]]
    if by(NATIVE_EXT): add("high", "NATIVE_CODE", ", ".join(by(NATIVE_EXT)[:5]))
    if by(PACKAGE_EXT): add("medium", "EMBEDDED_PACKAGE", ", ".join(by(PACKAGE_EXT)[:5]))
    if by(SCRIPT_EXT): add("medium", "SHELL_SCRIPT", ", ".join(by(SCRIPT_EXT)[:5]))
    spoof = [x["path"] for x in files if any(c in BIDI for c in x["path"])]
    if spoof: add("high", "BIDI_SPOOF", spoof[0])
    sens = sorted(p for p in (m.get("permissions") or []) if str(p).lower() in SENSITIVE_PERMS)
    if sens: add("medium", "SENSITIVE_PERMISSIONS", ", ".join(sens))
    elif m.get("permissions"): add("info", "PERMISSIONS", ", ".join(m["permissions"][:8]))
    if not m.get("signature"): add("info", "UNSIGNED")
    if (m.get("stats") or {}).get("bytes", 0) > 128 * 1024 * 1024: add("info", "LARGE_PACKAGE")
    incompatible = bool(app_version and m.get("minRinVersion") and version_gt(m["minRinVersion"], app_version))
    if incompatible: add("high", "NEWER_RIN_REQUIRED", m["minRinVersion"])
    top = max((LEVEL[x["level"]] for x in f), default=0)
    return {"risk": ["low", "medium", "high"][top], "incompatible": incompatible, "findings": f}

# ───────────────────────── project.og.urin (حاوية البيانات الوصفية للمشروع) ─────────────────────────
URIN = "project.og.urin"
URIN_SCHEMA = 2
URIN_FIELDS = ("type", "id", "name", "version", "description", "author", "license", "tags", "created",
               "modified", "minRinVersion", "permissions", "dependencies", "icon")
URIN_LISTS = {"tags": (16, 32), "permissions": (64, 64), "dependencies": (64, 128)}
URIN_LIMITS = {"name": 64, "id": 64, "version": 32, "description": 1000, "author": 128, "license": 64,
               "icon": 256, "minRinVersion": 32}
_URIN_LINE = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:"((?:[^"\\]|\\.)*)"|([^;\s"]+))\s*;?\s*$')
_ESC = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\"}

def urin_escape(v): return v.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
def urin_unescape(v): return re.sub(r"\\(.)", lambda m: _ESC.get(m.group(1), m.group(1)), v)
def _split_list(v, max_items, max_len):
    return [x.strip()[:max_len] for x in v.split(",") if x.strip()][:max_items]

def urin_parse(text):
    """يقرأ project.og.urin (الصيغة الجديدة @container، أو الصيغة الخام القديمة type=xxx).
    يعيد قاموساً: الحقول المعروفة بقيمها (القوائم list)، والمجهولة في "extra" لتُحفظ عند إعادة الكتابة."""
    meta = {"schema": 1, "extra": {}}
    for line in text.splitlines():
        t = line.strip()
        if not t or t.startswith(("#", "//", "@container", ".end")): continue
        m = _URIN_LINE.match(line)
        if not m: continue
        key, val = m.group(1), (urin_unescape(m.group(2)) if m.group(2) is not None else m.group(3))
        if key == "schema": meta["schema"] = int(val) if val.isdigit() else 1
        elif key in URIN_LISTS: meta[key] = _split_list(val, *URIN_LISTS[key])
        elif key in URIN_FIELDS: meta[key] = val[: URIN_LIMITS.get(key, 64)]
        elif len(meta["extra"]) < 32: meta["extra"][key] = val[:512]
    if not re.fullmatch(r"[A-Za-z0-9_]+", meta.get("type", "") or ""): meta["type"] = "free"
    return meta

def urin_render(meta):
    out = ["# بيانات وصفية لمشروع Rin — يُدار تلقائياً من التطبيق، يمكن تعديله بحذر",
           "// الحقول: النوع، المعرّف، الإصدار، المؤلف، الرخصة، الوسوم، الصلاحيات...",
           "@container=ProjectMeta", f"    schema={URIN_SCHEMA};", f'    type="{meta.get("type", "free")}";']
    for k in URIN_FIELDS[1:]:
        v = meta.get(k)
        if not v: continue
        if k in URIN_LISTS: v = ",".join(v)
        out.append(f'    {k}="{urin_escape(v)}";')
    for k, v in sorted((meta.get("extra") or {}).items()): out.append(f'    {k}="{urin_escape(v)}";')
    out.append(".end/container")
    return "\n".join(out) + "\n"

def urin_read(project_dir):
    p = os.path.join(project_dir, URIN)
    if not os.path.isfile(p) or os.path.getsize(p) > 64 * 1024: return None
    return urin_parse(open(p, encoding="utf-8", errors="replace").read())

def urin_write(project_dir, meta):
    with open(os.path.join(project_dir, URIN), "w", encoding="utf-8", newline="\n") as f: f.write(urin_render(meta))

def valid_icon(project_dir, rel):
    """مسار أيقونة المشروع (PNG ≤ 512KB داخل المشروع) أو None."""
    if not rel or safe_path(rel) != rel or rel.split("/")[0].startswith("."): return None
    full = os.path.join(project_dir, *rel.split("/"))
    if not os.path.isfile(full) or os.path.islink(full) or os.path.getsize(full) > MAX_ICON: return None
    return full if rb(full)[:8] == b"\x89PNG\r\n\x1a\n" else None

def pack(src, out, name=None, type_="free", icon=None, generator="rinpak.py", created=None, meta=None, sign_key=None):
    src = os.path.abspath(src); items = []
    for d, dirs, fs in os.walk(src):
        dirs[:] = [x for x in dirs if not x.startswith(".")]
        for f in fs:
            if os.path.islink(os.path.join(d, f)): continue
            rel = os.path.relpath(os.path.join(d, f), src).replace(os.sep, "/")
            if any(s.startswith(".") for s in rel.split("/")) or rel == "rin.project.json": continue
            if not safe_path(rel): raise RinPakError(f"unsafe path: {rel}")
            items.append((rel, os.path.join(d, f)))
    items.sort(key=lambda t: t[0].encode())
    if ENTRY not in [r for r, _ in items]: raise RinPakError(f"project lacks {ENTRY}")
    check_collisions([r for r, _ in items])
    files = []
    for rel, full in items:
        data = rb(full)
        if len(data) > MAX_FILE: raise RinPakError(f"file too large: {rel}")
        files.append({"path": rel, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    if sum(f["size"] for f in files) > MAX_BYTES: raise RinPakError("project too large")
    icon_bytes = None
    if icon:
        icon_bytes = rb(icon) if isinstance(icon, str) else icon
        if icon_bytes[:8] != b"\x89PNG\r\n\x1a\n": raise RinPakError("icon must be a PNG")
        if len(icon_bytes) > MAX_ICON: raise RinPakError("icon too large (max 512KB)")
    meta = dict(meta or {})
    if meta.get("id") and not ID_RE.match(meta["id"]): raise RinPakError(f"invalid package id: {meta['id']}")
    for k, (n, ln) in {"tags": (16, 32), "permissions": (64, 64), "dependencies": (64, 128)}.items():
        if meta.get(k): meta[k] = [str(x)[:ln] for x in meta[k]][:n]
    pname = name or os.path.basename(src)
    man = {"format": FORMAT, "specVersion": SPEC, "name": pname, "type": type_,
           "entry": ENTRY, "contentRoot": ROOT,
           "created": created or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "generator": {"name": generator, "version": "1.0.0"},
           "icon": {"path": ICON, "size": len(icon_bytes), "sha256": hashlib.sha256(icon_bytes).hexdigest()} if icon_bytes else None,
           **{k: v for k, v in meta.items() if v},
           "stats": {"files": len(files), "bytes": sum(f["size"] for f in files)},
           "integrity": {"algorithm": "SHA-256", "root": root_digest(files), "files": files}}
    with zipfile.ZipFile(out, "w") as z:
        def put(n, data, method):
            zi = zipfile.ZipInfo(n, FIXED); zi.compress_type = method; zi.external_attr = 0o644 << 16
            z.writestr(zi, data)
        put("mimetype", MIME.encode(), zipfile.ZIP_STORED)
        mbytes = json.dumps(man, indent=2, ensure_ascii=False).encode()
        put(MANIFEST, mbytes, zipfile.ZIP_DEFLATED)
        if sign_key: put(SIGFILE, json.dumps(_sign_json(sign_key, mbytes), indent=2).encode(), zipfile.ZIP_DEFLATED)
        if icon_bytes: put(ICON, icon_bytes, zipfile.ZIP_STORED)
        for rel, full in items:
            ext = rel.rsplit(".", 1)[-1].lower() if "." in rel else ""
            put(ROOT + rel, rb(full), zipfile.ZIP_STORED if ext in STORED_EXT else zipfile.ZIP_DEFLATED)
    return man

def read(path, dest=None):
    """يتحقق من كل شيء؛ إن مُرّر dest يفك الملفات إليه. يرجع المانيفست."""
    with zipfile.ZipFile(path) as z:
        infos = z.infolist()
        if len(infos) > MAX_ENTRIES + 3: raise RinPakError("too many entries")
        if not infos or infos[0].filename != "mimetype" or z.read("mimetype") != MIME.encode():
            raise RinPakError("not a RinPak: first entry must be 'mimetype'")
        if len(infos) < 2 or infos[1].filename != MANIFEST: raise RinPakError("second entry must be " + MANIFEST)
        mbytes = z.read(MANIFEST)
        try: m = json.loads(mbytes)
        except Exception: raise RinPakError("manifest is not valid JSON")
        if m.get("format") != FORMAT: raise RinPakError("wrong format")
        if m.get("specVersion") != SPEC: raise RinPakError(f"unsupported specVersion {m.get('specVersion')}")
        if m.get("entry", ENTRY) != ENTRY or m.get("contentRoot", ROOT) != ROOT: raise RinPakError("unsupported entry/contentRoot")
        integ = m.get("integrity") or {}
        if integ.get("algorithm") != "SHA-256": raise RinPakError("unsupported algorithm")
        files = {}
        for f in integ.get("files", []):
            p = f.get("path")
            if safe_path(p) != p: raise RinPakError(f"unsafe path in manifest: {p}")
            if p in files: raise RinPakError(f"duplicate path in manifest: {p}")
            if not isinstance(f.get("size"), int) or f["size"] < 0 or len(str(f.get("sha256", ""))) != 64:
                raise RinPakError(f"bad file record: {p}")
            if f["size"] > MAX_FILE: raise RinPakError(f"file too large: {p}")
            files[p] = f
        if ENTRY not in files: raise RinPakError(f"manifest lacks {ENTRY}")
        check_collisions(list(files))
        if sum(f["size"] for f in files.values()) > MAX_BYTES: raise RinPakError("project too large")
        if str(integ.get("root", "")).lower() != root_digest(list(files.values())): raise RinPakError("root digest mismatch")
        icon_decl = m.get("icon")
        if icon_decl is not None and not (isinstance(icon_decl, dict) and icon_decl.get("path") == ICON
                                          and isinstance(icon_decl.get("size"), int)
                                          and len(str(icon_decl.get("sha256", ""))) == 64):
            raise RinPakError("bad icon record")
        if icon_decl and icon_decl["size"] > MAX_ICON: raise RinPakError("icon too large")
        seen, total, sig, icon_bytes, icon_idx = set(), 0, None, None, None
        for idx, zi in enumerate(infos[2:], start=3):
            n = zi.filename
            if n == SIGFILE:
                if idx != 3 or sig: raise RinPakError("signature entry in wrong position")
                sig = verify_signature(z.read(zi), mbytes); continue
            if n == ICON:
                if not icon_decl: raise RinPakError("icon entry not declared in manifest")
                if idx != (4 if sig else 3): raise RinPakError("icon entry in wrong position")
                icon_bytes = z.read(zi)
                if len(icon_bytes) != icon_decl["size"] or hashlib.sha256(icon_bytes).hexdigest() != icon_decl["sha256"].lower():
                    raise RinPakError("icon SHA-256 mismatch")
                continue
            if not n.startswith(ROOT): raise RinPakError(f"unexpected entry outside {ROOT}: {n}")
            if n.endswith("/"): continue
            rel = n[len(ROOT):]
            if safe_path(rel) != rel: raise RinPakError(f"unsafe path: {n}")
            if rel not in files: raise RinPakError(f"file not in manifest: {rel}")
            if rel in seen: raise RinPakError(f"duplicate entry: {rel}")
            seen.add(rel)
            data = z.read(zi); total += len(data)
            if total > MAX_BYTES: raise RinPakError("content too large")
            if len(data) != files[rel]["size"]: raise RinPakError(f"size mismatch: {rel}")
            if hashlib.sha256(data).hexdigest() != files[rel]["sha256"].lower(): raise RinPakError(f"SHA-256 mismatch: {rel}")
            if dest:
                out = os.path.join(dest, *rel.split("/")); os.makedirs(os.path.dirname(out), exist_ok=True)
                with open(out, "wb") as fh: fh.write(data)
        missing = set(files) - seen
        if missing: raise RinPakError("missing files: " + ", ".join(sorted(missing)[:3]))
        if icon_decl and icon_bytes is None: raise RinPakError("icon declared but missing")
    m["signature"] = sig
    m["_icon"] = icon_bytes
    return m

def main(argv=None):
    ap = argparse.ArgumentParser(prog="rinpak"); sp = ap.add_subparsers(dest="cmd", required=True)
    a = sp.add_parser("pack"); a.add_argument("dir"); a.add_argument("-o"); a.add_argument("--name"); a.add_argument("--type"); a.add_argument("--icon"); a.add_argument("--sign", help="مفتاح Ed25519 خاص (PEM)")
    for k in ("version", "description", "author", "license", "minRinVersion", "id"): a.add_argument("--" + k)
    a.add_argument("--permission", action="append"); a.add_argument("--dependency", action="append"); a.add_argument("--tag", action="append")
    sp.add_parser("keygen").add_argument("out")
    i = sp.add_parser("info"); i.add_argument("file"); i.add_argument("--app-version")
    v = sp.add_parser("verify"); v.add_argument("file"); v.add_argument("--require-signature", action="store_true")
    v.add_argument("--trust", action="append", default=[], help="keyId موثوق؛ إن مُرِّر لا يُقبل غيره")
    v.add_argument("--max-risk", choices=["low", "medium", "high"], default="high"); v.add_argument("--app-version")
    u = sp.add_parser("unpack"); u.add_argument("file"); u.add_argument("dest")
    mt = sp.add_parser("meta"); mt.add_argument("dir"); mt.add_argument("--set", action="append", default=[], metavar="KEY=VALUE")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "pack":
            u_ = urin_read(args.dir) or {}
            pick = lambda cli, key, dflt=None: cli if cli else (u_.get(key) or dflt)
            name = args.name or u_.get("name") or os.path.basename(os.path.abspath(args.dir))
            meta = {"id": pick(args.id, "id") or make_id(name), "version": pick(args.version, "version"),
                    "description": pick(args.description, "description"), "author": pick(args.author, "author"),
                    "license": pick(args.license, "license"), "minRinVersion": pick(args.minRinVersion, "minRinVersion"),
                    "permissions": args.permission or u_.get("permissions"), "dependencies": args.dependency or u_.get("dependencies"),
                    "tags": args.tag or u_.get("tags")}
            icon = args.icon or valid_icon(args.dir, u_.get("icon"))
            out = args.o or name + ".rinpak"
            m = pack(args.dir, out, name, args.type or u_.get("type") or "free", icon, meta=meta, sign_key=args.sign)
            print(f"packed {m['stats']['files']} files ({m['stats']['bytes']} bytes) -> {out}")
        elif args.cmd == "keygen":
            print("keyId:", keygen(args.out), "->", args.out)
        elif args.cmd == "unpack":
            os.makedirs(args.dest, exist_ok=True); m = read(args.file, args.dest); print(f"unpacked {m['name']} -> {args.dest}")
        elif args.cmd == "meta":
            cur = urin_read(args.dir) or {"schema": URIN_SCHEMA, "type": "free", "extra": {}}
            for kv in args.set:
                k, _, val = kv.partition("=")
                if k in URIN_LISTS: cur[k] = _split_list(val, *URIN_LISTS[k])
                elif k in URIN_FIELDS: cur[k] = val[: URIN_LIMITS.get(k, 64)]
                else: raise RinPakError(f"unknown key: {k}")
            if args.set:
                if cur.get("id") and not ID_RE.match(cur["id"]): raise RinPakError(f"invalid id: {cur['id']}")
                cur["modified"] = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"); urin_write(args.dir, cur)
            print(json.dumps(cur, indent=2, ensure_ascii=False))
        else:
            m = read(args.file); an = analyze(m, getattr(args, "app_version", None)); sg = m["signature"]
            if args.cmd == "verify":
                if args.require_signature and not sg: print("ERROR: unsigned package", file=sys.stderr); return 2
                if args.trust and (not sg or sg["keyId"] not in args.trust): print("ERROR: signer not trusted", file=sys.stderr); return 2
                if LEVEL[an["risk"] if an["risk"] != "low" else "info"] > LEVEL[args.max_risk if args.max_risk != "low" else "info"]:
                    print("ERROR: risk", an["risk"], "exceeds", args.max_risk, file=sys.stderr); return 2
                if an["incompatible"]: print("ERROR: requires newer Rin", m["minRinVersion"], file=sys.stderr); return 2
                print("OK: all", m["stats"]["files"], "files verified;", f"signed by key {sg['keyId']}" if sg else "UNSIGNED", f"; risk={an['risk']}")
            else:
                print(json.dumps({k: v for k, v in m.items() if k not in ("integrity", "_icon")} | {"root": m["integrity"]["root"], "analysis": an,
                                  "hasIcon": m["_icon"] is not None}, indent=2, ensure_ascii=False))
    except (RinPakError, zipfile.BadZipFile) as e:
        print("ERROR:", e, file=sys.stderr); return 1
    return 0

if __name__ == "__main__": sys.exit(main())

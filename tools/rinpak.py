#!/usr/bin/env python3
"""rinpak — مرجع بايثون لصيغة RinPak (.rinpak). المواصفة: docs/rinpak-format.md

  rinpak.py keygen <key.pem>
  rinpak.py pack   <project_dir> [-o out.rinpak] [--name N] [--type T] [--sign key.pem]
                   [--version V --author A --license L --description D --permission P --dependency D]
  rinpak.py info   <file.rinpak>
  rinpak.py verify <file.rinpak>
  rinpak.py unpack <file.rinpak> <dest_dir>

يطابق منطق app/.../RinPak.kt بايتاً ببايت (الترتيب، البصمات، قواعد الرفض).
"""
import argparse, base64, hashlib, io, json, os, sys, zipfile, datetime

MIME = "application/vnd.rin.pak"
FORMAT, SPEC = "rinpak", 1
MANIFEST, SIGFILE, ICON, ROOT, ENTRY = "META-INF/manifest.json", "META-INF/signature.json", "META-INF/icon.png", "content/", "main.rin"
SIGN_DOMAIN = b"RinPak-v1\0"
MAX_FILE = 64 * 1024 * 1024
MAX_ENTRIES, MAX_BYTES = 10_000, 256 * 1024 * 1024
STORED_EXT = {"png","jpg","jpeg","webp","gif","heic","mp3","aac","m4a","ogg","opus","flac","mp4","m4v",
              "mkv","webm","mov","zip","jar","apk","gz","xz","7z","rar","woff","woff2"}
FIXED = (1980, 1, 1, 0, 0, 0)

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
    man = {"format": FORMAT, "specVersion": SPEC, "name": name or os.path.basename(src), "type": type_,
           "entry": ENTRY, "contentRoot": ROOT,
           "created": created or datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "generator": {"name": generator, "version": "1.0.0"}, "icon": ICON if icon else None,
           **{k: v for k, v in (meta or {}).items() if v},
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
        if icon: put(ICON, rb(icon), zipfile.ZIP_STORED)
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
        seen, total, sig = set(), 0, None
        for idx, zi in enumerate(infos[2:], start=3):
            n = zi.filename
            if n == SIGFILE:
                if idx != 3 or sig: raise RinPakError("signature entry in wrong position")
                sig = verify_signature(z.read(zi), mbytes); continue
            if n == ICON: continue
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
    m["signature"] = sig
    return m

def main(argv=None):
    ap = argparse.ArgumentParser(prog="rinpak"); sp = ap.add_subparsers(dest="cmd", required=True)
    a = sp.add_parser("pack"); a.add_argument("dir"); a.add_argument("-o"); a.add_argument("--name"); a.add_argument("--type", default="free"); a.add_argument("--icon"); a.add_argument("--sign", help="مفتاح Ed25519 خاص (PEM)")
    for k in ("version", "description", "author", "license", "minRinVersion"): a.add_argument("--" + k)
    a.add_argument("--permission", action="append"); a.add_argument("--dependency", action="append")
    sp.add_parser("keygen").add_argument("out")
    for c in ("info", "verify"): sp.add_parser(c).add_argument("file")
    u = sp.add_parser("unpack"); u.add_argument("file"); u.add_argument("dest")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "pack":
            out = args.o or (args.name or os.path.basename(os.path.abspath(args.dir))) + ".rinpak"
            meta = {k: getattr(args, k) for k in ("version", "description", "author", "license", "minRinVersion")}
            meta["permissions"], meta["dependencies"] = args.permission, args.dependency
            m = pack(args.dir, out, args.name, args.type, args.icon, meta=meta, sign_key=args.sign)
            print(f"packed {m['stats']['files']} files ({m['stats']['bytes']} bytes) -> {out}")
        elif args.cmd == "keygen":
            print("keyId:", keygen(args.out), "->", args.out)
        elif args.cmd == "unpack":
            os.makedirs(args.dest, exist_ok=True); m = read(args.file, args.dest); print(f"unpacked {m['name']} -> {args.dest}")
        else:
            m = read(args.file)
            if args.cmd == "verify":
                sg = m["signature"]
                print("OK: all", m["stats"]["files"], "files verified;", f"signed by key {sg['keyId']}" if sg else "UNSIGNED")
            else: print(json.dumps({k: v for k, v in m.items() if k != "integrity"} | {"root": m["integrity"]["root"]}, indent=2, ensure_ascii=False))
    except (RinPakError, zipfile.BadZipFile) as e:
        print("ERROR:", e, file=sys.stderr); return 1
    return 0

if __name__ == "__main__": sys.exit(main())

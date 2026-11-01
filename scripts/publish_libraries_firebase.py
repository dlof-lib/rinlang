#!/usr/bin/env python3
"""ينشر lib/*.og.rin إلى Firebase Realtime Database تحت /libraries/<key>.

GitHub (lib/) هو المصدر الأصلي؛ Firebase مجرد Registry/نسخة منشورة للويب.
النشر idempotent: المكتبة الموجودة تُحدَّث (update) ولا يُنشأ لها سجل مكرر، وعدّادات
likeCount/downloadCount وعُقد likes/downloaders لا تُمَسّ أبداً.

الاستخدام:
  python3 scripts/publish_libraries_firebase.py --dry-run            # بلا شبكة، يطبع ملخص ما سيُرفع
  python3 scripts/publish_libraries_firebase.py --dry-run --out /tmp/libs.json
  GOOGLE_APPLICATION_CREDENTIALS=/secure/path/key.json \\
  python3 scripts/publish_libraries_firebase.py                      # نشر فعلي (pip install firebase-admin)
  ... --prune                                                        # يحذف سجلات رسمية لم يعد لها ملف في lib/

الأمان: مفتاح الخدمة (service account JSON) يبقى خارج المستودع والموقع؛ الاعتماد على
متغير البيئة أو --credentials فقط. لا شيء من هذا الملف يُنشر إلى GitHub Pages.
"""
import argparse
import json
import os
import re
import sys
import time
from pathlib import Path

DEFAULT_DB_URL = "https://dlof-massage-default-rtdb.firebaseio.com/"
ROOT = Path(__file__).resolve().parent.parent
SUFFIX = ".og.rin"


def key_for(file_name: str) -> str:
    """math.og.rin -> math_og_rin (Firebase يمنع . $ # [ ] / في المفاتيح)."""
    return re.sub(r"[.$#\[\]/]", "_", file_name)


def extract_version(text: str) -> str:
    head = "\n".join(text.splitlines()[:60])
    m = re.search(r"//.*?@version[ \t:]+v?(\d+\.\d+\.\d+[\w.\-]*)", head)
    return m.group(1) if m else "1.0.0"


def extract_description(text: str, stem: str) -> str:
    for line in text.splitlines()[:40]:
        s = line.strip()
        if not s.startswith("//"):
            if s:
                break
            continue
        s = s.lstrip("/").strip(" =-\u2500\u2550*")
        # "lib/math.og.rin — وصف ..." أو "Rin CSV Pro — وصف ..." => نأخذ ما بعد الشرطة الطويلة
        if "\u2014" in s:
            s = s.split("\u2014", 1)[1].strip()
        if len(s) >= 8 and not re.fullmatch(r"[=\-\u2500\u2550 ]+", s):
            return s[:240]
    return f"Official Rin library: {stem}"


def build_record(path: Path, now_ms: int) -> tuple:
    raw = path.read_bytes()
    text = raw.decode("utf-8")
    file_name = path.name
    stem = file_name[: -len(SUFFIX)]
    return key_for(file_name), {
        "name": stem,
        "fileName": file_name,
        "version": extract_version(text),
        "description": extract_description(text, stem),
        "content": text,
        "sizeBytes": len(raw),
        "updatedAt": now_ms,
        "type": "official",
        "source": "github",
        "category": "Official",
    }


def collect(lib_dir: Path) -> dict:
    now_ms = int(time.time() * 1000)
    out = {}
    for p in sorted(lib_dir.glob("*" + SUFFIX)):
        k, rec = build_record(p, now_ms)
        if k in out:
            sys.exit(f"duplicate key {k}")
        out[k] = rec
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lib-dir", default=str(ROOT / "lib"))
    ap.add_argument("--db-url", default=os.environ.get("FIREBASE_DATABASE_URL", DEFAULT_DB_URL))
    ap.add_argument("--credentials", default=os.environ.get("GOOGLE_APPLICATION_CREDENTIALS"))
    ap.add_argument("--dry-run", action="store_true", help="لا اتصال بالشبكة؛ يطبع ما سيُرفع")
    ap.add_argument("--out", help="مع --dry-run: يكتب JSON الكامل لهذا الملف")
    ap.add_argument("--prune", action="store_true", help="يحذف السجلات الرسمية التي اختفى ملفها من lib/")
    args = ap.parse_args()

    records = collect(Path(args.lib_dir))
    if not records:
        sys.exit(f"no *{SUFFIX} files in {args.lib_dir}")

    if args.dry_run:
        for k, r in records.items():
            print(f"{k:28} v{r['version']:8} {r['sizeBytes']:>7} B  {r['description'][:70]}")
        print(f"\n{len(records)} libraries (dry-run, nothing uploaded)")
        if args.out:
            Path(args.out).write_text(json.dumps(records, ensure_ascii=False, indent=1), encoding="utf-8")
        return 0

    if not args.credentials or not Path(args.credentials).is_file():
        sys.exit("set GOOGLE_APPLICATION_CREDENTIALS (or --credentials) to a service-account JSON kept OUTSIDE the repo/site")
    try:
        import firebase_admin
        from firebase_admin import credentials, db
    except ImportError:
        sys.exit("pip install firebase-admin")

    firebase_admin.initialize_app(credentials.Certificate(args.credentials), {"databaseURL": args.db_url})
    root = db.reference("libraries")
    existing = set((root.get(shallow=True) or {}).keys())

    created = updated = 0
    for k, rec in records.items():
        ref = root.child(k)
        if k in existing:
            ref.update(rec)            # لا يلمس likeCount/likes/downloadCount/downloaders
            updated += 1
        else:
            ref.set(dict(rec, likeCount=0, downloadCount=0))
            created += 1
        print(("+ " if k not in existing else "~ ") + k)

    pruned = 0
    if args.prune:
        for k in existing - set(records):
            node = root.child(k).get(shallow=False) or {}
            if node.get("type") == "official" and node.get("source") == "github":
                root.child(k).delete()
                pruned += 1
                print("- " + k)
    print(f"done: {created} created, {updated} updated, {pruned} pruned")
    return 0


if __name__ == "__main__":
    sys.exit(main())

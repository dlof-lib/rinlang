#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/rin_link_index.py
============================================================================
فهرسة "معرّف الربط العام" (container.link.id) عبر أنواع ملفات مختلفة، وليس
فقط داخل مفسّر Rin. مفسّر Rin نفسه (rin_interpreter.cpp) يفهم فقط:

    link.id="core.user";   // بداخل حاوية: يسجّل هذه الحاوية تحت المعرّف "core.user"
    link id="core.user";   // في أي مكان آخر: يربط بالحاوية المسجَّلة بهذا المعرّف

لكنه لا يُنفِّذ ملفات html/js/cpp، لذا لا يمكنه "رؤية" إشارات لنفس المعرّف
هناك. هذا السكربت هو الجسر: يفحص شجرة مشروع كاملة (ملفات .rin و .html و
.js/.jsx/.ts/.tsx و .cpp/.h/.hpp) ويبني فهرساً واحداً يجمع كل مكان يُشار
فيه لنفس المعرّف، بحيث تصبح "container.link.id" مفهوماً واحداً يمكن تتبّعه
عبر اللغات كلها من نقطة واحدة (مفيد لبحث "من المرتبط بهذا؟" أو لأداة IDE
لاحقاً تعرض القفزة بين الملفات).

الاتفاقية في كل نوع ملف (بسيطة عمداً - سطر واحد قابل للـ grep):

    .rin              link.id="ID";                 (تصريح)   |  link id="ID";  (استخدام)
    .html             data-rin-link-id="ID"
    .js/.jsx/.ts/.tsx // @rin-link-id: ID            أو        rinLinkId: "ID"
    .cpp/.h/.hpp       // @rin-link-id: ID

الاستخدام:
    python3 tools/rin_link_index.py <مجلد المشروع> [--json out.json]

المخرجات: لكل معرّف (ID)، قائمة بكل الملفات/الأسطر التي أشارت إليه، مقسّمة
إلى "تصريح" (declare - أين وُلِد المعرّف داخل حاوية Rin) و"استخدام" (usage -
أي إشارة أخرى له من أي نوع ملف). لا توجد أي إشارة "تصريح" لمعرّف يعني أنه
معرّف يتيم (orphan) — يُبلَّغ عنه كتحذير.
"""
import argparse
import json
import re
import sys
from pathlib import Path

# نمط لكل نوع ملف: (regex, kind) حيث kind = "declare" أو "usage"
PATTERNS = {
    ".rin": [
        (re.compile(r'link\.id\s*=\s*"([^"]+)"'), "declare"),
        (re.compile(r'link\s+id\s*=\s*"([^"]+)"'), "usage"),
    ],
    ".html": [
        (re.compile(r'data-rin-link-id\s*=\s*"([^"]+)"'), "usage"),
    ],
    ".js": [
        (re.compile(r'//\s*@rin-link-id:\s*([^\s]+)'), "usage"),
        (re.compile(r'rinLinkId\s*:\s*["\']([^"\']+)["\']'), "usage"),
    ],
    ".cpp": [
        (re.compile(r'//\s*@rin-link-id:\s*([^\s]+)'), "usage"),
    ],
}
# امتدادات إضافية تتشارك نفس أنماط .js / .cpp
PATTERNS[".jsx"] = PATTERNS[".js"]
PATTERNS[".ts"] = PATTERNS[".js"]
PATTERNS[".tsx"] = PATTERNS[".js"]
PATTERNS[".h"] = PATTERNS[".cpp"]
PATTERNS[".hpp"] = PATTERNS[".cpp"]

SKIP_DIRS = {".git", "node_modules", "build", ".gradle", "app/build"}


def scan_file(path: Path, root: Path, index: dict):
    patterns = PATTERNS.get(path.suffix.lower())
    if not patterns:
        return
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return
    rel = str(path.relative_to(root))
    for line_no, line in enumerate(text.splitlines(), start=1):
        for regex, kind in patterns:
            m = regex.search(line)
            if m:
                link_id = m.group(1)
                entry = index.setdefault(link_id, {"declare": [], "usage": []})
                entry[kind].append(f"{rel}:{line_no}")


def build_index(root: Path) -> dict:
    index: dict = {}
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.parts):
            continue
        scan_file(path, root, index)
    return index


def main():
    ap = argparse.ArgumentParser(description="فهرسة container.link.id عبر rin/html/js/cpp")
    ap.add_argument("project_dir", help="جذر المشروع المراد فهرسته")
    ap.add_argument("--json", help="اكتب الفهرس أيضاً كملف JSON في هذا المسار")
    args = ap.parse_args()

    root = Path(args.project_dir).resolve()
    if not root.is_dir():
        print(f"غير موجود: {root}", file=sys.stderr)
        sys.exit(1)

    index = build_index(root)

    orphans = [lid for lid, entry in index.items() if not entry["declare"]]

    print(f"🔗 معرّفات الربط الموجودة: {len(index)}\n")
    for lid in sorted(index):
        entry = index[lid]
        print(f'  "{lid}"')
        for loc in entry["declare"]:
            print(f"     تصريح  -> {loc}")
        for loc in entry["usage"]:
            print(f"     استخدام -> {loc}")
    if orphans:
        print("\n⚠️  معرّفات مستخدَمة بلا أي تصريح (link.id=) في أي ملف .rin:")
        for lid in orphans:
            print(f"     - {lid}")

    if args.json:
        Path(args.json).write_text(
            json.dumps(index, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        print(f"\n📄 كُتب الفهرس إلى: {args.json}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tools/rin_md_preview.py
============================================================================
مُنفِّذ "▶ Preview rin" لملفات Markdown.

الفكرة: أي كتلة كود ```rin داخل ملف .md يسبقها سطر يحتوي العبارة المفتاحية
(بأي حالة أحرف) "view Preview rin" — سواء كتعليق HTML <!-- view Preview rin -->
أو كسطر عادي، أو كوسم مباشر على السياج نفسه ```rin view-preview-rin — تُعتبر
كتلة "حيّة": يشغّلها هذا السكربت فعلياً عبر محرّك Rin الحقيقي (tools/rin_run.cpp
المبني مسبقاً)، ثم يحقن الناتج الحقيقي في كتلة ```text مباشرة أسفلها تحت عنوان
"◀ الناتج الفعلي". لا تُخترع أي نتيجة يدوياً؛ كل شيء ناتج تنفيذ فعلي.

طريقة الاستخدام:
    python3 tools/rin_md_preview.py README.md            # يحدّث الملف في مكانه
    python3 tools/rin_md_preview.py README.md --check     # يتحقق فقط بلا كتابة (لِلـ CI)
    python3 tools/rin_md_preview.py *.md

المتطلبات: مُصرِّف g++ (لبناء rin_run مرة واحدة عند أول استخدام)، أو وجود
tools/rin_run الثنائي مسبقاً في PATH/المجلد.
"""
import re
import sys
import os
import subprocess
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TRIGGER_RE = re.compile(r"view\s*Preview\s*rin", re.IGNORECASE)
FENCE_RE = re.compile(r"^```rin([^\n]*)\n(.*?)^```\s*$", re.DOTALL | re.MULTILINE)
OUTPUT_BLOCK_RE = re.compile(
    r"\n> ◀ \*\*الناتج الفعلي \(preview rin\)\*\*\n```text\n.*?\n```\n\n*", re.DOTALL
)

RIN_RUN_BIN = os.path.join(ROOT, "tools", "_rin_run_bin")


def ensure_binary():
    """يبني tools/_rin_run_bin مرة واحدة من tools/rin_run.cpp + محرّك C++ الحقيقي."""
    if os.path.exists(RIN_RUN_BIN):
        return RIN_RUN_BIN
    cpp_dir = os.path.join(ROOT, "app", "src", "main", "cpp")
    sources = [
        os.path.join(ROOT, "tools", "rin_run.cpp"),
        os.path.join(cpp_dir, "rin_lexer.cpp"),
        os.path.join(cpp_dir, "rin_parser.cpp"),
        os.path.join(cpp_dir, "rin_interpreter.cpp"),
    ]
    cmd = ["g++", "-std=c++17", "-O2", "-o", RIN_RUN_BIN] + sources + ["-I", cpp_dir]
    subprocess.run(cmd, check=True)
    return RIN_RUN_BIN


def run_rin_snippet(code: str) -> str:
    binary = ensure_binary()
    with tempfile.NamedTemporaryFile("w", suffix=".rin", delete=False, encoding="utf-8") as tmp:
        tmp.write(code)
        tmp_path = tmp.name
    try:
        proc = subprocess.run([binary, tmp_path], capture_output=True, text=True, timeout=10)
        return (proc.stdout + proc.stderr).strip() or "(بلا مخرجات)"
    except subprocess.TimeoutExpired:
        return "⏱️ تجاوز الكود مهلة التنفيذ (10 ثوانٍ) — تحقق من حلقات لا نهائية."
    finally:
        os.unlink(tmp_path)


def is_triggered(preceding_text: str, fence_info: str) -> bool:
    if TRIGGER_RE.search(fence_info):
        return True
    # آخر 3 أسطر قبل السياج فقط، لتفادي إطلاق تنفيذ عبارات بعيدة عن الكتلة
    tail = "\n".join(preceding_text.splitlines()[-3:])
    return bool(TRIGGER_RE.search(tail))


def process(md_text: str):
    # نزيل أي كتل ناتج سابقة أُدرجت من تشغيل سابق قبل إعادة الحقن، لتفادي التكرار
    md_text = OUTPUT_BLOCK_RE.sub("\n", md_text)

    out = []
    pos = 0
    changed = False
    for m in FENCE_RE.finditer(md_text):
        preceding = md_text[pos:m.start()]
        out.append(preceding)
        fence_info, code = m.group(1), m.group(2)
        out.append(m.group(0))
        if is_triggered(preceding, fence_info):
            result = run_rin_snippet(code)
            out.append(f"\n> ◀ **الناتج الفعلي (preview rin)**\n```text\n{result}\n```\n")
            changed = True
        pos = m.end()
    out.append(md_text[pos:])
    return "".join(out), changed


def main():
    args = sys.argv[1:]
    check_only = "--check" in args
    files = [a for a in args if a != "--check"]
    if not files:
        print(__doc__)
        return 1

    any_diff = False
    for path in files:
        with open(path, "r", encoding="utf-8") as f:
            original = f.read()
        updated, changed = process(original)
        if updated != original:
            any_diff = True
            if check_only:
                print(f"✗ {path}: يحتاج إعادة توليد (شغّل بدون --check لتحديثه)")
            else:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(updated)
                print(f"✓ {path}: تم تحديث {'' if not changed else 'كتلة/كتل'} المعاينة الحية")
        else:
            print(f"= {path}: لا توجد كتل 'view Preview rin' أو النتائج محدَّثة أصلاً")
    return 1 if (check_only and any_diff) else 0


if __name__ == "__main__":
    raise SystemExit(main())

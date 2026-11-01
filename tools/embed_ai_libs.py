#!/usr/bin/env python3
"""Embeds rindf / rinml / rinai / rinstat / rinstat_adv / rinplot / rinplot_adv / rinauto into app/src/main/cpp/rin_stdlib_libs.h (idempotent).
Run after any change to lib/rindf.og.rin, lib/rinml.og.rin or lib/rinai.og.rin:
    python3 tools/embed_ai_libs.py
The Kotlin catalog (RinLibrary.kt) and README entries are added by hand."""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent
header = root / "app/src/main/cpp/rin_stdlib_libs.h"
LIBS = [  # (file name, constant name, raw string delimiter)
    ("rindf", "kLib_rindf_og_rin", "RINDFOGRIN"),
    ("rinml", "kLib_rinml_og_rin", "RINMLOGRIN"),
    ("rinai", "kLib_rinai_og_rin", "RINAIOGRIN"),
    ("rinstat", "kLib_rinstat_og_rin", "RINSTATOGRIN"),
    ("rinstat_adv", "kLib_rinstat_adv_og_rin", "RINSTATADVOGRIN"),
    ("rinplot", "kLib_rinplot_og_rin", "RINPLOTOGRIN"),
    ("rinplot_adv", "kLib_rinplot_adv_og_rin", "RINPLOTADVOGRIN"),
    ("rinauto", "kLib_rinauto_og_rin", "RINAUTOOGRIN"),
]
s = header.read_text(encoding="utf-8")
# نُدرج الثوابت مباشرة قبل دالة السجل (آخر ثابت نصي في الملف) ليبقى الترتيب مستقراً
anchor = "){0}\";\n".format("INDSINWEBOGRIN")
prev_end = None
for name, const, delim in LIBS:
    body = (root / f"lib/{name}.og.rin").read_text(encoding="utf-8").rstrip("\n")
    assert f"){delim}\"" not in body, name
    block = f'static const char* {const} = R"{delim}(\n{body}\n){delim}";\n'
    start_tok = f'static const char* {const} = R"{delim}(\n'
    if start_tok in s:
        a = s.index(start_tok)
        b = s.index(f'){delim}";\n', a) + len(f'){delim}";\n')
        s = s[:a] + block + s[b:]
    else:
        at = prev_end if prev_end else anchor
        assert s.count(at) == 1, at
        s = s.replace(at, at + block)
    prev_end = f'){delim}";\n'
    entry = f'        {{"lib/{name}.og.rin", {const}}},\n'
    if entry not in s:
        m = '        {"lib/indsinweb.og.rin", kLib_indsinweb_og_rin},\n'
        assert s.count(m) == 1
        s = s.replace(m, m + entry)
header.write_text(s, encoding="utf-8")
print("embedded:", [n for n, _, _ in LIBS])

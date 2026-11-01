#!/usr/bin/env python3
"""Embeds the passkit family libraries into app/src/main/cpp/rin_stdlib_libs.h (RinLibrary.kt entries are added by hand).
Run after any change to lib/passkit*.og.rin:   python3 tools/gen_passkitlang.py && python3 tools/embed_libs.py"""
import pathlib, re
root = pathlib.Path(__file__).resolve().parent.parent
header = root / "app/src/main/cpp/rin_stdlib_libs.h"
LIBS = [  # (file name, constant name, raw string delimiter)
    ("passkit", "kLib_passkit_og_rin", "PASSKITOGRIN"),
    ("passkitcrypt", "kLib_passkitcrypt_og_rin", "PKCRYPTOGRIN"),
    ("passkitdb", "kLib_passkitdb_og_rin", "PKDBOGRIN"),
    ("passkitlang", "kLib_passkitlang_og_rin", "PASSKITLANGOGRIN"),
]
s = header.read_text(encoding="utf-8")
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
        anchor = prev_end if prev_end else ')REQUIREKITOGRIN";\n'
        assert s.count(anchor) == 1, anchor
        s = s.replace(anchor, anchor + block)
    prev_end = f'){delim}";\n'
    entry = f'        {{"lib/{name}.og.rin", {const}}},\n'
    if entry not in s:
        m = '        {"lib/physics.og.rin", kLib_physics_og_rin},\n'
        assert s.count(m) == 1
        s = s.replace(m, m + entry)
header.write_text(s, encoding="utf-8")
print("embedded:", [n for n, _, _ in LIBS])

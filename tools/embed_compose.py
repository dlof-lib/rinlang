#!/usr/bin/env python3
"""Embeds lib/compose.og.rin into app/src/main/cpp/rin_stdlib_libs.h as a built-in library (idempotent).
Run after any change to lib/compose.og.rin:   python3 tools/embed_compose.py"""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent
header = root / "app/src/main/cpp/rin_stdlib_libs.h"
NAME, CONST, DELIM = "compose", "kLib_compose_og_rin", "COMPOSEOGRIN"
s = header.read_text(encoding="utf-8")
body = (root / f"lib/{NAME}.og.rin").read_text(encoding="utf-8").rstrip("\n")
assert f"){DELIM}\"" not in body
block = f'static const char* {CONST} = R"{DELIM}(\n{body}\n){DELIM}";\n'
start_tok = f'static const char* {CONST} = R"{DELIM}(\n'
end_tok = f'){DELIM}";\n'
if start_tok in s:
    a = s.index(start_tok)
    b = s.index(end_tok, a) + len(end_tok)
    s = s[:a] + block + s[b:]
else:
    anchor = '\ninline const std::unordered_map<std::string, std::string>& embeddedRinLibraries() {'
    assert s.count(anchor) == 1
    s = s.replace(anchor, "\n" + block + anchor)
entry = f'        {{"lib/{NAME}.og.rin", {CONST}}},\n'
if entry not in s:
    m = '        {"lib/functional.og.rin", kLib_functional_og_rin},\n'
    assert s.count(m) == 1
    s = s.replace(m, m + entry)
header.write_text(s, encoding="utf-8")
print("embedded:", NAME)

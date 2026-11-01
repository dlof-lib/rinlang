#!/usr/bin/env python3
"""Embeds the split library lib/extdb.og.rin/ into app/src/main/cpp/rin_stdlib_libs.h as ONE flattened token
`lib/extdb.og.rin` (the parts listed in disconom.toml, concatenated in order — the loader links split-library parts
into a single scope anyway), so `@import "extdb";` works on Android where no lib/ folder exists on disk.
Run after any change under lib/extdb.og.rin/:   python3 tools/embed_extdb.py"""
import pathlib, re
root = pathlib.Path(__file__).resolve().parent.parent
header = root / "app/src/main/cpp/rin_stdlib_libs.h"
libdir = root / "lib/extdb.og.rin"
CONST, DELIM, KEY = "kLib_extdb_og_rin", "EXTDBOGRIN", "lib/extdb.og.rin"

toml = (libdir / "disconom.toml").read_text(encoding="utf-8")
m = re.search(r'^parts\s*=\s*\[(.*?)\]', toml, re.S | re.M)
parts = re.findall(r'"([^"]+)"', m.group(1))
version = re.search(r'^version\s*=\s*"([^"]+)"', toml, re.M).group(1)
body = (f"// extdb {version} — مكتبة مقسَّمة (lib/extdb.og.rin/) مدمجة كملف واحد. لا تعدّل هنا: عدّل الأجزاء ثم شغّل tools/embed_extdb.py\n"
        + "\n".join((libdir / p).read_text(encoding="utf-8").rstrip("\n") + "\n" for p in parts)).rstrip("\n")
assert f"){DELIM}\"" not in body
block = f'static const char* {CONST} = R"{DELIM}(\n{body}\n){DELIM}";\n'

s = header.read_text(encoding="utf-8")
start_tok = f'static const char* {CONST} = R"{DELIM}(\n'
if start_tok in s:
    a = s.index(start_tok)
    b = s.index(f'){DELIM}";\n', a) + len(f'){DELIM}";\n')
    s = s[:a] + block + s[b:]
else:
    anchor = "inline const std::unordered_map<std::string, std::string>& embeddedRinLibraries()"
    assert s.count(anchor) == 1
    s = s.replace(anchor, block + "\n" + anchor)
entry = f'        {{"{KEY}", {CONST}}},\n'
if entry not in s:
    m2 = '        {"lib/physics.og.rin", kLib_physics_og_rin},\n'
    assert s.count(m2) == 1
    s = s.replace(m2, m2 + entry)
header.write_text(s, encoding="utf-8")

# editor intellisense: the library name list
ih = root / "app/src/main/cpp/editor/rin_intellisense_data.h"
t = ih.read_text(encoding="utf-8")
name = '  "extdb.og.rin",\n'
if name not in t:
    after = '  "events.og.rin",\n'
    assert t.count(after) == 1
    t = t.replace(after, after + name)
    ih.write_text(t, encoding="utf-8")
print(f"embedded {KEY} v{version}: {len(parts)} parts, {len(body)} bytes")

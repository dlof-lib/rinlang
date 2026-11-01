#!/usr/bin/env python3
"""يضمّن lib/indsinicons.og.rin داخل app/src/main/cpp/rin_stdlib_libs.h (كمكتبة مدمجة) ويسجّلها في embeddedRinLibraries().
تُشغَّل بعد كل تعديل على المكتبة:   python3 tools/icons/gen_indsin_icons.py && python3 tools/embed_indsinicons.py
آمنة التكرار (idempotent)."""
import pathlib
root = pathlib.Path(__file__).resolve().parent.parent
hdr = root / "app/src/main/cpp/rin_stdlib_libs.h"
CONST, DELIM = "kLib_indsinicons_og_rin", "INDSINICONSOGRIN"
body = (root / "lib/indsinicons.og.rin").read_text(encoding="utf-8").rstrip("\n")
assert f"){DELIM}\"" not in body
block = f'static const char* {CONST} = R"{DELIM}(\n{body}\n){DELIM}";\n\n'
s = hdr.read_text(encoding="utf-8")
start = f'static const char* {CONST} = R"{DELIM}(\n'
if start in s:
    a = s.index(start); b = s.index(f'){DELIM}";\n\n', a) + len(f'){DELIM}";\n\n')
    s = s[:a] + block + s[b:]
else:
    anchor = "inline const std::unordered_map<std::string, std::string>& embeddedRinLibraries() {"
    assert s.count(anchor) == 1
    s = s.replace(anchor, block + anchor)
entry = f'        {{"lib/indsinicons.og.rin", {CONST}}},\n'
if entry not in s:
    m = '        {"lib/physics.og.rin", kLib_physics_og_rin},\n'
    assert s.count(m) == 1
    s = s.replace(m, m + entry)
hdr.write_text(s, encoding="utf-8", newline="\n")
print("embedded indsinicons:", len(body), "bytes")

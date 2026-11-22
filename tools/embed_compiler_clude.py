#!/usr/bin/env python3
"""يُضمّن المكتبة المقسَّمة lib/compiler_clude.og.rin/ داخل المفسّر (app/src/main/cpp/rin_stdlib_libs.h) كملف واحد
`lib/compiler_clude.og.rin` (الأجزاء حسب disconom.json بالترتيب — المحمِّل يربطها في نطاق واحد أصلاً)،
فيعمل `@import "compiler_clude";` على أي منصة بلا مجلد lib على القرص (أندرويد مثلاً).
شغّله بعد أي تعديل داخل lib/compiler_clude.og.rin/ :   python3 tools/embed_compiler_clude.py"""
import json, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
header = root / "app/src/main/cpp/rin_stdlib_libs.h"
libdir = root / "lib/compiler_clude.og.rin"
CONST, DELIM, KEY = "kLib_compiler_clude_og_rin", "CCLUDEOGRIN", "lib/compiler_clude.og.rin"

meta = json.loads((libdir / "disconom.json").read_text(encoding="utf-8"))
parts, version = meta["parts"], meta["version"]
body = (f"// compiler_clude {version} — مكتبة مقسَّمة (lib/compiler_clude.og.rin/) مدمجة كملف واحد. لا تعدّل هنا: عدّل الأجزاء ثم شغّل tools/embed_compiler_clude.py\n"
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

ih = root / "app/src/main/cpp/editor/rin_intellisense_data.h"
t = ih.read_text(encoding="utf-8")
name = '  "compiler_clude.og.rin",\n'
if name not in t:
    after = '  "extdb.og.rin",\n'
    assert t.count(after) == 1
    ih.write_text(t.replace(after, after + name), encoding="utf-8")
print(f"embedded {KEY} v{version}: {len(parts)} parts, {len(body)} bytes")

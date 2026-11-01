#!/usr/bin/env python3
"""Generates lib/passkitlang.og.rin from the files in examples/customlang/passkit/ (Lexer+Parser+Interpreter).
Run:  python3 tools/gen_passkitlang.py
removes local @imports and renames common global functions (lex/parse/interpret/runSource...) with a pk prefix so they
do not collide with other custom languages imported in the same program. After generating, update rin_stdlib_libs.h (see README)."""
import re, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
src = root / "examples/customlang/passkit"
parts = []
for name in ("Lexer.rin", "Parser.rin", "Interpreter.rin"):
    text = (src / name).read_text(encoding="utf-8")
    text = "\n".join(l for l in text.splitlines() if not l.strip().startswith("@import"))
    parts.append(f"// ───────── {name} ─────────\n" + text.strip() + "\n")
body = "\n".join(parts)
ren = {"lex": "pkLex", "parse": "pkParse", "parseElement": "pkParseElement", "interpret": "pkInterpret",
       "runSource": "pkRunSource", "not_slash_gt": "pkNotSlashGt"}
for old, new in ren.items():
    body = re.sub(r"\b" + old + r"\(", new + "(", body)
header = '''// ============================================================================
//  lib/passkitlang.og.rin — interpreter of the <passkit> tag language as a library (auto-generated - do not edit by hand)
//  Source: examples/customlang/passkit/  ·  Generator: tools/gen_passkitlang.py
//  Import:
//    @import "lib/passkitlang.og.rin";
//
//  From Rin:
//    let r = passkitRun("signup.passkit", { email: "a@b.com" });   // {ok, output, vars, value, error, message}
//    print passkitGet(r, "pw.strength");
//    passkitRegister("double", fun(x) { return x * 2; });           // called from .passkit with <call fn="double" arg0="21"/>
//  From .passkit:  <import file> · <run file name in.k=...> · <call fn=...> · <input> · <return>
// ============================================================================

@import "langkit";
@import "passkit";
@import "passkitcrypt";
@import "passkitdb";

'''
(root / "lib/passkitlang.og.rin").write_text(header + body, encoding="utf-8")
print("lib/passkitlang.og.rin", len(header + body), "bytes")

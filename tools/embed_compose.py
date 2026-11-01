#!/usr/bin/env python3
"""Embeds the compose library family into app/src/main/cpp/rin_stdlib_libs.h as built-in libraries (idempotent).

    lib/compose.og.rin          core (convert / compose / merge / react / token<->number / utilities)
    lib/compose_seq.og.rin      sequences: sort, group, slice, zip, search
    lib/compose_path.og.rin     paths: getIn / setIn / flattenKeys / cloneDeep
    lib/compose_flow.og.rin     flow: curry, memoize, retry, cond, matchKind, Result
    lib/compose_schema.og.rin   schemas: validate + conform
    lib/compose_diff.og.rin     diffDeep / patchDeep / merge3
    lib/compose_store.og.rin    reactive store

Run after ANY change to one of those files:   python3 tools/embed_compose.py
Use --check to only verify that the header is up to date (exit code 1 if not).
Raw-string delimiters are kept <= 16 chars (C++ limit)."""
import pathlib, sys

root = pathlib.Path(__file__).resolve().parent.parent
header = root / "app/src/main/cpp/rin_stdlib_libs.h"

# (library name, C++ constant, raw-string delimiter)
LIBS = [
    ("compose",        "kLib_compose_og_rin",        "COMPOSEOGRIN"),
    ("compose_seq",    "kLib_compose_seq_og_rin",    "CMPSEQ"),
    ("compose_path",   "kLib_compose_path_og_rin",   "CMPPATH"),
    ("compose_flow",   "kLib_compose_flow_og_rin",   "CMPFLOW"),
    ("compose_schema", "kLib_compose_schema_og_rin", "CMPSCHEMA"),
    ("compose_diff",   "kLib_compose_diff_og_rin",   "CMPDIFF"),
    ("compose_store",  "kLib_compose_store_og_rin",  "CMPSTORE"),
]
ANCHOR_MAP = 'inline const std::unordered_map<std::string, std::string>& embeddedRinLibraries() {'
ANCHOR_ENTRY = '        {"lib/functional.og.rin", kLib_functional_og_rin},\n'

def embed(s, name, const, delim):
    assert len(delim) <= 16, delim
    body = (root / f"lib/{name}.og.rin").read_text(encoding="utf-8").rstrip("\n")
    assert f"){delim}\"" not in body, f"delimiter {delim} occurs inside {name}"
    block = f'static const char* {const} = R"{delim}(\n{body}\n){delim}";\n'
    start_tok = f'static const char* {const} = R"{delim}(\n'
    end_tok = f'){delim}";\n'
    if start_tok in s:
        a = s.index(start_tok)
        b = s.index(end_tok, a) + len(end_tok)
        s = s[:a] + block + s[b:]
    else:
        anchor = "\n" + ANCHOR_MAP
        assert s.count(anchor) == 1, "anchor for embeddedRinLibraries() not found"
        s = s.replace(anchor, "\n" + block + anchor)
    entry = f'        {{"lib/{name}.og.rin", {const}}},\n'
    if entry not in s:
        assert s.count(ANCHOR_ENTRY) == 1, "map anchor (functional) not found"
        s = s.replace(ANCHOR_ENTRY, ANCHOR_ENTRY + entry)
    return s

original = header.read_text(encoding="utf-8")
s = original
for name, const, delim in LIBS:
    s = embed(s, name, const, delim)

if "--check" in sys.argv:
    print("up to date" if s == original else "OUT OF DATE: run python3 tools/embed_compose.py")
    sys.exit(0 if s == original else 1)

header.write_text(s, encoding="utf-8")
print("embedded:", ", ".join(n for n, _, _ in LIBS))

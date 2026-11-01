#!/usr/bin/env python3
"""يولّد app/src/main/cpp/editor/rin_intellisense_data.h لمحرك IntelliSense.

المصادر (لا تعدّل الملف الناتج يدوياً):
  - syntaxes/rin-keywords.json            أسماء الدوال المدمجة والـ natives
  - app/src/main/cpp/*.cpp                عدد المعاملات الفعلي (expectArgs / expectArgsRange / need)
  - scripts/intellisense_docs.json        توثيق يدوي محقَّق (أسماء المعاملات + الوصف)
  - RinKeywordDocs.kt                     توثيق الكلمات المحجوزة
  - lib/*.og.rin                          وحدات @import

تشغيل من جذر المشروع:  python3 scripts/gen_intellisense_data.py
"""
import glob, json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

def cstr(s):
    out = []
    for ch in s:
        if ch == '\\': out.append('\\\\')
        elif ch == '"': out.append('\\"')
        elif ch == '\n': out.append('\\n')
        elif ch == '\t': out.append('\\t')
        elif ch == '?': out.append('\\?')   # يمنع trigraphs
        else: out.append(ch)
    return '"' + ''.join(out) + '"'

# ---- 1) عدد المعاملات من المصدر --------------------------------------------------
arity = {}
for f in glob.glob('app/src/main/cpp/**/*.cpp', recursive=True):
    s = open(f, encoding='utf8', errors='ignore').read()
    for m in re.finditer(r'expectArgs\("([A-Za-z_.0-9]+)",\s*\w+,\s*(\d+)', s):
        arity[m.group(1)] = (int(m.group(2)), int(m.group(2)))
    for m in re.finditer(r'expectArgsRange\("([A-Za-z_.0-9]+)",\s*\w+,\s*(\d+),\s*(\d+)', s):
        arity[m.group(1)] = (int(m.group(2)), int(m.group(3)))
    for m in re.finditer(r'\bneed\("([A-Za-z_.0-9]+)",\s*\w+,\s*(\d+),\s*(-?\d+)', s):
        mx = int(m.group(3))
        arity[m.group(1)] = (int(m.group(2)), mx if mx >= 0 else -1)

kw = json.load(open('syntaxes/rin-keywords.json', encoding='utf8'))
docs = json.load(open('scripts/intellisense_docs.json', encoding='utf8'))
fdocs = docs['functions']

names = set(kw['builtins']) | set(kw['natives'])
names = {n for n in names if n and not n.startswith('#') and not n.startswith('__')
         and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.]*', n)}
# أسماء المساحات نفسها (Set/Env) ليست دوال
names -= {'Set', 'Env'}
# كلمات محجوزة لا تُدرج كدوال
names -= set(kw['reserved'])

rows = []
bad = []
for n in sorted(names, key=lambda x: (x.count('.'), x.lower(), x)):
    mn, mx = arity.get(n, (-1, -1))
    d = fdocs.get(n)
    params, ret, doc = '', '', ''
    if d:
        params = ','.join(d['params'])
        ret = d.get('ret', '')
        doc = d['doc']
        req = len([p for p in d['params'] if not p.startswith('[') and not p.startswith('...')])
        tot = len(d['params'])
        variadic = any(p.startswith('...') for p in d['params'])
        if n in arity:
            amin, amax = arity[n]
            if req != amin and not variadic:
                bad.append(f'{n}: توثيق يدوي يطلب {req} إلزامي لكن المصدر {amin}')
            if amax >= 0 and tot != amax and not variadic:
                bad.append(f'{n}: توثيق يدوي {tot} معاملاً لكن المصدر {amax}')
        mn = req if mn < 0 else mn
        if mx < 0 and not variadic: mx = tot
    rows.append((n, mn, mx, params, ret, doc))

if bad:
    print('تعارض بين التوثيق اليدوي والمصدر:', *bad, sep='\n  ')
    sys.exit(1)

# ---- 2) توثيق الكلمات المحجوزة من Kotlin ------------------------------------------
ktxt = open('app/src/main/java/com/dlof/rinlang/RinKeywordDocs.kt', encoding='utf8').read()
kdocs = re.findall(r'^\s*"([A-Za-z_]+)" to "((?:[^"\\]|\\.)*)"', ktxt, re.M)
kdocs = [(k, v.replace('\\"', '"').replace('\\\\', '\\')) for k, v in kdocs]

# ---- 3) وحدات lib ---------------------------------------------------------------
mods = sorted(os.path.basename(p) for p in glob.glob('lib/*.og.rin'))

# ---- 4) كتابة الرأس -------------------------------------------------------------
o = []
o.append('// rin_intellisense_data.h — مولَّد آلياً بواسطة scripts/gen_intellisense_data.py. لا تعدّله يدوياً.')
o.append('#pragma once')
o.append('namespace rinedit { namespace intel { namespace data {')
o.append('struct FuncSig { const char* name; int minArgs; int maxArgs; const char* params; const char* ret; const char* doc; };')
o.append('struct NameDoc { const char* name; const char* doc; };')
o.append(f'inline const FuncSig kFuncs[] = {{')
for n, mn, mx, p, r, d in rows:
    o.append(f'  {{{cstr(n)}, {mn}, {mx}, {cstr(p)}, {cstr(r)}, {cstr(d)}}},')
o.append('};')
o.append('inline const NameDoc kKeywordDocs[] = {')
for k, v in kdocs: o.append(f'  {{{cstr(k)}, {cstr(v)}}},')
o.append('};')
o.append('inline const NameDoc kNamespaceDocs[] = {')
for k, v in docs['namespaces'].items(): o.append(f'  {{{cstr(k)}, {cstr(v)}}},')
o.append('};')
o.append('inline const char* const kLibModules[] = {')
for m in mods: o.append(f'  {cstr(m)},')
o.append('};')
o.append('inline const char* const kHashWords[] = {')
for w in kw['hash']: o.append(f'  {cstr(w)},')
o.append('};')
o.append('inline const char* const kContainerWords[] = {')
seen = set()
for w in kw['container'] + kw['directives']:
    if w not in seen: seen.add(w); o.append(f'  {cstr(w)},')
o.append('};')
o.append('inline const char* const kLivingWords[] = {')
for w in kw['living']: o.append(f'  {cstr(w)},')
o.append('};')
o.append('}}} // namespace rinedit::intel::data')
path = 'app/src/main/cpp/editor/rin_intellisense_data.h'
open(path, 'w', encoding='utf8').write('\n'.join(o) + '\n')
print(f'{path}: {len(rows)} دالة ({sum(1 for r in rows if r[5])} موثّقة يدوياً، {sum(1 for r in rows if r[1] >= 0)} بعدد معاملات معروف)، '
      f'{len(kdocs)} كلمة موثّقة، {len(mods)} وحدة')

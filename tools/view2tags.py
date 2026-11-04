#!/usr/bin/env python3
"""view2tags — يحوّل لهجة `@view.Kind=name … .end/view` (و@loop/@element/@container) إلى صيغة الوسوم `<Kind#name …>`.

الاستعمال:  python3 tools/view2tags.py in.rin [-o out.rin]      (بلا -o يطبع إلى stdout)
            python3 tools/view2tags.py --verify in.rin          (يحوّل ثم يقارن ناتج الرسم بالأصلي عبر $INDSIN_DUMP)

القواعد: كل سمة `key=expr;` تصبح `key="نص"` أو `key=12` أو `key={تعبير}`. كود Rin الخارجي (fun/let/print/@theme…)
يُلفّ بـ <Rin>…</Rin> حرفياً، و`warp x = e;` تصبح `<warp x={e} />`، والتعليقات `// …` تصبح `<!-- … -->`.
ملف لا يمكن تحويله بأمان (اسم عنصر غير صالح كمعرّف، مكوّن مدمج متعارض…) يُرفض برسالة بدل تحويل خاطئ.
"""
import re, sys

# مكوّنات مدمجة في صيغة الوسوم (rin_indsin_tags.h: usesBuiltinComponent) تتفوّق على نوع Strand الخام بنفس الاسم،
# فيُكتب النوع الخام صراحةً بـ <View.Kind> كي يبقى معنى @view.Kind كما هو.
BUILTIN_TAGS = {"Chip", "Stat", "Alert", "Section", "ListTile", "Hero", "EmptyState", "Toggle", "Field", "PasswordField",
                "OrDivider", "SocialButton", "OtpBoxes", "LoginForm", "Tabs", "Stepper", "Badge", "ProfileCard", "ConfirmCard",
                "TopBar", "Header", "NavBar", "Breadcrumb", "SearchBar", "Pagination", "Footer", "Fab", "TextField", "EmailField",
                "NumberField", "PhoneField", "SearchField", "TextAreaField"}
OPEN = re.compile(r'@(view|loop|element|container)(?:\.([A-Za-z_][A-Za-z0-9_]*))?\s*=\s*("?)([^\s";]+)\3')
END = re.compile(r'\.end/(view|loop|element|container)(?![.\w])')
DATA_BLOCK = re.compile(r'@container\.([A-Za-z_][A-Za-z0-9_]*)\s*=')


class Unsafe(Exception):
    pass


def skip_str(s, i):
    q = s[i]; i += 1
    while i < len(s):
        if s[i] == '\\': i += 2; continue
        if s[i] == q: return i + 1
        i += 1
    raise Unsafe("نص غير منتهٍ")


def read_attr_value(s, i):
    """يقرأ تعبيراً حتى `;` على مستوى 0 (يحترم النصوص والأقواس). يرجع (نص التعبير، موضع بعد ;)."""
    depth = 0; j = i
    while j < len(s):
        c = s[j]
        if c in '"\'': j = skip_str(s, j); continue
        if c == '/' and s[j:j + 2] == '//':
            while j < len(s) and s[j] != '\n': j += 1
            continue
        if c in '([{': depth += 1
        elif c in ')]}': depth -= 1
        elif c == ';' and depth == 0: return s[i:j].strip(), j + 1
        j += 1
    raise Unsafe("سمة بلا فاصلة منقوطة")


def fmt_value(expr):
    e = expr.strip()
    if re.fullmatch(r'-?\d+(\.\d+)?', e): return e
    if e in ('true', 'false', 'null'): return e
    if re.fullmatch(r'-?\d+(\.\d+)?(px|ms|s|%|vw|vh)', e): return e  # كما في docs/indsin_tags.md
    m = re.fullmatch(r'"([^"\\\n]*)"', e)
    if m and '{' not in e and '<' not in e and '>' not in e: return e
    if '\n' in e: e = re.sub(r'\s*\n\s*', ' ', e)
    return '{' + e + '}'


def parse_nodes(s, i, closer):
    """يحلّل سلسلة عناصر حتى closer. يرجع (قائمة عقد، موضع). العقدة: ('raw',text) | ('node',tag,name,attrs,children)."""
    out = []; raw_start = i
    def flush(upto):
        if upto > raw_start: out.append(('raw', s[raw_start:upto]))
    while i < len(s):
        if s[i] in '"\'': i = skip_str(s, i); continue
        if s.startswith('//', i):
            while i < len(s) and s[i] != '\n': i += 1
            continue
        if s.startswith('/*', i):
            j = s.find('*/', i); i = len(s) if j < 0 else j + 2; continue
        dm = DATA_BLOCK.match(s, i) if s[i] == '@' else None
        if dm:  # حاوية بيانات (@container.api/.doc/…): تبقى كوداً خاماً كاملةً بما فيها ما بداخلها
            endtok = '.end/container.' + dm.group(1); j = s.find(endtok, dm.end())
            if j < 0: raise Unsafe("%s بلا إغلاق" % endtok)
            i = j + len(endtok); continue
        m = END.match(s, i)
        if m:
            if closer is None: raise Unsafe(".end/%s زائدة" % m.group(1))
            flush(i)
            if m.group(1) != closer: raise Unsafe("إغلاق .end/%s لا يطابق %s" % (m.group(1), closer))
            return out, m.end()
        m = OPEN.match(s, i) if s[i] == '@' else None
        if m and (i == 0 or s[i - 1] in ' \t\n;{'):
            flush(i)
            role, kind, _, name = m.groups()
            i = m.end()
            attrs = []
            while True:
                while i < len(s) and s[i] in ' \t\r\n': i += 1
                if s.startswith('.end/', i) or s.startswith('@view', i) or s.startswith('@element', i) or s.startswith('@loop', i) or s.startswith('@container', i) or i >= len(s): break
                if s.startswith('//', i):
                    while i < len(s) and s[i] != '\n': i += 1
                    continue
                km = re.compile(r'([A-Za-z_][A-Za-z0-9_.]*)\s*=\s*').match(s, i)
                if not km: break
                val, i2 = read_attr_value(s, km.end())
                attrs.append((km.group(1), val)); i = i2
            children, i = parse_nodes(s, i, role)
            out.append(('node', role, kind, name, attrs, children, ))
            raw_start = i
            continue
        i += 1
    flush(len(s))
    if closer is not None: raise Unsafe("عنصر %s بلا إغلاق" % closer)
    return out, i


def tag_for(role, kind):
    if role == 'view': return ('View.' + kind) if kind in BUILTIN_TAGS else (kind or 'Box')
    if role == 'loop': return 'Loop'
    if role == 'container': return 'Container'
    return 'Element.' + (kind or 'button')


def render(nodes, ind, top):
    out = []; pad = '  ' * ind
    for n in nodes:
        if n[0] == 'raw':
            out.append(render_raw(n[1], pad))
            continue
        _, role, kind, name, attrs, children = n
        tag = tag_for(role, kind)
        if name and not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_-]*', name): raise Unsafe("اسم عنصر غير صالح كمعرّف: %r" % name)
        head = '<' + tag + ('#' + name if name else '')
        for k, v in attrs: head += ' %s=%s' % (k, fmt_value(v))
        kids = [c for c in children if not (c[0] == 'raw' and not c[1].strip())]
        if not kids: out.append('%s%s />\n' % (pad, head))
        else:
            out.append('%s%s>\n' % (pad, head)); out.append(render(kids, ind + 1, False)); out.append('%s</%s>\n' % (pad, tag))
    return ''.join(out)


WARP = re.compile(r'^\s*warp\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?);\s*$')
IMPORT = re.compile(r'^\s*@import\s+("[^"]*")\s*;?\s*$')


def render_raw(text, pad):
    out = []; code = []
    def flush_code():
        if code:
            body = ''.join(code).strip('\n')
            if body.strip():
                if '</Rin>' in body: raise Unsafe("</Rin> داخل كود خام")
                out.append('%s<Rin>\n%s\n%s</Rin>\n' % (pad, body, pad))
            code.clear()
    for line in text.split('\n'):
        s = line.strip()
        if not s: continue
        w = WARP.match(line); im = IMPORT.match(line)
        if w and '//' not in w.group(2) and '"' not in w.group(2).replace('\\"', ''):
            flush_code(); out.append('%s<warp %s={%s} />\n' % (pad, w.group(1), w.group(2)))
        elif w:
            flush_code(); out.append('%s<warp %s={%s} />\n' % (pad, w.group(1), w.group(2).strip()))
        elif im:
            flush_code(); out.append('%s<Import src=%s />\n' % (pad, im.group(1)))
        elif s.startswith('//') and not code:
            out.append('%s<!-- %s -->\n' % (pad, s[2:].strip().replace('-->', '- >')))
        elif s:
            code.append(line + '\n')
    flush_code()
    return ''.join(out)


def convert(src):
    nodes, _ = parse_nodes(src, 0, None)
    return render(nodes, 0, True)


if __name__ == '__main__':
    args = sys.argv[1:]
    if not args: print(__doc__); sys.exit(64)
    outp = None
    if '-o' in args: k = args.index('-o'); outp = args[k + 1]; del args[k:k + 2]
    verify = '--verify' in args
    if verify: args.remove('--verify')
    src = open(args[-1], encoding='utf-8').read()
    try: res = convert(src)
    except Unsafe as e:
        sys.stderr.write('%s: لا يمكن التحويل بأمان: %s\n' % (args[-1], e)); sys.exit(1)
    if outp: open(outp, 'w', encoding='utf-8').write(res)
    else: sys.stdout.write(res)
    if verify:
        import os, subprocess, tempfile
        dump = os.environ.get('INDSIN_DUMP')
        if not dump: sys.stderr.write('--verify يحتاج INDSIN_DUMP=مسار أداة dump\n'); sys.exit(64)
        with tempfile.NamedTemporaryFile('w', suffix='.rin', delete=False, encoding='utf-8') as t: t.write(res)
        a = subprocess.run([dump, args[-1]], capture_output=True, text=True).stdout
        b = subprocess.run([dump, t.name], capture_output=True, text=True).stdout
        os.unlink(t.name)
        strip = lambda t: re.sub(r',"line":\d+', '', t)
        if a.startswith('{"ok":false') and strip(a) == strip(b): sys.stderr.write('%s: الخطأ نفسه في الأصل والمحوَّل (مصدر غير صالح أصلاً)\n' % args[-1]); sys.exit(0)
        if a != b: sys.stderr.write('%s: الناتج المرسوم يختلف عن الأصل\n' % args[-1]); sys.exit(2)
        sys.stderr.write('%s: مطابق (%d بايت رسم)\n' % (args[-1], len(a)))

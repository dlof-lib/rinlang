#!/usr/bin/env python3
"""channel47 — لغة وصف بيانات وإعدادات (امتداد الملف: .ch47). المواصفة: docs/channel47.md

  channel47.py check   <file.ch47> [--kind library|package|tool] [--lang ar|en]
  channel47.py fmt     <file.ch47> [-w]          # تنسيق قانوني (يطبع أو يكتب في الملف)
  channel47.py json    <file.ch47>               # تصدير JSON
  channel47.py from-json <file.json>             # JSON -> channel47
  channel47.py get     <file.ch47> <a.b.c>       # قراءة قيمة بمسار منقّط
  channel47.py merge   <base.ch47> <over.ch47>   # دمج طبقات (الثاني يغلب)

مرجع بايثون (بلا اعتماديات) يطابق المواصفة 1.0. رموز الأخطاء C47xxx ثابتة.
"""
import argparse, copy, datetime as dt, json, math, re, sys

SPEC_VERSION = (1, 0)
INT_MIN, INT_MAX = -(2 ** 63), 2 ** 63 - 1

# ───────────────────────── الأخطاء ─────────────────────────
MSG = {
    "C47002": ("إصدار channel47 غير مدعوم: {ver}", "unsupported channel47 version: {ver}"),
    "C47010": ("صياغة غير متوقعة: {what}", "unexpected syntax: {what}"),
    "C47011": ("المسار «{path}» ليس قسماً قابلاً للتوسعة", "path '{path}' is not an extendable section"),
    "C47012": ("القسم «{path}» معرَّف مسبقاً", "section '{path}' is already defined"),
    "C47013": ("المفتاح «{path}» مكرر", "duplicate key '{path}'"),
    "C47014": ("مصفوفة الأقسام [[...]] غير مدعومة؛ استعمل table", "[[...]] is not supported; use a table"),
    "C47020": ("نص غير منتهٍ (علامة الاقتباس غير مغلقة)", "unterminated string"),
    "C47021": ("قيمة غير معروفة «{tok}» — النصوص تُكتب بين علامتي اقتباس", "unknown value '{tok}' - strings must be quoted"),
    "C47022": ("تاريخ/وقت غير صالح «{tok}»", "invalid date/time '{tok}'"),
    "C47023": ("رمز هروب غير صالح «{what}»", "invalid escape '{what}'"),
    "C47024": ("رقم غير صالح «{tok}»: {why}", "invalid number '{tok}': {why}"),
    "C47030": ("نوع عمود غير معروف «{typ}»", "unknown column type '{typ}'"),
    "C47031": ("عمود مكرر «{name}»", "duplicate column '{name}'"),
    "C47040": ("الصف فيه {got} خلية والجدول {want} أعمدة", "row has {got} cells but table has {want} columns"),
    "C47041": ("خلية ناقصة للعمود «{name}» (لا قيمة افتراضية)", "missing cell for column '{name}' (no default)"),
    "C47042": ("العمود «{name}» يتوقع {want} لكن القيمة {got}", "column '{name}' expects {want} but got {got}"),
    "C47043": ("الجدول «{name}» بلا end", "table '{name}' has no closing 'end'"),
    "C47050": ("المفتاح المطلوب «{path}» مفقود", "required key '{path}' is missing"),
    "C47051": ("«{path}» يتوقع {want} لكن القيمة {got}", "'{path}' expects {want} but got {got}"),
    "C47052": ("قيمة غير صالحة في «{path}»: {why}", "invalid value at '{path}': {why}"),
    "C47053": ("مفتاح غير معروف «{path}»", "unknown key '{path}'"),
    "C47054": ("نوع غير معروف «{kind}»", "unknown kind '{kind}'"),
}
LANG = "ar"


def _(ar, en):
    return ar if LANG == "ar" else en


def _m(code, **kw):
    ar, en = MSG[code]
    return (ar if LANG == "ar" else en).format(**kw)


class Channel47Error(Exception):
    def __init__(self, code, message, line=0, col=0, text="", file="<string>"):
        super().__init__(message)
        self.code, self.message, self.line, self.col, self.text, self.file = code, message, line, col, text, file

    def __str__(self):
        head = f"{self.file}:{self.line}:{self.col}: error {self.code}: {self.message}"
        if self.text:
            return head + f"\n  {self.text}\n  {' ' * max(self.col - 1, 0)}^"
        return head


# ───────────────────────── النماذج ─────────────────────────
class Column:
    __slots__ = ("name", "type", "array", "nullable", "has_default", "default")

    def __init__(self, name, type_, array=False, nullable=False, has_default=False, default=None):
        self.name, self.type, self.array, self.nullable = name, type_, array, nullable
        self.has_default, self.default = has_default, default

    def spec(self):
        return self.type + ("[]" if self.array else "") + ("?" if self.nullable else "")

    def __repr__(self):
        return f"Column({self.name}: {self.spec()})"


class Table(list):
    """قائمة قواميس (صفوف) مع تعريف الأعمدة."""
    def __init__(self, columns, rows=()):
        super().__init__(rows)
        self.columns = list(columns)


TYPES = ("text", "int", "float", "bool", "date", "time", "datetime", "any")
DEFAULT = object()  # الخلية «_»


def type_name(v):
    if v is None: return "nil"
    if isinstance(v, bool): return "bool"
    if isinstance(v, int): return "int"
    if isinstance(v, float): return "float"
    if isinstance(v, str): return "text"
    if isinstance(v, dt.datetime): return "datetime"
    if isinstance(v, dt.date): return "date"
    if isinstance(v, dt.time): return "time"
    if isinstance(v, Table): return "table"
    if isinstance(v, list): return "list"
    if isinstance(v, dict): return "object"
    return type(v).__name__


def _conform_base(v, typ):
    if typ == "any": return True, v
    if typ == "text": return isinstance(v, str), v
    if typ == "bool": return isinstance(v, bool), v
    if typ == "int": return (isinstance(v, int) and not isinstance(v, bool)), v
    if typ == "float":
        if isinstance(v, bool): return False, v
        if isinstance(v, (int, float)): return True, float(v)
        return False, v
    if typ == "datetime": return isinstance(v, dt.datetime), v
    if typ == "date": return (isinstance(v, dt.date) and not isinstance(v, dt.datetime)), v
    if typ == "time": return isinstance(v, dt.time), v
    return False, v


def _conform(v, typ, array=False, nullable=False):
    if v is None: return nullable, v
    if array:
        if not isinstance(v, list) or isinstance(v, Table): return False, v
        out = []
        for x in v:
            ok, y = _conform_base(x, typ)
            if not ok: return False, v
            out.append(y)
        return True, out
    return _conform_base(v, typ)


# ───────────────────────── المحلّل ─────────────────────────
KEY_RE = re.compile(r"[^\W\d][\w\-]*")
TOKEN_RE = re.compile(r"[A-Za-z0-9_+\-.:]+")
DATE_RE = re.compile(r"^(\d{4})-(\d{2})-(\d{2})$")
TIME_RE = re.compile(r"^(\d{2}):(\d{2})(?::(\d{2})(?:\.(\d{1,9}))?)?$")
DT_RE = re.compile(r"^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})(?::(\d{2})(?:\.(\d{1,9}))?)?(Z|[+-]\d{2}:\d{2})?$")
DEC_RE = re.compile(r"^[+-]?(?:0|[1-9](?:_?\d)*)$")
HEX_RE = re.compile(r"^0x[0-9A-Fa-f](?:_?[0-9A-Fa-f])*$")
OCT_RE = re.compile(r"^0o[0-7](?:_?[0-7])*$")
BIN_RE = re.compile(r"^0b[01](?:_?[01])*$")
FLT_RE = re.compile(r"^[+-]?(?:0|[1-9](?:_?\d)*)(?:\.\d(?:_?\d)*)?(?:[eE][+-]?\d(?:_?\d)*)?$")
HEADER_RE = re.compile(r"channel47[ \t]+(\d+)\.(\d+)[ \t]*(?:#[^\n]*)?(?:\n|$)")
ESC = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "0": "\0", "b": "\b", "f": "\f"}


class _Parser:
    def __init__(self, text, file):
        if text.startswith("\ufeff"): text = text[1:]
        self.s = text.replace("\r\n", "\n").replace("\r", "\n")
        self.n, self.i, self.file = len(self.s), 0, file
        self.explicit, self.frozen = set(), set()
        self.root = {}

    # --- أدوات ---
    def err(self, code, at=None, **kw):
        at = self.i if at is None else at
        at = min(at, self.n)
        line = self.s.count("\n", 0, at) + 1
        ls = self.s.rfind("\n", 0, at) + 1
        le = self.s.find("\n", at)
        le = self.n if le < 0 else le
        raise Channel47Error(code, _m(code, **kw), line, at - ls + 1, self.s[ls:le], self.file)

    def peek(self, k=0):
        j = self.i + k
        return self.s[j] if j < self.n else ""

    def ws(self):
        while self.i < self.n and self.s[self.i] in " \t": self.i += 1

    def comment(self):
        if self.peek() == "#":
            j = self.s.find("\n", self.i)
            self.i = self.n if j < 0 else j

    def blank(self):
        while self.i < self.n:
            c = self.s[self.i]
            if c in " \t\n": self.i += 1
            elif c == "#": self.comment()
            else: break

    def eol(self):
        self.ws(); self.comment()
        if self.i < self.n:
            if self.s[self.i] == "\n": self.i += 1
            else: self.err("C47010", what=_("«%s» بعد نهاية العبارة", "'%s' after end of statement") % self.s[self.i])

    def expect(self, ch):
        if self.peek() != ch:
            got = self.peek() or _("نهاية الملف", "end of file")
            self.err("C47010", what=_("متوقع «%s» لكن وُجد «%s»", "expected '%s' but found '%s'") % (ch, got))
        self.i += 1

    # --- المفاتيح والنصوص ---
    def keypath(self):
        segs = []
        while True:
            c = self.peek()
            if c in ('"', "'"): segs.append(self.string(c))
            else:
                m = KEY_RE.match(self.s, self.i)
                if not m: self.err("C47010", what=_("اسم مفتاح غير صالح", "invalid key name"))
                segs.append(m.group()); self.i = m.end()
            if self.peek() == ".": self.i += 1; continue
            return segs

    def string(self, q):
        start = self.i
        triple = self.s.startswith(q * 3, self.i)
        if triple:
            self.i += 3
            if self.peek() == "\n": self.i += 1
        else:
            self.i += 1
        out = []
        while True:
            if self.i >= self.n: self.err("C47020", at=start)
            c = self.s[self.i]
            if triple:
                if self.s.startswith(q * 3, self.i): self.i += 3; break
            else:
                if c == q: self.i += 1; break
                if c == "\n": self.err("C47020", at=start)
            if q == '"' and c == "\\":
                e = self.peek(1)
                if e in ESC: out.append(ESC[e]); self.i += 2
                elif e == "u" and self.peek(2) == "{":
                    j = self.s.find("}", self.i)
                    h = self.s[self.i + 3:j] if j > 0 else ""
                    if not re.fullmatch(r"[0-9A-Fa-f]{1,6}", h) or int(h, 16) > 0x10FFFF or 0xD800 <= int(h, 16) <= 0xDFFF:
                        self.err("C47023", what="\\u{" + h + "}")
                    out.append(chr(int(h, 16))); self.i = j + 1
                else:
                    self.err("C47023", what="\\" + e)
            else:
                out.append(c); self.i += 1
        return "".join(out)

    # --- القيم ---
    def value(self, in_table=False):
        c = self.peek()
        if c == "": self.err("C47010", what=_("نهاية الملف", "end of file"))
        if c in ('"', "'"): return self.string(c)
        if c == "[": return self.array()
        if c == "{": return self.obj()
        m = TOKEN_RE.match(self.s, self.i)
        if not m:
            self.err("C47010", what=_("«%s» حيث تُنتظر قيمة", "'%s' where a value is expected") % c)
        tok, at = m.group(), self.i
        self.i = m.end()
        if tok == "true": return True
        if tok == "false": return False
        if tok == "nil": return None
        if tok == "_" and in_table: return DEFAULT
        return self.scalar(tok, at)

    def scalar(self, tok, at):
        try:
            m = DT_RE.match(tok)
            if m:
                y, mo, d, h, mi, s, fr, tz = m.groups()
                tzi = None
                if tz == "Z": tzi = dt.timezone.utc
                elif tz:
                    sign = 1 if tz[0] == "+" else -1
                    tzi = dt.timezone(sign * dt.timedelta(hours=int(tz[1:3]), minutes=int(tz[4:6])))
                return dt.datetime(int(y), int(mo), int(d), int(h), int(mi), int(s or 0), int((fr or "0").ljust(6, "0")[:6]), tzi)
            m = DATE_RE.match(tok)
            if m: return dt.date(*map(int, m.groups()))
            m = TIME_RE.match(tok)
            if m:
                h, mi, s, fr = m.groups()
                return dt.time(int(h), int(mi), int(s or 0), int((fr or "0").ljust(6, "0")[:6]))
        except ValueError:
            self.err("C47022", at=at, tok=tok)
        if DEC_RE.match(tok): return self._int(int(tok.replace("_", "")), tok, at)
        if HEX_RE.match(tok): return self._int(int(tok[2:].replace("_", ""), 16), tok, at)
        if OCT_RE.match(tok): return self._int(int(tok[2:].replace("_", ""), 8), tok, at)
        if BIN_RE.match(tok): return self._int(int(tok[2:].replace("_", ""), 2), tok, at)
        if FLT_RE.match(tok):
            v = float(tok.replace("_", ""))
            if math.isinf(v): self.err("C47024", at=at, tok=tok, why=_("خارج المدى", "out of range"))
            return v
        if re.match(r"^[+-]?\d", tok):
            self.err("C47024", at=at, tok=tok, why=_("أصفار بادئة أو صيغة خاطئة", "leading zeros or malformed"))
        self.err("C47021", at=at, tok=tok)

    def _int(self, v, tok, at):
        if not INT_MIN <= v <= INT_MAX: self.err("C47024", at=at, tok=tok, why=_("خارج مدى 64 بت", "outside 64-bit range"))
        return v

    def array(self):
        self.i += 1
        out = []
        while True:
            self.blank()
            if self.peek() == "]": self.i += 1; return out
            out.append(self.value())
            self.blank()
            if self.peek() == ",": self.i += 1
            elif self.peek() != "]":
                self.err("C47010", what=_("متوقع «,» أو «]» في المصفوفة", "expected ',' or ']' in array"))

    def obj(self):
        self.i += 1
        out = {}
        while True:
            self.blank()
            if self.peek() == "}": self.i += 1; return out
            at = self.i
            segs = self.keypath()
            if len(segs) != 1: self.err("C47010", at=at, what=_("مفتاح منقّط داخل كائن مضمَّن", "dotted key inside inline object"))
            if segs[0] in out: self.err("C47013", at=at, path=segs[0])
            self.ws(); self.expect("="); self.ws()
            out[segs[0]] = self.value()
            self.blank()
            if self.peek() == ",": self.i += 1
            elif self.peek() != "}":
                self.err("C47010", what=_("متوقع «,» أو «}» في الكائن", "expected ',' or '}' in object"))

    # --- الجداول ---
    def columns(self):
        self.expect("(")
        cols, seen = [], set()
        while True:
            self.blank()
            if self.peek() == ")": self.i += 1; return cols
            at = self.i
            if self.peek() in ('"', "'"): name = self.string(self.peek())
            else:
                m = KEY_RE.match(self.s, self.i)
                if not m: self.err("C47010", what=_("اسم عمود غير صالح", "invalid column name"))
                name = m.group(); self.i = m.end()
            if name in seen: self.err("C47031", at=at, name=name)
            seen.add(name)
            self.ws(); self.expect(":"); self.ws()
            m = re.compile(r"[A-Za-z_]\w*").match(self.s, self.i)
            if not m: self.err("C47010", what=_("نوع العمود مفقود", "column type missing"))
            typ = m.group()
            if typ not in TYPES: self.err("C47030", typ=typ)
            self.i = m.end()
            arr = nul = False
            if self.s.startswith("[]", self.i): arr = True; self.i += 2
            if self.peek() == "?": nul = True; self.i += 1
            col = Column(name, typ, arr, nul)
            self.ws()
            if self.peek() == "=":
                self.i += 1; self.ws()
                vat = self.i
                dv = self.value()
                ok, dv2 = _conform(dv, typ, arr, nul)
                if not ok: self.err("C47042", at=vat, name=name, want=col.spec(), got=type_name(dv))
                col.has_default, col.default = True, dv2
            cols.append(col)
            self.blank()
            if self.peek() == ",": self.i += 1
            elif self.peek() != ")":
                self.err("C47010", what=_("متوقع «,» أو «)» في تعريف الأعمدة", "expected ',' or ')' in column list"))

    def table(self):
        start = self.i
        self.i += 5; self.ws()
        segs = self.keypath()
        self.ws()
        cols = self.columns()
        self.eol()
        rows = []
        while True:
            self.blank()
            if self.i >= self.n: self.err("C47043", at=start, name=".".join(segs))
            if re.match(r"end(?![\w\-])", self.s[self.i:self.i + 4]):
                self.i += 3; self.eol(); break
            row_at, cells = self.i, []
            while True:
                self.ws()
                cells.append((self.i, self.value(in_table=True)))
                self.ws()
                if self.peek() == ",":
                    self.i += 1; self.ws()
                    if self.peek() in ("\n", "#", ""): self.blank()
                    continue
                break
            self.eol()
            rows.append(self.make_row(cells, cols, row_at))
        return segs, Table(cols, rows)

    def make_row(self, cells, cols, row_at):
        if len(cells) > len(cols): self.err("C47040", at=row_at, got=len(cells), want=len(cols))
        row = {}
        for k, col in enumerate(cols):
            if k < len(cells) and cells[k][1] is not DEFAULT:
                at, v = cells[k]
            else:
                at = cells[k][0] if k < len(cells) else row_at
                if col.has_default: v = copy.deepcopy(col.default)
                elif col.nullable: v = None
                else: self.err("C47041", at=at, name=col.name)
            ok, v2 = _conform(v, col.type, col.array, col.nullable)
            if not ok: self.err("C47042", at=at, name=col.name, want=col.spec(), got=type_name(v))
            row[col.name] = v2
        return row

    # --- المستند ---
    def walk(self, path, at):
        node = self.root
        for k, seg in enumerate(path):
            pre = tuple(path[:k + 1])
            if seg not in node: node[seg] = {}
            elif not isinstance(node[seg], dict) or isinstance(node[seg], Table) or pre in self.frozen:
                self.err("C47011", at=at, path=".".join(pre))
            node = node[seg]
        return node

    def parse(self):
        cur_path = ()
        self.blank()
        m = HEADER_RE.match(self.s, self.i)
        if m:
            if int(m.group(1)) != SPEC_VERSION[0]: self.err("C47002", ver=f"{m.group(1)}.{m.group(2)}")
            self.i = m.end()
        while True:
            self.blank()
            if self.i >= self.n: return self.root
            c, at = self.peek(), self.i
            if c == "[":
                if self.peek(1) == "[": self.err("C47014")
                self.i += 1; self.ws()
                segs = self.keypath(); self.ws(); self.expect("]"); self.eol()
                path = tuple(segs)
                if path in self.explicit or path in self.frozen: self.err("C47012", at=at, path=".".join(path))
                self.walk(path, at)
                self.explicit.add(path); cur_path = path
            elif re.match(r"table[ \t]+[^=\s]", self.s[self.i:self.i + 8]):
                segs, tbl = self.table()
                full = cur_path + tuple(segs)
                parent = self.walk(full[:-1], at)
                if full[-1] in parent: self.err("C47013", at=at, path=".".join(full))
                parent[full[-1]] = tbl; self.frozen.add(full)
            else:
                segs = self.keypath()
                full = cur_path + tuple(segs)
                self.ws(); self.expect("="); self.ws()
                parent = self.walk(full[:-1], at)
                if full[-1] in parent: self.err("C47013", at=at, path=".".join(full))
                v = self.value()
                parent[full[-1]] = v
                if isinstance(v, dict): self.frozen.add(full)
                self.eol()


def loads(text, file="<string>"):
    return _Parser(text, file).parse()


def load(path):
    with open(path, encoding="utf-8") as f:
        return loads(f.read(), path)


# ───────────────────────── الكتابة القانونية ─────────────────────────
BARE_RE = re.compile(r"^[^\W\d][\w\-]*$")
_ESC_OUT = {"\n": "\\n", "\t": "\\t", "\r": "\\r", '"': '\\"', "\\": "\\\\", "\0": "\\0", "\b": "\\b", "\f": "\\f"}


def _key(k):
    return k if BARE_RE.match(k) else _str(k)


def _str(s):
    out = []
    for ch in s:
        if ch in _ESC_OUT: out.append(_ESC_OUT[ch])
        elif ord(ch) < 0x20 or ch == "\x7f": out.append("\\u{%x}" % ord(ch))
        else: out.append(ch)
    return '"' + "".join(out) + '"'


def _iso(v):
    r = v.isoformat()
    return r[:-6] + "Z" if r.endswith("+00:00") else r


def _inline(v):
    if v is None: return "nil"
    if isinstance(v, bool): return "true" if v else "false"
    if isinstance(v, int): return str(v)
    if isinstance(v, float):
        if math.isnan(v) or math.isinf(v): raise ValueError("NaN/Infinity لا يُمثَّلان في channel47")
        r = repr(v)
        return r if any(c in r for c in ".eE") else r + ".0"
    if isinstance(v, str): return _str(v)
    if isinstance(v, (dt.datetime, dt.date, dt.time)): return _iso(v)
    if isinstance(v, (list, tuple)): return "[" + ", ".join(_inline(x) for x in v) + "]"
    if isinstance(v, dict):
        if not v: return "{}"
        return "{ " + ", ".join(f"{_key(k)} = {_inline(x)}" for k, x in v.items()) + " }"
    raise TypeError(f"نوع غير مدعوم: {type(v).__name__}")


def _value(v, indent=0):
    one = _inline(v)
    if isinstance(v, (list, tuple)) and v and (len(one) > 72 or any(isinstance(x, (dict, list)) for x in v)):
        pad = "  " * (indent + 1)
        return "[\n" + "".join(f"{pad}{_value(x, indent + 1)},\n" for x in v) + "  " * indent + "]"
    return one


def _table_block(path, tbl, lines):
    cols = tbl.columns
    def cdef(c):
        s = f"{_key(c.name)}: {c.spec()}"
        return s + (f" = {_inline(c.default)}" if c.has_default else "")
    lines.append(f"table {path}({', '.join(cdef(c) for c in cols)})")
    grid = [[_inline(r[c.name]) for c in cols] for r in tbl]
    widths = [max((len(g[k]) for g in grid), default=0) for k in range(len(cols))]
    for g in grid:
        cells = [(g[k] + ",").ljust(widths[k] + 1) if k < len(cols) - 1 else g[k] for k in range(len(cols))]
        lines.append("  " + " ".join(cells).rstrip())
    lines.append("end")


def dumps(doc, header=True):
    lines = ["channel47 %d.%d" % SPEC_VERSION, ""] if header else []

    def emit(d, path):
        scal = [(k, v) for k, v in d.items() if not isinstance(v, Table) and not (isinstance(v, dict) and v)]
        tabs = [(k, v) for k, v in d.items() if isinstance(v, Table)]
        subs = [(k, v) for k, v in d.items() if isinstance(v, dict) and v and not isinstance(v, Table)]
        for k, v in scal: lines.append(f"{_key(k)} = {_value(v)}")
        for k, v in tabs:
            if lines and lines[-1] != "": lines.append("")
            _table_block(_key(k), v, lines)
        for k, v in subs:
            p = path + [k]
            if lines and lines[-1] != "": lines.append("")
            lines.append("[" + ".".join(_key(x) for x in p) + "]")
            emit(v, p)

    emit(doc, [])
    while lines and lines[-1] == "": lines.pop()
    return "\n".join(lines) + "\n"


def fmt(text, file="<string>"):
    return dumps(loads(text, file))


# ───────────────────────── JSON ─────────────────────────
def _jdefault(o):
    if isinstance(o, (dt.datetime, dt.date, dt.time)): return _iso(o)
    raise TypeError(type(o).__name__)


def to_json(doc, indent=2):
    return json.dumps(doc, ensure_ascii=False, indent=indent, default=_jdefault) + "\n"


def from_json(text):
    return json.loads(text)


# ───────────────────────── أدوات ─────────────────────────
def get(doc, path, default=None):
    node = doc
    for seg in path.split("."):
        if isinstance(node, dict) and seg in node: node = node[seg]
        else: return default
    return node


def merge(base, over):
    """دمج عميق: القواميس تُدمج، وغيرها (بما فيه الجداول والمصفوفات) يُستبدل."""
    if not (isinstance(base, dict) and isinstance(over, dict)): return copy.deepcopy(over)
    out = {k: copy.deepcopy(v) for k, v in base.items()}
    for k, v in over.items():
        out[k] = merge(out[k], v) if k in out and isinstance(out[k], dict) and isinstance(v, dict) else copy.deepcopy(v)
    return out


# ───────────────────────── التحقق من البيان (manifest) ─────────────────────────
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$")
COMMON_OPT = {"description": "text", "license": "text", "authors": "text[]", "homepage": "text", "keywords": "text[]",
              "released": "date|datetime", "permissions": "text[]", "stable": "bool", "rin": "text"}
KINDS = {
    "library": {"required": {"name": "text", "version": "text", "entry": "text"},
                "optional": {**COMMON_OPT, "exports": "text[]"},
                "tables": {"deps": ["name", "version"]}},
    "package": {"required": {"name": "text", "version": "text"},
                "optional": {**COMMON_OPT, "files": "text[]", "entry": "text"},
                "tables": {"deps": ["name", "version"]}},
    "tool": {"required": {"name": "text", "version": "text", "bin": "text"},
             "optional": {**COMMON_OPT, "entry": "text"},
             "tables": {"commands": ["name", "summary"], "deps": ["name", "version"]}},
}


class Issue:
    def __init__(self, level, code, path, message):
        self.level, self.code, self.path, self.message = level, code, path, message
    def __str__(self): return f"{self.level} {self.code}: {self.message}"
    def __repr__(self): return f"Issue({self.level},{self.code},{self.path})"


def _matches(v, spec):
    for alt in spec.split("|"):
        arr = alt.endswith("[]")
        ok, _v = _conform(v, alt[:-2] if arr else alt, arr, False)
        if ok: return True
    return False


def validate(doc, kind=None):
    issues = []
    def add(level, code, path, **kw): issues.append(Issue(level, code, path, _m(code, path=path, **kw)))
    kind = kind or doc.get("kind")
    if kind is None:
        add("error", "C47050", "kind"); return issues
    if not isinstance(kind, str) or kind not in KINDS:
        add("error", "C47054", "kind", kind=kind); return issues
    spec = KINDS[kind]
    for key, typ in spec["required"].items():
        if key not in doc: add("error", "C47050", key)
        elif not _matches(doc[key], typ): add("error", "C47051", key, want=typ, got=type_name(doc[key]))
    for key, typ in spec["optional"].items():
        if key in doc and not _matches(doc[key], typ): add("error", "C47051", key, want=typ, got=type_name(doc[key]))
    if isinstance(doc.get("name"), str) and not ID_RE.match(doc["name"]):
        add("error", "C47052", "name", why=_("يجب أن يطابق ^[a-z0-9][a-z0-9._-]{0,63}$", "must match ^[a-z0-9][a-z0-9._-]{0,63}$"))
    if isinstance(doc.get("version"), str) and not SEMVER_RE.match(doc["version"]):
        add("error", "C47052", "version", why=_("ليس إصداراً دلالياً X.Y.Z", "not a semantic version X.Y.Z"))
    for tname, need in spec["tables"].items():
        t = doc.get(tname)
        if t is None: continue
        if not isinstance(t, Table):
            add("error", "C47051", tname, want="table", got=type_name(t)); continue
        have = {c.name for c in t.columns}
        for c in need:
            if c not in have: add("error", "C47050", f"{tname}.{c}")
    known = {"kind", *spec["required"], *spec["optional"], *spec["tables"]}
    for key in doc:
        if key not in known and not isinstance(doc[key], dict):
            add("warning", "C47053", key)
    return issues


# ───────────────────────── سطر الأوامر ─────────────────────────
def main(argv=None):
    global LANG
    ap = argparse.ArgumentParser(prog="channel47", description="channel47 — لغة وصف بيانات وإعدادات")
    ap.add_argument("--lang", choices=["ar", "en"], default="ar")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("check"); p.add_argument("file"); p.add_argument("--kind", choices=list(KINDS)); p.add_argument("--syntax-only", action="store_true")
    p = sub.add_parser("fmt"); p.add_argument("file"); p.add_argument("-w", action="store_true")
    p = sub.add_parser("json"); p.add_argument("file")
    p = sub.add_parser("from-json"); p.add_argument("file")
    p = sub.add_parser("get"); p.add_argument("file"); p.add_argument("path")
    p = sub.add_parser("merge"); p.add_argument("base"); p.add_argument("over")
    a = ap.parse_args(argv)
    LANG = a.lang
    try:
        if a.cmd == "check":
            doc = load(a.file)
            if a.syntax_only or (a.kind is None and "kind" not in doc):
                print(f"{a.file}: OK"); return 0
            issues = validate(doc, a.kind)
            for i in issues: print(f"{a.file}: {i}")
            if any(i.level == "error" for i in issues): return 1
            print(f"{a.file}: OK"); return 0
        if a.cmd == "fmt":
            with open(a.file, encoding="utf-8") as f: out = fmt(f.read(), a.file)
            if a.w:
                with open(a.file, "w", encoding="utf-8", newline="\n") as f: f.write(out)
            else: sys.stdout.write(out)
            return 0
        if a.cmd == "json": sys.stdout.write(to_json(load(a.file))); return 0
        if a.cmd == "from-json":
            with open(a.file, encoding="utf-8") as f: sys.stdout.write(dumps(from_json(f.read())))
            return 0
        if a.cmd == "get":
            v = get(load(a.file), a.path, DEFAULT)
            if v is DEFAULT: print(_("غير موجود", "not found"), file=sys.stderr); return 1
            print(v if isinstance(v, str) else to_json(v).rstrip()); return 0
        if a.cmd == "merge": sys.stdout.write(dumps(merge(load(a.base), load(a.over)))); return 0
    except Channel47Error as e:
        print(e, file=sys.stderr); return 2
    except (OSError, ValueError, TypeError) as e:
        print(f"error: {e}", file=sys.stderr); return 2


if __name__ == "__main__":
    sys.exit(main())

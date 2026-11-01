import datetime as dt, os, subprocess, sys, unittest
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import channel47 as c
EX = os.path.join(ROOT, "examples", "channel47")


def err(text):
    with unittest.TestCase().assertRaises(c.Channel47Error) as cm:
        c.loads(text)
    return cm.exception


class Values(unittest.TestCase):
    def test_scalars(self):
        d = c.loads('a = "x\\n\\u{41}"\nb = \'raw\\n\'\nc = 1_000\nd = -7\ne = 0xff\nf = 0b101\ng = 0o17\nh = 1.5e3\ni = true\nj = false\nk = nil\n')
        self.assertEqual(d, {"a": "x\nA", "b": "raw\\n", "c": 1000, "d": -7, "e": 255, "f": 5, "g": 15, "h": 1500.0, "i": True, "j": False, "k": None})
        self.assertIsInstance(d["h"], float)

    def test_dates(self):
        d = c.loads("a = 2026-10-15\nb = 09:30:15.5\nc = 2026-10-15T09:30\nd = 2026-10-15T09:30:00Z\ne = 2026-10-15T09:30:00-05:30\n")
        self.assertEqual(d["a"], dt.date(2026, 10, 15))
        self.assertEqual(d["b"], dt.time(9, 30, 15, 500000))
        self.assertIsNone(d["c"].tzinfo)
        self.assertEqual(d["d"].utcoffset(), dt.timedelta(0))
        self.assertEqual(d["e"].utcoffset(), -dt.timedelta(hours=5, minutes=30))

    def test_multiline_and_arrays(self):
        d = c.loads('s = """\nx\n  y"""\nl = [1,\n 2, # c\n [3, "a"], {k = 1},\n]\n')
        self.assertEqual(d["s"], "x\n  y")
        self.assertEqual(d["l"], [1, 2, [3, "a"], {"k": 1}])

    def test_sections_dotted_unicode(self):
        d = c.loads('[a.b]\nx.y = 1\n["م ف"]\nمفتاح = 2\n')
        self.assertEqual(d, {"a": {"b": {"x": {"y": 1}}}, "م ف": {"مفتاح": 2}})

    def test_bom_crlf_header(self):
        self.assertEqual(c.loads("\ufeffchannel47 1.0\r\na = 1\r\n"), {"a": 1})


class Tables(unittest.TestCase):
    def test_table(self):
        d = c.loads('table t(n: text, v: int = 5, o: bool?)\n  "a", 1, true\n  "b"\n  "c", _, false\n  "d",\n    9\nend\n')
        t = d["t"]
        self.assertIsInstance(t, c.Table)
        self.assertEqual(list(t), [{"n": "a", "v": 1, "o": True}, {"n": "b", "v": 5, "o": None},
                                    {"n": "c", "v": 5, "o": False}, {"n": "d", "v": 9, "o": None}])

    def test_float_coerces_int(self):
        t = c.loads("table t(x: float)\n 3\nend\n")["t"]
        self.assertIsInstance(t[0]["x"], float)

    def test_key_named_table(self):
        self.assertEqual(c.loads("table = 5\n"), {"table": 5})


class Errors(unittest.TestCase):
    def test_codes(self):
        cases = {
            "C47010": "a = \nb", "C47011": "a = 1\n[a.b]\n", "C47012": "[a]\n[a]\n", "C47013": "a = 1\na = 2\n",
            "C47014": "[[a]]\n", "C47020": 'a = "x\n', "C47021": "a = hello\n", "C47022": "a = 2026-13-01\n",
            "C47023": 'a = "\\q"\n', "C47024": "a = 007\n", "C47030": "table t(a: str)\nend\n",
            "C47031": "table t(a: int, a: int)\nend\n", "C47040": "table t(a: int)\n 1, 2\nend\n",
            "C47041": "table t(a: int)\n _\nend\n", "C47042": 'table t(a: int)\n "x"\nend\n',
            "C47043": "table t(a: int)\n 1\n", "C47002": "channel47 2.0\n",
        }
        for code, text in cases.items():
            self.assertEqual(err(text).code, code, text)

    def test_position(self):
        e = err("a = 1\nb = oops\n")
        self.assertEqual((e.line, e.col, e.code), (2, 5, "C47021"))
        self.assertIn("^", str(e))

    def test_inline_object_frozen(self):
        self.assertEqual(err("a = {x = 1}\n[a]\n").code, "C47012")
        self.assertEqual(err("a = {x = 1}\na.y = 2\n").code, "C47011")

    def test_int_range(self):
        self.assertEqual(err("a = 9223372036854775808\n").code, "C47024")
        self.assertEqual(c.loads("a = 9223372036854775807\n")["a"], 2 ** 63 - 1)


class RoundTrip(unittest.TestCase):
    def test_examples_roundtrip_and_idempotent(self):
        for f in sorted(os.listdir(EX)):
            d = c.load(os.path.join(EX, f))
            out = c.dumps(d)
            self.assertEqual(c.loads(out), d, f)
            self.assertEqual(c.dumps(c.loads(out)), out, f)

    def test_dump_edge(self):
        d = {"a b": 'q"\\\n\t', "n": None, "f": 2.0, "big": [{"x": 1}, {"y": [1, 2]}], "e": {}, "tz": dt.datetime(2026, 1, 1, tzinfo=dt.timezone.utc)}
        self.assertEqual(c.loads(c.dumps(d)), d)
        with self.assertRaises(ValueError): c.dumps({"x": float("nan")})

    def test_json(self):
        import json
        d = c.load(os.path.join(EX, "settings.ch47"))
        j = json.loads(c.to_json(d))
        self.assertEqual(j["ui"]["quiet_from"], "22:30:00")
        self.assertEqual(c.loads(c.dumps(j))["net"]["retries"], 3)


class Validation(unittest.TestCase):
    def codes(self, doc, kind=None):
        return [(i.level, i.code) for i in c.validate(doc, kind)]

    def test_examples_valid(self):
        for f in ("rin-math.library.ch47", "mypkg.package.ch47", "rin-fmt.tool.ch47"):
            self.assertEqual(self.codes(c.load(os.path.join(EX, f))), [], f)

    def test_errors(self):
        d = c.loads('kind = "library"\nname = "Bad Name"\nversion = "1.0"\nentry = 5\nzzz = 1\n')
        got = self.codes(d)
        self.assertIn(("error", "C47052"), got)
        self.assertIn(("error", "C47051"), got)
        self.assertIn(("warning", "C47053"), got)
        self.assertEqual(self.codes({}), [("error", "C47050")])
        self.assertEqual(self.codes({"kind": "x"}), [("error", "C47054")])

    def test_missing_column(self):
        d = c.loads('kind="package"\nname="a"\nversion="1.0.0"\ntable deps(name: text)\n "x"\nend\n')
        self.assertEqual(self.codes(d), [("error", "C47050")])


class Tools(unittest.TestCase):
    def test_merge_get(self):
        a, b = c.loads("[x]\na = 1\nb = [1]\n"), c.loads("[x]\nb = [2]\nc = 3\n")
        m = c.merge(a, b)
        self.assertEqual(m, {"x": {"a": 1, "b": [2], "c": 3}})
        self.assertEqual(c.get(m, "x.c"), 3)
        self.assertIsNone(c.get(m, "x.zz"))

    def test_cli(self):
        exe = [sys.executable, os.path.join(ROOT, "tools", "channel47.py")]
        r = subprocess.run(exe + ["check", os.path.join(EX, "rin-math.library.ch47")], capture_output=True, text=True)
        self.assertEqual((r.returncode, r.stdout.strip().endswith("OK")), (0, True))
        r = subprocess.run(exe + ["json", os.path.join(EX, "mypkg.package.ch47")], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0)
        r = subprocess.run(exe + ["--lang", "en", "check", "/nonexistent.ch47"], capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=1)

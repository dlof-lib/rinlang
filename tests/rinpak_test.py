#!/usr/bin/env python3
"""اختبارات صيغة RinPak عبر المرجع tools/rinpak.py (نفس قواعد RinPak.kt)."""
import os, sys, tempfile, zipfile, json, shutil, unittest, hashlib
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import rinpak as R

def rewrite(src, dst, mutate):
    with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, "w") as zo:
        mutate(zi, zo)

class T(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.mkdtemp(); p = os.path.join(self.t, "proj"); os.makedirs(p + "/lib"); os.makedirs(p + "/.rin_state")
        open(p + "/main.rin", "w").write("print(\"hi\")\n"); open(p + "/lib/m.og.rin", "w").write("fun a(){}\n")
        open(p + "/logo.png", "wb").write(os.urandom(300)); open(p + "/.rin_state/x", "w").write("hidden")
        open(p + "/اختبار.rin", "w", encoding="utf-8").write("x")
        self.p, self.pak = p, os.path.join(self.t, "a.rinpak")
        R.pack(p, self.pak, "Demo", "container")
    def tearDown(self): shutil.rmtree(self.t)

    def test_layout_and_order(self):
        with zipfile.ZipFile(self.pak) as z:
            names = z.namelist()
            self.assertEqual(names[:2], ["mimetype", "META-INF/manifest.json"])
            self.assertEqual(z.getinfo("mimetype").compress_type, zipfile.ZIP_STORED)
            self.assertEqual(z.read("mimetype"), b"application/vnd.rin.pak")
            self.assertNotIn("content/.rin_state/x", names)
            self.assertEqual(z.getinfo("content/logo.png").compress_type, zipfile.ZIP_STORED)
            self.assertEqual(z.getinfo("content/main.rin").compress_type, zipfile.ZIP_DEFLATED)

    def test_roundtrip(self):
        d = os.path.join(self.t, "out"); m = R.read(self.pak, d)
        self.assertEqual(m["name"], "Demo")
        for rel in ("main.rin", "lib/m.og.rin", "logo.png", "اختبار.rin"):
            self.assertEqual(open(os.path.join(self.p, rel), "rb").read(), open(os.path.join(d, rel), "rb").read())
        self.assertFalse(os.path.exists(os.path.join(d, ".rin_state")))

    def test_deterministic(self):
        b = os.path.join(self.t, "b.rinpak"); R.pack(self.p, b, "Demo", "container", created=json.loads(zipfile.ZipFile(self.pak).read(R.MANIFEST))["created"])
        self.assertEqual(open(b, "rb").read(), open(self.pak, "rb").read())

    def test_tampered_content_rejected(self):
        def mut(zi, zo):
            for i in zi.infolist():
                data = zi.read(i)
                if i.filename == "content/main.rin": data = b"evil\n"
                zo.writestr(i, data, compress_type=i.compress_type)
        bad = os.path.join(self.t, "bad.rinpak"); rewrite(self.pak, bad, mut)
        with self.assertRaisesRegex(R.RinPakError, "mismatch"): R.read(bad)

    def test_extra_and_missing_files_rejected(self):
        def extra(zi, zo):
            for i in zi.infolist(): zo.writestr(i, zi.read(i), compress_type=i.compress_type)
            zo.writestr("content/extra.rin", b"x")
        b1 = os.path.join(self.t, "e.rinpak"); rewrite(self.pak, b1, extra)
        with self.assertRaisesRegex(R.RinPakError, "not in manifest"): R.read(b1)
        def miss(zi, zo):
            for i in zi.infolist():
                if i.filename != "content/logo.png": zo.writestr(i, zi.read(i), compress_type=i.compress_type)
        b2 = os.path.join(self.t, "m.rinpak"); rewrite(self.pak, b2, miss)
        with self.assertRaisesRegex(R.RinPakError, "missing"): R.read(b2)

    def test_zip_slip_rejected(self):
        def mut(zi, zo):
            for i in zi.infolist():
                data = zi.read(i)
                if i.filename == R.MANIFEST:
                    m = json.loads(data); f = m["integrity"]["files"][0]; f["path"] = "../evil"; m["integrity"]["root"] = R.root_digest(m["integrity"]["files"]); data = json.dumps(m).encode()
                zo.writestr(i, data, compress_type=i.compress_type)
        bad = os.path.join(self.t, "s.rinpak"); rewrite(self.pak, bad, mut)
        with self.assertRaisesRegex(R.RinPakError, "unsafe"): R.read(bad)

    def test_root_digest_forgery_rejected(self):
        def mut(zi, zo):
            for i in zi.infolist():
                data = zi.read(i)
                if i.filename == R.MANIFEST:
                    m = json.loads(data); m["integrity"]["root"] = "0" * 64; data = json.dumps(m).encode()
                zo.writestr(i, data, compress_type=i.compress_type)
        bad = os.path.join(self.t, "r.rinpak"); rewrite(self.pak, bad, mut)
        with self.assertRaisesRegex(R.RinPakError, "root"): R.read(bad)

    def test_not_rinpak_and_legacy_rejected(self):
        z = os.path.join(self.t, "plain.zip")
        with zipfile.ZipFile(z, "w") as f: f.writestr("rin.project.json", "{}"); f.writestr("main.rin", "")
        with self.assertRaisesRegex(R.RinPakError, "not a RinPak"): R.read(z)

    def test_wrong_order_rejected(self):
        def mut(zi, zo):
            infos = zi.infolist(); order = [infos[1], infos[0]] + infos[2:]
            for i in order: zo.writestr(i, zi.read(i), compress_type=i.compress_type)
        bad = os.path.join(self.t, "o.rinpak"); rewrite(self.pak, bad, mut)
        with self.assertRaises(R.RinPakError): R.read(bad)

    def test_missing_entry_point_rejected(self):
        os.remove(self.p + "/main.rin")
        with self.assertRaises(R.RinPakError): R.pack(self.p, os.path.join(self.t, "n.rinpak"))

class SignedTests(T):
    def setUp(self):
        super().setUp()
        self.key = os.path.join(self.t, "k.pem"); self.kid = R.keygen(self.key)
        self.spak = os.path.join(self.t, "s.rinpak")
        R.pack(self.p, self.spak, "Demo", "container", sign_key=self.key,
               meta={"version": "1.2.0", "author": "me", "permissions": ["network"], "minRinVersion": "1.0.0"})

    def test_signed_layout_and_verify(self):
        with zipfile.ZipFile(self.spak) as z: self.assertEqual(z.namelist()[:3], ["mimetype", R.MANIFEST, R.SIGFILE])
        m = R.read(self.spak); self.assertEqual(m["signature"]["keyId"], self.kid)
        self.assertEqual((m["version"], m["author"], m["permissions"]), ("1.2.0", "me", ["network"]))
        self.assertIsNone(R.read(self.pak)["signature"])

    def _mut(self, fn, name="x.rinpak"):
        out = os.path.join(self.t, name)
        def mut(zi, zo):
            for i in zi.infolist():
                data = fn(i.filename, zi.read(i))
                if data is not None: zo.writestr(i, data, compress_type=i.compress_type)
        rewrite(self.spak, out, mut); return out

    def test_manifest_edit_with_recomputed_root_breaks_signature(self):
        def f(n, d):
            if n == R.MANIFEST:
                m = json.loads(d); m["name"] = "Evil"; return json.dumps(m, indent=2).encode()
            return d
        with self.assertRaisesRegex(R.RinPakError, "invalid digital signature"): R.read(self._mut(f))

    def test_swapped_key_rejected(self):
        k2 = os.path.join(self.t, "k2.pem"); R.keygen(k2)
        other = os.path.join(self.t, "o.rinpak"); R.pack(self.p, other, "Demo", "container", sign_key=k2)
        evil_sig = zipfile.ZipFile(other).read(R.SIGFILE)
        with self.assertRaisesRegex(R.RinPakError, "signature"): R.read(self._mut(lambda n, d: evil_sig if n == R.SIGFILE else d))

    def test_forged_keyid_rejected(self):
        def f(n, d):
            if n == R.SIGFILE:
                o = json.loads(d); o["keyId"] = "0" * 16; return json.dumps(o).encode()
            return d
        with self.assertRaisesRegex(R.RinPakError, "keyId"): R.read(self._mut(f))

    def test_signature_in_wrong_position_rejected(self):
        with zipfile.ZipFile(self.spak) as zi, zipfile.ZipFile(os.path.join(self.t, "w.rinpak"), "w") as zo:
            infos = zi.infolist(); sig = infos[2]
            for i in infos[:2] + infos[3:] + [sig]: zo.writestr(i, zi.read(i), compress_type=i.compress_type)
        with self.assertRaisesRegex(R.RinPakError, "wrong position"): R.read(os.path.join(self.t, "w.rinpak"))

    def test_case_collision_rejected(self):
        open(self.p + "/Lib.txt", "w").write("a")
        try: open(self.p + "/lib.txt", "w").write("b")
        except OSError: self.skipTest("case-insensitive FS")
        if len(os.listdir(self.p)) and not (os.path.exists(self.p + "/Lib.txt") and os.path.exists(self.p + "/lib.txt") and "Lib.txt" in os.listdir(self.p) and "lib.txt" in os.listdir(self.p)):
            self.skipTest("case-insensitive FS")
        with self.assertRaisesRegex(R.RinPakError, "collision"): R.pack(self.p, os.path.join(self.t, "c.rinpak"))

    def test_unsafe_names_rejected(self):
        for bad in ("a.", "a ", "a|b", "a?b"): self.assertIsNone(R.safe_path(bad), bad)
        self.assertEqual(R.safe_path("ok/اختبار.rin"), "ok/اختبار.rin")

    def test_symlink_skipped(self):
        os.symlink("/etc/hostname", self.p + "/link.txt")
        out = os.path.join(self.t, "l.rinpak"); R.pack(self.p, out)
        self.assertNotIn("content/link.txt", zipfile.ZipFile(out).namelist())

if __name__ == "__main__": unittest.main(verbosity=2)

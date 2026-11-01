#!/usr/bin/env python3
"""اختبارات صيغة RinPak عبر المرجع tools/rinpak.py (نفس قواعد RinPak.kt)."""
import os, sys, io, tempfile, zipfile, json, shutil, unittest, hashlib
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


PNG = b"\x89PNG\r\n\x1a\n" + b"\x00" * 64

class IconTests(SignedTests):
    """RinPak v1: أيقونة موثّقة بـ SHA-256 (يغطيها التوقيع) وبموضع ثابت."""
    def setUp(self):
        super().setUp()
        self.ipak = os.path.join(self.t, "i.rinpak")
        R.pack(self.p, self.ipak, "Demo", "container", icon=PNG, sign_key=self.key, meta={"id": "demo", "tags": ["a", "b"]})

    def _mut(self, fn, name="y.rinpak", src=None):
        out = os.path.join(self.t, name)
        def mut(zi, zo):
            for i in zi.infolist():
                data = fn(i.filename, zi.read(i))
                if data is not None: zo.writestr(i, data, compress_type=i.compress_type)
        rewrite(src or self.ipak, out, mut); return out

    def test_icon_layout_and_roundtrip(self):
        with zipfile.ZipFile(self.ipak) as z: self.assertEqual(z.namelist()[:4], ["mimetype", R.MANIFEST, R.SIGFILE, R.ICON])
        m = R.read(self.ipak)
        self.assertEqual(m["_icon"], PNG); self.assertEqual(m["icon"]["sha256"], hashlib.sha256(PNG).hexdigest())
        self.assertEqual((m["id"], m["tags"]), ("demo", ["a", "b"]))
        self.assertIsNone(R.read(self.pak)["_icon"])

    def test_unsigned_icon_is_third(self):
        out = os.path.join(self.t, "u.rinpak"); R.pack(self.p, out, "Demo", icon=PNG)
        with zipfile.ZipFile(out) as z: self.assertEqual(z.namelist()[:3], ["mimetype", R.MANIFEST, R.ICON])
        self.assertEqual(R.read(out)["_icon"], PNG)

    def test_icon_tamper_rejected(self):
        with self.assertRaisesRegex(R.RinPakError, "icon SHA-256"): R.read(self._mut(lambda n, d: PNG + b"x" if n == R.ICON else d))

    def test_icon_wrong_position_rejected(self):
        with zipfile.ZipFile(self.ipak) as zi, zipfile.ZipFile(os.path.join(self.t, "w.rinpak"), "w") as zo:
            infos = zi.infolist(); ic = infos[3]
            for i in infos[:3] + infos[4:] + [ic]: zo.writestr(i, zi.read(i), compress_type=i.compress_type)
        with self.assertRaisesRegex(R.RinPakError, "wrong position"): R.read(os.path.join(self.t, "w.rinpak"))

    def test_undeclared_and_missing_icon_rejected(self):
        with zipfile.ZipFile(self.pak) as zi, zipfile.ZipFile(os.path.join(self.t, "n.rinpak"), "w") as zo:
            for k, i in enumerate(zi.infolist()):
                zo.writestr(i, zi.read(i), compress_type=i.compress_type)
                if k == 1: zo.writestr(R.ICON, PNG)
        with self.assertRaisesRegex(R.RinPakError, "not declared"): R.read(os.path.join(self.t, "n.rinpak"))
        with self.assertRaisesRegex(R.RinPakError, "declared but missing"): R.read(self._mut(lambda n, d: None if n == R.ICON else d))

    def test_bad_icons_refused_at_pack(self):
        with self.assertRaisesRegex(R.RinPakError, "PNG"): R.pack(self.p, os.path.join(self.t, "b.rinpak"), icon=b"GIF89a....")
        with self.assertRaisesRegex(R.RinPakError, "too large"): R.pack(self.p, os.path.join(self.t, "b.rinpak"), icon=PNG + b"\0" * R.MAX_ICON)

    def test_invalid_id_refused(self):
        for bad in ("Has Space", "-x", "ا" * 3, "x" * 70):
            with self.assertRaisesRegex(R.RinPakError, "invalid package id"): R.pack(self.p, os.path.join(self.t, "b.rinpak"), meta={"id": bad})

class AnalysisTests(unittest.TestCase):
    def mf(self, paths, **kw):
        files = [{"path": x, "size": 1, "sha256": "0" * 64} for x in paths]
        return {"integrity": {"files": files}, "signature": kw.pop("signature", None), "stats": {"bytes": 1}, **kw}
    def codes(self, a): return {f["code"] for f in a["findings"]}

    def test_clean_pack_is_low_risk(self):
        a = R.analyze(self.mf(["main.rin", "a.png"], signature={"keyId": "x"}))
        self.assertEqual((a["risk"], a["findings"], a["incompatible"]), ("low", [], False))
    def test_unsigned_is_info_only(self):
        a = R.analyze(self.mf(["main.rin"])); self.assertEqual((a["risk"], self.codes(a)), ("low", {"UNSIGNED"}))
    def test_native_code_high(self):
        a = R.analyze(self.mf(["main.rin", "lib/x.so", "y.DLL"])); self.assertEqual(a["risk"], "high"); self.assertIn("NATIVE_CODE", self.codes(a))
    def test_scripts_and_packages_medium(self):
        a = R.analyze(self.mf(["main.rin", "run.sh", "app.apk"], signature={"keyId": "x"})); self.assertEqual(a["risk"], "medium")
        self.assertEqual(self.codes(a), {"SHELL_SCRIPT", "EMBEDDED_PACKAGE"})
    def test_bidi_spoof_high(self):
        a = R.analyze(self.mf(["main.rin", "photo\u202egpj.exe"])); self.assertIn("BIDI_SPOOF", self.codes(a)); self.assertEqual(a["risk"], "high")
    def test_arabic_names_are_fine(self):
        a = R.analyze(self.mf(["main.rin", "ملف/اختبار.rin"], signature={"keyId": "x"})); self.assertEqual(a["risk"], "low")
    def test_permissions(self):
        a = R.analyze(self.mf(["main.rin"], permissions=["network", "SMS"], signature={"keyId": "x"})); self.assertEqual((a["risk"], self.codes(a)), ("medium", {"SENSITIVE_PERMISSIONS"}))
        a = R.analyze(self.mf(["main.rin"], permissions=["network"], signature={"keyId": "x"})); self.assertEqual((a["risk"], self.codes(a)), ("low", {"PERMISSIONS"}))
    def test_newer_rin_incompatible(self):
        m = self.mf(["main.rin"], minRinVersion="2.0.0", signature={"keyId": "x"})
        self.assertTrue(R.analyze(m, "1.9.9")["incompatible"]); self.assertFalse(R.analyze(m, "2.0.0")["incompatible"]); self.assertFalse(R.analyze(m)["incompatible"])
    def test_versions(self):
        self.assertTrue(R.version_gt("1.10.0", "1.9.9")); self.assertFalse(R.version_gt("1.0", "1.0.0")); self.assertFalse(R.version_gt("", "1.0"))
        self.assertEqual(R.parse_version("2.1.0-beta"), (2, 1))
    def test_make_id(self):
        self.assertEqual(R.make_id("My App_2"), "my-app-2"); self.assertTrue(R.ID_RE.match(R.make_id("اختبار"))); self.assertEqual(R.make_id("اختبار"), R.make_id("اختبار"))

class UrinTests(unittest.TestCase):
    def setUp(self): self.t = tempfile.mkdtemp()
    def tearDown(self): shutil.rmtree(self.t)

    def test_roundtrip_with_escapes_and_lists(self):
        m = {"type": "ui", "id": "a-b", "name": 'Q"uote \\ back', "description": "سطر1\nسطر2\ttab type=zzz", "tags": ["x", "y"],
             "permissions": ["network"], "dependencies": ["math", "@rin/strings"], "version": "1.2.3", "extra": {"future": "v"}}
        back = R.urin_parse(R.urin_render(m))
        for k in ("type", "id", "name", "description", "tags", "permissions", "dependencies", "version"): self.assertEqual(back[k], m[k], k)
        self.assertEqual(back["extra"], {"future": "v"}); self.assertEqual(back["schema"], R.URIN_SCHEMA)

    def test_old_regex_still_finds_type_first(self):
        # الصيغة القديمة في التطبيق: Regex(type\s*=\s*"?([A-Za-z0-9_]+)"?) أول تطابق في الملف.
        import re
        txt = R.urin_render({"type": "html", "name": "n", "description": "type=evil", "tags": ["type=x"]})
        self.assertEqual(re.search(r'type\s*=\s*"?([A-Za-z0-9_]+)"?', txt).group(1), "html")

    def test_legacy_raw_and_container_formats(self):
        self.assertEqual(R.urin_parse("type=container\n")["type"], "container")
        old = '# x\n@container=ProjectMeta\n    type="table";\n.end/container\n'
        m = R.urin_parse(old); self.assertEqual((m["type"], m["schema"]), ("table", 1))

    def test_garbage_is_safe(self):
        self.assertEqual(R.urin_parse("")["type"], "free"); self.assertEqual(R.urin_parse("\x00\xff garbage ===")["type"], "free")
        self.assertEqual(R.urin_parse('type="a b";')["type"], "free")

    def test_limits(self):
        m = R.urin_parse('type="free";\ntags="' + ",".join(f"t{i}" for i in range(40)) + '";\nname="' + "n" * 500 + '";')
        self.assertEqual(len(m["tags"]), 16); self.assertEqual(len(m["name"]), 64)
        extra = "\n".join(f'k{i}="v";' for i in range(100)); self.assertLessEqual(len(R.urin_parse(extra)["extra"]), 32)

    def test_read_write_files(self):
        self.assertIsNone(R.urin_read(self.t)); R.urin_write(self.t, {"type": "ui", "name": "N", "tags": ["z"]})
        self.assertEqual(R.urin_read(self.t)["tags"], ["z"])
        open(os.path.join(self.t, R.URIN), "w").write("x" * 70000); self.assertIsNone(R.urin_read(self.t))

class CliTests(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.mkdtemp(); self.p = os.path.join(self.t, "proj"); os.makedirs(self.p)
        with open(self.p + "/main.rin", "w") as f: f.write("print(1)\n")
        with open(self.p + "/icon.png", "wb") as f: f.write(PNG)
        R.urin_write(self.p, {"type": "ui", "name": "Shop", "id": "shop", "version": "3.1.0", "author": "ana", "tags": ["demo"],
                              "permissions": ["network"], "icon": "icon.png", "extra": {}})
        self.out = os.path.join(self.t, "o.rinpak")
    def tearDown(self): shutil.rmtree(self.t)
    def run_(self, *a):
        import contextlib
        with contextlib.redirect_stdout(io.StringIO()) as o, contextlib.redirect_stderr(io.StringIO()) as e: rc = R.main(list(a))
        return rc, o.getvalue(), e.getvalue()

    def test_pack_reads_urin_and_icon(self):
        self.assertEqual(self.run_("pack", self.p, "-o", self.out)[0], 0)
        m = R.read(self.out)
        self.assertEqual((m["name"], m["type"], m["id"], m["version"], m["author"], m["tags"]), ("Shop", "ui", "shop", "3.1.0", "ana", ["demo"]))
        self.assertEqual(m["_icon"], PNG)

    def test_cli_flags_override_urin(self):
        self.run_("pack", self.p, "-o", self.out, "--version", "9.9.9", "--type", "free"); m = R.read(self.out)
        self.assertEqual((m["version"], m["type"]), ("9.9.9", "free"))

    def test_verify_policies(self):
        key = os.path.join(self.t, "k.pem"); kid = R.keygen(key)
        self.run_("pack", self.p, "-o", self.out)
        self.assertEqual(self.run_("verify", self.out)[0], 0)
        self.assertEqual(self.run_("verify", self.out, "--require-signature")[0], 2)
        self.run_("pack", self.p, "-o", self.out, "--sign", key)
        self.assertEqual(self.run_("verify", self.out, "--require-signature", "--trust", kid)[0], 0)
        self.assertEqual(self.run_("verify", self.out, "--trust", "0" * 16)[0], 2)

    def test_verify_risk_and_version_gates(self):
        open(self.p + "/n.so", "wb").write(b"\x7fELF")
        self.run_("pack", self.p, "-o", self.out)
        self.assertEqual(self.run_("verify", self.out, "--max-risk", "medium")[0], 2)
        self.assertEqual(self.run_("verify", self.out)[0], 0)
        os.remove(self.p + "/n.so"); self.run_("pack", self.p, "-o", self.out, "--minRinVersion", "5.0.0")
        self.assertEqual(self.run_("verify", self.out, "--app-version", "1.0.0")[0], 2)
        self.assertEqual(self.run_("verify", self.out, "--app-version", "5.0.0")[0], 0)

    def test_meta_command(self):
        rc, out, _ = self.run_("meta", self.p, "--set", "version=4.0.0", "--set", "tags=a,b,c")
        self.assertEqual(rc, 0); m = R.urin_read(self.p)
        self.assertEqual((m["version"], m["tags"]), ("4.0.0", ["a", "b", "c"])); self.assertTrue(m["modified"])
        self.assertEqual(self.run_("meta", self.p, "--set", "bogus=1")[0], 1)
        self.assertEqual(self.run_("meta", self.p, "--set", "id=Bad Id")[0], 1)

    def test_info_output(self):
        self.run_("pack", self.p, "-o", self.out); rc, out, _ = self.run_("info", self.out)
        j = json.loads(out); self.assertTrue(j["hasIcon"]); self.assertIn("analysis", j); self.assertNotIn("_icon", j)

if __name__ == "__main__": unittest.main(verbosity=2)

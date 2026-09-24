#!/usr/bin/env python3
"""اختبار scripts/build_share_pages.py بلا شبكة.   python3 tests/web/make_share_fixture.py && python3 tests/web/test_share_pages.py"""
import importlib.util, json, re, sys, tempfile, unittest
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("bsp", ROOT / "scripts" / "build_share_pages.py")
bsp = importlib.util.module_from_spec(spec); spec.loader.exec_module(bsp)
FX = Path(__file__).with_name("share_fixture.json")


class Share(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(); cls.out = Path(cls.tmp.name)
        cls.counts = bsp.generate(json.loads(FX.read_text(encoding="utf-8")), cls.out)

    def page(self, rel): return (self.out / rel / "index.html").read_text(encoding="utf-8")

    def test_pure_helpers(self):
        self.assertEqual(bsp.fmt_count(1520), "1.5K"); self.assertEqual(bsp.fmt_count(3_400_000), "3.4M"); self.assertEqual(bsp.fmt_size(18200), "17.8 KB")
        self.assertEqual(bsp.lib_key("Http Client.og.rin"), "http-client"); self.assertEqual(bsp.enc("سارة"), "%D8%B3%D8%A7%D8%B1%D8%A9")
        self.assertEqual(bsp.lang_of("مكتبة", "x"), "ar"); self.assertEqual(bsp.lang_of("A tiny http client"), "en")
        self.assertFalse(bsp.safe_segment("../x")); self.assertFalse(bsp.safe_segment("a/b")); self.assertTrue(bsp.safe_segment("Http-Client.og.rin"))

    def test_counts_and_unsafe_skipped(self):
        items, profiles = self.counts
        self.assertEqual(items, 6)  # math, csv, Http Client (أحدث نسخة فقط), مكتبة-الرسم, long, Dark Theme
        self.assertFalse((self.out / "@mallory").exists())

    def test_library_page_has_full_preview_meta(self):
        h = self.page("@rin/math.og.rin")
        for needle in ('<link rel="canonical" href="https://dlof-lib.github.io/rinlang/@rin/math.og.rin/"/>', 'property="og:title"', 'property="og:image:width" content="1200"',
                       'name="twitter:card" content="summary_large_image"', 'rel="icon"', 'application/ld+json', 'Math helpers for Rin', "v2.1.0", 'name="keywords" content="math, @rin, Official'):
            self.assertIn(needle, h)
        self.assertIn("location.replace(", h); self.assertIn("?@rin/math.og.rin", h)

    def test_image_is_1200x630_and_exists(self):
        h = self.page("@rin/math.og.rin"); m = re.search(r'property="og:image" content="https://dlof-lib.github.io/rinlang/(og/[0-9a-f]+\.png)"', h)
        self.assertTrue(m); im = Image.open(self.out / m.group(1)); self.assertEqual(im.size, (1200, 630))
        self.assertEqual(Image.open(self.out / "og-store.png").size, (1200, 630))

    def test_html_and_jsonld_escaping(self):
        h = self.page("@dlof/Http-Client.og.rin")
        self.assertNotIn("<script>alert(1)</script>", h); self.assertIn("&lt;script&gt;", h)
        ld = re.search(r'<script type="application/ld\+json">(.*?)</script>', h, re.S).group(1); json.loads(ld)
        self.assertEqual(h.count("<script"), 2)  # JSON-LD + المحوّل فقط

    def test_arabic_page_is_rtl_and_percent_encoded(self):
        h = self.page("@سارة/مكتبة-الرسم.og.rin")
        self.assertIn('lang="ar" dir="rtl"', h); self.assertIn("%D8%B3%D8%A7%D8%B1%D8%A9/%D9%85%D9%83%D8%AA%D8%A8%D8%A9-%D8%A7%D9%84%D8%B1%D8%B3%D9%85.og.rin", h)

    def test_profile_and_extension(self):
        p = self.page("@dlof"); self.assertIn('property="og:type" content="profile"', p); self.assertIn("1 library", p); self.assertIn("1 extension", p)
        self.assertIn('"@type": "SoftwareApplication"', self.page("@dlof/Dark-Theme.rinex"))

    def test_case_alias_and_sitemap(self):
        self.assertTrue((self.out / "@dlof/http-client.og.rin/index.html").exists())
        sm = (self.out / "sitemap.xml").read_text(encoding="utf-8"); self.assertIn("/@rin/math.og.rin/", sm); self.assertIn("<loc>https://dlof-lib.github.io/rinlang/</loc>", sm)
        self.assertIn("Sitemap: https://dlof-lib.github.io/rinlang/sitemap.xml", (self.out / "robots.txt").read_text())


if __name__ == "__main__":
    unittest.main(verbosity=2)

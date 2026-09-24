#!/usr/bin/env python3
"""يبني tests/web/share_fixture.json بنفس شكل Firebase (بلا شبكة) لاختبار scripts/build_share_pages.py."""
import base64, io, json, sys
from pathlib import Path
from PIL import Image, ImageDraw

def icon_b64():
    im = Image.new("RGB", (128, 128), (30, 64, 175)); d = ImageDraw.Draw(im)
    d.ellipse((24, 24, 104, 104), fill=(250, 204, 21)); b = io.BytesIO(); im.save(b, "PNG")
    return base64.b64encode(b.getvalue()).decode()

data = {
  "libraries": {
    "math_og_rin": {"name": "math", "fileName": "math.og.rin", "version": "2.1.0", "sizeBytes": 18200, "updatedAt": 1790000000000,
                    "downloadCount": 1520, "likeCount": 87, "description": "Math helpers for Rin: sqrt, pow, trig, random ranges and statistics."},
    "csv_og_rin": {"name": "csv", "fileName": "csv.og.rin", "version": "1.0.0", "sizeBytes": 6100, "updatedAt": 1789000000000,
                   "downloadCount": 210, "likeCount": 9, "description": "قراءة وكتابة ملفات CSV بسهولة داخل لغة Rin مع دعم الفواصل والاقتباس."},
  },
  "packages": {
    "-Npk1": {"name": "Http Client", "version": "0.9.3", "publisherUsername": "dlof", "publisherName": "Dlof Dev", "sizeBytes": 42000,
              "createdAt": 1788000000000, "downloadCount": 3400000, "likeCount": 1200, "ratingSum": 46, "ratingCount": 10,
              "description": "A tiny HTTP client with retries, JSON helpers and timeouts. <script>alert(1)</script> \"quoted\" & more.",
              "iconBase64": icon_b64(), "category": "Network"},
    "-Npk2": {"name": "مكتبة-الرسم", "version": "1.2.0", "publisherUsername": "سارة", "publisherName": "سارة", "sizeBytes": 900,
              "createdAt": 1787000000000, "downloadCount": 12, "likeCount": 3,
              "description": "مكتبة عربية للرسم على اللوحة: خطوط ودوائر ومستطيلات وألوان جاهزة مع أمثلة كاملة وشرح مفصل لكل دالة موجودة في المكتبة.", "category": "Graphics"},
    "-Npk3": {"name": "A very long library name that keeps going and going forever", "version": "3.0.0-beta", "publisherUsername": "alex",
              "createdAt": 1786000000000, "downloadCount": 0, "likeCount": 0, "description": ""},
    "-Npk4": {"name": "evil/../name", "version": "1.0.0", "publisherUsername": "mallory", "createdAt": 1},
    "-Npk5": {"name": "Http Client", "version": "0.8.0", "publisherUsername": "dlof", "createdAt": 1700000000000},
  },
  "extensions": {
    "-Nex1": {"name": "Dark Theme", "version": "1.4.0", "developerUsername": "dlof", "developer": "Dlof Dev", "type": "theme",
              "releaseDate": 1790500000000, "downloadCount": 950, "likeCount": 61, "ratingSum": 19, "ratingCount": 4,
              "description": "A dark editor theme tuned for Rin syntax."},
  },
}
out = Path(__file__).with_name("share_fixture.json")
out.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8"); print("wrote", out)

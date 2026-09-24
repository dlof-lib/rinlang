#!/usr/bin/env python3
"""يولّد /tmp/pkg_*.b64 و /tmp/pkg_src.txt لاختبار tests/web/libraries.test.js (بنية أرشيف RinPM: lib/<name>.og.rin + README.md + LICENSE + assets/)."""
import base64, io, zipfile
src = '// مكتبة اختبار — <script>alert(1)</script>\nfun add(a, b) { return a + b; }\nlet s = "x\\"y"; @import "lib/z.og.rin";\n'
buf = io.BytesIO()
with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('lib/mylib.og.rin', src * 40); z.writestr('README.md', '# readme\n'); z.writestr('LICENSE', 'MIT'); z.writestr('assets/icon.png', b'\x89PNG' + b'0' * 50)
open('/tmp/pkg_deflate.b64', 'w').write(base64.b64encode(buf.getvalue()).decode())
buf = io.BytesIO()
with zipfile.ZipFile(buf, 'w', zipfile.ZIP_STORED) as z:
    z.writestr('lib/stored.og.rin', src)
open('/tmp/pkg_stored.b64', 'w').write(base64.b64encode(buf.getvalue()).decode())
open('/tmp/pkg_src.txt', 'w').write(src * 40)

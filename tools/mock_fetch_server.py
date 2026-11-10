#!/usr/bin/env python3
"""خادم اختبار محلي لـ fetch() (docs/fetch.md): يعكس الطلب، يحوّل، يفشل ثم ينجح، كوكيز، مصادقة، gzip، تنزيل، multipart.
usage: mock_fetch_server.py PORT"""
import gzip, json, sys, time, re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

STATE = {"flaky": 0, "r429": 0, "hits": 0}

class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def log_message(self, *a): pass
    def _send(self, code, body=b"", ctype="application/json", extra=None):
        if isinstance(body, (dict, list)): body = json.dumps(body).encode()
        if isinstance(body, str): body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or []): self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD": self.wfile.write(body)
    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""
    def handle_any(self):
        u = urlparse(self.path); p = u.path; q = parse_qs(u.query); body = self._body()
        STATE["hits"] += 1
        if p == "/echo":
            h = {k.lower(): v for k, v in self.headers.items()}
            return self._send(200, {"method": self.command, "query": q, "headers": h, "body": body.decode("utf-8", "replace")}, extra=[("X-Echo", "1")])
        m = re.match(r"^/status/(\d+)$", p)
        if m: return self._send(int(m.group(1)), {"status": int(m.group(1)), "message": "status " + m.group(1)})
        m = re.match(r"^/redirect/(\d+)$", p)
        if m:
            n = int(m.group(1)); return self._send(302, b"", extra=[("Location", "/redirect/%d" % (n - 1) if n > 1 else "/echo?via=redirect")])
        if p == "/flaky":
            STATE["flaky"] += 1
            if STATE["flaky"] % 3 != 0: return self._send(503, {"message": "try again"})
            return self._send(200, {"ok": True, "n": STATE["flaky"]})
        if p == "/rate":
            STATE["r429"] += 1
            if STATE["r429"] % 2 == 1: return self._send(429, {"message": "slow down"}, extra=[("Retry-After", "1")])
            return self._send(200, {"ok": True})
        if p == "/slow": time.sleep(float(q.get("s", ["2"])[0])); return self._send(200, {"slow": True})
        if p == "/cookie/set": return self._send(200, {"set": True}, extra=[("Set-Cookie", "sid=abc123; Path=/"), ("Set-Cookie", "theme=dark; Path=/")])
        if p == "/cookie/get": return self._send(200, {"cookie": self.headers.get("Cookie", "")})
        if p == "/basic":
            import base64
            ok = self.headers.get("Authorization") == "Basic " + base64.b64encode(b"sara:pw").decode()
            return self._send(200 if ok else 401, {"auth": ok}, extra=[] if ok else [("WWW-Authenticate", 'Basic realm="t"')])
        if p == "/gzip":
            data = gzip.compress(json.dumps({"gz": True, "text": "x" * 200}).encode())
            return self._send(200, data, extra=[("Content-Encoding", "gzip")])
        if p == "/big": return self._send(200, b"A" * 200000, ctype="application/octet-stream")
        if p == "/pic": return self._send(200, bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d49444154789c6360000002000001e221bc330000000049454e44ae426082"), ctype="image/png")
        if p == "/upload":
            ct = self.headers.get("Content-Type", ""); names = re.findall(rb'name="([^"]+)"', body)
            fns = re.findall(rb'filename="([^"]+)"', body)
            return self._send(200, {"ctype": ct.split(";")[0], "names": [n.decode() for n in names], "filenames": [f.decode() for f in fns], "size": len(body), "hasFileBytes": b"FILE-BYTES" in body})
        if p == "/text": return self._send(200, "hello text", ctype="text/plain; charset=utf-8")
        if p == "/hits": return self._send(200, {"hits": STATE["hits"]})
        return self._send(404, {"message": "not found: " + p})
    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_OPTIONS = do_HEAD = handle_any

if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()

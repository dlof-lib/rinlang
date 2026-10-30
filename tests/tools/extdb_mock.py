import json, sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs, unquote
FB = {}          # rtdb tree
SB = {"users": [{"id": "u1", "name": "Sara", "age": 30}], "orders": []}
SEQ = [100]
FLAKY = {"rt": 0, "pg": 0}
HITS = {"n": 0}
LAST = {"pg": "", "rt": ""}
USERS = {"sara@example.com": "pw123"}
TOK = {"n": 0}
def jget(tree, segs):
    for s in segs:
        if not isinstance(tree, dict) or s not in tree: return None
        tree = tree[s]
    return tree
def jset(tree, segs, val):
    for s in segs[:-1]: tree = tree.setdefault(s, {})
    if val is None: tree.pop(segs[-1], None)
    else: tree[segs[-1]] = val
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type","application/json"); self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n)) if n else None
    def do_any(self):
        u = urlparse(self.path); q = parse_qs(u.query, keep_blank_values=True); m = self.command
        if u.path.startswith("/idt/"): return self.idt(u, q, m)
        if u.path.startswith("/auth/v1/"): return self.gotrue(u, q, m)
        if u.path.startswith("/rt/"): return self.fb(u, q, m)
        if u.path.startswith("/rest/v1"): return self.sb(u, q, m)
        self.send(404, {"error": "nope"})
    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_any
    def idt(self, u, q, m):
        if q.get("key", [""])[0] != "APIKEY": return self.send(400, {"error": {"message": "API key not valid"}})
        route = u.path[len("/idt/"):]
        if route == "token":
            n = int(self.headers.get("Content-Length") or 0); form = parse_qs(self.rfile.read(n).decode())
            TOK["n"] += 1
            return self.send(200, {"id_token": "IDT_refreshed%d" % TOK["n"], "refresh_token": form["refresh_token"][0], "expires_in": "3600", "user_id": "uid1"})
        b = self.body()
        if route == "accounts:signInWithPassword":
            if USERS.get(b["email"]) != b["password"]: return self.send(400, {"error": {"message": "INVALID_PASSWORD"}, "message": "INVALID_PASSWORD"})
            return self.send(200, {"idToken": "IDT_" + b["email"], "refreshToken": "RT_1", "expiresIn": "3600", "localId": "uid1", "email": b["email"]})
        if route == "accounts:signUp":
            USERS[b["email"]] = b["password"]
            return self.send(200, {"idToken": "IDT_" + b["email"], "refreshToken": "RT_1", "expiresIn": "3600", "localId": "uid2", "email": b["email"]})
        self.send(404, {"error": "nope"})
    def gotrue(self, u, q, m):
        route = u.path[len("/auth/v1/"):]
        if self.headers.get("apikey") != "ANON": return self.send(401, {"message": "Invalid API key"})
        b = self.body() if m == "POST" else None
        if route == "token" and q.get("grant_type", [""])[0] == "password":
            if USERS.get(b["email"]) != b["password"]: return self.send(400, {"message": "Invalid login credentials"})
            return self.send(200, {"access_token": "JWT_" + b["email"], "refresh_token": "RT_1", "expires_in": 3600, "user": {"id": "uid1", "email": b["email"]}})
        if route == "token" and q.get("grant_type", [""])[0] == "refresh_token":
            TOK["n"] += 1
            return self.send(200, {"access_token": "JWT_refreshed%d" % TOK["n"], "refresh_token": b["refresh_token"], "expires_in": 3600})
        if route == "signup":
            USERS[b["email"]] = b["password"]
            return self.send(200, {"access_token": "JWT_" + b["email"], "refresh_token": "RT_1", "expires_in": 3600, "user": {"id": "uid2", "email": b["email"]}})
        if route == "logout": return self.send(204, None)
        self.send(404, {"message": "nope"})
    def fb(self, u, q, m):
        a = q.get("auth", [""])[0]; LAST["rt"] = a
        if u.path.endswith("/_whoami.json"): return self.send(200, a)
        if not (a == "SECRET" or a.startswith("IDT_")): return self.send(401, {"error": "Permission denied"})
        path = unquote(u.path[len("/rt/"):])
        HITS["n"] += 1
        if path == "flaky.json" and FLAKY["rt"] < 2:
            FLAKY["rt"] += 1; return self.send(503, {"error": "busy"})
        assert path.endswith(".json"); path = path[:-5]
        segs = [s for s in path.split("/") if s]
        cur = jget(FB, segs) if segs else (FB or None)
        if m == "GET":
            if "shallow" in q and isinstance(cur, dict): cur = {k: True for k in cur}
            return self.send(200, cur)
        b = self.body()
        if m == "PUT": jset(FB, segs, b); return self.send(200, b)
        if m == "PATCH":
            if not isinstance(cur, dict): cur = {}
            for k, v in b.items():
                if v is None: cur.pop(k, None)
                else: cur[k] = v
            jset(FB, segs, cur); return self.send(200, b)
        if m == "POST":
            SEQ[0] += 1; k = "-N%d" % SEQ[0]; jset(FB, segs + [k], b); return self.send(200, {"name": k})
        if m == "DELETE": jset(FB, segs, None); return self.send(200, None)
    def sb(self, u, q, m):
        t = u.path[len("/rest/v1"):].strip("/")
        az = self.headers.get("Authorization") or ""; LAST["pg"] = az
        if t == "_whoami": return self.send(200, {"auth": az})
        if self.headers.get("apikey") != "ANON" or not (az == "Bearer ANON" or az.startswith("Bearer JWT_")):
            return self.send(401, {"message": "Invalid API key"})
        if t == "": return self.send(200, {"swagger": "2.0"})
        prefer = self.headers.get("Prefer", "")
        HITS["n"] += 1
        if t == "flaky" and FLAKY["pg"] < 2:
            FLAKY["pg"] += 1; return self.send(429, {"message": "slow down"})
        if t == "_hits": return self.send(200, {"n": HITS["n"]})
        if t.startswith("rpc/"):
            return self.send(200, {"fn": t[4:], "args": self.body()})
        if t == "flaky": return self.send(200, [{"id": "f1", "ok": True}])
        if t != "ghost" and t != "ghost_table": SB.setdefault(t, [])
        if t not in SB: return self.send(404, {"message": "relation does not exist", "code": "42P01"})
        rows = SB[t]
        def match(r):
            for k, v in [(k, x) for k, vs in q.items() for x in vs]:
                if k in ("select","limit","offset","order","on_conflict"): continue
                op, val = v.partition(".")[0], v.partition(".")[2]
                neg = False
                if op == "not": neg = True; op, _, val = val.partition(".")
                def num(x):
                    try: return float(x)
                    except: return x
                have = r.get(k); ok = True
                if op == "eq": ok = str(have) == val
                elif op == "is": ok = (val == "null" and have is None) or (val == "true" and have is True)
                elif op == "in": ok = str(have) in [x for x in val.strip("()").split(",")]
                elif op in ("gt","gte","lt","lte"):
                    if have is None: ok = False
                    else:
                        a, b2 = num(have) if not isinstance(have, str) else have, num(val)
                        try: ok = {"gt": a > b2, "gte": a >= b2, "lt": a < b2, "lte": a <= b2}[op]
                        except TypeError: ok = False
                elif op in ("like","ilike"):
                    pat = val.lower().replace("*", "%"); h = str(have).lower()
                    core = pat.strip("%"); ok = (core in h) if pat.startswith("%") and pat.endswith("%") else h.startswith(core) if pat.endswith("%") else h.endswith(core) if pat.startswith("%") else h == core
                if ok == neg: return False
            return True
        if m == "GET":
            out = [r for r in rows if match(r)]
            if "order" in q:
                f, _, d = q["order"][0].partition(".")
                out.sort(key=lambda r: str(r.get(f)) if f == "id" else r.get(f), reverse=(d == "desc"))
            off = int(q.get("offset", ["0"])[0]); lim = int(q.get("limit", ["1000"])[0])
            out = out[off:off+lim]
            if "select" in q and q["select"][0] != "*":
                cols = q["select"][0].split(","); out = [{c: r.get(c) for c in cols} for r in out]
            return self.send(200, out)
        b = self.body()
        if m == "POST" and isinstance(b, list):
            for item in b:
                ex = next((r for r in rows if r.get("id") == item.get("id")), None)
                if ex: ex.update(item)
                else: rows.append(item)
            return self.send(201, None)
        if m == "POST":
            b = b if isinstance(b, dict) else {}
            ex = next((r for r in rows if "id" in b and r.get("id") == b["id"]), None)
            if ex and "merge-duplicates" in prefer: ex.update(b); res = [ex]
            elif ex: return self.send(409, {"message": "duplicate key value violates unique constraint", "code": "23505"})
            else:
                if "id" not in b: SEQ[0] += 1; b["id"] = SEQ[0]
                rows.append(b); res = [b]
            return self.send(201, res if "representation" in prefer else None) if "representation" not in prefer else self.send(201, res)
        if m == "PATCH":
            hit = [r for r in rows if match(r)]
            for r in hit: r.update(b)
            return self.send(200, hit)
        if m == "DELETE":
            hit = [r for r in rows if match(r)]
            for r in hit: rows.remove(r)
            return self.send(200, hit)
HTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()

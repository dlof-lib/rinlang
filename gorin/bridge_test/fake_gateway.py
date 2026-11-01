# بوابة وهمية بلغة Python تحاكي عقد gorin تماماً، لاختبار gorinkit مع محرك C++ الحقيقي
import json, hashlib, base64, uuid, re
from http.server import BaseHTTPRequestHandler, HTTPServer
TOKEN="SECRET"
class H(BaseHTTPRequestHandler):
    def log_message(self,*a): pass
    def send(self,code,obj):
        b=json.dumps(obj).encode(); self.send_response(code); self.send_header("Content-Type","application/json"); self.send_header("Content-Length",str(len(b))); self.end_headers(); self.wfile.write(b)
    def auth(self):
        if self.headers.get("Authorization")!="Bearer "+TOKEN:
            self.send(401,{"error":"unauthorized"}); return False
        return True
    def do_GET(self):
        if self.path=="/health": return self.send(200,{"status":"ok"})
        if not self.auth(): return
        self.send(404,{"ok":False,"error":"nope"})
    def do_POST(self):
        if not self.auth(): return
        n=int(self.headers.get("Content-Length","0")); body=json.loads(self.rfile.read(n) or b"{}")
        p=self.path
        if p=="/run": return self.send(200,{"ok":True,"engine":"cpp","stdout":"3\n","stderr":"","exit_code":0})
        name=p[len("/svc/"):]
        try:
            if name=="hash": r=getattr(hashlib,body["algo"])(body["data"].encode()).hexdigest()
            elif name=="base64": r=base64.b64encode(body["data"].encode()).decode() if body["op"]=="encode" else base64.b64decode(body["data"]).decode()
            elif name=="uuid": r=str(uuid.uuid4())
            elif name=="regex" and body["op"]=="findall": r=re.findall(body["pattern"],body["text"])
            elif name=="regex" and body["op"]=="match": r=bool(re.search(body["pattern"],body["text"]))
            else: return self.send(404,{"ok":False,"error":"unknown service: "+name})
            self.send(200,{"ok":True,"result":r})
        except Exception as e:
            self.send(400,{"ok":False,"error":str(e)})
HTTPServer(("127.0.0.1",8799),H).serve_forever()

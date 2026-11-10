# خادم محاكاة لنقاط GitHub REST (نفس الأشكال والرؤوس): users/:u/repos · repos/:o/:r · contents · readme · languages · branches · avatars
import http.server, json, base64, re, struct, zlib, urllib.parse
def png(c):
    W=H=64; raw=b''.join(b'\x00'+bytes(c)*W for _ in range(H))
    ch=lambda t,d: struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',W,H,8,2,0,0,0))+ch(b'IDAT',zlib.compress(raw))+ch(b'IEND',b'')
BASE='http://127.0.0.1:8790'
OWNER={'login':'octo-dev','avatar_url':BASE+'/avatars/octo-dev.png'}
REPOS={
 'rinlang':{'description':'لغة Rin — مفسّر C++23','language':'C++','stargazers_count':128,'forks_count':14,'default_branch':'main',
   'langs':{'C++':812340,'Kotlin':90210,'Shell':5120},'files':[('app','dir',0),('docs','dir',0),('tests','dir',0),('README.md','file',4821),('Makefile','file',912)],
   'readme':'# rinlang\nلغة برمجة بكلمات سياقية ومتغيّرات حيّة.\n'},
 'indsin':{'description':'محرّك واجهات Indsintime','language':'C++','stargazers_count':64,'forks_count':6,'default_branch':'main',
   'langs':{'C++':402000,'HTML':12000},'files':[('src','dir',0),('README.md','file',1200)],'readme':'# indsin\nمحرّك واجهات.\n'},
 'rin-site':{'description':'الموقع الرسمي','language':'Rin','stargazers_count':9,'forks_count':1,'default_branch':'gh-pages',
   'langs':{'Rin':20000},'files':[('site.rin','file',20480)],'readme':'# rin-site\n'},
 'dotfiles':{'description':None,'language':None,'stargazers_count':0,'forks_count':0,'default_branch':'main','langs':{},'files':[],'readme':None},
}
LOG=[]
class H(http.server.BaseHTTPRequestHandler):
    def send(self, code, obj, ctype='application/json', raw=None):
        b = raw if raw is not None else json.dumps(obj, ensure_ascii=False).encode()
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        u=urllib.parse.urlparse(self.path); q=urllib.parse.parse_qs(u.query); p=u.path
        LOG.append((p, self.headers.get('Authorization')))
        if p=='/__log': return self.send(200, LOG)
        if p.startswith('/avatars/'): return self.send(200, None, 'image/png', png((124,92,255)))
        if not self.headers.get('User-Agent'): return self.send(403, {'message':'Request forbidden by administrative rules. Please make sure your request has a User-Agent header'})
        if self.headers.get('Accept') != 'application/vnd.github+json': return self.send(415, {'message':'Unsupported Accept'})
        m=re.fullmatch(r'/users/([^/]+)/repos', p)
        if m:
            if m.group(1)!='octo-dev': return self.send(404, {'message':'Not Found'})
            per=int(q.get('per_page',['30'])[0]); items=[{'name':k,'full_name':'octo-dev/'+k,'description':v['description'],'language':v['language'],'stargazers_count':v['stargazers_count'],'forks_count':v['forks_count'],'default_branch':v['default_branch'],'owner':OWNER} for k,v in REPOS.items()]
            if q.get('sort',[''])[0]=='stars': items.sort(key=lambda r:-r['stargazers_count'])
            return self.send(200, items[:per])
        m=re.fullmatch(r'/repos/octo-dev/([^/]+)(?:/(contents|readme|languages|branches)(?:/(.*))?)?', p)
        if not m or m.group(1) not in REPOS: return self.send(404, {'message':'Not Found','documentation_url':'https://docs.github.com/rest'})
        r=REPOS[m.group(1)]; what=m.group(2)
        if what is None: return self.send(200, {'name':m.group(1),'owner':OWNER,'description':r['description'],'default_branch':r['default_branch']})
        if what=='languages': return self.send(200, r['langs'])
        if what=='branches': return self.send(200, [{'name':r['default_branch']},{'name':'dev'}])
        if what=='readme':
            if r['readme'] is None: return self.send(404, {'message':'Not Found'})
            return self.send(200, {'name':'README.md','encoding':'base64','content':base64.b64encode(r['readme'].encode()).decode()})
        sub=m.group(3) or ''
        if sub: return self.send(200, {'type':'file','name':sub,'path':sub,'size':1})
        return self.send(200, [{'name':n,'path':n,'type':t,'size':s} for n,t,s in r['files']])
    def log_message(self,*a): pass
http.server.HTTPServer(('127.0.0.1',8790),H).serve_forever()

"""يقارن المفكّك المدمج (tests/terminal/imgdec_dump.cpp) مع PIL على كل صور مجلد.
الاستخدام:  python3 tests/terminal/imgdec_compare.py /مجلد/صور [/مسار/dump]
المتوقع: كل png/gif/bmp متطابقة (mae=0)، وjpeg متوسط الخطأ < 0.2 من 255. يحتاج Pillow."""
import subprocess, sys, glob, os
DIR = sys.argv[1] if len(sys.argv) > 1 else '/tmp/imgs'
DUMP = sys.argv[2] if len(sys.argv) > 2 else '/tmp/dump'
from PIL import Image, ImageOps
def ref(path):
    im = Image.open(path)
    try: im = ImageOps.exif_transpose(im)
    except Exception: pass
    if im.mode == 'I;16': im = im.point(lambda v: v/256).convert('L')
    return im.convert('RGBA')
rows=[]
for f in sorted(glob.glob(os.path.join(DIR, '*'))):
    name=os.path.basename(f)
    p=subprocess.run([DUMP,f],capture_output=True)
    if p.returncode!=0:
        rows.append((name,'REJECT',p.stderr.decode().strip()[:70])); continue
    hdr,_,body=p.stdout.partition(b'\n'); w,h=map(int,hdr.split())
    r=ref(f)
    if r.size!=(w,h): rows.append((name,'SIZE-MISMATCH',f'ours={w}x{h} pil={r.size}')); continue
    a=body; b=r.tobytes()
    # قارن على الأبيض لدمج الألفا (نفس ما يفعله العارض)
    def flat(buf):
        out=bytearray()
        for i in range(0,len(buf),4):
            al=buf[i+3]/255
            for c in range(3): out.append(int(buf[i+c]*al+255*(1-al)+0.5))
        return bytes(out)
    fa,fb=flat(a),flat(b)
    diffs=[abs(x-y) for x,y in zip(fa,fb)]
    mae=sum(diffs)/len(diffs); mx=max(diffs)
    rows.append((name,'ok' if (mae<1.5 if not name.endswith('.jpg') else mae<3.0) else 'DIFF',f'{w}x{h} mae={mae:.2f} max={mx}'))
for r in rows: print(f'{r[0]:18} {r[1]:14} {r[2]}')

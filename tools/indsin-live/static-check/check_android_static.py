#!/usr/bin/env python3
"""فحوص ثابتة لجانب أندرويد بلا مترجم Kotlin ولا SDK (تكمّل android-server-test الذي يحتاج kotlinc).
 1) JNI: كل `external fun` في Kotlin له تصدير `Java_com_dlof_rinlang_*` في C++ بنفس عدد الوسائط وأنواعها وقيمة الإرجاع.
 2) الموارد: كل R.string.* / @string/* موجود، وتطابق مفاتيح strings_live_pro.xml بين values / ar / en / es.
 3) اكتمال المصيِّر: كل نوع Strand يُصدّره المحرّك (strandKindName) له فرع في IndsinFabricView.kt (عدا الحاويات العادية).
الاستعمال من جذر المستودع:  python3 tools/indsin-live/static-check/check_android_static.py   (يعيد 1 عند أي مشكلة)"""
import glob, os, re, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
MAIN = os.path.join(ROOT, 'app', 'src', 'main')
rd = lambda p: open(p, encoding='utf8').read()
problems = []

# ---- 1) JNI -------------------------------------------------------------------------------------------------------
JT = {'String': 'jstring', 'Int': 'jint', 'Long': 'jlong', 'Double': 'jdouble', 'Boolean': 'jboolean', 'Float': 'jfloat',
      'ByteArray': 'jbyteArray', 'IntArray': 'jintArray', 'LongArray': 'jlongArray', 'DoubleArray': 'jdoubleArray',
      'FloatArray': 'jfloatArray', 'BooleanArray': 'jbooleanArray', 'Unit': 'void'}
decl = {}
for f in glob.glob(MAIN + '/java/**/*.kt', recursive=True):
    for m in re.finditer(r'external fun (\w+)\(([^)]*)\)\s*(?::\s*([\w<>?]+))?', rd(f)):
        decl[(os.path.basename(f)[:-3], m.group(1))] = (m.group(2), m.group(3))
cpp = ''.join(rd(f) for f in glob.glob(MAIN + '/cpp/**/*.cpp', recursive=True))
impl = {}
for m in re.finditer(r'JNIEXPORT\s+(\w+)\s+JNICALL\s+Java_com_dlof_rinlang_(\w+?)_(\w+)\s*\(([^)]*)\)', cpp):
    impl[(m.group(2), m.group(3))] = (m.group(1), m.group(4))
for (cls, name), (args, ret) in sorted(decl.items()):
    k = (cls, name.replace('_', '_1'))
    if k not in impl: problems.append(f'JNI: لا تصدير C++ لـ {cls}.{name}'); continue
    jr, jargs = impl[k]
    ka = [a.split(':')[1].strip().rstrip('?') for a in args.split(',') if a.strip()]
    ja = [a for a in jargs.split(',') if a.strip()][2:]
    if len(ka) != len(ja): problems.append(f'JNI: عدد وسائط {cls}.{name} ({len(ka)} ≠ {len(ja)})'); continue
    for a, j in zip(ka, ja):
        want = JT.get(a, 'jobject')
        if want not in j: problems.append(f'JNI: نوع وسيط {cls}.{name}: {a} ≠ {j.strip()}')
    want = JT.get((ret or 'Unit').rstrip('?'), 'jobject')
    if want != jr and not (want == 'jobject' and jr in ('jobject', 'jstring', 'jobjectArray')):
        problems.append(f'JNI: إرجاع {cls}.{name}: {ret} ≠ {jr}')
print(f'JNI: {len(decl)} دالة Kotlin مقابل {len(impl)} تصديراً')

# ---- 2) الموارد ---------------------------------------------------------------------------------------------------
have = {}
for f in glob.glob(MAIN + '/res/values*/*.xml'):
    loc = f.split('/')[-2]
    for m in re.finditer(r'<string\s+name="([^"]+)"', rd(f)): have.setdefault(m.group(1), set()).add(loc)
used = set()
for f in glob.glob(MAIN + '/java/**/*.kt', recursive=True):
    used |= {u for u in re.findall(r'(?<!android\.)R\.string\.(\w+)', rd(f))}
for f in glob.glob(MAIN + '/res/**/*.xml', recursive=True) + [MAIN + '/AndroidManifest.xml']:
    used |= set(re.findall(r'@string/(\w+)', rd(f)))
ANDROID_R = {'copy', 'cut', 'paste', 'selectAll', 'ok', 'x'}  # android.R.string.* (لا تُعرَّف في التطبيق)
for u in sorted(used - set(have) - ANDROID_R): problems.append(f'موارد: @string/{u} غير معرَّف')
sets = {}
for f in glob.glob(MAIN + '/res/values*/strings_live_pro.xml'):
    sets[f.split('/')[-2]] = set(re.findall(r'<string\s+name="([^"]+)"', rd(f)))
base = sets.get('values', set())
for loc, keys in sorted(sets.items()):
    if keys != base: problems.append(f'موارد: strings_live_pro.xml في {loc} يختلف عن values (ناقص {sorted(base-keys)} زائد {sorted(keys-base)})')
print(f'الموارد: {len(used)} مرجع نص، {len(sets)} لغات لـ strings_live_pro')

# ---- 3) اكتمال المصيِّر -------------------------------------------------------------------------------------------
src = ''.join(rd(f) for f in glob.glob(MAIN + '/cpp/indsin/*.h'))
j = src.find('{', src.find('strandKindName(StrandKind'))
seen, kinds = set(), []
for k, n in re.findall(r'case StrandKind::(\w+):\s*return\s*"(\w+)"', src[j:j + 8000]):
    if k in seen: break
    seen.add(k); kinds.append((k, n))
kt = rd(MAIN + '/java/com/dlof/rinlang/IndsinFabricView.kt')
PLAIN = {'Box', 'Grid', 'Wrap'}  # حاويات عادية: تمرّ على الفرع العام ويرسمها المحرّك كما هي
for k, n in kinds:
    if n not in PLAIN and not re.search(r'"%s"' % n, kt): problems.append(f'المصيِّر: النوع {n} ({k}) بلا فرع في IndsinFabricView.kt')
print(f'المصيِّر: {len(kinds)} نوعاً من المحرّك')

# ---- 4) تكافؤ جدول الخطوط (tokens) بين المحرّك وKotlin -----------------------------------------------------------
tok = rd(MAIN + '/cpp/indsin/rin_indsin_tokens.h')
native = {n: (float(sz), wt) for n, sz, wt in re.findall(r'name == "(\w+)"\)\s*\{ out = \{(\d+), "(\w+)"\}', tok)}
fs = re.search(r'private fun resolveFontSize.*?return when \(raw\) \{(.*?)\n        \}', kt, re.S)
kt_sizes = {n: float(v) for n, v in re.findall(r'"(\w+)" -> (\d+)f', fs.group(1))} if fs else {}
for n, (sz, wt) in native.items():
    if kt_sizes.get(n) != sz: problems.append(f'tokens: size={n} في المحرّك {sz:g} وفي Kotlin {kt_sizes.get(n)}')
bold_kt = set(re.findall(r'"(\w+)"', (re.search(r'return when \(attrs\.optString\("size"\)\.trim\(\)\.lowercase\(\)\) \{ (.*?) -> true', kt) or [None, ''])[1]))
for n, (sz, wt) in native.items():
    if (wt in ('semibold', 'bold')) != (n in bold_kt): problems.append(f'tokens: ثِقل {n} ({wt}) لا يطابق قاعدة الخط العريض في Kotlin')
print(f'tokens: {len(native)} اسماً مقارَناً')

# ---- 5) (معلومة) سمات يقرؤها رسّام المحرّك ولا يذكرها Kotlin أبداً ------------------------------------------------
kt_attrs = set(re.findall(r'"(\w+)"', kt))
paint_attrs = set(re.findall(r'attr(?:Str|Num|Bool)\(\s*"(\w+)"', rd(MAIN + '/cpp/indsin/rin_indsin_paint.h')))
INTENTIONAL = {'effect', 'effect_key', 'effect_on_change', 'stagger', 'canvas', 'vector', 'stroke', 'onHoverEnter', 'onHoverExit'}
unseen = sorted(paint_attrs - kt_attrs - INTENTIONAL)
print('INFO سمات المحرّك غير المذكورة في Kotlin:', unseen or 'لا شيء')

# ---- 6) جدول الأيقونات: IndsinIconData.kt مطابق للمحرّك + أوامر المسارات يفهمها PathParser --------------------------
import subprocess
gen = os.path.join(os.path.dirname(__file__), 'gen_icon_data.py')
r = subprocess.run([sys.executable, gen, '--check'], capture_output=True, text=True)
print(r.stdout.strip())
if r.returncode != 0: problems.append('أيقونات: IndsinIconData.kt قديم أو مفقود -> شغّل tools/indsin-live/static-check/gen_icon_data.py')
icon_kt = rd(MAIN + '/java/com/dlof/rinlang/IndsinIconData.kt') if os.path.exists(MAIN + '/java/com/dlof/rinlang/IndsinIconData.kt') else ''
for nm, d in re.findall(r'"([^"]+)" to "([MLHVCAZmlhvcaz0-9 .,\-]+)"', icon_kt):
    bad = set(re.findall(r'[A-Za-z]', d)) - set('MLHVCAZ')
    if bad: problems.append(f'أيقونات: المسار {nm} يستعمل أوامر غير مدعومة {sorted(bad)}')

for p in problems: print('FAIL', p)
print('OK: لا مشاكل' if not problems else f'{len(problems)} مشكلة')
sys.exit(1 if problems else 0)

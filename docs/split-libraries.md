# الحزم في Rin — «توكن الأداة OG» و«المكتبات» (`.og.rin` · `.zpp` · `.rinz` · `disconom`)

## التسمية

| الاسم | الشكل | المعنى |
|---|---|---|
| **توكن الأداة OG** (Tool Token OG) | **ملف واحد** `rinname.og.rin` مثل `lib/math.og.rin` | ما كان يُسمّى سابقاً «مكتبة Rin» القديمة. لا إصدار ولا تبعيات مُعلَنة. |
| **مكتبة** (Library) | **مجلد** `name.og.rin/` فيه ملفات ومجلدات | الجديد: Disconom + أجزاء `.zpp` + `.rinz` + `native/` + `data/`. |

الاستيراد واحد للنوعين: `@import "name";` — والمحرّك يميّز النوع تلقائياً (المجلد = مكتبة، الملف = توكن أداة).
إن وُجد الاثنان بالاسم نفسه فالمكتبة (المجلد) لها الأولوية. أولوية المصدر: **مجلد ← ملف افتراضي (rin_vm) ← مدمج في المفسّر ← قرص**.
للاستعلام: `rinlib.kind("math")` ⇒ `"tool-token"` أو `"library"` أو `nil`.

المكتبة هنا **مجلد** وليست ملفاً مضغوطاً: ملفات نصية بلغة Rin نفسها تُربط معاً كأنها ملف واحد.

```
math.og.rin/                 ← اسم المجلد = اسم المكتبة + .og.rin
  disconom.json | .xml | .toml ← وصف المكتبة (الاسم، الإصدار، إصدار Rin، التفاصيل، التبعيات …)
  main.zpp                   ← الجزء الأساسي (يُحمَّل أولاً)
  job.zpp  print.zpp         ← أجزاء بأي اسم
  oop/shape.zpp              ← وفي أي مجلد فرعي (التقسيم حر)
  run.rinz                   ← ملف تشغيل/تهيئة: يُنفَّذ بعد ربط كل الأجزاء
  native/helper.cpp          ← كود C++ مساعد (اختياري)
  data/*.json | *.xml        ← بيانات تقرؤها المكتبة
```

## الاستيراد
```kotlin
@import "math";                      // lib/math.og.rin/  (مجلد) أو الملف القديم lib/math.og.rin
@import "libs/shapes.og.rin";        // مسار صريح
import { circle_area } from "shapes"; // انتقائي (يتطلب export)
```
المجلد المقسَّم له أولوية على المكتبة المدمجة بنفس الاسم. التبعيات تُبحث بجوار المكتبة ثم في `lib/`.

## ترتيب التحميل
1. تبعيات `dependencies` (تُستورَد أولاً) 2. `main.zpp` (أو `main` في Disconom) 3. بقية `.zpp` مرتبة بالمسار
(أو بترتيب `parts` إن حدّدته) 4. أغلفة الدوال الأصلية 5. ملفات `.rinz` (`run.rinz` أولاً).
تُستثنى تلقائياً المجلدات: `native tests test examples docs build node_modules` والمخفية؛ وتستطيع `exclude`.
إن استعملت `export` في أي جزء صار المُصدَّر فقط هو الواجهة العامة؛ وإلا يُدمج كل شيء كالمعتاد.
أخطاء التشغيل تُنسَب إلى ملفها: `[core/area.zpp] ...`.

## Disconom (الأهم)
`disconom.json`
```json
{
  "disconom": 1,
  "name": "shapes", "version": "2.1.0",
  "description": "…", "authors": ["Jo"], "license": "MIT",
  "rin": ">=1.0.0 <2.0.0",
  "main": "main.zpp",
  "parts": ["main.zpp", "core/**", "oop"],
  "exclude": ["drafts/**"],
  "run": ["run.rinz"],
  "dependencies": { "geomcore": "^1.0.0" },
  "native": { "sources": ["native/helper.cpp"], "std": "c++17", "required": false,
              "functions": [ {"name": "fastSum", "params": ["numbers"]} ] }
}
```
أو `disconom.xml`:
```xml
<disconom format="1">
  <name>shapes</name><version>2.1.0</version><rin>&gt;=1.0.0</rin>
  <dependencies><dependency name="geomcore" version="^1.0.0"/></dependencies>
  <native std="c++17"><source>native/helper.cpp</source><function name="fastSum" params="numbers"/></native>
</disconom>
```
- `rin`: قيد إصدار اللغة (`^ ~ >= <= > < =`، `1.2.x`، `*`، `||`، مسافة = AND). عدم التوافق ⇒ خطأ استيراد واضح.
- `name` و`version` (semver) إلزاميان؛ `parts`/`run`/`exclude` تقبل مسارات ومجلدات و`*` `**` `?`.

## Disconom 2 — الصيغة الاحترافية الكاملة

`"disconom": 2` (أو `disconom = 2` في TOML). كل ملفات الصيغة 1 تبقى صالحة كما هي؛ الصيغة 2 **إضافات فقط**.
ثلاث صيغ متكافئة يقرؤها المحرّك بالأولوية: `disconom.json` ثم `disconom.xml` ثم `disconom.toml` (المخطط: `schemas/disconom.schema.json`).

| الحقل | المعنى |
|---|---|
| `dependencies` | تبعيات إلزامية: تُحمَّل أولاً، وإصدارها يُفحص (`^ ~ >= < 1.x * \|\|`) |
| `optionalDependencies` | **اختيارية**: تُحمَّل إن وُجدت، وغيابها ليس خطأً |
| `peerDependencies` | يوفّرها المستهلِك: تُفحص في `rin lib check` ولا تُحمَّل |
| `devDependencies` | للتطوير والاختبار فقط (لا تُحمَّل عند الاستيراد) |
| `features` | ميزات اختيارية: `{ "default": ["text"], "text": ["dep:extra"], "fast": ["text"] }` |
| `scripts` | أوامر مسمّاة: `{ "test": "rin tests/basic.rin" }` |
| `files` · `exclude` · `workspaces` | ما يُحزَم، ما يُستثنى، مكتبات فرعية |
| `private` | `true` = لا تُنشر إلى سجلّ |
| `authors` `contributors` `categories` `bugs` `funding` `readme` `platforms` | بيانات النشر |
| `x-…` (أي مفتاح مخصّص) | يُحفظ كما هو عند الترجمة بين الصيغ |

```toml
# disconom.toml
disconom = 2
name = "pro"
version = "2.3.1"
rin = ">=1.0.0 <2.0.0"
license = "MIT"

[dependencies]
base = "^1.0.0"

[optionalDependencies]
extra = "^1.0.0"

[features]
default = ["text"]          # مفعّلة دائماً
text = ["dep:extra"]        # تُفعِّل التبعية الاختيارية extra
fast = ["text"]             # ميزة تحوي ميزة

[scripts]
test = "rin tests/basic.rin"
```

### الميزات (features)
- العنصر في الميزة: اسم تبعية اختيارية، أو `dep:اسم`، أو ميزة أخرى. الدورات والمراجع المجهولة **أخطاء** في `rin lib check`.
- تبعية اختيارية **لا تذكرها أي ميزة** ⇒ تُحمَّل متى وُجدت. وإن ذكرتها ميزة ⇒ تُحمَّل فقط إذا فُعِّلت تلك الميزة (`default` أو عبر المتغيّر):
  `RIN_FEATURES=fast,text rin run app.rin`.

### التحقق (`rin lib check`)
أخطاء: اسم/إصدار/نطاق غير صالح، دورة تبعيات، دورة ميزات، ميزة تشير إلى مجهول، اسم أو أمر `script` غير صالح، مسار غير آمن في `files`/`workspaces`/`readme`،
تبعية نظير غير مطابقة. تحذيرات: ترخيص ليس SPDX، رابط غير صالح، منصة مجهولة، أكثر من 20 كلمة مفتاحية، تبعية مكرّرة بين الأقسام،
حقول الصيغة 2 مع إعلان `"disconom": 1`.

## مترجم Disconom (`rin lib translate`)
يحوّل وصف المكتبة بين الصيغ **بلا فقدان** (وكل ما لا تحمله الصيغة الهدف يُسجَّل في «ملاحظة» — لا شيء يضيع بصمت):

| من / إلى | `json` | `xml` | `toml` | `npm` (package.json) | `rinpm` (rin.toml) |
|---|---|---|---|---|---|
| الحقول كلها | ✔ | ✔ | ✔ | ✔ (ما لا يعرفه npm تحت مفتاح `"disconom"`) | جزئي (الاسم/الإصدار/التبعيات/…) |
| الحقول المخصّصة `x-…` | ✔ | ✔ (`<extra>`) | ✔ | ✔ | ✘ (ملاحظة) |

```bash
rin lib translate lib/shapes.og.rin --to toml                 # يطبع على stdout
rin lib translate lib/shapes.og.rin --to xml --write          # يكتب disconom.xml بجوار المصدر (--force للاستبدال)
rin lib translate package.json --to json --write              # استيراد مشروع npm (الاسم @scope/x ⇒ scope_x، workspace:* ⇒ *)
rin lib translate lib/shapes.og.rin --to rinpm -o rin.toml    # إلى RinPM
rin lib translate x.og.rin --to json --strict                 # يفشل (رمز 1) إن لم يجتز التحقق
rin lib fmt lib/shapes.og.rin --write                         # إعادة كتابة بالشكل القانوني بنفس الصيغة (--check للـ CI)
```
الصيغة تُخمَّن من اسم الملف/محتواه، و`--from json|xml|toml|npm|rinpm` تحسمها يدوياً. كل ترجمة تُعاد قراءتها (round-trip) للتأكد من سلامة الناتج.

## ملف القفل `disconom.lock`
`rin lib lock <lib>` يكتب ملفاً حتمياً (بلا طوابع زمنية): كل تبعية محلولة بإصدارها وقيدها **وبصمة sha256 لمحتواها**.
`rin lib lock <lib> --check` يفشل (رمز 1) إن تغيّر إصدار أو محتوى تبعية أو أُضيفت/أُزيلت تبعية — مناسب للـ CI.
```json
{ "lockfile": 1, "root": {"name": "pro", "version": "2.3.1"},
  "packages": [ {"name": "base", "version": "1.4.0", "kind": "library", "range": "^1.0.0",
                 "fingerprint": "sha256:…", "dependencies": []} ] }
```

## السكربتات
`rin lib scripts <lib>` يسردها، و`rin lib script <lib> <name> --allow-scripts [-- args…]` يشغّل واحداً داخل مجلد المكتبة
(الأوامر تأتي من ملف المكتبة فلا تُنفَّذ إلا بـ `--allow-scripts` صراحةً؛ رمز الخروج يُمرَّر).

## كود C++ المساعد
يتطلب `rin --allow-native` (أو `RIN_ALLOW_NATIVE=1`) ومترجم C++. الدوال بعقد `RIN_FN(name)` (انظر `docs/packages-and-interop.md`).
الدوال المُعلَنة في `native.functions` تُولَّد لها أغلفة Rin تلقائياً. **الأغلفة تحلّ محلّ أي بديل Rin نقي بنفس الاسم**
عند توفر الكود الأصلي، وعند تعطيله يبقى البديل النقي (اكتبه في أي `.zpp`). `required: true` يجعل غيابه خطأً.

## دوال `rinlib.*`
| الدالة | الوصف |
|---|---|
| `rinlib.info(name)` / `list()` / `manifest(name)` / `exists(name)` | معلومات المكتبة (محمَّلة أو لا) — `info` يضيف `kind` و`loadMs`؛ ولتوكن الأداة يُرجع `{kind, name, root, source, bytes}` |
| `rinlib.kind(name)` / `resolve(name)` | نوع الحزمة (`library`/`tool-token`) وتفاصيل حلّها: `{kind, label, name, root, source, bytes}` حيث `source` = `disk`/`virtual`/`embedded` |
| `rinlib.tokens([dir])` / `libraries([dir])` / `packages([dir])` | سرد توكنات الأداة / المكتبات / الاثنين معاً (المكتبة تحجب التوكن بنفس الاسم) |
| `rinlib.graph(name)` | التبعيات المتعدّية بترتيب التحميل: `{ok, order, nodes, cycles, problems, missing}` (كشف الدورات وعدم توافق الإصدارات) |
| `rinlib.fingerprint(name)` / `rinlib.verify(name, hash)` | بصمة `sha256:…` لمحتوى المكتبة (مسارات + بايتات) أو التوكن، والتحقق منها |
| `rinlib.discover(dir)` / `files(name)` | اكتشاف مكتبات في مجلد / ملفات مكتبة |
| `rinlib.read(name, rel)` / `has` / `path` | ملفات المكتبة (مسارات نسبية فقط، بلا `..`) |
| `rinlib.json(name, rel)` / `rinlib.xml(name, rel)` | تحليل ملف بيانات؛ XML ⇒ `{tag, attrs, text, children}` |
| `rinlib.native(name, fn, args)` / `hasNative` / `nativeInfo` | استدعاء C++ مباشرة |
| `rinlib.check(path)` / `rinlib.scaffold(dir, name, {xml, native, …})` | فحص / إنشاء هيكل |

> أمان: `rinlib.read/json/xml/path/has` تقبل مساراً نسبياً فقط، وترفض `..` والمسارات المطلقة **والروابط الرمزية التي تخرج من مجلد المكتبة**.

## الأداة `rin lib`
`rin lib new <name> [--xml|--toml] [--native] [--dir D]` · `check` · `info [--json]` · `kind` · `list` · `tokens` · `packages` · `graph` · `fingerprint` · `run`
· **جديد:** `translate` · `fmt` · `lock` · `scripts` · `script`.
`check` يعمل على المكتبات (Disconom + صياغة كل جزء + التبعيات المتعدّية + بصمة) وعلى توكنات الأداة (صياغة).
أخطاء `check`: دورة تبعيات، إصدار تبعية غير مطابق، Disconom غير صالح. تحذيرات: تبعية غير موجودة (قد تكون حزمة RinPM).
ملف `.rinz` داخل مكتبة يُشغَّل وحده: `rin run mylib.og.rin/run.rinz` (تُربط الأجزاء ثم ينفَّذ هو مرة واحدة).

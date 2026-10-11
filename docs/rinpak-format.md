# RinPak — حزمة مشروع Rin (`.rinpak`)

| | |
|---|---|
| الاسم | **RinPak** (Rin Project Package) |
| الامتداد | `.rinpak` |
| نوع MIME | `application/vnd.rin.pak` |
| الإصدار الحالي | `specVersion = 1` |
| الحاوية | ZIP قياسي بترتيب عناصر ثابت (على نمط EPUB/ODF) |
| الأيقونة | `assets/branding/rinpak/` (SVG / PNG / ICO / ICNS) + `ic_rinpak_file.xml` داخل التطبيق |
| يحل محل | `.rinproj` (يبقى مدعوماً **للاستيراد فقط**) |

## 1) لماذا صيغة جديدة؟

`.rinproj` كان ZIP عادياً مع `rin.project.json`: لا بصمات تحقق، لا ترتيب مضمون، ويختلط المانيفست مع ملفات المشروع في الجذر. RinPak يضيف:

- **تعريف فوري للنوع**: أول عنصر `mimetype` غير مضغوط (يعمل مع `file` وأدوات الفحص).
- **فصل واضح**: الوصف في `META-INF/`، والمشروع كاملاً تحت `content/`.
- **سلامة**: SHA-256 لكل ملف + بصمة جذرية (`root`) — أي تعديل/قطع/ملف زائد يُرفض.
- **أمان**: حماية Zip Slip، حدود للحجم والعدد، رفض التكرار والعناصر غير المعلنة.
- **حتمية**: نفس المشروع ينتج نفس الحزمة (ترتيب ثابت، تواريخ ثابتة، ضغط حسب النوع).

## 2) هيكل الحزمة (التكديس)

```
MyApp.rinpak                         ← ZIP
├── mimetype                         [1] أول عنصر — STORED: application/vnd.rin.pak
├── META-INF/
│   ├── manifest.json                [2] ثاني عنصر — الهوية + قائمة الملفات + SHA-256
│   ├── signature.json               [3] (اختياري) توقيع Ed25519 على manifest.json
│   └── icon.png                     (اختياري) صورة مصغّرة للمشروع
└── content/                         شجرة المشروع كما هي، مرتبة بحسب بايتات UTF-8 للمسار
    ├── main.rin                     نقطة الدخول (إلزامية)
    ├── project.og.urin              نوع المشروع
    ├── lib/…                        المكتبات
    └── assets/…                     الصور والوسائط وأي ملفات ثنائية
```

قواعد التكديس عند التصدير:

1. `mimetype` أولاً، `manifest.json` ثانياً — ثم `signature.json` إن وُجد (ثالثاً حصراً) — ثم `icon.png` — ثم ملفات `content/`.
2. المسارات بفاصل `/` فقط، نسبية، بلا `..` ولا `\` ولا `:` ولا مقاطع فارغة.
3. **تُستثنى** الملفات/المجلدات المخفية (أي مقطع يبدأ بنقطة، مثل `.rin_state/`) وملف `rin.project.json` القديم و**الروابط الرمزية**.
4. الضغط: `STORED` للامتدادات المضغوطة أصلاً (png/jpg/mp4/zip/woff2/…)، و`DEFLATED` لغيرها.
5. المجلدات الفارغة لا تُحفظ.

## 3) المانيفست `META-INF/manifest.json`

```json
{
  "format": "rinpak",
  "specVersion": 1,
  "name": "MyApp",
  "type": "container",
  "entry": "main.rin",
  "contentRoot": "content/",
  "created": "2026-10-11T12:10:40Z",
  "generator": { "name": "RinStudio", "version": "1.0.0" },
  "icon": null,
  "stats": { "files": 4, "bytes": 150 },
  "integrity": {
    "algorithm": "SHA-256",
    "root": "0ad09448…25c7",
    "files": [
      { "path": "main.rin", "size": 15, "sha256": "b8079233…8180" }
    ]
  }
}
```

- حقول وصفية اختيارية: `version`، `description`، `author`، `license`، `minRinVersion`، `permissions[]`، `dependencies[]` (تُعرض في معاينة الاستيراد).
- `type` ∈ `container | table | ui | free | illust | html | html_js` (انظر `ProjectType`).
- **`root`** = SHA-256 لسلسلة أسطر `"<path>\0<size>\0<sha256>\n"` مرتّبة بحسب بايتات UTF-8 للمسار.
- المخطط الرسمي: `schemas/rinpak-manifest.schema.json`.

### الحدود والتصلّب الأمني
- ≤ 10 000 عنصر، ≤ 64MB للملف الواحد، ≤ 256MB للمحتوى كله — تُفحص من المانيفست **قبل** الفك، وأثناء الفك أيضاً.
- مسارات مرفوضة: مطلقة، `..`، `\`، `:`، مقاطع فارغة، محارف تحكم أو `<>"|?*`، مقطع ينتهي بنقطة/مسافة.
- **تصادم الأسماء** بين الحروف الكبيرة والصغيرة (`Lib.rin` ≙ `lib.rin`) يُرفض لأنه يفسد الفك على ويندوز/macOS.
- لا يُقبل عنصر غير معلن في المانيفست، ولا ملف معلن ومفقود.

## التوقيع الرقمي (Ed25519)

`META-INF/signature.json`:
```json
{ "algorithm": "Ed25519", "signs": "META-INF/manifest.json",
  "keyId": "25cc1f60f722132c", "publicKey": "<base64 32B>", "signature": "<base64 64B>" }
```
- الرسالة الموقَّعة = `"RinPak-v1\0"` + **بايتات manifest.json كما هي** (والمانيفست يضم `root` الذي يغطي كل الملفات، فالتوقيع يغطي الحزمة كلها).
- `keyId` = أول 16 خانة hex من SHA-256 للمفتاح العام الخام.
- **التوقيع الموجود وغير الصالح ⇒ رفض الاستيراد.** الحزمة غير الموقّعة تُقبل لكن المعاينة تحذّر «غير موقّعة».
- التوقيع يثبت **السلامة وهوية المفتاح**، لا «الثقة»: قارن `keyId` بمفتاح الناشر المعروف. (حزمة مجرَّدة من توقيعها تبدو غير موقّعة — لذا لا تعتمد غياب التحذير كدليل ثقة.)
- الأندرويد: مفتاح خاص بالجهاز يُنشأ عند أول تصدير داخل التخزين الخاص (`RinPakSigner`)، ويُوقَّع كل تصدير تلقائياً. المرجع: `rinpak.py keygen` ثم `pack --sign`.

## 4) التصدير (المشروع → `.rinpak`)

```
مجلد المشروع ──► جمع الملفات (بلا المخفي) ──► ترتيب حتمي
   ──► حساب SHA-256 + CRC32 + الحجم ──► بناء المانيفست + root
   ──► توقيع Ed25519 على المانيفست (إن وُجد مفتاح)
   ──► كتابة: mimetype → manifest → signature? → icon? → content/*  ──► MyApp.rinpak
```

- الكود: `RinPak.write()` ← `ProjectManager.exportProject()`؛ الواجهة: «تصدير .rinpak» في المشاريع/الملفات.
- الملف يُنشأ في `cache/project_exports/` ثم يُنسخ إلى «التنزيلات» بنوع `application/vnd.rin.pak` (حتى لا يضيف النظام لاحقة `.zip`).

## 5) الاستيراد (`.rinpak` → المشروع)

```
اختيار الملف / فتحه من مدير الملفات (alias: RinPakFileViewer)
   ──► الكشف: أول عنصر mimetype؟  نعم → RinPak | لا → .rinproj القديم
   ──► inspect: قراءة أول 3 عناصر فقط ← حوار معاينة (الاسم، النوع، الملفات، الحجم، المؤلف، الصلاحيات، حالة التوقيع)
   ──► موافقة المستخدم
   ──► extract (يتحقق من التوقيع أيضاً) إلى مجلد مؤقت .import_*/ مع التحقق أثناء الفك:
         • ترتيب العناصر + mimetype + المانيفست وإصداره + root
         • كل ملف مُدرج في المانيفست، وحجمه وSHA-256 يطابقان، بلا زيادة ولا نقص ولا تكرار
         • مسار آمن داخل الجذر، حدود: 10 000 عنصر / 256MB
   ──► وجود main.rin
   ──► اسم فريد (MyApp, MyApp_2, …) ثم rename ذري إلى projects/
   ──► كتابة project.og.urin بنوع المشروع
```

أي فشل ⇒ يُحذف المجلد المؤقت ولا يظهر مشروع نصف مستورد. بنية المشروع المستورد على القرص تساوي محتوى `content/` حرفياً:

```
filesDir/projects/MyApp/
├── main.rin
├── project.og.urin
├── lib/…
└── assets/…
```

## 6) التوافق مع `.rinproj`

| | `.rinproj` (قديم) | `.rinpak` (جديد) |
|---|---|---|
| تصدير | ✗ (أُوقف) | ✓ |
| استيراد | ✓ (يظهر في المعاينة «صيغة قديمة — بلا بصمات») | ✓ |
| تحقق SHA-256 | ✗ | ✓ |
| توقيع رقمي Ed25519 | ✗ | ✓ |
| بيانات وصفية (إصدار/مؤلف/صلاحيات…) | ✗ | ✓ |
| ترتيب ثابت / mimetype | ✗ | ✓ |

## 7) ربط الملف بالنظام

- **أندرويد**: `<activity-alias android:name=".RinPakFileViewer">` في `AndroidManifest.xml` (VIEW على `*.rinpak` أو MIME `application/vnd.rin.pak`) ⇒ فتح الملف يعرض معاينة الاستيراد.
- **لينكس**: `packaging/rinpak/rinpak-mime.xml`  •  **ويندوز**: `packaging/rinpak/rinpak.reg`  •  **macOS**: `packaging/rinpak/Info.plist.fragment.xml`.
- الأيقونات: `assets/branding/rinpak/rinpak_file.{ico,icns}` (أيقونة الملف) و`rinpak.{ico,icns}` (أيقونة التطبيق).

## 8) الأدوات والاختبار

```bash
python3 tools/rinpak.py keygen key.pem
python3 tools/rinpak.py pack   MyApp -o MyApp.rinpak --type container --sign key.pem --version 1.0.0 --author "me"
python3 tools/rinpak.py info   MyApp.rinpak
python3 tools/rinpak.py verify MyApp.rinpak
python3 tools/rinpak.py unpack MyApp.rinpak out/
python3 tests/rinpak_test.py        # 28 اختباراً: ترتيب، حتمية، عبث بالمحتوى، Zip Slip، تزوير root، التوقيع (تعديل/تبديل مفتاح/keyId/موضع)، التصادم، الروابط الرمزية…
```

`tools/rinpak.py` مرجع بايثون لنفس القواعد المطبّقة في `app/src/main/java/com/dlof/rinlang/RinPak.kt`.

تم التحقق أيضاً من التشغيل البيني: حزمة موقّعة بالمرجع البايثوني تُفتح ويُتحقق توقيعها بـ `java.security` (Ed25519) بنفس خطوات `RinPak.kt` (بادئة SPKI + فاصل النطاق).

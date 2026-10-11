# RinPak — صيغة جديدة لحزم مشاريع Rin (`.rinpak`)

استبدال تصدير `.rinproj` بصيغة احترافية: ZIP بترتيب ثابت (`mimetype` ← `META-INF/manifest.json` ← `content/`) مع SHA-256 لكل ملف وبصمة جذرية. التفاصيل: [`docs/rinpak-format.md`](docs/rinpak-format.md).

## الملفات
| الملف | التغيير |
|---|---|
| `app/.../RinPak.kt` | **جديد** — كتابة/فحص/فك الحزمة، تحقق SHA-256، **توقيع Ed25519**، بيانات وصفية، حدود وتصلّب أمني |
| `app/.../RinPakSigner.kt` | **جديد** — مفتاح توقيع خاص بالجهاز (يُنشأ عند أول تصدير) |
| `app/.../ProjectManager.kt` | التصدير عبر `RinPak.write`، الاستيراد/المعاينة تكتشف الصيغة (RinPak أو `.rinproj` القديمة) |
| `app/.../ProjectsActivity.kt` | معاينة أغنى، واستقبال `ACTION_VIEW` لفتح `.rinpak` من خارج التطبيق |
| `app/.../RinConsoleFormatter.kt` | `ArtifactKind.RINPAK` (MIME خاص بلا لاحقة `.zip`) |
| `app/.../FilesActivity.kt`، `FileIconResolver.kt` | اسم/أيقونة `.rinpak` |
| `AndroidManifest.xml` | `RinPakFileViewer` (ربط الامتداد) |
| `res/drawable/ic_rinpak_file.xml`، `strings_projects_files.xml` | أيقونة ونصوص |
| `assets/branding/rinpak/` | أيقونات SVG/PNG/ICO/ICNS (ملف + تطبيق) |
| `schemas/rinpak-manifest.schema.json`، `rinpak-signature.schema.json` | مخططا المانيفست والتوقيع |
| `packaging/rinpak/` | ربط الملف على لينكس/ويندوز/macOS |
| `tools/rinpak.py`، `tests/rinpak_test.py` | مرجع بايثون + 28 اختباراً |

**ملاحظة:** لم يُبنَ تطبيق أندرويد في بيئة العمل (لا SDK/Gradle متصل)، فشيفرة Kotlin تحتاج بناءً واحداً للتأكد؛ الصيغة نفسها مُختبرة عبر المرجع البايثوني.


---

## تحديث: RinPak على الجوال (أندرويد)

الصيغة لم تتغيّر (`specVersion = 1`، ومرجع بايثون والمخططات كما هي). التغييرات في التطبيق:

| الملف | التغيير |
|---|---|
| `RinPak.kt` | `verify()` (تحقق كامل بلا كتابة على القرص)، تقدّم وإلغاء في `extract`/`verify`، تحويل أعطال ZIP إلى رسائل عربية |
| `RinPakTrust.kt` | **جديد** — ثقة الناشرين (TOFU): OWN / TRUSTED / KNOWN / NEW / KEY_CHANGED / SIGNATURE_REMOVED |
| `RinPakActions.kt` | **جديد** — نسخ الحزمة الواردة مرة واحدة، حوار التصدير، المشاركة (`FileProvider`)، حفظ في التنزيلات |
| `ProjectsActivity.kt` | استيراد في خيط خلفي مع تقدّم، معاينة + ثقة، «تحقق فقط»، «استبدال»، استقبال `ACTION_SEND` |
| `ProjectManager.kt` | `importProjectPackage(replaceExisting, onProgress, isCancelled)`، بيانات وصفية للحزمة لكل مشروع، المجلدات المخفية لا تظهر كمشاريع، كنس بقايا الاستيراد |
| `FilesActivity.kt` | زر التصدير يستخدم نفس حوار التصدير/المشاركة |
| `AndroidManifest.xml` | فلتر `ACTION_SEND` (rinpak / octet-stream / zip) على `RinPakFileViewer` |
| `res/xml/file_paths.xml` | `cache/project_exports/` للمشاركة |
| `res/values{,-ar,-en,-es}/strings_rinpak.xml` | **جديد** — النصوص بأربع لغات |
| `app/src/test/.../RinPakTrustTest.kt` | **جديد** — اختبارات منطق الثقة |

**التحقق:** `RinPak.kt` و`RinPakTrust.kt` رُجمتا بـ kotlinc 2.0.21 واختُبرتا على JVM (27 فحصاً: حتمية، تقدّم، إلغاء، عبث/قطع، تشغيل بيني مع `tools/rinpak.py` في الاتجاهين، منطق الثقة)، واختبارات بايثون الـ 28 ما زالت تنجح. أما الأجزاء المرتبطة بواجهة أندرويد (`RinPakActions`، `ProjectsActivity`…) فلم تُبنَ لعدم توفر Android SDK في بيئة العمل — تحتاج بناءً واحداً.

### إضافة: أيقونة RinPak في الجهاز
- **أيقونة تطبيق مستقلة «RinPak»** في قائمة التطبيقات (`activity-alias` باسم `RinPakLauncher` → `ProjectsActivity`) بأيقونة Adaptive (`mipmap-anydpi-v26/ic_rinpak_launcher.xml`: صندوق/طرد ثلاثي الأبعاد بشريط تغليف أصفر وحرف R على وجهه فوق خلفية خضراء متدرّجة؛ المصدر `assets/branding/rinpak/rinpak_launcher_icon.svg`) مع نسخة أحادية للأيقونات المتجانسة (Android 13+). الضغط عليها يفتح شاشة المشاريع ثم منتقي ملفات الحزم مباشرة.
- **أيقونة الملف `.rinpak`**: تبقى `ic_rinpak_file` على `RinPakFileViewer` (تظهر في قائمة «فتح باستخدام»/المشاركة). ملاحظة: بعض مديري الملفات يرسمون أيقونة الملف بأنفسهم بحسب النوع ولا يسمحون للتطبيقات بتغييرها.

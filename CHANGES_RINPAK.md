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

# تحديث شامل: معاينة حية (Loomtime) + CLI لثلاث منصّات (Windows/Linux/macOS)

هذا الأرشيف يجمع **كل** الملفات الجديدة/المعدَّلة عبر المحادثة كاملة، بنفس
مسارات مجلداتها الأصلية. انسخها إلى جذر مشروعك (استبدال الملفات المعدَّلة،
إضافة الجديدة منها) ثم Sync/Rebuild على أندرويد + CMake على كل منصة سطح مكتب.

---

## 1) المعاينة الحية (Live Preview) لمحرّك Loomtime — أندرويد فقط

**جديد:**
- `app/src/main/java/com/dlof/rinlang/LoomViewTracer.kt`
- `app/src/main/java/com/dlof/rinlang/LoomPreviewManager.kt`
- `app/src/main/java/com/dlof/rinlang/LoomFabricView.kt`
- `app/src/main/java/com/dlof/rinlang/LoomPreviewActivity.kt`
- `app/src/main/res/layout/activity_loom_preview.xml`
- `app/src/main/res/drawable/bg_loom_device_frame.xml`
- `app/src/main/res/drawable/bg_loom_error_banner.xml`
- `app/src/main/res/drawable/bg_loom_inspector_panel.xml`
- `app/src/main/res/drawable/ic_refresh.xml`
- `app/src/main/res/drawable/ic_grid_overlay.xml`
- `app/src/main/res/drawable/ic_preview_eye.xml`

**معدَّل (إضافة فقط، لا حذف):**
- `app/src/main/java/com/dlof/rinlang/MainActivity.kt` — عند Run: فتح المعاينة
  تلقائياً إن وُجد `@view.<Kind>=name`؛ TextWatcher يحدّث الجلسة حيّاً كل تعديل.
- `app/src/main/AndroidManifest.xml` — تسجيل `LoomPreviewActivity` (singleTask).
- `app/src/main/res/values/colors.xml`, `values/strings.xml`,
  `values-ar/strings.xml`, `values-en/strings.xml` — ألوان ونصوص `loom_*` جديدة
  فقط، آخر كل ملف.

**كيف تعمل:** جلسة `RinEngine.LoomSession` حقيقية تبقى حيّة عبر شاشتين
(المحرر + المعاينة)، تُحدَّث حيّاً مع كل حرف دون فقدان حالة Warp (كعدّاد
ضُغط عليه)، وتُرسم شجرة Fabric بكسل بكسل على Canvas حقيقي (لا محاكاة).

---

## 2) CLI مستقل لثلاث منصّات سطح مكتب

كل منصّة تبني من **نفس** ملفات محرّك التفسير (`rin_lexer.cpp`, `rin_parser.cpp`,
`rin_interpreter.cpp`) بلا أي نسخ أو تفرّع — إصلاح أو ميزة تُضاف للغة نفسها
تنعكس تلقائياً في الأربعة (أندرويد + الثلاثة أدناه).

### Windows — `cli/windows/`
- `CMakeLists.txt`, `src/main.cpp`, `build.bat` (بناء تلقائي MSVC/MinGW),
  `README.md`, `.gitignore`

### Linux — `cli/linux/`
- `CMakeLists.txt`, `src/main.cpp`, `build.sh`, `README.md`, `.gitignore`

### macOS — `cli/macos/`
- `CMakeLists.txt`, `src/main.cpp`, `build.sh` (مع خيار `--universal`),
  `README.md`, `.gitignore`

### معدَّل (لضرورة توافقية على ويندوز فقط):
- `app/src/main/cpp/rin_interpreter.cpp` — استبدال `::stat`/`::mkdir(path, mode)`
  (POSIX فقط) بماكروهات `RIN_STAT`/`RIN_MKDIR` تُترجَم حسب المنصة عبر
  `#ifdef _WIN32`. **على أندرويد/لينكس/macOS تُترجَم لنفس الاستدعاء القديم
  حرفياً — لا تغيير في السلوك إطلاقاً هناك.** بدون هذا التعديل فقط، لا يُبنى
  rin.exe على ويندوز (خطأ ترجمة).

### بناء تلقائي عبر GitHub Actions (جميعها جديدة)
- `.github/workflows/windows-cli.yml` → Artifact: `rin-windows-x64`
- `.github/workflows/linux-cli.yml` → Artifact: `rin-linux-x64`
- `.github/workflows/macos-cli.yml` → Artifact: `rin-macos-universal` (arm64+x86_64)

كل واحد يبني وينشر تنفيذياً جاهزاً تلقائياً عند كل push — أي شخص يقدر يحمّل
`rin` جاهزاً دون تثبيت أي مترجم بنفسه (مهم لهدف "منتج/مجتمع عام").

---

## البناء والتجربة السريعة
```bash
# Linux / macOS
cd cli/linux   && ./build.sh          && build/rin --version
cd cli/macos   && ./build.sh --universal && build/rin --version

# Windows (من Developer Command Prompt أو أي cmd بعد تثبيت MinGW)
cd cli\windows && build.bat           && build\Release\rin.exe --version
```

## ملاحظة صادقة
لم أتمكن من تجميع (build) أياً من هذا فعلياً في هذه البيئة (لا Android SDK،
لا مترجم Windows/macOS، لا اتصال شبكة). كل الأكواد رُوجعت يدوياً بعناية —
خصوصاً توقيعات `Lexer::scanTokens`, `Parser::parse`, `Interpreter::run`,
`RinError`, و`RinEngine.LoomSession` — مقابل الـheaders/الملفات الفعلية في
مشروعك، لكن أول اختبار بناء حقيقي هو إما محلياً عندك أو عبر GitHub Actions
بعد الرفع.

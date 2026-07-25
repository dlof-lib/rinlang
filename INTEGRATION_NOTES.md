# ملاحظات دمج هذا التحديث مع مستودعك الكامل

هذا الرفع (zip) يحوي فقط جزءاً من مشروع RinLang (كما يوثّق `README.md` نفسه: `MainActivity.kt`, `PipelineRunnerActivity.kt`, `RinEngine.kt`, ملفات `cpp/`, و`tools/test_persistence.cpp`) — أي أن ملفات أساسية مثل `AndroidManifest.xml`, `build.gradle`, `res/layout/activity_main.xml`, `res/values/strings.xml`, `CMakeLists.txt`, `rin_parser.h/.cpp`, `rin_ast.h`, `rin_common.h`, و`RinSyntaxHighlighter.kt`/`CodeEditorController.kt`/`RinJobScheduler.kt` **غير موجودة في هذا الرفع تحديداً** رغم أن الكود المرفوع (وREADME) يعتمد عليها. لذلك لم أستطع تجربة/بناء (build) المشروع فعلياً هنا، لكن كل ما أضفته مصمَّم للاندماج المباشر مع تلك الملفات المفقودة عندما تدمجه داخل مستودعك الفعلي.

## ما الذي أُضيف في هذا التحديث؟

### 1) شاشة "الملفات" + إضافة/رفع ملفات
- `ProjectManager.kt`: يدير مشاريع Rin كمجلدات داخل `filesDir/projects/<name>/`، ويوفّر رفع (استيراد) ملف من الجهاز/Google Drive عبر SAF إلى داخل المشروع.
- `FilesActivity.kt` + `res/layout/activity_files.xml` + `res/layout/item_file.xml`: قائمة ملفات .rin داخل مشروع، مع زرّي "ملف جديد" و"رفع ملف"، وفتح/حذف كل ملف.

### 2) شاشة "أنشئ مشروع"
- `Project.kt`: نموذج بيانات بسيط (مشروع/ملف).
- `ProjectsActivity.kt` + `res/layout/activity_projects.xml` + `res/layout/item_project.xml`: قائمة المشاريع، إنشاء/إعادة تسمية/حذف مشروع، وفتحه (ينتقل لشاشة الملفات).

### 3) تكامل Rin مع لغات برمجة أخرى (استدعاء Rin من Python/Node/C/...)
- `app/src/main/cpp/rin_c_api.h` + `rin_c_api.cpp`: واجهة C مسطّحة (Flat C ABI) فوق نفس محرّك C++ الموجود (lex→parse→interpret)، تُبنى كمكتبة مشتركة عامة (`librin.so`/`.dylib`/`.dll`) لا علاقة لها بأندرويد.
- `bindings/CMakeLists.txt`, `bindings/python/rin.py`, `bindings/node/rin.js`, `bindings/c/example.c`, `bindings/README.md`: أمثلة استدعاء فعلية من بايثون/Node.js/C، وتنطبق بنفس الأسلوب على أي لغة أخرى (Rust عبر `extern "C"`, Go عبر `cgo`, C# عبر P/Invoke...).

### تعديلات على ملفات موجودة
- `MainActivity.kt`: يقبل الآن `EXTRA_PROJECT_NAME`/`EXTRA_FILE_NAME` (يفتح ملف مشروع مباشرة، ويحفظ التغييرات فيه بدل حوار SAF)، وأضفت زر تنقّل `btnProjects` (يحتاج إضافته لـ `activity_main.xml` — انظر أدناه).
- `RinEngine.kt`: زيادة تحميل زائد (overload) لـ `init(context, projectBasePath)` لتخصيص جذر save/installation لكل مشروع على حدة.

## خطوات الدمج المطلوبة منك يدوياً

1. **`AndroidManifest.xml`** (في مستودعك الحقيقي): أضف هاتين النشاطين:
   ```xml
   <activity android:name=".ProjectsActivity" android:exported="false" />
   <activity android:name=".FilesActivity" android:exported="false" />
   ```
2. **`app/src/main/res/layout/activity_main.xml`**: أضف زراً جديداً بمُعرِّف `@+id/btnProjects` (بجانب `btnPipeline`/`btnOpen` مثلاً) — نص الزر متوفر في `@string/btn_projects`.
3. **`app/src/main/res/values/strings.xml`**: ادمج محتوى `strings_projects_files.xml` المرفق داخل ملفك الأصلي (أو أبقِه كملف منفصل — أندرويد يدمج كل ملفات `values/*.xml` تلقائياً).
4. **`app/src/main/cpp/CMakeLists.txt`** (ملف NDK الأصلي لتطبيق أندرويد، غير `bindings/CMakeLists.txt`): أضف `rin_c_api.cpp` إلى قائمة المصادر إن أردت استخدام نفس الواجهة C أيضاً من داخل NDK (اختياري — غير ضروري لعمل التطبيق نفسه، فقط مفيد إن أردت مكتبة native إضافية مشتركة مع الأدوات الخارجية).
5. تأكد أن `build.gradle` (module) يفعّل `viewBinding`/AppCompat/RecyclerView/Material كما هو مستخدم أصلاً في `MainActivity.kt` — لم يتغيّر شيء هنا، الشاشات الجديدة تستخدم نفس المكتبات الموجودة أصلاً (`RecyclerView`, `AlertDialog`, `FloatingActionButton` من Material Components).

## تشغيل تكامل اللغات الأخرى (مستقل تماماً عن أندرويد)

```bash
cd bindings
cmake -B build && cmake --build build
python3 -c "
import sys; sys.path.insert(0, 'python')
from rin import Rin
r = Rin('build/librin.so')
print(r.run('print \"Hello from Rin, called from Python!\";'))
"
```

لكن ملاحظة: هذا يحتاج أيضاً `rin_parser.h/.cpp` و`rin_ast.h` و`rin_common.h` (غير موجودة في هذا الرفع) لأن `rin_c_api.cpp` يتضمّنها. أضِفها من مستودعك الكامل إلى `app/src/main/cpp/` قبل البناء.

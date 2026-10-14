# CHANGES_EDITOR_PRO — تطوير المحرر (دعم الحلقات + أدوات احترافية)
- `cpp/editor/rin_editor_tools.h` (جديد، header-only): تلوين سياقي لكلمات Loops v2/v3
  (`each repeat loop retry where reversed yield limit skip take sorted ... nobreak`، تسميات الحلقات،
  `break/continue name`، عوامل الاختزال `sum each ...`)؛ نطاقات الطيّ؛ هيكل الملف؛ إعادة محاذاة المستند؛
  قوالب (snippets) للحلقات و`fun/if/while/for`.
  التلوين يتبع منطق المحلل: `let loop = 1;` و`repeat("ab", 2)` تبقى معرِّفات/استدعاءات عادية.
- `rin_editor_engine.h/.cpp`: `computeFoldRanges` `computeOutline` `reindentDocument` (أمر تراجع واحد، لا يمسّ
  النصوص متعددة الأسطر) `expandSnippetAtCursor`؛ والكلمات السياقية ضمن الإكمال التلقائي.
- `rin_editor_jni.cpp` + `RinNativeEditor.kt`: `getFoldRanges` `getOutline` `reindentDocument` `expandSnippetAtCursor`
  `isSnippetTrigger` `snippetTitle`.
- `RinCodeEditorView.kt`: اختيار اقتراح مثل `each` يوسّع قالباً كاملاً؛ دوال عامة `formatDocument()` `getOutline()` `getFoldRanges()`.
- `RinKeywordDocs.kt`: توثيق كلمات الحلقات للضغط الطويل.
- اختبار: `sh tests/editor/run.sh` (74/74 على g++ مع ملفات tests/loops).
- لم يُبنَ: Kotlin و JNI (لا kotlinc/NDK هنا) ولم يُربط أي زر واجهة بـ formatDocument/getOutline/الطيّ بعد.

## ستايل الكود: مفاهيم دلالية + خط لكل مفهوم
- أنواع تلوين جديدة (20..26) في `rin_editor_engine.h` و`HighlightKind` (RinSyntax.kt): `Constant` (true/false/nil وثوابت `MAX_SIZE`)،
  `FunctionDef` (اسم الدالة بعد `fun`)، `Control` (if/else/while/for/return/break/continue/each/repeat/loop/retry/try/catch/throw/nobreak/`#do`/`#done`)،
  `Modifier` (where/yield/by/window/skip/take/sorted/distinct/... وعوامل `sum each`)، `Label` (تسمية الحلقة و`break outer`)،
  `Class` (كل `class X` واستخداماته + `Set`/`Env`)، `Property` (`obj.name`). `container`/`Section`/`Group`... تبقى `Type` (بنفسجي).
  المنطق في `classifyConcepts` (rin_editor_tools.h) فوق التصنيف السياقي للحلقات.
- `RinCodeEditorView.kt`: لون لكل مفهوم (ألوان فاتحة/داكنة في `colors.xml` و`values-night/colors.xml`) ونمط خط: **عريض** للكلمات المفتاحية/التحكّم/الأنواع/الأصناف/
  تعريف الدوال/الوسوم، *مائل* للتعليقات والمعدِّلات والتسميات. يُنفَّذ بـ `isFakeBoldText` + `textSkewX` (بلا خط بديل) فلا تتغيّر عروض الأحرف ويبقى المؤشر والتحديد مضبوطين.
- `AppSettings.kt`: مفتاح `syntax_font_styles` (افتراضياً مفعّل، بلا واجهة تبديل بعد).
- `syntaxes/rin.tmLanguage.json`: نفس المفاهيم لـ VS Code (`loops-and-concepts`).
- اختبار المحرر 90/90. لم يُبنَ Kotlin/XML/JNI هنا.

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

## ستايل قوالب الحلقات `@tmpl` في المحرر
- ثلاثة أنواع تلوين جديدة (27..29) في `rin_editor_engine.h` و`HighlightKind` (RinSyntax.kt):
  `TemplateKw` (`@tmpl` و`@body`) · `Template` (اسم القالب في التعريف وكل استعمال `@name(` مع علامة `@`) · `Parameter` (معاملات القالب وأسماء `as` وكل ظهور لها داخل جسم القالب).
- `classifyTemplates` (rin_editor_tools.h) تُشغَّل بعد `classifyConcepts`: تقرأ التعريفات أولاً فتلوّن الاستعمال ولو سبق التعريف،
  و`let tmpl = 1;` يبقى معرِّفاً عادياً. محلل Kotlin الاحتياطي يلوّن `@tmpl/@body` واسم القالب و`@name(`.
- الخط (يتبع إعداد `syntax_font_styles`): `@tmpl/@body` **عريض** برتقالي محروق (داكن: برتقالي فاتح) · اسم القالب/الاستعمال **عريض مائل** نيلي (داكن: بنفسجي فاتح) · المعاملات *مائلة* بنّية.
- `colors.xml` و`values-night/colors.xml`: `syntax_template` `syntax_template_kw` `syntax_parameter`. `RinCodeEditorView.kt`: لون ونمط لكل نوع.
- اختبار المحرر 105/105 (g++). لم يُبنَ Kotlin/XML هنا.

## خط مناسب لكل مفهوم (عائلة JetBrains Mono NL)
- تسعة ملفات `res/font/rin_code_*.ttf` (regular · medium · semibold · bold · extrabold · italic · medium_italic · bold_italic · light_italic)
  تحلّ محلّ `rin_tmpl_bold_italic.ttf` و`rin_tmpl_italic.ttf` (احذفهما). الرخصة OFL في `assets/licenses/JetBrainsMono-OFL.txt`.
  النسخة NL بلا ligatures، وعرض الحرف 0.6em في كل الأوزان، والمحرر كله يرسم بالعائلة نفسها (الخط الأساسي Regular) فيبقى
  عرض الخلية والمؤشر والتحديد وعلامات الأخطاء مضبوطة بلا رسم حرف بحرف.
- الوزن/النمط لكل مفهوم (`RinCodeEditorView.codeFontFor`):
  Bold: `if/each/return`… والأنواع والأصناف · ExtraBold: اسم الدالة بعد `fun` و`@tmpl/@body` · SemiBold: `let/fun/class` والثوابت و`@` ·
  Medium: النداءات `f()` والأرقام والأخطاء · Italic: المعدِّلات `where/yield/sorted` · Medium Italic: معاملات القالب ·
  Bold Italic: تسميات الحلقات وأسماء القوالب · Light Italic: التعليقات · Regular: النصوص والمعرّفات والخصائص.
- الشكل: `@tmpl/@body` داخل «حبة» مستديرة شفافة بلون الكلمة (حبة واحدة تشمل `@` و`tmpl`)، واسم القالب بخط سفلي ناعم.
- يتبع إعداد `syntax_font_styles` (معطَّلاً: Regular للكل). وإن تعذّر تحميل أي خط يعود الرسم للنمط الاصطناعي السابق. لم يُبنَ Kotlin/الموارد هنا.

## شكل الرموز (operators / punctuation / brackets)
- ستة أنواع تلوين جديدة (30..35) في `rin_editor_engine.h` و`HighlightKind`: `Compare` (`== != < > <= >= =/= =/ !` و`and`/`or`) ·
  `Punct` (`, ; : .` خافتة) · `Flow` (`|>` و`?`) · `Bracket0/1/2` (أقواس `() [] {}` بثلاثة ألوان بالتناوب حسب العمق). رموز الحساب `+ - * / % =` تبقى `Operator` وصار لها لون.
- `classifyPunctuation` (rin_editor_tools.h) تعمل آخر مصنِّف: قوس إغلاق بلا فتح أو غير مطابق يُصنَّف `Error` (أحمر). محلل Kotlin الاحتياطي يلوّن `OPERATOR`.
- الخط: ثلاثة خطوط JetBrains Mono **بـ ligatures** (`res/font/rin_sym_medium|semibold|bold.ttf`): الحساب Medium، المقارنة SemiBold، التدفق Bold.
  `==` `!=` `<=` `>=` `|>` `=/=` تُرسم كرمز واحد بعرض خليتين بالضبط (`fontFeatureSettings = "calt, liga"` لهذه الامتدادات فقط). الأقواس Medium، والفواصل Regular.
- ألوان جديدة: `syntax_operator` `syntax_compare` `syntax_punct` `syntax_flow` `syntax_bracket0/1/2` (فاتح وداكن).
- اختبار المحرر 115/115. لم يُبنَ Kotlin/الموارد هنا.

## نسخ / قص / لصق احترافي (RinCodeEditorView + ClipboardText.kt)
- `ClipboardText.kt` (جديد، منطق نقيّ بلا Android) + `ClipboardHistory`: توحيد نهايات الأسطر، حذف BOM/المسافات صفرية العرض/NBSP، تبويبات البداية → مسافات،
  إعادة إزاحة اللصق متعدد الأسطر ليلتحم مع إزاحة موضع المؤشر (وضع الكتلة / منتصف السطر)، سياج Markdown، معاينة، وسجلّ حافظة في الذاكرة فقط (25 عنصرًا، بلا تكرار).
- نسخ/قص بلا تحديد = السطر الحالي كاملًا (يُوسَم `rin_lines` فيُلصق فوق السطر الحالي بإزاحته ويبقى المؤشر في مكانه).
- أوامر جديدة: لصق كما هو · سجلّ الحافظة (نقرة = لصق، ضغطة مطوّلة = حذف) · نسخ/قص السطر · تكرار التحديد · تحديد السطر (يتوسّع بالتكرار) · نسخ كـ Markdown.
- لصق ملف ضخم (> 300 ألف حرف) يطلب تأكيدًا. وميض لحظي للنص الملصوق. اللصق/القص/استبدال التحديد = خطوة تراجع واحدة.
- الشريط العائم صار `ActionMode.Callback2` فيظهر عند موضع التحديد، يختفي أثناء سحب التحديد، ونقرة ثانية على المؤشر تفتحه (لصق/تحديد الكل/سجل).
- اختصارات لوحة المفاتيح: Ctrl+C/X/V · Ctrl+Shift+V (كما هو) · Ctrl+Alt+V (سجل) · Shift+Insert/Ctrl+Insert/Shift+Delete · Ctrl+A/D/L.
- قائمة «تحرير» في MainActivity: عناصر 18..26 للأوامر الجديدة. نصوص جديدة بالعربية/الإنجليزية/الإسبانية.
- اختبار: `KOTLINC=/path/kotlinc sh tests/editor/clipboard/run.sh` (28/28). لم يُبنَ المشروع كاملًا (لا Android SDK هنا)؛ فُحص تركيب Kotlin فقط.

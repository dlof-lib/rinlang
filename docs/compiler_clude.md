# compiler_clude — مكتبة إنشاء اللغات والقوالب والمكتبات

المسار: `indsin/piano/compiler_clude.og.rin/` · الكلمات المحجوزة في **`ccludelike.txt`**.

## صيغة ccludelike.txt
```
@inlr.compiler_clude.main[];          // ترويسة: @نطاق.مكتبة.دخول[وسائط];
@import "other.txt";                  // استيراد ملف · @import "mylib"; مكتبة · … as my
$token[]                              // مجموعة token (فارغة)
$token[ ; : / ]                       // رموز محجوزة (الأطول تُطابَق أولاً)
$token.keyword[ let fun if else ]     // مجموعة مسمّاة ($op[ => :: ] أيضاً)
container.inlr [ img , text ]@        // حاوية: inlr و img و text محجوزة، وتُكتب inlr.img
print                                 // كلمة محجوزة مفردة
```
- تنتهي الجملة بسطر جديد أو `;` خارج `[ ]` أو `@` بعد `]`؛ والجملة قد تمتد على أسطر داخل `[ ]`.
- التعليقات `//` و`/* */`. لرمز `//` اكتبه مقتبساً `"//"`.
- `@import "x" as ns`: كل كلمات x تصير أعضاء حاوية `ns` (تُكتب `ns.كلمة`)؛ بلا `as` تُدمج مباشرة.
- `@import "compiler_clude"` يستورد الافتراضي المضمَّن. الدوران بين الملفات يُرفض بخطأ.
- الأخطاء تحمل `[ملف:سطر]`؛ التنبيهات لتعريف مكرَّر في مجموعة أخرى.

## توجيهات إضافية (1.1)
```
@title "Demo";  @version "1.0";  @ext[ dm , demo ];
@option case=insensitive;             // الكلمات المحجوزة تُعرَّف بأحرف صغيرة
@option line_comment="#";             // أو "" للتعطيل
@option block_comment="(* *)";        // فاتحة وخاتمة بمسافة
@option strings="'";                  // محارف فتح النصوص
$infix.10[ + - ]  $infix.20[ * / % ]  // يسار-الترابط بأولوية
$infixr.30[ ^ ]   $prefix.40[ - ! ]   // يمين-الترابط · أحادي (الكلمات مثل and تصلح أيضاً)
$deprecated[ goto ]                   // يُنبَّه عند استعمالها في cc_check_source
```
الفاصلة فاصل عناصر، فاكتبها رمزاً مقتبسة: `$token[ ( ) "," ]`.

## الواجهة
| الدالة | الدور |
|---|---|
| `cc_load(path)` · `cc_parse(نص)` · `cc_load_default()` | ملف/نص → `spec` |
| `cc_ok(spec)` · `spec["errors"]` · `spec["warnings"]` | الأخطاء |
| `cc_is_reserved` · `cc_group_of` · `cc_reserved_words` · `cc_reserved_symbols` · `cc_check_name(spec, اسم)` | استعلام |
| `cc_lex(spec, مصدر)` · `cc_lex_full` (مع التعليقات) | `{tokens, errors}` |
| `cc_format_spec` · `cc_format_tokens` · `cc_count_types` · `cc_used_reserved` | عرض وإحصاء |
| `cc_new_language(dir, name, ext)` · `cc_new_template(dir, name)` · `cc_new_library(dir, name)` | مشاريع جاهزة بـ ccludelike.txt |
| `cc_render_template(نص, {مفتاح: قيمة})` | تعويض `{{مفتاح}}` |
| `cc_main(specPath, srcPath)` | نقطة الدخول: تقرير كامل |
| `cc_cli([أمر, …])` | `check lex lint export json stats diff highlight eval new-language new-template new-library` |
| `cc_format_text(spec)` · `cc_save(spec, path)` | تصدير ccludelike.txt مرتّب (قراءته تعيد spec مكافئاً) |
| `cc_to_json` · `cc_from_json` · `cc_to_data` · `cc_from_data` | JSON ذهاباً وإياباً |
| `cc_diff(a, b)` · `cc_stats(spec)` · `cc_search(spec, بادئة)` | فرق · إحصاء · إكمال تلقائي |
| `cc_lint(spec)` · `cc_check_source(spec, مصدر)` | فحص التعريفات؛ وفحص مصدر مع «هل تقصد؟» وتنبيه المهجور |
| `cc_suggest(قائمة, كلمة)` · `cc_distance(a, b)` | أقرب كلمة (Levenshtein) |
| `cc_highlight_html` · `cc_highlight_ansi` · `cc_tmlanguage(spec, اسم)` | تلوين وقواعد TextMate |
| `cc_cursor` `cc_peek` `cc_next` `cc_at` `cc_accept` `cc_expect` | لبنات محلّل يدوي فوق الرموز |
| `cc_parse_expr` · `cc_parse_statements` · `cc_ast_format` · `cc_eval` · `cc_run_expr(spec, مصدر, env)` | محلّل Pratt ومقيِّم من أولويات `$infix` |
| `cc_render(نص, vars)` | قوالب: `{{x|upper}}` `{{a.b}}` `{{#if}}…{{else}}…{{/if}}` `{{#unless}}` `{{#each xs}}{{.}} {{@index}}{{/each}}` |

أنواع الرموز: `RESERVED` `MEMBER` `BAD_MEMBER` `SYMBOL` `IDENT` `NUMBER` `STRING` `PUNCT` `COMMENT` `EOF`، وكلٌّ فيه `value line col group container`.

```kotlin
@import "compiler_clude";
let spec = cc_load("ccludelike.txt");
let r = cc_lex(spec, "print inlr.img 12 ;");
print cc_format_tokens(r["tokens"]);
```

## ملاحظات
- أسماء Rin المحجوزة (`text` `file` `end` `from` …) لا تصلح أسماء متغيرات داخل المكتبة نفسها.
- المفسّر الجاهز `rin-linux-x64/rin` المرفق لا يحمّل مكتبات المجلدات `.og.rin/` ولا `piano` نفسها؛ لذلك `tools/bundle.py` يضمّ الأجزاء في ملف واحد `.og.rin`، وعلى هذا الملف شُغِّلت الاختبارات: `RIN=rin tests/run.sh`.

## RinStudio — اللغات تعمل في المحرّر
RinStudio يملك نظام «لغة مخصصة»: مجلد فيه `manifest.json` و`syntax.rinsyntax.json` (تلوين بـ regex) و`Lexer/Parser/Interpreter/CodeGen/run.rin`. compiler_clude يولّد كل ذلك من `ccludelike.txt`:

| الدالة | الناتج |
|---|---|
| `cc_export_studio(dir, spec, id, name, ext)` | مشروع كامل (ccludelike + manifest + syntax + المراحل الأربع + run + examples/hello.ext) |
| `cc_new_language(dir, name, ext)` | الشيء نفسه من قالب لغة جاهز + README |
| `cc_studio_syntax(spec, name, ext)` · `cc_studio_manifest(spec, id, name, dev, ext)` | نص JSON منفرد |
| `cc_ast_to_rin(ast)` | AST تعبير → كود Rin (`^`→`pow` · `str`→`toString`) لمرحلة CodeGen |

التلوين يأتي كلّه من التعريف: الكلمات المحجوزة → `keyword` · أعضاء الحاويات `inlr.img` → قاعدة خاصة · الرموز → `operator` · و`@option` (case/line_comment/block_comment/strings) تُحترم. القواعد بـ lookaround يونيكود فتعمل مع العربية.

**المثال الكامل:** `examples/customlang/mini/` — لغة mini (let/print/if/while/fun/return + عوامل بأولويات). افتح المجلد في RinStudio كمشروع لغة؛ ملفات `.mini` تُلوَّن، و`run.rin` ينفّذ `examples/hello.mini` مباشرة ثم يطبع كود Rin المُولَّد. اختبار التكافؤ (مباشر = مُولَّد) في `tests/compiler_clude/mini_lang.rin`.

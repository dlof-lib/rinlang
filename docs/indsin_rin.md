# indsin_rin.md — indsin يعتمد على لغة Rin

قبل هذا التغيير كان محرّك indsin يقيّم قيم خصائص `@view` بمُقيِّم مصغَّر (`evalAttrExpr`): أرقام ونصوص و`+ - * /` والشرط
الثلاثي فقط؛ أي شيء آخر كان `"<expr>"`، و`count > 3` كانت تُلصَق كنص `"53"`. الآن أي تعبير لا يفهمه المُقيِّم المصغَّر
يُفوَّض إلى **مفسّر Rin الحقيقي** بدلالات اللغة الكاملة، فتصبح الواجهة مبنية على متغيرات Rin ودوالها ومفاهيمها.

## ما صار ممكناً داخل قيمة أي خاصية (عدا `on<Event>`)
```rin
let title = "مرحباً"; let LIMIT = 3; let tags = ["a", "b", "a"];
let user = {"name": "ليلى"};
fun badge(n) { return "[" + n + "]"; }
warp count = 0; warp loading = false;

@view.Text=A  text=title;                              // متغيّر let عام (كان يُعرض اسمه)
@view.Text=B  text=upper(title) + badge(count);        // دوال Rin الأصلية و fun المعرَّفة
@view.Text=C  text=user["name"]; .end/view             // فهرسة قاموس/مصفوفة، و x.member
@view.Text=D  visible=count > LIMIT and !loading;      // مقارنات، and/or/!، %، ==
@view.Text=E  text=Set(tags).size();                   // Set/Array/Map حقيقية وطرقها
```
عند تغيّر أي `warp` يدخل التعبير تُعاد الحسابات تلقائياً (المتغيرات الحرّة تُسجَّل كاشتراكات).
وسائط المعالجات والـ actions (`set(total, price * qty)`) وحقول الكائنات تُقيَّم بالطريقة نفسها.
معالجات الأحداث `onTap=increment(count)` تبقى **وصف معالج** كما كانت.

## `indsin.*` — نظام التصميم كدوال Rin
قراءة (نقية، تعمل داخل الخصائص): `theme() themes() themeColor(role[,theme]) themeColors([theme]) token(cat,name)
tokens(cat) breakpoint(w) sizeClass(w) palette(seed) harmony(seed,kind) contrast(fg,bg) ensureContrast(fg,bg[,min])
onColor(bg) mix(a,b,t) lighten(c,k) darken(c,k) isLight(c) themeScore(theme)`.
تعديل (للبرنامج فقط، ممنوعة داخل الخصائص): `useTheme(name)` و`defineTheme(name, seed[, dark])`.
أخطاؤها تعدّد الخيارات الصحيحة (مثلاً الفئات/الأدوار المتاحة).

## حارس النقاء
التعبير داخل الخاصية يُحسَب نقياً. يُرفض (ويعود السلوك القديم `"<expr>"`) كل تعبير فيه إسناد أو `lambda` أو `|>`،
أو استدعاء ذو أثر جانبي: شبكة/ملفات/طباعة/`input`، وطرق التعديل في المكان (`push pop remove clear sort reverse add ...`)،
و`Set.add/remove/...`، و`indsin.useTheme/defineTheme`. دوال `fun` المعرَّفة مسموحة وخاضعة لميزانية التنفيذ.

## تغييرات سلوك
- الكلمة المجرّدة في خاصية (مثل `align=center`) تأخذ **قيمة المتغير العام `let` بنفس الاسم** إن وُجد، بدل أن تبقى نصاً.
  أسماء الدوال تبقى أسماء معالجات.
- المقارنات داخل الخصائص صارت حقيقية. بلا مضيف (مفسّر) يبقى السلوك القديم حرفياً.
- خلية warp أصلها مصفوفة/قاموس/Set تُمرَّر للتعبير بقيمتها الحقيقية ما دامت لم تتغيّر منذ البذر.

## التنفيذ
`rin_interpreter.h` + `rin_expr_host.cpp` (`lookupGlobal` `isCallableName` `evalExpression` `registerHostNative`) ·
`indsin/rin_indsin_eval.h` (واجهة `RinExprHost`، سياق القيمة، `evalAttrExprKeyed/evalAttrValue`) ·
`indsin/rin_indsin_rinbridge.h` (المضيف + فحص النقاء) · `indsin/rin_indsin_rinlib.h` (`indsin.*`) ·
ربط في `pipeline` و`c_api` (جلسة دائمة) و`strand/shuttle/tokens/needle/actions/object`.
الاختبار: `tests/tools/test_indsin_rin_bridge.cpp` · المثال: `examples/samples/indsin_rin_bridge_demo.rin`.

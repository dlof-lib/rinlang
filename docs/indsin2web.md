# indsin2web — المترجم الرسمي من Indsin إلى الويب

يُترجم أي ملف واجهة Indsin (صيغة الوسوم `<Column#root>` أو `@view.Column=root … .end/view`) إلى **صفحة HTML واحدة
مستقلة**: بنية دلالية (`button`, `input`, `header`, `hr`, `h1…h3` …) + CSS + JavaScript صغير بلا اعتماديات يحافظ على
دلالات المحرّك: `warp` حالة تفاعلية، وكل خاصية تعبير يُعاد تقييمه عند تغيّر أي warp يقرؤه.

```
source ─ indsin::tags::desugar ─▶ لهجة @view ─ rin::Lexer/Parser ─▶ AST ─ indsin2web ─▶ HTML + CSS + JS
```

لا يوجد محلّل ثانٍ: المترجم يستعمل نفس `desugar` والـ lexer/parser الرسميين، فأي ملف يقبله المحرّك يقبله المترجم بنفس
رسائل الأخطاء وأرقام الأسطر.

## البناء والاستعمال

```bash
bash scripts/build_indsin2web.sh          # أو: make indsin2web   (g++ بدعم C++20 فقط)
build/indsin2web app.rin -o app.html
build/indsin2web app.rin --check          # تحقق وتحذيرات بلا كتابة ملف
build/indsin2web app.rin --strict         # أي تحذير = رمز خروج 2 (للـ CI)
build/indsin2web app.rin -o out/index.html --app-dir out  # هوية container.app: head كامل + lang/dir + manifest (docs/container_app.md)
build/indsin2web app.rin --fragment       # <style>+<div>+<script> للتضمين في صفحة أخرى
build/indsin2web app.rin --report r.json  # تقرير JSON: العناصر/الروابط/التحذيرات
build/indsin2web app.rin --lang ar --dir rtl --title "تطبيقي"
```

الافتراضي `lang=en dir=ltr` كي تطابق الإزاحات `left/right` سلوك المحرّك؛ النص العربي داخل الفقرات يُعرَض RTL تلقائياً.
رموز الخروج: `0` نجاح · `1` خطأ في المصدر (`file:line: error: …`) · `2` تحذيرات مع `--strict` · `64` سوء استعمال.

## ما الذي يُترجَم

| في Indsin | في الناتج |
|---|---|
| `warp n = 0` | حالة `S.n`؛ أي إسناد يجدول إعادة تقييم كل الخصائص (دفعة واحدة لكل microtask) |
| خاصية تعبير (`width={zoom}`, `visible={a and !b}`, `bg={dark ? "#111" : "#fff"}`) | دالة JS تُعاد؛ لا تُلمَس العقدة إن لم تتغيّر القيمة |
| `onTap/onClick/onChange/onInput/onSubmit/onFocus/onBlur/onHover` | `addEventListener`؛ الجسم تعبير Rin (`count = count + 1`) أو نداء إجراء |
| إجراءات المحرّك: `increment decrement toggle set show hide enable disable select unselect reset multiply negate clamp cycle append backspace copy swap seek scrollTo submit seq` | `A.name("cell", …)` — نفس دلالات `rin_indsin_actions.h` |
| `fun` و`let` على مستوى الملف | دوال/متغيّرات JS (`if/while/for/for-in/return/break/continue`، `print` ← `console.log`) |
| `value=cell` / `checked=cell` على حقل | ربط ثنائي الاتجاه تلقائي |
| `@theme` و`<Palette seed=…>` و`<Theme>` | متغيّرات CSS `--in-primary…`؛ `color="primary"` تُحلّ إلى `var(--in-primary)` |
| `<Screen size=phone canvas="#6C5CE7,#A29BFE,160">` | عرض/ارتفاع الشاشة وخلفية (حتى تدرّج `c1,c2,angle`) |
| الرموز المميّزة: `spacious`, `soft`, `heading`, `elevation=md` … | نفس جداول `rin_indsin_tokens.h` |
| `indsin.isEmail/isPhone/isUrl/isNumber/passwordScore/Label/Tone/maskEmail/initials/slug/fieldError/themeColor/onColor/isLight` | منقولة إلى JS بنفس المنطق |

المكوّنات: Column/Row/Stack/Box/Card/Grid/Wrap/Header/TopBar/BottomBar/Drawer/Menu/Table/Text/Button/Image/Input/TextArea/
Checkbox/Switch/Radio/Slider/Select/Divider/Progress/Link/Video/Audio/WebView/Dialog/Badge/Tag/Avatar/List/Banner/Tabs…
مكوّن غير معروف يصبح `<div data-indsin-kind="…">` مع تحذير، ولا يتوقف الترجمة.

## حاويات الويب: تخزين محلي وجلب في الخلفية
| في Indsin | في الناتج |
|---|---|
| `@container.storage=prefs … .end/container.storage` | `const prefs = IN.storage(...)`: كائن تفاعلي عميق تُحفَظ حقوله في `localStorage` وتُستعاد عند فتح الصفحة — [`container_storage.md`](container_storage.md) |
| `@container.api=feed` مع `let baseUrl` | `const feed = IN.api(...)`: `fetch` في الخلفية (دوري/عند الطلب، `debounce`، إلغاء الأقدم، إعادة محاولة، كاش) تملأ `data/status/error/updated/loading` — [`container_fetch.md`](container_fetch.md) |
| `value={prefs.name}` / `checked={prefs.dark}` | ربط ثنائي الاتجاه بحقل حاوية |
| `feed.get(...)` · `prefs.reset("cart")` · `callApi("feed", "GET", "/x")` · `container.get/set` | نداءات مباشرة على كائن الحاوية |

مثال: `examples/samples/indsin_storage_fetch.rin`. دالة جديدة مضمَّنة: `removeAt(list, i)`.

## الصدق في التغطية (تحذيرات لا إخفاء)

المترجم **لا يتظاهر** بأنه ينقل ما لا ينقله. كل ما يلي يُنتج تحذيراً على stderr (ويُخزَّن كـ `data-*` أو يُهمَل):
* خاصية غير موجودة في جدول الويب · مكوّن غير معروف · استدعاء دالة ليست معرَّفة في الملف ولا مضمَّنة (مثل `time.*`, `xdb_*`)
* `@import` لمكتبات Rin (لا تُضمَّن) · عبارات Rin غير المدعومة داخل الدوال (try/catch، الفئات، …)
* `<<each list as it, i>>` على قائمة غير حرفية (مثل `inbox.data` من `container.api`): تُترجَم إلى قالب يكرّره الـ runtime لكل عنصر وتُطابَق العناصر بالموضع: يُعاد بناء العنصر الذي تغيّر محتواه وحده، ويُضاف الجديد ويُحذف الزائد، وغير المتغيّر يبقى بعقدته (تركيز الإدخال سليم). الربط ثنائي الاتجاه لحقل warp أو حاوية (`value={prefs.q}`) يعمل داخل الجسم (القيمة مشتركة بين العناصر)، أما ربط خاصية العنصر نفسه (`n["x"]`) فغير ممكن. قيود: لا تداخل بين `<<each>>`، وأقصى 2000 عنصر. المثال: `examples/samples/indsin_live_list.rin`
* `<Palette seed=…>` تقريب مبسّط للوحة tonal الكاملة في المحرّك (لا فحص WCAG كامل)
* ملفات الوسائط (`src="poster.png"`) تبقى مساراً نسبياً كما كُتبت

## الاختبار

`make test-indsin2web` (أو `bash tests/indsin2web/run.sh`): يترجم كل `examples/samples/indsin_*.rin`، ويتحقق أن كل خاصية يعلنها
المترجم لها معالج في الـ runtime، ومعالجة الأخطاء (رمز خروج + سطر)، و`--strict`، وهروب `</script>`، ويشغّل
Chromium (إن توفّر node+playwright) ليتأكد أن النقر يغيّر `warp` فتُطبَّق الخصائص فعلاً على الـ DOM.

## الملفات

* `tools/indsin2web.cpp` — المترجم (AST → HTML/JS)
* `tools/indsin2web_runtime.inc` — CSS الأساسي + runtime الصفحة (جدول الخصائص `P`، الحالة، الإجراءات، دوال `indsin.*`)
* `scripts/build_indsin2web.sh` · `tests/indsin2web/`


## الهوية مصدر للمتصفح
إذا وُجد `app.json` و`head.html` بجوار ملف `.rin` يُفعَّل `--app-dir` تلقائياً (`--no-app` للتعطيل). تُضمَّن الهوية في الصفحة كـ `<script type="application/json" id="app-identity">` ويصير `window.APP` (مجمَّد) متاحاً لسكربتات الصفحة: `APP.name` · `APP.version` · `APP.seo` · `APP.icons`.

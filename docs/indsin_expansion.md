# Indsin — التوسعة الاحترافية (Design System v2)

توسعة **إضافية بحتة** لمحرّك Indsin: لا تُغيّر معنى أي خاصية أو token أو action موجود، وكل ما هنا
اختياري. الاختبارات: `tests/tools/test_indsin_system.cpp` (168 فحصاً). مثال حيّ:
`examples/samples/indsin_system_demo.rin`.

| الملف | المحتوى |
|---|---|
| `indsin/rin_indsin_system.h` | توكنز موسَّعة، أدوات ألوان، مولّد ثيمات، فحص ثيم، تصدير JSON |
| `indsin/rin_indsin_query.h` | بحث/مسارات/إحصاءات/outline/كتالوج مكوّنات مصنَّف |
| `indsin/rin_indsin_audit.h` | مدقّق إمكانية الوصول والتخطيط |
| `indsin/rin_indsin_responsive.h` | تجاوزات `<key>_<breakpoint>` |
| `indsin/rin_indsin_tokens.h` | +5 ثيمات مدمجة، `resolveDurationToken` |
| `indsin/rin_indsin_effects.h` | easings وحركات جديدة، `stagger=`، `duration="fast"` |
| `indsin/rin_indsin_actions.h` | 8 actions جديدة |
| `indsin/rin_indsin_c_api.h/.cpp` | 8 دوال مصدَّرة + ربط التجاوب داخل `relayout` |

## 1) التوكنز الموسَّعة

| الفئة | القيم |
|---|---|
| elevation (`elevation=`) | `none/xs/sm/md/lg/xl` أو `0..5`؛ كل مستوى: blur/offsetY/spread/opacity. في الثيمات الداكنة `elevatedSurface()` يُفتّح السطح بدل الظل |
| z (`layer=` / `z=`) | `base 0 · raised 10 · dropdown 100 · sticky 200 · drawer 300 · dialog 400 · toast 500 · tooltip 600` |
| breakpoint | `xs <360 · sm ≥360 · md ≥600 · lg ≥840 · xl ≥1200` ؛ `windowSizeClass`: compact/medium/expanded |
| duration | `instant 0 · fast 120 · normal 250 · slow 400 · slower 600` (ms) |
| opacity | `invisible 0 · faint .08 · subtle .16 · disabled .38 · medium .6 · strong .87 · opaque 1` |
| border | `none 0 · hairline 1 · thin 1 · medium 2 · thick 4` |
| icon | `xs 12 · sm 16 · md 20 · lg 24 · xl 32 · xxl 48` |
| touch target | حدّ أدنى 44px، موصى به 48px |

`resolveToken(category, name, out)` يستعلم أي فئة بالاسم (spacing/radius/typography تشمل القديمة).

## 2) أدوات الألوان

- `tonalPalette(seed)` — سلّم 50..900؛ الخطوة 500 = البذرة تماماً، والإضاءة تتناقص رتيباً.
- `ensureContrast(fg, bg, min=4.5)` — أقل تعديل ممكن (بحث ثنائي) ليبلغ التباين الحدّ؛ يتجه نحو الأبيض أو الأسود حسب الخلفية.
- `onColor(bg)` — لون النص المناسب فوق خلفية.
- `harmonyColors(seed, kind)` — complementary / analogous / triadic / split / tetradic.

## 3) الثيمات

مدمجة جديدة (تُفعَّل بـ `@theme=<Name> active=true;`): **HighContrastDark**, **HighContrastLight**
(نص/خلفية ≥ 7:1 — AAA)، **Sepia**، **Forest**، **Rose**.

- `themeFromSeed(name, seed, dark)` — ثيم كامل (12 دوراً) من لون واحد، مضمون ألا يفشل فحص التباين.
- `deriveTheme(base, name, overrides)` — وراثة مع تجاوز أدوار محددة؛ الأدوار المجهولة تُعاد في `unknownRoles`.
- `validateTheme(theme)` — تقرير WCAG: `THEME-TEXT-BG`، `THEME-TEXT-SURFACE`، `THEME-MUTED-*`،
  `THEME-PRIMARY-BG`، `THEME-ON-PRIMARY` (أخطاء)، وألوان الحالة والحدّ (تحذيرات). `score()` من 0 إلى 100.

## 4) الحركات

- Easings جديدة: `cubicIn/cubicOut/cubicInOut`, `sine`, `back`, `bounce`, `elastic`, `spring`
  (القديمة بقيت بقيمها). back/elastic/spring تتجاوز 1.0 عمداً؛ الـ opacity مقصوصة في `[0,1]`.
- حركات جديدة: `pulse` (نبضة تكبير)، `shake` (اهتزاز أفقي متلاشٍ)، `bounce` (قفزة رأسية)، `grow`/`pop` (شفافية + تكبير من 0).
- `duration="fast"` (توكن) أو رقم بالميلي ثانية.
- `stagger=<ms>` على **الحاوية**: أبناؤها المباشرون يبدؤون متتابعين بفارق i×stagger (لا يُورَّث للأحفاد).

## 5) Actions جديدة (في `onTap=` وداخل `seq()`)

| الدالة | الأثر |
|---|---|
| `copy(dst, src)` | `dst := src` |
| `swap(a, b)` | تبديل قيمتين |
| `cycle(cell, v1, v2, ...)` | الانتقال للقيمة التالية بدورة؛ القيمة المجهولة → الأولى |
| `clamp(cell, min, max)` | حصر الرقم في المدى (يتسامح مع حدود مقلوبة) |
| `multiply(cell[, k=2])` / `negate(cell)` | ضرب / عكس إشارة |
| `append(cell, text)` | إلحاق نص |
| `backspace(cell)` | حذف آخر **حرف** (UTF-8: العربية والإيموجي لا تتقطّع) |

## 6) تصميم متجاوب

```
@view.Grid=cards
  columns=1; columns_md=2; columns_lg=3; columns_xl=4;
  gap=8; gap_lg=24;
.end/view
```

أي خاصية + لاحقة `_sm/_md/_lg/_xl`. التطبيق mobile-first (الأكبر الأقرب للعرض يفوز)، قبل التخطيط، وفي
كل `relayout`. آمن للتكرار (idempotent) وقابل للتراجع: تُحفظ القيمة الأصلية في `_base_<key>`
(بما فيها ربط Warp) وتُستعاد عند العودة لشاشة أصغر؛ والخاصية التي لا أساس لها تُزال.
`applyResponsiveAttrs(fabric, width)` تعيد عدد الخصائص التي تغيّرت.

## 7) المدقّق (Audit)

| الكود | الشدّة | القاعدة |
|---|---|---|
| A11Y-001 | error | عنصر تفاعلي بلا اسم (`label/text/placeholder/a11y_label`) |
| A11Y-002 | error | `Image` بلا `alt` (إلا `decorative=true`) |
| A11Y-003 | warning | هدف لمس أصغر من 44px |
| A11Y-004 | error | تباين نص < 4.5:1 (3:1 للنص الكبير) — مع اقتراح لون مقبول |
| A11Y-005 | warning | `Dialog` بلا عنوان |
| STRUCT-001 | warning | اسمان متطابقان بين الإخوة |
| STRUCT-002 | warning | `Text` فارغ |
| STRUCT-003 | warning | عمق > 12 |
| LAYOUT-001 | warning | عنصر يتجاوز عرض الشاشة (عدا الطبقات المنبثقة) |

الدرجة = `100 − 10×errors − 3×warnings`. الخلفية الفعلية تُحسب من أقرب سلف (`tone`/`bg`/Card...)
وإلا خلفية الثيم النشط، فلا إنذارات كاذبة في الثيمات الداكنة.

## 8) واجهة C (تُعيد JSON/نص؛ حرّرها بـ `rin_free_string`)

| الدالة | الناتج |
|---|---|
| `rin_indsin_session_audit_json(session)` | تقرير المدقّق للشجرة الحالية |
| `rin_indsin_session_stats_json(session)` | عقد/عمق/أنواع/تصنيفات |
| `rin_indsin_session_outline(session)` | مخطط نصي مُزاح |
| `rin_indsin_tokens_json()` | كل مقاييس التوكنز + الثيم النشط + أسماء الثيمات |
| `rin_indsin_catalog_json()` | تصنيف المكوّنات (kind/category/role/interactive/overlay) |
| `rin_indsin_palette_json(seed)` | سلّم 50..900 |
| `rin_indsin_theme_from_seed_json(seed, dark)` | `{theme, report}` |
| `rin_indsin_validate_theme_json(name)` | تقرير WCAG لثيم مسجَّل |

## ملاحظات

- اسم `save` محجوز في المحلّل فلا يصلح اسماً لعنصر (`@view.Button=save` يفشل)؛ استخدم اسماً آخر.
- في صيغة `.rin` يُكتب اسم العنصر أولاً ثم كل خاصية تنتهي بـ `;` (`@view.Text=t` ثم `text="x";`).
- لم يُبنَ بعدُ لأندرويد/WASM ولا رُبطت دوال C الجديدة من Kotlin (`RinEngine.kt`/`jni_bridge.cpp`).

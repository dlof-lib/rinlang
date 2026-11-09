# indsin2web Pro — إمكانيات احترافية للويب والتطبيقات

إضافة بحتة فوق [indsin2web](indsin2web.md): لا يتغيّر معنى أي خاصية/مكوّن موجود. مثال حيّ: `examples/samples/indsin_pro_web.rin`،
اختبار متصفح: `tests/indsin2web/browser_pro.js` (ضمن `bash tests/indsin2web/run.sh`).

## 1) لواحق الاستجابة والوضع الداكن (على أي خاصية)
`padding=16 padding_md=32` · `columns=1 columns_md=2 columns_lg=3` · `bg="#fff" bg_dark="#111"` · `display_print="none"`
نقاط التوقف نفس المحرّك: `sm ≥360 · md ≥600 · lg ≥840 · xl ≥1200`؛ mobile-first، تُعاد عند تغيير النافذة/النظام. `_dark` يتبع `prefers-color-scheme` أو `setTheme`.
اللاحقة بلا خاصية أساسية تُنتج **تحذيراً** (لا قيمة افتراضية تُستعاد).

## 2) خصائص جديدة
| الفئة | الخصائص |
|---|---|
| تخطيط | `columns_min` (شبكة auto-fill) · `col_span` · `row_span` · `align_self` · `order` · `sticky` · `inset` · `center` · `overflow_x/y` · `snap` · `border_top/bottom/left/right` |
| مظهر | `glass` · `blur` · `backdrop_blur` · `filter` · `ring` · `outline(_color)` · `text_gradient` · `clip="circle|pill"` · `truncate` · `nowrap` · `text_align` · `select` · `pointer_events` |
| حالات | `hover_bg/color/scale/opacity/elevation/lift` · `active_scale` · `focus_ring` · `transition="fast|normal|slow|ms"` — CSS خالص (بلا JS عند المرور) |
| حركة | `animate="fadeIn|slideUp|slideDown|slideLeft|slideRight|scaleIn|pulse|shake|bounce|spin|float|blink"` · `duration` · `delay` · `repeat` · `stagger` (على الحاوية: الأبناء متتابعون) |
| مكوّنات | `selected` · `active` · `level` · `items` · `current` · `total` · `pages` · `page` |

## 3) مكوّنات جديدة (وسوم دلالية + CSS)
`Accordion`(details، `title`/`open`) · `Rating`(`value`+`max`، ثنائي الاتجاه مع warp) · `Steps`(`items`+`current`) · `Breadcrumb`(`items="A,B"` أو مصفوفة) ·
`Pagination`(`current`/`total` أو `page`/`pages`، ثنائي الاتجاه) · `Carousel` · `Timeline`/`TimelineItem` · `Sheet` · `Fab` · `Meter` · `Code`/`CodeBlock` · `Quote` ·
`Heading level=1..6` · `Paragraph` · `Section` `Main` `Article` `Sidebar` `Nav` `Footer` `Form` `Label` `Center` `Page`(حاوية 1200px) `Hero` `EmptyState` `Alert`(مع `tone`) `TabPanel`.
وأُضيف CSS كان ناقصاً لـ `Skeleton` (shimmer) و`Spinner` و`Tooltip` و`Table/TableRow` و`Tabs/TabItem` (`selected`).
ملاحظة صيغة الوسوم: أسماء مثل `Tabs/Breadcrumb/Pagination/Alert/Section/Hero/EmptyState/Footer/Fab` هي ماكرو مدمجة في الـ desugar؛ للوصول لهذه الأنواع اكتب `<View.Breadcrumb …/>`. الأوسمة ذات الأبناء النصّيين هي المعروفة فقط (Text/Button…)، وإلا استعمل `text=`/`label=`.

## 4) إجراءات جديدة (في `onTap` وغيره)
`toast(msg[, ms])` · `copyText(t)` · `openUrl(url)` · `share(title, text, url)` · `scrollTop()` · `setTheme("dark"|"light"|"auto")` · `toggleTheme()` (يُحفَظ في localStorage).

## 5) ثيم وإمكانية وصول
`data-theme` يتجاوز تفضيل النظام؛ `color-scheme` مضبوط؛ حلقة تركيز `:focus-visible` افتراضية؛ `role`/`aria-*` للتبويبات والتقييم والخطوات والصفحات؛ احترام `prefers-reduced-motion`؛ CSS للطباعة.

## 6) خيارات سطر الأوامر
```
indsin2web app.rin -o dist/app.html --description "…" --theme-color "#6C5CE7" --og-image URL
indsin2web app.rin -o dist/app.html --pwa [--icon icon.png]   # manifest.webmanifest + sw.js (شبكة أولاً ثم كاش) + تسجيل تلقائي
```
`--pwa` يحتاج `-o`؛ يعمل التسجيل على http(s) فقط.

## حدود صريحة
* لا ينطبق على مسار المحرّك الأصلي (Android/المعاينة) — هذه توسعة لمترجم الويب فقط.
* `padding_md` بلا `padding` أساسية لا تُستعاد عند الرجوع (تحذير).
* `ring`/`elevation`/`glass` تكتب `box-shadow`/`background` نفسها: آخر خاصية مُصرَّح بها تفوز.
* `Rating`/`Pagination` ثنائيا الاتجاه عبر ربط `value`/`page`/`current` بـ warp مباشرة.

---
# الجولة الثانية (Pro 2)
مثال: `examples/samples/indsin_pro2_showcase.rin` · اختبار: `tests/indsin2web/browser_pro2.js`.

## مسارات (تطبيق متعدد الشاشات بلا مكتبة)
`route="/data"` على أي عنصر ⇒ يظهر عند تطابق `location.hash` (يدعم `/users/:id` و`/docs/*` و`*`). `go="/data"` على زر/رابط ⇒ ينقل ويضبط `aria-current="page"` تلقائياً.
إجراءات: `navigate("/x")` · `back()`.

## مكوّنات بيانات
| المكوّن | الخصائص |
|---|---|
| `DataTable` | `columns="A,B"` أو مصفوفة · `rows={[[…],…]}` · `filter={q}` (تصفية حيّة) · `page_size` · فرز بالنقر على الترويسة (لوحة مفاتيح أيضاً) |
| `Stat` | `label` · `value` · `delta` (`-0.4%` ⇒ أحمر، غيره أخضر) |
| `ProgressRing` | `value` 0–100 · `label` |
| `Sparkline` · `BarChart` · `Donut` | `data={[…]}` · `labels="a,b"` — SVG/CSS بلا مكتبات، مع `aria-label` |
| `Dropdown` | `title` + أبناء القائمة؛ يُغلق بالنقر خارجاً أو Esc |
| `Marquee` | شريط متحرّك سلس (`speed` بالثواني، يتوقف عند المرور) |

## خصائص جديدة
`tip` + `tip_pos` (Tooltip لأي عنصر، يعمل بالتركيز أيضاً) · `reveal="slideUp|fadeIn|scaleIn…"` (كشف عند الدخول للشاشة) · `scroll_progress` (شريط تقدّم التمرير) ·
`count_to` + `count_prefix/suffix/decimals` (عدّاد متحرّك) · `hotkey="ctrl+k"` · `hover_border` · `focus_bg` · `focus_border` · `scrim` + `dismissible` (Dialog: Esc/نقرة خارجية ⇒ `onClose`).
أحداث جديدة: `onVisible` · `onClose` · `onSwipeLeft` · `onSwipeRight`.
إجراءات جديدة: `downloadText(name, text[, mime])` تصدير ملف من المتصفح · `installApp()` (PWA) · `navigate` · `back`.

## ملاحظات
* `columns`/`rows`/`filter` على `DataTable` تعني بيانات الجدول (وعلى غيره تبقى معناها القديم: أعمدة شبكة/صفوف نص/فلتر CSS).
* احترام `prefers-reduced-motion`: الكشف والعدّاد يُظهران النتيجة فوراً.

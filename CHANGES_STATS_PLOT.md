# CHANGES_STATS_PLOT.md — الإحصاء والرسوم البيانية

مبني فوق شجرة scope ← token ← number ← linalg_algo. **لا تغيير في أي دالة قائمة** — مكتبات جديدة بلغة Rin، والتغيير الوحيد في C++ هو الترويسة المولَّدة rin_stdlib_libs.h:

| الملف | التغيير |
|---|---|
| `lib/rinstat.og.rin` | جديد: توزيعات واختبارات وانحدار وPCA وbootstrap (docs/STATS.md) |
| `lib/rinplot.og.rin` | جديد: رسوم SVG/PNG معقّدة (docs/PLOT.md) |
| `lib/rinstat_adv.og.rin` | جديد: logit/ROC/لامعلمي/Fisher/سلاسل زمنية/بقاء/قوة/KDE2 |
| `lib/rinplot_adv.og.rin` | جديد: ROC/KM/ACF/forest/density2d/ridgeline وتقارير |
| `lib/rinauto.og.rin` | جديد: st_auto + pl_auto_report (تقرير HTML آلي ثنائي اللغة) |
| `lib/rinplot.og.rin` | توسيع: طبقتا raster/hrange وycats |
| `docs/STATS.md` `docs/PLOT.md` `docs/STATS_ADV.md` | جديد |
| `examples/auto_report.rin` | مثال التقرير الآلي |
| `tests/verification/stats_adv.rin/.expected` | اختبار محقَّق مقابل SciPy/NumPy |
| `tools/embed_ai_libs.py` `app/src/main/cpp/rin_stdlib_libs.h` | تضمين المكتبات في المفسّر |
| `docs/standard-library.md` | إضافة صفّين |
| `examples/stats_dashboard.rin` | مثال شامل |
| `tests/verification/stats_plot.rin/.expected` | اختبار محقَّق مقابل SciPy |

إعادة استعمال: الدوال الأصلية الإحصائية، `lib/rindf`، `mat*` (LINALG)، `img*`، `color*`، `number*`.
التضمين: أُضيفت المكتبات الخمس (rinstat, rinstat_adv, rinplot, rinplot_adv, rinauto) إلى `tools/embed_ai_libs.py` وولّدتُ `app/src/main/cpp/rin_stdlib_libs.h`؛ فصار `@import` يعمل بلا مجلد `lib/` (وأُدرجت معها rindf/rinml/rinai التي تعتمد عليها ولم تكن مضمَّنة في هذه الشجرة). بعد أي تعديل على هذه الملفات شغّل `python3 tools/embed_ai_libs.py`. هذا تغيير في C++ (الترويسة المولَّدة فقط)، فيلزم إعادة بناء المفسّر.

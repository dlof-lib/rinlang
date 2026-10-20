# RinStat المتقدّم + RinAuto

## `lib/rinstat_adv.og.rin` (يستورد rinstat)
| المجال | الدوال |
|---|---|
| انحدار لوجستي | `st_logit(X,y,names)` (IRLS فوق `matSolve/matInv/matMul`؛ OR، SE، z، p، CI، AIC، McFadden، LR) · `st_logit_predict` |
| تصنيف | `st_roc(y,scores)` (AUC من الرتب، Youden) · `st_confusion(y,pred)` |
| لامعلمي وغيره | `st_kruskal` · `st_wilcoxon` · `st_ks2` · `st_fisher` (دقيق 2×2) · `st_levene` · `st_pairwise(groups,adjust)` |
| اختيار آلي | `st_auto_compare(groups)`: طبيعية (JB) + تجانس (Levene) ← Welch / ANOVA / Mann-Whitney / Kruskal مع سبب الاختيار |
| سلاسل زمنية | `st_acf` `st_pacf` `st_ljungbox` `st_diff` · `st_ses` و`st_holt` (تُضبط معاملاتهما بـ `algoMinimize` الأصلية) |
| بقاء | `st_km(times,events)` (Greenwood، الوسيط) · `st_logrank` |
| قوة العيّنة | `st_power_t2(d,n,alpha)` · `st_sample_size_t2(d,alpha,power)` (تقريب طبيعي بحرج t؛ الفرق عن t غير المركزية ≈ 0.001) |
| أخرى | `st_kde2` (كثافة ثنائية) · `st_polyreg`/`st_poly_select` (فوق `algoPolyFit`) |

حدود: p في KS هو حدّ Kolmogorov التقاربي (متحفّظ للعيّنات الصغيرة)؛ Wilcoxon وMann-Whitney بتقريب طبيعي مع تصحيح الروابط؛ لا Cox ولا ARIMA.

## `lib/rinplot_adv.og.rin`
`pl_roc` `pl_km` `pl_acf` `pl_forest` `pl_logit_forest` `pl_density2d` `pl_ridgeline` `pl_confusion` `pl_forecast` `pl_polyfit` · تقارير: `pl_logit_report(fit,y,t)` `pl_ts_report(y,h,t)` `pl_survival_report([{name,t,e}],t)`.
النواة الآن تدعم أيضاً طبقتَي `raster` و`hrange` وتسميات المحور الرأسي الفئوية `ax["ycats"]`.

## `lib/rinauto.og.rin` — تحليل آلي كامل
```rin
@import "lib/rinauto.og.rin";
pl_auto_csv("data.csv", "report.html", "تقرير التجربة", "ar");   // أو "en"
let r = st_auto(df);   // التحليل فقط: profiles, correlations, comparisons, associations
```
يصنّف الأعمدة، وينمّط الجودة، ويختبر كل الارتباطات ومقارنات المجموعات والعلاقات الفئوية مع **تصحيح FDR**، ويكتب تقرير HTML واحداً (RTL للعربية) تتضمّنه الرسوم SVG. الفروق الدالة فقط تُرسم.

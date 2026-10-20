# RinPlot — الرسوم البيانية الإحصائية (`lib/rinplot.og.rin`)

`@import "lib/rinplot.og.rin";` (يستورد rinstat) — بادئة `pl_`. مشهد مجرّد واحد يُخرَج إلى **SVG** (نص، يدعم العربية) و**PNG** (دوال `img*` الأصلية). الألوان بـ `colorMix/colorParse/colorTextOn`، والتنسيق بـ `numberFormat/numberRound`.

```rin
let fig = pl_figure(960, 640, 1, 2, "عنوان");     // عرض، ارتفاع، صفوف، أعمدة
let ax  = pl_axes(fig, 0, 0);                      // أو pl_axes_span(fig, r, c, rs, cs)
pl_regplot(ax, xs, ys, {"predband": true});
pl_hist(pl_axes(fig, 0, 1), data, {"normal": true});
pl_save(fig, "out.svg");                           // .svg | .png | .html
```
**طبقات:** `pl_line pl_scatter pl_bar pl_band pl_area pl_errorbar pl_hline pl_vline pl_text pl_heat pl_pie`
**إحصائية:** `pl_hist` (كثافة+KDE+طبيعي) · `pl_box` · `pl_violin` · `pl_groups` · `pl_regplot` (نطاق ثقة/تنبؤ، R²، p) · `pl_qq` · `pl_ecdf` · `pl_timeseries` · `pl_corr_heatmap`
**أشكال كاملة:** `pl_dist_report` · `pl_compare_groups(df,g,v)` · `pl_pairplot(df,names,hue)` · `pl_regression_diagnostics(fit,t)` · `pl_pca_report(df,names,t)`
**ضبط:** `pl_title pl_xlabel pl_ylabel pl_xlim pl_ylim pl_xcats pl_legend pl_logx pl_logy pl_dark`، و`fig["fs"]` لحجم الخط. `pl_print(fig, cols)` للعرض في الطرفية.

الحدود: خط البكسل في PNG لا يُشكّل العربية ولا يدعم `±` (استعمل SVG)؛ محور ثانوي وتدوير النص غير مدعومين.

# RinStat — التحليل الإحصائي (`lib/rinstat.og.rin`)

`@import "lib/rinstat.og.rin";` — بادئة `st_`. **تركيب لا اختراع**: تعيد استعمال الدوال الأصلية (`mean median percentile iqr zscore movingAverage`)، و`lib/math` (`sampleVariance correlation`)، و`lib/rindf` (`df_col df_unique df_where`)، و**`mat*` الأصلية** (`matInv matMul matTranspose matEigSym` من docs/LINALG.md). المُضاف فقط ما لم يكن موجوداً: الدوال الخاصة، التوزيعات، الاختبارات.

| المجال | الدوال |
|---|---|
| دوال خاصة | `st_lgamma st_betainc st_gammainc st_erf` |
| توزيعات | `st_norm_pdf/cdf/ppf` · `st_t_cdf/ppf/p2` · `st_chi2_cdf/sf/ppf` · `st_f_cdf/sf/ppf` |
| وصفي | `st_summary st_sem st_skew st_kurtosis st_ci_mean st_hist st_kde st_ecdf st_qq st_outliers st_rank` |
| اختبارات | `st_ttest_1samp/welch/pooled/paired` · `st_anova` · `st_chi2_gof/indep` · `st_corr_test` · `st_spearman` · `st_mannwhitney` · `st_normality` · `st_cohens_d` · `st_p_adjust(ps,"bonferroni"\|"holm"\|"bh")` |
| انحدار | `st_ols(X,y,names)` (متعدد، معاملات+SE+t+p+CI+R²+F) · `st_linreg` · `st_linreg_band` · `st_ols_table` |
| متقدم | `st_bootstrap(arr,"mean"\|"median"\|"sd"\|"var",reps,level,seed)` · `st_pca(df,names)` |
| إطار بيانات | `st_groups` · `st_compare` (Welch أو ANOVA تلقائياً) · `st_corr_matrix` |
| عرض | `st_report(r)` · `st_print(r)` |

كل اختبار يعيد `{test, stat, df, p, n, ...}` (أو `nil` إن كانت البيانات غير كافية). القيم `nil` تُهمل تلقائياً. دقة الأرقام محقَّقة مقابل SciPy (`tests/verification/stats_plot.rin`).

الحدود: الدوال بعدد وسائط ثابت (لذا `opts` قاموس أخير)؛ `sort` يعدّل في المكان فنعمل على نسخة؛ Mann–Whitney بتقريب طبيعي؛ Jarque–Bera للطبيعية.

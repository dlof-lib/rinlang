# RinML / RinDF / RinAI — الذكاء الاصطناعي وتحليل البيانات في Rin

<p align="center"><img src="icons/rindf.svg" width="84" alt="rindf"> <img src="icons/rinml.svg" width="84" alt="rinml"> <img src="icons/rinai.svg" width="84" alt="rinai"></p>

ثلاث مكتبات بلغة Rin الخالصة (بلا اعتماد خارجي)، تعمل مع الحاويات `@container`:

| المكتبة | البادئة | الدور |
|---|---|---|
| `lib/rindf.og.rin` | `df_` | إطار بيانات (CSV، تصفية، تجميع، ربط، إحصاءات) |
| `lib/rinml.og.rin` | `nd_` `ml_` | جبر خطي، نماذج تعلّم آلة، شبكات عصبية، مقاييس |
| `lib/rinai.og.rin` | `ai_` | مساحات عمل داخل الحاويات، AutoML، أنابيب، جسر HTTP |

```rin
@import "lib/rinai.og.rin";   // يستورد rindf و rinml تلقائياً
```
شغّل الأمثلة من جذر المستودع: `rin run examples/ai/ai_container_demo.rin`
والاختبارات: `rin run tests/rinai_tests.rin`

## مثال سريع
```rin
let df = df_read_csv("data.csv");
df_print(df_describe(df));
let res = ai_auto_classify(df_dropna(df), "label", ["f1", "f2"], 1);
df_print(res["leaderboard"]);
ai_put_model("lab", "clf", res["best"], {});
print ai_predict("lab", "clf", {"f1": 0.4, "f2": 9.5});
```

## الربط بالحاويات
- مساحة العمل = حاوية `data` (`ai_workspace("lab")`) فيها: `datasets` و`models` و`log` وحقول `model_<name>`.
- كل مجموعة بيانات = حاوية `table` باسم `<ws>__ds_<name>` (حقل `records`).
- `ai_save_workspace` / `ai_load_workspace` يحفظان المساحة كاملة في ملف JSON واحد.
- حاوية `@container` تفاعلية تستطيع إعادة التدريب في `on update` عند `setState` (انظر المثال).
- `ai_remote_predict(url, payload)` يرسل POST JSON إلى نموذج مُستضاف (Docker/خادم) ويعيد `{ok,status,json,error}`.

## المرجع المختصر
- **rindf**: `df_from_records/rows/map` · `df_parse_csv(txt, ",")` · `df_read_csv` · `df_to_csv/write_csv` · `df_select/drop/rename` · `df_filter(df, fn)` · `df_where(df, col, op, v)` · `df_with_col` · `df_sort(df, col, desc)` · `df_groupby(df, key, col, "mean")` · `df_groupby_multi` · `df_join(a, b, key, "inner"|"left")` · `df_describe` · `df_value_counts` · `df_isna/dropna/fillna/fill_mean` · `df_to_matrix` · `df_standardize/minmax` · `df_one_hot` · `df_encode_labels` · `df_corr`
- **rinml**: `ml_linreg_fit(X,y,lambda)` · `ml_linreg_sgd` · `ml_logreg_fit` · `ml_knn_fit` · `ml_kmeans_fit` · `ml_nb_fit` · `ml_tree_fit` · `ml_mlp_init/fit/predict` · `ml_textnb_fit` (عربي/إنجليزي) · `ml_predict/predict_all` · `ml_accuracy/confusion/precision/recall/f1/mse/rmse/mae/r2` · `ml_kfold` · `ml_train_test_split` · `ml_scaler_*` · `ml_save/load`
- **rinai**: `ai_workspace` · `ai_put/get_dataset` · `ai_put/get_model` · `ai_predict` · `ai_log` · `ai_auto_classify/regress` · `ai_run_pipeline` · `ai_save/load_workspace` · `ai_remote_predict`

## ملاحظات
- النماذج قواميس بحقل `type` (قابلة لـ JSON). الدوال بعدد وسائط ثابت (قيد في المفسّر).
- لا تبدأ عبارة بأسماء متغيرات `row` / `text` / `state` / `style` (كلمات سياقية).
- التنفيذ مفسَّر، فالمكتبات مناسبة لبيانات صغيرة/متوسطة وللتعلّم والنماذج الأولية.

## التضمين في التطبيق/المفسّر
المكتبات الثلاث مضمّنة داخل المفسّر (`app/src/main/cpp/rin_stdlib_libs.h`) فيعمل `@import "lib/rinai.og.rin"` على أي منصة دون ملفات على القرص. بعد أي تعديل في `lib/rindf|rinml|rinai.og.rin` أعد التوليد:

```
python3 tools/embed_ai_libs.py
```

## الأيقونات
هوية بصرية موحّدة (مولَّدة بـ `python3 tools/gen_ai_lib_icons.py`، المعاينة في `docs/icons/preview.png`):

| الملف | الاستخدام |
|---|---|
| `res/drawable/ic_lib_<n>.xml` | رمز أبيض 24dp متعدّد الشفافية فوق بلاطة القائمة (يحترم `tint` الأبيض) |
| `res/drawable/bg_icon_gradient_teal|violet|indigo.xml` | بلاطة تدرّج لكل مكتبة (نفس شكل البلاطة الخضراء) |
| `res/drawable/ic_lib_<n>_badge.xml` | شارة ملوّنة كاملة 48dp (تدرّج + لمعة + ظل) للبطاقات والمشاركة |
| `docs/icons/<n>.svg` | الشارة بصيغة SVG |

الألوان: rindf فيروزي · rinml بنفسجي · rinai أزرق-نيلي، مع لون كهرماني `#FFD166` مميِّز موحّد للعائلة.
تُربط بالقائمة عبر `iconRes` و`tileRes` في `RinLibrary.kt`.

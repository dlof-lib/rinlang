# Indsin Pro — المحرّك الأصلي (Fabric → Indsin → Dye)

أُضيف للمحرّك الأصلي ثلاثة أنواع رسوم تُخطَّط وتُرسَم عبر **DrawList نفسها** (فتظهر تلقائياً في أندرويد والمعاينة والتصدير PNG/HTML بلا تعديل Kotlin):

| النوع | الخصائص | الرسم |
|---|---|---|
| `ProgressRing` | `value` 0–100 · `label` · `diameter` (افتراضي 84) · `tone` | مسار كامل + قوس تقدّم من الساعة 12 + نصّ بالمركز (`N%` افتراضياً) |
| `BarChart` (`Bars`) | `data="Mon:12,Tue:19"` أو أرقام · `colors` · `values=true` · `width`/`height` | أعمدة نسبية لأكبر قيمة + تسميات أسفلها |
| `Sparkline` | `data="3,5,4,8"` · `tone`/لون · `width`/`height` | خط مستمر + منطقة باهتة تحته |

اصطلاح `data="تسمية:قيمة"` نفسه الذي يستعمله `DonutChart`. مثال: `examples/samples/indsin_pro_native.rin`.
الاختبار: `bash tests/indsin_pro_native/run.sh` (أنواع في الشجرة، أقواس DrawList وزواياها، ألوان بكسلات الأعمدة والخط).

## الملفات
`rin_indsin_strand.h` (النوع والوسوم والاسم) · `rin_indsin_layout.h` (القياس) · `rin_indsin_paint.h` (الرسم) · `rin_indsin_query.h` (تصنيف DATA).

## حدود صريحة (طبيعة المحرّك لا قصور في الإضافة)
* المحرّك **رسم ساكن** (DrawList)؛ لذلك **لا ينتقل إليه** ما هو تفاعلي/زمني في مترجم الويب: `hover_*` و`route`/`go` و`DataTable` (فرز/تصفية) و`reveal` و`count_to` و`hotkey` و`tip` و`Dropdown` المنسدل. هذه تبقى في `indsin2web`.
* لا عملية «خط» في `DrawOp`: يُرسم `Sparkline` كأعمدة بكسل واحد (160 خطوة كحد أقصى) فيبدو خطاً مستمراً.
* `ProgressRing` يستعمل `diameter` لا `size` (الأخيرة تُقرأ كحجم خط للنصّ المركزي). النصّ المركزي يبدأ من يسار مستطيله (سلوك `TEXT_RUN` الحالي نفسه كـ `DonutChart`).
* الخط المدمج للمعاينة ASCII فقط (قيد قائم).
* لم يُختبر على جهاز أندرويد هنا (لا بيئة بناء APK)؛ الاختبار عبر C API نفسه الذي يستعمله.

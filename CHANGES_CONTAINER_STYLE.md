# CHANGES_CONTAINER_STYLE — ستايل كتل الحاويات في المحرر (نسخة هادئة)

فلسفة التصميم: لغة بصرية واحدة بلا زخارف — خط جانبي + لون للوسم. بلا كبسولات ولا تعبئة خلفية.

كل كتلة `@container.*` / `Containers.Group` / `Volume` / `Section` / `Translations` / `make`:

1. **خط إرشاد رأسي رفيع** (≈1.3dp، أطرافه مستديرة) بلون عائلة الحاوية، بزاوية ┌ عند الفتح و└ عند الإغلاق.
2. **سطر الفتح**: الوسم `@container.pipe` بلون العائلة؛ كلمة `container` **مزخرفة بتدرّج لوني** (لون العائلة ← درجة مزاحة الـHue ≈38°) بوزن ExtraBold، والـ`@` ExtraBold، ولاحقة النوع `.pipe` Medium؛ وتحته خط شعري يتلاشى تدريجياً؛ `=` باهت؛ اسم الحاوية SemiBold بلون نص المحرر.
3. **سطر الإغلاق**: `.end/` باهت وكلمة `container` بنفس التدرّج (Bold)، واللاحقة Medium. التدرّج Shader على الـPaint فقط، فلا يتغيّر عرض الخلايا.
4. **التعشيش**: لكل مستوى خطه عند إزاحته. **الطيّ**: ينتهي الخط عند سطر الكتلة المطويّة. **كتلة غير مغلقة**: خط أخفّ حتى النهاية وبلا زاوية سفلية.
5. `.object("..")` يكتفي بالخط الجانبي. الأسطر المحدَّدة تُرسم عادية حتى لا يُمحى لون التحديد.

العائلات: CORE بنفسجي · PIPE تيل · DATA أزرق · API أخضر · IO برتقالي · GROUP ذهبي (فاتح/داكن في colors.xml). `@view/@element/@loop/@theme` خارج الستايل عمداً.

## الملفات
- `RinContainerTags.kt`: `ContainerFamily` / `ContainerBlock` / `buildContainerBlocks` (مختبَر بـ kotlinc).
- `RinCodeEditorView.kt`: `recomputeContainerBlocks`، `drawContainerBands` (الخط، قبل النص)، `drawContainerBadge` و`drawContainerClosing` (بعد النص، تعيد رسم الكلمات بنفس عرض الخلايا فلا ينزاح المؤشر).
- `AppSettings.kt` / `SettingsActivity.kt`: إعداد `container_block_style` (مفعّل افتراضياً).
- `colors.xml` (×2) و`strings_settings.xml` (ar/en/es + content-packs en/es).

> رسم `RinCodeEditorView` لم يُبنَ هنا (لا Android SDK) — ابنِه بـ Gradle وراجعه على الجهاز.

# indsin — «شكل» الثيم (Aurora)

الثيمات كانت **ألواناً فقط** وكل شيء بزوايا حادّة بلا ظلّ. الآن للثيم أيضاً «شكل» (shape tokens) يُطبَّق على كل العناصر دفعة واحدة، دون كتابة `radius=`/`elevation=` على كل عنصر. الثيمات القديمة (Dark, Light, Midnight…) **لم تتغيّر** (شكلها الافتراضي صفر).

| الرمز | المعنى |
|---|---|
| `radius` | النصف الأساسي: الزر 0.8× · البطاقة 1× · الصورة 1× · الحقل (Input/Search/Select) 0.7× |
| `cardElevation` | 0..5 ظلّ ناعم للبطاقات (جدول `elevation` نفسه) |
| `buttonElevation` | 0..5 ظلّ للأزرار الممتلئة (FILL) |
| `hairline` | حدّ 1px بلون `border` حول البطاقات |

## ثيمان جاهزان
`Aurora` (ليلي بنفسجي/فيروزي) و`AuroraLight` (فاتح هادئ): `radius=12`، `cardElevation=2`، `buttonElevation=1`، `hairline`.
```kotlin
<Rin>
@theme=Aurora active=true;
.end/theme
</Rin>
```
أو وقت التشغيل: `setTheme("Aurora")`.

## ثيم مخصّص
```kotlin
@theme=Mine primary="#FF6600"; shape="soft"; active=true;      // soft = radius 12 + ظلال + حدّ شعري · pill = 24 · sharp = لا شيء
.end/theme
@theme=Mine2 radius=16; cardElevation=3; buttonElevation=0; hairline=true; active=true;
.end/theme
```
القيمة الصريحة على العنصر تغلب الثيم دائماً: `<Button radius=0 />` يبقى حادّاً، و`<Card elevation=0 />` بلا ظلّ، و`<Card border=2>` يحلّ محلّ الحدّ الشعري.

## مثال ومعاينة
`examples/samples/github_browser.rin` (يستعمل Aurora) · الصورة: `docs/indsin_aurora_preview.png` (قبل/بعد) · الاختبار: `tests/tools/test_indsin_theme_shape.cpp`.

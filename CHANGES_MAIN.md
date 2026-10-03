# تطوير main.rin: يبدأ بكود يمثّل لغة Rin

كل `main.rin` جديد يبدأ الآن بكود Rin حقيقي (لا تعليق) يعرّف اللغة نفسها من المحرّك الفعلي:

```rin
print "◆ Rin " + rinVersion() + " · " + rinEdition();
print "▸ اسم_المشروع";
```

عند Run يكون أول ما يظهر في الـ terminal هو توقيع Rin ونسخته وإصداره ثم اسم المشروع.
ينطبق على كل الأنواع (Container / Table / UI / Free / Illust / HTML)؛ ويبقى قالب Free «فارغ» فارغاً تماماً كما اختار المستخدم.

## قالب Free الافتراضي صار جولة سريعة في ما يميّز Rin
```rin
// 1) warp: متغيّر حيّ، تتحدّث أي واجهة مرتبطة به عند تغيّره
warp score = 0;
score = score + 10;
print "score:", score;

// 2) الأنابيب |> : مرّر القيمة عبر سلسلة دوال
fun double(x) { return x * 2; }
fun inc(x) { return x + 1; }
print "pipeline:", 5 |> double() |> inc();

// 3) الحاويات @container: بيانات وسلوك داخل كتلة مسمّاة
@container=Main
    warp counter = 0;
    print "counter:", counter;
.end/container
```

الملف المعدَّل: `app/src/main/java/com/dlof/rinlang/ProjectManager.kt`
(`mainRinTemplateFor` ← `rinIdentityPrologue` + `mainRinBodyFor`).

## الاختبار
شُغِّل الناتج على محرّك Rin الحقيقي (Linux): السطران الأولان يطبعان `◆ Rin 1.0.0 · 2026` و`▸ اسم`، والجولة
تعطي `score: 10` و`pipeline: 11` و`counter: 0`. جُرِّبت أيضاً البادئة قبل قوالب UI وTable دون أي مشكلة.

# تحديث: ربط المحرر بمعاينة حية (Live Preview) لمحرّك Loomtime

انسخ هذه الملفات إلى نفس المسارات داخل مشروعك (استبدال المُعدَّل، إضافة الجديد) ثم Sync/Rebuild.

## ملفات جديدة
- app/src/main/java/com/dlof/rinlang/LoomViewTracer.kt
- app/src/main/java/com/dlof/rinlang/LoomPreviewManager.kt
- app/src/main/java/com/dlof/rinlang/LoomFabricView.kt
- app/src/main/java/com/dlof/rinlang/LoomPreviewActivity.kt
- app/src/main/res/layout/activity_loom_preview.xml
- app/src/main/res/drawable/bg_loom_device_frame.xml
- app/src/main/res/drawable/bg_loom_error_banner.xml
- app/src/main/res/drawable/bg_loom_inspector_panel.xml
- app/src/main/res/drawable/ic_refresh.xml
- app/src/main/res/drawable/ic_grid_overlay.xml
- app/src/main/res/drawable/ic_preview_eye.xml

## ملفات مُعدَّلة (أُضيف إليها فقط، لا شيء حُذف)
- app/src/main/java/com/dlof/rinlang/MainActivity.kt
  - TextWatcher جديد على editCode يدفع كل تعديل (بعد 200ms تهدئة) إلى LoomPreviewManager.pushLiveEdit
    إن كانت جلسة معاينة حية قائمة.
  - runProgram(): إن وُجد @view.<Kind>=name في الكود، تُفتح شاشة المعاينة الحية تلقائياً.
  - عنصر قائمة جديد "معاينة حية" في قائمة Run (فتح يدوي بدون الحاجة لضغط Run).
- app/src/main/AndroidManifest.xml: تسجيل LoomPreviewActivity (launchMode="singleTask").
- app/src/main/res/values/colors.xml: ألوان loom_*.
- app/src/main/res/values/strings.xml
- app/src/main/res/values-ar/strings.xml
- app/src/main/res/values-en/strings.xml
  (كلها: إضافة نصوص loom_* و menu_run_live_preview فقط، آخر الملف.)

## كيف تعمل
1. عند الضغط على Run، إن وُجدت كتلة @view.، تُفتح LoomPreviewActivity وتبدأ جلسة
   RinEngine.LoomSession حقيقية (نفس محرّك Loomtime الأصلي في app/src/main/cpp/loom).
2. كل حرف تكتبه بعد ذلك في المحرر يُحدَّث حيّاً في نفس الجلسة (updateSource) دون فقدان حالة
   Warp (مثل عدّاد ضُغط عليه من قبل Button.onTap).
3. الرسم في LoomFabricView حقيقي وليس محاكاة: يقرأ JSON الـ Fabric (kind/x/y/w/h/attrs) الذي
   يُرجعه المحرّك C++ فعلياً ويرسمه بكسل بكسل (بطاقات، تدرّجات، ظلال، أزرار، صور، فواصل).
4. شريط الأدوات: إغلاق، اختيار عرض الجهاز (360/390/414/428/768px)، شبكة محاذاة، تكبير/تصغير،
   إعادة تشغيل الجلسة. لوحة "Snag" حمراء تظهر عند خطأ مع إبقاء آخر إطار سليم ظاهراً خلفها.
   ضغط مطوّل على أي عنصر يفتح لوحة "Inspector" بتفاصيله (kind/name/line/x/y/w/h/attrs).
   الشريط السفلي يعرض عدد العناصر الحقيقي، زمن الرسم الفعلي (ms)، وعدد إصابات الذاكرة المؤقتة.

## ملاحظة
لم أستطع تجميع (build) المشروع في هذه البيئة (لا يوجد Android SDK/NDK ولا اتصال شبكة)،
لذا الكود مكتوب ومُراجَع يدوياً بعناية لكنه لم يُختبر عبر Gradle فعلياً — راجعه بعد الدمج
وشغّل Build عندك للتأكد.

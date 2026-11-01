# android.md — استضافة Rin على أندرويد

> هذا الملف يغطّي تحديدًا **طبقة استضافة/تشغيل Rin** على أندرويد (كيف يُشغَّل
> برنامج `.rin` ويُعرَض على الشاشة) — وليس مرجعًا لتطبيق المحرِّر/IDE الكامل
> (تصدير APK، نظام الإضافات، إلخ)، وهو مشروع أندرويد ضخم بعشرات الملفات خارج
> نطاق توثيق اللغة نفسها.

## `RinEngine.kt` — جسر JNI الرئيسي
مكتبة أصلية واحدة (`System.loadLibrary("rinengine")`) تُحمَّل مرة عبر
`RinEngine.init(context)`، ثم كل شيء آخر دوال Kotlin عادية تستدعي دوال C++
عبر JNI (`external fun ...Native(...)`، خلف `jni_bridge.cpp`):

| دالة Kotlin عامة | الغرض |
|---|---|
| `runSource(source)` | تشغيل نص برنامج كامل، إرجاع المخرجات كنص (كما `print`/`show`). |
| `runSourceStructured(source)` | نفس التشغيل، لكن نتيجة مُهيكَلة (`RinExecutionResult`: نجاح/خطأ/سطر...). |
| `runSourceStructuredStreaming(source, listener)` | نفسها، مع بثّ الأسطر لحظيًا أثناء التنفيذ بدل انتظار الاكتمال. |
| `runSourceAsFlow(source, timeoutMs, listener)` | تشغيل عبر RinFlow (انظر [`rinflow.md`](./rinflow.md)) — رسم بياني + إلغاء + مهلة زمنية. |
| `cancelFlow(sessionId)` / `replayFlow(previousSessionId, ...)` | إلغاء/إعادة تشغيل جلسة Flow سابقة. |
| `renderView(source, rootWidth = 390)` | يبني ويقيس شجرة `@view`/`@element`/`@loop` الجذرية، يُرجعها JSON (نفس شجرة Fabric — انظر [`indsin.md`](../indsin.md)). |
| `renderContainerView(source, containerName, rootWidth = 390)` | نفسها، لكن الجذر مُحدَّد صراحةً باسم حاوية معيَّنة بدل أول جذر يُكتشَف تلقائيًا. |

طبقة أعمق (جلسة تفاعلية حيّة، لمعاينة قابلة للنقر فعليًا داخل المحرِّر) تُدار عبر
مقبض جلسة (`Long` handle، `indsinSessionCreateNative`/`...ForContainerNative`)
ثم: `indsinSessionRenderJsonNative(handle)` لإعادة الرسم، و`...TapNative`/
`...LongPressNative`/`...DoubleTapNative`/`...HoverNative` لإرسال تفاعل حقيقي
إلى `dispatchTap`/ما يعادله (انظر Needle في محرّك indsin)، و`...TickNative` لبثّ
تحديثات دورية (Spinner متحرّك، أنيميشن...)، و`...UpdateSourceNative` لإعادة بناء
الجلسة بعد تعديل حي للكود (Hot Reload، انظر Shuttle في [`indsin.md`](../indsin.md))،
و`...SetViewportNative`/`...FreeNative` للتحكّم بعرض الفيوبورت وتحرير الجلسة.

## `IndsinFabricView.kt` — العارض الفعلي على الشاشة
**لا** يستهلك مخرجات محرّك الرسم C++ (Dye/`DrawList`، انظر
[`indsin.md`](../indsin.md)) مباشرة — بدل ذلك يقرأ شجرة Fabric JSON نفسها
(نفس ما تُرجعه `renderView`/`indsinSessionRenderJsonNative` أعلاه) ويرسمها بمنطق
`Canvas` أصلي مستقل خاص به، بـ`when(kind)` واحدة لكل `StrandKind` (أُضيفت حالات
رسم صريحة لكل مكوّن جديد في توسعة UI/UX — Tag/Kbd/Rating/... — بدل تركها تسقط في
الصندوق الافتراضي العام). هذا يعني أن أي `StrandKind` جديد يُضاف للمحرّك يحتاج
حالة رسم منفصلة هنا أيضًا ليظهر بشكله الصحيح فعليًا على جهاز أندرويد حقيقي — وجود
دعمه في C++ وحده لا يكفي.

### مثال حقيقي: إصلاح حدود المعاينة (RTL + قصّ الحواف)
مثال ملموس على تفصيل خاص بأندرويد لا علاقة له بمنطق المحرّك نفسه: `Canvas` يرسم
أي عنصر تتجاوز حدوده حدود الجهاز الجذر (Row لم ينكمش، ظل على آخر عنصر...) ممتدًا
بصريًا خارج إطار الهاتف داخل chrome المعاينة نفسه ما لم يُقصّ صراحةً
(`canvas.clipRect(0f, 0f, rootWidthPx, rootHeightPx)`)، وموضع التمرير الافتراضي في
بيئة عربية (RTL) بلا `android:layoutDirection="ltr"` صريح على سطح التمرير يبدأ من
اليمين رغم أن إحداثيات Fabric تُحسَب دائمًا من الزاوية العلوية اليسرى — كلاهما
إصلاح مطبَّق فعليًا في `IndsinFabricView.kt`/`activity_indsin_preview.xml`.

## انظر أيضًا
- [`indsin.md`](../indsin.md) — محرّك التنفيذ نفسه (مستقل عن أي مضيف معيَّن).
- [`rinflow.md`](./rinflow.md) — تفاصيل `runSourceAsFlow`/`cancelFlow`/`replayFlow`.
- [`api.md`](./api.md) — واجهة C العامة (`rin_indsin_c_api.h`) التي يُبنى فوقها كل جسر JNI هذا.

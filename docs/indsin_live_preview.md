# معاينة indsin الحية — لكل المنصات

معاينة حيّة **حقيقية**: كل إطار يخرج من محرّك indsin الأصلي نفسه (`rin_indsin_session_*`: lexer ← parser ← Warp ← Fabric ← Indsin ← Dye)
الذي يستدعيه تطبيق أندرويد عبر JNI — لا محاكاة ولا إعادة تنفيذ للتخطيط. أي جهاز بمتصفح حديث هو مضيف:

| المنصة | كيف تفتح المعاينة |
|---|---|
| Windows / macOS / Linux | `rin_indsin_live app.indsin` ← يفتح المتصفح تلقائياً |
| VS Code (كل الأنظمة) | الأمر **Indsin: Open Live Preview to the Side** (`Ctrl+K V`) — تُحدَّث أثناء الكتابة قبل الحفظ |
| هاتف / لوحي (iOS, Android, أي متصفح) | `rin_indsin_live app.indsin --lan` ثم افتح عنوان `network` المطبوع على الجهاز نفسه |
| أندرويد (داخل التطبيق) | اللوحة المدمجة `IndsinLivePanel` (موجودة) |
| CI / لقطات | `GET /shot.png` أو `rin_indsin_preview file --png out.png` |

## البناء
```
tools/indsin-live/build.sh          # Linux / macOS؛ على Windows من صدفة MSYS2 UCRT64
scripts/build_all.sh live           # نفس الشيء ضمن نظام البناء الموحّد → dist/live/
cmake -S tools/indsin-live -B build-live && cmake --build build-live -j
```
أول بناء يترجم المحرّك كله (دقائق)، والتالي يعيد الربط فقط. المتطلبات: g++/clang++ بدعم C++23 وzlib. (الواجهة المضمَّنة جاهزة في `live_client.h`؛
بعد تعديل `client/index.html` شغّل `python3 tools/indsin-live/gen_client_h.py`.)

## الاستخدام
```
rin_indsin_live <ملف.indsin|ملف.rin|مجلد> [--port N] [--lan] [--width N] [--no-open] [--json]
```
يطبع عنواناً فيه مفتاح عشوائي: `http://127.0.0.1:5757/?k=…`. حفظ الملف (أو أي ملف مشروع مستورد) يُحدّث المعاينة فوراً **مع بقاء حالة Warp**
(عدّاد ضُغط عليه يبقى)، وخطأ التحليل يُظهر شريطاً بالسطر ومقتطف الكود مع إبقاء آخر إطار سليم.

## ما تدعمه الواجهة
- لمس/نقر حقيقي، ضغط مطوّل، نقر مزدوج، hover، حقول `Input`/`TextArea` المربوطة بـ warp، ساعة `time` الحيّة، التأثيرات (`animating`)، `Dialog`/`Tooltip`.
- تنقّل `onTap="navigate:page.rin"` و`href` وزر رجوع، و`navigate=`/`duration=` للشاشات الافتتاحية؛ `open:` للروابط الخارجية.
- `Image`/`Video`/`Audio`/`WebView` تُعرض فعلياً (ملفات المشروع تُخدَّم من `/f/` داخل جذر المشروع فقط؛ WebView داخل iframe معزول).
- `pickMedia` يفتح منتقي ملفات المتصفح (المسار فقط، بلا رفع). `uploadMedia` يُحاكى بإكمال فوري.
- النص تضعه المتصفحات: العربية والـRTL والإيموجي صحيحة (على عكس الرسّام النقطي الأصلي ASCII).
- أجهزة (هاتف/لوحي/لابتوب/مكتب)، تدوير، تكبير، «تلقائي» = عرض النافذة الفعلي (مثالي على الهاتف الحقيقي)، شبكة 8px، لون خلفية، فاحص عناصر،
  تقرير a11y/layout، هيكل الشجرة، إحصاءات، لقطة PNG، وضع التركيز، واجهة عربية/إنجليزية وسمة فاتحة/داكنة. اختصارات: `R` `G` `I` `B` `+` `-`.

## الأمان
- يستمع على `127.0.0.1` فقط، ومع `--lan` على الشبكة المحلية؛ كل طلب يحتاج المفتاح العشوائي (ترويسة `X-Indsin-Key` أو `?k=`).
- التحقق من ترويسة `Host` (يمنع DNS-rebinding)، وCSP على الصفحة، ولا يُخدَّم إلا الوسائط من داخل جذر المشروع (لا `..`، لا روابط تخرج منه).
- `--lan` يكشف المعاينة لمن يملك الرابط على شبكتك؛ لا تستعمله على شبكة غير موثوقة.

## الواجهة البرمجية (للمحرّرات الأخرى)
`POST /api` بجسم JSON: `source` (دفع نص المحرّر بلا حفظ: `{"cmd":"source","text":"…","page":"x.rin"}`)، `open`/`tap`/`long_press`/`double_tap`/
`hover`/`input`/`tick`/`resize`/`bg`/`reload`/`navigate`/`back`/`inspect`. البثّ الحي: `GET /events?cid=…` (SSE).
الاختبار: `node tools/indsin-live/test_live.js dist/live/rin_indsin_live` (23 فحصاً).

## حدود معروفة
- لم يُختبر البناء فعلياً على Windows/macOS (ترجمة الشيفرة تحققت بـ gcc وclang وMinGW فقط)؛ اللقطة `/shot.png` على Windows تحتاج مسار temp بحروف ASCII.
- الرسّام الأصلي (PNG) يرسم العربية كأشرطة؛ للنص الدقيق استعمل المعاينة الحية أو تصدير HTML.

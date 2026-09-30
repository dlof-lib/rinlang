package com.dlof.rinlang.store

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.text.Layout
import android.text.Spanned
import android.text.TextPaint
import android.text.method.LinkMovementMethod
import android.text.style.BackgroundColorSpan
import android.text.style.BulletSpan
import android.text.style.CharacterStyle
import android.text.style.ClickableSpan
import android.text.style.ForegroundColorSpan
import android.text.style.ImageSpan
import android.text.style.LeadingMarginSpan
import android.text.style.LineBackgroundSpan
import android.text.style.RelativeSizeSpan
import android.text.style.ReplacementSpan
import android.text.style.StrikethroughSpan
import android.text.style.SubscriptSpan
import android.text.style.SuperscriptSpan
import android.text.style.UnderlineSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import android.view.Gravity
import android.view.View
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TableLayout
import android.widget.TableRow
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors

/**
 * محوّل Markdown → معاينة حقيقية داخل TextView واحد، عبر بناء [SpannableStringBuilder] مباشرة
 * بدل تمرير وسوم HTML عبر android.text.Html.fromHtml (دعمها محدود وغير متّسق لكثير من هذه
 * العناصر داخل TextView عادي). بلا أي مكتبة Markdown خارجية — كل عنصر أدناه "مكتبة مصغّرة" ذاتية
 * الاكتفاء داخل هذا الملف الواحد، مبنيّة فوق أدوات Spannable القياسية + Span مخصّصة عند الحاجة.
 *
 * **المدعوم حالياً:**
 * - عناوين متدرّجة الحجم كاملة من `#` إلى `######` (العنوانان الخامس والسادس بلون مكتوم مميَّز).
 * - نصوص خطوط: **تشديد**، *مائل*، ***تشديد+مائل***، `__تشديد__`/`_مائل_` (الصيغة البديلة)،
 *   ~~يتوسّطه خط~~، ==تمييز بخلفية==، و`كود مضمَّن`.
 * - روابط `[نص](رابط)` قابلة للنقر فعلياً (لون مميَّز + خط تحته) عبر [applyTo].
 * - إفلات أحرف Markdown بمعكوس مائل: `\*` `\_` `\`` ... تُعرَض حرفياً بلا تنسيق.
 * - كتل كود ```لغة بخط أحادي المسافة، خلفية مميّزة تمتد بعرض السطر، ووسم صغير لاسم اللغة إن
 *   ذُكرت (بنفس أسلوب "ستيكر Rin" الجديد أدناه).
 * - اقتباسات `>` بشريط جانبي ملوَّن حقيقي يُرسم عبر [QuoteBarSpan]، لا مجرّد مسافة بادئة.
 * - قوائم نقطية حقيقية (`-`/`*`/`+`) بنقطة ملوَّنة فعلية، تدعم التعشيش بعمق حتى 3 مستويات
 *   (لون مختلف لكل عمق يدور بين هوية بنفسجي/أخضر/ذهبي الخاصة بالتطبيق).
 * - قوائم مرقَّمة `1.` بأرقام حقيقية ومحاذاة معلَّقة صحيحة للأسطر الملتفّة.
 * - قوائم مهام `- [ ]` / `- [x]` بمربّع اختيار حقيقي، وشطب تلقائي للعناصر المُنجَزة.
 * - خط أفقي فاصل (`---`/`***`/`___`) يُرسم كخط حقيقي عبر عرض السطر لا كسلسلة شرطات نصّية.
 * - جداول Markdown كاملة (`| ... | ... |` + سطر فاصل `---`) تُرسَم كصندوق حقيقي بخطوط اتصال.
 * - **جديد: "ستيكر Rin"** — بادج/شارة ملوَّنة حقيقية الشكل (خلفية بزوايا مدوَّرة) عبر صياغة
 *   `[[نص]]` أو `[[نص|لون]]`، بالألوان: accent (افتراضي)، green، gold، danger، info، like —
 *   نفس هوية التطبيق البصرية (بنفسجي+أخضر) المستخدمة في شارات "موثّق" وبطاقات الإحصاءات.
 * - **جديد: مظهر احترافي بأسلوب README على GitHub** — العنوان الأول `#` يظهر كـ"عنوان بطولي"
 *   أكبر حجماً مع خط تحته بلون هوية التطبيق (accent)، والعنوان الثاني `##` يحصل على خط أنحف
 *   بلون محايد، لفصل بصري واضح بين الأقسام تماماً كمعاينات README الاحترافية.
 * - **جديد: بطاقات بزوايا مدوَّرة حقيقية** — كتل الكود، الاقتباسات، والجداول لم تعد تُرسم كمستطيل
 *   خام بحواف حادة، بل كبطاقة مدوَّرة الزوايا فعلياً عبر [RoundedCardSpan] (يُرسم بـ[Path] لا
 *   [RectF] مباشرة)، بحدّ خفيف حول البطاقة، فتبدو أقرب لمكوّن Material Design من نص خام.
 * - **جديد: اقتباسات بعلامة تنصيص** — كل كتلة `>` تبدأ الآن بعلامة اقتباس مزخرفة كبيرة بلون
 *   الشريط الجانبي، لتمييزها بصرياً كـ"مقتطف مُقتبَس" لا مجرّد سطر مائل.
 * - **جديد: كتل كود Rin/indsin حيّة** — [splitLiveCodeBlocks] يفصل ```rin ```/```indsin ``` عن
 *   بقية النص Markdown العادي، ليعرضها المستدعي (PackageDetailActivity) ببطاقة معاينة حيّة
 *   حقيقية (وجهة مُصيَّرة، أو نتيجة تنفيذ فعلية لغير ذلك) بدل نص كود ثابت.
 * - **جديد: تلوين نحوي حقيقي (Syntax Highlighting)** — كل كتلة كود ```لغة ``` تُحلَّل الآن عبر
 *   [appendHighlightedCode] إلى رموز مصنَّفة (تعليقات/نصوص حرفية/أرقام/كلمات مفتاحية/استدعاءات
 *   دوال/أسماء مُسنَدة/توجيهات @/وسوم .end) بنفس لوحة ألوان محرِّر Rin الحقيقي (Night theme)،
 *   بدل كتلة رمادية موحَّدة اللون — لتقارب تجربة قراءة كتل الكود من محرِّرات الأكواد الاحترافية.
 * - **جديد: رأس بطاقة كود احترافي** — كل كتلة كود تُعرَض الآن داخل بطاقة موحَّدة برأس علوي:
 *   نقطة ملوَّنة بهوية اللغة + اسمها، ثم زر "نسخ" حقيقي قابل للنقر (عبر [CopyCodeSpan]) ينسخ
 *   الكود الخام كاملاً للحافظة (Clipboard) مع تنبيه تأكيد قصير، وخط فاصل رفيع أسفل الرأس يفصله
 *   بصرياً عن جسم الكود — تماماً كرأس نافذة كود في IDE احترافي، لا مجرد وسم عائم أعلى الكتلة.
 * - **جديد: شكل روابط محسَّن** — روابط `[نص](رابط)` بلا خط تحتها بعد الآن، بتشديد كامل وسهم
 *   رابط خارجي صغير ↗ بعد النص، أقرب لأسلوب شارات الروابط الاحترافية.
 * - **جديد: شارات/أزرار وصفية عبر `[* ... *]`** — صياغة عامة جديدة بأجزاء مفصولة بـ`/` (انظر
 *   [appendMetaBadge]): `[*الإصدار/1*]` لشارة إصدار ثنائية اللون (تسمية خافتة + قيمة بارزة)،
 *   `[*زر تثبيت/link=(رابط)*]` لزر رابط حقيقي قابل للنقر يفتح المتصفح عند الضغط،
 *   `[*Pin/link=(رابط)/icon=(اسم)*]` لنفس الزر مع أيقونة مصغَّرة قبل النص، و`[*#7C5CFF*]` (أو
 *   `[*Accent/#7C5CFF*]`، أو `color=(#hex)` مدمَجاً مع أي من الصيغتين أعلاه) لنقطة لون حقيقية
 *   مدمَجة داخل الحبّة نفسها عبر [ColorSwatchSpan] — لا عنصر منفصل قائم بذاته.
 * - **جديد: خلفية صفحة مخصَّصة (صلبة أو متدرّجة)** — سطر مستقل بصياغة `[*Page_background/...*]`
 *   لا يُعرَض كمحتوى مرئي إطلاقاً؛ يُستخرَج عبر [extractPageBackground] ليُطبَّق خلفيةً حقيقية
 *   للحاوية المستدعية (مثال: `containerReadme` في PackageDetailActivity)، بثلاث صيغ:
 *   `#7C5CFF` (لون صلب، كما كان)، أو `#346739,#79AE6F,#9FCB98,#F2EDC2` (تدرّج حقيقي من الأعلى
 *   للأسفل بأي عدد ألوان)، أو اسم إحدى [NAMED_PALETTES] الجاهزة (`forest`/`harbor`/`lagoon`)
 *   لتدرّج بأربع درجات دون كتابة الرموز يدوياً.
 * - **جديد: لوحات تدرّج مُسمّاة لطبقات وسام الهرم الهيكلي** — `hierarchy=(N)` يقبل الآن
 *   `palette=(forest|harbor|lagoon)` إضافياً ليرسم كل طبقة بدرجة حقيقية من نفس اللوحة (بدل تكرار
 *   ثلاث ألوان الهوية الافتراضية فقط) — نفس [NAMED_PALETTES] المستخدَمة لخلفية الصفحة، فيمكن
 *   لصفحة واحدة أن تبقى متّسقة بصرياً إن اختارت نفس اللوحة للاثنين.
 * - **جديد: شارتا التنزيل** — ضمن نفس صياغة `[* ... *]`: `[*تنزيلات/downloads=(15420)*]`
 *   لشارة عدد تنزيلات ثابتة (رقم مختصَر تلقائياً K/M/B عبر [formatCompactCount])، و
 *   `[*تحميل/downloading=(true)*]` (أو `state=(downloading)`) لشارة "جاري التحميل" مؤقّتة.
 * - **جديد: قسم وصف قابل للطي** — كتلة جديدة `[~عنوان]` ... `[~/]`: رأس قابل للنقر (عبر
 *   [ToggleSectionSpan]) يعرض △ عند الإغلاق (اضغط للفتح) و▽ عند الفتح (اضغط للإغلاق)، ومحتوى
 *   القسم لا يُعرَض إلا في الحالة المفتوحة، محفوظة في `expandedSections`.
 * - **جديد: شارات shields.io حقيقية** — `![نص](https://img.shields.io/badge/LABEL-MESSAGE-COLOR)`
 *   تُرسَم محلياً عبر [ShieldsBadgeSpan] بنفس شكل شارات shields.io المألوفة (جزء تسمية رمادي
 *   داكن ملاصِق لجزء رسالة ملوَّن)، بلا أي طلب شبكة لجلب صورة حقيقية — يدعم الألوان المُسمّاة
 *   الشائعة (brightgreen/red/blue/orange...) والسداسية العشرية، وصياغتَي `MESSAGE-COLOR` (بلا
 *   تسمية) و`LABEL-MESSAGE-COLOR` كلتيهما؛ رابط لا يطابق shields.io يسقط بهدوء لزر رابط عادي.
 * - **جديد: صور Markdown حقيقية `![نص](مسار/رابط)`** — أول فجوة حقيقية عن CommonMark كانت هذه
 *   الصياغة (لغير شارات shields.io) تسقط بصمت إلى رابط نصّي عادي بلا أي صورة معروضة فعلياً. الآن:
 *   مسار محلي (مطلق، أو `file://`، أو نسبي لمعامل `baseDir` الجديد في [toSpannable]/[applyTo])
 *   يُفكّ ويُدرَج فوراً كـ[ImageSpan] حقيقي (انظر [appendImage]/[decodeLocalImage])، ورابط بعيد
 *   http(s) يُحمَّل غير متزامن عبر [loadRemoteImageAsync] ويستبدل عنصره النائب المؤقت بصورة حقيقية
 *   فور الاكتمال (انظر [applyTo]/[loadPendingRemoteImages]) — بذاكرة تخزين مؤقت تتفادى إعادة
 *   التنزيل عند كل إعادة بناء لنفس الشاشة. أي مسار آخر غير قابل للحلّ يسقط كما كان لزر رابط عادي.
 * - **جديد: تنبيهات GitHub Flavored Markdown** — `> [!NOTE]`/`[!TIP]`/`[!IMPORTANT]`/`[!WARNING]`/
 *   `[!CAUTION]` كأول سطر داخل اقتباس `>` يحوِّله لبطاقة تنبيه ملوَّنة بأيقونة مميِّزة لكل نوع (انظر
 *   [appendCallout])، بدل بقائه اقتباساً محايداً — نفس الصياغة الشائعة في READMEs حديثة على GitHub.
 *
 * - **جديد: ثيم داكن حقيقي** — كل الألوان تتبدّل تلقائياً بين قيم الثيم الفاتح والداكن للتطبيق (انظر
 *   [applyPalette])، بعد أن كانت العناوين شبه سوداء فوق الخلفية الداكنة وبطاقات الكود ساطعة.
 * - **جديد: جداول حقيقية** عبر [splitReadmeSegments] + [buildTableView] (التفاف بدل القصّ، تمرير
 *   أفقي، محاذاة أعمدة `:---:`، تنسيق داخل الخلايا، `\|` حرفية) — مع تحسين الصندوق النصّي الاحتياطي.
 * - **جديد: إصلاحات تحليل** — `snake_case_name` لم تعد تُنتج مائلاً، و`2 * 3 * 4` تبقى حرفية،
 *   وتعشيش القوائم صار بمكدّس مسافات (2 أو 4 أو تاب حتى 5 مستويات)، وإغلاق العناوين `## عنوان ##`.
 * - **جديد: عناصر إضافية** — روابط تلقائية `<https://...>`، فاصل `<br>`، ورموز `:rocket:` `:tada:`...
 * - **إصلاح: روابط `[نص](رابط)` صارت ClickableSpan** بدل URLSpan لأن autoLinkMask في TextView كان
 *   يحذف كل URLSpan فتفقد الروابط النقر.
 *
 * - **جديد: HTML شائع في READMEs** — `<b> <i> <u> <s> <code> <kbd> <mark> <sub> <sup>`، `<a href>`،
 *   `<img src alt>` (وشارات shields تُرسَم محلياً)، `<details><summary>` كقسم قابل للطي، وإسقاط
 *   الوسوم الغلافية (`<p align>` `<div>` `<center>`...) بدل ظهورها كنص خام.
 * - **جديد: شريط Marquee متحرّك** عبر `[*نص/marquee*]` (يُرسَم بـ[MarqueeSpan] ويحرّكه [MarqueeTicker]).
 * - **جديد: روابط مرجعية** `[نص][id]` + `[id]: https://...` (تعريفاتها لا تُعرَض).
 *
 * الاستخدام المباشر: `textView.text = DocumentationContainer.toSpannable(md)`.
 * الاستخدام الموصى به عند وجود روابط قابلة للنقر (أو أقسام قابلة للطي/خلفية صفحة/صور محلية):
 * `DocumentationContainer.applyTo(textView, md, pageContainer, baseDir = ...)`.
 */
object DocumentationContainer {

    /**
     * الامتداد الخاص بملفات "Documentation Container" — مستند بنفس صياغة Markdown الموسَّعة التي يعالجها
     * هذا الكائن (جداول، خريطة شجرة، معاينة حيّة، خلفية صفحة...). يُقبل كملف README للحزمة
     * (README.rdoc) بجانب README.md، وله أيقونة خاصة (ic_doc_container_file).
     */
    const val EXTENSION = "rdoc"

    /** هل [fileName] ملف Documentation Container (امتداده .rdoc)؟ */
    fun isDocumentationFile(fileName: String): Boolean =
        fileName.substringAfterLast('/').substringAfterLast('.', "").equals(EXTENSION, ignoreCase = true)

    /** هل [fileName] (في جذر الحزمة) هو ملف README — بصيغة README.rdoc أو README.md؟ */
    fun isReadmeName(fileName: String): Boolean =
        fileName.equals("README.$EXTENSION", ignoreCase = true) || fileName.equals("README.md", ignoreCase = true)

    /** مجلد الأساس الحالي لحلّ مسارات صور محلية نسبية (انظر معامل baseDir في [toSpannable]) —
     *  متغيّر على مستوى الكائن (Object) لا معامل مُمرَّر عبر عشرات استدعاءات [appendInline]
     *  المتداخلة (تشديد داخل قائمة داخل اقتباس...)؛ يُضبَط مرة واحدة في بداية [toSpannable] فقط.
     *  آمن هنا لأن كل استدعاء لـ[toSpannable]/[applyTo] في هذا التطبيق يحدث من UI thread الرئيسي
     *  بشكل متزامن (لا تعشيش/تزامن فعلي)، تماماً كبقية حالة "أثناء التحليل" الأخرى في هذا الملف. */
    private var currentBaseDir: String? = null

    /** ذاكرة تخزين مؤقت بسيطة (بلا حدّ أقصى/انتهاء صلاحية — كافية لعمر شاشة واحدة) للصور البعيدة
     *  المحمَّلة فعلاً، لتفادي إعادة تنزيل نفس الرابط عند كل إعادة بناء (مثال: كل نقرة على قسم قابل
     *  للطي تُعيد بناء النص كاملاً عبر [applyTo] → [toSpannable] من جديد). */
    private val remoteImageCache = ConcurrentHashMap<String, Bitmap>()
    private val remoteImageExecutor = Executors.newFixedThreadPool(2)
    private val mainHandler = Handler(Looper.getMainLooper())

    // ألوان مأخوذة من نفس لوحة التطبيق (colors.xml) لتبدو المعاينة جزءاً من التطبيق لا دخيلة عليه
    private const val COLOR_BULLET = 0xFF7C5CFF.toInt()          // rin_accent
    private const val COLOR_BULLET_L2 = 0xFF22C88E.toInt()       // rin_accent_green
    private const val COLOR_BULLET_L3 = 0xFFFFC94D.toInt()       // rin_star_gold
    private val BULLET_DEPTH_COLORS = intArrayOf(COLOR_BULLET, COLOR_BULLET_L2, COLOR_BULLET_L3)

    /**
     * **جديد: لوحات تدرّج مسمّاة (Palettes)** — كل لوحة 4 درجات (غامق ← فاتح/لهجة) تُستخدَم في
     * مكانين معاً حتى تبقى هوية "الخلفية" و"طبقات الوسام" متّسقة إن اختِيرت نفس اللوحة للاثنين:
     * 1. خلفية صفحة متدرّجة حقيقية عبر `[*Page_background/اسم اللوحة*]` (انظر [extractPageBackground]).
     * 2. ألوان طبقات وسام الهرم الهيكلي عبر `[*تسمية/hierarchy=(N)/palette=(اسم اللوحة)*]`
     *    (انظر [pyramidTierColor] و[appendMetaBadge]) — بديل عن اللوحة الافتراضية الثلاثية
     *    [BULLET_DEPTH_COLORS] حين يريد المستخدم أكثر من 3 درجات مميَّزة فعلاً بدل تكرارها.
     * الاسم غير حسّاس لحالة الأحرف؛ لوحة غير معروفة تُتجاهَل بصمت (يبقى السلوك الافتراضي القديم).
     */
    private val NAMED_PALETTES: Map<String, IntArray> = mapOf(
        // غابة: أخضر غامق → أخضر متوسط → نعناعي فاتح → كريمي (دافئ/طبيعي)
        "forest" to intArrayOf(0xFF346739.toInt(), 0xFF79AE6F.toInt(), 0xFF9FCB98.toInt(), 0xFFF2EDC2.toInt()),
        // ميناء: كحلي غامق → أزرق متوسط → أزرق رمادي فاتح → برتقالي (لهجة تباين حادة)
        "harbor" to intArrayOf(0xFF253C6D.toInt(), 0xFF30497D.toInt(), 0xFF455B8A.toInt(), 0xFFF2842F.toInt()),
        // بحيرة: أخضر مزرق غامق → تركوازي متوسط → فيروزي فاتح → كهرماني (لهجة تباين حادة)
        "lagoon" to intArrayOf(0xFF224248.toInt(), 0xFF325E6A.toInt(), 0xFF44A1A4.toInt(), 0xFFFF9A00.toInt())
    )

    /** لون طبقة رقم [index] داخل وسام "الهرم الهيكلي" ([PyramidBadgeSpan])، من [palette] المُعطاة
     *  (افتراضياً نفس تدرّج ألوان أعماق القوائم المتعشِّشة [BULLET_DEPTH_COLORS] كما كان)، فتبقى
     *  هوية "العمق البصري" موحَّدة عبر كل عناصر الملف عندما لا تُطلب لوحة صريحة، مع إمكانية اختيار
     *  إحدى [NAMED_PALETTES] (4 درجات حقيقية بدل تكرار 3) عبر `palette=(...)`. */
    private fun pyramidTierColor(index: Int, palette: IntArray = BULLET_DEPTH_COLORS): Int =
        palette[index % palette.size]

    // ملاحظة مهمّة: كل الألوان أدناه كانت منقولة حرفياً من values-night/colors.xml (الثيم الداكن)
    // رغم أنّ ثيم التطبيق الافتراضي الفعلي هو الفاتح (values/colors.xml: rin_background فاتح،
    // rin_editor_text غامق) — ما كان يجعل بطاقات الكود/الاقتباس/الجدول شبه شفّافة (أبيض فوق أبيض)
    // ونص الكود شبه غير مرئي (رمادي فاتح جداً فوق خلفية فاتحة). أُعيد ضبط كل قيمة هنا لتُقرأ
    // بوضوح فوق الثيم الفاتح تحديداً، مع إعادة استخدام نفس رموز التطبيق (syntax_*، rin_editor_text،
    // rin_divider، rin_accent_pressed...) حتى تبقى المعاينة متّسقة بصرياً مع بقية الشاشات، ونسبة
    // تباين حقيقية (WCAG AA تقريباً) بدل الاعتماد على الشفافية فوق خلفية داكنة لم تعد موجودة.
    private var COLOR_RULE: Int = 0xFFDDE2E8.toInt()             // rin_divider — فاصل هادئ تحت H2
    private var COLOR_CARD_BORDER: Int = 0xFFE2E6ED.toInt()      // حدّ واضح موحّد لبطاقات الكود/الاقتباس/الجدول
    private var COLOR_CODE_TEXT: Int = 0xFF20252B.toInt()        // rin_editor_text (فاتح)
    private var COLOR_HEADING: Int = 0xFF12151A.toInt()          // أسود "حبري" أعمق من نص الجسم — تدرّج هرمي أوضح للعناوين
    private var COLOR_CODE_BLOCK_BG: Int = 0xFFF4F2FB.toInt()    // بطاقة فاتحة بلمسة بنفسجية خفيفة
    private var COLOR_INLINE_CODE_BG: Int = 0xFFEAE6F7.toInt()   // أغمق قليلاً لتمييز الكود المضمَّن عن السطر

    private const val COLOR_QUOTE_BAR = 0xFF7C5CFF.toInt()        // rin_accent
    private var COLOR_QUOTE_BG: Int = 0x147C5CFF                 // rin_current_line_bg (فاتح) — بنفسجي 8%
    private var COLOR_QUOTE_TEXT: Int = 0xFF54586B.toInt()       // rin_segment_unselected_text

    private var COLOR_LINK: Int = 0xFF6A47E8.toInt()             // rin_accent_pressed — بنفسجي أعمق، تباين كافٍ
    private const val COLOR_HIGHLIGHT_BG = 0x4DFFC94D              // rin_star_gold_dim (يعمل فوق أي خلفية)
    private var COLOR_STRIKE_TEXT: Int = 0xFF667085.toInt()      // rin_editor_hint (فاتح)

    private const val COLOR_TASK_DONE = 0xFF1CA877.toInt()        // rin_accent_green_pressed (أغمق، تباين أفضل)
    private var COLOR_TASK_PENDING: Int = 0xFF667085.toInt()     // rin_editor_hint (فاتح)
    private var COLOR_H_DIM: Int = 0xFF54586B.toInt()            // عناوين H5/H6 خافتة لكن مقروءة بوضوح

    private var COLOR_TABLE_TEXT: Int = 0xFF20252B.toInt()       // rin_editor_text (فاتح)
    private var COLOR_TABLE_HEADER: Int = 0xFF6A47E8.toInt()     // rin_accent_pressed
    private var COLOR_TABLE_BORDER: Int = 0xFFDDE2E8.toInt()     // rin_divider
    private var COLOR_TABLE_BG: Int = 0xFFF6F5FB.toInt()         // أخفّ قليلاً من خلفية كتلة الكود
    private var COLOR_TABLE_ROW_ALT: Int = 0xFFEDEAF7.toInt()    // تظليل تناوبي (Zebra) خفيف لصفوف البيانات الزوجية

    // لوحة تلوين نحوي (Syntax Palette) — نفس اللوحة الفعلية المستخدَمة في محرِّر Rin على الثيم
    // الفاتح (values/colors.xml: syntax_keyword/syntax_string/...)، مصمَّمة أصلاً لتحقّق تباين
    // ≥4.5:1 فوق خلفية بيضاء (انظر تعليق اللوحة هناك) — لا لوحة الثيم الداكن الباهتة السابقة.
    private var COLOR_SYNTAX_KEYWORD: Int = 0xFF0B4FCC.toInt()    // syntax_keyword
    private var COLOR_SYNTAX_DIRECTIVE: Int = 0xFF6A1B9A.toInt()  // syntax_container_keyword / syntax_make_directive
    private var COLOR_SYNTAX_STRING: Int = 0xFF1D7A4C.toInt()     // syntax_string
    private var COLOR_SYNTAX_NUMBER: Int = 0xFFB45F06.toInt()     // syntax_number
    private var COLOR_SYNTAX_COMMENT: Int = 0xFF6B7280.toInt()    // syntax_comment
    private var COLOR_SYNTAX_BUILTIN: Int = 0xFF8A6D00.toInt()    // syntax_builtin / syntax_tag
    private var COLOR_SYNTAX_ATTR: Int = 0xFF9C5700.toInt()       // syntax_style_keyword

    // خلفية زر "نسخ" وخط الفصل الرفيع أسفل رأس بطاقة الكود — بنفسجي فاتح جداً/حدّ فاتح واضحان
    // فوق البطاقة الفاتحة، بدل تراكب أبيض شبه شفاف كان يختفي تماماً فوق خلفية بيضاء.
    private var COLOR_COPY_BUTTON_BG: Int = 0x1F7C5CFF
    private var COLOR_HEADER_RULE: Int = 0xFFE2E6ED.toInt()
    // شريط رأس بطاقة الكود (أغمق قليلاً من جسم البطاقة) + لون أرقام الأسطر الخافت في هامش الكود.
    private var COLOR_CODE_HEADER_BG: Int = 0xFFE9E5F6.toInt()
    private var COLOR_CODE_GUTTER: Int = 0xFF9AA0B5.toInt()
    // خريطة الشجرة (Tree Map): لون أيقونة المجلد ولون أيقونة الملف الافتراضي (لملف بلا امتداد معروف).
    private var COLOR_TREE_FOLDER: Int = 0xFFD9930D.toInt()
    private var COLOR_TREE_FILE: Int = 0xFF7A8299.toInt()

    /** إزاحة عنصر قائمة بحسب عمق التعشيش (بكسل). */
    private fun listIndentPx(depth: Int): Float = dpPx(4f) + depth * dpPx(20f)

    /** يحوّل [v] (dp) إلى بكسل بكثافة الشاشة الحالية ([currentDensity] تُضبَط في [applyTo]). */
    private fun dpPx(v: Float): Float = v * currentDensity

    /** هل الباليت الحالي داكن — يُضبَط عبر [applyPalette] (انظر [adaptForTheme]). */
    private var darkMode = false

    /**
     * **جديد: ثيم داكن حقيقي** — كل الألوان أعلاه كانت مكتوبة للثيم الفاتح فقط، بينما التطبيق يدعم
     * ثيماً داكناً فعلياً (ThemeManager + values-night، خلفية #17181C): كانت العناوين تظهر شبه سوداء
     * فوق خلفية داكنة بلا أي تباين، وبطاقات الكود/الجدول بيضاء ساطعة، والروابط بنفسجي قاتم.
     * هذه الدالة تبدّل كل ألوان المعاينة دفعة واحدة بين قيم الثيم الفاتح (values/colors.xml) وقيم
     * الثيم الداكن الحقيقية للتطبيق (values-night/colors.xml: syntax_* وrin_editor_text...). تُستدعى
     * في بداية [toSpannable] و[buildTableView]؛ آمنة لأن كل الاستدعاءات من UI thread (نفس مبدأ
     * [currentBaseDir]).
     */
    private fun applyPalette(dark: Boolean) {
        darkMode = dark
        if (dark) {
            COLOR_RULE = 0xFF2A2D34.toInt()
            COLOR_CARD_BORDER = 0xFF33363F.toInt()
            COLOR_CODE_TEXT = 0xFFE3E5E8.toInt()
            COLOR_HEADING = 0xFFF4F5F7.toInt()
            COLOR_CODE_BLOCK_BG = 0xFF1E2027.toInt()
            COLOR_INLINE_CODE_BG = 0xFF2B2840.toInt()
            COLOR_QUOTE_BG = 0x267C5CFF
            COLOR_QUOTE_TEXT = 0xFFB4B8C5.toInt()
            COLOR_LINK = 0xFF9C85FF.toInt()
            COLOR_STRIKE_TEXT = 0xFF8B92A0.toInt()
            COLOR_TASK_PENDING = 0xFF8B92A0.toInt()
            COLOR_H_DIM = 0xFF9AA0AB.toInt()
            COLOR_TABLE_TEXT = 0xFFE3E5E8.toInt()
            COLOR_TABLE_HEADER = 0xFFB39DFF.toInt()
            COLOR_TABLE_BORDER = 0xFF33363F.toInt()
            COLOR_TABLE_BG = 0xFF1C1E25.toInt()
            COLOR_TABLE_ROW_ALT = 0xFF242733.toInt()
            COLOR_SYNTAX_KEYWORD = 0xFF569CD6.toInt()
            COLOR_SYNTAX_DIRECTIVE = 0xFFC586C0.toInt()
            COLOR_SYNTAX_STRING = 0xFF6FDC9E.toInt()
            COLOR_SYNTAX_NUMBER = 0xFFFFA95C.toInt()
            COLOR_SYNTAX_COMMENT = 0xFF9AA0AB.toInt()
            COLOR_SYNTAX_BUILTIN = 0xFFE6C260.toInt()
            COLOR_SYNTAX_ATTR = 0xFFF2A65A.toInt()
            COLOR_COPY_BUTTON_BG = 0x337C5CFF
            COLOR_HEADER_RULE = 0xFF33363F.toInt()
            COLOR_CODE_HEADER_BG = 0xFF272A34.toInt()
            COLOR_CODE_GUTTER = 0xFF6B7280.toInt()
            COLOR_TREE_FOLDER = 0xFFFFC94D.toInt()
            COLOR_TREE_FILE = 0xFF9AA3B8.toInt()
        } else {
            COLOR_RULE = 0xFFDDE2E8.toInt()
            COLOR_CARD_BORDER = 0xFFE2E6ED.toInt()
            COLOR_CODE_TEXT = 0xFF20252B.toInt()
            COLOR_HEADING = 0xFF12151A.toInt()
            COLOR_CODE_BLOCK_BG = 0xFFF4F2FB.toInt()
            COLOR_INLINE_CODE_BG = 0xFFEAE6F7.toInt()
            COLOR_QUOTE_BG = 0x147C5CFF
            COLOR_QUOTE_TEXT = 0xFF54586B.toInt()
            COLOR_LINK = 0xFF6A47E8.toInt()
            COLOR_STRIKE_TEXT = 0xFF667085.toInt()
            COLOR_TASK_PENDING = 0xFF667085.toInt()
            COLOR_H_DIM = 0xFF54586B.toInt()
            COLOR_TABLE_TEXT = 0xFF20252B.toInt()
            COLOR_TABLE_HEADER = 0xFF6A47E8.toInt()
            COLOR_TABLE_BORDER = 0xFFDDE2E8.toInt()
            COLOR_TABLE_BG = 0xFFF6F5FB.toInt()
            COLOR_TABLE_ROW_ALT = 0xFFEDEAF7.toInt()
            COLOR_SYNTAX_KEYWORD = 0xFF0B4FCC.toInt()
            COLOR_SYNTAX_DIRECTIVE = 0xFF6A1B9A.toInt()
            COLOR_SYNTAX_STRING = 0xFF1D7A4C.toInt()
            COLOR_SYNTAX_NUMBER = 0xFFB45F06.toInt()
            COLOR_SYNTAX_COMMENT = 0xFF6B7280.toInt()
            COLOR_SYNTAX_BUILTIN = 0xFF8A6D00.toInt()
            COLOR_SYNTAX_ATTR = 0xFF9C5700.toInt()
            COLOR_COPY_BUTTON_BG = 0x1F7C5CFF
            COLOR_HEADER_RULE = 0xFFE2E6ED.toInt()
            COLOR_CODE_HEADER_BG = 0xFFE9E5F6.toInt()
            COLOR_CODE_GUTTER = 0xFF9AA0B5.toInt()
            COLOR_TREE_FOLDER = 0xFFD9930D.toInt()
            COLOR_TREE_FILE = 0xFF7A8299.toInt()
        }
    }

    /** يفتّح [color] بمزجه ~45% نحو الأبيض في الثيم الداكن (ألوان هوية اللغات وأنواع التنبيهات
     *  مكتوبة أصلاً لتُقرأ فوق خلفية فاتحة)؛ في الثيم الفاتح يعيده كما هو. */
    private fun adaptForTheme(color: Int): Int {
        if (!darkMode) return color
        fun lift(c: Int): Int = (c * 0.55 + 255 * 0.45).toInt().coerceIn(0, 255)
        val r = lift((color shr 16) and 0xFF)
        val g = lift((color shr 8) and 0xFF)
        val b = lift(color and 0xFF)
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }

    /** هل يعمل [context] الآن بالثيم الداكن؟ يحترم اختيار المستخدم في ThemeManager لأن
     *  AppCompatDelegate ينعكس على uiMode لسياق الـActivity نفسه. */
    private fun isDarkMode(context: Context): Boolean =
        (context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) ==
            Configuration.UI_MODE_NIGHT_YES

    /** خلفية "معتّمة" (Tint) بلون [color] عند شفافية [alphaHex] (افتراضياً ~12%) — تُستخدَم لإعطاء
     *  كل نوع تنبيه GFM ([appendCallout]) خلفية فاتحة بلون هويته الخاص بدل خلفية محايدة موحَّدة
     *  للجميع، فتبدو بطاقات NOTE/TIP/WARNING... متمايزة فعلاً كما في READMEs الاحترافية. */
    private fun tintedBackground(color: Int, alphaHex: Int = 0x1E): Int =
        (alphaHex shl 24) or (color and 0x00FFFFFF)

    /** لون هوية بصرية مميَّز لكل لغة (نقطة + وسم اسمها أعلى بطاقة الكود) — يسقط بهدوء إلى لون
     *  محايد للغات غير المدرَجة، فلا يتعطّل عرض أي كتلة كود بسبب اسم لغة غير معروف. */
    private val LANGUAGE_ACCENTS: Map<String, Int> = mapOf(
        "rin" to 0xFF6A47E8.toInt(),
        "indsin" to 0xFF6A47E8.toInt(),
        "kotlin" to 0xFF7F3FBF.toInt(),
        "kt" to 0xFF7F3FBF.toInt(),
        "java" to 0xFFB35900.toInt(),
        "swift" to 0xFFC1440E.toInt(),
        "python" to 0xFF2B5FA8.toInt(),
        "py" to 0xFF2B5FA8.toInt(),
        "javascript" to 0xFF9C7A00.toInt(),
        "js" to 0xFF9C7A00.toInt(),
        "typescript" to 0xFF1D4ED8.toInt(),
        "ts" to 0xFF1D4ED8.toInt(),
        "json" to 0xFF55606F.toInt(),
        "xml" to 0xFF9C5700.toInt(),
        "html" to 0xFFB33A1C.toInt(),
        "css" to 0xFF2B4FD6.toInt(),
        "bash" to 0xFF1D7A4C.toInt(),
        "sh" to 0xFF1D7A4C.toInt(),
        "shell" to 0xFF1D7A4C.toInt(),
        "c" to 0xFF2E5AAC.toInt(),
        "cpp" to 0xFF2E5AAC.toInt(),
        "c++" to 0xFF2E5AAC.toInt(),
        "go" to 0xFF0E7C86.toInt(),
        "rust" to 0xFFB1481C.toInt(),
        "sql" to 0xFF0F766E.toInt(),
        "yaml" to 0xFFA05A12.toInt(),
        "yml" to 0xFFA05A12.toInt()
    )

    /** مجموعة موحَّدة من الكلمات المفتاحية: كلمات لغة Rin الحقيقية (من rin_lexer.cpp) + مجموعة
     *  عامة شائعة عبر أكثر اللغات ذكراً في ملفات README (Kotlin/Python/JS/TS/C/C++/Java/Swift/Go/
     *  Rust/Bash) — قائمة واحدة كافية عملياً بدل تفريع منطق كامل لكل لغة على حدة. */
    private val KEYWORDS: Set<String> = setOf(
        // Rin (rin_lexer.cpp)
        "and", "or", "break", "container", "continue", "data", "else", "end", "false", "file",
        "for", "fun", "if", "import", "installation", "let", "link", "merge", "nil", "pipe",
        "print", "return", "rinopen", "route", "save", "show", "simplified", "text", "translation",
        "true", "tying", "while",
        // شائعة عبر لغات أخرى
        "def", "class", "struct", "enum", "interface", "trait", "impl", "extends", "implements",
        "package", "namespace", "using", "include", "module", "export", "async", "await", "yield",
        "in", "is", "as", "self", "this", "super", "new", "try", "catch", "finally", "throw",
        "throws", "switch", "case", "default", "public", "private", "protected", "static", "final",
        "const", "var", "val", "function", "fn", "void", "int", "float", "double", "bool",
        "boolean", "string", "char", "null", "none", "None", "lambda", "with", "from", "elif",
        "pass", "raise", "except", "global", "typeof", "instanceof", "override", "abstract",
        "sealed", "companion", "object", "when", "do", "unsigned", "signed", "typedef", "template"
    )

    /** رموز محاطة بالخوارزمية أدناه بترتيب أولوية: تعليق كتلة، تعليق سطر، نص حرفي، توجيه @،
     *  وسم إغلاق .end/، رقم، كلمة مفتاحية، استدعاء دالة، اسم مُسنَد قبل =. */
    private val codeTokenRegex = Regex(
        "(/\\*[\\s\\S]*?\\*/)" +
            "|(//[^\n]*|#[^\n]*)" +
            "|(\"(?:\\\\.|[^\"\\\\\n])*\"|'(?:\\\\.|[^'\\\\\n])*'|`(?:\\\\.|[^`\\\\])*`)" +
            "|(@[A-Za-z_][A-Za-z0-9_.]*)" +
            "|(\\.end/[A-Za-z_]+)" +
            "|\\b(\\d+(?:\\.\\d+)?)\\b" +
            "|\\b(" + KEYWORDS.joinToString("|") + ")\\b" +
            "|\\b([A-Za-z_][A-Za-z0-9_]*)(?=\\()" +
            "|\\b([A-Za-z_][A-Za-z0-9_]*)\\b(?=\\s*=(?!=))"
    )

    /** ألوان "ستيكر Rin": كل صيغة اسم → (خلفية، نص). الافتراضي "accent" عند عدم ذكر أي اسم. */
    private val STICKER_VARIANTS: Map<String, Pair<Int, Int>> = mapOf(
        "accent" to (0xFF7C5CFF.toInt() to 0xFFFFFFFF.toInt()),   // rin_accent
        "green" to (0xFF22C88E.toInt() to 0xFF0C231A.toInt()),    // rin_accent_green
        "gold" to (0xFFFFC94D.toInt() to 0xFF2B1F02.toInt()),     // rin_star_gold
        "danger" to (0xFFF14C4C.toInt() to 0xFFFFFFFF.toInt()),   // status_error
        "info" to (0xFF3B9EFF.toInt() to 0xFFFFFFFF.toInt()),     // rin_verified_badge
        "like" to (0xFFFF4D6D.toInt() to 0xFFFFFFFF.toInt())      // rin_like_active
    )
    private val STICKER_DEFAULT = STICKER_VARIANTS.getValue("accent")

    // مجموعة تعابير نمطية للأنماط السطرية بترتيب أولوية يحلّ التعارض بين ** و * و __ و _ وغيرها.
    // ملاحظة الفهارس (مطابقة لترتيب المجموعات أدناه):
    // (تحديث) إصلاحات: `_`/`__` لا تُنشئ مائلاً/تشديداً داخل الكلمات (snake_case_name)، و`*`/`**` تتطلّبان
    // أن يلي الفاتحُ حرفاً غير مسافة وأن يسبق الخاتمَ حرفٌ غير مسافة (2 * 3 * 4 تبقى حرفية)، مطابقةً لـGFM.
    // مجموعات جديدة في الآخر (فهارس 1-17 لم تتغيّر): 18) رابط تلقائي <https://...>  19) <br>  20) :emoji:
    // مجموعات HTML/مراجع جديدة (21-35): <b>/<strong> <i>/<em> <s>/<del> <u> <code> <kbd> <mark> <sub> <sup>،
    // <a href>، <img>، وسوم غلافية تُسقَط (p/div/center/span/table...)، و`[نص][معرّف]` روابط مرجعية.
    //  1) escape حرف مُفلَت حرفياً        8) [[ستيكر]] أو [[ستيكر|لون]]
    //  2) ***تشديد+مائل***                9) [*شارة/قيمة*] أو [*نص/link=(رابط)*] أو
    //  3) **تشديد**                          [*نص/link=(رابط)/icon=(اسم)*] — انظر [appendMetaBadge]
    //  4) __تشديد بديل__                  10/11) ![نص](رابط shields.io/badge) — انظر [appendShieldsBadge]
    //  5) ~~يتوسّطه خط~~                  12/13) [نص](رابط)
    //  6) `كود مضمَّن`                    14) *مائل*
    //  7) ==تمييز==                       15) _مائل بديل_
    //                                     16/17) ![نص](أي رابط/مسار آخر) — صورة حقيقية، انظر [appendImage]
    // ملاحظة الترتيب: 16/17 مُلحَقة في نهاية السلسلة لا وسطها؛ هذا آمن رغم مجيء 12/13 (الرابط
    // العادي) قبلها لأن كلتيهما لا يمكن أن تتطابقا عند نفس موضع البداية أصلاً — الصورة تبدأ حرفياً
    // بـ"!" والرابط العادي يبدأ بـ"[" فقط، فمحرك المطابقة (الذي يجرّب كل موضع بداية بالترتيب من
    // اليسار) لا يصل إطلاقاً لتجربة بديل الرابط العادي عند موضع "!" مادام بديل الصورة يطابقه أولاً.
    private val inlineRegex = Regex(
        "\\\\([\\\\`*_{}\\[\\]()#+.!~=>-])" +
            "|\\*\\*\\*(?!\\s)([^*]+?)(?<!\\s)\\*\\*\\*" +
            "|\\*\\*(?!\\s)([^*]+?)(?<!\\s)\\*\\*" +
            "|(?<![\\p{L}\\p{N}_])__([^_]+?)__(?![\\p{L}\\p{N}_])" +
            "|~~([^~]+?)~~" +
            "|`([^`]+?)`" +
            "|==([^=]+?)==" +
            "|\\[\\[([^\\]]+?)\\]\\]" +
            "|\\[\\*([^\\]]+?)\\*\\]" +
            "|!\\[([^\\]]*?)\\]\\((https?://img\\.shields\\.io/[^)\\s]+)\\)" +
            "|\\[([^\\]]+?)\\]\\(([^)\\s]+?)\\)" +
            "|\\*(?![\\s*])([^*]+?)(?<!\\s)\\*" +
            "|(?<![\\p{L}\\p{N}_])_([^_]+?)_(?![\\p{L}\\p{N}_])" +
            "|!\\[([^\\]]*?)\\]\\(([^)\\s]+?)\\)" +
            "|<(https?://[^>\\s]+)>" +
            "|(<[bB][rR]\\s*/?>)" +
            "|(?<![\\p{L}\\p{N}:]):([a-z0-9_+-]{2,}):(?![\\p{L}\\p{N}:])" +
            "|(?i:<(?:b|strong)>(.+?)</(?:b|strong)>)" +
            "|(?i:<(?:i|em)>(.+?)</(?:i|em)>)" +
            "|(?i:<(?:s|del|strike)>(.+?)</(?:s|del|strike)>)" +
            "|(?i:<u>(.+?)</u>)" +
            "|(?i:<code>(.+?)</code>)" +
            "|(?i:<kbd>(.+?)</kbd>)" +
            "|(?i:<mark>(.+?)</mark>)" +
            "|(?i:<sub>(.+?)</sub>)" +
            "|(?i:<sup>(.+?)</sup>)" +
            "|(?i:<a\\s[^>]*?href\\s*=\\s*[\"']([^\"']+)[\"'][^>]*>(.+?)</a>)" +
            "|(?i:(<img\\b[^>]*>))" +
            "|(?i:(</?(?:p|div|center|span|section|picture|source|figure|figcaption|font|table|thead|tbody|tr|td|th|hr)\\b[^>]*>))" +
            "|\\[([^\\]]+?)\\]\\[([^\\]]*)\\]"
    )

    /** رأس كتلة اقتباس GFM: `[!NOTE]`/`[!TIP]`/`[!IMPORTANT]`/`[!WARNING]`/`[!CAUTION]` بمفردها على
     *  أول سطر داخل الاقتباس (بعد إسقاط "> ") — نفس صياغة GitHub Flavored Markdown الشائعة في
     *  READMEs، انظر [appendCallout]. */
    private val calloutMarkerRegex = Regex("^\\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)]\\s*$", RegexOption.IGNORE_CASE)

    private val orderedListRegex = Regex("^(\\d{1,4})[.)]\\s+(.*)$")
    private val taskListRegex = Regex("^[-*+]\\s+\\[([ xX])]\\s+(.*)$")
    private val bulletListRegex = Regex("^[-*+]\\s+(.*)$")
    private val tableSeparatorRegex = Regex("^:?-{2,}:?$")

    /** قيمة صالحة بعد `/`: إمّا لون سداسي مفرد (خلفية صلبة قديمة، بلا تغيير)، أو عدّة ألوان
     *  سداسية مفصولة بفواصل (تدرّج حقيقي من الأعلى للأسفل)، أو اسم إحدى [NAMED_PALETTES]
     *  (تدرّج جاهز بأربع درجات). كل هذا ضمن مجموعة أحرف واحدة تكفي للتحقّق من شكل السطر. */
    private const val PAGE_BG_VALUE = "[#0-9A-Za-z,\\s]+"

    /** سطر مستقل `[*Page_background/...*]` بالضبط (لا محتوى آخر معه بنفس السطر) — يُستهلَك
     *  بصمت دون أي عرض مرئي في [toSpannable] (انظر [appendMetaBadge] أيضاً لضمان عدم ظهوره
     *  حتى لو استُخدم داخل سطر مختلط). */
    private val pageBackgroundLineRegex =
        Regex("^\\[\\*\\s*page[_ ]background\\s*/\\s*$PAGE_BG_VALUE\\s*\\*\\]$", RegexOption.IGNORE_CASE)

    /** نفس الصياغة أعلاه لكن بلا تثبيت على السطر كاملاً — يُستخدَم فقط لاستخراج القيمة عبر
     *  [extractPageBackground]، فيعمل حتى لو وُضع السطر وسط فقرة أخرى. */
    private val pageBackgroundRegex =
        Regex("\\[\\*\\s*page[_ ]background\\s*/\\s*($PAGE_BG_VALUE)\\s*\\*\\]", RegexOption.IGNORE_CASE)

    /** خلفية صفحة مُستخرَجة: إمّا [Solid] (سلوك قديم، لون واحد) أو [Gradient] (لوحة/قائمة درجات،
     *  تُرسَم من الأعلى للأسفل). [applyTo] يبني `Drawable` مناسباً من أيّهما دون أن يعرف المستدعي
     *  الفرق. */
    sealed class PageBackground {
        data class Solid(val color: Int) : PageBackground()
        data class Gradient(val colors: IntArray) : PageBackground()
    }

    /** يستخرج خلفية الصفحة من صياغة `[*Page_background/...*]` إن وُجدت في [markdown]، أو null إن
     *  لم توجد:
     *  - `[*Page_background/#7C5CFF*]` (سلوك قديم بلا تغيير) → [PageBackground.Solid].
     *  - `[*Page_background/#346739,#79AE6F,#9FCB98,#F2EDC2*]` (٢-٤ ألوان بفواصل) → [PageBackground.Gradient]
     *    من الأعلى للأسفل بنفس ترتيبها.
     *  - `[*Page_background/forest*]` (أو `harbor`/`lagoon`، أحد [NAMED_PALETTES]) → نفس التدرّج
     *    الجاهز لتلك اللوحة، بلا حاجة لكتابة أربعة رموز سداسية يدوياً.
     *  لا يُعدِّل [markdown] نفسه؛ الاستدعاء المُوصى به هو تمرير [View] الحاوية إلى [applyTo]
     *  مباشرة عبر معامل `pageContainer` ليُطبَّق تلقائياً. */
    fun extractPageBackground(markdown: String): PageBackground? {
        val raw = pageBackgroundRegex.find(markdown)?.groupValues?.get(1)?.trim() ?: return null
        NAMED_PALETTES[raw.lowercase()]?.let { return PageBackground.Gradient(it) }
        val stops = raw.split(",").map { it.trim() }.filter { it.isNotEmpty() }
            .mapNotNull { parseHexColor(it)?.first }
        return when {
            stops.size >= 2 -> PageBackground.Gradient(stops.toIntArray())
            stops.size == 1 -> PageBackground.Solid(stops[0])
            else -> null
        }
    }

    /** بادئة كتلة الوصف القابلة للطي `[~عنوان]` ... `[~/]` — انظر [appendCollapsibleSection]. */
    private const val COLLAPSIBLE_CLOSE_MARKER = "[~/]"

    /** نمط عرض تنبيه GFM واحد (لون الهوية، أيقونة، تسمية العرض) — انظر [appendCallout]. الألوان
     *  الخمسة نفسها المستخدَمة في "ستيكر Rin" ([STICKER_VARIANTS]) أعلاه، فيبقى شعور اللوحة اللونية
     *  موحَّداً عبر كل عناصر الملف بدل تعريف طاقم ألوان مستقل لهذه الميزة وحدها. */
    private data class CalloutStyle(val color: Int, val icon: String, val label: String)
    private val CALLOUT_STYLES: Map<String, CalloutStyle> = mapOf(
        "NOTE" to CalloutStyle(0xFF1A56C7.toInt(), "\u2139\uFE0F", "Note"),
        "TIP" to CalloutStyle(0xFF1D7A4C.toInt(), "\uD83D\uDCA1", "Tip"),
        "IMPORTANT" to CalloutStyle(0xFF6A47E8.toInt(), "\u2757", "Important"),
        "WARNING" to CalloutStyle(0xFFB45F06.toInt(), "\u26A0\uFE0F", "Warning"),
        "CAUTION" to CalloutStyle(0xFFC0392B.toInt(), "\uD83D\uDED1", "Caution")
    )

    /**
     * **إضافة "معاينة Markdown حقيقية": تنبيهات GitHub Flavored Markdown** — صياغة شائعة جداً في
     * READMEs حديثة: `> [!NOTE]` / `[!TIP]` / `[!IMPORTANT]` / `[!WARNING]` / `[!CAUTION]` كأول
     * سطر داخل اقتباس `>`، فيتحوّل الاقتباس كاملاً من علامة تنصيص محايدة إلى بطاقة تنبيه ملوَّنة
     * (لون + أيقونة مميِّزان لكل نوع) بدل بقائها اقتباساً عادياً بلا تمييز دلالي — يُكتشَف هذا في
     * معالج `>` بـ[toSpannable] عبر [calloutMarkerRegex] على أول سطر فقط، وتُستدعى هذه الدالة
     * بدلاً من رسم الاقتباس العادي متى ما طابَقه.
     */
    private fun appendCallout(out: SpannableStringBuilder, type: String, bodyLines: List<String>) {
        val style = CALLOUT_STYLES.getValue(type.uppercase())
        val accent = adaptForTheme(style.color)
        val start = out.length
        val headerStart = out.length
        out.append(style.icon).append(' ').append(style.label)
        out.setSpan(StyleSpan(Typeface.BOLD), headerStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(accent), headerStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        val textStart = out.length
        val nonBlankBody = bodyLines.filter { it.isNotBlank() || bodyLines.size == 1 }
        if (nonBlankBody.isNotEmpty()) {
            out.append('\n')
            nonBlankBody.forEachIndexed { idx, line -> if (idx > 0) out.append('\n'); appendInline(out, line) }
        }
        val end = out.length
        if (end > textStart) {
            out.setSpan(ForegroundColorSpan(COLOR_QUOTE_TEXT), textStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        out.setSpan(QuoteBarSpan(accent), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(
            RoundedCardSpan(tintedBackground(accent), accent, start, end),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    /**
     * يبني معاينة Markdown حقيقية جاهزة لعرضها مباشرة عبر `textView.text = DocumentationContainer.toSpannable(md)`.
     *
     * @param baseDir مجلد أساس اختياري (مثال: `RinEngine.currentBaseDir()`) لحلّ مسارات صور محلية
     * نسبية في `![نص](مسار)` — انظر [appendImage]/[decodeLocalImage]. null = لا حلّ نسبي (صور بمسار
     * مطلق أو `file://` فقط)، بلا أي تغيير في السلوك القديم لأي استدعاء لا يمرِّره.
     * @param expandedSections مجموعة قابلة للتعديل بمعرِّفات أقسام `[~عنوان]` ... `[~/]` المفتوحة
     * حالياً (انظر [appendCollapsibleSection]) — نفس المجموعة يجب تمريرها في كل إعادة بناء لنفس
     * TextView حتى تبقى حالة الفتح/الإغلاق محفوظة بين استدعاء وآخر؛ [applyTo] يتكفّل بهذا تلقائياً.
     * @param dark يختار الباليت الداكن (انظر [applyPalette]) — [applyTo] يحدِّده تلقائياً من ثيم الشاشة.
     * @param onToggle يُستدعى بعد كل نقرة على رأس قسم قابل للطي (بعد تحديث [expandedSections])؛
     * المستدعي مسؤول عن إعادة بناء النص (مثال: استدعاء [applyTo] مجدَّداً بنفس TextView/المجموعة).
     */
    fun toSpannable(
        markdown: String,
        expandedSections: MutableSet<String> = mutableSetOf(),
        baseDir: String? = null,
        dark: Boolean = false,
        rdoc: Boolean = false,
        onToggle: (() -> Unit)? = null
    ): CharSequence {
        currentBaseDir = baseDir
        rdocMode = rdoc
        collapsibleCounter = 0
        containerDepth = 0
        suppressNextGap = false
        val truncated = rdoc && markdown.length > RDOC_MAX_CHARS
        val raw = if (truncated) markdown.substring(0, RDOC_MAX_CHARS) else markdown
        currentMeta = if (rdoc) docMeta + extractMeta(raw) else emptyMap()
        // `{{مفتاح}}` تُستبدَل بقيم بلوك meta قبل أي تحليل (خارج كتل الكود)؛ `\\{{x}}` تبقى حرفية.
        val source = if (rdoc) expandVariables(raw, currentMeta) else raw
        currentHeadings = if (rdoc) docHeadings.ifEmpty { collectHeadings(source) } else emptyList()
        currentApis = if (rdoc) docApis.ifEmpty { collectApis(source) } else emptyList()
        currentLinkRefs = collectLinkRefs(source)
        currentElementRefs = if (rdoc) docElementRefs + collectElementRefs(source) else emptyMap()
        applyPalette(dark)
        if (rdoc) {
            // ألوان العناوين/الروابط من `؛؛؛ صفحة` (تُعاد كل مرة لأن applyPalette يصفّرها).
            val pg = parsePageSettings(source, null) ?: docPageSettings
            pg?.headingColor?.let { COLOR_HEADING = it }
            pg?.linkColor?.let { COLOR_LINK = it }
        }
        val out = SpannableStringBuilder()
        renderBlocks(out, source, expandedSections, onToggle)
        if (truncated) {
            out.append("\n\n")
            appendDiagnosticChip(out, "اقتُطع المستند لتجاوزه الحد المسموح")
        }
        return out
    }

    /**
     * حلقة بناء الكتل الفعلية (فقرات، عناوين، قوائم، كود، اقتباس، جداول، حاويات rdoc...) على [out] نفسه —
     * فصلها عن [toSpannable] يسمح لحاويات `:::` بإعادة استدعائها لجسمها على نفس الـbuilder، فتبقى مواضع
     * البطاقات المدوَّرة ([RoundedCardSpan]) مطلقة وصحيحة داخل الحاوية بلا نسخ Spans بإزاحات خاطئة.
     */
    private fun renderBlocks(
        out: SpannableStringBuilder,
        markdown: String,
        expandedSections: MutableSet<String>,
        onToggle: (() -> Unit)?
    ) {
        val lines = markdown.lines()
        var i = 0

        var inCodeBlock = false
        var codeLang = ""
        val codeBuffer = StringBuilder()
        var lastWasListItem = false

        // مكدّس مسافات بادئة لعناصر القوائم: عمق العنصر = موضعه في المكدّس (نسبياً لما قبله) بدل قسمة
        // ثابتة /2 كانت تقفز بمستوى التعشيش بمسافة 4 (المعتادة) مباشرة للمستوى الثالث؛ والتاب = 4 مسافات.
        val listIndentStack = mutableListOf<Int>()
        fun listDepthFor(rawLine: String): Int {
            var indent = 0
            for (ch in rawLine) {
                if (ch == ' ') indent++ else if (ch == '\t') indent += 4 else break
            }
            while (listIndentStack.isNotEmpty() && listIndentStack.last() > indent) {
                listIndentStack.removeAt(listIndentStack.size - 1)
            }
            if (listIndentStack.isEmpty() || listIndentStack.last() < indent) listIndentStack.add(indent)
            return (listIndentStack.size - 1).coerceIn(0, 4)
        }

        fun blockGap() {
            // أول كتلة داخل حاوية rdoc تلتصق برأسها بلا سطر فارغ (انظر appendRdocContainer).
            if (suppressNextGap) { suppressNextGap = false; return }
            if (out.isNotEmpty()) out.append("\n\n")
        }

        // فراغ إضافي أوسع قبل عنوان قسم رئيسي (H1)، وأصغر قبل قسم فرعي (H2) — يُفصَلان بصرياً عن
        // الفقرة السابقة بأكثر من مجرّد سطر فارغ عادي، بنفس شعور الفصل الواضح بين أقسام/أقسام
        // فرعية في مستند طويل احترافي (بدل تباعد مُوحَّد لكل شيء بلا تمييز بين "فقرة جديدة"
        // و"قسم جديد").
        fun sectionGap(relativeHeight: Float = 0.55f) {
            if (out.isNotEmpty()) {
                val gapStart = out.length
                out.append("\n")
                out.setSpan(RelativeSizeSpan(relativeHeight), gapStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }

        fun flushCodeBlock() {
            if (codeBuffer.isEmpty() && codeLang.isBlank()) return
            blockGap()
            val content = codeBuffer.toString().trimEnd('\n')

            // خريطة شجرة (├── └── │): بطاقة "MAP" بخطوط توجيه مرسومة فعلياً وأيقونات مجلد/ملف
            // بدل عرضها ككود عادي (انظر [appendTreeCard]).
            if (isTreeBlock(codeLang, content)) {
                appendTreeCard(out, content)
                codeBuffer.clear()
                codeLang = ""
                lastWasListItem = false
                return
            }

            // بطاقة كود واحدة متّصلة: [رأس (نقطة اللغة + اسمها + نسخ) على شريط مميَّز] ثم [فراغ صغير]
            // ثم [أسطر الكود بأرقام أسطر خافتة] ثم [فراغ سفلي صغير] — يُرسَم حولها كلها إطار مدوَّر
            // واحد بلا خطوط فاصلة داخلية (انظر RoundedCardSpan).
            val cardStart = out.length
            appendCodeHeader(out, codeLang, content)
            out.append('\n')
            val headerEnd = out.length

            // فراغ علوي بين الرأس وأول سطر كود (سطر مصغَّر بما فيه محرف السطر الجديد).
            val topPadStart = out.length
            out.append("\u00A0\n")
            out.setSpan(RelativeSizeSpan(0.45f), topPadStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

            val start = out.length
            appendHighlightedCode(out, forceLtrPerLine(content))
            val end = out.length
            out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

            // أرقام الأسطر: هامش حقيقي لكل سطر منطقي (تُرسَم في أول سطر مرئي منه فقط، فلا تتكرّر
            // على الأسطر الملتفّة)، وتُعرَض فقط للكتل التي تتجاوز سطرين حتى لا تثقل المقاطع القصيرة.
            val logicalLines = content.lines().size
            val pad = dpPx(14f)
            if (logicalLines >= 3) {
                val gutter = dpPx(10f) + logicalLines.toString().length * dpPx(8f)
                var lineStart = start
                var number = 1
                while (lineStart < end) {
                    var lineEnd = lineStart
                    while (lineEnd < end && out[lineEnd] != '\n') lineEnd++
                    if (lineEnd > lineStart) {
                        out.setSpan(
                            CodeLineNumberSpan(number.toString(), COLOR_CODE_GUTTER, pad, gutter, dpPx(8f)),
                            lineStart, lineEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                        )
                    }
                    number++
                    lineStart = lineEnd + 1
                }
            } else {
                out.setSpan(LeadingMarginSpan.Standard(pad.toInt()), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }

            // فراغ سفلي مصغَّر يمنح آخر سطر كود هامشاً قبل حافة البطاقة.
            out.append('\n')
            val bottomPadStart = out.length
            out.append('\u00A0')
            out.setSpan(RelativeSizeSpan(0.45f), bottomPadStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            val cardEnd = out.length

            // حشوة أفقية موحَّدة للرأس والفراغين (كان الرأس بلا أي حشوة يلاصق الحافة). أسطر الكود
            // تحمل حشوتها بنفسها (داخل CodeLineNumberSpan أو Standard أعلاه) كي لا يتوقف موضع الرقم
            // على ترتيب رسم عدّة LeadingMarginSpan على الفقرة نفسها.
            out.setSpan(LeadingMarginSpan.Standard(pad.toInt()), cardStart, start, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(LeadingMarginSpan.Standard(pad.toInt()), bottomPadStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(
                RoundedCardSpan(
                    COLOR_CODE_BLOCK_BG, COLOR_CARD_BORDER, cardStart, cardEnd,
                    cornerRadius = dpPx(12f), insetTop = dpPx(7f), insetBottom = dpPx(6f),
                    borderWidth = dpPx(1f).coerceAtLeast(1f),
                    headerBg = COLOR_CODE_HEADER_BG, headerEnd = headerEnd, headerDivider = COLOR_HEADER_RULE
                ),
                cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            codeBuffer.clear()
            codeLang = ""
            lastWasListItem = false
        }

        while (i < lines.size) {
            val rawLine = lines[i]

            if (rawLine.trim().startsWith("```")) {
                if (inCodeBlock) {
                    flushCodeBlock(); inCodeBlock = false
                } else {
                    inCodeBlock = true
                    codeLang = rawLine.trim().removePrefix("```").trim()
                }
                i++; continue
            }
            if (inCodeBlock) { codeBuffer.append(rawLine).append('\n'); i++; continue }

            val trimmed = rawLine.trim()
            val isListLine = taskListRegex.matches(trimmed) || orderedListRegex.matches(trimmed) ||
                bulletListRegex.matches(trimmed)
            if (!isListLine && trimmed.isNotEmpty()) listIndentStack.clear()
            val depth = if (isListLine) listDepthFor(rawLine) else 0

            when {
                trimmed.isEmpty() -> { lastWasListItem = false; i++ }

                // سطر خلفية الصفحة المستقل — يُستهلَك بصمت، لا يُعرَض كمحتوى مرئي إطلاقاً.
                // اللون الفعلي يُستخرَج لاحقاً عبر [extractPageBackground] من النص الخام كاملاً.
                pageBackgroundLineRegex.matches(trimmed) -> { lastWasListItem = false; i++ }

                // تعريف رابط مرجعي `[id]: url` — يُستهلَك بصمت (يُستخدَم عبر `[نص][id]`).
                linkRefDefRegex.matches(rawLine) -> { lastWasListItem = false; i++ }

                // ── rdoc: تعليق سطر كامل `؛؛ نص` (لا يُعرَض إطلاقاً) ──
                rdocMode && trimmed.startsWith("\u061B\u061B") && !trimmed.startsWith("\u061B\u061B\u061B") -> {
                    i++
                }

                // ── rdoc: عناوين بأسلوب `= عنوان` … `====== عنوان` (بديل `#`) ──
                rdocMode && rdocHeadingRegex.matches(trimmed) -> {
                    val hm = rdocHeadingRegex.find(trimmed)!!
                    val text = hm.groupValues[2]
                    when (hm.groupValues[1].length) {
                        1 -> { sectionGap(); blockGap(); appendHeading(out, text, 1.6f, level = 1) }
                        2 -> { sectionGap(0.3f); blockGap(); appendHeading(out, text, 1.28f, level = 2) }
                        3 -> { blockGap(); appendHeading(out, text, 1.15f, level = 3) }
                        4 -> { blockGap(); appendHeading(out, text, 1.05f, level = 4) }
                        5 -> { blockGap(); appendHeading(out, text, 0.95f, level = 5, dim = true) }
                        else -> { blockGap(); appendHeading(out, text, 0.85f, level = 6, dim = true) }
                    }
                    lastWasListItem = false
                    i++
                }

                // ── rdoc: حاوية `:::` قابلة للتعشيش (انظر appendRdocContainer) ──
                rdocMode && colonFenceOpenRegex.matches(trimmed) -> {
                    val header = colonFenceOpenRegex.find(trimmed)!!.groupValues[1]
                    val scan = scanFence(lines, i, colonFenceOpenRegex, colonFenceCloseRegex)
                    blockGap()
                    appendRdocContainer(out, header, scan.body, scan.closed, expandedSections, onToggle)
                    lastWasListItem = false
                    i = scan.next
                }

                // ── rdoc: بلوك بيانات `؛؛؛` (حقائق/meta/مراجع) ──
                rdocMode && arSemiFenceOpenRegex.matches(trimmed) -> {
                    val header = arSemiFenceOpenRegex.find(trimmed)!!.groupValues[1]
                    val scan = scanDataFence(lines, i)
                    val dataType = header.trim().split(whitespaceRegex)[0].lowercase()
                    // الصامتة المغلقة (meta/rdoc/مراجع) لا تترك أي فراغ؛ ما سواها يظهر شيء مرئي.
                    if (dataType !in SILENT_DATA_TYPES || !scan.closed) blockGap()
                    appendRdocDataBlock(out, header, scan)
                    lastWasListItem = false
                    i = if (scan.next > i) scan.next else i + 1
                }

                // ── rdoc: إغلاق سياج بلا فتح → خطأ مرئي بدل الصمت ──
                rdocMode && (colonFenceCloseRegex.matches(trimmed) || arSemiFenceCloseRegex.matches(trimmed)) -> {
                    blockGap()
                    appendDiagnosticChip(out, "إغلاق سياج بلا فتح")
                    lastWasListItem = false
                    i++
                }

                // `<details><summary>عنوان</summary> ... </details>` بأسلوب GitHub → نفس القسم القابل للطي.
                trimmed.startsWith("<details", ignoreCase = true) -> {
                    val blockLines = mutableListOf<String>()
                    var j = i
                    while (j < lines.size) {
                        blockLines.add(lines[j])
                        if (lines[j].contains("</details>", ignoreCase = true)) break
                        j++
                    }
                    val block = blockLines.joinToString("\n")
                    val summary = detailsSummaryRegex.find(block)
                    val title = plainInlineText(summary?.groupValues?.get(1).orEmpty()).trim()
                    val rest = if (summary != null) block.substring(summary.range.last + 1) else block
                    val bodyText = rest.replace(detailsTagRegex, "").trim()
                    blockGap()
                    val sectionId = "det${collapsibleCounter++}:${title.ifBlank { "details" }}"
                    val isOpen = expandedSections.contains(sectionId)
                    appendCollapsibleSection(out, title, bodyText, isOpen) {
                        if (!expandedSections.remove(sectionId)) expandedSections.add(sectionId)
                        onToggle?.invoke()
                    }
                    lastWasListItem = false
                    i = j + 1
                }

                // قسم وصف قابل للطي: `[~عنوان]` يبدأ الكتلة، `[~/]` وحدها على سطر مستقل تُنهيها.
                trimmed.startsWith("[~") && trimmed.endsWith("]") && trimmed != COLLAPSIBLE_CLOSE_MARKER -> {
                    val title = trimmed.removePrefix("[~").removeSuffix("]").trim()
                    val bodyLines = mutableListOf<String>()
                    var j = i + 1
                    while (j < lines.size && lines[j].trim() != COLLAPSIBLE_CLOSE_MARKER) {
                        bodyLines.add(lines[j]); j++
                    }
                    blockGap()
                    val sectionId = "sec${collapsibleCounter++}:${title.ifBlank { "وصف" }}"
                    val isOpen = expandedSections.contains(sectionId)
                    appendCollapsibleSection(out, title, bodyLines.joinToString("\n"), isOpen) {
                        if (!expandedSections.remove(sectionId)) expandedSections.add(sectionId)
                        onToggle?.invoke()
                    }
                    lastWasListItem = false
                    i = if (j < lines.size) j + 1 else j // يتخطّى سطر [~/] الختامي إن وُجد
                }

                trimmed == "---" || trimmed == "***" || trimmed == "___" -> {
                    blockGap()
                    val start = out.length
                    out.append("\u00A0")
                    out.setSpan(RuleSpan(), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    lastWasListItem = false
                    i++
                }

                trimmed.startsWith("> ") || trimmed == ">" -> {
                    val quoteLines = mutableListOf<String>()
                    var j = i
                    while (j < lines.size) {
                        val t = lines[j].trim()
                        if (t.startsWith("> ") || t == ">") {
                            quoteLines.add(t.removePrefix(">").trimStart(' '))
                            j++
                        } else break
                    }
                    blockGap()
                    val calloutType = quoteLines.firstOrNull()?.let { calloutMarkerRegex.find(it) }
                        ?.groupValues?.get(1)?.uppercase()
                    if (calloutType != null) {
                        appendCallout(out, calloutType, quoteLines.drop(1))
                        lastWasListItem = false
                        i = j
                    } else {
                        val start = out.length
                        val glyphStart = out.length
                        out.append("\u201C ") // علامة تنصيص مزخرفة لتمييز المقتطف المُقتبَس بصرياً
                        val glyphEnd = out.length
                        out.setSpan(RelativeSizeSpan(1.3f), glyphStart, glyphEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        out.setSpan(StyleSpan(Typeface.BOLD), glyphStart, glyphEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        out.setSpan(ForegroundColorSpan(COLOR_QUOTE_BAR), glyphStart, glyphEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        val textStart = out.length
                        quoteLines.forEachIndexed { idx, ql ->
                            if (idx > 0) out.append("\n")
                            appendInline(out, ql)
                        }
                        val end = out.length
                        // نطاقات منفصلة غير متداخلة (بدل نطاق واحد شامل) حتى لا يطغى لون النص العام
                        // على لون علامة التنصيص المميّزة أعلاه عند تطبيق الـSpans بالترتيب.
                        out.setSpan(QuoteBarSpan(COLOR_QUOTE_BAR), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        out.setSpan(
                            RoundedCardSpan(COLOR_QUOTE_BG, COLOR_CARD_BORDER, start, end),
                            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                        )
                        out.setSpan(ForegroundColorSpan(COLOR_QUOTE_TEXT), textStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        out.setSpan(StyleSpan(Typeface.ITALIC), textStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        lastWasListItem = false
                        i = j
                    }
                }

                trimmed.contains("|") && i + 1 < lines.size && isTableSeparator(lines[i + 1]) -> {
                    val headerCells = splitTableRow(trimmed)
                    val bodyRows = mutableListOf<List<String>>()
                    var j = i + 2
                    while (j < lines.size && lines[j].trim().contains("|") && lines[j].isNotBlank()) {
                        bodyRows.add(splitTableRow(lines[j].trim()))
                        j++
                    }
                    blockGap()
                    appendTable(out, headerCells, bodyRows)
                    lastWasListItem = false
                    i = j
                }

                trimmed.startsWith("###### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("###### "), 0.85f, level = 6, dim = true); lastWasListItem = false; i++
                }
                trimmed.startsWith("##### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("##### "), 0.95f, level = 5, dim = true); lastWasListItem = false; i++
                }
                trimmed.startsWith("#### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("#### "), 1.05f, level = 4); lastWasListItem = false; i++
                }
                trimmed.startsWith("### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("### "), 1.15f, level = 3); lastWasListItem = false; i++
                }
                trimmed.startsWith("## ") -> {
                    sectionGap(0.3f); blockGap(); appendHeading(out, trimmed.removePrefix("## "), 1.28f, level = 2); lastWasListItem = false; i++
                }
                trimmed.startsWith("# ") -> {
                    sectionGap(); blockGap(); appendHeading(out, trimmed.removePrefix("# "), 1.6f, level = 1); lastWasListItem = false; i++
                }

                taskListRegex.matches(trimmed) -> {
                    val m = taskListRegex.find(trimmed)!!
                    val checked = m.groupValues[1].equals("x", ignoreCase = true)
                    val content = m.groupValues[2]
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val lineStart = out.length
                    val textStart = out.length
                    appendInline(out, content)
                    if (checked) {
                        out.setSpan(StrikethroughSpan(), textStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        out.setSpan(ForegroundColorSpan(COLOR_TASK_PENDING), textStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    }
                    // مربّع اختيار مرسوم فعلياً (بدل حرف ☐/☑): مربّع مدوَّر بحدّ للمعلَّق، ومملوء بلون
                    // النجاح مع علامة صحّ بيضاء للمُنجَز — يتحاذى مع أول سطر ويبقى الالتفاف معلَّقاً.
                    out.setSpan(
                        TaskBoxSpan(checked, COLOR_TASK_DONE, COLOR_TASK_PENDING, listIndentPx(depth), dpPx(26f)),
                        lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    lastWasListItem = true
                    i++
                }

                orderedListRegex.matches(trimmed) -> {
                    val m = orderedListRegex.find(trimmed)!!
                    val number = m.groupValues[1]
                    val content = m.groupValues[2]
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val lineStart = out.length
                    appendInline(out, content)
                    // الرقم يُرسَم في الهامش بعرض ثابت محاذى نحو النص (الأرقام 1..99 تتراصّ عمودياً)،
                    // وتبقى الأسطر الملتفّة معلَّقة تماماً تحت أول حرف من النص لا تحت الرقم.
                    val label = "$number."
                    out.setSpan(
                        OrderedMarkerSpan(
                            label, BULLET_DEPTH_COLORS[depth % BULLET_DEPTH_COLORS.size],
                            listIndentPx(depth), dpPx(8f) + label.length * dpPx(8f), dpPx(8f)
                        ),
                        lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    lastWasListItem = true
                    i++
                }

                bulletListRegex.matches(trimmed) -> {
                    val m = bulletListRegex.find(trimmed)!!
                    val content = m.groupValues[1]
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val lineStart = out.length
                    appendInline(out, content)
                    // علامة بحسب العمق: دائرة ممتلئة ← حلقة مفرغة ← مربّع مدوَّر (بدل نقطة BulletSpan
                    // الصغيرة الموحَّدة الشكل)، بنفس ألوان الأعماق، مع هامش معلَّق واحد متّسق.
                    out.setSpan(
                        ListMarkerSpan(
                            BULLET_DEPTH_COLORS[depth % BULLET_DEPTH_COLORS.size], depth,
                            listIndentPx(depth), dpPx(20f)
                        ),
                        lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    lastWasListItem = true
                    i++
                }

                else -> {
                    blockGap()
                    appendInline(out, trimmed)
                    lastWasListItem = false
                    i++
                }
            }
        }
        if (inCodeBlock) flushCodeBlock()
    }

    /**
     * يبني نص Markdown مباشرة داخل [textView]، ويُفعِّل [LinkMovementMethod] ولون الروابط حتى
     * تعمل روابط `[نص](رابط)` فعلياً بالنقر — الاستخدام المُوصى به بدل تعيين `.text` يدوياً.
     */
    /**
     * مقطع واحد من مقاطع [splitLiveCodeBlocks]: إمّا نص Markdown عادي يُعرَض عبر [toSpannable]
     * كالمعتاد، أو كتلة كود Rin/indsin مستخرَجة لتُعرَض ببطاقة "معاينة حية" منفصلة (كودها +
     * محاولة تشغيلها فعلياً عبر المحرّك الأصلي) بدل نص كود ثابت داخل نفس مسار العرض العادي.
     */
    sealed class MarkdownSegment {
        data class Text(val markdown: String) : MarkdownSegment()
        data class LiveCode(val language: String, val code: String) : MarkdownSegment()

        /** جدول Markdown مُستخرَج ليُعرَض كجدول حقيقي عبر [buildTableView] بدل صندوق نصّي مقصوص. */
        data class Table(
            val headers: List<String>,
            val aligns: List<Int>,
            val rows: List<List<String>>
        ) : MarkdownSegment()

        /** سطر `:; … ;:` مستقل (rdoc) — يُعرَض عبر [buildMediaView] كملصق تشغيل يفتح WebView مقيَّداً عند النقر.
         *  [kind] `video`/`web`، [url] الرابط الأصلي (https)، [embedUrl] صفحة التضمين، [heightDp] ارتفاع البطاقة. */
        data class Media(
            val kind: String,
            val url: String,
            val embedUrl: String,
            val title: String,
            val heightDp: Int,
            /** عرض/ارتفاع؛ 0 = استخدم [heightDp]. يوتيوب: 16:9 افتراضياً، وshorts بـ9:16. */
            val ratio: Float = 0f,
            val youtube: Boolean = false,
            /** رابط المشاهدة الأصلي للفتح الخارجي (يوتيوب: بوقت البدء). */
            val watchUrl: String = "",
            val startSec: Int = 0,
            val thumbId: String? = null,
            val thumb: Boolean = false
        ) : MarkdownSegment()
    }

    private val liveCodeBlockRegex = Regex(
        "```(rin|indsin)[ \\t]*\\r?\\n([\\s\\S]*?)```",
        RegexOption.IGNORE_CASE
    )

    /**
     * يقسّم [markdown] إلى مقاطع نصية عادية ومقاطع كود Rin/indsin حيّة منفصلة (```rin ... ```
     * أو ```indsin ... ```)، حتى يمكن لـ[com.dlof.rinlang.store.PackageDetailActivity.bindReadmeAndLicense]
     * عرض معاينة حيّة حقيقية أسفل كل كتلة كود Rin (وجهة/صفحة مُصيَّرة فعلياً عبر المحرّك الأصلي إن
     * وُجد `@view` قابل للعرض، وإلا نتيجة تنفيذ فعلية — يناسب هذا أي نوع كود آخر: حاوية `@container`
     * بلا واجهة، جدولة، منطق عادي...) بدل أن تبقى مجرد نص كود ثابت كباقي لغات الكود الأخرى (التي
     * تُعرَض كالمعتاد بلا أي تغيير ضمن مقاطع [MarkdownSegment.Text] العادية عبر [toSpannable]).
     * كتل الكود بأي لغة أخرى غير rin/indsin لا تُقتطَع هنا إطلاقاً وتبقى ضمن نص Markdown العادي.
     */
    fun splitLiveCodeBlocks(markdown: String): List<MarkdownSegment> {
        val segments = mutableListOf<MarkdownSegment>()
        var lastEnd = 0
        for (match in liveCodeBlockRegex.findAll(markdown)) {
            if (match.range.first > lastEnd) {
                segments.add(MarkdownSegment.Text(markdown.substring(lastEnd, match.range.first)))
            }
            val lang = match.groupValues[1].lowercase()
            val code = match.groupValues[2].trimEnd('\n')
            segments.add(MarkdownSegment.LiveCode(lang, code))
            lastEnd = match.range.last + 1
        }
        if (lastEnd < markdown.length) {
            segments.add(MarkdownSegment.Text(markdown.substring(lastEnd)))
        }
        return segments
    }

    /**
     * **جديد: جداول حقيقية** — يقسّم [markdown] إلى نص عادي وكتل كود Rin حيّة (كما [splitLiveCodeBlocks])
     * وجداول Markdown ([MarkdownSegment.Table]) لتُعرَض عبر [buildTableView]: خلايا تلتفّ بدل القصّ
     * عند 24 حرفاً، تمرير أفقي للجداول العريضة، محاذاة أعمدة، وتنسيق سطري داخل الخلايا (تشديد/روابط/كود)،
     * وتعمل مع العربية (الصندوق النصّي الأحادي المسافة كان ينحرف مع الحروف العربية). الجداول داخل كتل
     * الكود الأخرى أو داخل قسم قابل للطي `[~...]` تبقى ضمن النص كما كانت.
     */
    fun splitReadmeSegments(markdown: String): List<MarkdownSegment> {
        val rdoc = isRdocDocument(markdown)
        // مراجع `*"id"*` على مستوى المستند كله: كل مقطع نصي يُرسَم لاحقاً بشكل مستقل ويحتاج رؤية تعريفات غيره.
        docElementRefs = if (rdoc) collectElementRefs(markdown) else emptyMap()
        docMeta = if (rdoc) extractMeta(markdown) else emptyMap()
        docPageSettings = if (rdoc) parsePageSettings(markdown, null) else null
        docHeadings = if (rdoc) collectHeadings(markdown) else emptyList()
        docApis = if (rdoc) collectApis(markdown) else emptyList()
        val result = mutableListOf<MarkdownSegment>()
        // حاويات `:::` لا تُقطَع بين مقاطع العرض (جدول/معاينة حيّة/وسائط داخلها تبقى في بطاقتها كنص).
        val chunks = if (rdoc) splitContainerChunks(markdown) else listOf(markdown to false)
        for ((chunk, isContainer) in chunks) {
            if (isContainer) {
                result.add(MarkdownSegment.Text(chunk))
                continue
            }
            for (seg in splitLiveCodeBlocks(chunk)) {
                if (seg is MarkdownSegment.Text) result.addAll(splitTables(seg.markdown, rdoc)) else result.add(seg)
            }
        }
        return result
    }

    private fun splitTables(markdown: String, rdoc: Boolean = false): List<MarkdownSegment> {
        val lines = markdown.lines()
        val out = mutableListOf<MarkdownSegment>()
        val pending = StringBuilder()
        fun flushText() {
            if (pending.isNotBlank()) out.add(MarkdownSegment.Text(pending.toString()))
            pending.setLength(0)
        }
        var inFence = false
        var inSection = false
        var i = 0
        while (i < lines.size) {
            val line = lines[i]
            val t = line.trim()
            if (t.startsWith("```")) {
                inFence = !inFence
            } else if (!inFence) {
                if (t.startsWith("[~") && t.endsWith("]") && t != COLLAPSIBLE_CLOSE_MARKER) inSection = true
                else if (t == COLLAPSIBLE_CLOSE_MARKER) inSection = false
                else if (t.startsWith("<details", ignoreCase = true)) inSection = !t.contains("</details>", ignoreCase = true)
                else if (inSection && t.contains("</details>", ignoreCase = true)) inSection = false
            }
            // rdoc: سطر `:; … ;:` مستقل → بطاقة وسائط (فيديو/ويب فيو) بدل نص.
            if (rdoc && !inFence && !inSection && t.length > 4 && t.startsWith(":;") && t.endsWith(";:")) {
                val media = parseMedia(t.substring(2, t.length - 2))
                if (media != null) {
                    flushText()
                    out.add(media)
                    i++
                    continue
                }
            }
            if (!inFence && !inSection && t.contains("|") && i + 1 < lines.size && isTableSeparator(lines[i + 1])) {
                val header = splitTableRow(t)
                val aligns = parseTableAligns(lines[i + 1])
                val rows = mutableListOf<List<String>>()
                var j = i + 2
                while (j < lines.size && lines[j].isNotBlank() && lines[j].trim().contains("|")) {
                    rows.add(splitTableRow(lines[j].trim()))
                    j++
                }
                flushText()
                out.add(MarkdownSegment.Table(header, aligns, rows))
                i = j
                continue
            }
            pending.append(line).append('\n')
            i++
        }
        flushText()
        return out
    }

    /**
     * يبني جدولاً حقيقياً من [table]: بطاقة بزوايا مدوَّرة وحدّ خفيف، صف رأس بلون الهوية، تظليل
     * تناوبي، خلايا تلتفّ عند 220dp، وتمرير أفقي تلقائي للجداول الأعرض من الشاشة. كل خلية تُعرَض
     * عبر [applyTo] فتعمل الروابط والتشديد والكود المضمَّن والإيموجي داخلها، ويُختار الباليت
     * الفاتح/الداكن من ثيم [context]. [topMarginPx] هامش علوي بالبكسل (الحاوية LinearLayout).
     */
    fun buildTableView(context: Context, table: MarkdownSegment.Table, topMarginPx: Int = 0, rdoc: Boolean = false): View {
        applyPalette(paletteDark(context, if (rdoc) docPageSettings else null))
        val density = context.resources.displayMetrics.density
        fun dp(v: Float): Int = (v * density + 0.5f).toInt()

        val colCount = maxOf(table.headers.size, table.rows.maxOfOrNull { it.size } ?: 0).coerceAtLeast(1)

        val dividerLine = GradientDrawable().apply {
            setColor(COLOR_TABLE_BORDER)
            setSize(dp(1f), dp(1f))
        }

        val grid = TableLayout(context).apply {
            isStretchAllColumns = true
            showDividers = LinearLayout.SHOW_DIVIDER_MIDDLE
            dividerDrawable = dividerLine
            background = GradientDrawable().apply {
                setColor(COLOR_TABLE_BG)
                cornerRadius = dp(12f).toFloat()
                setStroke(dp(1f), COLOR_CARD_BORDER)
            }
            clipToOutline = true
        }

        fun addRow(cells: List<String>, isHeader: Boolean, zebra: Boolean) {
            val row = TableRow(context).apply {
                showDividers = LinearLayout.SHOW_DIVIDER_MIDDLE
                dividerDrawable = dividerLine
                if (isHeader) setBackgroundColor(tintedBackground(COLOR_BULLET, 0x24))
                else if (zebra) setBackgroundColor(COLOR_TABLE_ROW_ALT)
            }
            for (c in 0 until colCount) {
                val cell = TextView(context).apply {
                    textSize = 13f
                    setPadding(dp(12f), dp(9f), dp(12f), dp(9f))
                    maxWidth = dp(220f)
                    gravity = Gravity.CENTER_VERTICAL or table.aligns.getOrElse(c) { Gravity.START }
                    setTextColor(if (isHeader) COLOR_TABLE_HEADER else COLOR_TABLE_TEXT)
                    if (isHeader) setTypeface(typeface, Typeface.BOLD)
                }
                applyTo(cell, cells.getOrNull(c).orEmpty(), rdoc = rdoc, pageStyle = false)
                row.addView(cell)
            }
            grid.addView(row)
        }

        addRow(table.headers, isHeader = true, zebra = false)
        table.rows.forEachIndexed { idx, r -> addRow(r, isHeader = false, zebra = idx % 2 == 1) }

        return HorizontalScrollView(context).apply {
            isHorizontalScrollBarEnabled = false
            isFillViewport = true
            overScrollMode = View.OVER_SCROLL_NEVER
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { topMargin = topMarginPx }
            addView(grid, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT
            ))
        }
    }

    /**
     * @param pageContainer إن مُرِّرت (مثال: `containerReadme` الحاوي لكل مقاطع README)، يُطبَّق
     * تلقائياً عليها لون `[*Page_background/#hex*]` إن وُجد في [markdown] (انظر [extractPageBackground]).
     * @param expandedSections حالة الأقسام القابلة للطي المفتوحة حالياً؛ الافتراضي مجموعة جديدة
     * فارغة تبقى حيّة عبر إغلاقات النقر (closures) التي يبنيها هذا الاستدعاء، فتُعاد نفس الحالة
     * تلقائياً عند إعادة بناء النص بعد كل نقرة — لا حاجة لتمريرها يدوياً في الاستخدام العادي.
     * @param baseDir انظر معامل baseDir في [toSpannable] — يُمرَّر كما هو، افتراضياً null (لا تغيير
     * في أي استدعاء قديم لا يمرِّره).
     */
    fun applyTo(
        textView: TextView,
        markdown: String,
        pageContainer: View? = null,
        expandedSections: MutableSet<String> = mutableSetOf(),
        baseDir: String? = null,
        rdoc: Boolean = false,
        pageStyle: Boolean = true
    ) {
        // عرض حقيقي للصور المضمَّنة يناسب TextView الفعلي بدل قيمة تقديرية ثابتة دوماً؛ عرض الشاشة
        // الكامل كحدّ أقصى احتياطي إن لم يكن TextView قد قِيس بعد (width == 0 قبل أول تخطيط).
        currentDensity = textView.resources.displayMetrics.density
        currentImageMaxWidthPx = textView.width.takeIf { it > 0 }
            ?: (textView.resources.displayMetrics.widthPixels - (32 * textView.resources.displayMetrics.density).toInt())
                .coerceAtLeast(DEFAULT_IMAGE_MAX_WIDTH_PX)
        val page = if (rdoc) (parsePageSettings(markdown, null) ?: docPageSettings) else null
        val dark = paletteDark(textView.context, page)
        textView.text = toSpannable(markdown, expandedSections, baseDir, dark, rdoc) {
            applyTo(textView, markdown, pageContainer, expandedSections, baseDir, rdoc, pageStyle)
        }
        textView.movementMethod = LinkMovementMethod.getInstance()
        textView.setLinkTextColor(COLOR_LINK)
        textView.highlightColor = COLOR_HIGHLIGHT_BG
        if (page != null && pageStyle) applyPageTextStyle(textView, page, dark)
        pageContainer?.let { container ->
            when (val bg = extractPageBackground(markdown)) {
                is PageBackground.Solid -> container.background = ColorDrawable(bg.color)
                is PageBackground.Gradient -> container.background = GradientDrawable(
                    GradientDrawable.Orientation.TOP_BOTTOM, bg.colors
                )
                null -> {}
            }
        }
        loadPendingRemoteImages(textView)
        startMarqueeIfNeeded(textView)
    }

    /**
     * يبحث في نص [textView] الحالي عن كل [PendingImageSpan] (عناصر نائبة لصور بعيدة قيد التحميل،
     * انظر [appendImage])، ويطلب تحميل كل رابط منها عبر [loadRemoteImageAsync]، فإن نجح يستبدل
     * نطاق العنصر النائب بـ[ImageSpan] حقيقي مباشرة داخل [Spannable] المُعلَّق فعلياً على
     * [textView] (لا نص جديد يُبنى من الصفر). يتحقّق أولاً أن [textView] لم يُعِد بناء نصّه لسبب
     * آخر (نقرة على قسم قابل للطي مثلاً) بين لحظة الطلب واكتمال التنزيل عبر `getSpanStart` -- قيمة
     * سالبة تعني أن الوسم لم يعد جزءاً من النص الحالي، فيُسقَط الاستبدال بصمت بلا أي أثر.
     */
    private fun loadPendingRemoteImages(textView: TextView) {
        val spannable = textView.text as? Spannable ?: return
        val pendings = spannable.getSpans(0, spannable.length, PendingImageSpan::class.java)
        val maxWidthPx = currentImageMaxWidthPx
        for (pending in pendings) {
            loadRemoteImageAsync(pending.url) { bitmap ->
                if (bitmap == null) return@loadRemoteImageAsync
                val current = textView.text as? Spannable ?: return@loadRemoteImageAsync
                val start = current.getSpanStart(pending)
                val end = current.getSpanEnd(pending)
                if (start < 0 || end <= start) return@loadRemoteImageAsync // نص أُعيد بناؤه؛ لم يعد هذا الوسم موجوداً
                val (w, h) = if (bitmap.width > maxWidthPx) {
                    val scale = maxWidthPx.toFloat() / bitmap.width
                    maxWidthPx to (bitmap.height * scale).toInt().coerceAtLeast(1)
                } else {
                    bitmap.width to bitmap.height
                }
                val drawable = BitmapDrawable(textView.resources, bitmap).apply { setBounds(0, 0, w, h) }
                current.setSpan(ImageSpan(drawable, ImageSpan.ALIGN_BOTTOM), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                current.removeSpan(pending)
                textView.text = current // يُجبر TextView على إعادة القياس/الرسم بالأبعاد الجديدة للصورة
            }
        }
    }

    private val headingClosingHashesRegex = Regex("\\s+#+\\s*$")

    /**
     * يضيف نص عنوان بحجم [scale] النسبي وتشديد كامل، مع تطبيق تشديد أو كود داخلي إن وُجد.
     * العنوانان الأول والثاني ([level] 1 أو 2) يحصلان إضافياً على خط فاصل تحتهما — تماماً كأسلوب
     * عرض README الاحترافي على GitHub — لفصل الأقسام بصرياً بدل الاعتماد على المسافة فقط.
     */
    private fun appendHeading(out: SpannableStringBuilder, text: String, scale: Float, level: Int, dim: Boolean = false) {
        val start = out.length
        // `## عنوان ##` → يُسقَط تسلسل الإغلاق (يسبقه فراغ فقط، فلا يمسّ `C#`) كما في CommonMark
        appendInline(out, text.replace(headingClosingHashesRegex, ""))
        val end = out.length
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(scale), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(
            ForegroundColorSpan(if (dim) COLOR_H_DIM else COLOR_HEADING),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )

        // عنوان القسم الرئيسي (H1) يحصل على "لافتة" خلفية بلون الهوية خافتة خلف نصّه مباشرة —
        // ليست مجرّد نص أسود عريض فوق الصفحة، بل شريط عنوان مميَّز بصرياً (بنفس روح لافتات عنوان
        // المستندات الاحترافية)، إلى جانب الخط البنفسجي السفلي الموجود أصلاً.
        if (level == 1) {
            out.setSpan(
                RoundedCardSpan(
                    tintedBackground(COLOR_BULLET, 0x14), COLOR_BULLET, start, end,
                    cornerRadius = 10f, insetTop = 6f, insetBottom = 6f, borderWidth = 0f
                ),
                start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
        }

        if (level == 1 || level == 2) {
            out.append("\n")
            val ruleStart = out.length
            out.append("\u00A0")
            val ruleEnd = out.length
            val ruleColor = if (level == 1) COLOR_BULLET else COLOR_RULE
            val thickness = if (level == 1) 3f else 1.5f
            out.setSpan(RuleSpan(ruleColor, thickness), ruleStart, ruleEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    /**
     * يطبّق كل الأنماط السطرية المدعومة ضمن سطر واحد (تشديد/مائل/شطب/كود/تمييز/روابط/ستيكر Rin)
     * ويُلحق الباقي كنص عادي. يدعم تعشيشاً بمستوى واحد (مثال: `**نص _مائل_ داخل تشديد**`) عبر
     * إعادة الاستدعاء الذاتي لكل نمط باستثناء الكود والستيكر، اللذين يبقى محتواهما حرفياً دوماً.
     */
    private fun appendInline(out: SpannableStringBuilder, text: String) {
        var idx = 0
        val b = if (rdocMode) RDOC_GROUP_OFFSET else 0
        for (match in (if (rdocMode) inlineRegexRdoc else inlineRegex).findAll(text)) {
            if (match.range.first > idx) out.append(text.substring(idx, match.range.first))
            val g = match.groups
            when {
                // بدائل rdoc تسبق القديمة (فهارسها 1..6 مستقلة، وفهارس القديمة مُزاحة بـb): انظر RDOC_INLINE_PREFIX.
                rdocMode && g[1] != null -> out.append(g[1]!!.value)
                rdocMode && g[2] != null -> appendElementLink(out, g[2]!!.value)
                rdocMode && g[3] != null -> appendPayment(out, "", g[3]!!.value, null, null, null)
                rdocMode && g[4] != null -> appendMediaInline(out, g[4]!!.value)
                rdocMode && g[5] != null && g[6] != null -> appendTypedBracket(out, g[5]!!.value, g[6]!!.value, match.value)
                g[b + 1] != null -> out.append(g[b + 1]!!.value) // \x حرف مُفلَت → حرفي بلا تنسيق
                g[b + 2] != null -> appendStyled(out, g[b + 2]!!.value, Typeface.BOLD_ITALIC)
                g[b + 3] != null -> appendStyled(out, g[b + 3]!!.value, Typeface.BOLD)
                g[b + 4] != null -> appendStyled(out, g[b + 4]!!.value, Typeface.BOLD)
                g[b + 5] != null -> appendStrike(out, g[b + 5]!!.value)
                g[b + 6] != null -> appendCode(out, g[b + 6]!!.value)
                g[b + 7] != null -> appendHighlight(out, g[b + 7]!!.value)
                g[b + 8] != null -> appendSticker(out, g[b + 8]!!.value)
                g[b + 9] != null -> appendMetaBadge(out, g[b + 9]!!.value)
                g[b + 10] != null && g[b + 11] != null -> appendShieldsBadge(out, g[b + 10]!!.value, g[b + 11]!!.value)
                g[b + 12] != null && g[b + 13] != null -> appendLink(out, g[b + 12]!!.value, g[b + 13]!!.value)
                g[b + 14] != null -> appendStyled(out, g[b + 14]!!.value, Typeface.ITALIC)
                g[b + 15] != null -> appendStyled(out, g[b + 15]!!.value, Typeface.ITALIC)
                g[b + 16] != null && g[b + 17] != null -> appendImage(out, g[b + 16]!!.value, g[b + 17]!!.value)
                g[b + 18] != null -> appendAutolink(out, g[b + 18]!!.value)
                g[b + 19] != null -> out.append('\n')
                g[b + 20] != null -> appendEmoji(out, g[b + 20]!!.value, match.value)
                g[b + 21] != null -> appendStyled(out, g[b + 21]!!.value, Typeface.BOLD)
                g[b + 22] != null -> appendStyled(out, g[b + 22]!!.value, Typeface.ITALIC)
                g[b + 23] != null -> appendStrike(out, g[b + 23]!!.value)
                g[b + 24] != null -> appendUnderline(out, g[b + 24]!!.value)
                g[b + 25] != null -> appendCode(out, g[b + 25]!!.value)
                g[b + 26] != null -> appendKeycap(out, g[b + 26]!!.value)
                g[b + 27] != null -> appendHighlight(out, g[b + 27]!!.value)
                g[b + 28] != null -> appendScript(out, g[b + 28]!!.value, superscript = false)
                g[b + 29] != null -> appendScript(out, g[b + 29]!!.value, superscript = true)
                g[b + 30] != null && g[b + 31] != null -> appendLink(out, g[b + 31]!!.value, g[b + 30]!!.value)
                g[b + 32] != null -> appendHtmlImage(out, g[b + 32]!!.value)
                g[b + 33] != null -> {} // وسم HTML غلافي (p/div/center/span...) يُسقَط بصمت بدل ظهوره حرفياً
                g[b + 34] != null -> appendRefLink(out, g[b + 34]!!.value, g[b + 35]?.value.orEmpty(), match.value)
            }
            idx = match.range.last + 1
        }
        if (idx < text.length) out.append(text.substring(idx))
    }

    /** أشهر رموز `:emoji:` بأسلوب GitHub → رمزها الفعلي (رمز غير مدرَج يبقى نصاً حرفياً كما كُتب). */
    private val EMOJI_SHORTCODES: Map<String, String> = mapOf(
        "rocket" to "\uD83D\uDE80",
        "sparkles" to "\u2728",
        "fire" to "\uD83D\uDD25",
        "tada" to "\uD83C\uDF89",
        "warning" to "\u26A0\uFE0F",
        "white_check_mark" to "\u2705",
        "heavy_check_mark" to "\u2714\uFE0F",
        "x" to "\u274C",
        "star" to "\u2B50",
        "bulb" to "\uD83D\uDCA1",
        "book" to "\uD83D\uDCD6",
        "books" to "\uD83D\uDCDA",
        "zap" to "\u26A1",
        "bug" to "\uD83D\uDC1B",
        "wrench" to "\uD83D\uDD27",
        "hammer" to "\uD83D\uDD28",
        "package" to "\uD83D\uDCE6",
        "lock" to "\uD83D\uDD12",
        "key" to "\uD83D\uDD11",
        "link" to "\uD83D\uDD17",
        "heart" to "\u2764\uFE0F",
        "thumbsup" to "\uD83D\uDC4D",
        "+1" to "\uD83D\uDC4D",
        "eyes" to "\uD83D\uDC40",
        "memo" to "\uD83D\uDCDD",
        "pushpin" to "\uD83D\uDCCC",
        "bell" to "\uD83D\uDD14",
        "gear" to "\u2699\uFE0F",
        "construction" to "\uD83D\uDEA7",
        "checkered_flag" to "\uD83C\uDFC1",
        "globe_with_meridians" to "\uD83C\uDF10",
        "computer" to "\uD83D\uDCBB",
        "iphone" to "\uD83D\uDCF1",
        "art" to "\uD83C\uDFA8",
        "mag" to "\uD83D\uDD0D",
        "smile" to "\uD83D\uDE04",
        "point_right" to "\uD83D\uDC49",
        "trophy" to "\uD83C\uDFC6",
        "arrow_right" to "\u27A1\uFE0F",
        "information_source" to "\u2139\uFE0F",
        "question" to "\u2753",
        "exclamation" to "\u2757",
        "no_entry_sign" to "\uD83D\uDEAB",
        "shield" to "\uD83D\uDEE1\uFE0F",
        "recycle" to "\u267B\uFE0F",
        "hourglass" to "\u231B"
    )

    private fun appendEmoji(out: SpannableStringBuilder, name: String, original: String) {
        out.append(EMOJI_SHORTCODES[name.lowercase()] ?: original)
    }

    /** رابط تلقائي `<https://...>` — نفس شكل [appendLink] بلا سهم، والنص هو الرابط نفسه. */
    private fun appendAutolink(out: SpannableStringBuilder, url: String) {
        val start = out.length
        out.append(url)
        val end = out.length
        out.setSpan(LinkButtonClickSpan(url), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_LINK), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** النص المرئي لسطر Markdown سطري بلا رموز التنسيق (لعرض خلايا الجدول النصّي الأحادي المسافة). */
    private fun plainInlineText(md: String): String {
        val tmp = SpannableStringBuilder()
        appendInline(tmp, md)
        return tmp.toString().replace('\n', ' ')
    }

    private fun appendUnderline(out: SpannableStringBuilder, value: String) {
        val start = out.length
        appendInline(out, value)
        out.setSpan(UnderlineSpan(), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** `<sub>`/`<sup>`: أسفل/أعلى السطر بحجم مصغَّر. */
    private fun appendScript(out: SpannableStringBuilder, value: String, superscript: Boolean) {
        val start = out.length
        appendInline(out, value)
        val end = out.length
        out.setSpan(if (superscript) SuperscriptSpan() else SubscriptSpan(), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.75f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** `<kbd>Ctrl</kbd>` → مفتاح لوحة مفاتيح بإطار مدوَّر وخط أحادي المسافة. */
    private fun appendKeycap(out: SpannableStringBuilder, value: String) {
        val start = out.length
        out.append(value)
        val end = out.length
        out.setSpan(
            StickerSpan(
                COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT,
                cornerRadius = 8f, paddingH = 8f, paddingV = 2f,
                strokeColor = COLOR_CARD_BORDER, strokeWidth = 2f
            ),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.85f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private val htmlSrcRegex = Regex("(?i)\\bsrc\\s*=\\s*[\"']([^\"']+)[\"']")
    private val htmlAltRegex = Regex("(?i)\\balt\\s*=\\s*[\"']([^\"']*)[\"']")

    /** `<img src="..." alt="...">` → نفس معالجة `![alt](src)` (شارات shields.io تُرسَم محلياً). */
    private fun appendHtmlImage(out: SpannableStringBuilder, tag: String) {
        val src = htmlSrcRegex.find(tag)?.groupValues?.get(1) ?: return
        val alt = htmlAltRegex.find(tag)?.groupValues?.get(1).orEmpty()
        if (parseShieldsBadgeUrl(src) != null) appendShieldsBadge(out, alt, src) else appendImage(out, alt, src)
    }

    /** تعريفات الروابط المرجعية `[id]: https://...` في المستند الحالي (تُجمَع مرة واحدة في [toSpannable]). */
    private var currentLinkRefs: Map<String, String> = emptyMap()

    private val linkRefDefRegex = Regex("^\\s{0,3}\\[([\\p{L}\\p{N}][^\\]]*)\\]:\\s+<?([^\\s>]+)>?.*$")

    private fun collectLinkRefs(markdown: String): Map<String, String> {
        val refs = HashMap<String, String>()
        for (line in markdown.lines()) {
            val m = linkRefDefRegex.find(line) ?: continue
            refs.putIfAbsent(m.groupValues[1].trim().lowercase(), m.groupValues[2])
        }
        return refs
    }

    /** `[نص][معرّف]` أو `[نص][]` → رابط عادي إن وُجد تعريفه، وإلا يبقى النص كما كُتب. */
    private fun appendRefLink(out: SpannableStringBuilder, label: String, id: String, original: String) {
        val key = (if (id.isBlank()) label else id).trim().lowercase()
        val url = currentLinkRefs[key]
        if (url == null) out.append(original) else appendLink(out, label, url)
    }

    private val detailsSummaryRegex = Regex("(?is)<summary[^>]*>(.*?)</summary>")
    private val detailsTagRegex = Regex("(?i)</?details[^>]*>")

    private fun appendStyled(out: SpannableStringBuilder, value: String, style: Int) {
        val start = out.length
        appendInline(out, value)
        out.setSpan(StyleSpan(style), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun appendStrike(out: SpannableStringBuilder, value: String) {
        val start = out.length
        appendInline(out, value)
        val end = out.length
        out.setSpan(StrikethroughSpan(), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_STRIKE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun appendHighlight(out: SpannableStringBuilder, value: String) {
        val start = out.length
        appendInline(out, value)
        val end = out.length
        out.setSpan(BackgroundColorSpan(COLOR_HIGHLIGHT_BG), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun appendCode(out: SpannableStringBuilder, value: String) {
        val start = out.length
        // نفس إصلاح اتجاه [forceLtrPerLine] لكتل الكود الكاملة، مطبَّق هنا لأن الكود المضمَّن
        // `` `...` `` معرَّض لنفس مشكلة انعكاس الأقواس/الفواصل إن بدأ بمحرف عربي (مثال: `` `متغيّر=1` ``).
        out.append(forceLtrPerLine(value)) // محتوى الكود يبقى حرفياً دوماً، بلا تحليل تعشيش
        val end = out.length
        out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.92f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(BackgroundColorSpan(COLOR_INLINE_CODE_BG), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * رابط `[نص](رابط)` — بشكل احترافي جديد: بلا خط تحته (كان UnderlineSpan)، تشديد كامل، وسهم
     * رابط خارجي صغير ↗ بعد النص، بنفس أسلوب شارات الروابط في READMEs الاحترافية (GitHub/npm).
     * يبقى قابلاً للنقر فعلياً عبر [URLSpan] القياسي (يفتح المتصفح تلقائياً) — لا تغيير سلوكي.
     */
    private fun appendLink(out: SpannableStringBuilder, label: String, url: String) {
        val start = out.length
        appendInline(out, label)
        out.append(" \u2197")
        val end = out.length
        // LinkButtonClickSpan (ClickableSpan) بدل URLSpan: TextView.autoLinkMask (مضبوط في README) يستدعي
        // Linkify.addLinks الذي يحذف كل URLSpan موجودة قبل إعادة الربط، فكانت الروابط تفقد النقر.
        out.setSpan(LinkButtonClickSpan(url), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_LINK), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** أقصى عرض افتراضي (px) لصورة Markdown مضمَّنة حين لا يُمرَّر عرض فعلي من [applyTo] (مثال:
     *  استدعاء [toSpannable] المباشر بلا TextView) — قيمة معقولة تناسب معظم عروض الشاشات. */
    private const val DEFAULT_IMAGE_MAX_WIDTH_PX = 480

    /** عرض العرض الأقصى الفعلي الحالي (px) لصور Markdown المضمَّنة — [applyTo] يضبطه على عرض
     *  [TextView] الحقيقي قبل الاستدعاء، لتناسب الصورة الشاشة بدقّة بدل قيمة تقديرية ثابتة دوماً؛
     *  نفس أسلوب [currentBaseDir] أعلاه (حالة كائن مؤقتة، آمنة لأن كل استدعاء متزامن من UI thread). */
    private var currentImageMaxWidthPx = DEFAULT_IMAGE_MAX_WIDTH_PX

    /** وسم "تعليق" غير مرئي بذاته (لا يرسم شيئاً — [updateDrawState] فارغ عمداً) يُثبَّت فوق نص
     *  العنصر النائب لصورة بعيدة قيد التحميل، ليتمكّن [applyTo] لاحقاً من تحديد مكانها بدقّة عبر
     *  `Spannable.getSpanStart/End(this)` واستبدالها بـ[ImageSpan] حقيقي فور اكتمال التنزيل —
     *  بلا حاجة لأي قناة بيانات جانبية بين [toSpannable] و[applyTo]. */
    private class PendingImageSpan(val url: String) : CharacterStyle() {
        override fun updateDrawState(tp: TextPaint) {}
    }

    /**
     * صورة Markdown حقيقية `![نص](مسار/رابط)` — **إضافة "معاينة Markdown حقيقية"**: حتى الآن كانت
     * هذه الصياغة (لغير شارات shields.io) تسقط بصمت إلى [appendLink] عادي (نص + سهم ↗ يفتح
     * الرابط)، بلا أي صورة فعلية معروضة — أول فجوة حقيقية عن CommonMark القياسي في هذا الملف.
     * الآن:
     * 1. **مسار محلي** (مطلق، أو `file://`، أو نسبي لـ[currentBaseDir] إن مُرِّر) — يُفكّ فوراً
     *    ومتزامناً عبر [decodeLocalImage] ويُدرَج كـ[ImageSpan] حقيقي في نفس الاستدعاء، بلا أي
     *    تأخير أو حالة تحميل (نفس فلسفة `print.image` تماماً: عرض ملف موجود بالفعل على القرص).
     * 2. **رابط بعيد http(s)** — لا يمكن تنزيله متزامناً هنا (لا Thread/Context متاحين داخل
     *    [toSpannable] الخالصة، وحظرُ الشبكة على UI thread غير مقبول أصلاً)؛ يُدرَج بدلاً منه نص
     *    عنصر نائب مؤقت (أيقونة 🖼 + النص البديل) موسوماً بـ[PendingImageSpan]، يستبدله [applyTo]
     *    بصورة حقيقية فور اكتمال التنزيل غير المتزامن عبر [loadRemoteImageAsync] — التوليف الوحيد
     *    الممكن بين "بناء متزامن للنص" و"تحميل شبكي غير متزامن للصورة" دون كسر توقيع الدالة الحالي.
     * 3. **أي شيء آخر** (مسار غير قابل للحلّ محلياً بلا baseDir، مخطّط بروتوكول غريب...) — سلوك
     *    قديم بلا تغيير: زر رابط عادي عبر [appendLink].
     */
    private fun appendImage(out: SpannableStringBuilder, alt: String, url: String) {
        val trimmedUrl = url.trim()
        val local = decodeLocalImage(trimmedUrl, currentBaseDir, currentImageMaxWidthPx)
        if (local != null) {
            appendBitmap(out, local)
            return
        }
        if (trimmedUrl.startsWith("http://", ignoreCase = true) || trimmedUrl.startsWith("https://", ignoreCase = true)) {
            val start = out.length
            out.append("\uD83D\uDDBC ") // 🖼 — يوحي بصورة قيد التحميل قبل استبدالها فعلياً
            out.append(alt.ifBlank { "\u0635\u0648\u0631\u0629" }) // "صورة"
            val end = out.length
            out.setSpan(ForegroundColorSpan(COLOR_LINK), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(StyleSpan(Typeface.ITALIC), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(PendingImageSpan(trimmedUrl), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            return
        }
        // لا صورة محلية ولا رابط بعيد صالح -- نفس السلوك القديم قبل هذه الإضافة تماماً.
        appendLink(out, alt.ifBlank { url }, url)
    }

    /** يُدرِج [bitmap] كـ[ImageSpan] حقيقي واحد، بأبعاد مُصغَّرة لتُناسب [currentImageMaxWidthPx]
     *  مع الحفاظ على نسبة العرض/الارتفاع الأصلية (بلا تكبير أبداً إن كانت الصورة أصغر أصلاً). */
    private fun appendBitmap(out: SpannableStringBuilder, bitmap: Bitmap) {
        val maxW = currentImageMaxWidthPx
        val (w, h) = if (bitmap.width > maxW) {
            val scale = maxW.toFloat() / bitmap.width
            maxW to (bitmap.height * scale).toInt().coerceAtLeast(1)
        } else {
            bitmap.width to bitmap.height
        }
        val drawable = BitmapDrawable(null, bitmap).apply { setBounds(0, 0, w, h) }
        val start = out.length
        out.append('\uFFFC') // Object Replacement Character -- المحرف القياسي الذي يستبدله ImageSpan بصرياً
        out.setSpan(ImageSpan(drawable, ImageSpan.ALIGN_BOTTOM), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * يفكّ صورة محلية إن أمكن حلّ [url] كملف موجود فعلاً على القرص، بتصغير ذكي (inSampleSize) بدل
     * تحميلها بالحجم الكامل في الذاكرة دوماً — نفس أسلوب [RinJobAdapter.decodeSampledBitmap]
     * (`print.image`) تماماً. يرجع null بهدوء (بلا استثناء) لأي رابط بعيد أو مسار غير موجود، حتى
     * يتابع [appendImage] للمسار البعيد/الاحتياطي التالي بأمان.
     */
    private fun decodeLocalImage(url: String, baseDir: String?, maxWidthPx: Int): Bitmap? {
        if (url.startsWith("http://", ignoreCase = true) || url.startsWith("https://", ignoreCase = true)) return null
        val path = when {
            url.startsWith("file://") -> Uri.parse(url).path
            url.startsWith("/") -> url
            !baseDir.isNullOrBlank() -> File(baseDir, url).absolutePath
            else -> null
        } ?: return null
        val file = File(path)
        if (!file.exists() || !file.isFile) return null
        return try {
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.absolutePath, bounds)
            if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
            var sample = 1
            while (bounds.outWidth / (sample * 2) >= maxWidthPx) sample *= 2
            BitmapFactory.decodeFile(file.absolutePath, BitmapFactory.Options().apply { inSampleSize = sample })
        } catch (t: Throwable) {
            null
        }
    }

    /**
     * يُنزِّل صورة [url] البعيدة على thread خلفي (مع مهلة 8 ثوانٍ)، ثم يستدعي [onLoaded] على UI
     * thread دوماً (عبر [mainHandler]) بالـ[Bitmap] الناتج أو null عند أي فشل (رابط ميت، ليس صورة،
     * انقطاع شبكة...) — لا يرمي أبداً. النتائج الناجحة تُخزَّن في [remoteImageCache] لتفادي إعادة
     * التنزيل عند كل إعادة بناء لنفس الشاشة (كل نقرة على قسم قابل للطي).
     */
    private fun loadRemoteImageAsync(url: String, onLoaded: (Bitmap?) -> Unit) {
        remoteImageCache[url]?.let { mainHandler.post { onLoaded(it) }; return }
        remoteImageExecutor.execute {
            val bitmap = try {
                (URL(url).openConnection() as HttpURLConnection).apply {
                    connectTimeout = 8000
                    readTimeout = 8000
                    instanceFollowRedirects = true
                }.inputStream.use { BitmapFactory.decodeStream(it) }
            } catch (t: Throwable) {
                null
            }
            if (bitmap != null) remoteImageCache[url] = bitmap
            mainHandler.post { onLoaded(bitmap) }
        }
    }

    /** يطابق رابط شارة shields.io: `https://img.shields.io/badge/<مقاطع>` — امتداد `.svg`/`.png`
     *  ومعاملات الاستعلام (`?style=...`) اختياريان ويُتجاهَلان (انظر [parseShieldsBadgeUrl]). */
    private val shieldsBadgeUrlRegex = Regex(
        "^https?://img\\.shields\\.io/badge/(.+?)(?:\\.svg|\\.png)?(?:\\?.*)?$",
        RegexOption.IGNORE_CASE
    )

    /** أسماء ألوان shields.io الشائعة → قيمتها السداسية القياسية (من توثيق/مصدر shields.io نفسه)
     *  — لون غير مدرَج يسقط بهدوء إلى رمادي محايد عبر [shieldsColorToInt]. */
    private val SHIELDS_COLOR_NAMES: Map<String, String> = mapOf(
        "brightgreen" to "4C1", "success" to "4C1",
        "green" to "97CA00",
        "yellowgreen" to "A4A61D",
        "yellow" to "DFB317",
        "orange" to "FE7D37", "important" to "FE7D37",
        "red" to "E05D44", "critical" to "E05D44",
        "blue" to "007EC6", "informational" to "007EC6",
        "lightgrey" to "9F9F9F", "lightgray" to "9F9F9F", "inactive" to "9F9F9F",
        "grey" to "555555", "gray" to "555555",
        "black" to "000000", "white" to "FFFFFF",
        "blueviolet" to "8833D7", "purple" to "9B59B6", "pink" to "FF69B4"
    )

    /** يحوّل مقطع لون شارة (اسم مثل `brightgreen`، أو سداسي عشري بلا/مع `#`) إلى لون ARGB كامل
     *  الشفافية — رمادي شارات shields.io القياسي (`#9F9F9F`) عند عدم التعرّف على المقطع، فلا
     *  تتعطّل الشارة أبداً بسبب اسم لون غير مدعوم. */
    private fun shieldsColorToInt(raw: String): Int {
        val key = raw.trim().lowercase()
        var hex = SHIELDS_COLOR_NAMES[key] ?: run {
            val cleaned = key.removePrefix("#")
            when {
                cleaned.matches(Regex("^[0-9a-f]{6}$")) -> cleaned
                cleaned.matches(Regex("^[0-9a-f]{3}$")) -> cleaned.map { "$it$it" }.joinToString("")
                else -> "9F9F9F"
            }
        }
        if (hex.length == 3) hex = hex.map { "$it$it" }.joinToString("")
        return try {
            (0xFF000000.toInt()) or Integer.parseInt(hex, 16)
        } catch (t: Throwable) {
            0xFF9F9F9F.toInt()
        }
    }

    /**
     * يحلّل رابط شارة shields.io إلى (تسمية اختيارية، رسالة، لون) — الصياغة الرسمية تفصل
     * المقاطع بـ`-`، مع `--` للشرطة الحرفية داخل مقطع، و`_`/ترميز URL للمسافة:
     * - 3 مقاطع فأكثر `LABEL-MESSAGE-COLOR` (مثال: `build-passing-brightgreen`) → تسمية + رسالة
     *   ملوَّنة، بأسلوب شارة shields.io ثنائية اللون الحقيقي (جزء رمادي داكن + جزء ملوَّن).
     * - مقطعان `MESSAGE-COLOR` (مثال: `passing-brightgreen`) → شارة أحادية اللون بلا تسمية.
     * - مقطع واحد فقط → رسالة بلا تسمية ولا لون محدَّد (رمادي افتراضي).
     * يعيد null إن لم يطابق الرابط صياغة شارة shields.io أصلاً — عندها [appendShieldsBadge] يسقط
     * بهدوء إلى زر رابط عادي بدل عنصر مكسور.
     */
    private fun parseShieldsBadgeUrl(url: String): Triple<String?, String, Int>? {
        val m = shieldsBadgeUrlRegex.find(url.trim()) ?: return null
        val placeholder = "\u0001" // يحمي `--` (شرطة حرفية) من التقسيم قبل فكّ ترميزها لاحقاً
        val segments = m.groupValues[1].replace("--", placeholder).split("-").map { seg ->
            val decoded = try {
                java.net.URLDecoder.decode(seg.replace(placeholder, "-").replace("_", " "), "UTF-8")
            } catch (t: Throwable) {
                seg.replace(placeholder, "-").replace("_", " ")
            }
            decoded
        }
        return when {
            segments.isEmpty() || segments[0].isBlank() -> null
            segments.size == 1 -> Triple(null, segments[0], shieldsColorToInt("lightgrey"))
            segments.size == 2 -> Triple(null, segments[0], shieldsColorToInt(segments[1]))
            else -> Triple(
                segments[0],
                segments.subList(1, segments.size - 1).joinToString("-"),
                shieldsColorToInt(segments.last())
            )
        }
    }

    /**
     * شارة `![نص](https://img.shields.io/badge/...)` — تُرسَم محلياً عبر [ShieldsBadgeSpan] بنفس
     * الشكل البصري القياسي لشارات shields.io الحقيقية (جزء تسمية رمادي داكن مُلاصِق لجزء رسالة
     * ملوَّن)، لا بجلب صورة حقيقية عبر الشبكة — يبقى العرض فورياً وبلا اتصال بالإنترنت. رابط لا
     * يطابق صياغة `img.shields.io/badge/...` يسقط بهدوء إلى زر رابط عادي بنص [alt] (أو الرابط
     * نفسه إن كان [alt] فارغاً) عبر [appendLink]، فلا يُفقَد المحتوى صمتاً برابط شارة غير مدعوم.
     */
    private fun appendShieldsBadge(out: SpannableStringBuilder, alt: String, url: String) {
        val parsed = parseShieldsBadgeUrl(url)
        if (parsed == null) {
            appendLink(out, alt.ifBlank { url }, url)
            return
        }
        val (label, message, messageColor) = parsed
        val labelBg = 0xFF555555.toInt() // رمادي داكن قياسي لجزء التسمية في شارات shields.io الحقيقية
        val messageFg = contrastingTextColor(messageColor)
        val start = out.length
        out.append(if (label != null) "$label $message" else message)
        val end = out.length
        out.setSpan(
            ShieldsBadgeSpan(label, message, labelBg, messageColor, messageFg),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        out.setSpan(RelativeSizeSpan(0.8f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }


    /** يعرض «ملصقاً» ملوّناً بصياغة `[لصق:نص|متغير]` — انظر [STICKER_VARIANTS]. متغير غير
     * معروف يسقط بهدوء إلى accent، فلا يتعطّل العرض أبداً بسبب خطأ إملائي بسيط في الاسم.
     */
    private fun appendSticker(out: SpannableStringBuilder, raw: String) {
        val parts = raw.split("|", limit = 2)
        val label = parts[0].trim()
        val variantKey = parts.getOrNull(1)?.trim()?.lowercase().orEmpty()
        val (bg, fg) = STICKER_VARIANTS[variantKey] ?: STICKER_DEFAULT
        val start = out.length
        out.append(label)
        val end = out.length
        out.setSpan(StickerSpan(bg, fg), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.86f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** يلتقط مقاطع `مفتاح=(قيمة)` داخل صياغة `[* ... *]` — انظر [appendMetaBadge]. */
    private val metaBadgeKeyValueRegex = Regex("^(\\w+)\\s*=\\s*\\(([^)]*)\\)$")

    /** لون سداسي عشري صريح `#RGB` أو `#RRGGBB` — يُقبَل في أي جزء من `[* ... *]` (تسمية مجرَّدة
     *  أولى، قيمة ثانية مجرَّدة، أو `color=(#hex)`) — انظر [appendMetaBadge]. */
    private val hexColorRegex = Regex("^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{3})$")

    /** يحوّل نصاً كـ`#7C5CFF`/`#7CF` إلى (لون ARGB كامل الشفافية، نص العرض الموحَّد `#RRGGBB`)،
     *  أو null إن لم يطابق صيغة لون سداسي عشري صحيحة — يسقط بهدوء إلى المعالجة العادية حينها. */
    private fun parseHexColor(raw: String): Pair<Int, String>? {
        val m = hexColorRegex.find(raw.trim()) ?: return null
        var hex = m.groupValues[1]
        if (hex.length == 3) hex = hex.map { "$it$it" }.joinToString("")
        return try {
            val color = (0xFF000000.toInt()) or Integer.parseInt(hex, 16)
            color to "#${hex.uppercase()}"
        } catch (t: Throwable) {
            null
        }
    }

    /** نص/خلفية بيضاء أو داكنة (بحسب سطوع [bgColor]) — يضمن مقروئية زر برابط بلون مخصَّص
     *  [appendMetaBadge] مهما كان اللون المُدخَل فاتحاً أو داكناً. */
    private fun contrastingTextColor(bgColor: Int): Int {
        val r = (bgColor shr 16) and 0xFF
        val g = (bgColor shr 8) and 0xFF
        val b = bgColor and 0xFF
        val luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0
        return if (luminance > 0.6) 0xFF15161A.toInt() else 0xFFFFFFFF.toInt()
    }

    /** يختصر عدداً كبيراً لصيغة عرض مقروءة (K/M/B) لشارة `downloads=(N)` — مثال: 15420 → "15.4K"،
     *  2300000 → "2.3M". أرقام أقل من 1000 تُعرَض كاملة بلا اختصار. */
    private fun formatCompactCount(value: Long): String {
        val absValue = kotlin.math.abs(value)
        val (divisor, suffix) = when {
            absValue >= 1_000_000_000L -> 1_000_000_000.0 to "B"
            absValue >= 1_000_000L -> 1_000_000.0 to "M"
            absValue >= 1_000L -> 1_000.0 to "K"
            else -> return value.toString()
        }
        val scaled = value / divisor
        val rounded = Math.round(scaled * 10) / 10.0
        val text = if (rounded == Math.floor(rounded)) rounded.toLong().toString() else String.format("%.1f", rounded)
        return "$text$suffix"
    }

    /** رموز أيقونات مصغَّرة لصياغة `icon=(اسم)` — مطابقة نصّية بالاحتواء على اسم الملف/المعرِّف
     *  بلا امتداد (فـ`icon(pin.png)` أو `icon(ic_pin)` كلاهما يطابق "pin")، تسقط بهدوء لبلا أيقونة
     *  إن لم يُعرَف الاسم، فلا يتعطّل عرض الزر أبداً بسبب اسم أيقونة غير مدعوم. */
    private val ICON_GLYPHS: List<Pair<String, String>> = listOf(
        "pin" to "\uD83D\uDCCC",       // 📌
        "install" to "\u2B07\uFE0F",   // ⬇️
        "download" to "\u2B07\uFE0F",  // ⬇️
        "link" to "\uD83D\uDD17",      // 🔗
        "star" to "\u2605",            // ★
        "check" to "\u2713",           // ✓
        "play" to "\u25B6",            // ▶
        "web" to "\uD83C\uDF10",       // 🌐
        "arrow" to "\u2192",           // →
        "github" to "\u2318",
        "copy" to "\u29C9",
        "docs" to "\uD83D\uDCC4",
        "home" to "\uD83C\uDFE0",
        "mail" to "\u2709",
        "settings" to "\u2699",
        "bug" to "\uD83D\uDC1B",
        "heart" to "\u2665",
        "tag" to "\uD83C\uDFF7",
        "rocket" to "\uD83D\uDE80",
        "lock" to "\uD83D\uDD12",
        "open" to "\u2197",
        "share" to "\u2934"
    )

    private fun iconGlyphFor(rawIcon: String?): String? {
        if (rawIcon.isNullOrBlank()) return null
        val base = rawIcon.substringAfterLast('/').substringBeforeLast('.').lowercase()
        return ICON_GLYPHS.firstOrNull { (key, _) -> base.contains(key) }?.second
    }

    /**
     * "شارة/زر وصفي" عام عبر صياغة `[* ... *]` بأجزاء مفصولة بـ`/`: الجزء الأول دائماً هو
     * النص الظاهر (أو لون سداسي عشري مجرَّد بلا تسمية — انظر صياغة اللون أدناه)، وما بعده إمّا
     * `مفتاح=(قيمة)` (المفاتيح المدعومة: `link`، `icon`، `color`) أو قيمة مجرَّدة (رقم إصدار، أو
     * لون سداسي عشري). أربع صيغ عملية:
     * - `[*الإصدار/1*]` → شارة إصدار ثنائية اللون (تسمية خافتة + قيمة بارزة بلون الهوية) داخل
     *   حبّة واحدة، عبر [BadgeTwoToneSpan] — بلا أي تفاعل (عرض فقط).
     * - `[*زر تثبيت/link=(رابط)*]` → زر حقيقي قابل للنقر (حبّة مملوءة بلون الهوية + نص أبيض
     *   بارز) يفتح [رابط] في المتصفح عند الضغط عبر [LinkButtonClickSpan] — مثالي لأزرار
     *   "تثبيت"/"تحميل"/"زيارة الموقع" داخل README.
     * - `[*Pin/link=(رابط)/icon=(اسم)*]` → نفس زر الرابط أعلاه، مع أيقونة مصغَّرة (عبر
     *   [iconGlyphFor]) قبل النص مباشرة — مناسب لأزرار "تثبيت"/"تنزيل" المصحوبة برمز.
     * - **لون مدمَج (`color`)** — ثلاث طرق مكافئة كلها تُفعِّل نفس السلوك عبر [ColorSwatchSpan]
     *   أو تُلوِّن الزر/الشارة الحاليين مباشرة، بدل عنصر منفصل قائم بذاته:
     *   • `[*#7C5CFF*]` — نقطة لون حقيقية + رمزه السداسي، بلا تسمية.
     *   • `[*Accent/#7C5CFF*]` — نفس النقطة، مع تسمية نصية قبلها.
     *   • `[*تحميل/link=(رابط)/color=(#22C88E)*]` — الزر نفسه أعلاه لكن بخلفية [color] المُحدَّد
     *     بدل لون الهوية الافتراضي (مع اختيار نص أبيض/داكن تلقائياً عبر [contrastingTextColor]
     *     لضمان التباين مهما كان اللون).
     *   • `[*الإصدار/2/color=(#FFA95C)*]` — شارة الإصدار نفسها أعلاه، لكن بقيمة ملوَّنة بـ[color]
     *     بدل لون الهوية الافتراضي.
     * - **وسام الهرم الهيكلي (`hierarchy`)** — `[*بنية الصفحة/hierarchy=(4)*]` → وسام بأيقونة هرم
     *   حقيقية مرسومة (قمّة ضيّقة أعلى إلى قاعدة عريضة أسفل، بعدد طبقات = القيمة المُعطاة، كل
     *   طبقة بلون عمق مختلف عبر [pyramidTierColor]) عبر [PyramidBadgeSpan] — يمثّل بصرياً عدد
     *   مستويات تعشيش عناصر صفحة (مثال: `Scaffold ← TopBar/Column/BottomBar ← عناصرها الداخلية`
     *   = 3 مستويات). `hierarchy=(N)` مقبولة أيضاً باسم `pyramid=(N)`؛ N تُحصَر تلقائياً بين 1 و6
     *   طبقات (سقف معقول للرسم داخل حبّة نصّية واحدة). بلا تسمية منفصلة، يُستخدَم عنوان افتراضي
     *   "البنية الهيكلية". يقبل أيضاً `palette=(forest|harbor|lagoon)` (انظر [NAMED_PALETTES])
     *   لرسم الطبقات بدرجات حقيقية من تلك اللوحة بدل الهوية الافتراضية الثلاثية.
     * - **جديد (تطوير احترافي لـ`[* *]`):**
 *   • إصلاح: `link=(https://a.b/c)` لم يعد يتمزّق عند `/` داخل الأقواس ([splitMetaParts]).
 *   • `style=(solid|soft|outline)` و`size=(sm|md|lg)` لأزرار `link`/`copy` ([appendActionButton]).
 *   • `color=(green)` بأسماء الألوان (accent/green/gold/danger/info/like + red/blue/orange...) لا سداسي فقط.
 *   • `copy=(نص)` زر ينسخ للحافظة، `status=(ok|warn|error|info|beta|new|stable|deprecated)` شارة حالة،
 *     `rating=(4.5)` نجوم، `progress=(70)` شريط تقدّم حقيقي ([ProgressBadgeSpan]).
 *   • أيقونات إضافية: copy/docs/home/mail/settings/bug/heart/tag/rocket/lock/open/share.
 * - **جديد: Marquee** — `[*نص/marquee*]` أو `marquee=(60)`: شريط نص يتحرّك بلا توقّف (انظر [appendMarquee]؛
 *   `width`/`direction`/`style`/`color`/`icon`/`link` اختيارية).
 * لا رابط ولا قيمة مجرَّدة ولا لون ولا هرم (مثال: `[*جديد*]` وحدها) → يسقط بهدوء إلى ستيكر
     * عادي بلون الهوية الافتراضي عبر [appendSticker]، فلا يُفقَد المحتوى صمتاً بسبب صياغة ناقصة.
     */
    private fun appendMetaBadge(out: SpannableStringBuilder, raw: String) {
        val parts = splitMetaParts(raw)
        if (parts.isEmpty()) return
        // `[*Page_background/#hex*]` مُعالَجة حصرياً عبر [extractPageBackground] على مستوى السطر
        // في [toSpannable] (و[pageBackgroundLineRegex] يمنع وصولها لهنا أصلاً في الاستخدام العادي،
        // سطراً مستقلاً)؛ هذا الحارس تحسُّب إضافي لبقاء الصياغة بلا أي أثر مرئي حتى لو استُخدمت
        // مضمَّنة وسط سطر آخر بدل أن تُعرَض خطأً كشارة/نقطة لون عادية.
        if (parts[0].replace(' ', '_').equals("page_background", ignoreCase = true)) return
        var label = parts[0]

        var linkUrl: String? = null
        var iconRaw: String? = null
        var plainValue: String? = null
        var color: Pair<Int, String>? = null
        var hierarchyLevels: Int? = null
        var hierarchyPalette: IntArray? = null
        var downloadsCount: Long? = null
        var isDownloading = false
        var copyText: String? = null
        var styleKey: String? = null
        var sizeKey: String? = null
        var statusKey: String? = null
        var ratingValue: Double? = null
        var progressValue: Int? = null
        var marquee = false
        var marqueeSpeedDp: Float? = null
        var marqueeWidthDp: Float? = null
        var directionKey: String? = null
        var paymentRaw: String? = null

        // التسمية الأولى نفسها قد تكون لوناً مجرَّداً بلا نص (`[*#7C5CFF*]`) — عندها لا توجد
        // تسمية نصية منفصلة أصلاً.
        parseHexColor(label)?.let { color = it; label = "" }

        for (part in parts.drop(1)) {
            // `؛: … :؛` — دفع/تبرّع مضمَّن كجزء من الشارة (انظر appendPayment و splitMetaParts).
            if (part.length >= 5 && part.startsWith("\u061B:") && part.endsWith(":\u061B")) {
                paymentRaw = part.substring(2, part.length - 2)
                continue
            }
            val m = metaBadgeKeyValueRegex.find(part)
            if (m != null) {
                when (m.groupValues[1].lowercase()) {
                    "link" -> linkUrl = m.groupValues[2].trim()
                    "icon" -> iconRaw = m.groupValues[2].trim()
                    "color" -> parseColorValue(m.groupValues[2].trim())?.let { color = it }
                    "copy" -> copyText = m.groupValues[2].trim()
                    "pay", "donate" -> paymentRaw = m.groupValues[2].trim()
                    "marquee" -> { marquee = true; m.groupValues[2].trim().toFloatOrNull()?.let { marqueeSpeedDp = it } }
                    "speed" -> m.groupValues[2].trim().toFloatOrNull()?.let { marqueeSpeedDp = it }
                    "width" -> marqueeWidthDp = m.groupValues[2].trim().toFloatOrNull()
                    "direction", "dir" -> directionKey = m.groupValues[2].trim()
                    "style" -> styleKey = m.groupValues[2].trim()
                    "size" -> sizeKey = m.groupValues[2].trim()
                    "status" -> statusKey = m.groupValues[2].trim()
                    "rating" -> ratingValue = m.groupValues[2].trim().replace(',', '.').toDoubleOrNull()
                    "progress" -> progressValue = m.groupValues[2].trim().trimEnd('%').toDoubleOrNull()?.toInt()?.coerceIn(0, 100)
                    "hierarchy", "pyramid" -> hierarchyLevels = m.groupValues[2].trim().toIntOrNull()?.coerceIn(1, 6)
                    "palette" -> hierarchyPalette = NAMED_PALETTES[m.groupValues[2].trim().lowercase()]
                    "downloads" -> downloadsCount = m.groupValues[2].trim().replace(",", "").toLongOrNull()
                    "downloading" -> isDownloading = m.groupValues[2].trim().equals("true", ignoreCase = true)
                    "state" -> if (m.groupValues[2].trim().equals("downloading", ignoreCase = true)) isDownloading = true
                }
            } else {
                val asColor = parseHexColor(part)
                when {
                    part.equals("marquee", ignoreCase = true) -> marquee = true
                    asColor != null -> color = asColor
                    plainValue == null -> plainValue = part
                }
            }
        }

        when {
            // شريط نص متحرّك (Marquee): `[*عرض خاص — اطلب الآن/marquee*]` أو `marquee=(60)` (السرعة dp/ثانية).
            marquee && label.isNotBlank() -> appendMarquee(
                out, label, color, iconRaw, styleKey, marqueeSpeedDp, marqueeWidthDp, directionKey,
                linkUrl?.let { LinkButtonClickSpan(it) }
            )
            // دفع/تبرّع: `[*ادعمنا/؛:paypal|ahmad|5 USD:؛*]` أو `[*تبرّع/donate=(patreon|rin)*]`.
            paymentRaw != null -> appendPayment(out, label, paymentRaw!!, color, styleKey, sizeKey)
            linkUrl != null -> {
                appendActionButton(out, label, color, iconRaw, styleKey, sizeKey, LinkButtonClickSpan(linkUrl!!))
            }
            // زر نسخ نص: `[*نسخ الأمر/copy=(pip install x)*]` — ينسخ النص للحافظة عند النقر.
            copyText != null -> {
                appendActionButton(
                    out, label, color, iconRaw ?: "copy", styleKey, sizeKey,
                    CopyCodeSpan(copyText!!, "\u062A\u0645 \u0627\u0644\u0646\u0633\u062E"), "\u0646\u0633\u062E"
                )
            }
            // شارة حالة: `[*الخدمة/status=(ok)*]` → ● الخدمة: يعمل (ok/warn/error/info/beta/new/stable/deprecated).
            statusKey != null -> {
                val (stColor, stLabel) = STATUS_STYLES[statusKey!!.lowercase()] ?: (COLOR_BULLET to statusKey!!)
                val base = color?.first ?: stColor
                val shown = if (label.isNotBlank()) "$label: $stLabel" else stLabel
                val start = out.length
                out.append("\u25CF $shown")
                val end = out.length
                out.setSpan(
                    StickerSpan(tintedBackground(base, 0x26), softTextColor(base)),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            // شارة تقييم: `[*التقييم/rating=(4.5)*]` → التقييم ★★★★★ 4.5 (نجوم مُقرَّبة لأقرب عدد صحيح، من 5).
            ratingValue != null -> {
                val v = ratingValue!!.coerceIn(0.0, 5.0)
                val full = Math.round(v).toInt().coerceIn(0, 5)
                val stars = "\u2605".repeat(full) + "\u2606".repeat(5 - full)
                val valueText = if (v == Math.floor(v)) v.toInt().toString() else String.format(java.util.Locale.US, "%.1f", v)
                val start = out.length
                out.append(if (label.isNotBlank()) "$label  $stars $valueText" else "$stars $valueText")
                val end = out.length
                out.setSpan(
                    StickerSpan(tintedBackground(0xFFFFC94D.toInt(), 0x33), COLOR_SYNTAX_BUILTIN),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            // شريط تقدّم حقيقي: `[*الإنجاز/progress=(70)*]` → التسمية + شريط مملوء 70% + النسبة.
            progressValue != null -> {
                val pct = progressValue!!
                val start = out.length
                out.append(if (label.isNotBlank()) "$label $pct%" else "$pct%")
                val end = out.length
                out.setSpan(
                    ProgressBadgeSpan(
                        label, pct, COLOR_INLINE_CODE_BG, COLOR_H_DIM,
                        COLOR_TABLE_BORDER, color?.first ?: COLOR_BULLET, COLOR_CODE_TEXT
                    ),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            hierarchyLevels != null -> {
                val shownLabel = label.ifBlank { "\u0627\u0644\u0628\u0646\u064A\u0629 \u0627\u0644\u0647\u064A\u0643\u0644\u064A\u0629" } // "البنية الهيكلية"
                val start = out.length
                out.append("$shownLabel  $hierarchyLevels")
                val end = out.length
                out.setSpan(
                    PyramidBadgeSpan(
                        hierarchyLevels, COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT,
                        hierarchyPalette ?: BULLET_DEPTH_COLORS
                    ),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            // شارة "عدد التنزيلات": `[*تنزيلات/downloads=(15420)*]` → ⬇ تنزيلات 15.4K، عرض فقط
            // بلا أي تفاعل — رقم مختصَر تلقائياً عبر [formatCompactCount] (K/M/B).
            downloadsCount != null -> {
                val bg = color?.first ?: COLOR_INLINE_CODE_BG
                val fg = COLOR_CODE_TEXT
                val shownLabel = label.ifBlank { "\u062A\u0646\u0632\u064A\u0644\u0627\u062A" } // "تنزيلات"
                val start = out.length
                out.append("\u2B07 $shownLabel ${formatCompactCount(downloadsCount!!)}")
                val end = out.length
                out.setSpan(StickerSpan(bg, fg), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            // شارة "جاري التحميل": `[*تحميل/downloading=(true)*]` أو `[*تحميل/state=(downloading)*]`
            // → شارة ملوَّنة بهوية التطبيق تشير لتنزيل قيد التقدُّم (عرض ثابت، بلا انيميشن حقيقي).
            isDownloading -> {
                val bg = color?.first ?: COLOR_BULLET
                val fg = if (color != null) contrastingTextColor(bg) else 0xFFFFFFFF.toInt()
                val shownLabel = label.ifBlank { "\u062C\u0627\u0631\u064A \u0627\u0644\u062A\u062D\u0645\u064A\u0644" } // "جاري التحميل"
                val start = out.length
                out.append("\u21BB $shownLabel\u2026")
                val end = out.length
                out.setSpan(StickerSpan(bg, fg), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.86f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            plainValue != null -> {
                val valueColor = color?.first ?: COLOR_BULLET
                val start = out.length
                out.append("$label $plainValue")
                val end = out.length
                out.setSpan(
                    BadgeTwoToneSpan(label, plainValue, COLOR_INLINE_CODE_BG, COLOR_H_DIM, valueColor),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }
            color != null -> {
                val (swatchColor, hexDisplay) = color!!
                val text = if (label.isNotBlank()) "$label  $hexDisplay" else hexDisplay
                val start = out.length
                out.append(text)
                val end = out.length
                out.setSpan(
                    ColorSwatchSpan(swatchColor, COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            else -> appendSticker(out, label)
        }
    }

    /** يقسّم [raw] على `/` عند عمق أقواس صفر فقط — فيبقى `link=(https://a.b/c)` و`copy=(a/b)` قطعة
     *  واحدة (كان `split("/")` البسيط يقطع كل رابط عند أول `//` فيُعرَض الزر كشارة معطوبة). */
    private fun splitMetaParts(raw: String): List<String> {
        val parts = mutableListOf<String>()
        val cur = StringBuilder()
        var depth = 0
        var payDepth = 0 // داخل `؛: … :؛` لا يُقسَم على `/` أيضاً (روابط الدفع تحوي `/`)
        var k = 0
        while (k < raw.length) {
            val ch = raw[k]
            when {
                ch == '\u061B' && k + 1 < raw.length && raw[k + 1] == ':' -> { payDepth++; cur.append("\u061B:"); k++ }
                ch == ':' && k + 1 < raw.length && raw[k + 1] == '\u061B' && payDepth > 0 -> { payDepth--; cur.append(":\u061B"); k++ }
                ch == '(' -> { depth++; cur.append(ch) }
                ch == ')' -> { if (depth > 0) depth--; cur.append(ch) }
                ch == '/' && depth == 0 && payDepth == 0 -> { parts.add(cur.toString()); cur.setLength(0) }
                else -> cur.append(ch)
            }
            k++
        }
        parts.add(cur.toString())
        return parts.map { it.trim() }.filter { it.isNotEmpty() }
    }

    /** لون سداسي، أو اسم لون (ألوان الستيكر accent/green/gold/danger/info/like + أسماء shields.io
     *  الشائعة مثل red/blue/orange) — `null` إن لم يُعرَف فيُتجاهَل بصمت. */
    private fun parseColorValue(raw: String): Pair<Int, String>? {
        parseHexColor(raw)?.let { return it }
        val key = raw.trim().lowercase()
        STICKER_VARIANTS[key]?.let { return it.first to key }
        SHIELDS_COLOR_NAMES[key]?.let { hex -> return parseHexColor("#" + hex) }
        return null
    }

    /** نص بلون [color] مقروء فوق خلفيته الفاتحة الشفافة (tint): يُغمَّق في الثيم الفاتح ويُفتَّح في الداكن. */
    private fun softTextColor(color: Int): Int {
        if (darkMode) return adaptForTheme(color)
        fun dim(c: Int): Int = (c * 0.68).toInt().coerceIn(0, 255)
        return (0xFF shl 24) or (dim((color shr 16) and 0xFF) shl 16) or
            (dim((color shr 8) and 0xFF) shl 8) or dim(color and 0xFF)
    }

    /** ألوان/تسميات افتراضية لـ`status=(...)`؛ اسم غير معروف يُعرَض كما كُتب بلون الهوية. */
    private val STATUS_STYLES: Map<String, Pair<Int, String>> = mapOf(
        "ok" to (0xFF1CA877.toInt() to "\u064A\u0639\u0645\u0644"),
        "warn" to (0xFFB45F06.toInt() to "\u062A\u062D\u0630\u064A\u0631"),
        "error" to (0xFFC0392B.toInt() to "\u062E\u0637\u0623"),
        "info" to (0xFF1A56C7.toInt() to "\u0645\u0639\u0644\u0648\u0645\u0629"),
        "beta" to (0xFF6A47E8.toInt() to "\u062A\u062C\u0631\u064A\u0628\u064A"),
        "new" to (0xFF22C88E.toInt() to "\u062C\u062F\u064A\u062F"),
        "stable" to (0xFF1CA877.toInt() to "\u0645\u0633\u062A\u0642\u0631"),
        "deprecated" to (0xFF667085.toInt() to "\u0645\u062A\u0648\u0642\u0651\u0641")
    )

    /**
     * زر إجراء موحَّد لـ`link=(...)` و`copy=(...)`: [styleKey] يختار `solid` (الافتراضي، خلفية مملوءة) أو
     * `soft` (خلفية شفافة فاتحة + نص ملوَّن) أو `outline` (إطار فقط)؛ [sizeKey] يختار `sm`/`md`/`lg`؛
     * [color] لون الزر (هوية التطبيق افتراضياً)؛ [iconRaw] أيقونة مصغَّرة اختيارية.
     */
    private fun appendActionButton(
        out: SpannableStringBuilder,
        label: String,
        color: Pair<Int, String>?,
        iconRaw: String?,
        styleKey: String?,
        sizeKey: String?,
        clickSpan: ClickableSpan,
        defaultLabel: String = ""
    ) {
        val base = color?.first ?: COLOR_BULLET
        val glyph = iconGlyphFor(iconRaw)
        val shownLabel = label.ifBlank { color?.second.orEmpty().ifBlank { defaultLabel } }
        val text = if (glyph != null) "$glyph  $shownLabel" else shownLabel
        val bg: Int
        val fg: Int
        val stroke: Int
        when (styleKey?.lowercase()) {
            "soft" -> { bg = tintedBackground(base, 0x26); fg = softTextColor(base); stroke = 0 }
            "outline" -> { bg = 0x00000000; fg = softTextColor(base); stroke = base }
            else -> {
                bg = base
                fg = if (color != null) contrastingTextColor(base) else 0xFFFFFFFF.toInt()
                stroke = 0
            }
        }
        val scale = when (sizeKey?.lowercase()) {
            "sm", "small" -> 0.78f
            "lg", "large" -> 1.05f
            else -> 0.9f
        }
        val start = out.length
        out.append(text)
        val end = out.length
        out.setSpan(
            StickerSpan(bg, fg, strokeColor = stroke, strokeWidth = if (stroke != 0) 2.5f else 0f),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(scale), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(clickSpan, start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** كثافة الشاشة الحالية (لتحويل dp→px داخل Spans لا تملك سياقاً) — يضبطها [applyTo] مع [currentImageMaxWidthPx]. */
    private var currentDensity = 2f

    /**
     * **Marquee**: شريط بنص يتحرّك أفقياً بلا توقّف داخل حبّة واحدة (يُقصّ النص عند حدودها ويتكرّر بفاصل
     * حتى يبدو الشريط متّصلاً). الصياغة: `[*النص/marquee*]` أو `[*النص/marquee=(60)*]` (السرعة dp/ثانية،
     * الافتراضي 40)، ومعها اختيارياً: `width=(dp)` (الافتراضي عرض النص كاملاً تقريباً)، `direction=(right)`
     * (الافتراضي يتحرّك لليسار)، `style=(soft|solid|outline)` (الافتراضي soft)، `color=(...)`، `icon=(...)`،
     * و`link=(رابط)` لجعل الشريط كله قابلاً للنقر. التحريك يقوده [MarqueeTicker] عبر [applyTo].
     */
    private fun appendMarquee(
        out: SpannableStringBuilder,
        label: String,
        color: Pair<Int, String>?,
        iconRaw: String?,
        styleKey: String?,
        speedDp: Float?,
        widthDp: Float?,
        directionKey: String?,
        clickSpan: ClickableSpan?
    ) {
        val base = color?.first ?: COLOR_BULLET
        val bg: Int
        val fg: Int
        val stroke: Int
        when (styleKey?.lowercase()) {
            "solid" -> {
                bg = base
                fg = if (color != null) contrastingTextColor(base) else 0xFFFFFFFF.toInt()
                stroke = 0
            }
            "outline" -> { bg = 0x00000000; fg = softTextColor(base); stroke = base }
            else -> { bg = tintedBackground(base, 0x26); fg = softTextColor(base); stroke = 0 }
        }
        val glyph = iconGlyphFor(iconRaw)
        val shown = if (glyph != null) "$glyph  $label" else label
        val density = currentDensity
        val widthPx = if (widthDp != null && widthDp > 0f) widthDp * density else currentImageMaxWidthPx * 0.96f
        val leftward = directionKey?.lowercase() != "right"
        val speedPx = (speedDp ?: 40f).coerceIn(5f, 400f) * density

        val start = out.length
        // مسافات غير قابلة للكسر: النص الأصلي للنطاق لا يُرسَم (الرسم كله داخل [MarqueeSpan])، لكنه يقرّر
        // فرص كسر السطر — بلا NBSP قد ينكسر السطر داخل الشريط عند أي مسافة.
        out.append(shown.replace(' ', '\u00A0'))
        val end = out.length
        out.setSpan(
            MarqueeSpan(shown, bg, fg, stroke, if (stroke != 0) 2.5f else 0f, widthPx, speedPx, leftward),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (clickSpan != null) out.setSpan(clickSpan, start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** يدفع إعادة رسم [TextView] كل إطار ما دام يحوي [MarqueeSpan] ومتّصلاً بالنافذة (يتوقف تلقائياً عند
     *  فصله ويستأنف عند إعادة ربطه). يُخزَّن واحد لكل TextView فلا تتراكم مؤقّتات عند إعادة البناء. */
    private class MarqueeTicker(view: TextView) : Runnable, View.OnAttachStateChangeListener {
        private val ref = java.lang.ref.WeakReference(view)
        private var stopped = false

        override fun run() {
            val v = ref.get() ?: return
            if (stopped) return
            v.invalidate()
            v.postOnAnimation(this)
        }

        override fun onViewAttachedToWindow(v: View) {
            if (!stopped) v.postOnAnimation(this)
        }

        override fun onViewDetachedFromWindow(v: View) {
            v.removeCallbacks(this)
        }

        fun stop(v: View) {
            stopped = true
            v.removeCallbacks(this)
            v.removeOnAttachStateChangeListener(this)
        }
    }

    /** مفتاح ضعيف (WeakHashMap) وقيمة تحمل مرجعاً ضعيفاً للـView — لا تسريب ذاكرة. UI thread فقط. */
    private val marqueeTickers = java.util.WeakHashMap<TextView, MarqueeTicker>()

    private fun startMarqueeIfNeeded(textView: TextView) {
        marqueeTickers.remove(textView)?.stop(textView)
        val spanned = textView.text as? Spanned ?: return
        if (spanned.getSpans(0, spanned.length, MarqueeSpan::class.java).isEmpty() &&
            spanned.getSpans(0, spanned.length, RdocTickerSpan::class.java).isEmpty() &&
            spanned.getSpans(0, spanned.length, RdocVerticalTickerSpan::class.java).isEmpty()
        ) return
        val ticker = MarqueeTicker(textView)
        marqueeTickers[textView] = ticker
        textView.addOnAttachStateChangeListener(ticker)
        if (textView.isAttachedToWindow) textView.postOnAnimation(ticker)
    }

    /**
     * حبّة بعرض ثابت [widthPx] يمرّ فيها [label] بسرعة [speedPxPerSec] (بكسل/ثانية): موضع النص يُحسَب
     * من ساعة النظام مباشرة عند كل رسم (لا حالة داخلية)، فيكفي أن يستدعي [MarqueeTicker] `invalidate()`.
     * يُقصّ الرسم داخل الحبّة عبر `clipRect` ويتكرّر النص بفاصل [gapPx] ليمتلئ العرض دائماً.
     */
    private class MarqueeSpan(
        private val label: String,
        private val bg: Int,
        private val fg: Int,
        private val strokeColor: Int,
        private val strokeWidth: Float,
        private val widthPx: Float,
        private val speedPxPerSec: Float,
        private val leftward: Boolean,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 14f,
        private val paddingV: Float = 4f,
        private val gapPx: Float = 48f
    ) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            return widthPx.toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            val rect = RectF(x, top.toFloat() + paddingV, x + widthPx, bottom.toFloat() - paddingV)
            paint.style = Paint.Style.FILL
            paint.color = bg
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)
            if (strokeWidth > 0f) {
                val savedStroke = paint.strokeWidth
                val half = strokeWidth / 2f
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = strokeWidth
                paint.color = strokeColor
                canvas.drawRoundRect(
                    RectF(rect.left + half, rect.top + half, rect.right - half, rect.bottom - half),
                    cornerRadius, cornerRadius, paint
                )
                paint.strokeWidth = savedStroke
                paint.style = Paint.Style.FILL
            }

            val left = x + paddingH
            val right = x + widthPx - paddingH
            val textWidth = paint.measureText(label)
            val cycle = textWidth + gapPx
            // بالساعة (لا بعدّاد داخلي) وباقتطاع ساعة كاملة لتفادي فقد دقّة الـFloat؛ القفزة عند الاقتطاع غير ملحوظة.
            val seconds = (SystemClock.uptimeMillis() % 3_600_000L) / 1000f
            val offset = (seconds * speedPxPerSec) % cycle

            canvas.save()
            canvas.clipRect(left, top.toFloat(), right, bottom.toFloat())
            paint.color = fg
            var px = if (leftward) left - offset else left + offset - cycle
            while (px < right) {
                if (px + textWidth > left) canvas.drawText(label, px, y.toFloat(), paint)
                px += cycle
            }
            canvas.restore()

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * قسم وصف قابل للطي واحد: `[~عنوان]` ... `[~/]` (انظر موضع الاستدعاء في [toSpannable]).
     * الرأس سطر واحد قابل للنقر بالكامل عبر [ToggleSectionSpan]: يعرض △ (مثلث لأعلى) + "فتح"
     * عندما القسم مغلقاً، أو ▽ (مثلث لأسفل) + "إغلاق" عندما يكون مفتوحاً — نفس اصطلاح "أقسام
     * قابلة للطي" الشائع في وثائق GitHub/التطبيقات، بلا الاعتماد على وسم HTML `<details>` غير
     * المدعوم هنا. محتوى القسم [body] لا يُعرَض إطلاقاً إلا عندما [isOpen] صحيحة، فيبقى النص
     * الناتج أقصر بكثير حين يكون القسم مغلقاً بدل إخفائه بصرياً فقط. كل ذلك داخل بطاقة واحدة
     * بزوايا مدوَّرة عبر [RoundedCardSpan]، تماماً كبطاقات الاقتباس/الكود الأخرى في هذا الملف.
     */
    private fun appendCollapsibleSection(
        out: SpannableStringBuilder,
        title: String,
        body: String,
        isOpen: Boolean,
        onToggle: () -> Unit
    ) {
        val cardStart = out.length
        val headerStart = out.length
        val glyph = if (isOpen) "\u25BD" else "\u25B3" // ▽ مفتوح / △ مغلق
        val stateLabel = if (isOpen) "\u0625\u063A\u0644\u0627\u0642" else "\u0641\u062A\u062D" // "إغلاق"/"فتح"
        val shownTitle = title.ifBlank { "\u0627\u0644\u0648\u0635\u0641" } // "الوصف"
        out.append("$glyph  $shownTitle  ")
        val stateStart = out.length
        out.append("($stateLabel)")
        out.setSpan(ForegroundColorSpan(COLOR_H_DIM), stateStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.85f), stateStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        val headerEnd = out.length
        out.setSpan(StyleSpan(Typeface.BOLD), headerStart, headerEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_BULLET), headerStart, stateStart, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ToggleSectionSpan(onToggle), headerStart, headerEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        if (isOpen && body.isNotBlank()) {
            out.append('\n')
            val bodyStart = out.length
            val bodyLines = body.lines()
            bodyLines.forEachIndexed { idx, line ->
                if (idx > 0) out.append('\n')
                appendInline(out, line.trim())
            }
            out.setSpan(ForegroundColorSpan(COLOR_QUOTE_TEXT), bodyStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val cardEnd = out.length
        out.setSpan(
            RoundedCardSpan(COLOR_QUOTE_BG, COLOR_CARD_BORDER, cardStart, cardEnd),
            cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    /**
     * [ClickableSpan] رأس قسم الوصف القابل للطي: عند النقر يستدعي [onToggle] فقط (المستدعي في
     * [toSpannable]/[appendCollapsibleSection] هو من يُحدِّث `expandedSections` ويُعيد بناء النص
     * عبر `onToggle` الممرَّر لـ[applyTo]) — بلا لون/خط رابط افتراضي، فالشكل مُتحكَّم به بالكامل
     * عبر Spans النص المرافقة لنفس النطاق في [appendCollapsibleSection].
     */
    private class ToggleSectionSpan(private val onToggle: () -> Unit) : ClickableSpan() {
        override fun onClick(widget: View) {
            onToggle()
        }

        override fun updateDrawState(ds: TextPaint) {}
    }

    /**
     * زر رابط حقيقي: [ClickableSpan] يفتح [url] في المتصفح (Intent.ACTION_VIEW) عند النقر، عبر
     * سياق [widget] الممرَّر تلقائياً — بلا حاجة لتمرير Context لهذا الملف، بنفس أسلوب
     * [CopyCodeSpan]. رابط غير صالح أو غياب أي تطبيق قادر على فتحه يُسقَط بهدوء بلا انهيار.
     */
    private class LinkButtonClickSpan(private val url: String) : ClickableSpan() {
        override fun onClick(widget: View) {
            try {
                widget.context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
            } catch (t: Throwable) {
                // رابط غير صالح، أو لا يوجد تطبيق على الجهاز قادر على فتحه — نتجاهل بهدوء
            }
        }

        override fun updateDrawState(ds: TextPaint) {}
    }

    /** لون هوية اللغة (نقطة + وسم الاسم)، محايد للغات غير المدرَجة في [LANGUAGE_ACCENTS]. */
    private fun languageAccentColor(lang: String): Int =
        LANGUAGE_ACCENTS[lang.trim().lowercase()]?.let { adaptForTheme(it) } ?: COLOR_CODE_TEXT

    /**
     * رأس بطاقة كود احترافي واحد: نقطة ملوَّنة بهوية اللغة + اسمها (إن ذُكرت لغة، وإلا وسم عام
     * "CODE")، ثم مسافة، ثم زر "نسخ" حقيقي قابل للنقر عبر [CopyCodeSpan] — ينسخ [code] الخام
     * كاملاً (بلا أي تنسيق) للحافظة عند النقر، تماماً كزر النسخ في كتل كود GitHub/محرِّرات IDE.
     */
    private fun appendCodeHeader(out: SpannableStringBuilder, lang: String, code: String) {
        val accent = languageAccentColor(lang)
        val dotStart = out.length
        out.append("\u25CF ") // نقطة ملوَّنة صغيرة قبل اسم اللغة
        out.setSpan(ForegroundColorSpan(accent), dotStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.62f), dotStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        val labelStart = out.length
        out.append(if (lang.isNotBlank()) lang.uppercase() else "CODE")
        out.setSpan(ForegroundColorSpan(accent), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(TypefaceSpan("monospace"), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.68f), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        out.append("  ")

        val copyStart = out.length
        out.append("\u29C9 \u0646\u0633\u062E") // "⧉ نسخ"
        val copyEnd = out.length
        out.setSpan(StickerSpan(COLOR_COPY_BUTTON_BG, COLOR_CODE_TEXT), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.66f), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(CopyCodeSpan(code), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * زر نسخ حقيقي: [ClickableSpan] ينسخ [code] الخام كاملاً إلى حافظة الجهاز عند النقر (عبر
     * سياق [widget] الممرَّر تلقائياً من TextView، بلا حاجة لتمرير Context لهذا الملف بأكمله)،
     * مع رسالة تأكيد قصيرة (Toast). يعمل فقط إن كان TextView مُفعَّلاً بـ[LinkMovementMethod]
     * (يتم ذلك تلقائياً عبر [applyTo]).
     */
    private class CopyCodeSpan(
        private val code: String,
        private val toastText: String = "\u062A\u0645 \u0646\u0633\u062E \u0627\u0644\u0643\u0648\u062F"
    ) : ClickableSpan() {
        override fun onClick(widget: View) {
            val ctx = widget.context
            val clipboard = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
            clipboard?.setPrimaryClip(ClipData.newPlainText("code", code))
            Toast.makeText(ctx, toastText, Toast.LENGTH_SHORT).show()
        }

        // بلا تسطير/لون رابط افتراضي — الشكل مُتحكَّم به بالكامل عبر StickerSpan المرافق لنفس النطاق.
        override fun updateDrawState(ds: TextPaint) {}
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //                    لهجة rdoc — Documentation Container (README.rdoc)
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    // ملف `.rdoc` لم يعد "Markdown موسَّعاً" فحسب، بل لهجة مستقلة تُفعَّل بترويسة صريحة في أول المستند
    // (`؛؛؛ rdoc` ثم `؛؛؛`، وتُضاف تلقائياً لأي README.rdoc عبر [ensureRdocHeader]). بدون الترويسة يبقى
    // المستند Markdown خالصاً، فلا تتأثر ملفات README.md القديمة (مثل `*"نص"*` المائل بين علامتي تنصيص).
    //
    // الرموز الجديدة (لا وجود لها في Markdown):
    //   1) `:::` حاويات تخطيط قابلة للتعشيش:   ::: تحذير عنوان … :::   (note/tip/warning/danger/card/steps/spoiler…)
    //   2) `؛؛؛` بلوكات بيانات منظَّمة:          ؛؛؛ حقائق … ؛؛؛   |   ؛؛؛ meta … ؛؛؛   |   ؛؛؛ مراجع … ؛؛؛
    //   3) `*"…"*` استدعاء رابط عنصر بمعرِّفه:   *"install"*  أو  *"install|نص مخصّص"*  أو  *"@user/lib"*
    //   وأزواج مفتوحة/مغلقة:
    //   • `؛:` … `:؛` دفع/تبرّع:               ؛:paypal|ahmad|5 USD|ادعمنا:؛   (ويعمل داخل `[* … *]` أيضاً)
    //   • `:;` … `;:` فيديو/ويب فيو:            :;video https://youtu.be/ID|عنوان;:
    //   • `[نوع: قيمة]` أنواع للأقواس المربّعة:  [tag: rin] [key: Ctrl] [cmd: rin run x] [date: 2026-09-29] …
    //
    // "أصعب وأقوى": حدود صارمة (عمق حاويات ≤ 4، حجم مستند، https فقط للدفع/الوسائط، قوائم بيضاء لمزوّدي
    // الدفع، تأكيد قبل فتح أي رابط دفع، WebView بلا ملفات ولا JavaScript إلا لمضيفي فيديو موثوقين)،
    // وأخطاء مرئية بدل الصمت (مرجع مفقود، حاوية غير مغلقة…) + [validate] لفحص المستند كاملاً.

    /** أساس الروابط القانونية لـRin (انظر web/RIN_LINKS.md): ملف شخصي `@user` ومكتبة `@user/lib`. */
    private const val RIN_LINKS_BASE = "https://dlof-lib.github.io/rinlang/"

    /** أقصى عمق لتعشيش حاويات `:::`؛ ما بعده يُعرَض نصاً مع تنبيه. */
    private const val RDOC_MAX_CONTAINER_DEPTH = 4

    /** أقصى حجم لمستند rdoc (بالأحرف)؛ ما بعده يُقتطَع مع تنبيه. */
    private const val RDOC_MAX_CHARS = 400_000

    /** عدد المجموعات الملتقِطة في [RDOC_INLINE_PREFIX] — إزاحة فهارس مجموعات [inlineRegex] القديمة. */
    private const val RDOC_GROUP_OFFSET = 6

    private var rdocMode = false
    private var collapsibleCounter = 0
    private var containerDepth = 0
    private var suppressNextGap = false

    /** مرجع عنصر قابل للاستدعاء عبر `*"id"*`: رابطه + تسميته الافتراضية. */
    private class ElementRef(val url: String, val label: String)

    private var currentElementRefs: Map<String, ElementRef> = emptyMap()
    private var docElementRefs: Map<String, ElementRef> = emptyMap()

    /** متغيّرات المستند (من بلوك `؛؛؛ meta`) و عناوينه — على مستوى المستند كله لأن كل مقطع يُرسَم بمعزل. */
    private var docMeta: Map<String, String> = emptyMap()
    private var docHeadings: List<Pair<Int, String>> = emptyList()
    private var currentMeta: Map<String, String> = emptyMap()
    private var currentHeadings: List<Pair<Int, String>> = emptyList()

    /** نتيجة مسح سياج (`:::` أو `؛؛؛`): أسطر الجسم، فهرس السطر التالي للسياج، وهل وُجد الإغلاق. */
    private class FenceScan(val body: List<String>, val next: Int, val closed: Boolean)

    private val colonFenceOpenRegex = Regex("^:{3,}[ \\t]*([^\\s:].*)$")
    private val colonFenceCloseRegex = Regex("^:{3,}[ \\t]*$")
    private val arSemiFenceOpenRegex = Regex("^\u061B{3,}[ \\t]*(\\S.*)$")
    private val arSemiFenceCloseRegex = Regex("^\u061B{3,}[ \\t]*$")
    private val rdocHeaderRegex = Regex("(?m)^\\s*\u061B{3,}[ \\t]*(?:rdoc|meta|بيانات)(?=[ \\t]|$)", RegexOption.IGNORE_CASE)
    private val whitespaceRegex = Regex("\\s+")
    private val metaBadgeScanRegex = Regex("\\[\\*([^\\]]+?)\\*\\]")
    private val refLineRegex = Regex("^([\\p{L}\\p{N}_.-]+)\\s*=\\s*(\\S.*)$")
    private val stepItemRegex = Regex("^(?:[-*+]|\\d{1,3}[.)])\\s+(.*)$")
    private val elementLinkScanRegex = Regex("(?<![*\\\\])\\*\"([^\"\\n]+?)\"\\*(?!\\*)")
    private val paymentScanRegex = Regex("\u061B:(.+?):\u061B")
    private val mediaScanRegex = Regex(":;(.+?);:")
    private val elementIdScanRegex = Regex("\\[\\*[^\\]]*?/\\s*id\\s*=\\s*\\(([^)]*)\\)")

    private val rdocHeadingRegex = Regex("^(={1,6})[ \\t]+(\\S.*)$")
    private val headingScanRegex = Regex("^(#{1,6}|={1,6})[ \\t]+(\\S.*)$")
    private val variableRegex = Regex("(\\\\)?\\{\\{\\s*([\\p{L}\\p{N}_.-]+)\\s*\\}\\}")
    private val changelogLineRegex = Regex("^([+~!-])\\s+(.+)$")
    private val timelineLineRegex = Regex("^([^:|]{1,30}?)\\s*[:|]\\s+(.+)$")
    private val faqQuestionRegex = Regex("^(?:\u0633|Q|q)\\s*[:\uFF1A]\\s*(.+)$")
    private val faqAnswerRegex = Regex("^(?:\u062C|A|a)\\s*[:\uFF1A]\\s*(.+)$")
    private val linkItemRegex = Regex("^(.+?)\\s*=\\s*(\\S.*)$")
    private val badgeItemRegex = Regex("^(.+?)\\s*:\\s+(\\S.*)$")

    private val DATA_BLOCK_TYPES = setOf(
        "rdoc", "meta", "بيانات", "refs", "مراجع", "facts", "حقائق",
        "links", "روابط", "badges", "شارات", "support", "دعم", "page", "صفحة",
        "package", "حزمة", "deps", "dependencies", "تبعيات"
    )
    private val SILENT_DATA_TYPES = setOf("rdoc", "meta", "بيانات", "refs", "مراجع", "page", "صفحة")

    /** هل [text] مستند rdoc (يحوي ترويسة `؛؛؛ rdoc` أو `؛؛؛ meta`)؟ */
    fun isRdocDocument(text: String): Boolean = rdocHeaderRegex.containsMatchIn(text)

    /** يضمن وجود ترويسة rdoc في [text] (تُضاف صامتة في أوله إن غابت) — تُستدعى لكل README.rdoc عند القراءة. */
    fun ensureRdocHeader(text: String): String =
        if (isRdocDocument(text)) text else "\u061B\u061B\u061B rdoc\n\u061B\u061B\u061B\n\n$text"

    // ─────────────────────────── أنواع الحاويات `:::` ───────────────────────────

    private class ContainerStyle(val color: Int, val icon: String, val label: String, val bar: Boolean)

    private val CONTAINER_STYLES: Map<String, ContainerStyle> = mapOf(
        "note" to ContainerStyle(0xFF1A56C7.toInt(), "\u2139\uFE0F", "ملاحظة", true),
        "tip" to ContainerStyle(0xFF1D7A4C.toInt(), "\uD83D\uDCA1", "نصيحة", true),
        "important" to ContainerStyle(0xFF6A47E8.toInt(), "\u2757", "مهم", true),
        "warning" to ContainerStyle(0xFFB45F06.toInt(), "\u26A0\uFE0F", "تحذير", true),
        "danger" to ContainerStyle(0xFFC0392B.toInt(), "\uD83D\uDED1", "خطر", true),
        "success" to ContainerStyle(0xFF1CA877.toInt(), "\u2705", "تم", true),
        "quote" to ContainerStyle(0xFF7C5CFF.toInt(), "\u275D", "اقتباس", true),
        "steps" to ContainerStyle(0xFF22C88E.toInt(), "\u2630", "خطوات", false),
        "card" to ContainerStyle(0xFF7A8299.toInt(), "\u25A3", "", false),
        "timeline" to ContainerStyle(0xFF3B9EFF.toInt(), "\u25F7", "الخط الزمني", false),
        "changelog" to ContainerStyle(0xFF22C88E.toInt(), "\u27F3", "سجل التغييرات", false),
        "faq" to ContainerStyle(0xFF6A47E8.toInt(), "\u2753", "أسئلة شائعة", false),
        "toc" to ContainerStyle(0xFF7A8299.toInt(), "\u2630", "الفهرس", false),
        "api" to ContainerStyle(0xFF7C5CFF.toInt(), "\u0192", "دالة", true),
        "apiindex" to ContainerStyle(0xFF7A8299.toInt(), "\u0192", "فهرس الدوال", false),
        "install" to ContainerStyle(0xFF22C88E.toInt(), "\u2B07", "تثبيت", true),
        "example" to ContainerStyle(0xFF3B9EFF.toInt(), "\u25B6", "مثال", true)
    )

    private val CONTAINER_ALIASES: Map<String, String> = mapOf(
        "note" to "note", "ملاحظة" to "note", "info" to "note", "معلومة" to "note",
        "tip" to "tip", "نصيحة" to "tip",
        "important" to "important", "مهم" to "important",
        "warning" to "warning", "warn" to "warning", "تحذير" to "warning",
        "danger" to "danger", "caution" to "danger", "خطر" to "danger",
        "success" to "success", "نجاح" to "success", "تم" to "success",
        "quote" to "quote", "اقتباس" to "quote",
        "steps" to "steps", "خطوات" to "steps",
        "card" to "card", "بطاقة" to "card",
        "timeline" to "timeline", "زمني" to "timeline", "جدول-زمني" to "timeline",
        "changelog" to "changelog", "تغييرات" to "changelog", "سجل-التغييرات" to "changelog",
        "faq" to "faq", "أسئلة" to "faq",
        "toc" to "toc", "فهرس" to "toc",
        "spoiler" to "spoiler", "مخفي" to "spoiler", "طي" to "spoiler",
        "marquee" to "marquee", "ticker" to "marquee", "شريط" to "marquee", "متحرك" to "marquee", "شريط-متحرك" to "marquee",
        "api" to "api", "fn" to "api", "function" to "api", "class" to "api", "دالة" to "api", "صنف" to "api",
        "api-index" to "apiindex", "فهرس-الدوال" to "apiindex",
        "install" to "install", "تثبيت" to "install",
        "example" to "example", "مثال" to "example"
    )

    // ─────────────────────────── أنواع الأقواس المربّعة `[نوع: قيمة]` ───────────────────────────

    private val TYPED_BRACKET_TYPES: Map<String, String> = mapOf(
        "tag" to "tag", "وسم" to "tag",
        "user" to "user", "mention" to "user", "مستخدم" to "user",
        "lib" to "lib", "مكتبة" to "lib",
        "key" to "key", "kbd" to "key", "مفتاح" to "key",
        "file" to "file", "ملف" to "file",
        "date" to "date", "تاريخ" to "date",
        "version" to "version", "إصدار" to "version",
        "price" to "price", "سعر" to "price",
        "abbr" to "abbr", "اختصار" to "abbr",
        "note" to "note", "ملاحظة" to "note",
        "cmd" to "cmd", "أمر" to "cmd",
        "email" to "email", "بريد" to "email",
        "progress" to "progress", "تقدم" to "progress",
        "rating" to "rating", "تقييم" to "rating",
        "status" to "status", "حالة" to "status",
        "color" to "color", "لون" to "color",
        "link" to "link", "رابط" to "link",
        "download" to "download", "تحميل" to "download",
        "count" to "count", "عدد" to "count",
        "pay" to "pay", "دفع" to "pay",
        "badge" to "badge", "شارة" to "badge",
        "icon" to "icon", "أيقونة" to "icon",
        "time" to "time", "وقت" to "time",
        "size" to "size", "حجم" to "size",
        "license" to "license", "ترخيص" to "license",
        "platform" to "platform", "منصة" to "platform",
        "lang" to "lang", "لغة" to "lang",
        "copy" to "copy", "نسخ" to "copy",
        "phone" to "phone", "هاتف" to "phone",
        "video" to "video", "فيديو" to "video",
        "web" to "web", "ويب" to "web",
        // اختصارات حالة: `[ok: يعمل]` `[warn: تنبيه]` `[error: فشل]` `[new: جديد]`...
        "ok" to "status:ok", "warn" to "status:warn", "error" to "status:error", "info" to "status:info",
        "new" to "status:new", "beta" to "status:beta", "stable" to "status:stable",
        "deprecated" to "status:deprecated", "متوقف" to "status:deprecated",
        // أماكن
        "place" to "place", "location" to "place", "مكان" to "place", "موقع" to "place",
        "coords" to "place", "إحداثيات" to "place",
        "flag" to "flag", "country" to "flag", "علم" to "flag", "دولة" to "flag",
        "address" to "address", "عنوان" to "address",
        "zone" to "zone", "توقيت" to "zone",
        // أحجام وقياسات
        "dim" to "dim", "أبعاد" to "dim",
        "distance" to "distance", "مسافة" to "distance",
        "weight" to "weight", "وزن" to "weight",
        "temp" to "temp", "حرارة" to "temp",
        "percent" to "percent", "نسبة" to "percent",
        "hash" to "hash", "بصمة" to "hash",
        "marquee" to "marquee", "ticker" to "marquee", "شريط" to "marquee",
        // حزم وتوثيق
        "since" to "since", "منذ" to "since",
        "requires" to "requires", "يتطلب" to "requires",
        "dep" to "dep", "تبعية" to "dep",
        "install" to "install", "تثبيت" to "install",
        "author" to "author", "ناشر" to "author",
        "repo" to "repo", "مستودع" to "repo"
    )

    /** يلتقط `[نوع: قيمة]` بأنواع [TYPED_BRACKET_TYPES] في سطر (لـ[validate]). */
    private val typedBracketScanRegex = Regex(
        "\\[((?i:" + TYPED_BRACKET_TYPES.keys.joinToString("|") + ")):[ \\t]*([^\\]\\n]+?)\\](?![(\\[])"
    )

    /**
     * مقدّمة تعبير [inlineRegexRdoc]: بدائل rdoc تسبق بدائل [inlineRegex] القديمة (فتغلب `*"…"*` على المائل).
     * مجموعاتها الست (عدد [RDOC_GROUP_OFFSET]): 1 حرف مُفلَت، 2 `*"id"*`، 3 `؛:…:؛`، 4 `:;…;:`، 5/6 `[نوع: قيمة]`.
     * النوع في 5/6 لا يطابق إلا الأنواع المعروفة، وبعد `]` لا يجوز `(` أو `[` (كي لا يُخطَف رابط عادي).
     */
    private val RDOC_INLINE_PREFIX: String =
        "\\\\([*:;\u061B\\[])" +
            "|(?<![*\\\\])\\*\"([^\"\\n]+?)\"\\*(?!\\*)" +
            "|\u061B:(.+?):\u061B" +
            "|:;(.+?);:" +
            "|\\[((?i:" + TYPED_BRACKET_TYPES.keys.joinToString("|") + ")):[ \\t]*([^\\]\\n]+?)\\](?![(\\[])"

    private val inlineRegexRdoc = Regex(RDOC_INLINE_PREFIX + "|" + inlineRegex.pattern)

    // ─────────────────────────── مساعدات الروابط ───────────────────────────

    private fun isSafeLinkUrl(url: String): Boolean {
        val u = url.trim().lowercase()
        return u.startsWith("https://") || u.startsWith("http://") || u.startsWith("mailto:")
    }

    private fun isHttpsUrl(url: String): Boolean = url.trim().startsWith("https://", ignoreCase = true)

    private fun hostOf(url: String): String =
        try {
            Uri.parse(url.trim()).host.orEmpty().lowercase().removePrefix("www.")
        } catch (t: Throwable) {
            ""
        }

    private val rinHandleRegex = Regex("^@[A-Za-z0-9._-]{1,40}$")
    private val rinLibRegex = Regex("^[A-Za-z0-9._-]{1,80}$")

    /** `@user` → صفحة الناشر، `@user/lib` → صفحة المكتبة (`.og.rin` تُضاف مرة واحدة، المسافات → `-`). */
    private fun canonicalRinUrl(handle: String): String? {
        val h = handle.trim()
        val slash = h.indexOf('/')
        val user = if (slash < 0) h else h.substring(0, slash)
        if (!rinHandleRegex.matches(user)) return null
        if (slash < 0) return RIN_LINKS_BASE + user
        val lib = h.substring(slash + 1).trim().replace(' ', '-').removeSuffix(".og.rin")
        if (!rinLibRegex.matches(lib)) return null
        return "$RIN_LINKS_BASE$user/$lib.og.rin"
    }

    private fun resolveElement(key: String, refs: Map<String, ElementRef>): ElementRef? {
        val k = key.trim()
        if (k.startsWith("@")) {
            val url = canonicalRinUrl(k) ?: return null
            return ElementRef(url, k)
        }
        return refs[k.lowercase()]
    }

    /**
     * يجمع كل العناصر القابلة للاستدعاء في [markdown]: (أ) أزرار/شارات `[*نص/link=(رابط)/id=(معرّف)*]`،
     * و(ب) بلوكات `؛؛؛ مراجع` بأسطر `معرّف = رابط | تسمية`. يتجاهل كتل الكود. أول تعريف لمعرّف يفوز.
     */
    private fun collectElementRefs(markdown: String): Map<String, ElementRef> {
        val refs = LinkedHashMap<String, ElementRef>()
        var inFence = false
        var inRefs = false
        for (raw in markdown.lines()) {
            val t = raw.trim()
            if (t.startsWith("```")) { inFence = !inFence; continue }
            if (inFence) continue
            if (inRefs) {
                if (arSemiFenceCloseRegex.matches(t) || t.isEmpty()) { inRefs = false; continue }
                val m = refLineRegex.find(t) ?: continue
                val parts = m.groupValues[2].split("|", limit = 2)
                val url = parts[0].trim()
                if (isSafeLinkUrl(url)) {
                    refs.putIfAbsent(m.groupValues[1].trim().lowercase(), ElementRef(url, parts.getOrNull(1)?.trim().orEmpty()))
                }
                continue
            }
            val open = arSemiFenceOpenRegex.find(t)
            if (open != null) {
                val type = open.groupValues[1].trim().split(whitespaceRegex)[0].lowercase()
                if (type == "refs" || type == "مراجع") inRefs = true
                continue
            }
            for (bm in metaBadgeScanRegex.findAll(t)) {
                val parts = splitMetaParts(bm.groupValues[1])
                if (parts.size < 2) continue
                var id: String? = null
                var link: String? = null
                for (p in parts.drop(1)) {
                    val kv = metaBadgeKeyValueRegex.find(p) ?: continue
                    when (kv.groupValues[1].lowercase()) {
                        "id" -> id = kv.groupValues[2].trim().lowercase()
                        "link" -> link = kv.groupValues[2].trim()
                    }
                }
                val idValue = id
                val linkValue = link
                if (!idValue.isNullOrEmpty() && linkValue != null && isSafeLinkUrl(linkValue)) {
                    refs.putIfAbsent(idValue, ElementRef(linkValue, parts[0]))
                }
            }
        }
        return refs
    }

    // ─────────────────────────── مسح الأسوار ───────────────────────────

    /** يمسح جسم حاوية `:::` من السطر [start] (سطر الفتح) — كل `:::` فارغ يغلق أقرب حاوية مفتوحة، وتُتجاهَل كتل الكود. */
    private fun scanFence(lines: List<String>, start: Int, openRegex: Regex, closeRegex: Regex): FenceScan {
        val body = mutableListOf<String>()
        var depth = 1
        var inFence = false
        var j = start + 1
        while (j < lines.size) {
            val t = lines[j].trim()
            if (t.startsWith("```")) {
                inFence = !inFence
            } else if (!inFence) {
                if (closeRegex.matches(t)) {
                    depth--
                    if (depth == 0) return FenceScan(body, j + 1, true)
                } else if (openRegex.matches(t)) {
                    depth++
                }
            }
            body.add(lines[j])
            j++
        }
        return FenceScan(body, j, false)
    }

    /** يمسح بلوك `؛؛؛`: ينتهي عند `؛؛؛` أو عند أول سطر فارغ (غير مغلق) — فلا يبتلع نسيانُ الإغلاق بقية المستند. */
    private fun scanDataFence(lines: List<String>, start: Int): FenceScan {
        val body = mutableListOf<String>()
        var j = start + 1
        while (j < lines.size) {
            val t = lines[j].trim()
            if (arSemiFenceCloseRegex.matches(t)) return FenceScan(body, j + 1, true)
            if (t.isEmpty()) return FenceScan(body, j, false)
            body.add(lines[j])
            j++
        }
        return FenceScan(body, j, false)
    }

    /** يقسّم المستند إلى (نص، هل هو حاوية `:::` كاملة) — الحاوية لا تُقطَع أبداً بين مقاطع العرض. */
    private fun splitContainerChunks(markdown: String): List<Pair<String, Boolean>> {
        val lines = markdown.lines()
        val chunks = mutableListOf<Pair<String, Boolean>>()
        val pending = StringBuilder()
        fun flush() {
            if (pending.isNotBlank()) chunks.add(pending.toString() to false)
            pending.setLength(0)
        }
        var inFence = false
        var i = 0
        while (i < lines.size) {
            val t = lines[i].trim()
            if (t.startsWith("```")) {
                inFence = !inFence
            } else if (!inFence && colonFenceOpenRegex.matches(t)) {
                val scan = scanFence(lines, i, colonFenceOpenRegex, colonFenceCloseRegex)
                flush()
                val block = StringBuilder()
                for (k in i until scan.next) block.append(lines[k]).append('\n')
                chunks.add(block.toString() to true)
                i = scan.next
                continue
            }
            pending.append(lines[i]).append('\n')
            i++
        }
        flush()
        return chunks
    }

    // ─────────────────────────── عرض الحاويات `:::` ───────────────────────────

    /** يحوّل بنود القائمة العلوية إلى ترقيم متسلسل (لحاوية `steps`). */
    private fun numberSteps(body: List<String>): String {
        var n = 0
        return body.joinToString("\n") { line ->
            val m = stepItemRegex.find(line)
            if (m != null) {
                n++
                "$n. ${m.groupValues[1]}"
            } else {
                line
            }
        }
    }

    /** `+ أضيف` `~ عُدّل` `! أُصلح` `- حُذف` → بند قائمة بستيكر ملوَّن؛ أي سطر آخر (مثل عنوان الإصدار) يمرّ كما هو. */
    private fun changelogText(body: List<String>): String = body.joinToString("\n") { line ->
        val m = changelogLineRegex.find(line.trim())
        if (m == null) line else {
            val tag = when (m.groupValues[1]) {
                "+" -> "[[إضافة|green]]"
                "~" -> "[[تغيير|info]]"
                "!" -> "[[إصلاح|gold]]"
                else -> "[[حذف|danger]]"
            }
            "- $tag ${m.groupValues[2]}"
        }
    }

    /** `2026-09-29: نص` أو `v1 | نص` → بند بتاريخ/مرحلة عريضة. */
    private fun timelineText(body: List<String>): String = body.joinToString("\n") { line ->
        val m = timelineLineRegex.find(line.trim())
        if (m == null) line else "- **${m.groupValues[1].trim()}** \u2014 ${m.groupValues[2]}"
    }

    /** `س: سؤال` / `ج: جواب` (أو Q:/A:) → سؤال عريض وجواب متعشِّش تحته. */
    private fun faqText(body: List<String>): String = body.joinToString("\n") { line ->
        val t = line.trim()
        val q = faqQuestionRegex.find(t)
        val a = faqAnswerRegex.find(t)
        when {
            q != null -> "- **\u2753 ${q.groupValues[1]}**"
            a != null -> "    - ${a.groupValues[1]}"
            else -> line
        }
    }

    /** فهرس تلقائي من عناوين المستند كله (`#` و`=`)، متعشِّش بحسب المستوى. */
    private fun tocText(): String {
        if (currentHeadings.isEmpty()) return "لا عناوين في المستند."
        return currentHeadings.joinToString("\n") { (level, text) ->
            "    ".repeat((level - 1).coerceIn(0, 4)) + "- " + expandVariables(text, currentMeta)
        }
    }

    /** يستبدل `{{مفتاح}}` بقيمة [meta] خارج كتل الكود؛ مفتاح مجهول يبقى كما كُتب (ويُبلِّغ عنه [validate]). */
    private fun expandVariables(text: String, meta: Map<String, String>): String {
        if (!text.contains("{{")) return text
        var inFence = false
        return text.lines().joinToString("\n") { line ->
            if (line.trim().startsWith("```")) {
                inFence = !inFence
                line
            } else if (inFence) {
                line
            } else {
                variableRegex.replace(line) { m ->
                    if (m.groupValues[1].isNotEmpty()) m.value.substring(1)
                    else meta[m.groupValues[2].lowercase()] ?: m.value
                }
            }
        }
    }

    /** عناوين المستند (مستوى، نص) من `#` و`=` خارج كتل الكود. */
    private fun collectHeadings(markdown: String): List<Pair<Int, String>> {
        val list = ArrayList<Pair<Int, String>>()
        var inFence = false
        for (raw in markdown.lines()) {
            val t = raw.trim()
            if (t.startsWith("```")) { inFence = !inFence; continue }
            if (inFence) continue
            val m = headingScanRegex.find(t) ?: continue
            list.add(m.groupValues[1].length to m.groupValues[2].replace(headingClosingHashesRegex, "").trim())
        }
        return list
    }

    private fun appendRdocContainer(
        out: SpannableStringBuilder,
        header: String,
        body: List<String>,
        closed: Boolean,
        expandedSections: MutableSet<String>,
        onToggle: (() -> Unit)?
    ) {
        val tokens = header.trim().split(whitespaceRegex, limit = 2)
        val rawType = tokens[0].lowercase()
        val title = tokens.getOrNull(1)?.trim().orEmpty()
        val key = CONTAINER_ALIASES[rawType]
        val danger = 0xFFC0392B.toInt()

        if (containerDepth >= RDOC_MAX_CONTAINER_DEPTH) {
            appendDiagnosticChip(out, "عمق الحاويات يتجاوز $RDOC_MAX_CONTAINER_DEPTH")
            body.forEach { line -> out.append('\n'); appendInline(out, line.trim()) }
            return
        }

        if (key == "spoiler") {
            val sectionId = "cnt${collapsibleCounter++}:${title.ifBlank { "spoiler" }}"
            val isOpen = expandedSections.contains(sectionId)
            appendCollapsibleSection(out, title.ifBlank { "مخفي" }, body.joinToString("\n"), isOpen) {
                if (!expandedSections.remove(sectionId)) expandedSections.add(sectionId)
                onToggle?.invoke()
            }
            return
        }

        if (key == "marquee") {
            appendRdocMarquee(out, title, body, closed)
            return
        }

        val style = CONTAINER_STYLES[key ?: "card"] ?: CONTAINER_STYLES.getValue("card")
        val accent = adaptForTheme(style.color)
        val shownTitle = if (key == null) header.trim() else title.ifBlank { style.label }
        val hasHeader = shownTitle.isNotBlank() || !closed

        val cardStart = out.length
        if (hasHeader) {
            val headerStart = out.length
            out.append(if (shownTitle.isNotBlank()) "${style.icon} $shownTitle" else style.icon)
            val headerEnd = out.length
            out.setSpan(StyleSpan(Typeface.BOLD), headerStart, headerEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(ForegroundColorSpan(if (key == null) COLOR_H_DIM else accent), headerStart, headerEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            // توقيع الدالة بخط ثابت (بعد الأيقونة والمسافة).
            if (key == "api" && title.isNotBlank()) {
                out.setSpan(TypefaceSpan("monospace"), headerStart + style.icon.length + 1, headerEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            if (key == null) {
                val s = out.length
                out.append("  \u2716 نوع غير معروف")
                out.setSpan(ForegroundColorSpan(adaptForTheme(danger)), s, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            if (!closed) {
                val s = out.length
                out.append("  \u2716 حاوية غير مغلقة")
                out.setSpan(ForegroundColorSpan(adaptForTheme(danger)), s, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }
        val bodyText = when (key) {
            "steps" -> numberSteps(body)
            "changelog" -> changelogText(body)
            "timeline" -> timelineText(body)
            "faq" -> faqText(body)
            "toc" -> tocText()
            "api" -> apiText(body)
            "apiindex" -> apiIndexText()
            "install" -> installText(body)
            else -> body.joinToString("\n")
        }
        if (bodyText.isNotBlank()) {
            if (hasHeader) out.append('\n')
            suppressNextGap = true
            containerDepth++
            try {
                renderBlocks(out, bodyText, expandedSections, onToggle)
            } finally {
                containerDepth--
                suppressNextGap = false
            }
        }
        val cardEnd = out.length
        if (cardEnd <= cardStart) return
        if (style.bar) out.setSpan(QuoteBarSpan(accent), cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(
            RoundedCardSpan(tintedBackground(accent), accent, cardStart, cardEnd),
            cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    // ─────────────────────────── عرض بلوكات `؛؛؛` ───────────────────────────

    /** بلوك بيانات `؛؛؛`: `rdoc`/`meta`/`مراجع` صامتة (تُقرأ عبر [extractMeta]/`*"id"*`)، و`حقائق` بطاقة مفتاح: قيمة.
     *  الفراغ قبل البلوك يضيفه المستدعي (blockGap) فقط عند ظهور شيء مرئي — انظر [renderBlocks]. */
    private fun appendRdocDataBlock(out: SpannableStringBuilder, header: String, scan: FenceScan) {
        val tokens = header.trim().split(whitespaceRegex, limit = 2)
        val type = tokens[0].lowercase()
        val title = tokens.getOrNull(1)?.trim().orEmpty()
        if (type !in DATA_BLOCK_TYPES) {
            appendDiagnosticChip(out, "بلوك ؛؛؛ غير معروف: $type")
            return
        }
        if (type in SILENT_DATA_TYPES) {
            if (!scan.closed) appendDiagnosticChip(out, "بلوك ؛؛؛ $type غير مغلق")
            return
        }
        if (type == "package" || type == "حزمة") {
            appendPackageCard(out, scan)
            return
        }
        if (type == "deps" || type == "dependencies" || type == "تبعيات") {
            appendDepsCard(out, title, scan)
            return
        }
        if (type == "links" || type == "روابط" || type == "badges" || type == "شارات" || type == "support" || type == "دعم") {
            appendFlowBlock(out, type, scan)
            return
        }
        val cardStart = out.length
        val hs = out.length
        out.append("\u25A4 ${title.ifBlank { "حقائق" }}")
        out.setSpan(StyleSpan(Typeface.BOLD), hs, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_HEADING), hs, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        for (line in scan.body) {
            val t = line.trim()
            if (t.isEmpty() || t.startsWith("\u061B\u061B")) continue
            out.append('\n')
            val idx = t.indexOf(':')
            if (idx > 0 && idx < t.length - 1) {
                val ks = out.length
                out.append(t.substring(0, idx).trim())
                out.setSpan(StyleSpan(Typeface.BOLD), ks, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(ForegroundColorSpan(COLOR_H_DIM), ks, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.append("   ")
                appendInline(out, t.substring(idx + 1).trim())
            } else {
                appendInline(out, t)
            }
        }
        if (!scan.closed) {
            out.append("  ")
            appendDiagnosticChip(out, "بلوك ؛؛؛ غير مغلق")
        }
        val cardEnd = out.length
        out.setSpan(
            RoundedCardSpan(COLOR_TABLE_BG, COLOR_CARD_BORDER, cardStart, cardEnd),
            cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    private class LinkItem(val label: String, val url: String, val icon: String?)

    /** سطر بلوك `روابط`: `تسمية = رابط [| أيقونة]` — الطرف الأيمن رابط http(s)/mailto أو معرّف عنصر/`@user`. */
    private fun parseLinkItem(line: String): LinkItem? {
        val m = linkItemRegex.find(line.trim()) ?: return null
        val parts = m.groupValues[2].split("|", limit = 2).map { it.trim() }
        val target = parts[0]
        val url = if (isSafeLinkUrl(target)) target else resolveElement(target.trim('"', '*'), currentElementRefs)?.url ?: return null
        return LinkItem(m.groupValues[1].trim(), url, parts.getOrNull(1)?.takeIf { it.isNotEmpty() })
    }

    /**
     * بلوكات "تدفّق" بلا بطاقة: `؛؛؛ روابط` (أزرار روابط)، `؛؛؛ شارات` (`تسمية: قيمة | لون` شارات ثنائية)،
     * `؛؛؛ دعم` (كل سطر مواصفة دفع/تبرّع كما داخل `؛: :؛`) — عناصرها متجاورة في فقرة واحدة وتلتفّ.
     */
    private fun appendFlowBlock(out: SpannableStringBuilder, type: String, scan: FenceScan) {
        var first = true
        fun gap() { if (!first) out.append("  ") ; first = false }
        for (line in scan.body) {
            val t = line.trim()
            if (t.isEmpty() || t.startsWith("\u061B\u061B")) continue
            gap()
            when (type) {
                "links", "روابط" -> {
                    val item = parseLinkItem(t)
                    if (item == null) appendDiagnosticChip(out, "رابط غير صالح: ${t.take(30)}")
                    else appendActionButton(out, item.label, null, item.icon, "soft", null, LinkButtonClickSpan(item.url))
                }
                "badges", "شارات" -> {
                    val m = badgeItemRegex.find(t)
                    if (m == null) appendDiagnosticChip(out, "شارة غير صالحة: ${t.take(30)}")
                    else {
                        val vp = m.groupValues[2].split("|", limit = 2).map { it.trim() }
                        val color = vp.getOrNull(1)?.takeIf { it.isNotEmpty() }
                        val raw = m.groupValues[1].replace('/', '\u2215') + "/" + vp[0].replace('/', '\u2215') +
                            (if (color != null) "/color=($color)" else "")
                        appendMetaBadge(out, raw)
                    }
                }
                else -> appendPayment(out, "", t, null, null, null)
            }
        }
        if (!scan.closed) {
            out.append("  ")
            appendDiagnosticChip(out, "بلوك ؛؛؛ غير مغلق")
        }
    }

    /** يقرأ بيانات بلوك `؛؛؛ meta`/`بيانات`/`rdoc` (سطر `مفتاح: قيمة`، المفاتيح بأحرف صغيرة) — مثال: title/version/author. */
    fun extractMeta(markdown: String): Map<String, String> {
        val meta = LinkedHashMap<String, String>()
        val lines = markdown.lines()
        var i = 0
        while (i < lines.size) {
            val open = arSemiFenceOpenRegex.find(lines[i].trim())
            if (open == null) { i++; continue }
            val scan = scanDataFence(lines, i)
            val type = open.groupValues[1].trim().split(whitespaceRegex)[0].lowercase()
            if (type == "rdoc" || type == "meta" || type == "بيانات") {
                for (line in scan.body) {
                    val idx = line.indexOf(':')
                    if (idx > 0) meta.putIfAbsent(line.substring(0, idx).trim().lowercase(), line.substring(idx + 1).trim())
                }
            } else if (type == "package" || type == "حزمة") {
                // متغيّرات {{name}} {{version}} … من بلوك الحزمة (بمفاتيحها القانونية).
                for ((k, v) in packageKeys(scan.body, null)) meta.putIfAbsent(k, v)
            }
            i = if (scan.next > i) scan.next else i + 1
        }
        return meta
    }

    // ─────────────────────────── عناصر سطرية: مرجع/تشخيص/أنواع الأقواس ───────────────────────────

    /** شريحة خطأ حمراء مرئية (بدل الصمت) — مرجع مفقود، دفع غير صالح، بلوك غير مغلق... */
    private fun appendDiagnosticChip(out: SpannableStringBuilder, message: String) {
        val base = 0xFFC0392B.toInt()
        val start = out.length
        out.append("\u2716 $message")
        val end = out.length
        out.setSpan(StickerSpan(tintedBackground(base, 0x26), softTextColor(base)), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** `*"id"*` أو `*"id|نص"*` أو `*"@user/lib"*` → رابط العنصر؛ معرّف غير معروف → شريحة خطأ مرئية. */
    private fun appendElementLink(out: SpannableStringBuilder, raw: String) {
        val parts = raw.split("|", limit = 3)
        val key = parts[0].trim()
        val ref = resolveElement(key, currentElementRefs)
        if (ref == null) {
            appendDiagnosticChip(out, "مرجع مفقود: $key")
            return
        }
        val shown = parts.getOrNull(1)?.trim().orEmpty().ifBlank { ref.label.ifBlank { key } }
        // الجزء الثالث اختياري: button/زر (زر ممتلئ) أو soft/outline/solid — وإلا رابط نصي عادي.
        when (val style = parts.getOrNull(2)?.trim()?.lowercase()) {
            null, "" -> appendLink(out, shown, ref.url)
            "button", "زر" -> appendActionButton(out, shown, null, null, null, null, LinkButtonClickSpan(ref.url))
            "soft", "outline", "solid" -> appendActionButton(out, shown, null, null, style, null, LinkButtonClickSpan(ref.url))
            else -> appendLink(out, shown, ref.url)
        }
    }

    /** ستايل وسم `[نوع: قيمة|size=(lg)|style=(outline)…]` — يُقرأ من [appendChip] أثناء رسم القوس الحالي فقط. */
    private class TagStyle(
        val size: Float,
        val style: String?,
        val color: Int?,
        val shape: String?,
        val icon: String?,
        val iconEnd: Boolean
    )

    private var activeTagStyle: TagStyle? = null

    private val TAG_OPTION_KEYS = setOf("size", "style", "color", "shape", "icon", "pos")

    private val TAG_SIZES: Map<String, Float> = mapOf(
        "xs" to 0.8f, "sm" to 0.9f, "md" to 1f, "lg" to 1.25f, "xl" to 1.5f, "xxl" to 1.8f,
        "صغير" to 0.9f, "متوسط" to 1f, "كبير" to 1.25f, "ضخم" to 1.5f
    )
    private val TAG_STYLES: Map<String, String> = mapOf(
        "soft" to "soft", "outline" to "outline", "solid" to "solid", "ghost" to "ghost",
        "خفيف" to "soft", "إطار" to "outline", "ممتلئ" to "solid", "شفاف" to "ghost"
    )
    private val TAG_SHAPES: Map<String, String> = mapOf(
        "pill" to "pill", "round" to "round", "square" to "square",
        "كبسولة" to "pill", "مدور" to "round", "مربع" to "square"
    )

    /** يفصل أجزاء `key=(قيمة)` الخاصة بالستايل عن قيمة القوس، ويُبقي غيرها (مثل height/type) كما هي. */
    private fun splitTagOptions(value: String): Pair<String, Map<String, String>> {
        if (!value.contains('=')) return value to emptyMap()
        val opts = LinkedHashMap<String, String>()
        val kept = ArrayList<String>()
        for (part in value.split("|")) {
            val kv = metaBadgeKeyValueRegex.find(part.trim())
            if (kv != null && kv.groupValues[1].lowercase() in TAG_OPTION_KEYS) {
                opts[kv.groupValues[1].lowercase()] = kv.groupValues[2].trim()
            } else {
                kept.add(part)
            }
        }
        return kept.joinToString("|").trim() to opts
    }

    /** يبني [TagStyle] من الخيارات؛ null إن كانت قيمة أي خيار غير صالحة. */
    private fun buildTagStyle(opts: Map<String, String>): TagStyle? {
        var size = 1f
        opts["size"]?.let { raw ->
            val key = raw.lowercase()
            size = TAG_SIZES[key] ?: (key.removeSuffix("%").toFloatOrNull()?.takeIf { it in 50f..300f }?.div(100f)) ?: return null
        }
        val style = opts["style"]?.let { TAG_STYLES[it.lowercase()] ?: return null }
        val shape = opts["shape"]?.let { TAG_SHAPES[it.lowercase()] ?: return null }
        val color = opts["color"]?.let { (parseColorValue(it) ?: return null).first }
        val icon = opts["icon"]?.let { iconGlyphFor(it) ?: return null }
        val iconEnd = when (opts["pos"]?.lowercase()) {
            null, "start", "بداية" -> false
            "end", "نهاية" -> true
            else -> return null
        }
        return TagStyle(size, style, color, shape, icon, iconEnd)
    }

    private fun appendChip(
        out: SpannableStringBuilder, text: String, bg: Int, fg: Int,
        mono: Boolean = false, extra: CharacterStyle? = null
    ) {
        val ts = activeTagStyle
        var bgc = bg
        var fgc = fg
        var strokeC = 0
        var strokeW = 0f
        var radius = 10f
        var shown = text
        if (ts != null) {
            val base = ts.color
            when (ts.style) {
                "outline" -> { val c = base ?: fg; bgc = 0; fgc = c; strokeC = c; strokeW = 2f }
                "solid" -> { val c = base ?: fg; bgc = c; fgc = contrastingTextColor(c) }
                "ghost" -> { bgc = 0; fgc = base ?: fg }
                else -> if (base != null) { bgc = tintedBackground(base, 0x26); fgc = softTextColor(base) }
            }
            radius = when (ts.shape) { "square" -> 3f; "round" -> 8f; "pill" -> 40f; else -> 10f }
            if (ts.icon != null) shown = if (ts.iconEnd) "$text ${ts.icon}" else "${ts.icon} $text"
        }
        val start = out.length
        out.append(shown)
        val end = out.length
        out.setSpan(StickerSpan(bgc, fgc, radius, 14f, 4f, strokeC, strokeW), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (mono) out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.86f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (extra != null) out.setSpan(extra, start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private val emailRegex = Regex("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$")

    /** `[نوع: قيمة]` — النوع من [TYPED_BRACKET_TYPES]؛ قيمة فارغة أو نوع غير معروف يبقى نصاً كما كُتب. */
    /**
     * `[نوع: قيمة]` مع ستايل اختياري بعد `|` على شكل `key=(قيمة)`:
     * `size` (xs/sm/md/lg/xl/xxl أو 50–300%)، `style` (soft/outline/solid/ghost)، `color`، `shape` (pill/round/square)،
     * `icon` (اسم أيقونة)، `pos` (start/end لمكان الأيقونة). الحجم يسري على كل الأنواع؛ اللون والستايل والشكل والأيقونة
     * على الأنواع المرسومة كشرائح (tag/user/lib/file/date/place/flag/dim…). خيار غير صالح → شريحة خطأ حمراء.
     */
    private fun appendTypedBracket(out: SpannableStringBuilder, rawType: String, value: String, original: String) {
        val kind = TYPED_BRACKET_TYPES[rawType.trim().lowercase()]
        if (kind == null || value.isBlank()) { out.append(original); return }
        val (clean, opts) = splitTagOptions(value.trim())
        val style = if (opts.isEmpty()) null else (buildTagStyle(opts) ?: run {
            appendDiagnosticChip(out, "خيار ستايل غير صالح: $original")
            return
        })
        val start = out.length
        activeTagStyle = style
        try {
            renderTypedBracket(out, rawType, clean, original)
        } finally {
            activeTagStyle = null
        }
        if (style != null && style.size != 1f && out.length > start) {
            out.setSpan(RelativeSizeSpan(style.size), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    private val sizeUnitRegex = Regex("^\\d+(?:[.,]\\d+)?\\s*(?:B|KB|MB|GB|TB|KiB|MiB|GiB|TiB|بايت|كب|ميغا|جيجا)$", RegexOption.IGNORE_CASE)
    private val dimRegex = Regex("^\\d+(?:[.,]\\d+)?\\s*[x\u00D7*]\\s*\\d+(?:[.,]\\d+)?(?:\\s*[x\u00D7*]\\s*\\d+(?:[.,]\\d+)?)?\\s*(?:px|cm|mm|m|in|dp|سم|مم)?$", RegexOption.IGNORE_CASE)
    private val distanceRegex = Regex("^\\d+(?:[.,]\\d+)?\\s*(?:km|m|mi|cm|mm|ft|كم|م|ميل)$", RegexOption.IGNORE_CASE)
    private val weightRegex = Regex("^\\d+(?:[.,]\\d+)?\\s*(?:kg|g|mg|lb|oz|t|كجم|جم)$", RegexOption.IGNORE_CASE)
    private val tempRegex = Regex("^-?\\d+(?:[.,]\\d+)?\\s*(?:\u00B0\\s*[CFK]?|[CFK]|\u00B0)$", RegexOption.IGNORE_CASE)
    private val percentRegex = Regex("^\\d{1,4}(?:[.,]\\d+)?\\s*%?$")
    private val zoneRegex = Regex("^(?:(?:UTC|GMT)\\s*)?[+-]\\d{1,2}(?::\\d{2})?$|^[A-Za-z]+(?:/[A-Za-z_+-]+)+$|^(?:UTC|GMT)$", RegexOption.IGNORE_CASE)
    private val hashRegex = Regex("^[A-Fa-f0-9]{7,128}$")
    private val coordsRegex = Regex("^-?\\d{1,3}(?:\\.\\d+)?\\s*[, ]\\s*-?\\d{1,3}(?:\\.\\d+)?$")
    private val flagCodeRegex = Regex("^[A-Za-z]{2}$")

    private fun parseLatLng(raw: String): Pair<Double, Double>? {
        val parts = raw.trim().split(Regex("[,\\s]+")).filter { it.isNotEmpty() }
        if (parts.size != 2) return null
        val lat = parts[0].toDoubleOrNull() ?: return null
        val lon = parts[1].toDoubleOrNull() ?: return null
        return if (lat in -90.0..90.0 && lon in -180.0..180.0) lat to lon else null
    }

    private fun flagEmoji(code: String): String {
        val c = code.uppercase()
        return String(Character.toChars(0x1F1E6 + (c[0] - 'A'))) + String(Character.toChars(0x1F1E6 + (c[1] - 'A')))
    }

    private fun renderTypedBracket(out: SpannableStringBuilder, rawType: String, value: String, original: String) {
        val kind = TYPED_BRACKET_TYPES[rawType.trim().lowercase()]
        val v = value.trim()
        if (kind == null || v.isEmpty()) { out.append(original); return }
        fun bad() = appendDiagnosticChip(out, "قيمة غير صالحة: $original")
        val accentBg = tintedBackground(COLOR_BULLET, 0x26)
        val accentFg = softTextColor(COLOR_BULLET)
        when (kind) {
            "tag" -> appendChip(out, "#$v", accentBg, accentFg)
            "user" -> {
                val url = canonicalRinUrl(if (v.startsWith("@")) v else "@$v")
                if (url == null) bad()
                else appendChip(out, if (v.startsWith("@")) v else "@$v", accentBg, accentFg, extra = LinkButtonClickSpan(url))
            }
            "lib" -> {
                val url = canonicalRinUrl(if (v.startsWith("@")) v else "@$v")
                if (url == null) bad()
                else appendChip(out, "\uD83D\uDCE6 ${if (v.startsWith("@")) v else "@$v"}", accentBg, accentFg, extra = LinkButtonClickSpan(url))
            }
            "key" -> {
                val keys = v.split("+").map { it.trim() }.filter { it.isNotEmpty() }
                if (keys.isEmpty()) bad()
                keys.forEachIndexed { idx, k -> if (idx > 0) out.append(" + "); appendKeycap(out, k) }
            }
            "file" -> appendChip(out, "\uD83D\uDCC4 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT, mono = true)
            "date" -> appendChip(out, "\uD83D\uDCC5 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
            "version" -> {
                val label = "إصدار"
                val start = out.length
                out.append("$label $v")
                out.setSpan(
                    BadgeTwoToneSpan(label, v, COLOR_INLINE_CODE_BG, COLOR_H_DIM, COLOR_BULLET),
                    start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }
            "price" -> {
                val gold = 0xFFFFC94D.toInt()
                appendChip(out, "\uD83D\uDCB2 $v", tintedBackground(gold, 0x33), COLOR_SYNTAX_BUILTIN)
            }
            "abbr" -> {
                val parts = v.split("|", limit = 2)
                val term = parts[0].trim()
                val meaning = parts.getOrNull(1)?.trim().orEmpty()
                val start = out.length
                out.append(term)
                if (meaning.isNotEmpty()) {
                    out.setSpan(UnderlineSpan(), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(ForegroundColorSpan(COLOR_LINK), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(ToastClickSpan(meaning), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                }
            }
            "note" -> appendChip(out, "\u24D8 $v", tintedBackground(0xFF3B9EFF.toInt(), 0x26), softTextColor(0xFF3B9EFF.toInt()))
            "cmd" -> appendChip(
                out, "$ $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT, mono = true,
                extra = CopyCodeSpan(v, "تم نسخ الأمر")
            )
            "email" -> {
                if (emailRegex.matches(v)) appendChip(out, "\u2709 $v", accentBg, accentFg, extra = LinkButtonClickSpan("mailto:$v"))
                else bad()
            }
            "progress", "rating", "status", "count" -> {
                val parts = v.split("|", limit = 2).map { it.trim() }
                val num = parts[0]
                val lbl = parts.getOrNull(1).orEmpty().replace('/', '\u2215')
                val ok = when (kind) {
                    "status" -> num.isNotEmpty()
                    "count" -> num.replace(",", "").toLongOrNull() != null
                    else -> num.replace(',', '.').toDoubleOrNull() != null
                }
                if (!ok) { bad(); return }
                when (kind) {
                    "progress" -> appendMetaBadge(out, "${lbl.ifBlank { "التقدم" }}/progress=($num)")
                    "rating" -> appendMetaBadge(out, "${lbl.ifBlank { "التقييم" }}/rating=($num)")
                    "status" -> appendMetaBadge(out, "${lbl.ifBlank { "الحالة" }}/status=($num)")
                    else -> appendMetaBadge(out, "${lbl.ifBlank { "تنزيلات" }}/downloads=($num)")
                }
            }
            "color" -> {
                val parts = v.split("|", limit = 2).map { it.trim() }
                val hex = parts.last()
                if (parseHexColor(hex) == null) { bad(); return }
                appendMetaBadge(out, if (parts.size == 2) "${parts[0].replace('/', '\u2215')}/$hex" else hex)
            }
            "link", "download" -> {
                val parts = v.split("|", limit = 2).map { it.trim() }
                val url = parts[0]
                if (!isSafeLinkUrl(url)) { bad(); return }
                val text = parts.getOrNull(1).orEmpty()
                if (kind == "link") appendLink(out, text.ifBlank { url }, url)
                else appendActionButton(out, text.ifBlank { "تحميل" }, null, "download", null, null, LinkButtonClickSpan(url))
            }
            "badge" -> {
                val p = v.split("|").map { it.trim() }.filter { it.isNotEmpty() }
                if (p.isEmpty()) { bad(); return }
                var label: String? = null
                val message: String
                var colorRaw: String? = null
                when {
                    p.size >= 3 -> { label = p[0]; message = p[1]; colorRaw = p[2] }
                    p.size == 2 -> { label = p[0]; message = p[1] }
                    else -> message = p[0]
                }
                val mc = if (colorRaw == null) COLOR_BULLET else (parseColorValue(colorRaw)?.first ?: run { bad(); return })
                val start = out.length
                out.append(if (label != null) "$label $message" else message)
                out.setSpan(
                    ShieldsBadgeSpan(label, message, 0xFF555555.toInt(), mc, contrastingTextColor(mc)),
                    start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                out.setSpan(RelativeSizeSpan(0.8f), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(StyleSpan(Typeface.BOLD), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            "icon" -> {
                val p = v.split("|", limit = 2).map { it.trim() }
                val glyph = iconGlyphFor(p[0]) ?: run { bad(); return }
                val text = p.getOrNull(1).orEmpty()
                appendChip(out, if (text.isBlank()) glyph else "$glyph $text", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
            }
            "time" -> appendChip(out, "\u23F1 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
            "size" -> when {
                sizeUnitRegex.matches(v) -> appendChip(out, "\uD83D\uDCBE $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
                dimRegex.matches(v) -> appendChip(out, "\uD83D\uDCD0 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
                else -> bad()
            }
            "dim" -> if (dimRegex.matches(v)) appendChip(out, "\uD83D\uDCD0 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "distance" -> if (distanceRegex.matches(v)) appendChip(out, "\uD83D\uDCCF $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "weight" -> if (weightRegex.matches(v)) appendChip(out, "\uD83D\uDCE6 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "temp" -> if (tempRegex.matches(v)) appendChip(out, "\uD83C\uDF21 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "percent" -> if (percentRegex.matches(v)) appendChip(out, "${v.trimEnd('%').trim()}%", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "zone" -> if (zoneRegex.matches(v)) appendChip(out, "\uD83D\uDD52 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "hash" -> if (hashRegex.matches(v)) {
                appendChip(out, "\u2318 ${v.take(8)}", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT, mono = true, extra = CopyCodeSpan(v, "تم نسخ البصمة"))
            } else bad()
            "address" -> appendChip(out, "\uD83C\uDFE0 $v", accentBg, accentFg, extra = CopyCodeSpan(v, "تم نسخ العنوان"))
            "flag" -> {
                val p = v.split("|", limit = 2).map { it.trim() }
                if (!flagCodeRegex.matches(p[0])) { bad(); return }
                appendChip(out, "${flagEmoji(p[0])} ${p.getOrNull(1).orEmpty().ifBlank { p[0].uppercase() }}", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
            }
            "place" -> {
                val p = v.split("|", limit = 2).map { it.trim() }
                var name = p[0]
                var coordRaw = p.getOrNull(1).orEmpty()
                if (coordRaw.isEmpty() && coordsRegex.matches(name)) { coordRaw = name; name = "" }
                val ll = if (coordRaw.isNotEmpty()) (parseLatLng(coordRaw) ?: run { bad(); return }) else null
                val url = if (ll != null) {
                    "https://www.openstreetmap.org/?mlat=${ll.first}&mlon=${ll.second}#map=15/${ll.first}/${ll.second}"
                } else {
                    "https://www.openstreetmap.org/search?query=${Uri.encode(name)}"
                }
                val label = name.ifBlank { ll?.let { "${it.first}, ${it.second}" } ?: v }
                appendChip(out, "\uD83D\uDCCD $label", accentBg, accentFg, extra = LinkButtonClickSpan(url))
            }
            "license" -> appendChip(out, "\u2696 $v", tintedBackground(0xFF22C88E.toInt(), 0x26), softTextColor(0xFF22C88E.toInt()))
            "platform" -> {
                val names = v.split("|", ",").map { it.trim() }.filter { it.isNotEmpty() }
                if (names.isEmpty()) { bad(); return }
                names.forEachIndexed { idx, n ->
                    if (idx > 0) out.append(" ")
                    appendChip(out, "${PLATFORM_GLYPHS[n.lowercase()] ?: "\u25CF"} $n", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
                }
            }
            "lang" -> {
                val accent = LANGUAGE_ACCENTS[v.lowercase()]?.let { adaptForTheme(it) } ?: COLOR_CODE_TEXT
                appendChip(out, "\u25CF $v", tintedBackground(accent, 0x26), softTextColor(accent), mono = true)
            }
            "copy" -> {
                val p = v.split("|", limit = 2).map { it.trim() }
                val label = p.getOrNull(1).orEmpty().ifBlank { p[0] }
                appendChip(out, "\u29C9 $label", COLOR_COPY_BUTTON_BG, COLOR_CODE_TEXT, extra = CopyCodeSpan(p[0], "تم النسخ"))
            }
            "phone" -> {
                if (phoneRegex.matches(v)) {
                    appendChip(out, "\u260E $v", accentBg, accentFg, extra = LinkButtonClickSpan("tel:" + v.filter { it.isDigit() || it == '+' }))
                } else bad()
            }
            "video", "web" -> appendMediaInline(out, "$kind $v")
            "since" -> if (versionRegex.matches(v)) appendChip(out, "\uD83C\uDFF7 منذ ${v.removePrefix("v")}", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "requires" -> if (requiresRegex.matches(v)) appendChip(out, "\u2699 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT) else bad()
            "dep" -> {
                val dm = depValueRegex.find(v)
                if (dm == null) { bad(); return }
                val depName = dm.groupValues[1]
                val constraint = dm.groupValues[2]
                if (constraint.isNotEmpty() && !constraintRegex.matches(constraint)) { bad(); return }
                val label = "\uD83D\uDD17 $depName" + (if (constraint.isNotEmpty()) " $constraint" else "")
                if (depName.startsWith("@")) {
                    val url = canonicalRinUrl(depName)
                    if (url == null) bad() else appendChip(out, label, accentBg, accentFg, extra = LinkButtonClickSpan(url))
                } else {
                    appendChip(out, label, accentBg, accentFg)
                }
            }
            "install" -> if (pkgNameRegex.matches(v)) {
                appendChip(out, "$ rin install $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT, mono = true, extra = CopyCodeSpan("rin install $v", "تم نسخ الأمر"))
            } else bad()
            "author" -> {
                if (v.startsWith("@")) {
                    val url = canonicalRinUrl(v)
                    if (url == null) bad() else appendChip(out, "\uD83D\uDC64 $v", accentBg, accentFg, extra = LinkButtonClickSpan(url))
                } else {
                    appendChip(out, "\uD83D\uDC64 $v", COLOR_INLINE_CODE_BG, COLOR_CODE_TEXT)
                }
            }
            "repo" -> {
                val p = v.split("|", limit = 2).map { it.trim() }
                if (!isHttpsUrl(p[0])) { bad(); return }
                val label = p.getOrNull(1).orEmpty().ifBlank { hostOf(p[0]).ifBlank { p[0] } }
                appendChip(out, "\uD83D\uDDC2 $label", accentBg, accentFg, extra = LinkButtonClickSpan(p[0]))
            }
            "marquee" -> {
                val opts = LinkedHashMap<String, String>()
                val texts = ArrayList<String>()
                for (pt in v.split("|").map { it.trim() }.filter { it.isNotEmpty() }) {
                    val kv = metaBadgeKeyValueRegex.find(pt)
                    if (kv != null) opts[kv.groupValues[1].lowercase()] = kv.groupValues[2] else texts.add(pt)
                }
                val (o, err) = parseTickerOptions(opts, activeTagStyle)
                if (o == null || texts.isEmpty()) {
                    appendDiagnosticChip(out, "شريط غير صالح" + (err?.let { ": $it" } ?: ""))
                    return
                }
                appendRdocTicker(out, texts, o)
            }
            "pay" -> appendPayment(out, "", v, null, null, null)
            else -> {
                if (kind.startsWith("status:")) appendStatusChip(out, kind.removePrefix("status:"), v)
                else out.append(original)
            }
        }
    }

    private val PLATFORM_GLYPHS: Map<String, String> = mapOf(
        "android" to "\uD83E\uDD16", "linux" to "\uD83D\uDC27", "windows" to "\u229E",
        "macos" to "\u2318", "ios" to "\u2318", "web" to "\uD83C\uDF10", "cli" to "\u25B8"
    )

    private val phoneRegex = Regex("^\\+?[0-9][0-9 ()-]{4,19}$")

    /** شريحة حالة بنص مخصّص (`[ok: يعمل]`): لون وتسمية افتراضية من [STATUS_STYLES]. */
    private fun appendStatusChip(out: SpannableStringBuilder, key: String, text: String) {
        val (base, defLabel) = STATUS_STYLES[key] ?: (COLOR_BULLET to key)
        val start = out.length
        out.append("\u25CF ${text.ifBlank { defLabel }}")
        val end = out.length
        out.setSpan(StickerSpan(tintedBackground(base, 0x26), softTextColor(base)), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.84f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** نص ينبثق كتنبيه قصير عند النقر (لنوع `[abbr: HTML|المعنى]`). */
    private class ToastClickSpan(private val text: String) : ClickableSpan() {
        override fun onClick(widget: View) {
            Toast.makeText(widget.context, text, Toast.LENGTH_LONG).show()
        }

        override fun updateDrawState(ds: TextPaint) {}
    }

    // ─────────────────────────── الدفع والتبرّع `؛: … :؛` ───────────────────────────

    private class PaymentSpec(
        val url: String, val host: String, val label: String, val icon: String, val amount: String,
        val copy: String? = null
    )

    /** مزوّدو "انسخ العنوان" (عملات رقمية/IBAN): لا رابط ولا شبكة، النقر ينسخ القيمة للحافظة. */
    private val COPY_PROVIDERS = setOf("btc", "eth", "usdt", "ltc", "sol", "xmr", "crypto", "copy", "iban")
    private val copyTargetRegex = Regex("^[A-Za-z0-9:_+.\\- ]{8,120}$")

    /** مزوّدو الدفع المعروفون → المضيفون المسموح بها لو أُعطي رابط كامل بدل اسم مستخدم. */
    private val PAYMENT_HOSTS: Map<String, List<String>> = mapOf(
        "paypal" to listOf("paypal.com", "paypal.me"),
        "patreon" to listOf("patreon.com"),
        "kofi" to listOf("ko-fi.com"),
        "coffee" to listOf("buymeacoffee.com"),
        "github" to listOf("github.com"),
        "liberapay" to listOf("liberapay.com"),
        "opencollective" to listOf("opencollective.com"),
        "wise" to listOf("wise.com")
    )

    private val PAYMENT_ALIASES: Map<String, String> = mapOf(
        "ko-fi" to "kofi", "buymeacoffee" to "coffee", "bmc" to "coffee", "sponsors" to "github",
        "بايبال" to "paypal", "باي_بال" to "paypal",
        "url" to "url", "link" to "url", "stripe" to "url", "pay" to "url", "رابط" to "url"
    )

    private val paymentHandleRegex = Regex("^[A-Za-z0-9._-]{1,60}$")
    private val paymentAmountRegex = Regex("^(\\d{1,7}(?:[.,]\\d{1,2})?)\\s*([A-Za-z]{3}|[\$€£])?$")

    private fun buildPaymentUrl(provider: String, target: String, amount: Pair<String, String>?): String? {
        val t = target.trim()
        if (t.startsWith("http", ignoreCase = true)) {
            if (!isHttpsUrl(t)) return null
            if (provider == "url") return t
            val allowed = PAYMENT_HOSTS[provider] ?: return null
            val host = hostOf(t)
            return if (allowed.any { host == it || host.endsWith(".$it") }) t else null
        }
        if (provider == "url" || !paymentHandleRegex.matches(t)) return null
        return when (provider) {
            "paypal" -> {
                val suffix = if (amount != null && amount.second.isNotEmpty()) "/${amount.first.replace(',', '.')}${amount.second}" else ""
                "https://www.paypal.me/$t$suffix"
            }
            "patreon" -> "https://www.patreon.com/$t"
            "kofi" -> "https://ko-fi.com/$t"
            "coffee" -> "https://www.buymeacoffee.com/$t"
            "github" -> "https://github.com/sponsors/$t"
            "liberapay" -> "https://liberapay.com/$t"
            "opencollective" -> "https://opencollective.com/$t"
            "wise" -> "https://wise.com/pay/me/$t"
            else -> null
        }
    }

    /**
     * `مزوّد | هدف [| مبلغ [عملة]] [| نص الزر] [| type=(donate|pay|subscribe)]` — الهدف اسم مستخدم أو رابط https.
     * أمثلة: `paypal|ahmad|5 USD|ادعمنا` · `patreon|rin` · `url|https://buy.stripe.com/xyz|اشترِ الآن|type=(pay)`.
     */
    private fun parsePayment(raw: String): PaymentSpec? {
        val parts = raw.split("|").map { it.trim() }.filter { it.isNotEmpty() }
        if (parts.size < 2) return null
        val provider = PAYMENT_ALIASES[parts[0].lowercase()] ?: parts[0].lowercase()
        if (provider in COPY_PROVIDERS) {
            if (!copyTargetRegex.matches(parts[1])) return null
            val lbl = parts.drop(2).firstOrNull { !metaBadgeKeyValueRegex.matches(it) }.orEmpty()
            return PaymentSpec("", provider.uppercase(), lbl.ifBlank { "انسخ العنوان" }, "copy", "", parts[1])
        }
        var amount: Pair<String, String>? = null
        var label = ""
        var type = "donate"
        for (p in parts.drop(2)) {
            val kv = metaBadgeKeyValueRegex.find(p)
            if (kv != null) {
                if (kv.groupValues[1].equals("type", ignoreCase = true)) type = kv.groupValues[2].trim().lowercase()
                continue
            }
            val am = paymentAmountRegex.find(p)
            if (am != null && amount == null) {
                val cur = when (am.groupValues[2]) {
                    "\$" -> "USD"
                    "€" -> "EUR"
                    "£" -> "GBP"
                    else -> am.groupValues[2].uppercase()
                }
                amount = am.groupValues[1] to cur
            } else if (label.isEmpty()) {
                label = p
            }
        }
        val url = buildPaymentUrl(provider, parts[1], amount) ?: return null
        val host = hostOf(url)
        if (host.isEmpty()) return null
        val defaultLabel = when (type) {
            "pay" -> "ادفع"
            "subscribe" -> "اشترك"
            else -> "تبرّع"
        }
        val icon = when (type) {
            "pay" -> "lock"
            "subscribe" -> "star"
            else -> "heart"
        }
        val amountText = amount?.let { (n, c) -> if (c.isNotEmpty()) "$n $c" else n }.orEmpty()
        return PaymentSpec(url, host, label.ifBlank { defaultLabel }, icon, amountText)
    }

    /** زر دفع/تبرّع: يعرض المضيف الحقيقي على الزر نفسه، وعند النقر يطلب تأكيداً قبل فتح الرابط. */
    private fun appendPayment(
        out: SpannableStringBuilder, label: String, raw: String,
        color: Pair<Int, String>?, styleKey: String?, sizeKey: String?
    ) {
        val spec = parsePayment(raw)
        if (spec == null) {
            appendDiagnosticChip(out, "دفع غير صالح")
            return
        }
        if (spec.copy != null) {
            appendActionButton(
                out, "${label.ifBlank { spec.label }} \u00B7 ${spec.host}", color ?: (0xFFFFC94D.toInt() to "gold"),
                "copy", styleKey, sizeKey, CopyCodeSpan(spec.copy, "تم نسخ العنوان")
            )
            return
        }
        val amountPart = if (spec.amount.isNotEmpty()) " ${spec.amount}" else ""
        val shown = "${label.ifBlank { spec.label }}$amountPart \u00B7 ${spec.host}"
        appendActionButton(
            out, shown, color ?: (0xFFFFC94D.toInt() to "gold"), spec.icon, styleKey, sizeKey,
            PaymentClickSpan(spec.url, spec.host)
        )
    }

    private class PaymentClickSpan(private val url: String, private val host: String) : ClickableSpan() {
        override fun onClick(widget: View) {
            val ctx = widget.context
            try {
                AlertDialog.Builder(ctx)
                    .setTitle("متابعة إلى صفحة دفع خارجية؟")
                    .setMessage(
                        "الوجهة: $host\n\n$url\n\nوضع هذا الرابطَ ناشرُ الحزمة ولا تتحقق Rin منه. " +
                            "لا تُدخل بيانات بطاقتك إلا إن كنت تثق بالناشر."
                    )
                    .setPositiveButton("متابعة") { _, _ ->
                        try {
                            ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
                        } catch (t: Throwable) {
                        }
                    }
                    .setNegativeButton("إلغاء", null)
                    .show()
            } catch (t: Throwable) {
            }
        }

        override fun updateDrawState(ds: TextPaint) {}
    }

    // ─────────────────────────── الفيديو وويب فيو `:; … ;:` ───────────────────────────
    // يوتيوب احترافي: يحلّل كل صيغ الروابط (watch/youtu.be/shorts/live/embed/playlist/music)، يقرأ وقت البدء من الرابط
    // (`?t=1m30s`)، ويدعم خيارات التضمين (start/end/autoplay/mute/loop/captions/lang/related/controls/privacy) ونسبة أبعاد
    // حقيقية (16:9، وshorts بـ9:16)، وملصقاً بزر تشغيل أحمر (+ صورة مصغّرة اختيارية thumb=(true))، وملء الشاشة، وزر
    // "فتح في يوتيوب"، وإيقاف المشغّل عند مغادرة الشاشة. الصفحة تُحمَّل عبر iframe بأصل https صحيح (تفادي خطأ 153).

    private val MEDIA_KIND_ALIASES: Map<String, String> = mapOf(
        "video" to "video", "فيديو" to "video", "web" to "web", "ويب" to "web", "frame" to "web"
    )
    private val TRUSTED_VIDEO_HOSTS = listOf(
        "youtube.com", "youtube-nocookie.com", "youtu.be", "player.vimeo.com", "vimeo.com", "dailymotion.com"
    )
    private val mediaKindPrefixRegex = Regex("^(\\S+)\\s+(https?://\\S+)$")
    private val ytIdRegex = Regex("^[A-Za-z0-9_-]{11}$")
    private val ytListRegex = Regex("^[A-Za-z0-9_-]{10,64}$")
    private val ytTimeRegex = Regex("^(?:(\\d{1,3})h)?(?:(\\d{1,2})m)?(?:(\\d{1,2})s)?$")
    private val ytClockRegex = Regex("^(\\d{1,3}):(\\d{1,2})(?::(\\d{1,2}))?$")
    private val ytFragmentTimeRegex = Regex("(?:^|&)t=([^&]+)")
    private val mediaLangRegex = Regex("^[a-z]{2}(?:-[a-z]{2})?$")
    private val mediaRatioRegex = Regex("^(\\d{1,2}(?:\\.\\d+)?)\\s*[:/x]\\s*(\\d{1,2}(?:\\.\\d+)?)$")

    private const val YT_MAX_SECONDS = 172_800
    private const val YT_ORIGIN = "https://dlof-lib.github.io"
    private val YT_ONLY_KEYS = setOf("start", "end", "autoplay", "mute", "loop", "captions", "lang", "related", "controls", "privacy", "thumb")

    private val ytThumbCache = android.util.LruCache<String, Bitmap>(8)

    private fun isTrustedVideoHost(host: String): Boolean =
        TRUSTED_VIDEO_HOSTS.any { host == it || host.endsWith(".$it") }

    private fun isYoutubeHost(host: String): Boolean =
        host == "youtu.be" || host == "youtube.com" || host.endsWith(".youtube.com") ||
            host == "youtube-nocookie.com" || host.endsWith(".youtube-nocookie.com")

    private fun toEmbedUrl(url: String, host: String): String {
        val u = Uri.parse(url)
        return when (host) {
            "vimeo.com" -> u.lastPathSegment?.takeIf { s -> s.isNotEmpty() && s.all { c -> c.isDigit() } }
                ?.let { "https://player.vimeo.com/video/$it" } ?: url
            else -> url
        }
    }

    /** `90` · `90s` · `1m30s` · `1h2m3s` · `1:30` · `1:02:03` → ثوانٍ (0..48h)، أو null. */
    private fun parseYtTime(raw: String): Int? {
        val s = raw.trim().lowercase()
        if (s.isEmpty()) return null
        s.toIntOrNull()?.let { return it.takeIf { v -> v in 0..YT_MAX_SECONDS } }
        ytClockRegex.find(s)?.let { m ->
            val a = m.groupValues[1].toInt()
            val b = m.groupValues[2].toInt()
            val c = m.groupValues[3].toIntOrNull()
            val total = if (c == null) a * 60 + b else a * 3600 + b * 60 + c
            return total.takeIf { it in 0..YT_MAX_SECONDS }
        }
        val m = ytTimeRegex.find(s) ?: return null
        if (m.value.isEmpty()) return null
        val total = (m.groupValues[1].toIntOrNull() ?: 0) * 3600 +
            (m.groupValues[2].toIntOrNull() ?: 0) * 60 + (m.groupValues[3].toIntOrNull() ?: 0)
        return total.takeIf { it in 0..YT_MAX_SECONDS }
    }

    private fun formatClock(sec: Int): String {
        val h = sec / 3600
        val m = (sec % 3600) / 60
        val s = sec % 60
        return if (h > 0) "%d:%02d:%02d".format(h, m, s) else "%d:%02d".format(m, s)
    }

    private fun parseBool(v: String): Boolean? = when (v.trim().lowercase()) {
        "true", "yes", "1", "on", "نعم" -> true
        "false", "no", "0", "off", "لا" -> false
        else -> null
    }

    private fun parseRatio(v: String): Float? {
        val s = v.trim()
        val m = mediaRatioRegex.find(s)
        val r = if (m != null) {
            val a = m.groupValues[1].toFloat()
            val b = m.groupValues[2].toFloat()
            if (b == 0f) return null
            a / b
        } else {
            s.toFloatOrNull() ?: return null
        }
        return r.takeIf { it in 0.4f..3f }
    }

    private class YtRef(val videoId: String?, val listId: String?, val startSec: Int?, val short: Boolean)

    /** يستخرج معرّف الفيديو/القائمة/وقت البدء من أي صيغة رابط يوتيوب؛ null إن لم يوجد فيديو أو قائمة صالحان. */
    private fun parseYoutubeRef(url: String, host: String): YtRef? {
        val u = Uri.parse(url)
        val seg = u.pathSegments
        var id: String? = null
        var short = false
        if (host == "youtu.be") {
            id = seg.firstOrNull()
        } else {
            when (seg.firstOrNull()) {
                "watch" -> id = u.getQueryParameter("v")
                "embed", "v", "live" -> id = seg.getOrNull(1)
                "shorts" -> { id = seg.getOrNull(1); short = true }
                else -> {}
            }
        }
        val list = u.getQueryParameter("list")?.takeIf { ytListRegex.matches(it) }
        val tRaw = u.getQueryParameter("t") ?: u.getQueryParameter("start")
            ?: u.fragment?.let { f -> ytFragmentTimeRegex.find(f)?.groupValues?.get(1) }
        val vid = id?.takeIf { ytIdRegex.matches(it) }
        if (vid == null && list == null) return null
        return YtRef(vid, list, tRaw?.let { parseYtTime(it) }, short)
    }

    /**
     * `[نوع] رابط [| عنوان] [| key=(قيمة)…]` — النوع `video`/`فيديو`/`web`/`ويب` اختياري. https فقط.
     * الخيارات العامة: `height` `ratio`. خيارات يوتيوب: `start` `end` `autoplay` `mute` `loop` `captions` `lang` `related`
     * `controls` `privacy` (nocookie الافتراضي) `thumb`. يعيد (الوسائط، null) أو (null، سبب الخطأ).
     */
    private fun parseMediaEx(raw: String): Pair<MarkdownSegment.Media?, String?> {
        val parts = raw.split("|").map { it.trim() }.filter { it.isNotEmpty() }
        if (parts.isEmpty()) return null to "رابط فارغ"
        var kind: String? = null
        var urlPart = parts[0]
        val pm = mediaKindPrefixRegex.find(urlPart)
        if (pm != null) {
            kind = MEDIA_KIND_ALIASES[pm.groupValues[1].lowercase()] ?: return null to "نوع وسائط مجهول: ${pm.groupValues[1]}"
            urlPart = pm.groupValues[2]
        }
        if (!isHttpsUrl(urlPart)) return null to "https فقط: $urlPart"
        val host = hostOf(urlPart)
        if (host.isEmpty()) return null to "مضيف غير صالح"

        var title = ""
        val opts = LinkedHashMap<String, String>()
        for (p in parts.drop(1)) {
            val kv = metaBadgeKeyValueRegex.find(p)
            if (kv != null) opts[kv.groupValues[1].lowercase()] = kv.groupValues[2].trim()
            else if (title.isEmpty()) title = p
        }

        var yt: YtRef? = null
        if (isYoutubeHost(host)) {
            yt = parseYoutubeRef(urlPart, host) ?: return null to "رابط يوتيوب بلا معرّف فيديو (11 خانة) أو قائمة صالحة"
        }
        if (yt == null) {
            val bad = opts.keys.firstOrNull { it in YT_ONLY_KEYS }
            if (bad != null) return null to "الخيار $bad لروابط يوتيوب فقط"
        }

        var height = 0
        var ratio = 0f
        var start = yt?.startSec ?: 0
        var end = 0
        var autoplay = true
        var mute = false
        var loop = false
        var captions = false
        var lang: String? = null
        var related = false
        var controls = true
        var thumb = false
        var privacy = true
        for ((k, v) in opts) {
            when (k) {
                "height" -> height = v.removeSuffix("dp").trim().toIntOrNull()?.takeIf { it in 120..640 } ?: return null to "height=($v)"
                "ratio" -> ratio = parseRatio(v) ?: return null to "ratio=($v)"
                "start" -> start = parseYtTime(v) ?: return null to "start=($v)"
                "end" -> end = parseYtTime(v) ?: return null to "end=($v)"
                "autoplay" -> autoplay = parseBool(v) ?: return null to "autoplay=($v)"
                "mute" -> mute = parseBool(v) ?: return null to "mute=($v)"
                "loop" -> loop = parseBool(v) ?: return null to "loop=($v)"
                "captions" -> captions = parseBool(v) ?: return null to "captions=($v)"
                "related" -> related = parseBool(v) ?: return null to "related=($v)"
                "controls" -> controls = parseBool(v) ?: return null to "controls=($v)"
                "thumb" -> thumb = parseBool(v) ?: return null to "thumb=($v)"
                "lang" -> lang = v.lowercase().takeIf { mediaLangRegex.matches(it) } ?: return null to "lang=($v)"
                "privacy" -> privacy = when (v.lowercase()) {
                    "nocookie", "private", "خاص" -> true
                    "standard", "عادي" -> false
                    else -> return null to "privacy=($v)"
                }
                else -> return null to "خيار وسائط غير معروف: $k"
            }
        }
        if (end in 1..start) return null to "end يجب أن يأتي بعد start"

        val finalKind = kind ?: if (isTrustedVideoHost(host)) "video" else "web"
        var embed: String
        var watch = urlPart
        var thumbId: String? = null
        val ref = yt
        if (ref != null) {
            val base = if (privacy) "https://www.youtube-nocookie.com" else "https://www.youtube.com"
            val vid = ref.videoId
            val list = ref.listId
            val q = ArrayList<String>()
            if (list != null) q.add("list=$list")
            q.add("playsinline=1")
            q.add("modestbranding=1")
            q.add("iv_load_policy=3")
            if (!related) q.add("rel=0")
            if (autoplay) q.add("autoplay=1")
            if (mute) q.add("mute=1")
            if (start > 0) q.add("start=$start")
            if (end > 0) q.add("end=$end")
            if (loop) {
                q.add("loop=1")
                if (vid != null && list == null) q.add("playlist=$vid")
            }
            if (captions) q.add("cc_load_policy=1")
            lang?.let { q.add("hl=$it"); q.add("cc_lang_pref=$it") }
            if (!controls) q.add("controls=0")
            q.add("origin=$YT_ORIGIN")
            embed = base + (if (vid != null) "/embed/$vid" else "/embed/videoseries") + "?" + q.joinToString("&")
            watch = if (vid != null) {
                "https://www.youtube.com/watch?v=$vid" + (if (start > 0) "&t=${start}s" else "") + (if (list != null) "&list=$list" else "")
            } else {
                "https://www.youtube.com/playlist?list=$list"
            }
            thumbId = vid
            if (ratio == 0f && height == 0) ratio = if (ref.short) 9f / 16f else 16f / 9f
        } else {
            embed = toEmbedUrl(urlPart, host)
        }
        val h = if (height > 0) height else if (finalKind == "video") 210 else 320
        return MarkdownSegment.Media(finalKind, urlPart, embed, title, h, ratio, ref != null, watch, start, thumbId, thumb) to null
    }

    private fun parseMedia(raw: String): MarkdownSegment.Media? = parseMediaEx(raw).first

    /** `:; … ;:` داخل فقرة (لا على سطر مستقل): زر يفتح الرابط (يوتيوب: بوقت البدء) — التضمين الحيّ للأسطر المستقلة فقط. */
    private fun appendMediaInline(out: SpannableStringBuilder, raw: String) {
        val (m, err) = parseMediaEx(raw)
        if (m == null) {
            appendDiagnosticChip(out, "وسائط غير صالحة: $err")
            return
        }
        val name = m.title.ifBlank { if (m.youtube) "YouTube" else hostOf(m.url) }
        val at = if (m.startSec > 0) " \u00B7 ${formatClock(m.startSec)}" else ""
        val label = (if (m.kind == "video") "\u25B6 " else "\uD83C\uDF10 ") + name + at
        appendActionButton(out, label, null, null, "soft", null, LinkButtonClickSpan(m.watchUrl.ifBlank { m.url }))
    }

    private fun escapeHtmlAttr(s: String): String =
        s.replace("&", "&amp;").replace("\"", "&quot;").replace("<", "&lt;").replace(">", "&gt;")

    /** صفحة تغلّف iframe التضمين؛ تُحمَّل بأصل https (loadDataWithBaseURL) وسياسة referrer صحيحة لقبول يوتيوب للتضمين. */
    private fun ytWrapperHtml(embedUrl: String, title: String): String {
        val src = escapeHtmlAttr(embedUrl)
        val name = escapeHtmlAttr(title.ifBlank { "YouTube video" })
        return "<!doctype html><html><head><meta charset=\"utf-8\">" +
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" +
            "<style>html,body{margin:0;height:100%;background:#000;overflow:hidden}" +
            "iframe{position:absolute;top:0;left:0;width:100%;height:100%;border:0}</style></head><body>" +
            "<iframe src=\"$src\" title=\"$name\" " +
            "allow=\"accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share\" " +
            "referrerpolicy=\"strict-origin-when-cross-origin\" allowfullscreen></iframe></body></html>"
    }

    private fun findActivity(ctx: Context): android.app.Activity? {
        var c: Context? = ctx
        while (c is android.content.ContextWrapper) {
            if (c is android.app.Activity) return c
            c = c.baseContext
        }
        return null
    }

    /** صورة مصغّرة يوتيوب (اختيارية `thumb=(true)`): تُجلب في خيط خلفي وتُخزَّن مؤقتاً، ولا شيء يُجلب بدونها. */
    private fun loadYoutubeThumb(id: String, iv: ImageView) {
        ytThumbCache.get(id)?.let { iv.setImageBitmap(it); return }
        Thread {
            try {
                val conn = java.net.URL("https://i.ytimg.com/vi/$id/hqdefault.jpg").openConnection() as java.net.HttpURLConnection
                conn.connectTimeout = 6000
                conn.readTimeout = 6000
                try {
                    if (conn.responseCode == 200 && conn.contentLength <= 600_000) {
                        val bmp = BitmapFactory.decodeStream(conn.inputStream)
                        if (bmp != null) {
                            ytThumbCache.put(id, bmp)
                            iv.post { iv.setImageBitmap(bmp) }
                        }
                    }
                } finally {
                    conn.disconnect()
                }
            } catch (t: Throwable) {
            }
        }.start()
    }

    /**
     * يبني بطاقة وسائط لسطر `:; … ;:` مستقل: ملصق تشغيل أولاً (لا شبكة قبل النقر إلا الصورة المصغّرة الاختيارية)، وعند
     * النقر يُنشأ WebView مقيَّد: بلا وصول ملفات/محتوى، بلا محتوى مختلط، JavaScript لمضيفي الفيديو الموثوقين فقط،
     * التنقّل الرئيسي محصور بمضيف التضمين. أعلى المشغّل: ↗ فتح خارجي و✕ إغلاق. ملء الشاشة مدعوم، ويتوقف المشغّل
     * ويُدمَّر عند مغادرة الشاشة (لا صوت خلفي). الارتفاع من نسبة الأبعاد ([MarkdownSegment.Media.ratio]) إن وُجدت.
     */
    fun buildMediaView(context: Context, media: MarkdownSegment.Media, topMarginPx: Int = 0): View {
        applyPalette(paletteDark(context, docPageSettings))
        val density = context.resources.displayMetrics.density
        fun dp(v: Float): Int = (v * density + 0.5f).toInt()
        val isVideo = media.kind == "video"
        val host = hostOf(media.url)
        val embedHost = hostOf(media.embedUrl)
        val ratio = media.ratio
        val portrait = ratio in 0.01f..0.99f
        val openUrl = media.watchUrl.ifBlank { media.url }
        val white = 0xFFFFFFFF.toInt()

        val card = object : FrameLayout(context) {
            override fun onMeasure(widthSpec: Int, heightSpec: Int) {
                if (ratio > 0f) {
                    val w = View.MeasureSpec.getSize(widthSpec)
                    super.onMeasure(widthSpec, View.MeasureSpec.makeMeasureSpec((w / ratio).toInt(), View.MeasureSpec.EXACTLY))
                } else {
                    super.onMeasure(widthSpec, heightSpec)
                }
            }
        }
        card.layoutParams = if (ratio > 0f) {
            LinearLayout.LayoutParams(
                if (portrait) dp(300f) else LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                topMargin = topMarginPx
                if (portrait) gravity = Gravity.CENTER_HORIZONTAL
            }
        } else {
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(media.heightDp.toFloat())).apply { topMargin = topMarginPx }
        }
        card.background = GradientDrawable().apply {
            setColor(0xFF15171C.toInt())
            cornerRadius = dp(14f).toFloat()
            setStroke(dp(1f), COLOR_CARD_BORDER)
        }
        card.clipToOutline = true

        var loadPlayer: () -> Unit = {}
        var web: WebView? = null
        var exitFullscreen: (() -> Unit)? = null

        fun openExternal() {
            try {
                val i = Intent(Intent.ACTION_VIEW, Uri.parse(openUrl))
                if (findActivity(context) == null) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(i)
            } catch (t: Throwable) {
            }
        }

        fun buildPoster(): View {
            val root = FrameLayout(context)
            root.layoutParams = FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT)
            val tid = media.thumbId
            if (media.thumb && tid != null) {
                val iv = ImageView(context)
                iv.scaleType = ImageView.ScaleType.CENTER_CROP
                root.addView(iv, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
                loadYoutubeThumb(tid, iv)
            }
            val scrim = View(context)
            scrim.background = GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                intArrayOf(0x33000000, 0x00000000, 0xCC000000.toInt())
            )
            root.addView(scrim, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))

            val playBtn = TextView(context)
            playBtn.text = if (isVideo) "\u25B6" else "\uD83C\uDF10"
            playBtn.textSize = 22f
            playBtn.setTextColor(white)
            playBtn.gravity = Gravity.CENTER
            playBtn.background = GradientDrawable().apply {
                setColor(if (media.youtube) 0xFFFF0033.toInt() else 0xCC7C5CFF.toInt())
                cornerRadius = dp(12f).toFloat()
            }
            root.addView(playBtn, FrameLayout.LayoutParams(dp(64f), dp(44f), Gravity.CENTER))

            val badge = TextView(context)
            badge.text = if (media.youtube) "YouTube" else host
            badge.textSize = 11f
            badge.setTextColor(white)
            badge.setPadding(dp(8f), dp(3f), dp(8f), dp(3f))
            badge.background = GradientDrawable().apply {
                setColor(0x99000000.toInt())
                cornerRadius = dp(10f).toFloat()
            }
            root.addView(
                badge,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                    Gravity.TOP or Gravity.START
                ).apply { setMargins(dp(10f), dp(10f), dp(10f), dp(10f)) }
            )

            val sub = listOfNotNull(
                if (media.startSec > 0) "\u25B6 ${formatClock(media.startSec)}" else null,
                host
            ).joinToString(" \u00B7 ")
            val caption = TextView(context)
            caption.text = if (media.title.isNotBlank()) "${media.title}\n$sub" else sub
            caption.textSize = 12.5f
            caption.setTextColor(white)
            caption.maxLines = 3
            caption.ellipsize = android.text.TextUtils.TruncateAt.END
            caption.setPadding(dp(12f), dp(8f), dp(12f), dp(10f))
            root.addView(
                caption,
                FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM)
            )
            root.contentDescription = if (isVideo) "تشغيل الفيديو ${media.title}" else "تحميل ${media.title}"
            root.setOnClickListener { loadPlayer() }
            return root
        }

        fun stopPlayer() {
            exitFullscreen?.invoke()
            web?.let { w ->
                try {
                    w.stopLoading()
                    w.loadUrl("about:blank")
                    w.removeAllViews()
                    w.destroy()
                } catch (t: Throwable) {
                }
            }
            web = null
        }

        fun showPoster() {
            card.removeAllViews()
            card.addView(buildPoster())
        }

        fun showFallback() {
            card.post {
                stopPlayer()
                card.removeAllViews()
                val tv = TextView(context)
                tv.text = "تعذّر تحميل المشغّل — اضغط لفتح الرابط خارج التطبيق"
                tv.setTextColor(0xFFB4B8C5.toInt())
                tv.textSize = 13f
                tv.gravity = Gravity.CENTER
                tv.setPadding(dp(16f), dp(16f), dp(16f), dp(16f))
                tv.setOnClickListener { openExternal() }
                card.addView(tv, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
            }
        }

        loadPlayer = {
            card.removeAllViews()
            val trusted = isTrustedVideoHost(host) || isTrustedVideoHost(embedHost)
            val w = WebView(context)
            web = w
            w.settings.javaScriptEnabled = trusted
            w.settings.domStorageEnabled = trusted
            w.settings.allowFileAccess = false
            w.settings.allowContentAccess = false
            // المستخدم نقر الملصق عن قصد، فيُسمح بالتشغيل التلقائي لمضيفي الفيديو الموثوقين فقط.
            w.settings.mediaPlaybackRequiresUserGesture = !trusted
            w.settings.mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
            w.settings.setSupportZoom(false)
            w.setBackgroundColor(0xFF000000.toInt())
            w.webViewClient = object : WebViewClient() {
                override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                    if (!request.isForMainFrame) return false
                    val target = request.url
                    val h = hostOf(target.toString())
                    val sameSite = target.scheme.equals("https", ignoreCase = true) &&
                        (h == embedHost || h.endsWith(".$embedHost"))
                    return !sameSite
                }

                override fun onReceivedError(view: WebView, request: WebResourceRequest, error: android.webkit.WebResourceError) {
                    if (request.isForMainFrame) showFallback()
                }
            }
            w.webChromeClient = object : WebChromeClient() {
                private var custom: View? = null
                private var callback: WebChromeClient.CustomViewCallback? = null

                override fun onShowCustomView(view: View, cb: WebChromeClient.CustomViewCallback) {
                    val act = findActivity(context)
                    val decor = act?.window?.decorView as? FrameLayout
                    if (act == null || decor == null) {
                        cb.onCustomViewHidden()
                        return
                    }
                    if (custom != null) hide()
                    view.setBackgroundColor(0xFF000000.toInt())
                    decor.addView(view, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))
                    custom = view
                    callback = cb
                    @Suppress("DEPRECATION")
                    decor.systemUiVisibility = View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    exitFullscreen = { hide() }
                }

                override fun onHideCustomView() {
                    hide()
                }

                private fun hide() {
                    val v = custom ?: return
                    (v.parent as? FrameLayout)?.removeView(v)
                    custom = null
                    val act = findActivity(context)
                    @Suppress("DEPRECATION")
                    act?.window?.decorView?.systemUiVisibility = View.SYSTEM_UI_FLAG_VISIBLE
                    callback?.onCustomViewHidden()
                    callback = null
                    exitFullscreen = null
                }
            }
            if (media.youtube) {
                w.loadDataWithBaseURL(RIN_LINKS_BASE, ytWrapperHtml(media.embedUrl, media.title), "text/html", "utf-8", null)
            } else {
                w.loadUrl(media.embedUrl)
            }
            card.addView(w, FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT))

            fun pill(label: String, onClick: () -> Unit): TextView {
                val tv = TextView(context)
                tv.text = label
                tv.textSize = 15f
                tv.setTextColor(white)
                tv.setPadding(dp(12f), dp(6f), dp(12f), dp(6f))
                tv.background = GradientDrawable().apply {
                    setColor(0x99000000.toInt())
                    cornerRadius = dp(16f).toFloat()
                }
                tv.setOnClickListener { onClick() }
                return tv
            }
            val bar = LinearLayout(context)
            bar.orientation = LinearLayout.HORIZONTAL
            bar.addView(pill("\u2197") { openExternal() })
            bar.addView(
                pill("\u2715") { stopPlayer(); showPoster() },
                LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT)
                    .apply { marginStart = dp(6f) }
            )
            card.addView(
                bar,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT,
                    Gravity.TOP or Gravity.END
                ).apply { setMargins(dp(8f), dp(8f), dp(8f), dp(8f)) }
            )
        }

        // مغادرة الشاشة: أوقف المشغّل ودمّره (لا صوت خلفي، ولا تسرّب WebView).
        card.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(v: View) {}

            override fun onViewDetachedFromWindow(v: View) {
                if (web != null) {
                    stopPlayer()
                    showPoster()
                }
            }
        })

        card.addView(buildPoster())
        return card
    }

    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //            إعدادات الصفحة في rdoc: `؛؛؛ صفحة` (حجم، مساحة، خلفية، ألوان، محاذاة، اتجاه…)
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //   ؛؛؛ صفحة
    //   background: #0F1115,#1C1E25        (لون | 2–6 ألوان | اسم لوحة: forest/harbor/lagoon)
    //   bg-dir: down                       (down/up/right/left/diag/diag-rev)
    //   theme: dark                        (light/dark/auto)
    //   text-color / heading-color / link-color
    //   font-size: lg                      (xs/sm/md/lg/xl/xxl أو 10–32 أو 70%–200%)
    //   line-height: 1.6   letter-spacing: 0.02   font: serif (sans/serif/mono)
    //   padding: 20 16                     (dp، كـCSS: 1–4 قيم)   radius: 24   border: #7C5CFF 2
    //   max-width: 720                     (dp)   align: justify   direction: rtl
    //   ؛؛؛
    // المفاتيح تُقبل أيضاً داخل بلوك `؛؛؛ rdoc` نفسه، ولها أسماء عربية (خلفية، مساحة، حجم-الخط، محاذاة…).

    /** إعدادات صفحة مستخرَجة؛ كل حقل null = "لا تغيير" (يبقى الافتراضي). */
    class PageSettings(
        val background: PageBackground?,
        val bgOrientation: GradientDrawable.Orientation,
        val textColor: Int?,
        val headingColor: Int?,
        val linkColor: Int?,
        val fontSizeSp: Float?,
        val lineHeight: Float?,
        val letterSpacing: Float?,
        /** [يسار، أعلى، يمين، أسفل] بالـdp. */
        val padding: IntArray?,
        val radiusDp: Float?,
        val borderColor: Int?,
        val borderDp: Float,
        val maxWidthDp: Int?,
        val align: String?,
        val direction: String?,
        val font: String?,
        val theme: String?
    )

    private var docPageSettings: PageSettings? = null

    private val PAGE_KEYS: Map<String, String> = mapOf(
        "background" to "background", "bg" to "background", "خلفية" to "background",
        "bg-dir" to "bg-dir", "اتجاه-الخلفية" to "bg-dir",
        "text-color" to "text-color", "color" to "text-color", "لون-النص" to "text-color",
        "heading-color" to "heading-color", "لون-العناوين" to "heading-color",
        "link-color" to "link-color", "لون-الروابط" to "link-color",
        "font-size" to "font-size", "حجم-الخط" to "font-size",
        "line-height" to "line-height", "تباعد-الأسطر" to "line-height",
        "letter-spacing" to "letter-spacing", "تباعد-الحروف" to "letter-spacing",
        "padding" to "padding", "مساحة" to "padding", "حشوة" to "padding", "مساحة-الصفحة" to "padding",
        "radius" to "radius", "زوايا" to "radius",
        "border" to "border", "إطار" to "border",
        "max-width" to "max-width", "أقصى-عرض" to "max-width", "عرض" to "max-width",
        "align" to "align", "محاذاة" to "align",
        "direction" to "direction", "اتجاه" to "direction",
        "font" to "font", "خط" to "font",
        "theme" to "theme", "سمة" to "theme"
    )

    private val PAGE_FONT_SIZES: Map<String, Float> = mapOf(
        "xs" to 11f, "sm" to 13f, "md" to 14.5f, "lg" to 17f, "xl" to 20f, "xxl" to 24f,
        "صغير" to 13f, "متوسط" to 14.5f, "كبير" to 17f, "ضخم" to 20f
    )
    private val PAGE_BG_DIRS: Map<String, GradientDrawable.Orientation> = mapOf(
        "down" to GradientDrawable.Orientation.TOP_BOTTOM, "أسفل" to GradientDrawable.Orientation.TOP_BOTTOM,
        "up" to GradientDrawable.Orientation.BOTTOM_TOP, "أعلى" to GradientDrawable.Orientation.BOTTOM_TOP,
        "right" to GradientDrawable.Orientation.LEFT_RIGHT, "يمين" to GradientDrawable.Orientation.LEFT_RIGHT,
        "left" to GradientDrawable.Orientation.RIGHT_LEFT, "يسار" to GradientDrawable.Orientation.RIGHT_LEFT,
        "diag" to GradientDrawable.Orientation.TL_BR, "قطري" to GradientDrawable.Orientation.TL_BR,
        "diag-rev" to GradientDrawable.Orientation.TR_BL, "قطري-معكوس" to GradientDrawable.Orientation.TR_BL
    )
    private val PAGE_ALIGNS: Map<String, String> = mapOf(
        "start" to "start", "center" to "center", "end" to "end", "justify" to "justify",
        "بداية" to "start", "وسط" to "center", "نهاية" to "end", "ضبط" to "justify"
    )
    private val PAGE_FONTS: Map<String, String> = mapOf(
        "sans" to "sans", "serif" to "serif", "mono" to "mono", "monospace" to "mono",
        "عادي" to "sans", "مزخرف" to "serif", "ثابت" to "mono"
    )

    /** يستخرج إعدادات الصفحة من [markdown] (أو null إن لم توجد أي مفاتيح). للمستندات rdoc فقط. */
    fun extractPageSettings(markdown: String): PageSettings? = parsePageSettings(markdown, null)

    private fun paletteDark(context: Context, page: PageSettings?): Boolean =
        when (page?.theme) {
            "dark" -> true
            "light" -> false
            else -> isDarkMode(context)
        }

    /**
     * يقرأ بلوكات `؛؛؛ صفحة`/`page` (صارمة: مفتاح مجهول → تحذير) ومفاتيح الصفحة داخل `؛؛؛ rdoc`/`meta` (متساهلة:
     * مفتاح غير صفحة يُتجاهل). إن مُرِّر [issues] تُسجَّل القيم غير الصالحة بأرقام الأسطر؛ وإلا تُتجاهل بصمت.
     * أول تعريف لمفتاح يفوز.
     */
    private fun parsePageSettings(markdown: String, issues: MutableList<RdocIssue>?): PageSettings? {
        var background: PageBackground? = null
        var bgDir = GradientDrawable.Orientation.TOP_BOTTOM
        var textColor: Int? = null
        var headingColor: Int? = null
        var linkColor: Int? = null
        var fontSize: Float? = null
        var lineHeight: Float? = null
        var letterSpacing: Float? = null
        var padding: IntArray? = null
        var radius: Float? = null
        var borderColor: Int? = null
        var borderDp = 0f
        var maxWidth: Int? = null
        var align: String? = null
        var direction: String? = null
        var font: String? = null
        var theme: String? = null
        val seen = HashSet<String>()
        var found = false

        val lines = markdown.lines()
        var inFence = false
        var i = 0
        while (i < lines.size) {
            val t = lines[i].trim()
            if (t.startsWith("```")) { inFence = !inFence; i++; continue }
            val open = if (inFence) null else arSemiFenceOpenRegex.find(t)
            if (open == null) { i++; continue }
            val scan = scanDataFence(lines, i)
            val type = open.groupValues[1].trim().split(whitespaceRegex)[0].lowercase()
            val strict = type == "page" || type == "صفحة"
            if (strict || type == "rdoc" || type == "meta" || type == "بيانات") {
                for ((off, line) in scan.body.withIndex()) {
                    val idx = line.indexOf(':')
                    if (idx <= 0) continue
                    val rawKey = line.substring(0, idx).trim().lowercase().replace('_', '-').replace(' ', '-')
                    val value = line.substring(idx + 1).trim()
                    val key = PAGE_KEYS[rawKey]
                    val ln = i + 2 + off
                    if (key == null) {
                        if (strict) issues?.add(RdocIssue(ln, "warning", "مفتاح صفحة غير معروف: $rawKey"))
                        continue
                    }
                    found = true
                    if (!seen.add(key)) {
                        issues?.add(RdocIssue(ln, "warning", "مفتاح مكرّر: $rawKey (الأول هو المعتمد)"))
                        continue
                    }
                    val v = value.lowercase()
                    var ok = true
                    when (key) {
                        "background" -> {
                            val named = NAMED_PALETTES[v]
                            if (named != null) {
                                background = PageBackground.Gradient(named)
                            } else {
                                val stops = value.split(",").map { it.trim() }.filter { it.isNotEmpty() }.map { parseColorValue(it)?.first }
                                if (stops.isEmpty() || stops.size > 6 || stops.any { it == null }) {
                                    ok = false
                                } else {
                                    val cs = stops.filterNotNull().toIntArray()
                                    background = if (cs.size >= 2) PageBackground.Gradient(cs) else PageBackground.Solid(cs[0])
                                }
                            }
                        }
                        "bg-dir" -> { val d = PAGE_BG_DIRS[v]; if (d == null) ok = false else bgDir = d }
                        "text-color" -> { val c = parseColorValue(value)?.first; if (c == null) ok = false else textColor = c }
                        "heading-color" -> { val c = parseColorValue(value)?.first; if (c == null) ok = false else headingColor = c }
                        "link-color" -> { val c = parseColorValue(value)?.first; if (c == null) ok = false else linkColor = c }
                        "font-size" -> {
                            val f = PAGE_FONT_SIZES[v]
                                ?: v.removeSuffix("sp").trim().toFloatOrNull()?.takeIf { it in 10f..32f }
                                ?: v.takeIf { it.endsWith("%") }?.removeSuffix("%")?.toFloatOrNull()?.takeIf { it in 70f..200f }?.let { it / 100f * 14.5f }
                            if (f == null) ok = false else fontSize = f
                        }
                        "line-height" -> { val f = v.toFloatOrNull()?.takeIf { it in 1f..3f }; if (f == null) ok = false else lineHeight = f }
                        "letter-spacing" -> { val f = v.toFloatOrNull()?.takeIf { it in -0.05f..0.5f }; if (f == null) ok = false else letterSpacing = f }
                        "padding" -> {
                            val nums = v.replace("dp", "").split(Regex("[,\\s]+")).filter { it.isNotEmpty() }.map { it.toIntOrNull() }
                            if (nums.isEmpty() || nums.size > 4 || nums.any { it == null || it !in 0..64 }) {
                                ok = false
                            } else {
                                val n = nums.filterNotNull()
                                // كـCSS: قيمة (الكل) | رأسي أفقي | أعلى أفقي أسفل | أعلى يمين أسفل يسار
                                val (top, right, bottom, left) = when (n.size) {
                                    1 -> listOf(n[0], n[0], n[0], n[0])
                                    2 -> listOf(n[0], n[1], n[0], n[1])
                                    3 -> listOf(n[0], n[1], n[2], n[1])
                                    else -> listOf(n[0], n[1], n[2], n[3])
                                }
                                padding = intArrayOf(left, top, right, bottom)
                            }
                        }
                        "radius" -> { val f = v.removeSuffix("dp").trim().toFloatOrNull()?.takeIf { it in 0f..48f }; if (f == null) ok = false else radius = f }
                        "border" -> {
                            val toks = value.split(whitespaceRegex).filter { it.isNotEmpty() }
                            var c: Int? = null
                            var w: Float? = null
                            for (tk in toks) {
                                val num = tk.lowercase().removeSuffix("dp").toFloatOrNull()
                                if (num != null) w = num else c = parseColorValue(tk)?.first ?: run { ok = false; null }
                            }
                            if (!ok || c == null || w == null || w !in 0f..8f) {
                                ok = false
                            } else {
                                borderColor = c
                                borderDp = w
                            }
                        }
                        "max-width" -> { val n = v.removeSuffix("dp").trim().toIntOrNull()?.takeIf { it in 240..1200 }; if (n == null) ok = false else maxWidth = n }
                        "align" -> { val a = PAGE_ALIGNS[v]; if (a == null) ok = false else align = a }
                        "direction" -> { if (v == "rtl" || v == "ltr" || v == "auto") direction = v else ok = false }
                        "font" -> { val f = PAGE_FONTS[v]; if (f == null) ok = false else font = f }
                        "theme" -> { if (v == "light" || v == "dark" || v == "auto") theme = v.takeIf { it != "auto" } else ok = false }
                    }
                    if (!ok) issues?.add(RdocIssue(ln, "error", "قيمة غير صالحة لـ$rawKey: $value"))
                }
            }
            i = if (scan.next > i) scan.next else i + 1
        }
        if (!found) return null
        return PageSettings(
            background, bgDir, textColor, headingColor, linkColor, fontSize, lineHeight, letterSpacing,
            padding, radius, borderColor, borderDp, maxWidth, align, direction, font, theme
        )
    }

    /** يطبّق إعدادات النص (لون، حجم، تباعد، محاذاة، اتجاه، خط) على [tv] — تُستدعى من [applyTo] لمقاطع rdoc النصية. */
    private fun applyPageTextStyle(tv: TextView, page: PageSettings, dark: Boolean) {
        val tc = page.textColor
        if (tc != null) {
            tv.setTextColor(tc)
        } else if (page.theme != null) {
            tv.setTextColor(if (dark) 0xFFE3E5E8.toInt() else 0xFF20252B.toInt())
        }
        page.fontSizeSp?.let { tv.textSize = it }
        page.lineHeight?.let { tv.setLineSpacing(tv.lineSpacingExtra, it) }
        page.letterSpacing?.let { tv.letterSpacing = it }
        when (page.align) {
            "start" -> tv.textAlignment = View.TEXT_ALIGNMENT_VIEW_START
            "center" -> tv.textAlignment = View.TEXT_ALIGNMENT_CENTER
            "end" -> tv.textAlignment = View.TEXT_ALIGNMENT_VIEW_END
            "justify" -> {
                tv.textAlignment = View.TEXT_ALIGNMENT_VIEW_START
                if (Build.VERSION.SDK_INT >= 26) tv.justificationMode = Layout.JUSTIFICATION_MODE_INTER_WORD
            }
        }
        when (page.direction) {
            "rtl" -> tv.textDirection = View.TEXT_DIRECTION_RTL
            "ltr" -> tv.textDirection = View.TEXT_DIRECTION_LTR
            "auto" -> tv.textDirection = View.TEXT_DIRECTION_FIRST_STRONG
        }
        when (page.font) {
            "sans" -> tv.typeface = Typeface.SANS_SERIF
            "serif" -> tv.typeface = Typeface.SERIF
            "mono" -> tv.typeface = Typeface.MONOSPACE
        }
    }

    /**
     * يطبّق إعدادات الحاوية (خلفية متدرّجة/صلبة، زوايا، إطار، مساحة/حشوة، أقصى عرض) على [container] — تستدعيها
     * الشاشة الحاضنة لـREADME بعد بناء بطاقة الصفحة الافتراضية، فتغلب قيم `؛؛؛ صفحة` على الافتراضي وعلى `Page_background`.
     */
    fun applyPageContainer(container: View, page: PageSettings) {
        val density = container.resources.displayMetrics.density
        fun dp(v: Float): Int = (v * density + 0.5f).toInt()
        val dark = paletteDark(container.context, page)
        if (page.background != null || page.borderColor != null || page.radiusDp != null || page.theme != null) {
            val drawable = when (val bg = page.background) {
                is PageBackground.Solid -> GradientDrawable().apply { setColor(bg.color) }
                is PageBackground.Gradient -> GradientDrawable(page.bgOrientation, bg.colors)
                null -> GradientDrawable().apply { setColor(if (dark) 0xFF1C1E25.toInt() else 0xFFFFFFFF.toInt()) }
            }
            drawable.cornerRadius = dp(page.radiusDp ?: 16f).toFloat()
            val bc = page.borderColor
            if (bc != null && page.borderDp > 0f) {
                drawable.setStroke(dp(page.borderDp).coerceAtLeast(1), bc)
            } else if (page.background == null) {
                drawable.setStroke(dp(1f).coerceAtLeast(1), if (dark) 0xFF2A2D34.toInt() else 0xFFE2E6ED.toInt())
            }
            container.background = drawable
        }
        page.padding?.let { p -> container.setPadding(dp(p[0].toFloat()), dp(p[1].toFloat()), dp(p[2].toFloat()), dp(p[3].toFloat())) }
        page.maxWidthDp?.let { mw ->
            container.post {
                val maxPx = dp(mw.toFloat())
                val lp = container.layoutParams ?: return@post
                val parentWidth = (container.parent as? View)?.width ?: 0
                if (parentWidth > maxPx && lp.width != maxPx) {
                    lp.width = maxPx
                    if (lp is LinearLayout.LayoutParams) lp.gravity = Gravity.CENTER_HORIZONTAL
                    else if (lp is FrameLayout.LayoutParams) lp.gravity = Gravity.CENTER_HORIZONTAL
                    container.layoutParams = lp
                }
            }
        }
    }


    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //                        شريط rdoc المتحرّك: `::: شريط` و`[marquee: …]`
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //   ::: شريط speed=(60) style=(solid) color=(#7C5CFF) icon=(star) dir=(right) sep=(★) link=(repo)
    //   إصدار جديد متاح الآن
    //   دعم أكثر من 10 منصات
    //   :::
    //   [marquee: عرض خاص|خبر ثانٍ|speed=(45)|style=(outline)|color=(#22C88E)]
    // كل سطر (أو كل جزء `|`) عنصر، تفصل بينها `sep`. يختلف عن `[*نص/marquee*]` القديم: عدة عناصر، إيقاف/استئناف
    // بالنقر (`pause=(tap)` الافتراضي إن لم يوجد `link`)، `link` رابط أو معرّف عنصر، وستايل الأقواس (size/style/color/icon).

    private class TickerOptions(
        val speedDp: Float,
        val leftward: Boolean,
        val style: String,
        val color: Int?,
        val sep: String,
        val icon: String?,
        val widthDp: Float?,
        val pauseOnTap: Boolean,
        val linkUrl: String?,
        val size: Float,
        /** true = حركة رأسية (`dir=(up|down)`): عناصر في أسطر داخل صندوق بارتفاع ثابت. */
        val vertical: Boolean,
        val upward: Boolean,
        val heightDp: Float?,
        /** true = `mode=(step)`: عنصر واحد يظهر ثم ينزلق للتالي بعد [holdSec]. */
        val step: Boolean,
        val holdSec: Float
    )

    private val tickerOptionRegex = Regex("(\\w+)\\s*=\\s*\\(([^)]*)\\)")
    private val tickerBulletRegex = Regex("^[-*+]\\s+")

    /** حالة تحريك مشتركة بين رسم الشريط ونقرة الإيقاف (موضع تراكمي بالبكسل + آخر توقيت رسم). */
    private class TickerState {
        var paused = false
        var pos = 0f
        var last = 0L
    }

    private class TickerToggleSpan(private val state: TickerState) : ClickableSpan() {
        override fun onClick(widget: View) {
            state.paused = !state.paused
            widget.invalidate()
        }

        override fun updateDrawState(ds: TextPaint) {}
    }

    /** يحوّل خيارات `key=(قيمة)` إلى [TickerOptions]؛ [base] ستايل الوسم الفعّال (style/color/icon) للقوس السطري. */
    private fun parseTickerOptions(
        opts: Map<String, String>,
        base: TagStyle?,
        refs: Map<String, ElementRef> = currentElementRefs
    ): Pair<TickerOptions?, String?> {
        var speed = 50f
        var left = true
        var style = base?.style ?: "soft"
        var color = base?.color
        var sep = "  \u2022  "
        var icon = base?.icon
        var width: Float? = null
        var pause = true
        var link: String? = null
        var size = 1f
        var vertical = false
        var upward = true
        var height: Float? = null
        var step = false
        var hold = 2f
        var verticalOnly = false
        for ((k, raw) in opts) {
            val v = raw.trim()
            val lv = v.lowercase()
            when (k) {
                "speed" -> speed = lv.toFloatOrNull()?.takeIf { it in 5f..400f } ?: return null to "speed=($v)"
                "dir" -> when (lv) {
                    "left", "يسار" -> { left = true; vertical = false }
                    "right", "يمين" -> { left = false; vertical = false }
                    "up", "أعلى" -> { vertical = true; upward = true }
                    "down", "أسفل" -> { vertical = true; upward = false }
                    else -> return null to "dir=($v)"
                }
                "height" -> {
                    height = lv.removeSuffix("dp").trim().toFloatOrNull()?.takeIf { it in 24f..400f } ?: return null to "height=($v)"
                    verticalOnly = true
                }
                "mode" -> {
                    step = when (lv) {
                        "step", "خطوة" -> true
                        "scroll", "تمرير" -> false
                        else -> return null to "mode=($v)"
                    }
                    verticalOnly = true
                }
                "hold" -> {
                    hold = lv.toFloatOrNull()?.takeIf { it in 0.5f..30f } ?: return null to "hold=($v)"
                    verticalOnly = true
                }
                "style" -> style = TAG_STYLES[lv] ?: return null to "style=($v)"
                "color" -> color = (parseColorValue(v) ?: return null to "color=($v)").first
                "sep" -> sep = if (v.isBlank()) sep else "  ${v.take(6)}  "
                "icon" -> icon = iconGlyphFor(v) ?: return null to "icon=($v)"
                "width" -> width = lv.removeSuffix("dp").trim().toFloatOrNull()?.takeIf { it in 120f..1200f } ?: return null to "width=($v)"
                "pause" -> pause = when (lv) {
                    "tap", "نقر" -> true
                    "none", "no", "بلا" -> false
                    else -> return null to "pause=($v)"
                }
                "link" -> link = if (isSafeLinkUrl(v)) v else (resolveElement(v, refs)?.url ?: return null to "link=($v)")
                "size" -> size = TAG_SIZES[lv] ?: lv.removeSuffix("%").toFloatOrNull()?.takeIf { it in 50f..300f }?.div(100f) ?: return null to "size=($v)"
                "shape", "pos" -> {}
                else -> return null to "خيار شريط غير معروف: $k"
            }
        }
        if (verticalOnly && !vertical) return null to "height/mode/hold تحتاج dir=(up) أو dir=(down)"
        return TickerOptions(speed, left, style, color, sep, icon, width, pause, link, size, vertical, upward, height, step, hold) to null
    }

    /** يرسم شريطاً واحداً يمرّر [items] مفصولة بـ`sep` داخل حبّة بعرض ثابت، ويربطه بنقرة إيقاف أو رابط. */
    private fun appendRdocTicker(out: SpannableStringBuilder, items: List<String>, o: TickerOptions) {
        val base = o.color ?: COLOR_BULLET
        val bg: Int
        val fg: Int
        val stroke: Int
        when (o.style) {
            "solid" -> { bg = base; fg = if (o.color != null) contrastingTextColor(base) else 0xFFFFFFFF.toInt(); stroke = 0 }
            "outline" -> { bg = 0; fg = softTextColor(base); stroke = base }
            "ghost" -> { bg = 0; fg = softTextColor(base); stroke = 0 }
            else -> { bg = tintedBackground(base, 0x26); fg = softTextColor(base); stroke = 0 }
        }
        val glyph = o.icon
        val label = items.joinToString(o.sep) { if (glyph != null) "$glyph $it" else it }
        val density = currentDensity
        val customWidth = o.widthDp
        val widthPx = if (customWidth != null) customWidth * density else currentImageMaxWidthPx * 0.96f
        val state = TickerState()
        val start = out.length
        out.append("\u00A0")
        val end = out.length
        val span: ReplacementSpan = if (o.vertical) {
            RdocVerticalTickerSpan(
                items.map { if (glyph != null) "$glyph $it" else it }, bg, fg, stroke, if (stroke != 0) 2.5f else 0f,
                widthPx, (o.heightDp ?: 0f) * density, o.speedDp * density, o.upward, o.step, (o.holdSec * 1000f).toLong(), state
            )
        } else {
            RdocTickerSpan(label, bg, fg, stroke, if (stroke != 0) 2.5f else 0f, widthPx, o.speedDp * density, o.leftward, state)
        }
        out.setSpan(span, start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.9f * o.size), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        val linkUrl = o.linkUrl
        if (linkUrl != null) {
            out.setSpan(LinkButtonClickSpan(linkUrl), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        } else if (o.pauseOnTap) {
            out.setSpan(TickerToggleSpan(state), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    /**
     * شريط رأسي: [items] أسطر داخل صندوق بارتفاع ثابت ([heightPx]، أو 3 أسطر للتمرير/سطر للخطوة إن كان 0).
     * `scroll` تمرير مستمر (لأعلى إن [upward] وإلا لأسفل)، و`step` يعرض عنصراً واحداً [holdMs] ثم ينزلق للتالي.
     * الموضع في [TickerState] (بكسل للتمرير، مللي ثانية للخطوة) فيتوقف/يستأنف بلا قفزة، ويخفت النص أثناء الإيقاف.
     */
    private class RdocVerticalTickerSpan(
        private val items: List<String>,
        private val bg: Int,
        private val fg: Int,
        private val strokeColor: Int,
        private val strokeWidth: Float,
        private val widthPx: Float,
        private val heightPx: Float,
        private val speedPxPerSec: Float,
        private val upward: Boolean,
        private val step: Boolean,
        private val holdMs: Long,
        private val state: TickerState,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 14f,
        private val paddingV: Float = 4f
    ) : ReplacementSpan() {
        private var cacheWidth = -1f
        private var cacheSize = -1f
        private var cacheItems: List<String> = items

        private fun lineHeight(paint: Paint): Float = paint.fontSpacing * 1.35f

        private fun boxHeight(paint: Paint): Float {
            val visible = if (heightPx > 0f) heightPx else lineHeight(paint) * (if (step) 1f else 3f)
            return visible + paddingV * 2f
        }

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val h = boxHeight(paint)
                it.ascent = -(h * 0.8f).toInt()
                it.descent = (h * 0.2f).toInt()
                it.top = it.ascent
                it.bottom = it.descent
            }
            return widthPx.toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            val savedAlign = paint.textAlign
            paint.isAntiAlias = true

            val rect = RectF(x, top.toFloat() + paddingV, x + widthPx, bottom.toFloat() - paddingV)
            if (bg != 0) {
                paint.style = Paint.Style.FILL
                paint.color = bg
                canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)
            }
            if (strokeWidth > 0f) {
                val savedStroke = paint.strokeWidth
                val half = strokeWidth / 2f
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = strokeWidth
                paint.color = strokeColor
                canvas.drawRoundRect(
                    RectF(rect.left + half, rect.top + half, rect.right - half, rect.bottom - half),
                    cornerRadius, cornerRadius, paint
                )
                paint.strokeWidth = savedStroke
            }

            val availW = (widthPx - paddingH * 2f).coerceAtLeast(1f)
            if (cacheWidth != availW || cacheSize != paint.textSize) {
                val tp = TextPaint(paint)
                cacheItems = items.map { android.text.TextUtils.ellipsize(it, tp, availW, android.text.TextUtils.TruncateAt.END).toString() }
                cacheWidth = availW
                cacheSize = paint.textSize
            }
            val lines = cacheItems
            val n = lines.size
            if (n == 0) return
            val lineH = lineHeight(paint)
            val boxTop = rect.top
            val boxBottom = rect.bottom
            val boxH = boxBottom - boxTop

            val now = SystemClock.uptimeMillis()
            val dt = if (state.last != 0L && !state.paused) (now - state.last).coerceIn(0L, 100L) else 0L
            state.last = now
            val slotMs = holdMs + SLIDE_MS
            val cycle = maxOf(n * lineH, boxH + lineH)
            if (dt > 0L) {
                state.pos = if (step) {
                    (state.pos + dt) % (slotMs * n).toFloat()
                } else {
                    (state.pos + dt / 1000f * speedPxPerSec) % cycle
                }
            }

            val fmv = paint.fontMetrics
            val centerShift = (fmv.ascent + fmv.descent) / 2f
            val cx = x + widthPx / 2f
            paint.style = Paint.Style.FILL
            paint.textAlign = Paint.Align.CENTER
            paint.color = if (state.paused) (fg and 0x00FFFFFF) or (0x99 shl 24) else fg

            canvas.save()
            canvas.clipRect(x + paddingH, boxTop, x + widthPx - paddingH, boxBottom)
            if (step) {
                val t = state.pos
                val idx = (t / slotMs).toInt().coerceIn(0, n - 1)
                val local = t - idx * slotMs
                val cy = boxTop + boxH / 2f
                val dir = if (upward) -1f else 1f
                if (local < holdMs) {
                    canvas.drawText(lines[idx], cx, cy - centerShift, paint)
                } else {
                    val p = ((local - holdMs) / SLIDE_MS.toFloat()).coerceIn(0f, 1f)
                    val e = p * p * (3f - 2f * p)
                    canvas.drawText(lines[idx], cx, cy + dir * e * boxH - centerShift, paint)
                    canvas.drawText(lines[(idx + 1) % n], cx, cy - dir * (1f - e) * boxH - centerShift, paint)
                }
            } else {
                val offset = state.pos % cycle
                var yTop = if (upward) boxTop - offset else boxTop + offset - cycle
                while (yTop < boxBottom) {
                    for (i in 0 until n) {
                        val lineTop = yTop + i * lineH
                        if (lineTop + lineH > boxTop && lineTop < boxBottom) {
                            canvas.drawText(lines[i], cx, lineTop + lineH / 2f - centerShift, paint)
                        }
                    }
                    yTop += cycle
                }
            }
            canvas.restore()

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
            paint.textAlign = savedAlign
        }

        private companion object {
            const val SLIDE_MS = 450L
        }
    }

    /** حاوية `::: شريط …`: خيارات `key=(v)` في رأسها، وكل سطر غير فارغ في جسمها عنصر. */
    private fun appendRdocMarquee(out: SpannableStringBuilder, title: String, body: List<String>, closed: Boolean) {
        val opts = LinkedHashMap<String, String>()
        for (m in tickerOptionRegex.findAll(title)) opts[m.groupValues[1].lowercase()] = m.groupValues[2]
        val (o, err) = parseTickerOptions(opts, null)
        if (o == null) {
            appendDiagnosticChip(out, "شريط: $err")
            return
        }
        val items = body.map { tickerBulletRegex.replace(it.trim(), "").replace("**", "").replace("__", "").replace("`", "") }
            .filter { it.isNotBlank() }
        if (items.isEmpty()) {
            appendDiagnosticChip(out, "شريط فارغ")
            return
        }
        appendRdocTicker(out, items, o)
        if (!closed) {
            out.append("  ")
            appendDiagnosticChip(out, "حاوية غير مغلقة")
        }
    }

    /**
     * حبّة الشريط: كـ[MarqueeSpan] لكن الموضع تراكمي في [TickerState] (يتوقف عند الإيقاف بلا قفزة عند الاستئناف)،
     * ويخفت النص أثناء الإيقاف. الفرق الزمني بين رسمين يُقيَّد بـ100ms كي لا يقفز الشريط بعد غياب طويل.
     */
    private class RdocTickerSpan(
        private val label: String,
        private val bg: Int,
        private val fg: Int,
        private val strokeColor: Int,
        private val strokeWidth: Float,
        private val widthPx: Float,
        private val speedPxPerSec: Float,
        private val leftward: Boolean,
        private val state: TickerState,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 14f,
        private val paddingV: Float = 4f,
        private val gapPx: Float = 24f
    ) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            return widthPx.toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            val rect = RectF(x, top.toFloat() + paddingV, x + widthPx, bottom.toFloat() - paddingV)
            if (bg != 0) {
                paint.style = Paint.Style.FILL
                paint.color = bg
                canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)
            }
            if (strokeWidth > 0f) {
                val savedStroke = paint.strokeWidth
                val half = strokeWidth / 2f
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = strokeWidth
                paint.color = strokeColor
                canvas.drawRoundRect(
                    RectF(rect.left + half, rect.top + half, rect.right - half, rect.bottom - half),
                    cornerRadius, cornerRadius, paint
                )
                paint.strokeWidth = savedStroke
                paint.style = Paint.Style.FILL
            }

            val left = x + paddingH
            val right = x + widthPx - paddingH
            val textWidth = paint.measureText(label)
            val cycle = textWidth + gapPx

            val now = SystemClock.uptimeMillis()
            if (state.last != 0L && !state.paused) {
                val dt = (now - state.last).coerceIn(0L, 100L) / 1000f
                state.pos = (state.pos + dt * speedPxPerSec) % cycle
            }
            state.last = now
            val offset = state.pos % cycle

            canvas.save()
            canvas.clipRect(left, top.toFloat(), right, bottom.toFloat())
            paint.style = Paint.Style.FILL
            paint.color = if (state.paused) (fg and 0x00FFFFFF) or (0x99 shl 24) else fg
            var px = if (leftward) left - offset else left + offset - cycle
            while (px < right) {
                if (px + textWidth > left) canvas.drawText(label, px, y.toFloat(), paint)
                px += cycle
            }
            canvas.restore()

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }


    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //                     ميزات الحزم والتوثيق: `؛؛؛ حزمة` `؛؛؛ تبعيات` `::: دالة` `::: تثبيت` …
    // ═══════════════════════════════════════════════════════════════════════════════════════════
    //   ؛؛؛ حزمة                         بطاقة الحزمة (اسم/إصدار/وصف/ناشر/رخصة/يتطلب/منصات/كلمات/تثبيت/مستودع)
    //   ؛؛؛ تبعيات                        `@user/lib = ^1.2 | ملاحظة` — كل سطر تبعية بقيد إصدار
    //   ::: دالة indmedia_html(url, title)  توثيق API: @param @return @throws @since @deprecated @see @example
    //   ::: فهرس-الدوال                    فهرس تلقائي بتواقيع كل ::: دالة في المستند
    //   ::: تثبيت                          أوامر قابلة للنسخ (فارغة → rin install <اسم الحزمة>)
    //   ::: مثال عنوان                      بطاقة مثال
    //   [since: 1.0] [requires: rin>=1.0] [dep: @u/lib@^1.2] [install: @u/lib] [author: @u] [repo: https://…|نص]

    private val PKG_KEYS: Map<String, String> = mapOf(
        "name" to "name", "الاسم" to "name", "اسم" to "name",
        "version" to "version", "الإصدار" to "version", "إصدار" to "version",
        "description" to "description", "desc" to "description", "الوصف" to "description", "وصف" to "description",
        "author" to "author", "المؤلف" to "author", "الناشر" to "author",
        "license" to "license", "الرخصة" to "license", "ترخيص" to "license",
        "repo" to "repo", "repository" to "repo", "المستودع" to "repo",
        "homepage" to "homepage", "الموقع" to "homepage",
        "requires" to "requires", "يتطلب" to "requires",
        "platforms" to "platforms", "المنصات" to "platforms",
        "keywords" to "keywords", "الكلمات" to "keywords",
        "install" to "install", "التثبيت" to "install"
    )

    private val pkgNameRegex = Regex("^@?[A-Za-z0-9._-]+(?:/[A-Za-z0-9._-]+)?$")
    private val versionRegex = Regex("^v?\\d+(?:\\.\\d+){0,2}(?:[-+][A-Za-z0-9.]+)?$")
    private val constraintRegex = Regex("^(?:\\*|(?:>=|<=|>|<|=|\\^|~)?\\s*v?\\d+(?:\\.\\d+){0,2}(?:[-+][A-Za-z0-9.]+)?)$")
    private val requiresRegex = Regex("^[A-Za-z][A-Za-z0-9._-]*\\s*(?:>=|<=|>|<|=|\\^|~)?\\s*v?\\d+(?:\\.\\d+){0,2}$")
    private val depValueRegex = Regex("^(@?[A-Za-z0-9._-]+(?:/[A-Za-z0-9._-]+)?)(?:@([^\\s@]+))?$")
    private val depLineRegex = Regex("^(\\S+?)\\s*=\\s*([^|]+?)\\s*(?:\\|\\s*(.*))?$")
    private val apiDirectiveRegex = Regex("^@(\\w+)\\s*:?\\s*(.*)$")
    private val apiParamRegex = Regex("^([A-Za-z_][\\w.]*)\\s*(?:\\(([^)]*)\\))?\\s*(?:[:\\-\\u2014]\\s*)?(.*)$")
    private val apiSigParamsRegex = Regex("\\(([^)]*)\\)")
    private val identRegex = Regex("[A-Za-z_][A-Za-z0-9_]*")

    private val API_DIRECTIVES = setOf(
        "param", "معامل", "return", "returns", "يعيد", "throws", "يرمي",
        "since", "منذ", "deprecated", "متوقف", "see", "انظر", "example", "مثال"
    )

    private var docApis: List<String> = emptyList()
    private var currentApis: List<String> = emptyList()

    /** يقرأ أسطر `مفتاح: قيمة` من بلوك حزمة إلى مفاتيح قانونية؛ المفاتيح المجهولة تُجمَع في [unknown] (سطر نسبي، مفتاح). */
    private fun packageKeys(body: List<String>, unknown: MutableList<Pair<Int, String>>?): LinkedHashMap<String, String> {
        val kv = LinkedHashMap<String, String>()
        for ((idx, line) in body.withIndex()) {
            val t = line.trim()
            if (t.isEmpty() || t.startsWith("\u061B\u061B")) continue
            val c = t.indexOf(':')
            if (c <= 0) continue
            val raw = t.substring(0, c).trim().lowercase().replace('_', '-').replace(' ', '-')
            val key = PKG_KEYS[raw]
            if (key == null) { unknown?.add(idx to raw); continue }
            kv.putIfAbsent(key, t.substring(c + 1).trim())
        }
        return kv
    }

    /** معلومات الحزمة القانونية (name/version/description/author/license/repo/…) من بلوك `؛؛؛ حزمة` في [markdown]؛ فارغة إن غاب. */
    fun extractPackageInfo(markdown: String): Map<String, String> {
        val lines = markdown.lines()
        var i = 0
        while (i < lines.size) {
            val open = arSemiFenceOpenRegex.find(lines[i].trim())
            if (open == null) { i++; continue }
            val scan = scanDataFence(lines, i)
            val type = open.groupValues[1].trim().split(whitespaceRegex)[0].lowercase()
            if (type == "package" || type == "حزمة") return packageKeys(scan.body, null)
            i = if (scan.next > i) scan.next else i + 1
        }
        return emptyMap()
    }

    /** يقارن اسم/إصدار README (`؛؛؛ حزمة`) بقيم manifest الحزمة (package.rin.json)؛ أي اختلاف يعود كخطأ. */
    fun crossCheckPackage(markdown: String, manifestName: String, manifestVersion: String): List<RdocIssue> {
        val info = extractPackageInfo(markdown)
        val issues = ArrayList<RdocIssue>()
        if (info.isEmpty()) {
            issues.add(RdocIssue(1, "warning", "لا يوجد بلوك ؛؛؛ حزمة في README"))
            return issues
        }
        info["name"]?.let { if (!it.equals(manifestName, ignoreCase = true)) issues.add(RdocIssue(1, "error", "اسم README ($it) يخالف manifest ($manifestName)")) }
        info["version"]?.let { if (it.removePrefix("v") != manifestVersion.removePrefix("v")) issues.add(RdocIssue(1, "error", "إصدار README ($it) يخالف manifest ($manifestVersion)")) }
        return issues
    }

    private fun collectApis(markdown: String): List<String> {
        val res = ArrayList<String>()
        var inFence = false
        for (raw in markdown.lines()) {
            val t = raw.trim()
            if (t.startsWith("```")) { inFence = !inFence; continue }
            if (inFence) continue
            val m = colonFenceOpenRegex.find(t) ?: continue
            val tokens = m.groupValues[1].trim().split(whitespaceRegex, limit = 2)
            if (CONTAINER_ALIASES[tokens[0].lowercase()] == "api") {
                val sig = tokens.getOrNull(1)?.trim().orEmpty()
                if (sig.isNotEmpty()) res.add(sig)
            }
        }
        return res
    }

    private fun apiIndexText(): String =
        if (currentApis.isEmpty()) "لا دوال موثَّقة في المستند." else currentApis.joinToString("\n") { "- `$it`" }

    private fun installText(body: List<String>): String {
        val cmds = body.map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("\u061B\u061B") }
        val list = if (cmds.isNotEmpty()) cmds else {
            val name = currentMeta["name"]
            if (name.isNullOrBlank()) emptyList() else listOf("rin install $name")
        }
        if (list.isEmpty()) return "\u2716 لا أوامر ولا اسم حزمة (`؛؛؛ حزمة`) لاستنتاج أمر التثبيت"
        return list.joinToString("\n") { "- [cmd: $it]" }
    }

    /** يحوّل توجيهات `@param/@return/@throws/@since/@deprecated/@see/@example` في جسم `::: دالة` إلى Markdown مقروء. */
    private fun apiText(body: List<String>): String {
        val res = ArrayList<String>()
        var inFence = false
        var paramsOpen = false
        fun blank() { if (res.isNotEmpty() && res.last().isNotBlank()) res.add("") }
        for (line in body) {
            val t = line.trim()
            if (t.startsWith("```")) { inFence = !inFence; res.add(line); continue }
            if (inFence) { res.add(line); continue }
            val m = apiDirectiveRegex.find(t)
            val d = m?.groupValues?.get(1)?.lowercase()
            if (m == null || d == null || d !in API_DIRECTIVES) {
                if (t.isNotEmpty()) paramsOpen = false
                res.add(line)
                continue
            }
            val v = m.groupValues[2].trim()
            when (d) {
                "param", "معامل" -> {
                    if (!paramsOpen) { blank(); res.add("**المعاملات**"); res.add(""); paramsOpen = true }
                    val pm = apiParamRegex.find(v)
                    if (pm == null) {
                        res.add("- $v")
                    } else {
                        val type = pm.groupValues[2].trim()
                        val desc = pm.groupValues[3].trim()
                        res.add("- `${pm.groupValues[1]}`" + (if (type.isNotEmpty()) " *($type)*" else "") + (if (desc.isNotEmpty()) " \u2014 $desc" else ""))
                    }
                }
                "return", "returns", "يعيد" -> { paramsOpen = false; blank(); res.add("**يعيد:** $v"); res.add("") }
                "throws", "يرمي" -> { paramsOpen = false; blank(); res.add("**يرمي:** $v"); res.add("") }
                "since", "منذ" -> { paramsOpen = false; blank(); res.add("[since: $v]"); res.add("") }
                "deprecated", "متوقف" -> { paramsOpen = false; blank(); res.add("[deprecated: ${v.ifBlank { "متوقفة" }}]"); res.add("") }
                "see", "انظر" -> { paramsOpen = false; blank(); res.add("**انظر:** *\"$v\"*"); res.add("") }
                else -> { paramsOpen = false; blank(); res.add("**مثال:**"); if (v.isNotEmpty()) res.add(v); res.add("") }
            }
        }
        return res.joinToString("\n")
    }

    /** بطاقة `؛؛؛ حزمة`: اسم + إصدار، وصف، ثم صفوف الناشر/الرخصة/المتطلبات/المنصات/الكلمات/أمر التثبيت/الروابط. */
    private fun appendPackageCard(out: SpannableStringBuilder, scan: FenceScan) {
        val kv = packageKeys(scan.body, null)
        val name = kv["name"]
        val cardStart = out.length
        val hs = out.length
        out.append("\uD83D\uDCE6 ${name ?: "حزمة"}")
        out.setSpan(StyleSpan(Typeface.BOLD), hs, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_HEADING), hs, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        kv["version"]?.let { v -> out.append("  "); appendTypedBracket(out, "version", v, v) }
        kv["description"]?.let { v -> out.append('\n'); appendInline(out, v) }
        fun row(label: String, block: () -> Unit) {
            out.append('\n')
            val ks = out.length
            out.append(label)
            out.setSpan(StyleSpan(Typeface.BOLD), ks, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(ForegroundColorSpan(COLOR_H_DIM), ks, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.append("   ")
            block()
        }
        kv["author"]?.let { v -> row("الناشر") { appendTypedBracket(out, "author", v, v) } }
        kv["license"]?.let { v -> row("الرخصة") { appendTypedBracket(out, "license", v, v) } }
        kv["requires"]?.let { v -> row("يتطلب") { appendTypedBracket(out, "requires", v, v) } }
        kv["platforms"]?.let { v -> row("المنصات") { appendTypedBracket(out, "platform", v.replace(",", "|"), v) } }
        kv["keywords"]?.let { v ->
            row("الكلمات") {
                v.split(",").map { it.trim() }.filter { it.isNotEmpty() }.forEachIndexed { idx, k ->
                    if (idx > 0) out.append(" ")
                    appendTypedBracket(out, "tag", k, k)
                }
            }
        }
        val install = kv["install"] ?: name?.let { "rin install $it" }
        install?.let { v -> row("التثبيت") { appendTypedBracket(out, "cmd", v, v) } }
        kv["repo"]?.let { v -> row("المستودع") { appendTypedBracket(out, "repo", v, v) } }
        kv["homepage"]?.let { v -> row("الموقع") { appendTypedBracket(out, "repo", v, v) } }
        if (name == null || kv["version"] == null) {
            out.append("  ")
            appendDiagnosticChip(out, "حزمة بلا name/version")
        }
        if (!scan.closed) {
            out.append("  ")
            appendDiagnosticChip(out, "بلوك ؛؛؛ غير مغلق")
        }
        val cardEnd = out.length
        out.setSpan(
            RoundedCardSpan(COLOR_TABLE_BG, COLOR_CARD_BORDER, cardStart, cardEnd),
            cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    /** بطاقة `؛؛؛ تبعيات`: سطر لكل تبعية `اسم = قيد [| ملاحظة]`؛ أسماء `@user/lib` روابط. */
    private fun appendDepsCard(out: SpannableStringBuilder, title: String, scan: FenceScan) {
        val cardStart = out.length
        val hs = out.length
        out.append("\uD83D\uDD17 ${title.ifBlank { "التبعيات" }}")
        out.setSpan(StyleSpan(Typeface.BOLD), hs, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_HEADING), hs, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        var any = false
        for (line in scan.body) {
            val t = line.trim()
            if (t.isEmpty() || t.startsWith("\u061B\u061B")) continue
            any = true
            out.append('\n')
            val m = depLineRegex.find(t)
            if (m == null) {
                appendDiagnosticChip(out, "تبعية غير صالحة: ${t.take(30)}")
                continue
            }
            val depName = m.groupValues[1]
            val constraint = m.groupValues[2].replace(whitespaceRegex, "")
            appendTypedBracket(out, "dep", "$depName@$constraint", t)
            val note = m.groupValues[3].trim()
            if (note.isNotEmpty()) {
                out.append("  ")
                appendInline(out, note)
            }
        }
        if (!any) {
            out.append('\n')
            appendDiagnosticChip(out, "بلوك تبعيات فارغ")
        }
        if (!scan.closed) {
            out.append("  ")
            appendDiagnosticChip(out, "بلوك ؛؛؛ غير مغلق")
        }
        val cardEnd = out.length
        out.setSpan(
            RoundedCardSpan(COLOR_TABLE_BG, COLOR_CARD_BORDER, cardStart, cardEnd),
            cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    /** يفحص بلوكات `؛؛؛ حزمة` و`؛؛؛ تبعيات` (اسم/إصدار/صيغ/روابط https/قيود) ويُسجّل المشاكل بأرقام الأسطر. */
    private fun validatePackageBlocks(lines: List<String>, issues: MutableList<RdocIssue>) {
        var inFence = false
        var i = 0
        while (i < lines.size) {
            val t = lines[i].trim()
            if (t.startsWith("```")) { inFence = !inFence; i++; continue }
            val open = if (inFence) null else arSemiFenceOpenRegex.find(t)
            if (open == null) { i++; continue }
            val scan = scanDataFence(lines, i)
            val type = open.groupValues[1].trim().split(whitespaceRegex)[0].lowercase()
            val ln = i + 1
            if (type == "package" || type == "حزمة") {
                val unknown = ArrayList<Pair<Int, String>>()
                val kv = packageKeys(scan.body, unknown)
                for ((off, k) in unknown) issues.add(RdocIssue(ln + 1 + off, "warning", "مفتاح حزمة غير معروف: $k"))
                val name = kv["name"]
                val version = kv["version"]
                if (name == null) issues.add(RdocIssue(ln, "error", "بلوك حزمة بلا name"))
                else if (!pkgNameRegex.matches(name)) issues.add(RdocIssue(ln, "error", "اسم حزمة غير صالح: $name"))
                if (version == null) issues.add(RdocIssue(ln, "error", "بلوك حزمة بلا version"))
                else if (!versionRegex.matches(version)) issues.add(RdocIssue(ln, "error", "إصدار غير صالح: $version"))
                kv["requires"]?.let { if (!requiresRegex.matches(it)) issues.add(RdocIssue(ln, "error", "يتطلب غير صالح: $it")) }
                kv["repo"]?.let { if (!isHttpsUrl(it.substringBefore('|').trim())) issues.add(RdocIssue(ln, "error", "repo يجب أن يكون https: $it")) }
                kv["homepage"]?.let { if (!isHttpsUrl(it.substringBefore('|').trim())) issues.add(RdocIssue(ln, "error", "homepage يجب أن يكون https: $it")) }
                kv["author"]?.let { if (it.startsWith("@") && canonicalRinUrl(it) == null) issues.add(RdocIssue(ln, "error", "ناشر غير صالح: $it")) }
                if (!scan.closed) issues.add(RdocIssue(ln, "error", "بلوك حزمة غير مغلق"))
            } else if (type == "deps" || type == "dependencies" || type == "تبعيات") {
                val seen = HashSet<String>()
                for ((off, line) in scan.body.withIndex()) {
                    val l = line.trim()
                    if (l.isEmpty() || l.startsWith("\u061B\u061B")) continue
                    val at = ln + 1 + off
                    val m = depLineRegex.find(l)
                    if (m == null) { issues.add(RdocIssue(at, "error", "تبعية غير صالحة (اسم = قيد): ${l.take(40)}")); continue }
                    val depName = m.groupValues[1]
                    if (!pkgNameRegex.matches(depName) || (depName.startsWith("@") && canonicalRinUrl(depName) == null)) {
                        issues.add(RdocIssue(at, "error", "اسم تبعية غير صالح: $depName"))
                    }
                    if (!constraintRegex.matches(m.groupValues[2].trim())) issues.add(RdocIssue(at, "error", "قيد إصدار غير صالح: ${m.groupValues[2].trim()}"))
                    if (!seen.add(depName.lowercase())) issues.add(RdocIssue(at, "warning", "تبعية مكرّرة: $depName"))
                }
                if (!scan.closed) issues.add(RdocIssue(ln, "error", "بلوك تبعيات غير مغلق"))
            }
            i = if (scan.next > i) scan.next else i + 1
        }
    }

    /** يفحص حاويات `::: دالة`: توقيع مطلوب، @param مقابل معاملات التوقيع (ناقص/زائد)، وتوجيهات @ مجهولة. */
    private fun validateApiDocs(lines: List<String>, issues: MutableList<RdocIssue>) {
        var inFence = false
        var i = 0
        while (i < lines.size) {
            val t = lines[i].trim()
            if (t.startsWith("```")) { inFence = !inFence; i++; continue }
            val m = if (inFence) null else colonFenceOpenRegex.find(t)
            if (m == null) { i++; continue }
            val tokens = m.groupValues[1].trim().split(whitespaceRegex, limit = 2)
            if (CONTAINER_ALIASES[tokens[0].lowercase()] != "api") { i++; continue }
            val ln = i + 1
            val sig = tokens.getOrNull(1)?.trim().orEmpty()
            val scan = scanFence(lines, i, colonFenceOpenRegex, colonFenceCloseRegex)
            if (sig.isEmpty()) issues.add(RdocIssue(ln, "error", "دالة بلا توقيع"))
            val sigParams = apiSigParamsRegex.find(sig)?.groupValues?.get(1)?.split(",")
                ?.mapNotNull { identRegex.find(it.trim())?.value }?.toSet()
            val documented = LinkedHashSet<String>()
            var bodyFence = false
            for ((off, line) in scan.body.withIndex()) {
                val b = line.trim()
                if (b.startsWith("```")) { bodyFence = !bodyFence; continue }
                if (bodyFence) continue
                val dm = apiDirectiveRegex.find(b) ?: continue
                val d = dm.groupValues[1].lowercase()
                val at = ln + 1 + off
                if (d !in API_DIRECTIVES) {
                    issues.add(RdocIssue(at, "warning", "توجيه غير معروف: @$d"))
                } else if (d == "param" || d == "معامل") {
                    val pn = apiParamRegex.find(dm.groupValues[2].trim())?.groupValues?.get(1)
                    if (pn == null) {
                        issues.add(RdocIssue(at, "error", "@param بلا اسم"))
                    } else {
                        if (!documented.add(pn)) issues.add(RdocIssue(at, "warning", "معامل مكرّر: $pn"))
                        if (sigParams != null && pn !in sigParams) issues.add(RdocIssue(at, "warning", "معامل غير موجود في التوقيع: $pn"))
                    }
                }
            }
            if (sigParams != null) {
                for (p in sigParams) if (p !in documented) issues.add(RdocIssue(ln, "warning", "معامل بلا توثيق: $p"))
            }
            if (!scan.closed) issues.add(RdocIssue(ln, "error", "حاوية دالة غير مغلقة"))
            i++
        }
    }

    // ─────────────────────────── الفحص الصارم ───────────────────────────

    /** مشكلة وجدها [validate]: رقم السطر (من 1)، الخطورة (`error`/`warning`)، الوصف. */
    class RdocIssue(val line: Int, val severity: String, val message: String)

    /**
     * يفحص مستند rdoc كاملاً بلا رسم: حجم، أسوار `:::`/`؛؛؛` (غير مغلقة/زائدة/نوع مجهول/عمق)، مراجع `*"id"*` المفقودة،
     * معرّفات مكرّرة، روابط دفع ووسائط غير صالحة. يعيد قائمة فارغة إن كان المستند سليماً.
     */
    fun validate(markdown: String): List<RdocIssue> {
        val issues = ArrayList<RdocIssue>()
        if (markdown.length > RDOC_MAX_CHARS) {
            issues.add(RdocIssue(1, "error", "المستند أكبر من الحد المسموح ($RDOC_MAX_CHARS حرفاً)"))
        }
        val lines = markdown.lines()
        val refs = collectElementRefs(markdown)
        parsePageSettings(markdown, issues)
        validatePackageBlocks(markdown.lines(), issues)
        validateApiDocs(markdown.lines(), issues)
        val metaAll = extractMeta(markdown)
        val ids = HashMap<String, Int>()
        var inFence = false
        var depth = 0
        var i = 0
        while (i < lines.size) {
            val t = lines[i].trim()
            val ln = i + 1
            if (t.startsWith("```")) {
                inFence = !inFence
                i++
                continue
            }
            if (inFence) {
                i++
                continue
            }
            if (t.startsWith("\u061B\u061B") && !t.startsWith("\u061B\u061B\u061B")) {
                i++
                continue
            }
            if (arSemiFenceOpenRegex.matches(t)) {
                val scan = scanDataFence(lines, i)
                val type = arSemiFenceOpenRegex.find(t)!!.groupValues[1].trim().split(whitespaceRegex)[0].lowercase()
                if (type !in DATA_BLOCK_TYPES) issues.add(RdocIssue(ln, "error", "بلوك ؛؛؛ غير معروف: $type"))
                if (!scan.closed) issues.add(RdocIssue(ln, "error", "بلوك ؛؛؛ $type غير مغلق"))
                for ((k, bl) in scan.body.withIndex()) {
                    val bt = bl.trim()
                    if (bt.isEmpty() || bt.startsWith("\u061B\u061B")) continue
                    val at = ln + k + 1
                    when (type) {
                        "links", "روابط" -> {
                            val lm = linkItemRegex.find(bt)
                            val target = lm?.groupValues?.get(2)?.split("|")?.get(0)?.trim().orEmpty()
                            val ok = lm != null && (isSafeLinkUrl(target) || resolveElement(target.trim('"', '*'), refs) != null)
                            if (!ok) issues.add(RdocIssue(at, "error", "رابط غير صالح في بلوك روابط: ${bt.take(40)}"))
                        }
                        "support", "دعم" -> if (parsePayment(bt) == null) issues.add(RdocIssue(at, "error", "مواصفة دفع غير صالحة: ${bt.take(40)}"))
                        "badges", "شارات" -> if (badgeItemRegex.find(bt) == null) issues.add(RdocIssue(at, "error", "شارة غير صالحة: ${bt.take(40)}"))
                    }
                }
                i = if (scan.next > i) scan.next else i + 1
                continue
            }
            if (colonFenceOpenRegex.matches(t)) {
                depth++
                val type = colonFenceOpenRegex.find(t)!!.groupValues[1].trim().split(whitespaceRegex)[0].lowercase()
                if (depth > RDOC_MAX_CONTAINER_DEPTH) {
                    issues.add(RdocIssue(ln, "error", "عمق الحاويات $depth يتجاوز الحد $RDOC_MAX_CONTAINER_DEPTH"))
                }
                if (CONTAINER_ALIASES[type] == null) issues.add(RdocIssue(ln, "error", "نوع حاوية غير معروف: $type"))
                if (CONTAINER_ALIASES[type] == "marquee") {
                    val header = colonFenceOpenRegex.find(t)!!.groupValues[1]
                    val title = header.trim().split(whitespaceRegex, limit = 2).getOrNull(1).orEmpty()
                    val opts = LinkedHashMap<String, String>()
                    for (m in tickerOptionRegex.findAll(title)) opts[m.groupValues[1].lowercase()] = m.groupValues[2]
                    val (o, err) = parseTickerOptions(opts, null, refs)
                    if (o == null) issues.add(RdocIssue(ln, "error", "خيار شريط غير صالح: $err"))
                }
            } else if (colonFenceCloseRegex.matches(t)) {
                if (depth == 0) issues.add(RdocIssue(ln, "error", "إغلاق ::: بلا فتح")) else depth--
            } else if (arSemiFenceCloseRegex.matches(t)) {
                issues.add(RdocIssue(ln, "error", "إغلاق ؛؛؛ بلا فتح"))
            }
            for (m in variableRegex.findAll(t)) {
                if (m.groupValues[1].isEmpty() && !metaAll.containsKey(m.groupValues[2].lowercase())) {
                    issues.add(RdocIssue(ln, "warning", "متغير غير معرّف: ${m.groupValues[2]}"))
                }
            }
            for (m in elementLinkScanRegex.findAll(t)) {
                val key = m.groupValues[1].split("|", limit = 3)[0].trim()
                if (resolveElement(key, refs) == null) issues.add(RdocIssue(ln, "error", "مرجع مفقود: $key"))
            }
            for (m in paymentScanRegex.findAll(t)) {
                if (parsePayment(m.groupValues[1]) == null) issues.add(RdocIssue(ln, "error", "رابط دفع غير صالح: ${m.groupValues[1]}"))
            }
            for (m in mediaScanRegex.findAll(t)) {
                val mediaErr = parseMediaEx(m.groupValues[1]).second
                if (mediaErr != null) issues.add(RdocIssue(ln, "error", "وسائط غير صالحة: $mediaErr"))
            }
            for (m in typedBracketScanRegex.findAll(t)) {
                val tmp = SpannableStringBuilder()
                appendTypedBracket(tmp, m.groupValues[1], m.groupValues[2], m.value)
                if (tmp.startsWith("\u2716")) {
                    issues.add(RdocIssue(ln, "error", "قيمة غير صالحة في [${m.groupValues[1]}: …]: ${m.groupValues[2].trim()}"))
                }
            }
            for (m in elementIdScanRegex.findAll(t)) {
                val id = m.groupValues[1].trim().lowercase()
                if (id.isEmpty()) continue
                if (ids.containsKey(id)) issues.add(RdocIssue(ln, "warning", "معرّف مكرّر: $id (الأول هو المعتمد)")) else ids[id] = ln
            }
            i++
        }
        if (depth > 0) issues.add(RdocIssue(lines.size, "error", "$depth حاوية ::: غير مغلقة"))
        return issues
    }

    // ───────────────────────────── خريطة الشجرة (Tree Map) ─────────────────────────────

    /** سطر واحد من خريطة الشجرة بعد التحليل. [depth]: 0 للجذر، 1 لأبناء الجذر... [cont] (المفتاح k): هل يمرّ
     *  خط المستوى k الرأسي عبر هذا السطر (أي أنّ سلفاً في ذلك المستوى له أشقّاء لاحقون). */
    private class TreeLine(
        val depth: Int,
        val cont: BooleanArray,
        val hasElbow: Boolean,
        val isLast: Boolean,
        val name: String,
        val comment: String,
        val note: Boolean,
        val blank: Boolean,
        var isDir: Boolean = false,
        var hasChildren: Boolean = false
    )

    private class TreeParse(val lines: List<TreeLine>, val dirs: Int, val files: Int)

    private const val TREE_VERTICALS = "│|¦┃║"
    private const val TREE_LAST_CONNECTORS = "└┗╚╙`\\"

    private val TREE_LANGS = setOf(
        "tree", "map", "treemap", "tree-map", "filetree", "file-tree", "dirtree", "structure", "files", "folders"
    )
    private val TREE_NEUTRAL_LANGS = setOf("text", "txt", "plain", "plaintext", "console")

    private val treeConnectorRegex = Regex(
        "^([│|¦┃║ ]*)([├┣╠╟]|[└┗╚╙]|\\+(?=-)|`(?=-)|\\\\(?=-)|\\|(?=-))[─━═\\-]*>?[ ]?(.*)$"
    )
    private val treePrefixRegex = Regex("^([│|¦┃║ ]*)(.*)$")
    private val treeUnicodeLineRegex = Regex("^[│|¦┃║ ]*[├└┣┗╠╚╟╙]")
    private val treeAsciiLineRegex = Regex("^[|\\s]*[|+`\\\\]-{2,}\\s+[^|\\s-]")
    private val treeCommentRegex = Regex("(?:^|\\s)(#|//|<-+|←|→|—|–|--)(?=\\s|$)")

    /** هل الكتلة خريطة شجرة؟ صريحاً عبر لغة (tree/map/...) أو تلقائياً لكتلة بلا لغة (أو text) تحوي
     *  ≥2 سطر بموصّلات `├`/`└` (وبلا رموز جداول `┼ ┤ ┬`) أو ≥3 أسطر ASCII بأسلوب `|--` / `+--`. */
    private fun isTreeBlock(lang: String, content: String): Boolean {
        val l = lang.trim().lowercase()
        if (l in TREE_LANGS) return content.isNotBlank()
        if (l.isNotEmpty() && l !in TREE_NEUTRAL_LANGS) return false
        val lines = content.replace("\t", "    ").lines()
        if (lines.any { it.contains('┼') || it.contains('┤') || it.contains('┬') }) return false
        if (lines.count { treeUnicodeLineRegex.containsMatchIn(it) } >= 2) return true
        return lines.count { treeAsciiLineRegex.containsMatchIn(it) } >= 3
    }

    private fun splitTreeComment(rest: String): Pair<String, String> {
        val m = treeCommentRegex.find(rest) ?: return rest.trim() to ""
        val idx = m.groups[1]!!.range.first
        return rest.substring(0, idx).trim() to rest.substring(idx).trim()
    }

    private fun parseTreeLines(raw: String): TreeParse {
        val src = raw.replace("\t", "    ").lines()

        // خطوة المستوى (عدد الأعمدة بين مستوى وآخر): أصغر إزاحة موجبة لموصّل، وإلا 4.
        var step = 0
        for (line in src) {
            val m = treeConnectorRegex.find(line) ?: continue
            val p = m.groupValues[1].length
            if (p > 0 && (step == 0 || p < step)) step = p
        }
        if (step < 2) step = 4

        val list = ArrayList<TreeLine>()
        for (line in src) {
            val m = treeConnectorRegex.find(line)
            if (m != null) {
                val prefix = m.groupValues[1]
                val level = prefix.length / step
                val depth = level + 1
                val cont = BooleanArray(depth) { k ->
                    k < level && (prefix.getOrNull(k * step)?.let { it in TREE_VERTICALS } == true)
                }
                val (name, comment) = splitTreeComment(m.groupValues[3])
                list.add(
                    TreeLine(
                        depth, cont, hasElbow = true, isLast = m.groupValues[2][0] in TREE_LAST_CONNECTORS,
                        name = name, comment = comment, note = false, blank = false
                    )
                )
                continue
            }
            val t = line.trimEnd()
            if (t.isBlank()) {
                list.add(TreeLine(0, BooleanArray(0), false, false, "", "", note = false, blank = true))
                continue
            }
            val pm = treePrefixRegex.find(t)!!
            val prefix = pm.groupValues[1]
            val (name, comment) = splitTreeComment(pm.groupValues[2])
            if (prefix.isEmpty()) {
                list.add(TreeLine(0, BooleanArray(1), false, false, name, comment, note = false, blank = false))
            } else {
                // سطر ملاحظة داخل الشجرة (نص بلا موصّل): يحافظ على استمرار الخطوط الرأسية فقط.
                val lvl = prefix.length / step
                val cont = BooleanArray(lvl + 1) { k -> k < lvl && (prefix.getOrNull(k * step)?.let { it in TREE_VERTICALS } == true) }
                list.add(TreeLine(lvl, cont, false, false, name, comment, note = true, blank = false))
            }
        }

        for (i in list.indices) {
            val ln = list[i]
            val next = list.getOrNull(i + 1)
            ln.hasChildren = !ln.note && !ln.blank && next != null && next.hasElbow && next.depth > ln.depth
            val plain = !ln.note && !ln.blank
            ln.isDir = plain && (ln.hasChildren || ln.name.endsWith("/") || ln.name.endsWith("\\"))
        }
        val named = list.filter { !it.note && !it.blank && it.name.isNotEmpty() }
        return TreeParse(list, named.count { it.isDir }, named.count { !it.isDir })
    }

    /** لون امتداد الملف (يُستخدم لأيقونة الملف ولاحقته): كود/إعدادات/وسائط/سكربتات/وثائق. */
    private fun treeExtColor(ext: String): Int = when (ext.lowercase()) {
        "kt", "kts", "java", "rin", "indsin", "cpp", "cc", "c", "h", "hpp", "py", "js", "ts", "tsx", "jsx",
        "cs", "go", "rs", "swift", "dart", "html", "css" -> COLOR_SYNTAX_KEYWORD
        "xml", "json", "yml", "yaml", "toml", "ini", "properties", "csv" -> COLOR_SYNTAX_STRING
        "png", "jpg", "jpeg", "webp", "gif", "svg", "ico", "mp3", "mp4", "ttf", "otf", "apk", "aab" -> COLOR_SYNTAX_NUMBER
        "sh", "bat", "cmd", "mk", "cmake", "gradle", "sln" -> COLOR_SYNTAX_DIRECTIVE
        "md", "txt", "rst", "pdf" -> COLOR_SYNTAX_COMMENT
        else -> COLOR_TREE_FILE
    }

    private fun treeAlpha(color: Int, alpha: Int): Int = (color and 0x00FFFFFF) or (alpha shl 24)

    /**
     * **خريطة الشجرة (Tree Map)**: بدل عرض `├── └── │` كنص أحادي المسافة داخل كتلة كود، تُرسَم بطاقة
     * "MAP" بخطوط توجيه حقيقية ([TreeGuideSpan]: خطوط رأسية ملوَّنة بحسب المستوى، وصلات مدوَّرة الزاوية
     * عند آخر الأشقّاء، أيقونات مجلد/ملف)، مجلدات عريضة، امتدادات ملفات ملوَّنة، وتعليقات (`# ...`)
     * مائلة خافتة، مع عدّاد مجلدات/ملفات في الرأس. النسخ يبقى للنص الخام الأصلي.
     */
    private fun appendTreeCard(out: SpannableStringBuilder, content: String) {
        val parsed = parseTreeLines(content)
        val pad = dpPx(14f)
        val step = dpPx(20f)
        val gap = dpPx(5f)
        val stroke = dpPx(1.4f).coerceAtLeast(1f)
        val guideColors = IntArray(BULLET_DEPTH_COLORS.size) { treeAlpha(adaptForTheme(BULLET_DEPTH_COLORS[it]), 0xB8) }

        val cardStart = out.length
        appendMapHeader(out, parsed.dirs, parsed.files, content)
        out.append('\n')
        val headerEnd = out.length

        val topPadStart = out.length
        out.append("\u00A0\n")
        out.setSpan(RelativeSizeSpan(0.45f), topPadStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        val bodyStart = out.length

        fun seg(text: String, color: Int, style: Int = Typeface.NORMAL) {
            if (text.isEmpty()) return
            val s = out.length
            out.append(text)
            out.setSpan(ForegroundColorSpan(color), s, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            if (style != Typeface.NORMAL) out.setSpan(StyleSpan(style), s, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }

        parsed.lines.forEachIndexed { idx, ln ->
            val lineStart = out.length
            out.append('\u202A') // LRE: نفس تثبيت اتجاه أسطر الكود داخل صفحة RTL (انظر forceLtrPerLine)
            var iconKind = 0
            var iconColor = COLOR_TREE_FILE

            if (ln.blank) {
                out.append('\u00A0')
            } else if (ln.name.isEmpty()) {
                out.append('\u00A0')
                iconKind = if (ln.note) 0 else if (ln.isDir) 1 else 3
                iconColor = if (ln.isDir) COLOR_TREE_FOLDER else COLOR_CODE_GUTTER
            } else if (ln.note) {
                seg(ln.name, COLOR_CODE_TEXT)
            } else if (ln.isDir) {
                val base = ln.name.trimEnd('/', '\\')
                seg(base, COLOR_HEADING, Typeface.BOLD)
                if (base.length < ln.name.length) seg("/", COLOR_SYNTAX_COMMENT)
                iconKind = 1
                iconColor = COLOR_TREE_FOLDER
            } else {
                val dot = ln.name.lastIndexOf('.')
                if (dot > 0 && dot < ln.name.length - 1) {
                    val ext = ln.name.substring(dot + 1)
                    iconColor = treeExtColor(ext)
                    seg(ln.name.substring(0, dot), COLOR_CODE_TEXT)
                    seg(ln.name.substring(dot), iconColor)
                } else {
                    seg(ln.name, COLOR_CODE_TEXT)
                }
                iconKind = 2
            }
            if (ln.comment.isNotEmpty()) {
                seg("  ", COLOR_CODE_TEXT)
                seg(ln.comment, COLOR_SYNTAX_COMMENT, Typeface.ITALIC)
            }
            out.append('\u202C')
            val lineEnd = out.length
            out.setSpan(
                TreeGuideSpan(
                    ln.depth, ln.cont, ln.hasElbow, ln.isLast, ln.hasChildren, iconKind, iconColor,
                    guideColors, pad, step, gap, stroke
                ),
                lineStart, lineEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            if (idx < parsed.lines.size - 1) out.append('\n')
        }
        val bodyEnd = out.length
        out.setSpan(TypefaceSpan("monospace"), bodyStart, bodyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.9f), bodyStart, bodyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        out.append('\n')
        val bottomPadStart = out.length
        out.append('\u00A0')
        out.setSpan(RelativeSizeSpan(0.45f), bottomPadStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        val cardEnd = out.length

        out.setSpan(LeadingMarginSpan.Standard(pad.toInt()), cardStart, bodyStart, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(LeadingMarginSpan.Standard(pad.toInt()), bottomPadStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(
            RoundedCardSpan(
                COLOR_CODE_BLOCK_BG, COLOR_CARD_BORDER, cardStart, cardEnd,
                cornerRadius = dpPx(12f), insetTop = dpPx(7f), insetBottom = dpPx(6f),
                borderWidth = dpPx(1f).coerceAtLeast(1f),
                headerBg = COLOR_CODE_HEADER_BG, headerEnd = headerEnd, headerDivider = COLOR_HEADER_RULE
            ),
            cardStart, cardEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    /** رأس بطاقة الخريطة: نقطة خضراء + "MAP" + عدّاد (مجلد/ملف) خافت + زر نسخ للنص الخام. */
    private fun appendMapHeader(out: SpannableStringBuilder, dirs: Int, files: Int, code: String) {
        val accent = adaptForTheme(COLOR_BULLET_L2)
        val dotStart = out.length
        out.append("\u25CF ")
        out.setSpan(ForegroundColorSpan(accent), dotStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.62f), dotStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        val labelStart = out.length
        out.append("MAP")
        out.setSpan(ForegroundColorSpan(accent), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(TypefaceSpan("monospace"), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.68f), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        if (dirs + files > 0) {
            out.append("  ")
            val metaStart = out.length
            // "N مجلد · M ملف"
            out.append("$dirs \u0645\u062C\u0644\u062F \u00B7 $files \u0645\u0644\u0641")
            out.setSpan(ForegroundColorSpan(COLOR_SYNTAX_COMMENT), metaStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(RelativeSizeSpan(0.62f), metaStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }

        out.append("  ")
        val copyStart = out.length
        out.append("\u29C9 \u0646\u0633\u062E")
        val copyEnd = out.length
        out.setSpan(StickerSpan(COLOR_COPY_BUTTON_BG, COLOR_CODE_TEXT), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.66f), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        // "تم نسخ الخريطة"
        out.setSpan(
            CopyCodeSpan(code, "\u062A\u0645 \u0646\u0633\u062E \u0627\u0644\u062E\u0631\u064A\u0637\u0629"),
            copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    /**
     * **إصلاح شكل/ستايل: اتجاه صحيح لكتل الكود داخل صفحة عربية RTL** — كود Rin غالباً يحتوي
     * سلاسل نصّية عربية (`articleMeta("عنوان المقال", ...)`)، وأول محرف قوي الاتجاه في السطر هو
     * من يقرِّر اتجاه الفقرة كاملاً عند Android؛ فإذا كان أول محرف عربياً يصبح السطر كله فقرة RTL،
     * فتُعاد الأقواس/الفواصل بصرياً بترتيب معكوس (المشكلة الظاهرة في الصورة: `;[` بدل `];`،
     * وسلاسل مثل `"2026-07-27"،` تظهر مقلوبة). الحل: نُطوِّق كل سطر كود بـ
     * LRE (`\u202A`) ... PDF (`\u202C`) — "تضمين" اتجاه، لا "تجاوز" — فتُثبَّت الفقرة كلّها LTR
     * بينما تبقى كل سلسلة عربية مُضمَّنة بداخلها تُرسَم صحيحة الاتجاه (RTL) ومقروءة كما كُتبت،
     * تماماً كما تعرض محرِّرات الأكواد المحترفة كوداً بلغة LTR ضمن مستند RTL. يُطبَّق على مستوى كل
     * سطر منفرد (لا الكتلة كاملة) لأن Android يحسب اتجاه كل سطر بين فواصل الأسطر (`\n`) بشكل
     * مستقل، فتضمين واحد يلفّ الكتلة كلها لن يبقى نافذاً عبر أسطرها.
     */
    private fun forceLtrPerLine(code: String): String =
        code.lines().joinToString("\n") { "\u202A$it\u202C" }

    /**
     * يحوّل كود [code] الخام إلى نص مُصنَّف الرموز عبر [codeTokenRegex]: تعليقات (مائلة، رمادية)،
     * نصوص حرفية (أخضر)، أرقام (برتقالي)، توجيهات `@...` (بنفسجي فاتح)، وسوم إغلاق `.end/...`
     * (ذهبي)، كلمات مفتاحية (أزرق، بارز)، استدعاءات دوال (ذهبي)، وأسماء مُسنَدة قبل `=` (برتقالي
     * فاتح) — كل رمز بلون منفصل صريح (لا نطاق لون عام يغطّي الكتلة) لتفادي أي تعارض بين Span
     * لون شامل وSpans الألوان الجزئية لكل رمز. [code] يصل هنا مُطوَّقاً بالفعل عبر
     * [forceLtrPerLine] (انظر موضع الاستدعاء في `flushCodeBlock`)، فمحارف LRE/PDF تمرّ بلا تلوين
     * (لا تُطابِق أي رمز في [codeTokenRegex]، فتقع ضمن الأجزاء "العادية" وتُلوَّن بلون النص الافتراضي
     * كأي محرف غير مرئي آخر — لا أثر بصري لها).
     */
    private fun appendHighlightedCode(out: SpannableStringBuilder, code: String) {
        var idx = 0
        for (match in codeTokenRegex.findAll(code)) {
            if (match.range.first > idx) {
                val plain = code.substring(idx, match.range.first)
                val plainStart = out.length
                out.append(plain)
                out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), plainStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            val g = match.groups
            val (value, color, style) = when {
                g[1] != null -> Triple(g[1]!!.value, COLOR_SYNTAX_COMMENT, Typeface.ITALIC)
                g[2] != null -> Triple(g[2]!!.value, COLOR_SYNTAX_COMMENT, Typeface.ITALIC)
                g[3] != null -> Triple(g[3]!!.value, COLOR_SYNTAX_STRING, Typeface.NORMAL)
                g[4] != null -> Triple(g[4]!!.value, COLOR_SYNTAX_DIRECTIVE, Typeface.BOLD)
                g[5] != null -> Triple(g[5]!!.value, COLOR_SYNTAX_BUILTIN, Typeface.NORMAL)
                g[6] != null -> Triple(g[6]!!.value, COLOR_SYNTAX_NUMBER, Typeface.NORMAL)
                g[7] != null -> Triple(g[7]!!.value, COLOR_SYNTAX_KEYWORD, Typeface.BOLD)
                g[8] != null -> Triple(g[8]!!.value, COLOR_SYNTAX_BUILTIN, Typeface.NORMAL)
                g[9] != null -> Triple(g[9]!!.value, COLOR_SYNTAX_ATTR, Typeface.NORMAL)
                else -> Triple(match.value, COLOR_CODE_TEXT, Typeface.NORMAL)
            }
            val start = out.length
            out.append(value)
            val end = out.length
            out.setSpan(ForegroundColorSpan(color), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            if (style != Typeface.NORMAL) out.setSpan(StyleSpan(style), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            idx = match.range.last + 1
        }
        if (idx < code.length) {
            val plainStart = out.length
            out.append(code.substring(idx))
            out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), plainStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    private fun isTableSeparator(line: String): Boolean {
        val t = line.trim()
        if (t.isEmpty() || !t.contains("-")) return false
        val cells = splitTableRow(t)
        if (cells.isEmpty()) return false
        return cells.all { tableSeparatorRegex.matches(it.trim()) }
    }

    /** فاصل خلايا `|` غير المسبوق بـ`\` — `\|` تُعرَض `|` حرفياً داخل الخلية. */
    private val unescapedPipeRegex = Regex("(?<!\\\\)\\|")

    private fun splitTableRow(line: String): List<String> {
        var l = line.trim()
        if (l.startsWith("|")) l = l.drop(1)
        if (l.endsWith("|") && !l.endsWith("\\|")) l = l.dropLast(1)
        return l.split(unescapedPipeRegex).map { it.trim().replace("\\|", "|") }
    }

    /** محاذاة كل عمود من سطر الفاصل: `:---` بداية، `---:` نهاية، `:---:` وسط (START/END تحترمان RTL). */
    private fun parseTableAligns(separatorLine: String): List<Int> =
        splitTableRow(separatorLine).map { cell ->
            val c = cell.trim()
            when {
                c.length > 1 && c.startsWith(":") && c.endsWith(":") -> Gravity.CENTER_HORIZONTAL
                c.endsWith(":") -> Gravity.END
                else -> Gravity.START
            }
        }

    /** يبني جدول Markdown كامل كصندوق نصّي أحادي المسافة بخطوط اتصال حقيقية (┌─┬─┐ ...). */
    private fun appendTable(out: SpannableStringBuilder, header: List<String>, rows: List<List<String>>) {
        val colCount = header.size
        val maxCellLen = 24
        fun cell(row: List<String>, col: Int): String {
            val raw = plainInlineText(row.getOrNull(col).orEmpty())
            return if (raw.length > maxCellLen) raw.take(maxCellLen - 1) + "…" else raw
        }
        val widths = IntArray(colCount) { col ->
            var w = cell(header, col).length
            for (row in rows) w = maxOf(w, cell(row, col).length)
            maxOf(w, 3)
        }

        fun border(left: String, mid: String, right: String, fill: String): String =
            left + widths.joinToString(mid) { fill.repeat(it + 2) } + right

        fun dataRow(row: List<String>): String =
            "│ " + (0 until colCount).joinToString(" │ ") { col -> cell(row, col).padEnd(widths[col]) } + " │"

        val start = out.length
        out.append(border("┌─", "─┬─", "─┐", "─")).append('\n')

        val headerLineStart = out.length
        out.append(dataRow(header))
        val headerLineEnd = out.length
        out.append('\n')

        out.append(border("├─", "─┼─", "─┤", "─")).append('\n')
        rows.forEachIndexed { idx, row ->
            val rowStart = out.length
            out.append(dataRow(row))
            if (idx % 2 == 1) {
                out.setSpan(
                    BackgroundColorSpan(COLOR_TABLE_ROW_ALT), rowStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }
            if (idx != rows.lastIndex) out.append('\n')
        }
        out.append('\n').append(border("└─", "─┴─", "─┘", "─"))
        val end = out.length

        out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.82f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_TABLE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(
            RoundedCardSpan(COLOR_TABLE_BG, COLOR_CARD_BORDER, start, end),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        out.setSpan(ForegroundColorSpan(COLOR_TABLE_BORDER), start, headerLineStart, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_TABLE_HEADER), headerLineStart, headerLineEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), headerLineStart, headerLineEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * شريحة نصّية بخلفية مُدوَّرة موحَّدة اللون ("ستيكر") — يقيس النص فعلياً عبر [Paint.measureText]
     * ويرسم خلفه مستطيلاً مدوَّر الزوايا، ثم يرسم النص فوقه بلون [fg] — نفس أسلوب القياس/الرسم
     * المتّبع في [RoundedCardSpan] أدناه لكن لعنصر سطري (inline) لا كتلة كاملة.
     */
    private class StickerSpan(
        private val bg: Int,
        private val fg: Int,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 14f,
        private val paddingV: Float = 4f,
        private val strokeColor: Int = 0,
        private val strokeWidth: Float = 0f
    ) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            return (paint.measureText(text, start, end) + paddingH * 2).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            val textWidth = paint.measureText(text, start, end)
            val rect = RectF(x, top.toFloat() + paddingV, x + textWidth + paddingH * 2, bottom.toFloat() - paddingV)
            paint.style = Paint.Style.FILL
            paint.color = bg
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            if (strokeWidth > 0f) {
                val savedStroke = paint.strokeWidth
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = strokeWidth
                paint.color = strokeColor
                val half = strokeWidth / 2f
                canvas.drawRoundRect(
                    RectF(rect.left + half, rect.top + half, rect.right - half, rect.bottom - half),
                    cornerRadius, cornerRadius, paint
                )
                paint.strokeWidth = savedStroke
                paint.style = Paint.Style.FILL
            }

            paint.color = fg
            canvas.drawText(text, start, end, x + paddingH, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * شارة تقدّم: نص [label] اختياري + شريط تقدّم مدوَّر مملوء بنسبة [percent] (0-100) + النسبة كنص —
     * كلها داخل حبّة واحدة بخلفية [bg]. تقيس عرضها بنفسها (لا تعتمد على النص الأصلي للنطاق).
     */
    private class ProgressBadgeSpan(
        private val label: String,
        private val percent: Int,
        private val bg: Int,
        private val labelColor: Int,
        private val trackColor: Int,
        private val fillColor: Int,
        private val textColor: Int,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 12f,
        private val paddingV: Float = 4f,
        private val barWidth: Float = 64f,
        private val barHeight: Float = 7f,
        private val gap: Float = 8f
    ) : ReplacementSpan() {
        private val pctText: String get() = "$percent%"

        private fun labelPartWidth(paint: Paint): Float =
            if (label.isBlank()) 0f else paint.measureText(label) + gap

        private fun totalWidth(paint: Paint): Float =
            paddingH * 2 + labelPartWidth(paint) + barWidth + gap + paint.measureText(pctText)

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            return totalWidth(paint).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true
            paint.style = Paint.Style.FILL

            val pill = RectF(x, top.toFloat() + paddingV, x + totalWidth(paint), bottom.toFloat() - paddingV)
            paint.color = bg
            canvas.drawRoundRect(pill, cornerRadius, cornerRadius, paint)

            var cx = x + paddingH
            if (label.isNotBlank()) {
                paint.color = labelColor
                canvas.drawText(label, cx, y.toFloat(), paint)
                cx += paint.measureText(label) + gap
            }
            val cy = (top + bottom) / 2f
            val track = RectF(cx, cy - barHeight / 2f, cx + barWidth, cy + barHeight / 2f)
            paint.color = trackColor
            canvas.drawRoundRect(track, barHeight / 2f, barHeight / 2f, paint)
            if (percent > 0) {
                val fillWidth = (barWidth * percent / 100f).coerceAtLeast(barHeight)
                paint.color = fillColor
                canvas.drawRoundRect(
                    RectF(cx, track.top, cx + fillWidth, track.bottom), barHeight / 2f, barHeight / 2f, paint
                )
            }
            cx += barWidth + gap
            paint.color = textColor
            canvas.drawText(pctText, cx, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * شارة بجزأين حقيقية بأسلوب شارات shields.io (جزء تسمية [label] رمادي داكن مُلاصِق مباشرة
     * لجزء رسالة [message] ملوَّن) — التقويس يظهر فقط على الطرفين الخارجيين (يسار التسمية/يمين
     * الرسالة) بينما يلتقي الجزءان بحافة مستقيمة في المنتصف، تماماً كصورة شارة shields.io حقيقية.
     * [label] الفارغ (null) يرسم شارة بجزء واحد فقط بلون [messageBg].
     */
    private class ShieldsBadgeSpan(
        private val label: String?,
        private val message: String,
        private val labelBg: Int,
        private val messageBg: Int,
        private val messageFg: Int,
        private val cornerRadius: Float = 8f,
        private val paddingH: Float = 10f,
        private val paddingV: Float = 3f
    ) : ReplacementSpan() {
        private val labelFg = 0xFFFFFFFF.toInt()

        private fun labelWidth(paint: Paint): Float =
            if (label != null) paint.measureText(label) + paddingH * 2 else 0f

        private fun messageWidth(paint: Paint): Float =
            paint.measureText(message) + paddingH * 2

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            return (labelWidth(paint) + messageWidth(paint)).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true
            paint.style = Paint.Style.FILL

            val lw = labelWidth(paint)
            val mw = messageWidth(paint)
            val topF = top.toFloat() + paddingV
            val bottomF = bottom.toFloat() - paddingV

            if (label != null) {
                val labelRect = RectF(x, topF, x + lw, bottomF)
                val labelPath = Path().apply {
                    addRoundRect(
                        labelRect,
                        floatArrayOf(cornerRadius, cornerRadius, 0f, 0f, 0f, 0f, cornerRadius, cornerRadius),
                        Path.Direction.CW
                    )
                }
                paint.color = labelBg
                canvas.drawPath(labelPath, paint)
                paint.color = labelFg
                canvas.drawText(label, x + paddingH, y.toFloat(), paint)
            }

            val messageRect = RectF(x + lw, topF, x + lw + mw, bottomF)
            val messagePath = Path().apply {
                val radii = if (label != null)
                    floatArrayOf(0f, 0f, cornerRadius, cornerRadius, cornerRadius, cornerRadius, 0f, 0f)
                else
                    floatArrayOf(
                        cornerRadius, cornerRadius, cornerRadius, cornerRadius,
                        cornerRadius, cornerRadius, cornerRadius, cornerRadius
                    )
                addRoundRect(messageRect, radii, Path.Direction.CW)
            }
            paint.color = messageBg
            canvas.drawPath(messagePath, paint)
            paint.color = messageFg
            canvas.drawText(message, x + lw + paddingH, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * شارة "ثنائية اللون" بخلفية واحدة موحَّدة [bg]: جزء التسمية [label] بلون خافت [labelColor]
     * (عادة [COLOR_H_DIM])، متبوعاً مباشرة بجزء القيمة [value] بلون بارز [valueColor] — عكس
     * [ShieldsBadgeSpan] لا خلفيتين منفصلتين بل خلفية واحدة ولونَي نص فقط، لشارات "مفتاح: قيمة"
     * العادية مثل `[* إصدار = (1.2.0) *]`.
     */
    private class BadgeTwoToneSpan(
        private val label: String,
        private val value: String,
        private val bg: Int,
        private val labelColor: Int,
        private val valueColor: Int,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 12f,
        private val paddingV: Float = 4f
    ) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            return (paint.measureText(text, start, end) + paddingH * 2).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            val textWidth = paint.measureText(text, start, end)
            val rect = RectF(x, top.toFloat() + paddingV, x + textWidth + paddingH * 2, bottom.toFloat() - paddingV)
            paint.style = Paint.Style.FILL
            paint.color = bg
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            val labelPart = "$label "
            paint.color = labelColor
            canvas.drawText(labelPart, x + paddingH, y.toFloat(), paint)
            val labelPartWidth = paint.measureText(labelPart)
            paint.color = valueColor
            canvas.drawText(value, x + paddingH + labelPartWidth, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * شارة "بنية هيكلية" (`hierarchy=(N)` أو `pyramid=(N)`): أيقونة أعمدة متصاعدة الارتفاع
     * (N منها، N بين 1 و6) تمثّل بصرياً عدد المستويات، مرسومة بألوان [palette] بالتناوب (تكرار
     * الدورة إن كان عدد المستويات أكبر من طول [palette])، متبوعة بنص الشارة بلون [textColor] على
     * خلفية موحَّدة [bg].
     */
    private class PyramidBadgeSpan(
        private val levels: Int,
        private val bg: Int,
        private val textColor: Int,
        private val palette: IntArray,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 12f,
        private val paddingV: Float = 4f,
        private val barWidth: Float = 5f,
        private val barGap: Float = 2f,
        private val iconTextGap: Float = 6f
    ) : ReplacementSpan() {
        private fun barsCount(): Int = levels.coerceIn(1, 6)

        private fun iconWidth(): Float {
            val bars = barsCount()
            return bars * barWidth + (bars - 1) * barGap
        }

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            val textWidth = paint.measureText(text, start, end)
            return (paddingH * 2 + iconWidth() + iconTextGap + textWidth).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            val textWidth = paint.measureText(text, start, end)
            val bars = barsCount()
            val iconW = iconWidth()
            val pillWidth = paddingH * 2 + iconW + iconTextGap + textWidth
            val rect = RectF(x, top.toFloat() + paddingV, x + pillWidth, bottom.toFloat() - paddingV)
            paint.style = Paint.Style.FILL
            paint.color = bg
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            val iconBottom = bottom.toFloat() - paddingV - 3f
            val maxBarHeight = (bottom - top).toFloat() - paddingV * 2 - 6f
            for (i in 0 until bars) {
                val barHeight = maxBarHeight * (i + 1) / bars
                val barLeft = x + paddingH + i * (barWidth + barGap)
                val barTop = iconBottom - barHeight
                paint.color = palette[i % palette.size]
                canvas.drawRoundRect(RectF(barLeft, barTop, barLeft + barWidth, iconBottom), 1.5f, 1.5f, paint)
            }

            paint.color = textColor
            canvas.drawText(text, start, end, x + paddingH + iconW + iconTextGap, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * شارة "عيّنة لون" (`color=(#hex)`): دائرة صغيرة مملوءة بلون [swatchColor] الفعلي (بحدّ رفيع
     * شبه شفّاف يبقيها مرئية حتى فوق خلفية فاتحة قريبة اللون) متبوعة بنص الشارة (تسمية اختيارية +
     * الكود السداسي) بلون [textColor] على خلفية موحَّدة [bg].
     */
    private class ColorSwatchSpan(
        private val swatchColor: Int,
        private val bg: Int,
        private val textColor: Int,
        private val cornerRadius: Float = 10f,
        private val paddingH: Float = 12f,
        private val paddingV: Float = 4f,
        private val swatchSize: Float = 14f,
        private val swatchGap: Float = 8f
    ) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            fm?.let {
                val orig = paint.fontMetricsInt
                it.ascent = orig.ascent; it.descent = orig.descent; it.top = orig.top; it.bottom = orig.bottom
            }
            val textWidth = paint.measureText(text, start, end)
            return (paddingH * 2 + swatchSize + swatchGap + textWidth).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            val textWidth = paint.measureText(text, start, end)
            val pillWidth = paddingH * 2 + swatchSize + swatchGap + textWidth
            val rect = RectF(x, top.toFloat() + paddingV, x + pillWidth, bottom.toFloat() - paddingV)
            paint.style = Paint.Style.FILL
            paint.color = bg
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            val centerY = (top + bottom) / 2f
            paint.color = swatchColor
            canvas.drawCircle(x + paddingH + swatchSize / 2f, centerY, swatchSize / 2f, paint)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 1.5f
            paint.color = 0x33000000
            canvas.drawCircle(x + paddingH + swatchSize / 2f, centerY, swatchSize / 2f, paint)

            paint.style = Paint.Style.FILL
            paint.color = textColor
            canvas.drawText(text, start, end, x + paddingH + swatchSize + swatchGap, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * بطاقة خلفية بزوايا مدوَّرة حقيقية (لا مستطيل خام) تمتد بعرض السطر خلف كل كتلة (كود/اقتباس/
     * جدول)، مع حدّ خفيف حول كامل البطاقة — التقويس يظهر فقط عند السطر الأول والسطر الأخير من
     * الكتلة (يُكتشَفان بمقارنة نطاق كل سطر مُمرَّر من [drawBackground] بحدود الـSpan نفسه
     * [spanStart]/[spanEnd])، أما الأسطر الوسطى فتبقى بحواف مستقيمة لتتّصل بصرياً بسلاسة.
     */
    private class RoundedCardSpan(
        private val bgColor: Int,
        private val borderColor: Int,
        private val spanStart: Int,
        private val spanEnd: Int,
        private val cornerRadius: Float = 16f,
        private val insetTop: Float = 5f,
        private val insetBottom: Float = 5f,
        private val borderWidth: Float = 2f,
        // اختياري (بطاقات الكود): شريط رأس بلون مختلف يغطّي الأسطر التي تبدأ قبل [headerEnd]،
        // مع خط فاصل رفيع بلون [headerDivider] أسفل آخر سطر منه. 0 = بلا رأس (سلوك قديم).
        private val headerBg: Int = 0,
        private val headerEnd: Int = -1,
        private val headerDivider: Int = 0
    ) : LineBackgroundSpan {
        override fun drawBackground(
            canvas: Canvas, paint: Paint,
            left: Int, right: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, lnum: Int
        ) {
            val isFirst = start <= spanStart
            val isLast = end >= spanEnd
            val hasHeader = headerBg != 0 && headerEnd > spanStart
            val isHeaderLine = hasHeader && start < headerEnd

            // الأسطر متجاورة تماماً في Layout؛ الامتداد الإضافي (inset) لأول سطر وآخر سطر فقط، وإلا
            // تراكبت خلفيات الأسطر الوسطى فغطّت جزءاً من حدّ الجانبين وحدّ الرأس عند كل التقاء.
            val half = borderWidth / 2f
            val rectTop = if (isFirst) top - insetTop else top.toFloat()
            val rectBottom = if (isLast) bottom + insetBottom else bottom.toFloat()
            val rect = RectF(left.toFloat() + half, rectTop, right.toFloat() - half, rectBottom)
            val corner = cornerRadius
            val radii = floatArrayOf(
                if (isFirst) corner else 0f, if (isFirst) corner else 0f, // أعلى-يسار
                if (isFirst) corner else 0f, if (isFirst) corner else 0f, // أعلى-يمين
                if (isLast) corner else 0f, if (isLast) corner else 0f,   // أسفل-يمين
                if (isLast) corner else 0f, if (isLast) corner else 0f    // أسفل-يسار
            )
            val path = Path().apply { addRoundRect(rect, radii, Path.Direction.CW) }

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            val savedWidth = paint.strokeWidth
            paint.isAntiAlias = true

            paint.style = Paint.Style.FILL
            paint.color = if (isHeaderLine) headerBg else bgColor
            canvas.drawPath(path, paint)

            // خط فاصل الرأس: يُرسَم أعلى أول سطر من الجسم (بعد تعبئته) كي لا تُغطّي تعبئة السطر
            // التالي نصفه.
            if (hasHeader && headerDivider != 0 && start == headerEnd) {
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = borderWidth
                paint.color = headerDivider
                val y = rect.top + borderWidth / 2f
                canvas.drawLine(rect.left, y, rect.right, y, paint)
            }

            // الحدّ: يُرسَم لكل سطر (الجانبان دائماً، والأعلى/الأسفل لأول/آخر سطر فقط) بقصّ المنطقة
            // بحيث لا تظهر خطوط أفقية داخلية بين الأسطر — سابقاً كان الحدّ يُرسَم حول أول وآخر سطر
            // فقط فيظهر خط فاصل تحت السطر الأول وتغيب الجوانب عن الأسطر الوسطى.
            if (borderWidth > 0f) {
                canvas.save()
                val clipTop = if (isFirst) rect.top - borderWidth else rect.top + borderWidth
                val clipBottom = if (isLast) rect.bottom + borderWidth else rect.bottom - borderWidth
                canvas.clipRect(rect.left - borderWidth, clipTop, rect.right + borderWidth, clipBottom)
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = borderWidth
                paint.color = borderColor
                canvas.drawPath(path, paint)
                canvas.restore()
            }

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
            paint.strokeWidth = savedWidth
        }
    }

    /** علامة قائمة نقطية تُرسَم في هامش أول سطر من العنصر: دائرة ممتلئة (عمق 0)، حلقة مفرغة
     *  (عمق 1)، مربّع مدوَّر (عمق 2)، تدور بعدها. ينفرد هذا الـSpan بالإزاحة والهامش المعلَّق
     *  معاً (بدل BulletSpan + LeadingMarginSpan.Standard المتراكبين سابقاً)، ويحترم RTL عبر [dir]. */
    private class ListMarkerSpan(
        private val color: Int,
        private val depth: Int,
        private val indentPx: Float,
        private val gutterPx: Float
    ) : LeadingMarginSpan {
        override fun getLeadingMargin(first: Boolean): Int = (indentPx + gutterPx).toInt()

        override fun drawLeadingMargin(
            c: Canvas, p: Paint, x: Int, dir: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, first: Boolean, layout: Layout?
        ) {
            if (!first) return
            val savedColor = p.color
            val savedStyle = p.style
            val savedAA = p.isAntiAlias
            val savedWidth = p.strokeWidth
            p.isAntiAlias = true
            p.color = color

            val radius = (p.textSize * 0.17f).coerceAtLeast(2.5f)
            val cx = x + dir * (indentPx + (gutterPx - dpPx(6f)) / 2f)
            val cy = baseline - p.textSize * 0.32f
            when (depth % 3) {
                0 -> {
                    p.style = Paint.Style.FILL
                    c.drawCircle(cx, cy, radius, p)
                }
                1 -> {
                    p.style = Paint.Style.STROKE
                    p.strokeWidth = (radius * 0.5f).coerceAtLeast(1.5f)
                    c.drawCircle(cx, cy, radius - p.strokeWidth / 2f, p)
                }
                else -> {
                    p.style = Paint.Style.FILL
                    val side = radius * 0.95f
                    c.drawRoundRect(RectF(cx - side, cy - side, cx + side, cy + side), side * 0.3f, side * 0.3f, p)
                }
            }

            p.color = savedColor
            p.style = savedStyle
            p.isAntiAlias = savedAA
            p.strokeWidth = savedWidth
        }
    }

    /** رقم عنصر قائمة مرقَّمة يُرسَم في هامش أول سطر بخط عريض ملوَّن، محاذى نحو النص (الأرقام
     *  متراصّة على الحافة نفسها مهما اختلف عدد خاناتها)، مع هامش معلَّق ثابت للأسطر الملتفّة. */
    private class OrderedMarkerSpan(
        private val label: String,
        private val color: Int,
        private val indentPx: Float,
        private val gutterPx: Float,
        private val gapPx: Float
    ) : LeadingMarginSpan {
        override fun getLeadingMargin(first: Boolean): Int = (indentPx + gutterPx).toInt()

        override fun drawLeadingMargin(
            c: Canvas, p: Paint, x: Int, dir: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, first: Boolean, layout: Layout?
        ) {
            if (!first) return
            val savedColor = p.color
            val savedStyle = p.style
            val savedBold = p.isFakeBoldText
            p.color = color
            p.style = Paint.Style.FILL
            p.isFakeBoldText = true
            val w = p.measureText(label)
            val drawX = if (dir > 0) x + indentPx + gutterPx - gapPx - w else x - indentPx - gutterPx + gapPx
            c.drawText(label, drawX, baseline.toFloat(), p)
            p.color = savedColor
            p.style = savedStyle
            p.isFakeBoldText = savedBold
        }
    }

    /** مربّع اختيار حقيقي لعنصر مهمّة `- [ ]`/`- [x]`: مربّع مدوَّر بحدّ للمعلَّق، ومملوء مع علامة
     *  صحّ بيضاء للمُنجَز. يُرسَم في هامش أول سطر ويحاذي وسط الخط عمودياً. */
    private class TaskBoxSpan(
        private val checked: Boolean,
        private val doneColor: Int,
        private val pendingColor: Int,
        private val indentPx: Float,
        private val gutterPx: Float
    ) : LeadingMarginSpan {
        override fun getLeadingMargin(first: Boolean): Int = (indentPx + gutterPx).toInt()

        override fun drawLeadingMargin(
            c: Canvas, p: Paint, x: Int, dir: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, first: Boolean, layout: Layout?
        ) {
            if (!first) return
            val savedColor = p.color
            val savedStyle = p.style
            val savedAA = p.isAntiAlias
            val savedWidth = p.strokeWidth
            val savedCap = p.strokeCap
            val savedJoin = p.strokeJoin
            p.isAntiAlias = true

            val size = p.textSize * 0.8f
            val cx = x + dir * (indentPx + size / 2f)
            val cy = baseline - p.textSize * 0.32f
            val box = RectF(cx - size / 2f, cy - size / 2f, cx + size / 2f, cy + size / 2f)
            val corner = size * 0.24f
            if (checked) {
                p.style = Paint.Style.FILL
                p.color = doneColor
                c.drawRoundRect(box, corner, corner, p)
                p.style = Paint.Style.STROKE
                p.color = 0xFFFFFFFF.toInt()
                p.strokeWidth = (size * 0.13f).coerceAtLeast(2f)
                p.strokeCap = Paint.Cap.ROUND
                p.strokeJoin = Paint.Join.ROUND
                val check = Path().apply {
                    moveTo(box.left + size * 0.24f, cy)
                    lineTo(box.left + size * 0.43f, cy + size * 0.2f)
                    lineTo(box.left + size * 0.78f, cy - size * 0.22f)
                }
                c.drawPath(check, p)
            } else {
                val bw = (size * 0.1f).coerceAtLeast(1.5f)
                p.style = Paint.Style.STROKE
                p.color = pendingColor
                p.strokeWidth = bw
                val inset = bw / 2f
                c.drawRoundRect(
                    RectF(box.left + inset, box.top + inset, box.right - inset, box.bottom - inset),
                    corner, corner, p
                )
            }

            p.color = savedColor
            p.style = savedStyle
            p.isAntiAlias = savedAA
            p.strokeWidth = savedWidth
            p.strokeCap = savedCap
            p.strokeJoin = savedJoin
        }
    }

    /** رقم سطر خافت في هامش كتلة الكود، مُحاذى لليمين نحو الكود، يُرسَم في أول سطر مرئي من كل سطر
     *  منطقي فقط (فلا يتكرّر على الأسطر الملتفّة) ويُحجَز له [gutterPx] كاملة دائماً. */
    private class CodeLineNumberSpan(
        private val label: String,
        private val color: Int,
        private val padPx: Float,
        private val gutterPx: Float,
        private val gapPx: Float
    ) : LeadingMarginSpan {
        override fun getLeadingMargin(first: Boolean): Int = (padPx + gutterPx + gapPx).toInt()

        override fun drawLeadingMargin(
            c: Canvas, p: Paint, x: Int, dir: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, first: Boolean, layout: Layout?
        ) {
            if (!first) return
            val savedColor = p.color
            val savedStyle = p.style
            val savedTypeface = p.typeface
            val savedSize = p.textSize
            p.color = color
            p.style = Paint.Style.FILL
            p.typeface = Typeface.MONOSPACE
            p.textSize = savedSize * 0.78f
            val w = p.measureText(label)
            val drawX = if (dir > 0) x + padPx + gutterPx - w else x - padPx - gutterPx
            c.drawText(label, drawX, baseline.toFloat(), p)
            p.color = savedColor
            p.style = savedStyle
            p.typeface = savedTypeface
            p.textSize = savedSize
        }
    }

    /**
     * هامش خريطة الشجرة: يرسم لكل سطر (1) الخطوط الرأسية للأسلاف المستمرّة، (2) وصلة السطر نفسه
     * (├ مستقيمة أو └ بزاوية مدوَّرة) نحو الأيقونة، (3) أيقونة مجلد/ملف/نقطة، (4) بداية خط أبنائه
     * إن كان له أبناء. الخطوط تمتدّ من [top] إلى [bottom] فتتّصل بين الأسطر بلا فجوات، وتستمرّ على
     * الأسطر الملتفّة. لكل مستوى لون من [guideColors]. الهامش ثابت الحجم بحسب [depth].
     * [icon]: 0 بلا، 1 مجلد، 2 ملف، 3 نقطة (عقدة بلا اسم).
     */
    private class TreeGuideSpan(
        private val depth: Int,
        private val cont: BooleanArray,
        private val hasElbow: Boolean,
        private val isLast: Boolean,
        private val hasChildren: Boolean,
        private val icon: Int,
        private val iconColor: Int,
        private val guideColors: IntArray,
        private val padPx: Float,
        private val stepPx: Float,
        private val gapPx: Float,
        private val strokePx: Float
    ) : LeadingMarginSpan {
        override fun getLeadingMargin(first: Boolean): Int = (padPx + (depth + 1) * stepPx + gapPx).toInt()

        override fun drawLeadingMargin(
            c: Canvas, p: Paint, x: Int, dir: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, first: Boolean, layout: Layout?
        ) {
            val savedColor = p.color
            val savedStyle = p.style
            val savedAA = p.isAntiAlias
            val savedWidth = p.strokeWidth
            val savedCap = p.strokeCap
            p.isAntiAlias = true
            p.style = Paint.Style.STROKE
            p.strokeWidth = strokePx
            p.strokeCap = Paint.Cap.BUTT

            val t = top.toFloat()
            val b = bottom.toFloat()
            val cy = baseline - p.textSize * 0.32f
            val x0 = x + dir * padPx
            val iw = stepPx * 0.62f
            val ih = iw * 0.82f
            fun cx(level: Int): Float = x0 + dir * (level * stepPx + stepPx / 2f)
            fun guide(level: Int): Int = guideColors[level % guideColors.size]

            // (1) خطوط الأسلاف المستمرّة
            val ancestors = if (hasElbow) depth - 1 else depth
            for (k in 0 until ancestors) {
                if (k < cont.size && cont[k]) {
                    p.color = guide(k)
                    c.drawLine(cx(k), t, cx(k), b, p)
                }
            }

            // (2) وصلة السطر نفسه
            if (hasElbow && depth >= 1) {
                val e = depth - 1
                val ex = cx(e)
                p.color = guide(e)
                if (first) {
                    val hEnd = cx(depth) - dir * (iw / 2f + strokePx * 1.5f)
                    if (isLast) {
                        val r = minOf(stepPx * 0.35f, (cy - t).coerceAtLeast(0f))
                        val path = Path()
                        path.moveTo(ex, t)
                        path.lineTo(ex, cy - r)
                        path.quadTo(ex, cy, ex + dir * r, cy)
                        path.lineTo(hEnd, cy)
                        c.drawPath(path, p)
                    } else {
                        c.drawLine(ex, t, ex, b, p)
                        c.drawLine(ex, cy, hEnd, cy, p)
                    }
                } else if (!isLast) {
                    c.drawLine(ex, t, ex, b, p)
                }
            }

            // (3) الأيقونة
            if (first && icon != 0) {
                val ix = cx(depth)
                when (icon) {
                    1 -> {
                        p.style = Paint.Style.FILL
                        p.color = iconColor
                        val tabH = ih * 0.32f
                        c.drawRoundRect(
                            RectF(ix - iw / 2f, cy - ih / 2f, ix - iw / 2f + iw * 0.45f, cy - ih / 2f + tabH * 1.6f),
                            ih * 0.14f, ih * 0.14f, p
                        )
                        c.drawRoundRect(
                            RectF(ix - iw / 2f, cy - ih / 2f + ih * 0.18f, ix + iw / 2f, cy + ih / 2f),
                            ih * 0.16f, ih * 0.16f, p
                        )
                    }
                    2 -> {
                        val w = iw * 0.78f
                        val h = ih * 1.18f
                        val l = ix - w / 2f
                        val r = ix + w / 2f
                        val tp = cy - h / 2f
                        val bt = cy + h / 2f
                        val fold = w * 0.38f
                        val doc = Path()
                        doc.moveTo(l, tp)
                        doc.lineTo(r - fold, tp)
                        doc.lineTo(r, tp + fold)
                        doc.lineTo(r, bt)
                        doc.lineTo(l, bt)
                        doc.close()
                        p.style = Paint.Style.FILL
                        p.color = treeAlpha(iconColor, 0x26)
                        c.drawPath(doc, p)
                        p.style = Paint.Style.STROKE
                        p.strokeWidth = strokePx
                        p.color = iconColor
                        c.drawPath(doc, p)
                        c.drawLine(r - fold, tp, r - fold, tp + fold, p)
                        c.drawLine(r - fold, tp + fold, r, tp + fold, p)
                    }
                    else -> {
                        p.style = Paint.Style.FILL
                        p.color = iconColor
                        c.drawCircle(ix, cy, iw * 0.16f, p)
                    }
                }
            }

            // (4) بداية خط الأبناء أسفل الأيقونة (يستمرّ على الأسطر الملتفّة أيضاً)
            if (hasChildren) {
                p.style = Paint.Style.STROKE
                p.strokeWidth = strokePx
                p.color = guide(depth)
                val y0 = if (first) cy + ih / 2f + strokePx else t
                c.drawLine(cx(depth), y0, cx(depth), b, p)
            }

            p.color = savedColor
            p.style = savedStyle
            p.isAntiAlias = savedAA
            p.strokeWidth = savedWidth
            p.strokeCap = savedCap
        }
    }

    /**
     * خط أفقي فاصل حقيقي يُرسم بعرض السطر كاملاً، بدل الاعتماد على سلسلة شرطات نصّية. يُعاد
     * استخدامه أيضاً كخط تحت العناوين H1/H2 (بلون وسُمك مختلفَين) لأسلوب README احترافي.
     */
    private class RuleSpan(
        private val color: Int = COLOR_RULE,
        private val thickness: Float = 2f
    ) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int = 0

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedWidth = paint.strokeWidth
            val savedCap = paint.strokeCap
            paint.color = color
            paint.strokeWidth = thickness
            paint.strokeCap = Paint.Cap.ROUND
            val middle = (top + bottom) / 2f
            canvas.drawLine(0f, middle, canvas.width.toFloat(), middle, paint)
            paint.color = savedColor
            paint.strokeWidth = savedWidth
            paint.strokeCap = savedCap
        }
    }

    /** شريط جانبي ملوَّن حقيقي (لا مجرّد مسافة بادئة) يُرسم على طول كل سطر من كتلة اقتباس `>`. */
    private class QuoteBarSpan(
        private val barColor: Int,
        private val barWidth: Float = 6f,
        private val gap: Float = 12f
    ) : LineBackgroundSpan {
        override fun drawBackground(
            canvas: Canvas, paint: Paint,
            left: Int, right: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, lnum: Int
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true
            paint.style = Paint.Style.FILL
            paint.color = barColor
            val barLeft = left.toFloat() + gap
            canvas.drawRect(barLeft, top.toFloat(), barLeft + barWidth, bottom.toFloat(), paint)
            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }
}

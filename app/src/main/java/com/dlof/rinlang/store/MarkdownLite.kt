package com.dlof.rinlang.store

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
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
import android.os.Handler
import android.os.Looper
import android.text.Spannable
import android.text.SpannableStringBuilder
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
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import android.view.View
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
 * الاستخدام المباشر: `textView.text = MarkdownLite.toSpannable(md)`.
 * الاستخدام الموصى به عند وجود روابط قابلة للنقر (أو أقسام قابلة للطي/خلفية صفحة/صور محلية):
 * `MarkdownLite.applyTo(textView, md, pageContainer, baseDir = ...)`.
 */
object MarkdownLite {

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

    private const val COLOR_RULE = 0xFF2D2D30.toInt()            // rin_job_card_border
    private const val COLOR_CARD_BORDER = 0x26FFFFFF             // حدّ خفيف موحّد لبطاقات الكود/الاقتباس/الجدول
    private const val COLOR_CODE_TEXT = 0xFFE3E5E8.toInt()       // rin_editor_text
    private const val COLOR_CODE_BLOCK_BG = 0x26FFFFFF           // خلفية محايدة خفيفة لكتلة الكود
    private const val COLOR_INLINE_CODE_BG = 0x33FFFFFF          // أغمق قليلاً لتمييز الكود المضمَّن عن السطر

    private const val COLOR_QUOTE_BAR = 0xFF7C5CFF.toInt()       // rin_accent
    private const val COLOR_QUOTE_BG = 0x1FFFFFFF                // rin_current_line_bg
    private const val COLOR_QUOTE_TEXT = 0xFF9198A3.toInt()      // rin_on_toolbar_dim

    private const val COLOR_LINK = 0xFF3B9EFF.toInt()            // rin_verified_badge
    private const val COLOR_HIGHLIGHT_BG = 0x4DFFC94D             // rin_star_gold_dim
    private const val COLOR_STRIKE_TEXT = 0xFF6E7480.toInt()     // rin_editor_hint

    private const val COLOR_TASK_DONE = 0xFF22C88E.toInt()       // rin_accent_green
    private const val COLOR_TASK_PENDING = 0xFF6E7480.toInt()    // rin_editor_hint
    private const val COLOR_H_DIM = 0xFF9198A3.toInt()           // rin_on_toolbar_dim

    private const val COLOR_TABLE_TEXT = 0xFFE3E5E8.toInt()      // rin_editor_text
    private const val COLOR_TABLE_HEADER = 0xFF7C5CFF.toInt()    // rin_accent
    private const val COLOR_TABLE_BORDER = 0xFF6E7480.toInt()    // rin_editor_hint
    private const val COLOR_TABLE_BG = 0x14FFFFFF                // أخفّ من خلفية كتلة الكود

    // لوحة تلوين نحوي (Syntax Palette) — نفس ألوان محرِّر Rin الحقيقي بالضبط (values-night/colors.xml)
    // حتى تبدو معاينة README جزءاً من هوية المحرِّر البصرية نفسها، لا لوحة مستقلة مختلَقة هنا.
    private const val COLOR_SYNTAX_KEYWORD = 0xFF569CD6.toInt()    // syntax_keyword
    private const val COLOR_SYNTAX_DIRECTIVE = 0xFFC586C0.toInt()  // syntax_container_keyword / syntax_make_directive
    private const val COLOR_SYNTAX_STRING = 0xFF6FDC9E.toInt()     // syntax_string
    private const val COLOR_SYNTAX_NUMBER = 0xFFFFA95C.toInt()     // syntax_number
    private const val COLOR_SYNTAX_COMMENT = 0xFF9AA0AB.toInt()    // syntax_comment
    private const val COLOR_SYNTAX_BUILTIN = 0xFFE6C260.toInt()    // syntax_builtin / syntax_tag
    private const val COLOR_SYNTAX_ATTR = 0xFFF2A65A.toInt()       // syntax_style_keyword

    // خط الفصل الرفيع أسفل رأس بطاقة الكود
    private const val COLOR_COPY_BUTTON_BG = 0x33FFFFFF
    private const val COLOR_HEADER_RULE = 0x1FFFFFFF

    /** لون هوية بصرية مميَّز لكل لغة (نقطة + وسم اسمها أعلى بطاقة الكود) — يسقط بهدوء إلى لون
     *  محايد للغات غير المدرَجة، فلا يتعطّل عرض أي كتلة كود بسبب اسم لغة غير معروف. */
    private val LANGUAGE_ACCENTS: Map<String, Int> = mapOf(
        "rin" to 0xFF7C5CFF.toInt(),
        "indsin" to 0xFF7C5CFF.toInt(),
        "kotlin" to 0xFFB197FC.toInt(),
        "kt" to 0xFFB197FC.toInt(),
        "java" to 0xFFEA9B4C.toInt(),
        "swift" to 0xFFF2784B.toInt(),
        "python" to 0xFFE6C260.toInt(),
        "py" to 0xFFE6C260.toInt(),
        "javascript" to 0xFFE9D85C.toInt(),
        "js" to 0xFFE9D85C.toInt(),
        "typescript" to 0xFF5C9DE9.toInt(),
        "ts" to 0xFF5C9DE9.toInt(),
        "json" to 0xFF9AA0AB.toInt(),
        "xml" to 0xFFE6C260.toInt(),
        "html" to 0xFFF2784B.toInt(),
        "css" to 0xFF5C9DE9.toInt(),
        "bash" to 0xFF6FDC9E.toInt(),
        "sh" to 0xFF6FDC9E.toInt(),
        "shell" to 0xFF6FDC9E.toInt(),
        "c" to 0xFF5C9DE9.toInt(),
        "cpp" to 0xFF5C9DE9.toInt(),
        "c++" to 0xFF5C9DE9.toInt(),
        "go" to 0xFF5CD6E9.toInt(),
        "rust" to 0xFFEA9B4C.toInt(),
        "sql" to 0xFF6FDC9E.toInt(),
        "yaml" to 0xFFEA9B4C.toInt(),
        "yml" to 0xFFEA9B4C.toInt()
    )
    private val LANGUAGE_ACCENT_DEFAULT = COLOR_CODE_TEXT

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
            "|\\*\\*\\*([^*]+?)\\*\\*\\*" +
            "|\\*\\*([^*]+?)\\*\\*" +
            "|__([^_]+?)__" +
            "|~~([^~]+?)~~" +
            "|`([^`]+?)`" +
            "|==([^=]+?)==" +
            "|\\[\\[([^\\]]+?)\\]\\]" +
            "|\\[\\*([^\\]]+?)\\*\\]" +
            "|!\\[([^\\]]*?)\\]\\((https?://img\\.shields\\.io/[^)\\s]+)\\)" +
            "|\\[([^\\]]+?)\\]\\(([^)\\s]+?)\\)" +
            "|\\*([^*]+?)\\*" +
            "|_([^_]+?)_" +
            "|!\\[([^\\]]*?)\\]\\(([^)\\s]+?)\\)"
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
        "NOTE" to CalloutStyle(0xFF3B9EFF.toInt(), "\u2139\uFE0F", "Note"),
        "TIP" to CalloutStyle(0xFF22C88E.toInt(), "\uD83D\uDCA1", "Tip"),
        "IMPORTANT" to CalloutStyle(0xFF7C5CFF.toInt(), "\u2757", "Important"),
        "WARNING" to CalloutStyle(0xFFFFC94D.toInt(), "\u26A0\uFE0F", "Warning"),
        "CAUTION" to CalloutStyle(0xFFF14C4C.toInt(), "\uD83D\uDED1", "Caution")
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
        val start = out.length
        val headerStart = out.length
        out.append(style.icon).append(' ').append(style.label)
        out.setSpan(StyleSpan(Typeface.BOLD), headerStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(style.color), headerStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
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
        out.setSpan(QuoteBarSpan(style.color), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(
            RoundedCardSpan(COLOR_QUOTE_BG, style.color, start, end),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
    }

    /**
     * يبني معاينة Markdown حقيقية جاهزة لعرضها مباشرة عبر `textView.text = MarkdownLite.toSpannable(md)`.
     *
     * @param baseDir مجلد أساس اختياري (مثال: `RinEngine.currentBaseDir()`) لحلّ مسارات صور محلية
     * نسبية في `![نص](مسار)` — انظر [appendImage]/[decodeLocalImage]. null = لا حلّ نسبي (صور بمسار
     * مطلق أو `file://` فقط)، بلا أي تغيير في السلوك القديم لأي استدعاء لا يمرِّره.
     * @param expandedSections مجموعة قابلة للتعديل بمعرِّفات أقسام `[~عنوان]` ... `[~/]` المفتوحة
     * حالياً (انظر [appendCollapsibleSection]) — نفس المجموعة يجب تمريرها في كل إعادة بناء لنفس
     * TextView حتى تبقى حالة الفتح/الإغلاق محفوظة بين استدعاء وآخر؛ [applyTo] يتكفّل بهذا تلقائياً.
     * @param onToggle يُستدعى بعد كل نقرة على رأس قسم قابل للطي (بعد تحديث [expandedSections])؛
     * المستدعي مسؤول عن إعادة بناء النص (مثال: استدعاء [applyTo] مجدَّداً بنفس TextView/المجموعة).
     */
    fun toSpannable(
        markdown: String,
        expandedSections: MutableSet<String> = mutableSetOf(),
        baseDir: String? = null,
        onToggle: (() -> Unit)? = null
    ): CharSequence {
        currentBaseDir = baseDir
        val out = SpannableStringBuilder()
        val lines = markdown.lines()
        var i = 0

        var inCodeBlock = false
        var codeLang = ""
        val codeBuffer = StringBuilder()
        var lastWasListItem = false
        var collapsibleAutoIndex = 0

        fun blockGap() {
            if (out.isNotEmpty()) out.append("\n\n")
        }

        fun flushCodeBlock() {
            if (codeBuffer.isEmpty() && codeLang.isBlank()) return
            blockGap()
            val content = codeBuffer.toString().trimEnd('\n')

            // بداية البطاقة الكاملة (رأس + خط فاصل + جسم الكود) — نطاق واحد متّصل حتى تُرسَم
            // الزوايا المدوَّرة والحدّ الخفيف حول الكل معاً، لا حول جسم الكود وحده.
            val cardStart = out.length
            appendCodeHeader(out, codeLang, content)
            out.append('\n')
            val ruleStart = out.length
            out.append('\u00A0')
            out.setSpan(RuleSpan(COLOR_HEADER_RULE, 1.5f), ruleStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.append('\n')

            val start = out.length
            appendHighlightedCode(out, forceLtrPerLine(content))
            val end = out.length
            out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(LeadingMarginSpan.Standard(18), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(
                RoundedCardSpan(COLOR_CODE_BLOCK_BG, COLOR_CARD_BORDER, cardStart, end),
                cardStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
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
            val indentSpaces = rawLine.length - rawLine.trimStart(' ').length
            val depth = (indentSpaces / 2).coerceIn(0, 2)

            when {
                trimmed.isEmpty() -> { lastWasListItem = false; i++ }

                // سطر خلفية الصفحة المستقل — يُستهلَك بصمت، لا يُعرَض كمحتوى مرئي إطلاقاً.
                // اللون الفعلي يُستخرَج لاحقاً عبر [extractPageBackground] من النص الخام كاملاً.
                pageBackgroundLineRegex.matches(trimmed) -> { lastWasListItem = false; i++ }

                // قسم وصف قابل للطي: `[~عنوان]` يبدأ الكتلة، `[~/]` وحدها على سطر مستقل تُنهيها.
                trimmed.startsWith("[~") && trimmed.endsWith("]") && trimmed != COLLAPSIBLE_CLOSE_MARKER -> {
                    val title = trimmed.removePrefix("[~").removeSuffix("]").trim()
                    val bodyLines = mutableListOf<String>()
                    var j = i + 1
                    while (j < lines.size && lines[j].trim() != COLLAPSIBLE_CLOSE_MARKER) {
                        bodyLines.add(lines[j]); j++
                    }
                    blockGap()
                    val sectionId = "sec${collapsibleAutoIndex++}:${title.ifBlank { "وصف" }}"
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
                    blockGap(); appendHeading(out, trimmed.removePrefix("## "), 1.28f, level = 2); lastWasListItem = false; i++
                }
                trimmed.startsWith("# ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("# "), 1.6f, level = 1); lastWasListItem = false; i++
                }

                taskListRegex.matches(trimmed) -> {
                    val m = taskListRegex.find(trimmed)!!
                    val checked = m.groupValues[1].equals("x", ignoreCase = true)
                    val content = m.groupValues[2]
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val lineStart = out.length
                    val glyphStart = out.length
                    out.append(if (checked) "\u2611 " else "\u2610 ")
                    out.setSpan(
                        ForegroundColorSpan(if (checked) COLOR_TASK_DONE else COLOR_TASK_PENDING),
                        glyphStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    val textStart = out.length
                    appendInline(out, content)
                    if (checked) {
                        out.setSpan(StrikethroughSpan(), textStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        out.setSpan(ForegroundColorSpan(COLOR_TASK_PENDING), textStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    }
                    out.setSpan(
                        LeadingMarginSpan.Standard(depth * 24, depth * 24 + 30),
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
                    val numStart = out.length
                    out.append("$number. ")
                    out.setSpan(StyleSpan(Typeface.BOLD), numStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(
                        ForegroundColorSpan(BULLET_DEPTH_COLORS[depth % BULLET_DEPTH_COLORS.size]),
                        numStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    appendInline(out, content)
                    out.setSpan(
                        LeadingMarginSpan.Standard(depth * 24, depth * 24 + 34),
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
                    out.setSpan(
                        BulletSpan(22, BULLET_DEPTH_COLORS[depth % BULLET_DEPTH_COLORS.size]),
                        lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    if (depth > 0) {
                        out.setSpan(
                            LeadingMarginSpan.Standard(depth * 24, depth * 24),
                            lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                        )
                    }
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
        return out
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
        baseDir: String? = null
    ) {
        // عرض حقيقي للصور المضمَّنة يناسب TextView الفعلي بدل قيمة تقديرية ثابتة دوماً؛ عرض الشاشة
        // الكامل كحدّ أقصى احتياطي إن لم يكن TextView قد قِيس بعد (width == 0 قبل أول تخطيط).
        currentImageMaxWidthPx = textView.width.takeIf { it > 0 }
            ?: (textView.resources.displayMetrics.widthPixels - (32 * textView.resources.displayMetrics.density).toInt())
                .coerceAtLeast(DEFAULT_IMAGE_MAX_WIDTH_PX)
        textView.text = toSpannable(markdown, expandedSections, baseDir) {
            applyTo(textView, markdown, pageContainer, expandedSections, baseDir)
        }
        textView.movementMethod = LinkMovementMethod.getInstance()
        textView.setLinkTextColor(COLOR_LINK)
        textView.highlightColor = COLOR_HIGHLIGHT_BG
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

    /**
     * يضيف نص عنوان بحجم [scale] النسبي وتشديد كامل، مع تطبيق تشديد أو كود داخلي إن وُجد.
     * العنوانان الأول والثاني ([level] 1 أو 2) يحصلان إضافياً على خط فاصل تحتهما — تماماً كأسلوب
     * عرض README الاحترافي على GitHub — لفصل الأقسام بصرياً بدل الاعتماد على المسافة فقط.
     */
    private fun appendHeading(out: SpannableStringBuilder, text: String, scale: Float, level: Int, dim: Boolean = false) {
        val start = out.length
        appendInline(out, text)
        val end = out.length
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(scale), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (dim) out.setSpan(ForegroundColorSpan(COLOR_H_DIM), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

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
        for (match in inlineRegex.findAll(text)) {
            if (match.range.first > idx) out.append(text.substring(idx, match.range.first))
            val g = match.groups
            when {
                g[1] != null -> out.append(g[1]!!.value) // \x حرف مُفلَت → حرفي بلا تنسيق
                g[2] != null -> appendStyled(out, g[2]!!.value, Typeface.BOLD_ITALIC)
                g[3] != null -> appendStyled(out, g[3]!!.value, Typeface.BOLD)
                g[4] != null -> appendStyled(out, g[4]!!.value, Typeface.BOLD)
                g[5] != null -> appendStrike(out, g[5]!!.value)
                g[6] != null -> appendCode(out, g[6]!!.value)
                g[7] != null -> appendHighlight(out, g[7]!!.value)
                g[8] != null -> appendSticker(out, g[8]!!.value)
                g[9] != null -> appendMetaBadge(out, g[9]!!.value)
                g[10] != null && g[11] != null -> appendShieldsBadge(out, g[10]!!.value, g[11]!!.value)
                g[12] != null && g[13] != null -> appendLink(out, g[12]!!.value, g[13]!!.value)
                g[14] != null -> appendStyled(out, g[14]!!.value, Typeface.ITALIC)
                g[15] != null -> appendStyled(out, g[15]!!.value, Typeface.ITALIC)
                g[16] != null && g[17] != null -> appendImage(out, g[16]!!.value, g[17]!!.value)
            }
            idx = match.range.last + 1
        }
        if (idx < text.length) out.append(text.substring(idx))
    }

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
        out.setSpan(URLSpan(url), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
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
        "github" to "\u2318"
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
     * لا رابط ولا قيمة مجرَّدة ولا لون ولا هرم (مثال: `[*جديد*]` وحدها) → يسقط بهدوء إلى ستيكر
     * عادي بلون الهوية الافتراضي عبر [appendSticker]، فلا يُفقَد المحتوى صمتاً بسبب صياغة ناقصة.
     */
    private fun appendMetaBadge(out: SpannableStringBuilder, raw: String) {
        val parts = raw.split("/").map { it.trim() }.filter { it.isNotEmpty() }
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

        // التسمية الأولى نفسها قد تكون لوناً مجرَّداً بلا نص (`[*#7C5CFF*]`) — عندها لا توجد
        // تسمية نصية منفصلة أصلاً.
        parseHexColor(label)?.let { color = it; label = "" }

        for (part in parts.drop(1)) {
            val m = metaBadgeKeyValueRegex.find(part)
            if (m != null) {
                when (m.groupValues[1].lowercase()) {
                    "link" -> linkUrl = m.groupValues[2].trim()
                    "icon" -> iconRaw = m.groupValues[2].trim()
                    "color" -> parseHexColor(m.groupValues[2].trim())?.let { color = it }
                    "hierarchy", "pyramid" -> hierarchyLevels = m.groupValues[2].trim().toIntOrNull()?.coerceIn(1, 6)
                    "palette" -> hierarchyPalette = NAMED_PALETTES[m.groupValues[2].trim().lowercase()]
                    "downloads" -> downloadsCount = m.groupValues[2].trim().replace(",", "").toLongOrNull()
                    "downloading" -> isDownloading = m.groupValues[2].trim().equals("true", ignoreCase = true)
                    "state" -> if (m.groupValues[2].trim().equals("downloading", ignoreCase = true)) isDownloading = true
                }
            } else {
                val asColor = parseHexColor(part)
                when {
                    asColor != null -> color = asColor
                    plainValue == null -> plainValue = part
                }
            }
        }

        when {
            linkUrl != null -> {
                val bg = color?.first ?: COLOR_BULLET
                val fg = if (color != null) contrastingTextColor(bg) else 0xFFFFFFFF.toInt()
                val glyph = iconGlyphFor(iconRaw)
                val shownLabel = label.ifBlank { color?.second.orEmpty() }
                val buttonText = if (glyph != null) "$glyph  $shownLabel" else shownLabel
                val start = out.length
                out.append(buttonText)
                val end = out.length
                out.setSpan(StickerSpan(bg, fg), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(LinkButtonClickSpan(linkUrl), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
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
        LANGUAGE_ACCENTS[lang.trim().lowercase()] ?: LANGUAGE_ACCENT_DEFAULT

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
    private class CopyCodeSpan(private val code: String) : ClickableSpan() {
        override fun onClick(widget: View) {
            val ctx = widget.context
            val clipboard = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
            clipboard?.setPrimaryClip(ClipData.newPlainText("code", code))
            Toast.makeText(ctx, "\u062A\u0645 \u0646\u0633\u062E \u0627\u0644\u0643\u0648\u062F", Toast.LENGTH_SHORT).show()
        }

        // بلا تسطير/لون رابط افتراضي — الشكل مُتحكَّم به بالكامل عبر StickerSpan المرافق لنفس النطاق.
        override fun updateDrawState(ds: TextPaint) {}
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

    private fun splitTableRow(line: String): List<String> {
        var l = line.trim()
        if (l.startsWith("|")) l = l.drop(1)
        if (l.endsWith("|")) l = l.dropLast(1)
        return l.split("|").map { it.trim() }
    }

    /** يبني جدول Markdown كامل كصندوق نصّي أحادي المسافة بخطوط اتصال حقيقية (┌─┬─┐ ...). */
    private fun appendTable(out: SpannableStringBuilder, header: List<String>, rows: List<List<String>>) {
        val colCount = header.size
        val maxCellLen = 24
        fun cell(row: List<String>, col: Int): String {
            val raw = row.getOrNull(col).orEmpty()
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
            out.append(dataRow(row))
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
        private val insetTop: Float = 3f,
        private val insetBottom: Float = 3f,
        private val borderWidth: Float = 2f
    ) : LineBackgroundSpan {
        override fun drawBackground(
            canvas: Canvas, paint: Paint,
            left: Int, right: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, lnum: Int
        ) {
            val isFirst = start <= spanStart
            val isLast = end >= spanEnd
            val rect = RectF(left.toFloat(), top - insetTop, right.toFloat(), bottom + insetBottom)
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
            paint.isAntiAlias = true

            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawPath(path, paint)

            if (isFirst || isLast) {
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = borderWidth
                paint.color = borderColor
                canvas.drawPath(path, paint)
            }

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
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
        private val color: Int,
        private val barWidthPx: Int = 6,
        private val gapPx: Int = 18
    ) : LeadingMarginSpan {
        override fun getLeadingMargin(first: Boolean): Int = barWidthPx + gapPx

        override fun drawLeadingMargin(
            canvas: Canvas, paint: Paint, x: Int, dir: Int,
            top: Int, baseline: Int, bottom: Int,
            text: CharSequence?, start: Int, end: Int,
            first: Boolean, layout: android.text.Layout?
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            paint.color = color
            paint.style = Paint.Style.FILL
            val barX = x.toFloat()
            canvas.drawRect(barX, top.toFloat(), barX + barWidthPx, bottom.toFloat(), paint)
            paint.color = savedColor
            paint.style = savedStyle
        }
    }

    /**
     * شارة شكل shields.io حقيقية: جزء تسمية (اختياري) بخلفية رمادية داكنة قياسية [labelBg]
     * ملاصِق مباشرة لجزء رسالة بخلفية [messageBg] (اللون المُستخرَج من مقطع اللون في الرابط) —
     * قطعتان متلاصقتان بلا فراغ بينهما، بزوايا مدوَّرة فقط على الطرفين الخارجيين (يسار الجزء
     * الأول ويمين الجزء الأخير)، تماماً كشكل شارات shields.io المألوف في ملفات README. بلا جزء
     * تسمية (`label == null`) تُرسَم كحبّة واحدة كاملة الاستدارة بلون الرسالة فقط. انظر
     * [appendShieldsBadge]/[parseShieldsBadgeUrl].
     */
    private class ShieldsBadgeSpan(
        private val label: String?,
        private val message: String,
        private val labelBg: Int,
        private val messageBg: Int,
        private val messageFg: Int
    ) : ReplacementSpan() {
        private val horizontalPad = 12f
        private val verticalPad = 5f
        private val cornerRadius = 12f
        private val labelFg = 0xFFFFFFFF.toInt()

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            if (fm != null) {
                val orig = paint.fontMetricsInt
                fm.ascent = orig.ascent - verticalPad.toInt()
                fm.descent = orig.descent + verticalPad.toInt()
                fm.top = fm.ascent
                fm.bottom = fm.descent
            }
            val labelW = if (label != null) horizontalPad * 2 + paint.measureText(label) else 0f
            val messageW = horizontalPad * 2 + paint.measureText(message)
            return (labelW + messageW).toInt()
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

            val hasLabel = label != null
            val labelW = if (hasLabel) horizontalPad * 2 + paint.measureText(label) else 0f
            val messageW = horizontalPad * 2 + paint.measureText(message)
            val rectTop = top.toFloat() + 1f
            val rectBottom = bottom.toFloat() - 1f

            if (hasLabel) {
                val leftRect = RectF(x, rectTop, x + labelW, rectBottom)
                // مدوَّرة عند أعلى/أسفل-يسار فقط (الطرف الخارجي)، مستقيمة عند اليمين (يلاصق جزء الرسالة)
                val leftRadii = floatArrayOf(
                    cornerRadius, cornerRadius, 0f, 0f, 0f, 0f, cornerRadius, cornerRadius
                )
                paint.color = labelBg
                canvas.drawPath(Path().apply { addRoundRect(leftRect, leftRadii, Path.Direction.CW) }, paint)
                paint.color = labelFg
                canvas.drawText(label!!, x + horizontalPad, y.toFloat(), paint)
            }

            val rightRect = RectF(x + labelW, rectTop, x + labelW + messageW, rectBottom)
            val rightRadii = if (hasLabel) {
                // مدوَّرة عند أعلى/أسفل-يمين فقط (الطرف الخارجي)، مستقيمة عند اليسار (تلاصق التسمية)
                floatArrayOf(0f, 0f, cornerRadius, cornerRadius, cornerRadius, cornerRadius, 0f, 0f)
            } else {
                floatArrayOf(
                    cornerRadius, cornerRadius, cornerRadius, cornerRadius,
                    cornerRadius, cornerRadius, cornerRadius, cornerRadius
                )
            }
            paint.color = messageBg
            canvas.drawPath(Path().apply { addRoundRect(rightRect, rightRadii, Path.Direction.CW) }, paint)
            paint.color = messageFg
            canvas.drawText(message, x + labelW + horizontalPad, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * "ستيكر Rin": بادج ملوَّن بخلفية حقيقية بزوايا مدوَّرة يُرسم عبر Canvas مباشرة (لا مجرّد
     * تلوين نص)، بنفس روح شارات "موثّق"/إحصاءات التطبيق — مكوّن بصري جديد كلياً في هذا الملف.
     */
    private class StickerSpan(
        private val bgColor: Int,
        private val textColor: Int
    ) : ReplacementSpan() {
        private val horizontalPad = 16f
        private val verticalPad = 5f
        private val cornerRadius = 16f

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            if (fm != null) {
                val orig = paint.fontMetricsInt
                fm.ascent = orig.ascent - verticalPad.toInt()
                fm.descent = orig.descent + verticalPad.toInt()
                fm.top = fm.ascent
                fm.bottom = fm.descent
            }
            return (paint.measureText(text, start, end) + horizontalPad * 2).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val width = paint.measureText(text, start, end)
            val rect = RectF(x, top.toFloat() + 1f, x + width + horizontalPad * 2, bottom.toFloat() - 1f)

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias

            paint.isAntiAlias = true
            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            paint.color = textColor
            canvas.drawText(text, start, end, x + horizontalPad, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * شارة إصدار ثنائية اللون داخل حبّة واحدة (مثال: `الإصدار` بلون خافت + `1` بلون الهوية
     * بارزاً) — تُستخدَم لصياغة `[*تسمية/قيمة*]` (انظر [appendMetaBadge]). بخلاف [StickerSpan]
     * (لون نص واحد)، هذا الصنف يرسم النص بلونَين منفصلَين صراحةً داخل [draw] بدل الاعتماد على
     * Spans متداخلة (تُتجاهَل داخل نطاق أي [ReplacementSpan] لأن الرسم يُسلَّم إليه كاملاً).
     */
    private class BadgeTwoToneSpan(
        private val label: String,
        private val value: String,
        private val bgColor: Int,
        private val labelColor: Int,
        private val valueColor: Int
    ) : ReplacementSpan() {
        private val horizontalPad = 14f
        private val verticalPad = 5f
        private val cornerRadius = 14f
        private val gap = 6f

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            if (fm != null) {
                val orig = paint.fontMetricsInt
                fm.ascent = orig.ascent - verticalPad.toInt()
                fm.descent = orig.descent + verticalPad.toInt()
                fm.top = fm.ascent
                fm.bottom = fm.descent
            }
            return (horizontalPad * 2 + paint.measureText(label) + gap + paint.measureText(value)).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val labelW = paint.measureText(label)
            val valueW = paint.measureText(value)
            val rect = RectF(x, top.toFloat() + 1f, x + horizontalPad * 2 + labelW + gap + valueW, bottom.toFloat() - 1f)

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            val savedBold = paint.isFakeBoldText

            paint.isAntiAlias = true
            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            paint.color = labelColor
            canvas.drawText(label, x + horizontalPad, y.toFloat(), paint)

            paint.color = valueColor
            paint.isFakeBoldText = true
            canvas.drawText(value, x + horizontalPad + labelW + gap, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
            paint.isFakeBoldText = savedBold
        }
    }

    /**
     * "نقطة لون مدمَجة" داخل حبّة واحدة: دائرة مملوءة بلون [swatchColor] الحقيقي (المُستخرَج من
     * صياغة `#RRGGBB` في [appendMetaBadge]) + نص (تسمية و/أو الرمز السداسي) بجانبها مباشرة —
     * كل ذلك عنصر بصري واحد مُدمَج (لا نقطة منفصلة عن حبّة نصّية أخرى)، بحدّ رفيع شبه شفاف حول
     * الدائرة لتبقى مقروءة حتى لو قارب لونها لون خلفية البطاقة نفسها.
     */
    private class ColorSwatchSpan(
        private val swatchColor: Int,
        private val bgColor: Int,
        private val textColor: Int
    ) : ReplacementSpan() {
        private val horizontalPad = 14f
        private val verticalPad = 5f
        private val cornerRadius = 14f
        private val dotRadius = 6f
        private val dotGap = 8f

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            if (fm != null) {
                val orig = paint.fontMetricsInt
                fm.ascent = orig.ascent - verticalPad.toInt()
                fm.descent = orig.descent + verticalPad.toInt()
                fm.top = fm.ascent
                fm.bottom = fm.descent
            }
            return (horizontalPad * 2 + dotRadius * 2 + dotGap + paint.measureText(text, start, end)).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val textW = paint.measureText(text, start, end)
            val totalW = horizontalPad * 2 + dotRadius * 2 + dotGap + textW
            val rect = RectF(x, top.toFloat() + 1f, x + totalW, bottom.toFloat() - 1f)

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            val savedWidth = paint.strokeWidth
            paint.isAntiAlias = true

            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            val centerY = (top + bottom) / 2f
            val dotCx = x + horizontalPad + dotRadius
            paint.color = swatchColor
            canvas.drawCircle(dotCx, centerY, dotRadius, paint)
            paint.style = Paint.Style.STROKE
            paint.strokeWidth = 1.5f
            paint.color = 0x40FFFFFF
            canvas.drawCircle(dotCx, centerY, dotRadius, paint)

            paint.style = Paint.Style.FILL
            paint.color = textColor
            canvas.drawText(text, start, end, x + horizontalPad + dotRadius * 2 + dotGap, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
            paint.strokeWidth = savedWidth
        }
    }

    /**
     * "وسام الهرم الهيكلي": أيقونة هرم حقيقية مرسومة داخل الحبّة — قمّة ضيّقة أعلى الهرم إلى
     * قاعدة عريضة أسفله، مقسَّمة أفقياً إلى [levels] طبقة (كل طبقة شبه منحرف عُرضه يتّسع من
     * القمة للقاعدة، بلون عمق مختلف عبر [pyramidTierColor]) — تمثيل بصري مباشر لعدد مستويات
     * التعشيش في بنية صفحة أو تخطيط عناصر (مثال Rin: `Scaffold ← TopBar/Column ← عناصرها`).
     * انظر صياغة `[*تسمية/hierarchy=(N)*]` في [appendMetaBadge].
     */
    private class PyramidBadgeSpan(
        private val levels: Int,
        private val bgColor: Int,
        private val textColor: Int,
        private val tierColors: IntArray = BULLET_DEPTH_COLORS
    ) : ReplacementSpan() {
        private val horizontalPad = 14f
        private val verticalPad = 5f
        private val cornerRadius = 14f
        private val iconW = 16f
        private val iconH = 14f
        private val iconGap = 8f

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            if (fm != null) {
                val orig = paint.fontMetricsInt
                fm.ascent = orig.ascent - verticalPad.toInt()
                fm.descent = orig.descent + verticalPad.toInt()
                fm.top = fm.ascent
                fm.bottom = fm.descent
            }
            return (horizontalPad * 2 + iconW + iconGap + paint.measureText(text, start, end)).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val textW = paint.measureText(text, start, end)
            val totalW = horizontalPad * 2 + iconW + iconGap + textW
            val rect = RectF(x, top.toFloat() + 1f, x + totalW, bottom.toFloat() - 1f)

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            // الهرم نفسه: [levels] شبه منحرف مكدَّسة رأسياً — القمّة (i=0) بلا عرض تقريباً،
            // القاعدة (i=levels-1) بعرض iconW كاملاً، بفاصل رفيع شبه شفاف بين كل طبقتين.
            val centerY = (top + bottom) / 2f
            val iconTop = centerY - iconH / 2f
            val iconCx = x + horizontalPad + iconW / 2f
            val bandHeight = iconH / levels
            for (i in 0 until levels) {
                val bandTopY = iconTop + i * bandHeight
                val bandBottomY = iconTop + (i + 1) * bandHeight
                val topHalfW = (iconW / 2f) * (i * bandHeight / iconH)
                val bottomHalfW = (iconW / 2f) * ((i + 1) * bandHeight / iconH)
                val path = Path().apply {
                    moveTo(iconCx - topHalfW, bandTopY)
                    lineTo(iconCx + topHalfW, bandTopY)
                    lineTo(iconCx + bottomHalfW, bandBottomY)
                    lineTo(iconCx - bottomHalfW, bandBottomY)
                    close()
                }
                paint.color = pyramidTierColor(i, tierColors)
                canvas.drawPath(path, paint)
            }

            paint.style = Paint.Style.FILL
            paint.color = textColor
            canvas.drawText(text, start, end, x + horizontalPad + iconW + iconGap, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }
}

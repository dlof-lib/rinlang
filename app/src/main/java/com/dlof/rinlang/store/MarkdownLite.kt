package com.dlof.rinlang.store

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.method.LinkMovementMethod
import android.text.style.BackgroundColorSpan
import android.text.style.BulletSpan
import android.text.style.ForegroundColorSpan
import android.text.style.LeadingMarginSpan
import android.text.style.LineBackgroundSpan
import android.text.style.RelativeSizeSpan
import android.text.style.ReplacementSpan
import android.text.style.StrikethroughSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import android.text.style.UnderlineSpan
import android.widget.TextView

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
 *
 * الاستخدام المباشر: `textView.text = MarkdownLite.toSpannable(md)`.
 * الاستخدام الموصى به عند وجود روابط قابلة للنقر: `MarkdownLite.applyTo(textView, md)`.
 */
object MarkdownLite {

    // ألوان مأخوذة من نفس لوحة التطبيق (colors.xml) لتبدو المعاينة جزءاً من التطبيق لا دخيلة عليه
    private const val COLOR_BULLET = 0xFF7C5CFF.toInt()          // rin_accent
    private const val COLOR_BULLET_L2 = 0xFF22C88E.toInt()       // rin_accent_green
    private const val COLOR_BULLET_L3 = 0xFFFFC94D.toInt()       // rin_star_gold
    private val BULLET_DEPTH_COLORS = intArrayOf(COLOR_BULLET, COLOR_BULLET_L2, COLOR_BULLET_L3)

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
    //  1) escape حرف مُفلَت حرفياً        7) [[ستيكر]] أو [[ستيكر|لون]]
    //  2) ***تشديد+مائل***                8/9) [نص](رابط)
    //  3) **تشديد**                       10) *مائل*
    //  4) __تشديد بديل__                  11) _مائل بديل_
    //  5) ~~يتوسّطه خط~~
    //  6) `كود مضمَّن`
    //  7) ==تمييز==
    private val inlineRegex = Regex(
        "\\\\([\\\\`*_{}\\[\\]()#+.!~=>-])" +
            "|\\*\\*\\*([^*]+?)\\*\\*\\*" +
            "|\\*\\*([^*]+?)\\*\\*" +
            "|__([^_]+?)__" +
            "|~~([^~]+?)~~" +
            "|`([^`]+?)`" +
            "|==([^=]+?)==" +
            "|\\[\\[([^\\]]+?)\\]\\]" +
            "|\\[([^\\]]+?)\\]\\(([^)\\s]+?)\\)" +
            "|\\*([^*]+?)\\*" +
            "|_([^_]+?)_"
    )

    private val orderedListRegex = Regex("^(\\d{1,4})[.)]\\s+(.*)$")
    private val taskListRegex = Regex("^[-*+]\\s+\\[([ xX])]\\s+(.*)$")
    private val bulletListRegex = Regex("^[-*+]\\s+(.*)$")
    private val tableSeparatorRegex = Regex("^:?-{2,}:?$")

    /** يبني معاينة Markdown حقيقية جاهزة لعرضها مباشرة عبر `textView.text = MarkdownLite.toSpannable(md)`. */
    fun toSpannable(markdown: String): CharSequence {
        val out = SpannableStringBuilder()
        val lines = markdown.lines()
        var i = 0

        var inCodeBlock = false
        var codeLang = ""
        val codeBuffer = StringBuilder()
        var lastWasListItem = false

        fun blockGap() {
            if (out.isNotEmpty()) out.append("\n\n")
        }

        fun flushCodeBlock() {
            if (codeBuffer.isEmpty() && codeLang.isBlank()) return
            blockGap()
            if (codeLang.isNotBlank()) appendLanguageChip(out, codeLang)
            val content = codeBuffer.toString().trimEnd('\n')
            val start = out.length
            out.append(content)
            val end = out.length
            out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(LeadingMarginSpan.Standard(18), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(
                RoundedCardSpan(COLOR_CODE_BLOCK_BG, COLOR_CARD_BORDER, start, end),
                start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
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
    fun applyTo(textView: TextView, markdown: String) {
        textView.text = toSpannable(markdown)
        textView.movementMethod = LinkMovementMethod.getInstance()
        textView.setLinkTextColor(COLOR_LINK)
        textView.highlightColor = COLOR_HIGHLIGHT_BG
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
                g[9] != null && g[10] != null -> appendLink(out, g[9]!!.value, g[10]!!.value)
                g[11] != null -> appendStyled(out, g[11]!!.value, Typeface.ITALIC)
                g[12] != null -> appendStyled(out, g[12]!!.value, Typeface.ITALIC)
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
        out.append(value) // محتوى الكود يبقى حرفياً دوماً، بلا تحليل تعشيش
        val end = out.length
        out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.92f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(BackgroundColorSpan(COLOR_INLINE_CODE_BG), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun appendLink(out: SpannableStringBuilder, label: String, url: String) {
        val start = out.length
        appendInline(out, label)
        val end = out.length
        out.setSpan(URLSpan(url), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_LINK), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(UnderlineSpan(), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * "ستيكر Rin": بادج/شارة ملوَّنة حقيقية الشكل عبر `[[نص]]` أو `[[نص|لون]]`. الألوان المتاحة:
     * accent (افتراضي)، green، gold، danger، info، like — راجع [STICKER_VARIANTS]. اسم لون غير
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

    /** وسم صغير باسم اللغة أعلى كتلة كود، معاد استخدامه بنفس مكوّن StickerSpan لبقاء الهوية موحّدة. */
    private fun appendLanguageChip(out: SpannableStringBuilder, lang: String) {
        val start = out.length
        out.append(lang.uppercase())
        val end = out.length
        out.setSpan(StickerSpan(0x33FFFFFF, COLOR_CODE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.68f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.append("\n")
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
}

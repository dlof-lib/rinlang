package com.dlof.rinlang.store

import android.graphics.Paint
import android.graphics.Typeface
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.BackgroundColorSpan
import android.text.style.BulletSpan
import android.text.style.ForegroundColorSpan
import android.text.style.LeadingMarginSpan
import android.text.style.LineBackgroundSpan
import android.text.style.RelativeSizeSpan
import android.text.style.ReplacementSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan

/**
 * محوّل Markdown → معاينة حقيقية داخل TextView واحد، عبر بناء [SpannableStringBuilder] مباشرة
 * بدل تمرير وسوم HTML عبر android.text.Html.fromHtml (دعمها محدود وغير متّسق لكثير من هذه
 * العناصر داخل TextView عادي). يدعم: عناوين متدرّجة الحجم (#, ##, ### , ####)، **تشديد**،
 * `كود مضمَّن`، كتل كود ``` بخط أحادي المسافة وخلفية مميّزة تمتد بعرض السطر بالكامل، قوائم
 * نقطية حقيقية (- أو *) بنقطة ملوَّنة فعلية لا حرف "•" نصّي، وخط أفقي فاصل (--- أو ***) يُرسم
 * كخط حقيقي عبر عرض السطر لا كسلسلة شرطات نصّية. كل هذا دون إضافة مكتبة Markdown خارجية.
 */
object MarkdownLite {

    // ألوان مأخوذة من نفس لوحة التطبيق (colors.xml) لتبدو المعاينة جزءاً من التطبيق لا دخيلة عليه
    private const val COLOR_BULLET = 0xFF7C5CFF.toInt()          // rin_accent
    private const val COLOR_RULE = 0xFF2D2D30.toInt()            // rin_job_card_border
    private const val COLOR_CODE_TEXT = 0xFFE3E5E8.toInt()       // rin_editor_text
    private const val COLOR_CODE_BLOCK_BG = 0x26FFFFFF           // خلفية محايدة خفيفة لكتلة الكود
    private const val COLOR_INLINE_CODE_BG = 0x33FFFFFF          // أغمق قليلاً لتمييز الكود المضمَّن عن السطر

    private val boldOrCodeRegex = Regex("\\*\\*(.+?)\\*\\*|`(.+?)`")

    /** يبني معاينة Markdown حقيقية جاهزة لعرضها مباشرة عبر `textView.text = MarkdownLite.toSpannable(md)`. */
    fun toSpannable(markdown: String): CharSequence {
        val out = SpannableStringBuilder()
        var inCodeBlock = false
        val codeBuffer = StringBuilder()
        var lastWasListItem = false

        fun blockGap() {
            if (out.isNotEmpty()) out.append("\n\n")
        }

        fun flushCodeBlock() {
            if (codeBuffer.isEmpty()) return
            blockGap()
            val content = codeBuffer.toString().trimEnd('\n')
            val start = out.length
            out.append(content)
            val end = out.length
            out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(LeadingMarginSpan.Standard(18), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(codeBlockBackgroundSpan(), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            codeBuffer.clear()
            lastWasListItem = false
        }

        for (rawLine in markdown.lines()) {
            if (rawLine.trim().startsWith("```")) {
                if (inCodeBlock) { flushCodeBlock(); inCodeBlock = false } else { inCodeBlock = true }
                continue
            }
            if (inCodeBlock) { codeBuffer.append(rawLine).append('\n'); continue }

            val trimmed = rawLine.trim()
            when {
                trimmed.isEmpty() -> Unit // الأسطر الفارغة لا تفرض تباعداً بنفسها؛ العناصر التالية تتكفّل بتباعدها
                trimmed == "---" || trimmed == "***" -> {
                    blockGap()
                    val start = out.length
                    out.append("\u00A0")
                    out.setSpan(RuleSpan(), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    lastWasListItem = false
                }
                trimmed.startsWith("#### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("#### "), 1.05f); lastWasListItem = false
                }
                trimmed.startsWith("### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("### "), 1.15f); lastWasListItem = false
                }
                trimmed.startsWith("## ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("## "), 1.28f); lastWasListItem = false
                }
                trimmed.startsWith("# ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("# "), 1.45f); lastWasListItem = false
                }
                trimmed.startsWith("- ") || trimmed.startsWith("* ") -> {
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val start = out.length
                    appendInline(out, trimmed.drop(2))
                    out.setSpan(BulletSpan(22, COLOR_BULLET), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    lastWasListItem = true
                }
                else -> {
                    blockGap()
                    appendInline(out, trimmed)
                    lastWasListItem = false
                }
            }
        }
        if (inCodeBlock) flushCodeBlock()
        return out
    }

    /** يضيف نص عنوان بحجم [scale] النسبي وتشديد كامل، مع تطبيق **تشديد**/`كود` الداخلي إن وُجد. */
    private fun appendHeading(out: SpannableStringBuilder, text: String, scale: Float) {
        val start = out.length
        appendInline(out, text)
        val end = out.length
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(scale), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** يطبّق **تشديد** و`كود مضمَّن` ضمن سطر واحد، ويُلحق الباقي كنص عادي. */
    private fun appendInline(out: SpannableStringBuilder, text: String) {
        var idx = 0
        for (match in boldOrCodeRegex.findAll(text)) {
            if (match.range.first > idx) out.append(text.substring(idx, match.range.first))
            val bold = match.groups[1]
            val code = match.groups[2]
            when {
                bold != null -> {
                    val start = out.length
                    out.append(bold.value)
                    out.setSpan(StyleSpan(Typeface.BOLD), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                }
                code != null -> {
                    val start = out.length
                    out.append(code.value)
                    val end = out.length
                    out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(RelativeSizeSpan(0.92f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(BackgroundColorSpan(COLOR_INLINE_CODE_BG), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                }
            }
            idx = match.range.last + 1
        }
        if (idx < text.length) out.append(text.substring(idx))
    }

    /** خلفية تمتد بعرض السطر كاملاً خلف كل سطر من كتلة الكود، بدل خلفية بعرض النص فقط. */
    private fun codeBlockBackgroundSpan(): LineBackgroundSpan =
        LineBackgroundSpan { canvas, paint, left, right, top, _, bottom, _, _, _, _ ->
            val savedColor = paint.color
            paint.color = COLOR_CODE_BLOCK_BG
            canvas.drawRect(left.toFloat(), (top - 2).toFloat(), right.toFloat(), (bottom + 2).toFloat(), paint)
            paint.color = savedColor
        }

    /** خط أفقي فاصل حقيقي (---) يُرسم بعرض السطر كاملاً، بدل الاعتماد على سلسلة شرطات نصّية. */
    private class RuleSpan : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int = 0

        override fun draw(
            canvas: android.graphics.Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedWidth = paint.strokeWidth
            paint.color = COLOR_RULE
            paint.strokeWidth = 2f
            val middle = (top + bottom) / 2f
            canvas.drawLine(0f, middle, canvas.width.toFloat(), middle, paint)
            paint.color = savedColor
            paint.strokeWidth = savedWidth
        }
    }
}

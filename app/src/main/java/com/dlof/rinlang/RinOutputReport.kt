package com.dlof.rinlang

/** مستوى خطورة سطر مخرجات، مرتَّب تصاعدياً لأغراض التصفية. */
enum class LogLevel(val rank: Int) { DEBUG(0), INFO(1), NORMAL(2), WARNING(3), ERROR(4) }

/** يُسقط [LogKind] (التصنيف البصري) على مستوى خطورة؛ كل ما ليس info/warn/error/debug فهو NORMAL. */
fun LogKind.level(): LogLevel = when (this) {
    LogKind.DEBUG -> LogLevel.DEBUG
    LogKind.INFO -> LogLevel.INFO
    LogKind.WARNING -> LogLevel.WARNING
    LogKind.ERROR -> LogLevel.ERROR
    else -> LogLevel.NORMAL
}

/** يُبقي الأسطر التي مستواها [level] فأعلى (مثلاً WARNING يُظهر التحذيرات والأخطاء فقط). */
fun List<RinLogLine>.atLeast(level: LogLevel): List<RinLogLine> =
    filter { it.kind.level().rank >= level.rank }

/**
 * يدمج الأسطر المتطابقة المتتالية (نفس النوع والنص والإزاحة والصورة) في سطر واحد ذي [RinLogLine.repeat]
 * أكبر، بدل إغراق الكونسول بمئات النسخ من نفس الرسالة داخل حلقة.
 */
fun List<RinLogLine>.collapseRepeats(): List<RinLogLine> {
    if (size < 2) return this
    val out = ArrayList<RinLogLine>(size)
    for (line in this) {
        val last = out.lastOrNull()
        if (last != null &&
            last.container == null && line.container == null &&
            last.kind == line.kind &&
            last.text == line.text &&
            last.imageRelPath == line.imageRelPath &&
            last.indent == line.indent
        ) {
            out[out.size - 1] = last.copy(repeat = last.repeat + line.repeat)
        } else {
            out.add(line)
        }
    }
    return out
}

/** نتيجة [truncateMiddle]: الأسطر النهائية وعدد ما أُخفي منها. */
data class RinTruncated(val lines: List<RinLogLine>, val hiddenCount: Int)

/**
 * يحدّ عدد الأسطر المعروضة بإبقاء البداية والنهاية وإخفاء الوسط مع سطر INFO يوضح عدد المخفي.
 * احسب [RinOutputSummary] قبل الاقتطاع كي لا يُحتسب سطر التنبيه نفسه.
 */
fun List<RinLogLine>.truncateMiddle(maxLines: Int): RinTruncated {
    require(maxLines >= 2) { "maxLines must be >= 2" }
    if (size <= maxLines) return RinTruncated(this, 0)
    val head = maxLines / 2
    val tail = maxLines - head
    val hidden = size - head - tail
    val out = ArrayList<RinLogLine>(maxLines + 1)
    out.addAll(subList(0, head))
    out.add(RinLogLine(LogKind.INFO, "… $hidden سطراً مخفياً …"))
    out.addAll(subList(size - tail, size))
    return RinTruncated(out, hidden)
}

/** ملخص رقمي لمخرجات تشغيل واحد. السطر المكرَّر ×N يُحتسب N مرة. */
data class RinOutputSummary(
    val totalLines: Int,
    val errors: Int,
    val warnings: Int,
    val infos: Int,
    val debugs: Int,
    val images: Int,
    val countsByKind: Map<LogKind, Int>,
    val firstErrorLine: Int?,
    /** عدد الحاويات التي فُتحت أثناء التشغيل (أسطر الفتح فقط). */
    val containers: Int = 0
) {
    val hasProblems: Boolean get() = errors > 0 || warnings > 0

    /** عنوان قصير بالعربية لرأس الكونسول، مثل: «اكتمل التشغيل بنجاح — 12 سطراً». */
    fun headline(success: Boolean): String {
        val lines = arabicCount(totalLines, "سطر واحد", "سطران", "أسطر", "سطراً")
        if (success && errors == 0) {
            val inContainers = if (containers > 0) {
                " في " + arabicCount(containers, "حاوية واحدة", "حاويتين", "حاويات", "حاوية")
            } else ""
            val base = if (totalLines == 0) "اكتمل التشغيل بنجاح بلا مخرجات"
            else "اكتمل التشغيل بنجاح — $lines$inContainers"
            return if (warnings > 0) {
                base + " مع " + arabicCount(warnings, "تحذير واحد", "تحذيران", "تحذيرات", "تحذيراً")
            } else base
        }
        val errs = arabicCount(maxOf(errors, 1), "خطأ واحد", "خطآن", "أخطاء", "خطأً")
        val at = firstErrorLine?.let { " (السطر $it)" } ?: ""
        return "فشل التشغيل — $errs$at"
    }

    companion object {
        fun of(lines: List<RinLogLine>): RinOutputSummary {
            var total = 0
            var errors = 0
            var warnings = 0
            var infos = 0
            var debugs = 0
            var images = 0
            var containers = 0
            var firstError: Int? = null
            val byKind = LinkedHashMap<LogKind, Int>()
            for (line in lines) {
                val n = maxOf(line.repeat, 1)
                total += n
                byKind[line.kind] = (byKind[line.kind] ?: 0) + n
                if (line.container?.role == ContainerRole.OPEN) containers += n
                when (line.kind) {
                    LogKind.ERROR -> {
                        errors += n
                        if (firstError == null) firstError = line.errorLine
                    }
                    LogKind.WARNING -> warnings += n
                    LogKind.INFO -> infos += n
                    LogKind.DEBUG -> debugs += n
                    LogKind.PRINT_IMAGE -> images += n
                    else -> Unit
                }
            }
            return RinOutputSummary(total, errors, warnings, infos, debugs, images, byKind, firstError, containers)
        }
    }
}

private fun arabicCount(n: Int, one: String, two: String, few: String, many: String): String = when {
    n == 1 -> one
    n == 2 -> two
    n in 3..10 -> "$n $few"
    else -> "$n $many"
}

/** تصدير المخرجات كنص عادي أو Markdown للنسخ والمشاركة ومرفقات البلاغات. */
object RinConsoleExport {

    private fun tag(kind: LogKind): String = when (kind) {
        LogKind.ERROR -> "[ERROR]"
        LogKind.WARNING -> "[WARN]"
        LogKind.INFO -> "[INFO]"
        LogKind.DEBUG -> "[DEBUG]"
        LogKind.SUCCESS -> "[OK]"
        LogKind.PRINT_IMAGE, LogKind.IMAGE -> "[IMAGE]"
        else -> ""
    }

    /** نص عادي بأعمدة ثابتة: وسم المستوى، ثم الإزاحة الأصلية، ثم النص، ثم «×N» للمكرَّر. */
    fun toPlainText(lines: List<RinLogLine>): String =
        lines.joinToString("\n") { line ->
            val body = if (line.kind == LogKind.PRINT_IMAGE) (line.imageRelPath ?: line.text) else line.text
            val suffix = if (line.repeat > 1) "  ×${line.repeat}" else ""
            tag(line.kind).padEnd(8) + " ".repeat(line.indent) + body + suffix
        }

    /** Markdown: عنوان + ملخص + كتلة كود. تُطوَّل أسوار ``` تلقائياً إذا احتوى المخرج نفسه عليها. */
    fun toMarkdown(lines: List<RinLogLine>, success: Boolean, title: String = "مخرجات Rin"): String {
        val summary = RinOutputSummary.of(lines)
        val body = toPlainText(lines)
        val longestRun = Regex("`+").findAll(body).maxOfOrNull { it.value.length } ?: 0
        val fence = "`".repeat(maxOf(3, longestRun + 1))
        return buildString {
            append("### ").append(title).append('\n')
            append(summary.headline(success)).append("\n\n")
            append(fence).append('\n').append(body).append('\n').append(fence).append('\n')
        }
    }
}

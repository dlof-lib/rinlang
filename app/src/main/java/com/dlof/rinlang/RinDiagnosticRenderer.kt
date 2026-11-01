package com.dlof.rinlang

/**
 * يحوّل [RinDiagnostic] المهيكل إلى تقرير نصي احترافي على طريقة rustc/clang:
 *
 * ```
 * error[E0001]: متغير غير معرَّف 'x'
 *  --> main.rin:3:7
 *   |
 * 2 | let a = 1;
 * 3 | print x + a;
 *   |       ^ لم يُعرَّف هذا الاسم في النطاق الحالي
 * 4 |
 *   = expected: متغير معرَّف
 *   = help: عرّفه بـ let x = ...;
 * ```
 *
 * اتفاقيات الأعمدة مطابقة تماماً لـ `rin::diag::SourceLocation` في المحرّك: الأعمدة تبدأ من 1،
 * والنهاية حصرية، والمدى متعدد الأسطر يُرسَم له سهم واحد عند نقطة البداية.
 *
 * كود Kotlin صرف بلا اعتماد على Android، فيُختبر على JVM مباشرة.
 */
object RinDiagnosticRenderer {

    private const val TAB_WIDTH = 4

    /** سطر واحد مناسب لشريط الحالة أو السجلات: `main.rin:3:7: error[E0001]: message`. */
    fun renderShort(d: RinDiagnostic): String {
        val location = if (d.line > 0) "${d.file}:${d.line}:${maxOf(d.column, 1)}: " else ""
        return location + headline(d)
    }

    /**
     * التقرير الكامل. [source] اختياري: إن مُرِّر وكان [RinDiagnostic.line] ضمن نطاقه يُعرض مقطع
     * المصدر مع الأسهم، وإلا يُكتفى بالرأس والموقع والملاحظات. [contextLines] عدد الأسطر المعروضة
     * قبل وبعد سطر الخطأ.
     */
    fun render(d: RinDiagnostic, source: String?, contextLines: Int = 1): String {
        val sb = StringBuilder(renderSnippet(d, source, contextLines))
        sb.append('\n')
        val pad = if (d.line > 0) " ".repeat(gutterWidth(d, source, contextLines)) else ""
        appendField(sb, pad, "reason", d.reason)
        appendField(sb, pad, "expected", d.expected)
        appendField(sb, pad, "found", d.found)
        d.notes.forEach { appendField(sb, pad, "note", it) }
        d.hints.forEach { appendField(sb, pad, "help", it) }
        d.suggestions.forEach { appendField(sb, pad, "suggestion", it) }
        d.causedBy.forEach { appendField(sb, pad, "caused by", it) }
        return sb.toString().trimEnd()
    }

    /**
     * الرأس + الموقع + مقطع المصدر مع الأسهم فقط، بدون الحقول النصية (reason/help/...). يُستخدم حين
     * تعرض الواجهة تلك الحقول بنفسها بتسميات مترجمة (مثل نافذة التفاصيل).
     */
    fun renderSnippet(d: RinDiagnostic, source: String?, contextLines: Int = 1): String {
        val ctx = contextLines.coerceAtLeast(0)
        val srcLines: List<String> = if (source == null) emptyList() else source.lines()
        val hasSnippet = source != null && d.line >= 1 && d.line <= srcLines.size

        val firstShown = if (hasSnippet) maxOf(1, d.line - ctx) else d.line
        val lastShown = if (hasSnippet) minOf(srcLines.size, d.line + ctx) else d.line
        val width = gutterWidth(d, source, contextLines)
        val gutter = " ".repeat(width)

        val sb = StringBuilder()
        sb.append(headline(d)).append('\n')

        if (d.line > 0) {
            sb.append(gutter).append("--> ")
                .append(d.file).append(':').append(d.line).append(':').append(maxOf(d.column, 1))
                .append('\n')
        }

        if (hasSnippet) {
            sb.append(gutter).append(" |\n")
            for (n in firstShown..lastShown) {
                val text = expandTabs(srcLines[n - 1])
                sb.append(n.toString().padStart(width)).append(" | ").append(text.trimEnd()).append('\n')
                if (n == d.line) {
                    sb.append(gutter).append(" | ").append(caretLine(d, srcLines[n - 1])).append('\n')
                }
            }
        }

        return sb.toString().trimEnd()
    }

    /**
     * المصدر الصالح لمقطع التشخيص: البرنامج الرئيسي يُمرَّر للمحرّك باسم `<input>`؛ أما أخطاء الملفات
     * المستوردة فأسماؤها مسارات حقيقية، وعرض سطرها من المصدر الرئيسي سيُظهر كوداً خاطئاً — فنُرجع null.
     */
    fun snippetSource(d: RinDiagnostic, mainSource: String): String? =
        if (d.file == "<input>") mainSource else null

    // ---- internals -------------------------------------------------------------------------

    private fun gutterWidth(d: RinDiagnostic, source: String?, contextLines: Int): Int {
        val ctx = contextLines.coerceAtLeast(0)
        val srcLines: List<String> = if (source == null) emptyList() else source.lines()
        val has = source != null && d.line >= 1 && d.line <= srcLines.size
        val last = if (has) minOf(srcLines.size, d.line + ctx) else d.line
        return maxOf(last, 1).toString().length
    }

    private fun headline(d: RinDiagnostic): String {
        val code = if (d.code.isNotEmpty()) "[${d.code}]" else ""
        return "${d.severity}$code: ${d.message}"
    }

    private fun appendField(sb: StringBuilder, pad: String, label: String, value: String?) {
        if (value.isNullOrBlank()) return
        sb.append(pad).append(" = ").append(label).append(": ").append(value.trim()).append('\n')
    }

    /** يستبدل كل Tab بمسافات ثابتة كي يتطابق موضع السهم مع النص المعروض. */
    private fun expandTabs(line: String): String =
        if (line.indexOf('\t') < 0) line else line.replace("\t", " ".repeat(TAB_WIDTH))

    /** عرض الجزء الذي يسبق العمود [col] (1-indexed) بعد توسيع الـ Tab. */
    private fun displayOffset(rawLine: String, col: Int): Int {
        val prefix = rawLine.take((col - 1).coerceAtLeast(0))
        return expandTabs(prefix).length
    }

    private fun caretLine(d: RinDiagnostic, rawLine: String): String {
        val col = maxOf(d.column, 1)
        val offset = displayOffset(rawLine, col)
        val sameLine = d.endLine == d.line
        val rawWidth = if (sameLine) maxOf(1, d.endColumn - d.column) else 1
        // لا يتجاوز السهم نهاية السطر المعروض إلا بحرف واحد (حالة خطأ "نهاية السطر").
        val remaining = expandTabs(rawLine).length - offset
        val carets = minOf(rawWidth, maxOf(1, remaining))
        return " ".repeat(offset) + "^".repeat(carets)
    }
}

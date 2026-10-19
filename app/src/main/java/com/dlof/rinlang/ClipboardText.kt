package com.dlof.rinlang

/**
 * منطق نصّي نقيّ (بلا أي اعتماد على Android) لعمليات النسخ/القص/اللصق في المحرر.
 * مفصول عمدًا عن [RinCodeEditorView] ليُختبَر بمعزل (انظر tests/editor/clipboard).
 */
object ClipboardText {

    /** أحرف تُحذَف عند اللصق: BOM، المسافات صفرية العرض، NUL — تلصق غالبًا من المتصفحات/PDF وتُفسد الكود بصمت. */
    private val INVISIBLE = Regex("[\\uFEFF\\u200B\\u200C\\u200D\\u2060\\u0000]")

    /**
     * يوحّد النص الملصوق: نهايات الأسطر إلى `\n`، حذف الأحرف الخفية، واستبدال التبويبات في *بداية*
     * كل سطر بمسافات ([tabSize]) — التبويبات داخل السطر تُترك كما هي (قد تكون جزءًا من نص حرفي).
     * مسافات NBSP تتحوّل إلى مسافة عادية (تُلصق من صفحات الويب وتكسر المحلل).
     */
    fun normalize(raw: String, tabSize: Int): String {
        if (raw.isEmpty()) return raw
        var s = raw
        if (s.indexOf('\r') >= 0) s = s.replace("\r\n", "\n").replace('\r', '\n')
        s = INVISIBLE.replace(s, "")
        if (s.indexOf(' ') >= 0) s = s.replace(' ', ' ')
        if (s.indexOf('\t') >= 0) {
            val unit = " ".repeat(tabSize.coerceIn(1, 16))
            s = s.split('\n').joinToString("\n") { line ->
                var i = 0
                while (i < line.length && (line[i] == '\t' || line[i] == ' ')) i++
                if (i == 0 || line.substring(0, i).indexOf('\t') < 0) line
                else line.substring(0, i).replace("\t", unit) + line.substring(i)
            }
        }
        return s
    }

    fun leadingWhitespace(line: String): String {
        var i = 0
        while (i < line.length && (line[i] == ' ' || line[i] == '\t')) i++
        return line.substring(0, i)
    }

    private fun indentWidth(line: String): Int = leadingWhitespace(line).length

    /** عدد الأسطر في [text] (نص فارغ = 0؛ `"a\n"` = 1 لأن السطر الأخير الفارغ بعد `\n` لا يُعدّ). */
    fun lineCount(text: String): Int {
        if (text.isEmpty()) return 0
        val n = text.count { it == '\n' } + 1
        return if (text.endsWith('\n')) n - 1 else n
    }

    /**
     * إعادة إزاحة نص متعدد الأسطر عند لصقه، بحيث يلتحم مع إزاحة السطر الهدف (يتصرّف كـ VS Code/IntelliJ):
     *  - [before] = ما قبل المؤشر في السطر الهدف، [baseIndent] = إزاحة السطر الهدف.
     *  - وضع الكتلة (قبل المؤشر مسافات فقط): كل الأسطر تُزال منها الإزاحة المشتركة الدنيا ثم تُضاف [baseIndent]
     *    (السطر الأول لا يُضاف له شيء إن كانت [before] غير فارغة لأنها تحمل الإزاحة أصلًا).
     *  - وضع منتصف السطر: السطر الأول كما هو؛ الباقي تُزال إزاحته المشتركة الدنيا ثم تُضاف [baseIndent].
     *  الأسطر الفارغة تبقى فارغة (بلا مسافات زائدة). نص بسطر واحد لا يُمسّ.
     */
    fun reindentForPaste(text: String, before: String, baseIndent: String): String {
        if (text.indexOf('\n') < 0) return text
        val lines = text.split('\n')
        val blockMode = before.isBlank()
        val firstHasIndent = lines[0].isNotBlank() && indentWidth(lines[0]) > 0
        // الإزاحة الدنيا: في وضع الكتلة أو إن حمل السطر الأول إزاحته نفسها نحسبها على كل الأسطر؛ وإلا على 2..n.
        val candidates = lines.indices
            .filter { (it > 0 || blockMode || firstHasIndent) && lines[it].isNotBlank() }
            .map { indentWidth(lines[it]) }
        val minIndent = candidates.minOrNull() ?: 0
        val sb = StringBuilder(text.length + lines.size * baseIndent.length)
        for ((i, line) in lines.withIndex()) {
            if (i > 0) sb.append('\n')
            if (line.isBlank()) continue
            val ws = indentWidth(line)
            val strip = if (i == 0 && !(blockMode || firstHasIndent)) 0 else minOf(minIndent, ws)
            val body = line.substring(strip)
            when {
                i == 0 && before.isNotEmpty() -> sb.append(body)          // قبل المؤشر مسافات: تحمل الإزاحة
                i == 0 && !blockMode -> sb.append(body)                    // منتصف سطر
                else -> sb.append(baseIndent).append(body)
            }
        }
        return sb.toString()
    }

    /** نص لصق «سطر كامل» (نُسخ بلا تحديد): يُزال منه الشائع من الإزاحة وتُركَّب عليه [baseIndent]، وينتهي بـ `\n`. */
    fun reindentLines(text: String, baseIndent: String): String {
        val body = text.removeSuffix("\n")
        val lines = body.split('\n')
        val minIndent = lines.filter { it.isNotBlank() }.minOfOrNull { indentWidth(it) } ?: 0
        return lines.joinToString("\n", postfix = "\n") { l ->
            if (l.isBlank()) "" else baseIndent + l.substring(minOf(minIndent, indentWidth(l)))
        }
    }

    /** يحيط [text] بسياج Markdown باللغة [lang]؛ يزيد طول السياج إن احتوى النص على ``` . */
    fun toMarkdown(text: String, lang: String): String {
        var fence = "```"
        while (text.contains(fence)) fence += "`"
        return fence + lang + "\n" + text.trimEnd('\n') + "\n" + fence
    }

    /** معاينة سطر واحد قصيرة لعنصر في سجل الحافظة: أول سطر غير فارغ بلا إزاحة، مقتطع بـ … */
    fun preview(text: String, maxChars: Int = 80): String {
        val first = text.lineSequence().firstOrNull { it.isNotBlank() }?.trim() ?: ""
        val oneLine = first.replace('\t', ' ')
        return if (oneLine.length > maxChars) oneLine.substring(0, maxChars - 1) + "…" else oneLine
    }
}

/**
 * سجل حافظة داخلي (في الذاكرة فقط — لا يُحفظ على القرص عمدًا لأن الشيفرة المنسوخة قد تحوي أسرارًا).
 * الأحدث أولًا، بلا تكرار (النسخ المتكرر يرفع العنصر للأعلى)، بحدّ أقصى [MAX] عنصرًا.
 */
object ClipboardHistory {
    data class Entry(val text: String, val wholeLines: Boolean)

    const val MAX = 25
    /** نص أطول من هذا لا يدخل السجل (يبقى في حافظة النظام فقط) لتفادي استهلاك الذاكرة. */
    const val MAX_ENTRY_CHARS = 200_000

    private val items = ArrayList<Entry>()

    @Synchronized
    fun add(text: String, wholeLines: Boolean = false) {
        if (text.isBlank() || text.length > MAX_ENTRY_CHARS) return
        val existing = items.indexOfFirst { it.text == text }
        // إن كان النص نفسه موجودًا بوسم «أسطر كاملة» نحتفظ بالوسم الأقوى (النسخ الداخلي أدقّ من مراقب النظام).
        val flag = wholeLines || (existing >= 0 && items[existing].wholeLines)
        if (existing >= 0) items.removeAt(existing)
        items.add(0, Entry(text, flag))
        while (items.size > MAX) items.removeAt(items.size - 1)
    }

    @Synchronized fun snapshot(): List<Entry> = ArrayList(items)
    @Synchronized fun remove(entry: Entry) { items.remove(entry) }
    @Synchronized fun clear() { items.clear() }
}

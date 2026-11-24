package com.dlof.rinlang

/**
 * عمليات تحرير Markdown فوق نص المستند كاملاً (إزاحات مسطّحة، الأسطر مفصولة بـ `\n`) — منطق خالص بلا
 * اعتماد على أندرويد فيُختبر مباشرة. كل دالة تعيد [SmartEdit.Edit] أو null إن لم يكن هناك ما يُفعل؛
 * والمحرر يطبّقها باستبدال واحد قابل للتراجع (انظر RinCodeEditorView.markdownEdit).
 *
 * الميزات: تبديل التنسيق المضمَّن (عريض/مائل/مشطوب/كود) مع فكّه عند التكرار، العناوين H1–H6،
 * الاقتباس والقوائم النقطية والمرقّمة وقوائم المهام (تبديل/تحويل بين الأنواع)، قلب مربّع المهمة،
 * كتل الكود المسوَّرة، الروابط والصور، الجداول، الخط الفاصل، ومتابعة القائمة تلقائياً عند Enter.
 */
object MarkdownEdit {

    // ------------------------------------------------------------------ أدوات الأسطر

    private fun lineStart(doc: String, pos: Int): Int {
        val p = pos.coerceIn(0, doc.length)
        return doc.lastIndexOf('\n', p - 1) + 1
    }

    private fun lineEnd(doc: String, pos: Int): Int {
        val p = pos.coerceIn(0, doc.length)
        val i = doc.indexOf('\n', p)
        return if (i < 0) doc.length else i
    }

    /** نطاق الأسطر المتأثّرة بالتحديد [s, e]: من بداية أول سطر إلى نهاية آخر سطر (بلا `\n` الأخير). */
    private fun lineBlock(doc: String, s: Int, e: Int): Pair<Int, Int> {
        val lo = minOf(s, e); var hi = maxOf(s, e)
        // تحديد ينتهي عند بداية سطر (بعد \n مباشرة) لا يشمل ذلك السطر.
        if (hi > lo && hi > 0 && doc[hi - 1] == '\n') hi--
        return lineStart(doc, lo) to lineEnd(doc, hi)
    }

    private fun isLetterOrDigitAt(doc: String, i: Int) = i in doc.indices && doc[i].isLetterOrDigit()

    /** نطاق الكلمة (حروف/أرقام فقط، بلا `_`) حول [pos] أو الملاصق له؛ null إن لم توجد. */
    private fun wordAt(doc: String, pos: Int): IntRange? {
        val p = pos.coerceIn(0, doc.length)
        var s = p; var e = p
        while (isLetterOrDigitAt(doc, s - 1)) s--
        while (isLetterOrDigitAt(doc, e)) e++
        return if (e > s) s until e else null
    }

    // ------------------------------------------------------------------ تنسيق مضمَّن

    /**
     * يبدّل علامة التنسيق [mark] حول التحديد (أو الكلمة تحت المؤشر): يلفّه بها، أو يفكّها إن كانت موجودة
     * أصلاً (سواء كانت خارج التحديد أو داخله). بلا تحديد ولا كلمة يُدرج [mark]+[placeholder]+[mark]
     * ويحدّد النص البديل ليُستبدل بالكتابة مباشرة.
     */
    fun toggleInline(doc: String, selStart: Int, selEnd: Int, mark: String, placeholder: String): SmartEdit.Edit {
        var s = minOf(selStart, selEnd).coerceIn(0, doc.length)
        var e = maxOf(selStart, selEnd).coerceIn(0, doc.length)
        if (s == e) {
            val w = wordAt(doc, s)
            if (w == null) {
                val text = mark + placeholder + mark
                return SmartEdit.Edit(s, s, text, s + mark.length, s + mark.length + placeholder.length)
            }
            s = w.first; e = w.last + 1
        }
        val n = mark.length
        val sel = doc.substring(s, e)
        // علامات خارج التحديد: **كلمة**
        if (s >= n && e + n <= doc.length && doc.regionMatches(s - n, mark, 0, n) && doc.regionMatches(e, mark, 0, n) &&
            !isExtendedMark(doc, s - n, e + n, mark)
        ) {
            return SmartEdit.Edit(s - n, e + n, sel, s - n, s - n + sel.length)
        }
        // علامات داخل التحديد: التحديد نفسه يشملها.
        if (sel.length >= 2 * n && sel.startsWith(mark) && sel.endsWith(mark)) {
            val inner = sel.substring(n, sel.length - n)
            return SmartEdit.Edit(s, e, inner, s, s + inner.length)
        }
        return SmartEdit.Edit(s, e, mark + sel + mark, s + n, s + n + sel.length)
    }

    /** يمنع اعتبار `*` الواحدة محيطة بكلمة داخل `**…**` (علامة أطول)، وكذلك `~`/`_` المضاعفة. */
    private fun isExtendedMark(doc: String, openAt: Int, closeEnd: Int, mark: String): Boolean {
        if (mark.length != 1) return false
        val c = mark[0]
        val before = doc.getOrNull(openAt - 1)
        val after = doc.getOrNull(closeEnd)
        return before == c && after == c
    }

    // ------------------------------------------------------------------ العناوين

    private val headingRe = Regex("^(\\s*)#{1,6}[ \\t]+")

    /** يجعل الأسطر المحدّدة عناوين بمستوى [level]؛ إن كانت كلها بهذا المستوى أصلاً يزيل العلامة. */
    fun heading(doc: String, selStart: Int, selEnd: Int, level: Int): SmartEdit.Edit? {
        val lvl = level.coerceIn(1, 6)
        val marker = "#".repeat(lvl) + " "
        return transformLines(doc, selStart, selEnd) { lines ->
            val allSame = lines.all { l ->
                val m = headingRe.find(l)
                m != null && m.value.trim().length == lvl
            }
            lines.map { l ->
                val m = headingRe.find(l)
                val indent = m?.groupValues?.get(1) ?: ""
                val body = if (m != null) l.substring(m.value.length) else l.trimStart()
                if (allSame) indent + body else marker + body
            }
        }
    }

    // ------------------------------------------------------------------ اقتباس وقوائم

    enum class LineKind { QUOTE, BULLET, NUMBER, TASK }

    private val taskRe = Regex("^(\\s*)[-*+][ \\t]+\\[[ xX]\\][ \\t]+")
    private val bulletRe = Regex("^(\\s*)[-*+][ \\t]+(?!\\[[ xX]\\])")
    private val numberRe = Regex("^(\\s*)\\d+[.)][ \\t]+")
    private val quoteRe = Regex("^(\\s*)>[ \\t]?")
    /** أي بادئة سطر قابلة للاستبدال (ترتيب البدائل مهم: المهمة قبل النقطة). */
    private val anyPrefixRe = Regex("^(\\s*)(?:>[ \\t]?|[-*+][ \\t]+\\[[ xX]\\][ \\t]+|[-*+][ \\t]+|\\d+[.)][ \\t]+)")

    private fun hasKind(line: String, kind: LineKind) = when (kind) {
        LineKind.QUOTE -> quoteRe.containsMatchIn(line)
        LineKind.BULLET -> bulletRe.containsMatchIn(line)
        LineKind.NUMBER -> numberRe.containsMatchIn(line)
        LineKind.TASK -> taskRe.containsMatchIn(line)
    }

    private fun kindRegex(kind: LineKind) = when (kind) {
        LineKind.QUOTE -> quoteRe
        LineKind.BULLET -> bulletRe
        LineKind.NUMBER -> numberRe
        LineKind.TASK -> taskRe
    }

    /** يبدّل نوع السطر: يزيله إن كانت كل الأسطر غير الفارغة بهذا النوع، وإلا يحوّلها إليه (مع ترقيم متسلسل). */
    fun toggleLineKind(doc: String, selStart: Int, selEnd: Int, kind: LineKind): SmartEdit.Edit? =
        transformLines(doc, selStart, selEnd) { lines ->
            val targets = lines.filter { it.isNotBlank() }
            val remove = targets.isNotEmpty() && targets.all { hasKind(it, kind) }
            var counter = 0
            lines.map { l ->
                if (l.isBlank() && lines.size > 1) return@map l
                if (remove) {
                    val m = kindRegex(kind).find(l)
                    if (m != null) m.groupValues[1] + l.substring(m.value.length) else l
                } else {
                    val m = anyPrefixRe.find(l)
                    val indent = m?.groupValues?.get(1) ?: (Regex("^\\s*").find(l)?.value ?: "")
                    val body = if (m != null) l.substring(m.value.length) else l.substring(indent.length)
                    counter++
                    val prefix = when (kind) {
                        LineKind.QUOTE -> "> "
                        LineKind.BULLET -> "- "
                        LineKind.NUMBER -> "$counter. "
                        LineKind.TASK -> "- [ ] "
                    }
                    indent + prefix + body
                }
            }
        }

    /** يقلب مربّع المهمة `[ ]` ↔ `[x]` في السطر الذي فيه [pos]؛ null إن لم يكن سطر مهمة. */
    fun toggleCheckbox(doc: String, pos: Int): SmartEdit.Edit? {
        val ls = lineStart(doc, pos); val le = lineEnd(doc, pos)
        val line = doc.substring(ls, le)
        val m = Regex("^(\\s*[-*+][ \\t]+\\[)([ xX])(\\])").find(line) ?: return null
        val boxAt = ls + m.groupValues[1].length
        val now = doc[boxAt]
        val next = if (now == ' ') "x" else " "
        val caret = pos.coerceIn(ls, le)
        return SmartEdit.Edit(boxAt, boxAt + 1, next, caret, caret)
    }

    // ------------------------------------------------------------------ كتل وروابط وجداول

    /** يلفّ الأسطر المحدّدة بسياج ```؛ بلا تحديد يُدرج سياجاً فارغاً ويضع المؤشر داخله. */
    fun codeBlock(doc: String, selStart: Int, selEnd: Int): SmartEdit.Edit {
        val lo = minOf(selStart, selEnd); val hi = maxOf(selStart, selEnd)
        if (lo == hi) {
            val ls = lineStart(doc, lo); val le = lineEnd(doc, lo)
            return if (doc.substring(ls, le).isBlank()) {
                SmartEdit.Edit(ls, le, "```\n\n```", ls + 4, ls + 4)
            } else {
                SmartEdit.Edit(le, le, "\n```\n\n```", le + 5, le + 5)
            }
        }
        val (bs, be) = lineBlock(doc, lo, hi)
        val body = doc.substring(bs, be)
        val text = "```\n$body\n```"
        return SmartEdit.Edit(bs, be, text, bs + 4, bs + 4 + body.length)
    }

    /** يحوّل التحديد (أو الكلمة) إلى رابط `[نص](url)` أو صورة `![alt](url)`؛ إن كان التحديد عنوان URL يصبح هدف الرابط. */
    fun link(doc: String, selStart: Int, selEnd: Int, image: Boolean, textPlaceholder: String, urlPlaceholder: String): SmartEdit.Edit {
        var s = minOf(selStart, selEnd).coerceIn(0, doc.length)
        var e = maxOf(selStart, selEnd).coerceIn(0, doc.length)
        if (s == e) { wordAt(doc, s)?.let { s = it.first; e = it.last + 1 } }
        val sel = doc.substring(s, e)
        val bang = if (image) "!" else ""
        return if (sel.startsWith("http://") || sel.startsWith("https://") || sel.startsWith("www.")) {
            // [نص](URL): نحدّد النص البديل ليكتبه المستخدم.
            val text = "$bang[$textPlaceholder]($sel)"
            SmartEdit.Edit(s, e, text, s + bang.length + 1, s + bang.length + 1 + textPlaceholder.length)
        } else {
            val label = if (sel.isEmpty()) textPlaceholder else sel
            val text = "$bang[$label]($urlPlaceholder)"
            val urlStart = s + bang.length + 1 + label.length + 2
            SmartEdit.Edit(s, e, text, urlStart, urlStart + urlPlaceholder.length)
        }
    }

    /** يُدرج [block] في سطر خاص به: يستبدل السطر الحالي إن كان فارغاً، وإلا يضعه بعده بسطر فاصل. [selOffset]/[selLen] لتحديد جزء منه. */
    private fun insertBlock(doc: String, pos: Int, block: String, selOffset: Int, selLen: Int): SmartEdit.Edit {
        val ls = lineStart(doc, pos); val le = lineEnd(doc, pos)
        return if (doc.substring(ls, le).isBlank()) {
            SmartEdit.Edit(ls, le, block, ls + selOffset, ls + selOffset + selLen)
        } else {
            val text = "\n\n$block"
            SmartEdit.Edit(le, le, text, le + 2 + selOffset, le + 2 + selOffset + selLen)
        }
    }

    /** يُدرج جدولاً بسيطاً ([cols] أعمدة، صفّ بيانات واحد) ويحدّد أول عنوان عمود. */
    fun table(doc: String, pos: Int, cols: Int = 3, headerLabel: String = "col"): SmartEdit.Edit {
        val n = cols.coerceIn(1, 8)
        val heads = (1..n).map { "$headerLabel$it" }
        val header = "| " + heads.joinToString(" | ") + " |"
        val sep = "| " + List(n) { "---" }.joinToString(" | ") + " |"
        val row = "| " + List(n) { "  " }.joinToString(" | ") + " |"
        val block = "$header\n$sep\n$row\n"
        return insertBlock(doc, pos, block, 2, heads[0].length)
    }

    fun horizontalRule(doc: String, pos: Int): SmartEdit.Edit {
        val ls = lineStart(doc, pos); val le = lineEnd(doc, pos)
        val blank = doc.substring(ls, le).isBlank()
        val edit = insertBlock(doc, pos, "---\n", 0, 0)
        // المؤشر بعد الخط الفاصل (على السطر التالي) لمتابعة الكتابة.
        val caret = if (blank) ls + 4 else le + 2 + 4
        return SmartEdit.Edit(edit.start, edit.end, edit.text, caret, caret)
    }

    // ------------------------------------------------------------------ Enter: متابعة القوائم

    private val continueRe = Regex("^(\\s*)((?:[-*+]|\\d+[.)])[ \\t]+(?:\\[[ xX]\\][ \\t]+)?|>[ \\t]?)(.*)$")

    /** هل الموضع [pos] داخل سياج كود ``` مفتوح؟ (عدّ أسطر السياج قبل السطر الحالي) */
    private fun insideFence(doc: String, pos: Int): Boolean {
        var count = 0
        var i = 0
        val limit = lineStart(doc, pos)
        while (i < limit) {
            val le = lineEnd(doc, i).coerceAtMost(limit)
            if (doc.substring(i, le).trimStart().let { it.startsWith("```") || it.startsWith("~~~") }) count++
            i = le + 1
        }
        return count % 2 == 1
    }

    /**
     * سلوك Enter في Markdown عند نهاية سطر قائمة/اقتباس: يبدأ عنصراً جديداً بنفس العلامة (الأرقام تزداد،
     * ومربّع المهمة يعود فارغاً)، وإن كان العنصر الحالي فارغاً يُنهي القائمة بمسح علامته. null = سلوك Enter العادي.
     */
    fun continueList(doc: String, pos: Int): SmartEdit.Edit? {
        if (pos < 0 || pos > doc.length) return null
        val ls = lineStart(doc, pos); val le = lineEnd(doc, pos)
        if (pos != le) return null
        if (insideFence(doc, pos)) return null
        val line = doc.substring(ls, le)
        if (line.trimStart().startsWith("|")) {
            // صف جدول: Enter يبدأ صفاً جديداً بنفس عدد الأعمدة، وعلى صف فارغ كلياً يُنهي الجدول.
            val cells = splitRow(line)
            if (cells.all { it.isEmpty() }) return SmartEdit.Edit(ls, le, "", ls, ls)
            val indent = line.takeWhile { it == ' ' || it == '\t' }
            val text = "\n" + indent + "|" + "   |".repeat(cells.size)
            val caret = pos + 1 + indent.length + 2
            return SmartEdit.Edit(pos, pos, text, caret, caret)
        }
        val m = continueRe.find(line) ?: return null
        val indent = m.groupValues[1]
        val marker = m.groupValues[2]
        val content = m.groupValues[3]
        if (content.isBlank()) {
            // عنصر فارغ: أنهِ القائمة.
            return SmartEdit.Edit(ls, le, "", ls, ls)
        }
        val next = when {
            marker.startsWith(">") -> "> "
            marker.trimStart().firstOrNull()?.isDigit() == true -> {
                val num = marker.takeWhile { it.isDigit() }.toIntOrNull() ?: 0
                val delim = marker.firstOrNull { it == '.' || it == ')' } ?: '.'
                "${num + 1}$delim " + if (marker.contains('[')) "[ ] " else ""
            }
            else -> {
                val bullet = marker.first()
                "$bullet " + if (marker.contains('[')) "[ ] " else ""
            }
        }
        val text = "\n$indent$next"
        val caret = pos + text.length
        return SmartEdit.Edit(pos, pos, text, caret, caret)
    }

    // ------------------------------------------------------------------ إزاحة القوائم

    /** يزيح الأسطر المحدّدة مستوى واحداً (مسافتان) للداخل [dir] > 0 أو للخارج [dir] < 0. */
    fun shiftIndent(doc: String, selStart: Int, selEnd: Int, dir: Int): SmartEdit.Edit? =
        transformLines(doc, selStart, selEnd) { lines ->
            lines.map { l ->
                when {
                    l.isBlank() -> l
                    dir > 0 -> "  $l"
                    l.startsWith("\t") -> l.substring(1)
                    l.startsWith("  ") -> l.substring(2)
                    l.startsWith(" ") -> l.substring(1)
                    else -> l
                }
            }
        }

    /** Tab على سطر عنصر قائمة (نقطي/مرقّم/مهمة) يزيحه مستوى للداخل؛ null لأي سطر آخر (يبقى سلوك Tab العادي). */
    fun indentListItem(doc: String, pos: Int): SmartEdit.Edit? {
        val line = doc.substring(lineStart(doc, pos), lineEnd(doc, pos))
        if (!(taskRe.containsMatchIn(line) || bulletRe.containsMatchIn(line) || numberRe.containsMatchIn(line))) return null
        return shiftIndent(doc, pos, pos, 1)
    }

    // ------------------------------------------------------------------ الجداول

    private val separatorCellRe = Regex("^:?-+:?$")

    private fun isTableLine(line: String) = line.trimStart().startsWith("|")

    /** يقسّم صف جدول إلى خلاياه (يتجاهل `|` الحدّية و`\|` المهرَّبة). */
    private fun splitRow(line: String): List<String> {
        var t = line.trim()
        if (t.startsWith("|")) t = t.substring(1)
        if (t.endsWith("|") && !t.endsWith("\\|")) t = t.substring(0, t.length - 1)
        val cells = ArrayList<String>()
        val sb = StringBuilder()
        var i = 0
        while (i < t.length) {
            val c = t[i]
            if (c == '\\' && i + 1 < t.length && t[i + 1] == '|') { sb.append("\\|"); i += 2; continue }
            if (c == '|') { cells.add(sb.toString().trim()); sb.setLength(0) } else sb.append(c)
            i++
        }
        cells.add(sb.toString().trim())
        return cells
    }

    /**
     * ينسّق الجدول الذي فيه [pos]: يوحّد عرض الأعمدة ويحاذي الخلايا حسب صف الفاصل (`:--` يسار، `:-:` وسط، `--:` يمين).
     * يعيد null إن لم يكن المؤشر داخل جدول. المؤشر يُوضع في أول خلية من نفس الصف.
     */
    fun formatTable(doc: String, pos: Int): SmartEdit.Edit? {
        val curStart = lineStart(doc, pos)
        if (!isTableLine(doc.substring(curStart, lineEnd(doc, pos)))) return null
        var start = curStart
        while (start > 0) {
            val ps = lineStart(doc, start - 1)
            if (isTableLine(doc.substring(ps, start - 1))) start = ps else break
        }
        var end = lineEnd(doc, pos)
        while (end < doc.length) {
            val ne = lineEnd(doc, end + 1)
            if (isTableLine(doc.substring(end + 1, ne))) end = ne else break
        }
        val rawLines = doc.substring(start, end).split('\n')
        val rows = rawLines.map { splitRow(it) }
        val indent = rawLines[0].takeWhile { it == ' ' || it == '\t' }
        val sepIdx = rows.indexOfFirst { r -> r.isNotEmpty() && r.all { separatorCellRe.matches(it) } }
        val cols = rows.maxOf { it.size }
        // 0 = بلا محاذاة، 1 = يسار صريح، 2 = وسط، 3 = يمين
        val aligns = IntArray(cols) { c ->
            val cell = if (sepIdx >= 0) rows[sepIdx].getOrNull(c) ?: "" else ""
            when {
                cell.startsWith(":") && cell.endsWith(":") && cell.length > 1 -> 2
                cell.endsWith(":") -> 3
                cell.startsWith(":") -> 1
                else -> 0
            }
        }
        val widths = IntArray(cols) { c ->
            var w = 3
            for ((ri, r) in rows.withIndex()) if (ri != sepIdx) w = maxOf(w, r.getOrNull(c)?.length ?: 0)
            w
        }
        val out = rows.mapIndexed { ri, r ->
            val cells = (0 until cols).map { c ->
                val w = widths[c]
                if (ri == sepIdx) {
                    when (aligns[c]) {
                        2 -> ":" + "-".repeat(w - 2) + ":"
                        3 -> "-".repeat(w - 1) + ":"
                        1 -> ":" + "-".repeat(w - 1)
                        else -> "-".repeat(w)
                    }
                } else {
                    val t = r.getOrNull(c) ?: ""
                    when (aligns[c]) {
                        3 -> t.padStart(w)
                        2 -> { val total = w - t.length; " ".repeat(total / 2) + t + " ".repeat(total - total / 2) }
                        else -> t.padEnd(w)
                    }
                }
            }
            indent + "| " + cells.joinToString(" | ") + " |"
        }
        val newText = out.joinToString("\n")
        val rowIdx = doc.substring(start, pos.coerceIn(start, doc.length)).count { it == '\n' }
        var caret = start
        for (i in 0 until rowIdx) caret += out[i].length + 1
        caret += indent.length + 2
        return SmartEdit.Edit(start, end, newText, caret, caret)
    }

    // ------------------------------------------------------------------ العناوين: فهرس/جدول محتويات/إحصاءات

    data class Heading(val line: Int, val level: Int, val text: String)

    private val atxRe = Regex("^ {0,3}(#{1,6})[ \\t]+(.*?)(?:[ \\t]+#+)?[ \\t]*$")

    /** عناوين `#` في المستند (يتجاهل ما داخل أسطر الكود المسوَّرة)؛ [Heading.line] مبنيّ على الصفر. */
    fun headings(doc: String): List<Heading> {
        val out = ArrayList<Heading>()
        var fence = false
        doc.split('\n').forEachIndexed { i, raw ->
            val l = raw.trimEnd('\r')
            val t = l.trimStart()
            if (t.startsWith("```") || t.startsWith("~~~")) { fence = !fence; return@forEachIndexed }
            if (fence) return@forEachIndexed
            val m = atxRe.find(l) ?: return@forEachIndexed
            val text = m.groupValues[2].trim()
            if (text.isNotEmpty()) out.add(Heading(i, m.groupValues[1].length, text))
        }
        return out
    }

    /** مُعرّف المرساة بأسلوب GitHub: أحرف صغيرة، حذف علامات الترقيم، والمسافات → `-`. */
    private fun slug(text: String): String {
        val sb = StringBuilder()
        for (c in text.lowercase()) {
            when {
                c.isLetterOrDigit() || c == '-' || c == '_' -> sb.append(c)
                c == ' ' -> sb.append('-')
            }
        }
        return sb.toString()
    }

    /** يُدرج جدول محتويات (روابط مراسي لكل العناوين حتى [maxLevel]) عند [pos]؛ null إن لم توجد عناوين. */
    fun insertToc(doc: String, pos: Int, maxLevel: Int = 3): SmartEdit.Edit? {
        val hs = headings(doc).filter { it.level <= maxLevel }
        if (hs.isEmpty()) return null
        val minLevel = hs.minOf { it.level }
        val seen = HashMap<String, Int>()
        val lines = hs.map { h ->
            val base = slug(h.text)
            val n = seen[base] ?: 0
            seen[base] = n + 1
            val anchor = if (n == 0) base else "$base-$n"
            "  ".repeat(h.level - minLevel) + "- [" + h.text + "](#" + anchor + ")"
        }
        return insertBlock(doc, pos, lines.joinToString("\n") + "\n", 0, 0)
    }

    data class Stats(val words: Int, val chars: Int, val charsNoSpaces: Int, val lines: Int, val headings: Int, val readingMinutes: Int)

    fun stats(doc: String): Stats {
        val words = doc.split(Regex("\\s+")).count { it.isNotEmpty() }
        val lines = if (doc.isEmpty()) 0 else doc.count { it == '\n' } + 1
        return Stats(
            words = words,
            chars = doc.length,
            charsNoSpaces = doc.count { !it.isWhitespace() },
            lines = lines,
            headings = headings(doc).size,
            readingMinutes = if (words == 0) 0 else (words + 199) / 200
        )
    }

    // ------------------------------------------------------------------ مساعد: تحويل أسطر

    /**
     * يطبّق [f] على أسطر الكتلة المتأثّرة. سطر واحد: يُزاح المؤشر بفرق الطول؛ عدة أسطر: تُحدَّد الكتلة الناتجة كلها.
     * null إن لم يتغيّر شيء.
     */
    private fun transformLines(doc: String, selStart: Int, selEnd: Int, f: (List<String>) -> List<String>): SmartEdit.Edit? {
        val (bs, be) = lineBlock(doc, selStart, selEnd)
        val oldLines = doc.substring(bs, be).split('\n')
        val newLines = f(oldLines)
        if (newLines == oldLines) return null
        val newText = newLines.joinToString("\n")
        return if (oldLines.size == 1) {
            val delta = newText.length - oldLines[0].length
            val lo = minOf(selStart, selEnd); val hi = maxOf(selStart, selEnd)
            val a = (lo + delta).coerceIn(bs, bs + newText.length)
            val b = (hi + delta).coerceIn(bs, bs + newText.length)
            SmartEdit.Edit(bs, be, newText, a, b)
        } else {
            SmartEdit.Edit(bs, be, newText, bs, bs + newText.length)
        }
    }
}

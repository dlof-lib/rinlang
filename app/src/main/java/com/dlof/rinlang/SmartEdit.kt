package com.dlof.rinlang

/**
 * عمليات تحرير ذكية فوق نص المستند كاملاً (إزاحات مسطّحة، الأسطر مفصولة بـ `\n`) — منطق خالص بلا
 * اعتماد على أندرويد فيُختبر مباشرة. كل دالة تعيد [Edit] أو null إن لم يكن هناك ما يُفعل؛ والمحرر
 * يطبّقها باستبدال واحد قابل للتراجع (انظر RinCodeEditorView.applySmartEdit).
 *
 * الميزات: تغيير حالة الأحرف وأنماط التسمية (snake/kebab/camel/Pascal)، زيادة/إنقاص الرقم تحت المؤشر،
 * قلب القيم المنطقية (true↔false)، إحاطة التحديد بأقواس/علامات اقتباس وفكّها، توسيع التحديد تدريجياً
 * (كلمة ← نص ← أقواس ← سطر ← الكل)، إدراج سطر جديد فوق/تحت بنفس الإزاحة، تدوير صيغة اللون،
 * واستبدال اللون بقيمة جديدة، وأدوات أسطر (عكس/إزالة المكرّر).
 */
object SmartEdit {

    /** استبدال [start, end) من النص القديم بـ [text]؛ [selStart, selEnd] التحديد في النص **الجديد**. */
    data class Edit(val start: Int, val end: Int, val text: String, val selStart: Int, val selEnd: Int)

    fun apply(doc: String, e: Edit): String = doc.substring(0, e.start) + e.text + doc.substring(e.end)

    enum class CaseMode { UPPER, LOWER, TITLE, SNAKE, KEBAB, CAMEL, PASCAL, CONSTANT }

    private fun isWordChar(c: Char) = c.isLetterOrDigit() || c == '_'

    /** نطاق الكلمة (حروف/أرقام/_) الذي يحيط بالموضع [pos] أو يلاصقه؛ null إن لم توجد. */
    fun wordRange(doc: String, pos: Int): IntRange? {
        val p = pos.coerceIn(0, doc.length)
        var s = p; var e = p
        while (s > 0 && isWordChar(doc[s - 1])) s--
        while (e < doc.length && isWordChar(doc[e])) e++
        return if (e > s) s until e else null
    }

    // ------------------------------------------------------------------ الحالة

    /** يطبّق [mode] على التحديد، أو على الكلمة تحت المؤشر إن لم يوجد تحديد. */
    fun changeCase(doc: String, selStart: Int, selEnd: Int, mode: CaseMode): Edit? {
        var s = minOf(selStart, selEnd); var e = maxOf(selStart, selEnd)
        if (s == e) { val w = wordRange(doc, s) ?: return null; s = w.first; e = w.last + 1 }
        val src = doc.substring(s, e)
        val out = src.split('\n').joinToString("\n") { caseLine(it, mode) }
        if (out == src) return null
        return Edit(s, e, out, s, s + out.length)
    }

    private fun caseLine(line: String, mode: CaseMode): String = when (mode) {
        CaseMode.UPPER -> line.uppercase()
        CaseMode.LOWER -> line.lowercase()
        CaseMode.TITLE -> {
            val sb = StringBuilder(line.length); var start = true
            for (c in line) {
                if (c.isLetterOrDigit()) { sb.append(if (start) c.uppercaseChar() else c.lowercaseChar()); start = false }
                else { sb.append(c); start = true }
            }
            sb.toString()
        }
        else -> {
            val words = splitWords(line)
            if (words.isEmpty()) line else when (mode) {
                CaseMode.SNAKE -> words.joinToString("_") { it.lowercase() }
                CaseMode.KEBAB -> words.joinToString("-") { it.lowercase() }
                CaseMode.CONSTANT -> words.joinToString("_") { it.uppercase() }
                CaseMode.CAMEL -> words.mapIndexed { i, w -> if (i == 0) w.lowercase() else cap(w) }.joinToString("")
                else -> words.joinToString("") { cap(it) }
            }
        }
    }

    private fun cap(w: String) = if (w.isEmpty()) w else w.substring(0, 1).uppercase() + w.substring(1).lowercase()

    /** يفصل نصاً إلى كلمات: بالفواصل غير الحرفية وبحدود camelCase (fooBar→foo,Bar؛ HTTPServer→HTTP,Server). */
    fun splitWords(s: String): List<String> {
        val out = ArrayList<String>(); val cur = StringBuilder()
        fun flush() { if (cur.isNotEmpty()) { out.add(cur.toString()); cur.setLength(0) } }
        for (i in s.indices) {
            val c = s[i]
            if (!c.isLetterOrDigit()) { flush(); continue }
            if (cur.isNotEmpty()) {
                val p = cur.last()
                val boundary = (p.isLowerCase() && c.isUpperCase()) ||
                    (p.isLetter() && c.isDigit()) || (p.isDigit() && c.isLetter()) ||
                    (p.isUpperCase() && c.isUpperCase() && i + 1 < s.length && s[i + 1].isLowerCase())
                if (boundary) flush()
            }
            cur.append(c)
        }
        flush()
        return out
    }

    // ------------------------------------------------------------------ أرقام وقيم منطقية

    /** يزيد/ينقص الرقم تحت المؤشر (أو الأقرب له في السطر) بمقدار [delta]؛ يحافظ على عدد الخانات العشرية. */
    fun stepNumber(doc: String, pos: Int, delta: Int): Edit? {
        val p = pos.coerceIn(0, doc.length)
        val ls = lineStartOf(doc, p); val le = lineEndOf(doc, p)
        // اختر أقرب رقم في السطر يحوي المؤشر أو يلاصقه، وإلا أول رقم بعده
        var i = ls; var best: IntRange? = null
        while (i < le) {
            if (doc[i].isDigit()) {
                var a = i; var b = i
                while (b < le && doc[b].isDigit()) b++
                if (b + 1 < le && doc[b] == '.' && doc[b + 1].isDigit()) { b++; while (b < le && doc[b].isDigit()) b++ }
                // لا نلمس أرقاماً ملتصقة بحرف (مثل x1 أو #ff00) — نتعامل معها كجزء من اسم
                val glued = (a > ls && (doc[a - 1].isLetter() || doc[a - 1] == '_' || doc[a - 1] == '#'))
                if (!glued) {
                    if (a > ls && doc[a - 1] == '-' && (a - 1 == ls || !isWordChar(doc[a - 2]))) a--
                    val r = a until b
                    if (p in a..b) { best = r; break }
                    if (best == null && a >= p) best = r
                }
                i = b
            } else i++
        }
        val r = best ?: return null
        val txt = doc.substring(r.first, r.last + 1)
        val dot = txt.indexOf('.')
        val out: String = if (dot < 0) (txt.toLong() + delta).toString()
        else {
            val dec = txt.length - dot - 1
            java.math.BigDecimal(txt).add(java.math.BigDecimal(delta)).setScale(dec).toPlainString()
        }
        return Edit(r.first, r.last + 1, out, r.first, r.first + out.length)
    }

    private val FLIPS = mapOf("true" to "false", "false" to "true", "yes" to "no", "no" to "yes", "on" to "off", "off" to "on",
        "and" to "or", "or" to "and", "let" to "const", "const" to "let", "min" to "max", "max" to "min", "left" to "right", "right" to "left")

    /** يقلب الكلمة تحت المؤشر إن كانت من أزواج معروفة (true↔false، on↔off…) مع حفظ شكل الحروف. */
    fun flipWord(doc: String, pos: Int): Edit? {
        val w = wordRange(doc, pos) ?: return null
        val word = doc.substring(w.first, w.last + 1)
        val to = FLIPS[word.lowercase()] ?: return null
        val out = when {
            word.all { it.isUpperCase() } -> to.uppercase()
            word[0].isUpperCase() -> cap(to)
            else -> to
        }
        return Edit(w.first, w.last + 1, out, w.first, w.first + out.length)
    }

    // ------------------------------------------------------------------ إحاطة

    private val PAIRS = mapOf('(' to ')', '[' to ']', '{' to '}', '"' to '"', '\'' to '\'', '`' to '`', '<' to '>', '«' to '»')

    fun closerFor(open: Char): Char? = PAIRS[open]

    /** يحيط التحديد بـ [open] والمُغلِق المقابل، ويُبقي النص الأصلي محدداً. null بلا تحديد أو لمحرف غير معروف. */
    fun surround(doc: String, selStart: Int, selEnd: Int, open: Char): Edit? {
        val s = minOf(selStart, selEnd); val e = maxOf(selStart, selEnd)
        if (s == e) return null
        val close = PAIRS[open] ?: return null
        val inner = doc.substring(s, e)
        return Edit(s, e, open + inner + close, s + 1, s + 1 + inner.length)
    }

    /** يفكّ زوج الإحاطة الأقرب حول التحديد/المؤشر (أقواس أو اقتباس أو عنصر ثابت داخل النص). */
    fun unsurround(doc: String, selStart: Int, selEnd: Int): Edit? {
        val s = minOf(selStart, selEnd); val e = maxOf(selStart, selEnd)
        // 1) التحديد نفسه يبدأ بالفاتح وينتهي بالمُغلِق
        if (e - s >= 2 && PAIRS[doc[s]] == doc[e - 1]) {
            val inner = doc.substring(s + 1, e - 1)
            return Edit(s, e, inner, s, s + inner.length)
        }
        // 2) الزوج المحيط
        val pair = enclosingPairs(doc, s, e).firstOrNull() ?: return null
        val inner = doc.substring(pair.first + 1, pair.second)
        return Edit(pair.first, pair.second + 1, inner, pair.first + (s - pair.first - 1).coerceAtLeast(0),
            pair.first + (e - pair.first - 1).coerceAtLeast(0))
    }

    /** أزواج الأقواس/الاقتباس المحيطة بـ [s,e) من الأقرب إلى الأبعد؛ كل عنصر (موضع الفاتح، موضع المُغلِق). */
    fun enclosingPairs(doc: String, s: Int, e: Int): List<Pair<Int, Int>> {
        val out = ArrayList<Pair<Int, Int>>()
        // نصوص على نفس السطر
        val ls = lineStartOf(doc, s); val le = lineEndOf(doc, e)
        val strings = stringSpans(doc, ls, le)
        for ((a, b) in strings) if (a < s && b >= e) out.add(a to b)
        // أقواس كامل المستند (نتجاهل ما داخل النصوص والتعليقات `//`)
        val stack = ArrayList<Int>()
        val brackets = ArrayList<Pair<Int, Int>>()
        var i = 0; val n = doc.length
        while (i < n) {
            val c = doc[i]
            when {
                c == '/' && i + 1 < n && doc[i + 1] == '/' -> { while (i < n && doc[i] != '\n') i++; continue }
                c == '"' || c == '\'' -> { val j = closingQuote(doc, i); i = if (j < 0) n else j + 1; continue }
                c == '(' || c == '[' || c == '{' -> stack.add(i)
                c == ')' || c == ']' || c == '}' -> {
                    val o = stack.removeLastOrNull()
                    if (o != null && PAIRS[doc[o]] == c) brackets.add(o to i)
                }
            }
            i++
        }
        for ((a, b) in brackets) if (a < s && b >= e) out.add(a to b)
        out.sortBy { it.second - it.first }
        return out
    }

    private fun closingQuote(doc: String, open: Int): Int {
        val q = doc[open]; var i = open + 1
        while (i < doc.length && doc[i] != '\n') {
            if (doc[i] == '\\') { i += 2; continue }
            if (doc[i] == q) return i
            i++
        }
        return -1
    }

    private fun stringSpans(doc: String, ls: Int, le: Int): List<Pair<Int, Int>> {
        val out = ArrayList<Pair<Int, Int>>(); var i = ls
        while (i < le) {
            val c = doc[i]
            if (c == '"' || c == '\'') {
                val j = closingQuote(doc, i)
                if (j in 0 until le) { out.add(i to j); i = j + 1; continue }
            }
            i++
        }
        return out
    }

    // ------------------------------------------------------------------ توسيع التحديد

    /** النطاق التالي الأكبر الذي يحوي [s,e): كلمة ← محتوى نص/أقواس ← النص بعلامتيه ← السطر ← الكل. */
    fun expandSelection(doc: String, selStart: Int, selEnd: Int): IntRange? {
        val s = minOf(selStart, selEnd); val e = maxOf(selStart, selEnd)
        val cands = ArrayList<IntRange>()
        wordRange(doc, s)?.let { if (e <= it.last + 1) cands.add(it) }
        for ((a, b) in enclosingPairs(doc, s, e)) { cands.add((a + 1) until b); cands.add(a..b) }
        val ls = lineStartOf(doc, s); val le = lineEndOf(doc, e)
        val line = doc.substring(ls, le)
        val lead = line.length - line.trimStart().length
        val trimmedEnd = line.trimEnd().length
        if (trimmedEnd > lead) cands.add((ls + lead) until (ls + trimmedEnd))
        cands.add(ls until le)
        if (doc.isNotEmpty()) cands.add(0 until doc.length)
        var best: IntRange? = null
        for (c in cands) {
            val cs = c.first; val ce = c.last + 1
            if (cs <= s && ce >= e && (cs < s || ce > e)) {
                if (best == null || (ce - cs) < (best.last + 1 - best.first)) best = c
            }
        }
        return best
    }

    // ------------------------------------------------------------------ أسطر

    private fun lineStartOf(doc: String, pos: Int): Int = if (pos <= 0) 0 else doc.lastIndexOf('\n', pos - 1) + 1
    private fun lineEndOf(doc: String, pos: Int): Int = doc.indexOf('\n', pos).let { if (it < 0) doc.length else it }

    /** سطر جديد تحت الحالي بنفس الإزاحة (+ إزاحة إضافية إن انتهى السطر بـ `{` أو `:`) والمؤشر فيه. */
    fun insertLineBelow(doc: String, pos: Int, indentUnit: String = "    "): Edit {
        val ls = lineStartOf(doc, pos); val le = lineEndOf(doc, pos)
        val line = doc.substring(ls, le)
        val indent = line.takeWhile { it == ' ' || it == '\t' }
        val extra = if (line.trimEnd().let { it.endsWith("{") || it.endsWith("(") || it.endsWith("[") }) indentUnit else ""
        val ins = "\n$indent$extra"
        return Edit(le, le, ins, le + ins.length, le + ins.length)
    }

    /** سطر جديد فوق الحالي بنفس الإزاحة والمؤشر فيه. */
    fun insertLineAbove(doc: String, pos: Int): Edit {
        val ls = lineStartOf(doc, pos); val le = lineEndOf(doc, pos)
        val indent = doc.substring(ls, le).takeWhile { it == ' ' || it == '\t' }
        val ins = "$indent\n"
        return Edit(ls, ls, ins, ls + indent.length, ls + indent.length)
    }

    enum class LineOp { REVERSE, UNIQUE, SORT_NUMERIC }

    /** أدوات على الأسطر التي يمسّها التحديد (أو السطر الحالي). */
    fun transformLines(doc: String, selStart: Int, selEnd: Int, op: LineOp): Edit? {
        val s = minOf(selStart, selEnd); val e = maxOf(selStart, selEnd)
        val ls = lineStartOf(doc, s)
        val le = lineEndOf(doc, if (e > s && e > 0 && doc[e - 1] == '\n') e - 1 else e)
        val lines = doc.substring(ls, le).split('\n')
        val out = when (op) {
            LineOp.REVERSE -> lines.reversed()
            LineOp.UNIQUE -> lines.distinct()
            LineOp.SORT_NUMERIC -> lines.sortedBy { l -> Regex("-?\\d+(\\.\\d+)?").find(l)?.value?.toDoubleOrNull() ?: Double.MAX_VALUE }
        }
        if (out == lines) return null
        val text = out.joinToString("\n")
        return Edit(ls, le, text, ls, ls + text.length)
    }

    // ------------------------------------------------------------------ ألوان

    /** يدوّر صيغة اللون الذي يلاصق المؤشر (hex → rgb → hsl → hex). */
    fun cycleColorFormat(doc: String, pos: Int): Edit? {
        val p = pos.coerceIn(0, doc.length)
        val ls = lineStartOf(doc, p); val le = lineEndOf(doc, p)
        val m = ColorLiterals.matchAt(doc.substring(ls, le), p - ls) ?: return null
        val text = ColorLiterals.format(m.argb, ColorLiterals.nextFormat(m.format), m.upper)
        val a = ls + m.start; val b = ls + m.end
        return Edit(a, b, text, a, a + text.length)
    }

    /** يستبدل اللون الذي يلاصق المؤشر بـ [newArgb] محافظاً على صيغته. */
    fun replaceColorAt(doc: String, pos: Int, newArgb: Int): Edit? {
        val p = pos.coerceIn(0, doc.length)
        val ls = lineStartOf(doc, p); val le = lineEndOf(doc, p)
        val m = ColorLiterals.matchAt(doc.substring(ls, le), p - ls) ?: return null
        val text = ColorLiterals.replacementFor(m, newArgb)
        val a = ls + m.start
        return Edit(a, ls + m.end, text, a, a + text.length)
    }
}

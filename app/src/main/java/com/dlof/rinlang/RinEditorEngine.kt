package com.dlof.rinlang

/**
 * محرك محرر أكواد كامل — بلغة Kotlin خالصة بلا أي اعتماد على C++/JNI (يستبدل بالكامل
 * app/src/main/cpp/editor/rin_editor_engine.* القديم وRinNativeEditor.kt الذي كان يُعرِّضه).
 *
 * يحتفظ بالنص كسطور UTF-16 (نفس تمثيل [String] في Kotlin مباشرة — لا حاجة إطلاقاً لأي تحويل
 * بايت/حرف كما كان الحال مع المحرك الأصلي بلغة C++)، ويوفر: إدراج/حذف، تراجع/إعادة (undo/redo)
 * حقيقي مبني على سجلّ تعديلات دقيق (وليس نسخ نص كاملة)، مسافة بادئة تلقائية + إغلاق أقواس/علامات
 * تنصيص تلقائي + إزاحة ذكية عند سطر جديد بين قوسين، أوامر مستوى-السطر (تكرار/حذف/نقل/تعليق/
 * إزاحة)، فحص توازن الأقواس، بحث عن كل التطابقات، وتلوين نحوي حقيقي عبر [RinSyntax] (مُبنيّ على
 * ماسح رموز (tokenizer) حقيقي لكل لغة، وليس Regex ساذجاً).
 *
 * كل الإحداثيات (line, col) هي: line = فهرس سطر صفري (0-based)، col = فهرس-حرف UTF-16 صفري
 * (0-based) داخل السطر — أي *نفس* ما تتوقعه كل واجهات برمجة النصوص في Kotlin/Android مباشرة
 * ([String], [android.graphics.Paint.measureText], ...) بلا أي طبقة تحويل وسيطة.
 *
 * مثيل واحد من هذا الصنف = مستند واحد مفتوح في المحرر.
 */
@Deprecated("Use RinNativeEditor: the production editor engine is C++17 and shares the real Rin lexer.")
class RinEditorEngine {

    // --- تمثيل النص -------------------------------------------------------

    private val lines: MutableList<String> = mutableListOf("")
    private var cursor = Pos(0, 0)
    private var selAnchor = Pos(0, 0)
    private var hasSel = false
    private var language: SyntaxLanguage = SyntaxLanguage.RIN

    // --- تراجع/إعادة --------------------------------------------------------

    private val undoStack = ArrayDeque<EditRecord>()
    private val redoStack = ArrayDeque<EditRecord>()

    private data class EditRecord(
        val startLine: Int, val startCol: Int,
        val origEndLine: Int, val origEndCol: Int,
        val newEndLine: Int, val newEndCol: Int,
        val removedText: String,
        val insertedText: String,
        val cursorBefore: Pos,
        val cursorAfter: Pos,
        val timestampMs: Long,
        val coalescable: Boolean
    )

    companion object {
        private const val MAX_HISTORY = 500
        private const val COALESCE_WINDOW_MS = 800L
        private val OPEN_TO_CLOSE = mapOf('(' to ')', '[' to ']', '{' to '}')
        private val CLOSE_TO_OPEN = mapOf(')' to '(', ']' to '[', '}' to '{')
        private val AUTO_PAIR_QUOTES = setOf('"', '\'')
    }

    data class Pos(val line: Int, val col: Int)
    data class Selection(val hasSelection: Boolean, val start: Pos, val end: Pos)
    data class Highlight(val line: Int, val startCol: Int, val endCol: Int, val kind: Int)
    data class FindMatch(val line: Int, val startCol: Int, val endCol: Int)

    // --- دورة حياة (متوافقة مع الواجهة القديمة؛ لا حاجة فعلية لتحرير موارد هنا) ---

    fun destroy() { /* لا موارد أصلية للتحرير في محرك Kotlin خالص */ }

    // --- لغة التلوين النحوي/التعليق --------------------------------------

    fun setLanguage(newLanguage: SyntaxLanguage) {
        language = newLanguage
        invalidateHighlightCache()
    }

    fun getLanguage(): SyntaxLanguage = language

    // --- نص كامل ------------------------------------------------------------

    fun setText(text: String) {
        lines.clear()
        lines.addAll(splitLines(text))
        cursor = Pos(0, 0)
        selAnchor = cursor
        hasSel = false
        undoStack.clear()
        redoStack.clear()
        invalidateHighlightCache()
    }

    fun getText(): String = lines.joinToString("\n")

    fun lineCount(): Int = lines.size

    fun getLine(line: Int): String = lines.getOrElse(clampLine(line)) { "" }

    private fun splitLines(text: String): List<String> {
        val normalized = text.replace("\r\n", "\n").replace("\r", "\n")
        val parts = normalized.split("\n")
        return if (parts.isEmpty()) listOf("") else parts
    }

    // --- مؤشر/تحديد -----------------------------------------------------------

    fun getCursor(): Pos = cursor

    fun hasSelection(): Boolean = hasSel

    fun getSelection(): Selection {
        if (!hasSel) return Selection(false, cursor, cursor)
        val (a, b) = normalizedSelection()
        return Selection(true, a, b)
    }

    private fun normalizedSelection(): Pair<Pos, Pos> {
        return if (comparePos(selAnchor, cursor) <= 0) selAnchor to cursor else cursor to selAnchor
    }

    private fun comparePos(a: Pos, b: Pos): Int =
        if (a.line != b.line) a.line - b.line else a.col - b.col

    fun setCursor(line: Int, col: Int, extendSelection: Boolean) {
        val p = clampPos(Pos(line, col))
        if (extendSelection) {
            if (!hasSel) selAnchor = cursor
            hasSel = true
        } else {
            hasSel = false
            selAnchor = p
        }
        cursor = p
    }

    fun setSelection(aLine: Int, aCol: Int, bLine: Int, bCol: Int) {
        selAnchor = clampPos(Pos(aLine, aCol))
        cursor = clampPos(Pos(bLine, bCol))
        hasSel = selAnchor != cursor
    }

    fun collapseSelection() {
        hasSel = false
        selAnchor = cursor
    }

    private fun clampLine(line: Int): Int = line.coerceIn(0, lines.size - 1)

    private fun clampPos(p: Pos): Pos {
        val l = clampLine(p.line)
        val c = p.col.coerceIn(0, lines[l].length)
        return Pos(l, c)
    }

    // --- تحرير أساسي --------------------------------------------------------

    /** يُدرج [text] عند المؤشر (يستبدل التحديد إن وُجد). [smart]=true يفعّل الإزاحة التلقائية بعد
     *  Enter، وإغلاق الأقواس/التنصيص التلقائي والتخطي فوقها، للحرف الواحد المُدرَج فقط. */
    fun insertText(text: String, smart: Boolean): Pos {
        if (hasSel) {
            val (a, b) = normalizedSelection()
            return applyReplaceTracked(a.line, a.col, b.line, b.col, text, coalescable = false)
        }
        if (smart && text.length == 1) {
            return insertSingleSmartChar(text[0])
        }
        return applyReplaceTracked(cursor.line, cursor.col, cursor.line, cursor.col, text, coalescable = false)
    }

    private fun insertSingleSmartChar(ch: Char): Pos {
        val (l, c) = cursor
        val lineText = lines[l]
        val after: Char? = lineText.getOrNull(c)

        // التخطي فوق قوس/تنصيص مغلق مطابق بدل إدراج نسخة مكرّرة منه.
        if ((ch in CLOSE_TO_OPEN.keys || ch in AUTO_PAIR_QUOTES) && after == ch) {
            cursor = Pos(l, c + 1)
            collapseSelection()
            return cursor
        }

        if (ch == '\n') return insertNewlineSmart()

        if (ch in OPEN_TO_CLOSE.keys) {
            val closeCh = OPEN_TO_CLOSE.getValue(ch)
            val pos = applyReplaceTracked(l, c, l, c, "$ch$closeCh", coalescable = false)
            cursor = Pos(pos.line, pos.col - 1)
            collapseSelection()
            return cursor
        }

        if (ch in AUTO_PAIR_QUOTES) {
            val isWordBefore = c > 0 && (lineText[c - 1].isLetterOrDigit() || lineText[c - 1] == '_')
            if (!isWordBefore) {
                val pos = applyReplaceTracked(l, c, l, c, "$ch$ch", coalescable = false)
                cursor = Pos(pos.line, pos.col - 1)
                collapseSelection()
                return cursor
            }
        }

        return applyReplaceTracked(l, c, l, c, ch.toString(), coalescable = true)
    }

    private fun insertNewlineSmart(): Pos {
        val (l, c) = cursor
        val lineText = lines[l]
        val indent = leadingWhitespace(lineText)
        val before: Char? = if (c > 0) lineText[c - 1] else null
        val after: Char? = lineText.getOrNull(c)

        if (before != null && after != null && OPEN_TO_CLOSE[before] == after) {
            val innerIndent = "$indent    "
            val insertion = "\n$innerIndent\n$indent"
            applyReplaceTracked(l, c, l, c, insertion, coalescable = false)
            cursor = Pos(l + 1, innerIndent.length)
            collapseSelection()
            return cursor
        }

        val newIndent = if (before != null && before in OPEN_TO_CLOSE.keys) "$indent    " else indent
        applyReplaceTracked(l, c, l, c, "\n$newIndent", coalescable = false)
        cursor = Pos(l + 1, newIndent.length)
        collapseSelection()
        return cursor
    }

    /** مكافئ لزر Backspace: يحذف حرفاً واحداً (أو زوج قوس/تنصيص تلقائي بالكامل، أو التحديد إن وُجد). */
    fun deleteBackward(): Pos {
        if (hasSel) return deleteSelectionIfAny()
        val (l, c) = cursor
        if (c == 0) {
            if (l == 0) return cursor
            val prevLen = lines[l - 1].length
            applyReplaceTracked(l - 1, prevLen, l, 0, "", coalescable = true)
            cursor = Pos(l - 1, prevLen)
            collapseSelection()
            return cursor
        }
        val lineText = lines[l]
        val before = lineText[c - 1]
        val after = lineText.getOrNull(c)
        val isAutoPair = (OPEN_TO_CLOSE[before] != null && OPEN_TO_CLOSE[before] == after) ||
            (before in AUTO_PAIR_QUOTES && after == before)
        if (isAutoPair) {
            applyReplaceTracked(l, c - 1, l, c + 1, "", coalescable = true)
            cursor = Pos(l, c - 1)
        } else {
            applyReplaceTracked(l, c - 1, l, c, "", coalescable = true)
            cursor = Pos(l, c - 1)
        }
        collapseSelection()
        return cursor
    }

    /** مكافئ لزر Delete: يحذف حرفاً واحداً للأمام (أو التحديد إن وُجد). */
    fun deleteForward(): Pos {
        if (hasSel) return deleteSelectionIfAny()
        val (l, c) = cursor
        val lineText = lines[l]
        if (c >= lineText.length) {
            if (l >= lines.size - 1) return cursor
            applyReplaceTracked(l, c, l + 1, 0, "", coalescable = true)
        } else {
            applyReplaceTracked(l, c, l, c + 1, "", coalescable = true)
        }
        cursor = Pos(l, c)
        collapseSelection()
        return cursor
    }

    fun deleteSelectionIfAny(): Pos {
        if (!hasSel) return cursor
        val (a, b) = normalizedSelection()
        return applyReplaceTracked(a.line, a.col, b.line, b.col, "", coalescable = false)
    }

    fun replaceRange(sl: Int, sc: Int, el: Int, ec: Int, text: String): Pos =
        applyReplaceTracked(sl, sc, el, ec, text, coalescable = false)

    // --- التنفيذ الخام + التتبّع للتراجع/الإعادة --------------------------------

    private fun applyReplaceTracked(sl: Int, sc: Int, el: Int, ec: Int, text: String, coalescable: Boolean): Pos {
        val a = clampPos(Pos(sl, sc))
        val b = clampPos(Pos(el, ec))
        val (from, to) = if (comparePos(a, b) <= 0) a to b else b to a
        val removed = textOfRange(from.line, from.col, to.line, to.col)
        val cursorBefore = cursor
        val newEnd = applyRawReplace(from.line, from.col, to.line, to.col, text)
        val rec = EditRecord(
            from.line, from.col, to.line, to.col, newEnd.line, newEnd.col,
            removed, text, cursorBefore, newEnd, System.currentTimeMillis(), coalescable
        )
        pushUndo(rec)
        redoStack.clear()
        cursor = newEnd
        hasSel = false
        selAnchor = newEnd
        invalidateHighlightCache()
        return newEnd
    }

    private fun applyRawReplace(sl: Int, sc: Int, el: Int, ec: Int, text: String): Pos {
        val startLineText = lines[sl]
        val endLineText = lines[el]
        val prefix = startLineText.substring(0, sc.coerceIn(0, startLineText.length))
        val suffix = endLineText.substring(ec.coerceIn(0, endLineText.length))
        val combined = prefix + text + suffix
        val newPieces = combined.split("\n")

        val rebuilt = ArrayList<String>(lines.size - (el - sl + 1) + newPieces.size)
        rebuilt.addAll(lines.subList(0, sl))
        rebuilt.addAll(newPieces)
        rebuilt.addAll(lines.subList(el + 1, lines.size))
        lines.clear()
        lines.addAll(rebuilt)
        if (lines.isEmpty()) lines.add("")

        val newEndLine = sl + newPieces.size - 1
        val newEndCol = newPieces.last().length - suffix.length
        return Pos(newEndLine, newEndCol)
    }

    private fun textOfRange(sl: Int, sc: Int, el: Int, ec: Int): String {
        if (sl == el) return lines[sl].substring(sc.coerceIn(0, lines[sl].length), ec.coerceIn(0, lines[sl].length))
        val sb = StringBuilder()
        sb.append(lines[sl].substring(sc.coerceIn(0, lines[sl].length)))
        for (l in sl + 1 until el) {
            sb.append('\n')
            sb.append(lines[l])
        }
        sb.append('\n')
        sb.append(lines[el].substring(0, ec.coerceIn(0, lines[el].length)))
        return sb.toString()
    }

    private fun pushUndo(rec: EditRecord) {
        val top = undoStack.lastOrNull()
        val canCoalesceInsert = rec.coalescable && top != null && top.coalescable &&
            rec.timestampMs - top.timestampMs <= COALESCE_WINDOW_MS &&
            top.newEndLine == rec.startLine && top.newEndCol == rec.startCol &&
            rec.removedText.isEmpty() && top.removedText.isEmpty()
        val canCoalesceDelete = rec.coalescable && top != null && top.coalescable &&
            rec.timestampMs - top.timestampMs <= COALESCE_WINDOW_MS &&
            rec.insertedText.isEmpty() && top.insertedText.isEmpty() &&
            rec.origEndLine == top.startLine && rec.origEndCol == top.startCol
        when {
            canCoalesceInsert -> {
                undoStack.removeLast()
                undoStack.addLast(
                    top!!.copy(
                        newEndLine = rec.newEndLine, newEndCol = rec.newEndCol,
                        insertedText = top.insertedText + rec.insertedText,
                        cursorAfter = rec.cursorAfter, timestampMs = rec.timestampMs
                    )
                )
            }
            canCoalesceDelete -> {
                undoStack.removeLast()
                undoStack.addLast(
                    top!!.copy(
                        startLine = rec.startLine, startCol = rec.startCol,
                        removedText = rec.removedText + top.removedText,
                        cursorAfter = rec.cursorAfter, timestampMs = rec.timestampMs
                    )
                )
            }
            else -> {
                undoStack.addLast(rec)
                if (undoStack.size > MAX_HISTORY) undoStack.removeFirst()
            }
        }
    }

    // --- تراجع/إعادة ---------------------------------------------------------

    fun undo(): Pos? {
        val rec = undoStack.removeLastOrNull() ?: return null
        applyRawReplace(rec.startLine, rec.startCol, rec.newEndLine, rec.newEndCol, rec.removedText)
        cursor = rec.cursorBefore
        hasSel = false
        selAnchor = cursor
        redoStack.addLast(rec)
        invalidateHighlightCache()
        return cursor
    }

    fun redo(): Pos? {
        val rec = redoStack.removeLastOrNull() ?: return null
        applyRawReplace(rec.startLine, rec.startCol, rec.origEndLine, rec.origEndCol, rec.insertedText)
        cursor = rec.cursorAfter
        hasSel = false
        selAnchor = cursor
        undoStack.addLast(rec)
        invalidateHighlightCache()
        return cursor
    }

    // --- أوامر مستوى-السطر ----------------------------------------------------

    private fun selectionLineRangeOrCursorLine(): Pair<Int, Int> {
        if (!hasSel) return cursor.line to cursor.line
        val (a, b) = normalizedSelection()
        // إن انتهى التحديد عند بداية سطر بالضبط ولم يكن سطراً واحداً، لا نُدرج ذلك السطر الأخير
        // (سلوك محررات الأكواد المعتادة عند تحديد "حتى بداية السطر التالي" بالسحب).
        val endLine = if (b.line > a.line && b.col == 0) (b.line - 1).coerceAtLeast(a.line) else b.line
        return a.line to endLine
    }

    fun duplicateCurrentLine(): Pos {
        val l = cursor.line
        val col = cursor.col
        val lineText = lines[l]
        val pos = applyReplaceTracked(l, lineText.length, l, lineText.length, "\n$lineText", coalescable = false)
        cursor = Pos(pos.line, col.coerceIn(0, lines[pos.line].length))
        collapseSelection()
        return cursor
    }

    fun deleteCurrentLine(): Pos {
        val l = cursor.line
        if (lines.size == 1) {
            return applyReplaceTracked(0, 0, 0, lines[0].length, "", coalescable = false)
        }
        val pos = if (l < lines.size - 1) {
            applyReplaceTracked(l, 0, l + 1, 0, "", coalescable = false)
        } else {
            applyReplaceTracked(l - 1, lines[l - 1].length, l, lines[l].length, "", coalescable = false)
        }
        return pos
    }

    fun moveLineUp(): Pos {
        val l = cursor.line
        if (l == 0) return cursor
        val col = cursor.col
        val above = lines[l - 1]
        val current = lines[l]
        applyReplaceTracked(l - 1, 0, l, current.length, "$current\n$above", coalescable = false)
        cursor = Pos(l - 1, col.coerceIn(0, current.length))
        collapseSelection()
        return cursor
    }

    fun moveLineDown(): Pos {
        val l = cursor.line
        if (l >= lines.size - 1) return cursor
        val col = cursor.col
        val current = lines[l]
        val below = lines[l + 1]
        applyReplaceTracked(l, 0, l + 1, below.length, "$below\n$current", coalescable = false)
        cursor = Pos(l + 1, col.coerceIn(0, current.length))
        collapseSelection()
        return cursor
    }

    fun toggleLineComment(): Pos {
        val (sl, el) = selectionLineRangeOrCursorLine()
        val prefix = language.lineCommentPrefix() ?: "//"
        val original = (sl..el).map { lines[it] }
        val commentable = original.filter { it.isNotBlank() }
        val allCommented = commentable.isNotEmpty() && commentable.all { it.trimStart().startsWith(prefix) }
        val rebuilt = original.map { lineText ->
            when {
                lineText.isBlank() -> lineText
                allCommented -> {
                    val idx = lineText.indexOf(prefix)
                    val head = lineText.substring(0, idx)
                    var tail = lineText.substring(idx + prefix.length)
                    if (tail.startsWith(" ")) tail = tail.substring(1)
                    head + tail
                }
                else -> {
                    val indent = leadingWhitespace(lineText)
                    indent + prefix + " " + lineText.substring(indent.length)
                }
            }
        }
        val endColOriginal = lines[el].length
        return applyReplaceTracked(sl, 0, el, endColOriginal, rebuilt.joinToString("\n"), coalescable = false)
    }

    fun indentSelection(): Pos {
        val (sl, el) = selectionLineRangeOrCursorLine()
        val original = (sl..el).map { lines[it] }
        val rebuilt = original.map { if (it.isEmpty()) it else "    $it" }
        val endColOriginal = lines[el].length
        return applyReplaceTracked(sl, 0, el, endColOriginal, rebuilt.joinToString("\n"), coalescable = false)
    }

    fun unindentSelection(): Pos {
        val (sl, el) = selectionLineRangeOrCursorLine()
        val original = (sl..el).map { lines[it] }
        val rebuilt = original.map { lineText ->
            when {
                lineText.startsWith("    ") -> lineText.substring(4)
                lineText.startsWith("\t") -> lineText.substring(1)
                else -> {
                    var i = 0
                    while (i < lineText.length && i < 4 && lineText[i] == ' ') i++
                    lineText.substring(i)
                }
            }
        }
        val endColOriginal = lines[el].length
        return applyReplaceTracked(sl, 0, el, endColOriginal, rebuilt.joinToString("\n"), coalescable = false)
    }

    // --- فحص/بحث ----------------------------------------------------------

    /** -1 = متوازن، وإلا رقم أول سطر فيه خلل (1-based). لا يحتسب الأقواس داخل نصوص محاطة بتنصيص. */
    fun checkBracketBalance(): Int {
        val stack = ArrayDeque<Pair<Char, Int>>()
        for ((li, lineText) in lines.withIndex()) {
            var inString: Char? = null
            var i = 0
            while (i < lineText.length) {
                val c = lineText[i]
                if (inString != null) {
                    when {
                        c == '\\' -> i++
                        c == inString -> inString = null
                    }
                    i++
                    continue
                }
                when (c) {
                    '"', '\'' -> inString = c
                    '(', '[', '{' -> stack.addLast(c to li)
                    ')', ']', '}' -> {
                        if (stack.isEmpty() || stack.last().first != CLOSE_TO_OPEN[c]) return li + 1
                        stack.removeLast()
                    }
                }
                i++
            }
        }
        return if (stack.isEmpty()) -1 else stack.last().second + 1
    }

    fun findAll(query: String, caseSensitive: Boolean): List<FindMatch> {
        if (query.isEmpty()) return emptyList()
        val out = ArrayList<FindMatch>()
        for ((li, lineText) in lines.withIndex()) {
            var from = 0
            while (true) {
                val idx = lineText.indexOf(query, from, ignoreCase = !caseSensitive)
                if (idx < 0) break
                out.add(FindMatch(li, idx, idx + query.length))
                from = idx + query.length.coerceAtLeast(1)
            }
        }
        return out
    }

    /** بداية السطر رقم [oneBasedLine] (مُقيَّد ضمن الحدود). */
    fun lineStartPosition(oneBasedLine: Int): Pos {
        val zeroBased = clampLine(oneBasedLine - 1)
        return Pos(zeroBased, 0)
    }

    // --- تلوين نحوي حقيقي (RinSyntax) --------------------------------------------

    private var highlightCache: List<Highlight>? = null

    private fun invalidateHighlightCache() { highlightCache = null }

    fun getHighlights(): List<Highlight> {
        highlightCache?.let { return it }
        val computed = RinSyntax.computeHighlights(lines, language)
        highlightCache = computed
        return computed
    }

    // --- إكمال تلقائي (autocomplete) ------------------------------------------

    /** اقتراحات إكمال حتى [maxResults] تبدأ بـ[prefix] (غير حسّاسة لحالة الأحرف): كلمات محجوزة
     *  للغة الحالية أولاً، ثم معرِّفات المستند الفريدة الظاهرة فعلياً — كلٌّ منها مُرتَّب أبجدياً. */
    fun collectSuggestions(prefix: String, maxResults: Int): List<String> {
        if (prefix.isEmpty()) return emptyList()
        val lowerPrefix = prefix.lowercase()
        val keywords = RinSyntax.keywordsFor(language)
            .filter { it.lowercase().startsWith(lowerPrefix) && !it.equals(prefix, ignoreCase = true) }
            .sorted()

        val identifierRegex = Regex("[A-Za-z_][A-Za-z0-9_]*")
        val identifiers = sortedSetOf<String>()
        for (lineText in lines) {
            for (m in identifierRegex.findAll(lineText)) {
                val word = m.value
                if (word.lowercase().startsWith(lowerPrefix) && !word.equals(prefix, ignoreCase = true)) {
                    identifiers.add(word)
                }
            }
        }
        val merged = LinkedHashSet<String>()
        merged.addAll(keywords)
        merged.addAll(identifiers)
        return merged.take(maxResults)
    }

    private fun leadingWhitespace(s: String): String {
        var i = 0
        while (i < s.length && (s[i] == ' ' || s[i] == '\t')) i++
        return s.substring(0, i)
    }
}

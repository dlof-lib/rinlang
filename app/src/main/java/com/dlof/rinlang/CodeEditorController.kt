package com.dlof.rinlang

import android.content.Context
import android.text.Editable
import android.text.Spannable
import android.text.TextWatcher
import android.text.style.BackgroundColorSpan
import android.os.Handler
import android.os.Looper
import android.widget.ScrollView
import android.widget.TextView
import androidx.core.content.ContextCompat
import java.util.ArrayDeque

/**
 * Adds "real editor" behaviour on top of [RinEditText] used for Rin source:
 * a synced line-number gutter, debounced undo/redo, auto-indent (with smart
 * dedent on `}`), auto-closing brackets/quotes, matching-bracket + current-line
 * highlighting, line-level editing commands (duplicate/delete/move/comment/
 * indent), go-to-line, and a find/replace bar with match highlighting, a
 * match counter and case-sensitivity. Kept separate from [MainActivity] so
 * that activity stays focused on wiring, not text-editing mechanics.
 */
class CodeEditorController(
    private val context: Context,
    private val editText: RinEditText,
    private val lineNumbers: TextView,
    private val scrollView: ScrollView? = null
) {
    // --- Undo / redo -------------------------------------------------

    private val undoStack = ArrayDeque<String>()
    private val redoStack = ArrayDeque<String>()
    private var lastSnapshot: String = editText.text.toString()
    private val handler = Handler(Looper.getMainLooper())
    private var pendingSnapshot: Runnable? = null
    private var suppressWatchers = false

    private val snapshotDelayMs = 700L
    private val maxHistory = 100

    private fun scheduleSnapshot() {
        pendingSnapshot?.let { handler.removeCallbacks(it) }
        val r = Runnable { commitSnapshot() }
        pendingSnapshot = r
        handler.postDelayed(r, snapshotDelayMs)
    }

    private fun commitSnapshot() {
        val current = editText.text.toString()
        if (current == lastSnapshot) return
        undoStack.addLast(lastSnapshot)
        if (undoStack.size > maxHistory) undoStack.removeFirst()
        redoStack.clear()
        lastSnapshot = current
    }

    fun undo() {
        pendingSnapshot?.let { handler.removeCallbacks(it) }
        commitSnapshot()
        if (undoStack.isEmpty()) return
        redoStack.addLast(editText.text.toString())
        val prev = undoStack.removeLast()
        applyText(prev)
    }

    fun redo() {
        if (redoStack.isEmpty()) return
        undoStack.addLast(editText.text.toString())
        val next = redoStack.removeLast()
        applyText(next)
    }

    private fun applyText(text: String, selection: Int? = null) {
        suppressWatchers = true
        lastSnapshot = text
        editText.setText(text)
        val sel = (selection ?: text.length).coerceIn(0, text.length)
        editText.setSelection(sel)
        suppressWatchers = false
        updateLineNumbers(text)
        updateEditorHighlights()
    }

    // --- Auto-indent + smart dedent on `}` --------------------------------

    private var autoIndenting = false
    private val bracketPairs = mapOf('(' to ')', '[' to ']', '{' to '}')
    private val closingToOpening = mapOf(')' to '(', ']' to '[', '}' to '{')
    private val quoteChars = setOf('"', '\'')

    private fun applyAutoIndent(editable: Editable) {
        if (autoIndenting) return
        val cursor = editText.selectionStart
        if (cursor !in 1..editable.length) return

        val lastChar = editable[cursor - 1]
        if (lastChar == '\n') {
            val beforeCursor = editable.substring(0, cursor - 1)
            val prevLineStart = beforeCursor.lastIndexOf('\n') + 1
            val prevLine = beforeCursor.substring(prevLineStart)
            val currentIndent = Regex("^[ \t]*").find(prevLine)?.value ?: ""
            var indent = currentIndent
            if (prevLine.trimEnd().endsWith("{")) indent += "    "
            if (indent.isEmpty()) return

            autoIndenting = true
            editable.insert(cursor, indent)
            editText.setSelection(cursor + indent.length)
            autoIndenting = false
            return
        }

        // Dedent: if the user just typed `}` and it is the only non-whitespace
        // character on its line so far, align it with the line of its matching `{`.
        if (lastChar == '}') {
            val lineStart = editable.lastIndexOf('\n', (cursor - 2).coerceAtLeast(0)).let { if (it == -1) 0 else it + 1 }
            if (lineStart >= cursor - 1) return
            val beforeBrace = editable.substring(lineStart, cursor - 1)
            if (beforeBrace.isNotBlank()) return

            val fullText = editable.toString()
            val matchOpenIdx = findMatchingBracket(fullText, cursor - 1)
            if (matchOpenIdx == -1) return
            val openLineStart = fullText.lastIndexOf('\n', (matchOpenIdx - 1).coerceAtLeast(0)).let { if (it == -1) 0 else it + 1 }
            val openLineEndRaw = fullText.indexOf('\n', openLineStart)
            val openLineEnd = if (openLineEndRaw == -1) matchOpenIdx else openLineEndRaw
            val openLine = fullText.substring(openLineStart, openLineEnd.coerceAtLeast(openLineStart))
            val targetIndent = Regex("^[ \t]*").find(openLine)?.value ?: ""
            val currentIndent = beforeBrace // all whitespace, since beforeBrace.isBlank()

            if (currentIndent == targetIndent) return
            autoIndenting = true
            editable.replace(lineStart, cursor - 1, targetIndent)
            editText.setSelection(lineStart + targetIndent.length + 1)
            autoIndenting = false
        }
    }

    // --- Auto-closing brackets & quotes, and smart "delete pair" backspace --

    private var autoClosing = false
    private var pendingPairDelete = false
    private var pairDeletePos = -1

    private fun beforeTextChangedForPairs(s: CharSequence, start: Int, count: Int, after: Int) {
        pendingPairDelete = false
        // A single-character deletion (backspace) with nothing being inserted.
        if (count == 1 && after == 0 && start >= 0 && start < s.length && start + 1 < s.length) {
            val deleted = s[start]
            val next = s[start + 1]
            val isBracketPair = bracketPairs[deleted] == next
            val isQuotePair = deleted in quoteChars && next == deleted
            if (isBracketPair || isQuotePair) {
                pendingPairDelete = true
                pairDeletePos = start
            }
        }
    }

    private fun applyPairDelete(editable: Editable) {
        if (!pendingPairDelete) return
        pendingPairDelete = false
        if (pairDeletePos in 0 until editable.length) {
            autoClosing = true
            editable.delete(pairDeletePos, pairDeletePos + 1)
            autoClosing = false
        }
    }

    private fun charOrNull(s: CharSequence, index: Int): Char? = if (index in s.indices) s[index] else null

    private fun applyAutoClose(editable: Editable) {
        if (autoClosing) return
        val cursor = editText.selectionStart
        if (cursor !in 1..editable.length) return
        val typed = editable[cursor - 1]

        // Skip-over: typing a closing char that's already the very next char.
        if ((typed in closingToOpening.keys || typed in quoteChars) &&
            cursor < editable.length && editable[cursor] == typed
        ) {
            autoClosing = true
            editable.delete(cursor - 1, cursor)
            editText.setSelection(cursor)
            autoClosing = false
            return
        }

        val closer = bracketPairs[typed] ?: if (typed in quoteChars) typed else null
        if (closer != null) {
            // Don't auto-close a quote right before a letter/digit (likely mid-word edit).
            val nextChar = charOrNull(editable, cursor)
            if (typed in quoteChars && nextChar != null && nextChar.isLetterOrDigit()) return
            autoClosing = true
            editable.insert(cursor, closer.toString())
            editText.setSelection(cursor)
            autoClosing = false
        }
    }

    // --- Matching-bracket + current-line highlighting -----------------------

    private val colorCurrentLine by lazy { ContextCompat.getColor(context, R.color.rin_current_line_bg) }
    private val colorBracketMatch by lazy { ContextCompat.getColor(context, R.color.rin_bracket_match_bg) }
    private val colorBracketError by lazy { ContextCompat.getColor(context, R.color.rin_bracket_error_bg) }
    private val colorFindMatch by lazy { ContextCompat.getColor(context, R.color.rin_find_match_bg) }

    private val currentLineSpans = mutableListOf<Any>()
    private val bracketSpans = mutableListOf<Any>()
    private val findMatchSpans = mutableListOf<Any>()

    private fun clearSpans(list: MutableList<Any>, editable: Editable) {
        for (span in list) editable.removeSpan(span)
        list.clear()
    }

    private fun updateEditorHighlights() {
        val editable = editText.text ?: return
        val text = editable.toString()
        val cursor = editText.selectionStart.coerceIn(0, text.length)

        clearSpans(currentLineSpans, editable)
        val lineStart = text.lastIndexOf('\n', (cursor - 1).coerceAtLeast(0)).let { if (it == -1) 0 else it + 1 }
        val lineEndRaw = text.indexOf('\n', lineStart)
        val lineEnd = if (lineEndRaw == -1) text.length else lineEndRaw
        if (lineStart <= lineEnd) {
            val span = BackgroundColorSpan(colorCurrentLine)
            editable.setSpan(span, lineStart, lineEnd.coerceAtLeast(lineStart), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            currentLineSpans.add(span)
        }

        clearSpans(bracketSpans, editable)
        val charBefore = if (cursor > 0) charOrNull(text, cursor - 1) else null
        val charAt = charOrNull(text, cursor)
        val bracketAt = when {
            charBefore != null && (charBefore in bracketPairs.keys || charBefore in closingToOpening.keys) -> cursor - 1
            charAt != null && (charAt in bracketPairs.keys || charAt in closingToOpening.keys) -> cursor
            else -> -1
        }
        if (bracketAt != -1) {
            val matchIdx = findMatchingBracketAny(text, bracketAt)
            if (matchIdx != -1) {
                for (idx in intArrayOf(bracketAt, matchIdx)) {
                    val span = BackgroundColorSpan(colorBracketMatch)
                    editable.setSpan(span, idx, idx + 1, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                    bracketSpans.add(span)
                }
            } else {
                val span = BackgroundColorSpan(colorBracketError)
                editable.setSpan(span, bracketAt, bracketAt + 1, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                bracketSpans.add(span)
            }
        }
    }

    /** For a bracket at [index] (opening OR closing), returns the index of its match, or -1. */
    private fun findMatchingBracketAny(text: String, index: Int): Int {
        val c = charOrNull(text, index) ?: return -1
        return when {
            c in bracketPairs.keys -> findMatchingCloser(text, index)
            c in closingToOpening.keys -> findMatchingBracket(text, index)
            else -> -1
        }
    }

    private fun findMatchingCloser(text: String, openIndex: Int): Int {
        val open = text[openIndex]
        val close = bracketPairs[open] ?: return -1
        var depth = 0
        for (i in openIndex until text.length) {
            val c = text[i]
            if (c == open) depth++
            else if (c == close) {
                depth--
                if (depth == 0) return i
            }
        }
        return -1
    }

    /** For a closing bracket at [closeIndex], returns the index of its matching opener, or -1. */
    private fun findMatchingBracket(text: String, closeIndex: Int): Int {
        val close = charOrNull(text, closeIndex) ?: return -1
        val open = closingToOpening[close] ?: return -1
        var depth = 0
        for (i in closeIndex downTo 0) {
            val c = text[i]
            if (c == close) depth++
            else if (c == open) {
                depth--
                if (depth == 0) return i
            }
        }
        return -1
    }

    /**
     * Scans the whole document for bracket balance. Returns null if balanced,
     * or the 1-based line number where the first problem was detected.
     */
    fun checkBracketBalance(): Int? {
        val text = editText.text.toString()
        data class Open(val ch: Char, val line: Int)
        val stack = ArrayDeque<Open>()
        var line = 1
        for (c in text) {
            when {
                c == '\n' -> line++
                c in bracketPairs.keys -> stack.addLast(Open(c, line))
                c in closingToOpening.keys -> {
                    if (stack.isEmpty() || bracketPairs[stack.last().ch] != c) return line
                    stack.removeLast()
                }
            }
        }
        if (stack.isNotEmpty()) return stack.last().line
        return null
    }

    // --- Programmatic insertion (used by the "Libraries" section: inserting an @import line) ---

    /**
     * Inserts [text] at the current cursor position (or replacing the current selection),
     * then moves the cursor to just after the inserted text. Goes through the normal
     * [Editable], so undo/redo history, line numbers and syntax highlighting all update as usual.
     */
    fun insertAtCursor(text: String) {
        val start = editText.selectionStart.coerceAtLeast(0)
        val end = editText.selectionEnd.coerceAtLeast(start)
        editText.text?.replace(start, end, text)
        editText.setSelection(start + text.length)
    }

    /**
     * يُدرج مقتطف [template] عند المؤشر (مستبدلاً أي تحديد حالي)، تماماً كـ [insertAtCursor]،
     * لكن إن كان [template] يحتوي على [RinSnippets.CURSOR_MARKER] فإنها تُزال ويُترَك المؤشر
     * في مكانها بالضبط بدل نهاية النص المُدرَج بالكامل — يُستخدم من قِبل قائمة "مقتطفات Rin"
     * حتى يبقى المؤشر داخل جسم الحاوية المُدرَجة، لا بعد وسم `.end/...` الخاص بها.
     */
    fun insertSnippetAtCursor(template: String) {
        val markerIndex = template.indexOf(RinSnippets.CURSOR_MARKER)
        val text = if (markerIndex == -1) template else template.replace(RinSnippets.CURSOR_MARKER, "")
        val start = editText.selectionStart.coerceAtLeast(0)
        val end = editText.selectionEnd.coerceAtLeast(start)
        editText.text?.replace(start, end, text)
        val cursor = if (markerIndex == -1) start + text.length else start + markerIndex
        editText.setSelection(cursor)
        scrollToCursor()
    }

    /**
     * يفحص توازن وسوم لغة الحاويات في Rin (`@container=x ... .end/container`, `Section`,
     * `Translations`, `@view.*`, `@theme`...)، بنفس أسلوب [checkBracketBalance] لكن لوسوم Rin
     * بدل أقواس `{}`/`[]`/`()`. يُرجع null إن كانت متوازنة، أو رقم أول سطر فيه مشكلة.
     */
    fun checkTagBalance(): Int? = RinContainerTags.checkTagBalance(editText.text.toString())

    /** يبني قائمة "بنية الملف" (كل وسوم الحاويات مع أرقام أسطرها وعمق تعشيشها) للتنقّل السريع. */
    fun buildOutline(): List<RinContainerTags.OutlineEntry> = RinContainerTags.buildOutline(editText.text.toString())

    // --- Line-level editing commands ---------------------------------------

    private fun lineBounds(text: String, pos: Int): Pair<Int, Int> {
        val start = text.lastIndexOf('\n', (pos - 1).coerceAtLeast(0)).let { if (it == -1) 0 else it + 1 }
        val endRaw = text.indexOf('\n', pos)
        val end = if (endRaw == -1) text.length else endRaw
        return start to end
    }

    /** Duplicates the line the cursor is on, placing the copy right below. */
    fun duplicateCurrentLine() {
        val text = editText.text.toString()
        val cursor = editText.selectionStart
        val (start, end) = lineBounds(text, cursor)
        val line = text.substring(start, end)
        val newText = text.substring(0, end) + "\n" + line + text.substring(end)
        applyText(newText, end + 1 + (cursor - start))
    }

    /** Deletes the entire line the cursor is on (including its newline). */
    fun deleteCurrentLine() {
        val text = editText.text.toString()
        val cursor = editText.selectionStart
        val (start, end) = lineBounds(text, cursor)
        val deleteEnd = if (end < text.length) end + 1 else end
        val deleteStart = if (end == text.length && start > 0) start - 1 else start
        val newText = text.removeRange(deleteStart.coerceAtLeast(0), deleteEnd.coerceAtMost(text.length))
        applyText(newText, deleteStart.coerceIn(0, newText.length))
    }

    /** Swaps the current line with the one above it, keeping the cursor on the moved line. */
    fun moveLineUp() {
        val text = editText.text.toString()
        val cursor = editText.selectionStart
        val (start, end) = lineBounds(text, cursor)
        if (start == 0) return
        val prevEnd = start - 1
        val prevStart = text.lastIndexOf('\n', (prevEnd - 1).coerceAtLeast(0)).let { if (it == -1) 0 else it + 1 }
        val currentLine = text.substring(start, end)
        val prevLine = text.substring(prevStart, prevEnd)
        val newText = text.substring(0, prevStart) + currentLine + "\n" + prevLine + text.substring(end.coerceAtMost(text.length))
        val offsetInLine = cursor - start
        applyText(newText, prevStart + offsetInLine)
    }

    /** Swaps the current line with the one below it, keeping the cursor on the moved line. */
    fun moveLineDown() {
        val text = editText.text.toString()
        val cursor = editText.selectionStart
        val (start, end) = lineBounds(text, cursor)
        if (end >= text.length) return
        val nextStart = end + 1
        val nextEndRaw = text.indexOf('\n', nextStart)
        val nextEnd = if (nextEndRaw == -1) text.length else nextEndRaw
        val currentLine = text.substring(start, end)
        val nextLine = text.substring(nextStart, nextEnd)
        val newText = text.substring(0, start) + nextLine + "\n" + currentLine + text.substring(nextEnd.coerceAtMost(text.length))
        val offsetInLine = cursor - start
        val newLineStart = start + nextLine.length + 1
        applyText(newText, newLineStart + offsetInLine)
    }

    /** Toggles a leading `// ` line comment on every line touched by the current selection. */
    fun toggleLineComment() {
        val text = editText.text.toString()
        val selStart = editText.selectionStart.coerceIn(0, text.length)
        val selEnd = editText.selectionEnd.coerceIn(0, text.length)
        val blockStart = lineBounds(text, selStart.coerceAtMost(selEnd)).first
        val blockEndPos = lineBounds(text, selStart.coerceAtLeast(selEnd)).second
        val block = text.substring(blockStart, blockEndPos)
        val lines = block.split("\n")

        val allCommented = lines.all { it.isBlank() || it.trimStart().startsWith("//") }
        val newLines = lines.map { line ->
            if (line.isBlank()) return@map line
            if (allCommented) {
                val idx = line.indexOf("//")
                if (idx == -1) line else {
                    val after = line.substring(idx + 2).removePrefix(" ")
                    line.substring(0, idx) + after
                }
            } else {
                val indentEnd = line.indexOfFirst { !it.isWhitespace() }.let { if (it == -1) line.length else it }
                line.substring(0, indentEnd) + "// " + line.substring(indentEnd)
            }
        }
        val newBlock = newLines.joinToString("\n")
        val newText = text.substring(0, blockStart) + newBlock + text.substring(blockEndPos)
        val delta = newBlock.length - block.length
        applyText(newText, (selEnd + delta).coerceIn(0, newText.length))
    }

    private val indentUnit = "    "

    /** Adds one indent level to every line touched by the current selection. */
    fun indentSelection() {
        val text = editText.text.toString()
        val selStart = editText.selectionStart.coerceIn(0, text.length)
        val selEnd = editText.selectionEnd.coerceIn(0, text.length)
        val blockStart = lineBounds(text, selStart.coerceAtMost(selEnd)).first
        val blockEndPos = lineBounds(text, selStart.coerceAtLeast(selEnd)).second
        val block = text.substring(blockStart, blockEndPos)
        val lines = block.split("\n")
        val newLines = lines.map { if (it.isEmpty()) it else indentUnit + it }
        val newBlock = newLines.joinToString("\n")
        val newText = text.substring(0, blockStart) + newBlock + text.substring(blockEndPos)
        val delta = newBlock.length - block.length
        applyText(newText, (selEnd + delta).coerceIn(0, newText.length))
    }

    /** Removes up to one indent level from every line touched by the current selection. */
    fun unindentSelection() {
        val text = editText.text.toString()
        val selStart = editText.selectionStart.coerceIn(0, text.length)
        val selEnd = editText.selectionEnd.coerceIn(0, text.length)
        val blockStart = lineBounds(text, selStart.coerceAtMost(selEnd)).first
        val blockEndPos = lineBounds(text, selStart.coerceAtLeast(selEnd)).second
        val block = text.substring(blockStart, blockEndPos)
        val lines = block.split("\n")
        val newLines = lines.map { line ->
            val removed = when {
                line.startsWith(indentUnit) -> indentUnit.length
                line.startsWith("\t") -> 1
                else -> line.takeWhile { it == ' ' }.length.coerceAtMost(4)
            }
            line.substring(removed.coerceAtMost(line.length))
        }
        val newBlock = newLines.joinToString("\n")
        val newText = text.substring(0, blockStart) + newBlock + text.substring(blockEndPos)
        val delta = newBlock.length - block.length
        applyText(newText, (selEnd + delta).coerceIn(0, newText.length))
    }

    // --- Go to line ----------------------------------------------------------

    /** Moves the cursor to the start of [lineNumberOneBased] (clamped) and scrolls it into view. */
    fun goToLine(lineNumberOneBased: Int) {
        val text = editText.text.toString()
        val totalLines = text.count { it == '\n' } + 1
        val target = lineNumberOneBased.coerceIn(1, totalLines)
        var idx = 0
        var line = 1
        while (line < target) {
            val nl = text.indexOf('\n', idx)
            if (nl == -1) break
            idx = nl + 1
            line++
        }
        editText.requestFocus()
        editText.setSelection(idx)
        scrollToCursor()
    }

    fun lineCount(): Int = editText.text.toString().count { it == '\n' } + 1

    private fun scrollToCursor() {
        editText.post {
            val layout = editText.layout ?: return@post
            val cursor = editText.selectionStart.coerceIn(0, editText.text?.length ?: 0)
            val line = layout.getLineForOffset(cursor)
            val y = layout.getLineTop(line)
            scrollView?.smoothScrollTo(0, y)
        }
    }

    // --- Line numbers ----------------------------------------------------

    private fun updateLineNumbers(text: String) {
        val lineCount = text.count { it == '\n' } + 1
        val sb = StringBuilder(lineCount * 3)
        for (i in 1..lineCount) {
            sb.append(i)
            if (i != lineCount) sb.append('\n')
        }
        lineNumbers.text = sb.toString()
    }

    // --- Find / replace --------------------------------------------------

    var caseSensitiveSearch: Boolean = false

    private fun normalize(s: String): String = if (caseSensitiveSearch) s else s.lowercase()

    /** Selects the next occurrence of [query] after the current cursor, wrapping around. Returns whether found. */
    fun findNext(query: String): Boolean {
        if (query.isEmpty()) return false
        val text = normalize(editText.text.toString())
        val q = normalize(query)
        val from = editText.selectionEnd.coerceAtLeast(0)
        var idx = text.indexOf(q, from)
        if (idx == -1) idx = text.indexOf(q, 0)
        if (idx == -1) return false
        editText.requestFocus()
        editText.setSelection(idx, idx + query.length)
        scrollToCursor()
        return true
    }

    /** Selects the previous occurrence of [query] before the current cursor, wrapping around. */
    fun findPrevious(query: String): Boolean {
        if (query.isEmpty()) return false
        val text = normalize(editText.text.toString())
        val q = normalize(query)
        val from = (editText.selectionStart - 1).coerceAtLeast(0)
        var idx = text.lastIndexOf(q, from)
        if (idx == -1) idx = text.lastIndexOf(q)
        if (idx == -1) return false
        editText.requestFocus()
        editText.setSelection(idx, idx + query.length)
        scrollToCursor()
        return true
    }

    /** Replaces the current selection if it matches [query], then advances to the next match. */
    fun replaceOne(query: String, replacement: String) {
        if (query.isEmpty()) return
        val start = editText.selectionStart
        val end = editText.selectionEnd
        if (start in 0 until end && normalize(editText.text?.substring(start, end).orEmpty()) == normalize(query)) {
            editText.text?.replace(start, end, replacement)
            editText.setSelection(start + replacement.length)
        }
        findNext(query)
    }

    /** Replaces every occurrence of [query] with [replacement]; returns how many were replaced. */
    fun replaceAll(query: String, replacement: String): Int {
        if (query.isEmpty()) return 0
        val text = editText.text.toString()
        val searchIn = normalize(text)
        val q = normalize(query)
        var count = 0
        var idx = searchIn.indexOf(q)
        while (idx != -1) {
            count++
            idx = searchIn.indexOf(q, idx + q.length.coerceAtLeast(1))
        }
        if (count > 0) {
            val options = if (caseSensitiveSearch) emptySet() else setOf(RegexOption.IGNORE_CASE)
            val regex = Regex(Regex.escape(query), options)
            applyText(text.replace(regex, Regex.escapeReplacement(replacement)))
        }
        return count
    }

    /** 1-based index of the match at/after the cursor, and total match count, for an "N/M" display. */
    fun matchInfo(query: String): Pair<Int, Int> {
        if (query.isEmpty()) return 0 to 0
        val text = normalize(editText.text.toString())
        val q = normalize(query)
        val positions = mutableListOf<Int>()
        var idx = text.indexOf(q)
        while (idx != -1) {
            positions.add(idx)
            idx = text.indexOf(q, idx + q.length)
        }
        if (positions.isEmpty()) return 0 to 0
        val cursor = editText.selectionStart
        val currentIdx = positions.indexOfFirst { it >= cursor }.let { if (it == -1) 0 else it }
        return (currentIdx + 1) to positions.size
    }

    /** Highlights every occurrence of [query] in the editor (called as the find text changes). */
    fun highlightMatches(query: String) {
        val editable = editText.text ?: return
        clearSpans(findMatchSpans, editable)
        if (query.isEmpty()) return
        val text = normalize(editable.toString())
        val q = normalize(query)
        var idx = text.indexOf(q)
        while (idx != -1) {
            val span = BackgroundColorSpan(colorFindMatch)
            editable.setSpan(span, idx, idx + query.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            findMatchSpans.add(span)
            idx = text.indexOf(q, idx + q.length.coerceAtLeast(1))
        }
    }

    /** Clears any find-match highlighting (called when the find bar is closed). */
    fun clearMatchHighlights() {
        editText.text?.let { clearSpans(findMatchSpans, it) }
    }

    // --- Wiring ------------------------------------------------------------

    init {
        updateLineNumbers(editText.text.toString())
        editText.onSelectionChangedListener = { _, _ -> updateEditorHighlights() }
        editText.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {
                if (suppressWatchers || s == null) return
                beforeTextChangedForPairs(s, start, count, after)
            }
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(editable: Editable?) {
                if (suppressWatchers || editable == null) return
                applyPairDelete(editable)
                applyAutoIndent(editable)
                applyAutoClose(editable)
                updateLineNumbers(editable.toString())
                updateEditorHighlights()
                scheduleSnapshot()
            }
        })
        updateEditorHighlights()
    }
}

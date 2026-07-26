package com.dlof.rinlang

import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.widget.EditText
import android.widget.TextView
import java.util.ArrayDeque

/**
 * Adds "real editor" behaviour on top of the plain [EditText] used for Rin
 * source: a synced line-number gutter, debounced undo/redo, and auto-indent
 * after `{` / on newline. Kept separate from [MainActivity] so that activity
 * stays focused on wiring, not text-editing mechanics.
 */
class CodeEditorController(
    private val editText: EditText,
    private val lineNumbers: TextView
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

    private fun applyText(text: String) {
        suppressWatchers = true
        lastSnapshot = text
        editText.setText(text)
        editText.setSelection(text.length.coerceAtMost(text.length))
        suppressWatchers = false
        updateLineNumbers(text)
    }

    // --- Auto-indent ---------------------------------------------------

    private var autoIndenting = false

    private fun applyAutoIndent(editable: Editable) {
        if (autoIndenting) return
        val cursor = editText.selectionStart
        if (cursor !in 1..editable.length) return
        if (editable[cursor - 1] != '\n') return

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
        editText.text.replace(start, end, text)
        editText.setSelection(start + text.length)
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

    /** Selects the next occurrence of [query] after the current cursor, wrapping around. Returns whether found. */
    fun findNext(query: String): Boolean {
        if (query.isEmpty()) return false
        val text = editText.text.toString()
        val from = editText.selectionEnd.coerceAtLeast(0)
        var idx = text.indexOf(query, from)
        if (idx == -1) idx = text.indexOf(query, 0)
        if (idx == -1) return false
        editText.requestFocus()
        editText.setSelection(idx, idx + query.length)
        return true
    }

    /** Replaces the current selection if it matches [query], then advances to the next match. */
    fun replaceOne(query: String, replacement: String) {
        if (query.isEmpty()) return
        val start = editText.selectionStart
        val end = editText.selectionEnd
        if (start in 0 until end && editText.text.substring(start, end) == query) {
            editText.text.replace(start, end, replacement)
            editText.setSelection(start + replacement.length)
        }
        findNext(query)
    }

    /** Replaces every occurrence of [query] with [replacement]; returns how many were replaced. */
    fun replaceAll(query: String, replacement: String): Int {
        if (query.isEmpty()) return 0
        val text = editText.text.toString()
        val count = text.split(query).size - 1
        if (count > 0) {
            applyText(text.replace(query, replacement))
        }
        return count
    }

    // --- Wiring ------------------------------------------------------------

    init {
        updateLineNumbers(editText.text.toString())
        editText.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(editable: Editable?) {
                if (suppressWatchers || editable == null) return
                applyAutoIndent(editable)
                updateLineNumbers(editable.toString())
                scheduleSnapshot()
            }
        })
    }
}

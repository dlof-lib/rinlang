package com.dlof.rinlang

import android.content.Context
import android.text.Editable
import android.text.TextWatcher
import android.widget.ScrollView
import android.widget.TextView

/**
 * متحكم رفيع فوق [RinCodeEditorView] — لا يوجد أي منطق تحرير في هذا الملف نفسه، كل التحرير
 * الفعلي مُفوَّض بالكامل إلى [RinCodeEditorView] ومحركه [RinEditorEngine] (Kotlin خالص، بلا
 * أي C++/JNI). هذا الصنف مسؤول فقط عن:
 * مزامنة عمود أرقام الأسطر، التمرير التلقائي إلى المؤشر/التطابق، وحالة "بحث حسّاس لحالة الأحرف".
 */
class RinCodeEditorController(
    private val context: Context,
    private val editorView: RinCodeEditorView,
    private val lineNumbers: TextView,
    private val scrollView: ScrollView? = null
) {
    var caseSensitiveSearch: Boolean = false

    init {
        updateLineNumbers()
        editorView.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) { updateLineNumbers() }
        })
    }

    private fun updateLineNumbers() {
        val count = editorView.lineCount()
        val sb = StringBuilder(count * 3)
        for (i in 1..count) { sb.append(i); if (i != count) sb.append('\n') }
        lineNumbers.text = sb.toString()
    }

    private fun scrollToY(y: Int?) {
        if (y == null) return
        scrollView?.post { scrollView.smoothScrollTo(0, y) }
    }

    // --- تراجع/إعادة ---
    fun undo() { editorView.undo() }
    fun redo() { editorView.redo() }

    // --- أوامر مستوى-السطر ---
    fun duplicateCurrentLine() = editorView.duplicateCurrentLine()
    fun deleteCurrentLine() = editorView.deleteCurrentLine()
    fun moveLineUp() = editorView.moveLineUp()
    fun moveLineDown() = editorView.moveLineDown()
    fun toggleLineComment() = editorView.toggleLineComment()
    fun indentSelection() = editorView.indentSelection()
    fun unindentSelection() = editorView.unindentSelection()

    fun lineCount(): Int = editorView.lineCount()

    /** يفحص توازن الأقواس؛ null إن كانت متوازنة، وإلا رقم أول سطر فيه خلل (1-based). */
    fun checkBracketBalance(): Int? {
        val r = editorView.checkBracketBalance()
        return if (r == -1) null else r
    }

    /** يفحص توازن وسوم لغة الحاويات في Rin (@container=x ... .end/container، Section، ...). */
    fun checkTagBalance(): Int? = RinContainerTags.checkTagBalance(editorView.text.toString())

    /** يبني قائمة "بنية الملف" (وسوم الحاويات مع أرقام أسطرها وعمق تعشيشها) للتنقّل السريع. */
    fun buildOutline(): List<RinContainerTags.OutlineEntry> = RinContainerTags.buildOutline(editorView.text.toString())

    fun insertAtCursor(text: String) = editorView.insertAtCursor(text)

    /** يُدرج مقتطف [template]؛ إن حوى [RinSnippets.CURSOR_MARKER] يُزال ويُترَك المؤشر في مكانه بالضبط. */
    fun insertSnippetAtCursor(template: String) {
        val markerIndex = template.indexOf(RinSnippets.CURSOR_MARKER)
        if (markerIndex == -1) {
            editorView.insertAtCursor(template)
            return
        }
        val before = template.substring(0, markerIndex)
        val after = template.substring(markerIndex + RinSnippets.CURSOR_MARKER.length)
        editorView.insertAtCursor(before + after)
        // بعد الإدراج المؤشر في نهاية النص المُدرَج بالكامل؛ أرجعه للخلف بطول after ليستقر عند موضع العلامة.
        if (after.isNotEmpty()) {
            val cur = editorView.engine.getCursor()
            val targetChar = (cur.col - after.length).coerceAtLeast(0)
            editorView.engine.setCursor(cur.line, targetChar, false)
            editorView.invalidate()
        }
    }

    /** يبدّل لغة التلوين النحوي الحالية حسب امتداد الملف المفتوح (مثال: "kt"، "cpp"، "rin"). */
    fun setLanguage(extension: String) = editorView.setLanguage(extension)

    fun goToLine(lineNumberOneBased: Int) {
        val y = editorView.goToLine(lineNumberOneBased)
        scrollToY(y)
    }

    // --- بحث/استبدال ---
    fun findNext(query: String): Boolean {
        val y = editorView.findNext(query, caseSensitiveSearch) ?: return false
        scrollToY(y)
        return true
    }

    fun findPrevious(query: String): Boolean {
        val y = editorView.findPrevious(query, caseSensitiveSearch) ?: return false
        scrollToY(y)
        return true
    }

    fun replaceOne(query: String, replacement: String) = editorView.replaceOne(query, replacement, caseSensitiveSearch)
    fun replaceAll(query: String, replacement: String): Int = editorView.replaceAll(query, replacement, caseSensitiveSearch)
    fun matchInfo(query: String): Pair<Int, Int> = editorView.matchInfo(query, caseSensitiveSearch)

    fun highlightMatches(query: String) {
        if (query.isEmpty()) { editorView.setFindHighlights(null); return }
        editorView.setFindHighlights(editorView.engine.findAll(query, caseSensitiveSearch))
    }

    fun clearMatchHighlights() = editorView.setFindHighlights(null)
}

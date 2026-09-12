package com.dlof.rinlang

import android.content.Context
import android.view.GestureDetector
import android.view.MotionEvent
import android.widget.ScrollView
import android.widget.TextView

/**
 * متحكم رفيع فوق [RinCodeEditorView] — لا يوجد أي منطق تحرير في هذا الملف نفسه، كل التحرير
 * الفعلي مُفوَّض بالكامل إلى [RinCodeEditorView] ومحركه [RinNativeEditor] (C++17 عبر JNI، بلا
 * أي C++/JNI). هذا الصنف مسؤول فقط عن:
 * مزامنة عمود أرقام الأسطر (بما فيها مؤشر ▾/▸ الطيّ بجانب كل سطر، ومعالجة النقر عليه)،
 * التمرير التلقائي إلى المؤشر/التطابق، وحالة "بحث حسّاس لحالة الأحرف".
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
        // نقطة إعلام واحدة تُغطّي التعديل النصّي *و* أي طيّ/فكّ (foldAll/unfoldAll/toggleFold) —
        // كلاهما يمرّان عبر recomputeVisibleLines() داخل RinCodeEditorView، فلا حاجة لمستمع نصّي
        // منفصل بعد الآن (كان يُفوّت تمامًا أي تحديث لعمود الأسطر عند تبديل الطيّ وحده بلا أي
        // تعديل نصّي، فينحرف عدد/ترتيب الأسطر المعروضة هنا عمّا يرسمه RinCodeEditorView فعلياً).
        editorView.addVisibleLinesChangeListener { updateLineNumbers() }

        // النقر على عمود الأسطر نفسه يبدّل حالة طيّ السطر المقابل إن كان بداية كتلة قابلة للطيّ
        // (مؤشر ▾/▸ المرسوم بجانب رقمه في updateLineNumbers أدناه) — onSingleTapUp فقط (لا أي
        // إيماءة أخرى) حتى يبقى التمرير العمودي العادي بالسحب على هذا العمود يعمل بلا أي تعارض
        // (نُعيد false دوماً من onTouch لنترك الـScrollView المحيط يتولّى أي سحب طبيعي).
        val gutterGestureDetector = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
            override fun onSingleTapUp(e: MotionEvent): Boolean {
                val row = ((e.y - lineNumbers.paddingTop) / editorView.lineHeightPx).toInt()
                return editorView.toggleFoldAtVisibleRow(row)
            }
        })
        lineNumbers.isClickable = true
        lineNumbers.setOnTouchListener { _, event -> gutterGestureDetector.onTouchEvent(event); false }
    }

    /** يُعيد بناء نص عمود أرقام الأسطر بالكامل من الأسطر *الظاهرة* حالياً فقط (لا 1..lineCount()
     *  تسلسلياً كالسابق) — يبقى بالتالي محاذياً تمامًا لما يرسمه RinCodeEditorView.onDraw سطراً
     *  بسطر حتى مع وجود طيّات نشطة، ويحمل بجانب كل رقم مؤشره ▾ (كتلة مفتوحة)/▸ (مطويّة)/مسافة
     *  (سطر عادي) — بعرض حرف واحد ثابت دوماً (خط أحادي التباعد) فلا ينزاح محاذاة الأرقام يميناً
     *  (gravity="end") بين الأسطر التي تحمل مؤشراً وتلك التي لا تحمله. */
    private fun updateLineNumbers() {
        val numbers = editorView.visibleLineNumbers()
        val markers = editorView.visibleFoldMarkers()
        val sb = StringBuilder(numbers.size * 4)
        for (i in numbers.indices) {
            sb.append(numbers[i]).append(' ').append(markers[i])
            if (i != numbers.lastIndex) sb.append('\n')
        }
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

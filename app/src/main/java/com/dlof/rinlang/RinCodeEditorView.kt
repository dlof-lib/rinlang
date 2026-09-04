package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.InputType
import android.text.Selection
import android.text.SpannableStringBuilder
import android.text.TextWatcher
import android.util.AttributeSet
import android.util.TypedValue
import android.view.Gravity
import android.view.GestureDetector
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.inputmethod.BaseInputConnection
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.widget.LinearLayout
import android.widget.PopupWindow
import android.widget.TextView
import androidx.core.content.ContextCompat
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * محرر أكواد كامل مبني من الصفر فوق [View]/Canvas — بلا أي استخدام لـ [android.widget.EditText] —
 * وكل منطق التحرير الفعلي (تخزين، تراجع/إعادة، إزاحة تلقائية، إغلاق أقواس تلقائي، أوامر سطر،
 * بحث، تلوين نحوي حقيقي) موجود في محرك C++ (انظر [RinNativeEditor] وapp/src/main/cpp/editor/).
 *
 * هذا الصنف مسؤول فقط عن: القياس/الرسم (Canvas)، اللمس (وضع المؤشر/السحب للتحديد)، والربط
 * بلوحة المفاتيح الافتراضية (IME) عبر [InputConnection] حقيقي مبني على [BaseInputConnection]
 * فوق "مرآة" [Editable] تُبقى متزامنة مع المحرك الأصلي في الاتجاهين.
 *
 * يُستخدَم في XML بديلاً حرفياً عن RinEditText القديم (نفس wrap_content داخل
 * ScrollView/HorizontalScrollView خارجي يتوليان التمرير — هذا الصنف لا يُمرِّر نفسه إطلاقاً).
 */
class RinCodeEditorView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    val engine = RinNativeEditor()

    // --- إعداد الرسم ---------------------------------------------------

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = android.graphics.Typeface.MONOSPACE
        textSize = spToPx(14f)
        color = ContextCompat.getColor(context, R.color.rin_editor_text)
        isSubpixelText = true
        isLinearText = false
    }
    private val cursorPaint = Paint().apply { color = ContextCompat.getColor(context, R.color.rin_editor_text) }
    private val currentLinePaint = Paint().apply { color = ContextCompat.getColor(context, R.color.rin_current_line_bg) }
    private val selectionPaint = Paint().apply { color = 0x553D7EFF.toInt() }
    private val bracketMatchPaint = Paint().apply { color = ContextCompat.getColor(context, R.color.rin_bracket_match_bg) }
    private val bracketErrorPaint = Paint().apply { color = ContextCompat.getColor(context, R.color.rin_bracket_error_bg) }
    private val findMatchPaint = Paint().apply { color = ContextCompat.getColor(context, R.color.rin_find_match_bg) }

    private val colorKeyword = ContextCompat.getColor(context, R.color.syntax_keyword)
    private val colorString = ContextCompat.getColor(context, R.color.syntax_string)
    private val colorNumber = ContextCompat.getColor(context, R.color.syntax_number)
    private val colorAt = ContextCompat.getColor(context, R.color.syntax_tag)
    private val colorCall = ContextCompat.getColor(context, R.color.syntax_builtin)
    private val colorDefault = ContextCompat.getColor(context, R.color.rin_editor_text)
    private val colorError = Color.parseColor("#F14C4C")

    private var lineHeight = 0f
    private var charWidth = 0f
    private var ascent = 0f
    private fun recomputeMetrics() {
        val fm = textPaint.fontMetrics
        lineHeight = (fm.descent - fm.ascent) * 1.15f
        ascent = fm.ascent
        charWidth = textPaint.measureText("0")
    }

    // --- حالة نص الظل (Shadow Editable) لأجل IME فقط ---------------------

    private val shadowEditable = SpannableStringBuilder()
    private var lastKnownText: String = ""
    private var suppressForward = false

    // ذاكرة مؤقتة (cache) للتلوين النحوي ومطابقة الأقواس — تُحدَّث فقط داخل [afterNativeMutation]،
    // ويقرأها [onDraw] مباشرة بلا أي إعادة حساب (انظر التعليق داخل afterNativeMutation).
    private var cachedHighlightsByLine: Map<Int, List<RinNativeEditor.Highlight>> = emptyMap()
    private var cachedBracketInfo: Quad? = null

    private val shadowWatcher = object : TextWatcher {
        override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
        override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
            if (suppressForward || s == null) return
            routeShadowEdit(s, start, before, count)
        }
        override fun afterTextChanged(s: Editable?) {}
    }

    // --- مستمعو تغيّر النص الخارجيون (تطابقًا مع addTextChangedListener على EditText قديمًا) ---

    private val externalWatchers = mutableListOf<TextWatcher>()
    fun addTextChangedListener(watcher: TextWatcher) { externalWatchers.add(watcher) }
    fun removeTextChangedListener(watcher: TextWatcher) { externalWatchers.remove(watcher) }

    private fun notifyExternalWatchers(old: String, new: String) {
        for (w in externalWatchers) {
            w.beforeTextChanged(old, 0, old.length, new.length)
            w.onTextChanged(new, 0, old.length, new.length)
            w.afterTextChanged(shadowEditable)
        }
    }

    // --- تزامن ثنائي الاتجاه بين المحرك الأصلي وnative/shadow -----------------

    /** يُستدعى بعد أي عملية تُغيّر حالة المحرك مباشرة (لمس، مفاتيح، أوامر المتحكم...). */
    private fun afterNativeMutation() {
        val newText = engine.getText()
        val old = lastKnownText
        val changed = newText != old
        if (changed) {
            suppressForward = true
            shadowEditable.replace(0, shadowEditable.length, newText)
            suppressForward = false
            lastKnownText = newText
            // التلوين النحوي يستدعي rin::Lexer على كامل المستند (مكلف نسبيًا) — نُعيد حسابه
            // فقط عندما يتغيّر النص فعليًا، لا عند كل رسم (وميض المؤشر مثلاً يستدعي invalidate()
            // مرتين بالثانية بلا أي تغيير نصّي، فيقرأ القيمة المخزَّنة مباشرة بلا أي تكلفة).
            val byLine = HashMap<Int, MutableList<RinNativeEditor.Highlight>>()
            for (h in engine.getHighlights()) byLine.getOrPut(h.line) { mutableListOf() }.add(h)
            cachedHighlightsByLine = byLine
        }
        // مطابقة الأقواس تعتمد على موضع المؤشر (يتغيّر أيضًا بلا تعديل نصّي، كالأسهم)، فتُحسَب في
        // كل استدعاء لهذه الدالة (رخيصة الثمن)، لكن أبدًا داخل onDraw نفسها.
        cachedBracketInfo = computeBracketMatchForDraw()

        val cur = engine.getCursor()
        val flat = nativePosToFlatOffset(newText, cur.line, cur.col).coerceIn(0, shadowEditable.length)
        Selection.setSelection(shadowEditable, flat)
        if (changed) {
            notifyExternalWatchers(old, newText)
            updateSuggestionPopup()
        } else {
            dismissSuggestionPopup()
        }
        requestLayout()
        invalidate()
    }

    /** يوجّه تعديلاً وصل من IME (عبر [shadowWatcher]) إلى المحرك الأصلي بأذكى طريقة ممكنة. */
    private fun routeShadowEdit(s: CharSequence, start: Int, before: Int, count: Int) {
        val oldText = lastKnownText
        val insertedPiece = s.subSequence(start, start + count).toString()
        val cursorFlatBefore = nativeFlatCursor(oldText)

        if (before == 0 && count == 1 && start == cursorFlatBefore) {
            // كتابة حرف واحد عند المؤشر مباشرة: مرّرها عبر المسار "الذكي" (إغلاق أقواس/مسافة تلقائية)
            engine.insertText(insertedPiece, smart = true)
        } else if (before == 1 && count == 0 && start + before == cursorFlatBefore) {
            // حذف حرف واحد قبل المؤشر مباشرة: Backspace ذكي (يحذف زوج قوس تلقائي بالكامل عند اللزوم)
            engine.deleteBackward()
        } else if (before == 1 && count == 0 && start == cursorFlatBefore) {
            // حذف حرف واحد بعد المؤشر مباشرة: Delete للأمام
            engine.deleteForward()
        } else {
            // حالة عامة: لصق، إكمال تلقائي/تصحيح تلقائي من IME، أو استبدال تحديد
            val (l1, c1) = flatOffsetToNativePos(oldText, start)
            val (l2, c2) = flatOffsetToNativePos(oldText, start + before)
            engine.replaceRange(l1, c1, l2, c2, insertedPiece)
        }
        afterNativeMutation()
    }

    private fun nativeFlatCursor(text: String): Int {
        val cur = engine.getCursor()
        return nativePosToFlatOffset(text, cur.line, cur.col)
    }

    private fun nativePosToFlatOffset(text: String, line: Int, byteCol: Int): Int {
        val lines = text.split("\n")
        var offset = 0
        val clampedLine = line.coerceIn(0, max(0, lines.size - 1))
        for (i in 0 until clampedLine) offset += lines[i].length + 1
        val lineText = lines.getOrElse(clampedLine) { "" }
        offset += RinNativeEditor.byteOffsetToCharIndex(lineText, byteCol)
        return offset
    }

    private fun flatOffsetToNativePos(text: String, flatOffset: Int): Pair<Int, Int> {
        val lines = text.split("\n")
        var remaining = flatOffset.coerceIn(0, text.length)
        for ((i, l) in lines.withIndex()) {
            if (remaining <= l.length) {
                return i to RinNativeEditor.charIndexToByteOffset(l, remaining)
            }
            remaining -= (l.length + 1)
        }
        val lastIdx = max(0, lines.size - 1)
        val lastLine = lines.getOrElse(lastIdx) { "" }
        return lastIdx to RinNativeEditor.charIndexToByteOffset(lastLine, lastLine.length)
    }

    // --- واجهة عامة متوافقة مع الاستخدام القديم (android.widget.EditText-like) -------------

    /** نص المحرر الحالي (مطابق لـ `EditText.text.toString()` قديمًا). */
    val text: CharSequence get() = shadowEditable

    fun setText(newText: CharSequence) {
        engine.setText(newText.toString())
        afterNativeMutation()
    }

    fun selectAll() {
        engine.setSelection(0, 0, engine.lineCount() - 1, engine.getLine(engine.lineCount() - 1).toByteArray(Charsets.UTF_8).size)
        afterNativeMutation()
    }

    fun setTextSize(unit: Int, size: Float) {
        textPaint.textSize = TypedValue.applyDimension(unit, size, resources.displayMetrics)
        recomputeMetrics()
        requestLayout()
        invalidate()
    }

    /** بالبكسل، تطابقًا مع `EditText.textSize` قديمًا. */
    val textSize: Float get() = textPaint.textSize

    private fun spToPx(sp: Float): Float = TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, sp, resources.displayMetrics)

    // --- تهيئة ----------------------------------------------------------

    init {
        isFocusable = true
        isFocusableInTouchMode = true
        isClickable = true
        recomputeMetrics()
        lastKnownText = engine.getText()
        shadowEditable.replace(0, shadowEditable.length, lastKnownText)
        Selection.setSelection(shadowEditable, 0)
        // هذه هي الطريقة القياسية التي يسجّل بها Android نفسه TextWatcher على Editable
        // (انظر TextView.addTextChangedListener): الـwatcher يُرفَق كـ span عادي بامتداد
        // (0, length) وبعلامة SPAN_INCLUSIVE_INCLUSIVE، فيستدعيه SpannableStringBuilder
        // تلقائيًا عند أي replace() — بلا حاجة لأي آلية أخرى.
        shadowEditable.setSpan(shadowWatcher, 0, shadowEditable.length, Editable.SPAN_INCLUSIVE_INCLUSIVE)
        // تعبئة أولية للذاكرة المؤقتة (cache) قبل أول onDraw، حتى لا يُرسَم بلا تلوين للحظة.
        val byLine = HashMap<Int, MutableList<RinNativeEditor.Highlight>>()
        for (h in engine.getHighlights()) byLine.getOrPut(h.line) { mutableListOf() }.add(h)
        cachedHighlightsByLine = byLine
        cachedBracketInfo = computeBracketMatchForDraw()
    }

    // --- القياس والرسم ----------------------------------------------------

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        var maxLineWidth = 0f
        val lc = engine.lineCount()
        for (i in 0 until lc) {
            val w = textPaint.measureText(engine.getLine(i))
            if (w > maxLineWidth) maxLineWidth = w
        }
        val desiredWidth = (maxLineWidth + paddingLeft + paddingRight + charWidth).roundToInt()
        val desiredHeight = ((lc * lineHeight) + paddingTop + paddingBottom).roundToInt()
        setMeasuredDimension(
            resolveSizeAndState(desiredWidth, widthMeasureSpec, 0),
            resolveSizeAndState(desiredHeight, heightMeasureSpec, 0)
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val lc = engine.lineCount()
        val cur = engine.getCursor()
        val sel = engine.getSelection()
        val highlightsByLine = cachedHighlightsByLine
        val bracketInfo = cachedBracketInfo

        var y = paddingTop.toFloat()
        for (line in 0 until lc) {
            val lineText = engine.getLine(line)
            val baseline = y - ascent

            // تظليل السطر الحالي
            if (line == cur.line && !sel.hasSelection) {
                canvas.drawRect(0f, y, width.toFloat(), y + lineHeight, currentLinePaint)
            }

            // تظليل التحديد
            if (sel.hasSelection && line in sel.start.line..sel.end.line) {
                val fromCol = if (line == sel.start.line) RinNativeEditor.byteOffsetToCharIndex(lineText, sel.start.col) else 0
                val toCol = if (line == sel.end.line) RinNativeEditor.byteOffsetToCharIndex(lineText, sel.end.col) else lineText.length
                val x1 = paddingLeft + textPaint.measureText(lineText, 0, fromCol)
                val x2 = paddingLeft + (if (line == sel.end.line) textPaint.measureText(lineText, 0, toCol) else textPaint.measureText(lineText) + charWidth)
                canvas.drawRect(x1, y, max(x1, x2), y + lineHeight, selectionPaint)
            }

            // تظليل القوس المطابق/الخاطئ
            bracketInfo?.let { (aLine, aCharCol, bLineOrNeg, bCharCol, matched) ->
                if (line == aLine) {
                    val x1 = paddingLeft + textPaint.measureText(lineText, 0, aCharCol)
                    val x2 = paddingLeft + textPaint.measureText(lineText, 0, aCharCol + 1)
                    canvas.drawRect(x1, y, x2, y + lineHeight, if (matched) bracketMatchPaint else bracketErrorPaint)
                }
                if (matched && line == bLineOrNeg) {
                    val bLineText = engine.getLine(bLineOrNeg)
                    val x1 = paddingLeft + textPaint.measureText(bLineText, 0, bCharCol)
                    val x2 = paddingLeft + textPaint.measureText(bLineText, 0, bCharCol + 1)
                    canvas.drawRect(x1, y, x2, y + lineHeight, bracketMatchPaint)
                }
            }

            // تظليل تطابقات البحث
            findMatches?.let { matches ->
                for (m in matches) {
                    if (m.line != line) continue
                    val fromCol = RinNativeEditor.byteOffsetToCharIndex(lineText, m.startCol)
                    val toCol = RinNativeEditor.byteOffsetToCharIndex(lineText, m.endCol)
                    val x1 = paddingLeft + textPaint.measureText(lineText, 0, fromCol)
                    val x2 = paddingLeft + textPaint.measureText(lineText, 0, toCol)
                    canvas.drawRect(x1, y, x2, y + lineHeight, findMatchPaint)
                }
            }

            // النص الملوَّن نحويًا
            drawHighlightedLine(canvas, lineText, highlightsByLine[line], paddingLeft.toFloat(), baseline)

            // مؤشر الكتابة (يومض)
            if (line == cur.line && !sel.hasSelection && cursorVisible && hasFocus()) {
                val charCol = RinNativeEditor.byteOffsetToCharIndex(lineText, cur.col)
                val cx = paddingLeft + textPaint.measureText(lineText, 0, charCol)
                canvas.drawRect(cx, y, cx + max(2f, resources.displayMetrics.density * 1.5f), y + lineHeight, cursorPaint)
            }

            y += lineHeight
        }
    }

    private fun drawHighlightedLine(
        canvas: Canvas,
        lineText: String,
        spans: List<RinNativeEditor.Highlight>?,
        startX: Float,
        baseline: Float
    ) {
        if (lineText.isEmpty()) return
        if (spans.isNullOrEmpty()) {
            textPaint.color = colorDefault
            canvas.drawText(lineText, startX, baseline, textPaint)
            return
        }
        // رتّب الامتدادات وارسم كل جزء بلونه، وما بينها بلون افتراضي
        val sorted = spans.sortedBy { it.startCol }
        var cursorByte = 0
        var cursorX = startX
        for (h in sorted) {
            val fromChar = RinNativeEditor.byteOffsetToCharIndex(lineText, h.startCol)
            val toChar = RinNativeEditor.byteOffsetToCharIndex(lineText, h.endCol)
            val gapFromChar = RinNativeEditor.byteOffsetToCharIndex(lineText, cursorByte)
            if (fromChar > gapFromChar) {
                textPaint.color = colorDefault
                canvas.drawText(lineText, gapFromChar, fromChar, cursorX, baseline, textPaint)
                cursorX += textPaint.measureText(lineText, gapFromChar, fromChar)
            }
            if (toChar > fromChar) {
                textPaint.color = colorForKind(h.kind)
                canvas.drawText(lineText, fromChar, toChar, cursorX, baseline, textPaint)
                cursorX += textPaint.measureText(lineText, fromChar, toChar)
            }
            cursorByte = h.endCol
        }
        val tailFromChar = RinNativeEditor.byteOffsetToCharIndex(lineText, cursorByte)
        if (tailFromChar < lineText.length) {
            textPaint.color = colorDefault
            canvas.drawText(lineText, tailFromChar, lineText.length, cursorX, baseline, textPaint)
        }
    }

    private fun colorForKind(kind: Int): Int = when (kind) {
        1 -> colorKeyword   // HighlightKind::Keyword
        2 -> colorString    // HighlightKind::String
        3 -> colorNumber    // HighlightKind::Number
        7 -> colorAt        // HighlightKind::At
        8 -> colorError     // HighlightKind::Error
        9 -> colorCall      // HighlightKind::Call (معرِّف متبوع بقوس فتح — نمط استدعاء دالة)
        else -> colorDefault
    }

    /** يُرجع (سطر أ, عمود-حرف أ, سطر ب أو -1, عمود-حرف ب, هل تطابق) للرسم فقط، أو null إن لا قوس عند المؤشر. */
    private fun computeBracketMatchForDraw(): Quad? {
        val cur = engine.getCursor()
        if (engine.getSelection().hasSelection) return null
        val lineText = engine.getLine(cur.line)
        val charCol = RinNativeEditor.byteOffsetToCharIndex(lineText, cur.col)
        val before: Char? = if (charCol > 0) lineText[charCol - 1] else null
        val at: Char? = if (charCol < lineText.length) lineText[charCol] else null
        val opens = setOf('(', '[', '{')
        val closes = setOf(')', ']', '}')
        val (bracketChar, bracketCharCol) = when {
            before != null && (before in opens || before in closes) -> before to (charCol - 1)
            at != null && (at in opens || at in closes) -> at to charCol
            else -> return null
        }
        // ابحث في نص المستند الكامل عبر إحداثيات (سطر, عمود-حرف) باستخدام getLine لكل سطر
        val fullLines = (0 until engine.lineCount()).map { engine.getLine(it) }
        val target = findMatchingBracketAcrossLines(fullLines, cur.line, bracketCharCol, bracketChar, opens, closes)
        return if (target != null) Quad(cur.line, bracketCharCol, target.first, target.second, true)
        else Quad(cur.line, bracketCharCol, -1, -1, false)
    }

    private data class Quad(val a: Int, val b: Int, val c: Int, val d: Int, val matched: Boolean)

    private fun findMatchingBracketAcrossLines(
        lines: List<String>, line: Int, charCol: Int, bracket: Char,
        opens: Set<Char>, closes: Set<Char>
    ): Pair<Int, Int>? {
        val pairs = mapOf('(' to ')', '[' to ']', '{' to '}')
        val reversed = mapOf(')' to '(', ']' to '[', '}' to '{')
        if (bracket in opens) {
            var depth = 0
            var l = line; var c = charCol
            while (l < lines.size) {
                val text = lines[l]
                while (c < text.length) {
                    val ch = text[c]
                    if (ch == bracket) depth++
                    else if (ch == pairs[bracket]) { depth--; if (depth == 0) return l to c }
                    c++
                }
                l++; c = 0
            }
            return null
        } else {
            var depth = 0
            var l = line; var c = charCol
            while (l >= 0) {
                val text = lines[l]
                while (c >= 0) {
                    if (c < text.length) {
                        val ch = text[c]
                        if (ch == bracket) depth++
                        else if (ch == reversed[bracket]) { depth--; if (depth == 0) return l to c }
                    }
                    c--
                }
                l--; c = if (l >= 0) lines[l].length - 1 else -1
            }
            return null
        }
    }

    // --- وميض المؤشر ------------------------------------------------------

    private var cursorVisible = true
    private val blinkHandler = Handler(Looper.getMainLooper())
    private val blinkRunnable = object : Runnable {
        override fun run() {
            cursorVisible = !cursorVisible
            invalidate()
            blinkHandler.postDelayed(this, 500L)
        }
    }

    override fun onFocusChanged(gainFocus: Boolean, direction: Int, previouslyFocusedRect: android.graphics.Rect?) {
        super.onFocusChanged(gainFocus, direction, previouslyFocusedRect)
        blinkHandler.removeCallbacks(blinkRunnable)
        if (gainFocus) {
            cursorVisible = true
            blinkHandler.postDelayed(blinkRunnable, 500L)
        } else {
            dismissSuggestionPopup()
        }
        invalidate()
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        blinkHandler.removeCallbacks(blinkRunnable)
        dismissSuggestionPopup()
        engine.destroy()
    }

    override fun onCheckIsTextEditor(): Boolean = true

    // --- اللمس: وضع المؤشر بالنقر، السحب للتحديد، الضغط الطويل لتحديد كلمة ------------

    private fun offsetForTouch(x: Float, y: Float): RinNativeEditor.Pos {
        val line = (((y - paddingTop) / lineHeight).toInt()).coerceIn(0, max(0, engine.lineCount() - 1))
        val lineText = engine.getLine(line)
        val relX = x - paddingLeft
        var bestChar = 0
        var bestDist = Float.MAX_VALUE
        for (i in 0..lineText.length) {
            val w = textPaint.measureText(lineText, 0, i)
            val d = kotlin.math.abs(w - relX)
            if (d < bestDist) { bestDist = d; bestChar = i }
        }
        val byteCol = RinNativeEditor.charIndexToByteOffset(lineText, bestChar)
        return RinNativeEditor.Pos(line, byteCol)
    }

    private val gestureDetector = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onSingleTapUp(e: MotionEvent): Boolean {
            requestFocus()
            showKeyboard()
            val p = offsetForTouch(e.x, e.y)
            engine.setCursor(p.line, p.col, false)
            afterNativeMutation()
            return true
        }

        override fun onLongPress(e: MotionEvent) {
            requestFocus()
            showKeyboard()
            selectWordAt(e.x, e.y)
        }

        override fun onDoubleTap(e: MotionEvent): Boolean {
            selectWordAt(e.x, e.y)
            return true
        }
    })

    private var dragging = false

    override fun onTouchEvent(event: MotionEvent): Boolean {
        gestureDetector.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> { dragging = false; parent?.requestDisallowInterceptTouchEvent(true) }
            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount == 1) {
                    dragging = true
                    val p = offsetForTouch(event.x, event.y)
                    engine.setCursor(p.line, p.col, true)
                    afterNativeMutation()
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                dragging = false
                parent?.requestDisallowInterceptTouchEvent(false)
            }
        }
        return true
    }

    private fun selectWordAt(x: Float, y: Float) {
        val p = offsetForTouch(x, y)
        val lineText = engine.getLine(p.line)
        val charCol = RinNativeEditor.byteOffsetToCharIndex(lineText, p.col)
        fun isWordChar(c: Char) = c.isLetterOrDigit() || c == '_'
        var start = charCol
        var end = charCol
        while (start > 0 && isWordChar(lineText[start - 1])) start--
        while (end < lineText.length && isWordChar(lineText.getOrElse(end) { ' ' })) end++
        if (start == end) return
        engine.setSelection(p.line, RinNativeEditor.charIndexToByteOffset(lineText, start), p.line, RinNativeEditor.charIndexToByteOffset(lineText, end))
        afterNativeMutation()
    }

    private fun showKeyboard() {
        val imm = context.getSystemService(Context.INPUT_METHOD_SERVICE) as? android.view.inputmethod.InputMethodManager
        imm?.showSoftInput(this, android.view.inputmethod.InputMethodManager.SHOW_IMPLICIT)
    }

    // --- لوحة المفاتيح (IME) عبر InputConnection حقيقي ------------------------

    override fun onCreateInputConnection(outAttrs: EditorInfo): InputConnection {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT or
            InputType.TYPE_TEXT_FLAG_MULTI_LINE or
            InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI or EditorInfo.IME_ACTION_NONE
        outAttrs.initialSelStart = Selection.getSelectionStart(shadowEditable)
        outAttrs.initialSelEnd = Selection.getSelectionEnd(shadowEditable)
        return object : BaseInputConnection(this, true) {
            override fun getEditable(): Editable = shadowEditable
        }
    }

    // --- مفاتيح فعلية (لوحة مفاتيح خارجية): أسهم + Tab كإضافة للمسار الذكي عبر IME -----

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val cur = engine.getCursor()
        when (keyCode) {
            KeyEvent.KEYCODE_DPAD_LEFT -> {
                val lineText = engine.getLine(cur.line)
                val charCol = RinNativeEditor.byteOffsetToCharIndex(lineText, cur.col)
                if (charCol > 0) engine.setCursor(cur.line, RinNativeEditor.charIndexToByteOffset(lineText, charCol - 1), event.isShiftPressed)
                else if (cur.line > 0) { val prev = engine.getLine(cur.line - 1); engine.setCursor(cur.line - 1, prev.toByteArray(Charsets.UTF_8).size, event.isShiftPressed) }
                afterNativeMutation(); return true
            }
            KeyEvent.KEYCODE_DPAD_RIGHT -> {
                val lineText = engine.getLine(cur.line)
                val charCol = RinNativeEditor.byteOffsetToCharIndex(lineText, cur.col)
                if (charCol < lineText.length) engine.setCursor(cur.line, RinNativeEditor.charIndexToByteOffset(lineText, charCol + 1), event.isShiftPressed)
                else if (cur.line + 1 < engine.lineCount()) engine.setCursor(cur.line + 1, 0, event.isShiftPressed)
                afterNativeMutation(); return true
            }
            KeyEvent.KEYCODE_DPAD_UP -> { engine.setCursor(cur.line - 1, cur.col, event.isShiftPressed); afterNativeMutation(); return true }
            KeyEvent.KEYCODE_DPAD_DOWN -> { engine.setCursor(cur.line + 1, cur.col, event.isShiftPressed); afterNativeMutation(); return true }
            KeyEvent.KEYCODE_MOVE_HOME -> { engine.setCursor(cur.line, 0, event.isShiftPressed); afterNativeMutation(); return true }
            KeyEvent.KEYCODE_MOVE_END -> {
                val len = engine.getLine(cur.line).toByteArray(Charsets.UTF_8).size
                engine.setCursor(cur.line, len, event.isShiftPressed); afterNativeMutation(); return true
            }
            KeyEvent.KEYCODE_TAB -> {
                if (engine.getSelection().hasSelection) engine.indentSelection() else engine.insertText("    ", smart = false)
                afterNativeMutation(); return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    // --- دعم البحث (يُستدعى من RinCodeEditorController) -----------------------

    private var findMatches: List<RinNativeEditor.FindMatch>? = null
    fun setFindHighlights(matches: List<RinNativeEditor.FindMatch>?) { findMatches = matches; invalidate() }

    /** إحداثيات y (بالبكسل، ضمن هذا الـView) لبداية [line] — تُستخدم للتمرير الحالي إلى المؤشر. */
    fun yOfLine(line: Int): Int = (paddingTop + line * lineHeight).roundToInt()

    // --- عمليات تحرير عامة تُستدعى من [RinCodeEditorController] ------------------------
    // كل واحدة: تُغيّر حالة المحرك الأصلي مباشرة، ثم تُزامن الـshadow/الرسم عبر [afterNativeMutation].

    fun undo(): Boolean { val r = engine.undo() != null; afterNativeMutation(); return r }
    fun redo(): Boolean { val r = engine.redo() != null; afterNativeMutation(); return r }
    fun duplicateCurrentLine() { engine.duplicateCurrentLine(); afterNativeMutation() }
    fun deleteCurrentLine() { engine.deleteCurrentLine(); afterNativeMutation() }
    fun moveLineUp() { engine.moveLineUp(); afterNativeMutation() }
    fun moveLineDown() { engine.moveLineDown(); afterNativeMutation() }
    fun toggleLineComment() { engine.toggleLineComment(); afterNativeMutation() }
    fun indentSelection() { engine.indentSelection(); afterNativeMutation() }
    fun unindentSelection() { engine.unindentSelection(); afterNativeMutation() }
    fun checkBracketBalance(): Int = engine.checkBracketBalance()
    fun lineCount(): Int = engine.lineCount()

    /** يُدرج [textToInsert] عند المؤشر (مستبدلاً أي تحديد حالي) بلا سلوك "ذكي" (بلا إغلاق أقواس تلقائي). */
    fun insertAtCursor(textToInsert: String) {
        engine.insertText(textToInsert, smart = false)
        afterNativeMutation()
    }

    /** يحرّك المؤشر إلى بداية [oneBasedLine] (مُقيَّد ضمن الحدود) ويعيد y بالبكسل لأجل التمرير. */
    fun goToLine(oneBasedLine: Int): Int {
        val pos = engine.lineStartPosition(oneBasedLine)
        requestFocus()
        engine.setCursor(pos.line, pos.col, false)
        afterNativeMutation()
        return yOfLine(pos.line)
    }

    /** يحدّد التطابق التالي لـ[query] بعد المؤشر (بحث دائري)؛ يُرجع النجاح، ويعيد y بالبكسل عند النجاح. */
    fun findNext(query: String, caseSensitive: Boolean): Int? {
        if (query.isEmpty()) return null
        val matches = engine.findAll(query, caseSensitive)
        if (matches.isEmpty()) return null
        val cur = engine.getCursor()
        val next = matches.firstOrNull { it.line > cur.line || (it.line == cur.line && it.startCol >= cur.col) } ?: matches.first()
        engine.setSelection(next.line, next.startCol, next.line, next.endCol)
        requestFocus()
        afterNativeMutation()
        return yOfLine(next.line)
    }

    fun findPrevious(query: String, caseSensitive: Boolean): Int? {
        if (query.isEmpty()) return null
        val matches = engine.findAll(query, caseSensitive)
        if (matches.isEmpty()) return null
        val cur = engine.getCursor()
        val prev = matches.lastOrNull { it.line < cur.line || (it.line == cur.line && it.endCol <= cur.col) } ?: matches.last()
        engine.setSelection(prev.line, prev.startCol, prev.line, prev.endCol)
        requestFocus()
        afterNativeMutation()
        return yOfLine(prev.line)
    }

    /** يستبدل التحديد الحالي إن كان يطابق [query] بالضبط، ثم ينتقل للتطابق التالي. */
    fun replaceOne(query: String, replacement: String, caseSensitive: Boolean) {
        if (query.isEmpty()) return
        val sel = engine.getSelection()
        if (sel.hasSelection) {
            val selectedText = textOfSelection(sel)
            val matchesQuery = if (caseSensitive) selectedText == query else selectedText.equals(query, ignoreCase = true)
            if (matchesQuery) {
                engine.replaceRange(sel.start.line, sel.start.col, sel.end.line, sel.end.col, replacement)
                afterNativeMutation()
            }
        }
        findNext(query, caseSensitive)
    }

    /** يستبدل كل تطابقات [query] بـ[replacement]؛ يُرجع عدد الاستبدالات. */
    fun replaceAll(query: String, replacement: String, caseSensitive: Boolean): Int {
        if (query.isEmpty()) return 0
        val matches = engine.findAll(query, caseSensitive)
        // طبّق من الأخير للأول حتى لا تتغيّر إحداثيات التطابقات السابقة مع كل استبدال
        for (m in matches.asReversed()) {
            engine.replaceRange(m.line, m.startCol, m.line, m.endCol, replacement)
        }
        if (matches.isNotEmpty()) afterNativeMutation()
        return matches.size
    }

    /** فهرس (1-based) التطابق عند/بعد المؤشر، وإجمالي عدد التطابقات — لعرض "N/M". */
    fun matchInfo(query: String, caseSensitive: Boolean): Pair<Int, Int> {
        if (query.isEmpty()) return 0 to 0
        val matches = engine.findAll(query, caseSensitive)
        if (matches.isEmpty()) return 0 to 0
        val cur = engine.getCursor()
        val idx = matches.indexOfFirst { it.line > cur.line || (it.line == cur.line && it.startCol >= cur.col) }
            .let { if (it == -1) 0 else it }
        return (idx + 1) to matches.size
    }

    private fun textOfSelection(sel: RinNativeEditor.Selection): String {
        if (sel.start.line == sel.end.line) {
            val lineText = engine.getLine(sel.start.line)
            val fromChar = RinNativeEditor.byteOffsetToCharIndex(lineText, sel.start.col)
            val toChar = RinNativeEditor.byteOffsetToCharIndex(lineText, sel.end.col)
            return lineText.substring(fromChar.coerceIn(0, lineText.length), toChar.coerceIn(0, lineText.length))
        }
        val sb = StringBuilder()
        for (l in sel.start.line..sel.end.line) {
            val lineText = engine.getLine(l)
            when (l) {
                sel.start.line -> sb.append(lineText.substring(RinNativeEditor.byteOffsetToCharIndex(lineText, sel.start.col)))
                sel.end.line -> sb.append(lineText.substring(0, RinNativeEditor.byteOffsetToCharIndex(lineText, sel.end.col)))
                else -> sb.append(lineText)
            }
            if (l != sel.end.line) sb.append('\n')
        }
        return sb.toString()
    }

    // --- إكمال تلقائي (Autocomplete) — نافذة اقتراحات حقيقية مبنية على [RinNativeEditor.getSuggestions] ---

    private var suggestionPopup: PopupWindow? = null
    private val maxSuggestionRows = 6

    /** (بادئة الكلمة قبل المؤشر مباشرة، عمود-بايت بداية تلك الكلمة على سطر المؤشر) أو null إن لا كلمة جارية. */
    private fun currentWordPrefix(): Pair<String, Int>? {
        if (engine.getSelection().hasSelection) return null
        val cur = engine.getCursor()
        val lineText = engine.getLine(cur.line)
        val charCol = RinNativeEditor.byteOffsetToCharIndex(lineText, cur.col)
        fun isWordChar(c: Char) = c.isLetter() || c.isDigit() || c == '_'
        var start = charCol
        while (start > 0 && isWordChar(lineText[start - 1])) start--
        if (start == charCol) return null
        val prefix = lineText.substring(start, charCol)
        return prefix to RinNativeEditor.charIndexToByteOffset(lineText, start)
    }

    private fun updateSuggestionPopup() {
        if (!hasFocus() || !isAttachedToWindow) { dismissSuggestionPopup(); return }
        val (prefix, wordStartByteCol) = currentWordPrefix() ?: run { dismissSuggestionPopup(); return }
        val suggestions = engine.getSuggestions(prefix, maxSuggestionRows)
        if (suggestions.isEmpty()) { dismissSuggestionPopup(); return }
        showSuggestionPopup(suggestions, wordStartByteCol)
    }

    private fun buildSuggestionRow(text: String, onPick: () -> Unit): TextView = TextView(context).apply {
        this.text = text
        typeface = android.graphics.Typeface.MONOSPACE
        setTextColor(colorDefault)
        textSize = 14f
        val padH = (12 * resources.displayMetrics.density).toInt()
        val padV = (8 * resources.displayMetrics.density).toInt()
        setPadding(padH, padV, padH, padV)
        isClickable = true
        isFocusable = false
        setOnClickListener { onPick() }
    }

    private fun showSuggestionPopup(suggestions: List<String>, wordStartByteCol: Int) {
        val cur = engine.getCursor()
        val container = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(Color.parseColor("#2A2D31"))
                cornerRadius = 8 * resources.displayMetrics.density
                setStroke((resources.displayMetrics.density).toInt().coerceAtLeast(1), Color.parseColor("#454A50"))
            }
        }
        for (s in suggestions) {
            container.addView(buildSuggestionRow(s) { applySuggestion(s, wordStartByteCol) })
        }

        val popup = suggestionPopup ?: PopupWindow(
            container, LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
        ).apply {
            isOutsideTouchable = false
            isFocusable = false
            elevation = 12f
            suggestionPopup = this
        }
        popup.contentView = container

        val loc = IntArray(2)
        getLocationOnScreen(loc)
        val lineText = engine.getLine(cur.line)
        val charCol = RinNativeEditor.byteOffsetToCharIndex(lineText, wordStartByteCol)
        val cursorLocalX = paddingLeft + textPaint.measureText(lineText, 0, charCol)
        val cursorLocalY = yOfLine(cur.line)
        val screenX = (loc[0] + cursorLocalX).toInt()
        val screenY = loc[1] + cursorLocalY + lineHeight.roundToInt()

        if (popup.isShowing) {
            popup.update(screenX, screenY, -1, -1)
        } else {
            popup.showAtLocation(this, Gravity.NO_GRAVITY, screenX, screenY)
        }
    }

    private fun applySuggestion(suggestion: String, wordStartByteCol: Int) {
        val cur = engine.getCursor()
        engine.replaceRange(cur.line, wordStartByteCol, cur.line, cur.col, suggestion)
        dismissSuggestionPopup()
        afterNativeMutation()
    }

    private fun dismissSuggestionPopup() {
        suggestionPopup?.let { if (it.isShowing) it.dismiss() }
    }
}

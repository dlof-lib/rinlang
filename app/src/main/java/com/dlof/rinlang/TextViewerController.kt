package com.dlof.rinlang

import android.app.Activity
import android.content.Context
import android.text.Editable
import android.text.SpannableString
import android.text.Spanned
import android.text.TextWatcher
import android.text.style.BackgroundColorSpan
import android.text.style.ForegroundColorSpan
import android.util.TypedValue
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.File

/**
 * عارض الملفات النصّية داخل [MediaPreviewActivity]: عرض للقراءة بخط ثابت العرض، أرقام أسطر،
 * التفاف أسطر (تبديل)، تكبير/تصغير الخط بالقرص (يُحفظ)، وبحث داخل الملف مع تمييز النتائج والتنقل بينها.
 * يقرأ حتى 1.5 ميغابايت ويرفض الملفات الثنائية.
 */
class TextViewerController(private val a: Activity) {

    class Info(val lines: Int, val truncated: Boolean)

    companion object {
        private const val LIMIT = 1_500_000
        private const val MAX_NUMBERED_LINES = 20_000
        private const val MAX_MATCHES = 3000
        val WRAP_BY_DEFAULT = setOf("txt", "md", "markdown")
    }

    private val d = a.resources.displayMetrics.density
    private fun dp(v: Int) = (v * d).toInt()

    private val host: View = a.findViewById(R.id.layoutTextHost)
    private val scroll: ScrollView = a.findViewById(R.id.scrollText)
    private val hscroll: HorizontalScrollView = a.findViewById(R.id.hscrollText)
    private val nums: TextView = a.findViewById(R.id.txtLineNums)
    private val body: TextView = a.findViewById(R.id.txtBodyText)
    private val findBar: View = a.findViewById(R.id.layoutFind)
    private val edtFind: EditText = a.findViewById(R.id.edtFind)
    private val txtCount: TextView = a.findViewById(R.id.txtFindCount)

    private val prefs = a.getSharedPreferences("rin_text_viewer", Context.MODE_PRIVATE)
    private var sizeSp = prefs.getFloat("size", 13f)
    private var wrap = false
    private var lines = 0
    private var spannable: SpannableString? = null
    private val matches = ArrayList<Int>()
    private var matchLen = 0
    private var cur = -1
    private var strong: List<Any> = emptyList()

    private val scaleDetector = ScaleGestureDetector(a, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(det: ScaleGestureDetector): Boolean { setSize(sizeSp * det.scaleFactor); return true }
        override fun onScaleEnd(det: ScaleGestureDetector) { prefs.edit().putFloat("size", sizeSp).apply() }
    })

    val active: Boolean get() = host.visibility == View.VISIBLE
    val findVisible: Boolean get() = findBar.visibility == View.VISIBLE

    init {
        a.findViewById<View>(R.id.btnFindNext).setOnClickListener { goTo(cur + 1) }
        a.findViewById<View>(R.id.btnFindPrev).setOnClickListener { goTo(cur - 1) }
        a.findViewById<View>(R.id.btnFindClose).setOnClickListener { closeFind() }
        edtFind.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, st: Int, c: Int, af: Int) {}
            override fun onTextChanged(s: CharSequence?, st: Int, b: Int, c: Int) {}
            override fun afterTextChanged(s: Editable?) { runFind() }
        })
        edtFind.setOnEditorActionListener { _, id, _ ->
            if (id == EditorInfo.IME_ACTION_SEARCH) { goTo(cur + 1); true } else false
        }
        setSize(sizeSp)
    }

    /** يمرّر أحداث اللمس لكاشف القرص؛ يعيد true أثناء القرص لمنع تمرير النص. */
    fun dispatch(ev: MotionEvent): Boolean {
        if (!active) return false
        scaleDetector.onTouchEvent(ev)
        return scaleDetector.isInProgress
    }

    /** مسافات النوافذ (شريط الحالة/لوحة المفاتيح) كي لا يختفي النص تحت الشريط العلوي أو شريط البحث. */
    fun applyInsets(top: Int, bottom: Int) {
        host.setPadding(0, dp(78) + top, 0, 0)
        (findBar.layoutParams as android.widget.FrameLayout.LayoutParams).bottomMargin = dp(12) + bottom
        findBar.requestLayout()
    }

    /** يحمّل الملف؛ يعيد null إن كان ثنائياً أو تعذّرت القراءة. */
    fun load(file: File): Info? {
        val bytes = try {
            file.inputStream().buffered().use { it.readNBytes(LIMIT) }
        } catch (t: Throwable) { return null }
        for (i in 0 until minOf(bytes.size, 4096)) if (bytes[i].toInt() == 0) return null
        var text = String(bytes, Charsets.UTF_8)
        if (text.startsWith("\uFEFF")) text = text.substring(1)
        lines = if (text.isEmpty()) 0 else text.count { it == '\n' } + (if (text.endsWith("\n")) 0 else 1)

        spannable = SpannableString(text)
        body.setText(spannable, TextView.BufferType.SPANNABLE)
        nums.text = if (lines in 1..MAX_NUMBERED_LINES) (1..lines).joinToString("\n") else ""
        closeFind()
        wrap = file.extension.lowercase() in WRAP_BY_DEFAULT
        applyWrap()
        scroll.scrollTo(0, 0); hscroll.scrollTo(0, 0)
        host.visibility = View.VISIBLE
        return Info(lines, file.length() > LIMIT)
    }

    fun hide() { host.visibility = View.GONE; closeFind() }

    fun toggleWrap() { wrap = !wrap; applyWrap() }

    fun toggleFind() { if (findVisible) closeFind() else openFind() }

    private fun applyWrap() {
        hscroll.post {
            if (wrap) {
                nums.visibility = View.GONE
                body.layoutParams = LinearLayout.LayoutParams(maxOf(hscroll.width - dp(28), dp(200)), LinearLayout.LayoutParams.WRAP_CONTENT)
            } else {
                nums.visibility = if (lines in 1..MAX_NUMBERED_LINES) View.VISIBLE else View.GONE
                body.layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT)
            }
            body.requestLayout()
        }
    }

    private fun setSize(v: Float) {
        sizeSp = v.coerceIn(9f, 32f)
        body.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
        nums.setTextSize(TypedValue.COMPLEX_UNIT_SP, sizeSp)
    }

    private fun openFind() {
        findBar.visibility = View.VISIBLE
        edtFind.requestFocus()
        (a.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).showSoftInput(edtFind, InputMethodManager.SHOW_IMPLICIT)
    }

    private fun closeFind() {
        if (findBar.visibility != View.VISIBLE) return
        edtFind.setText("")
        findBar.visibility = View.GONE
        (a.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager).hideSoftInputFromWindow(edtFind.windowToken, 0)
    }

    private fun clearSpans() {
        val sp = spannable ?: return
        sp.getSpans(0, sp.length, BackgroundColorSpan::class.java).forEach { sp.removeSpan(it) }
        sp.getSpans(0, sp.length, ForegroundColorSpan::class.java).forEach { sp.removeSpan(it) }
        strong = emptyList()
    }

    private fun runFind() {
        val sp = spannable ?: return
        clearSpans()
        matches.clear(); cur = -1
        val q = edtFind.text.toString()
        matchLen = q.length
        if (q.isNotEmpty()) {
            val text = sp.toString()
            var i = text.indexOf(q, 0, ignoreCase = true)
            while (i >= 0 && matches.size < MAX_MATCHES) {
                matches += i
                sp.setSpan(BackgroundColorSpan(0x553DF59A), i, i + q.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                i = text.indexOf(q, i + q.length, ignoreCase = true)
            }
        }
        if (matches.isNotEmpty()) goTo(0)
        else txtCount.text = if (q.isEmpty()) "" else a.getString(R.string.media_no_match)
    }

    private fun goTo(i: Int) {
        val sp = spannable ?: return
        val n = matches.size
        if (n == 0) return
        cur = ((i % n) + n) % n
        strong.forEach { sp.removeSpan(it) }
        val start = matches[cur]
        val bg = BackgroundColorSpan(0xFF3DF59A.toInt())
        val fg = ForegroundColorSpan(0xFF00140B.toInt())
        sp.setSpan(bg, start, start + matchLen, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        sp.setSpan(fg, start, start + matchLen, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        strong = listOf(bg, fg)
        txtCount.text = "${cur + 1}/$n"
        body.layout?.let { l ->
            val line = l.getLineForOffset(start)
            scroll.smoothScrollTo(0, (dp(8) + l.getLineTop(line) - dp(160)).coerceAtLeast(0))
        }
        body.invalidate()
    }
}

package com.dlof.rinlang

import android.graphics.Color
import android.graphics.Rect
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.Spannable
import android.text.SpannableString
import android.text.SpannableStringBuilder
import android.text.TextUtils
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.PopupWindow
import android.widget.ScrollView
import android.widget.TextView
import kotlin.math.max
import kotlin.math.min

/**
 * واجهة IntelliSense: قائمة اقتراحات + لوحة توثيق + تلميح توقيع الدالة (Signature Help).
 *
 * - صفّ الاقتراح: شارة نوع ملوّنة · الاسم مع إبراز الحروف المطابقة · تفصيل خافت (نوع/معاملات).
 * - لوحة التوثيق أسفل القائمة تعرض توقيع وشرح العنصر المحدَّد (تعليقات `//` الخاصة بالمستخدم تظهر هنا).
 * - يُوضَع فوق/تحت سطر المؤشر حسب المساحة المرئية فعلاً (يراعي لوحة المفاتيح).
 * - نافذتان غير قابلتين للتركيز: لا تسرقان التركيز ولا لوحة المفاتيح من المحرر.
 *
 * لا يعرف شيئاً عن المحرر: يستقبل [Anchor] (إحداثيات الشاشة) وينادي [onAccept] عند الاختيار.
 */
class RinCompletionPopup(
    private val host: View,
    private val textColor: Int,
    private val accentColor: Int,
    private val kindColor: (Int) -> Int,
    private val onAccept: (RinNativeEditor.CompletionResult, RinNativeEditor.CompletionItem) -> Unit
) {
    /** موضع مرجعي على الشاشة: [x] بداية النص المُستبدَل، [lineTop]/[lineBottom] حدّا سطر المؤشر. */
    class Anchor(val x: Int, val lineTop: Int, val lineBottom: Int)

    private val density = host.resources.displayMetrics.density
    private fun dp(v: Float) = (v * density).toInt()

    private val dark: Boolean = Color.luminance(textColor) > 0.5f
    private val bg = if (dark) Color.parseColor("#252A30") else Color.parseColor("#FFFFFF")
    private val bgSelected = if (dark) Color.parseColor("#37404B") else Color.parseColor("#E6EEF9")
    private val stroke = if (dark) Color.parseColor("#4A525C") else Color.parseColor("#D0D7DE")
    private val muted = if (dark) Color.parseColor("#9AA4AF") else Color.parseColor("#667085")
    private val mono: Typeface = Typeface.MONOSPACE

    private var listPopup: PopupWindow? = null
    private var sigPopup: PopupWindow? = null
    private var result: RinNativeEditor.CompletionResult? = null
    private var rows: List<View> = emptyList()
    private var scroll: ScrollView? = null
    private var docView: TextView? = null
    private var selected = 0
    /** هل حرّك المستخدم التحديد بنفسه (أسهم)؟ Enter لا يقبل الاقتراح إلا بعدها (Tab يقبل دائماً). */
    var userNavigated = false
        private set
    private var listTop = Int.MAX_VALUE // أعلى حافة للقائمة (لتفادي تراكب تلميح التوقيع)

    val isShowing: Boolean get() = listPopup?.isShowing == true
    val isSignatureShowing: Boolean get() = sigPopup?.isShowing == true

    // ----------------------------------------------------------------------------------------
    // قائمة الاقتراحات
    // ----------------------------------------------------------------------------------------

    fun show(res: RinNativeEditor.CompletionResult, anchor: Anchor) {
        result = res
        selected = 0
        userNavigated = false

        val column = LinearLayout(host.context).apply {
            orientation = LinearLayout.VERTICAL
            layoutDirection = View.LAYOUT_DIRECTION_LTR // الكود دائماً LTR
            background = GradientDrawable().apply {
                setColor(bg); cornerRadius = dp(10f).toFloat(); setStroke(dp(1f).coerceAtLeast(1), stroke)
            }
            clipToOutline = true
        }
        val rowsBox = LinearLayout(host.context).apply { orientation = LinearLayout.VERTICAL }
        val built = ArrayList<View>(res.items.size)
        res.items.forEachIndexed { i, item ->
            val row = buildRow(item)
            row.setOnClickListener { selected = i; accept() }
            rowsBox.addView(row, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(34f)))
            built.add(row)
        }
        rows = built
        val sv = object : ScrollView(host.context) {
            private val cap = dp(34f) * 6 + dp(17f) // 6.5 صفوف: توحي بقابلية التمرير
            override fun onMeasure(w: Int, h: Int) {
                super.onMeasure(w, MeasureSpec.makeMeasureSpec(cap, MeasureSpec.AT_MOST))
            }
        }.apply { isVerticalScrollBarEnabled = true; overScrollMode = View.OVER_SCROLL_NEVER; addView(rowsBox) }
        scroll = sv
        column.addView(sv, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        val doc = TextView(host.context).apply {
            setTextColor(muted); setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setPadding(dp(12f), dp(8f), dp(12f), dp(8f))
            maxLines = 5; ellipsize = TextUtils.TruncateAt.END
            background = GradientDrawable().apply { setColor(if (dark) Color.parseColor("#1E2227") else Color.parseColor("#F4F6F8")) }
        }
        docView = doc
        column.addView(doc, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        updateSelectionUi()

        val popup = listPopup ?: PopupWindow(column, LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
            isOutsideTouchable = false; isFocusable = false; elevation = dp(8f).toFloat(); listPopup = this
        }
        popup.contentView = column

        val frame = visibleFrame()
        val maxW = min(dp(360f), frame.width() - dp(16f))
        column.measure(View.MeasureSpec.makeMeasureSpec(maxW, View.MeasureSpec.AT_MOST), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        val w = max(min(max(column.measuredWidth, dp(200f)), maxW), dp(160f))
        column.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        val h = column.measuredHeight

        val x = anchor.x.coerceIn(frame.left + dp(4f), max(frame.left + dp(4f), frame.right - w - dp(4f)))
        val below = anchor.lineBottom + dp(2f)
        val y = if (below + h <= frame.bottom) below else max(frame.top, anchor.lineTop - h - dp(2f))
        listTop = y
        if (popup.isShowing) popup.update(x, y, w, h) else {
            popup.width = w; popup.height = h
            popup.showAtLocation(host, Gravity.NO_GRAVITY, x, y)
        }
    }

    fun update(res: RinNativeEditor.CompletionResult, anchor: Anchor) = show(res, anchor)

    fun moveSelection(delta: Int) {
        val n = rows.size
        if (n == 0) return
        selected = ((selected + delta) % n + n) % n
        userNavigated = true
        updateSelectionUi()
        scroll?.let { sv -> rows.getOrNull(selected)?.let { r ->
            val top = r.top; val bottom = r.bottom
            if (top < sv.scrollY) sv.smoothScrollTo(0, top) else if (bottom > sv.scrollY + sv.height) sv.smoothScrollTo(0, bottom - sv.height)
        } }
    }

    fun accept(): Boolean {
        val res = result ?: return false
        val item = res.items.getOrNull(selected) ?: return false
        dismiss()
        onAccept(res, item)
        return true
    }

    fun dismiss() {
        listPopup?.let { if (it.isShowing) it.dismiss() }
        result = null; rows = emptyList(); listTop = Int.MAX_VALUE
    }

    private fun updateSelectionUi() {
        rows.forEachIndexed { i, r -> r.setBackgroundColor(if (i == selected) bgSelected else Color.TRANSPARENT) }
        val item = result?.items?.getOrNull(selected)
        val d = docView ?: return
        if (item == null || (item.doc.isEmpty() && item.detail.isEmpty())) { d.visibility = View.GONE; return }
        d.visibility = View.VISIBLE
        val sb = SpannableStringBuilder()
        val head = item.label + (if (item.detail.isNotEmpty() && item.detail.startsWith("(")) item.detail else "")
        sb.append(head)
        sb.setSpan(StyleSpan(Typeface.BOLD), 0, sb.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        sb.setSpan(ForegroundColorSpan(textColor), 0, sb.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (item.doc.isNotEmpty()) sb.append("\n").append(item.doc)
        d.text = sb
        d.typeface = Typeface.DEFAULT
    }

    private fun buildRow(item: RinNativeEditor.CompletionItem): View {
        val ctx = host.context
        val row = LinearLayout(ctx).apply {
            orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(8f), 0, dp(10f), 0); isClickable = true; isFocusable = false
        }
        val color = kindColor(item.kind)
        val badge = TextView(ctx).apply {
            text = kindLetter(item.kind); gravity = Gravity.CENTER
            setTextColor(if (Color.luminance(color) > 0.55f) Color.parseColor("#1B1F23") else Color.WHITE); typeface = Typeface.DEFAULT_BOLD; setTextSize(TypedValue.COMPLEX_UNIT_SP, 11f)
            background = GradientDrawable().apply { setColor(color); cornerRadius = dp(5f).toFloat() }
        }
        row.addView(badge, LinearLayout.LayoutParams(dp(22f), dp(22f)).apply { marginEnd = dp(8f) })

        val label = TextView(ctx).apply {
            typeface = mono; textSize = 14f; setTextColor(textColor); maxLines = 1; ellipsize = TextUtils.TruncateAt.MIDDLE
            val s = SpannableString(item.label)
            for (p in item.matchPos) if (p in item.label.indices) {
                s.setSpan(ForegroundColorSpan(accentColor), p, p + 1, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                s.setSpan(StyleSpan(Typeface.BOLD), p, p + 1, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            text = s
        }
        row.addView(label, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
        if (item.detail.isNotEmpty()) {
            val detail = TextView(ctx).apply {
                typeface = mono; setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f); setTextColor(muted)
                maxLines = 1; ellipsize = TextUtils.TruncateAt.END; text = item.detail; maxWidth = dp(150f)
            }
            row.addView(detail, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { marginStart = dp(10f) })
        }
        return row
    }

    // ----------------------------------------------------------------------------------------
    // Signature help
    // ----------------------------------------------------------------------------------------

    fun showSignature(info: RinNativeEditor.SignatureInfo, anchor: Anchor) {
        val sb = SpannableStringBuilder()
        sb.append(info.name).append('(')
        val nameEnd = info.name.length
        sb.setSpan(ForegroundColorSpan(kindColor(RinNativeEditor.CompletionKind.FUNCTION)), 0, nameEnd, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        info.params.forEachIndexed { i, p ->
            if (i > 0) sb.append(", ")
            val s = sb.length
            sb.append(p)
            if (i == info.active) {
                sb.setSpan(StyleSpan(Typeface.BOLD), s, sb.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
                sb.setSpan(ForegroundColorSpan(accentColor), s, sb.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            } else {
                sb.setSpan(ForegroundColorSpan(textColor), s, sb.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }
        sb.append(')')
        if (info.ret.isNotEmpty()) {
            val s = sb.length
            sb.append(" → ").append(info.ret)
            sb.setSpan(ForegroundColorSpan(muted), s, sb.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        if (info.doc.isNotEmpty()) {
            val s = sb.length
            sb.append('\n').append(info.doc.lineSequence().first())
            sb.setSpan(ForegroundColorSpan(muted), s, sb.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        val tv = TextView(host.context).apply {
            layoutDirection = View.LAYOUT_DIRECTION_LTR; textDirection = View.TEXT_DIRECTION_LTR
            typeface = mono; setTextSize(TypedValue.COMPLEX_UNIT_SP, 12.5f); text = sb
            setPadding(dp(12f), dp(7f), dp(12f), dp(7f)); maxLines = 3; ellipsize = TextUtils.TruncateAt.END
            background = GradientDrawable().apply { setColor(bg); cornerRadius = dp(8f).toFloat(); setStroke(dp(1f).coerceAtLeast(1), stroke) }
        }
        val popup = sigPopup ?: PopupWindow(tv, LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply {
            isOutsideTouchable = false; isFocusable = false; elevation = dp(6f).toFloat(); sigPopup = this
        }
        popup.contentView = tv
        val frame = visibleFrame()
        val maxW = frame.width() - dp(16f)
        tv.measure(View.MeasureSpec.makeMeasureSpec(maxW, View.MeasureSpec.AT_MOST), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
        val w = min(tv.measuredWidth, maxW); val h = tv.measuredHeight
        val x = anchor.x.coerceIn(frame.left + dp(4f), max(frame.left + dp(4f), frame.right - w - dp(4f)))
        // فوق السطر؛ وإن كانت القائمة فوقه أيضاً فنضعه فوق القائمة؛ وإن لم تتّسع المساحة فتحت السطر
        val ceiling = min(anchor.lineTop, listTop)
        val above = ceiling - h - dp(4f)
        val y = if (above >= frame.top) above else anchor.lineBottom + dp(4f)
        if (popup.isShowing) popup.update(x, y, w, h) else {
            popup.width = w; popup.height = h
            popup.showAtLocation(host, Gravity.NO_GRAVITY, x, y)
        }
    }

    fun dismissSignature() { sigPopup?.let { if (it.isShowing) it.dismiss() } }

    fun dismissAll() { dismiss(); dismissSignature() }

    // ----------------------------------------------------------------------------------------

    private fun visibleFrame(): Rect {
        val r = Rect()
        host.getWindowVisibleDisplayFrame(r)
        if (r.width() <= 0 || r.height() <= 0) {
            val dm = host.resources.displayMetrics
            r.set(0, 0, dm.widthPixels, dm.heightPixels)
        }
        return r
    }

    private fun kindLetter(kind: Int): String = when (kind) {
        RinNativeEditor.CompletionKind.KEYWORD -> "K"
        RinNativeEditor.CompletionKind.FUNCTION -> "ƒ"
        RinNativeEditor.CompletionKind.METHOD -> "m"
        RinNativeEditor.CompletionKind.VARIABLE -> "v"
        RinNativeEditor.CompletionKind.CONSTANT -> "c"
        RinNativeEditor.CompletionKind.CLASS -> "C"
        RinNativeEditor.CompletionKind.SNIPPET -> "⌘"
        RinNativeEditor.CompletionKind.NAMESPACE -> "N"
        RinNativeEditor.CompletionKind.PROPERTY -> "p"
        RinNativeEditor.CompletionKind.TEMPLATE -> "@"
        RinNativeEditor.CompletionKind.LABEL -> "L"
        RinNativeEditor.CompletionKind.PARAMETER -> "a"
        RinNativeEditor.CompletionKind.TAG -> "@"
        RinNativeEditor.CompletionKind.MODULE -> "M"
        RinNativeEditor.CompletionKind.ENUM -> "E"
        RinNativeEditor.CompletionKind.ENUM_MEMBER -> "e"
        RinNativeEditor.CompletionKind.INTERFACE -> "I"
        else -> "w"
    }
}

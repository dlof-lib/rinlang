package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.text.TextPaint
import android.text.TextUtils
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.View
import org.json.JSONObject
import kotlin.math.max
import kotlin.math.min

/**
 * Draws a Loomtime Fabric tree (the JSON [RinEngine.renderView] / [RinEngine.LoomSession] return —
 * see `loom::fabricToJsonString` on the native side) directly onto a [Canvas]. This is the real
 * pixel-accurate preview surface: every rectangle/text run drawn here comes straight from the
 * geometry & resolved attributes the native Loom layout engine computed, not a simulation.
 *
 * Coordinate space: the Fabric's x/y/w/h are in the same "root px" space [RinEngine.renderView]
 * was asked to lay out at (see [rootWidthPx]). This view maps that space onto itself at [zoom],
 * with 1 root-px == 1dp at zoom = 1 so a 390-wide Fabric roughly fills a 390dp-wide phone.
 */
class LoomFabricView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    /** Node kinds, mirroring `loom::StrandKind` (rin_loom_strand.h) exactly. */
    private object Kind {
        const val TEXT = "Text"; const val IMAGE = "Image"; const val BUTTON = "Button"
        const val CARD = "Card"; const val COLUMN = "Column"; const val ROW = "Row"
        const val STACK = "Stack"; const val DIVIDER = "Divider"
    }

    // ---- default palette — must match loom::colorForKind() in rin_loom_paint.h exactly ----
    private val defaultCard = Color.rgb(40, 42, 54)
    private val defaultButton = Color.rgb(124, 92, 255)
    private val defaultText = Color.rgb(230, 230, 240)
    private val defaultImage = Color.rgb(70, 70, 90)
    private val defaultDivider = Color.rgb(51, 51, 63)
    private val defaultContainer = Color.rgb(24, 25, 32) // Column/Row/Stack/Custom root fallback

    var rootWidthPx: Int = 390
    var rootHeightPx: Int = 640
        private set

    var zoom: Float = 1f
        set(value) {
            field = value.coerceIn(0.25f, 3f)
            invalidate()
        }

    var showGrid: Boolean = false
        set(value) { field = value; invalidate() }

    var showSafeArea: Boolean = false
        set(value) { field = value; invalidate() }

    /** The Fabric root node (the object under the top-level `"fabric"` key), or null while empty/erroring. */
    private var fabric: JSONObject? = null

    /** Fired with root-px coordinates on a single tap — forwarded straight to [LoomPreviewManager.tap]. */
    var onTap: ((x: Double, y: Double) -> Unit)? = null

    /** Fired on long-press with the deepest Fabric node under the finger (or null if none) — Inspector. */
    var onInspect: ((node: JSONObject?) -> Unit)? = null

    private val density = resources.displayMetrics.density

    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.2f
    }
    private val textPaint = TextPaint(Paint.ANTI_ALIAS_FLAG)
    private val gridPaint = Paint().apply { color = Color.argb(28, 255, 255, 255); strokeWidth = 1f }
    private val safeAreaPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.5f
        color = Color.argb(140, 124, 92, 255)
        pathEffect = android.graphics.DashPathEffect(floatArrayOf(8f, 6f), 0f)
    }
    private val inspectHighlightPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2.5f
        color = Color.rgb(255, 196, 77)
    }

    /** Node currently highlighted by the Inspector (long-press), drawn on top after the tree. */
    private var inspectedNode: JSONObject? = null

    init {
        // setShadowLayer() (used for Card `shadow=` attrs) requires a software layer.
        setLayerType(LAYER_TYPE_SOFTWARE, null)
    }

    private val gestureDetector = GestureDetector(context, object : GestureDetector.SimpleOnGestureListener() {
        override fun onSingleTapUp(e: MotionEvent): Boolean {
            val (rx, ry) = viewToRoot(e.x, e.y)
            onTap?.invoke(rx, ry)
            return true
        }

        override fun onLongPress(e: MotionEvent) {
            val (rx, ry) = viewToRoot(e.x, e.y)
            val hit = fabric?.let { hitTest(it, rx.toFloat(), ry.toFloat()) }
            inspectedNode = hit
            onInspect?.invoke(hit)
            invalidate()
        }
    })

    override fun onTouchEvent(event: MotionEvent): Boolean {
        gestureDetector.onTouchEvent(event)
        return true
    }

    private fun viewToRoot(vx: Float, vy: Float): Pair<Double, Double> {
        val scale = density * zoom
        return (vx / scale).toDouble() to (vy / scale).toDouble()
    }

    /** Clears the Inspector highlight (call when the info panel is dismissed). */
    fun clearInspection() {
        inspectedNode = null
        invalidate()
    }

    /**
     * Replaces the drawn Fabric with [node] (pass null to show an empty canvas — e.g. while the
     * very first render is pending). [rootW]/[rootH] size the view's intrinsic content bounds.
     */
    fun setFabric(node: JSONObject?, rootW: Int, rootH: Int) {
        fabric = node
        rootWidthPx = max(1, rootW)
        rootHeightPx = max(1, rootH)
        requestLayout()
        invalidate()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val scale = density * zoom
        val w = (rootWidthPx * scale).toInt()
        val h = (rootHeightPx * scale).toInt()
        setMeasuredDimension(max(w, suggestedMinimumWidth), max(h, suggestedMinimumHeight))
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.save()
        canvas.scale(density * zoom, density * zoom)

        val root = fabric
        if (root != null) {
            drawNode(canvas, root)
        }

        if (showGrid) drawGrid(canvas)
        if (showSafeArea) drawSafeArea(canvas)
        inspectedNode?.let { drawInspectHighlight(canvas, it) }

        canvas.restore()
    }

    // ---- tree walk ----

    private fun drawNode(canvas: Canvas, node: JSONObject) {
        val kind = node.optString("kind")
        val x = node.optDouble("x", 0.0).toFloat()
        val y = node.optDouble("y", 0.0).toFloat()
        val w = node.optDouble("w", 0.0).toFloat()
        val h = node.optDouble("h", 0.0).toFloat()
        val attrs = node.optJSONObject("attrs") ?: JSONObject()
        val rect = RectF(x, y, x + w, y + h)

        when (kind) {
            Kind.TEXT -> drawText(canvas, rect, attrs, attrs.optString("text"), defaultText)
            Kind.DIVIDER -> drawDivider(canvas, rect, attrs)
            Kind.IMAGE -> drawImagePlaceholder(canvas, rect, attrs)
            Kind.BUTTON -> {
                drawBox(canvas, rect, attrs, defaultButton, defaultRadius = 10f)
                drawText(canvas, rect, attrs, attrs.optString("label"), Color.WHITE, centered = true, boldHint = true, singleLine = true)
            }
            Kind.CARD -> drawBox(canvas, rect, attrs, defaultCard, defaultRadius = 14f)
            else -> drawBox(canvas, rect, attrs, defaultContainer, defaultRadius = 0f) // Column/Row/Stack/Custom
        }

        val children = node.optJSONArray("children")
        if (children != null) {
            for (i in 0 until children.length()) {
                drawNode(canvas, children.optJSONObject(i) ?: continue)
            }
        }
    }

    private fun drawBox(canvas: Canvas, rect: RectF, attrs: JSONObject, fallback: Int, defaultRadius: Float) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        val radius = attrs.optString("radius").toFloatOrNull()?.times(density) ?: (defaultRadius * density)
        val shadow = attrs.optString("shadow").toFloatOrNull()
        val gradient = attrs.optString("gradient").takeIf { it.contains(',') }

        fillPaint.shader = null
        fillPaint.clearShadowLayer()

        if (gradient != null) {
            val parts = gradient.split(',')
            val c1 = parseHexColor(parts.getOrNull(0)?.trim(), fallback)
            val c2 = parseHexColor(parts.getOrNull(1)?.trim(), fallback)
            fillPaint.shader = LinearGradient(rect.left, rect.top, rect.left, rect.bottom, c1, c2, Shader.TileMode.CLAMP)
        } else {
            fillPaint.color = parseHexColor(attrs.optString("color").ifBlank { null }, fallback)
        }

        if (shadow != null && shadow > 0f) {
            fillPaint.setShadowLayer(shadow, 0f, shadow / 2.5f, Color.argb(120, 0, 0, 0))
        }

        canvas.drawRoundRect(rect, radius, radius, fillPaint)

        // border= / borderColor=: a real stroked edge, inset by half its own width so it's drawn
        // fully inside the box's bounds (matches the border-box inset the native layout already
        // reserved for children — see loom::layoutSingleChildBox / layoutLinear).
        val borderWidthPx = attrs.optString("border").toFloatOrNull()
        if (borderWidthPx != null && borderWidthPx > 0f && rect.width() > 1f && rect.height() > 1f) {
            val strokeW = borderWidthPx * density
            strokePaint.color = parseHexColor(attrs.optString("borderColor").ifBlank { null }, Color.WHITE)
            strokePaint.strokeWidth = strokeW
            val inset = strokeW / 2f
            val strokeRect = RectF(rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset)
            val strokeRadius = max(0f, radius - inset)
            canvas.drawRoundRect(strokeRect, strokeRadius, strokeRadius, strokePaint)
        }
    }

    /**
     * Greedy word-wrap using REAL measured glyph widths (mirrors `loom::wrapText` in
     * rin_loom_layout.h, which only has a rough per-codepoint estimate to size the box before
     * paint). Matching algorithms means the line count — and therefore the box height the native
     * layout already committed to — lines up with what actually gets drawn here.
     */
    private fun wrapLines(text: String, paint: TextPaint, maxWidth: Float): List<String> {
        if (text.isEmpty()) return listOf("")
        if (maxWidth <= 0f) return listOf(text)
        val words = text.split(" ").filter { it.isNotEmpty() }
        if (words.isEmpty()) return listOf("")
        val lines = mutableListOf<String>()
        var line = ""
        for (w in words) {
            val candidate = if (line.isEmpty()) w else "$line $w"
            line = if (line.isEmpty() || paint.measureText(candidate) <= maxWidth) candidate
            else { lines.add(line); w }
        }
        if (line.isNotEmpty() || lines.isEmpty()) lines.add(line)
        return lines
    }

    private fun drawText(
        canvas: Canvas, rect: RectF, attrs: JSONObject, text: String,
        fallbackColor: Int, centered: Boolean = false, boldHint: Boolean = false, singleLine: Boolean = false
    ) {
        if (text.isEmpty() || rect.width() <= 0f || rect.height() <= 0f) return
        val sizeSp = attrs.optString("size").toFloatOrNull() ?: 14f
        textPaint.color = parseHexColor(attrs.optString("color").ifBlank { null }, fallbackColor)
        textPaint.textSize = sizeSp * density
        textPaint.isFakeBoldText = boldHint
        textPaint.isAntiAlias = true

        val hPad = 4f
        val available = max(4f, rect.width() - hPad * 2f)

        if (singleLine) {
            val truncated = TextUtils.ellipsize(text, textPaint, available, TextUtils.TruncateAt.END)
            val textWidth = textPaint.measureText(truncated, 0, truncated.length)
            val startX = if (centered) rect.left + (rect.width() - textWidth) / 2f else rect.left + hPad
            val baseline = rect.top + rect.height() / 2f - (textPaint.descent() + textPaint.ascent()) / 2f
            canvas.drawText(truncated, 0, truncated.length, startX, baseline, textPaint)
            return
        }

        // Real multi-line body text: wrap, then draw one line per row filling the box top-down —
        // the box's height was sized by loom::measureText for exactly this many lines * lineHeight,
        // so no vertical centering of the whole block is needed (it already fills the box).
        val lineHeight = textPaint.textSize * 1.4f
        var maxLines = max(1, (rect.height() / lineHeight).toInt())
        attrs.optString("maxLines").toIntOrNull()?.let { if (it > 0) maxLines = min(maxLines, it) }

        var lines = wrapLines(text, textPaint, available)
        val overflowed = lines.size > maxLines
        if (overflowed) lines = lines.subList(0, maxLines)

        var baseline = rect.top - textPaint.ascent()
        for ((i, rawLine) in lines.withIndex()) {
            val isLastVisible = i == lines.lastIndex
            val line = if (overflowed && isLastVisible)
                TextUtils.ellipsize(rawLine, textPaint, available, TextUtils.TruncateAt.END).toString()
            else rawLine
            val lineWidth = textPaint.measureText(line)
            val startX = if (centered) rect.left + (rect.width() - lineWidth) / 2f else rect.left + hPad
            canvas.drawText(line, startX, baseline, textPaint)
            baseline += lineHeight
        }
    }

    private fun drawDivider(canvas: Canvas, rect: RectF, attrs: JSONObject) {
        fillPaint.shader = null
        fillPaint.clearShadowLayer()
        fillPaint.color = parseHexColor(attrs.optString("color").ifBlank { null }, defaultDivider)
        canvas.drawRect(rect, fillPaint)
    }

    private fun drawImagePlaceholder(canvas: Canvas, rect: RectF, attrs: JSONObject) {
        if (rect.width() <= 0f || rect.height() <= 0f) return
        fillPaint.shader = null
        fillPaint.clearShadowLayer()
        fillPaint.color = defaultImage
        val radius = 8f * density
        canvas.drawRoundRect(rect, radius, radius, fillPaint)

        // simple "picture" glyph (mountain + dot) so an <Image> reads as an image, not a blank card
        strokePaint.color = Color.argb(160, 255, 255, 255)
        val pad = min(rect.width(), rect.height()) * 0.22f
        val glyph = RectF(rect.left + pad, rect.top + pad, rect.right - pad, rect.bottom - pad)
        if (glyph.width() > 2f && glyph.height() > 2f) {
            canvas.drawCircle(glyph.left + glyph.width() * 0.22f, glyph.top + glyph.height() * 0.28f, glyph.width() * 0.09f, strokePaint)
            val path = android.graphics.Path()
            path.moveTo(glyph.left, glyph.bottom)
            path.lineTo(glyph.left + glyph.width() * 0.38f, glyph.top + glyph.height() * 0.4f)
            path.lineTo(glyph.left + glyph.width() * 0.62f, glyph.bottom - glyph.height() * 0.2f)
            path.lineTo(glyph.left + glyph.width() * 0.8f, glyph.top + glyph.height() * 0.55f)
            path.lineTo(glyph.right, glyph.bottom)
            canvas.drawPath(path, strokePaint)
        }

        val label = attrs.optString("src").ifBlank { null }
        if (label != null) drawText(canvas, RectF(rect.left, rect.bottom - 16f * density, rect.right, rect.bottom), attrs, label, Color.argb(210, 255, 255, 255), singleLine = true)
    }

    private fun drawGrid(canvas: Canvas) {
        val step = 8f // 8dp baseline grid, standard mobile design unit
        var gx = 0f
        while (gx <= rootWidthPx) { canvas.drawLine(gx, 0f, gx, rootHeightPx.toFloat(), gridPaint); gx += step }
        var gy = 0f
        while (gy <= rootHeightPx) { canvas.drawLine(0f, gy, rootWidthPx.toFloat(), gy, gridPaint); gy += step }
    }

    private fun drawSafeArea(canvas: Canvas) {
        val margin = 16f
        canvas.drawRect(margin, margin, rootWidthPx - margin, rootHeightPx - margin, safeAreaPaint)
    }

    private fun drawInspectHighlight(canvas: Canvas, node: JSONObject) {
        val x = node.optDouble("x", 0.0).toFloat()
        val y = node.optDouble("y", 0.0).toFloat()
        val w = node.optDouble("w", 0.0).toFloat()
        val h = node.optDouble("h", 0.0).toFloat()
        if (w <= 0f || h <= 0f) return
        canvas.drawRect(x, y, x + w, y + h, inspectHighlightPaint)
    }

    // ---- hit test (client-side; independent of the native tap-dispatch used for onTap handlers) ----

    private fun hitTest(node: JSONObject, x: Float, y: Float): JSONObject? {
        val nx = node.optDouble("x", 0.0).toFloat()
        val ny = node.optDouble("y", 0.0).toFloat()
        val nw = node.optDouble("w", 0.0).toFloat()
        val nh = node.optDouble("h", 0.0).toFloat()
        if (x < nx || y < ny || x > nx + nw || y > ny + nh) return null

        val children = node.optJSONArray("children")
        if (children != null) {
            for (i in children.length() - 1 downTo 0) {
                val child = children.optJSONObject(i) ?: continue
                val hit = hitTest(child, x, y)
                if (hit != null) return hit
            }
        }
        return node
    }

    private fun parseHexColor(hex: String?, fallback: Int): Int {
        if (hex.isNullOrBlank() || hex.length < 7 || hex[0] != '#') return fallback
        return try { Color.parseColor(hex) } catch (t: Throwable) { fallback }
    }
}

package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.*
import android.view.View
import android.view.animation.LinearInterpolator
import kotlin.math.min

/**
 * RIN logo-as-progress renderer.
 * The progress indicator is not a bitmap: the official geometric mark is rebuilt from paths
 * and revealed segment-by-segment, so the logo itself becomes the loading bar.
 */
class RinLogoLoadingView(context: Context) : View(context) {
    private val logoPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.SQUARE
        strokeJoin = Paint.Join.MITER
        color = Color.WHITE
        strokeWidth = 7f
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        color = Color.rgb(100, 220, 120)
        strokeWidth = 12f
        maskFilter = BlurMaskFilter(16f, BlurMaskFilter.Blur.NORMAL)
    }
    private val accentPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        color = Color.rgb(112, 230, 130)
        strokeWidth = 4f
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
        color = Color.WHITE
        textSize = 18f
        letterSpacing = 0.08f
    }
    private val percentPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
        color = Color.rgb(112, 230, 130)
        textSize = 28f
    }

    private var progress = 0f
    private var animator: ValueAnimator? = null
    private val logoPath = Path()

    init {
        setLayerType(View.LAYER_TYPE_SOFTWARE, null)
        isClickable = false
    }

    fun setProgress(value: Float) {
        progress = value.coerceIn(0f, 1f)
        invalidate()
    }

    fun start(durationMs: Long = 1200L) {
        animator?.cancel()
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = durationMs
            interpolator = LinearInterpolator()
            addUpdateListener { setProgress(it.animatedValue as Float) }
            start()
        }
    }

    fun stop() {
        animator?.cancel()
        animator = null
        setProgress(1f)
    }

    override fun onDetachedFromWindow() {
        animator?.cancel()
        animator = null
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = width / 2f
        val cy = height * 0.42f
        val size = min(width, height) * 0.25f
        val scale = size / 260f

        // Logo coordinates follow the supplied RIN mark: broken outer diamond/hexagon + inner diamond.
        logoPath.reset()
        fun p(x: Float, y: Float) = PointF(cx + x * scale, cy + y * scale)
        val pts = listOf(
            p(-160f, -20f), p(-80f, -100f), p(80f, -100f), p(160f, -20f),
            p(80f, -20f), p(0f, -100f), p(-80f, -20f),
            p(-160f, 20f), p(-80f, 100f), p(80f, 100f), p(160f, 20f),
            p(80f, 20f), p(0f, 100f), p(-80f, 20f),
            p(0f, -58f), p(58f, 0f), p(0f, 58f), p(-58f, 0f), p(0f, -58f)
        )
        logoPath.moveTo(pts[0].x, pts[0].y)
        for (i in 1 until pts.size) logoPath.lineTo(pts[i].x, pts[i].y)

        // Subtle track: the logo is the loading bar; no static bitmap is required.
        val bounds = RectF(cx - 185f * scale, cy - 125f * scale, cx + 185f * scale, cy + 125f * scale)
        val track = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = 2f
            color = Color.argb(65, 255, 255, 255)
        }
        canvas.drawRoundRect(bounds, 18f, 18f, track)

        val measure = PathMeasure(logoPath, false)
        val total = measure.length
        val visible = total * progress
        val reveal = Path()
        measure.getSegment(0f, visible, reveal, true)
        canvas.drawPath(reveal, glowPaint)
        canvas.drawPath(reveal, logoPaint)

        // A moving light point marks the current redraw head.
        if (progress < 1f && total > 0f) {
            val pos = FloatArray(2)
            measure.getPosTan(visible.coerceAtMost(total - 0.1f), pos, null)
            canvas.drawCircle(pos[0], pos[1], 7f, accentPaint)
        }

        canvas.drawText("RIN", cx, cy + size * 0.78f, textPaint)
        canvas.drawText("${(progress * 100f).toInt()}%", cx, cy + size * 1.02f, percentPaint)

        // The percentage is also represented as a slim horizontal fill, keeping the same identity
        // when the editor is in a compact loading state.
        val barY = cy + size * 1.20f
        val left = cx - size * 1.65f
        val right = cx + size * 1.65f
        val bar = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = 4f
            strokeCap = Paint.Cap.ROUND
            color = Color.argb(80, 255, 255, 255)
        }
        val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = 4f
            strokeCap = Paint.Cap.ROUND
            color = Color.rgb(112, 230, 130)
        }
        canvas.drawLine(left, barY, right, barY, bar)
        canvas.drawLine(left, barY, left + (right - left) * progress, barY, fill)
    }
}

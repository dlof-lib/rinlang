package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.BlurMaskFilter
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PathMeasure
import android.graphics.PointF
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import android.view.animation.LinearInterpolator
import androidx.core.content.ContextCompat
import kotlin.math.min

/**
 * مؤشر تحميل يرسم علامة RIN الهندسية تدريجياً كأنها تُخَط بقلم — نفس فكرة [RinLogoLoadingView]
 * المستخدم داخل المحرر، لكن كمكوّن View عام قابل لإعادة الاستخدام في أي شاشة (وليس مرتبطاً بتراكب
 * كامل الشاشة أو بألوان/نصوص المحرر الثابتة). مناسب كبديل مميّز بصرياً لـ[RinSpinner] حين يُراد طابع
 * "هوية العلامة التجارية" بدل قوس مجرّد.
 *
 * XML: <com.dlof.rinlang.RinIconDrawLoadingView
 *          android:layout_width="72dp" android:layout_height="72dp" />
 * برمجياً: يبدأ تلقائياً مع attach، أو يدوياً عبر [start]/[stop].
 */
class RinIconDrawLoadingView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.SQUARE
        strokeJoin = Paint.Join.MITER
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        maskFilter = BlurMaskFilter(14f, BlurMaskFilter.Blur.NORMAL)
    }
    private val headPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }
    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 2f
    }

    private val logoPath = Path()
    private var progress = 0f
    private var animator: ValueAnimator? = null

    private val accentColor = ContextCompat.getColor(context, R.color.rin_accent)
    private val gradientEnd = ContextCompat.getColor(context, R.color.rin_run_gradient_end)

    init {
        setLayerType(LAYER_TYPE_SOFTWARE, null)
        isClickable = false
        strokePaint.color = accentColor
        glowPaint.color = accentColor
        headPaint.color = gradientEnd
        trackPaint.color = Color.argb(50, Color.red(accentColor), Color.green(accentColor), Color.blue(accentColor))
    }

    fun setProgress(value: Float) {
        progress = value.coerceIn(0f, 1f)
        invalidate()
    }

    fun start(durationMs: Long = 1300L) {
        animator?.cancel()
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = durationMs
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener { setProgress(it.animatedValue as Float) }
            start()
        }
    }

    /** يوقف الحلقة اللانهائية ويثبّت الشعار مكتملاً (للاستخدام كمؤشر نجاح لحظي قبل الإخفاء). */
    fun stop() {
        animator?.cancel()
        animator = null
        setProgress(1f)
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        start()
    }

    override fun onDetachedFromWindow() {
        animator?.cancel()
        animator = null
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = width / 2f
        val cy = height / 2f
        val size = min(width, height) * 0.42f
        val scale = size / 160f

        strokePaint.strokeWidth = 7f * scale
        glowPaint.strokeWidth = 11f * scale

        fun p(x: Float, y: Float) = PointF(cx + x * scale, cy + y * scale)
        // نفس هندسة علامة RIN (معين خارجي مكسور + معين داخلي)، بمقياس نسبي لحجم الـView.
        val pts = listOf(
            p(-160f, -20f), p(-80f, -100f), p(80f, -100f), p(160f, -20f),
            p(80f, -20f), p(0f, -100f), p(-80f, -20f),
            p(-160f, 20f), p(-80f, 100f), p(80f, 100f), p(160f, 20f),
            p(80f, 20f), p(0f, 100f), p(-80f, 20f),
            p(0f, -58f), p(58f, 0f), p(0f, 58f), p(-58f, 0f), p(0f, -58f)
        )
        logoPath.reset()
        logoPath.moveTo(pts[0].x, pts[0].y)
        for (i in 1 until pts.size) logoPath.lineTo(pts[i].x, pts[i].y)

        val trackBounds = RectF(cx - 185f * scale, cy - 125f * scale, cx + 185f * scale, cy + 125f * scale)
        canvas.drawRoundRect(trackBounds, 18f * scale, 18f * scale, trackPaint)

        val measure = PathMeasure(logoPath, false)
        val total = measure.length
        val visible = total * progress
        val reveal = Path()
        measure.getSegment(0f, visible, reveal, true)
        canvas.drawPath(reveal, glowPaint)
        canvas.drawPath(reveal, strokePaint)

        if (progress < 1f && total > 0f) {
            val pos = FloatArray(2)
            measure.getPosTan(visible.coerceAtMost(total - 0.1f), pos, null)
            canvas.drawCircle(pos[0], pos[1], 6f * scale, headPaint)
        }
    }
}

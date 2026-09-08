package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PathMeasure
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import android.view.animation.LinearInterpolator
import androidx.core.content.ContextCompat

/**
 * مؤشر تحميل على شكل "دودة" (Snake): سلسلة نقاط تتبع رأساً متحركاً حول مسار كبسولي مغلق،
 * كل نقطة أصغر وأخفت من سابقتها كلما ابتعدت عن الرأس — بالضبط كذيل ثعبان لعبة Snake الكلاسيكية.
 *
 * إضافة إلى عائلة مؤشرات RIN: [RinSpinner] (قوس دوّار صغير)، [RinProgressBar] (شريط خطي محدَّد/
 * غير محدَّد النسبة)، [RinIconDrawLoadingView] (رسم شعار التطبيق تدريجياً). هذا النوع مناسب لشاشات
 * الانتظار المتوسطة (لا زر صغير ولا تغطية كاملة) حيث يُراد طابع مرح ومميّز بصرياً.
 *
 * XML: <com.dlof.rinlang.RinSnakeLoadingView
 *          android:layout_width="120dp" android:layout_height="48dp"
 *          app:rinSnakeSegments="8" app:rinSnakeDotRadius="5dp" />
 */
class RinSnakeLoadingView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val dotPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val trackPath = Path()
    private val trackRect = RectF()
    private var pathMeasure = PathMeasure()
    private var totalLength = 0f

    private val headColor: Int
    private val tailColor: Int
    private val segmentCount: Int
    private val dotRadiusPx: Float

    private var headDistance = 0f
    private var animator: ValueAnimator? = null

    init {
        val a = context.obtainStyledAttributes(attrs, R.styleable.RinSnakeLoadingView)
        segmentCount = a.getInt(R.styleable.RinSnakeLoadingView_rinSnakeSegments, 8)
            .coerceIn(3, 24)
        dotRadiusPx = a.getDimension(
            R.styleable.RinSnakeLoadingView_rinSnakeDotRadius,
            resources.displayMetrics.density * 5f
        )
        a.recycle()

        headColor = ContextCompat.getColor(context, R.color.rin_accent_green)
        tailColor = ContextCompat.getColor(context, R.color.rin_run_gradient_start)
    }

    fun start() {
        if (animator?.isRunning == true) return
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = LOOP_DURATION_MS
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener {
                if (totalLength > 0f) {
                    headDistance = (it.animatedValue as Float) * totalLength
                    invalidate()
                }
            }
            start()
        }
    }

    fun stop() {
        animator?.cancel()
        animator = null
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        start()
    }

    override fun onDetachedFromWindow() {
        stop()
        super.onDetachedFromWindow()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        val inset = dotRadiusPx + 1f
        trackRect.set(inset, inset, w - inset, h - inset)
        val radius = trackRect.height() / 2f
        trackPath.reset()
        if (trackRect.width() > 0f && trackRect.height() > 0f) {
            trackPath.addRoundRect(trackRect, radius, radius, Path.Direction.CW)
            pathMeasure = PathMeasure(trackPath, true)
            totalLength = pathMeasure.length
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (totalLength <= 0f) return

        val spacing = totalLength / (segmentCount * 2.2f)
        val pos = FloatArray(2)

        for (i in segmentCount - 1 downTo 0) {
            var d = headDistance - i * spacing
            if (d < 0f) d += totalLength
            pathMeasure.getPosTan(d, pos, null)

            val fraction = i.toFloat() / (segmentCount - 1).coerceAtLeast(1)
            dotPaint.color = lerpColor(headColor, tailColor, fraction)
            dotPaint.alpha = (255 * (1f - fraction * 0.75f)).toInt()
            val radius = dotRadiusPx * (1f - fraction * 0.45f)

            canvas.drawCircle(pos[0], pos[1], radius, dotPaint)
        }
    }

    private fun lerpColor(start: Int, end: Int, fraction: Float): Int {
        val f = fraction.coerceIn(0f, 1f)
        val r = (Color.red(start) + (Color.red(end) - Color.red(start)) * f).toInt()
        val g = (Color.green(start) + (Color.green(end) - Color.green(start)) * f).toInt()
        val b = (Color.blue(start) + (Color.blue(end) - Color.blue(start)) * f).toInt()
        return Color.rgb(r, g, b)
    }

    companion object {
        private const val LOOP_DURATION_MS = 1400L
    }
}

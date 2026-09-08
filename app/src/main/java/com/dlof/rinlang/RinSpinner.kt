package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.SweepGradient
import android.util.AttributeSet
import android.view.View
import android.view.animation.LinearInterpolator
import androidx.core.content.ContextCompat
import kotlin.math.min

/**
 * مؤشر تحميل دائري صغير قابل للتضمين داخل أي شاشة (خلافاً لـ[RinLoadingView]/[RinLogoLoadingOverlay]
 * اللذين يغطّيان الشاشة كاملة، أو [RinProgressBar] الخطي المخصَّص لتقدّم محدَّد النسبة كالتنزيلات).
 *
 * الاستخدام المقصود: بجانب زر قيد التنفيذ، داخل عنصر قائمة يُحمَّل، ضمن بطاقة، أو أي منطقة صغيرة
 * تحتاج إشارة "جارٍ العمل" فورية بلا تغطية الشاشة.
 *
 * مبني بالكامل بـCanvas (بلا مكتبة خارجية كـLottie) حفاظاً على حجم APK وضمان تطابق الهوية اللونية
 * تماماً مع بقية مكوّنات التحميل (تدرّج rin_run_gradient نفسه المستخدم في [RinProgressBar]):
 *   • قوس دوّار بتدرّج لوني (SweepGradient) بدل الدائرة الرمادية المسطّحة القياسية.
 *   • "تنفّس" في طول القوس (يتمدّد وينكمش بلا توقف) بدل قوس ثابت الطول يدور فقط — تفصيل صغير
 *     يمنح إحساساً حيّاً مشابهاً لمؤشرات التحميل في التطبيقات الاحترافية الحديثة.
 *   • رأس القوس الأمامي مضاء بنقطة توهّج تتبعه أثناء الدوران.
 *
 * XML: <com.dlof.rinlang.RinSpinner
 *          android:layout_width="24dp" android:layout_height="24dp"
 *          app:rinSpinnerStrokeWidth="3dp" />
 * برمجياً: يبدأ/يتوقف تلقائياً مع attach/detach، أو يدوياً عبر [start]/[stop].
 */
class RinSpinner @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val arcPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.FILL
    }

    private val gradientStart: Int
    private val gradientEnd: Int
    private var strokeWidthPx: Float

    private val bounds = RectF()
    private var rotationDeg = 0f
    private var sweepDeg = MIN_SWEEP

    private var rotateAnimator: ValueAnimator? = null
    private var breatheAnimator: ValueAnimator? = null

    init {
        val a = context.obtainStyledAttributes(attrs, R.styleable.RinSpinner)
        strokeWidthPx = a.getDimension(
            R.styleable.RinSpinner_rinSpinnerStrokeWidth,
            resources.displayMetrics.density * 3f
        )
        val customColor = a.getColor(R.styleable.RinSpinner_rinSpinnerColor, 0)
        a.recycle()

        gradientStart = if (customColor != 0) customColor else
            ContextCompat.getColor(context, R.color.rin_run_gradient_start)
        gradientEnd = if (customColor != 0) customColor else
            ContextCompat.getColor(context, R.color.rin_run_gradient_end)

        arcPaint.strokeWidth = strokeWidthPx
        glowPaint.color = gradientStart
        glowPaint.alpha = 90
    }

    // --- واجهة عامة ---------------------------------------------------------

    fun start() {
        if (rotateAnimator?.isRunning == true) return
        rotateAnimator = ValueAnimator.ofFloat(0f, 360f).apply {
            duration = ROTATE_DURATION_MS
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener {
                rotationDeg = it.animatedValue as Float
                invalidate()
            }
            start()
        }
        breatheAnimator = ValueAnimator.ofFloat(MIN_SWEEP, MAX_SWEEP).apply {
            duration = BREATHE_DURATION_MS
            repeatMode = ValueAnimator.REVERSE
            repeatCount = ValueAnimator.INFINITE
            interpolator = android.view.animation.AccelerateDecelerateInterpolator()
            addUpdateListener {
                sweepDeg = it.animatedValue as Float
                invalidate()
            }
            start()
        }
    }

    fun stop() {
        rotateAnimator?.cancel()
        breatheAnimator?.cancel()
        rotateAnimator = null
        breatheAnimator = null
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        start()
    }

    override fun onDetachedFromWindow() {
        stop()
        super.onDetachedFromWindow()
    }

    // --- رسم -----------------------------------------------------------------

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        val inset = strokeWidthPx / 2f + 1f
        val size = min(w, h).toFloat()
        val left = (w - size) / 2f + inset
        val top = (h - size) / 2f + inset
        bounds.set(left, top, left + size - inset * 2f, top + size - inset * 2f)
        if (bounds.width() > 0f) {
            arcPaint.shader = SweepGradient(
                bounds.centerX(), bounds.centerY(),
                intArrayOf(gradientEnd, gradientStart, gradientStart),
                floatArrayOf(0f, 0.85f, 1f)
            )
        }
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (bounds.width() <= 0f) return

        val save = canvas.save()
        canvas.rotate(rotationDeg, bounds.centerX(), bounds.centerY())
        canvas.drawArc(bounds, 0f, sweepDeg, false, arcPaint)

        // نقطة توهّج عند رأس القوس الأمامي تتبع الدوران، لإحساس "طاقة" إضافي بلا مبالغة.
        val headAngleRad = Math.toRadians(sweepDeg.toDouble())
        val radius = bounds.width() / 2f
        val headX = bounds.centerX() + radius * Math.cos(headAngleRad).toFloat()
        val headY = bounds.centerY() + radius * Math.sin(headAngleRad).toFloat()
        canvas.drawCircle(headX, headY, strokeWidthPx * 0.9f, glowPaint)
        canvas.restoreToCount(save)
    }

    companion object {
        private const val ROTATE_DURATION_MS = 950L
        private const val BREATHE_DURATION_MS = 700L
        private const val MIN_SWEEP = 40f
        private const val MAX_SWEEP = 300f
    }
}

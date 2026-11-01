package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
import android.view.animation.AccelerateDecelerateInterpolator
import android.view.animation.DecelerateInterpolator
import androidx.core.content.ContextCompat
import kotlin.math.max
import kotlin.math.min

/**
 * شريط تحميل واحد موحَّد لكل التطبيق — مبني بالكامل بـCanvas بدل تركيب View/View لكل من
 * "المسار" و"التعبئة" (كما كان سابقاً في RinLoadingView)، أو الاعتماد على ProgressBar النظامية
 * القياسية بمظهرها الرمادي المسطّح الافتراضي (كما كان في RinDownloadProgressDialog).
 *
 * يدعم وضعين:
 *   • مُحدَّد (determinate): عبر [setProgress] — تعبئة متدرّجة اللون (هوية rin_run_gradient
 *     البنفسجية) بحواف كبسولية دائماً، مع بريق (shimmer) لامع يمرّ فوقها باستمرار بلا توقف —
 *     تفصيل بصري صغير يمنح إحساساً "حياً" حتى عند تقدّم بطيء أو ثابت مؤقتاً.
 *   • غير مُحدَّد (indeterminate) عبر [setIndeterminate](true): شريحة متحركة ذهاباً وإياباً
 *     على طول المسار (أسلوب Material الكلاسيكي) لأي عملية بلا نسبة تقدّم معروفة مسبقاً.
 *
 * الاستخدام المقصود: يستبدل هذا الصنف كل شريط تقدّم قديم في التطبيق (شاشة التحميل العامة
 * RinLoadingView، حوار تنزيل الحزم RinDownloadProgressDialog، وأي مكان مستقبلي) بمكوّن واحد
 * بمظهر وسلوك متطابقين تماماً أينما ظهر.
 */
class RinProgressBar @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.rin_editor_surface_border)
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val shimmerPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = ContextCompat.getColor(context, R.color.rin_accent)
        alpha = 46
    }

    private val gradientStart = ContextCompat.getColor(context, R.color.rin_run_gradient_start)
    private val gradientEnd = ContextCompat.getColor(context, R.color.rin_run_gradient_end)

    /** 0f..1f، أو -1f قبل أول [setProgress] فعلي (لا شيء يُرسم كتعبئة إلا في وضع اللانهائي). */
    private var displayedFraction = 0f
    private var isIndeterminate = false

    private var fractionAnimator: ValueAnimator? = null
    private var shimmerAnimator: ValueAnimator? = null
    private var indeterminateAnimator: ValueAnimator? = null

    private var shimmerPhase = 0f
    private var indeterminatePhase = 0f

    private val trackRect = RectF()
    private val fillRect = RectF()

    init {
        // البريق يبدأ فوراً ويستمر طوال عمر الـView (رخيص الثمن: مجرد تحريك RectF/Shader وInvalidate،
        // لا قياس/تخطيط جديد في كل إطار)، بصرف النظر عن كون الشريط في وضع محدَّد أو لا.
        startShimmerLoop()
    }

    // --- واجهة عامة --------------------------------------------------------

    /** يضبط الشريط في وضع محدَّد بنسبة [percent] (0..100)، متحركاً بسلاسة من قيمته الحالية. */
    fun setProgress(percent: Int, animate: Boolean = true) {
        if (isIndeterminate) setIndeterminate(false)
        val target = (percent.coerceIn(0, 100)) / 100f
        if (!animate) {
            fractionAnimator?.cancel()
            displayedFraction = target
            invalidate()
            return
        }
        fractionAnimator?.cancel()
        fractionAnimator = ValueAnimator.ofFloat(displayedFraction, target).apply {
            duration = 260L
            interpolator = DecelerateInterpolator()
            addUpdateListener {
                displayedFraction = it.animatedValue as Float
                invalidate()
            }
            start()
        }
    }

    /** يبدّل وضع اللانهائي (لعملية بلا نسبة تقدّم معروفة) — شريحة متحركة ذهاباً وإياباً. */
    fun setIndeterminate(enabled: Boolean) {
        if (isIndeterminate == enabled) return
        isIndeterminate = enabled
        if (enabled) {
            fractionAnimator?.cancel()
            indeterminateAnimator?.cancel()
            indeterminateAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
                duration = 1100L
                repeatMode = ValueAnimator.REVERSE
                repeatCount = ValueAnimator.INFINITE
                interpolator = AccelerateDecelerateInterpolator()
                addUpdateListener {
                    indeterminatePhase = it.animatedValue as Float
                    invalidate()
                }
                start()
            }
        } else {
            indeterminateAnimator?.cancel()
            indeterminateAnimator = null
            invalidate()
        }
    }

    /** يوقف كل الرسوم المتحركة (استدعِها عند إخفاء المضيف نهائياً لتوفير البطارية/المعالج). */
    fun stopAnimating() {
        fractionAnimator?.cancel()
        shimmerAnimator?.cancel()
        indeterminateAnimator?.cancel()
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        stopAnimating()
    }

    private fun startShimmerLoop() {
        shimmerAnimator?.cancel()
        shimmerAnimator = ValueAnimator.ofFloat(-0.35f, 1.35f).apply {
            duration = 1600L
            repeatCount = ValueAnimator.INFINITE
            interpolator = android.view.animation.LinearInterpolator()
            addUpdateListener {
                shimmerPhase = it.animatedValue as Float
                invalidate()
            }
            start()
        }
    }

    // --- رسم -----------------------------------------------------------

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        trackRect.set(0f, 0f, w.toFloat(), h.toFloat())
        rebuildGradientShader(w.toFloat())
    }

    private fun rebuildGradientShader(width: Float) {
        if (width <= 0f) return
        fillPaint.shader = LinearGradient(
            0f, 0f, width, 0f,
            gradientStart, gradientEnd,
            Shader.TileMode.CLAMP
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return
        val radius = h / 2f

        // المسار (الخلفية الفارغة) — كبسولة كاملة العرض دائماً.
        canvas.drawRoundRect(trackRect, radius, radius, trackPaint)

        if (isIndeterminate) {
            drawIndeterminateSegment(canvas, w, h, radius)
        } else if (displayedFraction > 0f) {
            drawDeterminateFill(canvas, w, h, radius)
        }
    }

    private fun drawDeterminateFill(canvas: Canvas, w: Float, h: Float, radius: Float) {
        val fillWidth = max(h, w * displayedFraction) // لا يقل عرض التعبئة عن الارتفاع، حتى تبقى كبسولة كاملة لا نصف-دائرة عند نسب صغيرة جداً
        fillRect.set(0f, 0f, min(fillWidth, w), h)

        // توهّج خفيف خلف طرف التعبئة يمنحها إحساس "طاقة" بدل تعبئة مسطّحة بحتة.
        canvas.drawRoundRect(fillRect, radius, radius, glowPaint)
        canvas.drawRoundRect(fillRect, radius, radius, fillPaint)
        drawShimmerClippedTo(canvas, fillRect, radius)
    }

    private fun drawIndeterminateSegment(canvas: Canvas, w: Float, h: Float, radius: Float) {
        val segmentWidth = max(h * 2.2f, w * 0.28f)
        val travel = (w - segmentWidth).coerceAtLeast(0f)
        val left = travel * indeterminatePhase
        fillRect.set(left, 0f, left + segmentWidth, h)
        canvas.drawRoundRect(fillRect, radius, radius, fillPaint)
        drawShimmerClippedTo(canvas, fillRect, radius)
    }

    /** يرسم بريقاً لامعاً يعبر أفقياً فوق [region] فقط (مقصوص عليها)، بلا تجاوز حدود التعبئة. */
    private fun drawShimmerClippedTo(canvas: Canvas, region: RectF, radius: Float) {
        if (region.width() <= 0f) return
        val bandWidth = region.width() * 0.5f
        val centerX = region.left + shimmerPhase * region.width()
        shimmerPaint.shader = LinearGradient(
            centerX - bandWidth, 0f, centerX + bandWidth, 0f,
            intArrayOf(0x00FFFFFF, 0x40FFFFFF, 0x00FFFFFF),
            floatArrayOf(0f, 0.5f, 1f),
            Shader.TileMode.CLAMP
        )
        val save = canvas.save()
        canvas.clipRect(region)
        canvas.drawRoundRect(region, radius, radius, shimmerPaint)
        canvas.restoreToCount(save)
    }
}

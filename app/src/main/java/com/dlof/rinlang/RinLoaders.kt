package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.os.SystemClock
import android.provider.Settings
import android.util.AttributeSet
import android.view.View
import android.animation.ValueAnimator
import android.view.animation.LinearInterpolator
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/**
 * عائلة مؤشرات التحميل الرسمية الإضافية لـ RinStudio (أخضر · أبيض · أسود)، كلها Canvas بلا صور:
 *
 *  • [RinBubblesLoadingView]  ثلاث فقاعات تقفز بموجة — للأزرار والسطور القصيرة.
 *  • [RinRipplesLoadingView]  تموّجات خضراء تتّسع من نقطة — للانتظار الهادئ/البحث.
 *  • [RinCodeLoadingView]     رمز </> تتنفّس أقواسه حول الشرطة — للتجميع/التشغيل.
 *  • [RinWaveBarsLoadingView] أعمدة (مجسّات) تتموّج بتدرّج أبيض←أخضر — للمعالجة.
 *  • [RinOrbitLoadingView]    نقاط تدور بذيل متلاشٍ — مؤشر دائري مميّز.
 *  • [RinJellyBarView]        شريط تقدّم يسبح عليه قنديل صغير — محدّد وغير محدّد.
 *
 * مع [RinLoaderKind] و[RinLoaders.create] لإنشاء أي نوع برمجياً، ومع المؤشرات الموجودة:
 * RinJellyLoaderView, RinIconDrawLoadingView, RinSpinner, RinSnakeLoadingView.
 * كلها تبدأ مع attach وتتوقف مع detach، وتحترم "مقياس مدة الحركة = 0".
 * على خلفية فاتحة اضبط [RinLoopView.inkColor] على لون داكن (الأجزاء البيضاء).
 */
abstract class RinLoopView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    protected val density = resources.displayMetrics.density
    protected val reducedMotion: Boolean = Settings.Global.getFloat(
        context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f
    ) == 0f

    /** مدة الدورة الواحدة بالمللي ثانية. */
    open var periodMs: Long = 1200L

    /** لون الأجزاء "البيضاء" (أبيض للخلفيات الداكنة). */
    var inkColor: Int = RinBrand.WHITE
        set(value) { field = value; invalidate() }

    private var animator: ValueAnimator? = null
    private var startUptime = 0L
    private var elapsedMs = 0L

    fun start() {
        if (reducedMotion) { invalidate(); return }
        if (animator?.isRunning == true) return
        startUptime = SystemClock.uptimeMillis() - elapsedMs
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 1000L
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener { elapsedMs = SystemClock.uptimeMillis() - startUptime; invalidate() }
            start()
        }
    }

    fun stop() { animator?.cancel(); animator = null }

    override fun onAttachedToWindow() { super.onAttachedToWindow(); start() }
    override fun onDetachedFromWindow() { stop(); super.onDetachedFromWindow() }
    override fun onVisibilityChanged(changedView: View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        if (changedView !== this || !isAttachedToWindow) return
        if (visibility == VISIBLE) start() else stop()
    }

    final override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat(); val h = height.toFloat()
        if (w <= 0f || h <= 0f) return
        // الحركة معطّلة: إطار ثابت جميل بدل البداية الفارغة.
        val cycle = if (reducedMotion) 0.25f else elapsedMs / periodMs.toFloat()
        drawLoop(canvas, w, h, cycle)
    }

    /** [cycle] يزداد باستمرار؛ جزؤه الكسري = موضع الدورة 0..1. */
    protected abstract fun drawLoop(canvas: Canvas, w: Float, h: Float, cycle: Float)

    protected fun frac(x: Float) = x - Math.floor(x.toDouble()).toFloat()
    protected fun lerpColor(a: Int, b: Int, t: Float): Int {
        val k = RinEase.clamp01(t)
        return Color.argb(
            (Color.alpha(a) + (Color.alpha(b) - Color.alpha(a)) * k).toInt(),
            (Color.red(a) + (Color.red(b) - Color.red(a)) * k).toInt(),
            (Color.green(a) + (Color.green(b) - Color.green(a)) * k).toInt(),
            (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * k).toInt()
        )
    }
}

/** ثلاث فقاعات (أخضر · أبيض · أخضر) تقفز بتتابع موجي. */
class RinBubblesLoadingView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    RinLoopView(context, attrs) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    override fun drawLoop(canvas: Canvas, w: Float, h: Float, cycle: Float) {
        val r = min(w / 9f, h / 4f)
        val gap = r * 2.9f
        val amp = (h - 2f * r) / 2f
        for (i in 0 until 3) {
            val s = sin(2.0 * PI * (cycle - i * 0.14f)).toFloat()
            val up = abs(s)                       // 0..1 قفزة
            paint.color = if (i == 1) inkColor else RinBrand.GREEN
            val sq = 1f - 0.18f * (1f - up)       // ينضغط قليلاً عند الأرض
            val cx = w / 2f + (i - 1) * gap
            val cy = h / 2f + amp - 2f * amp * up
            canvas.save(); canvas.scale(1f / sq, sq, cx, cy + r)
            canvas.drawCircle(cx, cy, r, paint)
            canvas.restore()
        }
    }
}

/** تموّجات خضراء متتابعة حول نواة تنبض. */
class RinRipplesLoadingView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    RinLoopView(context, attrs) {
    override var periodMs: Long = 1800L
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; color = RinBrand.GREEN }
    private val core = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = RinBrand.GREEN }
    private val coreIn = Paint(Paint.ANTI_ALIAS_FLAG)
    override fun drawLoop(canvas: Canvas, w: Float, h: Float, cycle: Float) {
        val cx = w / 2f; val cy = h / 2f
        val maxR = min(w, h) / 2f
        ring.strokeWidth = maxR * 0.07f
        for (i in 0 until 3) {
            val p = frac(cycle + i / 3f)
            ring.alpha = (230 * (1f - p) * (1f - p)).toInt()
            canvas.drawCircle(cx, cy, maxR * (0.18f + 0.82f * RinEase.out(p)), ring)
        }
        val beat = 0.5f - 0.5f * cos(cycle * 2f * PI.toFloat())
        val cr = maxR * (0.16f + 0.04f * beat)
        canvas.drawCircle(cx, cy, cr, core)
        coreIn.color = RinBrand.BLACK
        canvas.drawCircle(cx, cy, cr * 0.42f, coreIn)
    }
}

/** رمز </> : القوسان الأخضران يتباعدان ويقتربان، والشرطة البيضاء تومض. */
class RinCodeLoadingView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    RinLoopView(context, attrs) {
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND
    }
    private val left = Path().apply { moveTo(9f, 3f); lineTo(2f, 10f); lineTo(9f, 17f) }
    private val right = Path().apply { moveTo(31f, 3f); lineTo(38f, 10f); lineTo(31f, 17f) }
    private val slash = Path().apply { moveTo(22f, 1f); lineTo(18f, 19f) }
    override fun drawLoop(canvas: Canvas, w: Float, h: Float, cycle: Float) {
        val u = min(w / 44f, h / 24f)
        val breathe = 0.5f - 0.5f * cos(cycle * 2f * PI.toFloat())
        val sep = 4f * breathe
        canvas.save()
        canvas.translate((w - 40f * u) / 2f, (h - 20f * u) / 2f)
        canvas.scale(u, u)
        stroke.strokeWidth = 3.2f
        stroke.color = RinBrand.GREEN
        canvas.save(); canvas.translate(-sep, 0f); canvas.drawPath(left, stroke); canvas.restore()
        canvas.save(); canvas.translate(sep, 0f); canvas.drawPath(right, stroke); canvas.restore()
        stroke.color = inkColor
        stroke.alpha = (255 * (0.35f + 0.65f * (1f - breathe))).toInt()
        canvas.drawPath(slash, stroke)
        canvas.restore()
    }
}

/** أعمدة كمجسّات تتموّج؛ تدرّج من الأبيض إلى الأخضر عبر الصف. */
class RinWaveBarsLoadingView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    RinLoopView(context, attrs) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val rect = RectF()
    override fun drawLoop(canvas: Canvas, w: Float, h: Float, cycle: Float) {
        val n = 7
        val bw = min(w / (n * 2f), h / 3f)
        val gap = bw * 1.0f
        val total = n * bw + (n - 1) * gap
        var x = (w - total) / 2f
        for (i in 0 until n) {
            val s = 0.5f + 0.5f * sin(2.0 * PI * (cycle - i * 0.09f)).toFloat()
            val bh = h * (0.30f + 0.70f * s)
            paint.color = lerpColor(inkColor, RinBrand.GREEN, i / (n - 1f))
            rect.set(x, (h - bh) / 2f, x + bw, (h + bh) / 2f)
            canvas.drawRoundRect(rect, bw / 2f, bw / 2f, paint)
            x += bw + gap
        }
    }
}

/** نقاط تدور: رأس أخضر ثم ذيل أبيض يتلاشى ويصغر. */
class RinOrbitLoadingView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    RinLoopView(context, attrs) {
    override var periodMs: Long = 1400L
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    override fun drawLoop(canvas: Canvas, w: Float, h: Float, cycle: Float) {
        val cx = w / 2f; val cy = h / 2f
        val orbit = min(w, h) * 0.36f
        val dot = min(w, h) * 0.075f
        val n = 9
        val head = RinEase.inOut(frac(cycle)) * 2f * PI.toFloat() + frac(cycle) * 2f * PI.toFloat()
        for (i in n - 1 downTo 0) {
            val a = head - i * 0.34f - PI.toFloat() / 2f
            val k = 1f - i / n.toFloat()
            paint.color = if (i == 0) RinBrand.GREEN else inkColor
            paint.alpha = (255 * k * k).toInt().coerceAtLeast(20)
            canvas.drawCircle(cx + orbit * cos(a), cy + orbit * sin(a), dot * (0.45f + 0.55f * k), paint)
        }
    }
}

/**
 * شريط تقدّم بقنديل صغير يسبح عند رأس التعبئة. [progress] 0..1 (محدّد)، أو [setIndeterminate] لقنديل
 * يسبح ذهاباً وإياباً. الارتفاع المقترح ≥ 44dp. XML: layout_width=match_parent، layout_height=56dp.
 */
class RinJellyBarView @JvmOverloads constructor(context: Context, attrs: AttributeSet? = null) :
    RinLoopView(context, attrs) {
    private val jelly = RinJellyfish().apply { showGlow = false }
    private val track = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; color = Color.WHITE; alpha = 40 }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; color = RinBrand.GREEN }
    private var indeterminate = true
    private var target = 0f
    private var shown = 0f

    var progress: Float
        get() = target
        set(value) { indeterminate = false; target = value.coerceIn(0f, 1f); invalidate() }

    fun setIndeterminate(on: Boolean) { indeterminate = on; invalidate() }

    override fun drawLoop(canvas: Canvas, w: Float, h: Float, cycle: Float) {
        val sw = 5f * density
        track.strokeWidth = sw; fill.strokeWidth = sw
        val unit = (h - sw * 2f) / (RinJellyfish.HEIGHT_UNITS * 1.1f)
        val jw = RinJellyfish.WIDTH_UNITS * unit
        val x0 = jw / 2f + sw; val x1 = w - jw / 2f - sw
        val y = h - sw
        canvas.drawLine(x0, y, x1, y, track)

        val head: Float; val tail: Float
        if (indeterminate) {
            val p = frac(cycle * 0.55f)
            val pp = if (p < 0.5f) p * 2f else (1f - p) * 2f
            head = RinEase.inOut(pp); tail = (head - 0.22f).coerceAtLeast(0f)
        } else {
            shown += (target - shown) * 0.18f      // اقتراب ناعم من القيمة
            head = shown; tail = 0f
        }
        val hx = x0 + (x1 - x0) * head
        if (head > tail) canvas.drawLine(x0 + (x1 - x0) * tail, y, hx, y, fill)

        val c = cycle * 1.15f
        jelly.reveal = 1f
        jelly.pulse = RinEase.swimPulse(c)
        jelly.swayPhase = cycle * 6.2f
        val bob = unit * 1.2f * (0.5f - 0.5f * cos(c * RinJellyfish.TWO_PI))
        jelly.inkColor = inkColor
        jelly.draw(canvas, hx, sw * 0.5f - bob, unit)
        if (!indeterminate && abs(target - shown) > 0.002f) postInvalidateOnAnimation()
    }
}

/** أنواع المؤشرات الرسمية — لاختيار نوع برمجياً أو من الإعدادات. */
enum class RinLoaderKind { JELLY, JELLY_DRAW, JELLY_BAR, BUBBLES, RIPPLES, CODE, WAVE_BARS, ORBIT, SPINNER, SNAKE }

object RinLoaders {
    /** ينشئ المؤشر المطلوب؛ حدّد حجمه عبر LayoutParams (مثلاً 48dp للمدمجة، 160dp للقنديل). */
    fun create(context: Context, kind: RinLoaderKind): View = when (kind) {
        RinLoaderKind.JELLY -> RinJellyLoaderView(context)
        RinLoaderKind.JELLY_DRAW -> RinIconDrawLoadingView(context)
        RinLoaderKind.JELLY_BAR -> RinJellyBarView(context)
        RinLoaderKind.BUBBLES -> RinBubblesLoadingView(context)
        RinLoaderKind.RIPPLES -> RinRipplesLoadingView(context)
        RinLoaderKind.CODE -> RinCodeLoadingView(context)
        RinLoaderKind.WAVE_BARS -> RinWaveBarsLoadingView(context)
        RinLoaderKind.ORBIT -> RinOrbitLoadingView(context)
        RinLoaderKind.SPINNER -> RinSpinner(context)
        RinLoaderKind.SNAKE -> RinSnakeLoadingView(context)
    }
}

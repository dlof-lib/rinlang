package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.graphics.Typeface
import android.os.SystemClock
import android.provider.Settings
import android.util.AttributeSet
import android.view.View
import android.view.animation.LinearInterpolator
import java.util.Random
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/**
 * شاشة البداية الجديدة لـ RinStudio (أسود · أبيض · أخضر) — مرسومة بالكامل بـ Canvas بلا صور:
 *
 *  1) فقاعات صغيرة تصعد ببطء في الخلفية السوداء مع هالة خضراء خافتة (إحساس "تحت الماء").
 *  2) قنديل البحر ([RinJellyfish]) يُخطّ تدريجياً: محيط الجرس ← تعبئة خضراء ← رمز </> ← نمو المجسّات البيضاء.
 *  3) بعد اكتمال الرسم يبدأ بالسباحة (نبضة + ارتفاع + تمايل) والهالة تتنفّس معه.
 *  4) الاسم RIN (أخضر) + STUDIO (أبيض) يظهران بتلاشٍ صاعد، ثم الشعار النصي.
 *  5) خط تقدّم رفيع أسفل الشاشة يمتلئ بالأخضر على مدى [durationMs].
 *
 * الانتقال للشاشة التالية مسؤولية المُستدعي (SplashActivity)؛ هذا الـView يرسم فقط.
 * يحترم "مقياس مدة الحركة = 0" فيعرض الإطار النهائي ثابتاً.
 */
class RinSplashView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    /** اسم التطبيق. "RinStudio" يُرسم RIN أخضر + STUDIO أبيض، وأي اسم آخر (حزمة مُصدَّرة) أبيض كاملاً. */
    var appName: String = "RinStudio"
        set(value) { field = value; contentDescription = value; invalidate() }

    /** سطر وصف اختياري أسفل الاسم. */
    var tagline: String? = null
        set(value) { field = value; invalidate() }

    /** مدة الشاشة الكاملة (تتحكم بامتلاء خط التقدّم وتوقيت المراحل). */
    var durationMs: Long = 2200L
        set(value) { field = value.coerceAtLeast(300L) }

    private val jelly = RinJellyfish().apply { showGlow = true }
    private var animator: ValueAnimator? = null
    private var startUptime = 0L
    private var elapsedMs = 0L

    private val reducedMotion: Boolean =
        Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f

    private val density = resources.displayMetrics.density
    private val fontScale = resources.configuration.fontScale

    private class Bubble(val x: Float, val speed: Float, val radius: Float, val phase: Float, val green: Boolean)
    private val bubbles: List<Bubble> = Random(7).let { rnd ->
        List(BUBBLE_COUNT) {
            Bubble(
                x = rnd.nextFloat(),
                speed = 0.035f + rnd.nextFloat() * 0.05f,
                radius = 1.5f + rnd.nextFloat() * 3.5f,
                phase = rnd.nextFloat(),
                green = rnd.nextInt(3) == 0
            )
        }
    }

    private val bgPaint = Paint().apply { color = RinBrand.BLACK }
    private val vignettePaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val bubblePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = density * 1.1f }
    private val greenPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = RinBrand.GREEN
        typeface = Typeface.create("sans-serif", Typeface.BOLD)
        letterSpacing = 0.16f
    }
    private val whitePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = RinBrand.WHITE
        typeface = Typeface.create("sans-serif-light", Typeface.NORMAL)
        letterSpacing = 0.16f
    }
    private val singleNamePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = RinBrand.WHITE
        typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
        textAlign = Paint.Align.CENTER
        letterSpacing = 0.04f
    }
    private val taglinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = RinBrand.WHITE
        textAlign = Paint.Align.CENTER
        letterSpacing = 0.03f
    }
    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeWidth = density * 2.5f
        color = RinBrand.WHITE
        alpha = 36
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeWidth = density * 2.5f
        color = RinBrand.GREEN
    }
    private val headPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = RinBrand.WHITE }
    private val headGlowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = RinBrand.GREEN; alpha = 70 }

    init {
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_YES
        contentDescription = appName
    }

    fun start() {
        if (reducedMotion) { invalidate(); return }
        if (animator?.isRunning == true) return
        startUptime = SystemClock.uptimeMillis() - elapsedMs
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 1000L
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener {
                elapsedMs = SystemClock.uptimeMillis() - startUptime
                invalidate()
            }
            start()
        }
    }

    fun stop() {
        animator?.cancel()
        animator = null
    }

    override fun onAttachedToWindow() { super.onAttachedToWindow(); start() }
    override fun onDetachedFromWindow() { stop(); super.onDetachedFromWindow() }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w <= 0 || h <= 0) return
        vignettePaint.shader = RadialGradient(
            w / 2f, h * 0.42f, maxOf(w, h) * 0.72f,
            intArrayOf(Color.parseColor("#0B2417"), RinBrand.BLACK),
            floatArrayOf(0f, 1f), Shader.TileMode.CLAMP
        )
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return

        val total = durationMs.toFloat()
        val t = if (reducedMotion) total else elapsedMs.toFloat()
        val revealMs = (total * 0.55f).coerceIn(500f, 1250f)
        val reveal = RinEase.inOut(t / revealMs)

        // --- الخلفية ---
        canvas.drawRect(0f, 0f, w, h, bgPaint)
        vignettePaint.alpha = (255 * RinEase.clamp01(t / 500f)).toInt()
        canvas.drawRect(0f, 0f, w, h, vignettePaint)
        drawBubbles(canvas, w, h, t)

        // --- حالة السباحة بعد اكتمال الرسم ---
        val swimStart = revealMs * 0.85f
        val swimming = !reducedMotion && t > swimStart
        val cycle = if (swimming) (t - swimStart) / PULSE_MS else 0f
        val ramp = RinEase.clamp01((t - swimStart) / 400f)
        val pulse = if (swimming) RinEase.swimPulse(cycle) * ramp else 0f

        jelly.reveal = reveal
        jelly.pulse = pulse
        jelly.swayPhase = if (reducedMotion) 0f else t * 0.0052f
        jelly.glow = 0.35f + 0.65f * pulse

        // --- الهندسة ---
        val unit = min(w * 0.50f / RinJellyfish.WIDTH_UNITS, h * 0.34f / RinJellyfish.HEIGHT_UNITS)
        val jellyH = RinJellyfish.HEIGHT_UNITS * unit
        val blockH = jellyH + dp(118f)
        val baseTop = h * 0.46f - blockH / 2f
        val rise = (1f - RinEase.out(reveal)) * unit * 4f
        val lift = if (swimming) unit * 2.4f * (0.5f - 0.5f * cos(cycle * RinJellyfish.TWO_PI)) * ramp else 0f
        jelly.draw(canvas, w / 2f, baseTop + rise - lift, unit)

        // --- الاسم والشعار النصي ---
        val textA = RinEase.clamp01((t - revealMs * 0.55f) / (revealMs * 0.6f))
        if (textA > 0f) {
            val dy = (1f - RinEase.out(textA)) * dp(10f)
            val nameBase = baseTop + jellyH + dp(52f) + dy
            drawName(canvas, w / 2f, nameBase, textA)
            drawTagline(canvas, w, nameBase + dp(28f), RinEase.clamp01((t - revealMs * 0.7f) / (revealMs * 0.6f)))
        }

        // --- خط التقدّم ---
        drawProgress(canvas, w, h, RinEase.out(RinEase.clamp01(t / total)))
    }

    private fun drawBubbles(canvas: Canvas, w: Float, h: Float, t: Float) {
        val seconds = t / 1000f
        for (b in bubbles) {
            val travel = (b.phase + seconds * b.speed) % 1f
            val y = h * (1.05f - travel * 1.15f)
            val x = w * b.x + sin((seconds * 0.8f + b.phase * 6f).toDouble()).toFloat() * dp(10f)
            val fade = sin(travel * PI).toFloat().coerceAtLeast(0f)
            bubblePaint.color = if (b.green) RinBrand.GREEN else RinBrand.WHITE
            bubblePaint.alpha = (fade * (if (b.green) 90 else 55)).toInt()
            canvas.drawCircle(x, y, dp(b.radius), bubblePaint)
        }
    }

    private fun drawName(canvas: Canvas, cx: Float, baseline: Float, alpha: Float) {
        val size = 28f * fontScale * density
        val a = (255 * alpha).toInt()
        if (appName.equals("RinStudio", ignoreCase = true)) {
            greenPaint.textSize = size; whitePaint.textSize = size
            greenPaint.alpha = a; whitePaint.alpha = a
            val gap = dp(3f)
            val w1 = greenPaint.measureText("RIN")
            val w2 = whitePaint.measureText("STUDIO")
            val left = cx - (w1 + gap + w2) / 2f
            canvas.drawText("RIN", left, baseline, greenPaint)
            canvas.drawText("STUDIO", left + w1 + gap, baseline, whitePaint)
        } else {
            singleNamePaint.textSize = size
            singleNamePaint.alpha = a
            canvas.drawText(fit(appName, singleNamePaint, width * 0.86f), cx, baseline, singleNamePaint)
        }
    }

    private fun drawTagline(canvas: Canvas, w: Float, baseline: Float, alpha: Float) {
        val text = tagline?.takeIf { it.isNotBlank() } ?: return
        if (alpha <= 0f) return
        taglinePaint.textSize = 13f * fontScale * density
        taglinePaint.alpha = (255 * 0.62f * alpha).toInt()
        canvas.drawText(fit(text, taglinePaint, w * 0.86f), w / 2f, baseline, taglinePaint)
    }

    private fun drawProgress(canvas: Canvas, w: Float, h: Float, p: Float) {
        val len = min(w * 0.34f, dp(160f))
        val x0 = (w - len) / 2f
        val y = h - dp(72f)
        canvas.drawLine(x0, y, x0 + len, y, trackPaint)
        if (p <= 0f) return
        val x1 = x0 + len * p
        canvas.drawLine(x0, y, x1, y, fillPaint)
        if (p < 1f) {
            canvas.drawCircle(x1, y, dp(6f), headGlowPaint)
            canvas.drawCircle(x1, y, dp(2.6f), headPaint)
        }
    }

    /** يقصّ النص بـ"…" إن زاد عن العرض المتاح (أسماء التطبيقات المُصدَّرة قد تكون طويلة). */
    private fun fit(text: String, paint: Paint, maxWidth: Float): String {
        val n = paint.breakText(text, true, maxWidth, null)
        return if (n >= text.length) text else text.take((n - 1).coerceAtLeast(1)) + "…"
    }

    private fun dp(v: Float) = v * density

    private companion object {
        const val BUBBLE_COUNT = 16
        const val PULSE_MS = 1150f
    }
}

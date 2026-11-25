package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Typeface
import android.os.SystemClock
import android.util.AttributeSet
import android.view.View
import android.view.animation.LinearInterpolator
import kotlin.math.cos
import kotlin.math.min

/**
 * مؤشر تقدّم المحرر: **القنديل نفسه هو شريط التقدّم**. كلما زادت النسبة اكتمل رسمه
 * (محيط الجرس ← تعبئة خضراء ← رمز </> ← نمو المجسّات البيضاء)، وعند الاكتمال يستمر بالسباحة.
 * تحته: RIN أبيض، النسبة بالأخضر، وشريط رفيع أخضر. (أسود · أبيض · أخضر).
 *
 * الواجهة كما كانت: [setProgress] [start] [stop]، فيعمل [RinLogoLoadingOverlay] دون تعديل.
 */
class RinLogoLoadingView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val jelly = RinJellyfish()
    private val density = resources.displayMetrics.density

    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
        color = RinBrand.WHITE
        letterSpacing = 0.22f
    }
    private val percentPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.DEFAULT, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
        color = RinBrand.GREEN
    }
    private val trackPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND
        color = RinBrand.WHITE; alpha = 46
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND
        color = RinBrand.GREEN
    }

    private var progress = 0f
    private var progressAnimator: ValueAnimator? = null
    private var clock: ValueAnimator? = null
    private var clockStart = 0L
    private var elapsedMs = 0L

    init { isClickable = false }

    fun setProgress(value: Float) {
        progress = value.coerceIn(0f, 1f)
        invalidate()
    }

    fun start(durationMs: Long = 1200L) {
        progressAnimator?.cancel()
        progressAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = durationMs
            interpolator = LinearInterpolator()
            addUpdateListener { setProgress(it.animatedValue as Float) }
            start()
        }
        startClock()
    }

    /** يثبّت التقدّم عند 100% (الجرس مكتمل) ويترك القنديل يسبح حتى يُزال الـView. */
    fun stop() {
        progressAnimator?.cancel()
        progressAnimator = null
        setProgress(1f)
    }

    private fun startClock() {
        if (clock?.isRunning == true) return
        clockStart = SystemClock.uptimeMillis() - elapsedMs
        clock = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 1000L
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener { elapsedMs = SystemClock.uptimeMillis() - clockStart; invalidate() }
            start()
        }
    }

    override fun onAttachedToWindow() { super.onAttachedToWindow(); startClock() }

    override fun onDetachedFromWindow() {
        progressAnimator?.cancel(); progressAnimator = null
        clock?.cancel(); clock = null
        super.onDetachedFromWindow()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat(); val h = height.toFloat()
        if (w <= 0f || h <= 0f) return

        val done = progress >= 1f
        val cycle = elapsedMs / 1150f
        val pulse = if (done) RinEase.swimPulse(cycle) else 0f

        jelly.reveal = progress
        jelly.pulse = pulse
        jelly.swayPhase = elapsedMs * 0.0052f
        jelly.glow = 0.4f + 0.6f * RinEase.clamp01(progress) * (if (done) 0.5f + 0.5f * pulse else 0.6f)

        val unit = min(w * 0.46f / RinJellyfish.WIDTH_UNITS, h * 0.34f / RinJellyfish.HEIGHT_UNITS)
        val jellyH = RinJellyfish.HEIGHT_UNITS * unit
        val lift = if (done) unit * 2.4f * (0.5f - 0.5f * cos(cycle * RinJellyfish.TWO_PI)) else 0f
        val top = h * 0.40f - jellyH / 2f - lift
        jelly.draw(canvas, w / 2f, top, unit)

        val textBase = h * 0.40f + jellyH / 2f + unit * 6f
        textPaint.textSize = 18f * density
        canvas.drawText("RIN", w / 2f, textBase, textPaint)
        percentPaint.textSize = 26f * density
        canvas.drawText("${(progress * 100f).toInt()}%", w / 2f, textBase + 34f * density, percentPaint)

        val barY = textBase + 54f * density
        val half = min(w * 0.30f, 110f * density)
        trackPaint.strokeWidth = 4f * density; fillPaint.strokeWidth = 4f * density
        canvas.drawLine(w / 2f - half, barY, w / 2f + half, barY, trackPaint)
        if (progress > 0f) canvas.drawLine(w / 2f - half, barY, w / 2f - half + 2f * half * progress, barY, fillPaint)
    }
}

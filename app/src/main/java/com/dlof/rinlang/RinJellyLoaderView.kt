package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.os.SystemClock
import android.provider.Settings
import android.util.AttributeSet
import android.view.View
import android.view.animation.LinearInterpolator
import kotlin.math.cos
import kotlin.math.min

/**
 * مؤشر التحميل الرسمي المتوسط/الكبير في RinStudio: قنديل البحر (شعار التطبيق) يسبح في مكانه —
 * ينقبض جرسه بنبضة سباحة، ترتفع قليلاً مع كل نبضة، وتتمايل مجسّاته بموجة ناعمة، وخلفه هالة خضراء
 * تتنفّس مع النبض. يحلّ محلّ الحلقة الدوّارة + صورة الشعار القديمة في [RinLoadingView].
 *
 * يبدأ تلقائياً مع attach ويتوقف مع detach، أو يدوياً عبر [start]/[stop]. يحترم إعداد النظام
 * "مقياس مدة الحركة = 0" فيعرض إطاراً ثابتاً بدل الحركة.
 */
class RinJellyLoaderView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val jelly = RinJellyfish()
    private var animator: ValueAnimator? = null
    private var startUptime = 0L
    private var elapsedMs = 0L
    private val reducedMotion: Boolean =
        Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f

    /** لون المجسّات: أبيض افتراضياً (للخلفيات الداكنة). */
    var inkColor: Int
        get() = jelly.inkColor
        set(value) { jelly.inkColor = value; invalidate() }

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

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        start()
    }

    override fun onDetachedFromWindow() {
        stop()
        super.onDetachedFromWindow()
    }

    override fun onVisibilityChanged(changedView: View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        if (changedView !== this || !isAttachedToWindow) return
        if (visibility == VISIBLE) start() else stop()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat()
        val h = height.toFloat()
        if (w <= 0f || h <= 0f) return

        val cycle = if (reducedMotion) 0f else elapsedMs / PULSE_MS
        val pulse = if (reducedMotion) 0f else RinEase.swimPulse(cycle)
        jelly.reveal = 1f
        jelly.pulse = pulse
        jelly.swayPhase = if (reducedMotion) 0f else elapsedMs * 0.0052f
        jelly.glow = 0.45f + 0.55f * pulse

        // الحجم: يملأ المساحة مع هامش، وتُحجز مساحة علوية صغيرة لارتفاع السباحة.
        val unit = min(w / (RinJellyfish.WIDTH_UNITS * 1.5f), h / (RinJellyfish.HEIGHT_UNITS * 1.18f))
        val lift = unit * 2.4f * (0.5f - 0.5f * cos(cycle * RinJellyfish.TWO_PI))
        val top = (h - RinJellyfish.HEIGHT_UNITS * unit) / 2f + unit * 1.2f - lift
        jelly.draw(canvas, w / 2f, top, unit)
    }

    private companion object {
        const val PULSE_MS = 1150f
    }
}

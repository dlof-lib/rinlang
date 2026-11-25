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
 * مؤشر تحميل مدمج بهوية RinStudio: يرسم قنديل البحر تدريجياً (محيط ← تعبئة خضراء ← </> ← مجسّات
 * بيضاء)، يسبح لحظة، ثم يعيد الرسم في حلقة. بديل مميّز لـ[RinSpinner] حيث يُراد طابع العلامة.
 * بلا هالة (أخفّ وأوضح في الأحجام الصغيرة). على خلفية فاتحة اضبط [inkColor] على لون داكن.
 *
 * XML: <com.dlof.rinlang.RinIconDrawLoadingView android:layout_width="72dp" android:layout_height="80dp" />
 * الواجهة كما كانت: [setProgress] [start] [stop]؛ يبدأ تلقائياً مع attach.
 */
class RinIconDrawLoadingView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val jelly = RinJellyfish().apply { showGlow = false }
    private val reducedMotion = Settings.Global.getFloat(
        context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f
    ) == 0f

    private var progress = 0f
    private var loopAnimator: ValueAnimator? = null
    private var startUptime = 0L
    private var elapsedMs = 0L
    private var manual = false

    var inkColor: Int
        get() = jelly.inkColor
        set(value) { jelly.inkColor = value; invalidate() }

    /** يضبط اكتمال الرسم يدوياً (0..1) ويوقف الحلقة التلقائية. */
    fun setProgress(value: Float) {
        manual = true
        progress = value.coerceIn(0f, 1f)
        invalidate()
    }

    /** يبدأ حلقة الرسم؛ [durationMs] = مدة دورة كاملة (رسم + سباحة قصيرة). */
    fun start(durationMs: Long = 1900L) {
        manual = false
        if (reducedMotion) { progress = 1f; invalidate(); return }
        loopAnimator?.cancel()
        startUptime = SystemClock.uptimeMillis()
        loopAnimator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = durationMs
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener {
                val f = it.animatedValue as Float
                progress = RinEase.clamp01(f / 0.62f)   // 62% رسم، 38% سباحة بالقنديل المكتمل
                elapsedMs = SystemClock.uptimeMillis() - startUptime
                invalidate()
            }
            start()
        }
    }

    /** يوقف الحلقة ويثبّت الشعار مكتملاً (مؤشر نجاح لحظي قبل الإخفاء). */
    fun stop() {
        loopAnimator?.cancel()
        loopAnimator = null
        manual = true
        progress = 1f
        invalidate()
    }

    override fun onAttachedToWindow() { super.onAttachedToWindow(); if (!manual) start() }
    override fun onDetachedFromWindow() { loopAnimator?.cancel(); loopAnimator = null; super.onDetachedFromWindow() }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val w = width.toFloat(); val h = height.toFloat()
        if (w <= 0f || h <= 0f) return
        val cycle = elapsedMs / 1150f
        val swimming = progress >= 1f && !reducedMotion
        jelly.reveal = progress
        jelly.pulse = if (swimming) RinEase.swimPulse(cycle) else 0f
        jelly.swayPhase = if (reducedMotion) 0f else elapsedMs * 0.0052f
        val unit = min(w / (RinJellyfish.WIDTH_UNITS * 1.25f), h / (RinJellyfish.HEIGHT_UNITS * 1.12f))
        val lift = if (swimming) unit * 1.6f * (0.5f - 0.5f * cos(cycle * RinJellyfish.TWO_PI)) else 0f
        val top = (h - RinJellyfish.HEIGHT_UNITS * unit) / 2f + unit * 0.8f - lift
        jelly.draw(canvas, w / 2f, top, unit)
    }
}

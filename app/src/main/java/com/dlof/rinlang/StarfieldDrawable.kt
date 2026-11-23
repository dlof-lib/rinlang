package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorFilter
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.RadialGradient
import android.graphics.Shader
import android.graphics.drawable.Drawable
import android.provider.Settings
import android.view.Choreographer

/**
 * خلفية «فضاء ونجوم» رسمية: تدرّج ليلي عميق + سديمان خافتان + نجوم متلألئة بثلاث طبقات عمق + نيزك عابر.
 * الحركة بـ ~30 إطاراً/ث فقط أثناء الاستدعاء [start]، وتتوقف تلقائياً إن كان مقياس الأنيميشن في النظام صفراً.
 * [dim] (0..90 %) طبقة تعتيم فوقها لقراءة النص.
 */
class StarfieldDrawable(private val context: Context, dim: Int) : Drawable(), Choreographer.FrameCallback {
    private val model = StarfieldModel()
    private val density = context.resources.displayMetrics.density
    private val bgPaint = Paint()
    private val nebPaint = Paint()
    private val starPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val glarePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeWidth = density * 0.8f; strokeCap = Paint.Cap.ROUND }
    private val meteorPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeWidth = density * 1.6f; strokeCap = Paint.Cap.ROUND }
    private val dimPaint = Paint().apply { color = Color.BLACK; alpha = (dim.coerceIn(0, 90) * 255 / 100) }
    private var neb1: RadialGradient? = null
    private var neb2: RadialGradient? = null
    private var running = false
    private var animated = true
    private var last = 0L

    private val reduceMotion: Boolean
        get() = try { Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f } catch (t: Throwable) { false }

    override fun onBoundsChange(b: android.graphics.Rect) {
        val w = b.width().toFloat(); val h = b.height().toFloat()
        if (w <= 0f || h <= 0f) return
        bgPaint.shader = LinearGradient(0f, 0f, 0f, h, intArrayOf(0xFF02030A.toInt(), 0xFF070C24.toInt(), 0xFF120C30.toInt()), floatArrayOf(0f, 0.55f, 1f), Shader.TileMode.CLAMP)
        val r = maxOf(w, h) * 0.55f
        neb1 = RadialGradient(w * 0.25f, h * 0.30f, r, intArrayOf(0x40603CCF, 0x00603CCF), null, Shader.TileMode.CLAMP)
        neb2 = RadialGradient(w * 0.85f, h * 0.80f, r * 0.9f, intArrayOf(0x3000A0B8, 0x0000A0B8), null, Shader.TileMode.CLAMP)
    }

    override fun draw(c: Canvas) {
        val b = bounds
        if (b.isEmpty) return
        val w = b.width().toFloat(); val h = b.height().toFloat()
        c.drawRect(b, bgPaint)
        neb1?.let { nebPaint.shader = it; c.drawRect(b, nebPaint) }
        neb2?.let { nebPaint.shader = it; c.drawRect(b, nebPaint) }
        for (s in model.stars) {
            val a = (model.alpha(s) * 255).toInt().coerceIn(0, 255)
            val col = when (s.tint) { 1 -> Color.argb(a, 170, 200, 255); 2 -> Color.argb(a, 255, 228, 170); else -> Color.argb(a, 255, 255, 255) }
            val cx = b.left + s.x * w; val cy = b.top + s.y * h
            starPaint.color = col
            c.drawCircle(cx, cy, s.size * density, starPaint)
            if (s.layer == 2) {   // وهج صليبي للنجوم القريبة اللامعة
                glarePaint.color = Color.argb(a / 2, 255, 255, 255)
                val g = s.size * density * 3.2f
                c.drawLine(cx - g, cy, cx + g, cy, glarePaint)
                c.drawLine(cx, cy - g, cx, cy + g, glarePaint)
            }
        }
        model.meteor?.let { m ->
            val a = (model.meteorAlpha(m) * 255).toInt().coerceIn(0, 255)
            val x = b.left + m.x * w; val y = b.top + m.y * h
            val tail = 0.16f
            val tx = x - m.vx * tail * w / maxOf(0.2f, Math.abs(m.vx)) * 0.5f
            val ty = y - m.vy * tail * h / maxOf(0.2f, Math.abs(m.vx)) * 0.5f
            meteorPaint.shader = LinearGradient(x, y, tx, ty, Color.argb(a, 255, 255, 255), Color.argb(0, 255, 255, 255), Shader.TileMode.CLAMP)
            c.drawLine(x, y, tx, ty, meteorPaint)
        }
        if (dimPaint.alpha > 0) c.drawRect(b, dimPaint)
    }

    // ---- الحركة ----
    /** يشغّل/يوقف الحركة؛ عند الإيقاف تبقى النجوم ثابتة على آخر إطار. */
    fun setAnimated(on: Boolean) { animated = on; if (!on) stop() else if (running) { /* يستمر */ } }

    fun start() {
        if (running || !animated || reduceMotion) return
        running = true; last = 0L
        Choreographer.getInstance().postFrameCallback(this)
    }

    fun stop() {
        if (!running) return
        running = false
        Choreographer.getInstance().removeFrameCallback(this)
    }

    override fun doFrame(frameTimeNanos: Long) {
        if (!running) return
        if (last == 0L) last = frameTimeNanos
        val dtMs = (frameTimeNanos - last) / 1_000_000
        if (dtMs >= 33) {   // ~30fps يكفي للوميض ويوفّر البطارية
            model.step(dtMs / 1000f); last = frameTimeNanos
            invalidateSelf()
        }
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun setAlpha(alpha: Int) {}
    override fun setColorFilter(cf: ColorFilter?) {}
    @Deprecated("Deprecated in Java") override fun getOpacity(): Int = PixelFormat.OPAQUE
}

package com.dlof.rinlang

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.ColorFilter
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.PorterDuff
import android.graphics.PorterDuffColorFilter
import android.graphics.RadialGradient
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.drawable.Drawable
import android.provider.Settings
import android.view.Choreographer
import kotlin.math.hypot
import kotlin.math.min

/** خلفية حيّة قابلة للتشغيل/الإيقاف (الفضاء أو صورة متحركة). */
interface LiveBackground {
    /** يسمح بالحركة أو يمنعها؛ عند المنع تبقى آخر لقطة ظاهرة. */
    fun setAnimated(on: Boolean)
    fun start()
    fun stop()
}

/** هل المستخدم عطّل الأنيميشن من إعدادات النظام؟ */
internal fun reduceMotion(context: Context): Boolean =
    try { Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f } catch (t: Throwable) { false }

/**
 * خلفية «فضاء ونجوم» رسمية: تدرّج ليلي عميق + سديمان يطفوان ببطء + نجوم متلألئة بثلاث طبقات عمق
 * (هالة ناعمة للقريبة) + نيازك بذيل متدرّج + تعتيم حواف (vignette).
 * الحركة ~30 إطاراً/ث فقط أثناء [start]، وتتوقف تلقائياً إن كان مقياس الأنيميشن صفراً.
 * [dim] (0..90 %) طبقة تعتيم فوقها لقراءة النص.
 */
class StarfieldDrawable(private val context: Context, dim: Int, scrim: Int = Color.BLACK) : Drawable(), Choreographer.FrameCallback, LiveBackground {
    private val model = StarfieldModel()
    private val density = context.resources.displayMetrics.density
    private val bgPaint = Paint()
    private val nebPaint = Paint()
    private val vigPaint = Paint()
    private val starPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val glarePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeWidth = density * 0.8f; strokeCap = Paint.Cap.ROUND }
    private val meteorPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeWidth = density * 1.8f; strokeCap = Paint.Cap.ROUND }
    private val headPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val dimPaint = Paint().apply { color = scrim; alpha = (dim.coerceIn(0, 90) * 255 / 100) }

    // هالة جاهزة (دائرة بيضاء متلاشية) تُلوَّن لكل ميل لوني؛ أرخص من تدرّج لكل نجمة.
    private val glow: Bitmap = Bitmap.createBitmap(48, 48, Bitmap.Config.ARGB_8888).also { bmp ->
        val p = Paint(Paint.ANTI_ALIAS_FLAG)
        p.shader = RadialGradient(24f, 24f, 24f, intArrayOf(0xFFFFFFFF.toInt(), 0x55FFFFFF, 0x00FFFFFF), floatArrayOf(0f, 0.35f, 1f), Shader.TileMode.CLAMP)
        Canvas(bmp).drawCircle(24f, 24f, 24f, p)
    }
    private val glowPaints = arrayOf(
        Paint(Paint.FILTER_BITMAP_FLAG).apply { colorFilter = PorterDuffColorFilter(Color.rgb(255, 255, 255), PorterDuff.Mode.SRC_IN) },
        Paint(Paint.FILTER_BITMAP_FLAG).apply { colorFilter = PorterDuffColorFilter(Color.rgb(150, 190, 255), PorterDuff.Mode.SRC_IN) },
        Paint(Paint.FILTER_BITMAP_FLAG).apply { colorFilter = PorterDuffColorFilter(Color.rgb(255, 215, 150), PorterDuff.Mode.SRC_IN) }
    )
    private val glowDst = RectF()
    private val glowSrc = Rect(0, 0, 48, 48)

    private var neb1: RadialGradient? = null
    private var neb2: RadialGradient? = null
    private var neb3: RadialGradient? = null
    private var running = false
    private var animated = true
    private var last = 0L

    override fun onBoundsChange(b: Rect) {
        val w = b.width().toFloat(); val h = b.height().toFloat()
        if (w <= 0f || h <= 0f) return
        bgPaint.shader = LinearGradient(0f, 0f, 0f, h, intArrayOf(0xFF02030A.toInt(), 0xFF070C24.toInt(), 0xFF120C30.toInt()), floatArrayOf(0f, 0.55f, 1f), Shader.TileMode.CLAMP)
        val r = maxOf(w, h) * 0.55f
        neb1 = RadialGradient(w * 0.25f, h * 0.30f, r, intArrayOf(0x40603CCF, 0x00603CCF), null, Shader.TileMode.CLAMP)
        neb2 = RadialGradient(w * 0.85f, h * 0.80f, r * 0.9f, intArrayOf(0x3000A0B8, 0x0000A0B8), null, Shader.TileMode.CLAMP)
        neb3 = RadialGradient(w * 0.60f, h * 0.15f, r * 0.55f, intArrayOf(0x28C0508A, 0x00C0508A), null, Shader.TileMode.CLAMP)
        vigPaint.shader = RadialGradient(w / 2f, h / 2f, maxOf(w, h) * 0.75f, intArrayOf(0x00000000, 0x00000000, 0x80000000.toInt()), floatArrayOf(0f, 0.55f, 1f), Shader.TileMode.CLAMP)
    }

    override fun draw(c: Canvas) {
        val b = bounds
        if (b.isEmpty) return
        val w = b.width().toFloat(); val h = b.height().toFloat()
        c.drawRect(b, bgPaint)
        // السدم تطفو ببطء (إزاحة صغيرة للوحة كلها)
        c.save()
        c.translate(model.nebulaDx() * w, model.nebulaDy() * h)
        neb1?.let { nebPaint.shader = it; c.drawRect(b, nebPaint) }
        neb2?.let { nebPaint.shader = it; c.drawRect(b, nebPaint) }
        neb3?.let { nebPaint.shader = it; c.drawRect(b, nebPaint) }
        c.restore()

        for (s in model.stars) {
            val a = (model.alpha(s) * 255).toInt().coerceIn(0, 255)
            val col = when (s.tint) { 1 -> Color.argb(a, 170, 200, 255); 2 -> Color.argb(a, 255, 228, 170); else -> Color.argb(a, 255, 255, 255) }
            val cx = b.left + s.x * w; val cy = b.top + s.y * h
            val r = s.size * density
            if (s.layer >= 1) {   // هالة ناعمة للنجوم الأقرب
                val g = r * (if (s.layer == 2) 5f else 3.2f)
                glowDst.set(cx - g, cy - g, cx + g, cy + g)
                val gp = glowPaints[s.tint.coerceIn(0, 2)]
                gp.alpha = (a * (if (s.layer == 2) 0.55f else 0.32f)).toInt()
                c.drawBitmap(glow, glowSrc, glowDst, gp)
            }
            starPaint.color = col
            c.drawCircle(cx, cy, r, starPaint)
            if (s.layer == 2) {   // وهج صليبي للنجوم القريبة اللامعة
                glarePaint.color = Color.argb(a / 2, 255, 255, 255)
                val g = r * 3.6f
                c.drawLine(cx - g, cy, cx + g, cy, glarePaint)
                c.drawLine(cx, cy - g, cx, cy + g, glarePaint)
            }
        }

        for (m in model.meteors) {
            val a = (model.meteorAlpha(m) * 255).toInt().coerceIn(0, 255)
            val x = b.left + m.x * w; val y = b.top + m.y * h
            val dx = m.vx * w; val dy = m.vy * h
            val len = hypot(dx, dy).coerceAtLeast(1f)
            val tail = min(w, h) * 0.30f * model.meteorAlpha(m).coerceAtLeast(0.35f)
            val tx = x - dx / len * tail; val ty = y - dy / len * tail
            meteorPaint.shader = LinearGradient(x, y, tx, ty, Color.argb(a, 255, 255, 255), Color.argb(0, 150, 190, 255), Shader.TileMode.CLAMP)
            c.drawLine(x, y, tx, ty, meteorPaint)
            headPaint.color = Color.argb(a, 255, 255, 255)
            c.drawCircle(x, y, density * 1.6f, headPaint)
        }

        c.drawRect(b, vigPaint)
        if (dimPaint.alpha > 0) c.drawRect(b, dimPaint)
    }

    // ---- الحركة ----
    override fun setAnimated(on: Boolean) { animated = on; if (!on) stop() }

    override fun start() {
        if (running || !animated || reduceMotion(context)) return
        running = true; last = 0L
        Choreographer.getInstance().postFrameCallback(this)
    }

    override fun stop() {
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

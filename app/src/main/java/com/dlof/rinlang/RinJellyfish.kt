package com.dlof.rinlang

import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PathMeasure
import android.graphics.RadialGradient
import android.graphics.Shader
import kotlin.math.pow
import kotlin.math.sin

/** الألوان الرسمية لهوية RinStudio (أيقونة التطبيق + شاشة البداية + كل مؤشرات التحميل): أسود، أبيض، أخضر. */
object RinBrand {
    val BLACK: Int = Color.BLACK
    val WHITE: Int = Color.WHITE
    val GREEN: Int = Color.parseColor("#3DF59A")
    val GREEN_DEEP: Int = Color.parseColor("#0FA968")
}

/** دوال تسهيل (easing) صغيرة مشتركة بين كل رسوم الهوية المتحركة. */
internal object RinEase {
    fun clamp01(v: Float): Float = if (v < 0f) 0f else if (v > 1f) 1f else v
    fun out(t: Float): Float = 1f - (1f - clamp01(t)).pow(3)
    fun inOut(t: Float): Float {
        val x = clamp01(t)
        return if (x < 0.5f) 4f * x * x * x else 1f - (-2f * x + 2f).pow(3) / 2f
    }

    /**
     * نبضة سباحة قنديل البحر: انقباض سريع للجرس ثم ارتخاء أبطأ. [p] دورة كاملة 0..1 → قيمة 0..1.
     */
    fun swimPulse(p: Float): Float {
        val x = p - Math.floor(p.toDouble()).toFloat()
        return if (x < 0.28f) out(x / 0.28f) else 1f - inOut((x - 0.28f) / 0.72f)
    }
}

/**
 * محرك رسم قنديل البحر — شعار RinStudio مرسوماً بالكامل بـ Path (بلا صور)، لذا يتحرك ويُرسم تدريجياً
 * بدقة كاملة على أي حجم. تستخدمه شاشة البداية [RinSplashView] وكل مؤشرات التحميل (RinLoadingView،
 * RinLogoLoadingView، RinIconDrawLoadingView…) ليبقى لغة التحميل بصرياً واحدة في كل التطبيق.
 *
 * فضاء الإحداثيات الداخلي = نفس إحداثيات rin_app_icon.svg: الجرس بين x=24..84 و y=24..62،
 * والمجسّات تتدلّى حتى y≈90. الارتفاع الكلي ≈ [HEIGHT_UNITS] وحدة، والعرض [WIDTH_UNITS].
 *
 * الحالة تُضبط قبل كل رسم من المُستدعي:
 *  • [reveal]  0..1 : الرسم التدريجي (محيط الجرس ← تعبئة ← رمز </> ← نمو المجسّات).
 *  • [swayPhase]   : طور تمايل المجسّات (راديان) — زيادته المستمرة تعطي حركة موجية.
 *  • [pulse]   0..1 : انقباض الجرس (يضيق ويستطيل قليلاً) — القيمة 1 = أقصى انقباض.
 *  • [glow]    0..1 : شدّة الهالة الخضراء خلف الجرس.
 */
class RinJellyfish {

    var reveal = 1f
    var swayPhase = 0f
    var pulse = 0f
    var glow = 0.6f
    var swayAmount = 1f
    var showGlow = true

    /** لون المجسّات (أبيض على الخلفيات الداكنة، وأسود/داكن على الفاتحة). */
    var inkColor: Int = RinBrand.WHITE
    var bellColor: Int = RinBrand.GREEN
    var glyphColor: Int = RinBrand.BLACK

    private val bellPath = Path().apply {
        moveTo(24f, 55f)
        cubicTo(24f, 37f, 37f, 24f, 54f, 24f)
        cubicTo(71f, 24f, 84f, 37f, 84f, 55f)
        quadTo(78f, 62f, 72f, 55f)
        quadTo(66f, 62f, 60f, 55f)
        quadTo(54f, 62f, 48f, 55f)
        quadTo(42f, 62f, 36f, 55f)
        quadTo(30f, 62f, 24f, 55f)
        close()
    }
    private val bellLength = PathMeasure(bellPath, false).length

    private val shadePath = Path().apply {
        moveTo(18f, 52f)
        cubicTo(40f, 48.5f, 68f, 48.5f, 90f, 52f)
        lineTo(90f, 66f)
        lineTo(18f, 66f)
        close()
    }
    private val highlightPath = Path().apply {
        moveTo(32f, 40f)
        cubicTo(33f, 33f, 39f, 28.5f, 46f, 27.2f)
    }
    private val glyphParts: Array<Path> = arrayOf(
        Path().apply { moveTo(45f, 34f); lineTo(38f, 41f); lineTo(45f, 48f) },
        Path().apply { moveTo(63f, 34f); lineTo(70f, 41f); lineTo(63f, 48f) },
        Path().apply { moveTo(58f, 32f); lineTo(50f, 49f) }
    )
    private val glyphLengths = FloatArray(3) { PathMeasure(glyphParts[it], false).length }

    private val measure = PathMeasure()
    private val segment = Path()

    private val outlinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        strokeWidth = 1.7f
    }
    private val bellPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val shadePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL; color = Color.BLACK }
    private val highlightPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeWidth = 3f
        color = Color.WHITE
    }
    private val glyphPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        strokeWidth = 3.8f
    }
    private val tentaclePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }

    private var glowKeyX = Float.NaN
    private var glowKeyY = Float.NaN
    private var glowKeyR = Float.NaN
    private var glowKeyColor = 0

    /**
     * يرسم القنديل. [cx] منتصف العرض بالبكسل، [top] إحداثي y لقمة الجرس بالبكسل،
     * [unit] عدد البكسلات لكل وحدة (حجم الرسم = WIDTH_UNITS×unit عرضاً).
     */
    fun draw(canvas: Canvas, cx: Float, top: Float, unit: Float) {
        val r = RinEase.clamp01(reveal)
        if (r <= 0f || unit <= 0f) return

        val sx = 1f - 0.10f * pulse
        val sy = 1f + 0.06f * pulse

        if (showGlow && r > 0.3f) drawGlow(canvas, cx, top, unit, r)

        val save = canvas.save()
        canvas.translate(cx, top)
        canvas.scale(unit, unit)
        canvas.translate(-54f, -24f)

        drawTentacles(canvas, r, sx, sy)

        // الجرس (يتقلّص ويستطيل مع النبضة حول قمته)
        val bellSave = canvas.save()
        canvas.scale(sx, sy, 54f, 24f)

        // 1) محيط الجرس يُرسم تدريجياً
        if (r < 0.7f) {
            val a = RinEase.out(r / 0.45f)
            outlinePaint.color = bellColor
            segment.rewind()
            measure.setPath(bellPath, false)
            measure.getSegment(0f, bellLength * a, segment, true)
            canvas.drawPath(segment, outlinePaint)
        }

        // 2) تعبئة الجرس
        val fillA = RinEase.clamp01((r - 0.35f) / 0.3f)
        if (fillA > 0f) {
            bellPaint.color = bellColor
            bellPaint.alpha = (255 * fillA).toInt()
            canvas.drawPath(bellPath, bellPaint)

            val clip = canvas.save()
            canvas.clipPath(bellPath)
            shadePaint.alpha = (255 * 0.16f * fillA).toInt()
            canvas.drawPath(shadePath, shadePaint)
            canvas.restoreToCount(clip)

            highlightPaint.alpha = (255 * 0.32f * fillA).toInt()
            canvas.drawPath(highlightPath, highlightPaint)
        }

        // 3) رمز </> يُخطّ تدريجياً
        val glyphA = RinEase.clamp01((r - 0.55f) / 0.3f)
        if (glyphA > 0f) {
            glyphPaint.color = glyphColor
            for (k in 0 until 3) {
                val local = RinEase.clamp01(glyphA * 3f - k)
                if (local <= 0f) continue
                if (local >= 1f) {
                    canvas.drawPath(glyphParts[k], glyphPaint)
                } else {
                    segment.rewind()
                    measure.setPath(glyphParts[k], false)
                    measure.getSegment(0f, glyphLengths[k] * local, segment, true)
                    canvas.drawPath(segment, glyphPaint)
                }
            }
        }
        canvas.restoreToCount(bellSave)
        canvas.restoreToCount(save)
    }

    private fun drawGlow(canvas: Canvas, cx: Float, top: Float, unit: Float, r: Float) {
        val gr = unit * 54f
        val gcy = top + unit * 20f
        if (glowKeyX != cx || glowKeyY != gcy || glowKeyR != gr || glowKeyColor != bellColor) {
            glowPaint.shader = RadialGradient(
                cx, gcy, gr,
                intArrayOf(bellColor, Color.TRANSPARENT),
                floatArrayOf(0f, 1f),
                Shader.TileMode.CLAMP
            )
            glowKeyX = cx; glowKeyY = gcy; glowKeyR = gr; glowKeyColor = bellColor
        }
        glowPaint.alpha = (150f * glow.coerceIn(0f, 1f) * RinEase.clamp01((r - 0.3f) / 0.4f)).toInt()
        canvas.drawCircle(cx, gcy, gr, glowPaint)
    }

    private fun drawTentacles(canvas: Canvas, r: Float, sx: Float, sy: Float) {
        val grow = RinEase.out((r - 0.5f) / 0.5f)
        if (grow <= 0f) return
        tentaclePaint.color = inkColor
        tentaclePaint.alpha = (255 * RinEase.clamp01(grow * 1.6f)).toInt()

        val rootY = 24f + 32f * sy
        for (i in 0 until TENTACLES) {
            val off = OFFSETS[i]
            val len = LENGTHS[i] * grow
            val ax = 54f + off * sx
            var px = ax
            var py = rootY - 1f
            for (j in 1..SEGMENTS) {
                val s = j / SEGMENTS.toFloat()
                val wave = sin(swayPhase - s * 3.1f + i * 0.85f)
                val x = ax + wave * swayAmount * (0.25f + 0.75f * s) * 3.4f + off * 0.10f * pulse * s
                val y = rootY - 1f + s * len
                tentaclePaint.strokeWidth = 3.1f * (1f - s) + 0.9f * s
                canvas.drawLine(px, py, x, y, tentaclePaint)
                px = x
                py = y
            }
        }
    }

    companion object {
        const val WIDTH_UNITS = 60f
        const val HEIGHT_UNITS = 68f
        private const val TENTACLES = 5
        private const val SEGMENTS = 14
        private val OFFSETS = floatArrayOf(-25f, -11f, 0f, 11f, 25f)
        private val LENGTHS = floatArrayOf(21f, 29f, 33f, 29f, 21f)
        const val TWO_PI = 6.2831855f
    }
}

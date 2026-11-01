package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Shader
import android.graphics.SweepGradient
import android.util.AttributeSet
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

/**
 * لوحة اختيار لون كاملة (Hue + Saturation) على شكل "عجلة ألوان": التدرّج اللوني (Hue) يدور حول
 * مركز اللوحة عبر [SweepGradient]، ويعلوه تدرّج أبيض->شفاف ([RadialGradient]) يعطي الانتقال من
 * أبيض في المركز إلى اللون الكامل عند الحواف — بنفس فكرة منتقي الألوان الكلاسيكي (Image 1).
 * القيمة (Value/Brightness) ثابتة دوماً على 1، فلا حاجة لشريط سطوع منفصل هنا. تُستخدم من
 * [ProjectsActivity.showColorPickerDialog] لاختيار اللون الأساسي لمشروع نوع UI.
 */
class HueSaturationPickerView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    /** يُستدعى في كل مرة يتغيّر فيها اللون المختار أثناء اللمس/السحب. */
    var onColorChanged: ((Int) -> Unit)? = null

    private val wheelPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val highlightPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val markerStrokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 3.5f * resources.displayMetrics.density
        color = Color.WHITE
    }
    private val markerShadowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 6f * resources.displayMetrics.density
        color = 0x33000000
    }

    private var centerX = 0f
    private var centerY = 0f
    private var maxRadius = 0f

    // موضع المؤشر الحالي؛ افتراضياً في المركز (يقابل الأبيض FFFFFF، نفس افتراضي الحوار).
    private var markerX = 0f
    private var markerY = 0f
    private var pendingColor: Int? = null

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        centerX = w / 2f
        centerY = h / 2f
        maxRadius = min(w, h) / 2f
        markerX = centerX
        markerY = centerY

        val hueColors = intArrayOf(
            Color.RED, Color.MAGENTA, Color.BLUE, Color.CYAN, Color.GREEN, Color.YELLOW, Color.RED
        )
        wheelPaint.shader = SweepGradient(centerX, centerY, hueColors, null)
        highlightPaint.shader = RadialGradient(
            centerX, centerY, maxRadius.coerceAtLeast(1f),
            intArrayOf(0xFFFFFFFF.toInt(), 0x00FFFFFF), null, Shader.TileMode.CLAMP
        )

        // إن كان هناك لون بانتظار تطبيقه (وُصل عبر setColor قبل توفر الحجم)، طبّقه الآن.
        pendingColor?.let { positionMarkerForColor(it) }
        pendingColor = null
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), wheelPaint)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), highlightPaint)
        val markerRadius = 9f * resources.displayMetrics.density
        canvas.drawCircle(markerX, markerY, markerRadius, markerShadowPaint)
        canvas.drawCircle(markerX, markerY, markerRadius, markerStrokePaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.action) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_MOVE -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                setMarkerFromTouch(event.x, event.y)
                return true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
        }
        return super.onTouchEvent(event)
    }

    private fun setMarkerFromTouch(x: Float, y: Float) {
        val dx = x - centerX
        val dy = y - centerY
        val distance = hypot(dx, dy).coerceIn(0f, maxRadius)
        val angle = atan2(dy, dx)
        markerX = centerX + distance * cos(angle)
        markerY = centerY + distance * sin(angle)
        invalidate()
        onColorChanged?.invoke(colorAt(markerX, markerY))
    }

    private fun colorAt(x: Float, y: Float): Int {
        val dx = x - centerX
        val dy = y - centerY
        var hue = Math.toDegrees(atan2(dy, dx).toDouble()).toFloat()
        if (hue < 0f) hue += 360f
        val saturation = (hypot(dx, dy) / maxRadius).coerceIn(0f, 1f)
        return Color.HSVToColor(floatArrayOf(hue, saturation, 1f))
    }

    /** يضبط موضع المؤشر ليطابق لوناً مُعطى مسبقاً (مثلاً عند فتح الحوار بلون مختار سلفاً). */
    fun setColor(color: Int) {
        if (maxRadius <= 0f) {
            // اللوحة لم تُقَس بعد (onSizeChanged لم يُستدعَ)؛ نؤجّل التطبيق لحين توفر الحجم.
            pendingColor = color
            return
        }
        positionMarkerForColor(color)
    }

    private fun positionMarkerForColor(color: Int) {
        val hsv = FloatArray(3)
        Color.colorToHSV(color, hsv)
        val hueRad = Math.toRadians(hsv[0].toDouble())
        val distance = hsv[1].coerceIn(0f, 1f) * maxRadius
        markerX = centerX + (distance * cos(hueRad)).toFloat()
        markerY = centerY + (distance * sin(hueRad)).toFloat()
        invalidate()
    }
}

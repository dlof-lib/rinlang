package com.dlof.rinlang.widgets

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Matrix
import android.util.AttributeSet
import android.view.View
import androidx.core.graphics.PathParser

/**
 * يرسم أيقونة indsin (مسار SVG على شبكة 24×24، خط سماكته 2 وأطراف دائرية) بحجم الـView.
 * الاستعمال: setIcon(icon.path) — أو setIconPath(d). اللون عبر setIconColor (الافتراضي أسود).
 */
class IndsinIconView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
        color = 0xFF1C2128.toInt()
    }
    private val fillPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private var source: Path? = null
    private val drawn = Path()
    private val matrix = Matrix()
    private var strokeUnits = 2f
    private var filled = false

    fun setIconPath(d: String?) {
        source = d?.let { runCatching { PathParser.createPathFromPathData(it) }.getOrNull() }
        invalidate()
    }

    fun setIcon(icon: IndsinIcons.Icon?) {
        filled = false
        setIconPath(icon?.path)
    }

    fun setIconColor(color: Int) {
        paint.color = color
        fillPaint.color = color
        invalidate()
    }

    /** سماكة الخط بوحدات الشبكة (الافتراضي 2). */
    fun setStrokeUnits(units: Float) {
        strokeUnits = units
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val p = source ?: return
        val w = width - paddingLeft - paddingRight
        val h = height - paddingTop - paddingBottom
        val side = minOf(w, h).toFloat()
        if (side <= 0f) return
        val scale = side / 24f
        matrix.reset()
        matrix.setScale(scale, scale)
        matrix.postTranslate(paddingLeft + (w - side) / 2f, paddingTop + (h - side) / 2f)
        drawn.reset()
        p.transform(matrix, drawn)
        paint.strokeWidth = strokeUnits * scale
        canvas.drawPath(drawn, paint)
    }
}

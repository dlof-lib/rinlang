package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.util.AttributeSet
import android.view.View

/** معاينة خفيفة وفورية لخيارات UI أثناء إنشاء المشروع، بدون الاعتماد على محرك Rin. */
class UiDesignPreviewView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null
) : View(context, attrs) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private var primary = Color.rgb(124, 92, 255)
    private var background = Color.rgb(15, 15, 20)
    private var text = Color.rgb(245, 245, 247)
    private var topBar = true
    private var sidebar = true
    private var bottomNav = false
    private var buttonStyle = "filled"
    private var radius = 14f
    private var typeface = Typeface.DEFAULT
    private var textScale = 1f

    fun configure(primary: Int, background: Int, text: Int, topBar: Boolean, sidebar: Boolean,
                  bottomNav: Boolean, buttonStyle: String, radius: Int, font: String, typography: String) {
        this.primary = primary; this.background = background; this.text = text
        this.topBar = topBar; this.sidebar = sidebar; this.bottomNav = bottomNav
        this.buttonStyle = buttonStyle; this.radius = radius.toFloat().coerceIn(0f, 30f)
        this.typeface = when (font) {
            "serif" -> Typeface.SERIF
            "mono" -> Typeface.MONOSPACE
            else -> Typeface.DEFAULT
        }
        this.textScale = when (typography) { "small" -> .85f; "large" -> 1.18f; else -> 1f }
        invalidate()
    }

    override fun onDraw(c: Canvas) {
        super.onDraw(c)
        val w = width.toFloat(); val h = height.toFloat()
        paint.style = Paint.Style.FILL; paint.color = background
        c.drawRoundRect(RectF(0f, 0f, w, h), 18f, 18f, paint)
        var top = 0f
        if (topBar) {
            paint.color = blend(background, primary, .10f); c.drawRect(0f, 0f, w, 48f, paint)
            paint.color = primary; c.drawCircle(24f, 24f, 12f, paint)
            label(c, "مشروعي", 46f, 30f, 15f * textScale, true)
            top = 48f
        }
        val bottom = if (bottomNav) 54f else 0f
        val contentBottom = h - bottom
        val side = if (sidebar) minOf(92f, w * .27f) else 0f
        if (sidebar) {
            paint.color = blend(background, Color.WHITE, .035f); c.drawRect(0f, top, side, contentBottom, paint)
            button(c, 10f, top + 14f, side - 20f, 34f, "القائمة")
            button(c, 10f, top + 56f, side - 20f, 30f, "الرئيسية")
            button(c, 10f, top + 94f, side - 20f, 30f, "الإعدادات")
        }
        val left = side + 12f; val cw = w - left - 12f
        card(c, left, top + 14f, cw, 64f)
        label(c, "واجهة UI احترافية", left + 12f, top + 40f, 13f * textScale, true)
        card(c, left, top + 88f, cw, 70f)
        label(c, "بطاقة محتوى", left + 12f, top + 115f, 12f * textScale, false)
        button(c, left + 12f, top + 128f, minOf(100f, cw - 24f), 24f, "زر")
        if (bottomNav) {
            paint.color = blend(background, Color.WHITE, .045f); c.drawRect(0f, h - bottom, w, h, paint)
            label(c, "الرئيسية", 22f, h - 21f, 10f * textScale, true)
            label(c, "بحث", w / 2f - 10f, h - 21f, 10f * textScale, false)
            label(c, "حسابي", w - 50f, h - 21f, 10f * textScale, false)
        }
    }

    private fun card(c: Canvas, l: Float, t: Float, w: Float, h: Float) {
        paint.style = Paint.Style.FILL; paint.color = blend(background, Color.WHITE, .055f)
        c.drawRoundRect(RectF(l, t, l + w, t + h), radius, radius, paint)
    }

    private fun button(c: Canvas, l: Float, t: Float, w: Float, h: Float, value: String) {
        paint.style = if (buttonStyle == "outline") Paint.Style.STROKE else Paint.Style.FILL
        paint.strokeWidth = 1.5f
        paint.color = when (buttonStyle) { "soft" -> blend(primary, background, .72f); else -> primary }
        c.drawRoundRect(RectF(l, t, l + w, t + h), radius.coerceAtMost(12f), radius.coerceAtMost(12f), paint)
        paint.style = Paint.Style.FILL
        label(c, value, l + 8f, t + h / 2f + 4f, 9f * textScale, true, w - 16f)
    }

    private fun label(c: Canvas, value: String, x: Float, y: Float, size: Float, bold: Boolean, max: Float = 1000f) {
        paint.color = text; paint.textSize = size; paint.typeface = Typeface.create(typeface, if (bold) Typeface.BOLD else Typeface.NORMAL)
        c.drawText(value.take(22), x, y, paint)
    }

    private fun blend(a: Int, b: Int, amount: Float): Int {
        val f = amount.coerceIn(0f, 1f)
        return Color.rgb((Color.red(a) * (1-f) + Color.red(b)*f).toInt(), (Color.green(a) * (1-f) + Color.green(b)*f).toInt(), (Color.blue(a) * (1-f) + Color.blue(b)*f).toInt())
    }
}

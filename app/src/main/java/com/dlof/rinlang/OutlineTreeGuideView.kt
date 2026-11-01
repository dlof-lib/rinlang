package com.dlof.rinlang

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View
import androidx.core.graphics.ColorUtils

/**
 * خطوط إرشاد شجرية (tree guide lines) لصف واحد في حوار "بنية الملف": خط رأسي رمادي خفيف لكل
 * مستوى سلف ما زال له أشقاء لاحقون في الملف، وخط زاوية بلون مصنَّف الوسم نفسه (متابع إن كان
 * هناك شقيق لاحق في نفس المستوى، أو منتهٍ عند المنتصف إن كان آخر شقيق) يربط هذا الصف بعمود
 * شارته — بديل بصري حقيقي عن المسافات البادئة النصية المسطّحة السابقة
 * ("    ".repeat(entry.depth))، بحيث يُقرأ عمق تعشيش الحاويات كشجرة فعلية دفعة واحدة بدل عدّ
 * المسافات يدوياً. الخطوط الرأسية لصفوف متتالية بنفس الارتفاع الثابت (minHeight الصف) تتّصل
 * بصرياً تلقائياً لتُكوِّن أعمدة شجرة متواصلة عبر القائمة.
 *
 * [setDepth] تضبط عرض المقياس الخاص بها نفسها (unit لكل مستوى) — لا حاجة لأي منطق قياس خارجي.
 */
class OutlineTreeGuideView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val unitPx = 20f * resources.displayMetrics.density

    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        strokeWidth = 1.5f * resources.displayMetrics.density
        style = Paint.Style.STROKE
    }

    private var depth = 0
    private var ancestorContinues = BooleanArray(0)
    private var isLastChild = true
    private var branchColor = Color.GRAY

    /**
     * [depth]: عمق تعشيش هذا الصف (0 = وسم جذر بلا أي خط إرشاد).
     * [ancestorContinues]: لكل مستوى k في 0 حتى depth-1، هل ما زال لسلف ذلك المستوى شقيق لاحق
     * (فيُرسَم خط رأسي متواصل)، أم انتهت فروعه (فلا يُرسَم شيء عند ذلك المستوى).
     * [isLastChild]: هل هذا الصف آخر شقيق في مستواه هو (يحدّد شكل زاوية فرعه الخاص).
     * [accentColor]: لون فرع هذا الصف تحديداً، مطابق للون شارة نوع الوسم المصنَّف.
     */
    fun setDepth(depth: Int, ancestorContinues: BooleanArray, isLastChild: Boolean, accentColor: Int) {
        this.depth = depth
        this.ancestorContinues = ancestorContinues
        this.isLastChild = isLastChild
        this.branchColor = accentColor

        val widthPx = (unitPx * depth).toInt()
        val currentParams = layoutParams
        if (currentParams != null && currentParams.width != widthPx) {
            currentParams.width = widthPx
            layoutParams = currentParams
        }
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (depth <= 0) return

        val isRtl = layoutDirection == LAYOUT_DIRECTION_RTL
        val w = width.toFloat()
        val h = height.toFloat()

        fun xAt(level: Int): Float {
            val ltrX = unitPx * level + unitPx / 2f
            return if (isRtl) w - ltrX else ltrX
        }

        // أعمدة الأسلاف المستمرة (كل مستوى أب أعلى من هذا الصف ما زال له أشقاء لاحقون تحت هذا
        // الصف)، بلون رمادي خافت موحّد بصرف النظر عن نوع كل سلف — هيكل الشجرة الصامت.
        linePaint.color = ColorUtils.setAlphaComponent(Color.GRAY, 90)
        for (level in 0 until depth - 1) {
            if (level < ancestorContinues.size && ancestorContinues[level]) {
                val x = xAt(level)
                canvas.drawLine(x, 0f, x, h, linePaint)
            }
        }

        // فرع هذا الصف نفسه: عمودي من الأعلى حتى المنتصف (أو حتى الأسفل إن لم يكن آخر شقيق،
        // ليتّصل بصف الشقيق التالي)، ثم أفقي حتى عمود الشارة — بلون نوع الوسم المصنَّف، ليُقرأ
        // الفرع بأكمله كوحدة بصرية واحدة تنتهي عند الشارة الملوّنة.
        linePaint.color = branchColor
        val ownLevel = depth - 1
        val x = xAt(ownLevel)
        val midY = h / 2f
        val vEnd = if (isLastChild) midY else h
        canvas.drawLine(x, 0f, x, vEnd, linePaint)
        val endX = if (isRtl) 0f else w
        canvas.drawLine(x, midY, endX, midY, linePaint)
    }
}

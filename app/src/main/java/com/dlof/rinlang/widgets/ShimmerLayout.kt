package com.dlof.rinlang.widgets

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
import android.view.animation.LinearInterpolator
import android.widget.LinearLayout
import androidx.core.content.ContextCompat
import com.dlof.rinlang.R

/**
 * حاوية "تحميل هيكلي" (skeleton loading): تُرتَّب بلوكات رمادية (خلفية bg_skeleton_block/
 * bg_skeleton_circle) بداخلها بشكل عمودي كأي LinearLayout عادي، وهذا الصف يرسم فوقها
 * شريط لمعان (shimmer) متحرك من جهة لأخرى، بنفس أسلوب هياكل التحميل في التطبيقات
 * الاحترافية (Facebook/YouTube/إلخ) — بلا أي مكتبة خارجية.
 *
 * الاستخدام في XML:
 * ```
 * <com.dlof.rinlang.widgets.ShimmerLayout
 *     android:layout_width="match_parent"
 *     android:layout_height="wrap_content"
 *     android:orientation="vertical">
 *     <include layout="@layout/layout_skeleton_store_package" />
 * </com.dlof.rinlang.widgets.ShimmerLayout>
 * ```
 * ثم في الكود: `shimmerLayout.startShimmer()` عند بدء التحميل، و `stopShimmer()` عند الانتهاء.
 */
class ShimmerLayout @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : LinearLayout(context, attrs, defStyleAttr) {

    private val shimmerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        xfermode = PorterDuffXfermode(PorterDuff.Mode.SRC_IN)
    }
    private val gradientMatrix = Matrix()
    private var gradient: LinearGradient? = null
    private var shimmerWidth = 0f
    private var translateX = 0f
    private var animator: ValueAnimator? = null
    private var isShimmering = false

    init {
        setWillNotDraw(false)
        setLayerType(LAYER_TYPE_HARDWARE, null)
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w <= 0) return
        shimmerWidth = w * 0.7f
        val baseColor = ContextCompat.getColor(context, R.color.rin_skeleton_base)
        val highlightColor = ContextCompat.getColor(context, R.color.rin_skeleton_highlight)
        gradient = LinearGradient(
            0f, 0f, shimmerWidth, 0f,
            intArrayOf(baseColor, highlightColor, baseColor),
            floatArrayOf(0f, 0.5f, 1f),
            Shader.TileMode.CLAMP
        )
        shimmerPaint.shader = gradient
        if (isShimmering) restartAnimatorForCurrentSize()
    }

    override fun dispatchDraw(canvas: Canvas) {
        val g = gradient
        if (!isShimmering || g == null || width <= 0 || height <= 0) {
            super.dispatchDraw(canvas)
            return
        }
        val saveCount = canvas.saveLayer(0f, 0f, width.toFloat(), height.toFloat(), null)
        super.dispatchDraw(canvas)
        gradientMatrix.setTranslate(translateX, 0f)
        g.setLocalMatrix(gradientMatrix)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), shimmerPaint)
        canvas.restoreToCount(saveCount)
    }

    /** يبدأ حركة اللمعان. يتكرّر تلقائياً حتى استدعاء [stopShimmer]. */
    fun startShimmer() {
        isShimmering = true
        visibility = View.VISIBLE
        if (width > 0) restartAnimatorForCurrentSize()
    }

    /** يوقف الحركة ويعيد رسم المحتوى بشكل ثابت (يُستدعى تلقائياً أيضاً عند الفصل عن الشاشة). */
    fun stopShimmer() {
        isShimmering = false
        animator?.cancel()
        animator = null
        invalidate()
    }

    private fun restartAnimatorForCurrentSize() {
        animator?.cancel()
        val isRtl = layoutDirection == LAYOUT_DIRECTION_RTL
        val start = if (isRtl) width + shimmerWidth else -shimmerWidth
        val end = if (isRtl) -shimmerWidth else width + shimmerWidth
        animator = ValueAnimator.ofFloat(start, end).apply {
            duration = 1200L
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener {
                translateX = it.animatedValue as Float
                invalidate()
            }
            start()
        }
    }

    override fun onDetachedFromWindow() {
        stopShimmer()
        super.onDetachedFromWindow()
    }
}

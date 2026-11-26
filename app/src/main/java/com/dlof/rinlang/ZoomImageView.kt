package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Matrix
import android.graphics.RectF
import android.util.AttributeSet
import android.view.GestureDetector
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.animation.DecelerateInterpolator
import androidx.appcompat.widget.AppCompatImageView
import kotlin.math.abs
import kotlin.math.min

/**
 * عارض صورة قابل للتكبير: قرص بإصبعين، نقرتان للتبديل بين الملاءمة وتكبير 2.5×، تحريك بإصبع،
 * تدوير 90°. عند الملاءمة: سحب أفقي سريع = [onSwipe] (+1 التالي / -1 السابق)، وسحب لأسفل = [onDismiss].
 * نقرة واحدة = [onTap]. [onZoom] تُعيد نسبة التكبير % مقارنة بالملاءمة.
 */
class ZoomImageView @JvmOverloads constructor(ctx: Context, attrs: AttributeSet? = null) :
    AppCompatImageView(ctx, attrs) {

    var onTap: (() -> Unit)? = null
    var onSwipe: ((Int) -> Unit)? = null
    var onDismiss: (() -> Unit)? = null
    var onZoom: ((Int) -> Unit)? = null

    private val m = Matrix()
    private var cw = 0f   // أبعاد المحتوى (صورة ثابتة أو متحرّكة)
    private var ch = 0f
    private var rot = 0
    private var baseScale = 1f
    private var animator: ValueAnimator? = null

    init { scaleType = ScaleType.MATRIX }

    private val scaleDetector = ScaleGestureDetector(ctx, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(d: ScaleGestureDetector): Boolean {
            animator?.cancel()
            zoomBy(d.scaleFactor, d.focusX, d.focusY)
            return true
        }
    })

    private val gestures = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
        override fun onDown(e: MotionEvent): Boolean = true
        override fun onSingleTapConfirmed(e: MotionEvent): Boolean { onTap?.invoke(); return true }
        override fun onDoubleTap(e: MotionEvent): Boolean { toggleZoom(e.x, e.y); return true }
        override fun onScroll(e1: MotionEvent?, e2: MotionEvent, dx: Float, dy: Float): Boolean {
            if (!scaleDetector.isInProgress) { animator?.cancel(); m.postTranslate(-dx, -dy); apply() }
            return true
        }
        override fun onFling(e1: MotionEvent?, e2: MotionEvent, vx: Float, vy: Float): Boolean {
            if (curScale() > baseScale * 1.02f) return false
            if (abs(vx) > 900f && abs(vx) > abs(vy) * 1.5f) { onSwipe?.invoke(if (vx < 0) 1 else -1); return true }
            if (vy > 1600f && vy > abs(vx) * 1.5f) { onDismiss?.invoke(); return true }
            return false
        }
    })

    fun setBitmap(b: Bitmap?) {
        cw = b?.width?.toFloat() ?: 0f; ch = b?.height?.toFloat() ?: 0f; rot = 0
        setImageBitmap(b)
        fit()
    }

    /** صورة متحرّكة (GIF / WebP متحرك عبر AnimatedImageDrawable): نفس التكبير والتحريك والتدوير. */
    fun setAnimated(d: android.graphics.drawable.Drawable) {
        cw = d.intrinsicWidth.toFloat(); ch = d.intrinsicHeight.toFloat(); rot = 0
        setImageDrawable(d)
        fit()
    }

    fun rotate90() { rot = (rot + 90) % 360; fit() }

    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) { super.onSizeChanged(w, h, ow, oh); fit() }

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        scaleDetector.onTouchEvent(ev)
        gestures.onTouchEvent(ev)
        return true
    }

    private fun curScale(): Float = m.mapRadius(1f)

    private fun fit() {
        if (cw <= 0f || ch <= 0f || width == 0 || height == 0) return
        val bw = cw; val bh = ch
        val swap = rot % 180 != 0
        val rw = if (swap) bh else bw; val rh = if (swap) bw else bh
        baseScale = min(width / rw, height / rh)
        m.reset()
        m.postTranslate(-bw / 2f, -bh / 2f)
        m.postRotate(rot.toFloat())
        m.postScale(baseScale, baseScale)
        m.postTranslate(width / 2f, height / 2f)
        apply()
    }

    private fun apply() {
        fixBounds()
        imageMatrix = m
        onZoom?.invoke((curScale() / baseScale * 100f).toInt())
    }

    private fun fixBounds() {
        if (cw <= 0f || ch <= 0f) return
        val r = RectF(0f, 0f, cw, ch)
        m.mapRect(r)
        var dx = 0f; var dy = 0f
        if (r.width() <= width) dx = (width - r.width()) / 2f - r.left
        else if (r.left > 0f) dx = -r.left else if (r.right < width) dx = width - r.right
        if (r.height() <= height) dy = (height - r.height()) / 2f - r.top
        else if (r.top > 0f) dy = -r.top else if (r.bottom < height) dy = height - r.bottom
        m.postTranslate(dx, dy)
    }

    private fun zoomBy(factor: Float, fx: Float, fy: Float) {
        val cur = curScale()
        var f = factor
        val target = cur * f
        val lo = baseScale * 0.9f; val hi = baseScale * 8f
        if (target > hi) f = hi / cur else if (target < lo) f = lo / cur
        m.postScale(f, f, fx, fy)
        apply()
    }

    private fun toggleZoom(x: Float, y: Float) {
        animator?.cancel()
        val start = curScale()
        val end = if (start > baseScale * 1.2f) baseScale else baseScale * 2.5f
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 220L
            interpolator = DecelerateInterpolator()
            addUpdateListener {
                val s = start + (end - start) * (it.animatedValue as Float)
                zoomBy(s / curScale(), x, y)
            }
            start()
        }
    }
}

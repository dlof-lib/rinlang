package com.dlof.rinlang

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.*
import android.graphics.drawable.GradientDrawable
import android.view.View
import android.view.animation.DecelerateInterpolator
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import androidx.core.content.ContextCompat
import kotlin.math.min

/**
 * Rin's native loading surface.
 *
 * One reusable component for startup, project operations, package loading,
 * editor tasks, native execution and any other long-running operation.
 * The logo animation is deliberately drawn/animated here instead of relying
 * on a platform ProgressBar so the loading language stays visually identical
 * throughout Rin.
 */
class RinLoadingView @JvmOverloads constructor(
    context: Context,
    attrs: android.util.AttributeSet? = null
) : FrameLayout(context, attrs) {

    private val logo = ImageView(context)
    private val animation = RinLoadingAnimationView(context)
    private val title = TextView(context)
    private val stage = TextView(context)
    private val progressText = TextView(context)
    private val progressTrack = View(context)
    private val progressFill = View(context)
    private val detail = TextView(context)

    private var progress = -1
    private var running = false
    private var pulseAnimator: ValueAnimator? = null
    private var progressAnimator: ValueAnimator? = null

    init {
        isClickable = true
        isFocusable = true
        setBackgroundColor(ContextCompat.getColor(context, R.color.rin_background))
        alpha = 1f

        val content = FrameLayout(context)
        content.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
        addView(content)

        animation.layoutParams = LayoutParams(dp(156), dp(156), android.view.Gravity.CENTER).apply {
            bottomMargin = dp(112)
        }
        content.addView(animation)

        logo.setImageResource(R.drawable.rin_logo_loading)
        logo.scaleType = ImageView.ScaleType.CENTER_INSIDE
        logo.layoutParams = LayoutParams(dp(112), dp(148), android.view.Gravity.CENTER).apply {
            bottomMargin = dp(112)
        }
        content.addView(logo)

        title.textSize = 22f
        title.setTypeface(null, android.graphics.Typeface.BOLD)
        title.gravity = android.view.Gravity.CENTER
        title.setTextColor(ContextCompat.getColor(context, R.color.rin_editor_text))
        title.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(34), android.view.Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(108)
            marginStart = dp(28)
            marginEnd = dp(28)
        }
        content.addView(title)

        stage.textSize = 13f
        stage.gravity = android.view.Gravity.CENTER
        stage.setTextColor(ContextCompat.getColor(context, R.color.rin_editor_hint))
        stage.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(28), android.view.Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(146)
            marginStart = dp(28)
            marginEnd = dp(28)
        }
        content.addView(stage)

        progressTrack.background = rounded(R.color.rin_skeleton_base, 8)
        progressTrack.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(7), android.view.Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(188)
            marginStart = dp(46)
            marginEnd = dp(46)
        }
        content.addView(progressTrack)

        progressFill.background = gradientProgress()
        progressFill.layoutParams = LayoutParams(0, dp(7), android.view.Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(188)
            marginStart = dp(46)
        }
        content.addView(progressFill)

        progressText.textSize = 12f
        progressText.gravity = android.view.Gravity.CENTER
        progressText.setTextColor(ContextCompat.getColor(context, R.color.rin_editor_hint))
        progressText.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(24), android.view.Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(200)
            marginStart = dp(46)
            marginEnd = dp(46)
        }
        content.addView(progressText)

        detail.textSize = 11.5f
        detail.gravity = android.view.Gravity.CENTER
        detail.setTextColor(ContextCompat.getColor(context, R.color.rin_editor_hint))
        detail.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(28), android.view.Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(226)
            marginStart = dp(32)
            marginEnd = dp(32)
        }
        content.addView(detail)

        visibility = View.GONE
    }

    fun start(titleText: CharSequence, stageText: CharSequence, initialProgress: Int = -1, detailText: CharSequence = "") {
        title.text = titleText
        stage.text = stageText
        detail.text = detailText
        setProgress(initialProgress, animate = false)
        visibility = View.VISIBLE
        running = true
        alpha = 0f
        animate().alpha(1f).setDuration(180L).setInterpolator(DecelerateInterpolator()).start()
        animation.start()
        startPulse()
    }

    fun setStage(stageText: CharSequence, detailText: CharSequence = "") {
        stage.text = stageText
        detail.text = detailText
    }

    fun setProgress(value: Int, animate: Boolean = true) {
        val target = value.coerceIn(-1, 100)
        val old = progress
        progress = target
        if (target < 0) {
            progressText.text = context.getString(R.string.rin_loading_working)
            progressFill.layoutParams.width = 0
            progressFill.requestLayout()
            return
        }
        progressText.text = context.getString(R.string.rin_loading_percent, target)
        val width = progressTrack.width
        if (width <= 0) {
            progressFill.layoutParams.width = 0
            progressFill.requestLayout()
            progressFill.post { applyProgressWidth(target / 100f) }
            return
        }
        if (!animate || old < 0) applyProgressWidth(target / 100f)
        else {
            progressAnimator?.cancel()
            progressAnimator = ValueAnimator.ofFloat(old / 100f, target / 100f).apply {
                duration = 240L
                interpolator = DecelerateInterpolator()
                addUpdateListener { applyProgressWidth(it.animatedValue as Float) }
                start()
            }
        }
    }

    fun stop(immediate: Boolean = false) {
        if (!running) return
        running = false
        pulseAnimator?.cancel()
        progressAnimator?.cancel()
        animation.stop()
        if (immediate) {
            visibility = View.GONE
            alpha = 1f
            return
        }
        animate().alpha(0f).setDuration(160L).withEndAction {
            visibility = View.GONE
            alpha = 1f
        }.start()
    }

    private fun applyProgressWidth(fraction: Float) {
        val max = progressTrack.width
        val lp = progressFill.layoutParams
        lp.width = (max * fraction.coerceIn(0f, 1f)).toInt()
        progressFill.layoutParams = lp
    }

    private fun startPulse() {
        pulseAnimator?.cancel()
        pulseAnimator = ValueAnimator.ofFloat(0.92f, 1.04f).apply {
            duration = 900L
            repeatMode = ValueAnimator.REVERSE
            repeatCount = ValueAnimator.INFINITE
            addUpdateListener {
                val s = it.animatedValue as Float
                logo.scaleX = s
                logo.scaleY = s
            }
            start()
        }
    }

    private fun rounded(color: Int, radiusDp: Int): GradientDrawable =
        GradientDrawable().apply {
            shape = GradientDrawable.RECTANGLE
            cornerRadius = dp(radiusDp).toFloat()
            setColor(ContextCompat.getColor(context, color))
        }

    private fun gradientProgress(): GradientDrawable =
        GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, intArrayOf(
            ContextCompat.getColor(context, R.color.rin_gradient_green_start),
            ContextCompat.getColor(context, R.color.rin_gradient_green_end)
        )).apply { cornerRadius = dp(8).toFloat() }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()
}

/** Draws the animated Rin loading ring and soft logo aura. */
private class RinLoadingAnimationView(context: Context) : View(context) {
    private val ringPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(4f)
        strokeCap = Paint.Cap.ROUND
        color = ContextCompat.getColor(context, R.color.rin_accent_green)
    }
    private val glowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = dp(1.5f)
        color = ContextCompat.getColor(context, R.color.rin_accent_green_dim)
    }
    private var animator: ValueAnimator? = null
    private var sweep = 0f
    private var rotation = 0f

    fun start() {
        animator?.cancel()
        animator = ValueAnimator.ofFloat(0f, 360f).apply {
            duration = 1300L
            repeatCount = ValueAnimator.INFINITE
            interpolator = android.view.animation.LinearInterpolator()
            addUpdateListener {
                rotation = it.animatedValue as Float
                sweep = (rotation * 1.8f) % 360f
                invalidate()
            }
            start()
        }
    }

    fun stop() {
        animator?.cancel()
        animator = null
        rotation = 0f
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = width / 2f
        val cy = height / 2f
        val radius = min(width, height) * 0.42f
        val oval = RectF(cx - radius, cy - radius, cx + radius, cy + radius)
        canvas.drawArc(oval, rotation - 65f, 250f, false, glowPaint)
        canvas.drawArc(oval, rotation, 105f, false, ringPaint)
    }

    private fun dp(v: Float) = v * resources.displayMetrics.density
}

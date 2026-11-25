package com.dlof.rinlang

import android.content.Context
import android.graphics.Color
import android.view.Gravity
import android.view.View
import android.view.animation.DecelerateInterpolator
import android.widget.FrameLayout
import android.widget.TextView

/**
 * مؤشر التحميل الرسمي لكامل الشاشة في RinStudio.
 *
 * سطح واحد قابل لإعادة الاستخدام للإقلاع، عمليات المشاريع، تحميل الحزم، مهام المحرر، والتنفيذ
 * الأصلي. يعرض [RinJellyLoaderView] (قنديل البحر يسبح مع هالة خضراء) فوق خلفية سوداء، ثم عنوان
 * أبيض، المرحلة الحالية، وشريط تقدّم أخضر ([RinProgressBar]) مع النسبة. الألوان: أسود · أبيض · أخضر.
 * الواجهة العامة (start / setStage / setProgress / stop) كما كانت، فلا يلزم تعديل المستدعين.
 */
class RinLoadingView @JvmOverloads constructor(
    context: Context,
    attrs: android.util.AttributeSet? = null
) : FrameLayout(context, attrs) {

    private val jelly = RinJellyLoaderView(context)
    private val title = TextView(context)
    private val stage = TextView(context)
    private val progressText = TextView(context)
    private val progressBar = RinProgressBar(context)
    private val detail = TextView(context)

    private var progress = -1
    private var running = false

    init {
        isClickable = true
        isFocusable = true
        setBackgroundColor(RinBrand.BLACK)

        val content = FrameLayout(context)
        content.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT)
        addView(content)

        jelly.layoutParams = LayoutParams(dp(176), dp(196), Gravity.CENTER).apply { bottomMargin = dp(120) }
        content.addView(jelly)

        title.textSize = 22f
        title.setTypeface(null, android.graphics.Typeface.BOLD)
        title.gravity = Gravity.CENTER
        title.letterSpacing = 0.12f
        title.setTextColor(RinBrand.GREEN)
        title.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(34), Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(112); marginStart = dp(28); marginEnd = dp(28)
        }
        content.addView(title)

        stage.textSize = 13f
        stage.gravity = Gravity.CENTER
        stage.setTextColor(RinBrand.WHITE)
        stage.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(28), Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(150); marginStart = dp(28); marginEnd = dp(28)
        }
        content.addView(stage)

        progressBar.setTrackColor(Color.argb(46, 255, 255, 255))
        progressBar.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(6), Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(192); marginStart = dp(56); marginEnd = dp(56)
        }
        content.addView(progressBar)

        progressText.textSize = 12f
        progressText.gravity = Gravity.CENTER
        progressText.setTextColor(RinBrand.GREEN)
        progressText.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(24), Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(204); marginStart = dp(46); marginEnd = dp(46)
        }
        content.addView(progressText)

        detail.textSize = 11.5f
        detail.gravity = Gravity.CENTER
        detail.setTextColor(Color.argb(140, 255, 255, 255))
        detail.layoutParams = LayoutParams(LayoutParams.MATCH_PARENT, dp(28), Gravity.CENTER_HORIZONTAL).apply {
            topMargin = dp(230); marginStart = dp(32); marginEnd = dp(32)
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
        jelly.start()
    }

    fun setStage(stageText: CharSequence, detailText: CharSequence = "") {
        stage.text = stageText
        detail.text = detailText
    }

    fun setProgress(value: Int, animate: Boolean = true) {
        val target = value.coerceIn(-1, 100)
        progress = target
        if (target < 0) {
            progressText.text = context.getString(R.string.rin_loading_working)
            progressBar.setIndeterminate(true)
            return
        }
        progressText.text = context.getString(R.string.rin_loading_percent, target)
        progressBar.setProgress(target, animate)
    }

    fun stop(immediate: Boolean = false) {
        if (!running) return
        running = false
        if (immediate) {
            jelly.stop()
            visibility = View.GONE
            alpha = 1f
            return
        }
        animate().alpha(0f).setDuration(160L).withEndAction {
            jelly.stop()
            visibility = View.GONE
            alpha = 1f
        }.start()
    }

    private fun dp(v: Int): Int = (v * resources.displayMetrics.density).toInt()
}

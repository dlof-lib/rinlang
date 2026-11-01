package com.dlof.rinlang

import android.app.Activity
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.view.Gravity
import android.view.ViewGroup
import android.widget.FrameLayout

/** Central editor loading overlay. Any editor operation can reuse the same RIN logo redraw UI. */
object RinLogoLoadingOverlay {
    private var host: FrameLayout? = null
    private var view: RinLogoLoadingView? = null

    fun show(activity: Activity, autoFinishMs: Long = 1100L) {
        hide()
        val decor = activity.window.decorView as? ViewGroup ?: return
        val container = FrameLayout(activity).apply {
            setBackgroundColor(Color.argb(235, 4, 8, 10))
            foreground = ColorDrawable(Color.TRANSPARENT)
            elevation = 50f
        }
        val logo = RinLogoLoadingView(activity)
        container.addView(logo, FrameLayout.LayoutParams(-1, -1, Gravity.CENTER))
        decor.addView(container, ViewGroup.LayoutParams(-1, -1))
        host = container
        view = logo
        logo.start(autoFinishMs)
        if (autoFinishMs > 0) {
            container.postDelayed({ hide() }, autoFinishMs + 180L)
        }
    }

    fun setProgress(value: Float) { view?.setProgress(value) }

    fun finish() {
        view?.stop()
        view?.postDelayed({ hide() }, 160L)
    }

    fun hide() {
        val h = host ?: return
        (h.parent as? ViewGroup)?.removeView(h)
        host = null
        view = null
    }
}

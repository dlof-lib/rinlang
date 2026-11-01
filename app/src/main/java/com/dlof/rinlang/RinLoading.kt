package com.dlof.rinlang

import android.app.Activity
import android.os.Handler
import android.os.Looper
import android.view.ViewGroup
import java.util.WeakHashMap

/**
 * Global Rin loading API. Any screen can use the same loader without creating
 * its own ProgressBar/dialog. It is intentionally UI-thread safe and reusable.
 */
object RinLoading {
    private val main = Handler(Looper.getMainLooper())
    private val overlays = WeakHashMap<Activity, RinLoadingView>()

    fun show(
        activity: Activity,
        stage: CharSequence = activity.getString(R.string.rin_loading_preparing),
        progress: Int = -1,
        detail: CharSequence = ""
    ) {
        main.post {
            val root = activity.findViewById<ViewGroup>(android.R.id.content) ?: return@post
            val overlay = overlays[activity] ?: RinLoadingView(activity).also {
                overlays[activity] = it
                root.addView(it, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
            }
            overlay.bringToFront()
            overlay.start(activity.getString(R.string.rin_loading_title), stage, progress, detail)
        }
    }

    fun update(
        activity: Activity,
        progress: Int,
        stage: CharSequence? = null,
        detail: CharSequence? = null
    ) {
        main.post {
            val overlay = overlays[activity] ?: return@post
            if (stage != null || detail != null) {
                overlay.setStage(stage ?: "", detail ?: "")
            }
            overlay.setProgress(progress)
        }
    }

    fun hide(activity: Activity, immediate: Boolean = false) {
        main.post { overlays[activity]?.stop(immediate) }
    }

    /** Short branded startup animation; actual work remains on the caller's thread/task. */
    fun startup(activity: Activity, durationMs: Long = 850L) {
        show(activity, activity.getString(R.string.rin_loading_starting), 0, activity.getString(R.string.rin_loading_detail_starting))
        val steps = listOf(
            18 to R.string.rin_loading_stage_ui,
            42 to R.string.rin_loading_stage_editor,
            68 to R.string.rin_loading_stage_engine,
            88 to R.string.rin_loading_stage_ready,
            100 to R.string.rin_loading_stage_done
        )
        val slice = (durationMs / steps.size).coerceAtLeast(90L)
        steps.forEachIndexed { index, pair ->
            main.postDelayed({
                update(activity, pair.first, activity.getString(pair.second), "")
                if (index == steps.lastIndex) main.postDelayed({ hide(activity) }, 90L)
            }, slice * (index + 1))
        }
    }
}

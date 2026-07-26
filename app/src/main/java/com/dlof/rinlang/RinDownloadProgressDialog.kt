package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Typeface
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.core.content.ContextCompat

/** Determinate progress dialog driven by real bytes-copied / bytes-total from [RinDownloadManager]. */
class RinDownloadProgressDialog(activity: Activity, fileName: String) {

    private val dp = activity.resources.displayMetrics.density
    private val txtStatus: TextView
    private val txtBytes: TextView
    private val progressBar: ProgressBar
    private val dialog: AlertDialog

    init {
        val container = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((24 * dp).toInt(), (20 * dp).toInt(), (24 * dp).toInt(), (12 * dp).toInt())
        }

        val title = TextView(activity).apply {
            text = activity.getString(R.string.download_dialog_title)
            textSize = 16f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
        }
        container.addView(title)

        val fileNameView = TextView(activity).apply {
            text = fileName
            textSize = 12.5f
            typeface = Typeface.MONOSPACE
            setTextColor(ContextCompat.getColor(context, R.color.rin_editor_hint))
            setPadding(0, (4 * dp).toInt(), 0, (14 * dp).toInt())
        }
        container.addView(fileNameView)

        progressBar = ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal).apply {
            isIndeterminate = false
            max = 1000
            progress = 0
            progressTintList = android.content.res.ColorStateList.valueOf(
                ContextCompat.getColor(context, R.color.rin_accent)
            )
            progressBackgroundTintList = android.content.res.ColorStateList.valueOf(
                ContextCompat.getColor(context, R.color.rin_download_progress_track)
            )
        }
        container.addView(progressBar, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (10 * dp).toInt()))

        val row = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, (10 * dp).toInt(), 0, 0)
        }
        txtStatus = TextView(activity).apply {
            text = activity.getString(R.string.download_status_copying)
            textSize = 12.5f
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar_dim))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        txtBytes = TextView(activity).apply {
            text = "0 بايت"
            textSize = 12.5f
            typeface = Typeface.MONOSPACE
            gravity = Gravity.END
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
        }
        row.addView(txtStatus)
        row.addView(txtBytes)
        container.addView(row)

        dialog = AlertDialog.Builder(activity)
            .setView(container)
            .setCancelable(false)
            .create()
    }

    fun show() {
        dialog.show()
    }

    /** Updates the bar from real bytes copied so far vs. the artifact's true on-disk size. */
    fun updateProgress(copiedBytes: Long, totalBytes: Long) {
        val ratio = if (totalBytes > 0) copiedBytes.toDouble() / totalBytes.toDouble() else 0.0
        progressBar.progress = (ratio * 1000).toInt().coerceIn(0, 1000)
        val copiedStr = RinConsoleFormatter.formatBytes(copiedBytes)
        val totalStr = RinConsoleFormatter.formatBytes(totalBytes)
        txtBytes.text = "$copiedStr / $totalStr"
    }

    fun finishSuccess(message: String) {
        progressBar.progress = 1000
        txtStatus.text = message
        dialog.setCancelable(true)
    }

    fun finishError(message: String) {
        txtStatus.text = message
        txtStatus.setTextColor(ContextCompat.getColor(dialog.context, R.color.log_kind_error))
        dialog.setCancelable(true)
    }

    fun dismiss() {
        if (dialog.isShowing) dialog.dismiss()
    }
}

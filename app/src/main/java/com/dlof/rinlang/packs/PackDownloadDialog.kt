package com.dlof.rinlang.packs

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Typeface
import android.text.format.Formatter
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import com.dlof.rinlang.R
import com.dlof.rinlang.RinProgressBar

/**
 * مدخل واحد لأي ميزة تحتاج حزمة: [ensure] يضمن وجودها، فإن لم تكن مثبّتة يسأل المستخدم ثم ينزّلها
 * بتقدّم حقيقي ويستدعي [onReady] عند اكتمال التثبيت.
 */
object PackDownloadDialog {

    fun stageText(activity: Activity, stage: PackDownloader.Stage, done: Long, total: Long): String = when (stage) {
        PackDownloader.Stage.DOWNLOADING -> {
            val pct = if (total > 0) ((done * 100) / total).toInt().coerceIn(0, 100) else 0
            activity.getString(R.string.packs_downloading_format, pct)
        }
        PackDownloader.Stage.VERIFYING -> activity.getString(R.string.packs_verifying)
        PackDownloader.Stage.INSTALLING -> activity.getString(R.string.packs_installing)
    }

    /**
     * @param displayName اسم للعرض في الرسائل (افتراضياً عنوان الحزمة من الكتالوج).
     * @param onFailed يُستدعى عند الرفض/الإلغاء/الفشل (بعد إظهار رسالة مناسبة إن لزم).
     */
    fun ensure(
        activity: Activity,
        packId: String,
        displayName: String? = null,
        onFailed: (() -> Unit)? = null,
        onReady: () -> Unit
    ) {
        if (PackStore.isInstalled(activity, packId)) {
            onReady()
            return
        }
        // كتالوج طازج قدر الإمكان (يتراجع للنسخة المحفوظة دون اتصال)، فلا نُنزّل بتجزئة قديمة.
        PackCatalog.refresh(activity) { packs, error ->
            if (activity.isFinishing || activity.isDestroyed) return@refresh
            val pack = packs.firstOrNull { it.id == packId }
            if (pack == null) {
                val msg = if (error != null) activity.getString(R.string.packs_catalog_failed)
                else activity.getString(R.string.packs_error_unknown_pack)
                Toast.makeText(activity, msg, Toast.LENGTH_LONG).show()
                onFailed?.invoke()
                return@refresh
            }
            confirm(activity, pack, displayName ?: pack.title, onFailed, onReady)
        }
    }

    private fun confirm(activity: Activity, pack: ContentPack, name: String, onFailed: (() -> Unit)?, onReady: () -> Unit) {
        AlertDialog.Builder(activity)
            .setTitle(R.string.packs_need_title)
            .setMessage(activity.getString(R.string.packs_need_message_format, name, Formatter.formatShortFileSize(activity, pack.sizeBytes)))
            .setPositiveButton(activity.getString(R.string.packs_action_download, Formatter.formatShortFileSize(activity, pack.sizeBytes))) { _, _ ->
                download(activity, pack, name, onFailed, onReady)
            }
            .setNegativeButton(R.string.packs_action_cancel) { _, _ -> onFailed?.invoke() }
            .setOnCancelListener { onFailed?.invoke() }
            .show()
    }

    /** ينزّل [pack] مع حوار تقدّم؛ يُستدعى مباشرة (دون سؤال) من أزرار «تنزيل» الصريحة أيضاً. */
    fun download(activity: Activity, pack: ContentPack, name: String, onFailed: (() -> Unit)?, onReady: () -> Unit) {
        val dp = activity.resources.displayMetrics.density
        val container = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((24 * dp).toInt(), (20 * dp).toInt(), (24 * dp).toInt(), (8 * dp).toInt())
        }
        container.addView(TextView(activity).apply {
            text = activity.getString(R.string.packs_dialog_title_format, name)
            textSize = 16f
            setTypeface(typeface, Typeface.BOLD)
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
            setPadding(0, 0, 0, (14 * dp).toInt())
        })
        val bar = RinProgressBar(activity).apply { setProgress(0, animate = false) }
        container.addView(bar, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, (8 * dp).toInt()))
        val row = LinearLayout(activity).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, (10 * dp).toInt(), 0, 0)
        }
        val status = TextView(activity).apply {
            textSize = 12.5f
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar_dim))
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            text = activity.getString(R.string.packs_downloading_format, 0)
        }
        val bytes = TextView(activity).apply {
            textSize = 12.5f
            typeface = Typeface.MONOSPACE
            gravity = Gravity.END
            setTextColor(ContextCompat.getColor(context, R.color.rin_on_toolbar))
        }
        row.addView(status)
        row.addView(bytes)
        container.addView(row)

        var finished = false
        lateinit var listener: PackDownloader.Listener
        val dialog = AlertDialog.Builder(activity)
            .setView(container)
            .setCancelable(false)
            .setNegativeButton(R.string.packs_action_cancel, null) // يُربط يدوياً أدناه كي لا يُغلق الحوار تلقائياً
            .create()

        listener = object : PackDownloader.Listener {
            override fun onProgress(stage: PackDownloader.Stage, doneBytes: Long, totalBytes: Long) {
                if (finished) return
                status.text = stageText(activity, stage, doneBytes, totalBytes)
                if (stage == PackDownloader.Stage.DOWNLOADING) {
                    if (totalBytes > 0) bar.setProgress(((doneBytes * 100) / totalBytes).toInt().coerceIn(0, 100))
                    bytes.text = Formatter.formatShortFileSize(activity, doneBytes) + " / " + Formatter.formatShortFileSize(activity, totalBytes)
                } else {
                    bar.setProgress(100)
                }
            }

            override fun onSuccess(pack: ContentPack) {
                finished = true
                if (dialog.isShowing) dialog.dismiss()
                if (activity.isFinishing || activity.isDestroyed) return
                Toast.makeText(activity, activity.getString(R.string.packs_done_toast, name), Toast.LENGTH_SHORT).show()
                onReady()
            }

            override fun onError(pack: ContentPack, error: PackException) {
                finished = true
                if (dialog.isShowing) dialog.dismiss()
                if (activity.isFinishing || activity.isDestroyed) return
                Toast.makeText(activity, error.userMessage(activity), Toast.LENGTH_LONG).show()
                onFailed?.invoke()
            }
        }

        dialog.setOnDismissListener { PackDownloader.detach(pack.id, listener) }
        dialog.show()
        dialog.getButton(AlertDialog.BUTTON_NEGATIVE).setOnClickListener {
            PackDownloader.cancel(pack.id) // يُنهي التنزيل بـ CANCELLED فيُغلق الحوار من onError
        }
        PackDownloader.start(activity, pack, listener)
    }
}

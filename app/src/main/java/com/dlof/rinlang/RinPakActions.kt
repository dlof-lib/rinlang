package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.text.InputType
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Toast
import androidx.core.content.FileProvider
import java.io.File
import java.io.IOException

/**
 * طبقة الجوال لحزم RinPak: كل ما يلزم لاستقبال حزمة من أي تطبيق (مدير ملفات، بريد، واتساب، تيليجرام…)
 * وتصدير/مشاركة حزمة مشروع — بلا قفل لخيط الواجهة.
 *
 *  - [stageIncoming]: ينسخ الحزمة الواردة (content:// أو file://) **مرة واحدة** إلى ذاكرة التخزين المؤقت.
 *    روابط content:// الواردة من تطبيقات أخرى قد لا تُقرأ ثلاث مرات ولا بعد إعادة إنشاء الشاشة، والنسخ
 *    المحلي يجعل الكشف والمعاينة والتحقق والاستيراد تعمل على ملف ثابت.
 *  - [startExport]: حوار بيانات وصفية (إصدار/مؤلف/وصف/رخصة) ثم «مشاركة» أو «حفظ في التنزيلات».
 *  - [trust]: سجل ثقة الناشرين ([RinPakTrust]) فوق SharedPreferences.
 */
object RinPakActions {

    private const val INCOMING_DIR = "rinpak_incoming"
    /** حد أقصى لحجم الحزمة الواردة (المضغوطة) قبل رفضها: محتوى 256MB كحد أعلى + هامش للمانيفست/الضغط. */
    private const val MAX_INCOMING_BYTES = 300L * 1024L * 1024L

    // ───────────────────────────── الثقة ─────────────────────────────

    fun trust(context: Context): RinPakTrust {
        val prefs = context.applicationContext.getSharedPreferences("rinpak_trust", Context.MODE_PRIVATE)
        return RinPakTrust(object : RinPakTrust.Store {
            override fun get(key: String): String? = prefs.getString(key, null)
            override fun put(key: String, value: String) { prefs.edit().putString(key, value).apply() }
            override fun remove(key: String) { prefs.edit().remove(key).apply() }
        })
    }

    // ───────────────────────────── الاستقبال ─────────────────────────────

    /** الرابط الوارد من ACTION_VIEW (data) أو ACTION_SEND (EXTRA_STREAM)، أو null. */
    fun incomingUri(intent: Intent?): Uri? {
        if (intent == null) return null
        return when (intent.action) {
            Intent.ACTION_VIEW -> intent.data
            Intent.ACTION_SEND -> intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java) // minSdk 35
            // مشاركة عدة ملفات: نعالج الأول فقط (حوار معاينة واحد).
            Intent.ACTION_SEND_MULTIPLE ->
                intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)?.firstOrNull()
            else -> null
        }
    }

    fun displayName(context: Context, uri: Uri): String {
        val fromProvider = runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0) else null
            }
        }.getOrNull()
        return fromProvider ?: uri.lastPathSegment?.substringAfterLast('/') ?: "project${RinPak.EXTENSION}"
    }

    /**
     * ينسخ [uri] إلى ملف مؤقت ويعيده. يُستدعى من خيط خلفي. يرمي [IOException] إن تعذّرت القراءة
     * أو تجاوز الحجم [MAX_INCOMING_BYTES]. يحذف أي ملفات واردة أقدم من ساعة.
     */
    fun stageIncoming(
        context: Context,
        uri: Uri,
        onProgress: ((copied: Long, total: Long) -> Unit)? = null
    ): File {
        val dir = File(context.cacheDir, INCOMING_DIR).apply { mkdirs() }
        val cutoff = System.currentTimeMillis() - 60L * 60L * 1000L
        dir.listFiles()?.filter { it.lastModified() < cutoff }?.forEach { it.delete() }

        val resolver = context.contentResolver
        val total = runCatching {
            resolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst() && !c.isNull(0)) c.getLong(0) else -1L
            }
        }.getOrNull() ?: -1L
        if (total > MAX_INCOMING_BYTES) throw IOException("الحزمة أكبر من الحد المسموح (${MAX_INCOMING_BYTES / (1024 * 1024)}MB)")

        val out = File(dir, "incoming_${System.currentTimeMillis()}_${(1000..9999).random()}${RinPak.EXTENSION}")
        try {
            val input = resolver.openInputStream(uri) ?: throw IOException("تعذّرت قراءة الملف")
            input.use { src ->
                out.outputStream().buffered().use { dst ->
                    val buf = ByteArray(64 * 1024)
                    var copied = 0L
                    var lastReport = 0L
                    while (true) {
                        val n = src.read(buf)
                        if (n < 0) break
                        copied += n
                        if (copied > MAX_INCOMING_BYTES) throw IOException("الحزمة أكبر من الحد المسموح (${MAX_INCOMING_BYTES / (1024 * 1024)}MB)")
                        dst.write(buf, 0, n)
                        if (onProgress != null && copied - lastReport >= 512 * 1024) { lastReport = copied; onProgress(copied, total) }
                    }
                    onProgress?.invoke(copied, if (total >= 0) total else copied)
                }
            }
        } catch (t: Throwable) {
            out.delete()
            throw t
        }
        return out
    }

    // ───────────────────────────── التصدير والمشاركة ─────────────────────────────

    /** حوار التصدير: بيانات وصفية اختيارية ثم «مشاركة» أو «حفظ في التنزيلات». */
    fun startExport(activity: Activity, project: Project) {
        val saved = ProjectManager.loadPackMeta(activity, project)
        val pad = (20 * activity.resources.displayMetrics.density).toInt()
        val form = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad / 2, pad, 0)
        }
        fun field(hintRes: Int, value: String, multiline: Boolean = false): EditText =
            EditText(activity).apply {
                setHint(hintRes)
                setText(value)
                inputType = if (multiline) InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
                            else InputType.TYPE_CLASS_TEXT
                if (multiline) minLines = 2
                form.addView(this)
            }
        val version = field(R.string.rinpak_field_version, saved.version)
        val author = field(R.string.rinpak_field_author, saved.author)
        val description = field(R.string.rinpak_field_description, saved.description, multiline = true)
        val license = field(R.string.rinpak_field_license, saved.license)

        fun collect() = RinPak.PackMeta(
            version = version.text.toString().trim(),
            description = description.text.toString().trim(),
            author = author.text.toString().trim(),
            license = license.text.toString().trim()
        )

        AlertDialog.Builder(activity)
            .setTitle(R.string.rinpak_export_dialog_title)
            .setView(ScrollView(activity).apply { addView(form) })
            .setPositiveButton(R.string.rinpak_export_share) { _, _ -> runExport(activity, project, collect(), share = true) }
            .setNeutralButton(R.string.rinpak_export_save) { _, _ -> runExport(activity, project, collect(), share = false) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun runExport(activity: Activity, project: Project, meta: RinPak.PackMeta, share: Boolean) {
        ProjectManager.savePackMeta(activity, project, meta)
        val main = Handler(Looper.getMainLooper())
        val progress = RinDownloadProgressDialog(activity, "${project.name}${RinPak.EXTENSION}")
        progress.show()
        Thread {
            try {
                val file = ProjectManager.exportProject(activity, project, meta)
                main.post {
                    if (share) {
                        progress.dismiss()
                        shareFile(activity, file)
                    } else {
                        saveToDownloads(activity, file, progress, main)
                    }
                }
            } catch (t: Throwable) {
                main.post {
                    progress.finishError(activity.getString(R.string.download_error_toast, t.message ?: "?"))
                    main.postDelayed({ progress.dismiss() }, 1600)
                    Toast.makeText(activity, "${activity.getString(R.string.export_project_error)}: ${t.message}", Toast.LENGTH_LONG).show()
                }
            }
        }.start()
    }

    private fun saveToDownloads(activity: Activity, file: File, progress: RinDownloadProgressDialog, main: Handler) {
        val artifact = RinArtifact(
            kind = ArtifactKind.RINPAK,
            relPath = file.name,
            absoluteFile = file,
            sizeBytes = file.length()
        )
        RinDownloadManager.downloadToPublicDownloads(
            activity = activity,
            artifact = artifact,
            onProgress = { copied, total -> progress.updateProgress(copied, total) },
            onDone = { uri, error ->
                if (error != null || uri == null) {
                    progress.finishError(activity.getString(R.string.download_error_toast, error?.message ?: "?"))
                    main.postDelayed({ progress.dismiss() }, 1400)
                } else {
                    progress.finishSuccess(activity.getString(R.string.download_status_done))
                    Toast.makeText(activity, activity.getString(R.string.export_project_success, artifact.displayName), Toast.LENGTH_LONG).show()
                    main.postDelayed({ progress.dismiss() }, 800)
                }
            }
        )
    }

    /** مشاركة ملف .rinpak عبر قائمة المشاركة في النظام (واتساب/تيليجرام/بريد/Nearby…) بنوع MIME الخاص. */
    fun shareFile(activity: Activity, file: File) {
        try {
            val uri = FileProvider.getUriForFile(activity, "${activity.packageName}.fileprovider", file)
            val send = Intent(Intent.ACTION_SEND).apply {
                type = RinPak.MIME
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_SUBJECT, file.name)
                clipData = ClipData.newRawUri(file.name, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            activity.startActivity(Intent.createChooser(send, activity.getString(R.string.rinpak_share_chooser)))
        } catch (t: Throwable) {
            Toast.makeText(activity, "${activity.getString(R.string.export_project_error)}: ${t.message}", Toast.LENGTH_LONG).show()
        }
    }
}

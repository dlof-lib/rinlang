package com.dlof.rinlang

import android.app.Activity
import android.content.ActivityNotFoundException
import android.content.ContentValues
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.widget.Toast
import java.io.File
import java.io.FileInputStream
import java.io.IOException

/**
 * Copies a [RinArtifact] that save/installation already wrote to the app's private storage into
 * the device's public Downloads folder, so it shows up in Files / Downloads like any real
 * download — reporting genuine bytes-copied progress as it goes (no simulated progress).
 */
object RinDownloadManager {

    private const val CHUNK_BYTES = 64 * 1024

    fun downloadToPublicDownloads(
        activity: Activity,
        artifact: RinArtifact,
        onProgress: (copiedBytes: Long, totalBytes: Long) -> Unit,
        onDone: (uri: Uri?, error: Throwable?) -> Unit
    ) {
        val mainHandler = Handler(Looper.getMainLooper())
        Thread {
            try {
                val total = artifact.sizeBytes
                var destUri: Uri?
                val resolver = activity.contentResolver

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    val values = ContentValues().apply {
                        put(MediaStore.Downloads.DISPLAY_NAME, artifact.displayName)
                        put(MediaStore.Downloads.MIME_TYPE, artifact.kind.mime)
                        put(MediaStore.Downloads.IS_PENDING, 1)
                    }
                    destUri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                        ?: throw IOException("تعذّر إنشاء الملف داخل Downloads")

                    resolver.openOutputStream(destUri)?.use { output ->
                        copyWithProgress(artifact.absoluteFile, output, total, mainHandler, onProgress)
                    } ?: throw IOException("تعذّر فتح مجرى الكتابة")

                    val doneValues = ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }
                    resolver.update(destUri, doneValues, null, null)
                } else {
                    @Suppress("DEPRECATION")
                    val downloadsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
                    if (!downloadsDir.exists()) downloadsDir.mkdirs()
                    val destFile = uniqueFile(downloadsDir, artifact.displayName)
                    destFile.outputStream().use { output ->
                        copyWithProgress(artifact.absoluteFile, output, total, mainHandler, onProgress)
                    }
                    // content:// عبر FileProvider بدل file:// مباشرة، لتفادي FileUriExposedException
                    // عند تمرير الرابط لاحقاً إلى ACTION_VIEW على أندرويد 9 وأقدم.
                    destUri = androidx.core.content.FileProvider.getUriForFile(
                        activity, "${activity.packageName}.fileprovider", destFile
                    )
                    // يجعل الملف مرئياً فوراً في تطبيقات مثل "الملفات"/معرض الصور على الأجهزة الأقدم.
                    @Suppress("DEPRECATION")
                    activity.sendBroadcast(Intent(Intent.ACTION_MEDIA_SCANNER_SCAN_FILE, Uri.fromFile(destFile)))
                }

                mainHandler.post { onDone(destUri, null) }
            } catch (t: Throwable) {
                mainHandler.post { onDone(null, t) }
            }
        }.start()
    }

    private fun copyWithProgress(
        source: File,
        output: java.io.OutputStream,
        total: Long,
        mainHandler: Handler,
        onProgress: (Long, Long) -> Unit
    ) {
        FileInputStream(source).use { input ->
            val buffer = ByteArray(CHUNK_BYTES)
            var copied = 0L
            while (true) {
                val read = input.read(buffer)
                if (read < 0) break
                output.write(buffer, 0, read)
                copied += read
                val snapshot = copied
                mainHandler.post { onProgress(snapshot, total) }
            }
            output.flush()
        }
    }

    private fun uniqueFile(dir: File, name: String): File {
        var candidate = File(dir, name)
        if (!candidate.exists()) return candidate
        val dot = name.lastIndexOf('.')
        val base = if (dot >= 0) name.substring(0, dot) else name
        val ext = if (dot >= 0) name.substring(dot) else ""
        var i = 1
        while (candidate.exists()) {
            candidate = File(dir, "$base ($i)$ext")
            i++
        }
        return candidate
    }

    /** "تنفيذ" الملف المنزَّل: يفتحه بالتطبيق المناسب على الجهاز (صور/أرشيفات/نصوص). */
    fun openDownloadedFile(activity: Activity, uri: Uri, mimeType: String) {
        try {
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, mimeType)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            activity.startActivity(intent)
        } catch (e: ActivityNotFoundException) {
            Toast.makeText(
                activity,
                activity.getString(R.string.download_no_app_toast),
                Toast.LENGTH_SHORT
            ).show()
        }
    }
}

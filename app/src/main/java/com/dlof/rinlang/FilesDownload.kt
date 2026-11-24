package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.app.PendingIntent
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.net.Uri
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.text.InputType
import android.webkit.MimeTypeMap
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Toast
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.dlof.rinlang.permissions.RinNotifications
import com.dlof.rinlang.permissions.RinPermissions
import java.io.BufferedOutputStream
import java.io.File
import java.io.IOException
import java.io.OutputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/*
 * ميزات التنزيل لشاشتي «الملفات» و«المشاريع»:
 *
 *   • DownloadExporter : حفظ ملف/مجلد/عدة عناصر/مجلد حالي/مشروع كامل/عدة مشاريع في مجلد
 *                        Downloads/RinStudio على الجهاز (MediaStore — بلا أي إذن تخزين)، بتقدّم
 *                        حقيقي بالبايتات، ثم حوار «فتح / مشاركة» وإشعار اكتمال (إن سمح المستخدم).
 *                        ملف واحد ← يُحفظ كما هو؛ غير ذلك ← أرشيف ZIP.
 *   • UrlDownloader    : تنزيل ملف من رابط https مباشرةً داخل المجلد الحالي من المشروع،
 *                        بتقدّم وإلغاء، وبملف مؤقت .part لا يظهر ناقصاً أبداً.
 *
 * ملاحظة: «تصدير RinPak» (حزمة موقّعة بهوية) يبقى كما هو في RinPakActions؛ هذا الملف يضيف
 * التنزيل العادي (ملفات خام أو ZIP) الذي يفتحه أي تطبيق.
 */

object DownloadExporter {

    /** المجلد الفرعي داخل Downloads العام الذي تُحفظ فيه كل تنزيلات RinStudio. */
    private const val SUB_DIR = "RinStudio"
    private const val CHUNK = 64 * 1024
    private const val PROGRESS_INTERVAL_MS = 80L

    private class Item(val file: File, val zipName: String)

    // ------------------------------------------------------------------ واجهات عامة

    /** تنزيل عناصر محدّدة: عنصر ملف واحد ← ملف خام (إلا إذا [forceZip])، وإلا ← ZIP. */
    fun saveEntries(ctx: Context, project: Project, entries: List<FsEntry>, forceZip: Boolean = false) {
        val activity = ctx as? Activity ?: return
        val list = FileBatchOps.normalize(entries)
        if (list.isEmpty()) {
            Toast.makeText(activity, R.string.dl_empty, Toast.LENGTH_SHORT).show()
            return
        }
        val single = list.singleOrNull()
        if (single is FsEntry.FileEntry && !forceZip) {
            saveRawFile(activity, single.file.file, single.name)
            return
        }
        val items = ArrayList<Item>()
        for (e in list) {
            val f = when (e) {
                is FsEntry.FileEntry -> e.file.file
                is FsEntry.FolderEntry -> e.folder.dir
            }
            items += Item(f, f.name)
        }
        val zipName = when {
            single != null -> "${single.name.substringBeforeLast('.', single.name)}.zip"
            else -> "${safeName(project.name)}-${list.size}-items.zip"
        }
        saveZip(activity, zipName, items)
    }

    /** تنزيل المجلد الحالي (أو جذر المشروع إن كان [relDir] فارغاً) كأرشيف ZIP. */
    fun saveFolder(ctx: Context, project: Project, relDir: String) {
        val activity = ctx as? Activity ?: return
        val dir = if (relDir.isBlank()) project.dir else File(project.dir, relDir)
        if (!dir.isDirectory) {
            Toast.makeText(activity, R.string.dl_empty, Toast.LENGTH_SHORT).show()
            return
        }
        val name = if (relDir.isBlank()) project.name else dir.name
        saveZip(activity, "${safeName(name)}.zip", listOf(Item(dir, safeName(name))))
    }

    /** تنزيل مشروع كامل كأرشيف ZIP عادي (بنيته كما هي، يُفكّ في أي جهاز). */
    fun saveProject(ctx: Context, project: Project) {
        val activity = ctx as? Activity ?: return
        saveZip(activity, "${safeName(project.name)}.zip", listOf(Item(project.dir, safeName(project.name))))
    }

    /** تنزيل عدة مشاريع: واحد ← ZIP له، أكثر ← ZIP واحد يحوي مجلداً لكل مشروع. */
    fun saveProjects(ctx: Context, projects: List<Project>) {
        val activity = ctx as? Activity ?: return
        if (projects.isEmpty()) {
            Toast.makeText(activity, R.string.dl_empty, Toast.LENGTH_SHORT).show()
            return
        }
        if (projects.size == 1) {
            saveProject(activity, projects[0])
            return
        }
        val items = projects.map { Item(it.dir, safeName(it.name)) }
        saveZip(activity, "RinStudio-projects-${projects.size}.zip", items)
    }

    // ------------------------------------------------------------------ التنفيذ

    private fun saveRawFile(activity: Activity, file: File, name: String) {
        val mime = mimeFor(name)
        runJob(
            activity = activity,
            displayName = name,
            mime = mime,
            title = null,
            status = null,
            total = file.length()
        ) { out, report ->
            file.inputStream().use { input -> copy(input, out, report) }
        }
    }

    private fun saveZip(activity: Activity, zipName: String, items: List<Item>) {
        val total = items.sumOf { sizeOf(it.file) }
        if (total == 0L && items.all { countFiles(it.file) == 0 }) {
            Toast.makeText(activity, R.string.dl_empty, Toast.LENGTH_SHORT).show()
            return
        }
        runJob(
            activity = activity,
            displayName = zipName,
            mime = "application/zip",
            title = activity.getString(R.string.dl_zip_progress_title),
            status = activity.getString(R.string.dl_zip_progress_status),
            total = total
        ) { out, report ->
            val seen = HashSet<String>()
            var done = 0L
            ZipOutputStream(BufferedOutputStream(out, CHUNK)).use { zos ->
                for (item in items) {
                    val root = item.file
                    val files = if (root.isFile) listOf(root) else root.walkTopDown().filter { it.isFile }.toList()
                    for (f in files) {
                        val rel = if (root.isFile) item.zipName
                        else item.zipName + "/" + f.relativeTo(root).path.replace('\\', '/')
                        val entryName = uniqueEntry(seen, rel)
                        try {
                            zos.putNextEntry(ZipEntry(entryName).apply { time = f.lastModified() })
                            f.inputStream().use { input ->
                                copy(input, zos) { n ->
                                    report(done + n)
                                }
                            }
                            zos.closeEntry()
                        } catch (e: IOException) {
                            // ملف غير قابل للقراءة: نتجاوزه ولا نُفشل الأرشيف كاملاً.
                            runCatching { zos.closeEntry() }
                        }
                        done += f.length()
                        report(done)
                    }
                }
            }
        }
    }

    /**
     * ينشئ صفّ MediaStore في Download/RinStudio ويكتب فيه عبر [writer] على خيط خلفي مع تقدّم حقيقي.
     * عند الفشل يُحذف الصف الناقص كي لا يبقى ملف نصف مكتوب.
     */
    private fun runJob(
        activity: Activity,
        displayName: String,
        mime: String,
        title: String?,
        status: String?,
        total: Long,
        writer: (OutputStream, (Long) -> Unit) -> Unit
    ) {
        val main = Handler(Looper.getMainLooper())
        val progress = RinDownloadProgressDialog(activity, displayName, title, status)
        progress.show()
        val resolver = activity.contentResolver

        Thread {
            var uri: Uri? = null
            try {
                val values = ContentValues().apply {
                    put(MediaStore.Downloads.DISPLAY_NAME, displayName)
                    put(MediaStore.Downloads.MIME_TYPE, mime)
                    put(MediaStore.Downloads.RELATIVE_PATH, "${Environment.DIRECTORY_DOWNLOADS}/$SUB_DIR")
                    put(MediaStore.Downloads.IS_PENDING, 1)
                }
                uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values)
                    ?: throw IOException(activity.getString(R.string.dl_create_failed))

                var lastPost = 0L
                val report: (Long) -> Unit = { copied ->
                    val now = System.currentTimeMillis()
                    if (now - lastPost >= PROGRESS_INTERVAL_MS) {
                        lastPost = now
                        main.post { progress.updateProgress(copied, total) }
                    }
                }
                resolver.openOutputStream(uri)?.use { out ->
                    writer(out, report)
                    out.flush()
                } ?: throw IOException(activity.getString(R.string.dl_open_stream_failed))

                resolver.update(uri, ContentValues().apply { put(MediaStore.Downloads.IS_PENDING, 0) }, null, null)
                val finalName = queryName(activity, uri) ?: displayName
                val finalUri: Uri = uri
                main.post {
                    progress.updateProgress(total, total)
                    progress.finishSuccess(activity.getString(R.string.download_status_done))
                    main.postDelayed({
                        progress.dismiss()
                        onSaved(activity, finalUri, finalName, mime)
                    }, 450)
                }
            } catch (t: Throwable) {
                uri?.let { runCatching { resolver.delete(it, null, null) } }
                main.post {
                    progress.finishError(activity.getString(R.string.download_error_toast, t.message ?: "?"))
                    main.postDelayed({ progress.dismiss() }, 1600)
                }
            }
        }.start()
    }

    /** بعد النجاح: حوار فتح/مشاركة + إشعار اكتمال، ثم طلب إذن الإشعارات مرة واحدة إن لم يُمنح. */
    private fun onSaved(activity: Activity, uri: Uri, name: String, mime: String) {
        if (activity.isFinishing || activity.isDestroyed) {
            notifyDone(activity.applicationContext, uri, name, mime)
            return
        }
        notifyDone(activity, uri, name, mime)
        AlertDialog.Builder(activity)
            .setTitle(R.string.dl_saved_title)
            .setMessage(activity.getString(R.string.dl_saved_message, name, "${Environment.DIRECTORY_DOWNLOADS}/$SUB_DIR"))
            .setPositiveButton(R.string.dl_open) { _, _ -> view(activity, uri, mime) }
            .setNeutralButton(R.string.dl_share) { _, _ -> share(activity, uri, mime, name) }
            .setNegativeButton(R.string.dl_close) { _, _ -> RinPermissions.askNotificationsOnce(activity) }
            .setOnCancelListener { RinPermissions.askNotificationsOnce(activity) }
            .show()
    }

    private fun view(activity: Activity, uri: Uri, mime: String) {
        try {
            activity.startActivity(
                Intent(Intent.ACTION_VIEW).apply {
                    setDataAndType(uri, mime)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
            )
        } catch (t: Throwable) {
            Toast.makeText(activity, R.string.download_no_app_toast, Toast.LENGTH_SHORT).show()
        }
    }

    private fun share(activity: Activity, uri: Uri, mime: String, name: String) {
        try {
            val send = Intent(Intent.ACTION_SEND).apply {
                type = mime
                putExtra(Intent.EXTRA_STREAM, uri)
                putExtra(Intent.EXTRA_SUBJECT, name)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            activity.startActivity(Intent.createChooser(send, activity.getString(R.string.dl_share)))
        } catch (t: Throwable) {
            Toast.makeText(activity, "${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    private fun notifyDone(ctx: Context, uri: Uri, name: String, mime: String) {
        runCatching {
            if (!RinPermissions.notificationsGranted(ctx)) return
            val open = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(uri, mime)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            val pi = PendingIntent.getActivity(
                ctx, uri.hashCode(), open, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
            )
            val n = NotificationCompat.Builder(ctx, RinNotifications.CHANNEL_DOWNLOADS)
                .setSmallIcon(R.drawable.rs_ic_download)
                .setContentTitle(ctx.getString(R.string.dl_notif_title))
                .setContentText(ctx.getString(R.string.dl_notif_text, name))
                .setContentIntent(pi)
                .setAutoCancel(true)
                .setColor(ContextCompat.getColor(ctx, R.color.rin_accent))
                .build()
            NotificationManagerCompat.from(ctx).notify(uri.hashCode(), n)
        }
    }

    // ------------------------------------------------------------------ أدوات مساعدة

    private fun copy(input: java.io.InputStream, out: OutputStream, report: (Long) -> Unit) {
        val buf = ByteArray(CHUNK)
        var total = 0L
        while (true) {
            val n = input.read(buf)
            if (n < 0) break
            out.write(buf, 0, n)
            total += n
            report(total)
        }
    }

    private fun sizeOf(f: File): Long =
        if (f.isFile) f.length() else f.walkTopDown().filter { it.isFile }.sumOf { it.length() }

    private fun countFiles(f: File): Int =
        if (f.isFile) 1 else f.walkTopDown().count { it.isFile }

    private fun uniqueEntry(seen: MutableSet<String>, rel: String): String {
        if (seen.add(rel)) return rel
        val dot = rel.lastIndexOf('.')
        val slash = rel.lastIndexOf('/')
        val base = if (dot > slash) rel.substring(0, dot) else rel
        val ext = if (dot > slash) rel.substring(dot) else ""
        var i = 2
        while (true) {
            val cand = "$base ($i)$ext"
            if (seen.add(cand)) return cand
            i++
        }
    }

    private fun queryName(ctx: Context, uri: Uri): String? = runCatching {
        ctx.contentResolver.query(uri, arrayOf(MediaStore.Downloads.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    }.getOrNull()

    /**
     * امتداد غير معروف ← octet-stream. مهم: لو مرّرنا text/plain لملف .rin فإن MediaStore
     * سيُلحق «.txt» باسمه فيصير file.rin.txt.
     */
    private fun mimeFor(name: String): String {
        val ext = name.substringAfterLast('.', "").lowercase()
        return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext) ?: "application/octet-stream"
    }

    private fun safeName(raw: String): String {
        val s = raw.replace(Regex("[\\\\/:*?\"<>|\\u0000-\\u001F]"), "_").trim().trim('.')
        return s.ifEmpty { "RinStudio" }
    }
}

// =================================================================================================
// تنزيل من رابط إلى المشروع
// =================================================================================================

object UrlDownloader {

    private const val MAX_REDIRECTS = 5
    private const val CHUNK = 32 * 1024

    /** حوار: رابط + اسم ملف اختياري، ثم تنزيل إلى [relDir] من [project]. [onDone] يُستدعى بعد النجاح. */
    fun show(activity: Activity, project: Project, relDir: String, onDone: () -> Unit) {
        val d = activity.resources.displayMetrics.density
        fun px(v: Int) = (v * d).toInt()
        val urlField = EditText(activity).apply {
            hint = activity.getString(R.string.dl_url_hint)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            setSingleLine(true)
            textDirection = android.view.View.TEXT_DIRECTION_LTR
        }
        val nameField = EditText(activity).apply {
            hint = activity.getString(R.string.dl_url_name_hint)
            setSingleLine(true)
            textDirection = android.view.View.TEXT_DIRECTION_LTR
        }
        val form = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(px(22), px(12), px(22), px(4))
            addView(urlField)
            addView(nameField)
        }
        AlertDialog.Builder(activity)
            .setTitle(R.string.dl_url_title)
            .setView(form)
            .setPositiveButton(R.string.dl_url_start) { _, _ ->
                val url = parse(urlField.text.toString())
                if (url == null) {
                    Toast.makeText(activity, R.string.dl_url_invalid, Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                if (!isOnline(activity)) {
                    Toast.makeText(activity, R.string.dl_url_offline, Toast.LENGTH_LONG).show()
                    return@setPositiveButton
                }
                val dir = if (relDir.isBlank()) project.dir else File(project.dir, relDir)
                val wanted = nameField.text.toString().trim().ifEmpty { nameFromUrl(url) }
                start(activity, dir, cleanFileName(wanted), url, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun start(activity: Activity, dir: File, name: String, url: URL, onDone: () -> Unit) {
        val main = Handler(Looper.getMainLooper())
        val cancelled = AtomicBoolean(false)
        val progress = RinDownloadProgressDialog(
            activity, name,
            activity.getString(R.string.dl_url_progress_title),
            activity.getString(R.string.dl_url_progress_status)
        )
        progress.setCancelAction(activity.getString(R.string.cancel)) { cancelled.set(true) }
        progress.show()

        Thread {
            var part: File? = null
            try {
                dir.mkdirs()
                val target = uniqueFile(File(dir, name))
                part = File(dir, "${target.name}.part")
                var conn = open(url)
                var redirects = 0
                while (conn.responseCode in 300..399) {
                    val loc = conn.getHeaderField("Location") ?: break
                    conn.disconnect()
                    if (++redirects > MAX_REDIRECTS) throw IOException(activity.getString(R.string.dl_url_too_many_redirects))
                    val next = URL(conn.url, loc)
                    if (next.protocol != "https") throw IOException("https only")
                    conn = open(next)
                }
                if (conn.responseCode !in 200..299) throw IOException("HTTP ${conn.responseCode}")

                val total = conn.contentLengthLong
                var copied = 0L
                var lastPost = 0L
                conn.inputStream.use { input ->
                    part.outputStream().buffered(CHUNK).use { out ->
                        val buf = ByteArray(CHUNK)
                        while (true) {
                            if (cancelled.get()) throw CancelledException()
                            val n = input.read(buf)
                            if (n < 0) break
                            out.write(buf, 0, n)
                            copied += n
                            val now = System.currentTimeMillis()
                            if (now - lastPost >= 80) {
                                lastPost = now
                                val c = copied
                                main.post { progress.updateProgress(c, if (total > 0) total else c) }
                            }
                        }
                    }
                }
                conn.disconnect()
                if (!part.renameTo(target)) throw IOException(activity.getString(R.string.dl_create_failed))
                val finalName = target.name
                main.post {
                    progress.updateProgress(copied, copied)
                    progress.finishSuccess(activity.getString(R.string.download_status_done))
                    Toast.makeText(activity, activity.getString(R.string.dl_url_done, finalName), Toast.LENGTH_LONG).show()
                    main.postDelayed({ progress.dismiss(); onDone() }, 450)
                }
            } catch (c: CancelledException) {
                part?.delete()
                main.post { progress.dismiss() }
            } catch (t: Throwable) {
                part?.delete()
                main.post {
                    progress.finishError(activity.getString(R.string.download_error_toast, t.message ?: "?"))
                    main.postDelayed({ progress.dismiss() }, 1600)
                }
            }
        }.start()
    }

    private class CancelledException : RuntimeException()

    private fun open(url: URL): HttpURLConnection =
        (url.openConnection() as HttpURLConnection).apply {
            instanceFollowRedirects = false // نعالج التحويلات يدوياً كي نمنع الانتقال إلى بروتوكول غير https
            connectTimeout = 15_000
            readTimeout = 20_000
            setRequestProperty("User-Agent", "RinStudio")
            setRequestProperty("Accept", "*/*")
        }

    /** يقبل https فقط (الحركة النصية http محظورة افتراضياً في أندرويد)؛ يضيف https:// إن غاب البروتوكول. */
    private fun parse(raw: String): URL? {
        var s = raw.trim()
        if (s.isEmpty() || s.any { it.isWhitespace() }) return null
        if (!s.contains("://")) s = "https://$s"
        return try {
            val u = URL(s)
            if (u.protocol == "https" && !u.host.isNullOrEmpty()) u else null
        } catch (_: Throwable) { null }
    }

    private fun nameFromUrl(url: URL): String {
        val last = url.path.substringAfterLast('/').substringBefore('?')
        val decoded = runCatching { java.net.URLDecoder.decode(last, "UTF-8") }.getOrDefault(last)
        return decoded.ifBlank { "download" }
    }

    /** اسم ملف آمن: بلا فواصل مسار أو محارف تحكّم، ولا يبدأ بنقطة. */
    private fun cleanFileName(raw: String): String {
        val s = raw.replace(Regex("[\\\\/:*?\"<>|\\u0000-\\u001F]"), "_").trim().trimStart('.')
        return s.take(120).ifEmpty { "download" }
    }

    private fun uniqueFile(f: File): File {
        if (!f.exists()) return f
        val dot = f.name.lastIndexOf('.')
        var k = 1
        var t: File
        do {
            val n = if (dot > 0) "${f.name.substring(0, dot)}_$k${f.name.substring(dot)}" else "${f.name}_$k"
            t = File(f.parentFile, n)
            k++
        } while (t.exists())
        return t
    }

    private fun isOnline(ctx: Context): Boolean {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager ?: return true
        val caps = cm.getNetworkCapabilities(cm.activeNetwork) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
    }
}

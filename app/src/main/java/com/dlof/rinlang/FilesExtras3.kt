package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.text.format.Formatter
import android.util.TypedValue
import android.view.View
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.annotation.DrawableRes
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

/*
 * رفع الملفات والوسائط وأدوات الأرشيف (zip) — أيقونات fx_* مرسومة من الصفر:
 *   • IconMenu   : قائمة حوار بأيقونة لكل خيار.
 *   • Uploader   : رفع عدة ملفات أو مجلد كامل، مع سياسة التعارض (احتفاظ/تخطّي/استبدال)،
 *                  شريط تقدّم قابل للإلغاء، وتصغير تلقائي اختياري للصور الكبيرة.
 *   • ZipTools   : استعراض أرشيف وفك انتقائي، فك إلى مجلد باسم الأرشيف، اختبار السلامة، ضغط ومشاركة.
 *   • MediaTools : معلومات الوسائط ونسخة مصغّرة من الصورة.
 */

// ---------------------------------------------------------------------------------------------
// قائمة بأيقونات
// ---------------------------------------------------------------------------------------------

object IconMenu {
    class Item(@DrawableRes val icon: Int, val label: String, val action: () -> Unit)

    fun show(ctx: Context, title: String, items: List<Item>) {
        val d = ctx.resources.displayMetrics.density
        val accent = ContextCompat.getColor(ctx, R.color.rin_accent)
        val sel = TypedValue()
        ctx.theme.resolveAttribute(android.R.attr.selectableItemBackground, sel, true)
        val box = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, (6 * d).toInt(), 0, (6 * d).toInt())
        }
        var dlg: AlertDialog? = null
        for (item in items) {
            val row = TextView(ctx).apply {
                text = item.label
                textSize = 15f
                gravity = android.view.Gravity.CENTER_VERTICAL
                setPadding((20 * d).toInt(), (14 * d).toInt(), (20 * d).toInt(), (14 * d).toInt())
                if (sel.resourceId != 0) setBackgroundResource(sel.resourceId)
                isClickable = true
                isFocusable = true
                setOnClickListener { dlg?.dismiss(); item.action() }
            }
            RsIcons.start(row, item.icon, 22, 14, accent)
            box.addView(row)
        }
        dlg = AlertDialog.Builder(ctx)
            .setTitle(title)
            .setView(ScrollView(ctx).apply { addView(box) })
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

// ---------------------------------------------------------------------------------------------
// الرفع
// ---------------------------------------------------------------------------------------------

enum class Conflict { KEEP_BOTH, SKIP, REPLACE }

object Uploader {
    private const val PREFS = "rin_upload_prefs"
    private const val MAX_TREE_FILES = 5000
    private const val SHRINK_MAX_DIM = 2048

    class Job(val rel: String, val uri: Uri)

    fun shrinkEnabled(ctx: Context): Boolean =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("shrink", false)

    fun setShrink(ctx: Context, on: Boolean) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean("shrink", on).apply()
    }

    private fun sanitize(n: String): String =
        n.replace(Regex("[^A-Za-z0-9_\\-.\\u0600-\\u06FF]"), "_").ifBlank { "imported" }

    private fun displayName(ctx: Context, uri: Uri): String {
        try {
            ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0)?.let { n -> if (n.isNotBlank()) return n }
            }
        } catch (_: Throwable) {}
        return uri.lastPathSegment?.substringAfterLast('/') ?: "imported"
    }

    fun files(ctx: Context, project: Project, relDir: String, uris: List<Uri>, prot: Set<String>, onDone: () -> Unit) =
        start(ctx, project, relDir, prot, onDone) { uris.map { Job(sanitize(displayName(ctx, it)), it) } }

    fun tree(ctx: Context, project: Project, relDir: String, tree: Uri, prot: Set<String>, onDone: () -> Unit) =
        start(ctx, project, relDir, prot, onDone) { collectTree(ctx, tree) }

    private fun start(
        ctx: Context, project: Project, relDir: String, prot: Set<String>, onDone: () -> Unit,
        collect: () -> List<Job>
    ) {
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.upload_collecting, Toast.LENGTH_SHORT).show()
        Thread {
            val jobs = try { collect() } catch (t: Throwable) {
                ui.post { Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show() }
                return@Thread
            }
            ui.post {
                if (jobs.isEmpty()) {
                    Toast.makeText(ctx, R.string.upload_nothing, Toast.LENGTH_SHORT).show()
                    return@post
                }
                val root = ProjectManager.resolveDir(project, relDir)
                val conflicts = jobs.count { File(root, it.rel).exists() }
                if (conflicts == 0) {
                    copy(ctx, project, relDir, jobs, Conflict.KEEP_BOTH, prot, ui, onDone)
                } else {
                    val labels = arrayOf(
                        ctx.getString(R.string.conflict_keep),
                        ctx.getString(R.string.conflict_skip),
                        ctx.getString(R.string.conflict_replace)
                    )
                    AlertDialog.Builder(ctx)
                        .setTitle(ctx.getString(R.string.conflict_title, conflicts))
                        .setItems(labels) { _, i ->
                            copy(ctx, project, relDir, jobs, Conflict.values()[i], prot, ui, onDone)
                        }
                        .setNegativeButton(R.string.cancel, null)
                        .show()
                }
            }
        }.start()
    }

    private fun collectTree(ctx: Context, tree: Uri): List<Job> {
        val cr = ctx.contentResolver
        val rootId = DocumentsContract.getTreeDocumentId(tree)
        var rootName = "folder"
        cr.query(
            DocumentsContract.buildDocumentUriUsingTree(tree, rootId),
            arrayOf(DocumentsContract.Document.COLUMN_DISPLAY_NAME), null, null, null
        )?.use { if (it.moveToFirst()) rootName = it.getString(0) ?: rootName }

        val out = ArrayList<Job>()
        fun walk(docId: String, prefix: String) {
            if (out.size >= MAX_TREE_FILES) return
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId)
            cr.query(
                children,
                arrayOf(
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE
                ), null, null, null
            )?.use { c ->
                while (c.moveToNext() && out.size < MAX_TREE_FILES) {
                    val id = c.getString(0) ?: continue
                    val name = c.getString(1) ?: continue
                    if (c.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR) {
                        walk(id, "$prefix$name/")
                    } else {
                        out += Job("$prefix$name", DocumentsContract.buildDocumentUriUsingTree(tree, id))
                    }
                }
            }
        }
        walk(rootId, sanitize(rootName) + "/")
        return out
    }

    private fun unique(f: File): File {
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

    private fun copy(
        ctx: Context, project: Project, relDir: String, jobs: List<Job>, policy: Conflict,
        prot: Set<String>, ui: Handler, onDone: () -> Unit
    ) {
        val d = ctx.resources.displayMetrics.density
        val bar = ProgressBar(ctx, null, android.R.attr.progressBarStyleHorizontal).apply { max = jobs.size }
        val msg = TextView(ctx).apply { text = ctx.getString(R.string.upload_progress, 0, jobs.size) }
        val box = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((22 * d).toInt(), (14 * d).toInt(), (22 * d).toInt(), 0)
            addView(msg); addView(bar)
        }
        val cancelled = AtomicBoolean(false)
        val dlg = AlertDialog.Builder(ctx)
            .setTitle(R.string.upload_file)
            .setView(box)
            .setCancelable(false)
            .setNegativeButton(R.string.cancel) { _, _ -> cancelled.set(true) }
            .show()

        val root = ProjectManager.resolveDir(project, relDir)
        val relBase = if (relDir.isBlank()) "" else relDir.trim('/') + "/"
        val shrink = shrinkEnabled(ctx)

        Thread {
            var ok = 0; var skipped = 0; var failed = 0; var bytes = 0L
            for ((i, job) in jobs.withIndex()) {
                if (cancelled.get()) break
                try {
                    val segs = job.rel.split('/').filter { it.isNotEmpty() }.map { sanitize(it) }
                    var target = File(root, segs.joinToString("/"))
                    val relFull = relBase + segs.joinToString("/")
                    var skip = false
                    if (target.exists()) {
                        when (policy) {
                            Conflict.SKIP -> skip = true
                            Conflict.REPLACE -> if (relFull in prot) skip = true
                            Conflict.KEEP_BOTH -> target = unique(target)
                        }
                    }
                    if (skip) {
                        skipped++
                    } else {
                        target.parentFile?.mkdirs()
                        val input = ctx.contentResolver.openInputStream(job.uri)
                            ?: throw IllegalStateException("open failed")
                        input.use { ins -> FileOutputStream(target).use { ins.copyTo(it) } }
                        if (shrink && ProjectManager.isImageFile(target.name)) {
                            MediaTools.shrinkInPlace(target, SHRINK_MAX_DIM)
                        }
                        bytes += target.length()
                        ok++
                    }
                } catch (_: Throwable) {
                    failed++
                }
                ui.post {
                    bar.progress = i + 1
                    msg.text = ctx.getString(R.string.upload_progress, i + 1, jobs.size)
                }
            }
            ui.post {
                dlg.dismiss()
                val head = if (cancelled.get()) ctx.getString(R.string.upload_cancelled) + "\n" else ""
                Toast.makeText(
                    ctx,
                    head + ctx.getString(R.string.upload_result, ok, skipped, failed, Formatter.formatFileSize(ctx, bytes)),
                    Toast.LENGTH_LONG
                ).show()
                onDone()
            }
        }.start()
    }
}

// ---------------------------------------------------------------------------------------------
// أدوات zip
// ---------------------------------------------------------------------------------------------

object ZipTools {
    private const val MAX_TOTAL = 1L shl 30
    private const val MAX_SHOWN = 2000

    private fun cleanName(raw: String): String? {
        val n = raw.replace('\\', '/').trimStart('/')
        if (n.isEmpty() || n.startsWith("__MACOSX/")) return null
        val segs = n.split('/').filter { it.isNotEmpty() }
        if (segs.isEmpty() || segs.any { it == ".." || it == "." }) return null
        return segs.joinToString("/")
    }

    private fun copyLimited(ins: InputStream, out: OutputStream, limit: Long): Long {
        val buf = ByteArray(32 * 1024)
        var total = 0L
        while (true) {
            val n = ins.read(buf)
            if (n < 0) break
            total += n
            if (total > limit) throw IllegalStateException("too big")
            out.write(buf, 0, n)
        }
        return total
    }

    /** يفك [entries] داخل [target] (الذي يجب أن يقع داخل المشروع). يعيد (نجح, تخطّى). */
    private fun extract(
        zf: ZipFile, entries: List<ZipEntry>, target: File, projectRoot: File, relBase: String, prot: Set<String>
    ): Pair<Int, Int> {
        var ok = 0; var skip = 0; var total = 0L
        val tRoot = target.canonicalFile
        val pRoot = projectRoot.canonicalFile
        for (e in entries) {
            val name = cleanName(e.name) ?: continue
            val out = File(tRoot, name).canonicalFile
            if (!out.path.startsWith(pRoot.path + File.separator)) { skip++; continue }
            if (out.exists() && (relBase + name) in prot) { skip++; continue }
            try {
                out.parentFile?.mkdirs()
                zf.getInputStream(e).use { ins ->
                    FileOutputStream(out).use { total += copyLimited(ins, it, MAX_TOTAL - total) }
                }
                ok++
            } catch (_: Throwable) {
                skip++
                if (total > MAX_TOTAL) break
            }
        }
        return ok to skip
    }

    private fun relBase(relDir: String) = if (relDir.isBlank()) "" else relDir.trim('/') + "/"

    /** ينسخ أرشيفاً من Uri إلى الكاش ثم يعرضه. */
    fun browseUri(ctx: Context, project: Project, relDir: String, uri: Uri, prot: Set<String>, onDone: () -> Unit) {
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            try {
                val tmp = File(File(ctx.cacheDir, "zip_tmp").apply { mkdirs() }, "${System.currentTimeMillis()}.zip")
                ctx.contentResolver.openInputStream(uri)!!.use { ins -> FileOutputStream(tmp).use { ins.copyTo(it) } }
                ui.post { browseFile(ctx, project, relDir, tmp, prot, true, onDone) }
            } catch (t: Throwable) {
                ui.post { Toast.makeText(ctx, ctx.getString(R.string.zip_bad, t.message ?: ""), Toast.LENGTH_LONG).show() }
            }
        }.start()
    }

    fun browseFile(
        ctx: Context, project: Project, relDir: String, zip: File, prot: Set<String>,
        deleteAfter: Boolean, onDone: () -> Unit
    ) {
        val zf = try { ZipFile(zip) } catch (t: Throwable) {
            Toast.makeText(ctx, ctx.getString(R.string.zip_bad, t.message ?: ""), Toast.LENGTH_LONG).show()
            if (deleteAfter) zip.delete()
            return
        }
        val all = zf.entries().asSequence().filter { !it.isDirectory && cleanName(it.name) != null }.toList()
        val shown = all.take(MAX_SHOWN)
        val total = all.sumOf { maxOf(it.size, 0L) }
        val labels = shown.map {
            "${cleanName(it.name)}  ·  ${Formatter.formatFileSize(ctx, maxOf(it.size, 0L))}"
        }.toTypedArray()
        val checked = BooleanArray(shown.size) { true }
        val ui = Handler(Looper.getMainLooper())

        val b = AlertDialog.Builder(ctx)
            .setTitle(ctx.getString(R.string.zip_browse_title, zip.name, all.size, Formatter.formatFileSize(ctx, total)))
            .setMultiChoiceItems(labels, checked) { _, i, on -> checked[i] = on }
            .setPositiveButton(R.string.zip_extract_selected, null)
            .setNeutralButton(R.string.zip_toggle_all, null)
            .setNegativeButton(R.string.cancel, null)
            .setOnDismissListener {
                try { zf.close() } catch (_: Throwable) {}
                if (deleteAfter) zip.delete()
            }
        val dlg = b.show()
        if (all.size > shown.size) {
            Toast.makeText(ctx, ctx.getString(R.string.zip_truncated, MAX_SHOWN), Toast.LENGTH_LONG).show()
        }
        dlg.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener {
            val on = !checked.all { it }
            for (i in checked.indices) { checked[i] = on; dlg.listView.setItemChecked(i, on) }
        }
        dlg.getButton(AlertDialog.BUTTON_NEUTRAL).setOnLongClickListener {
            PatternSelect.ask(ctx) { rx ->
                for (i in shown.indices) {
                    val on = rx.matches((cleanName(shown[i].name) ?: "").substringAfterLast('/'))
                    checked[i] = on
                    dlg.listView.setItemChecked(i, on)
                }
            }
            true
        }
        dlg.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener { btn ->
            val sel = shown.filterIndexed { i, _ -> checked[i] }
            if (sel.isEmpty()) {
                Toast.makeText(ctx, R.string.zip_none_selected, Toast.LENGTH_SHORT).show()
                return@setOnClickListener
            }
            btn.isEnabled = false
            val root = ProjectManager.resolveDir(project, relDir)
            Thread {
                val (ok, skip) = extract(zf, sel, root, project.dir, relBase(relDir), prot)
                ui.post {
                    Toast.makeText(ctx, ctx.getString(R.string.zip_extract_result, ok, skip), Toast.LENGTH_LONG).show()
                    dlg.dismiss()
                    onDone()
                }
            }.start()
        }
    }

    /** يفك كل الأرشيف داخل مجلد جديد باسم الأرشيف (بلا .zip) ضمن [relDir]. */
    fun extractToNamedFolder(ctx: Context, project: Project, relDir: String, zip: File, prot: Set<String>, onDone: () -> Unit) {
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            try {
                ZipFile(zip).use { zf ->
                    val entries = zf.entries().asSequence().filter { !it.isDirectory }.toList()
                    val root = ProjectManager.resolveDir(project, relDir)
                    val base = zip.nameWithoutExtension.replace(Regex("[^A-Za-z0-9_\\-.\\u0600-\\u06FF ]"), "_").ifBlank { "archive" }
                    var folder = base
                    var k = 2
                    while (File(root, folder).exists()) { folder = "${base}_$k"; k++ }
                    val target = File(root, folder).apply { mkdirs() }
                    val (ok, skip) = extract(zf, entries, target, project.dir, relBase(relDir) + folder + "/", prot)
                    ui.post {
                        Toast.makeText(ctx, ctx.getString(R.string.zip_extract_result, ok, skip) + "\n$folder", Toast.LENGTH_LONG).show()
                        onDone()
                    }
                }
            } catch (t: Throwable) {
                ui.post { Toast.makeText(ctx, ctx.getString(R.string.zip_bad, t.message ?: ""), Toast.LENGTH_LONG).show() }
            }
        }.start()
    }

    /** يقرأ كل ملف داخل الأرشيف حتى نهايته (فحص CRC) ويبلّغ عن التالف. */
    fun test(ctx: Context, zip: File) {
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            val bad = mutableListOf<String>()
            var count = 0
            try {
                ZipFile(zip).use { zf ->
                    val buf = ByteArray(32 * 1024)
                    for (e in zf.entries()) {
                        if (e.isDirectory) continue
                        count++
                        try { zf.getInputStream(e).use { while (it.read(buf) >= 0) { } } } catch (_: Throwable) { bad += e.name }
                    }
                }
            } catch (t: Throwable) {
                ui.post { Toast.makeText(ctx, ctx.getString(R.string.zip_bad, t.message ?: ""), Toast.LENGTH_LONG).show() }
                return@Thread
            }
            ui.post {
                val msg = if (bad.isEmpty()) ctx.getString(R.string.zip_test_ok, count)
                else ctx.getString(R.string.zip_test_bad, bad.size, count, bad.take(10).joinToString("\n"))
                AlertDialog.Builder(ctx).setTitle(zip.name).setMessage(msg)
                    .setPositiveButton(android.R.string.ok, null).show()
            }
        }.start()
    }

    /** يضغط العناصر إلى zip مؤقت في الكاش ويفتح قائمة المشاركة. */
    fun shareAsZip(ctx: Context, entries: List<FsEntry>) {
        val list = FileBatchOps.normalize(entries)
        if (list.isEmpty()) return
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            try {
                val dir = File(ctx.cacheDir, "share_zips").apply { mkdirs() }
                val cutoff = System.currentTimeMillis() - 60 * 60 * 1000
                dir.listFiles()?.forEach { if (it.lastModified() < cutoff) it.delete() }
                val base = if (list.size == 1) list[0].name.substringBeforeLast('.') else "files"
                val out = File(dir, "$base.zip")
                ZipOutputStream(FileOutputStream(out).buffered()).use { zos ->
                    for (e in list) {
                        val f = when (e) { is FsEntry.FileEntry -> e.file.file; is FsEntry.FolderEntry -> e.folder.dir }
                        val parent = f.parentFile ?: continue
                        val files = if (f.isFile) listOf(f) else f.walkTopDown().filter { it.isFile }.toList()
                        for (x in files) {
                            zos.putNextEntry(ZipEntry(x.relativeTo(parent).path.replace('\\', '/')))
                            x.inputStream().use { it.copyTo(zos) }
                            zos.closeEntry()
                        }
                    }
                }
                ui.post {
                    try {
                        val uri = FileProvider.getUriForFile(ctx, "${ctx.packageName}.fileprovider", out)
                        val i = Intent(Intent.ACTION_SEND).apply {
                            type = "application/zip"
                            putExtra(Intent.EXTRA_STREAM, uri)
                            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                        }
                        ctx.startActivity(Intent.createChooser(i, ctx.getString(R.string.zip_share)))
                    } catch (t: Throwable) {
                        Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
                    }
                }
            } catch (t: Throwable) {
                ui.post { Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show() }
            }
        }.start()
    }
}

// ---------------------------------------------------------------------------------------------
// أدوات الوسائط
// ---------------------------------------------------------------------------------------------

object MediaTools {
    private val AUDIO = setOf("mp3", "wav", "ogg", "m4a", "aac", "flac", "opus")

    fun isAudio(name: String) = name.substringAfterLast('.', "").lowercase() in AUDIO
    fun isMedia(name: String) = ProjectManager.isImageFile(name) || ProjectManager.isVideoFile(name) || isAudio(name)
    fun canShrink(name: String) = name.substringAfterLast('.', "").lowercase() in setOf("jpg", "jpeg", "png")

    private fun fmtDuration(ms: Long): String {
        val s = ms / 1000
        return if (s >= 3600) "%d:%02d:%02d".format(s / 3600, (s % 3600) / 60, s % 60) else "%d:%02d".format(s / 60, s % 60)
    }

    fun info(ctx: Context, file: File) {
        val lines = mutableListOf<String>()
        lines += ctx.getString(R.string.info_size) + ": " + Formatter.formatFileSize(ctx, file.length())
        try {
            if (ProjectManager.isImageFile(file.name)) {
                val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeFile(file.path, o)
                if (o.outWidth > 0) lines += ctx.getString(R.string.info_dims) + ": ${o.outWidth} × ${o.outHeight}"
                o.outMimeType?.let { lines += ctx.getString(R.string.info_type) + ": $it" }
            } else {
                val r = MediaMetadataRetriever()
                try {
                    r.setDataSource(file.path)
                    r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull()
                        ?.let { lines += ctx.getString(R.string.info_duration) + ": " + fmtDuration(it) }
                    val w = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)
                    val h = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)
                    if (w != null && h != null) lines += ctx.getString(R.string.info_dims) + ": $w × $h"
                    r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_BITRATE)?.toLongOrNull()
                        ?.let { lines += ctx.getString(R.string.info_bitrate) + ": ${it / 1000} kbps" }
                } finally {
                    try { r.release() } catch (_: Throwable) {}
                }
            }
        } catch (_: Throwable) {}
        AlertDialog.Builder(ctx).setTitle(file.name).setMessage(lines.joinToString("\n"))
            .setPositiveButton(android.R.string.ok, null).show()
    }

    fun shrinkCopy(ctx: Context, file: File, onDone: () -> Unit) {
        if (!canShrink(file.name)) {
            Toast.makeText(ctx, R.string.shrink_unsupported, Toast.LENGTH_SHORT).show()
            return
        }
        val labels = arrayOf(ctx.getString(R.string.shrink_1024), ctx.getString(R.string.shrink_2048), ctx.getString(R.string.shrink_half))
        AlertDialog.Builder(ctx)
            .setTitle(R.string.row_shrink)
            .setItems(labels) { _, i ->
                val dot = file.name.lastIndexOf('.')
                val base = if (dot > 0) file.name.substring(0, dot) else file.name
                val ext = if (dot > 0) file.name.substring(dot) else ""
                var dst = File(file.parentFile, "${base}_small$ext")
                var k = 2
                while (dst.exists()) { dst = File(file.parentFile, "${base}_small$k$ext"); k++ }
                val target = dst
                val ui = Handler(Looper.getMainLooper())
                Thread {
                    val ok = when (i) {
                        0 -> scale(file, target, 1024, 0f)
                        1 -> scale(file, target, 2048, 0f)
                        else -> scale(file, target, 0, 0.5f)
                    }
                    ui.post {
                        Toast.makeText(
                            ctx,
                            if (ok) ctx.getString(R.string.shrink_done, target.name) else ctx.getString(R.string.shrink_nothing),
                            Toast.LENGTH_SHORT
                        ).show()
                        if (ok) onDone()
                    }
                }.start()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    fun shrinkInPlace(file: File, maxDim: Int): Boolean = scale(file, file, maxDim, 0f)

    /** يصغّر صورة JPG/PNG: إمّا لأقصى بُعد [maxDim] أو بنسبة [factor]. false إن لم يلزم أو فشل. */
    private fun scale(src: File, dst: File, maxDim: Int, factor: Float): Boolean {
        return try {
            val ext = src.extension.lowercase()
            val fmt = when (ext) {
                "jpg", "jpeg" -> Bitmap.CompressFormat.JPEG
                "png" -> Bitmap.CompressFormat.PNG
                else -> return false
            }
            val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(src.path, bounds)
            val w = bounds.outWidth
            val h = bounds.outHeight
            if (w <= 0 || h <= 0) return false
            val ratio = if (factor > 0f) factor else maxDim.toFloat() / maxOf(w, h)
            if (ratio >= 0.999f) return false
            val nw = (w * ratio).toInt().coerceAtLeast(1)
            val nh = (h * ratio).toInt().coerceAtLeast(1)
            var sample = 1
            while (w / (sample * 2) >= nw && h / (sample * 2) >= nh) sample *= 2
            val decoded = BitmapFactory.decodeFile(src.path, BitmapFactory.Options().apply { inSampleSize = sample })
                ?: return false
            var bmp = if (decoded.width == nw && decoded.height == nh) decoded
            else Bitmap.createScaledBitmap(decoded, nw, nh, true)
            if (fmt == Bitmap.CompressFormat.JPEG) {
                val deg = try {
                    when (ExifInterface(src.path).getAttributeInt(ExifInterface.TAG_ORIENTATION, 1)) {
                        ExifInterface.ORIENTATION_ROTATE_90 -> 90
                        ExifInterface.ORIENTATION_ROTATE_180 -> 180
                        ExifInterface.ORIENTATION_ROTATE_270 -> 270
                        else -> 0
                    }
                } catch (_: Throwable) { 0 }
                if (deg != 0) {
                    bmp = Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, Matrix().apply { postRotate(deg.toFloat()) }, true)
                }
            }
            val tmp = File(dst.parentFile, dst.name + ".tmp")
            FileOutputStream(tmp).use { bmp.compress(fmt, 90, it) }
            if (dst.exists()) dst.delete()
            tmp.renameTo(dst)
        } catch (_: Throwable) {
            false
        }
    }
}

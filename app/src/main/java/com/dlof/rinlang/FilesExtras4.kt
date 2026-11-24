package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.text.format.Formatter
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File
import java.io.FileOutputStream
import java.util.concurrent.atomic.AtomicBoolean
import java.util.zip.Deflater
import java.util.zip.ZipEntry
import java.util.zip.ZipFile
import java.util.zip.ZipOutputStream

/*
 * رفع zip المتقدّم:
 *   • ZipImport   : رفع أرشيف أو عدة أرشيفات ← ورقة خيارات (وجهة، إزالة المجلد الجذري المشترك،
 *                   تجاهل الملفات المهملة، سياسة التعارض، الاحتفاظ بنسخة الأرشيف) ← فك بتقدّم قابل
 *                   للإلغاء ← تقرير مفصّل + "تراجع عن الفك" (يحذف الجديد ويستعيد المُستبدَل).
 *   • ZipCompress : ضغط متقدّم (مستوى الضغط، استثناء المهملات) مع نسبة الضغط.
 *   • ZipAdd      : إضافة عناصر إلى أرشيف موجود (يُعاد بناؤه بأمان عبر ملف مؤقت).
 */

private val JUNK_NAMES = setOf(".ds_store", "thumbs.db", "desktop.ini")

private fun isJunk(path: String): Boolean {
    val segs = path.split('/')
    return segs.any { it == "__MACOSX" } || segs.last().lowercase() in JUNK_NAMES
}

private fun cleanZipName(raw: String): String? {
    val n = raw.replace('\\', '/').trimStart('/')
    val segs = n.split('/').filter { it.isNotEmpty() }
    if (segs.isEmpty() || segs.any { it == ".." || it == "." }) return null
    return segs.joinToString("/")
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

object ZipImport {
    private const val PREFS = "rin_zip_prefs"
    private const val MAX_TOTAL = 1L shl 30

    class Src(val file: File, val name: String, val owned: Boolean)
    class Info(val files: Int, val total: Long, val packed: Long, val root: String?, val junk: Int)
    class Opts(var named: Boolean, var flatten: Boolean, var junk: Boolean, var keep: Boolean, var policy: Conflict)

    private class Report {
        var ok = 0; var skipped = 0; var failed = 0; var bytes = 0L
        val created = ArrayList<File>()
        val newDirs = ArrayList<File>()
        val backups = ArrayList<Pair<File, File>>()
        val notes = ArrayList<String>()
    }

    private fun loadOpts(ctx: Context): Opts {
        val p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        return Opts(
            p.getBoolean("named", false), p.getBoolean("flatten", true), p.getBoolean("junk", true),
            p.getBoolean("keep", false), Conflict.values()[p.getInt("policy", 0).coerceIn(0, 2)]
        )
    }

    private fun saveOpts(ctx: Context, o: Opts) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean("named", o.named).putBoolean("flatten", o.flatten).putBoolean("junk", o.junk)
            .putBoolean("keep", o.keep).putInt("policy", o.policy.ordinal).apply()
    }

    private fun displayName(ctx: Context, uri: Uri): String {
        try {
            ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0)?.let { n -> if (n.isNotBlank()) return n }
            }
        } catch (_: Throwable) {}
        return uri.lastPathSegment?.substringAfterLast('/') ?: "archive.zip"
    }

    private fun analyze(zip: File): Info {
        ZipFile(zip).use { zf ->
            var files = 0; var total = 0L; var packed = 0L; var junk = 0
            val firsts = HashSet<String>()
            var allNested = true
            for (e in zf.entries()) {
                if (e.isDirectory) continue
                val n = cleanZipName(e.name) ?: continue
                files++
                total += maxOf(e.size, 0L)
                packed += maxOf(e.compressedSize, 0L)
                if (isJunk(n)) { junk++; continue }
                if (!n.contains('/')) allNested = false
                firsts += n.substringBefore('/')
            }
            val root = if (allNested && firsts.size == 1) firsts.first() else null
            return Info(files, total, packed, root, junk)
        }
    }

    /** أرشيفات مختارة من الجهاز: تُنسخ إلى الكاش ثم تُحلَّل. */
    fun fromUris(ctx: Context, project: Project, relDir: String, uris: List<Uri>, prot: Set<String>, onDone: () -> Unit) {
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            val srcs = ArrayList<Src>()
            try {
                val dir = File(ctx.cacheDir, "zip_tmp").apply { mkdirs() }
                for ((i, u) in uris.withIndex()) {
                    val tmp = File(dir, "${System.currentTimeMillis()}_$i.zip")
                    ctx.contentResolver.openInputStream(u)!!.use { ins -> FileOutputStream(tmp).use { ins.copyTo(it) } }
                    srcs += Src(tmp, displayName(ctx, u), true)
                }
            } catch (t: Throwable) {
                srcs.forEach { it.file.delete() }
                ui.post { Toast.makeText(ctx, ctx.getString(R.string.zip_bad, t.message ?: ""), Toast.LENGTH_LONG).show() }
                return@Thread
            }
            ui.post { sheet(ctx, project, relDir, srcs, prot, onDone) }
        }.start()
    }

    /** أرشيفات موجودة داخل المشروع (لا تُحذف ولا تُنقل). */
    fun fromFile(ctx: Context, project: Project, relDir: String, zip: File, prot: Set<String>, onDone: () -> Unit) {
        sheet(ctx, project, relDir, listOf(Src(zip, zip.name, false)), prot, onDone)
    }

    private fun sheet(ctx: Context, project: Project, relDir: String, srcs: List<Src>, prot: Set<String>, onDone: () -> Unit) {
        fun cleanup() = srcs.filter { it.owned }.forEach { it.file.delete() }
        val infos = try { srcs.map { analyze(it.file) } } catch (t: Throwable) {
            Toast.makeText(ctx, ctx.getString(R.string.zip_bad, t.message ?: ""), Toast.LENGTH_LONG).show()
            cleanup(); return
        }
        val opts = loadOpts(ctx)
        val d = ctx.resources.displayMetrics.density
        fun pad(v: android.view.View, top: Int = 0) = v.apply { setPadding(0, (top * d).toInt(), 0, 0) }

        val files = infos.sumOf { it.files }
        val summary = TextView(ctx).apply {
            text = if (srcs.size == 1)
                ctx.getString(R.string.zipimp_summary, files, Formatter.formatFileSize(ctx, infos[0].total), Formatter.formatFileSize(ctx, infos[0].packed))
            else ctx.getString(R.string.zipimp_summary_many, srcs.size, files)
            textSize = 12.5f
        }
        val destGroup = RadioGroup(ctx)
        val rbHere = RadioButton(ctx).apply { id = 101; text = ctx.getString(R.string.zipimp_dest_here) }
        val rbNamed = RadioButton(ctx).apply { id = 102; text = ctx.getString(R.string.zipimp_dest_named) }
        destGroup.addView(rbHere); destGroup.addView(rbNamed)
        destGroup.check(if (opts.named) 102 else 101)

        val root = if (srcs.size == 1) infos[0].root else null
        val cbFlatten = CheckBox(ctx).apply {
            text = if (root != null) ctx.getString(R.string.zipimp_flatten, root) else ctx.getString(R.string.zipimp_flatten_generic)
            isChecked = opts.flatten
        }
        val junkCount = infos.sumOf { it.junk }
        val cbJunk = CheckBox(ctx).apply { text = ctx.getString(R.string.zipimp_junk, junkCount); isChecked = opts.junk }
        val cbKeep = CheckBox(ctx).apply {
            text = ctx.getString(R.string.zipimp_keep); isChecked = opts.keep
            visibility = if (srcs.any { it.owned }) android.view.View.VISIBLE else android.view.View.GONE
        }
        val polGroup = RadioGroup(ctx)
        val polLabels = listOf(R.string.conflict_keep, R.string.conflict_skip, R.string.conflict_replace)
        for ((i, l) in polLabels.withIndex()) polGroup.addView(RadioButton(ctx).apply { id = 201 + i; text = ctx.getString(l) })
        polGroup.check(201 + opts.policy.ordinal)

        fun label(r: Int) = TextView(ctx).apply { text = ctx.getString(r); textSize = 13f; setTypeface(typeface, android.graphics.Typeface.BOLD) }
        val box = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((22 * d).toInt(), (10 * d).toInt(), (22 * d).toInt(), 0)
            addView(summary)
            addView(pad(label(R.string.zipimp_dest), 12)); addView(destGroup)
            addView(cbFlatten); addView(cbJunk); addView(cbKeep)
            addView(pad(label(R.string.zipimp_policy), 10)); addView(polGroup)
        }

        var handedOff = false
        val b = AlertDialog.Builder(ctx)
            .setTitle(R.string.zipimp_title)
            .setView(ScrollView(ctx).apply { addView(box) })
            .setPositiveButton(R.string.zipimp_extract) { _, _ ->
                val o = Opts(
                    destGroup.checkedRadioButtonId == 102, cbFlatten.isChecked, cbJunk.isChecked, cbKeep.isChecked,
                    Conflict.values()[(polGroup.checkedRadioButtonId - 201).coerceIn(0, 2)]
                )
                saveOpts(ctx, o)
                handedOff = true
                run(ctx, project, relDir, srcs, o, prot, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .setOnDismissListener { if (!handedOff) cleanup() }
        if (srcs.size == 1) {
            b.setNeutralButton(R.string.zipimp_browse) { _, _ ->
                handedOff = true
                ZipTools.browseFile(ctx, project, relDir, srcs[0].file, prot, srcs[0].owned, onDone)
            }
        }
        b.show()
    }

    private fun mkdirsTracked(dir: File, rep: Report) {
        val chain = ArrayList<File>()
        var d: File? = dir
        while (d != null && !d.exists()) { chain += d; d = d.parentFile }
        dir.mkdirs()
        rep.newDirs += chain
    }

    private fun run(ctx: Context, project: Project, relDir: String, srcs: List<Src>, o: Opts, prot: Set<String>, onDone: () -> Unit) {
        val ui = Handler(Looper.getMainLooper())
        val d = ctx.resources.displayMetrics.density
        val bar = ProgressBar(ctx, null, android.R.attr.progressBarStyleHorizontal).apply { isIndeterminate = true }
        val msg = TextView(ctx)
        val pbox = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((22 * d).toInt(), (14 * d).toInt(), (22 * d).toInt(), 0)
            addView(msg); addView(bar)
        }
        val cancelled = AtomicBoolean(false)
        val pdlg = AlertDialog.Builder(ctx).setTitle(R.string.zipimp_title).setView(pbox).setCancelable(false)
            .setNegativeButton(R.string.cancel) { _, _ -> cancelled.set(true) }.show()

        val projectRoot = project.dir.canonicalFile
        val destRoot = ProjectManager.resolveDir(project, relDir)
        val relBase = if (relDir.isBlank()) "" else relDir.trim('/') + "/"
        val undoDir = File(ctx.cacheDir, "zip_undo/${System.currentTimeMillis()}").apply { mkdirs() }

        Thread {
            val rep = Report()
            for (src in srcs) {
                if (cancelled.get()) break
                try {
                    ZipFile(src.file).use { zf ->
                        val info = analyze(src.file)
                        var target = destRoot
                        var base = relBase
                        if (o.named) {
                            val nm = src.name.substringBeforeLast('.').replace(Regex("[^A-Za-z0-9_\\-.\\u0600-\\u06FF ]"), "_").ifBlank { "archive" }
                            var folder = nm; var k = 2
                            while (File(destRoot, folder).exists()) { folder = "${nm}_$k"; k++ }
                            target = File(destRoot, folder)
                            mkdirsTracked(target, rep)
                            base = relBase + folder + "/"
                        }
                        val strip = if (o.flatten) info.root else null
                        val tRoot = target.canonicalFile
                        val all = zf.entries().asSequence().filter { !it.isDirectory }.toList()
                        var n = 0
                        for (e in all) {
                            if (cancelled.get()) break
                            n++
                            if (n % 15 == 0 || n == all.size) {
                                val shown = n
                                ui.post { msg.text = ctx.getString(R.string.zipimp_progress, src.name, shown, all.size) }
                            }
                            var name = cleanZipName(e.name)
                            if (name == null) { rep.skipped++; rep.notes += ctx.getString(R.string.zipimp_note_unsafe, e.name); continue }
                            if (o.junk && isJunk(name)) { rep.skipped++; continue }
                            if (strip != null && name.startsWith("$strip/")) name = name.removePrefix("$strip/")
                            if (name.isEmpty()) continue
                            var out = File(tRoot, name).canonicalFile
                            if (!out.path.startsWith(projectRoot.path + File.separator)) {
                                rep.skipped++; rep.notes += ctx.getString(R.string.zipimp_note_unsafe, e.name); continue
                            }
                            if (out.exists()) {
                                when (o.policy) {
                                    Conflict.SKIP -> { rep.skipped++; continue }
                                    Conflict.KEEP_BOTH -> out = uniqueFile(out)
                                    Conflict.REPLACE -> {
                                        if ((base + name) in prot) {
                                            rep.skipped++; rep.notes += ctx.getString(R.string.zipimp_note_protected, name); continue
                                        }
                                        val bk = File(undoDir, "${rep.backups.size}.bak")
                                        if (out.renameTo(bk)) rep.backups += out to bk
                                        else { rep.failed++; rep.notes += ctx.getString(R.string.zipimp_note_failed, name); continue }
                                    }
                                }
                            }
                            try {
                                out.parentFile?.let { mkdirsTracked(it, rep) }
                                zf.getInputStream(e).use { ins ->
                                    FileOutputStream(out).use { os ->
                                        val buf = ByteArray(32 * 1024)
                                        while (true) {
                                            val r = ins.read(buf)
                                            if (r < 0) break
                                            rep.bytes += r
                                            if (rep.bytes > MAX_TOTAL) throw IllegalStateException("too big")
                                            os.write(buf, 0, r)
                                        }
                                    }
                                }
                                if (e.time > 0) out.setLastModified(e.time)
                                rep.created += out
                                rep.ok++
                            } catch (t: Throwable) {
                                out.delete()
                                rep.failed++; rep.notes += ctx.getString(R.string.zipimp_note_failed, name)
                                if (rep.bytes > MAX_TOTAL) { cancelled.set(true); break }
                            }
                        }
                    }
                    if (o.keep && src.owned && !cancelled.get()) {
                        val keepName = src.name.replace(Regex("[^A-Za-z0-9_\\-.\\u0600-\\u06FF ]"), "_")
                        val dst = uniqueFile(File(destRoot, keepName))
                        if (src.file.renameTo(dst)) rep.created += dst
                    }
                } catch (t: Throwable) {
                    rep.failed++; rep.notes += ctx.getString(R.string.zip_bad, t.message ?: src.name)
                }
                if (src.owned) src.file.delete()
            }
            if (cancelled.get()) srcs.filter { it.owned }.forEach { it.file.delete() }
            ui.post {
                pdlg.dismiss()
                report(ctx, rep, cancelled.get(), undoDir, onDone)
            }
        }.start()
    }

    private fun report(ctx: Context, rep: Report, wasCancelled: Boolean, undoDir: File, onDone: () -> Unit) {
        val sb = StringBuilder()
        if (wasCancelled) sb.append(ctx.getString(R.string.zipimp_cancelled)).append('\n')
        sb.append(ctx.getString(R.string.zipimp_result, rep.ok, rep.skipped, rep.failed, Formatter.formatFileSize(ctx, rep.bytes)))
        if (rep.notes.isNotEmpty()) {
            sb.append("\n\n").append(rep.notes.take(12).joinToString("\n"))
            if (rep.notes.size > 12) sb.append('\n').append(ctx.getString(R.string.zipimp_notes_more, rep.notes.size - 12))
        }
        val b = AlertDialog.Builder(ctx)
            .setTitle(R.string.zipimp_title)
            .setMessage(sb.toString())
            .setPositiveButton(android.R.string.ok, null)
            .setOnDismissListener { undoDir.deleteRecursively(); onDone() }
        if (rep.created.isNotEmpty() || rep.backups.isNotEmpty()) {
            b.setNeutralButton(R.string.zipimp_undo, null)
        }
        val dlg = b.show()
        dlg.getButton(AlertDialog.BUTTON_NEUTRAL)?.setOnClickListener {
            var deleted = 0; var restored = 0
            for (f in rep.created) if (f.delete()) deleted++
            for ((orig, bk) in rep.backups) {
                orig.parentFile?.mkdirs()
                if (bk.renameTo(orig)) restored++
            }
            for (dir in rep.newDirs.sortedByDescending { it.path.length }) {
                if (dir.isDirectory && dir.list().isNullOrEmpty()) dir.delete()
            }
            Toast.makeText(ctx, ctx.getString(R.string.zipimp_undone, deleted, restored), Toast.LENGTH_LONG).show()
            dlg.setOnDismissListener { undoDir.deleteRecursively(); onDone() }
            dlg.dismiss()
        }
    }
}

// ---------------------------------------------------------------------------------------------
// ضغط متقدّم
// ---------------------------------------------------------------------------------------------

object ZipCompress {
    fun show(ctx: Context, project: Project, entries: List<FsEntry>, destRelDir: String, onDone: () -> Unit) {
        val list = FileBatchOps.normalize(entries)
        if (list.isEmpty()) return
        val d = ctx.resources.displayMetrics.density
        val name = EditText(ctx).apply {
            hint = ctx.getString(R.string.zip_name_hint)
            setText(if (list.size == 1) list[0].name.substringBeforeLast('.') else "archive")
        }
        val levels = listOf(R.string.zc_level0 to 0, R.string.zc_level1 to 1, R.string.zc_level2 to 6, R.string.zc_level3 to 9)
        val group = RadioGroup(ctx)
        for ((i, l) in levels.withIndex()) group.addView(RadioButton(ctx).apply { id = 301 + i; text = ctx.getString(l.first) })
        group.check(303)
        val junk = CheckBox(ctx).apply { text = ctx.getString(R.string.zc_junk); isChecked = true }
        val box = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((22 * d).toInt(), (10 * d).toInt(), (22 * d).toInt(), 0)
            addView(name)
            addView(TextView(ctx).apply { text = ctx.getString(R.string.zc_level); setPadding(0, (10 * d).toInt(), 0, 0) })
            addView(group); addView(junk)
        }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.zc_title)
            .setView(ScrollView(ctx).apply { addView(box) })
            .setPositiveButton(R.string.create) { _, _ ->
                val base = name.text.toString().trim().removeSuffix(".zip")
                if (!ProjectManager.isValidPathSegment(base)) return@setPositiveButton
                val level = levels[(group.checkedRadioButtonId - 301).coerceIn(0, 3)].second
                run(ctx, project, list, destRelDir, base, level, junk.isChecked, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun run(ctx: Context, project: Project, list: List<FsEntry>, destRelDir: String, base: String, level: Int, skipJunk: Boolean, onDone: () -> Unit) {
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            try {
                val dir = ProjectManager.resolveDir(project, destRelDir)
                val out = uniqueFile(File(dir, "$base.zip"))
                var count = 0; var raw = 0L
                ZipOutputStream(FileOutputStream(out).buffered()).use { zos ->
                    zos.setLevel(level)
                    for (e in list) {
                        val f = when (e) { is FsEntry.FileEntry -> e.file.file; is FsEntry.FolderEntry -> e.folder.dir }
                        val parent = f.parentFile ?: continue
                        val files = if (f.isFile) listOf(f) else f.walkTopDown().filter { it.isFile }.toList()
                        for (x in files) {
                            if (x.canonicalPath == out.canonicalPath) continue
                            val rel = x.relativeTo(parent).path.replace('\\', '/')
                            if (skipJunk && isJunk(rel)) continue
                            zos.putNextEntry(ZipEntry(rel).apply { time = x.lastModified() })
                            x.inputStream().use { it.copyTo(zos) }
                            zos.closeEntry()
                            count++; raw += x.length()
                        }
                    }
                }
                val pct = if (raw > 0) (out.length() * 100 / raw).toInt() else 100
                ui.post {
                    Toast.makeText(ctx, ctx.getString(R.string.zc_done, out.name, count, Formatter.formatFileSize(ctx, out.length()), pct), Toast.LENGTH_LONG).show()
                    onDone()
                }
            } catch (t: Throwable) {
                ui.post { Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show() }
            }
        }.start()
    }
}

// ---------------------------------------------------------------------------------------------
// إضافة إلى أرشيف موجود
// ---------------------------------------------------------------------------------------------

object ZipAdd {
    fun add(ctx: Context, zip: File, entries: List<FsEntry>, onDone: () -> Unit) {
        val list = FileBatchOps.normalize(entries)
        if (list.isEmpty()) return
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            val tmp = File(zip.parentFile, zip.name + ".tmp")
            try {
                // الملفات الجديدة: اسم الدخول ← الملف (الأحدث يحلّ محلّ القديم بنفس الاسم).
                val fresh = LinkedHashMap<String, File>()
                for (e in list) {
                    val f = when (e) { is FsEntry.FileEntry -> e.file.file; is FsEntry.FolderEntry -> e.folder.dir }
                    val parent = f.parentFile ?: continue
                    val files = if (f.isFile) listOf(f) else f.walkTopDown().filter { it.isFile }.toList()
                    for (x in files) {
                        if (x.canonicalPath == zip.canonicalPath) continue
                        fresh[x.relativeTo(parent).path.replace('\\', '/')] = x
                    }
                }
                ZipOutputStream(FileOutputStream(tmp).buffered()).use { zos ->
                    ZipFile(zip).use { zf ->
                        for (e in zf.entries()) {
                            if (e.name in fresh) continue
                            zos.putNextEntry(ZipEntry(e.name).apply { if (e.time > 0) time = e.time })
                            if (!e.isDirectory) zf.getInputStream(e).use { it.copyTo(zos) }
                            zos.closeEntry()
                        }
                    }
                    for ((name, f) in fresh) {
                        zos.putNextEntry(ZipEntry(name).apply { time = f.lastModified() })
                        f.inputStream().use { it.copyTo(zos) }
                        zos.closeEntry()
                    }
                }
                if (!zip.delete() || !tmp.renameTo(zip)) throw IllegalStateException("replace failed")
                ui.post {
                    Toast.makeText(ctx, ctx.getString(R.string.zadd_done, fresh.size, zip.name), Toast.LENGTH_LONG).show()
                    onDone()
                }
            } catch (t: Throwable) {
                tmp.delete()
                ui.post { Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show() }
            }
        }.start()
    }
}

package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Context
import android.text.InputType
import android.text.format.Formatter
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.PopupMenu
import android.widget.Toast
import java.io.File
import java.text.DateFormat
import java.util.Date

/*
 * أدوات إدارة ملفات إضافية (تُكمل FileSelection.kt و FolderCreator.kt):
 *   • FileSort        : فرز القائمة (الاسم/التاريخ/الحجم/النوع، تصاعدي/تنازلي) ويُحفظ الاختيار.
 *   • FileClipboard   : نسخ / قص / لصق بين المجلدات.
 *   • BatchRename     : إعادة تسمية جماعية (بحث واستبدال، بادئة/لاحقة، ترقيم).
 *   • FileProps       : خصائص العناصر المحددة (العدد، الحجم الكلي، المسار، آخر تعديل).
 *   • FileCreator     : إنشاء عدة ملفات دفعة واحدة أو من قوالب جاهزة.
 *   • SelectionMenu   : قائمة "المزيد" الموحّدة لشريط التحديد.
 */

// ---------------------------------------------------------------------------------------------
// الفرز
// ---------------------------------------------------------------------------------------------

object FileSort {
    enum class Key { NAME, DATE, SIZE, TYPE }

    private const val PREFS = "rin_file_sort"

    fun key(ctx: Context): Key =
        runCatching { Key.valueOf(ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("key", "NAME")!!) }
            .getOrDefault(Key.NAME)

    fun descending(ctx: Context): Boolean =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("desc", false)

    fun save(ctx: Context, key: Key, desc: Boolean) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString("key", key.name).putBoolean("desc", desc).apply()
    }

    private fun <T> pinnedFirst(sorted: List<T>, pins: Set<String>, rel: (T) -> String): List<T> =
        if (pins.isEmpty()) sorted else sorted.filter { rel(it) in pins } + sorted.filter { rel(it) !in pins }

    fun folders(ctx: Context, list: List<RinFolder>, pins: Set<String> = emptySet()): List<RinFolder> {
        val k = key(ctx)
        val sorted = when (k) {
            Key.DATE -> list.sortedBy { it.lastModified }
            else -> list.sortedBy { it.name.lowercase() }
        }
        return pinnedFirst(if (descending(ctx)) sorted.reversed() else sorted, pins) { it.relPath }
    }

    fun files(ctx: Context, list: List<RinFile>, pins: Set<String> = emptySet()): List<RinFile> {
        val sorted = when (key(ctx)) {
            Key.NAME -> list.sortedBy { it.name.lowercase() }
            Key.DATE -> list.sortedBy { it.lastModified }
            Key.SIZE -> list.sortedBy { it.sizeBytes }
            Key.TYPE -> list.sortedWith(compareBy({ it.name.substringAfterLast('.', "").lowercase() }, { it.name.lowercase() }))
        }
        return pinnedFirst(if (descending(ctx)) sorted.reversed() else sorted, pins) { it.relPath }
    }

    fun showDialog(ctx: Context, onChanged: () -> Unit) {
        val keys = Key.values()
        val labels = arrayOf(
            ctx.getString(R.string.sort_name), ctx.getString(R.string.sort_date),
            ctx.getString(R.string.sort_size), ctx.getString(R.string.sort_type)
        )
        val cur = key(ctx)
        AlertDialog.Builder(ctx)
            .setTitle(R.string.sort_title)
            .setSingleChoiceItems(labels, keys.indexOf(cur)) { d, which ->
                // اختيار نفس المفتاح مرة ثانية يعكس الاتجاه.
                val desc = if (keys[which] == cur) !descending(ctx) else false
                save(ctx, keys[which], desc)
                d.dismiss()
                onChanged()
            }
            .setNeutralButton(R.string.filter_title) { _, _ -> FileFilter.showDialog(ctx, onChanged) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

// ---------------------------------------------------------------------------------------------
// نسخ / قص / لصق
// ---------------------------------------------------------------------------------------------

object FileClipboard {
    var sources: List<File> = emptyList()
        private set
    var cut: Boolean = false
        private set

    val hasContent: Boolean get() = sources.isNotEmpty()

    fun set(entries: List<FsEntry>, cut: Boolean) {
        sources = FileBatchOps.normalize(entries).map {
            when (it) {
                is FsEntry.FileEntry -> it.file.file
                is FsEntry.FolderEntry -> it.folder.dir
            }
        }
        this.cut = cut
    }

    fun clear() { sources = emptyList(); cut = false }

    private fun uniqueTarget(dir: File, name: String, isDir: Boolean): File {
        var t = File(dir, name)
        if (!t.exists()) return t
        val dot = name.lastIndexOf('.')
        val base = if (!isDir && dot > 0) name.substring(0, dot) else name
        val ext = if (!isDir && dot > 0) name.substring(dot) else ""
        var n = 2
        do { t = File(dir, "$base ($n)$ext"); n++ } while (t.exists())
        return t
    }

    /** يلصق المحتوى داخل [destRelDir]. النسخ يُنتج أسماء فريدة تلقائياً؛ القص ينقل ويتخطى المتعارض. */
    fun paste(ctx: Context, project: Project, destRelDir: String, protectedPaths: Set<String>, onDone: () -> Unit) {
        if (!hasContent) return
        val destDir = ProjectManager.resolveDir(project, destRelDir).also { it.mkdirs() }
        val root = project.dir.canonicalPath
        var ok = 0; var skipped = 0; var failed = 0
        for (src in sources) {
            if (!src.exists()) { failed++; continue }
            val rel = src.canonicalPath.removePrefix(root).trim(File.separatorChar).replace(File.separatorChar, '/')
            val isProtected = cut && protectedPaths.any { p -> p == rel || p.startsWith("$rel/") }
            val intoSelf = src.isDirectory &&
                (destDir.canonicalPath == src.canonicalPath || destDir.canonicalPath.startsWith(src.canonicalPath + File.separator))
            if (isProtected || intoSelf) { skipped++; continue }
            try {
                if (cut) {
                    val t = File(destDir, src.name)
                    when {
                        t.canonicalPath == src.canonicalPath -> skipped++
                        t.exists() -> skipped++
                        src.renameTo(t) -> ok++
                        else -> failed++
                    }
                } else {
                    val t = uniqueTarget(destDir, src.name, src.isDirectory)
                    val good = if (src.isDirectory) src.copyRecursively(t, overwrite = false) else { src.copyTo(t); true }
                    if (good) ok++ else failed++
                }
            } catch (_: Throwable) { failed++ }
        }
        if (cut && ok > 0) clear()
        val parts = mutableListOf<String>()
        if (ok > 0) parts += ctx.getString(R.string.selection_done_toast, ok)
        if (skipped > 0) parts += ctx.getString(R.string.selection_skipped_toast, skipped)
        if (failed > 0) parts += ctx.getString(R.string.selection_failed_toast, failed)
        if (parts.isNotEmpty()) Toast.makeText(ctx, parts.joinToString("\n"), Toast.LENGTH_LONG).show()
        onDone()
    }
}

// ---------------------------------------------------------------------------------------------
// إعادة التسمية الجماعية
// ---------------------------------------------------------------------------------------------

object BatchRename {

    fun show(ctx: Context, project: Project, entries: List<FsEntry>, protectedPaths: Set<String>, onDone: () -> Unit) {
        val list = FileBatchOps.normalize(entries)
        if (list.isEmpty()) return
        val modes = arrayOf(
            ctx.getString(R.string.brename_replace),
            ctx.getString(R.string.brename_affix),
            ctx.getString(R.string.brename_number)
        )
        AlertDialog.Builder(ctx)
            .setTitle(ctx.getString(R.string.brename_title, list.size))
            .setItems(modes) { _, which ->
                when (which) {
                    0 -> askTwo(ctx, R.string.brename_replace, R.string.brename_find, R.string.brename_with) { a, b ->
                        if (a.isNotEmpty()) apply(ctx, list, protectedPaths, onDone) { _, e -> e.name.replace(a, b) }
                    }
                    1 -> askTwo(ctx, R.string.brename_affix, R.string.brename_prefix, R.string.brename_suffix) { a, b ->
                        apply(ctx, list, protectedPaths, onDone) { _, e ->
                            val n = e.name
                            val dot = n.lastIndexOf('.')
                            if (!e.isFolder && dot > 0) a + n.substring(0, dot) + b + n.substring(dot) else a + n + b
                        }
                    }
                    2 -> numbering(ctx, list, protectedPaths, onDone)
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun field(ctx: Context, hint: String): EditText = EditText(ctx).apply {
        this.hint = hint
        inputType = InputType.TYPE_CLASS_TEXT
    }

    private fun box(ctx: Context, vararg v: View): LinearLayout = LinearLayout(ctx).apply {
        orientation = LinearLayout.VERTICAL
        val p = (20 * ctx.resources.displayMetrics.density).toInt()
        setPadding(p, p / 2, p, 0)
        v.forEach { addView(it) }
    }

    /** حوار بحقلين نصيين؛ عند التأكيد يستدعي [onOk] بالقيمتين. */
    private fun askTwo(ctx: Context, title: Int, hintA: Int, hintB: Int, onOk: (String, String) -> Unit) {
        val a = field(ctx, ctx.getString(hintA))
        val b = field(ctx, ctx.getString(hintB))
        AlertDialog.Builder(ctx)
            .setTitle(title)
            .setView(box(ctx, a, b))
            .setPositiveButton(R.string.rename) { _, _ -> onOk(a.text.toString(), b.text.toString()) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun numbering(ctx: Context, list: List<FsEntry>, prot: Set<String>, onDone: () -> Unit) {
        val base = field(ctx, ctx.getString(R.string.brename_base))
        val start = field(ctx, ctx.getString(R.string.brename_start)).apply {
            inputType = InputType.TYPE_CLASS_NUMBER; setText("1")
        }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.brename_number)
            .setView(box(ctx, base, start))
            .setPositiveButton(R.string.rename) { _, _ ->
                val b = base.text.toString().trim()
                if (b.isEmpty()) return@setPositiveButton
                val s = start.text.toString().toIntOrNull() ?: 1
                val width = (s + list.size - 1).toString().length
                apply(ctx, list, prot, onDone) { i, e ->
                    val dot = e.name.lastIndexOf('.')
                    val ext = if (!e.isFolder && dot > 0) e.name.substring(dot) else ""
                    b + "_" + (s + i).toString().padStart(width, '0') + ext
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun apply(
        ctx: Context, list: List<FsEntry>, prot: Set<String>, onDone: () -> Unit,
        newName: (Int, FsEntry) -> String
    ) {
        var ok = 0; var skipped = 0
        list.forEachIndexed { i, e ->
            val src = when (e) { is FsEntry.FileEntry -> e.file.file; is FsEntry.FolderEntry -> e.folder.dir }
            val name = newName(i, e).trim()
            val blocked = prot.any { p -> p == e.relPath || (e.isFolder && p.startsWith(e.relPath + "/")) }
            val target = File(src.parentFile, name)
            if (blocked || name == e.name || !ProjectManager.isValidPathSegment(name) || target.exists() || !src.renameTo(target)) skipped++ else ok++
        }
        val parts = mutableListOf<String>()
        if (ok > 0) parts += ctx.getString(R.string.selection_done_toast, ok)
        if (skipped > 0) parts += ctx.getString(R.string.selection_skipped_toast, skipped)
        Toast.makeText(ctx, parts.joinToString("\n"), Toast.LENGTH_LONG).show()
        onDone()
    }
}

// ---------------------------------------------------------------------------------------------
// الخصائص
// ---------------------------------------------------------------------------------------------

object FileProps {
    private fun sizeOf(f: File): Long = if (f.isFile) f.length() else f.walkTopDown().filter { it.isFile }.sumOf { it.length() }

    fun show(ctx: Context, entries: List<FsEntry>) {
        val list = FileBatchOps.normalize(entries)
        if (list.isEmpty()) return
        var files = 0; var folders = 0; var total = 0L; var latest = 0L
        for (e in list) {
            val f = when (e) { is FsEntry.FileEntry -> e.file.file; is FsEntry.FolderEntry -> e.folder.dir }
            if (e.isFolder) {
                folders++
                f.walkTopDown().forEach { if (it.isFile) files++ }
            } else files++
            total += sizeOf(f)
            latest = maxOf(latest, f.lastModified())
        }
        val sb = StringBuilder()
        if (list.size == 1) sb.append(ctx.getString(R.string.props_path, list[0].relPath)).append('\n')
        sb.append(ctx.getString(R.string.props_counts, files, folders)).append('\n')
        sb.append(ctx.getString(R.string.props_size, Formatter.formatFileSize(ctx, total))).append('\n')
        sb.append(ctx.getString(R.string.props_modified, DateFormat.getDateTimeInstance().format(Date(latest))))
        AlertDialog.Builder(ctx)
            .setTitle(if (list.size == 1) list[0].name else ctx.getString(R.string.selection_count_format, list.size))
            .setMessage(sb.toString())
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }
}

// ---------------------------------------------------------------------------------------------
// إنشاء عدة ملفات / قوالب
// ---------------------------------------------------------------------------------------------

object FileCreator {
    private val TEMPLATES = listOf(
        "README.md" to "# \n",
        "notes.txt" to "",
        "config.json" to "{\n}\n",
        "main.rin" to ""
    )

    fun show(ctx: Context, project: Project, relDir: String, onSingle: () -> Unit, onDone: () -> Unit) {
        val labels = arrayOf(ctx.getString(R.string.mkfile_single), ctx.getString(R.string.mkfile_multi), ctx.getString(R.string.mkfile_template), ctx.getString(R.string.mkfile_clipboard))
        AlertDialog.Builder(ctx)
            .setTitle(R.string.new_file_title)
            .setItems(labels) { _, which ->
                when (which) {
                    0 -> onSingle()
                    1 -> multi(ctx, project, relDir, onDone)
                    3 -> ClipboardFile.show(ctx, project, relDir, onDone)
                    else -> template(ctx, project, relDir, onDone)
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun multi(ctx: Context, project: Project, relDir: String, onDone: () -> Unit) {
        val input = EditText(ctx).apply {
            hint = ctx.getString(R.string.mkfile_multi_hint)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
            minLines = 3
        }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.mkfile_multi)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                create(ctx, project, relDir, input.text.split('\n', ',', '،').map { it.trim() }.filter { it.isNotEmpty() }.map { it to "" }, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun template(ctx: Context, project: Project, relDir: String, onDone: () -> Unit) {
        val checked = BooleanArray(TEMPLATES.size)
        AlertDialog.Builder(ctx)
            .setTitle(R.string.mkfile_template)
            .setMultiChoiceItems(TEMPLATES.map { it.first }.toTypedArray(), checked) { _, i, on -> checked[i] = on }
            .setPositiveButton(R.string.create) { _, _ ->
                create(ctx, project, relDir, TEMPLATES.filterIndexed { i, _ -> checked[i] }, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** ينشئ ملفات (يدعم "a/b/file.ext"). لا يستبدل ملفاً موجوداً؛ اسم بلا امتداد يأخذ .rin. */
    private fun create(ctx: Context, project: Project, relDir: String, items: List<Pair<String, String>>, onDone: () -> Unit) {
        var ok = 0; var skipped = 0
        for ((rawName, content) in items) {
            try {
                val segs = rawName.trim().trim('/').split('/').map { it.trim() }.filter { it.isNotEmpty() }
                require(segs.isNotEmpty() && segs.all { it != "." && it != ".." && ProjectManager.isValidPathSegment(it) })
                var name = segs.last()
                if ('.' !in name) name += ".rin"
                val dir = File(ProjectManager.resolveDir(project, relDir), segs.dropLast(1).joinToString("/")).also { it.mkdirs() }
                val f = File(dir, name)
                if (f.exists()) skipped++ else { f.writeText(content); ok++ }
            } catch (_: Throwable) { skipped++ }
        }
        val parts = mutableListOf<String>()
        if (ok > 0) parts += ctx.getString(R.string.selection_done_toast, ok)
        if (skipped > 0) parts += ctx.getString(R.string.selection_skipped_toast, skipped)
        if (parts.isNotEmpty()) Toast.makeText(ctx, parts.joinToString("\n"), Toast.LENGTH_LONG).show()
        onDone()
    }
}

// ---------------------------------------------------------------------------------------------
// قائمة "المزيد" لشريط التحديد
// ---------------------------------------------------------------------------------------------

object SelectionMenu {
    /**
     * @param parentRelDir مجلد الإنشاء/اللصق الافتراضي.
     * @param onInvert عكس التحديد (اختياري).
     * @param onChanged يُستدعى بعد أي تغيير على القرص أو حافظة النسخ (للتحديث وإلغاء التحديد).
     */
    fun show(
        ctx: Context, anchor: View, project: Project, entries: List<FsEntry>, parentRelDir: String,
        protectedPaths: Set<String>, onInvert: (() -> Unit)?, onChanged: () -> Unit,
        onSelectPattern: (() -> Unit)? = null
    ) {
        val pm = PopupMenu(ctx, anchor)
        val m = pm.menu
        m.add(0, 1, 0, R.string.tool_copy)
        m.add(0, 2, 1, R.string.tool_cut)
        m.add(0, 3, 2, R.string.action_duplicate)
        m.add(0, 4, 3, R.string.action_move)
        m.add(0, 5, 4, R.string.brename_menu)
        m.add(0, 6, 5, R.string.new_folder_title)
        m.add(0, 7, 6, R.string.action_copy_path)
        if (onInvert != null) m.add(0, 8, 7, R.string.selection_invert)
        m.add(0, 9, 8, R.string.props_title)
        m.add(0, 10, 9, R.string.pin_toggle)
        m.add(0, 11, 10, R.string.zip_compress)
        if (entries.any { it is FsEntry.FileEntry && it.name.endsWith(".zip", true) }) m.add(0, 12, 11, R.string.zip_extract_here)
        if (onSelectPattern != null) m.add(0, 13, 12, R.string.pattern_title)
        m.add(0, 14, 13, R.string.tool_share)
        if (entries.size == 1 && entries[0] is FsEntry.FileEntry && !ProjectManager.isBinaryFile(entries[0].name)) {
            m.add(0, 15, 14, R.string.tool_preview)
        }
        val twoText = entries.size == 2 && entries.all { it is FsEntry.FileEntry && !ProjectManager.isBinaryFile(it.name) }
        if (twoText) m.add(0, 16, 15, R.string.tool_compare)
        m.add(0, 17, 16, R.string.zip_share)
        m.add(0, 18, 17, R.string.zc_menu)
        val zips = entries.filter { it is FsEntry.FileEntry && it.name.endsWith(".zip", true) }
        if (zips.size == 1 && entries.size > 1) m.add(0, 19, 18, R.string.zadd_menu)
        // تنزيل إلى الجهاز (Downloads/RinStudio): كما هو لملف واحد، أو ZIP لغير ذلك.
        m.add(0, 20, 19, R.string.dl_action)
        if (entries.size > 1 || entries.firstOrNull() is FsEntry.FileEntry) m.add(0, 21, 20, R.string.dl_action_zip)
        pm.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> { FileClipboard.set(entries, cut = false); toast(ctx, entries.size); onChanged() }
                2 -> { FileClipboard.set(entries, cut = true); toast(ctx, entries.size); onChanged() }
                3 -> FileBatchOps.duplicate(ctx, entries, onChanged)
                4 -> FileBatchOps.showMoveDialog(ctx, project, entries, protectedPaths, onChanged)
                5 -> BatchRename.show(ctx, project, entries, protectedPaths, onChanged)
                6 -> FolderCreator.show(ctx, project, parentRelDir, entries, protectedPaths, onChanged)
                7 -> FileBatchOps.copyPaths(ctx, entries)
                8 -> onInvert?.invoke()
                9 -> FileProps.show(ctx, entries)
                10 -> { FilePins.toggle(ctx, project.name, entries); onChanged() }
                11 -> FileArchive.compress(ctx, project, entries, parentRelDir, onChanged)
                12 -> FileArchive.extractHere(ctx, entries, onChanged)
                13 -> onSelectPattern?.invoke()
                14 -> FileShare.share(ctx, entries)
                15 -> (entries[0] as? FsEntry.FileEntry)?.let { FilePreview.show(ctx, it.file.file) }
                17 -> ZipTools.shareAsZip(ctx, entries)
                20 -> DownloadExporter.saveEntries(ctx, project, entries)
                21 -> DownloadExporter.saveEntries(ctx, project, entries, forceZip = true)
                18 -> ZipCompress.show(ctx, project, entries, parentRelDir, onChanged)
                19 -> {
                    val z = entries.first { it is FsEntry.FileEntry && it.name.endsWith(".zip", true) } as FsEntry.FileEntry
                    ZipAdd.add(ctx, z.file.file, entries.filter { it !== z }, onChanged)
                }
                16 -> {
                    val a = (entries[0] as FsEntry.FileEntry).file.file
                    val b = (entries[1] as FsEntry.FileEntry).file.file
                    FileDiff.show(ctx, a, b)
                }
                else -> return@setOnMenuItemClickListener false
            }
            true
        }
        pm.show()
    }

    private fun toast(ctx: Context, n: Int) =
        Toast.makeText(ctx, ctx.getString(R.string.clip_ready_toast, n), Toast.LENGTH_SHORT).show()
}

// ---------------------------------------------------------------------------------------------
// التثبيت (المفضّلة): الملفات/المجلدات المثبّتة تظهر أولاً وعليها نجمة
// ---------------------------------------------------------------------------------------------

object FilePins {
    private const val PREFS = "rin_file_pins"

    fun get(ctx: Context, projectName: String): Set<String> =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getStringSet(projectName, emptySet()) ?: emptySet()

    /** إن كانت كل العناصر مثبّتة فيُلغى تثبيتها، وإلا تُثبَّت كلها. يعيد true إن صار التثبيت فعّالاً. */
    fun toggle(ctx: Context, projectName: String, entries: List<FsEntry>): Boolean {
        val cur = get(ctx, projectName).toMutableSet()
        val keys = entries.map { it.relPath }
        val pin = !keys.all { it in cur }
        if (pin) cur.addAll(keys) else cur.removeAll(keys.toSet())
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putStringSet(projectName, cur).apply()
        return pin
    }
}

// ---------------------------------------------------------------------------------------------
// سلة مؤقتة: حذف قابل للتراجع فوراً
// ---------------------------------------------------------------------------------------------

object FileTrash {
    class Batch(val moves: List<Pair<File, File>>) // (الأصل, مكانه في السلة)

    private fun move(src: File, dst: File): Boolean {
        dst.parentFile?.mkdirs()
        if (src.renameTo(dst)) return true
        val copied = try {
            if (src.isDirectory) src.copyRecursively(dst, overwrite = false) else { src.copyTo(dst); true }
        } catch (_: Throwable) {
            false
        }
        if (copied) src.deleteRecursively()
        return copied
    }

    /** ينقل [sources] إلى سلة داخل ذاكرة التخزين المؤقت؛ يعيد null إن لم يُنقل شيء. */
    fun trash(ctx: Context, projectName: String, sources: List<File>): Batch? {
        val root = File(ctx.cacheDir, "rin_trash/$projectName/${System.currentTimeMillis()}")
        val moves = mutableListOf<Pair<File, File>>()
        sources.forEachIndexed { i, f ->
            val dst = File(root, "$i/${f.name}")
            if (move(f, dst)) moves += f to dst
        }
        return if (moves.isEmpty()) null else Batch(moves)
    }

    /** يعيد العناصر لأماكنها؛ يعيد عدد ما استُعيد (المتعارض في الاسم يُستعاد باسم فريد). */
    fun restore(batch: Batch): Int {
        var n = 0
        for ((orig, trashed) in batch.moves) {
            if (!trashed.exists()) continue
            var target = orig
            if (target.exists()) {
                val dot = orig.name.lastIndexOf('.')
                val base = if (orig.isFile && dot > 0) orig.name.substring(0, dot) else orig.name
                val ext = if (orig.isFile && dot > 0) orig.name.substring(dot) else ""
                var k = 2
                do { target = File(orig.parentFile, "$base ($k)$ext"); k++ } while (target.exists())
            }
            if (move(trashed, target)) n++
        }
        batch.moves.firstOrNull()?.second?.parentFile?.parentFile?.deleteRecursively()
        return n
    }
}

// ---------------------------------------------------------------------------------------------
// ضغط / فك ضغط zip
// ---------------------------------------------------------------------------------------------

object FileArchive {

    fun compress(ctx: Context, project: Project, entries: List<FsEntry>, destRelDir: String, onDone: () -> Unit) {
        val list = FileBatchOps.normalize(entries)
        if (list.isEmpty()) return
        val input = EditText(ctx).apply {
            hint = ctx.getString(R.string.zip_name_hint)
            setText(if (list.size == 1) list[0].name.substringBeforeLast('.') else "archive")
        }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.zip_compress)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val base = input.text.toString().trim().removeSuffix(".zip")
                if (!ProjectManager.isValidPathSegment(base)) return@setPositiveButton
                try {
                    val dir = ProjectManager.resolveDir(project, destRelDir)
                    var out = File(dir, "$base.zip")
                    var k = 2
                    while (out.exists()) { out = File(dir, "$base ($k).zip"); k++ }
                    val n = write(project, list, out)
                    Toast.makeText(ctx, ctx.getString(R.string.selection_done_toast, n), Toast.LENGTH_SHORT).show()
                } catch (t: Throwable) {
                    Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
                }
                onDone()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun write(project: Project, list: List<FsEntry>, out: File): Int {
        val files = list.map { e -> when (e) { is FsEntry.FileEntry -> e.file.file; is FsEntry.FolderEntry -> e.folder.dir } }
        // مرجع المسارات داخل الأرشيف: المجلد المشترك إن اتفقت كل العناصر عليه، وإلا جذر المشروع.
        val parents = files.map { it.parentFile?.canonicalPath }.toSet()
        val base = if (parents.size == 1 && parents.first() != null) File(parents.first()!!) else project.dir
        var count = 0
        java.util.zip.ZipOutputStream(out.outputStream().buffered()).use { zip ->
            fun add(f: File) {
                if (f.canonicalPath == out.canonicalPath) return
                val name = f.canonicalFile.relativeTo(base.canonicalFile).path.replace(File.separatorChar, '/')
                if (f.isDirectory) {
                    zip.putNextEntry(java.util.zip.ZipEntry("$name/")); zip.closeEntry()
                    f.listFiles()?.sortedBy { it.name }?.forEach { add(it) }
                } else {
                    zip.putNextEntry(java.util.zip.ZipEntry(name))
                    f.inputStream().use { it.copyTo(zip) }
                    zip.closeEntry(); count++
                }
            }
            files.forEach { add(it) }
        }
        return count
    }

    /** يفك كل ملفات .zip في [entries] إلى مجلد بجوارها باسم الأرشيف (مع حماية من مسارات الخروج). */
    fun extractHere(ctx: Context, entries: List<FsEntry>, onDone: () -> Unit) {
        var files = 0; var failed = 0
        for (e in entries.filterIsInstance<FsEntry.FileEntry>().filter { it.name.endsWith(".zip", true) }) {
            try {
                val zipFile = e.file.file
                var dest = File(zipFile.parentFile, zipFile.name.substringBeforeLast('.'))
                var k = 2
                while (dest.exists()) { dest = File(zipFile.parentFile, "${zipFile.name.substringBeforeLast('.')} ($k)"); k++ }
                dest.mkdirs()
                val destPath = dest.canonicalPath + File.separator
                java.util.zip.ZipInputStream(zipFile.inputStream().buffered()).use { zin ->
                    var ze = zin.nextEntry
                    while (ze != null) {
                        val target = File(dest, ze.name)
                        require(target.canonicalPath.startsWith(destPath)) { "bad zip entry" }
                        if (ze.isDirectory) target.mkdirs()
                        else { target.parentFile?.mkdirs(); target.outputStream().use { zin.copyTo(it) }; files++ }
                        ze = zin.nextEntry
                    }
                }
            } catch (_: Throwable) { failed++ }
        }
        val parts = mutableListOf<String>()
        if (files > 0) parts += ctx.getString(R.string.zip_extracted_toast, files)
        if (failed > 0) parts += ctx.getString(R.string.selection_failed_toast, failed)
        if (parts.isNotEmpty()) Toast.makeText(ctx, parts.joinToString("\n"), Toast.LENGTH_LONG).show()
        onDone()
    }
}

// ---------------------------------------------------------------------------------------------
// تحديد بنمط (مثل *.rin أو test_??)
// ---------------------------------------------------------------------------------------------

object PatternSelect {
    fun ask(ctx: Context, onPattern: (Regex) -> Unit) {
        val input = EditText(ctx).apply { hint = ctx.getString(R.string.pattern_hint) }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.pattern_title)
            .setView(input)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val g = input.text.toString().trim()
                if (g.isEmpty()) return@setPositiveButton
                val rx = buildString {
                    g.forEach { c ->
                        when (c) {
                            '*' -> append(".*")
                            '?' -> append('.')
                            else -> append(Regex.escape(c.toString()))
                        }
                    }
                }
                onPattern(Regex(rx, RegexOption.IGNORE_CASE))
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

// ---------------------------------------------------------------------------------------------
// تصفية القائمة حسب النوع
// ---------------------------------------------------------------------------------------------

object FileFilter {
    enum class Kind { ALL, CODE, IMAGES, MEDIA, ARCHIVES }

    private const val PREFS = "rin_file_filter"
    private val CODE = setOf("rin", "indsin", "json", "md", "txt", "html", "css", "js", "xml", "yaml", "yml",
        "kt", "java", "cpp", "h", "c", "py", "toml", "csv", "log", "sh", "zpp")
    private val IMAGES = setOf("png", "jpg", "jpeg", "gif", "webp", "bmp", "svg", "ico")
    private val MEDIA = setOf("mp4", "mkv", "webm", "mov", "avi", "mp3", "wav", "ogg", "m4a", "flac")
    private val ARCHIVES = setOf("zip", "rinpak", "rcl", "rinproj", "7z", "tar", "gz")

    fun kind(ctx: Context): Kind =
        runCatching { Kind.valueOf(ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("kind", "ALL")!!) }
            .getOrDefault(Kind.ALL)

    fun matches(kind: Kind, name: String): Boolean {
        if (kind == Kind.ALL) return true
        val ext = name.substringAfterLast('.', "").lowercase()
        return when (kind) {
            Kind.CODE -> ext in CODE
            Kind.IMAGES -> ext in IMAGES
            Kind.MEDIA -> ext in MEDIA
            Kind.ARCHIVES -> ext in ARCHIVES
            Kind.ALL -> true
        }
    }

    fun showHidden(ctx: Context): Boolean =
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("hidden", true)

    fun setShowHidden(ctx: Context, v: Boolean) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean("hidden", v).apply()
    }

    fun applyFolders(ctx: Context, folders: List<RinFolder>): List<RinFolder> =
        if (showHidden(ctx)) folders else folders.filter { !it.name.startsWith(".") }

    fun apply(ctx: Context, files: List<RinFile>): List<RinFile> {
        val k = kind(ctx)
        val visible = if (showHidden(ctx)) files else files.filter { !it.name.startsWith(".") }
        return if (k == Kind.ALL) visible else visible.filter { matches(k, it.name) }
    }

    fun showDialog(ctx: Context, onChanged: () -> Unit) {
        val kinds = Kind.values()
        val labels = arrayOf(
            ctx.getString(R.string.filter_all), ctx.getString(R.string.filter_code), ctx.getString(R.string.filter_images),
            ctx.getString(R.string.filter_media), ctx.getString(R.string.filter_archives)
        )
        AlertDialog.Builder(ctx)
            .setTitle(R.string.filter_title)
            .setSingleChoiceItems(labels, kinds.indexOf(kind(ctx))) { d, which ->
                ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("kind", kinds[which].name).apply()
                d.dismiss()
                onChanged()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

// ---------------------------------------------------------------------------------------------
// مشاركة الملفات المحددة
// ---------------------------------------------------------------------------------------------

object FileShare {
    fun share(ctx: Context, entries: List<FsEntry>) {
        val files = FileBatchOps.normalize(entries).flatMap { e ->
            when (e) {
                is FsEntry.FileEntry -> listOf(e.file.file)
                is FsEntry.FolderEntry -> e.folder.dir.walkTopDown().filter { it.isFile }.toList()
            }
        }.take(50)
        if (files.isEmpty()) return
        try {
            val uris = ArrayList(files.map {
                androidx.core.content.FileProvider.getUriForFile(ctx, "${ctx.packageName}.fileprovider", it)
            })
            val intent = android.content.Intent(
                if (uris.size == 1) android.content.Intent.ACTION_SEND else android.content.Intent.ACTION_SEND_MULTIPLE
            ).apply {
                type = "*/*"
                if (uris.size == 1) putExtra(android.content.Intent.EXTRA_STREAM, uris[0])
                else putParcelableArrayListExtra(android.content.Intent.EXTRA_STREAM, uris)
                addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            ctx.startActivity(android.content.Intent.createChooser(intent, ctx.getString(R.string.tool_share)))
        } catch (t: Throwable) {
            Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
        }
    }
}

// ---------------------------------------------------------------------------------------------
// وضع العرض المضغوط (يُبدَّل بحركة القرص على القائمة)
// ---------------------------------------------------------------------------------------------

object FileDensity {
    private const val PREFS = "rin_file_view"
    fun compact(ctx: Context): Boolean = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("compact", false)
    fun save(ctx: Context, v: Boolean) { ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean("compact", v).apply() }
}

// ---------------------------------------------------------------------------------------------
// بحث داخل محتوى الملفات
// ---------------------------------------------------------------------------------------------

object FileContentSearch {
    /** ملفات تطابق الاسم أو يحوي نصّها [query]؛ والخريطة: relPath ← (رقم أول سطر مطابق, مقتطف منه). */
    fun search(project: Project, query: String): Pair<List<RinFile>, Map<String, Pair<Int, String>>> {
        val q = query.trim()
        if (q.isEmpty()) return emptyList<RinFile>() to emptyMap()
        val out = mutableListOf<RinFile>()
        val hits = LinkedHashMap<String, Pair<Int, String>>()
        val root = project.dir.canonicalFile
        for (f in project.dir.walkTopDown().filter { it.isFile }) {
            val rel = f.canonicalFile.relativeTo(root).path.replace(File.separatorChar, '/')
            val rf = RinFile(f.name, f, f.length(), f.lastModified(), rel)
            if (f.name.contains(q, ignoreCase = true)) { out += rf; continue }
            if (ProjectManager.isBinaryFile(f.name) || f.length() > 1_000_000) continue
            try {
                f.useLines { lines ->
                    var n = 0
                    for (line in lines) {
                        n++
                        val i = line.indexOf(q, ignoreCase = true)
                        if (i >= 0) {
                            val from = (i - 20).coerceAtLeast(0)
                            hits[rel] = n to line.substring(from, (i + q.length + 40).coerceAtMost(line.length)).trim()
                            out += rf
                            break
                        }
                    }
                }
            } catch (_: Throwable) { /* ملف غير نصي: يُتجاهل */ }
        }
        return out to hits
    }
}

// ---------------------------------------------------------------------------------------------
// الملفات الأخيرة
// ---------------------------------------------------------------------------------------------

object FileRecents {
    private const val PREFS = "rin_file_recents"
    private const val MAX = 15

    fun record(ctx: Context, projectName: String, relPath: String) {
        val sp = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val cur = (sp.getString(projectName, "") ?: "").split('\n').filter { it.isNotEmpty() && it != relPath }
        sp.edit().putString(projectName, (listOf(relPath) + cur).take(MAX).joinToString("\n")).apply()
    }

    /** المسارات الأحدث أولاً، المحذوفة منها تُستبعد. */
    fun list(ctx: Context, project: Project): List<String> =
        (ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(project.name, "") ?: "")
            .split('\n').filter { it.isNotEmpty() && File(project.dir, it).isFile }
}

// ---------------------------------------------------------------------------------------------
// معاينة سريعة لملف نصي
// ---------------------------------------------------------------------------------------------

object FilePreview {
    fun show(ctx: Context, file: File) {
        val text = try {
            file.useLines { it.take(80).joinToString("\n") }.take(12000)
        } catch (t: Throwable) {
            t.message ?: ""
        }
        val d = ctx.resources.displayMetrics.density
        val tv = android.widget.TextView(ctx).apply {
            this.text = if (text.isEmpty()) "—" else text
            typeface = android.graphics.Typeface.MONOSPACE
            textSize = 12f
            setTextIsSelectable(true)
            setPadding((16 * d).toInt(), (8 * d).toInt(), (16 * d).toInt(), (8 * d).toInt())
        }
        AlertDialog.Builder(ctx)
            .setTitle(file.name)
            .setView(android.widget.ScrollView(ctx).apply { addView(tv) })
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }
}

// ---------------------------------------------------------------------------------------------
// تحليل المساحة
// ---------------------------------------------------------------------------------------------

object StorageAnalyzer {
    fun show(ctx: Context, project: Project) {
        val progress = AlertDialog.Builder(ctx).setTitle(R.string.tool_storage).setMessage(R.string.tool_working).show()
        Thread {
            val files = project.dir.walkTopDown().filter { it.isFile }.toList()
            val total = files.sumOf { it.length() }
            val byExt = files.groupBy { it.extension.lowercase().ifEmpty { "—" } }
                .mapValues { e -> e.value.sumOf { it.length() } }.entries.sortedByDescending { it.value }.take(6)
            val top = files.sortedByDescending { it.length() }.take(8)
            val root = project.dir.canonicalFile
            val msg = buildString {
                append(ctx.getString(R.string.storage_total, Formatter.formatFileSize(ctx, total), files.size)).append("\n\n")
                append(ctx.getString(R.string.storage_by_type)).append('\n')
                byExt.forEach { append("  .${it.key}  ·  ${Formatter.formatFileSize(ctx, it.value)}\n") }
                append('\n').append(ctx.getString(R.string.storage_largest)).append('\n')
                top.forEach {
                    append("  ${it.canonicalFile.relativeTo(root).path}  ·  ${Formatter.formatFileSize(ctx, it.length())}\n")
                }
            }
            android.os.Handler(android.os.Looper.getMainLooper()).post {
                try {
                    progress.dismiss()
                    AlertDialog.Builder(ctx).setTitle(R.string.tool_storage).setMessage(msg)
                        .setPositiveButton(android.R.string.ok, null).show()
                } catch (_: Throwable) { /* الشاشة أُغلقت أثناء الحساب */ }
            }
        }.start()
    }
}

// ---------------------------------------------------------------------------------------------
// كشف الملفات المكررة (نفس المحتوى فعلاً: الحجم ثم بصمة MD5)
// ---------------------------------------------------------------------------------------------

object DuplicateFinder {
    private fun md5(f: File): String {
        val md = java.security.MessageDigest.getInstance("MD5")
        f.inputStream().buffered().use { ins ->
            val buf = ByteArray(8192)
            while (true) { val n = ins.read(buf); if (n <= 0) break; md.update(buf, 0, n) }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    fun show(ctx: Context, project: Project, protectedPaths: Set<String>, onChanged: () -> Unit) {
        val progress = AlertDialog.Builder(ctx).setTitle(R.string.tool_dups).setMessage(R.string.tool_working).show()
        Thread {
            val root = project.dir.canonicalFile
            val groups = project.dir.walkTopDown().filter { it.isFile && it.length() in 1..20_000_000 }
                .groupBy { it.length() }.values.filter { it.size > 1 }
                .flatMap { sameSize -> sameSize.groupBy { try { md5(it) } catch (_: Throwable) { it.path } }.values }
                .filter { it.size > 1 }
                .map { g -> g.sortedWith(compareBy({ it.path.length }, { it.path })) }
            android.os.Handler(android.os.Looper.getMainLooper()).post {
                try {
                    progress.dismiss()
                    if (groups.isEmpty()) {
                        Toast.makeText(ctx, R.string.tool_dups_none, Toast.LENGTH_LONG).show()
                        return@post
                    }
                    val extras = groups.flatMap { it.drop(1) }
                        .filter { f -> f.canonicalFile.relativeTo(root).path.replace(File.separatorChar, '/') !in protectedPaths }
                    val saved = extras.sumOf { it.length() }
                    val msg = buildString {
                        append(ctx.getString(R.string.tool_dups_summary, groups.size, extras.size, Formatter.formatFileSize(ctx, saved))).append("\n\n")
                        groups.take(6).forEach { g ->
                            append(g.joinToString("\n") { "• " + it.canonicalFile.relativeTo(root).path }).append("\n\n")
                        }
                    }
                    AlertDialog.Builder(ctx).setTitle(R.string.tool_dups).setMessage(msg)
                        .setPositiveButton(R.string.tool_dups_trash) { _, _ ->
                            val batch = FileTrash.trash(ctx, project.name, extras)
                            onChanged()
                            val host = (ctx as? android.app.Activity)?.findViewById<View>(android.R.id.content)
                            if (batch != null && host != null) {
                                com.google.android.material.snackbar.Snackbar
                                    .make(host, ctx.getString(R.string.selection_done_toast, batch.moves.size),
                                        com.google.android.material.snackbar.Snackbar.LENGTH_LONG)
                                    .setAction(R.string.undo_action) { FileTrash.restore(batch); onChanged() }
                                    .show()
                            }
                        }
                        .setNegativeButton(R.string.cancel, null).show()
                } catch (_: Throwable) { }
            }
        }.start()
    }
}

// ---------------------------------------------------------------------------------------------
// نسخ شجرة المشروع كنص (للتوثيق/المشاركة)
// ---------------------------------------------------------------------------------------------

object TreeExport {
    fun copy(ctx: Context, project: Project) {
        val sb = StringBuilder(project.name).append("/\n")
        var lines = 0
        fun walk(dir: File, prefix: String) {
            val kids = (dir.listFiles() ?: return).sortedWith(compareBy({ !it.isDirectory }, { it.name.lowercase() }))
            kids.forEachIndexed { i, f ->
                if (lines++ > 800) return
                val last = i == kids.lastIndex
                sb.append(prefix).append(if (last) "└── " else "├── ").append(f.name).append(if (f.isDirectory) "/" else "").append('\n')
                if (f.isDirectory) walk(f, prefix + if (last) "    " else "│   ")
            }
        }
        walk(project.dir, "")
        val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        cm.setPrimaryClip(ClipData.newPlainText("tree", sb.toString()))
        Toast.makeText(ctx, R.string.tool_tree_copied, Toast.LENGTH_SHORT).show()
    }
}

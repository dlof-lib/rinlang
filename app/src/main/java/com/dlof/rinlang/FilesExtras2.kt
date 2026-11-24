package com.dlof.rinlang

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Toast
import java.io.File

/*
 * أدوات مشروع إضافية لشاشة "الملفات":
 *   • FolderJump       : انتقال سريع إلى أي مجلد في المشروع.
 *   • FindReplace      : بحث واستبدال نصي عبر ملفات المشروع النصية (مع معاينة العدد وتأكيد).
 *   • EmptyFiles       : العثور على الملفات الفارغة (0 بايت) وحذفها عبر السلة القابلة للتراجع.
 *   • TextTools        : نسخ محتوى ملف نصي، وإحصاءات (أسطر/كلمات/أحرف).
 */

private fun relOf(project: Project, f: File): String =
    f.relativeTo(project.dir).path.replace('\\', '/')

private fun textFiles(project: Project): Sequence<File> =
    project.dir.walkTopDown()
        .onEnter { !it.name.startsWith(".") }
        .filter { it.isFile && !it.name.startsWith(".") && !ProjectManager.isBinaryFile(it.name) && it.length() <= 2L * 1024 * 1024 }

object FolderJump {
    fun show(ctx: Context, project: Project, onPick: (String) -> Unit) {
        val dirs = mutableListOf("")
        fun walk(rel: String) {
            for (f in ProjectManager.listEntries(project, rel).first) { dirs += f.relPath; walk(f.relPath) }
        }
        walk("")
        val labels = dirs.map { if (it.isEmpty()) project.name else it }.toTypedArray()
        AlertDialog.Builder(ctx)
            .setTitle(R.string.tool_jump)
            .setItems(labels) { _, i -> onPick(dirs[i]) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

object FindReplace {
    fun show(ctx: Context, project: Project, protectedPaths: Set<String>, onDone: () -> Unit) {
        val d = ctx.resources.displayMetrics.density
        val find = EditText(ctx).apply { hint = ctx.getString(R.string.fr_find); inputType = InputType.TYPE_CLASS_TEXT }
        val with = EditText(ctx).apply { hint = ctx.getString(R.string.fr_with); inputType = InputType.TYPE_CLASS_TEXT }
        val cs = CheckBox(ctx).apply { text = ctx.getString(R.string.fr_case) }
        val box = LinearLayout(ctx).apply {
            orientation = LinearLayout.VERTICAL
            setPadding((20 * d).toInt(), (8 * d).toInt(), (20 * d).toInt(), 0)
            addView(find); addView(with); addView(cs)
        }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.tool_findreplace)
            .setView(box)
            .setPositiveButton(R.string.fr_preview) { _, _ ->
                val q = find.text.toString()
                if (q.isNotEmpty()) scan(ctx, project, protectedPaths, q, with.text.toString(), cs.isChecked, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun scan(ctx: Context, project: Project, prot: Set<String>, q: String, r: String, cs: Boolean, onDone: () -> Unit) {
        val ui = Handler(Looper.getMainLooper())
        Toast.makeText(ctx, R.string.tool_working, Toast.LENGTH_SHORT).show()
        Thread {
            val hits = mutableListOf<Pair<File, Int>>()
            var skipped = 0
            for (f in textFiles(project)) {
                try {
                    val n = count(f.readText(), q, cs)
                    if (n > 0) { if (relOf(project, f) in prot) skipped++ else hits += f to n }
                } catch (_: Throwable) {}
            }
            ui.post {
                if (hits.isEmpty()) {
                    Toast.makeText(ctx, R.string.fr_none, Toast.LENGTH_LONG).show()
                    return@post
                }
                val total = hits.sumOf { it.second }
                var msg = ctx.getString(R.string.fr_confirm, total, hits.size)
                if (skipped > 0) msg += "\n" + ctx.getString(R.string.selection_skipped_toast, skipped)
                AlertDialog.Builder(ctx)
                    .setTitle(R.string.tool_findreplace)
                    .setMessage(msg)
                    .setPositiveButton(R.string.fr_apply) { _, _ ->
                        Thread {
                            var ok = 0
                            for ((f, _) in hits) {
                                try { f.writeText(f.readText().replace(q, r, ignoreCase = !cs)); ok++ } catch (_: Throwable) {}
                            }
                            ui.post {
                                Toast.makeText(ctx, ctx.getString(R.string.selection_done_toast, ok), Toast.LENGTH_LONG).show()
                                onDone()
                            }
                        }.start()
                    }
                    .setNegativeButton(R.string.cancel, null)
                    .show()
            }
        }.start()
    }

    private fun count(text: String, q: String, cs: Boolean): Int {
        var n = 0
        var i = text.indexOf(q, 0, ignoreCase = !cs)
        while (i >= 0) { n++; i = text.indexOf(q, i + q.length, ignoreCase = !cs) }
        return n
    }
}

object EmptyFiles {
    fun show(ctx: Context, project: Project, protectedPaths: Set<String>, onDone: () -> Unit) {
        val empties = project.dir.walkTopDown()
            .onEnter { !it.name.startsWith(".") }
            .filter { it.isFile && it.length() == 0L }
            .toList()
        if (empties.isEmpty()) {
            Toast.makeText(ctx, R.string.empty_none, Toast.LENGTH_SHORT).show()
            return
        }
        val rels = empties.map { relOf(project, it) }
        AlertDialog.Builder(ctx)
            .setTitle(ctx.getString(R.string.empty_title, rels.size))
            .setItems(rels.toTypedArray(), null)
            .setPositiveButton(R.string.empty_delete) { _, _ ->
                val entries = empties.map { f ->
                    FsEntry.FileEntry(RinFile(f.name, f, f.length(), f.lastModified(), relOf(project, f)))
                }
                FileBatchOps.confirmDelete(ctx, entries, protectedPaths, project.name, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

object TextTools {
    fun copyContent(ctx: Context, file: File) {
        try {
            if (file.length() > 1024 * 1024) {
                Toast.makeText(ctx, R.string.compare_too_large, Toast.LENGTH_LONG).show()
                return
            }
            val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            cm.setPrimaryClip(ClipData.newPlainText(file.name, file.readText()))
            Toast.makeText(ctx, R.string.content_copied_toast, Toast.LENGTH_SHORT).show()
        } catch (t: Throwable) {
            Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
        }
    }

    fun stats(ctx: Context, file: File) {
        try {
            if (file.length() > 5L * 1024 * 1024) {
                Toast.makeText(ctx, R.string.compare_too_large, Toast.LENGTH_LONG).show()
                return
            }
            val t = file.readText()
            val lines = if (t.isEmpty()) 0 else t.count { it == '\n' } + if (t.endsWith("\n")) 0 else 1
            val words = t.split(Regex("\\s+")).count { it.isNotEmpty() }
            val blank = t.lines().count { it.isBlank() }
            AlertDialog.Builder(ctx)
                .setTitle(file.name)
                .setMessage(ctx.getString(R.string.stats_body, lines, blank, words, t.length, file.length()))
                .setPositiveButton(android.R.string.ok, null)
                .show()
        } catch (t: Throwable) {
            Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
        }
    }
}

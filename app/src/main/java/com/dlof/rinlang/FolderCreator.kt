package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Context
import android.text.InputType
import android.widget.EditText
import android.widget.Toast
import java.io.File

/**
 * أساليب إنشاء المجلدات (تُستدعى من FilesActivity ودرج المستكشف):
 *  1) مجلد واحد — مع دعم مسار متداخل: "ui/screens/home" ينشئ كل المستويات.
 *  2) عدة مجلدات دفعة واحدة — اسم في كل سطر أو مفصولة بفاصلة (ويصحّ كل اسم أن يكون مساراً متداخلاً).
 *  3) من قالب جاهز — اختيار متعدد من (src, lib, assets, docs, tests, config).
 *  4) تجميع العناصر المحددة في مجلد جديد — ينشئه وينقل المحدَّد إليه.
 */
object FolderCreator {

    private val TEMPLATES = listOf("src", "lib", "assets", "docs", "tests", "config")

    /** يفتح قائمة الأساليب. [selected] غير فارغة ⇒ يظهر خيار "تجميع المحدد". */
    fun show(
        ctx: Context, project: Project, parentRelDir: String,
        selected: List<FsEntry> = emptyList(), protectedPaths: Set<String> = emptySet(),
        onDone: () -> Unit
    ) {
        val labels = mutableListOf(
            ctx.getString(R.string.mkdir_single),
            ctx.getString(R.string.mkdir_multi),
            ctx.getString(R.string.mkdir_template)
        )
        val hasSel = selected.isNotEmpty()
        if (hasSel) labels += ctx.getString(R.string.mkdir_group_selected, selected.size)
        AlertDialog.Builder(ctx)
            .setTitle(R.string.new_folder_title)
            .setItems(labels.toTypedArray()) { _, which ->
                when (which) {
                    0 -> askNames(ctx, project, parentRelDir, multi = false, onDone = onDone)
                    1 -> askNames(ctx, project, parentRelDir, multi = true, onDone = onDone)
                    2 -> pickTemplates(ctx, project, parentRelDir, onDone)
                    3 -> groupSelected(ctx, project, parentRelDir, selected, protectedPaths, onDone)
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    /** ينشئ مساراً متداخلاً تحت [parentRelDir]؛ يعيد true إن أُنشئ مجلد جديد فعلاً. */
    fun ensurePath(project: Project, parentRelDir: String, path: String): Boolean {
        val segments = path.trim().trim('/').split('/').map { it.trim() }.filter { it.isNotEmpty() }
        require(segments.isNotEmpty()) { "empty" }
        for (seg in segments) {
            require(seg != "." && seg != ".." && ProjectManager.isValidPathSegment(seg)) {
                ctxMessage(seg)
            }
        }
        val target = File(ProjectManager.resolveDir(project, parentRelDir), segments.joinToString("/"))
        if (target.isDirectory) return false
        require(!target.exists()) { ctxMessage(segments.last()) }
        return target.mkdirs()
    }

    private fun ctxMessage(seg: String) = "اسم المجلد غير صالح أو موجود كملف: $seg"

    private fun askNames(ctx: Context, project: Project, parent: String, multi: Boolean, onDone: () -> Unit) {
        val input = EditText(ctx)
        input.hint = ctx.getString(if (multi) R.string.mkdir_multi_hint else R.string.mkdir_single_hint)
        if (multi) {
            input.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_MULTI_LINE
            input.minLines = 3
        }
        AlertDialog.Builder(ctx)
            .setTitle(if (multi) R.string.mkdir_multi else R.string.mkdir_single)
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val names = if (multi) input.text.split('\n', ',', '،').map { it.trim() }.filter { it.isNotEmpty() }
                else listOf(input.text.toString().trim()).filter { it.isNotEmpty() }
                createAll(ctx, project, parent, names, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun pickTemplates(ctx: Context, project: Project, parent: String, onDone: () -> Unit) {
        val checked = BooleanArray(TEMPLATES.size) { it < 2 }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.mkdir_template)
            .setMultiChoiceItems(TEMPLATES.toTypedArray(), checked) { _, i, on -> checked[i] = on }
            .setPositiveButton(R.string.create) { _, _ ->
                createAll(ctx, project, parent, TEMPLATES.filterIndexed { i, _ -> checked[i] }, onDone)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun createAll(ctx: Context, project: Project, parent: String, names: List<String>, onDone: () -> Unit) {
        if (names.isEmpty()) return
        var created = 0
        var failed: String? = null
        for (n in names) {
            try {
                if (ensurePath(project, parent, n)) created++
            } catch (t: Throwable) {
                failed = t.message
            }
        }
        if (created > 0) Toast.makeText(ctx, ctx.getString(R.string.mkdir_created_toast, created), Toast.LENGTH_SHORT).show()
        if (failed != null) Toast.makeText(ctx, failed, Toast.LENGTH_LONG).show()
        onDone()
    }

    private fun groupSelected(
        ctx: Context, project: Project, parent: String, selected: List<FsEntry>,
        protectedPaths: Set<String>, onDone: () -> Unit
    ) {
        val input = EditText(ctx)
        input.hint = ctx.getString(R.string.mkdir_single_hint)
        AlertDialog.Builder(ctx)
            .setTitle(ctx.getString(R.string.mkdir_group_selected, selected.size))
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                try {
                    ensurePath(project, parent, name)
                    val dest = File(ProjectManager.resolveDir(project, parent), name.trim('/'))
                    var moved = 0; var skipped = 0
                    for (e in FileBatchOps.normalize(selected)) {
                        val src = when (e) {
                            is FsEntry.FileEntry -> e.file.file
                            is FsEntry.FolderEntry -> e.folder.dir
                        }
                        val blocked = protectedPaths.any { p -> p == e.relPath || (e.isFolder && p.startsWith(e.relPath + "/")) }
                        val target = File(dest, src.name)
                        val intoSelf = dest.canonicalPath == src.canonicalPath || dest.canonicalPath.startsWith(src.canonicalPath + File.separator)
                        if (blocked || intoSelf || target.exists() || !src.renameTo(target)) skipped++ else moved++
                    }
                    Toast.makeText(ctx, ctx.getString(R.string.selection_done_toast, moved), Toast.LENGTH_SHORT).show()
                    if (skipped > 0) Toast.makeText(ctx, ctx.getString(R.string.selection_skipped_toast, skipped), Toast.LENGTH_LONG).show()
                } catch (t: Throwable) {
                    Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
                }
                onDone()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

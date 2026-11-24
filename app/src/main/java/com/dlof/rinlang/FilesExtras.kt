package com.dlof.rinlang

import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.os.Handler
import android.os.Looper
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.BackgroundColorSpan
import android.text.style.ForegroundColorSpan
import android.view.View
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import java.io.File

/*
 * ميزات إضافية لشاشة "الملفات":
 *   • StatusMode      : شرائح تصفية سريعة (الكل / فيها أخطاء / غير محفوظة / مثبّتة).
 *   • RowMenu         : قائمة ⋮ موحّدة لأي صف (ملف أو مجلد) — تثبيت، مشاركة، خصائص، ضغط، نقل...
 *   • FileDiff        : مقارنة ملفين نصيين سطراً بسطر (تحديد ملفين ← المزيد ← مقارنة).
 *   • ClipboardFile   : إنشاء ملف جديد من نص الحافظة.
 */

enum class StatusMode { ALL, ERRORS, UNSAVED, PINNED }

// ---------------------------------------------------------------------------------------------
// قائمة الصف
// ---------------------------------------------------------------------------------------------

object RowMenu {
    @Suppress("UNUSED_PARAMETER")
    fun show(
        ctx: Context, anchor: View, project: Project, entry: FsEntry, parentRelDir: String,
        protectedPaths: Set<String>, onChanged: () -> Unit
    ) {
        val list = listOf(entry)
        val file = (entry as? FsEntry.FileEntry)?.file?.file
        val isZip = file != null && entry.name.endsWith(".zip", ignoreCase = true)
        val isText = file != null && !ProjectManager.isBinaryFile(entry.name)
        val isMedia = file != null && MediaTools.isMedia(entry.name)
        val pinned = entry.relPath in FilePins.get(ctx, project.name)
        val items = mutableListOf<IconMenu.Item>()
        fun add(icon: Int, label: Int, action: () -> Unit) { items += IconMenu.Item(icon, ctx.getString(label), action) }

        add(R.drawable.fx_dups, R.string.action_duplicate) { FileBatchOps.duplicate(ctx, list, onChanged) }
        add(R.drawable.fx_jump, R.string.action_move) { FileBatchOps.showMoveDialog(ctx, project, list, protectedPaths, onChanged) }
        add(R.drawable.fx_path, R.string.action_copy_path) { FileBatchOps.copyPaths(ctx, list) }
        add(R.drawable.fx_clipboard, R.string.row_copy_name) {
            val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            cm.setPrimaryClip(ClipData.newPlainText("name", entry.name))
            Toast.makeText(ctx, R.string.name_copied_toast, Toast.LENGTH_SHORT).show()
        }
        add(R.drawable.fx_pin, if (pinned) R.string.row_unpin else R.string.row_pin) {
            FilePins.toggle(ctx, project.name, list); onChanged()
        }
        add(R.drawable.fx_share, R.string.tool_share) { FileShare.share(ctx, list) }
        add(R.drawable.fx_info, R.string.props_title) { FileProps.show(ctx, list) }
        add(R.drawable.fx_zip_add, R.string.zc_menu) { ZipCompress.show(ctx, project, list, parentRelDir, onChanged) }
        add(R.drawable.fx_zip, R.string.zip_compress) { FileArchive.compress(ctx, project, list, parentRelDir, onChanged) }
        add(R.drawable.fx_zip_share, R.string.zip_share) { ZipTools.shareAsZip(ctx, list) }
        if (isZip && file != null) {
            add(R.drawable.fx_zip_list, R.string.row_zip_browse) {
                ZipTools.browseFile(ctx, project, parentRelDir, file, protectedPaths, false, onChanged)
            }
            add(R.drawable.fx_extract_folder, R.string.row_zip_advanced) {
                ZipImport.fromFile(ctx, project, parentRelDir, file, protectedPaths, onChanged)
            }
            add(R.drawable.fx_extract_folder, R.string.zip_extract_here) { FileArchive.extractHere(ctx, list, onChanged) }
            add(R.drawable.fx_extract_folder, R.string.row_zip_named) {
                ZipTools.extractToNamedFolder(ctx, project, parentRelDir, file, protectedPaths, onChanged)
            }
            add(R.drawable.fx_zip_test, R.string.row_zip_test) { ZipTools.test(ctx, file) }
        }
        if (isMedia && file != null) {
            add(R.drawable.fx_info, R.string.row_media_info) { MediaTools.info(ctx, file) }
            if (MediaTools.canShrink(entry.name)) {
                add(R.drawable.fx_resize, R.string.row_shrink) { MediaTools.shrinkCopy(ctx, file, onChanged) }
            }
        }
        if (isText && file != null) {
            add(R.drawable.fx_eye, R.string.tool_preview) { FilePreview.show(ctx, file) }
            add(R.drawable.fx_clipboard, R.string.row_copy_content) { TextTools.copyContent(ctx, file) }
            add(R.drawable.fx_storage, R.string.row_stats) { TextTools.stats(ctx, file) }
        }
        IconMenu.show(ctx, entry.name, items)
    }
}

// ---------------------------------------------------------------------------------------------
// إنشاء ملف من الحافظة
// ---------------------------------------------------------------------------------------------

object ClipboardFile {
    fun show(ctx: Context, project: Project, relDir: String, onDone: () -> Unit) {
        val cm = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val text = cm.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(ctx)?.toString().orEmpty()
        if (text.isEmpty()) {
            Toast.makeText(ctx, R.string.clipboard_empty, Toast.LENGTH_SHORT).show()
            return
        }
        val input = EditText(ctx).apply {
            hint = ctx.getString(R.string.file_name_hint)
            setText("clipboard.txt")
            setSelection(0, "clipboard".length)
        }
        AlertDialog.Builder(ctx)
            .setTitle(R.string.mkfile_clipboard)
            .setMessage(ctx.getString(R.string.clipboard_chars_format, text.length))
            .setView(input)
            .setPositiveButton(R.string.create) { _, _ ->
                val name = input.text.toString().trim()
                if (name.isEmpty()) return@setPositiveButton
                try {
                    // يمنع الكتابة فوق ملف موجود: يضيف رقماً (clipboard (2).txt ...).
                    val dir = ProjectManager.resolveDir(project, relDir)
                    var finalName = name
                    var n = 2
                    while (File(dir, finalName).exists()) {
                        val dot = name.lastIndexOf('.')
                        finalName = if (dot > 0) "${name.substring(0, dot)} ($n)${name.substring(dot)}" else "$name ($n)"
                        n++
                    }
                    ProjectManager.writeFile(project, relDir, finalName, text)
                    Toast.makeText(ctx, ctx.getString(R.string.file_uploaded_toast, finalName), Toast.LENGTH_SHORT).show()
                    onDone()
                } catch (t: Throwable) {
                    Toast.makeText(ctx, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

// ---------------------------------------------------------------------------------------------
// مقارنة ملفين (diff)
// ---------------------------------------------------------------------------------------------

object FileDiff {
    private const val MAX_BYTES = 512 * 1024L
    private const val MAX_CELLS = 4_000_000L
    private const val CONTEXT = 2

    private class Op(val kind: Char, val text: String) // ' ' مشترك، '+' مضاف، '-' محذوف

    fun show(ctx: Context, a: File, b: File) {
        if (a.length() > MAX_BYTES || b.length() > MAX_BYTES) {
            Toast.makeText(ctx, R.string.compare_too_large, Toast.LENGTH_LONG).show()
            return
        }
        val ui = Handler(Looper.getMainLooper())
        Thread {
            val result = try {
                diff(a.readLines(), b.readLines())
            } catch (_: Throwable) {
                null
            }
            ui.post {
                when {
                    result == null -> Toast.makeText(ctx, R.string.compare_too_large, Toast.LENGTH_LONG).show()
                    result.none { it.kind != ' ' } -> Toast.makeText(ctx, R.string.compare_identical, Toast.LENGTH_LONG).show()
                    else -> render(ctx, a.name, b.name, result)
                }
            }
        }.start()
    }

    /** LCS على الجزء الأوسط بعد قصّ البادئة واللاحقة المشتركتين. يعيد null إن كان الحجم أكبر من الحد. */
    private fun diff(x: List<String>, y: List<String>): List<Op>? {
        var start = 0
        while (start < x.size && start < y.size && x[start] == y[start]) start++
        var endX = x.size
        var endY = y.size
        while (endX > start && endY > start && x[endX - 1] == y[endY - 1]) { endX--; endY-- }

        val n = endX - start
        val m = endY - start
        if (n.toLong() * m > MAX_CELLS) return null

        val dp = Array(n + 1) { IntArray(m + 1) }
        for (i in n - 1 downTo 0) {
            for (j in m - 1 downTo 0) {
                dp[i][j] = if (x[start + i] == y[start + j]) dp[i + 1][j + 1] + 1
                else maxOf(dp[i + 1][j], dp[i][j + 1])
            }
        }

        val ops = ArrayList<Op>(x.size + y.size)
        for (k in 0 until start) ops += Op(' ', x[k])
        var i = 0
        var j = 0
        while (i < n && j < m) {
            when {
                x[start + i] == y[start + j] -> { ops += Op(' ', x[start + i]); i++; j++ }
                dp[i + 1][j] >= dp[i][j + 1] -> { ops += Op('-', x[start + i]); i++ }
                else -> { ops += Op('+', y[start + j]); j++ }
            }
        }
        while (i < n) { ops += Op('-', x[start + i]); i++ }
        while (j < m) { ops += Op('+', y[start + j]); j++ }
        for (k in endX until x.size) ops += Op(' ', x[k])
        return ops
    }

    private fun render(ctx: Context, nameA: String, nameB: String, ops: List<Op>) {
        val added = ops.count { it.kind == '+' }
        val removed = ops.count { it.kind == '-' }

        // نُبقي الأسطر المتغيّرة وما حولها بسطرين فقط، ونكثّف الباقي بـ "…".
        val keep = BooleanArray(ops.size)
        for ((idx, op) in ops.withIndex()) {
            if (op.kind != ' ') {
                for (k in maxOf(0, idx - CONTEXT)..minOf(ops.lastIndex, idx + CONTEXT)) keep[k] = true
            }
        }

        val green = Color.parseColor("#3328A745")
        val red = Color.parseColor("#33E5484D")
        val dim = Color.parseColor("#888888")
        val sb = SpannableStringBuilder()
        var skipping = false
        for ((idx, op) in ops.withIndex()) {
            if (!keep[idx]) {
                if (!skipping) {
                    val s = sb.length
                    sb.append("  …\n")
                    sb.setSpan(ForegroundColorSpan(dim), s, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    skipping = true
                }
                continue
            }
            skipping = false
            val s = sb.length
            sb.append(op.kind).append(' ').append(op.text).append('\n')
            when (op.kind) {
                '+' -> sb.setSpan(BackgroundColorSpan(green), s, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                '-' -> sb.setSpan(BackgroundColorSpan(red), s, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
        }

        val d = ctx.resources.displayMetrics.density
        val tv = TextView(ctx).apply {
            text = sb
            typeface = Typeface.MONOSPACE
            textSize = 11.5f
            setTextIsSelectable(true)
            // الكود يُكتب من اليسار لليمين حتى في الواجهة العربية.
            textDirection = View.TEXT_DIRECTION_LTR
            setPadding((12 * d).toInt(), (8 * d).toInt(), (12 * d).toInt(), (8 * d).toInt())
        }
        AlertDialog.Builder(ctx)
            .setTitle(ctx.getString(R.string.compare_title, nameA, nameB))
            .setMessage(ctx.getString(R.string.compare_summary, added, removed))
            .setView(ScrollView(ctx).apply { addView(tv) })
            .setPositiveButton(android.R.string.ok, null)
            .show()
    }
}

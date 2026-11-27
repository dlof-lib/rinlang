package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import java.io.File

/*
 * قالب نصّي لإنشاء ملفات ومجلدات دفعة واحدة. كل سطر = عنصر أو أكثر:
 *   main                 ملف main.rin (يُضاف .rin إن لم يوجد امتداد)
 *   index, ti, main      عدّة ملفات في سطر (الفصل بـ , أو ، أو ;)
 *   main.{rin,ti}        توسيع الأقواس: main.rin و main.ti
 *   src/                 مجلد؛ والأسطر المُزاحة بعده (مسافتان أو Tab) تدخل داخله
 *   a/b/c.rin            مسار متداخل (تُنشأ المجلدات تلقائياً)
 *   LICENSE.             النقطة في آخر الاسم = ملف بلا امتداد
 *   main :: print("hi")  ما بعد :: محتوى الملف (\n لسطر جديد)
 *   # تعليق              يُتجاهل
 * الموجود مسبقاً لا يُستبدل أبداً بل يُتخطّى.
 */
object BatchTemplate {
    class Op(val path: String, val isDir: Boolean, val content: String, val noExt: Boolean) {
        /** المسار النهائي (مع .rin الافتراضي للملفات). */
        val finalPath: String get() {
            if (isDir) return path
            val last = path.substringAfterLast('/')
            val dot = last.lastIndexOf('.')
            val hasExt = dot > 0 && dot < last.length - 1
            return if (hasExt || noExt) path else "$path.rin"
        }
    }

    private fun splitTop(s: String): List<String> {
        val out = mutableListOf<String>(); var depth = 0; val cur = StringBuilder()
        for (ch in s) {
            when {
                ch == '{' -> { depth++; cur.append(ch) }
                ch == '}' -> { depth = maxOf(0, depth - 1); cur.append(ch) }
                depth == 0 && (ch == ',' || ch == '\u060C' || ch == ';') -> { out += cur.toString(); cur.clear() }
                else -> cur.append(ch)
            }
        }
        out += cur.toString()
        return out.map { it.trim() }.filter { it.isNotEmpty() }
    }

    private fun expandBraces(s: String): List<String> {
        val open = s.indexOf('{'); if (open < 0) return listOf(s)
        var depth = 0; var close = -1
        for (i in open until s.length) {
            if (s[i] == '{') depth++ else if (s[i] == '}') { depth--; if (depth == 0) { close = i; break } }
        }
        if (close < 0) return listOf(s)
        val pre = s.substring(0, open); val post = s.substring(close + 1)
        return splitTop(s.substring(open + 1, close)).flatMap { alt ->
            expandBraces(alt).flatMap { a -> expandBraces(post).map { p -> pre + a + p } }
        }
    }

    fun parse(text: String): List<Op> {
        val ops = mutableListOf<Op>()
        val stack = ArrayList<Pair<Int, String>>() // (indent, dirPath)
        for (raw in text.lines()) {
            if (raw.isBlank() || raw.trim().startsWith("#")) continue
            var indent = 0
            for (ch in raw) { if (ch == ' ') indent++ else if (ch == '\t') indent += 4 else break }
            var body = raw.trim(); var content = ""
            val ci = body.indexOf("::")
            if (ci >= 0) { content = body.substring(ci + 2).trim().replace("\\n", "\n"); body = body.substring(0, ci).trim() }
            while (stack.isNotEmpty() && stack.last().first >= indent) stack.removeAt(stack.lastIndex)
            val parent = stack.lastOrNull()?.second ?: ""
            var lastDir: String? = null
            for (part in splitTop(body)) for (e in expandBraces(part)) {
                val t = e.trim(); if (t.isEmpty()) continue
                val isDir = t.endsWith("/")
                var name = t.trim('/')
                if (name.isEmpty()) continue
                var noExt = false
                if (!isDir && name.endsWith(".") && name.length > 1) { name = name.dropLast(1); noExt = true }
                val full = if (parent.isEmpty()) name else "$parent/$name"
                ops += Op(full, isDir, if (isDir) "" else content, noExt)
                if (isDir) lastDir = full
            }
            if (lastDir != null) stack += indent to lastDir
        }
        return ops
    }

    /**
     * يولّد قالباً نصّياً من شجرة مجلد قائم (مجلدات أولاً، إزاحة مسافتين لكل مستوى، "name." للملف بلا امتداد).
     * يتخطى الأسماء التي تتعارض مع صيغة القالب (فواصل , ; { } :: أو تبدأ بـ #). حدّ أقصى للعمق والعدد.
     */
    fun fromDir(root: File, maxDepth: Int = 6, maxEntries: Int = 500): String {
        val sb = StringBuilder()
        var count = 0
        val unsafe = Regex("[,;{}\\u060C]|::|^#|^\\s|\\s$")
        fun walk(d: File, depth: Int) {
            val kids = d.listFiles()?.sortedWith(compareBy({ !it.isDirectory }, { it.name.lowercase() })) ?: return
            for (k in kids) {
                if (count >= maxEntries) return
                if (unsafe.containsMatchIn(k.name)) continue
                count++
                val indent = "  ".repeat(depth)
                if (k.isDirectory) {
                    sb.append(indent).append(k.name).append("/\n")
                    if (depth < maxDepth) walk(k, depth + 1)
                } else {
                    val hasExt = k.name.lastIndexOf('.').let { it > 0 && it < k.name.length - 1 }
                    sb.append(indent).append(k.name).append(if (hasExt) "" else ".").append("\n")
                }
            }
        }
        walk(root, 0)
        return sb.toString().trimEnd()
    }

    private fun validPath(p: String): Boolean {
        val segs = p.split('/')
        return segs.isNotEmpty() && segs.all { it.isNotBlank() && it != "." && it != ".." && ProjectManager.isValidPathSegment(it) }
    }

    class Result(val files: Int, val dirs: Int, val skipped: Int, val invalid: Int, val failed: List<Pair<String, Throwable>> = emptyList())

    /** يطبّق العمليات تحت [baseDir]؛ [dryRun] يحسب النتيجة بلا كتابة. */
    fun apply(baseDir: File, ops: List<Op>, dryRun: Boolean = false): Result {
        var files = 0; var dirs = 0; var skipped = 0; var invalid = 0
        val failed = ArrayList<Pair<String, Throwable>>()
        val seen = HashSet<String>()
        for (op in ops) {
            val rel = op.finalPath
            if (!validPath(rel)) { invalid++; continue }
            if (!seen.add(rel)) { skipped++; continue }
            val target = File(baseDir, rel)
            if (!target.canonicalPath.startsWith(baseDir.canonicalPath)) { invalid++; continue }
            try {
                if (op.isDir) {
                    if (target.exists()) { skipped++; continue }
                    if (!dryRun && !target.mkdirs()) throw java.io.IOException("mkdirs failed")
                    dirs++
                } else {
                    if (target.exists()) { skipped++; continue }
                    if (!dryRun) { target.parentFile?.mkdirs(); target.writeText(op.content) }
                    files++
                }
            } catch (t: Throwable) {
                failed.add(rel to t)
            }
        }
        return Result(files, dirs, skipped, invalid, failed)
    }
}

object BatchCreateDialog {
    private const val STARTER = "index, ti, main"
    private const val TREE = "src/\n  main\n  index\nlib/\n  ti\nREADME.md"

    fun show(activity: Activity, project: Project, relDir: String, onDone: () -> Unit) {
        val d = activity.resources.displayMetrics.density
        fun dp(v: Int) = (v * d).toInt()
        val c = { id: Int -> ContextCompat.getColor(activity, id) }
        val baseDir = if (relDir.isBlank()) project.dir else File(project.dir, relDir.trim('/'))

        val root = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(20), dp(8), dp(20), 0) }
        val input = EditText(activity).apply {
            hint = activity.getString(R.string.bt_hint)
            typeface = Typeface.MONOSPACE
            textSize = 14f
            minLines = 5; maxLines = 9
            gravity = Gravity.TOP or Gravity.START
            textDirection = View.TEXT_DIRECTION_LTR
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE or
                android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            background = GradientDrawable().apply {
                cornerRadius = dp(14).toFloat(); setColor(c(R.color.rin_files_tile)); setStroke(dp(1), c(R.color.rin_files_border))
            }
            setPadding(dp(12), dp(10), dp(12), dp(10))
        }
        root.addView(input)

        fun chip(label: String, onClick: () -> Unit) = TextView(activity).apply {
            text = label; textSize = 13.5f; typeface = Typeface.MONOSPACE; gravity = Gravity.CENTER
            setTextColor(c(R.color.rin_files_chip_text))
            setPadding(dp(12), dp(7), dp(12), dp(7))
            background = GradientDrawable().apply {
                cornerRadius = dp(16).toFloat(); setColor(c(R.color.rin_files_chip_bg)); setStroke(dp(1), c(R.color.rin_files_border))
            }
            layoutParams = LinearLayout.LayoutParams(-2, -2).apply { setMargins(0, 0, dp(8), 0) }
            setOnClickListener { onClick() }
        }
        fun insert(s: String) {
            val st = input.selectionStart.coerceAtLeast(0); val en = input.selectionEnd.coerceAtLeast(0)
            input.text.replace(minOf(st, en), maxOf(st, en), s)
        }
        fun row(vararg chips: TextView) {
            val r = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
            chips.forEach { r.addView(it) }
            root.addView(HorizontalScrollView(activity).apply {
                isHorizontalScrollBarEnabled = false; addView(r)
                setPadding(0, dp(8), 0, 0)
            })
        }
        row(chip("index") { insert("index") }, chip("ti") { insert("ti") }, chip("main") { insert("main") },
            chip(".rin") { insert(".rin") }, chip(".") { insert(".") }, chip("/") { insert("/") },
            chip(",") { insert(", ") }, chip("{ }") { insert("{,}") })
        row(chip(activity.getString(R.string.bt_preset_starter)) { input.setText(STARTER); input.setSelection(input.text.length) },
            chip(activity.getString(R.string.bt_preset_tree)) { input.setText(TREE); input.setSelection(input.text.length) },
            chip("\u21B9 " + activity.getString(R.string.bt_indent)) { insert("  ") })

        // قوالب محفوظة: لصق من الحافظة، حفظ الحالي باسم، وشرائح للقوالب المحفوظة (نقرة تحمّل، ضغطة مطوّلة تحذف)
        val tplPrefs = activity.getSharedPreferences("rin_batch_templates", android.content.Context.MODE_PRIVATE)
        val savedRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        fun refreshSaved() {
            savedRow.removeAllViews()
            tplPrefs.all.keys.filter { it.startsWith("tpl:") }.sorted().forEach { key ->
                val name = key.removePrefix("tpl:")
                val ch = chip("\u2605 $name") {
                    input.setText(tplPrefs.getString(key, "") ?: ""); input.setSelection(input.text.length)
                }
                ch.setOnLongClickListener {
                    AlertDialog.Builder(activity)
                        .setMessage(activity.getString(R.string.bt_delete_q, name))
                        .setPositiveButton(R.string.bt_delete) { _, _ -> tplPrefs.edit().remove(key).apply(); refreshSaved() }
                        .setNegativeButton(R.string.cancel, null)
                        .show()
                    true
                }
                savedRow.addView(ch)
            }
        }
        row(chip("\u29C9 " + activity.getString(R.string.bt_paste)) {
                val cm = activity.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
                val txt = cm.primaryClip?.takeIf { it.itemCount > 0 }?.getItemAt(0)?.coerceToText(activity)?.toString().orEmpty()
                if (txt.isBlank()) Toast.makeText(activity, R.string.bt_clip_empty, Toast.LENGTH_SHORT).show()
                else { input.setText(txt); input.setSelection(input.text.length) }
            },
            chip("\u2605 " + activity.getString(R.string.bt_save)) {
                if (input.text.isBlank()) return@chip
                val nameInput = EditText(activity).apply { setSingleLine(); setText("${tplPrefs.all.keys.count { it.startsWith("tpl:") } + 1}") ; setPadding(dp(24), dp(16), dp(24), dp(12)) }
                val dialog = AlertDialog.Builder(activity)
                    .setTitle(R.string.bt_save_title)
                    .setView(nameInput)
                    .setPositiveButton(R.string.create) { _, _ ->
                fun run() {
                    try {
                        val res = BatchTemplate.apply(baseDir, BatchTemplate.parse(input.text.toString()))
                        if (res.failed.isEmpty() && res.invalid == 0) {
                            Toast.makeText(activity, activity.getString(R.string.bt_done, res.files, res.dirs, res.skipped), Toast.LENGTH_LONG).show()
                        } else {
                            val fl = res.failed.map { FileErrors.failure(activity, it.first, it.second) }.toMutableList()
                            if (res.invalid > 0) fl.add(FileFailure(activity.getString(R.string.err_invalid_names_format, res.invalid), activity.getString(R.string.err_invalid_name)))
                            FileErrors.showReport(activity, activity.getString(R.string.err_batch_title), res.files + res.dirs, fl,
                                onRetry = if (res.failed.isNotEmpty()) ({ run() }) else null)
                        }
                        onDone()
                    } catch (t: Throwable) {
                        FileErrors.showSingle(activity, activity.getString(R.string.err_batch_title), activity.getString(R.string.bt_title), t) { run() }
                    }
                }
                run()
            }
            .setNegativeButton(R.string.cancel, null)
            .create()
        dialog.setOnShowListener { refreshSaved() }
        dialog.show()
    }
}

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

    private fun validPath(p: String): Boolean {
        val segs = p.split('/')
        return segs.isNotEmpty() && segs.all { it.isNotBlank() && it != "." && it != ".." && ProjectManager.isValidPathSegment(it) }
    }

    class Result(val files: Int, val dirs: Int, val skipped: Int, val invalid: Int)

    /** يطبّق العمليات تحت [baseDir]؛ [dryRun] يحسب النتيجة بلا كتابة. */
    fun apply(baseDir: File, ops: List<Op>, dryRun: Boolean = false): Result {
        var files = 0; var dirs = 0; var skipped = 0; var invalid = 0
        val seen = HashSet<String>()
        for (op in ops) {
            val rel = op.finalPath
            if (!validPath(rel)) { invalid++; continue }
            if (!seen.add(rel)) { skipped++; continue }
            val target = File(baseDir, rel)
            if (!target.canonicalPath.startsWith(baseDir.canonicalPath)) { invalid++; continue }
            if (op.isDir) {
                if (target.exists()) { skipped++; continue }
                if (!dryRun) target.mkdirs(); dirs++
            } else {
                if (target.exists()) { skipped++; continue }
                if (!dryRun) { target.parentFile?.mkdirs(); target.writeText(op.content) }
                files++
            }
        }
        return Result(files, dirs, skipped, invalid)
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

        val help = TextView(activity).apply {
            text = activity.getString(R.string.bt_help); textSize = 11.5f
            setTextColor(c(R.color.rin_files_dim)); setPadding(0, dp(10), 0, dp(6))
        }
        root.addView(help)
        val preview = TextView(activity).apply {
            typeface = Typeface.MONOSPACE; textSize = 12.5f; textDirection = View.TEXT_DIRECTION_LTR
            setTextColor(c(R.color.rin_files_text))
        }
        root.addView(ScrollView(activity).apply {
            layoutParams = LinearLayout.LayoutParams(-1, dp(110)); addView(preview)
        })
        val summary = TextView(activity).apply { textSize = 12.5f; setPadding(0, dp(6), 0, 0) }
        root.addView(summary)

        var dialog: AlertDialog? = null
        fun refresh() {
            val ops = BatchTemplate.parse(input.text.toString())
            val res = BatchTemplate.apply(baseDir, ops, dryRun = true)
            preview.text = ops.take(60).joinToString("\n") { op ->
                val rel = op.finalPath
                val ex = File(baseDir, rel).exists()
                (if (op.isDir) "\u25B8 " else "\u2022 ") + rel + (if (op.isDir) "/" else "") + (if (ex) "  \u2014 " + activity.getString(R.string.nfd_skip) else "")
            }
            val total = res.files + res.dirs
            if (ops.isEmpty()) { summary.text = " "; summary.setTextColor(c(R.color.rin_files_dim)) }
            else {
                summary.text = activity.getString(R.string.bt_summary, res.files, res.dirs, res.skipped) +
                    (if (res.invalid > 0) " \u00B7 " + activity.getString(R.string.bt_invalid, res.invalid) else "")
                summary.setTextColor(c(if (res.invalid > 0) R.color.rin_files_danger else R.color.rin_files_accent))
            }
            dialog?.getButton(AlertDialog.BUTTON_POSITIVE)?.isEnabled = total > 0
        }
        input.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, cc: Int) {}
            override fun onTextChanged(s: CharSequence?, a: Int, b: Int, cc: Int) {}
            override fun afterTextChanged(s: Editable?) { refresh() }
        })

        dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.bt_title)
            .setView(root)
            .setPositiveButton(R.string.create) { _, _ ->
                try {
                    val res = BatchTemplate.apply(baseDir, BatchTemplate.parse(input.text.toString()))
                    Toast.makeText(activity, activity.getString(R.string.bt_done, res.files, res.dirs, res.skipped), Toast.LENGTH_LONG).show()
                    onDone()
                } catch (t: Throwable) {
                    Toast.makeText(activity, "${t.message}", Toast.LENGTH_LONG).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .create()
        dialog.setOnShowListener { refresh() }
        dialog.show()
    }
}

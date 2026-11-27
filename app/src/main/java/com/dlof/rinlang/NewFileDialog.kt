package com.dlof.rinlang

import android.app.Activity
import android.app.AlertDialog
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.view.inputmethod.InputMethodManager
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import java.io.File

/*
 * إنشاء ملف بطريقة جديدة: حقل اسم + شرائح سريعة بلمسة واحدة:
 *   أسماء: index · ti · main      امتدادات: .rin · .      فاصل مجلد: /
 * مع سطر معاينة حيّ للاسم النهائي يحذّر إن كان الملف موجوداً أو الاسم غير صالح،
 * ويُعطَّل زر «إنشاء» حتى يصحّ الاسم. يدعم المسارات المتداخلة (src/main.rin) كما كان.
 */
object NewFileDialog {
    private val NAMES = listOf("index", "ti", "main")

    private class Parts(val dir: String, val base: String, val ext: String) {
        val last: String get() = base + ext
        /** الاسم النهائي كما سيُكتب (يضيف .rin إن لم يوجد امتداد صالح). */
        val finalLast: String get() = if (ext.length > 1) last else "$last.rin"
    }

    private fun split(text: String): Parts {
        val t = text.trim()
        val last = t.substringAfterLast('/')
        val dir = t.substring(0, t.length - last.length)
        val dot = last.lastIndexOf('.')
        return if (dot > 0) Parts(dir, last.substring(0, dot), last.substring(dot))
        else if (dot == 0) Parts(dir, "", last)
        else Parts(dir, last, "")
    }

    fun show(activity: Activity, project: Project, relDir: String, onBatch: (() -> Unit)? = null, onCreated: (RinFile) -> Unit) {
        val d = activity.resources.displayMetrics.density
        fun dp(v: Int) = (v * d).toInt()
        val c = { id: Int -> ContextCompat.getColor(activity, id) }

        val root = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(8), dp(20), dp(0))
        }
        val input = EditText(activity).apply {
            hint = activity.getString(R.string.nfd_hint)
            setSingleLine()
            textDirection = View.TEXT_DIRECTION_LTR
            typeface = android.graphics.Typeface.MONOSPACE
        }
        root.addView(input)

        val preview = TextView(activity).apply {
            textSize = 12.5f
            typeface = android.graphics.Typeface.MONOSPACE
            textDirection = View.TEXT_DIRECTION_LTR
            setPadding(0, dp(8), 0, dp(4))
        }
        root.addView(preview)

        fun chipView(label: String, onClick: () -> Unit): TextView = TextView(activity).apply {
            text = label
            textSize = 14f
            typeface = android.graphics.Typeface.MONOSPACE
            gravity = Gravity.CENTER
            setTextColor(c(R.color.rin_files_chip_text))
            setPadding(dp(14), dp(8), dp(14), dp(8))
            background = GradientDrawable().apply {
                cornerRadius = dp(18).toFloat()
                setColor(c(R.color.rin_files_chip_bg))
                setStroke(dp(1), c(R.color.rin_files_border))
            }
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT
            ).apply { setMargins(0, 0, dp(8), 0) }
            setOnClickListener { onClick() }
        }
        fun section(title: String, chips: List<TextView>) {
            root.addView(TextView(activity).apply {
                text = title
                textSize = 12f
                setTextColor(c(R.color.rin_files_dim))
                setPadding(0, dp(10), 0, dp(6))
            })
            val row = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
            chips.forEach { row.addView(it) }
            root.addView(HorizontalScrollView(activity).apply { isHorizontalScrollBarEnabled = false; addView(row) })
        }

        fun setText(s: String) { input.setText(s); input.setSelection(s.length) }

        section(activity.getString(R.string.nfd_names), NAMES.map { n ->
            chipView(n) { val p = split(input.text.toString()); setText(p.dir + n + p.ext) }
        })
        section(activity.getString(R.string.nfd_ext), listOf(
            chipView(".rin") { val p = split(input.text.toString()); setText(p.dir + p.base + ".rin") },
            chipView(".") {
                val t = input.text.toString()
                if (!t.substringAfterLast('/').contains('.')) setText("$t.")
            },
            chipView("/") {
                val t = input.text.toString()
                if (t.isNotBlank() && !t.endsWith("/")) setText("$t/")
            }
        ))

        val baseDir = if (relDir.isBlank()) project.dir else File(project.dir, relDir.trim('/'))
        var dialog: AlertDialog? = null

        fun refreshPreview() {
            val raw = input.text.toString().trim()
            val p = split(raw)
            val segments = (p.dir.split('/').filter { it.isNotBlank() } + p.finalLast)
            val valid = raw.isNotEmpty() && p.base.isNotEmpty() && segments.all { ProjectManager.isValidPathSegment(it) }
            val rel = (p.dir + p.finalLast)
            val exists = valid && File(baseDir, rel).exists()
            when {
                raw.isEmpty() -> { preview.text = " "; preview.setTextColor(c(R.color.rin_files_dim)) }
                !valid -> { preview.text = activity.getString(R.string.nfd_invalid); preview.setTextColor(c(R.color.rin_files_danger)) }
                exists -> { preview.text = activity.getString(R.string.nfd_exists, rel); preview.setTextColor(c(R.color.rin_files_danger)) }
                else -> { preview.text = activity.getString(R.string.nfd_will_create, rel); preview.setTextColor(c(R.color.rin_files_accent)) }
            }
            dialog?.getButton(AlertDialog.BUTTON_POSITIVE)?.isEnabled = valid && !exists
        }
        input.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, st: Int, c: Int, a: Int) {}
            override fun onTextChanged(s: CharSequence?, st: Int, b: Int, c: Int) {}
            override fun afterTextChanged(s: Editable?) { refreshPreview() }
        })

        dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.new_file_title)
            .setView(root)
            .setPositiveButton(R.string.create) { _, _ ->
                val p = split(input.text.toString())
                fun attempt() {
                    try {
                        val file = ProjectManager.writeFile(project, relDir, p.dir + p.finalLast, "")
                        onCreated(file)
                    } catch (t: Throwable) {
                        FileErrors.showSingle(activity, activity.getString(R.string.file_save_error), p.dir + p.finalLast, t) { attempt() }
                    }
                }
                attempt()
            }
            .setNegativeButton(R.string.cancel, null)
            .apply { if (onBatch != null) setNeutralButton(R.string.bt_neutral) { _, _ -> onBatch() } }
            .create()
        dialog.setOnShowListener {
            refreshPreview()
            input.requestFocus()
            (activity.getSystemService(Activity.INPUT_METHOD_SERVICE) as InputMethodManager)
                .showSoftInput(input, InputMethodManager.SHOW_IMPLICIT)
        }
        dialog.show()
    }
}

package com.dlof.rinlang

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.InputType
import android.text.TextWatcher
import android.util.TypedValue
import android.view.ContextThemeWrapper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import com.google.android.material.R as MaterialR

/**
 * مصمّم ثيم المحرر: حوار واحد يضم
 *  1. معاينة **حيّة** داخل [RinCodeEditorView] حقيقي (نفس الخط والرسم والمحلل) بلغة قابلة للتبديل،
 *  2. شرائح ثيمات جاهزة (EditorTheme.PRESETS) + "الافتراضي" (ألوان التطبيق الأصلية)،
 *  3. صف لكل مجموعة لون (كلمات مفتاحية، نصوص، أرقام…) + ألوان السطح (الخلفية، النص، السطر الحالي…)
 *     يفتح منتقي ألوان (عجلة + سطوع + HEX).
 *
 * كل تغيير يُحفَظ فوراً في [EditorDesign] وتُحدَّث المعاينة؛ زر "إلغاء" يستعيد اللقطة المأخوذة عند الفتح.
 * المحرر الفعلي يلتقط الثيم عبر [RinCodeEditorView.refreshTheme] (يستدعيه MainActivity.onResume).
 */
object EditorThemeDesigner {

    fun show(activity: Activity, onChanged: (() -> Unit)? = null) {
        val themed = ContextThemeWrapper(activity, MaterialR.style.ThemeOverlay_MaterialComponents_Dark)
        val density = activity.resources.displayMetrics.density
        fun dp(v: Int) = (v * density).toInt()

        val snapshot = EditorDesign.snapshot(activity)
        val working = EditorDesign.colors(activity)
        var presetId = EditorDesign.presetId(activity)
        var sample = EditorTheme.SAMPLES.first()

        val textColor = ContextCompat.getColor(activity, R.color.rin_on_toolbar)
        val dimColor = ContextCompat.getColor(activity, R.color.rin_on_toolbar_dim)
        val accent = ContextCompat.getColor(activity, R.color.rin_accent)

        // ---- المعاينة ------------------------------------------------------------------------
        val preview = RinCodeEditorView(themed).apply {
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            setPadding(dp(10), dp(8), dp(10), dp(8))
            // معاينة فقط: لا لمس ولا تركيز ولا لوحة مفاتيح
            isFocusable = false
            isFocusableInTouchMode = false
            setOnTouchListener { _, _ -> true }
        }
        val previewHolder = FrameLayout(themed).apply {
            addView(preview, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        }

        fun commit() {
            EditorDesign.save(activity, working, presetId)
            preview.refreshTheme()
            onChanged?.invoke()
        }

        fun loadSample(s: EditorTheme.Sample) {
            sample = s
            preview.setText(s.code)
            preview.setLanguage(s.ext)
            preview.refreshTheme()
        }

        fun chip(label: String, selected: Boolean, onClick: () -> Unit): TextView = TextView(themed).apply {
            text = label
            textSize = 12.5f
            gravity = Gravity.CENTER
            setPadding(dp(12), dp(6), dp(12), dp(6))
            setTextColor(if (selected) Color.WHITE else dimColor)
            setTypeface(typeface, if (selected) Typeface.BOLD else Typeface.NORMAL)
            background = GradientDrawable().apply {
                cornerRadius = dp(16).toFloat()
                setColor(if (selected) accent else 0x22FFFFFF)
            }
            setOnClickListener { onClick() }
        }

        val root = LinearLayout(themed).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(14), dp(6), dp(14), dp(2)) }
        val sampleBar = LinearLayout(themed).apply { orientation = LinearLayout.HORIZONTAL }
        val presetBar = LinearLayout(themed).apply { orientation = LinearLayout.HORIZONTAL }
        val rowsBox = LinearLayout(themed).apply { orientation = LinearLayout.VERTICAL }

        fun label(text: String): TextView = TextView(themed).apply {
            this.text = text
            textSize = 11.5f
            setTextColor(dimColor)
            setPadding(0, dp(10), 0, dp(4))
        }
        fun hscroll(inner: View): HorizontalScrollView = HorizontalScrollView(themed).apply {
            isHorizontalScrollBarEnabled = false
            addView(inner)
        }

        // مرجع للدوال المتبادلة الاستدعاء
        lateinit var rebuildRows: () -> Unit
        lateinit var rebuildPresets: () -> Unit
        lateinit var rebuildSamples: () -> Unit

        // ---- ألوان افتراضية للصفوف (ما يُرى فعلاً الآن) ----------------------------------------
        fun shownColor(key: String): Int {
            working[key]?.let { return it }
            // لا تجاوز: لون التطبيق الأصلي — نقرؤه من لون النص/الخلفية الأساسية أو رمادياً محايداً
            return when (key) {
                EditorTheme.SURFACE_BG -> ContextCompat.getColor(activity, R.color.rin_editor_bg)
                EditorTheme.SURFACE_TEXT -> ContextCompat.getColor(activity, R.color.rin_editor_text)
                EditorTheme.SURFACE_LINE -> ContextCompat.getColor(activity, R.color.rin_current_line_bg)
                EditorTheme.SURFACE_SELECTION -> 0x553D7EFF
                EditorTheme.SURFACE_GUIDE -> 0x33808080
                else -> preview.defaultColorForGroup(key)
            }
        }

        fun pickColor(key: String, title: String) {
            val start = shownColor(key)
            var alpha = (start ushr 24).let { if (it == 0) 0xFF else it }
            var current = (alpha shl 24) or (start and 0xFFFFFF)
            val hsv = FloatArray(3).also { Color.colorToHSV(current, it) }

            val body = LinearLayout(themed).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(16), dp(8), dp(16), 0) }
            val swatch = View(themed)
            val hex = EditText(themed).apply {
                setSingleLine(true)
                inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
                setTextColor(textColor)
                typeface = Typeface.MONOSPACE
                textSize = 14f
                gravity = Gravity.CENTER
            }
            val wheel = HueSaturationPickerView(themed)
            val bright = SeekBar(themed).apply { max = 100 }
            var updating = false

            fun refreshViews(fromHex: Boolean) {
                swatch.setBackgroundColor(current)
                if (!fromHex) { updating = true; hex.setText(EditorTheme.argbToHex(current)); updating = false }
            }
            fun fromHsv() {
                current = Color.HSVToColor(alpha, hsv)
                refreshViews(false)
            }
            wheel.onColorChanged = { c ->
                val h = FloatArray(3); Color.colorToHSV(c, h)
                hsv[0] = h[0]; hsv[1] = h[1]
                fromHsv()
            }
            bright.progress = (hsv[2] * 100).toInt()
            bright.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    hsv[2] = progress / 100f
                    fromHsv()
                }
                override fun onStartTrackingTouch(bar: SeekBar?) {}
                override fun onStopTrackingTouch(bar: SeekBar?) {}
            })
            hex.addTextChangedListener(object : TextWatcher {
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}
                override fun afterTextChanged(s: Editable?) {
                    if (updating) return
                    val v = EditorTheme.hexToArgb(s?.toString() ?: return) ?: return
                    current = v
                    alpha = v ushr 24
                    Color.colorToHSV(v, hsv)
                    bright.progress = (hsv[2] * 100).toInt()
                    wheel.setColor(Color.HSVToColor(floatArrayOf(hsv[0], hsv[1], 1f)))
                    swatch.setBackgroundColor(current)
                }
            })
            body.addView(wheel, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(210)))
            body.addView(bright, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(6) })
            body.addView(swatch, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(28)).apply { topMargin = dp(8) })
            body.addView(hex, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
            wheel.setColor(Color.HSVToColor(floatArrayOf(hsv[0], hsv[1], 1f)))
            refreshViews(false)

            AlertDialog.Builder(themed)
                .setTitle(title)
                .setView(body)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    working[key] = current
                    presetId = EditorDesign.PRESET_CUSTOM
                    commit(); rebuildRows(); rebuildPresets()
                }
                .setNeutralButton(R.string.design_reset_color) { _, _ ->
                    working.remove(key)
                    presetId = EditorDesign.PRESET_CUSTOM
                    commit(); rebuildRows(); rebuildPresets()
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        fun colorRow(key: String, title: String): View {
            val color = shownColor(key)
            val dot = View(themed).apply {
                background = GradientDrawable().apply {
                    shape = GradientDrawable.OVAL
                    setColor(color)
                    setStroke(dp(1), 0x66FFFFFF)
                }
            }
            val name = TextView(themed).apply { text = title; textSize = 13.5f; setTextColor(textColor) }
            val value = TextView(themed).apply {
                text = if (working.containsKey(key)) EditorTheme.argbToHex(color) else activity.getString(R.string.design_color_auto)
                textSize = 11.5f; typeface = Typeface.MONOSPACE; setTextColor(dimColor)
            }
            return LinearLayout(themed).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(0, dp(7), 0, dp(7))
                addView(dot, LinearLayout.LayoutParams(dp(22), dp(22)))
                addView(name, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply { marginStart = dp(12) })
                addView(value)
                setOnClickListener { pickColor(key, title) }
            }
        }

        rebuildRows = {
            rowsBox.removeAllViews()
            rowsBox.addView(label(activity.getString(R.string.design_section_surface)))
            for (k in EditorTheme.SURFACES) rowsBox.addView(colorRow(k, activity.getString(surfaceLabel(k))))
            rowsBox.addView(label(activity.getString(R.string.design_section_syntax)))
            for (g in EditorTheme.GROUPS) rowsBox.addView(colorRow(g.key, activity.getString(groupLabel(g.key))))
        }

        rebuildPresets = {
            presetBar.removeAllViews()
            fun add(text: String, id: String, action: () -> Unit) {
                val v = chip(text, presetId == id, action)
                presetBar.addView(v, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(6) })
            }
            add(activity.getString(R.string.design_preset_default), EditorDesign.PRESET_DEFAULT) {
                working.clear(); presetId = EditorDesign.PRESET_DEFAULT
                commit(); rebuildRows(); rebuildPresets()
            }
            for (p in EditorTheme.PRESETS) add(p.name, p.id) {
                working.clear(); working.putAll(p.colors); presetId = p.id
                commit(); rebuildRows(); rebuildPresets()
            }
            if (presetId == EditorDesign.PRESET_CUSTOM) {
                presetBar.addView(chip(activity.getString(R.string.design_preset_custom), true) {}, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT))
            }
        }

        rebuildSamples = {
            sampleBar.removeAllViews()
            for (s in EditorTheme.SAMPLES) {
                val v = chip(s.label, s === sample) { loadSample(s); rebuildSamples() }
                sampleBar.addView(v, LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { marginEnd = dp(6) })
            }
        }

        root.addView(previewHolder, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(190)))
        root.addView(hscroll(sampleBar), LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(8) })
        root.addView(label(activity.getString(R.string.design_section_presets)))
        root.addView(hscroll(presetBar))
        root.addView(rowsBox)

        val scroll = ScrollView(themed).apply { addView(root) }

        preview.refreshTheme()
        loadSample(sample)
        rebuildSamples(); rebuildPresets(); rebuildRows()

        AlertDialog.Builder(themed)
            .setTitle(R.string.design_dialog_title)
            .setView(scroll)
            .setPositiveButton(R.string.design_save) { d, _ -> onChanged?.invoke(); d.dismiss() }
            .setNegativeButton(android.R.string.cancel) { _, _ -> EditorDesign.restore(activity, snapshot); onChanged?.invoke() }
            .setOnCancelListener { EditorDesign.restore(activity, snapshot); onChanged?.invoke() }
            .show()
    }

    private fun groupLabel(key: String): Int = when (key) {
        "keyword" -> R.string.design_group_keyword
        "control" -> R.string.design_group_control
        "type" -> R.string.design_group_type
        "function" -> R.string.design_group_function
        "string" -> R.string.design_group_string
        "number" -> R.string.design_group_number
        "comment" -> R.string.design_group_comment
        "constant" -> R.string.design_group_constant
        "property" -> R.string.design_group_property
        "variable" -> R.string.design_group_variable
        "modifier" -> R.string.design_group_modifier
        "tag" -> R.string.design_group_tag
        "operator" -> R.string.design_group_operator
        "punct" -> R.string.design_group_punct
        "bracket0" -> R.string.design_group_bracket0
        "bracket1" -> R.string.design_group_bracket1
        "bracket2" -> R.string.design_group_bracket2
        else -> R.string.design_group_error
    }

    private fun surfaceLabel(key: String): Int = when (key) {
        EditorTheme.SURFACE_BG -> R.string.design_surface_bg
        EditorTheme.SURFACE_TEXT -> R.string.design_surface_text
        EditorTheme.SURFACE_LINE -> R.string.design_surface_line
        EditorTheme.SURFACE_SELECTION -> R.string.design_surface_selection
        else -> R.string.design_surface_guide
    }
}

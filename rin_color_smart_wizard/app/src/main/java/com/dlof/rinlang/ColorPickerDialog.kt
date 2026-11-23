package com.dlof.rinlang

import android.app.AlertDialog
import android.content.Context
import android.graphics.Color
import android.graphics.drawable.GradientDrawable
import android.text.Editable
import android.text.InputFilter
import android.text.InputType
import android.text.TextWatcher
import android.view.Gravity
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import androidx.core.content.ContextCompat

/**
 * منتقي ألوان قابل لإعادة الاستخدام (يستعمله المحرر عند الضغط على مربع لون، ومعالج المشروع الجديد):
 * عجلة Hue/Saturation ([HueSaturationPickerView]) + شريط السطوع + شريط الشفافية + حقل hex
 * (6 أو 8 خانات، الشفافية في الآخر كما في CSS) + معاينة حيّة. [onPicked] يُستدعى عند "موافق" فقط
 * بلون ARGB (الشفافية 255 ما لم يحرّكها المستخدم).
 */
object ColorPickerDialog {

    fun show(context: Context, initial: Int, onPicked: (Int) -> Unit) {
        val density = context.resources.displayMetrics.density
        fun dp(v: Int) = (v * density).toInt()

        val hsv = FloatArray(3)
        Color.colorToHSV(initial, hsv)
        var alpha = (initial ushr 24) and 0xFF
        var updating = false

        fun current(): Int = Color.HSVToColor(alpha, hsv)

        val textColor = ContextCompat.getColor(context, R.color.rin_on_toolbar)
        val dim = ContextCompat.getColor(context, R.color.rin_editor_hint)
        val divider = ContextCompat.getColor(context, R.color.rin_divider)

        val root = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(10), dp(22), dp(4))
        }
        val wheel = HueSaturationPickerView(context)
        root.addView(wheel, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(220)))

        fun label(text: String, topPadDp: Int = 0): TextView = TextView(context).apply {
            this.text = text
            setTextColor(dim)
            textSize = 12f
            setPadding(0, dp(topPadDp), 0, 0)
        }

        val valueBar = SeekBar(context).apply { max = 100; progress = (hsv[2] * 100f).toInt() }
        val alphaBar = SeekBar(context).apply { max = 255; progress = alpha }
        root.addView(label(context.getString(R.string.color_picker_brightness), 10))
        root.addView(valueBar, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))
        root.addView(label(context.getString(R.string.color_picker_opacity)))
        root.addView(alphaBar, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT))

        val row = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        val preview = View(context)
        row.addView(preview, LinearLayout.LayoutParams(dp(34), dp(34)))
        val hash = TextView(context).apply { text = "#"; setTextColor(dim); textSize = 15f }
        row.addView(hash, LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { marginStart = dp(12) })
        val hex = EditText(context).apply {
            setSingleLine(true)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            filters = arrayOf(InputFilter.LengthFilter(9))
            setTextColor(textColor)
            setHintTextColor(dim)
            hint = "RRGGBB"
        }
        row.addView(hex, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { marginStart = dp(4) })
        root.addView(row, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(14) })

        fun hexText(): String {
            val full = ColorLiterals.format(current(), ColorLiterals.Format.HEX6, true)
            return full.removePrefix("#")
        }

        fun refresh(writeHex: Boolean) {
            updating = true
            preview.background = GradientDrawable().apply {
                shape = GradientDrawable.RECTANGLE
                cornerRadius = dp(6).toFloat()
                setColor(current())
                setStroke(dp(1), divider)
            }
            if (writeHex) hex.setText(hexText())
            updating = false
        }

        wheel.onColorChanged = { c ->
            val tmp = FloatArray(3)
            Color.colorToHSV(c, tmp)
            hsv[0] = tmp[0]; hsv[1] = tmp[1]
            refresh(true)
        }
        wheel.post { wheel.setColor(Color.HSVToColor(floatArrayOf(hsv[0], hsv[1], 1f))) }

        valueBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                if (!fromUser) return
                hsv[2] = progress / 100f; refresh(true)
            }
            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })
        alphaBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                if (!fromUser) return
                alpha = progress; refresh(true)
            }
            override fun onStartTrackingTouch(sb: SeekBar?) {}
            override fun onStopTrackingTouch(sb: SeekBar?) {}
        })

        hex.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                if (updating) return
                val raw = s?.toString()?.trim()?.removePrefix("#") ?: return
                if (raw.length != 6 && raw.length != 8) return
                val parsed = ColorLiterals.parse("#$raw") ?: return
                Color.colorToHSV(parsed, hsv)
                alpha = (parsed ushr 24) and 0xFF
                valueBar.progress = (hsv[2] * 100f).toInt()
                alphaBar.progress = alpha
                wheel.setColor(Color.HSVToColor(floatArrayOf(hsv[0], hsv[1], 1f)))
                refresh(false)
            }
        })

        refresh(true)

        AlertDialog.Builder(context)
            .setTitle(R.string.color_picker_title)
            .setView(root)
            .setPositiveButton(android.R.string.ok) { _, _ -> onPicked(current()) }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

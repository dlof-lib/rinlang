package com.dlof.rinlang

import android.os.Bundle
import android.view.View
import android.widget.CompoundButton
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * شاشة "الإعدادات" العامة للتطبيق — منفصلة عن شاشة "حسابي" ([com.dlof.rinlang.store.AccountActivity])
 * لأنها لا علاقة لها بالحساب/تسجيل الدخول، بل بتفضيلات المحرر فقط (حجم الخط، أرقام الأسطر).
 * تُحفَظ القيم في [AppSettings] (SharedPreferences) ويقرأها [MainActivity] عند كل فتح للمحرر.
 */
class SettingsActivity : AppCompatActivity() {

    private lateinit var seekFontSize: SeekBar
    private lateinit var txtFontSizeLabel: TextView
    private lateinit var switchLineNumbers: Switch

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.settings_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        seekFontSize = findViewById(R.id.seekFontSize)
        txtFontSizeLabel = findViewById(R.id.txtFontSizeLabel)
        switchLineNumbers = findViewById(R.id.switchLineNumbers)

        applyCurrentValuesToUi()

        seekFontSize.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val sp = AppSettings.MIN_FONT_SIZE_SP + progress
                txtFontSizeLabel.text = getString(R.string.settings_font_size_label, sp)
                if (fromUser) AppSettings.setEditorFontSizeSp(this@SettingsActivity, sp)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })

        switchLineNumbers.setOnCheckedChangeListener { _: CompoundButton, isChecked: Boolean ->
            AppSettings.setShowLineNumbers(this, isChecked)
        }

        findViewById<View>(R.id.btnResetDefaults).setOnClickListener {
            AppSettings.resetToDefaults(this)
            applyCurrentValuesToUi()
            android.widget.Toast.makeText(this, R.string.settings_reset_toast, android.widget.Toast.LENGTH_SHORT).show()
        }
    }

    private fun applyCurrentValuesToUi() {
        val fontSize = AppSettings.getEditorFontSizeSp(this)
        seekFontSize.progress = (fontSize - AppSettings.MIN_FONT_SIZE_SP).toInt()
        txtFontSizeLabel.text = getString(R.string.settings_font_size_label, fontSize)
        switchLineNumbers.isChecked = AppSettings.getShowLineNumbers(this)
    }
}

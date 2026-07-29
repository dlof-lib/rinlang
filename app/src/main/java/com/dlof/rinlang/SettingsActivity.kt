package com.dlof.rinlang

import android.os.Bundle
import android.view.View
import android.widget.CompoundButton
import android.widget.ImageView
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/**
 * شاشة "الإعدادات" العامة للتطبيق — منفصلة عن شاشة "حسابي" ([com.dlof.rinlang.store.AccountActivity])
 * لأنها لا علاقة لها بالحساب/تسجيل الدخول، بل بتفضيلات المحرر فقط (حجم الخط، أرقام الأسطر)
 * ولغة واجهة التطبيق (عربي/إنجليزي/إسباني عبر [LocaleHelper]).
 * تُحفَظ القيم في [AppSettings] (SharedPreferences) ويقرأها [MainActivity] عند كل فتح للمحرر.
 */
class SettingsActivity : AppCompatActivity() {

    private lateinit var seekFontSize: SeekBar
    private lateinit var txtFontSizeLabel: TextView
    private lateinit var switchLineNumbers: Switch

    private lateinit var rowLangAr: View
    private lateinit var rowLangEn: View
    private lateinit var rowLangEs: View
    private lateinit var checkLangAr: ImageView
    private lateinit var checkLangEn: ImageView
    private lateinit var checkLangEs: ImageView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)

        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.settings_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        seekFontSize = findViewById(R.id.seekFontSize)
        txtFontSizeLabel = findViewById(R.id.txtFontSizeLabel)
        switchLineNumbers = findViewById(R.id.switchLineNumbers)

        rowLangAr = findViewById(R.id.rowLangAr)
        rowLangEn = findViewById(R.id.rowLangEn)
        rowLangEs = findViewById(R.id.rowLangEs)
        checkLangAr = findViewById(R.id.checkLangAr)
        checkLangEn = findViewById(R.id.checkLangEn)
        checkLangEs = findViewById(R.id.checkLangEs)

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

        rowLangAr.setOnClickListener { selectLanguage(LocaleHelper.LANG_ARABIC) }
        rowLangEn.setOnClickListener { selectLanguage(LocaleHelper.LANG_ENGLISH) }
        rowLangEs.setOnClickListener { selectLanguage(LocaleHelper.LANG_SPANISH) }

        findViewById<View>(R.id.btnResetDefaults).setOnClickListener {
            AppSettings.resetToDefaults(this)
            applyCurrentValuesToUi()
            android.widget.Toast.makeText(this, R.string.settings_reset_toast, android.widget.Toast.LENGTH_SHORT).show()
        }
    }

    /**
     * يطبّق اللغة الجديدة فوراً عبر [LocaleHelper]؛ يعيد أندرويد بناء هذه الشاشة (وكل الشاشات
     * المفتوحة) تلقائياً بعد هذا النداء لتحديث النصوص واتجاه الواجهة (RTL/LTR).
     */
    private fun selectLanguage(languageTag: String) {
        if (LocaleHelper.getCurrentAppLocaleTag() == languageTag) return
        LocaleHelper.setAppLocale(languageTag)
    }

    private fun applyCurrentValuesToUi() {
        val fontSize = AppSettings.getEditorFontSizeSp(this)
        seekFontSize.progress = (fontSize - AppSettings.MIN_FONT_SIZE_SP).toInt()
        txtFontSizeLabel.text = getString(R.string.settings_font_size_label, fontSize)
        switchLineNumbers.isChecked = AppSettings.getShowLineNumbers(this)

        val current = LocaleHelper.getCurrentAppLocaleTag() ?: LocaleHelper.LANG_ARABIC
        highlightSelectedLanguage(current)
    }

    private fun highlightSelectedLanguage(languageTag: String) {
        rowLangAr.setBackgroundResource(
            if (languageTag == LocaleHelper.LANG_ARABIC) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card
        )
        rowLangEn.setBackgroundResource(
            if (languageTag == LocaleHelper.LANG_ENGLISH) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card
        )
        rowLangEs.setBackgroundResource(
            if (languageTag == LocaleHelper.LANG_SPANISH) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card
        )
        checkLangAr.visibility = if (languageTag == LocaleHelper.LANG_ARABIC) View.VISIBLE else View.INVISIBLE
        checkLangEn.visibility = if (languageTag == LocaleHelper.LANG_ENGLISH) View.VISIBLE else View.INVISIBLE
        checkLangEs.visibility = if (languageTag == LocaleHelper.LANG_SPANISH) View.VISIBLE else View.INVISIBLE
    }
}

package com.dlof.rinlang

import android.os.Bundle
import android.view.View
import android.widget.CompoundButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

/** Professional settings hub: all editor preferences are persistent and immediately reusable. */
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

    private val switches = mutableMapOf<Int, Switch>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)
        RinLoading.startup(this, 650L)
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

        seekFontSize.max = 12
        seekFontSize.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, progress: Int, fromUser: Boolean) {
                val sp = AppSettings.MIN_FONT_SIZE_SP + progress
                txtFontSizeLabel.text = getString(R.string.settings_font_size_label, sp)
                if (fromUser) AppSettings.setEditorFontSizeSp(this@SettingsActivity, sp)
            }
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        switchLineNumbers.setOnCheckedChangeListener { _: CompoundButton, checked -> AppSettings.setShowLineNumbers(this, checked) }

        bindSwitch(R.id.switchSyntax, R.string.settings_syntax, R.string.settings_syntax_hint, AppSettings.isSyntaxHighlighting(this)) { AppSettings.setSyntaxHighlighting(this, it) }
        bindSwitch(R.id.switchBrackets, R.string.settings_brackets, R.string.settings_brackets_hint, AppSettings.isBracketMatching(this)) { AppSettings.setBracketMatching(this, it) }
        bindSwitch(R.id.switchAutocomplete, R.string.settings_autocomplete, R.string.settings_autocomplete_hint, AppSettings.isAutocomplete(this)) { AppSettings.setAutocomplete(this, it) }
        bindSwitch(R.id.switchAutoClose, R.string.settings_auto_close, R.string.settings_auto_close_hint, AppSettings.isAutoCloseBrackets(this)) { AppSettings.setAutoCloseBrackets(this, it) }
        bindSwitch(R.id.switchAutoIndent, R.string.settings_auto_indent, R.string.settings_auto_indent_hint, AppSettings.isAutoIndent(this)) { AppSettings.setAutoIndent(this, it) }
        bindSwitch(R.id.switchWordWrap, R.string.settings_word_wrap, R.string.settings_word_wrap_hint, AppSettings.isWordWrap(this)) { AppSettings.setWordWrap(this, it) }
        bindSwitch(R.id.switchWhitespace, R.string.settings_whitespace, R.string.settings_whitespace_hint, AppSettings.isShowWhitespace(this)) { AppSettings.setShowWhitespace(this, it) }
        bindSwitch(R.id.switchCurrentLine, R.string.settings_current_line, R.string.settings_current_line_hint, AppSettings.isCurrentLineHighlight(this)) { AppSettings.setCurrentLineHighlight(this, it) }
        bindSwitch(R.id.switchSmoothCursor, R.string.settings_smooth_cursor, R.string.settings_smooth_cursor_hint, AppSettings.isSmoothCursor(this)) { AppSettings.setSmoothCursor(this, it) }
        bindSwitch(R.id.switchMinimap, R.string.settings_minimap, R.string.settings_minimap_hint, AppSettings.isMinimap(this)) { AppSettings.setMinimap(this, it) }
        bindSwitch(R.id.switchHaptic, R.string.settings_haptic, R.string.settings_haptic_hint, AppSettings.isHapticFeedback(this)) { AppSettings.setHapticFeedback(this, it) }
        bindSwitch(R.id.switchSavePause, R.string.settings_save_pause, R.string.settings_save_pause_hint, AppSettings.isSaveOnPause(this)) { AppSettings.setSaveOnPause(this, it) }
        bindSwitch(R.id.switchConfirmExit, R.string.settings_confirm_exit, R.string.settings_confirm_exit_hint, AppSettings.isConfirmExit(this)) { AppSettings.setConfirmExit(this, it) }
        bindSwitch(R.id.switchFormatSave, R.string.settings_format_save, R.string.settings_format_save_hint, AppSettings.isFormatOnSave(this)) { AppSettings.setFormatOnSave(this, it) }

        findViewById<View>(R.id.btnTab2).setOnClickListener { selectTabSize(2) }
        findViewById<View>(R.id.btnTab4).setOnClickListener { selectTabSize(4) }
        findViewById<View>(R.id.btnTab8).setOnClickListener { selectTabSize(8) }

        rowLangAr.setOnClickListener { selectLanguage(LocaleHelper.LANG_ARABIC) }
        rowLangEn.setOnClickListener { selectLanguage(LocaleHelper.LANG_ENGLISH) }
        rowLangEs.setOnClickListener { selectLanguage(LocaleHelper.LANG_SPANISH) }

        findViewById<View>(R.id.btnThemeSystem).setOnClickListener { setTheme(ThemeManager.SYSTEM) }
        findViewById<View>(R.id.btnThemeDark).setOnClickListener { setTheme(ThemeManager.DARK) }
        findViewById<View>(R.id.btnThemeLight).setOnClickListener { setTheme(ThemeManager.LIGHT) }
        findViewById<View>(R.id.btnLayoutStandard).setOnClickListener { setLayout("standard") }
        findViewById<View>(R.id.btnLayoutFocus).setOnClickListener { setLayout("focus") }
        findViewById<View>(R.id.btnLayoutCompact).setOnClickListener { setLayout("compact") }
        bindSwitch(R.id.switchShowConsole, R.string.settings_show_console, R.string.settings_show_console_hint, AppSettings.isShowConsole(this)) { AppSettings.setShowConsole(this, it) }
        bindSwitch(R.id.switchShowToolbar, R.string.settings_show_toolbar, R.string.settings_show_toolbar_hint, AppSettings.isShowToolbar(this)) { AppSettings.setShowToolbar(this, it) }
        findViewById<View>(R.id.btnSortRecent).setOnClickListener { setProjectSort("recent") }
        findViewById<View>(R.id.btnSortName).setOnClickListener { setProjectSort("name") }
        findViewById<View>(R.id.btnSortType).setOnClickListener { setProjectSort("type") }

        findViewById<View>(R.id.btnResetDefaults).setOnClickListener {
            AppSettings.resetToDefaults(this)
            applyCurrentValuesToUi()
            Toast.makeText(this, R.string.settings_reset_toast, Toast.LENGTH_SHORT).show()
        }
        applyCurrentValuesToUi()
    }

    private fun setTheme(mode: String) { AppSettings.setThemeMode(this, mode); ThemeManager.apply(this); recreate() }
    private fun setLayout(value: String) { AppSettings.setEditorLayout(this, value); updateLayoutButtons() }
    private fun setProjectSort(value: String) { AppSettings.setProjectSort(this, value); updateProjectSortButtons() }
    private fun updateLayoutButtons() { val s=AppSettings.getEditorLayout(this); listOf(R.id.btnLayoutStandard to "standard",R.id.btnLayoutFocus to "focus",R.id.btnLayoutCompact to "compact").forEach { (id,v)->findViewById<TextView>(id).setBackgroundResource(if(s==v) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card) } }
    private fun updateProjectSortButtons() { val s=AppSettings.getProjectSort(this); listOf(R.id.btnSortRecent to "recent",R.id.btnSortName to "name",R.id.btnSortType to "type").forEach { (id,v)->findViewById<TextView>(id).setBackgroundResource(if(s==v) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card) } }

    private fun bindSwitch(id: Int, title: Int, hint: Int, initial: Boolean, save: (Boolean) -> Unit) {
        val row = findViewById<View>(id).parent as View
        row.findViewById<TextView>(R.id.preferenceTitle).text = getString(title)
        row.findViewById<TextView>(R.id.preferenceHint).text = getString(hint)
        val sw = findViewById<Switch>(id)
        switches[id] = sw
        sw.setOnCheckedChangeListener { _, checked -> save(checked) }
        sw.isChecked = initial
    }

    private fun selectTabSize(size: Int) {
        AppSettings.setTabSize(this, size)
        updateTabButtons()
    }

    private fun updateTabButtons() {
        val selected = AppSettings.getTabSize(this)
        findViewById<TextView>(R.id.btnTab2).setBackgroundResource(if (selected == 2) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card)
        findViewById<TextView>(R.id.btnTab4).setBackgroundResource(if (selected == 4) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card)
        findViewById<TextView>(R.id.btnTab8).setBackgroundResource(if (selected == 8) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card)
    }

    private fun selectLanguage(languageTag: String) {
        if (LocaleHelper.getCurrentAppLocaleTag() == languageTag) return
        LocaleHelper.setAppLocale(languageTag)
    }

    private fun applyCurrentValuesToUi() {
        val fontSize = AppSettings.getEditorFontSizeSp(this)
        seekFontSize.progress = (fontSize - AppSettings.MIN_FONT_SIZE_SP).toInt()
        txtFontSizeLabel.text = getString(R.string.settings_font_size_label, fontSize)
        switchLineNumbers.isChecked = AppSettings.getShowLineNumbers(this)
        switches[R.id.switchSyntax]?.isChecked = AppSettings.isSyntaxHighlighting(this)
        switches[R.id.switchBrackets]?.isChecked = AppSettings.isBracketMatching(this)
        switches[R.id.switchAutocomplete]?.isChecked = AppSettings.isAutocomplete(this)
        switches[R.id.switchAutoClose]?.isChecked = AppSettings.isAutoCloseBrackets(this)
        switches[R.id.switchAutoIndent]?.isChecked = AppSettings.isAutoIndent(this)
        switches[R.id.switchWordWrap]?.isChecked = AppSettings.isWordWrap(this)
        switches[R.id.switchWhitespace]?.isChecked = AppSettings.isShowWhitespace(this)
        switches[R.id.switchCurrentLine]?.isChecked = AppSettings.isCurrentLineHighlight(this)
        switches[R.id.switchSmoothCursor]?.isChecked = AppSettings.isSmoothCursor(this)
        switches[R.id.switchMinimap]?.isChecked = AppSettings.isMinimap(this)
        switches[R.id.switchHaptic]?.isChecked = AppSettings.isHapticFeedback(this)
        switches[R.id.switchSavePause]?.isChecked = AppSettings.isSaveOnPause(this)
        switches[R.id.switchConfirmExit]?.isChecked = AppSettings.isConfirmExit(this)
        switches[R.id.switchFormatSave]?.isChecked = AppSettings.isFormatOnSave(this)
        updateTabButtons()
        updateLayoutButtons()
        updateProjectSortButtons()
        val current = LocaleHelper.getCurrentAppLocaleTag() ?: LocaleHelper.LANG_ARABIC
        highlightSelectedLanguage(current)
    }

    private fun highlightSelectedLanguage(tag: String) {
        rowLangAr.setBackgroundResource(if (tag == LocaleHelper.LANG_ARABIC) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card)
        rowLangEn.setBackgroundResource(if (tag == LocaleHelper.LANG_ENGLISH) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card)
        rowLangEs.setBackgroundResource(if (tag == LocaleHelper.LANG_SPANISH) R.drawable.bg_lang_option_selected else R.drawable.bg_list_card)
        checkLangAr.visibility = if (tag == LocaleHelper.LANG_ARABIC) View.VISIBLE else View.INVISIBLE
        checkLangEn.visibility = if (tag == LocaleHelper.LANG_ENGLISH) View.VISIBLE else View.INVISIBLE
        checkLangEs.visibility = if (tag == LocaleHelper.LANG_SPANISH) View.VISIBLE else View.INVISIBLE
    }
}

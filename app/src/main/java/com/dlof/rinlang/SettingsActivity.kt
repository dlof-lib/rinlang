package com.dlof.rinlang

import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import androidx.annotation.StringRes
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import kotlin.math.roundToInt

/**
 * مركز الإعدادات. كل الأقسام والصفوف معرَّفة في [buildSchema] فقط، والشاشة تبنيها من
 * item_setting_{section,switch,choice,slider,link}.xml — لا XML مكرَّر لكل إعداد، وإضافة إعداد جديد
 * سطر واحد هنا (مع نصوصه) بدل صف XML كامل + ربط يدوي في onCreate + تحديث يدوي في applyCurrentValuesToUi.
 *
 * ترتيب الأقسام: المحرر ← المساعدة أثناء الكتابة ← الحفظ ← التشغيل ← الواجهة ← المشاريع ← اللغة.
 */
class SettingsActivity : AppCompatActivity() {

    // ---- نموذج المخطط -----------------------------------------------------------------------

    private class Option(val value: String, @StringRes val labelRes: Int = 0, val labelText: String? = null)

    private sealed class Row {
        class Section(@StringRes val title: Int) : Row()
        class Toggle(val key: String, @StringRes val title: Int, @StringRes val hint: Int) : Row()
        class Choice(
            @StringRes val title: Int,
            @StringRes val hint: Int?,
            val options: List<Option>,
            val read: () -> String,
            val write: (String) -> Unit,
            val onChanged: (() -> Unit)? = null
        ) : Row()
        object FontSize : Row()
        class Link(
            val icon: String,
            @StringRes val title: Int,
            @StringRes val hint: Int,
            val onClick: () -> Unit
        ) : Row()
    }

    private lateinit var container: LinearLayout
    private val schema: List<Row> by lazy { buildSchema() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_settings)
        RinLoading.startup(this, 650L)
        findViewById<TextView>(R.id.txtToolbarTitle).text = getString(R.string.settings_title)
        findViewById<View>(R.id.btnToolbarBack).setOnClickListener { finish() }

        container = findViewById(R.id.settingsContainer)
        findViewById<View>(R.id.btnResetDefaults).setOnClickListener {
            AppSettings.resetToDefaults(this)
            Toast.makeText(this, R.string.settings_reset_toast, Toast.LENGTH_SHORT).show()
            render()
            ThemeManager.apply(this)
        }
        render()
    }

    // ---- المخطط: مصدر الحقيقة الوحيد لمحتوى الشاشة وترتيبها ---------------------------------

    private fun buildSchema(): List<Row> {
        val key = AppSettings.Key
        return listOf(
            Row.Section(R.string.settings_editor_section_title),
            Row.FontSize,
            Row.Toggle(key.LINE_NUMBERS, R.string.settings_show_line_numbers, R.string.settings_show_line_numbers_hint),
            Row.Choice(
                R.string.settings_tab_size, R.string.settings_tab_size_hint,
                options = listOf(2, 4, 8).map { Option(it.toString(), labelText = it.toString()) },
                read = { AppSettings.getTabSize(this).toString() },
                write = { AppSettings.setTabSize(this, it.toInt()) }
            ),
            Row.Toggle(key.SHOW_WHITESPACE, R.string.settings_whitespace, R.string.settings_whitespace_hint),
            Row.Toggle(key.CURRENT_LINE, R.string.settings_current_line, R.string.settings_current_line_hint),
            Row.Toggle(key.SMOOTH_CURSOR, R.string.settings_smooth_cursor, R.string.settings_smooth_cursor_hint),

            Row.Section(R.string.settings_assist_section_title),
            Row.Toggle(key.SYNTAX, R.string.settings_syntax, R.string.settings_syntax_hint),
            Row.Toggle(key.BRACKETS, R.string.settings_brackets, R.string.settings_brackets_hint),
            Row.Toggle(key.LIVE_DIAGNOSTICS, R.string.settings_live_diagnostics, R.string.settings_live_diagnostics_hint),
            Row.Toggle(key.AUTOCOMPLETE, R.string.settings_autocomplete, R.string.settings_autocomplete_hint),
            Row.Toggle(key.AUTO_CLOSE, R.string.settings_auto_close, R.string.settings_auto_close_hint),
            Row.Toggle(key.AUTO_INDENT, R.string.settings_auto_indent, R.string.settings_auto_indent_hint),

            Row.Section(R.string.settings_saving_section_title),
            Row.Toggle(key.SAVE_ON_PAUSE, R.string.settings_save_pause, R.string.settings_save_pause_hint),
            Row.Choice(
                R.string.settings_auto_save, R.string.settings_auto_save_hint,
                options = AppSettings.AUTO_SAVE_DELAYS_MS.map { ms ->
                    if (ms == 0) Option(ms.toString(), R.string.settings_off)
                    else Option(ms.toString(), labelText = getString(R.string.settings_seconds_format, ms / 1000))
                },
                read = { AppSettings.getAutoSaveDelayMs(this).toString() },
                write = { AppSettings.setAutoSaveDelayMs(this, it.toInt()) }
            ),
            Row.Toggle(key.CONFIRM_EXIT, R.string.settings_confirm_exit, R.string.settings_confirm_exit_hint),
            Row.Toggle(key.FORMAT_ON_SAVE, R.string.settings_format_save, R.string.settings_format_save_hint),
            Row.Toggle(key.TRIM_TRAILING, R.string.settings_trim_trailing, R.string.settings_trim_trailing_hint),
            Row.Toggle(key.FINAL_NEWLINE, R.string.settings_final_newline, R.string.settings_final_newline_hint),

            Row.Section(R.string.settings_run_section_title),
            Row.Toggle(key.AUTO_RUN, R.string.settings_auto_run, R.string.settings_auto_run_hint),
            Row.Toggle(key.CLEAR_CONSOLE_ON_RUN, R.string.settings_clear_console_on_run, R.string.settings_clear_console_on_run_hint),

            Row.Section(R.string.settings_interface_section_title),
            Row.Choice(
                R.string.settings_theme_section_title, null,
                options = listOf(
                    Option(ThemeManager.SYSTEM, R.string.theme_system),
                    Option(ThemeManager.DARK, R.string.theme_dark),
                    Option(ThemeManager.LIGHT, R.string.theme_light)
                ),
                read = { AppSettings.getThemeMode(this) },
                write = { AppSettings.setThemeMode(this, it) },
                onChanged = { ThemeManager.apply(this); recreate() }
            ),
            Row.Choice(
                R.string.settings_layout_section_title, R.string.settings_layout_hint,
                options = listOf(
                    Option(AppSettings.LAYOUT_STANDARD, R.string.layout_standard),
                    Option(AppSettings.LAYOUT_FOCUS, R.string.layout_focus),
                    Option(AppSettings.LAYOUT_COMPACT, R.string.layout_compact)
                ),
                read = { AppSettings.getEditorLayout(this) },
                write = { AppSettings.setEditorLayout(this, it) }
            ),
            Row.Toggle(key.SHOW_TOOLBAR, R.string.settings_show_toolbar, R.string.settings_show_toolbar_hint),
            Row.Toggle(key.SHOW_CONSOLE, R.string.settings_show_console, R.string.settings_show_console_hint),
            Row.Toggle(key.KEEP_SCREEN_ON, R.string.settings_keep_screen_on, R.string.settings_keep_screen_on_hint),
            Row.Toggle(key.HAPTIC, R.string.settings_haptic, R.string.settings_haptic_hint),

            Row.Section(R.string.settings_projects_section_title),
            Row.Choice(
                R.string.settings_project_sort, null,
                options = listOf(
                    Option(AppSettings.SORT_RECENT, R.string.sort_recent),
                    Option(AppSettings.SORT_NAME, R.string.sort_name),
                    Option(AppSettings.SORT_TYPE, R.string.sort_type)
                ),
                read = { AppSettings.getProjectSort(this) },
                write = { AppSettings.setProjectSort(this, it) }
            ),

            Row.Section(R.string.settings_language_section_title),
            Row.Link("🌐", R.string.settings_language_current, R.string.settings_language_hint) {
                startActivity(Intent(this, LanguageActivity::class.java))
            }
        )
    }

    // ---- البناء من المخطط -------------------------------------------------------------------

    private fun render() {
        container.removeAllViews()
        for (row in schema) {
            container.addView(
                when (row) {
                    is Row.Section -> bindSection(row)
                    is Row.Toggle -> bindToggle(row)
                    is Row.Choice -> bindChoice(row)
                    is Row.FontSize -> bindFontSize()
                    is Row.Link -> bindLink(row)
                }
            )
        }
    }

    private fun inflateRow(layout: Int): View = layoutInflater.inflate(layout, container, false)

    private fun bindSection(row: Row.Section): View =
        (inflateRow(R.layout.item_setting_section) as TextView).apply { setText(row.title) }

    private fun bindToggle(row: Row.Toggle): View {
        val view = inflateRow(R.layout.item_setting_switch)
        view.findViewById<TextView>(R.id.settingTitle).setText(row.title)
        view.findViewById<TextView>(R.id.settingHint).setText(row.hint)
        val toggle = view.findViewById<Switch>(R.id.settingSwitch)
        toggle.isChecked = AppSettings.getBoolean(this, row.key)
        // الصف كله قابل للنقر (هدف لمس أكبر)؛ المفتاح نفسه غير قابل للنقر مباشرة.
        view.setOnClickListener {
            toggle.toggle()
            AppSettings.setBoolean(this, row.key, toggle.isChecked)
        }
        return view
    }

    private fun bindChoice(row: Row.Choice): View {
        val view = inflateRow(R.layout.item_setting_choice)
        view.findViewById<TextView>(R.id.settingTitle).setText(row.title)
        val hint = view.findViewById<TextView>(R.id.settingHint)
        if (row.hint != null) hint.setText(row.hint) else hint.visibility = View.GONE

        val optionsBar = view.findViewById<LinearLayout>(R.id.settingOptions)
        // Segmented control: كل خيار TextView بخلفية/لون نص/سماكة تتبدّل بين bg_segment_selected
        // (بنفسجي ممتلئ + نص أبيض عريض) وbg_segment_unselected (رمادي محايد + نص خافت) بدل
        // إبقاء لون/سماكة النص ثابتين وتبديل الخلفية بس — كانت الحالة المختارة قبل هيك ما
        // تبيّن بوضوح كافي.
        val buttons = row.options.map { option ->
            TextView(this).apply {
                text = option.labelText ?: getString(option.labelRes)
                gravity = Gravity.CENTER
                textSize = 12.5f
            }
        }
        fun refresh() {
            val current = row.read()
            buttons.forEachIndexed { index, button ->
                val selected = row.options[index].value == current
                button.setBackgroundResource(if (selected) R.drawable.bg_segment_selected else R.drawable.bg_segment_unselected)
                button.setTextColor(
                    ContextCompat.getColor(
                        this@SettingsActivity,
                        if (selected) R.color.rin_segment_selected_text else R.color.rin_segment_unselected_text
                    )
                )
                button.setTypeface(button.typeface, if (selected) android.graphics.Typeface.BOLD else android.graphics.Typeface.NORMAL)
            }
        }
        buttons.forEachIndexed { index, button ->
            val params = LinearLayout.LayoutParams(0, dp(40), 1f)
            if (index > 0) params.marginStart = dp(4)
            optionsBar.addView(button, params)
            button.setOnClickListener {
                row.write(row.options[index].value)
                refresh()
                row.onChanged?.invoke()
            }
        }
        refresh()
        return view
    }

    private fun bindFontSize(): View {
        val view = inflateRow(R.layout.item_setting_slider)
        val title = view.findViewById<TextView>(R.id.settingTitle)
        val seek = view.findViewById<SeekBar>(R.id.settingSeek)
        val min = AppSettings.MIN_FONT_SIZE_SP
        val current = AppSettings.getEditorFontSizeSp(this)
        seek.max = (AppSettings.MAX_FONT_SIZE_SP - min).toInt()
        seek.progress = (current - min).toInt()
        title.text = getString(R.string.settings_font_size_label, current)
        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                val sp = min + progress
                title.text = getString(R.string.settings_font_size_label, sp)
                if (fromUser) AppSettings.setEditorFontSizeSp(this@SettingsActivity, sp)
            }
            override fun onStartTrackingTouch(bar: SeekBar?) {}
            override fun onStopTrackingTouch(bar: SeekBar?) {}
        })
        return view
    }

    private fun bindLink(row: Row.Link): View {
        val view = inflateRow(R.layout.item_setting_link)
        view.findViewById<TextView>(R.id.settingIcon).text = row.icon
        view.findViewById<TextView>(R.id.settingTitle).setText(row.title)
        view.findViewById<TextView>(R.id.settingHint).setText(row.hint)
        view.setOnClickListener { row.onClick() }
        return view
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density).roundToInt()
}

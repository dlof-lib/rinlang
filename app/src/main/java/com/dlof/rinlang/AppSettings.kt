package com.dlof.rinlang

import android.content.Context

/** Persistent editor/application preferences. */
object AppSettings {
    const val MIN_FONT_SIZE_SP = 10f
    const val MAX_FONT_SIZE_SP = 22f
    const val DEFAULT_FONT_SIZE_SP = 14f
    const val DEFAULT_SHOW_LINE_NUMBERS = true

    private const val PREFS_NAME = "rin_app_settings"
    private const val KEY_FONT_SIZE = "editor_font_size_sp"
    private const val KEY_SHOW_LINE_NUMBERS = "show_line_numbers"
    private const val KEY_SYNTAX = "syntax_highlighting"
    private const val KEY_BRACKETS = "bracket_matching"
    private const val KEY_AUTOCOMPLETE = "autocomplete"
    private const val KEY_AUTO_CLOSE = "auto_close_brackets"
    private const val KEY_AUTO_INDENT = "auto_indent"
    private const val KEY_WORD_WRAP = "word_wrap"
    private const val KEY_WHITESPACE = "show_whitespace"
    private const val KEY_CURRENT_LINE = "current_line_highlight"
    private const val KEY_SMOOTH_CURSOR = "smooth_cursor"
    private const val KEY_MINIMAP = "minimap"
    private const val KEY_TAB_SIZE = "tab_size"
    private const val KEY_HAPTIC = "haptic_feedback"
    private const val KEY_SAVE_PAUSE = "save_on_pause"
    private const val KEY_CONFIRM_EXIT = "confirm_exit"
    private const val KEY_FORMAT_SAVE = "format_on_save"
    private const val KEY_THEME_MODE = "theme_mode"
    private const val KEY_EDITOR_LAYOUT = "editor_layout"
    private const val KEY_PROJECT_SORT = "project_sort"
    private const val KEY_SHOW_CONSOLE = "show_console"
    private const val KEY_SHOW_TOOLBAR = "show_toolbar"

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun getEditorFontSizeSp(context: Context) = prefs(context).getFloat(KEY_FONT_SIZE, DEFAULT_FONT_SIZE_SP).coerceIn(MIN_FONT_SIZE_SP, MAX_FONT_SIZE_SP)
    fun setEditorFontSizeSp(context: Context, value: Float) = prefs(context).edit().putFloat(KEY_FONT_SIZE, value.coerceIn(MIN_FONT_SIZE_SP, MAX_FONT_SIZE_SP)).apply()
    fun getShowLineNumbers(context: Context) = prefs(context).getBoolean(KEY_SHOW_LINE_NUMBERS, DEFAULT_SHOW_LINE_NUMBERS)
    fun setShowLineNumbers(context: Context, value: Boolean) = prefs(context).edit().putBoolean(KEY_SHOW_LINE_NUMBERS, value).apply()

    fun isSyntaxHighlighting(context: Context) = prefs(context).getBoolean(KEY_SYNTAX, true)
    fun setSyntaxHighlighting(context: Context, v: Boolean) = put(context, KEY_SYNTAX, v)
    fun isBracketMatching(context: Context) = prefs(context).getBoolean(KEY_BRACKETS, true)
    fun setBracketMatching(context: Context, v: Boolean) = put(context, KEY_BRACKETS, v)
    fun isAutocomplete(context: Context) = prefs(context).getBoolean(KEY_AUTOCOMPLETE, true)
    fun setAutocomplete(context: Context, v: Boolean) = put(context, KEY_AUTOCOMPLETE, v)
    fun isAutoCloseBrackets(context: Context) = prefs(context).getBoolean(KEY_AUTO_CLOSE, true)
    fun setAutoCloseBrackets(context: Context, v: Boolean) = put(context, KEY_AUTO_CLOSE, v)
    fun isAutoIndent(context: Context) = prefs(context).getBoolean(KEY_AUTO_INDENT, true)
    fun setAutoIndent(context: Context, v: Boolean) = put(context, KEY_AUTO_INDENT, v)
    fun isWordWrap(context: Context) = prefs(context).getBoolean(KEY_WORD_WRAP, false)
    fun setWordWrap(context: Context, v: Boolean) = put(context, KEY_WORD_WRAP, v)
    fun isShowWhitespace(context: Context) = prefs(context).getBoolean(KEY_WHITESPACE, false)
    fun setShowWhitespace(context: Context, v: Boolean) = put(context, KEY_WHITESPACE, v)
    fun isCurrentLineHighlight(context: Context) = prefs(context).getBoolean(KEY_CURRENT_LINE, true)
    fun setCurrentLineHighlight(context: Context, v: Boolean) = put(context, KEY_CURRENT_LINE, v)
    fun isSmoothCursor(context: Context) = prefs(context).getBoolean(KEY_SMOOTH_CURSOR, true)
    fun setSmoothCursor(context: Context, v: Boolean) = put(context, KEY_SMOOTH_CURSOR, v)
    fun isMinimap(context: Context) = prefs(context).getBoolean(KEY_MINIMAP, false)
    fun setMinimap(context: Context, v: Boolean) = put(context, KEY_MINIMAP, v)
    fun getTabSize(context: Context) = prefs(context).getInt(KEY_TAB_SIZE, 4).coerceIn(2, 8)
    fun setTabSize(context: Context, v: Int) = prefs(context).edit().putInt(KEY_TAB_SIZE, v.coerceIn(2, 8)).apply()
    fun isHapticFeedback(context: Context) = prefs(context).getBoolean(KEY_HAPTIC, true)
    fun setHapticFeedback(context: Context, v: Boolean) = put(context, KEY_HAPTIC, v)
    fun isSaveOnPause(context: Context) = prefs(context).getBoolean(KEY_SAVE_PAUSE, true)
    fun setSaveOnPause(context: Context, v: Boolean) = put(context, KEY_SAVE_PAUSE, v)
    fun isConfirmExit(context: Context) = prefs(context).getBoolean(KEY_CONFIRM_EXIT, false)
    fun setConfirmExit(context: Context, v: Boolean) = put(context, KEY_CONFIRM_EXIT, v)
    fun isFormatOnSave(context: Context) = prefs(context).getBoolean(KEY_FORMAT_SAVE, false)
    fun setFormatOnSave(context: Context, v: Boolean) = put(context, KEY_FORMAT_SAVE, v)

    fun getThemeMode(context: Context) = prefs(context).getString(KEY_THEME_MODE, "system") ?: "system"
    fun setThemeMode(context: Context, value: String) = prefs(context).edit().putString(KEY_THEME_MODE, value).apply()
    fun getEditorLayout(context: Context) = prefs(context).getString(KEY_EDITOR_LAYOUT, "standard") ?: "standard"
    fun setEditorLayout(context: Context, value: String) = prefs(context).edit().putString(KEY_EDITOR_LAYOUT, value).apply()
    fun getProjectSort(context: Context) = prefs(context).getString(KEY_PROJECT_SORT, "recent") ?: "recent"
    fun setProjectSort(context: Context, value: String) = prefs(context).edit().putString(KEY_PROJECT_SORT, value).apply()
    fun isShowConsole(context: Context) = prefs(context).getBoolean(KEY_SHOW_CONSOLE, true)
    fun setShowConsole(context: Context, v: Boolean) = put(context, KEY_SHOW_CONSOLE, v)
    fun isShowToolbar(context: Context) = prefs(context).getBoolean(KEY_SHOW_TOOLBAR, true)
    fun setShowToolbar(context: Context, v: Boolean) = put(context, KEY_SHOW_TOOLBAR, v)

    private fun put(context: Context, key: String, value: Boolean) = prefs(context).edit().putBoolean(key, value).apply()

    fun resetToDefaults(context: Context) {
        prefs(context).edit().clear()
            .putFloat(KEY_FONT_SIZE, DEFAULT_FONT_SIZE_SP)
            .putBoolean(KEY_SHOW_LINE_NUMBERS, DEFAULT_SHOW_LINE_NUMBERS)
            .putBoolean(KEY_SYNTAX, true)
            .putBoolean(KEY_BRACKETS, true)
            .putBoolean(KEY_AUTOCOMPLETE, true)
            .putBoolean(KEY_AUTO_CLOSE, true)
            .putBoolean(KEY_AUTO_INDENT, true)
            .putBoolean(KEY_WORD_WRAP, false)
            .putBoolean(KEY_WHITESPACE, false)
            .putBoolean(KEY_CURRENT_LINE, true)
            .putBoolean(KEY_SMOOTH_CURSOR, true)
            .putBoolean(KEY_MINIMAP, false)
            .putInt(KEY_TAB_SIZE, 4)
            .putBoolean(KEY_HAPTIC, true)
            .putBoolean(KEY_SAVE_PAUSE, true)
            .putBoolean(KEY_CONFIRM_EXIT, false)
            .putBoolean(KEY_FORMAT_SAVE, false)
            .putString(KEY_THEME_MODE, "system")
            .putString(KEY_EDITOR_LAYOUT, "standard")
            .putString(KEY_PROJECT_SORT, "recent")
            .putBoolean(KEY_SHOW_CONSOLE, true)
            .putBoolean(KEY_SHOW_TOOLBAR, true)
            .apply()
    }
}

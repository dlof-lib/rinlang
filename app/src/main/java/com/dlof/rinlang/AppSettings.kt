package com.dlof.rinlang

import android.content.Context

/**
 * تفضيلات المحرر والتطبيق المحفوظة.
 *
 * مصدر واحد للحقيقة: كل مفتاح منطقي يُعرَّف مرة واحدة في [Key]، وقيمته الافتراضية مرة واحدة في
 * [BOOL_DEFAULTS] (أو ثابت مسمّى لغير المنطقي). لذلك [resetToDefaults] مجرد مسح للتفضيلات —
 * غياب المفتاح يعني القيمة الافتراضية — بدل إعادة سرد كل قيمة يدويًا (وهو ما كان يتفرّع عن
 * القيم الفعلية كلما أُضيف إعداد جديد).
 *
 * كل إعداد هنا له مستهلِك حقيقي في الكود؛ لا تُضِف مفتاحًا لا يقرؤه أحد.
 */
object AppSettings {
    const val MIN_FONT_SIZE_SP = 10f
    const val MAX_FONT_SIZE_SP = 22f
    // خُفِّض الحجم الافتراضي من 17f إلى 13f لمظهر محرر أكثر إحكامًا (أسطر كود أكثر ظهورًا في الشاشة).
    const val DEFAULT_FONT_SIZE_SP = 13f
    const val DEFAULT_SHOW_LINE_NUMBERS = true

    const val DEFAULT_TAB_SIZE = 4
    const val MIN_TAB_SIZE = 2
    const val MAX_TAB_SIZE = 8

    /** قيم "تخطيط المحرر" المسموحة (تُخزَّن كنص). */
    const val LAYOUT_STANDARD = "standard"
    const val LAYOUT_FOCUS = "focus"
    const val LAYOUT_COMPACT = "compact"

    /** قيم ترتيب المشاريع. */
    const val SORT_RECENT = "recent"
    const val SORT_NAME = "name"
    const val SORT_TYPE = "type"

    /** الحفظ التلقائي بعد التوقف عن الكتابة، بالميلي ثانية؛ 0 = معطّل. */
    val AUTO_SAVE_DELAYS_MS = listOf(0, 2_000, 5_000, 10_000)

    /** مفاتيح التخزين — عامة كي تبني شاشة الإعدادات صفوفها منها مباشرة. */
    object Key {
        const val FONT_SIZE = "editor_font_size_sp"
        const val LINE_NUMBERS = "show_line_numbers"
        const val TAB_SIZE = "tab_size"
        const val SHOW_WHITESPACE = "show_whitespace"
        const val CURRENT_LINE = "current_line_highlight"
        const val SMOOTH_CURSOR = "smooth_cursor"

        const val SYNTAX = "syntax_highlighting"
        const val BRACKETS = "bracket_matching"
        const val LIVE_DIAGNOSTICS = "live_diagnostics"
        const val AUTOCOMPLETE = "autocomplete"
        const val AUTO_CLOSE = "auto_close_brackets"
        const val AUTO_INDENT = "auto_indent"

        const val SAVE_ON_PAUSE = "save_on_pause"
        const val AUTO_SAVE_DELAY = "auto_save_delay_ms"
        const val CONFIRM_EXIT = "confirm_exit"
        const val FORMAT_ON_SAVE = "format_on_save"
        const val TRIM_TRAILING = "trim_trailing_whitespace"
        const val FINAL_NEWLINE = "ensure_final_newline"

        const val AUTO_RUN = "auto_run_on_type"
        const val CLEAR_CONSOLE_ON_RUN = "clear_console_on_run"

        const val THEME_MODE = "theme_mode"
        const val EDITOR_LAYOUT = "editor_layout"
        const val SHOW_TOOLBAR = "show_toolbar"
        const val SHOW_CONSOLE = "show_console"
        const val KEEP_SCREEN_ON = "keep_screen_on"
        const val HAPTIC = "haptic_feedback"

        const val PROJECT_SORT = "project_sort"
    }

    /** القيمة الافتراضية لكل إعداد منطقي. أي مفتاح منطقي غير مذكور هنا افتراضيه false. */
    private val BOOL_DEFAULTS: Map<String, Boolean> = mapOf(
        Key.LINE_NUMBERS to DEFAULT_SHOW_LINE_NUMBERS,
        Key.SHOW_WHITESPACE to false,
        Key.CURRENT_LINE to true,
        Key.SMOOTH_CURSOR to true,
        Key.SYNTAX to true,
        Key.BRACKETS to true,
        Key.LIVE_DIAGNOSTICS to true,
        Key.AUTOCOMPLETE to true,
        Key.AUTO_CLOSE to true,
        Key.AUTO_INDENT to true,
        Key.SAVE_ON_PAUSE to true,
        Key.CONFIRM_EXIT to false,
        Key.FORMAT_ON_SAVE to false,
        Key.TRIM_TRAILING to false,
        Key.FINAL_NEWLINE to false,
        // تشغيل تلقائي: معطّل افتراضيًا حتى لا يمتلئ طابور العمل بمحاولات تشغيل لم يطلبها المستخدم.
        Key.AUTO_RUN to false,
        Key.CLEAR_CONSOLE_ON_RUN to false,
        Key.SHOW_TOOLBAR to true,
        Key.SHOW_CONSOLE to true,
        Key.KEEP_SCREEN_ON to false,
        Key.HAPTIC to true
    )

    private const val PREFS_NAME = "rin_app_settings"

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    // ---- وصول عام بالمفتاح (تستعمله شاشة الإعدادات) ----------------------------------------

    fun getBoolean(context: Context, key: String): Boolean =
        prefs(context).getBoolean(key, BOOL_DEFAULTS[key] ?: false)

    fun setBoolean(context: Context, key: String, value: Boolean) {
        prefs(context).edit().putBoolean(key, value).apply()
    }

    private fun getString(context: Context, key: String, default: String, allowed: Set<String>): String {
        val stored = prefs(context).getString(key, default) ?: default
        return if (stored in allowed) stored else default
    }

    private fun setString(context: Context, key: String, value: String) {
        prefs(context).edit().putString(key, value).apply()
    }

    // ---- المحرر ---------------------------------------------------------------------------

    fun getEditorFontSizeSp(context: Context): Float =
        prefs(context).getFloat(Key.FONT_SIZE, DEFAULT_FONT_SIZE_SP).coerceIn(MIN_FONT_SIZE_SP, MAX_FONT_SIZE_SP)

    fun setEditorFontSizeSp(context: Context, value: Float) {
        prefs(context).edit().putFloat(Key.FONT_SIZE, value.coerceIn(MIN_FONT_SIZE_SP, MAX_FONT_SIZE_SP)).apply()
    }

    fun getShowLineNumbers(context: Context) = getBoolean(context, Key.LINE_NUMBERS)
    fun setShowLineNumbers(context: Context, value: Boolean) = setBoolean(context, Key.LINE_NUMBERS, value)

    fun getTabSize(context: Context): Int =
        prefs(context).getInt(Key.TAB_SIZE, DEFAULT_TAB_SIZE).coerceIn(MIN_TAB_SIZE, MAX_TAB_SIZE)

    fun setTabSize(context: Context, value: Int) {
        prefs(context).edit().putInt(Key.TAB_SIZE, value.coerceIn(MIN_TAB_SIZE, MAX_TAB_SIZE)).apply()
    }

    fun isShowWhitespace(context: Context) = getBoolean(context, Key.SHOW_WHITESPACE)
    fun isCurrentLineHighlight(context: Context) = getBoolean(context, Key.CURRENT_LINE)
    fun isSmoothCursor(context: Context) = getBoolean(context, Key.SMOOTH_CURSOR)

    // ---- المساعدة أثناء الكتابة -------------------------------------------------------------

    fun isSyntaxHighlighting(context: Context) = getBoolean(context, Key.SYNTAX)
    fun isBracketMatching(context: Context) = getBoolean(context, Key.BRACKETS)
    /** تشخيص أخطاء الصياغة الحي: خط متعرّج تحت الأخطاء/التحذيرات أثناء الكتابة (rin::Lexer + rin::Parser). */
    fun isLiveDiagnostics(context: Context) = getBoolean(context, Key.LIVE_DIAGNOSTICS)
    fun isAutocomplete(context: Context) = getBoolean(context, Key.AUTOCOMPLETE)
    fun isAutoCloseBrackets(context: Context) = getBoolean(context, Key.AUTO_CLOSE)
    fun isAutoIndent(context: Context) = getBoolean(context, Key.AUTO_INDENT)

    // ---- الحفظ ----------------------------------------------------------------------------

    fun isSaveOnPause(context: Context) = getBoolean(context, Key.SAVE_ON_PAUSE)
    fun isConfirmExit(context: Context) = getBoolean(context, Key.CONFIRM_EXIT)
    fun isFormatOnSave(context: Context) = getBoolean(context, Key.FORMAT_ON_SAVE)
    fun isTrimTrailingWhitespace(context: Context) = getBoolean(context, Key.TRIM_TRAILING)
    fun isEnsureFinalNewline(context: Context) = getBoolean(context, Key.FINAL_NEWLINE)

    fun getAutoSaveDelayMs(context: Context): Int {
        val stored = prefs(context).getInt(Key.AUTO_SAVE_DELAY, 0)
        return if (stored in AUTO_SAVE_DELAYS_MS) stored else 0
    }

    fun setAutoSaveDelayMs(context: Context, value: Int) {
        prefs(context).edit().putInt(Key.AUTO_SAVE_DELAY, if (value in AUTO_SAVE_DELAYS_MS) value else 0).apply()
    }

    /** خيارات تحويل النص عند الحفظ مجمَّعة من الإعدادات الحالية. */
    fun saveOptions(context: Context) = EditorTextTransforms.SaveOptions(
        format = isFormatOnSave(context),
        trimTrailingWhitespace = isTrimTrailingWhitespace(context),
        ensureFinalNewline = isEnsureFinalNewline(context),
        tabSize = getTabSize(context)
    )

    // ---- التشغيل --------------------------------------------------------------------------

    /** تشغيل تلقائي (live output): يُعيد تنفيذ الكود في الطابور بعد توقّف قصير عن الكتابة. */
    fun isAutoRunEnabled(context: Context) = getBoolean(context, Key.AUTO_RUN)
    fun isClearConsoleOnRun(context: Context) = getBoolean(context, Key.CLEAR_CONSOLE_ON_RUN)

    // ---- الواجهة --------------------------------------------------------------------------

    fun getThemeMode(context: Context) = getString(context, Key.THEME_MODE, ThemeManager.SYSTEM, ThemeManager.MODES)
    fun setThemeMode(context: Context, value: String) = setString(context, Key.THEME_MODE, value)

    fun getEditorLayout(context: Context) =
        getString(context, Key.EDITOR_LAYOUT, LAYOUT_STANDARD, setOf(LAYOUT_STANDARD, LAYOUT_FOCUS, LAYOUT_COMPACT))
    fun setEditorLayout(context: Context, value: String) = setString(context, Key.EDITOR_LAYOUT, value)

    fun isShowToolbar(context: Context) = getBoolean(context, Key.SHOW_TOOLBAR)
    fun isShowConsole(context: Context) = getBoolean(context, Key.SHOW_CONSOLE)
    fun isKeepScreenOn(context: Context) = getBoolean(context, Key.KEEP_SCREEN_ON)
    fun isHapticFeedback(context: Context) = getBoolean(context, Key.HAPTIC)

    // ---- المشاريع -------------------------------------------------------------------------

    fun getProjectSort(context: Context) =
        getString(context, Key.PROJECT_SORT, SORT_RECENT, setOf(SORT_RECENT, SORT_NAME, SORT_TYPE))
    fun setProjectSort(context: Context, value: String) = setString(context, Key.PROJECT_SORT, value)

    /** يمسح كل التفضيلات فتعود جميعها إلى قيمها الافتراضية (غياب المفتاح = الافتراضي). */
    fun resetToDefaults(context: Context) {
        prefs(context).edit().clear().apply()
    }
}

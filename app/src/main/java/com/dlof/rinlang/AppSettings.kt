package com.dlof.rinlang

import android.content.Context

/**
 * تفضيلات التطبيق العامة (لا علاقة لها بحساب المستخدم)، تُقرأ في [MainActivity] عند فتح
 * المحرر وتُكتب من [SettingsActivity]. نفس مدى حجم الخط المستخدم أصلاً في تكبير/تصغير
 * الخط داخل المحرر (10sp إلى 22sp) حتى تبقى القيمة المحفوظة متوافقة دائماً معه.
 */
object AppSettings {

    const val MIN_FONT_SIZE_SP = 10f
    const val MAX_FONT_SIZE_SP = 22f
    const val DEFAULT_FONT_SIZE_SP = 14f
    const val DEFAULT_SHOW_LINE_NUMBERS = true

    private const val PREFS_NAME = "rin_app_settings"
    private const val KEY_FONT_SIZE = "editor_font_size_sp"
    private const val KEY_SHOW_LINE_NUMBERS = "show_line_numbers"

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    fun getEditorFontSizeSp(context: Context): Float =
        prefs(context).getFloat(KEY_FONT_SIZE, DEFAULT_FONT_SIZE_SP)
            .coerceIn(MIN_FONT_SIZE_SP, MAX_FONT_SIZE_SP)

    fun setEditorFontSizeSp(context: Context, sizeSp: Float) {
        prefs(context).edit()
            .putFloat(KEY_FONT_SIZE, sizeSp.coerceIn(MIN_FONT_SIZE_SP, MAX_FONT_SIZE_SP))
            .apply()
    }

    fun getShowLineNumbers(context: Context): Boolean =
        prefs(context).getBoolean(KEY_SHOW_LINE_NUMBERS, DEFAULT_SHOW_LINE_NUMBERS)

    fun setShowLineNumbers(context: Context, show: Boolean) {
        prefs(context).edit().putBoolean(KEY_SHOW_LINE_NUMBERS, show).apply()
    }

    fun resetToDefaults(context: Context) {
        prefs(context).edit()
            .putFloat(KEY_FONT_SIZE, DEFAULT_FONT_SIZE_SP)
            .putBoolean(KEY_SHOW_LINE_NUMBERS, DEFAULT_SHOW_LINE_NUMBERS)
            .apply()
    }
}

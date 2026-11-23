package com.dlof.rinlang

import android.content.Context

/**
 * تخزين تفضيلات "تصميم المحرر" و"حجم الصفحة" و"التنسيق" — طبقة أندرويد رقيقة فوق [EditorTheme].
 *
 * منفصل عن [AppSettings] عمداً: هذه التفضيلات بيانات تصميم (ألوان + دليل صفحة) وليست مفاتيح مشغّلات،
 * فلا يلزم لها تعديل جدول القيم الافتراضية هناك. غياب أي مفتاح = السلوك الأصلي للتطبيق.
 */
object EditorDesign {
    private const val PREFS = "rin_editor_design"
    private const val K_COLORS = "colors"
    private const val K_PRESET = "preset"
    private const val K_GUIDE = "page_guide_col"
    private const val K_SORT_IMPORTS = "format_sort_imports"
    private const val K_INDENT_TABS = "format_indent_tabs"
    private const val K_COLOR_PREVIEW = "color_preview_mode"
    private const val K_SURROUND = "surround_on_type"

    /** الأعمدة المسموحة لدليل الصفحة؛ 0 = بلا دليل. */
    val GUIDE_COLUMNS = listOf(0, 72, 80, 100, 120)

    /** معرّف الثيم الافتراضي (ألوان التطبيق الأصلية، يتبدّل مع الوضع الليلي). */
    const val PRESET_DEFAULT = "default"
    const val PRESET_CUSTOM = "custom"

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    // ---- الألوان ------------------------------------------------------------------------------

    /** تجاوزات الألوان الحالية (فارغة = الألوان الأصلية). */
    fun colors(context: Context): LinkedHashMap<String, Int> =
        EditorTheme.parse(prefs(context).getString(K_COLORS, ""))

    /** المعرّف الحالي: [PRESET_DEFAULT] أو id ثيم جاهز أو [PRESET_CUSTOM] بعد تعديل يدوي. */
    fun presetId(context: Context): String = prefs(context).getString(K_PRESET, PRESET_DEFAULT) ?: PRESET_DEFAULT

    fun save(context: Context, colors: Map<String, Int>, presetId: String) {
        prefs(context).edit()
            .putString(K_COLORS, EditorTheme.serialize(colors))
            .putString(K_PRESET, presetId)
            .apply()
    }

    fun resetColors(context: Context) = save(context, emptyMap(), PRESET_DEFAULT)

    /** لقطة نصية للحالة الحالية (لزر "إلغاء" في المصمّم). */
    fun snapshot(context: Context): Pair<String, String> =
        (prefs(context).getString(K_COLORS, "") ?: "") to presetId(context)

    fun restore(context: Context, snap: Pair<String, String>) {
        prefs(context).edit().putString(K_COLORS, snap.first).putString(K_PRESET, snap.second).apply()
    }

    // ---- حجم الصفحة ---------------------------------------------------------------------------

    fun guideColumn(context: Context): Int {
        val v = prefs(context).getInt(K_GUIDE, 0)
        return if (v in GUIDE_COLUMNS) v else 0
    }

    fun setGuideColumn(context: Context, value: Int) {
        prefs(context).edit().putInt(K_GUIDE, if (value in GUIDE_COLUMNS) value else 0).apply()
    }

    // ---- التنسيق ------------------------------------------------------------------------------

    fun isSortImports(context: Context) = prefs(context).getBoolean(K_SORT_IMPORTS, false)
    fun setSortImports(context: Context, value: Boolean) = prefs(context).edit().putBoolean(K_SORT_IMPORTS, value).apply()

    fun isIndentWithTabs(context: Context) = prefs(context).getBoolean(K_INDENT_TABS, false)
    fun setIndentWithTabs(context: Context, value: Boolean) = prefs(context).edit().putBoolean(K_INDENT_TABS, value).apply()

    // ---- معاينة الألوان والكتابة الذكية ---------------------------------------------------------

    /** وضع معاينة الألوان: 0 إيقاف، 1 مربع لون بجانب الكود (الافتراضي)، 2 تظليل الكود بلونه. */
    fun colorPreviewMode(context: Context): Int = prefs(context).getInt(K_COLOR_PREVIEW, 1).coerceIn(0, 2)
    fun setColorPreviewMode(context: Context, mode: Int) = prefs(context).edit().putInt(K_COLOR_PREVIEW, mode.coerceIn(0, 2)).apply()

    /** كتابة قوس/اقتباس فوق تحديد يحيط التحديد به بدل استبداله. */
    fun isSurroundOnType(context: Context) = prefs(context).getBoolean(K_SURROUND, true)
    fun setSurroundOnType(context: Context, value: Boolean) = prefs(context).edit().putBoolean(K_SURROUND, value).apply()

    /** خيارات [CodeFormatter] من الإعدادات الحالية (عرض الإزاحة من إعداد حجم التبويب). */
    fun formatOptions(context: Context) = CodeFormatter.Options(
        indent = AppSettings.getTabSize(context),
        useTabs = isIndentWithTabs(context),
        sortImports = isSortImports(context)
    )
}

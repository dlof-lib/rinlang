package com.dlof.rinlang

/**
 * Cheap static gate used only by the editor to decide whether the single official Indsin live
 * preview should be opened. It recognizes both legacy @view roots and the current @loop canvas
 * roots, including the shorthand forms (@loop=name / @view=name). The actual source is ALWAYS
 * parsed and executed by the native Rin/Indsin runtime after this gate; this is never a second parser.
 */
object IndsinViewTracer {

    private val rootRegex = Regex(
        """@(?:view|loop)(?:\.[A-Za-z_][A-Za-z0-9_]*)?\s*=\s*[A-Za-z_][A-Za-z0-9_]*"""
    )

    /**
     * صيغة الوسوم بالأقواس (docs/indsin_tags.md): سطر يبدأ بوسم واجهة معروف، مثل `<Screen#app size=phone>` أو `<Column#root>`.
     * تُشترط بداية السطر كي لا تُلتقط نصوص HTML/سلاسل داخل برامج Rin العادية.
     */
    private val tagRootRegex = Regex(
        """(?m)^[ \t]*<(?:Screen|Column|Row|Stack|Card|Grid|Scroll|Wrap|Text|Button|Input|Hero|Section|LoginForm|Field|PasswordField|Tabs|Stepper|ProfileCard|ConfirmCard|Stat|Alert|ListTile|EmptyState)(?:#[A-Za-z_][A-Za-z0-9_]*)?(?=[\s/>])"""
    )

    /** True if [source] declares a Indsin root: @view... / @loop... or a tag-syntax root (`<Screen>`, `<Column>`…). */
    fun containsView(source: String): Boolean = rootRegex.containsMatchIn(source) || tagRootRegex.containsMatchIn(source)
}

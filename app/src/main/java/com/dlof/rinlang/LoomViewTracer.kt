package com.dlof.rinlang

/**
 * Cheap static gate used only by the editor to decide whether the single official Loom live
 * preview should be opened. It recognizes both legacy @view roots and the current @loop canvas
 * roots, including the shorthand forms (@loop=name / @view=name). The actual source is ALWAYS
 * parsed and executed by the native Rin/Loom runtime after this gate; this is never a second parser.
 */
object LoomViewTracer {

    private val rootRegex = Regex(
        """@(?:view|loop)(?:\.[A-Za-z_][A-Za-z0-9_]*)?\s*=\s*[A-Za-z_][A-Za-z0-9_]*"""
    )

    /** True if [source] declares a Loom root: @view... or @loop.... */
    fun containsView(source: String): Boolean = rootRegex.containsMatchIn(source)
}

package com.dlof.rinlang

/**
 * Cheap static check (no engine execution) used by the editor to know whether the current
 * source contains a Loomtime UI tree (`@view.<Kind>=name ... .end/view`) worth rendering in the
 * Live Preview screen ([LoomPreviewActivity]). Mirrors [PipelineTracer.containsPipeline]'s role
 * for `@container.pipe` blocks — a fast regex probe the UI thread can call on every keystroke
 * without touching the native engine.
 */
object LoomViewTracer {

    private val viewBlockRegex = Regex("""@view\.[A-Za-z_][A-Za-z0-9_]*\s*=\s*[A-Za-z_][A-Za-z0-9_]*""")

    /** True if [source] declares at least one `@view.<Kind>=name` root. */
    fun containsView(source: String): Boolean = viewBlockRegex.containsMatchIn(source)
}

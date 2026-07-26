package com.dlof.rinlang

/**
 * Turns a `@container.pipe = name ... .end/container.pipe` block into a
 * step-by-step trace of *real* values.
 *
 * This does not simulate or fabricate the numbers shown in the diagram: it
 * re-runs the actual source through [RinEngine] (the native C++ interpreter)
 * once per pipeline stage, asking it to `print` the value of the pipeline
 * expression truncated at that stage. Because every stage reuses the exact
 * same `let`/`fun` declarations from the user's own code, every value shown
 * in the resulting [PipelineTrace] is something the real engine computed —
 * only the aggregation/visualization is done on the Kotlin side.
 */
object PipelineTracer {

    private const val MARK = "@@RINPIPE_TRACE@@" // improbable-to-collide marker prefix

    private val blockRegex = Regex(
        """@container\.pipe\s*=\s*([A-Za-z_][A-Za-z0-9_]*)([\s\S]*?)\.end/container\.pipe"""
    )
    private val pipeAssignRegex = Regex(
        """let\s+[A-Za-z_][A-Za-z0-9_]*\s*=\s*([A-Za-z_][A-Za-z0-9_]*(?:\s*\|>\s*[A-Za-z_][A-Za-z0-9_]*(?:\s*\([^)]*\))?)+)\s*;"""
    )
    private val callNameRegex = Regex("""^([A-Za-z_][A-Za-z0-9_]*)\s*\(""")

    data class Stage(
        val label: String,       // function name used as the step's title, e.g. "transform"
        val call: String,        // the raw call text, e.g. "transform()"
        var valueText: String = "",
        var ok: Boolean = true
    )

    data class PipelineTrace(
        val containerName: String,
        val sourceExpr: String,
        var sourceValueText: String = "",
        val stages: MutableList<Stage> = mutableListOf(),
        var finalValueText: String = "",
        var success: Boolean = true,
        var errorMessage: String? = null,
        var rawEngineOutput: String = ""
    )

    /** Cheap static check (no engine execution) used by the editor to offer a "view in RinFlow" prompt. */
    fun containsPipeline(source: String): Boolean = blockRegex.containsMatchIn(source)

    /** Returns null if [source] contains no `@container.pipe` block. */
    fun findPipeline(source: String): PipelineTrace? {
        val block = blockRegex.find(source) ?: return null
        val containerName = block.groupValues[1]
        val body = block.groupValues[2]

        val assign = pipeAssignRegex.find(body)
            ?: return PipelineTrace(
                containerName = containerName,
                sourceExpr = "",
                success = false,
                errorMessage = "No `let x = source |> step() |> step();` pipeline expression found inside this container.pipe block."
            )

        val chain = assign.groupValues[1].split("|>").map { it.trim() }
        val sourceExpr = chain.first()
        val stages = chain.drop(1).map { call ->
            val name = callNameRegex.find(call)?.groupValues?.get(1) ?: call
            Stage(label = name, call = call)
        }.toMutableList()

        val trace = PipelineTrace(containerName = containerName, sourceExpr = sourceExpr, stages = stages)

        // Build one program that keeps the user's original body intact (so every
        // `let`/`fun` it declares is still in scope) and appends real `print`
        // statements — one per pipeline prefix — right before the block closes.
        val probe = StringBuilder()
        probe.append("@container.pipe=").append(containerName).append('\n')
        probe.append(body)
        probe.append('\n')
        probe.append("print \"").append(MARK).append("SRC\";\n")
        probe.append("print ").append(sourceExpr).append(";\n")
        var running = sourceExpr
        for (stage in stages) {
            running = "$running |> ${stage.call}"
            probe.append("print \"").append(MARK).append("STEP\";\n")
            probe.append("print ").append(running).append(";\n")
        }
        probe.append(".end/container.pipe\n")

        val engineOutput = try {
            RinEngine.runSource(probe.toString())
        } catch (t: Throwable) {
            "[Internal error]: ${t.message}"
        }
        trace.rawEngineOutput = engineOutput

        if (engineOutput.startsWith("[")) {
            trace.success = false
            trace.errorMessage = engineOutput
            return trace
        }

        val lines = engineOutput.split("\n")
        var i = 0
        val values = mutableListOf<String>()
        while (i < lines.size) {
            val line = lines[i]
            if (line.contains(MARK)) {
                // the value is the next non-empty-marker line
                val value = if (i + 1 < lines.size) lines[i + 1] else ""
                values.add(value)
                i += 2
            } else {
                i++
            }
        }

        if (values.size < stages.size + 1) {
            trace.success = false
            trace.errorMessage = "Engine did not return a value for every pipeline stage."
            return trace
        }

        trace.sourceValueText = values[0]
        for ((idx, stage) in stages.withIndex()) {
            stage.valueText = values[idx + 1]
        }
        trace.finalValueText = values.last()
        return trace
    }
}

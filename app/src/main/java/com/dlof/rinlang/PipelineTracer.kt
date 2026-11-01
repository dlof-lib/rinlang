package com.dlof.rinlang

/**
 * Status of one [PipelineTracer.Stage] (section 2's Flow Node status vocabulary, as far as this
 * tracer's re-run-per-prefix probing can honestly support it). QUEUED/RUNNING/CANCELLED aren't
 * reachable from here yet — every stage in a probe resolves synchronously in one
 * [RinEngine.runSourceStructured] call, so there is no in-between "this exact stage is
 * currently executing" moment to observe or cancel independently; only SUCCESS, ERROR (the
 * stage whose value never got printed before the engine reported an error) and SKIPPED
 * (everything after it) are actually distinguishable today.
 */
enum class FlowNodeStatus { QUEUED, RUNNING, SUCCESS, ERROR, SKIPPED, CANCELLED, TIMEOUT }

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
        var status: FlowNodeStatus = FlowNodeStatus.SUCCESS,
        /** Real diagnostic (severity/code/message/line/column) from the engine when [status] is
         *  ERROR — see [RinEngine.runSourceStructured]. Null on SUCCESS/SKIPPED; never a guess. */
        var diagnostic: RinDiagnostic? = null
    ) {
        /** @deprecated kept for source compatibility; derive from [status] instead of a separate
         *  flag that could disagree with it. */
        @Deprecated("Use status", ReplaceWith("status == FlowNodeStatus.SUCCESS"))
        val ok: Boolean get() = status == FlowNodeStatus.SUCCESS
    }

    data class PipelineTrace(
        val containerName: String,
        val sourceExpr: String,
        var sourceValueText: String = "",
        val stages: MutableList<Stage> = mutableListOf(),
        var finalValueText: String = "",
        var success: Boolean = true,
        var errorMessage: String? = null,
        var rawEngineOutput: String = "",
        /** Real structured diagnostic for the failure (section 8: Flow Diagnostics needs a real
         *  Node + Source + Line + Column, not a text blob) — from the same
         *  [RinEngine.runSourceStructured] call that decided [success], never re-derived by
         *  sniffing [rawEngineOutput]. Null on success. */
        var diagnostic: RinDiagnostic? = null,
        /** الزمن الحقيقي (ملي ثانية) الذي استغرقه محرك Rin فعلاً لتنفيذ هذا الـ probe —
         *  مقاس بـ System.nanoTime حول الاستدعاء الفعلي، وليس رقماً وهمياً أو تقديرياً. */
        var totalDurationMs: Long = 0
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

        val startNs = System.nanoTime()
        // Real engine-reported SUCCESS/ERROR (Interpreter::hadError()), never guessed from the
        // shape of the printed text (section 6) — a pipeline stage's own value can legitimately
        // start with '[' (e.g. a stage returning an array), which the old
        // `engineOutput.startsWith("[")` check would have misclassified as a failed run.
        val result = try {
            RinEngine.runSourceStructured(probe.toString())
        } catch (t: Throwable) {
            RinExecutionResult(
                success = false, output = "", diagnostic = null,
                diagnosticText = null, errorMessage = "[Internal error]: ${t.message}", errorLine = 0
            )
        }
        trace.totalDurationMs = (System.nanoTime() - startNs) / 1_000_000
        trace.rawEngineOutput = result.output
        trace.diagnostic = result.diagnostic

        val values = extractMarkedValues(result.output)

        if (!result.success) {
            trace.success = false
            trace.errorMessage = result.diagnosticText ?: result.errorMessage ?: result.output
            // Stages whose value was already printed before the engine hit the error keep their
            // real value and a SUCCESS status; the first stage whose print never completed is
            // the one the engine actually failed on (real diagnostic attached, not a placeholder
            // stand-in); everything declared after it never ran at all -> SKIPPED, not a second
            // ERROR. If the failure happened before the very first probe print even ran (e.g. a
            // syntax/parse error, or a runtime error earlier in the user's own container body),
            // `values` is empty and every stage is honestly reported SKIPPED rather than blamed.
            if (values.isNotEmpty()) trace.sourceValueText = values[0]
            for ((idx, stage) in stages.withIndex()) {
                val valueIdx = idx + 1
                when {
                    valueIdx < values.size -> {
                        stage.valueText = values[valueIdx]
                        stage.status = FlowNodeStatus.SUCCESS
                    }
                    valueIdx == values.size -> {
                        stage.status = FlowNodeStatus.ERROR
                        stage.diagnostic = result.diagnostic
                    }
                    else -> stage.status = FlowNodeStatus.SKIPPED
                }
            }
            return trace
        }

        if (values.size < stages.size + 1) {
            trace.success = false
            trace.errorMessage = "Engine did not return a value for every pipeline stage."
            return trace
        }

        trace.sourceValueText = values[0]
        for ((idx, stage) in stages.withIndex()) {
            stage.valueText = values[idx + 1]
            stage.status = FlowNodeStatus.SUCCESS
        }
        trace.finalValueText = values.last()
        return trace
    }

    /** Scans engine output for the `MARK` sentinels this tracer injects and pulls out the value
     *  printed right after each one. Shared by both the success and failure paths so a run that
     *  errors partway through still surfaces every value that really did get printed first. */
    private fun extractMarkedValues(engineOutput: String): List<String> {
        val lines = engineOutput.split("\n")
        var i = 0
        val values = mutableListOf<String>()
        while (i < lines.size) {
            if (lines[i].contains(MARK)) {
                values.add(if (i + 1 < lines.size) lines[i + 1] else "")
                i += 2
            } else {
                i++
            }
        }
        return values
    }
}

package com.dlof.rinlang

/** One visual step in a RinFlow diagram — could be a pipe stage, a container open/close,
 *  a table row, a style change, or a real save/installation artifact ready to download. */
data class FlowNode(
    val icon: Int?,
    val colorRes: Int,
    val title: String,
    val subtitle: String = "",
    val valueText: String = "",
    val ok: Boolean = true,
    val artifact: RinArtifact? = null,
    /** Real per-node status (section 2) when the underlying trace can distinguish one — e.g. a
     *  pipeline stage that never ran because an earlier stage failed is SKIPPED, not lumped in
     *  with the ERROR stage. Defaults to deriving from [ok] for nodes ([fromGenericRun]'s) that
     *  only ever have a binary real/failed signal from the engine. */
    val status: FlowNodeStatus = if (ok) FlowNodeStatus.SUCCESS else FlowNodeStatus.ERROR,
    /** Real diagnostic (line/column/code/message) for this node when [status] is ERROR and the
     *  engine reported one — see [RinEngine.runSourceStructured]. Null otherwise; never guessed. */
    val diagnostic: RinDiagnostic? = null
)

/** The full result RinFlow renders: either a detailed `@container.pipe` stage trace, or a
 *  generic step-by-step flow built from the real engine output of any other Rin concept. */
data class RinFlowResult(
    val kindLabel: String,
    val containerName: String,
    val nodes: List<FlowNode>,
    val success: Boolean,
    val errorMessage: String? = null,
    val rawEngineOutput: String = "",
    /** Real structured diagnostic for the failure (section 8), straight from
     *  [RinEngine.runSourceStructured] — null on success. */
    val diagnostic: RinDiagnostic? = null
)

/**
 * RinFlow's execution core. Unlike a narrow "pipeline-only" tracer, this works with every
 * concept the Rin language has: `@container.pipe`, `@container.table`/`@table`, `@container.data`,
 * `@container.api`, `@container` (plain), `@Containers.Group`, `@Volume`, `@Section`,
 * `@Translations`, `@import`, `link`/`tying`/`merge`, `style`, `row`, and `save`/`installation`
 * (whose real on-disk artifacts become downloadable nodes right inside the diagram).
 *
 * `@container.pipe` blocks that contain a `let x = source |> step() |> step();` expression still
 * get the more detailed per-stage *value* trace (via [PipelineTracer]) because that needs the
 * engine re-run once per stage. Everything else is built from one real, single execution of the
 * user's actual code — nothing here is simulated or hand-written.
 */
object RinFlowTracer {

    fun trace(source: String): RinFlowResult? {
        if (source.isBlank()) return null

        val pipelineTrace = PipelineTracer.findPipeline(source)
        if (pipelineTrace != null) return fromPipelineTrace(pipelineTrace)

        return fromGenericRun(source)
    }

    // ---- detailed @container.pipe (source |> step |> step) trace ----

    private fun fromPipelineTrace(trace: PipelineTracer.PipelineTrace): RinFlowResult {
        val nodes = mutableListOf<FlowNode>()
        if (trace.sourceExpr.isNotEmpty()) {
            nodes += FlowNode(
                icon = R.drawable.ic_node_source,
                colorRes = R.color.pipeline_green,
                title = "Source",
                subtitle = trace.sourceExpr,
                valueText = trace.sourceValueText,
                ok = true
            )
            for (stage in trace.stages) {
                val stageOk = stage.status == FlowNodeStatus.SUCCESS
                nodes += FlowNode(
                    icon = iconForStage(stage.label),
                    colorRes = when (stage.status) {
                        FlowNodeStatus.SUCCESS -> R.color.pipeline_green
                        FlowNodeStatus.SKIPPED -> R.color.pipeline_text_primary
                        else -> R.color.pipeline_red
                    },
                    title = stage.label,
                    subtitle = stage.call,
                    valueText = stage.valueText,
                    ok = stageOk,
                    status = stage.status,
                    diagnostic = stage.diagnostic
                )
            }
            nodes += FlowNode(
                icon = if (trace.success) R.drawable.ic_status_success else R.drawable.ic_status_error,
                colorRes = if (trace.success) R.color.pipeline_green else R.color.pipeline_red,
                title = "Final Output",
                subtitle = "print",
                valueText = if (trace.success) trace.finalValueText else "—",
                ok = trace.success
            )
        }
        return RinFlowResult(
            kindLabel = "container.pipe",
            containerName = trace.containerName,
            nodes = nodes,
            success = trace.success,
            errorMessage = trace.errorMessage ?: (if (!trace.success) trace.rawEngineOutput else null),
            rawEngineOutput = trace.rawEngineOutput,
            diagnostic = trace.diagnostic
        )
    }

    private fun iconForStage(name: String): Int {
        val n = name.lowercase()
        return when {
            "aggregat" in n || "mean" in n || "sum" in n || "reduce" in n || "total" in n || "count" in n
                || "median" in n || "variance" in n || "stddev" in n || "mode" in n -> R.drawable.ic_node_aggregate
            "sort" in n -> R.drawable.ic_node_sort
            "filter" in n -> R.drawable.ic_node_filter
            else -> R.drawable.ic_node_transform
        }
    }

    // ---- generic trace: every other concept, built from one real execution ----

    private val containerOpenRegex = Regex(
        """@(container(?:\.[A-Za-z_]+)?|table|Containers\.Group|Volume|Section|Translations)\s*=?\s*([A-Za-z_][A-Za-z0-9_]*)?"""
    )

    /**
     * Cheap static check (no engine execution) used by the editor's Run button to decide whether
     * to offer "Open RinFlow" — true for a `@container.pipe` block *or* any other container
     * concept (table, data, api, group, volume, section, translations) RinFlow can also trace.
     */
    fun looksTraceable(source: String): Boolean =
        PipelineTracer.containsPipeline(source) || containerOpenRegex.containsMatchIn(source)

    private fun fromGenericRun(source: String): RinFlowResult {
        // Real engine-reported SUCCESS/ERROR (Interpreter::hadError()) instead of sniffing
        // whether the printed text happens to start with '[' (section 6) -- a plain
        // `print ["a","b"];` in this generic flow view used to be misreported as a failed run.
        val result = try {
            RinEngine.runSourceStructured(source)
        } catch (t: Throwable) {
            RinExecutionResult(
                success = false, output = "", diagnostic = null,
                diagnosticText = null, errorMessage = "[Internal error]: ${t.message}", errorLine = 0
            )
        }
        val engineOutput = result.output.ifEmpty {
            result.diagnosticText ?: result.errorMessage?.let { "[Error]: $it" } ?: ""
        }

        val baseDir = try { RinEngine.currentBaseDir() } catch (t: Throwable) { "" }
        val artifacts = RinConsoleFormatter.extractArtifacts(engineOutput, baseDir)

        val nodes = RinConsoleFormatter.formatLines(engineOutput).map { line ->
            val matchedArtifact = artifacts.firstOrNull { line.text.contains(it.relPath) }
            FlowNode(
                icon = line.kind.icon,
                colorRes = line.kind.colorRes,
                title = shortTitle(line.kind),
                valueText = line.text,
                ok = line.kind != LogKind.ERROR,
                artifact = matchedArtifact
            )
        }

        val match = containerOpenRegex.find(source)
        val kindLabel = match?.groupValues?.get(1) ?: "Rin program"
        val containerName = match?.groupValues?.get(2).orEmpty()

        return RinFlowResult(
            kindLabel = kindLabel,
            containerName = containerName,
            nodes = nodes,
            success = result.success,
            errorMessage = if (!result.success) engineOutput else null,
            rawEngineOutput = engineOutput,
            diagnostic = result.diagnostic
        )
    }

    private fun shortTitle(kind: LogKind): String = when (kind) {
        LogKind.STRUCTURE -> "Container"
        LogKind.SUCCESS -> "Done"
        LogKind.IMPORT -> "Import"
        LogKind.LINK -> "Link"
        LogKind.EXPORT -> "Save"
        LogKind.IMAGE -> "Image"
        LogKind.ARCHIVE -> "Archive"
        LogKind.FILE -> "File"
        LogKind.GRID -> "Row"
        LogKind.STYLE -> "Style"
        LogKind.NETWORK -> "Network"
        LogKind.DOC_INSERT -> "Insert"
        LogKind.DOC_UPDATE -> "Update"
        LogKind.ERROR -> "Error"
        LogKind.PLAIN -> "Output"
        LogKind.INFO -> "Info"
        LogKind.WARNING -> "Warning"
        LogKind.DEBUG -> "Debug"
    }
}

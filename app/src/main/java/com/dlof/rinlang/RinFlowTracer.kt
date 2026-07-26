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
    val artifact: RinArtifact? = null
)

/** The full result RinFlow renders: either a detailed `@container.pipe` stage trace, or a
 *  generic step-by-step flow built from the real engine output of any other Rin concept. */
data class RinFlowResult(
    val kindLabel: String,
    val containerName: String,
    val nodes: List<FlowNode>,
    val success: Boolean,
    val errorMessage: String? = null,
    val rawEngineOutput: String = ""
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
                nodes += FlowNode(
                    icon = iconForStage(stage.label),
                    colorRes = if (trace.success) R.color.pipeline_green else R.color.pipeline_red,
                    title = stage.label,
                    subtitle = stage.call,
                    valueText = stage.valueText,
                    ok = trace.success
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
            rawEngineOutput = trace.rawEngineOutput
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
        val engineOutput = try {
            RinEngine.runSource(source)
        } catch (t: Throwable) {
            "[Internal error]: ${t.message}"
        }
        val success = !engineOutput.trimStart().startsWith("[")

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
            success = success,
            errorMessage = if (!success) engineOutput else null,
            rawEngineOutput = engineOutput
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
        LogKind.ERROR -> "Error"
        LogKind.PLAIN -> "Output"
    }
}

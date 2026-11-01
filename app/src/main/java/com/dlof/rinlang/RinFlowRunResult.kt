package com.dlof.rinlang

import org.json.JSONObject

/** Status of an entire [RinFlowRunResult] (section 3), as reported by the native
 *  `rin::flow::SessionStatus` — never guessed from [RinFlowRunResult.output]. */
enum class RinFlowSessionStatus { RUNNING, SUCCESS, ERROR, CANCELLED, TIMEOUT }

/** One node in the real Flow Graph the native `rin::flow::RinFlowEngine` built while actually
 *  executing a `|>` chain (section 2) — a snapshot after the whole flow finished, mirroring
 *  `rin::flow::FlowNode` field-for-field. Never fabricated: [input]/[output] are null unless the
 *  engine really recorded a value for that node (section 7/12). */
data class RinFlowGraphNode(
    val id: Int,
    val type: String,      // INPUT/OUTPUT/FILTER/MAP/TRANSFORM/SORT/REDUCE/CONTAINER/FILE/NETWORK/PIPELINE/CUSTOM
    val name: String,
    val status: FlowNodeStatus,
    val input: RinFlowDataPreview?,
    val output: RinFlowDataPreview?,
    val startedAt: Long,
    val finishedAt: Long,
    val durationMs: Long,
    val line: Int,
    val column: Int,
    val error: RinFlowNodeError?
) {
    companion object {
        fun parse(obj: JSONObject): RinFlowGraphNode = RinFlowGraphNode(
            id = obj.optInt("id", 0),
            type = obj.optString("type", "CUSTOM"),
            name = obj.optString("name", ""),
            status = try { FlowNodeStatus.valueOf(obj.optString("status", "QUEUED")) } catch (t: Throwable) { FlowNodeStatus.QUEUED },
            input = obj.optJSONObject("input")?.let { RinFlowDataPreview.parse(it) },
            output = obj.optJSONObject("output")?.let { RinFlowDataPreview.parse(it) },
            startedAt = obj.optLong("startedAt", 0),
            finishedAt = obj.optLong("finishedAt", 0),
            durationMs = obj.optLong("durationMs", 0),
            line = obj.optInt("line", 0),
            column = obj.optInt("column", 1),
            error = obj.optJSONObject("error")?.let { RinFlowNodeError.parse(it) }
        )
    }
}

/** Section 12: Data Inspector — a size-capped preview of a real value the engine actually
 *  produced (`rin::flow::DataPreview`). [recordCount] is the *true* full length when the value
 *  was an array, even though [preview] itself may show fewer elements (see [truncated]). */
data class RinFlowDataPreview(
    val preview: String,
    val recordCount: Long, // -1 = not an array (no record count)
    val truncated: Boolean
) {
    companion object {
        fun parse(obj: JSONObject): RinFlowDataPreview = RinFlowDataPreview(
            preview = obj.optString("preview", ""),
            recordCount = obj.optLong("recordCount", -1),
            truncated = obj.optBoolean("truncated", false)
        )
    }
}

/** Section 8: Flow Diagnostics — a real error captured while executing this exact node, tied to
 *  its actual source line (`rin::flow::NodeError`). */
data class RinFlowNodeError(
    val code: String,   // e.g. "RIN-F-E0021" (RinFlow-internal, distinct from language diag codes)
    val message: String,
    val line: Int,
    val column: Int
) {
    companion object {
        fun parse(obj: JSONObject): RinFlowNodeError = RinFlowNodeError(
            code = obj.optString("code", ""),
            message = obj.optString("message", ""),
            line = obj.optInt("line", 0),
            column = obj.optInt("column", 1)
        )
    }
}

/** Section 13: Flow Metrics — every field is a real count/sum derived from the executed graph's
 *  nodes (`rin::flow::FlowMetrics`); nothing here is estimated. */
data class RinFlowMetrics(
    val totalNodes: Int,
    val completedNodes: Int,
    val failedNodes: Int,
    val skippedNodes: Int,
    val cancelledNodes: Int,
    val timeoutNodes: Int,
    val totalDurationMs: Long,
    val totalInputRecords: Long,
    val totalOutputRecords: Long
) {
    companion object {
        fun parse(obj: JSONObject): RinFlowMetrics = RinFlowMetrics(
            totalNodes = obj.optInt("totalNodes", 0),
            completedNodes = obj.optInt("completedNodes", 0),
            failedNodes = obj.optInt("failedNodes", 0),
            skippedNodes = obj.optInt("skippedNodes", 0),
            cancelledNodes = obj.optInt("cancelledNodes", 0),
            timeoutNodes = obj.optInt("timeoutNodes", 0),
            totalDurationMs = obj.optLong("totalDurationMs", 0),
            totalInputRecords = obj.optLong("totalInputRecords", 0),
            totalOutputRecords = obj.optLong("totalOutputRecords", 0)
        )
    }
}

/** One live event as a flow actually executes (section 4), delivered synchronously to
 *  [RinEngine.RinFlowListener.onFlowEvent] — mirrors `rin::flow::FlowEvent` field-for-field. */
data class RinFlowEvent(
    val sequence: Long,
    val timestamp: Long,
    val flowId: String,
    val nodeId: Int, // -1 = a flow-level event (FLOW_STARTED/FLOW_FINISHED/FLOW_CANCELLED/FLOW_TIMEOUT)
    val type: String, // FLOW_STARTED/NODE_QUEUED/NODE_STARTED/NODE_OUTPUT/NODE_FINISHED/NODE_ERROR/FLOW_FINISHED/FLOW_CANCELLED/FLOW_TIMEOUT
    val message: String,
    val line: Int,
    val column: Int,
    val durationMs: Long
) {
    companion object {
        /** Parses one event JSON string as delivered to [RinEngine.RinFlowListener.onFlowEvent]. */
        fun parse(json: String): RinFlowEvent? = try {
            val obj = JSONObject(json)
            RinFlowEvent(
                sequence = obj.optLong("sequence", 0),
                timestamp = obj.optLong("timestamp", 0),
                flowId = obj.optString("flowId", ""),
                nodeId = obj.optInt("nodeId", -1),
                type = obj.optString("type", ""),
                message = obj.optString("message", ""),
                line = obj.optInt("line", 0),
                column = obj.optInt("column", 1),
                durationMs = obj.optLong("durationMs", 0)
            )
        } catch (t: Throwable) {
            null
        }
    }
}

/**
 * Full result of [RinEngine.runSourceAsFlow] / [RinEngine.replayFlow] — a real, executed Flow
 * Graph plus the same `print`ed [output] a plain [RinEngine.runSource] call would have produced,
 * plus real [metrics]. Mirrors `rin::Interpreter::FlowRunResult` (see rin_interpreter.h/.cpp).
 *
 * [parseError] is set instead of a populated [nodes]/[metrics] only when the flow never actually
 * started running at all — a lexer/parser failure before any `|>` chain executed, or (for
 * [RinEngine.replayFlow]) an unknown/expired/never-piped session id. A *runtime* error partway
 * through a chain is not a [parseError]: it shows up as [status] == ERROR with the failing node's
 * own [RinFlowGraphNode.error] populated instead (section 8).
 */
data class RinFlowRunResult(
    val sessionId: String,
    val status: RinFlowSessionStatus,
    val output: String,
    val nodes: List<RinFlowGraphNode>,
    val edges: List<Pair<Int, Int>>,
    val metrics: RinFlowMetrics,
    val parseError: String? = null,
    val parseErrorLine: Int = 0
) {
    companion object {
        fun parse(json: String): RinFlowRunResult {
            return try {
                val obj = JSONObject(json)
                val graph = obj.optJSONObject("graph")
                val nodesArr = graph?.optJSONArray("nodes")
                val nodes = nodesArr?.let { arr -> (0 until arr.length()).map { RinFlowGraphNode.parse(arr.getJSONObject(it)) } } ?: emptyList()
                val edgesArr = graph?.optJSONArray("edges")
                val edges = edgesArr?.let { arr ->
                    (0 until arr.length()).map { i ->
                        val pair = arr.getJSONArray(i)
                        pair.getInt(0) to pair.getInt(1)
                    }
                } ?: emptyList()
                RinFlowRunResult(
                    sessionId = obj.optString("sessionId", ""),
                    status = try { RinFlowSessionStatus.valueOf(obj.optString("status", "ERROR")) } catch (t: Throwable) { RinFlowSessionStatus.ERROR },
                    output = obj.optString("output", ""),
                    nodes = nodes,
                    edges = edges,
                    metrics = obj.optJSONObject("metrics")?.let { RinFlowMetrics.parse(it) }
                        ?: RinFlowMetrics(0, 0, 0, 0, 0, 0, 0, 0, 0),
                    parseError = if (obj.has("parseError")) obj.optString("parseError", null) else null,
                    parseErrorLine = obj.optInt("parseErrorLine", 0)
                )
            } catch (t: Throwable) {
                RinFlowRunResult(
                    sessionId = "", status = RinFlowSessionStatus.ERROR, output = "",
                    nodes = emptyList(), edges = emptyList(),
                    metrics = RinFlowMetrics(0, 0, 0, 0, 0, 0, 0, 0, 0),
                    parseError = "[Malformed flow result]: ${t.message} — raw: $json"
                )
            }
        }
    }
}

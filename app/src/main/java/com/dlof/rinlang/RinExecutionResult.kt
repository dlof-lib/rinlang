package com.dlof.rinlang

import org.json.JSONObject

/**
 * A single structured diagnostic (error/warning/note/help) coming straight from the native
 * `rin::diag::Diagnostic` (see app/src/main/cpp/diagnostics/diagnostic.h), instead of being
 * scraped back out of formatted text. Mirrors `rin::diag::renderJson`'s field names 1:1.
 */
data class RinDiagnostic(
    val severity: String,       // "error" | "warning" | "note" | "help"
    val code: String,           // "E0001" style
    val codeName: String,       // "UndefinedVariable" style
    val message: String,
    val file: String,
    val line: Int,
    val column: Int,
    val endLine: Int,
    val endColumn: Int,
    val reason: String?,
    val expected: String?,
    val found: String?,
    val notes: List<String>,
    val hints: List<String>,
    val suggestions: List<String>,
    val causedBy: List<String>
) {
    companion object {
        fun parse(obj: JSONObject): RinDiagnostic = RinDiagnostic(
            severity = obj.optString("severity", "error"),
            code = obj.optString("code", ""),
            codeName = obj.optString("codeName", ""),
            message = obj.optString("message", ""),
            file = obj.optString("file", "<input>"),
            line = obj.optInt("line", 0),
            column = obj.optInt("column", 0),
            endLine = obj.optInt("endLine", obj.optInt("line", 0)),
            endColumn = obj.optInt("endColumn", obj.optInt("column", 0)),
            reason = if (obj.has("reason")) obj.optString("reason") else null,
            expected = if (obj.has("expected")) obj.optString("expected") else null,
            found = if (obj.has("found")) obj.optString("found") else null,
            notes = obj.optJSONArray("notes")?.let { arr -> (0 until arr.length()).map { arr.getString(it) } } ?: emptyList(),
            hints = obj.optJSONArray("help")?.let { arr -> (0 until arr.length()).map { arr.getString(it) } } ?: emptyList(),
            suggestions = obj.optJSONArray("suggestions")?.let { arr -> (0 until arr.length()).map { arr.getString(it) } } ?: emptyList(),
            causedBy = obj.optJSONArray("causedBy")?.let { arr -> (0 until arr.length()).map { arr.getString(it) } } ?: emptyList()
        )
    }
}

/**
 * Structured outcome of one [RinEngine.runSourceStructured] call: a real SUCCESS/ERROR decided
 * by the engine itself (`Interpreter::hadError()`), never guessed from the shape of [output].
 *
 * TIMEOUT and CANCELLED are not represented here — those are scheduler-level outcomes decided
 * by [RinJobScheduler] (the native call either never returned in time, or was never started),
 * not something the engine itself can report from a single synchronous call.
 */
data class RinExecutionResult(
    val success: Boolean,
    val output: String,
    val diagnostic: RinDiagnostic?,
    val diagnosticText: String?,
    val errorMessage: String?,
    val errorLine: Int
) {
    companion object {
        /** Parses the JSON produced by `runSourceStructuredNative`. Never throws: a malformed
         *  payload (should not happen, but native/JNI boundaries are worth being defensive about)
         *  degrades to an ERROR result carrying the raw text as [errorMessage] instead of crashing. */
        fun parse(json: String): RinExecutionResult {
            return try {
                val obj = JSONObject(json)
                val status = obj.optString("status", "ERROR")
                RinExecutionResult(
                    success = status == "SUCCESS",
                    output = obj.optString("output", ""),
                    diagnostic = obj.optJSONObject("diagnostic")?.let { RinDiagnostic.parse(it) },
                    diagnosticText = if (obj.isNull("diagnosticText")) null else obj.optString("diagnosticText", null),
                    errorMessage = if (obj.isNull("errorMessage")) null else obj.optString("errorMessage", null),
                    errorLine = obj.optInt("errorLine", 0)
                )
            } catch (t: Throwable) {
                RinExecutionResult(
                    success = false,
                    output = "",
                    diagnostic = null,
                    diagnosticText = null,
                    errorMessage = "[Malformed structured result]: ${t.message} — raw: $json",
                    errorLine = 0
                )
            }
        }
    }
}

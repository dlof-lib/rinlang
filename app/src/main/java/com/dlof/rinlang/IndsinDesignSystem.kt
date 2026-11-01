package com.dlof.rinlang

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/**
 * Typed Kotlin layer over the Indsin Design System v2 native API (rin_indsin_system.h,
 * rin_indsin_audit.h, rin_indsin_query.h — see docs/indsin_expansion.md).
 *
 * The native side speaks JSON ([RinEngine.designTokensJson], [RinEngine.IndsinSession.auditJson], ...).
 * This file turns that JSON into immutable data classes so UI code never touches `JSONObject`
 * directly. Every `parse` is total: malformed or error-envelope input yields a safe value (never an
 * exception), mirroring the engine's own rule of not letting failures cross a boundary.
 *
 * Nothing here needs a running session except [IndsinAuditReport] / [IndsinFabricStats], which come
 * from one; [IndsinDesign] is the stateless entry point for tokens, palettes and themes.
 */

// ---------------------------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------------------------

/**
 * Parses the engine's color literals into an Android-style ARGB [Int].
 * Accepts `#RRGGBB` and `#RRGGBBAA` (the native `toHexAuto` format: alpha is the LAST byte, unlike
 * Android's `#AARRGGBB`, which is why `android.graphics.Color.parseColor` must not be used here).
 * Returns null for anything else.
 */
fun parseIndsinColor(literal: String?): Int? {
    if (literal == null) return null
    val hex = literal.trim().removePrefix("#")
    if (hex.length != 6 && hex.length != 8) return null
    if (!hex.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) return null
    val rgb = hex.substring(0, 6).toLong(16).toInt()
    val alpha = if (hex.length == 8) hex.substring(6, 8).toInt(16) else 0xFF
    return (alpha shl 24) or rgb
}

/** Formats an ARGB [Int] as the engine's literal: `#RRGGBB` when opaque, else `#RRGGBBAA`. */
fun formatIndsinColor(argb: Int): String {
    val a = (argb ushr 24) and 0xFF
    val rgb = argb and 0xFFFFFF
    val base = "#%06X".format(rgb)
    return if (a == 0xFF) base else base + "%02X".format(a)
}

// ---------------------------------------------------------------------------------------------
// Breakpoints (mirror of breakpointFor() in rin_indsin_system.h — keep the thresholds in sync)
// ---------------------------------------------------------------------------------------------

/** Responsive breakpoints used by the `<attr>_<bp>` overrides (e.g. `columns_md=2`). */
enum class IndsinBreakpoint(val tag: String, val minWidth: Int) {
    XS("xs", 0), SM("sm", 360), MD("md", 600), LG("lg", 840), XL("xl", 1200);

    companion object {
        /** Breakpoint for a layout width in the same pixel space the session was created with. */
        fun forWidth(width: Double): IndsinBreakpoint = when {
            width < 360 -> XS
            width < 600 -> SM
            width < 840 -> MD
            width < 1200 -> LG
            else -> XL
        }

        fun fromTag(tag: String): IndsinBreakpoint? = values().firstOrNull { it.tag == tag }
    }
}

/** Coarse Material-style window class: compact (<600) / medium (<840) / expanded. */
enum class IndsinWindowSizeClass {
    COMPACT, MEDIUM, EXPANDED;

    companion object {
        fun forWidth(width: Double): IndsinWindowSizeClass = when {
            width < 600 -> COMPACT
            width < 840 -> MEDIUM
            else -> EXPANDED
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Audit
// ---------------------------------------------------------------------------------------------

enum class IndsinSeverity {
    ERROR, WARNING;

    companion object {
        fun parse(s: String?): IndsinSeverity = if (s == "error") ERROR else WARNING
    }
}

/** One finding of the native audit (codes are stable: A11Y-001.., STRUCT-001.., LAYOUT-001). */
data class IndsinAuditIssue(
    val code: String,
    val severity: IndsinSeverity,
    val strandId: Long,
    val kind: String,
    val name: String,
    val path: String,
    val line: Int,
    val message: String,
    val hint: String
) {
    /** `Button 'nolabel'` / `Text` — short label for a list row. */
    val title: String get() = if (name.isEmpty()) kind else "$kind '$name'"
}

data class IndsinAuditReport(
    val ok: Boolean,
    val score: Int,
    val nodes: Int,
    val errors: Int,
    val warnings: Int,
    val issues: List<IndsinAuditIssue>,
    /** Non-null when the native call itself failed (closed/invalid session), not when rules failed. */
    val engineError: String? = null
) {
    val isEmpty: Boolean get() = issues.isEmpty()

    fun byCode(code: String): List<IndsinAuditIssue> = issues.filter { it.code == code }

    /** Errors first, then warnings; stable within each group (source order). */
    fun sorted(): List<IndsinAuditIssue> =
        issues.sortedBy { if (it.severity == IndsinSeverity.ERROR) 0 else 1 }

    companion object {
        val EMPTY = IndsinAuditReport(ok = true, score = 100, nodes = 0, errors = 0, warnings = 0, issues = emptyList())

        fun parse(json: String?): IndsinAuditReport {
            val obj = parseObject(json) ?: return failed("empty audit result")
            val err = obj.optString("error", "")
            if (err.isNotEmpty() && !obj.has("issues")) return failed(err)
            val issues = obj.optJSONArray("issues").mapObjects { o ->
                IndsinAuditIssue(
                    code = o.optString("code"),
                    severity = IndsinSeverity.parse(o.optString("severity")),
                    strandId = o.optLong("strandId"),
                    kind = o.optString("kind"),
                    name = o.optString("name"),
                    path = o.optString("path"),
                    line = o.optInt("line"),
                    message = o.optString("message"),
                    hint = o.optString("hint")
                )
            }
            return IndsinAuditReport(
                ok = obj.optBoolean("ok", false),
                score = obj.optInt("score", 0),
                nodes = obj.optInt("nodes", 0),
                errors = obj.optInt("errors", issues.count { it.severity == IndsinSeverity.ERROR }),
                warnings = obj.optInt("warnings", issues.count { it.severity == IndsinSeverity.WARNING }),
                issues = issues
            )
        }

        private fun failed(message: String) = IndsinAuditReport(
            ok = false, score = 0, nodes = 0, errors = 0, warnings = 0, issues = emptyList(), engineError = message
        )
    }
}

// ---------------------------------------------------------------------------------------------
// Fabric statistics
// ---------------------------------------------------------------------------------------------

data class IndsinFabricStats(
    val nodes: Int,
    val maxDepth: Int,
    val leaves: Int,
    val interactive: Int,
    val byKind: Map<String, Int>,
    val byCategory: Map<String, Int>
) {
    companion object {
        val EMPTY = IndsinFabricStats(0, 0, 0, 0, emptyMap(), emptyMap())

        fun parse(json: String?): IndsinFabricStats {
            val o = parseObject(json) ?: return EMPTY
            if (!o.has("nodes")) return EMPTY
            return IndsinFabricStats(
                nodes = o.optInt("nodes"),
                maxDepth = o.optInt("maxDepth"),
                leaves = o.optInt("leaves"),
                interactive = o.optInt("interactive"),
                byKind = o.optJSONObject("byKind").toIntMap(),
                byCategory = o.optJSONObject("byCategory").toIntMap()
            )
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Themes
// ---------------------------------------------------------------------------------------------

/** The 12 theme roles the engine defines, in a fixed display order. */
object IndsinThemeRoles {
    val ALL = listOf(
        "primary", "secondary", "success", "danger", "warning", "info",
        "neutral", "surface", "background", "text", "text_muted", "border"
    )
}

data class IndsinTheme(
    val name: String,
    val dark: Boolean,
    /** role -> ARGB (see [IndsinThemeRoles.ALL]); roles the engine omitted are absent. */
    val colors: Map<String, Int>
) {
    fun color(role: String): Int? = colors[role]

    companion object {
        fun parse(o: JSONObject?): IndsinTheme? {
            if (o == null || !o.has("name")) return null
            val colorsObj = o.optJSONObject("colors")
            val colors = LinkedHashMap<String, Int>()
            if (colorsObj != null) {
                for (role in IndsinThemeRoles.ALL) {
                    parseIndsinColor(colorsObj.optString(role, null))?.let { colors[role] = it }
                }
            }
            return IndsinTheme(o.optString("name"), o.optBoolean("dark", false), colors)
        }
    }
}

data class IndsinThemeIssue(
    val code: String,
    val severity: IndsinSeverity,
    val message: String,
    val ratio: Double,
    val required: Double
)

/** WCAG contrast report for one theme (rules THEME-* in rin_indsin_system.h). */
data class IndsinThemeReport(
    val ok: Boolean,
    val score: Int,
    val checks: Int,
    val passed: Int,
    val issues: List<IndsinThemeIssue>,
    val engineError: String? = null
) {
    companion object {
        fun parse(o: JSONObject?): IndsinThemeReport {
            if (o == null) return failed("empty theme report")
            val err = o.optString("error", "")
            if (err.isNotEmpty() && !o.has("issues")) return failed(err)
            return IndsinThemeReport(
                ok = o.optBoolean("ok", false),
                score = o.optInt("score", 0),
                checks = o.optInt("checks", 0),
                passed = o.optInt("passed", 0),
                issues = o.optJSONArray("issues").mapObjects { i ->
                    IndsinThemeIssue(
                        code = i.optString("code"),
                        severity = IndsinSeverity.parse(i.optString("severity")),
                        message = i.optString("message"),
                        ratio = i.optDouble("ratio", 0.0),
                        required = i.optDouble("required", 0.0)
                    )
                }
            )
        }

        fun parse(json: String?): IndsinThemeReport = parse(parseObject(json))

        private fun failed(message: String) =
            IndsinThemeReport(ok = false, score = 0, checks = 0, passed = 0, issues = emptyList(), engineError = message)
    }
}

/** A theme generated from one seed color together with its WCAG report. */
data class IndsinGeneratedTheme(val theme: IndsinTheme, val report: IndsinThemeReport)

// ---------------------------------------------------------------------------------------------
// Palette, elevation, tokens, catalog
// ---------------------------------------------------------------------------------------------

/** Tonal palette: steps 50,100,200,...,900 (500 is exactly the seed). */
data class IndsinPalette(val tones: Map<Int, Int>) {
    operator fun get(step: Int): Int? = tones[step]
    val seed: Int? get() = tones[500]

    companion object {
        val STEPS = listOf(50, 100, 200, 300, 400, 500, 600, 700, 800, 900)

        fun parse(json: String?): IndsinPalette? {
            val o = parseObject(json) ?: return null
            if (o.has("error")) return null
            val tones = LinkedHashMap<Int, Int>()
            for (step in STEPS) parseIndsinColor(o.optString(step.toString(), null))?.let { tones[step] = it }
            return if (tones.size == STEPS.size) IndsinPalette(tones) else null
        }
    }
}

/** One elevation level; sizes are in the same px space as the layout. [opacity] is the shadow alpha 0..1. */
data class IndsinElevation(val level: Int, val blur: Double, val offsetY: Double, val spread: Double, val opacity: Double)

/** Every design-token scale exposed by the engine, as read at call time. */
data class IndsinTokens(
    val spacing: Map<String, Double>,
    val radius: Map<String, Double>,
    val typography: Map<String, Double>,
    val duration: Map<String, Double>,
    val opacity: Map<String, Double>,
    val border: Map<String, Double>,
    val icon: Map<String, Double>,
    val z: Map<String, Double>,
    val breakpoint: Map<String, Double>,
    val elevation: List<IndsinElevation>,
    val minTouchTarget: Double,
    val recommendedTouchTarget: Double,
    val activeTheme: IndsinTheme?,
    val themeNames: List<String>
) {
    fun elevationFor(level: Int): IndsinElevation? = elevation.getOrNull(level.coerceIn(0, elevation.lastIndex.coerceAtLeast(0)))

    companion object {
        fun parse(json: String?): IndsinTokens? {
            val o = parseObject(json) ?: return null
            if (!o.has("spacing")) return null
            val touch = o.optJSONObject("touchTarget")
            val names = ArrayList<String>()
            o.optJSONArray("themes")?.let { arr -> for (i in 0 until arr.length()) names += arr.optString(i) }
            return IndsinTokens(
                spacing = o.optJSONObject("spacing").toDoubleMap(),
                radius = o.optJSONObject("radius").toDoubleMap(),
                typography = o.optJSONObject("typography").toDoubleMap(),
                duration = o.optJSONObject("duration").toDoubleMap(),
                opacity = o.optJSONObject("opacity").toDoubleMap(),
                border = o.optJSONObject("border").toDoubleMap(),
                icon = o.optJSONObject("icon").toDoubleMap(),
                z = o.optJSONObject("z").toDoubleMap(),
                breakpoint = o.optJSONObject("breakpoint").toDoubleMap(),
                elevation = o.optJSONArray("elevation").mapObjects { e ->
                    IndsinElevation(
                        level = e.optInt("level"),
                        blur = e.optDouble("blur"),
                        offsetY = e.optDouble("offsetY"),
                        spread = e.optDouble("spread"),
                        opacity = e.optDouble("opacity")
                    )
                },
                minTouchTarget = touch?.optDouble("min", 44.0) ?: 44.0,
                recommendedTouchTarget = touch?.optDouble("recommended", 48.0) ?: 48.0,
                activeTheme = IndsinTheme.parse(o.optJSONObject("activeTheme")),
                themeNames = names
            )
        }
    }
}

enum class IndsinComponentCategory(val tag: String) {
    LAYOUT("layout"), STRUCTURE("structure"), TEXT("text"), MEDIA("media"), INPUT("input"),
    ACTION("action"), FEEDBACK("feedback"), NAVIGATION("navigation"), DATA("data"),
    OVERLAY("overlay"), CUSTOM("custom");

    companion object {
        fun fromTag(tag: String?): IndsinComponentCategory = values().firstOrNull { it.tag == tag } ?: CUSTOM
    }
}

data class IndsinComponentInfo(
    val kind: String,
    val category: IndsinComponentCategory,
    val role: String,
    val interactive: Boolean,
    val overlay: Boolean
)

// ---------------------------------------------------------------------------------------------
// Facade
// ---------------------------------------------------------------------------------------------

/**
 * Stateless entry point to the native design system. All calls are cheap but cross JNI, so call
 * them off the main thread if you invoke them in a tight loop; individual calls are fine on it.
 */
object IndsinDesign {

    /** Current tokens (spacing/radius/.../elevation) + active theme + registered theme names. */
    fun tokens(): IndsinTokens? = IndsinTokens.parse(RinEngine.designTokensJson())

    /** Component taxonomy, in the engine's StrandKind order. Empty if the engine returned garbage. */
    fun catalog(): List<IndsinComponentInfo> =
        parseArray(RinEngine.componentCatalogJson()).mapObjects { o ->
            IndsinComponentInfo(
                kind = o.optString("kind"),
                category = IndsinComponentCategory.fromTag(o.optString("category")),
                role = o.optString("role"),
                interactive = o.optBoolean("interactive"),
                overlay = o.optBoolean("overlay")
            )
        }

    /** Catalog grouped by category, preserving engine order inside each group. */
    fun catalogByCategory(): Map<IndsinComponentCategory, List<IndsinComponentInfo>> = catalog().groupBy { it.category }

    /** Tonal palette around [seed] (`#RRGGBB`); null when the seed is not a valid color. */
    fun palette(seed: String): IndsinPalette? = IndsinPalette.parse(RinEngine.tonalPaletteJson(seed))

    /** Full theme + WCAG report generated from [seed]; null when the seed is invalid. */
    fun themeFromSeed(seed: String, dark: Boolean): IndsinGeneratedTheme? {
        val o = parseObject(RinEngine.themeFromSeedJson(seed, dark)) ?: return null
        if (o.has("error")) return null
        val theme = IndsinTheme.parse(o.optJSONObject("theme")) ?: return null
        return IndsinGeneratedTheme(theme, IndsinThemeReport.parse(o.optJSONObject("report")))
    }

    /** WCAG report for a natively registered theme name (e.g. `"Sepia"`, `"HighContrastDark"`). */
    fun validateTheme(themeName: String): IndsinThemeReport = IndsinThemeReport.parse(RinEngine.validateThemeJson(themeName))

    /** Names of the built-in themes shipped with the engine, in a stable order. */
    val builtInThemeNames: List<String> = listOf(
        "Dark", "Light", "Midnight", "Ocean", "Slate",
        "HighContrastDark", "HighContrastLight", "Sepia", "Forest", "Rose"
    )
}

// ---------------------------------------------------------------------------------------------
// Session conveniences
// ---------------------------------------------------------------------------------------------

/** Typed audit of the session's current Fabric. Never throws. */
fun RinEngine.IndsinSession.audit(): IndsinAuditReport =
    try { IndsinAuditReport.parse(auditJson()) } catch (t: Throwable) {
        IndsinAuditReport(false, 0, 0, 0, 0, emptyList(), t.message ?: t.toString())
    }

/** Typed statistics of the session's current Fabric. Never throws. */
fun RinEngine.IndsinSession.stats(): IndsinFabricStats =
    try { IndsinFabricStats.parse(statsJson()) } catch (t: Throwable) { IndsinFabricStats.EMPTY }

// ---------------------------------------------------------------------------------------------
// Internal JSON helpers (private to this file's package-level API)
// ---------------------------------------------------------------------------------------------

private fun parseObject(json: String?): JSONObject? {
    if (json.isNullOrBlank()) return null
    return try { JSONObject(json) } catch (e: JSONException) { null }
}

private fun parseArray(json: String?): JSONArray? {
    if (json.isNullOrBlank()) return null
    return try { JSONArray(json) } catch (e: JSONException) { null }
}

private inline fun <T> JSONArray?.mapObjects(transform: (JSONObject) -> T): List<T> {
    if (this == null) return emptyList()
    val out = ArrayList<T>(length())
    for (i in 0 until length()) {
        val o = optJSONObject(i) ?: continue
        out += transform(o)
    }
    return out
}

private fun JSONObject?.toIntMap(): Map<String, Int> {
    if (this == null) return emptyMap()
    val out = LinkedHashMap<String, Int>()
    val keys = keys()
    while (keys.hasNext()) { val k = keys.next(); out[k] = optInt(k) }
    return out
}

private fun JSONObject?.toDoubleMap(): Map<String, Double> {
    if (this == null) return emptyMap()
    val out = LinkedHashMap<String, Double>()
    val keys = keys()
    while (keys.hasNext()) { val k = keys.next(); out[k] = optDouble(k) }
    return out
}

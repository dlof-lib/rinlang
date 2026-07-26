package com.dlof.rinlang

import androidx.annotation.ColorRes
import androidx.annotation.DrawableRes
import java.io.File

/** Category of a single formatted output line (drives the icon + accent color used to render it). */
enum class LogKind(@DrawableRes val icon: Int?, @ColorRes val colorRes: Int) {
    STRUCTURE(R.drawable.ic_log_container, R.color.log_kind_structure),
    SUCCESS(R.drawable.ic_log_check, R.color.log_kind_success),
    IMPORT(R.drawable.ic_log_import, R.color.log_kind_import),
    LINK(R.drawable.ic_log_link, R.color.log_kind_import),
    EXPORT(R.drawable.ic_log_export, R.color.log_kind_export),
    IMAGE(R.drawable.ic_log_image, R.color.log_kind_export),
    ARCHIVE(R.drawable.ic_log_archive, R.color.log_kind_export),
    FILE(R.drawable.ic_log_file, R.color.log_kind_data),
    GRID(R.drawable.ic_log_grid, R.color.log_kind_data),
    STYLE(R.drawable.ic_log_palette, R.color.log_kind_style),
    NETWORK(R.drawable.ic_log_globe, R.color.log_kind_network),
    ERROR(R.drawable.ic_status_error, R.color.log_kind_error),
    PLAIN(null, R.color.log_kind_plain)
}

/** One line of a job's console output, ready to be rendered as an icon + styled text row. */
data class RinLogLine(val kind: LogKind, val text: String)

/** File kinds save/installation can actually write to disk, and how to open/share them afterwards. */
enum class ArtifactKind(val mime: String, @DrawableRes val icon: Int) {
    IMAGE_PNG("image/png", R.drawable.ic_log_image),
    ARCHIVE_ZIP("application/zip", R.drawable.ic_log_archive),
    RIN_DOC("text/plain", R.drawable.ic_log_export)
}

/**
 * A real file produced by a `save` / `installation` statement during the last run.
 * [absoluteFile] always points at an existing file under the engine's base directory —
 * this is genuine on-disk output, not something synthesized for display.
 */
data class RinArtifact(
    val kind: ArtifactKind,
    val relPath: String,
    val absoluteFile: File,
    val sizeBytes: Long
) {
    val displayName: String get() = File(relPath).name
}

object RinConsoleFormatter {

    // كل بادئة مطابقة لنفس النص الذي يطبعه rin_interpreter.cpp حرفياً، مرتّبة من الأكثر تحديداً.
    private val PREFIX_ORDER: List<Pair<String, LogKind>> = listOf(
        "🖼️" to LogKind.IMAGE,
        "🗜️" to LogKind.ARCHIVE,
        "⚙️" to LogKind.EXPORT,
        "💾" to LogKind.EXPORT,
        "📄" to LogKind.FILE,
        "📥" to LogKind.IMPORT,
        "📦⬅️" to LogKind.IMPORT,
        "📦" to LogKind.IMPORT,
        "↺" to LogKind.IMPORT,
        "✅" to LogKind.SUCCESS,
        "◽" to LogKind.SUCCESS,
        "🗂️" to LogKind.STRUCTURE,
        "📊" to LogKind.STRUCTURE,
        "🧵" to LogKind.STRUCTURE,
        "📚" to LogKind.STRUCTURE,
        "🔹" to LogKind.STRUCTURE,
        "🌐" to LogKind.NETWORK,
        "🌍" to LogKind.NETWORK,
        "🔗" to LogKind.LINK,
        "🪢" to LogKind.LINK,
        "🧬" to LogKind.LINK,
        "▦" to LogKind.GRID,
        "🎨" to LogKind.STYLE
    )

    private val RE_SAVE_PNG = Regex("""^🖼️\s*save\s*\(png\)\s*->\s*(.+?)\s*\(""")
    private val RE_SAVE_ZIP = Regex("""^🗜️\s*save\s*\(zip\)\s*->\s*(.+?)\s*\(""")
    private val RE_SAVE_RIN = Regex("""^💾\s*save(?:\s*\(simplified\))?\s*->\s*(.+?)\s*\(""")
    private val RE_INSTALL_ZIP = Regex("""^🗜️\s*installation\s*\(zip\):\s*\S+\s*->\s*(.+?)\s*\(""")
    private val RE_INSTALL_RIN = Regex("""^⚙️\s*installation(?:\s*\(simplified\))?:\s*\S+\s*->\s*(.+?)\s*\(""")

    /** Splits raw native output into styled lines (strips the leading emoji glyph itself). */
    fun formatLines(rawOutput: String): List<RinLogLine> {
        if (rawOutput.isBlank()) return emptyList()
        return rawOutput.split("\n")
            .filter { it.isNotBlank() }
            .map { rawLine ->
                val line = rawLine.trimEnd()
                val trimmedStart = line.trimStart()
                if (trimmedStart.startsWith("[")) {
                    RinLogLine(LogKind.ERROR, trimmedStart)
                } else {
                    val match = PREFIX_ORDER.firstOrNull { (prefix, _) -> trimmedStart.startsWith(prefix) }
                    if (match != null) {
                        val (prefix, kind) = match
                        RinLogLine(kind, trimmedStart.removePrefix(prefix).trim())
                    } else {
                        RinLogLine(LogKind.PLAIN, trimmedStart)
                    }
                }
            }
    }

    /**
     * Finds every real file save/installation wrote to disk during this run by resolving the
     * relative path each line reports against the engine's actual base directory, then reading
     * that file's true size straight off disk (never trusting a byte-count printed in the log).
     */
    fun extractArtifacts(rawOutput: String, baseDir: String): List<RinArtifact> {
        if (rawOutput.isBlank() || baseDir.isBlank()) return emptyList()
        val base = File(baseDir)
        val found = LinkedHashMap<String, RinArtifact>()

        fun consider(relPath: String, kind: ArtifactKind) {
            val cleanRel = relPath.trim().trim('"')
            if (cleanRel.isEmpty()) return
            val file = File(base, cleanRel)
            if (file.exists() && file.isFile) {
                found[file.absolutePath] = RinArtifact(kind, cleanRel, file, file.length())
            }
        }

        rawOutput.split("\n").forEach { rawLine ->
            val line = rawLine.trim()
            RE_SAVE_PNG.find(line)?.let { consider(it.groupValues[1], ArtifactKind.IMAGE_PNG) }
            RE_SAVE_ZIP.find(line)?.let { consider(it.groupValues[1], ArtifactKind.ARCHIVE_ZIP) }
            RE_INSTALL_ZIP.find(line)?.let { consider(it.groupValues[1], ArtifactKind.ARCHIVE_ZIP) }
            RE_SAVE_RIN.find(line)?.let { consider(it.groupValues[1], ArtifactKind.RIN_DOC) }
            RE_INSTALL_RIN.find(line)?.let { consider(it.groupValues[1], ArtifactKind.RIN_DOC) }
        }
        return found.values.toList()
    }

    /** Human friendly "12.4 MB" / "512 KB" / "180 بايت" formatting for progress UI. */
    fun formatBytes(bytes: Long): String {
        if (bytes < 1024) return "$bytes بايت"
        val kb = bytes / 1024.0
        if (kb < 1024) return String.format("%.1f KB", kb)
        val mb = kb / 1024.0
        if (mb < 1024) return String.format("%.1f MB", mb)
        val gb = mb / 1024.0
        return String.format("%.2f GB", gb)
    }
}

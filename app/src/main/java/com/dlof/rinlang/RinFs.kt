package com.dlof.rinlang

import java.io.File

/**
 * أوامر الملفات للطرفية (pwd / ls / tree / cat) — منطق خالص على java.io، محصور داخل مجلد المشروع.
 * يُرجع أسطراً ملوّنة بـ ANSI تعرضها الطرفية عبر [RinAnsi]، أو رمز خطأ في [Out.error].
 */
object RinFs {
    class Out(val lines: List<String>, val error: String? = null, val extra: Long = 0)

    const val NOT_FOUND = "NOT_FOUND"
    const val OUTSIDE = "OUTSIDE"
    const val NOT_DIR = "NOT_DIR"
    const val NOT_FILE = "NOT_FILE"
    const val BINARY = "BINARY"

    private const val ESC = "\u001b"
    private const val RESET = "$ESC[0m"
    private const val DIM = "$ESC[38;2;139;148;158m"
    private const val SOFT = "$ESC[38;2;201;209;217m"
    private const val BLUE = "$ESC[1;38;2;88;166;255m"
    private const val GREEN = "$ESC[38;2;63;185;80m"
    private const val YELLOW = "$ESC[38;2;210;153;34m"

    /** يحلّ [rel] داخل [base] ويمنع الخروج منه (.. أو روابط). null = خارج المجلد. */
    fun resolve(base: File, rel: String): File? {
        val root = base.canonicalFile
        val r = rel.trim().trim('"')
        val raw = when {
            r.isEmpty() || r == "." || r == "~" -> root
            File(r).isAbsolute -> File(r)
            else -> File(root, r)
        }
        val target = raw.canonicalFile
        val ok = target == root || target.path.startsWith(root.path + File.separator)
        return if (ok) target else null
    }

    fun humanSize(n: Long): String = when {
        n < 1024 -> "$n B"
        n < 1024 * 1024 -> String.format(java.util.Locale.US, "%.1f KB", n / 1024.0)
        else -> String.format(java.util.Locale.US, "%.1f MB", n / 1048576.0)
    }

    private fun kids(dir: File, all: Boolean): List<File> =
        (dir.listFiles() ?: emptyArray()).filter { all || !it.name.startsWith(".") }
            .sortedWith(compareBy<File>({ !it.isDirectory }, { it.name.lowercase() }))

    private fun colorName(f: File): String = when {
        f.isDirectory -> "$BLUE${f.name}/$RESET"
        f.name.endsWith(".rin") -> "$GREEN${f.name}$RESET"
        f.name.endsWith(".json") || f.name.endsWith(".md") -> "$YELLOW${f.name}$RESET"
        else -> "$SOFT${f.name}$RESET"
    }

    fun ls(base: File, rel: String, all: Boolean = false): Out {
        val d = resolve(base, rel) ?: return Out(emptyList(), OUTSIDE)
        if (!d.exists()) return Out(emptyList(), NOT_FOUND)
        if (!d.isDirectory) return Out(listOf("$DIM${humanSize(d.length()).padStart(9)}  $RESET${colorName(d)}"))
        val ks = kids(d, all)
        val lines = ks.map { f ->
            val size = if (f.isDirectory) "${(f.listFiles()?.size ?: 0)} items" else humanSize(f.length())
            "$DIM${size.padStart(9)}  $RESET${colorName(f)}"
        }
        return Out(lines, null, ks.size.toLong())
    }

    fun tree(base: File, rel: String, maxDepth: Int = 3, maxEntries: Int = 300, all: Boolean = false): Out {
        val d = resolve(base, rel) ?: return Out(emptyList(), OUTSIDE)
        if (!d.exists()) return Out(emptyList(), NOT_FOUND)
        if (!d.isDirectory) return Out(emptyList(), NOT_DIR)
        val out = ArrayList<String>()
        out.add("$BLUE${if (d == base.canonicalFile) "." else d.name}/$RESET")
        var count = 0
        var truncated = false
        fun walk(dir: File, prefix: String, depth: Int) {
            val ks = kids(dir, all)
            for ((i, f) in ks.withIndex()) {
                if (count >= maxEntries) { truncated = true; return }
                count++
                val last = i == ks.lastIndex
                out.add("$DIM$prefix${if (last) "└── " else "├── "}$RESET${colorName(f)}")
                if (f.isDirectory && depth < maxDepth) walk(f, prefix + if (last) "    " else "│   ", depth + 1)
            }
        }
        walk(d, "", 1)
        if (truncated) out.add("$DIM…$RESET")
        return Out(out, null, count.toLong())
    }

    /** يقرأ حتى [maxLines] سطراً بأرقام؛ extra = إجمالي الأسطر غير المعروضة. */
    fun cat(base: File, rel: String, maxLines: Int = 200, maxBytes: Int = 256 * 1024): Out {
        val f = resolve(base, rel) ?: return Out(emptyList(), OUTSIDE)
        if (!f.exists()) return Out(emptyList(), NOT_FOUND)
        if (!f.isFile) return Out(emptyList(), NOT_FILE)
        val bytes = f.inputStream().use { it.readNBytes(maxBytes) }
        if (bytes.any { it == 0.toByte() }) return Out(emptyList(), BINARY)
        val all = String(bytes, Charsets.UTF_8).split('\n').let { if (it.isNotEmpty() && it.last().isEmpty()) it.dropLast(1) else it }
        val shown = all.take(maxLines)
        val w = shown.size.toString().length
        val lines = shown.mapIndexed { i, l -> "$DIM${(i + 1).toString().padStart(w)} │ $RESET${l.trimEnd('\r').replace("\u001b", "␛")}" }
        return Out(lines, null, (all.size - shown.size).toLong() + if (f.length() > maxBytes) 1 else 0)
    }
}

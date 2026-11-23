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

    // ------------------------------------------------------------------ find / head / tail / wc

    /** `head f 5` / `tail -n 5 f` / `head f`: يُرجع (المسار، العدد). العدد الافتراضي [def]، ويُقيَّد 1..10000. */
    fun parseCountArg(arg: String, def: Int = 10): Pair<String, Int> {
        val t = arg.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }.toMutableList()
        var n = def
        val i = t.indexOf("-n")
        if (i >= 0 && i + 1 < t.size && t[i + 1].toIntOrNull() != null) {
            n = t[i + 1].toInt(); t.removeAt(i + 1); t.removeAt(i)
        } else if (t.size >= 2 && t.last().toIntOrNull() != null) {
            n = t.removeAt(t.lastIndex).toInt()
        }
        return t.joinToString(" ") to n.coerceIn(1, 10000)
    }

    private class Text(val name: String, val lines: List<String>, val total: Int, val bytes: Long, val error: String?)

    private fun load(base: File, rel: String, maxBytes: Int = 1024 * 1024): Text {
        val f = resolve(base, rel) ?: return Text("", emptyList(), 0, 0, OUTSIDE)
        if (!f.exists()) return Text("", emptyList(), 0, 0, NOT_FOUND)
        if (!f.isFile) return Text("", emptyList(), 0, 0, NOT_FILE)
        val bytes = f.inputStream().use { it.readNBytes(maxBytes) }
        if (bytes.any { it == 0.toByte() }) return Text("", emptyList(), 0, 0, BINARY)
        val all = String(bytes, Charsets.UTF_8).split('\n').let { if (it.isNotEmpty() && it.last().isEmpty()) it.dropLast(1) else it }
        return Text(f.name, all.map { it.trimEnd('\r').replace("\u001b", "␛") }, all.size, f.length(), null)
    }

    private fun numbered(lines: List<String>, first: Int): List<String> {
        val w = (first + lines.size - 1).toString().length
        return lines.mapIndexed { i, l -> "$DIM${(first + i).toString().padStart(w)} │ $RESET$l" }
    }

    /** أول [n] سطراً بأرقامها؛ extra = عدد الأسطر غير المعروضة. */
    fun head(base: File, rel: String, n: Int = 10): Out {
        val t = load(base, rel); if (t.error != null) return Out(emptyList(), t.error)
        val shown = t.lines.take(n.coerceAtLeast(1))
        return Out(numbered(shown, 1), null, (t.total - shown.size).toLong())
    }

    /** آخر [n] سطراً بأرقامها الحقيقية في الملف. */
    fun tail(base: File, rel: String, n: Int = 10): Out {
        val t = load(base, rel); if (t.error != null) return Out(emptyList(), t.error)
        val shown = t.lines.takeLast(n.coerceAtLeast(1))
        return Out(numbered(shown, t.total - shown.size + 1), null, (t.total - shown.size).toLong())
    }

    /** عدد الأسطر والكلمات والحجم. */
    fun wc(base: File, rel: String): Out {
        val t = load(base, rel); if (t.error != null) return Out(emptyList(), t.error)
        val words = t.lines.sumOf { l -> l.trim().split(Regex("\\s+")).count { it.isNotEmpty() } }
        return Out(listOf("$SOFT${t.total}$RESET${DIM} lines · $RESET$SOFT$words$RESET${DIM} words · $RESET$SOFT${humanSize(t.bytes)}$RESET  $GREEN${t.name}$RESET"), null, t.total.toLong())
    }

    /** بحث بالاسم (بلا حساسية للحالة) داخل المشروع، يتخطى المخفي؛ حتى [maxResults] نتيجة وعمق [maxDepth]. */
    fun find(base: File, query: String, maxResults: Int = 100, maxDepth: Int = 8): Out {
        val q = query.trim().lowercase()
        if (q.isEmpty()) return Out(emptyList(), NOT_FOUND)
        val root = base.canonicalFile
        val out = ArrayList<String>()
        var count = 0
        fun walk(dir: File, depth: Int) {
            for (f in kids(dir, false)) {
                if (count >= maxResults) return
                if (f.name.lowercase().contains(q)) {
                    count++
                    val rel = f.relativeTo(root).path.replace(File.separatorChar, '/')
                    out.add(if (f.isDirectory) "$BLUE$rel/$RESET" else "${colorName(f).replace(f.name, rel)}")
                }
                if (f.isDirectory && depth < maxDepth) walk(f, depth + 1)
            }
        }
        walk(root, 1)
        return Out(out, null, count.toLong())
    }

    /** اسم ملف سجل الطرفية: terminal-yyyyMMdd-HHmmss.txt (الوقت بالمللي يُمرَّر لتبقى الدالة خالصة). */
    fun logFileName(epochMs: Long): String {
        val c = java.util.Calendar.getInstance(); c.timeInMillis = epochMs
        return String.format(java.util.Locale.US, "terminal-%04d%02d%02d-%02d%02d%02d.txt",
            c.get(java.util.Calendar.YEAR), c.get(java.util.Calendar.MONTH) + 1, c.get(java.util.Calendar.DAY_OF_MONTH),
            c.get(java.util.Calendar.HOUR_OF_DAY), c.get(java.util.Calendar.MINUTE), c.get(java.util.Calendar.SECOND))
    }

    /** يحفظ [text] في <base>/logs/ ويُرجع المسار النسبي (أو null عند الفشل). */
    fun saveLog(base: File, text: String, epochMs: Long): String? = try {
        val dir = File(base.canonicalFile, "logs"); dir.mkdirs()
        val f = File(dir, logFileName(epochMs)); f.writeText(text)
        "logs/" + f.name
    } catch (t: Throwable) { null }
}

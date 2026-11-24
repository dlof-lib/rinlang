package com.dlof.rinlang

import android.content.Context
import java.io.File
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.util.regex.PatternSyntaxException

/*
 * محرك البحث المتقدم في مشروع Rin (شاشة الملفات):
 *   • SearchOptions / SearchMode : نطاق البحث (أسماء / محتوى / الاثنان) + حالة الأحرف + كلمة كاملة + Regex + مجلد.
 *   • SearchQueryParser          : مرشّحات داخل النص نفسه:  ext:rin,kt   -ext:png   in:src   -in:build
 *   • FileSearch                 : مشي على الملفات، ترتيب بالصلة (تطابق تام > بادئة > جزء > تقريبي)،
 *                                  مطابقات متعددة لكل ملف مع مقتطف ومواضع التمييز، حدود أمان وإلغاء.
 *   • FileReplace                : استبدال في الملفات (حرفي أو Regex مع $1) مع تراجع (Undo) من الذاكرة.
 *   • SearchHistory / SearchPrefs: آخر عمليات البحث + حفظ الخيارات.
 * المحرك نفسه JVM خالص (لا يعتمد على واجهة أندرويد) ليسهل اختباره.
 */

enum class SearchMode {
    NAMES, CONTENT, BOTH;

    fun next(): SearchMode = values()[(ordinal + 1) % values().size]
}

data class SearchOptions(
    val query: String,
    val mode: SearchMode = SearchMode.BOTH,
    val caseSensitive: Boolean = false,
    val wholeWord: Boolean = false,
    val regex: Boolean = false,
    /** مجلد نسبي يُحصر فيه البحث؛ فارغ = المشروع كله. */
    val scopeRel: String = ""
)

/** مطابقة واحدة داخل سطر: [hlStart, hlEnd) مواضع التمييز داخل [preview] نفسه. */
data class SearchMatch(
    val line: Int, val preview: String, val hlStart: Int, val hlEnd: Int,
    /** عمودا البداية/النهاية (حرفياً) داخل السطر الأصلي — لتحديد المطابقة في المحرر. */
    val col: Int = 0, val endCol: Int = 0
)

class SearchHit(
    val file: RinFile,
    val nameRanges: List<IntRange>,
    val nameScore: Int,
    /** أول عدد محدود من المطابقات (للعرض)؛ العدد الكلي في [totalMatches]. */
    val matches: List<SearchMatch>,
    val totalMatches: Int
) {
    val firstLine: Int get() = matches.firstOrNull()?.line ?: 0
}

class SearchResult(
    val hits: List<SearchHit>,
    val totalMatches: Int,
    val filesScanned: Int,
    /** بلغ البحث حدّ النتائج فتوقّف مبكراً. */
    val truncated: Boolean,
    val elapsedMs: Long,
    val error: String? = null
)

class SearchFilters(
    val text: String,
    val includeExt: Set<String>,
    val excludeExt: Set<String>,
    val includeDirs: List<String>,
    val excludeDirs: List<String>
) {
    val hasFilters: Boolean
        get() = includeExt.isNotEmpty() || excludeExt.isNotEmpty() || includeDirs.isNotEmpty() || excludeDirs.isNotEmpty()

    fun accepts(relPath: String, name: String): Boolean {
        val ext = name.substringAfterLast('.', "").lowercase()
        if (includeExt.isNotEmpty() && ext !in includeExt) return false
        if (ext in excludeExt) return false
        if (includeDirs.isEmpty() && excludeDirs.isEmpty()) return true
        val dirPath = "/" + relPath.substringBeforeLast('/', "").lowercase() + "/"
        if (includeDirs.isNotEmpty() && includeDirs.none { dirPath.contains("/$it") }) return false
        if (excludeDirs.any { dirPath.contains("/$it") }) return false
        return true
    }
}

object SearchQueryParser {
    private val token = Regex("\\S+")

    fun parse(raw: String): SearchFilters {
        val incExt = linkedSetOf<String>()
        val excExt = linkedSetOf<String>()
        val incDir = ArrayList<String>()
        val excDir = ArrayList<String>()
        val cuts = ArrayList<IntRange>()
        for (m in token.findAll(raw)) {
            val t = m.value
            val neg = t.length > 1 && t[0] == '-'
            val body = if (neg) t.substring(1) else t
            val lower = body.lowercase()
            if (lower.startsWith("ext:") && body.length > 4) {
                val vals = body.substring(4).split(',')
                    .map { it.trim().trimStart('.').lowercase() }.filter { it.isNotEmpty() }
                if (vals.isEmpty()) continue
                if (neg) excExt.addAll(vals) else incExt.addAll(vals)
                cuts += m.range
            } else if (lower.startsWith("in:") && body.length > 3) {
                val v = body.substring(3).trim().trim('/').lowercase()
                if (v.isEmpty()) continue
                if (neg) excDir += v else incDir += v
                cuts += m.range
            }
        }
        val sb = StringBuilder()
        var pos = 0
        for (r in cuts) {
            sb.append(raw, pos, r.first)
            var end = r.last + 1
            while (end < raw.length && raw[end].isWhitespace()) end++
            pos = end
        }
        sb.append(raw, pos, raw.length)
        return SearchFilters(sb.toString().trim(), incExt, excExt, incDir, excDir)
    }
}

private class SearchAbort : RuntimeException()

/** CharSequence يقطع المطابقة (إلغاء أو تجاوز مهلة) حتى لا يعلّق Regex كارثي الواجهة. */
private class GuardedSeq(
    private val s: CharSequence,
    private val deadlineNs: Long,
    private val cancelled: () -> Boolean
) : CharSequence {
    private var n = 0
    override val length: Int get() = s.length
    override fun get(index: Int): Char {
        if ((++n and 0x3FFF) == 0 && (System.nanoTime() > deadlineNs || cancelled())) throw SearchAbort()
        return s[index]
    }
    override fun subSequence(startIndex: Int, endIndex: Int): CharSequence = s.subSequence(startIndex, endIndex)
    override fun toString(): String = s.toString()
}

object FileSearch {
    const val MAX_FILE_BYTES = 2L * 1024 * 1024
    const val MAX_RESULT_FILES = 500
    const val MAX_TOTAL_MATCHES = 3000
    const val MAX_MATCHES_PER_FILE = 500
    const val MAX_KEPT_PER_FILE = 25
    const val MAX_LINE = 10_000
    private const val FILE_BUDGET_NS = 1_500_000_000L
    private val SKIP_DIRS = setOf(".git", "node_modules")

    /** يبني Regex من النص والخيارات؛ يرمي [PatternSyntaxException] لتعبير Regex غير صالح. */
    fun compile(text: String, o: SearchOptions): Regex {
        val core = if (o.regex) text else Regex.escape(text)
        val body = if (o.wholeWord) "(?<![\\p{L}\\p{N}_])(?:$core)(?![\\p{L}\\p{N}_])" else core
        val flags = if (o.caseSensitive) emptySet() else setOf(RegexOption.IGNORE_CASE, RegexOption.UNICODE_CASE)
        return Regex(body, flags)
    }

    /** درجة صلة الاسم (الأصغر أفضل) ومواضع التمييز، أو null إن لم يطابق. */
    fun matchName(name: String, text: String, re: Regex?, o: SearchOptions): Pair<Int, List<IntRange>>? {
        if (text.isEmpty()) return 5 to emptyList()
        if (o.regex || o.wholeWord) {
            val ranges = re!!.findAll(name).filter { !it.range.isEmpty() }.map { it.range }.toList()
            if (ranges.isEmpty()) return null
            val score = when {
                ranges.size == 1 && ranges[0].first == 0 && ranges[0].last == name.length - 1 -> 0
                ranges[0].first == 0 -> 1
                else -> 3
            }
            return score to ranges
        }
        val ranges = ArrayList<IntRange>()
        var from = 0
        while (true) {
            val i = name.indexOf(text, from, ignoreCase = !o.caseSensitive)
            if (i < 0) break
            ranges += i until i + text.length
            from = i + text.length
        }
        if (ranges.isNotEmpty()) {
            val score = when {
                name.length == text.length -> 0
                ranges[0].first == 0 -> 1
                ranges[0].first > 0 && !name[ranges[0].first - 1].isLetterOrDigit() -> 2
                else -> 3
            }
            return score to ranges
        }
        // تطابق تقريبي (أحرف متتابعة): "mnact" ← MainActivity
        if (!o.caseSensitive && text.length >= 2 && text.none { it.isWhitespace() }) {
            val hay = name.lowercase()
            val needle = text.lowercase()
            if (hay.length != name.length) return null
            val idx = ArrayList<Int>()
            var ni = 0
            for (hi in hay.indices) {
                if (ni < needle.length && hay[hi] == needle[ni]) { idx += hi; ni++ }
            }
            if (ni == needle.length && idx.last() - idx.first() + 1 <= needle.length * 3 + 2) {
                val out = ArrayList<IntRange>()
                var s = idx[0]
                var p = idx[0]
                for (k in 1 until idx.size) {
                    if (idx[k] == p + 1) p = idx[k] else { out += s..p; s = idx[k]; p = idx[k] }
                }
                out += s..p
                return 4 to out
            }
        }
        return null
    }

    private class ScanOut(val matches: List<SearchMatch>, val count: Int)

    fun makeMatch(lineNo: Int, line: String, s: Int, e: Int): SearchMatch {
        var lead = 0
        while (lead < line.length && (line[lead] == ' ' || line[lead] == '\t')) lead++
        val from = maxOf(minOf(lead, s), s - 30)
        val to = minOf(e + 70, line.length)
        val pre = if (from > lead) "…" else ""
        val post = if (to < line.length) "…" else ""
        val body = line.substring(from, to).replace('\t', ' ')
        val hs = s - from + pre.length
        val he = minOf(e, to) - from + pre.length
        return SearchMatch(lineNo, pre + body + post, hs, he, s, e)
    }

    private fun looksBinary(f: File): Boolean = try {
        f.inputStream().use { ins ->
            val buf = ByteArray(1024)
            val n = ins.read(buf)
            n > 0 && (0 until n).any { buf[it].toInt() == 0 }
        }
    } catch (_: Exception) {
        true
    }

    private fun scanFile(f: File, re: Regex, cancelled: () -> Boolean): ScanOut? {
        if (looksBinary(f)) return null
        val kept = ArrayList<SearchMatch>()
        var count = 0
        val deadline = System.nanoTime() + FILE_BUDGET_NS
        try {
            f.bufferedReader(Charsets.UTF_8).use { br ->
                var n = 0
                while (true) {
                    val raw = br.readLine() ?: break
                    n++
                    val line = if (raw.length > MAX_LINE) raw.substring(0, MAX_LINE) else raw
                    for (m in re.findAll(GuardedSeq(line, deadline, cancelled))) {
                        if (m.range.isEmpty()) continue
                        count++
                        if (kept.size < MAX_KEPT_PER_FILE) kept += makeMatch(n, line, m.range.first, m.range.last + 1)
                        if (count >= MAX_MATCHES_PER_FILE) break
                    }
                    if (count >= MAX_MATCHES_PER_FILE) break
                }
            }
        } catch (_: SearchAbort) {
            // مهلة الملف أو إلغاء: نُبقي ما وُجد حتى الآن
        } catch (_: java.io.IOException) {
            return null
        }
        return if (count == 0) null else ScanOut(kept, count)
    }

    fun search(
        root: File,
        o: SearchOptions,
        cancelled: () -> Boolean = { false },
        isBinaryName: (String) -> Boolean = { false }
    ): SearchResult {
        val t0 = System.currentTimeMillis()
        val filters = SearchQueryParser.parse(o.query)
        val text = filters.text
        if (text.isEmpty() && !filters.hasFilters) return SearchResult(emptyList(), 0, 0, false, 0)
        val re: Regex? = if (text.isEmpty()) null else try {
            compile(text, o)
        } catch (e: PatternSyntaxException) {
            return SearchResult(emptyList(), 0, 0, false, 0, error = e.description ?: "regex")
        }
        val doNames = o.mode != SearchMode.CONTENT || text.isEmpty()
        val doContent = o.mode != SearchMode.NAMES && text.isNotEmpty()
        val scope = o.scopeRel.trim('/')
        val base = if (scope.isEmpty()) root else File(root, scope)
        if (!base.isDirectory) return SearchResult(emptyList(), 0, 0, false, 0)

        val hits = ArrayList<SearchHit>()
        var scanned = 0
        var totalMatches = 0
        var truncated = false
        var aborted = false

        fun walk(dir: File, rel: String) {
            val children = dir.listFiles() ?: return
            for (c in children) {
                if (aborted || truncated) return
                if (cancelled()) { aborted = true; return }
                val name = c.name
                val cr = if (rel.isEmpty()) name else "$rel/$name"
                val link = try { java.nio.file.Files.isSymbolicLink(c.toPath()) } catch (_: Exception) { false }
                if (link) continue
                if (c.isDirectory) {
                    if (name !in SKIP_DIRS) walk(c, cr)
                    continue
                }
                if (!c.isFile || !filters.accepts(cr, name)) continue
                scanned++
                var nameScore = 9
                var nameRanges: List<IntRange> = emptyList()
                var nameOk = false
                if (doNames) {
                    val m = matchName(name, text, re, o)
                    if (m != null) { nameOk = true; nameScore = m.first; nameRanges = m.second }
                }
                var matches: List<SearchMatch> = emptyList()
                var count = 0
                if (doContent && c.length() <= MAX_FILE_BYTES && !isBinaryName(name)) {
                    val r = scanFile(c, re!!, cancelled)
                    if (r != null) { matches = r.matches; count = r.count }
                }
                if (nameOk || count > 0) {
                    hits += SearchHit(RinFile(name, c, c.length(), c.lastModified(), cr), nameRanges, nameScore, matches, count)
                    totalMatches += count
                    if (hits.size >= MAX_RESULT_FILES || totalMatches >= MAX_TOTAL_MATCHES) truncated = true
                }
            }
        }
        walk(base, scope)

        val sorted = hits.sortedWith(Comparator { a, b ->
            val c = a.nameScore.compareTo(b.nameScore)
            when {
                c != 0 -> c
                a.nameScore < 9 && a.file.name.length != b.file.name.length ->
                    a.file.name.length.compareTo(b.file.name.length)
                else -> a.file.relPath.compareTo(b.file.relPath, ignoreCase = true)
            }
        })
        return SearchResult(sorted, totalMatches, scanned, truncated, System.currentTimeMillis() - t0)
    }
}

object FileReplace {
    class Outcome(
        val files: Int,
        val replacements: Int,
        val skipped: List<String>,
        /** النص الأصلي لكل ملف عُدِّل — للتراجع. */
        val backups: Map<File, String>
    )

    private const val MAX_BACKUP_CHARS = 16_000_000

    /** يستبدل داخل كل سطر على حدة (كما يبحث المحرك)، ويحفظ نهايات الأسطر \n و \r\n كما هي. */
    fun replaceLines(src: String, re: Regex, repl: String): Pair<String, Int> {
        val sb = StringBuilder(src.length + 16)
        var count = 0
        var i = 0
        while (i < src.length) {
            val nl = src.indexOf('\n', i)
            val end = if (nl < 0) src.length else nl + 1
            var bodyEnd = end
            if (bodyEnd > i && src[bodyEnd - 1] == '\n') bodyEnd--
            if (bodyEnd > i && src[bodyEnd - 1] == '\r') bodyEnd--
            val body = src.substring(i, bodyEnd)
            val n = re.findAll(body).count { !it.range.isEmpty() }
            if (n > 0) { sb.append(re.replace(body, repl)); count += n } else sb.append(body)
            sb.append(src, bodyEnd, end)
            i = end
        }
        return sb.toString() to count
    }

    private fun readStrict(f: File): String? = try {
        Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
            .decode(ByteBuffer.wrap(f.readBytes())).toString()
    } catch (_: CharacterCodingException) {
        null
    } catch (_: java.io.IOException) {
        null
    }

    private fun writeAtomic(f: File, text: String): Boolean = try {
        val tmp = File(f.parentFile, f.name + ".rin-replace.tmp")
        tmp.writeText(text, Charsets.UTF_8)
        if (!tmp.renameTo(f)) {
            f.writeText(text, Charsets.UTF_8)
            tmp.delete()
        }
        true
    } catch (_: Exception) {
        false
    }

    /** null إن كان النمط يطابق نصاً فارغاً (استبداله يُدخل نصاً في كل موضع). */
    fun validate(o: SearchOptions): String? {
        val text = SearchQueryParser.parse(o.query).text
        if (text.isEmpty()) return "empty"
        return try {
            if (o.regex && compile(text, o).find("") != null) "empty-match" else null
        } catch (e: PatternSyntaxException) {
            e.description ?: "regex"
        }
    }

    private fun compile(text: String, o: SearchOptions) = FileSearch.compile(text, o)

    fun apply(hits: List<SearchHit>, o: SearchOptions, replacement: String, skip: Set<String>): Outcome {
        val text = SearchQueryParser.parse(o.query).text
        val re = compile(text, o)
        val repl = if (o.regex) replacement else Regex.escapeReplacement(replacement)
        val skipped = ArrayList<String>()
        val backups = LinkedHashMap<File, String>()
        var files = 0
        var total = 0
        var backupChars = 0
        for (h in hits) {
            if (h.totalMatches == 0) continue
            val rel = h.file.relPath
            if (rel in skip || backupChars > MAX_BACKUP_CHARS) { skipped += rel; continue }
            val original = readStrict(h.file.file)
            if (original == null) { skipped += rel; continue }
            val (updated, n) = replaceLines(original, re, repl)
            if (n == 0) continue
            if (!writeAtomic(h.file.file, updated)) { skipped += rel; continue }
            backups[h.file.file] = original
            backupChars += original.length
            files++
            total += n
        }
        return Outcome(files, total, skipped, backups)
    }

    fun undo(backups: Map<File, String>): Int {
        var n = 0
        for ((f, text) in backups) if (writeAtomic(f, text)) n++
        return n
    }
}

object SearchHistory {
    private const val PREFS = "rin_search_history"
    private const val MAX = 12

    fun list(ctx: Context, project: String): List<String> =
        (ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(project, "") ?: "")
            .split('\n').filter { it.isNotEmpty() }

    fun add(ctx: Context, project: String, query: String) {
        val q = query.trim()
        if (q.isEmpty() || q.length > 200 || q.contains('\n')) return
        val cur = list(ctx, project).filter { it != q }
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(project, (listOf(q) + cur).take(MAX).joinToString("\n")).apply()
    }

    fun clear(ctx: Context, project: String) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(project).apply()
    }
}

object SearchPrefs {
    private const val PREFS = "rin_search_prefs"

    fun load(ctx: Context): SearchOptions {
        val sp = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val mode = runCatching { SearchMode.valueOf(sp.getString("mode", "BOTH")!!) }.getOrDefault(SearchMode.BOTH)
        return SearchOptions(
            query = "", mode = mode,
            caseSensitive = sp.getBoolean("case", false),
            wholeWord = sp.getBoolean("word", false),
            regex = sp.getBoolean("regex", false)
        )
    }

    fun save(ctx: Context, o: SearchOptions) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString("mode", o.mode.name).putBoolean("case", o.caseSensitive)
            .putBoolean("word", o.wholeWord).putBoolean("regex", o.regex).apply()
    }

    fun folderScope(ctx: Context): Boolean = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean("folder", false)

    fun setFolderScope(ctx: Context, v: Boolean) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean("folder", v).apply()
    }
}

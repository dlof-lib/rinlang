package com.dlof.rinlang

/**
 * مَسح ساكن (بلا أي تشغيل للمحرّك) لكل مفاهيم الأنابيب في Rin داخل نصّ مصدر واحد:
 *
 *  - `@container.pipe=name … .end/container.pipe` — حاوية أنابيب (قد يوجد أكثر من واحدة).
 *  - سلاسل `|>` المجرَّدة: `let r = data |> f() |> g();` داخل حاوية أو خارجها.
 *  - `reckon name(collection) [where cond] |> stage() …;` — الصياغة المختصرة المبنية فوق `|>`.
 *
 * تُستخدم هذه الفئة لأمرين فقط: (1) إخبار المستخدم قبل التشغيل بما وُجد في كوده، (2) قرار
 * «هل يستحق هذا الكود شاشة RinFlow؟» في المحرّر. القيم والأزمنة الفعلية لا تأتي من هنا أبداً؛
 * تأتي من محرّك Flow الأصلي (انظر [RinFlowTracer.traceRealFlow]).
 *
 * التعليقات (`// …`) ومحتوى السلاسل النصية `"…"` تُقنَّع قبل المسح، فلا يُحتسب `|>` مكتوب داخل
 * نص أو تعليق كأنبوب حقيقي، مع بقاء أرقام الأسطر دقيقة.
 */
object RinPipeScanner {

    enum class Kind { CHAIN, RECKON }

    /** سلسلة `|>` واحدة (أو `reckon` واحد): [source] هو المدخل، و[stages] أسماء/استدعاءات المراحل بالترتيب. */
    data class Chain(
        val kind: Kind,
        /** اسم النتيجة (`let x =` أو `reckon x`)، فارغ إن كانت سلسلة بلا إسناد. */
        val name: String,
        val line: Int,
        val source: String,
        val stages: List<String>,
        /** اسم `@container.pipe` الذي تقع السلسلة داخله، أو null إن كانت خارج أي حاوية أنابيب. */
        val container: String?,
        /** `where …` في reckon (null لغير reckon أو بلا where). */
        val where: String? = null
    )

    data class Block(val name: String, val line: Int)

    data class Scan(val blocks: List<Block>, val chains: List<Chain>) {
        val hasPipes: Boolean get() = blocks.isNotEmpty() || chains.isNotEmpty()
        val chainCount: Int get() = chains.count { it.kind == Kind.CHAIN }
        val reckonCount: Int get() = chains.count { it.kind == Kind.RECKON }
        val stageCount: Int get() = chains.sumOf { it.stages.size }

        /** هل يوجد `|>` عادي (لا reckon فقط)؟ — شرط عمل Replay الأصلي (يعيد تنفيذ آخر سلسلة `|>`). */
        val canReplay: Boolean get() = chainCount > 0
    }

    val EMPTY = Scan(emptyList(), emptyList())

    private val blockOpenRegex = Regex("""@container\.pipe\s*=\s*([A-Za-z_][A-Za-z0-9_]*)""")
    private const val BLOCK_CLOSE = ".end/container.pipe"
    private val letNameRegex = Regex("""^\s*(?:let|const|var)\s+([A-Za-z_][A-Za-z0-9_]*)\s*=""")
    private val reckonHeadRegex = Regex("""^\s*reckon\s+([A-Za-z_][A-Za-z0-9_]*)""")
    private val whereRegex = Regex("""\bwhere\b([\s\S]*)$""")
    private val blockTagRegex = Regex("""@container\.pipe\s*=\s*[A-Za-z_][A-Za-z0-9_]*|\.end/container\.pipe""")

    /** فحص سريع (بلا بناء قوائم) — للمحرّر عند كل Run. */
    fun hasPipes(source: String): Boolean {
        if (source.isBlank()) return false
        val m = mask(source)
        return m.contains("|>") || blockOpenRegex.containsMatchIn(m)
    }

    fun scan(source: String): Scan {
        if (source.isBlank()) return EMPTY
        val masked = mask(source)

        // 1) كتل @container.pipe: [bodyStart, bodyEnd) لكل كتلة.
        data class Range(val name: String, val line: Int, val start: Int, val end: Int)
        val ranges = mutableListOf<Range>()
        for (open in blockOpenRegex.findAll(masked)) {
            val bodyStart = open.range.last + 1
            val closeIdx = masked.indexOf(BLOCK_CLOSE, bodyStart)
            val bodyEnd = if (closeIdx < 0) masked.length else closeIdx
            ranges += Range(open.groupValues[1], lineOf(masked, open.range.first), bodyStart, bodyEnd)
        }
        val blocks = ranges.map { Block(it.name, it.line) }

        // 2) السلاسل: نجمع كل `|>` المتتالية التي لا يفصلها `;` ولا `{` ولا `}` في سلسلة واحدة.
        //    وسما فتح/إغلاق @container.pipe (بلا `;`) يُعاملان كحدّ عبارة أيضاً، كي لا تلتصق أول
        //    عبارة في الكتلة بسطر الفتح ولا تُنسَب آخر عبارة إلى كتلة أخرى.
        val scanText = boundaryMask(masked)
        val chains = mutableListOf<Chain>()
        var i = 0
        val n = scanText.length
        while (i < n - 1) {
            if (scanText[i] == '|' && scanText[i + 1] == '>') {
                val start = statementStart(scanText, i)
                var end = i
                var scan = i
                // يمتد حتى `;` (أو حتى `{`/`}` الذي يعني أن العبارة انتهت بلا `;`) أو نهاية النص.
                while (scan < n) {
                    val c = scanText[scan]
                    if (c == ';' || c == '{' || c == '}') break
                    scan++
                }
                end = scan
                val text = source.substring(start, end)
                val maskedText = scanText.substring(start, end)
                chains += buildChain(text, maskedText, lineOf(masked, firstNonSpace(masked, start)), start, ranges.map { Triple(it.name, it.start, it.end) })
                i = end
            } else {
                i++
            }
        }
        return Scan(blocks, chains)
    }

    private fun buildChain(
        text: String,
        maskedText: String,
        line: Int,
        absStart: Int,
        blockRanges: List<Triple<String, Int, Int>>
    ): Chain {
        // التقسيم على `|>` يتم على النص المقنَّع (حتى لا يُكسر داخل نص حرفي) ثم تُقتطع القطع من الأصل
        // بنفس الإزاحات، فتحتفظ المراحل بنصّها الحقيقي (مثلاً مع وسائطها).
        val cuts = mutableListOf<Int>()
        var k = 0
        while (k < maskedText.length - 1) {
            if (maskedText[k] == '|' && maskedText[k + 1] == '>') { cuts += k; k += 2 } else k++
        }
        val parts = mutableListOf<String>()
        var prev = 0
        for (c in cuts) { parts += text.substring(prev, c).trim(); prev = c + 2 }
        parts += text.substring(prev).trim()

        val head = parts.first()
        val stages = parts.drop(1).filter { it.isNotEmpty() }
        val container = blockRanges.firstOrNull { absStart >= it.second && absStart < it.third }?.first

        val reckonMatch = reckonHeadRegex.find(head)
        return if (reckonMatch != null) {
            // reckon name(collection) [where cond] — المصدر هو ما بين القوسين، وما بعدهما قد يحمل `where`.
            val rest = head.substring(reckonMatch.range.last + 1)
            val open = rest.indexOf('(')
            var close = -1
            if (open >= 0) {
                var depth = 0
                for (idx in open until rest.length) {
                    val ch = rest[idx]
                    if (ch == '(') depth++
                    else if (ch == ')') { depth--; if (depth == 0) { close = idx; break } }
                }
            }
            val collection = if (open >= 0 && close > open) rest.substring(open + 1, close).trim() else rest.trim()
            val tail = if (close >= 0) rest.substring(close + 1) else ""
            val where = whereRegex.find(tail)?.groupValues?.get(1)?.trim()?.ifEmpty { null }
            Chain(
                kind = Kind.RECKON,
                name = reckonMatch.groupValues[1],
                line = line,
                source = collection,
                stages = stages,
                container = container,
                where = where
            )
        } else {
            val name = letNameRegex.find(head)?.groupValues?.get(1).orEmpty()
            Chain(
                kind = Kind.CHAIN,
                name = name,
                line = line,
                source = head.replaceFirst(letNameRegex, "").trim(),
                stages = stages,
                container = container
            )
        }
    }

    private fun boundaryMask(masked: String): String {
        val sb = StringBuilder(masked)
        for (m in blockTagRegex.findAll(masked)) {
            for (k in m.range) sb.setCharAt(k, ' ')
            sb.setCharAt(m.range.last, ';')
        }
        return sb.toString()
    }

    /** بداية العبارة التي يقع فيها الفهرس [at]: بعد أقرب `;` أو `{` أو `}` سابق (أو بداية النص). */
    private fun statementStart(masked: String, at: Int): Int {
        var p = at - 1
        while (p >= 0) {
            val c = masked[p]
            if (c == ';' || c == '{' || c == '}') return p + 1
            p--
        }
        return 0
    }

    private fun firstNonSpace(s: String, from: Int): Int {
        var p = from
        while (p < s.length && s[p].isWhitespace()) p++
        return p.coerceAtMost((s.length - 1).coerceAtLeast(0))
    }

    private fun lineOf(s: String, index: Int): Int {
        var line = 1
        val stop = index.coerceAtMost(s.length)
        for (p in 0 until stop) if (s[p] == '\n') line++
        return line
    }

    /**
     * يعيد نسخة بنفس الطول: كل حرف داخل تعليق `//…` أو سلسلة `"…"` يُستبدَل بمسافة (الأسطر الجديدة
     * تبقى كما هي) كي لا يُحتسب ما بداخلها كشيفرة، وتبقى الإزاحات وأرقام الأسطر مطابقة للأصل.
     */
    internal fun mask(src: String): String {
        val out = CharArray(src.length)
        var i = 0
        var inString = false
        var inComment = false
        while (i < src.length) {
            val c = src[i]
            when {
                inComment -> {
                    if (c == '\n') { inComment = false; out[i] = c } else out[i] = ' '
                }
                inString -> {
                    if (c == '\\' && i + 1 < src.length) {
                        out[i] = ' '
                        out[i + 1] = if (src[i + 1] == '\n') '\n' else ' '
                        i++
                    } else if (c == '"') {
                        inString = false; out[i] = ' '
                    } else {
                        out[i] = if (c == '\n') '\n' else ' '
                    }
                }
                c == '/' && i + 1 < src.length && src[i + 1] == '/' -> { inComment = true; out[i] = ' ' }
                c == '"' -> { inString = true; out[i] = ' ' }
                else -> out[i] = c
            }
            i++
        }
        return String(out)
    }
}

package com.dlof.rinlang

/**
 * عمليات تحرير JSON فوق نص المستند كاملاً (إزاحات مسطّحة، الأسطر مفصولة بـ `\n`) — منطق خالص بلا
 * اعتماد على أندرويد فيُختبر مباشرة. الدوال التي تغيّر النص تعيد [SmartEdit.Edit] (يطبّقه المحرر
 * باستبدال واحد قابل للتراجع) أو null إن لم يكن هناك ما يتغيّر.
 *
 * الميزات: تحقق صارم بموضع الخطأ (سطر/عمود)، تنسيق وتصغير (يحفظان نصّ الأرقام والسلاسل كما هو فلا فقدان
 * دقّة)، ترتيب المفاتيح (متداخل أو سطحي، تصاعدي/تنازلي)، إصلاح الأخطاء الشائعة (تعليقات، فاصلة أخيرة،
 * علامات اقتباس مفردة، مفاتيح بلا اقتباس، True/False/None)، مسار JSON تحت المؤشر والانتقال إلى مسار،
 * مخطط هيكلي، إحصاءات، تحويل نص ↔ سلسلة JSON (escape/unescape)، إدراج مفتاح/كائن/مصفوفة بفاصلة تلقائية،
 * وإضافة الفاصلة تلقائياً عند Enter.
 */
object JsonEdit {

    // ------------------------------------------------------------------ الأخطاء

    enum class ErrKind {
        EMPTY, UNEXPECTED_CHAR, UNEXPECTED_END, EXPECTED_KEY, EXPECTED_COLON, EXPECTED_COMMA_OR_END,
        TRAILING_COMMA, COMMENT, SINGLE_QUOTE, EXTRA_DATA, CONTROL_CHAR, BAD_ESCAPE, UNTERMINATED_STRING,
        BAD_NUMBER, TOO_DEEP
    }

    /** خطأ تحليل: [offset] إزاحة مسطّحة، [line]/[col] مبنيّان على 1، [ch] الحرف المتسبّب إن وُجد. */
    data class JsonError(val kind: ErrKind, val offset: Int, val line: Int, val col: Int, val ch: Char?)

    private class Fail(val kind: ErrKind, val at: Int) : Exception(null, null, false, false)

    /** (سطر، عمود) مبنيّان على 1 للإزاحة [offset]. */
    fun lineCol(doc: String, offset: Int): Pair<Int, Int> {
        val o = offset.coerceIn(0, doc.length)
        var line = 1
        var last = -1
        for (i in 0 until o) if (doc[i] == '\n') { line++; last = i }
        return line to (o - last)
    }

    private fun toError(doc: String, f: Fail): JsonError {
        val (l, c) = lineCol(doc, f.at)
        return JsonError(f.kind, f.at.coerceIn(0, doc.length), l, c, doc.getOrNull(f.at))
    }

    // ------------------------------------------------------------------ الشجرة

    enum class Kind { OBJECT, ARRAY, STRING, NUMBER, BOOL, NULL }

    /**
     * عقدة قيمة. للأعضاء داخل كائن: [key] المفتاح المفكوك و[keyRaw] نصّه الموحَّد (بعلامات اقتباس مزدوجة)
     * و[keyStart] بداية المفتاح في المصدر. [raw] نصّ القيمة الموحَّد للقيم البسيطة.
     */
    class Node(val kind: Kind, val start: Int) {
        var end: Int = start
        var raw: String = ""
        var key: String? = null
        var keyRaw: String = ""
        var keyStart: Int = -1
        val children = ArrayList<Node>()
        /** بداية «المقطع» في المصدر: المفتاح لعضو الكائن، وإلا بداية القيمة. */
        val segStart: Int get() = if (keyStart >= 0) keyStart else start
    }

    private class Parsed(val root: Node, val sawComments: Boolean, val repaired: Boolean)

    private const val MAX_DEPTH = 1500

    private class Parser(val s: String, val lenient: Boolean, val comments: Boolean) {
        var i = 0
        var sawComments = false
        var repaired = false

        fun fail(kind: ErrKind, at: Int = i): Nothing = throw Fail(kind, at)

        fun ws() {
            while (i < s.length) {
                val c = s[i]
                if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '﻿' || (lenient && c.isWhitespace())) { i++; continue }
                if (c == '/' && i + 1 < s.length && (s[i + 1] == '/' || s[i + 1] == '*')) {
                    if (!comments) fail(ErrKind.COMMENT)
                    sawComments = true
                    if (s[i + 1] == '/') {
                        while (i < s.length && s[i] != '\n') i++
                    } else {
                        val e = s.indexOf("*/", i + 2)
                        if (e < 0) fail(ErrKind.UNEXPECTED_END, s.length)
                        i = e + 2
                    }
                    continue
                }
                break
            }
        }

        fun top(): Node {
            ws()
            if (i >= s.length) fail(ErrKind.EMPTY, 0)
            val n = value(0)
            ws()
            if (i < s.length) fail(ErrKind.EXTRA_DATA)
            return n
        }

        fun value(depth: Int): Node {
            if (depth > MAX_DEPTH) fail(ErrKind.TOO_DEEP)
            if (i >= s.length) fail(ErrKind.UNEXPECTED_END, s.length)
            val c = s[i]
            return when {
                c == '{' -> obj(depth)
                c == '[' -> arr(depth)
                c == '"' || (c == '\'' && lenient) -> {
                    val st = i
                    val n = Node(Kind.STRING, st)
                    n.raw = string()
                    n.end = i
                    n
                }
                c == '\'' -> fail(ErrKind.SINGLE_QUOTE)
                c == '-' || c in '0'..'9' -> number()
                c.isLetter() -> literal()
                else -> fail(ErrKind.UNEXPECTED_CHAR)
            }
        }

        /** يقرأ سلسلة ويعيد نصّها الموحَّد بعلامات اقتباس مزدوجة (المصدر نفسه إن كانت مزدوجة أصلاً). */
        fun string(): String {
            val st = i
            val q = s[i]
            i++
            val sb = StringBuilder()
            var needsRequote = q == '\''
            while (true) {
                if (i >= s.length) fail(ErrKind.UNTERMINATED_STRING, st)
                val c = s[i]
                when {
                    c == q -> { i++; break }
                    c == '\\' -> {
                        if (i + 1 >= s.length) fail(ErrKind.UNTERMINATED_STRING, st)
                        when (val e = s[i + 1]) {
                            '"', '\\', '/', 'b', 'f', 'n', 'r', 't' -> { sb.append(c).append(e); i += 2 }
                            'u' -> {
                                if (i + 6 > s.length) fail(ErrKind.BAD_ESCAPE)
                                for (k in 2..5) if (!isHex(s[i + k])) fail(ErrKind.BAD_ESCAPE, i)
                                sb.append(s, i, i + 6); i += 6
                            }
                            '\'' -> if (lenient) { sb.append('\''); needsRequote = true; i += 2 } else fail(ErrKind.BAD_ESCAPE)
                            else -> fail(ErrKind.BAD_ESCAPE)
                        }
                    }
                    c < ' ' -> fail(ErrKind.CONTROL_CHAR)
                    q == '\'' && c == '"' -> { sb.append("\\\""); i++ }
                    else -> { sb.append(c); i++ }
                }
            }
            if (needsRequote) repaired = true
            return "\"" + sb + "\""
        }

        fun isHex(c: Char) = c in '0'..'9' || c in 'a'..'f' || c in 'A'..'F'

        fun number(): Node {
            val st = i
            if (s[i] == '-') i++
            if (i >= s.length) fail(ErrKind.BAD_NUMBER, st)
            if (s[i] == '0') { i++; if (i < s.length && s[i] in '0'..'9') fail(ErrKind.BAD_NUMBER, st) }
            else if (s[i] in '1'..'9') { while (i < s.length && s[i] in '0'..'9') i++ }
            else fail(ErrKind.BAD_NUMBER, st)
            if (i < s.length && s[i] == '.') {
                i++
                val d = i
                while (i < s.length && s[i] in '0'..'9') i++
                if (i == d) fail(ErrKind.BAD_NUMBER, st)
            }
            if (i < s.length && (s[i] == 'e' || s[i] == 'E')) {
                i++
                if (i < s.length && (s[i] == '+' || s[i] == '-')) i++
                val d = i
                while (i < s.length && s[i] in '0'..'9') i++
                if (i == d) fail(ErrKind.BAD_NUMBER, st)
            }
            // رقم ملاصق لحروف (مثل 12abc) خطأ
            if (i < s.length && (s[i].isLetter() || s[i] == '_')) fail(ErrKind.BAD_NUMBER, st)
            val n = Node(Kind.NUMBER, st)
            n.raw = s.substring(st, i); n.end = i
            return n
        }

        fun literal(): Node {
            val st = i
            while (i < s.length && (s[i].isLetterOrDigit() || s[i] == '_')) i++
            val w = s.substring(st, i)
            val (kind, raw) = when (w) {
                "true" -> Kind.BOOL to "true"
                "false" -> Kind.BOOL to "false"
                "null" -> Kind.NULL to "null"
                else -> if (lenient) when (w) {
                    "True" -> { repaired = true; Kind.BOOL to "true" }
                    "False" -> { repaired = true; Kind.BOOL to "false" }
                    "None", "undefined" -> { repaired = true; Kind.NULL to "null" }
                    else -> fail(ErrKind.UNEXPECTED_CHAR, st)
                } else fail(ErrKind.UNEXPECTED_CHAR, st)
            }
            val n = Node(kind, st)
            n.raw = raw; n.end = i
            return n
        }

        fun isIdentStart(c: Char) = c.isLetter() || c == '_' || c == '$'
        fun isIdentPart(c: Char) = c.isLetterOrDigit() || c == '_' || c == '$'

        /** يقرأ مفتاحاً ويضعه في [n]. */
        fun key(n: Node) {
            val c = s[i]
            n.keyStart = i
            when {
                c == '"' || (c == '\'' && lenient) -> n.keyRaw = string()
                c == '\'' -> fail(ErrKind.SINGLE_QUOTE)
                lenient && isIdentStart(c) -> {
                    val st = i
                    while (i < s.length && isIdentPart(s[i])) i++
                    n.keyRaw = "\"" + s.substring(st, i) + "\""
                    repaired = true
                }
                else -> fail(ErrKind.EXPECTED_KEY)
            }
            n.key = decode(n.keyRaw)
        }

        fun obj(depth: Int): Node {
            val node = Node(Kind.OBJECT, i)
            i++
            ws()
            if (i < s.length && s[i] == '}') { i++; node.end = i; return node }
            while (true) {
                ws()
                if (i >= s.length) fail(ErrKind.UNEXPECTED_END, s.length)
                val tmp = Node(Kind.NULL, 0)
                key(tmp)
                ws()
                if (i >= s.length) fail(ErrKind.UNEXPECTED_END, s.length)
                if (s[i] != ':') fail(ErrKind.EXPECTED_COLON)
                i++
                ws()
                val v = value(depth + 1)
                v.key = tmp.key; v.keyRaw = tmp.keyRaw; v.keyStart = tmp.keyStart
                node.children.add(v)
                ws()
                if (i >= s.length) fail(ErrKind.UNEXPECTED_END, s.length)
                when (s[i]) {
                    ',' -> {
                        val comma = i
                        i++
                        ws()
                        if (i < s.length && s[i] == '}') {
                            if (!lenient) fail(ErrKind.TRAILING_COMMA, comma)
                            repaired = true; i++; break
                        }
                    }
                    '}' -> { i++; break }
                    else -> fail(ErrKind.EXPECTED_COMMA_OR_END)
                }
            }
            node.end = i
            return node
        }

        fun arr(depth: Int): Node {
            val node = Node(Kind.ARRAY, i)
            i++
            ws()
            if (i < s.length && s[i] == ']') { i++; node.end = i; return node }
            while (true) {
                ws()
                node.children.add(value(depth + 1))
                ws()
                if (i >= s.length) fail(ErrKind.UNEXPECTED_END, s.length)
                when (s[i]) {
                    ',' -> {
                        val comma = i
                        i++
                        ws()
                        if (i < s.length && s[i] == ']') {
                            if (!lenient) fail(ErrKind.TRAILING_COMMA, comma)
                            repaired = true; i++; break
                        }
                    }
                    ']' -> { i++; break }
                    else -> fail(ErrKind.EXPECTED_COMMA_OR_END)
                }
            }
            node.end = i
            return node
        }
    }

    private fun parse(doc: String, lenient: Boolean = false, comments: Boolean = false): Parsed {
        val p = Parser(doc, lenient, comments)
        val root = p.top()
        return Parsed(root, p.sawComments, p.repaired)
    }

    // ------------------------------------------------------------------ السلاسل

    /** يفكّ نصّ سلسلة JSON بعلامات اقتباس مزدوجة (`"…"`) إلى قيمتها. يفترض أنه صالح. */
    fun decode(raw: String): String {
        if (raw.length < 2) return ""
        val sb = StringBuilder()
        var i = 1
        val end = raw.length - 1
        while (i < end) {
            val c = raw[i]
            if (c != '\\' || i + 1 >= end) { sb.append(c); i++; continue }
            when (val e = raw[i + 1]) {
                'n' -> { sb.append('\n'); i += 2 }
                't' -> { sb.append('\t'); i += 2 }
                'r' -> { sb.append('\r'); i += 2 }
                'b' -> { sb.append('\b'); i += 2 }
                'f' -> { sb.append('\u000C'); i += 2 }
                'u' -> {
                    val hex = if (i + 6 <= raw.length) raw.substring(i + 2, i + 6) else ""
                    val v = hex.toIntOrNull(16)
                    if (v != null) { sb.append(v.toChar()); i += 6 } else { sb.append(e); i += 2 }
                }
                else -> { sb.append(e); i += 2 } // " \ / '
            }
        }
        return sb.toString()
    }

    /** يحوّل [text] إلى سلسلة JSON بعلامات اقتباس مزدوجة مع الهروب اللازم. */
    fun quote(text: String): String {
        val sb = StringBuilder(text.length + 2)
        sb.append('"')
        for (c in text) {
            when (c) {
                '"' -> sb.append("\\\"")
                '\\' -> sb.append("\\\\")
                '\n' -> sb.append("\\n")
                '\r' -> sb.append("\\r")
                '\t' -> sb.append("\\t")
                '\b' -> sb.append("\\b")
                '\u000C' -> sb.append("\\f")
                ' ' -> sb.append("\\u2028")
                ' ' -> sb.append("\\u2029")
                else -> if (c < ' ') sb.append("\\u").append(String.format("%04x", c.code)) else sb.append(c)
            }
        }
        sb.append('"')
        return sb.toString()
    }

    // ------------------------------------------------------------------ التحقق

    /** null إن كان [doc] JSON صالحاً صارماً (أو JSONC عند [allowComments])، وإلا موضع الخطأ. */
    fun validate(doc: String, allowComments: Boolean = false): JsonError? =
        try { parse(doc, lenient = false, comments = allowComments); null } catch (f: Fail) { toError(doc, f) }

    // ------------------------------------------------------------------ نتيجة العملية

    /** نتيجة عملية تحويل: إمّا [edit] (قد يكون null = لا تغيير)، أو [error]، أو [hasComments] (يمنع فقدان التعليقات). */
    class Result(val edit: SmartEdit.Edit? = null, val error: JsonError? = null, val hasComments: Boolean = false, val noChange: Boolean = false)

    // ------------------------------------------------------------------ الكتابة

    /** مسافة بادئة مستخرجة من المستند: tab أو n مسافة؛ [fallback] إن لم يُعثر على سطر مُزاح. */
    fun detectIndent(doc: String, fallback: String = "  "): String {
        for (line in doc.lineSequence()) {
            if (line.isBlank()) continue
            if (line[0] == '\t') return "\t"
            if (line[0] == ' ') {
                val n = line.takeWhile { it == ' ' }.length
                if (n in 1..8) return " ".repeat(n)
            }
        }
        return fallback
    }

    private fun write(n: Node, indent: String?, level: Int, sb: StringBuilder, sort: SortMode?) {
        when (n.kind) {
            Kind.OBJECT, Kind.ARRAY -> {
                val open = if (n.kind == Kind.OBJECT) '{' else '['
                val close = if (n.kind == Kind.OBJECT) '}' else ']'
                if (n.children.isEmpty()) { sb.append(open).append(close); return }
                var kids: List<Node> = n.children
                if (sort != null && n.kind == Kind.OBJECT) {
                    val cmp = Comparator<Node> { a, b ->
                        val c = String.CASE_INSENSITIVE_ORDER.compare(a.key ?: "", b.key ?: "")
                        if (c != 0) c else (a.key ?: "").compareTo(b.key ?: "")
                    }
                    kids = kids.sortedWith(if (sort.descending) cmp.reversed() else cmp) // sortedWith مستقر
                }
                sb.append(open)
                for ((idx, k) in kids.withIndex()) {
                    if (idx > 0) sb.append(',')
                    if (indent != null) { sb.append('\n'); repeat(level + 1) { sb.append(indent) } }
                    if (n.kind == Kind.OBJECT) {
                        sb.append(k.keyRaw).append(if (indent != null) ": " else ":")
                    }
                    write(k, indent, level + 1, sb, if (sort != null && sort.deep) sort else null)
                }
                if (indent != null) { sb.append('\n'); repeat(level) { sb.append(indent) } }
                sb.append(close)
            }
            else -> sb.append(n.raw)
        }
    }

    data class SortMode(val deep: Boolean = true, val descending: Boolean = false)

    private fun render(root: Node, indent: String?, sort: SortMode?): String {
        val sb = StringBuilder()
        write(root, indent, 0, sb, sort)
        return sb.toString()
    }

    // ------------------------------------------------------------------ نطاق العملية

    /** نطاق العملية: التحديد إن كان JSON صالحاً بحد ذاته، وإلا المستند كاملاً. */
    private fun scope(doc: String, selStart: Int, selEnd: Int, comments: Boolean): Pair<Int, Int> {
        val s = minOf(selStart, selEnd).coerceIn(0, doc.length)
        val e = maxOf(selStart, selEnd).coerceIn(0, doc.length)
        if (s < e) {
            val sel = doc.substring(s, e)
            if (sel.isNotBlank() && validate(sel, comments) == null) return s to e
        }
        return 0 to doc.length
    }

    /** يطبّق [indent]/[sort] على النطاق؛ يحفظ الأسطر الفارغة اللاحقة (`\n` الأخير) كما هي. */
    private fun transform(
        doc: String, selStart: Int, selEnd: Int, comments: Boolean, indentOf: (String) -> String?, sort: SortMode? = null
    ): Result {
        val (s, e) = scope(doc, selStart, selEnd, comments)
        val src = doc.substring(s, e)
        // الفراغ المحيط يُحفظ: مسافات البداية/النهاية (مثل \n الأخير) لا تُلمس
        val lead = src.takeWhile { it.isWhitespace() }
        val trail = if (lead.length == src.length) "" else src.takeLastWhile { it.isWhitespace() }
        val core = src.substring(lead.length, src.length - trail.length)
        val parsed = try { parse(core, lenient = false, comments = comments) } catch (f: Fail) {
            // أخطاء داخل core: نحوّل الإزاحة إلى إزاحة في المستند كاملاً
            return Result(error = toError(doc, Fail(f.kind, s + lead.length + f.at)))
        }
        if (parsed.sawComments) return Result(hasComments = true)
        val out = render(parsed.root, indentOf(core), sort)
        if (out == core) return Result(noChange = true)
        val a = s + lead.length
        val b = e - trail.length
        val whole = s == 0 && e == doc.length
        if (!whole) return Result(SmartEdit.Edit(a, b, out, a, a + out.length))
        // مستند كامل: يبقى المؤشر عند الموضع نفسه منطقياً (يُحسب بعدد الحروف غير الفراغية قبله)
        val caret = mapCaret(core, (maxOf(selStart, selEnd) - a).coerceIn(0, core.length), out)
        return Result(SmartEdit.Edit(a, b, out, a + caret, a + caret))
    }

    /** موضع في [newText] يقابل [caret] في [oldText] بعدد الحروف غير الفراغية السابقة. */
    private fun mapCaret(oldText: String, caret: Int, newText: String): Int {
        var n = 0
        for (i in 0 until caret) if (!oldText[i].isWhitespace()) n++
        if (n == 0) return 0
        var seen = 0
        for (i in newText.indices) {
            if (!newText[i].isWhitespace()) {
                seen++
                if (seen == n) {
                    var p = i + 1
                    // المؤشر كان قبل رمز (لا فراغ): نتجاوز الفراغ المُضاف حتى لا يقع قبل مسافة التنسيق
                    if (caret < oldText.length && !oldText[caret].isWhitespace()) {
                        while (p < newText.length && newText[p].isWhitespace()) p++
                    }
                    return p
                }
            }
        }
        return newText.length
    }

    /** تنسيق (طباعة منسّقة). [indent] null = المستخرجة من المستند ثم [fallback]. */
    fun pretty(doc: String, selStart: Int, selEnd: Int, indent: String? = null, comments: Boolean = false, fallback: String = "  "): Result =
        transform(doc, selStart, selEnd, comments, { indent ?: detectIndent(doc, fallback) })

    /** تصغير: يزيل كل مسافة غير لازمة. */
    fun minify(doc: String, selStart: Int, selEnd: Int, comments: Boolean = false): Result =
        transform(doc, selStart, selEnd, comments, { null })

    /**
     * ترتيب المفاتيح أبجدياً (بلا حساسية لحالة الأحرف ثم بالترتيب الحرفي) — [SortMode.deep] لكل المستويات.
     * الناتج منسّق بمسافة المستند، أو مصغَّر إن كان المصدر سطراً واحداً.
     */
    fun sortKeys(doc: String, selStart: Int, selEnd: Int, mode: SortMode = SortMode(), comments: Boolean = false, fallback: String = "  "): Result =
        transform(doc, selStart, selEnd, comments, { core -> if (core.contains('\n')) detectIndent(doc, fallback) else null }, mode)

    /**
     * إصلاح الأخطاء الشائعة: تعليقات `//` و`/* */`، فاصلة أخيرة، علامات اقتباس مفردة، مفاتيح بلا اقتباس،
     * وقيم True/False/None/undefined. الناتج منسّق. يعيد noChange إن كان المستند صالحاً أصلاً.
     */
    fun repair(doc: String, fallback: String = "  "): Result {
        if (doc.isBlank()) return Result(error = JsonError(ErrKind.EMPTY, 0, 1, 1, null))
        val parsed = try { parse(doc, lenient = true, comments = true) } catch (f: Fail) { return Result(error = toError(doc, f)) }
        if (!parsed.sawComments && !parsed.repaired) return Result(noChange = true)
        val trail = if (doc.endsWith("\n")) "\n" else ""
        val out = render(parsed.root, detectIndent(doc, fallback), null) + trail
        if (out == doc) return Result(noChange = true)
        return Result(SmartEdit.Edit(0, doc.length, out, 0, 0))
    }

    // ------------------------------------------------------------------ المسارات

    private fun isIdent(k: String): Boolean =
        k.isNotEmpty() && (k[0].isLetter() || k[0] == '_' || k[0] == '$') && k.all { it.isLetterOrDigit() || it == '_' || it == '$' }

    private fun segment(parent: Node, child: Node, index: Int): String =
        if (parent.kind == Kind.ARRAY) "[$index]"
        else child.key!!.let { if (isIdent(it)) ".$it" else "[${quote(it)}]" }

    private fun within(c: Node, offset: Int) = offset >= c.segStart && offset <= c.end

    /** مسار JSON (على نمط JSONPath: `$.a.b[2]["x y"]`) للقيمة تحت [offset]؛ null إن كان المستند غير صالح. */
    fun pathAt(doc: String, offset: Int, comments: Boolean = false): String? {
        val root = try { parse(doc, comments = comments).root } catch (f: Fail) { return null }
        return pathOf(root, offset.coerceIn(0, doc.length))
    }

    private fun pathOf(root: Node, offset: Int): String {
        val sb = StringBuilder("$")
        var node = root
        while (node.kind == Kind.OBJECT || node.kind == Kind.ARRAY) {
            val idx = node.children.indexOfFirst { within(it, offset) }
            if (idx < 0) break
            val c = node.children[idx]
            sb.append(segment(node, c, idx))
            node = c
        }
        return sb.toString()
    }

    /** رموز مسار: اسم مفتاح (String) أو فهرس (Int). null إن كانت الصياغة خاطئة. */
    private fun parsePath(path: String): List<Any>? {
        val p = path.trim()
        var i = 0
        if (p.startsWith("$")) i = 1
        val out = ArrayList<Any>()
        var first = true
        while (i < p.length) {
            val c = p[i]
            when {
                c == '.' -> {
                    i++
                    val st = i
                    while (i < p.length && p[i] != '.' && p[i] != '[') i++
                    if (i == st) return null
                    val t = p.substring(st, i)
                    out.add(t.toIntOrNull() ?: t)
                }
                c == '[' -> {
                    i++
                    if (i >= p.length) return null
                    if (p[i] == '"' || p[i] == '\'') {
                        val q = p[i]
                        val st = i
                        i++
                        while (i < p.length && p[i] != q) { if (p[i] == '\\') i++; i++ }
                        if (i >= p.length) return null
                        val lit = p.substring(st, i + 1)
                        i++
                        out.add(if (q == '"') decode(lit) else lit.substring(1, lit.length - 1))
                    } else {
                        val st = i
                        while (i < p.length && p[i] != ']') i++
                        if (i >= p.length) return null
                        out.add(p.substring(st, i).trim().toIntOrNull() ?: return null)
                    }
                    if (i >= p.length || p[i] != ']') return null
                    i++
                }
                first && i == 0 -> { // مسار بلا «$.»: users[0].name
                    val st = i
                    while (i < p.length && p[i] != '.' && p[i] != '[') i++
                    val t = p.substring(st, i)
                    out.add(t.toIntOrNull() ?: t)
                }
                else -> return null
            }
            first = false
        }
        return out
    }

    /** إزاحة بداية العنصر المسمّى بـ[path] (مفتاحه إن كان عضواً)؛ null إن لم يوجد أو كانت الصياغة/المستند خاطئين. */
    fun offsetOfPath(doc: String, path: String, comments: Boolean = false): Int? {
        val root = try { parse(doc, comments = comments).root } catch (f: Fail) { return null }
        val toks = parsePath(path) ?: return null
        var node = root
        for (t in toks) {
            node = when {
                node.kind == Kind.ARRAY && t is Int -> node.children.getOrNull(t) ?: return null
                node.kind == Kind.OBJECT -> {
                    val k = t.toString()
                    node.children.firstOrNull { it.key == k } ?: return null
                }
                else -> return null
            }
        }
        return node.segStart
    }

    // ------------------------------------------------------------------ المخطط

    /** سطر في المخطط الهيكلي: [depth] مستوى الإزاحة، [label] نص العرض، [offset] موضع الانتقال. */
    data class OutlineItem(val depth: Int, val label: String, val offset: Int, val path: String)

    private fun preview(n: Node): String = when (n.kind) {
        Kind.OBJECT -> "{${n.children.size}}"
        Kind.ARRAY -> "[${n.children.size}]"
        Kind.STRING -> decode(n.raw).replace('\n', ' ').let { if (it.length > 40) it.take(40) + "…" else it }.let { "\"$it\"" }
        else -> n.raw
    }

    /**
     * مخطط المستند: كل مفتاح/عنصر بقيمته أو حجمه، بإزاحة حسب العمق. للحماية من المستندات الضخمة:
     * أول [perContainer] ابناً لكل حاوية و[maxItems] سطراً كحد أقصى. null إن كان المستند غير صالح.
     */
    fun outline(doc: String, comments: Boolean = false, perContainer: Int = 40, maxItems: Int = 600): List<OutlineItem>? {
        val root = try { parse(doc, comments = comments).root } catch (f: Fail) { return null }
        val out = ArrayList<OutlineItem>()
        fun visit(n: Node, depth: Int, path: String) {
            if (n.kind != Kind.OBJECT && n.kind != Kind.ARRAY) return
            for ((idx, c) in n.children.withIndex()) {
                if (out.size >= maxItems) return
                if (idx >= perContainer) {
                    out.add(OutlineItem(depth, "… +${n.children.size - perContainer}", c.segStart, path))
                    return
                }
                val seg = segment(n, c, idx)
                val label = (if (n.kind == Kind.ARRAY) "[$idx]" else c.key!!) +
                    if (c.kind == Kind.OBJECT || c.kind == Kind.ARRAY) "  ${preview(c)}" else ": ${preview(c)}"
                out.add(OutlineItem(depth, label, c.segStart, path + seg))
                visit(c, depth + 1, path + seg)
            }
        }
        visit(root, 0, "$")
        return out
    }

    // ------------------------------------------------------------------ الإحصاءات

    data class Stats(
        val objects: Int, val arrays: Int, val keys: Int, val strings: Int, val numbers: Int,
        val bools: Int, val nulls: Int, val maxDepth: Int, val bytes: Int, val minifiedBytes: Int, val lines: Int
    )

    fun stats(doc: String, comments: Boolean = false): Stats? {
        val root = try { parse(doc, comments = comments).root } catch (f: Fail) { return null }
        var o = 0; var a = 0; var k = 0; var s = 0; var n = 0; var b = 0; var nl = 0; var depth = 0
        fun walk(x: Node, d: Int) {
            if (d > depth) depth = d
            when (x.kind) {
                Kind.OBJECT -> { o++; k += x.children.size; x.children.forEach { walk(it, d + 1) } }
                Kind.ARRAY -> { a++; x.children.forEach { walk(it, d + 1) } }
                Kind.STRING -> s++
                Kind.NUMBER -> n++
                Kind.BOOL -> b++
                Kind.NULL -> nl++
            }
        }
        walk(root, 1)
        return Stats(
            o, a, k, s, n, b, nl, depth,
            doc.toByteArray(Charsets.UTF_8).size,
            render(root, null, null).toByteArray(Charsets.UTF_8).size,
            doc.count { it == '\n' } + 1
        )
    }

    // ------------------------------------------------------------------ سلاسل: escape / unescape

    /** نطاق سلسلة JSON (بعلامتي اقتباس) في سطر المؤشر تحتوي [pos]؛ null إن لم يكن المؤشر داخل سلسلة. */
    private fun stringAt(doc: String, pos: Int): IntRange? {
        val p = pos.coerceIn(0, doc.length)
        val ls = doc.lastIndexOf('\n', p - 1) + 1
        var le = doc.indexOf('\n', p); if (le < 0) le = doc.length
        var i = ls
        while (i < le) {
            if (doc[i] == '"') {
                val st = i
                i++
                while (i < le && doc[i] != '"') { if (doc[i] == '\\') i++; i++ }
                if (i >= le) return null
                if (p in st..i) return st..i
                i++
            } else i++
        }
        return null
    }

    /**
     * تبديل «سلسلة JSON»: تحديد يمثّل سلسلة JSON كاملة (أو مؤشر داخل واحدة) يُفكّ هروبه إلى نصّه الخام؛
     * وأي نص آخر محدَّد يُحوَّل إلى سلسلة JSON مهروبة بعلامات اقتباس. بلا تحديد ولا سلسلة → null.
     */
    fun toggleStringLiteral(doc: String, selStart: Int, selEnd: Int): SmartEdit.Edit? {
        var s = minOf(selStart, selEnd).coerceIn(0, doc.length)
        var e = maxOf(selStart, selEnd).coerceIn(0, doc.length)
        if (s == e) {
            val r = stringAt(doc, s) ?: return null
            s = r.first; e = r.last + 1
        }
        val sel = doc.substring(s, e)
        val t = sel.trim()
        val isLiteral = t.length >= 2 && t[0] == '"' && t.last() == '"' && validate(t) == null
        if (isLiteral) {
            val lead = sel.length - sel.trimStart().length
            val trail = sel.length - sel.trimEnd().length
            val plain = decode(t)
            val a = s + lead; val b = e - trail
            return SmartEdit.Edit(a, b, plain, a, a + plain.length)
        }
        if (sel.isEmpty()) return null
        val q = quote(sel)
        return SmartEdit.Edit(s, e, q, s, s + q.length)
    }

    // ------------------------------------------------------------------ إدراج

    private fun lineStartOf(doc: String, pos: Int) = doc.lastIndexOf('\n', pos - 1) + 1

    private fun leadingWs(doc: String, pos: Int): String {
        val ls = lineStartOf(doc, pos)
        return doc.substring(ls).takeWhile { it == ' ' || it == '\t' }
    }

    /** آخر موضع بعد حرف غير فراغي قبل [pos]؛ -1 إن لم يوجد. */
    private fun prevNonWsEnd(doc: String, pos: Int): Int {
        var i = pos
        while (i > 0 && doc[i - 1].isWhitespace()) i--
        return if (i == 0) -1 else i
    }

    private fun isValueEnd(c: Char) = c == '"' || c == '}' || c == ']' || c in '0'..'9' || c == 'e' || c == 'l'

    /** هل يلزم فاصلة قبل عنصر جديد يُدرَج عند [pos]؟ (السابق قيمة منتهية لا `{` `[` `,` `:`). */
    private fun needsCommaBefore(doc: String, pos: Int): Boolean {
        val e = prevNonWsEnd(doc, pos)
        return e > 0 && isValueEnd(doc[e - 1]) && !inString(doc, e - 1)
    }

    /** هل الحرف عند [idx] جزء من سلسلة (تُحسب علامات الاقتباس غير المهروبة في سطره)؟ تقريبي لكنه كافٍ للقيم الختامية. */
    private fun inString(doc: String, idx: Int): Boolean {
        val ls = lineStartOf(doc, idx + 1)
        var inside = false
        var i = ls
        while (i < idx) {
            val c = doc[i]
            if (c == '\\' && inside) { i += 2; continue }
            if (c == '"') inside = !inside
            i++
        }
        // الحرف نفسه علامة اقتباس ختامية: السلسلة كانت مفتوحة قبلها وليست «داخل» بعدها
        return if (doc[idx] == '"') false else inside
    }

    /**
     * يستبدل [s,e) بـ[text] مع فاصلة قبله عند الحاجة (توضع بعد آخر قيمة مباشرة لا بعد الفراغ).
     * [selOff]/[selLen] موضع التحديد داخل [text].
     */
    private fun insertItem(doc: String, s: Int, e: Int, text: String, selOff: Int, selLen: Int): SmartEdit.Edit {
        if (needsCommaBefore(doc, s)) {
            val pe = prevNonWsEnd(doc, s)
            val between = doc.substring(pe, s)
            val full = "," + between + text
            val base = pe + 1 + between.length + selOff
            return SmartEdit.Edit(pe, e, full, base, base + selLen)
        }
        return SmartEdit.Edit(s, e, text, s + selOff, s + selOff + selLen)
    }

    /** يدرج `"key": ""` ويحدّد كلمة key لتُستبدل بالكتابة. */
    fun insertPair(doc: String, selStart: Int, selEnd: Int, keyPlaceholder: String = "key"): SmartEdit.Edit {
        val s = minOf(selStart, selEnd).coerceIn(0, doc.length)
        val e = maxOf(selStart, selEnd).coerceIn(0, doc.length)
        val text = "\"$keyPlaceholder\": \"\""
        return insertItem(doc, s, e, text, 1, keyPlaceholder.length)
    }

    /** يدرج `{}` أو `[]` ويضع المؤشر داخله. */
    fun insertContainer(doc: String, selStart: Int, selEnd: Int, isObject: Boolean): SmartEdit.Edit {
        val s = minOf(selStart, selEnd).coerceIn(0, doc.length)
        val e = maxOf(selStart, selEnd).coerceIn(0, doc.length)
        val text = if (isObject) "{}" else "[]"
        return insertItem(doc, s, e, text, 1, 0)
    }

    // ------------------------------------------------------------------ Enter

    /**
     * Enter عند نهاية سطر ينتهي بقيمة وبعده (في السطر التالي غير الفارغ) عنصر شقيق بلا فاصلة: يضيف الفاصلة
     * تلقائياً ثم يفتح سطراً جديداً بنفس الإزاحة. null في كل حالة أخرى فيعمل Enter العادي.
     */
    fun continueComma(doc: String, pos: Int): SmartEdit.Edit? {
        val p = pos.coerceIn(0, doc.length)
        var le = doc.indexOf('\n', p); if (le < 0) le = doc.length
        if (doc.substring(p, le).isNotBlank()) return null
        val ls = lineStartOf(doc, p)
        val before = doc.substring(ls, p).trimEnd()
        if (before.isEmpty()) return null
        val last = before.last()
        if (!isValueEnd(last)) return null
        if (before.contains("//")) return null
        if (inString(doc, ls + before.length - 1)) return null
        // الحرف الأول من أول سطر غير فارغ بعده
        var i = le
        var first: Char? = null
        while (i < doc.length) {
            val c = doc[i]
            if (!c.isWhitespace()) { first = c; break }
            i++
        }
        val f = first ?: return null
        if (!(f == '"' || f == '{' || f == '[' || f == '-' || f in '0'..'9' || f == 't' || f == 'f' || f == 'n')) return null
        val indent = leadingWs(doc, p)
        val text = ",\n$indent"
        val at = ls + before.length
        val cursor = at + text.length
        // يُستبدل [at, p) (فراغ ذيلي) بالفاصلة والسطر الجديد
        return SmartEdit.Edit(at, p, text, cursor, cursor)
    }

    // ------------------------------------------------------------------ اللغة

    private val JSON_EXTS = setOf("json", "jsonc", "geojson", "webmanifest", "har")

    fun isJsonExtension(ext: String): Boolean = ext.lowercase().removePrefix(".") in JSON_EXTS

    /** JSONC يسمح بالتعليقات. */
    fun allowsComments(ext: String): Boolean = ext.lowercase().removePrefix(".") == "jsonc"
}

package com.dlof.rinlang

/**
 * منسّق/مرتّب الكود لكل اللغات غير Rin (Rin له منسّقه الأصلي في محرك C++:
 * `RinCodeEditorView.formatDocument` → `engine.reindentDocument`).
 *
 * مبدأ التصميم: **تنسيق آمن غير مُدمِّر**. المنسّق يعيد المسافات البادئة وفواصل الأسطر فقط؛ لا يغيّر
 * أي حرف غير فراغي (ما عدا [sortImports] الذي يعيد ترتيب أسطر الاستيراد صراحةً). لذلك:
 *  - لا يلمس محتوى النصوص الممتدة (`"""…"""`، `` `…` ``) ولا التعليقات الكتلية إلا لمحاذاة `*`.
 *  - يعتمد على [PolyglotSyntax] ليعرف أي قوس حقيقي وأيّها داخل نص أو تعليق.
 *  - idempotent: تنسيق نص منسَّق يعطي النص نفسه.
 *
 * ما يفعله حسب اللغة:
 *  - لغات الأقواس (C/C++/C#/Java/Kotlin/Swift/Go/Rust/Dart/JS/TS/PHP/Gradle/CSS): إزاحة حسب عمق
 *    `() [] {}`، سطر يبدأ بقوس إغلاق يرجع مستوى، سلاسل `.method()` و`&&`/`||` المتتابعة بمستوى إضافي،
 *    المعالج المسبق `#include` في العمود 0، محاذاة `*` في تعليقات الكتل.
 *  - JSON: طباعة منسّقة كاملة (يفتح JSON المصغّر)، والمصفوفات القصيرة من القيم البسيطة تبقى في سطر.
 *  - HTML/XML/SVG: إزاحة حسب تداخل الوسوم (مع تجاهل الوسوم الفارغة void وما داخل pre/script/style).
 *  - الكل: حذف المسافات الزائدة، تحويل التبويبات، دمج الأسطر الفارغة المتتالية (حدّ أقصى 2)،
 *    وإنهاء الملف بسطر جديد واحد.
 */
object CodeFormatter {

    data class Options(
        val indent: Int = 4,
        val useTabs: Boolean = false,
        val sortImports: Boolean = false,
        val finalNewline: Boolean = true
    )

    private val BRACE_LANGS = setOf(
        "c", "h", "cpp", "cc", "cxx", "hpp", "hh", "ino", "cs", "java", "kt", "kts", "swift", "go", "rs",
        "dart", "js", "mjs", "cjs", "jsx", "ts", "tsx", "php", "gradle", "groovy", "css", "scss", "less"
    )
    private val MARKUP_LANGS = setOf("html", "htm", "xhtml", "xml", "svg", "xaml", "plist", "vue")
    private val JSON_LANGS = setOf("json", "jsonc")
    private val TIDY_ONLY = setOf("py", "rb", "lua", "sql", "sh", "bash", "zsh", "yml", "yaml", "toml", "ini", "properties", "cfg", "md", "markdown", "txt")

    fun supports(extension: String): Boolean {
        val e = norm(extension)
        return e in BRACE_LANGS || e in MARKUP_LANGS || e in JSON_LANGS || e in TIDY_ONLY
    }

    private fun norm(extension: String) = extension.lowercase().removePrefix(".")

    /** يعيد النص المنسَّق، أو null إن لم تكن اللغة مدعومة. */
    fun format(text: String, extension: String, options: Options = Options()): String? {
        val e = norm(extension)
        val src = text.replace("\r\n", "\n").replace('\r', '\n')
        val body = when {
            e in JSON_LANGS -> formatJson(src, e, options)
            e in MARKUP_LANGS -> formatMarkup(src, e, options)
            e in BRACE_LANGS -> formatBraces(src, e, options)
            e in TIDY_ONLY -> tidyOnly(src, e, options)
            else -> return null
        }
        return finish(body, options)
    }

    // ------------------------------------------------------------------------------------
    // مشترك
    // ------------------------------------------------------------------------------------

    private fun indentStr(level: Int, o: Options): String =
        if (level <= 0) "" else if (o.useTabs) "\t".repeat(level) else " ".repeat(level * o.indent.coerceIn(1, 16))

    /** حذف المسافات الزائدة تم عند بناء الأسطر؛ هنا دمج الفراغات وإنهاء الملف. */
    private fun finish(text: String, o: Options): String {
        val lines = text.split("\n")
        val out = ArrayList<String>(lines.size)
        var blanks = 0
        for (l in lines) {
            if (l.isEmpty()) { blanks++; if (blanks <= 2) out.add(l) } else { blanks = 0; out.add(l) }
        }
        var r = out.joinToString("\n")
        // لا نمسّ الفراغات النهائية داخل نص ممتد مفتوح؛ نقصّ فقط سطوراً فارغة تماماً في النهاية
        r = r.trimEnd('\n')
        if (r.isEmpty()) return ""
        return if (o.finalNewline) r + "\n" else r
    }

    private fun tidyOnly(src: String, ext: String, o: Options): String {
        val lines = src.split("\n")
        val cont = HashSet<Int>()
        // لا نعرف نصوصاً ممتدة لكل لغة هنا؛ نكتفي بالتبويبات البادئة والمسافات الزائدة (مع استثناء Markdown: سطر
        // ينتهي بمسافتين = فاصل سطر مقصود)
        val md = ext == "md" || ext == "markdown"
        val res = lines.map { l ->
            var line = l
            // تبويبات بادئة -> مسافات
            var k = 0
            while (k < line.length && (line[k] == ' ' || line[k] == '\t')) k++
            val lead = line.substring(0, k)
            if (lead.indexOf('\t') >= 0 && !o.useTabs) line = expandLead(lead, o.indent) + line.substring(k)
            if (md && line.endsWith("  ") && line.trim().isNotEmpty()) line.trimEnd() + "  " else line.trimEnd()
        }
        if (o.sortImports && (ext == "py")) return sortPythonImports(res).joinToString("\n")
        cont.clear()
        return res.joinToString("\n")
    }

    private fun expandLead(lead: String, tab: Int): String {
        val sb = StringBuilder(); var col = 0; val t = tab.coerceIn(1, 16)
        for (c in lead) { if (c == '\t') { val n = t - col % t; repeat(n) { sb.append(' ') }; col += n } else { sb.append(' '); col++ } }
        return sb.toString()
    }

    // ------------------------------------------------------------------------------------
    // لغات الأقواس
    // ------------------------------------------------------------------------------------

    private fun isOpen(c: Char) = c == '(' || c == '[' || c == '{'
    private fun isClose(c: Char) = c == ')' || c == ']' || c == '}'
    private fun isBracketKind(k: Int) = k == HighlightKind.BRACKET0 || k == HighlightKind.BRACKET1 || k == HighlightKind.BRACKET2

    private fun formatBraces(src: String, ext: String, o: Options): String {
        var lines = src.split("\n")
        if (o.sortImports && ext in setOf("kt", "kts", "java", "dart", "swift")) lines = sortImportBlocks(lines)
        val res = PolyglotSyntax.analyze(lines, ext)
        val byLine = res.spans.groupBy { it.line }
        val out = ArrayList<String>(lines.size)
        var depth = 0
        for ((i, raw) in lines.withIndex()) {
            val spans = byLine[i]?.sortedBy { it.startCol } ?: emptyList()
            val inCont = i in res.continuation
            val t = raw.trim()
            if (inCont) {
                // سطر يبدأ داخل تعليق/نص ممتد
                val firstIsComment = spans.firstOrNull()?.let { it.startCol == 0 && it.kind == HighlightKind.COMMENT } == true
                if (firstIsComment && t.startsWith("*")) {
                    out.add(indentStr(depth, o) + " " + t)      // محاذاة نجمة تعليق الكتلة
                } else {
                    out.add(raw)                                 // محتوى نص/تعليق ممتد: كما هو بالضبط
                }
                depth = applyBrackets(raw, spans, depth)
                continue
            }
            if (t.isEmpty()) { out.add(""); continue }

            // معالج مسبق: العمود 0
            val isPre = spans.firstOrNull()?.let { it.kind == HighlightKind.PREPROCESSOR && raw.indexOf(t) == it.startCol } == true && t.startsWith("#")
            // عدد أقواس الإغلاق في بداية السطر (قبل أي شيء آخر)
            var lead = 0
            run {
                var pos = raw.length - raw.trimStart().length
                while (pos < raw.length) {
                    val ch = raw[pos]
                    if (ch.isWhitespace()) { pos++; continue }
                    if (isClose(ch) && spans.any { it.startCol == pos && isBracketKind(it.kind) }) { lead++; pos++; continue }
                    break
                }
            }
            var level = (depth - lead).coerceAtLeast(0)
            // سلاسل متتابعة تُزاح مستوى إضافياً
            val isStyle = ext == "css" || ext == "scss" || ext == "less"
            if (!isStyle && (t.startsWith(".") && !t.startsWith("..") || t.startsWith("?.") || t.startsWith("&&") || t.startsWith("||"))) level += 1
            out.add(if (isPre) t else indentStr(level, o) + t)
            depth = applyBrackets(raw, spans, depth)
        }
        return out.joinToString("\n")
    }

    /** يطبّق أقواس السطر (الحقيقية فقط) على [depth] ويرجع العمق الجديد. */
    private fun applyBrackets(raw: String, spans: List<PolySpan>, depth: Int): Int {
        var d = depth
        for (s in spans) {
            if (!isBracketKind(s.kind) || s.endCol - s.startCol != 1) continue
            val ch = raw[s.startCol]
            if (isOpen(ch)) d++ else if (isClose(ch)) d = (d - 1).coerceAtLeast(0)
        }
        return d
    }

    // ------------------------------------------------------------------------------------
    // ترتيب الاستيراد (kt/java/dart/swift): كل كتلة متجاورة من أسطر import تُرتَّب أبجدياً وتُزال تكراراتها
    // ------------------------------------------------------------------------------------

    private fun sortImportBlocks(lines: List<String>): List<String> {
        val out = ArrayList<String>(lines.size)
        var i = 0
        while (i < lines.size) {
            if (lines[i].trimStart().startsWith("import ")) {
                var j = i
                while (j < lines.size && lines[j].trimStart().startsWith("import ")) j++
                out.addAll(lines.subList(i, j).map { it.trim() }.distinct().sortedBy { it.removePrefix("import ").removePrefix("static ").lowercase() })
                i = j
            } else { out.add(lines[i]); i++ }
        }
        return out
    }

    private fun sortPythonImports(lines: List<String>): List<String> {
        val out = ArrayList<String>(lines.size)
        var i = 0
        fun simpleImport(l: String) = (l.startsWith("import ") || l.startsWith("from ")) && !l.contains('(') && !l.endsWith("\\")
        while (i < lines.size) {
            if (simpleImport(lines[i])) {
                var j = i
                while (j < lines.size && simpleImport(lines[j])) j++
                out.addAll(lines.subList(i, j).distinct().sortedBy { it.lowercase() })
                i = j
            } else { out.add(lines[i]); i++ }
        }
        return out
    }

    // ------------------------------------------------------------------------------------
    // JSON
    // ------------------------------------------------------------------------------------

    private class JsonFail : Exception()

    private sealed class J {
        class Obj(val keys: List<String>, val vals: List<J>) : J()
        class Arr(val items: List<J>) : J()
        class Lit(val raw: String) : J()
    }

    private class JsonParser(val s: String) {
        var i = 0
        fun ws() { while (i < s.length && s[i].isWhitespace()) i++ }
        fun parseTop(): J { ws(); val v = value(); ws(); if (i != s.length) throw JsonFail(); return v }
        fun value(): J {
            ws()
            if (i >= s.length) throw JsonFail()
            return when (s[i]) {
                '{' -> {
                    i++; val ks = ArrayList<String>(); val vs = ArrayList<J>(); ws()
                    if (i < s.length && s[i] == '}') { i++; return J.Obj(ks, vs) }
                    while (true) {
                        ws(); if (i >= s.length || s[i] != '"') throw JsonFail()
                        ks.add(str()); ws()
                        if (i >= s.length || s[i] != ':') throw JsonFail()
                        i++; vs.add(value()); ws()
                        if (i >= s.length) throw JsonFail()
                        if (s[i] == ',') { i++; continue }
                        if (s[i] == '}') { i++; break }
                        throw JsonFail()
                    }
                    J.Obj(ks, vs)
                }
                '[' -> {
                    i++; val xs = ArrayList<J>(); ws()
                    if (i < s.length && s[i] == ']') { i++; return J.Arr(xs) }
                    while (true) {
                        xs.add(value()); ws()
                        if (i >= s.length) throw JsonFail()
                        if (s[i] == ',') { i++; continue }
                        if (s[i] == ']') { i++; break }
                        throw JsonFail()
                    }
                    J.Arr(xs)
                }
                '"' -> J.Lit(str())
                else -> {
                    val st = i
                    while (i < s.length && !s[i].isWhitespace() && s[i] !in ",]}:") i++
                    if (i == st) throw JsonFail()
                    val lit = s.substring(st, i)
                    if (!(lit == "true" || lit == "false" || lit == "null" || lit.toDoubleOrNull() != null)) throw JsonFail()
                    J.Lit(lit)
                }
            }
        }
        fun str(): String {
            val st = i; i++
            while (i < s.length) {
                val c = s[i]
                if (c == '\\') { i += 2; continue }
                if (c == '"') { i++; return s.substring(st, i) }
                if (c == '\n') throw JsonFail()
                i++
            }
            throw JsonFail()
        }
    }

    private fun formatJson(src: String, ext: String, o: Options): String {
        val trimmed = src.trim()
        if (trimmed.isEmpty()) return ""
        // ملفات jsonc (تعليقات) لا تُحلَّل بصرامة: نعيد الإزاحة بالأقواس فقط
        val parsed = try { JsonParser(trimmed).parseTop() } catch (e: JsonFail) { null }
        if (parsed == null) return formatBraces(src, "json", o)
        val sb = StringBuilder()
        writeJson(parsed, 0, o, sb)
        return sb.toString()
    }

    private fun inlineJson(v: J): String? = when (v) {
        is J.Lit -> v.raw
        is J.Arr -> if (v.items.all { it is J.Lit }) v.items.joinToString(", ", "[", "]") { (it as J.Lit).raw } else null
        is J.Obj -> null
    }

    private fun writeJson(v: J, level: Int, o: Options, sb: StringBuilder) {
        when (v) {
            is J.Lit -> sb.append(v.raw)
            is J.Arr -> {
                if (v.items.isEmpty()) { sb.append("[]"); return }
                val inl = inlineJson(v)
                if (inl != null && inl.length <= 72) { sb.append(inl); return }
                sb.append("[\n")
                for ((idx, it) in v.items.withIndex()) {
                    sb.append(indentStr(level + 1, o)); writeJson(it, level + 1, o, sb)
                    if (idx < v.items.size - 1) sb.append(',')
                    sb.append('\n')
                }
                sb.append(indentStr(level, o)).append(']')
            }
            is J.Obj -> {
                if (v.keys.isEmpty()) { sb.append("{}"); return }
                sb.append("{\n")
                for (idx in v.keys.indices) {
                    sb.append(indentStr(level + 1, o)).append(v.keys[idx]).append(": ")
                    writeJson(v.vals[idx], level + 1, o, sb)
                    if (idx < v.keys.size - 1) sb.append(',')
                    sb.append('\n')
                }
                sb.append(indentStr(level, o)).append('}')
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // HTML / XML
    // ------------------------------------------------------------------------------------

    private val VOID = setOf("area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr")
    private val RAW_BLOCK = setOf("pre", "textarea", "script", "style")
    private val P_CLOSERS = setOf("p", "div", "ul", "ol", "dl", "table", "pre", "blockquote", "form", "hr", "fieldset", "address",
        "section", "article", "header", "footer", "nav", "aside", "h1", "h2", "h3", "h4", "h5", "h6", "figure", "main")
    private val TAG_OPEN = Regex("<([A-Za-z_][\\w:.-]*)")
    private val TAG_CLOSE = Regex("</\\s*([A-Za-z_][\\w:.-]*)\\s*>?")

    private class MarkupState(val html: Boolean) {
        val stack = ArrayList<String>()
        var inComment = false
        var rawBlock: String? = null     // داخل pre/script/style/textarea
        var pending: String? = null      // وسم مفتوح ينتظر '>' في سطر لاحق
        var pendingLevel = 0

        fun nm(s: String) = if (html) s.lowercase() else s

        fun popTo(name: String) {
            val idx = stack.lastIndexOf(name)
            if (idx >= 0) while (stack.size > idx) stack.removeAt(stack.size - 1)
        }

        /** ينهي وسماً مفتوحاً: يدفع اسمه إلا إن كان ذاتي الإغلاق أو void. */
        fun finishOpen(name: String, selfClosing: Boolean) {
            if (html) implicitClose(name)
            if (selfClosing || (html && name in VOID)) return
            stack.add(name)
        }

        private fun top() = if (stack.isEmpty()) "" else stack[stack.size - 1]

        /** إغلاق ضمني لوسوم HTML الاختيارية (p, li, td, tr, option, dt/dd) كي لا يتراكم العمق. */
        fun implicitClose(name: String) {
            fun pop() { stack.removeAt(stack.size - 1) }
            when (name) {
                "li" -> if (top() == "li") pop()
                "dt", "dd" -> if (top() == "dt" || top() == "dd") pop()
                "option" -> if (top() == "option") pop()
                "td", "th" -> if (top() == "td" || top() == "th") pop()
                "tr" -> { while (top() == "td" || top() == "th") pop(); if (top() == "tr") pop() }
            }
            if (top() == "p" && name in P_CLOSERS) pop()
        }

        /** أول '>' خارج علامات التنصيص بدءاً من [from]، أو -1. */
        fun findClose(t: String, from: Int): Int {
            var q = '\u0000'
            var j = from
            while (j < t.length) {
                val c = t[j]
                if (q != '\u0000') { if (c == q) q = '\u0000' }
                else if (c == '"' || c == '\'') q = c
                else if (c == '>') return j
                j++
            }
            return -1
        }

        /** يمسح الوسوم من [from] ويحدّث الحالة. */
        fun scan(t: String, from: Int, level: Int) {
            var i = from
            while (i < t.length) {
                val lt = t.indexOf('<', i)
                if (lt == -1) return
                when {
                    t.startsWith("<!--", lt) -> {
                        val end = t.indexOf("-->", lt + 4)
                        if (end == -1) { inComment = true; return }
                        i = end + 3
                    }
                    t.startsWith("<![CDATA[", lt) -> { val end = t.indexOf("]]>", lt); if (end == -1) return; i = end + 3 }
                    t.startsWith("<!", lt) || t.startsWith("<?", lt) -> { val end = t.indexOf('>', lt); if (end == -1) return; i = end + 1 }
                    t.startsWith("</", lt) -> {
                        val m = TAG_CLOSE.find(t, lt)
                        if (m != null && m.range.first == lt) popTo(nm(m.groupValues[1]))
                        val end = t.indexOf('>', lt)
                        if (end == -1) return
                        i = end + 1
                    }
                    else -> {
                        val m = TAG_OPEN.find(t, lt)
                        if (m == null || m.range.first != lt) { i = lt + 1; continue }
                        val name = nm(m.groupValues[1])
                        val close = findClose(t, m.range.last + 1)
                        if (close == -1) { pending = name; pendingLevel = level; return }
                        finishOpen(name, t[close - 1] == '/')
                        if (html && name in RAW_BLOCK && !(t[close - 1] == '/')) {
                            if (t.lowercase().indexOf("</$name", close) == -1) { rawBlock = name; return }
                        }
                        i = close + 1
                    }
                }
            }
        }
    }

    private fun formatMarkup(src: String, ext: String, o: Options): String {
        val html = ext != "xml" && ext != "svg" && ext != "xaml" && ext != "plist"
        val st = MarkupState(html)
        val out = ArrayList<String>()
        for (raw in src.split("\n")) {
            val t = raw.trim()

            if (st.inComment) {
                out.add(raw.trimEnd())
                val end = t.indexOf("-->")
                if (end != -1) { st.inComment = false; st.scan(t, end + 3, st.stack.size) }
                continue
            }
            val rb = st.rawBlock
            if (rb != null) {
                val close = raw.lowercase().indexOf("</$rb")
                if (close == -1) { out.add(if (rb == "pre" || rb == "textarea") raw else raw.trimEnd()); continue }
                st.popTo(rb); st.rawBlock = null
                out.add(if (t.startsWith("</")) indentStr(st.stack.size, o) + t else raw.trimEnd())
                val gt = raw.indexOf('>', close)
                if (gt != -1) st.scan(raw, gt + 1, st.stack.size)
                continue
            }
            if (t.isEmpty()) { out.add(""); continue }

            val pend = st.pending
            if (pend != null) {
                out.add(indentStr(st.pendingLevel + 1, o) + t)
                val close = st.findClose(t, 0)
                if (close != -1) {
                    st.pending = null
                    val selfClosing = close > 0 && t[close - 1] == '/'
                    st.finishOpen(pend, selfClosing)
                    if (html && pend in RAW_BLOCK && !selfClosing && t.lowercase().indexOf("</$pend", close) == -1) { st.rawBlock = pend; continue }
                    st.scan(t, close + 1, st.stack.size)
                }
                continue
            }

            // وسوم إغلاق في بداية السطر تُنقص المستوى قبل طباعته
            var p = 0
            while (true) {
                val m = TAG_CLOSE.find(t, p)
                if (m == null || m.range.first != p || !t.substring(p).startsWith("</")) break
                st.popTo(st.nm(m.groupValues[1]))
                val gt = t.indexOf('>', p)
                if (gt == -1) { p = t.length; break }
                p = gt + 1
                while (p < t.length && t[p] == ' ') p++
            }
            // سطر يبدأ بوسم فتح يُغلق ضمنياً ما قبله (<li> بعد <li>، <p> بعد <p> …) فيُطبع بمستوى شقيقه
            if (html && p < t.length) {
                val om = TAG_OPEN.find(t, p)
                if (om != null && om.range.first == p) st.implicitClose(st.nm(om.groupValues[1]))
            }
            val level = st.stack.size
            out.add(indentStr(level, o) + t)
            st.scan(t, p, level)
        }
        return out.joinToString("\n")
    }
}

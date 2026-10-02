package com.dlof.rinlang

/**
 * جلسة REPL للـ terminal: منطق خالص (بلا Android ولا JNI) فيُختبر بـ JUnit.
 *
 * المحرك بلا حالة بين التشغيلات، فنحاكي الجلسة: التصريحات الناجحة (`let`/`fun`/`class`/...)
 * تُحفظ في [prelude] وتُقدَّم أمام كل أمر لاحق، فيعمل `let x = 5;` ثم `print x * 2;`.
 *
 * حدود معروفة (مقصودة): لا تُحفظ تصريحات `let` التي يبدو أن لها أثراً جانبياً أو تفاعلاً
 * (input/confirm/print/http/save/random...) كي لا تُنفَّذ ثانيةً مع كل أمر؛ وأرقام الأسطر في
 * رسائل الأخطاء تُحسب بعد الـ prelude.
 */
class RinReplSession {

    sealed class Feed {
        /** الأقواس/النصوص غير مكتملة: اعرض موجّه المتابعة وانتظر سطراً آخر. */
        object NeedMore : Feed()
        /** أمر مكتمل جاهز: [display] ما يُسجَّل في الـ terminal، [code] ما يُحفظ بعد النجاح. */
        data class Ready(val display: String, val code: String) : Feed()
    }

    private val pending = ArrayList<String>()
    private val prelude = ArrayList<Entry>()

    private class Entry(val key: String, val code: String)

    val isContinuing: Boolean get() = pending.isNotEmpty()

    fun cancelPending() = pending.clear()

    fun reset() { pending.clear(); prelude.clear() }

    /** أسماء ما صُرِّح به في الجلسة (للإكمال و`session`). */
    fun declaredNames(): List<String> = prelude.map { it.key }.filter { !it.startsWith("#") }.distinct()

    /** نص الـ prelude الحالي (للعرض بأمر `session`). */
    fun preludeSource(): String = prelude.joinToString("\n") { it.code }

    fun feed(line: String): Feed {
        pending.add(line)
        val joined = pending.joinToString("\n")
        if (balance(joined) > 0) return Feed.NeedMore
        pending.clear()
        return Feed.Ready(display = joined, code = joined.trim())
    }

    /** المصدر الذي يُشغَّل فعلاً: الـ prelude ثم الأمر (مع تغليف التعبير بـ print عند الحاجة). */
    fun buildSource(code: String): String {
        val body = normalize(code)
        return if (prelude.isEmpty()) body else preludeSource() + "\n" + body
    }

    /** يُستدعى عند نجاح التشغيل: يحفظ التصريحات (ويستبدل ما أُعيد تصريحه بالاسم). */
    fun commit(code: String) {
        for (stmt in splitStatements(code)) {
            val s = stmt.trim()
            if (s.isEmpty()) continue
            val name = declName(s)
            if (name != null) {
                // إعادة التصريح تُسقط التصريح القديم وكل تعيينات `name = ...` التي بُنيت عليه؛ وإلا أُعيد
                // تطبيقها فوق القيمة الجديدة فتفسدها. وإن صار التصريح الجديد غير نقي فالقيمة القديمة
                // المحفوظة مضلِّلة، فتُحذف أيضاً بدل أن تبقى (الاسم ببساطة لا يُحفظ).
                prelude.removeAll { it.key == name || it.key == "#assign:$name" }
                if (s.startsWith("let ") || s.startsWith("const ") || s.startsWith("var ")) {
                    if (!isPure(s)) continue
                }
                prelude.add(Entry(name, ensureTerminated(s)))
                continue
            }
            val target = assignTarget(s)
            if (target != null && prelude.any { it.key == target } && isPure(s)) {
                prelude.add(Entry("#assign:$target", ensureTerminated(s)))
                continue
            }
            if (s.startsWith("@import") || s.startsWith("import ")) {
                val key = "#import:$s"
                if (prelude.none { it.key == key }) prelude.add(Entry(key, ensureTerminated(s)))
            }
        }
    }

    /** إكمال Tab: أوامر الـ shell + الكلمات المفتاحية + أسماء الجلسة، مرتَّبة بلا تكرار. */
    fun complete(prefix: String, extra: Collection<String>): List<String> {
        if (prefix.isEmpty()) return emptyList()
        val pool = LinkedHashSet<String>()
        pool.addAll(declaredNames())
        pool.addAll(extra)
        return pool.filter { it.startsWith(prefix) && it != prefix }.sortedWith(compareBy({ it.length }, { it }))
    }

    // ---- تطبيع الأمر ----

    /** أمر تعبير بلا `;` (مثل `1 + 2`) يُغلَّف بـ print ليظهر ناتجه؛ غير ذلك يُشغَّل كما هو. */
    internal fun normalize(code: String): String {
        val t = code.trim()
        if (t.isEmpty()) return t
        if (t.endsWith(";") || t.endsWith("}")) return t
        return if (looksLikeExpression(t)) "print ($t);" else "$t;"
    }

    // ---- أدوات داخلية ----

    private fun ensureTerminated(s: String) = if (s.endsWith(";") || s.endsWith("}")) s else "$s;"

    private fun isPure(s: String) = !IMPURE.containsMatchIn(s)

    private fun declName(s: String): String? = DECL.find(s)?.groupValues?.get(1)

    private fun assignTarget(s: String): String? = ASSIGN.find(s)?.groupValues?.get(1)

    companion object {
        private val DECL = Regex("""^(?:(?:abstract|final|public|private|protected|static)\s+)*(?:let|const|var|fun|class|enum|struct|interface|trait)\s+([\p{L}_][\p{L}\p{N}_]*)""")
        private val ASSIGN = Regex("""^([\p{L}_][\p{L}\p{N}_]*)\s*(?:[-+*/%]?=)(?!=)""")
        private val IMPURE = Regex("""\b(?:input|inputNumber|confirm|choose|print|http|fetch|save|write|delete|remove|random|rand|now|time|date|installation)\b""")
        private val STATEMENT_WORDS = setOf(
            "print", "let", "const", "var", "fun", "class", "enum", "struct", "interface", "trait",
            "abstract", "final", "if", "else", "while", "for", "return", "break", "continue", "import",
            "use", "when", "match", "try", "catch", "throw", "static", "public", "private", "protected",
            "save", "installation", "show", "set", "on", "state", "container"
        )

        /** صافي الأقواس المفتوحة ({[( ) مع تجاهل النصوص والتعليقات؛ > 0 يعني أمراً ناقصاً. */
        fun balance(src: String): Int {
            var depth = 0
            var i = 0
            var quote = '\u0000'
            while (i < src.length) {
                val c = src[i]
                if (quote != '\u0000') {
                    if (c == '\\') i++
                    else if (c == quote) quote = '\u0000'
                    else if (c == '\n' && quote != '`') quote = '\u0000'   // نص لم يُغلق: لا نعلّق للأبد
                } else when {
                    c == '"' || c == '\'' || c == '`' -> quote = c
                    c == '/' && i + 1 < src.length && src[i + 1] == '/' -> {
                        while (i < src.length && src[i] != '\n') i++
                        continue
                    }
                    c == '(' || c == '[' || c == '{' -> depth++
                    c == ')' || c == ']' || c == '}' -> if (depth > 0) depth--
                }
                i++
            }
            return depth
        }

        /** يقسم الشيفرة إلى عبارات على المستوى الأعلى: عند `;` أو عند `}` يغلق عمقاً صفرياً. */
        fun splitStatements(src: String): List<String> {
            val out = ArrayList<String>()
            val cur = StringBuilder()
            var depth = 0
            var quote = '\u0000'
            var i = 0
            while (i < src.length) {
                val c = src[i]
                cur.append(c)
                if (quote != '\u0000') {
                    if (c == '\\' && i + 1 < src.length) { i++; cur.append(src[i]) }
                    else if (c == quote) quote = '\u0000'
                } else when {
                    c == '"' || c == '\'' || c == '`' -> quote = c
                    c == '(' || c == '[' || c == '{' -> depth++
                    c == ')' || c == ']' -> if (depth > 0) depth--
                    c == '}' -> {
                        if (depth > 0) depth--
                        if (depth == 0) {
                            // `let m = {a: 1};` — الفاصلة المنقوطة بعد القوس جزء من العبارة نفسها.
                            var j = i + 1
                            while (j < src.length && (src[j] == ' ' || src[j] == '\t')) j++
                            if (j < src.length && src[j] == ';') { cur.append(src, i + 1, j + 1); i = j }
                            out.add(cur.toString()); cur.setLength(0)
                        }
                    }
                    c == ';' && depth == 0 -> { out.add(cur.toString()); cur.setLength(0) }
                }
                i++
            }
            if (cur.isNotBlank()) out.add(cur.toString())
            return out
        }

        /** يشبه تعبيراً (قيمة) لا عبارة: رقم/نص/قوس/أو اسم يتبعه عامل أو نداء أو نقطة — لا اسم ثانٍ. */
        fun looksLikeExpression(t: String): Boolean {
            if (t.startsWith("@") || t.contains("|>")) return false
            val c = t[0]
            if (c.isDigit() || c == '"' || c == '\'' || c == '(' || c == '[' || c == '-' || c == '!') return true
            if (!(c.isLetter() || c == '_')) return false
            val m = Regex("""^([\p{L}_][\p{L}\p{N}_]*)""").find(t) ?: return false
            val word = m.groupValues[1]
            if (word in STATEMENT_WORDS) return false
            val rest = t.substring(m.range.last + 1).trimStart()
            if (rest.isEmpty()) return true
            val r = rest[0]
            if (r.isLetter() || r == '_' || r == '"' || r == '\'') return false   // `name word` = عبارة بصياغة خاصة
            if (ASSIGN.containsMatchIn(t)) return false
            return true
        }
    }
}

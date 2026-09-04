package com.dlof.rinlang

/** فئة التلوين لكل امتداد نصّي — تُرجَّم إلى لون في [RinCodeEditorView.colorForKind]. */
object HighlightKind {
    const val DEFAULT = 0
    const val KEYWORD = 1
    const val STRING = 2
    const val NUMBER = 3
    const val IDENT = 4
    const val OPERATOR = 5
    const val BRACKET = 6
    const val AT = 7          // وسوم/زخارف/تعليقات توجيهية (@Tag, .end/x, #include...)
    const val ERROR = 8
    const val CALL = 9        // معرِّف متبوع مباشرة بـ '(' — نمط استدعاء دالة
    const val COMMENT = 10
    const val TYPE = 11       // أنواع بيانات/كلمات حاوية مبنية داخلياً
    const val PREPROCESSOR = 12
}

enum class SyntaxLanguage {
    RIN, CPP, KOTLIN, JAVA, PYTHON, JS, HTML, CSS, JSON, SQL, SHELL, PLAIN;

    fun lineCommentPrefix(): String? = when (this) {
        RIN, CPP, KOTLIN, JAVA, JS -> "//"
        PYTHON, SHELL -> "#"
        SQL -> "--"
        HTML, CSS, JSON, PLAIN -> null
    }

    companion object {
        fun forExtension(extension: String): SyntaxLanguage = when (extension.lowercase().removePrefix(".")) {
            "rin", "" -> RIN
            "c", "h", "cpp", "cc", "cxx", "hpp", "hh" -> CPP
            "kt", "kts" -> KOTLIN
            "java" -> JAVA
            "py" -> PYTHON
            "js", "mjs", "jsx", "ts", "tsx" -> JS
            "html", "htm", "xml" -> HTML
            "css" -> CSS
            "json" -> JSON
            "sql" -> SQL
            "sh", "bash" -> SHELL
            else -> PLAIN
        }
    }
}

/**
 * تلوين نحوي حقيقي مبني على مسح الرموز (tokenizing) حرفاً-حرفاً لكل سطر، مع تمرير حالة بسيطة
 * (داخل تعليق كتلي؟) بين الأسطر المتتالية — بديل كامل بلغة Kotlin خالصة عن rin::Lexer/regex.
 */
object RinSyntax {

    fun computeHighlights(lines: List<String>, language: SyntaxLanguage): List<RinEditorEngine.Highlight> {
        return when (language) {
            SyntaxLanguage.RIN -> RinLexer.tokenize(lines)
            SyntaxLanguage.CPP -> CFamilyLexer.tokenize(lines, cppKeywords, cppTypes, hashPreprocessor = true, jsTemplate = false)
            SyntaxLanguage.KOTLIN -> CFamilyLexer.tokenize(lines, kotlinKeywords, kotlinTypes, hashPreprocessor = false, jsTemplate = false)
            SyntaxLanguage.JAVA -> CFamilyLexer.tokenize(lines, javaKeywords, javaTypes, hashPreprocessor = false, jsTemplate = false)
            SyntaxLanguage.JS -> CFamilyLexer.tokenize(lines, jsKeywords, jsTypes, hashPreprocessor = false, jsTemplate = true)
            SyntaxLanguage.PYTHON -> PythonLexer.tokenize(lines)
            SyntaxLanguage.SQL -> SimpleLexer.tokenizeLineComment(lines, sqlKeywords, "--", caseInsensitive = true)
            SyntaxLanguage.SHELL -> SimpleLexer.tokenizeLineComment(lines, shellKeywords, "#", caseInsensitive = false)
            SyntaxLanguage.JSON -> SimpleLexer.tokenizeLineComment(lines, setOf("true", "false", "null"), null, caseInsensitive = false)
            SyntaxLanguage.CSS -> CssLexer.tokenize(lines)
            SyntaxLanguage.HTML -> HtmlLexer.tokenize(lines)
            SyntaxLanguage.PLAIN -> emptyList()
        }
    }

    fun keywordsFor(language: SyntaxLanguage): List<String> = when (language) {
        SyntaxLanguage.RIN -> (RinLexer.coreKeywords + RinLexer.containerKeywords + RinLexer.builtins).toList()
        SyntaxLanguage.CPP -> (cppKeywords + cppTypes).toList()
        SyntaxLanguage.KOTLIN -> (kotlinKeywords + kotlinTypes).toList()
        SyntaxLanguage.JAVA -> (javaKeywords + javaTypes).toList()
        SyntaxLanguage.JS -> (jsKeywords + jsTypes).toList()
        SyntaxLanguage.PYTHON -> (PythonLexer.keywords + PythonLexer.types).toList()
        SyntaxLanguage.SQL -> sqlKeywords.toList()
        SyntaxLanguage.SHELL -> shellKeywords.toList()
        else -> emptyList()
    }

    // ------------------------------------------------------------------------------
    // قوائم الكلمات المفتاحية/الأنواع للغات نمط-C (تُشارَك بين CFamilyLexer وkeywordsFor)
    // ------------------------------------------------------------------------------

    private val cppKeywords = setOf(
        "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch", "char", "char8_t",
        "char16_t", "char32_t", "class", "concept", "const", "consteval", "constexpr", "constinit",
        "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default",
        "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "final", "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "nullptr", "operator", "override", "private",
        "protected", "public", "register", "reinterpret_cast", "requires", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch",
        "template", "this", "thread_local", "throw", "true", "try", "typedef", "typeid",
        "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
        "while", "NULL"
    )
    private val cppTypes = setOf(
        "size_t", "ssize_t", "string", "wstring", "vector", "map", "unordered_map", "set",
        "unordered_set", "pair", "shared_ptr", "unique_ptr", "int8_t", "int16_t", "int32_t",
        "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "std", "nullptr_t"
    )

    private val kotlinKeywords = setOf(
        "fun", "val", "var", "if", "else", "when", "for", "while", "do", "return", "break",
        "continue", "class", "object", "interface", "companion", "init", "constructor", "try",
        "catch", "finally", "throw", "is", "as", "as?", "!is", "in", "!in", "null", "true",
        "false", "this", "super", "override", "open", "abstract", "sealed", "data", "enum",
        "annotation", "private", "protected", "public", "internal", "lateinit", "lazy", "suspend",
        "inline", "vararg", "import", "package", "by", "get", "set", "typealias", "reified",
        "crossinline", "noinline", "const", "external", "actual", "expect", "infix", "operator",
        "tailrec", "out", "where", "dynamic", "field", "param", "receiver", "setparam"
    )
    private val kotlinTypes = setOf(
        "Int", "String", "Boolean", "Double", "Float", "Long", "Short", "Byte", "Char", "Unit",
        "Any", "List", "MutableList", "Map", "MutableMap", "Set", "MutableSet", "Array",
        "Nothing", "Pair", "Triple", "Sequence"
    )

    private val javaKeywords = setOf(
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
        "return", "class", "interface", "enum", "extends", "implements", "new", "try", "catch",
        "finally", "throw", "throws", "public", "private", "protected", "static", "final",
        "abstract", "synchronized", "volatile", "transient", "native", "this", "super",
        "package", "import", "instanceof", "true", "false", "null", "void", "record", "var"
    )
    private val javaTypes = setOf(
        "int", "float", "double", "char", "boolean", "long", "short", "byte",
        "String", "Integer", "Double", "Float", "Boolean", "Long", "Object", "List", "Map", "Set"
    )

    private val jsKeywords = setOf(
        "function", "var", "let", "const", "if", "else", "for", "while", "do", "return", "class",
        "extends", "new", "this", "typeof", "instanceof", "try", "catch", "finally", "throw",
        "switch", "case", "default", "break", "continue", "import", "export", "from", "as",
        "async", "await", "yield", "null", "undefined", "true", "false", "in", "of", "void",
        "delete", "static", "get", "set", "super", "interface", "type", "implements", "enum",
        "public", "private", "protected", "readonly", "namespace", "declare"
    )
    private val jsTypes = setOf("string", "number", "boolean", "any", "void", "never", "unknown", "object", "symbol", "bigint")

    private val sqlKeywords = setOf(
        "select", "from", "where", "insert", "into", "values", "update", "set", "delete",
        "create", "table", "alter", "drop", "join", "inner", "left", "right", "outer", "on",
        "group", "by", "order", "having", "and", "or", "not", "null", "as", "distinct", "limit",
        "union", "all", "in", "like", "between", "case", "when", "then", "end", "else"
    )
    private val shellKeywords = setOf(
        "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac",
        "function", "return", "export", "local", "echo", "in", "select", "until"
    )
}

/** حالة صغيرة تُنقَل بين سطر وتاليه (تعليق كتلي مفتوح لم يُغلق بعد ضمن نفس السطر). */
private data class LineState(val inBlockComment: Boolean = false)

// ====================================================================================
// لغة Rin — الماسح الأغنى (كلمات اللغة الأساسية + لغة الحاويات + دوال مدمجة + وسوم @).
// ====================================================================================
private object RinLexer {
    val coreKeywords = setOf(
        "let", "print", "if", "else", "while", "for", "fun", "return", "break", "continue",
        "true", "false", "nil", "and", "or", "reckon", "where"
    )
    val containerKeywords = setOf(
        "text", "container", "Containers", "Group", "Volume", "Section",
        "Translations", "translation", "link", "tying", "merge",
        "installation", "simplified", "save", "file", "end",
        "row", "style", "document", "route",
        "data", "api", "import", "table", "doc", "portal", "block", "pipe",
        "plus", "condition", "loop", "function", "view", "chatbot", "element", "on",
        "button", "input", "search", "image", "video", "audio", "progress",
        "checkbox", "radio", "switch", "slider", "select", "list", "column", "box",
        "card", "sidebar", "popup", "modal", "tabs", "code_editor", "calculator", "divider",
        "date", "time", "dropdown", "range", "listitem", "direction",
        "kind", "use", "need", "allow", "deny", "strict", "input", "output", "public", "private",
        "version", "description", "item",
        "txt", "img", "object", "Fonts", "background", "css3"
    )
    val builtins = setOf(
        "Addition", "Subtraction", "Multiplication", "Equal",
        "abs", "sqrt", "pow", "floor", "ceil", "round", "min", "max", "random",
        "len", "upper", "lower", "trim", "substr", "split", "join",
        "indexOf", "replace", "contains", "charAt", "toString", "toNumber",
        "push", "pop", "sort", "keys", "values", "has", "remove",
        "sum", "mean", "median", "variance", "stddev", "mode", "minOf", "maxOf",
        "normalize", "scale", "shift", "product", "count", "range", "geometricMean",
        "harmonicMean", "rms", "percentile", "iqr", "weightedMean", "zscore",
        "cumulativeSum", "movingAverage", "clamp"
    )

    fun tokenize(lines: List<String>): List<RinEditorEngine.Highlight> {
        val out = ArrayList<RinEditorEngine.Highlight>()
        var state = LineState()
        for ((li, text) in lines.withIndex()) {
            state = tokenizeLine(li, text, out, state)
        }
        return out
    }

    private fun tokenizeLine(li: Int, text: String, out: MutableList<RinEditorEngine.Highlight>, state: LineState): LineState {
        var i = 0
        val n = text.length
        var inBlockComment = state.inBlockComment
        while (i < n) {
            if (inBlockComment) {
                val end = text.indexOf("*/", i)
                if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                else { out.add(hl(li, i, end + 2, HighlightKind.COMMENT)); i = end + 2; inBlockComment = false }
                continue
            }
            val c = text[i]
            when {
                c == '/' && i + 1 < n && text[i + 1] == '/' -> { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                c == '/' && i + 1 < n && text[i + 1] == '*' -> {
                    val end = text.indexOf("*/", i + 2)
                    if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n; inBlockComment = true }
                    else { out.add(hl(li, i, end + 2, HighlightKind.COMMENT)); i = end + 2 }
                }
                c == '"' -> { val end = scanQuoted(text, i, '"'); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                c == '@' -> {
                    val start = i; i++
                    while (i < n && (text[i].isLetterOrDigit() || text[i] == '_' || text[i] == '.')) i++
                    out.add(hl(li, start, i, HighlightKind.AT))
                }
                c == '.' && text.startsWith(".end", i) -> {
                    val start = i; i += 4
                    while (i < n && (text[i].isLetterOrDigit() || text[i] == '_' || text[i] == '/' || text[i] == '.')) i++
                    out.add(hl(li, start, i, HighlightKind.AT))
                }
                c.isDigit() -> { val end = scanNumber(text, i); out.add(hl(li, i, end, HighlightKind.NUMBER)); i = end }
                c.isLetter() || c == '_' -> {
                    val start = i
                    while (i < n && (text[i].isLetterOrDigit() || text[i] == '_')) i++
                    val word = text.substring(start, i)
                    val kind = when {
                        word in coreKeywords -> HighlightKind.KEYWORD
                        word in builtins && followedByOpenParen(text, i) -> HighlightKind.CALL
                        word in builtins -> HighlightKind.CALL
                        word in containerKeywords -> HighlightKind.TYPE
                        followedByOpenParen(text, i) -> HighlightKind.CALL
                        else -> HighlightKind.IDENT
                    }
                    out.add(hl(li, start, i, kind))
                }
                else -> i++
            }
        }
        return LineState(inBlockComment)
    }
}

// ====================================================================================
// عائلة لغات نمط-C (C/C++، Kotlin، Java، JS/TS) — ماسح واحد مُشترَك بمعاملات لكل لغة.
// ====================================================================================
private object CFamilyLexer {
    fun tokenize(
        lines: List<String>,
        keywords: Set<String>,
        types: Set<String>,
        hashPreprocessor: Boolean,
        jsTemplate: Boolean
    ): List<RinEditorEngine.Highlight> {
        val out = ArrayList<RinEditorEngine.Highlight>()
        var state = LineState()
        for ((li, text) in lines.withIndex()) {
            state = tokenizeLine(li, text, keywords, types, hashPreprocessor, jsTemplate, out, state)
        }
        return out
    }

    private fun tokenizeLine(
        li: Int, text: String, keywords: Set<String>, types: Set<String>,
        hashPreprocessor: Boolean, jsTemplate: Boolean,
        out: MutableList<RinEditorEngine.Highlight>, state: LineState
    ): LineState {
        var i = 0
        val n = text.length
        var inBlockComment = state.inBlockComment
        if (hashPreprocessor) {
            val trimmed = text.trimStart()
            if (!inBlockComment && trimmed.startsWith("#")) {
                val start = text.indexOf('#')
                out.add(hl(li, start, n, HighlightKind.PREPROCESSOR))
                return LineState(false)
            }
        }
        while (i < n) {
            if (inBlockComment) {
                val end = text.indexOf("*/", i)
                if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                else { out.add(hl(li, i, end + 2, HighlightKind.COMMENT)); i = end + 2; inBlockComment = false }
                continue
            }
            val c = text[i]
            when {
                c == '/' && i + 1 < n && text[i + 1] == '/' -> { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                c == '/' && i + 1 < n && text[i + 1] == '*' -> {
                    val end = text.indexOf("*/", i + 2)
                    if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n; inBlockComment = true }
                    else { out.add(hl(li, i, end + 2, HighlightKind.COMMENT)); i = end + 2 }
                }
                c == '"' -> { val end = scanQuoted(text, i, '"'); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                c == '\'' -> { val end = scanQuoted(text, i, '\''); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                jsTemplate && c == '`' -> { val end = scanQuoted(text, i, '`'); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                c == '@' -> {
                    val start = i; i++
                    while (i < n && (text[i].isLetterOrDigit() || text[i] == '_')) i++
                    out.add(hl(li, start, i, HighlightKind.AT))
                }
                c.isDigit() -> { val end = scanNumber(text, i); out.add(hl(li, i, end, HighlightKind.NUMBER)); i = end }
                c.isLetter() || c == '_' -> {
                    val start = i
                    while (i < n && (text[i].isLetterOrDigit() || text[i] == '_')) i++
                    val word = text.substring(start, i)
                    val kind = when {
                        word in keywords -> HighlightKind.KEYWORD
                        word in types -> HighlightKind.TYPE
                        followedByOpenParen(text, i) -> HighlightKind.CALL
                        else -> HighlightKind.IDENT
                    }
                    out.add(hl(li, start, i, kind))
                }
                else -> i++
            }
        }
        return LineState(inBlockComment)
    }
}

// ====================================================================================
// Python — بلا تعليقات كتلية، مع دعم النصوص الثلاثية '''/""" ممتدة عبر الأسطر.
// ====================================================================================
private object PythonLexer {
    val keywords = setOf(
        "def", "class", "if", "elif", "else", "for", "while", "return", "import", "from", "as",
        "try", "except", "finally", "with", "lambda", "pass", "break", "continue", "global",
        "nonlocal", "yield", "assert", "raise", "in", "is", "not", "and", "or", "None", "True",
        "False", "self", "async", "await", "del"
    )
    val types = setOf("int", "str", "float", "bool", "list", "dict", "set", "tuple", "bytes", "object")

    fun tokenize(lines: List<String>): List<RinEditorEngine.Highlight> {
        val out = ArrayList<RinEditorEngine.Highlight>()
        var inTriple: String? = null
        for ((li, text) in lines.withIndex()) {
            inTriple = tokenizeLine(li, text, out, inTriple)
        }
        return out
    }

    private fun tokenizeLine(li: Int, text: String, out: MutableList<RinEditorEngine.Highlight>, tripleState: String?): String? {
        var i = 0
        val n = text.length
        var inTriple = tripleState
        if (inTriple != null) {
            val end = text.indexOf(inTriple, i)
            if (end == -1) { out.add(hl(li, 0, n, HighlightKind.STRING)); return inTriple }
            out.add(hl(li, 0, end + 3, HighlightKind.STRING)); i = end + 3; inTriple = null
        }
        while (i < n) {
            val c = text[i]
            when {
                c == '#' -> { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                (text.startsWith("'''", i) || text.startsWith("\"\"\"", i)) -> {
                    val quote = text.substring(i, i + 3)
                    val end = text.indexOf(quote, i + 3)
                    if (end == -1) { out.add(hl(li, i, n, HighlightKind.STRING)); i = n; inTriple = quote }
                    else { out.add(hl(li, i, end + 3, HighlightKind.STRING)); i = end + 3 }
                }
                c == '"' -> { val end = scanQuoted(text, i, '"'); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                c == '\'' -> { val end = scanQuoted(text, i, '\''); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                c == '@' && text.substring(0, i).isBlank() -> { out.add(hl(li, i, n, HighlightKind.AT)); i = n }
                c.isDigit() -> { val end = scanNumber(text, i); out.add(hl(li, i, end, HighlightKind.NUMBER)); i = end }
                c.isLetter() || c == '_' -> {
                    val start = i
                    while (i < n && (text[i].isLetterOrDigit() || text[i] == '_')) i++
                    val word = text.substring(start, i)
                    val kind = when {
                        word in keywords -> HighlightKind.KEYWORD
                        word in types -> HighlightKind.TYPE
                        followedByOpenParen(text, i) -> HighlightKind.CALL
                        else -> HighlightKind.IDENT
                    }
                    out.add(hl(li, start, i, kind))
                }
                else -> i++
            }
        }
        return inTriple
    }
}

// ====================================================================================
// ماسح مبسّط للغات الأبسط بنيوياً (SQL/Shell/JSON): كلمات مفتاحية + نصوص + تعليق سطر واحد فقط.
// ====================================================================================
private object SimpleLexer {
    fun tokenizeLineComment(
        lines: List<String>, keywords: Set<String>, lineComment: String?, caseInsensitive: Boolean
    ): List<RinEditorEngine.Highlight> {
        val out = ArrayList<RinEditorEngine.Highlight>()
        val kwSet = if (caseInsensitive) keywords.map { it.lowercase() }.toSet() else keywords
        for ((li, text) in lines.withIndex()) {
            var i = 0
            val n = text.length
            while (i < n) {
                val c = text[i]
                when {
                    lineComment != null && text.startsWith(lineComment, i) -> { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                    c == '"' -> { val end = scanQuoted(text, i, '"'); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                    c == '\'' -> { val end = scanQuoted(text, i, '\''); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                    c == '$' -> {
                        val start = i; i++
                        val braced = i < n && text[i] == '{'
                        if (braced) i++
                        while (i < n && (text[i].isLetterOrDigit() || text[i] == '_')) i++
                        if (braced && i < n && text[i] == '}') i++
                        out.add(hl(li, start, i, HighlightKind.AT))
                    }
                    c.isDigit() -> { val end = scanNumber(text, i); out.add(hl(li, i, end, HighlightKind.NUMBER)); i = end }
                    c.isLetter() || c == '_' -> {
                        val start = i
                        while (i < n && (text[i].isLetterOrDigit() || text[i] == '_')) i++
                        val word = text.substring(start, i)
                        val key = if (caseInsensitive) word.lowercase() else word
                        out.add(hl(li, start, i, if (key in kwSet) HighlightKind.KEYWORD else HighlightKind.IDENT))
                    }
                    else -> i++
                }
            }
        }
        return out
    }
}

// ====================================================================================
// CSS — قواعد @ + نصوص + أرقام + تعليقات كتلية فقط (لا تعليق سطر واحد في CSS).
// ====================================================================================
private object CssLexer {
    fun tokenize(lines: List<String>): List<RinEditorEngine.Highlight> {
        val out = ArrayList<RinEditorEngine.Highlight>()
        var state = LineState()
        for ((li, text) in lines.withIndex()) {
            var i = 0
            val n = text.length
            var inBlockComment = state.inBlockComment
            while (i < n) {
                if (inBlockComment) {
                    val end = text.indexOf("*/", i)
                    if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                    else { out.add(hl(li, i, end + 2, HighlightKind.COMMENT)); i = end + 2; inBlockComment = false }
                    continue
                }
                val c = text[i]
                when {
                    c == '/' && i + 1 < n && text[i + 1] == '*' -> {
                        val end = text.indexOf("*/", i + 2)
                        if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n; inBlockComment = true }
                        else { out.add(hl(li, i, end + 2, HighlightKind.COMMENT)); i = end + 2 }
                    }
                    c == '"' -> { val end = scanQuoted(text, i, '"'); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                    c == '\'' -> { val end = scanQuoted(text, i, '\''); out.add(hl(li, i, end, HighlightKind.STRING)); i = end }
                    c == '@' -> {
                        val start = i; i++
                        while (i < n && (text[i].isLetter() || text[i] == '-')) i++
                        out.add(hl(li, start, i, HighlightKind.AT))
                    }
                    c.isDigit() || (c == '-' && i + 1 < n && text[i + 1].isDigit()) -> {
                        val end = scanNumber(text, if (c == '-') i + 1 else i)
                        out.add(hl(li, i, end, HighlightKind.NUMBER)); i = end
                    }
                    else -> i++
                }
            }
            state = LineState(inBlockComment)
        }
        return out
    }
}

// ====================================================================================
// HTML/XML — وسوم <...> بلون At، وسمات نصّية بلون String، تعليقات <!-- -->.
// ====================================================================================
private object HtmlLexer {
    fun tokenize(lines: List<String>): List<RinEditorEngine.Highlight> {
        val out = ArrayList<RinEditorEngine.Highlight>()
        var inComment = false
        for ((li, text) in lines.withIndex()) {
            var i = 0
            val n = text.length
            while (i < n) {
                if (inComment) {
                    val end = text.indexOf("-->", i)
                    if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n }
                    else { out.add(hl(li, i, end + 3, HighlightKind.COMMENT)); i = end + 3; inComment = false }
                    continue
                }
                val c = text[i]
                when {
                    text.startsWith("<!--", i) -> {
                        val end = text.indexOf("-->", i + 4)
                        if (end == -1) { out.add(hl(li, i, n, HighlightKind.COMMENT)); i = n; inComment = true }
                        else { out.add(hl(li, i, end + 3, HighlightKind.COMMENT)); i = end + 3 }
                    }
                    c == '<' -> {
                        val end = text.indexOf('>', i)
                        val tagEnd = if (end == -1) n else end + 1
                        out.add(hl(li, i, tagEnd, HighlightKind.AT))
                        // نصوص السمات (attr="value") داخل الوسم بلون منفصل
                        var j = i
                        while (j < tagEnd) {
                            if (text[j] == '"') {
                                val strEnd = scanQuoted(text, j, '"').coerceAtMost(tagEnd)
                                out.add(hl(li, j, strEnd, HighlightKind.STRING))
                                j = strEnd
                            } else j++
                        }
                        i = tagEnd
                    }
                    else -> i++
                }
            }
        }
        return out
    }
}

// ------------------------------------------------------------------------------------
// أدوات مسح مشتركة
// ------------------------------------------------------------------------------------

private fun hl(line: Int, start: Int, end: Int, kind: Int) = RinEditorEngine.Highlight(line, start, end, kind)

/** يمسح نصاً محاطاً بـ[quote] بدءاً من [start] (حيث text[start]==quote)، بدعم تهريب `\x`،
 *  ويُرجع الفهرس مباشرة بعد علامة الإغلاق (أو نهاية السطر إن لم يُغلق النص). */
private fun scanQuoted(text: String, start: Int, quote: Char): Int {
    var i = start + 1
    val n = text.length
    while (i < n) {
        val c = text[i]
        if (c == '\\' && i + 1 < n) { i += 2; continue }
        if (c == quote) return i + 1
        i++
    }
    return n
}

private fun scanNumber(text: String, start: Int): Int {
    var i = start
    val n = text.length
    while (i < n && (text[i].isDigit() || text[i] == '.' || text[i] == '_' ||
            text[i] == 'x' || text[i] == 'X' || text[i] == 'b' || text[i] == 'B' ||
            (text[i] in 'a'..'f') || (text[i] in 'A'..'F') ||
            text[i] == 'e' || text[i] == 'E' ||
            ((text[i] == '+' || text[i] == '-') && i > start && (text[i - 1] == 'e' || text[i - 1] == 'E')) ||
            text[i] == 'f' || text[i] == 'F' || text[i] == 'L' || text[i] == 'u' || text[i] == 'U')) i++
    return i
}

private fun followedByOpenParen(text: String, fromIndex: Int): Boolean {
    var j = fromIndex
    val n = text.length
    while (j < n && text[j] == ' ') j++
    return j < n && text[j] == '('
}

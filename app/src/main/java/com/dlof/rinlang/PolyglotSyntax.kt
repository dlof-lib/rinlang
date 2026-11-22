package com.dlof.rinlang

/**
 * مدى ملوَّن واحد. [startCol]/[endCol] فهارس حروف UTF-16 داخل السطر (نفس ما يتوقعه المحرر الإنتاجي)،
 * و[kind] أحد ثوابت [HighlightKind].
 */
data class PolySpan(val line: Int, val startCol: Int, val endCol: Int, val kind: Int)

/**
 * ماسح تلوين موحّد لكل اللغات غير Rin، مبني على **جدول مواصفات** لكل لغة ([LangSpec]) بدل ماسح
 * مستقل لكل لغة.
 *
 * لماذا؟ محرك المحرر الأصلي (C++) يمرّر أي امتداد غير `.passkit` عبر محلل Rin، فكانت ملفات
 * `.kt/.cpp/.py/.html/.css/.json` تُلوَّن بقواعد Rin خطأً. هذا الماسح يعمل في Kotlin على نفس
 * الأسطر، وينتج نفس أنواع [HighlightKind] التي يرسمها المحرر، فتتبدّل الألوان تلقائياً مع الثيم.
 *
 * ما يضيفه على الماسحات القديمة في [RinSyntax]:
 *  - **الرموز**: عمليات حسابية [HighlightKind.OPERATOR]، مقارنات [HighlightKind.COMPARE]،
 *    أسهم/أنابيب [HighlightKind.FLOW]، وفواصل [HighlightKind.PUNCT].
 *  - **أقواس متدرّجة اللون** حسب العمق (BRACKET0/1/2) — يتتبّع العمق عبر الأسطر.
 *  - **دلالات**: اسم الدالة بعد fun/def/function/fn…، اسم الصنف بعد class/struct/interface…،
 *    ثوابت (true/null/nil…)، معدّلات (public/static/const…)، خصائص بعد النقطة، استدعاء دوال،
 *    تعليمات المعالج المسبق، والتعليقات التوجيهية (@Annotation).
 *  - لغات أكثر: C# Swift Go Rust Dart PHP Ruby Lua Groovy/Gradle YAML TOML/INI Markdown SCSS XML.
 *
 * الحالة العابرة للأسطر: تعليق كتلي، نص ثلاثي/خام ممتد، عمق الأقواس.
 * لا اعتماد على أندرويد إطلاقاً، فهو قابل للاختبار مباشرة.
 */
object PolyglotSyntax {

    /** أقصى طول سطر نمسحه؛ ما بعده يُترك بلا تلوين (يحمي من ملفات مصغّرة minified ضخمة). */
    private const val MAX_LINE = 4000

    // ------------------------------------------------------------------------------------
    // المواصفات
    // ------------------------------------------------------------------------------------

    private class LangSpec(
        val keywords: Set<String> = emptySet(),
        val control: Set<String> = emptySet(),
        val types: Set<String> = emptySet(),
        val constants: Set<String> = emptySet(),
        val modifiers: Set<String> = emptySet(),
        val funDecl: Set<String> = emptySet(),
        val classDecl: Set<String> = emptySet(),
        val lineComments: List<String> = listOf("//"),
        val blockStart: String? = "/*",
        val blockEnd: String? = "*/",
        val quotes: String = "\"'",
        val triple: Boolean = false,
        val backtickStrings: Boolean = false,
        val caseInsensitive: Boolean = false,
        val annotations: Boolean = false,        // @Name
        val hashPreprocessor: Boolean = false,   // #include / #define في أول السطر
        val dollarVars: Boolean = false,         // $name / ${name}
        val upperIsType: Boolean = true,         // Identifier يبدأ بحرف كبير => صنف/نوع
        val arrowOps: Boolean = true
    )

    private fun words(s: String): Set<String> = s.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }.toSet()

    private val cLikeControl = words("if else for while do switch case default break continue return goto try catch finally throw throws yield await when match")

    private val specs: Map<String, LangSpec> by lazy { buildSpecs() }

    private fun buildSpecs(): Map<String, LangSpec> {
        val m = HashMap<String, LangSpec>()

        val cpp = LangSpec(
            keywords = words("alignas alignof asm auto concept consteval constexpr constinit decltype delete explicit export extern friend inline mutable namespace new noexcept operator requires sizeof static_assert template this typedef typeid typename using virtual volatile override final"),
            control = cLikeControl + words("co_await co_return co_yield"),
            types = words("bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t size_t ssize_t int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t string vector map set unordered_map unordered_set array pair tuple optional variant unique_ptr shared_ptr weak_ptr span string_view"),
            constants = words("true false nullptr NULL"),
            modifiers = words("public private protected static const virtual inline explicit"),
            funDecl = emptySet(),
            classDecl = words("class struct union enum namespace"),
            hashPreprocessor = true
        )
        for (e in listOf("c", "h", "cpp", "cc", "cxx", "hpp", "hh", "ino")) m[e] = cpp

        m["cs"] = LangSpec(
            keywords = words("abstract as base checked class const delegate event explicit extern fixed foreach implicit in interface internal is lock namespace new object operator out override params partial readonly ref sealed sizeof stackalloc static struct this typeof unchecked unsafe using virtual volatile var async record init get set value nameof"),
            control = cLikeControl,
            types = words("bool byte char decimal double float int long object sbyte short string uint ulong ushort void dynamic String List Dictionary Task IEnumerable"),
            constants = words("true false null"),
            modifiers = words("public private protected internal static readonly sealed abstract override virtual async"),
            classDecl = words("class struct interface enum record namespace"),
            annotations = false, hashPreprocessor = true
        )

        m["java"] = LangSpec(
            keywords = words("abstract assert class const enum extends final implements import instanceof interface native new package record sealed permits static strictfp super synchronized this transient var volatile"),
            control = cLikeControl,
            types = words("boolean byte char double float int long short void String Object List Map Set ArrayList HashMap Optional Integer Long Double Boolean"),
            constants = words("true false null"),
            modifiers = words("public private protected static final abstract synchronized native"),
            classDecl = words("class interface enum record"),
            annotations = true
        )

        val kotlin = LangSpec(
            keywords = words("abstract actual annotation as by companion const constructor crossinline data delegate dynamic enum expect external field file final get import in infix init inline inner interface internal is lateinit noinline object open operator out override package param private property protected public receiver reified sealed set setparam super suspend tailrec this typealias typeof val value var vararg where"),
            control = words("if else for while do when break continue return try catch finally throw"),
            types = words("Int Long Short Byte Float Double Boolean Char String Unit Any Nothing Array List MutableList Map MutableMap Set MutableSet Pair Triple Sequence Result"),
            constants = words("true false null"),
            modifiers = words("public private protected internal open final abstract override suspend inline data sealed lateinit const"),
            funDecl = words("fun"),
            classDecl = words("class interface object enum typealias"),
            triple = true, annotations = true
        )
        m["kt"] = kotlin; m["kts"] = kotlin

        val gradle = LangSpec(
            keywords = words("def plugins apply plugin id version implementation api compileOnly runtimeOnly testImplementation android dependencies repositories buildscript allprojects subprojects task ext project include rootProject"),
            control = words("if else for while switch case default break continue return try catch finally throw"),
            types = words("String Integer Boolean List Map File"),
            constants = words("true false null"),
            funDecl = words("def"), triple = true, annotations = true
        )
        m["gradle"] = gradle; m["groovy"] = gradle

        m["swift"] = LangSpec(
            keywords = words("associatedtype class deinit enum extension fileprivate func import init inout internal let open operator private precedencegroup protocol public rethrows static struct subscript typealias var async await actor some any self Self super where lazy weak unowned mutating nonmutating override final convenience required"),
            control = words("if else guard switch case default for while repeat break continue fallthrough return defer do try catch throw throws in"),
            types = words("Int Int8 Int16 Int32 Int64 UInt Float Double Bool String Character Array Dictionary Set Optional Any AnyObject Void Data Date URL"),
            constants = words("true false nil"),
            modifiers = words("public private internal fileprivate open static final lazy weak"),
            funDecl = words("func"), classDecl = words("class struct enum protocol extension actor"),
            triple = true, annotations = true
        )

        m["go"] = LangSpec(
            keywords = words("chan const func import interface map package range struct type var go defer select iota"),
            control = words("if else for switch case default break continue return goto fallthrough"),
            types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr any"),
            constants = words("true false nil iota"),
            funDecl = words("func"), classDecl = words("type"),
            backtickStrings = true, quotes = "\"'"
        )

        m["rs"] = LangSpec(
            keywords = words("as async await const crate dyn enum extern fn impl let mod move mut pub ref self Self static struct super trait type unsafe use where union macro_rules"),
            control = words("if else for while loop match break continue return in"),
            types = words("bool char f32 f64 i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize str String Vec Option Result Box Rc Arc HashMap HashSet BTreeMap"),
            constants = words("true false None Some Ok Err"),
            modifiers = words("pub mut static const unsafe async"),
            funDecl = words("fn"), classDecl = words("struct enum trait impl mod type union"),
            hashPreprocessor = false
        )

        m["dart"] = LangSpec(
            keywords = words("abstract as assert async await base class const covariant default deferred dynamic enum export extends extension external factory final get hide implements import in interface is late library mixin new on operator part required rethrow sealed set show static super this typedef var with yield"),
            control = cLikeControl,
            types = words("int double num bool String List Map Set Future Stream void dynamic Object Iterable"),
            constants = words("true false null"),
            modifiers = words("final const static late required abstract"),
            funDecl = emptySet(), classDecl = words("class enum mixin extension typedef"),
            triple = true, annotations = true
        )

        val js = LangSpec(
            keywords = words("async await class const debugger delete export extends function get import in instanceof let new of set static super this typeof var void with yield interface type enum implements namespace declare abstract readonly keyof infer as is from"),
            control = words("if else for while do switch case default break continue return try catch finally throw"),
            types = words("string number boolean any unknown never object symbol bigint void Array Promise Map Set Date RegExp Error Record Partial Readonly Pick Omit"),
            constants = words("true false null undefined NaN Infinity"),
            modifiers = words("public private protected static readonly async export default"),
            funDecl = words("function"), classDecl = words("class interface enum type namespace"),
            backtickStrings = true, annotations = true
        )
        for (e in listOf("js", "mjs", "cjs", "jsx", "ts", "tsx")) m[e] = js

        m["py"] = LangSpec(
            keywords = words("as assert async await class def del from global import in is lambda nonlocal not with yield self cls print"),
            control = words("if elif else for while break continue return try except finally raise pass match case and or"),
            types = words("int str float bool list dict set tuple bytes bytearray object type complex frozenset range"),
            constants = words("True False None"),
            funDecl = words("def"), classDecl = words("class"),
            lineComments = listOf("#"), blockStart = null, blockEnd = null,
            triple = true, annotations = true, arrowOps = true
        )

        m["rb"] = LangSpec(
            keywords = words("alias begin class def defined? do end ensure module nil not or and self super then undef yield require require_relative include extend attr_reader attr_writer attr_accessor lambda proc puts"),
            control = words("if elsif else unless case when while until for in break next redo retry return rescue raise"),
            types = words("String Integer Float Array Hash Symbol Object Class Module Proc"),
            constants = words("true false nil"),
            funDecl = words("def"), classDecl = words("class module"),
            lineComments = listOf("#"), blockStart = null, blockEnd = null
        )

        m["php"] = LangSpec(
            keywords = words("abstract as class clone const declare echo enum extends final function global implements include include_once instanceof insteadof interface isset list namespace new print private protected public require require_once static trait use var readonly fn unset empty"),
            control = words("if elseif else for foreach while do switch case default break continue return try catch finally throw match yield"),
            types = words("int float string bool array object callable iterable mixed void null self parent"),
            constants = words("true false null TRUE FALSE NULL"),
            funDecl = words("function"), classDecl = words("class interface trait enum"),
            lineComments = listOf("//", "#"), dollarVars = true, annotations = false
        )

        m["lua"] = LangSpec(
            keywords = words("and do end function in local not or repeat then until self"),
            control = words("if elseif else for while break return goto"),
            types = emptySet(),
            constants = words("true false nil"),
            funDecl = words("function"), lineComments = listOf("--"), blockStart = "--[[", blockEnd = "]]",
            upperIsType = false
        )

        m["sql"] = LangSpec(
            keywords = words("select from where insert into values update set delete create table alter drop index view join inner left right outer full cross on group by order having distinct limit offset union all as primary key foreign references default constraint add column exists in like between is not and or null asc desc with returning pragma begin commit rollback transaction trigger unique check"),
            control = words("case when then else end"),
            types = words("int integer bigint smallint tinyint real double float decimal numeric text varchar char blob boolean date datetime timestamp time"),
            constants = words("true false null"),
            lineComments = listOf("--"), caseInsensitive = true, upperIsType = false
        )

        val shell = LangSpec(
            keywords = words("function local export readonly declare typeset alias unalias source echo printf cd set unset shift exit eval exec trap read test in select"),
            control = words("if then else elif fi for while until do done case esac return break continue"),
            constants = words("true false"),
            lineComments = listOf("#"), blockStart = null, blockEnd = null, dollarVars = true,
            funDecl = words("function"), upperIsType = false
        )
        m["sh"] = shell; m["bash"] = shell; m["zsh"] = shell

        m["yml"] = LangSpec(
            constants = words("true false null yes no on off True False Null"),
            lineComments = listOf("#"), blockStart = null, blockEnd = null, upperIsType = false
        )
        m["yaml"] = m["yml"]!!

        m["toml"] = LangSpec(
            constants = words("true false"),
            lineComments = listOf("#"), blockStart = null, blockEnd = null, upperIsType = false
        )
        m["ini"] = LangSpec(lineComments = listOf(";", "#"), blockStart = null, blockEnd = null, upperIsType = false)
        m["properties"] = m["ini"]!!
        m["cfg"] = m["ini"]!!

        m["json"] = LangSpec(
            constants = words("true false null"),
            lineComments = listOf("//"), blockStart = "/*", blockEnd = "*/", quotes = "\"", upperIsType = false
        )
        m["jsonc"] = m["json"]!!
        m["rinsyntax"] = m["json"]!!

        return m
    }

    /** يُعيد true إن كان لهذا الامتداد ماسح خاص هنا (وليس Rin/indsin/passkit). */
    fun supports(extension: String): Boolean {
        val e = norm(extension)
        return e in specs || e in MARKUP || e in STYLE || e == "md" || e == "markdown"
    }

    private fun norm(extension: String) = extension.lowercase().removePrefix(".")

    private val MARKUP = setOf("html", "htm", "xhtml", "xml", "svg", "xaml", "plist", "vue", "storyboard")
    private val STYLE = setOf("css", "scss", "less", "sass")

    // ------------------------------------------------------------------------------------
    // الواجهة
    // ------------------------------------------------------------------------------------

    fun tokenize(lines: List<String>, extension: String): List<PolySpan> = analyze(lines, extension).spans

    /**
     * نتيجة المسح: [spans] الأمداد الملوّنة، و[continuation] أرقام الأسطر (0-based) التي **تبدأ داخل**
     * نص أو تعليق ممتد من سطر سابق — يحتاجها [CodeFormatter] كي لا يعبث بمحتواها.
     */
    class Result(val spans: List<PolySpan>, val continuation: Set<Int>)

    fun analyze(lines: List<String>, extension: String): Result {
        val e = norm(extension)
        val cont = HashSet<Int>()
        val spans = when {
            e in MARKUP -> MarkupScanner.tokenize(lines, cont)
            e in STYLE -> StyleScanner.tokenize(lines, cont)
            e == "md" || e == "markdown" -> MarkdownScanner.tokenize(lines)
            else -> {
                val spec = specs[e] ?: return Result(emptyList(), cont)
                Generic(spec, jsonKeys = e == "json" || e == "jsonc" || e == "rinsyntax",
                    keyValue = e in KEY_VALUE, cont = cont).run(lines)
            }
        }
        return Result(spans, cont)
    }

    private val KEY_VALUE = setOf("yml", "yaml", "toml", "ini", "properties", "cfg")

    // ------------------------------------------------------------------------------------
    // أدوات مشتركة
    // ------------------------------------------------------------------------------------

    private fun isIdStart(c: Char) = c.isLetter() || c == '_' || c == '$'
    private fun isIdPart(c: Char) = c.isLetterOrDigit() || c == '_' || c == '$'

    /** مسح نص مقتبس يبدأ عند [start]؛ يرجع الفهرس بعد علامة الإغلاق أو نهاية السطر. */
    private fun scanQuoted(text: String, start: Int, quote: Char): Int {
        var i = start + 1
        while (i < text.length) {
            val c = text[i]
            if (c == '\\' && i + 1 < text.length) { i += 2; continue }
            if (c == quote) return i + 1
            i++
        }
        return text.length
    }

    private fun scanNumber(text: String, start: Int): Int {
        var i = start
        val n = text.length
        if (text[i] == '0' && i + 1 < n && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            i += 2
            while (i < n && (text[i].isDigit() || text[i] in 'a'..'f' || text[i] in 'A'..'F' || text[i] == '_')) i++
            while (i < n && (text[i] in "uUlL")) i++
            return i
        }
        if (text[i] == '0' && i + 1 < n && (text[i + 1] == 'b' || text[i + 1] == 'B')) {
            i += 2
            while (i < n && (text[i] == '0' || text[i] == '1' || text[i] == '_')) i++
            return i
        }
        while (i < n && (text[i].isDigit() || text[i] == '_')) i++
        if (i + 1 < n && text[i] == '.' && text[i + 1].isDigit()) {
            i++
            while (i < n && (text[i].isDigit() || text[i] == '_')) i++
        }
        if (i < n && (text[i] == 'e' || text[i] == 'E')) {
            var j = i + 1
            if (j < n && (text[j] == '+' || text[j] == '-')) j++
            if (j < n && text[j].isDigit()) {
                i = j
                while (i < n && text[i].isDigit()) i++
            }
        }
        // لواحق النوع: f F L u U d D ul…
        while (i < n && text[i] in "fFlLuUdD") i++
        return i
    }

    private fun bracketKind(depth: Int): Int = when (((depth % 3) + 3) % 3) {
        0 -> HighlightKind.BRACKET0
        1 -> HighlightKind.BRACKET1
        else -> HighlightKind.BRACKET2
    }

    /** يصنّف محرفاً رمزياً (غير حرف/رقم/فراغ) ويُرجع طول الرمز وكنْهه. أقواس تُعالَج خارجياً. */
    private fun classifySymbol(text: String, i: Int, arrowOps: Boolean): Pair<Int, Int> {
        val c = text[i]
        val c1 = if (i + 1 < text.length) text[i + 1] else '\u0000'
        val c2 = if (i + 2 < text.length) text[i + 2] else '\u0000'
        // ثلاثية
        if ((c == '=' && c1 == '=' && c2 == '=') || (c == '!' && c1 == '=' && c2 == '=')) return 3 to HighlightKind.COMPARE
        if ((c == '<' && c1 == '=' && c2 == '>')) return 3 to HighlightKind.COMPARE
        if ((c == '>' && c1 == '>' && c2 == '>') || (c == '<' && c1 == '<' && c2 == '=') || (c == '>' && c1 == '>' && c2 == '=')) return 3 to HighlightKind.OPERATOR
        if (c == '.' && c1 == '.' && c2 == '.') return 3 to HighlightKind.OPERATOR
        // ثنائية
        if (arrowOps && ((c == '-' && c1 == '>') || (c == '=' && c1 == '>') || (c == '|' && c1 == '>') || (c == '<' && c1 == '-'))) return 2 to HighlightKind.FLOW
        if (c == ':' && c1 == ':') return 2 to HighlightKind.OPERATOR
        if (c1 == '=' && (c == '=' || c == '!' || c == '<' || c == '>')) return 2 to HighlightKind.COMPARE
        if ((c == '&' && c1 == '&') || (c == '|' && c1 == '|') || (c == '?' && c1 == '?') || (c == '?' && c1 == '.') ||
            (c == '+' && c1 == '+') || (c == '-' && c1 == '-') || (c == '<' && c1 == '<') || (c == '>' && c1 == '>') ||
            (c == '.' && c1 == '.') || (c == '*' && c1 == '*') || (c == '/' && c1 == '/'))
            return 2 to HighlightKind.OPERATOR
        if (c1 == '=' && c in "+-*/%&|^") return 2 to HighlightKind.OPERATOR
        // أحادية
        return when (c) {
            '<', '>' -> 1 to HighlightKind.COMPARE
            '+', '-', '*', '/', '%', '=', '&', '|', '^', '~', '!', '?' -> 1 to HighlightKind.OPERATOR
            ',', ';', ':', '.' -> 1 to HighlightKind.PUNCT
            else -> 0 to HighlightKind.DEFAULT
        }
    }

    // ------------------------------------------------------------------------------------
    // الماسح العام (لغات نمط C وبايثون/روبي/لوا/SQL/شِل/JSON/YAML…)
    // ------------------------------------------------------------------------------------

    private class Generic(val spec: LangSpec, val jsonKeys: Boolean, val keyValue: Boolean, val cont: MutableSet<Int>) {
        val out = ArrayList<PolySpan>()
        var depth = 0
        var inBlock = false
        var tripleQuote: String? = null      // الفاصل الجاري ("""/''') إن كنا داخل نص ثلاثي
        var rawBacktick = false

        fun run(lines: List<String>): List<PolySpan> {
            for ((li, text) in lines.withIndex()) {
                if (text.length > MAX_LINE) continue
                scanLine(li, text)
            }
            return out
        }

        private fun add(li: Int, s: Int, e: Int, k: Int) { if (e > s) out.add(PolySpan(li, s, e, k)) }

        private fun kw(word: String) = if (spec.caseInsensitive) word.lowercase() else word

        private fun scanLine(li: Int, text: String) {
            val n = text.length
            var i = 0
            var expectFun = false     // الكلمة التالية اسم دالة
            var expectClass = false   // الكلمة التالية اسم صنف
            var prevSignificant = '\u0000'
            var afterDot = false

            // استمرار داخل نص ثلاثي أو تعليق كتلي أو نص backtick ممتد
            if (tripleQuote != null || rawBacktick || inBlock) cont.add(li)
            if (tripleQuote != null) {
                val end = text.indexOf(tripleQuote!!)
                if (end == -1) { add(li, 0, n, HighlightKind.STRING); return }
                add(li, 0, end + 3, HighlightKind.STRING); tripleQuote = null; i = end + 3
            } else if (rawBacktick) {
                val end = text.indexOf('`')
                if (end == -1) { add(li, 0, n, HighlightKind.STRING); return }
                add(li, 0, end + 1, HighlightKind.STRING); rawBacktick = false; i = end + 1
            } else if (inBlock) {
                val end = text.indexOf(spec.blockEnd!!)
                if (end == -1) { add(li, 0, n, HighlightKind.COMMENT); return }
                add(li, 0, end + spec.blockEnd.length, HighlightKind.COMMENT); inBlock = false; i = end + spec.blockEnd.length
            }

            // معالج مسبق في أول السطر (#include/#define/…)
            if (spec.hashPreprocessor && i == 0) {
                val t = text.trimStart()
                if (t.startsWith("#")) {
                    val s = text.indexOf('#')
                    var e = s + 1
                    while (e < n && (text[e] == ' ' || text[e] == '\t')) e++
                    while (e < n && text[e].isLetter()) e++
                    add(li, s, e, HighlightKind.PREPROCESSOR)
                    // ما بعد التوجيه: <header> أو "header" كنص
                    var j = e
                    while (j < n && text[j] == ' ') j++
                    if (j < n && text[j] == '<') {
                        val close = text.indexOf('>', j)
                        if (close != -1) { add(li, j, close + 1, HighlightKind.STRING); j = close + 1 }
                    }
                    i = j
                    // أكمل مسح بقية السطر بشكل عادي (تعريفات #define تحتوي رموزاً)
                }
            }

            // YAML/TOML/INI: رأس قسم [name] ثم مفتاح قبل = أو :
            if (keyValue && i == 0) {
                val t = text.trim()
                if (t.startsWith("[") && t.endsWith("]")) {
                    add(li, text.indexOf('['), text.trimEnd().length, HighlightKind.CLASS)
                    return
                }
                val keyEnd = keyValueSplit(text)
                if (keyEnd > 0) {
                    val ks = text.indexOfFirst { !it.isWhitespace() && it != '-' }.coerceAtLeast(0)
                    add(li, ks, keyEnd, HighlightKind.PROPERTY)
                    i = keyEnd
                }
            }

            while (i < n) {
                val loop = i
                val c = text[i]

                // تعليق سطر
                var lc: String? = null
                for (p in spec.lineComments) if (text.startsWith(p, i)) { lc = p; break }
                // لا تعتبر # تعليقاً داخل ${...} في شِل أو $#، ولا #[ في رست كتعليق
                if (lc == "#" && spec.dollarVars && i > 0 && text[i - 1] == '$') lc = null
                if (lc != null && spec.blockStart != null && lc == "--" && text.startsWith(spec.blockStart, i)) lc = null
                if (lc != null) { add(li, i, n, HighlightKind.COMMENT); return }

                // تعليق كتلي
                if (spec.blockStart != null && text.startsWith(spec.blockStart, i)) {
                    val end = text.indexOf(spec.blockEnd!!, i + spec.blockStart.length)
                    if (end == -1) { add(li, i, n, HighlightKind.COMMENT); inBlock = true; return }
                    add(li, i, end + spec.blockEnd.length, HighlightKind.COMMENT); i = end + spec.blockEnd.length
                    continue
                }

                // نصوص ثلاثية
                if (spec.triple && (c == '"' || c == '\'') && text.startsWith("$c$c$c", i)) {
                    val delim = "$c$c$c"
                    val end = text.indexOf(delim, i + 3)
                    if (end == -1) { add(li, i, n, HighlightKind.STRING); tripleQuote = delim; return }
                    add(li, i, end + 3, HighlightKind.STRING); i = end + 3
                    prevSignificant = '"'
                    continue
                }

                // نصوص
                if (spec.quotes.indexOf(c) >= 0 && !(c == '\'' && isLifetimeOrApostrophe(text, i))) {
                    val end = scanQuoted(text, i, c)
                    val isKey = jsonKeys && c == '"' && nextNonSpace(text, end) == ':'
                    add(li, i, end, if (isKey) HighlightKind.PROPERTY else HighlightKind.STRING)
                    i = end; prevSignificant = '"'; afterDot = false
                    continue
                }
                if (spec.backtickStrings && c == '`') {
                    val end = text.indexOf('`', i + 1)
                    if (end == -1) { add(li, i, n, HighlightKind.STRING); rawBacktick = true; return }
                    add(li, i, end + 1, HighlightKind.STRING); i = end + 1; prevSignificant = '"'
                    continue
                }

                // أرقام
                if (c.isDigit() || (c == '.' && i + 1 < n && text[i + 1].isDigit() && !(prevSignificant.isLetterOrDigit() || prevSignificant == '_' || prevSignificant == ')' || prevSignificant == ']'))) {
                    val end = scanNumber(text, if (c == '.') i + 1 else i)
                    add(li, i, maxOf(end, i + 1), HighlightKind.NUMBER)
                    i = maxOf(end, i + 1); prevSignificant = '0'; afterDot = false
                    continue
                }

                // تعليقات توجيهية @Name
                if (spec.annotations && c == '@' && i + 1 < n && isIdStart(text[i + 1])) {
                    var e = i + 1
                    while (e < n && (isIdPart(text[e]) || text[e] == '.')) e++
                    add(li, i, e, HighlightKind.AT); i = e; prevSignificant = 'a'
                    continue
                }

                // متغيرات $x و ${x}
                if (spec.dollarVars && c == '$' && i + 1 < n && (isIdStart(text[i + 1]) || text[i + 1] == '{' || text[i + 1].isDigit() || text[i + 1] in "@#?*!")) {
                    var e = i + 1
                    if (text[e] == '{') { val close = text.indexOf('}', e); e = if (close == -1) n else close + 1 }
                    else if (isIdStart(text[e])) { while (e < n && isIdPart(text[e]) && text[e] != '$') e++ }
                    else e++
                    add(li, i, e, HighlightKind.PARAMETER); i = e; prevSignificant = 'a'
                    continue
                }

                // معرّفات وكلمات
                if (isIdStart(c) && !(c == '$' && spec.dollarVars)) {
                    var e = i
                    while (e < n && (isIdPart(text[e]) && !(text[e] == '$' && spec.dollarVars))) e++
                    // لواحق ? و ! في روبي
                    val word = text.substring(i, e)
                    val key = kw(word)
                    val kind = when {
                        afterDot -> if (followedByParen(text, e)) HighlightKind.CALL else HighlightKind.PROPERTY
                        expectFun -> HighlightKind.FUNCTION_DEF
                        expectClass -> HighlightKind.CLASS
                        key in spec.control -> HighlightKind.CONTROL
                        key in spec.constants -> HighlightKind.CONSTANT
                        key in spec.modifiers && key !in spec.keywords -> HighlightKind.MODIFIER
                        key in spec.keywords -> HighlightKind.KEYWORD
                        key in spec.types -> HighlightKind.TYPE
                        followedByParen(text, e) -> HighlightKind.CALL
                        spec.upperIsType && word[0].isUpperCase() && word.length > 1 && !word.all { it.isUpperCase() || it.isDigit() || it == '_' } -> HighlightKind.CLASS
                        word.length > 1 && word.all { it.isUpperCase() || it.isDigit() || it == '_' } && !spec.caseInsensitive -> HighlightKind.CONSTANT
                        else -> HighlightKind.DEFAULT
                    }
                    if (kind != HighlightKind.DEFAULT) add(li, i, e, kind)
                    val wasDecl = expectFun || expectClass
                    expectFun = !afterDot && !wasDecl && key in spec.funDecl
                    expectClass = !afterDot && !wasDecl && key in spec.classDecl
                    afterDot = false
                    i = e; prevSignificant = 'a'
                    continue
                }

                // أقواس متدرّجة
                when (c) {
                    '(', '[', '{' -> { add(li, i, i + 1, bracketKind(depth)); depth++; i++; prevSignificant = c; afterDot = false; expectFun = false; expectClass = false; continue }
                    ')', ']', '}' -> { depth = maxOf(0, depth - 1); add(li, i, i + 1, bracketKind(depth)); i++; prevSignificant = c; afterDot = false; continue }
                }

                // رموز
                if (!c.isWhitespace()) {
                    val (len, kind) = classifySymbol(text, i, spec.arrowOps)
                    if (len > 0) {
                        add(li, i, i + len, kind)
                        afterDot = (len == 1 && c == '.') || (len == 2 && text.startsWith("?.", i)) || (len == 2 && text.startsWith("::", i))
                        i += len; prevSignificant = c
                        if (c == ';') { expectFun = false; expectClass = false }
                        continue
                    }
                    i++; prevSignificant = c
                } else {
                    i++
                }
                if (i <= loop) i = loop + 1
            }
        }

        private fun followedByParen(text: String, from: Int): Boolean {
            var j = from
            while (j < text.length && text[j] == ' ') j++
            return j < text.length && text[j] == '('
        }

        private fun nextNonSpace(text: String, from: Int): Char {
            var j = from
            while (j < text.length && text[j].isWhitespace()) j++
            return if (j < text.length) text[j] else '\u0000'
        }

        /** في Rust/Go الفاصلة العليا قد تكون lifetime ('a) وليست نصاً. */
        private fun isLifetimeOrApostrophe(text: String, i: Int): Boolean {
            if (spec.keywords.contains("fn") && spec.keywords.contains("impl")) {   // Rust فقط
                // lifetime: 'ident بلا إغلاق '؛ مثال 'a أو 'static
                if (i + 2 < text.length && isIdStart(text[i + 1]) && text[i + 2] != '\'') {
                    var j = i + 1
                    while (j < text.length && isIdPart(text[j])) j++
                    return j >= text.length || text[j] != '\''
                }
            }
            return false
        }

        /** موضع نهاية المفتاح في سطر `key: value` أو `key = value`؛ -1 إن لم يكن سطر مفتاح/قيمة. */
        private fun keyValueSplit(text: String): Int {
            val t = text.trimStart()
            if (t.isEmpty() || t.startsWith("#") || t.startsWith(";") || t.startsWith("[")) return -1
            var i = text.length - t.length
            if (text.startsWith("- ", i)) i += 2
            val start = i
            while (i < text.length && (isIdPart(text[i]) || text[i] == '.' || text[i] == '-' || text[i] == '"' || text[i] == '/')) i++
            if (i == start) return -1
            var j = i
            while (j < text.length && text[j] == ' ') j++
            return if (j < text.length && (text[j] == ':' || text[j] == '=')) i else -1
        }
    }

    // ------------------------------------------------------------------------------------
    // HTML / XML / SVG
    // ------------------------------------------------------------------------------------

    private object MarkupScanner {
        fun tokenize(lines: List<String>, cont: MutableSet<Int>): List<PolySpan> {
            val out = ArrayList<PolySpan>()
            var inComment = false
            var inTag = false          // داخل <...> عبر أسطر
            var inCdata = false
            var quote = '\u0000'       // داخل قيمة سمة مقتبسة ممتدة
            for ((li, text) in lines.withIndex()) {
                if (text.length > MAX_LINE) continue
                val n = text.length
                var i = 0
                fun add(s: Int, e: Int, k: Int) { if (e > s) out.add(PolySpan(li, s, e, k)) }
                if (inComment || inCdata || quote != '\u0000') cont.add(li)
                while (i < n) {
                    val loop = i
                    if (inComment) {
                        val end = text.indexOf("-->", i)
                        if (end == -1) { add(i, n, HighlightKind.PK_COMMENT); i = n } else { add(i, end + 3, HighlightKind.PK_COMMENT); i = end + 3; inComment = false }
                    } else if (inCdata) {
                        val end = text.indexOf("]]>", i)
                        if (end == -1) { add(i, n, HighlightKind.STRING); i = n } else { add(i, end + 3, HighlightKind.STRING); i = end + 3; inCdata = false }
                    } else if (inTag) {
                        val c = text[i]
                        when {
                            quote != '\u0000' -> {
                                val end = text.indexOf(quote, i)
                                if (end == -1) { add(i, n, HighlightKind.PK_STRING); i = n } else { add(i, end + 1, HighlightKind.PK_STRING); i = end + 1; quote = '\u0000' }
                            }
                            c == '"' || c == '\'' -> {
                                val end = text.indexOf(c, i + 1)
                                if (end == -1) { add(i, n, HighlightKind.PK_STRING); i = n; quote = c } else { add(i, end + 1, HighlightKind.PK_STRING); i = end + 1 }
                            }
                            c == '>' -> { add(i, i + 1, HighlightKind.PK_BRACKET); i++; inTag = false }
                            c == '/' && i + 1 < n && text[i + 1] == '>' -> { add(i, i + 2, HighlightKind.PK_BRACKET); i += 2; inTag = false }
                            c == '?' && i + 1 < n && text[i + 1] == '>' -> { add(i, i + 2, HighlightKind.PK_BRACKET); i += 2; inTag = false }
                            c == '=' -> { add(i, i + 1, HighlightKind.OPERATOR); i++ }
                            c.isLetter() || c == '_' || c == ':' || c == '@' || c == '-' -> {
                                var e = i + 1
                                while (e < n && (text[e].isLetterOrDigit() || text[e] in "_:.-@")) e++
                                add(i, e, HighlightKind.PK_ATTR); i = e
                            }
                            else -> i++
                        }
                    } else {
                        val lt = text.indexOf('<', i)
                        // مراجع كيانات &amp; في النص
                        if (lt == -1) { markEntities(text, i, n, li, out); i = n }
                        else {
                            markEntities(text, i, lt, li, out)
                            i = lt
                            when {
                                text.startsWith("<!--", i) -> { inComment = true; add(i, i + 4, HighlightKind.PK_COMMENT); i += 4 }
                                text.startsWith("<![CDATA[", i) -> { inCdata = true; add(i, i + 9, HighlightKind.STRING); i += 9 }
                                text.startsWith("<!", i) || text.startsWith("<?", i) -> {
                                    add(i, i + 2, HighlightKind.PK_BRACKET); i += 2
                                    var e = i
                                    while (e < n && (text[e].isLetterOrDigit() || text[e] in "_:-")) e++
                                    add(i, e, HighlightKind.PREPROCESSOR); i = e; inTag = true
                                }
                                else -> {
                                    val closing = i + 1 < n && text[i + 1] == '/'
                                    val nameStart = i + (if (closing) 2 else 1)
                                    if (nameStart < n && (text[nameStart].isLetter() || text[nameStart] == '_')) {
                                        add(i, nameStart, HighlightKind.PK_BRACKET)
                                        var e = nameStart
                                        while (e < n && (text[e].isLetterOrDigit() || text[e] in "_:.-")) e++
                                        val name = text.substring(nameStart, e)
                                        add(nameStart, e, if (name.isNotEmpty() && name[0].isUpperCase()) HighlightKind.PK_CUSTOM_TAG else HighlightKind.PK_TAG)
                                        i = e; inTag = true
                                    } else { i++ }
                                }
                            }
                        }
                    }
                    if (i <= loop) i = loop + 1
                }
            }
            return out
        }

        private fun markEntities(text: String, from: Int, to: Int, li: Int, out: MutableList<PolySpan>) {
            var i = from
            while (i < to) {
                if (text[i] == '&') {
                    val semi = text.indexOf(';', i)
                    if (semi != -1 && semi < to && semi - i <= 10) { out.add(PolySpan(li, i, semi + 1, HighlightKind.CONSTANT)); i = semi + 1; continue }
                }
                i++
            }
        }
    }

    // ------------------------------------------------------------------------------------
    // CSS / SCSS / LESS
    // ------------------------------------------------------------------------------------

    private object StyleScanner {
        fun tokenize(lines: List<String>, cont: MutableSet<Int>): List<PolySpan> {
            val out = ArrayList<PolySpan>()
            var inBlock = false
            var depth = 0          // عمق { }
            var inParen = 0
            for ((li, text) in lines.withIndex()) {
                if (text.length > MAX_LINE) continue
                val n = text.length
                var i = 0
                fun add(s: Int, e: Int, k: Int) { if (e > s) out.add(PolySpan(li, s, e, k)) }
                // داخل قاعدة (عمق>0): الجزء قبل ':' خاصية، وما بعده قيمة. خارجها: محدِّد.
                var seenColon = false
                if (inBlock) cont.add(li)
                while (i < n) {
                    val loop = i
                    val c = text[i]
                    if (inBlock) {
                        val end = text.indexOf("*/", i)
                        if (end == -1) { add(i, n, HighlightKind.COMMENT); i = n } else { add(i, end + 2, HighlightKind.COMMENT); i = end + 2; inBlock = false }
                    } else if (text.startsWith("/*", i)) {
                        val end = text.indexOf("*/", i + 2)
                        if (end == -1) { add(i, n, HighlightKind.COMMENT); i = n; inBlock = true } else { add(i, end + 2, HighlightKind.COMMENT); i = end + 2 }
                    } else if (text.startsWith("//", i)) {
                        add(i, n, HighlightKind.COMMENT); i = n
                    } else if (c == '"' || c == '\'') {
                        val end = scanQuoted(text, i, c); add(i, end, HighlightKind.STRING); i = end
                    } else if (c == '{') { add(i, i + 1, bracketKind(depth)); depth++; i++; seenColon = false }
                    else if (c == '}') { depth = maxOf(0, depth - 1); add(i, i + 1, bracketKind(depth)); i++; seenColon = false }
                    else if (c == '(') { add(i, i + 1, bracketKind(depth + inParen)); inParen++; i++ }
                    else if (c == ')') { inParen = maxOf(0, inParen - 1); add(i, i + 1, bracketKind(depth + inParen)); i++ }
                    else if (c == ';') { add(i, i + 1, HighlightKind.PUNCT); i++; seenColon = false }
                    else if (c == ':' ) {
                        if (depth > 0 && !seenColon) seenColon = true
                        add(i, i + 1, HighlightKind.PUNCT); i++
                    }
                    else if (c == ',') { add(i, i + 1, HighlightKind.PUNCT); i++ }
                    else if (c == '@' ) {
                        var e = i + 1
                        while (e < n && (text[e].isLetterOrDigit() || text[e] == '-' || text[e] == '_')) e++
                        add(i, e, HighlightKind.AT); i = e
                    }
                    else if (c == '#' && depth > 0 && seenColon) {
                        var e = i + 1
                        while (e < n && (text[e].isDigit() || text[e] in 'a'..'f' || text[e] in 'A'..'F')) e++
                        add(i, e, if (e - i in setOf(4, 5, 7, 9)) HighlightKind.IX_COLOR else HighlightKind.NUMBER); i = e
                    }
                    else if (c == '#' || c == '.') {
                        // محدِّد id/class (خارج الكتل أو قبل {)
                        var e = i + 1
                        while (e < n && (text[e].isLetterOrDigit() || text[e] in "-_")) e++
                        if (e > i + 1 && !(depth > 0 && seenColon)) { add(i, e, if (c == '#') HighlightKind.IX_ID else HighlightKind.CLASS); i = e }
                        else if (c == '.' && i + 1 < n && text[i + 1].isDigit()) { val e2 = scanNumber(text, i + 1); add(i, e2, HighlightKind.NUMBER); i = e2 }
                        else { add(i, i + 1, HighlightKind.PUNCT); i++ }
                    }
                    else if (c.isDigit() || ((c == '-' || c == '+') && i + 1 < n && (text[i + 1].isDigit() || text[i + 1] == '.') && depth > 0)) {
                        var e = i + (if (c == '-' || c == '+') 1 else 0)
                        e = scanNumber(text, e)
                        // الوحدة px em % ...
                        while (e < n && (text[e].isLetter() || text[e] == '%')) e++
                        add(i, e, HighlightKind.NUMBER); i = e
                    }
                    else if (c == '$' || (c == '-' && i + 1 < n && text[i + 1] == '-')) {
                        var e = i + 1
                        while (e < n && (text[e].isLetterOrDigit() || text[e] in "-_")) e++
                        add(i, e, HighlightKind.PARAMETER); i = e
                    }
                    else if (c.isLetter() || c == '-' || c == '_') {
                        var e = i
                        while (e < n && (text[e].isLetterOrDigit() || text[e] in "-_")) e++
                        val word = text.substring(i, e)
                        val kind = when {
                            e < n && text[e] == '(' -> HighlightKind.CALL
                            depth > 0 && !seenColon -> HighlightKind.PROPERTY       // اسم الخاصية
                            depth > 0 && seenColon -> if (word in CSS_CONSTS) HighlightKind.CONSTANT else HighlightKind.DEFAULT
                            else -> HighlightKind.TYPE                               // وسم في المحدِّد: div, body …
                        }
                        if (kind != HighlightKind.DEFAULT) add(i, e, kind)
                        i = e
                    }
                    else if (c == '>' || c == '+' || c == '~' || c == '*' || c == '=') { add(i, i + 1, HighlightKind.OPERATOR); i++ }
                    else i++
                    if (i <= loop) i = loop + 1
                }
            }
            return out
        }

        private val CSS_CONSTS = setOf("none", "auto", "inherit", "initial", "unset", "block", "flex", "grid", "inline", "inline-block", "absolute", "relative", "fixed", "sticky",
            "bold", "normal", "italic", "solid", "dashed", "dotted", "center", "left", "right", "top", "bottom", "transparent", "hidden", "visible", "pointer", "nowrap", "wrap", "row", "column", "important")
    }

    // ------------------------------------------------------------------------------------
    // Markdown
    // ------------------------------------------------------------------------------------

    private object MarkdownScanner {
        private val RULE = Regex("^([-*_])\\1{2,}\\s*$")
        private val BULLET = Regex("^(\\s*)([-*+]|\\d+[.)])\\s")

        fun tokenize(lines: List<String>): List<PolySpan> {
            val out = ArrayList<PolySpan>()
            var fence = false
            for ((li, text) in lines.withIndex()) {
                if (text.length > MAX_LINE) continue
                val n = text.length
                fun add(s: Int, e: Int, k: Int) { if (e > s) out.add(PolySpan(li, s, e, k)) }
                val t = text.trimStart()
                if (t.startsWith("```") || t.startsWith("~~~")) { add(0, n, HighlightKind.PREPROCESSOR); fence = !fence; continue }
                if (fence) { add(0, n, HighlightKind.STRING); continue }
                if (t.startsWith("#")) {
                    val lead = n - t.length
                    var h = 0
                    while (h < t.length && t[h] == '#') h++
                    if (h <= 6 && (h == t.length || t[h] == ' ')) { add(lead, n, HighlightKind.KEYWORD); continue }
                }
                if (t.startsWith(">")) { add(0, n, HighlightKind.COMMENT); continue }
                if (RULE.matches(t)) { add(0, n, HighlightKind.PUNCT); continue }
                // قوائم
                val bullet = BULLET.find(text)
                if (bullet != null) add(bullet.groups[2]!!.range.first, bullet.groups[2]!!.range.last + 1, HighlightKind.OPERATOR)
                // مضمّنات: `code` **bold** [text](url)
                var i = 0
                while (i < n) {
                    val c = text[i]
                    when {
                        c == '`' -> { val e = text.indexOf('`', i + 1); if (e == -1) { i++ } else { add(i, e + 1, HighlightKind.STRING); i = e + 1 } }
                        c == '[' -> {
                            val close = text.indexOf("](", i)
                            if (close != -1) {
                                val end = text.indexOf(')', close)
                                if (end != -1) { add(i, close + 1, HighlightKind.TYPE); add(close + 1, end + 1, HighlightKind.PROPERTY); i = end + 1 } else i++
                            } else i++
                        }
                        (c == '*' || c == '_') && i + 1 < n && text[i + 1] == c -> {
                            val e = text.indexOf("$c$c", i + 2)
                            if (e != -1) { add(i, e + 2, HighlightKind.CONTROL); i = e + 2 } else i += 2
                        }
                        else -> i++
                    }
                }
            }
            return out
        }
    }
}

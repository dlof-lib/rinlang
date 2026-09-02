package com.dlof.rinlang

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.Spannable
import android.text.TextWatcher
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
import android.widget.EditText
import androidx.core.content.ContextCompat
import java.util.regex.Pattern

/**
 * تلوين الصيغة النحوية (syntax highlighting) مباشرة أثناء الكتابة — يدعم لغة Rin نفسها
 * بكامل خصوصياتها، بالإضافة إلى تلوين عام لأشهر لغات/صيغ الملفات الأخرى التي قد تُفتَح في
 * نفس المحرر (C/C++, Kotlin, Java, Python, JavaScript/TypeScript, HTML/XML, CSS, JSON, SQL,
 * Shell)، بدل تلوينها خطأً بقواعد Rin كما كان يحدث سابقاً بغضّ النظر عن امتداد الملف المفتوح.
 *
 * يعمل بمسح النص كاملاً بتعبيرات نمطية (regex) عند كل تعديل، وتطبيق [ForegroundColorSpan]
 * على كل جزء مطابق. هذا لا يُسبّب حلقة لا نهائية لأن ضبط الـ spans لا يُشغّل [TextWatcher]
 * (الذي يستمع فقط لتغيّرات *محتوى* النص، وليس تنسيقه).
 *
 * الاستخدام:
 * - `RinSyntaxHighlighter.attach(context, editText, extension)` مرة واحدة عند إنشاء المحرر.
 * - `RinSyntaxHighlighter.setLanguage(extension)` كل مرة يُفتَح فيها ملف مختلف الامتداد في
 *   نفس المحرر (مثال: openFile في MainActivity)، لتبديل قواعد التلوين فوراً بلا تهدئة.
 *
 * تحسينات على النسخة الأساسية (خاصة بـ Rin):
 * - **تغطية كاملة للكلمات المفتاحية**: تُضاف الكلمات السياقية (row/style/document/route/
 *   data/api/import/table/doc/portal/block/pipe) التي كانت غير مُلوَّنة سابقاً رغم كونها جزءاً
 *   حقيقياً من صياغة اللغة (انظر Parser::declaration في rin_parser.cpp).
 * - **فئة لونية مستقلة لحقول الستايل** (txt/img/object.file/Fonts/background/css3) بنفس هوية
 *   "الستايل" اللونية المستخدمة في كونسول التشغيل (log_kind_style)، لتمييزها بصرياً عن بقية
 *   حقول لغة الحاويات العامة.
 * - **نصوص تدعم التهريب (escape sequences)** بنفس أسلوب rin_lexer.cpp::scanString، بحيث لا ينتهي
 *   تلوين النص عند أول علامة تنصيص مُهرَّبة (`\"`) بداخله.
 * - **تهدئة (debounce)** لإعادة التلوين بدل تنفيذه عند كل حرف يُكتب، لتحسين استجابة المحرر في
 *   الملفات الأطول، مع تلوين فوري (بلا تهدئة) عند فتح المحرر لأول مرة أو تبديل اللغة.
 * - **حدّ أقصى آمن لطول النص**: الملفات الضخمة جداً تُترك بلا تلوين بدل تجميد الواجهة (ANR).
 * - **مفاهيم وحدة make (@make.(name))** (انظر docs/MAKE_UNIT.md): توجيهات السياسة
 *   kind/use/need/allow/deny/strict/input/output/public/private/version/description تُلوَّن
 *   بفئة مستقلة [syntax_make_directive]، وأسماء القدرات (capabilities) الجديدة
 *   loop/function/view/chatbot أُضيفت إلى كلمات لغة الحاويات (container/data/api/import/table/doc
 *   وreturn كانت مُلوَّنة أصلاً).
 */
object RinSyntaxHighlighter {

    /** أقصى طول نص (بالحرف) نطبّق عليه التلوين؛ نصوص أكبر تُترك بلا تلوين لتفادي تجميد واجهة
     *  المستخدم عند مسح كامل ملف ضخم بتعبيرات نمطية عند كل تعديل. */
    private const val MAX_HIGHLIGHT_LENGTH = 200_000

    /** مهلة التهدئة (بالمللي ثانية) قبل إعادة التلوين بعد آخر تعديل في النص. */
    private const val DEBOUNCE_MILLIS = 120L

    // ----------------------------------------------------------------------------------
    // قواعد Rin (اللغة الأساسية للتطبيق) — كما كانت، مع فئات لونية مخصّصة لها فقط.
    // ----------------------------------------------------------------------------------

    private val rinCoreKeywords = listOf(
        "let", "print", "if", "else", "while", "for", "fun", "return", "break", "continue",
        "true", "false", "nil", "and", "or"
    )

    // كلمات لغة الحاويات/البيانات (data container language) - حساسة لحالة الأحرف كما في المحرّك.
    // تتضمن أيضاً أسماء قدرات (capabilities) وحدة make: loop/function/condition/view/chatbot
    // (container/data/api/import/table/doc/return مذكورة أصلاً ضمن قوائم أخرى).
    private val rinContainerKeywords = listOf(
        "text", "container", "Containers", "Group", "Volume", "Section",
        "Translations", "translation", "link", "tying", "merge",
        "installation", "simplified", "save", "file", "end",
        "row", "style", "document", "route",
        "data", "api", "import", "table", "doc", "portal", "block", "pipe",
        "plus", "condition",
        "loop", "function", "view", "chatbot"
    )

    // توجيهات سياسة وحدة make (@make.(name) ... kind/use/need/allow/deny/strict/...) —
    // انظر docs/MAKE_UNIT.md. فئة لونية مستقلة (بنفسجي) لتمييزها عن كلمات التحكم العادية.
    private val rinMakeDirectiveKeywords = listOf(
        "kind", "use", "need", "allow", "deny", "strict",
        "input", "output", "public", "private", "version", "description"
    )

    // حقول تنسيق/ستايل خاصة حصراً بـ @container.object/@Object: txt/img/object.file/Fonts/
    // background/css3.
    private val rinStyleFieldKeywords = listOf(
        "txt", "img", "object", "Fonts", "background", "css3"
    )

    private val rinBuiltins = listOf(
        "Addition", "Subtraction", "Multiplication", "Equal",
        "abs", "sqrt", "pow", "floor", "ceil", "round", "min", "max", "random",
        "len", "upper", "lower", "trim", "substr", "split", "join",
        "indexOf", "replace", "contains", "charAt", "toString", "toNumber",
        "push", "pop", "sort", "keys", "values", "has", "remove"
    )

    // نص محاط بعلامتي تنصيص مع دعم التهريب (\" \\ \n ...) بنفس أسلوب rin_lexer.cpp::scanString.
    private val defaultString = Pattern.compile("\"(?:\\\\.|[^\"\\\\])*\"")
    private val defaultNumber = Pattern.compile("\\b\\d+(\\.\\d+)?\\b")
    private val lineCommentSlash = Pattern.compile("//[^\\n]*")
    private val blockCommentCStyle = Pattern.compile("/\\*[\\s\\S]*?\\*/")
    private val rinTag = Pattern.compile("@[A-Za-z][A-Za-z0-9_.]*|\\.end/[A-Za-z0-9_.]*")

    private fun wordsPattern(words: List<String>, caseInsensitive: Boolean = false): Pattern {
        val flags = if (caseInsensitive) Pattern.CASE_INSENSITIVE else 0
        return Pattern.compile("\\b(" + words.joinToString("|") { Pattern.quote(it) } + ")\\b", flags)
    }

    /**
     * مجموعة الأنماط (patterns) المطلوبة لتلوين لغة واحدة. كل حقل اختياري: تُترك اللغات
     * الأبسط (JSON مثلاً) بلا [tag] أو [secondaryKeyword]، وتُترك اللغات التي لا تدعم تعليقات
     * كتلية (Python, Shell) بلا [blockComment].
     */
    private class LangProfile(
        val keyword: Pattern? = null,
        val secondaryKeyword: Pattern? = null, // أنواع البيانات (types) في اللغات المطبوعة/الحاويات في Rin
        val styleField: Pattern? = null,       // حصراً في Rin
        val makeDirective: Pattern? = null,    // توجيهات سياسة وحدة make (kind/use/need/...) — حصراً في Rin
        val builtin: Pattern? = null,
        val number: Pattern? = defaultNumber,
        val string: Pattern? = defaultString,
        val tag: Pattern? = null,              // preprocessor / annotations / decorators / HTML tags
        val lineComment: Pattern? = null,
        val blockComment: Pattern? = null
    )

    private val rinProfile = LangProfile(
        keyword = wordsPattern(rinCoreKeywords),
        secondaryKeyword = wordsPattern(rinContainerKeywords),
        styleField = wordsPattern(rinStyleFieldKeywords),
        makeDirective = wordsPattern(rinMakeDirectiveKeywords),
        builtin = wordsPattern(rinBuiltins),
        tag = rinTag,
        lineComment = lineCommentSlash
    )

    // ----------------------------------------------------------------------------------
    // C / C++
    // ----------------------------------------------------------------------------------
    private val cppKeywords = listOf(
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
        "return", "goto", "class", "struct", "union", "enum", "namespace", "using", "typedef",
        "public", "private", "protected", "virtual", "override", "final", "friend", "template",
        "typename", "new", "delete", "try", "catch", "throw", "sizeof", "static", "const",
        "constexpr", "inline", "extern", "volatile", "mutable", "explicit", "operator",
        "this", "true", "false", "nullptr", "NULL"
    )
    private val cppTypes = listOf(
        "int", "float", "double", "char", "bool", "void", "long", "short", "unsigned", "signed",
        "auto", "size_t", "string", "wchar_t", "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t"
    )
    private val cppPreprocessor = Pattern.compile("(?m)^\\s*#\\s*\\w+")
    private val cCharLiteral = Pattern.compile("'(?:\\\\.|[^'\\\\])*'")
    private val cppProfile = LangProfile(
        keyword = wordsPattern(cppKeywords),
        secondaryKeyword = wordsPattern(cppTypes),
        string = Pattern.compile(defaultString.pattern() + "|" + cCharLiteral.pattern()),
        tag = cppPreprocessor,
        lineComment = lineCommentSlash,
        blockComment = blockCommentCStyle
    )

    // ----------------------------------------------------------------------------------
    // Kotlin
    // ----------------------------------------------------------------------------------
    private val kotlinKeywords = listOf(
        "fun", "val", "var", "if", "else", "when", "for", "while", "do", "return", "break",
        "continue", "class", "object", "interface", "companion", "init", "constructor", "try",
        "catch", "finally", "throw", "is", "as", "in", "null", "true", "false", "this", "super",
        "override", "open", "abstract", "sealed", "data", "enum", "annotation", "private",
        "protected", "public", "internal", "lateinit", "lazy", "suspend", "inline", "vararg",
        "import", "package", "by", "get", "set", "typealias", "reified", "crossinline", "noinline"
    )
    private val kotlinTypes = listOf(
        "Int", "String", "Boolean", "Double", "Float", "Long", "Short", "Byte", "Char", "Unit",
        "Any", "List", "MutableList", "Map", "MutableMap", "Set", "MutableSet", "Array", "Nothing"
    )
    private val kotlinAnnotation = Pattern.compile("@[A-Za-z][A-Za-z0-9_]*")
    private val kotlinProfile = LangProfile(
        keyword = wordsPattern(kotlinKeywords),
        secondaryKeyword = wordsPattern(kotlinTypes),
        string = Pattern.compile(defaultString.pattern() + "|" + cCharLiteral.pattern()),
        tag = kotlinAnnotation,
        lineComment = lineCommentSlash,
        blockComment = blockCommentCStyle
    )

    // ----------------------------------------------------------------------------------
    // Java
    // ----------------------------------------------------------------------------------
    private val javaKeywords = listOf(
        "if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue",
        "return", "class", "interface", "enum", "extends", "implements", "new", "try", "catch",
        "finally", "throw", "throws", "public", "private", "protected", "static", "final",
        "abstract", "synchronized", "volatile", "transient", "native", "this", "super",
        "package", "import", "instanceof", "true", "false", "null", "void"
    )
    private val javaTypes = listOf(
        "int", "float", "double", "char", "boolean", "long", "short", "byte",
        "String", "Integer", "Double", "Float", "Boolean", "Long", "Object", "List", "Map", "Set"
    )
    private val javaAnnotation = Pattern.compile("@[A-Za-z][A-Za-z0-9_]*")
    private val javaProfile = LangProfile(
        keyword = wordsPattern(javaKeywords),
        secondaryKeyword = wordsPattern(javaTypes),
        string = Pattern.compile(defaultString.pattern() + "|" + cCharLiteral.pattern()),
        tag = javaAnnotation,
        lineComment = lineCommentSlash,
        blockComment = blockCommentCStyle
    )

    // ----------------------------------------------------------------------------------
    // Python
    // ----------------------------------------------------------------------------------
    private val pythonKeywords = listOf(
        "def", "class", "if", "elif", "else", "for", "while", "return", "import", "from", "as",
        "try", "except", "finally", "with", "lambda", "pass", "break", "continue", "global",
        "nonlocal", "yield", "assert", "raise", "in", "is", "not", "and", "or", "None", "True",
        "False", "self", "async", "await", "del"
    )
    private val pythonTypes = listOf(
        "int", "str", "float", "bool", "list", "dict", "set", "tuple", "bytes", "object"
    )
    private val pythonDecorator = Pattern.compile("(?m)^\\s*@[A-Za-z_][A-Za-z0-9_.]*")
    private val pythonString = Pattern.compile(
        "\"\"\"[\\s\\S]*?\"\"\"|'''[\\s\\S]*?'''|" + defaultString.pattern() + "|" + cCharLiteral.pattern()
    )
    private val pythonProfile = LangProfile(
        keyword = wordsPattern(pythonKeywords),
        secondaryKeyword = wordsPattern(pythonTypes),
        string = pythonString,
        tag = pythonDecorator,
        lineComment = Pattern.compile("#[^\\n]*")
    )

    // ----------------------------------------------------------------------------------
    // JavaScript / TypeScript (يشمل JSX/TSX بالتقريب — بلا تلوين خاص للوسوم)
    // ----------------------------------------------------------------------------------
    private val jsKeywords = listOf(
        "function", "var", "let", "const", "if", "else", "for", "while", "do", "return", "class",
        "extends", "new", "this", "typeof", "instanceof", "try", "catch", "finally", "throw",
        "switch", "case", "default", "break", "continue", "import", "export", "from", "as",
        "async", "await", "yield", "null", "undefined", "true", "false", "in", "of", "void",
        "delete", "static", "get", "set", "super", "interface", "type", "implements", "enum",
        "public", "private", "protected", "readonly", "namespace", "declare"
    )
    private val tsTypes = listOf(
        "string", "number", "boolean", "any", "void", "never", "unknown", "object", "symbol", "bigint"
    )
    private val jsTemplateOrString = Pattern.compile(
        "`(?:\\\\.|[^`\\\\])*`|" + defaultString.pattern() + "|'(?:\\\\.|[^'\\\\])*'"
    )
    private val jsDecorator = Pattern.compile("@[A-Za-z][A-Za-z0-9_]*")
    private val jsProfile = LangProfile(
        keyword = wordsPattern(jsKeywords),
        secondaryKeyword = wordsPattern(tsTypes),
        string = jsTemplateOrString,
        tag = jsDecorator,
        lineComment = lineCommentSlash,
        blockComment = blockCommentCStyle
    )

    // ----------------------------------------------------------------------------------
    // HTML / XML
    // ----------------------------------------------------------------------------------
    private val htmlProfile = LangProfile(
        string = Pattern.compile("\"[^\"]*\"|'[^']*'"),
        tag = Pattern.compile("</?[A-Za-z!][^>]*>"),
        blockComment = Pattern.compile("<!--[\\s\\S]*?-->")
    )

    // ----------------------------------------------------------------------------------
    // CSS
    // ----------------------------------------------------------------------------------
    private val cssProfile = LangProfile(
        keyword = Pattern.compile("(?m)^\\s*(@[A-Za-z-]+)"),
        string = Pattern.compile("\"[^\"]*\"|'[^']*'"),
        number = Pattern.compile("-?\\b\\d+(\\.\\d+)?(px|em|rem|%|vh|vw|s|ms)?\\b"),
        blockComment = Pattern.compile("/\\*[\\s\\S]*?\\*/")
    )

    // ----------------------------------------------------------------------------------
    // JSON
    // ----------------------------------------------------------------------------------
    private val jsonProfile = LangProfile(
        keyword = wordsPattern(listOf("true", "false", "null")),
        string = defaultString
    )

    // ----------------------------------------------------------------------------------
    // SQL
    // ----------------------------------------------------------------------------------
    private val sqlKeywords = listOf(
        "select", "from", "where", "insert", "into", "values", "update", "set", "delete",
        "create", "table", "alter", "drop", "join", "inner", "left", "right", "outer", "on",
        "group", "by", "order", "having", "and", "or", "not", "null", "as", "distinct", "limit",
        "union", "all", "in", "like", "between", "case", "when", "then", "end", "else"
    )
    private val sqlProfile = LangProfile(
        keyword = wordsPattern(sqlKeywords, caseInsensitive = true),
        string = Pattern.compile("'(?:''|[^'])*'"),
        lineComment = Pattern.compile("--[^\\n]*")
    )

    // ----------------------------------------------------------------------------------
    // Shell (sh/bash)
    // ----------------------------------------------------------------------------------
    private val shellKeywords = listOf(
        "if", "then", "else", "elif", "fi", "for", "while", "do", "done", "case", "esac",
        "function", "return", "export", "local", "echo", "in", "select", "until"
    )
    private val shellProfile = LangProfile(
        keyword = wordsPattern(shellKeywords),
        string = Pattern.compile(defaultString.pattern() + "|'[^']*'"),
        tag = Pattern.compile("\\$\\{?[A-Za-z_][A-Za-z0-9_]*\\}?"),
        lineComment = Pattern.compile("#[^\\n]*")
    )

    /** لا تلوين مطلقاً (نص عادي) — تُستخدم للامتدادات غير المعروفة بدل تطبيق قواعد Rin خطأً. */
    private val plainProfile = LangProfile()

    /** يحدّد ملف تعريف اللغة المناسب حسب امتداد الملف (بلا نقطة، بحروف صغيرة). */
    private fun profileFor(extension: String): LangProfile = when (extension.lowercase()) {
        "rin" -> rinProfile
        "c", "h", "cpp", "cc", "cxx", "hpp", "hh" -> cppProfile
        "kt", "kts" -> kotlinProfile
        "java" -> javaProfile
        "py" -> pythonProfile
        "js", "mjs", "jsx", "ts", "tsx" -> jsProfile
        "html", "htm", "xml" -> htmlProfile
        "css" -> cssProfile
        "json" -> jsonProfile
        "sql" -> sqlProfile
        "sh", "bash" -> shellProfile
        "" -> rinProfile // ملفات بلا امتداد داخل التطبيق تُعامَل كملفات Rin افتراضياً
        else -> plainProfile
    }

    private data class Colors(
        val keyword: Int,
        val secondaryKeyword: Int,
        val styleField: Int,
        val makeDirective: Int,
        val string: Int,
        val number: Int,
        val comment: Int,
        val builtin: Int,
        val tag: Int
    )

    private val mainHandler = Handler(Looper.getMainLooper())
    private var pendingHighlight: Runnable? = null

    // حالة آخر [attach] لتمكين [setLanguage] من إعادة التلوين فوراً بلا إعادة تسجيل TextWatcher.
    private var activeEditText: EditText? = null
    private var activeColors: Colors? = null
    private var activeProfile: LangProfile = rinProfile

    /**
     * يربط التلوين الحي بحقل تحرير الكود. آمن للاستدعاء مرة واحدة فقط لكل [EditText].
     * [extension] هو امتداد الملف المفتوح حالياً (بلا نقطة، مثال: "rin", "cpp", "kt")؛
     * يحدّد أي مجموعة كلمات مفتاحية/ألوان تُطبَّق. استخدم [setLanguage] لاحقاً لتغييره.
     */
    fun attach(context: Context, editText: EditText, extension: String = "rin") {
        val colors = Colors(
            keyword = ContextCompat.getColor(context, R.color.syntax_keyword),
            secondaryKeyword = ContextCompat.getColor(context, R.color.syntax_container_keyword),
            styleField = ContextCompat.getColor(context, R.color.syntax_style_keyword),
            makeDirective = ContextCompat.getColor(context, R.color.syntax_make_directive),
            string = ContextCompat.getColor(context, R.color.syntax_string),
            number = ContextCompat.getColor(context, R.color.syntax_number),
            comment = ContextCompat.getColor(context, R.color.syntax_comment),
            builtin = ContextCompat.getColor(context, R.color.syntax_builtin),
            tag = ContextCompat.getColor(context, R.color.syntax_tag)
        )
        activeEditText = editText
        activeColors = colors
        activeProfile = profileFor(extension)

        fun runHighlight(editable: Editable) {
            highlight(editable, activeProfile, colors)
        }

        fun scheduleHighlight(editable: Editable) {
            pendingHighlight?.let { mainHandler.removeCallbacks(it) }
            val runnable = Runnable { runHighlight(editable) }
            pendingHighlight = runnable
            mainHandler.postDelayed(runnable, DEBOUNCE_MILLIS)
        }

        editText.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(editable: Editable?) {
                if (editable == null) return
                // لا نعيد التلوين إن كان EditText قد استُبدل بآخر عبر attach جديد (نادر عملياً).
                if (activeEditText !== editText) return
                scheduleHighlight(editable)
            }
        })

        // تلوين أولي فوري (بلا تهدئة) للنص الموجود عند فتح المحرر لأول مرة
        editText.text?.let { runHighlight(it) }
    }

    /**
     * يبدّل لغة التلوين الحالية (مثال: عند فتح ملف .cpp بعد أن كان المحرر على ملف .rin)
     * ويعيد تلوين النص الحالي فوراً بلا تهدئة. لا يفعل شيئاً إن لم يسبق نداء [attach].
     */
    fun setLanguage(extension: String) {
        val editText = activeEditText ?: return
        val colors = activeColors ?: return
        activeProfile = profileFor(extension)
        editText.text?.let { highlight(it, activeProfile, colors) }
    }

    private fun highlight(editable: Editable, profile: LangProfile, colors: Colors) {
        // إزالة كل الـ spans السابقة التي وضعناها نحن فقط
        val existing = editable.getSpans(0, editable.length, ForegroundColorSpan::class.java)
        for (span in existing) editable.removeSpan(span)
        val existingStyles = editable.getSpans(0, editable.length, StyleSpan::class.java)
        for (span in existingStyles) editable.removeSpan(span)

        // ملفات ضخمة جداً: نكتفي بإزالة أي تلوين سابق (أعلاه) ونترك النص بلا تلوين، بدل تجميد
        // واجهة المستخدم بمسح كامل بتعبيرات نمطية على نص طويل جداً.
        if (editable.length > MAX_HIGHLIGHT_LENGTH) return

        val text = editable.toString()

        fun applyAll(pattern: Pattern?, color: Int, italic: Boolean = false) {
            if (pattern == null) return
            val matcher = pattern.matcher(text)
            while (matcher.find()) {
                editable.setSpan(
                    ForegroundColorSpan(color),
                    matcher.start(), matcher.end(),
                    Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                if (italic) {
                    editable.setSpan(
                        StyleSpan(android.graphics.Typeface.ITALIC),
                        matcher.start(), matcher.end(),
                        Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                }
            }
        }

        // الترتيب مهم: النصوص والتعليقات تُطبَّق أخيراً حتى "تكسب" فوق أي تطابق كلمة مفتاحية
        // وقع بالخطأ داخلها (مثال: كلمة "if" داخل نص "if you can")، والتعليقات الكتلية (/* */)
        // تُطبَّق آخر الجميع لأنها قد تحتوي على علامات تنصيص لا يجب تلوينها كنص.
        applyAll(profile.keyword, colors.keyword)
        applyAll(profile.secondaryKeyword, colors.secondaryKeyword)
        applyAll(profile.styleField, colors.styleField)
        applyAll(profile.makeDirective, colors.makeDirective)
        applyAll(profile.builtin, colors.builtin)
        applyAll(profile.number, colors.number)
        applyAll(profile.tag, colors.tag)
        applyAll(profile.string, colors.string)
        applyAll(profile.lineComment, colors.comment, italic = true)
        applyAll(profile.blockComment, colors.comment, italic = true)
    }
}

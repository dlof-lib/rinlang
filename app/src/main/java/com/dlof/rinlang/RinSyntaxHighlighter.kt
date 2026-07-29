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
 * تلوين الصيغة النحوية (syntax highlighting) لِلغة Rin مباشرة أثناء الكتابة.
 *
 * يعمل بمسح النص كاملاً بتعبيرات نمطية (regex) عند كل تعديل، وتطبيق [ForegroundColorSpan]
 * على كل جزء مطابق. هذا لا يُسبّب حلقة لا نهائية لأن ضبط الـ spans لا يُشغّل [TextWatcher]
 * (الذي يستمع فقط لتغيّرات *محتوى* النص، وليس تنسيقه).
 *
 * تحسينات إضافية على النسخة الأساسية:
 * - **تغطية كاملة للكلمات المفتاحية**: تُضاف الكلمات السياقية (row/style/document/route/
 *   data/api/import/table/doc/portal/block/pipe) التي كانت غير مُلوَّنة سابقاً رغم كونها جزءاً
 *   حقيقياً من صياغة اللغة (انظر Parser::declaration في rin_parser.cpp).
 * - **فئة لونية مستقلة لحقول الستايل** (txt/img/object.file/Fonts/background/css3) بنفس هوية
 *   "الستايل" اللونية المستخدمة في كونسول التشغيل (log_kind_style)، لتمييزها بصرياً عن بقية
 *   حقول لغة الحاويات العامة.
 * - **نصوص تدعم التهريب (escape sequences)** بنفس أسلوب rin_lexer.cpp::scanString، بحيث لا ينتهي
 *   تلوين النص عند أول علامة تنصيص مُهرَّبة (`\"`) بداخله.
 * - **تهدئة (debounce)** لإعادة التلوين بدل تنفيذه عند كل حرف يُكتب، لتحسين استجابة المحرر في
 *   الملفات الأطول، مع تلوين فوري (بلا تهدئة) عند فتح المحرر لأول مرة.
 * - **حدّ أقصى آمن لطول النص**: الملفات الضخمة جداً تُترك بلا تلوين بدل تجميد الواجهة (ANR).
 */
object RinSyntaxHighlighter {

    /** أقصى طول نص (بالحرف) نطبّق عليه التلوين؛ نصوص أكبر تُترك بلا تلوين لتفادي تجميد واجهة
     *  المستخدم عند مسح كامل ملف ضخم بتعبيرات نمطية عند كل تعديل. */
    private const val MAX_HIGHLIGHT_LENGTH = 200_000

    /** مهلة التهدئة (بالمللي ثانية) قبل إعادة التلوين بعد آخر تعديل في النص. */
    private const val DEBOUNCE_MILLIS = 120L

    // كلمات لغة Rin الأساسية (تحكّم بالتدفّق، تعريف متغيرات ودوال)
    private val coreKeywords = listOf(
        "let", "print", "if", "else", "while", "fun", "return",
        "true", "false", "nil", "and", "or"
    )

    // كلمات لغة الحاويات/البيانات (data container language) - حساسة لحالة الأحرف كما في المحرّك.
    // تشمل الوسوم المحجوزة (container/Containers/Group/Volume/Section/Translations...) والكلمات
    // السياقية غير المحجوزة عالمياً (row/style/document/route/data/api/import/table/doc/portal/
    // block/pipe) التي لا تتحوّل إلى عبارة خاصة إلا عند ظهورها أول عبارة (انظر
    // Parser::declaration في rin_parser.cpp)؛ نُلوّنها هنا دوماً لتسهيل قراءة الكود، حتى لو
    // استُخدم أحدها أحياناً كاسم متغيّر عادي.
    private val containerKeywords = listOf(
        "text", "container", "Containers", "Group", "Volume", "Section",
        "Translations", "translation", "link", "tying", "merge",
        "installation", "simplified", "save", "file", "end",
        "row", "style", "document", "route",
        "data", "api", "import", "table", "doc", "portal", "block", "pipe"
    )

    // حقول تنسيق/ستايل خاصة حصراً بـ @container.object/@Object: txt/img/object.file/Fonts/
    // background/css3 ("object" هنا مطلوبة لتلوين بداية عبارة "object.file"؛ الكلمة "file" نفسها
    // مذكورة أعلاه ضمن containerKeywords لأنها محجوزة أصلاً وبشكل عام لعبارة "file path=...;"
    // داخل @container.import). لها فئة لونية مستقلة (انظر syntax_style_keyword).
    private val styleFieldKeywords = listOf(
        "txt", "img", "object", "Fonts", "background", "css3"
    )

    // أسماء دوال المكتبة القياسية (stdlib) والعمليات المدمجة
    private val builtins = listOf(
        "Addition", "Subtraction", "Multiplication", "Equal",
        "abs", "sqrt", "pow", "floor", "ceil", "round", "min", "max", "random",
        "len", "upper", "lower", "trim", "substr", "split", "join",
        "indexOf", "replace", "contains", "charAt", "toString", "toNumber",
        "push", "pop", "sort", "keys", "values", "has", "remove"
    )

    private fun wordsPattern(words: List<String>): Pattern =
        Pattern.compile("\\b(" + words.joinToString("|") { Pattern.quote(it) } + ")\\b")

    private val keywordPattern = wordsPattern(coreKeywords)
    private val containerKeywordPattern = wordsPattern(containerKeywords)
    private val styleFieldPattern = wordsPattern(styleFieldKeywords)
    private val builtinPattern = wordsPattern(builtins)
    private val numberPattern = Pattern.compile("\\b\\d+(\\.\\d+)?\\b")
    // نص محاط بعلامتي تنصيص مع دعم التهريب (\" \\ \n ...) بنفس أسلوب rin_lexer.cpp::scanString،
    // حتى لا ينتهي التلوين عند أول علامة تنصيص مُهرَّبة داخل النص.
    private val stringPattern = Pattern.compile("\"(?:\\\\.|[^\"\\\\])*\"")
    private val commentPattern = Pattern.compile("//[^\\n]*")
    private val tagPattern = Pattern.compile("@[A-Za-z][A-Za-z0-9_.]*|\\.end/[A-Za-z0-9_.]*")

    private val mainHandler = Handler(Looper.getMainLooper())
    private var pendingHighlight: Runnable? = null

    /** يربط التلوين الحي بحقل تحرير الكود. آمن للاستدعاء مرة واحدة فقط لكل [EditText]. */
    fun attach(context: Context, editText: EditText) {
        val colorKeyword = ContextCompat.getColor(context, R.color.syntax_keyword)
        val colorContainerKeyword = ContextCompat.getColor(context, R.color.syntax_container_keyword)
        val colorStyleField = ContextCompat.getColor(context, R.color.syntax_style_keyword)
        val colorString = ContextCompat.getColor(context, R.color.syntax_string)
        val colorNumber = ContextCompat.getColor(context, R.color.syntax_number)
        val colorComment = ContextCompat.getColor(context, R.color.syntax_comment)
        val colorBuiltin = ContextCompat.getColor(context, R.color.syntax_builtin)
        val colorTag = ContextCompat.getColor(context, R.color.syntax_tag)

        fun runHighlight(editable: Editable) {
            highlight(
                editable,
                colorKeyword, colorContainerKeyword, colorStyleField, colorString,
                colorNumber, colorComment, colorBuiltin, colorTag
            )
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
                scheduleHighlight(editable)
            }
        })

        // تلوين أولي فوري (بلا تهدئة) للنص الموجود عند فتح المحرر لأول مرة
        editText.text?.let { runHighlight(it) }
    }

    private fun highlight(
        editable: Editable,
        colorKeyword: Int,
        colorContainerKeyword: Int,
        colorStyleField: Int,
        colorString: Int,
        colorNumber: Int,
        colorComment: Int,
        colorBuiltin: Int,
        colorTag: Int
    ) {
        // إزالة كل الـ spans السابقة التي وضعناها نحن فقط (نتعرّف عليها عبر نوعها الموحّد)
        val existing = editable.getSpans(0, editable.length, ForegroundColorSpan::class.java)
        for (span in existing) editable.removeSpan(span)
        val existingStyles = editable.getSpans(0, editable.length, StyleSpan::class.java)
        for (span in existingStyles) editable.removeSpan(span)

        // ملفات ضخمة جداً: نكتفي بإزالة أي تلوين سابق (أعلاه) ونترك النص بلا تلوين، بدل تجميد
        // واجهة المستخدم بمسح كامل بتعبيرات نمطية على نص طويل جداً.
        if (editable.length > MAX_HIGHLIGHT_LENGTH) return

        val text = editable.toString()

        fun applyAll(pattern: Pattern, color: Int, italic: Boolean = false) {
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
        // وقع بالخطأ داخلها (مثال: كلمة "if" داخل نص "if you can").
        applyAll(keywordPattern, colorKeyword)
        applyAll(containerKeywordPattern, colorContainerKeyword)
        applyAll(styleFieldPattern, colorStyleField)
        applyAll(builtinPattern, colorBuiltin)
        applyAll(numberPattern, colorNumber)
        applyAll(tagPattern, colorTag)
        applyAll(stringPattern, colorString)
        applyAll(commentPattern, colorComment, italic = true)
    }
}

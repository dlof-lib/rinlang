package com.dlof.rinlang

import android.content.Context
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
 */
object RinSyntaxHighlighter {

    // كلمات لغة Rin الأساسية (تحكّم بالتدفّق، تعريف متغيرات ودوال)
    private val coreKeywords = listOf(
        "let", "print", "if", "else", "while", "fun", "return",
        "true", "false", "nil", "and", "or"
    )

    // كلمات لغة الحاويات/البيانات (data container language) - حساسة لحالة الأحرف كما في المحرّك
    private val containerKeywords = listOf(
        "text", "container", "Containers", "Group", "Volume", "Section",
        "Translations", "translation", "link", "tying", "merge",
        "installation", "simplified", "save", "file", "end",
        // حقول الستايل الخاصة بـ @container.object/@Object فقط: txt/img/object.file/Fonts/background/css3
        // ("object" هنا مطلوبة لتلوين بداية عبارة "object.file"؛ "file" مذكورة أعلاه بالفعل)
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
        Pattern.compile("\\b(" + words.joinToString("|") + ")\\b")

    private val keywordPattern = wordsPattern(coreKeywords)
    private val containerKeywordPattern = wordsPattern(containerKeywords)
    private val builtinPattern = wordsPattern(builtins)
    private val numberPattern = Pattern.compile("\\b\\d+(\\.\\d+)?\\b")
    private val stringPattern = Pattern.compile("\"[^\"]*\"")
    private val commentPattern = Pattern.compile("//[^\\n]*")
    private val tagPattern = Pattern.compile("@[A-Za-z][A-Za-z0-9_.]*|\\.end/[A-Za-z0-9_.]*")

    /** يربط التلوين الحي بحقل تحرير الكود. آمن للاستدعاء مرة واحدة فقط لكل [EditText]. */
    fun attach(context: Context, editText: EditText) {
        val colorKeyword = ContextCompat.getColor(context, R.color.syntax_keyword)
        val colorContainerKeyword = ContextCompat.getColor(context, R.color.syntax_container_keyword)
        val colorString = ContextCompat.getColor(context, R.color.syntax_string)
        val colorNumber = ContextCompat.getColor(context, R.color.syntax_number)
        val colorComment = ContextCompat.getColor(context, R.color.syntax_comment)
        val colorBuiltin = ContextCompat.getColor(context, R.color.syntax_builtin)
        val colorTag = ContextCompat.getColor(context, R.color.syntax_tag)

        var isHighlighting = false

        editText.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}

            override fun afterTextChanged(editable: Editable?) {
                if (editable == null || isHighlighting) return
                isHighlighting = true
                try {
                    highlight(
                        editable,
                        colorKeyword, colorContainerKeyword, colorString,
                        colorNumber, colorComment, colorBuiltin, colorTag
                    )
                } finally {
                    isHighlighting = false
                }
            }
        })

        // تلوين أولي للنص الموجود عند فتح المحرر لأول مرة
        editText.text?.let {
            highlight(
                it,
                colorKeyword, colorContainerKeyword, colorString,
                colorNumber, colorComment, colorBuiltin, colorTag
            )
        }
    }

    private fun highlight(
        editable: Editable,
        colorKeyword: Int,
        colorContainerKeyword: Int,
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
        applyAll(builtinPattern, colorBuiltin)
        applyAll(numberPattern, colorNumber)
        applyAll(tagPattern, colorTag)
        applyAll(stringPattern, colorString)
        applyAll(commentPattern, colorComment, italic = true)
    }
}

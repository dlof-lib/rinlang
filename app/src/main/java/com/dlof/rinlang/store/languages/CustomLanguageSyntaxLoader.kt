package com.dlof.rinlang.store.languages

import org.json.JSONObject
import java.io.File
import java.util.regex.Pattern

/** قاعدة تلوين واحدة: نمط Regex ولون Hex يُطبَّق على كل تطابق. */
data class SyntaxRule(
    val name: String,
    val pattern: Pattern,
    val colorHex: String
)

/** وصف تلوين كامل للغة مخصصة، جاهز للتطبيق في المحرر عبر [CustomLanguageSyntaxLoader.highlight]. */
data class LanguageSyntax(
    val languageName: String,
    val fileExtension: String,
    val lineComment: String,
    val keywords: List<String>,
    val rules: List<SyntaxRule>
)

/**
 * يقرأ syntax.rinsyntax.json من مجلد مشروع لغة مخصصة (راجع [CustomLanguageRegistry]) ويبنيه إلى
 * [LanguageSyntax] قابل للاستخدام مباشرة من محرر الكود، دون أي تعديل على منطق تلوين Rin نفسه —
 * هذا نظام تلوين مستقل يُفعَّل فقط عند فتح ملف بامتداد لغة مخصصة مسجَّلة (raiseExtension).
 *
 * تنسيق rules في الملف: [{ "name", "pattern" (regex), "color" (hex) }, ...] تُطبَّق بالترتيب —
 * أول نمط يطابق شريحة نص "يفوز" بلونها (نفس فكرة قواعد Lexer ذات أولوية، بلا تراكب تلوين مزدوج).
 */
object CustomLanguageSyntaxLoader {

    fun load(projectDir: File, manifest: CustomLanguageManifest): LanguageSyntax? {
        val file = File(projectDir, manifest.syntaxFile)
        if (!file.exists()) return null
        return try {
            val json = JSONObject(file.readText(Charsets.UTF_8))
            val keywords = json.optJSONArray("keywords")?.let { arr ->
                (0 until arr.length()).map { arr.getString(it) }
            } ?: emptyList()
            val rulesArr = json.optJSONArray("rules")
            val rules = mutableListOf<SyntaxRule>()
            if (rulesArr != null) {
                for (i in 0 until rulesArr.length()) {
                    val r = rulesArr.getJSONObject(i)
                    val patternText = r.optString("pattern")
                    if (patternText.isEmpty()) continue
                    try {
                        rules.add(
                            SyntaxRule(
                                name = r.optString("name", "rule$i"),
                                pattern = Pattern.compile(patternText),
                                colorHex = r.optString("color", "#D4D4D4")
                            )
                        )
                    } catch (t: Throwable) {
                        // نمط regex غير صالح داخل syntax.rinsyntax.json: يُتخطّى بصمت بدل تعطيل بقية التلوين
                    }
                }
            }
            LanguageSyntax(
                languageName = json.optString("language", manifest.name),
                fileExtension = json.optString("fileExtension", manifest.fileExtension),
                lineComment = json.optString("lineComment", "//"),
                keywords = keywords,
                rules = rules
            )
        } catch (t: Throwable) {
            null
        }
    }

    /** نتيجة تلوين شريحة واحدة من النص: [start, end) بالفهرسة الحرفية، ولون Hex. */
    data class HighlightSpan(val start: Int, val end: Int, val colorHex: String)

    /**
     * يطبّق قواعد [LanguageSyntax.rules] بالترتيب على نص كامل، ويُعيد قائمة أجزاء ملوَّنة بلا
     * تراكب (أول قاعدة تطابق موضعاً تفوز به). محرر الكود يحوّل هذه القائمة إلى SpannableString
     * بربط كل [HighlightSpan] بـForegroundColorSpan مطابق للون Hex.
     */
    fun highlight(source: String, syntax: LanguageSyntax): List<HighlightSpan> {
        val spans = mutableListOf<HighlightSpan>()
        val claimed = BooleanArray(source.length)
        for (rule in syntax.rules) {
            val matcher = rule.pattern.matcher(source)
            while (matcher.find()) {
                val start = matcher.start()
                val end = matcher.end()
                if (start == end) continue
                var overlaps = false
                for (i in start until end) {
                    if (claimed[i]) { overlaps = true; break }
                }
                if (overlaps) continue
                for (i in start until end) claimed[i] = true
                spans.add(HighlightSpan(start, end, rule.colorHex))
            }
        }
        return spans.sortedBy { it.start }
    }
}

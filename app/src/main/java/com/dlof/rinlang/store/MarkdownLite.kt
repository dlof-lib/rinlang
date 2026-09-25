package com.dlof.rinlang.store

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.net.Uri
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.TextPaint
import android.text.method.LinkMovementMethod
import android.text.style.BackgroundColorSpan
import android.text.style.BulletSpan
import android.text.style.ClickableSpan
import android.text.style.ForegroundColorSpan
import android.text.style.LeadingMarginSpan
import android.text.style.LineBackgroundSpan
import android.text.style.RelativeSizeSpan
import android.text.style.ReplacementSpan
import android.text.style.StrikethroughSpan
import android.text.style.StyleSpan
import android.text.style.TypefaceSpan
import android.text.style.URLSpan
import android.view.View
import android.widget.TextView
import android.widget.Toast

/**
 * محوّل Markdown → معاينة حقيقية داخل TextView واحد، عبر بناء [SpannableStringBuilder] مباشرة
 * بدل تمرير وسوم HTML عبر android.text.Html.fromHtml (دعمها محدود وغير متّسق لكثير من هذه
 * العناصر داخل TextView عادي). بلا أي مكتبة Markdown خارجية — كل عنصر أدناه "مكتبة مصغّرة" ذاتية
 * الاكتفاء داخل هذا الملف الواحد، مبنيّة فوق أدوات Spannable القياسية + Span مخصّصة عند الحاجة.
 *
 * **المدعوم حالياً:**
 * - عناوين متدرّجة الحجم كاملة من `#` إلى `######` (العنوانان الخامس والسادس بلون مكتوم مميَّز).
 * - نصوص خطوط: **تشديد**، *مائل*، ***تشديد+مائل***، `__تشديد__`/`_مائل_` (الصيغة البديلة)،
 *   ~~يتوسّطه خط~~، ==تمييز بخلفية==، و`كود مضمَّن`.
 * - روابط `[نص](رابط)` قابلة للنقر فعلياً (لون مميَّز + خط تحته) عبر [applyTo].
 * - إفلات أحرف Markdown بمعكوس مائل: `\*` `\_` `\`` ... تُعرَض حرفياً بلا تنسيق.
 * - كتل كود ```لغة بخط أحادي المسافة، خلفية مميّزة تمتد بعرض السطر، ووسم صغير لاسم اللغة إن
 *   ذُكرت (بنفس أسلوب "ستيكر Rin" الجديد أدناه).
 * - اقتباسات `>` بشريط جانبي ملوَّن حقيقي يُرسم عبر [QuoteBarSpan]، لا مجرّد مسافة بادئة.
 * - قوائم نقطية حقيقية (`-`/`*`/`+`) بنقطة ملوَّنة فعلية، تدعم التعشيش بعمق حتى 3 مستويات
 *   (لون مختلف لكل عمق يدور بين هوية بنفسجي/أخضر/ذهبي الخاصة بالتطبيق).
 * - قوائم مرقَّمة `1.` بأرقام حقيقية ومحاذاة معلَّقة صحيحة للأسطر الملتفّة.
 * - قوائم مهام `- [ ]` / `- [x]` بمربّع اختيار حقيقي، وشطب تلقائي للعناصر المُنجَزة.
 * - خط أفقي فاصل (`---`/`***`/`___`) يُرسم كخط حقيقي عبر عرض السطر لا كسلسلة شرطات نصّية.
 * - جداول Markdown كاملة (`| ... | ... |` + سطر فاصل `---`) تُرسَم كصندوق حقيقي بخطوط اتصال.
 * - **جديد: "ستيكر Rin"** — بادج/شارة ملوَّنة حقيقية الشكل (خلفية بزوايا مدوَّرة) عبر صياغة
 *   `[[نص]]` أو `[[نص|لون]]`، بالألوان: accent (افتراضي)، green، gold، danger، info، like —
 *   نفس هوية التطبيق البصرية (بنفسجي+أخضر) المستخدمة في شارات "موثّق" وبطاقات الإحصاءات.
 * - **جديد: مظهر احترافي بأسلوب README على GitHub** — العنوان الأول `#` يظهر كـ"عنوان بطولي"
 *   أكبر حجماً مع خط تحته بلون هوية التطبيق (accent)، والعنوان الثاني `##` يحصل على خط أنحف
 *   بلون محايد، لفصل بصري واضح بين الأقسام تماماً كمعاينات README الاحترافية.
 * - **جديد: بطاقات بزوايا مدوَّرة حقيقية** — كتل الكود، الاقتباسات، والجداول لم تعد تُرسم كمستطيل
 *   خام بحواف حادة، بل كبطاقة مدوَّرة الزوايا فعلياً عبر [RoundedCardSpan] (يُرسم بـ[Path] لا
 *   [RectF] مباشرة)، بحدّ خفيف حول البطاقة، فتبدو أقرب لمكوّن Material Design من نص خام.
 * - **جديد: اقتباسات بعلامة تنصيص** — كل كتلة `>` تبدأ الآن بعلامة اقتباس مزخرفة كبيرة بلون
 *   الشريط الجانبي، لتمييزها بصرياً كـ"مقتطف مُقتبَس" لا مجرّد سطر مائل.
 * - **جديد: كتل كود Rin/indsin حيّة** — [splitLiveCodeBlocks] يفصل ```rin ```/```indsin ``` عن
 *   بقية النص Markdown العادي، ليعرضها المستدعي (PackageDetailActivity) ببطاقة معاينة حيّة
 *   حقيقية (وجهة مُصيَّرة، أو نتيجة تنفيذ فعلية لغير ذلك) بدل نص كود ثابت.
 * - **جديد: تلوين نحوي حقيقي (Syntax Highlighting)** — كل كتلة كود ```لغة ``` تُحلَّل الآن عبر
 *   [appendHighlightedCode] إلى رموز مصنَّفة (تعليقات/نصوص حرفية/أرقام/كلمات مفتاحية/استدعاءات
 *   دوال/أسماء مُسنَدة/توجيهات @/وسوم .end) بنفس لوحة ألوان محرِّر Rin الحقيقي (Night theme)،
 *   بدل كتلة رمادية موحَّدة اللون — لتقارب تجربة قراءة كتل الكود من محرِّرات الأكواد الاحترافية.
 * - **جديد: رأس بطاقة كود احترافي** — كل كتلة كود تُعرَض الآن داخل بطاقة موحَّدة برأس علوي:
 *   نقطة ملوَّنة بهوية اللغة + اسمها، ثم زر "نسخ" حقيقي قابل للنقر (عبر [CopyCodeSpan]) ينسخ
 *   الكود الخام كاملاً للحافظة (Clipboard) مع تنبيه تأكيد قصير، وخط فاصل رفيع أسفل الرأس يفصله
 *   بصرياً عن جسم الكود — تماماً كرأس نافذة كود في IDE احترافي، لا مجرد وسم عائم أعلى الكتلة.
 * - **جديد: شكل روابط محسَّن** — روابط `[نص](رابط)` بلا خط تحتها بعد الآن، بتشديد كامل وسهم
 *   رابط خارجي صغير ↗ بعد النص، أقرب لأسلوب شارات الروابط الاحترافية.
 * - **جديد: شارات/أزرار وصفية عبر `[* ... *]`** — صياغة عامة جديدة بأجزاء مفصولة بـ`/` (انظر
 *   [appendMetaBadge]): `[*الإصدار/1*]` لشارة إصدار ثنائية اللون (تسمية خافتة + قيمة بارزة)،
 *   `[*زر تثبيت/link=(رابط)*]` لزر رابط حقيقي قابل للنقر يفتح المتصفح عند الضغط، و
 *   `[*Pin/link=(رابط)/icon=(اسم)*]` لنفس الزر مع أيقونة مصغَّرة قبل النص.
 *
 * الاستخدام المباشر: `textView.text = MarkdownLite.toSpannable(md)`.
 * الاستخدام الموصى به عند وجود روابط قابلة للنقر: `MarkdownLite.applyTo(textView, md)`.
 */
object MarkdownLite {

    // ألوان مأخوذة من نفس لوحة التطبيق (colors.xml) لتبدو المعاينة جزءاً من التطبيق لا دخيلة عليه
    private const val COLOR_BULLET = 0xFF7C5CFF.toInt()          // rin_accent
    private const val COLOR_BULLET_L2 = 0xFF22C88E.toInt()       // rin_accent_green
    private const val COLOR_BULLET_L3 = 0xFFFFC94D.toInt()       // rin_star_gold
    private val BULLET_DEPTH_COLORS = intArrayOf(COLOR_BULLET, COLOR_BULLET_L2, COLOR_BULLET_L3)

    private const val COLOR_RULE = 0xFF2D2D30.toInt()            // rin_job_card_border
    private const val COLOR_CARD_BORDER = 0x26FFFFFF             // حدّ خفيف موحّد لبطاقات الكود/الاقتباس/الجدول
    private const val COLOR_CODE_TEXT = 0xFFE3E5E8.toInt()       // rin_editor_text
    private const val COLOR_CODE_BLOCK_BG = 0x26FFFFFF           // خلفية محايدة خفيفة لكتلة الكود
    private const val COLOR_INLINE_CODE_BG = 0x33FFFFFF          // أغمق قليلاً لتمييز الكود المضمَّن عن السطر

    private const val COLOR_QUOTE_BAR = 0xFF7C5CFF.toInt()       // rin_accent
    private const val COLOR_QUOTE_BG = 0x1FFFFFFF                // rin_current_line_bg
    private const val COLOR_QUOTE_TEXT = 0xFF9198A3.toInt()      // rin_on_toolbar_dim

    private const val COLOR_LINK = 0xFF3B9EFF.toInt()            // rin_verified_badge
    private const val COLOR_HIGHLIGHT_BG = 0x4DFFC94D             // rin_star_gold_dim
    private const val COLOR_STRIKE_TEXT = 0xFF6E7480.toInt()     // rin_editor_hint

    private const val COLOR_TASK_DONE = 0xFF22C88E.toInt()       // rin_accent_green
    private const val COLOR_TASK_PENDING = 0xFF6E7480.toInt()    // rin_editor_hint
    private const val COLOR_H_DIM = 0xFF9198A3.toInt()           // rin_on_toolbar_dim

    private const val COLOR_TABLE_TEXT = 0xFFE3E5E8.toInt()      // rin_editor_text
    private const val COLOR_TABLE_HEADER = 0xFF7C5CFF.toInt()    // rin_accent
    private const val COLOR_TABLE_BORDER = 0xFF6E7480.toInt()    // rin_editor_hint
    private const val COLOR_TABLE_BG = 0x14FFFFFF                // أخفّ من خلفية كتلة الكود

    // لوحة تلوين نحوي (Syntax Palette) — نفس ألوان محرِّر Rin الحقيقي بالضبط (values-night/colors.xml)
    // حتى تبدو معاينة README جزءاً من هوية المحرِّر البصرية نفسها، لا لوحة مستقلة مختلَقة هنا.
    private const val COLOR_SYNTAX_KEYWORD = 0xFF569CD6.toInt()    // syntax_keyword
    private const val COLOR_SYNTAX_DIRECTIVE = 0xFFC586C0.toInt()  // syntax_container_keyword / syntax_make_directive
    private const val COLOR_SYNTAX_STRING = 0xFF6FDC9E.toInt()     // syntax_string
    private const val COLOR_SYNTAX_NUMBER = 0xFFFFA95C.toInt()     // syntax_number
    private const val COLOR_SYNTAX_COMMENT = 0xFF9AA0AB.toInt()    // syntax_comment
    private const val COLOR_SYNTAX_BUILTIN = 0xFFE6C260.toInt()    // syntax_builtin / syntax_tag
    private const val COLOR_SYNTAX_ATTR = 0xFFF2A65A.toInt()       // syntax_style_keyword

    // خط الفصل الرفيع أسفل رأس بطاقة الكود
    private const val COLOR_COPY_BUTTON_BG = 0x33FFFFFF
    private const val COLOR_HEADER_RULE = 0x1FFFFFFF

    /** لون هوية بصرية مميَّز لكل لغة (نقطة + وسم اسمها أعلى بطاقة الكود) — يسقط بهدوء إلى لون
     *  محايد للغات غير المدرَجة، فلا يتعطّل عرض أي كتلة كود بسبب اسم لغة غير معروف. */
    private val LANGUAGE_ACCENTS: Map<String, Int> = mapOf(
        "rin" to 0xFF7C5CFF.toInt(),
        "indsin" to 0xFF7C5CFF.toInt(),
        "kotlin" to 0xFFB197FC.toInt(),
        "kt" to 0xFFB197FC.toInt(),
        "java" to 0xFFEA9B4C.toInt(),
        "swift" to 0xFFF2784B.toInt(),
        "python" to 0xFFE6C260.toInt(),
        "py" to 0xFFE6C260.toInt(),
        "javascript" to 0xFFE9D85C.toInt(),
        "js" to 0xFFE9D85C.toInt(),
        "typescript" to 0xFF5C9DE9.toInt(),
        "ts" to 0xFF5C9DE9.toInt(),
        "json" to 0xFF9AA0AB.toInt(),
        "xml" to 0xFFE6C260.toInt(),
        "html" to 0xFFF2784B.toInt(),
        "css" to 0xFF5C9DE9.toInt(),
        "bash" to 0xFF6FDC9E.toInt(),
        "sh" to 0xFF6FDC9E.toInt(),
        "shell" to 0xFF6FDC9E.toInt(),
        "c" to 0xFF5C9DE9.toInt(),
        "cpp" to 0xFF5C9DE9.toInt(),
        "c++" to 0xFF5C9DE9.toInt(),
        "go" to 0xFF5CD6E9.toInt(),
        "rust" to 0xFFEA9B4C.toInt(),
        "sql" to 0xFF6FDC9E.toInt(),
        "yaml" to 0xFFEA9B4C.toInt(),
        "yml" to 0xFFEA9B4C.toInt()
    )
    private val LANGUAGE_ACCENT_DEFAULT = COLOR_CODE_TEXT

    /** مجموعة موحَّدة من الكلمات المفتاحية: كلمات لغة Rin الحقيقية (من rin_lexer.cpp) + مجموعة
     *  عامة شائعة عبر أكثر اللغات ذكراً في ملفات README (Kotlin/Python/JS/TS/C/C++/Java/Swift/Go/
     *  Rust/Bash) — قائمة واحدة كافية عملياً بدل تفريع منطق كامل لكل لغة على حدة. */
    private val KEYWORDS: Set<String> = setOf(
        // Rin (rin_lexer.cpp)
        "and", "or", "break", "container", "continue", "data", "else", "end", "false", "file",
        "for", "fun", "if", "import", "installation", "let", "link", "merge", "nil", "pipe",
        "print", "return", "rinopen", "route", "save", "show", "simplified", "text", "translation",
        "true", "tying", "while",
        // شائعة عبر لغات أخرى
        "def", "class", "struct", "enum", "interface", "trait", "impl", "extends", "implements",
        "package", "namespace", "using", "include", "module", "export", "async", "await", "yield",
        "in", "is", "as", "self", "this", "super", "new", "try", "catch", "finally", "throw",
        "throws", "switch", "case", "default", "public", "private", "protected", "static", "final",
        "const", "var", "val", "function", "fn", "void", "int", "float", "double", "bool",
        "boolean", "string", "char", "null", "none", "None", "lambda", "with", "from", "elif",
        "pass", "raise", "except", "global", "typeof", "instanceof", "override", "abstract",
        "sealed", "companion", "object", "when", "do", "unsigned", "signed", "typedef", "template"
    )

    /** رموز محاطة بالخوارزمية أدناه بترتيب أولوية: تعليق كتلة، تعليق سطر، نص حرفي، توجيه @،
     *  وسم إغلاق .end/، رقم، كلمة مفتاحية، استدعاء دالة، اسم مُسنَد قبل =. */
    private val codeTokenRegex = Regex(
        "(/\\*[\\s\\S]*?\\*/)" +
            "|(//[^\n]*|#[^\n]*)" +
            "|(\"(?:\\\\.|[^\"\\\\\n])*\"|'(?:\\\\.|[^'\\\\\n])*'|`(?:\\\\.|[^`\\\\])*`)" +
            "|(@[A-Za-z_][A-Za-z0-9_.]*)" +
            "|(\\.end/[A-Za-z_]+)" +
            "|\\b(\\d+(?:\\.\\d+)?)\\b" +
            "|\\b(" + KEYWORDS.joinToString("|") + ")\\b" +
            "|\\b([A-Za-z_][A-Za-z0-9_]*)(?=\\()" +
            "|\\b([A-Za-z_][A-Za-z0-9_]*)\\b(?=\\s*=(?!=))"
    )

    /** ألوان "ستيكر Rin": كل صيغة اسم → (خلفية، نص). الافتراضي "accent" عند عدم ذكر أي اسم. */
    private val STICKER_VARIANTS: Map<String, Pair<Int, Int>> = mapOf(
        "accent" to (0xFF7C5CFF.toInt() to 0xFFFFFFFF.toInt()),   // rin_accent
        "green" to (0xFF22C88E.toInt() to 0xFF0C231A.toInt()),    // rin_accent_green
        "gold" to (0xFFFFC94D.toInt() to 0xFF2B1F02.toInt()),     // rin_star_gold
        "danger" to (0xFFF14C4C.toInt() to 0xFFFFFFFF.toInt()),   // status_error
        "info" to (0xFF3B9EFF.toInt() to 0xFFFFFFFF.toInt()),     // rin_verified_badge
        "like" to (0xFFFF4D6D.toInt() to 0xFFFFFFFF.toInt())      // rin_like_active
    )
    private val STICKER_DEFAULT = STICKER_VARIANTS.getValue("accent")

    // مجموعة تعابير نمطية للأنماط السطرية بترتيب أولوية يحلّ التعارض بين ** و * و __ و _ وغيرها.
    // ملاحظة الفهارس (مطابقة لترتيب المجموعات أدناه):
    //  1) escape حرف مُفلَت حرفياً        8) [[ستيكر]] أو [[ستيكر|لون]]
    //  2) ***تشديد+مائل***                9) [*شارة/قيمة*] أو [*نص/link=(رابط)*] أو
    //  3) **تشديد**                          [*نص/link=(رابط)/icon=(اسم)*] — انظر [appendMetaBadge]
    //  4) __تشديد بديل__                  10/11) [نص](رابط)
    //  5) ~~يتوسّطه خط~~                  12) *مائل*
    //  6) `كود مضمَّن`                    13) _مائل بديل_
    //  7) ==تمييز==
    private val inlineRegex = Regex(
        "\\\\([\\\\`*_{}\\[\\]()#+.!~=>-])" +
            "|\\*\\*\\*([^*]+?)\\*\\*\\*" +
            "|\\*\\*([^*]+?)\\*\\*" +
            "|__([^_]+?)__" +
            "|~~([^~]+?)~~" +
            "|`([^`]+?)`" +
            "|==([^=]+?)==" +
            "|\\[\\[([^\\]]+?)\\]\\]" +
            "|\\[\\*([^\\]]+?)\\*\\]" +
            "|\\[([^\\]]+?)\\]\\(([^)\\s]+?)\\)" +
            "|\\*([^*]+?)\\*" +
            "|_([^_]+?)_"
    )

    private val orderedListRegex = Regex("^(\\d{1,4})[.)]\\s+(.*)$")
    private val taskListRegex = Regex("^[-*+]\\s+\\[([ xX])]\\s+(.*)$")
    private val bulletListRegex = Regex("^[-*+]\\s+(.*)$")
    private val tableSeparatorRegex = Regex("^:?-{2,}:?$")

    /** يبني معاينة Markdown حقيقية جاهزة لعرضها مباشرة عبر `textView.text = MarkdownLite.toSpannable(md)`. */
    fun toSpannable(markdown: String): CharSequence {
        val out = SpannableStringBuilder()
        val lines = markdown.lines()
        var i = 0

        var inCodeBlock = false
        var codeLang = ""
        val codeBuffer = StringBuilder()
        var lastWasListItem = false

        fun blockGap() {
            if (out.isNotEmpty()) out.append("\n\n")
        }

        fun flushCodeBlock() {
            if (codeBuffer.isEmpty() && codeLang.isBlank()) return
            blockGap()
            val content = codeBuffer.toString().trimEnd('\n')

            // بداية البطاقة الكاملة (رأس + خط فاصل + جسم الكود) — نطاق واحد متّصل حتى تُرسَم
            // الزوايا المدوَّرة والحدّ الخفيف حول الكل معاً، لا حول جسم الكود وحده.
            val cardStart = out.length
            appendCodeHeader(out, codeLang, content)
            out.append('\n')
            val ruleStart = out.length
            out.append('\u00A0')
            out.setSpan(RuleSpan(COLOR_HEADER_RULE, 1.5f), ruleStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.append('\n')

            val start = out.length
            appendHighlightedCode(out, content)
            val end = out.length
            out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(LeadingMarginSpan.Standard(18), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            out.setSpan(
                RoundedCardSpan(COLOR_CODE_BLOCK_BG, COLOR_CARD_BORDER, cardStart, end),
                cardStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
            )
            codeBuffer.clear()
            codeLang = ""
            lastWasListItem = false
        }

        while (i < lines.size) {
            val rawLine = lines[i]

            if (rawLine.trim().startsWith("```")) {
                if (inCodeBlock) {
                    flushCodeBlock(); inCodeBlock = false
                } else {
                    inCodeBlock = true
                    codeLang = rawLine.trim().removePrefix("```").trim()
                }
                i++; continue
            }
            if (inCodeBlock) { codeBuffer.append(rawLine).append('\n'); i++; continue }

            val trimmed = rawLine.trim()
            val indentSpaces = rawLine.length - rawLine.trimStart(' ').length
            val depth = (indentSpaces / 2).coerceIn(0, 2)

            when {
                trimmed.isEmpty() -> { lastWasListItem = false; i++ }

                trimmed == "---" || trimmed == "***" || trimmed == "___" -> {
                    blockGap()
                    val start = out.length
                    out.append("\u00A0")
                    out.setSpan(RuleSpan(), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    lastWasListItem = false
                    i++
                }

                trimmed.startsWith("> ") || trimmed == ">" -> {
                    val quoteLines = mutableListOf<String>()
                    var j = i
                    while (j < lines.size) {
                        val t = lines[j].trim()
                        if (t.startsWith("> ") || t == ">") {
                            quoteLines.add(t.removePrefix(">").trimStart(' '))
                            j++
                        } else break
                    }
                    blockGap()
                    val start = out.length
                    val glyphStart = out.length
                    out.append("\u201C ") // علامة تنصيص مزخرفة لتمييز المقتطف المُقتبَس بصرياً
                    val glyphEnd = out.length
                    out.setSpan(RelativeSizeSpan(1.3f), glyphStart, glyphEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(StyleSpan(Typeface.BOLD), glyphStart, glyphEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(ForegroundColorSpan(COLOR_QUOTE_BAR), glyphStart, glyphEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    val textStart = out.length
                    quoteLines.forEachIndexed { idx, ql ->
                        if (idx > 0) out.append("\n")
                        appendInline(out, ql)
                    }
                    val end = out.length
                    // نطاقات منفصلة غير متداخلة (بدل نطاق واحد شامل) حتى لا يطغى لون النص العام
                    // على لون علامة التنصيص المميّزة أعلاه عند تطبيق الـSpans بالترتيب.
                    out.setSpan(QuoteBarSpan(COLOR_QUOTE_BAR), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(
                        RoundedCardSpan(COLOR_QUOTE_BG, COLOR_CARD_BORDER, start, end),
                        start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    out.setSpan(ForegroundColorSpan(COLOR_QUOTE_TEXT), textStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(StyleSpan(Typeface.ITALIC), textStart, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    lastWasListItem = false
                    i = j
                }

                trimmed.contains("|") && i + 1 < lines.size && isTableSeparator(lines[i + 1]) -> {
                    val headerCells = splitTableRow(trimmed)
                    val bodyRows = mutableListOf<List<String>>()
                    var j = i + 2
                    while (j < lines.size && lines[j].trim().contains("|") && lines[j].isNotBlank()) {
                        bodyRows.add(splitTableRow(lines[j].trim()))
                        j++
                    }
                    blockGap()
                    appendTable(out, headerCells, bodyRows)
                    lastWasListItem = false
                    i = j
                }

                trimmed.startsWith("###### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("###### "), 0.85f, level = 6, dim = true); lastWasListItem = false; i++
                }
                trimmed.startsWith("##### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("##### "), 0.95f, level = 5, dim = true); lastWasListItem = false; i++
                }
                trimmed.startsWith("#### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("#### "), 1.05f, level = 4); lastWasListItem = false; i++
                }
                trimmed.startsWith("### ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("### "), 1.15f, level = 3); lastWasListItem = false; i++
                }
                trimmed.startsWith("## ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("## "), 1.28f, level = 2); lastWasListItem = false; i++
                }
                trimmed.startsWith("# ") -> {
                    blockGap(); appendHeading(out, trimmed.removePrefix("# "), 1.6f, level = 1); lastWasListItem = false; i++
                }

                taskListRegex.matches(trimmed) -> {
                    val m = taskListRegex.find(trimmed)!!
                    val checked = m.groupValues[1].equals("x", ignoreCase = true)
                    val content = m.groupValues[2]
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val lineStart = out.length
                    val glyphStart = out.length
                    out.append(if (checked) "\u2611 " else "\u2610 ")
                    out.setSpan(
                        ForegroundColorSpan(if (checked) COLOR_TASK_DONE else COLOR_TASK_PENDING),
                        glyphStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    val textStart = out.length
                    appendInline(out, content)
                    if (checked) {
                        out.setSpan(StrikethroughSpan(), textStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                        out.setSpan(ForegroundColorSpan(COLOR_TASK_PENDING), textStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    }
                    out.setSpan(
                        LeadingMarginSpan.Standard(depth * 24, depth * 24 + 30),
                        lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    lastWasListItem = true
                    i++
                }

                orderedListRegex.matches(trimmed) -> {
                    val m = orderedListRegex.find(trimmed)!!
                    val number = m.groupValues[1]
                    val content = m.groupValues[2]
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val lineStart = out.length
                    val numStart = out.length
                    out.append("$number. ")
                    out.setSpan(StyleSpan(Typeface.BOLD), numStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                    out.setSpan(
                        ForegroundColorSpan(BULLET_DEPTH_COLORS[depth % BULLET_DEPTH_COLORS.size]),
                        numStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    appendInline(out, content)
                    out.setSpan(
                        LeadingMarginSpan.Standard(depth * 24, depth * 24 + 34),
                        lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    lastWasListItem = true
                    i++
                }

                bulletListRegex.matches(trimmed) -> {
                    val m = bulletListRegex.find(trimmed)!!
                    val content = m.groupValues[1]
                    if (lastWasListItem) out.append("\n") else blockGap()
                    val lineStart = out.length
                    appendInline(out, content)
                    out.setSpan(
                        BulletSpan(22, BULLET_DEPTH_COLORS[depth % BULLET_DEPTH_COLORS.size]),
                        lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    if (depth > 0) {
                        out.setSpan(
                            LeadingMarginSpan.Standard(depth * 24, depth * 24),
                            lineStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                        )
                    }
                    lastWasListItem = true
                    i++
                }

                else -> {
                    blockGap()
                    appendInline(out, trimmed)
                    lastWasListItem = false
                    i++
                }
            }
        }
        if (inCodeBlock) flushCodeBlock()
        return out
    }

    /**
     * يبني نص Markdown مباشرة داخل [textView]، ويُفعِّل [LinkMovementMethod] ولون الروابط حتى
     * تعمل روابط `[نص](رابط)` فعلياً بالنقر — الاستخدام المُوصى به بدل تعيين `.text` يدوياً.
     */
    /**
     * مقطع واحد من مقاطع [splitLiveCodeBlocks]: إمّا نص Markdown عادي يُعرَض عبر [toSpannable]
     * كالمعتاد، أو كتلة كود Rin/indsin مستخرَجة لتُعرَض ببطاقة "معاينة حية" منفصلة (كودها +
     * محاولة تشغيلها فعلياً عبر المحرّك الأصلي) بدل نص كود ثابت داخل نفس مسار العرض العادي.
     */
    sealed class MarkdownSegment {
        data class Text(val markdown: String) : MarkdownSegment()
        data class LiveCode(val language: String, val code: String) : MarkdownSegment()
    }

    private val liveCodeBlockRegex = Regex(
        "```(rin|indsin)[ \\t]*\\r?\\n([\\s\\S]*?)```",
        RegexOption.IGNORE_CASE
    )

    /**
     * يقسّم [markdown] إلى مقاطع نصية عادية ومقاطع كود Rin/indsin حيّة منفصلة (```rin ... ```
     * أو ```indsin ... ```)، حتى يمكن لـ[com.dlof.rinlang.store.PackageDetailActivity.bindReadmeAndLicense]
     * عرض معاينة حيّة حقيقية أسفل كل كتلة كود Rin (وجهة/صفحة مُصيَّرة فعلياً عبر المحرّك الأصلي إن
     * وُجد `@view` قابل للعرض، وإلا نتيجة تنفيذ فعلية — يناسب هذا أي نوع كود آخر: حاوية `@container`
     * بلا واجهة، جدولة، منطق عادي...) بدل أن تبقى مجرد نص كود ثابت كباقي لغات الكود الأخرى (التي
     * تُعرَض كالمعتاد بلا أي تغيير ضمن مقاطع [MarkdownSegment.Text] العادية عبر [toSpannable]).
     * كتل الكود بأي لغة أخرى غير rin/indsin لا تُقتطَع هنا إطلاقاً وتبقى ضمن نص Markdown العادي.
     */
    fun splitLiveCodeBlocks(markdown: String): List<MarkdownSegment> {
        val segments = mutableListOf<MarkdownSegment>()
        var lastEnd = 0
        for (match in liveCodeBlockRegex.findAll(markdown)) {
            if (match.range.first > lastEnd) {
                segments.add(MarkdownSegment.Text(markdown.substring(lastEnd, match.range.first)))
            }
            val lang = match.groupValues[1].lowercase()
            val code = match.groupValues[2].trimEnd('\n')
            segments.add(MarkdownSegment.LiveCode(lang, code))
            lastEnd = match.range.last + 1
        }
        if (lastEnd < markdown.length) {
            segments.add(MarkdownSegment.Text(markdown.substring(lastEnd)))
        }
        return segments
    }

    fun applyTo(textView: TextView, markdown: String) {
        textView.text = toSpannable(markdown)
        textView.movementMethod = LinkMovementMethod.getInstance()
        textView.setLinkTextColor(COLOR_LINK)
        textView.highlightColor = COLOR_HIGHLIGHT_BG
    }

    /**
     * يضيف نص عنوان بحجم [scale] النسبي وتشديد كامل، مع تطبيق تشديد أو كود داخلي إن وُجد.
     * العنوانان الأول والثاني ([level] 1 أو 2) يحصلان إضافياً على خط فاصل تحتهما — تماماً كأسلوب
     * عرض README الاحترافي على GitHub — لفصل الأقسام بصرياً بدل الاعتماد على المسافة فقط.
     */
    private fun appendHeading(out: SpannableStringBuilder, text: String, scale: Float, level: Int, dim: Boolean = false) {
        val start = out.length
        appendInline(out, text)
        val end = out.length
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(scale), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        if (dim) out.setSpan(ForegroundColorSpan(COLOR_H_DIM), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        if (level == 1 || level == 2) {
            out.append("\n")
            val ruleStart = out.length
            out.append("\u00A0")
            val ruleEnd = out.length
            val ruleColor = if (level == 1) COLOR_BULLET else COLOR_RULE
            val thickness = if (level == 1) 3f else 1.5f
            out.setSpan(RuleSpan(ruleColor, thickness), ruleStart, ruleEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    /**
     * يطبّق كل الأنماط السطرية المدعومة ضمن سطر واحد (تشديد/مائل/شطب/كود/تمييز/روابط/ستيكر Rin)
     * ويُلحق الباقي كنص عادي. يدعم تعشيشاً بمستوى واحد (مثال: `**نص _مائل_ داخل تشديد**`) عبر
     * إعادة الاستدعاء الذاتي لكل نمط باستثناء الكود والستيكر، اللذين يبقى محتواهما حرفياً دوماً.
     */
    private fun appendInline(out: SpannableStringBuilder, text: String) {
        var idx = 0
        for (match in inlineRegex.findAll(text)) {
            if (match.range.first > idx) out.append(text.substring(idx, match.range.first))
            val g = match.groups
            when {
                g[1] != null -> out.append(g[1]!!.value) // \x حرف مُفلَت → حرفي بلا تنسيق
                g[2] != null -> appendStyled(out, g[2]!!.value, Typeface.BOLD_ITALIC)
                g[3] != null -> appendStyled(out, g[3]!!.value, Typeface.BOLD)
                g[4] != null -> appendStyled(out, g[4]!!.value, Typeface.BOLD)
                g[5] != null -> appendStrike(out, g[5]!!.value)
                g[6] != null -> appendCode(out, g[6]!!.value)
                g[7] != null -> appendHighlight(out, g[7]!!.value)
                g[8] != null -> appendSticker(out, g[8]!!.value)
                g[9] != null -> appendMetaBadge(out, g[9]!!.value)
                g[10] != null && g[11] != null -> appendLink(out, g[10]!!.value, g[11]!!.value)
                g[12] != null -> appendStyled(out, g[12]!!.value, Typeface.ITALIC)
                g[13] != null -> appendStyled(out, g[13]!!.value, Typeface.ITALIC)
            }
            idx = match.range.last + 1
        }
        if (idx < text.length) out.append(text.substring(idx))
    }

    private fun appendStyled(out: SpannableStringBuilder, value: String, style: Int) {
        val start = out.length
        appendInline(out, value)
        out.setSpan(StyleSpan(style), start, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun appendStrike(out: SpannableStringBuilder, value: String) {
        val start = out.length
        appendInline(out, value)
        val end = out.length
        out.setSpan(StrikethroughSpan(), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_STRIKE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun appendHighlight(out: SpannableStringBuilder, value: String) {
        val start = out.length
        appendInline(out, value)
        val end = out.length
        out.setSpan(BackgroundColorSpan(COLOR_HIGHLIGHT_BG), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    private fun appendCode(out: SpannableStringBuilder, value: String) {
        val start = out.length
        out.append(value) // محتوى الكود يبقى حرفياً دوماً، بلا تحليل تعشيش
        val end = out.length
        out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.92f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(BackgroundColorSpan(COLOR_INLINE_CODE_BG), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * رابط `[نص](رابط)` — بشكل احترافي جديد: بلا خط تحته (كان UnderlineSpan)، تشديد كامل، وسهم
     * رابط خارجي صغير ↗ بعد النص، بنفس أسلوب شارات الروابط في READMEs الاحترافية (GitHub/npm).
     * يبقى قابلاً للنقر فعلياً عبر [URLSpan] القياسي (يفتح المتصفح تلقائياً) — لا تغيير سلوكي.
     */
    private fun appendLink(out: SpannableStringBuilder, label: String, url: String) {
        val start = out.length
        appendInline(out, label)
        out.append(" \u2197")
        val end = out.length
        out.setSpan(URLSpan(url), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_LINK), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * "ستيكر Rin": بادج/شارة ملوَّنة حقيقية الشكل عبر `[[نص]]` أو `[[نص|لون]]`. الألوان المتاحة:
     * accent (افتراضي)، green، gold، danger، info، like — راجع [STICKER_VARIANTS]. اسم لون غير
     * معروف يسقط بهدوء إلى accent، فلا يتعطّل العرض أبداً بسبب خطأ إملائي بسيط في الاسم.
     */
    private fun appendSticker(out: SpannableStringBuilder, raw: String) {
        val parts = raw.split("|", limit = 2)
        val label = parts[0].trim()
        val variantKey = parts.getOrNull(1)?.trim()?.lowercase().orEmpty()
        val (bg, fg) = STICKER_VARIANTS[variantKey] ?: STICKER_DEFAULT
        val start = out.length
        out.append(label)
        val end = out.length
        out.setSpan(StickerSpan(bg, fg), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.86f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /** يلتقط مقاطع `مفتاح=(قيمة)` داخل صياغة `[* ... *]` — انظر [appendMetaBadge]. */
    private val metaBadgeKeyValueRegex = Regex("^(\\w+)\\s*=\\s*\\(([^)]*)\\)$")

    /** رموز أيقونات مصغَّرة لصياغة `icon=(اسم)` — مطابقة نصّية بالاحتواء على اسم الملف/المعرِّف
     *  بلا امتداد (فـ`icon(pin.png)` أو `icon(ic_pin)` كلاهما يطابق "pin")، تسقط بهدوء لبلا أيقونة
     *  إن لم يُعرَف الاسم، فلا يتعطّل عرض الزر أبداً بسبب اسم أيقونة غير مدعوم. */
    private val ICON_GLYPHS: List<Pair<String, String>> = listOf(
        "pin" to "\uD83D\uDCCC",       // 📌
        "install" to "\u2B07\uFE0F",   // ⬇️
        "download" to "\u2B07\uFE0F",  // ⬇️
        "link" to "\uD83D\uDD17",      // 🔗
        "star" to "\u2605",            // ★
        "check" to "\u2713",           // ✓
        "play" to "\u25B6",            // ▶
        "web" to "\uD83C\uDF10",       // 🌐
        "arrow" to "\u2192",           // →
        "github" to "\u2318"
    )

    private fun iconGlyphFor(rawIcon: String?): String? {
        if (rawIcon.isNullOrBlank()) return null
        val base = rawIcon.substringAfterLast('/').substringBeforeLast('.').lowercase()
        return ICON_GLYPHS.firstOrNull { (key, _) -> base.contains(key) }?.second
    }

    /**
     * "شارة/زر وصفي" عام عبر صياغة `[* ... *]` بأجزاء مفصولة بـ`/`: الجزء الأول دائماً هو
     * النص الظاهر، وما بعده إمّا `مفتاح=(قيمة)` (المفتاحان المدعومان: `link` و`icon`) أو قيمة
     * مجرَّدة. ثلاث صيغ عملية:
     * - `[*الإصدار/1*]` → شارة إصدار ثنائية اللون (تسمية خافتة + قيمة بارزة بلون الهوية) داخل
     *   حبّة واحدة، عبر [BadgeTwoToneSpan] — بلا أي تفاعل (عرض فقط).
     * - `[*زر تثبيت/link=(رابط)*]` → زر حقيقي قابل للنقر (حبّة مملوءة بلون الهوية + نص أبيض
     *   بارز) يفتح [رابط] في المتصفح عند الضغط عبر [LinkButtonClickSpan] — مثالي لأزرار
     *   "تثبيت"/"تحميل"/"زيارة الموقع" داخل README.
     * - `[*Pin/link=(رابط)/icon=(اسم)*]` → نفس زر الرابط أعلاه، مع أيقونة مصغَّرة (عبر
     *   [iconGlyphFor]) قبل النص مباشرة — مناسب لأزرار "تثبيت"/"تنزيل" المصحوبة برمز.
     * لا رابط ولا قيمة مجرَّدة (مثال: `[*جديد*]` وحدها) → يسقط بهدوء إلى ستيكر عادي بلون الهوية
     * الافتراضي عبر [appendSticker]، فلا يُفقَد المحتوى صمتاً بسبب صياغة ناقصة.
     */
    private fun appendMetaBadge(out: SpannableStringBuilder, raw: String) {
        val parts = raw.split("/").map { it.trim() }.filter { it.isNotEmpty() }
        if (parts.isEmpty()) return
        val label = parts[0]

        var linkUrl: String? = null
        var iconRaw: String? = null
        var plainValue: String? = null
        for (part in parts.drop(1)) {
            val m = metaBadgeKeyValueRegex.find(part)
            if (m != null) {
                when (m.groupValues[1].lowercase()) {
                    "link" -> linkUrl = m.groupValues[2].trim()
                    "icon" -> iconRaw = m.groupValues[2].trim()
                }
            } else if (plainValue == null) {
                plainValue = part
            }
        }

        when {
            linkUrl != null -> {
                val glyph = iconGlyphFor(iconRaw)
                val buttonText = if (glyph != null) "$glyph  $label" else label
                val start = out.length
                out.append(buttonText)
                val end = out.length
                out.setSpan(StickerSpan(COLOR_BULLET, 0xFFFFFFFF.toInt()), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(StyleSpan(Typeface.BOLD), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(RelativeSizeSpan(0.9f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                out.setSpan(LinkButtonClickSpan(linkUrl), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            plainValue != null -> {
                val start = out.length
                out.append("$label $plainValue")
                val end = out.length
                out.setSpan(
                    BadgeTwoToneSpan(label, plainValue, COLOR_INLINE_CODE_BG, COLOR_H_DIM, COLOR_BULLET),
                    start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
                )
            }
            else -> appendSticker(out, label)
        }
    }

    /**
     * زر رابط حقيقي: [ClickableSpan] يفتح [url] في المتصفح (Intent.ACTION_VIEW) عند النقر، عبر
     * سياق [widget] الممرَّر تلقائياً — بلا حاجة لتمرير Context لهذا الملف، بنفس أسلوب
     * [CopyCodeSpan]. رابط غير صالح أو غياب أي تطبيق قادر على فتحه يُسقَط بهدوء بلا انهيار.
     */
    private class LinkButtonClickSpan(private val url: String) : ClickableSpan() {
        override fun onClick(widget: View) {
            try {
                widget.context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
            } catch (t: Throwable) {
                // رابط غير صالح، أو لا يوجد تطبيق على الجهاز قادر على فتحه — نتجاهل بهدوء
            }
        }

        override fun updateDrawState(ds: TextPaint) {}
    }

    /** لون هوية اللغة (نقطة + وسم الاسم)، محايد للغات غير المدرَجة في [LANGUAGE_ACCENTS]. */
    private fun languageAccentColor(lang: String): Int =
        LANGUAGE_ACCENTS[lang.trim().lowercase()] ?: LANGUAGE_ACCENT_DEFAULT

    /**
     * رأس بطاقة كود احترافي واحد: نقطة ملوَّنة بهوية اللغة + اسمها (إن ذُكرت لغة، وإلا وسم عام
     * "CODE")، ثم مسافة، ثم زر "نسخ" حقيقي قابل للنقر عبر [CopyCodeSpan] — ينسخ [code] الخام
     * كاملاً (بلا أي تنسيق) للحافظة عند النقر، تماماً كزر النسخ في كتل كود GitHub/محرِّرات IDE.
     */
    private fun appendCodeHeader(out: SpannableStringBuilder, lang: String, code: String) {
        val accent = languageAccentColor(lang)
        val dotStart = out.length
        out.append("\u25CF ") // نقطة ملوَّنة صغيرة قبل اسم اللغة
        out.setSpan(ForegroundColorSpan(accent), dotStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.62f), dotStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        val labelStart = out.length
        out.append(if (lang.isNotBlank()) lang.uppercase() else "CODE")
        out.setSpan(ForegroundColorSpan(accent), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(TypefaceSpan("monospace"), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.68f), labelStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

        out.append("  ")

        val copyStart = out.length
        out.append("\u29C9 \u0646\u0633\u062E") // "⧉ نسخ"
        val copyEnd = out.length
        out.setSpan(StickerSpan(COLOR_COPY_BUTTON_BG, COLOR_CODE_TEXT), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.66f), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(CopyCodeSpan(code), copyStart, copyEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * زر نسخ حقيقي: [ClickableSpan] ينسخ [code] الخام كاملاً إلى حافظة الجهاز عند النقر (عبر
     * سياق [widget] الممرَّر تلقائياً من TextView، بلا حاجة لتمرير Context لهذا الملف بأكمله)،
     * مع رسالة تأكيد قصيرة (Toast). يعمل فقط إن كان TextView مُفعَّلاً بـ[LinkMovementMethod]
     * (يتم ذلك تلقائياً عبر [applyTo]).
     */
    private class CopyCodeSpan(private val code: String) : ClickableSpan() {
        override fun onClick(widget: View) {
            val ctx = widget.context
            val clipboard = ctx.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
            clipboard?.setPrimaryClip(ClipData.newPlainText("code", code))
            Toast.makeText(ctx, "\u062A\u0645 \u0646\u0633\u062E \u0627\u0644\u0643\u0648\u062F", Toast.LENGTH_SHORT).show()
        }

        // بلا تسطير/لون رابط افتراضي — الشكل مُتحكَّم به بالكامل عبر StickerSpan المرافق لنفس النطاق.
        override fun updateDrawState(ds: TextPaint) {}
    }

    /**
     * يحوّل كود [code] الخام إلى نص مُصنَّف الرموز عبر [codeTokenRegex]: تعليقات (مائلة، رمادية)،
     * نصوص حرفية (أخضر)، أرقام (برتقالي)، توجيهات `@...` (بنفسجي فاتح)، وسوم إغلاق `.end/...`
     * (ذهبي)، كلمات مفتاحية (أزرق، بارز)، استدعاءات دوال (ذهبي)، وأسماء مُسنَدة قبل `=` (برتقالي
     * فاتح) — كل رمز بلون منفصل صريح (لا نطاق لون عام يغطّي الكتلة) لتفادي أي تعارض بين Span
     * لون شامل وSpans الألوان الجزئية لكل رمز.
     */
    private fun appendHighlightedCode(out: SpannableStringBuilder, code: String) {
        var idx = 0
        for (match in codeTokenRegex.findAll(code)) {
            if (match.range.first > idx) {
                val plain = code.substring(idx, match.range.first)
                val plainStart = out.length
                out.append(plain)
                out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), plainStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            }
            val g = match.groups
            val (value, color, style) = when {
                g[1] != null -> Triple(g[1]!!.value, COLOR_SYNTAX_COMMENT, Typeface.ITALIC)
                g[2] != null -> Triple(g[2]!!.value, COLOR_SYNTAX_COMMENT, Typeface.ITALIC)
                g[3] != null -> Triple(g[3]!!.value, COLOR_SYNTAX_STRING, Typeface.NORMAL)
                g[4] != null -> Triple(g[4]!!.value, COLOR_SYNTAX_DIRECTIVE, Typeface.BOLD)
                g[5] != null -> Triple(g[5]!!.value, COLOR_SYNTAX_BUILTIN, Typeface.NORMAL)
                g[6] != null -> Triple(g[6]!!.value, COLOR_SYNTAX_NUMBER, Typeface.NORMAL)
                g[7] != null -> Triple(g[7]!!.value, COLOR_SYNTAX_KEYWORD, Typeface.BOLD)
                g[8] != null -> Triple(g[8]!!.value, COLOR_SYNTAX_BUILTIN, Typeface.NORMAL)
                g[9] != null -> Triple(g[9]!!.value, COLOR_SYNTAX_ATTR, Typeface.NORMAL)
                else -> Triple(match.value, COLOR_CODE_TEXT, Typeface.NORMAL)
            }
            val start = out.length
            out.append(value)
            val end = out.length
            out.setSpan(ForegroundColorSpan(color), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            if (style != Typeface.NORMAL) out.setSpan(StyleSpan(style), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            idx = match.range.last + 1
        }
        if (idx < code.length) {
            val plainStart = out.length
            out.append(code.substring(idx))
            out.setSpan(ForegroundColorSpan(COLOR_CODE_TEXT), plainStart, out.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
    }

    private fun isTableSeparator(line: String): Boolean {
        val t = line.trim()
        if (t.isEmpty() || !t.contains("-")) return false
        val cells = splitTableRow(t)
        if (cells.isEmpty()) return false
        return cells.all { tableSeparatorRegex.matches(it.trim()) }
    }

    private fun splitTableRow(line: String): List<String> {
        var l = line.trim()
        if (l.startsWith("|")) l = l.drop(1)
        if (l.endsWith("|")) l = l.dropLast(1)
        return l.split("|").map { it.trim() }
    }

    /** يبني جدول Markdown كامل كصندوق نصّي أحادي المسافة بخطوط اتصال حقيقية (┌─┬─┐ ...). */
    private fun appendTable(out: SpannableStringBuilder, header: List<String>, rows: List<List<String>>) {
        val colCount = header.size
        val maxCellLen = 24
        fun cell(row: List<String>, col: Int): String {
            val raw = row.getOrNull(col).orEmpty()
            return if (raw.length > maxCellLen) raw.take(maxCellLen - 1) + "…" else raw
        }
        val widths = IntArray(colCount) { col ->
            var w = cell(header, col).length
            for (row in rows) w = maxOf(w, cell(row, col).length)
            maxOf(w, 3)
        }

        fun border(left: String, mid: String, right: String, fill: String): String =
            left + widths.joinToString(mid) { fill.repeat(it + 2) } + right

        fun dataRow(row: List<String>): String =
            "│ " + (0 until colCount).joinToString(" │ ") { col -> cell(row, col).padEnd(widths[col]) } + " │"

        val start = out.length
        out.append(border("┌─", "─┬─", "─┐", "─")).append('\n')

        val headerLineStart = out.length
        out.append(dataRow(header))
        val headerLineEnd = out.length
        out.append('\n')

        out.append(border("├─", "─┼─", "─┤", "─")).append('\n')
        rows.forEachIndexed { idx, row ->
            out.append(dataRow(row))
            if (idx != rows.lastIndex) out.append('\n')
        }
        out.append('\n').append(border("└─", "─┴─", "─┘", "─"))
        val end = out.length

        out.setSpan(TypefaceSpan("monospace"), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(RelativeSizeSpan(0.82f), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_TABLE_TEXT), start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(
            RoundedCardSpan(COLOR_TABLE_BG, COLOR_CARD_BORDER, start, end),
            start, end, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE
        )
        out.setSpan(ForegroundColorSpan(COLOR_TABLE_BORDER), start, headerLineStart, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(ForegroundColorSpan(COLOR_TABLE_HEADER), headerLineStart, headerLineEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        out.setSpan(StyleSpan(Typeface.BOLD), headerLineStart, headerLineEnd, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
    }

    /**
     * بطاقة خلفية بزوايا مدوَّرة حقيقية (لا مستطيل خام) تمتد بعرض السطر خلف كل كتلة (كود/اقتباس/
     * جدول)، مع حدّ خفيف حول كامل البطاقة — التقويس يظهر فقط عند السطر الأول والسطر الأخير من
     * الكتلة (يُكتشَفان بمقارنة نطاق كل سطر مُمرَّر من [drawBackground] بحدود الـSpan نفسه
     * [spanStart]/[spanEnd])، أما الأسطر الوسطى فتبقى بحواف مستقيمة لتتّصل بصرياً بسلاسة.
     */
    private class RoundedCardSpan(
        private val bgColor: Int,
        private val borderColor: Int,
        private val spanStart: Int,
        private val spanEnd: Int,
        private val cornerRadius: Float = 16f,
        private val insetTop: Float = 3f,
        private val insetBottom: Float = 3f,
        private val borderWidth: Float = 2f
    ) : LineBackgroundSpan {
        override fun drawBackground(
            canvas: Canvas, paint: Paint,
            left: Int, right: Int, top: Int, baseline: Int, bottom: Int,
            text: CharSequence, start: Int, end: Int, lnum: Int
        ) {
            val isFirst = start <= spanStart
            val isLast = end >= spanEnd
            val rect = RectF(left.toFloat(), top - insetTop, right.toFloat(), bottom + insetBottom)
            val corner = cornerRadius
            val radii = floatArrayOf(
                if (isFirst) corner else 0f, if (isFirst) corner else 0f, // أعلى-يسار
                if (isFirst) corner else 0f, if (isFirst) corner else 0f, // أعلى-يمين
                if (isLast) corner else 0f, if (isLast) corner else 0f,   // أسفل-يمين
                if (isLast) corner else 0f, if (isLast) corner else 0f    // أسفل-يسار
            )
            val path = Path().apply { addRoundRect(rect, radii, Path.Direction.CW) }

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            paint.isAntiAlias = true

            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawPath(path, paint)

            if (isFirst || isLast) {
                paint.style = Paint.Style.STROKE
                paint.strokeWidth = borderWidth
                paint.color = borderColor
                canvas.drawPath(path, paint)
            }

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * خط أفقي فاصل حقيقي يُرسم بعرض السطر كاملاً، بدل الاعتماد على سلسلة شرطات نصّية. يُعاد
     * استخدامه أيضاً كخط تحت العناوين H1/H2 (بلون وسُمك مختلفَين) لأسلوب README احترافي.
     */
    private class RuleSpan(
        private val color: Int = COLOR_RULE,
        private val thickness: Float = 2f
    ) : ReplacementSpan() {
        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int = 0

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val savedColor = paint.color
            val savedWidth = paint.strokeWidth
            val savedCap = paint.strokeCap
            paint.color = color
            paint.strokeWidth = thickness
            paint.strokeCap = Paint.Cap.ROUND
            val middle = (top + bottom) / 2f
            canvas.drawLine(0f, middle, canvas.width.toFloat(), middle, paint)
            paint.color = savedColor
            paint.strokeWidth = savedWidth
            paint.strokeCap = savedCap
        }
    }

    /** شريط جانبي ملوَّن حقيقي (لا مجرّد مسافة بادئة) يُرسم على طول كل سطر من كتلة اقتباس `>`. */
    private class QuoteBarSpan(
        private val color: Int,
        private val barWidthPx: Int = 6,
        private val gapPx: Int = 18
    ) : LeadingMarginSpan {
        override fun getLeadingMargin(first: Boolean): Int = barWidthPx + gapPx

        override fun drawLeadingMargin(
            canvas: Canvas, paint: Paint, x: Int, dir: Int,
            top: Int, baseline: Int, bottom: Int,
            text: CharSequence?, start: Int, end: Int,
            first: Boolean, layout: android.text.Layout?
        ) {
            val savedColor = paint.color
            val savedStyle = paint.style
            paint.color = color
            paint.style = Paint.Style.FILL
            val barX = x.toFloat()
            canvas.drawRect(barX, top.toFloat(), barX + barWidthPx, bottom.toFloat(), paint)
            paint.color = savedColor
            paint.style = savedStyle
        }
    }

    /**
     * "ستيكر Rin": بادج ملوَّن بخلفية حقيقية بزوايا مدوَّرة يُرسم عبر Canvas مباشرة (لا مجرّد
     * تلوين نص)، بنفس روح شارات "موثّق"/إحصاءات التطبيق — مكوّن بصري جديد كلياً في هذا الملف.
     */
    private class StickerSpan(
        private val bgColor: Int,
        private val textColor: Int
    ) : ReplacementSpan() {
        private val horizontalPad = 16f
        private val verticalPad = 5f
        private val cornerRadius = 16f

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            if (fm != null) {
                val orig = paint.fontMetricsInt
                fm.ascent = orig.ascent - verticalPad.toInt()
                fm.descent = orig.descent + verticalPad.toInt()
                fm.top = fm.ascent
                fm.bottom = fm.descent
            }
            return (paint.measureText(text, start, end) + horizontalPad * 2).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val width = paint.measureText(text, start, end)
            val rect = RectF(x, top.toFloat() + 1f, x + width + horizontalPad * 2, bottom.toFloat() - 1f)

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias

            paint.isAntiAlias = true
            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            paint.color = textColor
            canvas.drawText(text, start, end, x + horizontalPad, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
        }
    }

    /**
     * شارة إصدار ثنائية اللون داخل حبّة واحدة (مثال: `الإصدار` بلون خافت + `1` بلون الهوية
     * بارزاً) — تُستخدَم لصياغة `[*تسمية/قيمة*]` (انظر [appendMetaBadge]). بخلاف [StickerSpan]
     * (لون نص واحد)، هذا الصنف يرسم النص بلونَين منفصلَين صراحةً داخل [draw] بدل الاعتماد على
     * Spans متداخلة (تُتجاهَل داخل نطاق أي [ReplacementSpan] لأن الرسم يُسلَّم إليه كاملاً).
     */
    private class BadgeTwoToneSpan(
        private val label: String,
        private val value: String,
        private val bgColor: Int,
        private val labelColor: Int,
        private val valueColor: Int
    ) : ReplacementSpan() {
        private val horizontalPad = 14f
        private val verticalPad = 5f
        private val cornerRadius = 14f
        private val gap = 6f

        override fun getSize(paint: Paint, text: CharSequence, start: Int, end: Int, fm: Paint.FontMetricsInt?): Int {
            if (fm != null) {
                val orig = paint.fontMetricsInt
                fm.ascent = orig.ascent - verticalPad.toInt()
                fm.descent = orig.descent + verticalPad.toInt()
                fm.top = fm.ascent
                fm.bottom = fm.descent
            }
            return (horizontalPad * 2 + paint.measureText(label) + gap + paint.measureText(value)).toInt()
        }

        override fun draw(
            canvas: Canvas, text: CharSequence, start: Int, end: Int,
            x: Float, top: Int, y: Int, bottom: Int, paint: Paint
        ) {
            val labelW = paint.measureText(label)
            val valueW = paint.measureText(value)
            val rect = RectF(x, top.toFloat() + 1f, x + horizontalPad * 2 + labelW + gap + valueW, bottom.toFloat() - 1f)

            val savedColor = paint.color
            val savedStyle = paint.style
            val savedAA = paint.isAntiAlias
            val savedBold = paint.isFakeBoldText

            paint.isAntiAlias = true
            paint.style = Paint.Style.FILL
            paint.color = bgColor
            canvas.drawRoundRect(rect, cornerRadius, cornerRadius, paint)

            paint.color = labelColor
            canvas.drawText(label, x + horizontalPad, y.toFloat(), paint)

            paint.color = valueColor
            paint.isFakeBoldText = true
            canvas.drawText(value, x + horizontalPad + labelW + gap, y.toFloat(), paint)

            paint.color = savedColor
            paint.style = savedStyle
            paint.isAntiAlias = savedAA
            paint.isFakeBoldText = savedBold
        }
    }
}

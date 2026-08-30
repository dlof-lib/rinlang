package com.dlof.rinlang

/**
 * أدوات مشتركة تفهم "وسوم" لغة الحاويات في Rin (@container=name ... .end/container، Section،
 * Translations، @view.<Kind>، @theme...)، مبنية على القائمة الرسمية المعتمدة في محرّك اللغة
 * نفسه (انظر Parser::atBlock::validTags و sectionBlock/translationsBlock/viewBlock/themeBlock
 * في rin_parser.cpp)، لاستخدامها في أكثر من ميزة تحرير خاصة بلغة Rin تحديداً:
 * - فحص توازن الوسوم ([RinContainerTags.checkTagBalance])، إلى جانب "Check brackets" الحالي.
 * - "بنية الملف" (outline) للتنقّل السريع بين الحاويات ([RinContainerTags.buildOutline]).
 * - كتالوج مقتطفات الإدراج السريع ([RinSnippets]) في نفس الملف.
 *
 * ملاحظة مهمة: هذا تحليل سطحي *بمستوى السطر* (كل وسم فتح/إغلاق يشغل سطراً مستقلاً بمفرده،
 * كما هو الحال في كل أمثلة اللغة الرسمية)، وليس تحليلاً نحوياً كاملاً كمحرّك rin_parser.cpp
 * نفسه — لا يتحقق مثلاً من محتوى الجسم (route داخل container.api فقط، إلخ). هذا يكفي تماماً
 * لمساعدات محرر نصّية (تمييز/تنقّل)، بينما يبقى التحقق النهائي الملزم من صحة الشيفرة عبر "Run"
 * الذي يستدعي المحرّك الحقيقي.
 */
object RinContainerTags {

    // "@<tag>=name" التي تُغلَق بنفس النص الحرفي لِـ tag (انظر consumeEndTag(tag, ...) داخل
    // Parser::atBlock). القائمة مطابقة حرفياً لـ validTags في rin_parser.cpp.
    private val atTags = setOf(
        "container", "container.pipe", "container.data", "container.api", "container.import",
        "container.table", "container.doc", "container.object", "Object", "container.open/object",
        "container.portal", "portal", "container.block", "block", "container.sticker", "sticker",
        "container.aukt", "AUKT", "container.chatbot", "chatbot", "pipe", "data", "api",
        "Containers.Group", "Volume"
    )

    // "@view.<Kind>=name ... .end/view" — أي Kind تُقبل، لكن الإغلاق حرفياً دوماً ".end/view"
    // (انظر Parser::viewBlock، حيث consumeEndTag("view", ...) ثابتة بصرف النظر عن Kind).
    private val viewTagPattern = Regex("^@view\\.[A-Za-z][A-Za-z0-9_]*(=.*)?$")

    // "@theme=Name ... .end/theme"
    private val themeTagPattern = Regex("^@theme(=.*)?$")

    // كلمات مفتاحية بلا '@' تبدأ كتلة مغلقة بـ ".end/<Keyword>" (انظر sectionBlock/
    // translationsBlock في rin_parser.cpp).
    private val bareBlockKeywords = setOf("Section", "Translations")

    /**
     * إن كان [rawLine] (بعد trim) سطر فتح وسم معروف، تُرجع اسم الوسم المطلوب لإغلاقه (بلا
     * `.end/` وبلا `@`)، وإلا null. لا يطابق أسطر العبارات العادية المنتهية بـ `;` (تعيين حقل
     * مثل `text title = "..."`، وليس بداية كتلة).
     */
    fun closingTagFor(rawLine: String): String? {
        val line = rawLine.trim()
        if (line.isEmpty() || line.endsWith(";")) return null

        if (line.startsWith("@")) {
            if (viewTagPattern.matches(line)) return "view"
            if (themeTagPattern.matches(line)) return "theme"
            val tag = line.removePrefix("@").substringBefore('=').trim()
            return tag.takeIf { it in atTags }
        }

        val bareTag = line.substringBefore('=').trim()
        return bareTag.takeIf { it in bareBlockKeywords }
    }

    /** إن كان [rawLine] (بعد trim) سطر إغلاق `.end/<tag>` أو `.end/<tag>=name`، تُرجع اسم الوسم. */
    fun closingTagNameIn(rawLine: String): String? {
        val line = rawLine.trim()
        if (!line.startsWith(".end/")) return null
        return line.removePrefix(".end/").substringBefore('=').trim().ifEmpty { null }
    }

    /**
     * يفحص توازن كل وسوم الحاويات في [text] (فتح/إغلاق متطابقان ومتعشّشان بصورة صحيحة).
     * يُرجع null إن كان كل شيء متوازناً، أو رقم أول سطر (1-based) فيه مشكلة: وسم إغلاق يتيم،
     * وسم إغلاق لا يطابق آخر وسم فتح لم يُغلق بعد، أو وسم فُتح ولم يُغلق حتى نهاية الملف —
     * بنفس أسلوب [نتيجة] `checkBracketBalance` الموجودة أصلاً لأقواس `{}`/`[]`/`()`.
     */
    fun checkTagBalance(text: String): Int? {
        data class Open(val tag: String, val line: Int)
        val stack = ArrayDeque<Open>()
        val lines = text.lines()
        for (index in lines.indices) {
            val lineNumber = index + 1
            val trimmed = lines[index].trim()
            val closingName = closingTagNameIn(trimmed)
            if (closingName != null) {
                if (stack.isEmpty() || stack.last().tag != closingName) return lineNumber
                stack.removeLast()
                continue
            }
            val tag = closingTagFor(trimmed) ?: continue
            stack.addLast(Open(tag, lineNumber))
        }
        return stack.lastOrNull()?.line
    }

    /** عنصر واحد في قائمة "بنية الملف": رقم سطره الأصلي، عمق تعشيشه، ونص العرض. */
    data class OutlineEntry(val lineNumber: Int, val depth: Int, val label: String)

    /**
     * يبني قائمة مسطّحة (بترتيب الظهور في الملف) بكل وسوم الفتح في [text] مع رقم سطرها
     * الأصلي وعمق تعشيشها، لعرضها في حوار "بنية الملف" (Outline) والتنقّل السريع بينها.
     * لا يفشل على وسوم غير متوازنة أو غير معروفة: أي سطر لا يطابق وسماً معروفاً، أو `.end/`
     * لا يطابق قمة المكدّس، يُتجاهَل بصمت بدل رمي استثناء — الميزة تبقى مفيدة أثناء الكتابة
     * حتى قبل اكتمال الملف.
     */
    fun buildOutline(text: String): List<OutlineEntry> {
        val entries = mutableListOf<OutlineEntry>()
        val stack = ArrayDeque<String>()
        text.lines().forEachIndexed { index, rawLine ->
            val trimmed = rawLine.trim()
            val closingName = closingTagNameIn(trimmed)
            if (closingName != null) {
                if (stack.isNotEmpty() && stack.last() == closingName) stack.removeLast()
                return@forEachIndexed
            }
            val tag = closingTagFor(trimmed) ?: return@forEachIndexed
            entries.add(OutlineEntry(lineNumber = index + 1, depth = stack.size, label = trimmed))
            stack.addLast(tag)
        }
        return entries
    }
}

/**
 * كتالوج مقتطفات جاهزة لأشهر هياكل لغة الحاويات في Rin، تُدرَج دفعة واحدة صحيحة التعشيش
 * والمسافة البادئة بدل كتابتها يدوياً في كل مرة (تماماً كفكرة "Live Templates" في IDEs الكبيرة).
 * يحدّد كل مقتطف مكان المؤشر النهائي بعلامة [CURSOR_MARKER] الداخلية، التي تُستهلك وتُزال بواسطة
 * [CodeEditorController.insertSnippetAtCursor] بدل ترك المؤشر في نهاية النص المُدرَج بالكامل.
 */
object RinSnippets {
    /** علامة داخلية (لا تظهر أبداً في شيفرة Rin حقيقية) تحدّد أين يُترَك المؤشر بعد الإدراج. */
    const val CURSOR_MARKER = "\u0000CURSOR\u0000"

    data class Snippet(val title: String, val template: String)

    val all: List<Snippet> = listOf(
        Snippet(
            "container",
            "@container=my_data\n    $CURSOR_MARKER\n.end/container\n"
        ),
        Snippet(
            "container.doc",
            "@container.doc=users\n    document id=\"u1\" fields={ name: \"سارة\", age: 28 };\n    $CURSOR_MARKER\n.end/container.doc\n"
        ),
        Snippet(
            "container.pipe",
            "@container.pipe=my_pipeline\n    let raw = [1, 2, 3];\n    let result = raw $CURSOR_MARKER;\n.end/container.pipe\n"
        ),
        Snippet(
            "container.api (مع route)",
            "@container.api=my_api\n" +
                "    route method=\"GET\" path=\"/hello\" status=200 body={ message: \"hi\" };\n" +
                "    $CURSOR_MARKER\n" +
                ".end/container.api\n"
        ),
        Snippet(
            "Containers.Group",
            "@Containers.Group=my_group\n" +
                "    @container=item_1\n" +
                "        $CURSOR_MARKER\n" +
                "    .end/container\n" +
                ".end/Containers.Group\n"
        ),
        Snippet(
            "Section",
            "Section=my_section\n    $CURSOR_MARKER\n.end/Section\n"
        ),
        Snippet(
            "Translations",
            "Translations\n" +
                "    translation lang=\"ar\" text=\"مرحبا\";\n" +
                "    translation lang=\"en\" text=\"Hello\";\n" +
                "    $CURSOR_MARKER\n" +
                ".end/Translations\n"
        )
    )
}

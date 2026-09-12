package com.dlof.rinlang

/**
 * أدوات مشتركة تفهم "وسوم" لغة الحاويات في Rin (@container=name ... .end/container، Section،
 * Translations، @view.<Kind>، @theme، وطبقة الواجهة @element.<kind> / @loop(.Kind) الموصوفة في
 * docs/RIN_ELEMENTS.md)، مبنية على القائمة الرسمية المعتمدة في محرّك اللغة نفسه (انظر
 * Parser::atBlock::validTags و sectionBlock/translationsBlock/viewBlock/themeBlock/
 * elementDeclaration/loopCanvasDeclaration في rin_parser.cpp)، لاستخدامها في أكثر من ميزة تحرير
 * خاصة بلغة Rin تحديداً:
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
        "container.aukt", "AUKT", "container.chatbot", "chatbot",
        // make (سابقاً "Everything"؛ الاسمان القديمان "Everything"/"container.everything" ما زالا
        // مقبولين كاسم بديل للتوافق العكسي): ثلاث صيغ متكافئة تماماً "make"/"Rin.make"/"container.make".
        "container.everything", "Everything", "container.make", "make", "Rin.make",
        "pipe", "data", "api",
        "Containers.Group", "Volume"
    )

    // "@view.<Kind>=name ... .end/view" — أي Kind تُقبل، لكن الإغلاق حرفياً دوماً ".end/view"
    // (انظر Parser::viewBlock، حيث consumeEndTag("view", ...) ثابتة بصرف النظر عن Kind).
    private val viewTagPattern = Regex("^@view\\.[A-Za-z][A-Za-z0-9_]*(=.*)?$")

    // "@theme=Name ... .end/theme"
    private val themeTagPattern = Regex("^@theme(=.*)?$")

    // "@element.<kind>=name ... .end/element" — طبقة الـ Elements في docs/RIN_ELEMENTS.md، أي kind
    // تُقبل (button/input/search/.../calculator)، لكن الإغلاق حرفياً دوماً ".end/element" بصرف
    // النظر عن الـ kind (انظر Parser::elementDeclaration: consumeEndTag("element", ...) في
    // rin_parser.cpp) — بنفس منطق @view.<Kind> أعلاه تماماً.
    private val elementTagPattern = Regex("^@element\\.[A-Za-z_][A-Za-z0-9_]*(=.*)?$")

    // "@loop=name" أو "@loop.<Kind>=name" ... .end/loop" — طبقة اللوحة (canvas) في نفس المستند،
    // الشكل بلا ".<Kind>" افتراضه "Column" (انظر Parser::loopCanvasDeclaration)، وكلاهما يُغلَق
    // حرفياً دوماً بـ ".end/loop" (consumeEndTag("loop", ...)).
    private val loopTagPattern = Regex("^@loop(\\.[A-Za-z][A-Za-z0-9_]*)?(=.*)?$")

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
            if (elementTagPattern.matches(line)) return "element"
            if (loopTagPattern.matches(line)) return "loop"
            val tag = line.removePrefix("@").substringBefore('=').trim()
            return tag.takeIf { it in atTags }
        }

        // .object("id") ... .end/object  -> صيغة كائن (Object) بأسلوب استدعاء دوال (call-style)،
        // إضافية بحتة بجانب `.object=text` (التي تبقى عبارة حقل عادية منتهية بـ ';' فتُستبعَد أصلاً
        // أعلاه) و`@Object=name`. تُغلَق دوماً حرفياً بـ `.end/object` (انظر
        // Parser::objectLiteralStatement: consumeEndTag("object", ...) في rin_parser.cpp).
        if (line.startsWith(".object(") && line.endsWith(")")) return "object"

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
     * كتلة وسم واحدة قابلة للطيّ (Code Folding) — تُستهلَك من [RinCodeEditorView] لبناء خريطة
     * الطيّ الكاملة (إلى جانب كتل '{'..'}' الحقيقية المبنية هناك مباشرة من تلوين الـlexer).
     * [startLine]/[endLine] 0-based (سطر فتح الوسم وسطر ".end/..." المطابق له فعليًا).
     * [displayKind] نص عرض بحت (مثال: "view.Column"، "container.pipe"، "Section") مُشتقّ من
     * نص سطر الفتح نفسه، أدقّ من [closingTagFor] المُوحَّد (الذي يُرجع دوماً "view" لأي
     * @view.<Kind> مثلاً، لأن الإغلاق حرفياً موحَّد) — هنا فقط لعرضٍ أوضح في شريحة الطيّ.
     * [name] الاسم بعد '=' إن وُجد (أو محتوى `.object("...")`)، أو null إن لم يحمل الوسم اسماً.
     */
    data class FoldTagRegion(val startLine: Int, val endLine: Int, val displayKind: String, val name: String?)

    /**
     * يبني قائمة كل كتل الوسوم القابلة للطيّ في [text] — بنفس خوارزمية المكدّس (stack) المستخدَمة
     * في [checkTagBalance]/[buildOutline] تمامًا (فتح/إغلاق متطابقان ومتعشّشان بصورة صحيحة)، لكن
     * يُصدر هنا كل زوج (فتح، إغلاق مطابق فعلياً) بدل مجرّد رقم أول خلل أو قائمة مسطّحة لأغراض
     * التنقّل. بأمان تام مع كود غير مكتمل: أي وسم فُتح ولم يُغلق حتى نهاية الملف، أو وسم إغلاق
     * يتيم/لا يطابق قمة المكدّس، لا يُصدر له أي كتلة إطلاقاً (يُتجاهَل بصمت، بلا استثناء) — تماماً
     * كنهج "الأمان" في checkTagBalance، فتبقى الميزة مفيدة أثناء الكتابة قبل اكتمال الملف.
     */
    fun buildFoldRegions(text: String): List<FoldTagRegion> {
        data class Open(val tag: String, val displayKind: String, val name: String?, val line: Int)
        val stack = ArrayDeque<Open>()
        val regions = mutableListOf<FoldTagRegion>()
        val lines = text.lines()
        for (index in lines.indices) {
            val trimmed = lines[index].trim()
            val closingName = closingTagNameIn(trimmed)
            if (closingName != null) {
                if (stack.isNotEmpty() && stack.last().tag == closingName) {
                    val open = stack.removeLast()
                    regions.add(FoldTagRegion(open.line, index, open.displayKind, open.name))
                }
                continue
            }
            val tag = closingTagFor(trimmed) ?: continue
            stack.addLast(Open(tag, displayKindFor(trimmed), extractTagName(trimmed), index))
        }
        return regions
    }

    /** نص عرض أدقّ من الوسم القانوني (canonical) المستخدَم للمطابقة فقط — مثال: "@view.Column=root"
     *  يُعطي "view.Column" هنا بدل "view" الموحَّد الذي يُرجعه [closingTagFor] (صحيح للمطابقة لكن
     *  أقل فائدة للعرض، إذ يفقد الـKind الفعلي). عرض بحت، لا يُستخدَم في أي فحص/مطابقة. */
    private fun displayKindFor(trimmedLine: String): String = when {
        trimmedLine.startsWith(".object(") -> "object"
        trimmedLine.startsWith("@") -> trimmedLine.removePrefix("@").substringBefore('=').trim()
        else -> trimmedLine.substringBefore('=').trim()
    }

    /** يستخرج "اسم" وسم فتح لعرضه في شريحة الطيّ: ما بعد '=' (بلا علامات اقتباس محيطة)، أو
     *  محتوى `.object("id")`. عرض بحت — null إن لم يحمل الوسم اسماً (مثال: `@theme` بلا Kind،
     *  أو `Translations`). */
    private fun extractTagName(trimmedLine: String): String? {
        if (trimmedLine.startsWith(".object(")) {
            return trimmedLine.removePrefix(".object(").removeSuffix(")").trim().trim('"', '\'').ifEmpty { null }
        }
        val eq = trimmedLine.indexOf('=')
        if (eq == -1) return null
        return trimmedLine.substring(eq + 1).trim().trim('"', '\'').ifEmpty { null }
    }

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
 * [RinCodeEditorController.insertSnippetAtCursor] بدل ترك المؤشر في نهاية النص المُدرَج بالكامل.
 */
object RinSnippets {
    /** علامة داخلية (لا تظهر أبداً في شيفرة Rin حقيقية) تحدّد أين يُترَك المؤشر بعد الإدراج. */
    const val CURSOR_MARKER = "\u0000CURSOR\u0000"

    data class Snippet(val title: String, val template: String)

    val all: List<Snippet> = listOf(
        Snippet("UI: Loop Canvas", "@loop=app\n    width=390;\n    height=700;\n    background=\"#ffffff\";\n\n    @element.button=run\n        text=\"Run\";\n    .end/element\n.end/loop\n"),
        Snippet("UI: Button", "@element.button=button\n    text=\"Button\";\n.end/element\n"),
        Snippet("UI: Input", "@element.input=input\n    placeholder=\"Text...\";\n.end/element\n"),
        // Ready-elements expansion: a few of the newer element kinds (see docs/RIN_ELEMENTS.md),
        // inserted the same way the ones above are.
        Snippet("UI: Search Bar", "@element.search=search\n    placeholder=\"Search...\";\n.end/element\n"),
        Snippet("UI: Link", "@element.link=more\n    text=\"Learn more\";\n.end/element\n"),
        Snippet("UI: Row (RTL)", "@element.row=actions\n    direction=\"rtl\";\n\n    @element.button=ok\n        text=\"موافق\";\n    .end/element\n.end/element\n"),
        Snippet("UI: Code Editor", "@element.code_editor=snippet\n    value=\"print 1;\";\n.end/element\n"),
        Snippet("UI: Calculator", "@element.calculator=calc\n.end/element\n"),
        Snippet("UI: Container Event", "@container=app\n    @element.button=run\n        text=\"Run\";\n    .end/element\n    on.run.click=runCode();\n.end/container\n"),
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
        ),
        Snippet(
            ".object(\"id\") (call-style)",
            ".object(\"$CURSOR_MARKER\")\n" +
                "    name(\"...\");\n" +
                "    container.();\n" +
                ".end/object\n"
        ),
        Snippet(
            "@view.Object (معاينة حيّة لكائن في Loom)",
            "@view.Object=$CURSOR_MARKER\n" +
                "    source=\"user01\";\n" +
                ".end/view\n"
        ),
        Snippet(
            "make (أنشئ/برمِج أي شيء — سابقاً Everything)",
            "@make=$CURSOR_MARKER\n" +
                "    fun greet(name) { return \"Hello \" + name; }\n" +
                "    show greet(\"World\");\n" +
                ".end/make\n"
        ),
        Snippet("Artifact: QR داخل Container", "@container=Product\n    let id = uuid();\n    let qr = container.make.qr(id, \"product-qr\", 512);\n    $CURSOR_MARKER\n.end/container\n"),
        Snippet("Artifact: Barcode داخل Container", "@container=Product\n    let barcode = container.make.barcode(\"RIN-001\", \"product-barcode\", \"code128\");\n    $CURSOR_MARKER\n.end/container\n"),
        Snippet("Artifact: File + Hash", "@container=Data\n    let f = container.make.file(\"data.txt\", \"Rin\");\n    let h = hash(\"Rin\");\n    print h;\n    $CURSOR_MARKER\n.end/container\n")
    )
}

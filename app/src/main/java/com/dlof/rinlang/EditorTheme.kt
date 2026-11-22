package com.dlof.rinlang

/**
 * نموذج ثيم المحرر (ألوان الكود) — منطق خالص بلا اعتماد على أندرويد، فهو قابل للاختبار مباشرة.
 *
 * الفكرة: ثلاثون-ونيّف نوع تلوين ([HighlightKind]) تُجمَّع في **مجموعات تصميم** قليلة يفهمها المستخدم
 * (كلمات مفتاحية، نصوص، أرقام، تعليقات، دوال، رموز، أقواس…)، وكل مجموعة لها لون واحد. الثيم = خريطة
 * مجموعة → لون (+ ألوان السطح: الخلفية، النص، السطر الحالي، التحديد، دليل الصفحة).
 *
 * غياب مجموعة من الخريطة يعني **لون التطبيق الأصلي** (من colors.xml ويتبدّل مع الوضع الليلي)،
 * فالثيم "الافتراضي" هو خريطة فارغة، وأي تخصيص جزئي يبقى باقي الألوان على أصله.
 *
 * التخزين نصي مضغوط: `key=#RRGGBB;key=#RRGGBB` (انظر [serialize] / [parse]).
 */
object EditorTheme {

    /** مجموعة تصميم: [key] ثابت للتخزين، و[kinds] كل أنواع [HighlightKind] التي تتلوّن بلونها. */
    class Group(val key: String, val kinds: IntArray)

    val GROUPS: List<Group> = listOf(
        Group("keyword", intArrayOf(HighlightKind.KEYWORD, HighlightKind.TEMPLATE_KW, HighlightKind.IX_STRUCT)),
        Group("control", intArrayOf(HighlightKind.CONTROL)),
        Group("type", intArrayOf(HighlightKind.TYPE, HighlightKind.CLASS)),
        Group("function", intArrayOf(HighlightKind.CALL, HighlightKind.FUNCTION_DEF, HighlightKind.TEMPLATE, HighlightKind.IX_EVENT)),
        Group("string", intArrayOf(HighlightKind.STRING, HighlightKind.PK_STRING)),
        Group("number", intArrayOf(HighlightKind.NUMBER, HighlightKind.IX_LENGTH, HighlightKind.IX_COLOR)),
        Group("comment", intArrayOf(HighlightKind.COMMENT, HighlightKind.PK_COMMENT)),
        Group("constant", intArrayOf(HighlightKind.CONSTANT, HighlightKind.IX_TOKEN)),
        Group("property", intArrayOf(HighlightKind.PROPERTY, HighlightKind.PK_ATTR, HighlightKind.IX_ATTR)),
        Group("variable", intArrayOf(HighlightKind.PARAMETER, HighlightKind.PK_VARIABLE, HighlightKind.IX_STATE)),
        Group("modifier", intArrayOf(HighlightKind.MODIFIER, HighlightKind.LABEL)),
        Group("tag", intArrayOf(HighlightKind.AT, HighlightKind.PREPROCESSOR, HighlightKind.PK_TAG,
            HighlightKind.PK_CUSTOM_TAG, HighlightKind.IX_TAG, HighlightKind.IX_TAG_CUSTOM, HighlightKind.IX_ID)),
        Group("operator", intArrayOf(HighlightKind.OPERATOR, HighlightKind.COMPARE, HighlightKind.FLOW)),
        Group("punct", intArrayOf(HighlightKind.PUNCT, HighlightKind.PK_BRACKET, HighlightKind.IX_BRACKET, HighlightKind.IX_BRACE)),
        Group("bracket0", intArrayOf(HighlightKind.BRACKET0)),
        Group("bracket1", intArrayOf(HighlightKind.BRACKET1)),
        Group("bracket2", intArrayOf(HighlightKind.BRACKET2)),
        Group("error", intArrayOf(HighlightKind.ERROR))
    )

    /** مفاتيح ألوان السطح (ليست أنواع تلوين). */
    const val SURFACE_BG = "bg"
    const val SURFACE_TEXT = "text"
    const val SURFACE_LINE = "line"
    const val SURFACE_SELECTION = "selection"
    const val SURFACE_GUIDE = "guide"
    val SURFACES: List<String> = listOf(SURFACE_BG, SURFACE_TEXT, SURFACE_LINE, SURFACE_SELECTION, SURFACE_GUIDE)

    /** كل المفاتيح المسموحة (للتحقق عند القراءة). */
    private val ALL_KEYS: Set<String> by lazy { (GROUPS.map { it.key } + SURFACES).toSet() }

    /** أكبر قيمة نوع تلوين + 1 (لحجز مصفوفة البحث السريع). */
    private const val KIND_SPACE = 64

    // ------------------------------------------------------------------------------------
    // الثيمات الجاهزة
    // ------------------------------------------------------------------------------------

    class Preset(val id: String, val name: String, val dark: Boolean, val colors: Map<String, Int>)

    private fun p(
        id: String, name: String, dark: Boolean,
        bg: String, text: String, line: String, sel: String, guide: String,
        keyword: String, control: String, type: String, function: String, string: String, number: String,
        comment: String, constant: String, property: String, variable: String, modifier: String, tag: String,
        operator: String, punct: String, b0: String, b1: String, b2: String, error: String
    ): Preset {
        val m = LinkedHashMap<String, Int>()
        fun put(k: String, hex: String) { m[k] = hexToArgb(hex)!! }
        put(SURFACE_BG, bg); put(SURFACE_TEXT, text); put(SURFACE_LINE, line); put(SURFACE_SELECTION, sel); put(SURFACE_GUIDE, guide)
        put("keyword", keyword); put("control", control); put("type", type); put("function", function)
        put("string", string); put("number", number); put("comment", comment); put("constant", constant)
        put("property", property); put("variable", variable); put("modifier", modifier); put("tag", tag)
        put("operator", operator); put("punct", punct); put("bracket0", b0); put("bracket1", b1); put("bracket2", b2)
        put("error", error)
        return Preset(id, name, dark, m)
    }

    val PRESETS: List<Preset> = listOf(
        p("rin_night", "Rin Night", true,
            "#14161B", "#E3E5E8", "#1E222B", "#334D7EFF", "#2B303B",
            "#5C9DFF", "#F06292", "#B98CFF", "#E8C86A", "#7BD88F", "#F2A65A",
            "#6B7280", "#4DC9E6", "#8CB4FF", "#D9A6FF", "#9AA7D8", "#E8B23D",
            "#58C2D6", "#8A93A3", "#FFD166", "#C77DFF", "#4CC9F0", "#F14C4C"),
        p("monokai", "Monokai", true,
            "#272822", "#F8F8F2", "#3E3D32", "#49483E", "#3B3A32",
            "#F92672", "#F92672", "#66D9EF", "#A6E22E", "#E6DB74", "#AE81FF",
            "#75715E", "#AE81FF", "#FD971F", "#FD971F", "#66D9EF", "#F92672",
            "#F92672", "#A59F85", "#E6DB74", "#AE81FF", "#66D9EF", "#F92672"),
        p("dracula", "Dracula", true,
            "#282A36", "#F8F8F2", "#343746", "#44475A", "#3A3C4E",
            "#FF79C6", "#FF79C6", "#8BE9FD", "#50FA7B", "#F1FA8C", "#BD93F9",
            "#6272A4", "#BD93F9", "#FFB86C", "#FFB86C", "#8BE9FD", "#FF79C6",
            "#FF79C6", "#98A0BF", "#F1FA8C", "#BD93F9", "#8BE9FD", "#FF5555"),
        p("one_dark", "One Dark", true,
            "#282C34", "#ABB2BF", "#2C313C", "#3E4451", "#353B45",
            "#C678DD", "#C678DD", "#E5C07B", "#61AFEF", "#98C379", "#D19A66",
            "#5C6370", "#D19A66", "#E06C75", "#E06C75", "#C678DD", "#E06C75",
            "#56B6C2", "#7F848E", "#E5C07B", "#C678DD", "#61AFEF", "#E06C75"),
        p("nord", "Nord", true,
            "#2E3440", "#D8DEE9", "#3B4252", "#434C5E", "#3B4252",
            "#81A1C1", "#81A1C1", "#8FBCBB", "#88C0D0", "#A3BE8C", "#B48EAD",
            "#616E88", "#B48EAD", "#8FBCBB", "#D8DEE9", "#81A1C1", "#D08770",
            "#81A1C1", "#7B88A1", "#EBCB8B", "#B48EAD", "#88C0D0", "#BF616A"),
        p("solarized_dark", "Solarized Dark", true,
            "#002B36", "#93A1A1", "#073642", "#0A4A5B", "#0A3B47",
            "#859900", "#CB4B16", "#B58900", "#268BD2", "#2AA198", "#D33682",
            "#586E75", "#6C71C4", "#268BD2", "#B58900", "#859900", "#CB4B16",
            "#859900", "#657B83", "#B58900", "#D33682", "#268BD2", "#DC322F"),
        p("github_light", "GitHub Light", false,
            "#FFFFFF", "#24292F", "#F6F8FA", "#BBDFFF", "#E4E7EB",
            "#CF222E", "#CF222E", "#953800", "#8250DF", "#0A3069", "#0550AE",
            "#6E7781", "#0550AE", "#0550AE", "#953800", "#CF222E", "#116329",
            "#CF222E", "#57606A", "#BF8700", "#8250DF", "#0969DA", "#CF222E"),
        p("solarized_light", "Solarized Light", false,
            "#FDF6E3", "#586E75", "#EEE8D5", "#D3CBB7", "#E6DFC8",
            "#859900", "#CB4B16", "#B58900", "#268BD2", "#2AA198", "#D33682",
            "#93A1A1", "#6C71C4", "#268BD2", "#B58900", "#859900", "#CB4B16",
            "#859900", "#839496", "#B58900", "#D33682", "#268BD2", "#DC322F"),
        p("paper", "Paper", false,
            "#F7F5F0", "#2B2B2B", "#EFEBE0", "#CFE3F7", "#E3DED2",
            "#7A3E9D", "#C2185B", "#006B6B", "#7A5C00", "#2E7D32", "#B25E00",
            "#8A8578", "#00695C", "#1F5FBF", "#8A4B08", "#6A1B9A", "#A15C00",
            "#00697A", "#8A8578", "#B7791F", "#A23DAD", "#1976D2", "#C62828")
    )

    fun preset(id: String): Preset? = PRESETS.firstOrNull { it.id == id }

    // ------------------------------------------------------------------------------------
    // التحويل والتخزين
    // ------------------------------------------------------------------------------------

    /** `#RGB` أو `#RRGGBB` أو `#AARRGGBB` → ARGB (يفرض alpha=FF لو لم يُذكر)، أو null إن كان غير صالح. */
    fun hexToArgb(input: String): Int? {
        var h = input.trim().removePrefix("#")
        if (h.length == 3) h = "" + h[0] + h[0] + h[1] + h[1] + h[2] + h[2]
        if (h.length != 6 && h.length != 8) return null
        if (!h.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) return null
        val v = h.toLong(16)
        return if (h.length == 6) (0xFF000000L or v).toInt() else v.toInt()
    }

    /** ARGB → `#RRGGBB` (أو `#AARRGGBB` إن لم يكن معتماً كلياً). */
    fun argbToHex(argb: Int): String {
        val a = (argb ushr 24) and 0xFF
        return if (a == 0xFF) String.format("#%06X", argb and 0xFFFFFF) else String.format("#%08X", argb)
    }

    /** يقرأ `key=#RRGGBB;…`؛ يتجاهل المفاتيح المجهولة والقيم غير الصالحة. */
    fun parse(s: String?): LinkedHashMap<String, Int> {
        val out = LinkedHashMap<String, Int>()
        if (s.isNullOrBlank()) return out
        for (part in s.split(';')) {
            val i = part.indexOf('=')
            if (i <= 0) continue
            val k = part.substring(0, i).trim()
            if (k !in ALL_KEYS) continue
            val c = hexToArgb(part.substring(i + 1)) ?: continue
            out[k] = c
        }
        return out
    }

    fun serialize(colors: Map<String, Int>): String =
        colors.entries.filter { it.key in ALL_KEYS }.joinToString(";") { it.key + "=" + argbToHex(it.value) }

    // ------------------------------------------------------------------------------------
    // الاستعلام السريع أثناء الرسم
    // ------------------------------------------------------------------------------------

    /**
     * مصفوفة بحث: الفهرس = [HighlightKind]، والقيمة = لون الثيم أو 0 (لا تجاوز → لون التطبيق الأصلي).
     * تُبنى مرة عند تغيّر الثيم، ثم يقرأها المحرر في كل رسم بلا أي تخصيص ذاكرة.
     */
    fun kindColors(colors: Map<String, Int>): IntArray {
        val arr = IntArray(KIND_SPACE)
        for (g in GROUPS) {
            val c = colors[g.key] ?: continue
            for (k in g.kinds) if (k in arr.indices) arr[k] = c
        }
        return arr
    }

    /** لون التصميم الحالي لمجموعة/سطح، أو [fallback]. */
    fun colorOr(colors: Map<String, Int>, key: String, fallback: Int): Int = colors[key] ?: fallback

    /** يرجع لون نص مقروء (أسود/أبيض) فوق [bg] — لشارات اللون في المصمّم. */
    fun readableOn(bg: Int): Int {
        val r = (bg shr 16) and 0xFF; val g = (bg shr 8) and 0xFF; val b = bg and 0xFF
        val lum = 0.299 * r + 0.587 * g + 0.114 * b
        return if (lum > 150) 0xFF000000.toInt() else 0xFFFFFFFF.toInt()
    }

    // ------------------------------------------------------------------------------------
    // نماذج المعاينة (نصوص قصيرة تُظهر أغلب المجموعات)
    // ------------------------------------------------------------------------------------

    class Sample(val ext: String, val label: String, val code: String)

    val SAMPLES: List<Sample> = listOf(
        Sample("rin", "Rin", """
            // Rin: تجميع وعرض
            let items = [3, 7.5, 12]
            fun total(list, tax) {
                let sum = 0
                for (n in list) { sum = sum + n }
                if (sum >= 10 and tax != nil) { return sum * (1 + tax) }
                return sum |> round
            }
            print("إجمالي:", total(items, 0.15))
            """.trimIndent()),
        Sample("kt", "Kotlin", """
            @Composable
            fun Greeting(name: String = "Rin"): Int {
                val n = 0x1F + 3 // عدّاد
                return if (n >= 2 && name != null) n else -1
            }
            class Box<T>(val value: T) { companion object }
            """.trimIndent()),
        Sample("py", "Python", """
            @cached
            def run(self, x: int = 3) -> dict:
                '''وثيقة'''
                data = {'a': [1, 2.5e3], "ok": True}
                return data  # done
            class Job(Base): pass
            """.trimIndent()),
        Sample("cpp", "C++", """
            #include <vector>
            #define LIMIT 10
            template <class T>
            static int count(const std::vector<T>& v) {
                /* عدّ العناصر */
                return v.size() <= LIMIT ? 1 : 0;
            }
            """.trimIndent()),
        Sample("js", "JS / TS", """
            const add = async (a, b) => { return `sum: ${'$'}{a + b}`; };
            class Store extends Base { static count = 0 }
            let y = obj?.value ?? null;
            """.trimIndent()),
        Sample("html", "HTML", """
            <!DOCTYPE html>
            <div class="card" id='main'>Hi &amp; <Item size=3/></div>
            <!-- ملاحظة -->
            """.trimIndent()),
        Sample("css", "CSS", """
            .card:hover, #main > a {
              color: #1a2b3c; margin: -4px 0 1.5em;
            }
            @media (max-width: 600px) { body { --gap: 3; } }
            """.trimIndent()),
        Sample("json", "JSON", """
            {
              "name": "rin", "version": 2,
              "tags": ["a", "b"], "ok": true, "next": null
            }
            """.trimIndent())
    )
}

package com.dlof.rinlang

/**
 * شعار Rin الرسمي ولافتات المكتبات لطرفية RinStudio — نص بأكواد ANSI truecolor يرسمه [RinTerminalView]
 * عبر [RinAnsi] (منطق خالص بلا أندرويد فيُختبر مباشرة).
 *
 * التصميم: حروف RIN بخط «ANSI Shadow» (كتل █ مع ظلّ ╗║╝) بتدرّج أخضر #0FA34F ← نعناعي #7BF5A8
 * يتدرّج أفقياً وعمودياً، والظلّ بدرجة أخفت من الجسم فيبدو الحرف مجسَّماً. تحته خط فاصل وسطر هوية
 * وسطر أوامر. لافتة المكتبة: إطار مستدير باسم المكتبة بحروف مباعدة بتدرّج، وسطر الفئة، والوصف
 * ملفوفاً، وأبرز الدوال، وسطر الاستيراد الجاهز للنسخ.
 */
object RinBanner {

    private const val ESC = "\u001b"
    private const val RESET = "$ESC[0m"

    private var FROM = intArrayOf(0x0F, 0xA3, 0x4F)   // أخضر Rin
    private var TO = intArrayOf(0x7B, 0xF5, 0xA8)     // أخضر نعناعي فاتح

    /** يغيّر تدرّج الشعار واللافتات (RGB بلا ألفا) — تستدعيه الطرفية عند تغيير السمة. */
    fun setGradient(from: Int, to: Int) {
        FROM = intArrayOf((from shr 16) and 255, (from shr 8) and 255, from and 255)
        TO = intArrayOf((to shr 16) and 255, (to shr 8) and 255, to and 255)
    }
    const val INNER = 40
    private const val DIM = "$ESC[38;2;139;148;158m"
    private const val SOFT = "$ESC[38;2;201;209;217m"

    /** حروف RIN (6 أسطر × 21 عموداً). */
    val LOGO_ROWS = listOf(
        "██████╗ ██╗███╗   ██╗",
        "██╔══██╗██║████╗  ██║",
        "██████╔╝██║██╔██╗ ██║",
        "██╔══██╗██║██║╚██╗██║",
        "██║  ██║██║██║ ╚████║",
        "╚═╝  ╚═╝╚═╝╚═╝  ╚═══╝"
    )

    private fun lerp(a: Int, b: Int, t: Double) = (a + (b - a) * t).toInt().coerceIn(0, 255)

    /** لون التدرّج عند الإحداثي النسبي t∈[0,1]، بسطوع [k] (1 = كامل). */
    private fun grad(t: Double, k: Double = 1.0): String {
        val r = (lerp(FROM[0], TO[0], t) * k).toInt()
        val g = (lerp(FROM[1], TO[1], t) * k).toInt()
        val b = (lerp(FROM[2], TO[2], t) * k).toInt()
        return "$ESC[38;2;$r;$g;${b}m"
    }

    /** يلوّن سطراً من الشعار: الكتلة █ بلون كامل، وما سواها (الظلّ) بـ 55% من السطوع. */
    private fun paintLogoRow(row: String, rowIdx: Int, rows: Int): String {
        val sb = StringBuilder()
        val w = row.length
        var last = ""
        for ((i, ch) in row.withIndex()) {
            if (ch == ' ') { sb.append(' '); continue }
            val t = ((i.toDouble() / (w - 1)) * 0.75 + (rowIdx.toDouble() / (rows - 1)) * 0.25).coerceIn(0.0, 1.0)
            val code = grad(t, if (ch == '█') 1.0 else 0.55)
            if (code != last) { sb.append(code); last = code }
            sb.append(ch)
        }
        return sb.append(RESET).toString()
    }

    /**
     * شعار الترحيب. [version] نص إصدار المحرك (أو null)، [tagline] سطر الهوية، [hint] سطر الأوامر.
     * كل عنصر في القائمة الناتجة سطر مستقل.
     */
    fun logo(version: String? = null, tagline: String = "Rin Programming Language", hint: String = "help  ·  libs  ·  lib <name>", info: List<Pair<String, String>> = emptyList()): List<String> {
        val out = ArrayList<String>()
        val bar = "─".repeat(INNER + 2)
        fun row(painted: String, plainLen: Int) {
            val total = INNER - plainLen
            val l = (total / 2).coerceAtLeast(0); val r = (total - l).coerceAtLeast(0)
            out.add("$DIM│$RESET ${" ".repeat(l)}$painted${" ".repeat(r)} $DIM│$RESET")
        }
        out.add("$DIM╭$bar╮$RESET")
        row("", 0)
        for ((i, r) in LOGO_ROWS.withIndex()) row(paintLogoRow(r, i, LOGO_ROWS.size), r.length)
        row("", 0)
        val ruleW = 28
        val rule = StringBuilder()
        for (i in 0 until ruleW) rule.append(grad(i.toDouble() / (ruleW - 1), 0.8)).append('━')
        row(rule.append(RESET).toString(), ruleW)
        row("$SOFT$tagline$RESET", tagline.length)
        if (info.isNotEmpty()) {
            out.add("$DIM├$bar┤$RESET")
            val lw = info.maxOf { it.first.length } + 2
            for ((k, v) in info) {
                val value = if (lw + v.length > INNER) v.take(INNER - lw - 1) + "…" else v
                val padK = k.padEnd(lw)
                out.add("$DIM│$RESET $DIM$padK$RESET${grad(0.55)}$value$RESET${" ".repeat((INNER - lw - value.length).coerceAtLeast(0))} $DIM│$RESET")
            }
        } else {
            val ver = version?.trim()?.takeIf { it.isNotEmpty() }
            val id = "RinStudio" + if (ver != null) "  ·  $ver" else ""
            row("${grad(0.0)}$id$RESET", id.length)
        }
        out.add("$DIM├$bar┤$RESET")
        row("$DIM$hint$RESET", hint.length)
        out.add("$DIM╰$bar╯$RESET")
        return out
    }

    /** يستخرج رقم الإصدار من نص المحرّك ("Rin Engine 1.0.0 (C++17) — ...") أو null. */
    fun parseVersion(engineString: String?): String? =
        engineString?.let { Regex("""\d+\.\d+(?:\.\d+)?(?:[-+][0-9A-Za-z.]+)?""").find(it)?.value }

    /** الشعار كنص واحد مفصول بأسطر. */
    fun logoText(version: String? = null, tagline: String = "Rin Programming Language", hint: String = "help  ·  libs  ·  lib <name>", info: List<Pair<String, String>> = emptyList()): String =
        logo(version, tagline, hint, info).joinToString("\n")

    // ------------------------------------------------------------------ لافتة المكتبة

    /** الاسم بحروف كبيرة مباعدة (`m a t h` → `M A T H`)، الشرطة تبقى. */
    fun spaced(name: String): String = name.uppercase().map { if (it == ' ') "  " else it.toString() }.joinToString(" ")

    /** يلفّ [text] على كلمات بعرض لا يتجاوز [width] خلية (كل المحارف تُعدّ بعرض 1، فالعربية تُلفّ بالكلمات أيضاً). */
    fun wrap(text: String, width: Int): List<String> {
        val lines = ArrayList<String>(); val cur = StringBuilder()
        for (w in text.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }) {
            if (cur.isNotEmpty() && cur.length + 1 + w.length > width) { lines.add(cur.toString()); cur.setLength(0) }
            if (cur.isNotEmpty()) cur.append(' ')
            cur.append(w)
        }
        if (cur.isNotEmpty()) lines.add(cur.toString())
        return lines
    }

    /**
     * لافتة مكتبة: [name] اسم العرض، [category] نص الفئة، [description] وصف، [functions] أبرز الدوال
     * (مفصولة بـ •)، [importPath] مسار الاستيراد. العرض الداخلي ثابت [INNER] فتبقى الأطر متراصفة.
     */
    fun library(name: String, category: String, description: String, functions: String, importPath: String, info: List<Pair<String, String>> = emptyList()): List<String> {
        val bar = "─".repeat(INNER + 2)
        val out = ArrayList<String>()
        fun line(content: String, plainLen: Int) {
            out.add("$DIM│$RESET $content${" ".repeat((INNER - plainLen).coerceAtLeast(0))} $DIM│$RESET")
        }
        out.add("$DIM╭$bar╮$RESET")
        // الترويسة: RIN ▸ LIB على اليسار والفئة على اليمين
        val tag = "RIN ▸ LIB"
        val cat = category
        val gap = (INNER - tag.length - cat.length).coerceAtLeast(1)
        line("${grad(0.0)}${tag}$RESET${" ".repeat(gap)}$DIM$cat$RESET", tag.length + gap + cat.length)
        // اسم المكتبة بتدرّج (يُقصّ إن طال)
        var sp = spaced(name)
        if (sp.length > INNER) sp = sp.substring(0, INNER - 1) + "…"
        val colored = StringBuilder()
        for ((i, ch) in sp.withIndex()) colored.append(grad(i.toDouble() / maxOf(1, sp.length - 1))).append(ch)
        line("$ESC[1m$colored$RESET", sp.length)
        out.add("$DIM├$bar┤$RESET")
        for (l in wrap(description, INNER)) line("$SOFT$l$RESET", l.length)
        val fns = functions.trim()
        if (fns.isNotEmpty()) {
            line("", 0)
            for (l in wrap(fns, INNER)) line("$DIM$l$RESET", l.length)
        }
        if (info.isNotEmpty()) {
            out.add("$DIM├$bar┤$RESET")
            val lw = info.maxOf { it.first.length } + 2
            for ((k, v) in info) {
                val value = if (lw + v.length > INNER) v.take(INNER - lw - 1) + "…" else v
                out.add("$DIM│$RESET $DIM${k.padEnd(lw)}$RESET${grad(0.55)}$value$RESET${" ".repeat((INNER - lw - value.length).coerceAtLeast(0))} $DIM│$RESET")
            }
        }
        out.add("$DIM├$bar┤$RESET")
        val imp = "@import \"$importPath\";"
        val impShown = if (imp.length > INNER) imp.substring(0, INNER - 1) + "…" else imp
        line("${grad(1.0)}$impShown$RESET", impShown.length)
        out.add("$DIM╰$bar╯$RESET")
        return out
    }


    /**
     * شاشة مساعدة منسّقة. [rows]: أسطر `#عنوان القسم` و`الأمر::الوصف`؛ [footer]: ملاحظات أسفلها (سطر لكل ملاحظة).
     * أسماء الأوامر بعمود ثابت ملوّن والوصف ملفوف تحته بمحاذاة العمود.
     */
    fun help(rows: String, footer: String, title: String = "RIN ▸ HELP"): List<String> {
        val out = ArrayList<String>()
        out.add("${grad(0.0)}$ESC[1m$title$RESET")
        val rule = StringBuilder()
        for (i in 0 until 36) rule.append(grad(i / 35.0, 0.7)).append('━')
        out.add(rule.append(RESET).toString())
        val col = 14
        val descW = 32
        val sections = rows.lines().map { it.trim() }.filter { it.isNotEmpty() }
        val titles = sections.count { it.startsWith("#") }.coerceAtLeast(1)
        var si = 0
        for (line in sections) {
            if (line.startsWith("#")) {
                out.add("")
                out.add("${grad(si / maxOf(1, titles - 1).toDouble())}▍${line.drop(1).trim()}$RESET")
                si++
                continue
            }
            val parts = line.split("::", limit = 2)
            val name = parts[0].trim()
            val desc = parts.getOrElse(1) { "" }.trim()
            val wrapped = wrap(desc, descW).ifEmpty { listOf("") }
            val head = if (name.length >= col) name + " " else name.padEnd(col)
            out.add("  ${grad(0.45)}$head$RESET$SOFT${wrapped[0]}$RESET")
            for (w in wrapped.drop(1)) out.add("  ${" ".repeat(maxOf(col, head.length))}$SOFT$w$RESET")
        }
        val notes = footer.lines().map { it.trim() }.filter { it.isNotEmpty() }
        if (notes.isNotEmpty()) {
            out.add("")
            for (n in notes) for (w in wrap(n, 46)) out.add("$DIM$w$RESET")
        }
        return out
    }

    /** عيّنة ألوان وأنماط ANSI لاختبار دعم الطرفية (أمر `colors`). */
    fun palette(): List<String> {
        val out = ArrayList<String>()
        out.add("${DIM}16 colors$RESET")
        out.add((30..37).joinToString("") { "$ESC[${it}m██$RESET" } + " " + (90..97).joinToString("") { "$ESC[${it}m██$RESET" })
        out.add("${DIM}256 colors$RESET")
        out.add((16..51).joinToString("") { "$ESC[38;5;${it}m█$RESET" })
        out.add((52..87).joinToString("") { "$ESC[38;5;${it}m█$RESET" })
        out.add("${DIM}truecolor$RESET")
        val w = 36
        out.add((0 until w).joinToString("") { grad(it.toDouble() / (w - 1)) + "█" } + RESET)
        out.add("${DIM}styles$RESET")
        out.add("$ESC[1mbold$RESET  $ESC[3mitalic$RESET  $ESC[4munderline$RESET  $ESC[7mreverse$RESET".replace("$ESC[7mreverse$RESET", "${grad(0.5)}accent$RESET"))
        return out
    }

    /** قائمة المكتبات مجمّعة: كل فئة بعنوان ملوّن ثم أسماؤها بأعمدة تُلفّ على [width]. */
    fun libraryIndex(groups: List<Pair<String, List<String>>>, width: Int = 40): List<String> {
        val out = ArrayList<String>()
        val total = groups.sumOf { it.second.size }
        out.add("${grad(0.0)}◆$RESET $SOFT$total libraries$RESET")
        for ((gi, g) in groups.withIndex()) {
            if (g.second.isEmpty()) continue
            out.add("")
            out.add("${grad(gi.toDouble() / maxOf(1, groups.size - 1))}▍${g.first}$RESET$DIM  (${g.second.size})$RESET")
            val cur = StringBuilder("  ")
            for (n in g.second) {
                if (cur.length > 2 && cur.length + 2 + n.length > width) { out.add("$SOFT$cur$RESET"); cur.setLength(0); cur.append("  ") }
                cur.append(n).append("  ")
            }
            if (cur.length > 2) out.add("$SOFT${cur.toString().trimEnd()}$RESET")
        }
        out.add("")
        out.add("${DIM}lib <name>  ·  @import \"lib/<name>.og.rin\";$RESET")
        return out
    }
}

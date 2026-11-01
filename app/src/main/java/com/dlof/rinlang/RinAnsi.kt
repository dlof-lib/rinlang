package com.dlof.rinlang

/**
 * مقطع نص واحد بنمط ثابت، ناتج عن تحليل تسلسلات ANSI SGR (`ESC [ ... m`).
 * [fg] و[bg] بصيغة ARGB؛ `null` يعني «اللون الافتراضي للـ terminal».
 */
data class AnsiRun(
    val text: String,
    val fg: Int? = null,
    val bg: Int? = null,
    val bold: Boolean = false,
    val italic: Boolean = false,
    val underline: Boolean = false
)

/**
 * يحوّل مخرجات المحرك التي فيها أكواد ANSI (مثل ناتج `colorAnsi(text, color)`) إلى مقاطع ملوّنة
 * يرسمها [RinTerminalView]، فيظهر Unicode (عربي/لاتيني/رموز) بألوانه الحقيقية بدل أكواد خام.
 *
 * منطق خالص بلا Android فيُختبر بـ JUnit. المدعوم: إعادة الضبط `0`، غامق `1`، مائل `3`، تسطير `4`
 * وإيقافها (`22`/`23`/`24`)، ألوان 16 (`30–37`/`90–97` للنص و`40–47`/`100–107` للخلفية)، و256 لوناً
 * (`38;5;n`) وtruecolor (`38;2;r;g;b`)، و`39`/`49` للعودة للافتراضي.
 *
 * أمان: كل ما عدا SGR (حركة المؤشر، مسح الشاشة، OSC، أحرف التحكم C0/C1) يُسقَط، فلا يستطيع نص
 * قادم من صورة أو ملف أو شبكة أن يعبث بالشاشة؛ يبقى `\t` فقط. ألوان الأسطر مستقلة (تُعاد
 * الحالة مع كل سطر جديد في [TermLine])، وهو سلوك أي سطر مُنتهٍ بـ `ESC[0m`.
 */
object RinAnsi {

    private const val ESC = '\u001b'

    /** أول 16 لوناً بدرجات تقرأ جيداً على خلفية الـ terminal الداكنة. */
    private val BASIC = intArrayOf(
        0xFF484F58.toInt(), 0xFFFF7B72.toInt(), 0xFF3FB950.toInt(), 0xFFD29922.toInt(),
        0xFF58A6FF.toInt(), 0xFFBC8CFF.toInt(), 0xFF39C5CF.toInt(), 0xFFB1BAC4.toInt(),
        0xFF6E7681.toInt(), 0xFFFFA198.toInt(), 0xFF56D364.toInt(), 0xFFE3B341.toInt(),
        0xFF79C0FF.toInt(), 0xFFD2A8FF.toInt(), 0xFF56D4DD.toInt(), 0xFFF0F6FC.toInt()
    )

    /** النص بلا أي تسلسل تحكم (للنسخ والبحث والتصنيف). */
    fun strip(src: String): String = parse(src).joinToString("") { it.text }

    fun parse(src: String): List<AnsiRun> {
        if (src.isEmpty()) return emptyList()
        val runs = ArrayList<AnsiRun>()
        val text = StringBuilder()
        var style = AnsiRun("")

        fun flush() {
            if (text.isEmpty()) return
            runs.add(style.copy(text = text.toString()))
            text.setLength(0)
        }

        var i = 0
        while (i < src.length) {
            val c = src[i]
            when {
                c == ESC -> {
                    val end = skipSequence(src, i)
                    val params = sgrParams(src, i, end)
                    if (params != null) {
                        flush()
                        style = applySgr(style, params)
                    }
                    i = end
                    continue
                }
                c == '\t' || (c >= ' ' && c != '\u007f' && c !in '\u0080'..'\u009f') -> text.append(c)
            }
            i++
        }
        flush()
        return runs
    }

    /** فهرس ما بعد التسلسل الذي يبدأ بـ ESC عند [start] (CSI أو OSC أو ESC + حرف). */
    private fun skipSequence(s: String, start: Int): Int {
        var i = start + 1
        if (i >= s.length) return i
        when (s[i]) {
            '[' -> {                                             // CSI: معاملات 0x30–0x3F ثم وسطاء ثم حرف نهائي 0x40–0x7E
                i++
                while (i < s.length && s[i] in ' '..'?') i++
                return if (i < s.length) i + 1 else i
            }
            ']' -> {                                             // OSC: حتى BEL أو ESC \
                i++
                while (i < s.length) {
                    if (s[i] == '\u0007') return i + 1
                    if (s[i] == ESC && i + 1 < s.length && s[i + 1] == '\\') return i + 2
                    i++
                }
                return i
            }
            else -> return i + 1
        }
    }

    /** معاملات SGR إن كان التسلسل `ESC [ ... m`، وإلا `null`. فارغة تعني `0`. */
    private fun sgrParams(s: String, start: Int, end: Int): List<Int>? {
        if (end - start < 3 || s[start + 1] != '[' || s[end - 1] != 'm') return null
        val body = s.substring(start + 2, end - 1)
        if (body.any { it != ';' && it != ':' && it !in '0'..'9' }) return null
        if (body.isEmpty()) return listOf(0)
        return body.split(';', ':').map { it.toIntOrNull() ?: 0 }
    }

    private fun applySgr(from: AnsiRun, p: List<Int>): AnsiRun {
        var s = from
        var k = 0
        while (k < p.size) {
            when (val n = p[k]) {
                0 -> s = AnsiRun("")
                1 -> s = s.copy(bold = true)
                3 -> s = s.copy(italic = true)
                4 -> s = s.copy(underline = true)
                22 -> s = s.copy(bold = false)
                23 -> s = s.copy(italic = false)
                24 -> s = s.copy(underline = false)
                in 30..37 -> s = s.copy(fg = BASIC[n - 30])
                in 90..97 -> s = s.copy(fg = BASIC[n - 90 + 8])
                in 40..47 -> s = s.copy(bg = BASIC[n - 40])
                in 100..107 -> s = s.copy(bg = BASIC[n - 100 + 8])
                39 -> s = s.copy(fg = null)
                49 -> s = s.copy(bg = null)
                38, 48 -> {
                    val (color, used) = extendedColor(p, k + 1)
                    if (color != null) s = if (n == 38) s.copy(fg = color) else s.copy(bg = color)
                    k += used
                }
            }
            k++
        }
        return s
    }

    /** `5;n` أو `2;r;g;b` بعد 38/48؛ يعيد اللون وعدد المعاملات المستهلكة. */
    private fun extendedColor(p: List<Int>, at: Int): Pair<Int?, Int> {
        return when (p.getOrNull(at)) {
            5 -> p.getOrNull(at + 1)?.let { palette256(it) } to 2
            2 -> if (at + 3 < p.size) {
                rgb(p[at + 1], p[at + 2], p[at + 3]) to 4
            } else null to (p.size - at)
            else -> null to 0
        }
    }

    private fun palette256(n: Int): Int? = when {
        n !in 0..255 -> null
        n < 16 -> BASIC[n]
        n < 232 -> {
            val v = n - 16
            val level = { x: Int -> if (x == 0) 0 else 55 + x * 40 }
            rgb(level(v / 36), level((v / 6) % 6), level(v % 6))
        }
        else -> (8 + (n - 232) * 10).let { rgb(it, it, it) }
    }

    private fun rgb(r: Int, g: Int, b: Int): Int =
        (0xFF shl 24) or (r.coerceIn(0, 255) shl 16) or (g.coerceIn(0, 255) shl 8) or b.coerceIn(0, 255)
}

package com.dlof.rinlang

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/**
 * كشف أكواد الألوان داخل سطر نصّي وتحليلها وإعادة كتابتها — منطق خالص بلا أي اعتماد على أندرويد
 * (قابل للاختبار مباشرة). يطابق الصيغ التي يقبلها محرّك الألوان في Rin (انظر lib/colors.og.rin):
 *
 *  - `#rgb` `#rgba` `#rrggbb` `#rrggbbaa`   (الشفافية في **آخر** الرقم كما في CSS)
 *  - `rgb(r,g,b)` `rgba(r,g,b,a)` وكذلك الصيغة الحديثة `rgb(255 0 0 / 50%)`
 *  - `hsl(h,s%,l%)` `hsla(h,s%,l%,a)`
 *  - اسم CSS (tomato, steelblue…) لكن **فقط** حين يكون محتوى نص بين علامتي اقتباس بالكامل ("tomato")
 *    حتى لا تُلوَّن كلمة red في نص عادي.
 *
 * كل القيم الناتجة بصيغة أندرويد ARGB (Int) جاهزة لـ Paint/Canvas.
 */
object ColorLiterals {

    enum class Format { HEX3, HEX4, HEX6, HEX8, RGB, RGBA, HSL, HSLA, NAME }

    /** [start, end) نطاق الكود داخل السطر (للاسم: داخل علامتي الاقتباس دون العلامتين). */
    data class Match(val start: Int, val end: Int, val argb: Int, val format: Format, val upper: Boolean = false) {
        val hasAlpha: Boolean get() = (argb ushr 24) != 255
    }

    // ------------------------------------------------------------------ الكشف

    /** يعيد كل الألوان في [line] مرتبة بموضعها، بلا تداخل. */
    fun scan(line: String): List<Match> {
        if (line.length < 4) return emptyList()
        var out: ArrayList<Match>? = null
        var i = 0
        val n = line.length
        while (i < n) {
            val c = line[i]
            var m: Match? = null
            when {
                c == '#' -> m = scanHex(line, i)
                (c == 'r' || c == 'R' || c == 'h' || c == 'H') -> m = scanFunc(line, i)
                (c == '"' || c == '\'') -> m = scanQuotedName(line, i)
            }
            if (m != null) {
                if (out == null) out = ArrayList(2)
                out.add(m)
                i = m.end
            } else i++
        }
        return out ?: emptyList()
    }

    /** اللون الذي يشمل العمود [col] (أو يلاصقه من اليمين)، أو null. */
    fun matchAt(line: String, col: Int): Match? {
        for (m in scan(line)) if (col >= m.start && col <= m.end) return m
        return null
    }

    private fun isWordChar(c: Char) = c.isLetterOrDigit() || c == '_'
    private fun isHexDigit(c: Char) = c in '0'..'9' || c in 'a'..'f' || c in 'A'..'F'

    private fun scanHex(s: String, at: Int): Match? {
        if (at > 0 && (isWordChar(s[at - 1]) || s[at - 1] == '&')) return null
        var e = at + 1
        while (e < s.length && isHexDigit(s[e])) e++
        val len = e - at - 1
        if (len != 3 && len != 4 && len != 6 && len != 8) return null
        if (e < s.length && isWordChar(s[e])) return null
        val digits = s.substring(at + 1, e)
        val fmt = when (len) { 3 -> Format.HEX3; 4 -> Format.HEX4; 6 -> Format.HEX6; else -> Format.HEX8 }
        val argb = parseHexDigits(digits) ?: return null
        return Match(at, e, argb, fmt, upper = digits.any { it in 'A'..'F' })
    }

    private fun parseHexDigits(d: String): Int? {
        fun nib(c: Char): Int = Character.digit(c, 16)
        return when (d.length) {
            3 -> argb(255, nib(d[0]) * 17, nib(d[1]) * 17, nib(d[2]) * 17)
            4 -> argb(nib(d[3]) * 17, nib(d[0]) * 17, nib(d[1]) * 17, nib(d[2]) * 17)
            6 -> argb(255, nib(d[0]) * 16 + nib(d[1]), nib(d[2]) * 16 + nib(d[3]), nib(d[4]) * 16 + nib(d[5]))
            8 -> argb(nib(d[6]) * 16 + nib(d[7]), nib(d[0]) * 16 + nib(d[1]), nib(d[2]) * 16 + nib(d[3]), nib(d[4]) * 16 + nib(d[5]))
            else -> null
        }
    }

    private fun scanFunc(s: String, at: Int): Match? {
        if (at > 0 && (isWordChar(s[at - 1]) || s[at - 1] == '-' || s[at - 1] == '.')) return null
        val name: String
        val fmtBase: Boolean // true = rgb ، false = hsl
        when {
            s.startsWith("rgba(", at, ignoreCase = true) -> { name = "rgba("; fmtBase = true }
            s.startsWith("rgb(", at, ignoreCase = true) -> { name = "rgb("; fmtBase = true }
            s.startsWith("hsla(", at, ignoreCase = true) -> { name = "hsla("; fmtBase = false }
            s.startsWith("hsl(", at, ignoreCase = true) -> { name = "hsl("; fmtBase = false }
            else -> return null
        }
        val close = s.indexOf(')', at + name.length)
        if (close < 0) return null
        val args = s.substring(at + name.length, close)
        val tokens = args.split(',', ' ', '\t', '/').filter { it.isNotEmpty() }
        if (tokens.size != 3 && tokens.size != 4) return null
        val a = if (tokens.size == 4) parseAlpha(tokens[3]) ?: return null else 255
        val argb: Int
        val hasA = tokens.size == 4
        if (fmtBase) {
            val r = parseChannel(tokens[0]) ?: return null
            val g = parseChannel(tokens[1]) ?: return null
            val b = parseChannel(tokens[2]) ?: return null
            argb = argb(a, r, g, b)
        } else {
            val h = parseHue(tokens[0]) ?: return null
            val sat = parsePercent(tokens[1]) ?: return null
            val lig = parsePercent(tokens[2]) ?: return null
            argb = fromHsl(h, sat, lig, a)
        }
        val fmt = if (fmtBase) (if (hasA) Format.RGBA else Format.RGB) else (if (hasA) Format.HSLA else Format.HSL)
        return Match(at, close + 1, argb, fmt)
    }

    private fun parseChannel(t: String): Int? =
        if (t.endsWith("%")) t.dropLast(1).toDoubleOrNull()?.let { (it.coerceIn(0.0, 100.0) / 100.0 * 255).roundToInt() }
        else t.toDoubleOrNull()?.let { it.coerceIn(0.0, 255.0).roundToInt() }

    private fun parseAlpha(t: String): Int? =
        if (t.endsWith("%")) t.dropLast(1).toDoubleOrNull()?.let { (it.coerceIn(0.0, 100.0) / 100.0 * 255).roundToInt() }
        else t.toDoubleOrNull()?.let { (it.coerceIn(0.0, 1.0) * 255).roundToInt() }

    private fun parseHue(t: String): Double? =
        (if (t.endsWith("deg", ignoreCase = true)) t.dropLast(3) else t).toDoubleOrNull()?.let { ((it % 360) + 360) % 360 }

    private fun parsePercent(t: String): Double? =
        (if (t.endsWith("%")) t.dropLast(1) else t).toDoubleOrNull()?.coerceIn(0.0, 100.0)

    private fun scanQuotedName(s: String, at: Int): Match? {
        val q = s[at]
        val close = s.indexOf(q, at + 1)
        if (close < 0 || close - at - 1 > 24 || close == at + 1) return null
        val raw = s.substring(at + 1, close)
        val key = raw.trim().lowercase()
        val rgb = NAMED[key] ?: return null
        val lead = raw.length - raw.trimStart().length
        val start = at + 1 + lead
        return Match(start, start + key.length, rgb or (0xFF shl 24), Format.NAME)
    }

    /** اللون من نص كامل (hex/rgb/hsl/اسم)، أو null. */
    fun parse(text: String): Int? {
        val t = text.trim()
        if (t.isEmpty()) return null
        NAMED[t.lowercase()]?.let { return it or (0xFF shl 24) }
        val m = scan(t).firstOrNull() ?: return null
        return if (m.start == 0 && m.end == t.length) m.argb else null
    }

    // ------------------------------------------------------------------ الكتابة

    fun argb(a: Int, r: Int, g: Int, b: Int): Int =
        (a.coerceIn(0, 255) shl 24) or (r.coerceIn(0, 255) shl 16) or (g.coerceIn(0, 255) shl 8) or b.coerceIn(0, 255)

    fun alphaOf(argb: Int) = (argb ushr 24) and 0xFF
    fun redOf(argb: Int) = (argb shr 16) and 0xFF
    fun greenOf(argb: Int) = (argb shr 8) and 0xFF
    fun blueOf(argb: Int) = argb and 0xFF

    /** يكتب [argb] بالصيغة [fmt]. الصيغ التي لا تتّسع للقيمة تُرقّى تلقائياً (HEX3→HEX6، HEX6 مع شفافية→HEX8…). */
    fun format(argb: Int, fmt: Format, upper: Boolean = false): String {
        val a = alphaOf(argb); val r = redOf(argb); val g = greenOf(argb); val b = blueOf(argb)
        val opaque = a == 255
        fun hex2(v: Int) = (if (upper) "%02X" else "%02x").format(v)
        fun short(v: Int) = v % 17 == 0
        return when (fmt) {
            Format.HEX3, Format.HEX4 ->
                if (short(r) && short(g) && short(b) && short(a)) {
                    fun h1(v: Int) = (if (upper) "%X" else "%x").format(v / 17)
                    "#" + h1(r) + h1(g) + h1(b) + (if (opaque) "" else h1(a))
                } else format(argb, if (opaque) Format.HEX6 else Format.HEX8, upper)
            Format.HEX6, Format.HEX8 ->
                "#" + hex2(r) + hex2(g) + hex2(b) + (if (opaque) "" else hex2(a))
            Format.RGB, Format.RGBA ->
                if (opaque) "rgb($r, $g, $b)" else "rgba($r, $g, $b, ${alphaText(a)})"
            Format.HSL, Format.HSLA -> {
                val (h, s, l) = toHsl(argb)
                val core = "${h.roundToInt()}, ${s.roundToInt()}%, ${l.roundToInt()}%"
                if (opaque) "hsl($core)" else "hsla($core, ${alphaText(a)})"
            }
            Format.NAME -> nameOf(argb) ?: format(argb, if (opaque) Format.HEX6 else Format.HEX8, upper)
        }
    }

    private fun alphaText(a: Int): String {
        val v = (a / 255.0 * 100).roundToInt() / 100.0
        val s = v.toString()
        return if (s.endsWith(".0")) s.dropLast(2) else s
    }

    /** الاسم القياسي إن طابق اللون اسماً (بلا شفافية)، وإلا null. */
    fun nameOf(argb: Int): String? {
        if (alphaOf(argb) != 255) return null
        val rgb = argb and 0xFFFFFF
        return NAMED_REVERSE[rgb]
    }

    /**
     * النص البديل عند اختيار [newArgb] لكود موجود [old]: يحافظ على العائلة (hex/rgb/hsl) وعلى حالة
     * الأحرف، ويُدخل الشفافية عند الحاجة، ويُبقي الاقتباس لأن النطاق لا يشمله.
     */
    fun replacementFor(old: Match, newArgb: Int): String {
        val keep = when (old.format) {
            Format.HEX3, Format.HEX4 -> Format.HEX6   // اللون المُنتقى نادراً ما يكون قابلاً للاختصار
            Format.HEX6, Format.HEX8 -> Format.HEX6
            Format.RGB, Format.RGBA -> Format.RGB
            Format.HSL, Format.HSLA -> Format.HSL
            Format.NAME -> Format.NAME
        }
        // HEX3/4 تبقى مختصرة إن أمكن
        val f = if ((old.format == Format.HEX3 || old.format == Format.HEX4)) old.format else keep
        return format(newArgb, f, old.upper)
    }

    /** الصيغة التالية في الدورة hex → rgb → hsl → hex (الاسم يتحوّل إلى hex). */
    fun nextFormat(f: Format): Format = when (f) {
        Format.HEX3, Format.HEX4, Format.HEX6, Format.HEX8 -> Format.RGB
        Format.RGB, Format.RGBA -> Format.HSL
        Format.HSL, Format.HSLA -> Format.HEX6
        Format.NAME -> Format.HEX6
    }

    // ------------------------------------------------------------------ تحويلات HSL

    /** (h 0..360, s 0..100, l 0..100). */
    fun toHsl(argb: Int): Triple<Double, Double, Double> {
        val r = redOf(argb) / 255.0; val g = greenOf(argb) / 255.0; val b = blueOf(argb) / 255.0
        val mx = max(r, max(g, b)); val mn = min(r, min(g, b))
        val l = (mx + mn) / 2
        val d = mx - mn
        if (d == 0.0) return Triple(0.0, 0.0, l * 100)
        val s = d / (1 - abs(2 * l - 1))
        val h = when (mx) {
            r -> 60 * (((g - b) / d) % 6)
            g -> 60 * ((b - r) / d + 2)
            else -> 60 * ((r - g) / d + 4)
        }
        return Triple((h + 360) % 360, s * 100, l * 100)
    }

    fun fromHsl(h: Double, s: Double, l: Double, a: Int = 255): Int {
        val sat = s / 100; val lig = l / 100
        val c = (1 - abs(2 * lig - 1)) * sat
        val hp = (((h % 360) + 360) % 360) / 60
        val x = c * (1 - abs(hp % 2 - 1))
        val (r1, g1, b1) = when {
            hp < 1 -> Triple(c, x, 0.0)
            hp < 2 -> Triple(x, c, 0.0)
            hp < 3 -> Triple(0.0, c, x)
            hp < 4 -> Triple(0.0, x, c)
            hp < 5 -> Triple(x, 0.0, c)
            else -> Triple(c, 0.0, x)
        }
        val m = lig - c / 2
        return argb(a, ((r1 + m) * 255).roundToInt(), ((g1 + m) * 255).roundToInt(), ((b1 + m) * 255).roundToInt())
    }

    // ------------------------------------------------------------------ عرض

    /** تركيب [fg] فوق خلفية معتمة [bg] (الناتج معتم). */
    fun compositeOver(fg: Int, bg: Int): Int {
        val a = alphaOf(fg)
        if (a == 255) return fg or (0xFF shl 24)
        val f = a / 255.0
        fun mix(x: Int, y: Int) = (x * f + y * (1 - f)).roundToInt()
        return argb(255, mix(redOf(fg), redOf(bg)), mix(greenOf(fg), greenOf(bg)), mix(blueOf(fg), blueOf(bg)))
    }

    /** السطوع النسبي WCAG (0..1) للون معتم. */
    fun luminance(argb: Int): Double {
        fun ch(v: Int): Double { val c = v / 255.0; return if (c <= 0.03928) c / 12.92 else Math.pow((c + 0.055) / 1.055, 2.4) }
        return 0.2126 * ch(redOf(argb)) + 0.7152 * ch(greenOf(argb)) + 0.0722 * ch(blueOf(argb))
    }

    /** أسود أو أبيض أوضح قراءةً فوق [bg]. */
    fun onColor(bg: Int): Int = if (luminance(bg) > 0.4) 0xFF000000.toInt() else 0xFFFFFFFF.toInt()

    // ------------------------------------------------------------------ أسماء CSS

    private const val NAMES = "aliceblue:f0f8ff antiquewhite:faebd7 aqua:00ffff aquamarine:7fffd4 azure:f0ffff beige:f5f5dc " +
        "bisque:ffe4c4 black:000000 blanchedalmond:ffebcd blue:0000ff blueviolet:8a2be2 brown:a52a2a burlywood:deb887 " +
        "cadetblue:5f9ea0 chartreuse:7fff00 chocolate:d2691e coral:ff7f50 cornflowerblue:6495ed cornsilk:fff8dc " +
        "crimson:dc143c cyan:00ffff darkblue:00008b darkcyan:008b8b darkgoldenrod:b8860b darkgray:a9a9a9 darkgreen:006400 " +
        "darkgrey:a9a9a9 darkkhaki:bdb76b darkmagenta:8b008b darkolivegreen:556b2f darkorange:ff8c00 darkorchid:9932cc " +
        "darkred:8b0000 darksalmon:e9967a darkseagreen:8fbc8f darkslateblue:483d8b darkslategray:2f4f4f darkslategrey:2f4f4f " +
        "darkturquoise:00ced1 darkviolet:9400d3 deeppink:ff1493 deepskyblue:00bfff dimgray:696969 dimgrey:696969 " +
        "dodgerblue:1e90ff firebrick:b22222 floralwhite:fffaf0 forestgreen:228b22 fuchsia:ff00ff gainsboro:dcdcdc " +
        "ghostwhite:f8f8ff gold:ffd700 goldenrod:daa520 gray:808080 green:008000 greenyellow:adff2f grey:808080 " +
        "honeydew:f0fff0 hotpink:ff69b4 indianred:cd5c5c indigo:4b0082 ivory:fffff0 khaki:f0e68c lavender:e6e6fa " +
        "lavenderblush:fff0f5 lawngreen:7cfc00 lemonchiffon:fffacd lightblue:add8e6 lightcoral:f08080 lightcyan:e0ffff " +
        "lightgoldenrodyellow:fafad2 lightgray:d3d3d3 lightgreen:90ee90 lightgrey:d3d3d3 lightpink:ffb6c1 " +
        "lightsalmon:ffa07a lightseagreen:20b2aa lightskyblue:87cefa lightslategray:778899 lightslategrey:778899 " +
        "lightsteelblue:b0c4de lightyellow:ffffe0 lime:00ff00 limegreen:32cd32 linen:faf0e6 magenta:ff00ff maroon:800000 " +
        "mediumaquamarine:66cdaa mediumblue:0000cd mediumorchid:ba55d3 mediumpurple:9370db mediumseagreen:3cb371 " +
        "mediumslateblue:7b68ee mediumspringgreen:00fa9a mediumturquoise:48d1cc mediumvioletred:c71585 midnightblue:191970 " +
        "mintcream:f5fffa mistyrose:ffe4e1 moccasin:ffe4b5 navajowhite:ffdead navy:000080 oldlace:fdf5e6 olive:808000 " +
        "olivedrab:6b8e23 orange:ffa500 orangered:ff4500 orchid:da70d6 palegoldenrod:eee8aa palegreen:98fb98 " +
        "paleturquoise:afeeee palevioletred:db7093 papayawhip:ffefd5 peachpuff:ffdab9 peru:cd853f pink:ffc0cb plum:dda0dd " +
        "powderblue:b0e0e6 purple:800080 rebeccapurple:663399 red:ff0000 rosybrown:bc8f8f royalblue:4169e1 " +
        "saddlebrown:8b4513 salmon:fa8072 sandybrown:f4a460 seagreen:2e8b57 seashell:fff5ee sienna:a0522d silver:c0c0c0 " +
        "skyblue:87ceeb slateblue:6a5acd slategray:708090 slategrey:708090 snow:fffafa springgreen:00ff7f steelblue:4682b4 " +
        "tan:d2b48c teal:008080 thistle:d8bfd8 tomato:ff6347 turquoise:40e0d0 violet:ee82ee wheat:f5deb3 white:ffffff " +
        "whitesmoke:f5f5f5 yellow:ffff00 yellowgreen:9acd32"

    /** اسم → 0xRRGGBB. */
    val NAMED: Map<String, Int> by lazy {
        val m = HashMap<String, Int>(200)
        for (e in NAMES.split(' ')) { val k = e.indexOf(':'); m[e.substring(0, k)] = e.substring(k + 1).toInt(16) }
        m
    }

    /** 0xRRGGBB → أقصر/أشهر اسم (الأول وروداً في الجدول، فيُفضَّل gray على grey و aqua على cyan). */
    private val NAMED_REVERSE: Map<Int, String> by lazy {
        val m = HashMap<Int, String>(200)
        for (e in NAMES.split(' ')) { val k = e.indexOf(':'); m.putIfAbsent(e.substring(k + 1).toInt(16), e.substring(0, k)) }
        m
    }
}

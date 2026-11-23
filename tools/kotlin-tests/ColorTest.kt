import com.dlof.rinlang.ColorLiterals as C
var fails = 0
fun check(name: String, ok: Boolean) { if (!ok) { fails++; println("FAIL: $name") } }
fun eq(name: String, a: Any?, b: Any?) { if (a != b) { fails++; println("FAIL: $name  got=$a expected=$b") } }
fun main() {
    var m = C.scan("""primary: "#7C5CFF", bg: '#0f0f14'""")
    eq("two hex", m.size, 2)
    eq("hex6", m[0].argb, 0xFF7C5CFF.toInt()); check("upper", m[0].upper); check("lower", !m[1].upper)
    eq("range", """primary: "#7C5CFF", bg""".substring(m[0].start, m[0].end), "#7C5CFF")
    eq("hex3", C.parse("#f0a"), 0xFFFF00AA.toInt())
    eq("hex4 alpha", C.parse("#f008"), 0x88FF0000.toInt())
    eq("hex8 alpha last", C.parse("#11223344"), 0x44112233)
    eq("hex5 rejected", C.scan("x = #12345;").size, 0)
    eq("hex7 rejected", C.scan("x = #1234567;").size, 0)
    eq("word before", C.scan("abc#fff").size, 0)
    eq("word after", C.scan("#fffz").size, 0)
    eq("html entity", C.scan("&#123456;").size, 0)
    eq("rgb", C.parse("rgb(255, 0, 128)"), 0xFFFF0080.toInt())
    eq("rgb modern", C.parse("rgb(255 0 128 / 50%)"), 0x80FF0080.toInt())
    eq("rgba", C.parse("rgba(0,0,0,0.5)"), 0x80000000.toInt())
    eq("rgb pct", C.parse("rgb(100%, 0%, 0%)"), 0xFFFF0000.toInt())
    eq("hsl red", C.parse("hsl(0, 100%, 50%)"), 0xFFFF0000.toInt())
    eq("hsl green", C.parse("hsl(120deg 100% 25%)"), 0xFF008000.toInt())
    eq("hsla", C.parse("hsla(240, 100%, 50%, 0.5)"), 0x800000FF.toInt())
    eq("name quoted", C.scan("""color: "tomato";""").firstOrNull()?.argb, 0xFFFF6347.toInt())
    eq("name unquoted ignored", C.scan("let red = 1;").size, 0)
    eq("name text ignored", C.scan("""print "red alert";""").size, 0)
    eq("name range", """c: " Tomato "!""".let { s -> val x = C.scan(s)[0]; s.substring(x.start, x.end) }, "Tomato")
    eq("bad rgb", C.scan("rgb(1,2)").size, 0)
    eq("bad rgb2", C.scan("rgb(a,b,c)").size, 0)
    eq("func prefix word", C.scan("myrgb(1,2,3)").size, 0)
    eq("multi", C.scan("a: #fff b: rgb(1,2,3) c: hsl(10,20%,30%)").size, 3)
    eq("matchAt", C.matchAt("x = #ff0000;", 6)?.argb, 0xFFFF0000.toInt())
    eq("matchAt end", C.matchAt("x = #ff0000;", 11)?.argb, 0xFFFF0000.toInt())
    eq("matchAt none", C.matchAt("x = #ff0000;", 2), null)
    // format
    eq("fmt hex6", C.format(0xFF7C5CFF.toInt(), C.Format.HEX6, true), "#7C5CFF")
    eq("fmt hex6 lower", C.format(0xFF7C5CFF.toInt(), C.Format.HEX6, false), "#7c5cff")
    eq("fmt hex8", C.format(0x807C5CFF.toInt(), C.Format.HEX6, false), "#7c5cff80")
    eq("fmt hex3 ok", C.format(0xFFFF00AA.toInt(), C.Format.HEX3), "#f0a")
    eq("fmt hex3 promote", C.format(0xFF7C5CFF.toInt(), C.Format.HEX3, true), "#7C5CFF")
    eq("fmt rgb", C.format(0xFFFF0080.toInt(), C.Format.RGB), "rgb(255, 0, 128)")
    eq("fmt rgba", C.format(0x80FF0080.toInt(), C.Format.RGB), "rgba(255, 0, 128, 0.5)")
    eq("fmt hsl", C.format(0xFFFF0000.toInt(), C.Format.HSL), "hsl(0, 100%, 50%)")
    eq("fmt name", C.format(0xFFFF6347.toInt(), C.Format.NAME), "tomato")
    eq("fmt name fallback", C.format(0xFF123456.toInt(), C.Format.NAME, false), "#123456")
    eq("name gray pref", C.nameOf(0xFF808080.toInt()), "gray")
    // roundtrip
    for (c in listOf(0xFF7C5CFF, 0xFF0F0F14, 0xFFF5F5F7, 0xFFE11D48, 0xFF16A34A, 0x80336699)) {
        val ci = c.toInt()
        val viaRgb = C.parse(C.format(ci, C.Format.RGB)); eq("rt rgb ${c.toString(16)}", viaRgb, ci)
        val viaHex = C.parse(C.format(ci, C.Format.HEX6)); eq("rt hex ${c.toString(16)}", viaHex, ci)
        val viaHsl = C.parse(C.format(ci, C.Format.HSL))!!
        val d = maxOf(Math.abs(C.redOf(viaHsl) - C.redOf(ci)), Math.abs(C.greenOf(viaHsl) - C.greenOf(ci)), Math.abs(C.blueOf(viaHsl) - C.blueOf(ci)))
        check("rt hsl close ${c.toString(16)} d=$d", d <= 3)
    }
    // replacement keeps family
    val old = C.scan("""x: "#7c5cff" """)[0]
    eq("repl lower", C.replacementFor(old, 0xFF112233.toInt()), "#112233")
    val oldU = C.scan("""x: "#7C5CFF" """)[0]
    eq("repl upper", C.replacementFor(oldU, 0xFFaabbcc.toInt()), "#AABBCC")
    val oldR = C.scan("rgb(1,2,3)")[0]
    eq("repl rgb", C.replacementFor(oldR, 0x80010203.toInt()), "rgba(1, 2, 3, 0.5)")
    val old3 = C.scan("#f0a")[0]
    eq("repl hex3 keeps short", C.replacementFor(old3, 0xFF00FF00.toInt()), "#0f0")
    eq("next", C.nextFormat(C.Format.HEX6), C.Format.RGB)
    // contrast
    eq("on white", C.onColor(0xFFFFFFFF.toInt()), 0xFF000000.toInt())
    eq("on black", C.onColor(0xFF000000.toInt()), 0xFFFFFFFF.toInt())
    eq("on purple", C.onColor(0xFF7C5CFF.toInt()), 0xFFFFFFFF.toInt())
    eq("composite", C.compositeOver(0x80FFFFFF.toInt(), 0xFF000000.toInt()), 0xFF808080.toInt())
    eq("names count", C.NAMED.size, 148)
    println(if (fails == 0) "ALL OK" else "$fails FAILED")
}

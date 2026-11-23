import com.dlof.rinlang.*
fun main() {
    var bad = 0
    for (t in RinTheme.ALL) {
        val cases = listOf("fg" to RinTheme.contrast(t.fg, t.bg) to 7.0, "white" to RinTheme.contrast(t.white, t.bg) to 9.0,
            "dim" to RinTheme.contrast(t.dim, t.bg) to 3.5, "accent" to RinTheme.contrast(t.accent, t.bg) to 4.5,
            "err" to RinTheme.contrast(t.error, t.bg) to 4.5, "ok" to RinTheme.contrast(t.ok, t.bg) to 4.5, "warn" to RinTheme.contrast(t.warn, t.bg) to 4.5,
            "blue" to RinTheme.contrast(t.blue, t.bg) to 4.5, "input" to RinTheme.contrast(t.fg, t.inputBg) to 7.0, "dimInput" to RinTheme.contrast(t.dim, t.inputBg) to 3.0)
        for (c in cases) { val (nm, v) = c.first; if (v < c.second) { bad++; println("${t.id} $nm contrast ${"%.2f".format(v)} < ${c.second}") } }
        if (RinTheme.luminance(t.bg) > 0.05) { bad++; println("${t.id} not dark") }
    }
    check(RinTheme.find("mid")?.id == "midnight"); check(RinTheme.find("n") == null || RinTheme.find("n")?.id == "nord"); check(RinTheme.find("zzz") == null)
    check(RinTheme.byId("nope").id == "rin"); check(RinTheme.ALL.map { it.id }.toSet().size == RinTheme.ALL.size)
    println(if (bad == 0) "OK" else "FAIL $bad")
}

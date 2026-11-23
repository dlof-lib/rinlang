import com.dlof.rinlang.*
fun main() {
    var bad = 0
    val logo = RinBanner.logo("1.8", info = listOf("Language" to "Rin 1.0.0","Engine" to "C++17","App" to "RinStudio 1.8","Libraries" to "34 built-in"))
    check(RinBanner.parseVersion("Rin Engine 1.0.0 (C++17) — x") == "1.0.0")
    val w = RinBanner.LOGO_ROWS[0].length
    for (i in logo.indices) { val p = RinAnsi.strip(logo[i]); if (p.length != RinBanner.INNER + 4) { bad++; println("logo row $i len ${p.length}") } }
    val lib = RinBanner.library("twinkling-stars","UI","مكتبة نجوم متلألئة للواجهات مع حركة ناعمة وألوان قابلة للتخصيص بالكامل","twinkle() • stars() • sky()","lib/twinkling-stars.og.rin", listOf("Language" to "Rin 1.0.0","Source" to "built-in"))
    for (l in lib) { val p = RinAnsi.strip(l); if (p.length != RinBanner.INNER + 4) { bad++; println("lib len ${p.length}: $p") } }
    (logo + lib + RinBanner.libraryIndex(listOf("UI" to listOf("a","bb","ccc"),"DATA" to listOf("math","json")))).forEach { println(RinAnsi.strip(it)) }
    RinBanner.palette().forEach { check(RinAnsi.strip(it).isNotEmpty()) }
    println(if (bad == 0) "OK" else "FAIL $bad")
}

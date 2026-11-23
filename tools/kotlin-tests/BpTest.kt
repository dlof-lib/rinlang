import com.dlof.rinlang.ProjectBlueprint as B
import java.io.File
fun main(args: Array<String>) {
    val root = File(args[0]); root.deleteRecursively(); root.mkdirs()
    for ((lang, libs) in listOf("ar" to listOf("lib/math.og.rin", "lib/strings.og.rin"), "en" to emptyList<String>())) {
        val bp = B(name = "My \"App\" $lang", typeId = "free", description = "وصف \"تجريبي\"\nسطر", author = "me", libraries = libs, lang = lang)
        val d = File(root, lang); d.mkdirs()
        File(d, "main.rin").writeText(bp.patchMain("print \"hi\";\nprint greet(\"x\"), factorial(3);\n"))
        for (e in bp.plan("2026-11-23T00:00:00Z")) { val f = File(d, e.path); f.parentFile.mkdirs(); f.writeText(e.content) }
        println(B.renderTree(bp.previewPaths()).joinToString("\n"))
    }
    val bp2 = B(name = "w", typeId = "html")
    println(bp2.plan().map { it.path })
    val m = bp2.patchMain("x"); println("web main unchanged: ${m == "x"}")
    val bp3 = B(name = "z", typeId = "free", libraries = listOf("lib/math.og.rin"))
    val once = bp3.patchMain("print 1;\n"); println("idempotent: ${bp3.patchMain(once) == once}")
    println("problems: " + listOf("", "a/b", ".x", "ok name", "x".repeat(70)).map { B.nameProblem(it) })
}

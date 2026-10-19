import com.dlof.rinlang.ClipboardHistory
import com.dlof.rinlang.ClipboardText as C

var fails = 0; var total = 0
fun eq(name: String, got: Any?, want: Any?) {
    total++
    if (got != want) { fails++; println("FAIL $name\n  got : ${got.toString().replace("\n", "\\n")}\n  want: ${want.toString().replace("\n", "\\n")}") }
}

fun main() {
    // normalize
    eq("crlf", C.normalize("a\r\nb\rc", 4), "a\nb\nc")
    eq("bom/zw", C.normalize("\uFEFFlet\u200B x", 4), "let x")
    eq("nbsp", C.normalize("a\u00A0b", 4), "a b")
    eq("lead tab", C.normalize("\tfoo\n\t\tbar\nx\ty", 4), "    foo\n        bar\nx\ty")
    eq("mixed lead", C.normalize(" \tfoo", 2), "   foo")
    eq("empty", C.normalize("", 4), "")

    // lineCount
    eq("lc0", C.lineCount(""), 0); eq("lc1", C.lineCount("a"), 1)
    eq("lc1n", C.lineCount("a\n"), 1); eq("lc2", C.lineCount("a\nb"), 2)

    // reindent: single-line untouched
    eq("single", C.reindentForPaste("  x", "    ", "    "), "  x")
    // block mode at indented blank line (before = 4 spaces)
    eq("block", C.reindentForPaste("if x {\n    y\n}", "    ", "    "), "if x {\n        y\n    }".let { "if x {\n        y\n    }" })
    // block mode, col 0, base 4, source has its own indent 8
    eq("block col0", C.reindentForPaste("        a\n          b\n        c", "", "    "),
        "    a\n      b\n    c")
    // mid-line: let a = <paste> at base 4
    eq("midline", C.reindentForPaste("f(\n    1\n)", "    let a = ", "    "), "f(\n        1\n    )")
    // mid-line where source lines carry original indent (copied from inside block at indent 4)
    eq("midline orig", C.reindentForPaste("foo()\n    bar()\n    baz()", "x = ", ""), "foo()\nbar()\nbaz()")
    // blank lines stay empty
    eq("blank", C.reindentForPaste("a\n   \nb", "  ", "  "), "a\n\n  b")
    // reindentLines
    eq("lines", C.reindentLines("        a\n          b\n", "  "), "  a\n    b\n")
    eq("lines blank", C.reindentLines("    a\n\n    b", ""), "a\n\nb\n")

    // markdown
    eq("md", C.toMarkdown("let a = 1\n", "rin"), "```rin\nlet a = 1\n```")
    eq("md fence", C.toMarkdown("x ``` y", "rin"), "````rin\nx ``` y\n````")
    // preview
    eq("prev", C.preview("\n   hello  \nx"), "hello")
    eq("prev long", C.preview("a".repeat(100), 10), "aaaaaaaaa…")

    // history
    ClipboardHistory.clear()
    ClipboardHistory.add("a"); ClipboardHistory.add("b"); ClipboardHistory.add("a", true)
    eq("hist order", ClipboardHistory.snapshot().map { it.text }, listOf("a", "b"))
    eq("hist flag", ClipboardHistory.snapshot()[0].wholeLines, true)
    ClipboardHistory.add("a", false)
    eq("hist flag kept", ClipboardHistory.snapshot()[0].wholeLines, true)
    ClipboardHistory.add("   "); eq("hist blank", ClipboardHistory.snapshot().size, 2)
    for (i in 0 until 40) ClipboardHistory.add("x$i")
    eq("hist max", ClipboardHistory.snapshot().size, ClipboardHistory.MAX)
    eq("hist top", ClipboardHistory.snapshot()[0].text, "x39")

    println("clipboard tests: ${total - fails}/$total")
    if (fails > 0) kotlin.system.exitProcess(1)
}

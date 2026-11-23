import com.dlof.rinlang.SmartEdit as S
import com.dlof.rinlang.SmartEdit.CaseMode as CM
var fails = 0
fun eq(name: String, a: Any?, b: Any?) { if (a != b) { fails++; println("FAIL: $name  got=<$a> expected=<$b>") } }
fun ap(doc: String, e: S.Edit?) = e?.let { S.apply(doc, it) }
fun main() {
    eq("words", S.splitWords("HTTPServerError2go_fooBar-baz"), listOf("HTTP","Server","Error","2","go","foo","Bar","baz"))
    eq("snake", ap("let myValue = 1", S.changeCase("let myValue = 1", 6, 6, CM.SNAKE)), "let my_value = 1")
    eq("camel", ap("my_value_x", S.changeCase("my_value_x", 3, 3, CM.CAMEL)), "myValueX")
    eq("pascal sel", ap("a b my_val", S.changeCase("a b my_val", 4, 10, CM.PASCAL)), "a b MyVal")
    eq("const", ap("maxCount", S.changeCase("maxCount", 0, 8, CM.CONSTANT)), "MAX_COUNT")
    eq("kebab", ap("fooBar", S.changeCase("fooBar", 2, 2, CM.KEBAB)), "foo-bar")
    eq("upper", ap("hello world", S.changeCase("hello world", 0, 5, CM.UPPER)), "HELLO world")
    eq("title", ap("hello big world", S.changeCase("hello big world", 0, 15, CM.TITLE)), "Hello Big World")
    eq("noop", S.changeCase("ABC", 0, 3, CM.UPPER), null)
    eq("no word", S.changeCase("  ", 1, 1, CM.UPPER), null)
    eq("multi-line snake", ap("aB\ncD", S.changeCase("aB\ncD", 0, 5, CM.SNAKE)), "a_b\nc_d")
    // numbers
    eq("inc", ap("x = 41;", S.stepNumber("x = 41;", 5, 1)), "x = 42;")
    eq("dec neg", ap("x = 0;", S.stepNumber("x = 0;", 4, -1)), "x = -1;")
    eq("neg inc", ap("x = -1;", S.stepNumber("x = -1;", 5, 1)), "x = 0;")
    eq("dec digits", ap("w = 1.25;", S.stepNumber("w = 1.25;", 5, 1)), "w = 2.25;")
    eq("dec digits2", ap("w = 9.5;", S.stepNumber("w = 9.5;", 4, 1)), "w = 10.5;")
    eq("next number after cursor", ap("let a; x = 7;", S.stepNumber("let a; x = 7;", 0, 1)), "let a; x = 8;")
    eq("glued ident skip", S.stepNumber("var1 = x;", 3, 1), null)
    eq("color skip", S.stepNumber("c = #ff00;", 7, 1), null)
    eq("2nd line", ap("a = 1;\nb = 5;", S.stepNumber("a = 1;\nb = 5;", 12, 1)), "a = 1;\nb = 6;")
    eq("minus op", ap("a-1", S.stepNumber("a-1", 2, 1)), "a-2")
    // flip
    eq("flip true", ap("x = true;", S.flipWord("x = true;", 6)), "x = false;")
    eq("flip True", ap("X = True", S.flipWord("X = True", 6)), "X = False")
    eq("flip none", S.flipWord("x = abc", 5), null)
    // surround
    eq("surround", ap("a bc d", S.surround("a bc d", 2, 4, '"')), "a \"bc\" d")
    eq("surround sel", S.surround("a bc d", 2, 4, '(')?.let { it.selStart to it.selEnd }, 3 to 5)
    eq("surround none", S.surround("abc", 1, 1, '('), null)
    eq("surround bad", S.surround("abc", 0, 2, 'x'), null)
    eq("unsurround sel", ap("f(abc)", S.unsurround("f(abc)", 1, 6)), "fabc")
    eq("unsurround around", ap("f(abc)", S.unsurround("f(abc)", 3, 3)), "fabc")
    eq("unsurround str", ap("say \"hi\"", S.unsurround("say \"hi\"", 6, 6)), "say hi")
    // expand
    val d = "fun f(a, \"x y\") { return a + 1; }\nnext"
    var r = S.expandSelection(d, 6, 6); eq("e1 word", d.substring(r!!.first, r.last + 1), "f".let { "a" }.let { d.substring(r.first, r.last+1) })
    val p = d.indexOf("x y") + 1
    r = S.expandSelection(d, p, p); eq("e word", d.substring(r!!.first, r.last + 1), "x")
    r = S.expandSelection(d, r.first, r.last + 1); eq("e str inner", d.substring(r!!.first, r.last + 1), "x y")
    r = S.expandSelection(d, r.first, r.last + 1); eq("e str outer", d.substring(r!!.first, r.last + 1), "\"x y\"")
    r = S.expandSelection(d, r.first, r.last + 1); eq("e paren inner", d.substring(r!!.first, r.last + 1), "a, \"x y\"")
    r = S.expandSelection(d, r.first, r.last + 1); eq("e paren outer", d.substring(r!!.first, r.last + 1), "(a, \"x y\")")
    r = S.expandSelection(d, r.first, r.last + 1); eq("e line trimmed", d.substring(r!!.first, r.last + 1), "fun f(a, \"x y\") { return a + 1; }")
    r = S.expandSelection(d, r.first, r.last + 1); eq("e all", d.substring(r!!.first, r.last + 1), d)
    eq("e end", S.expandSelection(d, r.first, r.last + 1), null)
    val br = "a { b ( c ) }"
    r = S.expandSelection(br, br.indexOf('c'), br.indexOf('c')); eq("brace word", br.substring(r!!.first, r.last+1), "c")
    r = S.expandSelection(br, r.first, r.last + 1); eq("brace inner", br.substring(r!!.first, r.last+1), " c ")
    val cm = "x // ( not\n(a)"
    eq("comment parens ignored", S.enclosingPairs(cm, 12, 12).size, 1)
    // lines
    eq("below", ap("  if (x) {", S.insertLineBelow("  if (x) {", 5)), "  if (x) {\n      ")
    eq("below cursor", S.insertLineBelow("  a", 2).let { it.selStart }, 6)
    eq("above", ap("  a\nb", S.insertLineAbove("  a\nb", 3)), "  \n  a\nb")
    eq("above cursor", S.insertLineAbove("  a\nb", 3).selStart, 2)
    eq("above first", ap("x", S.insertLineAbove("x", 0)), "\nx")
    eq("reverse", ap("a\nb\nc", S.transformLines("a\nb\nc", 0, 5, S.LineOp.REVERSE)), "c\nb\na")
    eq("unique", ap("a\nb\na", S.transformLines("a\nb\na", 0, 5, S.LineOp.UNIQUE)), "a\nb")
    eq("unique none", S.transformLines("a\nb", 0, 3, S.LineOp.UNIQUE), null)
    eq("sort num", ap("x10\nx2\nx1", S.transformLines("x10\nx2\nx1", 0, 9, S.LineOp.SORT_NUMERIC)), "x1\nx2\nx10")
    // colors
    eq("cycle", ap("c: \"#7C5CFF\";", S.cycleColorFormat("c: \"#7C5CFF\";", 6)), "c: \"rgb(124, 92, 255)\";")
    eq("cycle2", ap("c: rgb(255, 0, 0);", S.cycleColorFormat("c: rgb(255, 0, 0);", 5)), "c: hsl(0, 100%, 50%);")
    eq("cycle3", ap("c: hsl(0, 100%, 50%);", S.cycleColorFormat("c: hsl(0, 100%, 50%);", 5)), "c: #ff0000;")
    eq("cycle none", S.cycleColorFormat("abc", 1), null)
    eq("replace", ap("x: \"#ffffff\" y: \"#000\"", S.replaceColorAt("x: \"#ffffff\" y: \"#000\"", 5, 0xFF112233.toInt())), "x: \"#112233\" y: \"#000\"")
    eq("replace 2nd line", ap("a\nb: #fff", S.replaceColorAt("a\nb: #fff", 6, 0xFF00FF00.toInt())), "a\nb: #0f0")
    println(if (fails == 0) "ALL OK" else "$fails FAILED")
}

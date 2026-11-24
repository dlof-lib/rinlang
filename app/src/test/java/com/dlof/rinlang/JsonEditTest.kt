package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Test
import com.dlof.rinlang.JsonEdit as J

/** عمليات تحرير JSON الخالصة (JsonEdit) — بلا واجهة ولا JNI. */
class JsonEditTest {
    private val pd = "{\n  \"users\": [\n    {\"name\": \"Ali\", \"x y\": 1},\n    {\"name\": \"Sara\"}\n  ],\n  \"n\": 5\n}"

    private fun eq(exp: Any?, act: Any?, msg: String = "") = assertEquals(msg, exp, act)
    private fun ap(doc: String, e: SmartEdit.Edit?) = SmartEdit.apply(doc, e!!)

    @Test fun validate() {

    eq(null, J.validate("""{"a":[1,2,{"b":null}],"c":"x\n\u00e9"}"""), "valid")
    eq(J.ErrKind.TRAILING_COMMA, J.validate("""{"a":1,}""")?.kind, "trailing")
    eq(J.ErrKind.EXPECTED_COMMA_OR_END, J.validate("{\"a\":1\n\"b\":2}")?.kind, "missing comma")
    val e = J.validate("{\n  \"a\": 1\n  \"b\": 2\n}")!!
    eq(3 to 3, e.line to e.col, "err pos")
    eq(J.ErrKind.SINGLE_QUOTE, J.validate("{'a':1}")?.kind, "single")
    eq(J.ErrKind.COMMENT, J.validate("{/*x*/}")?.kind, "comment strict")
    eq(null, J.validate("{/*x*/\"a\":1 // hi\n}", true), "comment jsonc")
    eq(J.ErrKind.BAD_NUMBER, J.validate("[01]")?.kind, "leading zero")
    eq(J.ErrKind.BAD_NUMBER, J.validate("[1.]")?.kind, "dot")
    eq(J.ErrKind.EMPTY, J.validate("  ")?.kind, "empty")
    eq(J.ErrKind.EXTRA_DATA, J.validate("{} x")?.kind, "extra")
    eq(J.ErrKind.UNTERMINATED_STRING, J.validate("[\"abc")?.kind, "unterminated")
    eq(J.ErrKind.BAD_ESCAPE, J.validate("[\"a\\qb\"]")?.kind, "bad esc")
    eq(J.ErrKind.BAD_ESCAPE, J.validate("[\"\\u12\"]")?.kind, "bad u")
    eq(J.ErrKind.CONTROL_CHAR, J.validate("[\"a\nb\"]")?.kind, "ctrl")
    eq(J.ErrKind.UNEXPECTED_END, J.validate("{\"a\":")?.kind, "end")
    eq(J.ErrKind.EXPECTED_KEY, J.validate("{1:2}")?.kind, "key")
    eq(J.ErrKind.EXPECTED_COLON, J.validate("{\"a\" 1}")?.kind, "colon")
    eq(null, J.validate("-0.5e+10"), "scalar top")
    }

    @Test fun prettyAndMinify() {
    val min = """{"a":[1,2,{"b":null}],"c":{},"d":[]}"""
    val pretty = "{\n  \"a\": [\n    1,\n    2,\n    {\n      \"b\": null\n    }\n  ],\n  \"c\": {},\n  \"d\": []\n}"
    eq(pretty, ap(min, J.pretty(min, 0, 0).edit), "pretty")
    eq(min, ap(pretty, J.minify(pretty, 0, 0).edit), "minify")
    eq(true, J.pretty(pretty, 0, 0).noChange, "pretty idempotent")
    eq(pretty.replace("  ", "    "), ap(min, J.pretty(min, 0, 0, indent = "    ").edit), "indent4")
    // trailing newline kept
    eq(pretty + "\n", ap(min + "\n", J.pretty(min + "\n", 0, 0).edit), "trailing nl")
    // precision/escapes preserved
    val raw = """{"n":12345678901234567890.123456789e5,"s":"\u00e9\/"}"""
    eq(true, J.minify(" $raw", 0, 0).noChange, "raw preserved (no change)")
    eq(raw, ap(raw.replace(",", ", "), J.minify(raw.replace(",", ", "), 0, 0).edit), "raw preserved")
    // error with offset
    val bad = "\n{\"a\":1,}"
    eq(J.ErrKind.TRAILING_COMMA, J.pretty(bad, 0, 0).error?.kind, "pretty err")
    eq(2, J.pretty(bad, 0, 0).error?.line, "pretty err line")
    // comments block
    eq(true, J.pretty("{/*c*/}", 0, 0, comments = true).hasComments, "comments block")
    // selection scope
    val doc = "{\"x\": {\"a\":1,\"b\":2}, \"y\": 3}"
    val s0 = doc.indexOf("{\"a\""); val s1 = doc.indexOf("}, ") + 1
    eq("{\"x\": {\n  \"a\": 1,\n  \"b\": 2\n}, \"y\": 3}", ap(doc, J.pretty(doc, s0, s1).edit), "selection scope")
    // invalid selection falls back to whole doc
    eq(pretty.length > 0, J.pretty(min, 3, 8).edit != null, "invalid sel -> whole")
    // caret kept
    val c = J.pretty(min, min.indexOf("null"), min.indexOf("null")).edit!!
    val outp = SmartEdit.apply(min, c)
    eq("null", outp.substring(c.selStart, c.selStart + 4), "caret mapped")
    }

    @Test fun sortKeys() {
    val un = "{\"b\":1,\"B\":2,\"a\":{\"z\":1,\"c\":[{\"y\":1,\"x\":2}]}}"
    eq("{\"a\":{\"c\":[{\"x\":2,\"y\":1}],\"z\":1},\"B\":2,\"b\":1}", ap(un, J.sortKeys(un, 0, 0).edit), "sort deep minified")
    eq("{\"a\":{\"z\":1,\"c\":[{\"y\":1,\"x\":2}]},\"B\":2,\"b\":1}", ap(un, J.sortKeys(un, 0, 0, J.SortMode(deep = false)).edit), "sort shallow")
    eq(true, J.sortKeys("{\"a\":1,\"b\":2}", 0, 0).noChange, "sort nochange")
    val desc = ap("{\"a\":1,\"b\":2}", J.sortKeys("{\"a\":1,\"b\":2}", 0, 0, J.SortMode(descending = true)).edit)
    eq("{\"b\":2,\"a\":1}", desc, "sort desc")
    val multi = "{\n    \"b\": 1,\n    \"a\": 2\n}"
    eq("{\n    \"a\": 2,\n    \"b\": 1\n}", ap(multi, J.sortKeys(multi, 0, 0).edit), "sort keeps indent 4")
    }

    @Test fun repair() {
    val broken = "{\n  // note\n  name: 'Ali \"Q\"',\n  ok: True, none: None,\n  list: [1, 2,],\n}\n"
    val fixed = ap(broken, J.repair(broken).edit)
    eq(null, J.validate(fixed), "repair valid")
    eq(true, fixed.contains("\"name\": \"Ali \\\"Q\\\"\"") && fixed.contains("\"ok\": true") && fixed.contains("\"none\": null"), "repair content: $fixed")
    eq(true, J.repair("{\"a\":1}").noChange, "repair nochange")
    eq(J.ErrKind.UNEXPECTED_END, J.repair("{\"a\":").error?.kind, "repair unrecoverable")
    }

    @Test fun paths() {
    eq("\$.users[1].name", J.pathAt(pd, pd.indexOf("Sara") + 1), "path name value")
    eq("\$.users[0][\"x y\"]", J.pathAt(pd, pd.indexOf("x y") + 1), "path quoted key")
    eq("\$.n", J.pathAt(pd, pd.indexOf("5")), "path n")
    eq("\$", J.pathAt(pd, 0), "path root")
    eq("\$.users", J.pathAt(pd, pd.indexOf("\"users\"") + 2), "path key of container")
    eq(null, J.pathAt("{bad", 2), "path invalid")
    eq(pd.indexOf("\"name\": \"Sara\""), J.offsetOfPath(pd, "\$.users[1].name")?.let { pd.indexOf("\"name\": \"Sara\"") }, "goto sanity")
    eq(pd.indexOf("\"name\": \"Sara\""), J.offsetOfPath(pd, "\$.users[1].name"), "goto")
    eq(pd.indexOf("\"x y\""), J.offsetOfPath(pd, "\$.users[0][\"x y\"]"), "goto quoted")
    eq(pd.indexOf("\"x y\""), J.offsetOfPath(pd, "users.0['x y']"), "goto relaxed")
    eq(null, J.offsetOfPath(pd, "\$.users[9]"), "goto missing")
    eq(null, J.offsetOfPath(pd, "\$.users[x"), "goto bad syntax")
    eq(0, J.offsetOfPath(pd, "\$"), "goto root")
    }

    @Test fun outline() {
    val o = J.outline(pd)!!
    eq(listOf("users  [2]", "[0]  {2}", "name: \"Ali\"", "x y: 1", "[1]  {1}", "name: \"Sara\"", "n: 5"), o.map { it.label }, "outline labels")
    eq(listOf(0, 1, 2, 2, 1, 2, 0), o.map { it.depth }, "outline depth")
    eq("\$.users[1].name", o[5].path, "outline path")
    val big = "[" + (1..100).joinToString(",") + "]"
    val ob = J.outline(big, perContainer = 5)!!
    eq(6, ob.size, "outline cap"); eq("… +95", ob.last().label, "outline cap label")
    eq(null, J.outline("{"), "outline invalid")
    }

    @Test fun stats() {
    val st = J.stats(pd)!!
    eq(listOf(3, 1, 5, 2, 2, 0, 0), listOf(st.objects, st.arrays, st.keys, st.strings, st.numbers, st.bools, st.nulls), "stats counts")
    eq(4, st.maxDepth, "depth")
    }

    @Test fun stringLiteralToggle() {
    eq("\"a\\nb \\\"q\\\"\"", ap("a\nb \"q\"", J.toggleStringLiteral("a\nb \"q\"", 0, 7)), "escape")
    eq("a\nb \"q\"", ap("\"a\\nb \\\"q\\\"\"", J.toggleStringLiteral("\"a\\nb \\\"q\\\"\"", 0, 14)), "unescape")
    val ln = "{\"k\": \"{\\\"a\\\":1}\", \"z\": 2}"
    eq("{\"k\": {\"a\":1}, \"z\": 2}", ap(ln, J.toggleStringLiteral(ln, ln.indexOf("{\\") + 3, ln.indexOf("{\\") + 3)), "unescape at caret")
    eq(null, J.toggleStringLiteral("{\"k\": 1}", 6, 6), "toggle none")
    eq("é\u2028".length, J.decode(J.quote("é\u2028")).length, "quote roundtrip")
    eq("é\u2028\t\u0001", J.decode(J.quote("é\u2028\t\u0001")), "quote roundtrip2")
    }

    @Test fun insertItems() {
    var d = "{\n  \"a\": 1\n  \n}"
    var e1 = J.insertPair(d, d.indexOf("  \n}") + 2, d.indexOf("  \n}") + 2)
    eq("{\n  \"a\": 1,\n  \"key\": \"\"\n}", ap(d, e1), "insert pair after value")
    eq("key", SmartEdit.apply(d, e1).substring(e1.selStart, e1.selEnd), "pair selects key")
    d = "{\n  \n}"
    e1 = J.insertPair(d, 4, 4)
    eq("{\n  \"key\": \"\"\n}", ap(d, e1), "insert pair first")
    d = "[1, ]"
    eq("[1, []]", ap(d, J.insertContainer(d, 4, 4, false)).let { it }.replace(",[", ", ["), "insert arr (comma handled)")
    d = "{\"a\": }"
    eq("{\"a\": {}}", ap(d, J.insertContainer(d, 6, 6, true)), "insert obj after colon (no comma)")
    d = "[1,\n  ]"
    eq("[1,\n  {}]", ap(d, J.insertContainer(d, 6, 6, true)), "after comma none")
    }

    @Test fun enterAddsComma() {
    var t = "{\n  \"a\": 1\n  \"b\": 2\n}"
    var ce = J.continueComma(t, t.indexOf("1") + 1)
    eq("{\n  \"a\": 1,\n  \n  \"b\": 2\n}", ap(t, ce), "enter adds comma")
    eq(true, ce!!.selStart == ap(t, ce).indexOf("  \n  \"b\"") + 2, "enter caret")
    t = "{\n  \"a\": 1\n}"
    eq(null, J.continueComma(t, t.indexOf("1") + 1), "last member: no comma")
    t = "{\n  \"a\": 1,\n  \"b\": 2\n}"
    eq(null, J.continueComma(t, t.indexOf("1,") + 2), "already comma")
    t = "{\n  \"a\": \"x\"\n  \"b\": 2\n}"
    eq(null, J.continueComma(t, t.indexOf("\"x") + 1), "inside string")
    t = "{\n  \"a\": [\n    {\"p\": 1}\n    {\"p\": 2}\n  ]\n}"
    ce = J.continueComma(t, t.indexOf("}\n    {") + 1)
    eq(true, ce != null && ap(t, ce).contains("{\"p\": 1},\n    \n    {\"p\": 2}"), "after nested obj")
    t = "[\n  1\n  2\n]"
    eq(true, J.continueComma(t, 5) != null, "numbers")
    t = "{\n  \"a\": {\n    \"b\": 1\n  }\n}"
    eq(null, J.continueComma(t, t.indexOf("1") + 1), "before closer")
    }

    @Test fun extensions() {
    eq(true, J.isJsonExtension(".JSON")); eq(true, J.allowsComments("jsonc")); eq(false, J.allowsComments("json")); eq(false, J.isJsonExtension("md"))
    }
}

package com.dlof.rinlang

import org.junit.Assert.*
import org.junit.Test

class RinContainerOutputTest {

    private val PIPE = "\uD83E\uDDF5"            // 🧵
    private val GROUP = "\uD83D\uDDC2\uFE0F"     // 🗂️
    private val DOC = "\uD83E\uDDFE"             // 🧾
    private val GENERIC = "\uD83D\uDCE6"         // 📦
    private val SECTION = "\uD83D\uDD39"         // 🔹
    private val OK = "\u2705"
    private val END = "\u25FD"

    @Test fun parsesOpenLines() {
        val m = RinContainerParser.parse("$PIPE container.pipe = P1")!!
        assertEquals(ContainerRole.OPEN, m.role)
        assertEquals("container.pipe", m.tag); assertEquals("P1", m.name)
        assertEquals(ContainerFamily.PIPE, m.family)
        assertNull(RinContainerParser.parse("$PIPE container.pipe")!!.name)
        assertEquals(ContainerFamily.GROUP, RinContainerParser.parse("$GROUP Containers.Group = G")!!.family)
        assertEquals(ContainerFamily.MAKE, RinContainerParser.parse("\uD83D\uDEE0\uFE0F make.unit = M")!!.family)
    }

    @Test fun parsesCloseLinesWithMembers() {
        val m = RinContainerParser.parse("$OK .end/Containers.Group (G) [تحتوي: a, b]")!!
        assertEquals(ContainerRole.CLOSE, m.role)
        assertEquals("G", m.name); assertEquals(listOf("a", "b"), m.members)
        assertEquals("x (y)", RinContainerParser.parse("$OK .end/container.data (x (y))")!!.name)
        assertEquals(ContainerRole.CLOSE, RinContainerParser.parse("$END .end/Section (A)")!!.role)
    }

    @Test fun ordinaryLinesAreNotContainers() {
        for (s in listOf(
            "hello", "container.pipe = x", "\uD83D\uDCE5 container.import: تم استيراد \"a\"",
            "[Error line 3]: container = x", "$OK .end/Translations", "$OK done", "",
        )) assertNull("input=$s", RinContainerParser.parse(s))
    }

    @Test fun formatterMarksContainerLinesAndFixesKind() {
        val l = RinConsoleFormatter.formatLines(
            "$DOC container.doc = D1\nplain\n$OK .end/container.doc (D1)\n$GENERIC container = X"
        )
        assertEquals(LogKind.STRUCTURE, l[0].kind)               // كان DOC_INSERT
        assertEquals("container.doc = D1", l[0].text)
        assertNotNull(l[0].container); assertNull(l[1].container)
        assertEquals(LogKind.SUCCESS, l[2].kind)
        assertEquals(ContainerRole.CLOSE, l[2].container!!.role)
        assertEquals(LogKind.STRUCTURE, l[3].kind)               // كان IMPORT
    }

    @Test fun treeNestsAndMatchesByTagAndName() {
        val l = RinConsoleFormatter.formatLines(
            listOf(
                "$GROUP Containers.Group = G",
                "$PIPE container.pipe = P",
                "a", "b",
                "$OK .end/container.pipe (P)",
                "$OK .end/Containers.Group (G) [تحتوي: P]",
                "after",
            ).joinToString("\n")
        )
        val tree = RinContainerTree.build(l)
        assertEquals(2, tree.size)
        val g = tree[0] as RinOutputNode.Block
        assertTrue(g.isClosed); assertEquals(listOf("P"), g.closeMark!!.members)
        assertEquals(1, g.children.size)
        val p = g.children[0] as RinOutputNode.Block
        assertEquals(2, p.contentLineCount()); assertEquals(2, g.contentLineCount())
        assertTrue(tree[1] is RinOutputNode.Leaf)
    }

    @Test fun unclosedContainerKeepsItsContent() {
        val l = RinConsoleFormatter.formatLines("$PIPE container.pipe = P\nx\n[Error line 2]: boom")
        val b = RinContainerTree.build(l)[0] as RinOutputNode.Block
        assertFalse(b.isClosed); assertEquals(2, b.children.size)
    }

    @Test fun strayCloseStaysAPlainLeaf() {
        val l = RinConsoleFormatter.formatLines("$OK .end/container.pipe (P)")
        val t = RinContainerTree.build(l)
        assertTrue(t.single() is RinOutputNode.Leaf)
    }

    @Test fun innerUnclosedBlockIsDroppedWhenOuterCloses() {
        val l = RinConsoleFormatter.formatLines(
            "$SECTION Section = A\n$PIPE container.pipe = P\nx\n$END .end/Section (A)"
        )
        val a = RinContainerTree.build(l)[0] as RinOutputNode.Block
        assertTrue(a.isClosed)
        assertFalse((a.children[0] as RinOutputNode.Block).isClosed)
    }

    @Test fun collapseRepeatsNeverMergesContainerLines() {
        val l = RinConsoleFormatter.formatLines(
            "$PIPE container.pipe = P\n$OK .end/container.pipe (P)\n$PIPE container.pipe = P\n$OK .end/container.pipe (P)"
        )
        assertEquals(4, l.collapseRepeats().size)
        assertEquals(2, RinOutputSummary.of(l).containers)
    }

    @Test fun headlineMentionsContainers() {
        val l = RinConsoleFormatter.formatLines("$PIPE container.pipe = P\nx\n$OK .end/container.pipe (P)")
        assertEquals("اكتمل التشغيل بنجاح — 3 أسطر في حاوية واحدة", RinOutputSummary.of(l).headline(true))
    }

    @Test fun treeTextExport() {
        val l = RinConsoleFormatter.formatLines(
            "$GROUP Containers.Group = G\nhi\n$OK .end/Containers.Group (G) [تحتوي: a, b]"
        )
        assertEquals(
            ContainerFamily.GROUP.glyph + " مجموعة = G\n│ hi\n└ انتهت [تحتوي: a, b]",
            RinContainerTree.toTreeText(l)
        )
    }
}

package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** عمليات تحرير Markdown الخالصة (MarkdownEdit) — بلا واجهة ولا JNI. */
class MarkdownEditTest {

    private fun run(doc: String, e: SmartEdit.Edit?): String = SmartEdit.apply(doc, e!!)

    @Test fun boldWrapsSelection() {
        assertEquals("**hello** world", run("hello world", MarkdownEdit.toggleInline("hello world", 0, 5, "**", "text")))
    }

    @Test fun boldUnwrapsWhenAlreadyWrapped() {
        assertEquals("hello world", run("**hello** world", MarkdownEdit.toggleInline("**hello** world", 2, 7, "**", "text")))
    }

    @Test fun boldUsesWordUnderCaret() {
        assertEquals("**hello**", run("hello", MarkdownEdit.toggleInline("hello", 2, 2, "**", "text")))
    }

    @Test fun boldOnEmptyInsertsPlaceholderAndSelectsIt() {
        val e = MarkdownEdit.toggleInline("", 0, 0, "**", "text")
        assertEquals("**text**", SmartEdit.apply("", e))
        assertEquals(2, e.selStart); assertEquals(6, e.selEnd)
    }

    @Test fun headingAddsChangesAndRemoves() {
        assertEquals("# title", run("title", MarkdownEdit.heading("title", 0, 0, 1)))
        assertEquals("title", run("# title", MarkdownEdit.heading("# title", 0, 0, 1)))
        assertEquals("# a", run("## a", MarkdownEdit.heading("## a", 0, 0, 1)))
    }

    @Test fun bulletListTogglesOnAndOffAcrossLines() {
        val on = run("a\nb", MarkdownEdit.toggleLineKind("a\nb", 0, 3, MarkdownEdit.LineKind.BULLET))
        assertEquals("- a\n- b", on)
        assertEquals("a\nb", run(on, MarkdownEdit.toggleLineKind(on, 0, on.length, MarkdownEdit.LineKind.BULLET)))
    }

    @Test fun numberedListIsSequential() {
        assertEquals("1. a\n2. b", run("a\nb", MarkdownEdit.toggleLineKind("a\nb", 0, 3, MarkdownEdit.LineKind.NUMBER)))
    }

    @Test fun bulletConvertsToTask() {
        assertEquals("- [ ] a", run("- a", MarkdownEdit.toggleLineKind("- a", 0, 0, MarkdownEdit.LineKind.TASK)))
    }

    @Test fun checkboxFlips() {
        assertEquals("- [x] a", run("- [ ] a", MarkdownEdit.toggleCheckbox("- [ ] a", 0)))
        assertNull(MarkdownEdit.toggleCheckbox("plain", 0))
    }

    @Test fun enterContinuesBulletNumberAndTask() {
        assertEquals("- item\n- ", run("- item", MarkdownEdit.continueList("- item", 6)))
        assertEquals("1. one\n2. ", run("1. one", MarkdownEdit.continueList("1. one", 6)))
        assertEquals("- [x] done\n- [ ] ", run("- [x] done", MarkdownEdit.continueList("- [x] done", 10)))
    }

    @Test fun enterOnEmptyItemEndsTheList() {
        assertEquals("", run("- ", MarkdownEdit.continueList("- ", 2)))
    }

    @Test fun enterIsNormalOutsideListsMidLineAndInsideFences() {
        assertNull(MarkdownEdit.continueList("hello", 5))
        assertNull(MarkdownEdit.continueList("- item", 3))
        assertNull(MarkdownEdit.continueList("```\n- a", 7))
    }

    @Test fun codeBlockWrapsSelectedLines() {
        assertEquals("```\na\nb\n```", run("a\nb", MarkdownEdit.codeBlock("a\nb", 0, 3)))
    }

    @Test fun linkUsesWordAsLabelAndSelectsUrl() {
        val e = MarkdownEdit.link("word", 2, 2, false, "text", "url")
        assertEquals("[word](url)", SmartEdit.apply("word", e))
        assertEquals(7, e.selStart); assertEquals(10, e.selEnd)
    }

    @Test fun indentAndOutdentListItems() {
        assertEquals("  - a", run("- a", MarkdownEdit.shiftIndent("- a", 0, 0, 1)))
        assertEquals("- a", run("  - a", MarkdownEdit.shiftIndent("  - a", 0, 0, -1)))
        assertEquals("  - a", run("- a", MarkdownEdit.indentListItem("- a", 3)))
        assertNull(MarkdownEdit.indentListItem("hello", 5))
    }

    @Test fun enterInTableStartsNewRowOrEndsTable() {
        assertEquals("| a | b |\n|   |   |", run("| a | b |", MarkdownEdit.continueList("| a | b |", 9)))
        assertEquals("", run("|   |   |", MarkdownEdit.continueList("|   |   |", 9)))
    }

    @Test fun formatTableAlignsColumns() {
        val doc = "|a|bb|\n|-|-|\n|ccc|d|"
        val e = MarkdownEdit.formatTable(doc, 0)!!
        assertEquals("| a   | bb  |\n| --- | --- |\n| ccc | d   |", SmartEdit.apply(doc, e))
        assertEquals(2, e.selStart)
        assertNull(MarkdownEdit.formatTable("hello", 0))
    }

    @Test fun formatTableHonoursAlignmentMarkers() {
        val doc = "| a | b |\n| :-: | --: |\n| x | y |"
        assertEquals("|  a  |   b |\n| :-: | --: |\n|  x  |   y |", run(doc, MarkdownEdit.formatTable(doc, 0)))
    }

    @Test fun headingsSkipFencedCode() {
        val hs = MarkdownEdit.headings("# A\n```\n# no\n```\n## B ##")
        assertEquals(listOf(MarkdownEdit.Heading(0, 1, "A"), MarkdownEdit.Heading(4, 2, "B")), hs)
    }

    @Test fun tocLinksEveryHeading() {
        val doc = "# Title\n## Sub Part\ntext"
        assertEquals(
            doc + "\n\n- [Title](#title)\n  - [Sub Part](#sub-part)\n",
            run(doc, MarkdownEdit.insertToc(doc, doc.length))
        )
        assertNull(MarkdownEdit.insertToc("no headings", 0))
    }

    @Test fun statsCountWordsAndHeadings() {
        val st = MarkdownEdit.stats("hello world\n# T")
        assertEquals(4, st.words); assertEquals(15, st.chars); assertEquals(12, st.charsNoSpaces)
        assertEquals(2, st.lines); assertEquals(1, st.headings); assertEquals(1, st.readingMinutes)
    }
}

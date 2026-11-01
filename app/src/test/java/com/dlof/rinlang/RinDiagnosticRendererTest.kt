package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Test

// يتطلب في build.gradle.kts: testImplementation("junit:junit:4.13.2") و testImplementation("org.json:json:20240303")
class RinDiagnosticRendererTest {

    private fun diag(
        line: Int = 2, column: Int = 7, endLine: Int = line, endColumn: Int = column + 1,
        hints: List<String> = listOf("declare it first"),
    ) = RinDiagnostic(
        severity = "error", code = "E0001", codeName = "UndefinedVariable",
        message = "undefined variable 'x'", file = "main.rin",
        line = line, column = column, endLine = endLine, endColumn = endColumn,
        reason = null, expected = null, found = null,
        notes = emptyList(), hints = hints, suggestions = emptyList(), causedBy = emptyList(),
    )

    private val src = "let a = 1;\nprint x + a;\nprint 2;"

    @Test fun rendersSnippetWithCaretUnderExactColumn() {
        val expected = listOf(
            "error[E0001]: undefined variable 'x'",
            " --> main.rin:2:7",
            "  |",
            "1 | let a = 1;",
            "2 | print x + a;",
            "  |       ^",
            "3 | print 2;",
            "  = help: declare it first",
        ).joinToString("\n")
        assertEquals(expected, RinDiagnosticRenderer.render(diag(), src))
    }

    @Test fun tabsAreExpandedSoCaretStaysAligned() {
        val out = RinDiagnosticRenderer.render(diag(column = 2, endColumn = 4), "a\n\tfoo bar\nz")
        val lines = out.lines()
        assertEquals("2 |     foo bar", lines[4])
        assertEquals("  |     ^^", lines[5])
    }

    @Test fun caretIsClampedToEndOfLine() {
        val out = RinDiagnosticRenderer.render(diag(column = 11, endColumn = 40), src)
        assertEquals("  |           ^^", out.lines()[5])
    }

    @Test fun multiLineSpanGetsSingleCaret() {
        val out = RinDiagnosticRenderer.render(diag(column = 7, endLine = 3, endColumn = 3), src)
        assertEquals("  |       ^", out.lines()[5])
    }

    @Test fun lineOutOfRangeOrNoSourceFallsBackToHeaderOnly() {
        val noLoc = diag(line = 0, column = 0, endLine = 0, endColumn = 0)
        assertEquals(
            "error[E0001]: undefined variable 'x'\n = help: declare it first",
            RinDiagnosticRenderer.render(noLoc, null),
        )
        val out = RinDiagnosticRenderer.render(diag(line = 99), src)
        assertEquals(false, out.contains(" | "))
    }

    @Test fun shortFormIsSingleLine() {
        assertEquals(
            "main.rin:2:7: error[E0001]: undefined variable 'x'",
            RinDiagnosticRenderer.renderShort(diag()),
        )
    }

    @Test fun snippetOmitsTextFieldsButKeepsCaret() {
        val expected = listOf(
            "error[E0001]: undefined variable 'x'",
            " --> main.rin:2:7",
            "  |",
            "1 | let a = 1;",
            "2 | print x + a;",
            "  |       ^",
            "3 | print 2;",
        ).joinToString("\n")
        assertEquals(expected, RinDiagnosticRenderer.renderSnippet(diag(), src))
    }

    @Test fun snippetSourceIsOnlyUsedForTheMainInput() {
        assertEquals(src, RinDiagnosticRenderer.snippetSource(diag().copy(file = "<input>"), src))
        assertEquals(null, RinDiagnosticRenderer.snippetSource(diag().copy(file = "lib/math.og.rin"), src))
    }
}

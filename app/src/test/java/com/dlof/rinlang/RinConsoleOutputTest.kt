package com.dlof.rinlang

import org.junit.Assert.*
import org.junit.Test
import java.util.Locale

// يستخدم LogKind (المرتبط بـ R.drawable) فيعمل ضمن وحدة app نفسها.
class RinConsoleOutputTest {

    private val OK = "\u2705"
    private val WARN = "\u26A0\uFE0F"

    @Test fun indentationIsRecordedButTextStaysTrimmed() {
        val l = RinConsoleFormatter.formatLines("  $OK done\n\tplain")
        assertEquals(LogKind.SUCCESS, l[0].kind); assertEquals("done", l[0].text); assertEquals(2, l[0].indent)
        assertEquals(LogKind.PLAIN, l[1].kind); assertEquals("plain", l[1].text); assertEquals(4, l[1].indent)
    }

    @Test fun errorLineNumberIsExtractedOnlyForNumberedErrors() {
        val l = RinConsoleFormatter.formatLines("[Error line 12]: boom\n[Error]: top-level\n[\"a\",\"b\"]")
        assertEquals(LogKind.ERROR, l[0].kind); assertEquals(12, l[0].errorLine)
        assertEquals(LogKind.ERROR, l[1].kind); assertNull(l[1].errorLine)
        assertEquals(LogKind.PLAIN, l[2].kind)
    }

    @Test fun crlfOutputIsHandled() {
        val l = RinConsoleFormatter.formatLines("$OK a\r\n$OK b\r\n")
        assertEquals(listOf("a", "b"), l.map { it.text })
    }

    @Test fun formatBytesIgnoresDeviceLocale() {
        val old = Locale.getDefault()
        try {
            Locale.setDefault(Locale("ar"))
            assertEquals("1.5 KB", RinConsoleFormatter.formatBytes(1536))
            assertEquals("2.00 GB", RinConsoleFormatter.formatBytes(2L * 1024 * 1024 * 1024))
            assertEquals("0 بايت", RinConsoleFormatter.formatBytes(-5))
        } finally { Locale.setDefault(old) }
    }

    @Test fun collapseRepeatsMergesOnlyConsecutiveEqualLines() {
        val lines = RinConsoleFormatter.formatLines("hi\nhi\nhi\nbye\nhi")
        val c = lines.collapseRepeats()
        assertEquals(listOf("hi", "bye", "hi"), c.map { it.text })
        assertEquals(listOf(3, 1, 1), c.map { it.repeat })
        assertEquals(5, RinOutputSummary.of(c).totalLines)
    }

    @Test fun truncateMiddleKeepsHeadAndTail() {
        val lines = (1..10).map { RinLogLine(LogKind.PLAIN, "l$it") }
        val t = lines.truncateMiddle(4)
        assertEquals(6, t.hiddenCount)
        assertEquals(listOf("l1", "l2"), t.lines.take(2).map { it.text })
        assertEquals(LogKind.INFO, t.lines[2].kind)
        assertEquals(listOf("l9", "l10"), t.lines.takeLast(2).map { it.text })
        assertEquals(0, lines.truncateMiddle(10).hiddenCount)
    }

    @Test fun levelFilterKeepsWarningsAndAbove() {
        val lines = RinConsoleFormatter.formatLines("plain\n$WARN w\n[Error line 1]: e")
        assertEquals(listOf(LogKind.WARNING, LogKind.ERROR), lines.atLeast(LogLevel.WARNING).map { it.kind })
    }

    @Test fun headlines() {
        val ok = RinOutputSummary.of(RinConsoleFormatter.formatLines("$OK a\n$OK b"))
        assertEquals("اكتمل التشغيل بنجاح — سطران", ok.headline(true))
        val warn = RinOutputSummary.of(RinConsoleFormatter.formatLines("$WARN w"))
        assertEquals("اكتمل التشغيل بنجاح — سطر واحد مع تحذير واحد", warn.headline(true))
        val bad = RinOutputSummary.of(RinConsoleFormatter.formatLines("[Error line 4]: x"))
        assertEquals("فشل التشغيل — خطأ واحد (السطر 4)", bad.headline(false))
        assertEquals("اكتمل التشغيل بنجاح بلا مخرجات", RinOutputSummary.of(emptyList()).headline(true))
    }

    @Test fun plainTextExportIsColumnar() {
        val lines = listOf(
            RinLogLine(LogKind.SUCCESS, "done"),
            RinLogLine(LogKind.ERROR, "[Error line 4]: x", errorLine = 4, repeat = 3),
            RinLogLine(LogKind.PLAIN, "text", indent = 2),
        )
        assertEquals(
            "[OK]    done\n[ERROR] [Error line 4]: x  ×3\n          text",
            RinConsoleExport.toPlainText(lines),
        )
    }

    @Test fun markdownFenceGrowsWhenOutputContainsBackticks() {
        val md = RinConsoleExport.toMarkdown(listOf(RinLogLine(LogKind.PLAIN, "a ``` b")), success = true)
        assertTrue(md.contains("````\n"))
    }
}

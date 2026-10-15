package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** تحليل ألوان ANSI (RinAnsi) وربطه بسجل الـ terminal — بلا واجهة ولا JNI. */
class RinAnsiTest {

    private val esc = "\u001b"

    @Test fun plainTextIsOneUncoloredRun() {
        val runs = RinAnsi.parse("مرحبا Rin ✓")
        assertEquals(1, runs.size)
        assertEquals("مرحبا Rin ✓", runs[0].text)
        assertNull(runs[0].fg)
    }

    @Test fun truecolorFromColorAnsiBecomesForeground() {
        // نفس ما تُنتجه colorAnsi(text, "#3fb950") في المحرك.
        val runs = RinAnsi.parse("$esc[38;2;63;185;80mنص عربي$esc[0m بعده")
        assertEquals(listOf("نص عربي", " بعده"), runs.map { it.text })
        assertEquals(0xFF3FB950.toInt(), runs[0].fg)
        assertNull(runs[1].fg)
    }

    @Test fun basicAndBrightColorsAndBackground() {
        val runs = RinAnsi.parse("$esc[31mred$esc[92mgreen$esc[44mblue-bg$esc[39;49mplain")
        assertEquals(4, runs.size)
        assertFalse(runs[0].fg == runs[1].fg)
        assertEquals(runs[1].fg, runs[2].fg)
        assertTrue(runs[2].bg != null)
        assertNull(runs[3].fg)
        assertNull(runs[3].bg)
    }

    @Test fun palette256CubeAndGray() {
        assertEquals(0xFFFF0000.toInt(), RinAnsi.parse("$esc[38;5;196mx").single().fg)
        assertEquals(0xFF080808.toInt(), RinAnsi.parse("$esc[38;5;232mx").single().fg)
    }

    @Test fun boldItalicUnderlineToggle() {
        val runs = RinAnsi.parse("$esc[1;3;4ma$esc[22;23;24mb")
        assertTrue(runs[0].bold && runs[0].italic && runs[0].underline)
        assertFalse(runs[1].bold || runs[1].italic || runs[1].underline)
    }

    @Test fun bareResetAndEmptyParamsReset() {
        val runs = RinAnsi.parse("$esc[31ma$esc[mb")
        assertNull(runs[1].fg)
    }

    @Test fun nonColorSequencesAreDroppedNotShown() {
        val hostile = "a$esc[2Jb$esc[10;5Hc$esc]0;title\u0007d$esc]8;;http://x$esc\\e\u0001f\u0085g"
        assertEquals("abcdefg", RinAnsi.strip(hostile))
    }

    @Test fun truncatedSequenceNeverThrows() {
        assertEquals("a", RinAnsi.strip("a$esc"))
        assertEquals("a", RinAnsi.strip("a$esc["))
        assertEquals("a", RinAnsi.strip("a$esc[38;2;1"))
        assertEquals("", RinAnsi.strip(""))
    }

    @Test fun tabIsKeptOtherControlsDropped() {
        assertEquals("a\tb", RinAnsi.strip("a\tb\u0000\r\u007f"))
    }

    @Test fun bufferCopyAndGrepSeeTextWithoutCodes() {
        val b = RinTerminalBuffer()
        b.onChunk("$esc[38;2;63;185;80mثقة 93%$esc[0m\n")
        assertEquals("ثقة 93%", b.toPlainText())
        assertEquals(listOf("1: ثقة 93%"), b.grep("93%"))
        // النص الخام يبقى في السطر ليرسمه الـ view ملوّناً.
        assertTrue(b.snapshot().lines.single().text.contains(esc))
    }

    @Test fun errorLineStillLinkedWhenColored() {
        val b = RinTerminalBuffer()
        b.onChunk("$esc[31m[Error line 7]: boom$esc[0m\n")
        assertEquals(7, b.snapshot().lines.single().errorLine)
    }
}

package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** منطق سجل الـ terminal (RinTerminalBuffer) — بلا واجهة ولا JNI. */
class RinTerminalBufferTest {

    private fun texts(b: RinTerminalBuffer) = b.snapshot().lines.map { it.text }

    @Test fun chunksSplitIntoLinesAndKeepPartial() {
        val b = RinTerminalBuffer()
        b.onChunk("a\nb")
        assertEquals(listOf("a"), texts(b))
        assertEquals("b", b.snapshot().partial)
        b.onChunk("c\n")
        assertEquals(listOf("a", "bc"), texts(b))
        assertEquals("", b.snapshot().partial)
    }

    @Test fun promptAndAnswerShareOneLine() {
        val b = RinTerminalBuffer()
        b.onChunk("hello\n")
        b.prompt("Name: ")
        assertEquals("Name: ", b.snapshot().partial)
        b.answer("Ahmed")
        val last = b.snapshot().lines.last()
        assertEquals("Name: Ahmed", last.text)
        assertEquals(6, last.answerFrom)
        assertEquals("", b.snapshot().partial)
    }

    @Test fun cancelledInputWritesCtrlCAndNotice() {
        val b = RinTerminalBuffer()
        b.prompt("Age: ")
        b.answer(null)
        assertEquals(listOf("Age: ^C", "input cancelled"), texts(b))
    }

    @Test fun multiLinePromptSplitsAtNewlines() {
        val b = RinTerminalBuffer()
        b.prompt("pick:\n1) a\n2) b\n> ")
        assertEquals(listOf("pick:", "1) a", "2) b"), texts(b))
        assertEquals("> ", b.snapshot().partial)
    }

    @Test fun finishAppendsOnlyWhatWasNotStreamed() {
        val b = RinTerminalBuffer()
        b.startJob(1)
        b.onChunk("one\n")
        b.finishJob(JobStatus.ERROR, "one\nerror: boom\n", 12)
        assertEquals(listOf("── run #1 ──", "one", "error: boom", "✗ failed after 12 ms"), texts(b))
    }

    @Test fun finishDoesNotDuplicateFullyStreamedOutput() {
        val b = RinTerminalBuffer()
        b.startJob(2)
        b.onChunk("x\n")
        b.finishJob(JobStatus.SUCCESS, "x\n", 5)
        assertEquals(listOf("── run #2 ──", "x", "✓ finished in 5 ms"), texts(b))
    }

    @Test fun finishShowsWholeOutputWhenNothingStreamed() {
        val b = RinTerminalBuffer()
        b.startJob(3)
        b.finishJob(JobStatus.ERROR, "[Error line 1]: unexpected token\n", 1)
        assertTrue(texts(b).contains("[Error line 1]: unexpected token"))
    }

    @Test fun commandsAreRecordedInHistoryWithoutConsecutiveDuplicates() {
        val b = RinTerminalBuffer()
        b.command("run"); b.command("run"); b.command("help")
        assertEquals(listOf("run", "help"), b.history())
        assertTrue(b.toPlainText().startsWith("$ run"))
    }

    @Test fun trimmingKeepsMostRecentLines() {
        val b = RinTerminalBuffer(maxLines = 10)
        for (i in 1..500) b.onChunk("l$i\n")
        val t = texts(b)
        assertTrue(t.size <= 210)
        assertEquals("l500", t.last())
    }

    @Test fun clearEmptiesEverything() {
        val b = RinTerminalBuffer()
        b.onChunk("a\nb")
        b.clear()
        assertTrue(b.isEmpty())
    }
}

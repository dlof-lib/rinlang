package com.dlof.rinlang

import org.junit.Assert.*
import org.junit.Test

// محرّك الكتابة الحيّة لشاشة Output (OutputActivity): منطق صرف، بلا واجهة.
class RinOutputTypewriterTest {

    private fun plain(text: String) = RinLogLine(LogKind.PLAIN, text)

    private fun drain(w: RinOutputTypewriter, frameMs: Long = 16, limitMs: Long = 60_000): Long {
        var t = 0L
        while (w.isTyping && t < limitMs) { w.step(frameMs); t += frameMs }
        return t
    }

    @Test fun typesCharacterByCharacterAndFinishes() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("hello")))
        assertTrue(w.isTyping)
        assertEquals("", w.rows[0].shownText())
        drain(w)
        assertFalse(w.isTyping)
        assertEquals("hello", w.rows[0].shownText())
    }

    @Test fun partialTextIsAlwaysAPrefix() {
        val w = RinOutputTypewriter()
        val text = "مرحبا بالعالم ✅ done"
        w.appendLines(listOf(plain(text)))
        while (w.isTyping) {
            w.step(16)
            assertTrue(text.startsWith(w.rows[0].shownText()))
        }
        assertEquals(text, w.rows[0].shownText())
    }

    @Test fun neverSplitsASurrogatePair() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("a😀b")))
        while (w.isTyping) {
            w.step(8)
            val s = w.rows[0].shownText()
            assertFalse("dangling high surrogate in '$s'", s.isNotEmpty() && Character.isHighSurrogate(s.last()))
        }
    }

    @Test fun rowsAreTypedInOrderOnlyOneAtATime() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("aaaa"), plain("bbbb"), plain("cccc")))
        while (w.isTyping) {
            w.step(16)
            // كل ما بعد الصف الجاري لم يبدأ، وكل ما قبله اكتمل
            for (i in w.rows.indices) {
                when {
                    i < w.cursorRow -> assertTrue(w.rows[i].isDone)
                    i > w.cursorRow -> assertEquals(0, w.rows[i].typed)
                }
            }
        }
    }

    @Test fun instantRowsAppearCompleteWithoutAnimation() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("old1"), plain("old2"), plain("new")), instantCount = 2)
        assertTrue(w.rows[0].isDone && w.rows[1].isDone)
        assertEquals(2, w.cursorRow)
        assertTrue(w.isTyping)
    }

    @Test fun instantSpeedCompletesInOneStep() {
        val w = RinOutputTypewriter()
        w.speed = TypeSpeed.INSTANT
        w.appendLines((1..50).map { plain("line $it") })
        assertFalse(w.isTyping)
    }

    @Test fun skipAllFinishesEverything() {
        val w = RinOutputTypewriter()
        w.appendLines((1..20).map { plain("line number $it") })
        val ch = w.skipAll()
        assertNotNull(ch)
        assertFalse(w.isTyping)
        assertTrue(w.rows.all { it.isDone })
        assertNull(w.skipAll())
    }

    @Test fun longOutputDrainsInFewSeconds() {
        val w = RinOutputTypewriter()
        w.appendLines((1..500).map { plain("x".repeat(40)) })
        val t = drain(w)
        assertFalse(w.isTyping)
        assertTrue("took ${t}ms", t < 8_000)
    }

    @Test fun shortOutputStaysReadable() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("x".repeat(30))))
        val t = drain(w)
        // 30 حرفاً بسرعة ×1 (120/ث) ≈ 250ms: ليست فورية ولا بطيئة جداً
        assertTrue("took ${t}ms", t in 150..800)
    }

    @Test fun appendingWhileTypingAndAfterIdleBothWork() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("first line")))
        repeat(5) { w.step(16) }
        w.appendLines(listOf(plain("second line")))
        drain(w)
        assertEquals("second line", w.rows[1].shownText())
        w.appendLines(listOf(plain("third")))
        assertTrue(w.isTyping)
        assertEquals(2, w.cursorRow)
        drain(w)
        assertEquals("third", w.rows[2].shownText())
    }

    @Test fun lineNumbersCountOutputLinesOnly() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("a"), plain("b")))
        w.appendSynthetic(OutRowKind.STATUS, plain("done"))
        w.appendLines(listOf(plain("c")))
        assertEquals(listOf(1, 2, 0, 3), w.rows.map { it.number })
        assertEquals(3, w.lineCount)
    }

    @Test fun containerDepthFollowsOpenAndClose() {
        val w = RinOutputTypewriter()
        val open = RinContainerMark(ContainerRole.OPEN, "container.table", "users")
        val close = RinContainerMark(ContainerRole.CLOSE, "container.table", "users")
        w.appendLines(
            listOf(
                RinLogLine(LogKind.STRUCTURE, "container.table = users", container = open),
                plain("row 1"),
                RinLogLine(LogKind.SUCCESS, "container.table = users", container = close),
                plain("after")
            )
        )
        assertEquals(listOf(0, 1, 0, 0), w.rows.map { it.depth })
        assertNotNull(w.rows[1].railColor)
        assertNull(w.rows[3].railColor)
    }

    @Test fun repeatedLineShowsCounter() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(RinLogLine(LogKind.PLAIN, "tick", repeat = 3)))
        assertEquals("tick  ×3", w.rows[0].text)
    }

    @Test fun resetClearsEverything() {
        val w = RinOutputTypewriter()
        w.appendLines(listOf(plain("a"), plain("b")))
        w.reset()
        assertTrue(w.rows.isEmpty())
        assertFalse(w.isTyping)
        assertEquals(0, w.lineCount)
        w.appendLines(listOf(plain("c")))
        assertEquals(1, w.rows[0].number)
    }
}

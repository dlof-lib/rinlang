package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** منطق جلسة الـ REPL (RinReplSession) — بلا واجهة ولا JNI. */
class RinReplSessionTest {

    @Test fun balanceIgnoresStringsAndComments() {
        assertEquals(1, RinReplSession.balance("fun f() {"))
        assertEquals(0, RinReplSession.balance("print \"(\";"))
        assertEquals(0, RinReplSession.balance("let a = 1; // {"))
    }

    @Test fun incompleteCommandAsksForMoreThenCompletes() {
        val s = RinReplSession()
        assertTrue(s.feed("fun f() {") is RinReplSession.Feed.NeedMore)
        assertTrue(s.isContinuing)
        val done = s.feed("}") as RinReplSession.Feed.Ready
        assertEquals("fun f() {\n}", done.display)
        assertTrue(!s.isContinuing)
    }

    @Test fun expressionsAreWrappedInPrintButStatementsAreNot() {
        val s = RinReplSession()
        assertEquals("print (1 + 2);", s.buildSource("1 + 2"))
        assertEquals("print (x);", s.buildSource("x"))
        assertEquals("let x = 5;", s.buildSource("let x = 5"))
        assertEquals("print 1;", s.buildSource("print 1;"))
    }

    @Test fun declarationsPersistAcrossCommands() {
        val s = RinReplSession()
        s.commit("let x = 5;")
        assertEquals("let x = 5;\nprint (x * 2);", s.buildSource("x * 2"))
        assertEquals(listOf("x"), s.declaredNames())
    }

    @Test fun redeclaringReplacesOldValue() {
        val s = RinReplSession()
        s.commit("let x = 1;")
        s.commit("let x = 2;")
        assertEquals("let x = 2;", s.preludeSource())
    }

    @Test fun redeclaringDropsStaleAssignments() {
        val s = RinReplSession()
        s.commit("let x = 1;")
        s.commit("x = x + 1;")
        assertEquals("let x = 1;\nx = x + 1;", s.preludeSource())
        s.commit("let x = 10;")
        assertEquals("let x = 10;", s.preludeSource())
    }

    @Test fun impureDeclarationsAreNeverReplayed() {
        val s = RinReplSession()
        s.commit("let n = input(\"name\");")
        assertTrue(s.declaredNames().isEmpty())
        s.commit("let k = 1;")
        s.commit("let k = input(\"k\");")
        assertTrue(s.declaredNames().isEmpty())
    }

    @Test fun resetForgetsEverything() {
        val s = RinReplSession()
        s.commit("let x = 5;")
        s.reset()
        assertEquals("print (x);", s.buildSource("x"))
    }

    @Test fun completeSuggestsSessionNamesAndExtraWords() {
        val s = RinReplSession()
        s.commit("let counter = 0;")
        assertEquals(listOf("count", "counter"), s.complete("cou", listOf("count")))
        assertTrue(s.complete("", listOf("count")).isEmpty())
    }
}

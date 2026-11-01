package com.dlof.rinlang

import org.junit.Assert.*
import org.junit.Test

// يتطلب في build.gradle.kts: testImplementation("junit:junit:4.13.2") و testImplementation("org.json:json:20240303")
// (org.json داخل android.jar مجرّد stubs في اختبارات JVM). لا يلمس JNI ولا Choreographer.
class IndsinEventTest {
    @Test fun parsesFullEnvelope() {
        val e = IndsinEvent.parse("""{"ok":true,"handled":true,"targetId":7,"changed":["count"],"animating":true,"fabric":{"id":1}}""")
        assertTrue(e.ok); assertTrue(e.handled); assertEquals(7, e.targetId)
        assertEquals(listOf("count"), e.changed); assertTrue(e.animating)
        assertNotNull(e.fabricJson); assertNull(e.error); assertTrue(e.needsRedraw)
    }

    @Test fun unhandledTapNeedsNoRedraw() {
        val e = IndsinEvent.parse("""{"ok":true,"handled":false}""")
        assertTrue(e.ok); assertFalse(e.handled); assertNull(e.targetId); assertFalse(e.needsRedraw)
    }

    @Test fun runtimeErrorIsKept() {
        val e = IndsinEvent.parse("""{"ok":true,"handled":true,"targetId":2,"error":"division by zero"}""")
        assertEquals("division by zero", e.error)
    }

    @Test fun closedSessionEnvelopeIsFailure() {
        val e = IndsinEvent.parse("""{"ok":false,"error":"session closed"}""")
        assertFalse(e.ok); assertEquals("session closed", e.error); assertFalse(e.needsRedraw)
    }

    @Test fun garbageNeverThrows() {
        for (bad in listOf(null, "", "   ", "not json", "[1,2]", "{")) {
            val e = IndsinEvent.parse(bad)
            assertFalse("input=$bad", e.ok); assertNotNull(e.error)
        }
    }

    @Test fun colorRoundTrip() {
        val argb = parseIndsinColor("#336699")
        assertNotNull(argb); assertEquals("#336699", formatIndsinColor(argb!!).lowercase())
        assertNull(parseIndsinColor("nope")); assertNull(parseIndsinColor(null))
    }
}

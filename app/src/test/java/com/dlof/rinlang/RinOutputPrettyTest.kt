package com.dlof.rinlang

import org.junit.Assert.*
import org.junit.Test

class RinOutputPrettyTest {
    @Test fun shortJsonStaysOnOneLine() = assertNull(RinOutputPretty.json("{\"a\":1}"))

    @Test fun nonJsonIsUntouched() {
        assertNull(RinOutputPretty.json("x".repeat(100)))
        assertNull(RinOutputPretty.json("{ unbalanced " + "y".repeat(80)))
    }

    @Test fun longJsonIsIndentedOneElementPerLine() {
        val src = "{\"name\":\"Droy\",\"tags\":[\"a\",\"b,c\"],\"nested\":{\"x\":1.5,\"y\":[],\"z\":{}},\"k\":[1,2,3]}"
        val out = RinOutputPretty.json(src)!!
        val lines = out.lines()
        assertEquals("{", lines.first())
        assertEquals("}", lines.last())
        assertTrue(lines.contains("  \"name\": \"Droy\","))
        assertTrue(lines.contains("    \"b,c\""))      // الفاصلة داخل النص لا تقسم السطر
        assertTrue(lines.contains("    \"y\": [],"))    // الفارغ يبقى مدمجاً
        // نفس المحتوى بعد حذف المسافات/الأسطر خارج النصوص
        assertEquals(src.replace(" ", ""), out.replace(" ", "").replace("\n", ""))
    }
}

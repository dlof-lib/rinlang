package com.dlof.rinlang

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** محرك البحث/الاستبدال (FileSearch/FileReplace) — JVM خالص. */
class FileSearchTest {
    private fun tree(): File {
        val root = Files.createTempDirectory("rinsearch").toFile()
        File(root, "src").mkdirs()
        File(root, "build").mkdirs()
        File(root, "main.rin").writeText("let count = 1\nprint(count)\nlet Counter = 2\n")
        File(root, "src/MainActivity.kt").writeText("class MainActivity {\n    val x = count\n}\n")
        File(root, "src/notes.md").writeText("nothing here\n")
        File(root, "build/out.rin").writeText("count count\n")
        File(root, "bin.dat").writeBytes(byteArrayOf(99, 111, 117, 110, 116, 0, 1))
        return root
    }

    private fun o(q: String, mode: SearchMode = SearchMode.BOTH, cs: Boolean = false, w: Boolean = false, re: Boolean = false) =
        SearchOptions(q, mode, cs, w, re)

    @Test fun contentMatchesWithLines() {
        val r = FileSearch.search(tree(), o("count", SearchMode.CONTENT))
        val main = r.hits.first { it.file.relPath == "main.rin" }
        assertEquals(3, main.totalMatches) // count, count, Counter (غير حسّاس للحالة)
        assertEquals(1, main.firstLine)
        assertNull(r.hits.firstOrNull { it.file.name == "bin.dat" })
    }

    @Test fun caseAndWholeWord() {
        val cs = FileSearch.search(tree(), o("count", SearchMode.CONTENT, cs = true))
        assertEquals(2, cs.hits.first { it.file.relPath == "main.rin" }.totalMatches)
        val ww = FileSearch.search(tree(), o("count", SearchMode.CONTENT, w = true))
        assertEquals(2, ww.hits.first { it.file.relPath == "main.rin" }.totalMatches)
    }

    @Test fun regexAndInvalidRegex() {
        val r = FileSearch.search(tree(), o("""let \w+ = \d""", SearchMode.CONTENT, re = true))
        assertEquals(2, r.hits.first { it.file.relPath == "main.rin" }.totalMatches)
        assertNotNull(FileSearch.search(tree(), o("(", SearchMode.CONTENT, re = true)).error)
    }

    @Test fun filtersInQuery() {
        val r = FileSearch.search(tree(), o("count ext:kt", SearchMode.CONTENT))
        assertEquals(listOf("src/MainActivity.kt"), r.hits.map { it.file.relPath })
        val r2 = FileSearch.search(tree(), o("count -in:build ext:rin", SearchMode.CONTENT))
        assertEquals(listOf("main.rin"), r2.hits.map { it.file.relPath })
        // مرشّح بلا نص = سرد الملفات
        assertEquals(2, FileSearch.search(tree(), o("ext:rin")).hits.size)
    }

    @Test fun nameRankingAndFuzzy() {
        val r = FileSearch.search(tree(), o("main", SearchMode.NAMES))
        assertEquals("main.rin", r.hits[0].file.name) // بادئة أقصر من MainActivity.kt
        val f = FileSearch.search(tree(), o("mnact", SearchMode.NAMES))
        assertEquals("MainActivity.kt", f.hits.single().file.name)
        assertTrue(f.hits.single().nameRanges.isNotEmpty())
    }

    @Test fun scopeFolder() {
        val r = FileSearch.search(tree(), SearchOptions("count", SearchMode.CONTENT, scopeRel = "src"))
        assertEquals(listOf("src/MainActivity.kt"), r.hits.map { it.file.relPath })
    }

    @Test fun cancelStopsEarly() {
        val r = FileSearch.search(tree(), o("count"), cancelled = { true })
        assertEquals(0, r.hits.size)
    }

    @Test fun previewHighlightOffsets() {
        val line = "    " + "x".repeat(60) + "NEEDLE" + "y".repeat(100)
        val m = FileSearch.makeMatch(7, line, 64, 70)
        assertEquals("NEEDLE", m.preview.substring(m.hlStart, m.hlEnd))
        assertEquals(7, m.line)
    }

    @Test fun replaceLiteralRegexAndUndo() {
        val root = tree()
        val opts = o("count", SearchMode.CONTENT, cs = true)
        val hits = FileSearch.search(root, opts).hits
        val out = FileReplace.apply(hits, opts, "total", skip = setOf("build/out.rin"))
        assertEquals(2, out.files) // main.rin + MainActivity.kt
        assertEquals(listOf("build/out.rin"), out.skipped)
        assertEquals("let total = 1\nprint(total)\nlet Counter = 2\n", File(root, "main.rin").readText())
        assertEquals("count count\n", File(root, "build/out.rin").readText())
        assertEquals(2, FileReplace.undo(out.backups))
        assertEquals("let count = 1\nprint(count)\nlet Counter = 2\n", File(root, "main.rin").readText())
    }

    @Test fun replaceRegexGroupsKeepCrlf() {
        val (s, n) = FileReplace.replaceLines("a=1\r\nb=2\n", Regex("(\\w)=(\\d)"), "$2=$1")
        assertEquals("1=a\r\n2=b\n", s)
        assertEquals(2, n)
        assertEquals("empty-match", FileReplace.validate(o("a*", re = true)))
    }
}

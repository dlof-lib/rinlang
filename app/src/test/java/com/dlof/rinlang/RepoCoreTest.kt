package com.dlof.rinlang

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.nio.file.Files
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** منطق استرداد المستودعات الخالص (RepoCore) — بلا أندرويد. */
class RepoCoreTest {

    private fun tmp(): File = Files.createTempDirectory("repo_test").toFile()

    private fun zip(vararg entries: Pair<String, String?>): ByteArray {
        val bos = ByteArrayOutputStream()
        ZipOutputStream(bos).use { z ->
            for ((name, text) in entries) {
                z.putNextEntry(ZipEntry(name))
                if (text != null) z.write(text.toByteArray())
                z.closeEntry()
            }
        }
        return bos.toByteArray()
    }

    // ---- RepoParser ----

    @Test fun githubUrlForms() {
        val a = RepoParser.parse("https://github.com/owner/repo")!!
        assertEquals(RepoProvider.GITHUB, a.provider); assertEquals("owner/repo", a.path); assertEquals("", a.ref)
        assertEquals("owner/repo", RepoParser.parse("owner/repo")!!.path)
        assertEquals("owner/repo", RepoParser.parse("github.com/owner/repo.git")!!.path)
        assertEquals("owner/repo", RepoParser.parse("git@github.com:owner/repo.git")!!.path)
        assertEquals("owner/repo", RepoParser.parse("github:owner/repo")!!.path)
        assertEquals("owner/repo", RepoParser.parse("http://www.github.com/owner/repo/")!!.path)
    }

    @Test fun githubTreeGivesRefAndSubPath() {
        val s = RepoParser.parse("https://github.com/o/r/tree/dev/src/lib")!!
        assertEquals("dev", s.ref); assertEquals("src/lib", s.subPath)
        assertEquals("v1.2", RepoParser.parse("https://github.com/o/r/releases/tag/v1.2")!!.ref)
        assertEquals("abc123", RepoParser.parse("https://github.com/o/r/commit/abc123")!!.ref)
    }

    @Test fun otherProviders() {
        val gl = RepoParser.parse("https://gitlab.com/group/sub/proj/-/tree/main/docs")!!
        assertEquals(RepoProvider.GITLAB, gl.provider); assertEquals("group/sub/proj", gl.path)
        assertEquals("main", gl.ref); assertEquals("docs", gl.subPath)
        val bb = RepoParser.parse("https://bitbucket.org/o/r/src/main/app")!!
        assertEquals(RepoProvider.BITBUCKET, bb.provider); assertEquals("main", bb.ref); assertEquals("app", bb.subPath)
        val cb = RepoParser.parse("https://codeberg.org/o/r/src/branch/dev")!!
        assertEquals(RepoProvider.GITEA, cb.provider); assertEquals("dev", cb.ref)
    }

    @Test fun directZipAndRejects() {
        val d = RepoParser.parse("https://example.com/files/pack.zip")!!
        assertEquals(RepoProvider.DIRECT, d.provider); assertEquals("pack", d.repoName)
        assertNull(RepoParser.parse(""))
        assertNull(RepoParser.parse("not a url"))
        assertNull(RepoParser.parse("https://example.com/o/r"))
        assertNull(RepoParser.parse("https://github.com/onlyowner"))
        assertNull(RepoParser.parse("ftp://github.com/o/r"))
        assertNull(RepoParser.parse("https://github.com/o/r/tree/a..b"))
    }

    @Test fun refValidation() {
        assertTrue(RepoParser.isValidRef(""))
        assertTrue(RepoParser.isValidRef("feature/x-1.0"))
        assertFalse(RepoParser.isValidRef("a..b"))
        assertFalse(RepoParser.isValidRef("/a"))
        assertFalse(RepoParser.isValidRef("a b"))
        assertFalse(RepoParser.isValidRef("x?y=1"))
    }

    // ---- RepoLinks ----

    @Test fun archiveUrls() {
        val gh = RepoSpec(RepoProvider.GITHUB, "github.com", "o/r")
        assertEquals("https://github.com/o/r/archive/HEAD.zip", RepoLinks.archiveUrl(gh, false))
        assertEquals("https://api.github.com/repos/o/r/zipball", RepoLinks.archiveUrl(gh, true))
        assertEquals("https://github.com/o/r/archive/feature/x.zip", RepoLinks.archiveUrl(gh.copy(ref = "feature/x"), false))
        val gl = RepoSpec(RepoProvider.GITLAB, "gitlab.com", "g/s/p", ref = "main")
        assertEquals("https://gitlab.com/api/v4/projects/g%2Fs%2Fp/repository/archive.zip?sha=main", RepoLinks.archiveUrl(gl, false))
        assertEquals("https://bitbucket.org/o/r/get/HEAD.zip", RepoLinks.archiveUrl(RepoSpec(RepoProvider.BITBUCKET, "bitbucket.org", "o/r"), false))
        assertEquals("https://codeberg.org/o/r/archive/main.zip", RepoLinks.archiveUrl(RepoSpec(RepoProvider.GITEA, "codeberg.org", "o/r", ref = "main"), false))
    }

    @Test fun authHeadersAreEmptyWithoutToken() {
        val gh = RepoSpec(RepoProvider.GITHUB, "github.com", "o/r")
        assertTrue(RepoLinks.authHeaders(gh, "").isEmpty())
        assertTrue(RepoLinks.authHeaders(gh, "bad token").isEmpty())
        assertEquals("Bearer abc", RepoLinks.authHeaders(gh, "abc")["Authorization"])
    }

    // ---- RepoArchive ----

    @Test fun extractStripsTopFolderAndSkipsGit() {
        val out = File(tmp(), "tree")
        val r = RepoArchive.extractZip(
            ByteArrayInputStream(zip("repo-main/" to null, "repo-main/a.txt" to "A", "repo-main/src/b.txt" to "B", "repo-main/.git/config" to "x")),
            out
        )
        assertEquals(2, r.files)
        assertEquals("repo-main", RepoArchive.stripSingleTopFolder(out))
        assertEquals("A", File(out, "a.txt").readText())
        assertEquals("B", File(out, "src/b.txt").readText())
        assertFalse(File(out, ".git").exists())
    }

    @Test fun zipSlipIsRejected() {
        val out = File(tmp(), "tree")
        try {
            RepoArchive.extractZip(ByteArrayInputStream(zip("a/../../evil.txt" to "x")), out)
        } catch (e: RepoMessage) { /* غير مهم: المسار يُهمل أو يُرفض */ }
        assertFalse(File(out.parentFile, "evil.txt").exists())
        assertEquals(0, RepoArchive.countFiles(out))
    }

    @Test fun limitsAreEnforced() {
        val out = File(tmp(), "tree")
        try {
            RepoArchive.extractZip(ByteArrayInputStream(zip("a.txt" to "0123456789")), out, RepoLimits(maxFileBytes = 5))
            fail("expected RepoTooBig")
        } catch (e: RepoTooBig) { /* ok */ }
    }

    @Test fun narrowFindsSubFolderAndRefusesEscape() {
        val root = tmp()
        File(root, "docs/guide").mkdirs()
        assertNotNull(RepoArchive.narrow(root, "docs/guide"))
        assertNull(RepoArchive.narrow(root, "missing"))
        assertNull(RepoArchive.narrow(root, "../x"))
        assertEquals(root, RepoArchive.narrow(root, ""))
    }

    // ---- RepoMerge ----

    private fun tree(base: File, vararg files: Pair<String, String>): File {
        for ((p, t) in files) File(base, p).also { it.parentFile.mkdirs() }.writeText(t)
        return base
    }

    private val noProtect: (String) -> Boolean = { it.startsWith(".rin_") }

    @Test fun addOnlyKeepsExisting() {
        val src = tree(tmp(), "a.txt" to "new-a", "b.txt" to "b")
        val dest = tree(tmp(), "a.txt" to "old-a")
        val rep = RepoMerge.merge(src, dest, MergeMode.ADD_ONLY, File(tmp(), "bak"), noProtect)
        assertEquals(1, rep.added); assertEquals(1, rep.skipped); assertEquals(0, rep.updated)
        assertEquals("old-a", File(dest, "a.txt").readText())
        assertEquals("b", File(dest, "b.txt").readText())
    }

    @Test fun overwriteBacksUpReplacedFile() {
        val src = tree(tmp(), "a.txt" to "new-a", "same.txt" to "s")
        val dest = tree(tmp(), "a.txt" to "old-a", "same.txt" to "s", "mine.txt" to "m")
        val bak = File(tmp(), "bak")
        val rep = RepoMerge.merge(src, dest, MergeMode.OVERWRITE, bak, noProtect)
        assertEquals(1, rep.updated); assertEquals(1, rep.unchanged); assertEquals(0, rep.removed)
        assertEquals("new-a", File(dest, "a.txt").readText())
        assertEquals("old-a", File(bak, "a.txt").readText())
        assertEquals("m", File(dest, "mine.txt").readText())
    }

    @Test fun mirrorMovesExtrasToBackupButKeepsProtected() {
        val src = tree(tmp(), "a.txt" to "a")
        val dest = tree(tmp(), "a.txt" to "a", "extra/x.txt" to "x", ".rin_repo.json" to "{}")
        val bak = File(tmp(), "bak")
        val rep = RepoMerge.merge(src, dest, MergeMode.MIRROR, bak, noProtect)
        assertEquals(1, rep.removed)
        assertFalse(File(dest, "extra").exists())
        assertEquals("x", File(bak, "extra/x.txt").readText())
        assertTrue(File(dest, ".rin_repo.json").exists())
    }

    @Test fun protectedFilesAreNeverOverwritten() {
        val src = tree(tmp(), "project.og.urin" to "evil")
        val dest = tree(tmp(), "project.og.urin" to "mine")
        val rep = RepoMerge.merge(src, dest, MergeMode.OVERWRITE, File(tmp(), "bak")) { it == "project.og.urin" }
        assertEquals(1, rep.skipped)
        assertEquals("mine", File(dest, "project.og.urin").readText())
    }

    @Test fun dryRunReportsButWritesNothing() {
        val src = tree(tmp(), "a.txt" to "new", "n.txt" to "n")
        val dest = tree(tmp(), "a.txt" to "old", "x.txt" to "x")
        val bak = File(tmp(), "bak")
        val rep = RepoMerge.merge(src, dest, MergeMode.MIRROR, bak, noProtect, dryRun = true)
        assertEquals(1, rep.added); assertEquals(1, rep.updated); assertEquals(1, rep.removed)
        assertEquals("old", File(dest, "a.txt").readText())
        assertFalse(File(dest, "n.txt").exists())
        assertTrue(File(dest, "x.txt").exists())
        assertFalse(bak.exists())
    }

    @Test fun commitApiUrls() {
        val gh = RepoSpec(RepoProvider.GITHUB, "github.com", "o/r")
        assertEquals("https://api.github.com/repos/o/r/commits/HEAD", RepoLinks.commitApi(gh)!!.url)
        assertEquals(0, RepoLinks.commitApi(gh.copy(ref = "dev"))!!.kind)
        val ge = RepoSpec(RepoProvider.GITEA, "codeberg.org", "o/r", ref = "dev")
        assertEquals("https://codeberg.org/api/v1/repos/o/r/commits?limit=1&stat=false&sha=dev", RepoLinks.commitApi(ge)!!.url)
        assertNull(RepoLinks.commitApi(RepoSpec(RepoProvider.DIRECT, "x.com", "p", directUrl = "https://x.com/p.zip")))
    }

    // ---- tar.gz ----

    private fun tarEntry(out: ByteArrayOutputStream, name: String, content: String?, type: Char = '0') {
        val h = ByteArray(512)
        val nb = name.toByteArray()
        System.arraycopy(nb, 0, h, 0, minOf(nb.size, 100))
        val data = content?.toByteArray() ?: ByteArray(0)
        val sz = String.format("%011o", data.size).toByteArray()
        System.arraycopy(sz, 0, h, 124, sz.size)
        h[156] = type.code.toByte()
        out.write(h)
        out.write(data)
        val pad = (512 - data.size % 512) % 512
        out.write(ByteArray(pad))
    }

    private fun tgz(build: (ByteArrayOutputStream) -> Unit): ByteArray {
        val tar = ByteArrayOutputStream()
        build(tar)
        tar.write(ByteArray(1024))
        val bos = ByteArrayOutputStream()
        java.util.zip.GZIPOutputStream(bos).use { it.write(tar.toByteArray()) }
        return bos.toByteArray()
    }

    @Test fun tarGzExtractsFilesAndAutoDetects() {
        val data = tgz {
            tarEntry(it, "repo-1/", null, '5')
            tarEntry(it, "repo-1/a.txt", "hello")
            tarEntry(it, "repo-1/src/b.txt", "x".repeat(700))
            tarEntry(it, "repo-1/.git/config", "no")
            tarEntry(it, "repo-1/link", null, '2')
        }
        val out = File(tmp(), "tree")
        val r = RepoArchive.extractAuto(ByteArrayInputStream(data), out)
        assertEquals(2, r.files)
        assertEquals("repo-1", RepoArchive.stripSingleTopFolder(out))
        assertEquals("hello", File(out, "a.txt").readText())
        assertEquals(700, File(out, "src/b.txt").length().toInt())
        assertFalse(File(out, ".git").exists())
        assertFalse(File(out, "link").exists())
    }

    @Test fun tarGzRejectsTraversalAndDirectLinkParses() {
        val out = File(tmp(), "tree")
        RepoArchive.extractTarGz(ByteArrayInputStream(tgz { tarEntry(it, "../evil.txt", "x") }), out)
        assertFalse(File(out.parentFile, "evil.txt").exists())
        val d = RepoParser.parse("https://example.com/dl/pkg-1.0.tar.gz")!!
        assertEquals(RepoProvider.DIRECT, d.provider); assertEquals("pkg-1.0", d.repoName)
        assertEquals("pkg", RepoParser.parse("https://example.com/pkg.tgz")!!.repoName)
    }
}

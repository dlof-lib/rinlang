package com.dlof.rinlang

import java.io.File
import java.io.FilterInputStream
import java.io.IOException
import java.io.InputStream
import java.net.URL
import java.net.URLDecoder
import java.net.URLEncoder
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream

/*
 * نواة «استرداد المستودعات» — منطق خالص (بلا أندرويد) كي يُختبر بوحدات JVM:
 *
 *   • RepoParser  : يفهم روابط GitHub / GitLab / Bitbucket / Codeberg-Gitea-Forgejo وروابط ZIP المباشرة،
 *                   وصيغ owner/repo و git@host:o/r.git و github:o/r، ويستخرج الفرع والمجلد الفرعي من الرابط.
 *   • RepoLinks   : يبني رابط الأرشيف (ZIP) ورؤوس المصادقة وروابط واجهات سرد الفروع/الوسوم.
 *   • RepoArchive : فكّ ZIP بأمان (Zip Slip، حدود العدد والحجم)، إزالة المجلد الأعلى الوحيد، تضييق إلى مجلد فرعي.
 *   • RepoMerge   : دمج شجرة مستردّة داخل مجلد موجود بثلاث طرق، مع نسخة احتياطية لكل ما يُستبدل أو يُزال.
 *
 * الرمز السرّي (token) لا يمرّ هنا إلا كنص يُحوَّل إلى رؤوس؛ لا يُخزَّن ولا يُسجَّل في أي مكان.
 */

enum class RepoProvider { GITHUB, GITLAB, BITBUCKET, GITEA, DIRECT }

/**
 * مواصفة مستودع بعد التحليل.
 * [path] = owner/repo (وفي GitLab قد يكون group/sub/repo). [ref] فرع أو وسم أو commit (فارغ = الافتراضي).
 * [subPath] مجلد فرعي داخل المستودع يُسترد وحده (فارغ = المستودع كله). [directUrl] لروابط ZIP المباشرة فقط.
 */
data class RepoSpec(
    val provider: RepoProvider,
    val host: String,
    val path: String,
    val ref: String = "",
    val subPath: String = "",
    val directUrl: String = ""
) {
    val repoName: String get() = path.substringAfterLast('/')
    val owner: String get() = path.substringBeforeLast('/', "")
    fun displayName(): String =
        if (provider == RepoProvider.DIRECT) host + "/" + repoName else host + "/" + path
}

class RepoCancelled : RuntimeException()
class RepoHttpException(val code: Int) : IOException("HTTP $code")
class RepoTooBig : IOException("repository too big")
class RepoMessage(message: String) : IOException(message)

// =================================================================================================
// تحليل الروابط
// =================================================================================================

object RepoParser {

    private val NAME = Regex("^[A-Za-z0-9_.~-]{1,100}$")
    private val REF = Regex("^[A-Za-z0-9._/+@#~-]{1,200}$")
    private val SHORT = Regex("^[A-Za-z0-9_.-]{1,100}/[A-Za-z0-9_.-]{1,100}$")
    private val SCP = Regex("^git@([A-Za-z0-9.-]+):(.+)$")
    private val PREFIXED = Regex("^(github|gitlab|bitbucket|codeberg|gitea):(?!//)(.+)$", RegexOption.IGNORE_CASE)
    private val PREFIX_HOST = mapOf(
        "github" to "github.com",
        "gitlab" to "gitlab.com",
        "bitbucket" to "bitbucket.org",
        "codeberg" to "codeberg.org",
        "gitea" to "gitea.com"
    )
    private val SCHEMES = setOf("https", "http", "ssh", "git", "git+https")

    /** اسم فرع/وسم/commit صالح (أو فارغ = الافتراضي). يمنع كل ما قد يكسر الرابط أو يخرج عن المسار. */
    fun isValidRef(ref: String): Boolean {
        if (ref.isEmpty()) return true
        if (!REF.matches(ref)) return false
        if (ref.contains("..") || ref.contains("//")) return false
        if (ref.startsWith("/") || ref.endsWith("/") || ref.endsWith(".lock")) return false
        return true
    }

    /** يعيد null إن كان النص ليس مستودعاً مدعوماً ولا رابط ZIP مباشراً. */
    fun parse(raw: String): RepoSpec? {
        var s = raw.trim()
        if (s.isEmpty() || s.any { it.isWhitespace() }) return null

        val scp = SCP.matchEntire(s)
        if (scp != null) s = "https://" + scp.groupValues[1] + "/" + scp.groupValues[2]

        val pre = PREFIXED.matchEntire(s)
        if (pre != null) {
            val h = PREFIX_HOST[pre.groupValues[1].lowercase()] ?: return null
            s = "https://" + h + "/" + pre.groupValues[2].trimStart('/')
        }

        if (!s.contains("://")) {
            val firstSeg = s.substringBefore('/')
            s = if (SHORT.matches(s) && !firstSeg.contains('.')) "https://github.com/$s" else "https://$s"
        }

        val schemeEnd = s.indexOf("://")
        val scheme = s.substring(0, schemeEnd).lowercase()
        if (scheme !in SCHEMES) return null
        s = "https://" + s.substring(schemeEnd + 3)

        val url: URL = try { URL(s) } catch (e: Exception) { return null }
        val hostName = url.host.orEmpty().lowercase().removePrefix("www.")
        if (hostName.isEmpty() || !hostName.contains('.')) return null
        val host = if (url.port != -1) "$hostName:${url.port}" else hostName

        val rawPath = url.path.orEmpty()
        val segs: List<String> = try {
            rawPath.split('/').filter { it.isNotEmpty() }
                .map { URLDecoder.decode(it.replace("+", "%2B"), "UTF-8") }
        } catch (e: Exception) { return null }

        // رابط أرشيف مباشر ZIP أو TAR.GZ/TGZ (من أي مضيف https)
        val lowPath = rawPath.lowercase()
        val archiveExt = listOf(".tar.gz", ".tgz", ".zip").firstOrNull { lowPath.endsWith(it) }
        if (archiveExt != null && segs.isNotEmpty()) {
            val direct = URL("https", url.host, url.port, url.file).toString()
            val name = segs.last().substring(0, segs.last().length - archiveExt.length).ifBlank { "download" }
            return RepoSpec(RepoProvider.DIRECT, host, name, directUrl = direct)
        }

        val provider = when {
            hostName == "github.com" -> RepoProvider.GITHUB
            hostName == "gitlab.com" || hostName.startsWith("gitlab.") -> RepoProvider.GITLAB
            hostName == "bitbucket.org" -> RepoProvider.BITBUCKET
            hostName == "codeberg.org" || hostName == "gitea.com" ||
                hostName.startsWith("gitea.") || hostName.contains("forgejo") -> RepoProvider.GITEA
            else -> return null
        }

        var pathSegs: List<String>
        var ref = ""
        var sub = ""
        when (provider) {
            RepoProvider.GITHUB -> {
                if (segs.size < 2) return null
                pathSegs = listOf(segs[0], segs[1])
                when (segs.getOrNull(2)) {
                    "tree" -> if (segs.size > 3) { ref = segs[3]; sub = segs.drop(4).joinToString("/") }
                    "blob" -> if (segs.size > 3) ref = segs[3]
                    "commit", "commits" -> ref = segs.getOrNull(3).orEmpty()
                    "releases" -> if (segs.getOrNull(3) == "tag") ref = segs.getOrNull(4).orEmpty()
                }
            }
            RepoProvider.GITLAB -> {
                val dash = segs.indexOf("-")
                pathSegs = if (dash >= 0) segs.subList(0, dash) else segs
                if (pathSegs.size < 2) return null
                val tail = if (dash >= 0) segs.drop(dash + 1) else emptyList()
                when (tail.getOrNull(0)) {
                    "tree" -> if (tail.size > 1) { ref = tail[1]; sub = tail.drop(2).joinToString("/") }
                    "blob" -> if (tail.size > 1) ref = tail[1]
                    "commit", "tags" -> ref = tail.getOrNull(1).orEmpty()
                }
            }
            RepoProvider.BITBUCKET -> {
                if (segs.size < 2) return null
                pathSegs = listOf(segs[0], segs[1])
                when (segs.getOrNull(2)) {
                    "src" -> if (segs.size > 3) { ref = segs[3]; sub = segs.drop(4).joinToString("/") }
                    "branch", "commits" -> ref = segs.getOrNull(3).orEmpty()
                }
            }
            else -> { // GITEA / Forgejo / Codeberg
                if (segs.size < 2) return null
                pathSegs = listOf(segs[0], segs[1])
                when (segs.getOrNull(2)) {
                    "src" -> {
                        val kind = segs.getOrNull(3)
                        if ((kind == "branch" || kind == "tag" || kind == "commit") && segs.size > 4) {
                            ref = segs[4]; sub = segs.drop(5).joinToString("/")
                        }
                    }
                    "commit" -> ref = segs.getOrNull(3).orEmpty()
                    "releases" -> if (segs.getOrNull(3) == "tag") ref = segs.getOrNull(4).orEmpty()
                }
            }
        }

        pathSegs = pathSegs.toMutableList().also {
            it[it.size - 1] = it[it.size - 1].removeSuffix(".git")
        }
        if (pathSegs.any { !NAME.matches(it) || it == "." || it == ".." }) return null
        if (!isValidRef(ref)) return null
        val subSegs = sub.split('/').filter { it.isNotEmpty() }
        if (subSegs.any { it == ".." || it == "." || it.contains('\u0000') || it.contains('\\') }) return null

        return RepoSpec(provider, host, pathSegs.joinToString("/"), ref, subSegs.joinToString("/"))
    }
}

// =================================================================================================
// روابط الأرشيف والمصادقة
// =================================================================================================

object RepoLinks {

    /** يرمّز كل مقطع من مسار مع إبقاء '/' (أسماء الفروع تحوي '/' أحياناً). */
    fun encodePath(p: String): String =
        p.split('/').joinToString("/") { URLEncoder.encode(it, "UTF-8").replace("+", "%20") }

    fun encodeOne(p: String): String = URLEncoder.encode(p, "UTF-8").replace("+", "%20")

    /**
     * رابط تنزيل الأرشيف. [hasToken]: مع الرمز نستخدم واجهة API الرسمية (تدعم المستودعات الخاصة)،
     * وبدونه نستخدم روابط الأرشيف العامة (لا تُحتسب ضمن حدّ طلبات API).
     * مواصفة GITEA يجب أن يكون لها [RepoSpec.ref] غير فارغ (يُحلّ الافتراضي قبل الاستدعاء).
     */
    fun archiveUrl(spec: RepoSpec, hasToken: Boolean): String {
        val ref = spec.ref
        return when (spec.provider) {
            RepoProvider.DIRECT -> spec.directUrl
            RepoProvider.GITHUB ->
                if (hasToken) {
                    "https://api.github.com/repos/${spec.path}/zipball" + (if (ref.isEmpty()) "" else "/" + encodePath(ref))
                } else {
                    "https://github.com/${spec.path}/archive/" + (if (ref.isEmpty()) "HEAD" else encodePath(ref)) + ".zip"
                }
            RepoProvider.GITLAB ->
                "https://${spec.host}/api/v4/projects/${encodeOne(spec.path)}/repository/archive.zip" +
                    (if (ref.isEmpty()) "" else "?sha=" + encodeOne(ref))
            RepoProvider.BITBUCKET ->
                "https://${spec.host}/${spec.path}/get/" + (if (ref.isEmpty()) "HEAD" else encodePath(ref)) + ".zip"
            RepoProvider.GITEA ->
                "https://${spec.host}/${spec.path}/archive/" + encodePath(ref.ifEmpty { "HEAD" }) + ".zip"
        }
    }

    /** رؤوس المصادقة. تُرسَل إلى مضيف الطلب الأول فقط (انظر RepoNet.connect: تُسقَط عند التحويل لمضيف آخر). */
    fun authHeaders(spec: RepoSpec, token: String): Map<String, String> {
        val t = token.trim()
        if (t.isEmpty() || t.any { it.isWhitespace() || it.code < 32 }) return emptyMap()
        return when (spec.provider) {
            RepoProvider.GITHUB -> mapOf("Authorization" to "Bearer $t")
            RepoProvider.GITLAB -> mapOf("PRIVATE-TOKEN" to t)
            RepoProvider.BITBUCKET -> mapOf("Authorization" to "Bearer $t")
            RepoProvider.GITEA -> mapOf("Authorization" to "token $t")
            RepoProvider.DIRECT -> mapOf("Authorization" to "Bearer $t")
        }
    }

    class RefsApi(val branches: String, val tags: String, val arrayKey: String?)

    /** روابط سرد الفروع والوسوم (أول 100)، أو null إن لم يكن ذلك ممكناً (رابط ZIP مباشر). */
    fun refsApi(spec: RepoSpec): RefsApi? = when (spec.provider) {
        RepoProvider.GITHUB -> RefsApi(
            "https://api.github.com/repos/${spec.path}/branches?per_page=100",
            "https://api.github.com/repos/${spec.path}/tags?per_page=100", null
        )
        RepoProvider.GITLAB -> {
            val id = encodeOne(spec.path)
            RefsApi(
                "https://${spec.host}/api/v4/projects/$id/repository/branches?per_page=100",
                "https://${spec.host}/api/v4/projects/$id/repository/tags?per_page=100", null
            )
        }
        RepoProvider.BITBUCKET -> RefsApi(
            "https://api.bitbucket.org/2.0/repositories/${spec.path}/refs/branches?pagelen=100",
            "https://api.bitbucket.org/2.0/repositories/${spec.path}/refs/tags?pagelen=100", "values"
        )
        RepoProvider.GITEA -> RefsApi(
            "https://${spec.host}/api/v1/repos/${spec.path}/branches?limit=50",
            "https://${spec.host}/api/v1/repos/${spec.path}/tags?limit=50", null
        )
        RepoProvider.DIRECT -> null
    }

    /** kind: 0 = نص SHA خام، 1 = JSON {"id"}، 2 = JSON مصفوفة [{"sha"}]، 3 = JSON {"values":[{"hash"}]}. */
    class CommitApi(val url: String, val accept: String, val kind: Int)

    /** رابط آخر commit للفرع (أو الافتراضي) للتحقق من وجود تحديث؛ null لروابط ZIP المباشرة. */
    fun commitApi(spec: RepoSpec): CommitApi? = when (spec.provider) {
        RepoProvider.GITHUB -> CommitApi(
            "https://api.github.com/repos/${spec.path}/commits/" + encodePath(spec.ref.ifEmpty { "HEAD" }),
            "application/vnd.github.sha", 0
        )
        RepoProvider.GITLAB -> CommitApi(
            "https://${spec.host}/api/v4/projects/${encodeOne(spec.path)}/repository/commits/" + encodeOne(spec.ref.ifEmpty { "HEAD" }),
            "application/json", 1
        )
        RepoProvider.BITBUCKET -> CommitApi(
            "https://api.bitbucket.org/2.0/repositories/${spec.path}/commits" +
                (if (spec.ref.isEmpty()) "" else "/" + encodePath(spec.ref)) + "?pagelen=1",
            "application/json", 3
        )
        RepoProvider.GITEA -> CommitApi(
            "https://${spec.host}/api/v1/repos/${spec.path}/commits?limit=1&stat=false" +
                (if (spec.ref.isEmpty()) "" else "&sha=" + encodeOne(spec.ref)),
            "application/json", 2
        )
        RepoProvider.DIRECT -> null
    }

    fun giteaRepoInfoUrl(spec: RepoSpec): String = "https://${spec.host}/api/v1/repos/${spec.path}"
}

// =================================================================================================
// فكّ الأرشيف بأمان
// =================================================================================================

class RepoLimits(
    val maxEntries: Int = 20_000,
    val maxTotalBytes: Long = 256L * 1024 * 1024,
    val maxFileBytes: Long = 100L * 1024 * 1024
)

/** يعدّ البايتات المقروءة من التدفق (لشريط التقدّم). */
class CountingInputStream(input: InputStream, private val onCount: (Long) -> Unit) : FilterInputStream(input) {
    private var count = 0L

    override fun read(): Int {
        val b = super.read()
        if (b >= 0) { count++; onCount(count) }
        return b
    }

    override fun read(b: ByteArray, off: Int, len: Int): Int {
        val n = super.read(b, off, len)
        if (n > 0) { count += n; onCount(count) }
        return n
    }
}

object RepoArchive {

    class Extracted(val files: Int, val bytes: Long)

    /** مسار نسبي آمن بفواصل '/' أو null (مطلق، يحوي ':' أو '..' أو NUL). */
    fun safeRelPath(raw: String): String? {
        val n = raw.replace('\\', '/')
        if (n.isBlank() || n.startsWith("/") || n.contains(':') || n.contains('\u0000')) return null
        val segs = n.split('/').filter { it.isNotEmpty() && it != "." }
        if (segs.isEmpty() || segs.any { it == ".." }) return null
        return segs.joinToString("/")
    }

    /**
     * يفكّ ZIP من [input] داخل [dest]. يرمي RepoMessage عند مسار غير آمن، وRepoTooBig عند تجاوز الحدود،
     * وRepoCancelled عند الإلغاء. عناصر `.git` تُتجاهل.
     */
    fun extractZip(
        input: InputStream,
        dest: File,
        limits: RepoLimits = RepoLimits(),
        isCancelled: () -> Boolean = { false }
    ): Extracted {
        dest.mkdirs()
        val root = dest.canonicalFile
        var files = 0
        var total = 0L
        var entries = 0
        val buf = ByteArray(32 * 1024)
        ZipInputStream(input).use { zip ->
            var entry: ZipEntry? = zip.nextEntry
            while (entry != null) {
                val cur: ZipEntry = entry
                if (isCancelled()) throw RepoCancelled()
                entries++
                if (entries > limits.maxEntries) throw RepoTooBig()
                val rel = safeRelPath(cur.name)
                if (rel != null && rel.split('/').none { it.equals(".git", ignoreCase = true) }) {
                    val out = File(root, rel).canonicalFile
                    if (out.path != root.path && !out.path.startsWith(root.path + File.separator)) {
                        throw RepoMessage("unsafe path in archive: ${cur.name}")
                    }
                    if (cur.isDirectory) {
                        out.mkdirs()
                    } else {
                        out.parentFile?.mkdirs()
                        var fileBytes = 0L
                        out.outputStream().buffered().use { o ->
                            while (true) {
                                if (isCancelled()) throw RepoCancelled()
                                val n = zip.read(buf)
                                if (n < 0) break
                                fileBytes += n
                                total += n
                                if (fileBytes > limits.maxFileBytes || total > limits.maxTotalBytes) throw RepoTooBig()
                                o.write(buf, 0, n)
                            }
                        }
                        files++
                    }
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        return Extracted(files, total)
    }

    /** يكتشف نوع الأرشيف من أول بايتين: gzip (1f 8b) ← tar.gz، وإلا ← zip. */
    fun extractAuto(
        input: InputStream,
        dest: File,
        limits: RepoLimits = RepoLimits(),
        isCancelled: () -> Boolean = { false }
    ): Extracted {
        val bin = java.io.BufferedInputStream(input, 64 * 1024)
        bin.mark(4)
        val b0 = bin.read()
        val b1 = bin.read()
        bin.reset()
        return if (b0 == 0x1f && b1 == 0x8b) extractTarGz(bin, dest, limits, isCancelled)
        else extractZip(bin, dest, limits, isCancelled)
    }

    private fun skipFully(s: InputStream, n: Long) {
        var left = n
        while (left > 0) {
            val k = s.skip(left)
            if (k > 0) { left -= k; continue }
            if (s.read() < 0) throw IOException("truncated archive")
            left--
        }
    }

    private fun readBlock(s: InputStream, buf: ByteArray): Boolean {
        var off = 0
        while (off < buf.size) {
            val n = s.read(buf, off, buf.size - off)
            if (n < 0) { if (off == 0) return false; throw IOException("truncated archive") }
            off += n
        }
        return true
    }

    private fun cstr(b: ByteArray, off: Int, len: Int): String {
        var end = off
        while (end < off + len && b[end].toInt() != 0) end++
        return String(b, off, end - off, Charsets.UTF_8)
    }

    private fun octal(b: ByteArray, off: Int, len: Int): Long {
        if ((b[off].toInt() and 0x80) != 0) { // base-256 (ملفات ضخمة)
            var v = (b[off].toInt() and 0x7f).toLong()
            for (i in 1 until len) v = (v shl 8) or (b[off + i].toLong() and 0xff)
            return v
        }
        val t = cstr(b, off, len).trim()
        return if (t.isEmpty()) 0L else t.toLongOrNull(8) ?: throw IOException("bad tar header")
    }

    private fun padded(size: Long): Long = (size + 511L) / 512L * 512L

    private fun readSmall(s: InputStream, size: Long): ByteArray {
        if (size < 0 || size > 65536) throw RepoTooBig()
        val data = ByteArray(size.toInt())
        var off = 0
        while (off < data.size) {
            val n = s.read(data, off, data.size - off)
            if (n < 0) throw IOException("truncated archive")
            off += n
        }
        skipFully(s, padded(size) - size)
        return data
    }

    /** يستخرج مسار `path=` من سجلات pax ("<len> key=value\n"). */
    private fun paxPath(data: ByteArray): String? {
        var pos = 0
        while (pos < data.size) {
            var sp = pos
            while (sp < data.size && data[sp] != ' '.code.toByte()) sp++
            if (sp >= data.size) return null
            val len = String(data, pos, sp - pos, Charsets.US_ASCII).toIntOrNull() ?: return null
            if (len <= 0 || pos + len > data.size) return null
            val rec = String(data, sp + 1, pos + len - sp - 2, Charsets.UTF_8)
            if (rec.startsWith("path=")) return rec.substring(5)
            pos += len
        }
        return null
    }

    /** فكّ tar.gz بالتدفق (ustar + GNU long names + pax path). الروابط الرمزية والملفات الخاصة تُتجاهل. */
    fun extractTarGz(
        input: InputStream,
        dest: File,
        limits: RepoLimits = RepoLimits(),
        isCancelled: () -> Boolean = { false }
    ): Extracted {
        dest.mkdirs()
        val root = dest.canonicalFile
        val tar = java.util.zip.GZIPInputStream(input)
        val hdr = ByteArray(512)
        val buf = ByteArray(32 * 1024)
        var files = 0
        var total = 0L
        var entries = 0
        var longName: String? = null
        var pax: String? = null
        while (true) {
            if (isCancelled()) throw RepoCancelled()
            if (!readBlock(tar, hdr)) break
            if (hdr.all { it.toInt() == 0 }) break
            val size = octal(hdr, 124, 12)
            val type = hdr[156].toInt().toChar()
            if (type == 'L') { longName = cstr(readSmall(tar, size), 0, size.toInt()); continue }
            if (type == 'x') { pax = paxPath(readSmall(tar, size)); continue }
            if (type == 'g') { skipFully(tar, padded(size)); continue }

            entries++
            if (entries > limits.maxEntries) throw RepoTooBig()
            val base = cstr(hdr, 0, 100)
            val ustar = cstr(hdr, 257, 5) == "ustar"
            val prefix = if (ustar) cstr(hdr, 345, 155) else ""
            val name = pax ?: longName ?: (if (prefix.isNotEmpty()) "$prefix/$base" else base)
            pax = null
            longName = null

            val isFile = type == '0' || type == '\u0000'
            val isDir = type == '5'
            val rel = if (isFile || isDir) safeRelPath(name) else null
            if (rel == null || rel.split('/').any { it.equals(".git", ignoreCase = true) }) {
                skipFully(tar, padded(size))
                continue
            }
            val out = File(root, rel).canonicalFile
            if (out.path != root.path && !out.path.startsWith(root.path + File.separator)) {
                throw RepoMessage("unsafe path in archive: $name")
            }
            if (isDir) {
                out.mkdirs()
                skipFully(tar, padded(size))
                continue
            }
            if (size > limits.maxFileBytes || total + size > limits.maxTotalBytes) throw RepoTooBig()
            out.parentFile?.mkdirs()
            var left = size
            out.outputStream().buffered().use { o ->
                while (left > 0) {
                    if (isCancelled()) throw RepoCancelled()
                    val n = tar.read(buf, 0, minOf(buf.size.toLong(), left).toInt())
                    if (n < 0) throw IOException("truncated archive")
                    o.write(buf, 0, n)
                    left -= n
                }
            }
            total += size
            files++
            skipFully(tar, padded(size) - size)
        }
        return Extracted(files, total)
    }

    /**
     * أرشيفات المستودعات تلفّ كل شيء بمجلد واحد (repo-main/). إن كان في [dir] مجلد وحيد فقط يُرفع محتواه
     * مستوى واحداً ويُعاد اسمه، وإلا لا يُغيَّر شيء (يعيد null).
     */
    fun stripSingleTopFolder(dir: File): String? {
        val kids = dir.listFiles() ?: return null
        if (kids.size != 1 || !kids[0].isDirectory) return null
        val top = kids[0]
        val tmp = File(dir, ".rin_strip_${System.nanoTime()}")
        if (!top.renameTo(tmp)) throw IOException("cannot restructure archive")
        for (child in tmp.listFiles().orEmpty()) {
            if (!child.renameTo(File(dir, child.name))) throw IOException("cannot restructure archive")
        }
        tmp.deleteRecursively()
        return top.name
    }

    /** مجلد [subPath] داخل [root] (آمناً)، أو [root] نفسه إن كان فارغاً، أو null إن لم يوجد. */
    fun narrow(root: File, subPath: String): File? {
        if (subPath.isBlank()) return root
        val rel = safeRelPath(subPath) ?: return null
        val r = root.canonicalFile
        val d = File(r, rel).canonicalFile
        if (!d.path.startsWith(r.path + File.separator) || !d.isDirectory) return null
        return d
    }

    fun countFiles(dir: File): Int = dir.walkTopDown().count { it.isFile }
}

// =================================================================================================
// الدمج مع نسخة احتياطية
// =================================================================================================

enum class MergeMode {
    /** يضيف الملفات غير الموجودة فقط ولا يلمس الموجود. */
    ADD_ONLY,
    /** يضيف الجديد ويستبدل المختلف (الأصل يُنسخ احتياطياً أولاً). */
    OVERWRITE,
    /** مطابقة تامة لحالة المستودع: كالسابق، والملفات غير الموجودة في المستودع تُنقل للنسخة الاحتياطية. */
    MIRROR
}

class MergeReport(
    val added: Int,
    val updated: Int,
    val unchanged: Int,
    val skipped: Int,
    val removed: Int
) {
    val backedUp: Int get() = updated + removed
}

object RepoMerge {

    /**
     * يدمج محتوى [src] في [dest]. كل ملف يُستبدل أو يُزال يُنسخ أولاً إلى [backupRoot] (بنفس مساره النسبي)؛
     * فإن فشل النسخ الاحتياطي تُرمى الاستثناء قبل أي كتابة فوق الملف الأصلي.
     * [isProtected] تأخذ المسار النسبي وتعيد true لما لا يجوز لمسه أبداً (يُحتسب «متخطّى»).
     */
    fun merge(
        src: File,
        dest: File,
        mode: MergeMode,
        backupRoot: File,
        isProtected: (String) -> Boolean,
        /** true: يحسب التقرير فقط دون أي كتابة أو حذف (معاينة قبل التنفيذ). */
        dryRun: Boolean = false
    ): MergeReport {
        if (!dryRun) dest.mkdirs()
        val d = dest.canonicalFile
        var added = 0
        var updated = 0
        var same = 0
        var skipped = 0
        var removed = 0
        val srcRel = HashSet<String>()

        for (f in src.walkTopDown().toList()) {
            if (!f.isFile) continue
            val rel = f.relativeTo(src).path.replace(File.separatorChar, '/')
            srcRel.add(rel)
            if (isProtected(rel)) { skipped++; continue }
            val target = File(d, rel).canonicalFile
            if (!target.path.startsWith(d.path + File.separator)) { skipped++; continue }
            if (!target.exists()) {
                if (!dryRun) {
                    target.parentFile?.mkdirs()
                    f.copyTo(target)
                }
                added++
                continue
            }
            if (target.isDirectory) { skipped++; continue }
            if (sameContent(f, target)) { same++; continue }
            if (mode == MergeMode.ADD_ONLY) { skipped++; continue }
            if (!dryRun) {
                val bak = File(backupRoot, rel)
                bak.parentFile?.mkdirs()
                target.copyTo(bak, overwrite = true)
                f.copyTo(target, overwrite = true)
            }
            updated++
        }

        if (mode == MergeMode.MIRROR) {
            for (f in d.walkTopDown().toList()) {
                if (!f.isFile) continue
                val rel = f.relativeTo(d).path.replace(File.separatorChar, '/')
                if (rel in srcRel || isProtected(rel)) continue
                if (!dryRun) {
                    val bak = File(backupRoot, rel)
                    bak.parentFile?.mkdirs()
                    f.copyTo(bak, overwrite = true)
                    if (!f.delete()) throw IOException("cannot remove ${f.name}")
                }
                removed++
            }
            // تنظيف المجلدات التي فرغت (دون مجلدات .rin_*)
            if (!dryRun) for (dir in d.walkBottomUp().toList()) {
                if (dir == d || !dir.isDirectory) continue
                val rel = dir.relativeTo(d).path.replace(File.separatorChar, '/')
                if (rel.startsWith(".rin_")) continue
                if (dir.list().isNullOrEmpty()) dir.delete()
            }
        }
        return MergeReport(added, updated, same, skipped, removed)
    }

    private fun sameContent(a: File, b: File): Boolean {
        if (a.length() != b.length()) return false
        return a.inputStream().buffered().use { x ->
            b.inputStream().buffered().use { y -> sameStreams(x, y) }
        }
    }

    private fun sameStreams(x: InputStream, y: InputStream): Boolean {
        val bx = ByteArray(16 * 1024)
        val by = ByteArray(16 * 1024)
        while (true) {
            val nx = readFully(x, bx)
            val ny = readFully(y, by)
            if (nx != ny) return false
            if (nx <= 0) return true
            for (i in 0 until nx) if (bx[i] != by[i]) return false
        }
    }

    private fun readFully(s: InputStream, buf: ByteArray): Int {
        var off = 0
        while (off < buf.size) {
            val n = s.read(buf, off, buf.size - off)
            if (n < 0) break
            off += n
        }
        return off
    }
}

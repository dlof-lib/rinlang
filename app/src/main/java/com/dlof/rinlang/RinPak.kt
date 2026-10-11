package com.dlof.rinlang

import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedOutputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.InputStream
import java.io.OutputStream
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * RinPak — حزمة مشروع Rin القابلة للنقل (الامتداد `.rinpak`، النوع `application/vnd.rin.pak`).
 *
 * الحزمة ملف ZIP بترتيب وبنية ثابتين (المواصفة الكاملة: docs/rinpak-format.md):
 *
 * ```
 * MyApp.rinpak
 * ├── mimetype                  ← أول عنصر دائماً، غير مضغوط: "application/vnd.rin.pak"
 * ├── META-INF/
 * │   ├── manifest.json         ← ثاني عنصر دائماً: الهوية + قائمة الملفات + SHA-256
 * │   ├── signature.json        ← اختياري (ثالث عنصر إن وُجد): توقيع Ed25519 على المانيفست
 * │   └── icon.png              ← اختياري
 * └── content/                  ← شجرة المشروع كما هي (main.rin, lib/, assets/...)
 * ```
 *
 * الاستيراد يتحقق من: ترتيب العناصر، mimetype، تطابق SHA-256 وحجم كل ملف مع المانيفست،
 * عدم وجود عناصر زائدة أو ناقصة، مسارات آمنة (Zip Slip)، وحدود الحجم/العدد.
 * هذا الملف لا يعتمد على Android إلا عبر org.json، فيمكن اختباره على JVM.
 */
object RinPak {

    const val EXTENSION = ".rinpak"
    const val MIME = "application/vnd.rin.pak"
    const val FORMAT = "rinpak"
    const val SPEC_VERSION = 1

    const val ENTRY_MIMETYPE = "mimetype"
    const val ENTRY_MANIFEST = "META-INF/manifest.json"
    const val ENTRY_SIGNATURE = "META-INF/signature.json"
    const val ENTRY_ICON = "META-INF/icon.png"
    /** فاصل نطاق التوقيع: يمنع إعادة استخدام توقيع المانيفست في سياق آخر. */
    private const val SIGN_DOMAIN = "RinPak-v1\u0000"
    const val CONTENT_ROOT = "content/"
    const val ENTRY_POINT = "main.rin"

    const val MAX_ENTRIES = 10_000
    const val MAX_TOTAL_BYTES = 256L * 1024L * 1024L
    const val MAX_MANIFEST_BYTES = 8 * 1024 * 1024
    /** أقصى حجم لملف واحد داخل الحزمة. */
    const val MAX_FILE_BYTES = 64L * 1024L * 1024L
    private const val FIXED_ENTRY_TIME = 315532800000L // 1980-01-01 (أقل قيمة يدعمها ZIP)

    /** امتدادات مضغوطة أصلاً: تُخزَّن STORED بدل إعادة ضغطها. */
    private val ALREADY_COMPRESSED = setOf(
        "png", "jpg", "jpeg", "webp", "gif", "heic", "mp3", "aac", "m4a", "ogg", "opus", "flac",
        "mp4", "m4v", "mkv", "webm", "mov", "zip", "jar", "apk", "gz", "xz", "7z", "rar", "woff", "woff2"
    )

    class RinPakException(message: String) : Exception(message)

    data class FileEntry(val path: String, val size: Long, val sha256: String)

    /** بيانات وصفية اختيارية تُكتب في المانيفست. */
    data class PackMeta(
        val version: String = "",
        val description: String = "",
        val author: String = "",
        val license: String = "",
        val minRinVersion: String = "",
        val permissions: List<String> = emptyList(),
        val dependencies: List<String> = emptyList()
    )

    /** موقِّع Ed25519: [publicKey] = 32 بايت خام، [sign] يعيد توقيعاً خاماً من 64 بايت. */
    interface Signer {
        val publicKey: ByteArray
        fun sign(message: ByteArray): ByteArray
    }

    /** نتيجة فحص التوقيع. [keyId] = أول 16 خانة hex من SHA-256 للمفتاح العام. */
    data class SignatureInfo(val keyId: String, val publicKey: String)

    data class Manifest(
        val name: String,
        val type: String,
        val entry: String,
        val specVersion: Int,
        val generatorName: String,
        val generatorVersion: String,
        val created: String,
        val files: List<FileEntry>,
        val root: String,
        val hasIcon: Boolean,
        val meta: PackMeta = PackMeta(),
        /** غير null فقط إذا كانت الحزمة موقّعة **وتحقق التوقيع** (التوقيع التالف يرمي استثناءً). */
        val signature: SignatureInfo? = null
    ) {
        val fileCount: Int get() = files.size
        val totalBytes: Long get() = files.sumOf { it.size }
    }

    // ───────────────────────────── كتابة (تصدير) ─────────────────────────────

    /**
     * يعبّئ [sourceDir] داخل [out] بصيغة RinPak. الملفات المخفية (أي مقطع مسار يبدأ بنقطة) تُستثنى،
     * ويُستثنى manifest الصيغة القديمة rin.project.json. الترتيب حتمي (حسب بايتات UTF-8 للمسار).
     */
    fun write(
        sourceDir: File,
        name: String,
        typeId: String,
        out: OutputStream,
        generatorVersion: String = "1.0.0",
        createdIso: String = java.time.Instant.now().toString(),
        iconPng: ByteArray? = null,
        meta: PackMeta = PackMeta(),
        signer: Signer? = null
    ): Manifest {
        val root = sourceDir.canonicalFile
        val items = root.walkTopDown()
            .filter { it.isFile && !java.nio.file.Files.isSymbolicLink(it.toPath()) }
            .map { it to it.relativeTo(root).path.replace(File.separatorChar, '/') }
            .filter { (_, rel) -> !rel.split('/').any { it.startsWith(".") } && rel != "rin.project.json" }
            .onEach { (_, rel) -> require(safePath(rel) != null) { "مسار مشروع غير آمن: $rel" } }
            .sortedBy { (_, rel) -> String(rel.toByteArray(Charsets.UTF_8), Charsets.ISO_8859_1) }
            .toList()
        if (items.none { it.second == ENTRY_POINT }) throw RinPakException("المشروع يفتقد $ENTRY_POINT")

        // مرحلة 1: SHA-256 + CRC32 + الحجم (CRC/الحجم مطلوبان لعناصر STORED).
        class Info(val file: File, val rel: String, val size: Long, val sha: String, val crc: Long)
        val infos = items.map { (file, rel) ->
            val md = MessageDigest.getInstance("SHA-256")
            val crc = CRC32()
            var size = 0L
            file.inputStream().buffered().use { input ->
                val buf = ByteArray(DEFAULT_BUF)
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    md.update(buf, 0, n); crc.update(buf, 0, n); size += n
                }
            }
            Info(file, rel, size, hex(md.digest()), crc.value)
        }
        val entries = infos.map { FileEntry(it.rel, it.size, it.sha) }
        val manifest = Manifest(
            name = name, type = typeId, entry = ENTRY_POINT, specVersion = SPEC_VERSION,
            generatorName = "RinStudio", generatorVersion = generatorVersion, created = createdIso,
            files = entries, root = rootDigest(entries), hasIcon = iconPng != null, meta = meta
        )
        checkCollisions(entries.map { it.path })
        entries.firstOrNull { it.size > MAX_FILE_BYTES }?.let { throw RinPakException("الملف أكبر من الحد المسموح: ${it.path}") }
        if (entries.sumOf { it.size } > MAX_TOTAL_BYTES) throw RinPakException("حجم المشروع يتجاوز الحد المسموح")
        val manifestBytes = manifestToJson(manifest).toString(2).toByteArray(Charsets.UTF_8)

        // مرحلة 2: الكتابة بالترتيب الثابت.
        ZipOutputStream(BufferedOutputStream(out)).use { zip ->
            putStored(zip, ENTRY_MIMETYPE, MIME.toByteArray(Charsets.US_ASCII))
            putDeflated(zip, ENTRY_MANIFEST, manifestBytes)
            if (signer != null) putDeflated(zip, ENTRY_SIGNATURE, signatureJson(signer, manifestBytes).toString(2).toByteArray(Charsets.UTF_8))
            if (iconPng != null) putStored(zip, ENTRY_ICON, iconPng)
            for (i in infos) {
                val entryName = CONTENT_ROOT + i.rel
                val ext = i.rel.substringAfterLast('.', "").lowercase()
                val e = ZipEntry(entryName).apply { time = FIXED_ENTRY_TIME }
                if (ext in ALREADY_COMPRESSED) {
                    e.method = ZipEntry.STORED; e.size = i.size; e.compressedSize = i.size; e.crc = i.crc
                } else e.method = ZipEntry.DEFLATED
                zip.putNextEntry(e)
                i.file.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
            }
        }
        return manifest
    }

    private fun putStored(zip: ZipOutputStream, name: String, data: ByteArray) {
        val crc = CRC32().apply { update(data) }
        val e = ZipEntry(name).apply {
            method = ZipEntry.STORED; size = data.size.toLong(); compressedSize = data.size.toLong()
            this.crc = crc.value; time = FIXED_ENTRY_TIME
        }
        zip.putNextEntry(e); zip.write(data); zip.closeEntry()
    }

    private fun putDeflated(zip: ZipOutputStream, name: String, data: ByteArray) {
        zip.putNextEntry(ZipEntry(name).apply { method = ZipEntry.DEFLATED; time = FIXED_ENTRY_TIME })
        zip.write(data); zip.closeEntry()
    }

    // ───────────────────────────── قراءة (فحص/استيراد) ─────────────────────────────

    /** هل يبدأ [input] بعنصر `mimetype` يطابق RinPak؟ (للتمييز عن صيغة .rinproj القديمة). يستهلك الدفق. */
    fun looksLikeRinPak(input: InputStream): Boolean = try {
        ZipInputStream(input).use { zip ->
            val first = zip.nextEntry
            first != null && first.name == ENTRY_MIMETYPE && readLimited(zip, 256).toString(Charsets.US_ASCII) == MIME
        }
    } catch (e: Exception) { false }

    /** يقرأ ويتحقق من المانيفست فقط (أول عنصرين) دون فك الملفات. */
    fun inspect(input: InputStream): Manifest = corruptionGuard {
        ZipInputStream(input).use { zip ->
            val (manifest, bytes) = readHeader(zip)
            // التوقيع (إن وُجد) هو العنصر الثالث؛ نقرؤه هنا فقط (الفحص لا يفك الملفات).
            val third = zip.nextEntry
            if (third != null && third.name == ENTRY_SIGNATURE) manifest.copy(signature = verifySignature(readLimited(zip, 64 * 1024), bytes))
            else manifest
        }
    }

    /** يحوّل أعطال بنية ZIP (ملف مقطوع/تالف، CRC خاطئ) إلى رسالة عربية واضحة بدل رسالة JVM الإنجليزية. */
    private inline fun <T> corruptionGuard(block: () -> T): T = try {
        block()
    } catch (e: java.util.zip.ZipException) {
        throw RinPakException("الحزمة تالفة أو غير مكتملة (${e.message})")
    } catch (e: java.io.EOFException) {
        throw RinPakException("الحزمة مقطوعة — الملف غير مكتمل")
    }

    /**
     * يفك الحزمة داخل [dest] (مجلد فارغ تنشئه أنت) مع التحقق الكامل. عند أي خطأ يرمي
     * [RinPakException] ويترك [dest] لك لتحذفه (الاستيراد الذري يتم في ProjectManager).
     */
    fun extract(
        input: InputStream,
        dest: File,
        onProgress: ((doneBytes: Long, totalBytes: Long) -> Unit)? = null,
        isCancelled: (() -> Boolean)? = null
    ): Manifest = process(input, dest, onProgress, isCancelled)

    /**
     * يتحقق من الحزمة تحققاً كاملاً (ترتيب، مانيفست، SHA-256 لكل ملف، التوقيع، الحدود) **دون كتابة أي ملف**
     * على القرص — مناسب للجوال: لا يستهلك مساحة تخزين ولا يترك أثراً. يرمي [RinPakException] عند أي خلل.
     */
    fun verify(
        input: InputStream,
        onProgress: ((doneBytes: Long, totalBytes: Long) -> Unit)? = null,
        isCancelled: (() -> Boolean)? = null
    ): Manifest = process(input, null, onProgress, isCancelled)

    /** المحرك المشترك لـ [extract] (dest != null) و[verify] (dest == null). */
    private fun process(
        input: InputStream,
        dest: File?,
        onProgress: ((doneBytes: Long, totalBytes: Long) -> Unit)?,
        isCancelled: (() -> Boolean)?
    ): Manifest {
        val destRoot = dest?.canonicalFile
        return corruptionGuard { ZipInputStream(input).use { zip ->
            var (manifest, manifestBytes) = readHeader(zip)
            val declaredTotal = manifest.totalBytes
            var lastReported = -1L
            fun report(done: Long, force: Boolean = false) {
                if (onProgress == null) return
                if (force || done - lastReported >= PROGRESS_STEP) { lastReported = done; onProgress(done, declaredTotal) }
            }
            val expected = manifest.files.associateBy { it.path }
            val seen = HashSet<String>()
            var total = 0L
            var count = 2
            while (true) {
                val entry = zip.nextEntry ?: break
                if (++count > MAX_ENTRIES + 4) throw RinPakException("الحزمة تحتوي عدداً كبيراً جداً من العناصر")
                val name = entry.name
                if (name == ENTRY_SIGNATURE) {
                    if (count != 3 || manifest.signature != null) throw RinPakException("موضع التوقيع غير صالح")
                    manifest = manifest.copy(signature = verifySignature(readLimited(zip, 64 * 1024), manifestBytes))
                    continue
                }
                if (name == ENTRY_ICON) { readLimited(zip, 2 * 1024 * 1024); continue }
                if (!name.startsWith(CONTENT_ROOT)) throw RinPakException("عنصر غير متوقع خارج $CONTENT_ROOT: $name")
                if (entry.isDirectory) continue
                val rel = name.removePrefix(CONTENT_ROOT)
                val safe = safePath(rel) ?: throw RinPakException("مسار غير آمن: $name")
                if (safe != rel) throw RinPakException("مسار غير قياسي: $name")
                val meta = expected[safe] ?: throw RinPakException("ملف غير مُدرج في المانيفست: $safe")
                if (!seen.add(safe)) throw RinPakException("عنصر مكرر: $safe")
                val target: OutputStream = if (destRoot != null) {
                    val out = File(destRoot, safe).canonicalFile
                    if (!out.path.startsWith(destRoot.path + File.separator)) throw RinPakException("مسار خارج الجذر: $name")
                    out.parentFile?.mkdirs()
                    BufferedOutputStream(FileOutputStream(out))
                } else NULL_SINK
                val md = MessageDigest.getInstance("SHA-256")
                var size = 0L
                target.use { sink ->
                    val buf = ByteArray(DEFAULT_BUF)
                    while (true) {
                        if (isCancelled?.invoke() == true) throw RinPakException("أُلغيت العملية")
                        val n = zip.read(buf)
                        if (n < 0) break
                        size += n; total += n
                        if (total > MAX_TOTAL_BYTES) throw RinPakException("حجم محتوى المشروع يتجاوز الحد المسموح")
                        if (size > meta.size) throw RinPakException("حجم الملف أكبر من المعلن: $safe")
                        md.update(buf, 0, n); sink.write(buf, 0, n)
                        report(total)
                    }
                }
                if (size != meta.size) throw RinPakException("حجم الملف لا يطابق المانيفست: $safe")
                if (!hex(md.digest()).equals(meta.sha256, ignoreCase = true)) throw RinPakException("فشل التحقق من SHA-256: $safe")
            }
            val missing = expected.keys - seen
            if (missing.isNotEmpty()) throw RinPakException("ملفات مفقودة من الحزمة: ${missing.sorted().take(3).joinToString()}")
            report(total, force = true)
            manifest
        } }
    }

    private fun readHeader(zip: ZipInputStream): Pair<Manifest, ByteArray> {
        val first = zip.nextEntry ?: throw RinPakException("الملف فارغ")
        if (first.name != ENTRY_MIMETYPE) throw RinPakException("ليست حزمة RinPak: العنصر الأول ليس $ENTRY_MIMETYPE")
        if (readLimited(zip, 256).toString(Charsets.US_ASCII) != MIME) throw RinPakException("mimetype غير مطابق")
        val second = zip.nextEntry
        if (second == null || second.name != ENTRY_MANIFEST) throw RinPakException("العنصر الثاني يجب أن يكون $ENTRY_MANIFEST")
        val bytes = readLimited(zip, MAX_MANIFEST_BYTES)
        return parseManifest(bytes.toString(Charsets.UTF_8)) to bytes
    }

    // ───────────────────────────── التوقيع ─────────────────────────────

    private fun signatureJson(signer: Signer, manifestBytes: ByteArray): JSONObject {
        val pub = signer.publicKey
        require(pub.size == 32) { "مفتاح Ed25519 العام يجب أن يكون 32 بايت" }
        val sig = signer.sign(SIGN_DOMAIN.toByteArray(Charsets.ISO_8859_1) + manifestBytes)
        require(sig.size == 64) { "توقيع Ed25519 يجب أن يكون 64 بايت" }
        val b64 = Base64.getEncoder()
        return JSONObject()
            .put("algorithm", "Ed25519")
            .put("signs", ENTRY_MANIFEST)
            .put("keyId", keyId(pub))
            .put("publicKey", b64.encodeToString(pub))
            .put("signature", b64.encodeToString(sig))
    }

    /** معرّف المفتاح: أول 16 خانة hex من SHA-256 للمفتاح العام الخام. */
    fun keyId(publicKey: ByteArray): String = hex(MessageDigest.getInstance("SHA-256").digest(publicKey)).take(16)

    private val SPKI_ED25519_PREFIX = byteArrayOf(0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00)

    /** يتحقق من توقيع المانيفست؛ يرمي [RinPakException] عند أي خلل (توقيع موجود وغير صالح = رفض). */
    private fun verifySignature(sigBytes: ByteArray, manifestBytes: ByteArray): SignatureInfo {
        try {
            val o = JSONObject(sigBytes.toString(Charsets.UTF_8))
            if (o.optString("algorithm") != "Ed25519" || o.optString("signs") != ENTRY_MANIFEST)
                throw RinPakException("خوارزمية/هدف التوقيع غير مدعوم")
            val dec = Base64.getDecoder()
            val pub = dec.decode(o.getString("publicKey"))
            val sig = dec.decode(o.getString("signature"))
            if (pub.size != 32 || sig.size != 64) throw RinPakException("أحجام مفتاح/توقيع غير صالحة")
            if (!o.optString("keyId").equals(keyId(pub), ignoreCase = true)) throw RinPakException("keyId لا يطابق المفتاح العام")
            val key = KeyFactory.getInstance("Ed25519").generatePublic(X509EncodedKeySpec(SPKI_ED25519_PREFIX + pub))
            val ok = Signature.getInstance("Ed25519").run {
                initVerify(key); update(SIGN_DOMAIN.toByteArray(Charsets.ISO_8859_1) + manifestBytes); verify(sig)
            }
            if (!ok) throw RinPakException("التوقيع الرقمي غير صالح — الحزمة عُدّلت بعد توقيعها")
            return SignatureInfo(keyId(pub), o.getString("publicKey"))
        } catch (e: RinPakException) {
            throw e
        } catch (e: Exception) {
            throw RinPakException("تعذّر التحقق من التوقيع الرقمي")
        }
    }

    /** يرفض أسماء تتصادم على أنظمة لا تفرّق بين الحروف الكبيرة والصغيرة (ويندوز/macOS). */
    private fun checkCollisions(paths: List<String>) {
        val seen = HashMap<String, String>()
        for (p in paths) {
            val prev = seen.put(p.lowercase(), p)
            if (prev != null && prev != p) throw RinPakException("تصادم أسماء (حالة الأحرف): $prev ≙ $p")
        }
    }

    // ───────────────────────────── المانيفست ─────────────────────────────

    private fun manifestToJson(m: Manifest): JSONObject = JSONObject().apply {
        put("format", FORMAT)
        put("specVersion", m.specVersion)
        put("name", m.name)
        put("type", m.type)
        put("entry", m.entry)
        put("contentRoot", CONTENT_ROOT)
        put("created", m.created)
        put("generator", JSONObject().put("name", m.generatorName).put("version", m.generatorVersion))
        put("icon", if (m.hasIcon) ENTRY_ICON else JSONObject.NULL)
        val mt = m.meta
        if (mt.version.isNotBlank()) put("version", mt.version)
        if (mt.description.isNotBlank()) put("description", mt.description)
        if (mt.author.isNotBlank()) put("author", mt.author)
        if (mt.license.isNotBlank()) put("license", mt.license)
        if (mt.minRinVersion.isNotBlank()) put("minRinVersion", mt.minRinVersion)
        if (mt.permissions.isNotEmpty()) put("permissions", JSONArray(mt.permissions))
        if (mt.dependencies.isNotEmpty()) put("dependencies", JSONArray(mt.dependencies))
        put("stats", JSONObject().put("files", m.fileCount).put("bytes", m.totalBytes))
        put("integrity", JSONObject().apply {
            put("algorithm", "SHA-256")
            put("root", m.root)
            put("files", JSONArray().also { arr ->
                m.files.forEach { arr.put(JSONObject().put("path", it.path).put("size", it.size).put("sha256", it.sha256)) }
            })
        })
    }

    private fun parseManifest(text: String): Manifest {
        val o = try { JSONObject(text) } catch (e: Exception) { throw RinPakException("المانيفست ليس JSON صالحاً") }
        if (o.optString("format") != FORMAT) throw RinPakException("المانيفست ليس بصيغة $FORMAT")
        val version = o.optInt("specVersion", -1)
        if (version != SPEC_VERSION) throw RinPakException("إصدار RinPak غير مدعوم: $version")
        val entry = o.optString("entry", ENTRY_POINT)
        if (entry != ENTRY_POINT) throw RinPakException("ملف الدخول غير مدعوم: $entry")
        if (o.optString("contentRoot", CONTENT_ROOT) != CONTENT_ROOT) throw RinPakException("contentRoot غير مدعوم")
        val integrity = o.optJSONObject("integrity") ?: throw RinPakException("قسم integrity مفقود")
        if (integrity.optString("algorithm") != "SHA-256") throw RinPakException("خوارزمية التحقق غير مدعومة")
        val arr = integrity.optJSONArray("files") ?: throw RinPakException("قائمة الملفات مفقودة")
        if (arr.length() > MAX_ENTRIES) throw RinPakException("عدد الملفات يتجاوز الحد")
        val files = ArrayList<FileEntry>(arr.length())
        val names = HashSet<String>()
        for (i in 0 until arr.length()) {
            val f = arr.getJSONObject(i)
            val p = f.optString("path")
            val safe = safePath(p)
            if (safe == null || safe != p) throw RinPakException("مسار غير آمن في المانيفست: $p")
            if (!names.add(p)) throw RinPakException("مسار مكرر في المانيفست: $p")
            val size = f.optLong("size", -1)
            val sha = f.optString("sha256")
            if (size > MAX_FILE_BYTES) throw RinPakException("الملف أكبر من الحد المسموح: $p")
            if (size < 0 || !Regex("^[0-9a-fA-F]{64}$").matches(sha)) throw RinPakException("بيانات ملف غير صالحة: $p")
            files += FileEntry(p, size, sha.lowercase())
        }
        if (ENTRY_POINT !in names) throw RinPakException("المانيفست لا يضم $ENTRY_POINT")
        checkCollisions(files.map { it.path })
        if (files.sumOf { it.size } > MAX_TOTAL_BYTES) throw RinPakException("حجم المشروع يتجاوز الحد المسموح")
        val root = integrity.optString("root").lowercase()
        if (root != rootDigest(files)) throw RinPakException("بصمة الجذر (root) لا تطابق قائمة الملفات")
        val gen = o.optJSONObject("generator")
        return Manifest(
            name = o.optString("name", "RinProject"), type = o.optString("type", "free"), entry = entry,
            specVersion = version, generatorName = gen?.optString("name").orEmpty(),
            generatorVersion = gen?.optString("version").orEmpty(), created = o.optString("created"),
            files = files, root = root, hasIcon = !o.isNull("icon") && o.has("icon"),
            meta = PackMeta(
                version = o.optString("version"), description = o.optString("description").take(1000),
                author = o.optString("author").take(128), license = o.optString("license").take(64),
                minRinVersion = o.optString("minRinVersion"),
                permissions = strings(o.optJSONArray("permissions")), dependencies = strings(o.optJSONArray("dependencies"))
            )
        )
    }

    private fun strings(a: JSONArray?): List<String> =
        if (a == null) emptyList() else (0 until minOf(a.length(), 256)).map { a.optString(it).take(256) }

    /** SHA-256 على أسطر "path\0size\0sha256\n" مرتبة حسب بايتات UTF-8 للمسار. */
    fun rootDigest(files: List<FileEntry>): String {
        val md = MessageDigest.getInstance("SHA-256")
        files.sortedBy { String(it.path.toByteArray(Charsets.UTF_8), Charsets.ISO_8859_1) }.forEach {
            md.update("${it.path}\u0000${it.size}\u0000${it.sha256.lowercase()}\n".toByteArray(Charsets.UTF_8))
        }
        return hex(md.digest())
    }

    // ───────────────────────────── أدوات ─────────────────────────────

    /** يطهّر مساراً نسبياً: يرفض المطلق و"..", حرف السواقة، المقاطع الفارغة؛ يعيد null إن كان غير آمن. */
    fun safePath(raw: String): String? {
        if (raw.isEmpty() || raw.contains('\\') || raw.contains(':') || raw.contains('\u0000') || raw.startsWith("/")) return null
        val segs = raw.split('/')
        if (segs.any { it.isEmpty() || it == "." || it == ".." }) return null
        // أسماء لا تُنشأ على ويندوز: لاحقة نقطة/مسافة، محارف تحكم، ومحارف محجوزة.
        if (segs.any { it.endsWith(".") || it.endsWith(" ") || it.any { c -> c < ' ' || c in "<>\"|?*" } }) return null
        return raw
    }

    private const val DEFAULT_BUF = 16 * 1024
    /** أقل زيادة (بايت) بين تقريرَي تقدّم متتاليين، كي لا نغرق خيط الواجهة بالإشعارات. */
    private const val PROGRESS_STEP = 256L * 1024L
    private val NULL_SINK = object : OutputStream() {
        override fun write(b: Int) {}
        override fun write(b: ByteArray, off: Int, len: Int) {}
    }

    private fun readLimited(input: InputStream, limit: Int): ByteArray {
        val bos = ByteArrayOutputStream()
        val buf = ByteArray(4096)
        while (true) {
            val n = input.read(buf)
            if (n < 0) break
            if (bos.size() + n > limit) throw RinPakException("عنصر وصفي أكبر من الحد المسموح")
            bos.write(buf, 0, n)
        }
        return bos.toByteArray()
    }

    private fun hex(b: ByteArray): String = b.joinToString("") { "%02x".format(it) }
}

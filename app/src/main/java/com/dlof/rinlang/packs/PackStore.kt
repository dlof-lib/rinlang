package com.dlof.rinlang.packs

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.zip.ZipEntry
import java.util.zip.ZipFile

/**
 * مخزن الحزم المثبّتة: `filesDir/content_packs/<id>/` لكل حزمة، وبداخله ملف علامة `.installed.json`
 * يُكتب آخر شيء بعد نجاح الفك — فوجوده = حزمة سليمة كاملة. التثبيت ذرّي: يُفك أولاً في مجلد مؤقت
 * ثم يُستبدل المجلد القديم بـ rename، فلا تبقى حزمة نصف مفكوكة إن انقطع التطبيق في المنتصف.
 */
object PackStore {

    private const val ROOT_NAME = "content_packs"
    private const val STAGING_NAME = ".staging"
    private const val DOWNLOADS_NAME = ".downloads"
    private const val MARKER = ".installed.json"

    // حدود دفاعية ضد zip خبيث (zip bomb / مسارات مخادعة).
    private const val MAX_ENTRIES = 2000
    private const val MAX_TOTAL_BYTES = 64L * 1024 * 1024
    private const val MAX_ENTRY_BYTES = 32L * 1024 * 1024

    fun root(context: Context): File = File(context.filesDir, ROOT_NAME).apply { mkdirs() }
    internal fun downloadsDir(context: Context): File = File(root(context), DOWNLOADS_NAME).apply { mkdirs() }
    private fun stagingDir(context: Context): File = File(root(context), STAGING_NAME).apply { mkdirs() }

    fun dirOf(context: Context, id: String): File {
        require(ContentPack.isValidId(id)) { "invalid pack id: $id" }
        return File(root(context), id)
    }

    fun installedInfo(context: Context, id: String): InstalledPack? {
        if (!ContentPack.isValidId(id)) return null
        val dir = File(root(context), id)
        val marker = File(dir, MARKER)
        if (!marker.isFile) return null
        return try {
            val o = JSONObject(marker.readText(Charsets.UTF_8))
            InstalledPack(
                id = id,
                version = o.optInt("version", 1),
                sha256 = o.optString("sha256"),
                installedAt = o.optLong("installedAt"),
                bytesOnDisk = dirSize(dir)
            )
        } catch (e: Exception) {
            null
        }
    }

    fun isInstalled(context: Context, id: String): Boolean = installedInfo(context, id) != null

    fun listInstalled(context: Context): List<InstalledPack> =
        (root(context).listFiles() ?: emptyArray())
            .filter { it.isDirectory && !it.name.startsWith(".") }
            .mapNotNull { installedInfo(context, it.name) }
            .sortedBy { it.id }

    fun totalBytes(context: Context): Long = listInstalled(context).sumOf { it.bytesOnDisk }

    /** يحل مساراً نسبياً داخل حزمة مثبّتة؛ يعيد null إن لم تكن مثبّتة أو إن حاول المسار الهروب من مجلدها. */
    fun fileOf(context: Context, id: String, relativePath: String): File? {
        if (!isInstalled(context, id)) return null
        val base = dirOf(context, id).canonicalFile
        val f = File(base, relativePath).canonicalFile
        return if (f.path.startsWith(base.path + File.separator) && f.isFile) f else null
    }

    fun delete(context: Context, id: String): Boolean {
        val dir = dirOf(context, id)
        val ok = !dir.exists() || dir.deleteRecursively()
        LanguagePacks.invalidateAll()
        return ok
    }

    /** يمسح بقايا التنزيلات/المجلدات المؤقتة (انقطاع سابق). آمن للاستدعاء عند إقلاع التطبيق. */
    fun cleanupLeftovers(context: Context) {
        File(root(context), STAGING_NAME).deleteRecursively()
        val downloads = File(root(context), DOWNLOADS_NAME)
        val weekAgo = System.currentTimeMillis() - 7L * 24 * 3600 * 1000
        downloads.listFiles()?.forEach { if (it.lastModified() < weekAgo) it.delete() }
    }

    /**
     * يفك [zip] (تم التحقق من sha256 له مسبقاً) ويثبّته كحزمة [pack]. يرمي [PackException] عند أي خلل.
     */
    @Throws(PackException::class)
    fun install(context: Context, pack: ContentPack, zip: File) {
        val staging = File(stagingDir(context), pack.id + "-" + System.nanoTime())
        try {
            if (!staging.mkdirs()) throw PackException(PackException.Kind.STORAGE, "cannot create staging dir")
            extract(zip, staging)

            // الحزمة يجب أن تصف نفسها بنفس الـ id/النوع الموجودين في الكتالوج (تمنع تبديل محتوى حزمة بأخرى).
            val meta = try {
                JSONObject(File(staging, "pack.json").readText(Charsets.UTF_8))
            } catch (e: Exception) {
                throw PackException(PackException.Kind.CORRUPT, "pack.json missing or invalid")
            }
            if (meta.optString("id") != pack.id || meta.optString("type") != pack.type.json) {
                throw PackException(PackException.Kind.CORRUPT, "pack.json does not match catalog entry")
            }
            if (pack.type == PackType.LANGUAGE) {
                // فحص مبكر: strings.json يجب أن يكون JSON صالحاً قبل أن يُعتمد.
                try {
                    JSONObject(File(staging, "strings.json").readText(Charsets.UTF_8))
                } catch (e: Exception) {
                    throw PackException(PackException.Kind.CORRUPT, "strings.json missing or invalid")
                }
            }

            File(staging, MARKER).writeText(
                JSONObject()
                    .put("id", pack.id)
                    .put("version", pack.version)
                    .put("sha256", pack.sha256)
                    .put("installedAt", System.currentTimeMillis())
                    .toString(),
                Charsets.UTF_8
            )

            val target = dirOf(context, pack.id)
            val old = File(stagingDir(context), pack.id + "-old-" + System.nanoTime())
            if (target.exists() && !target.renameTo(old)) {
                throw PackException(PackException.Kind.STORAGE, "cannot replace previous version")
            }
            if (!staging.renameTo(target)) {
                if (old.exists()) old.renameTo(target) // تراجع
                throw PackException(PackException.Kind.STORAGE, "cannot move pack into place")
            }
            old.deleteRecursively()
            LanguagePacks.invalidateAll()
        } catch (e: PackException) {
            throw e
        } catch (e: IOException) {
            throw PackException(PackException.Kind.STORAGE, e.message ?: "io error", e)
        } finally {
            staging.deleteRecursively()
        }
    }

    private fun extract(zip: File, destDir: File) {
        val destCanonical = destDir.canonicalFile
        var entries = 0
        var total = 0L
        ZipFile(zip).use { zf ->
            val e = zf.entries()
            while (e.hasMoreElements()) {
                val entry: ZipEntry = e.nextElement()
                if (++entries > MAX_ENTRIES) throw PackException(PackException.Kind.CORRUPT, "too many entries")
                val name = entry.name
                if (name.isEmpty() || name.startsWith("/") || name.contains("\\") || name.split('/').any { it == ".." }) {
                    throw PackException(PackException.Kind.CORRUPT, "unsafe entry name: $name")
                }
                val out = File(destCanonical, name).canonicalFile
                if (!out.path.startsWith(destCanonical.path + File.separator)) {
                    throw PackException(PackException.Kind.CORRUPT, "zip-slip blocked: $name")
                }
                if (entry.isDirectory) {
                    out.mkdirs()
                    continue
                }
                out.parentFile?.mkdirs()
                var written = 0L
                zf.getInputStream(entry).use { input ->
                    out.outputStream().use { output ->
                        val buf = ByteArray(16 * 1024)
                        while (true) {
                            val n = input.read(buf)
                            if (n < 0) break
                            written += n
                            total += n
                            // الحجم المعلن في الـ zip قد يكذب، لذا نعدّ البايتات الفعلية.
                            if (written > MAX_ENTRY_BYTES || total > MAX_TOTAL_BYTES) {
                                throw PackException(PackException.Kind.CORRUPT, "pack too large when extracted")
                            }
                            output.write(buf, 0, n)
                        }
                    }
                }
            }
        }
    }

    private fun dirSize(dir: File): Long {
        var sum = 0L
        dir.walkTopDown().forEach { if (it.isFile) sum += it.length() }
        return sum
    }
}

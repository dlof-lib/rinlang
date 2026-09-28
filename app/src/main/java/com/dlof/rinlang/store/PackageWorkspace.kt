package com.dlof.rinlang.store

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import android.util.Base64
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/** عقدة واحدة (ملف أو مجلد) داخل مساحة عمل تعديل الحزمة. [relPath] مسار نسبي بفواصل "/". */
data class WorkspaceNode(
    val name: String,
    val relPath: String,
    val isDir: Boolean,
    val sizeBytes: Long,
    /** عدد الملفات داخل المجلد (كل المستويات)؛ 0 للملفات. */
    val fileCount: Int = 0
)

/**
 * مساحة عمل مؤقتة (في cacheDir) لتعديل محتوى حزمة منشورة: تُفكّ الحزمة (zip المخزَّن base64 في
 * [RinPackage.base64Data]) إلى مجلد حقيقي، فيمكن تصفّح الملفات وإنشاء المجلدات والملفات
 * وتعديلها بالمحرر، ثم [pack] تُعيد تجميعها في zip جديد يُرفع للمتجر.
 *
 * كل المسارات المستلمة من الخارج تمرّ عبر [resolve] الذي يمنع الخروج من جذر المساحة
 * (path traversal / zip-slip).
 */
class PackageWorkspace(val root: File) {

    companion object {
        /** أقصى طول لاسم ملف/مجلد واحد. */
        const val MAX_NAME_LENGTH = 80
        /** أقصى عمق للمجلدات المتداخلة، لمنع مسارات مبالغ فيها. */
        const val MAX_DEPTH = 8
        /** أكبر ملف نصي يُفتح في المحرر (بايت). */
        const val MAX_EDITABLE_BYTES = 512 * 1024L

        private val IMAGE_EXTENSIONS = setOf("png", "jpg", "jpeg", "webp", "gif", "bmp")

        fun dirFor(context: Context, packageId: String): File =
            File(File(context.cacheDir, "store_edit"), packageId.filter { it.isLetterOrDigit() || it == '-' || it == '_' })

        /** يفكّ [pkg] إلى مساحة عمل جديدة (يمسح أي مساحة سابقة لنفس الحزمة). */
        fun create(context: Context, pkg: RinPackage): PackageWorkspace {
            val dir = dirFor(context, pkg.id)
            if (dir.exists()) dir.deleteRecursively()
            dir.mkdirs()
            val ws = PackageWorkspace(dir)
            ws.extract(Base64.decode(pkg.base64Data, Base64.NO_WRAP))
            return ws
        }

        /** يفتح مساحة عمل موجودة مسبقاً (مثلاً من شاشة محرر الملف)، أو null إن لم تكن موجودة. */
        fun openExisting(context: Context, packageId: String): PackageWorkspace? {
            val dir = dirFor(context, packageId)
            return if (dir.isDirectory) PackageWorkspace(dir) else null
        }

        fun isImage(name: String): Boolean =
            name.substringAfterLast('.', "").lowercase() in IMAGE_EXTENSIONS

        /** يتحقق أن [name] اسم ملف/مجلد صالح؛ يعيد رسالة الخطأ أو null إن كان صالحاً. */
        fun validateName(name: String): String? {
            val n = name.trim()
            return when {
                n.isEmpty() -> "الاسم فارغ"
                n == "." || n == ".." -> "اسم غير صالح"
                n.length > MAX_NAME_LENGTH -> "الاسم طويل جداً (الحد الأقصى $MAX_NAME_LENGTH حرفاً)"
                n.any { it == '/' || it == '\\' || it == ':' || it == '*' || it == '?' || it == '"' || it == '<' || it == '>' || it == '|' || it.code < 32 } ->
                    "الاسم يحتوي على رموز غير مسموحة ( / \\ : * ? \" < > | )"
                else -> null
            }
        }
    }

    /** يحوّل [rel] إلى ملف داخل الجذر فقط، ويرمي استثناء إن حاول المسار الخروج منه. */
    fun resolve(rel: String): File {
        val clean = rel.replace('\\', '/').trim('/')
        val file = if (clean.isEmpty()) root else File(root, clean)
        val rootPath = root.canonicalPath
        val path = file.canonicalPath
        if (path != rootPath && !path.startsWith(rootPath + File.separator)) {
            throw SecurityException("مسار غير مسموح: $rel")
        }
        return file
    }

    private fun relOf(file: File): String =
        file.canonicalPath.removePrefix(root.canonicalPath).replace(File.separatorChar, '/').trim('/')

    // ---------------------------------------------------------------- فك الحزمة

    fun extract(zipBytes: ByteArray) {
        ZipInputStream(zipBytes.inputStream()).use { zip ->
            var entry: ZipEntry? = zip.nextEntry
            while (entry != null) {
                val name = entry.name.replace('\\', '/')
                val unsafe = name.startsWith("/") || name.split("/").any { it == ".." }
                if (!unsafe && name.isNotBlank()) {
                    val target = resolve(name)
                    if (entry.isDirectory) {
                        target.mkdirs()
                    } else {
                        target.parentFile?.mkdirs()
                        BufferedOutputStream(FileOutputStream(target)).use { zip.copyTo(it) }
                    }
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
    }

    // ---------------------------------------------------------------- التصفّح

    /** محتويات المجلد [relDir] (المجلدات أولاً ثم الملفات، كلٌّ مرتّب أبجدياً). */
    fun list(relDir: String): List<WorkspaceNode> {
        val dir = resolve(relDir)
        val children = dir.listFiles() ?: return emptyList()
        val nodes = children.map { f ->
            WorkspaceNode(
                name = f.name,
                relPath = relOf(f),
                isDir = f.isDirectory,
                sizeBytes = if (f.isFile) f.length() else 0L,
                fileCount = if (f.isDirectory) countFiles(f) else 0
            )
        }
        return nodes.sortedWith(
            compareByDescending<WorkspaceNode> { it.isDir }.thenBy(String.CASE_INSENSITIVE_ORDER) { it.name }
        )
    }

    private fun countFiles(dir: File): Int =
        dir.walkTopDown().count { it.isFile }

    fun exists(rel: String): Boolean = try { resolve(rel).exists() } catch (t: SecurityException) { false }

    fun isDirectory(rel: String): Boolean = try { resolve(rel).isDirectory } catch (t: SecurityException) { false }

    /** كل ملفات مساحة العمل (مسارات نسبية). */
    fun allFiles(): List<String> =
        root.walkTopDown().filter { it.isFile }.map { relOf(it) }.sorted().toList()

    /** ملفات المكتبة (lib/*.og.rin) — يجب أن يبقى واحد منها على الأقل ليعمل التثبيت. */
    fun libraryFiles(): List<String> =
        allFiles().filter { it.startsWith("lib/") && it.endsWith(".og.rin") }

    // ---------------------------------------------------------------- القراءة والكتابة

    /** هل الملف نصّي قابل للتحرير؟ (لا يحوي بايت صفري في أول 4 كيلوبايت، وحجمه ضمن الحد). */
    fun isEditableText(rel: String): Boolean {
        val f = resolve(rel)
        if (!f.isFile || f.length() > MAX_EDITABLE_BYTES) return false
        if (isImage(f.name)) return false
        return f.inputStream().use { input ->
            val buf = ByteArray(4096)
            val n = input.read(buf)
            (0 until maxOf(n, 0)).none { buf[it].toInt() == 0 }
        }
    }

    fun readText(rel: String): String = resolve(rel).readText(Charsets.UTF_8)

    fun writeText(rel: String, text: String) {
        val f = resolve(rel)
        f.parentFile?.mkdirs()
        f.writeText(text, Charsets.UTF_8)
    }

    // ---------------------------------------------------------------- إنشاء/حذف/إعادة تسمية

    /** ينشئ مجلداً باسم [name] داخل [relParent]. يعيد رسالة خطأ أو null عند النجاح. */
    fun createFolder(relParent: String, name: String): String? {
        validateName(name)?.let { return it }
        val parent = resolve(relParent)
        if (relParent.trim('/').split("/").filter { it.isNotEmpty() }.size >= MAX_DEPTH) return "تجاوزت أقصى عمق للمجلدات ($MAX_DEPTH)"
        val target = File(parent, name.trim())
        resolve(relOf(parent) + "/" + name.trim()) // تحقق أمني
        if (target.exists()) return "يوجد عنصر بهذا الاسم مسبقاً"
        return if (target.mkdirs()) null else "تعذّر إنشاء المجلد"
    }

    /** ينشئ ملفاً فارغاً (أو بمحتوى [content]) باسم [name]. يعيد رسالة خطأ أو null عند النجاح. */
    fun createFile(relParent: String, name: String, content: String = ""): String? {
        validateName(name)?.let { return it }
        val parent = resolve(relParent)
        val target = File(parent, name.trim())
        resolve(relOf(parent) + "/" + name.trim())
        if (target.exists()) return "يوجد عنصر بهذا الاسم مسبقاً"
        return try {
            parent.mkdirs()
            target.writeText(content, Charsets.UTF_8)
            null
        } catch (t: Throwable) {
            t.message ?: "تعذّر إنشاء الملف"
        }
    }

    fun delete(rel: String): Boolean {
        val f = resolve(rel)
        if (f == root) return false
        return f.deleteRecursively()
    }

    /** يعيد تسمية عنصر (ملف/مجلد) إلى [newName] في نفس مجلده. يعيد رسالة خطأ أو null. */
    fun rename(rel: String, newName: String): String? {
        validateName(newName)?.let { return it }
        val f = resolve(rel)
        if (f == root) return "لا يمكن إعادة تسمية الجذر"
        val target = File(f.parentFile, newName.trim())
        if (target.exists()) return "يوجد عنصر بهذا الاسم مسبقاً"
        return if (f.renameTo(target)) null else "تعذّرت إعادة التسمية"
    }

    /** ينسخ ملفاً مختاراً عبر SAF إلى [relParent]؛ يضمن اسماً فريداً بإلحاق (1)، (2)... عند التكرار. يعيد المسار النسبي. */
    fun importUri(context: Context, relParent: String, uri: Uri): String {
        val display = queryName(context, uri) ?: "file_${System.currentTimeMillis()}"
        val safe = display.replace(Regex("[\\\\/:*?\"<>|]"), "_").take(MAX_NAME_LENGTH).ifBlank { "file" }
        val parent = resolve(relParent).apply { mkdirs() }
        var target = File(parent, safe)
        if (target.exists()) {
            val base = safe.substringBeforeLast('.', safe)
            val ext = safe.substringAfterLast('.', "").let { if (it.isEmpty()) "" else ".$it" }
            var i = 1
            while (target.exists()) { target = File(parent, "$base ($i)$ext"); i++ }
        }
        resolve(relOf(parent) + "/" + target.name)
        context.contentResolver.openInputStream(uri)?.use { input ->
            BufferedOutputStream(FileOutputStream(target)).use { input.copyTo(it) }
        } ?: throw IllegalStateException("تعذّر فتح الملف المختار")
        return relOf(target)
    }

    private fun queryName(context: Context, uri: Uri): String? = try {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getColumnIndex(OpenableColumns.DISPLAY_NAME).let { if (it >= 0) c.getString(it) else null } else null
        }
    } catch (t: Throwable) { null }

    // ---------------------------------------------------------------- التجميع

    /**
     * يجمّع مساحة العمل في zip جديد [out]. المجلدات الفارغة تُكتب كمدخلات مجلد ("name/") حتى
     * لا تضيع عند إعادة الرفع.
     */
    fun pack(out: File): File {
        if (out.exists()) out.delete()
        out.parentFile?.mkdirs()
        ZipOutputStream(BufferedOutputStream(FileOutputStream(out))).use { zip ->
            val all = root.walkTopDown().filter { it != root }.toList().sortedBy { relOf(it) }
            for (f in all) {
                val rel = relOf(f)
                if (f.isDirectory) {
                    if (f.listFiles().isNullOrEmpty()) {
                        zip.putNextEntry(ZipEntry("$rel/"))
                        zip.closeEntry()
                    }
                } else {
                    zip.putNextEntry(ZipEntry(rel))
                    f.inputStream().use { it.copyTo(zip) }
                    zip.closeEntry()
                }
            }
        }
        return out
    }

    fun destroy() {
        root.deleteRecursively()
    }
}

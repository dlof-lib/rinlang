package com.dlof.rinlang

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/**
 * يدير مشاريع Rin على تخزين التطبيق الخاص (filesDir/projects/<name>/...).
 * كل مشروع مجلد مستقل، حتى يحصل على basePath خاص به عند تمريره لـ RinEngine —
 * فتبقى عمليات save/installation/file (انظر لغة الحاويات في README) معزولة
 * بين مشروع وآخر بدل أن تتشارك كلها rin_installed/ واحدة.
 *
 * "رفع ملف" (upload) هنا يعني: استيراد ملف موجود بالفعل على الجهاز (أو Google
 * Drive وغيرها عبر SAF) بنسخ محتواه داخل مجلد المشروع، تماماً كما يفعل زر
 * "فتح" في MainActivity لكن مع الاحتفاظ بنسخة دائمة داخل المشروع بدل مجرد
 * فتحها في المحرر لمرة واحدة.
 */
object ProjectManager {

    private const val PROJECTS_DIR = "projects"
    private const val RIN_EXTENSION = ".rin"

    private fun projectsRoot(context: Context): File {
        val root = File(context.filesDir, PROJECTS_DIR)
        if (!root.exists()) root.mkdirs()
        return root
    }

    /** أسماء المشاريع الصالحة: حروف/أرقام/شرطة/شرطة سفلية فقط، لتفادي مشاكل مسارات الملفات. */
    fun isValidProjectName(name: String): Boolean {
        val trimmed = name.trim()
        return trimmed.isNotEmpty() && trimmed.matches(Regex("^[A-Za-z0-9_\\-\\u0600-\\u06FF ]{1,64}$"))
    }

    fun listProjects(context: Context): List<Project> {
        val root = projectsRoot(context)
        return (root.listFiles { f -> f.isDirectory } ?: emptyArray())
            .map { Project(it.name, it, it.lastModified()) }
            .sortedByDescending { it.lastModified }
    }

    /** ينشئ مشروعاً جديداً بمجلد فارغ + ملف main.rin ترحيبي، ويرمي IllegalArgumentException لو الاسم مستخدم أو غير صالح. */
    fun createProject(context: Context, name: String): Project {
        val trimmed = name.trim()
        require(isValidProjectName(trimmed)) { "اسم المشروع غير صالح" }
        val dir = File(projectsRoot(context), trimmed)
        require(!dir.exists()) { "يوجد مشروع بهذا الاسم بالفعل" }
        dir.mkdirs()
        File(dir, "main.rin").writeText(
            "// مشروع: $trimmed\n" +
                "print \"مرحباً من مشروع $trimmed\";\n"
        )
        return Project(trimmed, dir, dir.lastModified())
    }

    fun deleteProject(project: Project): Boolean = project.dir.deleteRecursively()

    fun renameProject(project: Project, newName: String): Project {
        require(isValidProjectName(newName)) { "اسم المشروع غير صالح" }
        val newDir = File(project.dir.parentFile, newName.trim())
        require(!newDir.exists()) { "يوجد مشروع بهذا الاسم بالفعل" }
        val ok = project.dir.renameTo(newDir)
        require(ok) { "تعذّر إعادة تسمية المشروع" }
        return Project(newName.trim(), newDir, newDir.lastModified())
    }

    // ---- ملفات داخل مشروع ----

    /** كل ملفات .rin مباشرة داخل مجلد المشروع (بدون تنقيب في مجلدات فرعية مثل rin_installed/). */
    fun listFiles(project: Project): List<RinFile> {
        return (project.dir.listFiles { f -> f.isFile && f.name.endsWith(RIN_EXTENSION) } ?: emptyArray())
            .map { RinFile(it.name, it, it.length(), it.lastModified()) }
            .sortedByDescending { it.lastModified }
    }

    fun readFile(rinFile: RinFile): String = rinFile.file.readText()

    fun writeFile(project: Project, fileName: String, content: String): RinFile {
        val safeName = ensureRinExtension(fileName)
        val target = File(project.dir, safeName)
        target.writeText(content)
        return RinFile(safeName, target, target.length(), target.lastModified())
    }

    fun deleteFile(rinFile: RinFile): Boolean = rinFile.file.delete()

    private fun ensureRinExtension(name: String): String {
        val trimmed = name.trim()
        return if (trimmed.endsWith(RIN_EXTENSION)) trimmed else "$trimmed$RIN_EXTENSION"
    }

    /**
     * "رفع" ملف من الجهاز (أو أي مزوّد SAF: تخزين محلي، Google Drive...) إلى داخل المشروع.
     * يقرأ محتوى [uri] عبر ContentResolver وينسخه كملف جديد داخل مجلد المشروع.
     */
    fun importFileFromUri(context: Context, project: Project, uri: Uri): RinFile {
        val resolver: ContentResolver = context.contentResolver
        val displayName = queryDisplayName(resolver, uri) ?: uri.lastPathSegment ?: "imported"
        val text = resolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
            ?: throw IllegalStateException("تعذّرت قراءة الملف المحدد")

        var safeName = ensureRinExtension(sanitizeFileName(displayName))
        var target = File(project.dir, safeName)
        var counter = 1
        // تفادي الكتابة فوق ملف موجود بنفس الاسم: أضف رقماً متسلسلاً.
        while (target.exists()) {
            val base = safeName.removeSuffix(RIN_EXTENSION)
            safeName = "${base}_$counter$RIN_EXTENSION"
            target = File(project.dir, safeName)
            counter++
        }
        target.writeText(text)
        return RinFile(safeName, target, target.length(), target.lastModified())
    }

    private fun sanitizeFileName(name: String): String =
        name.replace(Regex("[^A-Za-z0-9_\\-.\\u0600-\\u06FF]"), "_")

    // ---- مكتبات المشروع (lib/ *.og.rin) ----
    //
    // كل مشروع يملك مجلداً فرعياً lib/ (أسفل basePath الممرَّر لـ RinEngine)، وأي ملف
    // بداخله بامتداد .og.rin هو "مكتبة" يمكن لأي كود في هذا المشروع استيرادها مباشرة عبر
    // @import "lib/<name>.og.rin"; بالضبط بنفس آلية المكتبات القياسية الخمس المدمجة —
    // فرق المستخدم الوحيد هو أنّ مكتباته توجد فعلياً على القرص بدل أن تكون مدمجة في الثنائي.

    private const val LIB_DIR = "lib"
    private const val LIB_EXTENSION = ".og.rin"

    /** مجلد lib/ الخاص بالمشروع، يُنشأ تلقائياً إن لم يكن موجوداً. */
    fun libDir(project: Project): File {
        val dir = File(project.dir, LIB_DIR)
        if (!dir.exists()) dir.mkdirs()
        return dir
    }

    /** اسم مكتبة صالح: حروف/أرقام/شرطة/شرطة سفلية فقط (قبل امتداد .og.rin)، لتفادي مشاكل مسارات الاستيراد. */
    fun isValidLibraryName(name: String): Boolean {
        val base = name.trim().removeSuffix(LIB_EXTENSION)
        return base.isNotEmpty() && base.matches(Regex("^[A-Za-z0-9_\\-]{1,64}$"))
    }

    /** كل مكتبات المشروع (ملفات lib/ *.og.rin)، الأحدث تعديلاً أولاً. */
    fun listLibraries(project: Project): List<RinLibrary> {
        return (libDir(project).listFiles { f -> f.isFile && f.name.endsWith(LIB_EXTENSION) } ?: emptyArray())
            .map { RinLibrary(it.name, it, it.length(), it.lastModified()) }
            .sortedByDescending { it.lastModified }
    }

    fun readLibrary(library: RinLibrary): String = library.file.readText()

    /** يُنشئ مكتبة جديدة (فارغة أو بمحتوى بدائي جاهز) داخل lib/ الخاص بالمشروع. */
    fun createLibrary(project: Project, name: String): RinLibrary {
        val trimmed = name.trim()
        require(isValidLibraryName(trimmed)) { "اسم المكتبة غير صالح (حروف/أرقام/شرطة فقط)" }
        val safeName = ensureLibExtension(trimmed)
        val target = File(libDir(project), safeName)
        require(!target.exists()) { "توجد مكتبة بهذا الاسم بالفعل" }
        val libId = safeName.removeSuffix(LIB_EXTENSION)
        target.writeText(
            "// ============================================================================\n" +
                "//  lib/$safeName — مكتبتك الخاصة\n" +
                "//  استيراد:\n" +
                "//    @import \"lib/$safeName\";\n" +
                "//    @import \"lib/$safeName\" as $libId;\n" +
                "// ============================================================================\n\n" +
                "fun hello() {\n" +
                "    return \"مرحباً من مكتبة $libId\";\n" +
                "}\n"
        )
        return RinLibrary(safeName, target, target.length(), target.lastModified())
    }

    fun writeLibrary(project: Project, library: RinLibrary, content: String): RinLibrary {
        val target = File(libDir(project), library.name)
        target.writeText(content)
        return RinLibrary(library.name, target, target.length(), target.lastModified())
    }

    fun deleteLibrary(library: RinLibrary): Boolean = library.file.delete()

    private fun ensureLibExtension(name: String): String =
        if (name.endsWith(LIB_EXTENSION)) name else "$name$LIB_EXTENSION"

    /**
     * "رفع" مكتبة من الجهاز (أو أي مزوّد SAF) إلى داخل lib/ الخاص بالمشروع، مثل
     * [importFileFromUri] تماماً لكن للمكتبات: يضمن امتداد .og.rin دوماً حتى لو كان
     * اسم الملف الأصلي على الجهاز مختلفاً (مثل mylib.rin أو mylib.txt).
     */
    fun importLibraryFromUri(context: Context, project: Project, uri: Uri): RinLibrary {
        val resolver: ContentResolver = context.contentResolver
        val displayName = queryDisplayName(resolver, uri) ?: uri.lastPathSegment ?: "imported"
        val text = resolver.openInputStream(uri)?.bufferedReader()?.use { it.readText() }
            ?: throw IllegalStateException("تعذّرت قراءة المكتبة المحددة")

        val baseName = sanitizeFileName(displayName)
            .removeSuffix(".rin").removeSuffix(LIB_EXTENSION).removeSuffix(".txt")
        var safeName = ensureLibExtension(baseName.ifBlank { "library" })
        var target = File(libDir(project), safeName)
        var counter = 1
        while (target.exists()) {
            val base = safeName.removeSuffix(LIB_EXTENSION)
            safeName = "${base}_$counter$LIB_EXTENSION"
            target = File(libDir(project), safeName)
            counter++
        }
        target.writeText(text)
        return RinLibrary(safeName, target, target.length(), target.lastModified())
    }

    // ---- رفع أرشيف مضغوط (ZIP) وفك ضغطه داخل المشروع، وتنزيل المشروع كأرشيف ----
    //
    // "رفع مضغوط": يقرأ ملف .zip من [uri] عبر ContentResolver ويفكّ ضغطه مباشرة داخل
    // مجلد المشروع، محافظاً على بنية المجلدات الداخلية للأرشيف (مثل lib/mylib.og.rin).
    // نتحقق من كل مسار داخل الأرشيف حتى لا يخرج ("Zip Slip") إلى خارج مجلد المشروع.

    /** يفكّ ضغط أرشيف ZIP من [uri] داخل مجلد المشروع، ويرجع عدد الملفات المستخرجة. */
    fun importZipFromUri(context: Context, project: Project, uri: Uri): Int {
        val resolver: ContentResolver = context.contentResolver
        val projectRoot = project.dir.canonicalFile
        var extractedCount = 0

        val input = resolver.openInputStream(uri)
            ?: throw IllegalStateException("تعذّرت قراءة الأرشيف المحدد")
        ZipInputStream(input).use { zip ->
            var entry: ZipEntry? = zip.nextEntry
            while (entry != null) {
                val safeRelPath = sanitizeZipEntryPath(entry.name)
                if (safeRelPath != null) {
                    val outFile = File(projectRoot, safeRelPath)
                    // تأكيد إضافي أن المسار الناتج ما زال داخل مجلد المشروع فعلياً.
                    if (outFile.canonicalFile.path.startsWith(projectRoot.path + File.separator)) {
                        if (entry.isDirectory) {
                            outFile.mkdirs()
                        } else {
                            outFile.parentFile?.mkdirs()
                            BufferedOutputStream(FileOutputStream(outFile)).use { out ->
                                zip.copyTo(out)
                            }
                            extractedCount++
                        }
                    }
                }
                zip.closeEntry()
                entry = zip.nextEntry
            }
        }
        return extractedCount
    }

    /** ينظّف مسار عنصر داخل الأرشيف ويرفض أي محاولة خروج خارج مجلد المشروع (../، مسار مطلق). */
    private fun sanitizeZipEntryPath(rawName: String): String? {
        val normalized = rawName.replace('\\', '/').trim('/')
        if (normalized.isEmpty()) return null
        val segments = normalized.split('/').filter { it.isNotEmpty() && it != "." }
        if (segments.any { it == ".." }) return null
        return segments.joinToString(File.separator)
    }

    /**
     * يضغط كل ملفات مجلد المشروع (بما فيها lib/) داخل أرشيف ZIP واحد في cacheDir، تمهيداً
     * لتنزيله عبر [RinDownloadManager]. يرجع الملف الناتج مع اسم عرض مناسب.
     */
    fun exportProjectAsZip(context: Context, project: Project): File {
        val cacheDir = File(context.cacheDir, "project_exports").apply { mkdirs() }
        val zipFile = File(cacheDir, "${project.name}.zip")
        if (zipFile.exists()) zipFile.delete()

        ZipOutputStream(BufferedOutputStream(FileOutputStream(zipFile))).use { zipOut ->
            val root = project.dir
            root.walkTopDown().filter { it.isFile }.forEach { file ->
                val relPath = file.relativeTo(root).path.replace(File.separatorChar, '/')
                zipOut.putNextEntry(ZipEntry(relPath))
                file.inputStream().use { it.copyTo(zipOut) }
                zipOut.closeEntry()
            }
        }
        return zipFile
    }

    private fun queryDisplayName(resolver: ContentResolver, uri: Uri): String? {
        return try {
            resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) cursor.getString(idx) else null
                } else null
            }
        } catch (t: Throwable) {
            null
        }
    }
}

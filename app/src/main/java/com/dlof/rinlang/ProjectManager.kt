package com.dlof.rinlang

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File

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

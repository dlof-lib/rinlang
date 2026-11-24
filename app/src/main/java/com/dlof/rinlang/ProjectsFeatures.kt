package com.dlof.rinlang

import android.content.Context
import java.io.File
import java.util.Locale

/**
 * تثبيت المشاريع في أعلى شاشة «المشاريع». الأسماء تُحفظ في SharedPreferences (نفس ملف إعدادات عرض
 * الشاشة rin_projects_view)، فلا نلمس مجلد المشروع ولا ملف project.og.urin.
 */
object ProjectPins {
    private const val PREFS = "rin_projects_view"
    private const val KEY = "pinned_names"

    private fun prefs(ctx: Context) = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun all(ctx: Context): Set<String> = prefs(ctx).getStringSet(KEY, emptySet())?.toSet() ?: emptySet()

    fun isPinned(ctx: Context, name: String): Boolean = name in all(ctx)

    /** يبدّل حالة التثبيت ويعيد الحالة الجديدة (true = مثبّت الآن). */
    fun toggle(ctx: Context, name: String): Boolean {
        val set = all(ctx).toMutableSet()
        val nowPinned = if (set.contains(name)) { set.remove(name); false } else { set.add(name); true }
        prefs(ctx).edit().putStringSet(KEY, set).apply()
        return nowPinned
    }

    /** عند إعادة تسمية مشروع مثبّت يبقى مثبّتاً بالاسم الجديد. */
    fun rename(ctx: Context, oldName: String, newName: String) {
        val set = all(ctx).toMutableSet()
        if (set.remove(oldName)) {
            set.add(newName)
            prefs(ctx).edit().putStringSet(KEY, set).apply()
        }
    }

    /** يحذف من المثبّتات أي اسم لم يعد له مشروع (حذف/نقل إلى ألبوم). */
    fun prune(ctx: Context, existing: Set<String>) {
        val set = all(ctx)
        val kept = set.filter { it in existing }.toSet()
        if (kept.size != set.size) prefs(ctx).edit().putStringSet(KEY, kept).apply()
    }
}

/** إحصاءات مشروع: عدد الملفات والمجلدات وحجمها الكلي وآخر تعديل في أي ملف داخله. */
data class ProjectStats(val files: Int, val folders: Int, val bytes: Long, val lastModified: Long)

object ProjectOps {

    fun stats(dir: File): ProjectStats {
        var files = 0
        var folders = 0
        var bytes = 0L
        var last = dir.lastModified()
        dir.walkTopDown().forEach { f ->
            if (f == dir) return@forEach
            if (f.name.startsWith(".")) return@forEach
            if (f.isDirectory) folders++
            else if (f.isFile) { files++; bytes += f.length() }
            if (f.lastModified() > last) last = f.lastModified()
        }
        return ProjectStats(files, folders, bytes, last)
    }

    fun formatSize(bytes: Long): String = when {
        bytes < 1024L -> "$bytes B"
        bytes < 1024L * 1024L -> String.format(Locale.US, "%.1f KB", bytes / 1024.0)
        bytes < 1024L * 1024L * 1024L -> String.format(Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0))
        else -> String.format(Locale.US, "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0))
    }

    /**
     * ينسخ مشروعاً بكل ملفاته إلى مشروع جديد باسم «الاسم_copy» (أو _copy2 ...). المشروع الجديد يأخذ
     * اسماً ومعرّفاً (id) جديدين في project.og.urin كي لا يتعارض مع الأصل عند التصدير RinPak.
     * عملية قد تطول للمشاريع الكبيرة: تُستدعى من خيط خلفي.
     */
    fun duplicate(project: Project): Project {
        val parent = project.dir.parentFile ?: throw IllegalStateException("no parent dir")
        val suffix = "_copy"
        val base = project.name.take(64 - suffix.length - 3)
        var n = 1
        var name: String
        do {
            name = if (n == 1) base + suffix else base + suffix + n
            n++
        } while (File(parent, name).exists())
        require(ProjectManager.isValidProjectName(name)) { "invalid project name: $name" }

        val dest = File(parent, name)
        val ok = try {
            project.dir.copyRecursively(dest, overwrite = false)
        } catch (t: Throwable) {
            dest.deleteRecursively()
            throw t
        }
        if (!ok) {
            dest.deleteRecursively()
            throw IllegalStateException("copy failed")
        }
        runCatching {
            RinProjectMeta.update(dest, project.type, name) { it.copy(name = name, id = RinPak.makeId(name)) }
        }
        return Project(name, dest, dest.lastModified(), project.type)
    }
}

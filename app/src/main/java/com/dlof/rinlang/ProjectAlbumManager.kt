package com.dlof.rinlang

import android.content.Context
import java.io.File

/** Lightweight on-device project albums. Projects remain normal folders and are moved safely. */
object ProjectAlbumManager {
    private const val ALBUMS_DIR = "albums"

    private fun root(context: Context): File = File(context.filesDir, ALBUMS_DIR).also { if (!it.exists()) it.mkdirs() }

    fun listAlbums(context: Context): List<String> =
        root(context).listFiles { f -> f.isDirectory }?.map { it.name }?.sortedWith(String.CASE_INSENSITIVE_ORDER) ?: emptyList()

    fun createAlbum(context: Context, name: String) {
        val clean = name.trim()
        require(clean.isNotEmpty() && clean.length <= 64 && clean.matches(Regex("^[A-Za-z0-9_\\-\u0600-\u06FF ]+$"))) { "اسم الألبوم غير صالح" }
        val dir = File(root(context), clean)
        require(!dir.exists()) { "يوجد ألبوم بهذا الاسم" }
        dir.mkdirs()
    }

    fun deleteAlbum(context: Context, name: String) {
        val dir = File(root(context), name)
        if (dir.isDirectory) dir.deleteRecursively()
    }

    fun moveProjectToAlbum(context: Context, project: Project, album: String?) {
        val targetParent = if (album.isNullOrBlank()) File(context.filesDir, "projects") else File(root(context), album)
        if (!targetParent.exists()) targetParent.mkdirs()
        val source = project.dir
        val target = File(targetParent, project.name)
        require(source.canonicalFile != target.canonicalFile) { "المشروع موجود بالفعل في هذا المكان" }
        require(!target.exists()) { "يوجد مشروع بنفس الاسم في الألبوم" }
        require(source.renameTo(target)) { "تعذر نقل المشروع" }
    }

    fun projectsInAlbum(context: Context, album: String): List<Project> {
        val dir = File(root(context), album)
        return (dir.listFiles { f -> f.isDirectory } ?: emptyArray()).map {
            Project(it.name, it, it.lastModified(), ProjectManager.readProjectTypeForAlbum(it))
        }.sortedByDescending { it.lastModified }
    }
}

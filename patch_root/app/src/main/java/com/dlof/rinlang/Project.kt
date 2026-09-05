package com.dlof.rinlang

import java.io.File

/**
 * نوع مشروع Rin، يُختار عند الإنشاء ويُحفظ داخل ملف project.og.urin في جذر المشروع
 * (انظر [ProjectManager]). يحدّد أي قالب ابتدائي (main.rin) يُنشأ للمشروع، ويُعرض
 * كشارة صغيرة ضمن قائمة المشاريع.
 */
enum class ProjectType(val id: String) {
    CONTAINER("container"),
    TABLE("table"),
    UI("ui"),
    FREE("free"),
    ILLUST("illust");

    companion object {
        /** يحوّل معرّفاً نصياً (كما يُقرأ من project.og.urin) إلى [ProjectType]، أو FREE لأي قيمة غير معروفة/غائبة. */
        fun fromId(id: String?): ProjectType = values().firstOrNull { it.id == id } ?: FREE
    }
}

/** يمثّل مشروع Rin واحد: مجلد داخل تخزين التطبيق الخاص يحوي ملفات .rin ومجلد rin_installed/ خاص به. */
data class Project(
    val name: String,
    val dir: File,
    val lastModified: Long,
    val type: ProjectType = ProjectType.FREE
)

/**
 * يمثّل ملف .rin واحد (أو أي ملف آخر مرفوع) داخل مشروع، ربما داخل مجلد فرعي.
 * [relPath] هو المسار النسبي الكامل من جذر المشروع بفواصل "/" (مثل "assets/data.rin")،
 * بينما [name] يبقى اسم الملف وحده (بدون مجلده) لعرضه في الواجهة.
 */
data class RinFile(
    val name: String,
    val file: File,
    val sizeBytes: Long,
    val lastModified: Long,
    val relPath: String = name
)

/** يمثّل مجلداً فرعياً داخل مشروع (أُنشئ يدوياً، أو عبر "/" داخل اسم ملف جديد، أو من فك ضغط أرشيف). */
data class RinFolder(
    val name: String,
    val relPath: String,
    val dir: File,
    val lastModified: Long
)

package com.dlof.rinlang

import java.io.File

/** يمثّل مشروع Rin واحد: مجلد داخل تخزين التطبيق الخاص يحوي ملفات .rin ومجلد rin_installed/ خاص به. */
data class Project(
    val name: String,
    val dir: File,
    val lastModified: Long
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

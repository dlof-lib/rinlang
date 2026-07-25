package com.dlof.rinlang

import java.io.File

/** يمثّل مشروع Rin واحد: مجلد داخل تخزين التطبيق الخاص يحوي ملفات .rin ومجلد rin_installed/ خاص به. */
data class Project(
    val name: String,
    val dir: File,
    val lastModified: Long
)

/** يمثّل ملف .rin واحد داخل مشروع. */
data class RinFile(
    val name: String,
    val file: File,
    val sizeBytes: Long,
    val lastModified: Long
)

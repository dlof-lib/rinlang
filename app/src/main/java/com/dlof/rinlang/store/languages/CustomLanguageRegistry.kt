package com.dlof.rinlang.store.languages

import android.content.Context
import org.json.JSONObject
import java.io.File

/** سجل واحد: لغة مخصصة واحدة معروفة للمحرر، مع مسار مشروعها لإيجاد syntax.rinsyntax.json عند التلوين. */
data class CustomLanguageRecord(
    val id: String,
    val name: String,
    val fileExtension: String,
    val projectDirPath: String,
    val developer: String,
    val installedAt: Long
)

/**
 * يتتبّع كل اللغات المخصصة المعروفة على الجهاز (سواء أُنشئت محلياً عبر
 * [CustomLanguageProjectScaffolder] أو ثُبِّتت من متجر الإضافات عبر [LanguageMarketplacePublisher]/
 * ExtensionManager بنوع LANGUAGE)، عبر ملف صغير "rin_extensions/.installed_languages.json" —
 * بنفس فلسفة [com.dlof.rinlang.store.extensions.InstalledExtensionsIndex] تماماً، لكن مفهرَس
 * بامتداد الملف (fileExtension) لا بمعرّف الإضافة، لأن هذا ما يحتاجه المحرر فعلياً: "أي ملف ينتهي
 * بـ .calc يفتح بأي لغة، وأين أجد قواعد تلوينه؟"
 */
object CustomLanguageRegistry {

    private const val INDEX_FILE_NAME = ".installed_languages.json"
    private const val EXTENSIONS_DIR_NAME = "rin_extensions"

    private fun indexFile(context: Context): File {
        val root = File(context.filesDir, EXTENSIONS_DIR_NAME)
        if (!root.exists()) root.mkdirs()
        return File(root, INDEX_FILE_NAME)
    }

    fun readAll(context: Context): Map<String, CustomLanguageRecord> {
        val file = indexFile(context)
        if (!file.exists()) return emptyMap()
        return try {
            val json = JSONObject(file.readText(Charsets.UTF_8))
            val result = LinkedHashMap<String, CustomLanguageRecord>()
            json.keys().forEach { fileExt ->
                val o = json.getJSONObject(fileExt)
                result[fileExt] = CustomLanguageRecord(
                    id = o.optString("id"),
                    name = o.optString("name"),
                    fileExtension = fileExt,
                    projectDirPath = o.optString("projectDirPath"),
                    developer = o.optString("developer"),
                    installedAt = o.optLong("installedAt")
                )
            }
            result
        } catch (t: Throwable) {
            emptyMap()
        }
    }

    private fun writeAll(context: Context, records: Map<String, CustomLanguageRecord>) {
        val json = JSONObject()
        records.forEach { (fileExt, record) ->
            json.put(fileExt, JSONObject().apply {
                put("id", record.id)
                put("name", record.name)
                put("projectDirPath", record.projectDirPath)
                put("developer", record.developer)
                put("installedAt", record.installedAt)
            })
        }
        indexFile(context).writeText(json.toString(), Charsets.UTF_8)
    }

    /** يسجّل/يحدّث لغة مخصصة في الفهرس بعد إنشائها أو تثبيتها. */
    fun register(context: Context, manifest: CustomLanguageManifest, projectDir: File) {
        val current = readAll(context).toMutableMap()
        current[manifest.fileExtension] = CustomLanguageRecord(
            id = manifest.id,
            name = manifest.name,
            fileExtension = manifest.fileExtension,
            projectDirPath = projectDir.absolutePath,
            developer = manifest.developer,
            installedAt = System.currentTimeMillis()
        )
        writeAll(context, current)
    }

    fun unregister(context: Context, fileExtension: String) {
        val current = readAll(context).toMutableMap()
        current.remove(fileExtension)
        writeAll(context, current)
    }

    /** يبحث عن لغة مخصصة معروفة تطابق امتداد ملف معطى (بلا نقطة، مثل "calc")، إن وُجدت. */
    fun findByExtension(context: Context, fileExtension: String): CustomLanguageRecord? =
        readAll(context)[fileExtension.lowercase()]

    /** يقرأ manifest.json الكامل للغة مسجَّلة، لإعادة التحقق أو عرض تفاصيلها. */
    fun manifestOf(record: CustomLanguageRecord): CustomLanguageManifest? =
        CustomLanguageManifest.read(File(record.projectDirPath))
}

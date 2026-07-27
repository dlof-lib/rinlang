package com.dlof.rinlang.store.extensions

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * قراءة/كتابة ملف "extension.rinext" — ملف تعريف كل إضافة بصيغة JSON، بنفس الحقول التي
 * تُخزَّن أيضاً في Realtime Database عبر [RinExtension]. هذا الملف هو ما يُحزَم فعلياً داخل
 * أرشيف الإضافة (base64Data) ويُستخرَج إلى مجلد التثبيت المحلي عند [ExtensionManager.install].
 *
 * مثال:
 * {
 *   "name": "Rin UI Designer",
 *   "version": "1.0.0",
 *   "developer": "Rin Team",
 *   "permissions": ["project.read", "project.write", "editor.access"],
 *   "languages": ["Rin", "Kotlin", "JavaScript"],
 *   "screenshots": [],
 *   "changelog": []
 * }
 */
object ExtensionManifestFile {

    const val MANIFEST_FILE_NAME = "extension.rinext"

    /** يحوّل [RinExtension] إلى JSON بصيغة extension.rinext (بدون base64Data الثقيل). */
    fun toJson(ext: RinExtension): JSONObject = JSONObject().apply {
        put("id", ext.id)
        put("name", ext.name)
        put("version", ext.version)
        put("developer", ext.developer)
        put("developerUid", ext.developerUid)
        put("description", ext.description)
        put("type", ext.type)
        put("permissions", JSONArray(ext.permissions))
        put("languages", JSONArray(ext.languages))
        put("screenshots", JSONArray(ext.screenshots.map { s ->
            JSONObject().apply { put("caption", s.caption); put("base64Data", s.base64Data) }
        }))
        put("changelog", JSONArray(ext.changelog.map { c ->
            JSONObject().apply { put("version", c.version); put("date", c.date); put("notes", c.notes) }
        }))
        put("releaseDate", ext.releaseDate)
        put("sizeBytes", ext.sizeBytes)
        put("fileName", ext.fileName)
        put("signature", ext.signature)
    }

    /** يقرأ extension.rinext من نص JSON خام، ويدمجه مع [base64Data] الممرَّر بشكل منفصل. */
    fun fromJson(json: JSONObject, base64Data: String = ""): RinExtension {
        val permissions = json.optJSONArray("permissions")?.let { arr ->
            (0 until arr.length()).map { arr.getString(it) }
        } ?: emptyList()
        val languages = json.optJSONArray("languages")?.let { arr ->
            (0 until arr.length()).map { arr.getString(it) }
        } ?: emptyList()
        val screenshots = json.optJSONArray("screenshots")?.let { arr ->
            (0 until arr.length()).map { i ->
                val o = arr.getJSONObject(i)
                ExtensionScreenshot(o.optString("base64Data"), o.optString("caption"))
            }
        } ?: emptyList()
        val changelog = json.optJSONArray("changelog")?.let { arr ->
            (0 until arr.length()).map { i ->
                val o = arr.getJSONObject(i)
                ExtensionChangelogEntry(o.optString("version"), o.optLong("date"), o.optString("notes"))
            }
        } ?: emptyList()

        return RinExtension(
            id = json.optString("id"),
            name = json.optString("name"),
            version = json.optString("version", "1.0.0"),
            developer = json.optString("developer"),
            developerUid = json.optString("developerUid"),
            description = json.optString("description"),
            type = json.optString("type", ExtensionType.EXTENSION.id),
            permissions = permissions,
            languages = languages,
            screenshots = screenshots,
            changelog = changelog,
            releaseDate = json.optLong("releaseDate"),
            sizeBytes = json.optLong("sizeBytes"),
            fileName = json.optString("fileName", MANIFEST_FILE_NAME),
            base64Data = base64Data,
            signature = json.optString("signature")
        )
    }

    fun write(dir: File, ext: RinExtension) {
        File(dir, MANIFEST_FILE_NAME).writeText(toJson(ext).toString(2), Charsets.UTF_8)
    }

    fun read(dir: File): RinExtension? {
        val file = File(dir, MANIFEST_FILE_NAME)
        if (!file.exists()) return null
        return try {
            fromJson(JSONObject(file.readText(Charsets.UTF_8)))
        } catch (t: Throwable) {
            null
        }
    }
}

/** حالة إضافة مثبَّتة محلياً على الجهاز (تطبيق-عام، وليست محصورة بمشروع واحد). */
data class InstalledExtensionRecord(
    val id: String,
    val name: String,
    val version: String,
    val type: String,
    val developer: String,
    val enabled: Boolean,
    val installedAt: Long
)

/**
 * يتتبّع كل الإضافات المثبَّتة على الجهاز عبر ملف صغير مخفي
 * "rin_extensions/.installed_extensions.json" — مصدر الحقيقة الوحيد لسؤال
 * "ما الإضافات المثبَّتة الآن، وأيّها مفعَّلة؟" (بمعزل عن أي مشروع بعينه، لأن الإضافات
 * على مستوى المحرر/التطبيق، بخلاف مكتبات لغة Rin التي تبقى خاصة بكل مشروع).
 */
object InstalledExtensionsIndex {

    private const val INDEX_FILE_NAME = ".installed_extensions.json"

    private fun indexFile(context: Context): File =
        File(ExtensionManager.extensionsRoot(context), INDEX_FILE_NAME)

    fun readAll(context: Context): Map<String, InstalledExtensionRecord> {
        val file = indexFile(context)
        if (!file.exists()) return emptyMap()
        return try {
            val json = JSONObject(file.readText(Charsets.UTF_8))
            val result = LinkedHashMap<String, InstalledExtensionRecord>()
            json.keys().forEach { id ->
                val o = json.getJSONObject(id)
                result[id] = InstalledExtensionRecord(
                    id = id,
                    name = o.optString("name"),
                    version = o.optString("version"),
                    type = o.optString("type"),
                    developer = o.optString("developer"),
                    enabled = o.optBoolean("enabled", true),
                    installedAt = o.optLong("installedAt")
                )
            }
            result
        } catch (t: Throwable) {
            emptyMap()
        }
    }

    private fun writeAll(context: Context, records: Map<String, InstalledExtensionRecord>) {
        val json = JSONObject()
        records.forEach { (id, record) ->
            json.put(id, JSONObject().apply {
                put("name", record.name)
                put("version", record.version)
                put("type", record.type)
                put("developer", record.developer)
                put("enabled", record.enabled)
                put("installedAt", record.installedAt)
            })
        }
        indexFile(context).writeText(json.toString(), Charsets.UTF_8)
    }

    fun upsert(context: Context, record: InstalledExtensionRecord) {
        val current = readAll(context).toMutableMap()
        current[record.id] = record
        writeAll(context, current)
    }

    fun remove(context: Context, id: String) {
        val current = readAll(context).toMutableMap()
        current.remove(id)
        writeAll(context, current)
    }

    fun setEnabled(context: Context, id: String, enabled: Boolean) {
        val current = readAll(context).toMutableMap()
        val record = current[id] ?: return
        current[id] = record.copy(enabled = enabled)
        writeAll(context, current)
    }

    fun get(context: Context, id: String): InstalledExtensionRecord? = readAll(context)[id]
}

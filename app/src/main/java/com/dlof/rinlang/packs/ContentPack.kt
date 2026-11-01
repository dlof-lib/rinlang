package com.dlof.rinlang.packs

import org.json.JSONObject
import java.net.URL

/** نوع الحزمة القابلة للتنزيل. */
enum class PackType(val json: String) {
    /** حزمة نصوص واجهة للغة (strings.json) تُطبَّق وقت التشغيل — انظر [LanguagePacks]. */
    LANGUAGE("language"),

    /** ملفات أصول عامة تُفك كما هي داخل مجلد الحزمة (قوالب، لغات مخصّصة، صور...). */
    ASSET("asset");

    companion object {
        fun fromJson(value: String?): PackType? = values().firstOrNull { it.json == value }
    }
}

/**
 * وصف حزمة كما يرد في catalog.json (يبنيه scripts/build_content_packs.py).
 * [sha256] يُفحص بعد التنزيل قبل أي فك ضغط، و[sizeBytes] هو حجم ملف الـ zip نفسه.
 */
data class ContentPack(
    val id: String,
    val type: PackType,
    val language: String?,
    val version: Int,
    val title: String,
    val description: String,
    val sizeBytes: Long,
    val installedBytes: Long,
    val sha256: String,
    val url: String
) {
    companion object {
        private val ID_REGEX = Regex("[a-z0-9][a-z0-9._-]{0,63}")

        fun isValidId(id: String): Boolean = ID_REGEX.matches(id)

        /** يعيد null للعناصر غير الصالحة (id مشبوه، نوع مجهول، sha غير صالح، رابط غير https) بدل إسقاط القائمة كلها. */
        fun fromJson(o: JSONObject, catalogUrl: String): ContentPack? {
            val id = o.optString("id")
            val type = PackType.fromJson(o.optString("type")) ?: return null
            val sha = o.optString("sha256").lowercase()
            if (!isValidId(id) || !Regex("[0-9a-f]{64}").matches(sha)) return null
            val url = try {
                URL(URL(catalogUrl), o.optString("url")).toString()
            } catch (e: Exception) {
                return null
            }
            if (!url.startsWith("https://")) return null
            val language = o.optString("language").ifEmpty { null }
            if (type == PackType.LANGUAGE && (language == null || !Regex("[a-z]{2,3}").matches(language))) return null
            return ContentPack(
                id = id,
                type = type,
                language = language,
                version = o.optInt("version", 1),
                title = o.optString("title", id),
                description = o.optString("description"),
                sizeBytes = o.optLong("sizeBytes"),
                installedBytes = o.optLong("installedBytes"),
                sha256 = sha,
                url = url
            )
        }
    }
}

/** حالة حزمة مثبّتة على الجهاز (من ملف العلامة داخل مجلدها). */
data class InstalledPack(
    val id: String,
    val version: Int,
    val sha256: String,
    val installedAt: Long,
    val bytesOnDisk: Long
)

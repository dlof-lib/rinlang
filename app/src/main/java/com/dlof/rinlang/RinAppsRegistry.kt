package com.dlof.rinlang

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * سجل واحد لعملية تصدير APK ناجحة (انظر [com.dlof.rinlang.apk.RinApkExporter]) — يُغذّي
 * شاشة "تطبيقات Rin" ([RinAppsActivity]) وشاشتها التفصيلية ([RinAppSettingsActivity]).
 */
data class RinAppRecord(
    val id: String,
    val projectName: String,
    val displayName: String,
    val applicationId: String,
    val apkFileName: String,
    val sizeBytes: Long,
    val exportedAt: Long,
    val entryFile: String,
    val signedWithV2: Boolean
) {
    fun apkFile(context: Context): File = File(RinAppsRegistry.appsDir(context), apkFileName)
}

/**
 * سجل التطبيقات المُصدَّرة (تطبيقات Rin): يخزّن بيانات وصفية عن كل عملية تصدير APK ناجحة في
 * ملف JSON دائم (filesDir/rin_apps_registry.json)، بينما تُخزَّن حزم APK نفسها في مجلد دائم
 * أيضاً (filesDir/rin_apps/) بدل cacheDir الذي كان يُستخدَم سابقاً — cacheDir قد يُطهَّر
 * تلقائياً من نظام أندرويد وقت الحاجة للمساحة، ما كان يعني اختفاء أي تصدير سابق بصمت؛ الآن
 * تبقى شاشة "تطبيقات Rin" قادرة على سرد وتثبيت/مشاركة/حذف أي تصدير سابق حتى بعد إعادة
 * تشغيل الجهاز أو مرور وقت طويل، تماماً كسجل تطبيقات حقيقي.
 *
 * كل عملية تصدير (حتى لنفس المشروع مرتين) تُسجَّل كسجل منفصل بمعرّف حزمة مستقل (انظر
 * buildApplicationId في RinApkExporter) — فتُعرض شاشة "تطبيقات Rin" كتاريخ كامل لكل تصدير،
 * لا كنسخة وحيدة لكل مشروع تُستبدَل في كل مرة.
 */
object RinAppsRegistry {
    private const val REGISTRY_FILE = "rin_apps_registry.json"
    private const val APPS_DIR = "rin_apps"

    fun appsDir(context: Context): File = File(context.filesDir, APPS_DIR).apply { mkdirs() }

    private fun registryFile(context: Context): File = File(context.filesDir, REGISTRY_FILE)

    @Synchronized
    fun listAll(context: Context): List<RinAppRecord> {
        val file = registryFile(context)
        if (!file.exists()) return emptyList()
        return try {
            val arr = JSONArray(file.readText(Charsets.UTF_8))
            (0 until arr.length()).mapNotNull { i -> fromJson(arr.optJSONObject(i)) }
                .sortedByDescending { it.exportedAt }
        } catch (e: Exception) {
            emptyList()
        }
    }

    fun find(context: Context, id: String): RinAppRecord? = listAll(context).find { it.id == id }

    @Synchronized
    fun add(context: Context, record: RinAppRecord) {
        val current = listAll(context).toMutableList()
        current.removeAll { it.id == record.id }
        current.add(0, record)
        save(context, current)
    }

    /** يحذف السجل وملف الـ APK المرتبط به معاً. */
    @Synchronized
    fun remove(context: Context, id: String) {
        val current = listAll(context).toMutableList()
        val removed = current.find { it.id == id }
        current.removeAll { it.id == id }
        save(context, current)
        removed?.let { r -> r.apkFile(context).takeIf { it.exists() }?.delete() }
    }

    private fun save(context: Context, records: List<RinAppRecord>) {
        val arr = JSONArray()
        records.forEach { arr.put(toJson(it)) }
        registryFile(context).writeText(arr.toString(), Charsets.UTF_8)
    }

    private fun toJson(r: RinAppRecord): JSONObject = JSONObject().apply {
        put("id", r.id)
        put("project_name", r.projectName)
        put("display_name", r.displayName)
        put("application_id", r.applicationId)
        put("apk_file_name", r.apkFileName)
        put("size_bytes", r.sizeBytes)
        put("exported_at", r.exportedAt)
        put("entry_file", r.entryFile)
        put("signed_with_v2", r.signedWithV2)
    }

    private fun fromJson(o: JSONObject?): RinAppRecord? {
        if (o == null) return null
        return try {
            RinAppRecord(
                id = o.getString("id"),
                projectName = o.optString("project_name"),
                displayName = o.optString("display_name"),
                applicationId = o.getString("application_id"),
                apkFileName = o.getString("apk_file_name"),
                sizeBytes = o.optLong("size_bytes"),
                exportedAt = o.optLong("exported_at"),
                entryFile = o.optString("entry_file", "main.rin"),
                signedWithV2 = o.optBoolean("signed_with_v2", false)
            )
        } catch (e: Exception) {
            null
        }
    }
}

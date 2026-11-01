package com.dlof.rinlang.store

import com.dlof.rinlang.Project
import com.dlof.rinlang.ProjectManager
import org.json.JSONObject
import java.io.File

/** سجل حزمة واحدة مثبَّتة عبر متجر Rin داخل مشروع (اسم الحزمة، إصدارها، والملف الذي تُثبَّت فيه). */
data class InstalledPackageRecord(
    val packageName: String,
    val version: String,
    val libraryFileName: String
)

/**
 * يتتبّع أي حزم متجر Rin مثبَّتة فعلياً داخل كل مشروع، عبر ملف صغير مخفي
 * "lib/.rin_store_manifest.json" — ضروري لأن ملفات lib/ *.og.rin نفسها لا تحمل رقم إصدار،
 * فهذا هو المصدر الوحيد لمعرفة "ما الإصدار المثبَّت حالياً؟" عند التحقق من التبعيات.
 */
object PackageManifest {

    private const val MANIFEST_FILE_NAME = ".rin_store_manifest.json"

    private fun manifestFile(project: Project): File =
        File(ProjectManager.libDir(project), MANIFEST_FILE_NAME)

    /** يقرأ كل الحزم المثبَّتة حالياً في المشروع: اسم الحزمة -> السجل. */
    fun readAll(project: Project): Map<String, InstalledPackageRecord> {
        val file = manifestFile(project)
        if (!file.exists()) return emptyMap()
        return try {
            val json = JSONObject(file.readText(Charsets.UTF_8))
            val result = LinkedHashMap<String, InstalledPackageRecord>()
            json.keys().forEach { key ->
                val entry = json.getJSONObject(key)
                result[key] = InstalledPackageRecord(
                    packageName = key,
                    version = entry.optString("version", "0.0.0"),
                    libraryFileName = entry.optString("libraryFileName", "")
                )
            }
            result
        } catch (t: Throwable) {
            emptyMap()
        }
    }

    /** يسجّل/يحدّث حزمة كمثبَّتة بعد تثبيتها بنجاح. */
    fun recordInstalled(project: Project, packageName: String, version: String, libraryFileName: String) {
        val current = readAll(project).toMutableMap()
        current[packageName] = InstalledPackageRecord(packageName, version, libraryFileName)
        val json = JSONObject()
        current.forEach { (name, record) ->
            json.put(name, JSONObject().apply {
                put("version", record.version)
                put("libraryFileName", record.libraryFileName)
            })
        }
        manifestFile(project).writeText(json.toString(), Charsets.UTF_8)
    }
}

/** نتيجة التحقق من تبعيات حزمة قبل تثبيتها. */
data class DependencyCheckResult(
    /** تبعيات غير مثبَّتة إطلاقاً: اسم الحزمة -> شرط الإصدار المطلوب. */
    val missing: List<Pair<String, String>>,
    /** تبعيات مثبَّتة لكن بإصدار لا يحقّق الشرط: اسم الحزمة -> (المطلوب، المثبَّت حالياً). */
    val versionMismatch: List<Triple<String, String, String>>
) {
    val isSatisfied: Boolean get() = missing.isEmpty() && versionMismatch.isEmpty()
}

object DependencyResolver {

    /** يتحقّق من تبعيات [pkg] مقابل الحزم المثبَّتة فعلياً في [project]. */
    fun check(project: Project, pkg: RinPackage): DependencyCheckResult {
        if (pkg.dependencies.isEmpty()) return DependencyCheckResult(emptyList(), emptyList())
        val installed = PackageManifest.readAll(project)
        val missing = mutableListOf<Pair<String, String>>()
        val mismatch = mutableListOf<Triple<String, String, String>>()

        pkg.dependencies.forEach { (depName, requirement) ->
            val record = installed[depName]
            if (record == null) {
                missing.add(depName to requirement)
            } else if (!VersionUtils.satisfies(record.version, requirement)) {
                mismatch.add(Triple(depName, requirement, record.version))
            }
        }
        return DependencyCheckResult(missing, mismatch)
    }

    /** يبحث عن أفضل حزمة في [catalog] تحقّق اسم [depName] وشرط [requirement] (الأحدث أولاً). */
    fun findSatisfying(catalog: List<RinPackage>, depName: String, requirement: String): RinPackage? =
        catalog.filter { it.name.equals(depName, ignoreCase = true) && VersionUtils.satisfies(it.version, requirement) }
            .maxByOrNull { VersionUtils.parse(it.version).let { v -> (v.getOrElse(0){0} * 1_000_000) + (v.getOrElse(1){0} * 1000) + v.getOrElse(2){0} } }
}

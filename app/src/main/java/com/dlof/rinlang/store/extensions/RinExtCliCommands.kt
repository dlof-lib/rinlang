package com.dlof.rinlang.store.extensions

import android.content.Context

/**
 * محرِّك أوامر "rin ext ..." الطرفية داخل تطبيق Rin (شريط أوامر نصي داخل متجر الإضافات،
 * راجع [RinExtensionsMarketplaceActivity]). يفسّر سطراً نصياً واحداً وينفّذ الإجراء المطابق
 * عبر [ExtensionManager] و[ExtensionRepository]، ثم يعيد سطور ناتج جاهزة للعرض—بنفس روح
 * ناتج تشغيل الكود (Console) في بقية التطبيق.
 *
 * الأوامر المدعومة:
 *   rin ext install <extension>
 *   rin ext update <extension>
 *   rin ext remove <extension>
 *   rin ext search <name>
 *   rin ext list
 */
object RinExtCliCommands {

    /** ينفّذ سطر أمر واحد [rawCommand]، ويعيد نتيجته كأسطر نصية عبر [callback]. */
    fun execute(context: Context, rawCommand: String, callback: (List<String>) -> Unit) {
        val tokens = rawCommand.trim().split(Regex("\\s+")).filter { it.isNotBlank() }
        if (tokens.size < 2 || tokens[0] != "rin" || tokens[1] != "ext") {
            callback(listOf("✗ أمر غير معروف. الصيغة: rin ext <install|update|remove|search|list> [اسم]"))
            return
        }

        val sub = tokens.getOrNull(2)
        val arg = tokens.drop(3).joinToString(" ").trim()

        when (sub) {
            "install" -> if (arg.isBlank()) missingArg(callback, "install") else installOrUpdate(context, arg, isUpdate = false, callback)
            "update" -> if (arg.isBlank()) missingArg(callback, "update") else installOrUpdate(context, arg, isUpdate = true, callback)
            "remove" -> if (arg.isBlank()) missingArg(callback, "remove") else remove(context, arg, callback)
            "search" -> if (arg.isBlank()) missingArg(callback, "search") else search(arg, callback)
            "list" -> list(context, callback)
            null -> callback(listOf("✗ حدّد أمراً فرعياً: install / update / remove / search / list"))
            else -> callback(listOf("✗ أمر فرعي غير معروف: '$sub'"))
        }
    }

    private fun missingArg(callback: (List<String>) -> Unit, sub: String) {
        callback(listOf("✗ الاستخدام: rin ext $sub <اسم الإضافة>"))
    }

    private fun findByName(all: List<RinExtension>, name: String): RinExtension? {
        val normalized = name.trim().lowercase()
        return all.find { it.name.trim().lowercase() == normalized }
            ?: all.find { it.name.trim().lowercase().contains(normalized) }
    }

    private fun installOrUpdate(context: Context, name: String, isUpdate: Boolean, callback: (List<String>) -> Unit) {
        val verb = if (isUpdate) "تحديث" else "تثبيت"
        ExtensionRepository.fetchAll { all ->
            val ext = findByName(all, name)
            if (ext == null) {
                callback(listOf("✗ لم يُعثر على إضافة باسم '$name' في متجر Rin"))
                return@fetchAll
            }
            if (isUpdate) {
                val installed = ExtensionManager.installedRecord(context, ext.id)
                if (installed == null) {
                    callback(listOf("✗ ${ext.name} غير مثبَّتة أصلاً. استخدم: rin ext install ${ext.name}"))
                    return@fetchAll
                }
                if (VersionUtilsCompare.compare(installed.version, ext.version) >= 0) {
                    callback(listOf("✓ ${ext.name} محدَّثة بالفعل (${installed.version})"))
                    return@fetchAll
                }
            }
            val lines = mutableListOf("⏳ جارٍ $verb ${ext.name} (${ext.version})…")
            val action: (Context, RinExtension, (Int) -> Unit, (ExtensionInstallResult) -> Unit) -> Unit =
                if (isUpdate) ExtensionManager::update else ExtensionManager::install
            action(context, ext, {}) { result ->
                when (result) {
                    is ExtensionInstallResult.Success ->
                        lines.add("✅ تم $verb ${ext.name} بنجاح — الإصدار ${result.record.version}")
                    is ExtensionInstallResult.Failure ->
                        lines.add("✗ فشل $verb ${ext.name}: ${result.message}")
                }
                callback(lines)
            }
        }
    }

    private fun remove(context: Context, name: String, callback: (List<String>) -> Unit) {
        val installed = ExtensionManager.listInstalled(context)
        val record = installed.find { it.name.trim().lowercase() == name.trim().lowercase() }
            ?: installed.find { it.name.trim().lowercase().contains(name.trim().lowercase()) }
        if (record == null) {
            callback(listOf("✗ لا توجد إضافة مثبَّتة باسم '$name'"))
            return
        }
        val ok = ExtensionManager.uninstall(context, record.id)
        callback(listOf(if (ok) "✅ تمت إزالة ${record.name}" else "✗ تعذّرت إزالة ${record.name}"))
    }

    private fun search(name: String, callback: (List<String>) -> Unit) {
        ExtensionRepository.fetchAll { all ->
            val matches = all.filter { it.name.contains(name, ignoreCase = true) || it.description.contains(name, ignoreCase = true) }
            if (matches.isEmpty()) {
                callback(listOf("لا نتائج لـ '$name'"))
                return@fetchAll
            }
            callback(matches.take(20).map { "• ${it.name} (${it.version}) — ${it.developer} [${it.extensionType.id}]" })
        }
    }

    private fun list(context: Context, callback: (List<String>) -> Unit) {
        val installed = ExtensionManager.listInstalled(context)
        if (installed.isEmpty()) {
            callback(listOf("لا توجد إضافات مثبَّتة بعد. استخدم: rin ext search <اسم>"))
            return
        }
        callback(installed.map { r ->
            val status = if (r.enabled) "مفعَّلة" else "معطَّلة"
            "• ${r.name} (${r.version}) — $status [${r.type}]"
        })
    }
}

/** غلاف صغير حول [com.dlof.rinlang.store.VersionUtils.compare] لتفادي استيراد دائري بين الحزم. */
private object VersionUtilsCompare {
    fun compare(a: String, b: String): Int = com.dlof.rinlang.store.VersionUtils.compare(a, b)
}
